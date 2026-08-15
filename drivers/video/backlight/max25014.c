// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backlight driver for Maxim MAX25014
 *
 * Copyright (C) 2025 GOcontroll B.V.
 * Author: Maud Spierings <maudspierings@gocontroll.com>
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define MAX25014_ISET_DEFAULT_100 11
#define MAX_BRIGHTNESS            100
#define MIN_BRIGHTNESS            0
#define TON_MAX                   130720 /* @153Hz */
#define TON_STEP                  1307 /* @153Hz */
#define TON_MIN                   0

#define MAX25014_DEV_ID           0x00
#define MAX25014_REV_ID           0x01
#define MAX25014_ISET             0x02
#define MAX25014_IMODE            0x03
#define MAX25014_TON1H            0x04
#define MAX25014_TON1L            0x05
#define MAX25014_TON2H            0x06
#define MAX25014_TON2L            0x07
#define MAX25014_TON3H            0x08
#define MAX25014_TON3L            0x09
#define MAX25014_TON4H            0x0A
#define MAX25014_TON4L            0x0B
#define MAX25014_TON_1_4_LSB      0x0C
#define MAX25014_SETTING          0x12
#define MAX25014_DISABLE          0x13
#define MAX25014_BSTMON           0x14
#define MAX25014_IOUT1            0x15
#define MAX25014_IOUT2            0x16
#define MAX25014_IOUT3            0x17
#define MAX25014_IOUT4            0x18
#define MAX25014_OPEN             0x1B
#define MAX25014_SHORTGND         0x1C
#define MAX25014_SHORTED_LED      0x1D
#define MAX25014_MASK             0x1E
#define MAX25014_DIAG             0x1F

#define MAX25014_ISET_ENA         BIT(5)
#define MAX25014_ISET_PSEN        BIT(4)
#define MAX25014_IMODE_HDIM       BIT(2)
#define MAX25014_SETTING_FPWM     GENMASK(6, 4)
#define MAX25014_DISABLE_DIS_MASK GENMASK(3, 0)
#define MAX25014_DIAG_OT          BIT(0)
#define MAX25014_DIAG_OTW         BIT(1)
#define MAX25014_DIAG_HW_RST      BIT(2)
#define MAX25014_DIAG_BSTOV       BIT(3)
#define MAX25014_DIAG_BSTUV       BIT(4)
#define MAX25014_DIAG_IREFOOR     BIT(5)

struct max25014 {
	struct i2c_client *client;
	struct backlight_device *bl;
	struct regmap *regmap;
	struct gpio_desc *enable;
	uint32_t iset;
	uint8_t strings_mask;
};

static const struct regmap_config max25014_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX25014_DIAG,
};

static int max25014_initial_power_state(struct max25014 *maxim)
{
	uint32_t val;
	int ret;

	ret = regmap_read(maxim->regmap, MAX25014_ISET, &val);
	if (ret)
		return ret;

	return val & MAX25014_ISET_ENA ? BACKLIGHT_POWER_ON : BACKLIGHT_POWER_OFF;
}

static int max25014_check_errors(struct max25014 *maxim)
{
	uint32_t val;
	uint8_t i;
	int ret;

	ret = regmap_read(maxim->regmap, MAX25014_OPEN, &val);
	if (ret)
		return ret;
	if (val) {
		dev_err(&maxim->client->dev, "Open led strings detected on:\n");
		for (i = 0; i < 4; i++) {
			if (val & 1 << i)
				dev_err(&maxim->client->dev, "string %d\n", i + 1);
		}
		return -EIO;
	}

	ret = regmap_read(maxim->regmap, MAX25014_SHORTGND, &val);
	if (ret)
		return ret;
	if (val) {
		dev_err(&maxim->client->dev, "Short to ground detected on:\n");
		for (i = 0; i < 4; i++) {
			if (val & 1 << i)
				dev_err(&maxim->client->dev, "string %d\n", i + 1);
		}
		return -EIO;
	}

	ret = regmap_read(maxim->regmap, MAX25014_SHORTED_LED, &val);
	if (ret)
		return ret;
	if (val) {
		dev_err(&maxim->client->dev, "Shorted led detected on:\n");
		for (i = 0; i < 4; i++) {
			if (val & 1 << i)
				dev_err(&maxim->client->dev, "string %d\n", i + 1);
		}
		return -EIO;
	}

	ret = regmap_read(maxim->regmap, MAX25014_DIAG, &val);
	if (ret)
		return ret;
	/*
	 * The HW_RST bit always starts at 1 after power up.
	 * It is reset on first read, does not indicate an error.
	 */
	if (val && val != MAX25014_DIAG_HW_RST) {
		if (val & MAX25014_DIAG_OT)
			dev_err(&maxim->client->dev,
				"Overtemperature shutdown\n");
		if (val & MAX25014_DIAG_OTW)
			dev_err(&maxim->client->dev,
				 "Chip is getting too hot (>125C)\n");
		if (val & MAX25014_DIAG_BSTOV)
			dev_err(&maxim->client->dev,
				"Boost converter overvoltage\n");
		if (val & MAX25014_DIAG_BSTUV)
			dev_err(&maxim->client->dev,
				"Boost converter undervoltage\n");
		if (val & MAX25014_DIAG_IREFOOR)
			dev_err(&maxim->client->dev, "IREF out of range\n");
		return -EIO;
	}
	return 0;
}

/*
 * 1. disable unused strings
 * 2. set dim mode
 * 3. set setting register
 * 4. enable the backlight
 */
static int max25014_configure(struct max25014 *maxim, int initial_state)
{
	uint32_t val;
	int ret;

	ret = regmap_read(maxim->regmap, MAX25014_DISABLE, &val);
	if (ret)
		return ret;

	/*
	 * Strings can only be disabled when MAX25014_ISET_ENA == 0, check if
	 * it needs to be changed at all to prevent the backlight flashing when
	 * it is configured correctly by the bootloader
	 */
	if (!((val & MAX25014_DISABLE_DIS_MASK) == maxim->strings_mask)) {
		if (initial_state == BACKLIGHT_POWER_ON) {
			ret = regmap_write(maxim->regmap, MAX25014_ISET, 0);
			if (ret)
				return ret;
		}
		ret = regmap_write(maxim->regmap, MAX25014_DISABLE, maxim->strings_mask);
		if (ret)
			return ret;
	}

	ret = regmap_write(maxim->regmap, MAX25014_IMODE, MAX25014_IMODE_HDIM);
	if (ret)
		return ret;

	ret = regmap_read(maxim->regmap, MAX25014_SETTING, &val);
	if (ret)
		return ret;

	ret = regmap_write(maxim->regmap, MAX25014_SETTING,
			   val & ~MAX25014_SETTING_FPWM);
	if (ret)
		return ret;

	return regmap_write(maxim->regmap, MAX25014_ISET,
			   maxim->iset | MAX25014_ISET_ENA |
			   MAX25014_ISET_PSEN);
}

static int max25014_update_status(struct backlight_device *bl_dev)
{
	struct max25014 *maxim = bl_get_data(bl_dev);
	uint32_t reg;
	int ret;

	reg  = TON_STEP * backlight_get_brightness(bl_dev);

	/*
	 * 18 bit number lowest, 2 bits in first register,
	 * next lowest 8 in the L register, next 8 in the H register
	 * Seemingly setting the strength of only one string controls all of
	 * them, individual settings don't affect the outcome.
	 */
	ret = regmap_write(maxim->regmap, MAX25014_TON_1_4_LSB, reg & 0b00000011);
	if (ret != 0)
		return ret;
	ret = regmap_write(maxim->regmap, MAX25014_TON1L, (reg >> 2) & 0b11111111);
	if (ret != 0)
		return ret;
	return regmap_write(maxim->regmap, MAX25014_TON1H, (reg >> 10) & 0b11111111);
}

static const struct backlight_ops max25014_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = max25014_update_status,
};

static int max25014_parse_dt(struct max25014 *maxim,
			     uint32_t *initial_brightness)
{
	struct device *dev = &maxim->client->dev;
	struct device_node *node = dev->of_node;
	uint32_t strings[4];
	int res, i;

	res = of_property_count_u32_elems(node, "maxim,strings");
	if (res == 4) {
		of_property_read_u32_array(node, "maxim,strings", strings, 4);
		for (i = 0; i < 4; i++) {
			if (strings[i] == 0)
				maxim->strings_mask |= 1 << i;
	}
	} else {
		maxim->strings_mask = 0;
	}

	*initial_brightness = 50U;
	of_property_read_u32(node, "default-brightness", initial_brightness);

	maxim->iset = MAX25014_ISET_DEFAULT_100;
	of_property_read_u32(node, "maxim,iset", &maxim->iset);

	if (maxim->iset > 15)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid iset, should be a value from 0-15, entered was %d\n",
				     maxim->iset);

	if (*initial_brightness > 100)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid initial brightness, should be a value from 0-100, entered was %d\n",
				     *initial_brightness);

	return 0;
}

static int max25014_probe(struct i2c_client *cl)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(cl);
	struct backlight_properties props;
	uint32_t initial_brightness = 50;
	struct backlight_device *bl;
	struct max25014 *maxim;
	int ret;

	maxim = devm_kzalloc(&cl->dev, sizeof(struct max25014), GFP_KERNEL);
	if (!maxim)
		return -ENOMEM;

	maxim->client = cl;

	ret = max25014_parse_dt(maxim, &initial_brightness);
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(&maxim->client->dev, "power");
	if (ret)
		return dev_err_probe(&maxim->client->dev, ret,
				     "failed to get power-supply");

	maxim->enable = devm_gpiod_get_optional(&maxim->client->dev, "enable",
						GPIOD_OUT_HIGH);
	if (IS_ERR(maxim->enable))
		return dev_err_probe(&maxim->client->dev, PTR_ERR(maxim->enable),
				    "failed to get enable gpio\n");

	/* Datasheet Electrical Characteristics tSTARTUP 2ms */
	fsleep(2000);

	maxim->regmap = devm_regmap_init_i2c(cl, &max25014_regmap_config);
	if (IS_ERR(maxim->regmap))
		return dev_err_probe(&maxim->client->dev, PTR_ERR(maxim->regmap),
			"failed to initialize the i2c regmap\n");

	i2c_set_clientdata(cl, maxim);

	ret = max25014_check_errors(maxim);
	if (ret) /* error is already reported in the above function */
		return ret;

	ret = max25014_initial_power_state(maxim);
	if (ret < 0)
		return dev_err_probe(&maxim->client->dev, ret, "Could not get enabled state\n");

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = MAX_BRIGHTNESS;
	props.brightness = initial_brightness;
	props.scale = BACKLIGHT_SCALE_LINEAR;
	props.power = ret;

	ret = max25014_configure(maxim, ret);
	if (ret)
		return dev_err_probe(&maxim->client->dev, ret, "device config error");

	bl = devm_backlight_device_register(&maxim->client->dev, id->name,
					    &maxim->client->dev, maxim,
					    &max25014_bl_ops, &props);
	if (IS_ERR(bl))
		return dev_err_probe(&maxim->client->dev, PTR_ERR(bl),
				    "failed to register backlight\n");

	maxim->bl = bl;

	backlight_update_status(maxim->bl);

	return 0;
}

static void max25014_remove(struct i2c_client *cl)
{
	struct max25014 *maxim = i2c_get_clientdata(cl);

	backlight_device_set_brightness(maxim->bl, 0);
	gpiod_set_value_cansleep(maxim->enable, 0);
}

static const struct of_device_id max25014_dt_ids[] = {
	{ .compatible = "maxim,max25014", },
	{ }
};
MODULE_DEVICE_TABLE(of, max25014_dt_ids);

static const struct i2c_device_id max25014_ids[] = {
	{ "max25014" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max25014_ids);

static struct i2c_driver max25014_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(max25014_dt_ids),
	},
	.probe = max25014_probe,
	.remove = max25014_remove,
	.id_table = max25014_ids,
};
module_i2c_driver(max25014_driver);

MODULE_DESCRIPTION("Maxim MAX25014 backlight driver");
MODULE_AUTHOR("Maud Spierings <maudspierings@gocontroll.com>");
MODULE_LICENSE("GPL");
