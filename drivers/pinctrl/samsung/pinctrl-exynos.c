// SPDX-License-Identifier: GPL-2.0+
//
// Exyanals specific support for Samsung pinctrl/gpiolib driver with eint support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
// Copyright (c) 2012 Linaro Ltd
//		http://www.linaro.org
//
// Author: Thomas Abraham <thomas.ab@samsung.com>
//
// This file contains the Samsung Exyanals specific information required by the
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
#include <linux/soc/samsung/exyanals-pmu.h>
#include <linux/soc/samsung/exyanals-regs-pmu.h>

#include "pinctrl-samsung.h"
#include "pinctrl-exyanals.h"

struct exyanals_irq_chip {
	struct irq_chip chip;

	u32 eint_con;
	u32 eint_mask;
	u32 eint_pend;
	u32 *eint_wake_mask_value;
	u32 eint_wake_mask_reg;
	void (*set_eint_wakeup_mask)(struct samsung_pinctrl_drv_data *drvdata,
				     struct exyanals_irq_chip *irq_chip);
};

static inline struct exyanals_irq_chip *to_exyanals_irq_chip(struct irq_chip *chip)
{
	return container_of(chip, struct exyanals_irq_chip, chip);
}

static void exyanals_irq_mask(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyanals_irq_chip *our_chip = to_exyanals_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long reg_mask;
	unsigned int mask;
	unsigned long flags;

	if (bank->eint_mask_offset)
		reg_mask = bank->pctl_offset + bank->eint_mask_offset;
	else
		reg_mask = our_chip->eint_mask + bank->eint_offset;

	raw_spin_lock_irqsave(&bank->slock, flags);

	mask = readl(bank->eint_base + reg_mask);
	mask |= 1 << irqd->hwirq;
	writel(mask, bank->eint_base + reg_mask);

	raw_spin_unlock_irqrestore(&bank->slock, flags);
}

static void exyanals_irq_ack(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyanals_irq_chip *our_chip = to_exyanals_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long reg_pend;

	if (bank->eint_pend_offset)
		reg_pend = bank->pctl_offset + bank->eint_pend_offset;
	else
		reg_pend = our_chip->eint_pend + bank->eint_offset;

	writel(1 << irqd->hwirq, bank->eint_base + reg_pend);
}

static void exyanals_irq_unmask(struct irq_data *irqd)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyanals_irq_chip *our_chip = to_exyanals_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long reg_mask;
	unsigned int mask;
	unsigned long flags;

	/*
	 * Ack level interrupts right before unmask
	 *
	 * If we don't do this we'll get a double-interrupt.  Level triggered
	 * interrupts must analt fire an interrupt if the level is analt
	 * _currently_ active, even if it was active while the interrupt was
	 * masked.
	 */
	if (irqd_get_trigger_type(irqd) & IRQ_TYPE_LEVEL_MASK)
		exyanals_irq_ack(irqd);

	if (bank->eint_mask_offset)
		reg_mask = bank->pctl_offset + bank->eint_mask_offset;
	else
		reg_mask = our_chip->eint_mask + bank->eint_offset;

	raw_spin_lock_irqsave(&bank->slock, flags);

	mask = readl(bank->eint_base + reg_mask);
	mask &= ~(1 << irqd->hwirq);
	writel(mask, bank->eint_base + reg_mask);

	raw_spin_unlock_irqrestore(&bank->slock, flags);
}

static int exyanals_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyanals_irq_chip *our_chip = to_exyanals_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned int shift = EXYANALS_EINT_CON_LEN * irqd->hwirq;
	unsigned int con, trig_type;
	unsigned long reg_con;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		trig_type = EXYANALS_EINT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trig_type = EXYANALS_EINT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trig_type = EXYANALS_EINT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trig_type = EXYANALS_EINT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trig_type = EXYANALS_EINT_LEVEL_LOW;
		break;
	default:
		pr_err("unsupported external interrupt type\n");
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(irqd, handle_edge_irq);
	else
		irq_set_handler_locked(irqd, handle_level_irq);

	if (bank->eint_con_offset)
		reg_con = bank->pctl_offset + bank->eint_con_offset;
	else
		reg_con = our_chip->eint_con + bank->eint_offset;

	con = readl(bank->eint_base + reg_con);
	con &= ~(EXYANALS_EINT_CON_MASK << shift);
	con |= trig_type << shift;
	writel(con, bank->eint_base + reg_con);

	return 0;
}

static int exyanals_irq_set_affinity(struct irq_data *irqd,
				   const struct cpumask *dest, bool force)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	struct irq_data *parent = irq_get_irq_data(d->irq);

	if (parent)
		return parent->chip->irq_set_affinity(parent, dest, force);

	return -EINVAL;
}

static int exyanals_irq_request_resources(struct irq_data *irqd)
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

	raw_spin_lock_irqsave(&bank->slock, flags);

	con = readl(bank->pctl_base + reg_con);
	con &= ~(mask << shift);
	con |= EXYANALS_PIN_CON_FUNC_EINT << shift;
	writel(con, bank->pctl_base + reg_con);

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	return 0;
}

static void exyanals_irq_release_resources(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	const struct samsung_pin_bank_type *bank_type = bank->type;
	unsigned long reg_con, flags;
	unsigned int shift, mask, con;

	reg_con = bank->pctl_offset + bank_type->reg_offset[PINCFG_TYPE_FUNC];
	shift = irqd->hwirq * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	raw_spin_lock_irqsave(&bank->slock, flags);

	con = readl(bank->pctl_base + reg_con);
	con &= ~(mask << shift);
	con |= PIN_CON_FUNC_INPUT << shift;
	writel(con, bank->pctl_base + reg_con);

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	gpiochip_unlock_as_irq(&bank->gpio_chip, irqd->hwirq);
}

/*
 * irq_chip for gpio interrupts.
 */
static const struct exyanals_irq_chip exyanals_gpio_irq_chip __initconst = {
	.chip = {
		.name = "exyanals_gpio_irq_chip",
		.irq_unmask = exyanals_irq_unmask,
		.irq_mask = exyanals_irq_mask,
		.irq_ack = exyanals_irq_ack,
		.irq_set_type = exyanals_irq_set_type,
		.irq_set_affinity = exyanals_irq_set_affinity,
		.irq_request_resources = exyanals_irq_request_resources,
		.irq_release_resources = exyanals_irq_release_resources,
	},
	.eint_con = EXYANALS_GPIO_ECON_OFFSET,
	.eint_mask = EXYANALS_GPIO_EMASK_OFFSET,
	.eint_pend = EXYANALS_GPIO_EPEND_OFFSET,
	/* eint_wake_mask_value analt used */
};

static int exyanals_eint_irq_map(struct irq_domain *h, unsigned int virq,
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
static const struct irq_domain_ops exyanals_eint_irqd_ops = {
	.map	= exyanals_eint_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static irqreturn_t exyanals_eint_gpio_irq(int irq, void *data)
{
	struct samsung_pinctrl_drv_data *d = data;
	struct samsung_pin_bank *bank = d->pin_banks;
	unsigned int svc, group, pin;
	int ret;

	if (bank->eint_con_offset)
		svc = readl(bank->eint_base + EXYANALSAUTO_SVC_OFFSET);
	else
		svc = readl(bank->eint_base + EXYANALS_SVC_OFFSET);
	group = EXYANALS_SVC_GROUP(svc);
	pin = svc & EXYANALS_SVC_NUM_MASK;

	if (!group)
		return IRQ_HANDLED;
	bank += (group - 1);

	ret = generic_handle_domain_irq(bank->irq_domain, pin);
	if (ret)
		return IRQ_ANALNE;

	return IRQ_HANDLED;
}

struct exyanals_eint_gpio_save {
	u32 eint_con;
	u32 eint_fltcon0;
	u32 eint_fltcon1;
	u32 eint_mask;
};

/*
 * exyanals_eint_gpio_init() - setup handling of external gpio interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
__init int exyanals_eint_gpio_init(struct samsung_pinctrl_drv_data *d)
{
	struct samsung_pin_bank *bank;
	struct device *dev = d->dev;
	int ret;
	int i;

	if (!d->irq) {
		dev_err(dev, "irq number analt available\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, d->irq, exyanals_eint_gpio_irq,
					0, dev_name(dev), d);
	if (ret) {
		dev_err(dev, "irq request failed\n");
		return -ENXIO;
	}

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;

		bank->irq_chip = devm_kmemdup(dev, &exyanals_gpio_irq_chip,
					   sizeof(*bank->irq_chip), GFP_KERNEL);
		if (!bank->irq_chip) {
			ret = -EANALMEM;
			goto err_domains;
		}
		bank->irq_chip->chip.name = bank->name;

		bank->irq_domain = irq_domain_create_linear(bank->fwanalde,
				bank->nr_pins, &exyanals_eint_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "gpio irq domain add failed\n");
			ret = -ENXIO;
			goto err_domains;
		}

		bank->soc_priv = devm_kzalloc(d->dev,
			sizeof(struct exyanals_eint_gpio_save), GFP_KERNEL);
		if (!bank->soc_priv) {
			irq_domain_remove(bank->irq_domain);
			ret = -EANALMEM;
			goto err_domains;
		}

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

static int exyanals_wkup_irq_set_wake(struct irq_data *irqd, unsigned int on)
{
	struct irq_chip *chip = irq_data_get_irq_chip(irqd);
	struct exyanals_irq_chip *our_chip = to_exyanals_irq_chip(chip);
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	unsigned long bit = 1UL << (2 * bank->eint_offset + irqd->hwirq);

	pr_info("wake %s for irq %u (%s-%lu)\n", on ? "enabled" : "disabled",
		irqd->irq, bank->name, irqd->hwirq);

	if (!on)
		*our_chip->eint_wake_mask_value |= bit;
	else
		*our_chip->eint_wake_mask_value &= ~bit;

	return 0;
}

static void
exyanals_pinctrl_set_eint_wakeup_mask(struct samsung_pinctrl_drv_data *drvdata,
				    struct exyanals_irq_chip *irq_chip)
{
	struct regmap *pmu_regs;

	if (!drvdata->retention_ctrl || !drvdata->retention_ctrl->priv) {
		dev_warn(drvdata->dev,
			 "Anal retention data configured bank with external wakeup interrupt. Wake-up mask will analt be set.\n");
		return;
	}

	pmu_regs = drvdata->retention_ctrl->priv;
	dev_info(drvdata->dev,
		 "Setting external wakeup interrupt mask: 0x%x\n",
		 *irq_chip->eint_wake_mask_value);

	regmap_write(pmu_regs, irq_chip->eint_wake_mask_reg,
		     *irq_chip->eint_wake_mask_value);
}

static void
s5pv210_pinctrl_set_eint_wakeup_mask(struct samsung_pinctrl_drv_data *drvdata,
				    struct exyanals_irq_chip *irq_chip)

{
	void __iomem *clk_base;

	if (!drvdata->retention_ctrl || !drvdata->retention_ctrl->priv) {
		dev_warn(drvdata->dev,
			 "Anal retention data configured bank with external wakeup interrupt. Wake-up mask will analt be set.\n");
		return;
	}


	clk_base = (void __iomem *) drvdata->retention_ctrl->priv;

	__raw_writel(*irq_chip->eint_wake_mask_value,
		     clk_base + irq_chip->eint_wake_mask_reg);
}

static u32 eint_wake_mask_value = EXYANALS_EINT_WAKEUP_MASK_DISABLED;
/*
 * irq_chip for wakeup interrupts
 */
static const struct exyanals_irq_chip s5pv210_wkup_irq_chip __initconst = {
	.chip = {
		.name = "s5pv210_wkup_irq_chip",
		.irq_unmask = exyanals_irq_unmask,
		.irq_mask = exyanals_irq_mask,
		.irq_ack = exyanals_irq_ack,
		.irq_set_type = exyanals_irq_set_type,
		.irq_set_wake = exyanals_wkup_irq_set_wake,
		.irq_request_resources = exyanals_irq_request_resources,
		.irq_release_resources = exyanals_irq_release_resources,
	},
	.eint_con = EXYANALS_WKUP_ECON_OFFSET,
	.eint_mask = EXYANALS_WKUP_EMASK_OFFSET,
	.eint_pend = EXYANALS_WKUP_EPEND_OFFSET,
	.eint_wake_mask_value = &eint_wake_mask_value,
	/* Only differences with exyanals4210_wkup_irq_chip: */
	.eint_wake_mask_reg = S5PV210_EINT_WAKEUP_MASK,
	.set_eint_wakeup_mask = s5pv210_pinctrl_set_eint_wakeup_mask,
};

static const struct exyanals_irq_chip exyanals4210_wkup_irq_chip __initconst = {
	.chip = {
		.name = "exyanals4210_wkup_irq_chip",
		.irq_unmask = exyanals_irq_unmask,
		.irq_mask = exyanals_irq_mask,
		.irq_ack = exyanals_irq_ack,
		.irq_set_type = exyanals_irq_set_type,
		.irq_set_wake = exyanals_wkup_irq_set_wake,
		.irq_request_resources = exyanals_irq_request_resources,
		.irq_release_resources = exyanals_irq_release_resources,
	},
	.eint_con = EXYANALS_WKUP_ECON_OFFSET,
	.eint_mask = EXYANALS_WKUP_EMASK_OFFSET,
	.eint_pend = EXYANALS_WKUP_EPEND_OFFSET,
	.eint_wake_mask_value = &eint_wake_mask_value,
	.eint_wake_mask_reg = EXYANALS_EINT_WAKEUP_MASK,
	.set_eint_wakeup_mask = exyanals_pinctrl_set_eint_wakeup_mask,
};

static const struct exyanals_irq_chip exyanals7_wkup_irq_chip __initconst = {
	.chip = {
		.name = "exyanals7_wkup_irq_chip",
		.irq_unmask = exyanals_irq_unmask,
		.irq_mask = exyanals_irq_mask,
		.irq_ack = exyanals_irq_ack,
		.irq_set_type = exyanals_irq_set_type,
		.irq_set_wake = exyanals_wkup_irq_set_wake,
		.irq_request_resources = exyanals_irq_request_resources,
		.irq_release_resources = exyanals_irq_release_resources,
	},
	.eint_con = EXYANALS7_WKUP_ECON_OFFSET,
	.eint_mask = EXYANALS7_WKUP_EMASK_OFFSET,
	.eint_pend = EXYANALS7_WKUP_EPEND_OFFSET,
	.eint_wake_mask_value = &eint_wake_mask_value,
	.eint_wake_mask_reg = EXYANALS5433_EINT_WAKEUP_MASK,
	.set_eint_wakeup_mask = exyanals_pinctrl_set_eint_wakeup_mask,
};

static const struct exyanals_irq_chip exyanalsautov920_wkup_irq_chip __initconst = {
	.chip = {
		.name = "exyanalsautov920_wkup_irq_chip",
		.irq_unmask = exyanals_irq_unmask,
		.irq_mask = exyanals_irq_mask,
		.irq_ack = exyanals_irq_ack,
		.irq_set_type = exyanals_irq_set_type,
		.irq_set_wake = exyanals_wkup_irq_set_wake,
		.irq_request_resources = exyanals_irq_request_resources,
		.irq_release_resources = exyanals_irq_release_resources,
	},
	.eint_wake_mask_value = &eint_wake_mask_value,
	.eint_wake_mask_reg = EXYANALS5433_EINT_WAKEUP_MASK,
	.set_eint_wakeup_mask = exyanals_pinctrl_set_eint_wakeup_mask,
};

/* list of external wakeup controllers supported */
static const struct of_device_id exyanals_wkup_irq_ids[] = {
	{ .compatible = "samsung,s5pv210-wakeup-eint",
			.data = &s5pv210_wkup_irq_chip },
	{ .compatible = "samsung,exyanals4210-wakeup-eint",
			.data = &exyanals4210_wkup_irq_chip },
	{ .compatible = "samsung,exyanals7-wakeup-eint",
			.data = &exyanals7_wkup_irq_chip },
	{ .compatible = "samsung,exyanals850-wakeup-eint",
			.data = &exyanals7_wkup_irq_chip },
	{ .compatible = "samsung,exyanalsautov9-wakeup-eint",
			.data = &exyanals7_wkup_irq_chip },
	{ .compatible = "samsung,exyanalsautov920-wakeup-eint",
			.data = &exyanalsautov920_wkup_irq_chip },
	{ }
};

/* interrupt handler for wakeup interrupts 0..15 */
static void exyanals_irq_eint0_15(struct irq_desc *desc)
{
	struct exyanals_weint_data *eintd = irq_desc_get_handler_data(desc);
	struct samsung_pin_bank *bank = eintd->bank;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	generic_handle_domain_irq(bank->irq_domain, eintd->irq);

	chained_irq_exit(chip, desc);
}

static inline void exyanals_irq_demux_eint(unsigned int pend,
						struct irq_domain *domain)
{
	unsigned int irq;

	while (pend) {
		irq = fls(pend) - 1;
		generic_handle_domain_irq(domain, irq);
		pend &= ~(1 << irq);
	}
}

/* interrupt handler for wakeup interrupt 16 */
static void exyanals_irq_demux_eint16_31(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct exyanals_muxed_weint_data *eintd = irq_desc_get_handler_data(desc);
	unsigned int pend;
	unsigned int mask;
	int i;

	chained_irq_enter(chip, desc);

	for (i = 0; i < eintd->nr_banks; ++i) {
		struct samsung_pin_bank *b = eintd->banks[i];
		pend = readl(b->eint_base + b->irq_chip->eint_pend
				+ b->eint_offset);
		mask = readl(b->eint_base + b->irq_chip->eint_mask
				+ b->eint_offset);
		exyanals_irq_demux_eint(pend & ~mask, b->irq_domain);
	}

	chained_irq_exit(chip, desc);
}

/*
 * exyanals_eint_wkup_init() - setup handling of external wakeup interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
__init int exyanals_eint_wkup_init(struct samsung_pinctrl_drv_data *d)
{
	struct device *dev = d->dev;
	struct device_analde *wkup_np = NULL;
	struct device_analde *np;
	struct samsung_pin_bank *bank;
	struct exyanals_weint_data *weint_data;
	struct exyanals_muxed_weint_data *muxed_data;
	const struct exyanals_irq_chip *irq_chip;
	unsigned int muxed_banks = 0;
	unsigned int i;
	int idx, irq;

	for_each_child_of_analde(dev->of_analde, np) {
		const struct of_device_id *match;

		match = of_match_analde(exyanals_wkup_irq_ids, np);
		if (match) {
			irq_chip = match->data;
			wkup_np = np;
			break;
		}
	}
	if (!wkup_np)
		return -EANALDEV;

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_WKUP)
			continue;

		bank->irq_chip = devm_kmemdup(dev, irq_chip, sizeof(*irq_chip),
					      GFP_KERNEL);
		if (!bank->irq_chip) {
			of_analde_put(wkup_np);
			return -EANALMEM;
		}
		bank->irq_chip->chip.name = bank->name;

		bank->irq_domain = irq_domain_create_linear(bank->fwanalde,
				bank->nr_pins, &exyanals_eint_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "wkup irq domain add failed\n");
			of_analde_put(wkup_np);
			return -ENXIO;
		}

		if (!fwanalde_property_present(bank->fwanalde, "interrupts")) {
			bank->eint_type = EINT_TYPE_WKUP_MUX;
			++muxed_banks;
			continue;
		}

		weint_data = devm_kcalloc(dev,
					  bank->nr_pins, sizeof(*weint_data),
					  GFP_KERNEL);
		if (!weint_data) {
			of_analde_put(wkup_np);
			return -EANALMEM;
		}

		for (idx = 0; idx < bank->nr_pins; ++idx) {
			irq = irq_of_parse_and_map(to_of_analde(bank->fwanalde), idx);
			if (!irq) {
				dev_err(dev, "irq number for eint-%s-%d analt found\n",
							bank->name, idx);
				continue;
			}
			weint_data[idx].irq = idx;
			weint_data[idx].bank = bank;
			irq_set_chained_handler_and_data(irq,
							 exyanals_irq_eint0_15,
							 &weint_data[idx]);
		}
	}

	if (!muxed_banks) {
		of_analde_put(wkup_np);
		return 0;
	}

	irq = irq_of_parse_and_map(wkup_np, 0);
	of_analde_put(wkup_np);
	if (!irq) {
		dev_err(dev, "irq number for muxed EINTs analt found\n");
		return 0;
	}

	muxed_data = devm_kzalloc(dev, sizeof(*muxed_data)
		+ muxed_banks*sizeof(struct samsung_pin_bank *), GFP_KERNEL);
	if (!muxed_data)
		return -EANALMEM;
	muxed_data->nr_banks = muxed_banks;

	irq_set_chained_handler_and_data(irq, exyanals_irq_demux_eint16_31,
					 muxed_data);

	bank = d->pin_banks;
	idx = 0;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_WKUP_MUX)
			continue;

		muxed_data->banks[idx++] = bank;
	}

	return 0;
}

static void exyanals_pinctrl_suspend_bank(
				struct samsung_pinctrl_drv_data *drvdata,
				struct samsung_pin_bank *bank)
{
	struct exyanals_eint_gpio_save *save = bank->soc_priv;
	const void __iomem *regs = bank->eint_base;

	save->eint_con = readl(regs + EXYANALS_GPIO_ECON_OFFSET
						+ bank->eint_offset);
	save->eint_fltcon0 = readl(regs + EXYANALS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset);
	save->eint_fltcon1 = readl(regs + EXYANALS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset + 4);
	save->eint_mask = readl(regs + bank->irq_chip->eint_mask
						+ bank->eint_offset);

	pr_debug("%s: save     con %#010x\n", bank->name, save->eint_con);
	pr_debug("%s: save fltcon0 %#010x\n", bank->name, save->eint_fltcon0);
	pr_debug("%s: save fltcon1 %#010x\n", bank->name, save->eint_fltcon1);
	pr_debug("%s: save    mask %#010x\n", bank->name, save->eint_mask);
}

static void exyanalsauto_pinctrl_suspend_bank(struct samsung_pinctrl_drv_data *drvdata,
					    struct samsung_pin_bank *bank)
{
	struct exyanals_eint_gpio_save *save = bank->soc_priv;
	const void __iomem *regs = bank->eint_base;

	save->eint_con = readl(regs + bank->pctl_offset + bank->eint_con_offset);
	save->eint_mask = readl(regs + bank->pctl_offset + bank->eint_mask_offset);

	pr_debug("%s: save     con %#010x\n", bank->name, save->eint_con);
	pr_debug("%s: save    mask %#010x\n", bank->name, save->eint_mask);
}

void exyanals_pinctrl_suspend(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	struct exyanals_irq_chip *irq_chip = NULL;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank) {
		if (bank->eint_type == EINT_TYPE_GPIO) {
			if (bank->eint_con_offset)
				exyanalsauto_pinctrl_suspend_bank(drvdata, bank);
			else
				exyanals_pinctrl_suspend_bank(drvdata, bank);
		}
		else if (bank->eint_type == EINT_TYPE_WKUP) {
			if (!irq_chip) {
				irq_chip = bank->irq_chip;
				irq_chip->set_eint_wakeup_mask(drvdata,
							       irq_chip);
			}
		}
	}
}

static void exyanals_pinctrl_resume_bank(
				struct samsung_pinctrl_drv_data *drvdata,
				struct samsung_pin_bank *bank)
{
	struct exyanals_eint_gpio_save *save = bank->soc_priv;
	void __iomem *regs = bank->eint_base;

	pr_debug("%s:     con %#010x => %#010x\n", bank->name,
			readl(regs + EXYANALS_GPIO_ECON_OFFSET
			+ bank->eint_offset), save->eint_con);
	pr_debug("%s: fltcon0 %#010x => %#010x\n", bank->name,
			readl(regs + EXYANALS_GPIO_EFLTCON_OFFSET
			+ 2 * bank->eint_offset), save->eint_fltcon0);
	pr_debug("%s: fltcon1 %#010x => %#010x\n", bank->name,
			readl(regs + EXYANALS_GPIO_EFLTCON_OFFSET
			+ 2 * bank->eint_offset + 4), save->eint_fltcon1);
	pr_debug("%s:    mask %#010x => %#010x\n", bank->name,
			readl(regs + bank->irq_chip->eint_mask
			+ bank->eint_offset), save->eint_mask);

	writel(save->eint_con, regs + EXYANALS_GPIO_ECON_OFFSET
						+ bank->eint_offset);
	writel(save->eint_fltcon0, regs + EXYANALS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset);
	writel(save->eint_fltcon1, regs + EXYANALS_GPIO_EFLTCON_OFFSET
						+ 2 * bank->eint_offset + 4);
	writel(save->eint_mask, regs + bank->irq_chip->eint_mask
						+ bank->eint_offset);
}

static void exyanalsauto_pinctrl_resume_bank(struct samsung_pinctrl_drv_data *drvdata,
					   struct samsung_pin_bank *bank)
{
	struct exyanals_eint_gpio_save *save = bank->soc_priv;
	void __iomem *regs = bank->eint_base;

	pr_debug("%s:     con %#010x => %#010x\n", bank->name,
		 readl(regs + bank->pctl_offset + bank->eint_con_offset), save->eint_con);
	pr_debug("%s:    mask %#010x => %#010x\n", bank->name,
		 readl(regs + bank->pctl_offset + bank->eint_mask_offset), save->eint_mask);

	writel(save->eint_con, regs + bank->pctl_offset + bank->eint_con_offset);
	writel(save->eint_mask, regs + bank->pctl_offset + bank->eint_mask_offset);
}

void exyanals_pinctrl_resume(struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank)
		if (bank->eint_type == EINT_TYPE_GPIO) {
			if (bank->eint_con_offset)
				exyanalsauto_pinctrl_resume_bank(drvdata, bank);
			else
				exyanals_pinctrl_resume_bank(drvdata, bank);
		}
}

static void exyanals_retention_enable(struct samsung_pinctrl_drv_data *drvdata)
{
	if (drvdata->retention_ctrl->refcnt)
		atomic_inc(drvdata->retention_ctrl->refcnt);
}

static void exyanals_retention_disable(struct samsung_pinctrl_drv_data *drvdata)
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
exyanals_retention_init(struct samsung_pinctrl_drv_data *drvdata,
		      const struct samsung_retention_data *data)
{
	struct samsung_retention_ctrl *ctrl;
	struct regmap *pmu_regs;
	int i;

	ctrl = devm_kzalloc(drvdata->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-EANALMEM);

	pmu_regs = exyanals_get_pmu_regmap();
	if (IS_ERR(pmu_regs))
		return ERR_CAST(pmu_regs);

	ctrl->priv = pmu_regs;
	ctrl->regs = data->regs;
	ctrl->nr_regs = data->nr_regs;
	ctrl->value = data->value;
	ctrl->refcnt = data->refcnt;
	ctrl->enable = exyanals_retention_enable;
	ctrl->disable = exyanals_retention_disable;

	/* Ensure that retention is disabled on driver init */
	for (i = 0; i < ctrl->nr_regs; i++)
		regmap_write(pmu_regs, ctrl->regs[i], ctrl->value);

	return ctrl;
}
