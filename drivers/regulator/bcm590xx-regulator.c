/*
 * Broadcom BCM590xx regulator driver
 *
 * Copyright 2014 Linaro Limited
 * Author: Matt Porter <mporter@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
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
#define BCM590XX_IOSR1PMCTRL1	0x7a
#define BCM590XX_IOSR2PMCTRL1	0x7c
#define BCM590XX_CSRPMCTRL1	0x7e
#define BCM590XX_SDSR1PMCTRL1	0x82
#define BCM590XX_SDSR2PMCTRL1	0x86
#define BCM590XX_MSRPMCTRL1	0x8a
#define BCM590XX_VSRPMCTRL1	0x8e
#define BCM590XX_RFLDOCTRL	0x96
#define BCM590XX_CSRVOUT1	0xc0

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

#define BCM590XX_REG_IS_LDO(n)	(n < BCM590XX_REG_CSR)
#define BCM590XX_REG_IS_GPLDO(n) \
	((n > BCM590XX_REG_VSR) && (n < BCM590XX_REG_VBUS))
#define BCM590XX_REG_IS_VBUS(n)	(n == BCM590XX_REG_VBUS)

struct bcm590xx_board {
	struct regulator_init_data *bcm590xx_pmu_init_data[BCM590XX_NUM_REGS];
};

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

static const unsigned int ldo_vbus[] = {
	5000000,
};

/* DCDC group CSR: supported voltages in microvolts */
static const struct regulator_linear_range dcdc_csr_ranges[] = {
	REGULATOR_LINEAR_RANGE(860000, 2, 50, 10000),
	REGULATOR_LINEAR_RANGE(1360000, 51, 55, 20000),
	REGULATOR_LINEAR_RANGE(900000, 56, 63, 0),
};

/* DCDC group IOSR1: supported voltages in microvolts */
static const struct regulator_linear_range dcdc_iosr1_ranges[] = {
	REGULATOR_LINEAR_RANGE(860000, 2, 51, 10000),
	REGULATOR_LINEAR_RANGE(1500000, 52, 52, 0),
	REGULATOR_LINEAR_RANGE(1800000, 53, 53, 0),
	REGULATOR_LINEAR_RANGE(900000, 54, 63, 0),
};

/* DCDC group SDSR1: supported voltages in microvolts */
static const struct regulator_linear_range dcdc_sdsr1_ranges[] = {
	REGULATOR_LINEAR_RANGE(860000, 2, 50, 10000),
	REGULATOR_LINEAR_RANGE(1340000, 51, 51, 0),
	REGULATOR_LINEAR_RANGE(900000, 52, 63, 0),
};

struct bcm590xx_info {
	const char *name;
	const char *vin_name;
	u8 n_voltages;
	const unsigned int *volt_table;
	u8 n_linear_ranges;
	const struct regulator_linear_range *linear_ranges;
};

#define BCM590XX_REG_TABLE(_name, _table) \
	{ \
		.name = #_name, \
		.n_voltages = ARRAY_SIZE(_table), \
		.volt_table = _table, \
	}

#define BCM590XX_REG_RANGES(_name, _ranges) \
	{ \
		.name = #_name, \
		.n_voltages = 64, \
		.n_linear_ranges = ARRAY_SIZE(_ranges), \
		.linear_ranges = _ranges, \
	}

static struct bcm590xx_info bcm590xx_regs[] = {
	BCM590XX_REG_TABLE(rfldo, ldo_a_table),
	BCM590XX_REG_TABLE(camldo1, ldo_c_table),
	BCM590XX_REG_TABLE(camldo2, ldo_c_table),
	BCM590XX_REG_TABLE(simldo1, ldo_a_table),
	BCM590XX_REG_TABLE(simldo2, ldo_a_table),
	BCM590XX_REG_TABLE(sdldo, ldo_c_table),
	BCM590XX_REG_TABLE(sdxldo, ldo_a_table),
	BCM590XX_REG_TABLE(mmcldo1, ldo_a_table),
	BCM590XX_REG_TABLE(mmcldo2, ldo_a_table),
	BCM590XX_REG_TABLE(audldo, ldo_a_table),
	BCM590XX_REG_TABLE(micldo, ldo_a_table),
	BCM590XX_REG_TABLE(usbldo, ldo_a_table),
	BCM590XX_REG_TABLE(vibldo, ldo_c_table),
	BCM590XX_REG_RANGES(csr, dcdc_csr_ranges),
	BCM590XX_REG_RANGES(iosr1, dcdc_iosr1_ranges),
	BCM590XX_REG_RANGES(iosr2, dcdc_iosr1_ranges),
	BCM590XX_REG_RANGES(msr, dcdc_iosr1_ranges),
	BCM590XX_REG_RANGES(sdsr1, dcdc_sdsr1_ranges),
	BCM590XX_REG_RANGES(sdsr2, dcdc_iosr1_ranges),
	BCM590XX_REG_RANGES(vsr, dcdc_iosr1_ranges),
	BCM590XX_REG_TABLE(gpldo1, ldo_a_table),
	BCM590XX_REG_TABLE(gpldo2, ldo_a_table),
	BCM590XX_REG_TABLE(gpldo3, ldo_a_table),
	BCM590XX_REG_TABLE(gpldo4, ldo_a_table),
	BCM590XX_REG_TABLE(gpldo5, ldo_a_table),
	BCM590XX_REG_TABLE(gpldo6, ldo_a_table),
	BCM590XX_REG_TABLE(vbus, ldo_vbus),
};

struct bcm590xx_reg {
	struct regulator_desc *desc;
	struct bcm590xx *mfd;
};

static int bcm590xx_get_vsel_register(int id)
{
	if (BCM590XX_REG_IS_LDO(id))
		return BCM590XX_RFLDOCTRL + id;
	else if (BCM590XX_REG_IS_GPLDO(id))
		return BCM590XX_GPLDO1CTRL + id;
	else
		return BCM590XX_CSRVOUT1 + (id - BCM590XX_REG_CSR) * 3;
}

static int bcm590xx_get_enable_register(int id)
{
	int reg = 0;

	if (BCM590XX_REG_IS_LDO(id))
		reg = BCM590XX_RFLDOPMCTRL1 + id * 2;
	else if (BCM590XX_REG_IS_GPLDO(id))
		reg = BCM590XX_GPLDO1PMCTRL1 + id * 2;
	else
		switch (id) {
		case BCM590XX_REG_CSR:
			reg = BCM590XX_CSRPMCTRL1;
			break;
		case BCM590XX_REG_IOSR1:
			reg = BCM590XX_IOSR1PMCTRL1;
			break;
		case BCM590XX_REG_IOSR2:
			reg = BCM590XX_IOSR2PMCTRL1;
			break;
		case BCM590XX_REG_MSR:
			reg = BCM590XX_MSRPMCTRL1;
			break;
		case BCM590XX_REG_SDSR1:
			reg = BCM590XX_SDSR1PMCTRL1;
			break;
		case BCM590XX_REG_SDSR2:
			reg = BCM590XX_SDSR2PMCTRL1;
			break;
		case BCM590XX_REG_VBUS:
			reg = BCM590XX_OTG_CTRL;
		}


	return reg;
}

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

#define BCM590XX_MATCH(_name, _id) \
	{ \
		.name = #_name, \
		.driver_data = (void *)&bcm590xx_regs[BCM590XX_REG_##_id], \
	}

static struct of_regulator_match bcm590xx_matches[] = {
	BCM590XX_MATCH(rfldo, RFLDO),
	BCM590XX_MATCH(camldo1, CAMLDO1),
	BCM590XX_MATCH(camldo2, CAMLDO2),
	BCM590XX_MATCH(simldo1, SIMLDO1),
	BCM590XX_MATCH(simldo2, SIMLDO2),
	BCM590XX_MATCH(sdldo, SDLDO),
	BCM590XX_MATCH(sdxldo, SDXLDO),
	BCM590XX_MATCH(mmcldo1, MMCLDO1),
	BCM590XX_MATCH(mmcldo2, MMCLDO2),
	BCM590XX_MATCH(audldo, AUDLDO),
	BCM590XX_MATCH(micldo, MICLDO),
	BCM590XX_MATCH(usbldo, USBLDO),
	BCM590XX_MATCH(vibldo, VIBLDO),
	BCM590XX_MATCH(csr, CSR),
	BCM590XX_MATCH(iosr1, IOSR1),
	BCM590XX_MATCH(iosr2, IOSR2),
	BCM590XX_MATCH(msr, MSR),
	BCM590XX_MATCH(sdsr1, SDSR1),
	BCM590XX_MATCH(sdsr2, SDSR2),
	BCM590XX_MATCH(vsr, VSR),
	BCM590XX_MATCH(gpldo1, GPLDO1),
	BCM590XX_MATCH(gpldo2, GPLDO2),
	BCM590XX_MATCH(gpldo3, GPLDO3),
	BCM590XX_MATCH(gpldo4, GPLDO4),
	BCM590XX_MATCH(gpldo5, GPLDO5),
	BCM590XX_MATCH(gpldo6, GPLDO6),
	BCM590XX_MATCH(vbus, VBUS),
};

static struct bcm590xx_board *bcm590xx_parse_dt_reg_data(
		struct platform_device *pdev,
		struct of_regulator_match **bcm590xx_reg_matches)
{
	struct bcm590xx_board *data;
	struct device_node *np = pdev->dev.parent->of_node;
	struct device_node *regulators;
	struct of_regulator_match *matches = bcm590xx_matches;
	int count = ARRAY_SIZE(bcm590xx_matches);
	int idx = 0;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "of node not found\n");
		return NULL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	np = of_node_get(np);
	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_warn(&pdev->dev, "regulator node not found\n");
		return NULL;
	}

	ret = of_regulator_match(&pdev->dev, regulators, matches, count);
	of_node_put(regulators);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error parsing regulator init data: %d\n",
			ret);
		return NULL;
	}

	*bcm590xx_reg_matches = matches;

	for (idx = 0; idx < count; idx++) {
		if (!matches[idx].init_data || !matches[idx].of_node)
			continue;

		data->bcm590xx_pmu_init_data[idx] = matches[idx].init_data;
	}

	return data;
}

static int bcm590xx_probe(struct platform_device *pdev)
{
	struct bcm590xx *bcm590xx = dev_get_drvdata(pdev->dev.parent);
	struct bcm590xx_board *pmu_data = NULL;
	struct bcm590xx_reg *pmu;
	struct regulator_config config = { };
	struct bcm590xx_info *info;
	struct regulator_init_data *reg_data;
	struct regulator_dev *rdev;
	struct of_regulator_match *bcm590xx_reg_matches = NULL;
	int i;

	pmu_data = bcm590xx_parse_dt_reg_data(pdev,
					      &bcm590xx_reg_matches);

	pmu = devm_kzalloc(&pdev->dev, sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	pmu->mfd = bcm590xx;

	platform_set_drvdata(pdev, pmu);

	pmu->desc = devm_kcalloc(&pdev->dev,
				 BCM590XX_NUM_REGS,
				 sizeof(struct regulator_desc),
				 GFP_KERNEL);
	if (!pmu->desc)
		return -ENOMEM;

	info = bcm590xx_regs;

	for (i = 0; i < BCM590XX_NUM_REGS; i++, info++) {
		if (pmu_data)
			reg_data = pmu_data->bcm590xx_pmu_init_data[i];
		else
			reg_data = NULL;

		/* Register the regulators */
		pmu->desc[i].name = info->name;
		pmu->desc[i].supply_name = info->vin_name;
		pmu->desc[i].id = i;
		pmu->desc[i].volt_table = info->volt_table;
		pmu->desc[i].n_voltages = info->n_voltages;
		pmu->desc[i].linear_ranges = info->linear_ranges;
		pmu->desc[i].n_linear_ranges = info->n_linear_ranges;

		if ((BCM590XX_REG_IS_LDO(i)) || (BCM590XX_REG_IS_GPLDO(i))) {
			pmu->desc[i].ops = &bcm590xx_ops_ldo;
			pmu->desc[i].vsel_mask = BCM590XX_LDO_VSEL_MASK;
		} else if (BCM590XX_REG_IS_VBUS(i))
			pmu->desc[i].ops = &bcm590xx_ops_vbus;
		else {
			pmu->desc[i].ops = &bcm590xx_ops_dcdc;
			pmu->desc[i].vsel_mask = BCM590XX_SR_VSEL_MASK;
		}

		if (BCM590XX_REG_IS_VBUS(i))
			pmu->desc[i].enable_mask = BCM590XX_VBUS_ENABLE;
		else {
			pmu->desc[i].vsel_reg = bcm590xx_get_vsel_register(i);
			pmu->desc[i].enable_is_inverted = true;
			pmu->desc[i].enable_mask = BCM590XX_REG_ENABLE;
		}
		pmu->desc[i].enable_reg = bcm590xx_get_enable_register(i);
		pmu->desc[i].type = REGULATOR_VOLTAGE;
		pmu->desc[i].owner = THIS_MODULE;

		config.dev = bcm590xx->dev;
		config.init_data = reg_data;
		config.driver_data = pmu;
		if (BCM590XX_REG_IS_GPLDO(i) || BCM590XX_REG_IS_VBUS(i))
			config.regmap = bcm590xx->regmap_sec;
		else
			config.regmap = bcm590xx->regmap_pri;

		if (bcm590xx_reg_matches)
			config.of_node = bcm590xx_reg_matches[i].of_node;

		rdev = devm_regulator_register(&pdev->dev, &pmu->desc[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(bcm590xx->dev,
				"failed to register %s regulator\n",
				pdev->name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver bcm590xx_regulator_driver = {
	.driver = {
		.name = "bcm590xx-vregs",
	},
	.probe = bcm590xx_probe,
};
module_platform_driver(bcm590xx_regulator_driver);

MODULE_AUTHOR("Matt Porter <mporter@linaro.org>");
MODULE_DESCRIPTION("BCM590xx voltage regulator driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bcm590xx-vregs");
