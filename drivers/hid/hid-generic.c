/*
 *  HID support for Linux
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2012 Jiri Kosina
 *  Copyright (c) 2012 Henrik Rydberg
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <asm/unaligned.h>
#include <asm/byteorder.h>

#include <linux/hid.h>

static struct hid_driver hid_generic;

static int __unmap_hid_generic(struct device *dev, void *data)
{
	struct hid_driver *hdrv = data;
	struct hid_device *hdev = to_hid_device(dev);

	/* only unbind matching devices already bound to hid-generic */
	if (hdev->driver != &hid_generic ||
	    hid_match_device(hdev, hdrv) == NULL)
		return 0;

	if (dev->parent)	/* Needed for USB */
		device_lock(dev->parent);
	device_release_driver(dev);
	if (dev->parent)
		device_unlock(dev->parent);

	return 0;
}

static void hid_generic_add_driver(struct hid_driver *hdrv)
{
	bus_for_each_dev(&hid_bus_type, NULL, hdrv, __unmap_hid_generic);
}

static void hid_generic_removed_driver(struct hid_driver *hdrv)
{
	int ret;

	ret = driver_attach(&hid_generic.driver);
}

static int __check_hid_generic(struct device_driver *drv, void *data)
{
	struct hid_driver *hdrv = to_hid_driver(drv);
	struct hid_device *hdev = data;

	if (hdrv == &hid_generic)
		return 0;

	return hid_match_device(hdev, hdrv) != NULL;
}

static bool hid_generic_match(struct hid_device *hdev,
			      bool ignore_special_driver)
{
	if (ignore_special_driver)
		return true;

	if (hdev->quirks & HID_QUIRK_HAVE_SPECIAL_DRIVER)
		return false;

	/*
	 * If any other driver wants the device, leave the device to this other
	 * driver.
	 */
	if (bus_for_each_drv(&hid_bus_type, NULL, hdev, __check_hid_generic))
		return false;

	return true;
}

static const struct hid_device_id hid_table[] = {
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY, HID_ANY_ID, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, hid_table);

static struct hid_driver hid_generic = {
	.name = "hid-generic",
	.id_table = hid_table,
	.match = hid_generic_match,
	.bus_add_driver = hid_generic_add_driver,
	.bus_removed_driver = hid_generic_removed_driver,
};
module_hid_driver(hid_generic);

MODULE_AUTHOR("Henrik Rydberg");
MODULE_DESCRIPTION("HID generic driver");
MODULE_LICENSE("GPL");
