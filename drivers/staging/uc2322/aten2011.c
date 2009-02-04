/*
 * Aten 2011 USB serial driver for 4 port devices
 *
 * Copyright (C) 2000 Inside Out Networks
 * Copyright (C) 2001-2002, 2009 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2009 Novell Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>


#define ZLP_REG1		0x3A	/* Zero_Flag_Reg1 58 */
#define ZLP_REG2		0x3B	/* Zero_Flag_Reg2 59 */
#define ZLP_REG3		0x3C	/* Zero_Flag_Reg3 60 */
#define ZLP_REG4		0x3D	/* Zero_Flag_Reg4 61 */
#define ZLP_REG5		0x3E	/* Zero_Flag_Reg5 62 */

/* Interrupt Rotinue Defines	*/
#define SERIAL_IIR_RLS		0x06
#define SERIAL_IIR_RDA		0x04
#define SERIAL_IIR_CTI		0x0c
#define SERIAL_IIR_THR		0x02
#define SERIAL_IIR_MS		0x00

/* Emulation of the bit mask on the LINE STATUS REGISTER.  */
#define SERIAL_LSR_DR		0x0001
#define SERIAL_LSR_OE		0x0002
#define SERIAL_LSR_PE		0x0004
#define SERIAL_LSR_FE		0x0008
#define SERIAL_LSR_BI		0x0010
#define SERIAL_LSR_THRE		0x0020
#define SERIAL_LSR_TEMT		0x0040
#define SERIAL_LSR_FIFOERR	0x0080

/* MSR bit defines(place holders) */
#define ATEN_MSR_DELTA_CTS	0x10
#define ATEN_MSR_DELTA_DSR	0x20
#define ATEN_MSR_DELTA_RI	0x40
#define ATEN_MSR_DELTA_CD	0x80

/* Serial Port register Address */
#define RECEIVE_BUFFER_REGISTER		((__u16)(0x00))
#define TRANSMIT_HOLDING_REGISTER	((__u16)(0x00))
#define INTERRUPT_ENABLE_REGISTER	((__u16)(0x01))
#define INTERRUPT_IDENT_REGISTER	((__u16)(0x02))
#define FIFO_CONTROL_REGISTER		((__u16)(0x02))
#define LINE_CONTROL_REGISTER		((__u16)(0x03))
#define MODEM_CONTROL_REGISTER		((__u16)(0x04))
#define LINE_STATUS_REGISTER		((__u16)(0x05))
#define MODEM_STATUS_REGISTER		((__u16)(0x06))
#define SCRATCH_PAD_REGISTER		((__u16)(0x07))
#define DIVISOR_LATCH_LSB		((__u16)(0x00))
#define DIVISOR_LATCH_MSB		((__u16)(0x01))

#define SP1_REGISTER			((__u16)(0x00))
#define CONTROL1_REGISTER		((__u16)(0x01))
#define CLK_MULTI_REGISTER		((__u16)(0x02))
#define CLK_START_VALUE_REGISTER	((__u16)(0x03))
#define DCR1_REGISTER			((__u16)(0x04))
#define GPIO_REGISTER			((__u16)(0x07))

#define SERIAL_LCR_DLAB			((__u16)(0x0080))

/*
 * URB POOL related defines
 */
#define NUM_URBS			16	/* URB Count */
#define URB_TRANSFER_BUFFER_SIZE	32	/* URB Size  */

#define USB_VENDOR_ID_ATENINTL		0x0557
#define ATENINTL_DEVICE_ID_2011		0x2011
#define ATENINTL_DEVICE_ID_7820		0x7820

static struct usb_device_id id_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_ATENINTL, ATENINTL_DEVICE_ID_2011) },
	{ USB_DEVICE(USB_VENDOR_ID_ATENINTL, ATENINTL_DEVICE_ID_7820) },
	{ } /* terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

/* This structure holds all of the local port information */
struct ATENINTL_port {
	int		port_num;          /*Actual port number in the device(1,2,etc)*/
	__u8		bulk_out_endpoint;   	/* the bulk out endpoint handle */
	unsigned char	*bulk_out_buffer;     	/* buffer used for the bulk out endpoint */
	struct urb	*write_urb;	     	/* write URB for this port */
	__u8		bulk_in_endpoint;	/* the bulk in endpoint handle */
	unsigned char	*bulk_in_buffer;	/* the buffer we use for the bulk in endpoint */
	struct urb	*read_urb;	     	/* read URB for this port */
	__u8		shadowLCR;		/* last LCR value received */
	__u8		shadowMCR;		/* last MCR value received */
	char		open;
	char		chaseResponsePending;
	wait_queue_head_t	wait_chase;		/* for handling sleeping while waiting for chase to finish */
	wait_queue_head_t	wait_command;		/* for handling sleeping while waiting for command to finish */
	struct async_icount	icount;
	struct usb_serial_port	*port;			/* loop back to the owner of this object */
	/*Offsets*/
	__u8		SpRegOffset;
	__u8		ControlRegOffset;
	__u8		DcrRegOffset;
	/* for processing control URBS in interrupt context */
	struct urb 	*control_urb;
	char		*ctrl_buf;
	int		MsrLsr;

	struct urb	*write_urb_pool[NUM_URBS];
	/* we pass a pointer to this as the arguement sent to cypress_set_termios old_termios */
	struct ktermios	tmp_termios;        /* stores the old termios settings */
	spinlock_t 	lock;                   /* private lock */
};

/* This structure holds all of the individual serial device information */
struct ATENINTL_serial {
	__u8		interrupt_in_endpoint;		/* the interrupt endpoint handle */
	unsigned char	*interrupt_in_buffer;		/* the buffer we use for the interrupt endpoint */
	struct urb	*interrupt_read_urb;	/* our interrupt urb */
	__u8		bulk_in_endpoint;	/* the bulk in endpoint handle */
	unsigned char	*bulk_in_buffer;		/* the buffer we use for the bulk in endpoint */
	struct urb 	*read_urb;		/* our bulk read urb */
	__u8		bulk_out_endpoint;	/* the bulk out endpoint handle */
	struct usb_serial	*serial;	/* loop back to the owner of this object */
	int	ATEN2011_spectrum_2or4ports; 	/* this says the number of ports in the device */
	/* Indicates about the no.of opened ports of an individual USB-serial adapater. */
	unsigned int	NoOfOpenPorts;
	/* a flag for Status endpoint polling */
	unsigned char	status_polling_started;
};

static void ATEN2011_set_termios(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 struct ktermios *old_termios);
static void ATEN2011_change_port_settings(struct tty_struct *tty,
					  struct ATENINTL_port *ATEN2011_port,
					  struct ktermios *old_termios);

/*************************************
 * Bit definitions for each register *
 *************************************/
#define LCR_BITS_5		0x00	/* 5 bits/char */
#define LCR_BITS_6		0x01	/* 6 bits/char */
#define LCR_BITS_7		0x02	/* 7 bits/char */
#define LCR_BITS_8		0x03	/* 8 bits/char */
#define LCR_BITS_MASK		0x03	/* Mask for bits/char field */

#define LCR_STOP_1		0x00	/* 1 stop bit */
#define LCR_STOP_1_5		0x04	/* 1.5 stop bits (if 5   bits/char) */
#define LCR_STOP_2		0x04	/* 2 stop bits   (if 6-8 bits/char) */
#define LCR_STOP_MASK		0x04	/* Mask for stop bits field */

#define LCR_PAR_NONE		0x00	/* No parity */
#define LCR_PAR_ODD		0x08	/* Odd parity */
#define LCR_PAR_EVEN		0x18	/* Even parity */
#define LCR_PAR_MARK		0x28	/* Force parity bit to 1 */
#define LCR_PAR_SPACE		0x38	/* Force parity bit to 0 */
#define LCR_PAR_MASK		0x38	/* Mask for parity field */

#define LCR_SET_BREAK		0x40	/* Set Break condition */
#define LCR_DL_ENABLE		0x80	/* Enable access to divisor latch */

#define MCR_DTR			0x01	/* Assert DTR */
#define MCR_RTS			0x02	/* Assert RTS */
#define MCR_OUT1		0x04	/* Loopback only: Sets state of RI */
#define MCR_MASTER_IE		0x08	/* Enable interrupt outputs */
#define MCR_LOOPBACK		0x10	/* Set internal (digital) loopback mode */
#define MCR_XON_ANY		0x20	/* Enable any char to exit XOFF mode */

#define ATEN2011_MSR_CTS	0x10	/* Current state of CTS */
#define ATEN2011_MSR_DSR	0x20	/* Current state of DSR */
#define ATEN2011_MSR_RI		0x40	/* Current state of RI */
#define ATEN2011_MSR_CD		0x80	/* Current state of CD */


static int debug;

/*
 * Version Information
 */
#define DRIVER_VERSION "2.0"
#define DRIVER_DESC "ATENINTL 2011 USB Serial Adapter"

/*
 * Defines used for sending commands to port
 */

#define ATEN_WDR_TIMEOUT	(50)	/* default urb timeout */

/* Requests */
#define ATEN_RD_RTYPE		0xC0
#define ATEN_WR_RTYPE		0x40
#define ATEN_RDREQ		0x0D
#define ATEN_WRREQ		0x0E
#define ATEN_CTRL_TIMEOUT	500
#define VENDOR_READ_LENGTH	(0x01)

/* set to 1 for RS485 mode and 0 for RS232 mode */
/* FIXME make this somehow dynamic and not build time specific */
static int RS485mode;

static int set_reg_sync(struct usb_serial_port *port, __u16 reg, __u16 val)
{
	struct usb_device *dev = port->serial->dev;
	val = val & 0x00ff;

	dbg("%s: is %x, value %x", __func__, reg, val);

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ATEN_WRREQ,
			       ATEN_WR_RTYPE, val, reg, NULL, 0,
			       ATEN_WDR_TIMEOUT);
}

static int get_reg_sync(struct usb_serial_port *port, __u16 reg, __u16 *val)
{
	struct usb_device *dev = port->serial->dev;
	int ret;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ATEN_RDREQ,
			      ATEN_RD_RTYPE, 0, reg, val, VENDOR_READ_LENGTH,
			      ATEN_WDR_TIMEOUT);
	dbg("%s: offset is %x, return val %x", __func__, reg, *val);
	*val = (*val) & 0x00ff;
	return ret;
}

static int set_uart_reg(struct usb_serial_port *port, __u16 reg, __u16 val)
{
	struct usb_device *dev = port->serial->dev;
	struct ATENINTL_serial *a_serial;
	__u16 minor;

	a_serial = usb_get_serial_data(port->serial);
	minor = port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;
	val = val & 0x00ff;

	/*
	 * For the UART control registers,
	 * the application number need to be Or'ed
	 */
	if (a_serial->ATEN2011_spectrum_2or4ports == 4)
		val |= (((__u16)port->number - minor) + 1) << 8;
	else {
		if (((__u16) port->number - minor) == 0)
			val |= (((__u16)port->number - minor) + 1) << 8;
		else
			val |= (((__u16)port->number - minor) + 2) << 8;
	}
	dbg("%s: application number is %x", __func__, val);

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ATEN_WRREQ,
			       ATEN_WR_RTYPE, val, reg, NULL, 0,
			       ATEN_WDR_TIMEOUT);
}

static int get_uart_reg(struct usb_serial_port *port, __u16 reg, __u16 *val)
{
	struct usb_device *dev = port->serial->dev;
	int ret = 0;
	__u16 wval;
	struct ATENINTL_serial *a_serial;
	__u16 minor = port->serial->minor;

	a_serial = usb_get_serial_data(port->serial);
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;

	/* wval is same as application number */
	if (a_serial->ATEN2011_spectrum_2or4ports == 4)
		wval = (((__u16)port->number - minor) + 1) << 8;
	else {
		if (((__u16) port->number - minor) == 0)
			wval = (((__u16) port->number - minor) + 1) << 8;
		else
			wval = (((__u16) port->number - minor) + 2) << 8;
	}
	dbg("%s: application number is %x", __func__, wval);
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ATEN_RDREQ,
			      ATEN_RD_RTYPE, wval, reg, val, VENDOR_READ_LENGTH,
			      ATEN_WDR_TIMEOUT);
	*val = (*val) & 0x00ff;
	return ret;
}

static int handle_newMsr(struct ATENINTL_port *port, __u8 newMsr)
{
	struct ATENINTL_port *ATEN2011_port;
	struct async_icount *icount;
	ATEN2011_port = port;
	icount = &ATEN2011_port->icount;
	if (newMsr &
	    (ATEN_MSR_DELTA_CTS | ATEN_MSR_DELTA_DSR | ATEN_MSR_DELTA_RI |
	     ATEN_MSR_DELTA_CD)) {
		icount = &ATEN2011_port->icount;

		/* update input line counters */
		if (newMsr & ATEN_MSR_DELTA_CTS)
			icount->cts++;
		if (newMsr & ATEN_MSR_DELTA_DSR)
			icount->dsr++;
		if (newMsr & ATEN_MSR_DELTA_CD)
			icount->dcd++;
		if (newMsr & ATEN_MSR_DELTA_RI)
			icount->rng++;
	}

	return 0;
}

static int handle_newLsr(struct ATENINTL_port *port, __u8 newLsr)
{
	struct async_icount *icount;

	dbg("%s - %02x", __func__, newLsr);

	if (newLsr & SERIAL_LSR_BI) {
		/*
		 * Parity and Framing errors only count if they occur exclusive
		 * of a break being received.
		 */
		newLsr &= (__u8) (SERIAL_LSR_OE | SERIAL_LSR_BI);
	}

	/* update input line counters */
	icount = &port->icount;
	if (newLsr & SERIAL_LSR_BI)
		icount->brk++;
	if (newLsr & SERIAL_LSR_OE)
		icount->overrun++;
	if (newLsr & SERIAL_LSR_PE)
		icount->parity++;
	if (newLsr & SERIAL_LSR_FE)
		icount->frame++;

	return 0;
}

static void ATEN2011_control_callback(struct urb *urb)
{
	unsigned char *data;
	struct ATENINTL_port *ATEN2011_port;
	__u8 regval = 0x0;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__,
		    urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__,
		    urb->status);
		goto exit;
	}

	ATEN2011_port = (struct ATENINTL_port *)urb->context;

	dbg("%s urb buffer size is %d", __func__, urb->actual_length);
	dbg("%s ATEN2011_port->MsrLsr is %d port %d", __func__,
		ATEN2011_port->MsrLsr, ATEN2011_port->port_num);
	data = urb->transfer_buffer;
	regval = (__u8) data[0];
	dbg("%s data is %x", __func__, regval);
	if (ATEN2011_port->MsrLsr == 0)
		handle_newMsr(ATEN2011_port, regval);
	else if (ATEN2011_port->MsrLsr == 1)
		handle_newLsr(ATEN2011_port, regval);

exit:
	return;
}

static int ATEN2011_get_reg(struct ATENINTL_port *ATEN, __u16 Wval, __u16 reg,
			    __u16 *val)
{
	struct usb_device *dev = ATEN->port->serial->dev;
	struct usb_ctrlrequest *dr = NULL;
	unsigned char *buffer = NULL;
	int ret = 0;
	buffer = (__u8 *) ATEN->ctrl_buf;

	dr = (void *)(buffer + 2);
	dr->bRequestType = ATEN_RD_RTYPE;
	dr->bRequest = ATEN_RDREQ;
	dr->wValue = cpu_to_le16(Wval);
	dr->wIndex = cpu_to_le16(reg);
	dr->wLength = cpu_to_le16(2);

	usb_fill_control_urb(ATEN->control_urb, dev, usb_rcvctrlpipe(dev, 0),
			     (unsigned char *)dr, buffer, 2,
			     ATEN2011_control_callback, ATEN);
	ATEN->control_urb->transfer_buffer_length = 2;
	ret = usb_submit_urb(ATEN->control_urb, GFP_ATOMIC);
	return ret;
}

static void ATEN2011_interrupt_callback(struct urb *urb)
{
	int result;
	int length;
	struct ATENINTL_port *ATEN2011_port;
	struct ATENINTL_serial *ATEN2011_serial;
	struct usb_serial *serial;
	__u16 Data;
	unsigned char *data;
	__u8 sp[5], st;
	int i;
	__u16 wval;
	int minor;

	dbg("%s", " : Entering");

	ATEN2011_serial = (struct ATENINTL_serial *)urb->context;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__,
		    urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__,
		    urb->status);
		goto exit;
	}
	length = urb->actual_length;
	data = urb->transfer_buffer;

	serial = ATEN2011_serial->serial;

	/* ATENINTL get 5 bytes
	 * Byte 1 IIR Port 1 (port.number is 0)
	 * Byte 2 IIR Port 2 (port.number is 1)
	 * Byte 3 IIR Port 3 (port.number is 2)
	 * Byte 4 IIR Port 4 (port.number is 3)
	 * Byte 5 FIFO status for both */

	if (length && length > 5) {
		dbg("%s", "Wrong data !!!");
		return;
	}

	/* MATRIX */
	if (ATEN2011_serial->ATEN2011_spectrum_2or4ports == 4) {
		sp[0] = (__u8) data[0];
		sp[1] = (__u8) data[1];
		sp[2] = (__u8) data[2];
		sp[3] = (__u8) data[3];
		st = (__u8) data[4];
	} else {
		sp[0] = (__u8) data[0];
		sp[1] = (__u8) data[2];
		/* sp[2]=(__u8)data[2]; */
		/* sp[3]=(__u8)data[3]; */
		st = (__u8) data[4];

	}
	for (i = 0; i < serial->num_ports; i++) {
		ATEN2011_port = usb_get_serial_port_data(serial->port[i]);
		minor = serial->minor;
		if (minor == SERIAL_TTY_NO_MINOR)
			minor = 0;
		if ((ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)
		    && (i != 0))
			wval =
			    (((__u16) serial->port[i]->number -
			      (__u16) (minor)) + 2) << 8;
		else
			wval =
			    (((__u16) serial->port[i]->number -
			      (__u16) (minor)) + 1) << 8;
		if (ATEN2011_port->open != 0) {
			if (sp[i] & 0x01) {
				dbg("SP%d No Interrupt !!!", i);
			} else {
				switch (sp[i] & 0x0f) {
				case SERIAL_IIR_RLS:
					dbg("Serial Port %d: Receiver status error or address bit detected in 9-bit mode", i);
					ATEN2011_port->MsrLsr = 1;
					ATEN2011_get_reg(ATEN2011_port, wval,
							 LINE_STATUS_REGISTER,
							 &Data);
					break;
				case SERIAL_IIR_MS:
					dbg("Serial Port %d: Modem status change", i);
					ATEN2011_port->MsrLsr = 0;
					ATEN2011_get_reg(ATEN2011_port, wval,
							 MODEM_STATUS_REGISTER,
							 &Data);
					break;
				}
			}
		}

	}
exit:
	if (ATEN2011_serial->status_polling_started == 0)
		return;

	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result) {
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting interrupt urb\n",
			__func__, result);
	}

	return;
}

static void ATEN2011_bulk_in_callback(struct urb *urb)
{
	int status;
	unsigned char *data;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	struct tty_struct *tty;

	if (urb->status) {
		dbg("nonzero read bulk status received: %d", urb->status);
		return;
	}

	ATEN2011_port = (struct ATENINTL_port *)urb->context;

	port = (struct usb_serial_port *)ATEN2011_port->port;
	serial = port->serial;

	dbg("%s", "Entering...");

	data = urb->transfer_buffer;
	ATEN2011_serial = usb_get_serial_data(serial);

	if (urb->actual_length) {
		tty = tty_port_tty_get(&ATEN2011_port->port->port);
		if (tty) {
			tty_buffer_request_room(tty, urb->actual_length);
			tty_insert_flip_string(tty, data, urb->actual_length);
			tty_flip_buffer_push(tty);
			tty_kref_put(tty);
		}

		ATEN2011_port->icount.rx += urb->actual_length;
		dbg("ATEN2011_port->icount.rx is %d:",
			ATEN2011_port->icount.rx);
	}

	if (!ATEN2011_port->read_urb) {
		dbg("%s", "URB KILLED !!!");
		return;
	}

	if (ATEN2011_port->read_urb->status != -EINPROGRESS) {
		ATEN2011_port->read_urb->dev = serial->dev;

		status = usb_submit_urb(ATEN2011_port->read_urb, GFP_ATOMIC);
		if (status)
			dbg("usb_submit_urb(read bulk) failed, status = %d", status);
	}
}

static void ATEN2011_bulk_out_data_callback(struct urb *urb)
{
	struct ATENINTL_port *ATEN2011_port;
	struct tty_struct *tty;

	if (urb->status) {
		dbg("nonzero write bulk status received:%d", urb->status);
		return;
	}

	ATEN2011_port = (struct ATENINTL_port *)urb->context;

	dbg("%s", "Entering .........");

	tty = tty_port_tty_get(&ATEN2011_port->port->port);

	if (tty && ATEN2011_port->open) {
		/* tell the tty driver that something has changed */
		wake_up_interruptible(&tty->write_wait);
	}

	/* schedule_work(&ATEN2011_port->port->work); */
	tty_kref_put(tty);

}

#ifdef ATENSerialProbe
static int ATEN2011_serial_probe(struct usb_serial *serial,
				 const struct usb_device_id *id)
{

	/*need to implement the mode_reg reading and updating\
	   structures usb_serial_ device_type\
	   (i.e num_ports, num_bulkin,bulkout etc) */
	/* Also we can update the changes  attach */
	return 1;
}
#endif

static int ATEN2011_open(struct tty_struct *tty, struct usb_serial_port *port,
			 struct file *filp)
{
	int response;
	int j;
	struct usb_serial *serial;
	struct urb *urb;
	__u16 Data;
	int status;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	struct ktermios tmp_termios;
	int minor;

	serial = port->serial;

	ATEN2011_port = usb_get_serial_port_data(port);

	if (ATEN2011_port == NULL)
		return -ENODEV;

	ATEN2011_serial = usb_get_serial_data(serial);
	if (ATEN2011_serial == NULL)
		return -ENODEV;

	/* increment the number of opened ports counter here */
	ATEN2011_serial->NoOfOpenPorts++;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	/* Initialising the write urb pool */
	for (j = 0; j < NUM_URBS; ++j) {
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		ATEN2011_port->write_urb_pool[j] = urb;

		if (urb == NULL) {
			err("No more urbs???");
			continue;
		}

		urb->transfer_buffer = NULL;
		urb->transfer_buffer =
		    kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
		if (!urb->transfer_buffer) {
			err("%s-out of memory for urb buffers.", __func__);
			continue;
		}
	}

/*****************************************************************************
 * Initialize ATEN2011 -- Write Init values to corresponding Registers
 *
 * Register Index
 * 1 : IER
 * 2 : FCR
 * 3 : LCR
 * 4 : MCR
 *
 * 0x08 : SP1/2 Control Reg
 *****************************************************************************/

/* NEED to check the fallowing Block */

	Data = 0x0;
	status = get_reg_sync(port, ATEN2011_port->SpRegOffset, &Data);
	if (status < 0) {
		dbg("Reading Spreg failed");
		return -1;
	}
	Data |= 0x80;
	status = set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
	if (status < 0) {
		dbg("writing Spreg failed");
		return -1;
	}

	Data &= ~0x80;
	status = set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
	if (status < 0) {
		dbg("writing Spreg failed");
		return -1;
	}

/* End of block to be checked */
/**************************CHECK***************************/

	if (RS485mode == 0)
		Data = 0xC0;
	else
		Data = 0x00;
	status = set_uart_reg(port, SCRATCH_PAD_REGISTER, Data);
	if (status < 0) {
		dbg("Writing SCRATCH_PAD_REGISTER failed status-0x%x", status);
		return -1;
	} else
		dbg("SCRATCH_PAD_REGISTER Writing success status%d", status);

/**************************CHECK***************************/

	Data = 0x0;
	status = get_reg_sync(port, ATEN2011_port->ControlRegOffset, &Data);
	if (status < 0) {
		dbg("Reading Controlreg failed");
		return -1;
	}
	Data |= 0x08;		/* Driver done bit */
	Data |= 0x20;		/* rx_disable */
	status = 0;
	status =
	    set_reg_sync(port, ATEN2011_port->ControlRegOffset, Data);
	if (status < 0) {
		dbg("writing Controlreg failed");
		return -1;
	}
	/*
	 * do register settings here
	 * Set all regs to the device default values.
	 * First Disable all interrupts.
	 */

	Data = 0x00;
	status = set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);
	if (status < 0) {
		dbg("disableing interrupts failed");
		return -1;
	}
	/* Set FIFO_CONTROL_REGISTER to the default value */
	Data = 0x00;
	status = set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);
	if (status < 0) {
		dbg("Writing FIFO_CONTROL_REGISTER  failed");
		return -1;
	}

	Data = 0xcf;		/* chk */
	status = set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);
	if (status < 0) {
		dbg("Writing FIFO_CONTROL_REGISTER  failed");
		return -1;
	}

	Data = 0x03;		/* LCR_BITS_8 */
	status = set_uart_reg(port, LINE_CONTROL_REGISTER, Data);
	ATEN2011_port->shadowLCR = Data;

	Data = 0x0b;		/* MCR_DTR|MCR_RTS|MCR_MASTER_IE */
	status = set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
	ATEN2011_port->shadowMCR = Data;

#ifdef Check
	Data = 0x00;
	status = get_uart_reg(port, LINE_CONTROL_REGISTER, &Data);
	ATEN2011_port->shadowLCR = Data;

	Data |= SERIAL_LCR_DLAB;	/* data latch enable in LCR 0x80 */
	status = set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

	Data = 0x0c;
	status = set_uart_reg(port, DIVISOR_LATCH_LSB, Data);

	Data = 0x0;
	status = set_uart_reg(port, DIVISOR_LATCH_MSB, Data);

	Data = 0x00;
	status = get_uart_reg(port, LINE_CONTROL_REGISTER, &Data);

/*      Data = ATEN2011_port->shadowLCR; */	/* data latch disable */
	Data = Data & ~SERIAL_LCR_DLAB;
	status = set_uart_reg(port, LINE_CONTROL_REGISTER, Data);
	ATEN2011_port->shadowLCR = Data;
#endif
	/* clearing Bulkin and Bulkout Fifo */
	Data = 0x0;
	status = get_reg_sync(port, ATEN2011_port->SpRegOffset, &Data);

	Data = Data | 0x0c;
	status = set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);

	Data = Data & ~0x0c;
	status = set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
	/* Finally enable all interrupts */
	Data = 0x0;
	Data = 0x0c;
	status = set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	/* clearing rx_disable */
	Data = 0x0;
	status = get_reg_sync(port, ATEN2011_port->ControlRegOffset, &Data);
	Data = Data & ~0x20;
	status = set_reg_sync(port, ATEN2011_port->ControlRegOffset, Data);

	/* rx_negate */
	Data = 0x0;
	status = get_reg_sync(port, ATEN2011_port->ControlRegOffset, &Data);
	Data = Data | 0x10;
	status = 0;
	status = set_reg_sync(port, ATEN2011_port->ControlRegOffset, Data);

	/* force low_latency on so that our tty_push actually forces *
	 * the data through,otherwise it is scheduled, and with      *
	 * high data rates (like with OHCI) data can get lost.       */

	if (tty)
		tty->low_latency = 1;
	/*
	 * Check to see if we've set up our endpoint info yet
	 * (can't set it up in ATEN2011_startup as the structures
	 * were not set up at that time.)
	 */
	if (ATEN2011_serial->NoOfOpenPorts == 1) {
		/* start the status polling here */
		ATEN2011_serial->status_polling_started = 1;
		/* If not yet set, Set here */
		ATEN2011_serial->interrupt_in_buffer =
		    serial->port[0]->interrupt_in_buffer;
		ATEN2011_serial->interrupt_in_endpoint =
		    serial->port[0]->interrupt_in_endpointAddress;
		ATEN2011_serial->interrupt_read_urb =
		    serial->port[0]->interrupt_in_urb;

		/* set up interrupt urb */
		usb_fill_int_urb(ATEN2011_serial->interrupt_read_urb,
				 serial->dev,
				 usb_rcvintpipe(serial->dev,
						ATEN2011_serial->
						interrupt_in_endpoint),
				 ATEN2011_serial->interrupt_in_buffer,
				 ATEN2011_serial->interrupt_read_urb->
				 transfer_buffer_length,
				 ATEN2011_interrupt_callback, ATEN2011_serial,
				 ATEN2011_serial->interrupt_read_urb->interval);

		/* start interrupt read for ATEN2011               *
		 * will continue as long as ATEN2011 is connected  */

		response =
		    usb_submit_urb(ATEN2011_serial->interrupt_read_urb,
				   GFP_KERNEL);
		if (response) {
			dbg("%s - Error %d submitting interrupt urb",
				__func__, response);
		}

	}

	/*
	 * See if we've set up our endpoint info yet
	 * (can't set it up in ATEN2011_startup as the
	 * structures were not set up at that time.)
	 */

	dbg("port number is %d", port->number);
	dbg("serial number is %d", port->serial->minor);
	dbg("Bulkin endpoint is %d", port->bulk_in_endpointAddress);
	dbg("BulkOut endpoint is %d", port->bulk_out_endpointAddress);
	dbg("Interrupt endpoint is %d",
		port->interrupt_in_endpointAddress);
	dbg("port's number in the device is %d", ATEN2011_port->port_num);
	ATEN2011_port->bulk_in_buffer = port->bulk_in_buffer;
	ATEN2011_port->bulk_in_endpoint = port->bulk_in_endpointAddress;
	ATEN2011_port->read_urb = port->read_urb;
	ATEN2011_port->bulk_out_endpoint = port->bulk_out_endpointAddress;

	minor = port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;

	/* set up our bulk in urb */
	if ((ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)
	    && (((__u16) port->number - (__u16) (minor)) != 0)) {
		usb_fill_bulk_urb(ATEN2011_port->read_urb, serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  (port->
						   bulk_in_endpointAddress +
						   2)), port->bulk_in_buffer,
				  ATEN2011_port->read_urb->
				  transfer_buffer_length,
				  ATEN2011_bulk_in_callback, ATEN2011_port);
	} else
		usb_fill_bulk_urb(ATEN2011_port->read_urb,
				  serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  port->
						  bulk_in_endpointAddress),
				  port->bulk_in_buffer,
				  ATEN2011_port->read_urb->
				  transfer_buffer_length,
				  ATEN2011_bulk_in_callback, ATEN2011_port);

	dbg("ATEN2011_open: bulkin endpoint is %d",
		port->bulk_in_endpointAddress);
	response = usb_submit_urb(ATEN2011_port->read_urb, GFP_KERNEL);
	if (response) {
		err("%s - Error %d submitting control urb", __func__,
		    response);
	}

	/* initialize our wait queues */
	init_waitqueue_head(&ATEN2011_port->wait_chase);
	init_waitqueue_head(&ATEN2011_port->wait_command);

	/* initialize our icount structure */
	memset(&(ATEN2011_port->icount), 0x00, sizeof(ATEN2011_port->icount));

	/* initialize our port settings */
	ATEN2011_port->shadowMCR = MCR_MASTER_IE;	/* Must set to enable ints! */
	ATEN2011_port->chaseResponsePending = 0;
	/* send a open port command */
	ATEN2011_port->open = 1;
	/* ATEN2011_change_port_settings(ATEN2011_port,old_termios); */
	/* Setup termios */
	ATEN2011_set_termios(tty, port, &tmp_termios);
	ATEN2011_port->icount.tx = 0;
	ATEN2011_port->icount.rx = 0;

	dbg("usb_serial serial:%x       ATEN2011_port:%x\nATEN2011_serial:%x      usb_serial_port port:%x",
	     (unsigned int)serial, (unsigned int)ATEN2011_port,
	     (unsigned int)ATEN2011_serial, (unsigned int)port);

	return 0;

}

static int ATEN2011_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int chars = 0;
	struct ATENINTL_port *ATEN2011_port;

	/* dbg("%s"," ATEN2011_chars_in_buffer:entering ..........."); */

	ATEN2011_port = usb_get_serial_port_data(port);
	if (ATEN2011_port == NULL) {
		dbg("%s", "ATEN2011_break:leaving ...........");
		return -1;
	}

	for (i = 0; i < NUM_URBS; ++i)
		if (ATEN2011_port->write_urb_pool[i]->status == -EINPROGRESS)
			chars += URB_TRANSFER_BUFFER_SIZE;

	dbg("%s - returns %d", __func__, chars);
	return chars;

}

static void ATEN2011_block_until_tx_empty(struct tty_struct *tty,
					  struct ATENINTL_port *ATEN2011_port)
{
	int timeout = HZ / 10;
	int wait = 30;
	int count;

	while (1) {
		count = ATEN2011_chars_in_buffer(tty);

		/* Check for Buffer status */
		if (count <= 0)
			return;

		/* Block the thread for a while */
		interruptible_sleep_on_timeout(&ATEN2011_port->wait_chase,
					       timeout);

		/* No activity.. count down section */
		wait--;
		if (wait == 0) {
			dbg("%s - TIMEOUT", __func__);
			return;
		} else {
			/* Reset timout value back to seconds */
			wait = 30;
		}
	}
}

static void ATEN2011_close(struct tty_struct *tty, struct usb_serial_port *port,
			   struct file *filp)
{
	struct usb_serial *serial;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	int no_urbs;
	__u16 Data;

	dbg("%s", "ATEN2011_close:entering...");
	serial = port->serial;

	/* take the Adpater and port's private data */
	ATEN2011_serial = usb_get_serial_data(serial);
	ATEN2011_port = usb_get_serial_port_data(port);
	if ((ATEN2011_serial == NULL) || (ATEN2011_port == NULL))
		return;

	if (serial->dev) {
		/* flush and block(wait) until tx is empty */
		ATEN2011_block_until_tx_empty(tty, ATEN2011_port);
	}
	/* kill the ports URB's */
	for (no_urbs = 0; no_urbs < NUM_URBS; no_urbs++)
		usb_kill_urb(ATEN2011_port->write_urb_pool[no_urbs]);
	/* Freeing Write URBs */
	for (no_urbs = 0; no_urbs < NUM_URBS; ++no_urbs) {
		kfree(ATEN2011_port->write_urb_pool[no_urbs]->transfer_buffer);
		usb_free_urb(ATEN2011_port->write_urb_pool[no_urbs]);
	}
	/* While closing port, shutdown all bulk read, write  *
	 * and interrupt read if they exists                  */
	if (serial->dev) {
		if (ATEN2011_port->write_urb) {
			dbg("%s", "Shutdown bulk write");
			usb_kill_urb(ATEN2011_port->write_urb);
		}
		if (ATEN2011_port->read_urb) {
			dbg("%s", "Shutdown bulk read");
			usb_kill_urb(ATEN2011_port->read_urb);
		}
		if ((&ATEN2011_port->control_urb)) {
			dbg("%s", "Shutdown control read");
			/* usb_kill_urb (ATEN2011_port->control_urb); */

		}
	}
	/* if(ATEN2011_port->ctrl_buf != NULL) */
		/* kfree(ATEN2011_port->ctrl_buf); */
	/* decrement the no.of open ports counter of an individual USB-serial adapter. */
	ATEN2011_serial->NoOfOpenPorts--;
	dbg("NoOfOpenPorts in close%d:in port%d",
		ATEN2011_serial->NoOfOpenPorts, port->number);
	if (ATEN2011_serial->NoOfOpenPorts == 0) {
		/* stop the stus polling here */
		ATEN2011_serial->status_polling_started = 0;
		if (ATEN2011_serial->interrupt_read_urb) {
			dbg("%s", "Shutdown interrupt_read_urb");
			/* ATEN2011_serial->interrupt_in_buffer=NULL; */
			/* usb_kill_urb (ATEN2011_serial->interrupt_read_urb); */
		}
	}
	if (ATEN2011_port->write_urb) {
		/* if this urb had a transfer buffer already (old tx) free it */
		kfree(ATEN2011_port->write_urb->transfer_buffer);
		usb_free_urb(ATEN2011_port->write_urb);
	}

	/* clear the MCR & IER */
	Data = 0x00;
	set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
	Data = 0x00;
	set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	ATEN2011_port->open = 0;
	dbg("%s", "Leaving ............");

}

static void ATEN2011_block_until_chase_response(struct tty_struct *tty,
						struct ATENINTL_port
						*ATEN2011_port)
{
	int timeout = 1 * HZ;
	int wait = 10;
	int count;

	while (1) {
		count = ATEN2011_chars_in_buffer(tty);

		/* Check for Buffer status */
		if (count <= 0) {
			ATEN2011_port->chaseResponsePending = 0;
			return;
		}

		/* Block the thread for a while */
		interruptible_sleep_on_timeout(&ATEN2011_port->wait_chase,
					       timeout);
		/* No activity.. count down section */
		wait--;
		if (wait == 0) {
			dbg("%s - TIMEOUT", __func__);
			return;
		} else {
			/* Reset timout value back to seconds */
			wait = 10;
		}
	}

}

static void ATEN2011_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned char data;
	struct usb_serial *serial;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;

	dbg("%s", "Entering ...........");
	dbg("ATEN2011_break: Start");

	serial = port->serial;

	ATEN2011_serial = usb_get_serial_data(serial);
	ATEN2011_port = usb_get_serial_port_data(port);

	if ((ATEN2011_serial == NULL) || (ATEN2011_port == NULL))
		return;

	/* flush and chase */
	ATEN2011_port->chaseResponsePending = 1;

	if (serial->dev) {
		/* flush and block until tx is empty */
		ATEN2011_block_until_chase_response(tty, ATEN2011_port);
	}

	if (break_state == -1)
		data = ATEN2011_port->shadowLCR | LCR_SET_BREAK;
	else
		data = ATEN2011_port->shadowLCR & ~LCR_SET_BREAK;

	ATEN2011_port->shadowLCR = data;
	dbg("ATEN2011_break ATEN2011_port->shadowLCR is %x",
		ATEN2011_port->shadowLCR);
	set_uart_reg(port, LINE_CONTROL_REGISTER, ATEN2011_port->shadowLCR);

	return;
}

static int ATEN2011_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int i;
	int room = 0;
	struct ATENINTL_port *ATEN2011_port;

	ATEN2011_port = usb_get_serial_port_data(port);
	if (ATEN2011_port == NULL) {
		dbg("%s", "ATEN2011_break:leaving ...........");
		return -1;
	}

	for (i = 0; i < NUM_URBS; ++i)
		if (ATEN2011_port->write_urb_pool[i]->status != -EINPROGRESS)
			room += URB_TRANSFER_BUFFER_SIZE;

	dbg("%s - returns %d", __func__, room);
	return room;

}

static int ATEN2011_write(struct tty_struct *tty, struct usb_serial_port *port,
			  const unsigned char *data, int count)
{
	int status;
	int i;
	int bytes_sent = 0;
	int transfer_size;
	int minor;

	struct ATENINTL_port *ATEN2011_port;
	struct usb_serial *serial;
	struct ATENINTL_serial *ATEN2011_serial;
	struct urb *urb;
	const unsigned char *current_position = data;
	unsigned char *data1;
	dbg("%s", "entering ...........");

	serial = port->serial;

	ATEN2011_port = usb_get_serial_port_data(port);
	if (ATEN2011_port == NULL) {
		dbg("%s", "ATEN2011_port is NULL");
		return -1;
	}

	ATEN2011_serial = usb_get_serial_data(serial);
	if (ATEN2011_serial == NULL) {
		dbg("%s", "ATEN2011_serial is NULL");
		return -1;
	}

	/* try to find a free urb in the list */
	urb = NULL;

	for (i = 0; i < NUM_URBS; ++i) {
		if (ATEN2011_port->write_urb_pool[i]->status != -EINPROGRESS) {
			urb = ATEN2011_port->write_urb_pool[i];
			dbg("URB:%d", i);
			break;
		}
	}

	if (urb == NULL) {
		dbg("%s - no more free urbs", __func__);
		goto exit;
	}

	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer =
		    kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);

		if (urb->transfer_buffer == NULL) {
			err("%s no more kernel memory...", __func__);
			goto exit;
		}
	}
	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);

	memcpy(urb->transfer_buffer, current_position, transfer_size);
	/* usb_serial_debug_data (__FILE__, __func__, transfer_size, urb->transfer_buffer); */

	/* fill urb with data and submit  */
	minor = port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;
	if ((ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)
	    && (((__u16) port->number - (__u16) (minor)) != 0)) {
		usb_fill_bulk_urb(urb, ATEN2011_serial->serial->dev,
				  usb_sndbulkpipe(ATEN2011_serial->serial->dev,
						  (port->
						   bulk_out_endpointAddress) +
						  2), urb->transfer_buffer,
				  transfer_size,
				  ATEN2011_bulk_out_data_callback,
				  ATEN2011_port);
	} else

		usb_fill_bulk_urb(urb,
				  ATEN2011_serial->serial->dev,
				  usb_sndbulkpipe(ATEN2011_serial->serial->dev,
						  port->
						  bulk_out_endpointAddress),
				  urb->transfer_buffer, transfer_size,
				  ATEN2011_bulk_out_data_callback,
				  ATEN2011_port);

	data1 = urb->transfer_buffer;
	dbg("bulkout endpoint is %d", port->bulk_out_endpointAddress);
	/* for(i=0;i < urb->actual_length;i++) */
		/* dbg("Data is %c ",data1[i]); */

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);

	if (status) {
		err("%s - usb_submit_urb(write bulk) failed with status = %d",
		    __func__, status);
		bytes_sent = status;
		goto exit;
	}
	bytes_sent = transfer_size;
	ATEN2011_port->icount.tx += transfer_size;
	dbg("ATEN2011_port->icount.tx is %d:", ATEN2011_port->icount.tx);

exit:
	return bytes_sent;
}

static void ATEN2011_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ATENINTL_port *ATEN2011_port;
	int status;

	dbg("- port %d", port->number);

	ATEN2011_port = usb_get_serial_port_data(port);

	if (ATEN2011_port == NULL)
		return;

	if (!ATEN2011_port->open) {
		dbg("%s", "port not opened");
		return;
	}

	dbg("%s", "Entering .......... ");

	if (!tty) {
		dbg("%s - no tty available", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		status = ATEN2011_write(tty, port, &stop_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		ATEN2011_port->shadowMCR &= ~MCR_RTS;
		status = set_uart_reg(port, MODEM_CONTROL_REGISTER,
				      ATEN2011_port->shadowMCR);
		if (status < 0)
			return;
	}

	return;
}

static void ATEN2011_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	int status;
	struct ATENINTL_port *ATEN2011_port = usb_get_serial_port_data(port);

	if (ATEN2011_port == NULL)
		return;

	if (!ATEN2011_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	dbg("%s", "Entering .......... ");

	if (!tty) {
		dbg("%s - no tty available", __func__);
		return;
	}

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
		status = ATEN2011_write(tty, port, &start_char, 1);
		if (status <= 0)
			return;
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		ATEN2011_port->shadowMCR |= MCR_RTS;
		status = set_uart_reg(port, MODEM_CONTROL_REGISTER,
				      ATEN2011_port->shadowMCR);
		if (status < 0)
			return;
	}

	return;
}

static int ATEN2011_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ATENINTL_port *ATEN2011_port;
	unsigned int result;
	__u16 msr;
	__u16 mcr;
	/* unsigned int mcr; */
	int status = 0;
	ATEN2011_port = usb_get_serial_port_data(port);

	dbg("%s - port %d", __func__, port->number);

	if (ATEN2011_port == NULL)
		return -ENODEV;

	status = get_uart_reg(port, MODEM_STATUS_REGISTER, &msr);
	status = get_uart_reg(port, MODEM_CONTROL_REGISTER, &mcr);
	/* mcr = ATEN2011_port->shadowMCR; */
	/* COMMENT2: the Fallowing three line are commented for updating only MSR values */
	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)
	    | ((mcr & MCR_RTS) ? TIOCM_RTS : 0)
	    | ((mcr & MCR_LOOPBACK) ? TIOCM_LOOP : 0)
	    | ((msr & ATEN2011_MSR_CTS) ? TIOCM_CTS : 0)
	    | ((msr & ATEN2011_MSR_CD) ? TIOCM_CAR : 0)
	    | ((msr & ATEN2011_MSR_RI) ? TIOCM_RI : 0)
	    | ((msr & ATEN2011_MSR_DSR) ? TIOCM_DSR : 0);

	dbg("%s - 0x%04X", __func__, result);

	return result;
}

static int ATEN2011_tiocmset(struct tty_struct *tty, struct file *file,
			     unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ATENINTL_port *ATEN2011_port;
	unsigned int mcr;
	unsigned int status;

	dbg("%s - port %d", __func__, port->number);

	ATEN2011_port = usb_get_serial_port_data(port);

	if (ATEN2011_port == NULL)
		return -ENODEV;

	mcr = ATEN2011_port->shadowMCR;
	if (clear & TIOCM_RTS)
		mcr &= ~MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~MCR_LOOPBACK;

	if (set & TIOCM_RTS)
		mcr |= MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= MCR_LOOPBACK;

	ATEN2011_port->shadowMCR = mcr;

	status = set_uart_reg(port, MODEM_CONTROL_REGISTER, mcr);
	if (status < 0) {
		dbg("setting MODEM_CONTROL_REGISTER Failed");
		return -1;
	}

	return 0;
}

static void ATEN2011_set_termios(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 struct ktermios *old_termios)
{
	int status;
	unsigned int cflag;
	struct usb_serial *serial;
	struct ATENINTL_port *ATEN2011_port;

	dbg("ATEN2011_set_termios: START");

	serial = port->serial;

	ATEN2011_port = usb_get_serial_port_data(port);

	if (ATEN2011_port == NULL)
		return;

	if (!ATEN2011_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	dbg("%s", "setting termios - ");

	cflag = tty->termios->c_cflag;

	if (!cflag) {
		dbg("%s %s", __func__, "cflag is NULL");
		return;
	}

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(tty->termios->c_iflag) ==
		     RELEVANT_IFLAG(old_termios->c_iflag))) {
			dbg("%s", "Nothing to change");
			return;
		}
	}

	dbg("%s - clfag %08x iflag %08x", __func__,
	    tty->termios->c_cflag, RELEVANT_IFLAG(tty->termios->c_iflag));

	if (old_termios) {
		dbg("%s - old clfag %08x old iflag %08x", __func__,
		    old_termios->c_cflag, RELEVANT_IFLAG(old_termios->c_iflag));
	}

	dbg("%s - port %d", __func__, port->number);

	/* change the port settings to the new ones specified */

	ATEN2011_change_port_settings(tty, ATEN2011_port, old_termios);

	if (!ATEN2011_port->read_urb) {
		dbg("%s", "URB KILLED !!!!!");
		return;
	}

	if (ATEN2011_port->read_urb->status != -EINPROGRESS) {
		ATEN2011_port->read_urb->dev = serial->dev;
		status = usb_submit_urb(ATEN2011_port->read_urb, GFP_ATOMIC);
		if (status) {
			dbg
			    (" usb_submit_urb(read bulk) failed, status = %d",
			     status);
		}
	}
	return;
}

static int get_lsr_info(struct tty_struct *tty,
			struct ATENINTL_port *ATEN2011_port,
			unsigned int __user *value)
{
	int count;
	unsigned int result = 0;

	count = ATEN2011_chars_in_buffer(tty);
	if (count == 0) {
		dbg("%s -- Empty", __func__);
		result = TIOCSER_TEMT;
	}

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int get_number_bytes_avail(struct tty_struct *tty,
				  struct ATENINTL_port *ATEN2011_port,
				  unsigned int __user *value)
{
	unsigned int result = 0;

	if (!tty)
		return -ENOIOCTLCMD;

	result = tty->read_cnt;

	dbg("%s(%d) = %d", __func__, ATEN2011_port->port->number, result);
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;

	return -ENOIOCTLCMD;
}

static int set_modem_info(struct ATENINTL_port *ATEN2011_port, unsigned int cmd,
			  unsigned int __user *value)
{
	unsigned int mcr;
	unsigned int arg;
	__u16 Data;
	int status;
	struct usb_serial_port *port;

	if (ATEN2011_port == NULL)
		return -1;

	port = (struct usb_serial_port *)ATEN2011_port->port;

	mcr = ATEN2011_port->shadowMCR;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS)
			mcr |= MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr |= MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr |= MCR_LOOPBACK;
		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			mcr &= ~MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr &= ~MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr &= ~MCR_LOOPBACK;
		break;

	case TIOCMSET:
		/* turn off the RTS and DTR and LOOPBACK
		 * and then only turn on what was asked to */
		mcr &= ~(MCR_RTS | MCR_DTR | MCR_LOOPBACK);
		mcr |= ((arg & TIOCM_RTS) ? MCR_RTS : 0);
		mcr |= ((arg & TIOCM_DTR) ? MCR_DTR : 0);
		mcr |= ((arg & TIOCM_LOOP) ? MCR_LOOPBACK : 0);
		break;
	}

	ATEN2011_port->shadowMCR = mcr;

	Data = ATEN2011_port->shadowMCR;
	status = set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
	if (status < 0) {
		dbg("setting MODEM_CONTROL_REGISTER Failed");
		return -1;
	}

	return 0;
}

static int get_modem_info(struct ATENINTL_port *ATEN2011_port,
			  unsigned int __user *value)
{
	unsigned int result = 0;
	__u16 msr;
	unsigned int mcr = ATEN2011_port->shadowMCR;
	int status;

	status = get_uart_reg(ATEN2011_port->port, MODEM_STATUS_REGISTER, &msr);
	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)	/* 0x002 */
	    |((mcr & MCR_RTS) ? TIOCM_RTS : 0)	/* 0x004 */
	    |((msr & ATEN2011_MSR_CTS) ? TIOCM_CTS : 0)	/* 0x020 */
	    |((msr & ATEN2011_MSR_CD) ? TIOCM_CAR : 0)	/* 0x040 */
	    |((msr & ATEN2011_MSR_RI) ? TIOCM_RI : 0)	/* 0x080 */
	    |((msr & ATEN2011_MSR_DSR) ? TIOCM_DSR : 0);	/* 0x100 */

	dbg("%s -- %x", __func__, result);

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

static int get_serial_info(struct ATENINTL_port *ATEN2011_port,
			   struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (ATEN2011_port == NULL)
		return -1;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	tmp.type = PORT_16550A;
	tmp.line = ATEN2011_port->port->serial->minor;
	if (tmp.line == SERIAL_TTY_NO_MINOR)
		tmp.line = 0;
	tmp.port = ATEN2011_port->port->number;
	tmp.irq = 0;
	tmp.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size = NUM_URBS * URB_TRANSFER_BUFFER_SIZE;
	tmp.baud_base = 9600;
	tmp.close_delay = 5 * HZ;
	tmp.closing_wait = 30 * HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int ATEN2011_ioctl(struct tty_struct *tty, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ATENINTL_port *ATEN2011_port;
	struct async_icount cnow;
	struct async_icount cprev;
	struct serial_icounter_struct icount;
	int ATENret = 0;
	unsigned int __user *user_arg = (unsigned int __user *)arg;

	ATEN2011_port = usb_get_serial_port_data(port);

	if (ATEN2011_port == NULL)
		return -1;

	dbg("%s - port %d, cmd = 0x%x", __func__, port->number, cmd);

	switch (cmd) {
		/* return number of bytes available */

	case TIOCINQ:
		dbg("%s (%d) TIOCINQ", __func__, port->number);
		return get_number_bytes_avail(tty, ATEN2011_port, user_arg);
		break;

	case TIOCOUTQ:
		dbg("%s (%d) TIOCOUTQ", __func__, port->number);
		return put_user(ATEN2011_chars_in_buffer(tty), user_arg);
		break;

	case TIOCSERGETLSR:
		dbg("%s (%d) TIOCSERGETLSR", __func__, port->number);
		return get_lsr_info(tty, ATEN2011_port, user_arg);
		return 0;

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		dbg("%s (%d) TIOCMSET/TIOCMBIC/TIOCMSET", __func__,
		    port->number);
		ATENret = set_modem_info(ATEN2011_port, cmd, user_arg);
		return ATENret;

	case TIOCMGET:
		dbg("%s (%d) TIOCMGET", __func__, port->number);
		return get_modem_info(ATEN2011_port, user_arg);

	case TIOCGSERIAL:
		dbg("%s (%d) TIOCGSERIAL", __func__, port->number);
		return get_serial_info(ATEN2011_port,
				       (struct serial_struct __user *)arg);

	case TIOCSSERIAL:
		dbg("%s (%d) TIOCSSERIAL", __func__, port->number);
		break;

	case TIOCMIWAIT:
		dbg("%s (%d) TIOCMIWAIT", __func__, port->number);
		cprev = ATEN2011_port->icount;
		while (1) {
			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			cnow = ATEN2011_port->icount;
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO;	/* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD) && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */
		break;

	case TIOCGICOUNT:
		cnow = ATEN2011_port->icount;
		icount.cts = cnow.cts;
		icount.dsr = cnow.dsr;
		icount.rng = cnow.rng;
		icount.dcd = cnow.dcd;
		icount.rx = cnow.rx;
		icount.tx = cnow.tx;
		icount.frame = cnow.frame;
		icount.overrun = cnow.overrun;
		icount.parity = cnow.parity;
		icount.brk = cnow.brk;
		icount.buf_overrun = cnow.buf_overrun;

		dbg("%s (%d) TIOCGICOUNT RX=%d, TX=%d", __func__,
		    port->number, icount.rx, icount.tx);
		if (copy_to_user((void __user *)arg, &icount, sizeof(icount)))
			return -EFAULT;
		return 0;

	default:
		break;
	}

	return -ENOIOCTLCMD;
}

static int ATEN2011_calc_baud_rate_divisor(int baudRate, int *divisor,
					   __u16 *clk_sel_val)
{
	dbg("%s - %d", __func__, baudRate);

	if (baudRate <= 115200) {
		*divisor = 115200 / baudRate;
		*clk_sel_val = 0x0;
	}
	if ((baudRate > 115200) && (baudRate <= 230400)) {
		*divisor = 230400 / baudRate;
		*clk_sel_val = 0x10;
	} else if ((baudRate > 230400) && (baudRate <= 403200)) {
		*divisor = 403200 / baudRate;
		*clk_sel_val = 0x20;
	} else if ((baudRate > 403200) && (baudRate <= 460800)) {
		*divisor = 460800 / baudRate;
		*clk_sel_val = 0x30;
	} else if ((baudRate > 460800) && (baudRate <= 806400)) {
		*divisor = 806400 / baudRate;
		*clk_sel_val = 0x40;
	} else if ((baudRate > 806400) && (baudRate <= 921600)) {
		*divisor = 921600 / baudRate;
		*clk_sel_val = 0x50;
	} else if ((baudRate > 921600) && (baudRate <= 1572864)) {
		*divisor = 1572864 / baudRate;
		*clk_sel_val = 0x60;
	} else if ((baudRate > 1572864) && (baudRate <= 3145728)) {
		*divisor = 3145728 / baudRate;
		*clk_sel_val = 0x70;
	}
	return 0;
}

static int ATEN2011_send_cmd_write_baud_rate(struct ATENINTL_port
					     *ATEN2011_port, int baudRate)
{
	int divisor = 0;
	int status;
	__u16 Data;
	unsigned char number;
	__u16 clk_sel_val;
	struct usb_serial_port *port;
	int minor;

	if (ATEN2011_port == NULL)
		return -1;

	port = (struct usb_serial_port *)ATEN2011_port->port;

	dbg("%s", "Entering .......... ");

	minor = ATEN2011_port->port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;
	number = ATEN2011_port->port->number - minor;

	dbg("%s - port = %d, baud = %d", __func__,
	    ATEN2011_port->port->number, baudRate);
	/* reset clk_uart_sel in spregOffset */
	if (baudRate > 115200) {
#ifdef HW_flow_control
		/*
		 * NOTE: need to see the pther register to modify
		 * setting h/w flow control bit to 1;
		 */
		/* Data = ATEN2011_port->shadowMCR; */
		Data = 0x2b;
		ATEN2011_port->shadowMCR = Data;
		status = set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
		if (status < 0) {
			dbg("Writing spreg failed in set_serial_baud");
			return -1;
		}
#endif

	} else {
#ifdef HW_flow_control
		/* setting h/w flow control bit to 0; */
		/* Data = ATEN2011_port->shadowMCR; */
		Data = 0xb;
		ATEN2011_port->shadowMCR = Data;
		status = set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
		if (status < 0) {
			dbg("Writing spreg failed in set_serial_baud");
			return -1;
		}
#endif

	}

	if (1)			/* baudRate <= 115200) */ {
		clk_sel_val = 0x0;
		Data = 0x0;
		status =
		    ATEN2011_calc_baud_rate_divisor(baudRate, &divisor,
						    &clk_sel_val);
		status = get_reg_sync(port, ATEN2011_port->SpRegOffset, &Data);
		if (status < 0) {
			dbg("reading spreg failed in set_serial_baud");
			return -1;
		}
		Data = (Data & 0x8f) | clk_sel_val;
		status = set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
		if (status < 0) {
			dbg("Writing spreg failed in set_serial_baud");
			return -1;
		}
		/* Calculate the Divisor */

		if (status) {
			err("%s - bad baud rate", __func__);
			dbg("%s", "bad baud rate");
			return status;
		}
		/* Enable access to divisor latch */
		Data = ATEN2011_port->shadowLCR | SERIAL_LCR_DLAB;
		ATEN2011_port->shadowLCR = Data;
		set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

		/* Write the divisor */
		Data = (unsigned char)(divisor & 0xff);
		dbg("set_serial_baud Value to write DLL is %x", Data);
		set_uart_reg(port, DIVISOR_LATCH_LSB, Data);

		Data = (unsigned char)((divisor & 0xff00) >> 8);
		dbg("set_serial_baud Value to write DLM is %x", Data);
		set_uart_reg(port, DIVISOR_LATCH_MSB, Data);

		/* Disable access to divisor latch */
		Data = ATEN2011_port->shadowLCR & ~SERIAL_LCR_DLAB;
		ATEN2011_port->shadowLCR = Data;
		set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

	}

	return status;
}

static void ATEN2011_change_port_settings(struct tty_struct *tty,
					  struct ATENINTL_port *ATEN2011_port,
					  struct ktermios *old_termios)
{
	int baud;
	unsigned cflag;
	unsigned iflag;
	__u8 lData;
	__u8 lParity;
	__u8 lStop;
	int status;
	__u16 Data;
	struct usb_serial_port *port;
	struct usb_serial *serial;

	if (ATEN2011_port == NULL)
		return;

	port = (struct usb_serial_port *)ATEN2011_port->port;

	serial = port->serial;

	dbg("%s - port %d", __func__, ATEN2011_port->port->number);

	if (!ATEN2011_port->open) {
		dbg("%s - port not opened", __func__);
		return;
	}

	if ((!tty) || (!tty->termios)) {
		dbg("%s - no tty structures", __func__);
		return;
	}

	dbg("%s", "Entering .......... ");

	lData = LCR_BITS_8;
	lStop = LCR_STOP_1;
	lParity = LCR_PAR_NONE;

	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;

	/* Change the number of bits */

	/* COMMENT1: the below Line"if(cflag & CSIZE)" is added for the errors we get for serial loop data test i.e serial_loopback.pl -v */
	/* if(cflag & CSIZE) */
	{
		switch (cflag & CSIZE) {
		case CS5:
			lData = LCR_BITS_5;
			break;

		case CS6:
			lData = LCR_BITS_6;
			break;

		case CS7:
			lData = LCR_BITS_7;
			break;
		default:
		case CS8:
			lData = LCR_BITS_8;
			break;
		}
	}
	/* Change the Parity bit */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			lParity = LCR_PAR_ODD;
			dbg("%s - parity = odd", __func__);
		} else {
			lParity = LCR_PAR_EVEN;
			dbg("%s - parity = even", __func__);
		}

	} else {
		dbg("%s - parity = none", __func__);
	}

	if (cflag & CMSPAR)
		lParity = lParity | 0x20;

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = LCR_STOP_2;
		dbg("%s - stop bits = 2", __func__);
	} else {
		lStop = LCR_STOP_1;
		dbg("%s - stop bits = 1", __func__);
	}

	/* Update the LCR with the correct value */
	ATEN2011_port->shadowLCR &=
	    ~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	ATEN2011_port->shadowLCR |= (lData | lParity | lStop);

	dbg
	    ("ATEN2011_change_port_settings ATEN2011_port->shadowLCR is %x",
	     ATEN2011_port->shadowLCR);
	/* Disable Interrupts */
	Data = 0x00;
	set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	Data = 0x00;
	set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);

	Data = 0xcf;
	set_uart_reg(port, FIFO_CONTROL_REGISTER, Data);

	/* Send the updated LCR value to the ATEN2011 */
	Data = ATEN2011_port->shadowLCR;

	set_uart_reg(port, LINE_CONTROL_REGISTER, Data);

	Data = 0x00b;
	ATEN2011_port->shadowMCR = Data;
	set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);
	Data = 0x00b;
	set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);

	/* set up the MCR register and send it to the ATEN2011 */

	ATEN2011_port->shadowMCR = MCR_MASTER_IE;
	if (cflag & CBAUD)
		ATEN2011_port->shadowMCR |= (MCR_DTR | MCR_RTS);

	if (cflag & CRTSCTS)
		ATEN2011_port->shadowMCR |= (MCR_XON_ANY);
	else
		ATEN2011_port->shadowMCR &= ~(MCR_XON_ANY);

	Data = ATEN2011_port->shadowMCR;
	set_uart_reg(port, MODEM_CONTROL_REGISTER, Data);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);

	if (!baud) {
		/* pick a default, any default... */
		dbg("%s", "Picked default baud...");
		baud = 9600;
	}

	dbg("%s - baud rate = %d", __func__, baud);
	status = ATEN2011_send_cmd_write_baud_rate(ATEN2011_port, baud);

	/* Enable Interrupts */
	Data = 0x0c;
	set_uart_reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	if (ATEN2011_port->read_urb->status != -EINPROGRESS) {
		ATEN2011_port->read_urb->dev = serial->dev;

		status = usb_submit_urb(ATEN2011_port->read_urb, GFP_ATOMIC);

		if (status) {
			dbg
			    (" usb_submit_urb(read bulk) failed, status = %d",
			     status);
		}
	}
	dbg
	    ("ATEN2011_change_port_settings ATEN2011_port->shadowLCR is End %x",
	     ATEN2011_port->shadowLCR);

	return;
}

static int ATEN2011_calc_num_ports(struct usb_serial *serial)
{

	__u16 Data = 0x00;
	int ret = 0;
	int ATEN2011_2or4ports;
	ret = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			      ATEN_RDREQ, ATEN_RD_RTYPE, 0, GPIO_REGISTER,
			      &Data, VENDOR_READ_LENGTH, ATEN_WDR_TIMEOUT);

/* ghostgum: here is where the problem appears to bet */
/* Which of the following are needed? */
/* Greg used the serial->type->num_ports=2 */
/* But the code in the ATEN2011_open relies on serial->num_ports=2 */
	if ((Data & 0x01) == 0) {
		ATEN2011_2or4ports = 2;
		serial->type->num_ports = 2;
		serial->num_ports = 2;
	}
	/* else if(serial->interface->cur_altsetting->desc.bNumEndpoints == 9) */
	else {
		ATEN2011_2or4ports = 4;
		serial->type->num_ports = 4;
		serial->num_ports = 4;

	}

	return ATEN2011_2or4ports;
}

static int ATEN2011_startup(struct usb_serial *serial)
{
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	struct usb_device *dev;
	int i, status;
	int minor;

	__u16 Data;
	dbg("%s", " ATEN2011_startup :entering..........");

	if (!serial) {
		dbg("%s", "Invalid Handler");
		return -1;
	}

	dev = serial->dev;

	dbg("%s", "Entering...");

	/* create our private serial structure */
	ATEN2011_serial = kzalloc(sizeof(struct ATENINTL_serial), GFP_KERNEL);
	if (ATEN2011_serial == NULL) {
		err("%s - Out of memory", __func__);
		return -ENOMEM;
	}

	/* resetting the private structure field values to zero */
	memset(ATEN2011_serial, 0, sizeof(struct ATENINTL_serial));

	ATEN2011_serial->serial = serial;
	/* initilize status polling flag to 0 */
	ATEN2011_serial->status_polling_started = 0;

	usb_set_serial_data(serial, ATEN2011_serial);
	ATEN2011_serial->ATEN2011_spectrum_2or4ports =
	    ATEN2011_calc_num_ports(serial);
	/* we set up the pointers to the endpoints in the ATEN2011_open *
	 * function, as the structures aren't created yet.             */

	/* set up port private structures */
	for (i = 0; i < serial->num_ports; ++i) {
		ATEN2011_port =
		    kmalloc(sizeof(struct ATENINTL_port), GFP_KERNEL);
		if (ATEN2011_port == NULL) {
			err("%s - Out of memory", __func__);
			usb_set_serial_data(serial, NULL);
			kfree(ATEN2011_serial);
			return -ENOMEM;
		}
		memset(ATEN2011_port, 0, sizeof(struct ATENINTL_port));

		/*
		 * Initialize all port interrupt end point to port 0
		 * int endpoint. Our device has only one interrupt end point
		 * comman to all port
		 */
		/* serial->port[i]->interrupt_in_endpointAddress = serial->port[0]->interrupt_in_endpointAddress; */

		ATEN2011_port->port = serial->port[i];
		usb_set_serial_port_data(serial->port[i], ATEN2011_port);

		minor = serial->port[i]->serial->minor;
		if (minor == SERIAL_TTY_NO_MINOR)
			minor = 0;
		ATEN2011_port->port_num =
		    ((serial->port[i]->number - minor) + 1);

		if (ATEN2011_port->port_num == 1) {
			ATEN2011_port->SpRegOffset = 0x0;
			ATEN2011_port->ControlRegOffset = 0x1;
			ATEN2011_port->DcrRegOffset = 0x4;
		} else if ((ATEN2011_port->port_num == 2)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       4)) {
			ATEN2011_port->SpRegOffset = 0x8;
			ATEN2011_port->ControlRegOffset = 0x9;
			ATEN2011_port->DcrRegOffset = 0x16;
		} else if ((ATEN2011_port->port_num == 2)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       2)) {
			ATEN2011_port->SpRegOffset = 0xa;
			ATEN2011_port->ControlRegOffset = 0xb;
			ATEN2011_port->DcrRegOffset = 0x19;
		} else if ((ATEN2011_port->port_num == 3)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       4)) {
			ATEN2011_port->SpRegOffset = 0xa;
			ATEN2011_port->ControlRegOffset = 0xb;
			ATEN2011_port->DcrRegOffset = 0x19;
		} else if ((ATEN2011_port->port_num == 4)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       4)) {
			ATEN2011_port->SpRegOffset = 0xc;
			ATEN2011_port->ControlRegOffset = 0xd;
			ATEN2011_port->DcrRegOffset = 0x1c;
		}

		usb_set_serial_port_data(serial->port[i], ATEN2011_port);

		/* enable rx_disable bit in control register */

		status = get_reg_sync(serial->port[i],
				      ATEN2011_port->ControlRegOffset, &Data);
		if (status < 0) {
			dbg("Reading ControlReg failed status-0x%x",
				status);
			break;
		} else
			dbg
			    ("ControlReg Reading success val is %x, status%d",
			     Data, status);
		Data |= 0x08;	/* setting driver done bit */
		Data |= 0x04;	/* sp1_bit to have cts change reflect in modem status reg */

		/* Data |= 0x20; */	/* rx_disable bit */
		status = set_reg_sync(serial->port[i],
				      ATEN2011_port->ControlRegOffset, Data);
		if (status < 0) {
			dbg
			    ("Writing ControlReg failed(rx_disable) status-0x%x",
			     status);
			break;
		} else
			dbg
			    ("ControlReg Writing success(rx_disable) status%d",
			     status);

		/*
		 * Write default values in DCR (i.e 0x01 in DCR0, 0x05 in DCR2
		 * and 0x24 in DCR3
		 */
		Data = 0x01;
		status = set_reg_sync(serial->port[i],
				      (__u16)(ATEN2011_port->DcrRegOffset + 0),
				      Data);
		if (status < 0) {
			dbg("Writing DCR0 failed status-0x%x", status);
			break;
		} else
			dbg("DCR0 Writing success status%d", status);

		Data = 0x05;
		status = set_reg_sync(serial->port[i],
				      (__u16)(ATEN2011_port->DcrRegOffset + 1),
				      Data);
		if (status < 0) {
			dbg("Writing DCR1 failed status-0x%x", status);
			break;
		} else
			dbg("DCR1 Writing success status%d", status);

		Data = 0x24;
		status = set_reg_sync(serial->port[i],
				      (__u16)(ATEN2011_port->DcrRegOffset + 2),
				      Data);
		if (status < 0) {
			dbg("Writing DCR2 failed status-0x%x", status);
			break;
		} else
			dbg("DCR2 Writing success status%d", status);

		/* write values in clkstart0x0 and clkmulti 0x20 */
		Data = 0x0;
		status = set_reg_sync(serial->port[i], CLK_START_VALUE_REGISTER,
				      Data);
		if (status < 0) {
			dbg
			    ("Writing CLK_START_VALUE_REGISTER failed status-0x%x",
			     status);
			break;
		} else
			dbg
			    ("CLK_START_VALUE_REGISTER Writing success status%d",
			     status);

		Data = 0x20;
		status = set_reg_sync(serial->port[i], CLK_MULTI_REGISTER,
				      Data);
		if (status < 0) {
			dbg
			    ("Writing CLK_MULTI_REGISTER failed status-0x%x",
			     status);
			break;
		} else
			dbg("CLK_MULTI_REGISTER Writing success status%d",
				status);

		/* Zero Length flag register */
		if ((ATEN2011_port->port_num != 1)
		    && (ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)) {

			Data = 0xff;
			status = set_reg_sync(serial->port[i],
					      (__u16)(ZLP_REG1 + ((__u16)ATEN2011_port->port_num)),
					      Data);
			dbg("ZLIP offset%x",
				(__u16) (ZLP_REG1 +
					 ((__u16) ATEN2011_port->port_num)));
			if (status < 0) {
				dbg
				    ("Writing ZLP_REG%d failed status-0x%x",
				     i + 2, status);
				break;
			} else
				dbg("ZLP_REG%d Writing success status%d",
					i + 2, status);
		} else {
			Data = 0xff;
			status = set_reg_sync(serial->port[i],
					      (__u16)(ZLP_REG1 + ((__u16)ATEN2011_port->port_num) - 0x1),
					      Data);
			dbg("ZLIP offset%x",
				(__u16) (ZLP_REG1 +
					 ((__u16) ATEN2011_port->port_num) -
					 0x1));
			if (status < 0) {
				dbg
				    ("Writing ZLP_REG%d failed status-0x%x",
				     i + 1, status);
				break;
			} else
				dbg("ZLP_REG%d Writing success status%d",
					i + 1, status);

		}
		ATEN2011_port->control_urb = usb_alloc_urb(0, GFP_ATOMIC);
		ATEN2011_port->ctrl_buf = kmalloc(16, GFP_KERNEL);

	}

	/* Zero Length flag enable */
	Data = 0x0f;
	status = set_reg_sync(serial->port[0], ZLP_REG5, Data);
	if (status < 0) {
		dbg("Writing ZLP_REG5 failed status-0x%x", status);
		return -1;
	} else
		dbg("ZLP_REG5 Writing success status%d", status);

	/* setting configuration feature to one */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			(__u8) 0x03, 0x00, 0x01, 0x00, NULL, 0x00, 5 * HZ);
	return 0;
}

static void ATEN2011_shutdown(struct usb_serial *serial)
{
	int i;
	struct ATENINTL_port *ATEN2011_port;

	/* check for the ports to be closed,close the ports and disconnect */

	/* free private structure allocated for serial port  *
	 * stop reads and writes on all ports                */

	for (i = 0; i < serial->num_ports; ++i) {
		ATEN2011_port = usb_get_serial_port_data(serial->port[i]);
		kfree(ATEN2011_port->ctrl_buf);
		usb_kill_urb(ATEN2011_port->control_urb);
		kfree(ATEN2011_port);
		usb_set_serial_port_data(serial->port[i], NULL);
	}

	/* free private structure allocated for serial device */

	kfree(usb_get_serial_data(serial));
	usb_set_serial_data(serial, NULL);
}

static struct usb_serial_driver aten_serial_driver = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"aten2011",
		},
	.description =		DRIVER_DESC,
	.id_table =		id_table,
	.open =			ATEN2011_open,
	.close =		ATEN2011_close,
	.write =		ATEN2011_write,
	.write_room =		ATEN2011_write_room,
	.chars_in_buffer =	ATEN2011_chars_in_buffer,
	.throttle =		ATEN2011_throttle,
	.unthrottle =		ATEN2011_unthrottle,
	.calc_num_ports =	ATEN2011_calc_num_ports,

	.ioctl =		ATEN2011_ioctl,
	.set_termios =		ATEN2011_set_termios,
	.break_ctl =		ATEN2011_break,
	.tiocmget =		ATEN2011_tiocmget,
	.tiocmset =		ATEN2011_tiocmset,
	.attach =		ATEN2011_startup,
	.shutdown =		ATEN2011_shutdown,
	.read_bulk_callback =	ATEN2011_bulk_in_callback,
	.read_int_callback =	ATEN2011_interrupt_callback,
};

static struct usb_driver aten_driver = {
	.name =		"aten2011",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
};

static int __init aten_init(void)
{
	int retval;

	/* Register with the usb serial */
	retval = usb_serial_register(&aten_serial_driver);
	if (retval)
		return retval;

	printk(KERN_INFO KBUILD_MODNAME ":"
	       DRIVER_DESC " " DRIVER_VERSION "\n");

	/* Register with the usb */
	retval = usb_register(&aten_driver);
	if (retval)
		usb_serial_deregister(&aten_serial_driver);

	return retval;
}

static void __exit aten_exit(void)
{
	usb_deregister(&aten_driver);
	usb_serial_deregister(&aten_serial_driver);
}

module_init(aten_init);
module_exit(aten_exit);

/* Module information */
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug, "Debug enabled or not");
