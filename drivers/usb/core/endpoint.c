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
struct ep_object {
	struct usb_endpoint_descriptor *desc;
	struct usb_device *udev;
	struct kobject kobj;
};
#define to_ep_object(_kobj) \
	container_of(_kobj, struct ep_object, kobj)

struct ep_attribute {
	struct attribute attr;
	ssize_t (*show)(struct usb_device *,
			struct usb_endpoint_descriptor *, char *);
};
#define to_ep_attribute(_attr) \
	container_of(_attr, struct ep_attribute, attr)

#define EP_ATTR(_name)						\
struct ep_attribute ep_##_name = {				\
	.attr = {.name = #_name, .owner = THIS_MODULE,		\
			.mode = S_IRUGO},			\
	.show = show_ep_##_name}

#define usb_ep_attr(field, format_string)			\
static ssize_t show_ep_##field(struct usb_device *udev,		\
		struct usb_endpoint_descriptor *desc, 		\
		char *buf)					\
{								\
	return sprintf(buf, format_string, desc->field);	\
}								\
static EP_ATTR(field);

usb_ep_attr(bLength, "%02x\n")
usb_ep_attr(bEndpointAddress, "%02x\n")
usb_ep_attr(bmAttributes, "%02x\n")
usb_ep_attr(bInterval, "%02x\n")

static ssize_t show_ep_wMaxPacketSize(struct usb_device *udev,
		struct usb_endpoint_descriptor *desc, char *buf)
{
	return sprintf(buf, "%04x\n",
			le16_to_cpu(desc->wMaxPacketSize) & 0x07ff);
}
static EP_ATTR(wMaxPacketSize);

static ssize_t show_ep_type(struct usb_device *udev,
		struct usb_endpoint_descriptor *desc, char *buf)
{
	char *type = "unknown";

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
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
static EP_ATTR(type);

static ssize_t show_ep_interval(struct usb_device *udev,
		struct usb_endpoint_descriptor *desc, char *buf)
{
	char unit;
	unsigned interval = 0;
	unsigned in;

	in = (desc->bEndpointAddress & USB_DIR_IN);

	switch (desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
	case USB_ENDPOINT_XFER_CONTROL:
		if (udev->speed == USB_SPEED_HIGH) 	/* uframes per NAK */
			interval = desc->bInterval;
		break;
	case USB_ENDPOINT_XFER_ISOC:
		interval = 1 << (desc->bInterval - 1);
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (udev->speed == USB_SPEED_HIGH && !in) /* uframes per NAK */
			interval = desc->bInterval;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (udev->speed == USB_SPEED_HIGH)
			interval = 1 << (desc->bInterval - 1);
		else
			interval = desc->bInterval;
		break;
	}
	interval *= (udev->speed == USB_SPEED_HIGH) ? 125 : 1000;
	if (interval % 1000)
		unit = 'u';
	else {
		unit = 'm';
		interval /= 1000;
	}

	return sprintf(buf, "%d%cs\n", interval, unit);
}
static EP_ATTR(interval);

static ssize_t show_ep_direction(struct usb_device *udev,
		struct usb_endpoint_descriptor *desc, char *buf)
{
	char *direction;

	if ((desc->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_CONTROL)
		direction = "both";
	else if (desc->bEndpointAddress & USB_DIR_IN)
		direction = "in";
	else
		direction = "out";
	return sprintf(buf, "%s\n", direction);
}
static EP_ATTR(direction);

static struct attribute *ep_attrs[] = {
	&ep_bLength.attr,
	&ep_bEndpointAddress.attr,
	&ep_bmAttributes.attr,
	&ep_bInterval.attr,
	&ep_wMaxPacketSize.attr,
	&ep_type.attr,
	&ep_interval.attr,
	&ep_direction.attr,
	NULL,
};

static void ep_object_release(struct kobject *kobj)
{
	kfree(to_ep_object(kobj));
}

static ssize_t ep_object_show(struct kobject *kobj, struct attribute *attr,
		char *buf)
{
	struct ep_object *ep_obj = to_ep_object(kobj);
	struct ep_attribute *ep_attr = to_ep_attribute(attr);

	return (ep_attr->show)(ep_obj->udev, ep_obj->desc, buf);
}

static struct sysfs_ops ep_object_sysfs_ops = {
	.show =			ep_object_show,
};

static struct kobj_type ep_object_ktype = {
	.release =		ep_object_release,
	.sysfs_ops =		&ep_object_sysfs_ops,
	.default_attrs =	ep_attrs,
};

void usb_create_ep_files(struct kobject *parent,
			 struct usb_host_endpoint *endpoint,
			 struct usb_device *udev)
{
	struct ep_object *ep_obj;
	struct kobject *kobj;

	ep_obj = kzalloc(sizeof(struct ep_object), GFP_KERNEL);
	if (!ep_obj)
		return;

	ep_obj->desc = &endpoint->desc;
	ep_obj->udev = udev;

	kobj = &ep_obj->kobj;
	kobject_set_name(kobj, "ep_%02x", endpoint->desc.bEndpointAddress);
	kobj->parent = parent;
	kobj->ktype = &ep_object_ktype;

	/* Don't use kobject_register, because it generates a hotplug event */
	kobject_init(kobj);
	if (kobject_add(kobj) == 0)
		endpoint->kobj = kobj;
	else
		kobject_put(kobj);
}

void usb_remove_ep_files(struct usb_host_endpoint *endpoint)
{

	if (endpoint->kobj) {
		kobject_del(endpoint->kobj);
		kobject_put(endpoint->kobj);
		endpoint->kobj = NULL;
	}
}


