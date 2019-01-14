/*
 * Copyright 2016 CZ.NIC, z.s.p.o.
 *
 * Author: Tomas Hlavacek <tmshlvck@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. 
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>

#define MAX_LEDS 13
#define ALL_LEDS_INDEX 12

#define LED_AUTONOMOUS_ADDR 3
#define LED_ONOFF_ADDR 4
#define LED_COLOR_ADDR 5
#define GLOB_BRIGHTNESS_READ 8
#define GLOB_BRIGHTNESS_WRITE 7



struct omnia_platform_data {
        struct led_platform_data leds;
};

static const struct i2c_device_id omnia_id[] = {
	{ "omnia", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, omnia_id);

struct omnia_led_mcu {
	struct mutex mutex;
	struct i2c_client *client;
	struct omnia_led *leds;
};

struct omnia_led {
	struct omnia_led_mcu *chip;
	struct led_classdev led_cdev;
	int led_num; /* 0 .. 11 + 12=ALL */
	char name[32];
	u8 autonomous;
	u8 r;
	u8 g;
	u8 b;
};

static int omnia_led_brightness_set(struct omnia_led *led,
				enum led_brightness brightness)
{
	int ret;

	mutex_lock(&led->chip->mutex);

	ret = i2c_smbus_write_byte_data(led->chip->client, LED_ONOFF_ADDR,
			(led->led_num | ((brightness != LED_OFF)<<4)));

	mutex_unlock(&led->chip->mutex);
	return ret;
}

static int omnia_led_autonomous_set(struct omnia_led *led, int autonomous)
{
	int ret, i;

	mutex_lock(&led->chip->mutex);

	if (led->autonomous == (autonomous != 0)) {
		mutex_unlock(&led->chip->mutex);
		return 0;
	}

	led->autonomous = (autonomous != 0);

	if (led->led_num == ALL_LEDS_INDEX) {
		for (i=0; i<(MAX_LEDS-1); i++)
			led->chip->leds[i].autonomous = led->autonomous;
	}

	ret = i2c_smbus_write_byte_data(led->chip->client, LED_AUTONOMOUS_ADDR,
			(u8)(led->led_num | ((!led->autonomous) << 4)));

	mutex_unlock(&led->chip->mutex);
	return ret;
}

static int omnia_glob_brightness_set(struct omnia_led_mcu *chip,
					int glob_brightness)
{
	int ret;

	mutex_lock(&chip->mutex);

	ret = i2c_smbus_write_byte_data(chip->client, GLOB_BRIGHTNESS_WRITE,
						(u8)glob_brightness);

	mutex_unlock(&chip->mutex);
	return ret;
}

static int omnia_glob_brightness_get(struct omnia_led_mcu *chip)
{
	int ret;

	mutex_lock(&chip->mutex);

	ret = i2c_smbus_read_byte_data(chip->client, GLOB_BRIGHTNESS_READ);

	mutex_unlock(&chip->mutex);
	return ret;
}

static int omnia_led_color_set(struct omnia_led *led, u8 r, u8 g, u8 b)
{
	int ret, i;
	u8 buf[5];

	buf[0] = LED_COLOR_ADDR;
	buf[1] = led->led_num;
	buf[2] = r;
	buf[3] = g;
	buf[4] = b;

	mutex_lock(&led->chip->mutex);

	ret = i2c_master_send(led->chip->client, buf, 5);

	if (led->led_num == ALL_LEDS_INDEX) {
		for (i=0; i<(MAX_LEDS-1); i++) {
			led->chip->leds[i].r = led->r;
			led->chip->leds[i].g = led->g;
			led->chip->leds[i].b = led->b;
		}
	}

	mutex_unlock(&led->chip->mutex);
	return -(ret<=0);
}

static int omnia_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct omnia_led *led;

	led = container_of(led_cdev, struct omnia_led, led_cdev);

	return omnia_led_brightness_set(led, value);
}

static struct omnia_platform_data *
omnia_dt_init(struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node, *child;
	struct omnia_platform_data *pdata;
	struct led_info *leds;
	int count;

	count = of_get_child_count(np);
	if (!count || count > MAX_LEDS)
		return ERR_PTR(-ENODEV);

	leds = devm_kzalloc(&client->dev,
			sizeof(struct led_info) * MAX_LEDS, GFP_KERNEL);
	if (!leds)
		return ERR_PTR(-ENOMEM);

	for_each_child_of_node(np, child) {
		u32 reg;
		int res;

		res = of_property_read_u32(child, "reg", &reg);
		if ((res != 0) || (reg >= MAX_LEDS))
			continue;
		leds[reg].name =
			of_get_property(child, "label", NULL) ? : child->name;
		leds[reg].default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);
	}
	pdata = devm_kzalloc(&client->dev,
			     sizeof(struct omnia_platform_data), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	pdata->leds.leds = leds;
	pdata->leds.num_leds = MAX_LEDS;

	return pdata;
}

static ssize_t global_brightness_show(struct device *d,
                struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(d);
	struct omnia_led_mcu *chip = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
				omnia_glob_brightness_get(chip));
}

static ssize_t global_brightness_store(struct device *d,
                struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(d);
        struct omnia_led_mcu *chip = i2c_get_clientdata(client);
	int ret;
	int global_brightness;

	if ((sscanf(buf, "%i", &global_brightness)) != 1)
		return -EINVAL;

	ret = omnia_glob_brightness_set(chip, global_brightness);
	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(global_brightness);

static ssize_t autonomous_show(struct device *d,
                struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(d);
	struct omnia_led *led =
			container_of(led_cdev, struct omnia_led, led_cdev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", led->autonomous);
}

static ssize_t autonomous_store(struct device *d,
                struct device_attribute *attr, const char *buf, size_t count)
{
	int ret, autonomous;
	struct led_classdev *led_cdev = dev_get_drvdata(d);
	struct omnia_led *led =
			container_of(led_cdev, struct omnia_led, led_cdev);

	if ((sscanf(buf, "%i", &autonomous)) != 1)
		return -EINVAL;

	ret = omnia_led_autonomous_set(led, autonomous);
	if (ret < 0)
		return ret;

	led->autonomous = autonomous;
	return count;
}
static DEVICE_ATTR_RW(autonomous);

static ssize_t color_show(struct device *d,
                struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(d);
	struct omnia_led *led =
			container_of(led_cdev, struct omnia_led, led_cdev);

	return scnprintf(buf, PAGE_SIZE, "%d %d %d\n", led->r, led->g, led->b);
}

static ssize_t color_store(struct device *d,
                struct device_attribute *attr, const char *buf, size_t count)
{
	int ret, r, g, b;
	struct led_classdev *led_cdev = dev_get_drvdata(d);
	struct omnia_led *led =
			container_of(led_cdev, struct omnia_led, led_cdev);

	if ((sscanf(buf, "%i %i %i", &r, &g, &b)) != 3)
		return -EINVAL;

	ret = omnia_led_color_set(led, r, g, b);
	if (ret < 0)
		return ret;

	led->r = r;
	led->g = g;
	led->b = b;
	return count;
}
static DEVICE_ATTR_RW(color);


static const struct of_device_id of_omnia_match[] = {
	{ .compatible = "turris-leds,omnia", },
	{},
};
MODULE_DEVICE_TABLE(of, of_omnia_match);

static int omnia_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct omnia_led_mcu *chip;
	struct omnia_led *leds;
	struct omnia_platform_data *pdata;
	int i, err;

	pdata = dev_get_platdata(&client->dev);

	if (!pdata) {
		pdata = omnia_dt_init(client);
		if (IS_ERR(pdata)) {
			dev_warn(&client->dev, "could not parse configuration\n");
			pdata = NULL;
		}
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip),
				GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	leds = devm_kzalloc(&client->dev, MAX_LEDS * sizeof(*leds),
				GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);

	mutex_init(&chip->mutex);
	chip->client = client;
	chip->leds = leds;

	for (i = 0; i < MAX_LEDS; i++) {
		leds[i].led_num = i;
		leds[i].chip = chip;

		/* Platform data can specify LED names and default triggers */
		if (pdata && i < pdata->leds.num_leds) {
			if (pdata->leds.leds[i].name)
				snprintf(leds[i].name,
					 sizeof(leds[i].name), "omnia-led:%s",
					 pdata->leds.leds[i].name);
			if (pdata->leds.leds[i].default_trigger)
				leds[i].led_cdev.default_trigger =
					pdata->leds.leds[i].default_trigger;
		}
		if (!pdata || i >= pdata->leds.num_leds ||
						!pdata->leds.leds[i].name)
			snprintf(leds[i].name, sizeof(leds[i].name),
				 "omnia-led:%d", i);

		leds[i].led_cdev.name = leds[i].name;
		leds[i].led_cdev.brightness_set_blocking = omnia_led_set;

		err = led_classdev_register(&client->dev, &leds[i].led_cdev);
		if (err < 0)
			goto exit;

		err = device_create_file(leds[i].led_cdev.dev,
						&dev_attr_autonomous);
		if (err < 0) {
			dev_err(leds[i].led_cdev.dev,
				"failed to create attribute autonomous\n");
			goto exit;
		}

		err = device_create_file(leds[i].led_cdev.dev,
						&dev_attr_color);
		if (err < 0) {
			dev_err(leds[i].led_cdev.dev,
				"failed to create attribute color\n");
			goto exit;
		}

		/* Set AUTO for all LEDs by default */
		leds[i].autonomous = 0;
		omnia_led_autonomous_set(&leds[i], 1);

		/* Set brightness to LED_OFF by default */
		omnia_led_brightness_set(&leds[i], LED_OFF);

		/* MCU default color is white */
		leds[i].r = 255;
		leds[i].g = 255;
		leds[i].b = 255;
	}

	err = device_create_file(&client->dev, &dev_attr_global_brightness);
	if (err < 0) {
		dev_err(&client->dev,
			"failed to create attribute global_brightness\n");
		goto exit;
	}

	return 0;

exit:
	device_remove_file(&client->dev, &dev_attr_global_brightness);
	while (i--) {
		device_remove_file(chip->leds[i].led_cdev.dev,
			&dev_attr_color);
		device_remove_file(chip->leds[i].led_cdev.dev,
			&dev_attr_autonomous);

		led_classdev_unregister(&leds[i].led_cdev);
	}

	return err;
}

static int omnia_remove(struct i2c_client *client)
{
	struct omnia_led_mcu *chip = i2c_get_clientdata(client);
	int i;

	device_remove_file(&client->dev, &dev_attr_global_brightness);

	for (i = 0; i < MAX_LEDS; i++) {
		device_remove_file(chip->leds[i].led_cdev.dev,
			&dev_attr_color);
		device_remove_file(chip->leds[i].led_cdev.dev,
			&dev_attr_autonomous);

		led_classdev_unregister(&chip->leds[i].led_cdev);
	}

	return 0;
}

static struct i2c_driver omnia_driver = {
	.driver = {
		.name	= "leds-omnia",
		.of_match_table = of_match_ptr(of_omnia_match),
	},
	.probe	= omnia_probe,
	.remove	= omnia_remove,
	.id_table = omnia_id,
};

module_i2c_driver(omnia_driver);

MODULE_AUTHOR("Tomas Hlavacek <tmshlvck@gmail.com>");
MODULE_DESCRIPTION("Turris Omnia LED driver");
MODULE_LICENSE("GPL v2");


