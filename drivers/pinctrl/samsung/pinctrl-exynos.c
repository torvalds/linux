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
#include <linux/irqchip/chained_irq.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/err.h>

#include "pinctrl-samsung.h"
#include "pinctrl-exynos.h"

struct exynos_irq_chip {
	struct irq_chip chip;

	u32 eint_con;
	u32 eint_mask;
	u32 eint_pend;
};

static inline struct exynos_irq_chip *to_exynos_irq_chip(struct irq_chip *chip)
{
	return container_of(chip, struct exynos_irq_chip, chip);
}

static const struct samsung_pin_bank_type bank_type_off = {
	.fld_width = { 4, 1, 2, 2, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, },
};

static const struct samsung_pin_bank_type bank_type_alive = {
	.fld_width = { 4, 1, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, },
};

static void exynos_irq_mask(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exynos_irq_chip *our_chip = to_exynos_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long reg_mask = our_chip->eint_mask + bank->eint_offset;
	unsigned long mask;
	unsigned long flags;

	spin_lock_irqsave(&bank->slock, flags);

	mask = readl(d->virt_base + reg_mask);
	mask |= 1 << irqd->hwirq;
	writel(mask, d->virt_base + reg_mask);

	spin_unlock_irqrestore(&bank->slock, flags);
}

static void exynos_irq_ack(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exynos_irq_chip *our_chip = to_exynos_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long reg_pend = our_chip->eint_pend + bank->eint_offset;

	writel(1 << irqd->hwirq, d->virt_base + reg_pend);
}

static void exynos_irq_unmask(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exynos_irq_chip *our_chip = to_exynos_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long reg_mask = our_chip->eint_mask + bank->eint_offset;
	unsigned long mask;
	unsigned long flags;

	/*
	 * Ack level interrupts right before unmask
	 *
	 * If we don't do this we'll get a double-interrupt.  Level triggered
	 * interrupts must not fire an interrupt if the level is not
	 * _currently_ active, even if it was active while the interrupt was
	 * masked.
	 */
	if (irqd_get_trigger_type(irqd) & IRQ_TYPE_LEVEL_MASK)
		exynos_irq_ack(irqd);

	spin_lock_irqsave(&bank->slock, flags);

	mask = readl(d->virt_base + reg_mask);
	mask &= ~(1 << irqd->hwirq);
	writel(mask, d->virt_base + reg_mask);

	spin_unlock_irqrestore(&bank->slock, flags);
}

static int exynos_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exynos_irq_chip *our_chip = to_exynos_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned int shift = EXYNOS_EINT_CON_LEN * irqd->hwirq;
	unsigned int con, trig_type;
	unsigned long reg_con = our_chip->eint_con + bank->eint_offset;

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

static int exynos_irq_request_resources(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exynos_irq_chip *our_chip = to_exynos_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	const struct samsung_pin_bank_type *bank_type = bank->type;
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned int shift = EXYNOS_EINT_CON_LEN * irqd->hwirq;
	unsigned long reg_con = our_chip->eint_con + bank->eint_offset;
	unsigned long flags;
	unsigned int mask;
	unsigned int con;
	int ret;

	ret = gpiochip_lock_as_irq(&bank->gpio_chip, irqd->hwirq);
	if (ret) {
		dev_err(bank->gpio_chip.dev, "unable to lock pin %s-%lu IRQ\n",
			bank->name, irqd->hwirq);
		return ret;
	}

	reg_con = bank->pctl_offset + bank_type->reg_offset[PINCFG_TYPE_FUNC];
	shift = irqd->hwirq * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	spin_lock_irqsave(&bank->slock, flags);

	con = readl(d->virt_base + reg_con);
	con &= ~(mask << shift);
	con |= EXYNOS_EINT_FUNC << shift;
	writel(con, d->virt_base + reg_con);

	spin_unlock_irqrestore(&bank->slock, flags);

	exynos_irq_unmask(irqd);

	return 0;
}

static void exynos_irq_release_resources(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exynos_irq_chip *our_chip = to_exynos_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	const struct samsung_pin_bank_type *bank_type = bank->type;
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned int shift = EXYNOS_EINT_CON_LEN * irqd->hwirq;
	unsigned long reg_con = our_chip->eint_con + bank->eint_offset;
	unsigned long flags;
	unsigned int mask;
	unsigned int con;

	reg_con = bank->pctl_offset + bank_type->reg_offset[PINCFG_TYPE_FUNC];
	shift = irqd->hwirq * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	exynos_irq_mask(irqd);

	spin_lock_irqsave(&bank->slock, flags);

	con = readl(d->virt_base + reg_con);
	con &= ~(mask << shift);
	con |= FUNC_INPUT << shift;
	writel(con, d->virt_base + reg_con);

	spin_unlock_irqrestore(&bank->slock, flags);

	gpiochip_unlock_as_irq(&bank->gpio_chip, irqd->hwirq);
}

/*
 * irq_chip for gpio interrupts.
 */
static struct exynos_irq_chip exynos_gpio_irq_chip = {
	.chip = {
		.name = "exynos_gpio_irq_chip",
		.irq_unmask = exynos_irq_unmask,
		.irq_mask = exynos_irq_mask,
		.irq_ack = exynos_irq_ack,
		.irq_set_type = exynos_irq_set_type,
		.irq_request_resources = exynos_irq_request_resources,
		.irq_release_resources = exynos_irq_release_resources,
	},
	.eint_con = EXYNOS_GPIO_ECON_OFFSET,
	.eint_mask = EXYNOS_GPIO_EMASK_OFFSET,
	.eint_pend = EXYNOS_GPIO_EPEND_OFFSET,
};

static int exynos_eint_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct samsung_pin_bank *b = h->host_data;

	irq_set_chip_data(virq, b);
	irq_set_chip_and_handler(virq, &b->irq_chip->chip,
					handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

/*
 * irq domain callbacks for external gpio and wakeup interrupt controllers.
 */
static const struct irq_domain_ops exynos_eint_irqd_ops = {
	.map	= exynos_eint_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static irqreturn_t exynos_eint_gpio_irq(int irq, void *data)
{
	struct samsung_pinctrl_drv_data *d = data;
	struct samsung_pin_bank *bank = d->pin_banks;
	unsigned int svc, group, pin, virq;

	svc = readl(d->virt_base + EXYNOS_SVC_OFFSET);
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

struct exynos_eint_gpio_save {
	u32 eint_con;
	u32 eint_fltcon0;
	u32 eint_fltcon1;
};

/*
 * exynos_eint_gpio_init() - setup handling of external gpio interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
static int exynos_eint_gpio_init(struct samsung_pinctrl_drv_data *d)
{
	struct samsung_pin_bank *bank;
	struct device *dev = d->dev;
	int ret;
	int i;

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

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;
		bank->irq_domain = irq_domain_add_linear(bank->of_node,
				bank->nr_pins, &exynos_eint_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "gpio irq domain add failed\n");
			ret = -ENXIO;
			goto err_domains;
		}

		bank->soc_priv = devm_kzalloc(d->dev,
			sizeof(struct exynos_eint_gpio_save), GFP_KERNEL);
		if (!bank->soc_priv) {
			irq_domain_remove(bank->irq_domain);
			ret = -ENOMEM;
			goto err_domains;
		}

		bank->irq_chip = &exynos_gpio_irq_chip;
	}

	return 0;

err_domains:
	for (--i, --bank; i >= 0; --i, --bank) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;
		irq_domain_remove(bank->irq_domain);
	}

	return ret;
}

static u32 exynos_eint_wake_mask = 0xffffffff;

u32 exynos_get_eint_wake_mask(void)
{
	return exynos_eint_wake_mask;
}

static int exynos_wkup_irq_set_wake(struct irq_data *irqd, unsigned int on)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long bit = 1UL << (2 * bank->eint_offset + irqd->hwirq);

	pr_info("wake %s for irq %d\n", on ? "enabled" : "disabled", irqd->irq);

	if (!on)
		exynos_eint_wake_mask |= bit;
	else
		exynos_eint_wake_mask &= ~bit;

	return 0;
}

/*
 * irq_chip for wakeup interrupts
 */
static struct exynos_irq_chip exynos4210_wkup_irq_chip __initdata = {
	.chip = {
		.name = "exynos4210_wkup_irq_chip",
		.irq_unmask = exynos_irq_unmask,
		.irq_mask = exynos_irq_mask,
		.irq_ack = exynos_irq_ack,
		.irq_set_type = exynos_irq_set_type,
		.irq_set_wake = exynos_wkup_irq_set_wake,
		.irq_request_resources = exynos_irq_request_resources,
		.irq_release_resources = exynos_irq_release_resources,
	},
	.eint_con = EXYNOS_WKUP_ECON_OFFSET,
	.eint_mask = EXYNOS_WKUP_EMASK_OFFSET,
	.eint_pend = EXYNOS_WKUP_EPEND_OFFSET,
};

static struct exynos_irq_chip exynos7_wkup_irq_chip __initdata = {
	.chip = {
		.name = "exynos7_wkup_irq_chip",
		.irq_unmask = exynos_irq_unmask,
		.irq_mask = exynos_irq_mask,
		.irq_ack = exynos_irq_ack,
		.irq_set_type = exynos_irq_set_type,
		.irq_set_wake = exynos_wkup_irq_set_wake,
		.irq_request_resources = exynos_irq_request_resources,
		.irq_release_resources = exynos_irq_release_resources,
	},
	.eint_con = EXYNOS7_WKUP_ECON_OFFSET,
	.eint_mask = EXYNOS7_WKUP_EMASK_OFFSET,
	.eint_pend = EXYNOS7_WKUP_EPEND_OFFSET,
};

/* list of external wakeup controllers supported */
static const struct of_device_id exynos_wkup_irq_ids[] = {
	{ .compatible = "samsung,exynos4210-wakeup-eint",
			.data = &exynos4210_wkup_irq_chip },
	{ .compatible = "samsung,exynos7-wakeup-eint",
			.data = &exynos7_wkup_irq_chip },
	{ }
};

/* interrupt handler for wakeup interrupts 0..15 */
static void exynos_irq_eint0_15(unsigned int irq, struct irq_desc *desc)
{
	struct exynos_weint_data *eintd = irq_get_handler_data(irq);
	struct samsung_pin_bank *bank = eintd->bank;
	struct irq_chip *chip = irq_get_chip(irq);
	int eint_irq;

	chained_irq_enter(chip, desc);
	chip->irq_mask(&desc->irq_data);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	eint_irq = irq_linear_revmap(bank->irq_domain, eintd->irq);
	generic_handle_irq(eint_irq);
	chip->irq_unmask(&desc->irq_data);
	chained_irq_exit(chip, desc);
}

static inline void exynos_irq_demux_eint(unsigned long pend,
						struct irq_domain *domain)
{
	unsigned int irq;

	while (pend) {
		irq = fls(pend) - 1;
		generic_handle_irq(irq_find_mapping(domain, irq));
		pend &= ~(1 << irq);
	}
}

/* interrupt handler for wakeup interrupt 16 */
static void exynos_irq_demux_eint16_31(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct exynos_muxed_weint_data *eintd = irq_get_handler_data(irq);
	struct samsung_pinctrl_drv_data *d = eintd->banks[0]->drvdata;
	unsigned long pend;
	unsigned long mask;
	int i;

	chained_irq_enter(chip, desc);

	for (i = 0; i < eintd->nr_banks; ++i) {
		struct samsung_pin_bank *b = eintd->banks[i];
		pend = readl(d->virt_base + b->irq_chip->eint_pend
				+ b->eint_offset);
		mask = readl(d->virt_base + b->irq_chip->eint_mask
				+ b->eint_offset);
		exynos_irq_demux_eint(pend & ~mask, b->irq_domain);
	}

	chained_irq_exit(chip, desc);
}

/*
 * exynos_eint_wkup_init() - setup handling of external wakeup interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
static int exynos_eint_wkup_init(struct samsung_pinctrl_drv_data *d)
{
	struct device *dev = d->dev;
	struct device_node *wkup_np = NULL;
	struct device_node *np;
	struct samsung_pin_bank *bank;
	struct exynos_weint_data *weint_data;
	struct exynos_muxed_weint_data *muxed_data;
	struct exynos_irq_chip *irq_chip;
	unsigned int muxed_banks = 0;
	unsigned int i;
	int idx, irq;

	for_each_child_of_node(dev->of_node, np) {
		const struct of_device_id *match;

		match = of_match_node(exynos_wkup_irq_ids, np);
		if (match) {
			irq_chip = kmemdup(match->data,
				sizeof(*irq_chip), GFP_KERNEL);
			wkup_np = np;
			break;
		}
	}
	if (!wkup_np)
		return -ENODEV;

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_WKUP)
			continue;

		bank->irq_domain = irq_domain_add_linear(bank->of_node,
				bank->nr_pins, &exynos_eint_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "wkup irq domain add failed\n");
			return -ENXIO;
		}

		bank->irq_chip = irq_chip;

		if (!of_find_property(bank->of_node, "interrupts", NULL)) {
			bank->eint_type = EINT_TYPE_WKUP_MUX;
			++muxed_banks;
			continue;
		}

		weint_data = devm_kzalloc(dev, bank->nr_pins
					* sizeof(*weint_data), GFP_KERNEL);
		if (!weint_data) {
			dev_err(dev, "could not allocate memory for weint_data\n");
			return -ENOMEM;
		}

		for (idx = 0; idx < bank->nr_pins; ++idx) {
			irq = irq_of_parse_and_map(bank->of_node, idx);
			if (!irq) {
				dev_err(dev, "irq number for eint-%s-%d not found\n",
							bank->name, idx);
				continue;
			}
			weint_data[idx].irq = idx;
			weint_data[idx].bank = bank;
			irq_set_chained_handler_and_data(irq,
							 exynos_irq_eint0_15,
							 &weint_data[idx]);
		}
	}

	if (!muxed_banks)
		return 0;

	irq = irq_of_parse_and_map(wkup_np, 0);
	if (!irq) {
		dev_err(dev, "irq number for muxed EINTs not found\n");
		return 0;
	}

	muxed_data = devm_kzalloc(dev, sizeof(*muxed_data)
		+ muxed_banks*sizeof(struct samsung_pin_bank *), GFP_KERNEL);
	if (!muxed_data) {
		dev_err(dev, "could not allocate memory for muxed_data\n");
		return -ENOMEM;
	}

	irq_set_chained_handler_and_data(irq, exynos_irq_demux_eint16_31,
					 muxed_data);

	bank = d->pin_banks;
	idx = 0;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_WKUP_MUX)
			continue;

		muxed_data->banks[idx++] = bank;
	}
	muxed_data->nr_banks = muxed_banks;

	return 0;
}

static void exynos_pinctrl_suspend_bank(
				struct samsung_pinctrl_drv_data *drvdata,
				struct samsung_pin_bank *bank)
{
	struct exynos_eint_gpio_save *save = bank->soc_priv;
	void __iomem *regs = drvdata->virt_base;

	save->eint_con = readl(regs + EXYNOS_GPIO_ECON_OFFSET
						+ bank->eint_offset);
	save->eint_fltcon0 = readl(regs + EXYNOS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset);
	save->eint_fltcon1 = readl(regs + EXYNOS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset + 4);

	pr_debug("%s: save     con %#010x\n", bank->name, save->eint_con);
	pr_debug("%s: save fltcon0 %#010x\n", bank->name, save->eint_fltcon0);
	pr_debug("%s: save fltcon1 %#010x\n", bank->name, save->eint_fltcon1);
}

static void exynos_pinctrl_suspend(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank)
		if (bank->eint_type == EINT_TYPE_GPIO)
			exynos_pinctrl_suspend_bank(drvdata, bank);
}

static void exynos_pinctrl_resume_bank(
				struct samsung_pinctrl_drv_data *drvdata,
				struct samsung_pin_bank *bank)
{
	struct exynos_eint_gpio_save *save = bank->soc_priv;
	void __iomem *regs = drvdata->virt_base;

	pr_debug("%s:     con %#010x => %#010x\n", bank->name,
			readl(regs + EXYNOS_GPIO_ECON_OFFSET
			+ bank->eint_offset), save->eint_con);
	pr_debug("%s: fltcon0 %#010x => %#010x\n", bank->name,
			readl(regs + EXYNOS_GPIO_EFLTCON_OFFSET
			+ 2 * bank->eint_offset), save->eint_fltcon0);
	pr_debug("%s: fltcon1 %#010x => %#010x\n", bank->name,
			readl(regs + EXYNOS_GPIO_EFLTCON_OFFSET
			+ 2 * bank->eint_offset + 4), save->eint_fltcon1);

	writel(save->eint_con, regs + EXYNOS_GPIO_ECON_OFFSET
						+ bank->eint_offset);
	writel(save->eint_fltcon0, regs + EXYNOS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset);
	writel(save->eint_fltcon1, regs + EXYNOS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset + 4);
}

static void exynos_pinctrl_resume(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank)
		if (bank->eint_type == EINT_TYPE_GPIO)
			exynos_pinctrl_resume_bank(drvdata, bank);
}

/* pin banks of s5pv210 pin-controller */
static const struct samsung_pin_bank_data s5pv210_pin_bank[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(6, 0x0c0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpe0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(5, 0x100, "gpe1", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x120, "gpf0", 0x24),
	EXYNOS_PIN_BANK_EINTG(8, 0x140, "gpf1", 0x28),
	EXYNOS_PIN_BANK_EINTG(8, 0x160, "gpf2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(6, 0x180, "gpf3", 0x30),
	EXYNOS_PIN_BANK_EINTG(7, 0x1a0, "gpg0", 0x34),
	EXYNOS_PIN_BANK_EINTG(7, 0x1c0, "gpg1", 0x38),
	EXYNOS_PIN_BANK_EINTG(7, 0x1e0, "gpg2", 0x3c),
	EXYNOS_PIN_BANK_EINTG(7, 0x200, "gpg3", 0x40),
	EXYNOS_PIN_BANK_EINTN(7, 0x220, "gpi"),
	EXYNOS_PIN_BANK_EINTG(8, 0x240, "gpj0", 0x44),
	EXYNOS_PIN_BANK_EINTG(6, 0x260, "gpj1", 0x48),
	EXYNOS_PIN_BANK_EINTG(8, 0x280, "gpj2", 0x4c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2a0, "gpj3", 0x50),
	EXYNOS_PIN_BANK_EINTG(5, 0x2c0, "gpj4", 0x54),
	EXYNOS_PIN_BANK_EINTN(8, 0x2e0, "mp01"),
	EXYNOS_PIN_BANK_EINTN(4, 0x300, "mp02"),
	EXYNOS_PIN_BANK_EINTN(8, 0x320, "mp03"),
	EXYNOS_PIN_BANK_EINTN(8, 0x340, "mp04"),
	EXYNOS_PIN_BANK_EINTN(8, 0x360, "mp05"),
	EXYNOS_PIN_BANK_EINTN(8, 0x380, "mp06"),
	EXYNOS_PIN_BANK_EINTN(8, 0x3a0, "mp07"),
	EXYNOS_PIN_BANK_EINTW(8, 0xc00, "gph0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xc20, "gph1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xc40, "gph2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xc60, "gph3", 0x0c),
};

const struct samsung_pin_ctrl s5pv210_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= s5pv210_pin_bank,
		.nr_banks	= ARRAY_SIZE(s5pv210_pin_bank),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

/* pin banks of exynos3250 pin-controller 0 */
static const struct samsung_pin_bank_data exynos3250_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb",  0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0c0, "gpd1", 0x18),
};

/* pin banks of exynos3250 pin-controller 1 */
static const struct samsung_pin_bank_data exynos3250_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTN(8, 0x120, "gpe0"),
	EXYNOS_PIN_BANK_EINTN(8, 0x140, "gpe1"),
	EXYNOS_PIN_BANK_EINTN(3, 0x180, "gpe2"),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0c0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x260, "gpm0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x280, "gpm1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x2a0, "gpm2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2c0, "gpm3", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x2e0, "gpm4", 0x34),
	EXYNOS_PIN_BANK_EINTW(8, 0xc00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xc20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xc40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xc60, "gpx3", 0x0c),
};

/*
 * Samsung pinctrl driver data for Exynos3250 SoC. Exynos3250 SoC includes
 * two gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos3250_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos3250_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos3250_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos3250_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos3250_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

/* pin banks of exynos4210 pin-controller 0 */
static const struct samsung_pin_bank_data exynos4210_pin_banks0[] __initconst = {
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
static const struct samsung_pin_bank_data exynos4210_pin_banks1[] __initconst = {
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
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos4210 pin-controller 2 */
static const struct samsung_pin_bank_data exynos4210_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTN(7, 0x000, "gpz"),
};

/*
 * Samsung pinctrl driver data for Exynos4210 SoC. Exynos4210 SoC includes
 * three gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos4210_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos4210_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos4210_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos4210_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks2),
	},
};

/* pin banks of exynos4x12 pin-controller 0 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpf0", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x1A0, "gpf1", 0x34),
	EXYNOS_PIN_BANK_EINTG(8, 0x1C0, "gpf2", 0x38),
	EXYNOS_PIN_BANK_EINTG(6, 0x1E0, "gpf3", 0x3c),
	EXYNOS_PIN_BANK_EINTG(8, 0x240, "gpj0", 0x40),
	EXYNOS_PIN_BANK_EINTG(5, 0x260, "gpj1", 0x44),
};

/* pin banks of exynos4x12 pin-controller 1 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(7, 0x0A0, "gpk3", 0x14),
	EXYNOS_PIN_BANK_EINTG(7, 0x0C0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTG(2, 0x0E0, "gpl1", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpl2", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x260, "gpm0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x280, "gpm1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x2A0, "gpm2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2C0, "gpm3", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x2E0, "gpm4", 0x34),
	EXYNOS_PIN_BANK_EINTN(6, 0x120, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x140, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x160, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1A0, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1C0, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1E0, "gpy6"),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos4x12 pin-controller 2 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/* pin banks of exynos4x12 pin-controller 3 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks3[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpv0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpv1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpv2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpv3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x080, "gpv4", 0x10),
};

/*
 * Samsung pinctrl driver data for Exynos4x12 SoC. Exynos4x12 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos4x12_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos4x12_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos4x12_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos4x12_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos4x12_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

/* pin banks of exynos4415 pin-controller 0 */
static const struct samsung_pin_bank_data exynos4415_pin_banks0[] = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpf0", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x1A0, "gpf1", 0x34),
	EXYNOS_PIN_BANK_EINTG(1, 0x1C0, "gpf2", 0x38),
};

/* pin banks of exynos4415 pin-controller 1 */
static const struct samsung_pin_bank_data exynos4415_pin_banks1[] = {
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(7, 0x0A0, "gpk3", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTN(6, 0x120, "mp00"),
	EXYNOS_PIN_BANK_EINTN(4, 0x140, "mp01"),
	EXYNOS_PIN_BANK_EINTN(6, 0x160, "mp02"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "mp03"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1A0, "mp04"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1C0, "mp05"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1E0, "mp06"),
	EXYNOS_PIN_BANK_EINTG(8, 0x260, "gpm0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x280, "gpm1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x2A0, "gpm2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2C0, "gpm3", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x2E0, "gpm4", 0x34),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos4415 pin-controller 2 */
static const struct samsung_pin_bank_data exynos4415_pin_banks2[] = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
	EXYNOS_PIN_BANK_EINTN(2, 0x000, "etc1"),
};

/*
 * Samsung pinctrl driver data for Exynos4415 SoC. Exynos4415 SoC includes
 * three gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos4415_pin_ctrl[] = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos4415_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos4415_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos4415_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos4415_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos4415_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos4415_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

/* pin banks of exynos5250 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5250_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(7, 0x0E0, "gpc0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(4, 0x100, "gpc1", 0x20),
	EXYNOS_PIN_BANK_EINTG(7, 0x120, "gpc2", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x140, "gpc3", 0x28),
	EXYNOS_PIN_BANK_EINTG(4, 0x160, "gpd0", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpd1", 0x30),
	EXYNOS_PIN_BANK_EINTG(7, 0x2E0, "gpc4", 0x34),
	EXYNOS_PIN_BANK_EINTN(6, 0x1A0, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x1C0, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x1E0, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x200, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x220, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x240, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x260, "gpy6"),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos5250 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5250_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpe0", 0x00),
	EXYNOS_PIN_BANK_EINTG(2, 0x020, "gpe1", 0x04),
	EXYNOS_PIN_BANK_EINTG(4, 0x040, "gpf0", 0x08),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpf1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpg0", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpg1", 0x14),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpg2", 0x18),
	EXYNOS_PIN_BANK_EINTG(4, 0x0E0, "gph0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gph1", 0x20),
};

/* pin banks of exynos5250 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5250_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpv0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpv1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpv2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpv3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpv4", 0x10),
};

/* pin banks of exynos5250 pin-controller 3 */
static const struct samsung_pin_bank_data exynos5250_pin_banks3[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/*
 * Samsung pinctrl driver data for Exynos5250 SoC. Exynos5250 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos5250_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5250_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5250_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5250_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5250_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

/* pin banks of exynos5260 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5260_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(7, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(4, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(5, 0x0a0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0c0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpb4", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpb5", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x120, "gpd0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x140, "gpd1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x160, "gpd2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpe0", 0x30),
	EXYNOS_PIN_BANK_EINTG(5, 0x1a0, "gpe1", 0x34),
	EXYNOS_PIN_BANK_EINTG(4, 0x1c0, "gpf0", 0x38),
	EXYNOS_PIN_BANK_EINTG(8, 0x1e0, "gpf1", 0x3c),
	EXYNOS_PIN_BANK_EINTG(2, 0x200, "gpk0", 0x40),
	EXYNOS_PIN_BANK_EINTW(8, 0xc00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xc20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xc40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xc60, "gpx3", 0x0c),
};

/* pin banks of exynos5260 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5260_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpc0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpc1", 0x04),
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpc2", 0x08),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpc3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(4, 0x080, "gpc4", 0x10),
};

/* pin banks of exynos5260 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5260_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpz1", 0x04),
};

/*
 * Samsung pinctrl driver data for Exynos5260 SoC. Exynos5260 SoC includes
 * three gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos5260_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5260_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5260_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5260_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5260_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5260_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5260_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	},
};

/* pin banks of exynos5420 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5420_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpy7", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos5420 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5420_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpc0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpc1", 0x04),
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpc2", 0x08),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpc3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x080, "gpc4", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpd1", 0x14),
	EXYNOS_PIN_BANK_EINTN(6, 0x0C0, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x0E0, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x100, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x120, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x140, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x160, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "gpy6"),
};

/* pin banks of exynos5420 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5420_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpe0", 0x00),
	EXYNOS_PIN_BANK_EINTG(2, 0x020, "gpe1", 0x04),
	EXYNOS_PIN_BANK_EINTG(6, 0x040, "gpf0", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpf1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpg0", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpg1", 0x14),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpg2", 0x18),
	EXYNOS_PIN_BANK_EINTG(4, 0x0E0, "gpj4", 0x1c),
};

/* pin banks of exynos5420 pin-controller 3 */
static const struct samsung_pin_bank_data exynos5420_pin_banks3[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0C0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(2, 0x0E0, "gpb4", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gph0", 0x20),
};

/* pin banks of exynos5420 pin-controller 4 */
static const struct samsung_pin_bank_data exynos5420_pin_banks4[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/*
 * Samsung pinctrl driver data for Exynos5420 SoC. Exynos5420 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos5420_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5420_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5420_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5420_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5420_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 4 data */
		.pin_banks	= exynos5420_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
	},
};

/* pin banks of exynos5433 pin-controller - ALIVE */
static const struct samsung_pin_bank_data exynos5433_pin_banks0[] = {
	EXYNOS_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0x060, "gpa3", 0x0c),
};

/* pin banks of exynos5433 pin-controller - AUD */
static const struct samsung_pin_bank_data exynos5433_pin_banks1[] = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpz1", 0x04),
};

/* pin banks of exynos5433 pin-controller - CPIF */
static const struct samsung_pin_bank_data exynos5433_pin_banks2[] = {
	EXYNOS_PIN_BANK_EINTG(2, 0x000, "gpv6", 0x00),
};

/* pin banks of exynos5433 pin-controller - eSE */
static const struct samsung_pin_bank_data exynos5433_pin_banks3[] = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj2", 0x00),
};

/* pin banks of exynos5433 pin-controller - FINGER */
static const struct samsung_pin_bank_data exynos5433_pin_banks4[] = {
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpd5", 0x00),
};

/* pin banks of exynos5433 pin-controller - FSYS */
static const struct samsung_pin_bank_data exynos5433_pin_banks5[] = {
	EXYNOS_PIN_BANK_EINTG(6, 0x000, "gph1", 0x00),
	EXYNOS_PIN_BANK_EINTG(7, 0x020, "gpr4", 0x04),
	EXYNOS_PIN_BANK_EINTG(5, 0x040, "gpr0", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpr1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x080, "gpr2", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0a0, "gpr3", 0x14),
};

/* pin banks of exynos5433 pin-controller - IMEM */
static const struct samsung_pin_bank_data exynos5433_pin_banks6[] = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpf0", 0x00),
};

/* pin banks of exynos5433 pin-controller - NFC */
static const struct samsung_pin_bank_data exynos5433_pin_banks7[] = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj0", 0x00),
};

/* pin banks of exynos5433 pin-controller - PERIC */
static const struct samsung_pin_bank_data exynos5433_pin_banks8[] = {
	EXYNOS_PIN_BANK_EINTG(6, 0x000, "gpv7", 0x00),
	EXYNOS_PIN_BANK_EINTG(5, 0x020, "gpb0", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpc0", 0x08),
	EXYNOS_PIN_BANK_EINTG(2, 0x060, "gpc1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(6, 0x080, "gpc2", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0a0, "gpc3", 0x14),
	EXYNOS_PIN_BANK_EINTG(2, 0x0c0, "gpg0", 0x18),
	EXYNOS_PIN_BANK_EINTG(4, 0x0e0, "gpd0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(6, 0x100, "gpd1", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x120, "gpd2", 0x24),
	EXYNOS_PIN_BANK_EINTG(5, 0x140, "gpd4", 0x28),
	EXYNOS_PIN_BANK_EINTG(2, 0x160, "gpd8", 0x2c),
	EXYNOS_PIN_BANK_EINTG(7, 0x180, "gpd6", 0x30),
	EXYNOS_PIN_BANK_EINTG(3, 0x1a0, "gpd7", 0x34),
	EXYNOS_PIN_BANK_EINTG(5, 0x1c0, "gpg1", 0x38),
	EXYNOS_PIN_BANK_EINTG(2, 0x1e0, "gpg2", 0x3c),
	EXYNOS_PIN_BANK_EINTG(8, 0x200, "gpg3", 0x40),
};

/* pin banks of exynos5433 pin-controller - TOUCH */
static const struct samsung_pin_bank_data exynos5433_pin_banks9[] = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj1", 0x00),
};

/*
 * Samsung pinctrl driver data for Exynos5433 SoC. Exynos5433 SoC includes
 * ten gpio/pin-mux/pinconfig controllers.
 */
const struct samsung_pin_ctrl exynos5433_pin_ctrl[] = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5433_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5433_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5433_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5433_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 4 data */
		.pin_banks	= exynos5433_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 5 data */
		.pin_banks	= exynos5433_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 6 data */
		.pin_banks	= exynos5433_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 7 data */
		.pin_banks	= exynos5433_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 8 data */
		.pin_banks	= exynos5433_pin_banks8,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks8),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 9 data */
		.pin_banks	= exynos5433_pin_banks9,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks9),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

/* pin banks of exynos7 pin-controller - ALIVE */
static const struct samsung_pin_bank_data exynos7_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0x060, "gpa3", 0x0c),
};

/* pin banks of exynos7 pin-controller - BUS0 */
static const struct samsung_pin_bank_data exynos7_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(5, 0x000, "gpb0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpc0", 0x04),
	EXYNOS_PIN_BANK_EINTG(2, 0x040, "gpc1", 0x08),
	EXYNOS_PIN_BANK_EINTG(6, 0x060, "gpc2", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpc3", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(6, 0x0c0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpd2", 0x1c),
	EXYNOS_PIN_BANK_EINTG(5, 0x100, "gpd4", 0x20),
	EXYNOS_PIN_BANK_EINTG(4, 0x120, "gpd5", 0x24),
	EXYNOS_PIN_BANK_EINTG(6, 0x140, "gpd6", 0x28),
	EXYNOS_PIN_BANK_EINTG(3, 0x160, "gpd7", 0x2c),
	EXYNOS_PIN_BANK_EINTG(2, 0x180, "gpd8", 0x30),
	EXYNOS_PIN_BANK_EINTG(2, 0x1a0, "gpg0", 0x34),
	EXYNOS_PIN_BANK_EINTG(4, 0x1c0, "gpg3", 0x38),
};

/* pin banks of exynos7 pin-controller - NFC */
static const struct samsung_pin_bank_data exynos7_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj0", 0x00),
};

/* pin banks of exynos7 pin-controller - TOUCH */
static const struct samsung_pin_bank_data exynos7_pin_banks3[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj1", 0x00),
};

/* pin banks of exynos7 pin-controller - FF */
static const struct samsung_pin_bank_data exynos7_pin_banks4[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpg4", 0x00),
};

/* pin banks of exynos7 pin-controller - ESE */
static const struct samsung_pin_bank_data exynos7_pin_banks5[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(5, 0x000, "gpv7", 0x00),
};

/* pin banks of exynos7 pin-controller - FSYS0 */
static const struct samsung_pin_bank_data exynos7_pin_banks6[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpr4", 0x00),
};

/* pin banks of exynos7 pin-controller - FSYS1 */
static const struct samsung_pin_bank_data exynos7_pin_banks7[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpr0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpr1", 0x04),
	EXYNOS_PIN_BANK_EINTG(5, 0x040, "gpr2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpr3", 0x0c),
};

/* pin banks of exynos7 pin-controller - BUS1 */
static const struct samsung_pin_bank_data exynos7_pin_banks8[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpf0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpf1", 0x04),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpf2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpf3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x0a0, "gpf4", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0c0, "gpf5", 0x14),
	EXYNOS_PIN_BANK_EINTG(5, 0x0e0, "gpg1", 0x18),
	EXYNOS_PIN_BANK_EINTG(5, 0x100, "gpg2", 0x1c),
	EXYNOS_PIN_BANK_EINTG(6, 0x120, "gph1", 0x20),
	EXYNOS_PIN_BANK_EINTG(3, 0x140, "gpv6", 0x24),
};

static const struct samsung_pin_bank_data exynos7_pin_banks9[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpz1", 0x04),
};

const struct samsung_pin_ctrl exynos7_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 Alive data */
		.pin_banks	= exynos7_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 BUS0 data */
		.pin_banks	= exynos7_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 2 NFC data */
		.pin_banks	= exynos7_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 3 TOUCH data */
		.pin_banks	= exynos7_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 4 FF data */
		.pin_banks	= exynos7_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 5 ESE data */
		.pin_banks	= exynos7_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 6 FSYS0 data */
		.pin_banks	= exynos7_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 7 FSYS1 data */
		.pin_banks	= exynos7_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 8 BUS1 data */
		.pin_banks	= exynos7_pin_banks8,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks8),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 9 AUD data */
		.pin_banks	= exynos7_pin_banks9,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks9),
		.eint_gpio_init = exynos_eint_gpio_init,
	},
};
