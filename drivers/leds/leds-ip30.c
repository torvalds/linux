// SPDX-License-Identifier: GPL-2.0
/*
 * LED Driver for SGI Octane machines
 */

#include <asm/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

#define IP30_LED_SYSTEM	0
#define IP30_LED_FAULT	1

struct ip30_led {
	struct led_classdev cdev;
	u32 __iomem *reg;
};

static void ip30led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct ip30_led *led = container_of(led_cdev, struct ip30_led, cdev);

	writel(value, led->reg);
}

static int ip30led_create(struct platform_device *pdev, int num)
{
	struct ip30_led *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->reg = devm_platform_ioremap_resource(pdev, num);
	if (IS_ERR(data->reg))
		return PTR_ERR(data->reg);

	switch (num) {
	case IP30_LED_SYSTEM:
		data->cdev.name = "white:power";
		break;
	case IP30_LED_FAULT:
		data->cdev.name = "red:fault";
		break;
	default:
		return -EINVAL;
	}

	data->cdev.brightness = readl(data->reg);
	data->cdev.max_brightness = 1;
	data->cdev.brightness_set = ip30led_set;

	return devm_led_classdev_register(&pdev->dev, &data->cdev);
}

static int ip30led_probe(struct platform_device *pdev)
{
	int ret;

	ret = ip30led_create(pdev, IP30_LED_SYSTEM);
	if (ret < 0)
		return ret;

	return ip30led_create(pdev, IP30_LED_FAULT);
}

static struct platform_driver ip30led_driver = {
	.probe		= ip30led_probe,
	.driver		= {
		.name		= "ip30-leds",
	},
};

module_platform_driver(ip30led_driver);

MODULE_AUTHOR("Thomas Bogendoerfer <tbogendoerfer@suse.de>");
MODULE_DESCRIPTION("SGI Octane LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ip30-leds");
