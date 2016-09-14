/*
 * Copyright (C) 2015 Karol Kosik <karo9@interia.eu>
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/usb/gadget.h>
#include <linux/usb/ch9.h>
#include <linux/sysfs.h>
#include <linux/kthread.h>
#include <linux/byteorder/generic.h>

#include "usbip_common.h"
#include "vudc.h"

#include <net/sock.h>

/* called with udc->lock held */
int get_gadget_descs(struct vudc *udc)
{
	struct vrequest *usb_req;
	struct vep *ep0 = to_vep(udc->gadget.ep0);
	struct usb_device_descriptor *ddesc = &udc->dev_desc;
	struct usb_ctrlrequest req;
	int ret;

	if (!udc || !udc->driver || !udc->pullup)
		return -EINVAL;

	req.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
	req.bRequest = USB_REQ_GET_DESCRIPTOR;
	req.wValue = cpu_to_le16(USB_DT_DEVICE << 8);
	req.wIndex = cpu_to_le16(0);
	req.wLength = cpu_to_le16(sizeof(*ddesc));

	spin_unlock(&udc->lock);
	ret = udc->driver->setup(&(udc->gadget), &req);
	spin_lock(&udc->lock);
	if (ret < 0)
		goto out;

	/* assuming request queue is empty; request is now on top */
	usb_req = list_last_entry(&ep0->req_queue, struct vrequest, req_entry);
	list_del(&usb_req->req_entry);

	if (usb_req->req.length > sizeof(*ddesc)) {
		ret = -EOVERFLOW;
		goto giveback_req;
	}

	memcpy(ddesc, usb_req->req.buf, sizeof(*ddesc));
	udc->desc_cached = 1;
	ret = 0;
giveback_req:
	usb_req->req.status = 0;
	usb_req->req.actual = usb_req->req.length;
	usb_gadget_giveback_request(&(ep0->ep), &(usb_req->req));
out:
	return ret;
}

/*
 * Exposes device descriptor from the gadget driver.
 */
static ssize_t dev_desc_read(struct file *file, struct kobject *kobj,
			     struct bin_attribute *attr, char *out,
			     loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct vudc *udc = (struct vudc *)dev_get_drvdata(dev);
	char *desc_ptr = (char *) &udc->dev_desc;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&udc->lock, flags);
	if (!udc->desc_cached) {
		ret = -ENODEV;
		goto unlock;
	}

	memcpy(out, desc_ptr + off, count);
	ret = count;
unlock:
	spin_unlock_irqrestore(&udc->lock, flags);
	return ret;
}
static BIN_ATTR_RO(dev_desc, sizeof(struct usb_device_descriptor));

static ssize_t store_sockfd(struct device *dev, struct device_attribute *attr,
		     const char *in, size_t count)
{
	struct vudc *udc = (struct vudc *) dev_get_drvdata(dev);
	int rv;
	int sockfd = 0;
	int err;
	struct socket *socket;
	unsigned long flags;
	int ret;

	rv = kstrtoint(in, 0, &sockfd);
	if (rv != 0)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);
	/* Don't export what we don't have */
	if (!udc || !udc->driver || !udc->pullup) {
		dev_err(dev, "no device or gadget not bound");
		ret = -ENODEV;
		goto unlock;
	}

	if (sockfd != -1) {
		if (udc->connected) {
			dev_err(dev, "Device already connected");
			ret = -EBUSY;
			goto unlock;
		}

		spin_lock_irq(&udc->ud.lock);

		if (udc->ud.status != SDEV_ST_AVAILABLE) {
			ret = -EINVAL;
			goto unlock_ud;
		}

		socket = sockfd_lookup(sockfd, &err);
		if (!socket) {
			dev_err(dev, "failed to lookup sock");
			ret = -EINVAL;
			goto unlock_ud;
		}

		udc->ud.tcp_socket = socket;

		spin_unlock_irq(&udc->ud.lock);
		spin_unlock_irqrestore(&udc->lock, flags);

		udc->ud.tcp_rx = kthread_get_run(&v_rx_loop,
						    &udc->ud, "vudc_rx");
		udc->ud.tcp_tx = kthread_get_run(&v_tx_loop,
						    &udc->ud, "vudc_tx");

		spin_lock_irqsave(&udc->lock, flags);
		spin_lock_irq(&udc->ud.lock);
		udc->ud.status = SDEV_ST_USED;
		spin_unlock_irq(&udc->ud.lock);

		do_gettimeofday(&udc->start_time);
		v_start_timer(udc);
		udc->connected = 1;
	} else {
		if (!udc->connected) {
			dev_err(dev, "Device not connected");
			ret = -EINVAL;
			goto unlock;
		}

		spin_lock_irq(&udc->ud.lock);
		if (udc->ud.status != SDEV_ST_USED) {
			ret = -EINVAL;
			goto unlock_ud;
		}
		spin_unlock_irq(&udc->ud.lock);

		usbip_event_add(&udc->ud, VUDC_EVENT_DOWN);
	}

	spin_unlock_irqrestore(&udc->lock, flags);

	return count;

unlock_ud:
	spin_unlock_irq(&udc->ud.lock);
unlock:
	spin_unlock_irqrestore(&udc->lock, flags);

	return ret;
}
static DEVICE_ATTR(usbip_sockfd, S_IWUSR, NULL, store_sockfd);

static ssize_t usbip_status_show(struct device *dev,
			       struct device_attribute *attr, char *out)
{
	struct vudc *udc = (struct vudc *) dev_get_drvdata(dev);
	int status;

	if (!udc) {
		dev_err(dev, "no device");
		return -ENODEV;
	}
	spin_lock_irq(&udc->ud.lock);
	status = udc->ud.status;
	spin_unlock_irq(&udc->ud.lock);

	return snprintf(out, PAGE_SIZE, "%d\n", status);
}
static DEVICE_ATTR_RO(usbip_status);

static struct attribute *dev_attrs[] = {
	&dev_attr_usbip_sockfd.attr,
	&dev_attr_usbip_status.attr,
	NULL,
};

static struct bin_attribute *dev_bin_attrs[] = {
	&bin_attr_dev_desc,
	NULL,
};

const struct attribute_group vudc_attr_group = {
	.attrs = dev_attrs,
	.bin_attrs = dev_bin_attrs,
};
