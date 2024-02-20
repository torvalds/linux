// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MPS MP3309C White LED driver with I2C interface
 *
 * This driver support both analog (by I2C commands) and PWM dimming control
 * modes.
 *
 * Copyright (C) 2023 ASEM Srl
 * Author: Flavio Suligoi <f.suligoi@asem.it>
 *
 * Based on pwm_bl.c
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define REG_I2C_0	0x00
#define REG_I2C_1	0x01

#define REG_I2C_0_EN	0x80
#define REG_I2C_0_D0	0x40
#define REG_I2C_0_D1	0x20
#define REG_I2C_0_D2	0x10
#define REG_I2C_0_D3	0x08
#define REG_I2C_0_D4	0x04
#define REG_I2C_0_RSRV1	0x02
#define REG_I2C_0_RSRV2	0x01

#define REG_I2C_1_RSRV1	0x80
#define REG_I2C_1_DIMS	0x40
#define REG_I2C_1_SYNC	0x20
#define REG_I2C_1_OVP0	0x10
#define REG_I2C_1_OVP1	0x08
#define REG_I2C_1_VOS	0x04
#define REG_I2C_1_LEDO	0x02
#define REG_I2C_1_OTP	0x01

#define ANALOG_I2C_NUM_LEVELS	32		/* 0..31 */
#define ANALOG_I2C_REG_MASK	0x7c

#define MP3309C_PWM_DEFAULT_NUM_LEVELS	256	/* 0..255 */

enum mp3309c_status_value {
	FIRST_POWER_ON,
	BACKLIGHT_OFF,
	BACKLIGHT_ON,
};

enum mp3309c_dimming_mode_value {
	DIMMING_PWM,
	DIMMING_ANALOG_I2C,
};

struct mp3309c_platform_data {
	unsigned int max_brightness;
	unsigned int default_brightness;
	unsigned int *levels;
	u8  dimming_mode;
	u8  over_voltage_protection;
	bool sync_mode;
	u8 status;
};

struct mp3309c_chip {
	struct device *dev;
	struct mp3309c_platform_data *pdata;
	struct backlight_device *bl;
	struct gpio_desc *enable_gpio;
	struct regmap *regmap;
	struct pwm_device *pwmd;
};

static const struct regmap_config mp3309c_regmap = {
	.name = "mp3309c_regmap",
	.reg_bits = 8,
	.reg_stride = 1,
	.val_bits = 8,
	.max_register = REG_I2C_1,
};

static int mp3309c_enable_device(struct mp3309c_chip *chip)
{
	u8 reg_val;
	int ret;

	/* I2C register #0 - Device enable */
	ret = regmap_update_bits(chip->regmap, REG_I2C_0, REG_I2C_0_EN,
				 REG_I2C_0_EN);
	if (ret)
		return ret;

	/*
	 * I2C register #1 - Set working mode:
	 *  - set one of the two dimming mode:
	 *    - PWM dimming using an external PWM dimming signal
	 *    - analog dimming using I2C commands
	 *  - enable/disable synchronous mode
	 *  - set overvoltage protection (OVP)
	 */
	reg_val = 0x00;
	if (chip->pdata->dimming_mode == DIMMING_PWM)
		reg_val |= REG_I2C_1_DIMS;
	if (chip->pdata->sync_mode)
		reg_val |= REG_I2C_1_SYNC;
	reg_val |= chip->pdata->over_voltage_protection;
	ret = regmap_write(chip->regmap, REG_I2C_1, reg_val);
	if (ret)
		return ret;

	return 0;
}

static int mp3309c_bl_update_status(struct backlight_device *bl)
{
	struct mp3309c_chip *chip = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);
	struct pwm_state pwmstate;
	unsigned int analog_val, bits_val;
	int i, ret;

	if (chip->pdata->dimming_mode == DIMMING_PWM) {
		/*
		 * PWM control mode
		 */
		pwm_get_state(chip->pwmd, &pwmstate);
		pwm_set_relative_duty_cycle(&pwmstate,
					    chip->pdata->levels[brightness],
					    chip->pdata->levels[chip->pdata->max_brightness]);
		pwmstate.enabled = true;
		ret = pwm_apply_might_sleep(chip->pwmd, &pwmstate);
		if (ret)
			return ret;

		switch (chip->pdata->status) {
		case FIRST_POWER_ON:
		case BACKLIGHT_OFF:
			/*
			 * After 20ms of low pwm signal level, the chip turns
			 * off automatically. In this case, before enabling the
			 * chip again, we must wait about 10ms for pwm signal to
			 * stabilize.
			 */
			if (brightness > 0) {
				msleep(10);
				mp3309c_enable_device(chip);
				chip->pdata->status = BACKLIGHT_ON;
			} else {
				chip->pdata->status = BACKLIGHT_OFF;
			}
			break;
		case BACKLIGHT_ON:
			if (brightness == 0)
				chip->pdata->status = BACKLIGHT_OFF;
			break;
		}
	} else {
		/*
		 * Analog (by I2C command) control mode
		 *
		 * The first time, before setting brightness, we must enable the
		 * device
		 */
		if (chip->pdata->status == FIRST_POWER_ON)
			mp3309c_enable_device(chip);

		/*
		 * Dimming mode I2C command (fixed dimming range 0..31)
		 *
		 * The 5 bits of the dimming analog value D4..D0 is allocated
		 * in the I2C register #0, in the following way:
		 *
		 *     +--+--+--+--+--+--+--+--+
		 *     |EN|D0|D1|D2|D3|D4|XX|XX|
		 *     +--+--+--+--+--+--+--+--+
		 */
		analog_val = brightness;
		bits_val = 0;
		for (i = 0; i <= 5; i++)
			bits_val += ((analog_val >> i) & 0x01) << (6 - i);
		ret = regmap_update_bits(chip->regmap, REG_I2C_0,
					 ANALOG_I2C_REG_MASK, bits_val);
		if (ret)
			return ret;

		if (brightness > 0)
			chip->pdata->status = BACKLIGHT_ON;
		else
			chip->pdata->status = BACKLIGHT_OFF;
	}

	return 0;
}

static const struct backlight_ops mp3309c_bl_ops = {
	.update_status = mp3309c_bl_update_status,
};

static int mp3309c_parse_fwnode(struct mp3309c_chip *chip,
				struct mp3309c_platform_data *pdata)
{
	int ret, i;
	unsigned int num_levels, tmp_value;
	struct device *dev = chip->dev;

	if (!dev_fwnode(dev))
		return dev_err_probe(dev, -ENODEV, "failed to get firmware node\n");

	/*
	 * Dimming mode: the MP3309C provides two dimming control mode:
	 *
	 * - PWM mode
	 * - Analog by I2C control mode (default)
	 *
	 * I2C control mode is assumed as default but, if the pwms property is
	 * found in the backlight node, the mode switches to PWM mode.
	 */
	pdata->dimming_mode = DIMMING_ANALOG_I2C;
	if (device_property_present(dev, "pwms")) {
		chip->pwmd = devm_pwm_get(dev, NULL);
		if (IS_ERR(chip->pwmd))
			return dev_err_probe(dev, PTR_ERR(chip->pwmd), "error getting pwm data\n");
		pdata->dimming_mode = DIMMING_PWM;
		pwm_apply_args(chip->pwmd);
	}

	/*
	 * In I2C control mode the dimming levels (0..31) are fixed by the
	 * hardware, while in PWM control mode they can be chosen by the user,
	 * to allow nonlinear mappings.
	 */
	if  (pdata->dimming_mode == DIMMING_ANALOG_I2C) {
		/*
		 * Analog (by I2C commands) control mode: fixed 0..31 brightness
		 * levels
		 */
		num_levels = ANALOG_I2C_NUM_LEVELS;

		/* Enable GPIO used in I2C dimming mode only */
		chip->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
		if (IS_ERR(chip->enable_gpio))
			return dev_err_probe(dev, PTR_ERR(chip->enable_gpio),
					     "error getting enable gpio\n");
	} else {
		/*
		 * PWM control mode: check for brightness level in DT
		 */
		if (device_property_present(dev, "brightness-levels")) {
			/* Read brightness levels from DT */
			num_levels = device_property_count_u32(dev, "brightness-levels");
			if (num_levels < 2)
				return -EINVAL;
		} else {
			/* Use default brightness levels */
			num_levels = MP3309C_PWM_DEFAULT_NUM_LEVELS;
		}
	}

	/* Fill brightness levels array */
	pdata->levels = devm_kcalloc(dev, num_levels, sizeof(*pdata->levels), GFP_KERNEL);
	if (!pdata->levels)
		return -ENOMEM;
	if (device_property_present(dev, "brightness-levels")) {
		ret = device_property_read_u32_array(dev, "brightness-levels",
						     pdata->levels, num_levels);
		if (ret < 0)
			return ret;
	} else {
		for (i = 0; i < num_levels; i++)
			pdata->levels[i] = i;
	}

	pdata->max_brightness = num_levels - 1;

	ret = device_property_read_u32(dev, "default-brightness", &pdata->default_brightness);
	if (ret)
		pdata->default_brightness = pdata->max_brightness;
	if (pdata->default_brightness > pdata->max_brightness) {
		dev_err_probe(dev, -ERANGE, "default brightness exceeds max brightness\n");
		pdata->default_brightness = pdata->max_brightness;
	}

	/*
	 * Over-voltage protection (OVP)
	 *
	 * This (optional) property values are:
	 *
	 *  - 13.5V
	 *  - 24V
	 *  - 35.5V (hardware default setting)
	 *
	 * If missing, the default value for OVP is 35.5V
	 */
	pdata->over_voltage_protection = REG_I2C_1_OVP1;
	ret = device_property_read_u32(dev, "mps,overvoltage-protection-microvolt", &tmp_value);
	if (!ret) {
		switch (tmp_value) {
		case 13500000:
			pdata->over_voltage_protection = 0x00;
			break;
		case 24000000:
			pdata->over_voltage_protection = REG_I2C_1_OVP0;
			break;
		case 35500000:
			pdata->over_voltage_protection = REG_I2C_1_OVP1;
			break;
		default:
			return -EINVAL;
		}
	}

	/* Synchronous (default) and non-synchronous mode */
	pdata->sync_mode = !device_property_read_bool(dev, "mps,no-sync-mode");

	return 0;
}

static int mp3309c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mp3309c_platform_data *pdata = dev_get_platdata(dev);
	struct mp3309c_chip *chip;
	struct backlight_properties props;
	struct pwm_state pwmstate;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -EOPNOTSUPP, "failed to check i2c functionality\n");

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;

	chip->regmap = devm_regmap_init_i2c(client, &mp3309c_regmap);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap),
				     "failed to allocate register map\n");

	i2c_set_clientdata(client, chip);

	if (!pdata) {
		pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = mp3309c_parse_fwnode(chip, pdata);
		if (ret)
			return ret;
	}
	chip->pdata = pdata;

	/* Backlight properties */
	memset(&props, 0, sizeof(struct backlight_properties));
	props.brightness = pdata->default_brightness;
	props.max_brightness = pdata->max_brightness;
	props.scale = BACKLIGHT_SCALE_LINEAR;
	props.type = BACKLIGHT_RAW;
	props.power = FB_BLANK_UNBLANK;
	props.fb_blank = FB_BLANK_UNBLANK;
	chip->bl = devm_backlight_device_register(dev, "mp3309c", dev, chip,
						  &mp3309c_bl_ops, &props);
	if (IS_ERR(chip->bl))
		return dev_err_probe(dev, PTR_ERR(chip->bl),
				     "error registering backlight device\n");

	/* In PWM dimming mode, enable pwm device */
	if (chip->pdata->dimming_mode == DIMMING_PWM) {
		pwm_init_state(chip->pwmd, &pwmstate);
		pwm_set_relative_duty_cycle(&pwmstate,
					    chip->pdata->default_brightness,
					    chip->pdata->max_brightness);
		pwmstate.enabled = true;
		ret = pwm_apply_might_sleep(chip->pwmd, &pwmstate);
		if (ret)
			return dev_err_probe(dev, ret, "error setting pwm device\n");
	}

	chip->pdata->status = FIRST_POWER_ON;
	backlight_update_status(chip->bl);

	return 0;
}

static void mp3309c_remove(struct i2c_client *client)
{
	struct mp3309c_chip *chip = i2c_get_clientdata(client);
	struct backlight_device *bl = chip->bl;

	bl->props.power = FB_BLANK_POWERDOWN;
	bl->props.brightness = 0;
	backlight_update_status(chip->bl);
}

static const struct of_device_id mp3309c_match_table[] = {
	{ .compatible = "mps,mp3309c", },
	{ },
};
MODULE_DEVICE_TABLE(of, mp3309c_match_table);

static const struct i2c_device_id mp3309c_id[] = {
	{ "mp3309c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mp3309c_id);

static struct i2c_driver mp3309c_i2c_driver = {
	.driver	= {
			.name		= KBUILD_MODNAME,
			.of_match_table	= mp3309c_match_table,
	},
	.probe		= mp3309c_probe,
	.remove		= mp3309c_remove,
	.id_table	= mp3309c_id,
};

module_i2c_driver(mp3309c_i2c_driver);

MODULE_DESCRIPTION("Backlight Driver for MPS MP3309C");
MODULE_AUTHOR("Flavio Suligoi <f.suligoi@asem.it>");
MODULE_LICENSE("GPL");
