// SPDX-License-Identifier: GPL-2.0
/*
 * Siemens SIMATIC IPC driver for LEDs
 *
 * Copyright (c) Siemens AG, 2018-2021
 *
 * Authors:
 *  Henning Schild <henning.schild@siemens.com>
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *  Gerd Haeussler <gerd.haeussler.ext@siemens.com>
 */

#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>

#define SIMATIC_IPC_LED_PORT_BASE	0x404E

struct simatic_ipc_led {
	unsigned int value; /* mask for io */
	char *name;
	struct led_classdev cdev;
};

static struct simatic_ipc_led simatic_ipc_leds_io[] = {
	{1 << 15, "green:" LED_FUNCTION_STATUS "-1" },
	{1 << 7,  "yellow:" LED_FUNCTION_STATUS "-1" },
	{1 << 14, "red:" LED_FUNCTION_STATUS "-2" },
	{1 << 6,  "yellow:" LED_FUNCTION_STATUS "-2" },
	{1 << 13, "red:" LED_FUNCTION_STATUS "-3" },
	{1 << 5,  "yellow:" LED_FUNCTION_STATUS "-3" },
	{ }
};

static struct resource simatic_ipc_led_io_res =
	DEFINE_RES_IO_NAMED(SIMATIC_IPC_LED_PORT_BASE, SZ_2, KBUILD_MODNAME);

static DEFINE_SPINLOCK(reg_lock);

static inline struct simatic_ipc_led *cdev_to_led(struct led_classdev *led_cd)
{
	return container_of(led_cd, struct simatic_ipc_led, cdev);
}

static void simatic_ipc_led_set_io(struct led_classdev *led_cd,
				   enum led_brightness brightness)
{
	struct simatic_ipc_led *led = cdev_to_led(led_cd);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&reg_lock, flags);

	val = inw(SIMATIC_IPC_LED_PORT_BASE);
	if (brightness == LED_OFF)
		outw(val | led->value, SIMATIC_IPC_LED_PORT_BASE);
	else
		outw(val & ~led->value, SIMATIC_IPC_LED_PORT_BASE);

	spin_unlock_irqrestore(&reg_lock, flags);
}

static enum led_brightness simatic_ipc_led_get_io(struct led_classdev *led_cd)
{
	struct simatic_ipc_led *led = cdev_to_led(led_cd);

	return inw(SIMATIC_IPC_LED_PORT_BASE) & led->value ? LED_OFF : led_cd->max_brightness;
}

static int simatic_ipc_leds_probe(struct platform_device *pdev)
{
	const struct simatic_ipc_platform *plat = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct simatic_ipc_led *ipcled;
	struct led_classdev *cdev;
	struct resource *res;
	int err;

	switch (plat->devmode) {
	case SIMATIC_IPC_DEVICE_227D:
	case SIMATIC_IPC_DEVICE_427E:
		res = &simatic_ipc_led_io_res;
		ipcled = simatic_ipc_leds_io;
		/* on 227D the two bytes work the other way araound */
		if (plat->devmode == SIMATIC_IPC_DEVICE_227D) {
			while (ipcled->value) {
				ipcled->value = swab16(ipcled->value);
				ipcled++;
			}
			ipcled = simatic_ipc_leds_io;
		}
		if (!devm_request_region(dev, res->start, resource_size(res), KBUILD_MODNAME)) {
			dev_err(dev, "Unable to register IO resource at %pR\n", res);
			return -EBUSY;
		}
		break;
	default:
		return -ENODEV;
	}

	while (ipcled->value) {
		cdev = &ipcled->cdev;
		cdev->brightness_set = simatic_ipc_led_set_io;
		cdev->brightness_get = simatic_ipc_led_get_io;
		cdev->max_brightness = LED_ON;
		cdev->name = ipcled->name;

		err = devm_led_classdev_register(dev, cdev);
		if (err < 0)
			return err;
		ipcled++;
	}

	return 0;
}

static struct platform_driver simatic_ipc_led_driver = {
	.probe = simatic_ipc_leds_probe,
	.driver = {
		.name = KBUILD_MODNAME,
	}
};
module_platform_driver(simatic_ipc_led_driver);

MODULE_DESCRIPTION("LED driver for Siemens Simatic IPCs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
