/*
    hwmon.c - part of lm_sensors, Linux kernel modules for hardware monitoring

    This file defines the sysfs class "hwmon", for use by sensors drivers.

    Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/hwmon.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

static struct class *hwmon_class;

static DEFINE_IDR(hwmon_idr);
static DEFINE_SPINLOCK(idr_lock);

/**
 * hwmon_device_register - register w/ hwmon
 * @dev: the device to register
 *
 * hwmon_device_unregister() must be called when the device is no
 * longer needed.
 *
 * Returns the pointer to the new device.
 */
struct device *hwmon_device_register(struct device *dev)
{
	struct device *hwdev;
	int id, err;

again:
	if (unlikely(idr_pre_get(&hwmon_idr, GFP_KERNEL) == 0))
		return ERR_PTR(-ENOMEM);

	spin_lock(&idr_lock);
	err = idr_get_new(&hwmon_idr, NULL, &id);
	spin_unlock(&idr_lock);

	if (unlikely(err == -EAGAIN))
		goto again;
	else if (unlikely(err))
		return ERR_PTR(err);

	id = id & MAX_ID_MASK;
	hwdev = device_create(hwmon_class, dev, MKDEV(0, 0), NULL,
			      HWMON_ID_FORMAT, id);

	if (IS_ERR(hwdev)) {
		spin_lock(&idr_lock);
		idr_remove(&hwmon_idr, id);
		spin_unlock(&idr_lock);
	}

	return hwdev;
}

/**
 * hwmon_device_unregister - removes the previously registered class device
 *
 * @dev: the class device to destroy
 */
void hwmon_device_unregister(struct device *dev)
{
	int id;

	if (likely(sscanf(dev_name(dev), HWMON_ID_FORMAT, &id) == 1)) {
		device_unregister(dev);
		spin_lock(&idr_lock);
		idr_remove(&hwmon_idr, id);
		spin_unlock(&idr_lock);
	} else
		dev_dbg(dev->parent,
			"hwmon_device_unregister() failed: bad class ID!\n");
}

static void __init hwmon_pci_quirks(void)
{
#if defined CONFIG_X86 && defined CONFIG_PCI
	struct pci_dev *sb;
	u16 base;
	u8 enable;

	/* Open access to 0x295-0x296 on MSI MS-7031 */
	sb = pci_get_device(PCI_VENDOR_ID_ATI, 0x436c, NULL);
	if (sb &&
	    (sb->subsystem_vendor == 0x1462 &&	/* MSI */
	     sb->subsystem_device == 0x0031)) {	/* MS-7031 */

		pci_read_config_byte(sb, 0x48, &enable);
		pci_read_config_word(sb, 0x64, &base);

		if (base == 0 && !(enable & BIT(2))) {
			dev_info(&sb->dev,
				 "Opening wide generic port at 0x295\n");
			pci_write_config_word(sb, 0x64, 0x295);
			pci_write_config_byte(sb, 0x48, enable | BIT(2));
		}
	}
#endif
}

static int __init hwmon_init(void)
{
	hwmon_pci_quirks();

	hwmon_class = class_create(THIS_MODULE, "hwmon");
	if (IS_ERR(hwmon_class)) {
		printk(KERN_ERR "hwmon.c: couldn't create sysfs class\n");
		return PTR_ERR(hwmon_class);
	}
	return 0;
}

static void __exit hwmon_exit(void)
{
	class_destroy(hwmon_class);
}

subsys_initcall(hwmon_init);
module_exit(hwmon_exit);

EXPORT_SYMBOL_GPL(hwmon_device_register);
EXPORT_SYMBOL_GPL(hwmon_device_unregister);

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("hardware monitoring sysfs/class support");
MODULE_LICENSE("GPL");

