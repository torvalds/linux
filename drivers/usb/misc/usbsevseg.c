// SPDX-License-Identifier: GPL-2.0
/*
 * USB 7 Segment Driver
 *
 * Copyright (C) 2008 Harrison Metzger <harrisonmetz@gmail.com>
 * Based on usbled.c by Greg Kroah-Hartman (greg@kroah.com)
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/usb.h>


#define DRIVER_AUTHOR "Harrison Metzger <harrisonmetz@gmail.com>"
#define DRIVER_DESC "USB 7 Segment Driver"

#define VENDOR_ID	0x0fc5
#define PRODUCT_ID	0x1227
#define MAXLEN		8

/* table of devices that work with this driver */
static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

/* the different text display modes the device is capable of */
static const char *display_textmodes[] = {"raw", "hex", "ascii"};

struct usb_sevsegdev {
	struct usb_device *udev;
	struct usb_interface *intf;

	u8 powered;
	u8 mode_msb;
	u8 mode_lsb;
	u8 decimals[MAXLEN];
	u8 textmode;
	u8 text[MAXLEN];
	u16 textlength;

	u8 shadow_power; /* for PM */
	u8 has_interface_pm;
};

/* sysfs_streq can't replace this completely
 * If the device was in hex mode, and the user wanted a 0,
 * if str commands are used, we would assume the end of string
 * so mem commands are used.
 */
static inline size_t my_memlen(const char *buf, size_t count)
{
	if (count > 0 && buf[count-1] == '\n')
		return count - 1;
	else
		return count;
}

static void update_display_powered(struct usb_sevsegdev *mydev)
{
	int rc;

	if (mydev->powered && !mydev->has_interface_pm) {
		rc = usb_autopm_get_interface(mydev->intf);
		if (rc < 0)
			return;
		mydev->has_interface_pm = 1;
	}

	if (mydev->shadow_power != 1)
		return;

	rc = usb_control_msg_send(mydev->udev, 0, 0x12, 0x48,
				  (80 * 0x100) + 10, /*  (power mode) */
				  (0x00 * 0x100) + (mydev->powered ? 1 : 0),
				  NULL, 0, 2000, GFP_KERNEL);
	if (rc < 0)
		dev_dbg(&mydev->udev->dev, "power retval = %d\n", rc);

	if (!mydev->powered && mydev->has_interface_pm) {
		usb_autopm_put_interface(mydev->intf);
		mydev->has_interface_pm = 0;
	}
}

static void update_display_mode(struct usb_sevsegdev *mydev)
{
	int rc;

	if(mydev->shadow_power != 1)
		return;

	rc = usb_control_msg_send(mydev->udev, 0, 0x12, 0x48,
				  (82 * 0x100) + 10, /* (set mode) */
				  (mydev->mode_msb * 0x100) + mydev->mode_lsb,
				  NULL, 0, 2000, GFP_NOIO);

	if (rc < 0)
		dev_dbg(&mydev->udev->dev, "mode retval = %d\n", rc);
}

static void update_display_visual(struct usb_sevsegdev *mydev, gfp_t mf)
{
	int rc;
	int i;
	unsigned char buffer[MAXLEN] = {0};
	u8 decimals = 0;

	if(mydev->shadow_power != 1)
		return;

	/* The device is right to left, where as you write left to right */
	for (i = 0; i < mydev->textlength; i++)
		buffer[i] = mydev->text[mydev->textlength-1-i];

	rc = usb_control_msg_send(mydev->udev, 0, 0x12, 0x48,
				  (85 * 0x100) + 10, /* (write text) */
				  (0 * 0x100) + mydev->textmode, /* mode  */
				  &buffer, mydev->textlength, 2000, mf);

	if (rc < 0)
		dev_dbg(&mydev->udev->dev, "write retval = %d\n", rc);

	/* The device is right to left, where as you write left to right */
	for (i = 0; i < sizeof(mydev->decimals); i++)
		decimals |= mydev->decimals[i] << i;

	rc = usb_control_msg_send(mydev->udev, 0, 0x12, 0x48,
				  (86 * 0x100) + 10, /* (set decimal) */
				  (0 * 0x100) + decimals, /* decimals */
				  NULL, 0, 2000, mf);

	if (rc < 0)
		dev_dbg(&mydev->udev->dev, "decimal retval = %d\n", rc);
}

#define MYDEV_ATTR_SIMPLE_UNSIGNED(name, update_fcn)		\
static ssize_t name##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf) 		\
{								\
	struct usb_interface *intf = to_usb_interface(dev);	\
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);	\
								\
	return sprintf(buf, "%u\n", mydev->name);		\
}								\
								\
static ssize_t name##_store(struct device *dev,			\
	struct device_attribute *attr, const char *buf, size_t count) \
{								\
	struct usb_interface *intf = to_usb_interface(dev);	\
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);	\
								\
	mydev->name = simple_strtoul(buf, NULL, 10);		\
	update_fcn(mydev); 					\
								\
	return count;						\
}								\
static DEVICE_ATTR_RW(name);

static ssize_t text_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);

	return snprintf(buf, mydev->textlength, "%s\n", mydev->text);
}

static ssize_t text_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);
	size_t end = my_memlen(buf, count);

	if (end > sizeof(mydev->text))
		return -EINVAL;

	memset(mydev->text, 0, sizeof(mydev->text));
	mydev->textlength = end;

	if (end > 0)
		memcpy(mydev->text, buf, end);

	update_display_visual(mydev, GFP_KERNEL);
	return count;
}

static DEVICE_ATTR_RW(text);

static ssize_t decimals_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);
	int i;
	int pos;

	for (i = 0; i < sizeof(mydev->decimals); i++) {
		pos = sizeof(mydev->decimals) - 1 - i;
		if (mydev->decimals[i] == 0)
			buf[pos] = '0';
		else if (mydev->decimals[i] == 1)
			buf[pos] = '1';
		else
			buf[pos] = 'x';
	}

	buf[sizeof(mydev->decimals)] = '\n';
	return sizeof(mydev->decimals) + 1;
}

static ssize_t decimals_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);
	size_t end = my_memlen(buf, count);
	int i;

	if (end > sizeof(mydev->decimals))
		return -EINVAL;

	for (i = 0; i < end; i++)
		if (buf[i] != '0' && buf[i] != '1')
			return -EINVAL;

	memset(mydev->decimals, 0, sizeof(mydev->decimals));
	for (i = 0; i < end; i++)
		if (buf[i] == '1')
			mydev->decimals[end-1-i] = 1;

	update_display_visual(mydev, GFP_KERNEL);

	return count;
}

static DEVICE_ATTR_RW(decimals);

static ssize_t textmode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);
	int i;

	buf[0] = 0;

	for (i = 0; i < ARRAY_SIZE(display_textmodes); i++) {
		if (mydev->textmode == i) {
			strcat(buf, " [");
			strcat(buf, display_textmodes[i]);
			strcat(buf, "] ");
		} else {
			strcat(buf, " ");
			strcat(buf, display_textmodes[i]);
			strcat(buf, " ");
		}
	}
	strcat(buf, "\n");


	return strlen(buf);
}

static ssize_t textmode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_sevsegdev *mydev = usb_get_intfdata(intf);
	int i;

	i = sysfs_match_string(display_textmodes, buf);
	if (i < 0)
		return i;

	mydev->textmode = i;
	update_display_visual(mydev, GFP_KERNEL);
	return count;
}

static DEVICE_ATTR_RW(textmode);


MYDEV_ATTR_SIMPLE_UNSIGNED(powered, update_display_powered);
MYDEV_ATTR_SIMPLE_UNSIGNED(mode_msb, update_display_mode);
MYDEV_ATTR_SIMPLE_UNSIGNED(mode_lsb, update_display_mode);

static struct attribute *sevseg_attrs[] = {
	&dev_attr_powered.attr,
	&dev_attr_text.attr,
	&dev_attr_textmode.attr,
	&dev_attr_decimals.attr,
	&dev_attr_mode_msb.attr,
	&dev_attr_mode_lsb.attr,
	NULL
};
ATTRIBUTE_GROUPS(sevseg);

static int sevseg_probe(struct usb_interface *interface,
	const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_sevsegdev *mydev = NULL;
	int rc = -ENOMEM;

	mydev = kzalloc(sizeof(struct usb_sevsegdev), GFP_KERNEL);
	if (!mydev)
		goto error_mem;

	mydev->udev = usb_get_dev(udev);
	mydev->intf = interface;
	usb_set_intfdata(interface, mydev);

	/* PM */
	mydev->shadow_power = 1; /* currently active */
	mydev->has_interface_pm = 0; /* have not issued autopm_get */

	/*set defaults */
	mydev->textmode = 0x02; /* ascii mode */
	mydev->mode_msb = 0x06; /* 6 characters */
	mydev->mode_lsb = 0x3f; /* scanmode for 6 chars */

	dev_info(&interface->dev, "USB 7 Segment device now attached\n");
	return 0;

error_mem:
	return rc;
}

static void sevseg_disconnect(struct usb_interface *interface)
{
	struct usb_sevsegdev *mydev;

	mydev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);
	usb_put_dev(mydev->udev);
	kfree(mydev);
	dev_info(&interface->dev, "USB 7 Segment now disconnected\n");
}

static int sevseg_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_sevsegdev *mydev;

	mydev = usb_get_intfdata(intf);
	mydev->shadow_power = 0;

	return 0;
}

static int sevseg_resume(struct usb_interface *intf)
{
	struct usb_sevsegdev *mydev;

	mydev = usb_get_intfdata(intf);
	mydev->shadow_power = 1;
	update_display_mode(mydev);
	update_display_visual(mydev, GFP_NOIO);

	return 0;
}

static int sevseg_reset_resume(struct usb_interface *intf)
{
	struct usb_sevsegdev *mydev;

	mydev = usb_get_intfdata(intf);
	mydev->shadow_power = 1;
	update_display_mode(mydev);
	update_display_visual(mydev, GFP_NOIO);

	return 0;
}

static struct usb_driver sevseg_driver = {
	.name =		"usbsevseg",
	.probe =	sevseg_probe,
	.disconnect =	sevseg_disconnect,
	.suspend =	sevseg_suspend,
	.resume =	sevseg_resume,
	.reset_resume =	sevseg_reset_resume,
	.id_table =	id_table,
	.dev_groups =	sevseg_groups,
	.supports_autosuspend = 1,
};

module_usb_driver(sevseg_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
