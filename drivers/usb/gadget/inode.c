/*
 * inode.c -- user mode filesystem api for usb gadget controllers
 *
 * Copyright (C) 2003-2004 David Brownell
 * Copyright (C) 2003 Agilent Technologies
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/* #define VERBOSE_DEBUG */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/uts.h>
#include <linux/wait.h>
#include <linux/compiler.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/poll.h>

#include <linux/device.h>
#include <linux/moduleparam.h>

#include <linux/usb/gadgetfs.h>
#include <linux/usb/gadget.h>


/*
 * The gadgetfs API maps each endpoint to a file descriptor so that you
 * can use standard synchronous read/write calls for I/O.  There's some
 * O_NONBLOCK and O_ASYNC/FASYNC style i/o support.  Example usermode
 * drivers show how this works in practice.  You can also use AIO to
 * eliminate I/O gaps between requests, to help when streaming data.
 *
 * Key parts that must be USB-specific are protocols defining how the
 * read/write operations relate to the hardware state machines.  There
 * are two types of files.  One type is for the device, implementing ep0.
 * The other type is for each IN or OUT endpoint.  In both cases, the
 * user mode driver must configure the hardware before using it.
 *
 * - First, dev_config() is called when /dev/gadget/$CHIP is configured
 *   (by writing configuration and device descriptors).  Afterwards it
 *   may serve as a source of device events, used to handle all control
 *   requests other than basic enumeration.
 *
 * - Then, after a SET_CONFIGURATION control request, ep_config() is
 *   called when each /dev/gadget/ep* file is configured (by writing
 *   endpoint descriptors).  Afterwards these files are used to write()
 *   IN data or to read() OUT data.  To halt the endpoint, a "wrong
 *   direction" request is issued (like reading an IN endpoint).
 *
 * Unlike "usbfs" the only ioctl()s are for things that are rare, and maybe
 * not possible on all hardware.  For example, precise fault handling with
 * respect to data left in endpoint fifos after aborted operations; or
 * selective clearing of endpoint halts, to implement SET_INTERFACE.
 */

#define	DRIVER_DESC	"USB Gadget filesystem"
#define	DRIVER_VERSION	"24 Aug 2004"

static const char driver_desc [] = DRIVER_DESC;
static const char shortname [] = "gadgetfs";

MODULE_DESCRIPTION (DRIVER_DESC);
MODULE_AUTHOR ("David Brownell");
MODULE_LICENSE ("GPL");


/*----------------------------------------------------------------------*/

#define GADGETFS_MAGIC		0xaee71ee7
#define DMA_ADDR_INVALID	(~(dma_addr_t)0)

/* /dev/gadget/$CHIP represents ep0 and the whole device */
enum ep0_state {
	/* DISBLED is the initial state.
	 */
	STATE_DEV_DISABLED = 0,

	/* Only one open() of /dev/gadget/$CHIP; only one file tracks
	 * ep0/device i/o modes and binding to the controller.  Driver
	 * must always write descriptors to initialize the device, then
	 * the device becomes UNCONNECTED until enumeration.
	 */
	STATE_DEV_OPENED,

	/* From then on, ep0 fd is in either of two basic modes:
	 * - (UN)CONNECTED: read usb_gadgetfs_event(s) from it
	 * - SETUP: read/write will transfer control data and succeed;
	 *   or if "wrong direction", performs protocol stall
	 */
	STATE_DEV_UNCONNECTED,
	STATE_DEV_CONNECTED,
	STATE_DEV_SETUP,

	/* UNBOUND means the driver closed ep0, so the device won't be
	 * accessible again (DEV_DISABLED) until all fds are closed.
	 */
	STATE_DEV_UNBOUND,
};

/* enough for the whole queue: most events invalidate others */
#define	N_EVENT			5

struct dev_data {
	spinlock_t			lock;
	atomic_t			count;
	enum ep0_state			state;		/* P: lock */
	struct usb_gadgetfs_event	event [N_EVENT];
	unsigned			ev_next;
	struct fasync_struct		*fasync;
	u8				current_config;

	/* drivers reading ep0 MUST handle control requests (SETUP)
	 * reported that way; else the host will time out.
	 */
	unsigned			usermode_setup : 1,
					setup_in : 1,
					setup_can_stall : 1,
					setup_out_ready : 1,
					setup_out_error : 1,
					setup_abort : 1;
	unsigned			setup_wLength;

	/* the rest is basically write-once */
	struct usb_config_descriptor	*config, *hs_config;
	struct usb_device_descriptor	*dev;
	struct usb_request		*req;
	struct usb_gadget		*gadget;
	struct list_head		epfiles;
	void				*buf;
	wait_queue_head_t		wait;
	struct super_block		*sb;
	struct dentry			*dentry;

	/* except this scratch i/o buffer for ep0 */
	u8				rbuf [256];
};

static inline void get_dev (struct dev_data *data)
{
	atomic_inc (&data->count);
}

static void put_dev (struct dev_data *data)
{
	if (likely (!atomic_dec_and_test (&data->count)))
		return;
	/* needs no more cleanup */
	BUG_ON (waitqueue_active (&data->wait));
	kfree (data);
}

static struct dev_data *dev_new (void)
{
	struct dev_data		*dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;
	dev->state = STATE_DEV_DISABLED;
	atomic_set (&dev->count, 1);
	spin_lock_init (&dev->lock);
	INIT_LIST_HEAD (&dev->epfiles);
	init_waitqueue_head (&dev->wait);
	return dev;
}

/*----------------------------------------------------------------------*/

/* other /dev/gadget/$ENDPOINT files represent endpoints */
enum ep_state {
	STATE_EP_DISABLED = 0,
	STATE_EP_READY,
	STATE_EP_ENABLED,
	STATE_EP_UNBOUND,
};

struct ep_data {
	struct semaphore		lock;
	enum ep_state			state;
	atomic_t			count;
	struct dev_data			*dev;
	/* must hold dev->lock before accessing ep or req */
	struct usb_ep			*ep;
	struct usb_request		*req;
	ssize_t				status;
	char				name [16];
	struct usb_endpoint_descriptor	desc, hs_desc;
	struct list_head		epfiles;
	wait_queue_head_t		wait;
	struct dentry			*dentry;
	struct inode			*inode;
};

static inline void get_ep (struct ep_data *data)
{
	atomic_inc (&data->count);
}

static void put_ep (struct ep_data *data)
{
	if (likely (!atomic_dec_and_test (&data->count)))
		return;
	put_dev (data->dev);
	/* needs no more cleanup */
	BUG_ON (!list_empty (&data->epfiles));
	BUG_ON (waitqueue_active (&data->wait));
	kfree (data);
}

/*----------------------------------------------------------------------*/

/* most "how to use the hardware" policy choices are in userspace:
 * mapping endpoint roles (which the driver needs) to the capabilities
 * which the usb controller has.  most of those capabilities are exposed
 * implicitly, starting with the driver name and then endpoint names.
 */

static const char *CHIP;

/*----------------------------------------------------------------------*/

/* NOTE:  don't use dev_printk calls before binding to the gadget
 * at the end of ep0 configuration, or after unbind.
 */

/* too wordy: dev_printk(level , &(d)->gadget->dev , fmt , ## args) */
#define xprintk(d,level,fmt,args...) \
	printk(level "%s: " fmt , shortname , ## args)

#ifdef DEBUG
#define DBG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DBG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE_DEBUG
#define VDEBUG	DBG
#else
#define VDEBUG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)


/*----------------------------------------------------------------------*/

/* SYNCHRONOUS ENDPOINT OPERATIONS (bulk/intr/iso)
 *
 * After opening, configure non-control endpoints.  Then use normal
 * stream read() and write() requests; and maybe ioctl() to get more
 * precise FIFO status when recovering from cancellation.
 */

static void epio_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct ep_data	*epdata = ep->driver_data;

	if (!req->context)
		return;
	if (req->status)
		epdata->status = req->status;
	else
		epdata->status = req->actual;
	complete ((struct completion *)req->context);
}

/* tasklock endpoint, returning when it's connected.
 * still need dev->lock to use epdata->ep.
 */
static int
get_ready_ep (unsigned f_flags, struct ep_data *epdata)
{
	int	val;

	if (f_flags & O_NONBLOCK) {
		if (down_trylock (&epdata->lock) != 0)
			goto nonblock;
		if (epdata->state != STATE_EP_ENABLED) {
			up (&epdata->lock);
nonblock:
			val = -EAGAIN;
		} else
			val = 0;
		return val;
	}

	if ((val = down_interruptible (&epdata->lock)) < 0)
		return val;

	switch (epdata->state) {
	case STATE_EP_ENABLED:
		break;
	// case STATE_EP_DISABLED:		/* "can't happen" */
	// case STATE_EP_READY:			/* "can't happen" */
	default:				/* error! */
		pr_debug ("%s: ep %p not available, state %d\n",
				shortname, epdata, epdata->state);
		// FALLTHROUGH
	case STATE_EP_UNBOUND:			/* clean disconnect */
		val = -ENODEV;
		up (&epdata->lock);
	}
	return val;
}

static ssize_t
ep_io (struct ep_data *epdata, void *buf, unsigned len)
{
	DECLARE_COMPLETION_ONSTACK (done);
	int value;

	spin_lock_irq (&epdata->dev->lock);
	if (likely (epdata->ep != NULL)) {
		struct usb_request	*req = epdata->req;

		req->context = &done;
		req->complete = epio_complete;
		req->buf = buf;
		req->length = len;
		value = usb_ep_queue (epdata->ep, req, GFP_ATOMIC);
	} else
		value = -ENODEV;
	spin_unlock_irq (&epdata->dev->lock);

	if (likely (value == 0)) {
		value = wait_event_interruptible (done.wait, done.done);
		if (value != 0) {
			spin_lock_irq (&epdata->dev->lock);
			if (likely (epdata->ep != NULL)) {
				DBG (epdata->dev, "%s i/o interrupted\n",
						epdata->name);
				usb_ep_dequeue (epdata->ep, epdata->req);
				spin_unlock_irq (&epdata->dev->lock);

				wait_event (done.wait, done.done);
				if (epdata->status == -ECONNRESET)
					epdata->status = -EINTR;
			} else {
				spin_unlock_irq (&epdata->dev->lock);

				DBG (epdata->dev, "endpoint gone\n");
				epdata->status = -ENODEV;
			}
		}
		return epdata->status;
	}
	return value;
}


/* handle a synchronous OUT bulk/intr/iso transfer */
static ssize_t
ep_read (struct file *fd, char __user *buf, size_t len, loff_t *ptr)
{
	struct ep_data		*data = fd->private_data;
	void			*kbuf;
	ssize_t			value;

	if ((value = get_ready_ep (fd->f_flags, data)) < 0)
		return value;

	/* halt any endpoint by doing a "wrong direction" i/o call */
	if (data->desc.bEndpointAddress & USB_DIR_IN) {
		if ((data->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_ISOC)
			return -EINVAL;
		DBG (data->dev, "%s halt\n", data->name);
		spin_lock_irq (&data->dev->lock);
		if (likely (data->ep != NULL))
			usb_ep_set_halt (data->ep);
		spin_unlock_irq (&data->dev->lock);
		up (&data->lock);
		return -EBADMSG;
	}

	/* FIXME readahead for O_NONBLOCK and poll(); careful with ZLPs */

	value = -ENOMEM;
	kbuf = kmalloc (len, GFP_KERNEL);
	if (unlikely (!kbuf))
		goto free1;

	value = ep_io (data, kbuf, len);
	VDEBUG (data->dev, "%s read %zu OUT, status %d\n",
		data->name, len, (int) value);
	if (value >= 0 && copy_to_user (buf, kbuf, value))
		value = -EFAULT;

free1:
	up (&data->lock);
	kfree (kbuf);
	return value;
}

/* handle a synchronous IN bulk/intr/iso transfer */
static ssize_t
ep_write (struct file *fd, const char __user *buf, size_t len, loff_t *ptr)
{
	struct ep_data		*data = fd->private_data;
	void			*kbuf;
	ssize_t			value;

	if ((value = get_ready_ep (fd->f_flags, data)) < 0)
		return value;

	/* halt any endpoint by doing a "wrong direction" i/o call */
	if (!(data->desc.bEndpointAddress & USB_DIR_IN)) {
		if ((data->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_ISOC)
			return -EINVAL;
		DBG (data->dev, "%s halt\n", data->name);
		spin_lock_irq (&data->dev->lock);
		if (likely (data->ep != NULL))
			usb_ep_set_halt (data->ep);
		spin_unlock_irq (&data->dev->lock);
		up (&data->lock);
		return -EBADMSG;
	}

	/* FIXME writebehind for O_NONBLOCK and poll(), qlen = 1 */

	value = -ENOMEM;
	kbuf = kmalloc (len, GFP_KERNEL);
	if (!kbuf)
		goto free1;
	if (copy_from_user (kbuf, buf, len)) {
		value = -EFAULT;
		goto free1;
	}

	value = ep_io (data, kbuf, len);
	VDEBUG (data->dev, "%s write %zu IN, status %d\n",
		data->name, len, (int) value);
free1:
	up (&data->lock);
	kfree (kbuf);
	return value;
}

static int
ep_release (struct inode *inode, struct file *fd)
{
	struct ep_data		*data = fd->private_data;
	int value;

	if ((value = down_interruptible(&data->lock)) < 0)
		return value;

	/* clean up if this can be reopened */
	if (data->state != STATE_EP_UNBOUND) {
		data->state = STATE_EP_DISABLED;
		data->desc.bDescriptorType = 0;
		data->hs_desc.bDescriptorType = 0;
		usb_ep_disable(data->ep);
	}
	up (&data->lock);
	put_ep (data);
	return 0;
}

static int ep_ioctl (struct inode *inode, struct file *fd,
		unsigned code, unsigned long value)
{
	struct ep_data		*data = fd->private_data;
	int			status;

	if ((status = get_ready_ep (fd->f_flags, data)) < 0)
		return status;

	spin_lock_irq (&data->dev->lock);
	if (likely (data->ep != NULL)) {
		switch (code) {
		case GADGETFS_FIFO_STATUS:
			status = usb_ep_fifo_status (data->ep);
			break;
		case GADGETFS_FIFO_FLUSH:
			usb_ep_fifo_flush (data->ep);
			break;
		case GADGETFS_CLEAR_HALT:
			status = usb_ep_clear_halt (data->ep);
			break;
		default:
			status = -ENOTTY;
		}
	} else
		status = -ENODEV;
	spin_unlock_irq (&data->dev->lock);
	up (&data->lock);
	return status;
}

/*----------------------------------------------------------------------*/

/* ASYNCHRONOUS ENDPOINT I/O OPERATIONS (bulk/intr/iso) */

struct kiocb_priv {
	struct usb_request	*req;
	struct ep_data		*epdata;
	void			*buf;
	const struct iovec	*iv;
	unsigned long		nr_segs;
	unsigned		actual;
};

static int ep_aio_cancel(struct kiocb *iocb, struct io_event *e)
{
	struct kiocb_priv	*priv = iocb->private;
	struct ep_data		*epdata;
	int			value;

	local_irq_disable();
	epdata = priv->epdata;
	// spin_lock(&epdata->dev->lock);
	kiocbSetCancelled(iocb);
	if (likely(epdata && epdata->ep && priv->req))
		value = usb_ep_dequeue (epdata->ep, priv->req);
	else
		value = -EINVAL;
	// spin_unlock(&epdata->dev->lock);
	local_irq_enable();

	aio_put_req(iocb);
	return value;
}

static ssize_t ep_aio_read_retry(struct kiocb *iocb)
{
	struct kiocb_priv	*priv = iocb->private;
	ssize_t			len, total;
	void			*to_copy;
	int			i;

	/* we "retry" to get the right mm context for this: */

	/* copy stuff into user buffers */
	total = priv->actual;
	len = 0;
	to_copy = priv->buf;
	for (i=0; i < priv->nr_segs; i++) {
		ssize_t this = min((ssize_t)(priv->iv[i].iov_len), total);

		if (copy_to_user(priv->iv[i].iov_base, to_copy, this)) {
			if (len == 0)
				len = -EFAULT;
			break;
		}

		total -= this;
		len += this;
		to_copy += this;
		if (total == 0)
			break;
	}
	kfree(priv->buf);
	kfree(priv);
	return len;
}

static void ep_aio_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct kiocb		*iocb = req->context;
	struct kiocb_priv	*priv = iocb->private;
	struct ep_data		*epdata = priv->epdata;

	/* lock against disconnect (and ideally, cancel) */
	spin_lock(&epdata->dev->lock);
	priv->req = NULL;
	priv->epdata = NULL;

	/* if this was a write or a read returning no data then we
	 * don't need to copy anything to userspace, so we can
	 * complete the aio request immediately.
	 */
	if (priv->iv == NULL || unlikely(req->actual == 0)) {
		kfree(req->buf);
		kfree(priv);
		iocb->private = NULL;
		/* aio_complete() reports bytes-transferred _and_ faults */
		aio_complete(iocb, req->actual ? req->actual : req->status,
				req->status);
	} else {
		/* retry() won't report both; so we hide some faults */
		if (unlikely(0 != req->status))
			DBG(epdata->dev, "%s fault %d len %d\n",
				ep->name, req->status, req->actual);

		priv->buf = req->buf;
		priv->actual = req->actual;
		kick_iocb(iocb);
	}
	spin_unlock(&epdata->dev->lock);

	usb_ep_free_request(ep, req);
	put_ep(epdata);
}

static ssize_t
ep_aio_rwtail(
	struct kiocb	*iocb,
	char		*buf,
	size_t		len,
	struct ep_data	*epdata,
	const struct iovec *iv,
	unsigned long	nr_segs
)
{
	struct kiocb_priv	*priv;
	struct usb_request	*req;
	ssize_t			value;

	priv = kmalloc(sizeof *priv, GFP_KERNEL);
	if (!priv) {
		value = -ENOMEM;
fail:
		kfree(buf);
		return value;
	}
	iocb->private = priv;
	priv->iv = iv;
	priv->nr_segs = nr_segs;

	value = get_ready_ep(iocb->ki_filp->f_flags, epdata);
	if (unlikely(value < 0)) {
		kfree(priv);
		goto fail;
	}

	iocb->ki_cancel = ep_aio_cancel;
	get_ep(epdata);
	priv->epdata = epdata;
	priv->actual = 0;

	/* each kiocb is coupled to one usb_request, but we can't
	 * allocate or submit those if the host disconnected.
	 */
	spin_lock_irq(&epdata->dev->lock);
	if (likely(epdata->ep)) {
		req = usb_ep_alloc_request(epdata->ep, GFP_ATOMIC);
		if (likely(req)) {
			priv->req = req;
			req->buf = buf;
			req->length = len;
			req->complete = ep_aio_complete;
			req->context = iocb;
			value = usb_ep_queue(epdata->ep, req, GFP_ATOMIC);
			if (unlikely(0 != value))
				usb_ep_free_request(epdata->ep, req);
		} else
			value = -EAGAIN;
	} else
		value = -ENODEV;
	spin_unlock_irq(&epdata->dev->lock);

	up(&epdata->lock);

	if (unlikely(value)) {
		kfree(priv);
		put_ep(epdata);
	} else
		value = (iv ? -EIOCBRETRY : -EIOCBQUEUED);
	return value;
}

static ssize_t
ep_aio_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t o)
{
	struct ep_data		*epdata = iocb->ki_filp->private_data;
	char			*buf;

	if (unlikely(epdata->desc.bEndpointAddress & USB_DIR_IN))
		return -EINVAL;

	buf = kmalloc(iocb->ki_left, GFP_KERNEL);
	if (unlikely(!buf))
		return -ENOMEM;

	iocb->ki_retry = ep_aio_read_retry;
	return ep_aio_rwtail(iocb, buf, iocb->ki_left, epdata, iov, nr_segs);
}

static ssize_t
ep_aio_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t o)
{
	struct ep_data		*epdata = iocb->ki_filp->private_data;
	char			*buf;
	size_t			len = 0;
	int			i = 0;

	if (unlikely(!(epdata->desc.bEndpointAddress & USB_DIR_IN)))
		return -EINVAL;

	buf = kmalloc(iocb->ki_left, GFP_KERNEL);
	if (unlikely(!buf))
		return -ENOMEM;

	for (i=0; i < nr_segs; i++) {
		if (unlikely(copy_from_user(&buf[len], iov[i].iov_base,
				iov[i].iov_len) != 0)) {
			kfree(buf);
			return -EFAULT;
		}
		len += iov[i].iov_len;
	}
	return ep_aio_rwtail(iocb, buf, len, epdata, NULL, 0);
}

/*----------------------------------------------------------------------*/

/* used after endpoint configuration */
static const struct file_operations ep_io_operations = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,

	.read =		ep_read,
	.write =	ep_write,
	.ioctl =	ep_ioctl,
	.release =	ep_release,

	.aio_read =	ep_aio_read,
	.aio_write =	ep_aio_write,
};

/* ENDPOINT INITIALIZATION
 *
 *     fd = open ("/dev/gadget/$ENDPOINT", O_RDWR)
 *     status = write (fd, descriptors, sizeof descriptors)
 *
 * That write establishes the endpoint configuration, configuring
 * the controller to process bulk, interrupt, or isochronous transfers
 * at the right maxpacket size, and so on.
 *
 * The descriptors are message type 1, identified by a host order u32
 * at the beginning of what's written.  Descriptor order is: full/low
 * speed descriptor, then optional high speed descriptor.
 */
static ssize_t
ep_config (struct file *fd, const char __user *buf, size_t len, loff_t *ptr)
{
	struct ep_data		*data = fd->private_data;
	struct usb_ep		*ep;
	u32			tag;
	int			value, length = len;

	if ((value = down_interruptible (&data->lock)) < 0)
		return value;

	if (data->state != STATE_EP_READY) {
		value = -EL2HLT;
		goto fail;
	}

	value = len;
	if (len < USB_DT_ENDPOINT_SIZE + 4)
		goto fail0;

	/* we might need to change message format someday */
	if (copy_from_user (&tag, buf, 4)) {
		goto fail1;
	}
	if (tag != 1) {
		DBG(data->dev, "config %s, bad tag %d\n", data->name, tag);
		goto fail0;
	}
	buf += 4;
	len -= 4;

	/* NOTE:  audio endpoint extensions not accepted here;
	 * just don't include the extra bytes.
	 */

	/* full/low speed descriptor, then high speed */
	if (copy_from_user (&data->desc, buf, USB_DT_ENDPOINT_SIZE)) {
		goto fail1;
	}
	if (data->desc.bLength != USB_DT_ENDPOINT_SIZE
			|| data->desc.bDescriptorType != USB_DT_ENDPOINT)
		goto fail0;
	if (len != USB_DT_ENDPOINT_SIZE) {
		if (len != 2 * USB_DT_ENDPOINT_SIZE)
			goto fail0;
		if (copy_from_user (&data->hs_desc, buf + USB_DT_ENDPOINT_SIZE,
					USB_DT_ENDPOINT_SIZE)) {
			goto fail1;
		}
		if (data->hs_desc.bLength != USB_DT_ENDPOINT_SIZE
				|| data->hs_desc.bDescriptorType
					!= USB_DT_ENDPOINT) {
			DBG(data->dev, "config %s, bad hs length or type\n",
					data->name);
			goto fail0;
		}
	}

	spin_lock_irq (&data->dev->lock);
	if (data->dev->state == STATE_DEV_UNBOUND) {
		value = -ENOENT;
		goto gone;
	} else if ((ep = data->ep) == NULL) {
		value = -ENODEV;
		goto gone;
	}
	switch (data->dev->gadget->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		value = usb_ep_enable (ep, &data->desc);
		if (value == 0)
			data->state = STATE_EP_ENABLED;
		break;
#ifdef	CONFIG_USB_GADGET_DUALSPEED
	case USB_SPEED_HIGH:
		/* fails if caller didn't provide that descriptor... */
		value = usb_ep_enable (ep, &data->hs_desc);
		if (value == 0)
			data->state = STATE_EP_ENABLED;
		break;
#endif
	default:
		DBG(data->dev, "unconnected, %s init abandoned\n",
				data->name);
		value = -EINVAL;
	}
	if (value == 0) {
		fd->f_op = &ep_io_operations;
		value = length;
	}
gone:
	spin_unlock_irq (&data->dev->lock);
	if (value < 0) {
fail:
		data->desc.bDescriptorType = 0;
		data->hs_desc.bDescriptorType = 0;
	}
	up (&data->lock);
	return value;
fail0:
	value = -EINVAL;
	goto fail;
fail1:
	value = -EFAULT;
	goto fail;
}

static int
ep_open (struct inode *inode, struct file *fd)
{
	struct ep_data		*data = inode->i_private;
	int			value = -EBUSY;

	if (down_interruptible (&data->lock) != 0)
		return -EINTR;
	spin_lock_irq (&data->dev->lock);
	if (data->dev->state == STATE_DEV_UNBOUND)
		value = -ENOENT;
	else if (data->state == STATE_EP_DISABLED) {
		value = 0;
		data->state = STATE_EP_READY;
		get_ep (data);
		fd->private_data = data;
		VDEBUG (data->dev, "%s ready\n", data->name);
	} else
		DBG (data->dev, "%s state %d\n",
			data->name, data->state);
	spin_unlock_irq (&data->dev->lock);
	up (&data->lock);
	return value;
}

/* used before endpoint configuration */
static const struct file_operations ep_config_operations = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,

	.open =		ep_open,
	.write =	ep_config,
	.release =	ep_release,
};

/*----------------------------------------------------------------------*/

/* EP0 IMPLEMENTATION can be partly in userspace.
 *
 * Drivers that use this facility receive various events, including
 * control requests the kernel doesn't handle.  Drivers that don't
 * use this facility may be too simple-minded for real applications.
 */

static inline void ep0_readable (struct dev_data *dev)
{
	wake_up (&dev->wait);
	kill_fasync (&dev->fasync, SIGIO, POLL_IN);
}

static void clean_req (struct usb_ep *ep, struct usb_request *req)
{
	struct dev_data		*dev = ep->driver_data;

	if (req->buf != dev->rbuf) {
		kfree(req->buf);
		req->buf = dev->rbuf;
		req->dma = DMA_ADDR_INVALID;
	}
	req->complete = epio_complete;
	dev->setup_out_ready = 0;
}

static void ep0_complete (struct usb_ep *ep, struct usb_request *req)
{
	struct dev_data		*dev = ep->driver_data;
	unsigned long		flags;
	int			free = 1;

	/* for control OUT, data must still get to userspace */
	spin_lock_irqsave(&dev->lock, flags);
	if (!dev->setup_in) {
		dev->setup_out_error = (req->status != 0);
		if (!dev->setup_out_error)
			free = 0;
		dev->setup_out_ready = 1;
		ep0_readable (dev);
	}

	/* clean up as appropriate */
	if (free && req->buf != &dev->rbuf)
		clean_req (ep, req);
	req->complete = epio_complete;
	spin_unlock_irqrestore(&dev->lock, flags);
}

static int setup_req (struct usb_ep *ep, struct usb_request *req, u16 len)
{
	struct dev_data	*dev = ep->driver_data;

	if (dev->setup_out_ready) {
		DBG (dev, "ep0 request busy!\n");
		return -EBUSY;
	}
	if (len > sizeof (dev->rbuf))
		req->buf = kmalloc(len, GFP_ATOMIC);
	if (req->buf == NULL) {
		req->buf = dev->rbuf;
		return -ENOMEM;
	}
	req->complete = ep0_complete;
	req->length = len;
	req->zero = 0;
	return 0;
}

static ssize_t
ep0_read (struct file *fd, char __user *buf, size_t len, loff_t *ptr)
{
	struct dev_data			*dev = fd->private_data;
	ssize_t				retval;
	enum ep0_state			state;

	spin_lock_irq (&dev->lock);

	/* report fd mode change before acting on it */
	if (dev->setup_abort) {
		dev->setup_abort = 0;
		retval = -EIDRM;
		goto done;
	}

	/* control DATA stage */
	if ((state = dev->state) == STATE_DEV_SETUP) {

		if (dev->setup_in) {		/* stall IN */
			VDEBUG(dev, "ep0in stall\n");
			(void) usb_ep_set_halt (dev->gadget->ep0);
			retval = -EL2HLT;
			dev->state = STATE_DEV_CONNECTED;

		} else if (len == 0) {		/* ack SET_CONFIGURATION etc */
			struct usb_ep		*ep = dev->gadget->ep0;
			struct usb_request	*req = dev->req;

			if ((retval = setup_req (ep, req, 0)) == 0)
				retval = usb_ep_queue (ep, req, GFP_ATOMIC);
			dev->state = STATE_DEV_CONNECTED;

			/* assume that was SET_CONFIGURATION */
			if (dev->current_config) {
				unsigned power;

				if (gadget_is_dualspeed(dev->gadget)
						&& (dev->gadget->speed
							== USB_SPEED_HIGH))
					power = dev->hs_config->bMaxPower;
				else
					power = dev->config->bMaxPower;
				usb_gadget_vbus_draw(dev->gadget, 2 * power);
			}

		} else {			/* collect OUT data */
			if ((fd->f_flags & O_NONBLOCK) != 0
					&& !dev->setup_out_ready) {
				retval = -EAGAIN;
				goto done;
			}
			spin_unlock_irq (&dev->lock);
			retval = wait_event_interruptible (dev->wait,
					dev->setup_out_ready != 0);

			/* FIXME state could change from under us */
			spin_lock_irq (&dev->lock);
			if (retval)
				goto done;

			if (dev->state != STATE_DEV_SETUP) {
				retval = -ECANCELED;
				goto done;
			}
			dev->state = STATE_DEV_CONNECTED;

			if (dev->setup_out_error)
				retval = -EIO;
			else {
				len = min (len, (size_t)dev->req->actual);
// FIXME don't call this with the spinlock held ...
				if (copy_to_user (buf, dev->req->buf, len))
					retval = -EFAULT;
				clean_req (dev->gadget->ep0, dev->req);
				/* NOTE userspace can't yet choose to stall */
			}
		}
		goto done;
	}

	/* else normal: return event data */
	if (len < sizeof dev->event [0]) {
		retval = -EINVAL;
		goto done;
	}
	len -= len % sizeof (struct usb_gadgetfs_event);
	dev->usermode_setup = 1;

scan:
	/* return queued events right away */
	if (dev->ev_next != 0) {
		unsigned		i, n;

		n = len / sizeof (struct usb_gadgetfs_event);
		if (dev->ev_next < n)
			n = dev->ev_next;

		/* ep0 i/o has special semantics during STATE_DEV_SETUP */
		for (i = 0; i < n; i++) {
			if (dev->event [i].type == GADGETFS_SETUP) {
				dev->state = STATE_DEV_SETUP;
				n = i + 1;
				break;
			}
		}
		spin_unlock_irq (&dev->lock);
		len = n * sizeof (struct usb_gadgetfs_event);
		if (copy_to_user (buf, &dev->event, len))
			retval = -EFAULT;
		else
			retval = len;
		if (len > 0) {
			/* NOTE this doesn't guard against broken drivers;
			 * concurrent ep0 readers may lose events.
			 */
			spin_lock_irq (&dev->lock);
			if (dev->ev_next > n) {
				memmove(&dev->event[0], &dev->event[n],
					sizeof (struct usb_gadgetfs_event)
						* (dev->ev_next - n));
			}
			dev->ev_next -= n;
			spin_unlock_irq (&dev->lock);
		}
		return retval;
	}
	if (fd->f_flags & O_NONBLOCK) {
		retval = -EAGAIN;
		goto done;
	}

	switch (state) {
	default:
		DBG (dev, "fail %s, state %d\n", __func__, state);
		retval = -ESRCH;
		break;
	case STATE_DEV_UNCONNECTED:
	case STATE_DEV_CONNECTED:
		spin_unlock_irq (&dev->lock);
		DBG (dev, "%s wait\n", __func__);

		/* wait for events */
		retval = wait_event_interruptible (dev->wait,
				dev->ev_next != 0);
		if (retval < 0)
			return retval;
		spin_lock_irq (&dev->lock);
		goto scan;
	}

done:
	spin_unlock_irq (&dev->lock);
	return retval;
}

static struct usb_gadgetfs_event *
next_event (struct dev_data *dev, enum usb_gadgetfs_event_type type)
{
	struct usb_gadgetfs_event	*event;
	unsigned			i;

	switch (type) {
	/* these events purge the queue */
	case GADGETFS_DISCONNECT:
		if (dev->state == STATE_DEV_SETUP)
			dev->setup_abort = 1;
		// FALL THROUGH
	case GADGETFS_CONNECT:
		dev->ev_next = 0;
		break;
	case GADGETFS_SETUP:		/* previous request timed out */
	case GADGETFS_SUSPEND:		/* same effect */
		/* these events can't be repeated */
		for (i = 0; i != dev->ev_next; i++) {
			if (dev->event [i].type != type)
				continue;
			DBG(dev, "discard old event[%d] %d\n", i, type);
			dev->ev_next--;
			if (i == dev->ev_next)
				break;
			/* indices start at zero, for simplicity */
			memmove (&dev->event [i], &dev->event [i + 1],
				sizeof (struct usb_gadgetfs_event)
					* (dev->ev_next - i));
		}
		break;
	default:
		BUG ();
	}
	VDEBUG(dev, "event[%d] = %d\n", dev->ev_next, type);
	event = &dev->event [dev->ev_next++];
	BUG_ON (dev->ev_next > N_EVENT);
	memset (event, 0, sizeof *event);
	event->type = type;
	return event;
}

static ssize_t
ep0_write (struct file *fd, const char __user *buf, size_t len, loff_t *ptr)
{
	struct dev_data		*dev = fd->private_data;
	ssize_t			retval = -ESRCH;

	spin_lock_irq (&dev->lock);

	/* report fd mode change before acting on it */
	if (dev->setup_abort) {
		dev->setup_abort = 0;
		retval = -EIDRM;

	/* data and/or status stage for control request */
	} else if (dev->state == STATE_DEV_SETUP) {

		/* IN DATA+STATUS caller makes len <= wLength */
		if (dev->setup_in) {
			retval = setup_req (dev->gadget->ep0, dev->req, len);
			if (retval == 0) {
				dev->state = STATE_DEV_CONNECTED;
				spin_unlock_irq (&dev->lock);
				if (copy_from_user (dev->req->buf, buf, len))
					retval = -EFAULT;
				else {
					if (len < dev->setup_wLength)
						dev->req->zero = 1;
					retval = usb_ep_queue (
						dev->gadget->ep0, dev->req,
						GFP_KERNEL);
				}
				if (retval < 0) {
					spin_lock_irq (&dev->lock);
					clean_req (dev->gadget->ep0, dev->req);
					spin_unlock_irq (&dev->lock);
				} else
					retval = len;

				return retval;
			}

		/* can stall some OUT transfers */
		} else if (dev->setup_can_stall) {
			VDEBUG(dev, "ep0out stall\n");
			(void) usb_ep_set_halt (dev->gadget->ep0);
			retval = -EL2HLT;
			dev->state = STATE_DEV_CONNECTED;
		} else {
			DBG(dev, "bogus ep0out stall!\n");
		}
	} else
		DBG (dev, "fail %s, state %d\n", __func__, dev->state);

	spin_unlock_irq (&dev->lock);
	return retval;
}

static int
ep0_fasync (int f, struct file *fd, int on)
{
	struct dev_data		*dev = fd->private_data;
	// caller must F_SETOWN before signal delivery happens
	VDEBUG (dev, "%s %s\n", __func__, on ? "on" : "off");
	return fasync_helper (f, fd, on, &dev->fasync);
}

static struct usb_gadget_driver gadgetfs_driver;

static int
dev_release (struct inode *inode, struct file *fd)
{
	struct dev_data		*dev = fd->private_data;

	/* closing ep0 === shutdown all */

	usb_gadget_unregister_driver (&gadgetfs_driver);

	/* at this point "good" hardware has disconnected the
	 * device from USB; the host won't see it any more.
	 * alternatively, all host requests will time out.
	 */

	fasync_helper (-1, fd, 0, &dev->fasync);
	kfree (dev->buf);
	dev->buf = NULL;
	put_dev (dev);

	/* other endpoints were all decoupled from this device */
	spin_lock_irq(&dev->lock);
	dev->state = STATE_DEV_DISABLED;
	spin_unlock_irq(&dev->lock);
	return 0;
}

static unsigned int
ep0_poll (struct file *fd, poll_table *wait)
{
       struct dev_data         *dev = fd->private_data;
       int                     mask = 0;

       poll_wait(fd, &dev->wait, wait);

       spin_lock_irq (&dev->lock);

       /* report fd mode change before acting on it */
       if (dev->setup_abort) {
               dev->setup_abort = 0;
               mask = POLLHUP;
               goto out;
       }

       if (dev->state == STATE_DEV_SETUP) {
               if (dev->setup_in || dev->setup_can_stall)
                       mask = POLLOUT;
       } else {
               if (dev->ev_next != 0)
                       mask = POLLIN;
       }
out:
       spin_unlock_irq(&dev->lock);
       return mask;
}

static int dev_ioctl (struct inode *inode, struct file *fd,
		unsigned code, unsigned long value)
{
	struct dev_data		*dev = fd->private_data;
	struct usb_gadget	*gadget = dev->gadget;

	if (gadget->ops->ioctl)
		return gadget->ops->ioctl (gadget, code, value);
	return -ENOTTY;
}

/* used after device configuration */
static const struct file_operations ep0_io_operations = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,

	.read =		ep0_read,
	.write =	ep0_write,
	.fasync =	ep0_fasync,
	.poll =		ep0_poll,
	.ioctl =	dev_ioctl,
	.release =	dev_release,
};

/*----------------------------------------------------------------------*/

/* The in-kernel gadget driver handles most ep0 issues, in particular
 * enumerating the single configuration (as provided from user space).
 *
 * Unrecognized ep0 requests may be handled in user space.
 */

#ifdef	CONFIG_USB_GADGET_DUALSPEED
static void make_qualifier (struct dev_data *dev)
{
	struct usb_qualifier_descriptor		qual;
	struct usb_device_descriptor		*desc;

	qual.bLength = sizeof qual;
	qual.bDescriptorType = USB_DT_DEVICE_QUALIFIER;
	qual.bcdUSB = __constant_cpu_to_le16 (0x0200);

	desc = dev->dev;
	qual.bDeviceClass = desc->bDeviceClass;
	qual.bDeviceSubClass = desc->bDeviceSubClass;
	qual.bDeviceProtocol = desc->bDeviceProtocol;

	/* assumes ep0 uses the same value for both speeds ... */
	qual.bMaxPacketSize0 = desc->bMaxPacketSize0;

	qual.bNumConfigurations = 1;
	qual.bRESERVED = 0;

	memcpy (dev->rbuf, &qual, sizeof qual);
}
#endif

static int
config_buf (struct dev_data *dev, u8 type, unsigned index)
{
	int		len;
	int		hs = 0;

	/* only one configuration */
	if (index > 0)
		return -EINVAL;

	if (gadget_is_dualspeed(dev->gadget)) {
		hs = (dev->gadget->speed == USB_SPEED_HIGH);
		if (type == USB_DT_OTHER_SPEED_CONFIG)
			hs = !hs;
	}
	if (hs) {
		dev->req->buf = dev->hs_config;
		len = le16_to_cpu(dev->hs_config->wTotalLength);
	} else {
		dev->req->buf = dev->config;
		len = le16_to_cpu(dev->config->wTotalLength);
	}
	((u8 *)dev->req->buf) [1] = type;
	return len;
}

static int
gadgetfs_setup (struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	struct dev_data			*dev = get_gadget_data (gadget);
	struct usb_request		*req = dev->req;
	int				value = -EOPNOTSUPP;
	struct usb_gadgetfs_event	*event;
	u16				w_value = le16_to_cpu(ctrl->wValue);
	u16				w_length = le16_to_cpu(ctrl->wLength);

	spin_lock (&dev->lock);
	dev->setup_abort = 0;
	if (dev->state == STATE_DEV_UNCONNECTED) {
		if (gadget_is_dualspeed(gadget)
				&& gadget->speed == USB_SPEED_HIGH
				&& dev->hs_config == NULL) {
			spin_unlock(&dev->lock);
			ERROR (dev, "no high speed config??\n");
			return -EINVAL;
		}

		dev->state = STATE_DEV_CONNECTED;
		dev->dev->bMaxPacketSize0 = gadget->ep0->maxpacket;

		INFO (dev, "connected\n");
		event = next_event (dev, GADGETFS_CONNECT);
		event->u.speed = gadget->speed;
		ep0_readable (dev);

	/* host may have given up waiting for response.  we can miss control
	 * requests handled lower down (device/endpoint status and features);
	 * then ep0_{read,write} will report the wrong status. controller
	 * driver will have aborted pending i/o.
	 */
	} else if (dev->state == STATE_DEV_SETUP)
		dev->setup_abort = 1;

	req->buf = dev->rbuf;
	req->dma = DMA_ADDR_INVALID;
	req->context = NULL;
	value = -EOPNOTSUPP;
	switch (ctrl->bRequest) {

	case USB_REQ_GET_DESCRIPTOR:
		if (ctrl->bRequestType != USB_DIR_IN)
			goto unrecognized;
		switch (w_value >> 8) {

		case USB_DT_DEVICE:
			value = min (w_length, (u16) sizeof *dev->dev);
			req->buf = dev->dev;
			break;
#ifdef	CONFIG_USB_GADGET_DUALSPEED
		case USB_DT_DEVICE_QUALIFIER:
			if (!dev->hs_config)
				break;
			value = min (w_length, (u16)
				sizeof (struct usb_qualifier_descriptor));
			make_qualifier (dev);
			break;
		case USB_DT_OTHER_SPEED_CONFIG:
			// FALLTHROUGH
#endif
		case USB_DT_CONFIG:
			value = config_buf (dev,
					w_value >> 8,
					w_value & 0xff);
			if (value >= 0)
				value = min (w_length, (u16) value);
			break;
		case USB_DT_STRING:
			goto unrecognized;

		default:		// all others are errors
			break;
		}
		break;

	/* currently one config, two speeds */
	case USB_REQ_SET_CONFIGURATION:
		if (ctrl->bRequestType != 0)
			goto unrecognized;
		if (0 == (u8) w_value) {
			value = 0;
			dev->current_config = 0;
			usb_gadget_vbus_draw(gadget, 8 /* mA */ );
			// user mode expected to disable endpoints
		} else {
			u8	config, power;

			if (gadget_is_dualspeed(gadget)
					&& gadget->speed == USB_SPEED_HIGH) {
				config = dev->hs_config->bConfigurationValue;
				power = dev->hs_config->bMaxPower;
			} else {
				config = dev->config->bConfigurationValue;
				power = dev->config->bMaxPower;
			}

			if (config == (u8) w_value) {
				value = 0;
				dev->current_config = config;
				usb_gadget_vbus_draw(gadget, 2 * power);
			}
		}

		/* report SET_CONFIGURATION like any other control request,
		 * except that usermode may not stall this.  the next
		 * request mustn't be allowed start until this finishes:
		 * endpoints and threads set up, etc.
		 *
		 * NOTE:  older PXA hardware (before PXA 255: without UDCCFR)
		 * has bad/racey automagic that prevents synchronizing here.
		 * even kernel mode drivers often miss them.
		 */
		if (value == 0) {
			INFO (dev, "configuration #%d\n", dev->current_config);
			if (dev->usermode_setup) {
				dev->setup_can_stall = 0;
				goto delegate;
			}
		}
		break;

#ifndef	CONFIG_USB_GADGET_PXA2XX
	/* PXA automagically handles this request too */
	case USB_REQ_GET_CONFIGURATION:
		if (ctrl->bRequestType != 0x80)
			goto unrecognized;
		*(u8 *)req->buf = dev->current_config;
		value = min (w_length, (u16) 1);
		break;
#endif

	default:
unrecognized:
		VDEBUG (dev, "%s req%02x.%02x v%04x i%04x l%d\n",
			dev->usermode_setup ? "delegate" : "fail",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, le16_to_cpu(ctrl->wIndex), w_length);

		/* if there's an ep0 reader, don't stall */
		if (dev->usermode_setup) {
			dev->setup_can_stall = 1;
delegate:
			dev->setup_in = (ctrl->bRequestType & USB_DIR_IN)
						? 1 : 0;
			dev->setup_wLength = w_length;
			dev->setup_out_ready = 0;
			dev->setup_out_error = 0;
			value = 0;

			/* read DATA stage for OUT right away */
			if (unlikely (!dev->setup_in && w_length)) {
				value = setup_req (gadget->ep0, dev->req,
							w_length);
				if (value < 0)
					break;
				value = usb_ep_queue (gadget->ep0, dev->req,
							GFP_ATOMIC);
				if (value < 0) {
					clean_req (gadget->ep0, dev->req);
					break;
				}

				/* we can't currently stall these */
				dev->setup_can_stall = 0;
			}

			/* state changes when reader collects event */
			event = next_event (dev, GADGETFS_SETUP);
			event->u.setup = *ctrl;
			ep0_readable (dev);
			spin_unlock (&dev->lock);
			return 0;
		}
	}

	/* proceed with data transfer and status phases? */
	if (value >= 0 && dev->state != STATE_DEV_SETUP) {
		req->length = value;
		req->zero = value < w_length;
		value = usb_ep_queue (gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			DBG (dev, "ep_queue --> %d\n", value);
			req->status = 0;
		}
	}

	/* device stalls when value < 0 */
	spin_unlock (&dev->lock);
	return value;
}

static void destroy_ep_files (struct dev_data *dev)
{
	struct list_head	*entry, *tmp;

	DBG (dev, "%s %d\n", __func__, dev->state);

	/* dev->state must prevent interference */
restart:
	spin_lock_irq (&dev->lock);
	list_for_each_safe (entry, tmp, &dev->epfiles) {
		struct ep_data	*ep;
		struct inode	*parent;
		struct dentry	*dentry;

		/* break link to FS */
		ep = list_entry (entry, struct ep_data, epfiles);
		list_del_init (&ep->epfiles);
		dentry = ep->dentry;
		ep->dentry = NULL;
		parent = dentry->d_parent->d_inode;

		/* break link to controller */
		if (ep->state == STATE_EP_ENABLED)
			(void) usb_ep_disable (ep->ep);
		ep->state = STATE_EP_UNBOUND;
		usb_ep_free_request (ep->ep, ep->req);
		ep->ep = NULL;
		wake_up (&ep->wait);
		put_ep (ep);

		spin_unlock_irq (&dev->lock);

		/* break link to dcache */
		mutex_lock (&parent->i_mutex);
		d_delete (dentry);
		dput (dentry);
		mutex_unlock (&parent->i_mutex);

		/* fds may still be open */
		goto restart;
	}
	spin_unlock_irq (&dev->lock);
}


static struct inode *
gadgetfs_create_file (struct super_block *sb, char const *name,
		void *data, const struct file_operations *fops,
		struct dentry **dentry_p);

static int activate_ep_files (struct dev_data *dev)
{
	struct usb_ep	*ep;
	struct ep_data	*data;

	gadget_for_each_ep (ep, dev->gadget) {

		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			goto enomem0;
		data->state = STATE_EP_DISABLED;
		init_MUTEX (&data->lock);
		init_waitqueue_head (&data->wait);

		strncpy (data->name, ep->name, sizeof (data->name) - 1);
		atomic_set (&data->count, 1);
		data->dev = dev;
		get_dev (dev);

		data->ep = ep;
		ep->driver_data = data;

		data->req = usb_ep_alloc_request (ep, GFP_KERNEL);
		if (!data->req)
			goto enomem1;

		data->inode = gadgetfs_create_file (dev->sb, data->name,
				data, &ep_config_operations,
				&data->dentry);
		if (!data->inode)
			goto enomem2;
		list_add_tail (&data->epfiles, &dev->epfiles);
	}
	return 0;

enomem2:
	usb_ep_free_request (ep, data->req);
enomem1:
	put_dev (dev);
	kfree (data);
enomem0:
	DBG (dev, "%s enomem\n", __func__);
	destroy_ep_files (dev);
	return -ENOMEM;
}

static void
gadgetfs_unbind (struct usb_gadget *gadget)
{
	struct dev_data		*dev = get_gadget_data (gadget);

	DBG (dev, "%s\n", __func__);

	spin_lock_irq (&dev->lock);
	dev->state = STATE_DEV_UNBOUND;
	spin_unlock_irq (&dev->lock);

	destroy_ep_files (dev);
	gadget->ep0->driver_data = NULL;
	set_gadget_data (gadget, NULL);

	/* we've already been disconnected ... no i/o is active */
	if (dev->req)
		usb_ep_free_request (gadget->ep0, dev->req);
	DBG (dev, "%s done\n", __func__);
	put_dev (dev);
}

static struct dev_data		*the_device;

static int
gadgetfs_bind (struct usb_gadget *gadget)
{
	struct dev_data		*dev = the_device;

	if (!dev)
		return -ESRCH;
	if (0 != strcmp (CHIP, gadget->name)) {
		pr_err("%s expected %s controller not %s\n",
			shortname, CHIP, gadget->name);
		return -ENODEV;
	}

	set_gadget_data (gadget, dev);
	dev->gadget = gadget;
	gadget->ep0->driver_data = dev;
	dev->dev->bMaxPacketSize0 = gadget->ep0->maxpacket;

	/* preallocate control response and buffer */
	dev->req = usb_ep_alloc_request (gadget->ep0, GFP_KERNEL);
	if (!dev->req)
		goto enomem;
	dev->req->context = NULL;
	dev->req->complete = epio_complete;

	if (activate_ep_files (dev) < 0)
		goto enomem;

	INFO (dev, "bound to %s driver\n", gadget->name);
	spin_lock_irq(&dev->lock);
	dev->state = STATE_DEV_UNCONNECTED;
	spin_unlock_irq(&dev->lock);
	get_dev (dev);
	return 0;

enomem:
	gadgetfs_unbind (gadget);
	return -ENOMEM;
}

static void
gadgetfs_disconnect (struct usb_gadget *gadget)
{
	struct dev_data		*dev = get_gadget_data (gadget);

	spin_lock (&dev->lock);
	if (dev->state == STATE_DEV_UNCONNECTED)
		goto exit;
	dev->state = STATE_DEV_UNCONNECTED;

	INFO (dev, "disconnected\n");
	next_event (dev, GADGETFS_DISCONNECT);
	ep0_readable (dev);
exit:
	spin_unlock (&dev->lock);
}

static void
gadgetfs_suspend (struct usb_gadget *gadget)
{
	struct dev_data		*dev = get_gadget_data (gadget);

	INFO (dev, "suspended from state %d\n", dev->state);
	spin_lock (&dev->lock);
	switch (dev->state) {
	case STATE_DEV_SETUP:		// VERY odd... host died??
	case STATE_DEV_CONNECTED:
	case STATE_DEV_UNCONNECTED:
		next_event (dev, GADGETFS_SUSPEND);
		ep0_readable (dev);
		/* FALLTHROUGH */
	default:
		break;
	}
	spin_unlock (&dev->lock);
}

static struct usb_gadget_driver gadgetfs_driver = {
#ifdef	CONFIG_USB_GADGET_DUALSPEED
	.speed		= USB_SPEED_HIGH,
#else
	.speed		= USB_SPEED_FULL,
#endif
	.function	= (char *) driver_desc,
	.bind		= gadgetfs_bind,
	.unbind		= gadgetfs_unbind,
	.setup		= gadgetfs_setup,
	.disconnect	= gadgetfs_disconnect,
	.suspend	= gadgetfs_suspend,

	.driver	= {
		.name		= (char *) shortname,
	},
};

/*----------------------------------------------------------------------*/

static void gadgetfs_nop(struct usb_gadget *arg) { }

static int gadgetfs_probe (struct usb_gadget *gadget)
{
	CHIP = gadget->name;
	return -EISNAM;
}

static struct usb_gadget_driver probe_driver = {
	.speed		= USB_SPEED_HIGH,
	.bind		= gadgetfs_probe,
	.unbind		= gadgetfs_nop,
	.setup		= (void *)gadgetfs_nop,
	.disconnect	= gadgetfs_nop,
	.driver	= {
		.name		= "nop",
	},
};


/* DEVICE INITIALIZATION
 *
 *     fd = open ("/dev/gadget/$CHIP", O_RDWR)
 *     status = write (fd, descriptors, sizeof descriptors)
 *
 * That write establishes the device configuration, so the kernel can
 * bind to the controller ... guaranteeing it can handle enumeration
 * at all necessary speeds.  Descriptor order is:
 *
 * . message tag (u32, host order) ... for now, must be zero; it
 *	would change to support features like multi-config devices
 * . full/low speed config ... all wTotalLength bytes (with interface,
 *	class, altsetting, endpoint, and other descriptors)
 * . high speed config ... all descriptors, for high speed operation;
 *	this one's optional except for high-speed hardware
 * . device descriptor
 *
 * Endpoints are not yet enabled. Drivers must wait until device
 * configuration and interface altsetting changes create
 * the need to configure (or unconfigure) them.
 *
 * After initialization, the device stays active for as long as that
 * $CHIP file is open.  Events must then be read from that descriptor,
 * such as configuration notifications.
 */

static int is_valid_config (struct usb_config_descriptor *config)
{
	return config->bDescriptorType == USB_DT_CONFIG
		&& config->bLength == USB_DT_CONFIG_SIZE
		&& config->bConfigurationValue != 0
		&& (config->bmAttributes & USB_CONFIG_ATT_ONE) != 0
		&& (config->bmAttributes & USB_CONFIG_ATT_WAKEUP) == 0;
	/* FIXME if gadget->is_otg, _must_ include an otg descriptor */
	/* FIXME check lengths: walk to end */
}

static ssize_t
dev_config (struct file *fd, const char __user *buf, size_t len, loff_t *ptr)
{
	struct dev_data		*dev = fd->private_data;
	ssize_t			value = len, length = len;
	unsigned		total;
	u32			tag;
	char			*kbuf;

	if (len < (USB_DT_CONFIG_SIZE + USB_DT_DEVICE_SIZE + 4))
		return -EINVAL;

	/* we might need to change message format someday */
	if (copy_from_user (&tag, buf, 4))
		return -EFAULT;
	if (tag != 0)
		return -EINVAL;
	buf += 4;
	length -= 4;

	kbuf = kmalloc (length, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;
	if (copy_from_user (kbuf, buf, length)) {
		kfree (kbuf);
		return -EFAULT;
	}

	spin_lock_irq (&dev->lock);
	value = -EINVAL;
	if (dev->buf)
		goto fail;
	dev->buf = kbuf;

	/* full or low speed config */
	dev->config = (void *) kbuf;
	total = le16_to_cpu(dev->config->wTotalLength);
	if (!is_valid_config (dev->config) || total >= length)
		goto fail;
	kbuf += total;
	length -= total;

	/* optional high speed config */
	if (kbuf [1] == USB_DT_CONFIG) {
		dev->hs_config = (void *) kbuf;
		total = le16_to_cpu(dev->hs_config->wTotalLength);
		if (!is_valid_config (dev->hs_config) || total >= length)
			goto fail;
		kbuf += total;
		length -= total;
	}

	/* could support multiple configs, using another encoding! */

	/* device descriptor (tweaked for paranoia) */
	if (length != USB_DT_DEVICE_SIZE)
		goto fail;
	dev->dev = (void *)kbuf;
	if (dev->dev->bLength != USB_DT_DEVICE_SIZE
			|| dev->dev->bDescriptorType != USB_DT_DEVICE
			|| dev->dev->bNumConfigurations != 1)
		goto fail;
	dev->dev->bNumConfigurations = 1;
	dev->dev->bcdUSB = __constant_cpu_to_le16 (0x0200);

	/* triggers gadgetfs_bind(); then we can enumerate. */
	spin_unlock_irq (&dev->lock);
	value = usb_gadget_register_driver (&gadgetfs_driver);
	if (value != 0) {
		kfree (dev->buf);
		dev->buf = NULL;
	} else {
		/* at this point "good" hardware has for the first time
		 * let the USB the host see us.  alternatively, if users
		 * unplug/replug that will clear all the error state.
		 *
		 * note:  everything running before here was guaranteed
		 * to choke driver model style diagnostics.  from here
		 * on, they can work ... except in cleanup paths that
		 * kick in after the ep0 descriptor is closed.
		 */
		fd->f_op = &ep0_io_operations;
		value = len;
	}
	return value;

fail:
	spin_unlock_irq (&dev->lock);
	pr_debug ("%s: %s fail %Zd, %p\n", shortname, __func__, value, dev);
	kfree (dev->buf);
	dev->buf = NULL;
	return value;
}

static int
dev_open (struct inode *inode, struct file *fd)
{
	struct dev_data		*dev = inode->i_private;
	int			value = -EBUSY;

	spin_lock_irq(&dev->lock);
	if (dev->state == STATE_DEV_DISABLED) {
		dev->ev_next = 0;
		dev->state = STATE_DEV_OPENED;
		fd->private_data = dev;
		get_dev (dev);
		value = 0;
	}
	spin_unlock_irq(&dev->lock);
	return value;
}

static const struct file_operations dev_init_operations = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,

	.open =		dev_open,
	.write =	dev_config,
	.fasync =	ep0_fasync,
	.ioctl =	dev_ioctl,
	.release =	dev_release,
};

/*----------------------------------------------------------------------*/

/* FILESYSTEM AND SUPERBLOCK OPERATIONS
 *
 * Mounting the filesystem creates a controller file, used first for
 * device configuration then later for event monitoring.
 */


/* FIXME PAM etc could set this security policy without mount options
 * if epfiles inherited ownership and permissons from ep0 ...
 */

static unsigned default_uid;
static unsigned default_gid;
static unsigned default_perm = S_IRUSR | S_IWUSR;

module_param (default_uid, uint, 0644);
module_param (default_gid, uint, 0644);
module_param (default_perm, uint, 0644);


static struct inode *
gadgetfs_make_inode (struct super_block *sb,
		void *data, const struct file_operations *fops,
		int mode)
{
	struct inode *inode = new_inode (sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = default_uid;
		inode->i_gid = default_gid;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime
				= CURRENT_TIME;
		inode->i_private = data;
		inode->i_fop = fops;
	}
	return inode;
}

/* creates in fs root directory, so non-renamable and non-linkable.
 * so inode and dentry are paired, until device reconfig.
 */
static struct inode *
gadgetfs_create_file (struct super_block *sb, char const *name,
		void *data, const struct file_operations *fops,
		struct dentry **dentry_p)
{
	struct dentry	*dentry;
	struct inode	*inode;

	dentry = d_alloc_name(sb->s_root, name);
	if (!dentry)
		return NULL;

	inode = gadgetfs_make_inode (sb, data, fops,
			S_IFREG | (default_perm & S_IRWXUGO));
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	d_add (dentry, inode);
	*dentry_p = dentry;
	return inode;
}

static struct super_operations gadget_fs_operations = {
	.statfs =	simple_statfs,
	.drop_inode =	generic_delete_inode,
};

static int
gadgetfs_fill_super (struct super_block *sb, void *opts, int silent)
{
	struct inode	*inode;
	struct dentry	*d;
	struct dev_data	*dev;

	if (the_device)
		return -ESRCH;

	/* fake probe to determine $CHIP */
	(void) usb_gadget_register_driver (&probe_driver);
	if (!CHIP)
		return -ENODEV;

	/* superblock */
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = GADGETFS_MAGIC;
	sb->s_op = &gadget_fs_operations;
	sb->s_time_gran = 1;

	/* root inode */
	inode = gadgetfs_make_inode (sb,
			NULL, &simple_dir_operations,
			S_IFDIR | S_IRUGO | S_IXUGO);
	if (!inode)
		goto enomem0;
	inode->i_op = &simple_dir_inode_operations;
	if (!(d = d_alloc_root (inode)))
		goto enomem1;
	sb->s_root = d;

	/* the ep0 file is named after the controller we expect;
	 * user mode code can use it for sanity checks, like we do.
	 */
	dev = dev_new ();
	if (!dev)
		goto enomem2;

	dev->sb = sb;
	if (!gadgetfs_create_file (sb, CHIP,
				dev, &dev_init_operations,
				&dev->dentry))
		goto enomem3;

	/* other endpoint files are available after hardware setup,
	 * from binding to a controller.
	 */
	the_device = dev;
	return 0;

enomem3:
	put_dev (dev);
enomem2:
	dput (d);
enomem1:
	iput (inode);
enomem0:
	return -ENOMEM;
}

/* "mount -t gadgetfs path /dev/gadget" ends up here */
static int
gadgetfs_get_sb (struct file_system_type *t, int flags,
		const char *path, void *opts, struct vfsmount *mnt)
{
	return get_sb_single (t, flags, opts, gadgetfs_fill_super, mnt);
}

static void
gadgetfs_kill_sb (struct super_block *sb)
{
	kill_litter_super (sb);
	if (the_device) {
		put_dev (the_device);
		the_device = NULL;
	}
}

/*----------------------------------------------------------------------*/

static struct file_system_type gadgetfs_type = {
	.owner		= THIS_MODULE,
	.name		= shortname,
	.get_sb		= gadgetfs_get_sb,
	.kill_sb	= gadgetfs_kill_sb,
};

/*----------------------------------------------------------------------*/

static int __init init (void)
{
	int status;

	status = register_filesystem (&gadgetfs_type);
	if (status == 0)
		pr_info ("%s: %s, version " DRIVER_VERSION "\n",
			shortname, driver_desc);
	return status;
}
module_init (init);

static void __exit cleanup (void)
{
	pr_debug ("unregister %s\n", shortname);
	unregister_filesystem (&gadgetfs_type);
}
module_exit (cleanup);

