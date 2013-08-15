/*
  USB Driver for Sierra Wireless

  Copyright (C) 2006, 2007, 2008  Kevin Lloyd <klloyd@sierrawireless.com>,

  Copyright (C) 2008, 2009  Elina Pasheva, Matthew Safar, Rory Filer
			<linux@sierrawireless.com>

  IMPORTANT DISCLAIMER: This driver is not commercially supported by
  Sierra Wireless. Use at your own risk.

  This driver is free software; you can redistribute it and/or modify
  it under the terms of Version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  Portions based on the option driver by Matthias Urlichs <smurf@smurf.noris.de>
  Whom based his on the Keyspan driver by Hugh Blemings <hugh@blemings.org>
*/
/* Uncomment to log function calls */
/* #define DEBUG */

#define DRIVER_AUTHOR "Kevin Lloyd, Elina Pasheva, Matthew Safar, Rory Filer"
#define DRIVER_DESC "USB Driver for Sierra Wireless USB modems"

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define SWIMS_USB_REQUEST_SetPower	0x00
#define SWIMS_USB_REQUEST_SetNmea	0x07

#define N_IN_URB_HM	8
#define N_OUT_URB_HM	64
#define N_IN_URB	4
#define N_OUT_URB	4
#define IN_BUFLEN	4096

#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */

static bool nmea;

/* Used in interface blacklisting */
struct sierra_iface_info {
	const u32 infolen;	/* number of interface numbers on blacklist */
	const u8  *ifaceinfo;	/* pointer to the array holding the numbers */
};

struct sierra_intf_private {
	spinlock_t susp_lock;
	unsigned int suspended:1;
	int in_flight;
};

static int sierra_set_power_state(struct usb_device *udev, __u16 swiState)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			SWIMS_USB_REQUEST_SetPower,	/* __u8 request      */
			USB_TYPE_VENDOR,		/* __u8 request type */
			swiState,			/* __u16 value       */
			0,				/* __u16 index       */
			NULL,				/* void *data        */
			0,				/* __u16 size 	     */
			USB_CTRL_SET_TIMEOUT);		/* int timeout 	     */
}

static int sierra_vsc_set_nmea(struct usb_device *udev, __u16 enable)
{
	return usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			SWIMS_USB_REQUEST_SetNmea,	/* __u8 request      */
			USB_TYPE_VENDOR,		/* __u8 request type */
			enable,				/* __u16 value       */
			0x0000,				/* __u16 index       */
			NULL,				/* void *data        */
			0,				/* __u16 size 	     */
			USB_CTRL_SET_TIMEOUT);		/* int timeout       */
}

static int sierra_calc_num_ports(struct usb_serial *serial)
{
	int num_ports = 0;
	u8 ifnum, numendpoints;

	ifnum = serial->interface->cur_altsetting->desc.bInterfaceNumber;
	numendpoints = serial->interface->cur_altsetting->desc.bNumEndpoints;

	/* Dummy interface present on some SKUs should be ignored */
	if (ifnum == 0x99)
		num_ports = 0;
	else if (numendpoints <= 3)
		num_ports = 1;
	else
		num_ports = (numendpoints-1)/2;
	return num_ports;
}

static int is_blacklisted(const u8 ifnum,
				const struct sierra_iface_info *blacklist)
{
	const u8  *info;
	int i;

	if (blacklist) {
		info = blacklist->ifaceinfo;

		for (i = 0; i < blacklist->infolen; i++) {
			if (info[i] == ifnum)
				return 1;
		}
	}
	return 0;
}

static int is_himemory(const u8 ifnum,
				const struct sierra_iface_info *himemorylist)
{
	const u8  *info;
	int i;

	if (himemorylist) {
		info = himemorylist->ifaceinfo;

		for (i=0; i < himemorylist->infolen; i++) {
			if (info[i] == ifnum)
				return 1;
		}
	}
	return 0;
}

static int sierra_calc_interface(struct usb_serial *serial)
{
	int interface;
	struct usb_interface *p_interface;
	struct usb_host_interface *p_host_interface;

	/* Get the interface structure pointer from the serial struct */
	p_interface = serial->interface;

	/* Get a pointer to the host interface structure */
	p_host_interface = p_interface->cur_altsetting;

	/* read the interface descriptor for this active altsetting
	 * to find out the interface number we are on
	*/
	interface = p_host_interface->desc.bInterfaceNumber;

	return interface;
}

static int sierra_probe(struct usb_serial *serial,
			const struct usb_device_id *id)
{
	int result = 0;
	struct usb_device *udev;
	u8 ifnum;

	udev = serial->dev;
	ifnum = sierra_calc_interface(serial);

	/*
	 * If this interface supports more than 1 alternate
	 * select the 2nd one
	 */
	if (serial->interface->num_altsetting == 2) {
		dev_dbg(&udev->dev, "Selecting alt setting for interface %d\n",
			ifnum);
		/* We know the alternate setting is 1 for the MC8785 */
		usb_set_interface(udev, ifnum, 1);
	}

	/* ifnum could have changed - by calling usb_set_interface */
	ifnum = sierra_calc_interface(serial);

	if (is_blacklisted(ifnum,
				(struct sierra_iface_info *)id->driver_info)) {
		dev_dbg(&serial->dev->dev,
			"Ignoring blacklisted interface #%d\n", ifnum);
		return -ENODEV;
	}

	return result;
}

/* interfaces with higher memory requirements */
static const u8 hi_memory_typeA_ifaces[] = { 0, 2 };
static const struct sierra_iface_info typeA_interface_list = {
	.infolen = ARRAY_SIZE(hi_memory_typeA_ifaces),
	.ifaceinfo = hi_memory_typeA_ifaces,
};

static const u8 hi_memory_typeB_ifaces[] = { 3, 4, 5, 6 };
static const struct sierra_iface_info typeB_interface_list = {
	.infolen = ARRAY_SIZE(hi_memory_typeB_ifaces),
	.ifaceinfo = hi_memory_typeB_ifaces,
};

/* 'blacklist' of interfaces not served by this driver */
static const u8 direct_ip_non_serial_ifaces[] = { 7, 8, 9, 10, 11, 19, 20 };
static const struct sierra_iface_info direct_ip_interface_blacklist = {
	.infolen = ARRAY_SIZE(direct_ip_non_serial_ifaces),
	.ifaceinfo = direct_ip_non_serial_ifaces,
};

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0F3D, 0x0112) }, /* Airprime/Sierra PC 5220 */
	{ USB_DEVICE(0x03F0, 0x1B1D) },	/* HP ev2200 a.k.a MC5720 */
	{ USB_DEVICE(0x03F0, 0x211D) }, /* HP ev2210 a.k.a MC5725 */
	{ USB_DEVICE(0x03F0, 0x1E1D) },	/* HP hs2300 a.k.a MC8775 */

	{ USB_DEVICE(0x1199, 0x0017) },	/* Sierra Wireless EM5625 */
	{ USB_DEVICE(0x1199, 0x0018) },	/* Sierra Wireless MC5720 */
	{ USB_DEVICE(0x1199, 0x0218) },	/* Sierra Wireless MC5720 */
	{ USB_DEVICE(0x1199, 0x0020) },	/* Sierra Wireless MC5725 */
	{ USB_DEVICE(0x1199, 0x0220) },	/* Sierra Wireless MC5725 */
	{ USB_DEVICE(0x1199, 0x0022) },	/* Sierra Wireless EM5725 */
	{ USB_DEVICE(0x1199, 0x0024) },	/* Sierra Wireless MC5727 */
	{ USB_DEVICE(0x1199, 0x0224) },	/* Sierra Wireless MC5727 */
	{ USB_DEVICE(0x1199, 0x0019) },	/* Sierra Wireless AirCard 595 */
	{ USB_DEVICE(0x1199, 0x0021) },	/* Sierra Wireless AirCard 597E */
	{ USB_DEVICE(0x1199, 0x0112) }, /* Sierra Wireless AirCard 580 */
	{ USB_DEVICE(0x1199, 0x0120) },	/* Sierra Wireless USB Dongle 595U */
	{ USB_DEVICE(0x1199, 0x0301) },	/* Sierra Wireless USB Dongle 250U */
	/* Sierra Wireless C597 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x0023, 0xFF, 0xFF, 0xFF) },
	/* Sierra Wireless T598 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x0025, 0xFF, 0xFF, 0xFF) },
	{ USB_DEVICE(0x1199, 0x0026) }, /* Sierra Wireless T11 */
	{ USB_DEVICE(0x1199, 0x0027) }, /* Sierra Wireless AC402 */
	{ USB_DEVICE(0x1199, 0x0028) }, /* Sierra Wireless MC5728 */
	{ USB_DEVICE(0x1199, 0x0029) }, /* Sierra Wireless Device */

	{ USB_DEVICE(0x1199, 0x6802) },	/* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6803) },	/* Sierra Wireless MC8765 */
	{ USB_DEVICE(0x1199, 0x6804) },	/* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6805) },	/* Sierra Wireless MC8765 */
	{ USB_DEVICE(0x1199, 0x6808) },	/* Sierra Wireless MC8755 */
	{ USB_DEVICE(0x1199, 0x6809) },	/* Sierra Wireless MC8765 */
	{ USB_DEVICE(0x1199, 0x6812) },	/* Sierra Wireless MC8775 & AC 875U */
	{ USB_DEVICE(0x1199, 0x6813) },	/* Sierra Wireless MC8775 */
	{ USB_DEVICE(0x1199, 0x6815) },	/* Sierra Wireless MC8775 */
	{ USB_DEVICE(0x1199, 0x6816) },	/* Sierra Wireless MC8775 */
	{ USB_DEVICE(0x1199, 0x6820) },	/* Sierra Wireless AirCard 875 */
	{ USB_DEVICE(0x1199, 0x6821) },	/* Sierra Wireless AirCard 875U */
	{ USB_DEVICE(0x1199, 0x6822) },	/* Sierra Wireless AirCard 875E */
	{ USB_DEVICE(0x1199, 0x6832) },	/* Sierra Wireless MC8780 */
	{ USB_DEVICE(0x1199, 0x6833) },	/* Sierra Wireless MC8781 */
	{ USB_DEVICE(0x1199, 0x6834) },	/* Sierra Wireless MC8780 */
	{ USB_DEVICE(0x1199, 0x6835) },	/* Sierra Wireless MC8781 */
	{ USB_DEVICE(0x1199, 0x6838) },	/* Sierra Wireless MC8780 */
	{ USB_DEVICE(0x1199, 0x6839) },	/* Sierra Wireless MC8781 */
	{ USB_DEVICE(0x1199, 0x683A) },	/* Sierra Wireless MC8785 */
	{ USB_DEVICE(0x1199, 0x683B) },	/* Sierra Wireless MC8785 Composite */
	/* Sierra Wireless MC8790, MC8791, MC8792 Composite */
	{ USB_DEVICE(0x1199, 0x683C) },
	{ USB_DEVICE(0x1199, 0x683D) },	/* Sierra Wireless MC8791 Composite */
	/* Sierra Wireless MC8790, MC8791, MC8792 */
	{ USB_DEVICE(0x1199, 0x683E) },
	{ USB_DEVICE(0x1199, 0x6850) },	/* Sierra Wireless AirCard 880 */
	{ USB_DEVICE(0x1199, 0x6851) },	/* Sierra Wireless AirCard 881 */
	{ USB_DEVICE(0x1199, 0x6852) },	/* Sierra Wireless AirCard 880 E */
	{ USB_DEVICE(0x1199, 0x6853) },	/* Sierra Wireless AirCard 881 E */
	{ USB_DEVICE(0x1199, 0x6855) },	/* Sierra Wireless AirCard 880 U */
	{ USB_DEVICE(0x1199, 0x6856) },	/* Sierra Wireless AirCard 881 U */
	{ USB_DEVICE(0x1199, 0x6859) },	/* Sierra Wireless AirCard 885 E */
	{ USB_DEVICE(0x1199, 0x685A) },	/* Sierra Wireless AirCard 885 E */
	/* Sierra Wireless C885 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x6880, 0xFF, 0xFF, 0xFF)},
	/* Sierra Wireless C888, Air Card 501, USB 303, USB 304 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x6890, 0xFF, 0xFF, 0xFF)},
	/* Sierra Wireless C22/C33 */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x6891, 0xFF, 0xFF, 0xFF)},
	/* Sierra Wireless HSPA Non-Composite Device */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x1199, 0x6892, 0xFF, 0xFF, 0xFF)},
	{ USB_DEVICE(0x1199, 0x6893) },	/* Sierra Wireless Device */
	{ USB_DEVICE(0x1199, 0x68A3), 	/* Sierra Wireless Direct IP modems */
	  .driver_info = (kernel_ulong_t)&direct_ip_interface_blacklist
	},
	/* AT&T Direct IP LTE modems */
	{ USB_DEVICE_AND_INTERFACE_INFO(0x0F3D, 0x68AA, 0xFF, 0xFF, 0xFF),
	  .driver_info = (kernel_ulong_t)&direct_ip_interface_blacklist
	},
	{ USB_DEVICE(0x0f3d, 0x68A3), 	/* Airprime/Sierra Wireless Direct IP modems */
	  .driver_info = (kernel_ulong_t)&direct_ip_interface_blacklist
	},
       { USB_DEVICE(0x413C, 0x08133) }, /* Dell Computer Corp. Wireless 5720 VZW Mobile Broadband (EVDO Rev-A) Minicard GPS Port */

	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);


struct sierra_port_private {
	spinlock_t lock;	/* lock the structure */
	int outstanding_urbs;	/* number of out urbs in flight */
	struct usb_anchor active;
	struct usb_anchor delayed;

	int num_out_urbs;
	int num_in_urbs;
	/* Input endpoints and buffers for this port */
	struct urb *in_urbs[N_IN_URB_HM];

	/* Settings for the port */
	int rts_state;	/* Handshaking pins (outputs) */
	int dtr_state;
	int cts_state;	/* Handshaking pins (inputs) */
	int dsr_state;
	int dcd_state;
	int ri_state;
	unsigned int opened:1;
};

static int sierra_send_setup(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct sierra_port_private *portdata;
	__u16 interface = 0;
	int val = 0;
	int do_send = 0;
	int retval;

	portdata = usb_get_serial_port_data(port);

	if (portdata->dtr_state)
		val |= 0x01;
	if (portdata->rts_state)
		val |= 0x02;

	/* If composite device then properly report interface */
	if (serial->num_ports == 1) {
		interface = sierra_calc_interface(serial);
		/* Control message is sent only to interfaces with
		 * interrupt_in endpoints
		 */
		if (port->interrupt_in_urb) {
			/* send control message */
			do_send = 1;
		}
	}

	/* Otherwise the need to do non-composite mapping */
	else {
		if (port->bulk_out_endpointAddress == 2)
			interface = 0;
		else if (port->bulk_out_endpointAddress == 4)
			interface = 1;
		else if (port->bulk_out_endpointAddress == 5)
			interface = 2;

		do_send = 1;
	}
	if (!do_send)
		return 0;

	retval = usb_autopm_get_interface(serial->interface);
	if (retval < 0)
		return retval;

	retval = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
		0x22, 0x21, val, interface, NULL, 0, USB_CTRL_SET_TIMEOUT);
	usb_autopm_put_interface(serial->interface);

	return retval;
}

static void sierra_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	tty_termios_copy_hw(&tty->termios, old_termios);
	sierra_send_setup(port);
}

static int sierra_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int value;
	struct sierra_port_private *portdata;

	portdata = usb_get_serial_port_data(port);

	value = ((portdata->rts_state) ? TIOCM_RTS : 0) |
		((portdata->dtr_state) ? TIOCM_DTR : 0) |
		((portdata->cts_state) ? TIOCM_CTS : 0) |
		((portdata->dsr_state) ? TIOCM_DSR : 0) |
		((portdata->dcd_state) ? TIOCM_CAR : 0) |
		((portdata->ri_state) ? TIOCM_RNG : 0);

	return value;
}

static int sierra_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct sierra_port_private *portdata;

	portdata = usb_get_serial_port_data(port);

	if (set & TIOCM_RTS)
		portdata->rts_state = 1;
	if (set & TIOCM_DTR)
		portdata->dtr_state = 1;

	if (clear & TIOCM_RTS)
		portdata->rts_state = 0;
	if (clear & TIOCM_DTR)
		portdata->dtr_state = 0;
	return sierra_send_setup(port);
}

static void sierra_release_urb(struct urb *urb)
{
	struct usb_serial_port *port;
	if (urb) {
		port = urb->context;
		kfree(urb->transfer_buffer);
		usb_free_urb(urb);
	}
}

static void sierra_outdat_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct sierra_port_private *portdata = usb_get_serial_port_data(port);
	struct sierra_intf_private *intfdata;
	int status = urb->status;

	intfdata = port->serial->private;

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree(urb->transfer_buffer);
	usb_autopm_put_interface_async(port->serial->interface);
	if (status)
		dev_dbg(&port->dev, "%s - nonzero write bulk status "
		    "received: %d\n", __func__, status);

	spin_lock(&portdata->lock);
	--portdata->outstanding_urbs;
	spin_unlock(&portdata->lock);
	spin_lock(&intfdata->susp_lock);
	--intfdata->in_flight;
	spin_unlock(&intfdata->susp_lock);

	usb_serial_port_softint(port);
}

/* Write */
static int sierra_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count)
{
	struct sierra_port_private *portdata;
	struct sierra_intf_private *intfdata;
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	unsigned char *buffer;
	struct urb *urb;
	size_t writesize = min((size_t)count, (size_t)MAX_TRANSFER);
	int retval = 0;

	/* verify that we actually have some data to write */
	if (count == 0)
		return 0;

	portdata = usb_get_serial_port_data(port);
	intfdata = serial->private;

	dev_dbg(&port->dev, "%s: write (%zd bytes)\n", __func__, writesize);
	spin_lock_irqsave(&portdata->lock, flags);
	dev_dbg(&port->dev, "%s - outstanding_urbs: %d\n", __func__,
		portdata->outstanding_urbs);
	if (portdata->outstanding_urbs > portdata->num_out_urbs) {
		spin_unlock_irqrestore(&portdata->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	portdata->outstanding_urbs++;
	dev_dbg(&port->dev, "%s - 1, outstanding_urbs: %d\n", __func__,
		portdata->outstanding_urbs);
	spin_unlock_irqrestore(&portdata->lock, flags);

	retval = usb_autopm_get_interface_async(serial->interface);
	if (retval < 0) {
		spin_lock_irqsave(&portdata->lock, flags);
		portdata->outstanding_urbs--;
		spin_unlock_irqrestore(&portdata->lock, flags);
		goto error_simple;
	}

	buffer = kmalloc(writesize, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&port->dev, "out of memory\n");
		retval = -ENOMEM;
		goto error_no_buffer;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "no more free urbs\n");
		retval = -ENOMEM;
		goto error_no_urb;
	}

	memcpy(buffer, buf, writesize);

	usb_serial_debug_data(&port->dev, __func__, writesize, buffer);

	usb_fill_bulk_urb(urb, serial->dev,
			  usb_sndbulkpipe(serial->dev,
					  port->bulk_out_endpointAddress),
			  buffer, writesize, sierra_outdat_callback, port);

	/* Handle the need to send a zero length packet */
	urb->transfer_flags |= URB_ZERO_PACKET;

	spin_lock_irqsave(&intfdata->susp_lock, flags);

	if (intfdata->suspended) {
		usb_anchor_urb(urb, &portdata->delayed);
		spin_unlock_irqrestore(&intfdata->susp_lock, flags);
		goto skip_power;
	} else {
		usb_anchor_urb(urb, &portdata->active);
	}
	/* send it down the pipe */
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval) {
		usb_unanchor_urb(urb);
		spin_unlock_irqrestore(&intfdata->susp_lock, flags);
		dev_err(&port->dev, "%s - usb_submit_urb(write bulk) failed "
			"with status = %d\n", __func__, retval);
		goto error;
	} else {
		intfdata->in_flight++;
		spin_unlock_irqrestore(&intfdata->susp_lock, flags);
	}

skip_power:
	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb(urb);

	return writesize;
error:
	usb_free_urb(urb);
error_no_urb:
	kfree(buffer);
error_no_buffer:
	spin_lock_irqsave(&portdata->lock, flags);
	--portdata->outstanding_urbs;
	dev_dbg(&port->dev, "%s - 2. outstanding_urbs: %d\n", __func__,
		portdata->outstanding_urbs);
	spin_unlock_irqrestore(&portdata->lock, flags);
	usb_autopm_put_interface_async(serial->interface);
error_simple:
	return retval;
}

static void sierra_indat_callback(struct urb *urb)
{
	int err;
	int endpoint;
	struct usb_serial_port *port;
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);
	port = urb->context;

	if (status) {
		dev_dbg(&port->dev, "%s: nonzero status: %d on"
			" endpoint %02x\n", __func__, status, endpoint);
	} else {
		if (urb->actual_length) {
			tty_insert_flip_string(&port->port, data,
				urb->actual_length);
			tty_flip_buffer_push(&port->port);

			usb_serial_debug_data(&port->dev, __func__,
					      urb->actual_length, data);
		} else {
			dev_dbg(&port->dev, "%s: empty read urb"
				" received\n", __func__);
		}
	}

	/* Resubmit urb so we continue receiving */
	if (status != -ESHUTDOWN && status != -EPERM) {
		usb_mark_last_busy(port->serial->dev);
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err && err != -EPERM)
			dev_err(&port->dev, "resubmit read urb failed."
				"(%d)\n", err);
	}
}

static void sierra_instat_callback(struct urb *urb)
{
	int err;
	int status = urb->status;
	struct usb_serial_port *port =  urb->context;
	struct sierra_port_private *portdata = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;

	dev_dbg(&port->dev, "%s: urb %p port %p has data %p\n", __func__,
		urb, port, portdata);

	if (status == 0) {
		struct usb_ctrlrequest *req_pkt =
				(struct usb_ctrlrequest *)urb->transfer_buffer;

		if (!req_pkt) {
			dev_dbg(&port->dev, "%s: NULL req_pkt\n",
				__func__);
			return;
		}
		if ((req_pkt->bRequestType == 0xA1) &&
				(req_pkt->bRequest == 0x20)) {
			int old_dcd_state;
			unsigned char signals = *((unsigned char *)
					urb->transfer_buffer +
					sizeof(struct usb_ctrlrequest));

			dev_dbg(&port->dev, "%s: signal x%x\n", __func__,
				signals);

			old_dcd_state = portdata->dcd_state;
			portdata->cts_state = 1;
			portdata->dcd_state = ((signals & 0x01) ? 1 : 0);
			portdata->dsr_state = ((signals & 0x02) ? 1 : 0);
			portdata->ri_state = ((signals & 0x08) ? 1 : 0);

			if (old_dcd_state && !portdata->dcd_state)
				tty_port_tty_hangup(&port->port, true);
		} else {
			dev_dbg(&port->dev, "%s: type %x req %x\n",
				__func__, req_pkt->bRequestType,
				req_pkt->bRequest);
		}
	} else
		dev_dbg(&port->dev, "%s: error %d\n", __func__, status);

	/* Resubmit urb so we continue receiving IRQ data */
	if (status != -ESHUTDOWN && status != -ENOENT) {
		usb_mark_last_busy(serial->dev);
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err && err != -EPERM)
			dev_err(&port->dev, "%s: resubmit intr urb "
				"failed. (%d)\n", __func__, err);
	}
}

static int sierra_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct sierra_port_private *portdata = usb_get_serial_port_data(port);
	unsigned long flags;

	/* try to give a good number back based on if we have any free urbs at
	 * this point in time */
	spin_lock_irqsave(&portdata->lock, flags);
	if (portdata->outstanding_urbs > (portdata->num_out_urbs * 2) / 3) {
		spin_unlock_irqrestore(&portdata->lock, flags);
		dev_dbg(&port->dev, "%s - write limit hit\n", __func__);
		return 0;
	}
	spin_unlock_irqrestore(&portdata->lock, flags);

	return 2048;
}

static void sierra_stop_rx_urbs(struct usb_serial_port *port)
{
	int i;
	struct sierra_port_private *portdata = usb_get_serial_port_data(port);

	for (i = 0; i < portdata->num_in_urbs; i++)
		usb_kill_urb(portdata->in_urbs[i]);

	usb_kill_urb(port->interrupt_in_urb);
}

static int sierra_submit_rx_urbs(struct usb_serial_port *port, gfp_t mem_flags)
{
	int ok_cnt;
	int err = -EINVAL;
	int i;
	struct urb *urb;
	struct sierra_port_private *portdata = usb_get_serial_port_data(port);

	ok_cnt = 0;
	for (i = 0; i < portdata->num_in_urbs; i++) {
		urb = portdata->in_urbs[i];
		if (!urb)
			continue;
		err = usb_submit_urb(urb, mem_flags);
		if (err) {
			dev_err(&port->dev, "%s: submit urb failed: %d\n",
				__func__, err);
		} else {
			ok_cnt++;
		}
	}

	if (ok_cnt && port->interrupt_in_urb) {
		err = usb_submit_urb(port->interrupt_in_urb, mem_flags);
		if (err) {
			dev_err(&port->dev, "%s: submit intr urb failed: %d\n",
				__func__, err);
		}
	}

	if (ok_cnt > 0) /* at least one rx urb submitted */
		return 0;
	else
		return err;
}

static struct urb *sierra_setup_urb(struct usb_serial *serial, int endpoint,
					int dir, void *ctx, int len,
					gfp_t mem_flags,
					usb_complete_t callback)
{
	struct urb	*urb;
	u8		*buf;

	if (endpoint == -1)
		return NULL;

	urb = usb_alloc_urb(0, mem_flags);
	if (urb == NULL) {
		dev_dbg(&serial->dev->dev, "%s: alloc for endpoint %d failed\n",
			__func__, endpoint);
		return NULL;
	}

	buf = kmalloc(len, mem_flags);
	if (buf) {
		/* Fill URB using supplied data */
		usb_fill_bulk_urb(urb, serial->dev,
			usb_sndbulkpipe(serial->dev, endpoint) | dir,
			buf, len, callback, ctx);

		dev_dbg(&serial->dev->dev, "%s %c u : %p d:%p\n", __func__,
				dir == USB_DIR_IN ? 'i' : 'o', urb, buf);
	} else {
		dev_dbg(&serial->dev->dev, "%s %c u:%p d:%p\n", __func__,
				dir == USB_DIR_IN ? 'i' : 'o', urb, buf);

		sierra_release_urb(urb);
		urb = NULL;
	}

	return urb;
}

static void sierra_close(struct usb_serial_port *port)
{
	int i;
	struct usb_serial *serial = port->serial;
	struct sierra_port_private *portdata;
	struct sierra_intf_private *intfdata = port->serial->private;

	portdata = usb_get_serial_port_data(port);

	portdata->rts_state = 0;
	portdata->dtr_state = 0;

	mutex_lock(&serial->disc_mutex);
	if (!serial->disconnected) {
		serial->interface->needs_remote_wakeup = 0;
		/* odd error handling due to pm counters */
		if (!usb_autopm_get_interface(serial->interface))
			sierra_send_setup(port);
		else
			usb_autopm_get_interface_no_resume(serial->interface);

	}
	mutex_unlock(&serial->disc_mutex);
	spin_lock_irq(&intfdata->susp_lock);
	portdata->opened = 0;
	spin_unlock_irq(&intfdata->susp_lock);

	sierra_stop_rx_urbs(port);
	for (i = 0; i < portdata->num_in_urbs; i++) {
		sierra_release_urb(portdata->in_urbs[i]);
		portdata->in_urbs[i] = NULL;
	}
}

static int sierra_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct sierra_port_private *portdata;
	struct usb_serial *serial = port->serial;
	struct sierra_intf_private *intfdata = serial->private;
	int i;
	int err;
	int endpoint;
	struct urb *urb;

	portdata = usb_get_serial_port_data(port);

	/* Set some sane defaults */
	portdata->rts_state = 1;
	portdata->dtr_state = 1;


	endpoint = port->bulk_in_endpointAddress;
	for (i = 0; i < portdata->num_in_urbs; i++) {
		urb = sierra_setup_urb(serial, endpoint, USB_DIR_IN, port,
					IN_BUFLEN, GFP_KERNEL,
					sierra_indat_callback);
		portdata->in_urbs[i] = urb;
	}
	/* clear halt condition */
	usb_clear_halt(serial->dev,
			usb_sndbulkpipe(serial->dev, endpoint) | USB_DIR_IN);

	err = sierra_submit_rx_urbs(port, GFP_KERNEL);
	if (err) {
		/* get rid of everything as in close */
		sierra_close(port);
		/* restore balance for autopm */
		if (!serial->disconnected)
			usb_autopm_put_interface(serial->interface);
		return err;
	}
	sierra_send_setup(port);

	serial->interface->needs_remote_wakeup = 1;
	spin_lock_irq(&intfdata->susp_lock);
	portdata->opened = 1;
	spin_unlock_irq(&intfdata->susp_lock);
	usb_autopm_put_interface(serial->interface);

	return 0;
}


static void sierra_dtr_rts(struct usb_serial_port *port, int on)
{
	struct sierra_port_private *portdata;

	portdata = usb_get_serial_port_data(port);
	portdata->rts_state = on;
	portdata->dtr_state = on;

	sierra_send_setup(port);
}

static int sierra_startup(struct usb_serial *serial)
{
	struct sierra_intf_private *intfdata;

	intfdata = kzalloc(sizeof(*intfdata), GFP_KERNEL);
	if (!intfdata)
		return -ENOMEM;

	spin_lock_init(&intfdata->susp_lock);

	usb_set_serial_data(serial, intfdata);

	/* Set Device mode to D0 */
	sierra_set_power_state(serial->dev, 0x0000);

	/* Check NMEA and set */
	if (nmea)
		sierra_vsc_set_nmea(serial->dev, 1);

	return 0;
}

static void sierra_release(struct usb_serial *serial)
{
	struct sierra_intf_private *intfdata;

	intfdata = usb_get_serial_data(serial);
	kfree(intfdata);
}

static int sierra_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct sierra_port_private *portdata;
	const struct sierra_iface_info *himemoryp;
	u8 ifnum;

	portdata = kzalloc(sizeof(*portdata), GFP_KERNEL);
	if (!portdata)
		return -ENOMEM;

	spin_lock_init(&portdata->lock);
	init_usb_anchor(&portdata->active);
	init_usb_anchor(&portdata->delayed);

	/* Assume low memory requirements */
	portdata->num_out_urbs = N_OUT_URB;
	portdata->num_in_urbs  = N_IN_URB;

	/* Determine actual memory requirements */
	if (serial->num_ports == 1) {
		/* Get interface number for composite device */
		ifnum = sierra_calc_interface(serial);
		himemoryp = &typeB_interface_list;
	} else {
		/* This is really the usb-serial port number of the interface
		 * rather than the interface number.
		 */
		ifnum = port->port_number;
		himemoryp = &typeA_interface_list;
	}

	if (is_himemory(ifnum, himemoryp)) {
		portdata->num_out_urbs = N_OUT_URB_HM;
		portdata->num_in_urbs  = N_IN_URB_HM;
	}

	dev_dbg(&port->dev,
			"Memory usage (urbs) interface #%d, in=%d, out=%d\n",
			ifnum, portdata->num_in_urbs, portdata->num_out_urbs);

	usb_set_serial_port_data(port, portdata);

	return 0;
}

static int sierra_port_remove(struct usb_serial_port *port)
{
	struct sierra_port_private *portdata;

	portdata = usb_get_serial_port_data(port);
	kfree(portdata);

	return 0;
}

#ifdef CONFIG_PM
static void stop_read_write_urbs(struct usb_serial *serial)
{
	int i;
	struct usb_serial_port *port;
	struct sierra_port_private *portdata;

	/* Stop reading/writing urbs */
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);
		sierra_stop_rx_urbs(port);
		usb_kill_anchored_urbs(&portdata->active);
	}
}

static int sierra_suspend(struct usb_serial *serial, pm_message_t message)
{
	struct sierra_intf_private *intfdata;
	int b;

	if (PMSG_IS_AUTO(message)) {
		intfdata = serial->private;
		spin_lock_irq(&intfdata->susp_lock);
		b = intfdata->in_flight;

		if (b) {
			spin_unlock_irq(&intfdata->susp_lock);
			return -EBUSY;
		} else {
			intfdata->suspended = 1;
			spin_unlock_irq(&intfdata->susp_lock);
		}
	}
	stop_read_write_urbs(serial);

	return 0;
}

static int sierra_resume(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	struct sierra_intf_private *intfdata = serial->private;
	struct sierra_port_private *portdata;
	struct urb *urb;
	int ec = 0;
	int i, err;

	spin_lock_irq(&intfdata->susp_lock);
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);

		while ((urb = usb_get_from_anchor(&portdata->delayed))) {
			usb_anchor_urb(urb, &portdata->active);
			intfdata->in_flight++;
			err = usb_submit_urb(urb, GFP_ATOMIC);
			if (err < 0) {
				intfdata->in_flight--;
				usb_unanchor_urb(urb);
				usb_scuttle_anchored_urbs(&portdata->delayed);
				break;
			}
		}

		if (portdata->opened) {
			err = sierra_submit_rx_urbs(port, GFP_ATOMIC);
			if (err)
				ec++;
		}
	}
	intfdata->suspended = 0;
	spin_unlock_irq(&intfdata->susp_lock);

	return ec ? -EIO : 0;
}

#else
#define sierra_suspend NULL
#define sierra_resume NULL
#endif

static struct usb_serial_driver sierra_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"sierra",
	},
	.description       = "Sierra USB modem",
	.id_table          = id_table,
	.calc_num_ports	   = sierra_calc_num_ports,
	.probe		   = sierra_probe,
	.open              = sierra_open,
	.close             = sierra_close,
	.dtr_rts	   = sierra_dtr_rts,
	.write             = sierra_write,
	.write_room        = sierra_write_room,
	.set_termios       = sierra_set_termios,
	.tiocmget          = sierra_tiocmget,
	.tiocmset          = sierra_tiocmset,
	.attach            = sierra_startup,
	.release           = sierra_release,
	.port_probe        = sierra_port_probe,
	.port_remove       = sierra_port_remove,
	.suspend	   = sierra_suspend,
	.resume		   = sierra_resume,
	.read_int_callback = sierra_instat_callback,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&sierra_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(nmea, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(nmea, "NMEA streaming");
