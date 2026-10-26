// SPDX-License-Identifier: GPL-2.0
/*
 * LED driver for Analog Devices LTC3208 Multi-Display Driver
 *
 * Copyright 2026 Analog Devices Inc.
 *
 * Author: Jan Carlo Roleda <jancarlo.roleda@analog.com>
 *
 */
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>

/* Registers */
#define LTC3208_REG_A_GRNRED_CURRENT	0x1	/* Green and Red current DAC */
#define LTC3208_REG_B_AUXBLU_CURRENT	0x2	/* AUX and Blue current DAC */
#define LTC3208_REG_C_MAIN_CURRENT	0x3	/* Main current DAC */
#define LTC3208_REG_D_SUB_CURRENT	0x4	/* Sub current DAC */
#define LTC3208_REG_E_AUX_SELECT	0x5	/* AUX DAC Select */
#define  LTC3208_AUX1_MASK		GENMASK(1, 0)
#define  LTC3208_AUX2_MASK		GENMASK(3, 2)
#define  LTC3208_AUX3_MASK		GENMASK(5, 4)
#define  LTC3208_AUX4_MASK		GENMASK(7, 6)
#define LTC3208_REG_F_CAM_CURRENT	0x6	/* CAM (High and Low) current DAC */
#define LTC3208_REG_G_OPT		0x7	/* Device Options */
#define  LTC3208_OPT_CPO_MASK		GENMASK(7, 6)
#define  LTC3208_OPT_DIS_RGBDROP	BIT(3)
#define  LTC3208_OPT_DIS_CAMHILO	BIT(2)
#define  LTC3208_OPT_EN_RGBS		BIT(1)

#define LTC3208_MAX_BRIGHTNESS_4BIT	0xF
#define LTC3208_MAX_BRIGHTNESS_8BIT	0xFF

#define LTC3208_NUM_AUX_LEDS		4	/* Number of configurable aux channels */
#define LTC3208_NUM_REGS		7	/* Number of available registers to access */

enum ltc3208_aux_channel {
	LTC3208_AUX_CHAN_AUX = 0,
	LTC3208_AUX_CHAN_MAIN,
	LTC3208_AUX_CHAN_SUB,
	LTC3208_AUX_CHAN_CAM
};

enum ltc3208_channel {
	LTC3208_CHAN_MAIN = 0,
	LTC3208_CHAN_SUB,
	LTC3208_CHAN_AUX,
	LTC3208_CHAN_CAML,
	LTC3208_CHAN_CAMH,
	LTC3208_CHAN_RED,
	LTC3208_CHAN_BLUE,
	LTC3208_CHAN_GREEN,
	LTC3208_CHAN_N_COUNT,
};

static const char *const ltc3208_dt_aux_channels[] = {
	"adi,aux1-channel",
	"adi,aux2-channel",
	"adi,aux3-channel",
	"adi,aux4-channel"
};

static const char *const ltc3208_aux_opt[] = { "aux", "main", "sub", "cam" };

struct ltc3208_led {
	struct led_classdev cdev;
	struct regmap_field *rfield;
};

static const struct reg_default ltc3208_reg_defaults[] = {
	{ LTC3208_REG_A_GRNRED_CURRENT, 0 },
	{ LTC3208_REG_B_AUXBLU_CURRENT, 0 },
	{ LTC3208_REG_C_MAIN_CURRENT, 0 },
	{ LTC3208_REG_D_SUB_CURRENT, 0 },
	{ LTC3208_REG_E_AUX_SELECT, 0 },
	{ LTC3208_REG_F_CAM_CURRENT, 0 },
	{ LTC3208_REG_G_OPT, 0 }
};

static const struct regmap_config ltc3208_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC3208_REG_G_OPT,
	.cache_type = REGCACHE_FLAT_S,
	.reg_defaults = ltc3208_reg_defaults,
	.num_reg_defaults = LTC3208_NUM_REGS,
};

static const struct reg_field ltc3208_led_reg_field[LTC3208_CHAN_N_COUNT] = {
	[LTC3208_CHAN_MAIN]  = REG_FIELD(LTC3208_REG_C_MAIN_CURRENT, 0, 7),
	[LTC3208_CHAN_SUB]   = REG_FIELD(LTC3208_REG_D_SUB_CURRENT, 0, 7),
	[LTC3208_CHAN_BLUE]  = REG_FIELD(LTC3208_REG_B_AUXBLU_CURRENT, 0, 3),
	[LTC3208_CHAN_AUX]   = REG_FIELD(LTC3208_REG_B_AUXBLU_CURRENT, 4, 7),
	[LTC3208_CHAN_CAML]  = REG_FIELD(LTC3208_REG_F_CAM_CURRENT, 0, 3),
	[LTC3208_CHAN_CAMH]  = REG_FIELD(LTC3208_REG_F_CAM_CURRENT, 4, 7),
	[LTC3208_CHAN_RED]   = REG_FIELD(LTC3208_REG_A_GRNRED_CURRENT, 0, 3),
	[LTC3208_CHAN_GREEN] = REG_FIELD(LTC3208_REG_A_GRNRED_CURRENT, 4, 7),
};

static int ltc3208_led_set_brightness(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	struct ltc3208_led *led = container_of(led_cdev, struct ltc3208_led, cdev);
	u8 current_level = brightness;

	return regmap_field_write(led->rfield, current_level);
}

static int ltc3208_probe(struct i2c_client *client)
{
	enum ltc3208_aux_channel aux_channels[LTC3208_NUM_AUX_LEDS];
	DECLARE_BITMAP(registered_chans, LTC3208_CHAN_N_COUNT) = { };
	struct regmap *regmap;
	bool disable_rgb_aux4_dropout_signal;
	bool disable_camhl_pin;
	bool set_sub_control_pin;
	int ret;
	u8 dev_options;

	regmap = devm_regmap_init_i2c(client, &ltc3208_regmap_cfg);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap), "Failed to initialize regmap");

	regcache_mark_dirty(regmap);
	ret = regcache_sync(regmap);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to sync register cache");

	disable_camhl_pin = device_property_read_bool(&client->dev, "adi,disable-camhl-pin");

	set_sub_control_pin = device_property_read_bool(&client->dev, "adi,cfg-enrgbs-pin");

	disable_rgb_aux4_dropout_signal =
		device_property_read_bool(&client->dev, "adi,disable-rgb-aux4-dropout");

	dev_options = FIELD_PREP(LTC3208_OPT_EN_RGBS, set_sub_control_pin) |
		      FIELD_PREP(LTC3208_OPT_DIS_CAMHILO, disable_camhl_pin) |
		      FIELD_PREP(LTC3208_OPT_CPO_MASK, 0) |
		      FIELD_PREP(LTC3208_OPT_DIS_RGBDROP, disable_rgb_aux4_dropout_signal);

	ret = regmap_write(regmap, LTC3208_REG_G_OPT, dev_options);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to set device options register");

	/* Initialize AUX channel configurations */
	for (int i = 0; i < LTC3208_NUM_AUX_LEDS; i++) {
		/* Default value (AUX) if property is not present in DT */
		if (!device_property_present(&client->dev, ltc3208_dt_aux_channels[i])) {
			aux_channels[i] = LTC3208_AUX_CHAN_AUX;
			continue;
		}

		ret = device_property_match_property_string(&client->dev,
							    ltc3208_dt_aux_channels[i],
							    ltc3208_aux_opt, LTC3208_NUM_AUX_LEDS);
		if (ret < 0)
			return dev_err_probe(&client->dev, ret, "Error reading AUX Channel %d", i);

		aux_channels[i] = ret;
	}

	dev_options = FIELD_PREP(LTC3208_AUX1_MASK, aux_channels[0]) |
		      FIELD_PREP(LTC3208_AUX2_MASK, aux_channels[1]) |
		      FIELD_PREP(LTC3208_AUX3_MASK, aux_channels[2]) |
		      FIELD_PREP(LTC3208_AUX4_MASK, aux_channels[3]);

	ret = regmap_write(regmap, LTC3208_REG_E_AUX_SELECT, dev_options);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Error writing to aux channel register");

	device_for_each_child_node_scoped(&client->dev, child) {
		struct ltc3208_led *led;
		struct led_init_data init_data = { };
		u32 chan;

		ret = fwnode_property_read_u32(child, "reg", &chan);
		if (ret)
			return dev_err_probe(&client->dev, ret,
					     "Failed to get register value of LED");
		if (chan >= LTC3208_CHAN_N_COUNT)
			return dev_err_probe(&client->dev, -EINVAL, "%u is an invalid LED ID",
					     chan);
		if (test_and_set_bit(chan, registered_chans))
			return dev_err_probe(&client->dev, -EINVAL, "%u is already registered",
					     chan);

		led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->rfield = devm_regmap_field_alloc(&client->dev, regmap,
						      ltc3208_led_reg_field[chan]);
		if (IS_ERR(led->rfield))
			return dev_err_probe(&client->dev, PTR_ERR(led->rfield),
					     "Cannot allocate regmap field");
		led->cdev.brightness_set_blocking = ltc3208_led_set_brightness;
		led->cdev.max_brightness = LTC3208_MAX_BRIGHTNESS_4BIT;

		if (chan == LTC3208_CHAN_MAIN || chan == LTC3208_CHAN_SUB)
			led->cdev.max_brightness = LTC3208_MAX_BRIGHTNESS_8BIT;

		init_data.fwnode = child;

		ret = devm_led_classdev_register_ext(&client->dev, &led->cdev, &init_data);
		if (ret)
			return dev_err_probe(&client->dev, ret, "Failed to register LED %u", chan);
	}

	return 0;
}

static const struct of_device_id ltc3208_match_table[] = {
	{ .compatible = "adi,ltc3208" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc3208_match_table);

static struct i2c_driver ltc3208_driver = {
	.driver = {
		.name = "ltc3208",
		.of_match_table = ltc3208_match_table,
	},
	.probe = ltc3208_probe,
};
module_i2c_driver(ltc3208_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jan Carlo Roleda <jancarlo.roleda@analog.com>");
MODULE_DESCRIPTION("LTC3208 LED Driver");
