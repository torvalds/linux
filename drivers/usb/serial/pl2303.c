// SPDX-License-Identifier: GPL-2.0
/*
 * Prolific PL2303 USB to serial adaptor driver
 *
 * Copyright (C) 2001-2007 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2003 IBM Corp.
 *
 * Original driver for 2.2.x by anonymous
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <asm/unaligned.h>
#include "pl2303.h"


#define PL2303_QUIRK_UART_STATE_IDX0		BIT(0)
#define PL2303_QUIRK_LEGACY			BIT(1)
#define PL2303_QUIRK_ENDPOINT_HACK		BIT(2)

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID),
		.driver_info = PL2303_QUIRK_ENDPOINT_HACK },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_RSAQ2) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_DCU11) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_RSAQ3) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_CHILITAG) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_PHAROS) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_ALDIGA) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_MMX) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_GPRS) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_HCR331) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_MOTOROLA) },
	{ USB_DEVICE(PL2303_VENDOR_ID, PL2303_PRODUCT_ID_ZTEK) },
	{ USB_DEVICE(IODATA_VENDOR_ID, IODATA_PRODUCT_ID) },
	{ USB_DEVICE(IODATA_VENDOR_ID, IODATA_PRODUCT_ID_RSAQ5) },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_ID),
		.driver_info = PL2303_QUIRK_ENDPOINT_HACK },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_UC485),
		.driver_info = PL2303_QUIRK_ENDPOINT_HACK },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_UC232B),
		.driver_info = PL2303_QUIRK_ENDPOINT_HACK },
	{ USB_DEVICE(ATEN_VENDOR_ID, ATEN_PRODUCT_ID2) },
	{ USB_DEVICE(ATEN_VENDOR_ID2, ATEN_PRODUCT_ID) },
	{ USB_DEVICE(ELCOM_VENDOR_ID, ELCOM_PRODUCT_ID) },
	{ USB_DEVICE(ELCOM_VENDOR_ID, ELCOM_PRODUCT_ID_UCSGT) },
	{ USB_DEVICE(ITEGNO_VENDOR_ID, ITEGNO_PRODUCT_ID) },
	{ USB_DEVICE(ITEGNO_VENDOR_ID, ITEGNO_PRODUCT_ID_2080) },
	{ USB_DEVICE(MA620_VENDOR_ID, MA620_PRODUCT_ID) },
	{ USB_DEVICE(RATOC_VENDOR_ID, RATOC_PRODUCT_ID) },
	{ USB_DEVICE(TRIPP_VENDOR_ID, TRIPP_PRODUCT_ID) },
	{ USB_DEVICE(RADIOSHACK_VENDOR_ID, RADIOSHACK_PRODUCT_ID) },
	{ USB_DEVICE(DCU10_VENDOR_ID, DCU10_PRODUCT_ID) },
	{ USB_DEVICE(SITECOM_VENDOR_ID, SITECOM_PRODUCT_ID) },
	{ USB_DEVICE(ALCATEL_VENDOR_ID, ALCATEL_PRODUCT_ID) },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_SX1),
		.driver_info = PL2303_QUIRK_UART_STATE_IDX0 },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_X65),
		.driver_info = PL2303_QUIRK_UART_STATE_IDX0 },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_X75),
		.driver_info = PL2303_QUIRK_UART_STATE_IDX0 },
	{ USB_DEVICE(SIEMENS_VENDOR_ID, SIEMENS_PRODUCT_ID_EF81),
		.driver_info = PL2303_QUIRK_ENDPOINT_HACK },
	{ USB_DEVICE(BENQ_VENDOR_ID, BENQ_PRODUCT_ID_S81) }, /* Benq/Siemens S81 */
	{ USB_DEVICE(SYNTECH_VENDOR_ID, SYNTECH_PRODUCT_ID) },
	{ USB_DEVICE(NOKIA_CA42_VENDOR_ID, NOKIA_CA42_PRODUCT_ID) },
	{ USB_DEVICE(CA_42_CA42_VENDOR_ID, CA_42_CA42_PRODUCT_ID) },
	{ USB_DEVICE(SAGEM_VENDOR_ID, SAGEM_PRODUCT_ID) },
	{ USB_DEVICE(LEADTEK_VENDOR_ID, LEADTEK_9531_PRODUCT_ID) },
	{ USB_DEVICE(SPEEDDRAGON_VENDOR_ID, SPEEDDRAGON_PRODUCT_ID) },
	{ USB_DEVICE(DATAPILOT_U2_VENDOR_ID, DATAPILOT_U2_PRODUCT_ID) },
	{ USB_DEVICE(BELKIN_VENDOR_ID, BELKIN_PRODUCT_ID) },
	{ USB_DEVICE(ALCOR_VENDOR_ID, ALCOR_PRODUCT_ID),
		.driver_info = PL2303_QUIRK_ENDPOINT_HACK },
	{ USB_DEVICE(WS002IN_VENDOR_ID, WS002IN_PRODUCT_ID) },
	{ USB_DEVICE(COREGA_VENDOR_ID, COREGA_PRODUCT_ID) },
	{ USB_DEVICE(YCCABLE_VENDOR_ID, YCCABLE_PRODUCT_ID) },
	{ USB_DEVICE(SUPERIAL_VENDOR_ID, SUPERIAL_PRODUCT_ID) },
	{ USB_DEVICE(HP_VENDOR_ID, HP_LD220_PRODUCT_ID) },
	{ USB_DEVICE(HP_VENDOR_ID, HP_LD960_PRODUCT_ID) },
	{ USB_DEVICE(HP_VENDOR_ID, HP_LCM220_PRODUCT_ID) },
	{ USB_DEVICE(HP_VENDOR_ID, HP_LCM960_PRODUCT_ID) },
	{ USB_DEVICE(CRESSI_VENDOR_ID, CRESSI_EDY_PRODUCT_ID) },
	{ USB_DEVICE(ZEAGLE_VENDOR_ID, ZEAGLE_N2ITION3_PRODUCT_ID) },
	{ USB_DEVICE(SONY_VENDOR_ID, SONY_QN3USB_PRODUCT_ID) },
	{ USB_DEVICE(SANWA_VENDOR_ID, SANWA_PRODUCT_ID) },
	{ USB_DEVICE(ADLINK_VENDOR_ID, ADLINK_ND6530_PRODUCT_ID) },
	{ USB_DEVICE(SMART_VENDOR_ID, SMART_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

#define SET_LINE_REQUEST_TYPE		0x21
#define SET_LINE_REQUEST		0x20

#define SET_CONTROL_REQUEST_TYPE	0x21
#define SET_CONTROL_REQUEST		0x22
#define CONTROL_DTR			0x01
#define CONTROL_RTS			0x02

#define BREAK_REQUEST_TYPE		0x21
#define BREAK_REQUEST			0x23
#define BREAK_ON			0xffff
#define BREAK_OFF			0x0000

#define GET_LINE_REQUEST_TYPE		0xa1
#define GET_LINE_REQUEST		0x21

#define VENDOR_WRITE_REQUEST_TYPE	0x40
#define VENDOR_WRITE_REQUEST		0x01

#define VENDOR_READ_REQUEST_TYPE	0xc0
#define VENDOR_READ_REQUEST		0x01

#define UART_STATE_INDEX		8
#define UART_STATE_MSR_MASK		0x8b
#define UART_STATE_TRANSIENT_MASK	0x74
#define UART_DCD			0x01
#define UART_DSR			0x02
#define UART_BREAK_ERROR		0x04
#define UART_RING			0x08
#define UART_FRAME_ERROR		0x10
#define UART_PARITY_ERROR		0x20
#define UART_OVERRUN_ERROR		0x40
#define UART_CTS			0x80

static void pl2303_set_break(struct usb_serial_port *port, bool enable);

enum pl2303_type {
	TYPE_01,	/* Type 0 and 1 (difference unknown) */
	TYPE_HX,	/* HX version of the pl2303 chip */
	TYPE_COUNT
};

struct pl2303_type_data {
	speed_t max_baud_rate;
	unsigned long quirks;
};

struct pl2303_serial_private {
	const struct pl2303_type_data *type;
	unsigned long quirks;
};

struct pl2303_private {
	spinlock_t lock;
	u8 line_control;
	u8 line_status;

	u8 line_settings[7];
};

static const struct pl2303_type_data pl2303_type_data[TYPE_COUNT] = {
	[TYPE_01] = {
		.max_baud_rate =	1228800,
		.quirks =		PL2303_QUIRK_LEGACY,
	},
	[TYPE_HX] = {
		.max_baud_rate =	12000000,
	},
};

static int pl2303_vendor_read(struct usb_serial *serial, u16 value,
							unsigned char buf[1])
{
	struct device *dev = &serial->interface->dev;
	int res;

	res = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			VENDOR_READ_REQUEST, VENDOR_READ_REQUEST_TYPE,
			value, 0, buf, 1, 100);
	if (res != 1) {
		dev_err(dev, "%s - failed to read [%04x]: %d\n", __func__,
								value, res);
		if (res >= 0)
			res = -EIO;

		return res;
	}

	dev_dbg(dev, "%s - [%04x] = %02x\n", __func__, value, buf[0]);

	return 0;
}

static int pl2303_vendor_write(struct usb_serial *serial, u16 value, u16 index)
{
	struct device *dev = &serial->interface->dev;
	int res;

	dev_dbg(dev, "%s - [%04x] = %02x\n", __func__, value, index);

	res = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			VENDOR_WRITE_REQUEST, VENDOR_WRITE_REQUEST_TYPE,
			value, index, NULL, 0, 100);
	if (res) {
		dev_err(dev, "%s - failed to write [%04x]: %d\n", __func__,
								value, res);
		return res;
	}

	return 0;
}

static int pl2303_probe(struct usb_serial *serial,
					const struct usb_device_id *id)
{
	usb_set_serial_data(serial, (void *)id->driver_info);

	return 0;
}

/*
 * Use interrupt endpoint from first interface if available.
 *
 * This is needed due to the looney way its endpoints are set up.
 */
static int pl2303_endpoint_hack(struct usb_serial *serial,
					struct usb_serial_endpoints *epds)
{
	struct usb_interface *interface = serial->interface;
	struct usb_device *dev = serial->dev;
	struct device *ddev = &interface->dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	unsigned int i;

	if (interface == dev->actconfig->interface[0])
		return 0;

	/* check out the endpoints of the other interface */
	iface_desc = dev->actconfig->interface[0]->cur_altsetting;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!usb_endpoint_is_int_in(endpoint))
			continue;

		dev_dbg(ddev, "found interrupt in on separate interface\n");
		if (epds->num_interrupt_in < ARRAY_SIZE(epds->interrupt_in))
			epds->interrupt_in[epds->num_interrupt_in++] = endpoint;
	}

	return 0;
}

static int pl2303_calc_num_ports(struct usb_serial *serial,
					struct usb_serial_endpoints *epds)
{
	unsigned long quirks = (unsigned long)usb_get_serial_data(serial);
	struct device *dev = &serial->interface->dev;
	int ret;

	if (quirks & PL2303_QUIRK_ENDPOINT_HACK) {
		ret = pl2303_endpoint_hack(serial, epds);
		if (ret)
			return ret;
	}

	if (epds->num_interrupt_in < 1) {
		dev_err(dev, "required interrupt-in endpoint missing\n");
		return -ENODEV;
	}

	return 1;
}

static int pl2303_startup(struct usb_serial *serial)
{
	struct pl2303_serial_private *spriv;
	enum pl2303_type type = TYPE_01;
	unsigned char *buf;

	spriv = kzalloc(sizeof(*spriv), GFP_KERNEL);
	if (!spriv)
		return -ENOMEM;

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		kfree(spriv);
		return -ENOMEM;
	}

	if (serial->dev->descriptor.bDeviceClass == 0x02)
		type = TYPE_01;		/* type 0 */
	else if (serial->dev->descriptor.bMaxPacketSize0 == 0x40)
		type = TYPE_HX;
	else if (serial->dev->descriptor.bDeviceClass == 0x00)
		type = TYPE_01;		/* type 1 */
	else if (serial->dev->descriptor.bDeviceClass == 0xFF)
		type = TYPE_01;		/* type 1 */
	dev_dbg(&serial->interface->dev, "device type: %d\n", type);

	spriv->type = &pl2303_type_data[type];
	spriv->quirks = (unsigned long)usb_get_serial_data(serial);
	spriv->quirks |= spriv->type->quirks;

	usb_set_serial_data(serial, spriv);

	pl2303_vendor_read(serial, 0x8484, buf);
	pl2303_vendor_write(serial, 0x0404, 0);
	pl2303_vendor_read(serial, 0x8484, buf);
	pl2303_vendor_read(serial, 0x8383, buf);
	pl2303_vendor_read(serial, 0x8484, buf);
	pl2303_vendor_write(serial, 0x0404, 1);
	pl2303_vendor_read(serial, 0x8484, buf);
	pl2303_vendor_read(serial, 0x8383, buf);
	pl2303_vendor_write(serial, 0, 1);
	pl2303_vendor_write(serial, 1, 0);
	if (spriv->quirks & PL2303_QUIRK_LEGACY)
		pl2303_vendor_write(serial, 2, 0x24);
	else
		pl2303_vendor_write(serial, 2, 0x44);

	kfree(buf);

	return 0;
}

static void pl2303_release(struct usb_serial *serial)
{
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);

	kfree(spriv);
}

static int pl2303_port_probe(struct usb_serial_port *port)
{
	struct pl2303_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);

	usb_set_serial_port_data(port, priv);

	port->port.drain_delay = 256;

	return 0;
}

static int pl2303_port_remove(struct usb_serial_port *port)
{
	struct pl2303_private *priv = usb_get_serial_port_data(port);

	kfree(priv);

	return 0;
}

static int pl2303_set_control_lines(struct usb_serial_port *port, u8 value)
{
	struct usb_device *dev = port->serial->dev;
	int retval;

	dev_dbg(&port->dev, "%s - %02x\n", __func__, value);

	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				 SET_CONTROL_REQUEST, SET_CONTROL_REQUEST_TYPE,
				 value, 0, NULL, 0, 100);
	if (retval)
		dev_err(&port->dev, "%s - failed: %d\n", __func__, retval);

	return retval;
}

/*
 * Returns the nearest supported baud rate that can be set directly without
 * using divisors.
 */
static speed_t pl2303_get_supported_baud_rate(speed_t baud)
{
	static const speed_t baud_sup[] = {
		75, 150, 300, 600, 1200, 1800, 2400, 3600, 4800, 7200, 9600,
		14400, 19200, 28800, 38400, 57600, 115200, 230400, 460800,
		614400, 921600, 1228800, 2457600, 3000000, 6000000
	};

	unsigned i;

	for (i = 0; i < ARRAY_SIZE(baud_sup); ++i) {
		if (baud_sup[i] > baud)
			break;
	}

	if (i == ARRAY_SIZE(baud_sup))
		baud = baud_sup[i - 1];
	else if (i > 0 && (baud_sup[i] - baud) > (baud - baud_sup[i - 1]))
		baud = baud_sup[i - 1];
	else
		baud = baud_sup[i];

	return baud;
}

/*
 * NOTE: If unsupported baud rates are set directly, the PL2303 seems to
 *       use 9600 baud.
 */
static speed_t pl2303_encode_baud_rate_direct(unsigned char buf[4],
								speed_t baud)
{
	put_unaligned_le32(baud, buf);

	return baud;
}

static speed_t pl2303_encode_baud_rate_divisor(unsigned char buf[4],
								speed_t baud)
{
	unsigned int baseline, mantissa, exponent;

	/*
	 * Apparently the formula is:
	 *   baudrate = 12M * 32 / (mantissa * 4^exponent)
	 * where
	 *   mantissa = buf[8:0]
	 *   exponent = buf[11:9]
	 */
	baseline = 12000000 * 32;
	mantissa = baseline / baud;
	if (mantissa == 0)
		mantissa = 1;	/* Avoid dividing by zero if baud > 32*12M. */
	exponent = 0;
	while (mantissa >= 512) {
		if (exponent < 7) {
			mantissa >>= 2;	/* divide by 4 */
			exponent++;
		} else {
			/* Exponent is maxed. Trim mantissa and leave. */
			mantissa = 511;
			break;
		}
	}

	buf[3] = 0x80;
	buf[2] = 0;
	buf[1] = exponent << 1 | mantissa >> 8;
	buf[0] = mantissa & 0xff;

	/* Calculate and return the exact baud rate. */
	baud = (baseline / mantissa) >> (exponent << 1);

	return baud;
}

static void pl2303_encode_baud_rate(struct tty_struct *tty,
					struct usb_serial_port *port,
					u8 buf[4])
{
	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	speed_t	baud_sup;
	speed_t baud;

	baud = tty_get_baud_rate(tty);
	dev_dbg(&port->dev, "baud requested = %u\n", baud);
	if (!baud)
		return;

	if (spriv->type->max_baud_rate)
		baud = min_t(speed_t, baud, spriv->type->max_baud_rate);
	/*
	 * Use direct method for supported baud rates, otherwise use divisors.
	 */
	baud_sup = pl2303_get_supported_baud_rate(baud);

	if (baud == baud_sup)
		baud = pl2303_encode_baud_rate_direct(buf, baud);
	else
		baud = pl2303_encode_baud_rate_divisor(buf, baud);

	/* Save resulting baud rate */
	tty_encode_baud_rate(tty, baud, baud);
	dev_dbg(&port->dev, "baud set = %u\n", baud);
}

static int pl2303_get_line_request(struct usb_serial_port *port,
							unsigned char buf[7])
{
	struct usb_device *udev = port->serial->dev;
	int ret;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
				GET_LINE_REQUEST, GET_LINE_REQUEST_TYPE,
				0, 0, buf, 7, 100);
	if (ret != 7) {
		dev_err(&port->dev, "%s - failed: %d\n", __func__, ret);

		if (ret >= 0)
			ret = -EIO;

		return ret;
	}

	dev_dbg(&port->dev, "%s - %7ph\n", __func__, buf);

	return 0;
}

static int pl2303_set_line_request(struct usb_serial_port *port,
							unsigned char buf[7])
{
	struct usb_device *udev = port->serial->dev;
	int ret;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				SET_LINE_REQUEST, SET_LINE_REQUEST_TYPE,
				0, 0, buf, 7, 100);
	if (ret < 0) {
		dev_err(&port->dev, "%s - failed: %d\n", __func__, ret);
		return ret;
	}

	dev_dbg(&port->dev, "%s - %7ph\n", __func__, buf);

	return 0;
}

static bool pl2303_termios_change(const struct ktermios *a, const struct ktermios *b)
{
	bool ixon_change;

	ixon_change = ((a->c_iflag ^ b->c_iflag) & (IXON | IXANY)) ||
			a->c_cc[VSTART] != b->c_cc[VSTART] ||
			a->c_cc[VSTOP] != b->c_cc[VSTOP];

	return tty_termios_hw_change(a, b) || ixon_change;
}

static void pl2303_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned char *buf;
	int ret;
	u8 control;

	if (old_termios && !pl2303_termios_change(&tty->termios, old_termios))
		return;

	buf = kzalloc(7, GFP_KERNEL);
	if (!buf) {
		/* Report back no change occurred */
		if (old_termios)
			tty->termios = *old_termios;
		return;
	}

	pl2303_get_line_request(port, buf);

	switch (C_CSIZE(tty)) {
	case CS5:
		buf[6] = 5;
		break;
	case CS6:
		buf[6] = 6;
		break;
	case CS7:
		buf[6] = 7;
		break;
	default:
	case CS8:
		buf[6] = 8;
	}
	dev_dbg(&port->dev, "data bits = %d\n", buf[6]);

	/* For reference buf[0]:buf[3] baud rate value */
	pl2303_encode_baud_rate(tty, port, &buf[0]);

	/* For reference buf[4]=0 is 1 stop bits */
	/* For reference buf[4]=1 is 1.5 stop bits */
	/* For reference buf[4]=2 is 2 stop bits */
	if (C_CSTOPB(tty)) {
		/*
		 * NOTE: Comply with "real" UARTs / RS232:
		 *       use 1.5 instead of 2 stop bits with 5 data bits
		 */
		if (C_CSIZE(tty) == CS5) {
			buf[4] = 1;
			dev_dbg(&port->dev, "stop bits = 1.5\n");
		} else {
			buf[4] = 2;
			dev_dbg(&port->dev, "stop bits = 2\n");
		}
	} else {
		buf[4] = 0;
		dev_dbg(&port->dev, "stop bits = 1\n");
	}

	if (C_PARENB(tty)) {
		/* For reference buf[5]=0 is none parity */
		/* For reference buf[5]=1 is odd parity */
		/* For reference buf[5]=2 is even parity */
		/* For reference buf[5]=3 is mark parity */
		/* For reference buf[5]=4 is space parity */
		if (C_PARODD(tty)) {
			if (C_CMSPAR(tty)) {
				buf[5] = 3;
				dev_dbg(&port->dev, "parity = mark\n");
			} else {
				buf[5] = 1;
				dev_dbg(&port->dev, "parity = odd\n");
			}
		} else {
			if (C_CMSPAR(tty)) {
				buf[5] = 4;
				dev_dbg(&port->dev, "parity = space\n");
			} else {
				buf[5] = 2;
				dev_dbg(&port->dev, "parity = even\n");
			}
		}
	} else {
		buf[5] = 0;
		dev_dbg(&port->dev, "parity = none\n");
	}

	/*
	 * Some PL2303 are known to lose bytes if you change serial settings
	 * even to the same values as before. Thus we actually need to filter
	 * in this specific case.
	 *
	 * Note that the tty_termios_hw_change check above is not sufficient
	 * as a previously requested baud rate may differ from the one
	 * actually used (and stored in old_termios).
	 *
	 * NOTE: No additional locking needed for line_settings as it is
	 *       only used in set_termios, which is serialised against itself.
	 */
	if (!old_termios || memcmp(buf, priv->line_settings, 7)) {
		ret = pl2303_set_line_request(port, buf);
		if (!ret)
			memcpy(priv->line_settings, buf, 7);
	}

	/* change control lines if we are switching to or from B0 */
	spin_lock_irqsave(&priv->lock, flags);
	control = priv->line_control;
	if (C_BAUD(tty) == B0)
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	else if (old_termios && (old_termios->c_cflag & CBAUD) == B0)
		priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
	if (control != priv->line_control) {
		control = priv->line_control;
		spin_unlock_irqrestore(&priv->lock, flags);
		pl2303_set_control_lines(port, control);
	} else {
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	if (C_CRTSCTS(tty)) {
		if (spriv->quirks & PL2303_QUIRK_LEGACY)
			pl2303_vendor_write(serial, 0x0, 0x41);
		else
			pl2303_vendor_write(serial, 0x0, 0x61);
	} else if (I_IXON(tty) && !I_IXANY(tty) && START_CHAR(tty) == 0x11 &&
			STOP_CHAR(tty) == 0x13) {
		pl2303_vendor_write(serial, 0x0, 0xc0);
	} else {
		pl2303_vendor_write(serial, 0x0, 0x0);
	}

	kfree(buf);
}

static void pl2303_dtr_rts(struct usb_serial_port *port, int on)
{
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;

	spin_lock_irqsave(&priv->lock, flags);
	if (on)
		priv->line_control |= (CONTROL_DTR | CONTROL_RTS);
	else
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);

	pl2303_set_control_lines(port, control);
}

static void pl2303_close(struct usb_serial_port *port)
{
	usb_serial_generic_close(port);
	usb_kill_urb(port->interrupt_in_urb);
	pl2303_set_break(port, false);
}

static int pl2303_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	int result;

	if (spriv->quirks & PL2303_QUIRK_LEGACY) {
		usb_clear_halt(serial->dev, port->write_urb->pipe);
		usb_clear_halt(serial->dev, port->read_urb->pipe);
	} else {
		/* reset upstream data pipes */
		pl2303_vendor_write(serial, 8, 0);
		pl2303_vendor_write(serial, 9, 0);
	}

	/* Setup termios */
	if (tty)
		pl2303_set_termios(tty, port, NULL);

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "failed to submit interrupt urb: %d\n",
			result);
		return result;
	}

	result = usb_serial_generic_open(tty, port);
	if (result) {
		usb_kill_urb(port->interrupt_in_urb);
		return result;
	}

	return 0;
}

static int pl2303_tiocmset(struct tty_struct *tty,
			   unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	u8 control;
	int ret;

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->line_control |= CONTROL_RTS;
	if (set & TIOCM_DTR)
		priv->line_control |= CONTROL_DTR;
	if (clear & TIOCM_RTS)
		priv->line_control &= ~CONTROL_RTS;
	if (clear & TIOCM_DTR)
		priv->line_control &= ~CONTROL_DTR;
	control = priv->line_control;
	spin_unlock_irqrestore(&priv->lock, flags);

	ret = pl2303_set_control_lines(port, control);
	if (ret)
		return usb_translate_errors(ret);

	return 0;
}

static int pl2303_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int mcr;
	unsigned int status;
	unsigned int result;

	spin_lock_irqsave(&priv->lock, flags);
	mcr = priv->line_control;
	status = priv->line_status;
	spin_unlock_irqrestore(&priv->lock, flags);

	result = ((mcr & CONTROL_DTR)		? TIOCM_DTR : 0)
		  | ((mcr & CONTROL_RTS)	? TIOCM_RTS : 0)
		  | ((status & UART_CTS)	? TIOCM_CTS : 0)
		  | ((status & UART_DSR)	? TIOCM_DSR : 0)
		  | ((status & UART_RING)	? TIOCM_RI  : 0)
		  | ((status & UART_DCD)	? TIOCM_CD  : 0);

	dev_dbg(&port->dev, "%s - result = %x\n", __func__, result);

	return result;
}

static int pl2303_carrier_raised(struct usb_serial_port *port)
{
	struct pl2303_private *priv = usb_get_serial_port_data(port);

	if (priv->line_status & UART_DCD)
		return 1;

	return 0;
}

static int pl2303_get_serial(struct tty_struct *tty,
			struct serial_struct *ss)
{
	struct usb_serial_port *port = tty->driver_data;

	ss->type = PORT_16654;
	ss->line = port->minor;
	ss->port = port->port_number;
	ss->baud_base = 460800;
	return 0;
}

static void pl2303_set_break(struct usb_serial_port *port, bool enable)
{
	struct usb_serial *serial = port->serial;
	u16 state;
	int result;

	if (enable)
		state = BREAK_ON;
	else
		state = BREAK_OFF;

	dev_dbg(&port->dev, "%s - turning break %s\n", __func__,
			state == BREAK_OFF ? "off" : "on");

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 BREAK_REQUEST, BREAK_REQUEST_TYPE, state,
				 0, NULL, 0, 100);
	if (result)
		dev_err(&port->dev, "error sending break = %d\n", result);
}

static void pl2303_break_ctl(struct tty_struct *tty, int state)
{
	struct usb_serial_port *port = tty->driver_data;

	pl2303_set_break(port, state);
}

static void pl2303_update_line_status(struct usb_serial_port *port,
				      unsigned char *data,
				      unsigned int actual_length)
{
	struct usb_serial *serial = port->serial;
	struct pl2303_serial_private *spriv = usb_get_serial_data(serial);
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned long flags;
	unsigned int status_idx = UART_STATE_INDEX;
	u8 status;
	u8 delta;

	if (spriv->quirks & PL2303_QUIRK_UART_STATE_IDX0)
		status_idx = 0;

	if (actual_length < status_idx + 1)
		return;

	status = data[status_idx];

	/* Save off the uart status for others to look at */
	spin_lock_irqsave(&priv->lock, flags);
	delta = priv->line_status ^ status;
	priv->line_status = status;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (status & UART_BREAK_ERROR)
		usb_serial_handle_break(port);

	if (delta & UART_STATE_MSR_MASK) {
		if (delta & UART_CTS)
			port->icount.cts++;
		if (delta & UART_DSR)
			port->icount.dsr++;
		if (delta & UART_RING)
			port->icount.rng++;
		if (delta & UART_DCD) {
			port->icount.dcd++;
			tty = tty_port_tty_get(&port->port);
			if (tty) {
				usb_serial_handle_dcd_change(port, tty,
							status & UART_DCD);
				tty_kref_put(tty);
			}
		}

		wake_up_interruptible(&port->port.delta_msr_wait);
	}
}

static void pl2303_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port =  urb->context;
	unsigned char *data = urb->transfer_buffer;
	unsigned int actual_length = urb->actual_length;
	int status = urb->status;
	int retval;

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&port->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		goto exit;
	}

	usb_serial_debug_data(&port->dev, __func__,
			      urb->actual_length, urb->transfer_buffer);

	pl2303_update_line_status(port, data, actual_length);

exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval) {
		dev_err(&port->dev,
			"%s - usb_submit_urb failed with result %d\n",
			__func__, retval);
	}
}

static void pl2303_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct pl2303_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	char tty_flag = TTY_NORMAL;
	unsigned long flags;
	u8 line_status;
	int i;

	/* update line status */
	spin_lock_irqsave(&priv->lock, flags);
	line_status = priv->line_status;
	priv->line_status &= ~UART_STATE_TRANSIENT_MASK;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!urb->actual_length)
		return;

	/*
	 * Break takes precedence over parity, which takes precedence over
	 * framing errors.
	 */
	if (line_status & UART_BREAK_ERROR)
		tty_flag = TTY_BREAK;
	else if (line_status & UART_PARITY_ERROR)
		tty_flag = TTY_PARITY;
	else if (line_status & UART_FRAME_ERROR)
		tty_flag = TTY_FRAME;

	if (tty_flag != TTY_NORMAL)
		dev_dbg(&port->dev, "%s - tty_flag = %d\n", __func__,
								tty_flag);
	/* overrun is special, not associated with a char */
	if (line_status & UART_OVERRUN_ERROR)
		tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);

	if (port->port.console && port->sysrq) {
		for (i = 0; i < urb->actual_length; ++i)
			if (!usb_serial_handle_sysrq_char(port, data[i]))
				tty_insert_flip_char(&port->port, data[i],
						tty_flag);
	} else {
		tty_insert_flip_string_fixed_flag(&port->port, data, tty_flag,
							urb->actual_length);
	}

	tty_flip_buffer_push(&port->port);
}

static struct usb_serial_driver pl2303_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"pl2303",
	},
	.id_table =		id_table,
	.num_bulk_in =		1,
	.num_bulk_out =		1,
	.num_interrupt_in =	0,	/* see pl2303_calc_num_ports */
	.bulk_in_size =		256,
	.bulk_out_size =	256,
	.open =			pl2303_open,
	.close =		pl2303_close,
	.dtr_rts =		pl2303_dtr_rts,
	.carrier_raised =	pl2303_carrier_raised,
	.get_serial =		pl2303_get_serial,
	.break_ctl =		pl2303_break_ctl,
	.set_termios =		pl2303_set_termios,
	.tiocmget =		pl2303_tiocmget,
	.tiocmset =		pl2303_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
	.process_read_urb =	pl2303_process_read_urb,
	.read_int_callback =	pl2303_read_int_callback,
	.probe =		pl2303_probe,
	.calc_num_ports =	pl2303_calc_num_ports,
	.attach =		pl2303_startup,
	.release =		pl2303_release,
	.port_probe =		pl2303_port_probe,
	.port_remove =		pl2303_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&pl2303_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION("Prolific PL2303 USB to serial adaptor driver");
MODULE_LICENSE("GPL v2");
