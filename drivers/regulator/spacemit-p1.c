// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for regulators found in the SpacemiT P1 PMIC
 *
 * Copyright (C) 2025 by RISCstar Solutions Corporation.  All rights reserved.
 * Derived from code from SpacemiT.
 *	Copyright (c) 2023, SPACEMIT Co., Ltd
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>

#define MOD_NAME	"spacemit-p1-regulator"

enum p1_regulator_id {
	P1_BUCK1,
	P1_BUCK2,
	P1_BUCK3,
	P1_BUCK4,
	P1_BUCK5,
	P1_BUCK6,

	P1_ALDO1,
	P1_ALDO2,
	P1_ALDO3,
	P1_ALDO4,

	P1_DLDO1,
	P1_DLDO2,
	P1_DLDO3,
	P1_DLDO4,
	P1_DLDO5,
	P1_DLDO6,
	P1_DLDO7,
};

static const struct regulator_ops p1_regulator_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel   = regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

/* Selector value 255 can be used to disable the buck converter on sleep */
static const struct linear_range p1_buck_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 170, 5000),
	REGULATOR_LINEAR_RANGE(1375000, 171, 254, 25000),
};

/* Selector value 0 can be used for suspend */
static const struct linear_range p1_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 11, 127, 25000),
};

/* These define the voltage selector field for buck and LDO regulators */
#define BUCK_MASK		GENMASK(7, 0)
#define LDO_MASK		GENMASK(6, 0)

#define P1_ID(_TYPE, _n)	P1_ ## _TYPE ## _n
#define P1_ENABLE_REG(_off, _n)	((_off) + 3 * ((_n) - 1))

#define P1_REG_DESC(_TYPE, _type, _n, _s, _off, _mask, _nv, _ranges)	\
	{								\
		.name			= #_type #_n,			\
		.supply_name		= _s,				\
		.of_match		= of_match_ptr(#_type #_n),	\
		.regulators_node	= of_match_ptr("regulators"),	\
		.id			= P1_ID(_TYPE, _n),		\
		.n_voltages		= _nv,				\
		.ops			= &p1_regulator_ops,		\
		.owner			= THIS_MODULE,			\
		.linear_ranges		= _ranges,			\
		.n_linear_ranges	= ARRAY_SIZE(_ranges),		\
		.vsel_reg		= P1_ENABLE_REG(_off, _n) + 1,	\
		.vsel_mask		= _mask,			\
		.enable_reg		= P1_ENABLE_REG(_off, _n),	\
		.enable_mask		= BIT(0),			\
	}

#define P1_BUCK_DESC(_n) \
	P1_REG_DESC(BUCK, buck, _n, "vcc", 0x47, BUCK_MASK, 254, p1_buck_ranges)

#define P1_ALDO_DESC(_n) \
	P1_REG_DESC(ALDO, aldo, _n, "vcc", 0x5b, LDO_MASK, 117, p1_ldo_ranges)

#define P1_DLDO_DESC(_n) \
	P1_REG_DESC(DLDO, dldo, _n, "buck5", 0x67, LDO_MASK, 117, p1_ldo_ranges)

static const struct regulator_desc p1_regulator_desc[] = {
	P1_BUCK_DESC(1),
	P1_BUCK_DESC(2),
	P1_BUCK_DESC(3),
	P1_BUCK_DESC(4),
	P1_BUCK_DESC(5),
	P1_BUCK_DESC(6),

	P1_ALDO_DESC(1),
	P1_ALDO_DESC(2),
	P1_ALDO_DESC(3),
	P1_ALDO_DESC(4),

	P1_DLDO_DESC(1),
	P1_DLDO_DESC(2),
	P1_DLDO_DESC(3),
	P1_DLDO_DESC(4),
	P1_DLDO_DESC(5),
	P1_DLDO_DESC(6),
	P1_DLDO_DESC(7),
};

static int p1_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct device *dev = &pdev->dev;
	u32 i;

	/*
	 * The parent device (PMIC) owns the regmap.  Since we don't
	 * provide one in the config structure, that one will be used.
	 */
	config.dev = dev->parent;

	for (i = 0; i < ARRAY_SIZE(p1_regulator_desc); i++) {
		const struct regulator_desc *desc = &p1_regulator_desc[i];
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "error registering regulator %s\n",
					     desc->name);
	}

	return 0;
}

static struct platform_driver p1_regulator_driver = {
	.probe = p1_regulator_probe,
	.driver = {
		.name = MOD_NAME,
	},
};

module_platform_driver(p1_regulator_driver);

MODULE_DESCRIPTION("SpacemiT P1 regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MOD_NAME);
