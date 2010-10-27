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

static const struct usb_device_id edgeport_2port_id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_421) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_21) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_2_DIN) },
	{ }
};

static const struct usb_device_id edgeport_4port_id_table[] = {
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

static const struct usb_device_id edgeport_8port_id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8I) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8R) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_8RR) },
	{ USB_DEVICE(USB_VENDOR_ID_ION, ION_DEVICE_ID_EDGEPORT_412_8) },
	{ }
};

static const struct usb_device_id Epic_port_id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0202) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0203) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0310) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0311) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0312) },
	{ USB_DEVICE(USB_VENDOR_ID_AXIOHM, AXIOHM_DEVICE_ID_EPIC_A758) },
	{ USB_DEVICE(USB_VENDOR_ID_AXIOHM, AXIOHM_DEVICE_ID_EPIC_A794) },
	{ USB_DEVICE(USB_VENDOR_ID_AXIOHM, AXIOHM_DEVICE_ID_EPIC_A225) },
	{ }
};

/* Devices that this driver supports */
static const struct usb_device_id id_table_combined[] = {
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
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0202) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0203) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0310) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0311) },
	{ USB_DEVICE(USB_VENDOR_ID_NCR, NCR_DEVICE_ID_EPIC_0312) },
	{ USB_DEVICE(USB_VENDOR_ID_AXIOHM, AXIOHM_DEVICE_ID_EPIC_A758) },
	{ USB_DEVICE(USB_VENDOR_ID_AXIOHM, AXIOHM_DEVICE_ID_EPIC_A794) },
	{ USB_DEVICE(USB_VENDOR_ID_AXIOHM, AXIOHM_DEVICE_ID_EPIC_A225) },
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

static struct usb_driver io_driver = {
	.name =		"io_edgeport",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver edgeport_2port_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "edgeport_2",
	},
	.description		= "Edgeport 2 port adapter",
	.usb_driver		= &io_driver,
	.id_table		= edgeport_2port_id_table,
	.num_ports		= 2,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.disconnect		= edge_disconnect,
	.release		= edge_release,
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
	.usb_driver		= &io_driver,
	.id_table		= edgeport_4port_id_table,
	.num_ports		= 4,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.disconnect		= edge_disconnect,
	.release		= edge_release,
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
	.usb_driver		= &io_driver,
	.id_table		= edgeport_8port_id_table,
	.num_ports		= 8,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.disconnect		= edge_disconnect,
	.release		= edge_release,
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

static struct usb_serial_driver epic_device = {
	.driver = {
		.owner		= THIS_MODULE,
		.name		= "epic",
	},
	.description		= "EPiC device",
	.id_table		= Epic_port_id_table,
	.num_ports		= 1,
	.open			= edge_open,
	.close			= edge_close,
	.throttle		= edge_throttle,
	.unthrottle		= edge_unthrottle,
	.attach			= edge_startup,
	.disconnect		= edge_disconnect,
	.release		= edge_release,
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

