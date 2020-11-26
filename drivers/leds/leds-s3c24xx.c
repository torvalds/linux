// SPDX-License-Identifier: GPL-2.0-only
/* drivers/leds/leds-s3c24xx.c
 *
 * (c) 2006 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - LEDs GPIO driver
*/

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_data/leds-s3c24xx.h>

/* our context */

struct s3c24xx_gpio_led {
	struct led_classdev		 cdev;
	struct s3c24xx_led_platdata	*pdata;
	struct gpio_desc		*gpiod;
};

static inline struct s3c24xx_gpio_led *to_gpio(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct s3c24xx_gpio_led, cdev);
}

static void s3c24xx_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct s3c24xx_gpio_led *led = to_gpio(led_cdev);

	gpiod_set_value(led->gpiod, !!value);
}

static int s3c24xx_led_probe(struct platform_device *dev)
{
	struct s3c24xx_led_platdata *pdata = dev_get_platdata(&dev->dev);
	struct s3c24xx_gpio_led *led;
	int ret;

	led = devm_kzalloc(&dev->dev, sizeof(struct s3c24xx_gpio_led),
			   GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->cdev.brightness_set = s3c24xx_led_set;
	led->cdev.default_trigger = pdata->def_trigger;
	led->cdev.name = pdata->name;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;

	led->pdata = pdata;

	/* Default to off */
	led->gpiod = devm_gpiod_get(&dev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(led->gpiod))
		return PTR_ERR(led->gpiod);

	/* register our new led device */
	ret = devm_led_classdev_register(&dev->dev, &led->cdev);
	if (ret < 0)
		dev_err(&dev->dev, "led_classdev_register failed\n");

	return ret;
}

static struct platform_driver s3c24xx_led_driver = {
	.probe		= s3c24xx_led_probe,
	.driver		= {
		.name		= "s3c24xx_led",
	},
};

module_platform_driver(s3c24xx_led_driver);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C24XX LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:s3c24xx_led");
