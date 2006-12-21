/*
 * Funsoft Serial USB driver
 *
 * Copyright (C) 2006 Greg Kroah-Hartman <gregkh@suse.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <asm/uaccess.h>

static int debug;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x1404, 0xcddc) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static int funsoft_ioctl(struct usb_serial_port *port, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct termios t;

	dbg("%s - port %d, cmd 0x%04x", __FUNCTION__, port->number, cmd);

	if (cmd == TCSETSF) {
		if (user_termios_to_kernel_termios(&t, (void __user *)arg))
			return -EFAULT;

		dbg("%s - iflag:%x oflag:%x cflag:%x lflag:%x", __FUNCTION__,
		    t.c_iflag, t.c_oflag, t.c_cflag, t.c_lflag);

		if (!(t.c_lflag & ICANON))
			return -EINVAL;
	}
	return -ENOIOCTLCMD;
}

static struct usb_driver funsoft_driver = {
	.name =		"funsoft",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id = 	1,
};

static struct usb_serial_driver funsoft_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"funsoft",
	},
	.id_table =		id_table,
	.num_interrupt_in =	NUM_DONT_CARE,
	.num_bulk_in =		NUM_DONT_CARE,
	.num_bulk_out =		NUM_DONT_CARE,
	.num_ports =		1,
	.ioctl =		funsoft_ioctl,
};

static int __init funsoft_init(void)
{
	int retval;

	retval = usb_serial_register(&funsoft_device);
	if (retval)
		return retval;
	retval = usb_register(&funsoft_driver);
	if (retval)
		usb_serial_deregister(&funsoft_device);
	return retval;
}

static void __exit funsoft_exit(void)
{
	usb_deregister(&funsoft_driver);
	usb_serial_deregister(&funsoft_device);
}

module_init(funsoft_init);
module_exit(funsoft_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
