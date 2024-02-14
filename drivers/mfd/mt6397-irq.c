// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/suspend.h>
#include <linux/mfd/mt6323/core.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6331/core.h>
#include <linux/mfd/mt6331/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6397/registers.h>

static void mt6397_irq_lock(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);

	mutex_lock(&mt6397->irqlock);
}

static void mt6397_irq_sync_unlock(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);

	regmap_write(mt6397->regmap, mt6397->int_con[0],
		     mt6397->irq_masks_cur[0]);
	regmap_write(mt6397->regmap, mt6397->int_con[1],
		     mt6397->irq_masks_cur[1]);

	mutex_unlock(&mt6397->irqlock);
}

static void mt6397_irq_disable(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);
	int shift = data->hwirq & 0xf;
	int reg = data->hwirq >> 4;

	mt6397->irq_masks_cur[reg] &= ~BIT(shift);
}

static void mt6397_irq_enable(struct irq_data *data)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(data);
	int shift = data->hwirq & 0xf;
	int reg = data->hwirq >> 4;

	mt6397->irq_masks_cur[reg] |= BIT(shift);
}

#ifdef CONFIG_PM_SLEEP
static int mt6397_irq_set_wake(struct irq_data *irq_data, unsigned int on)
{
	struct mt6397_chip *mt6397 = irq_data_get_irq_chip_data(irq_data);
	int shift = irq_data->hwirq & 0xf;
	int reg = irq_data->hwirq >> 4;

	if (on)
		mt6397->wake_mask[reg] |= BIT(shift);
	else
		mt6397->wake_mask[reg] &= ~BIT(shift);

	return 0;
}
#else
#define mt6397_irq_set_wake NULL
#endif

static struct irq_chip mt6397_irq_chip = {
	.name = "mt6397-irq",
	.irq_bus_lock = mt6397_irq_lock,
	.irq_bus_sync_unlock = mt6397_irq_sync_unlock,
	.irq_enable = mt6397_irq_enable,
	.irq_disable = mt6397_irq_disable,
	.irq_set_wake = mt6397_irq_set_wake,
};

static void mt6397_irq_handle_reg(struct mt6397_chip *mt6397, int reg,
				  int irqbase)
{
	unsigned int status = 0;
	int i, irq, ret;

	ret = regmap_read(mt6397->regmap, reg, &status);
	if (ret) {
		dev_err(mt6397->dev, "Failed to read irq status: %d\n", ret);
		return;
	}

	for (i = 0; i < 16; i++) {
		if (status & BIT(i)) {
			irq = irq_find_mapping(mt6397->irq_domain, irqbase + i);
			if (irq)
				handle_nested_irq(irq);
		}
	}

	regmap_write(mt6397->regmap, reg, status);
}

static irqreturn_t mt6397_irq_thread(int irq, void *data)
{
	struct mt6397_chip *mt6397 = data;

	mt6397_irq_handle_reg(mt6397, mt6397->int_status[0], 0);
	mt6397_irq_handle_reg(mt6397, mt6397->int_status[1], 16);

	return IRQ_HANDLED;
}

static int mt6397_irq_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hw)
{
	struct mt6397_chip *mt6397 = d->host_data;

	irq_set_chip_data(irq, mt6397);
	irq_set_chip_and_handler(irq, &mt6397_irq_chip, handle_level_irq);
	irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops mt6397_irq_domain_ops = {
	.map = mt6397_irq_domain_map,
};

static int mt6397_irq_pm_notifier(struct notifier_block *notifier,
				  unsigned long pm_event, void *unused)
{
	struct mt6397_chip *chip =
		container_of(notifier, struct mt6397_chip, pm_nb);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		regmap_write(chip->regmap,
			     chip->int_con[0], chip->wake_mask[0]);
		regmap_write(chip->regmap,
			     chip->int_con[1], chip->wake_mask[1]);
		enable_irq_wake(chip->irq);
		break;

	case PM_POST_SUSPEND:
		regmap_write(chip->regmap,
			     chip->int_con[0], chip->irq_masks_cur[0]);
		regmap_write(chip->regmap,
			     chip->int_con[1], chip->irq_masks_cur[1]);
		disable_irq_wake(chip->irq);
		break;

	default:
		break;
	}

	return NOTIFY_DONE;
}

int mt6397_irq_init(struct mt6397_chip *chip)
{
	int ret;

	mutex_init(&chip->irqlock);

	switch (chip->chip_id) {
	case MT6323_CHIP_ID:
		chip->int_con[0] = MT6323_INT_CON0;
		chip->int_con[1] = MT6323_INT_CON1;
		chip->int_status[0] = MT6323_INT_STATUS0;
		chip->int_status[1] = MT6323_INT_STATUS1;
		break;
	case MT6331_CHIP_ID:
		chip->int_con[0] = MT6331_INT_CON0;
		chip->int_con[1] = MT6331_INT_CON1;
		chip->int_status[0] = MT6331_INT_STATUS_CON0;
		chip->int_status[1] = MT6331_INT_STATUS_CON1;
		break;
	case MT6391_CHIP_ID:
	case MT6397_CHIP_ID:
		chip->int_con[0] = MT6397_INT_CON0;
		chip->int_con[1] = MT6397_INT_CON1;
		chip->int_status[0] = MT6397_INT_STATUS0;
		chip->int_status[1] = MT6397_INT_STATUS1;
		break;

	default:
		dev_err(chip->dev, "unsupported chip: 0x%x\n", chip->chip_id);
		return -ENODEV;
	}

	/* Mask all interrupt sources */
	regmap_write(chip->regmap, chip->int_con[0], 0x0);
	regmap_write(chip->regmap, chip->int_con[1], 0x0);

	chip->pm_nb.notifier_call = mt6397_irq_pm_notifier;
	chip->irq_domain = irq_domain_add_linear(chip->dev->of_node,
						 MT6397_IRQ_NR,
						 &mt6397_irq_domain_ops,
						 chip);
	if (!chip->irq_domain) {
		dev_err(chip->dev, "could not create irq domain\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
					mt6397_irq_thread, IRQF_ONESHOT,
					"mt6397-pmic", chip);
	if (ret) {
		dev_err(chip->dev, "failed to register irq=%d; err: %d\n",
			chip->irq, ret);
		return ret;
	}

	register_pm_notifier(&chip->pm_nb);
	return 0;
}
