// SPDX-License-Identifier: GPL-2.0
/*
 * LTC3220 18-Channel LED Driver
 *
 * Copyright 2026 Analog Devices Inc.
 *
 * Author: Edelweise Escala <edelweise.escala@analog.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/types.h>

/* LTC3220 Registers */
#define LTC3220_COMMAND_REG				0x00
#define   LTC3220_QUICK_WRITE_MASK			BIT(0)
#define   LTC3220_SHUTDOWN_MASK				BIT(3)

#define LTC3220_ULED_REG(x)				(0x01 + (x))
#define   LTC3220_LED_CURRENT_MASK			GENMASK(5, 0)
#define   LTC3220_LED_MODE_MASK				GENMASK(7, 6)

#define LTC3220_GRAD_BLINK_REG				0x13
#define   LTC3220_GRADATION_MASK			GENMASK(2, 0)
#define   LTC3220_GRADATION_DIRECTION_MASK		BIT(0)
#define   LTC3220_GRADATION_PERIOD_MASK			GENMASK(2, 1)
#define   LTC3220_BLINK_MASK				GENMASK(4, 3)

#define LTC3220_NUM_LEDS				18
#define LTC3220_MAX_BRIGHTNESS				63

#define LTC3220_GRADATION_RAMP_TIME_240MS		240
#define LTC3220_GRADATION_RAMP_TIME_480MS		480

#define LTC3220_BLINK_ON_156MS				156
#define LTC3220_BLINK_ON_625MS				625
#define LTC3220_BLINK_PERIOD_1250MS			1250
#define LTC3220_BLINK_PERIOD_2500MS			2500

#define LTC3220_BLINK_SHORT_ON_TIME			BIT(0)
#define LTC3220_BLINK_LONG_PERIOD			BIT(1)

enum ltc3220_led_mode {
	LTC3220_NORMAL_MODE,
	LTC3220_BLINK_MODE,
	LTC3220_GRADATION_MODE,
};

enum ltc3220_blink_mode {
	LTC3220_BLINK_MODE_625MS_1250MS,
	LTC3220_BLINK_MODE_156MS_1250MS,
	LTC3220_BLINK_MODE_625MS_2500MS,
	LTC3220_BLINK_MODE_156MS_2500MS
};

enum ltc3220_gradation_mode {
	LTC3220_GRADATION_MODE_DISABLED,
	LTC3220_GRADATION_MODE_240MS_RAMP_TIME,
	LTC3220_GRADATION_MODE_480MS_RAMP_TIME,
	LTC3220_GRADATION_MODE_960MS_RAMP_TIME
};

static const struct regmap_config ltc3220_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC3220_GRAD_BLINK_REG,
	.cache_type = REGCACHE_FLAT_S,
};

struct ltc3220_uled_cfg {
	struct led_classdev led_cdev;
	u8 reg_value;
	u8 led_index;
	bool registered;
};

struct ltc3220 {
	struct ltc3220_uled_cfg uled_cfg[LTC3220_NUM_LEDS];
	struct regmap *regmap;
	struct mutex lock;
};

/*
 * Set LED brightness. Hardware supports 0-63 brightness levels.
 * Mode switching (blink/gradation) is handled through dedicated callbacks.
 *
 * In aggregated mode only a single LED (reg = 1) is registered and the
 * hardware quick-write feature propagates the write to all 18 channels, so
 * there is no need to update the other registers explicitly.
 */
static int __ltc3220_set_led_data(struct ltc3220 *ltc3220,
				  struct ltc3220_uled_cfg *uled_cfg,
				  enum led_brightness brightness)
{
	int ret;

	brightness &= LTC3220_LED_CURRENT_MASK;

	ret = regmap_write(ltc3220->regmap, LTC3220_ULED_REG(uled_cfg->led_index),
			   brightness);
	if (ret)
		return ret;

	uled_cfg->reg_value = brightness;

	return 0;
}

static int ltc3220_set_led_data(struct led_classdev *led_cdev,
				enum led_brightness brightness)
{
	struct ltc3220_uled_cfg *uled_cfg = container_of(led_cdev, struct ltc3220_uled_cfg,
							 led_cdev);
	struct ltc3220 *ltc3220 = container_of(uled_cfg - uled_cfg->led_index, struct ltc3220,
					       uled_cfg[0]);
	int ret;

	mutex_lock(&ltc3220->lock);
	ret = __ltc3220_set_led_data(ltc3220, uled_cfg, brightness);
	mutex_unlock(&ltc3220->lock);

	return ret;
}

static enum led_brightness ltc3220_get_led_data(struct led_classdev *led_cdev)
{
	struct ltc3220_uled_cfg *uled_cfg = container_of(led_cdev, struct ltc3220_uled_cfg,
							 led_cdev);

	return uled_cfg->reg_value;
}

/*
 * LTC3220 pattern support for hardware-assisted breathing/gradation.
 * The hardware supports 3 gradation ramp times (240ms, 480ms, 960ms)
 * and can ramp up or down. The gradation period and direction are chip-global
 * registers (LTC3220_GRAD_BLINK_REG), affecting all 18 channels simultaneously.
 * This is a hardware limitation, not a driver bug.
 *
 * Pattern array interpretation:
 *   pattern[0].brightness = start brightness (0-63)
 *   pattern[0].delta_t = ramp time in milliseconds
 *   pattern[1].brightness = end brightness (0-63)
 *   pattern[1].delta_t = (optional, can be 0 or same as pattern[0].delta_t)
 */
static int ltc3220_pattern_set(struct led_classdev *led_cdev,
			       struct led_pattern *pattern,
			       u32 len, int repeat)
{
	struct ltc3220_uled_cfg *uled_cfg = container_of(led_cdev, struct ltc3220_uled_cfg,
							 led_cdev);
	struct ltc3220 *ltc3220 = container_of(uled_cfg - uled_cfg->led_index, struct ltc3220,
					       uled_cfg[0]);
	u8 gradation_period;
	u8 start_brightness;
	u8 end_brightness;
	u8 gradation_val;
	u8 led_mode;
	bool is_increasing;
	int ret;

	if (len != 2)
		return -EINVAL;

	start_brightness = clamp_val(pattern[0].brightness, 0, LTC3220_LED_CURRENT_MASK);
	end_brightness = clamp_val(pattern[1].brightness, 0, LTC3220_LED_CURRENT_MASK);

	is_increasing = end_brightness > start_brightness;

	if (pattern[0].delta_t == 0)
		gradation_period = LTC3220_GRADATION_MODE_DISABLED;
	else if (pattern[0].delta_t <= LTC3220_GRADATION_RAMP_TIME_240MS)
		gradation_period = LTC3220_GRADATION_MODE_240MS_RAMP_TIME;
	else if (pattern[0].delta_t <= LTC3220_GRADATION_RAMP_TIME_480MS)
		gradation_period = LTC3220_GRADATION_MODE_480MS_RAMP_TIME;
	else
		gradation_period = LTC3220_GRADATION_MODE_960MS_RAMP_TIME;

	gradation_val = FIELD_PREP(LTC3220_GRADATION_PERIOD_MASK, gradation_period);
	gradation_val |= FIELD_PREP(LTC3220_GRADATION_DIRECTION_MASK, is_increasing);

	/*
	 * With the ramp disabled (delta_t == 0) there is no gradation to run,
	 * so apply the end brightness directly in NORMAL mode instead of
	 * leaving the channel in gradation mode with a disabled ramp.
	 */
	led_mode = gradation_period == LTC3220_GRADATION_MODE_DISABLED ?
		   LTC3220_NORMAL_MODE : LTC3220_GRADATION_MODE;

	mutex_lock(&ltc3220->lock);

	ret = regmap_update_bits(ltc3220->regmap, LTC3220_GRAD_BLINK_REG,
				 LTC3220_GRADATION_MASK, gradation_val);
	if (ret)
		goto unlock;

	if (led_mode == LTC3220_GRADATION_MODE) {
		ret = regmap_write(ltc3220->regmap, LTC3220_ULED_REG(uled_cfg->led_index),
				   start_brightness & LTC3220_LED_CURRENT_MASK);
		if (ret)
			goto unlock;

		ret = regmap_write(ltc3220->regmap, LTC3220_ULED_REG(uled_cfg->led_index),
				   FIELD_PREP(LTC3220_LED_MODE_MASK, led_mode) |
				   (end_brightness & LTC3220_LED_CURRENT_MASK));
		if (ret)
			goto unlock;

		uled_cfg->reg_value = end_brightness;
	} else {
		ret = __ltc3220_set_led_data(ltc3220, uled_cfg, end_brightness);
		if (ret)
			goto unlock;
	}

unlock:
	mutex_unlock(&ltc3220->lock);
	return ret;
}

static int ltc3220_pattern_clear(struct led_classdev *led_cdev)
{
	struct ltc3220_uled_cfg *uled_cfg = container_of(led_cdev, struct ltc3220_uled_cfg,
							 led_cdev);
	struct ltc3220 *ltc3220 = container_of(uled_cfg - uled_cfg->led_index, struct ltc3220,
					       uled_cfg[0]);
	int ret;

	mutex_lock(&ltc3220->lock);

	ret = regmap_update_bits(ltc3220->regmap, LTC3220_ULED_REG(uled_cfg->led_index),
				 LTC3220_LED_MODE_MASK, LTC3220_NORMAL_MODE);
	if (ret)
		goto unlock;

	ret = __ltc3220_set_led_data(ltc3220, uled_cfg, LED_OFF);

unlock:
	mutex_unlock(&ltc3220->lock);
	return ret;
}

/*
 * LTC3220 has a global blink configuration that affects all LEDs.
 * This implementation allows per-LED blink requests via sysfs, but setting
 * blink on any LED reprograms the timing for all 18 channels simultaneously.
 * The delay values are mapped to the hardware's discrete blink rates.
 *
 * HARDWARE LIMITATION: This is not a driver bug. Per-LED blink timing control
 * is not possible with this hardware due to the global blink register.
 */
static int ltc3220_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct ltc3220_uled_cfg *uled_cfg = container_of(led_cdev, struct ltc3220_uled_cfg,
							 led_cdev);
	struct ltc3220 *ltc3220 = container_of(uled_cfg - uled_cfg->led_index, struct ltc3220,
					       uled_cfg[0]);
	u8 blink_brightness;
	u8 blink_mode = 0;
	int ret;

	if (*delay_on <= LTC3220_BLINK_ON_156MS)
		blink_mode = LTC3220_BLINK_SHORT_ON_TIME;

	if (*delay_on + *delay_off > LTC3220_BLINK_PERIOD_1250MS)
		blink_mode |= LTC3220_BLINK_LONG_PERIOD;

	switch (blink_mode) {
	case LTC3220_BLINK_MODE_625MS_1250MS:
		*delay_on = LTC3220_BLINK_ON_625MS;
		*delay_off = LTC3220_BLINK_PERIOD_1250MS - LTC3220_BLINK_ON_625MS;
		break;
	case LTC3220_BLINK_MODE_156MS_1250MS:
		*delay_on = LTC3220_BLINK_ON_156MS;
		*delay_off = LTC3220_BLINK_PERIOD_1250MS - LTC3220_BLINK_ON_156MS;
		break;
	case LTC3220_BLINK_MODE_625MS_2500MS:
		*delay_on = LTC3220_BLINK_ON_625MS;
		*delay_off = LTC3220_BLINK_PERIOD_2500MS - LTC3220_BLINK_ON_625MS;
		break;
	case LTC3220_BLINK_MODE_156MS_2500MS:
		*delay_on = LTC3220_BLINK_ON_156MS;
		*delay_off = LTC3220_BLINK_PERIOD_2500MS - LTC3220_BLINK_ON_156MS;
		break;
	}

	mutex_lock(&ltc3220->lock);

	ret = regmap_update_bits(ltc3220->regmap, LTC3220_GRAD_BLINK_REG,
				 LTC3220_BLINK_MASK, FIELD_PREP(LTC3220_BLINK_MASK, blink_mode));
	if (ret)
		goto unlock;

	blink_brightness = uled_cfg->reg_value ? : led_cdev->max_brightness;

	ret = regmap_write(ltc3220->regmap, LTC3220_ULED_REG(uled_cfg->led_index),
			    FIELD_PREP(LTC3220_LED_MODE_MASK, LTC3220_BLINK_MODE) |
			    (blink_brightness & LTC3220_LED_CURRENT_MASK));
	if (ret)
		goto unlock;

	uled_cfg->reg_value = blink_brightness;

unlock:
	mutex_unlock(&ltc3220->lock);
	return ret;
}

static void ltc3220_reset_gpio_action(void *data)
{
	struct gpio_desc *reset_gpio = data;

	gpiod_set_value_cansleep(reset_gpio, 1);
}

static int ltc3220_reset(struct ltc3220 *ltc3220, struct i2c_client *client)
{
	struct gpio_desc *reset_gpio;
	int ret;

	reset_gpio = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(reset_gpio), "Failed on reset GPIO\n");

	if (reset_gpio) {
		usleep_range(10000, 12000);
		gpiod_set_value_cansleep(reset_gpio, 0);
		usleep_range(10000, 12000);

		ret = devm_add_action_or_reset(&client->dev, ltc3220_reset_gpio_action,
					       reset_gpio);
		if (ret)
			return ret;
	}

	ret = regmap_write(ltc3220->regmap, LTC3220_COMMAND_REG, 0);
	if (ret)
		return ret;

	for (int i = 0; i < LTC3220_NUM_LEDS; i++) {
		ret = regmap_write(ltc3220->regmap, LTC3220_ULED_REG(i), 0);
		if (ret)
			return ret;
	}

	return regmap_write(ltc3220->regmap, LTC3220_GRAD_BLINK_REG, 0);
}

static int ltc3220_suspend(struct device *dev)
{
	struct ltc3220 *ltc3220 = i2c_get_clientdata(to_i2c_client(dev));
	int ret;

	ret = regmap_update_bits(ltc3220->regmap, LTC3220_COMMAND_REG,
				 LTC3220_SHUTDOWN_MASK, LTC3220_SHUTDOWN_MASK);
	if (ret)
		return ret;

	regcache_mark_dirty(ltc3220->regmap);

	return 0;
}

static int ltc3220_resume(struct device *dev)
{
	struct ltc3220 *ltc3220 = i2c_get_clientdata(to_i2c_client(dev));
	bool quick_write_enabled;
	unsigned int command_reg;
	int ret;

	ret = regmap_read(ltc3220->regmap, LTC3220_COMMAND_REG, &command_reg);
	if (ret)
		return ret;

	quick_write_enabled = command_reg & LTC3220_QUICK_WRITE_MASK;

	if (quick_write_enabled) {
		ret = regmap_update_bits(ltc3220->regmap, LTC3220_COMMAND_REG,
					 LTC3220_QUICK_WRITE_MASK, 0);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(ltc3220->regmap, LTC3220_COMMAND_REG,
				 LTC3220_SHUTDOWN_MASK, 0);
	if (ret)
		return ret;

	usleep_range(10000, 12000);

	ret = regcache_sync(ltc3220->regmap);
	if (ret)
		return ret;

	if (quick_write_enabled) {
		ret = regmap_update_bits(ltc3220->regmap, LTC3220_COMMAND_REG,
					 LTC3220_QUICK_WRITE_MASK,
					 LTC3220_QUICK_WRITE_MASK);
		if (ret)
			return ret;
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ltc3220_pm_ops, ltc3220_suspend, ltc3220_resume);

static int ltc3220_probe(struct i2c_client *client)
{
	struct ltc3220 *ltc3220;
	bool aggregated_led_found = false;
	int num_leds = 0;
	u8 led_index = 0;
	int ret;

	ltc3220 = devm_kzalloc(&client->dev, sizeof(*ltc3220), GFP_KERNEL);
	if (!ltc3220)
		return -ENOMEM;

	ltc3220->regmap = devm_regmap_init_i2c(client, &ltc3220_regmap_config);
	if (IS_ERR(ltc3220->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(ltc3220->regmap),
				     "Failed to initialize regmap\n");

	ret = devm_mutex_init(&client->dev, &ltc3220->lock);
	if (ret)
		return ret;

	i2c_set_clientdata(client, ltc3220);

	ret = ltc3220_reset(ltc3220, client);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to reset device\n");

	/* First pass: validate configuration and set up LED structures */
	device_for_each_child_node_scoped(&client->dev, child) {
		struct ltc3220_uled_cfg *led;
		u32 source;

		ret = fwnode_property_read_u32(child, "reg", &source);
		if (ret)
			return dev_err_probe(&client->dev, ret, "Couldn't read LED address\n");

		if (!source || source > LTC3220_NUM_LEDS)
			return dev_err_probe(&client->dev, -EINVAL, "LED address out of range\n");

		if (fwnode_property_present(child, "led-sources")) {
			u32 led_sources[LTC3220_NUM_LEDS];
			int count;

			if (source != 1)
				return dev_err_probe(&client->dev, -EINVAL,
						     "Aggregated LED out of range\n");

			if (aggregated_led_found)
				return dev_err_probe(&client->dev, -EINVAL,
						     "One Aggregated LED only\n");

			count = fwnode_property_count_u32(child, "led-sources");
			if (count != LTC3220_NUM_LEDS)
				return dev_err_probe(&client->dev, -EINVAL,
						     "Aggregated mode requires all %d outputs in led-sources, got %d\n",
						     LTC3220_NUM_LEDS, count);

			ret = fwnode_property_read_u32_array(child, "led-sources",
							     led_sources, LTC3220_NUM_LEDS);
			if (ret)
				return dev_err_probe(&client->dev, ret,
						     "Failed to read led-sources array\n");

			/*
			 * Validate array contents for DT correctness. The hardware
			 * quick-write broadcasts to all 18 channels regardless of
			 * array contents, but checking helps catch DT mistakes.
			 */
			for (int i = 0; i < LTC3220_NUM_LEDS; i++) {
				if (led_sources[i] < 1 || led_sources[i] > LTC3220_NUM_LEDS)
					return dev_err_probe(&client->dev, -EINVAL,
							     "Invalid output %u in led-sources\n",
							     led_sources[i]);
			}

			aggregated_led_found = true;
		}

		num_leds++;

		/* LED node reg/index/address goes from 1 to 18 */
		led_index = source - 1;
		led = &ltc3220->uled_cfg[led_index];

		if (led->registered)
			return dev_err_probe(&client->dev, -EINVAL,
					     "Duplicate LED reg %u found\n", source);

		led->registered = true;
		led->led_index = led_index;
		led->reg_value = 0;
		led->led_cdev.brightness_set_blocking = ltc3220_set_led_data;
		led->led_cdev.brightness_get = ltc3220_get_led_data;
		led->led_cdev.max_brightness = LTC3220_MAX_BRIGHTNESS;
		led->led_cdev.blink_set = ltc3220_blink_set;
		led->led_cdev.pattern_set = ltc3220_pattern_set;
		led->led_cdev.pattern_clear = ltc3220_pattern_clear;
	}

	/*
	 * Aggregated LED mode uses hardware quick-write to control all 18 LEDs
	 * simultaneously. This is mutually exclusive with individual LED control.
	 * See Documentation/devicetree/bindings/leds/adi,ltc3220.yaml for details
	 * on how to configure aggregated LED mode.
	 */
	if (aggregated_led_found && num_leds > 1)
		return dev_err_probe(&client->dev, -EINVAL,
				     "Aggregated LED must be the only LED node\n");

	if (num_leds == 0)
		return dev_err_probe(&client->dev, -EINVAL,
				     "No LED nodes found in device tree\n");

	if (aggregated_led_found) {
		ret = regmap_update_bits(ltc3220->regmap,
						LTC3220_COMMAND_REG,
						LTC3220_QUICK_WRITE_MASK,
						LTC3220_QUICK_WRITE_MASK);
		if (ret)
			return dev_err_probe(&client->dev, ret,
						"Failed to set quick write mode\n");
	}

	/* Second pass: register LEDs after validation */
	device_for_each_child_node_scoped(&client->dev, child) {
		struct led_init_data init_data = {};
		struct ltc3220_uled_cfg *led;
		u32 source;

		ret = fwnode_property_read_u32(child, "reg", &source);
		if (ret)
			return ret;

		if (!source || source > LTC3220_NUM_LEDS)
			return dev_err_probe(&client->dev, -EINVAL,
					     "LED address out of range in second pass\n");

		init_data.fwnode = child;
		init_data.devicename = "ltc3220";

		led_index = source - 1;
		led = &ltc3220->uled_cfg[led_index];

		ret = devm_led_classdev_register_ext(&client->dev, &led->led_cdev, &init_data);
		if (ret)
			return dev_err_probe(&client->dev, ret, "Failed to register LED class\n");
	}

	return 0;
}

static const struct of_device_id ltc3220_of_match[] = {
	{ .compatible = "adi,ltc3220" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc3220_of_match);

static struct i2c_driver ltc3220_led_driver = {
	.driver = {
		.name = "ltc3220",
		.of_match_table = ltc3220_of_match,
		.pm = pm_sleep_ptr(&ltc3220_pm_ops),
	},
	.probe = ltc3220_probe,
};
module_i2c_driver(ltc3220_led_driver);

MODULE_AUTHOR("Edelweise Escala <edelweise.escala@analog.com>");
MODULE_DESCRIPTION("LED driver for LTC3220 controllers");
MODULE_LICENSE("GPL");
