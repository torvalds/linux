/*
 * usblp.c
 *
 * Copyright (c) 1999 Michael Gee	<michael@linuxspecific.com>
 * Copyright (c) 1999 Pavel Machek	<pavel@ucw.cz>
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
 *		use usb_alloc_coherent() for read buf & write buf;
 *      none  - Maintained in Linux kernel after v0.13
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
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/lp.h>
#include <linux/mutex.h>
#undef DEBUG
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/ratelimit.h>

/*
 * Version Information
 */
#define DRIVER_AUTHOR "Michael Gee, Pavel Machek, Vojtech Pavlik, Randy Dunlap, Pete Zaitcev, David Paschal"
#define DRIVER_DESC "USB Printer Device Class driver"

#define USBLP_BUF_SIZE		8192
#define USBLP_BUF_SIZE_IN	1024
#define USBLP_DEVICE_ID_SIZE	1024

/* ioctls: */
#define IOCNR_GET_DEVICE_ID		1
#define IOCNR_GET_PROTOCOLS		2
#define IOCNR_SET_PROTOCOL		3
#define IOCNR_HP_SET_CHANNEL		4
#define IOCNR_GET_BUS_ADDRESS		5
#define IOCNR_GET_VID_PID		6
#define IOCNR_SOFT_RESET		7
/* Get device_id string: */
#define LPIOC_GET_DEVICE_ID(len) _IOC(_IOC_READ, 'P', IOCNR_GET_DEVICE_ID, len)
/* The following ioctls were added for http://hpoj.sourceforge.net:
 * Get two-int array:
 * [0]=current protocol
 *     (1=USB_CLASS_PRINTER/1/1, 2=USB_CLASS_PRINTER/1/2,
 *         3=USB_CLASS_PRINTER/1/3),
 * [1]=supported protocol mask (mask&(1<<n)!=0 means
 *     USB_CLASS_PRINTER/1/n supported):
 */
#define LPIOC_GET_PROTOCOLS(len) _IOC(_IOC_READ, 'P', IOCNR_GET_PROTOCOLS, len)
/*
 * Set protocol
 *     (arg: 1=USB_CLASS_PRINTER/1/1, 2=USB_CLASS_PRINTER/1/2,
 *         3=USB_CLASS_PRINTER/1/3):
 */
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

#define USBLP_CTL_TIMEOUT	5000			/* 5 seconds */

#define USBLP_FIRST_PROTOCOL	1
#define USBLP_LAST_PROTOCOL	3
#define USBLP_MAX_PROTOCOLS	(USBLP_LAST_PROTOCOL+1)

/*
 * some arbitrary status buffer size;
 * need a status buffer that is allocated via kmalloc(), not on stack
 */
#define STATUS_BUF_SIZE		8

/*
 * Locks down the locking order:
 * ->wmut locks wstatus.
 * ->mut locks the whole usblp, except [rw]complete, and thus, by indirection,
 * [rw]status. We only touch status when we know the side idle.
 * ->lock locks what interrupt accesses.
 */
struct usblp {
	struct usb_device	*dev;			/* USB device */
	struct mutex		wmut;
	struct mutex		mut;
	spinlock_t		lock;		/* locks rcomplete, wcomplete */
	char			*readbuf;		/* read transfer_buffer */
	char			*statusbuf;		/* status transfer_buffer */
	struct usb_anchor	urbs;
	wait_queue_head_t	rwait, wwait;
	int			readcount;		/* Counter for reads */
	int			ifnum;			/* Interface number */
	struct usb_interface	*intf;			/* The interface */
	/*
	 * Alternate-setting numbers and endpoints for each protocol
	 * (USB_CLASS_PRINTER/1/{index=1,2,3}) that the device supports:
	 */
	struct {
		int				alt_setting;
		struct usb_endpoint_descriptor	*epwrite;
		struct usb_endpoint_descriptor	*epread;
	}			protocol[USBLP_MAX_PROTOCOLS];
	int			current_protocol;
	int			minor;			/* minor number of device */
	int			wcomplete, rcomplete;
	int			wstatus;	/* bytes written or error */
	int			rstatus;	/* bytes ready or error */
	unsigned int		quirks;			/* quirks flags */
	unsigned int		flags;			/* mode flags */
	unsigned char		used;			/* True if open */
	unsigned char		present;		/* True if not disconnected */
	unsigned char		bidir;			/* interface is bidirectional */
	unsigned char		no_paper;		/* Paper Out happened */
	unsigned char		*device_id_string;	/* IEEE 1284 DEVICE ID string (ptr) */
							/* first 2 bytes are (big-endian) length */
};

#ifdef DEBUG
static void usblp_dump(struct usblp *usblp)
{
	struct device *dev = &usblp->intf->dev;
	int p;

	dev_dbg(dev, "usblp=0x%p\n", usblp);
	dev_dbg(dev, "dev=0x%p\n", usblp->dev);
	dev_dbg(dev, "present=%d\n", usblp->present);
	dev_dbg(dev, "readbuf=0x%p\n", usblp->readbuf);
	dev_dbg(dev, "readcount=%d\n", usblp->readcount);
	dev_dbg(dev, "ifnum=%d\n", usblp->ifnum);
	for (p = USBLP_FIRST_PROTOCOL; p <= USBLP_LAST_PROTOCOL; p++) {
		dev_dbg(dev, "protocol[%d].alt_setting=%d\n", p,
			usblp->protocol[p].alt_setting);
		dev_dbg(dev, "protocol[%d].epwrite=%p\n", p,
			usblp->protocol[p].epwrite);
		dev_dbg(dev, "protocol[%d].epread=%p\n", p,
			usblp->protocol[p].epread);
	}
	dev_dbg(dev, "current_protocol=%d\n", usblp->current_protocol);
	dev_dbg(dev, "minor=%d\n", usblp->minor);
	dev_dbg(dev, "wstatus=%d\n", usblp->wstatus);
	dev_dbg(dev, "rstatus=%d\n", usblp->rstatus);
	dev_dbg(dev, "quirks=%d\n", usblp->quirks);
	dev_dbg(dev, "used=%d\n", usblp->used);
	dev_dbg(dev, "bidir=%d\n", usblp->bidir);
	dev_dbg(dev, "device_id_string=\"%s\"\n",
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
#define USBLP_QUIRK_BAD_CLASS	0x4	/* descriptor uses vendor-specific Class or SubClass */

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
	{ 0x0482, 0x0010, USBLP_QUIRK_BIDIR }, /* Kyocera Mita FS 820, by zut <kernel@zut.de> */
	{ 0x04f9, 0x000d, USBLP_QUIRK_BIDIR }, /* Brother Industries, Ltd HL-1440 Laser Printer */
	{ 0x04b8, 0x0202, USBLP_QUIRK_BAD_CLASS }, /* Seiko Epson Receipt Printer M129C */
	{ 0, 0 }
};

static int usblp_wwait(struct usblp *usblp, int nonblock);
static int usblp_wtest(struct usblp *usblp, int nonblock);
static int usblp_rwait_and_lock(struct usblp *usblp, int nonblock);
static int usblp_rtest(struct usblp *usblp, int nonblock);
static int usblp_submit_read(struct usblp *usblp);
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
	if ((request == USBLP_REQ_GET_ID) && (type == USB_TYPE_CLASS))
		index = (usblp->ifnum<<8)|usblp->protocol[usblp->current_protocol].alt_setting;

	retval = usb_control_msg(usblp->dev,
		dir ? usb_rcvctrlpipe(usblp->dev, 0) : usb_sndctrlpipe(usblp->dev, 0),
		request, type | dir | recip, value, index, buf, len, USBLP_CTL_TIMEOUT);
	dev_dbg(&usblp->intf->dev,
		"usblp_control_msg: rq: 0x%02x dir: %d recip: %d value: %d idx: %d len: %#x result: %d\n",
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
	int status = urb->status;

	if (usblp->present && usblp->used) {
		if (status)
			printk(KERN_WARNING "usblp%d: "
			    "nonzero read bulk status received: %d\n",
			    usblp->minor, status);
	}
	spin_lock(&usblp->lock);
	if (status < 0)
		usblp->rstatus = status;
	else
		usblp->rstatus = urb->actual_length;
	usblp->rcomplete = 1;
	wake_up(&usblp->rwait);
	spin_unlock(&usblp->lock);

	usb_free_urb(urb);
}

static void usblp_bulk_write(struct urb *urb)
{
	struct usblp *usblp = urb->context;
	int status = urb->status;

	if (usblp->present && usblp->used) {
		if (status)
			printk(KERN_WARNING "usblp%d: "
			    "nonzero write bulk status received: %d\n",
			    usblp->minor, status);
	}
	spin_lock(&usblp->lock);
	if (status < 0)
		usblp->wstatus = status;
	else
		usblp->wstatus = urb->actual_length;
	usblp->no_paper = 0;
	usblp->wcomplete = 1;
	wake_up(&usblp->wwait);
	spin_unlock(&usblp->lock);

	usb_free_urb(urb);
}

/*
 * Get and print printer errors.
 */

static const char *usblp_messages[] = { "ok", "out of paper", "off-line", "on fire" };

static int usblp_check_status(struct usblp *usblp, int err)
{
	unsigned char status, newerr = 0;
	int error;

	mutex_lock(&usblp->mut);
	if ((error = usblp_read_status(usblp, usblp->statusbuf)) < 0) {
		mutex_unlock(&usblp->mut);
		printk_ratelimited(KERN_ERR
				"usblp%d: error %d reading printer status\n",
				usblp->minor, error);
		return 0;
	}
	status = *usblp->statusbuf;
	mutex_unlock(&usblp->mut);

	if (~status & LP_PERRORP)
		newerr = 3;
	if (status & LP_POUTPA)
		newerr = 1;
	if (~status & LP_PSELECD)
		newerr = 2;

	if (newerr != err) {
		printk(KERN_INFO "usblp%d: %s\n",
		   usblp->minor, usblp_messages[newerr]);
	}

	return newerr;
}

static int handle_bidir(struct usblp *usblp)
{
	if (usblp->bidir && usblp->used) {
		if (usblp_submit_read(usblp) < 0)
			return -EIO;
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

	mutex_lock(&usblp_mutex);

	retval = -ENODEV;
	intf = usb_find_interface(&usblp_driver, minor);
	if (!intf)
		goto out;
	usblp = usb_get_intfdata(intf);
	if (!usblp || !usblp->dev || !usblp->present)
		goto out;

	retval = -EBUSY;
	if (usblp->used)
		goto out;

	/*
	 * We do not implement LP_ABORTOPEN/LPABORTOPEN for two reasons:
	 *  - We do not want persistent state which close(2) does not clear
	 *  - It is not used anyway, according to CUPS people
	 */

	retval = usb_autopm_get_interface(intf);
	if (retval < 0)
		goto out;
	usblp->used = 1;
	file->private_data = usblp;

	usblp->wcomplete = 1; /* we begin writeable */
	usblp->wstatus = 0;
	usblp->rcomplete = 0;

	if (handle_bidir(usblp) < 0) {
		usb_autopm_put_interface(intf);
		usblp->used = 0;
		file->private_data = NULL;
		retval = -EIO;
	}
out:
	mutex_unlock(&usblp_mutex);
	return retval;
}

static void usblp_cleanup(struct usblp *usblp)
{
	printk(KERN_INFO "usblp%d: removed\n", usblp->minor);

	kfree(usblp->readbuf);
	kfree(usblp->device_id_string);
	kfree(usblp->statusbuf);
	kfree(usblp);
}

static void usblp_unlink_urbs(struct usblp *usblp)
{
	usb_kill_anchored_urbs(&usblp->urbs);
}

static int usblp_release(struct inode *inode, struct file *file)
{
	struct usblp *usblp = file->private_data;

	usblp->flags &= ~LP_ABORT;

	mutex_lock(&usblp_mutex);
	usblp->used = 0;
	if (usblp->present) {
		usblp_unlink_urbs(usblp);
		usb_autopm_put_interface(usblp->intf);
	} else		/* finish cleanup from disconnect */
		usblp_cleanup(usblp);
	mutex_unlock(&usblp_mutex);
	return 0;
}

/* No kernel lock - fine */
static unsigned int usblp_poll(struct file *file, struct poll_table_struct *wait)
{
	int ret;
	unsigned long flags;

	struct usblp *usblp = file->private_data;
	/* Should we check file->f_mode & FMODE_WRITE before poll_wait()? */
	poll_wait(file, &usblp->rwait, wait);
	poll_wait(file, &usblp->wwait, wait);
	spin_lock_irqsave(&usblp->lock, flags);
	ret = ((usblp->bidir && usblp->rcomplete) ? POLLIN  | POLLRDNORM : 0) |
	   ((usblp->no_paper || usblp->wcomplete) ? POLLOUT | POLLWRNORM : 0);
	spin_unlock_irqrestore(&usblp->lock, flags);
	return ret;
}

static long usblp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usblp *usblp = file->private_data;
	int length, err, i;
	unsigned char newChannel;
	int status;
	int twoints[2];
	int retval = 0;

	mutex_lock(&usblp->mut);
	if (!usblp->present) {
		retval = -ENODEV;
		goto done;
	}

	dev_dbg(&usblp->intf->dev,
		"usblp_ioctl: cmd=0x%x (%c nr=%d len=%d dir=%d)\n", cmd,
		_IOC_TYPE(cmd), _IOC_NR(cmd), _IOC_SIZE(cmd), _IOC_DIR(cmd));

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
				dev_err(&usblp->dev->dev,
					"usblp%d: error = %d setting "
					"HP channel\n",
					usblp->minor, err);
				retval = -EIO;
				goto done;
			}

			dev_dbg(&usblp->intf->dev,
				"usblp%d requested/got HP channel %ld/%d\n",
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

			dev_dbg(&usblp->intf->dev,
				"usblp%d is bus=%d, device=%d\n",
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

			dev_dbg(&usblp->intf->dev,
				"usblp%d is VID=0x%4.4X, PID=0x%4.4X\n",
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
			retval = usblp_read_status(usblp, usblp->statusbuf);
			if (retval) {
				printk_ratelimited(KERN_ERR "usblp%d:"
					    "failed reading printer status (%d)\n",
					    usblp->minor, retval);
				retval = -EIO;
				goto done;
			}
			status = *usblp->statusbuf;
			if (copy_to_user((void __user *)arg, &status, sizeof(int)))
				retval = -EFAULT;
			break;

		case LPABORT:
			if (arg)
				usblp->flags |= LP_ABORT;
			else
				usblp->flags &= ~LP_ABORT;
			break;

		default:
			retval = -ENOTTY;
		}

done:
	mutex_unlock(&usblp->mut);
	return retval;
}

static struct urb *usblp_new_writeurb(struct usblp *usblp, int transfer_length)
{
	struct urb *urb;
	char *writebuf;

	writebuf = kmalloc(transfer_length, GFP_KERNEL);
	if (writebuf == NULL)
		return NULL;
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb == NULL) {
		kfree(writebuf);
		return NULL;
	}

	usb_fill_bulk_urb(urb, usblp->dev,
		usb_sndbulkpipe(usblp->dev,
		 usblp->protocol[usblp->current_protocol].epwrite->bEndpointAddress),
		writebuf, transfer_length, usblp_bulk_write, usblp);
	urb->transfer_flags |= URB_FREE_BUFFER;

	return urb;
}

static ssize_t usblp_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;
	struct urb *writeurb;
	int rv;
	int transfer_length;
	ssize_t writecount = 0;

	if (mutex_lock_interruptible(&usblp->wmut)) {
		rv = -EINTR;
		goto raise_biglock;
	}
	if ((rv = usblp_wwait(usblp, !!(file->f_flags & O_NONBLOCK))) < 0)
		goto raise_wait;

	while (writecount < count) {
		/*
		 * Step 1: Submit next block.
		 */
		if ((transfer_length = count - writecount) > USBLP_BUF_SIZE)
			transfer_length = USBLP_BUF_SIZE;

		rv = -ENOMEM;
		writeurb = usblp_new_writeurb(usblp, transfer_length);
		if (writeurb == NULL)
			goto raise_urb;
		usb_anchor_urb(writeurb, &usblp->urbs);

		if (copy_from_user(writeurb->transfer_buffer,
				   buffer + writecount, transfer_length)) {
			rv = -EFAULT;
			goto raise_badaddr;
		}

		spin_lock_irq(&usblp->lock);
		usblp->wcomplete = 0;
		spin_unlock_irq(&usblp->lock);
		if ((rv = usb_submit_urb(writeurb, GFP_KERNEL)) < 0) {
			usblp->wstatus = 0;
			spin_lock_irq(&usblp->lock);
			usblp->no_paper = 0;
			usblp->wcomplete = 1;
			wake_up(&usblp->wwait);
			spin_unlock_irq(&usblp->lock);
			if (rv != -ENOMEM)
				rv = -EIO;
			goto raise_submit;
		}

		/*
		 * Step 2: Wait for transfer to end, collect results.
		 */
		rv = usblp_wwait(usblp, !!(file->f_flags&O_NONBLOCK));
		if (rv < 0) {
			if (rv == -EAGAIN) {
				/* Presume that it's going to complete well. */
				writecount += transfer_length;
			}
			if (rv == -ENOSPC) {
				spin_lock_irq(&usblp->lock);
				usblp->no_paper = 1;	/* Mark for poll(2) */
				spin_unlock_irq(&usblp->lock);
				writecount += transfer_length;
			}
			/* Leave URB dangling, to be cleaned on close. */
			goto collect_error;
		}

		if (usblp->wstatus < 0) {
			rv = -EIO;
			goto collect_error;
		}
		/*
		 * This is critical: it must be our URB, not other writer's.
		 * The wmut exists mainly to cover us here.
		 */
		writecount += usblp->wstatus;
	}

	mutex_unlock(&usblp->wmut);
	return writecount;

raise_submit:
raise_badaddr:
	usb_unanchor_urb(writeurb);
	usb_free_urb(writeurb);
raise_urb:
raise_wait:
collect_error:		/* Out of raise sequence */
	mutex_unlock(&usblp->wmut);
raise_biglock:
	return writecount ? writecount : rv;
}

/*
 * Notice that we fail to restart in a few cases: on EFAULT, on restart
 * error, etc. This is the historical behaviour. In all such cases we return
 * EIO, and applications loop in order to get the new read going.
 */
static ssize_t usblp_read(struct file *file, char __user *buffer, size_t len, loff_t *ppos)
{
	struct usblp *usblp = file->private_data;
	ssize_t count;
	ssize_t avail;
	int rv;

	if (!usblp->bidir)
		return -EINVAL;

	rv = usblp_rwait_and_lock(usblp, !!(file->f_flags & O_NONBLOCK));
	if (rv < 0)
		return rv;

	if ((avail = usblp->rstatus) < 0) {
		printk(KERN_ERR "usblp%d: error %d reading from printer\n",
		    usblp->minor, (int)avail);
		usblp_submit_read(usblp);
		count = -EIO;
		goto done;
	}

	count = len < avail - usblp->readcount ? len : avail - usblp->readcount;
	if (count != 0 &&
	    copy_to_user(buffer, usblp->readbuf + usblp->readcount, count)) {
		count = -EFAULT;
		goto done;
	}

	if ((usblp->readcount += count) == avail) {
		if (usblp_submit_read(usblp) < 0) {
			/* We don't want to leak USB return codes into errno. */
			if (count == 0)
				count = -EIO;
			goto done;
		}
	}

done:
	mutex_unlock(&usblp->mut);
	return count;
}

/*
 * Wait for the write path to come idle.
 * This is called under the ->wmut, so the idle path stays idle.
 *
 * Our write path has a peculiar property: it does not buffer like a tty,
 * but waits for the write to succeed. This allows our ->release to bug out
 * without waiting for writes to drain. But it obviously does not work
 * when O_NONBLOCK is set. So, applications setting O_NONBLOCK must use
 * select(2) or poll(2) to wait for the buffer to drain before closing.
 * Alternatively, set blocking mode with fcntl and issue a zero-size write.
 */
static int usblp_wwait(struct usblp *usblp, int nonblock)
{
	DECLARE_WAITQUEUE(waita, current);
	int rc;
	int err = 0;

	add_wait_queue(&usblp->wwait, &waita);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (mutex_lock_interruptible(&usblp->mut)) {
			rc = -EINTR;
			break;
		}
		rc = usblp_wtest(usblp, nonblock);
		mutex_unlock(&usblp->mut);
		if (rc <= 0)
			break;

		if (schedule_timeout(msecs_to_jiffies(1500)) == 0) {
			if (usblp->flags & LP_ABORT) {
				err = usblp_check_status(usblp, err);
				if (err == 1) {	/* Paper out */
					rc = -ENOSPC;
					break;
				}
			} else {
				/* Prod the printer, Gentoo#251237. */
				mutex_lock(&usblp->mut);
				usblp_read_status(usblp, usblp->statusbuf);
				mutex_unlock(&usblp->mut);
			}
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&usblp->wwait, &waita);
	return rc;
}

static int usblp_wtest(struct usblp *usblp, int nonblock)
{
	unsigned long flags;

	if (!usblp->present)
		return -ENODEV;
	if (signal_pending(current))
		return -EINTR;
	spin_lock_irqsave(&usblp->lock, flags);
	if (usblp->wcomplete) {
		spin_unlock_irqrestore(&usblp->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&usblp->lock, flags);
	if (nonblock)
		return -EAGAIN;
	return 1;
}

/*
 * Wait for read bytes to become available. This probably should have been
 * called usblp_r_lock_and_wait(), because we lock first. But it's a traditional
 * name for functions which lock and return.
 *
 * We do not use wait_event_interruptible because it makes locking iffy.
 */
static int usblp_rwait_and_lock(struct usblp *usblp, int nonblock)
{
	DECLARE_WAITQUEUE(waita, current);
	int rc;

	add_wait_queue(&usblp->rwait, &waita);
	for (;;) {
		if (mutex_lock_interruptible(&usblp->mut)) {
			rc = -EINTR;
			break;
		}
		set_current_state(TASK_INTERRUPTIBLE);
		if ((rc = usblp_rtest(usblp, nonblock)) < 0) {
			mutex_unlock(&usblp->mut);
			break;
		}
		if (rc == 0)	/* Keep it locked */
			break;
		mutex_unlock(&usblp->mut);
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&usblp->rwait, &waita);
	return rc;
}

static int usblp_rtest(struct usblp *usblp, int nonblock)
{
	unsigned long flags;

	if (!usblp->present)
		return -ENODEV;
	if (signal_pending(current))
		return -EINTR;
	spin_lock_irqsave(&usblp->lock, flags);
	if (usblp->rcomplete) {
		spin_unlock_irqrestore(&usblp->lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&usblp->lock, flags);
	if (nonblock)
		return -EAGAIN;
	return 1;
}

/*
 * Please check ->bidir and other such things outside for now.
 */
static int usblp_submit_read(struct usblp *usblp)
{
	struct urb *urb;
	unsigned long flags;
	int rc;

	rc = -ENOMEM;
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (urb == NULL)
		goto raise_urb;

	usb_fill_bulk_urb(urb, usblp->dev,
		usb_rcvbulkpipe(usblp->dev,
		  usblp->protocol[usblp->current_protocol].epread->bEndpointAddress),
		usblp->readbuf, USBLP_BUF_SIZE_IN,
		usblp_bulk_read, usblp);
	usb_anchor_urb(urb, &usblp->urbs);

	spin_lock_irqsave(&usblp->lock, flags);
	usblp->readcount = 0; /* XXX Why here? */
	usblp->rcomplete = 0;
	spin_unlock_irqrestore(&usblp->lock, flags);
	if ((rc = usb_submit_urb(urb, GFP_KERNEL)) < 0) {
		dev_dbg(&usblp->intf->dev, "error submitting urb (%d)\n", rc);
		spin_lock_irqsave(&usblp->lock, flags);
		usblp->rstatus = rc;
		usblp->rcomplete = 1;
		spin_unlock_irqrestore(&usblp->lock, flags);
		goto raise_submit;
	}

	return 0;

raise_submit:
	usb_unanchor_urb(urb);
	usb_free_urb(urb);
raise_urb:
	return rc;
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
static unsigned int usblp_quirks(__u16 vendor, __u16 product)
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
	.llseek =	noop_llseek,
};

static char *usblp_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "usb/%s", dev_name(dev));
}

static struct usb_class_driver usblp_class = {
	.name =		"lp%d",
	.devnode =	usblp_devnode,
	.fops =		&usblp_fops,
	.minor_base =	USBLP_MINOR_BASE,
};

static ssize_t usblp_show_ieee1284_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usblp *usblp = usb_get_intfdata(intf);

	if (usblp->device_id_string[0] == 0 &&
	    usblp->device_id_string[1] == 0)
		return 0;

	return sprintf(buf, "%s", usblp->device_id_string+2);
}

static DEVICE_ATTR(ieee1284_id, S_IRUGO, usblp_show_ieee1284_id, NULL);

static int usblp_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usblp *usblp;
	int protocol;
	int retval;

	/* Malloc and start initializing usblp structure so we can use it
	 * directly. */
	usblp = kzalloc(sizeof(struct usblp), GFP_KERNEL);
	if (!usblp) {
		retval = -ENOMEM;
		goto abort_ret;
	}
	usblp->dev = dev;
	mutex_init(&usblp->wmut);
	mutex_init(&usblp->mut);
	spin_lock_init(&usblp->lock);
	init_waitqueue_head(&usblp->rwait);
	init_waitqueue_head(&usblp->wwait);
	init_usb_anchor(&usblp->urbs);
	usblp->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	usblp->intf = intf;

	/* Malloc device ID string buffer to the largest expected length,
	 * since we can re-query it on an ioctl and a dynamic string
	 * could change in length. */
	if (!(usblp->device_id_string = kmalloc(USBLP_DEVICE_ID_SIZE, GFP_KERNEL))) {
		retval = -ENOMEM;
		goto abort;
	}

	/*
	 * Allocate read buffer. We somewhat wastefully
	 * malloc both regardless of bidirectionality, because the
	 * alternate setting can be changed later via an ioctl.
	 */
	if (!(usblp->readbuf = kmalloc(USBLP_BUF_SIZE_IN, GFP_KERNEL))) {
		retval = -ENOMEM;
		goto abort;
	}

	/* Allocate buffer for printer status */
	usblp->statusbuf = kmalloc(STATUS_BUF_SIZE, GFP_KERNEL);
	if (!usblp->statusbuf) {
		retval = -ENOMEM;
		goto abort;
	}

	/* Lookup quirks for this printer. */
	usblp->quirks = usblp_quirks(
		le16_to_cpu(dev->descriptor.idVendor),
		le16_to_cpu(dev->descriptor.idProduct));

	/* Analyze and pick initial alternate settings and endpoints. */
	protocol = usblp_select_alts(usblp);
	if (protocol < 0) {
		dev_dbg(&intf->dev,
			"incompatible printer-class device 0x%4.4X/0x%4.4X\n",
			le16_to_cpu(dev->descriptor.idVendor),
			le16_to_cpu(dev->descriptor.idProduct));
		retval = -ENODEV;
		goto abort;
	}

	/* Setup the selected alternate setting and endpoints. */
	if (usblp_set_protocol(usblp, protocol) < 0) {
		retval = -ENODEV;	/* ->probe isn't ->ioctl */
		goto abort;
	}

	/* Retrieve and store the device ID string. */
	usblp_cache_device_id_string(usblp);
	retval = device_create_file(&intf->dev, &dev_attr_ieee1284_id);
	if (retval)
		goto abort_intfdata;

#ifdef DEBUG
	usblp_check_status(usblp, 0);
#endif

	usb_set_intfdata(intf, usblp);

	usblp->present = 1;

	retval = usb_register_dev(intf, &usblp_class);
	if (retval) {
		dev_err(&intf->dev,
			"usblp: Not able to get a minor (base %u, slice default): %d\n",
			USBLP_MINOR_BASE, retval);
		goto abort_intfdata;
	}
	usblp->minor = intf->minor;
	dev_info(&intf->dev,
		"usblp%d: USB %sdirectional printer dev %d if %d alt %d proto %d vid 0x%4.4X pid 0x%4.4X\n",
		usblp->minor, usblp->bidir ? "Bi" : "Uni", dev->devnum,
		usblp->ifnum,
		usblp->protocol[usblp->current_protocol].alt_setting,
		usblp->current_protocol,
		le16_to_cpu(usblp->dev->descriptor.idVendor),
		le16_to_cpu(usblp->dev->descriptor.idProduct));

	return 0;

abort_intfdata:
	usb_set_intfdata(intf, NULL);
	device_remove_file(&intf->dev, &dev_attr_ieee1284_id);
abort:
	kfree(usblp->readbuf);
	kfree(usblp->statusbuf);
	kfree(usblp->device_id_string);
	kfree(usblp);
abort_ret:
	return retval;
}

/*
 * We are a "new" style driver with usb_device_id table,
 * but our requirements are too intricate for simple match to handle.
 *
 * The "proto_bias" option may be used to specify the preferred protocol
 * for all USB printers (1=USB_CLASS_PRINTER/1/1, 2=USB_CLASS_PRINTER/1/2,
 * 3=USB_CLASS_PRINTER/1/3).  If the device supports the preferred protocol,
 * then we bind to it.
 *
 * The best interface for us is USB_CLASS_PRINTER/1/2, because it
 * is compatible with a stream of characters. If we find it, we bind to it.
 *
 * Note that the people from hpoj.sourceforge.net need to be able to
 * bind to USB_CLASS_PRINTER/1/3 (MLC/1284.4), so we provide them ioctls
 * for this purpose.
 *
 * Failing USB_CLASS_PRINTER/1/2, we look for USB_CLASS_PRINTER/1/3,
 * even though it's probably not stream-compatible, because this matches
 * the behaviour of the old code.
 *
 * If nothing else, we bind to USB_CLASS_PRINTER/1/1
 * - the unidirectional interface.
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

		if (ifd->desc.bInterfaceClass != USB_CLASS_PRINTER ||
		    ifd->desc.bInterfaceSubClass != 1)
			if (!(usblp->quirks & USBLP_QUIRK_BAD_CLASS))
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

		/*
		 * Turn off reads for USB_CLASS_PRINTER/1/1 (unidirectional)
		 * interfaces and buggy bidirectional printers.
		 */
		if (ifd->desc.bInterfaceProtocol == 1) {
			epread = NULL;
		} else if (usblp->quirks & USBLP_QUIRK_BIDIR) {
			printk(KERN_INFO "usblp%d: Disabling reads from "
			    "problematic bidirectional printer\n",
			    usblp->minor);
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
		printk(KERN_ERR "usblp: can't set desired altsetting %d on interface %d\n",
			alts, usblp->ifnum);
		return r;
	}

	usblp->bidir = (usblp->protocol[protocol].epread != NULL);
	usblp->current_protocol = protocol;
	dev_dbg(&usblp->intf->dev, "usblp%d set protocol %d\n",
		usblp->minor, protocol);
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
		dev_dbg(&usblp->intf->dev,
			"usblp%d: error = %d reading IEEE-1284 Device ID string\n",
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

	dev_dbg(&usblp->intf->dev, "usblp%d Device ID string [len=%d]=\"%s\"\n",
		usblp->minor, length, &usblp->device_id_string[2]);

	return length;
}

static void usblp_disconnect(struct usb_interface *intf)
{
	struct usblp *usblp = usb_get_intfdata(intf);

	usb_deregister_dev(intf, &usblp_class);

	if (!usblp || !usblp->dev) {
		dev_err(&intf->dev, "bogus disconnect\n");
		BUG();
	}

	device_remove_file(&intf->dev, &dev_attr_ieee1284_id);

	mutex_lock(&usblp_mutex);
	mutex_lock(&usblp->mut);
	usblp->present = 0;
	wake_up(&usblp->wwait);
	wake_up(&usblp->rwait);
	usb_set_intfdata(intf, NULL);

	usblp_unlink_urbs(usblp);
	mutex_unlock(&usblp->mut);

	if (!usblp->used)
		usblp_cleanup(usblp);
	mutex_unlock(&usblp_mutex);
}

static int usblp_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usblp *usblp = usb_get_intfdata(intf);

	usblp_unlink_urbs(usblp);
#if 0 /* XXX Do we want this? What if someone is reading, should we fail? */
	/* not strictly necessary, but just in case */
	wake_up(&usblp->wwait);
	wake_up(&usblp->rwait);
#endif

	return 0;
}

static int usblp_resume(struct usb_interface *intf)
{
	struct usblp *usblp = usb_get_intfdata(intf);
	int r;

	r = handle_bidir(usblp);

	return r;
}

static const struct usb_device_id usblp_ids[] = {
	{ USB_DEVICE_INFO(USB_CLASS_PRINTER, 1, 1) },
	{ USB_DEVICE_INFO(USB_CLASS_PRINTER, 1, 2) },
	{ USB_DEVICE_INFO(USB_CLASS_PRINTER, 1, 3) },
	{ USB_INTERFACE_INFO(USB_CLASS_PRINTER, 1, 1) },
	{ USB_INTERFACE_INFO(USB_CLASS_PRINTER, 1, 2) },
	{ USB_INTERFACE_INFO(USB_CLASS_PRINTER, 1, 3) },
	{ USB_DEVICE(0x04b8, 0x0202) },	/* Seiko Epson Receipt Printer M129C */
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usblp_ids);

static struct usb_driver usblp_driver = {
	.name =		"usblp",
	.probe =	usblp_probe,
	.disconnect =	usblp_disconnect,
	.suspend =	usblp_suspend,
	.resume =	usblp_resume,
	.id_table =	usblp_ids,
	.supports_autosuspend =	1,
};

module_usb_driver(usblp_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
module_param(proto_bias, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(proto_bias, "Favourite protocol number");
MODULE_LICENSE("GPL");
