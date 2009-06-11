/*
 * USB Serial Converter driver
 *
 * Copyright (C) 1999 - 2005 Greg Kroah-Hartman (greg@kroah.com)
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
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include "pl2303.h"

/*
 * Version Information
 */
#define DRIVER_AUTHOR "Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux/"
#define DRIVER_DESC "USB Serial Driver core"

static void port_free(struct usb_serial_port *port);

/* Driver structure we register with the USB core */
static struct usb_driver usb_serial_driver = {
	.name =		"usbserial",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.suspend =	usb_serial_suspend,
	.resume =	usb_serial_resume,
	.no_dynamic_id = 	1,
};

/* There is no MODULE_DEVICE_TABLE for usbserial.c.  Instead
   the MODULE_DEVICE_TABLE declarations in each serial driver
   cause the "hotplug" program to pull in whatever module is necessary
   via modprobe, and modprobe will load usbserial because the serial
   drivers depend on it.
*/

static int debug;
/* initially all NULL */
static struct usb_serial *serial_table[SERIAL_TTY_MINORS];
static DEFINE_MUTEX(table_lock);
static LIST_HEAD(usb_serial_driver_list);

struct usb_serial *usb_serial_get_by_index(unsigned index)
{
	struct usb_serial *serial;

	mutex_lock(&table_lock);
	serial = serial_table[index];

	if (serial)
		kref_get(&serial->kref);
	mutex_unlock(&table_lock);
	return serial;
}

static struct usb_serial *get_free_serial(struct usb_serial *serial,
					int num_ports, unsigned int *minor)
{
	unsigned int i, j;
	int good_spot;

	dbg("%s %d", __func__, num_ports);

	*minor = 0;
	mutex_lock(&table_lock);
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
		j = 0;
		dbg("%s - minor base = %d", __func__, *minor);
		for (i = *minor; (i < (*minor + num_ports)) && (i < SERIAL_TTY_MINORS); ++i) {
			serial_table[i] = serial;
			serial->port[j++]->number = i;
		}
		mutex_unlock(&table_lock);
		return serial;
	}
	mutex_unlock(&table_lock);
	return NULL;
}

static void return_serial(struct usb_serial *serial)
{
	int i;

	dbg("%s", __func__);

	for (i = 0; i < serial->num_ports; ++i)
		serial_table[serial->minor + i] = NULL;
}

static void destroy_serial(struct kref *kref)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	int i;

	serial = to_usb_serial(kref);

	dbg("%s - %s", __func__, serial->type->description);

	/* return the minor range that this device had */
	if (serial->minor != SERIAL_TTY_NO_MINOR)
		return_serial(serial);

	/* If this is a "fake" port, we have to clean it up here, as it will
	 * not get cleaned up in port_release() as it was never registered with
	 * the driver core */
	if (serial->num_ports < serial->num_port_pointers) {
		for (i = serial->num_ports;
					i < serial->num_port_pointers; ++i) {
			port = serial->port[i];
			if (!port)
				continue;
			port_free(port);
		}
	}

	usb_put_dev(serial->dev);

	/* free up any memory that we allocated */
	kfree(serial);
}

void usb_serial_put(struct usb_serial *serial)
{
	mutex_lock(&table_lock);
	kref_put(&serial->kref, destroy_serial);
	mutex_unlock(&table_lock);
}

/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open (struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	unsigned int portNumber;
	int retval = 0;

	dbg("%s", __func__);

	/* get the serial object associated with this tty pointer */
	serial = usb_serial_get_by_index(tty->index);
	if (!serial) {
		tty->driver_data = NULL;
		return -ENODEV;
	}

	mutex_lock(&serial->disc_mutex);
	portNumber = tty->index - serial->minor;
	port = serial->port[portNumber];
	if (!port || serial->disconnected)
		retval = -ENODEV;
	else
		get_device(&port->dev);
	/*
	 * Note: Our locking order requirement does not allow port->mutex
	 * to be acquired while serial->disc_mutex is held.
	 */
	mutex_unlock(&serial->disc_mutex);
	if (retval)
		goto bailout_serial_put;

	if (mutex_lock_interruptible(&port->mutex)) {
		retval = -ERESTARTSYS;
		goto bailout_port_put;
	}

	++port->port.count;

	/* set up our port structure making the tty driver
	 * remember our port object, and us it */
	tty->driver_data = port;
	tty_port_tty_set(&port->port, tty);

	if (port->port.count == 1) {

		/* lock this module before we call it
		 * this may fail, which means we must bail out,
		 * safe because we are called with BKL held */
		if (!try_module_get(serial->type->driver.owner)) {
			retval = -ENODEV;
			goto bailout_mutex_unlock;
		}

		mutex_lock(&serial->disc_mutex);
		if (serial->disconnected)
			retval = -ENODEV;
		else
			retval = usb_autopm_get_interface(serial->interface);
		if (retval)
			goto bailout_module_put;

		/* only call the device specific open if this
		 * is the first time the port is opened */
		retval = serial->type->open(tty, port, filp);
		if (retval)
			goto bailout_interface_put;
		mutex_unlock(&serial->disc_mutex);
	}
	mutex_unlock(&port->mutex);
	/* Now do the correct tty layer semantics */
	retval = tty_port_block_til_ready(&port->port, tty, filp);
	if (retval == 0)
		return 0;

bailout_interface_put:
	usb_autopm_put_interface(serial->interface);
bailout_module_put:
	mutex_unlock(&serial->disc_mutex);
	module_put(serial->type->driver.owner);
bailout_mutex_unlock:
	port->port.count = 0;
	tty->driver_data = NULL;
	tty_port_tty_set(&port->port, NULL);
	mutex_unlock(&port->mutex);
bailout_port_put:
	put_device(&port->dev);
bailout_serial_put:
	usb_serial_put(serial);
	return retval;
}

/**
 *	serial_do_down		-	shut down hardware
 *	@port: port to shut down
 *
 *	Shut down a USB port unless it is the console. We never shut down the
 *	console hardware as it will always be in use.
 *
 *	Don't free any resources at this point
 */
static void serial_do_down(struct usb_serial_port *port)
{
	struct usb_serial_driver *drv = port->serial->type;
	struct usb_serial *serial;
	struct module *owner;

	/* The console is magical, do not hang up the console hardware
	   or there will be tears */
	if (port->console)
		return;

	mutex_lock(&port->mutex);
	serial = port->serial;
	owner = serial->type->driver.owner;

	if (drv->close)
		drv->close(port);

	mutex_unlock(&port->mutex);
}

/**
 *	serial_do_free		-	free resources post close/hangup
 *	@port: port to free up
 *
 *	Do the resource freeing and refcount dropping for the port. We must
 *	be careful about ordering and we must avoid freeing up the console.
 */

static void serial_do_free(struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct module *owner;

	/* The console is magical, do not hang up the console hardware
	   or there will be tears */
	if (port->console)
		return;

	serial = port->serial;
	owner = serial->type->driver.owner;
	put_device(&port->dev);
	/* Mustn't dereference port any more */
	mutex_lock(&serial->disc_mutex);
	if (!serial->disconnected)
		usb_autopm_put_interface(serial->interface);
	mutex_unlock(&serial->disc_mutex);
	usb_serial_put(serial);
	/* Mustn't dereference serial any more */
	module_put(owner);
}

static void serial_close(struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);


	if (tty_port_close_start(&port->port, tty, filp) == 0)
		return;

	serial_do_down(port);		
	tty_port_close_end(&port->port, tty);
	tty_port_tty_set(&port->port, NULL);
	serial_do_free(port);
}

static void serial_hangup(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	serial_do_down(port);
	tty_port_hangup(&port->port);
	serial_do_free(port);
}

static int serial_write(struct tty_struct *tty, const unsigned char *buf,
								int count)
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -ENODEV;

	if (port->serial->dev->state == USB_STATE_NOTATTACHED)
		goto exit;

	dbg("%s - port %d, %d byte(s)", __func__, port->number, count);

	/* count is managed under the mutex lock for the tty so cannot
	   drop to zero until after the last close completes */
	WARN_ON(!port->port.count);

	/* pass on to the driver specific version of this function */
	retval = port->serial->type->write(tty, port, buf, count);

exit:
	return retval;
}

static int serial_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);
	WARN_ON(!port->port.count);
	/* pass on to the driver specific version of this function */
	return port->serial->type->write_room(tty);
}

static int serial_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s = port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	/* if the device was unplugged then any remaining characters
	   fell out of the connector ;) */
	if (port->serial->disconnected)
		return 0;
	/* pass on to the driver specific version of this function */
	return port->serial->type->chars_in_buffer(tty);
}

static void serial_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	/* pass on to the driver specific version of this function */
	if (port->serial->type->throttle)
		port->serial->type->throttle(tty);
}

static void serial_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	/* pass on to the driver specific version of this function */
	if (port->serial->type->unthrottle)
		port->serial->type->unthrottle(tty);
}

static int serial_ioctl(struct tty_struct *tty, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -ENODEV;

	dbg("%s - port %d, cmd 0x%.4x", __func__, port->number, cmd);

	WARN_ON(!port->port.count);

	/* pass on to the driver specific version of this function
	   if it is available */
	if (port->serial->type->ioctl) {
		retval = port->serial->type->ioctl(tty, file, cmd, arg);
	} else
		retval = -ENOIOCTLCMD;
	return retval;
}

static void serial_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	/* pass on to the driver specific version of this function
	   if it is available */
	if (port->serial->type->set_termios)
		port->serial->type->set_termios(tty, port, old);
	else
		tty_termios_copy_hw(tty->termios, old);
}

static int serial_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	/* pass on to the driver specific version of this function
	   if it is available */
	if (port->serial->type->break_ctl)
		port->serial->type->break_ctl(tty, break_state);
	return 0;
}

static int serial_proc_show(struct seq_file *m, void *v)
{
	struct usb_serial *serial;
	int i;
	char tmp[40];

	dbg("%s", __func__);
	seq_puts(m, "usbserinfo:1.0 driver:2.0\n");
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		serial = usb_serial_get_by_index(i);
		if (serial == NULL)
			continue;

		seq_printf(m, "%d:", i);
		if (serial->type->driver.owner)
			seq_printf(m, " module:%s",
				module_name(serial->type->driver.owner));
		seq_printf(m, " name:\"%s\"",
				serial->type->description);
		seq_printf(m, " vendor:%04x product:%04x",
			le16_to_cpu(serial->dev->descriptor.idVendor),
			le16_to_cpu(serial->dev->descriptor.idProduct));
		seq_printf(m, " num_ports:%d", serial->num_ports);
		seq_printf(m, " port:%d", i - serial->minor + 1);
		usb_make_path(serial->dev, tmp, sizeof(tmp));
		seq_printf(m, " path:%s", tmp);

		seq_putc(m, '\n');
		usb_serial_put(serial);
	}
	return 0;
}

static int serial_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, serial_proc_show, NULL);
}

static const struct file_operations serial_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= serial_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int serial_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	if (port->serial->type->tiocmget)
		return port->serial->type->tiocmget(tty, file);
	return -EINVAL;
}

static int serial_tiocmset(struct tty_struct *tty, struct file *file,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);

	WARN_ON(!port->port.count);
	if (port->serial->type->tiocmset)
		return port->serial->type->tiocmset(tty, file, set, clear);
	return -EINVAL;
}

/*
 * We would be calling tty_wakeup here, but unfortunately some line
 * disciplines have an annoying habit of calling tty->write from
 * the write wakeup callback (e.g. n_hdlc.c).
 */
void usb_serial_port_softint(struct usb_serial_port *port)
{
	schedule_work(&port->work);
}
EXPORT_SYMBOL_GPL(usb_serial_port_softint);

static void usb_serial_port_work(struct work_struct *work)
{
	struct usb_serial_port *port =
		container_of(work, struct usb_serial_port, work);
	struct tty_struct *tty;

	dbg("%s - port %d", __func__, port->number);

	tty = tty_port_tty_get(&port->port);
	if (!tty)
		return;

	tty_wakeup(tty);
	tty_kref_put(tty);
}

static void port_release(struct device *dev)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);

	dbg ("%s - %s", __func__, dev_name(dev));
	port_free(port);
}

static void kill_traffic(struct usb_serial_port *port)
{
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->write_urb);
	/*
	 * This is tricky.
	 * Some drivers submit the read_urb in the
	 * handler for the write_urb or vice versa
	 * this order determines the order in which
	 * usb_kill_urb() must be used to reliably
	 * kill the URBs. As it is unknown here,
	 * both orders must be used in turn.
	 * The call below is not redundant.
	 */
	usb_kill_urb(port->read_urb);
	usb_kill_urb(port->interrupt_in_urb);
	usb_kill_urb(port->interrupt_out_urb);
}

static void port_free(struct usb_serial_port *port)
{
	/*
	 * Stop all the traffic before cancelling the work, so that
	 * nobody will restart it by calling usb_serial_port_softint.
	 */
	kill_traffic(port);
	cancel_work_sync(&port->work);

	usb_free_urb(port->read_urb);
	usb_free_urb(port->write_urb);
	usb_free_urb(port->interrupt_in_urb);
	usb_free_urb(port->interrupt_out_urb);
	kfree(port->bulk_in_buffer);
	kfree(port->bulk_out_buffer);
	kfree(port->interrupt_in_buffer);
	kfree(port->interrupt_out_buffer);
	kfree(port);
}

static struct usb_serial *create_serial(struct usb_device *dev,
					struct usb_interface *interface,
					struct usb_serial_driver *driver)
{
	struct usb_serial *serial;

	serial = kzalloc(sizeof(*serial), GFP_KERNEL);
	if (!serial) {
		dev_err(&dev->dev, "%s - out of memory\n", __func__);
		return NULL;
	}
	serial->dev = usb_get_dev(dev);
	serial->type = driver;
	serial->interface = interface;
	kref_init(&serial->kref);
	mutex_init(&serial->disc_mutex);
	serial->minor = SERIAL_TTY_NO_MINOR;

	return serial;
}

static const struct usb_device_id *match_dynamic_id(struct usb_interface *intf,
					    struct usb_serial_driver *drv)
{
	struct usb_dynid *dynid;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry(dynid, &drv->dynids.list, node) {
		if (usb_match_one_id(intf, &dynid->id)) {
			spin_unlock(&drv->dynids.lock);
			return &dynid->id;
		}
	}
	spin_unlock(&drv->dynids.lock);
	return NULL;
}

static const struct usb_device_id *get_iface_id(struct usb_serial_driver *drv,
						struct usb_interface *intf)
{
	const struct usb_device_id *id;

	id = usb_match_id(intf, drv->id_table);
	if (id) {
		dbg("static descriptor matches");
		goto exit;
	}
	id = match_dynamic_id(intf, drv);
	if (id)
		dbg("dynamic descriptor matches");
exit:
	return id;
}

static struct usb_serial_driver *search_serial_device(
					struct usb_interface *iface)
{
	const struct usb_device_id *id;
	struct usb_serial_driver *drv;

	/* Check if the usb id matches a known device */
	list_for_each_entry(drv, &usb_serial_driver_list, driver_list) {
		id = get_iface_id(drv, iface);
		if (id)
			return drv;
	}

	return NULL;
}

static int serial_carrier_raised(struct tty_port *port)
{
	struct usb_serial_port *p = container_of(port, struct usb_serial_port, port);
	struct usb_serial_driver *drv = p->serial->type;
	if (drv->carrier_raised)
		return drv->carrier_raised(p);
	/* No carrier control - don't block */
	return 1;	
}

static void serial_dtr_rts(struct tty_port *port, int on)
{
	struct usb_serial_port *p = container_of(port, struct usb_serial_port, port);
	struct usb_serial_driver *drv = p->serial->type;
	if (drv->dtr_rts)
		drv->dtr_rts(p, on);
}

static const struct tty_port_operations serial_port_ops = {
	.carrier_raised = serial_carrier_raised,
	.dtr_rts = serial_dtr_rts,
};

int usb_serial_probe(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct usb_serial *serial = NULL;
	struct usb_serial_port *port;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *interrupt_out_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_out_endpoint[MAX_NUM_PORTS];
	struct usb_serial_driver *type = NULL;
	int retval;
	unsigned int minor;
	int buffer_size;
	int i;
	int num_interrupt_in = 0;
	int num_interrupt_out = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	int num_ports = 0;
	int max_endpoints;

	lock_kernel(); /* guard against unloading a serial driver module */
	type = search_serial_device(interface);
	if (!type) {
		unlock_kernel();
		dbg("none matched");
		return -ENODEV;
	}

	serial = create_serial(dev, interface, type);
	if (!serial) {
		unlock_kernel();
		dev_err(&interface->dev, "%s - out of memory\n", __func__);
		return -ENOMEM;
	}

	/* if this device type has a probe function, call it */
	if (type->probe) {
		const struct usb_device_id *id;

		if (!try_module_get(type->driver.owner)) {
			unlock_kernel();
			dev_err(&interface->dev,
				"module get failed, exiting\n");
			kfree(serial);
			return -EIO;
		}

		id = get_iface_id(type, interface);
		retval = type->probe(serial, id);
		module_put(type->driver.owner);

		if (retval) {
			unlock_kernel();
			dbg("sub driver rejected device");
			kfree(serial);
			return retval;
		}
	}

	/* descriptor matches, let's find the endpoints needed */
	/* check out the endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_bulk_in(endpoint)) {
			/* we found a bulk in endpoint */
			dbg("found bulk in on endpoint %d", i);
			bulk_in_endpoint[num_bulk_in] = endpoint;
			++num_bulk_in;
		}

		if (usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dbg("found bulk out on endpoint %d", i);
			bulk_out_endpoint[num_bulk_out] = endpoint;
			++num_bulk_out;
		}

		if (usb_endpoint_is_int_in(endpoint)) {
			/* we found a interrupt in endpoint */
			dbg("found interrupt in on endpoint %d", i);
			interrupt_in_endpoint[num_interrupt_in] = endpoint;
			++num_interrupt_in;
		}

		if (usb_endpoint_is_int_out(endpoint)) {
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
	     (le16_to_cpu(dev->descriptor.idProduct) == ATEN_PRODUCT_ID)) ||
	    ((le16_to_cpu(dev->descriptor.idVendor) == ALCOR_VENDOR_ID) &&
	     (le16_to_cpu(dev->descriptor.idProduct) == ALCOR_PRODUCT_ID)) ||
	    ((le16_to_cpu(dev->descriptor.idVendor) == SIEMENS_VENDOR_ID) &&
	     (le16_to_cpu(dev->descriptor.idProduct) == SIEMENS_PRODUCT_ID_EF81))) {
		if (interface != dev->actconfig->interface[0]) {
			/* check out the endpoints of the other interface*/
			iface_desc = dev->actconfig->interface[0]->cur_altsetting;
			for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
				endpoint = &iface_desc->endpoint[i].desc;
				if (usb_endpoint_is_int_in(endpoint)) {
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
			unlock_kernel();
			dev_info(&interface->dev, "PL-2303 hack: descriptors matched but endpoints did not\n");
			kfree(serial);
			return -ENODEV;
		}
	}
	/* END HORRIBLE HACK FOR PL2303 */
#endif

#ifdef CONFIG_USB_SERIAL_GENERIC
	if (type == &usb_serial_generic_device) {
		num_ports = num_bulk_out;
		if (num_ports == 0) {
			unlock_kernel();
			dev_err(&interface->dev,
			    "Generic device with no bulk out, not allowed.\n");
			kfree(serial);
			return -EIO;
		}
	}
#endif
	if (!num_ports) {
		/* if this device type has a calc_num_ports function, call it */
		if (type->calc_num_ports) {
			if (!try_module_get(type->driver.owner)) {
				unlock_kernel();
				dev_err(&interface->dev,
					"module get failed, exiting\n");
				kfree(serial);
				return -EIO;
			}
			num_ports = type->calc_num_ports(serial);
			module_put(type->driver.owner);
		}
		if (!num_ports)
			num_ports = type->num_ports;
	}

	serial->num_ports = num_ports;
	serial->num_bulk_in = num_bulk_in;
	serial->num_bulk_out = num_bulk_out;
	serial->num_interrupt_in = num_interrupt_in;
	serial->num_interrupt_out = num_interrupt_out;

	/* found all that we need */
	dev_info(&interface->dev, "%s converter detected\n",
			type->description);

	/* create our ports, we need as many as the max endpoints */
	/* we don't use num_ports here because some devices have more
	   endpoint pairs than ports */
	max_endpoints = max(num_bulk_in, num_bulk_out);
	max_endpoints = max(max_endpoints, num_interrupt_in);
	max_endpoints = max(max_endpoints, num_interrupt_out);
	max_endpoints = max(max_endpoints, (int)serial->num_ports);
	serial->num_port_pointers = max_endpoints;
	unlock_kernel();

	dbg("%s - setting up %d port structures for this device",
						__func__, max_endpoints);
	for (i = 0; i < max_endpoints; ++i) {
		port = kzalloc(sizeof(struct usb_serial_port), GFP_KERNEL);
		if (!port)
			goto probe_error;
		tty_port_init(&port->port);
		port->port.ops = &serial_port_ops;
		port->serial = serial;
		spin_lock_init(&port->lock);
		mutex_init(&port->mutex);
		INIT_WORK(&port->work, usb_serial_port_work);
		serial->port[i] = port;
	}

	/* set up the endpoint information */
	for (i = 0; i < num_bulk_in; ++i) {
		endpoint = bulk_in_endpoint[i];
		port = serial->port[i];
		port->read_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!port->read_urb) {
			dev_err(&interface->dev, "No free urbs available\n");
			goto probe_error;
		}
		buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
		port->bulk_in_size = buffer_size;
		port->bulk_in_endpointAddress = endpoint->bEndpointAddress;
		port->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!port->bulk_in_buffer) {
			dev_err(&interface->dev,
					"Couldn't allocate bulk_in_buffer\n");
			goto probe_error;
		}
		usb_fill_bulk_urb(port->read_urb, dev,
				usb_rcvbulkpipe(dev,
						endpoint->bEndpointAddress),
				port->bulk_in_buffer, buffer_size,
				serial->type->read_bulk_callback, port);
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
		port->bulk_out_buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!port->bulk_out_buffer) {
			dev_err(&interface->dev,
					"Couldn't allocate bulk_out_buffer\n");
			goto probe_error;
		}
		usb_fill_bulk_urb(port->write_urb, dev,
				usb_sndbulkpipe(dev,
					endpoint->bEndpointAddress),
				port->bulk_out_buffer, buffer_size,
				serial->type->write_bulk_callback, port);
	}

	if (serial->type->read_int_callback) {
		for (i = 0; i < num_interrupt_in; ++i) {
			endpoint = interrupt_in_endpoint[i];
			port = serial->port[i];
			port->interrupt_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->interrupt_in_urb) {
				dev_err(&interface->dev,
						"No free urbs available\n");
				goto probe_error;
			}
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			port->interrupt_in_endpointAddress =
						endpoint->bEndpointAddress;
			port->interrupt_in_buffer = kmalloc(buffer_size,
								GFP_KERNEL);
			if (!port->interrupt_in_buffer) {
				dev_err(&interface->dev,
				    "Couldn't allocate interrupt_in_buffer\n");
				goto probe_error;
			}
			usb_fill_int_urb(port->interrupt_in_urb, dev,
				usb_rcvintpipe(dev,
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
				dev_err(&interface->dev,
						"No free urbs available\n");
				goto probe_error;
			}
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			port->interrupt_out_size = buffer_size;
			port->interrupt_out_endpointAddress =
						endpoint->bEndpointAddress;
			port->interrupt_out_buffer = kmalloc(buffer_size,
								GFP_KERNEL);
			if (!port->interrupt_out_buffer) {
				dev_err(&interface->dev,
				  "Couldn't allocate interrupt_out_buffer\n");
				goto probe_error;
			}
			usb_fill_int_urb(port->interrupt_out_urb, dev,
				usb_sndintpipe(dev,
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
		if (!try_module_get(type->driver.owner)) {
			dev_err(&interface->dev,
					"module get failed, exiting\n");
			goto probe_error;
		}
		retval = type->attach(serial);
		module_put(type->driver.owner);
		if (retval < 0)
			goto probe_error;
		if (retval > 0) {
			/* quietly accept this device, but don't bind to a
			   serial port as it's about to disappear */
			serial->num_ports = 0;
			goto exit;
		}
	}

	if (get_free_serial(serial, num_ports, &minor) == NULL) {
		dev_err(&interface->dev, "No more free serial devices\n");
		goto probe_error;
	}
	serial->minor = minor;

	/* register all of the individual ports with the driver core */
	for (i = 0; i < num_ports; ++i) {
		port = serial->port[i];
		port->dev.parent = &interface->dev;
		port->dev.driver = NULL;
		port->dev.bus = &usb_serial_bus_type;
		port->dev.release = &port_release;

		dev_set_name(&port->dev, "ttyUSB%d", port->number);
		dbg ("%s - registering %s", __func__, dev_name(&port->dev));
		retval = device_register(&port->dev);
		if (retval)
			dev_err(&port->dev, "Error registering port device, "
				"continuing\n");
	}

	usb_serial_console_init(debug, minor);

exit:
	/* success */
	usb_set_intfdata(interface, serial);
	return 0;

probe_error:
	for (i = 0; i < num_bulk_in; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		usb_free_urb(port->read_urb);
		kfree(port->bulk_in_buffer);
	}
	for (i = 0; i < num_bulk_out; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		usb_free_urb(port->write_urb);
		kfree(port->bulk_out_buffer);
	}
	for (i = 0; i < num_interrupt_in; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		usb_free_urb(port->interrupt_in_urb);
		kfree(port->interrupt_in_buffer);
	}
	for (i = 0; i < num_interrupt_out; ++i) {
		port = serial->port[i];
		if (!port)
			continue;
		usb_free_urb(port->interrupt_out_urb);
		kfree(port->interrupt_out_buffer);
	}

	/* free up any memory that we allocated */
	for (i = 0; i < serial->num_port_pointers; ++i)
		kfree(serial->port[i]);
	kfree(serial);
	return -EIO;
}
EXPORT_SYMBOL_GPL(usb_serial_probe);

void usb_serial_disconnect(struct usb_interface *interface)
{
	int i;
	struct usb_serial *serial = usb_get_intfdata(interface);
	struct device *dev = &interface->dev;
	struct usb_serial_port *port;

	usb_serial_console_disconnect(serial);
	dbg("%s", __func__);

	mutex_lock(&serial->disc_mutex);
	usb_set_intfdata(interface, NULL);
	/* must set a flag, to signal subdrivers */
	serial->disconnected = 1;
	mutex_unlock(&serial->disc_mutex);

	/* Unfortunately, many of the sub-drivers expect the port structures
	 * to exist when their shutdown method is called, so we have to go
	 * through this awkward two-step unregistration procedure.
	 */
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (port) {
			struct tty_struct *tty = tty_port_tty_get(&port->port);
			if (tty) {
				/* The hangup will occur asynchronously but
				   the object refcounts will sort out all the
				   cleanup */
				tty_hangup(tty);
				tty_kref_put(tty);
			}
			kill_traffic(port);
			cancel_work_sync(&port->work);
			device_del(&port->dev);
		}
	}
	serial->type->shutdown(serial);
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (port) {
			put_device(&port->dev);
			serial->port[i] = NULL;
		}
	}

	/* let the last holder of this object
	 * cause it to be cleaned up */
	usb_serial_put(serial);
	dev_info(dev, "device disconnected\n");
}
EXPORT_SYMBOL_GPL(usb_serial_disconnect);

int usb_serial_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	struct usb_serial_port *port;
	int i, r = 0;

	serial->suspending = 1;

	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (port)
			kill_traffic(port);
	}

	if (serial->type->suspend)
		r = serial->type->suspend(serial, message);

	return r;
}
EXPORT_SYMBOL(usb_serial_suspend);

int usb_serial_resume(struct usb_interface *intf)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	int rv;

	serial->suspending = 0;
	if (serial->type->resume)
		rv = serial->type->resume(serial);
	else
		rv = usb_serial_generic_resume(serial);

	return rv;
}
EXPORT_SYMBOL(usb_serial_resume);

static const struct tty_operations serial_ops = {
	.open =			serial_open,
	.close =		serial_close,
	.write =		serial_write,
	.hangup = 		serial_hangup,
	.write_room =		serial_write_room,
	.ioctl =		serial_ioctl,
	.set_termios =		serial_set_termios,
	.throttle =		serial_throttle,
	.unthrottle =		serial_unthrottle,
	.break_ctl =		serial_break,
	.chars_in_buffer =	serial_chars_in_buffer,
	.tiocmget =		serial_tiocmget,
	.tiocmset =		serial_tiocmset,
	.proc_fops =		&serial_proc_fops,
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
	for (i = 0; i < SERIAL_TTY_MINORS; ++i)
		serial_table[i] = NULL;

	result = bus_register(&usb_serial_bus_type);
	if (result) {
		printk(KERN_ERR "usb-serial: %s - registering bus driver "
		       "failed\n", __func__);
		goto exit_bus;
	}

	usb_serial_tty_driver->owner = THIS_MODULE;
	usb_serial_tty_driver->driver_name = "usbserial";
	usb_serial_tty_driver->name = 	"ttyUSB";
	usb_serial_tty_driver->major = SERIAL_TTY_MAJOR;
	usb_serial_tty_driver->minor_start = 0;
	usb_serial_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	usb_serial_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	usb_serial_tty_driver->flags = TTY_DRIVER_REAL_RAW |
						TTY_DRIVER_DYNAMIC_DEV;
	usb_serial_tty_driver->init_termios = tty_std_termios;
	usb_serial_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD
							| HUPCL | CLOCAL;
	usb_serial_tty_driver->init_termios.c_ispeed = 9600;
	usb_serial_tty_driver->init_termios.c_ospeed = 9600;
	tty_set_operations(usb_serial_tty_driver, &serial_ops);
	result = tty_register_driver(usb_serial_tty_driver);
	if (result) {
		printk(KERN_ERR "usb-serial: %s - tty_register_driver failed\n",
		       __func__);
		goto exit_reg_driver;
	}

	/* register the USB driver */
	result = usb_register(&usb_serial_driver);
	if (result < 0) {
		printk(KERN_ERR "usb-serial: %s - usb_register failed\n",
		       __func__);
		goto exit_tty;
	}

	/* register the generic driver, if we should */
	result = usb_serial_generic_register(debug);
	if (result < 0) {
		printk(KERN_ERR "usb-serial: %s - registering generic "
		       "driver failed\n", __func__);
		goto exit_generic;
	}

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");

	return result;

exit_generic:
	usb_deregister(&usb_serial_driver);

exit_tty:
	tty_unregister_driver(usb_serial_tty_driver);

exit_reg_driver:
	bus_unregister(&usb_serial_bus_type);

exit_bus:
	printk(KERN_ERR "usb-serial: %s - returning with error %d\n",
	       __func__, result);
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

static void fixup_generic(struct usb_serial_driver *device)
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

int usb_serial_register(struct usb_serial_driver *driver)
{
	/* must be called with BKL held */
	int retval;

	if (usb_disabled())
		return -ENODEV;

	fixup_generic(driver);

	if (!driver->description)
		driver->description = driver->driver.name;

	/* Add this device to our list of devices */
	list_add(&driver->driver_list, &usb_serial_driver_list);

	retval = usb_serial_bus_register(driver);
	if (retval) {
		printk(KERN_ERR "usb-serial: problem %d when registering "
		       "driver %s\n", retval, driver->description);
		list_del(&driver->driver_list);
	} else
		printk(KERN_INFO "USB Serial support registered for %s\n",
						driver->description);

	return retval;
}
EXPORT_SYMBOL_GPL(usb_serial_register);


void usb_serial_deregister(struct usb_serial_driver *device)
{
	/* must be called with BKL held */
	printk(KERN_INFO "USB Serial deregistering driver %s\n",
	       device->description);
	list_del(&device->driver_list);
	usb_serial_bus_deregister(device);
}
EXPORT_SYMBOL_GPL(usb_serial_deregister);

/* Module information */
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
