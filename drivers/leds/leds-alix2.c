/*
 * LEDs driver for PCEngines ALIX.2 and ALIX.3
 *
 * Copyright (C) 2008 Constantin Baranov <const@mimas.ru>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>

static int force = 0;
module_param(force, bool, 0444);
MODULE_PARM_DESC(force, "Assume system has ALIX.2/ALIX.3 style LEDs");

struct alix_led {
	struct led_classdev cdev;
	unsigned short port;
	unsigned int on_value;
	unsigned int off_value;
};

static void alix_led_set(struct led_classdev *led_cdev,
			 enum led_brightness brightness)
{
	struct alix_led *led_dev =
		container_of(led_cdev, struct alix_led, cdev);

	if (brightness)
		outl(led_dev->on_value, led_dev->port);
	else
		outl(led_dev->off_value, led_dev->port);
}

static struct alix_led alix_leds[] = {
	{
		.cdev = {
			.name = "alix:1",
			.brightness_set = alix_led_set,
		},
		.port = 0x6100,
		.on_value = 1 << 22,
		.off_value = 1 << 6,
	},
	{
		.cdev = {
			.name = "alix:2",
			.brightness_set = alix_led_set,
		},
		.port = 0x6180,
		.on_value = 1 << 25,
		.off_value = 1 << 9,
	},
	{
		.cdev = {
			.name = "alix:3",
			.brightness_set = alix_led_set,
		},
		.port = 0x6180,
		.on_value = 1 << 27,
		.off_value = 1 << 11,
	},
};

static int __init alix_led_probe(struct platform_device *pdev)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(alix_leds); i++) {
		alix_leds[i].cdev.flags |= LED_CORE_SUSPENDRESUME;
		ret = led_classdev_register(&pdev->dev, &alix_leds[i].cdev);
		if (ret < 0)
			goto fail;
	}
	return 0;

fail:
	while (--i >= 0)
		led_classdev_unregister(&alix_leds[i].cdev);
	return ret;
}

static int alix_led_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(alix_leds); i++)
		led_classdev_unregister(&alix_leds[i].cdev);
	return 0;
}

static struct platform_driver alix_led_driver = {
	.remove = alix_led_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
};

static int __init alix_present(void)
{
	const unsigned long bios_phys = 0x000f0000;
	const size_t bios_len = 0x00010000;
	const char alix_sig[] = "PC Engines ALIX.";
	const size_t alix_sig_len = sizeof(alix_sig) - 1;

	const char *bios_virt;
	const char *scan_end;
	const char *p;
	int ret = 0;

	if (force) {
		printk(KERN_NOTICE "%s: forced to skip BIOS test, "
		       "assume system has ALIX.2 style LEDs\n",
		       KBUILD_MODNAME);
		ret = 1;
		goto out;
	}

	bios_virt = phys_to_virt(bios_phys);
	scan_end = bios_virt + bios_len - (alix_sig_len + 2);
	for (p = bios_virt; p < scan_end; p++) {
		const char *tail;

		if (memcmp(p, alix_sig, alix_sig_len) != 0) {
			continue;
		}

		tail = p + alix_sig_len;
		if ((tail[0] == '2' || tail[0] == '3') && tail[1] == '\0') {
			printk(KERN_INFO
			       "%s: system is recognized as \"%s\"\n",
			       KBUILD_MODNAME, p);
			ret = 1;
			break;
		}
	}

out:
	return ret;
}

static struct platform_device *pdev;

static int __init alix_led_init(void)
{
	int ret;

	if (!alix_present()) {
		ret = -ENODEV;
		goto out;
	}

	/* enable output on GPIO for LED 1,2,3 */
	outl(1 << 6, 0x6104);
	outl(1 << 9, 0x6184);
	outl(1 << 11, 0x6184);

	pdev = platform_device_register_simple(KBUILD_MODNAME, -1, NULL, 0);
	if (!IS_ERR(pdev)) {
		ret = platform_driver_probe(&alix_led_driver, alix_led_probe);
		if (ret)
			platform_device_unregister(pdev);
	} else
		ret = PTR_ERR(pdev);

out:
	return ret;
}

static void __exit alix_led_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&alix_led_driver);
}

module_init(alix_led_init);
module_exit(alix_led_exit);

MODULE_AUTHOR("Constantin Baranov <const@mimas.ru>");
MODULE_DESCRIPTION("PCEngines ALIX.2 and ALIX.3 LED driver");
MODULE_LICENSE("GPL");
