// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Fuzhou Rockchip Electronics Co., Ltd
 */
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

/* Voltage setting */

#define RK860X_VSEL0_A		0x00
#define RK860X_VSEL1_A		0x01
#define RK860X_VSEL0_B		0x06
#define RK860X_VSEL1_B		0x07
#define RK860X_MAX_SET		0x08

/* Control register */
#define RK860X_CONTROL		0x02
/* IC Type */
#define RK860X_ID1		0x03
/* IC mask version */
#define RK860X_ID2		0x04
/* Monitor register */
#define RK860X_MONITOR		0x05

/* VSEL bit definitions */
#define VSEL_BUCK_EN		BIT(7)
#define VSEL_MODE		BIT(6)
#define VSEL_A_NSEL_MASK	0x3F
#define VSEL_B_NSEL_MASK	0xff

/* Chip ID */
#define DIE_ID			0x0f
#define DIE_REV			0x0f
/* Control bit definitions */
#define CTL_OUTPUT_DISCHG	BIT(7)
#define CTL_SLEW_MASK		(0x7 << 4)
#define CTL_SLEW_SHIFT		4
#define CTL_RESET		BIT(2)

#define RK860X_NVOLTAGES_64	64
#define RK860X_NVOLTAGES_160	160

/* IC Type */
enum {
	RK860X_CHIP_ID_00 = 0,
	RK860X_CHIP_ID_01,
	RK860X_CHIP_ID_02,
	RK860X_CHIP_ID_03,
};

struct rk860x_platform_data {
	struct regulator_init_data *regulator;
	unsigned int slew_rate;
	/* Sleep VSEL ID */
	unsigned int sleep_vsel_id;
	int limit_volt;
	struct gpio_desc *vsel_gpio;
};

struct rk860x_device_info {
	struct regmap *regmap;
	struct device *dev;
	struct regulator_desc desc;
	struct regulator_dev *rdev;
	struct regulator_init_data *regulator;
	/* IC Type and Rev */
	int chip_id;
	/* Voltage setting register */
	unsigned int vol_reg;
	unsigned int sleep_reg;
	unsigned int en_reg;
	unsigned int sleep_en_reg;
	unsigned int mode_reg;
	unsigned int vol_mask;
	unsigned int mode_mask;
	unsigned int slew_reg;
	unsigned int slew_mask;
	unsigned int slew_shift;
	/* Voltage range and step(linear) */
	unsigned int vsel_min;
	unsigned int vsel_step;
	unsigned int n_voltages;
	/* Voltage slew rate limiting */
	unsigned int slew_rate;
	struct gpio_desc *vsel_gpio;
	unsigned int sleep_vsel_id;
};

static unsigned int rk860x_map_mode(unsigned int mode)
{
	return mode == REGULATOR_MODE_FAST ?
		REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static int rk860x_get_voltage(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	ret = regmap_read(di->regmap, RK860X_MAX_SET, &val);
	if (ret < 0)
		return ret;
	ret = regulator_get_voltage_sel_regmap(rdev);
	if (ret > val)
		return val;

	return ret;
}

static int rk860x_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_map_voltage_linear(rdev, uV, uV);
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(di->regmap, di->sleep_reg,
				 di->vol_mask, ret);
	if (ret < 0)
		return ret;

	return 0;
}

static int rk860x_set_suspend_enable(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);

	return regmap_update_bits(di->regmap, di->sleep_en_reg,
				  VSEL_BUCK_EN, VSEL_BUCK_EN);
}

static int rk860x_set_suspend_disable(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);

	return regmap_update_bits(di->regmap, di->sleep_en_reg,
				  VSEL_BUCK_EN, 0);
}

static int rk860x_resume(struct regulator_dev *rdev)
{
	int ret;

	if (!rdev->constraints->state_mem.changeable)
		return 0;

	ret = rk860x_set_suspend_enable(rdev);
	if (ret)
		return ret;

	return regulator_suspend_enable(rdev, PM_SUSPEND_MEM);
}

static int rk860x_set_enable(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);

	if (di->vsel_gpio) {
		gpiod_set_raw_value(di->vsel_gpio, !di->sleep_vsel_id);
		return 0;
	}

	return regmap_update_bits(di->regmap, di->en_reg,
				  VSEL_BUCK_EN, VSEL_BUCK_EN);
}

static int rk860x_set_disable(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);

	if (di->vsel_gpio) {
		gpiod_set_raw_value(di->vsel_gpio, di->sleep_vsel_id);
		return 0;
	}

	return regmap_update_bits(di->regmap, di->en_reg,
				  VSEL_BUCK_EN, 0);
}

static int rk860x_is_enabled(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret = 0;

	if (di->vsel_gpio) {
		if (di->sleep_vsel_id)
			return !gpiod_get_raw_value(di->vsel_gpio);
		else
			return gpiod_get_raw_value(di->vsel_gpio);
	}

	ret = regmap_read(di->regmap, di->en_reg, &val);
	if (ret < 0)
		return ret;
	if (val & VSEL_BUCK_EN)
		return 1;
	else
		return 0;
}

static int rk860x_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		regmap_update_bits(di->regmap, di->mode_reg,
				   di->mode_mask, di->mode_mask);
		break;
	case REGULATOR_MODE_NORMAL:
		regmap_update_bits(di->regmap, di->mode_reg, di->mode_mask, 0);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static unsigned int rk860x_get_mode(struct regulator_dev *rdev)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret = 0;

	ret = regmap_read(di->regmap, di->mode_reg, &val);
	if (ret < 0)
		return ret;
	if (val & di->mode_mask)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static const int slew_rates[] = {
	64000,
	32000,
	16000,
	 8000,
	 4000,
	 2000,
	 1000,
	  500,
};

static int rk860x_set_ramp(struct regulator_dev *rdev, int ramp)
{
	struct rk860x_device_info *di = rdev_get_drvdata(rdev);
	int regval = -1, i;
	const int *slew_rate_t;
	int slew_rate_n;

	slew_rate_t = slew_rates;
	slew_rate_n = ARRAY_SIZE(slew_rates);

	for (i = 0; i < slew_rate_n; i++) {
		if (ramp <= slew_rate_t[i])
			regval = i;
		else
			break;
	}

	if (regval < 0) {
		dev_err(di->dev, "unsupported ramp value %d\n", ramp);
		return -EINVAL;
	}

	return regmap_update_bits(di->regmap, di->slew_reg,
				  di->slew_mask, regval << di->slew_shift);
}

static const struct regulator_ops rk860x_regulator_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = rk860x_get_voltage,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
	.map_voltage = regulator_map_voltage_linear,
	.list_voltage = regulator_list_voltage_linear,
	.set_suspend_voltage = rk860x_set_suspend_voltage,
	.enable = rk860x_set_enable,
	.disable = rk860x_set_disable,
	.is_enabled = rk860x_is_enabled,
	.set_mode = rk860x_set_mode,
	.get_mode = rk860x_get_mode,
	.set_ramp_delay = rk860x_set_ramp,
	.set_suspend_enable = rk860x_set_suspend_enable,
	.set_suspend_disable = rk860x_set_suspend_disable,
	.resume = rk860x_resume,
};

/* For 00,01 options:
 * VOUT = 0.7125V + NSELx * 12.5mV, from 0.7125 to 1.5V.
 * For 02,03 options:
 * VOUT = 0.5V + NSELx * 6.25mV, from 0.5 to 1.5V.
 */
static int rk860x_device_setup(struct rk860x_device_info *di,
			       struct rk860x_platform_data *pdata)
{
	int ret = 0;
	u32 val = 0;

	switch (di->chip_id) {
	case RK860X_CHIP_ID_00:
	case RK860X_CHIP_ID_01:
		di->vsel_min = 712500;
		di->vsel_step = 12500;
		di->n_voltages = RK860X_NVOLTAGES_64;
		di->vol_mask = VSEL_A_NSEL_MASK;
		if (di->sleep_vsel_id) {
			di->sleep_reg = RK860X_VSEL1_A;
			di->vol_reg = RK860X_VSEL0_A;
			di->mode_reg = RK860X_VSEL0_A;
			di->en_reg = RK860X_VSEL0_A;
			di->sleep_en_reg = RK860X_VSEL1_A;
		} else {
			di->sleep_reg = RK860X_VSEL0_A;
			di->vol_reg = RK860X_VSEL1_A;
			di->mode_reg = RK860X_VSEL1_A;
			di->en_reg = RK860X_VSEL1_A;
			di->sleep_en_reg = RK860X_VSEL0_A;
		}
		break;
	case RK860X_CHIP_ID_02:
	case RK860X_CHIP_ID_03:
		di->vsel_min = 500000;
		di->vsel_step = 6250;
		di->n_voltages = RK860X_NVOLTAGES_160;
		di->vol_mask = VSEL_B_NSEL_MASK;
		if (di->sleep_vsel_id) {
			di->sleep_reg = RK860X_VSEL1_B;
			di->vol_reg = RK860X_VSEL0_B;
			di->mode_reg = RK860X_VSEL0_A;
			di->en_reg = RK860X_VSEL0_A;
			di->sleep_en_reg = RK860X_VSEL1_A;
		} else {
			di->sleep_reg = RK860X_VSEL0_B;
			di->vol_reg = RK860X_VSEL1_B;
			di->mode_reg = RK860X_VSEL1_A;
			di->en_reg = RK860X_VSEL1_A;
			di->sleep_en_reg = RK860X_VSEL0_A;
		}
		break;
	default:
		dev_err(di->dev, "Chip ID %d not supported!\n", di->chip_id);
		return -EINVAL;
	}

	di->mode_mask = VSEL_MODE;
	di->slew_reg = RK860X_CONTROL;
	di->slew_mask = CTL_SLEW_MASK;
	di->slew_shift = CTL_SLEW_SHIFT;

	if (pdata->limit_volt) {
		if (pdata->limit_volt < di->vsel_min ||
		    pdata->limit_volt > 1500000)
			pdata->limit_volt = 1500000;
		val = (pdata->limit_volt - di->vsel_min) / di->vsel_step;
		ret = regmap_write(di->regmap, RK860X_MAX_SET, val);
		if (ret < 0) {
			dev_err(di->dev, "Failed to set limit voltage!\n");
			return ret;
		}
	}

	return ret;
}

static int rk860x_regulator_register(struct rk860x_device_info *di,
				     struct regulator_config *config)
{
	struct regulator_desc *rdesc = &di->desc;

	rdesc->name = "rk860x-reg";
	rdesc->supply_name = "vin";
	rdesc->ops = &rk860x_regulator_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->n_voltages = di->n_voltages;
	rdesc->enable_reg = di->en_reg;
	rdesc->enable_mask = VSEL_BUCK_EN;
	rdesc->min_uV = di->vsel_min;
	rdesc->uV_step = di->vsel_step;
	rdesc->vsel_reg = di->vol_reg;
	rdesc->vsel_mask = di->vol_mask;
	rdesc->owner = THIS_MODULE;
	rdesc->enable_time = 400;

	di->rdev = devm_regulator_register(di->dev, &di->desc, config);
	return PTR_ERR_OR_ZERO(di->rdev);
}

static const struct regmap_config rk860x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static struct rk860x_platform_data *
rk860x_parse_dt(struct device *dev, struct device_node *np,
		const struct regulator_desc *desc)
{
	struct rk860x_platform_data *pdata;
	int ret, flag, limit_volt;
	u32 tmp;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->regulator = of_get_regulator_init_data(dev, np, desc);
	pdata->regulator->constraints.initial_state = PM_SUSPEND_MEM;

	if (!(of_property_read_u32(np, "limit-microvolt", &limit_volt)))
		pdata->limit_volt = limit_volt;

	ret = of_property_read_u32(np, "rockchip,suspend-voltage-selector",
				   &tmp);
	if (!ret)
		pdata->sleep_vsel_id = tmp;

	if (pdata->sleep_vsel_id)
		flag = GPIOD_OUT_LOW;
	else
		flag = GPIOD_OUT_HIGH;

	pdata->vsel_gpio = devm_gpiod_get_index_optional(dev, "vsel", 0, flag);
	if (IS_ERR(pdata->vsel_gpio)) {
		ret = PTR_ERR(pdata->vsel_gpio);
		dev_err(dev, "failed to get vesl gpio (%d)\n", ret);
		pdata->vsel_gpio = NULL;
	}

	return pdata;
}

static const struct of_device_id rk860x_dt_ids[] = {
	{
		.compatible = "rockchip,rk860x",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, rk860x_dt_ids);

static int rk860x_regulator_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct rk860x_device_info *di;
	struct rk860x_platform_data *pdata;
	struct regulator_config config = { };
	unsigned int val;
	int ret;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->desc.of_map_mode = rk860x_map_mode;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		pdata = rk860x_parse_dt(&client->dev, np, &di->desc);

	if (!pdata || !pdata->regulator) {
		dev_err(&client->dev, "Platform data not found!\n");
		return -ENODEV;
	}

	di->vsel_gpio = pdata->vsel_gpio;
	di->sleep_vsel_id = pdata->sleep_vsel_id;

	di->regulator = pdata->regulator;
	if (client->dev.of_node) {
		di->chip_id =
			(unsigned long)of_device_get_match_data(&client->dev);
	} else {
		/* if no ramp constraint set, get the pdata ramp_delay */
		if (!di->regulator->constraints.ramp_delay) {
			int slew_idx = (pdata->slew_rate & 0x7)
						? pdata->slew_rate : 0;

			di->regulator->constraints.ramp_delay =
				slew_rates[slew_idx];
		}
		di->chip_id = id->driver_data;
	}

	di->regmap = devm_regmap_init_i2c(client, &rk860x_regmap_config);
	if (IS_ERR(di->regmap)) {
		dev_err(&client->dev, "Failed to allocate regmap!\n");
		return PTR_ERR(di->regmap);
	}
	di->dev = &client->dev;
	i2c_set_clientdata(client, di);
	/* Get chip ID */
	ret = regmap_read(di->regmap, RK860X_ID1, &val);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get chip ID!\n");
		return ret;
	}

	switch (di->chip_id) {
	case RK860X_CHIP_ID_00:
	case RK860X_CHIP_ID_01:
		if ((val & DIE_ID) != 0x8) {
			dev_err(&client->dev, "Failed to match chip ID!\n");
			return -EINVAL;
		}
		break;
	case RK860X_CHIP_ID_02:
	case RK860X_CHIP_ID_03:
		if ((val & DIE_ID) != 0xa) {
			dev_err(&client->dev, "Failed to match chip ID!\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	/* Device init */
	ret = rk860x_device_setup(di, pdata);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to setup device!\n");
		return ret;
	}
	/* Register regulator */
	config.dev = di->dev;
	config.init_data = di->regulator;
	config.regmap = di->regmap;
	config.driver_data = di;
	config.of_node = np;

	ret = rk860x_regulator_register(di, &config);
	if (ret < 0)
		dev_err(&client->dev, "Failed to register regulator!\n");

	return ret;
}

static void rk860x_regulator_shutdown(struct i2c_client *client)
{
	struct rk860x_device_info *di;
	int ret;

	di = i2c_get_clientdata(client);

	dev_info(di->dev, "rk860..... reset\n");

	ret = regmap_update_bits(di->regmap, di->slew_reg,
				 CTL_RESET, CTL_RESET);

	if (ret < 0)
		dev_err(di->dev, "force rk860x_reset error! ret=%d\n", ret);
	else
		dev_info(di->dev, "force rk860x_reset ok!\n");
}

static const struct i2c_device_id rk860x_id[] = {
	{ .name = "rk8600", .driver_data = RK860X_CHIP_ID_00 },
	{ .name = "rk8601", .driver_data = RK860X_CHIP_ID_01 },
	{ .name = "rk8602", .driver_data = RK860X_CHIP_ID_02 },
	{ .name = "rk8603", .driver_data = RK860X_CHIP_ID_03 },
	{},
};
MODULE_DEVICE_TABLE(i2c, rk860x_id);

static struct i2c_driver rk860x_regulator_driver = {
	.driver = {
		.name = "rk860-regulator",
		.of_match_table = of_match_ptr(rk860x_dt_ids),
	},
	.probe = rk860x_regulator_probe,
	.shutdown = rk860x_regulator_shutdown,
	.id_table = rk860x_id,
};

module_i2c_driver(rk860x_regulator_driver);

MODULE_AUTHOR("Elaine Zhang <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("rk860x regulator driver");
MODULE_LICENSE("GPL v2");
