/*
 * IXP4XX GPIO driver LED driver
 *
 * Author: John Bowler <jbowler@acm.org>
 *
 * Copyright (c) 2006 John Bowler
 *
 * Permission is hereby granted, free of charge, to any
 * person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the
 * Software without restriction, including without
 * limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice
 * shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
 * ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 * SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/leds.h>
#include <asm/arch/hardware.h>

extern spinlock_t gpio_lock;

/* Up to 16 gpio lines are possible. */
#define GPIO_MAX 16
static struct ixp4xxgpioled_device {
	struct led_classdev ancestor;
	int               flags;
} ixp4xxgpioled_devices[GPIO_MAX];

void ixp4xxgpioled_brightness_set(struct led_classdev *pled,
				enum led_brightness value)
{
	const struct ixp4xxgpioled_device *const ixp4xx_dev =
		container_of(pled, struct ixp4xxgpioled_device, ancestor);
	const u32 gpio_pin = ixp4xx_dev - ixp4xxgpioled_devices;

	if (gpio_pin < GPIO_MAX && ixp4xx_dev->ancestor.name != 0) {
		/* Set or clear the 'gpio_pin' bit according to the style
		 * and the required setting (value > 0 == on)
		 */
		const int gpio_value =
			(value > 0) == (ixp4xx_dev->flags != IXP4XX_GPIO_LOW) ?
				IXP4XX_GPIO_HIGH : IXP4XX_GPIO_LOW;

		{
			unsigned long flags;
			spin_lock_irqsave(&gpio_lock, flags);
			gpio_line_set(gpio_pin, gpio_value);
			spin_unlock_irqrestore(&gpio_lock, flags);
		}
	}
}

/* LEDs are described in resources, the following iterates over the valid
 * LED resources.
 */
#define for_all_leds(i, pdev) \
	for (i=0; i<pdev->num_resources; ++i) \
		if (pdev->resource[i].start < GPIO_MAX && \
			pdev->resource[i].name != 0)

/* The following applies 'operation' to each LED from the given platform,
 * the function always returns 0 to allow tail call elimination.
 */
static int apply_to_all_leds(struct platform_device *pdev,
	void (*operation)(struct led_classdev *pled))
{
	int i;

	for_all_leds(i, pdev)
		operation(&ixp4xxgpioled_devices[pdev->resource[i].start].ancestor);
	return 0;
}

#ifdef CONFIG_PM
static int ixp4xxgpioled_suspend(struct platform_device *pdev,
				pm_message_t state)
{
	return apply_to_all_leds(pdev, led_classdev_suspend);
}

static int ixp4xxgpioled_resume(struct platform_device *pdev)
{
	return apply_to_all_leds(pdev, led_classdev_resume);
}
#endif

static void ixp4xxgpioled_remove_one_led(struct led_classdev *pled)
{
	led_classdev_unregister(pled);
	pled->name = 0;
}

static int ixp4xxgpioled_remove(struct platform_device *pdev)
{
	return apply_to_all_leds(pdev, ixp4xxgpioled_remove_one_led);
}

static int ixp4xxgpioled_probe(struct platform_device *pdev)
{
	/* The board level has to tell the driver where the
	 * LEDs are connected - there is no way to find out
	 * electrically.  It must also say whether the GPIO
	 * lines are active high or active low.
	 *
	 * To do this read the num_resources (the number of
	 * LEDs) and the struct resource (the data for each
	 * LED).  The name comes from the resource, and it
	 * isn't copied.
	 */
	int i;

	for_all_leds(i, pdev) {
		const u8 gpio_pin = pdev->resource[i].start;
		int      rc;

		if (ixp4xxgpioled_devices[gpio_pin].ancestor.name == 0) {
			unsigned long flags;

			spin_lock_irqsave(&gpio_lock, flags);
			gpio_line_config(gpio_pin, IXP4XX_GPIO_OUT);
			/* The config can, apparently, reset the state,
			 * I suspect the gpio line may be an input and
			 * the config may cause the line to be latched,
			 * so the setting depends on how the LED is
			 * connected to the line (which affects how it
			 * floats if not driven).
			 */
			gpio_line_set(gpio_pin, IXP4XX_GPIO_HIGH);
			spin_unlock_irqrestore(&gpio_lock, flags);

			ixp4xxgpioled_devices[gpio_pin].flags =
				pdev->resource[i].flags & IORESOURCE_BITS;

			ixp4xxgpioled_devices[gpio_pin].ancestor.name =
				pdev->resource[i].name;

			/* This is how a board manufacturer makes the LED
			 * come on on reset - the GPIO line will be high, so
			 * make the LED light when the line is low...
			 */
			if (ixp4xxgpioled_devices[gpio_pin].flags != IXP4XX_GPIO_LOW)
				ixp4xxgpioled_devices[gpio_pin].ancestor.brightness = 100;
			else
				ixp4xxgpioled_devices[gpio_pin].ancestor.brightness = 0;

			ixp4xxgpioled_devices[gpio_pin].ancestor.flags = 0;

			ixp4xxgpioled_devices[gpio_pin].ancestor.brightness_set =
				ixp4xxgpioled_brightness_set;

			ixp4xxgpioled_devices[gpio_pin].ancestor.default_trigger = 0;
		}

		rc = led_classdev_register(&pdev->dev,
				&ixp4xxgpioled_devices[gpio_pin].ancestor);
		if (rc < 0) {
			ixp4xxgpioled_devices[gpio_pin].ancestor.name = 0;
			ixp4xxgpioled_remove(pdev);
			return rc;
		}
	}

	return 0;
}

static struct platform_driver ixp4xxgpioled_driver = {
	.probe   = ixp4xxgpioled_probe,
	.remove  = ixp4xxgpioled_remove,
#ifdef CONFIG_PM
	.suspend = ixp4xxgpioled_suspend,
	.resume  = ixp4xxgpioled_resume,
#endif
	.driver  = {
		.name = "IXP4XX-GPIO-LED",
	},
};

static int __init ixp4xxgpioled_init(void)
{
	return platform_driver_register(&ixp4xxgpioled_driver);
}

static void __exit ixp4xxgpioled_exit(void)
{
	platform_driver_unregister(&ixp4xxgpioled_driver);
}

module_init(ixp4xxgpioled_init);
module_exit(ixp4xxgpioled_exit);

MODULE_AUTHOR("John Bowler <jbowler@acm.org>");
MODULE_DESCRIPTION("IXP4XX GPIO LED driver");
MODULE_LICENSE("Dual MIT/GPL");
