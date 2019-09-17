// SPDX-License-Identifier: GPL-2.0
/*
 * cdc-wdm.c
 *
 * This driver supports USB CDC WCM Device Management.
 *
 * Copyright (c) 2007-2009 Oliver Neukum
 *
 * Some code taken from cdc-acm.c
 *
 * Released under the GPLv2.
 *
 * Many thanks to Carl Nordbeck
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/poll.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/usb/cdc-wdm.h>

#define DRIVER_AUTHOR "Oliver Neukum"
#define DRIVER_DESC "USB Abstract Control Model driver for USB WCM Device Management"

static const struct usb_device_id wdm_ids[] = {
	{
		.match_flags = USB_DEVICE_ID_MATCH_INT_CLASS |
				 USB_DEVICE_ID_MATCH_INT_SUBCLASS,
		.bInterfaceClass = USB_CLASS_COMM,
		.bInterfaceSubClass = USB_CDC_SUBCLASS_DMM
	},
	{ }
};

MODULE_DEVICE_TABLE (usb, wdm_ids);

#define WDM_MINOR_BASE	176


#define WDM_IN_USE		1
#define WDM_DISCONNECTING	2
#define WDM_RESULT		3
#define WDM_READ		4
#define WDM_INT_STALL		5
#define WDM_POLL_RUNNING	6
#define WDM_RESPONDING		7
#define WDM_SUSPENDING		8
#define WDM_RESETTING		9
#define WDM_OVERFLOW		10

#define WDM_MAX			16

/* CDC-WMC r1.1 requires wMaxCommand to be "at least 256 decimal (0x100)" */
#define WDM_DEFAULT_BUFSIZE	256

static DEFINE_MUTEX(wdm_mutex);
static DEFINE_SPINLOCK(wdm_device_list_lock);
static LIST_HEAD(wdm_device_list);

/* --- method tables --- */

struct wdm_device {
	u8			*inbuf; /* buffer for response */
	u8			*outbuf; /* buffer for command */
	u8			*sbuf; /* buffer for status */
	u8			*ubuf; /* buffer for copy to user space */

	struct urb		*command;
	struct urb		*response;
	struct urb		*validity;
	struct usb_interface	*intf;
	struct usb_ctrlrequest	*orq;
	struct usb_ctrlrequest	*irq;
	spinlock_t		iuspin;

	unsigned long		flags;
	u16			bufsize;
	u16			wMaxCommand;
	u16			wMaxPacketSize;
	__le16			inum;
	int			reslength;
	int			length;
	int			read;
	int			count;
	dma_addr_t		shandle;
	dma_addr_t		ihandle;
	struct mutex		wlock;
	struct mutex		rlock;
	wait_queue_head_t	wait;
	struct work_struct	rxwork;
	struct work_struct	service_outs_intr;
	int			werr;
	int			rerr;
	int                     resp_count;

	struct list_head	device_list;
	int			(*manage_power)(struct usb_interface *, int);
};

static struct usb_driver wdm_driver;

/* return intfdata if we own the interface, else look up intf in the list */
static struct wdm_device *wdm_find_device(struct usb_interface *intf)
{
	struct wdm_device *desc;

	spin_lock(&wdm_device_list_lock);
	list_for_each_entry(desc, &wdm_device_list, device_list)
		if (desc->intf == intf)
			goto found;
	desc = NULL;
found:
	spin_unlock(&wdm_device_list_lock);

	return desc;
}

static struct wdm_device *wdm_find_device_by_minor(int minor)
{
	struct wdm_device *desc;

	spin_lock(&wdm_device_list_lock);
	list_for_each_entry(desc, &wdm_device_list, device_list)
		if (desc->intf->minor == minor)
			goto found;
	desc = NULL;
found:
	spin_unlock(&wdm_device_list_lock);

	return desc;
}

/* --- callbacks --- */
static void wdm_out_callback(struct urb *urb)
{
	struct wdm_device *desc;
	unsigned long flags;

	desc = urb->context;
	spin_lock_irqsave(&desc->iuspin, flags);
	desc->werr = urb->status;
	spin_unlock_irqrestore(&desc->iuspin, flags);
	kfree(desc->outbuf);
	desc->outbuf = NULL;
	clear_bit(WDM_IN_USE, &desc->flags);
	wake_up(&desc->wait);
}

static void wdm_in_callback(struct urb *urb)
{
	unsigned long flags;
	struct wdm_device *desc = urb->context;
	int status = urb->status;
	int length = urb->actual_length;

	spin_lock_irqsave(&desc->iuspin, flags);
	clear_bit(WDM_RESPONDING, &desc->flags);

	if (status) {
		switch (status) {
		case -ENOENT:
			dev_dbg(&desc->intf->dev,
				"nonzero urb status received: -ENOENT\n");
			goto skip_error;
		case -ECONNRESET:
			dev_dbg(&desc->intf->dev,
				"nonzero urb status received: -ECONNRESET\n");
			goto skip_error;
		case -ESHUTDOWN:
			dev_dbg(&desc->intf->dev,
				"nonzero urb status received: -ESHUTDOWN\n");
			goto skip_error;
		case -EPIPE:
			dev_err(&desc->intf->dev,
				"nonzero urb status received: -EPIPE\n");
			break;
		default:
			dev_err(&desc->intf->dev,
				"Unexpected error %d\n", status);
			break;
		}
	}

	/*
	 * only set a new error if there is no previous error.
	 * Errors are only cleared during read/open
	 * Avoid propagating -EPIPE (stall) to userspace since it is
	 * better handled as an empty read
	 */
	if (desc->rerr == 0 && status != -EPIPE)
		desc->rerr = status;

	if (length + desc->length > desc->wMaxCommand) {
		/* The buffer would overflow */
		set_bit(WDM_OVERFLOW, &desc->flags);
	} else {
		/* we may already be in overflow */
		if (!test_bit(WDM_OVERFLOW, &desc->flags)) {
			memmove(desc->ubuf + desc->length, desc->inbuf, length);
			desc->length += length;
			desc->reslength = length;
		}
	}
skip_error:

	if (desc->rerr) {
		/*
		 * Since there was an error, userspace may decide to not read
		 * any data after poll'ing.
		 * We should respond to further attempts from the device to send
		 * data, so that we can get unstuck.
		 */
		schedule_work(&desc->service_outs_intr);
	} else {
		set_bit(WDM_READ, &desc->flags);
		wake_up(&desc->wait);
	}
	spin_unlock_irqrestore(&desc->iuspin, flags);
}

static void wdm_int_callback(struct urb *urb)
{
	unsigned long flags;
	int rv = 0;
	int responding;
	int status = urb->status;
	struct wdm_device *desc;
	struct usb_cdc_notification *dr;

	desc = urb->context;
	dr = (struct usb_cdc_notification *)desc->sbuf;

	if (status) {
		switch (status) {
		case -ESHUTDOWN:
		case -ENOENT:
		case -ECONNRESET:
			return; /* unplug */
		case -EPIPE:
			set_bit(WDM_INT_STALL, &desc->flags);
			dev_err(&desc->intf->dev, "Stall on int endpoint\n");
			goto sw; /* halt is cleared in work */
		default:
			dev_err(&desc->intf->dev,
				"nonzero urb status received: %d\n", status);
			break;
		}
	}

	if (urb->actual_length < sizeof(struct usb_cdc_notification)) {
		dev_err(&desc->intf->dev, "wdm_int_callback - %d bytes\n",
			urb->actual_length);
		goto exit;
	}

	switch (dr->bNotificationType) {
	case USB_CDC_NOTIFY_RESPONSE_AVAILABLE:
		dev_dbg(&desc->intf->dev,
			"NOTIFY_RESPONSE_AVAILABLE received: index %d len %d\n",
			le16_to_cpu(dr->wIndex), le16_to_cpu(dr->wLength));
		break;

	case USB_CDC_NOTIFY_NETWORK_CONNECTION:

		dev_dbg(&desc->intf->dev,
			"NOTIFY_NETWORK_CONNECTION %s network\n",
			dr->wValue ? "connected to" : "disconnected from");
		goto exit;
	case USB_CDC_NOTIFY_SPEED_CHANGE:
		dev_dbg(&desc->intf->dev, "SPEED_CHANGE received (len %u)\n",
			urb->actual_length);
		goto exit;
	default:
		clear_bit(WDM_POLL_RUNNING, &desc->flags);
		dev_err(&desc->intf->dev,
			"unknown notification %d received: index %d len %d\n",
			dr->bNotificationType,
			le16_to_cpu(dr->wIndex),
			le16_to_cpu(dr->wLength));
		goto exit;
	}

	spin_lock_irqsave(&desc->iuspin, flags);
	responding = test_and_set_bit(WDM_RESPONDING, &desc->flags);
	if (!desc->resp_count++ && !responding
		&& !test_bit(WDM_DISCONNECTING, &desc->flags)
		&& !test_bit(WDM_SUSPENDING, &desc->flags)) {
		rv = usb_submit_urb(desc->response, GFP_ATOMIC);
		dev_dbg(&desc->intf->dev, "submit response URB %d\n", rv);
	}
	spin_unlock_irqrestore(&desc->iuspin, flags);
	if (rv < 0) {
		clear_bit(WDM_RESPONDING, &desc->flags);
		if (rv == -EPERM)
			return;
		if (rv == -ENOMEM) {
sw:
			rv = schedule_work(&desc->rxwork);
			if (rv)
				dev_err(&desc->intf->dev,
					"Cannot schedule work\n");
		}
	}
exit:
	rv = usb_submit_urb(urb, GFP_ATOMIC);
	if (rv)
		dev_err(&desc->intf->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, rv);

}

static void kill_urbs(struct wdm_device *desc)
{
	/* the order here is essential */
	usb_kill_urb(desc->command);
	usb_kill_urb(desc->validity);
	usb_kill_urb(desc->response);
}

static void free_urbs(struct wdm_device *desc)
{
	usb_free_urb(desc->validity);
	usb_free_urb(desc->response);
	usb_free_urb(desc->command);
}

static void cleanup(struct wdm_device *desc)
{
	kfree(desc->sbuf);
	kfree(desc->inbuf);
	kfree(desc->orq);
	kfree(desc->irq);
	kfree(desc->ubuf);
	free_urbs(desc);
	kfree(desc);
}

static ssize_t wdm_write
(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	u8 *buf;
	int rv = -EMSGSIZE, r, we;
	struct wdm_device *desc = file->private_data;
	struct usb_ctrlrequest *req;

	if (count > desc->wMaxCommand)
		count = desc->wMaxCommand;

	spin_lock_irq(&desc->iuspin);
	we = desc->werr;
	desc->werr = 0;
	spin_unlock_irq(&desc->iuspin);
	if (we < 0)
		return usb_translate_errors(we);

	buf = memdup_user(buffer, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	/* concurrent writes and disconnect */
	r = mutex_lock_interruptible(&desc->wlock);
	rv = -ERESTARTSYS;
	if (r)
		goto out_free_mem;

	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		rv = -ENODEV;
		goto out_free_mem_lock;
	}

	r = usb_autopm_get_interface(desc->intf);
	if (r < 0) {
		rv = usb_translate_errors(r);
		goto out_free_mem_lock;
	}

	if (!(file->f_flags & O_NONBLOCK))
		r = wait_event_interruptible(desc->wait, !test_bit(WDM_IN_USE,
								&desc->flags));
	else
		if (test_bit(WDM_IN_USE, &desc->flags))
			r = -EAGAIN;

	if (test_bit(WDM_RESETTING, &desc->flags))
		r = -EIO;

	if (r < 0) {
		rv = r;
		goto out_free_mem_pm;
	}

	req = desc->orq;
	usb_fill_control_urb(
		desc->command,
		interface_to_usbdev(desc->intf),
		/* using common endpoint 0 */
		usb_sndctrlpipe(interface_to_usbdev(desc->intf), 0),
		(unsigned char *)req,
		buf,
		count,
		wdm_out_callback,
		desc
	);

	req->bRequestType = (USB_DIR_OUT | USB_TYPE_CLASS |
			     USB_RECIP_INTERFACE);
	req->bRequest = USB_CDC_SEND_ENCAPSULATED_COMMAND;
	req->wValue = 0;
	req->wIndex = desc->inum; /* already converted */
	req->wLength = cpu_to_le16(count);
	set_bit(WDM_IN_USE, &desc->flags);
	desc->outbuf = buf;

	rv = usb_submit_urb(desc->command, GFP_KERNEL);
	if (rv < 0) {
		desc->outbuf = NULL;
		clear_bit(WDM_IN_USE, &desc->flags);
		dev_err(&desc->intf->dev, "Tx URB error: %d\n", rv);
		rv = usb_translate_errors(rv);
		goto out_free_mem_pm;
	} else {
		dev_dbg(&desc->intf->dev, "Tx URB has been submitted index=%d\n",
			le16_to_cpu(req->wIndex));
	}

	usb_autopm_put_interface(desc->intf);
	mutex_unlock(&desc->wlock);
	return count;

out_free_mem_pm:
	usb_autopm_put_interface(desc->intf);
out_free_mem_lock:
	mutex_unlock(&desc->wlock);
out_free_mem:
	kfree(buf);
	return rv;
}

/*
 * Submit the read urb if resp_count is non-zero.
 *
 * Called with desc->iuspin locked
 */
static int service_outstanding_interrupt(struct wdm_device *desc)
{
	int rv = 0;

	/* submit read urb only if the device is waiting for it */
	if (!desc->resp_count || !--desc->resp_count)
		goto out;

	set_bit(WDM_RESPONDING, &desc->flags);
	spin_unlock_irq(&desc->iuspin);
	rv = usb_submit_urb(desc->response, GFP_KERNEL);
	spin_lock_irq(&desc->iuspin);
	if (rv) {
		dev_err(&desc->intf->dev,
			"usb_submit_urb failed with result %d\n", rv);

		/* make sure the next notification trigger a submit */
		clear_bit(WDM_RESPONDING, &desc->flags);
		desc->resp_count = 0;
	}
out:
	return rv;
}

static ssize_t wdm_read
(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	int rv, cntr;
	int i = 0;
	struct wdm_device *desc = file->private_data;


	rv = mutex_lock_interruptible(&desc->rlock); /*concurrent reads */
	if (rv < 0)
		return -ERESTARTSYS;

	cntr = READ_ONCE(desc->length);
	if (cntr == 0) {
		desc->read = 0;
retry:
		if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
			rv = -ENODEV;
			goto err;
		}
		if (test_bit(WDM_OVERFLOW, &desc->flags)) {
			clear_bit(WDM_OVERFLOW, &desc->flags);
			rv = -ENOBUFS;
			goto err;
		}
		i++;
		if (file->f_flags & O_NONBLOCK) {
			if (!test_bit(WDM_READ, &desc->flags)) {
				rv = -EAGAIN;
				goto err;
			}
			rv = 0;
		} else {
			rv = wait_event_interruptible(desc->wait,
				test_bit(WDM_READ, &desc->flags));
		}

		/* may have happened while we slept */
		if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
			rv = -ENODEV;
			goto err;
		}
		if (test_bit(WDM_RESETTING, &desc->flags)) {
			rv = -EIO;
			goto err;
		}
		usb_mark_last_busy(interface_to_usbdev(desc->intf));
		if (rv < 0) {
			rv = -ERESTARTSYS;
			goto err;
		}

		spin_lock_irq(&desc->iuspin);

		if (desc->rerr) { /* read completed, error happened */
			rv = usb_translate_errors(desc->rerr);
			desc->rerr = 0;
			spin_unlock_irq(&desc->iuspin);
			goto err;
		}
		/*
		 * recheck whether we've lost the race
		 * against the completion handler
		 */
		if (!test_bit(WDM_READ, &desc->flags)) { /* lost race */
			spin_unlock_irq(&desc->iuspin);
			goto retry;
		}

		if (!desc->reslength) { /* zero length read */
			dev_dbg(&desc->intf->dev, "zero length - clearing WDM_READ\n");
			clear_bit(WDM_READ, &desc->flags);
			rv = service_outstanding_interrupt(desc);
			spin_unlock_irq(&desc->iuspin);
			if (rv < 0)
				goto err;
			goto retry;
		}
		cntr = desc->length;
		spin_unlock_irq(&desc->iuspin);
	}

	if (cntr > count)
		cntr = count;
	rv = copy_to_user(buffer, desc->ubuf, cntr);
	if (rv > 0) {
		rv = -EFAULT;
		goto err;
	}

	spin_lock_irq(&desc->iuspin);

	for (i = 0; i < desc->length - cntr; i++)
		desc->ubuf[i] = desc->ubuf[i + cntr];

	desc->length -= cntr;
	/* in case we had outstanding data */
	if (!desc->length) {
		clear_bit(WDM_READ, &desc->flags);
		service_outstanding_interrupt(desc);
	}
	spin_unlock_irq(&desc->iuspin);
	rv = cntr;

err:
	mutex_unlock(&desc->rlock);
	return rv;
}

static int wdm_flush(struct file *file, fl_owner_t id)
{
	struct wdm_device *desc = file->private_data;

	wait_event(desc->wait,
			/*
			 * needs both flags. We cannot do with one
			 * because resetting it would cause a race
			 * with write() yet we need to signal
			 * a disconnect
			 */
			!test_bit(WDM_IN_USE, &desc->flags) ||
			test_bit(WDM_DISCONNECTING, &desc->flags));

	/* cannot dereference desc->intf if WDM_DISCONNECTING */
	if (test_bit(WDM_DISCONNECTING, &desc->flags))
		return -ENODEV;
	if (desc->werr < 0)
		dev_err(&desc->intf->dev, "Error in flush path: %d\n",
			desc->werr);

	return usb_translate_errors(desc->werr);
}

static __poll_t wdm_poll(struct file *file, struct poll_table_struct *wait)
{
	struct wdm_device *desc = file->private_data;
	unsigned long flags;
	__poll_t mask = 0;

	spin_lock_irqsave(&desc->iuspin, flags);
	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		mask = EPOLLHUP | EPOLLERR;
		spin_unlock_irqrestore(&desc->iuspin, flags);
		goto desc_out;
	}
	if (test_bit(WDM_READ, &desc->flags))
		mask = EPOLLIN | EPOLLRDNORM;
	if (desc->rerr || desc->werr)
		mask |= EPOLLERR;
	if (!test_bit(WDM_IN_USE, &desc->flags))
		mask |= EPOLLOUT | EPOLLWRNORM;
	spin_unlock_irqrestore(&desc->iuspin, flags);

	poll_wait(file, &desc->wait, wait);

desc_out:
	return mask;
}

static int wdm_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	int rv = -ENODEV;
	struct usb_interface *intf;
	struct wdm_device *desc;

	mutex_lock(&wdm_mutex);
	desc = wdm_find_device_by_minor(minor);
	if (!desc)
		goto out;

	intf = desc->intf;
	if (test_bit(WDM_DISCONNECTING, &desc->flags))
		goto out;
	file->private_data = desc;

	rv = usb_autopm_get_interface(desc->intf);
	if (rv < 0) {
		dev_err(&desc->intf->dev, "Error autopm - %d\n", rv);
		goto out;
	}

	/* using write lock to protect desc->count */
	mutex_lock(&desc->wlock);
	if (!desc->count++) {
		desc->werr = 0;
		desc->rerr = 0;
		rv = usb_submit_urb(desc->validity, GFP_KERNEL);
		if (rv < 0) {
			desc->count--;
			dev_err(&desc->intf->dev,
				"Error submitting int urb - %d\n", rv);
			rv = usb_translate_errors(rv);
		}
	} else {
		rv = 0;
	}
	mutex_unlock(&desc->wlock);
	if (desc->count == 1)
		desc->manage_power(intf, 1);
	usb_autopm_put_interface(desc->intf);
out:
	mutex_unlock(&wdm_mutex);
	return rv;
}

static int wdm_release(struct inode *inode, struct file *file)
{
	struct wdm_device *desc = file->private_data;

	mutex_lock(&wdm_mutex);

	/* using write lock to protect desc->count */
	mutex_lock(&desc->wlock);
	desc->count--;
	mutex_unlock(&desc->wlock);

	if (!desc->count) {
		if (!test_bit(WDM_DISCONNECTING, &desc->flags)) {
			dev_dbg(&desc->intf->dev, "wdm_release: cleanup\n");
			kill_urbs(desc);
			spin_lock_irq(&desc->iuspin);
			desc->resp_count = 0;
			spin_unlock_irq(&desc->iuspin);
			desc->manage_power(desc->intf, 0);
		} else {
			/* must avoid dev_printk here as desc->intf is invalid */
			pr_debug(KBUILD_MODNAME " %s: device gone - cleaning up\n", __func__);
			cleanup(desc);
		}
	}
	mutex_unlock(&wdm_mutex);
	return 0;
}

static long wdm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct wdm_device *desc = file->private_data;
	int rv = 0;

	switch (cmd) {
	case IOCTL_WDM_MAX_COMMAND:
		if (copy_to_user((void __user *)arg, &desc->wMaxCommand, sizeof(desc->wMaxCommand)))
			rv = -EFAULT;
		break;
	default:
		rv = -ENOTTY;
	}
	return rv;
}

static const struct file_operations wdm_fops = {
	.owner =	THIS_MODULE,
	.read =		wdm_read,
	.write =	wdm_write,
	.open =		wdm_open,
	.flush =	wdm_flush,
	.release =	wdm_release,
	.poll =		wdm_poll,
	.unlocked_ioctl = wdm_ioctl,
	.compat_ioctl = wdm_ioctl,
	.llseek =	noop_llseek,
};

static struct usb_class_driver wdm_class = {
	.name =		"cdc-wdm%d",
	.fops =		&wdm_fops,
	.minor_base =	WDM_MINOR_BASE,
};

/* --- error handling --- */
static void wdm_rxwork(struct work_struct *work)
{
	struct wdm_device *desc = container_of(work, struct wdm_device, rxwork);
	unsigned long flags;
	int rv = 0;
	int responding;

	spin_lock_irqsave(&desc->iuspin, flags);
	if (test_bit(WDM_DISCONNECTING, &desc->flags)) {
		spin_unlock_irqrestore(&desc->iuspin, flags);
	} else {
		responding = test_and_set_bit(WDM_RESPONDING, &desc->flags);
		spin_unlock_irqrestore(&desc->iuspin, flags);
		if (!responding)
			rv = usb_submit_urb(desc->response, GFP_KERNEL);
		if (rv < 0 && rv != -EPERM) {
			spin_lock_irqsave(&desc->iuspin, flags);
			clear_bit(WDM_RESPONDING, &desc->flags);
			if (!test_bit(WDM_DISCONNECTING, &desc->flags))
				schedule_work(&desc->rxwork);
			spin_unlock_irqrestore(&desc->iuspin, flags);
		}
	}
}

static void service_interrupt_work(struct work_struct *work)
{
	struct wdm_device *desc;

	desc = container_of(work, struct wdm_device, service_outs_intr);

	spin_lock_irq(&desc->iuspin);
	service_outstanding_interrupt(desc);
	if (!desc->resp_count) {
		set_bit(WDM_READ, &desc->flags);
		wake_up(&desc->wait);
	}
	spin_unlock_irq(&desc->iuspin);
}

/* --- hotplug --- */

static int wdm_create(struct usb_interface *intf, struct usb_endpoint_descriptor *ep,
		u16 bufsize, int (*manage_power)(struct usb_interface *, int))
{
	int rv = -ENOMEM;
	struct wdm_device *desc;

	desc = kzalloc(sizeof(struct wdm_device), GFP_KERNEL);
	if (!desc)
		goto out;
	INIT_LIST_HEAD(&desc->device_list);
	mutex_init(&desc->rlock);
	mutex_init(&desc->wlock);
	spin_lock_init(&desc->iuspin);
	init_waitqueue_head(&desc->wait);
	desc->wMaxCommand = bufsize;
	/* this will be expanded and needed in hardware endianness */
	desc->inum = cpu_to_le16((u16)intf->cur_altsetting->desc.bInterfaceNumber);
	desc->intf = intf;
	INIT_WORK(&desc->rxwork, wdm_rxwork);
	INIT_WORK(&desc->service_outs_intr, service_interrupt_work);

	rv = -EINVAL;
	if (!usb_endpoint_is_int_in(ep))
		goto err;

	desc->wMaxPacketSize = usb_endpoint_maxp(ep);

	desc->orq = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!desc->orq)
		goto err;
	desc->irq = kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
	if (!desc->irq)
		goto err;

	desc->validity = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->validity)
		goto err;

	desc->response = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->response)
		goto err;

	desc->command = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->command)
		goto err;

	desc->ubuf = kmalloc(desc->wMaxCommand, GFP_KERNEL);
	if (!desc->ubuf)
		goto err;

	desc->sbuf = kmalloc(desc->wMaxPacketSize, GFP_KERNEL);
	if (!desc->sbuf)
		goto err;

	desc->inbuf = kmalloc(desc->wMaxCommand, GFP_KERNEL);
	if (!desc->inbuf)
		goto err;

	usb_fill_int_urb(
		desc->validity,
		interface_to_usbdev(intf),
		usb_rcvintpipe(interface_to_usbdev(intf), ep->bEndpointAddress),
		desc->sbuf,
		desc->wMaxPacketSize,
		wdm_int_callback,
		desc,
		ep->bInterval
	);

	desc->irq->bRequestType = (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE);
	desc->irq->bRequest = USB_CDC_GET_ENCAPSULATED_RESPONSE;
	desc->irq->wValue = 0;
	desc->irq->wIndex = desc->inum; /* already converted */
	desc->irq->wLength = cpu_to_le16(desc->wMaxCommand);

	usb_fill_control_urb(
		desc->response,
		interface_to_usbdev(intf),
		/* using common endpoint 0 */
		usb_rcvctrlpipe(interface_to_usbdev(desc->intf), 0),
		(unsigned char *)desc->irq,
		desc->inbuf,
		desc->wMaxCommand,
		wdm_in_callback,
		desc
	);

	desc->manage_power = manage_power;

	spin_lock(&wdm_device_list_lock);
	list_add(&desc->device_list, &wdm_device_list);
	spin_unlock(&wdm_device_list_lock);

	rv = usb_register_dev(intf, &wdm_class);
	if (rv < 0)
		goto err;
	else
		dev_info(&intf->dev, "%s: USB WDM device\n", dev_name(intf->usb_dev));
out:
	return rv;
err:
	spin_lock(&wdm_device_list_lock);
	list_del(&desc->device_list);
	spin_unlock(&wdm_device_list_lock);
	cleanup(desc);
	return rv;
}

static int wdm_manage_power(struct usb_interface *intf, int on)
{
	/* need autopm_get/put here to ensure the usbcore sees the new value */
	int rv = usb_autopm_get_interface(intf);

	intf->needs_remote_wakeup = on;
	if (!rv)
		usb_autopm_put_interface(intf);
	return 0;
}

static int wdm_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int rv = -EINVAL;
	struct usb_host_interface *iface;
	struct usb_endpoint_descriptor *ep;
	struct usb_cdc_parsed_header hdr;
	u8 *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	u16 maxcom = WDM_DEFAULT_BUFSIZE;

	if (!buffer)
		goto err;

	cdc_parse_cdc_header(&hdr, intf, buffer, buflen);

	if (hdr.usb_cdc_dmm_desc)
		maxcom = le16_to_cpu(hdr.usb_cdc_dmm_desc->wMaxCommand);

	iface = intf->cur_altsetting;
	if (iface->desc.bNumEndpoints != 1)
		goto err;
	ep = &iface->endpoint[0].desc;

	rv = wdm_create(intf, ep, maxcom, &wdm_manage_power);

err:
	return rv;
}

/**
 * usb_cdc_wdm_register - register a WDM subdriver
 * @intf: usb interface the subdriver will associate with
 * @ep: interrupt endpoint to monitor for notifications
 * @bufsize: maximum message size to support for read/write
 *
 * Create WDM usb class character device and associate it with intf
 * without binding, allowing another driver to manage the interface.
 *
 * The subdriver will manage the given interrupt endpoint exclusively
 * and will issue control requests referring to the given intf. It
 * will otherwise avoid interferring, and in particular not do
 * usb_set_intfdata/usb_get_intfdata on intf.
 *
 * The return value is a pointer to the subdriver's struct usb_driver.
 * The registering driver is responsible for calling this subdriver's
 * disconnect, suspend, resume, pre_reset and post_reset methods from
 * its own.
 */
struct usb_driver *usb_cdc_wdm_register(struct usb_interface *intf,
					struct usb_endpoint_descriptor *ep,
					int bufsize,
					int (*manage_power)(struct usb_interface *, int))
{
	int rv;

	rv = wdm_create(intf, ep, bufsize, manage_power);
	if (rv < 0)
		goto err;

	return &wdm_driver;
err:
	return ERR_PTR(rv);
}
EXPORT_SYMBOL(usb_cdc_wdm_register);

static void wdm_disconnect(struct usb_interface *intf)
{
	struct wdm_device *desc;
	unsigned long flags;

	usb_deregister_dev(intf, &wdm_class);
	desc = wdm_find_device(intf);
	mutex_lock(&wdm_mutex);

	/* the spinlock makes sure no new urbs are generated in the callbacks */
	spin_lock_irqsave(&desc->iuspin, flags);
	set_bit(WDM_DISCONNECTING, &desc->flags);
	set_bit(WDM_READ, &desc->flags);
	spin_unlock_irqrestore(&desc->iuspin, flags);
	wake_up_all(&desc->wait);
	mutex_lock(&desc->rlock);
	mutex_lock(&desc->wlock);
	kill_urbs(desc);
	cancel_work_sync(&desc->rxwork);
	cancel_work_sync(&desc->service_outs_intr);
	mutex_unlock(&desc->wlock);
	mutex_unlock(&desc->rlock);

	/* the desc->intf pointer used as list key is now invalid */
	spin_lock(&wdm_device_list_lock);
	list_del(&desc->device_list);
	spin_unlock(&wdm_device_list_lock);

	if (!desc->count)
		cleanup(desc);
	else
		dev_dbg(&intf->dev, "%d open files - postponing cleanup\n", desc->count);
	mutex_unlock(&wdm_mutex);
}

#ifdef CONFIG_PM
static int wdm_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct wdm_device *desc = wdm_find_device(intf);
	int rv = 0;

	dev_dbg(&desc->intf->dev, "wdm%d_suspend\n", intf->minor);

	/* if this is an autosuspend the caller does the locking */
	if (!PMSG_IS_AUTO(message)) {
		mutex_lock(&desc->rlock);
		mutex_lock(&desc->wlock);
	}
	spin_lock_irq(&desc->iuspin);

	if (PMSG_IS_AUTO(message) &&
			(test_bit(WDM_IN_USE, &desc->flags)
			|| test_bit(WDM_RESPONDING, &desc->flags))) {
		spin_unlock_irq(&desc->iuspin);
		rv = -EBUSY;
	} else {

		set_bit(WDM_SUSPENDING, &desc->flags);
		spin_unlock_irq(&desc->iuspin);
		/* callback submits work - order is essential */
		kill_urbs(desc);
		cancel_work_sync(&desc->rxwork);
		cancel_work_sync(&desc->service_outs_intr);
	}
	if (!PMSG_IS_AUTO(message)) {
		mutex_unlock(&desc->wlock);
		mutex_unlock(&desc->rlock);
	}

	return rv;
}
#endif

static int recover_from_urb_loss(struct wdm_device *desc)
{
	int rv = 0;

	if (desc->count) {
		rv = usb_submit_urb(desc->validity, GFP_NOIO);
		if (rv < 0)
			dev_err(&desc->intf->dev,
				"Error resume submitting int urb - %d\n", rv);
	}
	return rv;
}

#ifdef CONFIG_PM
static int wdm_resume(struct usb_interface *intf)
{
	struct wdm_device *desc = wdm_find_device(intf);
	int rv;

	dev_dbg(&desc->intf->dev, "wdm%d_resume\n", intf->minor);

	clear_bit(WDM_SUSPENDING, &desc->flags);
	rv = recover_from_urb_loss(desc);

	return rv;
}
#endif

static int wdm_pre_reset(struct usb_interface *intf)
{
	struct wdm_device *desc = wdm_find_device(intf);

	/*
	 * we notify everybody using poll of
	 * an exceptional situation
	 * must be done before recovery lest a spontaneous
	 * message from the device is lost
	 */
	spin_lock_irq(&desc->iuspin);
	set_bit(WDM_RESETTING, &desc->flags);	/* inform read/write */
	set_bit(WDM_READ, &desc->flags);	/* unblock read */
	clear_bit(WDM_IN_USE, &desc->flags);	/* unblock write */
	desc->rerr = -EINTR;
	spin_unlock_irq(&desc->iuspin);
	wake_up_all(&desc->wait);
	mutex_lock(&desc->rlock);
	mutex_lock(&desc->wlock);
	kill_urbs(desc);
	cancel_work_sync(&desc->rxwork);
	cancel_work_sync(&desc->service_outs_intr);
	return 0;
}

static int wdm_post_reset(struct usb_interface *intf)
{
	struct wdm_device *desc = wdm_find_device(intf);
	int rv;

	clear_bit(WDM_OVERFLOW, &desc->flags);
	clear_bit(WDM_RESETTING, &desc->flags);
	rv = recover_from_urb_loss(desc);
	mutex_unlock(&desc->wlock);
	mutex_unlock(&desc->rlock);
	return rv;
}

static struct usb_driver wdm_driver = {
	.name =		"cdc_wdm",
	.probe =	wdm_probe,
	.disconnect =	wdm_disconnect,
#ifdef CONFIG_PM
	.suspend =	wdm_suspend,
	.resume =	wdm_resume,
	.reset_resume =	wdm_resume,
#endif
	.pre_reset =	wdm_pre_reset,
	.post_reset =	wdm_post_reset,
	.id_table =	wdm_ids,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(wdm_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
