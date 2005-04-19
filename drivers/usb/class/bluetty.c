/*
 * bluetty.c   Version 0.13
 *
 * Copyright (C) 2000, 2001 Greg Kroah-Hartman	<greg@kroah.com>
 * Copyright (C) 2000 Mark Douglas Corner	<mcorner@umich.edu>
 *
 * USB Bluetooth TTY driver, based on the Bluetooth Spec version 1.0B
 * 
 * (2001/11/30) Version 0.13 gkh
 *	- added locking patch from Masoodur Rahman <rmasoodu@in.ibm.com>
 *	- removed active variable, as open_count will do.
 *
 * (2001/07/09) Version 0.12 gkh
 *	- removed in_interrupt() call, as it doesn't make sense to do 
 *	  that anymore.
 *
 * (2001/06/05) Version 0.11 gkh
 *	- Fixed problem with read urb status saying that we have shutdown,
 *	  and that we shouldn't resubmit the urb.  Patch from unknown.
 *
 * (2001/05/28) Version 0.10 gkh
 *	- Fixed problem with using data from userspace in the bluetooth_write
 *	  function as found by the CHECKER project.
 *	- Added a buffer to the write_urb_pool which reduces the number of
 *	  buffers being created and destroyed for ever write.  Also cleans
 *	  up the logic a bit.
 *	- Added a buffer to the control_urb_pool which fixes a memory leak
 *	  when the device is removed from the system.
 *
 * (2001/05/28) Version 0.9 gkh
 *	Fixed problem with bluetooth==NULL for bluetooth_read_bulk_callback
 *	which was found by both the CHECKER project and Mikko Rahkonen.
 *
 * (08/04/2001) gb
 *	Identify version on module load.
 *
 * (2001/03/10) Version 0.8 gkh
 *	Fixed problem with not unlinking interrupt urb on device close
 *	and resubmitting the read urb on error with bluetooth struct.
 *	Thanks to Narayan Mohanram <narayan@RovingNetworks.com> for the
 *	fixes.
 *
 * (11/29/2000) Version 0.7 gkh
 *	Fixed problem with overrunning the tty flip buffer.
 *	Removed unneeded NULL pointer initialization.
 *
 * (10/05/2000) Version 0.6 gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 *	Got a real major id number and name.
 *
 * (08/06/2000) Version 0.5 gkh
 *	Fixed problem of not resubmitting the bulk read urb if there is
 *	an error in the callback.  Ericsson devices seem to need this.
 *
 * (07/11/2000) Version 0.4 gkh
 *	Fixed bug in disconnect for when we call tty_hangup
 *	Fixed bug in bluetooth_ctrl_msg where the bluetooth struct was not
 *	getting attached to the control urb properly.
 *	Fixed bug in bluetooth_write where we pay attention to the result
 *	of bluetooth_ctrl_msg.
 *
 * (08/03/2000) Version 0.3 gkh mdc
 *	Merged in Mark's changes to make the driver play nice with the Axis
 *	stack.
 *	Made the write bulk use an urb pool to enable larger transfers with
 *	fewer calls to the driver.
 *	Fixed off by one bug in acl pkt receive
 *	Made packet counters specific to each bluetooth device 
 *	Added checks for zero length callbacks
 *	Added buffers for int and bulk packets.  Had to do this otherwise 
 *	packet types could intermingle.
 *	Made a control urb pool for the control messages.
 *
 * (07/11/2000) Version 0.2 gkh
 *	Fixed a small bug found by Nils Faerber in the usb_bluetooth_probe 
 *	function.
 *
 * (07/09/2000) Version 0.1 gkh
 *	Initial release. Has support for sending ACL data (which is really just
 *	a HCI frame.) Raw HCI commands and HCI events are not supported.
 *	A ioctl will probably be needed for the HCI commands and events in the
 *	future. All isoch endpoints are ignored at this time also.
 *	This driver should work for all currently shipping USB Bluetooth 
 *	devices at this time :)
 * 
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


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#define DEBUG
#include <linux/usb.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.13"
#define DRIVER_AUTHOR "Greg Kroah-Hartman, Mark Douglas Corner"
#define DRIVER_DESC "USB Bluetooth tty driver"

/* define this if you have hardware that is not good */
/*#define	BTBUGGYHARDWARE */

/* Class, SubClass, and Protocol codes that describe a Bluetooth device */
#define WIRELESS_CLASS_CODE			0xe0
#define RF_SUBCLASS_CODE			0x01
#define BLUETOOTH_PROGRAMMING_PROTOCOL_CODE	0x01


#define BLUETOOTH_TTY_MAJOR	216	/* real device node major id */
#define BLUETOOTH_TTY_MINORS	256	/* whole lotta bluetooth devices */

#define USB_BLUETOOTH_MAGIC	0x6d02	/* magic number for bluetooth struct */

#define BLUETOOTH_CONTROL_REQUEST_TYPE	0x20

/* Bluetooth packet types */
#define CMD_PKT			0x01
#define ACL_PKT			0x02
#define SCO_PKT			0x03
#define EVENT_PKT		0x04
#define ERROR_PKT		0x05
#define NEG_PKT			0x06

/* Message sizes */
#define MAX_EVENT_SIZE		0xFF
#define EVENT_HDR_SIZE		3	/* 2 for the header + 1 for the type indicator */
#define EVENT_BUFFER_SIZE	(MAX_EVENT_SIZE + EVENT_HDR_SIZE)

#define MAX_ACL_SIZE		0xFFFF
#define ACL_HDR_SIZE		5	/* 4 for the header + 1 for the type indicator */
#define ACL_BUFFER_SIZE		(MAX_ACL_SIZE + ACL_HDR_SIZE)

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

#define CHAR2INT16(c1,c0)	(((u32)((c1) & 0xff) << 8) + (u32)((c0) & 0xff))

#define NUM_BULK_URBS		24
#define NUM_CONTROL_URBS	16

struct usb_bluetooth {
	int			magic;
	struct usb_device *	dev;
	struct tty_driver *	tty_driver;	/* the tty_driver for this device */
	struct tty_struct *	tty;		/* the corresponding tty for this port */

	unsigned char		minor;		/* the starting minor number for this device */
	int			throttle;	/* throttled by tty layer */
	int			open_count;
	
	__u8			control_out_bInterfaceNum;
	struct urb *		control_urb_pool[NUM_CONTROL_URBS];
	struct usb_ctrlrequest	dr[NUM_CONTROL_URBS];

	unsigned char *		interrupt_in_buffer;
	struct urb *		interrupt_in_urb;
	__u8			interrupt_in_endpointAddress;
	__u8			interrupt_in_interval;
	int			interrupt_in_buffer_size;

	unsigned char *		bulk_in_buffer;
	struct urb *		read_urb;
	__u8			bulk_in_endpointAddress;
	int			bulk_in_buffer_size;

	int			bulk_out_buffer_size;
	__u8			bulk_out_endpointAddress;

	wait_queue_head_t	write_wait;

	struct work_struct			work;	/* work queue entry for line discipline waking up */
	
	unsigned int		int_packet_pos;
	unsigned char		int_buffer[EVENT_BUFFER_SIZE];
	unsigned int		bulk_packet_pos;
	unsigned char		bulk_buffer[ACL_BUFFER_SIZE];	/* 64k preallocated, fix? */
	struct semaphore	lock;
};


/* local function prototypes */
static int  bluetooth_open		(struct tty_struct *tty, struct file *filp);
static void bluetooth_close		(struct tty_struct *tty, struct file *filp);
static int  bluetooth_write		(struct tty_struct *tty, const unsigned char *buf, int count);
static int  bluetooth_write_room	(struct tty_struct *tty);
static int  bluetooth_chars_in_buffer	(struct tty_struct *tty);
static void bluetooth_throttle		(struct tty_struct *tty);
static void bluetooth_unthrottle	(struct tty_struct *tty);
static int  bluetooth_ioctl		(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void bluetooth_set_termios	(struct tty_struct *tty, struct termios *old);

static void bluetooth_int_callback		(struct urb *urb, struct pt_regs *regs);
static void bluetooth_ctrl_callback		(struct urb *urb, struct pt_regs *regs);
static void bluetooth_read_bulk_callback	(struct urb *urb, struct pt_regs *regs);
static void bluetooth_write_bulk_callback	(struct urb *urb, struct pt_regs *regs);

static int usb_bluetooth_probe (struct usb_interface *intf, 
				const struct usb_device_id *id);
static void usb_bluetooth_disconnect	(struct usb_interface *intf);


static struct usb_device_id usb_bluetooth_ids [] = {
	{ USB_DEVICE_INFO(WIRELESS_CLASS_CODE, RF_SUBCLASS_CODE, BLUETOOTH_PROGRAMMING_PROTOCOL_CODE) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_bluetooth_ids);

static struct usb_driver usb_bluetooth_driver = {
	.owner =	THIS_MODULE,
	.name =		"bluetty",
	.probe =	usb_bluetooth_probe,
	.disconnect =	usb_bluetooth_disconnect,
	.id_table =	usb_bluetooth_ids,
};

static struct tty_driver	*bluetooth_tty_driver;
static struct usb_bluetooth	*bluetooth_table[BLUETOOTH_TTY_MINORS];


static inline int bluetooth_paranoia_check (struct usb_bluetooth *bluetooth, const char *function)
{
	if (!bluetooth) {
		dbg("%s - bluetooth == NULL", function);
		return -1;
	}
	if (bluetooth->magic != USB_BLUETOOTH_MAGIC) {
		dbg("%s - bad magic number for bluetooth", function);
		return -1;
	}

	return 0;
}


static inline struct usb_bluetooth* get_usb_bluetooth (struct usb_bluetooth *bluetooth, const char *function)
{
	if (!bluetooth || 
	    bluetooth_paranoia_check (bluetooth, function)) { 
		/* then say that we don't have a valid usb_bluetooth thing, which will
		 * end up generating -ENODEV return values */
		return NULL;
	}

	return bluetooth;
}


static inline struct usb_bluetooth *get_bluetooth_by_index (int index)
{
	return bluetooth_table[index];
}


static int bluetooth_ctrl_msg (struct usb_bluetooth *bluetooth, int request, int value, const unsigned char *buf, int len)
{
	struct urb *urb = NULL;
	struct usb_ctrlrequest *dr = NULL;
	int i;
	int status;

	dbg ("%s", __FUNCTION__);

	/* try to find a free urb in our list */
	for (i = 0; i < NUM_CONTROL_URBS; ++i) {
		if (bluetooth->control_urb_pool[i]->status != -EINPROGRESS) {
			urb = bluetooth->control_urb_pool[i];
			dr = &bluetooth->dr[i];
			break;
		}
	}
	if (urb == NULL) {
		dbg ("%s - no free urbs", __FUNCTION__);
		return -ENOMEM;
	}

	/* keep increasing the urb transfer buffer to fit the size of the message */
	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer = kmalloc (len, GFP_KERNEL);
		if (urb->transfer_buffer == NULL) {
			err ("%s - out of memory", __FUNCTION__);
			return -ENOMEM;
		}
	}
	if (urb->transfer_buffer_length < len) {
		kfree(urb->transfer_buffer);
		urb->transfer_buffer = kmalloc (len, GFP_KERNEL);
		if (urb->transfer_buffer == NULL) {
			err ("%s - out of memory", __FUNCTION__);
			return -ENOMEM;
		}
	}
	memcpy (urb->transfer_buffer, buf, len);

	dr->bRequestType= BLUETOOTH_CONTROL_REQUEST_TYPE;
	dr->bRequest = request;
	dr->wValue = cpu_to_le16((u16) value);
	dr->wIndex = cpu_to_le16((u16) bluetooth->control_out_bInterfaceNum);
	dr->wLength = cpu_to_le16((u16) len);
	
	usb_fill_control_urb (urb, bluetooth->dev, usb_sndctrlpipe(bluetooth->dev, 0),
			  (unsigned char*)dr, urb->transfer_buffer, len, bluetooth_ctrl_callback, bluetooth);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_KERNEL);
	if (status)
		dbg("%s - usb_submit_urb(control) failed with status = %d", __FUNCTION__, status);
	
	return status;
}





/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int bluetooth_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_bluetooth *bluetooth;
	int result;

	dbg("%s", __FUNCTION__);

	/* initialize the pointer incase something fails */
	tty->driver_data = NULL;

	/* get the bluetooth object associated with this tty pointer */
	bluetooth = get_bluetooth_by_index (tty->index);

	if (bluetooth_paranoia_check (bluetooth, __FUNCTION__)) {
		return -ENODEV;
	}

	down (&bluetooth->lock);
 
	++bluetooth->open_count;
	if (bluetooth->open_count == 1) {
		/* set up our structure making the tty driver remember our object, and us it */
		tty->driver_data = bluetooth;
		bluetooth->tty = tty;

		/* force low_latency on so that our tty_push actually forces the data through, 
	 	* otherwise it is scheduled, and with high data rates (like with OHCI) data
	 	* can get lost. */
		bluetooth->tty->low_latency = 1;
	
		/* Reset the packet position counters */
		bluetooth->int_packet_pos = 0;
		bluetooth->bulk_packet_pos = 0;

#ifndef BTBUGGYHARDWARE
		/* Start reading from the device */
		usb_fill_bulk_urb (bluetooth->read_urb, bluetooth->dev, 
			       usb_rcvbulkpipe(bluetooth->dev, bluetooth->bulk_in_endpointAddress),
			       bluetooth->bulk_in_buffer,
			       bluetooth->bulk_in_buffer_size,
			       bluetooth_read_bulk_callback, bluetooth);
		result = usb_submit_urb(bluetooth->read_urb, GFP_KERNEL);
		if (result)
			dbg("%s - usb_submit_urb(read bulk) failed with status %d", __FUNCTION__, result);
#endif
		usb_fill_int_urb (bluetooth->interrupt_in_urb, bluetooth->dev,
			      usb_rcvintpipe(bluetooth->dev, bluetooth->interrupt_in_endpointAddress),
			      bluetooth->interrupt_in_buffer,
			      bluetooth->interrupt_in_buffer_size,
			      bluetooth_int_callback, bluetooth,
			      bluetooth->interrupt_in_interval);
		result = usb_submit_urb(bluetooth->interrupt_in_urb, GFP_KERNEL);
		if (result)
			dbg("%s - usb_submit_urb(interrupt in) failed with status %d", __FUNCTION__, result);
	}
	
	up(&bluetooth->lock);

	return 0;
}


static void bluetooth_close (struct tty_struct *tty, struct file * filp)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg("%s", __FUNCTION__);

	if (!bluetooth->open_count) {
		dbg ("%s - device not opened", __FUNCTION__);
		return;
	}

	down (&bluetooth->lock);
 
	--bluetooth->open_count;
	if (bluetooth->open_count <= 0) {
		bluetooth->open_count = 0;

		/* shutdown any in-flight urbs that we know about */
		usb_kill_urb (bluetooth->read_urb);
		usb_kill_urb (bluetooth->interrupt_in_urb);
	}
	up(&bluetooth->lock);
}


static int bluetooth_write (struct tty_struct * tty, const unsigned char *buf, int count)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);
	struct urb *urb = NULL;
	unsigned char *temp_buffer = NULL;
	const unsigned char *current_buffer;
	unsigned char *urb_buffer;
	int i;
	int retval = 0;

	if (!bluetooth) {
		return -ENODEV;
	}

	dbg("%s - %d byte(s)", __FUNCTION__, count);

	if (!bluetooth->open_count) {
		dbg ("%s - device not opened", __FUNCTION__);
		return -EINVAL;
	}

	if (count == 0) {
		dbg("%s - write request of 0 bytes", __FUNCTION__);
		return 0;
	}
	if (count == 1) {
		dbg("%s - write request only included type %d", __FUNCTION__, buf[0]);
		return 1;
	}

#ifdef DEBUG
	printk (KERN_DEBUG __FILE__ ": %s - length = %d, data = ", __FUNCTION__, count);
	for (i = 0; i < count; ++i) {
		printk ("%.2x ", buf[i]);
	}
	printk ("\n");
#endif

	current_buffer = buf;

	switch (*current_buffer) {
		/* First byte indicates the type of packet */
		case CMD_PKT:
			/* dbg("%s- Send cmd_pkt len:%d", __FUNCTION__, count);*/

			retval = bluetooth_ctrl_msg (bluetooth, 0x00, 0x00, &current_buffer[1], count-1);
			if (retval) {
				goto exit;
			}
			retval = count;
			break;

		case ACL_PKT:
			++current_buffer;
			--count;

			urb_buffer = kmalloc (count, GFP_ATOMIC);
			if (!urb_buffer) {
				dev_err(&bluetooth->dev->dev, "out of memory\n");
				retval = -ENOMEM;
				goto exit;
			}

			urb = usb_alloc_urb(0, GFP_ATOMIC);
			if (!urb) {
				dev_err(&bluetooth->dev->dev, "no more free urbs\n");
				kfree(urb_buffer);
				retval = -ENOMEM;
				goto exit;
			}
			memcpy (urb_buffer, current_buffer, count);

			/* build up our urb */
			usb_fill_bulk_urb(urb, bluetooth->dev, 
					  usb_sndbulkpipe(bluetooth->dev,
						  	  bluetooth->bulk_out_endpointAddress),
					  urb_buffer,
					  count,
					  bluetooth_write_bulk_callback,
					  bluetooth);


			/* send it down the pipe */
			retval = usb_submit_urb(urb, GFP_KERNEL);
			if (retval) {
				dbg("%s - usb_submit_urb(write bulk) failed with error = %d", __FUNCTION__, retval);
				goto exit;
			}

			/* we are done with this urb, so let the host driver
			 * really free it when it is finished with it */
			usb_free_urb (urb);
			retval = count + 1;
			break;
		
		default :
			dbg("%s - unsupported (at this time) write type", __FUNCTION__);
			retval = -EINVAL;
			break;
	}

exit:
	kfree(temp_buffer);

	return retval;
} 


static int bluetooth_write_room (struct tty_struct *tty) 
{
	dbg("%s", __FUNCTION__);

	/*
	 * We really can take anything the user throws at us
	 * but let's pick a nice big number to tell the tty
	 * layer that we have lots of free space
	 */
	return 2048;
}


static int bluetooth_chars_in_buffer (struct tty_struct *tty) 
{
	dbg("%s", __FUNCTION__);

	/* 
	 * We can't really account for how much data we
	 * have sent out, but hasn't made it through to the
	 * device, so just tell the tty layer that everything
	 * is flushed.
	 */
	return 0;
}


static void bluetooth_throttle (struct tty_struct * tty)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg("%s", __FUNCTION__);

	if (!bluetooth->open_count) {
		dbg ("%s - device not open", __FUNCTION__);
		return;
	}
	
	dbg("%s unsupported (at this time)", __FUNCTION__);

	return;
}


static void bluetooth_unthrottle (struct tty_struct * tty)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg("%s", __FUNCTION__);

	if (!bluetooth->open_count) {
		dbg ("%s - device not open", __FUNCTION__);
		return;
	}

	dbg("%s unsupported (at this time)", __FUNCTION__);
}


static int bluetooth_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return -ENODEV;
	}

	dbg("%s - cmd 0x%.4x", __FUNCTION__, cmd);

	if (!bluetooth->open_count) {
		dbg ("%s - device not open", __FUNCTION__);
		return -ENODEV;
	}

	/* FIXME!!! */
	return -ENOIOCTLCMD;
}


static void bluetooth_set_termios (struct tty_struct *tty, struct termios * old)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg("%s", __FUNCTION__);

	if (!bluetooth->open_count) {
		dbg ("%s - device not open", __FUNCTION__);
		return;
	}

	/* FIXME!!! */

	return;
}


#ifdef BTBUGGYHARDWARE
void btusb_enable_bulk_read(struct tty_struct *tty){
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);
	int result;

	if (!bluetooth) {
		return;
	}

	dbg("%s", __FUNCTION__);

	if (!bluetooth->open_count) {
		dbg ("%s - device not open", __FUNCTION__);
		return;
	}

	if (bluetooth->read_urb) {
		usb_fill_bulk_urb(bluetooth->read_urb, bluetooth->dev, 
			      usb_rcvbulkpipe(bluetooth->dev, bluetooth->bulk_in_endpointAddress),
			      bluetooth->bulk_in_buffer, bluetooth->bulk_in_buffer_size, 
			      bluetooth_read_bulk_callback, bluetooth);
		result = usb_submit_urb(bluetooth->read_urb, GFP_KERNEL);
		if (result)
			err ("%s - failed submitting read urb, error %d", __FUNCTION__, result);
	}
}

void btusb_disable_bulk_read(struct tty_struct *tty){
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)tty->driver_data, __FUNCTION__);

	if (!bluetooth) {
		return;
	}

	dbg("%s", __FUNCTION__);

	if (!bluetooth->open_count) {
		dbg ("%s - device not open", __FUNCTION__);
		return;
	}

	if ((bluetooth->read_urb) && (bluetooth->read_urb->actual_length))
		usb_kill_urb(bluetooth->read_urb);
}
#endif


/*****************************************************************************
 * urb callback functions
 *****************************************************************************/


static void bluetooth_int_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);
	unsigned char *data = urb->transfer_buffer;
	unsigned int i;
	unsigned int count = urb->actual_length;
	unsigned int packet_size;
	int status;

	dbg("%s", __FUNCTION__);

	if (!bluetooth) {
		dbg("%s - bad bluetooth pointer, exiting", __FUNCTION__);
		return;
	}

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__, urb->status);
		goto exit;
	}

	if (!count) {
		dbg("%s - zero length int", __FUNCTION__);
		goto exit;
	}


#ifdef DEBUG
	if (count) {
		printk (KERN_DEBUG __FILE__ ": %s- length = %d, data = ", __FUNCTION__, count);
		for (i = 0; i < count; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif

#ifdef BTBUGGYHARDWARE
	if ((count >= 2) && (data[0] == 0xFF) && (data[1] == 0x00)) {
		data += 2;
		count -= 2;
	}
	if (count == 0) {
		urb->actual_length = 0;
		goto exit;
	}
#endif
	/* We add  a packet type identifier to the beginning of each
	   HCI frame.  This makes the data in the tty look like a
	   serial USB devices.  Each HCI frame can be broken across
	   multiple URBs so we buffer them until we have a full hci
	   packet */

	if (!bluetooth->int_packet_pos) {
		bluetooth->int_buffer[0] = EVENT_PKT;
		bluetooth->int_packet_pos++;
	}
	
	if (bluetooth->int_packet_pos + count > EVENT_BUFFER_SIZE) {
		err("%s - exceeded EVENT_BUFFER_SIZE", __FUNCTION__);
		bluetooth->int_packet_pos = 0;
		goto exit;
	}

	memcpy (&bluetooth->int_buffer[bluetooth->int_packet_pos],
		urb->transfer_buffer, count);
	bluetooth->int_packet_pos += count;
	urb->actual_length = 0;

	if (bluetooth->int_packet_pos >= EVENT_HDR_SIZE)
		packet_size = bluetooth->int_buffer[2];
	else
		goto exit;

	if (packet_size + EVENT_HDR_SIZE < bluetooth->int_packet_pos) {
		err("%s - packet was too long", __FUNCTION__);
		bluetooth->int_packet_pos = 0;
		goto exit;
	}

	if (packet_size + EVENT_HDR_SIZE == bluetooth->int_packet_pos) {
		for (i = 0; i < bluetooth->int_packet_pos; ++i) {
			/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them */
			if (bluetooth->tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(bluetooth->tty);
			}
			tty_insert_flip_char(bluetooth->tty, bluetooth->int_buffer[i], 0);
		}
		tty_flip_buffer_push(bluetooth->tty);

		bluetooth->int_packet_pos = 0;
	}

exit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	if (status)
		err ("%s - usb_submit_urb failed with result %d",
		     __FUNCTION__, status);
}


static void bluetooth_ctrl_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);

	dbg("%s", __FUNCTION__);

	if (!bluetooth) {
		dbg("%s - bad bluetooth pointer, exiting", __FUNCTION__);
		return;
	}

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}
}


static void bluetooth_read_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);
	unsigned char *data = urb->transfer_buffer;
	unsigned int count = urb->actual_length;
	unsigned int i;
	unsigned int packet_size;
	int result;


	dbg("%s", __FUNCTION__);

	if (!bluetooth) {
		dbg("%s - bad bluetooth pointer, exiting", __FUNCTION__);
		return;
	}

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		if (urb->status == -ENOENT) {                   
			dbg("%s - URB canceled, won't reschedule", __FUNCTION__);
			return;
		}
		goto exit;
	}

	if (!count) {
		dbg("%s - zero length read bulk", __FUNCTION__);
		goto exit;
	}

#ifdef DEBUG
	if (count) {
		printk (KERN_DEBUG __FILE__ ": %s- length = %d, data = ", __FUNCTION__, count);
		for (i = 0; i < count; ++i) {
			printk ("%.2x ", data[i]);
		}
		printk ("\n");
	}
#endif
#ifdef BTBUGGYHARDWARE
	if ((count == 4) && (data[0] == 0x00) && (data[1] == 0x00)
	    && (data[2] == 0x00) && (data[3] == 0x00)) {
		urb->actual_length = 0;
		usb_fill_bulk_urb(bluetooth->read_urb, bluetooth->dev, 
			      usb_rcvbulkpipe(bluetooth->dev, bluetooth->bulk_in_endpointAddress),
			      bluetooth->bulk_in_buffer, bluetooth->bulk_in_buffer_size, 
			      bluetooth_read_bulk_callback, bluetooth);
		result = usb_submit_urb(bluetooth->read_urb, GFP_KERNEL);
		if (result)
			err ("%s - failed resubmitting read urb, error %d", __FUNCTION__, result);

		return;
	}
#endif
	/* We add  a packet type identifier to the beginning of each
	   HCI frame.  This makes the data in the tty look like a
	   serial USB devices.  Each HCI frame can be broken across
	   multiple URBs so we buffer them until we have a full hci
	   packet */
	
	if (!bluetooth->bulk_packet_pos) {
		bluetooth->bulk_buffer[0] = ACL_PKT;
		bluetooth->bulk_packet_pos++;
	}

	if (bluetooth->bulk_packet_pos + count > ACL_BUFFER_SIZE) {
		err("%s - exceeded ACL_BUFFER_SIZE", __FUNCTION__);
		bluetooth->bulk_packet_pos = 0;
		goto exit;
	}

	memcpy (&bluetooth->bulk_buffer[bluetooth->bulk_packet_pos],
		urb->transfer_buffer, count);
	bluetooth->bulk_packet_pos += count;
	urb->actual_length = 0;

	if (bluetooth->bulk_packet_pos >= ACL_HDR_SIZE) {
		packet_size = CHAR2INT16(bluetooth->bulk_buffer[4],bluetooth->bulk_buffer[3]);
	} else {
		goto exit;
	}

	if (packet_size + ACL_HDR_SIZE < bluetooth->bulk_packet_pos) {
		err("%s - packet was too long", __FUNCTION__);
		bluetooth->bulk_packet_pos = 0;
		goto exit;
	}

	if (packet_size + ACL_HDR_SIZE == bluetooth->bulk_packet_pos) {
		for (i = 0; i < bluetooth->bulk_packet_pos; ++i) {
			/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them. */
			if (bluetooth->tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(bluetooth->tty);
			}
			tty_insert_flip_char(bluetooth->tty, bluetooth->bulk_buffer[i], 0);
		}
		tty_flip_buffer_push(bluetooth->tty);
		bluetooth->bulk_packet_pos = 0;
	}	

exit:
	if (!bluetooth || !bluetooth->open_count)
		return;

	usb_fill_bulk_urb(bluetooth->read_urb, bluetooth->dev, 
		      usb_rcvbulkpipe(bluetooth->dev, bluetooth->bulk_in_endpointAddress),
		      bluetooth->bulk_in_buffer, bluetooth->bulk_in_buffer_size, 
		      bluetooth_read_bulk_callback, bluetooth);
	result = usb_submit_urb(bluetooth->read_urb, GFP_KERNEL);
	if (result)
		err ("%s - failed resubmitting read urb, error %d", __FUNCTION__, result);

	return;
}


static void bluetooth_write_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)urb->context, __FUNCTION__);

	dbg("%s", __FUNCTION__);

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree(urb->transfer_buffer);

	if (!bluetooth) {
		dbg("%s - bad bluetooth pointer, exiting", __FUNCTION__);
		return;
	}

	if (urb->status) {
		dbg("%s - nonzero write bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	/* wake up our little function to let the tty layer know that something happened */
	schedule_work(&bluetooth->work);
}


static void bluetooth_softint(void *private)
{
	struct usb_bluetooth *bluetooth = get_usb_bluetooth ((struct usb_bluetooth *)private, __FUNCTION__);

	dbg("%s", __FUNCTION__);

	if (!bluetooth)
		return;

	tty_wakeup(bluetooth->tty);
}


static int usb_bluetooth_probe (struct usb_interface *intf, 
				const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev (intf);
	struct usb_bluetooth *bluetooth = NULL;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[8];
	struct usb_endpoint_descriptor *bulk_in_endpoint[8];
	struct usb_endpoint_descriptor *bulk_out_endpoint[8];
	int control_out_endpoint;

	int minor;
	int buffer_size;
	int i;
	int num_interrupt_in = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;

	interface = intf->cur_altsetting;
	control_out_endpoint = interface->desc.bInterfaceNumber;

	/* find the endpoints that we need */
	for (i = 0; i < interface->desc.bNumEndpoints; ++i) {
		endpoint = &interface->endpoint[i].desc;

		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			dbg("found bulk in");
			bulk_in_endpoint[num_bulk_in] = endpoint;
			++num_bulk_in;
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			dbg("found bulk out");
			bulk_out_endpoint[num_bulk_out] = endpoint;
			++num_bulk_out;
		}

		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x03)) {
			/* we found a interrupt in endpoint */
			dbg("found interrupt in");
			interrupt_in_endpoint[num_interrupt_in] = endpoint;
			++num_interrupt_in;
		}
	}

	/* according to the spec, we can only have 1 bulk_in, 1 bulk_out, and 1 interrupt_in endpoints */
	if ((num_bulk_in != 1) ||
	    (num_bulk_out != 1) ||
	    (num_interrupt_in != 1)) {
		dbg ("%s - improper number of endpoints. Bluetooth driver not bound.", __FUNCTION__);
		return -EIO;
	}

	info("USB Bluetooth converter detected");

	for (minor = 0; minor < BLUETOOTH_TTY_MINORS && bluetooth_table[minor]; ++minor)
		;
	if (bluetooth_table[minor]) {
		err("No more free Bluetooth devices");
		return -ENODEV;
	}

	if (!(bluetooth = kmalloc(sizeof(struct usb_bluetooth), GFP_KERNEL))) {
		err("Out of memory");
		return -ENOMEM;
	}

	memset(bluetooth, 0, sizeof(struct usb_bluetooth));

	bluetooth->magic = USB_BLUETOOTH_MAGIC;
	bluetooth->dev = dev;
	bluetooth->minor = minor;
	INIT_WORK(&bluetooth->work, bluetooth_softint, bluetooth);
	init_MUTEX(&bluetooth->lock);

	/* record the interface number for the control out */
	bluetooth->control_out_bInterfaceNum = control_out_endpoint;
	
	/* create our control out urb pool */ 
	for (i = 0; i < NUM_CONTROL_URBS; ++i) {
		struct urb  *urb = usb_alloc_urb(0, GFP_KERNEL);
		if (urb == NULL) {
			err("No free urbs available");
			goto probe_error;
		}
		urb->transfer_buffer = NULL;
		bluetooth->control_urb_pool[i] = urb;
	}

	/* set up the endpoint information */
	endpoint = bulk_in_endpoint[0];
	bluetooth->read_urb = usb_alloc_urb (0, GFP_KERNEL);
	if (!bluetooth->read_urb) {
		err("No free urbs available");
		goto probe_error;
	}
	bluetooth->bulk_in_buffer_size = buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
	bluetooth->bulk_in_endpointAddress = endpoint->bEndpointAddress;
	bluetooth->bulk_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
	if (!bluetooth->bulk_in_buffer) {
		err("Couldn't allocate bulk_in_buffer");
		goto probe_error;
	}
	usb_fill_bulk_urb(bluetooth->read_urb, dev, usb_rcvbulkpipe(dev, endpoint->bEndpointAddress),
		      bluetooth->bulk_in_buffer, buffer_size, bluetooth_read_bulk_callback, bluetooth);

	endpoint = bulk_out_endpoint[0];
	bluetooth->bulk_out_endpointAddress = endpoint->bEndpointAddress;
	bluetooth->bulk_out_buffer_size = le16_to_cpu(endpoint->wMaxPacketSize) * 2;

	endpoint = interrupt_in_endpoint[0];
	bluetooth->interrupt_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!bluetooth->interrupt_in_urb) {
		err("No free urbs available");
		goto probe_error;
	}
	bluetooth->interrupt_in_buffer_size = buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
	bluetooth->interrupt_in_endpointAddress = endpoint->bEndpointAddress;
	bluetooth->interrupt_in_interval = endpoint->bInterval;
	bluetooth->interrupt_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
	if (!bluetooth->interrupt_in_buffer) {
		err("Couldn't allocate interrupt_in_buffer");
		goto probe_error;
	}
	usb_fill_int_urb(bluetooth->interrupt_in_urb, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
		     bluetooth->interrupt_in_buffer, buffer_size, bluetooth_int_callback,
		     bluetooth, endpoint->bInterval);

	/* initialize the devfs nodes for this device and let the user know what bluetooths we are bound to */
	tty_register_device (bluetooth_tty_driver, minor, &intf->dev);
	info("Bluetooth converter now attached to ttyUB%d (or usb/ttub/%d for devfs)", minor, minor);

	bluetooth_table[minor] = bluetooth;

	/* success */
	usb_set_intfdata (intf, bluetooth);
	return 0;

probe_error:
	if (bluetooth->read_urb)
		usb_free_urb (bluetooth->read_urb);
	if (bluetooth->bulk_in_buffer)
		kfree (bluetooth->bulk_in_buffer);
	if (bluetooth->interrupt_in_urb)
		usb_free_urb (bluetooth->interrupt_in_urb);
	if (bluetooth->interrupt_in_buffer)
		kfree (bluetooth->interrupt_in_buffer);
	for (i = 0; i < NUM_CONTROL_URBS; ++i) 
		if (bluetooth->control_urb_pool[i]) {
			if (bluetooth->control_urb_pool[i]->transfer_buffer)
				kfree (bluetooth->control_urb_pool[i]->transfer_buffer);
			usb_free_urb (bluetooth->control_urb_pool[i]);
		}

	bluetooth_table[minor] = NULL;

	/* free up any memory that we allocated */
	kfree (bluetooth);
	return -EIO;
}


static void usb_bluetooth_disconnect(struct usb_interface *intf)
{
	struct usb_bluetooth *bluetooth = usb_get_intfdata (intf);
	int i;

	usb_set_intfdata (intf, NULL);
	if (bluetooth) {
		if ((bluetooth->open_count) && (bluetooth->tty))
			tty_hangup(bluetooth->tty);

		bluetooth->open_count = 0;

		if (bluetooth->read_urb) {
			usb_kill_urb (bluetooth->read_urb);
			usb_free_urb (bluetooth->read_urb);
		}
		if (bluetooth->bulk_in_buffer)
			kfree (bluetooth->bulk_in_buffer);

		if (bluetooth->interrupt_in_urb) {
			usb_kill_urb (bluetooth->interrupt_in_urb);
			usb_free_urb (bluetooth->interrupt_in_urb);
		}
		if (bluetooth->interrupt_in_buffer)
			kfree (bluetooth->interrupt_in_buffer);

		tty_unregister_device (bluetooth_tty_driver, bluetooth->minor);

		for (i = 0; i < NUM_CONTROL_URBS; ++i) {
			if (bluetooth->control_urb_pool[i]) {
				usb_kill_urb (bluetooth->control_urb_pool[i]);
				if (bluetooth->control_urb_pool[i]->transfer_buffer)
					kfree (bluetooth->control_urb_pool[i]->transfer_buffer);
				usb_free_urb (bluetooth->control_urb_pool[i]);
			}
		}
		
		info("Bluetooth converter now disconnected from ttyUB%d", bluetooth->minor);

		bluetooth_table[bluetooth->minor] = NULL;

		/* free up any memory that we allocated */
		kfree (bluetooth);
	} else {
		info("device disconnected");
	}
}

static struct tty_operations bluetooth_ops = {
	.open =			bluetooth_open,
	.close =		bluetooth_close,
	.write =		bluetooth_write,
	.write_room =		bluetooth_write_room,
	.ioctl =		bluetooth_ioctl,
	.set_termios =		bluetooth_set_termios,
	.throttle =		bluetooth_throttle,
	.unthrottle =		bluetooth_unthrottle,
	.chars_in_buffer =	bluetooth_chars_in_buffer,
};

static int usb_bluetooth_init(void)
{
	int i;
	int result;

	/* Initialize our global data */
	for (i = 0; i < BLUETOOTH_TTY_MINORS; ++i) {
		bluetooth_table[i] = NULL;
	}

	info ("USB Bluetooth support registered");

	bluetooth_tty_driver = alloc_tty_driver(BLUETOOTH_TTY_MINORS);
	if (!bluetooth_tty_driver)
		return -ENOMEM;

	bluetooth_tty_driver->owner = THIS_MODULE;
	bluetooth_tty_driver->driver_name = "usb-bluetooth";
	bluetooth_tty_driver->name = "ttyUB";
	bluetooth_tty_driver->devfs_name = "usb/ttub/";
	bluetooth_tty_driver->major = BLUETOOTH_TTY_MAJOR;
	bluetooth_tty_driver->minor_start = 0;
	bluetooth_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	bluetooth_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	bluetooth_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	bluetooth_tty_driver->init_termios = tty_std_termios;
	bluetooth_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(bluetooth_tty_driver, &bluetooth_ops);
	if (tty_register_driver (bluetooth_tty_driver)) {
		err("%s - failed to register tty driver", __FUNCTION__);
		put_tty_driver(bluetooth_tty_driver);
		return -1;
	}

	/* register the USB driver */
	result = usb_register(&usb_bluetooth_driver);
	if (result < 0) {
		tty_unregister_driver(bluetooth_tty_driver);
		put_tty_driver(bluetooth_tty_driver);
		err("usb_register failed for the USB bluetooth driver. Error number %d", result);
		return -1;
	}

	info(DRIVER_DESC " " DRIVER_VERSION);

	return 0;
}


static void usb_bluetooth_exit(void)
{
	usb_deregister(&usb_bluetooth_driver);
	tty_unregister_driver(bluetooth_tty_driver);
	put_tty_driver(bluetooth_tty_driver);
}


module_init(usb_bluetooth_init);
module_exit(usb_bluetooth_exit);

/* Module information */
MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

