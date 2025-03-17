// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED driver for STMicroelectronics LED1202 chip
 *
 * Copyright (C) 2024 Remote-Tech Ltd. UK
 */

#include <linux/cleanup.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>

#define ST1202_CHAN_DISABLE_ALL            0x00
#define ST1202_CHAN_ENABLE_HIGH            0x03
#define ST1202_CHAN_ENABLE_LOW             0x02
#define ST1202_CONFIG_REG                  0x04
/* PATS: Pattern sequence feature enable */
#define ST1202_CONFIG_REG_PATS             BIT(7)
/* PATSR: Pattern sequence runs (self-clear when sequence is finished) */
#define ST1202_CONFIG_REG_PATSR            BIT(6)
#define ST1202_CONFIG_REG_SHFT             BIT(3)
#define ST1202_DEV_ENABLE                  0x01
#define ST1202_DEV_ENABLE_ON               BIT(0)
#define ST1202_DEV_ENABLE_RESET            BIT(7)
#define ST1202_DEVICE_ID                   0x00
#define ST1202_ILED_REG0                   0x09
#define ST1202_MAX_LEDS                    12
#define ST1202_MAX_PATTERNS                8
#define ST1202_MILLIS_PATTERN_DUR_MAX      5660
#define ST1202_MILLIS_PATTERN_DUR_MIN      22
#define ST1202_PATTERN_DUR                 0x16
#define ST1202_PATTERN_PWM                 0x1E
#define ST1202_PATTERN_REP                 0x15

struct st1202_led {
	struct fwnode_handle *fwnode;
	struct led_classdev led_cdev;
	struct st1202_chip *chip;
	bool is_active;
	int led_num;
};

struct st1202_chip {
	struct i2c_client *client;
	struct mutex lock;
	struct st1202_led leds[ST1202_MAX_LEDS];
};

static struct st1202_led *cdev_to_st1202_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct st1202_led, led_cdev);
}

static int st1202_read_reg(struct st1202_chip *chip, int reg, uint8_t *val)
{
	struct device *dev = &chip->client->dev;
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		dev_err(dev, "Failed to read register [0x%x]: %d\n", reg, ret);
		return ret;
	}

	*val = (uint8_t)ret;
	return 0;
}

static int st1202_write_reg(struct st1202_chip *chip, int reg, uint8_t val)
{
	struct device *dev = &chip->client->dev;
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret != 0)
		dev_err(dev, "Failed to write %d to register [0x%x]: %d\n", val, reg, ret);

	return ret;
}

static uint8_t st1202_prescalar_to_miliseconds(unsigned int value)
{
	return value / ST1202_MILLIS_PATTERN_DUR_MIN - 1;
}

static int st1202_pwm_pattern_write(struct st1202_chip *chip, int led_num,
				int pattern, unsigned int value)
{
	u8 value_l, value_h;
	int ret;

	value_l = (u8)value;
	value_h = (u8)(value >> 8);

	/*
	 *  Datasheet: Register address low = 1Eh + 2*(xh) + 18h*(yh),
	 *  where x is the channel number (led number) in hexadecimal (x = 00h .. 0Bh)
	 *  and y is the pattern number in hexadecimal (y = 00h .. 07h)
	 */
	ret = st1202_write_reg(chip, (ST1202_PATTERN_PWM + (led_num * 2) + 0x18 * pattern),
				value_l);
	if (ret != 0)
		return ret;

	/*
	 * Datasheet: Register address high = 1Eh + 01h + 2(xh) +18h*(yh),
	 * where x is the channel number in hexadecimal (x = 00h .. 0Bh)
	 * and y is the pattern number in hexadecimal (y = 00h .. 07h)
	 */
	ret = st1202_write_reg(chip, (ST1202_PATTERN_PWM + 0x1 + (led_num * 2) + 0x18 * pattern),
				value_h);
	if (ret != 0)
		return ret;

	return 0;
}

static int st1202_duration_pattern_write(struct st1202_chip *chip, int pattern,
					unsigned int value)
{
	return st1202_write_reg(chip, (ST1202_PATTERN_DUR + pattern),
				st1202_prescalar_to_miliseconds(value));
}

static void st1202_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	struct st1202_led *led = cdev_to_st1202_led(led_cdev);
	struct st1202_chip *chip = led->chip;

	guard(mutex)(&chip->lock);

	st1202_write_reg(chip, ST1202_ILED_REG0 + led->led_num, value);
}

static enum led_brightness st1202_brightness_get(struct led_classdev *led_cdev)
{
	struct st1202_led *led = cdev_to_st1202_led(led_cdev);
	struct st1202_chip *chip = led->chip;
	u8 value = 0;

	guard(mutex)(&chip->lock);

	st1202_read_reg(chip, ST1202_ILED_REG0 + led->led_num, &value);

	return value;
}

static int st1202_channel_set(struct st1202_chip *chip, int led_num, bool active)
{
	u8 chan_low, chan_high;
	int ret;

	guard(mutex)(&chip->lock);

	if (led_num <= 7) {
		ret = st1202_read_reg(chip, ST1202_CHAN_ENABLE_LOW, &chan_low);
		if (ret < 0)
			return ret;

		chan_low = active ? chan_low | BIT(led_num) : chan_low & ~BIT(led_num);

		ret = st1202_write_reg(chip, ST1202_CHAN_ENABLE_LOW, chan_low);
		if (ret < 0)
			return ret;

	} else {
		ret = st1202_read_reg(chip, ST1202_CHAN_ENABLE_HIGH, &chan_high);
		if (ret < 0)
			return ret;

		chan_high = active ? chan_high | (BIT(led_num) >> 8) :
					chan_high & ~(BIT(led_num) >> 8);

		ret = st1202_write_reg(chip, ST1202_CHAN_ENABLE_HIGH, chan_high);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int st1202_led_set(struct led_classdev *ldev, enum led_brightness value)
{
	struct st1202_led *led = cdev_to_st1202_led(ldev);
	struct st1202_chip *chip = led->chip;

	return st1202_channel_set(chip, led->led_num, value == LED_OFF ? false : true);
}

static int st1202_led_pattern_clear(struct led_classdev *ldev)
{
	struct st1202_led *led = cdev_to_st1202_led(ldev);
	struct st1202_chip *chip = led->chip;
	int ret;

	guard(mutex)(&chip->lock);

	for (int patt = 0; patt < ST1202_MAX_PATTERNS; patt++) {
		ret = st1202_pwm_pattern_write(chip, led->led_num, patt, LED_OFF);
		if (ret != 0)
			return ret;

		ret = st1202_duration_pattern_write(chip, patt, ST1202_MILLIS_PATTERN_DUR_MIN);
		if (ret != 0)
			return ret;
	}

	return 0;
}

static int st1202_led_pattern_set(struct led_classdev *ldev,
				struct led_pattern *pattern,
				u32 len, int repeat)
{
	struct st1202_led *led = cdev_to_st1202_led(ldev);
	struct st1202_chip *chip = led->chip;
	int ret;

	if (len > ST1202_MAX_PATTERNS)
		return -EINVAL;

	guard(mutex)(&chip->lock);

	for (int patt = 0; patt < len; patt++) {
		if (pattern[patt].delta_t < ST1202_MILLIS_PATTERN_DUR_MIN ||
				pattern[patt].delta_t > ST1202_MILLIS_PATTERN_DUR_MAX)
			return -EINVAL;

		ret = st1202_pwm_pattern_write(chip, led->led_num, patt, pattern[patt].brightness);
		if (ret != 0)
			return ret;

		ret = st1202_duration_pattern_write(chip, patt, pattern[patt].delta_t);
		if (ret != 0)
			return ret;
	}

	ret = st1202_write_reg(chip, ST1202_PATTERN_REP, repeat);
	if (ret != 0)
		return ret;

	ret = st1202_write_reg(chip, ST1202_CONFIG_REG, (ST1202_CONFIG_REG_PATSR |
							ST1202_CONFIG_REG_PATS | ST1202_CONFIG_REG_SHFT));
	if (ret != 0)
		return ret;

	return 0;
}

static int st1202_dt_init(struct st1202_chip *chip)
{
	struct device *dev = &chip->client->dev;
	struct st1202_led *led;
	int err, reg;

	for_each_available_child_of_node_scoped(dev_of_node(dev), child) {
		err = of_property_read_u32(child, "reg", &reg);
		if (err)
			return dev_err_probe(dev, err, "Invalid register\n");

		led = &chip->leds[reg];
		led->is_active = true;
		led->fwnode = of_fwnode_handle(child);

		led->led_cdev.max_brightness = U8_MAX;
		led->led_cdev.brightness_set_blocking = st1202_led_set;
		led->led_cdev.pattern_set = st1202_led_pattern_set;
		led->led_cdev.pattern_clear = st1202_led_pattern_clear;
		led->led_cdev.default_trigger = "pattern";
		led->led_cdev.brightness_set = st1202_brightness_set;
		led->led_cdev.brightness_get = st1202_brightness_get;
	}

	return 0;
}

static int st1202_setup(struct st1202_chip *chip)
{
	int ret;

	guard(mutex)(&chip->lock);

	/*
	 * Once the supply voltage is applied, the LED1202 executes some internal checks,
	 * afterwords it stops the oscillator and puts the internal LDO in quiescent mode.
	 * To start the device, EN bit must be set inside the “Device Enable” register at
	 * address 01h. As soon as EN is set, the LED1202 loads the adjustment parameters
	 * from the internal non-volatile memory and performs an auto-calibration procedure
	 * in order to increase the output current precision.
	 * Such initialization lasts about 6.5 ms.
	 */

	/* Reset the chip during setup */
	ret = st1202_write_reg(chip, ST1202_DEV_ENABLE, ST1202_DEV_ENABLE_RESET);
	if (ret < 0)
		return ret;

	/* Enable phase-shift delay feature */
	ret = st1202_write_reg(chip, ST1202_CONFIG_REG, ST1202_CONFIG_REG_SHFT);
	if (ret < 0)
		return ret;

	/* Enable the device */
	ret = st1202_write_reg(chip, ST1202_DEV_ENABLE, ST1202_DEV_ENABLE_ON);
	if (ret < 0)
		return ret;

	/* Duration of initialization */
	usleep_range(6500, 10000);

	/* Deactivate all LEDS (channels) and activate only the ones found in Device Tree */
	ret = st1202_write_reg(chip, ST1202_CHAN_ENABLE_LOW, ST1202_CHAN_DISABLE_ALL);
	if (ret < 0)
		return ret;

	ret = st1202_write_reg(chip, ST1202_CHAN_ENABLE_HIGH, ST1202_CHAN_DISABLE_ALL);
	if (ret < 0)
		return ret;

	ret = st1202_write_reg(chip, ST1202_CONFIG_REG,
				ST1202_CONFIG_REG_PATS | ST1202_CONFIG_REG_PATSR);
	if (ret < 0)
		return ret;

	return 0;
}

static int st1202_probe(struct i2c_client *client)
{
	struct st1202_chip *chip;
	struct st1202_led *led;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return dev_err_probe(&client->dev, -EIO, "SMBUS Byte Data not Supported\n");

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	devm_mutex_init(&client->dev, &chip->lock);
	chip->client = client;

	ret = st1202_dt_init(chip);
	if (ret < 0)
		return ret;

	ret = st1202_setup(chip);
	if (ret < 0)
		return ret;

	for (int i = 0; i < ST1202_MAX_LEDS; i++) {
		struct led_init_data init_data = {};
		led = &chip->leds[i];
		led->chip = chip;
		led->led_num = i;

		if (!led->is_active)
			continue;

		ret = st1202_channel_set(led->chip, led->led_num, true);
		if (ret < 0)
			return dev_err_probe(&client->dev, ret,
					"Failed to activate LED channel\n");

		ret = st1202_led_pattern_clear(&led->led_cdev);
		if (ret < 0)
			return dev_err_probe(&client->dev, ret,
					"Failed to clear LED pattern\n");

		init_data.fwnode = led->fwnode;
		init_data.devicename = "st1202";
		init_data.default_label = ":";

		ret = devm_led_classdev_register_ext(&client->dev, &led->led_cdev, &init_data);
		if (ret < 0)
			return dev_err_probe(&client->dev, ret,
					"Failed to register LED class device\n");
	}

	return 0;
}

static const struct i2c_device_id st1202_id[] = {
	{ "st1202-i2c" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, st1202_id);

static const struct of_device_id st1202_dt_ids[] = {
	{ .compatible = "st,led1202" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st1202_dt_ids);

static struct i2c_driver st1202_driver = {
	.driver = {
		.name = "leds-st1202",
		.of_match_table = of_match_ptr(st1202_dt_ids),
	},
	.probe = st1202_probe,
	.id_table = st1202_id,
};
module_i2c_driver(st1202_driver);

MODULE_AUTHOR("Remote Tech LTD");
MODULE_DESCRIPTION("STMicroelectronics LED1202 : 12-channel constant current LED driver");
MODULE_LICENSE("GPL");
