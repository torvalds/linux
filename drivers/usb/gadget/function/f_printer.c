/*
 * f_printer.c - USB printer function driver
 *
 * Copied from drivers/usb/gadget/legacy/printer.c,
 * which was:
 *
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
#include <linux/idr.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
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
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/usb/g_printer.h>

#include "u_printer.h"

#define PRINTER_MINORS		4
#define GET_DEVICE_ID		0
#define GET_PORT_STATUS		1
#define SOFT_RESET		2

static int major, minors;
static struct class *usb_gadget_class;
static DEFINE_IDA(printer_ida);
static DEFINE_MUTEX(printer_ida_lock); /* protects access do printer_ida */

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
	int			minor;
	struct cdev		printer_cdev;
	u8			printer_cdev_open;
	wait_queue_head_t	wait;
	unsigned		q_len;
	char			*pnp_string;	/* We don't own memory! */
	struct usb_function	function;
};

static inline struct printer_dev *func_to_printer(struct usb_function *f)
{
	return container_of(f, struct printer_dev, function);
}

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

/* holds our biggest descriptor */
#define USB_DESC_BUFSIZE		256
#define USB_BUFSIZE			8192

static struct usb_interface_descriptor intf_desc = {
	.bLength =		sizeof(intf_desc),
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

static struct usb_descriptor_header *hs_printer_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &hs_ep_in_desc,
	(struct usb_descriptor_header *) &hs_ep_out_desc,
	NULL
};

/*
 * Added endpoint descriptors for 3.0 devices
 */

static struct usb_endpoint_descriptor ss_ep_in_desc = {
	.bLength =              USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =      USB_DT_ENDPOINT,
	.bmAttributes =         USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =       cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_ep_in_comp_desc = {
	.bLength =              sizeof(ss_ep_in_comp_desc),
	.bDescriptorType =      USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor ss_ep_out_desc = {
	.bLength =              USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =      USB_DT_ENDPOINT,
	.bmAttributes =         USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =       cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_ep_out_comp_desc = {
	.bLength =              sizeof(ss_ep_out_comp_desc),
	.bDescriptorType =      USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_descriptor_header *ss_printer_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &ss_ep_in_desc,
	(struct usb_descriptor_header *) &ss_ep_in_comp_desc,
	(struct usb_descriptor_header *) &ss_ep_out_desc,
	(struct usb_descriptor_header *) &ss_ep_out_comp_desc,
	NULL
};

/* maxpacket and other transfer characteristics vary by speed. */
static inline struct usb_endpoint_descriptor *ep_desc(struct usb_gadget *gadget,
					struct usb_endpoint_descriptor *fs,
					struct usb_endpoint_descriptor *hs,
					struct usb_endpoint_descriptor *ss)
{
	switch (gadget->speed) {
	case USB_SPEED_SUPER:
		return ss;
	case USB_SPEED_HIGH:
		return hs;
	default:
		return fs;
	}
}

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

		/* here, we unlock, and only unlock, to avoid deadlock. */
		spin_unlock(&dev->lock);
		error = usb_ep_queue(dev->out_ep, req, GFP_ATOMIC);
		spin_lock(&dev->lock);
		if (error) {
			DBG(dev, "rx submit --> %d\n", error);
			list_add(&req->list, &dev->rx_reqs);
			break;
		}
		/* if the req is empty, then add it into dev->rx_reqs_active. */
		else if (list_empty(&req->list))
			list_add(&req->list, &dev->rx_reqs_active);
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
	int			value;

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
			/* If the data amount is not a multiple of the
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

		/* here, we unlock, and only unlock, to avoid deadlock. */
		spin_unlock(&dev->lock);
		value = usb_ep_queue(dev->in_ep, req, GFP_ATOMIC);
		spin_lock(&dev->lock);
		if (value) {
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

	if (bytes_copied)
		return bytes_copied;
	else
		return -EAGAIN;
}

static int
printer_fsync(struct file *fd, loff_t start, loff_t end, int datasync)
{
	struct printer_dev	*dev = fd->private_data;
	struct inode *inode = file_inode(fd);
	unsigned long		flags;
	int			tx_list_empty;

	inode_lock(inode);
	spin_lock_irqsave(&dev->lock, flags);
	tx_list_empty = (likely(list_empty(&dev->tx_reqs)));
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!tx_list_empty) {
		/* Sleep until all data has been sent */
		wait_event_interruptible(dev->tx_flush_wait,
				(likely(list_empty(&dev->tx_reqs_active))));
	}
	inode_unlock(inode);

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

	dev->in_ep->desc = ep_desc(dev->gadget, &fs_ep_in_desc, &hs_ep_in_desc,
				&ss_ep_in_desc);
	dev->in_ep->driver_data = dev;

	dev->out_ep->desc = ep_desc(dev->gadget, &fs_ep_out_desc,
				    &hs_ep_out_desc, &ss_ep_out_desc);
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
	unsigned long	flags;

	if (dev->interface < 0)
		return;

	DBG(dev, "%s\n", __func__);

	if (dev->in_ep->desc)
		usb_ep_disable(dev->in_ep);

	if (dev->out_ep->desc)
		usb_ep_disable(dev->out_ep);

	spin_lock_irqsave(&dev->lock, flags);
	dev->in_ep->desc = NULL;
	dev->out_ep->desc = NULL;
	dev->interface = -1;
	spin_unlock_irqrestore(&dev->lock, flags);
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

static bool gprinter_req_match(struct usb_function *f,
			       const struct usb_ctrlrequest *ctrl,
			       bool config0)
{
	struct printer_dev	*dev = func_to_printer(f);
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	if (config0)
		return false;

	if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE ||
	    (ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS)
		return false;

	switch (ctrl->bRequest) {
	case GET_DEVICE_ID:
		w_index >>= 8;
		if (USB_DIR_IN & ctrl->bRequestType)
			break;
		return false;
	case GET_PORT_STATUS:
		if (!w_value && w_length == 1 &&
		    (USB_DIR_IN & ctrl->bRequestType))
			break;
		return false;
	case SOFT_RESET:
		if (!w_value && !w_length &&
		   !(USB_DIR_IN & ctrl->bRequestType))
			break;
		/* fall through */
	default:
		return false;
	}
	return w_index == dev->interface;
}

/*
 * The setup() callback implements all the ep0 functionality that's not
 * handled lower down.
 */
static int printer_func_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct printer_dev *dev = func_to_printer(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	u8			*buf = req->buf;
	int			value = -EOPNOTSUPP;
	u16			wIndex = le16_to_cpu(ctrl->wIndex);
	u16			wValue = le16_to_cpu(ctrl->wValue);
	u16			wLength = le16_to_cpu(ctrl->wLength);

	DBG(dev, "ctrl req%02x.%02x v%04x i%04x l%d\n",
		ctrl->bRequestType, ctrl->bRequest, wValue, wIndex, wLength);

	switch (ctrl->bRequestType&USB_TYPE_MASK) {
	case USB_TYPE_CLASS:
		switch (ctrl->bRequest) {
		case GET_DEVICE_ID: /* Get the IEEE-1284 PNP String */
			/* Only one printer interface is supported. */
			if ((wIndex>>8) != dev->interface)
				break;

			if (!dev->pnp_string) {
				value = 0;
				break;
			}
			value = strlen(dev->pnp_string);
			buf[0] = (value >> 8) & 0xFF;
			buf[1] = value & 0xFF;
			memcpy(buf + 2, dev->pnp_string, value);
			DBG(dev, "1284 PNP String: %x %s\n", value,
			    dev->pnp_string);
			break;

		case GET_PORT_STATUS: /* Get Port Status */
			/* Only one printer interface is supported. */
			if (wIndex != dev->interface)
				break;

			buf[0] = dev->printer_status;
			value = min_t(u16, wLength, 1);
			break;

		case SOFT_RESET: /* Soft Reset */
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
	if (value >= 0) {
		req->length = value;
		req->zero = value < wLength;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			ERROR(dev, "%s:%d Error!\n", __func__, __LINE__);
			req->status = 0;
		}
	}
	return value;
}

static int printer_func_bind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct usb_gadget *gadget = c->cdev->gadget;
	struct printer_dev *dev = func_to_printer(f);
	struct device *pdev;
	struct usb_composite_dev *cdev = c->cdev;
	struct usb_ep *in_ep;
	struct usb_ep *out_ep = NULL;
	struct usb_request *req;
	dev_t devt;
	int id;
	int ret;
	u32 i;

	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	intf_desc.bInterfaceNumber = id;

	/* finish hookup to lower layer ... */
	dev->gadget = gadget;

	/* all we really need is bulk IN/OUT */
	in_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_in_desc);
	if (!in_ep) {
autoconf_fail:
		dev_err(&cdev->gadget->dev, "can't autoconfigure on %s\n",
			cdev->gadget->name);
		return -ENODEV;
	}

	out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
	if (!out_ep)
		goto autoconf_fail;

	/* assumes that all endpoints are dual-speed */
	hs_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
	hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;
	ss_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
	ss_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_printer_function,
			hs_printer_function, ss_printer_function, NULL);
	if (ret)
		return ret;

	dev->in_ep = in_ep;
	dev->out_ep = out_ep;

	ret = -ENOMEM;
	for (i = 0; i < dev->q_len; i++) {
		req = printer_req_alloc(dev->in_ep, USB_BUFSIZE, GFP_KERNEL);
		if (!req)
			goto fail_tx_reqs;
		list_add(&req->list, &dev->tx_reqs);
	}

	for (i = 0; i < dev->q_len; i++) {
		req = printer_req_alloc(dev->out_ep, USB_BUFSIZE, GFP_KERNEL);
		if (!req)
			goto fail_rx_reqs;
		list_add(&req->list, &dev->rx_reqs);
	}

	/* Setup the sysfs files for the printer gadget. */
	devt = MKDEV(major, dev->minor);
	pdev = device_create(usb_gadget_class, NULL, devt,
				  NULL, "g_printer%d", dev->minor);
	if (IS_ERR(pdev)) {
		ERROR(dev, "Failed to create device: g_printer\n");
		ret = PTR_ERR(pdev);
		goto fail_rx_reqs;
	}

	/*
	 * Register a character device as an interface to a user mode
	 * program that handles the printer specific functionality.
	 */
	cdev_init(&dev->printer_cdev, &printer_io_operations);
	dev->printer_cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev->printer_cdev, devt, 1);
	if (ret) {
		ERROR(dev, "Failed to open char device\n");
		goto fail_cdev_add;
	}

	return 0;

fail_cdev_add:
	device_destroy(usb_gadget_class, devt);

fail_rx_reqs:
	while (!list_empty(&dev->rx_reqs)) {
		req = container_of(dev->rx_reqs.next, struct usb_request, list);
		list_del(&req->list);
		printer_req_free(dev->out_ep, req);
	}

fail_tx_reqs:
	while (!list_empty(&dev->tx_reqs)) {
		req = container_of(dev->tx_reqs.next, struct usb_request, list);
		list_del(&req->list);
		printer_req_free(dev->in_ep, req);
	}

	return ret;

}

static int printer_func_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct printer_dev *dev = func_to_printer(f);
	int ret = -ENOTSUPP;

	if (!alt)
		ret = set_interface(dev, intf);

	return ret;
}

static void printer_func_disable(struct usb_function *f)
{
	struct printer_dev *dev = func_to_printer(f);

	DBG(dev, "%s\n", __func__);

	printer_reset_interface(dev);
}

static inline struct f_printer_opts
*to_f_printer_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_printer_opts,
			    func_inst.group);
}

static void printer_attr_release(struct config_item *item)
{
	struct f_printer_opts *opts = to_f_printer_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations printer_item_ops = {
	.release	= printer_attr_release,
};

static ssize_t f_printer_opts_pnp_string_show(struct config_item *item,
					      char *page)
{
	struct f_printer_opts *opts = to_f_printer_opts(item);
	int result = 0;

	mutex_lock(&opts->lock);
	if (!opts->pnp_string)
		goto unlock;

	result = strlcpy(page, opts->pnp_string, PAGE_SIZE);
	if (result >= PAGE_SIZE) {
		result = PAGE_SIZE;
	} else if (page[result - 1] != '\n' && result + 1 < PAGE_SIZE) {
		page[result++] = '\n';
		page[result] = '\0';
	}

unlock:
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_printer_opts_pnp_string_store(struct config_item *item,
					       const char *page, size_t len)
{
	struct f_printer_opts *opts = to_f_printer_opts(item);
	char *new_pnp;
	int result;

	mutex_lock(&opts->lock);

	new_pnp = kstrndup(page, len, GFP_KERNEL);
	if (!new_pnp) {
		result = -ENOMEM;
		goto unlock;
	}

	if (opts->pnp_string_allocated)
		kfree(opts->pnp_string);

	opts->pnp_string_allocated = true;
	opts->pnp_string = new_pnp;
	result = len;
unlock:
	mutex_unlock(&opts->lock);

	return result;
}

CONFIGFS_ATTR(f_printer_opts_, pnp_string);

static ssize_t f_printer_opts_q_len_show(struct config_item *item,
					 char *page)
{
	struct f_printer_opts *opts = to_f_printer_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%d\n", opts->q_len);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_printer_opts_q_len_store(struct config_item *item,
					  const char *page, size_t len)
{
	struct f_printer_opts *opts = to_f_printer_opts(item);
	int ret;
	u16 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou16(page, 0, &num);
	if (ret)
		goto end;

	opts->q_len = (unsigned)num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_printer_opts_, q_len);

static struct configfs_attribute *printer_attrs[] = {
	&f_printer_opts_attr_pnp_string,
	&f_printer_opts_attr_q_len,
	NULL,
};

static struct config_item_type printer_func_type = {
	.ct_item_ops	= &printer_item_ops,
	.ct_attrs	= printer_attrs,
	.ct_owner	= THIS_MODULE,
};

static inline int gprinter_get_minor(void)
{
	int ret;

	ret = ida_simple_get(&printer_ida, 0, 0, GFP_KERNEL);
	if (ret >= PRINTER_MINORS) {
		ida_simple_remove(&printer_ida, ret);
		ret = -ENODEV;
	}

	return ret;
}

static inline void gprinter_put_minor(int minor)
{
	ida_simple_remove(&printer_ida, minor);
}

static int gprinter_setup(int);
static void gprinter_cleanup(void);

static void gprinter_free_inst(struct usb_function_instance *f)
{
	struct f_printer_opts *opts;

	opts = container_of(f, struct f_printer_opts, func_inst);

	mutex_lock(&printer_ida_lock);

	gprinter_put_minor(opts->minor);
	if (ida_is_empty(&printer_ida))
		gprinter_cleanup();

	mutex_unlock(&printer_ida_lock);

	if (opts->pnp_string_allocated)
		kfree(opts->pnp_string);
	kfree(opts);
}

static struct usb_function_instance *gprinter_alloc_inst(void)
{
	struct f_printer_opts *opts;
	struct usb_function_instance *ret;
	int status = 0;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = gprinter_free_inst;
	ret = &opts->func_inst;

	mutex_lock(&printer_ida_lock);

	if (ida_is_empty(&printer_ida)) {
		status = gprinter_setup(PRINTER_MINORS);
		if (status) {
			ret = ERR_PTR(status);
			kfree(opts);
			goto unlock;
		}
	}

	opts->minor = gprinter_get_minor();
	if (opts->minor < 0) {
		ret = ERR_PTR(opts->minor);
		kfree(opts);
		if (ida_is_empty(&printer_ida))
			gprinter_cleanup();
		goto unlock;
	}
	config_group_init_type_name(&opts->func_inst.group, "",
				    &printer_func_type);

unlock:
	mutex_unlock(&printer_ida_lock);
	return ret;
}

static void gprinter_free(struct usb_function *f)
{
	struct printer_dev *dev = func_to_printer(f);
	struct f_printer_opts *opts;

	opts = container_of(f->fi, struct f_printer_opts, func_inst);
	kfree(dev);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void printer_func_unbind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct printer_dev	*dev;
	struct usb_request	*req;

	dev = func_to_printer(f);

	device_destroy(usb_gadget_class, MKDEV(major, dev->minor));

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
	usb_free_all_descriptors(f);
}

static struct usb_function *gprinter_alloc(struct usb_function_instance *fi)
{
	struct printer_dev	*dev;
	struct f_printer_opts	*opts;

	opts = container_of(fi, struct f_printer_opts, func_inst);

	mutex_lock(&opts->lock);
	if (opts->minor >= minors) {
		mutex_unlock(&opts->lock);
		return ERR_PTR(-ENOENT);
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		mutex_unlock(&opts->lock);
		return ERR_PTR(-ENOMEM);
	}

	++opts->refcnt;
	dev->minor = opts->minor;
	dev->pnp_string = opts->pnp_string;
	dev->q_len = opts->q_len;
	mutex_unlock(&opts->lock);

	dev->function.name = "printer";
	dev->function.bind = printer_func_bind;
	dev->function.setup = printer_func_setup;
	dev->function.unbind = printer_func_unbind;
	dev->function.set_alt = printer_func_set_alt;
	dev->function.disable = printer_func_disable;
	dev->function.req_match = gprinter_req_match;
	dev->function.free_func = gprinter_free;

	INIT_LIST_HEAD(&dev->tx_reqs);
	INIT_LIST_HEAD(&dev->rx_reqs);
	INIT_LIST_HEAD(&dev->rx_buffers);
	INIT_LIST_HEAD(&dev->tx_reqs_active);
	INIT_LIST_HEAD(&dev->rx_reqs_active);

	spin_lock_init(&dev->lock);
	mutex_init(&dev->lock_printer_io);
	init_waitqueue_head(&dev->rx_wait);
	init_waitqueue_head(&dev->tx_wait);
	init_waitqueue_head(&dev->tx_flush_wait);

	dev->interface = -1;
	dev->printer_cdev_open = 0;
	dev->printer_status = PRINTER_NOT_ERROR;
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;

	return &dev->function;
}

DECLARE_USB_FUNCTION_INIT(printer, gprinter_alloc_inst, gprinter_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Craig Nadler");

static int gprinter_setup(int count)
{
	int status;
	dev_t devt;

	usb_gadget_class = class_create(THIS_MODULE, "usb_printer_gadget");
	if (IS_ERR(usb_gadget_class)) {
		status = PTR_ERR(usb_gadget_class);
		usb_gadget_class = NULL;
		pr_err("unable to create usb_gadget class %d\n", status);
		return status;
	}

	status = alloc_chrdev_region(&devt, 0, count, "USB printer gadget");
	if (status) {
		pr_err("alloc_chrdev_region %d\n", status);
		class_destroy(usb_gadget_class);
		usb_gadget_class = NULL;
		return status;
	}

	major = MAJOR(devt);
	minors = count;

	return status;
}

static void gprinter_cleanup(void)
{
	if (major) {
		unregister_chrdev_region(MKDEV(major, 0), minors);
		major = minors = 0;
	}
	class_destroy(usb_gadget_class);
	usb_gadget_class = NULL;
}
