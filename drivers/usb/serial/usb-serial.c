/*
 * USB Serial Converter driver
 *
 * Copyright (C) 1999 - 2004 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2000 Peter Berger (pberger@brimson.com)
 * Copyright (C) 2000 Al Borchers (borchers@steinerpoint.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 * This driver was originally based on the ACM driver by Armin Fuerst (which was 
 * based on a driver by Brad Keryan)
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * (12/10/2002) gkh
 *	Split the ports off into their own struct device, and added a
 *	usb-serial bus driver.
 *
 * (11/19/2002) gkh
 *	removed a few #ifdefs for the generic code and cleaned up the failure
 *	logic in initialization.
 *
 * (10/02/2002) gkh
 *	moved the console code to console.c and out of this file.
 *
 * (06/05/2002) gkh
 *	moved location of startup() call in serial_probe() until after all
 *	of the port information and endpoints are initialized.  This makes
 *	things easier for some drivers.
 *
 * (04/10/2002) gkh
 *	added serial_read_proc function which creates a
 *	/proc/tty/driver/usb-serial file.
 *
 * (03/27/2002) gkh
 *	Got USB serial console code working properly and merged into the main
 *	version of the tree.  Thanks to Randy Dunlap for the initial version
 *	of this code, and for pushing me to finish it up.
 *	The USB serial console works with any usb serial driver device.
 *
 * (03/21/2002) gkh
 *	Moved all manipulation of port->open_count into the core.  Now the
 *	individual driver's open and close functions are called only when the
 *	first open() and last close() is called.  Making the drivers a bit
 *	smaller and simpler.
 *	Fixed a bug if a driver didn't have the owner field set.
 *
 * (02/26/2002) gkh
 *	Moved all locking into the main serial_* functions, instead of having 
 *	the individual drivers have to grab the port semaphore.  This should
 *	reduce races.
 *	Reworked the MOD_INC logic a bit to always increment and decrement, even
 *	if the generic driver is being used.
 *
 * (10/10/2001) gkh
 *	usb_serial_disconnect() now sets the serial->dev pointer is to NULL to
 *	help prevent child drivers from accessing the device since it is now
 *	gone.
 *
 * (09/13/2001) gkh
 *	Moved generic driver initialize after we have registered with the USB
 *	core.  Thanks to Randy Dunlap for pointing this problem out.
 *
 * (07/03/2001) gkh
 *	Fixed module paramater size.  Thanks to John Brockmeyer for the pointer.
 *	Fixed vendor and product getting defined through the MODULE_PARM macro
 *	if the Generic driver wasn't compiled in.
 *	Fixed problem with generic_shutdown() not being called for drivers that
 *	don't have a shutdown() function.
 *
 * (06/06/2001) gkh
 *	added evil hack that is needed for the prolific pl2303 device due to the
 *	crazy way its endpoints are set up.
 *
 * (05/30/2001) gkh
 *	switched from using spinlock to a semaphore, which fixes lots of problems.
 *
 * (04/08/2001) gb
 *	Identify version on module load.
 *
 * 2001_02_05 gkh
 *	Fixed buffer overflows bug with the generic serial driver.  Thanks to
 *	Todd Squires <squirest@ct0.com> for fixing this.
 *
 * (01/10/2001) gkh
 *	Fixed bug where the generic serial adaptor grabbed _any_ device that was
 *	offered to it.
 *
 * (12/12/2000) gkh
 *	Removed MOD_INC and MOD_DEC from poll and disconnect functions, and
 *	moved them to the serial_open and serial_close functions.
 *	Also fixed bug with there not being a MOD_DEC for the generic driver
 *	(thanks to Gary Brubaker for finding this.)
 *
 * (11/29/2000) gkh
 *	Small NULL pointer initialization cleanup which saves a bit of disk image
 *
 * (11/01/2000) Adam J. Richter
 *	instead of using idVendor/idProduct pairs, usb serial drivers
 *	now identify their hardware interest with usb_device_id tables,
 *	which they usually have anyhow for use with MODULE_DEVICE_TABLE.
 *
 * (10/05/2000) gkh
 *	Fixed bug with urb->dev not being set properly, now that the usb
 *	core needs it.
 * 
 * (09/11/2000) gkh
 *	Removed DEBUG #ifdefs with call to usb_serial_debug_data
 *
 * (08/28/2000) gkh
 *	Added port_lock to port structure.
 *	Added locks for SMP safeness to generic driver
 *	Fixed the ability to open a generic device's port more than once.
 *
 * (07/23/2000) gkh
 *	Added bulk_out_endpointAddress to port structure.
 *
 * (07/19/2000) gkh, pberger, and borchers
 *	Modifications to allow usb-serial drivers to be modules.
 *
 * (07/03/2000) gkh
 *	Added more debugging to serial_ioctl call
 * 
 * (06/25/2000) gkh
 *	Changed generic_write_bulk_callback to not call wake_up_interruptible
 *	directly, but to have port_softint do it at a safer time.
 *
 * (06/23/2000) gkh
 *	Cleaned up debugging statements in a quest to find UHCI timeout bug.
 *
 * (05/22/2000) gkh
 *	Changed the makefile, enabling the big CONFIG_USB_SERIAL_SOMTHING to be 
 *	removed from the individual device source files.
 *
 * (05/03/2000) gkh
 *	Added the Digi Acceleport driver from Al Borchers and Peter Berger.
 * 
 * (05/02/2000) gkh
 *	Changed devfs and tty register code to work properly now. This was based on
 *	the ACM driver changes by Vojtech Pavlik.
 *
 * (04/27/2000) Ryan VanderBijl
 * 	Put calls to *_paranoia_checks into one function.
 * 
 * (04/23/2000) gkh
 *	Fixed bug that Randy Dunlap found for Generic devices with no bulk out ports.
 *	Moved when the startup code printed out the devices that are supported.
 *
 * (04/19/2000) gkh
 *	Added driver for ZyXEL omni.net lcd plus ISDN TA
 *	Made startup info message specify which drivers were compiled in.
 *
 * (04/03/2000) gkh
 *	Changed the probe process to remove the module unload races.
 *	Changed where the tty layer gets initialized to have devfs work nicer.
 *	Added initial devfs support.
 *
 * (03/26/2000) gkh
 *	Split driver up into device specific pieces.
 * 
 * (03/19/2000) gkh
 *	Fixed oops that could happen when device was removed while a program
 *	was talking to the device.
 *	Removed the static urbs and now all urbs are created and destroyed
 *	dynamically.
 *	Reworked the internal interface. Now everything is based on the 
 *	usb_serial_port structure instead of the larger usb_serial structure.
 *	This fixes the bug that a multiport device could not have more than
 *	one port open at one time.
 *
 * (03/17/2000) gkh
 *	Added config option for debugging messages.
 *	Added patch for keyspan pda from Brian Warner.
 *
 * (03/06/2000) gkh
 *	Added the keyspan pda code from Brian Warner <warner@lothar.com>
 *	Moved a bunch of the port specific stuff into its own structure. This
 *	is in anticipation of the true multiport devices (there's a bug if you
 *	try to access more than one port of any multiport device right now)
 *
 * (02/21/2000) gkh
 *	Made it so that any serial devices only have to specify which functions
 *	they want to overload from the generic function calls (great, 
 *	inheritance in C, in a driver, just what I wanted...)
 *	Added support for set_termios and ioctl function calls. No drivers take
 *	advantage of this yet.
 *	Removed the #ifdef MODULE, now there is no module specific code.
 *	Cleaned up a few comments in usb-serial.h that were wrong (thanks again
 *	to Miles Lott).
 *	Small fix to get_free_serial.
 *
 * (02/14/2000) gkh
 *	Removed the Belkin and Peracom functionality from the driver due to
 *	the lack of support from the vendor, and me not wanting people to 
 *	accidenatly buy the device, expecting it to work with Linux.
 *	Added read_bulk_callback and write_bulk_callback to the type structure
 *	for the needs of the FTDI and WhiteHEAT driver.
 *	Changed all reverences to FTDI to FTDI_SIO at the request of Bill
 *	Ryder.
 *	Changed the output urb size back to the max endpoint size to make
 *	the ftdi_sio driver have it easier, and due to the fact that it didn't
 *	really increase the speed any.
 *
 * (02/11/2000) gkh
 *	Added VISOR_FUNCTION_CONSOLE to the visor startup function. This was a
 *	patch from Miles Lott (milos@insync.net).
 *	Fixed bug with not restoring the minor range that a device grabs, if
 *	the startup function fails (thanks Miles for finding this).
 *
 * (02/05/2000) gkh
 *	Added initial framework for the Keyspan PDA serial converter so that
 *	Brian Warner has a place to put his code.
 *	Made the ezusb specific functions generic enough that different
 *	devices can use them (whiteheat and keyspan_pda both need them).
 *	Split out a whole bunch of structure and other stuff to a separate
 *	usb-serial.h file.
 *	Made the Visor connection messages a little more understandable, now
 *	that Miles Lott (milos@insync.net) has gotten the Generic channel to
 *	work. Also made them always show up in the log file.
 * 
 * (01/25/2000) gkh
 *	Added initial framework for FTDI serial converter so that Bill Ryder
 *	has a place to put his code.
 *	Added the vendor specific info from Handspring. Now we can print out
 *	informational debug messages as well as understand what is happening.
 *
 * (01/23/2000) gkh
 *	Fixed problem of crash when trying to open a port that didn't have a
 *	device assigned to it. Made the minor node finding a little smarter,
 *	now it looks to find a continuous space for the new device.
 *
 * (01/21/2000) gkh
 *	Fixed bug in visor_startup with patch from Miles Lott (milos@insync.net)
 *	Fixed get_serial_by_minor which was all messed up for multi port 
 *	devices. Fixed multi port problem for generic devices. Now the number
 *	of ports is determined by the number of bulk out endpoints for the
 *	generic device.
 *
 * (01/19/2000) gkh
 *	Removed lots of cruft that was around from the old (pre urb) driver 
 *	interface.
 *	Made the serial_table dynamic. This should save lots of memory when
 *	the number of minor nodes goes up to 256.
 *	Added initial support for devices that have more than one port. 
 *	Added more debugging comments for the Visor, and added a needed 
 *	set_configuration call.
 *
 * (01/17/2000) gkh
 *	Fixed the WhiteHEAT firmware (my processing tool had a bug)
 *	and added new debug loader firmware for it.
 *	Removed the put_char function as it isn't really needed.
 *	Added visor startup commands as found by the Win98 dump.
 * 
 * (01/13/2000) gkh
 *	Fixed the vendor id for the generic driver to the one I meant it to be.
 *
 * (01/12/2000) gkh
 *	Forget the version numbering...that's pretty useless...
 *	Made the driver able to be compiled so that the user can select which
 *	converter they want to use. This allows people who only want the Visor
 *	support to not pay the memory size price of the WhiteHEAT.
 *	Fixed bug where the generic driver (idVendor=0000 and idProduct=0000)
 *	grabbed the root hub. Not good.
 * 
 * version 0.4.0 (01/10/2000) gkh
 *	Added whiteheat.h containing the firmware for the ConnectTech WhiteHEAT
 *	device. Added startup function to allow firmware to be downloaded to
 *	a device if it needs to be.
 *	Added firmware download logic to the WhiteHEAT device.
 *	Started to add #defines to split up the different drivers for potential
 *	configuration option.
 *	
 * version 0.3.1 (12/30/99) gkh
 *      Fixed problems with urb for bulk out.
 *      Added initial support for multiple sets of endpoints. This enables
 *      the Handspring Visor to be attached successfully. Only the first
 *      bulk in / bulk out endpoint pair is being used right now.
 *
 * version 0.3.0 (12/27/99) gkh
 *	Added initial support for the Handspring Visor based on a patch from
 *	Miles Lott (milos@sneety.insync.net)
 *	Cleaned up the code a bunch and converted over to using urbs only.
 *
 * version 0.2.3 (12/21/99) gkh
 *	Added initial support for the Connect Tech WhiteHEAT converter.
 *	Incremented the number of ports in expectation of getting the
 *	WhiteHEAT to work properly (4 ports per connection).
 *	Added notification on insertion and removal of what port the
 *	device is/was connected to (and what kind of device it was).
 *
 * version 0.2.2 (12/16/99) gkh
 *	Changed major number to the new allocated number. We're legal now!
 *
 * version 0.2.1 (12/14/99) gkh
 *	Fixed bug that happens when device node is opened when there isn't a
 *	device attached to it. Thanks to marek@webdesign.no for noticing this.
 *
 * version 0.2.0 (11/10/99) gkh
 *	Split up internals to make it easier to add different types of serial 
 *	converters to the code.
 *	Added a "generic" driver that gets it's vendor and product id
 *	from when the module is loaded. Thanks to David E. Nelson (dnelson@jump.net)
 *	for the idea and sample code (from the usb scanner driver.)
 *	Cleared up any licensing questions by releasing it under the GNU GPL.
 *
 * version 0.1.2 (10/25/99) gkh
 * 	Fixed bug in detecting device.
 *
 * version 0.1.1 (10/05/99) gkh
 * 	Changed the major number to not conflict with anything else.
 *
 * version 0.1 (09/28/99) gkh
 * 	Can recognize the two different devices and start up a read from
 *	device when asked to. Writes also work. No control signals yet, this
 *	all is vendor specific data (i.e. no spec), also no control for
 *	different baud rates or other bit settings.
 *	Currently we are using the same devid as the acm driver. This needs
 *	to change.
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
#include <linux/list.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include "usb-serial.h"
#include "pl2303.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v2.0"
#define DRIVER_AUTHOR "Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux/"
#define DRIVER_DESC "USB Serial Driver core"

/* Driver structure we register with the USB core */
static struct usb_driver usb_serial_driver = {
	.owner =	THIS_MODULE,
	.name =		"usbserial",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
};

/* There is no MODULE_DEVICE_TABLE for usbserial.c.  Instead
   the MODULE_DEVICE_TABLE declarations in each serial driver
   cause the "hotplug" program to pull in whatever module is necessary
   via modprobe, and modprobe will load usbserial because the serial
   drivers depend on it.
*/

static int debug;
static struct usb_serial *serial_table[SERIAL_TTY_MINORS];	/* initially all NULL */
static LIST_HEAD(usb_serial_driver_list);

struct usb_serial *usb_serial_get_by_index(unsigned index)
{
	struct usb_serial *serial = serial_table[index];

	if (serial)
		kref_get(&serial->kref);
	return serial;
}

static struct usb_serial *get_free_serial (struct usb_serial *serial, int num_ports, unsigned int *minor)
{
	unsigned int i, j;
	int good_spot;

	dbg("%s %d", __FUNCTION__, num_ports);

	*minor = 0;
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		if (serial_table[i])
			continue;

		good_spot = 1;
		for (j = 1; j <= num_ports-1; ++j)
			if ((i+j >= SERIAL_TTY_MINORS) || (serial_table[i+j])) {
				good_spot = 0;
				i += j;
				break;
			}
		if (good_spot == 0)
			continue;

		*minor = i;
		dbg("%s - minor base = %d", __FUNCTION__, *minor);
		for (i = *minor; (i < (*minor + num_ports)) && (i < SERIAL_TTY_MINORS); ++i)
			serial_table[i] = serial;
		return serial;
	}
	return NULL;
}

static void return_serial(struct usb_serial *serial)
{
	int i;

	dbg("%s", __FUNCTION__);

	if (serial == NULL)
		return;

	for (i = 0; i < serial->num_ports; ++i) {
		serial_table[serial->minor + i] = NULL;
	}
}

static void destroy_serial(struct kref *kref)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	int i;

	serial = to_usb_serial(kref);

	dbg ("%s - %s", __FUNCTION__, serial->type->name);

	serial->type->shutdown(serial);

	/* return the minor range that this device had */
	return_serial(serial);

	for (i = 0; i < serial->num_ports; ++i)
		serial->port[i]->open_count = 0;

	/* the ports are cleaned up and released in port_release() */
	for (i = 0; i < serial->num_ports; ++i)
		if (serial->port[i]->dev.parent != NULL) {
			device_unregister(&serial->port[i]->dev);
			serial->port[i] = NULL;
		}

	/* If this is a "fake" port, we have to clean it up here, as it will
	 * not get cleaned up in port_release() as it was never registered with
	 * the driver core */
	if (serial->num_ports < serial->num_port_pointers) {
		for (i = serial->num_ports; i < serial->num_port_pointers; ++i) {
			port = serial->port[i];
			if (!port)
				continue;
			usb_kill_urb(port->read_urb);
			usb_free_urb(port->read_urb);
			usb_kill_urb(port->write_urb);
			usb_free_urb(port->write_urb);
			usb_kill_urb(port->interrupt_in_urb);
			usb_free_urb(port->interrupt_in_urb);
			usb_kill_urb(port->interrupt_out_urb);
			usb_free_urb(port->interrupt_out_urb);
			kfree(port->bulk_in_buffer);
			kfree(port->bulk_out_buffer);
			kfree(port->interrupt_in_buffer);
			kfree(port->interrupt_out_buffer);
		}
	}

	usb_put_dev(serial->dev);

	/* free up any memory that we allocated */
	kfree (serial);
}

/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open (struct tty_struct *tty, struct file * filp)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	unsigned int portNumber;
	int retval;
	
	dbg("%s", __FUNCTION__);

	/* get the serial object associated with this tty pointer */
	serial = usb_serial_get_by_index(tty->index);
	if (!serial) {
		tty->driver_data = NULL;
		return -ENODEV;
	}

	portNumber = tty->index - serial->minor;
	port = serial->port[portNumber];
	 
	++port->open_count;

	if (port->open_count == 1) {

		/* set up our port structure making the tty driver
		 * remember our port object, and us it */
		tty->driver_data = port;
		port->tty = tty;

		/* lock this module before we call it
		 * this may fail, which means we must bail out,
		 * safe because we are called with BKL held */
		if (!try_module_get(serial->type->owner)) {
			retval = -ENODEV;
			goto bailout_kref_put;
		}

		/* only call the device specific open if this 
		 * is the first time the port is opened */
		retval = serial->type->open(port, filp);
		if (retval)
			goto bailout_module_put;
	}

	return 0;

bailout_module_put:
	module_put(serial->type->owner);
bailout_kref_put:
	kref_put(&serial->kref, destroy_serial);
	port->open_count = 0;
	return retval;
}

static void serial_close(struct tty_struct *tty, struct file * filp)
{
	struct usb_serial_port *port = tty->driver_data;

	if (!port)
		return;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (port->open_count == 0)
		return;

	--port->open_count;
	if (port->open_count == 0) {
		/* only call the device specific close if this 
		 * port is being closed by the last owner */
		port->serial->type->close(port, filp);

		if (port->tty) {
			if (port->tty->driver_data)
				port->tty->driver_data = NULL;
			port->tty = NULL;
		}

		module_put(port->serial->type->owner);
	}

	kref_put(&port->serial->kref, destroy_serial);
}

static int serial_write (struct tty_struct * tty, const unsigned char *buf, int count)
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -EINVAL;

	dbg("%s - port %d, %d byte(s)", __FUNCTION__, port->number, count);

	if (!port->open_count) {
		dbg("%s - port not opened", __FUNCTION__);
		goto exit;
	}

	/* pass on to the driver specific version of this function */
	retval = port->serial->type->write(port, buf, count);

exit:
	return retval;
}

static int serial_write_room (struct tty_struct *tty) 
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -EINVAL;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		goto exit;
	}

	/* pass on to the driver specific version of this function */
	retval = port->serial->type->write_room(port);

exit:
	return retval;
}

static int serial_chars_in_buffer (struct tty_struct *tty) 
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -EINVAL;

	dbg("%s = port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		goto exit;
	}

	/* pass on to the driver specific version of this function */
	retval = port->serial->type->chars_in_buffer(port);

exit:
	return retval;
}

static void serial_throttle (struct tty_struct * tty)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg ("%s - port not open", __FUNCTION__);
		return;
	}

	/* pass on to the driver specific version of this function */
	if (port->serial->type->throttle)
		port->serial->type->throttle(port);
}

static void serial_unthrottle (struct tty_struct * tty)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		return;
	}

	/* pass on to the driver specific version of this function */
	if (port->serial->type->unthrottle)
		port->serial->type->unthrottle(port);
}

static int serial_ioctl (struct tty_struct *tty, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -ENODEV;

	dbg("%s - port %d, cmd 0x%.4x", __FUNCTION__, port->number, cmd);

	if (!port->open_count) {
		dbg ("%s - port not open", __FUNCTION__);
		goto exit;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (port->serial->type->ioctl)
		retval = port->serial->type->ioctl(port, file, cmd, arg);
	else
		retval = -ENOIOCTLCMD;

exit:
	return retval;
}

static void serial_set_termios (struct tty_struct *tty, struct termios * old)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		return;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (port->serial->type->set_termios)
		port->serial->type->set_termios(port, old);
}

static void serial_break (struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		return;
	}

	/* pass on to the driver specific version of this function if it is available */
	if (port->serial->type->break_ctl)
		port->serial->type->break_ctl(port, break_state);
}

static int serial_read_proc (char *page, char **start, off_t off, int count, int *eof, void *data)
{
	struct usb_serial *serial;
	int length = 0;
	int i;
	off_t begin = 0;
	char tmp[40];

	dbg("%s", __FUNCTION__);
	length += sprintf (page, "usbserinfo:1.0 driver:%s\n", DRIVER_VERSION);
	for (i = 0; i < SERIAL_TTY_MINORS && length < PAGE_SIZE; ++i) {
		serial = usb_serial_get_by_index(i);
		if (serial == NULL)
			continue;

		length += sprintf (page+length, "%d:", i);
		if (serial->type->owner)
			length += sprintf (page+length, " module:%s", module_name(serial->type->owner));
		length += sprintf (page+length, " name:\"%s\"", serial->type->name);
		length += sprintf (page+length, " vendor:%04x product:%04x", 
				   le16_to_cpu(serial->dev->descriptor.idVendor), 
				   le16_to_cpu(serial->dev->descriptor.idProduct));
		length += sprintf (page+length, " num_ports:%d", serial->num_ports);
		length += sprintf (page+length, " port:%d", i - serial->minor + 1);

		usb_make_path(serial->dev, tmp, sizeof(tmp));
		length += sprintf (page+length, " path:%s", tmp);
			
		length += sprintf (page+length, "\n");
		if ((length + begin) > (off + count))
			goto done;
		if ((length + begin) < off) {
			begin += length;
			length = 0;
		}
		kref_put(&serial->kref, destroy_serial);
	}
	*eof = 1;
done:
	if (off >= (length + begin))
		return 0;
	*start = page + (off-begin);
	return ((count < begin+length-off) ? count : begin+length-off);
}

static int serial_tiocmget (struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		goto exit;
	}

	if (port->serial->type->tiocmget)
		return port->serial->type->tiocmget(port, file);

exit:
	return -EINVAL;
}

static int serial_tiocmset (struct tty_struct *tty, struct file *file,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (!port->open_count) {
		dbg("%s - port not open", __FUNCTION__);
		goto exit;
	}

	if (port->serial->type->tiocmset)
		return port->serial->type->tiocmset(port, file, set, clear);

exit:
	return -EINVAL;
}

void usb_serial_port_softint(void *private)
{
	struct usb_serial_port *port = private;
	struct tty_struct *tty;

	dbg("%s - port %d", __FUNCTION__, port->number);
	
	if (!port)
		return;

	tty = port->tty;
	if (!tty)
		return;

	tty_wakeup(tty);
}

static void port_release(struct device *dev)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);

	dbg ("%s - %s", __FUNCTION__, dev->bus_id);
	usb_kill_urb(port->read_urb);
	usb_free_urb(port->read_urb);
	usb_kill_urb(port->write_urb);
	usb_free_urb(port->write_urb);
	usb_kill_urb(port->interrupt_in_urb);
	usb_free_urb(port->interrupt_in_urb);
	usb_kill_urb(port->interrupt_out_urb);
	usb_free_urb(port->interrupt_out_urb);
	kfree(port->bulk_in_buffer);
	kfree(port->bulk_out_buffer);
	kfree(port->interrupt_in_buffer);
	kfree(port->interrupt_out_buffer);
	kfree(port);
}

static struct usb_serial * create_serial (struct usb_device *dev, 
					  struct usb_interface *interface,
					  struct usb_serial_device_type *type)
{
	struct usb_serial *serial;

	serial = kmalloc (sizeof (*serial), GFP_KERNEL);
	if (!serial) {
		dev_err(&dev->dev, "%s - out of memory\n", __FUNCTION__);
		return NULL;
	}
	memset (serial, 0, sizeof(*serial));
	serial->dev = usb_get_dev(dev);
	serial->type = type;
	serial->interface = interface;
	kref_init(&serial->kref);

	return serial;
}

static struct usb_serial_device_type *search_serial_device(struct usb_interface *iface)
{
	struct list_head *p;
	const struct usb_device_id *id;
	struct usb_serial_device_type *t;

	/* List trough know devices and see if the usb id matches */
	list_for_each(p, &usb_serial_driver_list) {
		t = list_entry(p, struct usb_serial_device_type, driver_list);
		id = usb_match_id(iface, t->id_table);
		if (id != NULL) {
			dbg("descriptor matches");
			return t;
		}
	}

	return NULL;
}

int usb_serial_probe(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev (interface);
	struct usb_serial *serial = NULL;
	struct usb_serial_port *port;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *interrupt_out_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_out_endpoint[MAX_NUM_PORTS];
	struct usb_serial_device_type *type = NULL;
	int retval;
	int minor;
	int buffer_size;
	int i;
	int num_interrupt_in = 0;
	int num_interrupt_out = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	int num_ports = 0;
	int max_endpoints;

	type = search_serial_device(interface);
	if (!type) {
		dbg("none matched");
		return -ENODEV;
	}

	serial = create_serial (dev, interface, type);
	if (!serial) {
		dev_err(&interface->dev, "%s - out of memory\n", __FUNCTION__);
		return -ENOMEM;
	}

	/* if this device type has a probe function, call it */
	if (type->probe) {
		const struct usb_device_id *id;

		if (!try_module_get(type->owner)) {
			dev_err(&interface->dev, "module get failed, exiting\n");
			kfree (serial);
			return -EIO;
		}

		id = usb_match_id(interface, type->id_table);
		retval = type->probe(serial, id);
		module_put(type->owner);

		if (retval) {
			dbg ("sub driver rejected device");
			kfree (serial);
			return retval;
		}
	}

	/* descriptor matches, let's find the endpoints needed */
	/* check out the endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		
		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			dbg("found bulk in on endpoint %d", i);
			bulk_in_endpoint[num_bulk_in] = endpoint;
			++num_bulk_in;
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			dbg("found bulk out on endpoint %d", i);
			bulk_out_endpoint[num_bulk_out] = endpoint;
			++num_bulk_out;
		}
		
		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x03)) {
			/* we found a interrupt in endpoint */
			dbg("found interrupt in on endpoint %d", i);
			interrupt_in_endpoint[num_interrupt_in] = endpoint;
			++num_interrupt_in;
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x03)) {
			/* we found an interrupt out endpoint */
			dbg("found interrupt out on endpoint %d", i);
			interrupt_out_endpoint[num_interrupt_out] = endpoint;
			++num_interrupt_out;
		}
	}

#if defined(CONFIG_USB_SERIAL_PL2303) || defined(CONFIG_USB_SERIAL_PL2303_MODULE)
	/* BEGIN HORRIBLE HACK FOR PL2303 */ 
	/* this is needed due to the looney way its endpoints are set up */
	if (((le16_to_cpu(dev->descriptor.idVendor) == PL2303_VENDOR_ID) &&
	     (le16_to_cpu(dev->descriptor.idProduct) == PL2303_PRODUCT_ID)) ||
	    ((le16_to_cpu(dev->descriptor.idVendor) == ATEN_VENDOR_ID) &&
	     (le16_to_cpu(dev->descriptor.idProduct) == ATEN_PRODUCT_ID))) {
		if (interface != dev->actconfig->interface[0]) {
			/* check out the endpoints of the other interface*/
			iface_desc = dev->actconfig->interface[0]->cur_altsetting;
			for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
				endpoint = &iface_desc->endpoint[i].desc;
				if ((endpoint->bEndpointAddress & 0x80) &&
				    ((endpoint->bmAttributes & 3) == 0x03)) {
					/* we found a interrupt in endpoint */
					dbg("found interrupt in for Prolific device on separate interface");
					interrupt_in_endpoint[num_interrupt_in] = endpoint;
					++num_interrupt_in;
				}
			}
		}

		/* Now make sure the PL-2303 is configured correctly.
		 * If not, give up now and hope this hack will work
		 * properly during a later invocation of usb_serial_probe
		 */
		if (num_bulk_in == 0 || num_bulk_out == 0) {
			dev_info(&interface->dev, "PL-2303 hack: descriptors matched but endpoints did not\n");
			kfree (serial);
			return -ENODEV;
		}
	}
	/* END HORRIBLE HACK FOR PL2303 */
#endif

	/* found all that we need */
	dev_info(&interface->dev, "%s converter detected\n", type->name);

#ifdef CONFIG_USB_SERIAL_GENERIC
	if (type == &usb_serial_generic_device) {
		num_ports = num_bulk_out;
		if (num_ports == 0) {
			dev_err(&interface->dev, "Generic device with no bulk out, not allowed.\n");
			kfree (serial);
			return -EIO;
		}
	}
#endif
	if (!num_ports) {
		/* if this device type has a calc_num_ports function, call it */
		if (type->calc_num_ports) {
			if (!try_module_get(type->owner)) {
				dev_err(&interface->dev, "module get failed, exiting\n");
				kfree (serial);
				return -EIO;
			}
			num_ports = type->calc_num_ports (serial);
			module_put(type->owner);
		}
		if (!num_ports)
			num_ports = type->num_ports;
	}

	if (get_free_serial (serial, num_ports, &minor) == NULL) {
		dev_err(&interface->dev, "No more free serial devices\n");
		kfree (serial);
		return -ENOMEM;
	}

	serial->minor = minor;
	serial->num_ports = num_ports;
	serial->num_bulk_in = num_bulk_in;
	serial->num_bulk_out = num_bulk_out;
	serial->num_interrupt_in = num_interrupt_in;
	serial->num_interrupt_out = num_interrupt_out;

	/* create our ports, we need as many as the max endpoints */
	/* we don't use num_ports here cauz some devices have more endpoint pairs than ports */
	max_endpoints = max(num_bulk_in, num_bulk_out);
	max_endpoints = max(max_endpoints, num_interrupt_in);
	max_endpoints = max(max_endpoints, num_interrupt_out);
	max_endpoints = max(max_endpoints, (int)serial->num_ports);
	serial->num_port_pointers = max_endpoints;
	dbg("%s - setting up %d port structures for this device", __FUNCTION__, max_endpoints);
	for (i = 0; i < max_endpoints; ++i) {
		port = kmalloc(sizeof(struct usb_serial_port), GFP_KERNEL);
		if (!port)
			goto probe_error;
		memset(port, 0x00, sizeof(struct usb_serial_port));
		port->number = i + serial->minor;
		port->serial = serial;
		spin_lock_init(&port->lock);
		INIT_WORK(&port->work, usb_serial_port_softint, port);
		serial->port[i] = port;
	}

	/* set up the endpoint information */
	for (i = 0; i < num_bulk_in; ++i) {
		endpoint = bulk_in_endpoint[i];
		port = serial->port[i];
		port->read_urb = usb_alloc_urb (0, GFP_KERNEL);
		if (!port->read_urb) {
			dev_err(&interface->dev, "No free urbs available\n");
			goto probe_error;
		}
		buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
		port->bulk_in_size = buffer_size;
		port->bulk_in_endpointAddress = endpoint->bEndpointAddress;
		port->bulk_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
		if (!port->bulk_in_buffer) {
			dev_err(&interface->dev, "Couldn't allocate bulk_in_buffer\n");
			goto probe_error;
		}
		usb_fill_bulk_urb (port->read_urb, dev,
				   usb_rcvbulkpipe (dev,
					   	    endpoint->bEndpointAddress),
				   port->bulk_in_buffer, buffer_size,
				   serial->type->read_bulk_callback,
				   port);
	}

	for (i = 0; i < num_bulk_out; ++i) {
		endpoint = bulk_out_endpoint[i];
		port = serial->port[i];
		port->write_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!port->write_urb) {
			dev_err(&interface->dev, "No free urbs available\n");
			goto probe_error;
		}
		buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
		port->bulk_out_size = buffer_size;
		port->bulk_out_endpointAddress = endpoint->bEndpointAddress;
		port->bulk_out_buffer = kmalloc (buffer_size, GFP_KERNEL);
		if (!port->bulk_out_buffer) {
			dev_err(&interface->dev, "Couldn't allocate bulk_out_buffer\n");
			goto probe_error;
		}
		usb_fill_bulk_urb (port->write_urb, dev,
				   usb_sndbulkpipe (dev,
						    endpoint->bEndpointAddress),
				   port->bulk_out_buffer, buffer_size, 
				   serial->type->write_bulk_callback,
				   port);
	}

	if (serial->type->read_int_callback) {
		for (i = 0; i < num_interrupt_in; ++i) {
			endpoint = interrupt_in_endpoint[i];
			port = serial->port[i];
			port->interrupt_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->interrupt_in_urb) {
				dev_err(&interface->dev, "No free urbs available\n");
				goto probe_error;
			}
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			port->interrupt_in_endpointAddress = endpoint->bEndpointAddress;
			port->interrupt_in_buffer = kmalloc (buffer_size, GFP_KERNEL);
			if (!port->interrupt_in_buffer) {
				dev_err(&interface->dev, "Couldn't allocate interrupt_in_buffer\n");
				goto probe_error;
			}
			usb_fill_int_urb (port->interrupt_in_urb, dev, 
					  usb_rcvintpipe (dev,
							  endpoint->bEndpointAddress),
					  port->interrupt_in_buffer, buffer_size, 
					  serial->type->read_int_callback, port, 
					  endpoint->bInterval);
		}
	} else if (num_interrupt_in) {
		dbg("the device claims to support interrupt in transfers, but read_int_callback is not defined");
	}
	
	if (serial->type->write_int_callback) {
		for (i = 0; i < num_interrupt_out; ++i) {
			endpoint = interrupt_out_endpoint[i];
			port = serial->port[i];
			port->interrupt_out_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->interrupt_out_urb) {
				dev_err(&interface->dev, "No free urbs available\n");
				goto probe_error;
			}
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			port->interrupt_out_size = buffer_size;
			port->interrupt_out_endpointAddress = endpoint->bEndpointAddress;
			port->interrupt_out_buffer = kmalloc (buffer_size, GFP_KERNEL);
			if (!port->interrupt_out_buffer) {
				dev_err(&interface->dev, "Couldn't allocate interrupt_out_buffer\n");
				goto probe_error;
			}
			usb_fill_int_urb (port->interrupt_out_urb, dev,
					  usb_sndintpipe (dev,
							  endpoint->bEndpointAddress),
					  port->interrupt_out_buffer, buffer_size,
					  serial->type->write_int_callback, port,
					  endpoint->bInterval);
		}
	} else if (num_interrupt_out) {
		dbg("the device claims to support interrupt out transfers, but write_int_callback is not defined");
	}
	
	/* if this device type has an attach function, call it */
	if (type->attach) {
		if (!try_module_get(type->owner)) {
			dev_err(&interface->dev, "module get failed, exiting\n");
			goto probe_error;
		}
		retval = type->attach (serial);
		module_put(type->owner);
		if (retval < 0)
			goto probe_error;
		if (retval > 0) {
			/* quietly accept this device, but don't bind to a serial port
			 * as it's about to disappear */
			goto exit;
		}
	}

	/* register all of the individual ports with the driver core */
	for (i = 0; i < num_ports; ++i) {
		port = serial->port[i];
		port->dev.parent = &interface->dev;
		port->dev.driver = NULL;
		port->dev.bus = &usb_serial_bus_type;
		port->dev.release = &port_release;

		snprintf (&port->dev.bus_id[0], sizeof(port->dev.bus_id), "ttyUSB%d", port->number);
		dbg ("%s - registering %s", __FUNCTION__, port->dev.bus_id);
		device_register (&port->dev);
	}

	usb_serial_console_init (debug, minor);

exit:
	/* success */
	usb_set_intfdata (interface, serial);
	return 0;

probe_error:
	for (i = 0; i < num_bulk_in; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		if (port->read_urb)
			usb_free_urb (port->read_urb);
		kfree(port->bulk_in_buffer);
	}
	for (i = 0; i < num_bulk_out; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		if (port->write_urb)
			usb_free_urb (port->write_urb);
		kfree(port->bulk_out_buffer);
	}
	for (i = 0; i < num_interrupt_in; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		if (port->interrupt_in_urb)
			usb_free_urb (port->interrupt_in_urb);
		kfree(port->interrupt_in_buffer);
	}
	for (i = 0; i < num_interrupt_out; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		if (port->interrupt_out_urb)
			usb_free_urb (port->interrupt_out_urb);
		kfree(port->interrupt_out_buffer);
	}

	/* return the minor range that this device had */
	return_serial (serial);

	/* free up any memory that we allocated */
	for (i = 0; i < serial->num_port_pointers; ++i)
		kfree(serial->port[i]);
	kfree (serial);
	return -EIO;
}

void usb_serial_disconnect(struct usb_interface *interface)
{
	int i;
	struct usb_serial *serial = usb_get_intfdata (interface);
	struct device *dev = &interface->dev;
	struct usb_serial_port *port;

	dbg ("%s", __FUNCTION__);

	usb_set_intfdata (interface, NULL);
	if (serial) {
		for (i = 0; i < serial->num_ports; ++i) {
			port = serial->port[i];
			if (port && port->tty)
				tty_hangup(port->tty);
		}
		/* let the last holder of this object 
		 * cause it to be cleaned up */
		kref_put(&serial->kref, destroy_serial);
	}
	dev_info(dev, "device disconnected\n");
}

static struct tty_operations serial_ops = {
	.open =			serial_open,
	.close =		serial_close,
	.write =		serial_write,
	.write_room =		serial_write_room,
	.ioctl =		serial_ioctl,
	.set_termios =		serial_set_termios,
	.throttle =		serial_throttle,
	.unthrottle =		serial_unthrottle,
	.break_ctl =		serial_break,
	.chars_in_buffer =	serial_chars_in_buffer,
	.read_proc =		serial_read_proc,
	.tiocmget =		serial_tiocmget,
	.tiocmset =		serial_tiocmset,
};

struct tty_driver *usb_serial_tty_driver;

static int __init usb_serial_init(void)
{
	int i;
	int result;

	usb_serial_tty_driver = alloc_tty_driver(SERIAL_TTY_MINORS);
	if (!usb_serial_tty_driver)
		return -ENOMEM;

	/* Initialize our global data */
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		serial_table[i] = NULL;
	}

	result = bus_register(&usb_serial_bus_type);
	if (result) {
		err("%s - registering bus driver failed", __FUNCTION__);
		goto exit_bus;
	}

	usb_serial_tty_driver->owner = THIS_MODULE;
	usb_serial_tty_driver->driver_name = "usbserial";
	usb_serial_tty_driver->devfs_name = "usb/tts/";
	usb_serial_tty_driver->name = 	"ttyUSB";
	usb_serial_tty_driver->major = SERIAL_TTY_MAJOR;
	usb_serial_tty_driver->minor_start = 0;
	usb_serial_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	usb_serial_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	usb_serial_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	usb_serial_tty_driver->init_termios = tty_std_termios;
	usb_serial_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(usb_serial_tty_driver, &serial_ops);
	result = tty_register_driver(usb_serial_tty_driver);
	if (result) {
		err("%s - tty_register_driver failed", __FUNCTION__);
		goto exit_reg_driver;
	}

	/* register the USB driver */
	result = usb_register(&usb_serial_driver);
	if (result < 0) {
		err("%s - usb_register failed", __FUNCTION__);
		goto exit_tty;
	}

	/* register the generic driver, if we should */
	result = usb_serial_generic_register(debug);
	if (result < 0) {
		err("%s - registering generic driver failed", __FUNCTION__);
		goto exit_generic;
	}

	info(DRIVER_DESC " " DRIVER_VERSION);

	return result;

exit_generic:
	usb_deregister(&usb_serial_driver);

exit_tty:
	tty_unregister_driver(usb_serial_tty_driver);

exit_reg_driver:
	bus_unregister(&usb_serial_bus_type);

exit_bus:
	err ("%s - returning with error %d", __FUNCTION__, result);
	put_tty_driver(usb_serial_tty_driver);
	return result;
}


static void __exit usb_serial_exit(void)
{
	usb_serial_console_exit();

	usb_serial_generic_deregister();

	usb_deregister(&usb_serial_driver);
	tty_unregister_driver(usb_serial_tty_driver);
	put_tty_driver(usb_serial_tty_driver);
	bus_unregister(&usb_serial_bus_type);
}


module_init(usb_serial_init);
module_exit(usb_serial_exit);

#define set_to_generic_if_null(type, function)				\
	do {								\
		if (!type->function) {					\
			type->function = usb_serial_generic_##function;	\
			dbg("Had to override the " #function		\
				 " usb serial operation with the generic one.");\
			}						\
	} while (0)

static void fixup_generic(struct usb_serial_device_type *device)
{
	set_to_generic_if_null(device, open);
	set_to_generic_if_null(device, write);
	set_to_generic_if_null(device, close);
	set_to_generic_if_null(device, write_room);
	set_to_generic_if_null(device, chars_in_buffer);
	set_to_generic_if_null(device, read_bulk_callback);
	set_to_generic_if_null(device, write_bulk_callback);
	set_to_generic_if_null(device, shutdown);
}

int usb_serial_register(struct usb_serial_device_type *new_device)
{
	int retval;

	fixup_generic(new_device);

	/* Add this device to our list of devices */
	list_add(&new_device->driver_list, &usb_serial_driver_list);

	retval = usb_serial_bus_register(new_device);
	if (retval) {
		err("problem %d when registering driver %s", retval, new_device->name);
		list_del(&new_device->driver_list);
	}
	else
		info("USB Serial support registered for %s", new_device->name);

	return retval;
}


void usb_serial_deregister(struct usb_serial_device_type *device)
{
	info("USB Serial deregistering driver %s", device->name);
	list_del(&device->driver_list);
	usb_serial_bus_deregister(device);
}



/* If the usb-serial core is built into the core, the usb-serial drivers
   need these symbols to load properly as modules. */
EXPORT_SYMBOL_GPL(usb_serial_register);
EXPORT_SYMBOL_GPL(usb_serial_deregister);
EXPORT_SYMBOL_GPL(usb_serial_probe);
EXPORT_SYMBOL_GPL(usb_serial_disconnect);
EXPORT_SYMBOL_GPL(usb_serial_port_softint);


/* Module information */
MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_VERSION( DRIVER_VERSION );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
