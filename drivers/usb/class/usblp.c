/*
 * usblp.c  Version 0.13
 *
 * Copyright (c) 1999 Michael Gee	<michael@linuxspecific.com>
 * Copyright (c) 1999 Pavel Machek	<pavel@suse.cz>
 * Copyright (c) 2000 Randy Dunlap	<rdunlap@xenotime.net>
 * Copyright (c) 2000 Vojtech Pavlik	<vojtech@suse.cz>
 # Copyright (c) 2001 Pete Zaitcev	<zaitcev@redhat.com>
 # Copyright (c) 2001 David Paschal	<paschal@rcsis.com>
 * Copyright (c) 2006 Oliver Neukum	<oliver@neukum.name>
 *
 * USB Printer Device Class driver for USB printers and printer cables
 *
 * Sponsored by SuSE
 *
 * ChangeLog:
 *	v0.1 - thorough cleaning, URBification, almost a rewrite
 *	v0.2 - some more cleanups
 *	v0.3 - cleaner again, waitqueue fixes
 *	v0.4 - fixes in unidirectional mode
 *	v0.5 - add DEVICE_ID string support
 *	v0.6 - never time out
 *	v0.7 - fixed bulk-IN read and poll (David Paschal)
 *	v0.8 - add devfs support
 *	v0.9 - fix unplug-while-open paths
 *	v0.10- remove sleep_on, fix error on oom (oliver@neukum.org)
 *	v0.11 - add proto_bias option (Pete Zaitcev)
 *	v0.12 - add hpoj.sourceforge.net ioctls (David Paschal)
 *	v0.13 - alloc space for statusbuf (<status> not on stack);
 *		use usb_buffer_alloc() for read buf & write buf;
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/lp.h>
#include <linux/mutex.h>
#undef DEBUG
#include <linux/usb.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.13"
#define DRIVER_AUTHOR "Michael Gee, Pavel Machek, Vojtech Pavlik, Randy Dunlap, Pete Zaitcev, David Paschal"
#define DRIVER_DESC "USB Printer Device Class driver"

#define USBLP_BUF_SIZE		8192
#define USBLP_DEVICE_ID_SIZE	1024

/* ioctls: */
#define LPGETSTATUS		0x060b		/* same as in drivers/char/lp.c */
#define IOCNR_GET_DEVICE_ID		1
#define IOCNR_GET_PROTOCOLS		2
#define IOCNR_SET_PROTOCOL		3
#define IOCNR_HP_SET_CHANNEL		4
#define IOCNR_GET_BUS_ADDRESS		5
#define IOCNR_GET_VID_PID		6
#define IOCNR_SOFT_RESET		7
/* Get device_id string: */
#define LPIOC_GET_DEVICE_ID(len) _IOC(_IOC_READ, 'P', IOCNR_GET_DEVICE_ID, len)
/* The following ioctls were added for http://hpoj.sourceforge.net: */
/* Get two-int array:
 * [0]=current protocol (1=7/1/1, 2=7/1/2, 3=7/1/3),
 * [1]=supported protocol mask (mask&(1<<n)!=0 means 7/1/n supported): */
#define LPIOC_GET_PROTOCOLS(len) _IOC(_IOC_READ, 'P', IOCNR_GET_PROTOCOLS, len)
/* Set protocol (arg: 1=7/1/1, 2=7/1/2, 3=7/1/3): */
#define LPIOC_SET_PROTOCOL _IOC(_IOC_WRITE, 'P', IOCNR_SET_PROTOCOL, 0)
/* Set channel number (HP Vendor-specific command): */
#define LPIOC_HP_SET_CHANNEL _IOC(_IOC_WRITE, 'P', IOCNR_HP_SET_CHANNEL, 0)
/* Get two-int array: [0]=bus number, [1]=device address: */
#define LPIOC_GET_BUS_ADDRESS(len) _IOC(_IOC_READ, 'P', IOCNR_GET_BUS_ADDRESS, len)
/* Get two-int array: [0]=vendor ID, [1]=product ID: */
#define LPIOC_GET_VID_PID(len) _IOC(_IOC_READ, 'P', IOCNR_GET_VID_PID, len)
/* Perform class specific soft reset */
#define LPIOC_SOFT_RESET _IOC(_IOC_NONE, 'P', IOCNR_SOFT_RESET, 0);

/*
 * A DEVICE_ID string may include the printer's serial number.
 * It should end with a semi-colon (';').
 * An example from an HP 970C DeskJet printer is (this is one long string,
 * with the serial number changed):
MFG:HEWLETT-PACKARD;MDL:DESKJET 970C;CMD:MLC,PCL,PML;CLASS:PRINTER;DESCRIPTION:Hewlett-Packard DeskJet 970C;SERN:US970CSEPROF;VSTATUS:$HB0$NC0,ff,DN,IDLE,CUT,K1,C0,DP,NR,KP000,CP027;VP:0800,FL,B0;VJ:                    ;
 */

/*
 * USB Printer Requests
 */

#define USBLP_REQ_GET_ID			0x00
#define USBLP_REQ_GET_STATUS			0x01
#define USBLP_REQ_RESET				0x02
#define USBLP_REQ_HP_CHANNEL_CHANGE_REQUEST	0x00	/* HP Vendor-specific */

#define USBLP_MINORS		16
#define USBLP_MINOR_BASE	0

#define USBLP_WRITE_TIMEOUT	(5000)			/* 5 seconds */

#define USBLP_FIRST_PROTOCOL	1
#define USBLP_LAST_PROTOCOL	3
#define USBLP_MAX_PROTOCOLS	(USBLP_LAST_PROTOCOL+1)

/*
 * some arbitrary status buffer size;
 * need a status buffer that is allocated via kmalloc(), not on stack
 */
#define STATUS_BUF_SIZE		8

struct usblp {
	struct usb_device 	*dev;			/* USB device */
	struct semaphore	sem;			/* locks this struct, especially "dev" */
	char			*writebuf;		/* write transfer_buffer */
	char			*readbuf;		/* read transfer_buffer */
	char			*statusbuf;		/* status transfer_buffer */
	struct urb		*readurb, *writeurb;	/* The urbs */
	wait_queue_head_t	wait;			/* Zzzzz ... */
	int			readcount;		/* Counter for reads */
	int			ifnum;			/* Interface number */
	struct usb_interface	*intf;			/* The interface */
	/* Alternate-setting numbers and endpoints for each protocol
	 * (7/1/{index=1,2,3}) that the device supports: */
	struct {
		int				alt_setting;
		struct usb_endpoint_descriptor	*epwrite;
		struct usb_endpoint_descriptor	*epread;
	}			protocol[USBLP_MAX_PROTOCOLS];
	int			current_protocol;
	int			minor;			/* minor number of device */
	int			wcomplete;		/* writing is completed */
	int			rcomplete;		/* reading is completed */
	unsigned int		quirks;			/* quirks flags */
	unsigned char		used;			/* True if open */
	unsigned char		present;		/* True if not disconnected */
	unsigned char		bidir;			/* interface is bidirectional */
	unsigned char		sleeping;		/* interface is suspended */
	unsigned char		*device_id_string;	/* IEEE 1284 DEVICE ID string (ptr) */
							/* first 2 bytes are (big-endian) length */
};

#ifdef DEBUG
static void usblp_dump(struct usblp *usblp) {
	int p;

	dbg("usblp=0x%p", usblp);
	dbg("dev=0x%p", usblp->dev);
	dbg("present=%d", usblp->present);
	dbg("readbuf=0x%p", usblp->readbuf);
	dbg("writebuf=0x%p", usblp->writebuf);
	dbg("readurb=0x%p", usblp->readurb);
	dbg("writeurb=0x%p", usblp->writeurb);
	dbg("readcount=%d", usblp->readcount);
	dbg("ifnum=%d", usblp->ifnum);
    for (p = USBLP_FIRST_PROTOCOL; p <= USBLP_LAST_PROTOCOL; p++) {
	dbg("protocol[%d].alt_setting=%d", p, usblp->protocol[p].alt_setting);
	dbg("protocol[%d].epwrite=%p", p, usblp->protocol[p].epwrite);
	dbg("protocol[%d].epread=%p", p, usblp->protocol[p].epread);
    }
	dbg("current_protocol=%d", usblp->current_protocol);
	dbg("minor=%d", usblp->minor);
	dbg("wcomplete=%d", usblp->wcomplete);
	dbg("rcomplete=%d", usblp->rcomplete);
	dbg("quirks=%d", usblp->quirks);
	dbg("used=%d", usblp->used);
	dbg("bidir=%d", usblp->bidir);
	dbg("sleeping=%d", usblp->sleeping);
	dbg("device_id_string=\"%s\"",
		usblp->device_id_string ?
			usblp->device_id_string + 2 :
			(unsigned char *)"(null)");
}
#endif

/* Quirks: various printer quirks are handled by this table & its flags. */

struct quirk_printer_struct {
	__u16 vendorId;
	__u16 productId;
	unsigned int quirks;
};

#define USBLP_QUIRK_BIDIR	0x1	/* reports bidir but requires unidirectional mode (no INs/reads) */
#define USBLP_QUIRK_USB_INIT	0x2	/* needs vendor USB init string */

static const struct quirk_printer_struct quirk_printers[] = {
	{ 0x03f0, 0x0004, USBLP_QUIRK_BIDIR }, /* HP DeskJet 895C */
	{ 0x03f0, 0x0104, USBLP_QUIRK_BIDIR }, /* HP DeskJet 880C */
	{ 0x03f0, 0x0204, USBLP_QUIRK_BIDIR }, /* HP DeskJet 815C */
	{ 0x03f0, 0x0304, USBLP_QUIRK_BIDIR }, /* HP DeskJet 810C/812C */
	{ 0x03f0, 0x0404, USBLP_QUIRK_BIDIR }, /* HP DeskJet 830C */
	{ 0x03f0, 0x0504, USBLP_QUIRK_BIDIR }, /* HP DeskJet 885C */
	{ 0x03f0, 0x0604, USBLP_QUIRK_BIDIR }, /* HP DeskJet 840C */   
	{ 0x03f0, 0x0804, USBLP_QUIRK_BIDIR }, /* HP DeskJet 816C */   
	{ 0x03f0, 0x1104, USBLP_QUIRK_BIDIR }, /* HP Deskjet 959C */
	{ 0x0409, 0xefbe, USBLP_QUIRK_BIDIR }, /* NEC Picty900 (HP OEM) */
	{ 0x0409, 0xbef4, USBLP_QUIRK_BIDIR }, /* NEC Picty760 (HP OEM) */
	{ 0x0409, 0xf0be, USBLP_QUIRK_BIDIR }, /* NEC Picty920 (HP OEM) */
	{ 0x0409, 0xf1be, USBLP_QUIRK_BIDIR }, /* NEC Picty800 (HP OEM) */
	{ 0, 0 }
};

static int usblp_select_alts(struct usblp *usblp);
static int usblp_set_protocol(struct usblp *usblp, int protocol);
static int usblp_cache_device_id_string(struct usblp *usblp);

/* forward reference to make our lives easier */
static struct usb_driver usblp_driver;
static DEFINE_MUTEX(usblp_mutex);	/* locks the existence of usblp's */

/*
 * Functions for usblp control messages.
 */

static int usblp_ctrl_msg(struct usblp *usblp, int request, int type, int dir, int recip, int value, void *buf, int len)
{
	int retval;
	int index = usblp->ifnum;

	/* High byte has the interface index.
	   Low byte has the alternate setting.
	 */
	if ((request == USBLP_REQ_GET_ID) && (type == USB_TYPE_CLASS)) {
	  index = (usblp->ifnum<<8)|usblp->protocol[usblp->current_protocol].alt_setting;
	}

	retval = usb_control_msg(usblp->dev,
		dir ? usb_rcvctrlpipe(usblp->dev, 0) : usb_sndctrlpipe(usblp->dev, 0),
		request, type | dir | recip, value, index, buf, len, USBLP_WRITE_TIMEOUT);
	dbg("usblp_control_msg: rq: 0x%02x dir: %d recip: %d value: %d idx: %d len: %#x result: %d",
		request, !!dir, recip, value, index, len, retval);
	return retval < 0 ? retval : 0;
}

#define usblp_read_status(usblp, status)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_STATUS, USB_TYPE_CLASS, USB_DIR_IN, USB_RECIP_INTERFACE, 0, status, 1)
#define usblp_get_id(usblp, config, id, maxlen)\
	usblp_ctrl_msg(usblp, USBLP_REQ_GET_ID, USB_TYPE_CLASS, USB_DIR_IN, USB_RECIP_INTERFACE, config, id, maxlen)
#define usblp_reset(usblp)\
	usblp_ctrl_msg(usblp, USBLP_REQ_RESET, USB_TYPE_CLASS, USB_DIR_OUT, USB_RECIP_OTHER, 0, NULL, 0)

#define usblp_hp_channel_change_request(usblp, channel, buffer) \
	usblp_ctrl_msg(usblp, USBLP_REQ_HP_CHANNEL_CHANGE_REQUEST, USB_TYPE_VENDOR, USB_DIR_IN, USB_RECIP_INTERFACE, channel, buffer, 1)

/*
 * See the description for usblp_select_alts() below for the usage
 * explanation.  Look into your /proc/bus/usb/devices and dmesg in
 * case of any trouble.
 */
static int proto_bias = -1;

/*
 * URB callback.
 */

static void usblp_bulk_read(struct urb *urb)
{
	struct usblp *usblp = urb->context;

	if (unlikely(!usblp || !usblp->dev || !usblp->used))
		return;

	if (unlikely(!usblp->present))
		goto unplug;
	if (unlikely(urb->status))
		warn("usblp%d: nonzero read/write bulk status received: %d",
			usblp->minor, urb->status);
	usblp->rcomplete = 1;
unplug:
	wake_up_interruptible(&usblp->wait);
}

static void usblp_bulk_write(struct urb *urb)
{
	struct usblp *usblp = urb->context;

	if (unlikely(!usblp || !usblp->dev || !usblp->used))
		return;
	if (unlikely(!usblp->present))
		goto unplug;
	if (unlikely(urb->status))
		warn("usblp%d: nonzero read/write bulk status received: %d",
			usblp->minor, urb->status);
	usblp->wcomplete = 1;
unplug:
	wake_up_interruptible(&usblp->wait);
}

/*
 * Get and print printer errors.
 */

static const char *usblp_messages[] = { "ok", "out of paper", "off-line", "on fire" };

static int usblp_check_status(struct usblp *usblp, int err)
{
	unsigned char status, newerr = 0;
	int error;

	error = usblp_read_status (usblp, usblp->statusbuf);
	if (error < 0) {
		if (printk_ratelimit())
			err("usblp%d: error %d reading printer status",
				usblp->minor, error);
		return 0;
	}

	status = *usblp->statusbuf;

	if (~status & LP_PERRORP)
		newerr = 3;
	if (status & LP_POUTPA)
		newerr = 1;
	if (~status & LP_PSELECD)
		newerr = 2;

	if (newerr != err)
		info("usblp%d: %s", usblp->minor, usblp_messages[newerr]);

	return newerr;
}

static int handle_bidir (struct usblp *usblp)
{
	if (usblp->bidir && usblp->used && !usblp->sleeping) {
		usblp->readcount = 0;
		usblp->readurb->dev = usblp->dev;
		if (usb_submit_urb(usblp->readurb, GFP_KERNEL) < 0) {
			usblp->used = 0;
			return -EIO;
		}
	}

	return 0;
}

/*
 * File op functions.
 */

static int usblp_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct usblp *usblp;
	struct usb_interface *intf;
	int retval;

	if (minor < 0)
		return -ENODEV;

	mutex_lock (&usblp_mutex);

	retval = -ENODEV;
	intf = usb_find_interface(&usblp_driver, minor);
	if (!intf) {
		goto out;
	}
	usblp = usb_get_intfdata (intf);
	if (!usblp || !usblp->dev || !usblp->present)
		goto out;

	retval = -EBUSY;
	if (usblp->used)
		goto out;

	/*
	 * TODO: need to implement LP_ABORTOPEN + O_NONBLOCK as in drivers/char/lp.c ???
	 * This is #if 0-ed because we *don't* want to fail an open
	 * just because the printer is off-line.
	 */
#if 0
	if ((retval = usblp_check_status(usblp, 0))) {
		retval = retval > 1 ? -EIO : -ENOSPC;
		goto out;
	}
#else
	retval = 0;
#endif

	usblp->used = 1;
	file->private_data = usblp;

	usblp->writeurb->transfer_buffer_length = 0;
	usblp->wcomplete = 1; /* we begin writeable */
	usblp->rcomplete = 0;
	usblp->writeurb->status = 0;
	usblp->readurb->status = 0;

	if (handle_bidir(usblp) < 0) {
		file->private_data = NULL;
		retval = -EIO;
	}
out:
	mutex_unlock (&usblp_mutex);
	return retval;
}

static void usblp_cleanup (struct usblp *usblp)
{
	info("usblp%d: removed", usblp->minor);

	kfree (usblp->device_id_string);
	kfree (usblp->statusbuf);
	usb_free_urb(usblp->writeurb);
	usb_free_urb(usblp->readurb);
	kfree (usblp);
}

static void usblp_unlink_urbs(struct usblp *usblp)
{
	usb_kill_urb(usblp->writeurb);
	if (usblp->bidir)
		usb_kill_urb(usblp->readurb);
}

static int usblp_release(struct inode *inode, struct file *file)
{
	struct usblp *usblp = file->private_data;

	mutex_lock (&usblp_mutex);
	usblp->used = 0;
	if (usblp->present) {
		usblp_unlink_urbs(usblp);
	} else 		/* finish cleanup from disconnect */
		usblp_cleanup (usblp);
	mutex_unlock (&usblp_mutex);
	return 0;
}

/* No kernel lock - fine */
static unsigned int usblp_poll(struct file *file, struct poll_table_struct *wait)
{
	struct usblp *usblp = file->private_data;
	poll_wait(file, &usblp->wait, wait);
 	return ((!usblp->bidir || !usblp->rcomplete) ? 0 : POLLIN  | POLLRDNORM)
 			       | (!usblp->wcomplete ? 0 : POLLOUT | POLLWRNORM);
}

static long usblp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usblp *usblp = file->private_data;
	int length, err, i;
	unsigned char newChannel;
	int status;
	int twoints[2];
	int retval = 0;

	down (&usblp->sem);
	if (!usblp->present) {
		retval = -ENODEV;
		goto done;
	}

	if (usblp->sleeping) {
		retval = -ENODEV;
		goto done;
	}

	dbg("usblp_ioctl: cmd=0x%x (%c nr=%d len=%d dir=%d)", cmd, _IOC_TYPE(cmd),
		_IOC_NR(cmd), _IOC_SIZE(cmd), _IOC_DIR(cmd) );

	if (_IOC_TYPE(cmd) == 'P')	/* new-style ioctl number */

		switch (_IOC_NR(cmd)) {

			case IOCNR_GET_DEVICE_ID: /* get the DEVICE_ID string */
				if (_IOC_DIR(cmd) != _IOC_READ) {
					retval = -EINVAL;
					goto done;
				}

				length = usblp_cache_device_id_string(usblp);
				if (length < 0) {
					retval = length;
					goto done;
				}
				if (length > _IOC_SIZE(cmd))
					length = _IOC_SIZE(cmd); /* truncate */

				if (copy_to_user((void __user *) arg,
						usblp->device_id_string,
						(unsigned long) length)) {
					retval = -EFAULT;
					goto done;
				}

				break;

			case IOCNR_GET_PROTOCOLS:
				if (_IOC_DIR(cmd) != _IOC_READ ||
				    _IOC_SIZE(cmd) < sizeof(twoints)) {
					retval = -EINVAL;
					goto done;
				}

				twoints[0] = usblp->current_protocol;
				twoints[1] = 0;
				for (i = USBLP_FIRST_PROTOCOL;
				     i <= USBLP_LAST_PROTOCOL; i++) {
					if (usblp->protocol[i].alt_setting >= 0)
						twoints[1] |= (1<<i);
				}

				if (copy_to_user((void __user *)arg,
						(unsigned char *)twoints,
						sizeof(twoints))) {
					retval = -EFAULT;
					goto done;
				}

				break;

			case IOCNR_SET_PROTOCOL:
				if (_IOC_DIR(cmd) != _IOC_WRITE) {
					retval = -EINVAL;
					goto done;
				}

#ifdef DEBUG
				if (arg == -10) {
					usblp_dump(usblp);
					break;
				}
#endif

				usblp_unlink_urbs(usblp);
				retval = usblp_set_protocol(usblp, arg);
				if (retval < 0) {
					usblp_set_protocol(usblp,
						usblp->current_protocol);
				}
				break;

			case IOCNR_HP_SET_CHANNEL:
				if (_IOC_DIR(cmd) != _IOC_WRITE ||
				    le16_to_cpu(usblp->dev->descriptor.idVendor) != 0x03F0 ||
				    usblp->quirks & USBLP_QUIRK_BIDIR) {
					retval = -EINVAL;
					goto done;
				}

				err = usblp_hp_channel_change_request(usblp,
					arg, &newChannel);
				if (err < 0) {
					err("usblp%d: error = %d setting "
						"HP channel",
						usblp->minor, err);
					retval = -EIO;
					goto done;
				}

				dbg("usblp%d requested/got HP channel %ld/%d",
					usblp->minor, arg, newChannel);
				break;

			case IOCNR_GET_BUS_ADDRESS:
				if (_IOC_DIR(cmd) != _IOC_READ ||
				    _IOC_SIZE(cmd) < sizeof(twoints)) {
					retval = -EINVAL;
					goto done;
				}

				twoints[0] = usblp->dev->bus->busnum;
				twoints[1] = usblp->dev->devnum;
				if (copy_to_user((void __user *)arg,
						(unsigned char *)twoints,
						sizeof(twoints))) {
					retval = -EFAULT;
					goto done;
				}

				dbg("usblp%d is bus=%d, device=%d",
					usblp->minor, twoints[0], twoints[1]);
				break;

			case IOCNR_GET_VID_PID:
				if (_IOC_DIR(cmd) != _IOC_READ ||
				    _IOC_SIZE(cmd) < sizeof(twoints)) {
					retval = -EINVAL;
					goto done;
				}

				twoints[0] = le16_to_cpu(usblp->dev->descriptor.idVendor);
				twoints[1] = le16_to_cpu(usblp->dev->descriptor.idProduct);
				if (copy_to_user((void __user *)arg,
						(unsigned char *)twoints,
						sizeof(twoints))) {
					retval = -EFAULT;
					goto done;
				}

				dbg("usblp%d is VID=0x%4.4X, PID=0x%4.4X",
					usblp->minor, twoints[0], twoints[1]);
				break;

			case IOCNR_SOFT_RESET:
				if (_IOC_DIR(cmd) != _IOC_NONE) {
					retval = -EINVAL;
					goto done;
				}
				retval = usblp_reset(usblp);
				break;
			default:
				retval = -ENOTTY;
		}
	else	/* old-style ioctl value */
		switch (cmd) {

			case LPGETSTATUS:
				if (usblp_read_status(usblp, usblp->statusbuf)) {
					if (printk_ratelimit())
						err("usblp%d: failed reading printer status",
							usblp->minor);
					retval = -EIO;
					goto done;
				}
				status = *usblp->statusbuf;
				if (copy_to_user ((void __user *)arg, &status, sizeof(int)))
					retval = -EFAULT;
				break;

			default:
				retval = -ENOTTY;
		}

done:
	up (&usblp->sem);
	return retval;
}

static ssize_t usblp_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;
	int timeout, rv, err = 0, transfer_length = 0;
	size_t writecount = 0;

	while (writecount < count) {
		if (!usblp->wcomplete) {
			barrier();
			if (file->f_flags & O_NONBLOCK) {
				writecount += transfer_length;
				return writecount ? writecount : -EAGAIN;
			}

			timeout = USBLP_WRITE_TIMEOUT;

			rv = wait_event_interruptible_timeout(usblp->wait, usblp->wcomplete || !usblp->present , timeout);
			if (rv < 0)
				return writecount ? writecount : -EINTR;
		}
		down (&usblp->sem);
		if (!usblp->present) {
			up (&usblp->sem);
			return -ENODEV;
		}

		if (usblp->sleeping) {
			up (&usblp->sem);
			return writecount ? writecount : -ENODEV;
		}

		if (usblp->writeurb->status != 0) {
			if (usblp->quirks & USBLP_QUIRK_BIDIR) {
				if (!usblp->wcomplete)
					err("usblp%d: error %d writing to printer",
						usblp->minor, usblp->writeurb->status);
				err = usblp->writeurb->status;
			} else
				err = usblp_check_status(usblp, err);
			up (&usblp->sem);

			/* if the fault was due to disconnect, let khubd's
			 * call to usblp_disconnect() grab usblp->sem ...
			 */
			schedule ();
			continue;
		}

		/* We must increment writecount here, and not at the
		 * end of the loop. Otherwise, the final loop iteration may
		 * be skipped, leading to incomplete printer output.
		 */
		writecount += transfer_length;
		if (writecount == count) {
			up(&usblp->sem);
			break;
		}

		transfer_length=(count - writecount);
		if (transfer_length > USBLP_BUF_SIZE)
			transfer_length = USBLP_BUF_SIZE;

		usblp->writeurb->transfer_buffer_length = transfer_length;

		if (copy_from_user(usblp->writeurb->transfer_buffer, 
				   buffer + writecount, transfer_length)) {
			up(&usblp->sem);
			return writecount ? writecount : -EFAULT;
		}

		usblp->writeurb->dev = usblp->dev;
		usblp->wcomplete = 0;
		err = usb_submit_urb(usblp->writeurb, GFP_KERNEL);
		if (err) {
			if (err != -ENOMEM)
				count = -EIO;
			else
				count = writecount ? writecount : -ENOMEM;
			up (&usblp->sem);
			break;
		}
		up (&usblp->sem);
	}

	return count;
}

static ssize_t usblp_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;
	int rv;

	if (!usblp->bidir)
		return -EINVAL;

	down (&usblp->sem);
	if (!usblp->present) {
		count = -ENODEV;
		goto done;
	}

	if (!usblp->rcomplete) {
		barrier();

		if (file->f_flags & O_NONBLOCK) {
			count = -EAGAIN;
			goto done;
		}
		up(&usblp->sem);
		rv = wait_event_interruptible(usblp->wait, usblp->rcomplete || !usblp->present);
		down(&usblp->sem);
		if (rv < 0) {
			count = -EINTR;
			goto done;
		}
	}

	if (!usblp->present) {
		count = -ENODEV;
		goto done;
	}

	if (usblp->sleeping) {
		count = -ENODEV;
		goto done;
	}

	if (usblp->readurb->status) {
		err("usblp%d: error %d reading from printer",
			usblp->minor, usblp->readurb->status);
		usblp->readurb->dev = usblp->dev;
 		usblp->readcount = 0;
		usblp->rcomplete = 0;
		if (usb_submit_urb(usblp->readurb, GFP_KERNEL) < 0)
			dbg("error submitting urb");
		count = -EIO;
		goto done;
	}

	count = count < usblp->readurb->actual_length - usblp->readcount ?
		count :	usblp->readurb->actual_length - usblp->readcount;

	if (copy_to_user(buffer, usblp->readurb->transfer_buffer + usblp->readcount, count)) {
		count = -EFAULT;
		goto done;
	}

	if ((usblp->readcount += count) == usblp->readurb->actual_length) {
		usblp->readcount = 0;
		usblp->readurb->dev = usblp->dev;
		usblp->rcomplete = 0;
		if (usb_submit_urb(usblp->readurb, GFP_KERNEL)) {
			count = -EIO;
			goto done;
		}
	}

done:
	up (&usblp->sem);
	return count;
}

/*
 * Checks for printers that have quirks, such as requiring unidirectional
 * communication but reporting bidirectional; currently some HP printers
 * have this flaw (HP 810, 880, 895, etc.), or needing an init string
 * sent at each open (like some Epsons).
 * Returns 1 if found, 0 if not found.
 *
 * HP recommended that we use the bidirectional interface but
 * don't attempt any bulk IN transfers from the IN endpoint.
 * Here's some more detail on the problem:
 * The problem is not that it isn't bidirectional though. The problem
 * is that if you request a device ID, or status information, while
 * the buffers are full, the return data will end up in the print data
 * buffer. For example if you make sure you never request the device ID
 * while you are sending print data, and you don't try to query the
 * printer status every couple of milliseconds, you will probably be OK.
 */
static unsigned int usblp_quirks (__u16 vendor, __u16 product)
{
	int i;

	for (i = 0; quirk_printers[i].vendorId; i++) {
		if (vendor == quirk_printers[i].vendorId &&
		    product == quirk_printers[i].productId)
			return quirk_printers[i].quirks;
 	}
	return 0;
}

static const struct file_operations usblp_fops = {
	.owner =	THIS_MODULE,
	.read =		usblp_read,
	.write =	usblp_write,
	.poll =		usblp_poll,
	.unlocked_ioctl =	usblp_ioctl,
	.compat_ioctl =		usblp_ioctl,
	.open =		usblp_open,
	.release =	usblp_release,
};

static struct usb_class_driver usblp_class = {
	.name =		"lp%d",
	.fops =		&usblp_fops,
	.minor_base =	USBLP_MINOR_BASE,
};

static ssize_t usblp_show_ieee1284_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usblp *usblp = usb_get_intfdata (intf);

	if (usblp->device_id_string[0] == 0 &&
	    usblp->device_id_string[1] == 0)
		return 0;

	return sprintf(buf, "%s", usblp->device_id_string+2);
}

static DEVICE_ATTR(ieee1284_id, S_IRUGO, usblp_show_ieee1284_id, NULL);

static int usblp_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev (intf);
	struct usblp *usblp = NULL;
	int protocol;
	int retval;

	/* Malloc and start initializing usblp structure so we can use it
	 * directly. */
	if (!(usblp = kzalloc(sizeof(struct usblp), GFP_KERNEL))) {
		err("out of memory for usblp");
		goto abort;
	}
	usblp->dev = dev;
	init_MUTEX (&usblp->sem);
	init_waitqueue_head(&usblp->wait);
	usblp->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	usblp->intf = intf;

	usblp->writeurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!usblp->writeurb) {
		err("out of memory");
		goto abort;
	}
	usblp->readurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!usblp->readurb) {
		err("out of memory");
		goto abort;
	}

	/* Malloc device ID string buffer to the largest expected length,
	 * since we can re-query it on an ioctl and a dynamic string
	 * could change in length. */
	if (!(usblp->device_id_string = kmalloc(USBLP_DEVICE_ID_SIZE, GFP_KERNEL))) {
		err("out of memory for device_id_string");
		goto abort;
	}

	usblp->writebuf = usblp->readbuf = NULL;
	usblp->writeurb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	usblp->readurb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	/* Malloc write & read buffers.  We somewhat wastefully
	 * malloc both regardless of bidirectionality, because the
	 * alternate setting can be changed later via an ioctl. */
	if (!(usblp->writebuf = usb_buffer_alloc(dev, USBLP_BUF_SIZE,
				GFP_KERNEL, &usblp->writeurb->transfer_dma))) {
		err("out of memory for write buf");
		goto abort;
	}
	if (!(usblp->readbuf = usb_buffer_alloc(dev, USBLP_BUF_SIZE,
				GFP_KERNEL, &usblp->readurb->transfer_dma))) {
		err("out of memory for read buf");
		goto abort;
	}

	/* Allocate buffer for printer status */
	usblp->statusbuf = kmalloc(STATUS_BUF_SIZE, GFP_KERNEL);
	if (!usblp->statusbuf) {
		err("out of memory for statusbuf");
		goto abort;
	}

	/* Lookup quirks for this printer. */
	usblp->quirks = usblp_quirks(
		le16_to_cpu(dev->descriptor.idVendor),
		le16_to_cpu(dev->descriptor.idProduct));

	/* Analyze and pick initial alternate settings and endpoints. */
	protocol = usblp_select_alts(usblp);
	if (protocol < 0) {
		dbg("incompatible printer-class device 0x%4.4X/0x%4.4X",
			le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct));
		goto abort;
	}

	/* Setup the selected alternate setting and endpoints. */
	if (usblp_set_protocol(usblp, protocol) < 0)
		goto abort;

	/* Retrieve and store the device ID string. */
	usblp_cache_device_id_string(usblp);
	retval = device_create_file(&intf->dev, &dev_attr_ieee1284_id);
	if (retval)
		goto abort_intfdata;

#ifdef DEBUG
	usblp_check_status(usblp, 0);
#endif

	usb_set_intfdata (intf, usblp);

	usblp->present = 1;

	retval = usb_register_dev(intf, &usblp_class);
	if (retval) {
		err("Not able to get a minor for this device.");
		goto abort_intfdata;
	}
	usblp->minor = intf->minor;
	info("usblp%d: USB %sdirectional printer dev %d "
		"if %d alt %d proto %d vid 0x%4.4X pid 0x%4.4X",
		usblp->minor, usblp->bidir ? "Bi" : "Uni", dev->devnum,
		usblp->ifnum,
		usblp->protocol[usblp->current_protocol].alt_setting,
		usblp->current_protocol,
		le16_to_cpu(usblp->dev->descriptor.idVendor),
		le16_to_cpu(usblp->dev->descriptor.idProduct));

	return 0;

abort_intfdata:
	usb_set_intfdata (intf, NULL);
	device_remove_file(&intf->dev, &dev_attr_ieee1284_id);
abort:
	if (usblp) {
		if (usblp->writebuf)
			usb_buffer_free (usblp->dev, USBLP_BUF_SIZE,
				usblp->writebuf, usblp->writeurb->transfer_dma);
		if (usblp->readbuf)
			usb_buffer_free (usblp->dev, USBLP_BUF_SIZE,
				usblp->readbuf, usblp->writeurb->transfer_dma);
		kfree(usblp->statusbuf);
		kfree(usblp->device_id_string);
		usb_free_urb(usblp->writeurb);
		usb_free_urb(usblp->readurb);
		kfree(usblp);
	}
	return -EIO;
}

/*
 * We are a "new" style driver with usb_device_id table,
 * but our requirements are too intricate for simple match to handle.
 *
 * The "proto_bias" option may be used to specify the preferred protocol
 * for all USB printers (1=7/1/1, 2=7/1/2, 3=7/1/3).  If the device
 * supports the preferred protocol, then we bind to it.
 *
 * The best interface for us is 7/1/2, because it is compatible
 * with a stream of characters. If we find it, we bind to it.
 *
 * Note that the people from hpoj.sourceforge.net need to be able to
 * bind to 7/1/3 (MLC/1284.4), so we provide them ioctls for this purpose.
 *
 * Failing 7/1/2, we look for 7/1/3, even though it's probably not
 * stream-compatible, because this matches the behaviour of the old code.
 *
 * If nothing else, we bind to 7/1/1 - the unidirectional interface.
 */
static int usblp_select_alts(struct usblp *usblp)
{
	struct usb_interface *if_alt;
	struct usb_host_interface *ifd;
	struct usb_endpoint_descriptor *epd, *epwrite, *epread;
	int p, i, e;

	if_alt = usblp->intf;

	for (p = 0; p < USBLP_MAX_PROTOCOLS; p++)
		usblp->protocol[p].alt_setting = -1;

	/* Find out what we have. */
	for (i = 0; i < if_alt->num_altsetting; i++) {
		ifd = &if_alt->altsetting[i];

		if (ifd->desc.bInterfaceClass != 7 || ifd->desc.bInterfaceSubClass != 1)
			continue;

		if (ifd->desc.bInterfaceProtocol < USBLP_FIRST_PROTOCOL ||
		    ifd->desc.bInterfaceProtocol > USBLP_LAST_PROTOCOL)
			continue;

		/* Look for bulk OUT and IN endpoints. */
		epwrite = epread = NULL;
		for (e = 0; e < ifd->desc.bNumEndpoints; e++) {
			epd = &ifd->endpoint[e].desc;

			if (usb_endpoint_is_bulk_out(epd))
				if (!epwrite)
					epwrite = epd;

			if (usb_endpoint_is_bulk_in(epd))
				if (!epread)
					epread = epd;
		}

		/* Ignore buggy hardware without the right endpoints. */
		if (!epwrite || (ifd->desc.bInterfaceProtocol > 1 && !epread))
			continue;

		/* Turn off reads for 7/1/1 (unidirectional) interfaces
		 * and buggy bidirectional printers. */
		if (ifd->desc.bInterfaceProtocol == 1) {
			epread = NULL;
		} else if (usblp->quirks & USBLP_QUIRK_BIDIR) {
			info("Disabling reads from problem bidirectional "
				"printer on usblp%d", usblp->minor);
			epread = NULL;
		}

		usblp->protocol[ifd->desc.bInterfaceProtocol].alt_setting =
				ifd->desc.bAlternateSetting;
		usblp->protocol[ifd->desc.bInterfaceProtocol].epwrite = epwrite;
		usblp->protocol[ifd->desc.bInterfaceProtocol].epread = epread;
	}

	/* If our requested protocol is supported, then use it. */
	if (proto_bias >= USBLP_FIRST_PROTOCOL &&
	    proto_bias <= USBLP_LAST_PROTOCOL &&
	    usblp->protocol[proto_bias].alt_setting != -1)
		return proto_bias;

	/* Ordering is important here. */
	if (usblp->protocol[2].alt_setting != -1)
		return 2;
	if (usblp->protocol[1].alt_setting != -1)
		return 1;
	if (usblp->protocol[3].alt_setting != -1)
		return 3;

	/* If nothing is available, then don't bind to this device. */
	return -1;
}

static int usblp_set_protocol(struct usblp *usblp, int protocol)
{
	int r, alts;

	if (protocol < USBLP_FIRST_PROTOCOL || protocol > USBLP_LAST_PROTOCOL)
		return -EINVAL;

	alts = usblp->protocol[protocol].alt_setting;
	if (alts < 0)
		return -EINVAL;
	r = usb_set_interface(usblp->dev, usblp->ifnum, alts);
	if (r < 0) {
		err("can't set desired altsetting %d on interface %d",
			alts, usblp->ifnum);
		return r;
	}

	usb_fill_bulk_urb(usblp->writeurb, usblp->dev,
		usb_sndbulkpipe(usblp->dev,
		  usblp->protocol[protocol].epwrite->bEndpointAddress),
		usblp->writebuf, 0,
		usblp_bulk_write, usblp);

	usblp->bidir = (usblp->protocol[protocol].epread != NULL);
	if (usblp->bidir)
		usb_fill_bulk_urb(usblp->readurb, usblp->dev,
			usb_rcvbulkpipe(usblp->dev,
			  usblp->protocol[protocol].epread->bEndpointAddress),
			usblp->readbuf, USBLP_BUF_SIZE,
			usblp_bulk_read, usblp);

	usblp->current_protocol = protocol;
	dbg("usblp%d set protocol %d", usblp->minor, protocol);
	return 0;
}

/* Retrieves and caches device ID string.
 * Returns length, including length bytes but not null terminator.
 * On error, returns a negative errno value. */
static int usblp_cache_device_id_string(struct usblp *usblp)
{
	int err, length;

	err = usblp_get_id(usblp, 0, usblp->device_id_string, USBLP_DEVICE_ID_SIZE - 1);
	if (err < 0) {
		dbg("usblp%d: error = %d reading IEEE-1284 Device ID string",
			usblp->minor, err);
		usblp->device_id_string[0] = usblp->device_id_string[1] = '\0';
		return -EIO;
	}

	/* First two bytes are length in big-endian.
	 * They count themselves, and we copy them into
	 * the user's buffer. */
	length = be16_to_cpu(*((__be16 *)usblp->device_id_string));
	if (length < 2)
		length = 2;
	else if (length >= USBLP_DEVICE_ID_SIZE)
		length = USBLP_DEVICE_ID_SIZE - 1;
	usblp->device_id_string[length] = '\0';

	dbg("usblp%d Device ID string [len=%d]=\"%s\"",
		usblp->minor, length, &usblp->device_id_string[2]);

	return length;
}

static void usblp_disconnect(struct usb_interface *intf)
{
	struct usblp *usblp = usb_get_intfdata (intf);

	usb_deregister_dev(intf, &usblp_class);

	if (!usblp || !usblp->dev) {
		err("bogus disconnect");
		BUG ();
	}

	device_remove_file(&intf->dev, &dev_attr_ieee1284_id);

	mutex_lock (&usblp_mutex);
	down (&usblp->sem);
	usblp->present = 0;
	usb_set_intfdata (intf, NULL);

	usblp_unlink_urbs(usblp);
	usb_buffer_free (usblp->dev, USBLP_BUF_SIZE,
			usblp->writebuf, usblp->writeurb->transfer_dma);
	usb_buffer_free (usblp->dev, USBLP_BUF_SIZE,
			usblp->readbuf, usblp->readurb->transfer_dma);
	up (&usblp->sem);

	if (!usblp->used)
		usblp_cleanup (usblp);
	mutex_unlock (&usblp_mutex);
}

static int usblp_suspend (struct usb_interface *intf, pm_message_t message)
{
	struct usblp *usblp = usb_get_intfdata (intf);

	/* this races against normal access and open */
	mutex_lock (&usblp_mutex);
	down (&usblp->sem);
	/* we take no more IO */
	usblp->sleeping = 1;
	/* we wait for anything printing */
	wait_event (usblp->wait, usblp->wcomplete || !usblp->present);
	usblp_unlink_urbs(usblp);
	up (&usblp->sem);
	mutex_unlock (&usblp_mutex);

	return 0;
}

static int usblp_resume (struct usb_interface *intf)
{
	struct usblp *usblp = usb_get_intfdata (intf);
	int r;

	mutex_lock (&usblp_mutex);
	down (&usblp->sem);

	usblp->sleeping = 0;
	r = handle_bidir (usblp);

	up (&usblp->sem);
	mutex_unlock (&usblp_mutex);

	return r;
}

static struct usb_device_id usblp_ids [] = {
	{ USB_DEVICE_INFO(7, 1, 1) },
	{ USB_DEVICE_INFO(7, 1, 2) },
	{ USB_DEVICE_INFO(7, 1, 3) },
	{ USB_INTERFACE_INFO(7, 1, 1) },
	{ USB_INTERFACE_INFO(7, 1, 2) },
	{ USB_INTERFACE_INFO(7, 1, 3) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usblp_ids);

static struct usb_driver usblp_driver = {
	.name =		"usblp",
	.probe =	usblp_probe,
	.disconnect =	usblp_disconnect,
	.suspend =	usblp_suspend,
	.resume =	usblp_resume,
	.id_table =	usblp_ids,
};

static int __init usblp_init(void)
{
	int retval;
	retval = usb_register(&usblp_driver);
	if (!retval)
		info(DRIVER_VERSION ": " DRIVER_DESC);

	return retval;
}

static void __exit usblp_exit(void)
{
	usb_deregister(&usblp_driver);
}

module_init(usblp_init);
module_exit(usblp_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
module_param(proto_bias, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(proto_bias, "Favourite protocol number");
MODULE_LICENSE("GPL");
