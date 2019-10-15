// SPDX-License-Identifier: GPL-2.0
// Flash and torch driver for Texas Instruments LM3601X LED
// Flash driver chip family
// Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define LM3601X_LED_IR		0x0
#define LM3601X_LED_TORCH	0x1

/* Registers */
#define LM3601X_ENABLE_REG	0x01
#define LM3601X_CFG_REG		0x02
#define LM3601X_LED_FLASH_REG	0x03
#define LM3601X_LED_TORCH_REG	0x04
#define LM3601X_FLAGS_REG	0x05
#define LM3601X_DEV_ID_REG	0x06

#define LM3601X_SW_RESET	BIT(7)

/* Enable Mode bits */
#define LM3601X_MODE_STANDBY	0x00
#define LM3601X_MODE_IR_DRV	BIT(0)
#define LM3601X_MODE_TORCH	BIT(1)
#define LM3601X_MODE_STROBE	(BIT(0) | BIT(1))
#define LM3601X_STRB_EN		BIT(2)
#define LM3601X_STRB_EDGE_TRIG	BIT(3)
#define LM3601X_IVFM_EN		BIT(4)

#define LM36010_BOOST_LIMIT_28	BIT(5)
#define LM36010_BOOST_FREQ_4MHZ	BIT(6)
#define LM36010_BOOST_MODE_PASS	BIT(7)

/* Flag Mask */
#define LM3601X_FLASH_TIME_OUT	BIT(0)
#define LM3601X_UVLO_FAULT	BIT(1)
#define LM3601X_THERM_SHUTDOWN	BIT(2)
#define LM3601X_THERM_CURR	BIT(3)
#define LM36010_CURR_LIMIT	BIT(4)
#define LM3601X_SHORT_FAULT	BIT(5)
#define LM3601X_IVFM_TRIP	BIT(6)
#define LM36010_OVP_FAULT	BIT(7)

#define LM3601X_MAX_TORCH_I_UA	376000
#define LM3601X_MIN_TORCH_I_UA	2400
#define LM3601X_TORCH_REG_DIV	2965

#define LM3601X_MAX_STROBE_I_UA	1500000
#define LM3601X_MIN_STROBE_I_UA	11000
#define LM3601X_STROBE_REG_DIV	11800

#define LM3601X_TIMEOUT_MASK	0x1e
#define LM3601X_ENABLE_MASK	(LM3601X_MODE_IR_DRV | LM3601X_MODE_TORCH)

#define LM3601X_LOWER_STEP_US	40000
#define LM3601X_UPPER_STEP_US	200000
#define LM3601X_MIN_TIMEOUT_US	40000
#define LM3601X_MAX_TIMEOUT_US	1600000
#define LM3601X_TIMEOUT_XOVER_US 400000

enum lm3601x_type {
	CHIP_LM36010 = 0,
	CHIP_LM36011,
};

/**
 * struct lm3601x_led -
 * @fled_cdev: flash LED class device pointer
 * @client: Pointer to the I2C client
 * @regmap: Devices register map
 * @lock: Lock for reading/writing the device
 * @led_name: LED label for the Torch or IR LED
 * @flash_timeout: the timeout for the flash
 * @last_flag: last known flags register value
 * @torch_current_max: maximum current for the torch
 * @flash_current_max: maximum current for the flash
 * @max_flash_timeout: maximum timeout for the flash
 * @led_mode: The mode to enable either IR or Torch
 */
struct lm3601x_led {
	struct led_classdev_flash fled_cdev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex lock;

	unsigned int flash_timeout;
	unsigned int last_flag;

	u32 torch_current_max;
	u32 flash_current_max;
	u32 max_flash_timeout;

	u32 led_mode;
};

static const struct reg_default lm3601x_regmap_defs[] = {
	{ LM3601X_ENABLE_REG, 0x20 },
	{ LM3601X_CFG_REG, 0x15 },
	{ LM3601X_LED_FLASH_REG, 0x00 },
	{ LM3601X_LED_TORCH_REG, 0x00 },
};

static bool lm3601x_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LM3601X_FLAGS_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config lm3601x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = LM3601X_DEV_ID_REG,
	.reg_defaults = lm3601x_regmap_defs,
	.num_reg_defaults = ARRAY_SIZE(lm3601x_regmap_defs),
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = lm3601x_volatile_reg,
};

static struct lm3601x_led *fled_cdev_to_led(struct led_classdev_flash *fled_cdev)
{
	return container_of(fled_cdev, struct lm3601x_led, fled_cdev);
}

static int lm3601x_read_faults(struct lm3601x_led *led)
{
	int flags_val;
	int ret;

	ret = regmap_read(led->regmap, LM3601X_FLAGS_REG, &flags_val);
	if (ret < 0)
		return -EIO;

	led->last_flag = 0;

	if (flags_val & LM36010_OVP_FAULT)
		led->last_flag |= LED_FAULT_OVER_VOLTAGE;

	if (flags_val & (LM3601X_THERM_SHUTDOWN | LM3601X_THERM_CURR))
		led->last_flag |= LED_FAULT_OVER_TEMPERATURE;

	if (flags_val & LM3601X_SHORT_FAULT)
		led->last_flag |= LED_FAULT_SHORT_CIRCUIT;

	if (flags_val & LM36010_CURR_LIMIT)
		led->last_flag |= LED_FAULT_OVER_CURRENT;

	if (flags_val & LM3601X_UVLO_FAULT)
		led->last_flag |= LED_FAULT_UNDER_VOLTAGE;

	if (flags_val & LM3601X_IVFM_TRIP)
		led->last_flag |= LED_FAULT_INPUT_VOLTAGE;

	if (flags_val & LM3601X_THERM_SHUTDOWN)
		led->last_flag |= LED_FAULT_LED_OVER_TEMPERATURE;

	return led->last_flag;
}

static int lm3601x_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(cdev);
	struct lm3601x_led *led = fled_cdev_to_led(fled_cdev);
	int ret, led_mode_val;

	mutex_lock(&led->lock);

	ret = lm3601x_read_faults(led);
	if (ret < 0)
		goto out;

	if (led->led_mode == LM3601X_LED_TORCH)
		led_mode_val = LM3601X_MODE_TORCH;
	else
		led_mode_val = LM3601X_MODE_IR_DRV;

	if (brightness == LED_OFF) {
		ret = regmap_update_bits(led->regmap, LM3601X_ENABLE_REG,
					led_mode_val, LED_OFF);
		goto out;
	}

	ret = regmap_write(led->regmap, LM3601X_LED_TORCH_REG, brightness);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(led->regmap, LM3601X_ENABLE_REG,
				LM3601X_MODE_TORCH | LM3601X_MODE_IR_DRV,
				led_mode_val);
out:
	mutex_unlock(&led->lock);
	return ret;
}

static int lm3601x_strobe_set(struct led_classdev_flash *fled_cdev,
				bool state)
{
	struct lm3601x_led *led = fled_cdev_to_led(fled_cdev);
	int timeout_reg_val;
	int current_timeout;
	int ret;

	mutex_lock(&led->lock);

	ret = regmap_read(led->regmap, LM3601X_CFG_REG, &current_timeout);
	if (ret < 0)
		goto out;

	if (led->flash_timeout >= LM3601X_TIMEOUT_XOVER_US)
		timeout_reg_val = led->flash_timeout / LM3601X_UPPER_STEP_US + 0x07;
	else
		timeout_reg_val = led->flash_timeout / LM3601X_LOWER_STEP_US - 0x01;

	if (led->flash_timeout != current_timeout)
		ret = regmap_update_bits(led->regmap, LM3601X_CFG_REG,
					LM3601X_TIMEOUT_MASK, timeout_reg_val);

	if (state)
		ret = regmap_update_bits(led->regmap, LM3601X_ENABLE_REG,
					LM3601X_MODE_TORCH | LM3601X_MODE_IR_DRV,
					LM3601X_MODE_STROBE);
	else
		ret = regmap_update_bits(led->regmap, LM3601X_ENABLE_REG,
					LM3601X_MODE_STROBE, LED_OFF);

	ret = lm3601x_read_faults(led);
out:
	mutex_unlock(&led->lock);
	return ret;
}

static int lm3601x_flash_brightness_set(struct led_classdev_flash *fled_cdev,
					u32 brightness)
{
	struct lm3601x_led *led = fled_cdev_to_led(fled_cdev);
	u8 brightness_val;
	int ret;

	mutex_lock(&led->lock);
	ret = lm3601x_read_faults(led);
	if (ret < 0)
		goto out;

	if (brightness == LED_OFF) {
		ret = regmap_update_bits(led->regmap, LM3601X_ENABLE_REG,
					LM3601X_MODE_STROBE, LED_OFF);
		goto out;
	}

	brightness_val = brightness / LM3601X_STROBE_REG_DIV;

	ret = regmap_write(led->regmap, LM3601X_LED_FLASH_REG, brightness_val);
out:
	mutex_unlock(&led->lock);
	return ret;
}

static int lm3601x_flash_timeout_set(struct led_classdev_flash *fled_cdev,
				u32 timeout)
{
	struct lm3601x_led *led = fled_cdev_to_led(fled_cdev);

	mutex_lock(&led->lock);

	led->flash_timeout = timeout;

	mutex_unlock(&led->lock);

	return 0;
}

static int lm3601x_strobe_get(struct led_classdev_flash *fled_cdev, bool *state)
{
	struct lm3601x_led *led = fled_cdev_to_led(fled_cdev);
	int strobe_state;
	int ret;

	mutex_lock(&led->lock);

	ret = regmap_read(led->regmap, LM3601X_ENABLE_REG, &strobe_state);
	if (ret < 0)
		goto out;

	*state = strobe_state & LM3601X_MODE_STROBE;

out:
	mutex_unlock(&led->lock);
	return ret;
}

static int lm3601x_flash_fault_get(struct led_classdev_flash *fled_cdev,
				u32 *fault)
{
	struct lm3601x_led *led = fled_cdev_to_led(fled_cdev);

	lm3601x_read_faults(led);

	*fault = led->last_flag;

	return 0;
}

static const struct led_flash_ops flash_ops = {
	.flash_brightness_set	= lm3601x_flash_brightness_set,
	.strobe_set		= lm3601x_strobe_set,
	.strobe_get		= lm3601x_strobe_get,
	.timeout_set		= lm3601x_flash_timeout_set,
	.fault_get		= lm3601x_flash_fault_get,
};

static int lm3601x_register_leds(struct lm3601x_led *led,
				 struct fwnode_handle *fwnode)
{
	struct led_classdev *led_cdev;
	struct led_flash_setting *setting;
	struct led_init_data init_data = {};

	led->fled_cdev.ops = &flash_ops;

	setting = &led->fled_cdev.timeout;
	setting->min = LM3601X_MIN_TIMEOUT_US;
	setting->max = led->max_flash_timeout;
	setting->step = LM3601X_LOWER_STEP_US;
	setting->val = led->max_flash_timeout;

	setting = &led->fled_cdev.brightness;
	setting->min = LM3601X_MIN_STROBE_I_UA;
	setting->max = led->flash_current_max;
	setting->step = LM3601X_TORCH_REG_DIV;
	setting->val = led->flash_current_max;

	led_cdev = &led->fled_cdev.led_cdev;
	led_cdev->brightness_set_blocking = lm3601x_brightness_set;
	led_cdev->max_brightness = DIV_ROUND_UP(led->torch_current_max,
						LM3601X_TORCH_REG_DIV);
	led_cdev->flags |= LED_DEV_CAP_FLASH;

	init_data.fwnode = fwnode;
	init_data.devicename = led->client->name;
	init_data.default_label = (led->led_mode == LM3601X_LED_TORCH) ?
					"torch" : "infrared";

	return led_classdev_flash_register_ext(&led->client->dev,
						&led->fled_cdev, &init_data);
}

static int lm3601x_parse_node(struct lm3601x_led *led,
			      struct fwnode_handle **fwnode)
{
	struct fwnode_handle *child = NULL;
	int ret = -ENODEV;

	child = device_get_next_child_node(&led->client->dev, child);
	if (!child) {
		dev_err(&led->client->dev, "No LED Child node\n");
		return ret;
	}

	ret = fwnode_property_read_u32(child, "reg", &led->led_mode);
	if (ret) {
		dev_err(&led->client->dev, "reg DT property missing\n");
		goto out_err;
	}

	if (led->led_mode > LM3601X_LED_TORCH ||
	    led->led_mode < LM3601X_LED_IR) {
		dev_warn(&led->client->dev, "Invalid led mode requested\n");
		ret = -EINVAL;
		goto out_err;
	}

	ret = fwnode_property_read_u32(child, "led-max-microamp",
					&led->torch_current_max);
	if (ret) {
		dev_warn(&led->client->dev,
			"led-max-microamp DT property missing\n");
		goto out_err;
	}

	ret = fwnode_property_read_u32(child, "flash-max-microamp",
				&led->flash_current_max);
	if (ret) {
		dev_warn(&led->client->dev,
			 "flash-max-microamp DT property missing\n");
		goto out_err;
	}

	ret = fwnode_property_read_u32(child, "flash-max-timeout-us",
				&led->max_flash_timeout);
	if (ret) {
		dev_warn(&led->client->dev,
			 "flash-max-timeout-us DT property missing\n");
		goto out_err;
	}

	*fwnode = child;

out_err:
	fwnode_handle_put(child);
	return ret;
}

static int lm3601x_probe(struct i2c_client *client)
{
	struct lm3601x_led *led;
	struct fwnode_handle *fwnode;
	int ret;

	led = devm_kzalloc(&client->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->client = client;
	i2c_set_clientdata(client, led);

	ret = lm3601x_parse_node(led, &fwnode);
	if (ret)
		return -ENODEV;

	led->regmap = devm_regmap_init_i2c(client, &lm3601x_regmap);
	if (IS_ERR(led->regmap)) {
		ret = PTR_ERR(led->regmap);
		dev_err(&client->dev,
			"Failed to allocate register map: %d\n", ret);
		return ret;
	}

	mutex_init(&led->lock);

	return lm3601x_register_leds(led, fwnode);
}

static int lm3601x_remove(struct i2c_client *client)
{
	struct lm3601x_led *led = i2c_get_clientdata(client);

	led_classdev_flash_unregister(&led->fled_cdev);
	mutex_destroy(&led->lock);

	return regmap_update_bits(led->regmap, LM3601X_ENABLE_REG,
			   LM3601X_ENABLE_MASK,
			   LM3601X_MODE_STANDBY);
}

static const struct i2c_device_id lm3601x_id[] = {
	{ "LM36010", CHIP_LM36010 },
	{ "LM36011", CHIP_LM36011 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3601x_id);

static const struct of_device_id of_lm3601x_leds_match[] = {
	{ .compatible = "ti,lm36010", },
	{ .compatible = "ti,lm36011", },
	{ }
};
MODULE_DEVICE_TABLE(of, of_lm3601x_leds_match);

static struct i2c_driver lm3601x_i2c_driver = {
	.driver = {
		.name = "lm3601x",
		.of_match_table = of_lm3601x_leds_match,
	},
	.probe_new = lm3601x_probe,
	.remove = lm3601x_remove,
	.id_table = lm3601x_id,
};
module_i2c_driver(lm3601x_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments Flash Lighting driver for LM3601X");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
