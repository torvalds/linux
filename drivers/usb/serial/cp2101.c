/*
 * Silicon Laboratories CP2101/CP2102 USB to RS232 serial adaptor driver
 *
 * Copyright (C) 2005 Craig Shelley (craig@microtron.org.uk)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <asm/uaccess.h>
#include "usb-serial.h"

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.03"
#define DRIVER_DESC "Silicon Labs CP2101/CP2102 RS232 serial adaptor driver"

/*
 * Function Prototypes
 */
static int cp2101_open(struct usb_serial_port*, struct file*);
static void cp2101_cleanup(struct usb_serial_port*);
static void cp2101_close(struct usb_serial_port*, struct file*);
static void cp2101_get_termios(struct usb_serial_port*);
static void cp2101_set_termios(struct usb_serial_port*, struct termios*);
static void cp2101_break_ctl(struct usb_serial_port*, int);
static int cp2101_startup (struct usb_serial *);
static void cp2101_shutdown(struct usb_serial*);


static int debug;

static struct usb_device_id id_table [] = {
	{USB_DEVICE(0x10c4, 0xea60) },	/*Silicon labs factory default*/
	{USB_DEVICE(0x10ab, 0x10c5) },	/*Siemens MC60 Cable*/
	{ } /* Terminating Entry*/
};

MODULE_DEVICE_TABLE (usb, id_table);

static struct usb_driver cp2101_driver = {
	.owner		= THIS_MODULE,
	.name		= "CP2101",
	.probe		= usb_serial_probe,
	.disconnect	= usb_serial_disconnect,
	.id_table	= id_table,
};

static struct usb_serial_device_type cp2101_device = {
	.owner			= THIS_MODULE,
	.name			= "CP2101",
	.id_table		= id_table,
	.num_interrupt_in	= 0,
	.num_bulk_in		= 0,
	.num_bulk_out		= 0,
	.num_ports		= 1,
	.open			= cp2101_open,
	.close			= cp2101_close,
	.break_ctl		= cp2101_break_ctl,
	.set_termios		= cp2101_set_termios,
	.attach			= cp2101_startup,
	.shutdown		= cp2101_shutdown,
};

/*Config request types*/
#define REQTYPE_HOST_TO_DEVICE	0x41
#define REQTYPE_DEVICE_TO_HOST	0xc1

/*Config SET requests. To GET, add 1 to the request number*/
#define CP2101_UART 		0x00	/*Enable / Disable*/
#define CP2101_BAUDRATE		0x01	/*(BAUD_RATE_GEN_FREQ / baudrate)*/
#define CP2101_BITS		0x03	/*0x(0)(data bits)(parity)(stop bits)*/
#define CP2101_BREAK		0x05	/*On / Off*/
#define CP2101_DTRRTS		0x07	/*101 / 202  ???*/
#define CP2101_CONFIG_16	0x13	/*16 bytes of config data ???*/
#define CP2101_CONFIG_6		0x19	/*6 bytes of config data ???*/

/*CP2101_UART*/
#define UART_ENABLE		0x0001
#define UART_DISABLE		0x0000

/*CP2101_BAUDRATE*/
#define BAUD_RATE_GEN_FREQ	0x384000

/*CP2101_BITS*/
#define BITS_DATA_MASK		0X0f00
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
#define BREAK_ON		0x0000
#define BREAK_OFF		0x0001


static int cp2101_get_config(struct usb_serial_port* port, u8 request)
{
	struct usb_serial *serial = port->serial;
	unsigned char buf[4];
	unsigned int value;
	int result, i;

	/*For get requests, the request number must be incremented*/
	request++;

	/*Issue the request, attempting to read 4 bytes*/
	result = usb_control_msg (serial->dev,usb_rcvctrlpipe (serial->dev, 0),
				request, REQTYPE_DEVICE_TO_HOST, 0x0000,
				0, buf, 4, 300);

	if (result < 0) {
		dev_err(&port->dev, "%s - Unable to send config request, "
				"request=0x%x result=%d\n",
				__FUNCTION__, request, result);
		return result;
	}

	/*Assemble each byte read into an integer value*/
	value = 0;
	for (i=0; i<4 && i<result; i++)
		value |= (buf[i] << (i * 8));

	dbg( " %s - request=0x%x result=%d value=0x%x",
			__FUNCTION__, request, result, value);

	return value;
}

static int cp2101_set_config(struct usb_serial_port* port, u8 request, u16 value)
{
	struct usb_serial *serial = port->serial;
	int result;
	result = usb_control_msg (serial->dev, usb_sndctrlpipe(serial->dev, 0),
			request, REQTYPE_HOST_TO_DEVICE, value,
			0, NULL, 0, 300);

	if (result <0) {
		dev_err(&port->dev, "%s - Unable to send config request, "
				"request=0x%x value=0x%x result=%d\n",
				__FUNCTION__, request, value, result);
		return result;
	}

	dbg(" %s - request=0x%x value=0x%x result=%d",
			__FUNCTION__, request, value, result);

	return 0;
}

static int cp2101_open (struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	int result;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (cp2101_set_config(port, CP2101_UART, UART_ENABLE)) {
		dev_err(&port->dev, "%s - Unable to enable UART\n",
				__FUNCTION__);
		return -EPROTO;
	}

	/* Start reading from the device */
	usb_fill_bulk_urb (port->read_urb, serial->dev,
			usb_rcvbulkpipe(serial->dev,
			port->bulk_in_endpointAddress),
			port->read_urb->transfer_buffer,
			port->read_urb->transfer_buffer_length,
			serial->type->read_bulk_callback,
			port);
	result = usb_submit_urb(port->read_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "%s - failed resubmitting read urb, "
				"error %d\n", __FUNCTION__, result);
		return result;
	}

	/*Configure the termios structure*/
	cp2101_get_termios(port);

	return 0;
}

static void cp2101_cleanup (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (serial->dev) {
		/* shutdown any bulk reads that might be going on */
		if (serial->num_bulk_out)
			usb_kill_urb(port->write_urb);
		if (serial->num_bulk_in)
			usb_kill_urb(port->read_urb);
	}
}

static void cp2101_close (struct usb_serial_port *port, struct file * filp)
{
	dbg("%s - port %d", __FUNCTION__, port->number);

	/* shutdown our urbs */
	dbg("%s - shutting down urbs", __FUNCTION__);
	usb_kill_urb(port->write_urb);
	usb_kill_urb(port->read_urb);

	cp2101_set_config(port, CP2101_UART, UART_DISABLE);
}

/* cp2101_get_termios*/
/* Reads the baud rate, data bits, parity and stop bits from the device*/
/* Corrects any unsupported values*/
/* Configures the termios structure to reflect the state of the device*/
static void cp2101_get_termios (struct usb_serial_port *port)
{
	unsigned int cflag;
	int baud;
	int bits;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if ((!port->tty) || (!port->tty->termios)) {
		dbg("%s - no tty structures", __FUNCTION__);
		return;
	}
	cflag = port->tty->termios->c_cflag;

	baud = cp2101_get_config(port, CP2101_BAUDRATE);
	/*Convert to baudrate*/
	if (baud)
		baud = BAUD_RATE_GEN_FREQ / baud;

	dbg("%s - baud rate = %d", __FUNCTION__, baud);
	cflag &= ~CBAUD;
	switch (baud) {
		/* The baud rates which are commented out below
		 * appear to be supported by the device
		 * but are non-standard
		 */
		case 600:	cflag |= B600;		break;
		case 1200:	cflag |= B1200;		break;
		case 1800:	cflag |= B1800;		break;
		case 2400:	cflag |= B2400;		break;
		case 4800:	cflag |= B4800;		break;
		/*case 7200:	cflag |= B7200;		break;*/
		case 9600:	cflag |= B9600;		break;
		/*case 14400:	cflag |= B14400;	break;*/
		case 19200:	cflag |= B19200;	break;
		/*case 28800:	cflag |= B28800;	break;*/
		case 38400:	cflag |= B38400;	break;
		/*case 55854:	cflag |= B55054;	break;*/
		case 57600:	cflag |= B57600;	break;
		case 115200:	cflag |= B115200;	break;
		/*case 127117:	cflag |= B127117;	break;*/
		case 230400:	cflag |= B230400;	break;
		case 460800:	cflag |= B460800;	break;
		case 921600:	cflag |= B921600;	break;
		/*case 3686400:	cflag |= B3686400;	break;*/
		default:
			dbg("%s - Baud rate is not supported, "
					"using 9600 baud", __FUNCTION__);
			cflag |= B9600;
			cp2101_set_config(port, CP2101_BAUDRATE,
					(BAUD_RATE_GEN_FREQ/9600));
			break;
	}

	bits = cp2101_get_config(port, CP2101_BITS);
	cflag &= ~CSIZE;
	switch(bits & BITS_DATA_MASK) {
		case BITS_DATA_6:
			dbg("%s - data bits = 6", __FUNCTION__);
			cflag |= CS6;
			break;
		case BITS_DATA_7:
			dbg("%s - data bits = 7", __FUNCTION__);
			cflag |= CS7;
			break;
		case BITS_DATA_8:
			dbg("%s - data bits = 8", __FUNCTION__);
			cflag |= CS8;
			break;
		case BITS_DATA_9:
			dbg("%s - data bits = 9 (not supported, "
					"using 8 data bits)", __FUNCTION__);
			cflag |= CS8;
			bits &= ~BITS_DATA_MASK;
			bits |= BITS_DATA_8;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
		default:
			dbg("%s - Unknown number of data bits, "
					"using 8", __FUNCTION__);
			cflag |= CS8;
			bits &= ~BITS_DATA_MASK;
			bits |= BITS_DATA_8;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
	}

	switch(bits & BITS_PARITY_MASK) {
		case BITS_PARITY_NONE:
			dbg("%s - parity = NONE", __FUNCTION__);
			cflag &= ~PARENB;
			break;
		case BITS_PARITY_ODD:
			dbg("%s - parity = ODD", __FUNCTION__);
			cflag |= (PARENB|PARODD);
			break;
		case BITS_PARITY_EVEN:
			dbg("%s - parity = EVEN", __FUNCTION__);
			cflag &= ~PARODD;
			cflag |= PARENB;
			break;
		case BITS_PARITY_MARK:
			dbg("%s - parity = MARK (not supported, "
					"disabling parity)", __FUNCTION__);
			cflag &= ~PARENB;
			bits &= ~BITS_PARITY_MASK;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
		case BITS_PARITY_SPACE:
			dbg("%s - parity = SPACE (not supported, "
					"disabling parity)", __FUNCTION__);
			cflag &= ~PARENB;
			bits &= ~BITS_PARITY_MASK;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
		default:
			dbg("%s - Unknown parity mode, "
					"disabling parity", __FUNCTION__);
			cflag &= ~PARENB;
			bits &= ~BITS_PARITY_MASK;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
	}

	cflag &= ~CSTOPB;
	switch(bits & BITS_STOP_MASK) {
		case BITS_STOP_1:
			dbg("%s - stop bits = 1", __FUNCTION__);
			break;
		case BITS_STOP_1_5:
			dbg("%s - stop bits = 1.5 (not supported, "
					"using 1 stop bit", __FUNCTION__);
			bits &= ~BITS_STOP_MASK;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
		case BITS_STOP_2:
			dbg("%s - stop bits = 2", __FUNCTION__);
			cflag |= CSTOPB;
			break;
		default:
			dbg("%s - Unknown number of stop bits, "
					"using 1 stop bit", __FUNCTION__);
			bits &= ~BITS_STOP_MASK;
			cp2101_set_config(port, CP2101_BITS, bits);
			break;
	}

	port->tty->termios->c_cflag = cflag;
}

static void cp2101_set_termios (struct usb_serial_port *port,
		struct termios *old_termios)
{
	unsigned int cflag, old_cflag=0;
	int baud=0;
	int bits;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if ((!port->tty) || (!port->tty->termios)) {
		dbg("%s - no tty structures", __FUNCTION__);
		return;
	}
	cflag = port->tty->termios->c_cflag;

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
				(RELEVANT_IFLAG(port->tty->termios->c_iflag)
				== RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("%s - nothing to change...", __FUNCTION__);
			return;
		}

		old_cflag = old_termios->c_cflag;
	}

	/* If the baud rate is to be updated*/
	if ((cflag & CBAUD) != (old_cflag & CBAUD)) {
		switch (cflag & CBAUD) {
			/* The baud rates which are commented out below
			 * appear to be supported by the device
			 * but are non-standard
			 */
			case B0:	baud = 0;	break;
			case B600:	baud = 600;	break;
			case B1200:	baud = 1200;	break;
			case B1800:	baud = 1800;	break;
			case B2400:	baud = 2400;	break;
			case B4800:	baud = 4800;	break;
			/*case B7200:	baud = 7200;	break;*/
			case B9600:	baud = 9600;	break;
			/*ase B14400:	baud = 14400;	break;*/
			case B19200:	baud = 19200;	break;
			/*case B28800:	baud = 28800;	break;*/
			case B38400:	baud = 38400;	break;
			/*case B55854:	baud = 55054;	break;*/
			case B57600:	baud = 57600;	break;
			case B115200:	baud = 115200;	break;
			/*case B127117:	baud = 127117;	break;*/
			case B230400:	baud = 230400;	break;
			case B460800:	baud = 460800;	break;
			case B921600:	baud = 921600;	break;
			/*case B3686400:	baud = 3686400;	break;*/
			default:
				dev_err(&port->dev, "cp2101 driver does not "
					"support the baudrate requested\n");
				break;
		}

		if (baud) {
			dbg("%s - Setting baud rate to %d baud", __FUNCTION__,
					baud);
			if (cp2101_set_config(port, CP2101_BAUDRATE,
						(BAUD_RATE_GEN_FREQ / baud)))
				dev_err(&port->dev, "Baud rate requested not "
						"supported by device\n");
		}
	}

	/*If the number of data bits is to be updated*/
	if ((cflag & CSIZE) != (old_cflag & CSIZE)) {
		bits = cp2101_get_config(port, CP2101_BITS);
		bits &= ~BITS_DATA_MASK;
		switch (cflag & CSIZE) {
			case CS6:
				bits |= BITS_DATA_6;
				dbg("%s - data bits = 6", __FUNCTION__);
				break;
			case CS7:
				bits |= BITS_DATA_7;
				dbg("%s - data bits = 7", __FUNCTION__);
				break;
			case CS8:
				bits |= BITS_DATA_8;
				dbg("%s - data bits = 8", __FUNCTION__);
				break;
			/*case CS9:
			 	bits |= BITS_DATA_9;
				dbg("%s - data bits = 9", __FUNCTION__);
				break;*/
			default:
				dev_err(&port->dev, "cp2101 driver does not "
					"support the number of bits requested,"
					" using 8 bit mode\n");
				bits |= BITS_DATA_8;
				break;
		}
		if (cp2101_set_config(port, CP2101_BITS, bits))
			dev_err(&port->dev, "Number of data bits requested "
					"not supported by device\n");
	}

	if ((cflag & (PARENB|PARODD)) != (old_cflag & (PARENB|PARODD))) {
		bits = cp2101_get_config(port, CP2101_BITS);
		bits &= ~BITS_PARITY_MASK;
		if (cflag & PARENB) {
			if (cflag & PARODD) {
				bits |= BITS_PARITY_ODD;
				dbg("%s - parity = ODD", __FUNCTION__);
			} else {
				bits |= BITS_PARITY_EVEN;
				dbg("%s - parity = EVEN", __FUNCTION__);
			}
		}
		if (cp2101_set_config(port, CP2101_BITS, bits))
			dev_err(&port->dev, "Parity mode not supported "
					"by device\n");
	}

	if ((cflag & CSTOPB) != (old_cflag & CSTOPB)) {
		bits = cp2101_get_config(port, CP2101_BITS);
		bits &= ~BITS_STOP_MASK;
		if (cflag & CSTOPB) {
			bits |= BITS_STOP_2;
			dbg("%s - stop bits = 2", __FUNCTION__);
		} else {
			bits |= BITS_STOP_1;
			dbg("%s - stop bits = 1", __FUNCTION__);
		}
		if (cp2101_set_config(port, CP2101_BITS, bits))
			dev_err(&port->dev, "Number of stop bits requested "
					"not supported by device\n");
	}
}

static void cp2101_break_ctl (struct usb_serial_port *port, int break_state)
{
	u16 state;

	dbg("%s - port %d", __FUNCTION__, port->number);
	if (break_state == 0)
		state = BREAK_OFF;
	else
		state = BREAK_ON;
	dbg("%s - turning break %s", __FUNCTION__,
			state==BREAK_OFF ? "off" : "on");
	cp2101_set_config(port, CP2101_BREAK, state);
}

static int cp2101_startup (struct usb_serial *serial)
{
	/*CP2101 buffers behave strangely unless device is reset*/
	usb_reset_device(serial->dev);
	return 0;
}

static void cp2101_shutdown (struct usb_serial *serial)
{
	int i;

	dbg("%s", __FUNCTION__);

	/* stop reads and writes on all ports */
	for (i=0; i < serial->num_ports; ++i) {
		cp2101_cleanup(serial->port[i]);
	}
}

static int __init cp2101_init (void)
{
	int retval;

	retval = usb_serial_register(&cp2101_device);
	if (retval)
		return retval; /*Failed to register*/

	retval = usb_register(&cp2101_driver);
	if (retval) {
		/*Failed to register*/
		usb_serial_deregister(&cp2101_device);
		return retval;
	}

	/*Success*/
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}

static void __exit cp2101_exit (void)
{
	usb_deregister (&cp2101_driver);
	usb_serial_deregister (&cp2101_device);
}

module_init(cp2101_init);
module_exit(cp2101_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable verbose debugging messages");
