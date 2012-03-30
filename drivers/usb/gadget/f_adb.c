/*
 * Gadget Driver for Android ADB
 *
 * Copyright (C) 2008 Google, Inc.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#define ADB_BULK_BUFFER_SIZE           4096

/* number of tx requests to allocate */
#define TX_REQ_MAX 4

static const char adb_shortname[] = "android_adb";

struct adb_dev {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	spinlock_t lock;

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;

	int online;
	int error;

	atomic_t read_excl;
	atomic_t write_excl;
	atomic_t open_excl;

	struct list_head tx_idle;

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	struct usb_request *rx_req;
	int rx_done;
};

static struct usb_interface_descriptor adb_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 2,
	.bInterfaceClass        = 0xFF,
	.bInterfaceSubClass     = 0x42,
	.bInterfaceProtocol     = 1,
};

static struct usb_endpoint_descriptor adb_highspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(64),
};

static struct usb_endpoint_descriptor adb_highspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = __constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor adb_fullspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor adb_fullspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_adb_descs[] = {
	(struct usb_descriptor_header *) &adb_interface_desc,
	(struct usb_descriptor_header *) &adb_fullspeed_in_desc,
	(struct usb_descriptor_header *) &adb_fullspeed_out_desc,
	NULL,
};

static struct usb_descriptor_header *hs_adb_descs[] = {
	(struct usb_descriptor_header *) &adb_interface_desc,
	(struct usb_descriptor_header *) &adb_highspeed_in_desc,
	(struct usb_descriptor_header *) &adb_highspeed_out_desc,
	NULL,
};


/* temporary variable used between adb_open() and adb_gadget_bind() */
static struct adb_dev *_adb_dev;

static inline struct adb_dev *func_to_adb(struct usb_function *f)
{
	return container_of(f, struct adb_dev, function);
}


static struct usb_request *adb_request_new(struct usb_ep *ep, int buffer_size)
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

static void adb_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static inline int adb_lock(atomic_t *excl)
{
	if (atomic_inc_return(excl) == 1) {
		return 0;
	} else {
		atomic_dec(excl);
		return -1;
	}
}

static inline void adb_unlock(atomic_t *excl)
{
	atomic_dec(excl);
}

/* add a request to the tail of a list */
void adb_req_put(struct adb_dev *dev, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	list_add_tail(&req->list, head);
	spin_unlock_irqrestore(&dev->lock, flags);
}

/* remove a request from the head of a list */
struct usb_request *adb_req_get(struct adb_dev *dev, struct list_head *head)
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

static void adb_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct adb_dev *dev = _adb_dev;

	if (req->status != 0)
		dev->error = 1;

	adb_req_put(dev, &dev->tx_idle, req);

	wake_up(&dev->write_wq);
}

static void adb_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct adb_dev *dev = _adb_dev;

	dev->rx_done = 1;
	if (req->status != 0)
		dev->error = 1;

	wake_up(&dev->read_wq);
}

static int adb_create_bulk_endpoints(struct adb_dev *dev,
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
	DBG(cdev, "usb_ep_autoconfig for adb ep_out got %s\n", ep->name);
	ep->driver_data = dev;		/* claim the endpoint */
	dev->ep_out = ep;

	/* now allocate requests for our endpoints */
	req = adb_request_new(dev->ep_out, ADB_BULK_BUFFER_SIZE);
	if (!req)
		goto fail;
	req->complete = adb_complete_out;
	dev->rx_req = req;

	for (i = 0; i < TX_REQ_MAX; i++) {
		req = adb_request_new(dev->ep_in, ADB_BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->complete = adb_complete_in;
		adb_req_put(dev, &dev->tx_idle, req);
	}

	return 0;

fail:
	printk(KERN_ERR "adb_bind() could not allocate requests\n");
	return -1;
}

static ssize_t adb_read(struct file *fp, char __user *buf,
				size_t count, loff_t *pos)
{
	struct adb_dev *dev = fp->private_data;
	struct usb_request *req;
	int r = count, xfer;
	int ret;

	pr_debug("adb_read(%d)\n", count);
	if (!_adb_dev)
		return -ENODEV;

	if (count > ADB_BULK_BUFFER_SIZE)
		return -EINVAL;

	if (adb_lock(&dev->read_excl))
		return -EBUSY;

	/* we will block until we're online */
	while (!(dev->online || dev->error)) {
		pr_debug("adb_read: waiting for online state\n");
		ret = wait_event_interruptible(dev->read_wq,
				(dev->online || dev->error));
		if (ret < 0) {
			adb_unlock(&dev->read_excl);
			return ret;
		}
	}
	if (dev->error) {
		r = -EIO;
		goto done;
	}

requeue_req:
	/* queue a request */
	req = dev->rx_req;
	req->length = count;
	dev->rx_done = 0;
	ret = usb_ep_queue(dev->ep_out, req, GFP_ATOMIC);
	if (ret < 0) {
		pr_debug("adb_read: failed to queue req %p (%d)\n", req, ret);
		r = -EIO;
		dev->error = 1;
		goto done;
	} else {
		pr_debug("rx %p queue\n", req);
	}

	/* wait for a request to complete */
	ret = wait_event_interruptible(dev->read_wq, dev->rx_done);
	if (ret < 0) {
		dev->error = 1;
		r = ret;
		usb_ep_dequeue(dev->ep_out, req);
		goto done;
	}
	if (!dev->error) {
		/* If we got a 0-len packet, throw it back and try again. */
		if (req->actual == 0)
			goto requeue_req;

		pr_debug("rx %p %d\n", req, req->actual);
		xfer = (req->actual < count) ? req->actual : count;
		if (copy_to_user(buf, req->buf, xfer))
			r = -EFAULT;

	} else
		r = -EIO;

done:
	adb_unlock(&dev->read_excl);
	pr_debug("adb_read returning %d\n", r);
	return r;
}

static ssize_t adb_write(struct file *fp, const char __user *buf,
				 size_t count, loff_t *pos)
{
	struct adb_dev *dev = fp->private_data;
	struct usb_request *req = 0;
	int r = count, xfer;
	int ret;

	if (!_adb_dev)
		return -ENODEV;
	pr_debug("adb_write(%d)\n", count);

	if (adb_lock(&dev->write_excl))
		return -EBUSY;

	while (count > 0) {
		if (dev->error) {
			pr_debug("adb_write dev->error\n");
			r = -EIO;
			break;
		}

		/* get an idle tx request to use */
		req = 0;
		ret = wait_event_interruptible(dev->write_wq,
			(req = adb_req_get(dev, &dev->tx_idle)) || dev->error);

		if (ret < 0) {
			r = ret;
			break;
		}

		if (req != 0) {
			if (count > ADB_BULK_BUFFER_SIZE)
				xfer = ADB_BULK_BUFFER_SIZE;
			else
				xfer = count;
			if (copy_from_user(req->buf, buf, xfer)) {
				r = -EFAULT;
				break;
			}

			req->length = xfer;
			ret = usb_ep_queue(dev->ep_in, req, GFP_ATOMIC);
			if (ret < 0) {
				pr_debug("adb_write: xfer error %d\n", ret);
				dev->error = 1;
				r = -EIO;
				break;
			}

			buf += xfer;
			count -= xfer;

			/* zero this so we don't try to free it on error exit */
			req = 0;
		}
	}

	if (req)
		adb_req_put(dev, &dev->tx_idle, req);

	adb_unlock(&dev->write_excl);
	pr_debug("adb_write returning %d\n", r);
	return r;
}

static int adb_open(struct inode *ip, struct file *fp)
{
	printk(KERN_INFO "adb_open\n");
	if (!_adb_dev)
		return -ENODEV;

	if (adb_lock(&_adb_dev->open_excl))
		return -EBUSY;

	fp->private_data = _adb_dev;

	/* clear the error latch */
	_adb_dev->error = 0;

	return 0;
}

static int adb_release(struct inode *ip, struct file *fp)
{
	printk(KERN_INFO "adb_release\n");
	adb_unlock(&_adb_dev->open_excl);
	return 0;
}

/* file operations for ADB device /dev/android_adb */
static struct file_operations adb_fops = {
	.owner = THIS_MODULE,
	.read = adb_read,
	.write = adb_write,
	.open = adb_open,
	.release = adb_release,
};

static struct miscdevice adb_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = adb_shortname,
	.fops = &adb_fops,
};




static int
adb_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct adb_dev	*dev = func_to_adb(f);
	int			id;
	int			ret;

	dev->cdev = cdev;
	DBG(cdev, "adb_function_bind dev: %p\n", dev);

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	adb_interface_desc.bInterfaceNumber = id;

	/* allocate endpoints */
	ret = adb_create_bulk_endpoints(dev, &adb_fullspeed_in_desc,
			&adb_fullspeed_out_desc);
	if (ret)
		return ret;

	/* support high speed hardware */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		adb_highspeed_in_desc.bEndpointAddress =
			adb_fullspeed_in_desc.bEndpointAddress;
		adb_highspeed_out_desc.bEndpointAddress =
			adb_fullspeed_out_desc.bEndpointAddress;
	}

	DBG(cdev, "%s speed %s: IN/%s, OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			f->name, dev->ep_in->name, dev->ep_out->name);
	return 0;
}

static void
adb_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct adb_dev	*dev = func_to_adb(f);
	struct usb_request *req;


	dev->online = 0;
	dev->error = 1;

	wake_up(&dev->read_wq);

	adb_request_free(dev->rx_req, dev->ep_out);
	while ((req = adb_req_get(dev, &dev->tx_idle)))
		adb_request_free(req, dev->ep_in);
}

static int adb_function_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct adb_dev	*dev = func_to_adb(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	DBG(cdev, "adb_function_set_alt intf: %d alt: %d\n", intf, alt);
	ret = usb_ep_enable(dev->ep_in,
			ep_choose(cdev->gadget,
				&adb_highspeed_in_desc,
				&adb_fullspeed_in_desc));
	if (ret)
		return ret;
	ret = usb_ep_enable(dev->ep_out,
			ep_choose(cdev->gadget,
				&adb_highspeed_out_desc,
				&adb_fullspeed_out_desc));
	if (ret) {
		usb_ep_disable(dev->ep_in);
		return ret;
	}
	dev->online = 1;

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);
	return 0;
}

static void adb_function_disable(struct usb_function *f)
{
	struct adb_dev	*dev = func_to_adb(f);
	struct usb_composite_dev	*cdev = dev->cdev;

	DBG(cdev, "adb_function_disable cdev %p\n", cdev);
	dev->online = 0;
	dev->error = 1;
	usb_ep_disable(dev->ep_in);
	dev->ep_in->driver_data = NULL;
	usb_ep_disable(dev->ep_out);
	dev->ep_out->driver_data = NULL;

	/* readers may be blocked waiting for us to go online */
	wake_up(&dev->read_wq);

	VDBG(cdev, "%s disabled\n", dev->function.name);
}

static int adb_bind_config(struct usb_configuration *c)
{
	struct adb_dev *dev = _adb_dev;

	dev->cdev = c->cdev;
	dev->function.name = "adb";
	dev->function.descriptors = fs_adb_descs;
	dev->function.hs_descriptors = hs_adb_descs;
	dev->function.bind = adb_function_bind;
	dev->function.unbind = adb_function_unbind;
	dev->function.set_alt = adb_function_set_alt;
	dev->function.disable = adb_function_disable;

	return usb_add_function(c, &dev->function);
}

static int adb_setup(void)
{
	struct adb_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->lock);

	init_waitqueue_head(&dev->read_wq);
	init_waitqueue_head(&dev->write_wq);

	atomic_set(&dev->open_excl, 0);
	atomic_set(&dev->read_excl, 0);
	atomic_set(&dev->write_excl, 0);

	INIT_LIST_HEAD(&dev->tx_idle);

	_adb_dev = dev;

	ret = misc_register(&adb_device);
	if (ret)
		goto err;

	return 0;

err:
	kfree(dev);
	printk(KERN_ERR "adb gadget driver failed to initialize\n");
	return ret;
}

static void adb_cleanup(void)
{
	misc_deregister(&adb_device);

	kfree(_adb_dev);
	_adb_dev = NULL;
}
