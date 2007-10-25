/* -*- linux-c -*-
 * Cypress USB Thermometer driver 
 * 
 * Copyright (c) 2004 Erik Rigtorp <erkki@linux.nu> <erik@rigtorp.com>
 * 
 * This driver works with Elektor magazine USB Interface as published in 
 * issue #291. It should also work with the original starter kit/demo board
 * from Cypress.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>

#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Erik Rigtorp"
#define DRIVER_DESC "Cypress USB Thermometer driver"

#define USB_SKEL_VENDOR_ID	0x04b4
#define USB_SKEL_PRODUCT_ID	0x0002

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(USB_SKEL_VENDOR_ID, USB_SKEL_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE (usb, id_table);

/* Structure to hold all of our device specific stuff */
struct usb_cytherm {
	struct usb_device    *udev;	 /* save off the usb device pointer */
	struct usb_interface *interface; /* the interface for this device */
	int brightness;
};


/* local function prototypes */
static int cytherm_probe(struct usb_interface *interface, 
			 const struct usb_device_id *id);
static void cytherm_disconnect(struct usb_interface *interface);


/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver cytherm_driver = {
	.name =		"cytherm",
	.probe =	cytherm_probe,
	.disconnect =	cytherm_disconnect,
	.id_table =	id_table,
};

/* Vendor requests */
/* They all operate on one byte at a time */
#define PING       0x00
#define READ_ROM   0x01 /* Reads form ROM, value = address */
#define READ_RAM   0x02 /* Reads form RAM, value = address */
#define WRITE_RAM  0x03 /* Write to RAM, value = address, index = data */
#define READ_PORT  0x04 /* Reads from port, value = address */
#define WRITE_PORT 0x05 /* Write to port, value = address, index = data */ 


/* Send a vendor command to device */
static int vendor_command(struct usb_device *dev, unsigned char request, 
			  unsigned char value, unsigned char index,
			  void *buf, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       request, 
			       USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			       value, 
			       index, buf, size,
			       USB_CTRL_GET_TIMEOUT);
}



#define BRIGHTNESS 0x2c     /* RAM location for brightness value */
#define BRIGHTNESS_SEM 0x2b /* RAM location for brightness semaphore */

static ssize_t show_brightness(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);    
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);     

	return sprintf(buf, "%i", cytherm->brightness);
}

static ssize_t set_brightness(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	unsigned char *buffer;
	int retval;
   
	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	cytherm->brightness = simple_strtoul(buf, NULL, 10);
   
	if (cytherm->brightness > 0xFF)
		cytherm->brightness = 0xFF;
	else if (cytherm->brightness < 0)
		cytherm->brightness = 0;
   
	/* Set brightness */
	retval = vendor_command(cytherm->udev, WRITE_RAM, BRIGHTNESS, 
				cytherm->brightness, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);
	/* Inform µC that we have changed the brightness setting */
	retval = vendor_command(cytherm->udev, WRITE_RAM, BRIGHTNESS_SEM,
				0x01, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);
   
	kfree(buffer);
   
	return count;
}

static DEVICE_ATTR(brightness, S_IRUGO | S_IWUSR | S_IWGRP, 
		   show_brightness, set_brightness);


#define TEMP 0x33 /* RAM location for temperature */
#define SIGN 0x34 /* RAM location for temperature sign */

static ssize_t show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{

	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	int retval;
	unsigned char *buffer;

	int temp, sign;
   
	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	/* read temperature */
	retval = vendor_command(cytherm->udev, READ_RAM, TEMP, 0, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);
	temp = buffer[1];
   
	/* read sign */
	retval = vendor_command(cytherm->udev, READ_RAM, SIGN, 0, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);
	sign = buffer[1];

	kfree(buffer);
   
	return sprintf(buf, "%c%i.%i", sign ? '-' : '+', temp >> 1,
		       5*(temp - ((temp >> 1) << 1)));
}


static ssize_t set_temp(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(temp, S_IRUGO, show_temp, set_temp);


#define BUTTON 0x7a

static ssize_t show_button(struct device *dev, struct device_attribute *attr, char *buf)
{

	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	int retval;
	unsigned char *buffer;

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	/* check button */
	retval = vendor_command(cytherm->udev, READ_RAM, BUTTON, 0, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);
   
	retval = buffer[1];

	kfree(buffer);

	if (retval)
		return sprintf(buf, "1");
	else
		return sprintf(buf, "0");
}


static ssize_t set_button(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(button, S_IRUGO, show_button, set_button);


static ssize_t show_port0(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	int retval;
	unsigned char *buffer;

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	retval = vendor_command(cytherm->udev, READ_PORT, 0, 0, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);

	retval = buffer[1];

	kfree(buffer);

	return sprintf(buf, "%d", retval);
}


static ssize_t set_port0(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	unsigned char *buffer;
	int retval;
	int tmp;
   
	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	tmp = simple_strtoul(buf, NULL, 10);
   
	if (tmp > 0xFF)
		tmp = 0xFF;
	else if (tmp < 0)
		tmp = 0;
   
	retval = vendor_command(cytherm->udev, WRITE_PORT, 0,
				tmp, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);

	kfree(buffer);

	return count;
}

static DEVICE_ATTR(port0, S_IRUGO | S_IWUSR | S_IWGRP, show_port0, set_port0);

static ssize_t show_port1(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	int retval;
	unsigned char *buffer;

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	retval = vendor_command(cytherm->udev, READ_PORT, 1, 0, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);
   
	retval = buffer[1];

	kfree(buffer);

	return sprintf(buf, "%d", retval);
}


static ssize_t set_port1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_cytherm *cytherm = usb_get_intfdata(intf);

	unsigned char *buffer;
	int retval;
	int tmp;
   
	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer) {
		dev_err(&cytherm->udev->dev, "out of memory\n");
		return 0;
	}

	tmp = simple_strtoul(buf, NULL, 10);
   
	if (tmp > 0xFF)
		tmp = 0xFF;
	else if (tmp < 0)
		tmp = 0;
   
	retval = vendor_command(cytherm->udev, WRITE_PORT, 1,
				tmp, buffer, 8);
	if (retval)
		dev_dbg(&cytherm->udev->dev, "retval = %d\n", retval);

	kfree(buffer);

	return count;
}

static DEVICE_ATTR(port1, S_IRUGO | S_IWUSR | S_IWGRP, show_port1, set_port1);



static int cytherm_probe(struct usb_interface *interface, 
			 const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_cytherm *dev = NULL;
	int retval = -ENOMEM;

	dev = kzalloc (sizeof(struct usb_cytherm), GFP_KERNEL);
	if (dev == NULL) {
		dev_err (&interface->dev, "Out of memory\n");
		goto error_mem;
	}

	dev->udev = usb_get_dev(udev);

	usb_set_intfdata (interface, dev);

	dev->brightness = 0xFF;

	retval = device_create_file(&interface->dev, &dev_attr_brightness);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_temp);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_button);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_port0);
	if (retval)
		goto error;
	retval = device_create_file(&interface->dev, &dev_attr_port1);
	if (retval)
		goto error;

	dev_info (&interface->dev,
		  "Cypress thermometer device now attached\n");
	return 0;
error:
	device_remove_file(&interface->dev, &dev_attr_brightness);
	device_remove_file(&interface->dev, &dev_attr_temp);
	device_remove_file(&interface->dev, &dev_attr_button);
	device_remove_file(&interface->dev, &dev_attr_port0);
	device_remove_file(&interface->dev, &dev_attr_port1);
	usb_set_intfdata (interface, NULL);
	usb_put_dev(dev->udev);
	kfree(dev);
error_mem:
	return retval;
}

static void cytherm_disconnect(struct usb_interface *interface)
{
	struct usb_cytherm *dev;

	dev = usb_get_intfdata (interface);

	device_remove_file(&interface->dev, &dev_attr_brightness);
	device_remove_file(&interface->dev, &dev_attr_temp);
	device_remove_file(&interface->dev, &dev_attr_button);
	device_remove_file(&interface->dev, &dev_attr_port0);
	device_remove_file(&interface->dev, &dev_attr_port1);

	/* first remove the files, then NULL the pointer */
	usb_set_intfdata (interface, NULL);

	usb_put_dev(dev->udev);

	kfree(dev);

	dev_info(&interface->dev, "Cypress thermometer now disconnected\n");
}


static int __init usb_cytherm_init(void)
{
	int result;

	result = usb_register(&cytherm_driver);
	if (result) 
	{	
		err("usb_register failed. Error number %d", result);
		return result;
	}

	info(DRIVER_VERSION ":" DRIVER_DESC);
	return 0;
}

static void __exit usb_cytherm_exit(void)
{
	usb_deregister(&cytherm_driver);
}


module_init (usb_cytherm_init);
module_exit (usb_cytherm_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
