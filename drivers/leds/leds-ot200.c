/*
 * Bachmann ot200 leds driver.
 *
 * Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *         Christian Gmeiner <christian.gmeiner@gmail.com>
 *
 * License: GPL as published by the FSF.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/io.h>
#include <linux/module.h>


struct ot200_led {
	struct led_classdev cdev;
	const char *name;
	unsigned long port;
	u8 mask;
};

/*
 * The device has three leds on the back panel (led_err, led_init and led_run)
 * and can handle up to seven leds on the front panel.
 */

static struct ot200_led leds[] = {
	{
		.name = "led_run",
		.port = 0x5a,
		.mask = BIT(0),
	},
	{
		.name = "led_init",
		.port = 0x5a,
		.mask = BIT(1),
	},
	{
		.name = "led_err",
		.port = 0x5a,
		.mask = BIT(2),
	},
	{
		.name = "led_1",
		.port = 0x49,
		.mask = BIT(6),
	},
	{
		.name = "led_2",
		.port = 0x49,
		.mask = BIT(5),
	},
	{
		.name = "led_3",
		.port = 0x49,
		.mask = BIT(4),
	},
	{
		.name = "led_4",
		.port = 0x49,
		.mask = BIT(3),
	},
	{
		.name = "led_5",
		.port = 0x49,
		.mask = BIT(2),
	},
	{
		.name = "led_6",
		.port = 0x49,
		.mask = BIT(1),
	},
	{
		.name = "led_7",
		.port = 0x49,
		.mask = BIT(0),
	}
};

static DEFINE_SPINLOCK(value_lock);

/*
 * we need to store the current led states, as it is not
 * possible to read the current led state via inb().
 */
static u8 leds_back;
static u8 leds_front;

static void ot200_led_brightness_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	struct ot200_led *led = container_of(led_cdev, struct ot200_led, cdev);
	u8 *val;
	unsigned long flags;

	spin_lock_irqsave(&value_lock, flags);

	if (led->port == 0x49)
		val = &leds_front;
	else if (led->port == 0x5a)
		val = &leds_back;
	else
		BUG();

	if (value == LED_OFF)
		*val &= ~led->mask;
	else
		*val |= led->mask;

	outb(*val, led->port);
	spin_unlock_irqrestore(&value_lock, flags);
}

static int __devinit ot200_led_probe(struct platform_device *pdev)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(leds); i++) {

		leds[i].cdev.name = leds[i].name;
		leds[i].cdev.brightness_set = ot200_led_brightness_set;

		ret = led_classdev_register(&pdev->dev, &leds[i].cdev);
		if (ret < 0)
			goto err;
	}

	leds_front = 0;		/* turn off all front leds */
	leds_back = BIT(1);	/* turn on init led */
	outb(leds_front, 0x49);
	outb(leds_back, 0x5a);

	return 0;

err:
	for (i = i - 1; i >= 0; i--)
		led_classdev_unregister(&leds[i].cdev);

	return ret;
}

static int __devexit ot200_led_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(leds); i++)
		led_classdev_unregister(&leds[i].cdev);

	return 0;
}

static struct platform_driver ot200_led_driver = {
	.probe		= ot200_led_probe,
	.remove		= __devexit_p(ot200_led_remove),
	.driver		= {
		.name	= "leds-ot200",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(ot200_led_driver);

MODULE_AUTHOR("Sebastian A. Siewior <bigeasy@linutronix.de>");
MODULE_DESCRIPTION("ot200 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-ot200");
