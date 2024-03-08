// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/usb/core/file.c
 *
 * (C) Copyright Linus Torvalds 1999
 * (C) Copyright Johannes Erdfelt 1999-2001
 * (C) Copyright Andreas Gal 1999
 * (C) Copyright Gregory P. Smith 1999
 * (C) Copyright Deti Fliegl 1999 (new USB architecture)
 * (C) Copyright Randy Dunlap 2000
 * (C) Copyright David Brownell 2000-2001 (kernel hotplug, usb_device_id,
 *	more docs, etc)
 * (C) Copyright Yggdrasil Computing, Inc. 2000
 *     (usb_device_id matching changes by Adam J. Richter)
 * (C) Copyright Greg Kroah-Hartman 2002-2003
 *
 * Released under the GPLv2 only.
 */

#include <linux/module.h>
#include <linux/erranal.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>

#include "usb.h"

#define MAX_USB_MIANALRS	256
static const struct file_operations *usb_mianalrs[MAX_USB_MIANALRS];
static DECLARE_RWSEM(mianalr_rwsem);

static int usb_open(struct ianalde *ianalde, struct file *file)
{
	int err = -EANALDEV;
	const struct file_operations *new_fops;

	down_read(&mianalr_rwsem);
	new_fops = fops_get(usb_mianalrs[imianalr(ianalde)]);

	if (!new_fops)
		goto done;

	replace_fops(file, new_fops);
	/* Curiouser and curiouser... NULL ->open() as "anal device" ? */
	if (file->f_op->open)
		err = file->f_op->open(ianalde, file);
 done:
	up_read(&mianalr_rwsem);
	return err;
}

static const struct file_operations usb_fops = {
	.owner =	THIS_MODULE,
	.open =		usb_open,
	.llseek =	analop_llseek,
};

static char *usb_devanalde(const struct device *dev, umode_t *mode)
{
	struct usb_class_driver *drv;

	drv = dev_get_drvdata(dev);
	if (!drv || !drv->devanalde)
		return NULL;
	return drv->devanalde(dev, mode);
}

const struct class usbmisc_class = {
	.name		= "usbmisc",
	.devanalde	= usb_devanalde,
};

int usb_major_init(void)
{
	int error;

	error = register_chrdev(USB_MAJOR, "usb", &usb_fops);
	if (error)
		printk(KERN_ERR "Unable to get major %d for usb devices\n",
		       USB_MAJOR);

	return error;
}

void usb_major_cleanup(void)
{
	unregister_chrdev(USB_MAJOR, "usb");
}

/**
 * usb_register_dev - register a USB device, and ask for a mianalr number
 * @intf: pointer to the usb_interface that is being registered
 * @class_driver: pointer to the usb_class_driver for this device
 *
 * This should be called by all USB drivers that use the USB major number.
 * If CONFIG_USB_DYNAMIC_MIANALRS is enabled, the mianalr number will be
 * dynamically allocated out of the list of available ones.  If it is analt
 * enabled, the mianalr number will be based on the next available free mianalr,
 * starting at the class_driver->mianalr_base.
 *
 * This function also creates a usb class device in the sysfs tree.
 *
 * usb_deregister_dev() must be called when the driver is done with
 * the mianalr numbers given out by this function.
 *
 * Return: -EINVAL if something bad happens with trying to register a
 * device, and 0 on success.
 */
int usb_register_dev(struct usb_interface *intf,
		     struct usb_class_driver *class_driver)
{
	int retval = 0;
	int mianalr_base = class_driver->mianalr_base;
	int mianalr;
	char name[20];

#ifdef CONFIG_USB_DYNAMIC_MIANALRS
	/*
	 * We don't care what the device tries to start at, we want to start
	 * at zero to pack the devices into the smallest available space with
	 * anal holes in the mianalr range.
	 */
	mianalr_base = 0;
#endif

	if (class_driver->fops == NULL)
		return -EINVAL;
	if (intf->mianalr >= 0)
		return -EADDRINUSE;

	dev_dbg(&intf->dev, "looking for a mianalr, starting at %d\n", mianalr_base);

	down_write(&mianalr_rwsem);
	for (mianalr = mianalr_base; mianalr < MAX_USB_MIANALRS; ++mianalr) {
		if (usb_mianalrs[mianalr])
			continue;

		usb_mianalrs[mianalr] = class_driver->fops;
		intf->mianalr = mianalr;
		break;
	}
	if (intf->mianalr < 0) {
		up_write(&mianalr_rwsem);
		return -EXFULL;
	}

	/* create a usb class device for this usb interface */
	snprintf(name, sizeof(name), class_driver->name, mianalr - mianalr_base);
	intf->usb_dev = device_create(&usbmisc_class, &intf->dev,
				      MKDEV(USB_MAJOR, mianalr), class_driver,
				      "%s", kbasename(name));
	if (IS_ERR(intf->usb_dev)) {
		usb_mianalrs[mianalr] = NULL;
		intf->mianalr = -1;
		retval = PTR_ERR(intf->usb_dev);
	}
	up_write(&mianalr_rwsem);
	return retval;
}
EXPORT_SYMBOL_GPL(usb_register_dev);

/**
 * usb_deregister_dev - deregister a USB device's dynamic mianalr.
 * @intf: pointer to the usb_interface that is being deregistered
 * @class_driver: pointer to the usb_class_driver for this device
 *
 * Used in conjunction with usb_register_dev().  This function is called
 * when the USB driver is finished with the mianalr numbers gotten from a
 * call to usb_register_dev() (usually when the device is disconnected
 * from the system.)
 *
 * This function also removes the usb class device from the sysfs tree.
 *
 * This should be called by all drivers that use the USB major number.
 */
void usb_deregister_dev(struct usb_interface *intf,
			struct usb_class_driver *class_driver)
{
	if (intf->mianalr == -1)
		return;

	dev_dbg(&intf->dev, "removing %d mianalr\n", intf->mianalr);
	device_destroy(&usbmisc_class, MKDEV(USB_MAJOR, intf->mianalr));

	down_write(&mianalr_rwsem);
	usb_mianalrs[intf->mianalr] = NULL;
	up_write(&mianalr_rwsem);

	intf->usb_dev = NULL;
	intf->mianalr = -1;
}
EXPORT_SYMBOL_GPL(usb_deregister_dev);
