/*
 *  drivers/regulator/rt5025-regulator.c
 *  Driver foo Richtek RT5025 PMIC Regulator
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/version.h>
#ifdef CONFIG_OF
#include <linux/regulator/of_regulator.h>
#endif /* #ifdef CONFIG_OF */

#include <linux/mfd/rt5025.h>
#include <linux/regulator/rt5025-regulator.h>

struct rt5025_regulator_info {
	struct regulator_desc	desc;
	struct regulator_dev	*regulator;
	struct i2c_client	*i2c;
	const unsigned int	*vol_output_list;
	const int		vol_output_size;
	int	min_uV;
	int	max_uV;
	int	vol_reg;
	int	vol_shift;
	int	vol_mask;
	int	enable_bit;
	int	enable_reg;
	int	mode_bit;
	int	mode_reg;
	int	ramp_bit;
	int	ramp_reg;
};

/*for DCDC1*/
static const unsigned int rt5025_vol_output_list1[] = {
	 700*1000,  725*1000,  750*1000,  775*1000,  800*1000,  825*1000,  850*1000,  875*1000,
	 900*1000,  925*1000,  950*1000,  975*1000, 1000*1000, 1025*1000, 1050*1000, 1075*1000,
	1100*1000, 1125*1000, 1150*1000, 1175*1000, 1200*1000, 1225*1000, 1250*1000, 1275*1000,
	1300*1000, 1325*1000, 1350*1000, 1375*1000, 1400*1000, 1425*1000, 1450*1000, 1475*1000,
	1500*1000, 1525*1000, 1550*1000, 1575*1000, 1600*1000, 1625*1000, 1650*1000, 1675*1000,
	1700*1000, 1725*1000, 1750*1000, 1775*1000, 1800*1000, 1825*1000, 1850*1000, 1875*1000,
	1900*1000, 1925*1000, 1950*1000, 1975*1000, 2000*2000, 2025*1000, 2050*1000, 2075*1000,
	2100*1000, 2125*1000, 2150*1000, 2175*1000, 2200*1000, 2225*1000, 2250*1000, 2275*1000,
};
#define rt5025_vol_output_size1 ARRAY_SIZE(rt5025_vol_output_list1)

/*DCDC2, LDO1, LDO2*/
static const unsigned int rt5025_vol_output_list2[] = {
	 700*1000,  725*1000,  750*1000,  775*1000,  800*1000,  825*1000,  850*1000,  875*1000,
	 900*1000,  925*1000,  950*1000,  975*1000, 1000*1000, 1025*1000, 1050*1000, 1075*1000,
	1100*1000, 1125*1000, 1150*1000, 1175*1000, 1200*1000, 1225*1000, 1250*1000, 1275*1000,
	1300*1000, 1325*1000, 1350*1000, 1375*1000, 1400*1000, 1425*1000, 1450*1000, 1475*1000,
	1500*1000, 1525*1000, 1550*1000, 1575*1000, 1600*1000, 1625*1000, 1650*1000, 1675*1000,
	1700*1000, 1725*1000, 1750*1000, 1775*1000, 1800*1000, 1825*1000, 1850*1000, 1875*1000,
	1900*1000, 1925*1000, 1950*1000, 1975*1000, 2000*2000, 2025*1000, 2050*1000, 2075*1000,
	2100*1000, 2125*1000, 2150*1000, 2175*1000, 2200*1000, 2225*1000, 2250*1000, 2275*1000,
	2300*1000, 2325*1000, 2350*1000, 2375*1000, 2400*1000, 2425*1000, 2450*1000, 2475*1000,
	2500*1000, 2525*1000, 2550*1000, 2575*1000, 2600*1000, 2625*1000, 2650*1000, 2675*1000,
	2700*1000, 2725*1000, 2750*1000, 2775*1000, 2800*1000, 2825*1000, 2850*1000, 2875*1000,
	2900*1000, 2925*1000, 2950*1000, 2975*1000, 3000*1000, 3025*1000, 3050*1000, 3075*1000,
	3100*1000, 3125*1000, 3150*1000, 3175*1000, 3200*1000, 3225*1000, 3250*1000, 3275*1000,
	3300*1000, 3325*1000, 3350*1000, 3375*1000, 3400*1000, 3425*1000, 3450*1000, 3475*1000,
	3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000,
	3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000,
};
#define rt5025_vol_output_size2 ARRAY_SIZE(rt5025_vol_output_list2)

/*DCDC3*/
static const unsigned int rt5025_vol_output_list3[] = {
	 700*1000,  750*1000,  800*1000,  850*1000,  900*1000,  950*1000, 1000*1000, 1050*1000,
	1100*1000, 1150*1000, 1200*1000, 1250*1000, 1300*1000, 1350*1000, 1400*1000, 1450*1000,
	1500*1000, 1550*1000, 1600*1000, 1650*1000, 1700*1000, 1750*1000, 1800*1000, 1850*1000,
	1900*1000, 1950*1000, 2000*1000, 2050*1000, 2100*1000, 2150*1000, 2200*1000, 2250*1000,
	2300*1000, 2350*1000, 2400*1000, 2450*1000, 2500*1000, 2550*1000, 2600*1000, 2650*1000,
	2700*1000, 2750*1000, 2800*1000, 2850*1000, 2900*1000, 2950*1000, 3000*1000, 3050*1000,
	3100*1000, 3150*1000, 3200*1000, 3250*1000, 3300*1000, 3350*1000, 3400*1000, 3450*1000,
	3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000, 3500*1000,
};
#define rt5025_vol_output_size3 ARRAY_SIZE(rt5025_vol_output_list3)

/*DCDC4*/
static const unsigned int rt5025_vol_output_list4[] = {
	4500*1000, 4600*1000, 4700*1000, 4800*1000, 4900*1000, 5000*1000, 5100*1000, 5200*1000,
	5300*1000, 5400*1000, 5500*1000, 5500*1000, 5500*1000, 5500*1000, 5500*1000, 5500*1000,
};
#define rt5025_vol_output_size4 ARRAY_SIZE(rt5025_vol_output_list4)

/*LDO3, LDO4, LDO5, LDO6*/
static const unsigned int rt5025_vol_output_list5[] = {
	1000*1000, 1100*1000, 1200*1000, 1300*1000, 1400*1000, 1500*1000, 1600*1000, 1700*1000,
	1800*1000, 1900*1000, 2000*1000, 2100*1000, 2200*1000, 2300*1000, 2400*1000, 2500*1000,
	2600*1000, 2700*1000, 2800*1000, 2900*1000, 3000*1000, 3100*1000, 3200*1000, 3300*1000,
	3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000,
	3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000, 3300*1000,
};
#define rt5025_vol_output_size5 ARRAY_SIZE(rt5025_vol_output_list5)

static inline int check_range(struct rt5025_regulator_info *info,
			      int min_uV, int max_uV)
{
	if (min_uV < info->min_uV || min_uV > info->max_uV)
		return -EINVAL;

	return 0;
}

static int rt5025_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);

	return (index >= info->vol_output_size) ? -EINVAL : info->vol_output_list[index];
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
static int rt5025_set_voltage_sel(struct regulator_dev *rdev, unsigned selector)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data;
	const int count = info->vol_output_size;

	if (selector > count)
		return -EINVAL;
	data = (unsigned char)selector;
	data <<= info->vol_shift;
	return rt5025_assign_bits(info->i2c, info->vol_reg,
		info->vol_mask, data);
}

static int rt5025_get_voltage_sel(struct regulator_dev *rdev)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = rt5025_reg_read(info->i2c, info->vol_reg);
	if (ret < 0)
		return ret;
	return (ret & info->vol_mask)  >> info->vol_shift;
}
#else
static int rt5025_find_voltage(struct regulator_dev *rdev,
			       int min_uV, int max_uV)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	int i = 0;
	const int count = info->vol_output_size;

	for (i = 0; i < count; i++) {
		if ((info->vol_output_list[i] >= min_uV)
			&& (info->vol_output_list[i] <= max_uV))
			return i;
	}
	return -EINVAL;
}

static int rt5025_set_voltage(struct regulator_dev *rdev,
			       int min_uV, int max_uV, unsigned *selector)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data;

	if (check_range(info, min_uV, max_uV)) {
		dev_err(&rdev->dev, "invalid voltage range (%d, %d) uV\n",
			min_uV, max_uV);
		return -EINVAL;
	}
	data = rt5025_find_voltage(rdev, min_uV, max_uV);
	data <<= info->vol_shift;

	return rt5025_assign_bits(info->i2c, info->vol_reg,
		info->vol_mask, data);
}


static int rt5025_get_voltage(struct regulator_dev *rdev)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = rt5025_reg_read(info->i2c, info->vol_reg);
	if (ret < 0)
		return ret;
	ret =  (ret & info->vol_mask)  >> info->vol_shift;
	return rt5025_list_voltage(rdev, ret);
}
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,38) */

static int rt5025_enable(struct regulator_dev *rdev)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);

	return rt5025_set_bits(info->i2c, info->enable_reg,
				info->enable_bit);
}

static int rt5025_disable(struct regulator_dev *rdev)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);

	return rt5025_clr_bits(info->i2c, info->enable_reg,
				info->enable_bit);
}

static int rt5025_is_enabled(struct regulator_dev *rdev)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = rt5025_reg_read(info->i2c, info->enable_reg);
	if (ret < 0)
		return ret;
	return (ret & (info->enable_bit))?1 : 0;
}

static int rt5025_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	if (!info->mode_bit) {
		ret = 0;
	} else {
		switch (mode) {
		case REGULATOR_MODE_NORMAL:
			ret = rt5025_set_bits(info->i2c, info->mode_reg,
				info->mode_bit);
			break;
		case REGULATOR_MODE_FAST:
			ret = rt5025_clr_bits(info->i2c, info->mode_reg,
				info->mode_bit);
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}
	return ret;
}

static unsigned int rt5025_get_mode(struct regulator_dev *rdev)
{
	struct rt5025_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned int mode;
	int data;

	if (!info->mode_bit) {
		mode = REGULATOR_MODE_NORMAL;
	} else {
		data = rt5025_reg_read(info->i2c, info->mode_reg);
		mode = (data & info->mode_bit)?REGULATOR_MODE_NORMAL : REGULATOR_MODE_FAST;
	}
	return mode;
}

static struct regulator_ops rt5025_regulator_ops = {
	.list_voltage		= rt5025_list_voltage,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
	.get_voltage_sel	= rt5025_get_voltage_sel,
	.set_voltage_sel	= rt5025_set_voltage_sel,
#else
	.set_voltage		= rt5025_set_voltage,
	.get_voltage		= rt5025_get_voltage,
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,38) */
	.enable			= rt5025_enable,
	.disable		= rt5025_disable,
	.is_enabled		= rt5025_is_enabled,
	.set_mode		= rt5025_set_mode,
	.get_mode		= rt5025_get_mode,
};

#define RT5025_DCDCVOUT_LIST1 rt5025_vol_output_list1
#define RT5025_DCDCVOUT_LIST2 rt5025_vol_output_list2
#define RT5025_DCDCVOUT_LIST3 rt5025_vol_output_list3
#define RT5025_DCDCVOUT_LIST4 rt5025_vol_output_list4
#define RT5025_LDOVOUT_LIST1  rt5025_vol_output_list2
#define RT5025_LDOVOUT_LIST2  rt5025_vol_output_list2
#define RT5025_LDOVOUT_LIST3  rt5025_vol_output_list5
#define RT5025_LDOVOUT_LIST4  rt5025_vol_output_list5
#define RT5025_LDOVOUT_LIST5  rt5025_vol_output_list5
#define RT5025_LDOVOUT_LIST6  rt5025_vol_output_list5

#define RT5025_DCDCVOUT_SIZE1 rt5025_vol_output_size1
#define RT5025_DCDCVOUT_SIZE2 rt5025_vol_output_size2
#define RT5025_DCDCVOUT_SIZE3 rt5025_vol_output_size3
#define RT5025_DCDCVOUT_SIZE4 rt5025_vol_output_size4
#define RT5025_LDOVOUT_SIZE1  rt5025_vol_output_size2
#define RT5025_LDOVOUT_SIZE2  rt5025_vol_output_size2
#define RT5025_LDOVOUT_SIZE3  rt5025_vol_output_size5
#define RT5025_LDOVOUT_SIZE4  rt5025_vol_output_size5
#define RT5025_LDOVOUT_SIZE5  rt5025_vol_output_size5
#define RT5025_LDOVOUT_SIZE6  rt5025_vol_output_size5


#define RT5025_DCDC(_id, min, max)				\
{								\
	.desc	= {						\
		.name	= "rt5025-dcdc" #_id,			\
		.n_voltages = RT5025_DCDCVOUT_SIZE##_id,	\
		.ops	= &rt5025_regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= RT5025_ID_DCDC##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.vol_output_list = RT5025_DCDCVOUT_LIST##_id,		\
	.vol_output_size = RT5025_DCDCVOUT_SIZE##_id,		\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.vol_reg	= RT5025_DCDCVOUT##_id,			\
	.vol_shift	= RT5025_DCDCVOUT_SHIFT##_id,		\
	.vol_mask	= RT5025_DCDCVOUT_MASK##_id,		\
	.enable_reg	= RT5025_REG_DCDCONOFF,			\
	.enable_bit	= RT5025_DCDCEN_MASK##_id,		\
	.mode_reg	= RT5025_DCDCMODE_REG##_id,		\
	.mode_bit	= RT5025_DCDCMODE_MASK##_id,		\
	.ramp_reg	= RT5025_DCDCVOUT##_id,			\
	.ramp_bit	= RT5025_DCDCRAMP_MASK##_id,		\
}

#define RT5025_LDO(_id, min, max)				\
{								\
	.desc	= {						\
		.name	= "rt5025-ldo" #_id,			\
		.n_voltages = RT5025_LDOVOUT_SIZE##_id,		\
		.ops	= &rt5025_regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= RT5025_ID_LDO##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.vol_output_list = RT5025_LDOVOUT_LIST##_id,		\
	.vol_output_size = RT5025_LDOVOUT_SIZE##_id,		\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.vol_reg	= RT5025_LDOVOUT##_id,			\
	.vol_shift	= RT5025_LDOVOUT_SHIFT##_id,		\
	.vol_mask	= RT5025_LDOVOUT_MASK##_id,		\
	.enable_reg	= RT5025_REG_LDOONOFF,			\
	.enable_bit	= RT5025_LDOEN_MASK##_id,		\
}

static struct rt5025_regulator_info rt5025_regulator_info[] = {
	RT5025_DCDC(1, 700, 2275),
	RT5025_DCDC(2, 700, 3500),
	RT5025_DCDC(3, 700, 3500),
	RT5025_DCDC(4, 4500, 5500),
	RT5025_LDO(1, 700, 3500),
	RT5025_LDO(2, 700, 3500),
	RT5025_LDO(3, 1000, 3300),
	RT5025_LDO(4, 1000, 3300),
	RT5025_LDO(5, 1000, 3300),
	RT5025_LDO(6, 1000, 3300),
};

static struct rt5025_regulator_info *find_regulator_info(int id)
{
	struct rt5025_regulator_info *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5025_regulator_info); i++) {
		ri = &rt5025_regulator_info[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}
struct regulator_dev *rt5025_regulator_register(struct regulator_desc *regulator_desc,
	struct device *dev, struct regulator_init_data *init_data,
	void *driver_data)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	struct regulator_config config = {
		.dev = dev,
		.init_data = init_data,
		.driver_data = driver_data,
		.of_node = dev->of_node,
	};
	return regulator_register(regulator_desc, &config);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 37))
	return regulator_register(regulator_desc, dev, init_data,
		driver_data, dev->of_node);
#else
	return regulator_register(regulator_desc, dev, init_data, driver_data);
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(3,5,0)) */
}

static struct regulator_init_data *of_parse_dt(struct rt5025_regulator_info *ri,
	struct device *dev)
{
	struct regulator_init_data *init_data = NULL;
	#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	int rc;
	u32 tmp;
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	init_data = of_get_regulator_init_data(dev, dev->of_node);
	#else
	init_data = of_get_regulator_init_data(dev);
	#endif /* #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)) */
	rc = of_property_read_u32(np, "rt,ramp_sel", &tmp);
	if (rc) {
		dev_info(dev, "no ramp_sel property, use default value\n");
	} else {
		if (tmp > RT5025_DCDCRAMP_MAX)
			tmp = RT5025_DCDCRAMP_MAX;
		rt5025_assign_bits(ri->i2c, ri->ramp_reg, ri->ramp_bit, tmp);
	}

	if (of_property_read_bool(np, "rt,allow_mode_mask")) {
		init_data->constraints.valid_modes_mask |=
			(REGULATOR_MODE_FAST|\
			REGULATOR_MODE_NORMAL);
		init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_MODE;
	}
	#endif /* #ifdef CONFIG_OF */
	return init_data;
}

static int rt5025_regulator_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5025_regulator_info *ri;
	struct rt5025_regulator_ramp *ramp;
	struct regulator_dev *rdev;
	struct regulator_init_data *init_data;
	bool use_dt = pdev->dev.of_node;

	ri = find_regulator_info(pdev->id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}
	ri->i2c = chip->i2c;
	if (use_dt) {
		init_data = of_parse_dt(ri, &pdev->dev);
	} else {
		init_data = pdata->regulator[pdev->id];
		ramp = init_data->driver_data;
		if (ramp)
			rt5025_assign_bits(ri->i2c, ri->ramp_reg,
				ri->ramp_bit, ramp->ramp_sel);
	}
	if (!init_data) {
		dev_err(&pdev->dev, "no initializing data\n");
		return -EINVAL;
	}

	rdev = rt5025_regulator_register(&ri->desc, &pdev->dev,
				  init_data, ri);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}
	platform_set_drvdata(pdev, rdev);
/*	dev_info(&pdev->dev, "driver successfully loaded\n");*/
	return 0;
}

static int rt5025_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	regulator_unregister(rdev);
	dev_info(&pdev->dev, "%s\n", __func__);
	return 0;
}

static struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025-dcdc1",},
	{ .compatible = "rt,rt5025-dcdc2",},
	{ .compatible = "rt,rt5025-dcdc3",},
	{ .compatible = "rt,rt5025-dcdc4",},
	{ .compatible = "rt,rt5025-ldo1",},
	{ .compatible = "rt,rt5025-ldo2",},
	{ .compatible = "rt,rt5025-ldo3",},
	{ .compatible = "rt,rt5025-ldo4",},
	{ .compatible = "rt,rt5025-ldo5",},
	{ .compatible = "rt,rt5025-ldo6",},
	{},
};

static struct platform_driver rt5025_regulator_driver = {
	.driver = {
		.name = RT5025_DEV_NAME "-regulator",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt5025_regulator_probe,
	.remove = rt5025_regulator_remove,
};

static int rt5025_regulator_init(void)
{
	return platform_driver_register(&rt5025_regulator_driver);
}
subsys_initcall(rt5025_regulator_init);

static void rt5025_regulator_exit(void)
{
	platform_driver_unregister(&rt5025_regulator_driver);
}
module_exit(rt5025_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Regulator driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEV_NAME "-regulator");
MODULE_VERSION(RT5025_DRV_VER);
