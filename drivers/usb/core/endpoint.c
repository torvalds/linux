/*
 * drivers/usb/core/endpoint.c
 *
 * (C) Copyright 2002,2004,2006 Greg Kroah-Hartman
 * (C) Copyright 2002,2004 IBM Corp.
 * (C) Copyright 2006 Novell Inc.
 *
 * Endpoint sysfs stuff
 *
 */

#include <linux/kernel.h>
#include <linux/usb.h>
#include "usb.h"

/* endpoint stuff */

struct ep_device {
	struct usb_endpoint_descriptor *desc;
	struct usb_device *udev;
	struct device dev;
};
#define to_ep_device(_dev) \
	container_of(_dev, struct ep_device, dev)

struct ep_attribute {
	struct attribute attr;
	ssize_t (*show)(struct usb_device *,
			struct usb_endpoint_descriptor *, char *);
};
#define to_ep_attribute(_attr) \
	container_of(_attr, struct ep_attribute, attr)

#define usb_ep_attr(field, format_string)			\
static ssize_t show_ep_##field(struct device *dev,		\
			       struct device_attribute *attr,	\
			       char *buf)			\
{								\
	struct ep_device *ep = to_ep_device(dev);		\
	return sprintf(buf, format_string, ep->desc->field);	\
}								\
static DEVICE_ATTR(field, S_IRUGO, show_ep_##field, NULL);

usb_ep_attr(bLength, "%02x\n")
usb_ep_attr(bEndpointAddress, "%02x\n")
usb_ep_attr(bmAttributes, "%02x\n")
usb_ep_attr(bInterval, "%02x\n")

static ssize_t show_ep_wMaxPacketSize(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct ep_device *ep = to_ep_device(dev);
	return sprintf(buf, "%04x\n",
			le16_to_cpu(ep->desc->wMaxPacketSize) & 0x07ff);
}
static DEVICE_ATTR(wMaxPacketSize, S_IRUGO, show_ep_wMaxPacketSize, NULL);

static ssize_t show_ep_type(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct ep_device *ep = to_ep_device(dev);
	char *type = "unknown";

	switch (ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		type = "Control";
		break;
	case USB_ENDPOINT_XFER_ISOC:
		type = "Isoc";
		break;
	case USB_ENDPOINT_XFER_BULK:
		type = "Bulk";
		break;
	case USB_ENDPOINT_XFER_INT:
		type = "Interrupt";
		break;
	}
	return sprintf(buf, "%s\n", type);
}
static DEVICE_ATTR(type, S_IRUGO, show_ep_type, NULL);

static ssize_t show_ep_interval(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct ep_device *ep = to_ep_device(dev);
	char unit;
	unsigned interval = 0;
	unsigned in;

	in = (ep->desc->bEndpointAddress & USB_DIR_IN);

	switch (ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		if (ep->udev->speed == USB_SPEED_HIGH) 	/* uframes per NAK */
			interval = ep->desc->bInterval;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		interval = 1 << (ep->desc->bInterval - 1);
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (ep->udev->speed == USB_SPEED_HIGH && !in) /* uframes per NAK */
			interval = ep->desc->bInterval;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (ep->udev->speed == USB_SPEED_HIGH)
			interval = 1 << (ep->desc->bInterval - 1);
		else
			interval = ep->desc->bInterval;
		break;
	}
	interval *= (ep->udev->speed == USB_SPEED_HIGH) ? 125 : 1000;
	if (interval % 1000)
		unit = 'u';
	else {
		unit = 'm';
		interval /= 1000;
	}

	return sprintf(buf, "%d%cs\n", interval, unit);
}
static DEVICE_ATTR(interval, S_IRUGO, show_ep_interval, NULL);

static ssize_t show_ep_direction(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct ep_device *ep = to_ep_device(dev);
	char *direction;

	if ((ep->desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_CONTROL)
		direction = "both";
	else if (ep->desc->bEndpointAddress & USB_DIR_IN)
		direction = "in";
	else
		direction = "out";
	return sprintf(buf, "%s\n", direction);
}
static DEVICE_ATTR(direction, S_IRUGO, show_ep_direction, NULL);

static struct attribute *ep_dev_attrs[] = {
	&dev_attr_bLength.attr,
	&dev_attr_bEndpointAddress.attr,
	&dev_attr_bmAttributes.attr,
	&dev_attr_bInterval.attr,
	&dev_attr_wMaxPacketSize.attr,
	&dev_attr_interval.attr,
	&dev_attr_type.attr,
	&dev_attr_direction.attr,
	NULL,
};
static struct attribute_group ep_dev_attr_grp = {
	.attrs = ep_dev_attrs,
};

static struct endpoint_class {
	struct kref kref;
	struct class *class;
} *ep_class;

static int init_endpoint_class(void)
{
	int result = 0;

	if (ep_class != NULL) {
		kref_get(&ep_class->kref);
		goto exit;
	}

	ep_class = kmalloc(sizeof(*ep_class), GFP_KERNEL);
	if (!ep_class) {
		result = -ENOMEM;
		goto exit;
	}

	kref_init(&ep_class->kref);
	ep_class->class = class_create(THIS_MODULE, "usb_endpoint");
	if (IS_ERR(ep_class->class)) {
		result = IS_ERR(ep_class->class);
		kfree(ep_class);
		ep_class = NULL;
		goto exit;
	}

exit:
	return result;
}

static void release_endpoint_class(struct kref *kref)
{
	/* Ok, we cheat as we know we only have one ep_class */
	class_destroy(ep_class->class);
	kfree(ep_class);
	ep_class = NULL;
}

static void destroy_endpoint_class(void)
{
	if (ep_class)
		kref_put(&ep_class->kref, release_endpoint_class);
}

static void ep_device_release(struct device *dev)
{
	struct ep_device *ep_dev = to_ep_device(dev);

	dev_dbg(dev, "%s called for %s\n", __FUNCTION__, dev->bus_id);
	kfree(ep_dev);
}

int usb_create_ep_files(struct device *parent,
			struct usb_host_endpoint *endpoint,
			struct usb_device *udev)
{
	char name[8];
	struct ep_device *ep_dev;
	int minor;
	int retval;

	retval = init_endpoint_class();
	if (retval)
		goto exit;

	ep_dev = kzalloc(sizeof(*ep_dev), GFP_KERNEL);
	if (!ep_dev) {
		retval = -ENOMEM;
		goto error_alloc;
	}

	/* fun calculation to determine the minor of this endpoint */
	minor = (((udev->bus->busnum - 1) * 128) * 16) + (udev->devnum - 1);

	ep_dev->desc = &endpoint->desc;
	ep_dev->udev = udev;
	ep_dev->dev.devt = MKDEV(442, minor);	// FIXME fake number...
	ep_dev->dev.class = ep_class->class;
	ep_dev->dev.parent = parent;
	ep_dev->dev.release = ep_device_release;
	snprintf(ep_dev->dev.bus_id, BUS_ID_SIZE, "usbdev%d.%d_ep%02x",
		 udev->bus->busnum, udev->devnum,
		 endpoint->desc.bEndpointAddress);

	retval = device_register(&ep_dev->dev);
	if (retval)
		goto error_register;
	retval = sysfs_create_group(&ep_dev->dev.kobj, &ep_dev_attr_grp);
	if (retval)
		goto error_group;

	/* create the symlink to the old-style "ep_XX" directory */
	sprintf(name, "ep_%02x", endpoint->desc.bEndpointAddress);
	retval = sysfs_create_link(&parent->kobj, &ep_dev->dev.kobj, name);
	if (retval)
		goto error_link;
	endpoint->ep_dev = ep_dev;
	return retval;

error_link:
	sysfs_remove_group(&ep_dev->dev.kobj, &ep_dev_attr_grp);
error_group:
	device_unregister(&ep_dev->dev);
	destroy_endpoint_class();
	return retval;

error_register:
	kfree(ep_dev);
error_alloc:
	destroy_endpoint_class();
exit:
	return retval;
}

void usb_remove_ep_files(struct usb_host_endpoint *endpoint)
{

	if (endpoint->ep_dev) {
		char name[8];

		sprintf(name, "ep_%02x", endpoint->desc.bEndpointAddress);
		sysfs_remove_link(&endpoint->ep_dev->dev.parent->kobj, name);
		sysfs_remove_group(&endpoint->ep_dev->dev.kobj, &ep_dev_attr_grp);
		device_unregister(&endpoint->ep_dev->dev);
		endpoint->ep_dev = NULL;
		destroy_endpoint_class();
	}
}
