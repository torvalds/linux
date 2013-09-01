/*
 * Regulators driver for Dialog Semiconductor DA903x
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Copyright (C) 2008 Compulab Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/da903x.h>

/* DA9030 Registers */
#define DA9030_INVAL		(-1)
#define DA9030_LDO1011		(0x10)
#define DA9030_LDO15		(0x11)
#define DA9030_LDO1416		(0x12)
#define DA9030_LDO1819		(0x13)
#define DA9030_LDO17		(0x14)
#define DA9030_BUCK2DVM1	(0x15)
#define DA9030_BUCK2DVM2	(0x16)
#define DA9030_RCTL11		(0x17)
#define DA9030_RCTL21		(0x18)
#define DA9030_LDO1		(0x90)
#define DA9030_LDO23		(0x91)
#define DA9030_LDO45		(0x92)
#define DA9030_LDO6		(0x93)
#define DA9030_LDO78		(0x94)
#define DA9030_LDO912		(0x95)
#define DA9030_BUCK		(0x96)
#define DA9030_RCTL12		(0x97)
#define DA9030_RCTL22		(0x98)
#define DA9030_LDO_UNLOCK	(0xa0)
#define DA9030_LDO_UNLOCK_MASK	(0xe0)
#define DA9034_OVER1		(0x10)

/* DA9034 Registers */
#define DA9034_INVAL		(-1)
#define DA9034_OVER2		(0x11)
#define DA9034_OVER3		(0x12)
#define DA9034_LDO643		(0x13)
#define DA9034_LDO987		(0x14)
#define DA9034_LDO1110		(0x15)
#define DA9034_LDO1312		(0x16)
#define DA9034_LDO1514		(0x17)
#define DA9034_VCC1		(0x20)
#define DA9034_ADTV1		(0x23)
#define DA9034_ADTV2		(0x24)
#define DA9034_AVRC		(0x25)
#define DA9034_CDTV1		(0x26)
#define DA9034_CDTV2		(0x27)
#define DA9034_CVRC		(0x28)
#define DA9034_SDTV1		(0x29)
#define DA9034_SDTV2		(0x2a)
#define DA9034_SVRC		(0x2b)
#define DA9034_MDTV1		(0x32)
#define DA9034_MDTV2		(0x33)
#define DA9034_MVRC		(0x34)

/* DA9035 Registers. DA9034 Registers are comptabile to DA9035. */
#define DA9035_OVER3		(0x12)
#define DA9035_VCC2		(0x1f)
#define DA9035_3DTV1		(0x2c)
#define DA9035_3DTV2		(0x2d)
#define DA9035_3VRC		(0x2e)
#define DA9035_AUTOSKIP		(0x2f)

struct da903x_regulator_info {
	struct regulator_desc desc;

	int	max_uV;
	int	vol_reg;
	int	vol_shift;
	int	vol_nbits;
	int	update_reg;
	int	update_bit;
	int	enable_reg;
	int	enable_bit;
};

static inline struct device *to_da903x_dev(struct regulator_dev *rdev)
{
	return rdev_get_dev(rdev)->parent->parent;
}

static inline int check_range(struct da903x_regulator_info *info,
				int min_uV, int max_uV)
{
	if (min_uV < info->desc.min_uV || min_uV > info->max_uV)
		return -EINVAL;

	return 0;
}

/* DA9030/DA9034 common operations */
static int da903x_set_voltage_sel(struct regulator_dev *rdev, unsigned selector)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da9034_dev = to_da903x_dev(rdev);
	uint8_t val, mask;

	if (rdev->desc->n_voltages == 1)
		return -EINVAL;

	val = selector << info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	return da903x_update(da9034_dev, info->vol_reg, val, mask);
}

static int da903x_get_voltage_sel(struct regulator_dev *rdev)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da9034_dev = to_da903x_dev(rdev);
	uint8_t val, mask;
	int ret;

	if (rdev->desc->n_voltages == 1)
		return 0;

	ret = da903x_read(da9034_dev, info->vol_reg, &val);
	if (ret)
		return ret;

	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = (val & mask) >> info->vol_shift;

	return val;
}

static int da903x_enable(struct regulator_dev *rdev)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da9034_dev = to_da903x_dev(rdev);

	return da903x_set_bits(da9034_dev, info->enable_reg,
					1 << info->enable_bit);
}

static int da903x_disable(struct regulator_dev *rdev)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da9034_dev = to_da903x_dev(rdev);

	return da903x_clr_bits(da9034_dev, info->enable_reg,
					1 << info->enable_bit);
}

static int da903x_is_enabled(struct regulator_dev *rdev)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da9034_dev = to_da903x_dev(rdev);
	uint8_t reg_val;
	int ret;

	ret = da903x_read(da9034_dev, info->enable_reg, &reg_val);
	if (ret)
		return ret;

	return !!(reg_val & (1 << info->enable_bit));
}

/* DA9030 specific operations */
static int da9030_set_ldo1_15_voltage_sel(struct regulator_dev *rdev,
					  unsigned selector)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da903x_dev = to_da903x_dev(rdev);
	uint8_t val, mask;
	int ret;

	val = selector << info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val |= DA9030_LDO_UNLOCK; /* have to set UNLOCK bits */
	mask |= DA9030_LDO_UNLOCK_MASK;

	/* write twice */
	ret = da903x_update(da903x_dev, info->vol_reg, val, mask);
	if (ret)
		return ret;

	return da903x_update(da903x_dev, info->vol_reg, val, mask);
}

static int da9030_map_ldo14_voltage(struct regulator_dev *rdev,
				    int min_uV, int max_uV)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	int thresh, sel;

	if (check_range(info, min_uV, max_uV)) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	thresh = (info->max_uV + info->desc.min_uV) / 2;
	if (min_uV < thresh) {
		sel = DIV_ROUND_UP(thresh - min_uV, info->desc.uV_step);
		sel |= 0x4;
	} else {
		sel = DIV_ROUND_UP(min_uV - thresh, info->desc.uV_step);
	}

	return sel;
}

static int da9030_list_ldo14_voltage(struct regulator_dev *rdev,
				     unsigned selector)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	int volt;

	if (selector & 0x4)
		volt = rdev->desc->min_uV +
		       rdev->desc->uV_step * (3 - (selector & ~0x4));
	else
		volt = (info->max_uV + rdev->desc->min_uV) / 2 +
		       rdev->desc->uV_step * (selector & ~0x4);

	if (volt > info->max_uV)
		return -EINVAL;

	return volt;
}

/* DA9034 specific operations */
static int da9034_set_dvc_voltage_sel(struct regulator_dev *rdev,
				      unsigned selector)
{
	struct da903x_regulator_info *info = rdev_get_drvdata(rdev);
	struct device *da9034_dev = to_da903x_dev(rdev);
	uint8_t val, mask;
	int ret;

	val = selector << info->vol_shift;
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	ret = da903x_update(da9034_dev, info->vol_reg, val, mask);
	if (ret)
		return ret;

	ret = da903x_set_bits(da9034_dev, info->update_reg,
					1 << info->update_bit);
	return ret;
}

static const struct regulator_linear_range da9034_ldo12_ranges[] = {
	{ .min_uV = 1700000, .max_uV = 2050000, .min_sel =  0, .max_sel = 7,
	  .uV_step =  50000 },
	{ .min_uV = 2700000, .max_uV = 3050000, .min_sel =  8, .max_sel = 15,
	  .uV_step =  50000 },
};

static struct regulator_ops da903x_regulator_ldo_ops = {
	.set_voltage_sel = da903x_set_voltage_sel,
	.get_voltage_sel = da903x_get_voltage_sel,
	.list_voltage	= regulator_list_voltage_linear,
	.map_voltage	= regulator_map_voltage_linear,
	.enable		= da903x_enable,
	.disable	= da903x_disable,
	.is_enabled	= da903x_is_enabled,
};

/* NOTE: this is dedicated for the insane DA9030 LDO14 */
static struct regulator_ops da9030_regulator_ldo14_ops = {
	.set_voltage_sel = da903x_set_voltage_sel,
	.get_voltage_sel = da903x_get_voltage_sel,
	.list_voltage	= da9030_list_ldo14_voltage,
	.map_voltage	= da9030_map_ldo14_voltage,
	.enable		= da903x_enable,
	.disable	= da903x_disable,
	.is_enabled	= da903x_is_enabled,
};

/* NOTE: this is dedicated for the DA9030 LDO1 and LDO15 that have locks  */
static struct regulator_ops da9030_regulator_ldo1_15_ops = {
	.set_voltage_sel = da9030_set_ldo1_15_voltage_sel,
	.get_voltage_sel = da903x_get_voltage_sel,
	.list_voltage	= regulator_list_voltage_linear,
	.map_voltage	= regulator_map_voltage_linear,
	.enable		= da903x_enable,
	.disable	= da903x_disable,
	.is_enabled	= da903x_is_enabled,
};

static struct regulator_ops da9034_regulator_dvc_ops = {
	.set_voltage_sel = da9034_set_dvc_voltage_sel,
	.get_voltage_sel = da903x_get_voltage_sel,
	.list_voltage	= regulator_list_voltage_linear,
	.map_voltage	= regulator_map_voltage_linear,
	.enable		= da903x_enable,
	.disable	= da903x_disable,
	.is_enabled	= da903x_is_enabled,
};

/* NOTE: this is dedicated for the insane LDO12 */
static struct regulator_ops da9034_regulator_ldo12_ops = {
	.set_voltage_sel = da903x_set_voltage_sel,
	.get_voltage_sel = da903x_get_voltage_sel,
	.list_voltage	= regulator_list_voltage_linear_range,
	.map_voltage	= regulator_map_voltage_linear_range,
	.enable		= da903x_enable,
	.disable	= da903x_disable,
	.is_enabled	= da903x_is_enabled,
};

#define DA903x_LDO(_pmic, _id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
{									\
	.desc	= {							\
		.name	= "LDO" #_id,					\
		.ops	= &da903x_regulator_ldo_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= _pmic##_ID_LDO##_id,				\
		.n_voltages = (step) ? ((max - min) / step + 1) : 1,	\
		.owner	= THIS_MODULE,					\
		.min_uV	 = (min) * 1000,				\
		.uV_step = (step) * 1000,				\
	},								\
	.max_uV		= (max) * 1000,					\
	.vol_reg	= _pmic##_##vreg,				\
	.vol_shift	= (shift),					\
	.vol_nbits	= (nbits),					\
	.enable_reg	= _pmic##_##ereg,				\
	.enable_bit	= (ebit),					\
}

#define DA903x_DVC(_pmic, _id, min, max, step, vreg, nbits, ureg, ubit, ereg, ebit) \
{									\
	.desc	= {							\
		.name	= #_id,						\
		.ops	= &da9034_regulator_dvc_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= _pmic##_ID_##_id,				\
		.n_voltages = (step) ? ((max - min) / step + 1) : 1,	\
		.owner	= THIS_MODULE,					\
		.min_uV = (min) * 1000,					\
		.uV_step = (step) * 1000,				\
	},								\
	.max_uV		= (max) * 1000,					\
	.vol_reg	= _pmic##_##vreg,				\
	.vol_shift	= (0),						\
	.vol_nbits	= (nbits),					\
	.update_reg	= _pmic##_##ureg,				\
	.update_bit	= (ubit),					\
	.enable_reg	= _pmic##_##ereg,				\
	.enable_bit	= (ebit),					\
}

#define DA9034_LDO(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	DA903x_LDO(DA9034, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

#define DA9030_LDO(_id, min, max, step, vreg, shift, nbits, ereg, ebit)	\
	DA903x_LDO(DA9030, _id, min, max, step, vreg, shift, nbits, ereg, ebit)

#define DA9030_DVC(_id, min, max, step, vreg, nbits, ureg, ubit, ereg, ebit) \
	DA903x_DVC(DA9030, _id, min, max, step, vreg, nbits, ureg, ubit, \
		   ereg, ebit)

#define DA9034_DVC(_id, min, max, step, vreg, nbits, ureg, ubit, ereg, ebit) \
	DA903x_DVC(DA9034, _id, min, max, step, vreg, nbits, ureg, ubit, \
		   ereg, ebit)

#define DA9035_DVC(_id, min, max, step, vreg, nbits, ureg, ubit, ereg, ebit) \
	DA903x_DVC(DA9035, _id, min, max, step, vreg, nbits, ureg, ubit, \
		   ereg, ebit)

static struct da903x_regulator_info da903x_regulator_info[] = {
	/* DA9030 */
	DA9030_DVC(BUCK2, 850, 1625, 25, BUCK2DVM1, 5, BUCK2DVM1, 7, RCTL11, 0),

	DA9030_LDO( 1, 1200, 3200, 100,    LDO1, 0, 5, RCTL12, 1),
	DA9030_LDO( 2, 1800, 3200, 100,   LDO23, 0, 4, RCTL12, 2),
	DA9030_LDO( 3, 1800, 3200, 100,   LDO23, 4, 4, RCTL12, 3),
	DA9030_LDO( 4, 1800, 3200, 100,   LDO45, 0, 4, RCTL12, 4),
	DA9030_LDO( 5, 1800, 3200, 100,   LDO45, 4, 4, RCTL12, 5),
	DA9030_LDO( 6, 1800, 3200, 100,    LDO6, 0, 4, RCTL12, 6),
	DA9030_LDO( 7, 1800, 3200, 100,   LDO78, 0, 4, RCTL12, 7),
	DA9030_LDO( 8, 1800, 3200, 100,   LDO78, 4, 4, RCTL22, 0),
	DA9030_LDO( 9, 1800, 3200, 100,  LDO912, 0, 4, RCTL22, 1),
	DA9030_LDO(10, 1800, 3200, 100, LDO1011, 0, 4, RCTL22, 2),
	DA9030_LDO(11, 1800, 3200, 100, LDO1011, 4, 4, RCTL22, 3),
	DA9030_LDO(12, 1800, 3200, 100,  LDO912, 4, 4, RCTL22, 4),
	DA9030_LDO(14, 2760, 2940,  30, LDO1416, 0, 3, RCTL11, 4),
	DA9030_LDO(15, 1100, 2650,  50,	  LDO15, 0, 5, RCTL11, 5),
	DA9030_LDO(16, 1100, 2650,  50, LDO1416, 3, 5, RCTL11, 6),
	DA9030_LDO(17, 1800, 3200, 100,   LDO17, 0, 4, RCTL11, 7),
	DA9030_LDO(18, 1800, 3200, 100, LDO1819, 0, 4, RCTL21, 2),
	DA9030_LDO(19, 1800, 3200, 100, LDO1819, 4, 4, RCTL21, 1),
	DA9030_LDO(13, 2100, 2100, 0, INVAL, 0, 0, RCTL11, 3), /* fixed @2.1V */

	/* DA9034 */
	DA9034_DVC(BUCK1, 725, 1500, 25, ADTV2, 5, VCC1, 0, OVER1, 0),
	DA9034_DVC(BUCK2, 725, 1500, 25, CDTV2, 5, VCC1, 2, OVER1, 1),
	DA9034_DVC(LDO2,  725, 1500, 25, SDTV2, 5, VCC1, 4, OVER1, 2),
	DA9034_DVC(LDO1, 1700, 2075, 25, MDTV1, 4, VCC1, 6, OVER3, 4),

	DA9034_LDO( 3, 1800, 3300, 100,  LDO643, 0, 4, OVER3, 5),
	DA9034_LDO( 4, 1800, 2900,1100,  LDO643, 4, 1, OVER3, 6),
	DA9034_LDO( 6, 2500, 2850,  50,  LDO643, 5, 3, OVER2, 0),
	DA9034_LDO( 7, 2700, 3050,  50,  LDO987, 0, 3, OVER2, 1),
	DA9034_LDO( 8, 2700, 2850,  50,  LDO987, 3, 2, OVER2, 2),
	DA9034_LDO( 9, 2700, 3050,  50,  LDO987, 5, 3, OVER2, 3),
	DA9034_LDO(10, 2700, 3050,  50, LDO1110, 0, 3, OVER2, 4),
	DA9034_LDO(11, 1800, 3300, 100, LDO1110, 4, 4, OVER2, 5),
	DA9034_LDO(12, 1700, 3050,  50, LDO1312, 0, 4, OVER3, 6),
	DA9034_LDO(13, 1800, 3300, 100, LDO1312, 4, 4, OVER2, 7),
	DA9034_LDO(14, 1800, 3300, 100, LDO1514, 0, 4, OVER3, 0),
	DA9034_LDO(15, 1800, 3300, 100, LDO1514, 4, 4, OVER3, 1),
	DA9034_LDO(5, 3100, 3100, 0, INVAL, 0, 0, OVER3, 7), /* fixed @3.1V */

	/* DA9035 */
	DA9035_DVC(BUCK3, 1800, 2200, 100, 3DTV1, 3, VCC2, 0, OVER3, 3),
};

static inline struct da903x_regulator_info *find_regulator_info(int id)
{
	struct da903x_regulator_info *ri;
	int i;

	for (i = 0; i < ARRAY_SIZE(da903x_regulator_info); i++) {
		ri = &da903x_regulator_info[i];
		if (ri->desc.id == id)
			return ri;
	}
	return NULL;
}

static int da903x_regulator_probe(struct platform_device *pdev)
{
	struct da903x_regulator_info *ri = NULL;
	struct regulator_dev *rdev;
	struct regulator_config config = { };

	ri = find_regulator_info(pdev->id);
	if (ri == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	/* Workaround for the weird LDO12 voltage setting */
	if (ri->desc.id == DA9034_ID_LDO12) {
		ri->desc.ops = &da9034_regulator_ldo12_ops;
		ri->desc.n_voltages = 16;
		ri->desc.linear_ranges = da9034_ldo12_ranges;
		ri->desc.n_linear_ranges = ARRAY_SIZE(da9034_ldo12_ranges);
	}

	if (ri->desc.id == DA9030_ID_LDO14)
		ri->desc.ops = &da9030_regulator_ldo14_ops;

	if (ri->desc.id == DA9030_ID_LDO1 || ri->desc.id == DA9030_ID_LDO15)
		ri->desc.ops = &da9030_regulator_ldo1_15_ops;

	config.dev = &pdev->dev;
	config.init_data = dev_get_platdata(&pdev->dev);
	config.driver_data = ri;

	rdev = regulator_register(&ri->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
				ri->desc.name);
		return PTR_ERR(rdev);
	}

	platform_set_drvdata(pdev, rdev);
	return 0;
}

static int da903x_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);
	return 0;
}

static struct platform_driver da903x_regulator_driver = {
	.driver	= {
		.name	= "da903x-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= da903x_regulator_probe,
	.remove		= da903x_regulator_remove,
};

static int __init da903x_regulator_init(void)
{
	return platform_driver_register(&da903x_regulator_driver);
}
subsys_initcall(da903x_regulator_init);

static void __exit da903x_regulator_exit(void)
{
	platform_driver_unregister(&da903x_regulator_driver);
}
module_exit(da903x_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>"
	      "Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("Regulator Driver for Dialog Semiconductor DA903X PMIC");
MODULE_ALIAS("platform:da903x-regulator");
