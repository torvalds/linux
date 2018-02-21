// SPDX-License-Identifier: GPL-2.0+
/**
 * drivers/usb/class/usbtmc.c - USB Test & Measurement class driver
 *
 * Copyright (C) 2007 Stefan Kopp, Gechingen, Germany
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 Greg Kroah-Hartman <gregkh@suse.de>
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
#include <linux/usb/tmc.h>


#define RIGOL			1
#define USBTMC_HEADER_SIZE	12
#define USBTMC_MINOR_BASE	176

/*
 * Size of driver internal IO buffer. Must be multiple of 4 and at least as
 * large as wMaxPacketSize (which is usually 512 bytes).
 */
#define USBTMC_SIZE_IOBUFFER	2048

/* Default USB timeout (in milliseconds) */
#define USBTMC_TIMEOUT		5000

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

	unsigned int bulk_in;
	unsigned int bulk_out;

	u8 bTag;
	u8 bTag_last_write;	/* needed for abort */
	u8 bTag_last_read;	/* needed for abort */

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
	atomic_t       srq_asserted;

	/* coalesced usb488_caps from usbtmc_dev_capabilities */
	__u8 usb488_caps;

	u8 rigol_quirk;

	/* attributes from the USB TMC spec for this device */
	u8 TermChar;
	bool TermCharEnabled;
	bool auto_abort;

	bool zombie; /* fd of disconnected device */

	struct usbtmc_dev_capabilities	capabilities;
	struct kref kref;
	struct mutex io_mutex;	/* only one i/o function running at a time */
	wait_queue_head_t waitq;
	struct fasync_struct *fasync;
};
#define to_usbtmc_data(d) container_of(d, struct usbtmc_device_data, kref)

struct usbtmc_ID_rigol_quirk {
	__u16 idVendor;
	__u16 idProduct;
};

static const struct usbtmc_ID_rigol_quirk usbtmc_id_quirk[] = {
	{ 0x1ab1, 0x0588 },
	{ 0x1ab1, 0x04b0 },
	{ 0, 0 }
};

/* Forward declarations */
static struct usb_driver usbtmc_driver;

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
	int retval = 0;

	intf = usb_find_interface(&usbtmc_driver, iminor(inode));
	if (!intf) {
		pr_err("can not find device for minor %d", iminor(inode));
		return -ENODEV;
	}

	data = usb_get_intfdata(intf);
	/* Protect reference to data from file structure until release */
	kref_get(&data->kref);

	/* Store pointer in file structure's private data field */
	filp->private_data = data;

	return retval;
}

static int usbtmc_release(struct inode *inode, struct file *file)
{
	struct usbtmc_device_data *data = file->private_data;

	kref_put(&data->kref, usbtmc_delete);
	return 0;
}

static int usbtmc_ioctl_abort_bulk_in(struct usbtmc_device_data *data)
{
	u8 *buffer;
	struct device *dev;
	int rv;
	int n;
	int actual;
	struct usb_host_interface *current_setting;
	int max_size;

	dev = &data->intf->dev;
	buffer = kmalloc(USBTMC_SIZE_IOBUFFER, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_INITIATE_ABORT_BULK_IN,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     data->bTag_last_read, data->bulk_in,
			     buffer, 2, USBTMC_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "INITIATE_ABORT_BULK_IN returned %x\n", buffer[0]);

	if (buffer[0] == USBTMC_STATUS_FAILED) {
		rv = 0;
		goto exit;
	}

	if (buffer[0] != USBTMC_STATUS_SUCCESS) {
		dev_err(dev, "INITIATE_ABORT_BULK_IN returned %x\n",
			buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	max_size = 0;
	current_setting = data->intf->cur_altsetting;
	for (n = 0; n < current_setting->desc.bNumEndpoints; n++)
		if (current_setting->endpoint[n].desc.bEndpointAddress ==
			data->bulk_in)
			max_size = usb_endpoint_maxp(&current_setting->endpoint[n].desc);

	if (max_size == 0) {
		dev_err(dev, "Couldn't get wMaxPacketSize\n");
		rv = -EPERM;
		goto exit;
	}

	dev_dbg(&data->intf->dev, "wMaxPacketSize is %d\n", max_size);

	n = 0;

	do {
		dev_dbg(dev, "Reading from bulk in EP\n");

		rv = usb_bulk_msg(data->usb_dev,
				  usb_rcvbulkpipe(data->usb_dev,
						  data->bulk_in),
				  buffer, USBTMC_SIZE_IOBUFFER,
				  &actual, USBTMC_TIMEOUT);

		n++;

		if (rv < 0) {
			dev_err(dev, "usb_bulk_msg returned %d\n", rv);
			goto exit;
		}
	} while ((actual == max_size) &&
		 (n < USBTMC_MAX_READS_TO_CLEAR_BULK_IN));

	if (actual == max_size) {
		dev_err(dev, "Couldn't clear device buffer within %d cycles\n",
			USBTMC_MAX_READS_TO_CLEAR_BULK_IN);
		rv = -EPERM;
		goto exit;
	}

	n = 0;

usbtmc_abort_bulk_in_status:
	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_CHECK_ABORT_BULK_IN_STATUS,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     0, data->bulk_in, buffer, 0x08,
			     USBTMC_TIMEOUT);

	if (rv < 0) {
		dev_err(dev, "usb_control_msg returned %d\n", rv);
		goto exit;
	}

	dev_dbg(dev, "INITIATE_ABORT_BULK_IN returned %x\n", buffer[0]);

	if (buffer[0] == USBTMC_STATUS_SUCCESS) {
		rv = 0;
		goto exit;
	}

	if (buffer[0] != USBTMC_STATUS_PENDING) {
		dev_err(dev, "INITIATE_ABORT_BULK_IN returned %x\n", buffer[0]);
		rv = -EPERM;
		goto exit;
	}

	if (buffer[1] == 1)
		do {
			dev_dbg(dev, "Reading from bulk in EP\n");

			rv = usb_bulk_msg(data->usb_dev,
					  usb_rcvbulkpipe(data->usb_dev,
							  data->bulk_in),
					  buffer, USBTMC_SIZE_IOBUFFER,
					  &actual, USBTMC_TIMEOUT);

			n++;

			if (rv < 0) {
				dev_err(dev, "usb_bulk_msg returned %d\n", rv);
				goto exit;
			}
		} while ((actual == max_size) &&
			 (n < USBTMC_MAX_READS_TO_CLEAR_BULK_IN));

	if (actual == max_size) {
		dev_err(dev, "Couldn't clear device buffer within %d cycles\n",
			USBTMC_MAX_READS_TO_CLEAR_BULK_IN);
		rv = -EPERM;
		goto exit;
	}

	goto usbtmc_abort_bulk_in_status;

exit:
	kfree(buffer);
	return rv;

}

static int usbtmc_ioctl_abort_bulk_out(struct usbtmc_device_data *data)
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
			     data->bTag_last_write, data->bulk_out,
			     buffer, 2, USBTMC_TIMEOUT);

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
	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_CHECK_ABORT_BULK_OUT_STATUS,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
			     0, data->bulk_out, buffer, 0x08,
			     USBTMC_TIMEOUT);
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

static int usbtmc488_ioctl_read_stb(struct usbtmc_device_data *data,
				void __user *arg)
{
	struct device *dev = &data->intf->dev;
	u8 *buffer;
	u8 tag;
	__u8 stb;
	int rv;

	dev_dbg(dev, "Enter ioctl_read_stb iin_ep_present: %d\n",
		data->iin_ep_present);

	buffer = kmalloc(8, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	atomic_set(&data->iin_data_valid, 0);

	/* must issue read_stb before using poll or select */
	atomic_set(&data->srq_asserted, 0);

	rv = usb_control_msg(data->usb_dev,
			usb_rcvctrlpipe(data->usb_dev, 0),
			USBTMC488_REQUEST_READ_STATUS_BYTE,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			data->iin_bTag,
			data->ifnum,
			buffer, 0x03, USBTMC_TIMEOUT);
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
			USBTMC_TIMEOUT);
		if (rv < 0) {
			dev_dbg(dev, "wait interrupted %d\n", rv);
			goto exit;
		}

		if (rv == 0) {
			dev_dbg(dev, "wait timed out\n");
			rv = -ETIME;
			goto exit;
		}

		tag = data->bNotify1 & 0x7f;
		if (tag != data->iin_bTag) {
			dev_err(dev, "expected bTag %x got %x\n",
				data->iin_bTag, tag);
		}

		stb = data->bNotify2;
	} else {
		stb = buffer[2];
	}

	rv = copy_to_user(arg, &stb, sizeof(stb));
	if (rv)
		rv = -EFAULT;

 exit:
	/* bump interrupt bTag */
	data->iin_bTag += 1;
	if (data->iin_bTag > 127)
		/* 1 is for SRQ see USBTMC-USB488 subclass spec section 4.3.1 */
		data->iin_bTag = 2;

	kfree(buffer);
	return rv;
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
			buffer, 0x01, USBTMC_TIMEOUT);
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
 * Sends a REQUEST_DEV_DEP_MSG_IN message on the Bulk-OUT endpoint.
 * @transfer_size: number of bytes to request from the device.
 *
 * See the USBTMC specification, Table 4.
 *
 * Also updates bTag_last_write.
 */
static int send_request_dev_dep_msg_in(struct usbtmc_device_data *data, size_t transfer_size)
{
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
	buffer[8] = data->TermCharEnabled * 2;
	/* Use term character? */
	buffer[9] = data->TermChar;
	buffer[10] = 0; /* Reserved */
	buffer[11] = 0; /* Reserved */

	/* Send bulk URB */
	retval = usb_bulk_msg(data->usb_dev,
			      usb_sndbulkpipe(data->usb_dev,
					      data->bulk_out),
			      buffer, USBTMC_HEADER_SIZE, &actual, USBTMC_TIMEOUT);

	/* Store bTag (in case we need to abort) */
	data->bTag_last_write = data->bTag;

	/* Increment bTag -- and increment again if zero */
	data->bTag++;
	if (!data->bTag)
		data->bTag++;

	kfree(buffer);
	if (retval < 0) {
		dev_err(&data->intf->dev, "usb_bulk_msg in send_request_dev_dep_msg_in() returned %d\n", retval);
		return retval;
	}

	return 0;
}

static ssize_t usbtmc_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	struct usbtmc_device_data *data;
	struct device *dev;
	u32 n_characters;
	u8 *buffer;
	int actual;
	size_t done;
	size_t remaining;
	int retval;
	size_t this_part;

	/* Get pointer to private data structure */
	data = filp->private_data;
	dev = &data->intf->dev;

	buffer = kmalloc(USBTMC_SIZE_IOBUFFER, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	mutex_lock(&data->io_mutex);
	if (data->zombie) {
		retval = -ENODEV;
		goto exit;
	}

	if (data->rigol_quirk) {
		dev_dbg(dev, "usb_bulk_msg_in: count(%zu)\n", count);

		retval = send_request_dev_dep_msg_in(data, count);

		if (retval < 0) {
			if (data->auto_abort)
				usbtmc_ioctl_abort_bulk_out(data);
			goto exit;
		}
	}

	/* Loop until we have fetched everything we requested */
	remaining = count;
	this_part = remaining;
	done = 0;

	while (remaining > 0) {
		if (!data->rigol_quirk) {
			dev_dbg(dev, "usb_bulk_msg_in: remaining(%zu), count(%zu)\n", remaining, count);

			if (remaining > USBTMC_SIZE_IOBUFFER - USBTMC_HEADER_SIZE - 3)
				this_part = USBTMC_SIZE_IOBUFFER - USBTMC_HEADER_SIZE - 3;
			else
				this_part = remaining;

			retval = send_request_dev_dep_msg_in(data, this_part);
			if (retval < 0) {
			dev_err(dev, "usb_bulk_msg returned %d\n", retval);
				if (data->auto_abort)
					usbtmc_ioctl_abort_bulk_out(data);
				goto exit;
			}
		}

		/* Send bulk URB */
		retval = usb_bulk_msg(data->usb_dev,
				      usb_rcvbulkpipe(data->usb_dev,
						      data->bulk_in),
				      buffer, USBTMC_SIZE_IOBUFFER, &actual,
				      USBTMC_TIMEOUT);

		dev_dbg(dev, "usb_bulk_msg: retval(%u), done(%zu), remaining(%zu), actual(%d)\n", retval, done, remaining, actual);

		/* Store bTag (in case we need to abort) */
		data->bTag_last_read = data->bTag;

		if (retval < 0) {
			dev_dbg(dev, "Unable to read data, error %d\n", retval);
			if (data->auto_abort)
				usbtmc_ioctl_abort_bulk_in(data);
			goto exit;
		}

		/* Parse header in first packet */
		if ((done == 0) || !data->rigol_quirk) {
			/* Sanity checks for the header */
			if (actual < USBTMC_HEADER_SIZE) {
				dev_err(dev, "Device sent too small first packet: %u < %u\n", actual, USBTMC_HEADER_SIZE);
				if (data->auto_abort)
					usbtmc_ioctl_abort_bulk_in(data);
				goto exit;
			}

			if (buffer[0] != 2) {
				dev_err(dev, "Device sent reply with wrong MsgID: %u != 2\n", buffer[0]);
				if (data->auto_abort)
					usbtmc_ioctl_abort_bulk_in(data);
				goto exit;
			}

			if (buffer[1] != data->bTag_last_write) {
				dev_err(dev, "Device sent reply with wrong bTag: %u != %u\n", buffer[1], data->bTag_last_write);
				if (data->auto_abort)
					usbtmc_ioctl_abort_bulk_in(data);
				goto exit;
			}

			/* How many characters did the instrument send? */
			n_characters = buffer[4] +
				       (buffer[5] << 8) +
				       (buffer[6] << 16) +
				       (buffer[7] << 24);

			if (n_characters > this_part) {
				dev_err(dev, "Device wants to return more data than requested: %u > %zu\n", n_characters, count);
				if (data->auto_abort)
					usbtmc_ioctl_abort_bulk_in(data);
				goto exit;
			}

			/* Remove the USBTMC header */
			actual -= USBTMC_HEADER_SIZE;

			/* Check if the message is smaller than requested */
			if (data->rigol_quirk) {
				if (remaining > n_characters)
					remaining = n_characters;
				/* Remove padding if it exists */
				if (actual > remaining)
					actual = remaining;
			}
			else {
				if (this_part > n_characters)
					this_part = n_characters;
				/* Remove padding if it exists */
				if (actual > this_part)
					actual = this_part;
			}

			dev_dbg(dev, "Bulk-IN header: N_characters(%u), bTransAttr(%u)\n", n_characters, buffer[8]);

			remaining -= actual;

			/* Terminate if end-of-message bit received from device */
			if ((buffer[8] & 0x01) && (actual >= n_characters))
				remaining = 0;

			dev_dbg(dev, "Bulk-IN header: remaining(%zu), buf(%p), buffer(%p) done(%zu)\n", remaining,buf,buffer,done);


			/* Copy buffer to user space */
			if (copy_to_user(buf + done, &buffer[USBTMC_HEADER_SIZE], actual)) {
				/* There must have been an addressing problem */
				retval = -EFAULT;
				goto exit;
			}
			done += actual;
		}
		else  {
			if (actual > remaining)
				actual = remaining;

			remaining -= actual;

			dev_dbg(dev, "Bulk-IN header cont: actual(%u), done(%zu), remaining(%zu), buf(%p), buffer(%p)\n", actual, done, remaining,buf,buffer);

			/* Copy buffer to user space */
			if (copy_to_user(buf + done, buffer, actual)) {
				/* There must have been an addressing problem */
				retval = -EFAULT;
				goto exit;
			}
			done += actual;
		}
	}

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
	struct usbtmc_device_data *data;
	u8 *buffer;
	int retval;
	int actual;
	unsigned long int n_bytes;
	int remaining;
	int done;
	int this_part;

	data = filp->private_data;

	buffer = kmalloc(USBTMC_SIZE_IOBUFFER, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	mutex_lock(&data->io_mutex);
	if (data->zombie) {
		retval = -ENODEV;
		goto exit;
	}

	remaining = count;
	done = 0;

	while (remaining > 0) {
		if (remaining > USBTMC_SIZE_IOBUFFER - USBTMC_HEADER_SIZE) {
			this_part = USBTMC_SIZE_IOBUFFER - USBTMC_HEADER_SIZE;
			buffer[8] = 0;
		} else {
			this_part = remaining;
			buffer[8] = 1;
		}

		/* Setup IO buffer for DEV_DEP_MSG_OUT message */
		buffer[0] = 1;
		buffer[1] = data->bTag;
		buffer[2] = ~data->bTag;
		buffer[3] = 0; /* Reserved */
		buffer[4] = this_part >> 0;
		buffer[5] = this_part >> 8;
		buffer[6] = this_part >> 16;
		buffer[7] = this_part >> 24;
		/* buffer[8] is set above... */
		buffer[9] = 0; /* Reserved */
		buffer[10] = 0; /* Reserved */
		buffer[11] = 0; /* Reserved */

		if (copy_from_user(&buffer[USBTMC_HEADER_SIZE], buf + done, this_part)) {
			retval = -EFAULT;
			goto exit;
		}

		n_bytes = roundup(USBTMC_HEADER_SIZE + this_part, 4);
		memset(buffer + USBTMC_HEADER_SIZE + this_part, 0, n_bytes - (USBTMC_HEADER_SIZE + this_part));

		do {
			retval = usb_bulk_msg(data->usb_dev,
					      usb_sndbulkpipe(data->usb_dev,
							      data->bulk_out),
					      buffer, n_bytes,
					      &actual, USBTMC_TIMEOUT);
			if (retval != 0)
				break;
			n_bytes -= actual;
		} while (n_bytes);

		data->bTag_last_write = data->bTag;
		data->bTag++;

		if (!data->bTag)
			data->bTag++;

		if (retval < 0) {
			dev_err(&data->intf->dev,
				"Unable to send data, error %d\n", retval);
			if (data->auto_abort)
				usbtmc_ioctl_abort_bulk_out(data);
			goto exit;
		}

		remaining -= this_part;
		done += this_part;
	}

	retval = count;
exit:
	mutex_unlock(&data->io_mutex);
	kfree(buffer);
	return retval;
}

static int usbtmc_ioctl_clear(struct usbtmc_device_data *data)
{
	struct usb_host_interface *current_setting;
	struct usb_endpoint_descriptor *desc;
	struct device *dev;
	u8 *buffer;
	int rv;
	int n;
	int actual = 0;
	int max_size;

	dev = &data->intf->dev;

	dev_dbg(dev, "Sending INITIATE_CLEAR request\n");

	buffer = kmalloc(USBTMC_SIZE_IOBUFFER, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_INITIATE_CLEAR,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     0, 0, buffer, 1, USBTMC_TIMEOUT);
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

	max_size = 0;
	current_setting = data->intf->cur_altsetting;
	for (n = 0; n < current_setting->desc.bNumEndpoints; n++) {
		desc = &current_setting->endpoint[n].desc;
		if (desc->bEndpointAddress == data->bulk_in)
			max_size = usb_endpoint_maxp(desc);
	}

	if (max_size == 0) {
		dev_err(dev, "Couldn't get wMaxPacketSize\n");
		rv = -EPERM;
		goto exit;
	}

	dev_dbg(dev, "wMaxPacketSize is %d\n", max_size);

	n = 0;

usbtmc_clear_check_status:

	dev_dbg(dev, "Sending CHECK_CLEAR_STATUS request\n");

	rv = usb_control_msg(data->usb_dev,
			     usb_rcvctrlpipe(data->usb_dev, 0),
			     USBTMC_REQUEST_CHECK_CLEAR_STATUS,
			     USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     0, 0, buffer, 2, USBTMC_TIMEOUT);
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

	if (buffer[1] == 1)
		do {
			dev_dbg(dev, "Reading from bulk in EP\n");

			rv = usb_bulk_msg(data->usb_dev,
					  usb_rcvbulkpipe(data->usb_dev,
							  data->bulk_in),
					  buffer, USBTMC_SIZE_IOBUFFER,
					  &actual, USBTMC_TIMEOUT);
			n++;

			if (rv < 0) {
				dev_err(dev, "usb_control_msg returned %d\n",
					rv);
				goto exit;
			}
		} while ((actual == max_size) &&
			  (n < USBTMC_MAX_READS_TO_CLEAR_BULK_IN));

	if (actual == max_size) {
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
		dev_err(dev, "usb_control_msg returned %d\n", rv);
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

	if (rv < 0) {
		dev_err(&data->usb_dev->dev, "usb_control_msg returned %d\n",
			rv);
		return rv;
	}
	return 0;
}

static int usbtmc_ioctl_clear_in_halt(struct usbtmc_device_data *data)
{
	int rv;

	rv = usb_clear_halt(data->usb_dev,
			    usb_rcvbulkpipe(data->usb_dev, data->bulk_in));

	if (rv < 0) {
		dev_err(&data->usb_dev->dev, "usb_control_msg returned %d\n",
			rv);
		return rv;
	}
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
			     0, 0, buffer, 0x18, USBTMC_TIMEOUT);
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

static struct attribute *capability_attrs[] = {
	&dev_attr_interface_capabilities.attr,
	&dev_attr_device_capabilities.attr,
	&dev_attr_usb488_interface_capabilities.attr,
	&dev_attr_usb488_device_capabilities.attr,
	NULL,
};

static const struct attribute_group capability_attr_grp = {
	.attrs = capability_attrs,
};

static ssize_t TermChar_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usbtmc_device_data *data = usb_get_intfdata(intf);

	return sprintf(buf, "%c\n", data->TermChar);
}

static ssize_t TermChar_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usbtmc_device_data *data = usb_get_intfdata(intf);

	if (count < 1)
		return -EINVAL;
	data->TermChar = buf[0];
	return count;
}
static DEVICE_ATTR_RW(TermChar);

#define data_attribute(name)						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usbtmc_device_data *data = usb_get_intfdata(intf);	\
									\
	return sprintf(buf, "%d\n", data->name);			\
}									\
static ssize_t name##_store(struct device *dev,				\
			    struct device_attribute *attr,		\
			    const char *buf, size_t count)		\
{									\
	struct usb_interface *intf = to_usb_interface(dev);		\
	struct usbtmc_device_data *data = usb_get_intfdata(intf);	\
	ssize_t result;							\
	unsigned val;							\
									\
	result = sscanf(buf, "%u\n", &val);				\
	if (result != 1)						\
		result = -EINVAL;					\
	data->name = val;						\
	if (result < 0)							\
		return result;						\
	else								\
		return count;						\
}									\
static DEVICE_ATTR_RW(name)

data_attribute(TermCharEnabled);
data_attribute(auto_abort);

static struct attribute *data_attrs[] = {
	&dev_attr_TermChar.attr,
	&dev_attr_TermCharEnabled.attr,
	&dev_attr_auto_abort.attr,
	NULL,
};

static const struct attribute_group data_attr_grp = {
	.attrs = data_attrs,
};

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
			     0, 0, buffer, 0x01, USBTMC_TIMEOUT);

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

static long usbtmc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usbtmc_device_data *data;
	int retval = -EBADRQC;

	data = file->private_data;
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

	case USBTMC488_IOCTL_GET_CAPS:
		retval = copy_to_user((void __user *)arg,
				&data->usb488_caps,
				sizeof(data->usb488_caps));
		if (retval)
			retval = -EFAULT;
		break;

	case USBTMC488_IOCTL_READ_STB:
		retval = usbtmc488_ioctl_read_stb(data, (void __user *)arg);
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
	}

skip_io_on_zombie:
	mutex_unlock(&data->io_mutex);
	return retval;
}

static int usbtmc_fasync(int fd, struct file *file, int on)
{
	struct usbtmc_device_data *data = file->private_data;

	return fasync_helper(fd, file, on, &data->fasync);
}

static __poll_t usbtmc_poll(struct file *file, poll_table *wait)
{
	struct usbtmc_device_data *data = file->private_data;
	__poll_t mask;

	mutex_lock(&data->io_mutex);

	if (data->zombie) {
		mask = EPOLLHUP | EPOLLERR;
		goto no_poll;
	}

	poll_wait(file, &data->waitq, wait);

	mask = (atomic_read(&data->srq_asserted)) ? EPOLLIN | EPOLLRDNORM : 0;

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
	.unlocked_ioctl	= usbtmc_ioctl,
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
			if (data->fasync)
				kill_fasync(&data->fasync,
					SIGIO, POLL_IN);

			atomic_set(&data->srq_asserted, 1);
			wake_up_interruptible(&data->waitq);
			goto exit;
		}
		dev_warn(dev, "invalid notification: %x\n", data->iin_buffer[0]);
		break;
	case -EOVERFLOW:
		dev_err(dev, "overflow with length %d, actual length is %d\n",
			data->iin_wMaxPacketSize, urb->actual_length);
		/* fall through */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -EILSEQ:
	case -ETIME:
		/* urb terminated, clean up */
		dev_dbg(dev, "urb terminated, status: %d\n", status);
		return;
	default:
		dev_err(dev, "unknown status received: %d\n", status);
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
	usb_free_urb(data->iin_urb);
	kref_put(&data->kref, usbtmc_delete);
}

static int usbtmc_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usbtmc_device_data *data;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *bulk_in, *bulk_out, *int_in;
	int n;
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
	atomic_set(&data->srq_asserted, 0);
	data->zombie = 0;

	/* Determine if it is a Rigol or not */
	data->rigol_quirk = 0;
	dev_dbg(&intf->dev, "Trying to find if device Vendor 0x%04X Product 0x%04X has the RIGOL quirk\n",
		le16_to_cpu(data->usb_dev->descriptor.idVendor),
		le16_to_cpu(data->usb_dev->descriptor.idProduct));
	for(n = 0; usbtmc_id_quirk[n].idVendor > 0; n++) {
		if ((usbtmc_id_quirk[n].idVendor == le16_to_cpu(data->usb_dev->descriptor.idVendor)) &&
		    (usbtmc_id_quirk[n].idProduct == le16_to_cpu(data->usb_dev->descriptor.idProduct))) {
			dev_dbg(&intf->dev, "Setting this device as having the RIGOL quirk\n");
			data->rigol_quirk = 1;
			break;
		}
	}

	/* Initialize USBTMC bTag and other fields */
	data->bTag	= 1;
	data->TermCharEnabled = 0;
	data->TermChar = '\n';
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

	data->bulk_in = bulk_in->bEndpointAddress;
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
	else
		retcode = sysfs_create_group(&intf->dev.kobj,
					     &capability_attr_grp);

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

	retcode = sysfs_create_group(&intf->dev.kobj, &data_attr_grp);

	retcode = usb_register_dev(intf, &usbtmc_class);
	if (retcode) {
		dev_err(&intf->dev, "Not able to get a minor"
			" (base %u, slice default): %d\n", USBTMC_MINOR_BASE,
			retcode);
		goto error_register;
	}
	dev_dbg(&intf->dev, "Using minor number %d\n", intf->minor);

	return 0;

error_register:
	sysfs_remove_group(&intf->dev.kobj, &capability_attr_grp);
	sysfs_remove_group(&intf->dev.kobj, &data_attr_grp);
	usbtmc_free_int(data);
err_put:
	kref_put(&data->kref, usbtmc_delete);
	return retcode;
}

static void usbtmc_disconnect(struct usb_interface *intf)
{
	struct usbtmc_device_data *data;

	dev_dbg(&intf->dev, "usbtmc_disconnect called\n");

	data = usb_get_intfdata(intf);
	usb_deregister_dev(intf, &usbtmc_class);
	sysfs_remove_group(&intf->dev.kobj, &capability_attr_grp);
	sysfs_remove_group(&intf->dev.kobj, &data_attr_grp);
	mutex_lock(&data->io_mutex);
	data->zombie = 1;
	wake_up_all(&data->waitq);
	mutex_unlock(&data->io_mutex);
	usbtmc_free_int(data);
	kref_put(&data->kref, usbtmc_delete);
}

static int usbtmc_suspend(struct usb_interface *intf, pm_message_t message)
{
	/* this driver does not have pending URBs */
	return 0;
}

static int usbtmc_resume(struct usb_interface *intf)
{
	return 0;
}

static struct usb_driver usbtmc_driver = {
	.name		= "usbtmc",
	.id_table	= usbtmc_devices,
	.probe		= usbtmc_probe,
	.disconnect	= usbtmc_disconnect,
	.suspend	= usbtmc_suspend,
	.resume		= usbtmc_resume,
};

module_usb_driver(usbtmc_driver);

MODULE_LICENSE("GPL");
