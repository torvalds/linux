/*
 * Driver for the 8 user LEDs found on the RealViews and Versatiles
 * Based on DaVinci's DM365 board code
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Linus Walleij <triad@df.lth.se>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

struct versatile_led {
	void __iomem		*base;
	struct led_classdev	cdev;
	u8			mask;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} versatile_leds[] = {
	{ "versatile:0", "heartbeat", },
	{ "versatile:1", "mmc0", },
	{ "versatile:2", "cpu0" },
	{ "versatile:3", "cpu1" },
	{ "versatile:4", "cpu2" },
	{ "versatile:5", "cpu3" },
	{ "versatile:6", },
	{ "versatile:7", },
};

static void versatile_led_set(struct led_classdev *cdev,
			      enum led_brightness b)
{
	struct versatile_led *led = container_of(cdev,
						 struct versatile_led, cdev);
	u32 reg = readl(led->base);

	if (b != LED_OFF)
		reg |= led->mask;
	else
		reg &= ~led->mask;
	writel(reg, led->base);
}

static enum led_brightness versatile_led_get(struct led_classdev *cdev)
{
	struct versatile_led *led = container_of(cdev,
						 struct versatile_led, cdev);
	u32 reg = readl(led->base);

	return (reg & led->mask) ? LED_FULL : LED_OFF;
}

static int versatile_leds_probe(struct platform_device *dev)
{
	int i;
	struct resource *res;
	void __iomem *base;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&dev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* All off */
	writel(0, base);
	for (i = 0; i < ARRAY_SIZE(versatile_leds); i++) {
		struct versatile_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->base = base;
		led->cdev.name = versatile_leds[i].name;
		led->cdev.brightness_set = versatile_led_set;
		led->cdev.brightness_get = versatile_led_get;
		led->cdev.default_trigger = versatile_leds[i].trigger;
		led->mask = BIT(i);

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}

static struct platform_driver versatile_leds_driver = {
	.driver = {
		.name   = "versatile-leds",
	},
	.probe = versatile_leds_probe,
};

module_platform_driver(versatile_leds_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("ARM Versatile LED driver");
MODULE_LICENSE("GPL v2");
