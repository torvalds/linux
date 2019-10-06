// SPDX-License-Identifier: GPL-2.0-only
/*
 * MFD core driver for the Richtek RT5033.
 *
 * RT5033 comprises multiple sub-devices switcing charger, fuel gauge,
 * flash LED, current source, LDO and BUCK regulators.
 *
 * Copyright (C) 2014 Samsung Electronics, Co., Ltd.
 * Author: Beomho Seo <beomho.seo@samsung.com>
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rt5033.h>
#include <linux/mfd/rt5033-private.h>

static const struct regmap_irq rt5033_irqs[] = {
	{ .mask = RT5033_PMIC_IRQ_BUCKOCP, },
	{ .mask = RT5033_PMIC_IRQ_BUCKLV, },
	{ .mask = RT5033_PMIC_IRQ_SAFELDOLV, },
	{ .mask = RT5033_PMIC_IRQ_LDOLV, },
	{ .mask = RT5033_PMIC_IRQ_OT, },
	{ .mask = RT5033_PMIC_IRQ_VDDA_UV, },
};

static const struct regmap_irq_chip rt5033_irq_chip = {
	.name		= "rt5033",
	.status_base	= RT5033_REG_PMIC_IRQ_STAT,
	.mask_base	= RT5033_REG_PMIC_IRQ_CTRL,
	.mask_invert	= true,
	.num_regs	= 1,
	.irqs		= rt5033_irqs,
	.num_irqs	= ARRAY_SIZE(rt5033_irqs),
};

static const struct mfd_cell rt5033_devs[] = {
	{ .name = "rt5033-regulator", },
	{
		.name = "rt5033-charger",
		.of_compatible = "richtek,rt5033-charger",
	}, {
		.name = "rt5033-battery",
		.of_compatible = "richtek,rt5033-battery",
	}, {
		.name = "rt5033-led",
		.of_compatible = "richtek,rt5033-led",
	},
};

static const struct regmap_config rt5033_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= RT5033_REG_END,
};

static int rt5033_i2c_probe(struct i2c_client *i2c,
				const struct i2c_device_id *id)
{
	struct rt5033_dev *rt5033;
	unsigned int dev_id;
	int ret;

	rt5033 = devm_kzalloc(&i2c->dev, sizeof(*rt5033), GFP_KERNEL);
	if (!rt5033)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5033);
	rt5033->dev = &i2c->dev;
	rt5033->irq = i2c->irq;
	rt5033->wakeup = true;

	rt5033->regmap = devm_regmap_init_i2c(i2c, &rt5033_regmap_config);
	if (IS_ERR(rt5033->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate register map.\n");
		return PTR_ERR(rt5033->regmap);
	}

	ret = regmap_read(rt5033->regmap, RT5033_REG_DEVICE_ID, &dev_id);
	if (ret) {
		dev_err(&i2c->dev, "Device not found\n");
		return -ENODEV;
	}
	dev_info(&i2c->dev, "Device found Device ID: %04x\n", dev_id);

	ret = regmap_add_irq_chip(rt5033->regmap, rt5033->irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			0, &rt5033_irq_chip, &rt5033->irq_data);
	if (ret) {
		dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n",
							rt5033->irq, ret);
		return ret;
	}

	ret = devm_mfd_add_devices(rt5033->dev, -1, rt5033_devs,
				   ARRAY_SIZE(rt5033_devs), NULL, 0,
				   regmap_irq_get_domain(rt5033->irq_data));
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to add RT5033 child devices.\n");
		return ret;
	}

	device_init_wakeup(rt5033->dev, rt5033->wakeup);

	return 0;
}

static const struct i2c_device_id rt5033_i2c_id[] = {
	{ "rt5033", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5033_i2c_id);

static const struct of_device_id rt5033_dt_match[] = {
	{ .compatible = "richtek,rt5033", },
	{ }
};
MODULE_DEVICE_TABLE(of, rt5033_dt_match);

static struct i2c_driver rt5033_driver = {
	.driver = {
		.name = "rt5033",
		.of_match_table = of_match_ptr(rt5033_dt_match),
	},
	.probe = rt5033_i2c_probe,
	.id_table = rt5033_i2c_id,
};
module_i2c_driver(rt5033_driver);

MODULE_DESCRIPTION("Richtek RT5033 multi-function core driver");
MODULE_AUTHOR("Beomho Seo <beomho.seo@samsung.com>");
MODULE_LICENSE("GPL");
