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
	unsigned int value; /* mask for io and offset for mem */
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

/* the actual start will be discovered with PCI, 0 is a placeholder */
static struct resource simatic_ipc_led_mem_res = DEFINE_RES_MEM_NAMED(0, SZ_4K, KBUILD_MODNAME);

static void *simatic_ipc_led_memory;

static struct simatic_ipc_led simatic_ipc_leds_mem[] = {
	{0x500 + 0x1A0, "red:" LED_FUNCTION_STATUS "-1"},
	{0x500 + 0x1A8, "green:" LED_FUNCTION_STATUS "-1"},
	{0x500 + 0x1C8, "red:" LED_FUNCTION_STATUS "-2"},
	{0x500 + 0x1D0, "green:" LED_FUNCTION_STATUS "-2"},
	{0x500 + 0x1E0, "red:" LED_FUNCTION_STATUS "-3"},
	{0x500 + 0x198, "green:" LED_FUNCTION_STATUS "-3"},
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

static void simatic_ipc_led_set_mem(struct led_classdev *led_cd,
				    enum led_brightness brightness)
{
	struct simatic_ipc_led *led = cdev_to_led(led_cd);

	u32 *p;

	p = simatic_ipc_led_memory + led->value;
	*p = (*p & ~1) | (brightness == LED_OFF);
}

static enum led_brightness simatic_ipc_led_get_mem(struct led_classdev *led_cd)
{
	struct simatic_ipc_led *led = cdev_to_led(led_cd);

	u32 *p;

	p = simatic_ipc_led_memory + led->value;
	return (*p & 1) ? LED_OFF : led_cd->max_brightness;
}

static int simatic_ipc_leds_probe(struct platform_device *pdev)
{
	const struct simatic_ipc_platform *plat = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct simatic_ipc_led *ipcled;
	struct led_classdev *cdev;
	struct resource *res;
	int err, type;
	u32 *p;

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
		type = IORESOURCE_IO;
		if (!devm_request_region(dev, res->start, resource_size(res), KBUILD_MODNAME)) {
			dev_err(dev, "Unable to register IO resource at %pR\n", res);
			return -EBUSY;
		}
		break;
	case SIMATIC_IPC_DEVICE_127E:
		res = &simatic_ipc_led_mem_res;
		ipcled = simatic_ipc_leds_mem;
		type = IORESOURCE_MEM;

		/* get GPIO base from PCI */
		res->start = simatic_ipc_get_membase0(PCI_DEVFN(13, 0));
		if (res->start == 0)
			return -ENODEV;

		/* do the final address calculation */
		res->start = res->start + (0xC5 << 16);
		res->end += res->start;

		simatic_ipc_led_memory = devm_ioremap_resource(dev, res);
		if (IS_ERR(simatic_ipc_led_memory))
			return PTR_ERR(simatic_ipc_led_memory);

		/* initialize power/watchdog LED */
		p = simatic_ipc_led_memory + 0x500 + 0x1D8; /* PM_WDT_OUT */
		*p = (*p & ~1);
		p = simatic_ipc_led_memory + 0x500 + 0x1C0; /* PM_BIOS_BOOT_N */
		*p = (*p | 1);

		break;
	default:
		return -ENODEV;
	}

	while (ipcled->value) {
		cdev = &ipcled->cdev;
		if (type == IORESOURCE_MEM) {
			cdev->brightness_set = simatic_ipc_led_set_mem;
			cdev->brightness_get = simatic_ipc_led_get_mem;
		} else {
			cdev->brightness_set = simatic_ipc_led_set_io;
			cdev->brightness_get = simatic_ipc_led_get_io;
		}
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

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
