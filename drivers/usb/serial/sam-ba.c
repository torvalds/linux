/*
 * Atmel SAM Boot Assistant (SAM-BA) driver
 *
 * Copyright (C) 2010 Johan Hovold <jhovold@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>


#define DRIVER_VERSION	"v1.0"
#define DRIVER_AUTHOR	"Johan Hovold <jhovold@gmail.com>"
#define DRIVER_DESC	"Atmel SAM Boot Assistant (SAM-BA) driver"

#define SAMBA_VENDOR_ID		0x3eb
#define SAMBA_PRODUCT_ID	0x6124


static int debug;

static const struct usb_device_id id_table[] = {
	/*
	 * NOTE: Only match the CDC Data interface.
	 */
	{ USB_DEVICE_AND_INTERFACE_INFO(SAMBA_VENDOR_ID, SAMBA_PRODUCT_ID,
					USB_CLASS_CDC_DATA, 0, 0) },
	{ }
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver samba_driver = {
	.name		= "sam-ba",
	.probe		= usb_serial_probe,
	.disconnect	= usb_serial_disconnect,
	.id_table	= id_table,
	.no_dynamic_id	= 1,
};


/*
 * NOTE: The SAM-BA firmware cannot handle merged write requests so we cannot
 *       use the generic write implementation (which uses the port write fifo).
 */
static int samba_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count)
{
	struct urb *urb;
	unsigned long flags;
	int result;
	int i;

	if (!count)
		return 0;

	count = min_t(int, count, port->bulk_out_size);

	spin_lock_irqsave(&port->lock, flags);
	if (!port->write_urbs_free) {
		spin_unlock_irqrestore(&port->lock, flags);
		return 0;
	}
	i = find_first_bit(&port->write_urbs_free,
						ARRAY_SIZE(port->write_urbs));
	__clear_bit(i, &port->write_urbs_free);
	port->tx_bytes += count;
	spin_unlock_irqrestore(&port->lock, flags);

	urb = port->write_urbs[i];
	memcpy(urb->transfer_buffer, buf, count);
	urb->transfer_buffer_length = count;
	usb_serial_debug_data(debug, &port->dev, __func__, count,
						urb->transfer_buffer);
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result) {
		dev_err(&port->dev, "%s - error submitting urb: %d\n",
						__func__, result);
		spin_lock_irqsave(&port->lock, flags);
		__set_bit(i, &port->write_urbs_free);
		port->tx_bytes -= count;
		spin_unlock_irqrestore(&port->lock, flags);

		return result;
	}

	return count;
}

static int samba_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	unsigned long free;
	int count;
	int room;

	spin_lock_irqsave(&port->lock, flags);
	free = port->write_urbs_free;
	spin_unlock_irqrestore(&port->lock, flags);

	count = hweight_long(free);
	room = count * port->bulk_out_size;

	dbg("%s - returns %d", __func__, room);

	return room;
}

static int samba_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned long flags;
	int chars;

	spin_lock_irqsave(&port->lock, flags);
	chars = port->tx_bytes;
	spin_unlock_irqrestore(&port->lock, flags);

	dbg("%s - returns %d", __func__, chars);

	return chars;
}

static void samba_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned long flags;
	int i;

	dbg("%s - port %d", __func__, port->number);

	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i) {
		if (port->write_urbs[i] == urb)
			break;
	}
	spin_lock_irqsave(&port->lock, flags);
	__set_bit(i, &port->write_urbs_free);
	port->tx_bytes -= urb->transfer_buffer_length;
	spin_unlock_irqrestore(&port->lock, flags);

	if (urb->status)
		dbg("%s - non-zero urb status: %d", __func__, urb->status);

	usb_serial_port_softint(port);
}

static struct usb_serial_driver samba_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "sam-ba",
	},
	.usb_driver		= &samba_driver,
	.id_table		= id_table,
	.num_ports		= 1,
	.bulk_in_size		= 512,
	.bulk_out_size		= 2048,
	.write			= samba_write,
	.write_room		= samba_write_room,
	.chars_in_buffer	= samba_chars_in_buffer,
	.write_bulk_callback	= samba_write_bulk_callback,
	.throttle		= usb_serial_generic_throttle,
	.unthrottle		= usb_serial_generic_unthrottle,
};

static int __init samba_init(void)
{
	int retval;

	retval = usb_serial_register(&samba_device);
	if (retval)
		return retval;

	retval = usb_register(&samba_driver);
	if (retval) {
		usb_serial_deregister(&samba_device);
		return retval;
	}

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ": "
							DRIVER_DESC "\n");
	return 0;
}

static void __exit samba_exit(void)
{
	usb_deregister(&samba_driver);
	usb_serial_deregister(&samba_device);
}

module_init(samba_init);
module_exit(samba_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable verbose debugging messages");
