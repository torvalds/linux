// SPDX-License-Identifier: GPL-2.0+
//
// max77686.c - mfd core driver for the Maxim 77686/802
//
// Copyright (C) 2012 Samsung Electronics
// Chiwoong Byun <woong.byun@samsung.com>
// Jonghwa Lee <jonghwa3.lee@samsung.com>
//
//This driver is based on max8997.c

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>

static const struct mfd_cell max77686_devs[] = {
	{ .name = "max77686-pmic", },
	{ .name = "max77686-rtc", },
	{ .name = "max77686-clk", },
};

static const struct mfd_cell max77802_devs[] = {
	{ .name = "max77802-pmic", },
	{ .name = "max77802-clk", },
	{ .name = "max77802-rtc", },
};

static bool max77802_pmic_is_accessible_reg(struct device *dev,
					    unsigned int reg)
{
	return reg < MAX77802_REG_PMIC_END;
}

static bool max77802_rtc_is_accessible_reg(struct device *dev,
					   unsigned int reg)
{
	return (reg >= MAX77802_RTC_INT && reg < MAX77802_RTC_END);
}

static bool max77802_is_accessible_reg(struct device *dev, unsigned int reg)
{
	return (max77802_pmic_is_accessible_reg(dev, reg) ||
		max77802_rtc_is_accessible_reg(dev, reg));
}

static bool max77802_pmic_is_precious_reg(struct device *dev, unsigned int reg)
{
	return (reg == MAX77802_REG_INTSRC || reg == MAX77802_REG_INT1 ||
		reg == MAX77802_REG_INT2);
}

static bool max77802_rtc_is_precious_reg(struct device *dev, unsigned int reg)
{
	return (reg == MAX77802_RTC_INT ||
		reg == MAX77802_RTC_UPDATE0 ||
		reg == MAX77802_RTC_UPDATE1);
}

static bool max77802_is_precious_reg(struct device *dev, unsigned int reg)
{
	return (max77802_pmic_is_precious_reg(dev, reg) ||
		max77802_rtc_is_precious_reg(dev, reg));
}

static bool max77802_pmic_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (max77802_is_precious_reg(dev, reg) ||
		reg == MAX77802_REG_STATUS1 || reg == MAX77802_REG_STATUS2 ||
		reg == MAX77802_REG_PWRON);
}

static bool max77802_rtc_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (max77802_rtc_is_precious_reg(dev, reg) ||
		reg == MAX77802_RTC_SEC ||
		reg == MAX77802_RTC_MIN ||
		reg == MAX77802_RTC_HOUR ||
		reg == MAX77802_RTC_WEEKDAY ||
		reg == MAX77802_RTC_MONTH ||
		reg == MAX77802_RTC_YEAR ||
		reg == MAX77802_RTC_DATE);
}

static bool max77802_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (max77802_pmic_is_volatile_reg(dev, reg) ||
		max77802_rtc_is_volatile_reg(dev, reg));
}

static const struct regmap_config max77686_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct regmap_config max77802_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = max77802_is_accessible_reg,
	.readable_reg = max77802_is_accessible_reg,
	.precious_reg = max77802_is_precious_reg,
	.volatile_reg = max77802_is_volatile_reg,
	.name = "max77802-pmic",
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_irq max77686_irqs[] = {
	/* INT1 interrupts */
	{ .reg_offset = 0, .mask = MAX77686_INT1_PWRONF_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_PWRONR_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_JIGONBF_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_JIGONBR_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_ACOKBF_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_ACOKBR_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_ONKEY1S_MSK, },
	{ .reg_offset = 0, .mask = MAX77686_INT1_MRSTB_MSK, },
	/* INT2 interrupts */
	{ .reg_offset = 1, .mask = MAX77686_INT2_140C_MSK, },
	{ .reg_offset = 1, .mask = MAX77686_INT2_120C_MSK, },
};

static const struct regmap_irq_chip max77686_irq_chip = {
	.name			= "max77686-pmic",
	.status_base		= MAX77686_REG_INT1,
	.mask_base		= MAX77686_REG_INT1MSK,
	.num_regs		= 2,
	.irqs			= max77686_irqs,
	.num_irqs		= ARRAY_SIZE(max77686_irqs),
};

static const struct regmap_irq_chip max77802_irq_chip = {
	.name			= "max77802-pmic",
	.status_base		= MAX77802_REG_INT1,
	.mask_base		= MAX77802_REG_INT1MSK,
	.num_regs		= 2,
	.irqs			= max77686_irqs, /* same masks as 77686 */
	.num_irqs		= ARRAY_SIZE(max77686_irqs),
};

static const struct of_device_id max77686_pmic_dt_match[] = {
	{
		.compatible = "maxim,max77686",
		.data = (void *)TYPE_MAX77686,
	},
	{
		.compatible = "maxim,max77802",
		.data = (void *)TYPE_MAX77802,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, max77686_pmic_dt_match);

static int max77686_i2c_probe(struct i2c_client *i2c)
{
	struct max77686_dev *max77686 = NULL;
	unsigned int data;
	int ret = 0;
	const struct regmap_config *config;
	const struct regmap_irq_chip *irq_chip;
	const struct mfd_cell *cells;
	int n_devs;

	max77686 = devm_kzalloc(&i2c->dev,
				sizeof(struct max77686_dev), GFP_KERNEL);
	if (!max77686)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max77686);
	max77686->type = (unsigned long)of_device_get_match_data(&i2c->dev);
	max77686->dev = &i2c->dev;
	max77686->i2c = i2c;

	max77686->irq = i2c->irq;

	if (max77686->type == TYPE_MAX77686) {
		config = &max77686_regmap_config;
		irq_chip = &max77686_irq_chip;
		cells =  max77686_devs;
		n_devs = ARRAY_SIZE(max77686_devs);
	} else {
		config = &max77802_regmap_config;
		irq_chip = &max77802_irq_chip;
		cells =  max77802_devs;
		n_devs = ARRAY_SIZE(max77802_devs);
	}

	max77686->regmap = devm_regmap_init_i2c(i2c, config);
	if (IS_ERR(max77686->regmap)) {
		ret = PTR_ERR(max77686->regmap);
		dev_err(max77686->dev, "Failed to allocate register map: %d\n",
				ret);
		return ret;
	}

	ret = regmap_read(max77686->regmap, MAX77686_REG_DEVICE_ID, &data);
	if (ret < 0) {
		dev_err(max77686->dev,
			"device not found on this channel (this is not an error)\n");
		return -ENODEV;
	}

	ret = devm_regmap_add_irq_chip(&i2c->dev, max77686->regmap,
				       max77686->irq,
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT |
				       IRQF_SHARED, 0, irq_chip,
				       &max77686->irq_data);
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to add PMIC irq chip: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(max77686->dev, -1, cells, n_devs, NULL,
				   0, NULL);
	if (ret < 0) {
		dev_err(&i2c->dev, "failed to add MFD devices: %d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77686_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		enable_irq_wake(max77686->irq);

	/*
	 * IRQ must be disabled during suspend because if it happens
	 * while suspended it will be handled before resuming I2C.
	 *
	 * When device is woken up from suspend (e.g. by RTC wake alarm),
	 * an interrupt occurs before resuming I2C bus controller.
	 * Interrupt handler tries to read registers but this read
	 * will fail because I2C is still suspended.
	 */
	disable_irq(max77686->irq);

	return 0;
}

static int max77686_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct max77686_dev *max77686 = i2c_get_clientdata(i2c);

	if (device_may_wakeup(dev))
		disable_irq_wake(max77686->irq);

	enable_irq(max77686->irq);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(max77686_pm, max77686_suspend, max77686_resume);

static struct i2c_driver max77686_i2c_driver = {
	.driver = {
		   .name = "max77686",
		   .pm = &max77686_pm,
		   .of_match_table = of_match_ptr(max77686_pmic_dt_match),
	},
	.probe_new = max77686_i2c_probe,
};

module_i2c_driver(max77686_i2c_driver);

MODULE_DESCRIPTION("MAXIM 77686/802 multi-function core driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
