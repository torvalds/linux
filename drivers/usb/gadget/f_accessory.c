/*
 * Gadget Function Driver for Android USB accessories
 *
 * Copyright (C) 2011 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <linux/types.h>
#include <linux/file.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/f_accessory.h>

#define BULK_BUFFER_SIZE    16384
#define ACC_STRING_SIZE     256

#define PROTOCOL_VERSION    1

/* String IDs */
#define INTERFACE_STRING_INDEX	0

/* number of tx and rx requests to allocate */
#define TX_REQ_MAX 4
#define RX_REQ_MAX 2

struct acc_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	spinlock_t lock;

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;

	/* set to 1 when we connect */
	int online:1;
	/* Set to 1 when we disconnect.
	 * Not cleared until our file is closed.
	 */
	int disconnected:1;

	/* strings sent by the host */
	char manufacturer[ACC_STRING_SIZE];
	char model[ACC_STRING_SIZE];
	char description[ACC_STRING_SIZE];
	char version[ACC_STRING_SIZE];
	char uri[ACC_STRING_SIZE];
	char serial[ACC_STRING_SIZE];

	/* for acc_complete_set_string */
	int string_index;

	/* set to 1 if we have a pending start request */
	int start_requested;

	/* synchronize access to our device file */
	atomic_t open_excl;

	struct list_head tx_idle;

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	struct usb_request *rx_req[RX_REQ_MAX];
	int rx_done;
	struct delayed_work work;
};

static struct usb_interface_descriptor acc_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 2,
	.bInterfaceClass        = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass     = USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol     = 0,
};

static struct usb_endpoint_descriptor acc_highspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor acc_highspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor acc_fullspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor acc_fullspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_acc_descs[] = {
	(struct usb_descriptor_header *) &acc_interface_desc,
	(struct usb_descriptor_header *) &acc_fullspeed_in_desc,
	(struct usb_descriptor_header *) &acc_fullspeed_out_desc,
	NULL,
};

static struct usb_descriptor_header *hs_acc_descs[] = {
	(struct usb_descriptor_header *) &acc_interface_desc,
	(struct usb_descriptor_header *) &acc_highspeed_in_desc,
	(struct usb_descriptor_header *) &acc_highspeed_out_desc,
	NULL,
};

static struct usb_string acc_string_defs[] = {
	[INTERFACE_STRING_INDEX].s	= "Android Accessory Interface",
	{  },	/* end of list */
};

static struct usb_gadget_strings acc_string_table = {
	.language		= 0x0409,	/* en-US */
	.strings		= acc_string_defs,
};

static struct usb_gadget_strings *acc_strings[] = {
	&acc_string_table,
	NULL,
};

/* temporary variable used between acc_open() and acc_gadget_bind() */
static struct acc_dev *_acc_dev;

static inline struct acc_dev *func_to_dev(struct usb_function *f)
{
	return container_of(f, struct acc_dev, function);
}

static struct usb_request *acc_request_new(struct usb_ep *ep, int buffer_size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req)
		return NULL;

	/* now allocate buffers for the requests */
	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void acc_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

/* add a request to the tail of a list */
static void req_put(struct acc_dev *dev, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* remove a request from the head of a list */
static struct usb_request *req_get(struct acc_dev *dev, struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req;

	spin_lock_irqsave(&dev->lock, flags);
	if (list_empty(head)) {
		req = 0;
	} else {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	return req;
}

static void acc_set_disconnected(struct acc_dev *dev)
{
	dev->online = 0;
	dev->disconnected = 1;
}

static void acc_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct acc_dev *dev = _acc_dev;

	if (req->status != 0)
		acc_set_disconnected(dev);

	req_put(dev, &dev->tx_idle, req);

	wake_up(&dev->write_wq);
}

static void acc_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct acc_dev *dev = _acc_dev;

	dev->rx_done = 1;
	if (req->status != 0)
		acc_set_disconnected(dev);

	wake_up(&dev->read_wq);
}

static void acc_complete_set_string(struct usb_ep *ep, struct usb_request *req)
{
	struct acc_dev	*dev = ep->driver_data;
	char *string_dest = NULL;
	int length = req->actual;

	if (req->status != 0) {
		pr_err("acc_complete_set_string, err %d\n", req->status);
		return;
	}

	switch (dev->string_index) {
	case ACCESSORY_STRING_MANUFACTURER:
		string_dest = dev->manufacturer;
		break;
	case ACCESSORY_STRING_MODEL:
		string_dest = dev->model;
		break;
	case ACCESSORY_STRING_DESCRIPTION:
		string_dest = dev->description;
		break;
	case ACCESSORY_STRING_VERSION:
		string_dest = dev->version;
		break;
	case ACCESSORY_STRING_URI:
		string_dest = dev->uri;
		break;
	case ACCESSORY_STRING_SERIAL:
		string_dest = dev->serial;
		break;
	}
	if (string_dest) {
		unsigned long flags;

		if (length >= ACC_STRING_SIZE)
			length = ACC_STRING_SIZE - 1;

		spin_lock_irqsave(&dev->lock, flags);
		memcpy(string_dest, req->buf, length);
		/* ensure zero termination */
		string_dest[length] = 0;
		spin_unlock_irqrestore(&dev->lock, flags);
	} else {
		pr_err("unknown accessory string index %d\n",
			dev->string_index);
	}
}

static int __init create_bulk_endpoints(struct acc_dev *dev,
				struct usb_endpoint_descriptor *in_desc,
				struct usb_endpoint_descriptor *out_desc)
{
	struct usb_composite_dev *cdev = dev->cdev;
	struct usb_request *req;
	struct usb_ep *ep;
	int i;

	DBG(cdev, "create_bulk_endpoints dev: %p\n", dev);

	ep = usb_ep_autoconfig(cdev->gadget, in_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_in failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_in got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		DBG(cdev, "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	DBG(cdev, "usb_ep_autoconfig for ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	/* now allocate requests for our endpoints */
	for (i = 0; i < TX_REQ_MAX; i++) {
		req = acc_request_new(dev->ep_in, BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = acc_complete_in;
		req_put(dev, &dev->tx_idle, req);
	}
	for (i = 0; i < RX_REQ_MAX; i++) {
		req = acc_request_new(dev->ep_out, BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = acc_complete_out;
		dev->rx_req[i] = req;
	}

	return 0;

fail:
	printk(KERN_ERR "acc_bind() could not allocate requests\n");
	while ((req = req_get(dev, &dev->tx_idle)))
		acc_request_free(req, dev->ep_in);
	for (i = 0; i < RX_REQ_MAX; i++)
		acc_request_free(dev->rx_req[i], dev->ep_out);
	return -1;
}

static ssize_t acc_read(struct file *fp, char __user *buf,
	size_t count, loff_t *pos)
{
	struct acc_dev *dev = fp->private_data;
	struct usb_request *req;
	int r = count, xfer;
	int ret = 0;

	pr_debug("acc_read(%d)\n", count);

	if (dev->disconnected)
		return -ENODEV;

	if (count > BULK_BUFFER_SIZE)
		count = BULK_BUFFER_SIZE;

	/* we will block until we're online */
	pr_debug("acc_read: waiting for online\n");
	ret = wait_event_interruptible(dev->read_wq, dev->online);
	if (ret < 0) {
		r = ret;
		goto done;
	}

requeue_req:
	/* queue a request */
	req = dev->rx_req[0];
	req->length = count;
	dev->rx_done = 0;
	ret = usb_ep_queue(dev->ep_out, req, GFP_KERNEL);
	if (ret < 0) {
		r = -EIO;
		goto done;
	} else {
		pr_debug("rx %p queue\n", req);
	}

	/* wait for a request to complete */
	ret = wait_event_interruptible(dev->read_wq, dev->rx_done);
	if (ret < 0) {
		r = ret;
		usb_ep_dequeue(dev->ep_out, req);
		goto done;
	}
	if (dev->online) {
		/* If we got a 0-len packet, throw it back and try again. */
		if (req->actual == 0)
			goto requeue_req;

		pr_debug("rx %p %d\n", req, req->actual);
		xfer = (req->actual < count) ? req->actual : count;
		r = xfer;
		if (copy_to_user(buf, req->buf, xfer))
			r = -EFAULT;
	} else
		r = -EIO;

done:
	pr_debug("acc_read returning %d\n", r);
	return r;
}

static ssize_t acc_write(struct file *fp, const char __user *buf,
	size_t count, loff_t *pos)
{
	struct acc_dev *dev = fp->private_data;
	struct usb_request *req = 0;
	int r = count, xfer;
	int ret;

	pr_debug("acc_write(%d)\n", count);

	if (!dev->online || dev->disconnected)
		return -ENODEV;

	while (count > 0) {
		if (!dev->online) {
			pr_debug("acc_write dev->error\n");
			r = -EIO;
			break;
		}

		/* get an idle tx request to use */
		req = 0;
		ret = wait_event_interruptible(dev->write_wq,
			((req = req_get(dev, &dev->tx_idle)) || !dev->online));
		if (!req) {
			r = ret;
			break;
		}

		if (count > BULK_BUFFER_SIZE)
			xfer = BULK_BUFFER_SIZE;
		else
			xfer = count;
		if (copy_from_user(req->buf, buf, xfer)) {
			r = -EFAULT;
			break;
		}

		req->length = xfer;
		ret = usb_ep_queue(dev->ep_in, req, GFP_KERNEL);
		if (ret < 0) {
			pr_debug("acc_write: xfer error %d\n", ret);
			r = -EIO;
			break;
		}

		buf += xfer;
		count -= xfer;

		/* zero this so we don't try to free it on error exit */
		req = 0;
	}

	if (req)
		req_put(dev, &dev->tx_idle, req);

	pr_debug("acc_write returning %d\n", r);
	return r;
}

static long acc_ioctl(struct file *fp, unsigned code, unsigned long value)
{
	struct acc_dev *dev = fp->private_data;
	char *src = NULL;
	int ret;

	switch (code) {
	case ACCESSORY_GET_STRING_MANUFACTURER:
		src = dev->manufacturer;
		break;
	case ACCESSORY_GET_STRING_MODEL:
		src = dev->model;
		break;
	case ACCESSORY_GET_STRING_DESCRIPTION:
		src = dev->description;
		break;
	case ACCESSORY_GET_STRING_VERSION:
		src = dev->version;
		break;
	case ACCESSORY_GET_STRING_URI:
		src = dev->uri;
		break;
	case ACCESSORY_GET_STRING_SERIAL:
		src = dev->serial;
		break;
	case ACCESSORY_IS_START_REQUESTED:
		return dev->start_requested;
	}
	if (!src)
		return -EINVAL;

	ret = strlen(src) + 1;
	if (copy_to_user((void __user *)value, src, ret))
		ret = -EFAULT;
	return ret;
}

static int acc_open(struct inode *ip, struct file *fp)
{
	printk(KERN_INFO "acc_open\n");
	if (atomic_xchg(&_acc_dev->open_excl, 1))
		return -EBUSY;

	_acc_dev->disconnected = 0;
	fp->private_data = _acc_dev;
	return 0;
}

static int acc_release(struct inode *ip, struct file *fp)
{
	printk(KERN_INFO "acc_release\n");

	WARN_ON(!atomic_xchg(&_acc_dev->open_excl, 0));
	_acc_dev->disconnected = 0;
	return 0;
}

/* file operations for /dev/acc_usb */
static const struct file_operations acc_fops = {
	.owner = THIS_MODULE,
	.read = acc_read,
	.write = acc_write,
	.unlocked_ioctl = acc_ioctl,
	.open = acc_open,
	.release = acc_release,
};

static struct miscdevice acc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "usb_accessory",
	.fops = &acc_fops,
};


static int acc_ctrlrequest(struct usb_composite_dev *cdev,
				const struct usb_ctrlrequest *ctrl)
{
	struct acc_dev	*dev = _acc_dev;
	int	value = -EOPNOTSUPP;
	u8 b_requestType = ctrl->bRequestType;
	u8 b_request = ctrl->bRequest;
	u16	w_index = le16_to_cpu(ctrl->wIndex);
	u16	w_value = le16_to_cpu(ctrl->wValue);
	u16	w_length = le16_to_cpu(ctrl->wLength);

/*
	printk(KERN_INFO "acc_ctrlrequest "
			"%02x.%02x v%04x i%04x l%u\n",
			b_requestType, b_request,
			w_value, w_index, w_length);
*/

	if (b_requestType == (USB_DIR_OUT | USB_TYPE_VENDOR)) {
		if (b_request == ACCESSORY_START) {
			dev->start_requested = 1;
			schedule_delayed_work(
				&dev->work, msecs_to_jiffies(10));
			value = 0;
		} else if (b_request == ACCESSORY_SEND_STRING) {
			dev->string_index = w_index;
			cdev->gadget->ep0->driver_data = dev;
			cdev->req->complete = acc_complete_set_string;
			value = w_length;
		}
	} else if (b_requestType == (USB_DIR_IN | USB_TYPE_VENDOR)) {
		if (b_request == ACCESSORY_GET_PROTOCOL) {
			*((u16 *)cdev->req->buf) = PROTOCOL_VERSION;
			value = sizeof(u16);

			/* clear any strings left over from a previous session */
			memset(dev->manufacturer, 0, sizeof(dev->manufacturer));
			memset(dev->model, 0, sizeof(dev->model));
			memset(dev->description, 0, sizeof(dev->description));
			memset(dev->version, 0, sizeof(dev->version));
			memset(dev->uri, 0, sizeof(dev->uri));
			memset(dev->serial, 0, sizeof(dev->serial));
			dev->start_requested = 0;
		}
	}

	if (value >= 0) {
		cdev->req->zero = 0;
		cdev->req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "%s setup response queue error\n",
				__func__);
	}

	if (value == -EOPNOTSUPP)
		VDBG(cdev,
			"unknown class-specific control req "
			"%02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	return value;
}

static int
acc_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct acc_dev	*dev = func_to_dev(f);
	int			id;
	int			ret;

	DBG(cdev, "acc_function_bind dev: %p\n", dev);

	dev->start_requested = 0;

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	acc_interface_desc.bInterfaceNumber = id;

	/* allocate endpoints */
	ret = create_bulk_endpoints(dev, &acc_fullspeed_in_desc,
			&acc_fullspeed_out_desc);
	if (ret)
		return ret;

	/* support high speed hardware */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		acc_highspeed_in_desc.bEndpointAddress =
			acc_fullspeed_in_desc.bEndpointAddress;
		acc_highspeed_out_desc.bEndpointAddress =
			acc_fullspeed_out_desc.bEndpointAddress;
	}

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			f->name, dev->ep_in->name, dev->ep_out->name);
	return 0;
}

static void
acc_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct acc_dev	*dev = func_to_dev(f);
	struct usb_request *req;
	int i;

	while ((req = req_get(dev, &dev->tx_idle)))
		acc_request_free(req, dev->ep_in);
	for (i = 0; i < RX_REQ_MAX; i++)
		acc_request_free(dev->rx_req[i], dev->ep_out);
}

static void acc_work(struct work_struct *data)
{
	char *envp[2] = { "ACCESSORY=START", NULL };
	kobject_uevent_env(&acc_device.this_device->kobj, KOBJ_CHANGE, envp);
}

static int acc_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct acc_dev	*dev = func_to_dev(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	DBG(cdev, "acc_function_set_alt intf: %d alt: %d\n", intf, alt);
	ret = usb_ep_enable(dev->ep_in,
			ep_choose(cdev->gadget,
				&acc_highspeed_in_desc,
				&acc_fullspeed_in_desc));
	if (ret)
		return ret;
	ret = usb_ep_enable(dev->ep_out,
			ep_choose(cdev->gadget,
				&acc_highspeed_out_desc,
				&acc_fullspeed_out_desc));
	if (ret) {
		usb_ep_disable(dev->ep_in);
		return ret;
	}

	dev->online = 1;

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);
	return 0;
}

static void acc_function_disable(struct usb_function *f)
{
	struct acc_dev	*dev = func_to_dev(f);
	struct usb_composite_dev	*cdev = dev->cdev;

	DBG(cdev, "acc_function_disable\n");
	acc_set_disconnected(dev);
	usb_ep_disable(dev->ep_in);
	usb_ep_disable(dev->ep_out);

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);

	VDBG(cdev, "%s disabled\n", dev->function.name);
}

static int acc_bind_config(struct usb_configuration *c)
{
	struct acc_dev *dev = _acc_dev;
	int ret;

	printk(KERN_INFO "acc_bind_config\n");

	/* allocate a string ID for our interface */
	if (acc_string_defs[INTERFACE_STRING_INDEX].id == 0) {
		ret = usb_string_id(c->cdev);
		if (ret < 0)
			return ret;
		acc_string_defs[INTERFACE_STRING_INDEX].id = ret;
		acc_interface_desc.iInterface = ret;
	}

	dev->cdev = c->cdev;
	dev->function.name = "accessory";
	dev->function.strings = acc_strings,
	dev->function.descriptors = fs_acc_descs;
	dev->function.hs_descriptors = hs_acc_descs;
	dev->function.bind = acc_function_bind;
	dev->function.unbind = acc_function_unbind;
	dev->function.set_alt = acc_function_set_alt;
	dev->function.disable = acc_function_disable;

	return usb_add_function(c, &dev->function);
}

static int acc_setup(void)
{
	struct acc_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);
	atomic_set(&dev->open_excl, 0);
	INIT_LIST_HEAD(&dev->tx_idle);
	INIT_DELAYED_WORK(&dev->work, acc_work);

	/* _acc_dev must be set before calling usb_gadget_register_driver */
	_acc_dev = dev;

	ret = misc_register(&acc_device);
	if (ret)
		goto err;

	return 0;

err:
	kfree(dev);
	printk(KERN_ERR "USB accessory gadget driver failed to initialize\n");
	return ret;
}

static void acc_cleanup(void)
{
	misc_deregister(&acc_device);
	kfree(_acc_dev);
	_acc_dev = NULL;
}
