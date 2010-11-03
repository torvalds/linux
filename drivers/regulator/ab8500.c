/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License Terms: GNU General Public License v2
 *
 * Author: Sundar Iyer <sundar.iyer@stericsson.com> for ST-Ericsson
 *
 * AB8500 peripheral regulators
 *
 * AB8500 supports the following regulators,
 * LDOs - VAUDIO, VANAMIC2/2, VDIGMIC, VINTCORE12, VTVOUT,
 *        VAUX1/2/3, VANA
 *
 * for DB8500 cut 1.0 and previous versions of the silicon, all accesses
 * to registers are through the DB8500 SPI. In cut 1.1 onwards, these
 * accesses are through the DB8500 PRCMU I2C
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/ab8500.h>

/**
 * struct ab8500_regulator_info - ab8500 regulator information
 * @desc: regulator description
 * @ab8500: ab8500 parent
 * @regulator_dev: regulator device
 * @max_uV: maximum voltage (for variable voltage supplies)
 * @min_uV: minimum voltage (for variable voltage supplies)
 * @fixed_uV: typical voltage (for fixed voltage supplies)
 * @update_bank: bank to control on/off
 * @update_reg: register to control on/off
 * @mask: mask to enable/disable regulator
 * @enable: bits to enable the regulator in normal(high power) mode
 * @voltage_bank: bank to control regulator voltage
 * @voltage_reg: register to control regulator voltage
 * @voltage_mask: mask to control regulator voltage
 * @supported_voltages: supported voltage table
 * @voltages_len: number of supported voltages for the regulator
 */
struct ab8500_regulator_info {
	struct device		*dev;
	struct regulator_desc	desc;
	struct ab8500		*ab8500;
	struct regulator_dev	*regulator;
	int max_uV;
	int min_uV;
	int fixed_uV;
	u8 update_bank;
	u8 update_reg;
	u8 mask;
	u8 enable;
	u8 voltage_bank;
	u8 voltage_reg;
	u8 voltage_mask;
	int const *supported_voltages;
	int voltages_len;
};

/* voltage tables for the vauxn/vintcore supplies */
static const int ldo_vauxn_voltages[] = {
	1100000,
	1200000,
	1300000,
	1400000,
	1500000,
	1800000,
	1850000,
	1900000,
	2500000,
	2650000,
	2700000,
	2750000,
	2800000,
	2900000,
	3000000,
	3300000,
};

static const int ldo_vintcore_voltages[] = {
	1200000,
	1225000,
	1250000,
	1275000,
	1300000,
	1325000,
	1350000,
};

static int ab8500_regulator_enable(struct regulator_dev *rdev)
{
	int regulator_id, ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg, info->mask, info->enable);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
			"couldn't set enable bits for regulator\n");
	return ret;
}

static int ab8500_regulator_disable(struct regulator_dev *rdev)
{
	int regulator_id, ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->update_bank, info->update_reg, info->mask, 0x0);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
			"couldn't set disable bits for regulator\n");
	return ret;
}

static int ab8500_regulator_is_enabled(struct regulator_dev *rdev)
{
	int regulator_id, ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 value;

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	ret = abx500_get_register_interruptible(info->dev,
		info->update_bank, info->update_reg, &value);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read 0x%x register\n", info->update_reg);
		return ret;
	}

	if (value & info->mask)
		return true;
	else
		return false;
}

static int ab8500_list_voltage(struct regulator_dev *rdev, unsigned selector)
{
	int regulator_id;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	/* return the uV for the fixed regulators */
	if (info->fixed_uV)
		return info->fixed_uV;

	if (selector >= info->voltages_len)
		return -EINVAL;

	return info->supported_voltages[selector];
}

static int ab8500_regulator_get_voltage(struct regulator_dev *rdev)
{
	int regulator_id, ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	u8 value;

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	ret = abx500_get_register_interruptible(info->dev, info->voltage_bank,
		info->voltage_reg, &value);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
			"couldn't read voltage reg for regulator\n");
		return ret;
	}

	/* vintcore has a different layout */
	value &= info->voltage_mask;
	if (regulator_id == AB8500_LDO_INTCORE)
		ret = info->supported_voltages[value >> 0x3];
	else
		ret = info->supported_voltages[value];

	return ret;
}

static int ab8500_get_best_voltage_index(struct regulator_dev *rdev,
		int min_uV, int max_uV)
{
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);
	int i;

	/* check the supported voltage */
	for (i = 0; i < info->voltages_len; i++) {
		if ((info->supported_voltages[i] >= min_uV) &&
		    (info->supported_voltages[i] <= max_uV))
			return i;
	}

	return -EINVAL;
}

static int ab8500_regulator_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV)
{
	int regulator_id, ret;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	/* get the appropriate voltages within the range */
	ret = ab8500_get_best_voltage_index(rdev, min_uV, max_uV);
	if (ret < 0) {
		dev_err(rdev_get_dev(rdev),
				"couldn't get best voltage for regulator\n");
		return ret;
	}

	/* set the registers for the request */
	ret = abx500_mask_and_set_register_interruptible(info->dev,
		info->voltage_bank, info->voltage_reg,
		info->voltage_mask, (u8)ret);
	if (ret < 0)
		dev_err(rdev_get_dev(rdev),
		"couldn't set voltage reg for regulator\n");

	return ret;
}

static struct regulator_ops ab8500_regulator_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.is_enabled	= ab8500_regulator_is_enabled,
	.get_voltage	= ab8500_regulator_get_voltage,
	.set_voltage	= ab8500_regulator_set_voltage,
	.list_voltage	= ab8500_list_voltage,
};

static int ab8500_fixed_get_voltage(struct regulator_dev *rdev)
{
	int regulator_id;
	struct ab8500_regulator_info *info = rdev_get_drvdata(rdev);

	regulator_id = rdev_get_id(rdev);
	if (regulator_id >= AB8500_NUM_REGULATORS)
		return -EINVAL;

	return info->fixed_uV;
}

static struct regulator_ops ab8500_ldo_fixed_ops = {
	.enable		= ab8500_regulator_enable,
	.disable	= ab8500_regulator_disable,
	.is_enabled	= ab8500_regulator_is_enabled,
	.get_voltage	= ab8500_fixed_get_voltage,
	.list_voltage	= ab8500_list_voltage,
};

#define AB8500_LDO(_id, min, max, bank, reg, reg_mask,		\
		reg_enable, volt_bank, volt_reg, volt_mask,	\
		voltages, len_volts)				\
{								\
	.desc	= {						\
		.name	= "LDO-" #_id,				\
		.ops	= &ab8500_regulator_ops,		\
		.type	= REGULATOR_VOLTAGE,			\
		.id	= AB8500_LDO_##_id,			\
		.owner	= THIS_MODULE,				\
	},							\
	.min_uV		= (min) * 1000,				\
	.max_uV		= (max) * 1000,				\
	.update_bank	= bank,					\
	.update_reg	= reg,					\
	.mask		= reg_mask,				\
	.enable		= reg_enable,				\
	.voltage_bank	= volt_bank,				\
	.voltage_reg	= volt_reg,				\
	.voltage_mask	= volt_mask,				\
	.supported_voltages = voltages,				\
	.voltages_len	= len_volts,				\
	.fixed_uV	= 0,					\
}

#define AB8500_FIXED_LDO(_id, fixed, bank, reg,		\
			reg_mask, reg_enable)		\
{							\
	.desc	= {					\
		.name	= "LDO-" #_id,			\
		.ops	= &ab8500_ldo_fixed_ops,	\
		.type	= REGULATOR_VOLTAGE,		\
		.id	= AB8500_LDO_##_id,		\
		.owner	= THIS_MODULE,			\
	},						\
	.fixed_uV	= fixed * 1000,			\
	.update_bank	= bank,				\
	.update_reg	= reg,				\
	.mask		= reg_mask,			\
	.enable		= reg_enable,			\
}

static struct ab8500_regulator_info ab8500_regulator_info[] = {
	/*
	 * Variable Voltage LDOs
	 * name, min uV, max uV, ctrl bank, ctrl reg, reg mask, enable mask,
	 *      volt ctrl bank, volt ctrl reg, volt ctrl mask, volt table,
	 *      num supported volts
	 */
	AB8500_LDO(AUX1, 1100, 3300, 0x04, 0x09, 0x3, 0x1, 0x04, 0x1f, 0xf,
			ldo_vauxn_voltages, ARRAY_SIZE(ldo_vauxn_voltages)),
	AB8500_LDO(AUX2, 1100, 3300, 0x04, 0x09, 0xc, 0x4, 0x04, 0x20, 0xf,
			ldo_vauxn_voltages, ARRAY_SIZE(ldo_vauxn_voltages)),
	AB8500_LDO(AUX3, 1100, 3300, 0x04, 0x0a, 0x3, 0x1, 0x04, 0x21, 0xf,
			ldo_vauxn_voltages, ARRAY_SIZE(ldo_vauxn_voltages)),
	AB8500_LDO(INTCORE, 1100, 3300, 0x03, 0x80, 0x4, 0x4, 0x03, 0x80, 0x38,
		ldo_vintcore_voltages, ARRAY_SIZE(ldo_vintcore_voltages)),

	/*
	 * Fixed Voltage LDOs
	 *		 name,	o/p uV, ctrl bank, ctrl reg, enable, disable
	 */
	AB8500_FIXED_LDO(TVOUT,	  2000, 0x03,      0x80,     0x2,    0x2),
	AB8500_FIXED_LDO(AUDIO,   2000, 0x03,      0x83,     0x2,    0x2),
	AB8500_FIXED_LDO(ANAMIC1, 2050, 0x03,      0x83,     0x4,    0x4),
	AB8500_FIXED_LDO(ANAMIC2, 2050, 0x03,      0x83,     0x8,    0x8),
	AB8500_FIXED_LDO(DMIC,    1800, 0x03,      0x83,     0x10,   0x10),
	AB8500_FIXED_LDO(ANA,     1200, 0x03,      0x83,     0xc,    0x4),
};

static inline struct ab8500_regulator_info *find_regulator_info(int id)
{
	struct ab8500_regulator_info *info;
	int i;

	for (i = 0; i < ARRAY_SIZE(ab8500_regulator_info); i++) {
		info = &ab8500_regulator_info[i];
		if (info->desc.id == id)
			return info;
	}
	return NULL;
}

static __devinit int ab8500_regulator_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	struct ab8500_platform_data *pdata;
	int i, err;

	if (!ab8500) {
		dev_err(&pdev->dev, "null mfd parent\n");
		return -EINVAL;
	}
	pdata = dev_get_platdata(ab8500->dev);

	/* register all regulators */
	for (i = 0; i < ARRAY_SIZE(ab8500_regulator_info); i++) {
		struct ab8500_regulator_info *info = NULL;

		/* assign per-regulator data */
		info = &ab8500_regulator_info[i];
		info->dev = &pdev->dev;
		info->ab8500 = ab8500;

		info->regulator = regulator_register(&info->desc, &pdev->dev,
				pdata->regulator[i], info);
		if (IS_ERR(info->regulator)) {
			err = PTR_ERR(info->regulator);
			dev_err(&pdev->dev, "failed to register regulator %s\n",
					info->desc.name);
			/* when we fail, un-register all earlier regulators */
			while (--i >= 0) {
				info = &ab8500_regulator_info[i];
				regulator_unregister(info->regulator);
			}
			return err;
		}
	}

	return 0;
}

static __devexit int ab8500_regulator_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ab8500_regulator_info); i++) {
		struct ab8500_regulator_info *info = NULL;
		info = &ab8500_regulator_info[i];
		regulator_unregister(info->regulator);
	}

	return 0;
}

static struct platform_driver ab8500_regulator_driver = {
	.probe = ab8500_regulator_probe,
	.remove = __devexit_p(ab8500_regulator_remove),
	.driver         = {
		.name   = "ab8500-regulator",
		.owner  = THIS_MODULE,
	},
};

static int __init ab8500_regulator_init(void)
{
	int ret;

	ret = platform_driver_register(&ab8500_regulator_driver);
	if (ret != 0)
		pr_err("Failed to register ab8500 regulator: %d\n", ret);

	return ret;
}
subsys_initcall(ab8500_regulator_init);

static void __exit ab8500_regulator_exit(void)
{
	platform_driver_unregister(&ab8500_regulator_driver);
}
module_exit(ab8500_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sundar Iyer <sundar.iyer@stericsson.com>");
MODULE_DESCRIPTION("Regulator Driver for ST-Ericsson AB8500 Mixed-Sig PMIC");
MODULE_ALIAS("platform:ab8500-regulator");
