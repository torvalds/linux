/*
 * Driver for Meywa-Denki & KAYAC YUREX
 *
 * Copyright (C) 2010 Tomoki Sekiyama (tomoki.sekiyama@gmail.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/hid.h>

#define DRIVER_AUTHOR "Tomoki Sekiyama"
#define DRIVER_DESC "Driver for Meywa-Denki & KAYAC YUREX"

#define YUREX_VENDOR_ID		0x0c45
#define YUREX_PRODUCT_ID	0x1010

#define CMD_ACK		'!'
#define CMD_ANIMATE	'A'
#define CMD_COUNT	'C'
#define CMD_LED		'L'
#define CMD_READ	'R'
#define CMD_SET		'S'
#define CMD_VERSION	'V'
#define CMD_EOF		0x0d
#define CMD_PADDING	0xff

#define YUREX_BUF_SIZE		8
#define YUREX_WRITE_TIMEOUT	(HZ*2)

/* table of devices that work with this driver */
static struct usb_device_id yurex_table[] = {
	{ USB_DEVICE(YUREX_VENDOR_ID, YUREX_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, yurex_table);

#ifdef CONFIG_USB_DYNAMIC_MINORS
#define YUREX_MINOR_BASE	0
#else
#define YUREX_MINOR_BASE	192
#endif

/* Structure to hold all of our device specific stuff */
struct usb_yurex {
	struct usb_device	*udev;
	struct usb_interface	*interface;
	__u8			int_in_endpointAddr;
	struct urb		*urb;		/* URB for interrupt in */
	unsigned char           *int_buffer;	/* buffer for intterupt in */
	struct urb		*cntl_urb;	/* URB for control msg */
	struct usb_ctrlrequest	*cntl_req;	/* req for control msg */
	unsigned char		*cntl_buffer;	/* buffer for control msg */

	struct kref		kref;
	struct mutex		io_mutex;
	struct fasync_struct	*async_queue;
	wait_queue_head_t	waitq;

	spinlock_t		lock;
	__s64			bbu;		/* BBU from device */
};
#define to_yurex_dev(d) container_of(d, struct usb_yurex, kref)

static struct usb_driver yurex_driver;
static const struct file_operations yurex_fops;


static void yurex_control_callback(struct urb *urb)
{
	struct usb_yurex *dev = urb->context;
	int status = urb->status;

	if (status) {
		dev_err(&urb->dev->dev, "%s - control failed: %d\n",
			__func__, status);
		wake_up_interruptible(&dev->waitq);
		return;
	}
	/* on success, sender woken up by CMD_ACK int in, or timeout */
}

static void yurex_delete(struct kref *kref)
{
	struct usb_yurex *dev = to_yurex_dev(kref);

	dev_dbg(&dev->interface->dev, "%s\n", __func__);

	usb_put_dev(dev->udev);
	if (dev->cntl_urb) {
		usb_kill_urb(dev->cntl_urb);
		kfree(dev->cntl_req);
		if (dev->cntl_buffer)
			usb_free_coherent(dev->udev, YUREX_BUF_SIZE,
				dev->cntl_buffer, dev->cntl_urb->transfer_dma);
		usb_free_urb(dev->cntl_urb);
	}
	if (dev->urb) {
		usb_kill_urb(dev->urb);
		if (dev->int_buffer)
			usb_free_coherent(dev->udev, YUREX_BUF_SIZE,
				dev->int_buffer, dev->urb->transfer_dma);
		usb_free_urb(dev->urb);
	}
	kfree(dev);
}

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver yurex_class = {
	.name =		"yurex%d",
	.fops =		&yurex_fops,
	.minor_base =	YUREX_MINOR_BASE,
};

static void yurex_interrupt(struct urb *urb)
{
	struct usb_yurex *dev = urb->context;
	unsigned char *buf = dev->int_buffer;
	int status = urb->status;
	unsigned long flags;
	int retval, i;

	switch (status) {
	case 0: /*success*/
		break;
	case -EOVERFLOW:
		dev_err(&dev->interface->dev,
			"%s - overflow with length %d, actual length is %d\n",
			__func__, YUREX_BUF_SIZE, dev->urb->actual_length);
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EILSEQ:
		/* The device is terminated, clean up */
		return;
	default:
		dev_err(&dev->interface->dev,
			"%s - unknown status received: %d\n", __func__, status);
		goto exit;
	}

	/* handle received message */
	switch (buf[0]) {
	case CMD_COUNT:
	case CMD_READ:
		if (buf[6] == CMD_EOF) {
			spin_lock_irqsave(&dev->lock, flags);
			dev->bbu = 0;
			for (i = 1; i < 6; i++) {
				dev->bbu += buf[i];
				if (i != 5)
					dev->bbu <<= 8;
			}
			dev_dbg(&dev->interface->dev, "%s count: %lld\n",
				__func__, dev->bbu);
			spin_unlock_irqrestore(&dev->lock, flags);

			kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
		}
		else
			dev_dbg(&dev->interface->dev,
				"data format error - no EOF\n");
		break;
	case CMD_ACK:
		dev_dbg(&dev->interface->dev, "%s ack: %c\n",
			__func__, buf[1]);
		wake_up_interruptible(&dev->waitq);
		break;
	}

exit:
	retval = usb_submit_urb(dev->urb, GFP_ATOMIC);
	if (retval) {
		dev_err(&dev->interface->dev, "%s - usb_submit_urb failed: %d\n",
			__func__, retval);
	}
}

static int yurex_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_yurex *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int retval = -ENOMEM;
	int i;
	DEFINE_WAIT(wait);

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}
	kref_init(&dev->kref);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->waitq);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint)) {
			dev->int_in_endpointAddr = endpoint->bEndpointAddress;
			break;
		}
	}
	if (!dev->int_in_endpointAddr) {
		retval = -ENODEV;
		dev_err(&interface->dev, "Could not find endpoints\n");
		goto error;
	}


	/* allocate control URB */
	dev->cntl_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->cntl_urb) {
		dev_err(&interface->dev, "Could not allocate control URB\n");
		goto error;
	}

	/* allocate buffer for control req */
	dev->cntl_req = kmalloc(YUREX_BUF_SIZE, GFP_KERNEL);
	if (!dev->cntl_req) {
		dev_err(&interface->dev, "Could not allocate cntl_req\n");
		goto error;
	}

	/* allocate buffer for control msg */
	dev->cntl_buffer = usb_alloc_coherent(dev->udev, YUREX_BUF_SIZE,
					      GFP_KERNEL,
					      &dev->cntl_urb->transfer_dma);
	if (!dev->cntl_buffer) {
		dev_err(&interface->dev, "Could not allocate cntl_buffer\n");
		goto error;
	}

	/* configure control URB */
	dev->cntl_req->bRequestType = USB_DIR_OUT | USB_TYPE_CLASS |
				      USB_RECIP_INTERFACE;
	dev->cntl_req->bRequest	= HID_REQ_SET_REPORT;
	dev->cntl_req->wValue	= cpu_to_le16((HID_OUTPUT_REPORT + 1) << 8);
	dev->cntl_req->wIndex	= cpu_to_le16(iface_desc->desc.bInterfaceNumber);
	dev->cntl_req->wLength	= cpu_to_le16(YUREX_BUF_SIZE);

	usb_fill_control_urb(dev->cntl_urb, dev->udev,
			     usb_sndctrlpipe(dev->udev, 0),
			     (void *)dev->cntl_req, dev->cntl_buffer,
			     YUREX_BUF_SIZE, yurex_control_callback, dev);
	dev->cntl_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;


	/* allocate interrupt URB */
	dev->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->urb) {
		dev_err(&interface->dev, "Could not allocate URB\n");
		goto error;
	}

	/* allocate buffer for interrupt in */
	dev->int_buffer = usb_alloc_coherent(dev->udev, YUREX_BUF_SIZE,
					GFP_KERNEL, &dev->urb->transfer_dma);
	if (!dev->int_buffer) {
		dev_err(&interface->dev, "Could not allocate int_buffer\n");
		goto error;
	}

	/* configure interrupt URB */
	usb_fill_int_urb(dev->urb, dev->udev,
			 usb_rcvintpipe(dev->udev, dev->int_in_endpointAddr),
			 dev->int_buffer, YUREX_BUF_SIZE, yurex_interrupt,
			 dev, 1);
	dev->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	if (usb_submit_urb(dev->urb, GFP_KERNEL)) {
		retval = -EIO;
		dev_err(&interface->dev, "Could not submitting URB\n");
		goto error;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &yurex_class);
	if (retval) {
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	dev->bbu = -1;

	dev_info(&interface->dev,
		 "USB YUREX device now attached to Yurex #%d\n",
		 interface->minor);

	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, yurex_delete);
	return retval;
}

static void yurex_disconnect(struct usb_interface *interface)
{
	struct usb_yurex *dev;
	int minor = interface->minor;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &yurex_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	/* wakeup waiters */
	kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	wake_up_interruptible(&dev->waitq);

	/* decrement our usage count */
	kref_put(&dev->kref, yurex_delete);

	dev_info(&interface->dev, "USB YUREX #%d now disconnected\n", minor);
}

static struct usb_driver yurex_driver = {
	.name =		"yurex",
	.probe =	yurex_probe,
	.disconnect =	yurex_disconnect,
	.id_table =	yurex_table,
};


static int yurex_fasync(int fd, struct file *file, int on)
{
	struct usb_yurex *dev;

	dev = (struct usb_yurex *)file->private_data;
	return fasync_helper(fd, file, on, &dev->async_queue);
}

static int yurex_open(struct inode *inode, struct file *file)
{
	struct usb_yurex *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&yurex_driver, subminor);
	if (!interface) {
		printk(KERN_ERR "%s - error, can't find device for minor %d",
		       __func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	mutex_lock(&dev->io_mutex);
	file->private_data = dev;
	mutex_unlock(&dev->io_mutex);

exit:
	return retval;
}

static int yurex_release(struct inode *inode, struct file *file)
{
	struct usb_yurex *dev;

	dev = (struct usb_yurex *)file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* decrement the count on our device */
	kref_put(&dev->kref, yurex_delete);
	return 0;
}

static ssize_t yurex_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct usb_yurex *dev;
	int retval = 0;
	int bytes_read = 0;
	char in_buffer[20];
	unsigned long flags;

	dev = (struct usb_yurex *)file->private_data;

	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* already disconnected */
		retval = -ENODEV;
		goto exit;
	}

	spin_lock_irqsave(&dev->lock, flags);
	bytes_read = snprintf(in_buffer, 20, "%lld\n", dev->bbu);
	spin_unlock_irqrestore(&dev->lock, flags);

	if (*ppos < bytes_read) {
		if (copy_to_user(buffer, in_buffer + *ppos, bytes_read - *ppos))
			retval = -EFAULT;
		else {
			retval = bytes_read - *ppos;
			*ppos += bytes_read;
		}
	}

exit:
	mutex_unlock(&dev->io_mutex);
	return retval;
}

static ssize_t yurex_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos)
{
	struct usb_yurex *dev;
	int i, set = 0, retval = 0;
	char buffer[16];
	char *data = buffer;
	unsigned long long c, c2 = 0;
	signed long timeout = 0;
	DEFINE_WAIT(wait);

	count = min(sizeof(buffer), count);
	dev = (struct usb_yurex *)file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto error;

	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* already disconnected */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

	if (copy_from_user(buffer, user_buffer, count)) {
		mutex_unlock(&dev->io_mutex);
		retval = -EFAULT;
		goto error;
	}
	memset(dev->cntl_buffer, CMD_PADDING, YUREX_BUF_SIZE);

	switch (buffer[0]) {
	case CMD_ANIMATE:
	case CMD_LED:
		dev->cntl_buffer[0] = buffer[0];
		dev->cntl_buffer[1] = buffer[1];
		dev->cntl_buffer[2] = CMD_EOF;
		break;
	case CMD_READ:
	case CMD_VERSION:
		dev->cntl_buffer[0] = buffer[0];
		dev->cntl_buffer[1] = 0x00;
		dev->cntl_buffer[2] = CMD_EOF;
		break;
	case CMD_SET:
		data++;
		/* FALL THROUGH */
	case '0' ... '9':
		set = 1;
		c = c2 = simple_strtoull(data, NULL, 0);
		dev->cntl_buffer[0] = CMD_SET;
		for (i = 1; i < 6; i++) {
			dev->cntl_buffer[i] = (c>>32) & 0xff;
			c <<= 8;
		}
		buffer[6] = CMD_EOF;
		break;
	default:
		mutex_unlock(&dev->io_mutex);
		return -EINVAL;
	}

	/* send the data as the control msg */
	prepare_to_wait(&dev->waitq, &wait, TASK_INTERRUPTIBLE);
	dev_dbg(&dev->interface->dev, "%s - submit %c\n", __func__,
		dev->cntl_buffer[0]);
	retval = usb_submit_urb(dev->cntl_urb, GFP_KERNEL);
	if (retval >= 0)
		timeout = schedule_timeout(YUREX_WRITE_TIMEOUT);
	finish_wait(&dev->waitq, &wait);

	mutex_unlock(&dev->io_mutex);

	if (retval < 0) {
		dev_err(&dev->interface->dev,
			"%s - failed to send bulk msg, error %d\n",
			__func__, retval);
		goto error;
	}
	if (set && timeout)
		dev->bbu = c2;
	return timeout ? count : -EIO;

error:
	return retval;
}

static const struct file_operations yurex_fops = {
	.owner =	THIS_MODULE,
	.read =		yurex_read,
	.write =	yurex_write,
	.open =		yurex_open,
	.release =	yurex_release,
	.fasync	=	yurex_fasync,
	.llseek =	default_llseek,
};

module_usb_driver(yurex_driver);

MODULE_LICENSE("GPL");
