// SPDX-License-Identifier: GPL-2.0-only
/*
 * LEDs driver for Dialog Semiconductor DA9030/DA9034
 *
 * Copyright (C) 2008 Compulab, Ltd.
 *	Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 *	Eric Miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/mfd/da903x.h>
#include <linux/slab.h>

#define DA9030_LED1_CONTROL	0x20
#define DA9030_LED2_CONTROL	0x21
#define DA9030_LED3_CONTROL	0x22
#define DA9030_LED4_CONTROL	0x23
#define DA9030_LEDPC_CONTROL	0x24
#define DA9030_MISC_CONTROL_A	0x26	/* Vibrator Control */

#define DA9034_LED1_CONTROL	0x35
#define DA9034_LED2_CONTROL	0x36
#define DA9034_VIBRA		0x40

struct da903x_led {
	struct led_classdev	cdev;
	struct device		*master;
	int			id;
	int			flags;
};

#define DA9030_LED_OFFSET(id)	((id) - DA9030_ID_LED_1)
#define DA9034_LED_OFFSET(id)	((id) - DA9034_ID_LED_1)

static int da903x_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct da903x_led *led =
			container_of(led_cdev, struct da903x_led, cdev);
	uint8_t val;
	int offset, ret = -EINVAL;

	switch (led->id) {
	case DA9030_ID_LED_1:
	case DA9030_ID_LED_2:
	case DA9030_ID_LED_3:
	case DA9030_ID_LED_4:
	case DA9030_ID_LED_PC:
		offset = DA9030_LED_OFFSET(led->id);
		val = led->flags & ~0x87;
		val |= value ? 0x80 : 0; /* EN bit */
		val |= (0x7 - (value >> 5)) & 0x7; /* PWM<2:0> */
		ret = da903x_write(led->master, DA9030_LED1_CONTROL + offset,
				   val);
		break;
	case DA9030_ID_VIBRA:
		val = led->flags & ~0x80;
		val |= value ? 0x80 : 0; /* EN bit */
		ret = da903x_write(led->master, DA9030_MISC_CONTROL_A, val);
		break;
	case DA9034_ID_LED_1:
	case DA9034_ID_LED_2:
		offset = DA9034_LED_OFFSET(led->id);
		val = (value * 0x5f / LED_FULL) & 0x7f;
		val |= (led->flags & DA9034_LED_RAMP) ? 0x80 : 0;
		ret = da903x_write(led->master, DA9034_LED1_CONTROL + offset,
				   val);
		break;
	case DA9034_ID_VIBRA:
		val = value & 0xfe;
		ret = da903x_write(led->master, DA9034_VIBRA, val);
		break;
	}

	return ret;
}

static int da903x_led_probe(struct platform_device *pdev)
{
	struct led_info *pdata = dev_get_platdata(&pdev->dev);
	struct da903x_led *led;
	int id, ret;

	if (pdata == NULL)
		return 0;

	id = pdev->id;

	if (!((id >= DA9030_ID_LED_1 && id <= DA9030_ID_VIBRA) ||
	      (id >= DA9034_ID_LED_1 && id <= DA9034_ID_VIBRA))) {
		dev_err(&pdev->dev, "invalid LED ID (%d) specified\n", id);
		return -EINVAL;
	}

	led = devm_kzalloc(&pdev->dev, sizeof(struct da903x_led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->cdev.name = pdata->name;
	led->cdev.default_trigger = pdata->default_trigger;
	led->cdev.brightness_set_blocking = da903x_led_set;
	led->cdev.brightness = LED_OFF;

	led->id = id;
	led->flags = pdata->flags;
	led->master = pdev->dev.parent;

	ret = led_classdev_register(led->master, &led->cdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register LED %d\n", id);
		return ret;
	}

	platform_set_drvdata(pdev, led);

	return 0;
}

static void da903x_led_remove(struct platform_device *pdev)
{
	struct da903x_led *led = platform_get_drvdata(pdev);

	led_classdev_unregister(&led->cdev);
}

static struct platform_driver da903x_led_driver = {
	.driver	= {
		.name	= "da903x-led",
	},
	.probe		= da903x_led_probe,
	.remove		= da903x_led_remove,
};

module_platform_driver(da903x_led_driver);

MODULE_DESCRIPTION("LEDs driver for Dialog Semiconductor DA9030/DA9034");
MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da903x-led");
