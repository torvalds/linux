/*
 * LED driver for Marvell 88PM860x
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mfd/88pm860x.h>

#define LED_PWM_SHIFT		(3)
#define LED_PWM_MASK		(0x1F)
#define LED_CURRENT_MASK	(0x07 << 5)

#define LED_BLINK_ON_MASK	(0x07)
#define LED_BLINK_MASK		(0x7F)

#define LED_BLINK_ON(x)		((x & 0x7) * 66 + 66)
#define LED_BLINK_ON_MIN	LED_BLINK_ON(0)
#define LED_BLINK_ON_MAX	LED_BLINK_ON(0x7)
#define LED_ON_CONTINUOUS	(0x0F << 3)
#define LED_TO_ON(x)		((x - 66) / 66)

#define LED1_BLINK_EN		(1 << 1)
#define LED2_BLINK_EN		(1 << 2)

struct pm860x_led {
	struct led_classdev cdev;
	struct i2c_client *i2c;
	struct work_struct work;
	struct pm860x_chip *chip;
	struct mutex lock;
	char name[MFD_NAME_SIZE];

	int port;
	int iset;
	unsigned char brightness;
	unsigned char current_brightness;

	int blink_data;
	int blink_time;
	int blink_on;
	int blink_off;
};

/* return offset of color register */
static inline int __led_off(int port)
{
	int ret = -EINVAL;

	switch (port) {
	case PM8606_LED1_RED:
	case PM8606_LED1_GREEN:
	case PM8606_LED1_BLUE:
		ret = port - PM8606_LED1_RED + PM8606_RGB1B;
		break;
	case PM8606_LED2_RED:
	case PM8606_LED2_GREEN:
	case PM8606_LED2_BLUE:
		ret = port - PM8606_LED2_RED + PM8606_RGB2B;
		break;
	}
	return ret;
}

/* return offset of blink register */
static inline int __blink_off(int port)
{
	int ret = -EINVAL;

	switch (port) {
	case PM8606_LED1_RED:
	case PM8606_LED1_GREEN:
	case PM8606_LED1_BLUE:
		ret = PM8606_RGB1A;
		break;
	case PM8606_LED2_RED:
	case PM8606_LED2_GREEN:
	case PM8606_LED2_BLUE:
		ret = PM8606_RGB2A;
		break;
	}
	return ret;
}

static inline int __blink_ctl_mask(int port)
{
	int ret = -EINVAL;

	switch (port) {
	case PM8606_LED1_RED:
	case PM8606_LED1_GREEN:
	case PM8606_LED1_BLUE:
		ret = LED1_BLINK_EN;
		break;
	case PM8606_LED2_RED:
	case PM8606_LED2_GREEN:
	case PM8606_LED2_BLUE:
		ret = LED2_BLINK_EN;
		break;
	}
	return ret;
}

static void pm860x_led_work(struct work_struct *work)
{

	struct pm860x_led *led;
	struct pm860x_chip *chip;
	int mask;

	led = container_of(work, struct pm860x_led, work);
	chip = led->chip;
	mutex_lock(&led->lock);
	if ((led->current_brightness == 0) && led->brightness) {
		if (led->iset) {
			pm860x_set_bits(led->i2c, __led_off(led->port),
					LED_CURRENT_MASK, led->iset);
		}
		mask = __blink_ctl_mask(led->port);
		pm860x_set_bits(led->i2c, PM8606_WLED3B, mask, mask);
	} else if (led->brightness == 0) {
		pm860x_set_bits(led->i2c, __led_off(led->port),
				LED_CURRENT_MASK, 0);
		mask = __blink_ctl_mask(led->port);
		pm860x_set_bits(led->i2c, PM8606_WLED3B, mask, 0);
	}
	pm860x_set_bits(led->i2c, __led_off(led->port), LED_PWM_MASK,
			led->brightness);
	led->current_brightness = led->brightness;
	dev_dbg(chip->dev, "Update LED. (reg:%d, brightness:%d)\n",
		__led_off(led->port), led->brightness);
	mutex_unlock(&led->lock);
}

static void pm860x_led_set(struct led_classdev *cdev,
			   enum led_brightness value)
{
	struct pm860x_led *data = container_of(cdev, struct pm860x_led, cdev);

	data->brightness = value >> 3;
	schedule_work(&data->work);
}

static int __check_device(struct pm860x_led_pdata *pdata, char *name)
{
	struct pm860x_led_pdata *p = pdata;
	int ret = -EINVAL;

	while (p && p->id) {
		if ((p->id != PM8606_ID_LED) || (p->flags < 0))
			break;

		if (!strncmp(name, pm860x_led_name[p->flags],
			MFD_NAME_SIZE)) {
			ret = (int)p->flags;
			break;
		}
		p++;
	}
	return ret;
}

static int pm860x_led_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_platform_data *pm860x_pdata;
	struct pm860x_led_pdata *pdata;
	struct pm860x_led *data;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No I/O resource!\n");
		return -EINVAL;
	}

	if (pdev->dev.parent->platform_data) {
		pm860x_pdata = pdev->dev.parent->platform_data;
		pdata = pm860x_pdata->led;
	} else {
		dev_err(&pdev->dev, "No platform data!\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(struct pm860x_led), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	strncpy(data->name, res->name, MFD_NAME_SIZE);
	dev_set_drvdata(&pdev->dev, data);
	data->chip = chip;
	data->i2c = (chip->id == CHIP_PM8606) ? chip->client : chip->companion;
	data->iset = pdata->iset;
	data->port = __check_device(pdata, data->name);
	if (data->port < 0) {
		dev_err(&pdev->dev, "check device failed\n");
		kfree(data);
		return -EINVAL;
	}

	data->current_brightness = 0;
	data->cdev.name = data->name;
	data->cdev.brightness_set = pm860x_led_set;
	mutex_init(&data->lock);
	INIT_WORK(&data->work, pm860x_led_work);

	ret = led_classdev_register(chip->dev, &data->cdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register LED: %d\n", ret);
		goto out;
	}
	return 0;
out:
	kfree(data);
	return ret;
}

static int pm860x_led_remove(struct platform_device *pdev)
{
	struct pm860x_led *data = platform_get_drvdata(pdev);

	led_classdev_unregister(&data->cdev);
	kfree(data);

	return 0;
}

static struct platform_driver pm860x_led_driver = {
	.driver	= {
		.name	= "88pm860x-led",
		.owner	= THIS_MODULE,
	},
	.probe	= pm860x_led_probe,
	.remove	= pm860x_led_remove,
};

static int __devinit pm860x_led_init(void)
{
	return platform_driver_register(&pm860x_led_driver);
}
module_init(pm860x_led_init);

static void __devexit pm860x_led_exit(void)
{
	platform_driver_unregister(&pm860x_led_driver);
}
module_exit(pm860x_led_exit);

MODULE_DESCRIPTION("LED driver for Marvell PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88pm860x-led");
