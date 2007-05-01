/*
 * adutux - driver for ADU devices from Ontrak Control Systems
 * This is an experimental driver. Use at your own risk.
 * This driver is not supported by Ontrak Control Systems.
 *
 * Copyright (c) 2003 John Homppi (SCO, leave this notice here)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * derived from the Lego USB Tower driver 0.56:
 * Copyright (c) 2003 David Glance <davidgsf@sourceforge.net>
 *               2001 Juergen Stuber <stuber@loria.fr>
 * that was derived from USB Skeleton driver - 0.5
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <asm/uaccess.h>

#ifdef CONFIG_USB_DEBUG
static int debug = 5;
#else
static int debug = 1;
#endif

/* Use our own dbg macro */
#undef dbg
#define dbg(lvl, format, arg...) 					\
do { 									\
	if (debug >= lvl)						\
		printk(KERN_DEBUG __FILE__ " : " format " \n", ## arg);	\
} while (0)


/* Version Information */
#define DRIVER_VERSION "v0.0.13"
#define DRIVER_AUTHOR "John Homppi"
#define DRIVER_DESC "adutux (see www.ontrak.net)"

/* Module parameters */
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

/* Define these values to match your device */
#define ADU_VENDOR_ID 0x0a07
#define ADU_PRODUCT_ID 0x0064

/* table of devices that work with this driver */
static struct usb_device_id device_table [] = {
	{ USB_DEVICE(ADU_VENDOR_ID, ADU_PRODUCT_ID) },		/* ADU100 */
	{ USB_DEVICE(ADU_VENDOR_ID, ADU_PRODUCT_ID+20) }, 	/* ADU120 */
	{ USB_DEVICE(ADU_VENDOR_ID, ADU_PRODUCT_ID+30) }, 	/* ADU130 */
	{ USB_DEVICE(ADU_VENDOR_ID, ADU_PRODUCT_ID+100) },	/* ADU200 */
	{ USB_DEVICE(ADU_VENDOR_ID, ADU_PRODUCT_ID+108) },	/* ADU208 */
	{ USB_DEVICE(ADU_VENDOR_ID, ADU_PRODUCT_ID+118) },	/* ADU218 */
	{ }/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, device_table);

#ifdef CONFIG_USB_DYNAMIC_MINORS
#define ADU_MINOR_BASE	0
#else
#define ADU_MINOR_BASE	67
#endif

/* we can have up to this number of device plugged in at once */
#define MAX_DEVICES	16

#define COMMAND_TIMEOUT	(2*HZ)	/* 60 second timeout for a command */

/* Structure to hold all of our device specific stuff */
struct adu_device {
	struct semaphore	sem; /* locks this structure */
	struct usb_device*	udev; /* save off the usb device pointer */
	struct usb_interface*	interface;
	unsigned char		minor; /* the starting minor number for this device */
	char			serial_number[8];

	int			open_count; /* number of times this port has been opened */

	char*			read_buffer_primary;
	int			read_buffer_length;
	char*			read_buffer_secondary;
	int			secondary_head;
	int			secondary_tail;
	spinlock_t		buflock;

	wait_queue_head_t	read_wait;
	wait_queue_head_t	write_wait;

	char*			interrupt_in_buffer;
	struct usb_endpoint_descriptor* interrupt_in_endpoint;
	struct urb*		interrupt_in_urb;
	int			read_urb_finished;

	char*			interrupt_out_buffer;
	struct usb_endpoint_descriptor* interrupt_out_endpoint;
	struct urb*		interrupt_out_urb;
};

/* prevent races between open() and disconnect */
static DEFINE_MUTEX(disconnect_mutex);
static struct usb_driver adu_driver;

static void adu_debug_data(int level, const char *function, int size,
			   const unsigned char *data)
{
	int i;

	if (debug < level)
		return;

	printk(KERN_DEBUG __FILE__": %s - length = %d, data = ",
	       function, size);
	for (i = 0; i < size; ++i)
		printk("%.2x ", data[i]);
	printk("\n");
}

/**
 * adu_abort_transfers
 *      aborts transfers and frees associated data structures
 */
static void adu_abort_transfers(struct adu_device *dev)
{
	dbg(2," %s : enter", __FUNCTION__);

	if (dev == NULL) {
		dbg(1," %s : dev is null", __FUNCTION__);
		goto exit;
	}

	if (dev->udev == NULL) {
		dbg(1," %s : udev is null", __FUNCTION__);
		goto exit;
	}

	dbg(2," %s : udev state %d", __FUNCTION__, dev->udev->state);
	if (dev->udev->state == USB_STATE_NOTATTACHED) {
		dbg(1," %s : udev is not attached", __FUNCTION__);
		goto exit;
	}

	/* shutdown transfer */
	usb_unlink_urb(dev->interrupt_in_urb);
	usb_unlink_urb(dev->interrupt_out_urb);

exit:
	dbg(2," %s : leave", __FUNCTION__);
}

static void adu_delete(struct adu_device *dev)
{
	dbg(2, "%s enter", __FUNCTION__);

	adu_abort_transfers(dev);

	/* free data structures */
	usb_free_urb(dev->interrupt_in_urb);
	usb_free_urb(dev->interrupt_out_urb);
	kfree(dev->read_buffer_primary);
	kfree(dev->read_buffer_secondary);
	kfree(dev->interrupt_in_buffer);
	kfree(dev->interrupt_out_buffer);
	kfree(dev);

	dbg(2, "%s : leave", __FUNCTION__);
}

static void adu_interrupt_in_callback(struct urb *urb)
{
	struct adu_device *dev = urb->context;

	dbg(4," %s : enter, status %d", __FUNCTION__, urb->status);
	adu_debug_data(5, __FUNCTION__, urb->actual_length,
		       urb->transfer_buffer);

	spin_lock(&dev->buflock);

	if (urb->status != 0) {
		if ((urb->status != -ENOENT) && (urb->status != -ECONNRESET)) {
			dbg(1," %s : nonzero status received: %d",
			    __FUNCTION__, urb->status);
		}
		goto exit;
	}

	if (urb->actual_length > 0 && dev->interrupt_in_buffer[0] != 0x00) {
		if (dev->read_buffer_length <
		    (4 * le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize)) -
		     (urb->actual_length)) {
			memcpy (dev->read_buffer_primary +
				dev->read_buffer_length,
				dev->interrupt_in_buffer, urb->actual_length);

			dev->read_buffer_length += urb->actual_length;
			dbg(2," %s reading  %d ", __FUNCTION__,
			    urb->actual_length);
		} else {
			dbg(1," %s : read_buffer overflow", __FUNCTION__);
		}
	}

exit:
	dev->read_urb_finished = 1;
	spin_unlock(&dev->buflock);
	/* always wake up so we recover from errors */
	wake_up_interruptible(&dev->read_wait);
	adu_debug_data(5, __FUNCTION__, urb->actual_length,
		       urb->transfer_buffer);
	dbg(4," %s : leave, status %d", __FUNCTION__, urb->status);
}

static void adu_interrupt_out_callback(struct urb *urb)
{
	struct adu_device *dev = urb->context;

	dbg(4," %s : enter, status %d", __FUNCTION__, urb->status);
	adu_debug_data(5,__FUNCTION__, urb->actual_length, urb->transfer_buffer);

	if (urb->status != 0) {
		if ((urb->status != -ENOENT) &&
		    (urb->status != -ECONNRESET)) {
			dbg(1, " %s :nonzero status received: %d",
			    __FUNCTION__, urb->status);
		}
		goto exit;
	}

	wake_up_interruptible(&dev->write_wait);
exit:

	adu_debug_data(5, __FUNCTION__, urb->actual_length,
		       urb->transfer_buffer);
	dbg(4," %s : leave, status %d", __FUNCTION__, urb->status);
}

static int adu_open(struct inode *inode, struct file *file)
{
	struct adu_device *dev = NULL;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	dbg(2,"%s : enter", __FUNCTION__);

	subminor = iminor(inode);

	mutex_lock(&disconnect_mutex);

	interface = usb_find_interface(&adu_driver, subminor);
	if (!interface) {
		err("%s - error, can't find device for minor %d",
		    __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit_no_device;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit_no_device;
	}

	/* lock this device */
	if ((retval = down_interruptible(&dev->sem))) {
		dbg(2, "%s : sem down failed", __FUNCTION__);
		goto exit_no_device;
	}

	/* increment our usage count for the device */
	++dev->open_count;
	dbg(2,"%s : open count %d", __FUNCTION__, dev->open_count);

	/* save device in the file's private structure */
	file->private_data = dev;

	if (dev->open_count == 1) {
		/* initialize in direction */
		dev->read_buffer_length = 0;

		/* fixup first read by having urb waiting for it */
		usb_fill_int_urb(dev->interrupt_in_urb,dev->udev,
				 usb_rcvintpipe(dev->udev,
				 		dev->interrupt_in_endpoint->bEndpointAddress),
				 dev->interrupt_in_buffer,
				 le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize),
				 adu_interrupt_in_callback, dev,
				 dev->interrupt_in_endpoint->bInterval);
		/* dev->interrupt_in_urb->transfer_flags |= URB_ASYNC_UNLINK; */
		dev->read_urb_finished = 0;
		retval = usb_submit_urb(dev->interrupt_in_urb, GFP_KERNEL);
		if (retval)
			--dev->open_count;
	}
	up(&dev->sem);

exit_no_device:
	mutex_unlock(&disconnect_mutex);
	dbg(2,"%s : leave, return value %d ", __FUNCTION__, retval);

	return retval;
}

static int adu_release_internal(struct adu_device *dev)
{
	int retval = 0;

	dbg(2," %s : enter", __FUNCTION__);

	if (dev->udev == NULL) {
		/* the device was unplugged before the file was released */
		adu_delete(dev);
		goto exit;
	}

	/* decrement our usage count for the device */
	--dev->open_count;
	dbg(2," %s : open count %d", __FUNCTION__, dev->open_count);
	if (dev->open_count <= 0) {
		adu_abort_transfers(dev);
		dev->open_count = 0;
	}

exit:
	dbg(2," %s : leave", __FUNCTION__);
	return retval;
}

static int adu_release(struct inode *inode, struct file *file)
{
	struct adu_device *dev = NULL;
	int retval = 0;

	dbg(2," %s : enter", __FUNCTION__);

	if (file == NULL) {
 		dbg(1," %s : file is NULL", __FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}

	dev = file->private_data;

	if (dev == NULL) {
 		dbg(1," %s : object is NULL", __FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}

	/* lock our device */
	down(&dev->sem); /* not interruptible */

	if (dev->open_count <= 0) {
		dbg(1," %s : device not opened", __FUNCTION__);
		retval = -ENODEV;
		goto exit;
	}

	/* do the work */
	retval = adu_release_internal(dev);

exit:
	if (dev)
		up(&dev->sem);
	dbg(2," %s : leave, return value %d", __FUNCTION__, retval);
	return retval;
}

static ssize_t adu_read(struct file *file, __user char *buffer, size_t count,
			loff_t *ppos)
{
	struct adu_device *dev;
	size_t bytes_read = 0;
	size_t bytes_to_read = count;
	int i;
	int retval = 0;
	int timeout = 0;
	int should_submit = 0;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	dbg(2," %s : enter, count = %Zd, file=%p", __FUNCTION__, count, file);

	dev = file->private_data;
	dbg(2," %s : dev=%p", __FUNCTION__, dev);
	/* lock this object */
	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* verify that the device wasn't unplugged */
	if (dev->udev == NULL || dev->minor == 0) {
		retval = -ENODEV;
		err("No device or device unplugged %d", retval);
		goto exit;
	}

	/* verify that some data was requested */
	if (count == 0) {
		dbg(1," %s : read request of 0 bytes", __FUNCTION__);
		goto exit;
	}

	timeout = COMMAND_TIMEOUT;
	dbg(2," %s : about to start looping", __FUNCTION__);
	while (bytes_to_read) {
		int data_in_secondary = dev->secondary_tail - dev->secondary_head;
		dbg(2," %s : while, data_in_secondary=%d, status=%d",
		    __FUNCTION__, data_in_secondary,
		    dev->interrupt_in_urb->status);

		if (data_in_secondary) {
			/* drain secondary buffer */
			int amount = bytes_to_read < data_in_secondary ? bytes_to_read : data_in_secondary;
			i = copy_to_user(buffer, dev->read_buffer_secondary+dev->secondary_head, amount);
			if (i < 0) {
				retval = -EFAULT;
				goto exit;
			}
			dev->secondary_head += (amount - i);
			bytes_read += (amount - i);
			bytes_to_read -= (amount - i);
			if (i) {
				retval = bytes_read ? bytes_read : -EFAULT;
				goto exit;
			}
		} else {
			/* we check the primary buffer */
			spin_lock_irqsave (&dev->buflock, flags);
			if (dev->read_buffer_length) {
				/* we secure access to the primary */
				char *tmp;
				dbg(2," %s : swap, read_buffer_length = %d",
				    __FUNCTION__, dev->read_buffer_length);
				tmp = dev->read_buffer_secondary;
				dev->read_buffer_secondary = dev->read_buffer_primary;
				dev->read_buffer_primary = tmp;
				dev->secondary_head = 0;
				dev->secondary_tail = dev->read_buffer_length;
				dev->read_buffer_length = 0;
				spin_unlock_irqrestore(&dev->buflock, flags);
				/* we have a free buffer so use it */
				should_submit = 1;
			} else {
				/* even the primary was empty - we may need to do IO */
				if (dev->interrupt_in_urb->status == -EINPROGRESS) {
					/* somebody is doing IO */
					spin_unlock_irqrestore(&dev->buflock, flags);
					dbg(2," %s : submitted already", __FUNCTION__);
				} else {
					/* we must initiate input */
					dbg(2," %s : initiate input", __FUNCTION__);
					dev->read_urb_finished = 0;

					usb_fill_int_urb(dev->interrupt_in_urb,dev->udev,
							 usb_rcvintpipe(dev->udev,
							 		dev->interrupt_in_endpoint->bEndpointAddress),
							 dev->interrupt_in_buffer,
							 le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize),
							 adu_interrupt_in_callback,
							 dev,
							 dev->interrupt_in_endpoint->bInterval);
					retval = usb_submit_urb(dev->interrupt_in_urb, GFP_ATOMIC);
					if (!retval) {
						spin_unlock_irqrestore(&dev->buflock, flags);
						dbg(2," %s : submitted OK", __FUNCTION__);
					} else {
						if (retval == -ENOMEM) {
							retval = bytes_read ? bytes_read : -ENOMEM;
						}
						spin_unlock_irqrestore(&dev->buflock, flags);
						dbg(2," %s : submit failed", __FUNCTION__);
						goto exit;
					}
				}

				/* we wait for I/O to complete */
				set_current_state(TASK_INTERRUPTIBLE);
				add_wait_queue(&dev->read_wait, &wait);
				if (!dev->read_urb_finished)
					timeout = schedule_timeout(COMMAND_TIMEOUT);
				else
					set_current_state(TASK_RUNNING);
				remove_wait_queue(&dev->read_wait, &wait);

				if (timeout <= 0) {
					dbg(2," %s : timeout", __FUNCTION__);
					retval = bytes_read ? bytes_read : -ETIMEDOUT;
					goto exit;
				}

				if (signal_pending(current)) {
					dbg(2," %s : signal pending", __FUNCTION__);
					retval = bytes_read ? bytes_read : -EINTR;
					goto exit;
				}
			}
		}
	}

	retval = bytes_read;
	/* if the primary buffer is empty then use it */
	if (should_submit && !dev->interrupt_in_urb->status==-EINPROGRESS) {
		usb_fill_int_urb(dev->interrupt_in_urb,dev->udev,
				 usb_rcvintpipe(dev->udev,
				 		dev->interrupt_in_endpoint->bEndpointAddress),
						dev->interrupt_in_buffer,
						le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize),
						adu_interrupt_in_callback,
						dev,
						dev->interrupt_in_endpoint->bInterval);
		/* dev->interrupt_in_urb->transfer_flags |= URB_ASYNC_UNLINK; */
		dev->read_urb_finished = 0;
		usb_submit_urb(dev->interrupt_in_urb, GFP_KERNEL);
		/* we ignore failure */
	}

exit:
	/* unlock the device */
	up(&dev->sem);

	dbg(2," %s : leave, return value %d", __FUNCTION__, retval);
	return retval;
}

static ssize_t adu_write(struct file *file, const __user char *buffer,
			 size_t count, loff_t *ppos)
{
	struct adu_device *dev;
	size_t bytes_written = 0;
	size_t bytes_to_write;
	size_t buffer_size;
	int retval;
	int timeout = 0;

	dbg(2," %s : enter, count = %Zd", __FUNCTION__, count);

	dev = file->private_data;

	/* lock this object */
	retval = down_interruptible(&dev->sem);
	if (retval)
		goto exit_nolock;

	/* verify that the device wasn't unplugged */
	if (dev->udev == NULL || dev->minor == 0) {
		retval = -ENODEV;
		err("No device or device unplugged %d", retval);
		goto exit;
	}

	/* verify that we actually have some data to write */
	if (count == 0) {
		dbg(1," %s : write request of 0 bytes", __FUNCTION__);
		goto exit;
	}


	while (count > 0) {
		if (dev->interrupt_out_urb->status == -EINPROGRESS) {
			timeout = COMMAND_TIMEOUT;

			while (timeout > 0) {
				if (signal_pending(current)) {
				dbg(1," %s : interrupted", __FUNCTION__);
				retval = -EINTR;
				goto exit;
			}
			up(&dev->sem);
			timeout = interruptible_sleep_on_timeout(&dev->write_wait, timeout);
			retval = down_interruptible(&dev->sem);
			if (retval) {
				retval = bytes_written ? bytes_written : retval;
				goto exit_nolock;
			}
			if (timeout > 0) {
				break;
			}
			dbg(1," %s : interrupted timeout: %d", __FUNCTION__, timeout);
		}


		dbg(1," %s : final timeout: %d", __FUNCTION__, timeout);

		if (timeout == 0) {
			dbg(1, "%s - command timed out.", __FUNCTION__);
			retval = -ETIMEDOUT;
			goto exit;
		}

		dbg(4," %s : in progress, count = %Zd", __FUNCTION__, count);

		} else {
			dbg(4," %s : sending, count = %Zd", __FUNCTION__, count);

			/* write the data into interrupt_out_buffer from userspace */
			buffer_size = le16_to_cpu(dev->interrupt_out_endpoint->wMaxPacketSize);
			bytes_to_write = count > buffer_size ? buffer_size : count;
			dbg(4," %s : buffer_size = %Zd, count = %Zd, bytes_to_write = %Zd",
			    __FUNCTION__, buffer_size, count, bytes_to_write);

			if (copy_from_user(dev->interrupt_out_buffer, buffer, bytes_to_write) != 0) {
				retval = -EFAULT;
				goto exit;
			}

			/* send off the urb */
			usb_fill_int_urb(
				dev->interrupt_out_urb,
				dev->udev,
				usb_sndintpipe(dev->udev, dev->interrupt_out_endpoint->bEndpointAddress),
				dev->interrupt_out_buffer,
				bytes_to_write,
				adu_interrupt_out_callback,
				dev,
				dev->interrupt_in_endpoint->bInterval);
			/* dev->interrupt_in_urb->transfer_flags |= URB_ASYNC_UNLINK; */
			dev->interrupt_out_urb->actual_length = bytes_to_write;
			retval = usb_submit_urb(dev->interrupt_out_urb, GFP_KERNEL);
			if (retval < 0) {
				err("Couldn't submit interrupt_out_urb %d", retval);
				goto exit;
			}

			buffer += bytes_to_write;
			count -= bytes_to_write;

			bytes_written += bytes_to_write;
		}
	}

	retval = bytes_written;

exit:
	/* unlock the device */
	up(&dev->sem);
exit_nolock:

	dbg(2," %s : leave, return value %d", __FUNCTION__, retval);

	return retval;
}

/* file operations needed when we register this driver */
static const struct file_operations adu_fops = {
	.owner = THIS_MODULE,
	.read  = adu_read,
	.write = adu_write,
	.open = adu_open,
	.release = adu_release,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver adu_class = {
	.name = "usb/adutux%d",
	.fops = &adu_fops,
	.minor_base = ADU_MINOR_BASE,
};

/**
 * adu_probe
 *
 * Called by the usb core when a new device is connected that it thinks
 * this driver might be interested in.
 */
static int adu_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct adu_device *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	int retval = -ENODEV;
	int in_end_size;
	int out_end_size;
	int i;

	dbg(2," %s : enter", __FUNCTION__);

	if (udev == NULL) {
		dev_err(&interface->dev, "udev is NULL.\n");
		goto exit;
	}

	/* allocate memory for our device state and intialize it */
	dev = kzalloc(sizeof(struct adu_device), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		retval = -ENOMEM;
		goto exit;
	}

	init_MUTEX(&dev->sem);
	spin_lock_init(&dev->buflock);
	dev->udev = udev;
	init_waitqueue_head(&dev->read_wait);
	init_waitqueue_head(&dev->write_wait);

	iface_desc = &interface->altsetting[0];

	/* set up the endpoint information */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(endpoint))
			dev->interrupt_in_endpoint = endpoint;

		if (usb_endpoint_is_int_out(endpoint))
			dev->interrupt_out_endpoint = endpoint;
	}
	if (dev->interrupt_in_endpoint == NULL) {
		dev_err(&interface->dev, "interrupt in endpoint not found\n");
		goto error;
	}
	if (dev->interrupt_out_endpoint == NULL) {
		dev_err(&interface->dev, "interrupt out endpoint not found\n");
		goto error;
	}

	in_end_size = le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize);
	out_end_size = le16_to_cpu(dev->interrupt_out_endpoint->wMaxPacketSize);

	dev->read_buffer_primary = kmalloc((4 * in_end_size), GFP_KERNEL);
	if (!dev->read_buffer_primary) {
		dev_err(&interface->dev, "Couldn't allocate read_buffer_primary\n");
		retval = -ENOMEM;
		goto error;
	}

	/* debug code prime the buffer */
	memset(dev->read_buffer_primary, 'a', in_end_size);
	memset(dev->read_buffer_primary + in_end_size, 'b', in_end_size);
	memset(dev->read_buffer_primary + (2 * in_end_size), 'c', in_end_size);
	memset(dev->read_buffer_primary + (3 * in_end_size), 'd', in_end_size);

	dev->read_buffer_secondary = kmalloc((4 * in_end_size), GFP_KERNEL);
	if (!dev->read_buffer_secondary) {
		dev_err(&interface->dev, "Couldn't allocate read_buffer_secondary\n");
		retval = -ENOMEM;
		goto error;
	}

	/* debug code prime the buffer */
	memset(dev->read_buffer_secondary, 'e', in_end_size);
	memset(dev->read_buffer_secondary + in_end_size, 'f', in_end_size);
	memset(dev->read_buffer_secondary + (2 * in_end_size), 'g', in_end_size);
	memset(dev->read_buffer_secondary + (3 * in_end_size), 'h', in_end_size);

	dev->interrupt_in_buffer = kmalloc(in_end_size, GFP_KERNEL);
	if (!dev->interrupt_in_buffer) {
		dev_err(&interface->dev, "Couldn't allocate interrupt_in_buffer\n");
		goto error;
	}

	/* debug code prime the buffer */
	memset(dev->interrupt_in_buffer, 'i', in_end_size);

	dev->interrupt_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->interrupt_in_urb) {
		dev_err(&interface->dev, "Couldn't allocate interrupt_in_urb\n");
		goto error;
	}
	dev->interrupt_out_buffer = kmalloc(out_end_size, GFP_KERNEL);
	if (!dev->interrupt_out_buffer) {
		dev_err(&interface->dev, "Couldn't allocate interrupt_out_buffer\n");
		goto error;
	}
	dev->interrupt_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->interrupt_out_urb) {
		dev_err(&interface->dev, "Couldn't allocate interrupt_out_urb\n");
		goto error;
	}

	if (!usb_string(udev, udev->descriptor.iSerialNumber, dev->serial_number,
			sizeof(dev->serial_number))) {
		dev_err(&interface->dev, "Could not retrieve serial number\n");
		goto error;
	}
	dbg(2," %s : serial_number=%s", __FUNCTION__, dev->serial_number);

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, dev);

	retval = usb_register_dev(interface, &adu_class);

	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev, "Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	dev->minor = interface->minor;

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev, "ADU%d %s now attached to /dev/usb/adutux%d",
		 udev->descriptor.idProduct, dev->serial_number,
		 (dev->minor - ADU_MINOR_BASE));
exit:
	dbg(2," %s : leave, return value %p (dev)", __FUNCTION__, dev);

	return retval;

error:
	adu_delete(dev);
	return retval;
}

/**
 * adu_disconnect
 *
 * Called by the usb core when the device is removed from the system.
 */
static void adu_disconnect(struct usb_interface *interface)
{
	struct adu_device *dev;
	int minor;

	dbg(2," %s : enter", __FUNCTION__);

	mutex_lock(&disconnect_mutex); /* not interruptible */

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	down(&dev->sem); /* not interruptible */

	minor = dev->minor;

	/* give back our minor */
	usb_deregister_dev(interface, &adu_class);
	dev->minor = 0;

	/* if the device is not opened, then we clean up right now */
	dbg(2," %s : open count %d", __FUNCTION__, dev->open_count);
	if (!dev->open_count) {
		up(&dev->sem);
		adu_delete(dev);
	} else {
		dev->udev = NULL;
		up(&dev->sem);
	}

	mutex_unlock(&disconnect_mutex);

	dev_info(&interface->dev, "ADU device adutux%d now disconnected",
		 (minor - ADU_MINOR_BASE));

	dbg(2," %s : leave", __FUNCTION__);
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver adu_driver = {
	.name = "adutux",
	.probe = adu_probe,
	.disconnect = adu_disconnect,
	.id_table = device_table,
};

static int __init adu_init(void)
{
	int result;

	dbg(2," %s : enter", __FUNCTION__);

	/* register this driver with the USB subsystem */
	result = usb_register(&adu_driver);
	if (result < 0) {
		err("usb_register failed for the "__FILE__" driver. "
		    "Error number %d", result);
		goto exit;
	}

	info("adutux " DRIVER_DESC " " DRIVER_VERSION);
	info("adutux is an experimental driver. Use at your own risk");

exit:
	dbg(2," %s : leave, return value %d", __FUNCTION__, result);

	return result;
}

static void __exit adu_exit(void)
{
	dbg(2," %s : enter", __FUNCTION__);
	/* deregister this driver with the USB subsystem */
	usb_deregister(&adu_driver);
	dbg(2," %s : leave", __FUNCTION__);
}

module_init(adu_init);
module_exit(adu_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
