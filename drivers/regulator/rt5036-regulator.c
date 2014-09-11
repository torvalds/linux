/*
 *  drivers/regulator/rt5036-regulator.c
 *  Driver for Richtek RT5036 PMIC Regulator
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

#include <linux/mfd/rt5036/rt5036.h>
#include <linux/regulator/rt5036-regulator.h>

struct rt5036_regulator_info {
	struct regulator_desc desc;
	struct regulator_dev *regulator;
	struct i2c_client *i2c;
	const unsigned int *vol_output_list;
	const int vol_output_size;
	int min_uV;
	int max_uV;
	unsigned char nvol_reg;
	unsigned char nvol_shift;
	unsigned char nvol_mask;
	unsigned char nenable_reg;
	unsigned char nenable_bit;
	unsigned char nmode_reg;
	unsigned char nmode_bit;
	unsigned char nramp_reg;
	unsigned char nramp_bit;
	unsigned char nramp_shift;
	unsigned char svol_reg;
	unsigned char svol_shift;
	unsigned char svol_mask;
	unsigned char senable_reg;
	unsigned char senable_bit;
	unsigned char smode_reg;
	unsigned char smode_bit;
	unsigned char sramp_reg;
	unsigned char sramp_bit;
	unsigned char sramp_shift;
};

/*For DCDC1~4 and LDO1~4 and LSW1~2*/
static const unsigned int rt5036_vol_output_list[] = {
	/*0~7 */
	800 * 1000, 825 * 1000, 850 * 1000, 875 * 1000, 900 * 1000, 925 * 1000,
	    950 * 1000, 975 * 1000,
	/*8~15 */
	1000 * 1000, 1025 * 1000, 1050 * 1000, 1075 * 1000, 1100 * 1000,
	    1125 * 1000, 1150 * 1000, 1175 * 1000,
	/*16~23 */
	1200 * 1000, 1225 * 1000, 1250 * 1000, 1275 * 1000, 1300 * 1000,
	    1325 * 1000, 1350 * 1000, 1375 * 1000,
	/*24~31 */
	1400 * 1000, 1425 * 1000, 1450 * 1000, 1475 * 1000, 1500 * 1000,
	    1525 * 1000, 1550 * 1000, 1575 * 1000,
	/*32~39 */
	1600 * 1000, 1625 * 1000, 1650 * 1000, 1675 * 1000, 1700 * 1000,
	    1725 * 1000, 1750 * 1000, 1775 * 1000,
	/*40~47 */
	1800 * 1000, 1825 * 1000, 1850 * 1000, 1875 * 1000, 1900 * 1000,
	    1925 * 1000, 1950 * 1000, 1975 * 1000,
	/*48~55 */
	2000 * 1000, 2025 * 1000, 2050 * 1000, 2075 * 1000, 2100 * 1000,
	    2125 * 1000, 2150 * 1000, 2175 * 1000,
	/*56~63 */
	2200 * 1000, 2225 * 1000, 2250 * 1000, 2275 * 1000, 2300 * 1000,
	    2325 * 1000, 2350 * 1000, 2375 * 1000,
	/*64~71 */
	2400 * 1000, 2425 * 1000, 2450 * 1000, 2475 * 1000, 2500 * 1000,
	    2525 * 1000, 2550 * 1000, 2575 * 1000,
	/*72~79 */
	2600 * 1000, 2625 * 1000, 2650 * 1000, 2675 * 1000, 2700 * 1000,
	    2725 * 1000, 2750 * 1000, 2775 * 1000,
	/*80~87 */
	2800 * 1000, 2825 * 1000, 2850 * 1000, 2875 * 1000, 2900 * 1000,
	    2925 * 1000, 2950 * 1000, 2975 * 1000,
	/*88~95 */
	3000 * 1000, 3025 * 1000, 3050 * 1000, 3075 * 1000, 3100 * 1000,
	    3125 * 1000, 3150 * 1000, 3175 * 1000,
	/*96~103 */
	3200 * 1000, 3225 * 1000, 3250 * 1000, 3275 * 1000, 3300 * 1000,
	    3300 * 1000, 3300 * 1000, 3300 * 1000,
	/*104~111 */
	3300 * 1000, 3300 * 1000, 3300 * 1000, 3300 * 1000, 3300 * 1000,
	    3300 * 1000, 3300 * 1000, 3300 * 1000,
	/*112~119 */
	3300 * 1000, 3300 * 1000, 3300 * 1000, 3300 * 1000, 3300 * 1000,
	    3300 * 1000, 3300 * 1000, 3300 * 1000,
	/*120~127 */
	3300 * 1000, 3300 * 1000, 3300 * 1000, 3300 * 1000, 3300 * 1000,
	    3300 * 1000, 3300 * 1000, 3300 * 1000,
};

#define rt5036_vol_output_size	ARRAY_SIZE(rt5036_vol_output_list)

static inline int check_range(struct rt5036_regulator_info *info,
			      int min_uV, int max_uV)
{
	if (min_uV < info->min_uV || min_uV > info->max_uV)
		return -EINVAL;
	return 0;
}

static int rt5036_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);

	return (index >= info->vol_output_size) ?
	    -EINVAL : info->vol_output_list[index];
}

static int rt5036_find_voltage(struct regulator_dev *rdev,
			       int min_uV, int max_uV)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	int i = 0;
	const int count = info->vol_output_size;

	for (i = 0; i < count; i++) {
		if ((info->vol_output_list[i] >= min_uV)
		    && (info->vol_output_list[i] <= max_uV))
			return i;
	}
	return -EINVAL;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
static int rt5036_set_voltage_sel(struct regulator_dev *rdev, unsigned selector)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data;
	const int count = info->vol_output_size;

	if (selector > count)
		return -EINVAL;
	data = (unsigned char)selector;
	data <<= info->nvol_shift;
	return rt5036_assign_bits(info->i2c, info->nvol_reg, info->nvol_mask,
				  data);
}

static int rt5036_get_voltage_sel(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = rt5036_reg_read(info->i2c, info->nvol_reg);
	if (ret < 0)
		return ret;
	return (ret & info->nvol_mask) >> info->nvol_shift;
}
#else

static int rt5036_set_voltage(struct regulator_dev *rdev,
			      int min_uV, int max_uV, unsigned *selector)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data;

	if (check_range(info, min_uV, max_uV)) {
		dev_err(&rdev->dev, "invalid voltage range (%d, %d) uV\n",
			min_uV, max_uV);
		return -EINVAL;
	}
	data = rt5036_find_voltage(rdev, min_uV, max_uV);
	data <<= info->nvol_shift;
	return rt5036_assign_bits(info->i2c, info->nvol_reg, info->nvol_mask,
				  data);
}

static int rt5036_get_voltage(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = rt5036_reg_read(info->i2c, info->nvol_reg);
	if (ret < 0)
		return ret;
	ret = (ret & info->nvol_mask) >> info->nvol_shift;
	return rt5036_list_voltage(rdev, ret);
}
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,38) */

static int rt5036_enable(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);

	return rt5036_set_bits(info->i2c, info->nenable_reg, info->nenable_bit);
}

static int rt5036_disable(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);

	return rt5036_clr_bits(info->i2c, info->nenable_reg, info->nenable_bit);
}

static int rt5036_is_enabled(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = rt5036_reg_read(info->i2c, info->nenable_reg);
	if (ret < 0)
		return ret;
	return (ret & info->nenable_bit) ? 1 : 0;
}

static int rt5036_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret =
		    rt5036_set_bits(info->i2c, info->nmode_reg,
				    info->nmode_bit);
		break;
	case REGULATOR_MODE_FAST:
		ret =
		    rt5036_clr_bits(info->i2c, info->nmode_reg,
				    info->nmode_bit);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static unsigned int rt5036_get_mode(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned int mode;
	int data;

	data = rt5036_reg_read(info->i2c, info->nmode_reg);
	mode =
	    (data & info->nmode_bit) ? REGULATOR_MODE_NORMAL :
	    REGULATOR_MODE_FAST;
	return mode;
}

static int rt5036_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned char data;

	if (check_range(info, uV, uV)) {
		dev_err(&rdev->dev, "invalid voltage range (%d, %d) uV\n",
			uV, uV);
		return -EINVAL;
	}
	data = rt5036_find_voltage(rdev, uV, uV);
	data <<= info->svol_shift;
	return rt5036_assign_bits(info->i2c, info->svol_reg, info->svol_mask,
				  data);
}

static int rt5036_set_suspend_enable(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);

	return rt5036_set_bits(info->i2c, info->senable_reg, info->senable_bit);
}

static int rt5036_set_suspend_disable(struct regulator_dev *rdev)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);

	return rt5036_clr_bits(info->i2c, info->senable_reg, info->senable_bit);
}

static int rt5036_set_suspend_mode(struct regulator_dev *rdev,
				   unsigned int mode)
{
	struct rt5036_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret =
		    rt5036_set_bits(info->i2c, info->smode_reg,
				    info->smode_bit);
		break;
	case REGULATOR_MODE_FAST:
		ret =
		    rt5036_clr_bits(info->i2c, info->smode_reg,
				    info->smode_bit);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static struct regulator_ops rt5036_regulator_ops = {
	.list_voltage = rt5036_list_voltage,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
	.get_voltage_sel = rt5036_get_voltage_sel,
	.set_voltage_sel = rt5036_set_voltage_sel,
#else
	.set_voltage = rt5036_set_voltage,
	.get_voltage = rt5036_get_voltage,
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,38) */
	.enable = rt5036_enable,
	.disable = rt5036_disable,
	.is_enabled = rt5036_is_enabled,
	.set_mode = rt5036_set_mode,
	.get_mode = rt5036_get_mode,
	.set_suspend_voltage = rt5036_set_suspend_voltage,
	.set_suspend_enable = rt5036_set_suspend_enable,
	.set_suspend_disable = rt5036_set_suspend_disable,
	.set_suspend_mode = rt5036_set_suspend_mode,
};

#define RT5036_VOUT_LIST rt5036_vol_output_list
#define RT5036_VOUT_SIZE rt5036_vol_output_size

#define RT5036_DCDC(_id, min, max)				\
{								\
	.desc	= {						\
		.name	= "rt5036-dcdc" #_id,			\
		.n_voltages = RT5036_VOUT_SIZE,			\
		.ops	= &rt5036_regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= RT5036_ID_DCDC##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.vol_output_list = RT5036_VOUT_LIST,			\
	.vol_output_size = RT5036_VOUT_SIZE,			\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.nvol_reg	= RT5036_REG_BUCKVN##_id,		\
	.nvol_shift	= RT5036_DCDCVOUT_SHIFT##_id,		\
	.nvol_mask	= RT5036_DCDCVOUT_MASK##_id,		\
	.nenable_reg	= RT5036_REG_BUCKLDONEN,		\
	.nenable_bit	= RT5036_DCDCEN_MASK##_id,		\
	.nmode_reg	= RT5036_REG_BUCKVRCNEN,		\
	.nmode_bit	= RT5036_DCDCMODE_MASK##_id,		\
	.nramp_reg	= RT5036_REG_BUCKVRCN,			\
	.nramp_bit	= RT5036_DCDCRAMP_MASK##_id,		\
	.nramp_shift	= RT5036_DCDCRAMP_SHIFT##_id,		\
	.svol_reg	= RT5036_REG_BUCKVS##_id,		\
	.svol_shift	= RT5036_DCDCVOUT_SHIFT##_id,		\
	.svol_mask	= RT5036_DCDCVOUT_MASK##_id,		\
	.senable_reg	= RT5036_REG_BUCKLDOSEN,		\
	.senable_bit	= RT5036_DCDCEN_MASK##_id,		\
	.smode_reg	= RT5036_REG_BUCKVRCSEN,		\
	.smode_bit	= RT5036_DCDCMODE_MASK##_id,		\
	.sramp_reg	= RT5036_REG_BUCKVRCS,			\
	.sramp_bit	= RT5036_DCDCRAMP_MASK##_id,		\
	.sramp_shift	= RT5036_DCDCRAMP_SHIFT##_id,		\
}

#define RT5036_LDO(_id, min, max)				\
{								\
	.desc	= {						\
		.name	= "rt5036-ldo" #_id,			\
		.n_voltages = RT5036_VOUT_SIZE,			\
		.ops	= &rt5036_regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= RT5036_ID_LDO##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.vol_output_list = RT5036_VOUT_LIST,			\
	.vol_output_size = RT5036_VOUT_SIZE,			\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.nvol_reg	= RT5036_REG_LDOVN##_id,		\
	.nvol_shift	= RT5036_LDOVOUT_SHIFT##_id,		\
	.nvol_mask	= RT5036_LDOVOUT_MASK##_id,		\
	.nenable_reg	= RT5036_REG_BUCKLDONEN,		\
	.nenable_bit	= RT5036_LDOEN_MASK##_id,		\
	.nmode_reg	= RT5036_REG_LDOVRCNEN,			\
	.nmode_bit	= RT5036_LDOMODE_MASK##_id,		\
	.nramp_reg	= RT5036_REG_LDOVRCN,			\
	.nramp_bit	= RT5036_LDORAMP_MASK##_id,		\
	.nramp_shift	= RT5036_LDORAMP_SHIFT##_id,		\
	.svol_reg	= RT5036_REG_LDOVS##_id,		\
	.svol_shift	= RT5036_LDOVOUT_SHIFT##_id,		\
	.svol_mask	= RT5036_LDOVOUT_MASK##_id,		\
	.senable_reg	= RT5036_REG_BUCKLDOSEN,		\
	.senable_bit	= RT5036_LDOEN_MASK##_id,		\
	.smode_reg	= RT5036_REG_LDOVRCSEN,			\
	.smode_bit	= RT5036_LDOMODE_MASK##_id,		\
	.sramp_reg	= RT5036_REG_LDOVRCS,			\
	.sramp_bit	= RT5036_LDORAMP_MASK##_id,		\
	.sramp_shift	= RT5036_LDORAMP_SHIFT##_id,		\
}

#define RT5036_LSW(_id, min, max)				\
{								\
	.desc	= {						\
		.name	= "rt5036-lsw" #_id,			\
		.n_voltages = RT5036_VOUT_SIZE,			\
		.ops	= &rt5036_regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= RT5036_ID_LSW##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.vol_output_list = RT5036_VOUT_LIST,			\
	.vol_output_size = RT5036_VOUT_SIZE,			\
	.min_uV		= min * 1000,				\
	.max_uV		= max * 1000,				\
	.nvol_reg	= RT5036_REG_LSWVN##_id,		\
	.nvol_shift	= RT5036_LSWVOUT_SHIFT##_id,		\
	.nvol_mask	= RT5036_LSWVOUT_MASK##_id,		\
	.nenable_reg	= RT5036_REG_LSWEN,			\
	.nenable_bit	= RT5036_LSWNEN_MASK##_id,		\
	.nmode_reg	= RT5036_REG_LSWVRCEN,			\
	.nmode_bit	= RT5036_LSWNMODE_MASK##_id,		\
	.nramp_reg	= RT5036_REG_LSWVRC,			\
	.nramp_bit	= RT5036_LSWNRAMP_MASK##_id,		\
	.nramp_shift	= RT5036_LSWNRAMP_SHIFT##_id,		\
	.svol_reg	= RT5036_REG_LSWVS##_id,		\
	.svol_shift	= RT5036_LSWVOUT_SHIFT##_id,		\
	.svol_mask	= RT5036_LSWVOUT_MASK##_id,		\
	.senable_reg	= RT5036_REG_LSWEN,			\
	.senable_bit	= RT5036_LSWSEN_MASK##_id,		\
	.smode_reg	= RT5036_REG_LSWVRCEN,			\
	.smode_bit	= RT5036_LSWSMODE_MASK##_id,		\
	.sramp_reg	= RT5036_REG_LSWVRC,			\
	.sramp_bit	= RT5036_LSWSRAMP_MASK##_id,		\
	.sramp_shift	= RT5036_LSWSRAMP_SHIFT##_id,		\
}

static struct rt5036_regulator_info rt5036_regulator_info[] = {
	RT5036_DCDC(1, 800, 3300),
	RT5036_DCDC(2, 800, 3300),
	RT5036_DCDC(3, 800, 3300),
	RT5036_DCDC(4, 800, 3300),
	RT5036_LDO(1, 800, 3300),
	RT5036_LDO(2, 800, 3300),
	RT5036_LDO(3, 800, 3300),
	RT5036_LDO(4, 800, 3300),
	RT5036_LSW(1, 800, 3300),
	RT5036_LSW(2, 800, 3300),
};

static struct rt5036_regulator_info *find_regulator_info(int id)
{
	struct rt5036_regulator_info *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5036_regulator_info); i++) {
		ri = &rt5036_regulator_info[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

inline struct regulator_dev *rt5036_regulator_register(struct regulator_desc
						       *regulator_desc,
						       struct device *dev,
						       struct
						       regulator_init_data
						       *init_data,
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
	return regulator_register(regulator_desc, dev, init_data, driver_data,
				  dev->of_node);
#else
	return regulator_register(regulator_desc, dev, init_data, driver_data);
#endif /* LINUX_VERSION_CODE>=KERNEL_VERSION(3,5,0)) */
}

static struct regulator_init_data *rt_parse_dt(struct
							 rt5036_regulator_info
							 *ri,
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
#endif /* #if (LINUX_KERNEL_VERSION >= KERNEL_VERSION(3,3,0)) */

	if (init_data) {
		init_data->supply_regulator = (char *)of_get_property(np,
							"supply-regulator",
							NULL);
		rc = of_property_read_u32(np, "rt,standby_vol", &tmp);
		if (rc)
			dev_info(dev, "no standby voltage specified\n");
		else
			init_data->constraints.state_standby.uV = tmp;
		if (of_property_read_bool(np, "rt,standby_enabled")) {
			init_data->constraints.state_standby.enabled = 1;
			init_data->constraints.initial_state =
			    PM_SUSPEND_STANDBY;
		}
		if (of_property_read_bool(np, "rt,standby_disabled")) {
			init_data->constraints.state_standby.disabled = 1;
			init_data->constraints.initial_state =
			    PM_SUSPEND_STANDBY;
		}
	}

	rc = of_property_read_u32(np, "rt,nramp_sel", &tmp);
	if (rc) {
		dev_info(dev, "no nramp_sel property, use default value\n");
	} else {
		if (tmp > RT5036_RAMP_MAX)
			tmp = RT5036_RAMP_MAX;
		rt5036_assign_bits(ri->i2c, ri->nramp_reg, ri->nramp_bit,
				   tmp << ri->nramp_shift);
	}

	rc = of_property_read_u32(np, "rt,sramp_sel", &tmp);
	if (rc) {
		dev_info(dev, "no sramp_sel property, use default value\n");
	} else {
		if (tmp > RT5036_RAMP_MAX)
			tmp = RT5036_RAMP_MAX;
		rt5036_assign_bits(ri->i2c, ri->sramp_reg, ri->sramp_bit,
				   tmp << ri->sramp_shift);
	}

	if (of_property_read_bool(np, "rt,allow_mode_mask")) {
		init_data->constraints.valid_modes_mask |=
		    (REGULATOR_MODE_FAST | REGULATOR_MODE_NORMAL);
		init_data->constraints.valid_ops_mask |= REGULATOR_CHANGE_MODE;
	}
#endif /* #ifdef CONFIG_OF */
	return init_data;
}

static int rt5036_regulator_probe(struct platform_device *pdev)
{
	struct rt5036_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5036_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5036_regulator_info *ri;
	struct rt5036_regulator_ramp *ramp;
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
		init_data = rt_parse_dt(ri, &pdev->dev);
	} else {
		if (!pdata)
			return -EINVAL;
		init_data = pdata->regulator[pdev->id];
		ramp = init_data ? init_data->driver_data : NULL;
		if (ramp) {
			rt5036_assign_bits(ri->i2c, ri->nramp_reg,
					   ri->nramp_bit,
					   ramp->nramp_sel << ri->nramp_shift);
			rt5036_assign_bits(ri->i2c, ri->sramp_reg,
					   ri->sramp_bit,
					   ramp->sramp_sel << ri->sramp_shift);
		}
	}

	if (!init_data) {
		dev_err(&pdev->dev, "no initializing data\n");
		return -EINVAL;
	}

	rdev = rt5036_regulator_register(&ri->desc, &pdev->dev, init_data, ri);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);
	/*dev_info(&pdev->dev, "regulator successfully registered\n");*/
	RTINFO("\n");
	return 0;
}

static int rt5036_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	RTINFO("\n");
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt5036-dcdc1",},
	{.compatible = "rt,rt5036-dcdc2",},
	{.compatible = "rt,rt5036-dcdc3",},
	{.compatible = "rt,rt5036-dcdc4",},
	{.compatible = "rt,rt5036-ldo1",},
	{.compatible = "rt,rt5036-ldo2",},
	{.compatible = "rt,rt5036-ldo3",},
	{.compatible = "rt,rt5036-ldo4",},
	{.compatible = "rt,rt5036-lsw1",},
	{.compatible = "rt,rt5036-lsw2",},
	{},
};

static struct platform_driver rt5036_regulator_driver = {
	.driver = {
		   .name = RT5036_DEV_NAME "-regulator",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt5036_regulator_probe,
	.remove = rt5036_regulator_remove,
};

static int __init rt5036_regulator_init(void)
{
	return platform_driver_register(&rt5036_regulator_driver);
}
subsys_initcall(rt5036_regulator_init);

static void __exit rt5036_regulator_exit(void)
{
	platform_driver_unregister(&rt5036_regulator_driver);
}
module_exit(rt5036_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Regulator driver for RT5036");
MODULE_ALIAS("platform:" RT5036_DEV_NAME "-regulator");
MODULE_VERSION(RT5036_DRV_VER);
