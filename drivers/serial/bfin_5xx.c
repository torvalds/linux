/*
 * File:         drivers/serial/bfin_5xx.c
 * Based on:     Based on drivers/serial/sa1100.c
 * Author:       Aubrey Li <aubrey.li@analog.com>
 *
 * Created:
 * Description:  Driver for blackfin 5xx serial ports
 *
 * Rev:          $Id: bfin_5xx.c,v 1.19 2006/09/24 02:33:53 aubrey Exp $
 *
 * Modified:
 *               Copyright 2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#if defined(CONFIG_SERIAL_BFIN_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>

#include <asm/gpio.h>
#include <asm/mach/bfin_serial_5xx.h>

#ifdef CONFIG_SERIAL_BFIN_DMA
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/cacheflush.h>
#endif

/* UART name and device definitions */
#define BFIN_SERIAL_NAME	"ttyBF"
#define BFIN_SERIAL_MAJOR	204
#define BFIN_SERIAL_MINOR	64

/*
 * Setup for console. Argument comes from the menuconfig
 */
#define DMA_RX_XCOUNT		512
#define DMA_RX_YCOUNT		(PAGE_SIZE / DMA_RX_XCOUNT)

#define DMA_RX_FLUSH_JIFFIES	5

#ifdef CONFIG_SERIAL_BFIN_DMA
static void bfin_serial_dma_tx_chars(struct bfin_serial_port *uart);
#else
static void bfin_serial_do_work(struct work_struct *work);
static void bfin_serial_tx_chars(struct bfin_serial_port *uart);
static void local_put_char(struct bfin_serial_port *uart, char ch);
#endif

static void bfin_serial_mctrl_check(struct bfin_serial_port *uart);

/*
 * interrupts are disabled on entry
 */
static void bfin_serial_stop_tx(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

#ifdef CONFIG_SERIAL_BFIN_DMA
	disable_dma(uart->tx_dma_channel);
#else
	unsigned short ier;

	ier = UART_GET_IER(uart);
	ier &= ~ETBEI;
	UART_PUT_IER(uart, ier);
#endif
}

/*
 * port is locked and interrupts are disabled
 */
static void bfin_serial_start_tx(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

#ifdef CONFIG_SERIAL_BFIN_DMA
	bfin_serial_dma_tx_chars(uart);
#else
	unsigned short ier;
	ier = UART_GET_IER(uart);
	ier |= ETBEI;
	UART_PUT_IER(uart, ier);
	bfin_serial_tx_chars(uart);
#endif
}

/*
 * Interrupts are enabled
 */
static void bfin_serial_stop_rx(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned short ier;

	ier = UART_GET_IER(uart);
	ier &= ~ERBFI;
	UART_PUT_IER(uart, ier);
}

/*
 * Set the modem control timer to fire immediately.
 */
static void bfin_serial_enable_ms(struct uart_port *port)
{
}

#ifdef CONFIG_SERIAL_BFIN_PIO
static void local_put_char(struct bfin_serial_port *uart, char ch)
{
	unsigned short status;
	int flags = 0;

	spin_lock_irqsave(&uart->port.lock, flags);

	do {
		status = UART_GET_LSR(uart);
	} while (!(status & THRE));

	UART_PUT_CHAR(uart, ch);
	SSYNC();

	spin_unlock_irqrestore(&uart->port.lock, flags);
}

static void bfin_serial_rx_chars(struct bfin_serial_port *uart)
{
	struct tty_struct *tty = uart->port.info?uart->port.info->tty:0;
	unsigned int status, ch, flg;
#ifdef BF533_FAMILY
	static int in_break = 0;
#endif

	status = UART_GET_LSR(uart);
 	ch = UART_GET_CHAR(uart);
 	uart->port.icount.rx++;

#ifdef BF533_FAMILY
	/* The BF533 family of processors have a nice misbehavior where
	 * they continuously generate characters for a "single" break.
	 * We have to basically ignore this flood until the "next" valid
	 * character comes across.  All other Blackfin families operate
	 * properly though.
	 */
	if (in_break) {
		if (ch != 0) {
			in_break = 0;
			ch = UART_GET_CHAR(uart);
		}
		return;
	}
#endif

	if (status & BI) {
#ifdef BF533_FAMILY
		in_break = 1;
#endif
		uart->port.icount.brk++;
		if (uart_handle_break(&uart->port))
			goto ignore_char;
		flg = TTY_BREAK;
	} else if (status & PE) {
		flg = TTY_PARITY;
		uart->port.icount.parity++;
	} else if (status & OE) {
		flg = TTY_OVERRUN;
		uart->port.icount.overrun++;
	} else if (status & FE) {
		flg = TTY_FRAME;
		uart->port.icount.frame++;
	} else
		flg = TTY_NORMAL;

	if (uart_handle_sysrq_char(&uart->port, ch))
		goto ignore_char;
	if (tty)
		uart_insert_char(&uart->port, status, 2, ch, flg);

ignore_char:
	if (tty)
		tty_flip_buffer_push(tty);
}

static void bfin_serial_tx_chars(struct bfin_serial_port *uart)
{
	struct circ_buf *xmit = &uart->port.info->xmit;

	if (uart->port.x_char) {
		UART_PUT_CHAR(uart, uart->port.x_char);
		uart->port.icount.tx++;
		uart->port.x_char = 0;
		return;
	}
	/*
	 * Check the modem control lines before
	 * transmitting anything.
	 */
	bfin_serial_mctrl_check(uart);

	if (uart_circ_empty(xmit) || uart_tx_stopped(&uart->port)) {
		bfin_serial_stop_tx(&uart->port);
		return;
	}

	local_put_char(uart, xmit->buf[xmit->tail]);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	uart->port.icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&uart->port);

	if (uart_circ_empty(xmit))
		bfin_serial_stop_tx(&uart->port);
}

static irqreturn_t bfin_serial_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;
	unsigned short status;

	spin_lock(&uart->port.lock);
	status = UART_GET_IIR(uart);
	do {
		if ((status & IIR_STATUS) == IIR_TX_READY)
			bfin_serial_tx_chars(uart);
		if ((status & IIR_STATUS) == IIR_RX_READY)
			bfin_serial_rx_chars(uart);
		status = UART_GET_IIR(uart);
	} while (status & (IIR_TX_READY | IIR_RX_READY));
	spin_unlock(&uart->port.lock);
	return IRQ_HANDLED;
}

static void bfin_serial_do_work(struct work_struct *work)
{
	struct bfin_serial_port *uart = container_of(work, struct bfin_serial_port, cts_workqueue);

	bfin_serial_mctrl_check(uart);
}

#endif

#ifdef CONFIG_SERIAL_BFIN_DMA
static void bfin_serial_dma_tx_chars(struct bfin_serial_port *uart)
{
	struct circ_buf *xmit = &uart->port.info->xmit;
	unsigned short ier;
	int flags = 0;

	if (!uart->tx_done)
		return;

	uart->tx_done = 0;

	if (uart->port.x_char) {
		UART_PUT_CHAR(uart, uart->port.x_char);
		uart->port.icount.tx++;
		uart->port.x_char = 0;
		uart->tx_done = 1;
		return;
	}
	/*
	 * Check the modem control lines before
	 * transmitting anything.
	 */
	bfin_serial_mctrl_check(uart);

	if (uart_circ_empty(xmit) || uart_tx_stopped(&uart->port)) {
		bfin_serial_stop_tx(&uart->port);
		uart->tx_done = 1;
		return;
	}

	spin_lock_irqsave(&uart->port.lock, flags);
	uart->tx_count = CIRC_CNT(xmit->head, xmit->tail, UART_XMIT_SIZE);
	if (uart->tx_count > (UART_XMIT_SIZE - xmit->tail))
		uart->tx_count = UART_XMIT_SIZE - xmit->tail;
	blackfin_dcache_flush_range((unsigned long)(xmit->buf+xmit->tail),
					(unsigned long)(xmit->buf+xmit->tail+uart->tx_count));
	set_dma_config(uart->tx_dma_channel,
		set_bfin_dma_config(DIR_READ, DMA_FLOW_STOP,
			INTR_ON_BUF,
			DIMENSION_LINEAR,
			DATA_SIZE_8));
	set_dma_start_addr(uart->tx_dma_channel, (unsigned long)(xmit->buf+xmit->tail));
	set_dma_x_count(uart->tx_dma_channel, uart->tx_count);
	set_dma_x_modify(uart->tx_dma_channel, 1);
	enable_dma(uart->tx_dma_channel);
	ier = UART_GET_IER(uart);
	ier |= ETBEI;
	UART_PUT_IER(uart, ier);
	spin_unlock_irqrestore(&uart->port.lock, flags);
}

static void bfin_serial_dma_rx_chars(struct bfin_serial_port * uart)
{
	struct tty_struct *tty = uart->port.info->tty;
	int i, flg, status;

	status = UART_GET_LSR(uart);
	uart->port.icount.rx += CIRC_CNT(uart->rx_dma_buf.head, uart->rx_dma_buf.tail, UART_XMIT_SIZE);;

	if (status & BI) {
		uart->port.icount.brk++;
		if (uart_handle_break(&uart->port))
			goto dma_ignore_char;
		flg = TTY_BREAK;
	} else if (status & PE) {
		flg = TTY_PARITY;
		uart->port.icount.parity++;
	} else if (status & OE) {
		flg = TTY_OVERRUN;
		uart->port.icount.overrun++;
	} else if (status & FE) {
		flg = TTY_FRAME;
		uart->port.icount.frame++;
	} else
		flg = TTY_NORMAL;

	for (i = uart->rx_dma_buf.head; i < uart->rx_dma_buf.tail; i++) {
		if (uart_handle_sysrq_char(&uart->port, uart->rx_dma_buf.buf[i]))
			goto dma_ignore_char;
		uart_insert_char(&uart->port, status, 2, uart->rx_dma_buf.buf[i], flg);
	}
dma_ignore_char:
	tty_flip_buffer_push(tty);
}

void bfin_serial_rx_dma_timeout(struct bfin_serial_port *uart)
{
	int x_pos, pos;
	int flags = 0;

	bfin_serial_dma_tx_chars(uart);

	spin_lock_irqsave(&uart->port.lock, flags);
	x_pos = DMA_RX_XCOUNT - get_dma_curr_xcount(uart->rx_dma_channel);
	if (x_pos == DMA_RX_XCOUNT)
		x_pos = 0;

	pos = uart->rx_dma_nrows * DMA_RX_XCOUNT + x_pos;

	if (pos>uart->rx_dma_buf.tail) {
		uart->rx_dma_buf.tail = pos;
		bfin_serial_dma_rx_chars(uart);
		uart->rx_dma_buf.head = uart->rx_dma_buf.tail;
	}
	spin_unlock_irqrestore(&uart->port.lock, flags);
	uart->rx_dma_timer.expires = jiffies + DMA_RX_FLUSH_JIFFIES;
	add_timer(&(uart->rx_dma_timer));
}

static irqreturn_t bfin_serial_dma_tx_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;
	struct circ_buf *xmit = &uart->port.info->xmit;
	unsigned short ier;

	spin_lock(&uart->port.lock);
	if (!(get_dma_curr_irqstat(uart->tx_dma_channel)&DMA_RUN)) {
		clear_dma_irqstat(uart->tx_dma_channel);
		disable_dma(uart->tx_dma_channel);
		ier = UART_GET_IER(uart);
		ier &= ~ETBEI;
		UART_PUT_IER(uart, ier);
		xmit->tail = (xmit->tail+uart->tx_count) &(UART_XMIT_SIZE -1);
		uart->port.icount.tx+=uart->tx_count;

		if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
			uart_write_wakeup(&uart->port);

		if (uart_circ_empty(xmit))
			bfin_serial_stop_tx(&uart->port);
		uart->tx_done = 1;
	}

	spin_unlock(&uart->port.lock);
	return IRQ_HANDLED;
}

static irqreturn_t bfin_serial_dma_rx_int(int irq, void *dev_id)
{
	struct bfin_serial_port *uart = dev_id;
	unsigned short irqstat;

	uart->rx_dma_nrows++;
	if (uart->rx_dma_nrows == DMA_RX_YCOUNT) {
		uart->rx_dma_nrows = 0;
		uart->rx_dma_buf.tail = DMA_RX_XCOUNT*DMA_RX_YCOUNT;
		bfin_serial_dma_rx_chars(uart);
		uart->rx_dma_buf.head = uart->rx_dma_buf.tail = 0;
	}
	spin_lock(&uart->port.lock);
	irqstat = get_dma_curr_irqstat(uart->rx_dma_channel);
	clear_dma_irqstat(uart->rx_dma_channel);

	spin_unlock(&uart->port.lock);
	return IRQ_HANDLED;
}
#endif

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static unsigned int bfin_serial_tx_empty(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned short lsr;

	lsr = UART_GET_LSR(uart);
	if (lsr & TEMT)
		return TIOCSER_TEMT;
	else
		return 0;
}

static unsigned int bfin_serial_get_mctrl(struct uart_port *port)
{
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	if (uart->cts_pin < 0)
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;

	if (gpio_get_value(uart->cts_pin))
		return TIOCM_DSR | TIOCM_CAR;
	else
#endif
		return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void bfin_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	if (uart->rts_pin < 0)
		return;

	if (mctrl & TIOCM_RTS)
		gpio_set_value(uart->rts_pin, 0);
	else
		gpio_set_value(uart->rts_pin, 1);
#endif
}

/*
 * Handle any change of modem status signal since we were last called.
 */
static void bfin_serial_mctrl_check(struct bfin_serial_port *uart)
{
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	unsigned int status;
# ifdef CONFIG_SERIAL_BFIN_DMA
	struct uart_info *info = uart->port.info;
	struct tty_struct *tty = info->tty;

	status = bfin_serial_get_mctrl(&uart->port);
	if (!(status & TIOCM_CTS)) {
		tty->hw_stopped = 1;
	} else {
		tty->hw_stopped = 0;
	}
# else
	status = bfin_serial_get_mctrl(&uart->port);
	uart_handle_cts_change(&uart->port, status & TIOCM_CTS);
	if (!(status & TIOCM_CTS))
		schedule_work(&uart->cts_workqueue);
# endif
#endif
}

/*
 * Interrupts are always disabled.
 */
static void bfin_serial_break_ctl(struct uart_port *port, int break_state)
{
}

static int bfin_serial_startup(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

#ifdef CONFIG_SERIAL_BFIN_DMA
	dma_addr_t dma_handle;

	if (request_dma(uart->rx_dma_channel, "BFIN_UART_RX") < 0) {
		printk(KERN_NOTICE "Unable to attach Blackfin UART RX DMA channel\n");
		return -EBUSY;
	}

	if (request_dma(uart->tx_dma_channel, "BFIN_UART_TX") < 0) {
		printk(KERN_NOTICE "Unable to attach Blackfin UART TX DMA channel\n");
		free_dma(uart->rx_dma_channel);
		return -EBUSY;
	}

	set_dma_callback(uart->rx_dma_channel, bfin_serial_dma_rx_int, uart);
	set_dma_callback(uart->tx_dma_channel, bfin_serial_dma_tx_int, uart);

	uart->rx_dma_buf.buf = (unsigned char *)dma_alloc_coherent(NULL, PAGE_SIZE, &dma_handle, GFP_DMA);
	uart->rx_dma_buf.head = 0;
	uart->rx_dma_buf.tail = 0;
	uart->rx_dma_nrows = 0;

	set_dma_config(uart->rx_dma_channel,
		set_bfin_dma_config(DIR_WRITE, DMA_FLOW_AUTO,
				INTR_ON_ROW, DIMENSION_2D,
				DATA_SIZE_8));
	set_dma_x_count(uart->rx_dma_channel, DMA_RX_XCOUNT);
	set_dma_x_modify(uart->rx_dma_channel, 1);
	set_dma_y_count(uart->rx_dma_channel, DMA_RX_YCOUNT);
	set_dma_y_modify(uart->rx_dma_channel, 1);
	set_dma_start_addr(uart->rx_dma_channel, (unsigned long)uart->rx_dma_buf.buf);
	enable_dma(uart->rx_dma_channel);

	uart->rx_dma_timer.data = (unsigned long)(uart);
	uart->rx_dma_timer.function = (void *)bfin_serial_rx_dma_timeout;
	uart->rx_dma_timer.expires = jiffies + DMA_RX_FLUSH_JIFFIES;
	add_timer(&(uart->rx_dma_timer));
#else
	if (request_irq
	    (uart->port.irq, bfin_serial_int, IRQF_DISABLED,
	     "BFIN_UART_RX", uart)) {
		printk(KERN_NOTICE "Unable to attach BlackFin UART RX interrupt\n");
		return -EBUSY;
	}

	if (request_irq
	    (uart->port.irq+1, bfin_serial_int, IRQF_DISABLED,
	     "BFIN_UART_TX", uart)) {
		printk(KERN_NOTICE "Unable to attach BlackFin UART TX interrupt\n");
		free_irq(uart->port.irq, uart);
		return -EBUSY;
	}
#endif
	UART_PUT_IER(uart, UART_GET_IER(uart) | ERBFI);
	return 0;
}

static void bfin_serial_shutdown(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

#ifdef CONFIG_SERIAL_BFIN_DMA
	disable_dma(uart->tx_dma_channel);
	free_dma(uart->tx_dma_channel);
	disable_dma(uart->rx_dma_channel);
	free_dma(uart->rx_dma_channel);
	del_timer(&(uart->rx_dma_timer));
#else
	free_irq(uart->port.irq, uart);
	free_irq(uart->port.irq+1, uart);
#endif
}

static void
bfin_serial_set_termios(struct uart_port *port, struct ktermios *termios,
		   struct ktermios *old)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	unsigned long flags;
	unsigned int baud, quot;
	unsigned short val, ier, lsr, lcr = 0;

	switch (termios->c_cflag & CSIZE) {
	case CS8:
		lcr = WLS(8);
		break;
	case CS7:
		lcr = WLS(7);
		break;
	case CS6:
		lcr = WLS(6);
		break;
	case CS5:
		lcr = WLS(5);
		break;
	default:
		printk(KERN_ERR "%s: word lengh not supported\n",
			__FUNCTION__);
	}

	if (termios->c_cflag & CSTOPB)
		lcr |= STB;
	if (termios->c_cflag & PARENB) {
		lcr |= PEN;
		if (!(termios->c_cflag & PARODD))
			lcr |= EPS;
	}

	/* These controls are not implemented for this port */
	termios->c_iflag |= INPCK | BRKINT | PARMRK;
	termios->c_iflag &= ~(IGNPAR | IGNBRK);

	/* These controls are not implemented for this port */
	termios->c_iflag |= INPCK | BRKINT | PARMRK;
	termios->c_iflag &= ~(IGNPAR | IGNBRK);

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);
	spin_lock_irqsave(&uart->port.lock, flags);

	do {
		lsr = UART_GET_LSR(uart);
	} while (!(lsr & TEMT));

	/* Disable UART */
	ier = UART_GET_IER(uart);
	UART_PUT_IER(uart, 0);

	/* Set DLAB in LCR to Access DLL and DLH */
	val = UART_GET_LCR(uart);
	val |= DLAB;
	UART_PUT_LCR(uart, val);
	SSYNC();

	UART_PUT_DLL(uart, quot & 0xFF);
	SSYNC();
	UART_PUT_DLH(uart, (quot >> 8) & 0xFF);
	SSYNC();

	/* Clear DLAB in LCR to Access THR RBR IER */
	val = UART_GET_LCR(uart);
	val &= ~DLAB;
	UART_PUT_LCR(uart, val);
	SSYNC();

	UART_PUT_LCR(uart, lcr);

	/* Enable UART */
	UART_PUT_IER(uart, ier);

	val = UART_GET_GCTL(uart);
	val |= UCEN;
	UART_PUT_GCTL(uart, val);

	spin_unlock_irqrestore(&uart->port.lock, flags);
}

static const char *bfin_serial_type(struct uart_port *port)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

	return uart->port.type == PORT_BFIN ? "BFIN-UART" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void bfin_serial_release_port(struct uart_port *port)
{
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int bfin_serial_request_port(struct uart_port *port)
{
	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void bfin_serial_config_port(struct uart_port *port, int flags)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;

	if (flags & UART_CONFIG_TYPE &&
	    bfin_serial_request_port(&uart->port) == 0)
		uart->port.type = PORT_BFIN;
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 * The only change we allow are to the flags and type, and
 * even then only between PORT_BFIN and PORT_UNKNOWN
 */
static int
bfin_serial_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return 0;
}

static struct uart_ops bfin_serial_pops = {
	.tx_empty	= bfin_serial_tx_empty,
	.set_mctrl	= bfin_serial_set_mctrl,
	.get_mctrl	= bfin_serial_get_mctrl,
	.stop_tx	= bfin_serial_stop_tx,
	.start_tx	= bfin_serial_start_tx,
	.stop_rx	= bfin_serial_stop_rx,
	.enable_ms	= bfin_serial_enable_ms,
	.break_ctl	= bfin_serial_break_ctl,
	.startup	= bfin_serial_startup,
	.shutdown	= bfin_serial_shutdown,
	.set_termios	= bfin_serial_set_termios,
	.type		= bfin_serial_type,
	.release_port	= bfin_serial_release_port,
	.request_port	= bfin_serial_request_port,
	.config_port	= bfin_serial_config_port,
	.verify_port	= bfin_serial_verify_port,
};

static void __init bfin_serial_init_ports(void)
{
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0; i < nr_ports; i++) {
		bfin_serial_ports[i].port.uartclk   = get_sclk();
		bfin_serial_ports[i].port.ops       = &bfin_serial_pops;
		bfin_serial_ports[i].port.line      = i;
		bfin_serial_ports[i].port.iotype    = UPIO_MEM;
		bfin_serial_ports[i].port.membase   =
			(void __iomem *)bfin_serial_resource[i].uart_base_addr;
		bfin_serial_ports[i].port.mapbase   =
			bfin_serial_resource[i].uart_base_addr;
		bfin_serial_ports[i].port.irq       =
			bfin_serial_resource[i].uart_irq;
		bfin_serial_ports[i].port.flags     = UPF_BOOT_AUTOCONF;
#ifdef CONFIG_SERIAL_BFIN_DMA
		bfin_serial_ports[i].tx_done	    = 1;
		bfin_serial_ports[i].tx_count	    = 0;
		bfin_serial_ports[i].tx_dma_channel =
			bfin_serial_resource[i].uart_tx_dma_channel;
		bfin_serial_ports[i].rx_dma_channel =
			bfin_serial_resource[i].uart_rx_dma_channel;
		init_timer(&(bfin_serial_ports[i].rx_dma_timer));
#else
		INIT_WORK(&bfin_serial_ports[i].cts_workqueue, bfin_serial_do_work);
#endif
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
		bfin_serial_ports[i].cts_pin	    =
			bfin_serial_resource[i].uart_cts_pin;
		bfin_serial_ports[i].rts_pin	    =
			bfin_serial_resource[i].uart_rts_pin;
#endif
		bfin_serial_hw_init(&bfin_serial_ports[i]);

	}
}

#ifdef CONFIG_SERIAL_BFIN_CONSOLE
static void bfin_serial_console_putchar(struct uart_port *port, int ch)
{
	struct bfin_serial_port *uart = (struct bfin_serial_port *)port;
	while (!(UART_GET_LSR(uart)))
		barrier();
	UART_PUT_CHAR(uart, ch);
	SSYNC();
}

/*
 * Interrupts are disabled on entering
 */
static void
bfin_serial_console_write(struct console *co, const char *s, unsigned int count)
{
	struct bfin_serial_port *uart = &bfin_serial_ports[co->index];
	int flags = 0;

	spin_lock_irqsave(&uart->port.lock, flags);
	uart_console_write(&uart->port, s, count, bfin_serial_console_putchar);
	spin_unlock_irqrestore(&uart->port.lock, flags);

}

/*
 * If the port was already initialised (eg, by a boot loader),
 * try to determine the current setup.
 */
static void __init
bfin_serial_console_get_options(struct bfin_serial_port *uart, int *baud,
			   int *parity, int *bits)
{
	unsigned short status;

	status = UART_GET_IER(uart) & (ERBFI | ETBEI);
	if (status == (ERBFI | ETBEI)) {
		/* ok, the port was enabled */
		unsigned short lcr, val;
		unsigned short dlh, dll;

		lcr = UART_GET_LCR(uart);

		*parity = 'n';
		if (lcr & PEN) {
			if (lcr & EPS)
				*parity = 'e';
			else
				*parity = 'o';
		}
		switch (lcr & 0x03) {
			case 0:	*bits = 5; break;
			case 1:	*bits = 6; break;
			case 2:	*bits = 7; break;
			case 3:	*bits = 8; break;
		}
		/* Set DLAB in LCR to Access DLL and DLH */
		val = UART_GET_LCR(uart);
		val |= DLAB;
		UART_PUT_LCR(uart, val);

		dll = UART_GET_DLL(uart);
		dlh = UART_GET_DLH(uart);

		/* Clear DLAB in LCR to Access THR RBR IER */
		val = UART_GET_LCR(uart);
		val &= ~DLAB;
		UART_PUT_LCR(uart, val);

		*baud = get_sclk() / (16*(dll | dlh << 8));
	}
	pr_debug("%s:baud = %d, parity = %c, bits= %d\n", __FUNCTION__, *baud, *parity, *bits);
}

static int __init
bfin_serial_console_setup(struct console *co, char *options)
{
	struct bfin_serial_port *uart;
	int baud = 57600;
	int bits = 8;
	int parity = 'n';
#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	int flow = 'r';
#else
	int flow = 'n';
#endif

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= nr_ports)
		co->index = 0;
	uart = &bfin_serial_ports[co->index];

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		bfin_serial_console_get_options(uart, &baud, &parity, &bits);

	return uart_set_options(&uart->port, co, baud, parity, bits, flow);
}

static struct uart_driver bfin_serial_reg;
static struct console bfin_serial_console = {
	.name		= BFIN_SERIAL_NAME,
	.write		= bfin_serial_console_write,
	.device		= uart_console_device,
	.setup		= bfin_serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &bfin_serial_reg,
};

static int __init bfin_serial_rs_console_init(void)
{
	bfin_serial_init_ports();
	register_console(&bfin_serial_console);
	return 0;
}
console_initcall(bfin_serial_rs_console_init);

#define BFIN_SERIAL_CONSOLE	&bfin_serial_console
#else
#define BFIN_SERIAL_CONSOLE	NULL
#endif

static struct uart_driver bfin_serial_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "bfin-uart",
	.dev_name		= BFIN_SERIAL_NAME,
	.major			= BFIN_SERIAL_MAJOR,
	.minor			= BFIN_SERIAL_MINOR,
	.nr			= NR_PORTS,
	.cons			= BFIN_SERIAL_CONSOLE,
};

static int bfin_serial_suspend(struct platform_device *dev, pm_message_t state)
{
	struct bfin_serial_port *uart = platform_get_drvdata(dev);

	if (uart)
		uart_suspend_port(&bfin_serial_reg, &uart->port);

	return 0;
}

static int bfin_serial_resume(struct platform_device *dev)
{
	struct bfin_serial_port *uart = platform_get_drvdata(dev);

	if (uart)
		uart_resume_port(&bfin_serial_reg, &uart->port);

	return 0;
}

static int bfin_serial_probe(struct platform_device *dev)
{
	struct resource *res = dev->resource;
	int i;

	for (i = 0; i < dev->num_resources; i++, res++)
		if (res->flags & IORESOURCE_MEM)
			break;

	if (i < dev->num_resources) {
		for (i = 0; i < nr_ports; i++, res++) {
			if (bfin_serial_ports[i].port.mapbase != res->start)
				continue;
			bfin_serial_ports[i].port.dev = &dev->dev;
			uart_add_one_port(&bfin_serial_reg, &bfin_serial_ports[i].port);
			platform_set_drvdata(dev, &bfin_serial_ports[i]);
		}
	}

	return 0;
}

static int bfin_serial_remove(struct platform_device *pdev)
{
	struct bfin_serial_port *uart = platform_get_drvdata(pdev);


#ifdef CONFIG_SERIAL_BFIN_CTSRTS
	gpio_free(uart->cts_pin);
	gpio_free(uart->rts_pin);
#endif

	platform_set_drvdata(pdev, NULL);

	if (uart)
		uart_remove_one_port(&bfin_serial_reg, &uart->port);

	return 0;
}

static struct platform_driver bfin_serial_driver = {
	.probe		= bfin_serial_probe,
	.remove		= bfin_serial_remove,
	.suspend	= bfin_serial_suspend,
	.resume		= bfin_serial_resume,
	.driver		= {
		.name	= "bfin-uart",
	},
};

static int __init bfin_serial_init(void)
{
	int ret;

	pr_info("Serial: Blackfin serial driver\n");

	bfin_serial_init_ports();

	ret = uart_register_driver(&bfin_serial_reg);
	if (ret == 0) {
		ret = platform_driver_register(&bfin_serial_driver);
		if (ret) {
			pr_debug("uart register failed\n");
			uart_unregister_driver(&bfin_serial_reg);
		}
	}
	return ret;
}

static void __exit bfin_serial_exit(void)
{
	platform_driver_unregister(&bfin_serial_driver);
	uart_unregister_driver(&bfin_serial_reg);
}

module_init(bfin_serial_init);
module_exit(bfin_serial_exit);

MODULE_AUTHOR("Aubrey.Li <aubrey.li@analog.com>");
MODULE_DESCRIPTION("Blackfin generic serial port driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(BFIN_SERIAL_MAJOR);
