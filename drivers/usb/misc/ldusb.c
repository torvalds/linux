/**
 * Generic USB driver for report based interrupt in/out devices
 * like LD Didactic's USB devices. LD Didactic's USB devices are
 * HID devices which do not use HID report definitons (they use
 * raw interrupt in and our reports only for communication).
 *
 * This driver uses a ring buffer for time critical reading of
 * interrupt in reports and provides read and write methods for
 * raw interrupt reports (similar to the Windows HID driver).
 * Devices based on the book USB COMPLETE by Jan Axelson may need
 * such a compatibility to the Windows HID driver.
 *
 * Copyright (C) 2005 Michael Hund <mhund@ld-didactic.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 * Derived from Lego USB Tower driver
 * Copyright (C) 2003 David Glance <advidgsf@sourceforge.net>
 *		 2001-2004 Juergen Stuber <starblue@users.sourceforge.net>
 *
 * V0.1  (mh) Initial version
 * V0.11 (mh) Added raw support for HID 1.0 devices (no interrupt out endpoint)
 * V0.12 (mh) Added kmalloc check for string buffer
 * V0.13 (mh) Added support for LD X-Ray and Machine Test System
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/poll.h>

/* Define these values to match your devices */
#define USB_VENDOR_ID_LD		0x0f11	/* USB Vendor ID of LD Didactic GmbH */
#define USB_DEVICE_ID_LD_CASSY		0x1000	/* USB Product ID of CASSY-S */
#define USB_DEVICE_ID_LD_POCKETCASSY	0x1010	/* USB Product ID of Pocket-CASSY */
#define USB_DEVICE_ID_LD_MOBILECASSY	0x1020	/* USB Product ID of Mobile-CASSY */
#define USB_DEVICE_ID_LD_JWM		0x1080	/* USB Product ID of Joule and Wattmeter */
#define USB_DEVICE_ID_LD_DMMP		0x1081	/* USB Product ID of Digital Multimeter P (reserved) */
#define USB_DEVICE_ID_LD_UMIP		0x1090	/* USB Product ID of UMI P */
#define USB_DEVICE_ID_LD_XRAY1		0x1100	/* USB Product ID of X-Ray Apparatus */
#define USB_DEVICE_ID_LD_XRAY2		0x1101	/* USB Product ID of X-Ray Apparatus */
#define USB_DEVICE_ID_LD_VIDEOCOM	0x1200	/* USB Product ID of VideoCom */
#define USB_DEVICE_ID_LD_COM3LAB	0x2000	/* USB Product ID of COM3LAB */
#define USB_DEVICE_ID_LD_TELEPORT	0x2010	/* USB Product ID of Terminal Adapter */
#define USB_DEVICE_ID_LD_NETWORKANALYSER 0x2020	/* USB Product ID of Network Analyser */
#define USB_DEVICE_ID_LD_POWERCONTROL	0x2030	/* USB Product ID of Converter Control Unit */
#define USB_DEVICE_ID_LD_MACHINETEST	0x2040	/* USB Product ID of Machine Test System */

#define USB_VENDOR_ID_VERNIER		0x08f7
#define USB_DEVICE_ID_VERNIER_LABPRO	0x0001
#define USB_DEVICE_ID_VERNIER_GOTEMP	0x0002
#define USB_DEVICE_ID_VERNIER_SKIP	0x0003
#define USB_DEVICE_ID_VERNIER_CYCLOPS	0x0004
#define USB_DEVICE_ID_VERNIER_LCSPEC	0x0006

#define USB_VENDOR_ID_MICROCHIP		0x04d8
#define USB_DEVICE_ID_PICDEM		0x000c

#ifdef CONFIG_USB_DYNAMIC_MINORS
#define USB_LD_MINOR_BASE	0
#else
#define USB_LD_MINOR_BASE	176
#endif

/* table of devices that work with this driver */
static struct usb_device_id ld_usb_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_CASSY) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_POCKETCASSY) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_MOBILECASSY) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_JWM) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_DMMP) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_UMIP) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_XRAY1) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_XRAY2) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_VIDEOCOM) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_COM3LAB) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_TELEPORT) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_NETWORKANALYSER) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_POWERCONTROL) },
	{ USB_DEVICE(USB_VENDOR_ID_LD, USB_DEVICE_ID_LD_MACHINETEST) },
	{ USB_DEVICE(USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_LABPRO) },
	{ USB_DEVICE(USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_GOTEMP) },
	{ USB_DEVICE(USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_SKIP) },
	{ USB_DEVICE(USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_CYCLOPS) },
	{ USB_DEVICE(USB_VENDOR_ID_MICROCHIP, USB_DEVICE_ID_PICDEM) },
	{ USB_DEVICE(USB_VENDOR_ID_VERNIER, USB_DEVICE_ID_VERNIER_LCSPEC) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ld_usb_table);
MODULE_VERSION("V0.13");
MODULE_AUTHOR("Michael Hund <mhund@ld-didactic.de>");
MODULE_DESCRIPTION("LD USB Driver");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("LD USB Devices");

#ifdef CONFIG_USB_DEBUG
	static int debug = 1;
#else
	static int debug = 0;
#endif

/* Use our own dbg macro */
#define dbg_info(dev, format, arg...) do { if (debug) dev_info(dev , format , ## arg); } while (0)

/* Module parameters */
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

/* All interrupt in transfers are collected in a ring buffer to
 * avoid racing conditions and get better performance of the driver.
 */
static int ring_buffer_size = 128;
module_param(ring_buffer_size, int, 0);
MODULE_PARM_DESC(ring_buffer_size, "Read ring buffer size in reports");

/* The write_buffer can contain more than one interrupt out transfer.
 */
static int write_buffer_size = 10;
module_param(write_buffer_size, int, 0);
MODULE_PARM_DESC(write_buffer_size, "Write buffer size in reports");

/* As of kernel version 2.6.4 ehci-hcd uses an
 * "only one interrupt transfer per frame" shortcut
 * to simplify the scheduling of periodic transfers.
 * This conflicts with our standard 1ms intervals for in and out URBs.
 * We use default intervals of 2ms for in and 2ms for out transfers,
 * which should be fast enough.
 * Increase the interval to allow more devices that do interrupt transfers,
 * or set to 1 to use the standard interval from the endpoint descriptors.
 */
static int min_interrupt_in_interval = 2;
module_param(min_interrupt_in_interval, int, 0);
MODULE_PARM_DESC(min_interrupt_in_interval, "Minimum interrupt in interval in ms");

static int min_interrupt_out_interval = 2;
module_param(min_interrupt_out_interval, int, 0);
MODULE_PARM_DESC(min_interrupt_out_interval, "Minimum interrupt out interval in ms");

/* Structure to hold all of our device specific stuff */
struct ld_usb {
	struct semaphore	sem;		/* locks this structure */
	struct usb_interface*	intf;		/* save off the usb interface pointer */

	int			open_count;	/* number of times this port has been opened */

	char*			ring_buffer;
	unsigned int		ring_head;
	unsigned int		ring_tail;

	wait_queue_head_t	read_wait;
	wait_queue_head_t	write_wait;

	char*			interrupt_in_buffer;
	struct usb_endpoint_descriptor* interrupt_in_endpoint;
	struct urb*		interrupt_in_urb;
	int			interrupt_in_interval;
	size_t			interrupt_in_endpoint_size;
	int			interrupt_in_running;
	int			interrupt_in_done;
	int			buffer_overflow;
	spinlock_t		rbsl;

	char*			interrupt_out_buffer;
	struct usb_endpoint_descriptor* interrupt_out_endpoint;
	struct urb*		interrupt_out_urb;
	int			interrupt_out_interval;
	size_t			interrupt_out_endpoint_size;
	int			interrupt_out_busy;
};

static struct usb_driver ld_usb_driver;

/**
 *	ld_usb_abort_transfers
 *      aborts transfers and frees associated data structures
 */
static void ld_usb_abort_transfers(struct ld_usb *dev)
{
	/* shutdown transfer */
	if (dev->interrupt_in_running) {
		dev->interrupt_in_running = 0;
		if (dev->intf)
			usb_kill_urb(dev->interrupt_in_urb);
	}
	if (dev->interrupt_out_busy)
		if (dev->intf)
			usb_kill_urb(dev->interrupt_out_urb);
}

/**
 *	ld_usb_delete
 */
static void ld_usb_delete(struct ld_usb *dev)
{
	ld_usb_abort_transfers(dev);

	/* free data structures */
	usb_free_urb(dev->interrupt_in_urb);
	usb_free_urb(dev->interrupt_out_urb);
	kfree(dev->ring_buffer);
	kfree(dev->interrupt_in_buffer);
	kfree(dev->interrupt_out_buffer);
	kfree(dev);
}

/**
 *	ld_usb_interrupt_in_callback
 */
static void ld_usb_interrupt_in_callback(struct urb *urb)
{
	struct ld_usb *dev = urb->context;
	size_t *actual_buffer;
	unsigned int next_ring_head;
	int status = urb->status;
	int retval;

	if (status) {
		if (status == -ENOENT ||
		    status == -ECONNRESET ||
		    status == -ESHUTDOWN) {
			goto exit;
		} else {
			dbg_info(&dev->intf->dev, "%s: nonzero status received: %d\n",
				 __FUNCTION__, status);
			spin_lock(&dev->rbsl);
			goto resubmit; /* maybe we can recover */
		}
	}

	spin_lock(&dev->rbsl);
	if (urb->actual_length > 0) {
		next_ring_head = (dev->ring_head+1) % ring_buffer_size;
		if (next_ring_head != dev->ring_tail) {
			actual_buffer = (size_t*)(dev->ring_buffer + dev->ring_head*(sizeof(size_t)+dev->interrupt_in_endpoint_size));
			/* actual_buffer gets urb->actual_length + interrupt_in_buffer */
			*actual_buffer = urb->actual_length;
			memcpy(actual_buffer+1, dev->interrupt_in_buffer, urb->actual_length);
			dev->ring_head = next_ring_head;
			dbg_info(&dev->intf->dev, "%s: received %d bytes\n",
				 __FUNCTION__, urb->actual_length);
		} else {
			dev_warn(&dev->intf->dev,
				 "Ring buffer overflow, %d bytes dropped\n",
				 urb->actual_length);
			dev->buffer_overflow = 1;
		}
	}

resubmit:
	/* resubmit if we're still running */
	if (dev->interrupt_in_running && !dev->buffer_overflow && dev->intf) {
		retval = usb_submit_urb(dev->interrupt_in_urb, GFP_ATOMIC);
		if (retval) {
			dev_err(&dev->intf->dev,
				"usb_submit_urb failed (%d)\n", retval);
			dev->buffer_overflow = 1;
		}
	}
	spin_unlock(&dev->rbsl);
exit:
	dev->interrupt_in_done = 1;
	wake_up_interruptible(&dev->read_wait);
}

/**
 *	ld_usb_interrupt_out_callback
 */
static void ld_usb_interrupt_out_callback(struct urb *urb)
{
	struct ld_usb *dev = urb->context;
	int status = urb->status;

	/* sync/async unlink faults aren't errors */
	if (status && !(status == -ENOENT ||
			status == -ECONNRESET ||
			status == -ESHUTDOWN))
		dbg_info(&dev->intf->dev,
			 "%s - nonzero write interrupt status received: %d\n",
			 __FUNCTION__, status);

	dev->interrupt_out_busy = 0;
	wake_up_interruptible(&dev->write_wait);
}

/**
 *	ld_usb_open
 */
static int ld_usb_open(struct inode *inode, struct file *file)
{
	struct ld_usb *dev;
	int subminor;
	int retval;
	struct usb_interface *interface;

	nonseekable_open(inode, file);
	subminor = iminor(inode);

	interface = usb_find_interface(&ld_usb_driver, subminor);

	if (!interface) {
		err("%s - error, can't find device for minor %d\n",
		     __FUNCTION__, subminor);
		return -ENODEV;
	}

	dev = usb_get_intfdata(interface);

	if (!dev)
		return -ENODEV;

	/* lock this device */
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* allow opening only once */
	if (dev->open_count) {
		retval = -EBUSY;
		goto unlock_exit;
	}
	dev->open_count = 1;

	/* initialize in direction */
	dev->ring_head = 0;
	dev->ring_tail = 0;
	dev->buffer_overflow = 0;
	usb_fill_int_urb(dev->interrupt_in_urb,
			 interface_to_usbdev(interface),
			 usb_rcvintpipe(interface_to_usbdev(interface),
					dev->interrupt_in_endpoint->bEndpointAddress),
			 dev->interrupt_in_buffer,
			 dev->interrupt_in_endpoint_size,
			 ld_usb_interrupt_in_callback,
			 dev,
			 dev->interrupt_in_interval);

	dev->interrupt_in_running = 1;
	dev->interrupt_in_done = 0;

	retval = usb_submit_urb(dev->interrupt_in_urb, GFP_KERNEL);
	if (retval) {
		dev_err(&interface->dev, "Couldn't submit interrupt_in_urb %d\n", retval);
		dev->interrupt_in_running = 0;
		dev->open_count = 0;
		goto unlock_exit;
	}

	/* save device in the file's private structure */
	file->private_data = dev;

unlock_exit:
	up(&dev->sem);

	return retval;
}

/**
 *	ld_usb_release
 */
static int ld_usb_release(struct inode *inode, struct file *file)
{
	struct ld_usb *dev;
	int retval = 0;

	dev = file->private_data;

	if (dev == NULL) {
		retval = -ENODEV;
		goto exit;
	}

	if (down_interruptible(&dev->sem)) {
		retval = -ERESTARTSYS;
		goto exit;
	}

	if (dev->open_count != 1) {
		retval = -ENODEV;
		goto unlock_exit;
	}
	if (dev->intf == NULL) {
		/* the device was unplugged before the file was released */
		up(&dev->sem);
		/* unlock here as ld_usb_delete frees dev */
		ld_usb_delete(dev);
		goto exit;
	}

	/* wait until write transfer is finished */
	if (dev->interrupt_out_busy)
		wait_event_interruptible_timeout(dev->write_wait, !dev->interrupt_out_busy, 2 * HZ);
	ld_usb_abort_transfers(dev);
	dev->open_count = 0;

unlock_exit:
	up(&dev->sem);

exit:
	return retval;
}

/**
 *	ld_usb_poll
 */
static unsigned int ld_usb_poll(struct file *file, poll_table *wait)
{
	struct ld_usb *dev;
	unsigned int mask = 0;

	dev = file->private_data;

	poll_wait(file, &dev->read_wait, wait);
	poll_wait(file, &dev->write_wait, wait);

	if (dev->ring_head != dev->ring_tail)
		mask |= POLLIN | POLLRDNORM;
	if (!dev->interrupt_out_busy)
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

/**
 *	ld_usb_read
 */
static ssize_t ld_usb_read(struct file *file, char __user *buffer, size_t count,
			   loff_t *ppos)
{
	struct ld_usb *dev;
	size_t *actual_buffer;
	size_t bytes_to_read;
	int retval = 0;
	int rv;

	dev = file->private_data;

	/* verify that we actually have some data to read */
	if (count == 0)
		goto exit;

	/* lock this object */
	if (down_interruptible(&dev->sem)) {
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* verify that the device wasn't unplugged */
	if (dev->intf == NULL) {
		retval = -ENODEV;
		err("No device or device unplugged %d\n", retval);
		goto unlock_exit;
	}

	/* wait for data */
	spin_lock_irq(&dev->rbsl);
	if (dev->ring_head == dev->ring_tail) {
		dev->interrupt_in_done = 0;
		spin_unlock_irq(&dev->rbsl);
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto unlock_exit;
		}
		retval = wait_event_interruptible(dev->read_wait, dev->interrupt_in_done);
		if (retval < 0)
			goto unlock_exit;
	} else {
		spin_unlock_irq(&dev->rbsl);
	}

	/* actual_buffer contains actual_length + interrupt_in_buffer */
	actual_buffer = (size_t*)(dev->ring_buffer + dev->ring_tail*(sizeof(size_t)+dev->interrupt_in_endpoint_size));
	bytes_to_read = min(count, *actual_buffer);
	if (bytes_to_read < *actual_buffer)
		dev_warn(&dev->intf->dev, "Read buffer overflow, %zd bytes dropped\n",
			 *actual_buffer-bytes_to_read);

	/* copy one interrupt_in_buffer from ring_buffer into userspace */
	if (copy_to_user(buffer, actual_buffer+1, bytes_to_read)) {
		retval = -EFAULT;
		goto unlock_exit;
	}
	dev->ring_tail = (dev->ring_tail+1) % ring_buffer_size;

	retval = bytes_to_read;

	spin_lock_irq(&dev->rbsl);
	if (dev->buffer_overflow) {
		dev->buffer_overflow = 0;
		spin_unlock_irq(&dev->rbsl);
		rv = usb_submit_urb(dev->interrupt_in_urb, GFP_KERNEL);
		if (rv < 0)
			dev->buffer_overflow = 1;
	} else {
		spin_unlock_irq(&dev->rbsl);
	}

unlock_exit:
	/* unlock the device */
	up(&dev->sem);

exit:
	return retval;
}

/**
 *	ld_usb_write
 */
static ssize_t ld_usb_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	struct ld_usb *dev;
	size_t bytes_to_write;
	int retval = 0;

	dev = file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/* lock this object */
	if (down_interruptible(&dev->sem)) {
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* verify that the device wasn't unplugged */
	if (dev->intf == NULL) {
		retval = -ENODEV;
		err("No device or device unplugged %d\n", retval);
		goto unlock_exit;
	}

	/* wait until previous transfer is finished */
	if (dev->interrupt_out_busy) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto unlock_exit;
		}
		retval = wait_event_interruptible(dev->write_wait, !dev->interrupt_out_busy);
		if (retval < 0) {
			goto unlock_exit;
		}
	}

	/* write the data into interrupt_out_buffer from userspace */
	bytes_to_write = min(count, write_buffer_size*dev->interrupt_out_endpoint_size);
	if (bytes_to_write < count)
		dev_warn(&dev->intf->dev, "Write buffer overflow, %zd bytes dropped\n",count-bytes_to_write);
	dbg_info(&dev->intf->dev, "%s: count = %zd, bytes_to_write = %zd\n", __FUNCTION__, count, bytes_to_write);

	if (copy_from_user(dev->interrupt_out_buffer, buffer, bytes_to_write)) {
		retval = -EFAULT;
		goto unlock_exit;
	}

	if (dev->interrupt_out_endpoint == NULL) {
		/* try HID_REQ_SET_REPORT=9 on control_endpoint instead of interrupt_out_endpoint */
		retval = usb_control_msg(interface_to_usbdev(dev->intf),
					 usb_sndctrlpipe(interface_to_usbdev(dev->intf), 0),
					 9,
					 USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
					 1 << 8, 0,
					 dev->interrupt_out_buffer,
					 bytes_to_write,
					 USB_CTRL_SET_TIMEOUT * HZ);
		if (retval < 0)
			err("Couldn't submit HID_REQ_SET_REPORT %d\n", retval);
		goto unlock_exit;
	}

	/* send off the urb */
	usb_fill_int_urb(dev->interrupt_out_urb,
			 interface_to_usbdev(dev->intf),
			 usb_sndintpipe(interface_to_usbdev(dev->intf),
					dev->interrupt_out_endpoint->bEndpointAddress),
			 dev->interrupt_out_buffer,
			 bytes_to_write,
			 ld_usb_interrupt_out_callback,
			 dev,
			 dev->interrupt_out_interval);

	dev->interrupt_out_busy = 1;
	wmb();

	retval = usb_submit_urb(dev->interrupt_out_urb, GFP_KERNEL);
	if (retval) {
		dev->interrupt_out_busy = 0;
		err("Couldn't submit interrupt_out_urb %d\n", retval);
		goto unlock_exit;
	}
	retval = bytes_to_write;

unlock_exit:
	/* unlock the device */
	up(&dev->sem);

exit:
	return retval;
}

/* file operations needed when we register this driver */
static const struct file_operations ld_usb_fops = {
	.owner =	THIS_MODULE,
	.read  =	ld_usb_read,
	.write =	ld_usb_write,
	.open =		ld_usb_open,
	.release =	ld_usb_release,
	.poll =		ld_usb_poll,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver ld_usb_class = {
	.name =		"ldusb%d",
	.fops =		&ld_usb_fops,
	.minor_base =	USB_LD_MINOR_BASE,
};

/**
 *	ld_usb_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int ld_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ld_usb *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	char *buffer;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and intialize it */

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&intf->dev, "Out of memory\n");
		goto exit;
	}
	init_MUTEX(&dev->sem);
	spin_lock_init(&dev->rbsl);
	dev->intf = intf;
	init_waitqueue_head(&dev->read_wait);
	init_waitqueue_head(&dev->write_wait);

	/* workaround for early firmware versions on fast computers */
	if ((le16_to_cpu(udev->descriptor.idVendor) == USB_VENDOR_ID_LD) &&
	    ((le16_to_cpu(udev->descriptor.idProduct) == USB_DEVICE_ID_LD_CASSY) ||
	     (le16_to_cpu(udev->descriptor.idProduct) == USB_DEVICE_ID_LD_COM3LAB)) &&
	    (le16_to_cpu(udev->descriptor.bcdDevice) <= 0x103)) {
		buffer = kmalloc(256, GFP_KERNEL);
		if (buffer == NULL) {
			dev_err(&intf->dev, "Couldn't allocate string buffer\n");
			goto error;
		}
		/* usb_string makes SETUP+STALL to leave always ControlReadLoop */
		usb_string(udev, 255, buffer, 256);
		kfree(buffer);
	}

	iface_desc = intf->cur_altsetting;

	/* set up the endpoint information */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint))
			dev->interrupt_in_endpoint = endpoint;

		if (usb_endpoint_is_int_out(endpoint))
			dev->interrupt_out_endpoint = endpoint;
	}
	if (dev->interrupt_in_endpoint == NULL) {
		dev_err(&intf->dev, "Interrupt in endpoint not found\n");
		goto error;
	}
	if (dev->interrupt_out_endpoint == NULL)
		dev_warn(&intf->dev, "Interrupt out endpoint not found (using control endpoint instead)\n");

	dev->interrupt_in_endpoint_size = le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize);
	dev->ring_buffer = kmalloc(ring_buffer_size*(sizeof(size_t)+dev->interrupt_in_endpoint_size), GFP_KERNEL);
	if (!dev->ring_buffer) {
		dev_err(&intf->dev, "Couldn't allocate ring_buffer\n");
		goto error;
	}
	dev->interrupt_in_buffer = kmalloc(dev->interrupt_in_endpoint_size, GFP_KERNEL);
	if (!dev->interrupt_in_buffer) {
		dev_err(&intf->dev, "Couldn't allocate interrupt_in_buffer\n");
		goto error;
	}
	dev->interrupt_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->interrupt_in_urb) {
		dev_err(&intf->dev, "Couldn't allocate interrupt_in_urb\n");
		goto error;
	}
	dev->interrupt_out_endpoint_size = dev->interrupt_out_endpoint ? le16_to_cpu(dev->interrupt_out_endpoint->wMaxPacketSize) :
									 udev->descriptor.bMaxPacketSize0;
	dev->interrupt_out_buffer = kmalloc(write_buffer_size*dev->interrupt_out_endpoint_size, GFP_KERNEL);
	if (!dev->interrupt_out_buffer) {
		dev_err(&intf->dev, "Couldn't allocate interrupt_out_buffer\n");
		goto error;
	}
	dev->interrupt_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->interrupt_out_urb) {
		dev_err(&intf->dev, "Couldn't allocate interrupt_out_urb\n");
		goto error;
	}
	dev->interrupt_in_interval = min_interrupt_in_interval > dev->interrupt_in_endpoint->bInterval ? min_interrupt_in_interval : dev->interrupt_in_endpoint->bInterval;
	if (dev->interrupt_out_endpoint)
		dev->interrupt_out_interval = min_interrupt_out_interval > dev->interrupt_out_endpoint->bInterval ? min_interrupt_out_interval : dev->interrupt_out_endpoint->bInterval;

	/* we can register the device now, as it is ready */
	usb_set_intfdata(intf, dev);

	retval = usb_register_dev(intf, &ld_usb_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&intf->dev, "Not able to get a minor for this device.\n");
		usb_set_intfdata(intf, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&intf->dev, "LD USB Device #%d now attached to major %d minor %d\n",
		(intf->minor - USB_LD_MINOR_BASE), USB_MAJOR, intf->minor);

exit:
	return retval;

error:
	ld_usb_delete(dev);

	return retval;
}

/**
 *	ld_usb_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 */
static void ld_usb_disconnect(struct usb_interface *intf)
{
	struct ld_usb *dev;
	int minor;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	minor = intf->minor;

	/* give back our minor */
	usb_deregister_dev(intf, &ld_usb_class);

	down(&dev->sem);

	/* if the device is not opened, then we clean up right now */
	if (!dev->open_count) {
		up(&dev->sem);
		ld_usb_delete(dev);
	} else {
		dev->intf = NULL;
		up(&dev->sem);
	}

	dev_info(&intf->dev, "LD USB Device #%d now disconnected\n",
		 (minor - USB_LD_MINOR_BASE));
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver ld_usb_driver = {
	.name =		"ldusb",
	.probe =	ld_usb_probe,
	.disconnect =	ld_usb_disconnect,
	.id_table =	ld_usb_table,
};

/**
 *	ld_usb_init
 */
static int __init ld_usb_init(void)
{
	int retval;

	/* register this driver with the USB subsystem */
	retval = usb_register(&ld_usb_driver);
	if (retval)
		err("usb_register failed for the "__FILE__" driver. Error number %d\n", retval);

	return retval;
}

/**
 *	ld_usb_exit
 */
static void __exit ld_usb_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&ld_usb_driver);
}

module_init(ld_usb_init);
module_exit(ld_usb_exit);

