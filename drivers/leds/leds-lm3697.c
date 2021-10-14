// SPDX-License-Identifier: GPL-2.0
// TI LM3697 LED chip family driver
// Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/leds-ti-lmu-common.h>

#define LM3697_REV			0x0
#define LM3697_RESET			0x1
#define LM3697_OUTPUT_CONFIG		0x10
#define LM3697_CTRL_A_RAMP		0x11
#define LM3697_CTRL_B_RAMP		0x12
#define LM3697_CTRL_A_B_RT_RAMP		0x13
#define LM3697_CTRL_A_B_RAMP_CFG	0x14
#define LM3697_CTRL_A_B_BRT_CFG		0x16
#define LM3697_CTRL_A_FS_CURR_CFG	0x17
#define LM3697_CTRL_B_FS_CURR_CFG	0x18
#define LM3697_PWM_CFG			0x1c
#define LM3697_CTRL_A_BRT_LSB		0x20
#define LM3697_CTRL_A_BRT_MSB		0x21
#define LM3697_CTRL_B_BRT_LSB		0x22
#define LM3697_CTRL_B_BRT_MSB		0x23
#define LM3697_CTRL_ENABLE		0x24

#define LM3697_SW_RESET		BIT(0)

#define LM3697_CTRL_A_EN	BIT(0)
#define LM3697_CTRL_B_EN	BIT(1)
#define LM3697_CTRL_A_B_EN	(LM3697_CTRL_A_EN | LM3697_CTRL_B_EN)

#define LM3697_MAX_LED_STRINGS	3

#define LM3697_CONTROL_A	0
#define LM3697_CONTROL_B	1
#define LM3697_MAX_CONTROL_BANKS 2

/**
 * struct lm3697_led -
 * @hvled_strings: Array of LED strings associated with a control bank
 * @label: LED label
 * @led_dev: LED class device
 * @priv: Pointer to the device struct
 * @lmu_data: Register and setting values for common code
 * @control_bank: Control bank the LED is associated to. 0 is control bank A
 *		   1 is control bank B
 */
struct lm3697_led {
	u32 hvled_strings[LM3697_MAX_LED_STRINGS];
	char label[LED_MAX_NAME_SIZE];
	struct led_classdev led_dev;
	struct lm3697 *priv;
	struct ti_lmu_bank lmu_data;
	int control_bank;
	int enabled;
	int num_leds;
};

/**
 * struct lm3697 -
 * @enable_gpio: Hardware enable gpio
 * @regulator: LED supply regulator pointer
 * @client: Pointer to the I2C client
 * @regmap: Devices register map
 * @dev: Pointer to the devices device struct
 * @lock: Lock for reading/writing the device
 * @leds: Array of LED strings
 */
struct lm3697 {
	struct gpio_desc *enable_gpio;
	struct regulator *regulator;
	struct i2c_client *client;
	struct regmap *regmap;
	struct device *dev;
	struct mutex lock;

	int bank_cfg;
	int num_banks;

	struct lm3697_led leds[];
};

static const struct reg_default lm3697_reg_defs[] = {
	{LM3697_OUTPUT_CONFIG, 0x6},
	{LM3697_CTRL_A_RAMP, 0x0},
	{LM3697_CTRL_B_RAMP, 0x0},
	{LM3697_CTRL_A_B_RT_RAMP, 0x0},
	{LM3697_CTRL_A_B_RAMP_CFG, 0x0},
	{LM3697_CTRL_A_B_BRT_CFG, 0x0},
	{LM3697_CTRL_A_FS_CURR_CFG, 0x13},
	{LM3697_CTRL_B_FS_CURR_CFG, 0x13},
	{LM3697_PWM_CFG, 0xc},
	{LM3697_CTRL_A_BRT_LSB, 0x0},
	{LM3697_CTRL_A_BRT_MSB, 0x0},
	{LM3697_CTRL_B_BRT_LSB, 0x0},
	{LM3697_CTRL_B_BRT_MSB, 0x0},
	{LM3697_CTRL_ENABLE, 0x0},
};

static const struct regmap_config lm3697_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LM3697_CTRL_ENABLE,
	.reg_defaults = lm3697_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(lm3697_reg_defs),
	.cache_type = REGCACHE_FLAT,
};

static int lm3697_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness brt_val)
{
	struct lm3697_led *led = container_of(led_cdev, struct lm3697_led,
					      led_dev);
	int ctrl_en_val = (1 << led->control_bank);
	struct device *dev = led->priv->dev;
	int ret;

	mutex_lock(&led->priv->lock);

	if (brt_val == LED_OFF) {
		ret = regmap_update_bits(led->priv->regmap, LM3697_CTRL_ENABLE,
					 ctrl_en_val, ~ctrl_en_val);
		if (ret) {
			dev_err(dev, "Cannot write ctrl register\n");
			goto brightness_out;
		}

		led->enabled = LED_OFF;
	} else {
		ret = ti_lmu_common_set_brightness(&led->lmu_data, brt_val);
		if (ret) {
			dev_err(dev, "Cannot write brightness\n");
			goto brightness_out;
		}

		if (!led->enabled) {
			ret = regmap_update_bits(led->priv->regmap,
						 LM3697_CTRL_ENABLE,
						 ctrl_en_val, ctrl_en_val);
			if (ret) {
				dev_err(dev, "Cannot enable the device\n");
				goto brightness_out;
			}

			led->enabled = brt_val;
		}
	}

brightness_out:
	mutex_unlock(&led->priv->lock);
	return ret;
}

static int lm3697_init(struct lm3697 *priv)
{
	struct device *dev = priv->dev;
	struct lm3697_led *led;
	int i, ret;

	if (priv->enable_gpio) {
		gpiod_direction_output(priv->enable_gpio, 1);
	} else {
		ret = regmap_write(priv->regmap, LM3697_RESET, LM3697_SW_RESET);
		if (ret) {
			dev_err(dev, "Cannot reset the device\n");
			goto out;
		}
	}

	ret = regmap_write(priv->regmap, LM3697_CTRL_ENABLE, 0x0);
	if (ret) {
		dev_err(dev, "Cannot write ctrl enable\n");
		goto out;
	}

	ret = regmap_write(priv->regmap, LM3697_OUTPUT_CONFIG, priv->bank_cfg);
	if (ret)
		dev_err(dev, "Cannot write OUTPUT config\n");

	for (i = 0; i < priv->num_banks; i++) {
		led = &priv->leds[i];
		ret = ti_lmu_common_set_ramp(&led->lmu_data);
		if (ret)
			dev_err(dev, "Setting the ramp rate failed\n");
	}
out:
	return ret;
}

static int lm3697_probe_dt(struct lm3697 *priv)
{
	struct fwnode_handle *child = NULL;
	struct device *dev = priv->dev;
	struct lm3697_led *led;
	int ret = -EINVAL;
	int control_bank;
	size_t i = 0;
	int j;

	priv->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						    GPIOD_OUT_LOW);
	if (IS_ERR(priv->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->enable_gpio),
					  "Failed to get enable GPIO\n");

	priv->regulator = devm_regulator_get(dev, "vled");
	if (IS_ERR(priv->regulator))
		priv->regulator = NULL;

	device_for_each_child_node(dev, child) {
		struct led_init_data init_data = {};

		ret = fwnode_property_read_u32(child, "reg", &control_bank);
		if (ret) {
			dev_err(dev, "reg property missing\n");
			fwnode_handle_put(child);
			goto child_out;
		}

		if (control_bank > LM3697_CONTROL_B) {
			dev_err(dev, "reg property is invalid\n");
			ret = -EINVAL;
			fwnode_handle_put(child);
			goto child_out;
		}

		led = &priv->leds[i];

		ret = ti_lmu_common_get_brt_res(dev, child, &led->lmu_data);
		if (ret)
			dev_warn(dev,
				 "brightness resolution property missing\n");

		led->control_bank = control_bank;
		led->lmu_data.regmap = priv->regmap;
		led->lmu_data.runtime_ramp_reg = LM3697_CTRL_A_RAMP +
						 control_bank;
		led->lmu_data.msb_brightness_reg = LM3697_CTRL_A_BRT_MSB +
						   led->control_bank * 2;
		led->lmu_data.lsb_brightness_reg = LM3697_CTRL_A_BRT_LSB +
						   led->control_bank * 2;

		led->num_leds = fwnode_property_count_u32(child, "led-sources");
		if (led->num_leds > LM3697_MAX_LED_STRINGS) {
			dev_err(dev, "Too many LED strings defined\n");
			continue;
		}

		ret = fwnode_property_read_u32_array(child, "led-sources",
						    led->hvled_strings,
						    led->num_leds);
		if (ret) {
			dev_err(dev, "led-sources property missing\n");
			fwnode_handle_put(child);
			goto child_out;
		}

		for (j = 0; j < led->num_leds; j++)
			priv->bank_cfg |=
				(led->control_bank << led->hvled_strings[j]);

		ret = ti_lmu_common_get_ramp_params(dev, child, &led->lmu_data);
		if (ret)
			dev_warn(dev, "runtime-ramp properties missing\n");

		init_data.fwnode = child;
		init_data.devicename = priv->client->name;
		/* for backwards compatibility if `label` is not present */
		init_data.default_label = ":";

		led->priv = priv;
		led->led_dev.max_brightness = led->lmu_data.max_brightness;
		led->led_dev.brightness_set_blocking = lm3697_brightness_set;

		ret = devm_led_classdev_register_ext(dev, &led->led_dev,
						     &init_data);
		if (ret) {
			dev_err(dev, "led register err: %d\n", ret);
			fwnode_handle_put(child);
			goto child_out;
		}

		i++;
	}

child_out:
	return ret;
}

static int lm3697_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct lm3697 *led;
	int count;
	int ret;

	count = device_get_child_node_count(dev);
	if (!count || count > LM3697_MAX_CONTROL_BANKS) {
		dev_err(dev, "Strange device tree!");
		return -ENODEV;
	}

	led = devm_kzalloc(dev, struct_size(led, leds, count), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	mutex_init(&led->lock);
	i2c_set_clientdata(client, led);

	led->client = client;
	led->dev = dev;
	led->num_banks = count;
	led->regmap = devm_regmap_init_i2c(client, &lm3697_regmap_config);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	ret = lm3697_probe_dt(led);
	if (ret)
		return ret;

	return lm3697_init(led);
}

static int lm3697_remove(struct i2c_client *client)
{
	struct lm3697 *led = i2c_get_clientdata(client);
	struct device *dev = &led->client->dev;
	int ret;

	ret = regmap_update_bits(led->regmap, LM3697_CTRL_ENABLE,
				 LM3697_CTRL_A_B_EN, 0);
	if (ret) {
		dev_err(dev, "Failed to disable the device\n");
		return ret;
	}

	if (led->enable_gpio)
		gpiod_direction_output(led->enable_gpio, 0);

	if (led->regulator) {
		ret = regulator_disable(led->regulator);
		if (ret)
			dev_err(dev, "Failed to disable regulator\n");
	}

	mutex_destroy(&led->lock);

	return 0;
}

static const struct i2c_device_id lm3697_id[] = {
	{ "lm3697", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3697_id);

static const struct of_device_id of_lm3697_leds_match[] = {
	{ .compatible = "ti,lm3697", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lm3697_leds_match);

static struct i2c_driver lm3697_driver = {
	.driver = {
		.name	= "lm3697",
		.of_match_table = of_lm3697_leds_match,
	},
	.probe		= lm3697_probe,
	.remove		= lm3697_remove,
	.id_table	= lm3697_id,
};
module_i2c_driver(lm3697_driver);

MODULE_DESCRIPTION("Texas Instruments LM3697 LED driver");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
