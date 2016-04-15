/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Flora Fu, MediaTek
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6323/core.h>
#include <linux/mfd/mt6397/registers.h>
#include <linux/mfd/mt6323/registers.h>

#define MT6397_RTC_BASE		0xe000
#define MT6397_RTC_SIZE		0x3e

#define MT6323_CID_CODE		0x23
#define MT6391_CID_CODE		0x91
#define MT6397_CID_CODE		0x97

static const struct resource mt6397_rtc_resources[] = {
	{
		.start = MT6397_RTC_BASE,
		.end   = MT6397_RTC_BASE + MT6397_RTC_SIZE,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MT6397_IRQ_RTC,
		.end   = MT6397_IRQ_RTC,
		.flags = IORESOURCE_IRQ,
	},
};

static const struct mfd_cell mt6323_devs[] = {
	{
		.name = "mt6323-regulator",
		.of_compatible = "mediatek,mt6323-regulator"
	},
};

static const struct mfd_cell mt6397_devs[] = {
	{
		.name = "mt6397-rtc",
		.num_resources = ARRAY_SIZE(mt6397_rtc_resources),
		.resources = mt6397_rtc_resources,
		.of_compatible = "mediatek,mt6397-rtc",
	}, {
		.name = "mt6397-regulator",
		.of_compatible = "mediatek,mt6397-regulator",
	}, {
		.name = "mt6397-codec",
		.of_compatible = "mediatek,mt6397-codec",
	}, {
		.name = "mt6397-clk",
		.of_compatible = "mediatek,mt6397-clk",
	}, {
		.name = "mt6397-pinctrl",
		.of_compatible = "mediatek,mt6397-pinctrl",
	},
};

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
	unsigned int status;
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

static int mt6397_irq_init(struct mt6397_chip *mt6397)
{
	int ret;

	mutex_init(&mt6397->irqlock);

	/* Mask all interrupt sources */
	regmap_write(mt6397->regmap, mt6397->int_con[0], 0x0);
	regmap_write(mt6397->regmap, mt6397->int_con[1], 0x0);

	mt6397->irq_domain = irq_domain_add_linear(mt6397->dev->of_node,
		MT6397_IRQ_NR, &mt6397_irq_domain_ops, mt6397);
	if (!mt6397->irq_domain) {
		dev_err(mt6397->dev, "could not create irq domain\n");
		return -ENOMEM;
	}

	ret = devm_request_threaded_irq(mt6397->dev, mt6397->irq, NULL,
		mt6397_irq_thread, IRQF_ONESHOT, "mt6397-pmic", mt6397);
	if (ret) {
		dev_err(mt6397->dev, "failed to register irq=%d; err: %d\n",
			mt6397->irq, ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mt6397_irq_suspend(struct device *dev)
{
	struct mt6397_chip *chip = dev_get_drvdata(dev);

	regmap_write(chip->regmap, chip->int_con[0], chip->wake_mask[0]);
	regmap_write(chip->regmap, chip->int_con[1], chip->wake_mask[1]);

	enable_irq_wake(chip->irq);

	return 0;
}

static int mt6397_irq_resume(struct device *dev)
{
	struct mt6397_chip *chip = dev_get_drvdata(dev);

	regmap_write(chip->regmap, chip->int_con[0], chip->irq_masks_cur[0]);
	regmap_write(chip->regmap, chip->int_con[1], chip->irq_masks_cur[1]);

	disable_irq_wake(chip->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mt6397_pm_ops, mt6397_irq_suspend,
			mt6397_irq_resume);

static int mt6397_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int id;
	struct mt6397_chip *pmic;

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	pmic->dev = &pdev->dev;

	/*
	 * mt6397 MFD is child device of soc pmic wrapper.
	 * Regmap is set from its parent.
	 */
	pmic->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pmic->regmap)
		return -ENODEV;

	platform_set_drvdata(pdev, pmic);

	ret = regmap_read(pmic->regmap, MT6397_CID, &id);
	if (ret) {
		dev_err(pmic->dev, "Failed to read chip id: %d\n", ret);
		return ret;
	}

	pmic->irq = platform_get_irq(pdev, 0);
	if (pmic->irq <= 0)
		return pmic->irq;

	switch (id & 0xff) {
	case MT6323_CID_CODE:
		pmic->int_con[0] = MT6323_INT_CON0;
		pmic->int_con[1] = MT6323_INT_CON1;
		pmic->int_status[0] = MT6323_INT_STATUS0;
		pmic->int_status[1] = MT6323_INT_STATUS1;
		ret = mt6397_irq_init(pmic);
		if (ret)
			return ret;

		ret = devm_mfd_add_devices(&pdev->dev, -1, mt6323_devs,
					   ARRAY_SIZE(mt6323_devs), NULL,
					   0, NULL);
		break;

	case MT6397_CID_CODE:
	case MT6391_CID_CODE:
		pmic->int_con[0] = MT6397_INT_CON0;
		pmic->int_con[1] = MT6397_INT_CON1;
		pmic->int_status[0] = MT6397_INT_STATUS0;
		pmic->int_status[1] = MT6397_INT_STATUS1;
		ret = mt6397_irq_init(pmic);
		if (ret)
			return ret;

		ret = devm_mfd_add_devices(&pdev->dev, -1, mt6397_devs,
					   ARRAY_SIZE(mt6397_devs), NULL,
					   0, NULL);
		break;

	default:
		dev_err(&pdev->dev, "unsupported chip: %d\n", id);
		ret = -ENODEV;
		break;
	}

	if (ret) {
		irq_domain_remove(pmic->irq_domain);
		dev_err(&pdev->dev, "failed to add child devices: %d\n", ret);
	}

	return ret;
}

static const struct of_device_id mt6397_of_match[] = {
	{ .compatible = "mediatek,mt6397" },
	{ .compatible = "mediatek,mt6323" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6397_of_match);

static const struct platform_device_id mt6397_id[] = {
	{ "mt6397", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, mt6397_id);

static struct platform_driver mt6397_driver = {
	.probe = mt6397_probe,
	.driver = {
		.name = "mt6397",
		.of_match_table = of_match_ptr(mt6397_of_match),
		.pm = &mt6397_pm_ops,
	},
	.id_table = mt6397_id,
};

module_platform_driver(mt6397_driver);

MODULE_AUTHOR("Flora Fu, MediaTek");
MODULE_DESCRIPTION("Driver for MediaTek MT6397 PMIC");
MODULE_LICENSE("GPL");
