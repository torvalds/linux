/*
  Keyspan USB to Serial Converter driver

  (C) Copyright (C) 2000-2001	Hugh Blemings <hugh@blemings.org>
  (C) Copyright (C) 2002	Greg Kroah-Hartman <greg@kroah.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  See http://blemings.org/hugh/keyspan.html for more information.

  Code in this driver inspired by and in a number of places taken
  from Brian Warner's original Keyspan-PDA driver.

  This driver has been put together with the support of Innosys, Inc.
  and Keyspan, Inc the manufacturers of the Keyspan USB-serial products.
  Thanks Guys :)

  Thanks to Paulus for miscellaneous tidy ups, some largish chunks
  of much nicer and/or completely new code and (perhaps most uniquely)
  having the patience to sit down and explain why and where he'd changed
  stuff.

  Tip 'o the hat to IBM (and previously Linuxcare :) for supporting
  staff in their work on open source projects.
*/


#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/firmware.h>
#include <linux/ihex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include "keyspan.h"

static bool debug;

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.1.5"
#define DRIVER_AUTHOR "Hugh Blemings <hugh@misc.nu"
#define DRIVER_DESC "Keyspan USB to Serial Converter Driver"

#define INSTAT_BUFLEN	32
#define GLOCONT_BUFLEN	64
#define INDAT49W_BUFLEN	512

	/* Per device and per port private data */
struct keyspan_serial_private {
	const struct keyspan_device_details	*device_details;

	struct urb	*instat_urb;
	char		instat_buf[INSTAT_BUFLEN];

	/* added to support 49wg, where data from all 4 ports comes in
	   on 1 EP and high-speed supported */
	struct urb	*indat_urb;
	char		indat_buf[INDAT49W_BUFLEN];

	/* XXX this one probably will need a lock */
	struct urb	*glocont_urb;
	char		glocont_buf[GLOCONT_BUFLEN];
	char		ctrl_buf[8];	/* for EP0 control message */
};

struct keyspan_port_private {
	/* Keep track of which input & output endpoints to use */
	int		in_flip;
	int		out_flip;

	/* Keep duplicate of device details in each port
	   structure as well - simplifies some of the
	   callback functions etc. */
	const struct keyspan_device_details	*device_details;

	/* Input endpoints and buffer for this port */
	struct urb	*in_urbs[2];
	char		in_buffer[2][64];
	/* Output endpoints and buffer for this port */
	struct urb	*out_urbs[2];
	char		out_buffer[2][64];

	/* Input ack endpoint */
	struct urb	*inack_urb;
	char		inack_buffer[1];

	/* Output control endpoint */
	struct urb	*outcont_urb;
	char		outcont_buffer[64];

	/* Settings for the port */
	int		baud;
	int		old_baud;
	unsigned int	cflag;
	unsigned int	old_cflag;
	enum		{flow_none, flow_cts, flow_xon} flow_control;
	int		rts_state;	/* Handshaking pins (outputs) */
	int		dtr_state;
	int		cts_state;	/* Handshaking pins (inputs) */
	int		dsr_state;
	int		dcd_state;
	int		ri_state;
	int		break_on;

	unsigned long	tx_start_time[2];
	int		resend_cont;	/* need to resend control packet */
};

/* Include Keyspan message headers.  All current Keyspan Adapters
   make use of one of five message formats which are referred
   to as USA-26, USA-28, USA-49, USA-90, USA-67 by Keyspan and
   within this driver. */
#include "keyspan_usa26msg.h"
#include "keyspan_usa28msg.h"
#include "keyspan_usa49msg.h"
#include "keyspan_usa90msg.h"
#include "keyspan_usa67msg.h"


module_usb_serial_driver(serial_drivers, keyspan_ids_combined);

static void keyspan_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private 	*p_priv;

	p_priv = usb_get_serial_port_data(port);

	if (break_state == -1)
		p_priv->break_on = 1;
	else
		p_priv->break_on = 0;

	keyspan_send_setup(port, 0);
}


static void keyspan_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	int				baud_rate, device_port;
	struct keyspan_port_private 	*p_priv;
	const struct keyspan_device_details	*d_details;
	unsigned int 			cflag;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;
	cflag = tty->termios->c_cflag;
	device_port = port->number - port->serial->minor;

	/* Baud rate calculation takes baud rate as an integer
	   so other rates can be generated if desired. */
	baud_rate = tty_get_baud_rate(tty);
	/* If no match or invalid, don't change */
	if (d_details->calculate_baud_rate(baud_rate, d_details->baudclk,
				NULL, NULL, NULL, device_port) == KEYSPAN_BAUD_RATE_OK) {
		/* FIXME - more to do here to ensure rate changes cleanly */
		/* FIXME - calcuate exact rate from divisor ? */
		p_priv->baud = baud_rate;
	} else
		baud_rate = tty_termios_baud_rate(old_termios);

	tty_encode_baud_rate(tty, baud_rate, baud_rate);
	/* set CTS/RTS handshake etc. */
	p_priv->cflag = cflag;
	p_priv->flow_control = (cflag & CRTSCTS) ? flow_cts : flow_none;

	/* Mark/Space not supported */
	tty->termios->c_cflag &= ~CMSPAR;

	keyspan_send_setup(port, 0);
}

static int keyspan_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private *p_priv = usb_get_serial_port_data(port);
	unsigned int			value;

	value = ((p_priv->rts_state) ? TIOCM_RTS : 0) |
		((p_priv->dtr_state) ? TIOCM_DTR : 0) |
		((p_priv->cts_state) ? TIOCM_CTS : 0) |
		((p_priv->dsr_state) ? TIOCM_DSR : 0) |
		((p_priv->dcd_state) ? TIOCM_CAR : 0) |
		((p_priv->ri_state) ? TIOCM_RNG : 0);

	return value;
}

static int keyspan_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private *p_priv = usb_get_serial_port_data(port);

	if (set & TIOCM_RTS)
		p_priv->rts_state = 1;
	if (set & TIOCM_DTR)
		p_priv->dtr_state = 1;
	if (clear & TIOCM_RTS)
		p_priv->rts_state = 0;
	if (clear & TIOCM_DTR)
		p_priv->dtr_state = 0;
	keyspan_send_setup(port, 0);
	return 0;
}

/* Write function is similar for the four protocols used
   with only a minor change for usa90 (usa19hs) required */
static int keyspan_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count)
{
	struct keyspan_port_private 	*p_priv;
	const struct keyspan_device_details	*d_details;
	int				flip;
	int 				left, todo;
	struct urb			*this_urb;
	int 				err, maxDataLen, dataOffset;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;

	if (d_details->msg_format == msg_usa90) {
		maxDataLen = 64;
		dataOffset = 0;
	} else {
		maxDataLen = 63;
		dataOffset = 1;
	}

	dbg("%s - for port %d (%d chars), flip=%d",
	    __func__, port->number, count, p_priv->out_flip);

	for (left = count; left > 0; left -= todo) {
		todo = left;
		if (todo > maxDataLen)
			todo = maxDataLen;

		flip = p_priv->out_flip;

		/* Check we have a valid urb/endpoint before we use it... */
		this_urb = p_priv->out_urbs[flip];
		if (this_urb == NULL) {
			/* no bulk out, so return 0 bytes written */
			dbg("%s - no output urb :(", __func__);
			return count;
		}

		dbg("%s - endpoint %d flip %d",
			__func__, usb_pipeendpoint(this_urb->pipe), flip);

		if (this_urb->status == -EINPROGRESS) {
			if (time_before(jiffies,
					p_priv->tx_start_time[flip] + 10 * HZ))
				break;
			usb_unlink_urb(this_urb);
			break;
		}

		/* First byte in buffer is "last flag" (except for usa19hx)
		   - unused so for now so set to zero */
		((char *)this_urb->transfer_buffer)[0] = 0;

		memcpy(this_urb->transfer_buffer + dataOffset, buf, todo);
		buf += todo;

		/* send the data out the bulk port */
		this_urb->transfer_buffer_length = todo + dataOffset;

		err = usb_submit_urb(this_urb, GFP_ATOMIC);
		if (err != 0)
			dbg("usb_submit_urb(write bulk) failed (%d)", err);
		p_priv->tx_start_time[flip] = jiffies;

		/* Flip for next time if usa26 or usa28 interface
		   (not used on usa49) */
		p_priv->out_flip = (flip + 1) & d_details->outdat_endp_flip;
	}

	return count - left;
}

static void	usa26_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);

	if (status) {
		dbg("%s - nonzero status: %x on endpoint %d.",
		    __func__, status, endpoint);
		return;
	}

	port =  urb->context;
	tty = tty_port_tty_get(&port->port);
	if (tty && urb->actual_length) {
		/* 0x80 bit is error flag */
		if ((data[0] & 0x80) == 0) {
			/* no errors on individual bytes, only
			   possible overrun err */
			if (data[0] & RXERROR_OVERRUN)
				err = TTY_OVERRUN;
			else
				err = 0;
			for (i = 1; i < urb->actual_length ; ++i)
				tty_insert_flip_char(tty, data[i], err);
		} else {
			/* some bytes had errors, every byte has status */
			dbg("%s - RX error!!!!", __func__);
			for (i = 0; i + 1 < urb->actual_length; i += 2) {
				int stat = data[i], flag = 0;
				if (stat & RXERROR_OVERRUN)
					flag |= TTY_OVERRUN;
				if (stat & RXERROR_FRAMING)
					flag |= TTY_FRAME;
				if (stat & RXERROR_PARITY)
					flag |= TTY_PARITY;
				/* XXX should handle break (0x10) */
				tty_insert_flip_char(tty, data[i+1], flag);
			}
		}
		tty_flip_buffer_push(tty);
	}
	tty_kref_put(tty);

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
}

/* Outdat handling is common for all devices */
static void	usa2x_outdat_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);
	dbg("%s - urb %d", __func__, urb == p_priv->out_urbs[1]);

	usb_serial_port_softint(port);
}

static void	usa26_inack_callback(struct urb *urb)
{
}

static void	usa26_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (p_priv->resend_cont) {
		dbg("%s - sending setup", __func__);
		keyspan_usa26_send_setup(port->serial, port,
						p_priv->resend_cont - 1);
	}
}

static void	usa26_instat_callback(struct urb *urb)
{
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa26_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	struct tty_struct			*tty;
	int old_dcd_state, err;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dbg("%s - nonzero status: %x", __func__, status);
		return;
	}
	if (urb->actual_length != 9) {
		dbg("%s - %d byte report??", __func__, urb->actual_length);
		goto exit;
	}

	msg = (struct keyspan_usa26_portStatusMessage *)data;

#if 0
	dbg("%s - port status: port %d cts %d dcd %d dsr %d ri %d toff %d txoff %d rxen %d cr %d",
	    __func__, msg->port, msg->hskia_cts, msg->gpia_dcd, msg->dsr, msg->ri, msg->_txOff,
	    msg->_txXoff, msg->rxEnabled, msg->controlResponse);
#endif

	/* Now do something useful with the data */


	/* Check port number from message and retrieve private data */
	if (msg->port >= serial->num_ports) {
		dbg("%s - Unexpected port number %d", __func__, msg->port);
		goto exit;
	}
	port = serial->port[msg->port];
	p_priv = usb_get_serial_port_data(port);

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->hskia_cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->gpia_dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state) {
		tty = tty_port_tty_get(&port->port);
		if (tty && !C_CLOCAL(tty))
			tty_hangup(tty);
		tty_kref_put(tty);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
exit: ;
}

static void	usa26_glocont_callback(struct urb *urb)
{
}


static void usa28_indat_callback(struct urb *urb)
{
	int                     err;
	struct usb_serial_port  *port;
	struct tty_struct       *tty;
	unsigned char           *data;
	struct keyspan_port_private             *p_priv;
	int status = urb->status;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);
	data = urb->transfer_buffer;

	if (urb != p_priv->in_urbs[p_priv->in_flip])
		return;

	do {
		if (status) {
			dbg("%s - nonzero status: %x on endpoint %d.",
			    __func__, status, usb_pipeendpoint(urb->pipe));
			return;
		}

		port =  urb->context;
		p_priv = usb_get_serial_port_data(port);
		data = urb->transfer_buffer;

		tty = tty_port_tty_get(&port->port);
		if (tty && urb->actual_length) {
			tty_insert_flip_string(tty, data, urb->actual_length);
			tty_flip_buffer_push(tty);
		}
		tty_kref_put(tty);

		/* Resubmit urb so we continue receiving */
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err != 0)
			dbg("%s - resubmit read urb failed. (%d)",
							__func__, err);
		p_priv->in_flip ^= 1;

		urb = p_priv->in_urbs[p_priv->in_flip];
	} while (urb->status != -EINPROGRESS);
}

static void	usa28_inack_callback(struct urb *urb)
{
}

static void	usa28_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (p_priv->resend_cont) {
		dbg("%s - sending setup", __func__);
		keyspan_usa28_send_setup(port->serial, port,
						p_priv->resend_cont - 1);
	}
}

static void	usa28_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa28_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	struct tty_struct			*tty;
	int old_dcd_state;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dbg("%s - nonzero status: %x", __func__, status);
		return;
	}

	if (urb->actual_length != sizeof(struct keyspan_usa28_portStatusMessage)) {
		dbg("%s - bad length %d", __func__, urb->actual_length);
		goto exit;
	}

	/*dbg("%s %x %x %x %x %x %x %x %x %x %x %x %x", __func__
	    data[0], data[1], data[2], data[3], data[4], data[5],
	    data[6], data[7], data[8], data[9], data[10], data[11]);*/

	/* Now do something useful with the data */
	msg = (struct keyspan_usa28_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->port >= serial->num_ports) {
		dbg("%s - Unexpected port number %d", __func__, msg->port);
		goto exit;
	}
	port = serial->port[msg->port];
	p_priv = usb_get_serial_port_data(port);

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state) {
		tty = tty_port_tty_get(&port->port);
		if (tty && !C_CLOCAL(tty))
			tty_hangup(tty);
		tty_kref_put(tty);
	}

		/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
exit: ;
}

static void	usa28_glocont_callback(struct urb *urb)
{
}


static void	usa49_glocont_callback(struct urb *urb)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;
	int i;

	serial =  urb->context;
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);

		if (p_priv->resend_cont) {
			dbg("%s - sending setup", __func__);
			keyspan_usa49_send_setup(serial, port,
						p_priv->resend_cont - 1);
			break;
		}
	}
}

	/* This is actually called glostat in the Keyspan
	   doco */
static void	usa49_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa49_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dbg("%s - nonzero status: %x", __func__, status);
		return;
	}

	if (urb->actual_length !=
			sizeof(struct keyspan_usa49_portStatusMessage)) {
		dbg("%s - bad length %d", __func__, urb->actual_length);
		goto exit;
	}

	/*dbg(" %x %x %x %x %x %x %x %x %x %x %x", __func__,
	    data[0], data[1], data[2], data[3], data[4], data[5],
	    data[6], data[7], data[8], data[9], data[10]);*/

	/* Now do something useful with the data */
	msg = (struct keyspan_usa49_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->portNumber >= serial->num_ports) {
		dbg("%s - Unexpected port number %d",
					__func__, msg->portNumber);
		goto exit;
	}
	port = serial->port[msg->portNumber];
	p_priv = usb_get_serial_port_data(port);

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state) {
		struct tty_struct *tty = tty_port_tty_get(&port->port);
		if (tty && !C_CLOCAL(tty))
			tty_hangup(tty);
		tty_kref_put(tty);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
exit:	;
}

static void	usa49_inack_callback(struct urb *urb)
{
}

static void	usa49_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);

	if (status) {
		dbg("%s - nonzero status: %x on endpoint %d.", __func__,
		    status, endpoint);
		return;
	}

	port =  urb->context;
	tty = tty_port_tty_get(&port->port);
	if (tty && urb->actual_length) {
		/* 0x80 bit is error flag */
		if ((data[0] & 0x80) == 0) {
			/* no error on any byte */
			tty_insert_flip_string(tty, data + 1,
						urb->actual_length - 1);
		} else {
			/* some bytes had errors, every byte has status */
			for (i = 0; i + 1 < urb->actual_length; i += 2) {
				int stat = data[i], flag = 0;
				if (stat & RXERROR_OVERRUN)
					flag |= TTY_OVERRUN;
				if (stat & RXERROR_FRAMING)
					flag |= TTY_FRAME;
				if (stat & RXERROR_PARITY)
					flag |= TTY_PARITY;
				/* XXX should handle break (0x10) */
				tty_insert_flip_char(tty, data[i+1], flag);
			}
		}
		tty_flip_buffer_push(tty);
	}
	tty_kref_put(tty);

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
}

static void usa49wg_indat_callback(struct urb *urb)
{
	int			i, len, x, err;
	struct usb_serial	*serial;
	struct usb_serial_port	*port;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	serial = urb->context;

	if (status) {
		dbg("%s - nonzero status: %x", __func__, status);
		return;
	}

	/* inbound data is in the form P#, len, status, data */
	i = 0;
	len = 0;

	if (urb->actual_length) {
		while (i < urb->actual_length) {

			/* Check port number from message*/
			if (data[i] >= serial->num_ports) {
				dbg("%s - Unexpected port number %d",
					__func__, data[i]);
				return;
			}
			port = serial->port[data[i++]];
			tty = tty_port_tty_get(&port->port);
			len = data[i++];

			/* 0x80 bit is error flag */
			if ((data[i] & 0x80) == 0) {
				/* no error on any byte */
				i++;
				for (x = 1; x < len ; ++x)
					tty_insert_flip_char(tty, data[i++], 0);
			} else {
				/*
				 * some bytes had errors, every byte has status
				 */
				for (x = 0; x + 1 < len; x += 2) {
					int stat = data[i], flag = 0;
					if (stat & RXERROR_OVERRUN)
						flag |= TTY_OVERRUN;
					if (stat & RXERROR_FRAMING)
						flag |= TTY_FRAME;
					if (stat & RXERROR_PARITY)
						flag |= TTY_PARITY;
					/* XXX should handle break (0x10) */
					tty_insert_flip_char(tty,
							data[i+1], flag);
					i += 2;
				}
			}
			tty_flip_buffer_push(tty);
			tty_kref_put(tty);
		}
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
}

/* not used, usa-49 doesn't have per-port control endpoints */
static void usa49_outcont_callback(struct urb *urb)
{
}

static void usa90_indat_callback(struct urb *urb)
{
	int			i, err;
	int			endpoint;
	struct usb_serial_port	*port;
	struct keyspan_port_private	 	*p_priv;
	struct tty_struct	*tty;
	unsigned char 		*data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);

	if (status) {
		dbg("%s - nonzero status: %x on endpoint %d.",
		    __func__, status, endpoint);
		return;
	}

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (urb->actual_length) {
		tty = tty_port_tty_get(&port->port);
		/* if current mode is DMA, looks like usa28 format
		   otherwise looks like usa26 data format */

		if (p_priv->baud > 57600)
			tty_insert_flip_string(tty, data, urb->actual_length);
		else {
			/* 0x80 bit is error flag */
			if ((data[0] & 0x80) == 0) {
				/* no errors on individual bytes, only
				   possible overrun err*/
				if (data[0] & RXERROR_OVERRUN)
					err = TTY_OVERRUN;
				else
					err = 0;
				for (i = 1; i < urb->actual_length ; ++i)
					tty_insert_flip_char(tty, data[i],
									err);
			}  else {
			/* some bytes had errors, every byte has status */
				dbg("%s - RX error!!!!", __func__);
				for (i = 0; i + 1 < urb->actual_length; i += 2) {
					int stat = data[i], flag = 0;
					if (stat & RXERROR_OVERRUN)
						flag |= TTY_OVERRUN;
					if (stat & RXERROR_FRAMING)
						flag |= TTY_FRAME;
					if (stat & RXERROR_PARITY)
						flag |= TTY_PARITY;
					/* XXX should handle break (0x10) */
					tty_insert_flip_char(tty, data[i+1],
									flag);
				}
			}
		}
		tty_flip_buffer_push(tty);
		tty_kref_put(tty);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
}


static void	usa90_instat_callback(struct urb *urb)
{
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa90_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	struct tty_struct			*tty;
	int old_dcd_state, err;
	int status = urb->status;

	serial =  urb->context;

	if (status) {
		dbg("%s - nonzero status: %x", __func__, status);
		return;
	}
	if (urb->actual_length < 14) {
		dbg("%s - %d byte report??", __func__, urb->actual_length);
		goto exit;
	}

	msg = (struct keyspan_usa90_portStatusMessage *)data;

	/* Now do something useful with the data */

	port = serial->port[0];
	p_priv = usb_get_serial_port_data(port);

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->cts) ? 1 : 0);
	p_priv->dsr_state = ((msg->dsr) ? 1 : 0);
	p_priv->dcd_state = ((msg->dcd) ? 1 : 0);
	p_priv->ri_state = ((msg->ri) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state) {
		tty = tty_port_tty_get(&port->port);
		if (tty && !C_CLOCAL(tty))
			tty_hangup(tty);
		tty_kref_put(tty);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
exit:
	;
}

static void	usa90_outcont_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;

	port =  urb->context;
	p_priv = usb_get_serial_port_data(port);

	if (p_priv->resend_cont) {
		dbg("%s - sending setup", __func__);
		keyspan_usa90_send_setup(port->serial, port,
						p_priv->resend_cont - 1);
	}
}

/* Status messages from the 28xg */
static void	usa67_instat_callback(struct urb *urb)
{
	int					err;
	unsigned char 				*data = urb->transfer_buffer;
	struct keyspan_usa67_portStatusMessage	*msg;
	struct usb_serial			*serial;
	struct usb_serial_port			*port;
	struct keyspan_port_private	 	*p_priv;
	int old_dcd_state;
	int status = urb->status;

	serial = urb->context;

	if (status) {
		dbg("%s - nonzero status: %x", __func__, status);
		return;
	}

	if (urb->actual_length !=
			sizeof(struct keyspan_usa67_portStatusMessage)) {
		dbg("%s - bad length %d", __func__, urb->actual_length);
		return;
	}


	/* Now do something useful with the data */
	msg = (struct keyspan_usa67_portStatusMessage *)data;

	/* Check port number from message and retrieve private data */
	if (msg->port >= serial->num_ports) {
		dbg("%s - Unexpected port number %d", __func__, msg->port);
		return;
	}

	port = serial->port[msg->port];
	p_priv = usb_get_serial_port_data(port);

	/* Update handshaking pin state information */
	old_dcd_state = p_priv->dcd_state;
	p_priv->cts_state = ((msg->hskia_cts) ? 1 : 0);
	p_priv->dcd_state = ((msg->gpia_dcd) ? 1 : 0);

	if (old_dcd_state != p_priv->dcd_state && old_dcd_state) {
		struct tty_struct *tty = tty_port_tty_get(&port->port);
		if (tty && !C_CLOCAL(tty))
			tty_hangup(tty);
		tty_kref_put(tty);
	}

	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - resubmit read urb failed. (%d)", __func__, err);
}

static void usa67_glocont_callback(struct urb *urb)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct keyspan_port_private *p_priv;
	int i;

	serial = urb->context;
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);

		if (p_priv->resend_cont) {
			dbg("%s - sending setup", __func__);
			keyspan_usa67_send_setup(serial, port,
						p_priv->resend_cont - 1);
			break;
		}
	}
}

static int keyspan_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct keyspan_port_private	*p_priv;
	const struct keyspan_device_details	*d_details;
	int				flip;
	int				data_len;
	struct urb			*this_urb;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;

	/* FIXME: locking */
	if (d_details->msg_format == msg_usa90)
		data_len = 64;
	else
		data_len = 63;

	flip = p_priv->out_flip;

	/* Check both endpoints to see if any are available. */
	this_urb = p_priv->out_urbs[flip];
	if (this_urb != NULL) {
		if (this_urb->status != -EINPROGRESS)
			return data_len;
		flip = (flip + 1) & d_details->outdat_endp_flip;
		this_urb = p_priv->out_urbs[flip];
		if (this_urb != NULL) {
			if (this_urb->status != -EINPROGRESS)
				return data_len;
		}
	}
	return 0;
}


static int keyspan_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct keyspan_port_private 	*p_priv;
	const struct keyspan_device_details	*d_details;
	int				i, err;
	int				baud_rate, device_port;
	struct urb			*urb;
	unsigned int			cflag = 0;

	p_priv = usb_get_serial_port_data(port);
	d_details = p_priv->device_details;

	/* Set some sane defaults */
	p_priv->rts_state = 1;
	p_priv->dtr_state = 1;
	p_priv->baud = 9600;

	/* force baud and lcr to be set on open */
	p_priv->old_baud = 0;
	p_priv->old_cflag = 0;

	p_priv->out_flip = 0;
	p_priv->in_flip = 0;

	/* Reset low level data toggle and start reading from endpoints */
	for (i = 0; i < 2; i++) {
		urb = p_priv->in_urbs[i];
		if (urb == NULL)
			continue;

		/* make sure endpoint data toggle is synchronized
		   with the device */
		usb_clear_halt(urb->dev, urb->pipe);
		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err != 0)
			dbg("%s - submit urb %d failed (%d)",
							__func__, i, err);
	}

	/* Reset low level data toggle on out endpoints */
	for (i = 0; i < 2; i++) {
		urb = p_priv->out_urbs[i];
		if (urb == NULL)
			continue;
		/* usb_settoggle(urb->dev, usb_pipeendpoint(urb->pipe),
						usb_pipeout(urb->pipe), 0); */
	}

	/* get the terminal config for the setup message now so we don't
	 * need to send 2 of them */

	device_port = port->number - port->serial->minor;
	if (tty) {
		cflag = tty->termios->c_cflag;
		/* Baud rate calculation takes baud rate as an integer
		   so other rates can be generated if desired. */
		baud_rate = tty_get_baud_rate(tty);
		/* If no match or invalid, leave as default */
		if (baud_rate >= 0
		    && d_details->calculate_baud_rate(baud_rate, d_details->baudclk,
					NULL, NULL, NULL, device_port) == KEYSPAN_BAUD_RATE_OK) {
			p_priv->baud = baud_rate;
		}
	}
	/* set CTS/RTS handshake etc. */
	p_priv->cflag = cflag;
	p_priv->flow_control = (cflag & CRTSCTS) ? flow_cts : flow_none;

	keyspan_send_setup(port, 1);
	/* mdelay(100); */
	/* keyspan_set_termios(port, NULL); */

	return 0;
}

static inline void stop_urb(struct urb *urb)
{
	if (urb && urb->status == -EINPROGRESS)
		usb_kill_urb(urb);
}

static void keyspan_dtr_rts(struct usb_serial_port *port, int on)
{
	struct keyspan_port_private *p_priv = usb_get_serial_port_data(port);

	p_priv->rts_state = on;
	p_priv->dtr_state = on;
	keyspan_send_setup(port, 0);
}

static void keyspan_close(struct usb_serial_port *port)
{
	int			i;
	struct usb_serial	*serial = port->serial;
	struct keyspan_port_private 	*p_priv;

	p_priv = usb_get_serial_port_data(port);

	p_priv->rts_state = 0;
	p_priv->dtr_state = 0;

	if (serial->dev) {
		keyspan_send_setup(port, 2);
		/* pilot-xfer seems to work best with this delay */
		mdelay(100);
		/* keyspan_set_termios(port, NULL); */
	}

	/*while (p_priv->outcont_urb->status == -EINPROGRESS) {
		dbg("%s - urb in progress", __func__);
	}*/

	p_priv->out_flip = 0;
	p_priv->in_flip = 0;

	if (serial->dev) {
		/* Stop reading/writing urbs */
		stop_urb(p_priv->inack_urb);
		/* stop_urb(p_priv->outcont_urb); */
		for (i = 0; i < 2; i++) {
			stop_urb(p_priv->in_urbs[i]);
			stop_urb(p_priv->out_urbs[i]);
		}
	}
}

/* download the firmware to a pre-renumeration device */
static int keyspan_fake_startup(struct usb_serial *serial)
{
	int 				response;
	const struct ihex_binrec 	*record;
	char				*fw_name;
	const struct firmware		*fw;

	dbg("Keyspan startup version %04x product %04x",
	    le16_to_cpu(serial->dev->descriptor.bcdDevice),
	    le16_to_cpu(serial->dev->descriptor.idProduct));

	if ((le16_to_cpu(serial->dev->descriptor.bcdDevice) & 0x8000)
								!= 0x8000) {
		dbg("Firmware already loaded.  Quitting.");
		return 1;
	}

		/* Select firmware image on the basis of idProduct */
	switch (le16_to_cpu(serial->dev->descriptor.idProduct)) {
	case keyspan_usa28_pre_product_id:
		fw_name = "keyspan/usa28.fw";
		break;

	case keyspan_usa28x_pre_product_id:
		fw_name = "keyspan/usa28x.fw";
		break;

	case keyspan_usa28xa_pre_product_id:
		fw_name = "keyspan/usa28xa.fw";
		break;

	case keyspan_usa28xb_pre_product_id:
		fw_name = "keyspan/usa28xb.fw";
		break;

	case keyspan_usa19_pre_product_id:
		fw_name = "keyspan/usa19.fw";
		break;

	case keyspan_usa19qi_pre_product_id:
		fw_name = "keyspan/usa19qi.fw";
		break;

	case keyspan_mpr_pre_product_id:
		fw_name = "keyspan/mpr.fw";
		break;

	case keyspan_usa19qw_pre_product_id:
		fw_name = "keyspan/usa19qw.fw";
		break;

	case keyspan_usa18x_pre_product_id:
		fw_name = "keyspan/usa18x.fw";
		break;

	case keyspan_usa19w_pre_product_id:
		fw_name = "keyspan/usa19w.fw";
		break;

	case keyspan_usa49w_pre_product_id:
		fw_name = "keyspan/usa49w.fw";
		break;

	case keyspan_usa49wlc_pre_product_id:
		fw_name = "keyspan/usa49wlc.fw";
		break;

	default:
		dev_err(&serial->dev->dev, "Unknown product ID (%04x)\n",
			le16_to_cpu(serial->dev->descriptor.idProduct));
		return 1;
	}

	if (request_ihex_firmware(&fw, fw_name, &serial->dev->dev)) {
		dev_err(&serial->dev->dev, "Required keyspan firmware image (%s) unavailable.\n", fw_name);
		return 1;
	}

	dbg("Uploading Keyspan %s firmware.", fw_name);

		/* download the firmware image */
	response = ezusb_set_reset(serial->dev, 1);

	record = (const struct ihex_binrec *)fw->data;

	while (record) {
		response = ezusb_writememory(serial->dev, be32_to_cpu(record->addr),
					     (unsigned char *)record->data,
					     be16_to_cpu(record->len), 0xa0);
		if (response < 0) {
			dev_err(&serial->dev->dev, "ezusb_writememory failed for Keyspan firmware (%d %04X %p %d)\n",
				response, be32_to_cpu(record->addr),
				record->data, be16_to_cpu(record->len));
			break;
		}
		record = ihex_next_binrec(record);
	}
	release_firmware(fw);
		/* bring device out of reset. Renumeration will occur in a
		   moment and the new device will bind to the real driver */
	response = ezusb_set_reset(serial->dev, 0);

	/* we don't want this device to have a driver assigned to it. */
	return 1;
}

/* Helper functions used by keyspan_setup_urbs */
static struct usb_endpoint_descriptor const *find_ep(struct usb_serial const *serial,
						     int endpoint)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *ep;
	int i;

	iface_desc = serial->interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		ep = &iface_desc->endpoint[i].desc;
		if (ep->bEndpointAddress == endpoint)
			return ep;
	}
	dev_warn(&serial->interface->dev, "found no endpoint descriptor for "
		 "endpoint %x\n", endpoint);
	return NULL;
}

static struct urb *keyspan_setup_urb(struct usb_serial *serial, int endpoint,
				      int dir, void *ctx, char *buf, int len,
				      void (*callback)(struct urb *))
{
	struct urb *urb;
	struct usb_endpoint_descriptor const *ep_desc;
	char const *ep_type_name;

	if (endpoint == -1)
		return NULL;		/* endpoint not needed */

	dbg("%s - alloc for endpoint %d.", __func__, endpoint);
	urb = usb_alloc_urb(0, GFP_KERNEL);		/* No ISO */
	if (urb == NULL) {
		dbg("%s - alloc for endpoint %d failed.", __func__, endpoint);
		return NULL;
	}

	if (endpoint == 0) {
		/* control EP filled in when used */
		return urb;
	}

	ep_desc = find_ep(serial, endpoint);
	if (!ep_desc) {
		/* leak the urb, something's wrong and the callers don't care */
		return urb;
	}
	if (usb_endpoint_xfer_int(ep_desc)) {
		ep_type_name = "INT";
		usb_fill_int_urb(urb, serial->dev,
				 usb_sndintpipe(serial->dev, endpoint) | dir,
				 buf, len, callback, ctx,
				 ep_desc->bInterval);
	} else if (usb_endpoint_xfer_bulk(ep_desc)) {
		ep_type_name = "BULK";
		usb_fill_bulk_urb(urb, serial->dev,
				  usb_sndbulkpipe(serial->dev, endpoint) | dir,
				  buf, len, callback, ctx);
	} else {
		dev_warn(&serial->interface->dev,
			 "unsupported endpoint type %x\n",
			 usb_endpoint_type(ep_desc));
		usb_free_urb(urb);
		return NULL;
	}

	dbg("%s - using urb %p for %s endpoint %x",
	    __func__, urb, ep_type_name, endpoint);
	return urb;
}

static struct callbacks {
	void	(*instat_callback)(struct urb *);
	void	(*glocont_callback)(struct urb *);
	void	(*indat_callback)(struct urb *);
	void	(*outdat_callback)(struct urb *);
	void	(*inack_callback)(struct urb *);
	void	(*outcont_callback)(struct urb *);
} keyspan_callbacks[] = {
	{
		/* msg_usa26 callbacks */
		.instat_callback =	usa26_instat_callback,
		.glocont_callback =	usa26_glocont_callback,
		.indat_callback =	usa26_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa26_inack_callback,
		.outcont_callback =	usa26_outcont_callback,
	}, {
		/* msg_usa28 callbacks */
		.instat_callback =	usa28_instat_callback,
		.glocont_callback =	usa28_glocont_callback,
		.indat_callback =	usa28_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa28_inack_callback,
		.outcont_callback =	usa28_outcont_callback,
	}, {
		/* msg_usa49 callbacks */
		.instat_callback =	usa49_instat_callback,
		.glocont_callback =	usa49_glocont_callback,
		.indat_callback =	usa49_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa49_inack_callback,
		.outcont_callback =	usa49_outcont_callback,
	}, {
		/* msg_usa90 callbacks */
		.instat_callback =	usa90_instat_callback,
		.glocont_callback =	usa28_glocont_callback,
		.indat_callback =	usa90_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa28_inack_callback,
		.outcont_callback =	usa90_outcont_callback,
	}, {
		/* msg_usa67 callbacks */
		.instat_callback =	usa67_instat_callback,
		.glocont_callback =	usa67_glocont_callback,
		.indat_callback =	usa26_indat_callback,
		.outdat_callback =	usa2x_outdat_callback,
		.inack_callback =	usa26_inack_callback,
		.outcont_callback =	usa26_outcont_callback,
	}
};

	/* Generic setup urbs function that uses
	   data in device_details */
static void keyspan_setup_urbs(struct usb_serial *serial)
{
	int				i, j;
	struct keyspan_serial_private 	*s_priv;
	const struct keyspan_device_details	*d_details;
	struct usb_serial_port		*port;
	struct keyspan_port_private	*p_priv;
	struct callbacks		*cback;
	int				endp;

	s_priv = usb_get_serial_data(serial);
	d_details = s_priv->device_details;

	/* Setup values for the various callback routines */
	cback = &keyspan_callbacks[d_details->msg_format];

	/* Allocate and set up urbs for each one that is in use,
	   starting with instat endpoints */
	s_priv->instat_urb = keyspan_setup_urb
		(serial, d_details->instat_endpoint, USB_DIR_IN,
		 serial, s_priv->instat_buf, INSTAT_BUFLEN,
		 cback->instat_callback);

	s_priv->indat_urb = keyspan_setup_urb
		(serial, d_details->indat_endpoint, USB_DIR_IN,
		 serial, s_priv->indat_buf, INDAT49W_BUFLEN,
		 usa49wg_indat_callback);

	s_priv->glocont_urb = keyspan_setup_urb
		(serial, d_details->glocont_endpoint, USB_DIR_OUT,
		 serial, s_priv->glocont_buf, GLOCONT_BUFLEN,
		 cback->glocont_callback);

	/* Setup endpoints for each port specific thing */
	for (i = 0; i < d_details->num_ports; i++) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);

		/* Do indat endpoints first, once for each flip */
		endp = d_details->indat_endpoints[i];
		for (j = 0; j <= d_details->indat_endp_flip; ++j, ++endp) {
			p_priv->in_urbs[j] = keyspan_setup_urb
				(serial, endp, USB_DIR_IN, port,
				 p_priv->in_buffer[j], 64,
				 cback->indat_callback);
		}
		for (; j < 2; ++j)
			p_priv->in_urbs[j] = NULL;

		/* outdat endpoints also have flip */
		endp = d_details->outdat_endpoints[i];
		for (j = 0; j <= d_details->outdat_endp_flip; ++j, ++endp) {
			p_priv->out_urbs[j] = keyspan_setup_urb
				(serial, endp, USB_DIR_OUT, port,
				 p_priv->out_buffer[j], 64,
				 cback->outdat_callback);
		}
		for (; j < 2; ++j)
			p_priv->out_urbs[j] = NULL;

		/* inack endpoint */
		p_priv->inack_urb = keyspan_setup_urb
			(serial, d_details->inack_endpoints[i], USB_DIR_IN,
			 port, p_priv->inack_buffer, 1, cback->inack_callback);

		/* outcont endpoint */
		p_priv->outcont_urb = keyspan_setup_urb
			(serial, d_details->outcont_endpoints[i], USB_DIR_OUT,
			 port, p_priv->outcont_buffer, 64,
			 cback->outcont_callback);
	}
}

/* usa19 function doesn't require prescaler */
static int keyspan_usa19_calc_baud(u32 baud_rate, u32 baudclk, u8 *rate_hi,
				   u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		div,	/* divisor */
		cnt;	/* inverse of divisor (programmed into 8051) */

	dbg("%s - %d.", __func__, baud_rate);

	/* prevent divide by zero...  */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;
	/* Any "standard" rate over 57k6 is marginal on the USA-19
	   as we run out of divisor resolution. */
	if (baud_rate > 57600)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* calculate the divisor and the counter (its inverse) */
	div = baudclk / b16;
	if (div == 0)
		return KEYSPAN_INVALID_BAUD_RATE;
	else
		cnt = 0 - div;

	if (div > 0xffff)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* return the counter values if non-null */
	if (rate_low)
		*rate_low = (u8) (cnt & 0xff);
	if (rate_hi)
		*rate_hi = (u8) ((cnt >> 8) & 0xff);
	if (rate_low && rate_hi)
		dbg("%s - %d %02x %02x.",
				__func__, baud_rate, *rate_hi, *rate_low);
	return KEYSPAN_BAUD_RATE_OK;
}

/* usa19hs function doesn't require prescaler */
static int keyspan_usa19hs_calc_baud(u32 baud_rate, u32 baudclk, u8 *rate_hi,
				   u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
			div;	/* divisor */

	dbg("%s - %d.", __func__, baud_rate);

	/* prevent divide by zero...  */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* calculate the divisor */
	div = baudclk / b16;
	if (div == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	if (div > 0xffff)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* return the counter values if non-null */
	if (rate_low)
		*rate_low = (u8) (div & 0xff);

	if (rate_hi)
		*rate_hi = (u8) ((div >> 8) & 0xff);

	if (rate_low && rate_hi)
		dbg("%s - %d %02x %02x.",
			__func__, baud_rate, *rate_hi, *rate_low);

	return KEYSPAN_BAUD_RATE_OK;
}

static int keyspan_usa19w_calc_baud(u32 baud_rate, u32 baudclk, u8 *rate_hi,
				    u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		clk,	/* clock with 13/8 prescaler */
		div,	/* divisor using 13/8 prescaler */
		res,	/* resulting baud rate using 13/8 prescaler */
		diff,	/* error using 13/8 prescaler */
		smallest_diff;
	u8	best_prescaler;
	int	i;

	dbg("%s - %d.", __func__, baud_rate);

	/* prevent divide by zero */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* Calculate prescaler by trying them all and looking
	   for best fit */

	/* start with largest possible difference */
	smallest_diff = 0xffffffff;

		/* 0 is an invalid prescaler, used as a flag */
	best_prescaler = 0;

	for (i = 8; i <= 0xff; ++i) {
		clk = (baudclk * 8) / (u32) i;

		div = clk / b16;
		if (div == 0)
			continue;

		res = clk / div;
		diff = (res > b16) ? (res-b16) : (b16-res);

		if (diff < smallest_diff) {
			best_prescaler = i;
			smallest_diff = diff;
		}
	}

	if (best_prescaler == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	clk = (baudclk * 8) / (u32) best_prescaler;
	div = clk / b16;

	/* return the divisor and prescaler if non-null */
	if (rate_low)
		*rate_low = (u8) (div & 0xff);
	if (rate_hi)
		*rate_hi = (u8) ((div >> 8) & 0xff);
	if (prescaler) {
		*prescaler = best_prescaler;
		/*  dbg("%s - %d %d", __func__, *prescaler, div); */
	}
	return KEYSPAN_BAUD_RATE_OK;
}

	/* USA-28 supports different maximum baud rates on each port */
static int keyspan_usa28_calc_baud(u32 baud_rate, u32 baudclk, u8 *rate_hi,
				    u8 *rate_low, u8 *prescaler, int portnum)
{
	u32 	b16,	/* baud rate times 16 (actual rate used internally) */
		div,	/* divisor */
		cnt;	/* inverse of divisor (programmed into 8051) */

	dbg("%s - %d.", __func__, baud_rate);

		/* prevent divide by zero */
	b16 = baud_rate * 16L;
	if (b16 == 0)
		return KEYSPAN_INVALID_BAUD_RATE;

	/* calculate the divisor and the counter (its inverse) */
	div = KEYSPAN_USA28_BAUDCLK / b16;
	if (div == 0)
		return KEYSPAN_INVALID_BAUD_RATE;
	else
		cnt = 0 - div;

	/* check for out of range, based on portnum,
	   and return result */
	if (portnum == 0) {
		if (div > 0xffff)
			return KEYSPAN_INVALID_BAUD_RATE;
	} else {
		if (portnum == 1) {
			if (div > 0xff)
				return KEYSPAN_INVALID_BAUD_RATE;
		} else
			return KEYSPAN_INVALID_BAUD_RATE;
	}

		/* return the counter values if not NULL
		   (port 1 will ignore retHi) */
	if (rate_low)
		*rate_low = (u8) (cnt & 0xff);
	if (rate_hi)
		*rate_hi = (u8) ((cnt >> 8) & 0xff);
	dbg("%s - %d OK.", __func__, baud_rate);
	return KEYSPAN_BAUD_RATE_OK;
}

static int keyspan_usa26_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa26_portControlMessage	msg;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	int 					outcont_urb;
	struct urb				*this_urb;
	int 					device_port, err;

	dbg("%s reset=%d", __func__, reset_port);

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;
	device_port = port->number - port->serial->minor;

	outcont_urb = d_details->outcont_endpoints[port->number];
	this_urb = p_priv->outcont_urb;

	dbg("%s - endpoint %d", __func__, usb_pipeendpoint(this_urb->pipe));

		/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dbg("%s - oops no urb.", __func__);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		/*  dbg("%s - already writing", __func__); */
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa26_portControlMessage));

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate
		    (p_priv->baud, d_details->baudclk, &msg.baudHi,
		     &msg.baudLo, &msg.prescaler, device_port) == KEYSPAN_INVALID_BAUD_RATE) {
			dbg("%s - Invalid baud rate %d requested, using 9600.",
						__func__, p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		msg.setPrescaler = 0xff;
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
			USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;
	msg.forwardingLength = 16;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/* Opening port */
	if (reset_port == 1) {
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
	}

	/* Closing port */
	else if (reset_port == 2) {
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
	}

	/* Sending intermediate configs */
	else {
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
	}

	/* Do handshaking outputs */
	msg.setTxTriState_setRts = 0xff;
	msg.txTriState_rts = p_priv->rts_state;

	msg.setHskoa_setDtr = 0xff;
	msg.hskoa_dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;
	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - usb_submit_urb(setup) failed (%d)", __func__, err);
#if 0
	else {
		dbg("%s - usb_submit_urb(%d) OK %d bytes (end %d)", __func__
		    outcont_urb, this_urb->transfer_buffer_length,
		    usb_pipeendpoint(this_urb->pipe));
	}
#endif

	return 0;
}

static int keyspan_usa28_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa28_portControlMessage	msg;
	struct keyspan_serial_private	 	*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					device_port, err;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;
	device_port = port->number - port->serial->minor;

	/* only do something if we have a bulk out endpoint */
	this_urb = p_priv->outcont_urb;
	if (this_urb == NULL) {
		dbg("%s - oops no urb.", __func__);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		dbg("%s already writing", __func__);
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa28_portControlMessage));

	msg.setBaudRate = 1;
	if (d_details->calculate_baud_rate(p_priv->baud, d_details->baudclk,
		&msg.baudHi, &msg.baudLo, NULL, device_port) == KEYSPAN_INVALID_BAUD_RATE) {
		dbg("%s - Invalid baud rate requested %d.",
						__func__, p_priv->baud);
		msg.baudLo = 0xff;
		msg.baudHi = 0xb2;	/* Values for 9600 baud */
	}

	/* If parity is enabled, we must calculate it ourselves. */
	msg.parity = 0;		/* XXX for now */

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;

	/* Do handshaking outputs, DTR is inverted relative to RTS */
	msg.rts = p_priv->rts_state;
	msg.dtr = p_priv->dtr_state;

	msg.forwardingLength = 16;
	msg.forwardMs = 10;
	msg.breakThreshold = 45;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/*msg.returnStatus = 1;
	msg.resetDataToggle = 0xff;*/
	/* Opening port */
	if (reset_port == 1) {
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txForceXoff = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
	}
	/* Closing port */
	else if (reset_port == 2) {
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txForceXoff = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
	}
	/* Sending intermediate configs */
	else {
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txForceXoff = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
	}

	p_priv->resend_cont = 0;
	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - usb_submit_urb(setup) failed", __func__);
#if 0
	else {
		dbg("%s - usb_submit_urb(setup) OK %d bytes", __func__,
		    this_urb->transfer_buffer_length);
	}
#endif

	return 0;
}

static int keyspan_usa49_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa49_portControlMessage	msg;
	struct usb_ctrlrequest 			*dr = NULL;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					err, device_port;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;

	this_urb = s_priv->glocont_urb;

	/* Work out which port within the device is being setup */
	device_port = port->number - port->serial->minor;

	/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dbg("%s - oops no urb for port %d.", __func__, port->number);
		return -1;
	}

	dbg("%s - endpoint %d port %d (%d)",
			__func__, usb_pipeendpoint(this_urb->pipe),
			port->number, device_port);

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;

	if (this_urb->status == -EINPROGRESS) {
		/*  dbg("%s - already writing", __func__); */
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa49_portControlMessage));

	/*msg.portNumber = port->number;*/
	msg.portNumber = device_port;

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate
		    (p_priv->baud, d_details->baudclk, &msg.baudHi,
		     &msg.baudLo, &msg.prescaler, device_port) == KEYSPAN_INVALID_BAUD_RATE) {
			dbg("%s - Invalid baud rate %d requested, using 9600.",
						__func__, p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		/* msg.setPrescaler = 0xff; */
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
			USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;

	msg.forwardingLength = 16;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/* Opening port */
	if (reset_port == 1) {
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
		msg.enablePort = 1;
		msg.disablePort = 0;
	}
	/* Closing port */
	else if (reset_port == 2) {
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
		msg.enablePort = 0;
		msg.disablePort = 1;
	}
	/* Sending intermediate configs */
	else {
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
		msg.enablePort = 0;
		msg.disablePort = 0;
	}

	/* Do handshaking outputs */
	msg.setRts = 0xff;
	msg.rts = p_priv->rts_state;

	msg.setDtr = 0xff;
	msg.dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;

	/* if the device is a 49wg, we send control message on usb
	   control EP 0 */

	if (d_details->product_id == keyspan_usa49wg_product_id) {
		dr = (void *)(s_priv->ctrl_buf);
		dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_OUT;
		dr->bRequest = 0xB0;	/* 49wg control message */;
		dr->wValue = 0;
		dr->wIndex = 0;
		dr->wLength = cpu_to_le16(sizeof(msg));

		memcpy(s_priv->glocont_buf, &msg, sizeof(msg));

		usb_fill_control_urb(this_urb, serial->dev,
				usb_sndctrlpipe(serial->dev, 0),
				(unsigned char *)dr, s_priv->glocont_buf,
				sizeof(msg), usa49_glocont_callback, serial);

	} else {
		memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

		/* send the data out the device on control endpoint */
		this_urb->transfer_buffer_length = sizeof(msg);
	}
	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - usb_submit_urb(setup) failed (%d)", __func__, err);
#if 0
	else {
		dbg("%s - usb_submit_urb(%d) OK %d bytes (end %d)", __func__,
			   outcont_urb, this_urb->transfer_buffer_length,
			   usb_pipeendpoint(this_urb->pipe));
	}
#endif

	return 0;
}

static int keyspan_usa90_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa90_portControlMessage	msg;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					err;
	u8						prescaler;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;

	/* only do something if we have a bulk out endpoint */
	this_urb = p_priv->outcont_urb;
	if (this_urb == NULL) {
		dbg("%s - oops no urb.", __func__);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		dbg("%s already writing", __func__);
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa90_portControlMessage));

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0x01;
		if (d_details->calculate_baud_rate
		    (p_priv->baud, d_details->baudclk, &msg.baudHi,
		     &msg.baudLo, &prescaler, 0) == KEYSPAN_INVALID_BAUD_RATE) {
			dbg("%s - Invalid baud rate %d requested, using 9600.",
						__func__, p_priv->baud);
			p_priv->baud = 9600;
			d_details->calculate_baud_rate(p_priv->baud, d_details->baudclk,
				&msg.baudHi, &msg.baudLo, &prescaler, 0);
		}
		msg.setRxMode = 1;
		msg.setTxMode = 1;
	}

	/* modes must always be correctly specified */
	if (p_priv->baud > 57600) {
		msg.rxMode = RXMODE_DMA;
		msg.txMode = TXMODE_DMA;
	} else {
		msg.rxMode = RXMODE_BYHAND;
		msg.txMode = TXMODE_BYHAND;
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
			USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	if (p_priv->old_cflag != p_priv->cflag) {
		p_priv->old_cflag = p_priv->cflag;
		msg.setLcr = 0x01;
	}

	if (p_priv->flow_control == flow_cts)
		msg.txFlowControl = TXFLOW_CTS;
	msg.setTxFlowControl = 0x01;
	msg.setRxFlowControl = 0x01;

	msg.rxForwardingLength = 16;
	msg.rxForwardingTimeout = 16;
	msg.txAckSetting = 0;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	/* Opening port */
	if (reset_port == 1) {
		msg.portEnabled = 1;
		msg.rxFlush = 1;
		msg.txBreak = (p_priv->break_on);
	}
	/* Closing port */
	else if (reset_port == 2)
		msg.portEnabled = 0;
	/* Sending intermediate configs */
	else {
		msg.portEnabled = 1;
		msg.txBreak = (p_priv->break_on);
	}

	/* Do handshaking outputs */
	msg.setRts = 0x01;
	msg.rts = p_priv->rts_state;

	msg.setDtr = 0x01;
	msg.dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;
	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - usb_submit_urb(setup) failed (%d)", __func__, err);
	return 0;
}

static int keyspan_usa67_send_setup(struct usb_serial *serial,
				    struct usb_serial_port *port,
				    int reset_port)
{
	struct keyspan_usa67_portControlMessage	msg;
	struct keyspan_serial_private 		*s_priv;
	struct keyspan_port_private 		*p_priv;
	const struct keyspan_device_details	*d_details;
	struct urb				*this_urb;
	int 					err, device_port;

	s_priv = usb_get_serial_data(serial);
	p_priv = usb_get_serial_port_data(port);
	d_details = s_priv->device_details;

	this_urb = s_priv->glocont_urb;

	/* Work out which port within the device is being setup */
	device_port = port->number - port->serial->minor;

	/* Make sure we have an urb then send the message */
	if (this_urb == NULL) {
		dbg("%s - oops no urb for port %d.", __func__,
			port->number);
		return -1;
	}

	/* Save reset port val for resend.
	   Don't overwrite resend for open/close condition. */
	if ((reset_port + 1) > p_priv->resend_cont)
		p_priv->resend_cont = reset_port + 1;
	if (this_urb->status == -EINPROGRESS) {
		/*  dbg("%s - already writing", __func__); */
		mdelay(5);
		return -1;
	}

	memset(&msg, 0, sizeof(struct keyspan_usa67_portControlMessage));

	msg.port = device_port;

	/* Only set baud rate if it's changed */
	if (p_priv->old_baud != p_priv->baud) {
		p_priv->old_baud = p_priv->baud;
		msg.setClocking = 0xff;
		if (d_details->calculate_baud_rate
		    (p_priv->baud, d_details->baudclk, &msg.baudHi,
		     &msg.baudLo, &msg.prescaler, device_port) == KEYSPAN_INVALID_BAUD_RATE) {
			dbg("%s - Invalid baud rate %d requested, using 9600.",
						__func__, p_priv->baud);
			msg.baudLo = 0;
			msg.baudHi = 125;	/* Values for 9600 baud */
			msg.prescaler = 10;
		}
		msg.setPrescaler = 0xff;
	}

	msg.lcr = (p_priv->cflag & CSTOPB) ? STOPBITS_678_2 : STOPBITS_5678_1;
	switch (p_priv->cflag & CSIZE) {
	case CS5:
		msg.lcr |= USA_DATABITS_5;
		break;
	case CS6:
		msg.lcr |= USA_DATABITS_6;
		break;
	case CS7:
		msg.lcr |= USA_DATABITS_7;
		break;
	case CS8:
		msg.lcr |= USA_DATABITS_8;
		break;
	}
	if (p_priv->cflag & PARENB) {
		/* note USA_PARITY_NONE == 0 */
		msg.lcr |= (p_priv->cflag & PARODD) ?
					USA_PARITY_ODD : USA_PARITY_EVEN;
	}
	msg.setLcr = 0xff;

	msg.ctsFlowControl = (p_priv->flow_control == flow_cts);
	msg.xonFlowControl = 0;
	msg.setFlowControl = 0xff;
	msg.forwardingLength = 16;
	msg.xonChar = 17;
	msg.xoffChar = 19;

	if (reset_port == 1) {
		/* Opening port */
		msg._txOn = 1;
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 1;
		msg.rxOff = 0;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0xff;
	} else if (reset_port == 2) {
		/* Closing port */
		msg._txOn = 0;
		msg._txOff = 1;
		msg.txFlush = 0;
		msg.txBreak = 0;
		msg.rxOn = 0;
		msg.rxOff = 1;
		msg.rxFlush = 1;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0;
	} else {
		/* Sending intermediate configs */
		msg._txOn = (!p_priv->break_on);
		msg._txOff = 0;
		msg.txFlush = 0;
		msg.txBreak = (p_priv->break_on);
		msg.rxOn = 0;
		msg.rxOff = 0;
		msg.rxFlush = 0;
		msg.rxForward = 0;
		msg.returnStatus = 0;
		msg.resetDataToggle = 0x0;
	}

	/* Do handshaking outputs */
	msg.setTxTriState_setRts = 0xff;
	msg.txTriState_rts = p_priv->rts_state;

	msg.setHskoa_setDtr = 0xff;
	msg.hskoa_dtr = p_priv->dtr_state;

	p_priv->resend_cont = 0;

	memcpy(this_urb->transfer_buffer, &msg, sizeof(msg));

	/* send the data out the device on control endpoint */
	this_urb->transfer_buffer_length = sizeof(msg);

	err = usb_submit_urb(this_urb, GFP_ATOMIC);
	if (err != 0)
		dbg("%s - usb_submit_urb(setup) failed (%d)", __func__,
				err);
	return 0;
}

static void keyspan_send_setup(struct usb_serial_port *port, int reset_port)
{
	struct usb_serial *serial = port->serial;
	struct keyspan_serial_private *s_priv;
	const struct keyspan_device_details *d_details;

	s_priv = usb_get_serial_data(serial);
	d_details = s_priv->device_details;

	switch (d_details->msg_format) {
	case msg_usa26:
		keyspan_usa26_send_setup(serial, port, reset_port);
		break;
	case msg_usa28:
		keyspan_usa28_send_setup(serial, port, reset_port);
		break;
	case msg_usa49:
		keyspan_usa49_send_setup(serial, port, reset_port);
		break;
	case msg_usa90:
		keyspan_usa90_send_setup(serial, port, reset_port);
		break;
	case msg_usa67:
		keyspan_usa67_send_setup(serial, port, reset_port);
		break;
	}
}


/* Gets called by the "real" driver (ie once firmware is loaded
   and renumeration has taken place. */
static int keyspan_startup(struct usb_serial *serial)
{
	int				i, err;
	struct usb_serial_port		*port;
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private	*p_priv;
	const struct keyspan_device_details	*d_details;

	for (i = 0; (d_details = keyspan_devices[i]) != NULL; ++i)
		if (d_details->product_id ==
				le16_to_cpu(serial->dev->descriptor.idProduct))
			break;
	if (d_details == NULL) {
		dev_err(&serial->dev->dev, "%s - unknown product id %x\n",
		    __func__, le16_to_cpu(serial->dev->descriptor.idProduct));
		return 1;
	}

	/* Setup private data for serial driver */
	s_priv = kzalloc(sizeof(struct keyspan_serial_private), GFP_KERNEL);
	if (!s_priv) {
		dbg("%s - kmalloc for keyspan_serial_private failed.",
								__func__);
		return -ENOMEM;
	}

	s_priv->device_details = d_details;
	usb_set_serial_data(serial, s_priv);

	/* Now setup per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		p_priv = kzalloc(sizeof(struct keyspan_port_private),
								GFP_KERNEL);
		if (!p_priv) {
			dbg("%s - kmalloc for keyspan_port_private (%d) failed!.", __func__, i);
			return 1;
		}
		p_priv->device_details = d_details;
		usb_set_serial_port_data(port, p_priv);
	}

	keyspan_setup_urbs(serial);

	if (s_priv->instat_urb != NULL) {
		err = usb_submit_urb(s_priv->instat_urb, GFP_KERNEL);
		if (err != 0)
			dbg("%s - submit instat urb failed %d", __func__,
				err);
	}
	if (s_priv->indat_urb != NULL) {
		err = usb_submit_urb(s_priv->indat_urb, GFP_KERNEL);
		if (err != 0)
			dbg("%s - submit indat urb failed %d", __func__,
				err);
	}

	return 0;
}

static void keyspan_disconnect(struct usb_serial *serial)
{
	int				i, j;
	struct usb_serial_port		*port;
	struct keyspan_serial_private 	*s_priv;
	struct keyspan_port_private	*p_priv;

	s_priv = usb_get_serial_data(serial);

	/* Stop reading/writing urbs */
	stop_urb(s_priv->instat_urb);
	stop_urb(s_priv->glocont_urb);
	stop_urb(s_priv->indat_urb);
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);
		stop_urb(p_priv->inack_urb);
		stop_urb(p_priv->outcont_urb);
		for (j = 0; j < 2; j++) {
			stop_urb(p_priv->in_urbs[j]);
			stop_urb(p_priv->out_urbs[j]);
		}
	}

	/* Now free them */
	usb_free_urb(s_priv->instat_urb);
	usb_free_urb(s_priv->indat_urb);
	usb_free_urb(s_priv->glocont_urb);
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		p_priv = usb_get_serial_port_data(port);
		usb_free_urb(p_priv->inack_urb);
		usb_free_urb(p_priv->outcont_urb);
		for (j = 0; j < 2; j++) {
			usb_free_urb(p_priv->in_urbs[j]);
			usb_free_urb(p_priv->out_urbs[j]);
		}
	}
}

static void keyspan_release(struct usb_serial *serial)
{
	int				i;
	struct usb_serial_port		*port;
	struct keyspan_serial_private 	*s_priv;

	s_priv = usb_get_serial_data(serial);

	/*  dbg("Freeing serial->private."); */
	kfree(s_priv);

	/*  dbg("Freeing port->private."); */
	/* Now free per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		kfree(usb_get_serial_port_data(port));
	}
}

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_FIRMWARE("keyspan/usa28.fw");
MODULE_FIRMWARE("keyspan/usa28x.fw");
MODULE_FIRMWARE("keyspan/usa28xa.fw");
MODULE_FIRMWARE("keyspan/usa28xb.fw");
MODULE_FIRMWARE("keyspan/usa19.fw");
MODULE_FIRMWARE("keyspan/usa19qi.fw");
MODULE_FIRMWARE("keyspan/mpr.fw");
MODULE_FIRMWARE("keyspan/usa19qw.fw");
MODULE_FIRMWARE("keyspan/usa18x.fw");
MODULE_FIRMWARE("keyspan/usa19w.fw");
MODULE_FIRMWARE("keyspan/usa49w.fw");
MODULE_FIRMWARE("keyspan/usa49wlc.fw");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

