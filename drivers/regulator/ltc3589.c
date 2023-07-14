// SPDX-License-Identifier: GPL-2.0
//
// Linear Technology LTC3589,LTC3589-1 regulator support
//
// Copyright (c) 2014 Philipp Zabel <p.zabel@pengutronix.de>, Pengutronix

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
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

#define LTC3589_VRRCR_SW1_RAMP_MASK	GENMASK(1, 0)
#define LTC3589_VRRCR_SW2_RAMP_MASK	GENMASK(3, 2)
#define LTC3589_VRRCR_SW3_RAMP_MASK	GENMASK(5, 4)
#define LTC3589_VRRCR_LDO2_RAMP_MASK	GENMASK(7, 6)

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

struct ltc3589 {
	struct regmap *regmap;
	struct device *dev;
	enum ltc3589_variant variant;
	struct regulator_desc regulator_descs[LTC3589_NUM_REGULATORS];
	struct regulator_dev *regulators[LTC3589_NUM_REGULATORS];
};

static const int ltc3589_ldo4[] = {
	2800000, 2500000, 1800000, 3300000,
};

static const int ltc3589_12_ldo4[] = {
	1200000, 1800000, 2500000, 3200000,
};

static const unsigned int ltc3589_ramp_table[] = {
	880, 1750, 3500, 7000
};

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
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
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

static inline unsigned int ltc3589_scale(unsigned int uV, u32 r1, u32 r2)
{
	uint64_t tmp;

	if (uV == 0)
		return 0;

	tmp = (uint64_t)uV * r1;
	do_div(tmp, r2);
	return uV + (unsigned int)tmp;
}

static int ltc3589_of_parse_cb(struct device_node *np,
			       const struct regulator_desc *desc,
			       struct regulator_config *config)
{
	struct ltc3589 *ltc3589 = config->driver_data;
	struct regulator_desc *rdesc = &ltc3589->regulator_descs[desc->id];
	u32 r[2];
	int ret;

	/* Parse feedback voltage dividers. LDO3 and LDO4 don't have them */
	if (desc->id >= LTC3589_LDO3)
		return 0;

	ret = of_property_read_u32_array(np, "lltc,fb-voltage-divider", r, 2);
	if (ret) {
		dev_err(ltc3589->dev, "Failed to parse voltage divider: %d\n",
			ret);
		return ret;
	}

	if (!r[0] || !r[1])
		return 0;

	rdesc->min_uV = ltc3589_scale(desc->min_uV, r[0], r[1]);
	rdesc->uV_step = ltc3589_scale(desc->uV_step, r[0], r[1]);
	rdesc->fixed_uV = ltc3589_scale(desc->fixed_uV, r[0], r[1]);

	return 0;
}

#define LTC3589_REG(_name, _of_name, _ops, en_bit, dtv1_reg, dtv_mask)	\
	[LTC3589_ ## _name] = {						\
		.name = #_name,						\
		.of_match = of_match_ptr(#_of_name),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.of_parse_cb = ltc3589_of_parse_cb,			\
		.n_voltages = (dtv_mask) + 1,				\
		.fixed_uV = (dtv_mask) ? 0 : 800000,			\
		.ops = &ltc3589_ ## _ops ## _regulator_ops,		\
		.type = REGULATOR_VOLTAGE,				\
		.id = LTC3589_ ## _name,				\
		.owner = THIS_MODULE,					\
		.vsel_reg = (dtv1_reg),					\
		.vsel_mask = (dtv_mask),				\
		.enable_reg = (en_bit) ? LTC3589_OVEN : 0,		\
		.enable_mask = (en_bit),				\
	}

#define LTC3589_LINEAR_REG(_name, _of_name, _dtv1)			\
	[LTC3589_ ## _name] = {						\
		.name = #_name,						\
		.of_match = of_match_ptr(#_of_name),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.of_parse_cb = ltc3589_of_parse_cb,			\
		.n_voltages = 32,					\
		.min_uV = 362500,					\
		.uV_step = 12500,					\
		.ramp_delay = 1750,					\
		.ops = &ltc3589_linear_regulator_ops,			\
		.type = REGULATOR_VOLTAGE,				\
		.id = LTC3589_ ## _name,				\
		.owner = THIS_MODULE,					\
		.vsel_reg = LTC3589_ ## _dtv1,				\
		.vsel_mask = 0x1f,					\
		.apply_reg = LTC3589_VCCR,				\
		.apply_bit = LTC3589_VCCR_ ## _name ## _GO,		\
		.enable_reg = LTC3589_OVEN,				\
		.enable_mask = (LTC3589_OVEN_ ## _name),		\
		.ramp_reg = LTC3589_VRRCR,				\
		.ramp_mask = LTC3589_VRRCR_ ## _name ## _RAMP_MASK,	\
		.ramp_delay_table = ltc3589_ramp_table,			\
		.n_ramp_values = ARRAY_SIZE(ltc3589_ramp_table),	\
	}


#define LTC3589_FIXED_REG(_name, _of_name)				\
	LTC3589_REG(_name, _of_name, fixed, LTC3589_OVEN_ ## _name, 0, 0)

static const struct regulator_desc ltc3589_regulators[] = {
	LTC3589_LINEAR_REG(SW1, sw1, B1DTV1),
	LTC3589_LINEAR_REG(SW2, sw2, B2DTV1),
	LTC3589_LINEAR_REG(SW3, sw3, B3DTV1),
	LTC3589_FIXED_REG(BB_OUT, bb-out),
	LTC3589_REG(LDO1, ldo1, fixed_standby, 0, 0, 0),
	LTC3589_LINEAR_REG(LDO2, ldo2, L2DTV1),
	LTC3589_FIXED_REG(LDO3, ldo3),
	LTC3589_REG(LDO4, ldo4, table, LTC3589_OVEN_LDO4, LTC3589_L2DTV2, 0x60),
};

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
	.cache_type = REGCACHE_MAPLE,
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

static int ltc3589_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	struct regulator_desc *descs;
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
		descs[LTC3589_LDO3].fixed_uV = 1800000;
		descs[LTC3589_LDO4].volt_table = ltc3589_ldo4;
	} else {
		descs[LTC3589_LDO3].fixed_uV = 2800000;
		descs[LTC3589_LDO4].volt_table = ltc3589_12_ldo4;
	}

	ltc3589->regmap = devm_regmap_init_i2c(client, &ltc3589_regmap_config);
	if (IS_ERR(ltc3589->regmap)) {
		ret = PTR_ERR(ltc3589->regmap);
		dev_err(dev, "failed to initialize regmap: %d\n", ret);
		return ret;
	}

	for (i = 0; i < LTC3589_NUM_REGULATORS; i++) {
		struct regulator_desc *desc = &ltc3589->regulator_descs[i];
		struct regulator_config config = { };

		config.dev = dev;
		config.driver_data = ltc3589;

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

static const struct of_device_id __maybe_unused ltc3589_of_match[] = {
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
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(ltc3589_of_match),
	},
	.probe = ltc3589_probe,
	.id_table = ltc3589_i2c_id,
};
module_i2c_driver(ltc3589_driver);

MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_DESCRIPTION("Regulator driver for Linear Technology LTC3589(-1,2)");
MODULE_LICENSE("GPL v2");
