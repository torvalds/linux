// SPDX-License-Identifier: GPL-2.0+
//
// Exyyess specific support for Samsung pinctrl/gpiolib driver with eint support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
// Copyright (c) 2012 Linaro Ltd
//		http://www.linaro.org
//
// Author: Thomas Abraham <thomas.ab@samsung.com>
//
// This file contains the Samsung Exyyess specific information required by the
// the Samsung pinctrl/gpiolib driver. It also includes the implementation of
// external gpio and wakeup interrupt support.

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/soc/samsung/exyyess-pmu.h>
#include <linux/soc/samsung/exyyess-regs-pmu.h>

#include <dt-bindings/pinctrl/samsung.h>

#include "pinctrl-samsung.h"
#include "pinctrl-exyyess.h"

struct exyyess_irq_chip {
	struct irq_chip chip;

	u32 eint_con;
	u32 eint_mask;
	u32 eint_pend;
	u32 eint_wake_mask_value;
	u32 eint_wake_mask_reg;
};

static inline struct exyyess_irq_chip *to_exyyess_irq_chip(struct irq_chip *chip)
{
	return container_of(chip, struct exyyess_irq_chip, chip);
}

static void exyyess_irq_mask(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyyess_irq_chip *our_chip = to_exyyess_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long reg_mask = our_chip->eint_mask + bank->eint_offset;
	unsigned long mask;
	unsigned long flags;

	spin_lock_irqsave(&bank->slock, flags);

	mask = readl(bank->eint_base + reg_mask);
	mask |= 1 << irqd->hwirq;
	writel(mask, bank->eint_base + reg_mask);

	spin_unlock_irqrestore(&bank->slock, flags);
}

static void exyyess_irq_ack(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyyess_irq_chip *our_chip = to_exyyess_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long reg_pend = our_chip->eint_pend + bank->eint_offset;

	writel(1 << irqd->hwirq, bank->eint_base + reg_pend);
}

static void exyyess_irq_unmask(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyyess_irq_chip *our_chip = to_exyyess_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long reg_mask = our_chip->eint_mask + bank->eint_offset;
	unsigned long mask;
	unsigned long flags;

	/*
	 * Ack level interrupts right before unmask
	 *
	 * If we don't do this we'll get a double-interrupt.  Level triggered
	 * interrupts must yest fire an interrupt if the level is yest
	 * _currently_ active, even if it was active while the interrupt was
	 * masked.
	 */
	if (irqd_get_trigger_type(irqd) & IRQ_TYPE_LEVEL_MASK)
		exyyess_irq_ack(irqd);

	spin_lock_irqsave(&bank->slock, flags);

	mask = readl(bank->eint_base + reg_mask);
	mask &= ~(1 << irqd->hwirq);
	writel(mask, bank->eint_base + reg_mask);

	spin_unlock_irqrestore(&bank->slock, flags);
}

static int exyyess_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyyess_irq_chip *our_chip = to_exyyess_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
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
		irq_set_handler_locked(irqd, handle_edge_irq);
	else
		irq_set_handler_locked(irqd, handle_level_irq);

	con = readl(bank->eint_base + reg_con);
	con &= ~(EXYNOS_EINT_CON_MASK << shift);
	con |= trig_type << shift;
	writel(con, bank->eint_base + reg_con);

	return 0;
}

static int exyyess_irq_request_resources(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	const struct samsung_pin_bank_type *bank_type = bank->type;
	unsigned long reg_con, flags;
	unsigned int shift, mask, con;
	int ret;

	ret = gpiochip_lock_as_irq(&bank->gpio_chip, irqd->hwirq);
	if (ret) {
		dev_err(bank->gpio_chip.parent,
			"unable to lock pin %s-%lu IRQ\n",
			bank->name, irqd->hwirq);
		return ret;
	}

	reg_con = bank->pctl_offset + bank_type->reg_offset[PINCFG_TYPE_FUNC];
	shift = irqd->hwirq * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	spin_lock_irqsave(&bank->slock, flags);

	con = readl(bank->pctl_base + reg_con);
	con &= ~(mask << shift);
	con |= EXYNOS_PIN_FUNC_EINT << shift;
	writel(con, bank->pctl_base + reg_con);

	spin_unlock_irqrestore(&bank->slock, flags);

	return 0;
}

static void exyyess_irq_release_resources(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	const struct samsung_pin_bank_type *bank_type = bank->type;
	unsigned long reg_con, flags;
	unsigned int shift, mask, con;

	reg_con = bank->pctl_offset + bank_type->reg_offset[PINCFG_TYPE_FUNC];
	shift = irqd->hwirq * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	spin_lock_irqsave(&bank->slock, flags);

	con = readl(bank->pctl_base + reg_con);
	con &= ~(mask << shift);
	con |= EXYNOS_PIN_FUNC_INPUT << shift;
	writel(con, bank->pctl_base + reg_con);

	spin_unlock_irqrestore(&bank->slock, flags);

	gpiochip_unlock_as_irq(&bank->gpio_chip, irqd->hwirq);
}

/*
 * irq_chip for gpio interrupts.
 */
static struct exyyess_irq_chip exyyess_gpio_irq_chip = {
	.chip = {
		.name = "exyyess_gpio_irq_chip",
		.irq_unmask = exyyess_irq_unmask,
		.irq_mask = exyyess_irq_mask,
		.irq_ack = exyyess_irq_ack,
		.irq_set_type = exyyess_irq_set_type,
		.irq_request_resources = exyyess_irq_request_resources,
		.irq_release_resources = exyyess_irq_release_resources,
	},
	.eint_con = EXYNOS_GPIO_ECON_OFFSET,
	.eint_mask = EXYNOS_GPIO_EMASK_OFFSET,
	.eint_pend = EXYNOS_GPIO_EPEND_OFFSET,
	/* eint_wake_mask_value yest used */
};

static int exyyess_eint_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct samsung_pin_bank *b = h->host_data;

	irq_set_chip_data(virq, b);
	irq_set_chip_and_handler(virq, &b->irq_chip->chip,
					handle_level_irq);
	return 0;
}

/*
 * irq domain callbacks for external gpio and wakeup interrupt controllers.
 */
static const struct irq_domain_ops exyyess_eint_irqd_ops = {
	.map	= exyyess_eint_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static irqreturn_t exyyess_eint_gpio_irq(int irq, void *data)
{
	struct samsung_pinctrl_drv_data *d = data;
	struct samsung_pin_bank *bank = d->pin_banks;
	unsigned int svc, group, pin, virq;

	svc = readl(bank->eint_base + EXYNOS_SVC_OFFSET);
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

struct exyyess_eint_gpio_save {
	u32 eint_con;
	u32 eint_fltcon0;
	u32 eint_fltcon1;
};

/*
 * exyyess_eint_gpio_init() - setup handling of external gpio interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
int exyyess_eint_gpio_init(struct samsung_pinctrl_drv_data *d)
{
	struct samsung_pin_bank *bank;
	struct device *dev = d->dev;
	int ret;
	int i;

	if (!d->irq) {
		dev_err(dev, "irq number yest available\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, d->irq, exyyess_eint_gpio_irq,
					0, dev_name(dev), d);
	if (ret) {
		dev_err(dev, "irq request failed\n");
		return -ENXIO;
	}

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;
		bank->irq_domain = irq_domain_add_linear(bank->of_yesde,
				bank->nr_pins, &exyyess_eint_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "gpio irq domain add failed\n");
			ret = -ENXIO;
			goto err_domains;
		}

		bank->soc_priv = devm_kzalloc(d->dev,
			sizeof(struct exyyess_eint_gpio_save), GFP_KERNEL);
		if (!bank->soc_priv) {
			irq_domain_remove(bank->irq_domain);
			ret = -ENOMEM;
			goto err_domains;
		}

		bank->irq_chip = &exyyess_gpio_irq_chip;
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

static int exyyess_wkup_irq_set_wake(struct irq_data *irqd, unsigned int on)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyyess_irq_chip *our_chip = to_exyyess_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long bit = 1UL << (2 * bank->eint_offset + irqd->hwirq);

	pr_info("wake %s for irq %d\n", on ? "enabled" : "disabled", irqd->irq);

	if (!on)
		our_chip->eint_wake_mask_value |= bit;
	else
		our_chip->eint_wake_mask_value &= ~bit;

	return 0;
}

/*
 * irq_chip for wakeup interrupts
 */
static const struct exyyess_irq_chip s5pv210_wkup_irq_chip __initconst = {
	.chip = {
		.name = "s5pv210_wkup_irq_chip",
		.irq_unmask = exyyess_irq_unmask,
		.irq_mask = exyyess_irq_mask,
		.irq_ack = exyyess_irq_ack,
		.irq_set_type = exyyess_irq_set_type,
		.irq_set_wake = exyyess_wkup_irq_set_wake,
		.irq_request_resources = exyyess_irq_request_resources,
		.irq_release_resources = exyyess_irq_release_resources,
	},
	.eint_con = EXYNOS_WKUP_ECON_OFFSET,
	.eint_mask = EXYNOS_WKUP_EMASK_OFFSET,
	.eint_pend = EXYNOS_WKUP_EPEND_OFFSET,
	.eint_wake_mask_value = EXYNOS_EINT_WAKEUP_MASK_DISABLED,
	/* Only difference with exyyess4210_wkup_irq_chip: */
	.eint_wake_mask_reg = S5PV210_EINT_WAKEUP_MASK,
};

static const struct exyyess_irq_chip exyyess4210_wkup_irq_chip __initconst = {
	.chip = {
		.name = "exyyess4210_wkup_irq_chip",
		.irq_unmask = exyyess_irq_unmask,
		.irq_mask = exyyess_irq_mask,
		.irq_ack = exyyess_irq_ack,
		.irq_set_type = exyyess_irq_set_type,
		.irq_set_wake = exyyess_wkup_irq_set_wake,
		.irq_request_resources = exyyess_irq_request_resources,
		.irq_release_resources = exyyess_irq_release_resources,
	},
	.eint_con = EXYNOS_WKUP_ECON_OFFSET,
	.eint_mask = EXYNOS_WKUP_EMASK_OFFSET,
	.eint_pend = EXYNOS_WKUP_EPEND_OFFSET,
	.eint_wake_mask_value = EXYNOS_EINT_WAKEUP_MASK_DISABLED,
	.eint_wake_mask_reg = EXYNOS_EINT_WAKEUP_MASK,
};

static const struct exyyess_irq_chip exyyess7_wkup_irq_chip __initconst = {
	.chip = {
		.name = "exyyess7_wkup_irq_chip",
		.irq_unmask = exyyess_irq_unmask,
		.irq_mask = exyyess_irq_mask,
		.irq_ack = exyyess_irq_ack,
		.irq_set_type = exyyess_irq_set_type,
		.irq_set_wake = exyyess_wkup_irq_set_wake,
		.irq_request_resources = exyyess_irq_request_resources,
		.irq_release_resources = exyyess_irq_release_resources,
	},
	.eint_con = EXYNOS7_WKUP_ECON_OFFSET,
	.eint_mask = EXYNOS7_WKUP_EMASK_OFFSET,
	.eint_pend = EXYNOS7_WKUP_EPEND_OFFSET,
	.eint_wake_mask_value = EXYNOS_EINT_WAKEUP_MASK_DISABLED,
	.eint_wake_mask_reg = EXYNOS5433_EINT_WAKEUP_MASK,
};

/* list of external wakeup controllers supported */
static const struct of_device_id exyyess_wkup_irq_ids[] = {
	{ .compatible = "samsung,s5pv210-wakeup-eint",
			.data = &s5pv210_wkup_irq_chip },
	{ .compatible = "samsung,exyyess4210-wakeup-eint",
			.data = &exyyess4210_wkup_irq_chip },
	{ .compatible = "samsung,exyyess7-wakeup-eint",
			.data = &exyyess7_wkup_irq_chip },
	{ }
};

/* interrupt handler for wakeup interrupts 0..15 */
static void exyyess_irq_eint0_15(struct irq_desc *desc)
{
	struct exyyess_weint_data *eintd = irq_desc_get_handler_data(desc);
	struct samsung_pin_bank *bank = eintd->bank;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int eint_irq;

	chained_irq_enter(chip, desc);

	eint_irq = irq_linear_revmap(bank->irq_domain, eintd->irq);
	generic_handle_irq(eint_irq);

	chained_irq_exit(chip, desc);
}

static inline void exyyess_irq_demux_eint(unsigned long pend,
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
static void exyyess_irq_demux_eint16_31(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct exyyess_muxed_weint_data *eintd = irq_desc_get_handler_data(desc);
	unsigned long pend;
	unsigned long mask;
	int i;

	chained_irq_enter(chip, desc);

	for (i = 0; i < eintd->nr_banks; ++i) {
		struct samsung_pin_bank *b = eintd->banks[i];
		pend = readl(b->eint_base + b->irq_chip->eint_pend
				+ b->eint_offset);
		mask = readl(b->eint_base + b->irq_chip->eint_mask
				+ b->eint_offset);
		exyyess_irq_demux_eint(pend & ~mask, b->irq_domain);
	}

	chained_irq_exit(chip, desc);
}

/*
 * exyyess_eint_wkup_init() - setup handling of external wakeup interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
int exyyess_eint_wkup_init(struct samsung_pinctrl_drv_data *d)
{
	struct device *dev = d->dev;
	struct device_yesde *wkup_np = NULL;
	struct device_yesde *np;
	struct samsung_pin_bank *bank;
	struct exyyess_weint_data *weint_data;
	struct exyyess_muxed_weint_data *muxed_data;
	struct exyyess_irq_chip *irq_chip;
	unsigned int muxed_banks = 0;
	unsigned int i;
	int idx, irq;

	for_each_child_of_yesde(dev->of_yesde, np) {
		const struct of_device_id *match;

		match = of_match_yesde(exyyess_wkup_irq_ids, np);
		if (match) {
			irq_chip = kmemdup(match->data,
				sizeof(*irq_chip), GFP_KERNEL);
			if (!irq_chip) {
				of_yesde_put(np);
				return -ENOMEM;
			}
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

		bank->irq_domain = irq_domain_add_linear(bank->of_yesde,
				bank->nr_pins, &exyyess_eint_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "wkup irq domain add failed\n");
			of_yesde_put(wkup_np);
			return -ENXIO;
		}

		bank->irq_chip = irq_chip;

		if (!of_find_property(bank->of_yesde, "interrupts", NULL)) {
			bank->eint_type = EINT_TYPE_WKUP_MUX;
			++muxed_banks;
			continue;
		}

		weint_data = devm_kcalloc(dev,
					  bank->nr_pins, sizeof(*weint_data),
					  GFP_KERNEL);
		if (!weint_data) {
			of_yesde_put(wkup_np);
			return -ENOMEM;
		}

		for (idx = 0; idx < bank->nr_pins; ++idx) {
			irq = irq_of_parse_and_map(bank->of_yesde, idx);
			if (!irq) {
				dev_err(dev, "irq number for eint-%s-%d yest found\n",
							bank->name, idx);
				continue;
			}
			weint_data[idx].irq = idx;
			weint_data[idx].bank = bank;
			irq_set_chained_handler_and_data(irq,
							 exyyess_irq_eint0_15,
							 &weint_data[idx]);
		}
	}

	if (!muxed_banks) {
		of_yesde_put(wkup_np);
		return 0;
	}

	irq = irq_of_parse_and_map(wkup_np, 0);
	of_yesde_put(wkup_np);
	if (!irq) {
		dev_err(dev, "irq number for muxed EINTs yest found\n");
		return 0;
	}

	muxed_data = devm_kzalloc(dev, sizeof(*muxed_data)
		+ muxed_banks*sizeof(struct samsung_pin_bank *), GFP_KERNEL);
	if (!muxed_data)
		return -ENOMEM;

	irq_set_chained_handler_and_data(irq, exyyess_irq_demux_eint16_31,
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

static void
exyyess_pinctrl_set_eint_wakeup_mask(struct samsung_pinctrl_drv_data *drvdata,
				    struct exyyess_irq_chip *irq_chip)
{
	struct regmap *pmu_regs;

	if (!drvdata->retention_ctrl || !drvdata->retention_ctrl->priv) {
		dev_warn(drvdata->dev,
			 "No retention data configured bank with external wakeup interrupt. Wake-up mask will yest be set.\n");
		return;
	}

	pmu_regs = drvdata->retention_ctrl->priv;
	dev_info(drvdata->dev,
		 "Setting external wakeup interrupt mask: 0x%x\n",
		 irq_chip->eint_wake_mask_value);

	regmap_write(pmu_regs, irq_chip->eint_wake_mask_reg,
		     irq_chip->eint_wake_mask_value);
}

static void exyyess_pinctrl_suspend_bank(
				struct samsung_pinctrl_drv_data *drvdata,
				struct samsung_pin_bank *bank)
{
	struct exyyess_eint_gpio_save *save = bank->soc_priv;
	void __iomem *regs = bank->eint_base;

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

void exyyess_pinctrl_suspend(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	struct exyyess_irq_chip *irq_chip = NULL;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank) {
		if (bank->eint_type == EINT_TYPE_GPIO)
			exyyess_pinctrl_suspend_bank(drvdata, bank);
		else if (bank->eint_type == EINT_TYPE_WKUP) {
			if (!irq_chip) {
				irq_chip = bank->irq_chip;
				exyyess_pinctrl_set_eint_wakeup_mask(drvdata,
								    irq_chip);
			} else if (bank->irq_chip != irq_chip) {
				dev_warn(drvdata->dev,
					 "More than one external wakeup interrupt chip configured (bank: %s). This is yest supported by hardware yesr by driver.\n",
					 bank->name);
			}
		}
	}
}

static void exyyess_pinctrl_resume_bank(
				struct samsung_pinctrl_drv_data *drvdata,
				struct samsung_pin_bank *bank)
{
	struct exyyess_eint_gpio_save *save = bank->soc_priv;
	void __iomem *regs = bank->eint_base;

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

void exyyess_pinctrl_resume(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank)
		if (bank->eint_type == EINT_TYPE_GPIO)
			exyyess_pinctrl_resume_bank(drvdata, bank);
}

static void exyyess_retention_enable(struct samsung_pinctrl_drv_data *drvdata)
{
	if (drvdata->retention_ctrl->refcnt)
		atomic_inc(drvdata->retention_ctrl->refcnt);
}

static void exyyess_retention_disable(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_retention_ctrl *ctrl = drvdata->retention_ctrl;
	struct regmap *pmu_regs = ctrl->priv;
	int i;

	if (ctrl->refcnt && !atomic_dec_and_test(ctrl->refcnt))
		return;

	for (i = 0; i < ctrl->nr_regs; i++)
		regmap_write(pmu_regs, ctrl->regs[i], ctrl->value);
}

struct samsung_retention_ctrl *
exyyess_retention_init(struct samsung_pinctrl_drv_data *drvdata,
		      const struct samsung_retention_data *data)
{
	struct samsung_retention_ctrl *ctrl;
	struct regmap *pmu_regs;
	int i;

	ctrl = devm_kzalloc(drvdata->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);

	pmu_regs = exyyess_get_pmu_regmap();
	if (IS_ERR(pmu_regs))
		return ERR_CAST(pmu_regs);

	ctrl->priv = pmu_regs;
	ctrl->regs = data->regs;
	ctrl->nr_regs = data->nr_regs;
	ctrl->value = data->value;
	ctrl->refcnt = data->refcnt;
	ctrl->enable = exyyess_retention_enable;
	ctrl->disable = exyyess_retention_disable;

	/* Ensure that retention is disabled on driver init */
	for (i = 0; i < ctrl->nr_regs; i++)
		regmap_write(pmu_regs, ctrl->regs[i], ctrl->value);

	return ctrl;
}
