// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/usb/misc/lvstest.c
 *
 * Test pattern generation for Link Layer Validation System Tests
 *
 * Copyright (C) 2014 ST Microelectronics
 * Pratyush Anand <pratyush.anand@gmail.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/ch11.h>
#include <linux/usb/hcd.h>
#include <linux/usb/phy.h>

struct lvs_rh {
	/* root hub interface */
	struct usb_interface *intf;
	/* if lvs device connected */
	bool present;
	/* port no at which lvs device is present */
	int portnum;
	/* urb buffer */
	u8 buffer[8];
	/* class descriptor */
	struct usb_hub_descriptor descriptor;
	/* urb for polling interrupt pipe */
	struct urb *urb;
	/* LVH RH work */
	struct work_struct	rh_work;
	/* RH port status */
	struct usb_port_status port_status;
};

static struct usb_device *create_lvs_device(struct usb_interface *intf)
{
	struct usb_device *udev, *hdev;
	struct usb_hcd *hcd;
	struct lvs_rh *lvs = usb_get_intfdata(intf);

	if (!lvs->present) {
		dev_err(&intf->dev, "No LVS device is present\n");
		return NULL;
	}

	hdev = interface_to_usbdev(intf);
	hcd = bus_to_hcd(hdev->bus);

	udev = usb_alloc_dev(hdev, hdev->bus, lvs->portnum);
	if (!udev) {
		dev_err(&intf->dev, "Could not allocate lvs udev\n");
		return NULL;
	}
	udev->speed = USB_SPEED_SUPER;
	udev->ep0.desc.wMaxPacketSize = cpu_to_le16(512);
	usb_set_device_state(udev, USB_STATE_DEFAULT);

	if (hcd->driver->enable_device) {
		if (hcd->driver->enable_device(hcd, udev) < 0) {
			dev_err(&intf->dev, "Failed to enable\n");
			usb_put_dev(udev);
			return NULL;
		}
	}

	return udev;
}

static void destroy_lvs_device(struct usb_device *udev)
{
	struct usb_device *hdev = udev->parent;
	struct usb_hcd *hcd = bus_to_hcd(hdev->bus);

	if (hcd->driver->free_dev)
		hcd->driver->free_dev(hcd, udev);

	usb_put_dev(udev);
}

static int lvs_rh_clear_port_feature(struct usb_device *hdev,
		int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, 1000);
}

static int lvs_rh_set_port_feature(struct usb_device *hdev,
		int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, 1000);
}

static ssize_t u3_entry_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	struct usb_device *udev;
	int ret;

	udev = create_lvs_device(intf);
	if (!udev) {
		dev_err(dev, "failed to create lvs device\n");
		return -ENOMEM;
	}

	ret = lvs_rh_set_port_feature(hdev, lvs->portnum,
			USB_PORT_FEAT_SUSPEND);
	if (ret < 0)
		dev_err(dev, "can't issue U3 entry %d\n", ret);

	destroy_lvs_device(udev);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(u3_entry);

static ssize_t u3_exit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	struct usb_device *udev;
	int ret;

	udev = create_lvs_device(intf);
	if (!udev) {
		dev_err(dev, "failed to create lvs device\n");
		return -ENOMEM;
	}

	ret = lvs_rh_clear_port_feature(hdev, lvs->portnum,
			USB_PORT_FEAT_SUSPEND);
	if (ret < 0)
		dev_err(dev, "can't issue U3 exit %d\n", ret);

	destroy_lvs_device(udev);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(u3_exit);

static ssize_t hot_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	int ret;

	ret = lvs_rh_set_port_feature(hdev, lvs->portnum,
			USB_PORT_FEAT_RESET);
	if (ret < 0) {
		dev_err(dev, "can't issue hot reset %d\n", ret);
		return ret;
	}

	return count;
}
static DEVICE_ATTR_WO(hot_reset);

static ssize_t warm_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	int ret;

	ret = lvs_rh_set_port_feature(hdev, lvs->portnum,
			USB_PORT_FEAT_BH_PORT_RESET);
	if (ret < 0) {
		dev_err(dev, "can't issue warm reset %d\n", ret);
		return ret;
	}

	return count;
}
static DEVICE_ATTR_WO(warm_reset);

static ssize_t u2_timeout_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0) {
		dev_err(dev, "couldn't parse string %d\n", ret);
		return ret;
	}

	if (val > 127)
		return -EINVAL;

	ret = lvs_rh_set_port_feature(hdev, lvs->portnum | (val << 8),
			USB_PORT_FEAT_U2_TIMEOUT);
	if (ret < 0) {
		dev_err(dev, "Error %d while setting U2 timeout %ld\n", ret, val);
		return ret;
	}

	return count;
}
static DEVICE_ATTR_WO(u2_timeout);

static ssize_t u1_timeout_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0) {
		dev_err(dev, "couldn't parse string %d\n", ret);
		return ret;
	}

	if (val > 127)
		return -EINVAL;

	ret = lvs_rh_set_port_feature(hdev, lvs->portnum | (val << 8),
			USB_PORT_FEAT_U1_TIMEOUT);
	if (ret < 0) {
		dev_err(dev, "Error %d while setting U1 timeout %ld\n", ret, val);
		return ret;
	}

	return count;
}
static DEVICE_ATTR_WO(u1_timeout);

static ssize_t get_dev_desc_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *udev;
	struct usb_device_descriptor *descriptor;
	int ret;

	descriptor = kmalloc(sizeof(*descriptor), GFP_KERNEL);
	if (!descriptor)
		return -ENOMEM;

	udev = create_lvs_device(intf);
	if (!udev) {
		dev_err(dev, "failed to create lvs device\n");
		ret = -ENOMEM;
		goto free_desc;
	}

	ret = usb_control_msg(udev, (PIPE_CONTROL << 30) | USB_DIR_IN,
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN, USB_DT_DEVICE << 8,
			0, descriptor, sizeof(*descriptor),
			USB_CTRL_GET_TIMEOUT);
	if (ret < 0)
		dev_err(dev, "can't read device descriptor %d\n", ret);

	destroy_lvs_device(udev);

free_desc:
	kfree(descriptor);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(get_dev_desc);

static ssize_t enable_compliance_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct lvs_rh *lvs = usb_get_intfdata(intf);
	int ret;

	ret = lvs_rh_set_port_feature(hdev,
			lvs->portnum | USB_SS_PORT_LS_COMP_MOD << 3,
			USB_PORT_FEAT_LINK_STATE);
	if (ret < 0) {
		dev_err(dev, "can't enable compliance mode %d\n", ret);
		return ret;
	}

	return count;
}
static DEVICE_ATTR_WO(enable_compliance);

static struct attribute *lvs_attrs[] = {
	&dev_attr_get_dev_desc.attr,
	&dev_attr_u1_timeout.attr,
	&dev_attr_u2_timeout.attr,
	&dev_attr_hot_reset.attr,
	&dev_attr_warm_reset.attr,
	&dev_attr_u3_entry.attr,
	&dev_attr_u3_exit.attr,
	&dev_attr_enable_compliance.attr,
	NULL
};
ATTRIBUTE_GROUPS(lvs);

static void lvs_rh_work(struct work_struct *work)
{
	struct lvs_rh *lvs = container_of(work, struct lvs_rh, rh_work);
	struct usb_interface *intf = lvs->intf;
	struct usb_device *hdev = interface_to_usbdev(intf);
	struct usb_hcd *hcd = bus_to_hcd(hdev->bus);
	struct usb_hub_descriptor *descriptor = &lvs->descriptor;
	struct usb_port_status *port_status = &lvs->port_status;
	int i, ret = 0;
	u16 portchange;

	/* Examine each root port */
	for (i = 1; i <= descriptor->bNbrPorts; i++) {
		ret = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, i,
			port_status, sizeof(*port_status), 1000);
		if (ret < 4)
			continue;

		portchange = le16_to_cpu(port_status->wPortChange);

		if (portchange & USB_PORT_STAT_C_LINK_STATE)
			lvs_rh_clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_PORT_LINK_STATE);
		if (portchange & USB_PORT_STAT_C_ENABLE)
			lvs_rh_clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_ENABLE);
		if (portchange & USB_PORT_STAT_C_RESET)
			lvs_rh_clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_RESET);
		if (portchange & USB_PORT_STAT_C_BH_RESET)
			lvs_rh_clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_BH_PORT_RESET);
		if (portchange & USB_PORT_STAT_C_CONNECTION) {
			lvs_rh_clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_CONNECTION);

			if (le16_to_cpu(port_status->wPortStatus) &
					USB_PORT_STAT_CONNECTION) {
				lvs->present = true;
				lvs->portnum = i;
				if (hcd->usb_phy)
					usb_phy_notify_connect(hcd->usb_phy,
							USB_SPEED_SUPER);
			} else {
				lvs->present = false;
				if (hcd->usb_phy)
					usb_phy_notify_disconnect(hcd->usb_phy,
							USB_SPEED_SUPER);
			}
			break;
		}
	}

	ret = usb_submit_urb(lvs->urb, GFP_KERNEL);
	if (ret != 0 && ret != -ENODEV && ret != -EPERM)
		dev_err(&intf->dev, "urb resubmit error %d\n", ret);
}

static void lvs_rh_irq(struct urb *urb)
{
	struct lvs_rh *lvs = urb->context;

	schedule_work(&lvs->rh_work);
}

static int lvs_rh_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	struct usb_device *hdev;
	struct usb_host_interface *desc;
	struct usb_endpoint_descriptor *endpoint;
	struct lvs_rh *lvs;
	unsigned int pipe;
	int ret, maxp;

	hdev = interface_to_usbdev(intf);
	desc = intf->cur_altsetting;

	ret = usb_find_int_in_endpoint(desc, &endpoint);
	if (ret)
		return ret;

	/* valid only for SS root hub */
	if (hdev->descriptor.bDeviceProtocol != USB_HUB_PR_SS || hdev->parent) {
		dev_err(&intf->dev, "Bind LVS driver with SS root Hub only\n");
		return -EINVAL;
	}

	lvs = devm_kzalloc(&intf->dev, sizeof(*lvs), GFP_KERNEL);
	if (!lvs)
		return -ENOMEM;

	lvs->intf = intf;
	usb_set_intfdata(intf, lvs);

	/* how many number of ports this root hub has */
	ret = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
			USB_DT_SS_HUB << 8, 0, &lvs->descriptor,
			USB_DT_SS_HUB_SIZE, USB_CTRL_GET_TIMEOUT);
	if (ret < (USB_DT_HUB_NONVAR_SIZE + 2)) {
		dev_err(&hdev->dev, "wrong root hub descriptor read %d\n", ret);
		return ret < 0 ? ret : -EINVAL;
	}

	/* submit urb to poll interrupt endpoint */
	lvs->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!lvs->urb)
		return -ENOMEM;

	INIT_WORK(&lvs->rh_work, lvs_rh_work);

	pipe = usb_rcvintpipe(hdev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(hdev, pipe, usb_pipeout(pipe));
	usb_fill_int_urb(lvs->urb, hdev, pipe, &lvs->buffer[0], maxp,
			lvs_rh_irq, lvs, endpoint->bInterval);

	ret = usb_submit_urb(lvs->urb, GFP_KERNEL);
	if (ret < 0) {
		dev_err(&intf->dev, "couldn't submit lvs urb %d\n", ret);
		goto free_urb;
	}

	return ret;

free_urb:
	usb_free_urb(lvs->urb);
	return ret;
}

static void lvs_rh_disconnect(struct usb_interface *intf)
{
	struct lvs_rh *lvs = usb_get_intfdata(intf);

	usb_poison_urb(lvs->urb); /* used in scheduled work */
	flush_work(&lvs->rh_work);
	usb_free_urb(lvs->urb);
}

static struct usb_driver lvs_driver = {
	.name =		"lvs",
	.probe =	lvs_rh_probe,
	.disconnect =	lvs_rh_disconnect,
	.dev_groups =	lvs_groups,
};

module_usb_driver(lvs_driver);

MODULE_DESCRIPTION("Link Layer Validation System Driver");
MODULE_LICENSE("GPL");
