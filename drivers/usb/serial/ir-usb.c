/*
 * USB IR Dongle driver
 *
 *	Copyright (C) 2001-2002	Greg Kroah-Hartman (greg@kroah.com)
 *	Copyright (C) 2002	Gary Brubaker (xavyer@ix.netcom.com)
 *	Copyright (C) 2010	Johan Hovold (jhovold@gmail.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * This driver allows a USB IrDA device to be used as a "dumb" serial device.
 * This can be useful if you do not have access to a full IrDA stack on the
 * other side of the connection.  If you do have an IrDA stack on both devices,
 * please use the usb-irda driver, as it contains the proper error checking and
 * other goodness of a full IrDA stack.
 *
 * Portions of this driver were taken from drivers/net/irda/irda-usb.c, which
 * was written by Roman Weissgaerber <weissg@vienna.at>, Dag Brattli
 * <dag@brattli.net>, and Jean Tourrilhes <jt@hpl.hp.com>
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/usb/irda.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.5"
#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Johan Hovold <jhovold@gmail.com>"
#define DRIVER_DESC "USB IR Dongle driver"

static bool debug;

/* if overridden by the user, then use their value for the size of the read and
 * write urbs */
static int buffer_size;

/* if overridden by the user, then use the specified number of XBOFs */
static int xbof = -1;

static int  ir_startup (struct usb_serial *serial);
static int  ir_open(struct tty_struct *tty, struct usb_serial_port *port);
static int ir_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size);
static void ir_process_read_urb(struct urb *urb);
static void ir_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios);

/* Not that this lot means you can only have one per system */
static u8 ir_baud;
static u8 ir_xbof;
static u8 ir_add_bof;

static const struct usb_device_id ir_id_table[] = {
	{ USB_DEVICE(0x050f, 0x0180) },		/* KC Technology, KC-180 */
	{ USB_DEVICE(0x08e9, 0x0100) },		/* XTNDAccess */
	{ USB_DEVICE(0x09c4, 0x0011) },		/* ACTiSys ACT-IR2000U */
	{ USB_INTERFACE_INFO(USB_CLASS_APP_SPEC, USB_SUBCLASS_IRDA, 0) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ir_id_table);

static struct usb_driver ir_driver = {
	.name		= "ir-usb",
	.probe		= usb_serial_probe,
	.disconnect	= usb_serial_disconnect,
	.id_table	= ir_id_table,
	.no_dynamic_id	= 1,
};

static struct usb_serial_driver ir_device = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "ir-usb",
	},
	.description		= "IR Dongle",
	.usb_driver		= &ir_driver,
	.id_table		= ir_id_table,
	.num_ports		= 1,
	.set_termios		= ir_set_termios,
	.attach			= ir_startup,
	.open			= ir_open,
	.prepare_write_buffer	= ir_prepare_write_buffer,
	.process_read_urb	= ir_process_read_urb,
};

static inline void irda_usb_dump_class_desc(struct usb_irda_cs_descriptor *desc)
{
	dbg("bLength=%x", desc->bLength);
	dbg("bDescriptorType=%x", desc->bDescriptorType);
	dbg("bcdSpecRevision=%x", __le16_to_cpu(desc->bcdSpecRevision));
	dbg("bmDataSize=%x", desc->bmDataSize);
	dbg("bmWindowSize=%x", desc->bmWindowSize);
	dbg("bmMinTurnaroundTime=%d", desc->bmMinTurnaroundTime);
	dbg("wBaudRate=%x", __le16_to_cpu(desc->wBaudRate));
	dbg("bmAdditionalBOFs=%x", desc->bmAdditionalBOFs);
	dbg("bIrdaRateSniff=%x", desc->bIrdaRateSniff);
	dbg("bMaxUnicastList=%x", desc->bMaxUnicastList);
}

/*------------------------------------------------------------------*/
/*
 * Function irda_usb_find_class_desc(dev, ifnum)
 *
 *    Returns instance of IrDA class descriptor, or NULL if not found
 *
 * The class descriptor is some extra info that IrDA USB devices will
 * offer to us, describing their IrDA characteristics. We will use that in
 * irda_usb_init_qos()
 *
 * Based on the same function in drivers/net/irda/irda-usb.c
 */
static struct usb_irda_cs_descriptor *
irda_usb_find_class_desc(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_irda_cs_descriptor *desc;
	int ret;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_CS_IRDA_GET_CLASS_DESC,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, ifnum, desc, sizeof(*desc), 1000);

	dbg("%s -  ret=%d", __func__, ret);
	if (ret < sizeof(*desc)) {
		dbg("%s - class descriptor read %s (%d)",
				__func__,
				(ret < 0) ? "failed" : "too short",
				ret);
		goto error;
	}
	if (desc->bDescriptorType != USB_DT_CS_IRDA) {
		dbg("%s - bad class descriptor type", __func__);
		goto error;
	}

	irda_usb_dump_class_desc(desc);
	return desc;

error:
	kfree(desc);
	return NULL;
}

static u8 ir_xbof_change(u8 xbof)
{
	u8 result;

	/* reference irda-usb.c */
	switch (xbof) {
	case 48:
		result = 0x10;
		break;
	case 28:
	case 24:
		result = 0x20;
		break;
	default:
	case 12:
		result = 0x30;
		break;
	case  5:
	case  6:
		result = 0x40;
		break;
	case  3:
		result = 0x50;
		break;
	case  2:
		result = 0x60;
		break;
	case  1:
		result = 0x70;
		break;
	case  0:
		result = 0x80;
		break;
	}

	return(result);
}

static int ir_startup(struct usb_serial *serial)
{
	struct usb_irda_cs_descriptor *irda_desc;

	irda_desc = irda_usb_find_class_desc(serial->dev, 0);
	if (!irda_desc) {
		dev_err(&serial->dev->dev,
			"IRDA class descriptor not found, device not bound\n");
		return -ENODEV;
	}

	dbg("%s - Baud rates supported:%s%s%s%s%s%s%s%s%s",
		__func__,
		(irda_desc->wBaudRate & USB_IRDA_BR_2400) ? " 2400" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_9600) ? " 9600" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_19200) ? " 19200" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_38400) ? " 38400" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_57600) ? " 57600" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_115200) ? " 115200" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_576000) ? " 576000" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_1152000) ? " 1152000" : "",
		(irda_desc->wBaudRate & USB_IRDA_BR_4000000) ? " 4000000" : "");

	switch (irda_desc->bmAdditionalBOFs) {
	case USB_IRDA_AB_48:
		ir_add_bof = 48;
		break;
	case USB_IRDA_AB_24:
		ir_add_bof = 24;
		break;
	case USB_IRDA_AB_12:
		ir_add_bof = 12;
		break;
	case USB_IRDA_AB_6:
		ir_add_bof = 6;
		break;
	case USB_IRDA_AB_3:
		ir_add_bof = 3;
		break;
	case USB_IRDA_AB_2:
		ir_add_bof = 2;
		break;
	case USB_IRDA_AB_1:
		ir_add_bof = 1;
		break;
	case USB_IRDA_AB_0:
		ir_add_bof = 0;
		break;
	default:
		break;
	}

	kfree(irda_desc);

	return 0;
}

static int ir_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int i;

	dbg("%s - port %d", __func__, port->number);

	for (i = 0; i < ARRAY_SIZE(port->write_urbs); ++i)
		port->write_urbs[i]->transfer_flags = URB_ZERO_PACKET;

	/* Start reading from the device */
	return usb_serial_generic_open(tty, port);
}

static int ir_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size)
{
	unsigned char *buf = dest;
	int count;

	/*
	 * The first byte of the packet we send to the device contains an
	 * inbound header which indicates an additional number of BOFs and
	 * a baud rate change.
	 *
	 * See section 5.4.2.2 of the USB IrDA spec.
	 */
	*buf = ir_xbof | ir_baud;

	count = kfifo_out_locked(&port->write_fifo, buf + 1, size - 1,
								&port->lock);
	return count + 1;
}

static void ir_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;

	if (!urb->actual_length)
		return;
	/*
	 * The first byte of the packet we get from the device
	 * contains a busy indicator and baud rate change.
	 * See section 5.4.1.2 of the USB IrDA spec.
	 */
	if (*data & 0x0f)
		ir_baud = *data & 0x0f;

	if (urb->actual_length == 1)
		return;

	tty = tty_port_tty_get(&port->port);
	if (!tty)
		return;
	tty_insert_flip_string(tty, data + 1, urb->actual_length - 1);
	tty_flip_buffer_push(tty);
	tty_kref_put(tty);
}

static void ir_set_termios_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	kfree(urb->transfer_buffer);

	if (status)
		dbg("%s - non-zero urb status: %d", __func__, status);
}

static void ir_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct urb *urb;
	unsigned char *transfer_buffer;
	int result;
	speed_t baud;
	int ir_baud;

	dbg("%s - port %d", __func__, port->number);

	baud = tty_get_baud_rate(tty);

	/*
	 * FIXME, we should compare the baud request against the
	 * capability stated in the IR header that we got in the
	 * startup function.
	 */

	switch (baud) {
	case 2400:
		ir_baud = USB_IRDA_BR_2400;
		break;
	case 9600:
		ir_baud = USB_IRDA_BR_9600;
		break;
	case 19200:
		ir_baud = USB_IRDA_BR_19200;
		break;
	case 38400:
		ir_baud = USB_IRDA_BR_38400;
		break;
	case 57600:
		ir_baud = USB_IRDA_BR_57600;
		break;
	case 115200:
		ir_baud = USB_IRDA_BR_115200;
		break;
	case 576000:
		ir_baud = USB_IRDA_BR_576000;
		break;
	case 1152000:
		ir_baud = USB_IRDA_BR_1152000;
		break;
	case 4000000:
		ir_baud = USB_IRDA_BR_4000000;
		break;
	default:
		ir_baud = USB_IRDA_BR_9600;
		baud = 9600;
	}

	if (xbof == -1)
		ir_xbof = ir_xbof_change(ir_add_bof);
	else
		ir_xbof = ir_xbof_change(xbof) ;

	/* Only speed changes are supported */
	tty_termios_copy_hw(tty->termios, old_termios);
	tty_encode_baud_rate(tty, baud, baud);

	/*
	 * send the baud change out on an "empty" data packet
	 */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		dev_err(&port->dev, "%s - no more urbs\n", __func__);
		return;
	}
	transfer_buffer = kmalloc(1, GFP_KERNEL);
	if (!transfer_buffer) {
		dev_err(&port->dev, "%s - out of memory\n", __func__);
		goto err_buf;
	}

	*transfer_buffer = ir_xbof | ir_baud;

	usb_fill_bulk_urb(
		urb,
		port->serial->dev,
		usb_sndbulkpipe(port->serial->dev,
			port->bulk_out_endpointAddress),
		transfer_buffer,
		1,
		ir_set_termios_callback,
		port);

	urb->transfer_flags = URB_ZERO_PACKET;

	result = usb_submit_urb(urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "%s - failed to submit urb: %d\n",
							__func__, result);
		goto err_subm;
	}

	usb_free_urb(urb);

	return;
err_subm:
	kfree(transfer_buffer);
err_buf:
	usb_free_urb(urb);
}

static int __init ir_init(void)
{
	int retval;

	if (buffer_size) {
		ir_device.bulk_in_size = buffer_size;
		ir_device.bulk_out_size = buffer_size;
	}

	retval = usb_serial_register(&ir_device);
	if (retval)
		goto failed_usb_serial_register;

	retval = usb_register(&ir_driver);
	if (retval)
		goto failed_usb_register;

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");

	return 0;

failed_usb_register:
	usb_serial_deregister(&ir_device);

failed_usb_serial_register:
	return retval;
}

static void __exit ir_exit(void)
{
	usb_deregister(&ir_driver);
	usb_serial_deregister(&ir_device);
}


module_init(ir_init);
module_exit(ir_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(xbof, int, 0);
MODULE_PARM_DESC(xbof, "Force specific number of XBOFs");
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Size of the transfer buffers");

