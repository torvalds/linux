// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Analog Devices, Inc.
 * ADI Regulator driver for the MAX77857
 * MAX77859 and MAX77831.
 */
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/util_macros.h>

#define MAX77857_REG_INT_SRC		0x10
#define MAX77857_REG_INT_MASK		0x11
#define MAX77857_REG_CONT1		0x12
#define MAX77857_REG_CONT2		0x13
#define MAX77857_REG_CONT3		0x14

#define MAX77857_INT_SRC_OCP		BIT(0)
#define MAX77857_INT_SRC_THS		BIT(1)
#define MAX77857_INT_SRC_HARDSHORT	BIT(2)
#define MAX77857_INT_SRC_OVP		BIT(3)
#define MAX77857_INT_SRC_POK		BIT(4)

#define MAX77857_ILIM_MASK		GENMASK(2, 0)
#define MAX77857_CONT1_FREQ		GENMASK(4, 3)
#define MAX77857_CONT3_FPWM		BIT(5)

#define MAX77859_REG_INT_SRC		0x11
#define MAX77859_REG_CONT1		0x13
#define MAX77859_REG_CONT2		0x14
#define MAX77859_REG_CONT3		0x15
#define MAX77859_REG_CONT5		0x17
#define MAX77859_CONT2_FPWM		BIT(2)
#define MAX77859_CONT2_INTB		BIT(3)
#define MAX77859_CONT3_DVS_START	BIT(2)
#define MAX77859_VOLTAGE_SEL_MASK	GENMASK(9, 0)

#define MAX77859_CURRENT_MIN		1000000
#define MAX77859_CURRENT_MAX		5000000
#define MAX77859_CURRENT_STEP		50000

enum max77857_id {
	ID_MAX77831 = 1,
	ID_MAX77857,
	ID_MAX77859,
	ID_MAX77859A,
};

static bool max77857_volatile_reg(struct device *dev, unsigned int reg)
{
	enum max77857_id id = (uintptr_t)dev_get_drvdata(dev);

	switch (id) {
	case ID_MAX77831:
	case ID_MAX77857:
		return reg == MAX77857_REG_INT_SRC;
	case ID_MAX77859:
	case ID_MAX77859A:
		return reg == MAX77859_REG_INT_SRC;
	default:
		return true;
	}
}

static struct regmap_config max77857_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max77857_volatile_reg,
};

static int max77857_get_status(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, MAX77857_REG_INT_SRC, &val);
	if (ret)
		return ret;

	if (FIELD_GET(MAX77857_INT_SRC_POK, val))
		return REGULATOR_STATUS_ON;

	return REGULATOR_STATUS_ERROR;
}

static unsigned int max77857_get_mode(struct regulator_dev *rdev)
{
	enum max77857_id id = (uintptr_t)rdev_get_drvdata(rdev);
	unsigned int regval;
	int ret;

	switch (id) {
	case ID_MAX77831:
	case ID_MAX77857:
		ret = regmap_read(rdev->regmap, MAX77857_REG_CONT3, &regval);
		if (ret)
			return ret;

		if (FIELD_GET(MAX77857_CONT3_FPWM, regval))
			return REGULATOR_MODE_FAST;

		break;
	case ID_MAX77859:
	case ID_MAX77859A:
		ret = regmap_read(rdev->regmap, MAX77859_REG_CONT2, &regval);
		if (ret)
			return ret;

		if (FIELD_GET(MAX77859_CONT2_FPWM, regval))
			return REGULATOR_MODE_FAST;

		break;
	default:
		return -EINVAL;
	}

	return REGULATOR_MODE_NORMAL;
}

static int max77857_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	enum max77857_id id = (uintptr_t)rdev_get_drvdata(rdev);
	unsigned int reg, val;

	switch (id) {
	case ID_MAX77831:
	case ID_MAX77857:
		reg = MAX77857_REG_CONT3;
		val = MAX77857_CONT3_FPWM;
		break;
	case ID_MAX77859:
	case ID_MAX77859A:
		reg = MAX77859_REG_CONT2;
		val = MAX77859_CONT2_FPWM;
		break;
	default:
		return -EINVAL;
	}

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_set_bits(rdev->regmap, reg, val);
	case REGULATOR_MODE_NORMAL:
		return regmap_clear_bits(rdev->regmap, reg, val);
	default:
		return -EINVAL;
	}
}

static int max77857_get_error_flags(struct regulator_dev *rdev,
				    unsigned int *flags)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, MAX77857_REG_INT_SRC, &val);
	if (ret)
		return ret;

	*flags = 0;

	if (FIELD_GET(MAX77857_INT_SRC_OVP, val))
		*flags |= REGULATOR_ERROR_OVER_VOLTAGE_WARN;

	if (FIELD_GET(MAX77857_INT_SRC_OCP, val) ||
	    FIELD_GET(MAX77857_INT_SRC_HARDSHORT, val))
		*flags |= REGULATOR_ERROR_OVER_CURRENT;

	if (FIELD_GET(MAX77857_INT_SRC_THS, val))
		*flags |= REGULATOR_ERROR_OVER_TEMP;

	if (!FIELD_GET(MAX77857_INT_SRC_POK, val))
		*flags |= REGULATOR_ERROR_FAIL;

	return 0;
}

static struct linear_range max77859_lin_ranges[] = {
	REGULATOR_LINEAR_RANGE(3200000, 0x0A0, 0x320, 20000)
};

static const unsigned int max77859_ramp_table[4] = {
	1000, 500, 250, 125
};

static int max77859_set_voltage_sel(struct regulator_dev *rdev,
				    unsigned int sel)
{
	__be16 reg;
	int ret;

	reg = cpu_to_be16(sel);

	ret = regmap_bulk_write(rdev->regmap, MAX77859_REG_CONT3, &reg, 2);
	if (ret)
		return ret;

	/* actually apply new voltage */
	return regmap_set_bits(rdev->regmap, MAX77859_REG_CONT3,
			       MAX77859_CONT3_DVS_START);
}

static int max77859_get_voltage_sel(struct regulator_dev *rdev)
{
	__be16 reg;
	int ret;

	ret = regmap_bulk_read(rdev->regmap, MAX77859_REG_CONT3, &reg, 2);
	if (ret)
		return ret;

	return FIELD_GET(MAX77859_VOLTAGE_SEL_MASK, __be16_to_cpu(reg));
}

static int max77859_set_current_limit(struct regulator_dev *rdev, int min_uA, int max_uA)
{
	u32 selector;

	if (max_uA < MAX77859_CURRENT_MIN)
		return -EINVAL;

	selector = 0x12 + (max_uA - MAX77859_CURRENT_MIN) / MAX77859_CURRENT_STEP;

	selector = clamp_val(selector, 0x00, 0x7F);

	return regmap_write(rdev->regmap, MAX77859_REG_CONT5, selector);
}

static int max77859_get_current_limit(struct regulator_dev *rdev)
{
	u32 selector;
	int ret;

	ret = regmap_read(rdev->regmap, MAX77859_REG_CONT5, &selector);
	if (ret)
		return ret;

	if (selector <= 0x12)
		return MAX77859_CURRENT_MIN;

	if (selector >= 0x64)
		return MAX77859_CURRENT_MAX;

	return MAX77859_CURRENT_MIN + (selector - 0x12) * MAX77859_CURRENT_STEP;
}

static const struct regulator_ops max77859_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = max77859_set_voltage_sel,
	.get_voltage_sel = max77859_get_voltage_sel,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.get_status = max77857_get_status,
	.set_mode = max77857_set_mode,
	.get_mode = max77857_get_mode,
	.get_error_flags = max77857_get_error_flags,
};

static const struct regulator_ops max77859a_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = max77859_set_voltage_sel,
	.get_voltage_sel = max77859_get_voltage_sel,
	.set_current_limit = max77859_set_current_limit,
	.get_current_limit = max77859_get_current_limit,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.get_status = max77857_get_status,
	.set_mode = max77857_set_mode,
	.get_mode = max77857_get_mode,
	.get_error_flags = max77857_get_error_flags,
};

static const struct regulator_ops max77857_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_ramp_delay = regulator_set_ramp_delay_regmap,
	.get_status = max77857_get_status,
	.set_mode = max77857_set_mode,
	.get_mode = max77857_get_mode,
	.get_error_flags = max77857_get_error_flags,
};

static struct linear_range max77857_lin_ranges[] = {
	REGULATOR_LINEAR_RANGE(4485000, 0x3D, 0xCC, 73500)
};

static const unsigned int max77857_switch_freq[] = {
	1200000, 1500000, 1800000, 2100000
};

#define RAMAP_DELAY_INIT_VAL 1333

static const unsigned int max77857_ramp_table[2][4] = {
	{ RAMAP_DELAY_INIT_VAL, 667, 333, 227 }, /* when switch freq is 1.8MHz or 2.1MHz */
	{ 1166, 667, 333, 167 }, /* when switch freq is 1.2MHz or 1.5MHz */
};

static struct regulator_desc max77857_regulator_desc = {
	.ops = &max77857_regulator_ops,
	.name = "max77857",
	.linear_ranges = max77857_lin_ranges,
	.n_linear_ranges = ARRAY_SIZE(max77857_lin_ranges),
	.vsel_mask = 0xFF,
	.vsel_reg = MAX77857_REG_CONT2,
	.ramp_delay_table = max77857_ramp_table[0],
	.n_ramp_values = ARRAY_SIZE(max77857_ramp_table[0]),
	.ramp_reg = MAX77857_REG_CONT3,
	.ramp_mask = GENMASK(1, 0),
	.ramp_delay = RAMAP_DELAY_INIT_VAL,
	.owner = THIS_MODULE,
};

static void max77857_calc_range(struct device *dev, enum max77857_id id)
{
	struct linear_range *range;
	unsigned long vref_step;
	u32 rtop = 0;
	u32 rbot = 0;

	device_property_read_u32(dev, "adi,rtop-ohms", &rtop);
	device_property_read_u32(dev, "adi,rbot-ohms", &rbot);

	if (!rbot || !rtop)
		return;

	switch (id) {
	case ID_MAX77831:
	case ID_MAX77857:
		range = max77857_lin_ranges;
		vref_step = 4900UL;
		break;
	case ID_MAX77859:
	case ID_MAX77859A:
		range = max77859_lin_ranges;
		vref_step = 1250UL;
		break;
	}

	range->step = DIV_ROUND_CLOSEST(vref_step * (rbot + rtop), rbot);
	range->min = range->step * range->min_sel;
}

static int max77857_probe(struct i2c_client *client)
{
	const struct i2c_device_id *i2c_id;
	struct device *dev = &client->dev;
	struct regulator_config cfg = { };
	struct regulator_dev *rdev;
	struct regmap *regmap;
	enum max77857_id id;
	u32 switch_freq = 0;
	int ret;

	i2c_id = i2c_client_get_device_id(client);
	if (!i2c_id)
		return -EINVAL;

	id = i2c_id->driver_data;

	dev_set_drvdata(dev, (void *)id);

	if (id == ID_MAX77859 || id == ID_MAX77859A) {
		max77857_regulator_desc.ops = &max77859_regulator_ops;
		max77857_regulator_desc.linear_ranges = max77859_lin_ranges;
		max77857_regulator_desc.ramp_delay_table = max77859_ramp_table;
		max77857_regulator_desc.ramp_delay = max77859_ramp_table[0];
	}

	if (id == ID_MAX77859A)
		max77857_regulator_desc.ops = &max77859a_regulator_ops;

	max77857_calc_range(dev, id);

	regmap = devm_regmap_init_i2c(client, &max77857_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "cannot initialize regmap\n");

	device_property_read_u32(dev, "adi,switch-frequency-hz", &switch_freq);
	if (switch_freq) {
		switch_freq = find_closest(switch_freq, max77857_switch_freq,
					   ARRAY_SIZE(max77857_switch_freq));

		if (id == ID_MAX77831 && switch_freq == 3)
			switch_freq = 2;

		switch (id) {
		case ID_MAX77831:
		case ID_MAX77857:
			ret = regmap_update_bits(regmap, MAX77857_REG_CONT1,
						 MAX77857_CONT1_FREQ, switch_freq);

			if (switch_freq >= 2)
				break;

			max77857_regulator_desc.ramp_delay_table = max77857_ramp_table[1];
			max77857_regulator_desc.ramp_delay = max77857_ramp_table[1][0];
			break;
		case ID_MAX77859:
		case ID_MAX77859A:
			ret = regmap_update_bits(regmap, MAX77859_REG_CONT1,
						 MAX77857_CONT1_FREQ, switch_freq);
			break;
		}
		if (ret)
			return ret;
	}

	cfg.dev = dev;
	cfg.driver_data = (void *)id;
	cfg.regmap = regmap;
	cfg.init_data = of_get_regulator_init_data(dev, dev->of_node,
						   &max77857_regulator_desc);
	if (!cfg.init_data)
		return -ENOMEM;

	rdev = devm_regulator_register(dev, &max77857_regulator_desc, &cfg);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev),
				     "cannot register regulator\n");

	return 0;
}

const struct i2c_device_id max77857_id[] = {
	{ "max77831", ID_MAX77831 },
	{ "max77857", ID_MAX77857 },
	{ "max77859", ID_MAX77859 },
	{ "max77859a", ID_MAX77859A },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max77857_id);

static const struct of_device_id max77857_of_id[] = {
	{ .compatible = "adi,max77831", .data = (void *)ID_MAX77831 },
	{ .compatible = "adi,max77857", .data = (void *)ID_MAX77857 },
	{ .compatible = "adi,max77859", .data = (void *)ID_MAX77859 },
	{ .compatible = "adi,max77859a", .data = (void *)ID_MAX77859A },
	{ }
};
MODULE_DEVICE_TABLE(of, max77857_of_id);

static struct i2c_driver max77857_driver = {
	.driver = {
		.name = "max77857",
		.of_match_table = max77857_of_id,
	},
	.id_table = max77857_id,
	.probe = max77857_probe,
};
module_i2c_driver(max77857_driver);

MODULE_DESCRIPTION("Analog Devices MAX77857 Buck-Boost Converter Driver");
MODULE_AUTHOR("Ibrahim Tilki <Ibrahim.Tilki@analog.com>");
MODULE_AUTHOR("Okan Sahin <Okan.Sahin@analog.com>");
MODULE_LICENSE("GPL");
