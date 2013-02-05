/*
 * LP5521/LP5523/LP55231 Common Driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from leds-lp5521.c, leds-lp5523.c
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/leds-lp55xx.h>

#include "leds-lp55xx-common.h"

static struct lp55xx_led *cdev_to_lp55xx_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct lp55xx_led, cdev);
}

static void lp55xx_reset_device(struct lp55xx_chip *chip)
{
	struct lp55xx_device_config *cfg = chip->cfg;
	u8 addr = cfg->reset.addr;
	u8 val  = cfg->reset.val;

	/* no error checking here because no ACK from the device after reset */
	lp55xx_write(chip, addr, val);
}

static int lp55xx_detect_device(struct lp55xx_chip *chip)
{
	struct lp55xx_device_config *cfg = chip->cfg;
	u8 addr = cfg->enable.addr;
	u8 val  = cfg->enable.val;
	int ret;

	ret = lp55xx_write(chip, addr, val);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = lp55xx_read(chip, addr, &val);
	if (ret)
		return ret;

	if (val != cfg->enable.val)
		return -ENODEV;

	return 0;
}

static int lp55xx_post_init_device(struct lp55xx_chip *chip)
{
	struct lp55xx_device_config *cfg = chip->cfg;

	if (!cfg->post_init_device)
		return 0;

	return cfg->post_init_device(chip);
}

static struct attribute *lp55xx_led_attributes[] = {
	NULL,
};

static struct attribute_group lp55xx_led_attr_group = {
	.attrs = lp55xx_led_attributes
};

static void lp55xx_set_brightness(struct led_classdev *cdev,
			     enum led_brightness brightness)
{
	struct lp55xx_led *led = cdev_to_lp55xx_led(cdev);

	led->brightness = (u8)brightness;
	schedule_work(&led->brightness_work);
}

static int lp55xx_init_led(struct lp55xx_led *led,
			struct lp55xx_chip *chip, int chan)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	struct lp55xx_device_config *cfg = chip->cfg;
	struct device *dev = &chip->cl->dev;
	char name[32];
	int ret;
	int max_channel = cfg->max_channel;

	if (chan >= max_channel) {
		dev_err(dev, "invalid channel: %d / %d\n", chan, max_channel);
		return -EINVAL;
	}

	if (pdata->led_config[chan].led_current == 0)
		return 0;

	led->led_current = pdata->led_config[chan].led_current;
	led->max_current = pdata->led_config[chan].max_current;
	led->chan_nr = pdata->led_config[chan].chan_nr;

	if (led->chan_nr >= max_channel) {
		dev_err(dev, "Use channel numbers between 0 and %d\n",
			max_channel - 1);
		return -EINVAL;
	}

	led->cdev.brightness_set = lp55xx_set_brightness;

	if (pdata->led_config[chan].name) {
		led->cdev.name = pdata->led_config[chan].name;
	} else {
		snprintf(name, sizeof(name), "%s:channel%d",
			pdata->label ? : chip->cl->name, chan);
		led->cdev.name = name;
	}

	/*
	 * register led class device for each channel and
	 * add device attributes
	 */

	ret = led_classdev_register(dev, &led->cdev);
	if (ret) {
		dev_err(dev, "led register err: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&led->cdev.dev->kobj, &lp55xx_led_attr_group);
	if (ret) {
		dev_err(dev, "led sysfs err: %d\n", ret);
		led_classdev_unregister(&led->cdev);
		return ret;
	}

	return 0;
}

int lp55xx_write(struct lp55xx_chip *chip, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(chip->cl, reg, val);
}
EXPORT_SYMBOL_GPL(lp55xx_write);

int lp55xx_read(struct lp55xx_chip *chip, u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(chip->cl, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(lp55xx_read);

int lp55xx_update_bits(struct lp55xx_chip *chip, u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	ret = lp55xx_read(chip, reg, &tmp);
	if (ret)
		return ret;

	tmp &= ~mask;
	tmp |= val & mask;

	return lp55xx_write(chip, reg, tmp);
}
EXPORT_SYMBOL_GPL(lp55xx_update_bits);

int lp55xx_init_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata;
	struct lp55xx_device_config *cfg;
	struct device *dev = &chip->cl->dev;
	int ret = 0;

	WARN_ON(!chip);

	pdata = chip->pdata;
	cfg = chip->cfg;

	if (!pdata || !cfg)
		return -EINVAL;

	if (pdata->setup_resources) {
		ret = pdata->setup_resources();
		if (ret < 0) {
			dev_err(dev, "setup resoure err: %d\n", ret);
			goto err;
		}
	}

	if (pdata->enable) {
		pdata->enable(0);
		usleep_range(1000, 2000); /* Keep enable down at least 1ms */
		pdata->enable(1);
		usleep_range(1000, 2000); /* 500us abs min. */
	}

	lp55xx_reset_device(chip);

	/*
	 * Exact value is not available. 10 - 20ms
	 * appears to be enough for reset.
	 */
	usleep_range(10000, 20000);

	ret = lp55xx_detect_device(chip);
	if (ret) {
		dev_err(dev, "device detection err: %d\n", ret);
		goto err;
	}

	/* chip specific initialization */
	ret = lp55xx_post_init_device(chip);
	if (ret) {
		dev_err(dev, "post init device err: %d\n", ret);
		goto err_post_init;
	}

	return 0;

err_post_init:
	lp55xx_deinit_device(chip);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_init_device);

void lp55xx_deinit_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata = chip->pdata;

	if (pdata->enable)
		pdata->enable(0);

	if (pdata->release_resources)
		pdata->release_resources();
}
EXPORT_SYMBOL_GPL(lp55xx_deinit_device);

int lp55xx_register_leds(struct lp55xx_led *led, struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	struct lp55xx_device_config *cfg = chip->cfg;
	int num_channels = pdata->num_channels;
	struct lp55xx_led *each;
	u8 led_current;
	int ret;
	int i;

	if (!cfg->brightness_work_fn) {
		dev_err(&chip->cl->dev, "empty brightness configuration\n");
		return -EINVAL;
	}

	for (i = 0; i < num_channels; i++) {

		/* do not initialize channels that are not connected */
		if (pdata->led_config[i].led_current == 0)
			continue;

		led_current = pdata->led_config[i].led_current;
		each = led + i;
		ret = lp55xx_init_led(each, chip, i);
		if (ret)
			goto err_init_led;

		INIT_WORK(&each->brightness_work, cfg->brightness_work_fn);

		chip->num_leds++;
		each->chip = chip;

		/* setting led current at each channel */
		if (cfg->set_led_current)
			cfg->set_led_current(each, led_current);
	}

	return 0;

err_init_led:
	for (i = 0; i < chip->num_leds; i++) {
		each = led + i;
		led_classdev_unregister(&each->cdev);
		flush_work(&each->brightness_work);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_register_leds);

MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP55xx Common Driver");
MODULE_LICENSE("GPL");
