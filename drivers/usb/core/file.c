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
 	more docs, etc)
 * (C) Copyright Yggdrasil Computing, Inc. 2000
 *     (usb_device_id matching changes by Adam J. Richter)
 * (C) Copyright Greg Kroah-Hartman 2002-2003
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "usb.h"

#define MAX_USB_MINORS	256
static const struct file_operations *usb_minors[MAX_USB_MINORS];
static DECLARE_RWSEM(minor_rwsem);

static int usb_open(struct inode * inode, struct file * file)
{
	int minor = iminor(inode);
	const struct file_operations *c;
	int err = -ENODEV;
	const struct file_operations *old_fops, *new_fops = NULL;

	down_read(&minor_rwsem);
	c = usb_minors[minor];

	if (!c || !(new_fops = fops_get(c)))
		goto done;

	old_fops = file->f_op;
	file->f_op = new_fops;
	/* Curiouser and curiouser... NULL ->open() as "no device" ? */
	if (file->f_op->open)
		err = file->f_op->open(inode,file);
	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
 done:
	up_read(&minor_rwsem);
	return err;
}

static const struct file_operations usb_fops = {
	.owner =	THIS_MODULE,
	.open =		usb_open,
	.llseek =	noop_llseek,
};

static struct usb_class {
	struct kref kref;
	struct class *class;
} *usb_class;

static char *usb_devnode(struct device *dev, mode_t *mode)
{
	struct usb_class_driver *drv;

	drv = dev_get_drvdata(dev);
	if (!drv || !drv->devnode)
		return NULL;
	return drv->devnode(dev, mode);
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
	usb_class->class = class_create(THIS_MODULE, "usb");
	if (IS_ERR(usb_class->class)) {
		result = IS_ERR(usb_class->class);
		printk(KERN_ERR "class_create failed for usb devices\n");
		kfree(usb_class);
		usb_class = NULL;
		goto exit;
	}
	usb_class->class->devnode = usb_devnode;

exit:
	return result;
}

static void release_usb_class(struct kref *kref)
{
	/* Ok, we cheat as we know we only have one usb_class */
	class_destroy(usb_class->class);
	kfree(usb_class);
	usb_class = NULL;
}

static void destroy_usb_class(void)
{
	if (usb_class)
		kref_put(&usb_class->kref, release_usb_class);
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
 * usb_register_dev - register a USB device, and ask for a minor number
 * @intf: pointer to the usb_interface that is being registered
 * @class_driver: pointer to the usb_class_driver for this device
 *
 * This should be called by all USB drivers that use the USB major number.
 * If CONFIG_USB_DYNAMIC_MINORS is enabled, the minor number will be
 * dynamically allocated out of the list of available ones.  If it is not
 * enabled, the minor number will be based on the next available free minor,
 * starting at the class_driver->minor_base.
 *
 * This function also creates a usb class device in the sysfs tree.
 *
 * usb_deregister_dev() must be called when the driver is done with
 * the minor numbers given out by this function.
 *
 * Returns -EINVAL if something bad happens with trying to register a
 * device, and 0 on success.
 */
int usb_register_dev(struct usb_interface *intf,
		     struct usb_class_driver *class_driver)
{
	int retval;
	int minor_base = class_driver->minor_base;
	int minor;
	char name[20];
	char *temp;

#ifdef CONFIG_USB_DYNAMIC_MINORS
	/* 
	 * We don't care what the device tries to start at, we want to start
	 * at zero to pack the devices into the smallest available space with
	 * no holes in the minor range.
	 */
	minor_base = 0;
#endif

	if (class_driver->fops == NULL)
		return -EINVAL;
	if (intf->minor >= 0)
		return -EADDRINUSE;

	retval = init_usb_class();
	if (retval)
		return retval;

	dev_dbg(&intf->dev, "looking for a minor, starting at %d", minor_base);

	down_write(&minor_rwsem);
	for (minor = minor_base; minor < MAX_USB_MINORS; ++minor) {
		if (usb_minors[minor])
			continue;

		usb_minors[minor] = class_driver->fops;
		intf->minor = minor;
		break;
	}
	up_write(&minor_rwsem);
	if (intf->minor < 0)
		return -EXFULL;

	/* create a usb class device for this usb interface */
	snprintf(name, sizeof(name), class_driver->name, minor - minor_base);
	temp = strrchr(name, '/');
	if (temp && (temp[1] != '\0'))
		++temp;
	else
		temp = name;
	intf->usb_dev = device_create(usb_class->class, &intf->dev,
				      MKDEV(USB_MAJOR, minor), class_driver,
				      "%s", temp);
	if (IS_ERR(intf->usb_dev)) {
		down_write(&minor_rwsem);
		usb_minors[minor] = NULL;
		intf->minor = -1;
		up_write(&minor_rwsem);
		retval = PTR_ERR(intf->usb_dev);
	}
	return retval;
}
EXPORT_SYMBOL_GPL(usb_register_dev);

/**
 * usb_deregister_dev - deregister a USB device's dynamic minor.
 * @intf: pointer to the usb_interface that is being deregistered
 * @class_driver: pointer to the usb_class_driver for this device
 *
 * Used in conjunction with usb_register_dev().  This function is called
 * when the USB driver is finished with the minor numbers gotten from a
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
	if (intf->minor == -1)
		return;

	dbg ("removing %d minor", intf->minor);

	down_write(&minor_rwsem);
	usb_minors[intf->minor] = NULL;
	up_write(&minor_rwsem);

	device_destroy(usb_class->class, MKDEV(USB_MAJOR, intf->minor));
	intf->usb_dev = NULL;
	intf->minor = -1;
	destroy_usb_class();
}
EXPORT_SYMBOL_GPL(usb_deregister_dev);
