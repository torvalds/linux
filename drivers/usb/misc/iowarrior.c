// SPDX-License-Identifier: GPL-2.0
/*
 *  Native support for the I/O-Warrior USB devices
 *
 *  Copyright (c) 2003-2005  Code Mercenaries GmbH
 *  written by Christian Lucht <lucht@codemercs.com>
 *
 *  based on

 *  usb-skeleton.c by Greg Kroah-Hartman  <greg@kroah.com>
 *  brlvger.c by Stephane Dalton  <sdalton@videotron.ca>
 *           and St�hane Doyon   <s.doyon@videotron.ca>
 *
 *  Released under the GPLv2.
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/usb/iowarrior.h>

#define DRIVER_AUTHOR "Christian Lucht <lucht@codemercs.com>"
#define DRIVER_DESC "USB IO-Warrior driver"

#define USB_VENDOR_ID_CODEMERCS		1984
/* low speed iowarrior */
#define USB_DEVICE_ID_CODEMERCS_IOW40	0x1500
#define USB_DEVICE_ID_CODEMERCS_IOW24	0x1501
#define USB_DEVICE_ID_CODEMERCS_IOWPV1	0x1511
#define USB_DEVICE_ID_CODEMERCS_IOWPV2	0x1512
/* full speed iowarrior */
#define USB_DEVICE_ID_CODEMERCS_IOW56	0x1503

/* Get a minor range for your devices from the usb maintainer */
#ifdef CONFIG_USB_DYNAMIC_MINORS
#define IOWARRIOR_MINOR_BASE	0
#else
#define IOWARRIOR_MINOR_BASE	208	// SKELETON_MINOR_BASE 192 + 16, not official yet
#endif

/* interrupt input queue size */
#define MAX_INTERRUPT_BUFFER 16
/*
   maximum number of urbs that are submitted for writes at the same time,
   this applies to the IOWarrior56 only!
   IOWarrior24 and IOWarrior40 use synchronous usb_control_msg calls.
*/
#define MAX_WRITES_IN_FLIGHT 4

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/* Module parameters */
static DEFINE_MUTEX(iowarrior_mutex);

static struct usb_driver iowarrior_driver;
static DEFINE_MUTEX(iowarrior_open_disc_lock);

/*--------------*/
/*     data     */
/*--------------*/

/* Structure to hold all of our device specific stuff */
struct iowarrior {
	struct mutex mutex;			/* locks this structure */
	struct usb_device *udev;		/* save off the usb device pointer */
	struct usb_interface *interface;	/* the interface for this device */
	unsigned char minor;			/* the starting minor number for this device */
	struct usb_endpoint_descriptor *int_out_endpoint;	/* endpoint for reading (needed for IOW56 only) */
	struct usb_endpoint_descriptor *int_in_endpoint;	/* endpoint for reading */
	struct urb *int_in_urb;		/* the urb for reading data */
	unsigned char *int_in_buffer;	/* buffer for data to be read */
	unsigned char serial_number;	/* to detect lost packages */
	unsigned char *read_queue;	/* size is MAX_INTERRUPT_BUFFER * packet size */
	wait_queue_head_t read_wait;
	wait_queue_head_t write_wait;	/* wait-queue for writing to the device */
	atomic_t write_busy;		/* number of write-urbs submitted */
	atomic_t read_idx;
	atomic_t intr_idx;
	atomic_t overflow_flag;		/* signals an index 'rollover' */
	int present;			/* this is 1 as long as the device is connected */
	int opened;			/* this is 1 if the device is currently open */
	char chip_serial[9];		/* the serial number string of the chip connected */
	int report_size;		/* number of bytes in a report */
	u16 product_id;
};

/*--------------*/
/*    globals   */
/*--------------*/

/*
 *  USB spec identifies 5 second timeouts.
 */
#define GET_TIMEOUT 5
#define USB_REQ_GET_REPORT  0x01
//#if 0
static int usb_get_report(struct usb_device *dev,
			  struct usb_host_interface *inter, unsigned char type,
			  unsigned char id, void *buf, int size)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       USB_REQ_GET_REPORT,
			       USB_DIR_IN | USB_TYPE_CLASS |
			       USB_RECIP_INTERFACE, (type << 8) + id,
			       inter->desc.bInterfaceNumber, buf, size,
			       GET_TIMEOUT*HZ);
}
//#endif

#define USB_REQ_SET_REPORT 0x09

static int usb_set_report(struct usb_interface *intf, unsigned char type,
			  unsigned char id, void *buf, int size)
{
	return usb_control_msg(interface_to_usbdev(intf),
			       usb_sndctrlpipe(interface_to_usbdev(intf), 0),
			       USB_REQ_SET_REPORT,
			       USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			       (type << 8) + id,
			       intf->cur_altsetting->desc.bInterfaceNumber, buf,
			       size, HZ);
}

/*---------------------*/
/* driver registration */
/*---------------------*/
/* table of devices that work with this driver */
static const struct usb_device_id iowarrior_ids[] = {
	{USB_DEVICE(USB_VENDOR_ID_CODEMERCS, USB_DEVICE_ID_CODEMERCS_IOW40)},
	{USB_DEVICE(USB_VENDOR_ID_CODEMERCS, USB_DEVICE_ID_CODEMERCS_IOW24)},
	{USB_DEVICE(USB_VENDOR_ID_CODEMERCS, USB_DEVICE_ID_CODEMERCS_IOWPV1)},
	{USB_DEVICE(USB_VENDOR_ID_CODEMERCS, USB_DEVICE_ID_CODEMERCS_IOWPV2)},
	{USB_DEVICE(USB_VENDOR_ID_CODEMERCS, USB_DEVICE_ID_CODEMERCS_IOW56)},
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, iowarrior_ids);

/*
 * USB callback handler for reading data
 */
static void iowarrior_callback(struct urb *urb)
{
	struct iowarrior *dev = urb->context;
	int intr_idx;
	int read_idx;
	int aux_idx;
	int offset;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		goto exit;
	}

	intr_idx = atomic_read(&dev->intr_idx);
	/* aux_idx become previous intr_idx */
	aux_idx = (intr_idx == 0) ? (MAX_INTERRUPT_BUFFER - 1) : (intr_idx - 1);
	read_idx = atomic_read(&dev->read_idx);

	/* queue is not empty and it's interface 0 */
	if ((intr_idx != read_idx)
	    && (dev->interface->cur_altsetting->desc.bInterfaceNumber == 0)) {
		/* + 1 for serial number */
		offset = aux_idx * (dev->report_size + 1);
		if (!memcmp
		    (dev->read_queue + offset, urb->transfer_buffer,
		     dev->report_size)) {
			/* equal values on interface 0 will be ignored */
			goto exit;
		}
	}

	/* aux_idx become next intr_idx */
	aux_idx = (intr_idx == (MAX_INTERRUPT_BUFFER - 1)) ? 0 : (intr_idx + 1);
	if (read_idx == aux_idx) {
		/* queue full, dropping oldest input */
		read_idx = (++read_idx == MAX_INTERRUPT_BUFFER) ? 0 : read_idx;
		atomic_set(&dev->read_idx, read_idx);
		atomic_set(&dev->overflow_flag, 1);
	}

	/* +1 for serial number */
	offset = intr_idx * (dev->report_size + 1);
	memcpy(dev->read_queue + offset, urb->transfer_buffer,
	       dev->report_size);
	*(dev->read_queue + offset + (dev->report_size)) = dev->serial_number++;

	atomic_set(&dev->intr_idx, aux_idx);
	/* tell the blocking read about the new data */
	wake_up_interruptible(&dev->read_wait);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&dev->interface->dev, "%s - usb_submit_urb failed with result %d\n",
			__func__, retval);

}

/*
 * USB Callback handler for write-ops
 */
static void iowarrior_write_callback(struct urb *urb)
{
	struct iowarrior *dev;
	int status = urb->status;

	dev = urb->context;
	/* sync/async unlink faults aren't errors */
	if (status &&
	    !(status == -ENOENT ||
	      status == -ECONNRESET || status == -ESHUTDOWN)) {
		dev_dbg(&dev->interface->dev,
			"nonzero write bulk status received: %d\n", status);
	}
	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	/* tell a waiting writer the interrupt-out-pipe is available again */
	atomic_dec(&dev->write_busy);
	wake_up_interruptible(&dev->write_wait);
}

/**
 *	iowarrior_delete
 */
static inline void iowarrior_delete(struct iowarrior *dev)
{
	dev_dbg(&dev->interface->dev, "minor %d\n", dev->minor);
	kfree(dev->int_in_buffer);
	usb_free_urb(dev->int_in_urb);
	kfree(dev->read_queue);
	kfree(dev);
}

/*---------------------*/
/* fops implementation */
/*---------------------*/

static int read_index(struct iowarrior *dev)
{
	int intr_idx, read_idx;

	read_idx = atomic_read(&dev->read_idx);
	intr_idx = atomic_read(&dev->intr_idx);

	return (read_idx == intr_idx ? -1 : read_idx);
}

/**
 *  iowarrior_read
 */
static ssize_t iowarrior_read(struct file *file, char __user *buffer,
			      size_t count, loff_t *ppos)
{
	struct iowarrior *dev;
	int read_idx;
	int offset;

	dev = file->private_data;

	/* verify that the device wasn't unplugged */
	if (!dev || !dev->present)
		return -ENODEV;

	dev_dbg(&dev->interface->dev, "minor %d, count = %zd\n",
		dev->minor, count);

	/* read count must be packet size (+ time stamp) */
	if ((count != dev->report_size)
	    && (count != (dev->report_size + 1)))
		return -EINVAL;

	/* repeat until no buffer overrun in callback handler occur */
	do {
		atomic_set(&dev->overflow_flag, 0);
		if ((read_idx = read_index(dev)) == -1) {
			/* queue empty */
			if (file->f_flags & O_NONBLOCK)
				return -EAGAIN;
			else {
				//next line will return when there is either new data, or the device is unplugged
				int r = wait_event_interruptible(dev->read_wait,
								 (!dev->present
								  || (read_idx =
								      read_index
								      (dev)) !=
								  -1));
				if (r) {
					//we were interrupted by a signal
					return -ERESTART;
				}
				if (!dev->present) {
					//The device was unplugged
					return -ENODEV;
				}
				if (read_idx == -1) {
					// Can this happen ???
					return 0;
				}
			}
		}

		offset = read_idx * (dev->report_size + 1);
		if (copy_to_user(buffer, dev->read_queue + offset, count)) {
			return -EFAULT;
		}
	} while (atomic_read(&dev->overflow_flag));

	read_idx = ++read_idx == MAX_INTERRUPT_BUFFER ? 0 : read_idx;
	atomic_set(&dev->read_idx, read_idx);
	return count;
}

/*
 * iowarrior_write
 */
static ssize_t iowarrior_write(struct file *file,
			       const char __user *user_buffer,
			       size_t count, loff_t *ppos)
{
	struct iowarrior *dev;
	int retval = 0;
	char *buf = NULL;	/* for IOW24 and IOW56 we need a buffer */
	struct urb *int_out_urb = NULL;

	dev = file->private_data;

	mutex_lock(&dev->mutex);
	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		retval = -ENODEV;
		goto exit;
	}
	dev_dbg(&dev->interface->dev, "minor %d, count = %zd\n",
		dev->minor, count);
	/* if count is 0 we're already done */
	if (count == 0) {
		retval = 0;
		goto exit;
	}
	/* We only accept full reports */
	if (count != dev->report_size) {
		retval = -EINVAL;
		goto exit;
	}
	switch (dev->product_id) {
	case USB_DEVICE_ID_CODEMERCS_IOW24:
	case USB_DEVICE_ID_CODEMERCS_IOWPV1:
	case USB_DEVICE_ID_CODEMERCS_IOWPV2:
	case USB_DEVICE_ID_CODEMERCS_IOW40:
		/* IOW24 and IOW40 use a synchronous call */
		buf = memdup_user(user_buffer, count);
		if (IS_ERR(buf)) {
			retval = PTR_ERR(buf);
			goto exit;
		}
		retval = usb_set_report(dev->interface, 2, 0, buf, count);
		kfree(buf);
		goto exit;
		break;
	case USB_DEVICE_ID_CODEMERCS_IOW56:
		/* The IOW56 uses asynchronous IO and more urbs */
		if (atomic_read(&dev->write_busy) == MAX_WRITES_IN_FLIGHT) {
			/* Wait until we are below the limit for submitted urbs */
			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				goto exit;
			} else {
				retval = wait_event_interruptible(dev->write_wait,
								  (!dev->present || (atomic_read (&dev-> write_busy) < MAX_WRITES_IN_FLIGHT)));
				if (retval) {
					/* we were interrupted by a signal */
					retval = -ERESTART;
					goto exit;
				}
				if (!dev->present) {
					/* The device was unplugged */
					retval = -ENODEV;
					goto exit;
				}
				if (!dev->opened) {
					/* We were closed while waiting for an URB */
					retval = -ENODEV;
					goto exit;
				}
			}
		}
		atomic_inc(&dev->write_busy);
		int_out_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!int_out_urb) {
			retval = -ENOMEM;
			goto error_no_urb;
		}
		buf = usb_alloc_coherent(dev->udev, dev->report_size,
					 GFP_KERNEL, &int_out_urb->transfer_dma);
		if (!buf) {
			retval = -ENOMEM;
			dev_dbg(&dev->interface->dev,
				"Unable to allocate buffer\n");
			goto error_no_buffer;
		}
		usb_fill_int_urb(int_out_urb, dev->udev,
				 usb_sndintpipe(dev->udev,
						dev->int_out_endpoint->bEndpointAddress),
				 buf, dev->report_size,
				 iowarrior_write_callback, dev,
				 dev->int_out_endpoint->bInterval);
		int_out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		if (copy_from_user(buf, user_buffer, count)) {
			retval = -EFAULT;
			goto error;
		}
		retval = usb_submit_urb(int_out_urb, GFP_KERNEL);
		if (retval) {
			dev_dbg(&dev->interface->dev,
				"submit error %d for urb nr.%d\n",
				retval, atomic_read(&dev->write_busy));
			goto error;
		}
		/* submit was ok */
		retval = count;
		usb_free_urb(int_out_urb);
		goto exit;
		break;
	default:
		/* what do we have here ? An unsupported Product-ID ? */
		dev_err(&dev->interface->dev, "%s - not supported for product=0x%x\n",
			__func__, dev->product_id);
		retval = -EFAULT;
		goto exit;
		break;
	}
error:
	usb_free_coherent(dev->udev, dev->report_size, buf,
			  int_out_urb->transfer_dma);
error_no_buffer:
	usb_free_urb(int_out_urb);
error_no_urb:
	atomic_dec(&dev->write_busy);
	wake_up_interruptible(&dev->write_wait);
exit:
	mutex_unlock(&dev->mutex);
	return retval;
}

/**
 *	iowarrior_ioctl
 */
static long iowarrior_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct iowarrior *dev = NULL;
	__u8 *buffer;
	__u8 __user *user_buffer;
	int retval;
	int io_res;		/* checks for bytes read/written and copy_to/from_user results */

	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	buffer = kzalloc(dev->report_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* lock this object */
	mutex_lock(&iowarrior_mutex);
	mutex_lock(&dev->mutex);

	/* verify that the device wasn't unplugged */
	if (!dev->present) {
		retval = -ENODEV;
		goto error_out;
	}

	dev_dbg(&dev->interface->dev, "minor %d, cmd 0x%.4x, arg %ld\n",
		dev->minor, cmd, arg);

	retval = 0;
	io_res = 0;
	switch (cmd) {
	case IOW_WRITE:
		if (dev->product_id == USB_DEVICE_ID_CODEMERCS_IOW24 ||
		    dev->product_id == USB_DEVICE_ID_CODEMERCS_IOWPV1 ||
		    dev->product_id == USB_DEVICE_ID_CODEMERCS_IOWPV2 ||
		    dev->product_id == USB_DEVICE_ID_CODEMERCS_IOW40) {
			user_buffer = (__u8 __user *)arg;
			io_res = copy_from_user(buffer, user_buffer,
						dev->report_size);
			if (io_res) {
				retval = -EFAULT;
			} else {
				io_res = usb_set_report(dev->interface, 2, 0,
							buffer,
							dev->report_size);
				if (io_res < 0)
					retval = io_res;
			}
		} else {
			retval = -EINVAL;
			dev_err(&dev->interface->dev,
				"ioctl 'IOW_WRITE' is not supported for product=0x%x.\n",
				dev->product_id);
		}
		break;
	case IOW_READ:
		user_buffer = (__u8 __user *)arg;
		io_res = usb_get_report(dev->udev,
					dev->interface->cur_altsetting, 1, 0,
					buffer, dev->report_size);
		if (io_res < 0)
			retval = io_res;
		else {
			io_res = copy_to_user(user_buffer, buffer, dev->report_size);
			if (io_res)
				retval = -EFAULT;
		}
		break;
	case IOW_GETINFO:
		{
			/* Report available information for the device */
			struct iowarrior_info info;
			/* needed for power consumption */
			struct usb_config_descriptor *cfg_descriptor = &dev->udev->actconfig->desc;

			memset(&info, 0, sizeof(info));
			/* directly from the descriptor */
			info.vendor = le16_to_cpu(dev->udev->descriptor.idVendor);
			info.product = dev->product_id;
			info.revision = le16_to_cpu(dev->udev->descriptor.bcdDevice);

			/* 0==UNKNOWN, 1==LOW(usb1.1) ,2=FULL(usb1.1), 3=HIGH(usb2.0) */
			info.speed = dev->udev->speed;
			info.if_num = dev->interface->cur_altsetting->desc.bInterfaceNumber;
			info.report_size = dev->report_size;

			/* serial number string has been read earlier 8 chars or empty string */
			memcpy(info.serial, dev->chip_serial,
			       sizeof(dev->chip_serial));
			if (cfg_descriptor == NULL) {
				info.power = -1;	/* no information available */
			} else {
				/* the MaxPower is stored in units of 2mA to make it fit into a byte-value */
				info.power = cfg_descriptor->bMaxPower * 2;
			}
			io_res = copy_to_user((struct iowarrior_info __user *)arg, &info,
					 sizeof(struct iowarrior_info));
			if (io_res)
				retval = -EFAULT;
			break;
		}
	default:
		/* return that we did not understand this ioctl call */
		retval = -ENOTTY;
		break;
	}
error_out:
	/* unlock the device */
	mutex_unlock(&dev->mutex);
	mutex_unlock(&iowarrior_mutex);
	kfree(buffer);
	return retval;
}

/**
 *	iowarrior_open
 */
static int iowarrior_open(struct inode *inode, struct file *file)
{
	struct iowarrior *dev = NULL;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	mutex_lock(&iowarrior_mutex);
	subminor = iminor(inode);

	interface = usb_find_interface(&iowarrior_driver, subminor);
	if (!interface) {
		mutex_unlock(&iowarrior_mutex);
		printk(KERN_ERR "%s - error, can't find device for minor %d\n",
		       __func__, subminor);
		return -ENODEV;
	}

	mutex_lock(&iowarrior_open_disc_lock);
	dev = usb_get_intfdata(interface);
	if (!dev) {
		mutex_unlock(&iowarrior_open_disc_lock);
		mutex_unlock(&iowarrior_mutex);
		return -ENODEV;
	}

	mutex_lock(&dev->mutex);
	mutex_unlock(&iowarrior_open_disc_lock);

	/* Only one process can open each device, no sharing. */
	if (dev->opened) {
		retval = -EBUSY;
		goto out;
	}

	/* setup interrupt handler for receiving values */
	if ((retval = usb_submit_urb(dev->int_in_urb, GFP_KERNEL)) < 0) {
		dev_err(&interface->dev, "Error %d while submitting URB\n", retval);
		retval = -EFAULT;
		goto out;
	}
	/* increment our usage count for the driver */
	++dev->opened;
	/* save our object in the file's private structure */
	file->private_data = dev;
	retval = 0;

out:
	mutex_unlock(&dev->mutex);
	mutex_unlock(&iowarrior_mutex);
	return retval;
}

/**
 *	iowarrior_release
 */
static int iowarrior_release(struct inode *inode, struct file *file)
{
	struct iowarrior *dev;
	int retval = 0;

	dev = file->private_data;
	if (!dev)
		return -ENODEV;

	dev_dbg(&dev->interface->dev, "minor %d\n", dev->minor);

	/* lock our device */
	mutex_lock(&dev->mutex);

	if (dev->opened <= 0) {
		retval = -ENODEV;	/* close called more than once */
		mutex_unlock(&dev->mutex);
	} else {
		dev->opened = 0;	/* we're closing now */
		retval = 0;
		if (dev->present) {
			/*
			   The device is still connected so we only shutdown
			   pending read-/write-ops.
			 */
			usb_kill_urb(dev->int_in_urb);
			wake_up_interruptible(&dev->read_wait);
			wake_up_interruptible(&dev->write_wait);
			mutex_unlock(&dev->mutex);
		} else {
			/* The device was unplugged, cleanup resources */
			mutex_unlock(&dev->mutex);
			iowarrior_delete(dev);
		}
	}
	return retval;
}

static __poll_t iowarrior_poll(struct file *file, poll_table * wait)
{
	struct iowarrior *dev = file->private_data;
	__poll_t mask = 0;

	if (!dev->present)
		return EPOLLERR | EPOLLHUP;

	poll_wait(file, &dev->read_wait, wait);
	poll_wait(file, &dev->write_wait, wait);

	if (!dev->present)
		return EPOLLERR | EPOLLHUP;

	if (read_index(dev) != -1)
		mask |= EPOLLIN | EPOLLRDNORM;

	if (atomic_read(&dev->write_busy) < MAX_WRITES_IN_FLIGHT)
		mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}

/*
 * File operations needed when we register this driver.
 * This assumes that this driver NEEDS file operations,
 * of course, which means that the driver is expected
 * to have a node in the /dev directory. If the USB
 * device were for a network interface then the driver
 * would use "struct net_driver" instead, and a serial
 * device would use "struct tty_driver".
 */
static const struct file_operations iowarrior_fops = {
	.owner = THIS_MODULE,
	.write = iowarrior_write,
	.read = iowarrior_read,
	.unlocked_ioctl = iowarrior_ioctl,
	.open = iowarrior_open,
	.release = iowarrior_release,
	.poll = iowarrior_poll,
	.llseek = noop_llseek,
};

static char *iowarrior_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "usb/%s", dev_name(dev));
}

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver iowarrior_class = {
	.name = "iowarrior%d",
	.devnode = iowarrior_devnode,
	.fops = &iowarrior_fops,
	.minor_base = IOWARRIOR_MINOR_BASE,
};

/*---------------------------------*/
/*  probe and disconnect functions */
/*---------------------------------*/
/**
 *	iowarrior_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int iowarrior_probe(struct usb_interface *interface,
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct iowarrior *dev = NULL;
	struct usb_host_interface *iface_desc;
	int retval = -ENOMEM;
	int res;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(struct iowarrior), GFP_KERNEL);
	if (!dev)
		return retval;

	mutex_init(&dev->mutex);

	atomic_set(&dev->intr_idx, 0);
	atomic_set(&dev->read_idx, 0);
	atomic_set(&dev->overflow_flag, 0);
	init_waitqueue_head(&dev->read_wait);
	atomic_set(&dev->write_busy, 0);
	init_waitqueue_head(&dev->write_wait);

	dev->udev = udev;
	dev->interface = interface;

	iface_desc = interface->cur_altsetting;
	dev->product_id = le16_to_cpu(udev->descriptor.idProduct);

	res = usb_find_last_int_in_endpoint(iface_desc, &dev->int_in_endpoint);
	if (res) {
		dev_err(&interface->dev, "no interrupt-in endpoint found\n");
		retval = res;
		goto error;
	}

	if (dev->product_id == USB_DEVICE_ID_CODEMERCS_IOW56) {
		res = usb_find_last_int_out_endpoint(iface_desc,
				&dev->int_out_endpoint);
		if (res) {
			dev_err(&interface->dev, "no interrupt-out endpoint found\n");
			retval = res;
			goto error;
		}
	}

	/* we have to check the report_size often, so remember it in the endianness suitable for our machine */
	dev->report_size = usb_endpoint_maxp(dev->int_in_endpoint);
	if ((dev->interface->cur_altsetting->desc.bInterfaceNumber == 0) &&
	    (dev->product_id == USB_DEVICE_ID_CODEMERCS_IOW56))
		/* IOWarrior56 has wMaxPacketSize different from report size */
		dev->report_size = 7;

	/* create the urb and buffer for reading */
	dev->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->int_in_urb)
		goto error;
	dev->int_in_buffer = kmalloc(dev->report_size, GFP_KERNEL);
	if (!dev->int_in_buffer)
		goto error;
	usb_fill_int_urb(dev->int_in_urb, dev->udev,
			 usb_rcvintpipe(dev->udev,
					dev->int_in_endpoint->bEndpointAddress),
			 dev->int_in_buffer, dev->report_size,
			 iowarrior_callback, dev,
			 dev->int_in_endpoint->bInterval);
	/* create an internal buffer for interrupt data from the device */
	dev->read_queue =
	    kmalloc_array(dev->report_size + 1, MAX_INTERRUPT_BUFFER,
			  GFP_KERNEL);
	if (!dev->read_queue)
		goto error;
	/* Get the serial-number of the chip */
	memset(dev->chip_serial, 0x00, sizeof(dev->chip_serial));
	usb_string(udev, udev->descriptor.iSerialNumber, dev->chip_serial,
		   sizeof(dev->chip_serial));
	if (strlen(dev->chip_serial) != 8)
		memset(dev->chip_serial, 0x00, sizeof(dev->chip_serial));

	/* Set the idle timeout to 0, if this is interface 0 */
	if (dev->interface->cur_altsetting->desc.bInterfaceNumber == 0) {
	    usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			    0x0A,
			    USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0,
			    0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	}
	/* allow device read and ioctl */
	dev->present = 1;

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, dev);

	retval = usb_register_dev(interface, &iowarrior_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev, "Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	dev->minor = interface->minor;

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev, "IOWarrior product=0x%x, serial=%s interface=%d "
		 "now attached to iowarrior%d\n", dev->product_id, dev->chip_serial,
		 iface_desc->desc.bInterfaceNumber, dev->minor - IOWARRIOR_MINOR_BASE);
	return retval;

error:
	iowarrior_delete(dev);
	return retval;
}

/**
 *	iowarrior_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 */
static void iowarrior_disconnect(struct usb_interface *interface)
{
	struct iowarrior *dev;
	int minor;

	dev = usb_get_intfdata(interface);
	mutex_lock(&iowarrior_open_disc_lock);
	usb_set_intfdata(interface, NULL);

	minor = dev->minor;

	/* give back our minor */
	usb_deregister_dev(interface, &iowarrior_class);

	mutex_lock(&dev->mutex);

	/* prevent device read, write and ioctl */
	dev->present = 0;

	mutex_unlock(&dev->mutex);
	mutex_unlock(&iowarrior_open_disc_lock);

	if (dev->opened) {
		/* There is a process that holds a filedescriptor to the device ,
		   so we only shutdown read-/write-ops going on.
		   Deleting the device is postponed until close() was called.
		 */
		usb_kill_urb(dev->int_in_urb);
		wake_up_interruptible(&dev->read_wait);
		wake_up_interruptible(&dev->write_wait);
	} else {
		/* no process is using the device, cleanup now */
		iowarrior_delete(dev);
	}

	dev_info(&interface->dev, "I/O-Warror #%d now disconnected\n",
		 minor - IOWARRIOR_MINOR_BASE);
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver iowarrior_driver = {
	.name = "iowarrior",
	.probe = iowarrior_probe,
	.disconnect = iowarrior_disconnect,
	.id_table = iowarrior_ids,
};

module_usb_driver(iowarrior_driver);
