/*
 * USB HandSpring Visor, Palm m50x, and Sony Clie driver
 * (supports all of the Palm OS USB devices)
 *
 *	Copyright (C) 1999 - 2004
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * (06/03/2003) Judd Montgomery <judd at jpilot.org>
 *     Added support for module parameter options for untested/unknown
 *     devices.
 *
 * (03/09/2003) gkh
 *	Added support for the Sony Clie NZ90V device.  Thanks to Martin Brachtl
 *	<brachtl@redgrep.cz> for the information.
 *
 * (03/05/2003) gkh
 *	Think Treo support is now working.
 *
 * (04/03/2002) gkh
 *	Added support for the Sony OS 4.1 devices.  Thanks to Hiroyuki ARAKI
 *	<hiro@zob.ne.jp> for the information.
 *
 * (03/27/2002) gkh
 *	Removed assumptions that port->tty was always valid (is not true
 *	for usb serial console devices.)
 *
 * (03/23/2002) gkh
 *	Added support for the Palm i705 device, thanks to Thomas Riemer
 *	<tom@netmech.com> for the information.
 *
 * (03/21/2002) gkh
 *	Added support for the Palm m130 device, thanks to Udo Eisenbarth
 *	<udo.eisenbarth@web.de> for the information.
 *
 * (02/27/2002) gkh
 *	Reworked the urb handling logic.  We have no more pool, but dynamically
 *	allocate the urb and the transfer buffer on the fly.  In testing this
 *	does not incure any measurable overhead.  This also relies on the fact
 *	that we have proper reference counting logic for urbs.
 *
 * (02/21/2002) SilaS
 *  Added initial support for the Palm m515 devices.
 *
 * (02/14/2002) gkh
 *	Added support for the Clie S-360 device.
 *
 * (12/18/2001) gkh
 *	Added better Clie support for 3.5 devices.  Thanks to Geoffrey Levand
 *	for the patch.
 *
 * (11/11/2001) gkh
 *	Added support for the m125 devices, and added check to prevent oopses
 *	for Clié devices that lie about the number of ports they have.
 *
 * (08/30/2001) gkh
 *	Added support for the Clie devices, both the 3.5 and 4.0 os versions.
 *	Many thanks to Daniel Burke, and Bryan Payne for helping with this.
 *
 * (08/23/2001) gkh
 *	fixed a few potential bugs pointed out by Oliver Neukum.
 *
 * (05/30/2001) gkh
 *	switched from using spinlock to a semaphore, which fixes lots of problems.
 *
 * (05/28/2000) gkh
 *	Added initial support for the Palm m500 and Palm m505 devices.
 *
 * (04/08/2001) gb
 *	Identify version on module load.
 *
 * (01/21/2000) gkh
 *	Added write_room and chars_in_buffer, as they were previously using the
 *	generic driver versions which is all wrong now that we are using an urb
 *	pool.  Thanks to Wolfgang Grandegger for pointing this out to me.
 *	Removed count assignment in the write function, which was not needed anymore
 *	either.  Thanks to Al Borchers for pointing this out.
 *
 * (12/12/2000) gkh
 *	Moved MOD_DEC to end of visor_close to be nicer, as the final write 
 *	message can sleep.
 * 
 * (11/12/2000) gkh
 *	Fixed bug with data being dropped on the floor by forcing tty->low_latency
 *	to be on.  Hopefully this fixes the OHCI issue!
 *
 * (11/01/2000) Adam J. Richter
 *	usb_device_id table support
 * 
 * (10/05/2000) gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 * 
 * (09/11/2000) gkh
 *	Got rid of always calling kmalloc for every urb we wrote out to the
 *	device.
 *	Added visor_read_callback so we can keep track of bytes in and out for
 *	those people who like to know the speed of their device.
 *	Removed DEBUG #ifdefs with call to usb_serial_debug_data
 *
 * (09/06/2000) gkh
 *	Fixed oops in visor_exit.  Need to uncomment usb_unlink_urb call _after_
 *	the host controller drivers set urb->dev = NULL when the urb is finished.
 *
 * (08/28/2000) gkh
 *	Added locks for SMP safeness.
 *
 * (08/08/2000) gkh
 *	Fixed endian problem in visor_startup.
 *	Fixed MOD_INC and MOD_DEC logic and the ability to open a port more 
 *	than once.
 * 
 * (07/23/2000) gkh
 *	Added pool of write urbs to speed up transfers to the visor.
 * 
 * (07/19/2000) gkh
 *	Added module_init and module_exit functions to handle the fact that this
 *	driver is a loadable module now.
 *
 * (07/03/2000) gkh
 *	Added visor_set_ioctl and visor_set_termios functions (they don't do much
 *	of anything, but are good for debugging.)
 * 
 * (06/25/2000) gkh
 *	Fixed bug in visor_unthrottle that should help with the disconnect in PPP
 *	bug that people have been reporting.
 *
 * (06/23/2000) gkh
 *	Cleaned up debugging statements in a quest to find UHCI timeout bug.
 *
 * (04/27/2000) Ryan VanderBijl
 * 	Fixed memory leak in visor_close
 *
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include "usb-serial.h"
#include "visor.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.1"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC "USB HandSpring Visor / Palm OS driver"

/* function prototypes for a handspring visor */
static int  visor_open		(struct usb_serial_port *port, struct file *filp);
static void visor_close		(struct usb_serial_port *port, struct file *filp);
static int  visor_write		(struct usb_serial_port *port, const unsigned char *buf, int count);
static int  visor_write_room		(struct usb_serial_port *port);
static int  visor_chars_in_buffer	(struct usb_serial_port *port);
static void visor_throttle	(struct usb_serial_port *port);
static void visor_unthrottle	(struct usb_serial_port *port);
static int  visor_probe		(struct usb_serial *serial, const struct usb_device_id *id);
static int  visor_calc_num_ports(struct usb_serial *serial);
static void visor_shutdown	(struct usb_serial *serial);
static int  visor_ioctl		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
static void visor_set_termios	(struct usb_serial_port *port, struct termios *old_termios);
static void visor_write_bulk_callback	(struct urb *urb, struct pt_regs *regs);
static void visor_read_bulk_callback	(struct urb *urb, struct pt_regs *regs);
static void visor_read_int_callback	(struct urb *urb, struct pt_regs *regs);
static int  clie_3_5_startup	(struct usb_serial *serial);
static int  treo_attach		(struct usb_serial *serial);
static int clie_5_attach (struct usb_serial *serial);
static int palm_os_3_probe (struct usb_serial *serial, const struct usb_device_id *id);
static int palm_os_4_probe (struct usb_serial *serial, const struct usb_device_id *id);

/* Parameters that may be passed into the module. */
static int debug;
static __u16 vendor;
static __u16 product;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_VISOR_ID),
		.driver_info = (kernel_ulong_t)&palm_os_3_probe },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO600_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M500_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M505_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M515_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_I705_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M100_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M125_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M130_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_T_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TREO_650),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_Z_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_ZIRE31_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_ZIRE_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_0_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_S360_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_1_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NX60_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NZ90V_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_TJ25_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SCH_I330_ID), 
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SPH_I500_ID), 
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(TAPWAVE_VENDOR_ID, TAPWAVE_ZODIAC_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(GARMIN_VENDOR_ID, GARMIN_IQUE_3600_ID), 
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(ACEECA_VENDOR_ID, ACEECA_MEZ1000_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(KYOCERA_VENDOR_ID, KYOCERA_7135_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ USB_DEVICE(FOSSIL_VENDOR_ID, FOSSIL_ABACUS_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ },					/* optional parameter entry */
	{ }					/* Terminating entry */
};

static struct usb_device_id clie_id_5_table [] = {
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_UX50_ID),
		.driver_info = (kernel_ulong_t)&palm_os_4_probe },
	{ },					/* optional parameter entry */
	{ }					/* Terminating entry */
};

static struct usb_device_id clie_id_3_5_table [] = {
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_3_5_ID) },
	{ }					/* Terminating entry */
};

static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_VISOR_ID) },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO_ID) },
	{ USB_DEVICE(HANDSPRING_VENDOR_ID, HANDSPRING_TREO600_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M500_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M505_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M515_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_I705_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M100_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M125_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_M130_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_T_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TREO_650) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_TUNGSTEN_Z_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_ZIRE31_ID) },
	{ USB_DEVICE(PALM_VENDOR_ID, PALM_ZIRE_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_3_5_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_0_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_S360_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_4_1_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NX60_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_NZ90V_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_UX50_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_CLIE_TJ25_ID) },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SCH_I330_ID) },
	{ USB_DEVICE(SAMSUNG_VENDOR_ID, SAMSUNG_SPH_I500_ID) },
	{ USB_DEVICE(TAPWAVE_VENDOR_ID, TAPWAVE_ZODIAC_ID) },
	{ USB_DEVICE(GARMIN_VENDOR_ID, GARMIN_IQUE_3600_ID) },
	{ USB_DEVICE(ACEECA_VENDOR_ID, ACEECA_MEZ1000_ID) },
	{ USB_DEVICE(KYOCERA_VENDOR_ID, KYOCERA_7135_ID) },
	{ USB_DEVICE(FOSSIL_VENDOR_ID, FOSSIL_ABACUS_ID) },
	{ },					/* optional parameter entry */
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver visor_driver = {
	.owner =	THIS_MODULE,
	.name =		"visor",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
};

/* All of the device info needed for the Handspring Visor, and Palm 4.0 devices */
static struct usb_serial_device_type handspring_device = {
	.owner =		THIS_MODULE,
	.name =			"Handspring Visor / Palm OS",
	.short_name =		"visor",
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		2,
	.num_bulk_out =		2,
	.num_ports =		2,
	.open =			visor_open,
	.close =		visor_close,
	.throttle =		visor_throttle,
	.unthrottle =		visor_unthrottle,
	.attach =		treo_attach,
	.probe =		visor_probe,
	.calc_num_ports =	visor_calc_num_ports,
	.shutdown =		visor_shutdown,
	.ioctl =		visor_ioctl,
	.set_termios =		visor_set_termios,
	.write =		visor_write,
	.write_room =		visor_write_room,
	.chars_in_buffer =	visor_chars_in_buffer,
	.write_bulk_callback =	visor_write_bulk_callback,
	.read_bulk_callback =	visor_read_bulk_callback,
	.read_int_callback =	visor_read_int_callback,
};

/* All of the device info needed for the Clie UX50, TH55 Palm 5.0 devices */
static struct usb_serial_device_type clie_5_device = {
	.owner =		THIS_MODULE,
	.name =			"Sony Clie 5.0",
	.short_name =		"clie_5",
	.id_table =		clie_id_5_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		2,
	.num_bulk_out =		2,
	.num_ports =		2,
	.open =			visor_open,
	.close =		visor_close,
	.throttle =		visor_throttle,
	.unthrottle =		visor_unthrottle,
	.attach =		clie_5_attach,
	.probe =		visor_probe,
	.calc_num_ports =	visor_calc_num_ports,
	.shutdown =		visor_shutdown,
	.ioctl =		visor_ioctl,
	.set_termios =		visor_set_termios,
	.write =		visor_write,
	.write_room =		visor_write_room,
	.chars_in_buffer =	visor_chars_in_buffer,
	.write_bulk_callback =	visor_write_bulk_callback,
	.read_bulk_callback =	visor_read_bulk_callback,
	.read_int_callback =	visor_read_int_callback,
};

/* device info for the Sony Clie OS version 3.5 */
static struct usb_serial_device_type clie_3_5_device = {
	.owner =		THIS_MODULE,
	.name =			"Sony Clie 3.5",
	.short_name =		"clie_3.5",
	.id_table =		clie_id_3_5_table,
	.num_interrupt_in =	0,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_ports =		1,
	.open =			visor_open,
	.close =		visor_close,
	.throttle =		visor_throttle,
	.unthrottle =		visor_unthrottle,
	.attach =		clie_3_5_startup,
	.ioctl =		visor_ioctl,
	.set_termios =		visor_set_termios,
	.write =		visor_write,
	.write_room =		visor_write_room,
	.chars_in_buffer =	visor_chars_in_buffer,
	.write_bulk_callback =	visor_write_bulk_callback,
	.read_bulk_callback =	visor_read_bulk_callback,
};

struct visor_private {
	spinlock_t lock;
	int bytes_in;
	int bytes_out;
	int outstanding_urbs;
	int throttled;
};

/* number of outstanding urbs to prevent userspace DoS from happening */
#define URB_UPPER_LIMIT	42

static int stats;

/******************************************************************************
 * Handspring Visor specific driver functions
 ******************************************************************************/
static int visor_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	struct visor_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->read_urb) {
		/* this is needed for some brain dead Sony devices */
		dev_err(&port->dev, "Device lied about number of ports, please use a lower one.\n");
		return -ENODEV;
	}

	spin_lock_irqsave(&priv->lock, flags);
	priv->bytes_in = 0;
	priv->bytes_out = 0;
	priv->outstanding_urbs = 0;
	priv->throttled = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	/*
	 * Force low_latency on so that our tty_push actually forces the data
	 * through, otherwise it is scheduled, and with high data rates (like
	 * with OHCI) data can get lost.
	 */
	if (port->tty)
		port->tty->low_latency = 1;

	/* Start reading from the device */
	usb_fill_bulk_urb (port->read_urb, serial->dev,
			   usb_rcvbulkpipe (serial->dev, 
					    port->bulk_in_endpointAddress),
			   port->read_urb->transfer_buffer,
			   port->read_urb->transfer_buffer_length,
			   visor_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "%s - failed submitting read urb, error %d\n",
			__FUNCTION__, result);
		goto exit;
	}
	
	if (port->interrupt_in_urb) {
		dbg("%s - adding interrupt input for treo", __FUNCTION__);
		result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (result)
			dev_err(&port->dev, "%s - failed submitting interrupt urb, error %d\n",
				__FUNCTION__, result);
	}
exit:	
	return result;
}


static void visor_close (struct usb_serial_port *port, struct file * filp)
{
	struct visor_private *priv = usb_get_serial_port_data(port);
	unsigned char *transfer_buffer;

	dbg("%s - port %d", __FUNCTION__, port->number);
			 
	/* shutdown our urbs */
	usb_kill_urb(port->read_urb);
	if (port->interrupt_in_urb)
		usb_kill_urb(port->interrupt_in_urb);

	/* Try to send shutdown message, if the device is gone, this will just fail. */
	transfer_buffer =  kmalloc (0x12, GFP_KERNEL);
	if (transfer_buffer) {
		usb_control_msg (port->serial->dev,
				 usb_rcvctrlpipe(port->serial->dev, 0),
				 VISOR_CLOSE_NOTIFICATION, 0xc2,
				 0x0000, 0x0000, 
				 transfer_buffer, 0x12, 300);
		kfree (transfer_buffer);
	}

	if (stats)
		dev_info(&port->dev, "Bytes In = %d  Bytes Out = %d\n",
			 priv->bytes_in, priv->bytes_out);
}


static int visor_write (struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct visor_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	struct urb *urb;
	unsigned char *buffer;
	unsigned long flags;
	int status;

	dbg("%s - port %d", __FUNCTION__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->outstanding_urbs > URB_UPPER_LIMIT) {
		spin_unlock_irqrestore(&priv->lock, flags);
		dbg("%s - write limit hit\n", __FUNCTION__);
		return 0;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	buffer = kmalloc (count, GFP_ATOMIC);
	if (!buffer) {
		dev_err(&port->dev, "out of memory\n");
		return -ENOMEM;
	}

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		dev_err(&port->dev, "no more free urbs\n");
		kfree (buffer);
		return -ENOMEM;
	}

	memcpy (buffer, buf, count);

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, count, buffer);

	usb_fill_bulk_urb (urb, serial->dev,
			   usb_sndbulkpipe (serial->dev,
					    port->bulk_out_endpointAddress),
			   buffer, count, 
			   visor_write_bulk_callback, port);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&port->dev, "%s - usb_submit_urb(write bulk) failed with status = %d\n",
			__FUNCTION__, status);
		count = status;
		kfree (buffer);
	} else {
		spin_lock_irqsave(&priv->lock, flags);
		++priv->outstanding_urbs;
		priv->bytes_out += count;
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	/* we are done with this urb, so let the host driver
	 * really free it when it is finished with it */
	usb_free_urb (urb);

	return count;
}


static int visor_write_room (struct usb_serial_port *port)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	/*
	 * We really can take anything the user throws at us
	 * but let's pick a nice big number to tell the tty
	 * layer that we have lots of free space
	 */
	return 2048;
}


static int visor_chars_in_buffer (struct usb_serial_port *port)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	/* 
	 * We can't really account for how much data we
	 * have sent out, but hasn't made it through to the
	 * device, so just tell the tty layer that everything
	 * is flushed.
	 */
	return 0;
}


static void visor_write_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct visor_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	/* free up the transfer buffer, as usb_free_urb() does not do this */
	kfree (urb->transfer_buffer);

	dbg("%s - port %d", __FUNCTION__, port->number);
	
	if (urb->status)
		dbg("%s - nonzero write bulk status received: %d",
		    __FUNCTION__, urb->status);

	spin_lock_irqsave(&priv->lock, flags);
	--priv->outstanding_urbs;
	spin_unlock_irqrestore(&priv->lock, flags);

	schedule_work(&port->work);
}


static void visor_read_bulk_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct visor_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	unsigned long flags;
	int i;
	int throttled;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (urb->status) {
		dbg("%s - nonzero read bulk status received: %d", __FUNCTION__, urb->status);
		return;
	}

	usb_serial_debug_data(debug, &port->dev, __FUNCTION__, urb->actual_length, data);

	tty = port->tty;
	if (tty && urb->actual_length) {
		for (i = 0; i < urb->actual_length ; ++i) {
			/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them. */
			if(tty->flip.count >= TTY_FLIPBUF_SIZE) {
				tty_flip_buffer_push(tty);
			}
			/* this doesn't actually push the data through unless tty->low_latency is set */
			tty_insert_flip_char(tty, data[i], 0);
		}
		tty_flip_buffer_push(tty);
	}
	spin_lock_irqsave(&priv->lock, flags);
	priv->bytes_in += urb->actual_length;
	throttled = priv->throttled;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Continue trying to always read if we should */
	if (!throttled) {
		usb_fill_bulk_urb (port->read_urb, port->serial->dev,
				   usb_rcvbulkpipe(port->serial->dev,
						   port->bulk_in_endpointAddress),
				   port->read_urb->transfer_buffer,
				   port->read_urb->transfer_buffer_length,
				   visor_read_bulk_callback, port);
		result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
		if (result)
			dev_err(&port->dev, "%s - failed resubmitting read urb, error %d\n", __FUNCTION__, result);
	}
	return;
}

static void visor_read_int_callback (struct urb *urb, struct pt_regs *regs)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	int result;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
		    __FUNCTION__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d",
		    __FUNCTION__, urb->status);
		goto exit;
	}

	/*
	 * This information is still unknown what it can be used for.
	 * If anyone has an idea, please let the author know...
	 *
	 * Rumor has it this endpoint is used to notify when data
	 * is ready to be read from the bulk ones.
	 */
	usb_serial_debug_data(debug, &port->dev, __FUNCTION__,
			      urb->actual_length, urb->transfer_buffer);

exit:
	result = usb_submit_urb (urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev, "%s - Error %d submitting interrupt urb\n",
			__FUNCTION__, result);
}

static void visor_throttle (struct usb_serial_port *port)
{
	struct visor_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __FUNCTION__, port->number);
	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = 1;
	spin_unlock_irqrestore(&priv->lock, flags);
}


static void visor_unthrottle (struct usb_serial_port *port)
{
	struct visor_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);
	spin_lock_irqsave(&priv->lock, flags);
	priv->throttled = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	port->read_urb->dev = port->serial->dev;
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev, "%s - failed submitting read urb, error %d\n", __FUNCTION__, result);
}

static int palm_os_3_probe (struct usb_serial *serial, const struct usb_device_id *id)
{
	struct device *dev = &serial->dev->dev;
	struct visor_connection_info *connection_info;
	unsigned char *transfer_buffer;
	char *string;
	int retval = 0;
	int i;
	int num_ports = 0;

	dbg("%s", __FUNCTION__);

	transfer_buffer = kmalloc (sizeof (*connection_info), GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(dev, "%s - kmalloc(%Zd) failed.\n", __FUNCTION__,
			sizeof(*connection_info));
		return -ENOMEM;
	}

	/* send a get connection info request */
	retval = usb_control_msg (serial->dev,
				  usb_rcvctrlpipe(serial->dev, 0),
				  VISOR_GET_CONNECTION_INFORMATION,
				  0xc2, 0x0000, 0x0000, transfer_buffer,
				  sizeof(*connection_info), 300);
	if (retval < 0) {
		dev_err(dev, "%s - error %d getting connection information\n",
			__FUNCTION__, retval);
		goto exit;
	}

	if (retval == sizeof(*connection_info)) {
	        connection_info = (struct visor_connection_info *)transfer_buffer;

		num_ports = le16_to_cpu(connection_info->num_ports);
		for (i = 0; i < num_ports; ++i) {
			switch (connection_info->connections[i].port_function_id) {
				case VISOR_FUNCTION_GENERIC:
					string = "Generic";
					break;
				case VISOR_FUNCTION_DEBUGGER:
					string = "Debugger";
					break;
				case VISOR_FUNCTION_HOTSYNC:
					string = "HotSync";
					break;
				case VISOR_FUNCTION_CONSOLE:
					string = "Console";
					break;
				case VISOR_FUNCTION_REMOTE_FILE_SYS:
					string = "Remote File System";
					break;
				default:
					string = "unknown";
					break;
			}
			dev_info(dev, "%s: port %d, is for %s use\n",
				serial->type->name,
				connection_info->connections[i].port, string);
		}
	}
	/*
	* Handle devices that report invalid stuff here.
	*/
	if (num_ports == 0 || num_ports > 2) {
		dev_warn (dev, "%s: No valid connect info available\n",
			serial->type->name);
		num_ports = 2;
	}
  
	dev_info(dev, "%s: Number of ports: %d\n", serial->type->name,
		num_ports);

	/*
	 * save off our num_ports info so that we can use it in the
	 * calc_num_ports callback
	 */
	usb_set_serial_data(serial, (void *)(long)num_ports);

	/* ask for the number of bytes available, but ignore the response as it is broken */
	retval = usb_control_msg (serial->dev,
				  usb_rcvctrlpipe(serial->dev, 0),
				  VISOR_REQUEST_BYTES_AVAILABLE,
				  0xc2, 0x0000, 0x0005, transfer_buffer,
				  0x02, 300);
	if (retval < 0)
		dev_err(dev, "%s - error %d getting bytes available request\n",
			__FUNCTION__, retval);
	retval = 0;

exit:
	kfree (transfer_buffer);

	return retval;
}

static int palm_os_4_probe (struct usb_serial *serial, const struct usb_device_id *id)
{
	struct device *dev = &serial->dev->dev;
	struct palm_ext_connection_info *connection_info;
	unsigned char *transfer_buffer;
	int retval;

	dbg("%s", __FUNCTION__);

	transfer_buffer =  kmalloc (sizeof (*connection_info), GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(dev, "%s - kmalloc(%Zd) failed.\n", __FUNCTION__,
			sizeof(*connection_info));
		return -ENOMEM;
	}

	retval = usb_control_msg (serial->dev,
				  usb_rcvctrlpipe(serial->dev, 0), 
				  PALM_GET_EXT_CONNECTION_INFORMATION,
				  0xc2, 0x0000, 0x0000, transfer_buffer,
				  sizeof (*connection_info), 300);
	if (retval < 0)
		dev_err(dev, "%s - error %d getting connection info\n",
			__FUNCTION__, retval);
	else
		usb_serial_debug_data(debug, &serial->dev->dev, __FUNCTION__,
				      retval, transfer_buffer);

	kfree (transfer_buffer);
	return 0;
}


static int visor_probe (struct usb_serial *serial, const struct usb_device_id *id)
{
	int retval = 0;
	int (*startup) (struct usb_serial *serial, const struct usb_device_id *id);

	dbg("%s", __FUNCTION__);

	if (serial->dev->actconfig->desc.bConfigurationValue != 1) {
		err("active config #%d != 1 ??",
			serial->dev->actconfig->desc.bConfigurationValue);
		return -ENODEV;
	}

	if (id->driver_info) {
		startup = (void *)id->driver_info;
		retval = startup(serial, id);
	}

	return retval;
}

static int visor_calc_num_ports (struct usb_serial *serial)
{
	int num_ports = (int)(long)(usb_get_serial_data(serial));

	if (num_ports)
		usb_set_serial_data(serial, NULL);

	return num_ports;
}

static int generic_startup(struct usb_serial *serial)
{
	struct visor_private *priv;
	int i;

	for (i = 0; i < serial->num_ports; ++i) {
		priv = kmalloc (sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;
		memset (priv, 0x00, sizeof(*priv));
		spin_lock_init(&priv->lock);
		usb_set_serial_port_data(serial->port[i], priv);
	}
	return 0;
}

static int clie_3_5_startup (struct usb_serial *serial)
{
	struct device *dev = &serial->dev->dev;
	int result;
	u8 data;

	dbg("%s", __FUNCTION__);

	/*
	 * Note that PEG-300 series devices expect the following two calls.
	 */

	/* get the config number */
	result = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				  USB_REQ_GET_CONFIGURATION, USB_DIR_IN,
				  0, 0, &data, 1, 3000);
	if (result < 0) {
		dev_err(dev, "%s: get config number failed: %d\n", __FUNCTION__, result);
		return result;
	}
	if (result != 1) {
		dev_err(dev, "%s: get config number bad return length: %d\n", __FUNCTION__, result);
		return -EIO;
	}

	/* get the interface number */
	result = usb_control_msg (serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				  USB_REQ_GET_INTERFACE, 
				  USB_DIR_IN | USB_RECIP_INTERFACE,
				  0, 0, &data, 1, 3000);
	if (result < 0) {
		dev_err(dev, "%s: get interface number failed: %d\n", __FUNCTION__, result);
		return result;
	}
	if (result != 1) {
		dev_err(dev, "%s: get interface number bad return length: %d\n", __FUNCTION__, result);
		return -EIO;
	}

	return generic_startup(serial);
}
 
static int treo_attach (struct usb_serial *serial)
{
	struct usb_serial_port *swap_port;

	/* Only do this endpoint hack for the Handspring devices with
	 * interrupt in endpoints, which for now are the Treo devices. */
	if (!((le16_to_cpu(serial->dev->descriptor.idVendor) == HANDSPRING_VENDOR_ID) ||
	      (le16_to_cpu(serial->dev->descriptor.idVendor) == KYOCERA_VENDOR_ID)) ||
	    (serial->num_interrupt_in == 0))
		goto generic_startup;

	dbg("%s", __FUNCTION__);

	/*
	* It appears that Treos and Kyoceras want to use the 
	* 1st bulk in endpoint to communicate with the 2nd bulk out endpoint, 
	* so let's swap the 1st and 2nd bulk in and interrupt endpoints.  
	* Note that swapping the bulk out endpoints would break lots of 
	* apps that want to communicate on the second port.
	*/
#define COPY_PORT(dest, src)						\
	dest->read_urb = src->read_urb;					\
	dest->bulk_in_endpointAddress = src->bulk_in_endpointAddress;	\
	dest->bulk_in_buffer = src->bulk_in_buffer;			\
	dest->interrupt_in_urb = src->interrupt_in_urb;			\
	dest->interrupt_in_endpointAddress = src->interrupt_in_endpointAddress;	\
	dest->interrupt_in_buffer = src->interrupt_in_buffer;

	swap_port = kmalloc(sizeof(*swap_port), GFP_KERNEL);
	if (!swap_port)
		return -ENOMEM;
	COPY_PORT(swap_port, serial->port[0]);
	COPY_PORT(serial->port[0], serial->port[1]);
	COPY_PORT(serial->port[1], swap_port);
	kfree(swap_port);

generic_startup:
	return generic_startup(serial);
}

static int clie_5_attach (struct usb_serial *serial)
{
	dbg("%s", __FUNCTION__);

	/* TH55 registers 2 ports. 
	   Communication in from the UX50/TH55 uses bulk_in_endpointAddress from port 0 
	   Communication out to the UX50/TH55 uses bulk_out_endpointAddress from port 1 
	   
	   Lets do a quick and dirty mapping
	 */
	
	/* some sanity check */
	if (serial->num_ports < 2)
		return -1;
		
	/* port 0 now uses the modified endpoint Address */
	serial->port[0]->bulk_out_endpointAddress = serial->port[1]->bulk_out_endpointAddress;

	return generic_startup(serial);
}

static void visor_shutdown (struct usb_serial *serial)
{
	dbg("%s", __FUNCTION__);
}

static int visor_ioctl (struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg)
{
	dbg("%s - port %d, cmd 0x%.4x", __FUNCTION__, port->number, cmd);

	return -ENOIOCTLCMD;
}


/* This function is all nice and good, but we don't change anything based on it :) */
static void visor_set_termios (struct usb_serial_port *port, struct termios *old_termios)
{
	unsigned int cflag;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if ((!port->tty) || (!port->tty->termios)) {
		dbg("%s - no tty structures", __FUNCTION__);
		return;
	}

	cflag = port->tty->termios->c_cflag;
	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(port->tty->termios->c_iflag) == RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("%s - nothing to change...", __FUNCTION__);
			return;
		}
	}

	/* get the byte size */
	switch (cflag & CSIZE) {
		case CS5:	dbg("%s - data bits = 5", __FUNCTION__);   break;
		case CS6:	dbg("%s - data bits = 6", __FUNCTION__);   break;
		case CS7:	dbg("%s - data bits = 7", __FUNCTION__);   break;
		default:
		case CS8:	dbg("%s - data bits = 8", __FUNCTION__);   break;
	}
	
	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & PARODD)
			dbg("%s - parity = odd", __FUNCTION__);
		else
			dbg("%s - parity = even", __FUNCTION__);
	else
		dbg("%s - parity = none", __FUNCTION__);

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		dbg("%s - stop bits = 2", __FUNCTION__);
	else
		dbg("%s - stop bits = 1", __FUNCTION__);

	
	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		dbg("%s - RTS/CTS is enabled", __FUNCTION__);
	else
		dbg("%s - RTS/CTS is disabled", __FUNCTION__);
	
	/* determine software flow control */
	if (I_IXOFF(port->tty))
		dbg("%s - XON/XOFF is enabled, XON = %2x, XOFF = %2x",
		    __FUNCTION__, START_CHAR(port->tty), STOP_CHAR(port->tty));
	else
		dbg("%s - XON/XOFF is disabled", __FUNCTION__);

	/* get the baud rate wanted */
	dbg("%s - baud rate = %d", __FUNCTION__, tty_get_baud_rate(port->tty));

	return;
}


static int __init visor_init (void)
{
	int i, retval;
	/* Only if parameters were passed to us */
	if ((vendor>0) && (product>0)) {
		struct usb_device_id usb_dev_temp[]=
			{{USB_DEVICE(vendor, product),
			.driver_info = (kernel_ulong_t)&palm_os_4_probe }};

		/* Find the last entry in id_table */
		for (i=0; ; i++) {
			if (id_table[i].idVendor==0) {
				id_table[i] = usb_dev_temp[0];
				break;
			}
		}
		/* Find the last entry in id_table_combined */
		for (i=0; ; i++) {
			if (id_table_combined[i].idVendor==0) {
				id_table_combined[i] = usb_dev_temp[0];
				break;
			}
		}
		info("Untested USB device specified at time of module insertion");
		info("Warning: This is not guaranteed to work");
		info("Using a newer kernel is preferred to this method");
		info("Adding Palm OS protocol 4.x support for unknown device: 0x%x/0x%x",
			vendor, product);
	}
	retval = usb_serial_register(&handspring_device);
	if (retval)
		goto failed_handspring_register;
	retval = usb_serial_register(&clie_3_5_device);
	if (retval)
		goto failed_clie_3_5_register;
	retval = usb_serial_register(&clie_5_device);
	if (retval)
		goto failed_clie_5_register;
	retval = usb_register(&visor_driver);
	if (retval) 
		goto failed_usb_register;
	info(DRIVER_DESC " " DRIVER_VERSION);

	return 0;
failed_usb_register:
	usb_serial_deregister(&clie_5_device);
failed_clie_5_register:
	usb_serial_deregister(&clie_3_5_device);
failed_clie_3_5_register:
	usb_serial_deregister(&handspring_device);
failed_handspring_register:
	return retval;
}


static void __exit visor_exit (void)
{
	usb_deregister (&visor_driver);
	usb_serial_deregister (&handspring_device);
	usb_serial_deregister (&clie_3_5_device);
	usb_serial_deregister (&clie_5_device);
}


module_init(visor_init);
module_exit(visor_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(stats, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(stats, "Enables statistics or not");

module_param(vendor, ushort, 0);
MODULE_PARM_DESC(vendor, "User specified vendor ID");
module_param(product, ushort, 0);
MODULE_PARM_DESC(product, "User specified product ID");

