// SPDX-License-Identifier: GPL-2.0+
/*
 * USB IR Dongle driver
 *
 *	Copyright (C) 2001-2002	Greg Kroah-Hartman (greg@kroah.com)
 *	Copyright (C) 2002	Gary Brubaker (xavyer@ix.netcom.com)
 *	Copyright (C) 2010	Johan Hovold (jhovold@gmail.com)
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
 * See Documentation/usb/usb-serial.rst for more information on using this
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

#define DRIVER_AUTHOR "Greg Kroah-Hartman <greg@kroah.com>, Johan Hovold <jhovold@gmail.com>"
#define DRIVER_DESC "USB IR Dongle driver"

/* if overridden by the user, then use their value for the size of the read and
 * write urbs */
static int buffer_size;

/* if overridden by the user, then use the specified number of XBOFs */
static int xbof = -1;

static int  ir_startup (struct usb_serial *serial);
static int ir_write(struct tty_struct *tty, struct usb_serial_port *port,
		const unsigned char *buf, int count);
static unsigned int ir_write_room(struct tty_struct *tty);
static void ir_write_bulk_callback(struct urb *urb);
static void ir_process_read_urb(struct urb *urb);
static void ir_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   const struct ktermios *old_termios);

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

static struct usb_serial_driver ir_device = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "ir-usb",
	},
	.description		= "IR Dongle",
	.id_table		= ir_id_table,
	.num_ports		= 1,
	.num_bulk_in		= 1,
	.num_bulk_out		= 1,
	.set_termios		= ir_set_termios,
	.attach			= ir_startup,
	.write			= ir_write,
	.write_room		= ir_write_room,
	.write_bulk_callback	= ir_write_bulk_callback,
	.process_read_urb	= ir_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ir_device, NULL
};

static inline void irda_usb_dump_class_desc(struct usb_serial *serial,
					    struct usb_irda_cs_descriptor *desc)
{
	struct device *dev = &serial->dev->dev;

	dev_dbg(dev, "bLength=%x\n", desc->bLength);
	dev_dbg(dev, "bDescriptorType=%x\n", desc->bDescriptorType);
	dev_dbg(dev, "bcdSpecRevision=%x\n", __le16_to_cpu(desc->bcdSpecRevision));
	dev_dbg(dev, "bmDataSize=%x\n", desc->bmDataSize);
	dev_dbg(dev, "bmWindowSize=%x\n", desc->bmWindowSize);
	dev_dbg(dev, "bmMinTurnaroundTime=%d\n", desc->bmMinTurnaroundTime);
	dev_dbg(dev, "wBaudRate=%x\n", __le16_to_cpu(desc->wBaudRate));
	dev_dbg(dev, "bmAdditionalBOFs=%x\n", desc->bmAdditionalBOFs);
	dev_dbg(dev, "bIrdaRateSniff=%x\n", desc->bIrdaRateSniff);
	dev_dbg(dev, "bMaxUnicastList=%x\n", desc->bMaxUnicastList);
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
irda_usb_find_class_desc(struct usb_serial *serial, unsigned int ifnum)
{
	struct usb_device *dev = serial->dev;
	struct usb_irda_cs_descriptor *desc;
	int ret;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_CS_IRDA_GET_CLASS_DESC,
			USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, ifnum, desc, sizeof(*desc), 1000);

	dev_dbg(&serial->dev->dev, "%s -  ret=%d\n", __func__, ret);
	if (ret < (int)sizeof(*desc)) {
		dev_dbg(&serial->dev->dev,
			"%s - class descriptor read %s (%d)\n", __func__,
			(ret < 0) ? "failed" : "too short", ret);
		goto error;
	}
	if (desc->bDescriptorType != USB_DT_CS_IRDA) {
		dev_dbg(&serial->dev->dev, "%s - bad class descriptor type\n",
			__func__);
		goto error;
	}

	irda_usb_dump_class_desc(serial, desc);
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
	int rates;

	irda_desc = irda_usb_find_class_desc(serial, 0);
	if (!irda_desc) {
		dev_err(&serial->dev->dev,
			"IRDA class descriptor not found, device not bound\n");
		return -ENODEV;
	}

	rates = le16_to_cpu(irda_desc->wBaudRate);

	dev_dbg(&serial->dev->dev,
		"%s - Baud rates supported:%s%s%s%s%s%s%s%s%s\n",
		__func__,
		(rates & USB_IRDA_BR_2400) ? " 2400" : "",
		(rates & USB_IRDA_BR_9600) ? " 9600" : "",
		(rates & USB_IRDA_BR_19200) ? " 19200" : "",
		(rates & USB_IRDA_BR_38400) ? " 38400" : "",
		(rates & USB_IRDA_BR_57600) ? " 57600" : "",
		(rates & USB_IRDA_BR_115200) ? " 115200" : "",
		(rates & USB_IRDA_BR_576000) ? " 576000" : "",
		(rates & USB_IRDA_BR_1152000) ? " 1152000" : "",
		(rates & USB_IRDA_BR_4000000) ? " 4000000" : "");

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

static int ir_write(struct tty_struct *tty, struct usb_serial_port *port,
		const unsigned char *buf, int count)
{
	struct urb *urb = NULL;
	unsigned long flags;
	int ret;

	if (port->bulk_out_size == 0)
		return -EINVAL;

	if (count == 0)
		return 0;

	count = min(count, port->bulk_out_size - 1);

	spin_lock_irqsave(&port->lock, flags);
	if (__test_and_clear_bit(0, &port->write_urbs_free)) {
		urb = port->write_urbs[0];
		port->tx_bytes += count;
	}
	spin_unlock_irqrestore(&port->lock, flags);

	if (!urb)
		return 0;

	/*
	 * The first byte of the packet we send to the device contains an
	 * outbound header which indicates an additional number of BOFs and
	 * a baud rate change.
	 *
	 * See section 5.4.2.2 of the USB IrDA spec.
	 */
	*(u8 *)urb->transfer_buffer = ir_xbof | ir_baud;

	memcpy(urb->transfer_buffer + 1, buf, count);

	urb->transfer_buffer_length = count + 1;
	urb->transfer_flags = URB_ZERO_PACKET;

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		dev_err(&port->dev, "failed to submit write urb: %d\n", ret);

		spin_lock_irqsave(&port->lock, flags);
		__set_bit(0, &port->write_urbs_free);
		port->tx_bytes -= count;
		spin_unlock_irqrestore(&port->lock, flags);

		return ret;
	}

	return count;
}

static void ir_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int status = urb->status;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	__set_bit(0, &port->write_urbs_free);
	port->tx_bytes -= urb->transfer_buffer_length - 1;
	spin_unlock_irqrestore(&port->lock, flags);

	switch (status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		dev_dbg(&port->dev, "write urb stopped: %d\n", status);
		return;
	case -EPIPE:
		dev_err(&port->dev, "write urb stopped: %d\n", status);
		return;
	default:
		dev_err(&port->dev, "nonzero write-urb status: %d\n", status);
		break;
	}

	usb_serial_port_softint(port);
}

static unsigned int ir_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int count = 0;

	if (port->bulk_out_size == 0)
		return 0;

	if (test_bit(0, &port->write_urbs_free))
		count = port->bulk_out_size - 1;

	return count;
}

static void ir_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;

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

	tty_insert_flip_string(&port->port, data + 1, urb->actual_length - 1);
	tty_flip_buffer_push(&port->port);
}

static void ir_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   const struct ktermios *old_termios)
{
	struct usb_device *udev = port->serial->dev;
	unsigned char *transfer_buffer;
	int actual_length;
	speed_t baud;
	int ir_baud;
	int ret;

	baud = tty_get_baud_rate(tty);

	/*
	 * FIXME, we should compare the baud request against the
	 * capability stated in the IR header that we got in the
	 * startup function.
	 */

	switch (baud) {
	case 2400:
		ir_baud = USB_IRDA_LS_2400;
		break;
	case 9600:
		ir_baud = USB_IRDA_LS_9600;
		break;
	case 19200:
		ir_baud = USB_IRDA_LS_19200;
		break;
	case 38400:
		ir_baud = USB_IRDA_LS_38400;
		break;
	case 57600:
		ir_baud = USB_IRDA_LS_57600;
		break;
	case 115200:
		ir_baud = USB_IRDA_LS_115200;
		break;
	case 576000:
		ir_baud = USB_IRDA_LS_576000;
		break;
	case 1152000:
		ir_baud = USB_IRDA_LS_1152000;
		break;
	case 4000000:
		ir_baud = USB_IRDA_LS_4000000;
		break;
	default:
		ir_baud = USB_IRDA_LS_9600;
		baud = 9600;
	}

	if (xbof == -1)
		ir_xbof = ir_xbof_change(ir_add_bof);
	else
		ir_xbof = ir_xbof_change(xbof) ;

	/* Only speed changes are supported */
	tty_termios_copy_hw(&tty->termios, old_termios);
	tty_encode_baud_rate(tty, baud, baud);

	/*
	 * send the baud change out on an "empty" data packet
	 */
	transfer_buffer = kmalloc(1, GFP_KERNEL);
	if (!transfer_buffer)
		return;

	*transfer_buffer = ir_xbof | ir_baud;

	ret = usb_bulk_msg(udev,
			usb_sndbulkpipe(udev, port->bulk_out_endpointAddress),
			transfer_buffer, 1, &actual_length, 5000);
	if (ret || actual_length != 1) {
		if (!ret)
			ret = -EIO;
		dev_err(&port->dev, "failed to change line speed: %d\n", ret);
	}

	kfree(transfer_buffer);
}

static int __init ir_init(void)
{
	if (buffer_size) {
		ir_device.bulk_in_size = buffer_size;
		ir_device.bulk_out_size = buffer_size;
	}

	return usb_serial_register_drivers(serial_drivers, KBUILD_MODNAME, ir_id_table);
}

static void __exit ir_exit(void)
{
	usb_serial_deregister_drivers(serial_drivers);
}


module_init(ir_init);
module_exit(ir_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(xbof, int, 0);
MODULE_PARM_DESC(xbof, "Force specific number of XBOFs");
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Size of the transfer buffers");

