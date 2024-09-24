// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backlight driver for the Kinetic KTZ8866
 *
 * Copyright (C) 2022, 2023 Jianhua Lu <lujianhua000@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define DEFAULT_BRIGHTNESS 1500
#define MAX_BRIGHTNESS 2047
#define REG_MAX 0x15

/* reg */
#define DEVICE_ID 0x01
#define BL_CFG1 0x02
#define BL_CFG2 0x03
#define BL_BRT_LSB 0x04
#define BL_BRT_MSB 0x05
#define BL_EN 0x08
#define LCD_BIAS_CFG1 0x09
#define LCD_BIAS_CFG2 0x0A
#define LCD_BIAS_CFG3 0x0B
#define LCD_BOOST_CFG 0x0C
#define OUTP_CFG 0x0D
#define OUTN_CFG 0x0E
#define FLAG 0x0F
#define BL_OPTION1 0x10
#define BL_OPTION2 0x11
#define PWM2DIG_LSBs 0x12
#define PWM2DIG_MSBs 0x13
#define BL_DIMMING 0x14
#define PWM_RAMP_TIME 0x15

/* definition */
#define BL_EN_BIT BIT(6)
#define LCD_BIAS_EN 0x9F
#define PWM_HYST 0x5

struct ktz8866 {
	struct i2c_client *client;
	struct regmap *regmap;
	bool led_on;
	struct gpio_desc *enable_gpio;
};

static const struct regmap_config ktz8866_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int ktz8866_write(struct ktz8866 *ktz, unsigned int reg,
			 unsigned int val)
{
	return regmap_write(ktz->regmap, reg, val);
}

static int ktz8866_update_bits(struct ktz8866 *ktz, unsigned int reg,
			       unsigned int mask, unsigned int val)
{
	return regmap_update_bits(ktz->regmap, reg, mask, val);
}

static int ktz8866_backlight_update_status(struct backlight_device *backlight_dev)
{
	struct ktz8866 *ktz = bl_get_data(backlight_dev);
	unsigned int brightness = backlight_get_brightness(backlight_dev);

	if (!ktz->led_on && brightness > 0) {
		ktz8866_update_bits(ktz, BL_EN, BL_EN_BIT, BL_EN_BIT);
		ktz->led_on = true;
	} else if (brightness == 0) {
		ktz8866_update_bits(ktz, BL_EN, BL_EN_BIT, 0);
		ktz->led_on = false;
	}

	/* Set brightness */
	ktz8866_write(ktz, BL_BRT_LSB, brightness & 0x7);
	ktz8866_write(ktz, BL_BRT_MSB, (brightness >> 3) & 0xFF);

	return 0;
}

static const struct backlight_ops ktz8866_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = ktz8866_backlight_update_status,
};

static void ktz8866_init(struct ktz8866 *ktz)
{
	unsigned int val = 0;

	if (!of_property_read_u32(ktz->client->dev.of_node, "current-num-sinks", &val))
		ktz8866_write(ktz, BL_EN, BIT(val) - 1);
	else
		/* Enable all 6 current sinks if the number of current sinks isn't specified. */
		ktz8866_write(ktz, BL_EN, BIT(6) - 1);

	if (!of_property_read_u32(ktz->client->dev.of_node, "kinetic,current-ramp-delay-ms", &val)) {
		if (val <= 128)
			ktz8866_write(ktz, BL_CFG2, BIT(7) | (ilog2(val) << 3) | PWM_HYST);
		else
			ktz8866_write(ktz, BL_CFG2, BIT(7) | ((5 + val / 64) << 3) | PWM_HYST);
	}

	if (!of_property_read_u32(ktz->client->dev.of_node, "kinetic,led-enable-ramp-delay-ms", &val)) {
		if (val == 0)
			ktz8866_write(ktz, BL_DIMMING, 0);
		else {
			unsigned int ramp_off_time = ilog2(val) + 1;
			unsigned int ramp_on_time = ramp_off_time << 4;
			ktz8866_write(ktz, BL_DIMMING, ramp_on_time | ramp_off_time);
		}
	}

	if (of_property_read_bool(ktz->client->dev.of_node, "kinetic,enable-lcd-bias"))
		ktz8866_write(ktz, LCD_BIAS_CFG1, LCD_BIAS_EN);
}

static int ktz8866_probe(struct i2c_client *client)
{
	struct backlight_device *backlight_dev;
	struct backlight_properties props;
	struct ktz8866 *ktz;
	int ret = 0;

	ktz = devm_kzalloc(&client->dev, sizeof(*ktz), GFP_KERNEL);
	if (!ktz)
		return -ENOMEM;

	ktz->client = client;
	ktz->regmap = devm_regmap_init_i2c(client, &ktz8866_regmap_config);
	if (IS_ERR(ktz->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(ktz->regmap), "failed to init regmap\n");

	ret = devm_regulator_get_enable(&client->dev, "vddpos");
	if (ret)
		return dev_err_probe(&client->dev, ret, "get regulator vddpos failed\n");
	ret = devm_regulator_get_enable(&client->dev, "vddneg");
	if (ret)
		return dev_err_probe(&client->dev, ret, "get regulator vddneg failed\n");

	ktz->enable_gpio = devm_gpiod_get_optional(&client->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ktz->enable_gpio))
		return PTR_ERR(ktz->enable_gpio);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;
	props.brightness = DEFAULT_BRIGHTNESS;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	backlight_dev = devm_backlight_device_register(&client->dev, "ktz8866-backlight",
					&client->dev, ktz, &ktz8866_backlight_ops, &props);
	if (IS_ERR(backlight_dev))
		return dev_err_probe(&client->dev, PTR_ERR(backlight_dev),
				"failed to register backlight device\n");

	ktz8866_init(ktz);

	i2c_set_clientdata(client, backlight_dev);
	backlight_update_status(backlight_dev);

	return 0;
}

static void ktz8866_remove(struct i2c_client *client)
{
	struct backlight_device *backlight_dev = i2c_get_clientdata(client);
	backlight_dev->props.brightness = 0;
	backlight_update_status(backlight_dev);
}

static const struct i2c_device_id ktz8866_ids[] = {
	{ "ktz8866" },
	{}
};
MODULE_DEVICE_TABLE(i2c, ktz8866_ids);

static const struct of_device_id ktz8866_match_table[] = {
	{
		.compatible = "kinetic,ktz8866",
	},
	{},
};

static struct i2c_driver ktz8866_driver = {
	.driver = {
		.name = "ktz8866",
		.of_match_table = ktz8866_match_table,
	},
	.probe = ktz8866_probe,
	.remove = ktz8866_remove,
	.id_table = ktz8866_ids,
};

module_i2c_driver(ktz8866_driver);

MODULE_DESCRIPTION("Kinetic KTZ8866 Backlight Driver");
MODULE_AUTHOR("Jianhua Lu <lujianhua000@gmail.com>");
MODULE_LICENSE("GPL");
