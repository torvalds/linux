// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include "leds-aw2016.h"

#define AW2016_DRIVER_VERSION "V1.0.3"

/* register address */
#define AW2016_REG_RESET 0x00
#define AW2016_REG_GCR1 0x01
#define AW2016_REG_STATUS 0x02
#define AW2016_REG_PATST 0x03
#define AW2016_REG_GCR2 0x04
#define AW2016_REG_LEDEN 0x30
#define AW2016_REG_LCFG1 0x31
#define AW2016_REG_LCFG2 0x32
#define AW2016_REG_LCFG3 0x33
#define AW2016_REG_PWM1 0x34
#define AW2016_REG_PWM2 0x35
#define AW2016_REG_PWM3 0x36
#define AW2016_REG_LED1T0 0x37
#define AW2016_REG_LED1T1 0x38
#define AW2016_REG_LED1T2 0x39
#define AW2016_REG_LED2T0 0x3A
#define AW2016_REG_LED2T1 0x3B
#define AW2016_REG_LED2T2 0x3C
#define AW2016_REG_LED3T0 0x3D
#define AW2016_REG_LED3T1 0x3E
#define AW2016_REG_LED3T2 0x3F

/* register bits */
#define AW2016_CHIPID 0x09
#define AW2016_CHIP_RESET_MASK 0x55
#define AW2016_CHIP_DISABLE_MASK 0x00
#define AW2016_CHIP_ENABLE_MASK 0x01
#define AW2016_CHARGE_DISABLE_MASK 0x02
#define AW2016_LED_BREATH_MODE_MASK 0x10
#define AW2016_LED_MANUAL_MODE_MASK 0x00
#define AW2016_LED_BREATHE_PWM_MASK 0xFF
#define AW2016_LED_MANUAL_PWM_MASK 0xFF
#define AW2016_LED_FADEIN_MODE_MASK 0x20
#define AW2016_LED_FADEOUT_MODE_MASK 0x40
#define AW2016_CHIP_STANDBY 0x02

#define MAX_RISE_TIME_MS 15
#define MAX_HOLD_TIME_MS 15
#define MAX_FALL_TIME_MS 15
#define MAX_OFF_TIME_MS 15

/* aw2016 register read/write access*/
#define REG_NONE_ACCESS 0
#define REG_RD_ACCESS (1 << 0)
#define REG_WR_ACCESS (1 << 1)
#define AW2016_REG_MAX 0x7F

const unsigned char aw2016_reg_access[AW2016_REG_MAX] = {
	[AW2016_REG_RESET] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_GCR1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_STATUS] = REG_RD_ACCESS,
	[AW2016_REG_PATST] = REG_RD_ACCESS,
	[AW2016_REG_GCR2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LEDEN] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LCFG1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LCFG2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LCFG3] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_PWM1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_PWM2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_PWM3] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED1T0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED1T1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED1T2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED2T0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED2T1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED2T2] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED3T0] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED3T1] = REG_RD_ACCESS | REG_WR_ACCESS,
	[AW2016_REG_LED3T2] = REG_RD_ACCESS | REG_WR_ACCESS,
};

struct aw2016_led {
	struct i2c_client *client;
	struct led_classdev cdev;
	struct aw2016_platform_data *pdata;
	struct work_struct brightness_work;
	struct work_struct blink_work;
	struct mutex lock;
	int num_leds;
	int id;
	int blinking;
};

static int aw2016_write(struct aw2016_led *led, u8 reg, u8 val)
{
	int ret = -EINVAL, retry_times = 0;

	do {
		ret = i2c_smbus_write_byte_data(led->client, reg, val);
		retry_times++;
		if (retry_times == 5)
			break;
	} while (ret < 0);

	return ret;
}

static int aw2016_read(struct aw2016_led *led, u8 reg, u8 *val)
{
	int ret = -EINVAL, retry_times = 0;

	do {
		ret = i2c_smbus_read_byte_data(led->client, reg);
		retry_times++;
		if (retry_times == 5)
			break;
	} while (ret < 0);

	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static void aw2016_soft_reset(struct aw2016_led *led)
{
	aw2016_write(led, AW2016_REG_RESET, AW2016_CHIP_RESET_MASK);
	usleep_range(5000, 6000);
}

static void aw2016_brightness_work(struct work_struct *work)
{
	struct aw2016_led *led =
		container_of(work, struct aw2016_led, brightness_work);
	u8 val;

	mutex_lock(&led->pdata->led->lock);

	/* enable aw2016 if disabled */
	aw2016_read(led, AW2016_REG_GCR1, &val);
	if (!(val & AW2016_CHIP_ENABLE_MASK)) {
		aw2016_write(led, AW2016_REG_GCR1,
			     AW2016_CHARGE_DISABLE_MASK |
				     AW2016_CHIP_ENABLE_MASK);
		usleep_range(2000, 3000);
	}

	if (led->cdev.brightness > 0) {
		if (led->cdev.brightness > led->cdev.max_brightness)
			led->cdev.brightness = led->cdev.max_brightness;
		aw2016_write(led, AW2016_REG_GCR2, led->pdata->imax);
		aw2016_write(led, AW2016_REG_LCFG1 + led->id,
			     (AW2016_LED_MANUAL_MODE_MASK |
			      led->pdata->led_current));
		aw2016_write(led, AW2016_REG_PWM1 + led->id,
			     led->cdev.brightness);
		aw2016_read(led, AW2016_REG_LEDEN, &val);
		aw2016_write(led, AW2016_REG_LEDEN, val | (1 << led->id));
	} else {
		aw2016_read(led, AW2016_REG_LEDEN, &val);
		aw2016_write(led, AW2016_REG_LEDEN, val & (~(1 << led->id)));
	}

	/*
	 * If value in AW2016_REG_LEDEN is 0, it means the RGB leds are
	 * all off. So we need to power it off.
	 */
	aw2016_read(led, AW2016_REG_LEDEN, &val);
	if (val == 0) {
		aw2016_write(led, AW2016_REG_GCR1,
			     AW2016_CHARGE_DISABLE_MASK |
				     AW2016_CHIP_DISABLE_MASK);
		mutex_unlock(&led->pdata->led->lock);
		return;
	}

	mutex_unlock(&led->pdata->led->lock);
}

static void aw2016_blink_work(struct work_struct *work)
{
	struct aw2016_led *led =
		container_of(work, struct aw2016_led, blink_work);
	u8 val;

	mutex_lock(&led->pdata->led->lock);

	/* enable aw2016 if disabled */
	aw2016_read(led, AW2016_REG_GCR1, &val);
	if (!(val & AW2016_CHIP_ENABLE_MASK)) {
		aw2016_write(led, AW2016_REG_GCR1,
			     AW2016_CHARGE_DISABLE_MASK |
				     AW2016_CHIP_ENABLE_MASK);
		usleep_range(2000, 3000);
	}

	led->cdev.brightness = led->blinking ? led->cdev.max_brightness : 0;

	if (led->blinking > 0) {
		aw2016_write(led, AW2016_REG_GCR2, led->pdata->imax);
		aw2016_write(led, AW2016_REG_PWM1 + led->id,
			     led->cdev.brightness);
		aw2016_write(led, AW2016_REG_LED1T0 + led->id * 3,
			     (led->pdata->rise_time_ms << 4 |
			      led->pdata->hold_time_ms));
		aw2016_write(led, AW2016_REG_LED1T1 + led->id * 3,
			     (led->pdata->fall_time_ms << 4 |
			      led->pdata->off_time_ms));
		aw2016_write(led, AW2016_REG_LCFG1 + led->id,
			     (AW2016_LED_BREATH_MODE_MASK |
			      led->pdata->led_current));
		aw2016_read(led, AW2016_REG_LEDEN, &val);
		aw2016_write(led, AW2016_REG_LEDEN, val | (1 << led->id));
	} else {
		aw2016_read(led, AW2016_REG_LEDEN, &val);
		aw2016_write(led, AW2016_REG_LEDEN, val & (~(1 << led->id)));
	}

	/*
	 * If value in AW2016_REG_LEDEN is 0, it means the RGB leds are
	 * all off. So we need to power it off.
	 */
	aw2016_read(led, AW2016_REG_LEDEN, &val);
	if (val == 0) {
		aw2016_write(led, AW2016_REG_GCR1,
			     AW2016_CHARGE_DISABLE_MASK |
				     AW2016_CHIP_DISABLE_MASK);
	}

	mutex_unlock(&led->pdata->led->lock);
}

static enum led_brightness aw2016_get_brightness(struct led_classdev *cdev)
{
	return cdev->brightness;
}

static void aw2016_set_brightness(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct aw2016_led *led = container_of(cdev, struct aw2016_led, cdev);

	led->cdev.brightness = brightness;

	schedule_work(&led->brightness_work);
}

static ssize_t breath_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2016_led *led = container_of(led_cdev, struct aw2016_led, cdev);

	return led->blinking;
}

static ssize_t breath_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	unsigned long blinking;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2016_led *led =
		container_of(led_cdev, struct aw2016_led, cdev);
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &blinking);
	if (ret)
		return ret;
	led->blinking = (int)blinking;
	schedule_work(&led->blink_work);

	return len;
}

static ssize_t led_time_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2016_led *led =
		container_of(led_cdev, struct aw2016_led, cdev);

	return scnprintf(buf, PAGE_SIZE, "%d %d %d %d\n",
			led->pdata->rise_time_ms, led->pdata->hold_time_ms,
			led->pdata->fall_time_ms, led->pdata->off_time_ms);
}

static ssize_t led_time_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2016_led *led =
		container_of(led_cdev, struct aw2016_led, cdev);
	int rc, rise_time_ms, hold_time_ms, fall_time_ms, off_time_ms;

	rc = sscanf(buf, "%d %d %d %d", &rise_time_ms, &hold_time_ms,
		    &fall_time_ms, &off_time_ms);

	mutex_lock(&led->pdata->led->lock);
	led->pdata->rise_time_ms = (rise_time_ms > MAX_RISE_TIME_MS) ?
					   MAX_RISE_TIME_MS :
					   rise_time_ms;
	led->pdata->hold_time_ms = (hold_time_ms > MAX_HOLD_TIME_MS) ?
					   MAX_HOLD_TIME_MS :
					   hold_time_ms;
	led->pdata->fall_time_ms = (fall_time_ms > MAX_FALL_TIME_MS) ?
					   MAX_FALL_TIME_MS :
					   fall_time_ms;
	led->pdata->off_time_ms =
		(off_time_ms > MAX_OFF_TIME_MS) ? MAX_OFF_TIME_MS : off_time_ms;
	led->blinking = 1;
	mutex_unlock(&led->pdata->led->lock);

	schedule_work(&led->blink_work);

	return len;
}

static ssize_t reg_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2016_led *led =
		container_of(led_cdev, struct aw2016_led, cdev);

	unsigned char i, reg_val;
	ssize_t len = 0;

	for (i = 0; i < AW2016_REG_MAX; i++) {
		if (!(aw2016_reg_access[i] & REG_RD_ACCESS))
			continue;
		aw2016_read(led, i, &reg_val);
		len += scnprintf(buf + len, PAGE_SIZE - len,
				"reg:0x%02x=0x%02x\n", i, reg_val);
	}

	return len;
}

static ssize_t reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw2016_led *led =
		container_of(led_cdev, struct aw2016_led, cdev);

	unsigned int databuf[2];

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		aw2016_write(led, (unsigned char)databuf[0],
			     (unsigned char)databuf[1]);
	}

	return len;
}

static DEVICE_ATTR_RW(breath);
static DEVICE_ATTR_RW(led_time);
static DEVICE_ATTR_RW(reg);

static struct attribute *aw2016_led_attributes[] = {
	&dev_attr_breath.attr,
	&dev_attr_led_time.attr,
	&dev_attr_reg.attr,
	NULL,
};

static struct attribute_group aw2016_led_attr_group = {
	.attrs = aw2016_led_attributes
};
static int aw2016_check_chipid(struct aw2016_led *led)
{
	u8 val;
	u8 cnt;

	for (cnt = 5; cnt > 0; cnt--) {
		aw2016_read(led, AW2016_REG_RESET, &val);
		dev_notice(&led->client->dev, "aw2016 chip id %0x\n", val);
		if (val == AW2016_CHIPID)
			return 0;
	}

	return -EINVAL;
}

static int aw2016_led_err_handle(struct aw2016_led *led_array, int parsed_leds)
{
	int i;
	/*
	 * If probe fails, cannot free resource of all LEDs, only free
	 * resources of LEDs which have allocated these resource really.
	 */
	for (i = 0; i < parsed_leds; i++) {
		sysfs_remove_group(&led_array[i].cdev.dev->kobj,
				   &aw2016_led_attr_group);
		led_classdev_unregister(&led_array[i].cdev);
		cancel_work_sync(&led_array[i].brightness_work);
		cancel_work_sync(&led_array[i].blink_work);
		led_array[i].pdata = NULL;
	}
	return i;
}

static int aw2016_led_parse_child_node(struct aw2016_led *led_array,
				       struct device_node *node)
{
	struct aw2016_led *led;
	struct device_node *temp;
	struct aw2016_platform_data *pdata;
	int rc = 0, parsed_leds = 0;

	for_each_child_of_node(node, temp) {
		led = &led_array[parsed_leds];
		led->client = led_array->client;

		pdata = devm_kzalloc(&led->client->dev,
				     sizeof(struct aw2016_platform_data),
				     GFP_KERNEL);
		if (!pdata) {
			dev_err(&led->client->dev,
				"Failed to allocate memory\n");
			goto free_err;
		}
		pdata->led = led_array;
		led->pdata = pdata;

		rc = of_property_read_string(temp, "awinic,name",
					     &led->cdev.name);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading led name, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,id", &led->id);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading id, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,imax",
					  &led->pdata->imax);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading imax, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,led-current",
					  &led->pdata->led_current);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading led-current, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,max-brightness",
					  &led->cdev.max_brightness);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading max-brightness, rc = %d\n",
				rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,rise-time-ms",
					  &led->pdata->rise_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading rise-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,hold-time-ms",
					  &led->pdata->hold_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading hold-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,fall-time-ms",
					  &led->pdata->fall_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading fall-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		rc = of_property_read_u32(temp, "awinic,off-time-ms",
					  &led->pdata->off_time_ms);
		if (rc < 0) {
			dev_err(&led->client->dev,
				"Failure reading off-time-ms, rc = %d\n", rc);
			goto free_pdata;
		}

		INIT_WORK(&led->brightness_work, aw2016_brightness_work);
		INIT_WORK(&led->blink_work, aw2016_blink_work);

		led->cdev.brightness_set = aw2016_set_brightness;
		led->cdev.brightness_get = aw2016_get_brightness;

		rc = led_classdev_register(&led->client->dev, &led->cdev);
		if (rc) {
			dev_err(&led->client->dev,
				"unable to register led %d,rc=%d\n", led->id,
				rc);
			goto free_pdata;
		}

		rc = sysfs_create_group(&led->cdev.dev->kobj,
					&aw2016_led_attr_group);
		if (rc) {
			dev_err(&led->client->dev, "led sysfs rc: %d\n", rc);
			goto free_class;
		}
		parsed_leds++;
	}

	return 0;

free_class:
	aw2016_led_err_handle(led_array, parsed_leds);
	led_classdev_unregister(&led_array[parsed_leds].cdev);
	cancel_work_sync(&led_array[parsed_leds].brightness_work);
	cancel_work_sync(&led_array[parsed_leds].blink_work);
	led_array[parsed_leds].pdata = NULL;
	return rc;

free_pdata:
	aw2016_led_err_handle(led_array, parsed_leds);
	return rc;

free_err:
	aw2016_led_err_handle(led_array, parsed_leds);
	return rc;
}

static int aw2016_led_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct aw2016_led *led_array;
	struct device_node *node;
	int ret = -EINVAL, num_leds = 0;

	node = client->dev.of_node;
	if (node == NULL)
		return -EINVAL;

	num_leds = of_get_child_count(node);

	if (!num_leds)
		return -EINVAL;

	led_array = devm_kzalloc(&client->dev,
				 (sizeof(struct aw2016_led) * num_leds),
				 GFP_KERNEL);
	if (!led_array)
		return -ENOMEM;

	led_array->client = client;
	led_array->num_leds = num_leds;

	mutex_init(&led_array->lock);

	ret = aw2016_led_parse_child_node(led_array, node);
	if (ret) {
		dev_err(&client->dev, "parsed node error\n");
		goto free_led_arry;
	}

	i2c_set_clientdata(client, led_array);

	ret = aw2016_check_chipid(led_array);
	if (ret) {
		dev_err(&client->dev, "Check chip id error\n");
		goto fail_parsed_node;
	}

	/* soft rst */
	aw2016_soft_reset(led_array);

	return 0;

fail_parsed_node:
	aw2016_led_err_handle(led_array, num_leds);
free_led_arry:
	mutex_destroy(&led_array->lock);
	led_array = NULL;
	return ret;
}

static void aw2016_led_remove(struct i2c_client *client)
{
	struct aw2016_led *led_array = i2c_get_clientdata(client);
	int i, parsed_leds = led_array->num_leds;

	for (i = 0; i < parsed_leds; i++) {
		sysfs_remove_group(&led_array[i].cdev.dev->kobj,
				   &aw2016_led_attr_group);
		led_classdev_unregister(&led_array[i].cdev);
		cancel_work_sync(&led_array[i].brightness_work);
		cancel_work_sync(&led_array[i].blink_work);
		led_array[i].pdata = NULL;
	}
	mutex_destroy(&led_array->lock);
	led_array = NULL;
}

static void aw2016_led_shutdown(struct i2c_client *client)
{
	struct aw2016_led *led = i2c_get_clientdata(client);

	aw2016_write(led, AW2016_REG_GCR1, AW2016_CHIP_STANDBY);
}

static const struct i2c_device_id aw2016_led_id[] = {
	{ "aw2016_led", 0 },
	{},
};

MODULE_DEVICE_TABLE(i2c, aw2016_led_id);

static const struct of_device_id aw2016_match_table[] = {
	{
		.compatible = "awinic,aw2016_led",
	},
	{},
};

static struct i2c_driver aw2016_led_driver = {
	.probe = aw2016_led_probe,
	.remove = aw2016_led_remove,
	.shutdown = aw2016_led_shutdown,
	.driver = {
		.name = "aw2016_led",
		.of_match_table = of_match_ptr(aw2016_match_table),
	},
	.id_table = aw2016_led_id,
};

static int __init aw2016_led_init(void)
{
	pr_info("%s: driver version: %s\n", __func__, AW2016_DRIVER_VERSION);
	return i2c_add_driver(&aw2016_led_driver);
}
module_init(aw2016_led_init);

static void __exit aw2016_led_exit(void)
{
	i2c_del_driver(&aw2016_led_driver);
}
module_exit(aw2016_led_exit);

MODULE_AUTHOR("<liweilei@awinic.com.cn>");
MODULE_DESCRIPTION("AWINIC AW2016 LED driver");
MODULE_LICENSE("GPL v2");
