/*
 * Linear Technology LTC3589,LTC3589-1 regulator support
 *
 * Copyright (c) 2014 Philipp Zabel <p.zabel@pengutronix.de>, Pengutronix
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define DRIVER_NAME		"ltc3589"

#define LTC3589_IRQSTAT		0x02
#define LTC3589_SCR1		0x07
#define LTC3589_OVEN		0x10
#define LTC3589_SCR2		0x12
#define LTC3589_PGSTAT		0x13
#define LTC3589_VCCR		0x20
#define LTC3589_CLIRQ		0x21
#define LTC3589_B1DTV1		0x23
#define LTC3589_B1DTV2		0x24
#define LTC3589_VRRCR		0x25
#define LTC3589_B2DTV1		0x26
#define LTC3589_B2DTV2		0x27
#define LTC3589_B3DTV1		0x29
#define LTC3589_B3DTV2		0x2a
#define LTC3589_L2DTV1		0x32
#define LTC3589_L2DTV2		0x33

#define LTC3589_IRQSTAT_PGOOD_TIMEOUT	BIT(3)
#define LTC3589_IRQSTAT_UNDERVOLT_WARN	BIT(4)
#define LTC3589_IRQSTAT_UNDERVOLT_FAULT	BIT(5)
#define LTC3589_IRQSTAT_THERMAL_WARN	BIT(6)
#define LTC3589_IRQSTAT_THERMAL_FAULT	BIT(7)

#define LTC3589_OVEN_SW1		BIT(0)
#define LTC3589_OVEN_SW2		BIT(1)
#define LTC3589_OVEN_SW3		BIT(2)
#define LTC3589_OVEN_BB_OUT		BIT(3)
#define LTC3589_OVEN_LDO2		BIT(4)
#define LTC3589_OVEN_LDO3		BIT(5)
#define LTC3589_OVEN_LDO4		BIT(6)
#define LTC3589_OVEN_SW_CTRL		BIT(7)

#define LTC3589_VCCR_SW1_GO		BIT(0)
#define LTC3589_VCCR_SW2_GO		BIT(2)
#define LTC3589_VCCR_SW3_GO		BIT(4)
#define LTC3589_VCCR_LDO2_GO		BIT(6)

enum ltc3589_variant {
	LTC3589,
	LTC3589_1,
	LTC3589_2,
};

enum ltc3589_reg {
	LTC3589_SW1,
	LTC3589_SW2,
	LTC3589_SW3,
	LTC3589_BB_OUT,
	LTC3589_LDO1,
	LTC3589_LDO2,
	LTC3589_LDO3,
	LTC3589_LDO4,
	LTC3589_NUM_REGULATORS,
};

struct ltc3589_regulator {
	struct regulator_desc desc;

	/* External feedback voltage divider */
	unsigned int r1;
	unsigned int r2;
};

struct ltc3589 {
	struct regmap *regmap;
	struct device *dev;
	enum ltc3589_variant variant;
	struct ltc3589_regulator regulator_descs[LTC3589_NUM_REGULATORS];
	struct regulator_dev *regulators[LTC3589_NUM_REGULATORS];
};

static const int ltc3589_ldo4[] = {
	2800000, 2500000, 1800000, 3300000,
};

static const int ltc3589_12_ldo4[] = {
	1200000, 1800000, 2500000, 3200000,
};

static int ltc3589_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct ltc3589 *ltc3589 = rdev_get_drvdata(rdev);
	int sel, shift;

	if (unlikely(ramp_delay <= 0))
		return -EINVAL;

	/* VRRCR slew rate offsets are the same as VCCR go bit offsets */
	shift = ffs(rdev->desc->apply_bit) - 1;

	/* The slew rate can be set to 0.88, 1.75, 3.5, or 7 mV/uS */
	for (sel = 0; sel < 4; sel++) {
		if ((880 << sel) >= ramp_delay) {
			return regmap_update_bits(ltc3589->regmap,
						  LTC3589_VRRCR,
						  0x3 << shift, sel << shift);
		}
	}
	return -EINVAL;
}

static int ltc3589_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct ltc3589 *ltc3589 = rdev_get_drvdata(rdev);
	int sel;

	sel = regulator_map_voltage_linear(rdev, uV, uV);
	if (sel < 0)
		return sel;

	/* DTV2 register follows right after the corresponding DTV1 register */
	return regmap_update_bits(ltc3589->regmap, rdev->desc->vsel_reg + 1,
				  rdev->desc->vsel_mask, sel);
}

static int ltc3589_set_suspend_mode(struct regulator_dev *rdev,
				    unsigned int mode)
{
	struct ltc3589 *ltc3589 = rdev_get_drvdata(rdev);
	int mask, bit = 0;

	/* VCCR reference selects are right next to the VCCR go bits */
	mask = rdev->desc->apply_bit << 1;

	if (mode == REGULATOR_MODE_STANDBY)
		bit = mask;	/* Select DTV2 */

	mask |= rdev->desc->apply_bit;
	bit |= rdev->desc->apply_bit;
	return regmap_update_bits(ltc3589->regmap, LTC3589_VCCR, mask, bit);
}

/* SW1, SW2, SW3, LDO2 */
static const struct regulator_ops ltc3589_linear_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_ramp_delay = ltc3589_set_ramp_delay,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.set_suspend_voltage = ltc3589_set_suspend_voltage,
	.set_suspend_mode = ltc3589_set_suspend_mode,
};

/* BB_OUT, LDO3 */
static const struct regulator_ops ltc3589_fixed_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

/* LDO1 */
static const struct regulator_ops ltc3589_fixed_standby_regulator_ops = {
};

/* LDO4 */
static const struct regulator_ops ltc3589_table_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.list_voltage = regulator_list_voltage_table,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
};


#define LTC3589_REG(_name, _ops, en_bit, dtv1_reg, dtv_mask, go_bit)	\
	[LTC3589_ ## _name] = {						\
		.desc = {						\
			.name = #_name,					\
			.n_voltages = (dtv_mask) + 1,			\
			.min_uV = (go_bit) ? 362500 : 0,		\
			.uV_step = (go_bit) ? 12500 : 0,		\
			.ramp_delay = (go_bit) ? 1750 : 0,		\
			.fixed_uV = (dtv_mask) ? 0 : 800000,		\
			.ops = &ltc3589_ ## _ops ## _regulator_ops,	\
			.type = REGULATOR_VOLTAGE,			\
			.id = LTC3589_ ## _name,			\
			.owner = THIS_MODULE,				\
			.vsel_reg = (dtv1_reg),			\
			.vsel_mask = (dtv_mask),			\
			.apply_reg = (go_bit) ? LTC3589_VCCR : 0,	\
			.apply_bit = (go_bit),				\
			.enable_reg = (en_bit) ? LTC3589_OVEN : 0,	\
			.enable_mask = (en_bit),			\
		},							\
	}

#define LTC3589_LINEAR_REG(_name, _dtv1)				\
	LTC3589_REG(_name, linear, LTC3589_OVEN_ ## _name,		\
		    LTC3589_ ## _dtv1, 0x1f,				\
		    LTC3589_VCCR_ ## _name ## _GO)

#define LTC3589_FIXED_REG(_name) \
	LTC3589_REG(_name, fixed, LTC3589_OVEN_ ## _name, 0, 0, 0)

static struct ltc3589_regulator ltc3589_regulators[LTC3589_NUM_REGULATORS] = {
	LTC3589_LINEAR_REG(SW1, B1DTV1),
	LTC3589_LINEAR_REG(SW2, B2DTV1),
	LTC3589_LINEAR_REG(SW3, B3DTV1),
	LTC3589_FIXED_REG(BB_OUT),
	LTC3589_REG(LDO1, fixed_standby, 0, 0, 0, 0),
	LTC3589_LINEAR_REG(LDO2, L2DTV1),
	LTC3589_FIXED_REG(LDO3),
	LTC3589_REG(LDO4, table, LTC3589_OVEN_LDO4, LTC3589_L2DTV2, 0x60, 0),
};

#ifdef CONFIG_OF
static struct of_regulator_match ltc3589_matches[LTC3589_NUM_REGULATORS] = {
	{ .name = "sw1",    },
	{ .name = "sw2",    },
	{ .name = "sw3",    },
	{ .name = "bb-out", },
	{ .name = "ldo1",   }, /* standby */
	{ .name = "ldo2",   },
	{ .name = "ldo3",   },
	{ .name = "ldo4",   },
};

static int ltc3589_parse_regulators_dt(struct ltc3589 *ltc3589)
{
	struct device *dev = ltc3589->dev;
	struct device_node *node;
	int i, ret;

	node = of_get_child_by_name(dev->of_node, "regulators");
	if (!node) {
		dev_err(dev, "regulators node not found\n");
		return -EINVAL;
	}

	ret = of_regulator_match(dev, node, ltc3589_matches,
				 ARRAY_SIZE(ltc3589_matches));
	of_node_put(node);
	if (ret < 0) {
		dev_err(dev, "Error parsing regulator init data: %d\n", ret);
		return ret;
	}
	if (ret != LTC3589_NUM_REGULATORS) {
		dev_err(dev, "Only %d regulators described in device tree\n",
			ret);
		return -EINVAL;
	}

	/* Parse feedback voltage dividers. LDO3 and LDO4 don't have them */
	for (i = 0; i < LTC3589_LDO3; i++) {
		struct ltc3589_regulator *desc = &ltc3589->regulator_descs[i];
		struct device_node *np = ltc3589_matches[i].of_node;
		u32 vdiv[2];

		ret = of_property_read_u32_array(np, "lltc,fb-voltage-divider",
						 vdiv, 2);
		if (ret) {
			dev_err(dev, "Failed to parse voltage divider: %d\n",
				ret);
			return ret;
		}

		desc->r1 = vdiv[0];
		desc->r2 = vdiv[1];
	}

	return 0;
}

static inline struct regulator_init_data *match_init_data(int index)
{
	return ltc3589_matches[index].init_data;
}

static inline struct device_node *match_of_node(int index)
{
	return ltc3589_matches[index].of_node;
}
#else
static inline int ltc3589_parse_regulators_dt(struct ltc3589 *ltc3589)
{
	return 0;
}

static inline struct regulator_init_data *match_init_data(int index)
{
	return NULL;
}

static inline struct device_node *match_of_node(int index)
{
	return NULL;
}
#endif

static bool ltc3589_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTC3589_IRQSTAT:
	case LTC3589_SCR1:
	case LTC3589_OVEN:
	case LTC3589_SCR2:
	case LTC3589_VCCR:
	case LTC3589_CLIRQ:
	case LTC3589_B1DTV1:
	case LTC3589_B1DTV2:
	case LTC3589_VRRCR:
	case LTC3589_B2DTV1:
	case LTC3589_B2DTV2:
	case LTC3589_B3DTV1:
	case LTC3589_B3DTV2:
	case LTC3589_L2DTV1:
	case LTC3589_L2DTV2:
		return true;
	}
	return false;
}

static bool ltc3589_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTC3589_IRQSTAT:
	case LTC3589_SCR1:
	case LTC3589_OVEN:
	case LTC3589_SCR2:
	case LTC3589_PGSTAT:
	case LTC3589_VCCR:
	case LTC3589_B1DTV1:
	case LTC3589_B1DTV2:
	case LTC3589_VRRCR:
	case LTC3589_B2DTV1:
	case LTC3589_B2DTV2:
	case LTC3589_B3DTV1:
	case LTC3589_B3DTV2:
	case LTC3589_L2DTV1:
	case LTC3589_L2DTV2:
		return true;
	}
	return false;
}

static bool ltc3589_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTC3589_IRQSTAT:
	case LTC3589_PGSTAT:
	case LTC3589_VCCR:
		return true;
	}
	return false;
}

static const struct reg_default ltc3589_reg_defaults[] = {
	{ LTC3589_SCR1,   0x00 },
	{ LTC3589_OVEN,   0x00 },
	{ LTC3589_SCR2,   0x00 },
	{ LTC3589_VCCR,   0x00 },
	{ LTC3589_B1DTV1, 0x19 },
	{ LTC3589_B1DTV2, 0x19 },
	{ LTC3589_VRRCR,  0xff },
	{ LTC3589_B2DTV1, 0x19 },
	{ LTC3589_B2DTV2, 0x19 },
	{ LTC3589_B3DTV1, 0x19 },
	{ LTC3589_B3DTV2, 0x19 },
	{ LTC3589_L2DTV1, 0x19 },
	{ LTC3589_L2DTV2, 0x19 },
};

static const struct regmap_config ltc3589_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = ltc3589_writeable_reg,
	.readable_reg = ltc3589_readable_reg,
	.volatile_reg = ltc3589_volatile_reg,
	.max_register = LTC3589_L2DTV2,
	.reg_defaults = ltc3589_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ltc3589_reg_defaults),
	.use_single_read = true,
	.use_single_write = true,
	.cache_type = REGCACHE_RBTREE,
};


static irqreturn_t ltc3589_isr(int irq, void *dev_id)
{
	struct ltc3589 *ltc3589 = dev_id;
	unsigned int i, irqstat, event;

	regmap_read(ltc3589->regmap, LTC3589_IRQSTAT, &irqstat);

	if (irqstat & LTC3589_IRQSTAT_THERMAL_WARN) {
		event = REGULATOR_EVENT_OVER_TEMP;
		for (i = 0; i < LTC3589_NUM_REGULATORS; i++)
			regulator_notifier_call_chain(ltc3589->regulators[i],
						      event, NULL);
	}

	if (irqstat & LTC3589_IRQSTAT_UNDERVOLT_WARN) {
		event = REGULATOR_EVENT_UNDER_VOLTAGE;
		for (i = 0; i < LTC3589_NUM_REGULATORS; i++)
			regulator_notifier_call_chain(ltc3589->regulators[i],
						      event, NULL);
	}

	/* Clear warning condition */
	regmap_write(ltc3589->regmap, LTC3589_CLIRQ, 0);

	return IRQ_HANDLED;
}

static inline unsigned int ltc3589_scale(unsigned int uV, u32 r1, u32 r2)
{
	uint64_t tmp;
	if (uV == 0)
		return 0;
	tmp = (uint64_t)uV * r1;
	do_div(tmp, r2);
	return uV + (unsigned int)tmp;
}

static void ltc3589_apply_fb_voltage_divider(struct ltc3589_regulator *rdesc)
{
	struct regulator_desc *desc = &rdesc->desc;

	if (!rdesc->r1 || !rdesc->r2)
		return;

	desc->min_uV = ltc3589_scale(desc->min_uV, rdesc->r1, rdesc->r2);
	desc->uV_step = ltc3589_scale(desc->uV_step, rdesc->r1, rdesc->r2);
	desc->fixed_uV = ltc3589_scale(desc->fixed_uV, rdesc->r1, rdesc->r2);
}

static int ltc3589_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ltc3589_regulator *descs;
	struct ltc3589 *ltc3589;
	int i, ret;

	ltc3589 = devm_kzalloc(dev, sizeof(*ltc3589), GFP_KERNEL);
	if (!ltc3589)
		return -ENOMEM;

	i2c_set_clientdata(client, ltc3589);
	if (client->dev.of_node)
		ltc3589->variant = (enum ltc3589_variant)
			of_device_get_match_data(&client->dev);
	else
		ltc3589->variant = id->driver_data;
	ltc3589->dev = dev;

	descs = ltc3589->regulator_descs;
	memcpy(descs, ltc3589_regulators, sizeof(ltc3589_regulators));
	if (ltc3589->variant == LTC3589) {
		descs[LTC3589_LDO3].desc.fixed_uV = 1800000;
		descs[LTC3589_LDO4].desc.volt_table = ltc3589_ldo4;
	} else {
		descs[LTC3589_LDO3].desc.fixed_uV = 2800000;
		descs[LTC3589_LDO4].desc.volt_table = ltc3589_12_ldo4;
	}

	ltc3589->regmap = devm_regmap_init_i2c(client, &ltc3589_regmap_config);
	if (IS_ERR(ltc3589->regmap)) {
		ret = PTR_ERR(ltc3589->regmap);
		dev_err(dev, "failed to initialize regmap: %d\n", ret);
		return ret;
	}

	ret = ltc3589_parse_regulators_dt(ltc3589);
	if (ret)
		return ret;

	for (i = 0; i < LTC3589_NUM_REGULATORS; i++) {
		struct ltc3589_regulator *rdesc = &ltc3589->regulator_descs[i];
		struct regulator_desc *desc = &rdesc->desc;
		struct regulator_init_data *init_data;
		struct regulator_config config = { };

		init_data = match_init_data(i);

		if (i < LTC3589_LDO3)
			ltc3589_apply_fb_voltage_divider(rdesc);

		config.dev = dev;
		config.init_data = init_data;
		config.driver_data = ltc3589;
		config.of_node = match_of_node(i);

		ltc3589->regulators[i] = devm_regulator_register(dev, desc,
								 &config);
		if (IS_ERR(ltc3589->regulators[i])) {
			ret = PTR_ERR(ltc3589->regulators[i]);
			dev_err(dev, "failed to register regulator %s: %d\n",
				desc->name, ret);
			return ret;
		}
	}

	if (client->irq) {
		ret = devm_request_threaded_irq(dev, client->irq, NULL,
						ltc3589_isr,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						client->name, ltc3589);
		if (ret) {
			dev_err(dev, "Failed to request IRQ: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static const struct i2c_device_id ltc3589_i2c_id[] = {
	{ "ltc3589",   LTC3589   },
	{ "ltc3589-1", LTC3589_1 },
	{ "ltc3589-2", LTC3589_2 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc3589_i2c_id);

static const struct of_device_id ltc3589_of_match[] = {
	{
		.compatible = "lltc,ltc3589",
		.data = (void *)LTC3589,
	},
	{
		.compatible = "lltc,ltc3589-1",
		.data = (void *)LTC3589_1,
	},
	{
		.compatible = "lltc,ltc3589-2",
		.data = (void *)LTC3589_2,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, ltc3589_of_match);

static struct i2c_driver ltc3589_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(ltc3589_of_match),
	},
	.probe = ltc3589_probe,
	.id_table = ltc3589_i2c_id,
};
module_i2c_driver(ltc3589_driver);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("Regulator driver for Linear Technology LTC3589(-1,2)");
MODULE_LICENSE("GPL v2");
