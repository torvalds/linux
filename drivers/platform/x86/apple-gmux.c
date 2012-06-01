/*
 *  Gmux driver for Apple laptops
 *
 *  Copyright (C) Canonical Ltd. <seth.forshee@canonical.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/backlight.h>
#include <linux/acpi.h>
#include <linux/pnp.h>
#include <linux/apple_bl.h>
#include <linux/slab.h>
#include <acpi/video.h>
#include <asm/io.h>

struct apple_gmux_data {
	unsigned long iostart;
	unsigned long iolen;

	struct backlight_device *bdev;
};

/*
 * gmux port offsets. Many of these are not yet used, but may be in the
 * future, and it's useful to have them documented here anyhow.
 */
#define GMUX_PORT_VERSION_MAJOR		0x04
#define GMUX_PORT_VERSION_MINOR		0x05
#define GMUX_PORT_VERSION_RELEASE	0x06
#define GMUX_PORT_SWITCH_DISPLAY	0x10
#define GMUX_PORT_SWITCH_GET_DISPLAY	0x11
#define GMUX_PORT_INTERRUPT_ENABLE	0x14
#define GMUX_PORT_INTERRUPT_STATUS	0x16
#define GMUX_PORT_SWITCH_DDC		0x28
#define GMUX_PORT_SWITCH_EXTERNAL	0x40
#define GMUX_PORT_SWITCH_GET_EXTERNAL	0x41
#define GMUX_PORT_DISCRETE_POWER	0x50
#define GMUX_PORT_MAX_BRIGHTNESS	0x70
#define GMUX_PORT_BRIGHTNESS		0x74

#define GMUX_MIN_IO_LEN			(GMUX_PORT_BRIGHTNESS + 4)

#define GMUX_INTERRUPT_ENABLE		0xff
#define GMUX_INTERRUPT_DISABLE		0x00

#define GMUX_INTERRUPT_STATUS_ACTIVE	0
#define GMUX_INTERRUPT_STATUS_DISPLAY	(1 << 0)
#define GMUX_INTERRUPT_STATUS_POWER	(1 << 2)
#define GMUX_INTERRUPT_STATUS_HOTPLUG	(1 << 3)

#define GMUX_BRIGHTNESS_MASK		0x00ffffff
#define GMUX_MAX_BRIGHTNESS		GMUX_BRIGHTNESS_MASK

static inline u8 gmux_read8(struct apple_gmux_data *gmux_data, int port)
{
	return inb(gmux_data->iostart + port);
}

static inline void gmux_write8(struct apple_gmux_data *gmux_data, int port,
			       u8 val)
{
	outb(val, gmux_data->iostart + port);
}

static inline u32 gmux_read32(struct apple_gmux_data *gmux_data, int port)
{
	return inl(gmux_data->iostart + port);
}

static int gmux_get_brightness(struct backlight_device *bd)
{
	struct apple_gmux_data *gmux_data = bl_get_data(bd);
	return gmux_read32(gmux_data, GMUX_PORT_BRIGHTNESS) &
	       GMUX_BRIGHTNESS_MASK;
}

static int gmux_update_status(struct backlight_device *bd)
{
	struct apple_gmux_data *gmux_data = bl_get_data(bd);
	u32 brightness = bd->props.brightness;

	if (bd->props.state & BL_CORE_SUSPENDED)
		return 0;

	/*
	 * Older gmux versions require writing out lower bytes first then
	 * setting the upper byte to 0 to flush the values. Newer versions
	 * accept a single u32 write, but the old method also works, so we
	 * just use the old method for all gmux versions.
	 */
	gmux_write8(gmux_data, GMUX_PORT_BRIGHTNESS, brightness);
	gmux_write8(gmux_data, GMUX_PORT_BRIGHTNESS + 1, brightness >> 8);
	gmux_write8(gmux_data, GMUX_PORT_BRIGHTNESS + 2, brightness >> 16);
	gmux_write8(gmux_data, GMUX_PORT_BRIGHTNESS + 3, 0);

	return 0;
}

static const struct backlight_ops gmux_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = gmux_get_brightness,
	.update_status = gmux_update_status,
};

static int __devinit gmux_probe(struct pnp_dev *pnp,
				const struct pnp_device_id *id)
{
	struct apple_gmux_data *gmux_data;
	struct resource *res;
	struct backlight_properties props;
	struct backlight_device *bdev;
	u8 ver_major, ver_minor, ver_release;
	int ret = -ENXIO;

	gmux_data = kzalloc(sizeof(*gmux_data), GFP_KERNEL);
	if (!gmux_data)
		return -ENOMEM;
	pnp_set_drvdata(pnp, gmux_data);

	res = pnp_get_resource(pnp, IORESOURCE_IO, 0);
	if (!res) {
		pr_err("Failed to find gmux I/O resource\n");
		goto err_free;
	}

	gmux_data->iostart = res->start;
	gmux_data->iolen = res->end - res->start;

	if (gmux_data->iolen < GMUX_MIN_IO_LEN) {
		pr_err("gmux I/O region too small (%lu < %u)\n",
		       gmux_data->iolen, GMUX_MIN_IO_LEN);
		goto err_free;
	}

	if (!request_region(gmux_data->iostart, gmux_data->iolen,
			    "Apple gmux")) {
		pr_err("gmux I/O already in use\n");
		goto err_free;
	}

	/*
	 * On some machines the gmux is in ACPI even thought the machine
	 * doesn't really have a gmux. Check for invalid version information
	 * to detect this.
	 */
	ver_major = gmux_read8(gmux_data, GMUX_PORT_VERSION_MAJOR);
	ver_minor = gmux_read8(gmux_data, GMUX_PORT_VERSION_MINOR);
	ver_release = gmux_read8(gmux_data, GMUX_PORT_VERSION_RELEASE);
	if (ver_major == 0xff && ver_minor == 0xff && ver_release == 0xff) {
		pr_info("gmux device not present\n");
		ret = -ENODEV;
		goto err_release;
	}

	pr_info("Found gmux version %d.%d.%d\n", ver_major, ver_minor,
		ver_release);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = gmux_read32(gmux_data, GMUX_PORT_MAX_BRIGHTNESS);

	/*
	 * Currently it's assumed that the maximum brightness is less than
	 * 2^24 for compatibility with old gmux versions. Cap the max
	 * brightness at this value, but print a warning if the hardware
	 * reports something higher so that it can be fixed.
	 */
	if (WARN_ON(props.max_brightness > GMUX_MAX_BRIGHTNESS))
		props.max_brightness = GMUX_MAX_BRIGHTNESS;

	bdev = backlight_device_register("gmux_backlight", &pnp->dev,
					 gmux_data, &gmux_bl_ops, &props);
	if (IS_ERR(bdev)) {
		ret = PTR_ERR(bdev);
		goto err_release;
	}

	gmux_data->bdev = bdev;
	bdev->props.brightness = gmux_get_brightness(bdev);
	backlight_update_status(bdev);

	/*
	 * The backlight situation on Macs is complicated. If the gmux is
	 * present it's the best choice, because it always works for
	 * backlight control and supports more levels than other options.
	 * Disable the other backlight choices.
	 */
	acpi_video_unregister();
	apple_bl_unregister();

	return 0;

err_release:
	release_region(gmux_data->iostart, gmux_data->iolen);
err_free:
	kfree(gmux_data);
	return ret;
}

static void __devexit gmux_remove(struct pnp_dev *pnp)
{
	struct apple_gmux_data *gmux_data = pnp_get_drvdata(pnp);

	backlight_device_unregister(gmux_data->bdev);
	release_region(gmux_data->iostart, gmux_data->iolen);
	kfree(gmux_data);

	acpi_video_register();
	apple_bl_register();
}

static const struct pnp_device_id gmux_device_ids[] = {
	{"APP000B", 0},
	{"", 0}
};

static struct pnp_driver gmux_pnp_driver = {
	.name		= "apple-gmux",
	.probe		= gmux_probe,
	.remove		= __devexit_p(gmux_remove),
	.id_table	= gmux_device_ids,
};

static int __init apple_gmux_init(void)
{
	return pnp_register_driver(&gmux_pnp_driver);
}

static void __exit apple_gmux_exit(void)
{
	pnp_unregister_driver(&gmux_pnp_driver);
}

module_init(apple_gmux_init);
module_exit(apple_gmux_exit);

MODULE_AUTHOR("Seth Forshee <seth.forshee@canonical.com>");
MODULE_DESCRIPTION("Apple Gmux Driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pnp, gmux_device_ids);
