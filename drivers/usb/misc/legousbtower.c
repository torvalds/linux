/*
 * LEGO USB Tower driver
 *
 * Copyright (C) 2003 David Glance <davidgsf@sourceforge.net>
 *               2001-2004 Juergen Stuber <starblue@users.sourceforge.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 * derived from USB Skeleton driver - 0.5
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 * History:
 *
 * 2001-10-13 - 0.1 js
 *   - first version
 * 2001-11-03 - 0.2 js
 *   - simplified buffering, one-shot URBs for writing
 * 2001-11-10 - 0.3 js
 *   - removed IOCTL (setting power/mode is more complicated, postponed)
 * 2001-11-28 - 0.4 js
 *   - added vendor commands for mode of operation and power level in open
 * 2001-12-04 - 0.5 js
 *   - set IR mode by default (by oversight 0.4 set VLL mode)
 * 2002-01-11 - 0.5? pcchan
 *   - make read buffer reusable and work around bytes_to_write issue between
 *     uhci and legusbtower
 * 2002-09-23 - 0.52 david (david@csse.uwa.edu.au)
 *   - imported into lejos project
 *   - changed wake_up to wake_up_interruptible
 *   - changed to use lego0 rather than tower0
 *   - changed dbg() to use __func__ rather than deprecated __FUNCTION__
 * 2003-01-12 - 0.53 david (david@csse.uwa.edu.au)
 *   - changed read and write to write everything or
 *     timeout (from a patch by Chris Riesen and Brett Thaeler driver)
 *   - added ioctl functionality to set timeouts
 * 2003-07-18 - 0.54 davidgsf (david@csse.uwa.edu.au)
 *   - initial import into LegoUSB project
 *   - merge of existing LegoUSB.c driver
 * 2003-07-18 - 0.56 davidgsf (david@csse.uwa.edu.au)
 *   - port to 2.6 style driver
 * 2004-02-29 - 0.6 Juergen Stuber <starblue@users.sourceforge.net>
 *   - fix locking
 *   - unlink read URBs which are no longer needed
 *   - allow increased buffer size, eliminates need for timeout on write
 *   - have read URB running continuously
 *   - added poll
 *   - forbid seeking
 *   - added nonblocking I/O
 *   - changed back __func__ to __FUNCTION__
 *   - read and log tower firmware version
 *   - reset tower on probe, avoids failure of first write
 * 2004-03-09 - 0.7 Juergen Stuber <starblue@users.sourceforge.net>
 *   - timeout read now only after inactivity, shorten default accordingly
 * 2004-03-11 - 0.8 Juergen Stuber <starblue@users.sourceforge.net>
 *   - log major, minor instead of possibly confusing device filename
 *   - whitespace cleanup
 * 2004-03-12 - 0.9 Juergen Stuber <starblue@users.sourceforge.net>
 *   - normalize whitespace in debug messages
 *   - take care about endianness in control message responses
 * 2004-03-13 - 0.91 Juergen Stuber <starblue@users.sourceforge.net>
 *   - make default intervals longer to accommodate current EHCI driver
 * 2004-03-19 - 0.92 Juergen Stuber <starblue@users.sourceforge.net>
 *   - replaced atomic_t by memory barriers
 * 2004-04-21 - 0.93 Juergen Stuber <starblue@users.sourceforge.net>
 *   - wait for completion of write urb in release (needed for remotecontrol)
 *   - corrected poll for write direction (missing negation)
 * 2004-04-22 - 0.94 Juergen Stuber <starblue@users.sourceforge.net>
 *   - make device locking interruptible
 * 2004-04-30 - 0.95 Juergen Stuber <starblue@users.sourceforge.net>
 *   - check for valid udev on resubmitting and unlinking urbs
 * 2004-08-03 - 0.96 Juergen Stuber <starblue@users.sourceforge.net>
 *   - move reset into open to clean out spurious data
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/poll.h>


#ifdef CONFIG_USB_DEBUG
	static int debug = 4;
#else
	static int debug = 0;
#endif

/* Use our own dbg macro */
#undef dbg
#define dbg(lvl, format, arg...) do { if (debug >= lvl) printk(KERN_DEBUG  __FILE__ ": " format "\n", ## arg); } while (0)


/* Version Information */
#define DRIVER_VERSION "v0.96"
#define DRIVER_AUTHOR "Juergen Stuber <starblue@sourceforge.net>"
#define DRIVER_DESC "LEGO USB Tower Driver"

/* Module parameters */
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

/* The defaults are chosen to work with the latest versions of leJOS and NQC.
 */

/* Some legacy software likes to receive packets in one piece.
 * In this case read_buffer_size should exceed the maximal packet length
 * (417 for datalog uploads), and packet_timeout should be set.
 */
static int read_buffer_size = 480;
module_param(read_buffer_size, int, 0);
MODULE_PARM_DESC(read_buffer_size, "Read buffer size");

/* Some legacy software likes to send packets in one piece.
 * In this case write_buffer_size should exceed the maximal packet length
 * (417 for firmware and program downloads).
 * A problem with long writes is that the following read may time out
 * if the software is not prepared to wait long enough.
 */
static int write_buffer_size = 480;
module_param(write_buffer_size, int, 0);
MODULE_PARM_DESC(write_buffer_size, "Write buffer size");

/* Some legacy software expects reads to contain whole LASM packets.
 * To achieve this, characters which arrive before a packet timeout
 * occurs will be returned in a single read operation.
 * A problem with long reads is that the software may time out
 * if it is not prepared to wait long enough.
 * The packet timeout should be greater than the time between the
 * reception of subsequent characters, which should arrive about
 * every 5ms for the standard 2400 baud.
 * Set it to 0 to disable.
 */
static int packet_timeout = 50;
module_param(packet_timeout, int, 0);
MODULE_PARM_DESC(packet_timeout, "Packet timeout in ms");

/* Some legacy software expects blocking reads to time out.
 * Timeout occurs after the specified time of read and write inactivity.
 * Set it to 0 to disable.
 */
static int read_timeout = 200;
module_param(read_timeout, int, 0);
MODULE_PARM_DESC(read_timeout, "Read timeout in ms");

/* As of kernel version 2.6.4 ehci-hcd uses an
 * "only one interrupt transfer per frame" shortcut
 * to simplify the scheduling of periodic transfers.
 * This conflicts with our standard 1ms intervals for in and out URBs.
 * We use default intervals of 2ms for in and 8ms for out transfers,
 * which is fast enough for 2400 baud and allows a small additional load.
 * Increase the interval to allow more devices that do interrupt transfers,
 * or set to 0 to use the standard interval from the endpoint descriptors.
 */
static int interrupt_in_interval = 2;
module_param(interrupt_in_interval, int, 0);
MODULE_PARM_DESC(interrupt_in_interval, "Interrupt in interval in ms");

static int interrupt_out_interval = 8;
module_param(interrupt_out_interval, int, 0);
MODULE_PARM_DESC(interrupt_out_interval, "Interrupt out interval in ms");

/* Define these values to match your device */
#define LEGO_USB_TOWER_VENDOR_ID	0x0694
#define LEGO_USB_TOWER_PRODUCT_ID	0x0001

/* Vendor requests */
#define LEGO_USB_TOWER_REQUEST_RESET		0x04
#define LEGO_USB_TOWER_REQUEST_GET_VERSION	0xFD

struct tower_reset_reply {
	__le16 size;		/* little-endian */
	__u8 err_code;
	__u8 spare;
} __attribute__ ((packed));

struct tower_get_version_reply {
	__le16 size;		/* little-endian */
	__u8 err_code;
	__u8 spare;
	__u8 major;
	__u8 minor;
	__le16 build_no;		/* little-endian */
} __attribute__ ((packed));


/* table of devices that work with this driver */
static struct usb_device_id tower_table [] = {
	{ USB_DEVICE(LEGO_USB_TOWER_VENDOR_ID, LEGO_USB_TOWER_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, tower_table);
static DEFINE_MUTEX(open_disc_mutex);

#define LEGO_USB_TOWER_MINOR_BASE	160


/* Structure to hold all of our device specific stuff */
struct lego_usb_tower {
	struct semaphore	sem;		/* locks this structure */
	struct usb_device*	udev;		/* save off the usb device pointer */
	unsigned char		minor;		/* the starting minor number for this device */

	int			open_count;	/* number of times this port has been opened */

	char*			read_buffer;
	size_t			read_buffer_length; /* this much came in */
	size_t			read_packet_length; /* this much will be returned on read */
	spinlock_t		read_buffer_lock;
	int			packet_timeout_jiffies;
	unsigned long		read_last_arrival;

	wait_queue_head_t	read_wait;
	wait_queue_head_t	write_wait;

	char*			interrupt_in_buffer;
	struct usb_endpoint_descriptor* interrupt_in_endpoint;
	struct urb*		interrupt_in_urb;
	int			interrupt_in_interval;
	int			interrupt_in_running;
	int			interrupt_in_done;

	char*			interrupt_out_buffer;
	struct usb_endpoint_descriptor* interrupt_out_endpoint;
	struct urb*		interrupt_out_urb;
	int			interrupt_out_interval;
	int			interrupt_out_busy;

};


/* local function prototypes */
static ssize_t tower_read	(struct file *file, char __user *buffer, size_t count, loff_t *ppos);
static ssize_t tower_write	(struct file *file, const char __user *buffer, size_t count, loff_t *ppos);
static inline void tower_delete (struct lego_usb_tower *dev);
static int tower_open		(struct inode *inode, struct file *file);
static int tower_release	(struct inode *inode, struct file *file);
static unsigned int tower_poll	(struct file *file, poll_table *wait);
static loff_t tower_llseek	(struct file *file, loff_t off, int whence);

static void tower_abort_transfers (struct lego_usb_tower *dev);
static void tower_check_for_read_packet (struct lego_usb_tower *dev);
static void tower_interrupt_in_callback (struct urb *urb);
static void tower_interrupt_out_callback (struct urb *urb);

static int  tower_probe	(struct usb_interface *interface, const struct usb_device_id *id);
static void tower_disconnect	(struct usb_interface *interface);


/* file operations needed when we register this driver */
static const struct file_operations tower_fops = {
	.owner =	THIS_MODULE,
	.read  =	tower_read,
	.write =	tower_write,
	.open =		tower_open,
	.release =	tower_release,
	.poll =		tower_poll,
	.llseek =	tower_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver tower_class = {
	.name =		"legousbtower%d",
	.fops =		&tower_fops,
	.minor_base =	LEGO_USB_TOWER_MINOR_BASE,
};


/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver tower_driver = {
	.name =		"legousbtower",
	.probe =	tower_probe,
	.disconnect =	tower_disconnect,
	.id_table =	tower_table,
};


/**
 *	lego_usb_tower_debug_data
 */
static inline void lego_usb_tower_debug_data (int level, const char *function, int size, const unsigned char *data)
{
	int i;

	if (debug < level)
		return;

	printk (KERN_DEBUG __FILE__": %s - length = %d, data = ", function, size);
	for (i = 0; i < size; ++i) {
		printk ("%.2x ", data[i]);
	}
	printk ("\n");
}


/**
 *	tower_delete
 */
static inline void tower_delete (struct lego_usb_tower *dev)
{
	dbg(2, "%s: enter", __FUNCTION__);

	tower_abort_transfers (dev);

	/* free data structures */
	usb_free_urb(dev->interrupt_in_urb);
	usb_free_urb(dev->interrupt_out_urb);
	kfree (dev->read_buffer);
	kfree (dev->interrupt_in_buffer);
	kfree (dev->interrupt_out_buffer);
	kfree (dev);

	dbg(2, "%s: leave", __FUNCTION__);
}


/**
 *	tower_open
 */
static int tower_open (struct inode *inode, struct file *file)
{
	struct lego_usb_tower *dev = NULL;
	int subminor;
	int retval = 0;
	struct usb_interface *interface;
	struct tower_reset_reply reset_reply;
	int result;

	dbg(2, "%s: enter", __FUNCTION__);

	nonseekable_open(inode, file);
	subminor = iminor(inode);

	interface = usb_find_interface (&tower_driver, subminor);

	if (!interface) {
		err ("%s - error, can't find device for minor %d",
		     __FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	mutex_lock(&open_disc_mutex);
	dev = usb_get_intfdata(interface);

	if (!dev) {
		mutex_unlock(&open_disc_mutex);
		retval = -ENODEV;
		goto exit;
	}

	/* lock this device */
	if (down_interruptible (&dev->sem)) {
		mutex_unlock(&open_disc_mutex);
	        retval = -ERESTARTSYS;
		goto exit;
	}


	/* allow opening only once */
	if (dev->open_count) {
		mutex_unlock(&open_disc_mutex);
		retval = -EBUSY;
		goto unlock_exit;
	}
	dev->open_count = 1;
	mutex_unlock(&open_disc_mutex);

	/* reset the tower */
	result = usb_control_msg (dev->udev,
				  usb_rcvctrlpipe(dev->udev, 0),
				  LEGO_USB_TOWER_REQUEST_RESET,
				  USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE,
				  0,
				  0,
				  &reset_reply,
				  sizeof(reset_reply),
				  1000);
	if (result < 0) {
		err("LEGO USB Tower reset control request failed");
		retval = result;
		goto unlock_exit;
	}

	/* initialize in direction */
	dev->read_buffer_length = 0;
	dev->read_packet_length = 0;
	usb_fill_int_urb (dev->interrupt_in_urb,
			  dev->udev,
			  usb_rcvintpipe(dev->udev, dev->interrupt_in_endpoint->bEndpointAddress),
			  dev->interrupt_in_buffer,
			  le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize),
			  tower_interrupt_in_callback,
			  dev,
			  dev->interrupt_in_interval);

	dev->interrupt_in_running = 1;
	dev->interrupt_in_done = 0;
	mb();

	retval = usb_submit_urb (dev->interrupt_in_urb, GFP_KERNEL);
	if (retval) {
		err("Couldn't submit interrupt_in_urb %d", retval);
		dev->interrupt_in_running = 0;
		dev->open_count = 0;
		goto unlock_exit;
	}

	/* save device in the file's private structure */
	file->private_data = dev;

unlock_exit:
	up (&dev->sem);

exit:
	dbg(2, "%s: leave, return value %d ", __FUNCTION__, retval);

	return retval;
}

/**
 *	tower_release
 */
static int tower_release (struct inode *inode, struct file *file)
{
	struct lego_usb_tower *dev;
	int retval = 0;

	dbg(2, "%s: enter", __FUNCTION__);

	dev = (struct lego_usb_tower *)file->private_data;

	if (dev == NULL) {
		dbg(1, "%s: object is NULL", __FUNCTION__);
		retval = -ENODEV;
		goto exit_nolock;
	}

	mutex_lock(&open_disc_mutex);
	if (down_interruptible (&dev->sem)) {
	        retval = -ERESTARTSYS;
		goto exit;
	}

	if (dev->open_count != 1) {
		dbg(1, "%s: device not opened exactly once", __FUNCTION__);
		retval = -ENODEV;
		goto unlock_exit;
	}
	if (dev->udev == NULL) {
		/* the device was unplugged before the file was released */
		up (&dev->sem);	/* unlock here as tower_delete frees dev */
		tower_delete (dev);
		goto exit;
	}

	/* wait until write transfer is finished */
	if (dev->interrupt_out_busy) {
		wait_event_interruptible_timeout (dev->write_wait, !dev->interrupt_out_busy, 2 * HZ);
	}
	tower_abort_transfers (dev);
	dev->open_count = 0;

unlock_exit:
	up (&dev->sem);

exit:
	mutex_unlock(&open_disc_mutex);
exit_nolock:
	dbg(2, "%s: leave, return value %d", __FUNCTION__, retval);
	return retval;
}


/**
 *	tower_abort_transfers
 *      aborts transfers and frees associated data structures
 */
static void tower_abort_transfers (struct lego_usb_tower *dev)
{
	dbg(2, "%s: enter", __FUNCTION__);

	if (dev == NULL) {
		dbg(1, "%s: dev is null", __FUNCTION__);
		goto exit;
	}

	/* shutdown transfer */
	if (dev->interrupt_in_running) {
		dev->interrupt_in_running = 0;
		mb();
		if (dev->udev)
			usb_kill_urb (dev->interrupt_in_urb);
	}
	if (dev->interrupt_out_busy && dev->udev)
		usb_kill_urb(dev->interrupt_out_urb);

exit:
	dbg(2, "%s: leave", __FUNCTION__);
}


/**
 *	tower_check_for_read_packet
 *
 *      To get correct semantics for signals and non-blocking I/O
 *      with packetizing we pretend not to see any data in the read buffer
 *      until it has been there unchanged for at least
 *      dev->packet_timeout_jiffies, or until the buffer is full.
 */
static void tower_check_for_read_packet (struct lego_usb_tower *dev)
{
	spin_lock_irq (&dev->read_buffer_lock);
	if (!packet_timeout
	    || time_after(jiffies, dev->read_last_arrival + dev->packet_timeout_jiffies)
	    || dev->read_buffer_length == read_buffer_size) {
		dev->read_packet_length = dev->read_buffer_length;
	}
	dev->interrupt_in_done = 0;
	spin_unlock_irq (&dev->read_buffer_lock);
}


/**
 *	tower_poll
 */
static unsigned int tower_poll (struct file *file, poll_table *wait)
{
	struct lego_usb_tower *dev;
	unsigned int mask = 0;

	dbg(2, "%s: enter", __FUNCTION__);

	dev = file->private_data;

	poll_wait(file, &dev->read_wait, wait);
	poll_wait(file, &dev->write_wait, wait);

	tower_check_for_read_packet(dev);
	if (dev->read_packet_length > 0) {
		mask |= POLLIN | POLLRDNORM;
	}
	if (!dev->interrupt_out_busy) {
		mask |= POLLOUT | POLLWRNORM;
	}

	dbg(2, "%s: leave, mask = %d", __FUNCTION__, mask);

	return mask;
}


/**
 *	tower_llseek
 */
static loff_t tower_llseek (struct file *file, loff_t off, int whence)
{
	return -ESPIPE;		/* unseekable */
}


/**
 *	tower_read
 */
static ssize_t tower_read (struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct lego_usb_tower *dev;
	size_t bytes_to_read;
	int i;
	int retval = 0;
	unsigned long timeout = 0;

	dbg(2, "%s: enter, count = %Zd", __FUNCTION__, count);

	dev = (struct lego_usb_tower *)file->private_data;

	/* lock this object */
	if (down_interruptible (&dev->sem)) {
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* verify that the device wasn't unplugged */
	if (dev->udev == NULL) {
		retval = -ENODEV;
		err("No device or device unplugged %d", retval);
		goto unlock_exit;
	}

	/* verify that we actually have some data to read */
	if (count == 0) {
		dbg(1, "%s: read request of 0 bytes", __FUNCTION__);
		goto unlock_exit;
	}

	if (read_timeout) {
		timeout = jiffies + read_timeout * HZ / 1000;
	}

	/* wait for data */
	tower_check_for_read_packet (dev);
	while (dev->read_packet_length == 0) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto unlock_exit;
		}
		retval = wait_event_interruptible_timeout(dev->read_wait, dev->interrupt_in_done, dev->packet_timeout_jiffies);
		if (retval < 0) {
			goto unlock_exit;
		}

		/* reset read timeout during read or write activity */
		if (read_timeout
		    && (dev->read_buffer_length || dev->interrupt_out_busy)) {
			timeout = jiffies + read_timeout * HZ / 1000;
		}
		/* check for read timeout */
		if (read_timeout && time_after (jiffies, timeout)) {
			retval = -ETIMEDOUT;
			goto unlock_exit;
		}
		tower_check_for_read_packet (dev);
	}

	/* copy the data from read_buffer into userspace */
	bytes_to_read = min(count, dev->read_packet_length);

	if (copy_to_user (buffer, dev->read_buffer, bytes_to_read)) {
		retval = -EFAULT;
		goto unlock_exit;
	}

	spin_lock_irq (&dev->read_buffer_lock);
	dev->read_buffer_length -= bytes_to_read;
	dev->read_packet_length -= bytes_to_read;
	for (i=0; i<dev->read_buffer_length; i++) {
		dev->read_buffer[i] = dev->read_buffer[i+bytes_to_read];
	}
	spin_unlock_irq (&dev->read_buffer_lock);

	retval = bytes_to_read;

unlock_exit:
	/* unlock the device */
	up (&dev->sem);

exit:
	dbg(2, "%s: leave, return value %d", __FUNCTION__, retval);
	return retval;
}


/**
 *	tower_write
 */
static ssize_t tower_write (struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct lego_usb_tower *dev;
	size_t bytes_to_write;
	int retval = 0;

	dbg(2, "%s: enter, count = %Zd", __FUNCTION__, count);

	dev = (struct lego_usb_tower *)file->private_data;

	/* lock this object */
	if (down_interruptible (&dev->sem)) {
		retval = -ERESTARTSYS;
		goto exit;
	}

	/* verify that the device wasn't unplugged */
	if (dev->udev == NULL) {
		retval = -ENODEV;
		err("No device or device unplugged %d", retval);
		goto unlock_exit;
	}

	/* verify that we actually have some data to write */
	if (count == 0) {
		dbg(1, "%s: write request of 0 bytes", __FUNCTION__);
		goto unlock_exit;
	}

	/* wait until previous transfer is finished */
	while (dev->interrupt_out_busy) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto unlock_exit;
		}
		retval = wait_event_interruptible (dev->write_wait, !dev->interrupt_out_busy);
		if (retval) {
			goto unlock_exit;
		}
	}

	/* write the data into interrupt_out_buffer from userspace */
	bytes_to_write = min_t(int, count, write_buffer_size);
	dbg(4, "%s: count = %Zd, bytes_to_write = %Zd", __FUNCTION__, count, bytes_to_write);

	if (copy_from_user (dev->interrupt_out_buffer, buffer, bytes_to_write)) {
		retval = -EFAULT;
		goto unlock_exit;
	}

	/* send off the urb */
	usb_fill_int_urb(dev->interrupt_out_urb,
			 dev->udev,
			 usb_sndintpipe(dev->udev, dev->interrupt_out_endpoint->bEndpointAddress),
			 dev->interrupt_out_buffer,
			 bytes_to_write,
			 tower_interrupt_out_callback,
			 dev,
			 dev->interrupt_out_interval);

	dev->interrupt_out_busy = 1;
	wmb();

	retval = usb_submit_urb (dev->interrupt_out_urb, GFP_KERNEL);
	if (retval) {
		dev->interrupt_out_busy = 0;
		err("Couldn't submit interrupt_out_urb %d", retval);
		goto unlock_exit;
	}
	retval = bytes_to_write;

unlock_exit:
	/* unlock the device */
	up (&dev->sem);

exit:
	dbg(2, "%s: leave, return value %d", __FUNCTION__, retval);

	return retval;
}


/**
 *	tower_interrupt_in_callback
 */
static void tower_interrupt_in_callback (struct urb *urb)
{
	struct lego_usb_tower *dev = (struct lego_usb_tower *)urb->context;
	int status = urb->status;
	int retval;

	dbg(4, "%s: enter, status %d", __FUNCTION__, status);

	lego_usb_tower_debug_data(5, __FUNCTION__, urb->actual_length, urb->transfer_buffer);

	if (status) {
		if (status == -ENOENT ||
		    status == -ECONNRESET ||
		    status == -ESHUTDOWN) {
			goto exit;
		} else {
			dbg(1, "%s: nonzero status received: %d", __FUNCTION__, status);
			goto resubmit; /* maybe we can recover */
		}
	}

	if (urb->actual_length > 0) {
		spin_lock (&dev->read_buffer_lock);
		if (dev->read_buffer_length + urb->actual_length < read_buffer_size) {
			memcpy (dev->read_buffer + dev->read_buffer_length,
				dev->interrupt_in_buffer,
				urb->actual_length);
			dev->read_buffer_length += urb->actual_length;
			dev->read_last_arrival = jiffies;
			dbg(3, "%s: received %d bytes", __FUNCTION__, urb->actual_length);
		} else {
			printk(KERN_WARNING "%s: read_buffer overflow, %d bytes dropped", __FUNCTION__, urb->actual_length);
		}
		spin_unlock (&dev->read_buffer_lock);
	}

resubmit:
	/* resubmit if we're still running */
	if (dev->interrupt_in_running && dev->udev) {
		retval = usb_submit_urb (dev->interrupt_in_urb, GFP_ATOMIC);
		if (retval) {
			err("%s: usb_submit_urb failed (%d)", __FUNCTION__, retval);
		}
	}

exit:
	dev->interrupt_in_done = 1;
	wake_up_interruptible (&dev->read_wait);

	lego_usb_tower_debug_data(5, __FUNCTION__, urb->actual_length, urb->transfer_buffer);
	dbg(4, "%s: leave, status %d", __FUNCTION__, status);
}


/**
 *	tower_interrupt_out_callback
 */
static void tower_interrupt_out_callback (struct urb *urb)
{
	struct lego_usb_tower *dev = (struct lego_usb_tower *)urb->context;
	int status = urb->status;

	dbg(4, "%s: enter, status %d", __FUNCTION__, status);
	lego_usb_tower_debug_data(5, __FUNCTION__, urb->actual_length, urb->transfer_buffer);

	/* sync/async unlink faults aren't errors */
	if (status && !(status == -ENOENT ||
			status == -ECONNRESET ||
			status == -ESHUTDOWN)) {
		dbg(1, "%s - nonzero write bulk status received: %d",
		    __FUNCTION__, status);
	}

	dev->interrupt_out_busy = 0;
	wake_up_interruptible(&dev->write_wait);

	lego_usb_tower_debug_data(5, __FUNCTION__, urb->actual_length, urb->transfer_buffer);
	dbg(4, "%s: leave, status %d", __FUNCTION__, status);
}


/**
 *	tower_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int tower_probe (struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct lego_usb_tower *dev = NULL;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor* endpoint;
	struct tower_get_version_reply get_version_reply;
	int i;
	int retval = -ENOMEM;
	int result;

	dbg(2, "%s: enter", __FUNCTION__);

	if (udev == NULL) {
		info ("udev is NULL.");
	}

	/* allocate memory for our device state and initialize it */

	dev = kmalloc (sizeof(struct lego_usb_tower), GFP_KERNEL);

	if (dev == NULL) {
		err ("Out of memory");
		goto exit;
	}

	init_MUTEX (&dev->sem);

	dev->udev = udev;
	dev->open_count = 0;

	dev->read_buffer = NULL;
	dev->read_buffer_length = 0;
	dev->read_packet_length = 0;
	spin_lock_init (&dev->read_buffer_lock);
	dev->packet_timeout_jiffies = packet_timeout * HZ / 1000;
	dev->read_last_arrival = jiffies;

	init_waitqueue_head (&dev->read_wait);
	init_waitqueue_head (&dev->write_wait);

	dev->interrupt_in_buffer = NULL;
	dev->interrupt_in_endpoint = NULL;
	dev->interrupt_in_urb = NULL;
	dev->interrupt_in_running = 0;
	dev->interrupt_in_done = 0;

	dev->interrupt_out_buffer = NULL;
	dev->interrupt_out_endpoint = NULL;
	dev->interrupt_out_urb = NULL;
	dev->interrupt_out_busy = 0;

	iface_desc = interface->cur_altsetting;

	/* set up the endpoint information */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_xfer_int(endpoint)) {
			if (usb_endpoint_dir_in(endpoint))
				dev->interrupt_in_endpoint = endpoint;
			else
				dev->interrupt_out_endpoint = endpoint;
		}
	}
	if(dev->interrupt_in_endpoint == NULL) {
		err("interrupt in endpoint not found");
		goto error;
	}
	if (dev->interrupt_out_endpoint == NULL) {
		err("interrupt out endpoint not found");
		goto error;
	}

	dev->read_buffer = kmalloc (read_buffer_size, GFP_KERNEL);
	if (!dev->read_buffer) {
		err("Couldn't allocate read_buffer");
		goto error;
	}
	dev->interrupt_in_buffer = kmalloc (le16_to_cpu(dev->interrupt_in_endpoint->wMaxPacketSize), GFP_KERNEL);
	if (!dev->interrupt_in_buffer) {
		err("Couldn't allocate interrupt_in_buffer");
		goto error;
	}
	dev->interrupt_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->interrupt_in_urb) {
		err("Couldn't allocate interrupt_in_urb");
		goto error;
	}
	dev->interrupt_out_buffer = kmalloc (write_buffer_size, GFP_KERNEL);
	if (!dev->interrupt_out_buffer) {
		err("Couldn't allocate interrupt_out_buffer");
		goto error;
	}
	dev->interrupt_out_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->interrupt_out_urb) {
		err("Couldn't allocate interrupt_out_urb");
		goto error;
	}
	dev->interrupt_in_interval = interrupt_in_interval ? interrupt_in_interval : dev->interrupt_in_endpoint->bInterval;
	dev->interrupt_out_interval = interrupt_out_interval ? interrupt_out_interval : dev->interrupt_out_endpoint->bInterval;

	/* we can register the device now, as it is ready */
	usb_set_intfdata (interface, dev);

	retval = usb_register_dev (interface, &tower_class);

	if (retval) {
		/* something prevented us from registering this driver */
		err ("Not able to get a minor for this device.");
		usb_set_intfdata (interface, NULL);
		goto error;
	}
	dev->minor = interface->minor;

	/* let the user know what node this device is now attached to */
	info ("LEGO USB Tower #%d now attached to major %d minor %d", (dev->minor - LEGO_USB_TOWER_MINOR_BASE), USB_MAJOR, dev->minor);

	/* get the firmware version and log it */
	result = usb_control_msg (udev,
				  usb_rcvctrlpipe(udev, 0),
				  LEGO_USB_TOWER_REQUEST_GET_VERSION,
				  USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_DEVICE,
				  0,
				  0,
				  &get_version_reply,
				  sizeof(get_version_reply),
				  1000);
	if (result < 0) {
		err("LEGO USB Tower get version control request failed");
		retval = result;
		goto error;
	}
	info("LEGO USB Tower firmware version is %d.%d build %d",
	     get_version_reply.major,
	     get_version_reply.minor,
	     le16_to_cpu(get_version_reply.build_no));


exit:
	dbg(2, "%s: leave, return value 0x%.8lx (dev)", __FUNCTION__, (long) dev);

	return retval;

error:
	tower_delete(dev);
	return retval;
}


/**
 *	tower_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 */
static void tower_disconnect (struct usb_interface *interface)
{
	struct lego_usb_tower *dev;
	int minor;

	dbg(2, "%s: enter", __FUNCTION__);

	dev = usb_get_intfdata (interface);
	mutex_lock(&open_disc_mutex);
	usb_set_intfdata (interface, NULL);

	minor = dev->minor;

	/* give back our minor */
	usb_deregister_dev (interface, &tower_class);

	down (&dev->sem);
	mutex_unlock(&open_disc_mutex);

	/* if the device is not opened, then we clean up right now */
	if (!dev->open_count) {
		up (&dev->sem);
		tower_delete (dev);
	} else {
		dev->udev = NULL;
		up (&dev->sem);
	}

	info("LEGO USB Tower #%d now disconnected", (minor - LEGO_USB_TOWER_MINOR_BASE));

	dbg(2, "%s: leave", __FUNCTION__);
}



/**
 *	lego_usb_tower_init
 */
static int __init lego_usb_tower_init(void)
{
	int result;
	int retval = 0;

	dbg(2, "%s: enter", __FUNCTION__);

	/* register this driver with the USB subsystem */
	result = usb_register(&tower_driver);
	if (result < 0) {
		err("usb_register failed for the "__FILE__" driver. Error number %d", result);
		retval = -1;
		goto exit;
	}

	info(DRIVER_DESC " " DRIVER_VERSION);

exit:
	dbg(2, "%s: leave, return value %d", __FUNCTION__, retval);

	return retval;
}


/**
 *	lego_usb_tower_exit
 */
static void __exit lego_usb_tower_exit(void)
{
	dbg(2, "%s: enter", __FUNCTION__);

	/* deregister this driver with the USB subsystem */
	usb_deregister (&tower_driver);

	dbg(2, "%s: leave", __FUNCTION__);
}

module_init (lego_usb_tower_init);
module_exit (lego_usb_tower_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
