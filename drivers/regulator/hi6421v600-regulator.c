// SPDX-License-Identifier: GPL-2.0
//
// Device driver for regulators in Hisi IC
//
// Copyright (c) 2013 Linaro Ltd.
// Copyright (c) 2011 HiSilicon Ltd.
// Copyright (c) 2020-2021 Huawei Technologies Co., Ltd.
//
// Guodong Xu <guodong.xu@linaro.org>

#include <linux/delay.h>
#include <linux/mfd/hi6421-spmi-pmic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/spmi.h>

struct hi6421_spmi_reg_priv {
	/* Serialize regulator enable logic */
	struct mutex enable_mutex;
};

struct hi6421_spmi_reg_info {
	struct regulator_desc	desc;
	u8			eco_mode_mask;
	u32			eco_uA;
};

static const unsigned int ldo3_voltages[] = {
	1500000, 1550000, 1600000, 1650000,
	1700000, 1725000, 1750000, 1775000,
	1800000, 1825000, 1850000, 1875000,
	1900000, 1925000, 1950000, 2000000
};

static const unsigned int ldo4_voltages[] = {
	1725000, 1750000, 1775000, 1800000,
	1825000, 1850000, 1875000, 1900000
};

static const unsigned int ldo9_voltages[] = {
	1750000, 1800000, 1825000, 2800000,
	2850000, 2950000, 3000000, 3300000
};

static const unsigned int ldo15_voltages[] = {
	1800000, 1850000, 2400000, 2600000,
	2700000, 2850000, 2950000, 3000000
};

static const unsigned int ldo17_voltages[] = {
	2500000, 2600000, 2700000, 2800000,
	3000000, 3100000, 3200000, 3300000
};

static const unsigned int ldo34_voltages[] = {
	2600000, 2700000, 2800000, 2900000,
	3000000, 3100000, 3200000, 3300000
};

/**
 * HI6421V600_LDO() - specify a LDO power line
 * @_id: LDO id name string
 * @vtable: voltage table
 * @ereg: enable register
 * @emask: enable mask
 * @vreg: voltage select register
 * @odelay: off/on delay time in uS
 * @etime: enable time in uS
 * @ecomask: eco mode mask
 * @ecoamp: eco mode load uppler limit in uA
 */
#define HI6421V600_LDO(_id, vtable, ereg, emask, vreg,			       \
		       odelay, etime, ecomask, ecoamp)			       \
	[HI6421V600_##_id] = {						       \
		.desc = {						       \
			.name		= #_id,				       \
			.of_match        = of_match_ptr(#_id),		       \
			.regulators_node = of_match_ptr("regulators"),	       \
			.ops		= &hi6421_spmi_ldo_rops,	       \
			.type		= REGULATOR_VOLTAGE,		       \
			.id		= HI6421V600_##_id,		       \
			.owner		= THIS_MODULE,			       \
			.volt_table	= vtable,			       \
			.n_voltages	= ARRAY_SIZE(vtable),		       \
			.vsel_mask	= ARRAY_SIZE(vtable) - 1,	       \
			.vsel_reg	= vreg,				       \
			.enable_reg	= ereg,				       \
			.enable_mask	= emask,			       \
			.enable_time	= etime,			       \
			.ramp_delay	= etime,			       \
			.off_on_delay	= odelay,			       \
		},							       \
		.eco_mode_mask		= ecomask,			       \
		.eco_uA			= ecoamp,			       \
	}

static int hi6421_spmi_regulator_enable(struct regulator_dev *rdev)
{
	struct hi6421_spmi_reg_priv *priv = rdev_get_drvdata(rdev);
	int ret;

	/* cannot enable more than one regulator at one time */
	mutex_lock(&priv->enable_mutex);

	ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				 rdev->desc->enable_mask,
				 rdev->desc->enable_mask);

	/* Avoid powering up multiple devices at the same time */
	usleep_range(rdev->desc->off_on_delay, rdev->desc->off_on_delay + 60);

	mutex_unlock(&priv->enable_mutex);

	return ret;
}

static unsigned int hi6421_spmi_regulator_get_mode(struct regulator_dev *rdev)
{
	struct hi6421_spmi_reg_info *sreg;
	unsigned int reg_val;

	sreg = container_of(rdev->desc, struct hi6421_spmi_reg_info, desc);
	regmap_read(rdev->regmap, rdev->desc->enable_reg, &reg_val);

	if (reg_val & sreg->eco_mode_mask)
		return REGULATOR_MODE_IDLE;

	return REGULATOR_MODE_NORMAL;
}

static int hi6421_spmi_regulator_set_mode(struct regulator_dev *rdev,
					  unsigned int mode)
{
	struct hi6421_spmi_reg_info *sreg;
	unsigned int val;

	sreg = container_of(rdev->desc, struct hi6421_spmi_reg_info, desc);
	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_IDLE:
		if (!sreg->eco_mode_mask)
			return -EINVAL;

		val = sreg->eco_mode_mask;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  sreg->eco_mode_mask, val);
}

static unsigned int
hi6421_spmi_regulator_get_optimum_mode(struct regulator_dev *rdev,
				       int input_uV, int output_uV,
				       int load_uA)
{
	struct hi6421_spmi_reg_info *sreg;

	sreg = container_of(rdev->desc, struct hi6421_spmi_reg_info, desc);

	if (!sreg->eco_uA || ((unsigned int)load_uA > sreg->eco_uA))
		return REGULATOR_MODE_NORMAL;

	return REGULATOR_MODE_IDLE;
}

static const struct regulator_ops hi6421_spmi_ldo_rops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable = hi6421_spmi_regulator_enable,
	.disable = regulator_disable_regmap,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_mode = hi6421_spmi_regulator_get_mode,
	.set_mode = hi6421_spmi_regulator_set_mode,
	.get_optimum_mode = hi6421_spmi_regulator_get_optimum_mode,
};

/* HI6421v600 regulators with known registers */
enum hi6421_spmi_regulator_id {
	HI6421V600_LDO3,
	HI6421V600_LDO4,
	HI6421V600_LDO9,
	HI6421V600_LDO15,
	HI6421V600_LDO16,
	HI6421V600_LDO17,
	HI6421V600_LDO33,
	HI6421V600_LDO34,
};

static struct hi6421_spmi_reg_info regulator_info[] = {
	HI6421V600_LDO(LDO3, ldo3_voltages,
		       0x16, 0x01, 0x51,
		       20000, 120,
		       0, 0),
	HI6421V600_LDO(LDO4, ldo4_voltages,
		       0x17, 0x01, 0x52,
		       20000, 120,
		       0x10, 10000),
	HI6421V600_LDO(LDO9, ldo9_voltages,
		       0x1c, 0x01, 0x57,
		       20000, 360,
		       0x10, 10000),
	HI6421V600_LDO(LDO15, ldo15_voltages,
		       0x21, 0x01, 0x5c,
		       20000, 360,
		       0x10, 10000),
	HI6421V600_LDO(LDO16, ldo15_voltages,
		       0x22, 0x01, 0x5d,
		       20000, 360,
		       0x10, 10000),
	HI6421V600_LDO(LDO17, ldo17_voltages,
		       0x23, 0x01, 0x5e,
		       20000, 120,
		       0x10, 10000),
	HI6421V600_LDO(LDO33, ldo17_voltages,
		       0x32, 0x01, 0x6d,
		       20000, 120,
		       0, 0),
	HI6421V600_LDO(LDO34, ldo34_voltages,
		       0x33, 0x01, 0x6e,
		       20000, 120,
		       0, 0),
};

static int hi6421_spmi_regulator_probe(struct platform_device *pdev)
{
	struct device *pmic_dev = pdev->dev.parent;
	struct regulator_config config = { };
	struct hi6421_spmi_reg_priv *priv;
	struct hi6421_spmi_reg_info *info;
	struct device *dev = &pdev->dev;
	struct hi6421_spmi_pmic *pmic;
	struct regulator_dev *rdev;
	int i;

	/*
	 * This driver is meant to be called by hi6421-spmi-core,
	 * which should first set drvdata. If this doesn't happen, hit
	 * a warn on and return.
	 */
	pmic = dev_get_drvdata(pmic_dev);
	if (WARN_ON(!pmic))
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->enable_mutex);

	for (i = 0; i < ARRAY_SIZE(regulator_info); i++) {
		info = &regulator_info[i];

		config.dev = pdev->dev.parent;
		config.driver_data = priv;
		config.regmap = pmic->regmap;

		rdev = devm_regulator_register(dev, &info->desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(dev, "failed to register %s\n",
				info->desc.name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id hi6421_spmi_regulator_table[] = {
	{ .name = "hi6421v600-regulator" },
	{},
};
MODULE_DEVICE_TABLE(platform, hi6421_spmi_regulator_table);

static struct platform_driver hi6421_spmi_regulator_driver = {
	.id_table = hi6421_spmi_regulator_table,
	.driver = {
		.name = "hi6421v600-regulator",
	},
	.probe	= hi6421_spmi_regulator_probe,
};
module_platform_driver(hi6421_spmi_regulator_driver);

MODULE_DESCRIPTION("Hi6421v600 SPMI regulator driver");
MODULE_LICENSE("GPL v2");

