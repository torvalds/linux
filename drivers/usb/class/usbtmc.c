// SPDX-License-Identifier: GPL-2.0+
/*
 * drivers/usb/class/usbtmc.c - USB Test & Measurement class driver
 *
 * Copyright (C) 2007 Stefan Kopp, Gechingen, Germany
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 Greg Kroah-Hartman <gregkh@suse.de>
 * Copyright (C) 2018 IVI Foundation, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/compat.h>
#include <linux/usb/tmc.h>

/* Increment API VERSION when changing tmc.h with new flags or ioctls
 * or when changing a significant behavior of the driver.
 */
#define USBTMC_API_VERSION (3)

#define USBTMC_HEADER_SIZE	12
#define USBTMC_MINOR_BASE	176

/* Minimum USB timeout (in milliseconds) */
#define USBTMC_MIN_TIMEOUT	100
/* Default USB timeout (in milliseconds) */
#define USBTMC_TIMEOUT		5000

/* Max number of urbs used in write transfers */
#define MAX_URBS_IN_FLIGHT	16
/* I/O buffer size used in generic read/write functions */
#define USBTMC_BUFSIZE		(4096)

/*
 * Maximum number of read cycles to empty bulk in endpoint during CLEAR and
 * ABORT_BULK_IN requests. Ends the loop if (for whatever reason) a short
 * packet is never read.
 */
#define USBTMC_MAX_READS_TO_CLEAR_BULK_IN	100

static const struct usb_device_id usbtmc_devices[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_APP_SPEC, 3, 0), },
	{ USB_INTERFACE_INFO(USB_CLASS_APP_SPEC, 3, 1), },
	{ 0, } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, usbtmc_devices);

/*
 * This structure is the capabilities for the device
 * See section 4.2.1.8 of the USBTMC specification,
 * and section 4.2.2 of the USBTMC usb488 subclass
 * specification for details.
 */
struct usbtmc_dev_capabilities {
	__u8 interface_capabilities;
	__u8 device_capabilities;
	__u8 usb488_interface_capabilities;
	__u8 usb488_device_capabilities;
};

/* This structure holds private data for each USBTMC device. One copy is
 * allocated for each USBTMC device in the driver's probe function.
 */
struct usbtmc_device_data {
	const struct usb_device_id *id;
	struct usb_device *usb_dev;
	struct usb_interface *intf;
	struct list_head file_list;

	unsigned int bulk_in;
	unsigned int bulk_out;

	u8 bTag;
	u8 bTag_last_write;	/* needed for abort */
	u8 bTag_last_read;	/* needed for abort */

	/* packet size of IN bulk */
	u16            wMaxPacketSize;

	/* data for interrupt in endpoint handling */
	u8             bNotify1;
	u8             bNotify2;
	u16            ifnum;
	u8             iin_bTag;
	u8            *iin_buffer;
	atomic_t       iin_data_valid;
	unsigned int   iin_ep;
	int            iin_ep_present;
	int            iin_interval;
	struct urb    *iin_urb;
	u16            iin_wMaxPacketSize;

	/* coalesced usb488_caps from usbtmc_dev_capabilities */
	__u8 usb488_caps;

	bool zombie; /* fd of disconnected device */

	struct usbtmc_dev_capabilities	capabilities;
	struct kref kref;
	struct mutex io_mutex;	/* only one i/o function running at a time */
	wait_queue_head_t waitq;
	struct fasync_struct *fasync;
	spinlock_t dev_lock; /* lock for file_list */
};
#define to_usbtmc_data(d) container_of(d, struct usbtmc_device_data, kref)

/*
 * This structure holds private data for each USBTMC file handle.
 */
struct usbtmc_file_data {
	struct usbtmc_device_data *data;
	struct list_head file_elem;

	u32            timeout;
	u8             srq_byte;
	atomic_t       srq_asserted;
	atomic_t       closing;
	u8             bmTransferAttributes; /* member of DEV_DEP_MSG_IN */

	u8             eom_val;
	u8             term_char;
	bool           term_char_enabled;
	bool           auto_abort;

	spinlock_t     err_lock; /* lock for errors */

	struct usb_anchor submitted;

	/* data for generic_write */
	struct semaphore limit_write_sem;
	u32 out_transfer_size;
	int out_status;

	/* data for generic_read */
	u32 in_transfer_size;
	int in_status;
	int in_urbs_used;
	struct usb_anchor in_anchor;
	wait_queue_head_t wait_bulk_in;
};

/* Forward declarations */
static struct usb_driver usbtmc_driver;
static void usbtmc_draw_down(struct usbtmc_file_data *file_data);

static void usbtmc_delete(struct kref *kref)
{
	struct usbtmc_device_data *data = to_usbtmc_data(kref);

	usb_put_dev(data->usb_dev);
	kfree(data);
}

static int usbtmc_open(struct inode *inode, struct file *filp)
{
	struct usb_interface *intf;
	struct usbtmc_device_data *data;
	struct usbtmc_file_data *file_data;

	intf = usb_find_interface(&usbtmc_driver, iminor(inode));
	if (!intf) {
		pr_err("can not find device for minor %d", iminor(inode));
		return -ENODEV;
	}

	file_data = kzalloc(sizeof(*file_data), GFP_KERNEL);
	if (!file_data)
		return -ENOMEM;

	spin_lock_init(&file_data->err_lock);
	sema_init(&file_data->limit_write_sem, MAX_URBS_IN_FLIGHT);
	init_usb_anchor(&file_data->submitted);
	init_usb_anchor(&file_data->in_anchor);
	init_waitqueue_head(&file_data->wait_bulk_in);

	data = usb_get_intfdata(intf);
	/* Protect reference to data from file structure until release */
	kref_get(&data->kref);

	mutex_lock(&data->io_mutex);
	file_data->data = data;

	atomic_set(&file_data->closing, 0);

	file_data->timeout = USBTMC_TIMEOUT;
	file_data->term_char = '\n';
	file_data->term_char_enabled = 0;
	file_data->auto_abort = 0;
	file_data->eom_val = 1;

	INIT_LIST_HEAD(&file_data->file_elem);
	spin_lock_irq(&data->dev_lock);
	list_add_tail(&file_data->file_elem, &data->file_list);
	spin_unlock_irq(&data->dev_lock);
	mutex_unlock(&data->io_mutex);

	/* Store pointer in file structure's private data field */
	filp->private_data = file_data;

	return 0;
}

/*
 * usbtmc_flush - called before file handle is closed
 */
static int usbtmc_flush(struct file *file, fl_owner_t id)
{
	struct usbtmc_file_data *file_data;
	struct usbtmc_device_data *data;

	file_data = file->private_data;
	if (file_data == NULL)
		return -ENODEV;

	atomic_set(&file_data->closing, 1);
	data = file_data->data;

	/* wait for io to stop */
	mutex_lock(&data->io_mutex);

	usbtmc_draw_down(file_data);

	spin_lock_irq(&file_data->err_lock);
	file_data->in_status = 0;
	file_data->in_transfer_size = 0;
	file_data->in_urbs_used = 0;
	file_data->out_status = 0;
	file_data->out_transfer_size = 0;
	spin_unlock_irq(&file_data->err_lock);

	wake_up_interruptible_all(&data->waitq);
	mutex_unlock(&data->io_mutex);

	return 0;
}

static int usbtmc_release(struct inode *inode, struct file *file)
{
	struct usbtmc_file_data *file_data = file->private_data;

	/* prevent IO _AND_ usbtmc_interrupt */
	mutex_lock(&file_data->data->io_mutex);
	spin_lock_irq(&file_data->data->dev_lock);

	list_del(&file_data->file_elem);

	spin_unlock_irq(&file_data->data->dev_lock);
	mutex_unlock(&file_data->data->io_mutex);

	kref_put(&file_data->data->kref, usbtmc_delete);
	file_data->data = NULL;
	kfree(file_data);
	return 0;
}

static int usbtmc_ioctl_abort_bulk_in_tag(struct usbtmc_device_data *data,
					  u8 tag)
{
	u8 *buffer;
	struct device *dev;
	int rv;
	int n;
	int actual;

	dev = &data->intf->dev;
	buffer = kmalloc(USBTMC_BUFSIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_INITIATE_ABORT_BULK_IN,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     tag, data->bulk_in,
			     buffer, 2, USB_CTRL_GET_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "INITIATE_ABORT_BULK_IN returned %x with tag %02x\n",
		buffer[0], buffer[1]);

	if (buffer[0] == USBTMC_STATUS_FAILED) {
		/* No transfer in progress and the Bulk-OUT FIFO is empty. */
		rv = 0;
		goto exit;
	}

	if (buffer[0] == USBTMC_STATUS_TRANSFER_NOT_IN_PROGRESS) {
		/* The device returns this status if either:
		 * - There is a transfer in progress, but the specified bTag
		 *   does not match.
		 * - There is no transfer in progress, but the Bulk-OUT FIFO
		 *   is not empty.
		 */
		rv = -ENOMSG;
		goto exit;
	}

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "INITIATE_ABORT_BULK_IN returned %x\n",
			buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	n = 0;

usbtmc_abort_bulk_in_status:
	dev_dbg(dev, "Reading from bulk in EP\n");

	/* Data must be present. So use low timeout 300 ms */
	actual = 0;
	rv = usb_bulk_msg(data->usb_dev,
			  usb_rcvbulkpipe(data->usb_dev,
					  data->bulk_in),
			  buffer, USBTMC_BUFSIZE,
			  &actual, 300);

	print_hex_dump_debug("usbtmc ", DUMP_PREFIX_NONE, 16, 1,
			     buffer, actual, true);

	n++;

	if (rv < 0) {
		dev_err(dev, "usb_bulk_msg returned %d\n", rv);
		if (rv != -ETIMEDOUT)
			goto exit;
	}

	if (actual == USBTMC_BUFSIZE)
		goto usbtmc_abort_bulk_in_status;

	if (n >= USBTMC_MAX_READS_TO_CLEAR_BULK_IN) {
		dev_err(dev, "Couldn't clear device buffer within %d cycles\n",
			USBTMC_MAX_READS_TO_CLEAR_BULK_IN);
		rv = -EPERM;
		goto exit;
	}

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_CHECK_ABORT_BULK_IN_STATUS,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     0, data->bulk_in, buffer, 0x08,
			     USB_CTRL_GET_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "CHECK_ABORT_BULK_IN returned %x\n", buffer[0]);

	if (buffer[0] == USBTMC_STATUS_SUCCESS) {
		rv = 0;
		goto exit;
	}

	if (buffer[0] != USBTMC_STATUS_PENDING) {
		dev_err(dev, "CHECK_ABORT_BULK_IN returned %x\n", buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	if ((buffer[1] & 1) > 0) {
		/* The device has 1 or more queued packets the Host can read */
		goto usbtmc_abort_bulk_in_status;
	}

	/* The Host must send CHECK_ABORT_BULK_IN_STATUS at a later time. */
	rv = -EAGAIN;
exit:
	kfree(buffer);
	return rv;
}

static int usbtmc_ioctl_abort_bulk_in(struct usbtmc_device_data *data)
{
	return usbtmc_ioctl_abort_bulk_in_tag(data, data->bTag_last_read);
}

static int usbtmc_ioctl_abort_bulk_out_tag(struct usbtmc_device_data *data,
					   u8 tag)
{
	struct device *dev;
	u8 *buffer;
	int rv;
	int n;

	dev = &data->intf->dev;

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_INITIATE_ABORT_BULK_OUT,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     tag, data->bulk_out,
			     buffer, 2, USB_CTRL_GET_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "INITIATE_ABORT_BULK_OUT returned %x\n", buffer[0]);

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "INITIATE_ABORT_BULK_OUT returned %x\n",
			buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	n = 0;

usbtmc_abort_bulk_out_check_status:
	/* do not stress device with subsequent requests */
	msleep(50);
	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_CHECK_ABORT_BULK_OUT_STATUS,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     0, data->bulk_out, buffer, 0x08,
			     USB_CTRL_GET_TIMEOUT);
	n++;
	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "CHECK_ABORT_BULK_OUT returned %x\n", buffer[0]);

	if (buffer[0] == USBTMC_STATUS_SUCCESS)
		goto usbtmc_abort_bulk_out_clear_halt;

	if ((buffer[0] == USBTMC_STATUS_PENDING) &&
	    (n < USBTMC_MAX_READS_TO_CLEAR_BULK_IN))
		goto usbtmc_abort_bulk_out_check_status;

	rv = -EPERM;
	goto exit;

usbtmc_abort_bulk_out_clear_halt:
	rv = usb_clear_halt(data->usb_dev,
			    usb_sndbulkpipe(data->usb_dev, data->bulk_out));

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}
	rv = 0;

exit:
	kfree(buffer);
	return rv;
}

static int usbtmc_ioctl_abort_bulk_out(struct usbtmc_device_data *data)
{
	return usbtmc_ioctl_abort_bulk_out_tag(data, data->bTag_last_write);
}

static int usbtmc_get_stb(struct usbtmc_file_data *file_data, __u8 *stb)
{
	struct usbtmc_device_data *data = file_data->data;
	struct device *dev = &data->intf->dev;
	u8 *buffer;
	u8 tag;
	int rv;

	dev_dbg(dev, "Enter ioctl_read_stb iin_ep_present: %d\n",
		data->iin_ep_present);

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	atomic_set(&data->iin_data_valid, 0);

	rv = usb_control_msg(data->usb_dev,
			usb_rcvctrlpipe(data->usb_dev, 0),
			USBTMC488_REQUEST_READ_STATUS_BYTE,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			data->iin_bTag,
			data->ifnum,
			buffer, 0x03, USB_CTRL_GET_TIMEOUT);
	if (rv < 0) {
		dev_err(dev, "stb usb_control_msg returned %d\n", rv);
		goto exit;
	}

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "control status returned %x\n", buffer[0]);
		rv = -EIO;
		goto exit;
	}

	if (data->iin_ep_present) {
		rv = wait_event_interruptible_timeout(
			data->waitq,
			atomic_read(&data->iin_data_valid) != 0,
			file_data->timeout);
		if (rv < 0) {
			dev_dbg(dev, "wait interrupted %d\n", rv);
			goto exit;
		}

		if (rv == 0) {
			dev_dbg(dev, "wait timed out\n");
			rv = -ETIMEDOUT;
			goto exit;
		}

		tag = data->bNotify1 & 0x7f;
		if (tag != data->iin_bTag) {
			dev_err(dev, "expected bTag %x got %x\n",
				data->iin_bTag, tag);
		}

		*stb = data->bNotify2;
	} else {
		*stb = buffer[2];
	}

	dev_dbg(dev, "stb:0x%02x received %d\n", (unsigned int)*stb, rv);

 exit:
	/* bump interrupt bTag */
	data->iin_bTag += 1;
	if (data->iin_bTag > 127)
		/* 1 is for SRQ see USBTMC-USB488 subclass spec section 4.3.1 */
		data->iin_bTag = 2;

	kfree(buffer);
	return rv;
}

static int usbtmc488_ioctl_read_stb(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	int srq_asserted = 0;
	__u8 stb;
	int rv;

	rv = usbtmc_get_stb(file_data, &stb);

	if (rv > 0) {
		srq_asserted = atomic_xchg(&file_data->srq_asserted,
					srq_asserted);
		if (srq_asserted)
			stb |= 0x40; /* Set RQS bit */

		rv = put_user(stb, (__u8 __user *)arg);
	}
	return rv;

}

static int usbtmc_ioctl_get_srq_stb(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	struct usbtmc_device_data *data = file_data->data;
	struct device *dev = &data->intf->dev;
	int srq_asserted = 0;
	__u8 stb = 0;
	int rv;

	spin_lock_irq(&data->dev_lock);
	srq_asserted  = atomic_xchg(&file_data->srq_asserted, srq_asserted);

	if (srq_asserted) {
		stb = file_data->srq_byte;
		spin_unlock_irq(&data->dev_lock);
		rv = put_user(stb, (__u8 __user *)arg);
	} else {
		spin_unlock_irq(&data->dev_lock);
		rv = -ENOMSG;
	}

	dev_dbg(dev, "stb:0x%02x with srq received %d\n", (unsigned int)stb, rv);

	return rv;
}

static int usbtmc488_ioctl_wait_srq(struct usbtmc_file_data *file_data,
				    __u32 __user *arg)
{
	struct usbtmc_device_data *data = file_data->data;
	struct device *dev = &data->intf->dev;
	int rv;
	u32 timeout;
	unsigned long expire;

	if (!data->iin_ep_present) {
		dev_dbg(dev, "no interrupt endpoint present\n");
		return -EFAULT;
	}

	if (get_user(timeout, arg))
		return -EFAULT;

	expire = msecs_to_jiffies(timeout);

	mutex_unlock(&data->io_mutex);

	rv = wait_event_interruptible_timeout(
			data->waitq,
			atomic_read(&file_data->srq_asserted) != 0 ||
			atomic_read(&file_data->closing),
			expire);

	mutex_lock(&data->io_mutex);

	/* Note! disconnect or close could be called in the meantime */
	if (atomic_read(&file_data->closing) || data->zombie)
		rv = -ENODEV;

	if (rv < 0) {
		/* dev can be invalid now! */
		pr_debug("%s - wait interrupted %d\n", __func__, rv);
		return rv;
	}

	if (rv == 0) {
		dev_dbg(dev, "%s - wait timed out\n", __func__);
		return -ETIMEDOUT;
	}

	dev_dbg(dev, "%s - srq asserted\n", __func__);
	return 0;
}

static int usbtmc488_ioctl_simple(struct usbtmc_device_data *data,
				void __user *arg, unsigned int cmd)
{
	struct device *dev = &data->intf->dev;
	__u8 val;
	u8 *buffer;
	u16 wValue;
	int rv;

	if (!(data->usb488_caps & USBTMC488_CAPABILITY_SIMPLE))
		return -EINVAL;

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	if (cmd == USBTMC488_REQUEST_REN_CONTROL) {
		rv = copy_from_user(&val, arg, sizeof(val));
		if (rv) {
			rv = -EFAULT;
			goto exit;
		}
		wValue = val ? 1 : 0;
	} else {
		wValue = 0;
	}

	rv = usb_control_msg(data->usb_dev,
			usb_rcvctrlpipe(data->usb_dev, 0),
			cmd,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			wValue,
			data->ifnum,
			buffer, 0x01, USB_CTRL_GET_TIMEOUT);
	if (rv < 0) {
		dev_err(dev, "simple usb_control_msg failed %d\n", rv);
		goto exit;
	} else if (rv != 1) {
		dev_warn(dev, "simple usb_control_msg returned %d\n", rv);
		rv = -EIO;
		goto exit;
	}

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "simple control status returned %x\n", buffer[0]);
		rv = -EIO;
		goto exit;
	}
	rv = 0;

 exit:
	kfree(buffer);
	return rv;
}

/*
 * Sends a TRIGGER Bulk-OUT command message
 * See the USBTMC-USB488 specification, Table 2.
 *
 * Also updates bTag_last_write.
 */
static int usbtmc488_ioctl_trigger(struct usbtmc_file_data *file_data)
{
	struct usbtmc_device_data *data = file_data->data;
	int retval;
	u8 *buffer;
	int actual;

	buffer = kzalloc(USBTMC_HEADER_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buffer[0] = 128;
	buffer[1] = data->bTag;
	buffer[2] = ~data->bTag;

	retval = usb_bulk_msg(data->usb_dev,
			      usb_sndbulkpipe(data->usb_dev,
					      data->bulk_out),
			      buffer, USBTMC_HEADER_SIZE,
			      &actual, file_data->timeout);

	/* Store bTag (in case we need to abort) */
	data->bTag_last_write = data->bTag;

	/* Increment bTag -- and increment again if zero */
	data->bTag++;
	if (!data->bTag)
		data->bTag++;

	kfree(buffer);
	if (retval < 0) {
		dev_err(&data->intf->dev, "%s returned %d\n",
			__func__, retval);
		return retval;
	}

	return 0;
}

static struct urb *usbtmc_create_urb(void)
{
	const size_t bufsize = USBTMC_BUFSIZE;
	u8 *dmabuf = NULL;
	struct urb *urb = usb_alloc_urb(0, GFP_KERNEL);

	if (!urb)
		return NULL;

	dmabuf = kzalloc(bufsize, GFP_KERNEL);
	if (!dmabuf) {
		usb_free_urb(urb);
		return NULL;
	}

	urb->transfer_buffer = dmabuf;
	urb->transfer_buffer_length = bufsize;
	urb->transfer_flags |= URB_FREE_BUFFER;
	return urb;
}

static void usbtmc_read_bulk_cb(struct urb *urb)
{
	struct usbtmc_file_data *file_data = urb->context;
	int status = urb->status;
	unsigned long flags;

	/* sync/async unlink faults aren't errors */
	if (status) {
		if (!(/* status == -ENOENT || */
			status == -ECONNRESET ||
			status == -EREMOTEIO || /* Short packet */
			status == -ESHUTDOWN))
			dev_err(&file_data->data->intf->dev,
			"%s - nonzero read bulk status received: %d\n",
			__func__, status);

		spin_lock_irqsave(&file_data->err_lock, flags);
		if (!file_data->in_status)
			file_data->in_status = status;
		spin_unlock_irqrestore(&file_data->err_lock, flags);
	}

	spin_lock_irqsave(&file_data->err_lock, flags);
	file_data->in_transfer_size += urb->actual_length;
	dev_dbg(&file_data->data->intf->dev,
		"%s - total size: %u current: %d status: %d\n",
		__func__, file_data->in_transfer_size,
		urb->actual_length, status);
	spin_unlock_irqrestore(&file_data->err_lock, flags);
	usb_anchor_urb(urb, &file_data->in_anchor);

	wake_up_interruptible(&file_data->wait_bulk_in);
	wake_up_interruptible(&file_data->data->waitq);
}

static inline bool usbtmc_do_transfer(struct usbtmc_file_data *file_data)
{
	bool data_or_error;

	spin_lock_irq(&file_data->err_lock);
	data_or_error = !usb_anchor_empty(&file_data->in_anchor)
			|| file_data->in_status;
	spin_unlock_irq(&file_data->err_lock);
	dev_dbg(&file_data->data->intf->dev, "%s: returns %d\n", __func__,
		data_or_error);
	return data_or_error;
}

static ssize_t usbtmc_generic_read(struct usbtmc_file_data *file_data,
				   void __user *user_buffer,
				   u32 transfer_size,
				   u32 *transferred,
				   u32 flags)
{
	struct usbtmc_device_data *data = file_data->data;
	struct device *dev = &data->intf->dev;
	u32 done = 0;
	u32 remaining;
	const u32 bufsize = USBTMC_BUFSIZE;
	int retval = 0;
	u32 max_transfer_size;
	unsigned long expire;
	int bufcount = 1;
	int again = 0;

	/* mutex already locked */

	*transferred = done;

	max_transfer_size = transfer_size;

	if (flags & USBTMC_FLAG_IGNORE_TRAILER) {
		/* The device may send extra alignment bytes (up to
		 * wMaxPacketSize – 1) to avoid sending a zero-length
		 * packet
		 */
		remaining = transfer_size;
		if ((max_transfer_size % data->wMaxPacketSize) == 0)
			max_transfer_size += (data->wMaxPacketSize - 1);
	} else {
		/* round down to bufsize to avoid truncated data left */
		if (max_transfer_size > bufsize) {
			max_transfer_size =
				roundup(max_transfer_size + 1 - bufsize,
					bufsize);
		}
		remaining = max_transfer_size;
	}

	spin_lock_irq(&file_data->err_lock);

	if (file_data->in_status) {
		/* return the very first error */
		retval = file_data->in_status;
		spin_unlock_irq(&file_data->err_lock);
		goto error;
	}

	if (flags & USBTMC_FLAG_ASYNC) {
		if (usb_anchor_empty(&file_data->in_anchor))
			again = 1;

		if (file_data->in_urbs_used == 0) {
			file_data->in_transfer_size = 0;
			file_data->in_status = 0;
		}
	} else {
		file_data->in_transfer_size = 0;
		file_data->in_status = 0;
	}

	if (max_transfer_size == 0) {
		bufcount = 0;
	} else {
		bufcount = roundup(max_transfer_size, bufsize) / bufsize;
		if (bufcount > file_data->in_urbs_used)
			bufcount -= file_data->in_urbs_used;
		else
			bufcount = 0;

		if (bufcount + file_data->in_urbs_used > MAX_URBS_IN_FLIGHT) {
			bufcount = MAX_URBS_IN_FLIGHT -
					file_data->in_urbs_used;
		}
	}
	spin_unlock_irq(&file_data->err_lock);

	dev_dbg(dev, "%s: requested=%u flags=0x%X size=%u bufs=%d used=%d\n",
		__func__, transfer_size, flags,
		max_transfer_size, bufcount, file_data->in_urbs_used);

	while (bufcount > 0) {
		u8 *dmabuf = NULL;
		struct urb *urb = usbtmc_create_urb();

		if (!urb) {
			retval = -ENOMEM;
			goto error;
		}

		dmabuf = urb->transfer_buffer;

		usb_fill_bulk_urb(urb, data->usb_dev,
			usb_rcvbulkpipe(data->usb_dev, data->bulk_in),
			dmabuf, bufsize,
			usbtmc_read_bulk_cb, file_data);

		usb_anchor_urb(urb, &file_data->submitted);
		retval = usb_submit_urb(urb, GFP_KERNEL);
		/* urb is anchored. We can release our reference. */
		usb_free_urb(urb);
		if (unlikely(retval)) {
			usb_unanchor_urb(urb);
			goto error;
		}
		file_data->in_urbs_used++;
		bufcount--;
	}

	if (again) {
		dev_dbg(dev, "%s: ret=again\n", __func__);
		return -EAGAIN;
	}

	if (user_buffer == NULL)
		return -EINVAL;

	expire = msecs_to_jiffies(file_data->timeout);

	while (max_transfer_size > 0) {
		u32 this_part;
		struct urb *urb = NULL;

		if (!(flags & USBTMC_FLAG_ASYNC)) {
			dev_dbg(dev, "%s: before wait time %lu\n",
				__func__, expire);
			retval = wait_event_interruptible_timeout(
				file_data->wait_bulk_in,
				usbtmc_do_transfer(file_data),
				expire);

			dev_dbg(dev, "%s: wait returned %d\n",
				__func__, retval);

			if (retval <= 0) {
				if (retval == 0)
					retval = -ETIMEDOUT;
				goto error;
			}
		}

		urb = usb_get_from_anchor(&file_data->in_anchor);
		if (!urb) {
			if (!(flags & USBTMC_FLAG_ASYNC)) {
				/* synchronous case: must not happen */
				retval = -EFAULT;
				goto error;
			}

			/* asynchronous case: ready, do not block or wait */
			*transferred = done;
			dev_dbg(dev, "%s: (async) done=%u ret=0\n",
				__func__, done);
			return 0;
		}

		file_data->in_urbs_used--;

		if (max_transfer_size > urb->actual_length)
			max_transfer_size -= urb->actual_length;
		else
			max_transfer_size = 0;

		if (remaining > urb->actual_length)
			this_part = urb->actual_length;
		else
			this_part = remaining;

		print_hex_dump_debug("usbtmc ", DUMP_PREFIX_NONE, 16, 1,
			urb->transfer_buffer, urb->actual_length, true);

		if (copy_to_user(user_buffer + done,
				 urb->transfer_buffer, this_part)) {
			usb_free_urb(urb);
			retval = -EFAULT;
			goto error;
		}

		remaining -= this_part;
		done += this_part;

		spin_lock_irq(&file_data->err_lock);
		if (urb->status) {
			/* return the very first error */
			retval = file_data->in_status;
			spin_unlock_irq(&file_data->err_lock);
			usb_free_urb(urb);
			goto error;
		}
		spin_unlock_irq(&file_data->err_lock);

		if (urb->actual_length < bufsize) {
			/* short packet or ZLP received => ready */
			usb_free_urb(urb);
			retval = 1;
			break;
		}

		if (!(flags & USBTMC_FLAG_ASYNC) &&
		    max_transfer_size > (bufsize * file_data->in_urbs_used)) {
			/* resubmit, since other buffers still not enough */
			usb_anchor_urb(urb, &file_data->submitted);
			retval = usb_submit_urb(urb, GFP_KERNEL);
			if (unlikely(retval)) {
				usb_unanchor_urb(urb);
				usb_free_urb(urb);
				goto error;
			}
			file_data->in_urbs_used++;
		}
		usb_free_urb(urb);
		retval = 0;
	}

error:
	*transferred = done;

	dev_dbg(dev, "%s: before kill\n", __func__);
	/* Attention: killing urbs can take long time (2 ms) */
	usb_kill_anchored_urbs(&file_data->submitted);
	dev_dbg(dev, "%s: after kill\n", __func__);
	usb_scuttle_anchored_urbs(&file_data->in_anchor);
	file_data->in_urbs_used = 0;
	file_data->in_status = 0; /* no spinlock needed here */
	dev_dbg(dev, "%s: done=%u ret=%d\n", __func__, done, retval);

	return retval;
}

static ssize_t usbtmc_ioctl_generic_read(struct usbtmc_file_data *file_data,
					 void __user *arg)
{
	struct usbtmc_message msg;
	ssize_t retval = 0;

	/* mutex already locked */

	if (copy_from_user(&msg, arg, sizeof(struct usbtmc_message)))
		return -EFAULT;

	retval = usbtmc_generic_read(file_data, msg.message,
				     msg.transfer_size, &msg.transferred,
				     msg.flags);

	if (put_user(msg.transferred,
		     &((struct usbtmc_message __user *)arg)->transferred))
		return -EFAULT;

	return retval;
}

static void usbtmc_write_bulk_cb(struct urb *urb)
{
	struct usbtmc_file_data *file_data = urb->context;
	int wakeup = 0;
	unsigned long flags;

	spin_lock_irqsave(&file_data->err_lock, flags);
	file_data->out_transfer_size += urb->actual_length;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
			urb->status == -ECONNRESET ||
			urb->status == -ESHUTDOWN))
			dev_err(&file_data->data->intf->dev,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		if (!file_data->out_status) {
			file_data->out_status = urb->status;
			wakeup = 1;
		}
	}
	spin_unlock_irqrestore(&file_data->err_lock, flags);

	dev_dbg(&file_data->data->intf->dev,
		"%s - write bulk total size: %u\n",
		__func__, file_data->out_transfer_size);

	up(&file_data->limit_write_sem);
	if (usb_anchor_empty(&file_data->submitted) || wakeup)
		wake_up_interruptible(&file_data->data->waitq);
}

static ssize_t usbtmc_generic_write(struct usbtmc_file_data *file_data,
				    const void __user *user_buffer,
				    u32 transfer_size,
				    u32 *transferred,
				    u32 flags)
{
	struct usbtmc_device_data *data = file_data->data;
	struct device *dev;
	u32 done = 0;
	u32 remaining;
	unsigned long expire;
	const u32 bufsize = USBTMC_BUFSIZE;
	struct urb *urb = NULL;
	int retval = 0;
	u32 timeout;

	*transferred = 0;

	/* Get pointer to private data structure */
	dev = &data->intf->dev;

	dev_dbg(dev, "%s: size=%u flags=0x%X sema=%u\n",
		__func__, transfer_size, flags,
		file_data->limit_write_sem.count);

	if (flags & USBTMC_FLAG_APPEND) {
		spin_lock_irq(&file_data->err_lock);
		retval = file_data->out_status;
		spin_unlock_irq(&file_data->err_lock);
		if (retval < 0)
			return retval;
	} else {
		spin_lock_irq(&file_data->err_lock);
		file_data->out_transfer_size = 0;
		file_data->out_status = 0;
		spin_unlock_irq(&file_data->err_lock);
	}

	remaining = transfer_size;
	if (remaining > INT_MAX)
		remaining = INT_MAX;

	timeout = file_data->timeout;
	expire = msecs_to_jiffies(timeout);

	while (remaining > 0) {
		u32 this_part, aligned;
		u8 *buffer = NULL;

		if (flags & USBTMC_FLAG_ASYNC) {
			if (down_trylock(&file_data->limit_write_sem)) {
				retval = (done)?(0):(-EAGAIN);
				goto exit;
			}
		} else {
			retval = down_timeout(&file_data->limit_write_sem,
					      expire);
			if (retval < 0) {
				retval = -ETIMEDOUT;
				goto error;
			}
		}

		spin_lock_irq(&file_data->err_lock);
		retval = file_data->out_status;
		spin_unlock_irq(&file_data->err_lock);
		if (retval < 0) {
			up(&file_data->limit_write_sem);
			goto error;
		}

		/* prepare next urb to send */
		urb = usbtmc_create_urb();
		if (!urb) {
			retval = -ENOMEM;
			up(&file_data->limit_write_sem);
			goto error;
		}
		buffer = urb->transfer_buffer;

		if (remaining > bufsize)
			this_part = bufsize;
		else
			this_part = remaining;

		if (copy_from_user(buffer, user_buffer + done, this_part)) {
			retval = -EFAULT;
			up(&file_data->limit_write_sem);
			goto error;
		}

		print_hex_dump_debug("usbtmc ", DUMP_PREFIX_NONE,
			16, 1, buffer, this_part, true);

		/* fill bulk with 32 bit alignment to meet USBTMC specification
		 * (size + 3 & ~3) rounds up and simplifies user code
		 */
		aligned = (this_part + 3) & ~3;
		dev_dbg(dev, "write(size:%u align:%u done:%u)\n",
			(unsigned int)this_part,
			(unsigned int)aligned,
			(unsigned int)done);

		usb_fill_bulk_urb(urb, data->usb_dev,
			usb_sndbulkpipe(data->usb_dev, data->bulk_out),
			urb->transfer_buffer, aligned,
			usbtmc_write_bulk_cb, file_data);

		usb_anchor_urb(urb, &file_data->submitted);
		retval = usb_submit_urb(urb, GFP_KERNEL);
		if (unlikely(retval)) {
			usb_unanchor_urb(urb);
			up(&file_data->limit_write_sem);
			goto error;
		}

		usb_free_urb(urb);
		urb = NULL; /* urb will be finally released by usb driver */

		remaining -= this_part;
		done += this_part;
	}

	/* All urbs are on the fly */
	if (!(flags & USBTMC_FLAG_ASYNC)) {
		if (!usb_wait_anchor_empty_timeout(&file_data->submitted,
						   timeout)) {
			retval = -ETIMEDOUT;
			goto error;
		}
	}

	retval = 0;
	goto exit;

error:
	usb_kill_anchored_urbs(&file_data->submitted);
exit:
	usb_free_urb(urb);

	spin_lock_irq(&file_data->err_lock);
	if (!(flags & USBTMC_FLAG_ASYNC))
		done = file_data->out_transfer_size;
	if (!retval && file_data->out_status)
		retval = file_data->out_status;
	spin_unlock_irq(&file_data->err_lock);

	*transferred = done;

	dev_dbg(dev, "%s: done=%u, retval=%d, urbstat=%d\n",
		__func__, done, retval, file_data->out_status);

	return retval;
}

static ssize_t usbtmc_ioctl_generic_write(struct usbtmc_file_data *file_data,
					  void __user *arg)
{
	struct usbtmc_message msg;
	ssize_t retval = 0;

	/* mutex already locked */

	if (copy_from_user(&msg, arg, sizeof(struct usbtmc_message)))
		return -EFAULT;

	retval = usbtmc_generic_write(file_data, msg.message,
				      msg.transfer_size, &msg.transferred,
				      msg.flags);

	if (put_user(msg.transferred,
		     &((struct usbtmc_message __user *)arg)->transferred))
		return -EFAULT;

	return retval;
}

/*
 * Get the generic write result
 */
static ssize_t usbtmc_ioctl_write_result(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	u32 transferred;
	int retval;

	spin_lock_irq(&file_data->err_lock);
	transferred = file_data->out_transfer_size;
	retval = file_data->out_status;
	spin_unlock_irq(&file_data->err_lock);

	if (put_user(transferred, (__u32 __user *)arg))
		return -EFAULT;

	return retval;
}

/*
 * Sends a REQUEST_DEV_DEP_MSG_IN message on the Bulk-OUT endpoint.
 * @transfer_size: number of bytes to request from the device.
 *
 * See the USBTMC specification, Table 4.
 *
 * Also updates bTag_last_write.
 */
static int send_request_dev_dep_msg_in(struct usbtmc_file_data *file_data,
				       u32 transfer_size)
{
	struct usbtmc_device_data *data = file_data->data;
	int retval;
	u8 *buffer;
	int actual;

	buffer = kmalloc(USBTMC_HEADER_SIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	/* Setup IO buffer for REQUEST_DEV_DEP_MSG_IN message
	 * Refer to class specs for details
	 */
	buffer[0] = 2;
	buffer[1] = data->bTag;
	buffer[2] = ~data->bTag;
	buffer[3] = 0; /* Reserved */
	buffer[4] = transfer_size >> 0;
	buffer[5] = transfer_size >> 8;
	buffer[6] = transfer_size >> 16;
	buffer[7] = transfer_size >> 24;
	buffer[8] = file_data->term_char_enabled * 2;
	/* Use term character? */
	buffer[9] = file_data->term_char;
	buffer[10] = 0; /* Reserved */
	buffer[11] = 0; /* Reserved */

	/* Send bulk URB */
	retval = usb_bulk_msg(data->usb_dev,
			      usb_sndbulkpipe(data->usb_dev,
					      data->bulk_out),
			      buffer, USBTMC_HEADER_SIZE,
			      &actual, file_data->timeout);

	/* Store bTag (in case we need to abort) */
	data->bTag_last_write = data->bTag;

	/* Increment bTag -- and increment again if zero */
	data->bTag++;
	if (!data->bTag)
		data->bTag++;

	kfree(buffer);
	if (retval < 0)
		dev_err(&data->intf->dev, "%s returned %d\n",
			__func__, retval);

	return retval;
}

static ssize_t usbtmc_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	struct usbtmc_file_data *file_data;
	struct usbtmc_device_data *data;
	struct device *dev;
	const u32 bufsize = USBTMC_BUFSIZE;
	u32 n_characters;
	u8 *buffer;
	int actual;
	u32 done = 0;
	u32 remaining;
	int retval;

	/* Get pointer to private data structure */
	file_data = filp->private_data;
	data = file_data->data;
	dev = &data->intf->dev;

	buffer = kmalloc(bufsize, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	mutex_lock(&data->io_mutex);
	if (data->zombie) {
		retval = -ENODEV;
		goto exit;
	}

	if (count > INT_MAX)
		count = INT_MAX;

	dev_dbg(dev, "%s(count:%zu)\n", __func__, count);

	retval = send_request_dev_dep_msg_in(file_data, count);

	if (retval < 0) {
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_out(data);
		goto exit;
	}

	/* Loop until we have fetched everything we requested */
	remaining = count;
	actual = 0;

	/* Send bulk URB */
	retval = usb_bulk_msg(data->usb_dev,
			      usb_rcvbulkpipe(data->usb_dev,
					      data->bulk_in),
			      buffer, bufsize, &actual,
			      file_data->timeout);

	dev_dbg(dev, "%s: bulk_msg retval(%u), actual(%d)\n",
		__func__, retval, actual);

	/* Store bTag (in case we need to abort) */
	data->bTag_last_read = data->bTag;

	if (retval < 0) {
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_in(data);
		goto exit;
	}

	/* Sanity checks for the header */
	if (actual < USBTMC_HEADER_SIZE) {
		dev_err(dev, "Device sent too small first packet: %u < %u\n",
			actual, USBTMC_HEADER_SIZE);
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_in(data);
		goto exit;
	}

	if (buffer[0] != 2) {
		dev_err(dev, "Device sent reply with wrong MsgID: %u != 2\n",
			buffer[0]);
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_in(data);
		goto exit;
	}

	if (buffer[1] != data->bTag_last_write) {
		dev_err(dev, "Device sent reply with wrong bTag: %u != %u\n",
		buffer[1], data->bTag_last_write);
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_in(data);
		goto exit;
	}

	/* How many characters did the instrument send? */
	n_characters = buffer[4] +
		       (buffer[5] << 8) +
		       (buffer[6] << 16) +
		       (buffer[7] << 24);

	file_data->bmTransferAttributes = buffer[8];

	dev_dbg(dev, "Bulk-IN header: N_characters(%u), bTransAttr(%u)\n",
		n_characters, buffer[8]);

	if (n_characters > remaining) {
		dev_err(dev, "Device wants to return more data than requested: %u > %zu\n",
			n_characters, count);
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_in(data);
		goto exit;
	}

	print_hex_dump_debug("usbtmc ", DUMP_PREFIX_NONE,
			     16, 1, buffer, actual, true);

	remaining = n_characters;

	/* Remove the USBTMC header */
	actual -= USBTMC_HEADER_SIZE;

	/* Remove padding if it exists */
	if (actual > remaining)
		actual = remaining;

	remaining -= actual;

	/* Copy buffer to user space */
	if (copy_to_user(buf, &buffer[USBTMC_HEADER_SIZE], actual)) {
		/* There must have been an addressing problem */
		retval = -EFAULT;
		goto exit;
	}

	if ((actual + USBTMC_HEADER_SIZE) == bufsize) {
		retval = usbtmc_generic_read(file_data, buf + actual,
					     remaining,
					     &done,
					     USBTMC_FLAG_IGNORE_TRAILER);
		if (retval < 0)
			goto exit;
	}
	done += actual;

	/* Update file position value */
	*f_pos = *f_pos + done;
	retval = done;

exit:
	mutex_unlock(&data->io_mutex);
	kfree(buffer);
	return retval;
}

static ssize_t usbtmc_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	struct usbtmc_file_data *file_data;
	struct usbtmc_device_data *data;
	struct urb *urb = NULL;
	ssize_t retval = 0;
	u8 *buffer;
	u32 remaining, done;
	u32 transfersize, aligned, buflen;

	file_data = filp->private_data;
	data = file_data->data;

	mutex_lock(&data->io_mutex);

	if (data->zombie) {
		retval = -ENODEV;
		goto exit;
	}

	done = 0;

	spin_lock_irq(&file_data->err_lock);
	file_data->out_transfer_size = 0;
	file_data->out_status = 0;
	spin_unlock_irq(&file_data->err_lock);

	if (!count)
		goto exit;

	if (down_trylock(&file_data->limit_write_sem)) {
		/* previous calls were async */
		retval = -EBUSY;
		goto exit;
	}

	urb = usbtmc_create_urb();
	if (!urb) {
		retval = -ENOMEM;
		up(&file_data->limit_write_sem);
		goto exit;
	}

	buffer = urb->transfer_buffer;
	buflen = urb->transfer_buffer_length;

	if (count > INT_MAX) {
		transfersize = INT_MAX;
		buffer[8] = 0;
	} else {
		transfersize = count;
		buffer[8] = file_data->eom_val;
	}

	/* Setup IO buffer for DEV_DEP_MSG_OUT message */
	buffer[0] = 1;
	buffer[1] = data->bTag;
	buffer[2] = ~data->bTag;
	buffer[3] = 0; /* Reserved */
	buffer[4] = transfersize >> 0;
	buffer[5] = transfersize >> 8;
	buffer[6] = transfersize >> 16;
	buffer[7] = transfersize >> 24;
	/* buffer[8] is set above... */
	buffer[9] = 0; /* Reserved */
	buffer[10] = 0; /* Reserved */
	buffer[11] = 0; /* Reserved */

	remaining = transfersize;

	if (transfersize + USBTMC_HEADER_SIZE > buflen) {
		transfersize = buflen - USBTMC_HEADER_SIZE;
		aligned = buflen;
	} else {
		aligned = (transfersize + (USBTMC_HEADER_SIZE + 3)) & ~3;
	}

	if (copy_from_user(&buffer[USBTMC_HEADER_SIZE], buf, transfersize)) {
		retval = -EFAULT;
		up(&file_data->limit_write_sem);
		goto exit;
	}

	dev_dbg(&data->intf->dev, "%s(size:%u align:%u)\n", __func__,
		(unsigned int)transfersize, (unsigned int)aligned);

	print_hex_dump_debug("usbtmc ", DUMP_PREFIX_NONE,
			     16, 1, buffer, aligned, true);

	usb_fill_bulk_urb(urb, data->usb_dev,
		usb_sndbulkpipe(data->usb_dev, data->bulk_out),
		urb->transfer_buffer, aligned,
		usbtmc_write_bulk_cb, file_data);

	usb_anchor_urb(urb, &file_data->submitted);
	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (unlikely(retval)) {
		usb_unanchor_urb(urb);
		up(&file_data->limit_write_sem);
		goto exit;
	}

	remaining -= transfersize;

	data->bTag_last_write = data->bTag;
	data->bTag++;

	if (!data->bTag)
		data->bTag++;

	/* call generic_write even when remaining = 0 */
	retval = usbtmc_generic_write(file_data, buf + transfersize, remaining,
				      &done, USBTMC_FLAG_APPEND);
	/* truncate alignment bytes */
	if (done > remaining)
		done = remaining;

	/*add size of first urb*/
	done += transfersize;

	if (retval < 0) {
		usb_kill_anchored_urbs(&file_data->submitted);

		dev_err(&data->intf->dev,
			"Unable to send data, error %d\n", (int)retval);
		if (file_data->auto_abort)
			usbtmc_ioctl_abort_bulk_out(data);
		goto exit;
	}

	retval = done;
exit:
	usb_free_urb(urb);
	mutex_unlock(&data->io_mutex);
	return retval;
}

static int usbtmc_ioctl_clear(struct usbtmc_device_data *data)
{
	struct device *dev;
	u8 *buffer;
	int rv;
	int n;
	int actual = 0;

	dev = &data->intf->dev;

	dev_dbg(dev, "Sending INITIATE_CLEAR request\n");

	buffer = kmalloc(USBTMC_BUFSIZE, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_INITIATE_CLEAR,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     0, 0, buffer, 1, USB_CTRL_GET_TIMEOUT);
	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "INITIATE_CLEAR returned %x\n", buffer[0]);

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "INITIATE_CLEAR returned %x\n", buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	n = 0;

usbtmc_clear_check_status:

	dev_dbg(dev, "Sending CHECK_CLEAR_STATUS request\n");

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_CHECK_CLEAR_STATUS,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     0, 0, buffer, 2, USB_CTRL_GET_TIMEOUT);
	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "CHECK_CLEAR_STATUS returned %x\n", buffer[0]);

	if (buffer[0] == USBTMC_STATUS_SUCCESS)
		goto usbtmc_clear_bulk_out_halt;

	if (buffer[0] != USBTMC_STATUS_PENDING) {
		dev_err(dev, "CHECK_CLEAR_STATUS returned %x\n", buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	if ((buffer[1] & 1) != 0) {
		do {
			dev_dbg(dev, "Reading from bulk in EP\n");

			actual = 0;
			rv = usb_bulk_msg(data->usb_dev,
					  usb_rcvbulkpipe(data->usb_dev,
							  data->bulk_in),
					  buffer, USBTMC_BUFSIZE,
					  &actual, USB_CTRL_GET_TIMEOUT);

			print_hex_dump_debug("usbtmc ", DUMP_PREFIX_NONE,
					     16, 1, buffer, actual, true);

			n++;

			if (rv < 0) {
				dev_err(dev, "usb_control_msg returned %d\n",
					rv);
				goto exit;
			}
		} while ((actual == USBTMC_BUFSIZE) &&
			  (n < USBTMC_MAX_READS_TO_CLEAR_BULK_IN));
	} else {
		/* do not stress device with subsequent requests */
		msleep(50);
		n++;
	}

	if (n >= USBTMC_MAX_READS_TO_CLEAR_BULK_IN) {
		dev_err(dev, "Couldn't clear device buffer within %d cycles\n",
			USBTMC_MAX_READS_TO_CLEAR_BULK_IN);
		rv = -EPERM;
		goto exit;
	}

	goto usbtmc_clear_check_status;

usbtmc_clear_bulk_out_halt:

	rv = usb_clear_halt(data->usb_dev,
			    usb_sndbulkpipe(data->usb_dev, data->bulk_out));
	if (rv < 0) {
		dev_err(dev, "usb_clear_halt returned %d\n", rv);
		goto exit;
	}
	rv = 0;

exit:
	kfree(buffer);
	return rv;
}

static int usbtmc_ioctl_clear_out_halt(struct usbtmc_device_data *data)
{
	int rv;

	rv = usb_clear_halt(data->usb_dev,
			    usb_sndbulkpipe(data->usb_dev, data->bulk_out));

	if (rv < 0)
		dev_err(&data->usb_dev->dev, "%s returned %d\n", __func__, rv);
	return rv;
}

static int usbtmc_ioctl_clear_in_halt(struct usbtmc_device_data *data)
{
	int rv;

	rv = usb_clear_halt(data->usb_dev,
			    usb_rcvbulkpipe(data->usb_dev, data->bulk_in));

	if (rv < 0)
		dev_err(&data->usb_dev->dev, "%s returned %d\n", __func__, rv);
	return rv;
}

static int usbtmc_ioctl_cancel_io(struct usbtmc_file_data *file_data)
{
	spin_lock_irq(&file_data->err_lock);
	file_data->in_status = -ECANCELED;
	file_data->out_status = -ECANCELED;
	spin_unlock_irq(&file_data->err_lock);
	usb_kill_anchored_urbs(&file_data->submitted);
	return 0;
}

static int usbtmc_ioctl_cleanup_io(struct usbtmc_file_data *file_data)
{
	usb_kill_anchored_urbs(&file_data->submitted);
	usb_scuttle_anchored_urbs(&file_data->in_anchor);
	spin_lock_irq(&file_data->err_lock);
	file_data->in_status = 0;
	file_data->in_transfer_size = 0;
	file_data->out_status = 0;
	file_data->out_transfer_size = 0;
	spin_unlock_irq(&file_data->err_lock);

	file_data->in_urbs_used = 0;
	return 0;
}

static int get_capabilities(struct usbtmc_device_data *data)
{
	struct device *dev = &data->usb_dev->dev;
	char *buffer;
	int rv = 0;

	buffer = kmalloc(0x18, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev, usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_GET_CAPABILITIES,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     0, 0, buffer, 0x18, USB_CTRL_GET_TIMEOUT);
	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto err_out;
	}

	dev_dbg(dev, "GET_CAPABILITIES returned %x\n", buffer[0]);
	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "GET_CAPABILITIES returned %x\n", buffer[0]);
		rv = -EPERM;
		goto err_out;
	}
	dev_dbg(dev, "Interface capabilities are %x\n", buffer[4]);
	dev_dbg(dev, "Device capabilities are %x\n", buffer[5]);
	dev_dbg(dev, "USB488 interface capabilities are %x\n", buffer[14]);
	dev_dbg(dev, "USB488 device capabilities are %x\n", buffer[15]);

	data->capabilities.interface_capabilities = buffer[4];
	data->capabilities.device_capabilities = buffer[5];
	data->capabilities.usb488_interface_capabilities = buffer[14];
	data->capabilities.usb488_device_capabilities = buffer[15];
	data->usb488_caps = (buffer[14] & 0x07) | ((buffer[15] & 0x0f) << 4);
	rv = 0;

err_out:
	kfree(buffer);
	return rv;
}

#define capability_attribute(name)					\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usbtmc_device_data *data = usb_get_intfdata(intf);	\
									\
	return sprintf(buf, "%d\n", data->capabilities.name);		\
}									\
static DEVICE_ATTR_RO(name)

capability_attribute(interface_capabilities);
capability_attribute(device_capabilities);
capability_attribute(usb488_interface_capabilities);
capability_attribute(usb488_device_capabilities);

static struct attribute *usbtmc_attrs[] = {
	&dev_attr_interface_capabilities.attr,
	&dev_attr_device_capabilities.attr,
	&dev_attr_usb488_interface_capabilities.attr,
	&dev_attr_usb488_device_capabilities.attr,
	NULL,
};
ATTRIBUTE_GROUPS(usbtmc);

static int usbtmc_ioctl_indicator_pulse(struct usbtmc_device_data *data)
{
	struct device *dev;
	u8 *buffer;
	int rv;

	dev = &data->intf->dev;

	buffer = kmalloc(2, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_INDICATOR_PULSE,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     0, 0, buffer, 0x01, USB_CTRL_GET_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "INDICATOR_PULSE returned %x\n", buffer[0]);

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "INDICATOR_PULSE returned %x\n", buffer[0]);
		rv = -EPERM;
		goto exit;
	}
	rv = 0;

exit:
	kfree(buffer);
	return rv;
}

static int usbtmc_ioctl_request(struct usbtmc_device_data *data,
				void __user *arg)
{
	struct device *dev = &data->intf->dev;
	struct usbtmc_ctrlrequest request;
	u8 *buffer = NULL;
	int rv;
	unsigned int is_in, pipe;
	unsigned long res;

	res = copy_from_user(&request, arg, sizeof(struct usbtmc_ctrlrequest));
	if (res)
		return -EFAULT;

	if (request.req.wLength > USBTMC_BUFSIZE)
		return -EMSGSIZE;
	if (request.req.wLength == 0)	/* Length-0 requests are never IN */
		request.req.bRequestType &= ~USB_DIR_IN;

	is_in = request.req.bRequestType & USB_DIR_IN;

	if (request.req.wLength) {
		buffer = kmalloc(request.req.wLength, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		if (!is_in) {
			/* Send control data to device */
			res = copy_from_user(buffer, request.data,
					     request.req.wLength);
			if (res) {
				rv = -EFAULT;
				goto exit;
			}
		}
	}

	if (is_in)
		pipe = usb_rcvctrlpipe(data->usb_dev, 0);
	else
		pipe = usb_sndctrlpipe(data->usb_dev, 0);
	rv = usb_control_msg(data->usb_dev,
			pipe,
			request.req.bRequest,
			request.req.bRequestType,
			request.req.wValue,
			request.req.wIndex,
			buffer, request.req.wLength, USB_CTRL_GET_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "%s failed %d\n", __func__, rv);
		goto exit;
	}

	if (rv && is_in) {
		/* Read control data from device */
		res = copy_to_user(request.data, buffer, rv);
		if (res)
			rv = -EFAULT;
	}

 exit:
	kfree(buffer);
	return rv;
}

/*
 * Get the usb timeout value
 */
static int usbtmc_ioctl_get_timeout(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	u32 timeout;

	timeout = file_data->timeout;

	return put_user(timeout, (__u32 __user *)arg);
}

/*
 * Set the usb timeout value
 */
static int usbtmc_ioctl_set_timeout(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	u32 timeout;

	if (get_user(timeout, (__u32 __user *)arg))
		return -EFAULT;

	/* Note that timeout = 0 means
	 * MAX_SCHEDULE_TIMEOUT in usb_control_msg
	 */
	if (timeout < USBTMC_MIN_TIMEOUT)
		return -EINVAL;

	file_data->timeout = timeout;

	return 0;
}

/*
 * enables/disables sending EOM on write
 */
static int usbtmc_ioctl_eom_enable(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	u8 eom_enable;

	if (copy_from_user(&eom_enable, arg, sizeof(eom_enable)))
		return -EFAULT;

	if (eom_enable > 1)
		return -EINVAL;

	file_data->eom_val = eom_enable;

	return 0;
}

/*
 * Configure termination character for read()
 */
static int usbtmc_ioctl_config_termc(struct usbtmc_file_data *file_data,
				void __user *arg)
{
	struct usbtmc_termchar termc;

	if (copy_from_user(&termc, arg, sizeof(termc)))
		return -EFAULT;

	if ((termc.term_char_enabled > 1) ||
		(termc.term_char_enabled &&
		!(file_data->data->capabilities.device_capabilities & 1)))
		return -EINVAL;

	file_data->term_char = termc.term_char;
	file_data->term_char_enabled = termc.term_char_enabled;

	return 0;
}

static long usbtmc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usbtmc_file_data *file_data;
	struct usbtmc_device_data *data;
	int retval = -EBADRQC;
	__u8 tmp_byte;

	file_data = file->private_data;
	data = file_data->data;

	mutex_lock(&data->io_mutex);
	if (data->zombie) {
		retval = -ENODEV;
		goto skip_io_on_zombie;
	}

	switch (cmd) {
	case USBTMC_IOCTL_CLEAR_OUT_HALT:
		retval = usbtmc_ioctl_clear_out_halt(data);
		break;

	case USBTMC_IOCTL_CLEAR_IN_HALT:
		retval = usbtmc_ioctl_clear_in_halt(data);
		break;

	case USBTMC_IOCTL_INDICATOR_PULSE:
		retval = usbtmc_ioctl_indicator_pulse(data);
		break;

	case USBTMC_IOCTL_CLEAR:
		retval = usbtmc_ioctl_clear(data);
		break;

	case USBTMC_IOCTL_ABORT_BULK_OUT:
		retval = usbtmc_ioctl_abort_bulk_out(data);
		break;

	case USBTMC_IOCTL_ABORT_BULK_IN:
		retval = usbtmc_ioctl_abort_bulk_in(data);
		break;

	case USBTMC_IOCTL_CTRL_REQUEST:
		retval = usbtmc_ioctl_request(data, (void __user *)arg);
		break;

	case USBTMC_IOCTL_GET_TIMEOUT:
		retval = usbtmc_ioctl_get_timeout(file_data,
						  (void __user *)arg);
		break;

	case USBTMC_IOCTL_SET_TIMEOUT:
		retval = usbtmc_ioctl_set_timeout(file_data,
						  (void __user *)arg);
		break;

	case USBTMC_IOCTL_EOM_ENABLE:
		retval = usbtmc_ioctl_eom_enable(file_data,
						 (void __user *)arg);
		break;

	case USBTMC_IOCTL_CONFIG_TERMCHAR:
		retval = usbtmc_ioctl_config_termc(file_data,
						   (void __user *)arg);
		break;

	case USBTMC_IOCTL_WRITE:
		retval = usbtmc_ioctl_generic_write(file_data,
						    (void __user *)arg);
		break;

	case USBTMC_IOCTL_READ:
		retval = usbtmc_ioctl_generic_read(file_data,
						   (void __user *)arg);
		break;

	case USBTMC_IOCTL_WRITE_RESULT:
		retval = usbtmc_ioctl_write_result(file_data,
						   (void __user *)arg);
		break;

	case USBTMC_IOCTL_API_VERSION:
		retval = put_user(USBTMC_API_VERSION,
				  (__u32 __user *)arg);
		break;

	case USBTMC488_IOCTL_GET_CAPS:
		retval = put_user(data->usb488_caps,
				  (unsigned char __user *)arg);
		break;

	case USBTMC488_IOCTL_READ_STB:
		retval = usbtmc488_ioctl_read_stb(file_data,
						  (void __user *)arg);
		break;

	case USBTMC488_IOCTL_REN_CONTROL:
		retval = usbtmc488_ioctl_simple(data, (void __user *)arg,
						USBTMC488_REQUEST_REN_CONTROL);
		break;

	case USBTMC488_IOCTL_GOTO_LOCAL:
		retval = usbtmc488_ioctl_simple(data, (void __user *)arg,
						USBTMC488_REQUEST_GOTO_LOCAL);
		break;

	case USBTMC488_IOCTL_LOCAL_LOCKOUT:
		retval = usbtmc488_ioctl_simple(data, (void __user *)arg,
						USBTMC488_REQUEST_LOCAL_LOCKOUT);
		break;

	case USBTMC488_IOCTL_TRIGGER:
		retval = usbtmc488_ioctl_trigger(file_data);
		break;

	case USBTMC488_IOCTL_WAIT_SRQ:
		retval = usbtmc488_ioctl_wait_srq(file_data,
						  (__u32 __user *)arg);
		break;

	case USBTMC_IOCTL_MSG_IN_ATTR:
		retval = put_user(file_data->bmTransferAttributes,
				  (__u8 __user *)arg);
		break;

	case USBTMC_IOCTL_AUTO_ABORT:
		retval = get_user(tmp_byte, (unsigned char __user *)arg);
		if (retval == 0)
			file_data->auto_abort = !!tmp_byte;
		break;

	case USBTMC_IOCTL_GET_STB:
		retval = usbtmc_get_stb(file_data, &tmp_byte);
		if (retval > 0)
			retval = put_user(tmp_byte, (__u8 __user *)arg);
		break;

	case USBTMC_IOCTL_GET_SRQ_STB:
		retval = usbtmc_ioctl_get_srq_stb(file_data,
						  (void __user *)arg);
		break;

	case USBTMC_IOCTL_CANCEL_IO:
		retval = usbtmc_ioctl_cancel_io(file_data);
		break;

	case USBTMC_IOCTL_CLEANUP_IO:
		retval = usbtmc_ioctl_cleanup_io(file_data);
		break;
	}

skip_io_on_zombie:
	mutex_unlock(&data->io_mutex);
	return retval;
}

static int usbtmc_fasync(int fd, struct file *file, int on)
{
	struct usbtmc_file_data *file_data = file->private_data;

	return fasync_helper(fd, file, on, &file_data->data->fasync);
}

static __poll_t usbtmc_poll(struct file *file, poll_table *wait)
{
	struct usbtmc_file_data *file_data = file->private_data;
	struct usbtmc_device_data *data = file_data->data;
	__poll_t mask;

	mutex_lock(&data->io_mutex);

	if (data->zombie) {
		mask = EPOLLHUP | EPOLLERR;
		goto no_poll;
	}

	poll_wait(file, &data->waitq, wait);

	/* Note that EPOLLPRI is now assigned to SRQ, and
	 * EPOLLIN|EPOLLRDNORM to normal read data.
	 */
	mask = 0;
	if (atomic_read(&file_data->srq_asserted))
		mask |= EPOLLPRI;

	/* Note that the anchor submitted includes all urbs for BULK IN
	 * and OUT. So EPOLLOUT is signaled when BULK OUT is empty and
	 * all BULK IN urbs are completed and moved to in_anchor.
	 */
	if (usb_anchor_empty(&file_data->submitted))
		mask |= (EPOLLOUT | EPOLLWRNORM);
	if (!usb_anchor_empty(&file_data->in_anchor))
		mask |= (EPOLLIN | EPOLLRDNORM);

	spin_lock_irq(&file_data->err_lock);
	if (file_data->in_status || file_data->out_status)
		mask |= EPOLLERR;
	spin_unlock_irq(&file_data->err_lock);

	dev_dbg(&data->intf->dev, "poll mask = %x\n", mask);

no_poll:
	mutex_unlock(&data->io_mutex);
	return mask;
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.read		= usbtmc_read,
	.write		= usbtmc_write,
	.open		= usbtmc_open,
	.release	= usbtmc_release,
	.flush		= usbtmc_flush,
	.unlocked_ioctl	= usbtmc_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
	.fasync         = usbtmc_fasync,
	.poll           = usbtmc_poll,
	.llseek		= default_llseek,
};

static struct usb_class_driver usbtmc_class = {
	.name =		"usbtmc%d",
	.fops =		&fops,
	.minor_base =	USBTMC_MINOR_BASE,
};

static void usbtmc_interrupt(struct urb *urb)
{
	struct usbtmc_device_data *data = urb->context;
	struct device *dev = &data->intf->dev;
	int status = urb->status;
	int rv;

	dev_dbg(&data->intf->dev, "int status: %d len %d\n",
		status, urb->actual_length);

	switch (status) {
	case 0: /* SUCCESS */
		/* check for valid STB notification */
		if (data->iin_buffer[0] > 0x81) {
			data->bNotify1 = data->iin_buffer[0];
			data->bNotify2 = data->iin_buffer[1];
			atomic_set(&data->iin_data_valid, 1);
			wake_up_interruptible(&data->waitq);
			goto exit;
		}
		/* check for SRQ notification */
		if (data->iin_buffer[0] == 0x81) {
			unsigned long flags;
			struct list_head *elem;

			if (data->fasync)
				kill_fasync(&data->fasync,
					SIGIO, POLL_PRI);

			spin_lock_irqsave(&data->dev_lock, flags);
			list_for_each(elem, &data->file_list) {
				struct usbtmc_file_data *file_data;

				file_data = list_entry(elem,
						       struct usbtmc_file_data,
						       file_elem);
				file_data->srq_byte = data->iin_buffer[1];
				atomic_set(&file_data->srq_asserted, 1);
			}
			spin_unlock_irqrestore(&data->dev_lock, flags);

			dev_dbg(dev, "srq received bTag %x stb %x\n",
				(unsigned int)data->iin_buffer[0],
				(unsigned int)data->iin_buffer[1]);
			wake_up_interruptible_all(&data->waitq);
			goto exit;
		}
		dev_warn(dev, "invalid notification: %x\n",
			 data->iin_buffer[0]);
		break;
	case -EOVERFLOW:
		dev_err(dev, "overflow with length %d, actual length is %d\n",
			data->iin_wMaxPacketSize, urb->actual_length);
		fallthrough;
	default:
		/* urb terminated, clean up */
		dev_dbg(dev, "urb terminated, status: %d\n", status);
		return;
	}
exit:
	rv = usb_submit_urb(urb, GFP_ATOMIC);
	if (rv)
		dev_err(dev, "usb_submit_urb failed: %d\n", rv);
}

static void usbtmc_free_int(struct usbtmc_device_data *data)
{
	if (!data->iin_ep_present || !data->iin_urb)
		return;
	usb_kill_urb(data->iin_urb);
	kfree(data->iin_buffer);
	data->iin_buffer = NULL;
	usb_free_urb(data->iin_urb);
	data->iin_urb = NULL;
	kref_put(&data->kref, usbtmc_delete);
}

static int usbtmc_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usbtmc_device_data *data;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *bulk_in, *bulk_out, *int_in;
	int retcode;

	dev_dbg(&intf->dev, "%s called\n", __func__);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->intf = intf;
	data->id = id;
	data->usb_dev = usb_get_dev(interface_to_usbdev(intf));
	usb_set_intfdata(intf, data);
	kref_init(&data->kref);
	mutex_init(&data->io_mutex);
	init_waitqueue_head(&data->waitq);
	atomic_set(&data->iin_data_valid, 0);
	INIT_LIST_HEAD(&data->file_list);
	spin_lock_init(&data->dev_lock);

	data->zombie = 0;

	/* Initialize USBTMC bTag and other fields */
	data->bTag	= 1;
	/*  2 <= bTag <= 127   USBTMC-USB488 subclass specification 4.3.1 */
	data->iin_bTag = 2;

	/* USBTMC devices have only one setting, so use that */
	iface_desc = data->intf->cur_altsetting;
	data->ifnum = iface_desc->desc.bInterfaceNumber;

	/* Find bulk endpoints */
	retcode = usb_find_common_endpoints(iface_desc,
			&bulk_in, &bulk_out, NULL, NULL);
	if (retcode) {
		dev_err(&intf->dev, "bulk endpoints not found\n");
		goto err_put;
	}

	retcode = -EINVAL;
	data->bulk_in = bulk_in->bEndpointAddress;
	data->wMaxPacketSize = usb_endpoint_maxp(bulk_in);
	if (!data->wMaxPacketSize)
		goto err_put;
	dev_dbg(&intf->dev, "Found bulk in endpoint at %u\n", data->bulk_in);

	data->bulk_out = bulk_out->bEndpointAddress;
	dev_dbg(&intf->dev, "Found Bulk out endpoint at %u\n", data->bulk_out);

	/* Find int endpoint */
	retcode = usb_find_int_in_endpoint(iface_desc, &int_in);
	if (!retcode) {
		data->iin_ep_present = 1;
		data->iin_ep = int_in->bEndpointAddress;
		data->iin_wMaxPacketSize = usb_endpoint_maxp(int_in);
		data->iin_interval = int_in->bInterval;
		dev_dbg(&intf->dev, "Found Int in endpoint at %u\n",
				data->iin_ep);
	}

	retcode = get_capabilities(data);
	if (retcode)
		dev_err(&intf->dev, "can't read capabilities\n");

	if (data->iin_ep_present) {
		/* allocate int urb */
		data->iin_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!data->iin_urb) {
			retcode = -ENOMEM;
			goto error_register;
		}

		/* Protect interrupt in endpoint data until iin_urb is freed */
		kref_get(&data->kref);

		/* allocate buffer for interrupt in */
		data->iin_buffer = kmalloc(data->iin_wMaxPacketSize,
					GFP_KERNEL);
		if (!data->iin_buffer) {
			retcode = -ENOMEM;
			goto error_register;
		}

		/* fill interrupt urb */
		usb_fill_int_urb(data->iin_urb, data->usb_dev,
				usb_rcvintpipe(data->usb_dev, data->iin_ep),
				data->iin_buffer, data->iin_wMaxPacketSize,
				usbtmc_interrupt,
				data, data->iin_interval);

		retcode = usb_submit_urb(data->iin_urb, GFP_KERNEL);
		if (retcode) {
			dev_err(&intf->dev, "Failed to submit iin_urb\n");
			goto error_register;
		}
	}

	retcode = usb_register_dev(intf, &usbtmc_class);
	if (retcode) {
		dev_err(&intf->dev, "Not able to get a minor (base %u, slice default): %d\n",
			USBTMC_MINOR_BASE,
			retcode);
		goto error_register;
	}
	dev_dbg(&intf->dev, "Using minor number %d\n", intf->minor);

	return 0;

error_register:
	usbtmc_free_int(data);
err_put:
	kref_put(&data->kref, usbtmc_delete);
	return retcode;
}

static void usbtmc_disconnect(struct usb_interface *intf)
{
	struct usbtmc_device_data *data  = usb_get_intfdata(intf);
	struct list_head *elem;

	usb_deregister_dev(intf, &usbtmc_class);
	mutex_lock(&data->io_mutex);
	data->zombie = 1;
	wake_up_interruptible_all(&data->waitq);
	list_for_each(elem, &data->file_list) {
		struct usbtmc_file_data *file_data;

		file_data = list_entry(elem,
				       struct usbtmc_file_data,
				       file_elem);
		usb_kill_anchored_urbs(&file_data->submitted);
		usb_scuttle_anchored_urbs(&file_data->in_anchor);
	}
	mutex_unlock(&data->io_mutex);
	usbtmc_free_int(data);
	kref_put(&data->kref, usbtmc_delete);
}

static void usbtmc_draw_down(struct usbtmc_file_data *file_data)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&file_data->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&file_data->submitted);
	usb_scuttle_anchored_urbs(&file_data->in_anchor);
}

static int usbtmc_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbtmc_device_data *data = usb_get_intfdata(intf);
	struct list_head *elem;

	if (!data)
		return 0;

	mutex_lock(&data->io_mutex);
	list_for_each(elem, &data->file_list) {
		struct usbtmc_file_data *file_data;

		file_data = list_entry(elem,
				       struct usbtmc_file_data,
				       file_elem);
		usbtmc_draw_down(file_data);
	}

	if (data->iin_ep_present && data->iin_urb)
		usb_kill_urb(data->iin_urb);

	mutex_unlock(&data->io_mutex);
	return 0;
}

static int usbtmc_resume(struct usb_interface *intf)
{
	struct usbtmc_device_data *data = usb_get_intfdata(intf);
	int retcode = 0;

	if (data->iin_ep_present && data->iin_urb)
		retcode = usb_submit_urb(data->iin_urb, GFP_KERNEL);
	if (retcode)
		dev_err(&intf->dev, "Failed to submit iin_urb\n");

	return retcode;
}

static int usbtmc_pre_reset(struct usb_interface *intf)
{
	struct usbtmc_device_data *data  = usb_get_intfdata(intf);
	struct list_head *elem;

	if (!data)
		return 0;

	mutex_lock(&data->io_mutex);

	list_for_each(elem, &data->file_list) {
		struct usbtmc_file_data *file_data;

		file_data = list_entry(elem,
				       struct usbtmc_file_data,
				       file_elem);
		usbtmc_ioctl_cancel_io(file_data);
	}

	return 0;
}

static int usbtmc_post_reset(struct usb_interface *intf)
{
	struct usbtmc_device_data *data  = usb_get_intfdata(intf);

	mutex_unlock(&data->io_mutex);

	return 0;
}

static struct usb_driver usbtmc_driver = {
	.name		= "usbtmc",
	.id_table	= usbtmc_devices,
	.probe		= usbtmc_probe,
	.disconnect	= usbtmc_disconnect,
	.suspend	= usbtmc_suspend,
	.resume		= usbtmc_resume,
	.pre_reset	= usbtmc_pre_reset,
	.post_reset	= usbtmc_post_reset,
	.dev_groups	= usbtmc_groups,
};

module_usb_driver(usbtmc_driver);

MODULE_DESCRIPTION("USB Test & Measurement class driver");
MODULE_LICENSE("GPL");
