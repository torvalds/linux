// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2020 MediaTek Inc.

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/mt6357/core.h>
#include <linux/mfd/mt6357/registers.h>
#include <linux/mfd/mt6358/core.h>
#include <linux/mfd/mt6358/registers.h>
#include <linux/mfd/mt6359/core.h>
#include <linux/mfd/mt6359/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define MTK_PMIC_REG_WIDTH 16

static const struct irq_top_t mt6357_ints[] = {
	MT6357_TOP_GEN(BUCK),
	MT6357_TOP_GEN(LDO),
	MT6357_TOP_GEN(PSC),
	MT6357_TOP_GEN(SCK),
	MT6357_TOP_GEN(BM),
	MT6357_TOP_GEN(HK),
	MT6357_TOP_GEN(AUD),
	MT6357_TOP_GEN(MISC),
};

static const struct irq_top_t mt6358_ints[] = {
	MT6358_TOP_GEN(BUCK),
	MT6358_TOP_GEN(LDO),
	MT6358_TOP_GEN(PSC),
	MT6358_TOP_GEN(SCK),
	MT6358_TOP_GEN(BM),
	MT6358_TOP_GEN(HK),
	MT6358_TOP_GEN(AUD),
	MT6358_TOP_GEN(MISC),
};

static const struct irq_top_t mt6359_ints[] = {
	MT6359_TOP_GEN(BUCK),
	MT6359_TOP_GEN(LDO),
	MT6359_TOP_GEN(PSC),
	MT6359_TOP_GEN(SCK),
	MT6359_TOP_GEN(BM),
	MT6359_TOP_GEN(HK),
	MT6359_TOP_GEN(AUD),
	MT6359_TOP_GEN(MISC),
};

static struct pmic_irq_data mt6357_irqd = {
	.num_top = ARRAY_SIZE(mt6357_ints),
	.num_pmic_irqs = MT6357_IRQ_NR,
	.top_int_status_reg = MT6357_TOP_INT_STATUS0,
	.pmic_ints = mt6357_ints,
};

static struct pmic_irq_data mt6358_irqd = {
	.num_top = ARRAY_SIZE(mt6358_ints),
	.num_pmic_irqs = MT6358_IRQ_NR,
	.top_int_status_reg = MT6358_TOP_INT_STATUS0,
	.pmic_ints = mt6358_ints,
};

static struct pmic_irq_data mt6359_irqd = {
	.num_top = ARRAY_SIZE(mt6359_ints),
	.num_pmic_irqs = MT6359_IRQ_NR,
	.top_int_status_reg = MT6359_TOP_INT_STATUS0,
	.pmic_ints = mt6359_ints,
};

static void pmic_irq_enable(struct irq_data *data)
{
	unsigned int hwirq = irqd_to_hwirq(data);
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);
	struct pmic_irq_data *irqd = chip->irq_data;

	irqd->enable_hwirq[hwirq] = true;
}

static void pmic_irq_disable(struct irq_data *data)
{
	unsigned int hwirq = irqd_to_hwirq(data);
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);
	struct pmic_irq_data *irqd = chip->irq_data;

	irqd->enable_hwirq[hwirq] = false;
}

static void pmic_irq_lock(struct irq_data *data)
{
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);

	mutex_lock(&chip->irqlock);
}

static void pmic_irq_sync_unlock(struct irq_data *data)
{
	unsigned int i, top_gp, gp_offset, en_reg, int_regs, shift;
	struct mt6397_chip *chip = irq_data_get_irq_chip_data(data);
	struct pmic_irq_data *irqd = chip->irq_data;

	for (i = 0; i < irqd->num_pmic_irqs; i++) {
		if (irqd->enable_hwirq[i] == irqd->cache_hwirq[i])
			continue;

		/* Find out the IRQ group */
		top_gp = 0;
		while ((top_gp + 1) < irqd->num_top &&
		       i >= irqd->pmic_ints[top_gp + 1].hwirq_base)
			top_gp++;

		/* Find the IRQ registers */
		gp_offset = i - irqd->pmic_ints[top_gp].hwirq_base;
		int_regs = gp_offset / MTK_PMIC_REG_WIDTH;
		shift = gp_offset % MTK_PMIC_REG_WIDTH;
		en_reg = irqd->pmic_ints[top_gp].en_reg +
			 (irqd->pmic_ints[top_gp].en_reg_shift * int_regs);

		regmap_update_bits(chip->regmap, en_reg, BIT(shift),
				   irqd->enable_hwirq[i] << shift);

		irqd->cache_hwirq[i] = irqd->enable_hwirq[i];
	}
	mutex_unlock(&chip->irqlock);
}

static struct irq_chip mt6358_irq_chip = {
	.name = "mt6358-irq",
	.flags = IRQCHIP_SKIP_SET_WAKE,
	.irq_enable = pmic_irq_enable,
	.irq_disable = pmic_irq_disable,
	.irq_bus_lock = pmic_irq_lock,
	.irq_bus_sync_unlock = pmic_irq_sync_unlock,
};

static void mt6358_irq_sp_handler(struct mt6397_chip *chip,
				  unsigned int top_gp)
{
	unsigned int irq_status, sta_reg, status;
	unsigned int hwirq, virq;
	int i, j, ret;
	struct pmic_irq_data *irqd = chip->irq_data;

	for (i = 0; i < irqd->pmic_ints[top_gp].num_int_regs; i++) {
		sta_reg = irqd->pmic_ints[top_gp].sta_reg +
			irqd->pmic_ints[top_gp].sta_reg_shift * i;

		ret = regmap_read(chip->regmap, sta_reg, &irq_status);
		if (ret) {
			dev_err(chip->dev,
				"Failed to read IRQ status, ret=%d\n", ret);
			return;
		}

		if (!irq_status)
			continue;

		status = irq_status;
		do {
			j = __ffs(status);

			hwirq = irqd->pmic_ints[top_gp].hwirq_base +
				MTK_PMIC_REG_WIDTH * i + j;

			virq = irq_find_mapping(chip->irq_domain, hwirq);
			if (virq)
				handle_nested_irq(virq);

			status &= ~BIT(j);
		} while (status);

		regmap_write(chip->regmap, sta_reg, irq_status);
	}
}

static irqreturn_t mt6358_irq_handler(int irq, void *data)
{
	struct mt6397_chip *chip = data;
	struct pmic_irq_data *irqd = chip->irq_data;
	unsigned int bit, i, top_irq_status = 0;
	int ret;

	ret = regmap_read(chip->regmap,
			  irqd->top_int_status_reg,
			  &top_irq_status);
	if (ret) {
		dev_err(chip->dev,
			"Failed to read status from the device, ret=%d\n", ret);
		return IRQ_NONE;
	}

	for (i = 0; i < irqd->num_top; i++) {
		bit = BIT(irqd->pmic_ints[i].top_offset);
		if (top_irq_status & bit) {
			mt6358_irq_sp_handler(chip, i);
			top_irq_status &= ~bit;
			if (!top_irq_status)
				break;
		}
	}

	return IRQ_HANDLED;
}

static int pmic_irq_domain_map(struct irq_domain *d, unsigned int irq,
			       irq_hw_number_t hw)
{
	struct mt6397_chip *mt6397 = d->host_data;

	irq_set_chip_data(irq, mt6397);
	irq_set_chip_and_handler(irq, &mt6358_irq_chip, handle_level_irq);
	irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops mt6358_irq_domain_ops = {
	.map = pmic_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

int mt6358_irq_init(struct mt6397_chip *chip)
{
	int i, j, ret;
	struct pmic_irq_data *irqd;

	switch (chip->chip_id) {
	case MT6357_CHIP_ID:
		chip->irq_data = &mt6357_irqd;
		break;

	case MT6358_CHIP_ID:
	case MT6366_CHIP_ID:
		chip->irq_data = &mt6358_irqd;
		break;

	case MT6359_CHIP_ID:
		chip->irq_data = &mt6359_irqd;
		break;

	default:
		dev_err(chip->dev, "unsupported chip: 0x%x\n", chip->chip_id);
		return -ENODEV;
	}

	mutex_init(&chip->irqlock);
	irqd = chip->irq_data;
	irqd->enable_hwirq = devm_kcalloc(chip->dev,
					  irqd->num_pmic_irqs,
					  sizeof(*irqd->enable_hwirq),
					  GFP_KERNEL);
	if (!irqd->enable_hwirq)
		return -ENOMEM;

	irqd->cache_hwirq = devm_kcalloc(chip->dev,
					 irqd->num_pmic_irqs,
					 sizeof(*irqd->cache_hwirq),
					 GFP_KERNEL);
	if (!irqd->cache_hwirq)
		return -ENOMEM;

	/* Disable all interrupts for initializing */
	for (i = 0; i < irqd->num_top; i++) {
		for (j = 0; j < irqd->pmic_ints[i].num_int_regs; j++)
			regmap_write(chip->regmap,
				     irqd->pmic_ints[i].en_reg +
				     irqd->pmic_ints[i].en_reg_shift * j, 0);
	}

	chip->irq_domain = irq_domain_create_linear(dev_fwnode(chip->dev), irqd->num_pmic_irqs,
						    &mt6358_irq_domain_ops, chip);
	if (!chip->irq_domain) {
		dev_err(chip->dev, "Could not create IRQ domain\n");
		return -ENODEV;
	}

	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					mt6358_irq_handler, IRQF_ONESHOT,
					mt6358_irq_chip.name, chip);
	if (ret) {
		dev_err(chip->dev, "Failed to register IRQ=%d, ret=%d\n",
			chip->irq, ret);
		return ret;
	}

	enable_irq_wake(chip->irq);
	return ret;
}
