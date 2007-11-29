/*
 * linux/drivers/mmc/card/sdio_uart.c - SDIO UART/GPS driver
 *
 * Based on drivers/serial/8250.c and drivers/serial/serial_core.c
 * by Russell King.
 *
 * Author:	Nicolas Pitre
 * Created:	June 15, 2007
 * Copyright:	MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

/*
 * Note: Although this driver assumes a 16550A-like UART implementation,
 * it is not possible to leverage the common 8250/16550 driver, nor the
 * core UART infrastructure, as they assumes direct access to the hardware
 * registers, often under a spinlock.  This is not possible in the SDIO
 * context as SDIO access functions must be able to sleep.
 *
 * Because we need to lock the SDIO host to ensure an exclusive access to
 * the card, we simply rely on that lock to also prevent and serialize
 * concurrent access to the same port.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/serial_reg.h>
#include <linux/circ_buf.h>
#include <linux/gfp.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>


#define UART_NR		8	/* Number of UARTs this driver can handle */


#define UART_XMIT_SIZE	PAGE_SIZE
#define WAKEUP_CHARS	256

#define circ_empty(circ)	((circ)->head == (circ)->tail)
#define circ_clear(circ)	((circ)->head = (circ)->tail = 0)

#define circ_chars_pending(circ) \
		(CIRC_CNT((circ)->head, (circ)->tail, UART_XMIT_SIZE))

#define circ_chars_free(circ) \
		(CIRC_SPACE((circ)->head, (circ)->tail, UART_XMIT_SIZE))


struct uart_icount {
	__u32	cts;
	__u32	dsr;
	__u32	rng;
	__u32	dcd;
	__u32	rx;
	__u32	tx;
	__u32	frame;
	__u32	overrun;
	__u32	parity;
	__u32	brk;
};

struct sdio_uart_port {
	struct kref		kref;
	struct tty_struct	*tty;
	unsigned int		index;
	unsigned int		opened;
	struct mutex		open_lock;
	struct sdio_func	*func;
	struct mutex		func_lock;
	struct task_struct	*in_sdio_uart_irq;
	unsigned int		regs_offset;
	struct circ_buf		xmit;
	spinlock_t		write_lock;
	struct uart_icount	icount;
	unsigned int		uartclk;
	unsigned int		mctrl;
	unsigned int		read_status_mask;
	unsigned int		ignore_status_mask;
	unsigned char		x_char;
	unsigned char           ier;
	unsigned char           lcr;
};

static struct sdio_uart_port *sdio_uart_table[UART_NR];
static DEFINE_SPINLOCK(sdio_uart_table_lock);

static int sdio_uart_add_port(struct sdio_uart_port *port)
{
	int index, ret = -EBUSY;

	kref_init(&port->kref);
	mutex_init(&port->open_lock);
	mutex_init(&port->func_lock);
	spin_lock_init(&port->write_lock);

	spin_lock(&sdio_uart_table_lock);
	for (index = 0; index < UART_NR; index++) {
		if (!sdio_uart_table[index]) {
			port->index = index;
			sdio_uart_table[index] = port;
			ret = 0;
			break;
		}
	}
	spin_unlock(&sdio_uart_table_lock);

	return ret;
}

static struct sdio_uart_port *sdio_uart_port_get(unsigned index)
{
	struct sdio_uart_port *port;

	if (index >= UART_NR)
		return NULL;

	spin_lock(&sdio_uart_table_lock);
	port = sdio_uart_table[index];
	if (port)
		kref_get(&port->kref);
	spin_unlock(&sdio_uart_table_lock);

	return port;
}

static void sdio_uart_port_destroy(struct kref *kref)
{
	struct sdio_uart_port *port =
		container_of(kref, struct sdio_uart_port, kref);
	kfree(port);
}

static void sdio_uart_port_put(struct sdio_uart_port *port)
{
	kref_put(&port->kref, sdio_uart_port_destroy);
}

static void sdio_uart_port_remove(struct sdio_uart_port *port)
{
	struct sdio_func *func;

	BUG_ON(sdio_uart_table[port->index] != port);

	spin_lock(&sdio_uart_table_lock);
	sdio_uart_table[port->index] = NULL;
	spin_unlock(&sdio_uart_table_lock);

	/*
	 * We're killing a port that potentially still is in use by
	 * the tty layer. Be careful to prevent any further access
	 * to the SDIO function and arrange for the tty layer to
	 * give up on that port ASAP.
	 * Beware: the lock ordering is critical.
	 */
	mutex_lock(&port->open_lock);
	mutex_lock(&port->func_lock);
	func = port->func;
	sdio_claim_host(func);
	port->func = NULL;
	mutex_unlock(&port->func_lock);
	if (port->opened)
		tty_hangup(port->tty);
	mutex_unlock(&port->open_lock);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	sdio_uart_port_put(port);
}

static int sdio_uart_claim_func(struct sdio_uart_port *port)
{
	mutex_lock(&port->func_lock);
	if (unlikely(!port->func)) {
		mutex_unlock(&port->func_lock);
		return -ENODEV;
	}
	if (likely(port->in_sdio_uart_irq != current))
		sdio_claim_host(port->func);
	mutex_unlock(&port->func_lock);
	return 0;
}

static inline void sdio_uart_release_func(struct sdio_uart_port *port)
{
	if (likely(port->in_sdio_uart_irq != current))
		sdio_release_host(port->func);
}

static inline unsigned int sdio_in(struct sdio_uart_port *port, int offset)
{
	unsigned char c;
	c = sdio_readb(port->func, port->regs_offset + offset, NULL);
	return c;
}

static inline void sdio_out(struct sdio_uart_port *port, int offset, int value)
{
	sdio_writeb(port->func, value, port->regs_offset + offset, NULL);
}

static unsigned int sdio_uart_get_mctrl(struct sdio_uart_port *port)
{
	unsigned char status;
	unsigned int ret;

	status = sdio_in(port, UART_MSR);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void sdio_uart_write_mctrl(struct sdio_uart_port *port, unsigned int mctrl)
{
	unsigned char mcr = 0;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	sdio_out(port, UART_MCR, mcr);
}

static inline void sdio_uart_update_mctrl(struct sdio_uart_port *port,
					  unsigned int set, unsigned int clear)
{
	unsigned int old;

	old = port->mctrl;
	port->mctrl = (old & ~clear) | set;
	if (old != port->mctrl)
		sdio_uart_write_mctrl(port, port->mctrl);
}

#define sdio_uart_set_mctrl(port, x)	sdio_uart_update_mctrl(port, x, 0)
#define sdio_uart_clear_mctrl(port, x)	sdio_uart_update_mctrl(port, 0, x)

static void sdio_uart_change_speed(struct sdio_uart_port *port,
				   struct ktermios *termios,
				   struct ktermios *old)
{
	unsigned char cval, fcr = 0;
	unsigned int baud, quot;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		cval = UART_LCR_WLEN5;
		break;
	case CS6:
		cval = UART_LCR_WLEN6;
		break;
	case CS7:
		cval = UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		cval = UART_LCR_WLEN8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		cval |= UART_LCR_STOP;
	if (termios->c_cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(termios->c_cflag & PARODD))
		cval |= UART_LCR_EPAR;

	for (;;) {
		baud = tty_termios_baud_rate(termios);
		if (baud == 0)
			baud = 9600;  /* Special case: B0 rate. */
		if (baud <= port->uartclk)
			break;
		/*
		 * Oops, the quotient was zero.  Try again with the old
		 * baud rate if possible, otherwise default to 9600.
		 */
		termios->c_cflag &= ~CBAUD;
		if (old) {
			termios->c_cflag |= old->c_cflag & CBAUD;
			old = NULL;
		} else
			termios->c_cflag |= B9600;
	}
	quot = (2 * port->uartclk + baud) / (2 * baud);

	if (baud < 2400)
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
	else
		fcr = UART_FCR_ENABLE_FIFO | UART_FCR_R_TRIG_10;

	port->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UART_LSR_BI;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	port->ier &= ~UART_IER_MSI;
	if ((termios->c_cflag & CRTSCTS) || !(termios->c_cflag & CLOCAL))
		port->ier |= UART_IER_MSI;

	port->lcr = cval;

	sdio_out(port, UART_IER, port->ier);
	sdio_out(port, UART_LCR, cval | UART_LCR_DLAB);
	sdio_out(port, UART_DLL, quot & 0xff);
	sdio_out(port, UART_DLM, quot >> 8);
	sdio_out(port, UART_LCR, cval);
	sdio_out(port, UART_FCR, fcr);

	sdio_uart_write_mctrl(port, port->mctrl);
}

static void sdio_uart_start_tx(struct sdio_uart_port *port)
{
	if (!(port->ier & UART_IER_THRI)) {
		port->ier |= UART_IER_THRI;
		sdio_out(port, UART_IER, port->ier);
	}
}

static void sdio_uart_stop_tx(struct sdio_uart_port *port)
{
	if (port->ier & UART_IER_THRI) {
		port->ier &= ~UART_IER_THRI;
		sdio_out(port, UART_IER, port->ier);
	}
}

static void sdio_uart_stop_rx(struct sdio_uart_port *port)
{
	port->ier &= ~UART_IER_RLSI;
	port->read_status_mask &= ~UART_LSR_DR;
	sdio_out(port, UART_IER, port->ier);
}

static void sdio_uart_receive_chars(struct sdio_uart_port *port, unsigned int *status)
{
	struct tty_struct *tty = port->tty;
	unsigned int ch, flag;
	int max_count = 256;

	do {
		ch = sdio_in(port, UART_RX);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(*status & (UART_LSR_BI | UART_LSR_PE |
				        UART_LSR_FE | UART_LSR_OE))) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				port->icount.brk++;
			} else if (*status & UART_LSR_PE)
				port->icount.parity++;
			else if (*status & UART_LSR_FE)
				port->icount.frame++;
			if (*status & UART_LSR_OE)
				port->icount.overrun++;

			/*
			 * Mask off conditions which should be ignored.
			 */
			*status &= port->read_status_mask;
			if (*status & UART_LSR_BI) {
				flag = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				flag = TTY_FRAME;
		}

		if ((*status & port->ignore_status_mask & ~UART_LSR_OE) == 0)
			tty_insert_flip_char(tty, ch, flag);

		/*
		 * Overrun is special.  Since it's reported immediately,
		 * it doesn't affect the current character.
		 */
		if (*status & ~port->ignore_status_mask & UART_LSR_OE)
			tty_insert_flip_char(tty, 0, TTY_OVERRUN);

		*status = sdio_in(port, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));
	tty_flip_buffer_push(tty);
}

static void sdio_uart_transmit_chars(struct sdio_uart_port *port)
{
	struct circ_buf *xmit = &port->xmit;
	int count;

	if (port->x_char) {
		sdio_out(port, UART_TX, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (circ_empty(xmit) || port->tty->stopped || port->tty->hw_stopped) {
		sdio_uart_stop_tx(port);
		return;
	}

	count = 16;
	do {
		sdio_out(port, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (circ_empty(xmit))
			break;
	} while (--count > 0);

	if (circ_chars_pending(xmit) < WAKEUP_CHARS)
		tty_wakeup(port->tty);

	if (circ_empty(xmit))
		sdio_uart_stop_tx(port);
}

static void sdio_uart_check_modem_status(struct sdio_uart_port *port)
{
	int status;

	status = sdio_in(port, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		port->icount.rng++;
	if (status & UART_MSR_DDSR)
		port->icount.dsr++;
	if (status & UART_MSR_DDCD)
		port->icount.dcd++;
	if (status & UART_MSR_DCTS) {
		port->icount.cts++;
		if (port->tty->termios->c_cflag & CRTSCTS) {
			int cts = (status & UART_MSR_CTS);
			if (port->tty->hw_stopped) {
				if (cts) {
					port->tty->hw_stopped = 0;
					sdio_uart_start_tx(port);
					tty_wakeup(port->tty);
				}
			} else {
				if (!cts) {
					port->tty->hw_stopped = 1;
					sdio_uart_stop_tx(port);
				}
			}
		}
	}
}

/*
 * This handles the interrupt from one port.
 */
static void sdio_uart_irq(struct sdio_func *func)
{
	struct sdio_uart_port *port = sdio_get_drvdata(func);
	unsigned int iir, lsr;

	/*
	 * In a few places sdio_uart_irq() is called directly instead of
	 * waiting for the actual interrupt to be raised and the SDIO IRQ
	 * thread scheduled in order to reduce latency.  However, some
	 * interaction with the tty core may end up calling us back
	 * (serial echo, flow control, etc.) through those same places
	 * causing undesirable effects.  Let's stop the recursion here.
	 */
	if (unlikely(port->in_sdio_uart_irq == current))
		return;

	iir = sdio_in(port, UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return;

	port->in_sdio_uart_irq = current;
	lsr = sdio_in(port, UART_LSR);
	if (lsr & UART_LSR_DR)
		sdio_uart_receive_chars(port, &lsr);
	sdio_uart_check_modem_status(port);
	if (lsr & UART_LSR_THRE)
		sdio_uart_transmit_chars(port);
	port->in_sdio_uart_irq = NULL;
}

static int sdio_uart_startup(struct sdio_uart_port *port)
{
	unsigned long page;
	int ret;

	/*
	 * Set the TTY IO error marker - we will only clear this
	 * once we have successfully opened the port.
	 */
	set_bit(TTY_IO_ERROR, &port->tty->flags);

	/* Initialise and allocate the transmit buffer. */
	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	port->xmit.buf = (unsigned char *)page;
	circ_clear(&port->xmit);

	ret = sdio_uart_claim_func(port);
	if (ret)
		goto err1;
	ret = sdio_enable_func(port->func);
	if (ret)
		goto err2;
	ret = sdio_claim_irq(port->func, sdio_uart_irq);
	if (ret)
		goto err3;

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in sdio_change_speed())
	 */
	sdio_out(port, UART_FCR, UART_FCR_ENABLE_FIFO);
	sdio_out(port, UART_FCR, UART_FCR_ENABLE_FIFO |
			UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	sdio_out(port, UART_FCR, 0);

	/*
	 * Clear the interrupt registers.
	 */
	(void) sdio_in(port, UART_LSR);
	(void) sdio_in(port, UART_RX);
	(void) sdio_in(port, UART_IIR);
	(void) sdio_in(port, UART_MSR);

	/*
	 * Now, initialize the UART
	 */
	sdio_out(port, UART_LCR, UART_LCR_WLEN8);

	port->ier = UART_IER_RLSI | UART_IER_RDI | UART_IER_RTOIE | UART_IER_UUE;
	port->mctrl = TIOCM_OUT2;

	sdio_uart_change_speed(port, port->tty->termios, NULL);

	if (port->tty->termios->c_cflag & CBAUD)
		sdio_uart_set_mctrl(port, TIOCM_RTS | TIOCM_DTR);

	if (port->tty->termios->c_cflag & CRTSCTS)
		if (!(sdio_uart_get_mctrl(port) & TIOCM_CTS))
			port->tty->hw_stopped = 1;

	clear_bit(TTY_IO_ERROR, &port->tty->flags);

	/* Kick the IRQ handler once while we're still holding the host lock */
	sdio_uart_irq(port->func);

	sdio_uart_release_func(port);
	return 0;

err3:
	sdio_disable_func(port->func);
err2:
	sdio_uart_release_func(port);
err1:
	free_page((unsigned long)port->xmit.buf);
	return ret;
}

static void sdio_uart_shutdown(struct sdio_uart_port *port)
{
	int ret;

	ret = sdio_uart_claim_func(port);
	if (ret)
		goto skip;

	sdio_uart_stop_rx(port);

	/* TODO: wait here for TX FIFO to drain */

	/* Turn off DTR and RTS early. */
	if (port->tty->termios->c_cflag & HUPCL)
		sdio_uart_clear_mctrl(port, TIOCM_DTR | TIOCM_RTS);

	 /* Disable interrupts from this port */
	sdio_release_irq(port->func);
	port->ier = 0;
	sdio_out(port, UART_IER, 0);

	sdio_uart_clear_mctrl(port, TIOCM_OUT2);

	/* Disable break condition and FIFOs. */
	port->lcr &= ~UART_LCR_SBC;
	sdio_out(port, UART_LCR, port->lcr);
	sdio_out(port, UART_FCR, UART_FCR_ENABLE_FIFO |
				 UART_FCR_CLEAR_RCVR |
				 UART_FCR_CLEAR_XMIT);
	sdio_out(port, UART_FCR, 0);

	sdio_disable_func(port->func);

	sdio_uart_release_func(port);

skip:
	/* Free the transmit buffer page. */
	free_page((unsigned long)port->xmit.buf);
}

static int sdio_uart_open (struct tty_struct *tty, struct file * filp)
{
	struct sdio_uart_port *port;
	int ret;

	port = sdio_uart_port_get(tty->index);
	if (!port)
		return -ENODEV;

	mutex_lock(&port->open_lock);

	/*
	 * Make sure not to mess up with a dead port
	 * which has not been closed yet.
	 */
	if (tty->driver_data && tty->driver_data != port) {
		mutex_unlock(&port->open_lock);
		sdio_uart_port_put(port);
		return -EBUSY;
	}

	if (!port->opened) {
		tty->driver_data = port;
		port->tty = tty;
		ret = sdio_uart_startup(port);
		if (ret) {
			tty->driver_data = NULL;
			port->tty = NULL;
			mutex_unlock(&port->open_lock);
			sdio_uart_port_put(port);
			return ret;
		}
	}
	port->opened++;
	mutex_unlock(&port->open_lock);
	return 0;
}

static void sdio_uart_close(struct tty_struct *tty, struct file * filp)
{
	struct sdio_uart_port *port = tty->driver_data;

	if (!port)
		return;

	mutex_lock(&port->open_lock);
	BUG_ON(!port->opened);

	/*
	 * This is messy.  The tty layer calls us even when open()
	 * returned an error.  Ignore this close request if tty->count
	 * is larger than port->count.
	 */
	if (tty->count > port->opened) {
		mutex_unlock(&port->open_lock);
		return;
	}

	if (--port->opened == 0) {
		tty->closing = 1;
		sdio_uart_shutdown(port);
		tty_ldisc_flush(tty);
		port->tty = NULL;
		tty->driver_data = NULL;
		tty->closing = 0;
	}
	mutex_unlock(&port->open_lock);
	sdio_uart_port_put(port);
}

static int sdio_uart_write(struct tty_struct * tty, const unsigned char *buf,
			   int count)
{
	struct sdio_uart_port *port = tty->driver_data;
	struct circ_buf *circ = &port->xmit;
	int c, ret = 0;

	if (!port->func)
		return -ENODEV;

	spin_lock(&port->write_lock);
	while (1) {
		c = CIRC_SPACE_TO_END(circ->head, circ->tail, UART_XMIT_SIZE);
		if (count < c)
			c = count;
		if (c <= 0)
			break;
		memcpy(circ->buf + circ->head, buf, c);
		circ->head = (circ->head + c) & (UART_XMIT_SIZE - 1);
		buf += c;
		count -= c;
		ret += c;
	}
	spin_unlock(&port->write_lock);

	if ( !(port->ier & UART_IER_THRI)) {
		int err = sdio_uart_claim_func(port);
		if (!err) {
			sdio_uart_start_tx(port);
			sdio_uart_irq(port->func);
			sdio_uart_release_func(port);
		} else
			ret = err;
	}

	return ret;
}

static int sdio_uart_write_room(struct tty_struct *tty)
{
	struct sdio_uart_port *port = tty->driver_data;
	return port ? circ_chars_free(&port->xmit) : 0;
}

static int sdio_uart_chars_in_buffer(struct tty_struct *tty)
{
	struct sdio_uart_port *port = tty->driver_data;
	return port ? circ_chars_pending(&port->xmit) : 0;
}

static void sdio_uart_send_xchar(struct tty_struct *tty, char ch)
{
	struct sdio_uart_port *port = tty->driver_data;

	port->x_char = ch;
	if (ch && !(port->ier & UART_IER_THRI)) {
		if (sdio_uart_claim_func(port) != 0)
			return;
		sdio_uart_start_tx(port);
		sdio_uart_irq(port->func);
		sdio_uart_release_func(port);
	}
}

static void sdio_uart_throttle(struct tty_struct *tty)
{
	struct sdio_uart_port *port = tty->driver_data;

	if (!I_IXOFF(tty) && !(tty->termios->c_cflag & CRTSCTS))
		return;

	if (sdio_uart_claim_func(port) != 0)
		return;

	if (I_IXOFF(tty)) {
		port->x_char = STOP_CHAR(tty);
		sdio_uart_start_tx(port);
	}

	if (tty->termios->c_cflag & CRTSCTS)
		sdio_uart_clear_mctrl(port, TIOCM_RTS);

	sdio_uart_irq(port->func);
	sdio_uart_release_func(port);
}

static void sdio_uart_unthrottle(struct tty_struct *tty)
{
	struct sdio_uart_port *port = tty->driver_data;

	if (!I_IXOFF(tty) && !(tty->termios->c_cflag & CRTSCTS))
		return;

	if (sdio_uart_claim_func(port) != 0)
		return;

	if (I_IXOFF(tty)) {
		if (port->x_char) {
			port->x_char = 0;
		} else {
			port->x_char = START_CHAR(tty);
			sdio_uart_start_tx(port);
		}
	}

	if (tty->termios->c_cflag & CRTSCTS)
		sdio_uart_set_mctrl(port, TIOCM_RTS);

	sdio_uart_irq(port->func);
	sdio_uart_release_func(port);
}

static void sdio_uart_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct sdio_uart_port *port = tty->driver_data;
	unsigned int cflag = tty->termios->c_cflag;

#define RELEVANT_IFLAG(iflag)   ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	if ((cflag ^ old_termios->c_cflag) == 0 &&
	    RELEVANT_IFLAG(tty->termios->c_iflag ^ old_termios->c_iflag) == 0)
		return;

	if (sdio_uart_claim_func(port) != 0)
		return;

	sdio_uart_change_speed(port, tty->termios, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD))
		sdio_uart_clear_mctrl(port, TIOCM_RTS | TIOCM_DTR);

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		unsigned int mask = TIOCM_DTR;
		if (!(cflag & CRTSCTS) || !test_bit(TTY_THROTTLED, &tty->flags))
			mask |= TIOCM_RTS;
		sdio_uart_set_mctrl(port, mask);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) && !(cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		sdio_uart_start_tx(port);
	}

	/* Handle turning on CRTSCTS */
	if (!(old_termios->c_cflag & CRTSCTS) && (cflag & CRTSCTS)) {
		if (!(sdio_uart_get_mctrl(port) & TIOCM_CTS)) {
			tty->hw_stopped = 1;
			sdio_uart_stop_tx(port);
		}
	}

	sdio_uart_release_func(port);
}

static void sdio_uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct sdio_uart_port *port = tty->driver_data;

	if (sdio_uart_claim_func(port) != 0)
		return;

	if (break_state == -1)
		port->lcr |= UART_LCR_SBC;
	else
		port->lcr &= ~UART_LCR_SBC;
	sdio_out(port, UART_LCR, port->lcr);

	sdio_uart_release_func(port);
}

static int sdio_uart_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct sdio_uart_port *port = tty->driver_data;
	int result;

	result = sdio_uart_claim_func(port);
	if (!result) {
		result = port->mctrl | sdio_uart_get_mctrl(port);
		sdio_uart_release_func(port);
	}

	return result;
}

static int sdio_uart_tiocmset(struct tty_struct *tty, struct file *file,
			      unsigned int set, unsigned int clear)
{
	struct sdio_uart_port *port = tty->driver_data;
	int result;

	result =sdio_uart_claim_func(port);
	if(!result) {
		sdio_uart_update_mctrl(port, set, clear);
		sdio_uart_release_func(port);
	}

	return result;
}

static int sdio_uart_read_proc(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	int i, len = 0;
	off_t begin = 0;

	len += sprintf(page, "serinfo:1.0 driver%s%s revision:%s\n",
		       "", "", "");
	for (i = 0; i < UART_NR && len < PAGE_SIZE - 96; i++) {
		struct sdio_uart_port *port = sdio_uart_port_get(i);
		if (port) {
			len += sprintf(page+len, "%d: uart:SDIO", i);
			if(capable(CAP_SYS_ADMIN)) {
				len += sprintf(page + len, " tx:%d rx:%d",
					       port->icount.tx, port->icount.rx);
				if (port->icount.frame)
					len += sprintf(page + len, " fe:%d",
						       port->icount.frame);
				if (port->icount.parity)
					len += sprintf(page + len, " pe:%d",
						       port->icount.parity);
				if (port->icount.brk)
					len += sprintf(page + len, " brk:%d",
						       port->icount.brk);
				if (port->icount.overrun)
					len += sprintf(page + len, " oe:%d",
						       port->icount.overrun);
				if (port->icount.cts)
					len += sprintf(page + len, " cts:%d",
						       port->icount.cts);
				if (port->icount.dsr)
					len += sprintf(page + len, " dsr:%d",
						       port->icount.dsr);
				if (port->icount.rng)
					len += sprintf(page + len, " rng:%d",
						       port->icount.rng);
				if (port->icount.dcd)
					len += sprintf(page + len, " dcd:%d",
						       port->icount.dcd);
			}
			strcat(page, "\n");
			len++;
			sdio_uart_port_put(port);
		}

		if (len + begin > off + count)
			goto done;
		if (len + begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;

done:
	if (off >= len + begin)
		return 0;
	*start = page + (off - begin);
	return (count < begin + len - off) ? count : (begin + len - off);
}

static const struct tty_operations sdio_uart_ops = {
	.open			= sdio_uart_open,
	.close			= sdio_uart_close,
	.write			= sdio_uart_write,
	.write_room		= sdio_uart_write_room,
	.chars_in_buffer	= sdio_uart_chars_in_buffer,
	.send_xchar		= sdio_uart_send_xchar,
	.throttle		= sdio_uart_throttle,
	.unthrottle		= sdio_uart_unthrottle,
	.set_termios		= sdio_uart_set_termios,
	.break_ctl		= sdio_uart_break_ctl,
	.tiocmget		= sdio_uart_tiocmget,
	.tiocmset		= sdio_uart_tiocmset,
	.read_proc		= sdio_uart_read_proc,
};

static struct tty_driver *sdio_uart_tty_driver;

static int sdio_uart_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct sdio_uart_port *port;
	int ret;

	port = kzalloc(sizeof(struct sdio_uart_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	if (func->class == SDIO_CLASS_UART) {
		printk(KERN_WARNING "%s: need info on UART class basic setup\n",
		       sdio_func_id(func));
		kfree(port);
		return -ENOSYS;
	} else if (func->class == SDIO_CLASS_GPS) {
		/*
		 * We need tuple 0x91.  It contains SUBTPL_SIOREG
		 * and SUBTPL_RCVCAPS.
		 */
		struct sdio_func_tuple *tpl;
		for (tpl = func->tuples; tpl; tpl = tpl->next) {
			if (tpl->code != 0x91)
				continue;
			if (tpl->size < 10)
				continue;
			if (tpl->data[1] == 0)  /* SUBTPL_SIOREG */
				break;
		}
		if (!tpl) {
			printk(KERN_WARNING
			       "%s: can't find tuple 0x91 subtuple 0 (SUBTPL_SIOREG) for GPS class\n",
			       sdio_func_id(func));
			kfree(port);
			return -EINVAL;
		}
		printk(KERN_DEBUG "%s: Register ID = 0x%02x, Exp ID = 0x%02x\n",
		       sdio_func_id(func), tpl->data[2], tpl->data[3]);
		port->regs_offset = (tpl->data[4] << 0) |
				    (tpl->data[5] << 8) |
				    (tpl->data[6] << 16);
		printk(KERN_DEBUG "%s: regs offset = 0x%x\n",
		       sdio_func_id(func), port->regs_offset);
		port->uartclk = tpl->data[7] * 115200;
		if (port->uartclk == 0)
			port->uartclk = 115200;
		printk(KERN_DEBUG "%s: clk %d baudcode %u 4800-div %u\n",
		       sdio_func_id(func), port->uartclk,
		       tpl->data[7], tpl->data[8] | (tpl->data[9] << 8));
	} else {
		kfree(port);
		return -EINVAL;
	}

	port->func = func;
	sdio_set_drvdata(func, port);

	ret = sdio_uart_add_port(port);
	if (ret) {
		kfree(port);
	} else {
		struct device *dev;
		dev = tty_register_device(sdio_uart_tty_driver, port->index, &func->dev);
		if (IS_ERR(dev)) {
			sdio_uart_port_remove(port);
			ret = PTR_ERR(dev);
		}
	}

	return ret;
}

static void sdio_uart_remove(struct sdio_func *func)
{
	struct sdio_uart_port *port = sdio_get_drvdata(func);

	tty_unregister_device(sdio_uart_tty_driver, port->index);
	sdio_uart_port_remove(port);
}

static const struct sdio_device_id sdio_uart_ids[] = {
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_UART)		},
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_GPS)		},
	{ /* end: all zeroes */				},
};

MODULE_DEVICE_TABLE(sdio, sdio_uart_ids);

static struct sdio_driver sdio_uart_driver = {
	.probe		= sdio_uart_probe,
	.remove		= sdio_uart_remove,
	.name		= "sdio_uart",
	.id_table	= sdio_uart_ids,
};

static int __init sdio_uart_init(void)
{
	int ret;
	struct tty_driver *tty_drv;

	sdio_uart_tty_driver = tty_drv = alloc_tty_driver(UART_NR);
	if (!tty_drv)
		return -ENOMEM;

	tty_drv->owner = THIS_MODULE;
	tty_drv->driver_name = "sdio_uart";
	tty_drv->name =   "ttySDIO";
	tty_drv->major = 0;  /* dynamically allocated */
	tty_drv->minor_start = 0;
	tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	tty_drv->subtype = SERIAL_TYPE_NORMAL;
	tty_drv->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_drv->init_termios = tty_std_termios;
	tty_drv->init_termios.c_cflag = B4800 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_drv->init_termios.c_ispeed = 4800;
	tty_drv->init_termios.c_ospeed = 4800;
	tty_set_operations(tty_drv, &sdio_uart_ops);

	ret = tty_register_driver(tty_drv);
	if (ret)
		goto err1;

	ret = sdio_register_driver(&sdio_uart_driver);
	if (ret)
		goto err2;

	return 0;

err2:
	tty_unregister_driver(tty_drv);
err1:
	put_tty_driver(tty_drv);
	return ret;
}

static void __exit sdio_uart_exit(void)
{
	sdio_unregister_driver(&sdio_uart_driver);
	tty_unregister_driver(sdio_uart_tty_driver);
	put_tty_driver(sdio_uart_tty_driver);
}

module_init(sdio_uart_init);
module_exit(sdio_uart_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_LICENSE("GPL");
