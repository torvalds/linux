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
#include <linux/erryes.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>

#include "usb.h"

#define MAX_USB_MINORS	256
static const struct file_operations *usb_miyesrs[MAX_USB_MINORS];
static DECLARE_RWSEM(miyesr_rwsem);
static DEFINE_MUTEX(init_usb_class_mutex);

static int usb_open(struct iyesde *iyesde, struct file *file)
{
	int err = -ENODEV;
	const struct file_operations *new_fops;

	down_read(&miyesr_rwsem);
	new_fops = fops_get(usb_miyesrs[imiyesr(iyesde)]);

	if (!new_fops)
		goto done;

	replace_fops(file, new_fops);
	/* Curiouser and curiouser... NULL ->open() as "yes device" ? */
	if (file->f_op->open)
		err = file->f_op->open(iyesde, file);
 done:
	up_read(&miyesr_rwsem);
	return err;
}

static const struct file_operations usb_fops = {
	.owner =	THIS_MODULE,
	.open =		usb_open,
	.llseek =	yesop_llseek,
};

static struct usb_class {
	struct kref kref;
	struct class *class;
} *usb_class;

static char *usb_devyesde(struct device *dev, umode_t *mode)
{
	struct usb_class_driver *drv;

	drv = dev_get_drvdata(dev);
	if (!drv || !drv->devyesde)
		return NULL;
	return drv->devyesde(dev, mode);
}

static int init_usb_class(void)
{
	int result = 0;

	if (usb_class != NULL) {
		kref_get(&usb_class->kref);
		goto exit;
	}

	usb_class = kmalloc(sizeof(*usb_class), GFP_KERNEL);
	if (!usb_class) {
		result = -ENOMEM;
		goto exit;
	}

	kref_init(&usb_class->kref);
	usb_class->class = class_create(THIS_MODULE, "usbmisc");
	if (IS_ERR(usb_class->class)) {
		result = PTR_ERR(usb_class->class);
		printk(KERN_ERR "class_create failed for usb devices\n");
		kfree(usb_class);
		usb_class = NULL;
		goto exit;
	}
	usb_class->class->devyesde = usb_devyesde;

exit:
	return result;
}

static void release_usb_class(struct kref *kref)
{
	/* Ok, we cheat as we kyesw we only have one usb_class */
	class_destroy(usb_class->class);
	kfree(usb_class);
	usb_class = NULL;
}

static void destroy_usb_class(void)
{
	mutex_lock(&init_usb_class_mutex);
	kref_put(&usb_class->kref, release_usb_class);
	mutex_unlock(&init_usb_class_mutex);
}

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
 * usb_register_dev - register a USB device, and ask for a miyesr number
 * @intf: pointer to the usb_interface that is being registered
 * @class_driver: pointer to the usb_class_driver for this device
 *
 * This should be called by all USB drivers that use the USB major number.
 * If CONFIG_USB_DYNAMIC_MINORS is enabled, the miyesr number will be
 * dynamically allocated out of the list of available ones.  If it is yest
 * enabled, the miyesr number will be based on the next available free miyesr,
 * starting at the class_driver->miyesr_base.
 *
 * This function also creates a usb class device in the sysfs tree.
 *
 * usb_deregister_dev() must be called when the driver is done with
 * the miyesr numbers given out by this function.
 *
 * Return: -EINVAL if something bad happens with trying to register a
 * device, and 0 on success.
 */
int usb_register_dev(struct usb_interface *intf,
		     struct usb_class_driver *class_driver)
{
	int retval;
	int miyesr_base = class_driver->miyesr_base;
	int miyesr;
	char name[20];

#ifdef CONFIG_USB_DYNAMIC_MINORS
	/*
	 * We don't care what the device tries to start at, we want to start
	 * at zero to pack the devices into the smallest available space with
	 * yes holes in the miyesr range.
	 */
	miyesr_base = 0;
#endif

	if (class_driver->fops == NULL)
		return -EINVAL;
	if (intf->miyesr >= 0)
		return -EADDRINUSE;

	mutex_lock(&init_usb_class_mutex);
	retval = init_usb_class();
	mutex_unlock(&init_usb_class_mutex);

	if (retval)
		return retval;

	dev_dbg(&intf->dev, "looking for a miyesr, starting at %d\n", miyesr_base);

	down_write(&miyesr_rwsem);
	for (miyesr = miyesr_base; miyesr < MAX_USB_MINORS; ++miyesr) {
		if (usb_miyesrs[miyesr])
			continue;

		usb_miyesrs[miyesr] = class_driver->fops;
		intf->miyesr = miyesr;
		break;
	}
	if (intf->miyesr < 0) {
		up_write(&miyesr_rwsem);
		return -EXFULL;
	}

	/* create a usb class device for this usb interface */
	snprintf(name, sizeof(name), class_driver->name, miyesr - miyesr_base);
	intf->usb_dev = device_create(usb_class->class, &intf->dev,
				      MKDEV(USB_MAJOR, miyesr), class_driver,
				      "%s", kbasename(name));
	if (IS_ERR(intf->usb_dev)) {
		usb_miyesrs[miyesr] = NULL;
		intf->miyesr = -1;
		retval = PTR_ERR(intf->usb_dev);
	}
	up_write(&miyesr_rwsem);
	return retval;
}
EXPORT_SYMBOL_GPL(usb_register_dev);

/**
 * usb_deregister_dev - deregister a USB device's dynamic miyesr.
 * @intf: pointer to the usb_interface that is being deregistered
 * @class_driver: pointer to the usb_class_driver for this device
 *
 * Used in conjunction with usb_register_dev().  This function is called
 * when the USB driver is finished with the miyesr numbers gotten from a
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
	if (intf->miyesr == -1)
		return;

	dev_dbg(&intf->dev, "removing %d miyesr\n", intf->miyesr);
	device_destroy(usb_class->class, MKDEV(USB_MAJOR, intf->miyesr));

	down_write(&miyesr_rwsem);
	usb_miyesrs[intf->miyesr] = NULL;
	up_write(&miyesr_rwsem);

	intf->usb_dev = NULL;
	intf->miyesr = -1;
	destroy_usb_class();
}
EXPORT_SYMBOL_GPL(usb_deregister_dev);
