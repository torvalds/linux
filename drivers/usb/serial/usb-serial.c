/*
 * USB Serial Converter driver
 *
 * Copyright (C) 1999 - 2012 Greg Kroah-Hartman (greg@kroah.com)
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
#include <linux/serial.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/kfifo.h>
#include "pl2303.h"

/*
 * Version Information
 */
#define DRIVER_AUTHOR "Greg Kroah-Hartman, greg@kroah.com, http://www.kroah.com/linux/"
#define DRIVER_DESC "USB Serial Driver core"

/* There is no MODULE_DEVICE_TABLE for usbserial.c.  Instead
   the MODULE_DEVICE_TABLE declarations in each serial driver
   cause the "hotplug" program to pull in whatever module is necessary
   via modprobe, and modprobe will load usbserial because the serial
   drivers depend on it.
*/

static bool debug;
/* initially all NULL */
static struct usb_serial *serial_table[SERIAL_TTY_MINORS];
static DEFINE_MUTEX(table_lock);
static LIST_HEAD(usb_serial_driver_list);

/*
 * Look up the serial structure.  If it is found and it hasn't been
 * disconnected, return with its disc_mutex held and its refcount
 * incremented.  Otherwise return NULL.
 */
struct usb_serial *usb_serial_get_by_index(unsigned index)
{
	struct usb_serial *serial;

	mutex_lock(&table_lock);
	serial = serial_table[index];

	if (serial) {
		mutex_lock(&serial->disc_mutex);
		if (serial->disconnected) {
			mutex_unlock(&serial->disc_mutex);
			serial = NULL;
		} else {
			kref_get(&serial->kref);
		}
	}
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

	mutex_lock(&table_lock);
	for (i = 0; i < serial->num_ports; ++i)
		serial_table[serial->minor + i] = NULL;
	mutex_unlock(&table_lock);
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

	if (serial->attached)
		serial->type->release(serial);

	/* Now that nothing is using the ports, they can be freed */
	for (i = 0; i < serial->num_port_pointers; ++i) {
		port = serial->port[i];
		if (port) {
			port->serial = NULL;
			put_device(&port->dev);
		}
	}

	usb_put_dev(serial->dev);
	kfree(serial);
}

void usb_serial_put(struct usb_serial *serial)
{
	kref_put(&serial->kref, destroy_serial);
}

/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/

/**
 * serial_install - install tty
 * @driver: the driver (USB in our case)
 * @tty: the tty being created
 *
 * Create the termios objects for this tty.  We use the default
 * USB serial settings but permit them to be overridden by
 * serial->type->init_termios.
 *
 * This is the first place a new tty gets used.  Hence this is where we
 * acquire references to the usb_serial structure and the driver module,
 * where we store a pointer to the port, and where we do an autoresume.
 * All these actions are reversed in serial_cleanup().
 */
static int serial_install(struct tty_driver *driver, struct tty_struct *tty)
{
	int idx = tty->index;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	int retval = -ENODEV;

	dbg("%s", __func__);

	serial = usb_serial_get_by_index(idx);
	if (!serial)
		return retval;

	port = serial->port[idx - serial->minor];
	if (!port)
		goto error_no_port;
	if (!try_module_get(serial->type->driver.owner))
		goto error_module_get;

	retval = usb_autopm_get_interface(serial->interface);
	if (retval)
		goto error_get_interface;

	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error_init_termios;

	mutex_unlock(&serial->disc_mutex);

	/* allow the driver to update the settings */
	if (serial->type->init_termios)
		serial->type->init_termios(tty);

	tty->driver_data = port;

	return retval;

 error_init_termios:
	usb_autopm_put_interface(serial->interface);
 error_get_interface:
	module_put(serial->type->driver.owner);
 error_module_get:
 error_no_port:
	usb_serial_put(serial);
	mutex_unlock(&serial->disc_mutex);
	return retval;
}

static int serial_activate(struct tty_port *tport, struct tty_struct *tty)
{
	struct usb_serial_port *port =
		container_of(tport, struct usb_serial_port, port);
	struct usb_serial *serial = port->serial;
	int retval;

	mutex_lock(&serial->disc_mutex);
	if (serial->disconnected)
		retval = -ENODEV;
	else
		retval = port->serial->type->open(tty, port);
	mutex_unlock(&serial->disc_mutex);

	if (retval < 0)
		retval = usb_translate_errors(retval);

	return retval;
}

static int serial_open(struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);
	return tty_port_open(&port->port, tty, filp);
}

/**
 * serial_down - shut down hardware
 * @tport: tty port to shut down
 *
 * Shut down a USB serial port unless it is the console.  We never
 * shut down the console hardware as it will always be in use. Serialized
 * against activate by the tport mutex and kept to matching open/close pairs
 * of calls by the ASYNCB_INITIALIZED flag.
 */
static void serial_down(struct tty_port *tport)
{
	struct usb_serial_port *port =
		container_of(tport, struct usb_serial_port, port);
	struct usb_serial_driver *drv = port->serial->type;
	/*
	 * The console is magical.  Do not hang up the console hardware
	 * or there will be tears.
	 */
	if (port->port.console)
		return;
	if (drv->close)
		drv->close(port);
}

static void serial_hangup(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);
	tty_port_hangup(&port->port);
}

static void serial_close(struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);
	tty_port_close(&port->port, tty, filp);
}

/**
 * serial_cleanup - free resources post close/hangup
 * @port: port to free up
 *
 * Do the resource freeing and refcount dropping for the port.
 * Avoid freeing the console.
 *
 * Called asynchronously after the last tty kref is dropped,
 * and the tty layer has already done the tty_shutdown(tty);
 */
static void serial_cleanup(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial;
	struct module *owner;

	/* The console is magical.  Do not hang up the console hardware
	 * or there will be tears.
	 */
	if (port->port.console)
		return;

	dbg("%s - port %d", __func__, port->number);

	tty->driver_data = NULL;

	serial = port->serial;
	owner = serial->type->driver.owner;

	mutex_lock(&serial->disc_mutex);
	if (!serial->disconnected)
		usb_autopm_put_interface(serial->interface);
	mutex_unlock(&serial->disc_mutex);

	usb_serial_put(serial);
	module_put(owner);
}

static int serial_write(struct tty_struct *tty, const unsigned char *buf,
								int count)
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -ENODEV;

	if (port->serial->dev->state == USB_STATE_NOTATTACHED)
		goto exit;

	dbg("%s - port %d, %d byte(s)", __func__, port->number, count);

	/* pass on to the driver specific version of this function */
	retval = port->serial->type->write(tty, port, buf, count);
	if (retval < 0)
		retval = usb_translate_errors(retval);
exit:
	return retval;
}

static int serial_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);
	/* pass on to the driver specific version of this function */
	return port->serial->type->write_room(tty);
}

static int serial_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);

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

	/* pass on to the driver specific version of this function */
	if (port->serial->type->throttle)
		port->serial->type->throttle(tty);
}

static void serial_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);

	/* pass on to the driver specific version of this function */
	if (port->serial->type->unthrottle)
		port->serial->type->unthrottle(tty);
}

static int serial_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	int retval = -ENODEV;

	dbg("%s - port %d, cmd 0x%.4x", __func__, port->number, cmd);

	/* pass on to the driver specific version of this function
	   if it is available */
	if (port->serial->type->ioctl) {
		retval = port->serial->type->ioctl(tty, cmd, arg);
	} else
		retval = -ENOIOCTLCMD;
	return retval;
}

static void serial_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct usb_serial_port *port = tty->driver_data;
	dbg("%s - port %d", __func__, port->number);

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
		mutex_unlock(&serial->disc_mutex);
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

static int serial_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);

	if (port->serial->type->tiocmget)
		return port->serial->type->tiocmget(tty);
	return -EINVAL;
}

static int serial_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);

	if (port->serial->type->tiocmset)
		return port->serial->type->tiocmset(tty, set, clear);
	return -EINVAL;
}

static int serial_get_icount(struct tty_struct *tty,
				struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;

	dbg("%s - port %d", __func__, port->number);

	if (port->serial->type->get_icount)
		return port->serial->type->get_icount(tty, icount);
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

static void kill_traffic(struct usb_serial_port *port)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i)
		usb_kill_urb(port->read_urbs[i]);
	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i)
		usb_kill_urb(port->write_urbs[i]);
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

static void port_release(struct device *dev)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	int i;

	dbg ("%s - %s", __func__, dev_name(dev));

	/*
	 * Stop all the traffic before cancelling the work, so that
	 * nobody will restart it by calling usb_serial_port_softint.
	 */
	kill_traffic(port);
	cancel_work_sync(&port->work);

	usb_free_urb(port->interrupt_in_urb);
	usb_free_urb(port->interrupt_out_urb);
	for (i = 0; i < ARRAY_SIZE(port->read_urbs); ++i) {
		usb_free_urb(port->read_urbs[i]);
		kfree(port->bulk_in_buffers[i]);
	}
	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i) {
		usb_free_urb(port->write_urbs[i]);
		kfree(port->bulk_out_buffers[i]);
	}
	kfifo_free(&port->write_fifo);
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

/* Caller must hold table_lock */
static struct usb_serial_driver *search_serial_device(
					struct usb_interface *iface)
{
	const struct usb_device_id *id = NULL;
	struct usb_serial_driver *drv;
	struct usb_driver *driver = to_usb_driver(iface->dev.driver);

	/* Check if the usb id matches a known device */
	list_for_each_entry(drv, &usb_serial_driver_list, driver_list) {
		if (drv->usb_driver == driver)
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
	.activate = serial_activate,
	.shutdown = serial_down,
};

static int usb_serial_probe(struct usb_interface *interface,
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
	int j;
	int num_interrupt_in = 0;
	int num_interrupt_out = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	int num_ports = 0;
	int max_endpoints;

	mutex_lock(&table_lock);
	type = search_serial_device(interface);
	if (!type) {
		mutex_unlock(&table_lock);
		dbg("none matched");
		return -ENODEV;
	}

	if (!try_module_get(type->driver.owner)) {
		mutex_unlock(&table_lock);
		dev_err(&interface->dev, "module get failed, exiting\n");
		return -EIO;
	}
	mutex_unlock(&table_lock);

	serial = create_serial(dev, interface, type);
	if (!serial) {
		module_put(type->driver.owner);
		dev_err(&interface->dev, "%s - out of memory\n", __func__);
		return -ENOMEM;
	}

	/* if this device type has a probe function, call it */
	if (type->probe) {
		const struct usb_device_id *id;

		id = get_iface_id(type, interface);
		retval = type->probe(serial, id);

		if (retval) {
			dbg("sub driver rejected device");
			usb_serial_put(serial);
			module_put(type->driver.owner);
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
			dev_info(&interface->dev, "PL-2303 hack: descriptors matched but endpoints did not\n");
			usb_serial_put(serial);
			module_put(type->driver.owner);
			return -ENODEV;
		}
	}
	/* END HORRIBLE HACK FOR PL2303 */
#endif

#ifdef CONFIG_USB_SERIAL_GENERIC
	if (type == &usb_serial_generic_device) {
		num_ports = num_bulk_out;
		if (num_ports == 0) {
			dev_err(&interface->dev,
			    "Generic device with no bulk out, not allowed.\n");
			usb_serial_put(serial);
			module_put(type->driver.owner);
			return -EIO;
		}
		dev_info(&interface->dev, "The \"generic\" usb-serial driver is only for testing and one-off prototypes.\n");
		dev_info(&interface->dev, "Tell linux-usb@vger.kernel.org to add your device to a proper driver.\n");
	}
#endif
	if (!num_ports) {
		/* if this device type has a calc_num_ports function, call it */
		if (type->calc_num_ports)
			num_ports = type->calc_num_ports(serial);
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
		/* Keep this for private driver use for the moment but
		   should probably go away */
		INIT_WORK(&port->work, usb_serial_port_work);
		serial->port[i] = port;
		port->dev.parent = &interface->dev;
		port->dev.driver = NULL;
		port->dev.bus = &usb_serial_bus_type;
		port->dev.release = &port_release;
		device_initialize(&port->dev);
	}

	/* set up the endpoint information */
	for (i = 0; i < num_bulk_in; ++i) {
		endpoint = bulk_in_endpoint[i];
		port = serial->port[i];
		buffer_size = max_t(int, serial->type->bulk_in_size,
				usb_endpoint_maxp(endpoint));
		port->bulk_in_size = buffer_size;
		port->bulk_in_endpointAddress = endpoint->bEndpointAddress;

		for (j = 0; j < ARRAY_SIZE(port->read_urbs); ++j) {
			set_bit(j, &port->read_urbs_free);
			port->read_urbs[j] = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->read_urbs[j]) {
				dev_err(&interface->dev,
						"No free urbs available\n");
				goto probe_error;
			}
			port->bulk_in_buffers[j] = kmalloc(buffer_size,
								GFP_KERNEL);
			if (!port->bulk_in_buffers[j]) {
				dev_err(&interface->dev,
					"Couldn't allocate bulk_in_buffer\n");
				goto probe_error;
			}
			usb_fill_bulk_urb(port->read_urbs[j], dev,
					usb_rcvbulkpipe(dev,
						endpoint->bEndpointAddress),
					port->bulk_in_buffers[j], buffer_size,
					serial->type->read_bulk_callback,
					port);
		}

		port->read_urb = port->read_urbs[0];
		port->bulk_in_buffer = port->bulk_in_buffers[0];
	}

	for (i = 0; i < num_bulk_out; ++i) {
		endpoint = bulk_out_endpoint[i];
		port = serial->port[i];
		if (kfifo_alloc(&port->write_fifo, PAGE_SIZE, GFP_KERNEL))
			goto probe_error;
		buffer_size = serial->type->bulk_out_size;
		if (!buffer_size)
			buffer_size = usb_endpoint_maxp(endpoint);
		port->bulk_out_size = buffer_size;
		port->bulk_out_endpointAddress = endpoint->bEndpointAddress;

		for (j = 0; j < ARRAY_SIZE(port->write_urbs); ++j) {
			set_bit(j, &port->write_urbs_free);
			port->write_urbs[j] = usb_alloc_urb(0, GFP_KERNEL);
			if (!port->write_urbs[j]) {
				dev_err(&interface->dev,
						"No free urbs available\n");
				goto probe_error;
			}
			port->bulk_out_buffers[j] = kmalloc(buffer_size,
								GFP_KERNEL);
			if (!port->bulk_out_buffers[j]) {
				dev_err(&interface->dev,
					"Couldn't allocate bulk_out_buffer\n");
				goto probe_error;
			}
			usb_fill_bulk_urb(port->write_urbs[j], dev,
					usb_sndbulkpipe(dev,
						endpoint->bEndpointAddress),
					port->bulk_out_buffers[j], buffer_size,
					serial->type->write_bulk_callback,
					port);
		}

		port->write_urb = port->write_urbs[0];
		port->bulk_out_buffer = port->bulk_out_buffers[0];
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
			buffer_size = usb_endpoint_maxp(endpoint);
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
			buffer_size = usb_endpoint_maxp(endpoint);
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

	usb_set_intfdata(interface, serial);

	/* if this device type has an attach function, call it */
	if (type->attach) {
		retval = type->attach(serial);
		if (retval < 0)
			goto probe_error;
		serial->attached = 1;
		if (retval > 0) {
			/* quietly accept this device, but don't bind to a
			   serial port as it's about to disappear */
			serial->num_ports = 0;
			goto exit;
		}
	} else {
		serial->attached = 1;
	}

	/* Avoid race with tty_open and serial_install by setting the
	 * disconnected flag and not clearing it until all ports have been
	 * registered.
	 */
	serial->disconnected = 1;

	if (get_free_serial(serial, num_ports, &minor) == NULL) {
		dev_err(&interface->dev, "No more free serial devices\n");
		goto probe_error;
	}
	serial->minor = minor;

	/* register all of the individual ports with the driver core */
	for (i = 0; i < num_ports; ++i) {
		port = serial->port[i];
		dev_set_name(&port->dev, "ttyUSB%d", port->number);
		dbg ("%s - registering %s", __func__, dev_name(&port->dev));
		device_enable_async_suspend(&port->dev);

		retval = device_add(&port->dev);
		if (retval)
			dev_err(&port->dev, "Error registering port device, "
				"continuing\n");
	}

	serial->disconnected = 0;

	usb_serial_console_init(debug, minor);
exit:
	module_put(type->driver.owner);
	return 0;

probe_error:
	usb_serial_put(serial);
	module_put(type->driver.owner);
	return -EIO;
}

static void usb_serial_disconnect(struct usb_interface *interface)
{
	int i;
	struct usb_serial *serial = usb_get_intfdata(interface);
	struct device *dev = &interface->dev;
	struct usb_serial_port *port;

	usb_serial_console_disconnect(serial);
	dbg("%s", __func__);

	mutex_lock(&serial->disc_mutex);
	/* must set a flag, to signal subdrivers */
	serial->disconnected = 1;
	mutex_unlock(&serial->disc_mutex);

	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (port) {
			struct tty_struct *tty = tty_port_tty_get(&port->port);
			if (tty) {
				tty_vhangup(tty);
				tty_kref_put(tty);
			}
			kill_traffic(port);
			cancel_work_sync(&port->work);
			if (device_is_registered(&port->dev))
				device_del(&port->dev);
		}
	}
	serial->type->disconnect(serial);

	/* let the last holder of this object cause it to be cleaned up */
	usb_serial_put(serial);
	dev_info(dev, "device disconnected\n");
}

int usb_serial_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	struct usb_serial_port *port;
	int i, r = 0;

	serial->suspending = 1;

	if (serial->type->suspend) {
		r = serial->type->suspend(serial, message);
		if (r < 0) {
			serial->suspending = 0;
			goto err_out;
		}
	}

	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		if (port)
			kill_traffic(port);
	}

err_out:
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

static int usb_serial_reset_resume(struct usb_interface *intf)
{
	struct usb_serial *serial = usb_get_intfdata(intf);
	int rv;

	serial->suspending = 0;
	if (serial->type->reset_resume)
		rv = serial->type->reset_resume(serial);
	else {
		rv = -EOPNOTSUPP;
		intf->needs_binding = 1;
	}

	return rv;
}

static const struct tty_operations serial_ops = {
	.open =			serial_open,
	.close =		serial_close,
	.write =		serial_write,
	.hangup =		serial_hangup,
	.write_room =		serial_write_room,
	.ioctl =		serial_ioctl,
	.set_termios =		serial_set_termios,
	.throttle =		serial_throttle,
	.unthrottle =		serial_unthrottle,
	.break_ctl =		serial_break,
	.chars_in_buffer =	serial_chars_in_buffer,
	.tiocmget =		serial_tiocmget,
	.tiocmset =		serial_tiocmset,
	.get_icount =		serial_get_icount,
	.cleanup =		serial_cleanup,
	.install =		serial_install,
	.proc_fops =		&serial_proc_fops,
};


struct tty_driver *usb_serial_tty_driver;

/* Driver structure we register with the USB core */
static struct usb_driver usb_serial_driver = {
	.name =		"usbserial",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.suspend =	usb_serial_suspend,
	.resume =	usb_serial_resume,
	.no_dynamic_id =	1,
	.supports_autosuspend =	1,
};

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

	usb_serial_tty_driver->driver_name = "usbserial";
	usb_serial_tty_driver->name = "ttyUSB";
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
	set_to_generic_if_null(device, disconnect);
	set_to_generic_if_null(device, release);
	set_to_generic_if_null(device, process_read_urb);
	set_to_generic_if_null(device, prepare_write_buffer);
}

static int usb_serial_register(struct usb_serial_driver *driver)
{
	int retval;

	if (usb_disabled())
		return -ENODEV;

	fixup_generic(driver);

	if (!driver->description)
		driver->description = driver->driver.name;
	if (!driver->usb_driver) {
		WARN(1, "Serial driver %s has no usb_driver\n",
				driver->description);
		return -EINVAL;
	}

	/* Add this device to our list of devices */
	mutex_lock(&table_lock);
	list_add(&driver->driver_list, &usb_serial_driver_list);

	retval = usb_serial_bus_register(driver);
	if (retval) {
		printk(KERN_ERR "usb-serial: problem %d when registering "
		       "driver %s\n", retval, driver->description);
		list_del(&driver->driver_list);
	} else
		printk(KERN_INFO "USB Serial support registered for %s\n",
						driver->description);

	mutex_unlock(&table_lock);
	return retval;
}

static void usb_serial_deregister(struct usb_serial_driver *device)
{
	printk(KERN_INFO "USB Serial deregistering driver %s\n",
	       device->description);
	mutex_lock(&table_lock);
	list_del(&device->driver_list);
	usb_serial_bus_deregister(device);
	mutex_unlock(&table_lock);
}

/**
 * usb_serial_register_drivers - register drivers for a usb-serial module
 * @serial_drivers: NULL-terminated array of pointers to drivers to be registered
 * @name: name of the usb_driver for this set of @serial_drivers
 * @id_table: list of all devices this @serial_drivers set binds to
 *
 * Registers all the drivers in the @serial_drivers array, and dynamically
 * creates a struct usb_driver with the name @name and id_table of @id_table.
 */
int usb_serial_register_drivers(struct usb_serial_driver *const serial_drivers[],
				const char *name,
				const struct usb_device_id *id_table)
{
	int rc;
	struct usb_driver *udriver;
	struct usb_serial_driver * const *sd;

	/*
	 * udriver must be registered before any of the serial drivers,
	 * because the store_new_id() routine for the serial drivers (in
	 * bus.c) probes udriver.
	 *
	 * Performance hack: We don't want udriver to be probed until
	 * the serial drivers are registered, because the probe would
	 * simply fail for lack of a matching serial driver.
	 * So we leave udriver's id_table set to NULL until we are all set.
	 *
	 * Suspend/resume support is implemented in the usb-serial core,
	 * so fill in the PM-related fields in udriver.
	 */
	udriver = kzalloc(sizeof(*udriver), GFP_KERNEL);
	if (!udriver)
		return -ENOMEM;

	udriver->name = name;
	udriver->no_dynamic_id = 1;
	udriver->supports_autosuspend = 1;
	udriver->suspend = usb_serial_suspend;
	udriver->resume = usb_serial_resume;
	udriver->probe = usb_serial_probe;
	udriver->disconnect = usb_serial_disconnect;

	/* we only set the reset_resume field if the serial_driver has one */
	for (sd = serial_drivers; *sd; ++sd) {
		if ((*sd)->reset_resume)
			udriver->reset_resume = usb_serial_reset_resume;
			break;
	}

	rc = usb_register(udriver);
	if (rc)
		return rc;

	for (sd = serial_drivers; *sd; ++sd) {
		(*sd)->usb_driver = udriver;
		rc = usb_serial_register(*sd);
		if (rc)
			goto failed;
	}

	/* Now set udriver's id_table and look for matches */
	udriver->id_table = id_table;
	rc = driver_attach(&udriver->drvwrap.driver);
	return 0;

 failed:
	while (sd-- > serial_drivers)
		usb_serial_deregister(*sd);
	usb_deregister(udriver);
	return rc;
}
EXPORT_SYMBOL_GPL(usb_serial_register_drivers);

/**
 * usb_serial_deregister_drivers - deregister drivers for a usb-serial module
 * @serial_drivers: NULL-terminated array of pointers to drivers to be deregistered
 *
 * Deregisters all the drivers in the @serial_drivers array and deregisters and
 * frees the struct usb_driver that was created by the call to
 * usb_serial_register_drivers().
 */
void usb_serial_deregister_drivers(struct usb_serial_driver *const serial_drivers[])
{
	struct usb_driver *udriver = (*serial_drivers)->usb_driver;

	for (; *serial_drivers; ++serial_drivers)
		usb_serial_deregister(*serial_drivers);
	usb_deregister(udriver);
	kfree(udriver);
}
EXPORT_SYMBOL_GPL(usb_serial_deregister_drivers);

/* Module information */
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
