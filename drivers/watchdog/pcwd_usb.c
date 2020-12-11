// SPDX-License-Identifier: GPL-2.0+
/*
 *	Berkshire USB-PC Watchdog Card Driver
 *
 *	(c) Copyright 2004-2007 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	Based on source code of the following authors:
 *	  Ken Hollis <kenji@bitgate.com>,
 *	  Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *	  Matt Domsch <Matt_Domsch@dell.com>,
 *	  Rob Radez <rob@osinvestor.com>,
 *	  Greg Kroah-Hartman <greg@kroah.com>
 *
 *	Neither Wim Van Sebroeck nor Iguana vzw. admit liability nor
 *	provide warranty for any of this software. This material is
 *	provided "AS-IS" and at no charge.
 *
 *	Thanks also to Simon Machell at Berkshire Products Inc. for
 *	providing the test hardware. More info is available at
 *	http://www.berkprod.com/ or http://www.pcwatchdog.com/
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>	/* For module specific items */
#include <linux/moduleparam.h>	/* For new moduleparam's */
#include <linux/types.h>	/* For standard types (like size_t) */
#include <linux/errno.h>	/* For the -ENODEV/... values */
#include <linux/kernel.h>	/* For printk/panic/... */
#include <linux/delay.h>	/* For mdelay function */
#include <linux/miscdevice.h>	/* For struct miscdevice */
#include <linux/watchdog.h>	/* For the watchdog specific items */
#include <linux/notifier.h>	/* For notifier support */
#include <linux/reboot.h>	/* For reboot_notifier stuff */
#include <linux/init.h>		/* For __init/__exit/... */
#include <linux/fs.h>		/* For file operations */
#include <linux/usb.h>		/* For USB functions */
#include <linux/slab.h>		/* For kmalloc, ... */
#include <linux/mutex.h>	/* For mutex locking */
#include <linux/hid.h>		/* For HID_REQ_SET_REPORT & HID_DT_REPORT */
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */


/* Module and Version Information */
#define DRIVER_VERSION "1.02"
#define DRIVER_AUTHOR "Wim Van Sebroeck <wim@iguana.be>"
#define DRIVER_DESC "Berkshire USB-PC Watchdog driver"
#define DRIVER_NAME "pcwd_usb"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define WATCHDOG_HEARTBEAT 0	/* default heartbeat =
						delay-time from dip-switches */
static int heartbeat = WATCHDOG_HEARTBEAT;
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds. "
	"(0<heartbeat<65536 or 0=delay-time from dip-switches, default="
				__MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* The vendor and product id's for the USB-PC Watchdog card */
#define USB_PCWD_VENDOR_ID	0x0c98
#define USB_PCWD_PRODUCT_ID	0x1140

/* table of devices that work with this driver */
static const struct usb_device_id usb_pcwd_table[] = {
	{ USB_DEVICE(USB_PCWD_VENDOR_ID, USB_PCWD_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, usb_pcwd_table);

/* according to documentation max. time to process a command for the USB
 * watchdog card is 100 or 200 ms, so we give it 250 ms to do it's job */
#define USB_COMMAND_TIMEOUT	250

/* Watchdog's internal commands */
#define CMD_READ_TEMP			0x02	/* Read Temperature;
							Re-trigger Watchdog */
#define CMD_TRIGGER			CMD_READ_TEMP
#define CMD_GET_STATUS			0x04	/* Get Status Information */
#define CMD_GET_FIRMWARE_VERSION	0x08	/* Get Firmware Version */
#define CMD_GET_DIP_SWITCH_SETTINGS	0x0c	/* Get Dip Switch Settings */
#define CMD_READ_WATCHDOG_TIMEOUT	0x18	/* Read Current Watchdog Time */
#define CMD_WRITE_WATCHDOG_TIMEOUT	0x19	/* Write Current WatchdogTime */
#define CMD_ENABLE_WATCHDOG		0x30	/* Enable / Disable Watchdog */
#define CMD_DISABLE_WATCHDOG		CMD_ENABLE_WATCHDOG

/* Watchdog's Dip Switch heartbeat values */
static const int heartbeat_tbl[] = {
	5,	/* OFF-OFF-OFF	=  5 Sec  */
	10,	/* OFF-OFF-ON	= 10 Sec  */
	30,	/* OFF-ON-OFF	= 30 Sec  */
	60,	/* OFF-ON-ON	=  1 Min  */
	300,	/* ON-OFF-OFF	=  5 Min  */
	600,	/* ON-OFF-ON	= 10 Min  */
	1800,	/* ON-ON-OFF	= 30 Min  */
	3600,	/* ON-ON-ON	=  1 hour */
};

/* We can only use 1 card due to the /dev/watchdog restriction */
static int cards_found;

/* some internal variables */
static unsigned long is_active;
static char expect_release;

/* Structure to hold all of our device specific stuff */
struct usb_pcwd_private {
	/* save off the usb device pointer */
	struct usb_device	*udev;
	/* the interface for this device */
	struct usb_interface	*interface;

	/* the interface number used for cmd's */
	unsigned int		interface_number;

	/* the buffer to intr data */
	unsigned char		*intr_buffer;
	/* the dma address for the intr buffer */
	dma_addr_t		intr_dma;
	/* the size of the intr buffer */
	size_t			intr_size;
	/* the urb used for the intr pipe */
	struct urb		*intr_urb;

	/* The command that is reported back */
	unsigned char		cmd_command;
	/* The data MSB that is reported back */
	unsigned char		cmd_data_msb;
	/* The data LSB that is reported back */
	unsigned char		cmd_data_lsb;
	/* true if we received a report after a command */
	atomic_t		cmd_received;

	/* Wether or not the device exists */
	int			exists;
	/* locks this structure */
	struct mutex		mtx;
};
static struct usb_pcwd_private *usb_pcwd_device;

/* prevent races between open() and disconnect() */
static DEFINE_MUTEX(disconnect_mutex);

/* local function prototypes */
static int usb_pcwd_probe(struct usb_interface *interface,
						const struct usb_device_id *id);
static void usb_pcwd_disconnect(struct usb_interface *interface);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver usb_pcwd_driver = {
	.name =		DRIVER_NAME,
	.probe =	usb_pcwd_probe,
	.disconnect =	usb_pcwd_disconnect,
	.id_table =	usb_pcwd_table,
};


static void usb_pcwd_intr_done(struct urb *urb)
{
	struct usb_pcwd_private *usb_pcwd =
				(struct usb_pcwd_private *)urb->context;
	unsigned char *data = usb_pcwd->intr_buffer;
	struct device *dev = &usb_pcwd->interface->dev;
	int retval;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d",
			__func__, urb->status);
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		dev_dbg(dev, "%s - nonzero urb status received: %d",
			__func__, urb->status);
		goto resubmit;
	}

	dev_dbg(dev, "received following data cmd=0x%02x msb=0x%02x lsb=0x%02x",
		data[0], data[1], data[2]);

	usb_pcwd->cmd_command  = data[0];
	usb_pcwd->cmd_data_msb = data[1];
	usb_pcwd->cmd_data_lsb = data[2];

	/* notify anyone waiting that the cmd has finished */
	atomic_set(&usb_pcwd->cmd_received, 1);

resubmit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		pr_err("can't resubmit intr, usb_submit_urb failed with result %d\n",
		       retval);
}

static int usb_pcwd_send_command(struct usb_pcwd_private *usb_pcwd,
		unsigned char cmd, unsigned char *msb, unsigned char *lsb)
{
	int got_response, count;
	unsigned char *buf;

	/* We will not send any commands if the USB PCWD device does
	 * not exist */
	if ((!usb_pcwd) || (!usb_pcwd->exists))
		return -1;

	buf = kmalloc(6, GFP_KERNEL);
	if (buf == NULL)
		return 0;

	/* The USB PC Watchdog uses a 6 byte report format.
	 * The board currently uses only 3 of the six bytes of the report. */
	buf[0] = cmd;			/* Byte 0 = CMD */
	buf[1] = *msb;			/* Byte 1 = Data MSB */
	buf[2] = *lsb;			/* Byte 2 = Data LSB */
	buf[3] = buf[4] = buf[5] = 0;	/* All other bytes not used */

	dev_dbg(&usb_pcwd->interface->dev,
		"sending following data cmd=0x%02x msb=0x%02x lsb=0x%02x",
		buf[0], buf[1], buf[2]);

	atomic_set(&usb_pcwd->cmd_received, 0);

	if (usb_control_msg(usb_pcwd->udev, usb_sndctrlpipe(usb_pcwd->udev, 0),
			HID_REQ_SET_REPORT, HID_DT_REPORT,
			0x0200, usb_pcwd->interface_number, buf, 6,
			USB_COMMAND_TIMEOUT) != 6) {
		dev_dbg(&usb_pcwd->interface->dev,
			"usb_pcwd_send_command: error in usb_control_msg for cmd 0x%x 0x%x 0x%x\n",
			cmd, *msb, *lsb);
	}
	/* wait till the usb card processed the command,
	 * with a max. timeout of USB_COMMAND_TIMEOUT */
	got_response = 0;
	for (count = 0; (count < USB_COMMAND_TIMEOUT) && (!got_response);
								count++) {
		mdelay(1);
		if (atomic_read(&usb_pcwd->cmd_received))
			got_response = 1;
	}

	if ((got_response) && (cmd == usb_pcwd->cmd_command)) {
		/* read back response */
		*msb = usb_pcwd->cmd_data_msb;
		*lsb = usb_pcwd->cmd_data_lsb;
	}

	kfree(buf);

	return got_response;
}

static int usb_pcwd_start(struct usb_pcwd_private *usb_pcwd)
{
	unsigned char msb = 0x00;
	unsigned char lsb = 0x00;
	int retval;

	/* Enable Watchdog */
	retval = usb_pcwd_send_command(usb_pcwd, CMD_ENABLE_WATCHDOG,
								&msb, &lsb);

	if ((retval == 0) || (lsb == 0)) {
		pr_err("Card did not acknowledge enable attempt\n");
		return -1;
	}

	return 0;
}

static int usb_pcwd_stop(struct usb_pcwd_private *usb_pcwd)
{
	unsigned char msb = 0xA5;
	unsigned char lsb = 0xC3;
	int retval;

	/* Disable Watchdog */
	retval = usb_pcwd_send_command(usb_pcwd, CMD_DISABLE_WATCHDOG,
								&msb, &lsb);

	if ((retval == 0) || (lsb != 0)) {
		pr_err("Card did not acknowledge disable attempt\n");
		return -1;
	}

	return 0;
}

static int usb_pcwd_keepalive(struct usb_pcwd_private *usb_pcwd)
{
	unsigned char dummy;

	/* Re-trigger Watchdog */
	usb_pcwd_send_command(usb_pcwd, CMD_TRIGGER, &dummy, &dummy);

	return 0;
}

static int usb_pcwd_set_heartbeat(struct usb_pcwd_private *usb_pcwd, int t)
{
	unsigned char msb = t / 256;
	unsigned char lsb = t % 256;

	if ((t < 0x0001) || (t > 0xFFFF))
		return -EINVAL;

	/* Write new heartbeat to watchdog */
	usb_pcwd_send_command(usb_pcwd, CMD_WRITE_WATCHDOG_TIMEOUT, &msb, &lsb);

	heartbeat = t;
	return 0;
}

static int usb_pcwd_get_temperature(struct usb_pcwd_private *usb_pcwd,
							int *temperature)
{
	unsigned char msb, lsb;

	usb_pcwd_send_command(usb_pcwd, CMD_READ_TEMP, &msb, &lsb);

	/*
	 * Convert celsius to fahrenheit, since this was
	 * the decided 'standard' for this return value.
	 */
	*temperature = (lsb * 9 / 5) + 32;

	return 0;
}

static int usb_pcwd_get_timeleft(struct usb_pcwd_private *usb_pcwd,
								int *time_left)
{
	unsigned char msb, lsb;

	/* Read the time that's left before rebooting */
	/* Note: if the board is not yet armed then we will read 0xFFFF */
	usb_pcwd_send_command(usb_pcwd, CMD_READ_WATCHDOG_TIMEOUT, &msb, &lsb);

	*time_left = (msb << 8) + lsb;

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static ssize_t usb_pcwd_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	/* See if we got the magic character 'V' and reload the timer */
	if (len) {
		if (!nowayout) {
			size_t i;

			/* note: just in case someone wrote the magic character
			 * five months ago... */
			expect_release = 0;

			/* scan to see whether or not we got the
			 * magic character */
			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}

		/* someone wrote to us, we should reload the timer */
		usb_pcwd_keepalive(usb_pcwd_device);
	}
	return len;
}

static long usb_pcwd_ioctl(struct file *file, unsigned int cmd,
						unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options =		WDIOF_KEEPALIVEPING |
					WDIOF_SETTIMEOUT |
					WDIOF_MAGICCLOSE,
		.firmware_version =	1,
		.identity =		DRIVER_NAME,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_GETTEMP:
	{
		int temperature;

		if (usb_pcwd_get_temperature(usb_pcwd_device, &temperature))
			return -EFAULT;

		return put_user(temperature, p);
	}

	case WDIOC_SETOPTIONS:
	{
		int new_options, retval = -EINVAL;

		if (get_user(new_options, p))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD) {
			usb_pcwd_stop(usb_pcwd_device);
			retval = 0;
		}

		if (new_options & WDIOS_ENABLECARD) {
			usb_pcwd_start(usb_pcwd_device);
			retval = 0;
		}

		return retval;
	}

	case WDIOC_KEEPALIVE:
		usb_pcwd_keepalive(usb_pcwd_device);
		return 0;

	case WDIOC_SETTIMEOUT:
	{
		int new_heartbeat;

		if (get_user(new_heartbeat, p))
			return -EFAULT;

		if (usb_pcwd_set_heartbeat(usb_pcwd_device, new_heartbeat))
			return -EINVAL;

		usb_pcwd_keepalive(usb_pcwd_device);
	}
		fallthrough;

	case WDIOC_GETTIMEOUT:
		return put_user(heartbeat, p);

	case WDIOC_GETTIMELEFT:
	{
		int time_left;

		if (usb_pcwd_get_timeleft(usb_pcwd_device, &time_left))
			return -EFAULT;

		return put_user(time_left, p);
	}

	default:
		return -ENOTTY;
	}
}

static int usb_pcwd_open(struct inode *inode, struct file *file)
{
	/* /dev/watchdog can only be opened once */
	if (test_and_set_bit(0, &is_active))
		return -EBUSY;

	/* Activate */
	usb_pcwd_start(usb_pcwd_device);
	usb_pcwd_keepalive(usb_pcwd_device);
	return stream_open(inode, file);
}

static int usb_pcwd_release(struct inode *inode, struct file *file)
{
	/*
	 *      Shut off the timer.
	 */
	if (expect_release == 42) {
		usb_pcwd_stop(usb_pcwd_device);
	} else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		usb_pcwd_keepalive(usb_pcwd_device);
	}
	expect_release = 0;
	clear_bit(0, &is_active);
	return 0;
}

/*
 *	/dev/temperature handling
 */

static ssize_t usb_pcwd_temperature_read(struct file *file, char __user *data,
				size_t len, loff_t *ppos)
{
	int temperature;

	if (usb_pcwd_get_temperature(usb_pcwd_device, &temperature))
		return -EFAULT;

	if (copy_to_user(data, &temperature, 1))
		return -EFAULT;

	return 1;
}

static int usb_pcwd_temperature_open(struct inode *inode, struct file *file)
{
	return stream_open(inode, file);
}

static int usb_pcwd_temperature_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	Notify system
 */

static int usb_pcwd_notify_sys(struct notifier_block *this, unsigned long code,
								void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		usb_pcwd_stop(usb_pcwd_device);	/* Turn the WDT off */

	return NOTIFY_DONE;
}

/*
 *	Kernel Interfaces
 */

static const struct file_operations usb_pcwd_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.write =	usb_pcwd_write,
	.unlocked_ioctl = usb_pcwd_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open =		usb_pcwd_open,
	.release =	usb_pcwd_release,
};

static struct miscdevice usb_pcwd_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&usb_pcwd_fops,
};

static const struct file_operations usb_pcwd_temperature_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		usb_pcwd_temperature_read,
	.open =		usb_pcwd_temperature_open,
	.release =	usb_pcwd_temperature_release,
};

static struct miscdevice usb_pcwd_temperature_miscdev = {
	.minor =	TEMP_MINOR,
	.name =		"temperature",
	.fops =		&usb_pcwd_temperature_fops,
};

static struct notifier_block usb_pcwd_notifier = {
	.notifier_call =	usb_pcwd_notify_sys,
};

/**
 *	usb_pcwd_delete
 */
static inline void usb_pcwd_delete(struct usb_pcwd_private *usb_pcwd)
{
	usb_free_urb(usb_pcwd->intr_urb);
	usb_free_coherent(usb_pcwd->udev, usb_pcwd->intr_size,
			  usb_pcwd->intr_buffer, usb_pcwd->intr_dma);
	kfree(usb_pcwd);
}

/**
 *	usb_pcwd_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 */
static int usb_pcwd_probe(struct usb_interface *interface,
						const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_pcwd_private *usb_pcwd = NULL;
	int pipe;
	int retval = -ENOMEM;
	int got_fw_rev;
	unsigned char fw_rev_major, fw_rev_minor;
	char fw_ver_str[20];
	unsigned char option_switches, dummy;

	cards_found++;
	if (cards_found > 1) {
		pr_err("This driver only supports 1 device\n");
		return -ENODEV;
	}

	/* get the active interface descriptor */
	iface_desc = interface->cur_altsetting;

	/* check out that we have a HID device */
	if (!(iface_desc->desc.bInterfaceClass == USB_CLASS_HID)) {
		pr_err("The device isn't a Human Interface Device\n");
		return -ENODEV;
	}

	if (iface_desc->desc.bNumEndpoints < 1)
		return -ENODEV;

	/* check out the endpoint: it has to be Interrupt & IN */
	endpoint = &iface_desc->endpoint[0].desc;

	if (!usb_endpoint_is_int_in(endpoint)) {
		/* we didn't find a Interrupt endpoint with direction IN */
		pr_err("Couldn't find an INTR & IN endpoint\n");
		return -ENODEV;
	}

	/* get a handle to the interrupt data pipe */
	pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);

	/* allocate memory for our device and initialize it */
	usb_pcwd = kzalloc(sizeof(struct usb_pcwd_private), GFP_KERNEL);
	if (usb_pcwd == NULL)
		goto error;

	usb_pcwd_device = usb_pcwd;

	mutex_init(&usb_pcwd->mtx);
	usb_pcwd->udev = udev;
	usb_pcwd->interface = interface;
	usb_pcwd->interface_number = iface_desc->desc.bInterfaceNumber;
	usb_pcwd->intr_size = (le16_to_cpu(endpoint->wMaxPacketSize) > 8 ?
				le16_to_cpu(endpoint->wMaxPacketSize) : 8);

	/* set up the memory buffer's */
	usb_pcwd->intr_buffer = usb_alloc_coherent(udev, usb_pcwd->intr_size,
					GFP_KERNEL, &usb_pcwd->intr_dma);
	if (!usb_pcwd->intr_buffer) {
		pr_err("Out of memory\n");
		goto error;
	}

	/* allocate the urb's */
	usb_pcwd->intr_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!usb_pcwd->intr_urb)
		goto error;

	/* initialise the intr urb's */
	usb_fill_int_urb(usb_pcwd->intr_urb, udev, pipe,
			usb_pcwd->intr_buffer, usb_pcwd->intr_size,
			usb_pcwd_intr_done, usb_pcwd, endpoint->bInterval);
	usb_pcwd->intr_urb->transfer_dma = usb_pcwd->intr_dma;
	usb_pcwd->intr_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* register our interrupt URB with the USB system */
	if (usb_submit_urb(usb_pcwd->intr_urb, GFP_KERNEL)) {
		pr_err("Problem registering interrupt URB\n");
		retval = -EIO; /* failure */
		goto error;
	}

	/* The device exists and can be communicated with */
	usb_pcwd->exists = 1;

	/* disable card */
	usb_pcwd_stop(usb_pcwd);

	/* Get the Firmware Version */
	got_fw_rev = usb_pcwd_send_command(usb_pcwd, CMD_GET_FIRMWARE_VERSION,
						&fw_rev_major, &fw_rev_minor);
	if (got_fw_rev)
		sprintf(fw_ver_str, "%u.%02u", fw_rev_major, fw_rev_minor);
	else
		sprintf(fw_ver_str, "<card no answer>");

	pr_info("Found card (Firmware: %s) with temp option\n", fw_ver_str);

	/* Get switch settings */
	usb_pcwd_send_command(usb_pcwd, CMD_GET_DIP_SWITCH_SETTINGS, &dummy,
							&option_switches);

	pr_info("Option switches (0x%02x): Temperature Reset Enable=%s, Power On Delay=%s\n",
		option_switches,
		((option_switches & 0x10) ? "ON" : "OFF"),
		((option_switches & 0x08) ? "ON" : "OFF"));

	/* If heartbeat = 0 then we use the heartbeat from the dip-switches */
	if (heartbeat == 0)
		heartbeat = heartbeat_tbl[(option_switches & 0x07)];

	/* Check that the heartbeat value is within it's range ;
	 * if not reset to the default */
	if (usb_pcwd_set_heartbeat(usb_pcwd, heartbeat)) {
		usb_pcwd_set_heartbeat(usb_pcwd, WATCHDOG_HEARTBEAT);
		pr_info("heartbeat value must be 0<heartbeat<65536, using %d\n",
			WATCHDOG_HEARTBEAT);
	}

	retval = register_reboot_notifier(&usb_pcwd_notifier);
	if (retval != 0) {
		pr_err("cannot register reboot notifier (err=%d)\n", retval);
		goto error;
	}

	retval = misc_register(&usb_pcwd_temperature_miscdev);
	if (retval != 0) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       TEMP_MINOR, retval);
		goto err_out_unregister_reboot;
	}

	retval = misc_register(&usb_pcwd_miscdev);
	if (retval != 0) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, retval);
		goto err_out_misc_deregister;
	}

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, usb_pcwd);

	pr_info("initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;

err_out_misc_deregister:
	misc_deregister(&usb_pcwd_temperature_miscdev);
err_out_unregister_reboot:
	unregister_reboot_notifier(&usb_pcwd_notifier);
error:
	if (usb_pcwd)
		usb_pcwd_delete(usb_pcwd);
	usb_pcwd_device = NULL;
	return retval;
}


/**
 *	usb_pcwd_disconnect
 *
 *	Called by the usb core when the device is removed from the system.
 *
 *	This routine guarantees that the driver will not submit any more urbs
 *	by clearing dev->udev.
 */
static void usb_pcwd_disconnect(struct usb_interface *interface)
{
	struct usb_pcwd_private *usb_pcwd;

	/* prevent races with open() */
	mutex_lock(&disconnect_mutex);

	usb_pcwd = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	mutex_lock(&usb_pcwd->mtx);

	/* Stop the timer before we leave */
	if (!nowayout)
		usb_pcwd_stop(usb_pcwd);

	/* We should now stop communicating with the USB PCWD device */
	usb_pcwd->exists = 0;

	/* Deregister */
	misc_deregister(&usb_pcwd_miscdev);
	misc_deregister(&usb_pcwd_temperature_miscdev);
	unregister_reboot_notifier(&usb_pcwd_notifier);

	mutex_unlock(&usb_pcwd->mtx);

	/* Delete the USB PCWD device */
	usb_pcwd_delete(usb_pcwd);

	cards_found--;

	mutex_unlock(&disconnect_mutex);

	pr_info("USB PC Watchdog disconnected\n");
}

module_usb_driver(usb_pcwd_driver);
