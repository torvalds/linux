/*
 * ZTE_EV USB serial driver
 *
 * Copyright (C) 2012 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
 * Copyright (C) 2012 Linux Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver is based on code found in a ZTE_ENV patch that modified
 * the usb-serial generic driver.  Comments were left in that I think
 * show the commands used to talk to the device, but I am not sure.
 */
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

#define  MAX_SETUP_DATA_SIZE	32

static void debug_data(struct device *dev, const char *function, int len,
		       const unsigned char *data, int result)
{
	dev_dbg(dev, "result = %d\n", result);
	if (result == len)
		dev_dbg(dev, "%s - length = %d, data = %*ph\n", function,
			len, len, data);
}

static int zte_ev_usb_serial_open(struct tty_struct *tty,
				  struct usb_serial_port *port)
{
	struct usb_device *udev = port->serial->dev;
	struct device *dev = &port->dev;
	int result = 0;
	int len;
	unsigned char *buf;

	buf = kmalloc(MAX_SETUP_DATA_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* send 1st ctl cmd(CTL    21 22 01 00  00 00 00 00) */
	len = 0;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x22, 0x21,
				 0x0001, 0x0000, NULL, len,
				 USB_CTRL_GET_TIMEOUT);
	dev_dbg(dev, "result = %d\n", result);

	/* send 2st cmd and receive data */
	/*
	 * 16.0  CTL    a1 21 00 00  00 00 07 00   CLASS              25.1.0(5)
	 * 16.0  DI     00 96 00 00  00 00 08
	 */
	len = 0x0007;
	result = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 0x21, 0xa1,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 3rd cmd */
	/*
	 * 16.0 CTL    21 20 00 00  00 00 07 00    CLASS                30.1.0
	 * 16.0 DO     80 25 00 00  00 00 08       .%.....              30.2.0
	 */
	len = 0x0007;
	buf[0] = 0x80;
	buf[1] = 0x25;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x08;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x20, 0x21,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 4th cmd */
	/*
	 * 16.0 CTL    21 22 03 00  00 00 00 00
	 */
	len = 0;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x22, 0x21,
				 0x0003, 0x0000, NULL, len,
				 USB_CTRL_GET_TIMEOUT);
	dev_dbg(dev, "result = %d\n", result);

	/* send 5th cmd */
	/*
	 * 16.0  CTL    a1 21 00 00  00 00 07 00   CLASS               33.1.0
	 * 16.0  DI     80 25 00 00  00 00 08
	 */
	len = 0x0007;
	result = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 0x21, 0xa1,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 6th cmd */
	/*
	 * 16.0  CTL    21 20 00 00  00 00 07 00    CLASS               34.1.0
	 * 16.0  DO     80 25 00 00  00 00 08
	 */
	len = 0x0007;
	buf[0] = 0x80;
	buf[1] = 0x25;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x08;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x20, 0x21,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);
	kfree(buf);

	return usb_serial_generic_open(tty, port);
}

/*
 *       CTL    21 22 02 00  00 00 00 00         CLASS               338.1.0
 *
 * 16.1  DI     a1 20 00 00  00 00 02 00  02 00  . ........          340.1.0
 * 16.0  CTL    21 22 03 00  00 00 00 00         CLASS               341.1.0
 *
 * 16.0  CTL    a1 21 00 00  00 00 07 00         CLASS               346.1.0(3)
 * 16.0  DI     00 08 07 00  00 00 08            .......             346.2.0
 *
 * 16.0  CTL    21 20 00 00  00 00 07 00         CLASS               349.1.0
 * 16.0  DO     00 c2 01 00  00 00 08            .......             349.2.0
 *
 * 16.0  CTL    21 22 03 00  00 00 00 00         CLASS               350.1.0(2)
 *
 * 16.0  CTL    a1 21 00 00  00 00 07 00         CLASS               352.1.0
 * 16.0  DI     00 c2 01 00  00 00 08            .......             352.2.0
 *
 * 16.1  DI     a1 20 00 00  00 00 02 00  02 00  . ........          353.1.0
 *
 * 16.0  CTL    21 20 00 00  00 00 07 00         CLASS               354.1.0
 * 16.0  DO     00 c2 01 00  00 00 08            .......             354.2.0
 *
 * 16.0  CTL    21 22 03 00  00 00 00 00
*/

static void zte_ev_usb_serial_close(struct usb_serial_port *port)
{
	struct usb_device *udev = port->serial->dev;
	struct device *dev = &port->dev;
	int result = 0;
	int len;
	unsigned char *buf;

	buf = kmalloc(MAX_SETUP_DATA_SIZE, GFP_KERNEL);
	if (!buf)
		return;

	/* send 1st ctl cmd(CTL    21 22 02 00  00 00 00 00) */
	len = 0;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x22, 0x21,
				 0x0002, 0x0000, NULL, len,
				 USB_CTRL_GET_TIMEOUT);
	dev_dbg(dev, "result = %d\n", result);

	/* send 2st ctl cmd(CTL    21 22 03 00  00 00 00 00 ) */
	len = 0;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x22, 0x21,
				 0x0003, 0x0000, NULL, len,
				 USB_CTRL_GET_TIMEOUT);
	dev_dbg(dev, "result = %d\n", result);

	/* send  3st cmd and recieve data */
	/*
	 * 16.0  CTL    a1 21 00 00  00 00 07 00      CLASS         25.1.0(5)
	 * 16.0  DI     00 08 07 00  00 00 08
	 */
	len = 0x0007;
	result = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 0x21, 0xa1,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 4th cmd */
	/*
	 * 16.0 CTL    21 20 00 00  00 00 07 00      CLASS            30.1.0
	 * 16.0  DO    00 c2 01 00  00 00 08         .%.....          30.2.0
	 */
	len = 0x0007;
	buf[0] = 0x00;
	buf[1] = 0xc2;
	buf[2] = 0x01;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x08;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x20, 0x21,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 5th cmd */
	/*
	 * 16.0 CTL    21 22 03 00  00 00 00 00
	 */
	len = 0;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x22, 0x21,
				 0x0003, 0x0000, NULL, len,
				 USB_CTRL_GET_TIMEOUT);
	dev_dbg(dev, "result = %d\n", result);

	/* send 6th cmd */
	/*
	 * 16.0  CTL    a1 21 00 00  00 00 07 00        CLASS          33.1.0
	 * 16.0  DI     00 c2 01 00  00 00 08
	 */
	len = 0x0007;
	result = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				 0x21, 0xa1,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 7th cmd */
	/*
	 * 16.0  CTL    21 20 00 00  00 00 07 00  CLASS               354.1.0
	 * 16.0  DO     00 c2 01 00  00 00 08     .......             354.2.0
	 */
	len = 0x0007;
	buf[0] = 0x00;
	buf[1] = 0xc2;
	buf[2] = 0x01;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x08;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x20, 0x21,
				 0x0000, 0x0000, buf, len,
				 USB_CTRL_GET_TIMEOUT);
	debug_data(dev, __func__, len, buf, result);

	/* send 8th cmd */
	/*
	 * 16.0 CTL    21 22 03 00  00 00 00 00
	 */
	len = 0;
	result = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				 0x22, 0x21,
				 0x0003, 0x0000, NULL, len,
				 USB_CTRL_GET_TIMEOUT);
	dev_dbg(dev, "result = %d\n", result);

	kfree(buf);

	usb_serial_generic_close(port);
}

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x19d2, 0xffec) },
	{ USB_DEVICE(0x19d2, 0xffee) },
	{ USB_DEVICE(0x19d2, 0xfff6) },
	{ USB_DEVICE(0x19d2, 0xfff7) },
	{ USB_DEVICE(0x19d2, 0xfff8) },
	{ USB_DEVICE(0x19d2, 0xfff9) },
	{ USB_DEVICE(0x19d2, 0xfffb) },
	{ USB_DEVICE(0x19d2, 0xfffc) },
	/* MG880 */
	{ USB_DEVICE(0x19d2, 0xfffd) },
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver zio_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"zte_ev",
	},
	.id_table =		id_table,
	.num_ports =		1,
	.open =			zte_ev_usb_serial_open,
	.close =		zte_ev_usb_serial_close,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&zio_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);
MODULE_LICENSE("GPL v2");
