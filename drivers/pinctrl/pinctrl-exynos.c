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
	struct samsung_pinctrl_drv_data *d = irqd->domain->host_data;
	struct exynos_geint_data *edata = irq_data_get_irq_handler_data(irqd);
	unsigned long reg_mask = d->ctrl->geint_mask + edata->eint_offset;
	unsigned long mask;

	mask = readl(d->virt_base + reg_mask);
	mask &= ~(1 << edata->pin);
	writel(mask, d->virt_base + reg_mask);
}

static void exynos_gpio_irq_mask(struct irq_data *irqd)
{
	struct samsung_pinctrl_drv_data *d = irqd->domain->host_data;
	struct exynos_geint_data *edata = irq_data_get_irq_handler_data(irqd);
	unsigned long reg_mask = d->ctrl->geint_mask + edata->eint_offset;
	unsigned long mask;

	mask = readl(d->virt_base + reg_mask);
	mask |= 1 << edata->pin;
	writel(mask, d->virt_base + reg_mask);
}

static void exynos_gpio_irq_ack(struct irq_data *irqd)
{
	struct samsung_pinctrl_drv_data *d = irqd->domain->host_data;
	struct exynos_geint_data *edata = irq_data_get_irq_handler_data(irqd);
	unsigned long reg_pend = d->ctrl->geint_pend + edata->eint_offset;

	writel(1 << edata->pin, d->virt_base + reg_pend);
}

static int exynos_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct samsung_pinctrl_drv_data *d = irqd->domain->host_data;
	struct samsung_pin_ctrl *ctrl = d->ctrl;
	struct exynos_geint_data *edata = irq_data_get_irq_handler_data(irqd);
	struct samsung_pin_bank *bank = edata->bank;
	unsigned int shift = EXYNOS_EINT_CON_LEN * edata->pin;
	unsigned int con, trig_type;
	unsigned long reg_con = ctrl->geint_con + edata->eint_offset;
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
	shift = edata->pin * bank->func_width;
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

/*
 * given a controller-local external gpio interrupt number, prepare the handler
 * data for it.
 */
static struct exynos_geint_data *exynos_get_eint_data(irq_hw_number_t hw,
				struct samsung_pinctrl_drv_data *d)
{
	struct samsung_pin_bank *bank = d->ctrl->pin_banks;
	struct exynos_geint_data *eint_data;
	unsigned int nr_banks = d->ctrl->nr_banks, idx;
	unsigned int irq_base = 0, eint_offset = 0;

	if (hw >= d->ctrl->nr_gint) {
		dev_err(d->dev, "unsupported ext-gpio interrupt\n");
		return NULL;
	}

	for (idx = 0; idx < nr_banks; idx++, bank++) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;
		if ((hw >= irq_base) && (hw < (irq_base + bank->nr_pins)))
			break;
		irq_base += bank->nr_pins;
		eint_offset += 4;
	}

	if (idx == nr_banks) {
		dev_err(d->dev, "pin bank not found for ext-gpio interrupt\n");
		return NULL;
	}

	eint_data = devm_kzalloc(d->dev, sizeof(*eint_data), GFP_KERNEL);
	if (!eint_data) {
		dev_err(d->dev, "no memory for eint-gpio data\n");
		return NULL;
	}

	eint_data->bank	= bank;
	eint_data->pin = hw - irq_base;
	eint_data->eint_offset = eint_offset;
	return eint_data;
}

static int exynos_gpio_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct samsung_pinctrl_drv_data *d = h->host_data;
	struct exynos_geint_data *eint_data;

	eint_data = exynos_get_eint_data(hw, d);
	if (!eint_data)
		return -EINVAL;

	irq_set_handler_data(virq, eint_data);
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &exynos_gpio_irq_chip,
					handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

static void exynos_gpio_irq_unmap(struct irq_domain *h, unsigned int virq)
{
	struct samsung_pinctrl_drv_data *d = h->host_data;
	struct exynos_geint_data *eint_data;

	eint_data = irq_get_handler_data(virq);
	devm_kfree(d->dev, eint_data);
}

/*
 * irq domain callbacks for external gpio interrupt controller.
 */
static const struct irq_domain_ops exynos_gpio_irqd_ops = {
	.map	= exynos_gpio_irq_map,
	.unmap	= exynos_gpio_irq_unmap,
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

	virq = irq_linear_revmap(d->gpio_irqd, bank->irq_base + pin);
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
	struct device *dev = d->dev;
	unsigned int ret;

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

	d->gpio_irqd = irq_domain_add_linear(dev->of_node, d->ctrl->nr_gint,
				&exynos_gpio_irqd_ops, d);
	if (!d->gpio_irqd) {
		dev_err(dev, "gpio irq domain allocation failed\n");
		return -ENXIO;
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
	struct device_node *wkup_np;
	struct exynos_weint_data *weint_data;
	int idx, irq;

	wkup_np = of_find_matching_node(dev->of_node, exynos_wkup_irq_ids);
	if (!wkup_np) {
		dev_err(dev, "wakeup controller node not found\n");
		return -ENODEV;
	}

	d->wkup_irqd = irq_domain_add_linear(wkup_np, d->ctrl->nr_wint,
				&exynos_wkup_irqd_ops, d);
	if (!d->gpio_irqd) {
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
	EXYNOS_PIN_BANK_EINTG(0x000, EXYNOS4210_GPIO_A0, "gpa0"),
	EXYNOS_PIN_BANK_EINTG(0x020, EXYNOS4210_GPIO_A1, "gpa1"),
	EXYNOS_PIN_BANK_EINTG(0x040, EXYNOS4210_GPIO_B, "gpb"),
	EXYNOS_PIN_BANK_EINTG(0x060, EXYNOS4210_GPIO_C0, "gpc0"),
	EXYNOS_PIN_BANK_EINTG(0x080, EXYNOS4210_GPIO_C1, "gpc1"),
	EXYNOS_PIN_BANK_EINTG(0x0A0, EXYNOS4210_GPIO_D0, "gpd0"),
	EXYNOS_PIN_BANK_EINTG(0x0C0, EXYNOS4210_GPIO_D1, "gpd1"),
	EXYNOS_PIN_BANK_EINTG(0x0E0, EXYNOS4210_GPIO_E0, "gpe0"),
	EXYNOS_PIN_BANK_EINTG(0x100, EXYNOS4210_GPIO_E1, "gpe1"),
	EXYNOS_PIN_BANK_EINTG(0x120, EXYNOS4210_GPIO_E2, "gpe2"),
	EXYNOS_PIN_BANK_EINTG(0x140, EXYNOS4210_GPIO_E3, "gpe3"),
	EXYNOS_PIN_BANK_EINTG(0x160, EXYNOS4210_GPIO_E4, "gpe4"),
	EXYNOS_PIN_BANK_EINTG(0x180, EXYNOS4210_GPIO_F0, "gpf0"),
	EXYNOS_PIN_BANK_EINTG(0x1A0, EXYNOS4210_GPIO_F1, "gpf1"),
	EXYNOS_PIN_BANK_EINTG(0x1C0, EXYNOS4210_GPIO_F2, "gpf2"),
	EXYNOS_PIN_BANK_EINTG(0x1E0, EXYNOS4210_GPIO_F3, "gpf3"),
};

/* pin banks of exynos4210 pin-controller 1 */
static struct samsung_pin_bank exynos4210_pin_banks1[] = {
	EXYNOS_PIN_BANK_EINTG(0x000, EXYNOS4210_GPIO_J0, "gpj0"),
	EXYNOS_PIN_BANK_EINTG(0x020, EXYNOS4210_GPIO_J1, "gpj1"),
	EXYNOS_PIN_BANK_EINTG(0x040, EXYNOS4210_GPIO_K0, "gpk0"),
	EXYNOS_PIN_BANK_EINTG(0x060, EXYNOS4210_GPIO_K1, "gpk1"),
	EXYNOS_PIN_BANK_EINTG(0x080, EXYNOS4210_GPIO_K2, "gpk2"),
	EXYNOS_PIN_BANK_EINTG(0x0A0, EXYNOS4210_GPIO_K3, "gpk3"),
	EXYNOS_PIN_BANK_EINTG(0x0C0, EXYNOS4210_GPIO_L0, "gpl0"),
	EXYNOS_PIN_BANK_EINTG(0x0E0, EXYNOS4210_GPIO_L1, "gpl1"),
	EXYNOS_PIN_BANK_EINTG(0x100, EXYNOS4210_GPIO_L2, "gpl2"),
	EXYNOS_PIN_BANK_EINTN(0x120, EXYNOS4210_GPIO_Y0, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(0x140, EXYNOS4210_GPIO_Y1, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(0x160, EXYNOS4210_GPIO_Y2, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(0x180, EXYNOS4210_GPIO_Y3, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(0x1A0, EXYNOS4210_GPIO_Y4, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(0x1C0, EXYNOS4210_GPIO_Y5, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(0x1E0, EXYNOS4210_GPIO_Y6, "gpy6"),
	EXYNOS_PIN_BANK_EINTN(0xC00, EXYNOS4210_GPIO_X0, "gpx0"),
	EXYNOS_PIN_BANK_EINTN(0xC20, EXYNOS4210_GPIO_X1, "gpx1"),
	EXYNOS_PIN_BANK_EINTN(0xC40, EXYNOS4210_GPIO_X2, "gpx2"),
	EXYNOS_PIN_BANK_EINTN(0xC60, EXYNOS4210_GPIO_X3, "gpx3"),
};

/* pin banks of exynos4210 pin-controller 2 */
static struct samsung_pin_bank exynos4210_pin_banks2[] = {
	EXYNOS_PIN_BANK_EINTN(0x000, EXYNOS4210_GPIO_Z, "gpz"),
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
		.base		= EXYNOS4210_GPIO_A0_START,
		.nr_pins	= EXYNOS4210_GPIOA_NR_PINS,
		.nr_gint	= EXYNOS4210_GPIOA_NR_GINT,
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
		.base		= EXYNOS4210_GPIOA_NR_PINS,
		.nr_pins	= EXYNOS4210_GPIOB_NR_PINS,
		.nr_gint	= EXYNOS4210_GPIOB_NR_GINT,
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
		.base		= EXYNOS4210_GPIOA_NR_PINS +
					EXYNOS4210_GPIOB_NR_PINS,
		.nr_pins	= EXYNOS4210_GPIOC_NR_PINS,
		.label		= "exynos4210-gpio-ctrl2",
	},
};
