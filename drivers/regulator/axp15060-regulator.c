// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Starfive Technology Co., Ltd.
 * Author: Kevin Xie <kevin.xie@starfivetech.com>
 */
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/axp15060.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>

#define AXP15060_ON_OFF_CTRL_1		0x10
#define AXP15060_ON_OFF_CTRL_2		0x11
#define AXP15060_ON_OFF_CTRL_3		0x12
#define AXP15060_VOL_CTRL_DCDC_1	0x13
#define AXP15060_VOL_CTRL_DCDC_2	0x14
#define AXP15060_VOL_CTRL_DCDC_3	0x15
#define AXP15060_VOL_CTRL_DCDC_4	0x16
#define AXP15060_VOL_CTRL_DCDC_5	0x17
#define AXP15060_VOL_CTRL_DCDC_6	0x18
#define AXP15060_VOL_CTRL_ALDO_1	0x19
#define AXP15060_DCDC_MODE_CTRL_1	0x1A
#define AXP15060_DCDC_MODE_CTRL_2	0x1B

#define AXP15060_OUTPUT_MONITOR_OFF_DISCHARGE	0x1E
#define AXP15060_IRQ_PWROK_VOFF_SETTING		0x1F
#define AXP15060_VOL_CTRL_ALDO_2	0x20
#define AXP15060_VOL_CTRL_ALDO_3	0x21
#define AXP15060_VOL_CTRL_ALDO_4	0x22
#define AXP15060_VOL_CTRL_ALDO_5	0x23
#define AXP15060_VOL_CTRL_BLDO_1	0x24
#define AXP15060_VOL_CTRL_BLDO_2	0x25
#define AXP15060_VOL_CTRL_BLDO_3	0x26
#define AXP15060_VOL_CTRL_BLDO_4	0x27
#define AXP15060_VOL_CTRL_BLDO_5	0x28
#define AXP15060_VOL_CTRL_CLDO_1	0x29
#define AXP15060_VOL_CTRL_CLDO_2	0x2A
/* CLDO3 voltage ctrl and CLDO3/GPIO1/Wakeup ctrl */
#define AXP15060_VOL_CTRL_CLDO_3	0x2B
#define AXP15060_CLDO_4_GPIO_2_CTRL	0x2C
#define AXP15060_VOL_CTRL_CLDO_4	0x2D
#define AXP15060_VOL_CTRL_CPUSLDO	0x2E

#define AXP15060_PWR_WAKEUP_CTRL	0x31
/* Power disable and power down sequence */
#define AXP15060_PWR_DISABLE		0x32

#define AXP15060_POK_SETTING		0x36

#define AXP15060_INTERFACE_MODE_SEL	0x3E

#define AXP15060_IRQ_ENABLE_1		0x40
#define AXP15060_IRQ_ENABLE_2		0x41

#define AXP15060_IRQ_STATUS_1		0x48
#define AXP15060_IRQ_STATUS_2		0x49

/* AXP15060_ON_OFF_CTRL_1 */
#define AXP15060_PWR_OUT_DCDC1_MASK		BIT(0)
#define AXP15060_PWR_OUT_DCDC2_MASK		BIT(1)
#define AXP15060_PWR_OUT_DCDC3_MASK		BIT(2)
#define AXP15060_PWR_OUT_DCDC4_MASK		BIT(3)
#define AXP15060_PWR_OUT_DCDC5_MASK		BIT(4)
#define AXP15060_PWR_OUT_DCDC6_MASK		BIT(5)

/* AXP15060_ON_OFF_CTRL_2 */
#define AXP15060_PWR_OUT_ALDO1_MASK		BIT(0)
#define AXP15060_PWR_OUT_ALDO2_MASK		BIT(1)
#define AXP15060_PWR_OUT_ALDO3_MASK		BIT(2)
#define AXP15060_PWR_OUT_ALDO4_MASK		BIT(3)
#define AXP15060_PWR_OUT_ALDO5_MASK		BIT(4)
#define AXP15060_PWR_OUT_BLDO1_MASK		BIT(5)
#define AXP15060_PWR_OUT_BLDO2_MASK		BIT(6)
#define AXP15060_PWR_OUT_BLDO3_MASK		BIT(7)

/* AXP15060_ON_OFF_CTRL_3 */
#define AXP15060_PWR_OUT_BLDO4_MASK		BIT(0)
#define AXP15060_PWR_OUT_BLDO5_MASK		BIT(1)
#define AXP15060_PWR_OUT_CLDO1_MASK		BIT(2)
#define AXP15060_PWR_OUT_CLDO2_MASK		BIT(3)
#define AXP15060_PWR_OUT_CLDO3_MASK		BIT(4)
#define AXP15060_PWR_OUT_CLDO4_MASK		BIT(5)
#define AXP15060_PWR_OUT_CPULDO_MASK		BIT(6)
#define AXP15060_PWR_OUT_SWITCH_MASK		BIT(7)

#define AXP15060_ALDO1_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_ALDO2_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_ALDO3_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_ALDO4_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_ALDO5_V_OUT_MASK		GENMASK(4, 0)

#define AXP15060_BLDO1_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_BLDO2_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_BLDO3_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_BLDO4_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_BLDO5_V_OUT_MASK		GENMASK(4, 0)

#define AXP15060_CLDO1_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_CLDO2_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_CLDO3_V_OUT_MASK		GENMASK(4, 0)
#define AXP15060_CLDO4_V_OUT_MASK		GENMASK(5, 0)
#define AXP15060_CPUSLDO_V_OUT_MASK		GENMASK(3, 0)

#define AXP15060_DCDC1_V_OUT_MASK		GENMASK(4, 0)
/* DCDC2 bit7 is set by fw, which is different from datasheet desc. */
#define AXP15060_DCDC2_V_OUT_MASK		GENMASK(7, 0)
#define AXP15060_DCDC3_V_OUT_MASK		GENMASK(6, 0)
#define AXP15060_DCDC4_V_OUT_MASK		GENMASK(6, 0)
#define AXP15060_DCDC5_V_OUT_MASK		GENMASK(6, 0)
#define AXP15060_DCDC6_V_OUT_MASK		GENMASK(4, 0)

#define AXP15060_DCDC2_V_OUT_RANGE1_7bit_500mV_START		0x00
#define AXP15060_DCDC2_V_OUT_RANGE1_7bit_1200mV_END		0x46
#define AXP15060_DCDC2_V_OUT_RANGE2_7bit_1220mV_START		0x47
#define AXP15060_DCDC2_V_OUT_RANGE2_7bit_1540mV_END		0x57
#define AXP15060_DCDC2_V_OUT_RANGE1_500mV_START			0x80
#define AXP15060_DCDC2_V_OUT_RANGE1_1200mV_END			0xC6
#define AXP15060_DCDC2_V_OUT_RANGE2_1220mV_START		0xC7
#define AXP15060_DCDC2_V_OUT_RANGE2_1540mV_END			0xD7
#define AXP15060_DCDC2_NUM_VOLTAGES					\
				(AXP15060_DCDC2_V_OUT_RANGE2_1540mV_END + 1)

static const struct linear_range axp15060_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000,
				AXP15060_DCDC2_V_OUT_RANGE1_7bit_500mV_START,
				AXP15060_DCDC2_V_OUT_RANGE1_7bit_1200mV_END,
				10000),
	REGULATOR_LINEAR_RANGE(1220000,
				AXP15060_DCDC2_V_OUT_RANGE2_7bit_1220mV_START,
				AXP15060_DCDC2_V_OUT_RANGE2_7bit_1540mV_END,
				20000),
	REGULATOR_LINEAR_RANGE(500000,
				AXP15060_DCDC2_V_OUT_RANGE1_500mV_START,
				AXP15060_DCDC2_V_OUT_RANGE1_1200mV_END,
				10000),
	REGULATOR_LINEAR_RANGE(1220000,
				AXP15060_DCDC2_V_OUT_RANGE2_1220mV_START,
				AXP15060_DCDC2_V_OUT_RANGE2_1540mV_END,
				20000),
};

static const struct regmap_range axp15060_writeable_ranges[] = {
	regmap_reg_range(AXP15060_ON_OFF_CTRL_1, AXP15060_DCDC_MODE_CTRL_2),
	regmap_reg_range(AXP15060_OUTPUT_MONITOR_OFF_DISCHARGE,
					AXP15060_VOL_CTRL_CPUSLDO),
	regmap_reg_range(AXP15060_PWR_WAKEUP_CTRL, AXP15060_PWR_DISABLE),
	regmap_reg_range(AXP15060_POK_SETTING, AXP15060_POK_SETTING),
	regmap_reg_range(AXP15060_INTERFACE_MODE_SEL, AXP15060_INTERFACE_MODE_SEL),
	regmap_reg_range(AXP15060_IRQ_ENABLE_1, AXP15060_IRQ_ENABLE_2),
	regmap_reg_range(AXP15060_IRQ_STATUS_1, AXP15060_IRQ_STATUS_2),
};

static const struct regmap_access_table axp15060_writeable_table = {
	.yes_ranges	= axp15060_writeable_ranges,
	.n_yes_ranges	= ARRAY_SIZE(axp15060_writeable_ranges),
};

static const struct regmap_config axp15060_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.wr_table	= &axp15060_writeable_table,
	.max_register = AXP15060_IRQ_STATUS_2,
	.cache_type = REGCACHE_NONE,
};

static const struct regulator_ops axp15060_fixed_ops = {
	.list_voltage		= regulator_list_voltage_linear,
};

static const struct regulator_ops axp15060_sw_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops axp15060_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_ops axp15060_range_ops = {
	.set_voltage_sel		= regulator_set_voltage_sel_regmap,
	.get_voltage_sel		= regulator_get_voltage_sel_regmap,
	.set_voltage_time_sel		= regulator_set_voltage_time_sel,
	.list_voltage			= regulator_list_voltage_linear_range,
	.enable				= regulator_enable_regmap,
	.disable			= regulator_disable_regmap,
	.is_enabled			= regulator_is_enabled_regmap,
};

#define AXP15060_DESC_FIXED(_id, _match, _volt)					\
	{									\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP15060_ID_##_id,				\
		.n_voltages	= 1,						\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_volt) * 1000,				\
		.ops		= &axp15060_fixed_ops,				\
	}

#define AXP_DESC_SW(_id, _match, _ereg, _emask)					\
	{									\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP15060_ID_##_id,				\
		.owner		= THIS_MODULE,					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp15060_sw_ops,				\
	}

#define AXP15060_DESC(_id, _match, _min, _max, _step, _vreg,			\
		_vmask, _ereg, _emask)						\
	{									\
		.name		= (_match),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP15060_ID_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp15060_ops,				\
	}

#define AXP15060_DESC_RANGES(_id, _match, _ranges, _n_voltages,\
					 _vreg, _vmask, _ereg, _emask)		\
	{									\
		.name			= (_match),				\
		.of_match		= of_match_ptr(_match),			\
		.regulators_node	= of_match_ptr("regulators"),		\
		.type			= REGULATOR_VOLTAGE,			\
		.id			= AXP15060_ID_##_id,			\
		.n_voltages		= (_n_voltages),			\
		.owner			= THIS_MODULE,				\
		.vsel_reg		= (_vreg),				\
		.vsel_mask		= (_vmask),				\
		.enable_reg		= (_ereg),				\
		.enable_mask		= (_emask),				\
		.linear_ranges		= (_ranges),				\
		.n_linear_ranges	= ARRAY_SIZE(_ranges),			\
		.ops			= &axp15060_range_ops,			\
	}
/* Only register the regulators that needed to be controlled(onoff/vol) */
static const struct regulator_desc axp15060_regulators[] = {
	AXP15060_DESC(ALDO1, "mipi_0p9", 700, 3300, 100,
			AXP15060_VOL_CTRL_ALDO_1, AXP15060_ALDO1_V_OUT_MASK,
			AXP15060_ON_OFF_CTRL_2, AXP15060_PWR_OUT_ALDO1_MASK),

	AXP15060_DESC(ALDO3, "hdmi_1p8", 700, 3300, 100,
			AXP15060_VOL_CTRL_ALDO_3, AXP15060_ALDO3_V_OUT_MASK,
			AXP15060_ON_OFF_CTRL_2, AXP15060_PWR_OUT_ALDO3_MASK),

	AXP15060_DESC(ALDO4, "sdio_vdd", 700, 3300, 100,
			AXP15060_VOL_CTRL_ALDO_4, AXP15060_ALDO4_V_OUT_MASK,
			AXP15060_ON_OFF_CTRL_2, AXP15060_PWR_OUT_ALDO4_MASK),

	AXP15060_DESC(ALDO5, "hdmi_0p9", 700, 3300, 100,
			AXP15060_VOL_CTRL_ALDO_5, AXP15060_ALDO5_V_OUT_MASK,
			AXP15060_ON_OFF_CTRL_2, AXP15060_PWR_OUT_ALDO5_MASK),

	AXP15060_DESC(DCDC1, "vcc_3v3", 1500, 3400, 100,
		 AXP15060_VOL_CTRL_DCDC_1, AXP15060_DCDC1_V_OUT_MASK,
		 AXP15060_ON_OFF_CTRL_1, AXP15060_PWR_OUT_DCDC1_MASK),

	AXP15060_DESC_RANGES(DCDC2, "cpu_vdd",
				axp15060_dcdc2_ranges, AXP15060_DCDC2_NUM_VOLTAGES,
				AXP15060_VOL_CTRL_DCDC_2, AXP15060_DCDC2_V_OUT_MASK,
				AXP15060_ON_OFF_CTRL_1, AXP15060_PWR_OUT_DCDC2_MASK),
};

static struct of_regulator_match axp15060_matches[] = {
	{ .name = "mipi_0p9", },
	{ .name = "hdmi_1p8", },
	{ .name = "sdio_vdd", },
	{ .name = "hdmi_0p9", },
	{ .name = "vcc_3v3", },
	{ .name = "cpu_vdd", },
};

static int axp15060_i2c_probe(struct i2c_client *i2c)
{
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct device_node *np, *regulators;
	struct regmap *regmap;
	int i, ret;

	np = of_node_get(i2c->dev.of_node);
	if (!np)
		return -EINVAL;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&i2c->dev, "Regulators node not found\n");
		return -EINVAL;
	}

	i = of_regulator_match(&i2c->dev, regulators, axp15060_matches,
				 ARRAY_SIZE(axp15060_matches));
	of_node_put(i2c->dev.of_node);
	if (i < 0) {
		dev_err(&i2c->dev, "Failed to match regulators\n");
		return -EINVAL;
	}

	regmap = devm_regmap_init_i2c(i2c, &axp15060_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	for (i = 0; i < AXP15060_MAX_REGULATORS; i++) {
		config.dev = &i2c->dev;
		config.regmap = regmap;
		config.init_data = axp15060_matches[i].init_data;

		rdev = devm_regulator_register(&i2c->dev,
			&axp15060_regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&i2c->dev,
				"Failed to register AXP15060 regulator\n");
			return PTR_ERR(rdev);
		}
		dev_info(&i2c->dev, "Register %s done! vol range:%d ~ %d mV\n",
				rdev->desc->name,
				(rdev->constraints->min_uV) / 1000,
				(rdev->constraints->max_uV) / 1000);
	}

	return 0;
}

static const struct i2c_device_id axp15060_i2c_id[] = {
	{"axp15060_reg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, axp15060_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id axp15060_dt_ids[] = {
	{ .compatible = "stf,axp15060-regulator",
	  .data = &axp15060_i2c_id[0] },
	{},
};
MODULE_DEVICE_TABLE(of, axp15060_dt_ids);
#endif

static struct i2c_driver axp15060_regulator_driver = {
	.driver = {
		.name = "axp15060-regulator",
		.of_match_table = of_match_ptr(axp15060_dt_ids),
	},
	.probe_new = axp15060_i2c_probe,
	.id_table = axp15060_i2c_id,
};

module_i2c_driver(axp15060_regulator_driver);

MODULE_AUTHOR("Kevin Xie <kevin.xie@starfivetech.com>");
MODULE_DESCRIPTION("Regulator device driver for X-Powers AXP15060");
MODULE_LICENSE("GPL v2");
