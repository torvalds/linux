/*
 * Infinity Unlimited USB Phoenix driver
 *
 * Copyright (C) 2010 James Courtier-Dutton (James@superbug.co.uk)

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
static bool debug = 1;
#else
static bool debug;
#endif

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.12"
#define DRIVER_DESC "Infinity USB Unlimited Phoenix driver"

static const struct usb_device_id id_table[] = {
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
static bool xmas;
static int vcc_default = 5;

static void read_rxcmd_callback(struct urb *urb);

struct iuu_private {
	spinlock_t lock;	/* store irq state */
	wait_queue_head_t delta_msr_wait;
	u8 line_status;
	int tiostatus;		/* store IUART SIGNAL for tiocmget call */
	u8 reset;		/* if 1 reset is needed */
	int poll;		/* number of poll */
	u8 *writebuf;		/* buffer for writing to device */
	int writelen;		/* num of byte to write to device */
	u8 *buf;		/* used for initialize speed */
	u8 *dbgbuf;		/* debug buffer */
	u8 len;
	int vcc;		/* vcc (either 3 or 5 V) */
	u32 baud;
	u32 boost;
	u32 clk;
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
	priv->vcc = vcc_default;
	spin_lock_init(&priv->lock);
	init_waitqueue_head(&priv->delta_msr_wait);
	usb_set_serial_port_data(serial->port[0], priv);
	return 0;
}

/* Release function */
static void iuu_release(struct usb_serial *serial)
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

static int iuu_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	/* FIXME: locking on tiomstatus */
	dbg("%s (%d) msg : SET = 0x%04x, CLEAR = 0x%04x ", __func__,
	    port->number, set, clear);

	spin_lock_irqsave(&priv->lock, flags);

	if ((set & TIOCM_RTS) && !(priv->tiostatus == TIOCM_RTS)) {
		dbg("%s TIOCMSET RESET called !!!", __func__);
		priv->reset = 1;
	}
	if (set & TIOCM_RTS)
		priv->tiostatus = TIOCM_RTS;

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;
}

/* This is used to provide a carrier detect mechanism
 * When a card is present, the response is 0x00
 * When no card , the reader respond with TIOCM_CD
 * This is known as CD autodetect mechanism
 */
static int iuu_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
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
	int status = urb->status;

	dbg("%s - enter", __func__);

	if (status) {
		dbg("%s - status = %d", __func__, status);
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
	int status = urb->status;

	dbg("%s - enter", __func__);

	if (status) {
		dbg("%s - status = %d", __func__, status);
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
	int status = urb->status;

	dbg("%s - status = %d", __func__, status);
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

	if (status != IUU_OPERATION_OK)
		dbg("%s - error = %2x", __func__, status);
	else
		dbg("%s - write OK !", __func__);
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

	if (status != IUU_OPERATION_OK)
		dbg("%s - error = %2x", __func__, status);
	else
		dbg("%s - read OK !", __func__);
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
	int status = urb->status;

	dbg("%s - status = %d", __func__, status);

	if (status) {
		if (status == -EPROTO) {
			/* reschedule needed */
		}
		return;
	}

	dbg("%s - %i chars to write", __func__, urb->actual_length);
	tty = tty_port_tty_get(&port->port);
	if (data == NULL)
		dbg("%s - data is NULL !!!", __func__);
	if (tty && urb->actual_length && data) {
		tty_insert_flip_string(tty, data, urb->actual_length);
		tty_flip_buffer_push(tty);
	}
	tty_kref_put(tty);
	iuu_led_activity_on(urb);
}

static int iuu_bulk_write(struct usb_serial_port *port)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	int result;
	int i;
	int buf_len;
	char *buf_ptr = port->write_urb->transfer_buffer;
	dbg("%s - enter", __func__);

	spin_lock_irqsave(&priv->lock, flags);
	*buf_ptr++ = IUU_UART_ESC;
	*buf_ptr++ = IUU_UART_TX;
	*buf_ptr++ = priv->writelen;

	memcpy(buf_ptr, priv->writebuf, priv->writelen);
	buf_len = priv->writelen;
	priv->writelen = 0;
	spin_unlock_irqrestore(&priv->lock, flags);
	if (debug == 1) {
		for (i = 0; i < buf_len; i++)
			sprintf(priv->dbgbuf + i*2 ,
				"%02X", priv->writebuf[i]);
		priv->dbgbuf[buf_len+i*2] = 0;
		dbg("%s - writing %i chars : %s", __func__,
		    buf_len, priv->dbgbuf);
	}
	usb_fill_bulk_urb(port->write_urb, port->serial->dev,
			  usb_sndbulkpipe(port->serial->dev,
					  port->bulk_out_endpointAddress),
			  port->write_urb->transfer_buffer, buf_len + 3,
			  iuu_rxcmd, port);
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
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
	unsigned long flags;
	int status = urb->status;
	int error = 0;
	int len = 0;
	unsigned char *data = urb->transfer_buffer;
	priv->poll++;

	dbg("%s - enter", __func__);

	if (status) {
		dbg("%s - status = %d", __func__, status);
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
}

static int iuu_uart_write(struct tty_struct *tty, struct usb_serial_port *port,
			  const u8 *buf, int count)
{
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;
	dbg("%s - enter", __func__);

	if (count > 256)
		return -ENOMEM;

	spin_lock_irqsave(&priv->lock, flags);

	/* fill the buffer */
	memcpy(priv->writebuf + priv->writelen, buf, count);
	priv->writelen += count;
	spin_unlock_irqrestore(&priv->lock, flags);

	return count;
}

static void read_rxcmd_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int result;
	int status = urb->status;

	dbg("%s - status = %d", __func__, status);

	if (status) {
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
	buf[3] = (u8) (0x0F0 & IUU_ONE_STOP_BIT) | (0x07 & IUU_PARITY_EVEN);

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

static int iuu_uart_baud(struct usb_serial_port *port, u32 baud_base,
			 u32 *actual, u8 parity)
{
	int status;
	u32 baud;
	u8 *dataout;
	u8 DataCount = 0;
	u8 T1Frekvens = 0;
	u8 T1reload = 0;
	unsigned int T1FrekvensHZ = 0;

	dbg("%s - enter baud_base=%d", __func__, baud_base);
	dataout = kmalloc(sizeof(u8) * 5, GFP_KERNEL);

	if (!dataout)
		return -ENOMEM;
	/*baud = (((priv->clk / 35) * baud_base) / 100000); */
	baud = baud_base;

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

static void iuu_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	const u32 supported_mask = CMSPAR|PARENB|PARODD;
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned int cflag = tty->termios->c_cflag;
	int status;
	u32 actual;
	u32 parity;
	int csize = CS7;
	int baud;
	u32 newval = cflag & supported_mask;

	/* Just use the ospeed. ispeed should be the same. */
	baud = tty->termios->c_ospeed;

	dbg("%s - enter c_ospeed or baud=%d", __func__, baud);

	/* compute the parity parameter */
	parity = 0;
	if (cflag & CMSPAR) {	/* Using mark space */
		if (cflag & PARODD)
			parity |= IUU_PARITY_SPACE;
		else
			parity |= IUU_PARITY_MARK;
	} else if (!(cflag & PARENB)) {
		parity |= IUU_PARITY_NONE;
		csize = CS8;
	} else if (cflag & PARODD)
		parity |= IUU_PARITY_ODD;
	else
		parity |= IUU_PARITY_EVEN;

	parity |= (cflag & CSTOPB ? IUU_TWO_STOP_BITS : IUU_ONE_STOP_BIT);

	/* set it */
	status = iuu_uart_baud(port,
			baud * priv->boost / 100,
			&actual, parity);

	/* set the termios value to the real one, so the user now what has
	 * changed. We support few fields so its easies to copy the old hw
	 * settings back over and then adjust them
	 */
	if (old_termios)
		tty_termios_copy_hw(tty->termios, old_termios);
	if (status != 0)	/* Set failed - return old bits */
		return;
	/* Re-encode speed, parity and csize */
	tty_encode_baud_rate(tty, baud, baud);
	tty->termios->c_cflag &= ~(supported_mask|CSIZE);
	tty->termios->c_cflag |= newval | csize;
}

static void iuu_close(struct usb_serial_port *port)
{
	/* iuu_led (port,255,0,0,0); */
	struct usb_serial *serial;

	serial = port->serial;
	if (!serial)
		return;

	dbg("%s - port %d", __func__, port->number);

	iuu_uart_off(port);
	if (serial->dev) {
		/* free writebuf */
		/* shutdown our urbs */
		dbg("%s - shutting down urbs", __func__);
		usb_kill_urb(port->write_urb);
		usb_kill_urb(port->read_urb);
		usb_kill_urb(port->interrupt_in_urb);
		iuu_led(port, 0, 0, 0xF000, 0xFF);
	}
}

static void iuu_init_termios(struct tty_struct *tty)
{
	dbg("%s - enter", __func__);
	*(tty->termios) = tty_std_termios;
	tty->termios->c_cflag = CLOCAL | CREAD | CS8 | B9600
				| TIOCM_CTS | CSTOPB | PARENB;
	tty->termios->c_ispeed = 9600;
	tty->termios->c_ospeed = 9600;
	tty->termios->c_lflag = 0;
	tty->termios->c_oflag = 0;
	tty->termios->c_iflag = 0;
}

static int iuu_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	u8 *buf;
	int result;
	int baud;
	u32 actual;
	struct iuu_private *priv = usb_get_serial_port_data(port);

	baud = tty->termios->c_ospeed;
	tty->termios->c_ispeed = baud;
	/* Re-encode speed */
	tty_encode_baud_rate(tty, baud, baud);

	dbg("%s -  port %d, baud %d", __func__, port->number, baud);
	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	buf = kmalloc(10, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	priv->poll = 0;

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
	priv->boost = boost;
	priv->baud = baud;
	switch (clockmode) {
	case 2:		/*  3.680 Mhz */
		priv->clk = IUU_CLK_3680000;
		iuu_clk(port, IUU_CLK_3680000 * boost / 100);
		result =
		    iuu_uart_baud(port, baud * boost / 100, &actual,
				  IUU_PARITY_EVEN);
		break;
	case 3:		/*  6.00 Mhz */
		iuu_clk(port, IUU_CLK_6000000 * boost / 100);
		priv->clk = IUU_CLK_6000000;
		/* Ratio of 6000000 to 3500000 for baud 9600 */
		result =
		    iuu_uart_baud(port, 16457 * boost / 100, &actual,
				  IUU_PARITY_EVEN);
		break;
	default:		/*  3.579 Mhz */
		iuu_clk(port, IUU_CLK_3579000 * boost / 100);
		priv->clk = IUU_CLK_3579000;
		result =
		    iuu_uart_baud(port, baud * boost / 100, &actual,
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
		iuu_close(port);
	} else {
		dbg("%s - rxcmd OK", __func__);
	}

	return result;
}

/* how to change VCC */
static int iuu_vcc_set(struct usb_serial_port *port, unsigned int vcc)
{
	int status;
	u8 *buf;

	buf = kmalloc(5, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	dbg("%s - enter", __func__);

	buf[0] = IUU_SET_VCC;
	buf[1] = vcc & 0xFF;
	buf[2] = (vcc >> 8) & 0xFF;
	buf[3] = (vcc >> 16) & 0xFF;
	buf[4] = (vcc >> 24) & 0xFF;

	status = bulk_immediate(port, buf, 5);
	kfree(buf);

	if (status != IUU_OPERATION_OK)
		dbg("%s - vcc error status = %2x", __func__, status);
	else
		dbg("%s - vcc OK !", __func__);

	return status;
}

/*
 * Sysfs Attributes
 */

static ssize_t show_vcc_mode(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct iuu_private *priv = usb_get_serial_port_data(port);

	return sprintf(buf, "%d\n", priv->vcc);
}

static ssize_t store_vcc_mode(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_serial_port *port = to_usb_serial_port(dev);
	struct iuu_private *priv = usb_get_serial_port_data(port);
	unsigned long v;

	if (strict_strtoul(buf, 10, &v)) {
		dev_err(dev, "%s - vcc_mode: %s is not a unsigned long\n",
				__func__, buf);
		goto fail_store_vcc_mode;
	}

	dbg("%s: setting vcc_mode = %ld", __func__, v);

	if ((v != 3) && (v != 5)) {
		dev_err(dev, "%s - vcc_mode %ld is invalid\n", __func__, v);
	} else {
		iuu_vcc_set(port, v);
		priv->vcc = v;
	}
fail_store_vcc_mode:
	return count;
}

static DEVICE_ATTR(vcc_mode, S_IRUSR | S_IWUSR, show_vcc_mode,
	store_vcc_mode);

static int iuu_create_sysfs_attrs(struct usb_serial_port *port)
{
	dbg("%s", __func__);

	return device_create_file(&port->dev, &dev_attr_vcc_mode);
}

static int iuu_remove_sysfs_attrs(struct usb_serial_port *port)
{
	dbg("%s", __func__);

	device_remove_file(&port->dev, &dev_attr_vcc_mode);
	return 0;
}

/*
 * End Sysfs Attributes
 */

static struct usb_serial_driver iuu_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "iuu_phoenix",
		   },
	.id_table = id_table,
	.usb_driver = &iuu_driver,
	.num_ports = 1,
	.bulk_in_size = 512,
	.bulk_out_size = 512,
	.port_probe = iuu_create_sysfs_attrs,
	.port_remove = iuu_remove_sysfs_attrs,
	.open = iuu_open,
	.close = iuu_close,
	.write = iuu_uart_write,
	.read_bulk_callback = iuu_uart_read_callback,
	.tiocmget = iuu_tiocmget,
	.tiocmset = iuu_tiocmset,
	.set_termios = iuu_set_termios,
	.init_termios = iuu_init_termios,
	.attach = iuu_startup,
	.release = iuu_release,
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
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
	       DRIVER_DESC "\n");
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
MODULE_PARM_DESC(xmas, "Xmas colors enabled or not");

module_param(boost, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(boost, "Card overclock boost (in percent 100-500)");

module_param(clockmode, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(clockmode, "Card clock mode (1=3.579 MHz, 2=3.680 MHz, "
		"3=6 Mhz)");

module_param(cdmode, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(cdmode, "Card detect mode (0=none, 1=CD, 2=!CD, 3=DSR, "
		 "4=!DSR, 5=CTS, 6=!CTS, 7=RING, 8=!RING)");

module_param(vcc_default, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(vcc_default, "Set default VCC (either 3 for 3.3V or 5 "
		"for 5V). Default to 5.");
