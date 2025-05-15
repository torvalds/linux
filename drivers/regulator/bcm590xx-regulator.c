// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom BCM590xx regulator driver
 *
 * Copyright 2014 Linaro Limited
 * Author: Matt Porter <mporter@linaro.org>
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/bcm590xx.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

/* I2C slave 0 registers */
#define BCM590XX_RFLDOPMCTRL1	0x60
#define BCM590XX_CAMLDO1PMCTRL1	0x62
#define BCM590XX_CAMLDO2PMCTRL1	0x64
#define BCM590XX_SIMLDO1PMCTRL1	0x66
#define BCM590XX_SIMLDO2PMCTRL1	0x68
#define BCM590XX_SDLDOPMCTRL1	0x6a
#define BCM590XX_SDXLDOPMCTRL1	0x6c
#define BCM590XX_MMCLDO1PMCTRL1	0x6e
#define BCM590XX_MMCLDO2PMCTRL1	0x70
#define BCM590XX_AUDLDOPMCTRL1	0x72
#define BCM590XX_MICLDOPMCTRL1	0x74
#define BCM590XX_USBLDOPMCTRL1	0x76
#define BCM590XX_VIBLDOPMCTRL1	0x78
#define BCM590XX_IOSR1PMCTRL1	0x7a
#define BCM590XX_IOSR2PMCTRL1	0x7c
#define BCM590XX_CSRPMCTRL1	0x7e
#define BCM590XX_SDSR1PMCTRL1	0x82
#define BCM590XX_SDSR2PMCTRL1	0x86
#define BCM590XX_MSRPMCTRL1	0x8a
#define BCM590XX_VSRPMCTRL1	0x8e
#define BCM590XX_RFLDOCTRL	0x96
#define BCM590XX_CAMLDO1CTRL	0x97
#define BCM590XX_CAMLDO2CTRL	0x98
#define BCM590XX_SIMLDO1CTRL	0x99
#define BCM590XX_SIMLDO2CTRL	0x9a
#define BCM590XX_SDLDOCTRL	0x9b
#define BCM590XX_SDXLDOCTRL	0x9c
#define BCM590XX_MMCLDO1CTRL	0x9d
#define BCM590XX_MMCLDO2CTRL	0x9e
#define BCM590XX_AUDLDOCTRL	0x9f
#define BCM590XX_MICLDOCTRL	0xa0
#define BCM590XX_USBLDOCTRL	0xa1
#define BCM590XX_VIBLDOCTRL	0xa2
#define BCM590XX_CSRVOUT1	0xc0
#define BCM590XX_IOSR1VOUT1	0xc3
#define BCM590XX_IOSR2VOUT1	0xc6
#define BCM590XX_MSRVOUT1	0xc9
#define BCM590XX_SDSR1VOUT1	0xcc
#define BCM590XX_SDSR2VOUT1	0xcf
#define BCM590XX_VSRVOUT1	0xd2

/* I2C slave 1 registers */
#define BCM590XX_GPLDO5PMCTRL1	0x16
#define BCM590XX_GPLDO6PMCTRL1	0x18
#define BCM590XX_GPLDO1CTRL	0x1a
#define BCM590XX_GPLDO2CTRL	0x1b
#define BCM590XX_GPLDO3CTRL	0x1c
#define BCM590XX_GPLDO4CTRL	0x1d
#define BCM590XX_GPLDO5CTRL	0x1e
#define BCM590XX_GPLDO6CTRL	0x1f
#define BCM590XX_OTG_CTRL	0x40
#define BCM590XX_GPLDO1PMCTRL1	0x57
#define BCM590XX_GPLDO2PMCTRL1	0x59
#define BCM590XX_GPLDO3PMCTRL1	0x5b
#define BCM590XX_GPLDO4PMCTRL1	0x5d

#define BCM590XX_REG_ENABLE	BIT(7)
#define BCM590XX_VBUS_ENABLE	BIT(2)
#define BCM590XX_LDO_VSEL_MASK	GENMASK(5, 3)
#define BCM590XX_SR_VSEL_MASK	GENMASK(5, 0)

/*
 * RFLDO to VSR regulators are
 * accessed via I2C slave 0
 */

/* LDO regulator IDs */
#define BCM590XX_REG_RFLDO	0
#define BCM590XX_REG_CAMLDO1	1
#define BCM590XX_REG_CAMLDO2	2
#define BCM590XX_REG_SIMLDO1	3
#define BCM590XX_REG_SIMLDO2	4
#define BCM590XX_REG_SDLDO	5
#define BCM590XX_REG_SDXLDO	6
#define BCM590XX_REG_MMCLDO1	7
#define BCM590XX_REG_MMCLDO2	8
#define BCM590XX_REG_AUDLDO	9
#define BCM590XX_REG_MICLDO	10
#define BCM590XX_REG_USBLDO	11
#define BCM590XX_REG_VIBLDO	12

/* DCDC regulator IDs */
#define BCM590XX_REG_CSR	13
#define BCM590XX_REG_IOSR1	14
#define BCM590XX_REG_IOSR2	15
#define BCM590XX_REG_MSR	16
#define BCM590XX_REG_SDSR1	17
#define BCM590XX_REG_SDSR2	18
#define BCM590XX_REG_VSR	19

/*
 * GPLDO1 to VBUS regulators are
 * accessed via I2C slave 1
 */

#define BCM590XX_REG_GPLDO1	20
#define BCM590XX_REG_GPLDO2	21
#define BCM590XX_REG_GPLDO3	22
#define BCM590XX_REG_GPLDO4	23
#define BCM590XX_REG_GPLDO5	24
#define BCM590XX_REG_GPLDO6	25
#define BCM590XX_REG_VBUS	26

#define BCM590XX_NUM_REGS	27

/* LDO group A: supported voltages in microvolts */
static const unsigned int ldo_a_table[] = {
	1200000, 1800000, 2500000, 2700000, 2800000,
	2900000, 3000000, 3300000,
};

/* LDO group C: supported voltages in microvolts */
static const unsigned int ldo_c_table[] = {
	3100000, 1800000, 2500000, 2700000, 2800000,
	2900000, 3000000, 3300000,
};

/* DCDC group CSR: supported voltages in microvolts */
static const struct linear_range dcdc_csr_ranges[] = {
	REGULATOR_LINEAR_RANGE(860000, 2, 50, 10000),
	REGULATOR_LINEAR_RANGE(1360000, 51, 55, 20000),
	REGULATOR_LINEAR_RANGE(900000, 56, 63, 0),
};

/* DCDC group IOSR1: supported voltages in microvolts */
static const struct linear_range dcdc_iosr1_ranges[] = {
	REGULATOR_LINEAR_RANGE(860000, 2, 51, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 52, 52, 0),
	REGULATOR_LINEAR_RANGE(1800000, 53, 53, 0),
	REGULATOR_LINEAR_RANGE(900000, 54, 63, 0),
};

/* DCDC group SDSR1: supported voltages in microvolts */
static const struct linear_range dcdc_sdsr1_ranges[] = {
	REGULATOR_LINEAR_RANGE(860000, 2, 50, 10000),
	REGULATOR_LINEAR_RANGE(1340000, 51, 51, 0),
	REGULATOR_LINEAR_RANGE(900000, 52, 63, 0),
};

enum bcm590xx_reg_type {
	BCM590XX_REG_TYPE_LDO,
	BCM590XX_REG_TYPE_GPLDO,
	BCM590XX_REG_TYPE_SR,
	BCM590XX_REG_TYPE_VBUS
};

struct bcm590xx_reg_data {
	enum bcm590xx_reg_type type;
	enum bcm590xx_regmap_type regmap;
	const struct regulator_desc desc;
};

struct bcm590xx_reg {
	struct bcm590xx *mfd;
	unsigned int n_regulators;
	const struct bcm590xx_reg_data *regs;
};

static const struct regulator_ops bcm590xx_ops_ldo = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_table,
	.map_voltage		= regulator_map_voltage_iterate,
};

static const struct regulator_ops bcm590xx_ops_dcdc = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
};

static const struct regulator_ops bcm590xx_ops_vbus = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
};

#define BCM590XX_REG_DESC(_name, _name_lower)				\
	.id = BCM590XX_REG_##_name,					\
	.name = #_name_lower,						\
	.of_match = of_match_ptr(#_name_lower),				\
	.regulators_node = of_match_ptr("regulators"),			\
	.type = REGULATOR_VOLTAGE,					\
	.owner = THIS_MODULE						\

#define BCM590XX_LDO_DESC(_name, _name_lower, _table)			\
	BCM590XX_REG_DESC(_name, _name_lower),				\
	.ops = &bcm590xx_ops_ldo,					\
	.n_voltages = ARRAY_SIZE(_table),				\
	.volt_table = _table,						\
	.vsel_reg = BCM590XX_##_name##CTRL,				\
	.vsel_mask = BCM590XX_LDO_VSEL_MASK,				\
	.enable_reg = BCM590XX_##_name##PMCTRL1,			\
	.enable_mask = BCM590XX_REG_ENABLE,				\
	.enable_is_inverted = true

#define BCM590XX_SR_DESC(_name, _name_lower, _ranges)			\
	BCM590XX_REG_DESC(_name, _name_lower),				\
	.ops = &bcm590xx_ops_dcdc,					\
	.n_voltages = 64,						\
	.linear_ranges = _ranges,					\
	.n_linear_ranges = ARRAY_SIZE(_ranges),				\
	.vsel_reg = BCM590XX_##_name##VOUT1,				\
	.vsel_mask = BCM590XX_SR_VSEL_MASK,				\
	.enable_reg = BCM590XX_##_name##PMCTRL1,			\
	.enable_mask = BCM590XX_REG_ENABLE,				\
	.enable_is_inverted = true

static const struct bcm590xx_reg_data bcm590xx_regs[BCM590XX_NUM_REGS] = {
	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(RFLDO, rfldo, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(CAMLDO1, camldo1, ldo_c_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(CAMLDO2, camldo2, ldo_c_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(SIMLDO1, simldo1, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(SIMLDO2, simldo2, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(SDLDO, sdldo, ldo_c_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(SDXLDO, sdxldo, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(MMCLDO1, mmcldo1, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(MMCLDO2, mmcldo2, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(AUDLDO, audldo, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(MICLDO, micldo, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(USBLDO, usbldo, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_LDO,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_LDO_DESC(VIBLDO, vibldo, ldo_c_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(CSR, csr, dcdc_csr_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(IOSR1, iosr1, dcdc_iosr1_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(IOSR2, iosr2, dcdc_iosr1_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(MSR, msr, dcdc_iosr1_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(SDSR1, sdsr1, dcdc_sdsr1_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(SDSR2, sdsr2, dcdc_iosr1_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_SR,
		.regmap = BCM590XX_REGMAP_PRI,
		.desc = {
			BCM590XX_SR_DESC(VSR, vsr, dcdc_iosr1_ranges),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_GPLDO,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_LDO_DESC(GPLDO1, gpldo1, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_GPLDO,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_LDO_DESC(GPLDO2, gpldo2, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_GPLDO,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_LDO_DESC(GPLDO3, gpldo3, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_GPLDO,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_LDO_DESC(GPLDO4, gpldo4, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_GPLDO,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_LDO_DESC(GPLDO5, gpldo5, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_GPLDO,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_LDO_DESC(GPLDO6, gpldo6, ldo_a_table),
		},
	},

	{
		.type = BCM590XX_REG_TYPE_VBUS,
		.regmap = BCM590XX_REGMAP_SEC,
		.desc = {
			BCM590XX_REG_DESC(VBUS, vbus),
			.ops = &bcm590xx_ops_vbus,
			.n_voltages = 1,
			.fixed_uV = 5000000,
			.enable_reg = BCM590XX_OTG_CTRL,
			.enable_mask = BCM590XX_VBUS_ENABLE,
		},
	},
};

static int bcm590xx_probe(struct platform_device *pdev)
{
	struct bcm590xx *bcm590xx = dev_get_drvdata(pdev->dev.parent);
	struct bcm590xx_reg *pmu;
	const struct bcm590xx_reg_data *info;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	unsigned int i;

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->mfd = bcm590xx;
	pmu->n_regulators = BCM590XX_NUM_REGS;
	pmu->regs = bcm590xx_regs;

	platform_set_drvdata(pdev, pmu);

	/* Register the regulators */
	for (i = 0; i < pmu->n_regulators; i++) {
		info = &pmu->regs[i];

		config.dev = bcm590xx->dev;
		config.driver_data = pmu;

		switch (info->regmap) {
		case BCM590XX_REGMAP_PRI:
			config.regmap = bcm590xx->regmap_pri;
			break;
		case BCM590XX_REGMAP_SEC:
			config.regmap = bcm590xx->regmap_sec;
			break;
		default:
			dev_err(bcm590xx->dev,
				"invalid regmap for %s regulator; this is a driver bug\n",
				pdev->name);
			return -EINVAL;
		}

		rdev = devm_regulator_register(&pdev->dev, &info->desc,
					       &config);
		if (IS_ERR(rdev))
			return dev_err_probe(bcm590xx->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     pdev->name);
	}

	return 0;
}

static struct platform_driver bcm590xx_regulator_driver = {
	.driver = {
		.name = "bcm590xx-vregs",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = bcm590xx_probe,
};
module_platform_driver(bcm590xx_regulator_driver);

MODULE_AUTHOR("Matt Porter <mporter@linaro.org>");
MODULE_DESCRIPTION("BCM590xx voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bcm590xx-vregs");
