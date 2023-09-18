// SPDX-License-Identifier: GPL-2.0+
//
// MFD core driver for the Maxim MAX77843
//
// Copyright (C) 2015 Samsung Electronics
// Author: Jaewon Kim <jaewon02.kim@samsung.com>
// Author: Beomho Seo <beomho.seo@samsung.com>

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77843-private.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

static const struct mfd_cell max77843_devs[] = {
	{
		.name = "max77843-muic",
		.of_compatible = "maxim,max77843-muic",
	}, {
		.name = "max77843-regulator",
		.of_compatible = "maxim,max77843-regulator",
	}, {
		.name = "max77843-charger",
		.of_compatible = "maxim,max77843-charger"
	}, {
		.name = "max77843-fuelgauge",
		.of_compatible = "maxim,max77843-fuelgauge",
	}, {
		.name = "max77843-haptic",
		.of_compatible = "maxim,max77843-haptic",
	},
};

static const struct regmap_config max77843_charger_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX77843_CHG_REG_END,
};

static const struct regmap_config max77843_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX77843_SYS_REG_END,
};

static const struct regmap_irq max77843_irqs[] = {
	/* TOPSYS interrupts */
	{ .reg_offset = 0, .mask = MAX77843_SYS_IRQ_SYSUVLO_INT, },
	{ .reg_offset = 0, .mask = MAX77843_SYS_IRQ_SYSOVLO_INT, },
	{ .reg_offset = 0, .mask = MAX77843_SYS_IRQ_TSHDN_INT, },
	{ .reg_offset = 0, .mask = MAX77843_SYS_IRQ_TM_INT, },
};

static const struct regmap_irq_chip max77843_irq_chip = {
	.name		= "max77843",
	.status_base	= MAX77843_SYS_REG_SYSINTSRC,
	.mask_base	= MAX77843_SYS_REG_SYSINTMASK,
	.num_regs	= 1,
	.irqs		= max77843_irqs,
	.num_irqs	= ARRAY_SIZE(max77843_irqs),
};

/* Charger and Charger regulator use same regmap. */
static int max77843_chg_init(struct max77693_dev *max77843)
{
	int ret;

	max77843->i2c_chg = i2c_new_dummy_device(max77843->i2c->adapter, I2C_ADDR_CHG);
	if (IS_ERR(max77843->i2c_chg)) {
		dev_err(&max77843->i2c->dev,
				"Cannot allocate I2C device for Charger\n");
		return PTR_ERR(max77843->i2c_chg);
	}
	i2c_set_clientdata(max77843->i2c_chg, max77843);

	max77843->regmap_chg = devm_regmap_init_i2c(max77843->i2c_chg,
			&max77843_charger_regmap_config);
	if (IS_ERR(max77843->regmap_chg)) {
		ret = PTR_ERR(max77843->regmap_chg);
		goto err_chg_i2c;
	}

	return 0;

err_chg_i2c:
	i2c_unregister_device(max77843->i2c_chg);

	return ret;
}

static int max77843_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	struct max77693_dev *max77843;
	unsigned int reg_data;
	int ret;

	max77843 = devm_kzalloc(&i2c->dev, sizeof(*max77843), GFP_KERNEL);
	if (!max77843)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77843);
	max77843->dev = &i2c->dev;
	max77843->i2c = i2c;
	max77843->irq = i2c->irq;
	max77843->type = id->driver_data;

	max77843->regmap = devm_regmap_init_i2c(i2c,
			&max77843_regmap_config);
	if (IS_ERR(max77843->regmap)) {
		dev_err(&i2c->dev, "Failed to allocate topsys register map\n");
		return PTR_ERR(max77843->regmap);
	}

	ret = regmap_add_irq_chip(max77843->regmap, max77843->irq,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT | IRQF_SHARED,
			0, &max77843_irq_chip, &max77843->irq_data_topsys);
	if (ret) {
		dev_err(&i2c->dev, "Failed to add TOPSYS IRQ chip\n");
		return ret;
	}

	ret = regmap_read(max77843->regmap,
			MAX77843_SYS_REG_PMICID, &reg_data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read PMIC ID\n");
		goto err_pmic_id;
	}
	dev_info(&i2c->dev, "device ID: 0x%x\n", reg_data);

	ret = max77843_chg_init(max77843);
	if (ret) {
		dev_err(&i2c->dev, "Failed to init Charger\n");
		goto err_pmic_id;
	}

	ret = regmap_update_bits(max77843->regmap,
				 MAX77843_SYS_REG_INTSRCMASK,
				 MAX77843_INTSRC_MASK_MASK,
				 (unsigned int)~MAX77843_INTSRC_MASK_MASK);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to unmask interrupt source\n");
		goto err_pmic_id;
	}

	ret = mfd_add_devices(max77843->dev, -1, max77843_devs,
			      ARRAY_SIZE(max77843_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to add mfd device\n");
		goto err_pmic_id;
	}

	device_init_wakeup(max77843->dev, true);

	return 0;

err_pmic_id:
	regmap_del_irq_chip(max77843->irq, max77843->irq_data_topsys);

	return ret;
}

static const struct of_device_id max77843_dt_match[] = {
	{ .compatible = "maxim,max77843", },
	{ },
};

static const struct i2c_device_id max77843_id[] = {
	{ "max77843", TYPE_MAX77843, },
	{ },
};

static int __maybe_unused max77843_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77693_dev *max77843 = i2c_get_clientdata(i2c);

	disable_irq(max77843->irq);
	if (device_may_wakeup(dev))
		enable_irq_wake(max77843->irq);

	return 0;
}

static int __maybe_unused max77843_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77693_dev *max77843 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(max77843->irq);
	enable_irq(max77843->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(max77843_pm, max77843_suspend, max77843_resume);

static struct i2c_driver max77843_i2c_driver = {
	.driver	= {
		.name = "max77843",
		.pm = &max77843_pm,
		.of_match_table = max77843_dt_match,
		.suppress_bind_attrs = true,
	},
	.probe = max77843_probe,
	.id_table = max77843_id,
};

static int __init max77843_i2c_init(void)
{
	return i2c_add_driver(&max77843_i2c_driver);
}
subsys_initcall(max77843_i2c_init);
