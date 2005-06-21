/*
 * IO Edgeport Driver tables
 *
 *	Copyright (C) 2001
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 * 
 */

#ifndef IO_TABLES_H
#define IO_TABLES_H

static struct usb_device_id edgeport_2port_id_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_421) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_21) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2_DIN) },
	{ }
};

static struct usb_device_id edgeport_4port_id_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_RAPIDPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4T) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_MT4X56USB) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_4_DIN) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_22I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_412_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_COMPATIBLE) },
	{ }
};

static struct usb_device_id edgeport_8port_id_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8R) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8RR) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_412_8) },
	{ }
};

/* Devices that this driver supports */
static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_RAPIDPORT_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4T) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_MT4X56USB) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_2) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_2I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_421) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_21) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_8) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_2_DIN) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_4_DIN) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_22I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_412_4) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_COMPATIBLE) },
	{ USB_DEVICE(USB_VENDOR_ID_ION,	ION_DEVICE_ID_EDGEPORT_8I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8R) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8RR) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_412_8) },
	{ }							/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_serial_driver edgeport_2port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "edgeport_2",
	},
	.description		= "Edgeport 2 port adapter",
	.id_table		= edgeport_2port_id_table,
	.num_interrupt_in	= 1,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.num_ports		= 2,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.shutdown		= edge_shutdown,
	.ioctl			= edge_ioctl,
	.set_termios		= edge_set_termios,
	.tiocmget		= edge_tiocmget,
	.tiocmset		= edge_tiocmset,
	.write			= edge_write,
	.write_room		= edge_write_room,
	.chars_in_buffer	= edge_chars_in_buffer,
	.break_ctl		= edge_break,
	.read_int_callback	= edge_interrupt_callback,
	.read_bulk_callback	= edge_bulk_in_callback,
	.write_bulk_callback	= edge_bulk_out_data_callback,
};

static struct usb_serial_driver edgeport_4port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "edgeport_4",
	},
	.description		= "Edgeport 4 port adapter",
	.id_table		= edgeport_4port_id_table,
	.num_interrupt_in	= 1,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.num_ports		= 4,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.shutdown		= edge_shutdown,
	.ioctl			= edge_ioctl,
	.set_termios		= edge_set_termios,
	.tiocmget		= edge_tiocmget,
	.tiocmset		= edge_tiocmset,
	.write			= edge_write,
	.write_room		= edge_write_room,
	.chars_in_buffer	= edge_chars_in_buffer,
	.break_ctl		= edge_break,
	.read_int_callback	= edge_interrupt_callback,
	.read_bulk_callback	= edge_bulk_in_callback,
	.write_bulk_callback	= edge_bulk_out_data_callback,
};

static struct usb_serial_driver edgeport_8port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "edgeport_8",
	},
	.description		= "Edgeport 8 port adapter",
	.id_table		= edgeport_8port_id_table,
	.num_interrupt_in	= 1,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.num_ports		= 8,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.shutdown		= edge_shutdown,
	.ioctl			= edge_ioctl,
	.set_termios		= edge_set_termios,
	.tiocmget		= edge_tiocmget,
	.tiocmset		= edge_tiocmset,
	.write			= edge_write,
	.write_room		= edge_write_room,
	.chars_in_buffer	= edge_chars_in_buffer,
	.break_ctl		= edge_break,
	.read_int_callback	= edge_interrupt_callback,
	.read_bulk_callback	= edge_bulk_in_callback,
	.write_bulk_callback	= edge_bulk_out_data_callback,
};

#endif

