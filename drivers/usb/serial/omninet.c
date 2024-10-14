// SPDX-License-Identifier: GPL-2.0
/*
 * USB ZyXEL omni.net driver
 *
 * Copyright (C) 2013,2017 Johan Hovold <johan@kernel.org>
 *
 * See Documentation/usb/usb-serial.rst for more information on using this
 * driver
 *
 * Please report both successes and troubles to the author at omninet@kroah.com
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>

#define DRIVER_AUTHOR "Alessandro Zummo"
#define DRIVER_DESC "USB ZyXEL omni.net Driver"

#define ZYXEL_VENDOR_ID		0x0586
#define ZYXEL_OMNINET_ID	0x1000
#define ZYXEL_OMNI_56K_PLUS_ID	0x1500
/* This one seems to be a re-branded ZyXEL device */
#define BT_IGNITIONPRO_ID	0x2000

/* function prototypes */
static void omninet_process_read_urb(struct urb *urb);
static int omninet_prepare_write_buffer(struct usb_serial_port *port,
				void *buf, size_t count);
static int omninet_calc_num_ports(struct usb_serial *serial,
				struct usb_serial_endpoints *epds);
static int omninet_port_probe(struct usb_serial_port *port);
static void omninet_port_remove(struct usb_serial_port *port);

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(ZYXEL_VENDOR_ID, ZYXEL_OMNINET_ID) },
	{ USB_DEVICE(ZYXEL_VENDOR_ID, ZYXEL_OMNI_56K_PLUS_ID) },
	{ USB_DEVICE(ZYXEL_VENDOR_ID, BT_IGNITIONPRO_ID) },
	{ }						/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_serial_driver zyxel_omninet_device = {
	.driver = {
		.name =		"omninet",
	},
	.description =		"ZyXEL - omni.net usb",
	.id_table =		id_table,
	.num_bulk_out =		2,
	.calc_num_ports =	omninet_calc_num_ports,
	.port_probe =		omninet_port_probe,
	.port_remove =		omninet_port_remove,
	.process_read_urb =	omninet_process_read_urb,
	.prepare_write_buffer =	omninet_prepare_write_buffer,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&zyxel_omninet_device, NULL
};


/*
 * The protocol.
 *
 * The omni.net always exchange 64 bytes of data with the host. The first
 * four bytes are the control header.
 *
 * oh_seq is a sequence number. Don't know if/how it's used.
 * oh_len is the length of the data bytes in the packet.
 * oh_xxx Bit-mapped, related to handshaking and status info.
 *	I normally set it to 0x03 in transmitted frames.
 *	7: Active when the TA is in a CONNECTed state.
 *	6: unknown
 *	5: handshaking, unknown
 *	4: handshaking, unknown
 *	3: unknown, usually 0
 *	2: unknown, usually 0
 *	1: handshaking, unknown, usually set to 1 in transmitted frames
 *	0: handshaking, unknown, usually set to 1 in transmitted frames
 * oh_pad Probably a pad byte.
 *
 * After the header you will find data bytes if oh_len was greater than zero.
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

static int omninet_calc_num_ports(struct usb_serial *serial,
					struct usb_serial_endpoints *epds)
{
	/* We need only the second bulk-out for our single-port device. */
	epds->bulk_out[0] = epds->bulk_out[1];
	epds->num_bulk_out = 1;

	return 1;
}

static int omninet_port_probe(struct usb_serial_port *port)
{
	struct omninet_data *od;

	od = kzalloc(sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	usb_set_serial_port_data(port, od);

	return 0;
}

static void omninet_port_remove(struct usb_serial_port *port)
{
	struct omninet_data *od;

	od = usb_get_serial_port_data(port);
	kfree(od);
}

#define OMNINET_HEADERLEN	4
#define OMNINET_BULKOUTSIZE	64
#define OMNINET_PAYLOADSIZE	(OMNINET_BULKOUTSIZE - OMNINET_HEADERLEN)

static void omninet_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	const struct omninet_header *hdr = urb->transfer_buffer;
	const unsigned char *data;
	size_t data_len;

	if (urb->actual_length <= OMNINET_HEADERLEN || !hdr->oh_len)
		return;

	data = (char *)urb->transfer_buffer + OMNINET_HEADERLEN;
	data_len = min_t(size_t, urb->actual_length - OMNINET_HEADERLEN,
								hdr->oh_len);
	tty_insert_flip_string(&port->port, data, data_len);
	tty_flip_buffer_push(&port->port);
}

static int omninet_prepare_write_buffer(struct usb_serial_port *port,
					void *buf, size_t count)
{
	struct omninet_data *od = usb_get_serial_port_data(port);
	struct omninet_header *header = buf;

	count = min_t(size_t, count, OMNINET_PAYLOADSIZE);

	count = kfifo_out_locked(&port->write_fifo, buf + OMNINET_HEADERLEN,
			count, &port->lock);

	header->oh_seq = od->od_outseq++;
	header->oh_len = count;
	header->oh_xxx = 0x03;
	header->oh_pad = 0x00;

	/* always 64 bytes */
	return OMNINET_BULKOUTSIZE;
}

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
