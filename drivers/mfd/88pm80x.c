/*
 * I2C driver for Marvell 88PM80x
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Haojian Zhuang <haojian.zhuang@marvell.com>
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mfd/88pm80x.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/err.h>

/* 88pm80x chips have same definition for chip id register. */
#define PM80X_CHIP_ID			(0x00)
#define PM80X_CHIP_ID_NUM(x)		(((x) >> 5) & 0x7)
#define PM80X_CHIP_ID_REVISION(x)	((x) & 0x1F)

struct pm80x_chip_mapping {
	unsigned int	id;
	int		type;
};

static struct pm80x_chip_mapping chip_mapping[] = {
	/* 88PM800 chip id number */
	{0x3,	CHIP_PM800},
	/* 88PM805 chip id number */
	{0x0,	CHIP_PM805},
	/* 88PM860 chip id number */
	{0x4,	CHIP_PM860},
};

/*
 * workaround: some registers needed by pm805 are defined in pm800, so
 * need to use this global variable to maintain the relation between
 * pm800 and pm805. would remove it after HW chip fixes the issue.
 */
static struct pm80x_chip *g_pm80x_chip;

const struct regmap_config pm80x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};
EXPORT_SYMBOL_GPL(pm80x_regmap_config);


int pm80x_init(struct i2c_client *client)
{
	struct pm80x_chip *chip;
	struct regmap *map;
	unsigned int val;
	int i, ret = 0;

	chip =
	    devm_kzalloc(&client->dev, sizeof(struct pm80x_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	map = devm_regmap_init_i2c(client, &pm80x_regmap_config);
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	chip->client = client;
	chip->regmap = map;

	chip->irq = client->irq;

	chip->dev = &client->dev;
	dev_set_drvdata(chip->dev, chip);
	i2c_set_clientdata(chip->client, chip);

	ret = regmap_read(chip->regmap, PM80X_CHIP_ID, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CHIP ID: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(chip_mapping); i++) {
		if (chip_mapping[i].id == PM80X_CHIP_ID_NUM(val)) {
			chip->type = chip_mapping[i].type;
			break;
		}
	}

	if (i == ARRAY_SIZE(chip_mapping)) {
		dev_err(chip->dev,
			"Failed to detect Marvell 88PM800:ChipID[0x%x]\n", val);
		return -EINVAL;
	}

	device_init_wakeup(&client->dev, 1);

	/*
	 * workaround: set g_pm80x_chip to the first probed chip. if the
	 * second chip is probed, just point to the companion to each
	 * other so that pm805 can access those specific register. would
	 * remove it after HW chip fixes the issue.
	 */
	if (!g_pm80x_chip)
		g_pm80x_chip = chip;
	else {
		chip->companion = g_pm80x_chip->client;
		g_pm80x_chip->companion = chip->client;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pm80x_init);

int pm80x_deinit(void)
{
	/*
	 * workaround: clear the dependency between pm800 and pm805.
	 * would remove it after HW chip fixes the issue.
	 */
	if (g_pm80x_chip->companion)
		g_pm80x_chip->companion = NULL;
	else
		g_pm80x_chip = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(pm80x_deinit);

#ifdef CONFIG_PM_SLEEP
static int pm80x_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct pm80x_chip *chip = i2c_get_clientdata(client);

	if (chip && chip->wu_flag)
		if (device_may_wakeup(chip->dev))
			enable_irq_wake(chip->irq);

	return 0;
}

static int pm80x_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct pm80x_chip *chip = i2c_get_clientdata(client);

	if (chip && chip->wu_flag)
		if (device_may_wakeup(chip->dev))
			disable_irq_wake(chip->irq);

	return 0;
}
#endif

SIMPLE_DEV_PM_OPS(pm80x_pm_ops, pm80x_suspend, pm80x_resume);
EXPORT_SYMBOL_GPL(pm80x_pm_ops);

MODULE_DESCRIPTION("I2C Driver for Marvell 88PM80x");
MODULE_AUTHOR("Qiao Zhou <zhouqiao@marvell.com>");
MODULE_LICENSE("GPL");
