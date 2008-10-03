/*
 * Silicon Laboratories CP2101/CP2102 USB to RS232 serial adaptor driver
 *
 * Copyright (C) 2005 Craig Shelley (craig@microtron.org.uk)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 * Support to set flow control line levels using TIOCMGET and TIOCMSET
 * thanks to Karl Hiramoto karl@hiramoto.org. RTSCTS hardware flow
 * control thanks to Munir Nassar nassarmu@real-time.com
 *
 * Outstanding Issues:
 *  Buffers are not flushed when the port is opened.
 *  Multiple calls to write() may fail with "Resource temporarily unavailable"
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/usb/serial.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.07"
#define DRIVER_DESC "Silicon Labs CP2101/CP2102 RS232 serial adaptor driver"

/*
 * Function Prototypes
 */
static int cp2101_open(struct tty_struct *, struct usb_serial_port *,
							struct file *);
static void cp2101_cleanup(struct usb_serial_port *);
static void cp2101_close(struct tty_struct *, struct usb_serial_port *,
							struct file*);
static void cp2101_get_termios(struct tty_struct *);
static void cp2101_set_termios(struct tty_struct *, struct usb_serial_port *,
							struct ktermios*);
static int cp2101_tiocmget(struct tty_struct *, struct file *);
static int cp2101_tiocmset(struct tty_struct *, struct file *,
		unsigned int, unsigned int);
static void cp2101_break_ctl(struct tty_struct *, int);
static int cp2101_startup(struct usb_serial *);
static void cp2101_shutdown(struct usb_serial *);


static int debug;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x0489, 0xE000) }, /* Pirelli Broadband S.p.A, DP-L10 SIP/GSM Mobile */
	{ USB_DEVICE(0x08e6, 0x5501) }, /* Gemalto Prox-PU/CU contactless smartcard reader */
	{ USB_DEVICE(0x0FCF, 0x1003) }, /* Dynastream ANT development board */
	{ USB_DEVICE(0x0FCF, 0x1004) }, /* Dynastream ANT2USB */
	{ USB_DEVICE(0x0FCF, 0x1006) }, /* Dynastream ANT development board */
	{ USB_DEVICE(0x10A6, 0xAA26) }, /* Knock-off DCU-11 cable */
	{ USB_DEVICE(0x10AB, 0x10C5) }, /* Siemens MC60 Cable */
	{ USB_DEVICE(0x10B5, 0xAC70) }, /* Nokia CA-42 USB */
	{ USB_DEVICE(0x10C4, 0x800A) }, /* SPORTident BSM7-D-USB main station */
	{ USB_DEVICE(0x10C4, 0x803B) }, /* Pololu USB-serial converter */
	{ USB_DEVICE(0x10C4, 0x8053) }, /* Enfora EDG1228 */
	{ USB_DEVICE(0x10C4, 0x8066) }, /* Argussoft In-System Programmer */
	{ USB_DEVICE(0x10C4, 0x807A) }, /* Crumb128 board */
	{ USB_DEVICE(0x10C4, 0x80CA) }, /* Degree Controls Inc */
	{ USB_DEVICE(0x10C4, 0x80DD) }, /* Tracient RFID */
	{ USB_DEVICE(0x10C4, 0x80F6) }, /* Suunto sports instrument */
	{ USB_DEVICE(0x10C4, 0x8115) }, /* Arygon NFC/Mifare Reader */
	{ USB_DEVICE(0x10C4, 0x813D) }, /* Burnside Telecom Deskmobile */
	{ USB_DEVICE(0x10C4, 0x814A) }, /* West Mountain Radio RIGblaster P&P */
	{ USB_DEVICE(0x10C4, 0x814B) }, /* West Mountain Radio RIGtalk */
	{ USB_DEVICE(0x10C4, 0x815E) }, /* Helicomm IP-Link 1220-DVM */
	{ USB_DEVICE(0x10C4, 0x81A6) }, /* ThinkOptics WavIt */
	{ USB_DEVICE(0x10C4, 0x81AC) }, /* MSD Dash Hawk */
	{ USB_DEVICE(0x10C4, 0x81C8) }, /* Lipowsky Industrie Elektronik GmbH, Baby-JTAG */
	{ USB_DEVICE(0x10C4, 0x81E2) }, /* Lipowsky Industrie Elektronik GmbH, Baby-LIN */
	{ USB_DEVICE(0x10C4, 0x81E7) }, /* Aerocomm Radio */
	{ USB_DEVICE(0x10C4, 0x8218) }, /* Lipowsky Industrie Elektronik GmbH, HARP-1 */
	{ USB_DEVICE(0x10c4, 0x8293) }, /* Telegesys ETRX2USB */
	{ USB_DEVICE(0x10C4, 0x8341) }, /* Siemens MC35PU GPRS Modem */
	{ USB_DEVICE(0x10C4, 0xEA60) }, /* Silicon Labs factory default */
	{ USB_DEVICE(0x10C4, 0xEA61) }, /* Silicon Labs factory default */
	{ USB_DEVICE(0x10C4, 0xF001) }, /* Elan Digital Systems USBscope50 */
	{ USB_DEVICE(0x10C4, 0xF002) }, /* Elan Digital Systems USBwave12 */
	{ USB_DEVICE(0x10C4, 0xF003) }, /* Elan Digital Systems USBpulse100 */
	{ USB_DEVICE(0x10C4, 0xF004) }, /* Elan Digital Systems USBcount50 */
	{ USB_DEVICE(0x10C5, 0xEA61) }, /* Silicon Labs MobiData GPRS USB Modem */
	{ USB_DEVICE(0x13AD, 0x9999) }, /* Baltech card reader */
	{ USB_DEVICE(0x166A, 0x0303) }, /* Clipsal 5500PCU C-Bus USB interface */
	{ USB_DEVICE(0x16D6, 0x0001) }, /* Jablotron serial interface */
	{ USB_DEVICE(0x18EF, 0xE00F) }, /* ELV USB-I2C-Interface */
	{ } /* Terminating Entry */
};

MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver cp2101_driver = {
	.name		= "cp2101",
	.probe		= usb_serial_probe,
	.disconnect	= usb_serial_disconnect,
	.id_table	= id_table,
	.no_dynamic_id	= 	1,
};

static struct usb_serial_driver cp2101_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name = 	"cp2101",
	},
	.usb_driver		= &cp2101_driver,
	.id_table		= id_table,
	.num_ports		= 1,
	.open			= cp2101_open,
	.close			= cp2101_close,
	.break_ctl		= cp2101_break_ctl,
	.set_termios		= cp2101_set_termios,
	.tiocmget 		= cp2101_tiocmget,
	.tiocmset		= cp2101_tiocmset,
	.attach			= cp2101_startup,
	.shutdown		= cp2101_shutdown,
};

/* Config request types */
#define REQTYPE_HOST_TO_DEVICE	0x41
#define REQTYPE_DEVICE_TO_HOST	0xc1

/* Config SET requests. To GET, add 1 to the request number */
#define CP2101_UART 		0x00	/* Enable / Disable */
#define CP2101_BAUDRATE		0x01	/* (BAUD_RATE_GEN_FREQ / baudrate) */
#define CP2101_BITS		0x03	/* 0x(0)(databits)(parity)(stopbits) */
#define CP2101_BREAK		0x05	/* On / Off */
#define CP2101_CONTROL		0x07	/* Flow control line states */
#define CP2101_MODEMCTL		0x13	/* Modem controls */
#define CP2101_CONFIG_6		0x19	/* 6 bytes of config data ??? */

/* CP2101_UART */
#define UART_ENABLE		0x0001
#define UART_DISABLE		0x0000

/* CP2101_BAUDRATE */
#define BAUD_RATE_GEN_FREQ	0x384000

/* CP2101_BITS */
#define BITS_DATA_MASK		0X0f00
#define BITS_DATA_5		0X0500
#define BITS_DATA_6		0X0600
#define BITS_DATA_7		0X0700
#define BITS_DATA_8		0X0800
#define BITS_DATA_9		0X0900

#define BITS_PARITY_MASK	0x00f0
#define BITS_PARITY_NONE	0x0000
#define BITS_PARITY_ODD		0x0010
#define BITS_PARITY_EVEN	0x0020
#define BITS_PARITY_MARK	0x0030
#define BITS_PARITY_SPACE	0x0040

#define BITS_STOP_MASK		0x000f
#define BITS_STOP_1		0x0000
#define BITS_STOP_1_5		0x0001
#define BITS_STOP_2		0x0002

/* CP2101_BREAK */
#define BREAK_ON		0x0000
#define BREAK_OFF		0x0001

/* CP2101_CONTROL */
#define CONTROL_DTR		0x0001
#define CONTROL_RTS		0x0002
#define CONTROL_CTS		0x0010
#define CONTROL_DSR		0x0020
#define CONTROL_RING		0x0040
#define CONTROL_DCD		0x0080
#define CONTROL_WRITE_DTR	0x0100
#define CONTROL_WRITE_RTS	0x0200

/*
 * cp2101_get_config
 * Reads from the CP2101 configuration registers
 * 'size' is specified in bytes.
 * 'data' is a pointer to a pre-allocated array of integers large
 * enough to hold 'size' bytes (with 4 bytes to each integer)
 */
static int cp2101_get_config(struct usb_serial_port *port, u8 request,
		unsigned int *data, int size)
{
	struct usb_serial *serial = port->serial;
	__le32 *buf;
	int result, i, length;

	/* Number of integers required to contain the array */
	length = (((size - 1) | 3) + 1)/4;

	buf = kcalloc(length, sizeof(__le32), GFP_KERNEL);
	if (!buf) {
		dev_err(&port->dev, "%s - out of memory.\n", __func__);
		return -ENOMEM;
	}

	/* For get requests, the request number must be incremented */
	request++;

	/* Issue the request, attempting to read 'size' bytes */
	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				request, REQTYPE_DEVICE_TO_HOST, 0x0000,
				0, buf, size, 300);

	/* Convert data into an array of integers */
	for (i = 0; i < length; i++)
		data[i] = le32_to_cpu(buf[i]);

	kfree(buf);

	if (result != size) {
		dev_err(&port->dev, "%s - Unable to send config request, "
				"request=0x%x size=%d result=%d\n",
				__func__, request, size, result);
		return -EPROTO;
	}

	return 0;
}

/*
 * cp2101_set_config
 * Writes to the CP2101 configuration registers
 * Values less than 16 bits wide are sent directly
 * 'size' is specified in bytes.
 */
static int cp2101_set_config(struct usb_serial_port *port, u8 request,
		unsigned int *data, int size)
{
	struct usb_serial *serial = port->serial;
	__le32 *buf;
	int result, i, length;

	/* Number of integers required to contain the array */
	length = (((size - 1) | 3) + 1)/4;

	buf = kmalloc(length * sizeof(__le32), GFP_KERNEL);
	if (!buf) {
		dev_err(&port->dev, "%s - out of memory.\n",
				__func__);
		return -ENOMEM;
	}

	/* Array of integers into bytes */
	for (i = 0; i < length; i++)
		buf[i] = cpu_to_le32(data[i]);

	if (size > 2) {
		result = usb_control_msg(serial->dev,
				usb_sndctrlpipe(serial->dev, 0),
				request, REQTYPE_HOST_TO_DEVICE, 0x0000,
				0, buf, size, 300);
	} else {
		result = usb_control_msg(serial->dev,
				usb_sndctrlpipe(serial->dev, 0),
				request, REQTYPE_HOST_TO_DEVICE, data[0],
				0, NULL, 0, 300);
	}

	kfree(buf);

	if ((size > 2 && result != size) || result < 0) {
		dev_err(&port->dev, "%s - Unable to send request, "
				"request=0x%x size=%d result=%d\n",
				__func__, request, size, result);
		return -EPROTO;
	}

	/* Single data value */
	result = usb_control_msg(serial->dev,
			usb_sndctrlpipe(serial->dev, 0),
			request, REQTYPE_HOST_TO_DEVICE, data[0],
			0, NULL, 0, 300);
	return 0;
}

/*
 * cp2101_set_config_single
 * Convenience function for calling cp2101_set_config on single data values
 * without requiring an integer pointer
 */
static inline int cp2101_set_config_single(struct usb_serial_port *port,
		u8 request, unsigned int data)
{
	return cp2101_set_config(port, request, &data, 2);
}

static int cp2101_open(struct tty_struct *tty, struct usb_serial_port *port,
				struct file *filp)
{
	struct usb_serial *serial = port->serial;
	int result;

	dbg("%s - port %d", __func__, port->number);

	if (cp2101_set_config_single(port, CP2101_UART, UART_ENABLE)) {
		dev_err(&port->dev, "%s - Unable to enable UART\n",
				__func__);
		return -EPROTO;
	}

	/* Start reading from the device */
	usb_fill_bulk_urb(port->read_urb, serial->dev,
			usb_rcvbulkpipe(serial->dev,
			port->bulk_in_endpointAddress),
			port->read_urb->transfer_buffer,
			port->read_urb->transfer_buffer_length,
			serial->type->read_bulk_callback,
			port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "%s - failed resubmitting read urb, "
				"error %d\n", __func__, result);
		return result;
	}

	/* Configure the termios structure */
	cp2101_get_termios(tty);

	/* Set the DTR and RTS pins low */
	cp2101_tiocmset(tty, NULL, TIOCM_DTR | TIOCM_RTS, 0);

	return 0;
}

static void cp2101_cleanup(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	dbg("%s - port %d", __func__, port->number);

	if (serial->dev) {
		/* shutdown any bulk reads that might be going on */
		if (serial->num_bulk_out)
			usb_kill_urb(port->write_urb);
		if (serial->num_bulk_in)
			usb_kill_urb(port->read_urb);
	}
}

static void cp2101_close(struct tty_struct *tty, struct usb_serial_port *port,
					struct file *filp)
{
	dbg("%s - port %d", __func__, port->number);

	/* shutdown our urbs */
	dbg("%s - shutting down urbs", __func__);
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);

	mutex_lock(&port->serial->disc_mutex);
	if (!port->serial->disconnected)
		cp2101_set_config_single(port, CP2101_UART, UART_DISABLE);
	mutex_unlock(&port->serial->disc_mutex);
}

/*
 * cp2101_get_termios
 * Reads the baud rate, data bits, parity, stop bits and flow control mode
 * from the device, corrects any unsupported values, and configures the
 * termios structure to reflect the state of the device
 */
static void cp2101_get_termios (struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int cflag, modem_ctl[4];
	unsigned int baud;
	unsigned int bits;

	dbg("%s - port %d", __func__, port->number);

	cp2101_get_config(port, CP2101_BAUDRATE, &baud, 2);
	/* Convert to baudrate */
	if (baud)
		baud = BAUD_RATE_GEN_FREQ / baud;

	dbg("%s - baud rate = %d", __func__, baud);

	tty_encode_baud_rate(tty, baud, baud);
	cflag = tty->termios->c_cflag;

	cp2101_get_config(port, CP2101_BITS, &bits, 2);
	cflag &= ~CSIZE;
	switch (bits & BITS_DATA_MASK) {
	case BITS_DATA_5:
		dbg("%s - data bits = 5", __func__);
		cflag |= CS5;
		break;
	case BITS_DATA_6:
		dbg("%s - data bits = 6", __func__);
		cflag |= CS6;
		break;
	case BITS_DATA_7:
		dbg("%s - data bits = 7", __func__);
		cflag |= CS7;
		break;
	case BITS_DATA_8:
		dbg("%s - data bits = 8", __func__);
		cflag |= CS8;
		break;
	case BITS_DATA_9:
		dbg("%s - data bits = 9 (not supported, using 8 data bits)",
								__func__);
		cflag |= CS8;
		bits &= ~BITS_DATA_MASK;
		bits |= BITS_DATA_8;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	default:
		dbg("%s - Unknown number of data bits, using 8", __func__);
		cflag |= CS8;
		bits &= ~BITS_DATA_MASK;
		bits |= BITS_DATA_8;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	}

	switch (bits & BITS_PARITY_MASK) {
	case BITS_PARITY_NONE:
		dbg("%s - parity = NONE", __func__);
		cflag &= ~PARENB;
		break;
	case BITS_PARITY_ODD:
		dbg("%s - parity = ODD", __func__);
		cflag |= (PARENB|PARODD);
		break;
	case BITS_PARITY_EVEN:
		dbg("%s - parity = EVEN", __func__);
		cflag &= ~PARODD;
		cflag |= PARENB;
		break;
	case BITS_PARITY_MARK:
		dbg("%s - parity = MARK (not supported, disabling parity)",
				__func__);
		cflag &= ~PARENB;
		bits &= ~BITS_PARITY_MASK;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	case BITS_PARITY_SPACE:
		dbg("%s - parity = SPACE (not supported, disabling parity)",
				__func__);
		cflag &= ~PARENB;
		bits &= ~BITS_PARITY_MASK;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	default:
		dbg("%s - Unknown parity mode, disabling parity", __func__);
		cflag &= ~PARENB;
		bits &= ~BITS_PARITY_MASK;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	}

	cflag &= ~CSTOPB;
	switch (bits & BITS_STOP_MASK) {
	case BITS_STOP_1:
		dbg("%s - stop bits = 1", __func__);
		break;
	case BITS_STOP_1_5:
		dbg("%s - stop bits = 1.5 (not supported, using 1 stop bit)",
								__func__);
		bits &= ~BITS_STOP_MASK;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	case BITS_STOP_2:
		dbg("%s - stop bits = 2", __func__);
		cflag |= CSTOPB;
		break;
	default:
		dbg("%s - Unknown number of stop bits, using 1 stop bit",
								__func__);
		bits &= ~BITS_STOP_MASK;
		cp2101_set_config(port, CP2101_BITS, &bits, 2);
		break;
	}

	cp2101_get_config(port, CP2101_MODEMCTL, modem_ctl, 16);
	if (modem_ctl[0] & 0x0008) {
		dbg("%s - flow control = CRTSCTS", __func__);
		cflag |= CRTSCTS;
	} else {
		dbg("%s - flow control = NONE", __func__);
		cflag &= ~CRTSCTS;
	}

	tty->termios->c_cflag = cflag;
}

static void cp2101_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	unsigned int cflag, old_cflag;
	unsigned int baud = 0, bits;
	unsigned int modem_ctl[4];

	dbg("%s - port %d", __func__, port->number);

	if (!tty)
		return;

	tty->termios->c_cflag &= ~CMSPAR;
	cflag = tty->termios->c_cflag;
	old_cflag = old_termios->c_cflag;
	baud = tty_get_baud_rate(tty);

	/* If the baud rate is to be updated*/
	if (baud != tty_termios_baud_rate(old_termios)) {
		switch (baud) {
		case 0:
		case 600:
		case 1200:
		case 1800:
		case 2400:
		case 4800:
		case 7200:
		case 9600:
		case 14400:
		case 19200:
		case 28800:
		case 38400:
		case 55854:
		case 57600:
		case 115200:
		case 127117:
		case 230400:
		case 460800:
		case 921600:
		case 3686400:
			break;
		default:
			baud = 9600;
			break;
		}

		if (baud) {
			dbg("%s - Setting baud rate to %d baud", __func__,
					baud);
			if (cp2101_set_config_single(port, CP2101_BAUDRATE,
						(BAUD_RATE_GEN_FREQ / baud))) {
				dev_err(&port->dev, "Baud rate requested not "
						"supported by device\n");
				baud = tty_termios_baud_rate(old_termios);
			}
		}
	}
	/* Report back the resulting baud rate */
	tty_encode_baud_rate(tty, baud, baud);

	/* If the number of data bits is to be updated */
	if ((cflag & CSIZE) != (old_cflag & CSIZE)) {
		cp2101_get_config(port, CP2101_BITS, &bits, 2);
		bits &= ~BITS_DATA_MASK;
		switch (cflag & CSIZE) {
		case CS5:
			bits |= BITS_DATA_5;
			dbg("%s - data bits = 5", __func__);
			break;
		case CS6:
			bits |= BITS_DATA_6;
			dbg("%s - data bits = 6", __func__);
			break;
		case CS7:
			bits |= BITS_DATA_7;
			dbg("%s - data bits = 7", __func__);
			break;
		case CS8:
			bits |= BITS_DATA_8;
			dbg("%s - data bits = 8", __func__);
			break;
		/*case CS9:
			bits |= BITS_DATA_9;
			dbg("%s - data bits = 9", __func__);
			break;*/
		default:
			dev_err(&port->dev, "cp2101 driver does not "
					"support the number of bits requested,"
					" using 8 bit mode\n");
				bits |= BITS_DATA_8;
				break;
		}
		if (cp2101_set_config(port, CP2101_BITS, &bits, 2))
			dev_err(&port->dev, "Number of data bits requested "
					"not supported by device\n");
	}

	if ((cflag & (PARENB|PARODD)) != (old_cflag & (PARENB|PARODD))) {
		cp2101_get_config(port, CP2101_BITS, &bits, 2);
		bits &= ~BITS_PARITY_MASK;
		if (cflag & PARENB) {
			if (cflag & PARODD) {
				bits |= BITS_PARITY_ODD;
				dbg("%s - parity = ODD", __func__);
			} else {
				bits |= BITS_PARITY_EVEN;
				dbg("%s - parity = EVEN", __func__);
			}
		}
		if (cp2101_set_config(port, CP2101_BITS, &bits, 2))
			dev_err(&port->dev, "Parity mode not supported "
					"by device\n");
	}

	if ((cflag & CSTOPB) != (old_cflag & CSTOPB)) {
		cp2101_get_config(port, CP2101_BITS, &bits, 2);
		bits &= ~BITS_STOP_MASK;
		if (cflag & CSTOPB) {
			bits |= BITS_STOP_2;
			dbg("%s - stop bits = 2", __func__);
		} else {
			bits |= BITS_STOP_1;
			dbg("%s - stop bits = 1", __func__);
		}
		if (cp2101_set_config(port, CP2101_BITS, &bits, 2))
			dev_err(&port->dev, "Number of stop bits requested "
					"not supported by device\n");
	}

	if ((cflag & CRTSCTS) != (old_cflag & CRTSCTS)) {
		cp2101_get_config(port, CP2101_MODEMCTL, modem_ctl, 16);
		dbg("%s - read modem controls = 0x%.4x 0x%.4x 0x%.4x 0x%.4x",
				__func__, modem_ctl[0], modem_ctl[1],
				modem_ctl[2], modem_ctl[3]);

		if (cflag & CRTSCTS) {
			modem_ctl[0] &= ~0x7B;
			modem_ctl[0] |= 0x09;
			modem_ctl[1] = 0x80;
			dbg("%s - flow control = CRTSCTS", __func__);
		} else {
			modem_ctl[0] &= ~0x7B;
			modem_ctl[0] |= 0x01;
			modem_ctl[1] |= 0x40;
			dbg("%s - flow control = NONE", __func__);
		}

		dbg("%s - write modem controls = 0x%.4x 0x%.4x 0x%.4x 0x%.4x",
				__func__, modem_ctl[0], modem_ctl[1],
				modem_ctl[2], modem_ctl[3]);
		cp2101_set_config(port, CP2101_MODEMCTL, modem_ctl, 16);
	}

}

static int cp2101_tiocmset (struct tty_struct *tty, struct file *file,
		unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int control = 0;

	dbg("%s - port %d", __func__, port->number);

	if (set & TIOCM_RTS) {
		control |= CONTROL_RTS;
		control |= CONTROL_WRITE_RTS;
	}
	if (set & TIOCM_DTR) {
		control |= CONTROL_DTR;
		control |= CONTROL_WRITE_DTR;
	}
	if (clear & TIOCM_RTS) {
		control &= ~CONTROL_RTS;
		control |= CONTROL_WRITE_RTS;
	}
	if (clear & TIOCM_DTR) {
		control &= ~CONTROL_DTR;
		control |= CONTROL_WRITE_DTR;
	}

	dbg("%s - control = 0x%.4x", __func__, control);

	return cp2101_set_config(port, CP2101_CONTROL, &control, 2);

}

static int cp2101_tiocmget (struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int control;
	int result;

	dbg("%s - port %d", __func__, port->number);

	cp2101_get_config(port, CP2101_CONTROL, &control, 1);

	result = ((control & CONTROL_DTR) ? TIOCM_DTR : 0)
		|((control & CONTROL_RTS) ? TIOCM_RTS : 0)
		|((control & CONTROL_CTS) ? TIOCM_CTS : 0)
		|((control & CONTROL_DSR) ? TIOCM_DSR : 0)
		|((control & CONTROL_RING)? TIOCM_RI  : 0)
		|((control & CONTROL_DCD) ? TIOCM_CD  : 0);

	dbg("%s - control = 0x%.2x", __func__, control);

	return result;
}

static void cp2101_break_ctl (struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int state;

	dbg("%s - port %d", __func__, port->number);
	if (break_state == 0)
		state = BREAK_OFF;
	else
		state = BREAK_ON;
	dbg("%s - turning break %s", __func__,
			state == BREAK_OFF ? "off" : "on");
	cp2101_set_config(port, CP2101_BREAK, &state, 2);
}

static int cp2101_startup(struct usb_serial *serial)
{
	/* CP2101 buffers behave strangely unless device is reset */
	usb_reset_device(serial->dev);
	return 0;
}

static void cp2101_shutdown(struct usb_serial *serial)
{
	int i;

	dbg("%s", __func__);

	/* Stop reads and writes on all ports */
	for (i = 0; i < serial->num_ports; ++i)
		cp2101_cleanup(serial->port[i]);
}

static int __init cp2101_init(void)
{
	int retval;

	retval = usb_serial_register(&cp2101_device);
	if (retval)
		return retval; /* Failed to register */

	retval = usb_register(&cp2101_driver);
	if (retval) {
		/* Failed to register */
		usb_serial_deregister(&cp2101_device);
		return retval;
	}

	/* Success */
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}

static void __exit cp2101_exit(void)
{
	usb_deregister(&cp2101_driver);
	usb_serial_deregister(&cp2101_device);
}

module_init(cp2101_init);
module_exit(cp2101_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable verbose debugging messages");
