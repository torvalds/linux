/****************************************************************************
 *
 * Driver for the IFX 6x60 spi modem.
 *
 * Copyright (C) 2008 Option International
 * Copyright (C) 2008 Filip Aben <f.aben@option.com>
 *		      Denis Joseph Barrow <d.barow@option.com>
 *		      Jan Dumon <j.dumon@option.com>
 *
 * Copyright (C) 2009, 2010 Intel Corp
 * Russ Gorby <russ.gorby@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA
 *
 * Driver modified by Intel from Option gtm501l_spi.c
 *
 * Notes
 * o	The driver currently assumes a single device only. If you need to
 *	change this then look for saved_ifx_dev and add a device lookup
 * o	The driver is intended to be big-endian safe but has never been
 *	tested that way (no suitable hardware). There are a couple of FIXME
 *	notes by areas that may need addressing
 * o	Some of the GPIO naming/setup assumptions may need revisiting if
 *	you need to use this driver for another platform.
 *
 *****************************************************************************/
#include <linux/module.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/kfifo.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/rfkill.h>
#include <linux/fs.h>
#include <linux/ip.h>
#include <linux/dmapool.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/spi/ifx_modem.h>
#include <linux/delay.h>

#include "ifx6x60.h"

#define IFX_SPI_MORE_MASK		0x10
#define IFX_SPI_MORE_BIT		12	/* bit position in u16 */
#define IFX_SPI_CTS_BIT			13	/* bit position in u16 */
#define IFX_SPI_MODE			SPI_MODE_1
#define IFX_SPI_TTY_ID			0
#define IFX_SPI_TIMEOUT_SEC		2
#define IFX_SPI_HEADER_0		(-1)
#define IFX_SPI_HEADER_F		(-2)

/* forward reference */
static void ifx_spi_handle_srdy(struct ifx_spi_device *ifx_dev);

/* local variables */
static int spi_bpw = 16;		/* 8, 16 or 32 bit word length */
static struct tty_driver *tty_drv;
static struct ifx_spi_device *saved_ifx_dev;
static struct lock_class_key ifx_spi_key;

/* GPIO/GPE settings */

/**
 *	mrdy_set_high		-	set MRDY GPIO
 *	@ifx: device we are controlling
 *
 */
static inline void mrdy_set_high(struct ifx_spi_device *ifx)
{
	gpio_set_value(ifx->gpio.mrdy, 1);
}

/**
 *	mrdy_set_low		-	clear MRDY GPIO
 *	@ifx: device we are controlling
 *
 */
static inline void mrdy_set_low(struct ifx_spi_device *ifx)
{
	gpio_set_value(ifx->gpio.mrdy, 0);
}

/**
 *	ifx_spi_power_state_set
 *	@ifx_dev: our SPI device
 *	@val: bits to set
 *
 *	Set bit in power status and signal power system if status becomes non-0
 */
static void
ifx_spi_power_state_set(struct ifx_spi_device *ifx_dev, unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&ifx_dev->power_lock, flags);

	/*
	 * if power status is already non-0, just update, else
	 * tell power system
	 */
	if (!ifx_dev->power_status)
		pm_runtime_get(&ifx_dev->spi_dev->dev);
	ifx_dev->power_status |= val;

	spin_unlock_irqrestore(&ifx_dev->power_lock, flags);
}

/**
 *	ifx_spi_power_state_clear	-	clear power bit
 *	@ifx_dev: our SPI device
 *	@val: bits to clear
 *
 *	clear bit in power status and signal power system if status becomes 0
 */
static void
ifx_spi_power_state_clear(struct ifx_spi_device *ifx_dev, unsigned char val)
{
	unsigned long flags;

	spin_lock_irqsave(&ifx_dev->power_lock, flags);

	if (ifx_dev->power_status) {
		ifx_dev->power_status &= ~val;
		if (!ifx_dev->power_status)
			pm_runtime_put(&ifx_dev->spi_dev->dev);
	}

	spin_unlock_irqrestore(&ifx_dev->power_lock, flags);
}

/**
 *	swap_buf
 *	@buf: our buffer
 *	@len : number of bytes (not words) in the buffer
 *	@end: end of buffer
 *
 *	Swap the contents of a buffer into big endian format
 */
static inline void swap_buf(u16 *buf, int len, void *end)
{
	int n;

	len = ((len + 1) >> 1);
	if ((void *)&buf[len] > end) {
		pr_err("swap_buf: swap exceeds boundary (%p > %p)!",
		       &buf[len], end);
		return;
	}
	for (n = 0; n < len; n++) {
		*buf = cpu_to_be16(*buf);
		buf++;
	}
}

/**
 *	mrdy_assert		-	assert MRDY line
 *	@ifx_dev: our SPI device
 *
 *	Assert mrdy and set timer to wait for SRDY interrupt, if SRDY is low
 *	now.
 *
 *	FIXME: Can SRDY even go high as we are running this code ?
 */
static void mrdy_assert(struct ifx_spi_device *ifx_dev)
{
	int val = gpio_get_value(ifx_dev->gpio.srdy);
	if (!val) {
		if (!test_and_set_bit(IFX_SPI_STATE_TIMER_PENDING,
				      &ifx_dev->flags)) {
			ifx_dev->spi_timer.expires =
				jiffies + IFX_SPI_TIMEOUT_SEC*HZ;
			add_timer(&ifx_dev->spi_timer);

		}
	}
	ifx_spi_power_state_set(ifx_dev, IFX_SPI_POWER_DATA_PENDING);
	mrdy_set_high(ifx_dev);
}

/**
 *	ifx_spi_hangup		-	hang up an IFX device
 *	@ifx_dev: our SPI device
 *
 *	Hang up the tty attached to the IFX device if one is currently
 *	open. If not take no action
 */
static void ifx_spi_ttyhangup(struct ifx_spi_device *ifx_dev)
{
	struct tty_port *pport = &ifx_dev->tty_port;
	struct tty_struct *tty = tty_port_tty_get(pport);
	if (tty) {
		tty_hangup(tty);
		tty_kref_put(tty);
	}
}

/**
 *	ifx_spi_timeout		-	SPI timeout
 *	@arg: our SPI device
 *
 *	The SPI has timed out: hang up the tty. Users will then see a hangup
 *	and error events.
 */
static void ifx_spi_timeout(unsigned long arg)
{
	struct ifx_spi_device *ifx_dev = (struct ifx_spi_device *)arg;

	dev_warn(&ifx_dev->spi_dev->dev, "*** SPI Timeout ***");
	ifx_spi_ttyhangup(ifx_dev);
	mrdy_set_low(ifx_dev);
	clear_bit(IFX_SPI_STATE_TIMER_PENDING, &ifx_dev->flags);
}

/* char/tty operations */

/**
 *	ifx_spi_tiocmget	-	get modem lines
 *	@tty: our tty device
 *	@filp: file handle issuing the request
 *
 *	Map the signal state into Linux modem flags and report the value
 *	in Linux terms
 */
static int ifx_spi_tiocmget(struct tty_struct *tty)
{
	unsigned int value;
	struct ifx_spi_device *ifx_dev = tty->driver_data;

	value =
	(test_bit(IFX_SPI_RTS, &ifx_dev->signal_state) ? TIOCM_RTS : 0) |
	(test_bit(IFX_SPI_DTR, &ifx_dev->signal_state) ? TIOCM_DTR : 0) |
	(test_bit(IFX_SPI_CTS, &ifx_dev->signal_state) ? TIOCM_CTS : 0) |
	(test_bit(IFX_SPI_DSR, &ifx_dev->signal_state) ? TIOCM_DSR : 0) |
	(test_bit(IFX_SPI_DCD, &ifx_dev->signal_state) ? TIOCM_CAR : 0) |
	(test_bit(IFX_SPI_RI, &ifx_dev->signal_state) ? TIOCM_RNG : 0);
	return value;
}

/**
 *	ifx_spi_tiocmset	-	set modem bits
 *	@tty: the tty structure
 *	@set: bits to set
 *	@clear: bits to clear
 *
 *	The IFX6x60 only supports DTR and RTS. Set them accordingly
 *	and flag that an update to the modem is needed.
 *
 *	FIXME: do we need to kick the tranfers when we do this ?
 */
static int ifx_spi_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct ifx_spi_device *ifx_dev = tty->driver_data;

	if (set & TIOCM_RTS)
		set_bit(IFX_SPI_RTS, &ifx_dev->signal_state);
	if (set & TIOCM_DTR)
		set_bit(IFX_SPI_DTR, &ifx_dev->signal_state);
	if (clear & TIOCM_RTS)
		clear_bit(IFX_SPI_RTS, &ifx_dev->signal_state);
	if (clear & TIOCM_DTR)
		clear_bit(IFX_SPI_DTR, &ifx_dev->signal_state);

	set_bit(IFX_SPI_UPDATE, &ifx_dev->signal_state);
	return 0;
}

/**
 *	ifx_spi_open	-	called on tty open
 *	@tty: our tty device
 *	@filp: file handle being associated with the tty
 *
 *	Open the tty interface. We let the tty_port layer do all the work
 *	for us.
 *
 *	FIXME: Remove single device assumption and saved_ifx_dev
 */
static int ifx_spi_open(struct tty_struct *tty, struct file *filp)
{
	return tty_port_open(&saved_ifx_dev->tty_port, tty, filp);
}

/**
 *	ifx_spi_close	-	called when our tty closes
 *	@tty: the tty being closed
 *	@filp: the file handle being closed
 *
 *	Perform the close of the tty. We use the tty_port layer to do all
 *	our hard work.
 */
static void ifx_spi_close(struct tty_struct *tty, struct file *filp)
{
	struct ifx_spi_device *ifx_dev = tty->driver_data;
	tty_port_close(&ifx_dev->tty_port, tty, filp);
	/* FIXME: should we do an ifx_spi_reset here ? */
}

/**
 *	ifx_decode_spi_header	-	decode received header
 *	@buffer: the received data
 *	@length: decoded length
 *	@more: decoded more flag
 *	@received_cts: status of cts we received
 *
 *	Note how received_cts is handled -- if header is all F it is left
 *	the same as it was, if header is all 0 it is set to 0 otherwise it is
 *	taken from the incoming header.
 *
 *	FIXME: endianness
 */
static int ifx_spi_decode_spi_header(unsigned char *buffer, int *length,
			unsigned char *more, unsigned char *received_cts)
{
	u16 h1;
	u16 h2;
	u16 *in_buffer = (u16 *)buffer;

	h1 = *in_buffer;
	h2 = *(in_buffer+1);

	if (h1 == 0 && h2 == 0) {
		*received_cts = 0;
		return IFX_SPI_HEADER_0;
	} else if (h1 == 0xffff && h2 == 0xffff) {
		/* spi_slave_cts remains as it was */
		return IFX_SPI_HEADER_F;
	}

	*length = h1 & 0xfff;	/* upper bits of byte are flags */
	*more = (buffer[1] >> IFX_SPI_MORE_BIT) & 1;
	*received_cts = (buffer[3] >> IFX_SPI_CTS_BIT) & 1;
	return 0;
}

/**
 *	ifx_setup_spi_header	-	set header fields
 *	@txbuffer: pointer to start of SPI buffer
 *	@tx_count: bytes
 *	@more: indicate if more to follow
 *
 *	Format up an SPI header for a transfer
 *
 *	FIXME: endianness?
 */
static void ifx_spi_setup_spi_header(unsigned char *txbuffer, int tx_count,
					unsigned char more)
{
	*(u16 *)(txbuffer) = tx_count;
	*(u16 *)(txbuffer+2) = IFX_SPI_PAYLOAD_SIZE;
	txbuffer[1] |= (more << IFX_SPI_MORE_BIT) & IFX_SPI_MORE_MASK;
}

/**
 *	ifx_spi_wakeup_serial	-	SPI space made
 *	@port_data: our SPI device
 *
 *	We have emptied the FIFO enough that we want to get more data
 *	queued into it. Poke the line discipline via tty_wakeup so that
 *	it will feed us more bits
 */
static void ifx_spi_wakeup_serial(struct ifx_spi_device *ifx_dev)
{
	struct tty_struct *tty;

	tty = tty_port_tty_get(&ifx_dev->tty_port);
	if (!tty)
		return;
	tty_wakeup(tty);
	tty_kref_put(tty);
}

/**
 *	ifx_spi_prepare_tx_buffer	-	prepare transmit frame
 *	@ifx_dev: our SPI device
 *
 *	The transmit buffr needs a header and various other bits of
 *	information followed by as much data as we can pull from the FIFO
 *	and transfer. This function formats up a suitable buffer in the
 *	ifx_dev->tx_buffer
 *
 *	FIXME: performance - should we wake the tty when the queue is half
 *			     empty ?
 */
static int ifx_spi_prepare_tx_buffer(struct ifx_spi_device *ifx_dev)
{
	int temp_count;
	int queue_length;
	int tx_count;
	unsigned char *tx_buffer;

	tx_buffer = ifx_dev->tx_buffer;
	memset(tx_buffer, 0, IFX_SPI_TRANSFER_SIZE);

	/* make room for required SPI header */
	tx_buffer += IFX_SPI_HEADER_OVERHEAD;
	tx_count = IFX_SPI_HEADER_OVERHEAD;

	/* clear to signal no more data if this turns out to be the
	 * last buffer sent in a sequence */
	ifx_dev->spi_more = 0;

	/* if modem cts is set, just send empty buffer */
	if (!ifx_dev->spi_slave_cts) {
		/* see if there's tx data */
		queue_length = kfifo_len(&ifx_dev->tx_fifo);
		if (queue_length != 0) {
			/* data to mux -- see if there's room for it */
			temp_count = min(queue_length, IFX_SPI_PAYLOAD_SIZE);
			temp_count = kfifo_out_locked(&ifx_dev->tx_fifo,
					tx_buffer, temp_count,
					&ifx_dev->fifo_lock);

			/* update buffer pointer and data count in message */
			tx_buffer += temp_count;
			tx_count += temp_count;
			if (temp_count == queue_length)
				/* poke port to get more data */
				ifx_spi_wakeup_serial(ifx_dev);
			else /* more data in port, use next SPI message */
				ifx_dev->spi_more = 1;
		}
	}
	/* have data and info for header -- set up SPI header in buffer */
	/* spi header needs payload size, not entire buffer size */
	ifx_spi_setup_spi_header(ifx_dev->tx_buffer,
					tx_count-IFX_SPI_HEADER_OVERHEAD,
					ifx_dev->spi_more);
	/* swap actual data in the buffer */
	swap_buf((u16 *)(ifx_dev->tx_buffer), tx_count,
		&ifx_dev->tx_buffer[IFX_SPI_TRANSFER_SIZE]);
	return tx_count;
}

/**
 *	ifx_spi_write		-	line discipline write
 *	@tty: our tty device
 *	@buf: pointer to buffer to write (kernel space)
 *	@count: size of buffer
 *
 *	Write the characters we have been given into the FIFO. If the device
 *	is not active then activate it, when the SRDY line is asserted back
 *	this will commence I/O
 */
static int ifx_spi_write(struct tty_struct *tty, const unsigned char *buf,
			 int count)
{
	struct ifx_spi_device *ifx_dev = tty->driver_data;
	unsigned char *tmp_buf = (unsigned char *)buf;
	int tx_count = kfifo_in_locked(&ifx_dev->tx_fifo, tmp_buf, count,
				   &ifx_dev->fifo_lock);
	mrdy_assert(ifx_dev);
	return tx_count;
}

/**
 *	ifx_spi_chars_in_buffer	-	line discipline helper
 *	@tty: our tty device
 *
 *	Report how much data we can accept before we drop bytes. As we use
 *	a simple FIFO this is nice and easy.
 */
static int ifx_spi_write_room(struct tty_struct *tty)
{
	struct ifx_spi_device *ifx_dev = tty->driver_data;
	return IFX_SPI_FIFO_SIZE - kfifo_len(&ifx_dev->tx_fifo);
}

/**
 *	ifx_spi_chars_in_buffer	-	line discipline helper
 *	@tty: our tty device
 *
 *	Report how many characters we have buffered. In our case this is the
 *	number of bytes sitting in our transmit FIFO.
 */
static int ifx_spi_chars_in_buffer(struct tty_struct *tty)
{
	struct ifx_spi_device *ifx_dev = tty->driver_data;
	return kfifo_len(&ifx_dev->tx_fifo);
}

/**
 *	ifx_port_hangup
 *	@port: our tty port
 *
 *	tty port hang up. Called when tty_hangup processing is invoked either
 *	by loss of carrier, or by software (eg vhangup). Serialized against
 *	activate/shutdown by the tty layer.
 */
static void ifx_spi_hangup(struct tty_struct *tty)
{
	struct ifx_spi_device *ifx_dev = tty->driver_data;
	tty_port_hangup(&ifx_dev->tty_port);
}

/**
 *	ifx_port_activate
 *	@port: our tty port
 *
 *	tty port activate method - called for first open. Serialized
 *	with hangup and shutdown by the tty layer.
 */
static int ifx_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct ifx_spi_device *ifx_dev =
		container_of(port, struct ifx_spi_device, tty_port);

	/* clear any old data; can't do this in 'close' */
	kfifo_reset(&ifx_dev->tx_fifo);

	/* put port data into this tty */
	tty->driver_data = ifx_dev;

	/* allows flip string push from int context */
	tty->low_latency = 1;

	return 0;
}

/**
 *	ifx_port_shutdown
 *	@port: our tty port
 *
 *	tty port shutdown method - called for last port close. Serialized
 *	with hangup and activate by the tty layer.
 */
static void ifx_port_shutdown(struct tty_port *port)
{
	struct ifx_spi_device *ifx_dev =
		container_of(port, struct ifx_spi_device, tty_port);

	mrdy_set_low(ifx_dev);
	del_timer(&ifx_dev->spi_timer);
	clear_bit(IFX_SPI_STATE_TIMER_PENDING, &ifx_dev->flags);
	tasklet_kill(&ifx_dev->io_work_tasklet);
}

static const struct tty_port_operations ifx_tty_port_ops = {
	.activate = ifx_port_activate,
	.shutdown = ifx_port_shutdown,
};

static const struct tty_operations ifx_spi_serial_ops = {
	.open = ifx_spi_open,
	.close = ifx_spi_close,
	.write = ifx_spi_write,
	.hangup = ifx_spi_hangup,
	.write_room = ifx_spi_write_room,
	.chars_in_buffer = ifx_spi_chars_in_buffer,
	.tiocmget = ifx_spi_tiocmget,
	.tiocmset = ifx_spi_tiocmset,
};

/**
 *	ifx_spi_insert_fip_string	-	queue received data
 *	@ifx_ser: our SPI device
 *	@chars: buffer we have received
 *	@size: number of chars reeived
 *
 *	Queue bytes to the tty assuming the tty side is currently open. If
 *	not the discard the data.
 */
static void ifx_spi_insert_flip_string(struct ifx_spi_device *ifx_dev,
				    unsigned char *chars, size_t size)
{
	struct tty_struct *tty = tty_port_tty_get(&ifx_dev->tty_port);
	if (!tty)
		return;
	tty_insert_flip_string(tty, chars, size);
	tty_flip_buffer_push(tty);
	tty_kref_put(tty);
}

/**
 *	ifx_spi_complete	-	SPI transfer completed
 *	@ctx: our SPI device
 *
 *	An SPI transfer has completed. Process any received data and kick off
 *	any further transmits we can commence.
 */
static void ifx_spi_complete(void *ctx)
{
	struct ifx_spi_device *ifx_dev = ctx;
	struct tty_struct *tty;
	struct tty_ldisc *ldisc = NULL;
	int length;
	int actual_length;
	unsigned char more;
	unsigned char cts;
	int local_write_pending = 0;
	int queue_length;
	int srdy;
	int decode_result;

	mrdy_set_low(ifx_dev);

	if (!ifx_dev->spi_msg.status) {
		/* check header validity, get comm flags */
		swap_buf((u16 *)ifx_dev->rx_buffer, IFX_SPI_HEADER_OVERHEAD,
			&ifx_dev->rx_buffer[IFX_SPI_HEADER_OVERHEAD]);
		decode_result = ifx_spi_decode_spi_header(ifx_dev->rx_buffer,
				&length, &more, &cts);
		if (decode_result == IFX_SPI_HEADER_0) {
			dev_dbg(&ifx_dev->spi_dev->dev,
				"ignore input: invalid header 0");
			ifx_dev->spi_slave_cts = 0;
			goto complete_exit;
		} else if (decode_result == IFX_SPI_HEADER_F) {
			dev_dbg(&ifx_dev->spi_dev->dev,
				"ignore input: invalid header F");
			goto complete_exit;
		}

		ifx_dev->spi_slave_cts = cts;

		actual_length = min((unsigned int)length,
					ifx_dev->spi_msg.actual_length);
		swap_buf((u16 *)(ifx_dev->rx_buffer + IFX_SPI_HEADER_OVERHEAD),
			 actual_length,
			 &ifx_dev->rx_buffer[IFX_SPI_TRANSFER_SIZE]);
		ifx_spi_insert_flip_string(
			ifx_dev,
			ifx_dev->rx_buffer + IFX_SPI_HEADER_OVERHEAD,
			(size_t)actual_length);
	} else {
		dev_dbg(&ifx_dev->spi_dev->dev, "SPI transfer error %d",
		       ifx_dev->spi_msg.status);
	}

complete_exit:
	if (ifx_dev->write_pending) {
		ifx_dev->write_pending = 0;
		local_write_pending = 1;
	}

	clear_bit(IFX_SPI_STATE_IO_IN_PROGRESS, &(ifx_dev->flags));

	queue_length = kfifo_len(&ifx_dev->tx_fifo);
	srdy = gpio_get_value(ifx_dev->gpio.srdy);
	if (!srdy)
		ifx_spi_power_state_clear(ifx_dev, IFX_SPI_POWER_SRDY);

	/* schedule output if there is more to do */
	if (test_and_clear_bit(IFX_SPI_STATE_IO_READY, &ifx_dev->flags))
		tasklet_schedule(&ifx_dev->io_work_tasklet);
	else {
		if (more || ifx_dev->spi_more || queue_length > 0 ||
			local_write_pending) {
			if (ifx_dev->spi_slave_cts) {
				if (more)
					mrdy_assert(ifx_dev);
			} else
				mrdy_assert(ifx_dev);
		} else {
			/*
			 * poke line discipline driver if any for more data
			 * may or may not get more data to write
			 * for now, say not busy
			 */
			ifx_spi_power_state_clear(ifx_dev,
						  IFX_SPI_POWER_DATA_PENDING);
			tty = tty_port_tty_get(&ifx_dev->tty_port);
			if (tty) {
				ldisc = tty_ldisc_ref(tty);
				if (ldisc) {
					ldisc->ops->write_wakeup(tty);
					tty_ldisc_deref(ldisc);
				}
				tty_kref_put(tty);
			}
		}
	}
}

/**
 *	ifx_spio_io		-	I/O tasklet
 *	@data: our SPI device
 *
 *	Queue data for transmission if possible and then kick off the
 *	transfer.
 */
static void ifx_spi_io(unsigned long data)
{
	int retval;
	struct ifx_spi_device *ifx_dev = (struct ifx_spi_device *) data;

	if (!test_and_set_bit(IFX_SPI_STATE_IO_IN_PROGRESS, &ifx_dev->flags)) {
		if (ifx_dev->gpio.unack_srdy_int_nb > 0)
			ifx_dev->gpio.unack_srdy_int_nb--;

		ifx_spi_prepare_tx_buffer(ifx_dev);

		spi_message_init(&ifx_dev->spi_msg);
		INIT_LIST_HEAD(&ifx_dev->spi_msg.queue);

		ifx_dev->spi_msg.context = ifx_dev;
		ifx_dev->spi_msg.complete = ifx_spi_complete;

		/* set up our spi transfer */
		/* note len is BYTES, not transfers */
		ifx_dev->spi_xfer.len = IFX_SPI_TRANSFER_SIZE;
		ifx_dev->spi_xfer.cs_change = 0;
		ifx_dev->spi_xfer.speed_hz = ifx_dev->spi_dev->max_speed_hz;
		/* ifx_dev->spi_xfer.speed_hz = 390625; */
		ifx_dev->spi_xfer.bits_per_word = spi_bpw;

		ifx_dev->spi_xfer.tx_buf = ifx_dev->tx_buffer;
		ifx_dev->spi_xfer.rx_buf = ifx_dev->rx_buffer;

		/*
		 * setup dma pointers
		 */
		if (ifx_dev->use_dma) {
			ifx_dev->spi_msg.is_dma_mapped = 1;
			ifx_dev->tx_dma = ifx_dev->tx_bus;
			ifx_dev->rx_dma = ifx_dev->rx_bus;
			ifx_dev->spi_xfer.tx_dma = ifx_dev->tx_dma;
			ifx_dev->spi_xfer.rx_dma = ifx_dev->rx_dma;
		} else {
			ifx_dev->spi_msg.is_dma_mapped = 0;
			ifx_dev->tx_dma = (dma_addr_t)0;
			ifx_dev->rx_dma = (dma_addr_t)0;
			ifx_dev->spi_xfer.tx_dma = (dma_addr_t)0;
			ifx_dev->spi_xfer.rx_dma = (dma_addr_t)0;
		}

		spi_message_add_tail(&ifx_dev->spi_xfer, &ifx_dev->spi_msg);

		/* Assert MRDY. This may have already been done by the write
		 * routine.
		 */
		mrdy_assert(ifx_dev);

		retval = spi_async(ifx_dev->spi_dev, &ifx_dev->spi_msg);
		if (retval) {
			clear_bit(IFX_SPI_STATE_IO_IN_PROGRESS,
				  &ifx_dev->flags);
			tasklet_schedule(&ifx_dev->io_work_tasklet);
			return;
		}
	} else
		ifx_dev->write_pending = 1;
}

/**
 *	ifx_spi_free_port	-	free up the tty side
 *	@ifx_dev: IFX device going away
 *
 *	Unregister and free up a port when the device goes away
 */
static void ifx_spi_free_port(struct ifx_spi_device *ifx_dev)
{
	if (ifx_dev->tty_dev)
		tty_unregister_device(tty_drv, ifx_dev->minor);
	kfifo_free(&ifx_dev->tx_fifo);
}

/**
 *	ifx_spi_create_port	-	create a new port
 *	@ifx_dev: our spi device
 *
 *	Allocate and initialise the tty port that goes with this interface
 *	and add it to the tty layer so that it can be opened.
 */
static int ifx_spi_create_port(struct ifx_spi_device *ifx_dev)
{
	int ret = 0;
	struct tty_port *pport = &ifx_dev->tty_port;

	spin_lock_init(&ifx_dev->fifo_lock);
	lockdep_set_class_and_subclass(&ifx_dev->fifo_lock,
		&ifx_spi_key, 0);

	if (kfifo_alloc(&ifx_dev->tx_fifo, IFX_SPI_FIFO_SIZE, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto error_ret;
	}

	tty_port_init(pport);
	pport->ops = &ifx_tty_port_ops;
	ifx_dev->minor = IFX_SPI_TTY_ID;
	ifx_dev->tty_dev = tty_register_device(tty_drv, ifx_dev->minor,
					       &ifx_dev->spi_dev->dev);
	if (IS_ERR(ifx_dev->tty_dev)) {
		dev_dbg(&ifx_dev->spi_dev->dev,
			"%s: registering tty device failed", __func__);
		ret = PTR_ERR(ifx_dev->tty_dev);
		goto error_ret;
	}
	return 0;

error_ret:
	ifx_spi_free_port(ifx_dev);
	return ret;
}

/**
 *	ifx_spi_handle_srdy		-	handle SRDY
 *	@ifx_dev: device asserting SRDY
 *
 *	Check our device state and see what we need to kick off when SRDY
 *	is asserted. This usually means killing the timer and firing off the
 *	I/O processing.
 */
static void ifx_spi_handle_srdy(struct ifx_spi_device *ifx_dev)
{
	if (test_bit(IFX_SPI_STATE_TIMER_PENDING, &ifx_dev->flags)) {
		del_timer_sync(&ifx_dev->spi_timer);
		clear_bit(IFX_SPI_STATE_TIMER_PENDING, &ifx_dev->flags);
	}

	ifx_spi_power_state_set(ifx_dev, IFX_SPI_POWER_SRDY);

	if (!test_bit(IFX_SPI_STATE_IO_IN_PROGRESS, &ifx_dev->flags))
		tasklet_schedule(&ifx_dev->io_work_tasklet);
	else
		set_bit(IFX_SPI_STATE_IO_READY, &ifx_dev->flags);
}

/**
 *	ifx_spi_srdy_interrupt	-	SRDY asserted
 *	@irq: our IRQ number
 *	@dev: our ifx device
 *
 *	The modem asserted SRDY. Handle the srdy event
 */
static irqreturn_t ifx_spi_srdy_interrupt(int irq, void *dev)
{
	struct ifx_spi_device *ifx_dev = dev;
	ifx_dev->gpio.unack_srdy_int_nb++;
	ifx_spi_handle_srdy(ifx_dev);
	return IRQ_HANDLED;
}

/**
 *	ifx_spi_reset_interrupt	-	Modem has changed reset state
 *	@irq: interrupt number
 *	@dev: our device pointer
 *
 *	The modem has either entered or left reset state. Check the GPIO
 *	line to see which.
 *
 *	FIXME: review locking on MR_INPROGRESS versus
 *	parallel unsolicited reset/solicited reset
 */
static irqreturn_t ifx_spi_reset_interrupt(int irq, void *dev)
{
	struct ifx_spi_device *ifx_dev = dev;
	int val = gpio_get_value(ifx_dev->gpio.reset_out);
	int solreset = test_bit(MR_START, &ifx_dev->mdm_reset_state);

	if (val == 0) {
		/* entered reset */
		set_bit(MR_INPROGRESS, &ifx_dev->mdm_reset_state);
		if (!solreset) {
			/* unsolicited reset  */
			ifx_spi_ttyhangup(ifx_dev);
		}
	} else {
		/* exited reset */
		clear_bit(MR_INPROGRESS, &ifx_dev->mdm_reset_state);
		if (solreset) {
			set_bit(MR_COMPLETE, &ifx_dev->mdm_reset_state);
			wake_up(&ifx_dev->mdm_reset_wait);
		}
	}
	return IRQ_HANDLED;
}

/**
 *	ifx_spi_free_device - free device
 *	@ifx_dev: device to free
 *
 *	Free the IFX device
 */
static void ifx_spi_free_device(struct ifx_spi_device *ifx_dev)
{
	ifx_spi_free_port(ifx_dev);
	dma_free_coherent(&ifx_dev->spi_dev->dev,
				IFX_SPI_TRANSFER_SIZE,
				ifx_dev->tx_buffer,
				ifx_dev->tx_bus);
	dma_free_coherent(&ifx_dev->spi_dev->dev,
				IFX_SPI_TRANSFER_SIZE,
				ifx_dev->rx_buffer,
				ifx_dev->rx_bus);
}

/**
 *	ifx_spi_reset	-	reset modem
 *	@ifx_dev: modem to reset
 *
 *	Perform a reset on the modem
 */
static int ifx_spi_reset(struct ifx_spi_device *ifx_dev)
{
	int ret;
	/*
	 * set up modem power, reset
	 *
	 * delays are required on some platforms for the modem
	 * to reset properly
	 */
	set_bit(MR_START, &ifx_dev->mdm_reset_state);
	gpio_set_value(ifx_dev->gpio.po, 0);
	gpio_set_value(ifx_dev->gpio.reset, 0);
	msleep(25);
	gpio_set_value(ifx_dev->gpio.reset, 1);
	msleep(1);
	gpio_set_value(ifx_dev->gpio.po, 1);
	msleep(1);
	gpio_set_value(ifx_dev->gpio.po, 0);
	ret = wait_event_timeout(ifx_dev->mdm_reset_wait,
				 test_bit(MR_COMPLETE,
					  &ifx_dev->mdm_reset_state),
				 IFX_RESET_TIMEOUT);
	if (!ret)
		dev_warn(&ifx_dev->spi_dev->dev, "Modem reset timeout: (state:%lx)",
			 ifx_dev->mdm_reset_state);

	ifx_dev->mdm_reset_state = 0;
	return ret;
}

/**
 *	ifx_spi_spi_probe	-	probe callback
 *	@spi: our possible matching SPI device
 *
 *	Probe for a 6x60 modem on SPI bus. Perform any needed device and
 *	GPIO setup.
 *
 *	FIXME:
 *	-	Support for multiple devices
 *	-	Split out MID specific GPIO handling eventually
 */

static int ifx_spi_spi_probe(struct spi_device *spi)
{
	int ret;
	int srdy;
	struct ifx_modem_platform_data *pl_data;
	struct ifx_spi_device *ifx_dev;

	if (saved_ifx_dev) {
		dev_dbg(&spi->dev, "ignoring subsequent detection");
		return -ENODEV;
	}

	pl_data = (struct ifx_modem_platform_data *)spi->dev.platform_data;
	if (!pl_data) {
		dev_err(&spi->dev, "missing platform data!");
		return -ENODEV;
	}

	/* initialize structure to hold our device variables */
	ifx_dev = kzalloc(sizeof(struct ifx_spi_device), GFP_KERNEL);
	if (!ifx_dev) {
		dev_err(&spi->dev, "spi device allocation failed");
		return -ENOMEM;
	}
	saved_ifx_dev = ifx_dev;
	ifx_dev->spi_dev = spi;
	clear_bit(IFX_SPI_STATE_IO_IN_PROGRESS, &ifx_dev->flags);
	spin_lock_init(&ifx_dev->write_lock);
	spin_lock_init(&ifx_dev->power_lock);
	ifx_dev->power_status = 0;
	init_timer(&ifx_dev->spi_timer);
	ifx_dev->spi_timer.function = ifx_spi_timeout;
	ifx_dev->spi_timer.data = (unsigned long)ifx_dev;
	ifx_dev->modem = pl_data->modem_type;
	ifx_dev->use_dma = pl_data->use_dma;
	ifx_dev->max_hz = pl_data->max_hz;
	/* initialize spi mode, etc */
	spi->max_speed_hz = ifx_dev->max_hz;
	spi->mode = IFX_SPI_MODE | (SPI_LOOP & spi->mode);
	spi->bits_per_word = spi_bpw;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "SPI setup wasn't successful %d", ret);
		return -ENODEV;
	}

	/* ensure SPI protocol flags are initialized to enable transfer */
	ifx_dev->spi_more = 0;
	ifx_dev->spi_slave_cts = 0;

	/*initialize transfer and dma buffers */
	ifx_dev->tx_buffer = dma_alloc_coherent(ifx_dev->spi_dev->dev.parent,
				IFX_SPI_TRANSFER_SIZE,
				&ifx_dev->tx_bus,
				GFP_KERNEL);
	if (!ifx_dev->tx_buffer) {
		dev_err(&spi->dev, "DMA-TX buffer allocation failed");
		ret = -ENOMEM;
		goto error_ret;
	}
	ifx_dev->rx_buffer = dma_alloc_coherent(ifx_dev->spi_dev->dev.parent,
				IFX_SPI_TRANSFER_SIZE,
				&ifx_dev->rx_bus,
				GFP_KERNEL);
	if (!ifx_dev->rx_buffer) {
		dev_err(&spi->dev, "DMA-RX buffer allocation failed");
		ret = -ENOMEM;
		goto error_ret;
	}

	/* initialize waitq for modem reset */
	init_waitqueue_head(&ifx_dev->mdm_reset_wait);

	spi_set_drvdata(spi, ifx_dev);
	tasklet_init(&ifx_dev->io_work_tasklet, ifx_spi_io,
						(unsigned long)ifx_dev);

	set_bit(IFX_SPI_STATE_PRESENT, &ifx_dev->flags);

	/* create our tty port */
	ret = ifx_spi_create_port(ifx_dev);
	if (ret != 0) {
		dev_err(&spi->dev, "create default tty port failed");
		goto error_ret;
	}

	ifx_dev->gpio.reset = pl_data->rst_pmu;
	ifx_dev->gpio.po = pl_data->pwr_on;
	ifx_dev->gpio.mrdy = pl_data->mrdy;
	ifx_dev->gpio.srdy = pl_data->srdy;
	ifx_dev->gpio.reset_out = pl_data->rst_out;

	dev_info(&spi->dev, "gpios %d, %d, %d, %d, %d",
		 ifx_dev->gpio.reset, ifx_dev->gpio.po, ifx_dev->gpio.mrdy,
		 ifx_dev->gpio.srdy, ifx_dev->gpio.reset_out);

	/* Configure gpios */
	ret = gpio_request(ifx_dev->gpio.reset, "ifxModem");
	if (ret < 0) {
		dev_err(&spi->dev, "Unable to allocate GPIO%d (RESET)",
			ifx_dev->gpio.reset);
		goto error_ret;
	}
	ret += gpio_direction_output(ifx_dev->gpio.reset, 0);
	ret += gpio_export(ifx_dev->gpio.reset, 1);
	if (ret) {
		dev_err(&spi->dev, "Unable to configure GPIO%d (RESET)",
			ifx_dev->gpio.reset);
		ret = -EBUSY;
		goto error_ret2;
	}

	ret = gpio_request(ifx_dev->gpio.po, "ifxModem");
	ret += gpio_direction_output(ifx_dev->gpio.po, 0);
	ret += gpio_export(ifx_dev->gpio.po, 1);
	if (ret) {
		dev_err(&spi->dev, "Unable to configure GPIO%d (ON)",
			ifx_dev->gpio.po);
		ret = -EBUSY;
		goto error_ret3;
	}

	ret = gpio_request(ifx_dev->gpio.mrdy, "ifxModem");
	if (ret < 0) {
		dev_err(&spi->dev, "Unable to allocate GPIO%d (MRDY)",
			ifx_dev->gpio.mrdy);
		goto error_ret3;
	}
	ret += gpio_export(ifx_dev->gpio.mrdy, 1);
	ret += gpio_direction_output(ifx_dev->gpio.mrdy, 0);
	if (ret) {
		dev_err(&spi->dev, "Unable to configure GPIO%d (MRDY)",
			ifx_dev->gpio.mrdy);
		ret = -EBUSY;
		goto error_ret4;
	}

	ret = gpio_request(ifx_dev->gpio.srdy, "ifxModem");
	if (ret < 0) {
		dev_err(&spi->dev, "Unable to allocate GPIO%d (SRDY)",
			ifx_dev->gpio.srdy);
		ret = -EBUSY;
		goto error_ret4;
	}
	ret += gpio_export(ifx_dev->gpio.srdy, 1);
	ret += gpio_direction_input(ifx_dev->gpio.srdy);
	if (ret) {
		dev_err(&spi->dev, "Unable to configure GPIO%d (SRDY)",
			ifx_dev->gpio.srdy);
		ret = -EBUSY;
		goto error_ret5;
	}

	ret = gpio_request(ifx_dev->gpio.reset_out, "ifxModem");
	if (ret < 0) {
		dev_err(&spi->dev, "Unable to allocate GPIO%d (RESET_OUT)",
			ifx_dev->gpio.reset_out);
		goto error_ret5;
	}
	ret += gpio_export(ifx_dev->gpio.reset_out, 1);
	ret += gpio_direction_input(ifx_dev->gpio.reset_out);
	if (ret) {
		dev_err(&spi->dev, "Unable to configure GPIO%d (RESET_OUT)",
			ifx_dev->gpio.reset_out);
		ret = -EBUSY;
		goto error_ret6;
	}

	ret = request_irq(gpio_to_irq(ifx_dev->gpio.reset_out),
			  ifx_spi_reset_interrupt,
			  IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, DRVNAME,
		(void *)ifx_dev);
	if (ret) {
		dev_err(&spi->dev, "Unable to get irq %x\n",
			gpio_to_irq(ifx_dev->gpio.reset_out));
		goto error_ret6;
	}

	ret = ifx_spi_reset(ifx_dev);

	ret = request_irq(gpio_to_irq(ifx_dev->gpio.srdy),
			  ifx_spi_srdy_interrupt,
			  IRQF_TRIGGER_RISING, DRVNAME,
			  (void *)ifx_dev);
	if (ret) {
		dev_err(&spi->dev, "Unable to get irq %x",
			gpio_to_irq(ifx_dev->gpio.srdy));
		goto error_ret7;
	}

	/* set pm runtime power state and register with power system */
	pm_runtime_set_active(&spi->dev);
	pm_runtime_enable(&spi->dev);

	/* handle case that modem is already signaling SRDY */
	/* no outgoing tty open at this point, this just satisfies the
	 * modem's read and should reset communication properly
	 */
	srdy = gpio_get_value(ifx_dev->gpio.srdy);

	if (srdy) {
		mrdy_assert(ifx_dev);
		ifx_spi_handle_srdy(ifx_dev);
	} else
		mrdy_set_low(ifx_dev);
	return 0;

error_ret7:
	free_irq(gpio_to_irq(ifx_dev->gpio.reset_out), (void *)ifx_dev);
error_ret6:
	gpio_free(ifx_dev->gpio.srdy);
error_ret5:
	gpio_free(ifx_dev->gpio.mrdy);
error_ret4:
	gpio_free(ifx_dev->gpio.reset);
error_ret3:
	gpio_free(ifx_dev->gpio.po);
error_ret2:
	gpio_free(ifx_dev->gpio.reset_out);
error_ret:
	ifx_spi_free_device(ifx_dev);
	saved_ifx_dev = NULL;
	return ret;
}

/**
 *	ifx_spi_spi_remove	-	SPI device was removed
 *	@spi: SPI device
 *
 *	FIXME: We should be shutting the device down here not in
 *	the module unload path.
 */

static int ifx_spi_spi_remove(struct spi_device *spi)
{
	struct ifx_spi_device *ifx_dev = spi_get_drvdata(spi);
	/* stop activity */
	tasklet_kill(&ifx_dev->io_work_tasklet);
	/* free irq */
	free_irq(gpio_to_irq(ifx_dev->gpio.reset_out), (void *)ifx_dev);
	free_irq(gpio_to_irq(ifx_dev->gpio.srdy), (void *)ifx_dev);

	gpio_free(ifx_dev->gpio.srdy);
	gpio_free(ifx_dev->gpio.mrdy);
	gpio_free(ifx_dev->gpio.reset);
	gpio_free(ifx_dev->gpio.po);
	gpio_free(ifx_dev->gpio.reset_out);

	/* free allocations */
	ifx_spi_free_device(ifx_dev);

	saved_ifx_dev = NULL;
	return 0;
}

/**
 *	ifx_spi_spi_shutdown	-	called on SPI shutdown
 *	@spi: SPI device
 *
 *	No action needs to be taken here
 */

static void ifx_spi_spi_shutdown(struct spi_device *spi)
{
}

/*
 * various suspends and resumes have nothing to do
 * no hardware to save state for
 */

/**
 *	ifx_spi_spi_suspend	-	suspend SPI on system suspend
 *	@dev: device being suspended
 *
 *	Suspend the SPI side. No action needed on Intel MID platforms, may
 *	need extending for other systems.
 */
static int ifx_spi_spi_suspend(struct spi_device *spi, pm_message_t msg)
{
	return 0;
}

/**
 *	ifx_spi_spi_resume	-	resume SPI side on system resume
 *	@dev: device being suspended
 *
 *	Suspend the SPI side. No action needed on Intel MID platforms, may
 *	need extending for other systems.
 */
static int ifx_spi_spi_resume(struct spi_device *spi)
{
	return 0;
}

/**
 *	ifx_spi_pm_suspend	-	suspend modem on system suspend
 *	@dev: device being suspended
 *
 *	Suspend the modem. No action needed on Intel MID platforms, may
 *	need extending for other systems.
 */
static int ifx_spi_pm_suspend(struct device *dev)
{
	return 0;
}

/**
 *	ifx_spi_pm_resume	-	resume modem on system resume
 *	@dev: device being suspended
 *
 *	Allow the modem to resume. No action needed.
 *
 *	FIXME: do we need to reset anything here ?
 */
static int ifx_spi_pm_resume(struct device *dev)
{
	return 0;
}

/**
 *	ifx_spi_pm_runtime_resume	-	suspend modem
 *	@dev: device being suspended
 *
 *	Allow the modem to resume. No action needed.
 */
static int ifx_spi_pm_runtime_resume(struct device *dev)
{
	return 0;
}

/**
 *	ifx_spi_pm_runtime_suspend	-	suspend modem
 *	@dev: device being suspended
 *
 *	Allow the modem to suspend and thus suspend to continue up the
 *	device tree.
 */
static int ifx_spi_pm_runtime_suspend(struct device *dev)
{
	return 0;
}

/**
 *	ifx_spi_pm_runtime_idle		-	check if modem idle
 *	@dev: our device
 *
 *	Check conditions and queue runtime suspend if idle.
 */
static int ifx_spi_pm_runtime_idle(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct ifx_spi_device *ifx_dev = spi_get_drvdata(spi);

	if (!ifx_dev->power_status)
		pm_runtime_suspend(dev);

	return 0;
}

static const struct dev_pm_ops ifx_spi_pm = {
	.resume = ifx_spi_pm_resume,
	.suspend = ifx_spi_pm_suspend,
	.runtime_resume = ifx_spi_pm_runtime_resume,
	.runtime_suspend = ifx_spi_pm_runtime_suspend,
	.runtime_idle = ifx_spi_pm_runtime_idle
};

static const struct spi_device_id ifx_id_table[] = {
	{"ifx6160", 0},
	{"ifx6260", 0},
	{ }
};
MODULE_DEVICE_TABLE(spi, ifx_id_table);

/* spi operations */
static const struct spi_driver ifx_spi_driver = {
	.driver = {
		.name = DRVNAME,
		.bus = &spi_bus_type,
		.pm = &ifx_spi_pm,
		.owner = THIS_MODULE},
	.probe = ifx_spi_spi_probe,
	.shutdown = ifx_spi_spi_shutdown,
	.remove = __devexit_p(ifx_spi_spi_remove),
	.suspend = ifx_spi_spi_suspend,
	.resume = ifx_spi_spi_resume,
	.id_table = ifx_id_table
};

/**
 *	ifx_spi_exit	-	module exit
 *
 *	Unload the module.
 */

static void __exit ifx_spi_exit(void)
{
	/* unregister */
	tty_unregister_driver(tty_drv);
	spi_unregister_driver((void *)&ifx_spi_driver);
}

/**
 *	ifx_spi_init		-	module entry point
 *
 *	Initialise the SPI and tty interfaces for the IFX SPI driver
 *	We need to initialize upper-edge spi driver after the tty
 *	driver because otherwise the spi probe will race
 */

static int __init ifx_spi_init(void)
{
	int result;

	tty_drv = alloc_tty_driver(1);
	if (!tty_drv) {
		pr_err("%s: alloc_tty_driver failed", DRVNAME);
		return -ENOMEM;
	}

	tty_drv->magic = TTY_DRIVER_MAGIC;
	tty_drv->owner = THIS_MODULE;
	tty_drv->driver_name = DRVNAME;
	tty_drv->name = TTYNAME;
	tty_drv->minor_start = IFX_SPI_TTY_ID;
	tty_drv->num = 1;
	tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	tty_drv->subtype = SERIAL_TYPE_NORMAL;
	tty_drv->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_drv->init_termios = tty_std_termios;

	tty_set_operations(tty_drv, &ifx_spi_serial_ops);

	result = tty_register_driver(tty_drv);
	if (result) {
		pr_err("%s: tty_register_driver failed(%d)",
			DRVNAME, result);
		put_tty_driver(tty_drv);
		return result;
	}

	result = spi_register_driver((void *)&ifx_spi_driver);
	if (result) {
		pr_err("%s: spi_register_driver failed(%d)",
			DRVNAME, result);
		tty_unregister_driver(tty_drv);
	}
	return result;
}

module_init(ifx_spi_init);
module_exit(ifx_spi_exit);

MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("IFX6x60 spi driver");
MODULE_LICENSE("GPL");
MODULE_INFO(Version, "0.1-IFX6x60");
