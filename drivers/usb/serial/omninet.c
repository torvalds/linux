/*
 * USB ZyXEL omni.net LCD PLUS driver
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 * Please report both successes and troubles to the author at omninet@kroah.com
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DRIVER_AUTHOR "Alessandro Zummo"
#define DRIVER_DESC "USB ZyXEL omni.net LCD PLUS Driver"

#define ZYXEL_VENDOR_ID		0x0586
#define ZYXEL_OMNINET_ID	0x1000
/* This one seems to be a re-branded ZyXEL device */
#define BT_IGNITIONPRO_ID	0x2000

/* function prototypes */
static int  omninet_open(struct tty_struct *tty, struct usb_serial_port *port);
static void omninet_close(struct usb_serial_port *port);
static void omninet_read_bulk_callback(struct urb *urb);
static void omninet_write_bulk_callback(struct urb *urb);
static int  omninet_write(struct tty_struct *tty, struct usb_serial_port *port,
				const unsigned char *buf, int count);
static int  omninet_write_room(struct tty_struct *tty);
static void omninet_disconnect(struct usb_serial *serial);
static int omninet_port_probe(struct usb_serial_port *port);
static int omninet_port_remove(struct usb_serial_port *port);

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(ZYXEL_VENDOR_ID, ZYXEL_OMNINET_ID) },
	{ USB_DEVICE(ZYXEL_VENDOR_ID, BT_IGNITIONPRO_ID) },
	{ }						/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver zyxel_omninet_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"omninet",
	},
	.description =		"ZyXEL - omni.net lcd plus usb",
	.id_table =		id_table,
	.num_ports =		1,
	.port_probe =		omninet_port_probe,
	.port_remove =		omninet_port_remove,
	.open =			omninet_open,
	.close =		omninet_close,
	.write =		omninet_write,
	.write_room =		omninet_write_room,
	.read_bulk_callback =	omninet_read_bulk_callback,
	.write_bulk_callback =	omninet_write_bulk_callback,
	.disconnect =		omninet_disconnect,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&zyxel_omninet_device, NULL
};


/* The protocol.
 *
 * The omni.net always exchange 64 bytes of data with the host. The first
 * four bytes are the control header, you can see it in the above structure.
 *
 * oh_seq is a sequence number. Don't know if/how it's used.
 * oh_len is the length of the data bytes in the packet.
 * oh_xxx Bit-mapped, related to handshaking and status info.
 *	I normally set it to 0x03 in trasmitted frames.
 *	7: Active when the TA is in a CONNECTed state.
 *	6: unknown
 *	5: handshaking, unknown
 *	4: handshaking, unknown
 *	3: unknown, usually 0
 *	2: unknown, usually 0
 *	1: handshaking, unknown, usually set to 1 in trasmitted frames
 *	0: handshaking, unknown, usually set to 1 in trasmitted frames
 * oh_pad Probably a pad byte.
 *
 * After the header you will find data bytes if oh_len was greater than zero.
 *
 */

struct omninet_header {
	__u8	oh_seq;
	__u8	oh_len;
	__u8	oh_xxx;
	__u8	oh_pad;
};

struct omninet_data {
	__u8	od_outseq;	/* Sequence number for bulk_out URBs */
};

static int omninet_port_probe(struct usb_serial_port *port)
{
	struct omninet_data *od;

	od = kmalloc(sizeof(struct omninet_data), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	usb_set_serial_port_data(port, od);

	return 0;
}

static int omninet_port_remove(struct usb_serial_port *port)
{
	struct omninet_data *od;

	od = usb_get_serial_port_data(port);
	kfree(od);

	return 0;
}

static int omninet_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial	*serial = port->serial;
	struct usb_serial_port	*wport;
	int			result = 0;

	wport = serial->port[1];
	tty_port_tty_set(&wport->port, tty);

	/* Start reading from the device */
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result)
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
			__func__, result);
	return result;
}

static void omninet_close(struct usb_serial_port *port)
{
	usb_kill_urb(port->read_urb);
}


#define OMNINET_DATAOFFSET	0x04
#define OMNINET_HEADERLEN	sizeof(struct omninet_header)
#define OMNINET_BULKOUTSIZE 	(64 - OMNINET_HEADERLEN)

static void omninet_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port 	*port 	= urb->context;
	unsigned char 		*data 	= urb->transfer_buffer;
	struct omninet_header 	*header = (struct omninet_header *) &data[0];
	int status = urb->status;
	int result;

	if (status) {
		dev_dbg(&port->dev, "%s - nonzero read bulk status received: %d\n",
			__func__, status);
		return;
	}

	if (urb->actual_length && header->oh_len) {
		tty_insert_flip_string(&port->port, data + OMNINET_DATAOFFSET,
				header->oh_len);
		tty_flip_buffer_push(&port->port);
	}

	/* Continue trying to always read  */
	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&port->dev,
			"%s - failed resubmitting read urb, error %d\n",
			__func__, result);
}

static int omninet_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count)
{
	struct usb_serial *serial = port->serial;
	struct usb_serial_port *wport = serial->port[1];

	struct omninet_data *od = usb_get_serial_port_data(port);
	struct omninet_header *header = (struct omninet_header *)
					wport->write_urb->transfer_buffer;

	int			result;

	if (count == 0) {
		dev_dbg(&port->dev, "%s - write request of 0 bytes\n", __func__);
		return 0;
	}

	if (!test_and_clear_bit(0, &port->write_urbs_free)) {
		dev_dbg(&port->dev, "%s - already writing\n", __func__);
		return 0;
	}

	count = (count > OMNINET_BULKOUTSIZE) ? OMNINET_BULKOUTSIZE : count;

	memcpy(wport->write_urb->transfer_buffer + OMNINET_DATAOFFSET,
								buf, count);

	usb_serial_debug_data(&port->dev, __func__, count,
			      wport->write_urb->transfer_buffer);

	header->oh_seq 	= od->od_outseq++;
	header->oh_len 	= count;
	header->oh_xxx  = 0x03;
	header->oh_pad 	= 0x00;

	/* send the data out the bulk port, always 64 bytes */
	wport->write_urb->transfer_buffer_length = 64;

	result = usb_submit_urb(wport->write_urb, GFP_ATOMIC);
	if (result) {
		set_bit(0, &wport->write_urbs_free);
		dev_err_console(port,
			"%s - failed submitting write urb, error %d\n",
			__func__, result);
	} else
		result = count;

	return result;
}


static int omninet_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial 	*serial = port->serial;
	struct usb_serial_port 	*wport 	= serial->port[1];

	int room = 0; /* Default: no room */

	if (test_bit(0, &wport->write_urbs_free))
		room = wport->bulk_out_size - OMNINET_HEADERLEN;

	dev_dbg(&port->dev, "%s - returns %d\n", __func__, room);

	return room;
}

static void omninet_write_bulk_callback(struct urb *urb)
{
/*	struct omninet_header	*header = (struct omninet_header  *)
						urb->transfer_buffer; */
	struct usb_serial_port 	*port   =  urb->context;
	int status = urb->status;

	set_bit(0, &port->write_urbs_free);
	if (status) {
		dev_dbg(&port->dev, "%s - nonzero write bulk status received: %d\n",
			__func__, status);
		return;
	}

	usb_serial_port_softint(port);
}


static void omninet_disconnect(struct usb_serial *serial)
{
	struct usb_serial_port *wport = serial->port[1];

	usb_kill_urb(wport->write_urb);
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
