/*
 * printer.c -- Printer gadget driver
 *
 * Copyright (C) 2003-2005 David Brownell
 * Copyright (C) 2006 Craig W. Nadler
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/cdev.h>

#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/g_printer.h>

#include "gadget_chips.h"


/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "composite.c"

/*-------------------------------------------------------------------------*/

#define DRIVER_DESC		"Printer Gadget"
#define DRIVER_VERSION		"2007 OCT 06"

static DEFINE_MUTEX(printer_mutex);
static const char shortname [] = "printer";
static const char driver_desc [] = DRIVER_DESC;

static dev_t g_printer_devno;

static struct class *usb_gadget_class;

/*-------------------------------------------------------------------------*/

struct printer_dev {
	spinlock_t		lock;		/* lock this structure */
	/* lock buffer lists during read/write calls */
	struct mutex		lock_printer_io;
	struct usb_gadget	*gadget;
	s8			interface;
	struct usb_ep		*in_ep, *out_ep;

	struct list_head	rx_reqs;	/* List of free RX structs */
	struct list_head	rx_reqs_active;	/* List of Active RX xfers */
	struct list_head	rx_buffers;	/* List of completed xfers */
	/* wait until there is data to be read. */
	wait_queue_head_t	rx_wait;
	struct list_head	tx_reqs;	/* List of free TX structs */
	struct list_head	tx_reqs_active; /* List of Active TX xfers */
	/* Wait until there are write buffers available to use. */
	wait_queue_head_t	tx_wait;
	/* Wait until all write buffers have been sent. */
	wait_queue_head_t	tx_flush_wait;
	struct usb_request	*current_rx_req;
	size_t			current_rx_bytes;
	u8			*current_rx_buf;
	u8			printer_status;
	u8			reset_printer;
	struct cdev		printer_cdev;
	struct device		*pdev;
	u8			printer_cdev_open;
	wait_queue_head_t	wait;
	struct usb_function	function;
};

static struct printer_dev usb_printer_gadget;

/*-------------------------------------------------------------------------*/

/* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
 * Instead:  allocate your own, using normal USB-IF procedures.
 */

/* Thanks to NetChip Technologies for donating this product ID.
 */
#define PRINTER_VENDOR_NUM	0x0525		/* NetChip */
#define PRINTER_PRODUCT_NUM	0xa4a8		/* Linux-USB Printer Gadget */

/* Some systems will want different product identifiers published in the
 * device descriptor, either numbers or strings or both.  These string
 * parameters are in UTF-8 (superset of ASCII's 7 bit characters).
 */

static char *iSerialNum;
module_param(iSerialNum, charp, S_IRUGO);
MODULE_PARM_DESC(iSerialNum, "1");

static char *iPNPstring;
module_param(iPNPstring, charp, S_IRUGO);
MODULE_PARM_DESC(iPNPstring, "MFG:linux;MDL:g_printer;CLS:PRINTER;SN:1;");

/* Number of requests to allocate per endpoint, not used for ep0. */
static unsigned qlen = 10;
module_param(qlen, uint, S_IRUGO|S_IWUSR);

#define QLEN	qlen

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

#define STRING_MANUFACTURER		0
#define STRING_PRODUCT			1
#define STRING_SERIALNUM		2

/* holds our biggest descriptor */
#define USB_DESC_BUFSIZE		256
#define USB_BUFSIZE			8192

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	.idVendor =		cpu_to_le16(PRINTER_VENDOR_NUM),
	.idProduct =		cpu_to_le16(PRINTER_PRODUCT_NUM),
	.bNumConfigurations =	1
};

static struct usb_interface_descriptor intf_desc = {
	.bLength =		sizeof intf_desc,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_PRINTER,
	.bInterfaceSubClass =	1,	/* Printer Sub-Class */
	.bInterfaceProtocol =	2,	/* Bi-Directional */
	.iInterface =		0
};

static struct usb_endpoint_descriptor fs_ep_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK
};

static struct usb_endpoint_descriptor fs_ep_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK
};

static struct usb_descriptor_header *fs_printer_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &fs_ep_in_desc,
	(struct usb_descriptor_header *) &fs_ep_out_desc,
	NULL
};

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 */

static struct usb_endpoint_descriptor hs_ep_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512)
};

static struct usb_endpoint_descriptor hs_ep_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512)
};

static struct usb_qualifier_descriptor dev_qualifier = {
	.bLength =		sizeof dev_qualifier,
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,
	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PRINTER,
	.bNumConfigurations =	1
};

static struct usb_descriptor_header *hs_printer_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &hs_ep_in_desc,
	(struct usb_descriptor_header *) &hs_ep_out_desc,
	NULL
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =              sizeof otg_descriptor,
	.bDescriptorType =      USB_DT_OTG,
	.bmAttributes =         USB_OTG_SRP,
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};

/* maxpacket and other transfer characteristics vary by speed. */
#define ep_desc(g, hs, fs) (((g)->speed == USB_SPEED_HIGH)?(hs):(fs))

/*-------------------------------------------------------------------------*/

/* descriptors that are built on-demand */

static char				manufacturer [50];
static char				product_desc [40] = DRIVER_DESC;
static char				serial_num [40] = "1";
static char				pnp_string [1024] =
	"XXMFG:linux;MDL:g_printer;CLS:PRINTER;SN:1;";

/* static strings, in UTF-8 */
static struct usb_string		strings [] = {
	[STRING_MANUFACTURER].s = manufacturer,
	[STRING_PRODUCT].s = product_desc,
	[STRING_SERIALNUM].s =	serial_num,
	{  }		/* end of list */
};

static struct usb_gadget_strings	stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

/*-------------------------------------------------------------------------*/

static struct usb_request *
printer_req_alloc(struct usb_ep *ep, unsigned len, gfp_t gfp_flags)
{
	struct usb_request	*req;

	req = usb_ep_alloc_request(ep, gfp_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(len, gfp_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

static void
printer_req_free(struct usb_ep *ep, struct usb_request *req)
{
	if (ep != NULL && req != NULL) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

/*-------------------------------------------------------------------------*/

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct printer_dev	*dev = ep->driver_data;
	int			status = req->status;
	unsigned long		flags;

	spin_lock_irqsave(&dev->lock, flags);

	list_del_init(&req->list);	/* Remode from Active List */

	switch (status) {

	/* normal completion */
	case 0:
		if (req->actual > 0) {
			list_add_tail(&req->list, &dev->rx_buffers);
			DBG(dev, "G_Printer : rx length %d\n", req->actual);
		} else {
			list_add(&req->list, &dev->rx_reqs);
		}
		break;

	/* software-driven interface shutdown */
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		VDBG(dev, "rx shutdown, code %d\n", status);
		list_add(&req->list, &dev->rx_reqs);
		break;

	/* for hardware automagic (such as pxa) */
	case -ECONNABORTED:		/* endpoint reset */
		DBG(dev, "rx %s reset\n", ep->name);
		list_add(&req->list, &dev->rx_reqs);
		break;

	/* data overrun */
	case -EOVERFLOW:
		/* FALLTHROUGH */

	default:
		DBG(dev, "rx status %d\n", status);
		list_add(&req->list, &dev->rx_reqs);
		break;
	}

	wake_up_interruptible(&dev->rx_wait);
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct printer_dev	*dev = ep->driver_data;

	switch (req->status) {
	default:
		VDBG(dev, "tx err %d\n", req->status);
		/* FALLTHROUGH */
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		break;
	case 0:
		break;
	}

	spin_lock(&dev->lock);
	/* Take the request struct off the active list and put it on the
	 * free list.
	 */
	list_del_init(&req->list);
	list_add(&req->list, &dev->tx_reqs);
	wake_up_interruptible(&dev->tx_wait);
	if (likely(list_empty(&dev->tx_reqs_active)))
		wake_up_interruptible(&dev->tx_flush_wait);

	spin_unlock(&dev->lock);
}

/*-------------------------------------------------------------------------*/

static int
printer_open(struct inode *inode, struct file *fd)
{
	struct printer_dev	*dev;
	unsigned long		flags;
	int			ret = -EBUSY;

	mutex_lock(&printer_mutex);
	dev = container_of(inode->i_cdev, struct printer_dev, printer_cdev);

	spin_lock_irqsave(&dev->lock, flags);

	if (!dev->printer_cdev_open) {
		dev->printer_cdev_open = 1;
		fd->private_data = dev;
		ret = 0;
		/* Change the printer status to show that it's on-line. */
		dev->printer_status |= PRINTER_SELECTED;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	DBG(dev, "printer_open returned %x\n", ret);
	mutex_unlock(&printer_mutex);
	return ret;
}

static int
printer_close(struct inode *inode, struct file *fd)
{
	struct printer_dev	*dev = fd->private_data;
	unsigned long		flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->printer_cdev_open = 0;
	fd->private_data = NULL;
	/* Change printer status to show that the printer is off-line. */
	dev->printer_status &= ~PRINTER_SELECTED;
	spin_unlock_irqrestore(&dev->lock, flags);

	DBG(dev, "printer_close\n");

	return 0;
}

/* This function must be called with interrupts turned off. */
static void
setup_rx_reqs(struct printer_dev *dev)
{
	struct usb_request              *req;

	while (likely(!list_empty(&dev->rx_reqs))) {
		int error;

		req = container_of(dev->rx_reqs.next,
				struct usb_request, list);
		list_del_init(&req->list);

		/* The USB Host sends us whatever amount of data it wants to
		 * so we always set the length field to the full USB_BUFSIZE.
		 * If the amount of data is more than the read() caller asked
		 * for it will be stored in the request buffer until it is
		 * asked for by read().
		 */
		req->length = USB_BUFSIZE;
		req->complete = rx_complete;

		error = usb_ep_queue(dev->out_ep, req, GFP_ATOMIC);
		if (error) {
			DBG(dev, "rx submit --> %d\n", error);
			list_add(&req->list, &dev->rx_reqs);
			break;
		} else {
			list_add(&req->list, &dev->rx_reqs_active);
		}
	}
}

static ssize_t
printer_read(struct file *fd, char __user *buf, size_t len, loff_t *ptr)
{
	struct printer_dev		*dev = fd->private_data;
	unsigned long			flags;
	size_t				size;
	size_t				bytes_copied;
	struct usb_request		*req;
	/* This is a pointer to the current USB rx request. */
	struct usb_request		*current_rx_req;
	/* This is the number of bytes in the current rx buffer. */
	size_t				current_rx_bytes;
	/* This is a pointer to the current rx buffer. */
	u8				*current_rx_buf;

	if (len == 0)
		return -EINVAL;

	DBG(dev, "printer_read trying to read %d bytes\n", (int)len);

	mutex_lock(&dev->lock_printer_io);
	spin_lock_irqsave(&dev->lock, flags);

	/* We will use this flag later to check if a printer reset happened
	 * after we turn interrupts back on.
	 */
	dev->reset_printer = 0;

	setup_rx_reqs(dev);

	bytes_copied = 0;
	current_rx_req = dev->current_rx_req;
	current_rx_bytes = dev->current_rx_bytes;
	current_rx_buf = dev->current_rx_buf;
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;

	/* Check if there is any data in the read buffers. Please note that
	 * current_rx_bytes is the number of bytes in the current rx buffer.
	 * If it is zero then check if there are any other rx_buffers that
	 * are on the completed list. We are only out of data if all rx
	 * buffers are empty.
	 */
	if ((current_rx_bytes == 0) &&
			(likely(list_empty(&dev->rx_buffers)))) {
		/* Turn interrupts back on before sleeping. */
		spin_unlock_irqrestore(&dev->lock, flags);

		/*
		 * If no data is available check if this is a NON-Blocking
		 * call or not.
		 */
		if (fd->f_flags & (O_NONBLOCK|O_NDELAY)) {
			mutex_unlock(&dev->lock_printer_io);
			return -EAGAIN;
		}

		/* Sleep until data is available */
		wait_event_interruptible(dev->rx_wait,
				(likely(!list_empty(&dev->rx_buffers))));
		spin_lock_irqsave(&dev->lock, flags);
	}

	/* We have data to return then copy it to the caller's buffer.*/
	while ((current_rx_bytes || likely(!list_empty(&dev->rx_buffers)))
			&& len) {
		if (current_rx_bytes == 0) {
			req = container_of(dev->rx_buffers.next,
					struct usb_request, list);
			list_del_init(&req->list);

			if (req->actual && req->buf) {
				current_rx_req = req;
				current_rx_bytes = req->actual;
				current_rx_buf = req->buf;
			} else {
				list_add(&req->list, &dev->rx_reqs);
				continue;
			}
		}

		/* Don't leave irqs off while doing memory copies */
		spin_unlock_irqrestore(&dev->lock, flags);

		if (len > current_rx_bytes)
			size = current_rx_bytes;
		else
			size = len;

		size -= copy_to_user(buf, current_rx_buf, size);
		bytes_copied += size;
		len -= size;
		buf += size;

		spin_lock_irqsave(&dev->lock, flags);

		/* We've disconnected or reset so return. */
		if (dev->reset_printer) {
			list_add(&current_rx_req->list, &dev->rx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_printer_io);
			return -EAGAIN;
		}

		/* If we not returning all the data left in this RX request
		 * buffer then adjust the amount of data left in the buffer.
		 * Othewise if we are done with this RX request buffer then
		 * requeue it to get any incoming data from the USB host.
		 */
		if (size < current_rx_bytes) {
			current_rx_bytes -= size;
			current_rx_buf += size;
		} else {
			list_add(&current_rx_req->list, &dev->rx_reqs);
			current_rx_bytes = 0;
			current_rx_buf = NULL;
			current_rx_req = NULL;
		}
	}

	dev->current_rx_req = current_rx_req;
	dev->current_rx_bytes = current_rx_bytes;
	dev->current_rx_buf = current_rx_buf;

	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_printer_io);

	DBG(dev, "printer_read returned %d bytes\n", (int)bytes_copied);

	if (bytes_copied)
		return bytes_copied;
	else
		return -EAGAIN;
}

static ssize_t
printer_write(struct file *fd, const char __user *buf, size_t len, loff_t *ptr)
{
	struct printer_dev	*dev = fd->private_data;
	unsigned long		flags;
	size_t			size;	/* Amount of data in a TX request. */
	size_t			bytes_copied = 0;
	struct usb_request	*req;

	DBG(dev, "printer_write trying to send %d bytes\n", (int)len);

	if (len == 0)
		return -EINVAL;

	mutex_lock(&dev->lock_printer_io);
	spin_lock_irqsave(&dev->lock, flags);

	/* Check if a printer reset happens while we have interrupts on */
	dev->reset_printer = 0;

	/* Check if there is any available write buffers */
	if (likely(list_empty(&dev->tx_reqs))) {
		/* Turn interrupts back on before sleeping. */
		spin_unlock_irqrestore(&dev->lock, flags);

		/*
		 * If write buffers are available check if this is
		 * a NON-Blocking call or not.
		 */
		if (fd->f_flags & (O_NONBLOCK|O_NDELAY)) {
			mutex_unlock(&dev->lock_printer_io);
			return -EAGAIN;
		}

		/* Sleep until a write buffer is available */
		wait_event_interruptible(dev->tx_wait,
				(likely(!list_empty(&dev->tx_reqs))));
		spin_lock_irqsave(&dev->lock, flags);
	}

	while (likely(!list_empty(&dev->tx_reqs)) && len) {

		if (len > USB_BUFSIZE)
			size = USB_BUFSIZE;
		else
			size = len;

		req = container_of(dev->tx_reqs.next, struct usb_request,
				list);
		list_del_init(&req->list);

		req->complete = tx_complete;
		req->length = size;

		/* Check if we need to send a zero length packet. */
		if (len > size)
			/* They will be more TX requests so no yet. */
			req->zero = 0;
		else
			/* If the data amount is not a multple of the
			 * maxpacket size then send a zero length packet.
			 */
			req->zero = ((len % dev->in_ep->maxpacket) == 0);

		/* Don't leave irqs off while doing memory copies */
		spin_unlock_irqrestore(&dev->lock, flags);

		if (copy_from_user(req->buf, buf, size)) {
			list_add(&req->list, &dev->tx_reqs);
			mutex_unlock(&dev->lock_printer_io);
			return bytes_copied;
		}

		bytes_copied += size;
		len -= size;
		buf += size;

		spin_lock_irqsave(&dev->lock, flags);

		/* We've disconnected or reset so free the req and buffer */
		if (dev->reset_printer) {
			list_add(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_printer_io);
			return -EAGAIN;
		}

		if (usb_ep_queue(dev->in_ep, req, GFP_ATOMIC)) {
			list_add(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_printer_io);
			return -EAGAIN;
		}

		list_add(&req->list, &dev->tx_reqs_active);

	}

	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_printer_io);

	DBG(dev, "printer_write sent %d bytes\n", (int)bytes_copied);

	if (bytes_copied) {
		return bytes_copied;
	} else {
		return -EAGAIN;
	}
}

static int
printer_fsync(struct file *fd, loff_t start, loff_t end, int datasync)
{
	struct printer_dev	*dev = fd->private_data;
	struct inode *inode = fd->f_path.dentry->d_inode;
	unsigned long		flags;
	int			tx_list_empty;

	mutex_lock(&inode->i_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	tx_list_empty = (likely(list_empty(&dev->tx_reqs)));
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!tx_list_empty) {
		/* Sleep until all data has been sent */
		wait_event_interruptible(dev->tx_flush_wait,
				(likely(list_empty(&dev->tx_reqs_active))));
	}
	mutex_unlock(&inode->i_mutex);

	return 0;
}

static unsigned int
printer_poll(struct file *fd, poll_table *wait)
{
	struct printer_dev	*dev = fd->private_data;
	unsigned long		flags;
	int			status = 0;

	mutex_lock(&dev->lock_printer_io);
	spin_lock_irqsave(&dev->lock, flags);
	setup_rx_reqs(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_printer_io);

	poll_wait(fd, &dev->rx_wait, wait);
	poll_wait(fd, &dev->tx_wait, wait);

	spin_lock_irqsave(&dev->lock, flags);
	if (likely(!list_empty(&dev->tx_reqs)))
		status |= POLLOUT | POLLWRNORM;

	if (likely(dev->current_rx_bytes) ||
			likely(!list_empty(&dev->rx_buffers)))
		status |= POLLIN | POLLRDNORM;

	spin_unlock_irqrestore(&dev->lock, flags);

	return status;
}

static long
printer_ioctl(struct file *fd, unsigned int code, unsigned long arg)
{
	struct printer_dev	*dev = fd->private_data;
	unsigned long		flags;
	int			status = 0;

	DBG(dev, "printer_ioctl: cmd=0x%4.4x, arg=%lu\n", code, arg);

	/* handle ioctls */

	spin_lock_irqsave(&dev->lock, flags);

	switch (code) {
	case GADGET_GET_PRINTER_STATUS:
		status = (int)dev->printer_status;
		break;
	case GADGET_SET_PRINTER_STATUS:
		dev->printer_status = (u8)arg;
		break;
	default:
		/* could not handle ioctl */
		DBG(dev, "printer_ioctl: ERROR cmd=0x%4.4xis not supported\n",
				code);
		status = -ENOTTY;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return status;
}

/* used after endpoint configuration */
static const struct file_operations printer_io_operations = {
	.owner =	THIS_MODULE,
	.open =		printer_open,
	.read =		printer_read,
	.write =	printer_write,
	.fsync =	printer_fsync,
	.poll =		printer_poll,
	.unlocked_ioctl = printer_ioctl,
	.release =	printer_close,
	.llseek =	noop_llseek,
};

/*-------------------------------------------------------------------------*/

static int
set_printer_interface(struct printer_dev *dev)
{
	int			result = 0;

	dev->in_ep->desc = ep_desc(dev->gadget, &hs_ep_in_desc, &fs_ep_in_desc);
	dev->in_ep->driver_data = dev;

	dev->out_ep->desc = ep_desc(dev->gadget, &hs_ep_out_desc,
				    &fs_ep_out_desc);
	dev->out_ep->driver_data = dev;

	result = usb_ep_enable(dev->in_ep);
	if (result != 0) {
		DBG(dev, "enable %s --> %d\n", dev->in_ep->name, result);
		goto done;
	}

	result = usb_ep_enable(dev->out_ep);
	if (result != 0) {
		DBG(dev, "enable %s --> %d\n", dev->in_ep->name, result);
		goto done;
	}

done:
	/* on error, disable any endpoints  */
	if (result != 0) {
		(void) usb_ep_disable(dev->in_ep);
		(void) usb_ep_disable(dev->out_ep);
		dev->in_ep->desc = NULL;
		dev->out_ep->desc = NULL;
	}

	/* caller is responsible for cleanup on error */
	return result;
}

static void printer_reset_interface(struct printer_dev *dev)
{
	if (dev->interface < 0)
		return;

	DBG(dev, "%s\n", __func__);

	if (dev->in_ep->desc)
		usb_ep_disable(dev->in_ep);

	if (dev->out_ep->desc)
		usb_ep_disable(dev->out_ep);

	dev->in_ep->desc = NULL;
	dev->out_ep->desc = NULL;
	dev->interface = -1;
}

/* Change our operational Interface. */
static int set_interface(struct printer_dev *dev, unsigned number)
{
	int			result = 0;

	/* Free the current interface */
	printer_reset_interface(dev);

	result = set_printer_interface(dev);
	if (result)
		printer_reset_interface(dev);
	else
		dev->interface = number;

	if (!result)
		INFO(dev, "Using interface %x\n", number);

	return result;
}

static void printer_soft_reset(struct printer_dev *dev)
{
	struct usb_request	*req;

	INFO(dev, "Received Printer Reset Request\n");

	if (usb_ep_disable(dev->in_ep))
		DBG(dev, "Failed to disable USB in_ep\n");
	if (usb_ep_disable(dev->out_ep))
		DBG(dev, "Failed to disable USB out_ep\n");

	if (dev->current_rx_req != NULL) {
		list_add(&dev->current_rx_req->list, &dev->rx_reqs);
		dev->current_rx_req = NULL;
	}
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;
	dev->reset_printer = 1;

	while (likely(!(list_empty(&dev->rx_buffers)))) {
		req = container_of(dev->rx_buffers.next, struct usb_request,
				list);
		list_del_init(&req->list);
		list_add(&req->list, &dev->rx_reqs);
	}

	while (likely(!(list_empty(&dev->rx_reqs_active)))) {
		req = container_of(dev->rx_buffers.next, struct usb_request,
				list);
		list_del_init(&req->list);
		list_add(&req->list, &dev->rx_reqs);
	}

	while (likely(!(list_empty(&dev->tx_reqs_active)))) {
		req = container_of(dev->tx_reqs_active.next,
				struct usb_request, list);
		list_del_init(&req->list);
		list_add(&req->list, &dev->tx_reqs);
	}

	if (usb_ep_enable(dev->in_ep))
		DBG(dev, "Failed to enable USB in_ep\n");
	if (usb_ep_enable(dev->out_ep))
		DBG(dev, "Failed to enable USB out_ep\n");

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);
}

/*-------------------------------------------------------------------------*/

/*
 * The setup() callback implements all the ep0 functionality that's not
 * handled lower down.
 */
static int printer_func_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct printer_dev *dev = container_of(f, struct printer_dev, function);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			wIndex = le16_to_cpu(ctrl->wIndex);
	u16			wValue = le16_to_cpu(ctrl->wValue);
	u16			wLength = le16_to_cpu(ctrl->wLength);

	DBG(dev, "ctrl req%02x.%02x v%04x i%04x l%d\n",
		ctrl->bRequestType, ctrl->bRequest, wValue, wIndex, wLength);

	switch (ctrl->bRequestType&USB_TYPE_MASK) {
	case USB_TYPE_CLASS:
		switch (ctrl->bRequest) {
		case 0: /* Get the IEEE-1284 PNP String */
			/* Only one printer interface is supported. */
			if ((wIndex>>8) != dev->interface)
				break;

			value = (pnp_string[0]<<8)|pnp_string[1];
			memcpy(req->buf, pnp_string, value);
			DBG(dev, "1284 PNP String: %x %s\n", value,
					&pnp_string[2]);
			break;

		case 1: /* Get Port Status */
			/* Only one printer interface is supported. */
			if (wIndex != dev->interface)
				break;

			*(u8 *)req->buf = dev->printer_status;
			value = min(wLength, (u16) 1);
			break;

		case 2: /* Soft Reset */
			/* Only one printer interface is supported. */
			if (wIndex != dev->interface)
				break;

			printer_soft_reset(dev);

			value = 0;
			break;

		default:
			goto unknown;
		}
		break;

	default:
unknown:
		VDBG(dev,
			"unknown ctrl req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
		break;
	}
	/* host either stalls (value < 0) or reports success */
	return value;
}

static int __init printer_func_bind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct printer_dev *dev = container_of(f, struct printer_dev, function);
	struct usb_composite_dev *cdev = c->cdev;
	struct usb_ep		*in_ep, *out_ep;
	int id;

	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	intf_desc.bInterfaceNumber = id;

	/* all we really need is bulk IN/OUT */
	in_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_in_desc);
	if (!in_ep) {
autoconf_fail:
		dev_err(&cdev->gadget->dev, "can't autoconfigure on %s\n",
			cdev->gadget->name);
		return -ENODEV;
	}
	in_ep->driver_data = in_ep;	/* claim */

	out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
	if (!out_ep)
		goto autoconf_fail;
	out_ep->driver_data = out_ep;	/* claim */

	/* assumes that all endpoints are dual-speed */
	hs_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
	hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;

	dev->in_ep = in_ep;
	dev->out_ep = out_ep;
	return 0;
}

static void printer_func_unbind(struct usb_configuration *c,
		struct usb_function *f)
{
}

static int printer_func_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct printer_dev *dev = container_of(f, struct printer_dev, function);
	int ret = -ENOTSUPP;

	if (!alt)
		ret = set_interface(dev, intf);

	return ret;
}

static void printer_func_disable(struct usb_function *f)
{
	struct printer_dev *dev = container_of(f, struct printer_dev, function);
	unsigned long		flags;

	DBG(dev, "%s\n", __func__);

	spin_lock_irqsave(&dev->lock, flags);
	printer_reset_interface(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void printer_cfg_unbind(struct usb_configuration *c)
{
	struct printer_dev	*dev;
	struct usb_request	*req;

	dev = &usb_printer_gadget;

	DBG(dev, "%s\n", __func__);

	/* Remove sysfs files */
	device_destroy(usb_gadget_class, g_printer_devno);

	/* Remove Character Device */
	cdev_del(&dev->printer_cdev);

	/* we must already have been disconnected ... no i/o may be active */
	WARN_ON(!list_empty(&dev->tx_reqs_active));
	WARN_ON(!list_empty(&dev->rx_reqs_active));

	/* Free all memory for this driver. */
	while (!list_empty(&dev->tx_reqs)) {
		req = container_of(dev->tx_reqs.next, struct usb_request,
				list);
		list_del(&req->list);
		printer_req_free(dev->in_ep, req);
	}

	if (dev->current_rx_req != NULL)
		printer_req_free(dev->out_ep, dev->current_rx_req);

	while (!list_empty(&dev->rx_reqs)) {
		req = container_of(dev->rx_reqs.next,
				struct usb_request, list);
		list_del(&req->list);
		printer_req_free(dev->out_ep, req);
	}

	while (!list_empty(&dev->rx_buffers)) {
		req = container_of(dev->rx_buffers.next,
				struct usb_request, list);
		list_del(&req->list);
		printer_req_free(dev->out_ep, req);
	}
}

static struct usb_configuration printer_cfg_driver = {
	.label			= "printer",
	.unbind			= printer_cfg_unbind,
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
};

static int __init printer_bind_config(struct usb_configuration *c)
{
	struct usb_gadget	*gadget = c->cdev->gadget;
	struct printer_dev	*dev;
	int			status = -ENOMEM;
	int			gcnum;
	size_t			len;
	u32			i;
	struct usb_request	*req;

	usb_ep_autoconfig_reset(gadget);

	dev = &usb_printer_gadget;

	dev->function.name = shortname;
	dev->function.descriptors = fs_printer_function;
	dev->function.hs_descriptors = hs_printer_function;
	dev->function.bind = printer_func_bind;
	dev->function.setup = printer_func_setup;
	dev->function.unbind = printer_func_unbind;
	dev->function.set_alt = printer_func_set_alt;
	dev->function.disable = printer_func_disable;

	status = usb_add_function(c, &dev->function);
	if (status)
		return status;

	/* Setup the sysfs files for the printer gadget. */
	dev->pdev = device_create(usb_gadget_class, NULL, g_printer_devno,
				  NULL, "g_printer");
	if (IS_ERR(dev->pdev)) {
		ERROR(dev, "Failed to create device: g_printer\n");
		goto fail;
	}

	/*
	 * Register a character device as an interface to a user mode
	 * program that handles the printer specific functionality.
	 */
	cdev_init(&dev->printer_cdev, &printer_io_operations);
	dev->printer_cdev.owner = THIS_MODULE;
	status = cdev_add(&dev->printer_cdev, g_printer_devno, 1);
	if (status) {
		ERROR(dev, "Failed to open char device\n");
		goto fail;
	}

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0) {
		device_desc.bcdDevice = cpu_to_le16(0x0200 + gcnum);
	} else {
		dev_warn(&gadget->dev, "controller '%s' not recognized\n",
			gadget->name);
		/* unrecognized, but safe unless bulk is REALLY quirky */
		device_desc.bcdDevice =
			cpu_to_le16(0xFFFF);
	}
	snprintf(manufacturer, sizeof(manufacturer), "%s %s with %s",
		init_utsname()->sysname, init_utsname()->release,
		gadget->name);

	if (iSerialNum)
		strlcpy(serial_num, iSerialNum, sizeof serial_num);

	if (iPNPstring)
		strlcpy(&pnp_string[2], iPNPstring, (sizeof pnp_string)-2);

	len = strlen(pnp_string);
	pnp_string[0] = (len >> 8) & 0xFF;
	pnp_string[1] = len & 0xFF;

	usb_gadget_set_selfpowered(gadget);

	if (gadget->is_otg) {
		otg_descriptor.bmAttributes |= USB_OTG_HNP;
		printer_cfg_driver.descriptors = otg_desc;
		printer_cfg_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	spin_lock_init(&dev->lock);
	mutex_init(&dev->lock_printer_io);
	INIT_LIST_HEAD(&dev->tx_reqs);
	INIT_LIST_HEAD(&dev->tx_reqs_active);
	INIT_LIST_HEAD(&dev->rx_reqs);
	INIT_LIST_HEAD(&dev->rx_reqs_active);
	INIT_LIST_HEAD(&dev->rx_buffers);
	init_waitqueue_head(&dev->rx_wait);
	init_waitqueue_head(&dev->tx_wait);
	init_waitqueue_head(&dev->tx_flush_wait);

	dev->interface = -1;
	dev->printer_cdev_open = 0;
	dev->printer_status = PRINTER_NOT_ERROR;
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;

	for (i = 0; i < QLEN; i++) {
		req = printer_req_alloc(dev->in_ep, USB_BUFSIZE, GFP_KERNEL);
		if (!req) {
			while (!list_empty(&dev->tx_reqs)) {
				req = container_of(dev->tx_reqs.next,
						struct usb_request, list);
				list_del(&req->list);
				printer_req_free(dev->in_ep, req);
			}
			return -ENOMEM;
		}
		list_add(&req->list, &dev->tx_reqs);
	}

	for (i = 0; i < QLEN; i++) {
		req = printer_req_alloc(dev->out_ep, USB_BUFSIZE, GFP_KERNEL);
		if (!req) {
			while (!list_empty(&dev->rx_reqs)) {
				req = container_of(dev->rx_reqs.next,
						struct usb_request, list);
				list_del(&req->list);
				printer_req_free(dev->out_ep, req);
			}
			return -ENOMEM;
		}
		list_add(&req->list, &dev->rx_reqs);
	}

	/* finish hookup to lower layer ... */
	dev->gadget = gadget;

	INFO(dev, "%s, version: " DRIVER_VERSION "\n", driver_desc);
	return 0;

fail:
	printer_cfg_unbind(c);
	return status;
}

static int printer_unbind(struct usb_composite_dev *cdev)
{
	return 0;
}

static int __init printer_bind(struct usb_composite_dev *cdev)
{
	int ret;

	ret = usb_string_ids_tab(cdev, strings);
	if (ret < 0)
		return ret;
	device_desc.iManufacturer = strings[STRING_MANUFACTURER].id;
	device_desc.iProduct = strings[STRING_PRODUCT].id;
	device_desc.iSerialNumber = strings[STRING_SERIALNUM].id;

	ret = usb_add_config(cdev, &printer_cfg_driver, printer_bind_config);
	return ret;
}

static __refdata struct usb_composite_driver printer_driver = {
	.name           = shortname,
	.dev            = &device_desc,
	.strings        = dev_strings,
	.max_speed      = USB_SPEED_HIGH,
	.bind		= printer_bind,
	.unbind		= printer_unbind,
};

static int __init
init(void)
{
	int status;

	usb_gadget_class = class_create(THIS_MODULE, "usb_printer_gadget");
	if (IS_ERR(usb_gadget_class)) {
		status = PTR_ERR(usb_gadget_class);
		pr_err("unable to create usb_gadget class %d\n", status);
		return status;
	}

	status = alloc_chrdev_region(&g_printer_devno, 0, 1,
			"USB printer gadget");
	if (status) {
		pr_err("alloc_chrdev_region %d\n", status);
		class_destroy(usb_gadget_class);
		return status;
	}

	status = usb_composite_probe(&printer_driver);
	if (status) {
		class_destroy(usb_gadget_class);
		unregister_chrdev_region(g_printer_devno, 1);
		pr_err("usb_gadget_probe_driver %x\n", status);
	}

	return status;
}
module_init(init);

static void __exit
cleanup(void)
{
	mutex_lock(&usb_printer_gadget.lock_printer_io);
	usb_composite_unregister(&printer_driver);
	unregister_chrdev_region(g_printer_devno, 1);
	class_destroy(usb_gadget_class);
	mutex_unlock(&usb_printer_gadget.lock_printer_io);
}
module_exit(cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Craig Nadler");
MODULE_LICENSE("GPL");
