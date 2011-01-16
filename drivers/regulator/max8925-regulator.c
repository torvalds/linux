/*
 * Regulators driver for Maxim max8925
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *      Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max8925.h>

#define SD1_DVM_VMIN		850000
#define SD1_DVM_VMAX		1000000
#define SD1_DVM_STEP		50000
#define SD1_DVM_SHIFT		5		/* SDCTL1 bit5 */
#define SD1_DVM_EN		6		/* SDV1 bit 6 */

struct max8925_regulator_info {
	struct regulator_desc	desc;
	struct regulator_dev	*regulator;
	struct i2c_client	*i2c;
	struct max8925_chip	*chip;

	int	min_uV;
	int	max_uV;
	int	step_uV;
	int	vol_reg;
	int	vol_shift;
	int	vol_nbits;
	int	enable_bit;
	int	enable_reg;
};

static inline int check_range(struct max8925_regulator_info *info,
			      int min_uV, int max_uV)
{
	if (min_uV < info->min_uV || min_uV > info->max_uV)
		return -EINVAL;

	return 0;
}

static int max8925_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);
	return info->min_uV + index * info->step_uV;
}

static int max8925_set_voltage(struct regulator_dev *rdev,
			       int min_uV, int max_uV, unsigned int *selector)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data, mask;

	if (check_range(info, min_uV, max_uV)) {
		dev_err(info->chip->dev, "invalid voltage range (%d, %d) uV\n",
			min_uV, max_uV);
		return -EINVAL;
	}
	data = (min_uV - info->min_uV + info->step_uV - 1) / info->step_uV;
	*selector = data;
	data <<= info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1) << info->vol_shift;

	return max8925_set_bits(info->i2c, info->vol_reg, mask, data);
}

static int max8925_get_voltage(struct regulator_dev *rdev)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data, mask;
	int ret;

	ret = max8925_reg_read(info->i2c, info->vol_reg);
	if (ret < 0)
		return ret;
	mask = ((1 << info->vol_nbits) - 1) << info->vol_shift;
	data = (ret & mask) >> info->vol_shift;

	return max8925_list_voltage(rdev, data);
}

static int max8925_enable(struct regulator_dev *rdev)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);

	return max8925_set_bits(info->i2c, info->enable_reg,
				1 << info->enable_bit,
				1 << info->enable_bit);
}

static int max8925_disable(struct regulator_dev *rdev)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);

	return max8925_set_bits(info->i2c, info->enable_reg,
				1 << info->enable_bit, 0);
}

static int max8925_is_enabled(struct regulator_dev *rdev)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = max8925_reg_read(info->i2c, info->enable_reg);
	if (ret < 0)
		return ret;

	return ret & (1 << info->enable_bit);
}

static int max8925_set_dvm_voltage(struct regulator_dev *rdev, int uV)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data, mask;

	if (uV < SD1_DVM_VMIN || uV > SD1_DVM_VMAX)
		return -EINVAL;

	data = (uV - SD1_DVM_VMIN + SD1_DVM_STEP - 1) / SD1_DVM_STEP;
	data <<= SD1_DVM_SHIFT;
	mask = 3 << SD1_DVM_SHIFT;

	return max8925_set_bits(info->i2c, info->enable_reg, mask, data);
}

static int max8925_set_dvm_enable(struct regulator_dev *rdev)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);

	return max8925_set_bits(info->i2c, info->vol_reg, 1 << SD1_DVM_EN,
				1 << SD1_DVM_EN);
}

static int max8925_set_dvm_disable(struct regulator_dev *rdev)
{
	struct max8925_regulator_info *info = rdev_get_drvdata(rdev);

	return max8925_set_bits(info->i2c, info->vol_reg, 1 << SD1_DVM_EN, 0);
}

static struct regulator_ops max8925_regulator_sdv_ops = {
	.set_voltage		= max8925_set_voltage,
	.get_voltage		= max8925_get_voltage,
	.enable			= max8925_enable,
	.disable		= max8925_disable,
	.is_enabled		= max8925_is_enabled,
	.set_suspend_voltage	= max8925_set_dvm_voltage,
	.set_suspend_enable	= max8925_set_dvm_enable,
	.set_suspend_disable	= max8925_set_dvm_disable,
};

static struct regulator_ops max8925_regulator_ldo_ops = {
	.set_voltage		= max8925_set_voltage,
	.get_voltage		= max8925_get_voltage,
	.enable			= max8925_enable,
	.disable		= max8925_disable,
	.is_enabled		= max8925_is_enabled,
};

#define MAX8925_SDV(_id, min, max, step)			\
{								\
	.desc	= {						\
		.name	= "SDV" #_id,				\
		.ops	= &max8925_regulator_sdv_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= MAX8925_ID_SD##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.step_uV	= step * 1000,				\
	.vol_reg	= MAX8925_SDV##_id,			\
	.vol_shift	= 0,					\
	.vol_nbits	= 6,					\
	.enable_reg	= MAX8925_SDCTL##_id,			\
	.enable_bit	= 0,					\
}

#define MAX8925_LDO(_id, min, max, step)			\
{								\
	.desc	= {						\
		.name	= "LDO" #_id,				\
		.ops	= &max8925_regulator_ldo_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= MAX8925_ID_LDO##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.step_uV	= step * 1000,				\
	.vol_reg	= MAX8925_LDOVOUT##_id,			\
	.vol_shift	= 0,					\
	.vol_nbits	= 6,					\
	.enable_reg	= MAX8925_LDOCTL##_id,			\
	.enable_bit	= 0,					\
}

static struct max8925_regulator_info max8925_regulator_info[] = {
	MAX8925_SDV(1, 637.5, 1425, 12.5),
	MAX8925_SDV(2,   650, 2225,   25),
	MAX8925_SDV(3,   750, 3900,   50),

	MAX8925_LDO(1,  750, 3900, 50),
	MAX8925_LDO(2,  650, 2250, 25),
	MAX8925_LDO(3,  650, 2250, 25),
	MAX8925_LDO(4,  750, 3900, 50),
	MAX8925_LDO(5,  750, 3900, 50),
	MAX8925_LDO(6,  750, 3900, 50),
	MAX8925_LDO(7,  750, 3900, 50),
	MAX8925_LDO(8,  750, 3900, 50),
	MAX8925_LDO(9,  750, 3900, 50),
	MAX8925_LDO(10, 750, 3900, 50),
	MAX8925_LDO(11, 750, 3900, 50),
	MAX8925_LDO(12, 750, 3900, 50),
	MAX8925_LDO(13, 750, 3900, 50),
	MAX8925_LDO(14, 750, 3900, 50),
	MAX8925_LDO(15, 750, 3900, 50),
	MAX8925_LDO(16, 750, 3900, 50),
	MAX8925_LDO(17, 650, 2250, 25),
	MAX8925_LDO(18, 650, 2250, 25),
	MAX8925_LDO(19, 750, 3900, 50),
	MAX8925_LDO(20, 750, 3900, 50),
};

static struct max8925_regulator_info * __devinit find_regulator_info(int id)
{
	struct max8925_regulator_info *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(max8925_regulator_info); i++) {
		ri = &max8925_regulator_info[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

static int __devinit max8925_regulator_probe(struct platform_device *pdev)
{
	struct max8925_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct max8925_platform_data *pdata = chip->dev->platform_data;
	struct max8925_regulator_info *ri;
	struct regulator_dev *rdev;

	ri = find_regulator_info(pdev->id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}
	ri->i2c = chip->i2c;
	ri->chip = chip;

	rdev = regulator_register(&ri->desc, &pdev->dev,
				  pdata->regulator[pdev->id], ri);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);
	return 0;
}

static int __devexit max8925_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	regulator_unregister(rdev);

	return 0;
}

static struct platform_driver max8925_regulator_driver = {
	.driver		= {
		.name	= "max8925-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= max8925_regulator_probe,
	.remove		= __devexit_p(max8925_regulator_remove),
};

static int __init max8925_regulator_init(void)
{
	return platform_driver_register(&max8925_regulator_driver);
}
subsys_initcall(max8925_regulator_init);

static void __exit max8925_regulator_exit(void)
{
	platform_driver_unregister(&max8925_regulator_driver);
}
module_exit(max8925_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Maxim 8925 PMIC");
MODULE_ALIAS("platform:max8925-regulator");

