/*
 * Regulators driver for Marvell 88PM8607
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/88pm8607.h>

struct pm8607_regulator_info {
	struct regulator_desc	desc;
	struct pm8607_chip	*chip;
	struct regulator_dev	*regulator;

	int	min_uV;
	int	max_uV;
	int	step_uV;
	int	vol_reg;
	int	vol_shift;
	int	vol_nbits;
	int	update_reg;
	int	update_bit;
	int	enable_reg;
	int	enable_bit;
	int	slope_double;
};

static inline int check_range(struct pm8607_regulator_info *info,
				int min_uV, int max_uV)
{
	if (max_uV < info->min_uV || min_uV > info->max_uV || min_uV > max_uV)
		return -EINVAL;

	return 0;
}

static int pm8607_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	uint8_t chip_id = info->chip->chip_id;
	int ret = -EINVAL;

	switch (info->desc.id) {
	case PM8607_ID_BUCK1:
		ret = (index < 0x1d) ? (index * 25000 + 800000) :
			((index < 0x20) ? 1500000 :
			((index < 0x40) ? ((index - 0x20) * 25000) :
			-EINVAL));
		break;
	case PM8607_ID_BUCK3:
		ret = (index < 0x3d) ? (index * 25000) :
			((index < 0x40) ? 1500000 : -EINVAL);
		if (ret < 0)
			break;
		if (info->slope_double)
			ret <<= 1;
		break;
	case PM8607_ID_LDO1:
		ret = (index == 0) ? 1800000 :
			((index == 1) ? 1200000 :
			((index == 2) ? 2800000 : -EINVAL));
		break;
	case PM8607_ID_LDO5:
		ret = (index == 0) ? 2900000 :
			((index == 1) ? 3000000 :
			((index == 2) ? 3100000 : 3300000));
		break;
	case PM8607_ID_LDO7:
	case PM8607_ID_LDO8:
		ret = (index < 3) ? (index * 50000 + 1800000) :
			((index < 8) ? (index * 50000 + 2550000) :
			 -EINVAL);
		break;
	case PM8607_ID_LDO12:
		ret = (index < 2) ? (index * 100000 + 1800000) :
			((index < 7) ? (index * 100000 + 2500000) :
			((index == 7) ? 3300000 : 1200000));
		break;
	case PM8607_ID_LDO2:
	case PM8607_ID_LDO3:
	case PM8607_ID_LDO9:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 8) ? (index * 50000 + 2550000) :
				 -EINVAL);
			break;
		case PM8607_CHIP_B0:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 7) ? (index * 50000 + 2550000) :
				3300000);
			break;
		}
		break;
	case PM8607_ID_LDO4:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 8) ? (index * 50000 + 2550000) :
				 -EINVAL);
			break;
		case PM8607_CHIP_B0:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 6) ? (index * 50000 + 2550000) :
				((index == 6) ? 2900000 : 3300000));
			break;
		}
		break;
	case PM8607_ID_LDO6:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 8) ? (index * 50000 + 2450000) :
				-EINVAL);
			break;
		case PM8607_CHIP_B0:
			ret = (index < 2) ? (index * 50000 + 1800000) :
				((index < 7) ? (index * 50000 + 2500000) :
				3300000);
			break;
		}
		break;
	case PM8607_ID_LDO10:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 8) ? (index * 50000 + 2550000) :
				1200000);
			break;
		case PM8607_CHIP_B0:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 7) ? (index * 50000 + 2550000) :
				((index == 7) ? 3300000 : 1200000));
			break;
		}
		break;
	case PM8607_ID_LDO14:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			ret = (index < 3) ? (index * 50000 + 1800000) :
				((index < 8) ? (index * 50000 + 2550000) :
				 -EINVAL);
			break;
		case PM8607_CHIP_B0:
			ret = (index < 2) ? (index * 50000 + 1800000) :
				((index < 7) ? (index * 50000 + 2600000) :
				3300000);
			break;
		}
		break;
	}
	return ret;
}

static int choose_voltage(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	uint8_t chip_id = info->chip->chip_id;
	int val = -ENOENT;
	int ret;

	switch (info->desc.id) {
	case PM8607_ID_BUCK1:
		if (min_uV >= 800000) 		/* 800mV ~ 1500mV / 25mV */
			val = (min_uV - 775001) / 25000;
		else {				/* 25mV ~ 775mV / 25mV */
			val = (min_uV + 249999) / 25000;
			val += 32;
		}
		break;
	case PM8607_ID_BUCK3:
		if (info->slope_double)
			min_uV = min_uV >> 1;
		val = (min_uV + 249999) / 25000; /* 0mV ~ 1500mV / 25mV */

		break;
	case PM8607_ID_LDO1:
		if (min_uV > 1800000)
			val = 2;
		else if (min_uV > 1200000)
			val = 0;
		else
			val = 1;
		break;
	case PM8607_ID_LDO5:
		if (min_uV > 3100000)
			val = 3;
		else				/* 2900mV ~ 3100mV / 100mV */
			val = (min_uV - 2800001) / 100000;
		break;
	case PM8607_ID_LDO7:
	case PM8607_ID_LDO8:
		if (min_uV < 2700000) {	/* 1800mV ~ 1900mV / 50mV */
			if (min_uV <= 1800000)
				val = 0;	/* 1800mv */
			else if (min_uV <= 1900000)
				val = (min_uV - 1750001) / 50000;
			else
				val = 3;	/* 2700mV */
		} else {		 /* 2700mV ~ 2900mV / 50mV */
			if (min_uV <= 2900000) {
				val = (min_uV - 2650001) / 50000;
				val += 3;
			} else
				val = -EINVAL;
		}
		break;
	case PM8607_ID_LDO10:
		if (min_uV > 2850000)
			val = 7;
		else if (min_uV <= 1200000)
			val = 8;
		else if (min_uV < 2700000)	/* 1800mV ~ 1900mV / 50mV */
			val = (min_uV - 1750001) / 50000;
		else {				/* 2700mV ~ 2850mV / 50mV */
			val = (min_uV - 2650001) / 50000;
			val += 3;
		}
		break;
	case PM8607_ID_LDO12:
		if (min_uV < 2700000) {		/* 1800mV ~ 1900mV / 100mV */
			if (min_uV <= 1200000)
				val = 8;	/* 1200mV */
			else if (min_uV <= 1800000)
				val = 0;	/* 1800mV */
			else if (min_uV <= 1900000)
				val = (min_uV - 1700001) / 100000;
			else
				val = 2;	/* 2700mV */
		} else {			/* 2700mV ~ 3100mV / 100mV */
			if (min_uV <= 3100000) {
				val = (min_uV - 2600001) / 100000;
				val += 2;
			} else if (min_uV <= 3300000)
				val = 7;
			else
				val = -EINVAL;
		}
		break;
	case PM8607_ID_LDO2:
	case PM8607_ID_LDO3:
	case PM8607_ID_LDO9:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			if (min_uV < 2700000)	/* 1800mV ~ 1900mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1900000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 3;	/* 2700mV */
			else {			/* 2700mV ~ 2900mV / 50mV */
				if (min_uV <= 2900000) {
					val = (min_uV - 2650001) / 50000;
					val += 3;
				} else
					val = -EINVAL;
			}
			break;
		case PM8607_CHIP_B0:
			if (min_uV < 2700000) {	/* 1800mV ~ 1900mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1900000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 3;	/* 2700mV */
			} else {		 /* 2700mV ~ 2850mV / 50mV */
				if (min_uV <= 2850000) {
					val = (min_uV - 2650001) / 50000;
					val += 3;
				} else if (min_uV <= 3300000)
					val = 7;
				else
					val = -EINVAL;
			}
			break;
		}
		break;
	case PM8607_ID_LDO4:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			if (min_uV < 2700000)	/* 1800mV ~ 1900mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1900000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 3;	/* 2700mV */
			else {			/* 2700mV ~ 2900mV / 50mV */
				if (min_uV <= 2900000) {
					val = (min_uV - 2650001) / 50000;
					val += 3;
				} else
					val = -EINVAL;
			}
			break;
		case PM8607_CHIP_B0:
			if (min_uV < 2700000) {	/* 1800mV ~ 1900mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1900000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 3;	/* 2700mV */
			} else {		 /* 2700mV ~ 2800mV / 50mV */
				if (min_uV <= 2850000) {
					val = (min_uV - 2650001) / 50000;
					val += 3;
				} else if (min_uV <= 2900000)
					val = 6;
				else if (min_uV <= 3300000)
					val = 7;
				else
					val = -EINVAL;
			}
			break;
		}
		break;
	case PM8607_ID_LDO6:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			if (min_uV < 2600000) {	/* 1800mV ~ 1900mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1900000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 3;	/* 2600mV */
			} else {		/* 2600mV ~ 2800mV / 50mV */
				if (min_uV <= 2800000) {
					val = (min_uV - 2550001) / 50000;
					val += 3;
				} else
					val = -EINVAL;
			}
			break;
		case PM8607_CHIP_B0:
			if (min_uV < 2600000) {	/* 1800mV ~ 1850mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1850000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 2;	/* 2600mV */
			} else {		/* 2600mV ~ 2800mV / 50mV */
				if (min_uV <= 2800000) {
					val = (min_uV - 2550001) / 50000;
					val += 2;
				} else if (min_uV <= 3300000)
					val = 7;
				else
					val = -EINVAL;
			}
			break;
		}
		break;
	case PM8607_ID_LDO14:
		switch (chip_id) {
		case PM8607_CHIP_A0:
		case PM8607_CHIP_A1:
			if (min_uV < 2700000) {	/* 1800mV ~ 1900mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1900000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 3;	/* 2700mV */
			} else {		 /* 2700mV ~ 2900mV / 50mV */
				if (min_uV <= 2900000) {
					val = (min_uV - 2650001) / 50000;
					val += 3;
				} else
					val = -EINVAL;
			}
			break;
		case PM8607_CHIP_B0:
			if (min_uV < 2700000) {	/* 1800mV ~ 1850mV / 50mV */
				if (min_uV <= 1800000)
					val = 0;
				else if (min_uV <= 1850000)
					val = (min_uV - 1750001) / 50000;
				else
					val = 2;	/* 2700mV */
			} else {		 /* 2700mV ~ 2900mV / 50mV */
				if (min_uV <= 2900000) {
					val = (min_uV - 2650001) / 50000;
					val += 2;
				} else if (min_uV <= 3300000)
					val = 7;
				else
					val = -EINVAL;
			}
			break;
		}
		break;
	}
	if (val >= 0) {
		ret = pm8607_list_voltage(rdev, val);
		if (ret > max_uV) {
			pr_err("exceed voltage range (%d %d) uV",
				min_uV, max_uV);
			return -EINVAL;
		}
	} else
		pr_err("invalid voltage range (%d %d) uV", min_uV, max_uV);
	return val;
}

static int pm8607_set_voltage(struct regulator_dev *rdev,
			      int min_uV, int max_uV)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	struct pm8607_chip *chip = info->chip;
	uint8_t val, mask;
	int ret;

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	ret = choose_voltage(rdev, min_uV, max_uV);
	if (ret < 0)
		return -EINVAL;
	val = (uint8_t)(ret << info->vol_shift);
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	ret = pm8607_set_bits(chip, info->vol_reg, mask, val);
	if (ret)
		return ret;
	switch (info->desc.id) {
	case PM8607_ID_BUCK1:
	case PM8607_ID_BUCK3:
		ret = pm8607_set_bits(chip, info->update_reg,
				      1 << info->update_bit,
				      1 << info->update_bit);
		break;
	}
	return ret;
}

static int pm8607_get_voltage(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	struct pm8607_chip *chip = info->chip;
	uint8_t val, mask;
	int ret;

	ret = pm8607_reg_read(chip, info->vol_reg);
	if (ret < 0)
		return ret;

	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = ((unsigned char)ret & mask) >> info->vol_shift;

	return pm8607_list_voltage(rdev, val);
}

static int pm8607_enable(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	struct pm8607_chip *chip = info->chip;

	return pm8607_set_bits(chip, info->enable_reg,
			       1 << info->enable_bit,
			       1 << info->enable_bit);
}

static int pm8607_disable(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	struct pm8607_chip *chip = info->chip;

	return pm8607_set_bits(chip, info->enable_reg,
			       1 << info->enable_bit, 0);
}

static int pm8607_is_enabled(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	struct pm8607_chip *chip = info->chip;
	int ret;

	ret = pm8607_reg_read(chip, info->enable_reg);
	if (ret < 0)
		return ret;

	return !!((unsigned char)ret & (1 << info->enable_bit));
}

static struct regulator_ops pm8607_regulator_ops = {
	.set_voltage	= pm8607_set_voltage,
	.get_voltage	= pm8607_get_voltage,
	.enable		= pm8607_enable,
	.disable	= pm8607_disable,
	.is_enabled	= pm8607_is_enabled,
};

#define PM8607_DVC(_id, min, max, step, vreg, nbits, ureg, ubit, ereg, ebit) \
{									\
	.desc	= {							\
		.name	= "BUCK" #_id,					\
		.ops	= &pm8607_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM8607_ID_BUCK##_id,				\
		.owner	= THIS_MODULE,					\
	},								\
	.min_uV		= (min) * 1000,					\
	.max_uV		= (max) * 1000,					\
	.step_uV	= (step) * 1000,				\
	.vol_reg	= PM8607_##vreg,				\
	.vol_shift	= (0),						\
	.vol_nbits	= (nbits),					\
	.update_reg	= PM8607_##ureg,				\
	.update_bit	= (ubit),					\
	.enable_reg	= PM8607_##ereg,				\
	.enable_bit	= (ebit),					\
	.slope_double	= (0),						\
}

#define PM8607_LDO(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
{									\
	.desc	= {							\
		.name	= "LDO" #_id,					\
		.ops	= &pm8607_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM8607_ID_LDO##_id,				\
		.owner	= THIS_MODULE,					\
	},								\
	.min_uV		= (min) * 1000,					\
	.max_uV		= (max) * 1000,					\
	.step_uV	= (step) * 1000,				\
	.vol_reg	= PM8607_##vreg,				\
	.vol_shift	= (shift),					\
	.vol_nbits	= (nbits),					\
	.enable_reg	= PM8607_##ereg,				\
	.enable_bit	= (ebit),					\
	.slope_double	= (0),						\
}

static struct pm8607_regulator_info pm8607_regulator_info[] = {
	PM8607_DVC(1, 0, 1500, 25, BUCK1, 6, GO, 0, SUPPLIES_EN11, 0),
	PM8607_DVC(3, 0, 1500, 25, BUCK3, 6, GO, 2, SUPPLIES_EN11, 2),

	PM8607_LDO(1 , 1200, 2800, 0, LDO1 , 0, 2, SUPPLIES_EN11, 3),
	PM8607_LDO(2 , 1800, 3300, 0, LDO2 , 0, 3, SUPPLIES_EN11, 4),
	PM8607_LDO(3 , 1800, 3300, 0, LDO3 , 0, 3, SUPPLIES_EN11, 5),
	PM8607_LDO(4 , 1800, 3300, 0, LDO4 , 0, 3, SUPPLIES_EN11, 6),
	PM8607_LDO(5 , 2900, 3300, 0, LDO5 , 0, 2, SUPPLIES_EN11, 7),
	PM8607_LDO(6 , 1800, 3300, 0, LDO6 , 0, 3, SUPPLIES_EN12, 0),
	PM8607_LDO(7 , 1800, 2900, 0, LDO7 , 0, 3, SUPPLIES_EN12, 1),
	PM8607_LDO(8 , 1800, 2900, 0, LDO8 , 0, 3, SUPPLIES_EN12, 2),
	PM8607_LDO(9 , 1800, 3300, 0, LDO9 , 0, 3, SUPPLIES_EN12, 3),
	PM8607_LDO(10, 1200, 3300, 0, LDO10, 0, 4, SUPPLIES_EN11, 4),
	PM8607_LDO(12, 1200, 3300, 0, LDO12, 0, 4, SUPPLIES_EN11, 5),
	PM8607_LDO(14, 1800, 3300, 0, LDO14, 0, 3, SUPPLIES_EN11, 6),
};

static inline struct pm8607_regulator_info *find_regulator_info(int id)
{
	struct pm8607_regulator_info *info;
	int i;

	for (i = 0; i < ARRAY_SIZE(pm8607_regulator_info); i++) {
		info = &pm8607_regulator_info[i];
		if (info->desc.id == id)
			return info;
	}
	return NULL;
}

static int __devinit pm8607_regulator_probe(struct platform_device *pdev)
{
	struct pm8607_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm8607_platform_data *pdata = chip->dev->platform_data;
	struct pm8607_regulator_info *info = NULL;

	info = find_regulator_info(pdev->id);
	if (info == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	info->chip = chip;

	info->regulator = regulator_register(&info->desc, &pdev->dev,
					     pdata->regulator[pdev->id], info);
	if (IS_ERR(info->regulator)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			info->desc.name);
		return PTR_ERR(info->regulator);
	}

	/* check DVC ramp slope double */
	if (info->desc.id == PM8607_ID_BUCK3)
		if (info->chip->buck3_double)
			info->slope_double = 1;

	platform_set_drvdata(pdev, info);
	return 0;
}

static int __devexit pm8607_regulator_remove(struct platform_device *pdev)
{
	struct pm8607_regulator_info *info = platform_get_drvdata(pdev);

	regulator_unregister(info->regulator);
	return 0;
}

#define PM8607_REGULATOR_DRIVER(_name)				\
{								\
	.driver		= {					\
		.name	= "88pm8607-" #_name,			\
		.owner	= THIS_MODULE,				\
	},							\
	.probe		= pm8607_regulator_probe,		\
	.remove		= __devexit_p(pm8607_regulator_remove),	\
}

static struct platform_driver pm8607_regulator_driver[] = {
	PM8607_REGULATOR_DRIVER(buck1),
	PM8607_REGULATOR_DRIVER(buck2),
	PM8607_REGULATOR_DRIVER(buck3),
	PM8607_REGULATOR_DRIVER(ldo1),
	PM8607_REGULATOR_DRIVER(ldo2),
	PM8607_REGULATOR_DRIVER(ldo3),
	PM8607_REGULATOR_DRIVER(ldo4),
	PM8607_REGULATOR_DRIVER(ldo5),
	PM8607_REGULATOR_DRIVER(ldo6),
	PM8607_REGULATOR_DRIVER(ldo7),
	PM8607_REGULATOR_DRIVER(ldo8),
	PM8607_REGULATOR_DRIVER(ldo9),
	PM8607_REGULATOR_DRIVER(ldo10),
	PM8607_REGULATOR_DRIVER(ldo12),
	PM8607_REGULATOR_DRIVER(ldo14),
};

static int __init pm8607_regulator_init(void)
{
	int i, count, ret;

	count = ARRAY_SIZE(pm8607_regulator_driver);
	for (i = 0; i < count; i++) {
		ret = platform_driver_register(&pm8607_regulator_driver[i]);
		if (ret != 0)
			pr_err("Failed to register regulator driver: %d\n",
				ret);
	}
	return 0;
}
subsys_initcall(pm8607_regulator_init);

static void __exit pm8607_regulator_exit(void)
{
	int i, count;

	count = ARRAY_SIZE(pm8607_regulator_driver);
	for (i = 0; i < count; i++)
		platform_driver_unregister(&pm8607_regulator_driver[i]);
}
module_exit(pm8607_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Marvell 88PM8607 PMIC");
MODULE_ALIAS("platform:88pm8607-regulator");
