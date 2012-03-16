/*
 *  Backlight Driver for Intel-based Apples
 *
 *  Copyright (c) Red Hat <mjg@redhat.com>
 *  Based on code from Pommed:
 *  Copyright (C) 2006 Nicolas Boichat <nicolas @boichat.ch>
 *  Copyright (C) 2006 Felipe Alfaro Solana <felipe_alfaro @linuxmail.org>
 *  Copyright (C) 2007 Julien BLACHE <jb@jblache.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This driver triggers SMIs which cause the firmware to change the
 *  backlight brightness. This is icky in many ways, but it's impractical to
 *  get at the firmware code in order to figure out what it's actually doing.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/atomic.h>

static struct backlight_device *apple_backlight_device;

struct hw_data {
	/* I/O resource to allocate. */
	unsigned long iostart;
	unsigned long iolen;
	/* Backlight operations structure. */
	const struct backlight_ops backlight_ops;
	void (*set_brightness)(int);
};

static const struct hw_data *hw_data;

#define DRIVER "apple_backlight: "

/* Module parameters. */
static int debug;
module_param_named(debug, debug, int, 0644);
MODULE_PARM_DESC(debug, "Set to one to enable debugging messages.");

/*
 * Implementation for machines with Intel chipset.
 */
static void intel_chipset_set_brightness(int intensity)
{
	outb(0x04 | (intensity << 4), 0xb3);
	outb(0xbf, 0xb2);
}

static int intel_chipset_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;

	if (debug)
		printk(KERN_DEBUG DRIVER "setting brightness to %d\n",
		       intensity);

	intel_chipset_set_brightness(intensity);
	return 0;
}

static int intel_chipset_get_intensity(struct backlight_device *bd)
{
	int intensity;

	outb(0x03, 0xb3);
	outb(0xbf, 0xb2);
	intensity = inb(0xb3) >> 4;

	if (debug)
		printk(KERN_DEBUG DRIVER "read brightness of %d\n",
		       intensity);

	return intensity;
}

static const struct hw_data intel_chipset_data = {
	.iostart = 0xb2,
	.iolen = 2,
	.backlight_ops	= {
		.options	= BL_CORE_SUSPENDRESUME,
		.get_brightness	= intel_chipset_get_intensity,
		.update_status	= intel_chipset_send_intensity,
	},
	.set_brightness = intel_chipset_set_brightness,
};

/*
 * Implementation for machines with Nvidia chipset.
 */
static void nvidia_chipset_set_brightness(int intensity)
{
	outb(0x04 | (intensity << 4), 0x52f);
	outb(0xbf, 0x52e);
}

static int nvidia_chipset_send_intensity(struct backlight_device *bd)
{
	int intensity = bd->props.brightness;

	if (debug)
		printk(KERN_DEBUG DRIVER "setting brightness to %d\n",
		       intensity);

	nvidia_chipset_set_brightness(intensity);
	return 0;
}

static int nvidia_chipset_get_intensity(struct backlight_device *bd)
{
	int intensity;

	outb(0x03, 0x52f);
	outb(0xbf, 0x52e);
	intensity = inb(0x52f) >> 4;

	if (debug)
		printk(KERN_DEBUG DRIVER "read brightness of %d\n",
		       intensity);

	return intensity;
}

static const struct hw_data nvidia_chipset_data = {
	.iostart = 0x52e,
	.iolen = 2,
	.backlight_ops		= {
		.options	= BL_CORE_SUSPENDRESUME,
		.get_brightness	= nvidia_chipset_get_intensity,
		.update_status	= nvidia_chipset_send_intensity
	},
	.set_brightness = nvidia_chipset_set_brightness,
};

static int __devinit apple_bl_add(struct acpi_device *dev)
{
	struct backlight_properties props;
	struct pci_dev *host;
	int intensity;

	host = pci_get_bus_and_slot(0, 0);

	if (!host) {
		printk(KERN_ERR DRIVER "unable to find PCI host\n");
		return -ENODEV;
	}

	if (host->vendor == PCI_VENDOR_ID_INTEL)
		hw_data = &intel_chipset_data;
	else if (host->vendor == PCI_VENDOR_ID_NVIDIA)
		hw_data = &nvidia_chipset_data;

	pci_dev_put(host);

	if (!hw_data) {
		printk(KERN_ERR DRIVER "unknown hardware\n");
		return -ENODEV;
	}

	/* Check that the hardware responds - this may not work under EFI */

	intensity = hw_data->backlight_ops.get_brightness(NULL);

	if (!intensity) {
		hw_data->set_brightness(1);
		if (!hw_data->backlight_ops.get_brightness(NULL))
			return -ENODEV;

		hw_data->set_brightness(0);
	}

	if (!request_region(hw_data->iostart, hw_data->iolen,
			    "Apple backlight"))
		return -ENXIO;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = 15;
	apple_backlight_device = backlight_device_register("apple_backlight",
				  NULL, NULL, &hw_data->backlight_ops, &props);

	if (IS_ERR(apple_backlight_device)) {
		release_region(hw_data->iostart, hw_data->iolen);
		return PTR_ERR(apple_backlight_device);
	}

	apple_backlight_device->props.brightness =
		hw_data->backlight_ops.get_brightness(apple_backlight_device);
	backlight_update_status(apple_backlight_device);

	return 0;
}

static int __devexit apple_bl_remove(struct acpi_device *dev, int type)
{
	backlight_device_unregister(apple_backlight_device);

	release_region(hw_data->iostart, hw_data->iolen);
	hw_data = NULL;
	return 0;
}

static const struct acpi_device_id apple_bl_ids[] = {
	{"APP0002", 0},
	{"", 0},
};

static struct acpi_driver apple_bl_driver = {
	.name = "Apple backlight",
	.ids = apple_bl_ids,
	.ops = {
		.add = apple_bl_add,
		.remove = apple_bl_remove,
	},
};

static atomic_t apple_bl_registered = ATOMIC_INIT(0);

int apple_bl_register(void)
{
	if (atomic_xchg(&apple_bl_registered, 1) == 0)
		return acpi_bus_register_driver(&apple_bl_driver);

	return 0;
}
EXPORT_SYMBOL_GPL(apple_bl_register);

void apple_bl_unregister(void)
{
	if (atomic_xchg(&apple_bl_registered, 0) == 1)
		acpi_bus_unregister_driver(&apple_bl_driver);
}
EXPORT_SYMBOL_GPL(apple_bl_unregister);

static int __init apple_bl_init(void)
{
	return apple_bl_register();
}

static void __exit apple_bl_exit(void)
{
	apple_bl_unregister();
}

module_init(apple_bl_init);
module_exit(apple_bl_exit);

MODULE_AUTHOR("Matthew Garrett <mjg@redhat.com>");
MODULE_DESCRIPTION("Apple Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(acpi, apple_bl_ids);
MODULE_ALIAS("mbp_nvidia_bl");
