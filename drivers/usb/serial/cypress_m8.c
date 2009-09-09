/*
 * USB Cypress M8 driver
 *
 * 	Copyright (C) 2004
 * 	    Lonnie Mendez (dignome@gmail.com)
 *	Copyright (C) 2003,2004
 *	    Neil Whelchel (koyama@firstlight.net)
 *
 * 	This program is free software; you can redistribute it and/or modify
 * 	it under the terms of the GNU General Public License as published by
 * 	the Free Software Foundation; either version 2 of the License, or
 * 	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this
 * driver
 *
 * See http://geocities.com/i0xox0i for information on this driver and the
 * earthmate usb device.
 *
 *  Lonnie Mendez <dignome@gmail.com>
 *  4-29-2005
 *	Fixed problem where setting or retreiving the serial config would fail
 *	with EPIPE.  Removed CRTS toggling so the driver behaves more like
 *	other usbserial adapters.  Issued new interval of 1ms instead of the
 *	default 10ms.  As a result, transfer speed has been substantially
 *	increased from avg. 850bps to avg. 3300bps.  initial termios has also
 *	been modified.  Cleaned up code and formatting issues so it is more
 *	readable.  Replaced the C++ style comments.
 *
 *  Lonnie Mendez <dignome@gmail.com>
 *  12-15-2004
 *	Incorporated write buffering from pl2303 driver.  Fixed bug with line
 *	handling so both lines are raised in cypress_open. (was dropping rts)
 *      Various code cleanups made as well along with other misc bug fixes.
 *
 *  Lonnie Mendez <dignome@gmail.com>
 *  04-10-2004
 *	Driver modified to support dynamic line settings.  Various improvments
 *      and features.
 *
 *  Neil Whelchel
 *  10-2003
 *	Driver first released.
 *
 */

/* Thanks to Neil Whelchel for writing the first cypress m8 implementation
   for linux. */
/* Thanks to cypress for providing references for the hid reports. */
/* Thanks to Jiang Zhang for providing links and for general help. */
/* Code originates and was built up from ftdi_sio, belkin, pl2303 and others.*/


#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "cypress_m8.h"


#ifdef CONFIG_USB_SERIAL_DEBUG
	static int debug = 1;
#else
	static int debug;
#endif
static int stats;
static int interval;

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.09"
#define DRIVER_AUTHOR "Lonnie Mendez <dignome@gmail.com>, Neil Whelchel <koyama@firstlight.net>"
#define DRIVER_DESC "Cypress USB to Serial Driver"

/* write buffer size defines */
#define CYPRESS_BUF_SIZE	1024
#define CYPRESS_CLOSING_WAIT	(30*HZ)

static struct usb_device_id id_table_earthmate [] = {
	{ USB_DEVICE(VENDOR_ID_DELORME, PRODUCT_ID_EARTHMATEUSB) },
	{ USB_DEVICE(VENDOR_ID_DELORME, PRODUCT_ID_EARTHMATEUSB_LT20) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_cyphidcomrs232 [] = {
	{ USB_DEVICE(VENDOR_ID_CYPRESS, PRODUCT_ID_CYPHIDCOM) },
	{ USB_DEVICE(VENDOR_ID_POWERCOM, PRODUCT_ID_UPS) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_nokiaca42v2 [] = {
	{ USB_DEVICE(VENDOR_ID_DAZZLE, PRODUCT_ID_CA42) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(VENDOR_ID_DELORME, PRODUCT_ID_EARTHMATEUSB) },
	{ USB_DEVICE(VENDOR_ID_DELORME, PRODUCT_ID_EARTHMATEUSB_LT20) },
	{ USB_DEVICE(VENDOR_ID_CYPRESS, PRODUCT_ID_CYPHIDCOM) },
	{ USB_DEVICE(VENDOR_ID_POWERCOM, PRODUCT_ID_UPS) },
	{ USB_DEVICE(VENDOR_ID_DAZZLE, PRODUCT_ID_CA42) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

static struct usb_driver cypress_driver = {
	.name =		"cypress",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id = 	1,
};

enum packet_format {
	packet_format_1,  /* b0:status, b1:payload count */
	packet_format_2   /* b0[7:3]:status, b0[2:0]:payload count */
};

struct cypress_private {
	spinlock_t lock;		   /* private lock */
	int chiptype;			   /* identifier of device, for quirks/etc */
	int bytes_in;			   /* used for statistics */
	int bytes_out;			   /* used for statistics */
	int cmd_count;			   /* used for statistics */
	int cmd_ctrl;			   /* always set this to 1 before issuing a command */
	struct cypress_buf *buf;	   /* write buffer */
	int write_urb_in_use;		   /* write urb in use indicator */
	int write_urb_interval;            /* interval to use for write urb */
	int read_urb_interval;             /* interval to use for read urb */
	int comm_is_ok;                    /* true if communication is (still) ok */
	int termios_initialized;
	__u8 line_control;	   	   /* holds dtr / rts value */
	__u8 current_status;	   	   /* received from last read - info on dsr,cts,cd,ri,etc */
	__u8 current_config;	   	   /* stores the current configuration byte */
	__u8 rx_flags;			   /* throttling - used from whiteheat/ftdi_sio */
	enum packet_format pkt_fmt;	   /* format to use for packet send / receive */
	int get_cfg_unsafe;		   /* If true, the CYPRESS_GET_CONFIG is unsafe */
	int baud_rate;			   /* stores current baud rate in
					      integer form */
	int isthrottled;		   /* if throttled, discard reads */
	wait_queue_head_t delta_msr_wait;  /* used for TIOCMIWAIT */
	char prev_status, diff_status;	   /* used for TIOCMIWAIT */
	/* we pass a pointer to this as the arguement sent to
	   cypress_set_termios old_termios */
	struct ktermios tmp_termios; 	   /* stores the old termios settings */
};

/* write buffer structure */
struct cypress_buf {
	unsigned int	buf_size;
	char		*buf_buf;
	char		*buf_get;
	char		*buf_put;
};

/* function prototypes for the Cypress USB to serial device */
static int  cypress_earthmate_startup(struct usb_serial *serial);
static int  cypress_hidcom_startup(struct usb_serial *serial);
static int  cypress_ca42v2_startup(struct usb_serial *serial);
static void cypress_release(struct usb_serial *serial);
static int  cypress_open(struct tty_struct *tty,
			struct usb_serial_port *port, struct file *filp);
static void cypress_close(struct usb_serial_port *port);
static void cypress_dtr_rts(struct usb_serial_port *port, int on);
static int  cypress_write(struct tty_struct *tty, struct usb_serial_port *port,
			const unsigned char *buf, int count);
static void cypress_send(struct usb_serial_port *port);
static int  cypress_write_room(struct tty_struct *tty);
static int  cypress_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg);
static void cypress_set_termios(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old);
static int  cypress_tiocmget(struct tty_struct *tty, struct file *file);
static int  cypress_tiocmset(struct tty_struct *tty, struct file *file,
			unsigned int set, unsigned int clear);
static int  cypress_chars_in_buffer(struct tty_struct *tty);
static void cypress_throttle(struct tty_struct *tty);
static void cypress_unthrottle(struct tty_struct *tty);
static void cypress_set_dead(struct usb_serial_port *port);
static void cypress_read_int_callback(struct urb *urb);
static void cypress_write_int_callback(struct urb *urb);
/* write buffer functions */
static struct cypress_buf *cypress_buf_alloc(unsigned int size);
static void cypress_buf_free(struct cypress_buf *cb);
static void cypress_buf_clear(struct cypress_buf *cb);
static unsigned int cypress_buf_data_avail(struct cypress_buf *cb);
static unsigned int cypress_buf_space_avail(struct cypress_buf *cb);
static unsigned int cypress_buf_put(struct cypress_buf *cb,
					const char *buf, unsigned int count);
static unsigned int cypress_buf_get(struct cypress_buf *cb,
					char *buf, unsigned int count);


static struct usb_serial_driver cypress_earthmate_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"earthmate",
	},
	.description =			"DeLorme Earthmate USB",
	.usb_driver = 			&cypress_driver,
	.id_table =			id_table_earthmate,
	.num_ports =			1,
	.attach =			cypress_earthmate_startup,
	.release =			cypress_release,
	.open =				cypress_open,
	.close =			cypress_close,
	.dtr_rts =			cypress_dtr_rts,
	.write =			cypress_write,
	.write_room =			cypress_write_room,
	.ioctl =			cypress_ioctl,
	.set_termios =			cypress_set_termios,
	.tiocmget =			cypress_tiocmget,
	.tiocmset =			cypress_tiocmset,
	.chars_in_buffer =		cypress_chars_in_buffer,
	.throttle =		 	cypress_throttle,
	.unthrottle =			cypress_unthrottle,
	.read_int_callback =		cypress_read_int_callback,
	.write_int_callback =		cypress_write_int_callback,
};

static struct usb_serial_driver cypress_hidcom_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"cyphidcom",
	},
	.description =			"HID->COM RS232 Adapter",
	.usb_driver = 			&cypress_driver,
	.id_table =			id_table_cyphidcomrs232,
	.num_ports =			1,
	.attach =			cypress_hidcom_startup,
	.release =			cypress_release,
	.open =				cypress_open,
	.close =			cypress_close,
	.dtr_rts =			cypress_dtr_rts,
	.write =			cypress_write,
	.write_room =			cypress_write_room,
	.ioctl =			cypress_ioctl,
	.set_termios =			cypress_set_termios,
	.tiocmget =			cypress_tiocmget,
	.tiocmset =			cypress_tiocmset,
	.chars_in_buffer =		cypress_chars_in_buffer,
	.throttle =			cypress_throttle,
	.unthrottle =			cypress_unthrottle,
	.read_int_callback =		cypress_read_int_callback,
	.write_int_callback =		cypress_write_int_callback,
};

static struct usb_serial_driver cypress_ca42v2_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"nokiaca42v2",
	},
	.description =			"Nokia CA-42 V2 Adapter",
	.usb_driver = 			&cypress_driver,
	.id_table =			id_table_nokiaca42v2,
	.num_ports =			1,
	.attach =			cypress_ca42v2_startup,
	.release =			cypress_release,
	.open =				cypress_open,
	.close =			cypress_close,
	.dtr_rts =			cypress_dtr_rts,
	.write =			cypress_write,
	.write_room =			cypress_write_room,
	.ioctl =			cypress_ioctl,
	.set_termios =			cypress_set_termios,
	.tiocmget =			cypress_tiocmget,
	.tiocmset =			cypress_tiocmset,
	.chars_in_buffer =		cypress_chars_in_buffer,
	.throttle =			cypress_throttle,
	.unthrottle =			cypress_unthrottle,
	.read_int_callback =		cypress_read_int_callback,
	.write_int_callback =		cypress_write_int_callback,
};

/*****************************************************************************
 * Cypress serial helper functions
 *****************************************************************************/


static int analyze_baud_rate(struct usb_serial_port *port, speed_t new_rate)
{
	struct cypress_private *priv;
	priv = usb_get_serial_port_data(port);

	/*
	 * The general purpose firmware for the Cypress M8 allows for
	 * a maximum speed of 57600bps (I have no idea whether DeLorme
	 * chose to use the general purpose firmware or not), if you
	 * need to modify this speed setting for your own project
	 * please add your own chiptype and modify the code likewise.
	 * The Cypress HID->COM device will work successfully up to
	 * 115200bps (but the actual throughput is around 3kBps).
	 */
	if (port->serial->dev->speed == USB_SPEED_LOW) {
		/*
		 * Mike Isely <isely@pobox.com> 2-Feb-2008: The
		 * Cypress app note that describes this mechanism
		 * states the the low-speed part can't handle more
		 * than 800 bytes/sec, in which case 4800 baud is the
		 * safest speed for a part like that.
		 */
		if (new_rate > 4800) {
			dbg("%s - failed setting baud rate, device incapable "
			    "speed %d", __func__, new_rate);
			return -1;
		}
	}
	switch (priv->chiptype) {
	case CT_EARTHMATE:
		if (new_rate <= 600) {
			/* 300 and 600 baud rates are supported under
			 * the generic firmware, but are not used with
			 * NMEA and SiRF protocols */
			dbg("%s - failed setting baud rate, unsupported speed "
			    "of %d on Earthmate GPS", __func__, new_rate);
			return -1;
		}
		break;
	default:
		break;
	}
	return new_rate;
}


/* This function can either set or retrieve the current serial line settings */
static int cypress_serial_control(struct tty_struct *tty,
	struct usb_serial_port *port, speed_t baud_rate, int data_bits,
	int stop_bits, int parity_enable, int parity_type, int reset,
	int cypress_request_type)
{
	int new_baudrate = 0, retval = 0, tries = 0;
	struct cypress_private *priv;
	__u8 feature_buffer[5];
	unsigned long flags;

	dbg("%s", __func__);

	priv = usb_get_serial_port_data(port);

	if (!priv->comm_is_ok)
		return -ENODEV;

	switch (cypress_request_type) {
	case CYPRESS_SET_CONFIG:
		new_baudrate = priv->baud_rate;
		/* 0 means 'Hang up' so doesn't change the true bit rate */
		if (baud_rate == 0)
			new_baudrate = priv->baud_rate;
		/* Change of speed ? */
		else if (baud_rate != priv->baud_rate) {
			dbg("%s - baud rate is changing", __func__);
			retval = analyze_baud_rate(port, baud_rate);
			if (retval >=  0) {
				new_baudrate = retval;
				dbg("%s - New baud rate set to %d",
				    __func__, new_baudrate);
			}
		}
		dbg("%s - baud rate is being sent as %d",
					__func__, new_baudrate);

		memset(feature_buffer, 0, sizeof(feature_buffer));
		/* fill the feature_buffer with new configuration */
		*((u_int32_t *)feature_buffer) = new_baudrate;
		feature_buffer[4] |= data_bits;   /* assign data bits in 2 bit space ( max 3 ) */
		/* 1 bit gap */
		feature_buffer[4] |= (stop_bits << 3);   /* assign stop bits in 1 bit space */
		feature_buffer[4] |= (parity_enable << 4);   /* assign parity flag in 1 bit space */
		feature_buffer[4] |= (parity_type << 5);   /* assign parity type in 1 bit space */
		/* 1 bit gap */
		feature_buffer[4] |= (reset << 7);   /* assign reset at end of byte, 1 bit space */

		dbg("%s - device is being sent this feature report:",
								__func__);
		dbg("%s - %02X - %02X - %02X - %02X - %02X", __func__,
			feature_buffer[0], feature_buffer[1],
			feature_buffer[2], feature_buffer[3],
			feature_buffer[4]);

		do {
			retval = usb_control_msg(port->serial->dev,
					usb_sndctrlpipe(port->serial->dev, 0),
					HID_REQ_SET_REPORT,
					USB_DIR_OUT | USB_RECIP_INTERFACE | USB_TYPE_CLASS,
					0x0300, 0, feature_buffer,
					sizeof(feature_buffer), 500);

			if (tries++ >= 3)
				break;

		} while (retval != sizeof(feature_buffer) &&
			 retval != -ENODEV);

		if (retval != sizeof(feature_buffer)) {
			dev_err(&port->dev, "%s - failed sending serial "
				"line settings - %d\n", __func__, retval);
			cypress_set_dead(port);
		} else {
			spin_lock_irqsave(&priv->lock, flags);
			priv->baud_rate = new_baudrate;
			priv->current_config = feature_buffer[4];
			spin_unlock_irqrestore(&priv->lock, flags);
			/* If we asked for a speed change encode it */
			if (baud_rate)
				tty_encode_baud_rate(tty,
					new_baudrate, new_baudrate);
		}
	break;
	case CYPRESS_GET_CONFIG:
		if (priv->get_cfg_unsafe) {
			/* Not implemented for this device,
			   and if we try to do it we're likely
			   to crash the hardware. */
			return -ENOTTY;
		}
		dbg("%s - retreiving serial line settings", __func__);
		/* set initial values in feature buffer */
		memset(feature_buffer, 0, sizeof(feature_buffer));

		do {
			retval = usb_control_msg(port->serial->dev,
					usb_rcvctrlpipe(port->serial->dev, 0),
					HID_REQ_GET_REPORT,
					USB_DIR_IN | USB_RECIP_INTERFACE | USB_TYPE_CLASS,
					0x0300, 0, feature_buffer,
					sizeof(feature_buffer), 500);

			if (tries++ >= 3)
				break;
		} while (retval != sizeof(feature_buffer)
						&& retval != -ENODEV);

		if (retval != sizeof(feature_buffer)) {
			dev_err(&port->dev, "%s - failed to retrieve serial "
				"line settings - %d\n", __func__, retval);
			cypress_set_dead(port);
			return retval;
		} else {
			spin_lock_irqsave(&priv->lock, flags);
			/* store the config in one byte, and later
			   use bit masks to check values */
			priv->current_config = feature_buffer[4];
			priv->baud_rate = *((u_int32_t *)feature_buffer);
			spin_unlock_irqrestore(&priv->lock, flags);
		}
	}
	spin_lock_irqsave(&priv->lock, flags);
	++priv->cmd_count;
	spin_unlock_irqrestore(&priv->lock, flags);

	return retval;
} /* cypress_serial_control */


static void cypress_set_dead(struct usb_serial_port *port)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (!priv->comm_is_ok) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}
	priv->comm_is_ok = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	dev_err(&port->dev, "cypress_m8 suspending failing port %d - "
		"interval might be too short\n", port->number);
}


/*****************************************************************************
 * Cypress serial driver functions
 *****************************************************************************/


static int generic_startup(struct usb_serial *serial)
{
	struct cypress_private *priv;
	struct usb_serial_port *port = serial->port[0];

	dbg("%s - port %d", __func__, port->number);

	priv = kzalloc(sizeof(struct cypress_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->comm_is_ok = !0;
	spin_lock_init(&priv->lock);
	priv->buf = cypress_buf_alloc(CYPRESS_BUF_SIZE);
	if (priv->buf == NULL) {
		kfree(priv);
		return -ENOMEM;
	}
	init_waitqueue_head(&priv->delta_msr_wait);

	usb_reset_configuration(serial->dev);

	priv->cmd_ctrl = 0;
	priv->line_control = 0;
	priv->termios_initialized = 0;
	priv->rx_flags = 0;
	/* Default packet format setting is determined by packet size.
	   Anything with a size larger then 9 must have a separate
	   count field since the 3 bit count field is otherwise too
	   small.  Otherwise we can use the slightly more compact
	   format.  This is in accordance with the cypress_m8 serial
	   converter app note. */
	if (port->interrupt_out_size > 9)
		priv->pkt_fmt = packet_format_1;
	else
		priv->pkt_fmt = packet_format_2;

	if (interval > 0) {
		priv->write_urb_interval = interval;
		priv->read_urb_interval = interval;
		dbg("%s - port %d read & write intervals forced to %d",
		    __func__, port->number, interval);
	} else {
		priv->write_urb_interval = port->interrupt_out_urb->interval;
		priv->read_urb_interval = port->interrupt_in_urb->interval;
		dbg("%s - port %d intervals: read=%d write=%d",
		    __func__, port->number,
		    priv->read_urb_interval, priv->write_urb_interval);
	}
	usb_set_serial_port_data(port, priv);

	return 0;
}


static int cypress_earthmate_startup(struct usb_serial *serial)
{
	struct cypress_private *priv;
	struct usb_serial_port *port = serial->port[0];

	dbg("%s", __func__);

	if (generic_startup(serial)) {
		dbg("%s - Failed setting up port %d", __func__,
				port->number);
		return 1;
	}

	priv = usb_get_serial_port_data(port);
	priv->chiptype = CT_EARTHMATE;
	/* All Earthmate devices use the separated-count packet
	   format!  Idiotic. */
	priv->pkt_fmt = packet_format_1;
	if (serial->dev->descriptor.idProduct !=
				cpu_to_le16(PRODUCT_ID_EARTHMATEUSB)) {
		/* The old original USB Earthmate seemed able to
		   handle GET_CONFIG requests; everything they've
		   produced since that time crashes if this command is
		   attempted :-( */
		dbg("%s - Marking this device as unsafe for GET_CONFIG "
		    "commands", __func__);
		priv->get_cfg_unsafe = !0;
	}

	return 0;
} /* cypress_earthmate_startup */


static int cypress_hidcom_startup(struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg("%s", __func__);

	if (generic_startup(serial)) {
		dbg("%s - Failed setting up port %d", __func__,
				serial->port[0]->number);
		return 1;
	}

	priv = usb_get_serial_port_data(serial->port[0]);
	priv->chiptype = CT_CYPHIDCOM;

	return 0;
} /* cypress_hidcom_startup */


static int cypress_ca42v2_startup(struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg("%s", __func__);

	if (generic_startup(serial)) {
		dbg("%s - Failed setting up port %d", __func__,
				serial->port[0]->number);
		return 1;
	}

	priv = usb_get_serial_port_data(serial->port[0]);
	priv->chiptype = CT_CA42V2;

	return 0;
} /* cypress_ca42v2_startup */


static void cypress_release(struct usb_serial *serial)
{
	struct cypress_private *priv;

	dbg("%s - port %d", __func__, serial->port[0]->number);

	/* all open ports are closed at this point */

	priv = usb_get_serial_port_data(serial->port[0]);

	if (priv) {
		cypress_buf_free(priv->buf);
		kfree(priv);
	}
}


static int cypress_open(struct tty_struct *tty,
			struct usb_serial_port *port, struct file *filp)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	unsigned long flags;
	int result = 0;

	dbg("%s - port %d", __func__, port->number);

	if (!priv->comm_is_ok)
		return -EIO;

	/* clear halts before open */
	usb_clear_halt(serial->dev, 0x81);
	usb_clear_halt(serial->dev, 0x02);

	spin_lock_irqsave(&priv->lock, flags);
	/* reset read/write statistics */
	priv->bytes_in = 0;
	priv->bytes_out = 0;
	priv->cmd_count = 0;
	priv->rx_flags = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Set termios */
	result = cypress_write(tty, port, NULL, 0);

	if (result) {
		dev_err(&port->dev,
			"%s - failed setting the control lines - error %d\n",
							__func__, result);
		return result;
	} else
		dbg("%s - success setting the control lines", __func__);

	if (tty)
		cypress_set_termios(tty, port, &priv->tmp_termios);

	/* setup the port and start reading from the device */
	if (!port->interrupt_in_urb) {
		dev_err(&port->dev, "%s - interrupt_in_urb is empty!\n",
			__func__);
		return -1;
	}

	usb_fill_int_urb(port->interrupt_in_urb, serial->dev,
		usb_rcvintpipe(serial->dev, port->interrupt_in_endpointAddress),
		port->interrupt_in_urb->transfer_buffer,
		port->interrupt_in_urb->transfer_buffer_length,
		cypress_read_int_callback, port, priv->read_urb_interval);
	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);

	if (result) {
		dev_err(&port->dev,
			"%s - failed submitting read urb, error %d\n",
							__func__, result);
		cypress_set_dead(port);
	}
	port->port.drain_delay = 256;
	return result;
} /* cypress_open */

static void cypress_dtr_rts(struct usb_serial_port *port, int on)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	/* drop dtr and rts */
	priv = usb_get_serial_port_data(port);
	spin_lock_irq(&priv->lock);
	if (on == 0)
		priv->line_control = 0;
	else 
		priv->line_control = CONTROL_DTR | CONTROL_RTS;
	priv->cmd_ctrl = 1;
	spin_unlock_irq(&priv->lock);
	cypress_write(NULL, port, NULL, 0);
}

static void cypress_close(struct usb_serial_port *port)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);

	/* writing is potentially harmful, lock must be taken */
	mutex_lock(&port->serial->disc_mutex);
	if (port->serial->disconnected) {
		mutex_unlock(&port->serial->disc_mutex);
		return;
	}
	cypress_buf_clear(priv->buf);
	dbg("%s - stopping urbs", __func__);
	usb_kill_urb(port->interrupt_in_urb);
	usb_kill_urb(port->interrupt_out_urb);


	if (stats)
		dev_info(&port->dev, "Statistics: %d Bytes In | %d Bytes Out | %d Commands Issued\n",
			priv->bytes_in, priv->bytes_out, priv->cmd_count);
	mutex_unlock(&port->serial->disc_mutex);
} /* cypress_close */


static int cypress_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d, %d bytes", __func__, port->number, count);

	/* line control commands, which need to be executed immediately,
	   are not put into the buffer for obvious reasons.
	 */
	if (priv->cmd_ctrl) {
		count = 0;
		goto finish;
	}

	if (!count)
		return count;

	spin_lock_irqsave(&priv->lock, flags);
	count = cypress_buf_put(priv->buf, buf, count);
	spin_unlock_irqrestore(&priv->lock, flags);

finish:
	cypress_send(port);

	return count;
} /* cypress_write */


static void cypress_send(struct usb_serial_port *port)
{
	int count = 0, result, offset, actual_size;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	if (!priv->comm_is_ok)
		return;

	dbg("%s - port %d", __func__, port->number);
	dbg("%s - interrupt out size is %d", __func__,
						port->interrupt_out_size);

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->write_urb_in_use) {
		dbg("%s - can't write, urb in use", __func__);
		spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	/* clear buffer */
	memset(port->interrupt_out_urb->transfer_buffer, 0,
						port->interrupt_out_size);

	spin_lock_irqsave(&priv->lock, flags);
	switch (priv->pkt_fmt) {
	default:
	case packet_format_1:
		/* this is for the CY7C64013... */
		offset = 2;
		port->interrupt_out_buffer[0] = priv->line_control;
		break;
	case packet_format_2:
		/* this is for the CY7C63743... */
		offset = 1;
		port->interrupt_out_buffer[0] = priv->line_control;
		break;
	}

	if (priv->line_control & CONTROL_RESET)
		priv->line_control &= ~CONTROL_RESET;

	if (priv->cmd_ctrl) {
		priv->cmd_count++;
		dbg("%s - line control command being issued", __func__);
		spin_unlock_irqrestore(&priv->lock, flags);
		goto send;
	} else
		spin_unlock_irqrestore(&priv->lock, flags);

	count = cypress_buf_get(priv->buf, &port->interrupt_out_buffer[offset],
				port->interrupt_out_size-offset);

	if (count == 0)
		return;

	switch (priv->pkt_fmt) {
	default:
	case packet_format_1:
		port->interrupt_out_buffer[1] = count;
		break;
	case packet_format_2:
		port->interrupt_out_buffer[0] |= count;
	}

	dbg("%s - count is %d", __func__, count);

send:
	spin_lock_irqsave(&priv->lock, flags);
	priv->write_urb_in_use = 1;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->cmd_ctrl)
		actual_size = 1;
	else
		actual_size = count +
			      (priv->pkt_fmt == packet_format_1 ? 2 : 1);

	usb_serial_debug_data(debug, &port->dev, __func__,
		port->interrupt_out_size,
		port->interrupt_out_urb->transfer_buffer);

	usb_fill_int_urb(port->interrupt_out_urb, port->serial->dev,
		usb_sndintpipe(port->serial->dev, port->interrupt_out_endpointAddress),
		port->interrupt_out_buffer, port->interrupt_out_size,
		cypress_write_int_callback, port, priv->write_urb_interval);
	result = usb_submit_urb(port->interrupt_out_urb, GFP_ATOMIC);
	if (result) {
		dev_err(&port->dev,
				"%s - failed submitting write urb, error %d\n",
							__func__, result);
		priv->write_urb_in_use = 0;
		cypress_set_dead(port);
	}

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->cmd_ctrl)
		priv->cmd_ctrl = 0;

	/* do not count the line control and size bytes */
	priv->bytes_out += count;
	spin_unlock_irqrestore(&priv->lock, flags);

	usb_serial_port_softint(port);
} /* cypress_send */


/* returns how much space is available in the soft buffer */
static int cypress_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	int room = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	room = cypress_buf_space_avail(priv->buf);
	spin_unlock_irqrestore(&priv->lock, flags);

	dbg("%s - returns %d", __func__, room);
	return room;
}


static int cypress_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	__u8 status, control;
	unsigned int result = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	control = priv->line_control;
	status = priv->current_status;
	spin_unlock_irqrestore(&priv->lock, flags);

	result = ((control & CONTROL_DTR)        ? TIOCM_DTR : 0)
		| ((control & CONTROL_RTS)       ? TIOCM_RTS : 0)
		| ((status & UART_CTS)        ? TIOCM_CTS : 0)
		| ((status & UART_DSR)        ? TIOCM_DSR : 0)
		| ((status & UART_RI)         ? TIOCM_RI  : 0)
		| ((status & UART_CD)         ? TIOCM_CD  : 0);

	dbg("%s - result = %x", __func__, result);

	return result;
}


static int cypress_tiocmset(struct tty_struct *tty, struct file *file,
			       unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->line_control |= CONTROL_RTS;
	if (set & TIOCM_DTR)
		priv->line_control |= CONTROL_DTR;
	if (clear & TIOCM_RTS)
		priv->line_control &= ~CONTROL_RTS;
	if (clear & TIOCM_DTR)
		priv->line_control &= ~CONTROL_DTR;
	priv->cmd_ctrl = 1;
	spin_unlock_irqrestore(&priv->lock, flags);

	return cypress_write(tty, port, NULL, 0);
}


static int cypress_ioctl(struct tty_struct *tty, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);

	dbg("%s - port %d, cmd 0x%.4x", __func__, port->number, cmd);

	switch (cmd) {
	/* This code comes from drivers/char/serial.c and ftdi_sio.c */
	case TIOCMIWAIT:
		while (priv != NULL) {
			interruptible_sleep_on(&priv->delta_msr_wait);
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			else {
				char diff = priv->diff_status;
				if (diff == 0)
					return -EIO; /* no change => error */

				/* consume all events */
				priv->diff_status = 0;

				/* return 0 if caller wanted to know about
				   these bits */
				if (((arg & TIOCM_RNG) && (diff & UART_RI)) ||
				    ((arg & TIOCM_DSR) && (diff & UART_DSR)) ||
				    ((arg & TIOCM_CD) && (diff & UART_CD)) ||
				    ((arg & TIOCM_CTS) && (diff & UART_CTS)))
					return 0;
				/* otherwise caller can't care less about what
				 * happened, and so we continue to wait for
				 * more events.
				 */
			}
		}
		return 0;
	default:
		break;
	}
	dbg("%s - arg not supported - it was 0x%04x - check include/asm/ioctls.h", __func__, cmd);
	return -ENOIOCTLCMD;
} /* cypress_ioctl */


static void cypress_set_termios(struct tty_struct *tty,
	struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct cypress_private *priv = usb_get_serial_port_data(port);
	int data_bits, stop_bits, parity_type, parity_enable;
	unsigned cflag, iflag;
	unsigned long flags;
	__u8 oldlines;
	int linechange = 0;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (!priv->termios_initialized) {
		if (priv->chiptype == CT_EARTHMATE) {
			*(tty->termios) = tty_std_termios;
			tty->termios->c_cflag = B4800 | CS8 | CREAD | HUPCL |
				CLOCAL;
			tty->termios->c_ispeed = 4800;
			tty->termios->c_ospeed = 4800;
		} else if (priv->chiptype == CT_CYPHIDCOM) {
			*(tty->termios) = tty_std_termios;
			tty->termios->c_cflag = B9600 | CS8 | CREAD | HUPCL |
				CLOCAL;
			tty->termios->c_ispeed = 9600;
			tty->termios->c_ospeed = 9600;
		} else if (priv->chiptype == CT_CA42V2) {
			*(tty->termios) = tty_std_termios;
			tty->termios->c_cflag = B9600 | CS8 | CREAD | HUPCL |
				CLOCAL;
			tty->termios->c_ispeed = 9600;
			tty->termios->c_ospeed = 9600;
		}
		priv->termios_initialized = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	/* Unsupported features need clearing */
	tty->termios->c_cflag &= ~(CMSPAR|CRTSCTS);

	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;

	/* check if there are new settings */
	if (old_termios) {
		spin_lock_irqsave(&priv->lock, flags);
		priv->tmp_termios = *(tty->termios);
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	/* set number of data bits, parity, stop bits */
	/* when parity is disabled the parity type bit is ignored */

	/* 1 means 2 stop bits, 0 means 1 stop bit */
	stop_bits = cflag & CSTOPB ? 1 : 0;

	if (cflag & PARENB) {
		parity_enable = 1;
		/* 1 means odd parity, 0 means even parity */
		parity_type = cflag & PARODD ? 1 : 0;
	} else
		parity_enable = parity_type = 0;

	switch (cflag & CSIZE) {
	case CS5:
		data_bits = 0;
		break;
	case CS6:
		data_bits = 1;
		break;
	case CS7:
		data_bits = 2;
		break;
	case CS8:
		data_bits = 3;
		break;
	default:
		dev_err(&port->dev, "%s - CSIZE was set, but not CS5-CS8\n",
			__func__);
		data_bits = 3;
	}
	spin_lock_irqsave(&priv->lock, flags);
	oldlines = priv->line_control;
	if ((cflag & CBAUD) == B0) {
		/* drop dtr and rts */
		dbg("%s - dropping the lines, baud rate 0bps", __func__);
		priv->line_control &= ~(CONTROL_DTR | CONTROL_RTS);
	} else
		priv->line_control = (CONTROL_DTR | CONTROL_RTS);
	spin_unlock_irqrestore(&priv->lock, flags);

	dbg("%s - sending %d stop_bits, %d parity_enable, %d parity_type, "
			"%d data_bits (+5)", __func__, stop_bits,
			parity_enable, parity_type, data_bits);

	cypress_serial_control(tty, port, tty_get_baud_rate(tty),
			data_bits, stop_bits,
			parity_enable, parity_type,
			0, CYPRESS_SET_CONFIG);

	/* we perform a CYPRESS_GET_CONFIG so that the current settings are
	 * filled into the private structure this should confirm that all is
	 * working if it returns what we just set */
	cypress_serial_control(tty, port, 0, 0, 0, 0, 0, 0, CYPRESS_GET_CONFIG);

	/* Here we can define custom tty settings for devices; the main tty
	 * termios flag base comes from empeg.c */

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->chiptype == CT_EARTHMATE && priv->baud_rate == 4800) {
		dbg("Using custom termios settings for a baud rate of "
				"4800bps.");
		/* define custom termios settings for NMEA protocol */

		tty->termios->c_iflag /* input modes - */
			&= ~(IGNBRK  /* disable ignore break */
			| BRKINT     /* disable break causes interrupt */
			| PARMRK     /* disable mark parity errors */
			| ISTRIP     /* disable clear high bit of input char */
			| INLCR      /* disable translate NL to CR */
			| IGNCR      /* disable ignore CR */
			| ICRNL      /* disable translate CR to NL */
			| IXON);     /* disable enable XON/XOFF flow control */

		tty->termios->c_oflag /* output modes */
			&= ~OPOST;    /* disable postprocess output char */

		tty->termios->c_lflag /* line discipline modes */
			&= ~(ECHO     /* disable echo input characters */
			| ECHONL      /* disable echo new line */
			| ICANON      /* disable erase, kill, werase, and rprnt
					 special characters */
			| ISIG        /* disable interrupt, quit, and suspend
					 special characters */
			| IEXTEN);    /* disable non-POSIX special characters */
	} /* CT_CYPHIDCOM: Application should handle this for device */

	linechange = (priv->line_control != oldlines);
	spin_unlock_irqrestore(&priv->lock, flags);

	/* if necessary, set lines */
	if (linechange) {
		priv->cmd_ctrl = 1;
		cypress_write(tty, port, NULL, 0);
	}
} /* cypress_set_termios */


/* returns amount of data still left in soft buffer */
static int cypress_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	int chars = 0;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	chars = cypress_buf_data_avail(priv->buf);
	spin_unlock_irqrestore(&priv->lock, flags);

	dbg("%s - returns %d", __func__, chars);
	return chars;
}


static void cypress_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	priv->rx_flags = THROTTLED;
	spin_unlock_irqrestore(&priv->lock, flags);
}


static void cypress_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	int actually_throttled, result;
	unsigned long flags;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	actually_throttled = priv->rx_flags & ACTUALLY_THROTTLED;
	priv->rx_flags = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (!priv->comm_is_ok)
		return;

	if (actually_throttled) {
		port->interrupt_in_urb->dev = port->serial->dev;

		result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
		if (result) {
			dev_err(&port->dev, "%s - failed submitting read urb, "
					"error %d\n", __func__, result);
			cypress_set_dead(port);
		}
	}
}


static void cypress_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	struct tty_struct *tty;
	unsigned char *data = urb->transfer_buffer;
	unsigned long flags;
	char tty_flag = TTY_NORMAL;
	int havedata = 0;
	int bytes = 0;
	int result;
	int i = 0;
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	switch (status) {
	case 0: /* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* precursor to disconnect so just go away */
		return;
	case -EPIPE:
		usb_clear_halt(port->serial->dev, 0x81);
		break;
	default:
		/* something ugly is going on... */
		dev_err(&urb->dev->dev,
			"%s - unexpected nonzero read status received: %d\n",
							__func__, status);
		cypress_set_dead(port);
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->rx_flags & THROTTLED) {
		dbg("%s - now throttling", __func__);
		priv->rx_flags |= ACTUALLY_THROTTLED;
		spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	tty = tty_port_tty_get(&port->port);
	if (!tty) {
		dbg("%s - bad tty pointer - exiting", __func__);
		return;
	}

	spin_lock_irqsave(&priv->lock, flags);
	result = urb->actual_length;
	switch (priv->pkt_fmt) {
	default:
	case packet_format_1:
		/* This is for the CY7C64013... */
		priv->current_status = data[0] & 0xF8;
		bytes = data[1] + 2;
		i = 2;
		if (bytes > 2)
			havedata = 1;
		break;
	case packet_format_2:
		/* This is for the CY7C63743... */
		priv->current_status = data[0] & 0xF8;
		bytes = (data[0] & 0x07) + 1;
		i = 1;
		if (bytes > 1)
			havedata = 1;
		break;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	if (result < bytes) {
		dbg("%s - wrong packet size - received %d bytes but packet "
		    "said %d bytes", __func__, result, bytes);
		goto continue_read;
	}

	usb_serial_debug_data(debug, &port->dev, __func__,
						urb->actual_length, data);

	spin_lock_irqsave(&priv->lock, flags);
	/* check to see if status has changed */
	if (priv->current_status != priv->prev_status) {
		priv->diff_status |= priv->current_status ^
			priv->prev_status;
		wake_up_interruptible(&priv->delta_msr_wait);
		priv->prev_status = priv->current_status;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	/* hangup, as defined in acm.c... this might be a bad place for it
	 * though */
	if (tty && !(tty->termios->c_cflag & CLOCAL) &&
			!(priv->current_status & UART_CD)) {
		dbg("%s - calling hangup", __func__);
		tty_hangup(tty);
		goto continue_read;
	}

	/* There is one error bit... I'm assuming it is a parity error
	 * indicator as the generic firmware will set this bit to 1 if a
	 * parity error occurs.
	 * I can not find reference to any other error events. */
	spin_lock_irqsave(&priv->lock, flags);
	if (priv->current_status & CYP_ERROR) {
		spin_unlock_irqrestore(&priv->lock, flags);
		tty_flag = TTY_PARITY;
		dbg("%s - Parity Error detected", __func__);
	} else
		spin_unlock_irqrestore(&priv->lock, flags);

	/* process read if there is data other than line status */
	if (tty && (bytes > i)) {
		bytes = tty_buffer_request_room(tty, bytes);
		for (; i < bytes ; ++i) {
			dbg("pushing byte number %d - %d - %c", i, data[i],
					data[i]);
			tty_insert_flip_char(tty, data[i], tty_flag);
		}
		tty_flip_buffer_push(tty);
	}

	spin_lock_irqsave(&priv->lock, flags);
	/* control and status byte(s) are also counted */
	priv->bytes_in += bytes;
	spin_unlock_irqrestore(&priv->lock, flags);

continue_read:
	tty_kref_put(tty);

	/* Continue trying to always read... unless the port has closed. */

	if (port->port.count > 0 && priv->comm_is_ok) {
		usb_fill_int_urb(port->interrupt_in_urb, port->serial->dev,
				usb_rcvintpipe(port->serial->dev,
					port->interrupt_in_endpointAddress),
				port->interrupt_in_urb->transfer_buffer,
				port->interrupt_in_urb->transfer_buffer_length,
				cypress_read_int_callback, port,
				priv->read_urb_interval);
		result = usb_submit_urb(port->interrupt_in_urb, GFP_ATOMIC);
		if (result) {
			dev_err(&urb->dev->dev, "%s - failed resubmitting "
					"read urb, error %d\n", __func__,
					result);
			cypress_set_dead(port);
		}
	}

	return;
} /* cypress_read_int_callback */


static void cypress_write_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct cypress_private *priv = usb_get_serial_port_data(port);
	int result;
	int status = urb->status;

	dbg("%s - port %d", __func__, port->number);

	switch (status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d",
						__func__, status);
		priv->write_urb_in_use = 0;
		return;
	case -EPIPE: /* no break needed; clear halt and resubmit */
		if (!priv->comm_is_ok)
			break;
		usb_clear_halt(port->serial->dev, 0x02);
		/* error in the urb, so we have to resubmit it */
		dbg("%s - nonzero write bulk status received: %d",
			__func__, status);
		port->interrupt_out_urb->transfer_buffer_length = 1;
		port->interrupt_out_urb->dev = port->serial->dev;
		result = usb_submit_urb(port->interrupt_out_urb, GFP_ATOMIC);
		if (!result)
			return;
		dev_err(&urb->dev->dev,
			"%s - failed resubmitting write urb, error %d\n",
							__func__, result);
		cypress_set_dead(port);
		break;
	default:
		dev_err(&urb->dev->dev,
			 "%s - unexpected nonzero write status received: %d\n",
							__func__, status);
		cypress_set_dead(port);
		break;
	}
	priv->write_urb_in_use = 0;

	/* send any buffered data */
	cypress_send(port);
}


/*****************************************************************************
 * Write buffer functions - buffering code from pl2303 used
 *****************************************************************************/

/*
 * cypress_buf_alloc
 *
 * Allocate a circular buffer and all associated memory.
 */

static struct cypress_buf *cypress_buf_alloc(unsigned int size)
{

	struct cypress_buf *cb;


	if (size == 0)
		return NULL;

	cb = kmalloc(sizeof(struct cypress_buf), GFP_KERNEL);
	if (cb == NULL)
		return NULL;

	cb->buf_buf = kmalloc(size, GFP_KERNEL);
	if (cb->buf_buf == NULL) {
		kfree(cb);
		return NULL;
	}

	cb->buf_size = size;
	cb->buf_get = cb->buf_put = cb->buf_buf;

	return cb;

}


/*
 * cypress_buf_free
 *
 * Free the buffer and all associated memory.
 */

static void cypress_buf_free(struct cypress_buf *cb)
{
	if (cb) {
		kfree(cb->buf_buf);
		kfree(cb);
	}
}


/*
 * cypress_buf_clear
 *
 * Clear out all data in the circular buffer.
 */

static void cypress_buf_clear(struct cypress_buf *cb)
{
	if (cb != NULL)
		cb->buf_get = cb->buf_put;
		/* equivalent to a get of all data available */
}


/*
 * cypress_buf_data_avail
 *
 * Return the number of bytes of data available in the circular
 * buffer.
 */

static unsigned int cypress_buf_data_avail(struct cypress_buf *cb)
{
	if (cb != NULL)
		return (cb->buf_size + cb->buf_put - cb->buf_get)
							% cb->buf_size;
	else
		return 0;
}


/*
 * cypress_buf_space_avail
 *
 * Return the number of bytes of space available in the circular
 * buffer.
 */

static unsigned int cypress_buf_space_avail(struct cypress_buf *cb)
{
	if (cb != NULL)
		return (cb->buf_size + cb->buf_get - cb->buf_put - 1)
							% cb->buf_size;
	else
		return 0;
}


/*
 * cypress_buf_put
 *
 * Copy data data from a user buffer and put it into the circular buffer.
 * Restrict to the amount of space available.
 *
 * Return the number of bytes copied.
 */

static unsigned int cypress_buf_put(struct cypress_buf *cb, const char *buf,
	unsigned int count)
{

	unsigned int len;


	if (cb == NULL)
		return 0;

	len  = cypress_buf_space_avail(cb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = cb->buf_buf + cb->buf_size - cb->buf_put;
	if (count > len) {
		memcpy(cb->buf_put, buf, len);
		memcpy(cb->buf_buf, buf+len, count - len);
		cb->buf_put = cb->buf_buf + count - len;
	} else {
		memcpy(cb->buf_put, buf, count);
		if (count < len)
			cb->buf_put += count;
		else /* count == len */
			cb->buf_put = cb->buf_buf;
	}

	return count;

}


/*
 * cypress_buf_get
 *
 * Get data from the circular buffer and copy to the given buffer.
 * Restrict to the amount of data available.
 *
 * Return the number of bytes copied.
 */

static unsigned int cypress_buf_get(struct cypress_buf *cb, char *buf,
	unsigned int count)
{

	unsigned int len;


	if (cb == NULL)
		return 0;

	len = cypress_buf_data_avail(cb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = cb->buf_buf + cb->buf_size - cb->buf_get;
	if (count > len) {
		memcpy(buf, cb->buf_get, len);
		memcpy(buf+len, cb->buf_buf, count - len);
		cb->buf_get = cb->buf_buf + count - len;
	} else {
		memcpy(buf, cb->buf_get, count);
		if (count < len)
			cb->buf_get += count;
		else /* count == len */
			cb->buf_get = cb->buf_buf;
	}

	return count;

}

/*****************************************************************************
 * Module functions
 *****************************************************************************/

static int __init cypress_init(void)
{
	int retval;

	dbg("%s", __func__);

	retval = usb_serial_register(&cypress_earthmate_device);
	if (retval)
		goto failed_em_register;
	retval = usb_serial_register(&cypress_hidcom_device);
	if (retval)
		goto failed_hidcom_register;
	retval = usb_serial_register(&cypress_ca42v2_device);
	if (retval)
		goto failed_ca42v2_register;
	retval = usb_register(&cypress_driver);
	if (retval)
		goto failed_usb_register;

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");
	return 0;

failed_usb_register:
	usb_serial_deregister(&cypress_ca42v2_device);
failed_ca42v2_register:
	usb_serial_deregister(&cypress_hidcom_device);
failed_hidcom_register:
	usb_serial_deregister(&cypress_earthmate_device);
failed_em_register:
	return retval;
}


static void __exit cypress_exit(void)
{
	dbg("%s", __func__);

	usb_deregister(&cypress_driver);
	usb_serial_deregister(&cypress_earthmate_device);
	usb_serial_deregister(&cypress_hidcom_device);
	usb_serial_deregister(&cypress_ca42v2_device);
}


module_init(cypress_init);
module_exit(cypress_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
module_param(stats, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(stats, "Enable statistics or not");
module_param(interval, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(interval, "Overrides interrupt interval");
