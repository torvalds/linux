/*
 * nvec_leds: LED driver for a NVIDIA compliant embedded controller
 *
 * Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.launchpad.net>
 *
 * Authors:  Ilya Petrov <ilya.muromec@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include "nvec.h"

#define to_nvec_led(led_cdev) \
	container_of(led_cdev, struct nvec_led, cdev)

#define NVEC_LED_REQ {'\x0d', '\x10', '\x45', '\x10', '\x00'}

#define NVEC_LED_MAX 8

struct nvec_led {
	struct led_classdev cdev;
	struct nvec_chip *nvec;
};

static void nvec_led_brightness_set(struct led_classdev *led_cdev,
				    enum led_brightness value)
{
	struct nvec_led *led = to_nvec_led(led_cdev);
	unsigned char buf[] = NVEC_LED_REQ;
	buf[4] = value;

	nvec_write_async(led->nvec, buf, sizeof(buf));

	led->cdev.brightness = value;

}

static int __devinit nvec_led_probe(struct platform_device *pdev)
{
	struct nvec_chip *nvec = dev_get_drvdata(pdev->dev.parent);
	struct nvec_led *led;
	int ret = 0;

	led = kzalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	led->cdev.max_brightness = NVEC_LED_MAX;

	led->cdev.brightness_set = nvec_led_brightness_set;
	led->cdev.name = "nvec-led";
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led->nvec = nvec;

	platform_set_drvdata(pdev, led);

	ret = led_classdev_register(&pdev->dev, &led->cdev);
	if (ret < 0)
		goto err_led;

	/* to expose the default value to userspace */
	led->cdev.brightness = 0;

	return 0;

err_led:
	kfree(led);
	return ret;
}

static int __devexit nvec_led_remove(struct platform_device *pdev)
{
	struct nvec_led *led = platform_get_drvdata(pdev);

	led_classdev_unregister(&led->cdev);
	kfree(led);
	return 0;
}

static struct platform_driver nvec_led_driver = {
	.probe  = nvec_led_probe,
	.remove = __devexit_p(nvec_led_remove),
	.driver = {
		   .name  = "nvec-leds",
		   .owner = THIS_MODULE,
	},
};

static int __init nvec_led_init(void)
{
	return platform_driver_register(&nvec_led_driver);
}

module_init(nvec_led_init);

static void __exit nvec_led_exit(void)
{
	platform_driver_unregister(&nvec_led_driver);
}

module_exit(nvec_led_exit);

MODULE_AUTHOR("Ilya Petrov <ilya.muromec@gmail.com>");
MODULE_DESCRIPTION("Tegra NVEC LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nvec-leds");
