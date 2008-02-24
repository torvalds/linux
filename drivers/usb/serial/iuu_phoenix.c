/*
 * Infinity Unlimited USB Phoenix driver
 *
 * Copyright (C) 2007 Alain Degreffe (eczema@ecze.com)
 *
 * Original code taken from iuutool (Copyright (C) 2006 Juan Carlos BorrÃ¡s)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *  And tested with help of WB Electronics
 *
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
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
#include "iuu_phoenix.h"
#include <linux/random.h>


#ifdef CONFIG_USB_SERIAL_DEBUG
static int debug = 1;
#else
static int debug;
#endif

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.5"
#define DRIVER_DESC "Infinity USB Unlimited Phoenix driver"

static struct usb_device_id id_table[] = {
	{USB_DEVICE(IUU_USB_VENDOR_ID, IUU_USB_PRODUCT_ID)},
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver iuu_driver = {
	.name = "iuu_phoenix",
	.probe = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
	.id_table = id_table,
	.no_dynamic_id = 1,
};

/* turbo parameter */
static int boost = 100;
static int clockmode = 1;
static int cdmode = 1;
static int iuu_cardin;
static int iuu_cardout;
static int xmas;

static void read_rxcmd_callback(struct urb *urb);

struct iuu_private {
	spinlock_t lock;	/* store irq state */
	wait_queue_head_t delta_msr_wait;
	u8 line_control;
	u8 line_status;
	u8 termios_initialized;
	int tiostatus;		/* store IUART SIGNAL for tiocmget call */
	u8 reset;		/* if 1 reset is needed */
	int poll;		/* number of poll */
	u8 *writebuf;		/* buffer for writing to device */
	int writelen;		/* num of byte to write to device */
	u8 *buf;		/* used for initialize speed */
	u8 *dbgbuf;		/* debug buffer */
	u8 len;
};


static void iuu_free_buf(struct iuu_private *priv)
{
	kfree(priv->buf);
	kfree(priv->dbgbuf);
	kfree(priv->writebuf);
}

static int iuu_alloc_buf(struct iuu_private *priv)
{
	priv->buf = kzalloc(256, GFP_KERNEL);
	priv->dbgbuf = kzalloc(256, GFP_KERNEL);
	priv->writebuf = kzalloc(256, GFP_KERNEL);
	if (!priv->buf || !priv->dbgbuf || !priv->writebuf) {
		iuu_free_buf(priv);
		dbg("%s problem allocation buffer", __func__);
		return -ENOMEM;
	}
	dbg("%s - Privates buffers allocation success", __func__);
	return 0;
}

static int iuu_startup(struct usb_serial *serial)
{
	struct iuu_private *priv;
	priv = kzalloc(sizeof(struct iuu_private), GFP_KERNEL);
	dbg("%s- priv allocation success", __func__);
	if (!priv)
		return -ENOMEM;
	if (iuu_alloc_buf(priv)) {
		kfree(priv);
		return -ENOMEM;
	}
	spin_lock_init(&priv->lock);
	init_waitqueue_head(&priv->delta_msr_wait);
	usb_set_serial_port_data(serial->port[0], priv);
	return 0;
}

/* Shutdown function */
static void iuu_shutdown(struct usb_serial *serial)
{
	struct usb_serial_port *port = serial->port[0];
	struct iuu_private *priv = usb_get_serial_port_data(port);
	if (!port)
		return;

	dbg("%s", __func__);

	if (priv) {
		iuu_free_buf(priv);
		dbg("%s - I will free all", __func__);
		usb_set_serial_port_data(port, NULL);

		dbg("%s - priv is not anymore in port structure", __func__);
		kfree(priv);

		dbg("%s priv is now kfree", __func__);
	}
}

static int iuu_tiocmset(struct usb_serial_port *port, struct file *file,
			unsigned int set, unsigned int clear)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	/* FIXME: locking on tiomstatus */
	dbg("%s (%d) msg : SET = 0x%04x, CLEAR = 0x%04x ", __func__,
	    port->number, set, clear);

	spin_lock_irqsave(&priv->lock, flags);
	if (set & TIOCM_RTS)
		priv->tiostatus = TIOCM_RTS;

	if (!(set & TIOCM_RTS) && priv->tiostatus == TIOCM_RTS) {
		dbg("%s TIOCMSET RESET called !!!", __func__);
		priv->reset = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

/* This is used to provide a carrier detect mechanism
 * When a card is present, the response is 0x00
 * When no card , the reader respond with TIOCM_CD
 * This is known as CD autodetect mechanism
 */
static int iuu_tiocmget(struct usb_serial_port *port, struct file *file)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&priv->lock, flags);
	rc = priv->tiostatus;
	spin_unlock_irqrestore(&priv->lock, flags);

	return rc;
}

static void iuu_rxcmd(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;
	dbg("%s - enter", __func__);

	if (urb->status) {
		dbg("%s - urb->status = %d", __func__, urb->status);
		/* error stop all */
		return;
	}


	memset(port->write_urb->transfer_buffer, IUU_UART_RX, 1);
	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, 1,
			  read_rxcmd_callback, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
}

static int iuu_reset(struct usb_serial_port *port, u8 wt)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	int result;
	char *buf_ptr = port->write_urb->transfer_buffer;
	dbg("%s - enter", __func__);

	/* Prepare the reset sequence */

	*buf_ptr++ = IUU_RST_SET;
	*buf_ptr++ = IUU_DELAY_MS;
	*buf_ptr++ = wt;
	*buf_ptr = IUU_RST_CLEAR;

	/* send the sequence */

	usb_fill_bulk_urb(port->write_urb,
			  port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, 4, iuu_rxcmd, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	priv->reset = 0;
	return result;
}

/* Status Function
 * Return value is
 * 0x00 = no card
 * 0x01 = smartcard
 * 0x02 = sim card
 */
static void iuu_update_status_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct iuu_private *priv = usb_get_serial_port_data(port);
	u8 *st;
	dbg("%s - enter", __func__);

	if (urb->status) {
		dbg("%s - urb->status = %d", __func__, urb->status);
		/* error stop all */
		return;
	}

	st = urb->transfer_buffer;
	dbg("%s - enter", __func__);
	if (urb->actual_length == 1) {
		switch (st[0]) {
		case 0x1:
			priv->tiostatus = iuu_cardout;
			break;
		case 0x0:
			priv->tiostatus = iuu_cardin;
			break;
		default:
			priv->tiostatus = iuu_cardin;
		}
	}
	iuu_rxcmd(urb);
}

static void iuu_status_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;
	dbg("%s - enter", __func__);

	dbg("%s - urb->status = %d", __func__, urb->status);
	usb_fill_bulk_urb(port->read_urb, port->serial->dev,
			  usb_rcvbulkpipe(port->serial->dev,
					  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer, 256,
			  iuu_update_status_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
}

static int iuu_status(struct usb_serial_port *port)
{
	int result;

	dbg("%s - enter", __func__);

	memset(port->write_urb->transfer_buffer, IUU_GET_STATE_REGISTER, 1);
	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, 1,
			  iuu_status_callback, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	return result;

}

static int bulk_immediate(struct usb_serial_port *port, u8 *buf, u8 count)
{
	int status;
	struct usb_serial *serial = port->serial;
	int actual = 0;

	dbg("%s - enter", __func__);

	/* send the data out the bulk port */

	status =
	    usb_bulk_msg(serial->dev,
			 usb_sndbulkpipe(serial->dev,
					 port->bulk_out_endpointAddress), buf,
			 count, &actual, HZ * 1);

	if (status != IUU_OPERATION_OK) {
		dbg("%s - error = %2x", __func__, status);
	} else {
		dbg("%s - write OK !", __func__);
	}
	return status;
}

static int read_immediate(struct usb_serial_port *port, u8 *buf, u8 count)
{
	int status;
	struct usb_serial *serial = port->serial;
	int actual = 0;

	dbg("%s - enter", __func__);

	/* send the data out the bulk port */

	status =
	    usb_bulk_msg(serial->dev,
			 usb_rcvbulkpipe(serial->dev,
					 port->bulk_in_endpointAddress), buf,
			 count, &actual, HZ * 1);

	if (status != IUU_OPERATION_OK) {
		dbg("%s - error = %2x", __func__, status);
	} else {
		dbg("%s - read OK !", __func__);
	}

	return status;
}

static int iuu_led(struct usb_serial_port *port, unsigned int R,
		   unsigned int G, unsigned int B, u8 f)
{
	int status;
	u8 *buf;
	buf = kmalloc(8, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dbg("%s - enter", __func__);

	buf[0] = IUU_SET_LED;
	buf[1] = R & 0xFF;
	buf[2] = (R >> 8) & 0xFF;
	buf[3] = G & 0xFF;
	buf[4] = (G >> 8) & 0xFF;
	buf[5] = B & 0xFF;
	buf[6] = (B >> 8) & 0xFF;
	buf[7] = f;
	status = bulk_immediate(port, buf, 8);
	kfree(buf);
	if (status != IUU_OPERATION_OK)
		dbg("%s - led error status = %2x", __func__, status);
	else
		dbg("%s - led OK !", __func__);
	return IUU_OPERATION_OK;
}

static void iuu_rgbf_fill_buffer(u8 *buf, u8 r1, u8 r2, u8 g1, u8 g2, u8 b1,
				 u8 b2, u8 freq)
{
	*buf++ = IUU_SET_LED;
	*buf++ = r1;
	*buf++ = r2;
	*buf++ = g1;
	*buf++ = g2;
	*buf++ = b1;
	*buf++ = b2;
	*buf = freq;
}

static void iuu_led_activity_on(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;
	char *buf_ptr = port->write_urb->transfer_buffer;
	*buf_ptr++ = IUU_SET_LED;
	if (xmas == 1) {
		get_random_bytes(buf_ptr, 6);
		*(buf_ptr+7) = 1;
	} else {
		iuu_rgbf_fill_buffer(buf_ptr, 255, 255, 0, 0, 0, 0, 255);
	}

	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, 8 ,
			  iuu_rxcmd, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
}

static void iuu_led_activity_off(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;
	char *buf_ptr = port->write_urb->transfer_buffer;
	if (xmas == 1) {
		iuu_rxcmd(urb);
		return;
	} else {
		*buf_ptr++ = IUU_SET_LED;
		iuu_rgbf_fill_buffer(buf_ptr, 0, 0, 255, 255, 0, 0, 255);
	}
	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, 8 ,
			  iuu_rxcmd, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
}



static int iuu_clk(struct usb_serial_port *port, int dwFrq)
{
	int status;
	struct iuu_private *priv = usb_get_serial_port_data(port);
	int Count = 0;
	u8 FrqGenAdr = 0x69;
	u8 DIV = 0;		/* 8bit */
	u8 XDRV = 0;		/* 8bit */
	u8 PUMP = 0;		/* 3bit */
	u8 PBmsb = 0;		/* 2bit */
	u8 PBlsb = 0;		/* 8bit */
	u8 PO = 0;		/* 1bit */
	u8 Q = 0;		/* 7bit */
	/* 24bit = 3bytes */
	unsigned int P = 0;
	unsigned int P2 = 0;
	int frq = (int)dwFrq;

	dbg("%s - enter", __func__);

	if (frq == 0) {
		priv->buf[Count++] = IUU_UART_WRITE_I2C;
		priv->buf[Count++] = FrqGenAdr << 1;
		priv->buf[Count++] = 0x09;
		priv->buf[Count++] = 0x00;

		status = bulk_immediate(port, (u8 *) priv->buf, Count);
		if (status != 0) {
			dbg("%s - write error ", __func__);
			return status;
		}
	} else if (frq == 3579000) {
		DIV = 100;
		P = 1193;
		Q = 40;
		XDRV = 0;
	} else if (frq == 3680000) {
		DIV = 105;
		P = 161;
		Q = 5;
		XDRV = 0;
	} else if (frq == 6000000) {
		DIV = 66;
		P = 66;
		Q = 2;
		XDRV = 0x28;
	} else {
		unsigned int result = 0;
		unsigned int tmp = 0;
		unsigned int check;
		unsigned int check2;
		char found = 0x00;
		unsigned int lQ = 2;
		unsigned int lP = 2055;
		unsigned int lDiv = 4;

		for (lQ = 2; lQ <= 47 && !found; lQ++)
			for (lP = 2055; lP >= 8 && !found; lP--)
				for (lDiv = 4; lDiv <= 127 && !found; lDiv++) {
					tmp = (12000000 / lDiv) * (lP / lQ);
					if (abs((int)(tmp - frq)) <
					    abs((int)(frq - result))) {
						check2 = (12000000 / lQ);
						if (check2 < 250000)
							continue;
						check = (12000000 / lQ) * lP;
						if (check > 400000000)
							continue;
						if (check < 100000000)
							continue;
						if (lDiv < 4 || lDiv > 127)
							continue;
						result = tmp;
						P = lP;
						DIV = lDiv;
						Q = lQ;
						if (result == frq)
							found = 0x01;
					}
				}
	}
	P2 = ((P - PO) / 2) - 4;
	DIV = DIV;
	PUMP = 0x04;
	PBmsb = (P2 >> 8 & 0x03);
	PBlsb = P2 & 0xFF;
	PO = (P >> 10) & 0x01;
	Q = Q - 2;

	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/* 0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x09;
	priv->buf[Count++] = 0x20;	/* Adr = 0x09 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/* 0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x0C;
	priv->buf[Count++] = DIV;	/* Adr = 0x0C */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/* 0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x12;
	priv->buf[Count++] = XDRV;	/* Adr = 0x12 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x13;
	priv->buf[Count++] = 0x6B;	/* Adr = 0x13 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x40;
	priv->buf[Count++] = (0xC0 | ((PUMP & 0x07) << 2)) |
			     (PBmsb & 0x03);	/* Adr = 0x40 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x41;
	priv->buf[Count++] = PBlsb;	/* Adr = 0x41 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x42;
	priv->buf[Count++] = Q | (((PO & 0x01) << 7));	/* Adr = 0x42 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x44;
	priv->buf[Count++] = (char)0xFF;	/* Adr = 0x44 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x45;
	priv->buf[Count++] = (char)0xFE;	/* Adr = 0x45 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x46;
	priv->buf[Count++] = 0x7F;	/* Adr = 0x46 */
	priv->buf[Count++] = IUU_UART_WRITE_I2C;	/*  0x4C */
	priv->buf[Count++] = FrqGenAdr << 1;
	priv->buf[Count++] = 0x47;
	priv->buf[Count++] = (char)0x84;	/* Adr = 0x47 */

	status = bulk_immediate(port, (u8 *) priv->buf, Count);
	if (status != IUU_OPERATION_OK)
		dbg("%s - write error ", __func__);
	return status;
}

static int iuu_uart_flush(struct usb_serial_port *port)
{
	int i;
	int status;
	u8 rxcmd = IUU_UART_RX;
	struct iuu_private *priv = usb_get_serial_port_data(port);

	dbg("%s - enter", __func__);

	if (iuu_led(port, 0xF000, 0, 0, 0xFF) < 0)
		return -EIO;

	for (i = 0; i < 2; i++) {
		status = bulk_immediate(port, &rxcmd, 1);
		if (status != IUU_OPERATION_OK) {
			dbg("%s - uart_flush_write error", __func__);
			return status;
		}

		status = read_immediate(port, &priv->len, 1);
		if (status != IUU_OPERATION_OK) {
			dbg("%s - uart_flush_read error", __func__);
			return status;
		}

		if (priv->len > 0) {
			dbg("%s - uart_flush datalen is : %i ", __func__,
			    priv->len);
			status = read_immediate(port, priv->buf, priv->len);
			if (status != IUU_OPERATION_OK) {
				dbg("%s - uart_flush_read error", __func__);
				return status;
			}
		}
	}
	dbg("%s - uart_flush_read OK!", __func__);
	iuu_led(port, 0, 0xF000, 0, 0xFF);
	return status;
}

static void read_buf_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	unsigned char *data = urb->transfer_buffer;
	struct tty_struct *tty;
	dbg("%s - urb->status = %d", __func__, urb->status);

	if (urb->status) {
		dbg("%s - urb->status = %d", __func__, urb->status);
		if (urb->status == -EPROTO) {
			/* reschedule needed */
		}
		return;
	}

	dbg("%s - %i chars to write", __func__, urb->actual_length);
	tty = port->tty;
	if (data == NULL)
		dbg("%s - data is NULL !!!", __func__);
	if (tty && urb->actual_length && data) {
		tty_insert_flip_string(tty, data, urb->actual_length);
		tty_flip_buffer_push(tty);
	}
	iuu_led_activity_on(urb);
}

static int iuu_bulk_write(struct usb_serial_port *port)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned int flags;
	int result;
	int i;
	char *buf_ptr = port->write_urb->transfer_buffer;
	dbg("%s - enter", __func__);

	*buf_ptr++ = IUU_UART_ESC;
	*buf_ptr++ = IUU_UART_TX;
	*buf_ptr++ = priv->writelen;

	memcpy(buf_ptr, priv->writebuf,
	       priv->writelen);
	if (debug == 1) {
		for (i = 0; i < priv->writelen; i++)
			sprintf(priv->dbgbuf + i*2 ,
				"%02X", priv->writebuf[i]);
		priv->dbgbuf[priv->writelen+i*2] = 0;
		dbg("%s - writing %i chars : %s", __func__,
		    priv->writelen, priv->dbgbuf);
	}
	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, priv->writelen + 3,
			  iuu_rxcmd, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	spin_lock_irqsave(&priv->lock, flags);
	priv->writelen = 0;
	spin_unlock_irqrestore(&priv->lock, flags);
	usb_serial_port_softint(port);
	return result;
}

static int iuu_read_buf(struct usb_serial_port *port, int len)
{
	int result;
	dbg("%s - enter", __func__);

	usb_fill_bulk_urb(port->read_urb, port->serial->dev,
			  usb_rcvbulkpipe(port->serial->dev,
					  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer, len,
			  read_buf_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	return result;
}

static void iuu_uart_read_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned int flags;
	int status;
	int error = 0;
	int len = 0;
	unsigned char *data = urb->transfer_buffer;
	priv->poll++;

	dbg("%s - enter", __func__);

	if (urb->status) {
		dbg("%s - urb->status = %d", __func__, urb->status);
		/* error stop all */
		return;
	}
	if (data == NULL)
		dbg("%s - data is NULL !!!", __func__);

	if (urb->actual_length == 1  && data != NULL)
		len = (int) data[0];

	if (urb->actual_length > 1) {
		dbg("%s - urb->actual_length = %i", __func__,
		    urb->actual_length);
		error = 1;
		return;
	}
	/* if len > 0 call readbuf */

	if (len > 0 && error == 0) {
		dbg("%s - call read buf - len to read is %i ",
			__func__, len);
		status = iuu_read_buf(port, len);
		return;
	}
	/* need to update status  ? */
	if (priv->poll > 99) {
		status = iuu_status(port);
		priv->poll = 0;
		return;
	}

	/* reset waiting ? */

	if (priv->reset == 1) {
		status = iuu_reset(port, 0xC);
		return;
	}
	/* Writebuf is waiting */
	spin_lock_irqsave(&priv->lock, flags);
	if (priv->writelen > 0) {
		spin_unlock_irqrestore(&priv->lock, flags);
		status = iuu_bulk_write(port);
		return;
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	/* if nothing to write call again rxcmd */
	dbg("%s - rxcmd recall", __func__);
	iuu_led_activity_off(urb);
	return;
}

static int iuu_uart_write(struct usb_serial_port *port, const u8 *buf,
			  int count)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned int flags;
	dbg("%s - enter", __func__);

	if (count > 256)
		return -ENOMEM;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->writelen > 0) {
		/* buffer already filled but not commited */
		spin_unlock_irqrestore(&priv->lock, flags);
		return (0);
	}
	/* fill the buffer */
	memcpy(priv->writebuf, buf, count);
	priv->writelen = count;
	spin_unlock_irqrestore(&priv->lock, flags);

	return (count);
}

static void read_rxcmd_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;
	dbg("%s - enter", __func__);

	dbg("%s - urb->status = %d", __func__, urb->status);

	if (urb->status) {
		dbg("%s - urb->status = %d", __func__, urb->status);
		/* error stop all */
		return;
	}

	usb_fill_bulk_urb(port->read_urb, port->serial->dev,
			  usb_rcvbulkpipe(port->serial->dev,
					  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer, 256,
			  iuu_uart_read_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	dbg("%s - submit result = %d", __func__, result);
	return;
}

static int iuu_uart_on(struct usb_serial_port *port)
{
	int status;
	u8 *buf;

	buf = kmalloc(sizeof(u8) * 4, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	buf[0] = IUU_UART_ENABLE;
	buf[1] = (u8) ((IUU_BAUD_9600 >> 8) & 0x00FF);
	buf[2] = (u8) (0x00FF & IUU_BAUD_9600);
	buf[3] = (u8) (0x0F0 & IUU_TWO_STOP_BITS) | (0x07 & IUU_PARITY_EVEN);

	status = bulk_immediate(port, buf, 4);
	if (status != IUU_OPERATION_OK) {
		dbg("%s - uart_on error", __func__);
		goto uart_enable_failed;
	}
	/*  iuu_reset() the card after iuu_uart_on() */
	status = iuu_uart_flush(port);
	if (status != IUU_OPERATION_OK)
		dbg("%s - uart_flush error", __func__);
uart_enable_failed:
	kfree(buf);
	return status;
}

/*  Diables the IUU UART (a.k.a. the Phoenix voiderface) */
static int iuu_uart_off(struct usb_serial_port *port)
{
	int status;
	u8 *buf;
	buf = kmalloc(1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	buf[0] = IUU_UART_DISABLE;

	status = bulk_immediate(port, buf, 1);
	if (status != IUU_OPERATION_OK)
		dbg("%s - uart_off error", __func__);

	kfree(buf);
	return status;
}

static int iuu_uart_baud(struct usb_serial_port *port, u32 baud,
			 u32 *actual, u8 parity)
{
	int status;
	u8 *dataout;
	u8 DataCount = 0;
	u8 T1Frekvens = 0;
	u8 T1reload = 0;
	unsigned int T1FrekvensHZ = 0;

	dataout = kmalloc(sizeof(u8) * 5, GFP_KERNEL);

	if (!dataout)
		return -ENOMEM;

	if (baud < 1200 || baud > 230400) {
		kfree(dataout);
		return IUU_INVALID_PARAMETER;
	}
	if (baud > 977) {
		T1Frekvens = 3;
		T1FrekvensHZ = 500000;
	}

	if (baud > 3906) {
		T1Frekvens = 2;
		T1FrekvensHZ = 2000000;
	}

	if (baud > 11718) {
		T1Frekvens = 1;
		T1FrekvensHZ = 6000000;
	}

	if (baud > 46875) {
		T1Frekvens = 0;
		T1FrekvensHZ = 24000000;
	}

	T1reload = 256 - (u8) (T1FrekvensHZ / (baud * 2));

	/*  magic number here:  ENTER_FIRMWARE_UPDATE; */
	dataout[DataCount++] = IUU_UART_ESC;
	/*  magic number here:  CHANGE_BAUD; */
	dataout[DataCount++] = IUU_UART_CHANGE;
	dataout[DataCount++] = T1Frekvens;
	dataout[DataCount++] = T1reload;

	*actual = (T1FrekvensHZ / (256 - T1reload)) / 2;

	switch (parity & 0x0F) {
	case IUU_PARITY_NONE:
		dataout[DataCount++] = 0x00;
		break;
	case IUU_PARITY_EVEN:
		dataout[DataCount++] = 0x01;
		break;
	case IUU_PARITY_ODD:
		dataout[DataCount++] = 0x02;
		break;
	case IUU_PARITY_MARK:
		dataout[DataCount++] = 0x03;
		break;
	case IUU_PARITY_SPACE:
		dataout[DataCount++] = 0x04;
		break;
	default:
		kfree(dataout);
		return IUU_INVALID_PARAMETER;
		break;
	}

	switch (parity & 0xF0) {
	case IUU_ONE_STOP_BIT:
		dataout[DataCount - 1] |= IUU_ONE_STOP_BIT;
		break;

	case IUU_TWO_STOP_BITS:
		dataout[DataCount - 1] |= IUU_TWO_STOP_BITS;
		break;
	default:
		kfree(dataout);
		return IUU_INVALID_PARAMETER;
		break;
	}

	status = bulk_immediate(port, dataout, DataCount);
	if (status != IUU_OPERATION_OK)
		dbg("%s - uart_off error", __func__);
	kfree(dataout);
	return status;
}

static int set_control_lines(struct usb_device *dev, u8 value)
{
	return 0;
}

static void iuu_close(struct usb_serial_port *port, struct file *filp)
{
	/* iuu_led (port,255,0,0,0); */
	struct usb_serial *serial;
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	unsigned int c_cflag;

	serial = port->serial;
	if (!serial)
		return;

	dbg("%s - port %d", __func__, port->number);

	iuu_uart_off(port);
	if (serial->dev) {
		if (port->tty) {
			c_cflag = port->tty->termios->c_cflag;
			if (c_cflag & HUPCL) {
				/* drop DTR and RTS */
				priv = usb_get_serial_port_data(port);
				spin_lock_irqsave(&priv->lock, flags);
				priv->line_control = 0;
				spin_unlock_irqrestore(&priv->lock, flags);
				set_control_lines(port->serial->dev, 0);
			}
		}
		/* free writebuf */
		/* shutdown our urbs */
		dbg("%s - shutting down urbs", __func__);
		usb_kill_urb(port->write_urb);
		usb_kill_urb(port->read_urb);
		usb_kill_urb(port->interrupt_in_urb);
		msleep(1000);
		/* wait one second to free all buffers */
		iuu_led(port, 0, 0, 0xF000, 0xFF);
		msleep(1000);
		usb_reset_device(port->serial->dev);
	}
}

static int iuu_open(struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial = port->serial;
	u8 *buf;
	int result;
	u32 actual;
	unsigned long flags;
	struct iuu_private *priv = usb_get_serial_port_data(port);

	dbg("%s -  port %d", __func__, port->number);
	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	buf = kmalloc(10, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* fixup the endpoint buffer size */
	kfree(port->bulk_out_buffer);
	port->bulk_out_buffer = kmalloc(512, GFP_KERNEL);
	port->bulk_out_size = 512;
	kfree(port->bulk_in_buffer);
	port->bulk_in_buffer = kmalloc(512, GFP_KERNEL);
	port->bulk_in_size = 512;

	if (!port->bulk_out_buffer || !port->bulk_in_buffer) {
		kfree(port->bulk_out_buffer);
		kfree(port->bulk_in_buffer);
		kfree(buf);
		return -ENOMEM;
	}

	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->bulk_out_buffer, 512,
			  NULL, NULL);


	usb_fill_bulk_urb(port->read_urb, port->serial->dev,
			  usb_rcvbulkpipe(port->serial->dev,
					  port->bulk_in_endpointAddress),
			  port->bulk_in_buffer, 512,
			  NULL, NULL);

	/* set the termios structure */
	spin_lock_irqsave(&priv->lock, flags);
	if (!priv->termios_initialized) {
		*(port->tty->termios) = tty_std_termios;
		port->tty->termios->c_cflag = CLOCAL | CREAD | CS8 | B9600
						| TIOCM_CTS | CSTOPB | PARENB;
		port->tty->termios->c_lflag = 0;
		port->tty->termios->c_oflag = 0;
		port->tty->termios->c_iflag = 0;
		priv->termios_initialized = 1;
		port->tty->low_latency = 1;
		priv->poll = 0;
	 }
	spin_unlock_irqrestore(&priv->lock, flags);

	/* initialize writebuf */
#define FISH(a, b, c, d) do { \
	result = usb_control_msg(port->serial->dev,	\
				usb_rcvctrlpipe(port->serial->dev, 0),	\
				b, a, c, d, buf, 1, 1000); \
	dbg("0x%x:0x%x:0x%x:0x%x  %d - %x", a, b, c, d, result, \
				buf[0]); } while (0);

#define SOUP(a, b, c, d)  do { \
	result = usb_control_msg(port->serial->dev,	\
				usb_sndctrlpipe(port->serial->dev, 0),	\
				b, a, c, d, NULL, 0, 1000); \
	dbg("0x%x:0x%x:0x%x:0x%x  %d", a, b, c, d, result); } while (0)

	/*  This is not UART related but IUU USB driver related or something */
	/*  like that. Basically no IUU will accept any commands from the USB */
	/*  host unless it has received the following message */
	/* sprintf(buf ,"%c%c%c%c",0x03,0x02,0x02,0x0); */

	SOUP(0x03, 0x02, 0x02, 0x0);
	kfree(buf);
	iuu_led(port, 0xF000, 0xF000, 0, 0xFF);
	iuu_uart_on(port);
	if (boost < 100)
		boost = 100;
	switch (clockmode) {
	case 2:		/*  3.680 Mhz */
		iuu_clk(port, IUU_CLK_3680000 * boost / 100);
		result =
		    iuu_uart_baud(port, 9600 * boost / 100, &actual,
				  IUU_PARITY_EVEN);
		break;
	case 3:		/*  6.00 Mhz */
		iuu_clk(port, IUU_CLK_6000000 * boost / 100);
		result =
		    iuu_uart_baud(port, 16457 * boost / 100, &actual,
				  IUU_PARITY_EVEN);
		break;
	default:		/*  3.579 Mhz */
		iuu_clk(port, IUU_CLK_3579000 * boost / 100);
		result =
		    iuu_uart_baud(port, 9600 * boost / 100, &actual,
				  IUU_PARITY_EVEN);
	}

	/* set the cardin cardout signals */
	switch (cdmode) {
	case 0:
		iuu_cardin = 0;
		iuu_cardout = 0;
		break;
	case 1:
		iuu_cardin = TIOCM_CD;
		iuu_cardout =  0;
		break;
	case 2:
		iuu_cardin = 0;
		iuu_cardout = TIOCM_CD;
		break;
	case 3:
		iuu_cardin = TIOCM_DSR;
		iuu_cardout = 0;
		break;
	case 4:
		iuu_cardin = 0;
		iuu_cardout = TIOCM_DSR;
		break;
	case 5:
		iuu_cardin = TIOCM_CTS;
		iuu_cardout = 0;
		break;
	case 6:
		iuu_cardin = 0;
		iuu_cardout = TIOCM_CTS;
		break;
	case 7:
		iuu_cardin = TIOCM_RNG;
		iuu_cardout = 0;
		break;
	case 8:
		iuu_cardin = 0;
		iuu_cardout = TIOCM_RNG;
	}

	iuu_uart_flush(port);

	dbg("%s - initialization done", __func__);

	memset(port->write_urb->transfer_buffer, IUU_UART_RX, 1);
	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, 1,
			  read_rxcmd_callback, port);
	result = usb_submit_urb(port->write_urb, GFP_KERNEL);

	if (result) {
		dev_err(&port->dev, "%s - failed submitting read urb,"
			" error %d\n", __func__, result);
		iuu_close(port, NULL);
		return -EPROTO;
	} else {
		dbg("%s - rxcmd OK", __func__);
	}
	return result;
}

static struct usb_serial_driver iuu_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "iuu_phoenix",
		   },
	.id_table = id_table,
	.num_ports = 1,
	.open = iuu_open,
	.close = iuu_close,
	.write = iuu_uart_write,
	.read_bulk_callback = iuu_uart_read_callback,
	.tiocmget = iuu_tiocmget,
	.tiocmset = iuu_tiocmset,
	.attach = iuu_startup,
	.shutdown = iuu_shutdown,
};

static int __init iuu_init(void)
{
	int retval;
	retval = usb_serial_register(&iuu_device);
	if (retval)
		goto failed_usb_serial_register;
	retval = usb_register(&iuu_driver);
	if (retval)
		goto failed_usb_register;
	info(DRIVER_DESC " " DRIVER_VERSION);
	return 0;
failed_usb_register:
	usb_serial_deregister(&iuu_device);
failed_usb_serial_register:
	return retval;
}

static void __exit iuu_exit(void)
{
	usb_deregister(&iuu_driver);
	usb_serial_deregister(&iuu_device);
}

module_init(iuu_init);
module_exit(iuu_exit);

MODULE_AUTHOR("Alain Degreffe eczema@ecze.com");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_VERSION(DRIVER_VERSION);
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

module_param(xmas, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(xmas, "xmas color enabled or not");

module_param(boost, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(boost, "overclock boost percent 100 to 500");

module_param(clockmode, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(clockmode, "1=3Mhz579,2=3Mhz680,3=6Mhz");

module_param(cdmode, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(cdmode, "Card detect mode 0=none, 1=CD, 2=!CD, 3=DSR, "
		 "4=!DSR, 5=CTS, 6=!CTS, 7=RING, 8=!RING");
