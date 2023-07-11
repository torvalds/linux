// SPDX-License-Identifier: GPL-2.0-only
/*
 *	LED driver for TI lp3952 controller
 *
 *	Copyright (C) 2016, DAQRI, LLC.
 *	Author: Tony Makkiel <tony.makkiel@daqri.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/leds-lp3952.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

static int lp3952_register_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;
	struct lp3952_led_array *priv = i2c_get_clientdata(client);

	ret = regmap_write(priv->regmap, reg, val);

	if (ret)
		dev_err(&client->dev, "%s: reg 0x%x, val 0x%x, err %d\n",
			__func__, reg, val, ret);
	return ret;
}

static void lp3952_on_off(struct lp3952_led_array *priv,
			  enum lp3952_leds led_id, bool on)
{
	int ret, val;

	dev_dbg(&priv->client->dev, "%s LED %d to %d\n", __func__, led_id, on);

	val = 1 << led_id;
	if (led_id == LP3952_LED_ALL)
		val = LP3952_LED_MASK_ALL;

	ret = regmap_update_bits(priv->regmap, LP3952_REG_LED_CTRL, val,
				 on ? val : 0);
	if (ret)
		dev_err(&priv->client->dev, "%s, Error %d\n", __func__, ret);
}

/*
 * Using Imax to control brightness. There are 4 possible
 * setting 25, 50, 75 and 100 % of Imax. Possible values are
 * values 0-4. 0 meaning turn off.
 */
static int lp3952_set_brightness(struct led_classdev *cdev,
				 enum led_brightness value)
{
	unsigned int reg, shift_val;
	struct lp3952_ctrl_hdl *led = container_of(cdev,
						   struct lp3952_ctrl_hdl,
						   cdev);
	struct lp3952_led_array *priv = (struct lp3952_led_array *)led->priv;

	dev_dbg(cdev->dev, "Brightness request: %d on %d\n", value,
		led->channel);

	if (value == LED_OFF) {
		lp3952_on_off(priv, led->channel, false);
		return 0;
	}

	if (led->channel > LP3952_RED_1) {
		dev_err(cdev->dev, " %s Invalid LED requested", __func__);
		return -EINVAL;
	}

	if (led->channel >= LP3952_BLUE_1) {
		reg = LP3952_REG_RGB1_MAX_I_CTRL;
		shift_val = (led->channel - LP3952_BLUE_1) * 2;
	} else {
		reg = LP3952_REG_RGB2_MAX_I_CTRL;
		shift_val = led->channel * 2;
	}

	/* Enable the LED in case it is not enabled already */
	lp3952_on_off(priv, led->channel, true);

	return regmap_update_bits(priv->regmap, reg, 3 << shift_val,
				  --value << shift_val);
}

static int lp3952_get_label(struct device *dev, const char *label, char *dest)
{
	int ret;
	const char *str;

	ret = device_property_read_string(dev, label, &str);
	if (ret)
		return ret;

	strncpy(dest, str, LP3952_LABEL_MAX_LEN);
	return 0;
}

static int lp3952_register_led_classdev(struct lp3952_led_array *priv)
{
	int i, acpi_ret, ret = -ENODEV;
	static const char *led_name_hdl[LP3952_LED_ALL] = {
		"blue2",
		"green2",
		"red2",
		"blue1",
		"green1",
		"red1"
	};

	for (i = 0; i < LP3952_LED_ALL; i++) {
		acpi_ret = lp3952_get_label(&priv->client->dev, led_name_hdl[i],
					    priv->leds[i].name);
		if (acpi_ret)
			continue;

		priv->leds[i].cdev.name = priv->leds[i].name;
		priv->leds[i].cdev.brightness = LED_OFF;
		priv->leds[i].cdev.max_brightness = LP3952_BRIGHT_MAX;
		priv->leds[i].cdev.brightness_set_blocking =
				lp3952_set_brightness;
		priv->leds[i].channel = i;
		priv->leds[i].priv = priv;

		ret = devm_led_classdev_register(&priv->client->dev,
						 &priv->leds[i].cdev);
		if (ret < 0) {
			dev_err(&priv->client->dev,
				"couldn't register LED %s\n",
				priv->leds[i].cdev.name);
			break;
		}
	}
	return ret;
}

static int lp3952_set_pattern_gen_cmd(struct lp3952_led_array *priv,
				      u8 cmd_index, u8 r, u8 g, u8 b,
				      enum lp3952_tt tt, enum lp3952_cet cet)
{
	int ret;
	struct ptrn_gen_cmd line = {
		{
			{
				.r = r,
				.g = g,
				.b = b,
				.cet = cet,
				.tt = tt
			}
		}
	};

	if (cmd_index >= LP3952_CMD_REG_COUNT)
		return -EINVAL;

	ret = lp3952_register_write(priv->client,
				    LP3952_REG_CMD_0 + cmd_index * 2,
				    line.bytes.msb);
	if (ret)
		return ret;

	return lp3952_register_write(priv->client,
				      LP3952_REG_CMD_0 + cmd_index * 2 + 1,
				      line.bytes.lsb);
}

static int lp3952_configure(struct lp3952_led_array *priv)
{
	int ret;

	/* Disable any LEDs on from any previous conf. */
	ret = lp3952_register_write(priv->client, LP3952_REG_LED_CTRL, 0);
	if (ret)
		return ret;

	/* enable rgb patter, loop */
	ret = lp3952_register_write(priv->client, LP3952_REG_PAT_GEN_CTRL,
				    LP3952_PATRN_LOOP | LP3952_PATRN_GEN_EN);
	if (ret)
		return ret;

	/* Update Bit 6 (Active mode), Select both Led sets, Bit [1:0] */
	ret = lp3952_register_write(priv->client, LP3952_REG_ENABLES,
				    LP3952_ACTIVE_MODE | LP3952_INT_B00ST_LDR);
	if (ret)
		return ret;

	/* Set Cmd1 for RGB intensity,cmd and transition time */
	return lp3952_set_pattern_gen_cmd(priv, 0, I46, I71, I100, TT0,
					   CET197);
}

static const struct regmap_config lp3952_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
	.cache_type = REGCACHE_RBTREE,
};

static int lp3952_probe(struct i2c_client *client)
{
	int status;
	struct lp3952_led_array *priv;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->enable_gpio = devm_gpiod_get(&client->dev, "nrst",
					   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->enable_gpio)) {
		status = PTR_ERR(priv->enable_gpio);
		dev_err(&client->dev, "Failed to enable gpio: %d\n", status);
		return status;
	}

	priv->regmap = devm_regmap_init_i2c(client, &lp3952_regmap);
	if (IS_ERR(priv->regmap)) {
		int err = PTR_ERR(priv->regmap);

		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			err);
		return err;
	}

	i2c_set_clientdata(client, priv);

	status = lp3952_configure(priv);
	if (status) {
		dev_err(&client->dev, "Probe failed. Device not found (%d)\n",
			status);
		return status;
	}

	status = lp3952_register_led_classdev(priv);
	if (status) {
		dev_err(&client->dev, "Unable to register led_classdev: %d\n",
			status);
		return status;
	}

	return 0;
}

static void lp3952_remove(struct i2c_client *client)
{
	struct lp3952_led_array *priv;

	priv = i2c_get_clientdata(client);
	lp3952_on_off(priv, LP3952_LED_ALL, false);
	gpiod_set_value(priv->enable_gpio, 0);
}

static const struct i2c_device_id lp3952_id[] = {
	{LP3952_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, lp3952_id);

static struct i2c_driver lp3952_i2c_driver = {
	.driver = {
			.name = LP3952_NAME,
	},
	.probe = lp3952_probe,
	.remove = lp3952_remove,
	.id_table = lp3952_id,
};

module_i2c_driver(lp3952_i2c_driver);

MODULE_AUTHOR("Tony Makkiel <tony.makkiel@daqri.com>");
MODULE_DESCRIPTION("lp3952 I2C LED controller driver");
MODULE_LICENSE("GPL v2");
