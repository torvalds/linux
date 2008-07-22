/* drivers/serial/serial_lh7a40x.c
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

/* Driver for Sharp LH7A40X embedded serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *  Based on drivers/serial/amba.c, by Deep Blue Solutions Ltd.
 *
 *  ---
 *
 * This driver supports the embedded UARTs of the Sharp LH7A40X series
 * CPUs.  While similar to the 16550 and other UART chips, there is
 * nothing close to register compatibility.  Moreover, some of the
 * modem control lines are not available, either in the chip or they
 * are lacking in the board-level implementation.
 *
 * - Use of SIRDIS
 *   For simplicity, we disable the IR functions of any UART whenever
 *   we enable it.
 *
 */


#if defined(CONFIG_SERIAL_LH7A40X_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <asm/io.h>
#include <asm/irq.h>

#define DEV_MAJOR	204
#define DEV_MINOR	16
#define DEV_NR		3

#define ISR_LOOP_LIMIT	256

#define UR(p,o)	_UR ((p)->membase, o)
#define _UR(b,o) (*((volatile unsigned int*)(((unsigned char*) b) + (o))))
#define BIT_CLR(p,o,m)	UR(p,o) = UR(p,o) & (~(unsigned int)m)
#define BIT_SET(p,o,m)	UR(p,o) = UR(p,o) | ( (unsigned int)m)

#define UART_REG_SIZE	32

#define UART_R_DATA	(0x00)
#define UART_R_FCON	(0x04)
#define UART_R_BRCON	(0x08)
#define UART_R_CON	(0x0c)
#define UART_R_STATUS	(0x10)
#define UART_R_RAWISR	(0x14)
#define UART_R_INTEN	(0x18)
#define UART_R_ISR	(0x1c)

#define UARTEN		(0x01)		/* UART enable */
#define SIRDIS		(0x02)		/* Serial IR disable (UART1 only) */

#define RxEmpty		(0x10)
#define TxEmpty		(0x80)
#define TxFull		(0x20)
#define nRxRdy		RxEmpty
#define nTxRdy		TxFull
#define TxBusy		(0x08)

#define RxBreak		(0x0800)
#define RxOverrunError	(0x0400)
#define RxParityError	(0x0200)
#define RxFramingError	(0x0100)
#define RxError     (RxBreak | RxOverrunError | RxParityError | RxFramingError)

#define DCD		(0x04)
#define DSR		(0x02)
#define CTS		(0x01)

#define RxInt		(0x01)
#define TxInt		(0x02)
#define ModemInt	(0x04)
#define RxTimeoutInt	(0x08)

#define MSEOI		(0x10)

#define WLEN_8		(0x60)
#define WLEN_7		(0x40)
#define WLEN_6		(0x20)
#define WLEN_5		(0x00)
#define WLEN		(0x60)	/* Mask for all word-length bits */
#define STP2		(0x08)
#define PEN		(0x02)	/* Parity Enable */
#define EPS		(0x04)	/* Even Parity Set */
#define FEN		(0x10)	/* FIFO Enable */
#define BRK		(0x01)	/* Send Break */


struct uart_port_lh7a40x {
	struct uart_port port;
	unsigned int statusPrev; /* Most recently read modem status */
};

static void lh7a40xuart_stop_tx (struct uart_port* port)
{
	BIT_CLR (port, UART_R_INTEN, TxInt);
}

static void lh7a40xuart_start_tx (struct uart_port* port)
{
	BIT_SET (port, UART_R_INTEN, TxInt);

	/* *** FIXME: do I need to check for startup of the
		      transmitter?  The old driver did, but AMBA
		      doesn't . */
}

static void lh7a40xuart_stop_rx (struct uart_port* port)
{
	BIT_SET (port, UART_R_INTEN, RxTimeoutInt | RxInt);
}

static void lh7a40xuart_enable_ms (struct uart_port* port)
{
	BIT_SET (port, UART_R_INTEN, ModemInt);
}

static void lh7a40xuart_rx_chars (struct uart_port* port)
{
	struct tty_struct* tty = port->info->port.tty;
	int cbRxMax = 256;	/* (Gross) limit on receive */
	unsigned int data;	/* Received data and status */
	unsigned int flag;

	while (!(UR (port, UART_R_STATUS) & nRxRdy) && --cbRxMax) {
		data = UR (port, UART_R_DATA);
		flag = TTY_NORMAL;
		++port->icount.rx;

		if (unlikely(data & RxError)) {
			if (data & RxBreak) {
				data &= ~(RxFramingError | RxParityError);
				++port->icount.brk;
				if (uart_handle_break (port))
					continue;
			}
			else if (data & RxParityError)
				++port->icount.parity;
			else if (data & RxFramingError)
				++port->icount.frame;
			if (data & RxOverrunError)
				++port->icount.overrun;

				/* Mask by termios, leave Rx'd byte */
			data &= port->read_status_mask | 0xff;

			if (data & RxBreak)
				flag = TTY_BREAK;
			else if (data & RxParityError)
				flag = TTY_PARITY;
			else if (data & RxFramingError)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char (port, (unsigned char) data))
			continue;

		uart_insert_char(port, data, RxOverrunError, data, flag);
	}
	tty_flip_buffer_push (tty);
	return;
}

static void lh7a40xuart_tx_chars (struct uart_port* port)
{
	struct circ_buf* xmit = &port->info->xmit;
	int cbTxMax = port->fifosize;

	if (port->x_char) {
		UR (port, UART_R_DATA) = port->x_char;
		++port->icount.tx;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty (xmit) || uart_tx_stopped (port)) {
		lh7a40xuart_stop_tx (port);
		return;
	}

	/* Unlike the AMBA UART, the lh7a40x UART does not guarantee
	   that at least half of the FIFO is empty.  Instead, we check
	   status for every character.  Using the AMBA method causes
	   the transmitter to drop characters. */

	do {
		UR (port, UART_R_DATA) = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		++port->icount.tx;
		if (uart_circ_empty(xmit))
			break;
	} while (!(UR (port, UART_R_STATUS) & nTxRdy)
		 && cbTxMax--);

	if (uart_circ_chars_pending (xmit) < WAKEUP_CHARS)
		uart_write_wakeup (port);

	if (uart_circ_empty (xmit))
		lh7a40xuart_stop_tx (port);
}

static void lh7a40xuart_modem_status (struct uart_port* port)
{
	unsigned int status = UR (port, UART_R_STATUS);
	unsigned int delta
		= status ^ ((struct uart_port_lh7a40x*) port)->statusPrev;

	BIT_SET (port, UART_R_RAWISR, MSEOI); /* Clear modem status intr */

	if (!delta)		/* Only happens if we missed 2 transitions */
		return;

	((struct uart_port_lh7a40x*) port)->statusPrev = status;

	if (delta & DCD)
		uart_handle_dcd_change (port, status & DCD);

	if (delta & DSR)
		++port->icount.dsr;

	if (delta & CTS)
		uart_handle_cts_change (port, status & CTS);

	wake_up_interruptible (&port->info->delta_msr_wait);
}

static irqreturn_t lh7a40xuart_int (int irq, void* dev_id)
{
	struct uart_port* port = dev_id;
	unsigned int cLoopLimit = ISR_LOOP_LIMIT;
	unsigned int isr = UR (port, UART_R_ISR);


	do {
		if (isr & (RxInt | RxTimeoutInt))
			lh7a40xuart_rx_chars(port);
		if (isr & ModemInt)
			lh7a40xuart_modem_status (port);
		if (isr & TxInt)
			lh7a40xuart_tx_chars (port);

		if (--cLoopLimit == 0)
			break;

		isr = UR (port, UART_R_ISR);
	} while (isr & (RxInt | TxInt | RxTimeoutInt));

	return IRQ_HANDLED;
}

static unsigned int lh7a40xuart_tx_empty (struct uart_port* port)
{
	return (UR (port, UART_R_STATUS) & TxEmpty) ? TIOCSER_TEMT : 0;
}

static unsigned int lh7a40xuart_get_mctrl (struct uart_port* port)
{
	unsigned int result = 0;
	unsigned int status = UR (port, UART_R_STATUS);

	if (status & DCD)
		result |= TIOCM_CAR;
	if (status & DSR)
		result |= TIOCM_DSR;
	if (status & CTS)
		result |= TIOCM_CTS;

	return result;
}

static void lh7a40xuart_set_mctrl (struct uart_port* port, unsigned int mctrl)
{
	/* None of the ports supports DTR. UART1 supports RTS through GPIO. */
	/* Note, kernel appears to be setting DTR and RTS on console. */

	/* *** FIXME: this deserves more work.  There's some work in
	       tracing all of the IO pins. */
#if 0
	if( port->mapbase == UART1_PHYS) {
		gpioRegs_t *gpio = (gpioRegs_t *)IO_ADDRESS(GPIO_PHYS);

		if (mctrl & TIOCM_RTS)
			gpio->pbdr &= ~GPIOB_UART1_RTS;
		else
			gpio->pbdr |= GPIOB_UART1_RTS;
	}
#endif
}

static void lh7a40xuart_break_ctl (struct uart_port* port, int break_state)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	if (break_state == -1)
		BIT_SET (port, UART_R_FCON, BRK); /* Assert break */
	else
		BIT_CLR (port, UART_R_FCON, BRK); /* Deassert break */
	spin_unlock_irqrestore(&port->lock, flags);
}

static int lh7a40xuart_startup (struct uart_port* port)
{
	int retval;

	retval = request_irq (port->irq, lh7a40xuart_int, 0,
			      "serial_lh7a40x", port);
	if (retval)
		return retval;

				/* Initial modem control-line settings */
	((struct uart_port_lh7a40x*) port)->statusPrev
		= UR (port, UART_R_STATUS);

	/* There is presently no configuration option to enable IR.
	   Thus, we always disable it. */

	BIT_SET (port, UART_R_CON, UARTEN | SIRDIS);
	BIT_SET (port, UART_R_INTEN, RxTimeoutInt | RxInt);

	return 0;
}

static void lh7a40xuart_shutdown (struct uart_port* port)
{
	free_irq (port->irq, port);
	BIT_CLR (port, UART_R_FCON, BRK | FEN);
	BIT_CLR (port, UART_R_CON, UARTEN);
}

static void lh7a40xuart_set_termios (struct uart_port* port,
				     struct ktermios* termios,
				     struct ktermios* old)
{
	unsigned int con;
	unsigned int inten;
	unsigned int fcon;
	unsigned long flags;
	unsigned int baud;
	unsigned int quot;

	baud = uart_get_baud_rate (port, termios, old, 8, port->uartclk/16);
	quot = uart_get_divisor (port, baud); /* -1 performed elsewhere */

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		fcon = WLEN_5;
		break;
	case CS6:
		fcon = WLEN_6;
		break;
	case CS7:
		fcon = WLEN_7;
		break;
	case CS8:
	default:
		fcon = WLEN_8;
		break;
	}
	if (termios->c_cflag & CSTOPB)
		fcon |= STP2;
	if (termios->c_cflag & PARENB) {
		fcon |= PEN;
		if (!(termios->c_cflag & PARODD))
			fcon |= EPS;
	}
	if (port->fifosize > 1)
		fcon |= FEN;

	spin_lock_irqsave (&port->lock, flags);

	uart_update_timeout (port, termios->c_cflag, baud);

	port->read_status_mask = RxOverrunError;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= RxFramingError | RxParityError;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= RxBreak;

		/* Figure mask for status we ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= RxFramingError | RxParityError;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= RxBreak;
		/* Ignore overrun when ignorning parity */
		/* *** FIXME: is this in the right place? */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= RxOverrunError;
	}

		/* Ignore all receive errors when receive disabled */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= RxError;

	con   = UR (port, UART_R_CON);
	inten = (UR (port, UART_R_INTEN) & ~ModemInt);

	if (UART_ENABLE_MS (port, termios->c_cflag))
		inten |= ModemInt;

	BIT_CLR (port, UART_R_CON, UARTEN);	/* Disable UART */
	UR (port, UART_R_INTEN) = 0;		/* Disable interrupts */
	UR (port, UART_R_BRCON) = quot - 1;	/* Set baud rate divisor */
	UR (port, UART_R_FCON)  = fcon;		/* Set FIFO and frame ctrl */
	UR (port, UART_R_INTEN) = inten;	/* Enable interrupts */
	UR (port, UART_R_CON)   = con;		/* Restore UART mode */

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char* lh7a40xuart_type (struct uart_port* port)
{
	return port->type == PORT_LH7A40X ? "LH7A40X" : NULL;
}

static void lh7a40xuart_release_port (struct uart_port* port)
{
	release_mem_region (port->mapbase, UART_REG_SIZE);
}

static int lh7a40xuart_request_port (struct uart_port* port)
{
	return request_mem_region (port->mapbase, UART_REG_SIZE,
				   "serial_lh7a40x") != NULL
		? 0 : -EBUSY;
}

static void lh7a40xuart_config_port (struct uart_port* port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_LH7A40X;
		lh7a40xuart_request_port (port);
	}
}

static int lh7a40xuart_verify_port (struct uart_port* port,
				    struct serial_struct* ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_LH7A40X)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		ret = -EINVAL;
	if (ser->baud_base < 9600) /* *** FIXME: is this true? */
		ret = -EINVAL;
	return ret;
}

static struct uart_ops lh7a40x_uart_ops = {
	.tx_empty	= lh7a40xuart_tx_empty,
	.set_mctrl	= lh7a40xuart_set_mctrl,
	.get_mctrl	= lh7a40xuart_get_mctrl,
	.stop_tx	= lh7a40xuart_stop_tx,
	.start_tx	= lh7a40xuart_start_tx,
	.stop_rx	= lh7a40xuart_stop_rx,
	.enable_ms	= lh7a40xuart_enable_ms,
	.break_ctl	= lh7a40xuart_break_ctl,
	.startup	= lh7a40xuart_startup,
	.shutdown	= lh7a40xuart_shutdown,
	.set_termios	= lh7a40xuart_set_termios,
	.type		= lh7a40xuart_type,
	.release_port	= lh7a40xuart_release_port,
	.request_port	= lh7a40xuart_request_port,
	.config_port	= lh7a40xuart_config_port,
	.verify_port	= lh7a40xuart_verify_port,
};

static struct uart_port_lh7a40x lh7a40x_ports[DEV_NR] = {
	{
		.port = {
			.membase	= (void*) io_p2v (UART1_PHYS),
			.mapbase	= UART1_PHYS,
			.iotype		= UPIO_MEM,
			.irq		= IRQ_UART1INTR,
			.uartclk	= 14745600/2,
			.fifosize	= 16,
			.ops		= &lh7a40x_uart_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 0,
		},
	},
	{
		.port = {
			.membase	= (void*) io_p2v (UART2_PHYS),
			.mapbase	= UART2_PHYS,
			.iotype		= UPIO_MEM,
			.irq		= IRQ_UART2INTR,
			.uartclk	= 14745600/2,
			.fifosize	= 16,
			.ops		= &lh7a40x_uart_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 1,
		},
	},
	{
		.port = {
			.membase	= (void*) io_p2v (UART3_PHYS),
			.mapbase	= UART3_PHYS,
			.iotype		= UPIO_MEM,
			.irq		= IRQ_UART3INTR,
			.uartclk	= 14745600/2,
			.fifosize	= 16,
			.ops		= &lh7a40x_uart_ops,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 2,
		},
	},
};

#ifndef CONFIG_SERIAL_LH7A40X_CONSOLE
# define LH7A40X_CONSOLE NULL
#else
# define LH7A40X_CONSOLE &lh7a40x_console

static void lh7a40xuart_console_putchar(struct uart_port *port, int ch)
{
	while (UR(port, UART_R_STATUS) & nTxRdy)
		;
	UR(port, UART_R_DATA) = ch;
}

static void lh7a40xuart_console_write (struct console* co,
				       const char* s,
				       unsigned int count)
{
	struct uart_port* port = &lh7a40x_ports[co->index].port;
	unsigned int con = UR (port, UART_R_CON);
	unsigned int inten = UR (port, UART_R_INTEN);


	UR (port, UART_R_INTEN) = 0;		/* Disable all interrupts */
	BIT_SET (port, UART_R_CON, UARTEN | SIRDIS); /* Enable UART */

	uart_console_write(port, s, count, lh7a40xuart_console_putchar);

				/* Wait until all characters are sent */
	while (UR (port, UART_R_STATUS) & TxBusy)
		;

				/* Restore control and interrupt mask */
	UR (port, UART_R_CON) = con;
	UR (port, UART_R_INTEN) = inten;
}

static void __init lh7a40xuart_console_get_options (struct uart_port* port,
						    int* baud,
						    int* parity,
						    int* bits)
{
	if (UR (port, UART_R_CON) & UARTEN) {
		unsigned int fcon = UR (port, UART_R_FCON);
		unsigned int quot = UR (port, UART_R_BRCON) + 1;

		switch (fcon & (PEN | EPS)) {
		default:        *parity = 'n'; break;
		case PEN:       *parity = 'o'; break;
		case PEN | EPS: *parity = 'e'; break;
		}

		switch (fcon & WLEN) {
		default:
		case WLEN_8: *bits = 8; break;
		case WLEN_7: *bits = 7; break;
		case WLEN_6: *bits = 6; break;
		case WLEN_5: *bits = 5; break;
		}

		*baud = port->uartclk/(16*quot);
	}
}

static int __init lh7a40xuart_console_setup (struct console* co, char* options)
{
	struct uart_port* port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= DEV_NR) /* Bounds check on device number */
		co->index = 0;
	port = &lh7a40x_ports[co->index].port;

	if (options)
		uart_parse_options (options, &baud, &parity, &bits, &flow);
	else
		lh7a40xuart_console_get_options (port, &baud, &parity, &bits);

	return uart_set_options (port, co, baud, parity, bits, flow);
}

static struct uart_driver lh7a40x_reg;
static struct console lh7a40x_console = {
	.name		= "ttyAM",
	.write		= lh7a40xuart_console_write,
	.device		= uart_console_device,
	.setup		= lh7a40xuart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &lh7a40x_reg,
};

static int __init lh7a40xuart_console_init(void)
{
	register_console (&lh7a40x_console);
	return 0;
}

console_initcall (lh7a40xuart_console_init);

#endif

static struct uart_driver lh7a40x_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "ttyAM",
	.dev_name		= "ttyAM",
	.major			= DEV_MAJOR,
	.minor			= DEV_MINOR,
	.nr			= DEV_NR,
	.cons			= LH7A40X_CONSOLE,
};

static int __init lh7a40xuart_init(void)
{
	int ret;

	printk (KERN_INFO "serial: LH7A40X serial driver\n");

	ret = uart_register_driver (&lh7a40x_reg);

	if (ret == 0) {
		int i;

		for (i = 0; i < DEV_NR; i++) {
			/* UART3, when used, requires GPIO pin reallocation */
			if (lh7a40x_ports[i].port.mapbase == UART3_PHYS)
				GPIO_PINMUX |= 1<<3;
			uart_add_one_port (&lh7a40x_reg,
					   &lh7a40x_ports[i].port);
		}
	}
	return ret;
}

static void __exit lh7a40xuart_exit(void)
{
	int i;

	for (i = 0; i < DEV_NR; i++)
		uart_remove_one_port (&lh7a40x_reg, &lh7a40x_ports[i].port);

	uart_unregister_driver (&lh7a40x_reg);
}

module_init (lh7a40xuart_init);
module_exit (lh7a40xuart_exit);

MODULE_AUTHOR ("Marc Singer");
MODULE_DESCRIPTION ("Sharp LH7A40X serial port driver");
MODULE_LICENSE ("GPL");
