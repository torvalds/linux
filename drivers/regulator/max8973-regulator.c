/*
 * max8973-regulator.c -- Maxim max8973
 *
 * Regulator driver for MAXIM 8973 DC-DC step-down switching regulator.
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max8973-regulator.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

/* Register definitions */
#define MAX8973_VOUT					0x0
#define MAX8973_VOUT_DVS				0x1
#define MAX8973_CONTROL1				0x2
#define MAX8973_CONTROL2				0x3
#define MAX8973_CHIPID1					0x4
#define MAX8973_CHIPID2					0x5

#define MAX8973_MAX_VOUT_REG				2

/* MAX8973_VOUT */
#define MAX8973_VOUT_ENABLE				BIT(7)
#define MAX8973_VOUT_MASK				0x7F

/* MAX8973_VOUT_DVS */
#define MAX8973_DVS_VOUT_MASK				0x7F

/* MAX8973_CONTROL1 */
#define MAX8973_SNS_ENABLE				BIT(7)
#define MAX8973_FPWM_EN_M				BIT(6)
#define MAX8973_NFSR_ENABLE				BIT(5)
#define MAX8973_AD_ENABLE				BIT(4)
#define MAX8973_BIAS_ENABLE				BIT(3)
#define MAX8973_FREQSHIFT_9PER				BIT(2)

#define MAX8973_RAMP_12mV_PER_US			0x0
#define MAX8973_RAMP_25mV_PER_US			0x1
#define MAX8973_RAMP_50mV_PER_US			0x2
#define MAX8973_RAMP_200mV_PER_US			0x3
#define MAX8973_RAMP_MASK				0x3

/* MAX8973_CONTROL2 */
#define MAX8973_WDTMR_ENABLE				BIT(6)
#define MAX8973_DISCH_ENBABLE				BIT(5)
#define MAX8973_FT_ENABLE				BIT(4)

#define MAX8973_CKKADV_TRIP_DISABLE			0xC
#define MAX8973_CKKADV_TRIP_75mV_PER_US			0x0
#define MAX8973_CKKADV_TRIP_150mV_PER_US		0x4
#define MAX8973_CKKADV_TRIP_75mV_PER_US_HIST_DIS	0x8
#define MAX8973_CONTROL_CLKADV_TRIP_MASK		0x00030000

#define MAX8973_INDUCTOR_MIN_30_PER			0x0
#define MAX8973_INDUCTOR_NOMINAL			0x1
#define MAX8973_INDUCTOR_PLUS_30_PER			0x2
#define MAX8973_INDUCTOR_PLUS_60_PER			0x3
#define MAX8973_CONTROL_INDUCTOR_VALUE_MASK		0x00300000

#define MAX8973_MIN_VOLATGE				606250
#define MAX8973_MAX_VOLATGE				1400000
#define MAX8973_VOLATGE_STEP				6250
#define MAX8973_BUCK_N_VOLTAGE				0x80

enum device_id {
	MAX8973,
	MAX77621
};

/* Maxim 8973 chip information */
struct max8973_chip {
	struct device *dev;
	struct regulator_desc desc;
	struct regmap *regmap;
	bool enable_external_control;
	int enable_gpio;
	int dvs_gpio;
	int lru_index[MAX8973_MAX_VOUT_REG];
	int curr_vout_val[MAX8973_MAX_VOUT_REG];
	int curr_vout_reg;
	int curr_gpio_val;
	struct regulator_ops ops;
	enum device_id id;
};

/*
 * find_voltage_set_register: Find new voltage configuration register (VOUT).
 * The finding of the new VOUT register will be based on the LRU mechanism.
 * Each VOUT register will have different voltage configured . This
 * Function will look if any of the VOUT register have requested voltage set
 * or not.
 *     - If it is already there then it will make that register as most
 *       recently used and return as found so that caller need not to set
 *       the VOUT register but need to set the proper gpios to select this
 *       VOUT register.
 *     - If requested voltage is not found then it will use the least
 *       recently mechanism to get new VOUT register for new configuration
 *       and will return not_found so that caller need to set new VOUT
 *       register and then gpios (both).
 */
static bool find_voltage_set_register(struct max8973_chip *tps,
		int req_vsel, int *vout_reg, int *gpio_val)
{
	int i;
	bool found = false;
	int new_vout_reg = tps->lru_index[MAX8973_MAX_VOUT_REG - 1];
	int found_index = MAX8973_MAX_VOUT_REG - 1;

	for (i = 0; i < MAX8973_MAX_VOUT_REG; ++i) {
		if (tps->curr_vout_val[tps->lru_index[i]] == req_vsel) {
			new_vout_reg = tps->lru_index[i];
			found_index = i;
			found = true;
			goto update_lru_index;
		}
	}

update_lru_index:
	for (i = found_index; i > 0; i--)
		tps->lru_index[i] = tps->lru_index[i - 1];

	tps->lru_index[0] = new_vout_reg;
	*gpio_val = new_vout_reg;
	*vout_reg = MAX8973_VOUT + new_vout_reg;
	return found;
}

static int max8973_dcdc_get_voltage_sel(struct regulator_dev *rdev)
{
	struct max8973_chip *max = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(max->regmap, max->curr_vout_reg, &data);
	if (ret < 0) {
		dev_err(max->dev, "register %d read failed, err = %d\n",
			max->curr_vout_reg, ret);
		return ret;
	}
	return data & MAX8973_VOUT_MASK;
}

static int max8973_dcdc_set_voltage_sel(struct regulator_dev *rdev,
	     unsigned vsel)
{
	struct max8973_chip *max = rdev_get_drvdata(rdev);
	int ret;
	bool found = false;
	int vout_reg = max->curr_vout_reg;
	int gpio_val = max->curr_gpio_val;

	/*
	 * If gpios are available to select the VOUT register then least
	 * recently used register for new configuration.
	 */
	if (gpio_is_valid(max->dvs_gpio))
		found = find_voltage_set_register(max, vsel,
					&vout_reg, &gpio_val);

	if (!found) {
		ret = regmap_update_bits(max->regmap, vout_reg,
					MAX8973_VOUT_MASK, vsel);
		if (ret < 0) {
			dev_err(max->dev, "register %d update failed, err %d\n",
				 vout_reg, ret);
			return ret;
		}
		max->curr_vout_reg = vout_reg;
		max->curr_vout_val[gpio_val] = vsel;
	}

	/* Select proper VOUT register vio gpios */
	if (gpio_is_valid(max->dvs_gpio)) {
		gpio_set_value_cansleep(max->dvs_gpio, gpio_val & 0x1);
		max->curr_gpio_val = gpio_val;
	}
	return 0;
}

static int max8973_dcdc_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct max8973_chip *max = rdev_get_drvdata(rdev);
	int ret;
	int pwm;

	/* Enable force PWM mode in FAST mode only. */
	switch (mode) {
	case REGULATOR_MODE_FAST:
		pwm = MAX8973_FPWM_EN_M;
		break;

	case REGULATOR_MODE_NORMAL:
		pwm = 0;
		break;

	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(max->regmap, MAX8973_CONTROL1,
				MAX8973_FPWM_EN_M, pwm);
	if (ret < 0)
		dev_err(max->dev, "register %d update failed, err %d\n",
				MAX8973_CONTROL1, ret);
	return ret;
}

static unsigned int max8973_dcdc_get_mode(struct regulator_dev *rdev)
{
	struct max8973_chip *max = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret;

	ret = regmap_read(max->regmap, MAX8973_CONTROL1, &data);
	if (ret < 0) {
		dev_err(max->dev, "register %d read failed, err %d\n",
				MAX8973_CONTROL1, ret);
		return ret;
	}
	return (data & MAX8973_FPWM_EN_M) ?
		REGULATOR_MODE_FAST : REGULATOR_MODE_NORMAL;
}

static int max8973_set_ramp_delay(struct regulator_dev *rdev,
		int ramp_delay)
{
	struct max8973_chip *max = rdev_get_drvdata(rdev);
	unsigned int control;
	int ret;
	int ret_val;

	/* Set ramp delay */
	if (ramp_delay < 25000) {
		control = MAX8973_RAMP_12mV_PER_US;
		ret_val = 12000;
	} else if (ramp_delay < 50000) {
		control = MAX8973_RAMP_25mV_PER_US;
		ret_val = 25000;
	} else if (ramp_delay < 200000) {
		control = MAX8973_RAMP_50mV_PER_US;
		ret_val = 50000;
	} else {
		control = MAX8973_RAMP_200mV_PER_US;
		ret_val = 200000;
	}

	ret = regmap_update_bits(max->regmap, MAX8973_CONTROL1,
			MAX8973_RAMP_MASK, control);
	if (ret < 0)
		dev_err(max->dev, "register %d update failed, %d",
				MAX8973_CONTROL1, ret);
	return ret;
}

static const struct regulator_ops max8973_dcdc_ops = {
	.get_voltage_sel	= max8973_dcdc_get_voltage_sel,
	.set_voltage_sel	= max8973_dcdc_set_voltage_sel,
	.list_voltage		= regulator_list_voltage_linear,
	.set_mode		= max8973_dcdc_set_mode,
	.get_mode		= max8973_dcdc_get_mode,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.set_ramp_delay		= max8973_set_ramp_delay,
};

static int max8973_init_dcdc(struct max8973_chip *max,
			     struct max8973_regulator_platform_data *pdata)
{
	int ret;
	uint8_t	control1 = 0;
	uint8_t control2 = 0;
	unsigned int data;

	ret = regmap_read(max->regmap, MAX8973_CONTROL1, &data);
	if (ret < 0) {
		dev_err(max->dev, "register %d read failed, err = %d",
				MAX8973_CONTROL1, ret);
		return ret;
	}
	control1 = data & MAX8973_RAMP_MASK;
	switch (control1) {
	case MAX8973_RAMP_12mV_PER_US:
		max->desc.ramp_delay = 12000;
		break;
	case MAX8973_RAMP_25mV_PER_US:
		max->desc.ramp_delay = 25000;
		break;
	case MAX8973_RAMP_50mV_PER_US:
		max->desc.ramp_delay = 50000;
		break;
	case MAX8973_RAMP_200mV_PER_US:
		max->desc.ramp_delay = 200000;
		break;
	}

	if (pdata->control_flags & MAX8973_CONTROL_REMOTE_SENSE_ENABLE)
		control1 |= MAX8973_SNS_ENABLE;

	if (!(pdata->control_flags & MAX8973_CONTROL_FALLING_SLEW_RATE_ENABLE))
		control1 |= MAX8973_NFSR_ENABLE;

	if (pdata->control_flags & MAX8973_CONTROL_OUTPUT_ACTIVE_DISCH_ENABLE)
		control1 |= MAX8973_AD_ENABLE;

	if (pdata->control_flags & MAX8973_CONTROL_BIAS_ENABLE) {
		control1 |= MAX8973_BIAS_ENABLE;
		max->desc.enable_time = 20;
	} else {
		max->desc.enable_time = 240;
	}

	if (pdata->control_flags & MAX8973_CONTROL_FREQ_SHIFT_9PER_ENABLE)
		control1 |= MAX8973_FREQSHIFT_9PER;

	if (!(pdata->control_flags & MAX8973_CONTROL_PULL_DOWN_ENABLE))
		control2 |= MAX8973_DISCH_ENBABLE;

	/*  Clock advance trip configuration */
	switch (pdata->control_flags & MAX8973_CONTROL_CLKADV_TRIP_MASK) {
	case MAX8973_CONTROL_CLKADV_TRIP_DISABLED:
		control2 |= MAX8973_CKKADV_TRIP_DISABLE;
		break;

	case MAX8973_CONTROL_CLKADV_TRIP_75mV_PER_US:
		control2 |= MAX8973_CKKADV_TRIP_75mV_PER_US;
		break;

	case MAX8973_CONTROL_CLKADV_TRIP_150mV_PER_US:
		control2 |= MAX8973_CKKADV_TRIP_150mV_PER_US;
		break;

	case MAX8973_CONTROL_CLKADV_TRIP_75mV_PER_US_HIST_DIS:
		control2 |= MAX8973_CKKADV_TRIP_75mV_PER_US_HIST_DIS;
		break;
	}

	/* Configure inductor value */
	switch (pdata->control_flags & MAX8973_CONTROL_INDUCTOR_VALUE_MASK) {
	case MAX8973_CONTROL_INDUCTOR_VALUE_NOMINAL:
		control2 |= MAX8973_INDUCTOR_NOMINAL;
		break;

	case MAX8973_CONTROL_INDUCTOR_VALUE_MINUS_30_PER:
		control2 |= MAX8973_INDUCTOR_MIN_30_PER;
		break;

	case MAX8973_CONTROL_INDUCTOR_VALUE_PLUS_30_PER:
		control2 |= MAX8973_INDUCTOR_PLUS_30_PER;
		break;

	case MAX8973_CONTROL_INDUCTOR_VALUE_PLUS_60_PER:
		control2 |= MAX8973_INDUCTOR_PLUS_60_PER;
		break;
	}

	ret = regmap_write(max->regmap, MAX8973_CONTROL1, control1);
	if (ret < 0) {
		dev_err(max->dev, "register %d write failed, err = %d",
				MAX8973_CONTROL1, ret);
		return ret;
	}

	ret = regmap_write(max->regmap, MAX8973_CONTROL2, control2);
	if (ret < 0) {
		dev_err(max->dev, "register %d write failed, err = %d",
				MAX8973_CONTROL2, ret);
		return ret;
	}

	/* If external control is enabled then disable EN bit */
	if (max->enable_external_control && (max->id == MAX8973)) {
		ret = regmap_update_bits(max->regmap, MAX8973_VOUT,
						MAX8973_VOUT_ENABLE, 0);
		if (ret < 0)
			dev_err(max->dev, "register %d update failed, err = %d",
				MAX8973_VOUT, ret);
	}
	return ret;
}

static const struct regmap_config max8973_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= MAX8973_CHIPID2,
	.cache_type		= REGCACHE_RBTREE,
};

static struct max8973_regulator_platform_data *max8973_parse_dt(
		struct device *dev)
{
	struct max8973_regulator_platform_data *pdata;
	struct device_node *np = dev->of_node;
	int ret;
	u32 pval;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->enable_ext_control = of_property_read_bool(np,
						"maxim,externally-enable");
	pdata->enable_gpio = of_get_named_gpio(np, "maxim,enable-gpio", 0);
	pdata->dvs_gpio = of_get_named_gpio(np, "maxim,dvs-gpio", 0);

	ret = of_property_read_u32(np, "maxim,dvs-default-state", &pval);
	if (!ret)
		pdata->dvs_def_state = pval;

	if (of_property_read_bool(np, "maxim,enable-remote-sense"))
		pdata->control_flags  |= MAX8973_CONTROL_REMOTE_SENSE_ENABLE;

	if (of_property_read_bool(np, "maxim,enable-falling-slew-rate"))
		pdata->control_flags  |=
				MAX8973_CONTROL_FALLING_SLEW_RATE_ENABLE;

	if (of_property_read_bool(np, "maxim,enable-active-discharge"))
		pdata->control_flags  |=
				MAX8973_CONTROL_OUTPUT_ACTIVE_DISCH_ENABLE;

	if (of_property_read_bool(np, "maxim,enable-frequency-shift"))
		pdata->control_flags  |= MAX8973_CONTROL_FREQ_SHIFT_9PER_ENABLE;

	if (of_property_read_bool(np, "maxim,enable-bias-control"))
		pdata->control_flags  |= MAX8973_BIAS_ENABLE;

	return pdata;
}

static const struct of_device_id of_max8973_match_tbl[] = {
	{ .compatible = "maxim,max8973", .data = (void *)MAX8973, },
	{ .compatible = "maxim,max77621", .data = (void *)MAX77621, },
	{ },
};
MODULE_DEVICE_TABLE(of, of_max8973_match_tbl);

static int max8973_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct max8973_regulator_platform_data *pdata;
	struct regulator_init_data *ridata;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	struct max8973_chip *max;
	bool pdata_from_dt = false;
	unsigned int chip_id;
	int ret;

	pdata = dev_get_platdata(&client->dev);

	if (!pdata && client->dev.of_node) {
		pdata = max8973_parse_dt(&client->dev);
		pdata_from_dt = true;
	}

	if (!pdata) {
		dev_err(&client->dev, "No Platform data");
		return -EIO;
	}

	if ((pdata->dvs_gpio == -EPROBE_DEFER) ||
		(pdata->enable_gpio == -EPROBE_DEFER))
		return -EPROBE_DEFER;

	max = devm_kzalloc(&client->dev, sizeof(*max), GFP_KERNEL);
	if (!max)
		return -ENOMEM;

	max->regmap = devm_regmap_init_i2c(client, &max8973_regmap_config);
	if (IS_ERR(max->regmap)) {
		ret = PTR_ERR(max->regmap);
		dev_err(&client->dev, "regmap init failed, err %d\n", ret);
		return ret;
	}

	if (client->dev.of_node) {
		const struct of_device_id *match;

		match = of_match_device(of_match_ptr(of_max8973_match_tbl),
				&client->dev);
		if (!match)
			return -ENODATA;
		max->id = (u32)((uintptr_t)match->data);
	} else {
		max->id = id->driver_data;
	}

	ret = regmap_read(max->regmap, MAX8973_CHIPID1, &chip_id);
	if (ret < 0) {
		dev_err(&client->dev, "register CHIPID1 read failed, %d", ret);
		return ret;
	}

	dev_info(&client->dev, "CHIP-ID OTP: 0x%02x ID_M: 0x%02x\n",
			(chip_id >> 4) & 0xF, (chip_id >> 1) & 0x7);

	i2c_set_clientdata(client, max);
	max->ops = max8973_dcdc_ops;
	max->dev = &client->dev;
	max->desc.name = id->name;
	max->desc.id = 0;
	max->desc.ops = &max->ops;
	max->desc.type = REGULATOR_VOLTAGE;
	max->desc.owner = THIS_MODULE;
	max->desc.min_uV = MAX8973_MIN_VOLATGE;
	max->desc.uV_step = MAX8973_VOLATGE_STEP;
	max->desc.n_voltages = MAX8973_BUCK_N_VOLTAGE;

	max->dvs_gpio = (pdata->dvs_gpio) ? pdata->dvs_gpio : -EINVAL;
	max->enable_gpio = (pdata->enable_gpio) ? pdata->enable_gpio : -EINVAL;
	max->enable_external_control = pdata->enable_ext_control;
	max->curr_gpio_val = pdata->dvs_def_state;
	max->curr_vout_reg = MAX8973_VOUT + pdata->dvs_def_state;

	if (gpio_is_valid(max->enable_gpio))
		max->enable_external_control = true;

	max->lru_index[0] = max->curr_vout_reg;

	if (gpio_is_valid(max->dvs_gpio)) {
		int gpio_flags;
		int i;

		gpio_flags = (pdata->dvs_def_state) ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
		ret = devm_gpio_request_one(&client->dev, max->dvs_gpio,
				gpio_flags, "max8973-dvs");
		if (ret) {
			dev_err(&client->dev,
				"gpio_request for gpio %d failed, err = %d\n",
				max->dvs_gpio, ret);
			return ret;
		}

		/*
		 * Initialize the lru index with vout_reg id
		 * The index 0 will be most recently used and
		 * set with the max->curr_vout_reg */
		for (i = 0; i < MAX8973_MAX_VOUT_REG; ++i)
			max->lru_index[i] = i;
		max->lru_index[0] = max->curr_vout_reg;
		max->lru_index[max->curr_vout_reg] = 0;
	}

	if (pdata_from_dt)
		pdata->reg_init_data = of_get_regulator_init_data(&client->dev,
					client->dev.of_node, &max->desc);

	ridata = pdata->reg_init_data;
	switch (max->id) {
	case MAX8973:
		if (!pdata->enable_ext_control) {
			max->desc.enable_reg = MAX8973_VOUT;
			max->desc.enable_mask = MAX8973_VOUT_ENABLE;
			max->ops.enable = regulator_enable_regmap;
			max->ops.disable = regulator_disable_regmap;
			max->ops.is_enabled = regulator_is_enabled_regmap;
			break;
		}

		if (gpio_is_valid(max->enable_gpio)) {
			config.ena_gpio_flags = GPIOF_OUT_INIT_LOW;
			if (ridata && (ridata->constraints.always_on ||
					ridata->constraints.boot_on))
				config.ena_gpio_flags = GPIOF_OUT_INIT_HIGH;
			config.ena_gpio = max->enable_gpio;
		}
		break;

	case MAX77621:
		if (gpio_is_valid(max->enable_gpio)) {
			ret = devm_gpio_request_one(&client->dev,
					max->enable_gpio, GPIOF_OUT_INIT_HIGH,
					"max8973-en-gpio");
			if (ret) {
				dev_err(&client->dev,
					"gpio_request for gpio %d failed: %d\n",
					max->enable_gpio, ret);
				return ret;
			}
		}

		max->desc.enable_reg = MAX8973_VOUT;
		max->desc.enable_mask = MAX8973_VOUT_ENABLE;
		max->ops.enable = regulator_enable_regmap;
		max->ops.disable = regulator_disable_regmap;
		max->ops.is_enabled = regulator_is_enabled_regmap;
		break;
	default:
		break;
	}

	ret = max8973_init_dcdc(max, pdata);
	if (ret < 0) {
		dev_err(max->dev, "Max8973 Init failed, err = %d\n", ret);
		return ret;
	}

	config.dev = &client->dev;
	config.init_data = pdata->reg_init_data;
	config.driver_data = max;
	config.of_node = client->dev.of_node;
	config.regmap = max->regmap;

	/* Register the regulators */
	rdev = devm_regulator_register(&client->dev, &max->desc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(max->dev, "regulator register failed, err %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id max8973_id[] = {
	{.name = "max8973", .driver_data = MAX8973},
	{.name = "max77621", .driver_data = MAX77621},
	{},
};
MODULE_DEVICE_TABLE(i2c, max8973_id);

static struct i2c_driver max8973_i2c_driver = {
	.driver = {
		.name = "max8973",
		.of_match_table = of_max8973_match_tbl,
		.owner = THIS_MODULE,
	},
	.probe = max8973_probe,
	.id_table = max8973_id,
};

static int __init max8973_init(void)
{
	return i2c_add_driver(&max8973_i2c_driver);
}
subsys_initcall(max8973_init);

static void __exit max8973_cleanup(void)
{
	i2c_del_driver(&max8973_i2c_driver);
}
module_exit(max8973_cleanup);

MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_DESCRIPTION("MAX8973 voltage regulator driver");
MODULE_LICENSE("GPL v2");
