/*
 * AXP20x regulators driver.
 *
 * Copyright (C) 2013 Carlo Caione <carlo@caione.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/axp20x.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define AXP20X_IO_ENABLED		0x03
#define AXP20X_IO_DISABLED		0x07

#define AXP22X_IO_ENABLED		0x03
#define AXP22X_IO_DISABLED		0x04

#define AXP20X_WORKMODE_DCDC2_MASK	BIT(2)
#define AXP20X_WORKMODE_DCDC3_MASK	BIT(1)
#define AXP22X_WORKMODE_DCDCX_MASK(x)	BIT(x)

#define AXP20X_FREQ_DCDC_MASK		0x0f

#define AXP22X_MISC_N_VBUSEN_FUNC	BIT(4)

#define AXP_DESC_IO(_family, _id, _match, _supply, _min, _max, _step, _vreg,	\
		    _vmask, _ereg, _emask, _enable_val, _disable_val)		\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.enable_val	= (_enable_val),				\
		.disable_val	= (_disable_val),				\
		.ops		= &axp20x_ops,					\
	}

#define AXP_DESC(_family, _id, _match, _supply, _min, _max, _step, _vreg,	\
		 _vmask, _ereg, _emask) 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops,					\
	}

#define AXP_DESC_SW(_family, _id, _match, _supply, _ereg, _emask)		\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.owner		= THIS_MODULE,					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops_sw,				\
	}

#define AXP_DESC_FIXED(_family, _id, _match, _supply, _volt)			\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= 1,						\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_volt) * 1000,				\
		.ops		= &axp20x_ops_fixed				\
	}

#define AXP_DESC_RANGES(_family, _id, _match, _supply, _ranges, _n_voltages,	\
			_vreg, _vmask, _ereg, _emask)				\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (_n_voltages),				\
		.owner		= THIS_MODULE,					\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.linear_ranges	= (_ranges),					\
		.n_linear_ranges = ARRAY_SIZE(_ranges),				\
		.ops		= &axp20x_ops_range,				\
	}

static const struct regulator_ops axp20x_ops_fixed = {
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops axp20x_ops_range = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops axp20x_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops axp20x_ops_sw = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_linear_range axp20x_ldo4_ranges[] = {
	REGULATOR_LINEAR_RANGE(1250000, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(1300000, 0x1, 0x8, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 0x9, 0x9, 0),
	REGULATOR_LINEAR_RANGE(2700000, 0xa, 0xb, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xc, 0xf, 100000),
};

static const struct regulator_desc axp20x_regulators[] = {
	AXP_DESC(AXP20X, DCDC2, "dcdc2", "vin2", 700, 2275, 25,
		 AXP20X_DCDC2_V_OUT, 0x3f, AXP20X_PWR_OUT_CTRL, 0x10),
	AXP_DESC(AXP20X, DCDC3, "dcdc3", "vin3", 700, 3500, 25,
		 AXP20X_DCDC3_V_OUT, 0x7f, AXP20X_PWR_OUT_CTRL, 0x02),
	AXP_DESC_FIXED(AXP20X, LDO1, "ldo1", "acin", 1300),
	AXP_DESC(AXP20X, LDO2, "ldo2", "ldo24in", 1800, 3300, 100,
		 AXP20X_LDO24_V_OUT, 0xf0, AXP20X_PWR_OUT_CTRL, 0x04),
	AXP_DESC(AXP20X, LDO3, "ldo3", "ldo3in", 700, 3500, 25,
		 AXP20X_LDO3_V_OUT, 0x7f, AXP20X_PWR_OUT_CTRL, 0x40),
	AXP_DESC_RANGES(AXP20X, LDO4, "ldo4", "ldo24in", axp20x_ldo4_ranges,
			16, AXP20X_LDO24_V_OUT, 0x0f, AXP20X_PWR_OUT_CTRL,
			0x08),
	AXP_DESC_IO(AXP20X, LDO5, "ldo5", "ldo5in", 1800, 3300, 100,
		    AXP20X_LDO5_V_OUT, 0xf0, AXP20X_GPIO0_CTRL, 0x07,
		    AXP20X_IO_ENABLED, AXP20X_IO_DISABLED),
};

static const struct regulator_desc axp22x_regulators[] = {
	AXP_DESC(AXP22X, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP22X_DCDC1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(1)),
	AXP_DESC(AXP22X, DCDC2, "dcdc2", "vin2", 600, 1540, 20,
		 AXP22X_DCDC2_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(2)),
	AXP_DESC(AXP22X, DCDC3, "dcdc3", "vin3", 600, 1860, 20,
		 AXP22X_DCDC3_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(3)),
	AXP_DESC(AXP22X, DCDC4, "dcdc4", "vin4", 600, 1540, 20,
		 AXP22X_DCDC4_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(4)),
	AXP_DESC(AXP22X, DCDC5, "dcdc5", "vin5", 1000, 2550, 50,
		 AXP22X_DCDC5_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(5)),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP22X, DC1SW, "dc1sw", NULL, AXP22X_PWR_OUT_CTRL2,
		    BIT(7)),
	/* LDO regulator internally chained to DCDC5 */
	AXP_DESC(AXP22X, DC5LDO, "dc5ldo", NULL, 700, 1400, 100,
		 AXP22X_DC5LDO_V_OUT, 0x7, AXP22X_PWR_OUT_CTRL1, BIT(0)),
	AXP_DESC(AXP22X, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(6)),
	AXP_DESC(AXP22X, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(7)),
	AXP_DESC(AXP22X, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL3, BIT(7)),
	AXP_DESC(AXP22X, DLDO1, "dldo1", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(3)),
	AXP_DESC(AXP22X, DLDO2, "dldo2", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(4)),
	AXP_DESC(AXP22X, DLDO3, "dldo3", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(5)),
	AXP_DESC(AXP22X, DLDO4, "dldo4", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO4_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(6)),
	AXP_DESC(AXP22X, ELDO1, "eldo1", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(0)),
	AXP_DESC(AXP22X, ELDO2, "eldo2", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(1)),
	AXP_DESC(AXP22X, ELDO3, "eldo3", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(2)),
	/* Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints */
	AXP_DESC_IO(AXP22X, LDO_IO0, "ldo_io0", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO0_V_OUT, 0x1f, AXP20X_GPIO0_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	/* Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints */
	AXP_DESC_IO(AXP22X, LDO_IO1, "ldo_io1", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO1_V_OUT, 0x1f, AXP20X_GPIO1_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP22X, RTC_LDO, "rtc_ldo", "ips", 3000),
};

static const struct regulator_desc axp22x_drivevbus_regulator = {
	.name		= "drivevbus",
	.supply_name	= "drivevbus",
	.of_match	= of_match_ptr("drivevbus"),
	.regulators_node = of_match_ptr("regulators"),
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.enable_reg	= AXP20X_VBUS_IPSOUT_MGMT,
	.enable_mask	= BIT(2),
	.ops		= &axp20x_ops_sw,
};

static const struct regulator_linear_range axp806_dcdca_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x32, 10000),
	REGULATOR_LINEAR_RANGE(1120000, 0x33, 0x47, 20000),
};

static const struct regulator_linear_range axp806_dcdcd_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x2d, 20000),
	REGULATOR_LINEAR_RANGE(1600000, 0x2e, 0x3f, 100000),
};

static const struct regulator_linear_range axp806_cldo2_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0x1a, 100000),
	REGULATOR_LINEAR_RANGE(3400000, 0x1b, 0x1f, 200000),
};

static const struct regulator_desc axp806_regulators[] = {
	AXP_DESC_RANGES(AXP806, DCDCA, "dcdca", "vina", axp806_dcdca_ranges,
			72, AXP806_DCDCA_V_CTRL, 0x7f, AXP806_PWR_OUT_CTRL1,
			BIT(0)),
	AXP_DESC(AXP806, DCDCB, "dcdcb", "vinb", 1000, 2550, 50,
		 AXP806_DCDCB_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(1)),
	AXP_DESC_RANGES(AXP806, DCDCC, "dcdcc", "vinc", axp806_dcdca_ranges,
			72, AXP806_DCDCC_V_CTRL, 0x7f, AXP806_PWR_OUT_CTRL1,
			BIT(2)),
	AXP_DESC_RANGES(AXP806, DCDCD, "dcdcd", "vind", axp806_dcdcd_ranges,
			64, AXP806_DCDCD_V_CTRL, 0x3f, AXP806_PWR_OUT_CTRL1,
			BIT(3)),
	AXP_DESC(AXP806, DCDCE, "dcdce", "vine", 1100, 3400, 100,
		 AXP806_DCDCE_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(4)),
	AXP_DESC(AXP806, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP806_ALDO1_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(5)),
	AXP_DESC(AXP806, ALDO2, "aldo2", "aldoin", 700, 3400, 100,
		 AXP806_ALDO2_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(6)),
	AXP_DESC(AXP806, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP806_ALDO3_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(7)),
	AXP_DESC(AXP806, BLDO1, "bldo1", "bldoin", 700, 1900, 100,
		 AXP806_BLDO1_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(0)),
	AXP_DESC(AXP806, BLDO2, "bldo2", "bldoin", 700, 1900, 100,
		 AXP806_BLDO2_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(1)),
	AXP_DESC(AXP806, BLDO3, "bldo3", "bldoin", 700, 1900, 100,
		 AXP806_BLDO3_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(2)),
	AXP_DESC(AXP806, BLDO4, "bldo4", "bldoin", 700, 1900, 100,
		 AXP806_BLDO4_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(3)),
	AXP_DESC(AXP806, CLDO1, "cldo1", "cldoin", 700, 3300, 100,
		 AXP806_CLDO1_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL2, BIT(4)),
	AXP_DESC_RANGES(AXP806, CLDO2, "cldo2", "cldoin", axp806_cldo2_ranges,
			32, AXP806_CLDO2_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL2,
			BIT(5)),
	AXP_DESC(AXP806, CLDO3, "cldo3", "cldoin", 700, 3300, 100,
		 AXP806_CLDO3_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL2, BIT(6)),
	AXP_DESC_SW(AXP806, SW, "sw", "swin", AXP806_PWR_OUT_CTRL2, BIT(7)),
};

static const struct regulator_linear_range axp809_dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x2f, 20000),
	REGULATOR_LINEAR_RANGE(1800000, 0x30, 0x38, 100000),
};

static const struct regulator_desc axp809_regulators[] = {
	AXP_DESC(AXP809, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP22X_DCDC1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(1)),
	AXP_DESC(AXP809, DCDC2, "dcdc2", "vin2", 600, 1540, 20,
		 AXP22X_DCDC2_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(2)),
	AXP_DESC(AXP809, DCDC3, "dcdc3", "vin3", 600, 1860, 20,
		 AXP22X_DCDC3_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(3)),
	AXP_DESC_RANGES(AXP809, DCDC4, "dcdc4", "vin4", axp809_dcdc4_ranges,
			57, AXP22X_DCDC4_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1,
			BIT(4)),
	AXP_DESC(AXP809, DCDC5, "dcdc5", "vin5", 1000, 2550, 50,
		 AXP22X_DCDC5_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(5)),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP809, DC1SW, "dc1sw", NULL, AXP22X_PWR_OUT_CTRL2,
		    BIT(7)),
	/* LDO regulator internally chained to DCDC5 */
	AXP_DESC(AXP809, DC5LDO, "dc5ldo", NULL, 700, 1400, 100,
		 AXP22X_DC5LDO_V_OUT, 0x7, AXP22X_PWR_OUT_CTRL1, BIT(0)),
	AXP_DESC(AXP809, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(6)),
	AXP_DESC(AXP809, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(7)),
	AXP_DESC(AXP809, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(5)),
	AXP_DESC_RANGES(AXP809, DLDO1, "dldo1", "dldoin", axp806_cldo2_ranges,
			32, AXP22X_DLDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2,
			BIT(3)),
	AXP_DESC(AXP809, DLDO2, "dldo2", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(4)),
	AXP_DESC(AXP809, ELDO1, "eldo1", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(0)),
	AXP_DESC(AXP809, ELDO2, "eldo2", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(1)),
	AXP_DESC(AXP809, ELDO3, "eldo3", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(2)),
	/*
	 * Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints
	 */
	AXP_DESC_IO(AXP809, LDO_IO0, "ldo_io0", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO0_V_OUT, 0x1f, AXP20X_GPIO0_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	/*
	 * Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints
	 */
	AXP_DESC_IO(AXP809, LDO_IO1, "ldo_io1", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO1_V_OUT, 0x1f, AXP20X_GPIO1_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP809, RTC_LDO, "rtc_ldo", "ips", 1800),
	AXP_DESC_SW(AXP809, SW, "sw", "swin", AXP22X_PWR_OUT_CTRL2, BIT(6)),
};

static int axp20x_set_dcdc_freq(struct platform_device *pdev, u32 dcdcfreq)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	unsigned int reg = AXP20X_DCDC_FREQ;
	u32 min, max, def, step;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		min = 750;
		max = 1875;
		def = 1500;
		step = 75;
		break;
	case AXP806_ID:
		/*
		 * AXP806 DCDC work frequency setting has the same range and
		 * step as AXP22X, but at a different register.
		 * Fall through to the check below.
		 * (See include/linux/mfd/axp20x.h)
		 */
		reg = AXP806_DCDC_FREQ_CTRL;
	case AXP221_ID:
	case AXP223_ID:
	case AXP809_ID:
		min = 1800;
		max = 4050;
		def = 3000;
		step = 150;
		break;
	default:
		dev_err(&pdev->dev,
			"Setting DCDC frequency for unsupported AXP variant\n");
		return -EINVAL;
	}

	if (dcdcfreq == 0)
		dcdcfreq = def;

	if (dcdcfreq < min) {
		dcdcfreq = min;
		dev_warn(&pdev->dev, "DCDC frequency too low. Set to %ukHz\n",
			 min);
	}

	if (dcdcfreq > max) {
		dcdcfreq = max;
		dev_warn(&pdev->dev, "DCDC frequency too high. Set to %ukHz\n",
			 max);
	}

	dcdcfreq = (dcdcfreq - min) / step;

	return regmap_update_bits(axp20x->regmap, reg,
				  AXP20X_FREQ_DCDC_MASK, dcdcfreq);
}

static int axp20x_regulator_parse_dt(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	int ret;
	u32 dcdcfreq = 0;

	np = of_node_get(pdev->dev.parent->of_node);
	if (!np)
		return 0;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_warn(&pdev->dev, "regulators node not found\n");
	} else {
		of_property_read_u32(regulators, "x-powers,dcdc-freq", &dcdcfreq);
		ret = axp20x_set_dcdc_freq(pdev, dcdcfreq);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error setting dcdc frequency: %d\n", ret);
			return ret;
		}

		of_node_put(regulators);
	}

	return 0;
}

static int axp20x_set_dcdc_workmode(struct regulator_dev *rdev, int id, u32 workmode)
{
	struct axp20x_dev *axp20x = rdev_get_drvdata(rdev);
	unsigned int reg = AXP20X_DCDC_MODE;
	unsigned int mask;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		if ((id != AXP20X_DCDC2) && (id != AXP20X_DCDC3))
			return -EINVAL;

		mask = AXP20X_WORKMODE_DCDC2_MASK;
		if (id == AXP20X_DCDC3)
			mask = AXP20X_WORKMODE_DCDC3_MASK;

		workmode <<= ffs(mask) - 1;
		break;

	case AXP806_ID:
		reg = AXP806_DCDC_MODE_CTRL2;
		/*
		 * AXP806 DCDC regulator IDs have the same range as AXP22X.
		 * Fall through to the check below.
		 * (See include/linux/mfd/axp20x.h)
		 */
	case AXP221_ID:
	case AXP223_ID:
	case AXP809_ID:
		if (id < AXP22X_DCDC1 || id > AXP22X_DCDC5)
			return -EINVAL;

		mask = AXP22X_WORKMODE_DCDCX_MASK(id - AXP22X_DCDC1);
		workmode <<= id - AXP22X_DCDC1;
		break;

	default:
		/* should not happen */
		WARN_ON(1);
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, reg, mask, workmode);
}

/*
 * This function checks whether a regulator is part of a poly-phase
 * output setup based on the registers settings. Returns true if it is.
 */
static bool axp20x_is_polyphase_slave(struct axp20x_dev *axp20x, int id)
{
	u32 reg = 0;

	/* Only AXP806 has poly-phase outputs */
	if (axp20x->variant != AXP806_ID)
		return false;

	regmap_read(axp20x->regmap, AXP806_DCDC_MODE_CTRL2, &reg);

	switch (id) {
	case AXP806_DCDCB:
		return (((reg & GENMASK(7, 6)) == BIT(6)) ||
			((reg & GENMASK(7, 6)) == BIT(7)));
	case AXP806_DCDCC:
		return ((reg & GENMASK(7, 6)) == BIT(7));
	case AXP806_DCDCE:
		return !!(reg & BIT(5));
	}

	return false;
}

static int axp20x_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	const struct regulator_desc *regulators;
	struct regulator_config config = {
		.dev = pdev->dev.parent,
		.regmap = axp20x->regmap,
		.driver_data = axp20x,
	};
	int ret, i, nregulators;
	u32 workmode;
	const char *dcdc1_name = axp22x_regulators[AXP22X_DCDC1].name;
	const char *dcdc5_name = axp22x_regulators[AXP22X_DCDC5].name;
	bool drivevbus = false;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		regulators = axp20x_regulators;
		nregulators = AXP20X_REG_ID_MAX;
		break;
	case AXP221_ID:
	case AXP223_ID:
		regulators = axp22x_regulators;
		nregulators = AXP22X_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
		break;
	case AXP806_ID:
		regulators = axp806_regulators;
		nregulators = AXP806_REG_ID_MAX;
		break;
	case AXP809_ID:
		regulators = axp809_regulators;
		nregulators = AXP809_REG_ID_MAX;
		break;
	default:
		dev_err(&pdev->dev, "Unsupported AXP variant: %ld\n",
			axp20x->variant);
		return -EINVAL;
	}

	/* This only sets the dcdc freq. Ignore any errors */
	axp20x_regulator_parse_dt(pdev);

	for (i = 0; i < nregulators; i++) {
		const struct regulator_desc *desc = &regulators[i];
		struct regulator_desc *new_desc;

		/*
		 * If this regulator is a slave in a poly-phase setup,
		 * skip it, as its controls are bound to the master
		 * regulator and won't work.
		 */
		if (axp20x_is_polyphase_slave(axp20x, i))
			continue;

		/*
		 * Regulators DC1SW and DC5LDO are connected internally,
		 * so we have to handle their supply names separately.
		 *
		 * We always register the regulators in proper sequence,
		 * so the supply names are correctly read. See the last
		 * part of this loop to see where we save the DT defined
		 * name.
		 */
		if ((regulators == axp22x_regulators && i == AXP22X_DC1SW) ||
		    (regulators == axp809_regulators && i == AXP809_DC1SW)) {
			new_desc = devm_kzalloc(&pdev->dev, sizeof(*desc),
						GFP_KERNEL);
			*new_desc = regulators[i];
			new_desc->supply_name = dcdc1_name;
			desc = new_desc;
		}

		if ((regulators == axp22x_regulators && i == AXP22X_DC5LDO) ||
		    (regulators == axp809_regulators && i == AXP809_DC5LDO)) {
			new_desc = devm_kzalloc(&pdev->dev, sizeof(*desc),
						GFP_KERNEL);
			*new_desc = regulators[i];
			new_desc->supply_name = dcdc5_name;
			desc = new_desc;
		}

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register %s\n",
				regulators[i].name);

			return PTR_ERR(rdev);
		}

		ret = of_property_read_u32(rdev->dev.of_node,
					   "x-powers,dcdc-workmode",
					   &workmode);
		if (!ret) {
			if (axp20x_set_dcdc_workmode(rdev, i, workmode))
				dev_err(&pdev->dev, "Failed to set workmode on %s\n",
					rdev->desc->name);
		}

		/*
		 * Save AXP22X DCDC1 / DCDC5 regulator names for later.
		 */
		if ((regulators == axp22x_regulators && i == AXP22X_DCDC1) ||
		    (regulators == axp809_regulators && i == AXP809_DCDC1))
			of_property_read_string(rdev->dev.of_node,
						"regulator-name",
						&dcdc1_name);

		if ((regulators == axp22x_regulators && i == AXP22X_DCDC5) ||
		    (regulators == axp809_regulators && i == AXP809_DCDC5))
			of_property_read_string(rdev->dev.of_node,
						"regulator-name",
						&dcdc5_name);
	}

	if (drivevbus) {
		/* Change N_VBUSEN sense pin to DRIVEVBUS output pin */
		regmap_update_bits(axp20x->regmap, AXP20X_OVER_TMP,
				   AXP22X_MISC_N_VBUSEN_FUNC, 0);
		rdev = devm_regulator_register(&pdev->dev,
					       &axp22x_drivevbus_regulator,
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register drivevbus\n");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static struct platform_driver axp20x_regulator_driver = {
	.probe	= axp20x_regulator_probe,
	.driver	= {
		.name		= "axp20x-regulator",
	},
};

module_platform_driver(axp20x_regulator_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_DESCRIPTION("Regulator Driver for AXP20X PMIC");
MODULE_ALIAS("platform:axp20x-regulator");
