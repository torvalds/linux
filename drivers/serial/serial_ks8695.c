/*
 *  drivers/serial/serial_ks8695.c
 *
 *  Driver for KS8695 serial ports
 *
 *  Based on drivers/serial/serial_amba.c, by Kam Lee.
 *
 *  Copyright 2002-2005 Micrel Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <mach/regs-uart.h>
#include <mach/regs-irq.h>

#if defined(CONFIG_SERIAL_KS8695_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>


#define SERIAL_KS8695_MAJOR	204
#define SERIAL_KS8695_MINOR	16
#define SERIAL_KS8695_DEVNAME	"ttyAM"

#define SERIAL_KS8695_NR	1

/*
 * Access macros for the KS8695 UART
 */
#define UART_GET_CHAR(p)	(__raw_readl((p)->membase + KS8695_URRB) & 0xFF)
#define UART_PUT_CHAR(p, c)	__raw_writel((c), (p)->membase + KS8695_URTH)
#define UART_GET_FCR(p)		__raw_readl((p)->membase + KS8695_URFC)
#define UART_PUT_FCR(p, c)	__raw_writel((c), (p)->membase + KS8695_URFC)
#define UART_GET_MSR(p)		__raw_readl((p)->membase + KS8695_URMS)
#define UART_GET_LSR(p)		__raw_readl((p)->membase + KS8695_URLS)
#define UART_GET_LCR(p)		__raw_readl((p)->membase + KS8695_URLC)
#define UART_PUT_LCR(p, c)	__raw_writel((c), (p)->membase + KS8695_URLC)
#define UART_GET_MCR(p)		__raw_readl((p)->membase + KS8695_URMC)
#define UART_PUT_MCR(p, c)	__raw_writel((c), (p)->membase + KS8695_URMC)
#define UART_GET_BRDR(p)	__raw_readl((p)->membase + KS8695_URBD)
#define UART_PUT_BRDR(p, c)	__raw_writel((c), (p)->membase + KS8695_URBD)

#define KS8695_CLR_TX_INT()	__raw_writel(1 << KS8695_IRQ_UART_TX, KS8695_IRQ_VA + KS8695_INTST)

#define UART_DUMMY_LSR_RX	0x100
#define UART_PORT_SIZE		(KS8695_USR - KS8695_URRB + 4)

static inline int tx_enabled(struct uart_port *port)
{
	return port->unused[0] & 1;
}

static inline int rx_enabled(struct uart_port *port)
{
	return port->unused[0] & 2;
}

static inline int ms_enabled(struct uart_port *port)
{
	return port->unused[0] & 4;
}

static inline void ms_enable(struct uart_port *port, int enabled)
{
	if(enabled)
		port->unused[0] |= 4;
	else
		port->unused[0] &= ~4;
}

static inline void rx_enable(struct uart_port *port, int enabled)
{
	if(enabled)
		port->unused[0] |= 2;
	else
		port->unused[0] &= ~2;
}

static inline void tx_enable(struct uart_port *port, int enabled)
{
	if(enabled)
		port->unused[0] |= 1;
	else
		port->unused[0] &= ~1;
}


#ifdef SUPPORT_SYSRQ
static struct console ks8695_console;
#endif

static void ks8695uart_stop_tx(struct uart_port *port)
{
	if (tx_enabled(port)) {
		/* use disable_irq_nosync() and not disable_irq() to avoid self
		 * imposed deadlock by not waiting for irq handler to end,
		 * since this ks8695uart_stop_tx() is called from interrupt context.
		 */
		disable_irq_nosync(KS8695_IRQ_UART_TX);
		tx_enable(port, 0);
	}
}

static void ks8695uart_start_tx(struct uart_port *port)
{
	if (!tx_enabled(port)) {
		enable_irq(KS8695_IRQ_UART_TX);
		tx_enable(port, 1);
	}
}

static void ks8695uart_stop_rx(struct uart_port *port)
{
	if (rx_enabled(port)) {
		disable_irq(KS8695_IRQ_UART_RX);
		rx_enable(port, 0);
	}
}

static void ks8695uart_enable_ms(struct uart_port *port)
{
	if (!ms_enabled(port)) {
		enable_irq(KS8695_IRQ_UART_MODEM_STATUS);
		ms_enable(port,1);
	}
}

static void ks8695uart_disable_ms(struct uart_port *port)
{
	if (ms_enabled(port)) {
		disable_irq(KS8695_IRQ_UART_MODEM_STATUS);
		ms_enable(port,0);
	}
}

static irqreturn_t ks8695uart_rx_chars(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct tty_struct *tty = port->state->port.tty;
	unsigned int status, ch, lsr, flg, max_count = 256;

	status = UART_GET_LSR(port);		/* clears pending LSR interrupts */
	while ((status & URLS_URDR) && max_count--) {
		ch = UART_GET_CHAR(port);
		flg = TTY_NORMAL;

		port->icount.rx++;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		lsr = UART_GET_LSR(port) | UART_DUMMY_LSR_RX;
		if (unlikely(lsr & (URLS_URBI | URLS_URPE | URLS_URFE | URLS_URROE))) {
			if (lsr & URLS_URBI) {
				lsr &= ~(URLS_URFE | URLS_URPE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			}
			if (lsr & URLS_URPE)
				port->icount.parity++;
			if (lsr & URLS_URFE)
				port->icount.frame++;
			if (lsr & URLS_URROE)
				port->icount.overrun++;

			lsr &= port->read_status_mask;

			if (lsr & URLS_URBI)
				flg = TTY_BREAK;
			else if (lsr & URLS_URPE)
				flg = TTY_PARITY;
			else if (lsr & URLS_URFE)
				flg = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, lsr, URLS_URROE, ch, flg);

ignore_char:
		status = UART_GET_LSR(port);
	}
	tty_flip_buffer_push(tty);

	return IRQ_HANDLED;
}


static irqreturn_t ks8695uart_tx_chars(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int count;

	if (port->x_char) {
		KS8695_CLR_TX_INT();
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return IRQ_HANDLED;
	}

	if (uart_tx_stopped(port) || uart_circ_empty(xmit)) {
		ks8695uart_stop_tx(port);
		return IRQ_HANDLED;
	}

	count = 16;	/* fifo size */
	while (!uart_circ_empty(xmit) && (count-- > 0)) {
		KS8695_CLR_TX_INT();
		UART_PUT_CHAR(port, xmit->buf[xmit->tail]);

		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		ks8695uart_stop_tx(port);

	return IRQ_HANDLED;
}

static irqreturn_t ks8695uart_modem_status(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	unsigned int status;

	/*
	 * clear modem interrupt by reading MSR
	 */
	status = UART_GET_MSR(port);

	if (status & URMS_URDDCD)
		uart_handle_dcd_change(port, status & URMS_URDDCD);

	if (status & URMS_URDDST)
		port->icount.dsr++;

	if (status & URMS_URDCTS)
		uart_handle_cts_change(port, status & URMS_URDCTS);

	if (status & URMS_URTERI)
		port->icount.rng++;

	wake_up_interruptible(&port->state->port.delta_msr_wait);

	return IRQ_HANDLED;
}

static unsigned int ks8695uart_tx_empty(struct uart_port *port)
{
	return (UART_GET_LSR(port) & URLS_URTE) ? TIOCSER_TEMT : 0;
}

static unsigned int ks8695uart_get_mctrl(struct uart_port *port)
{
	unsigned int result = 0;
	unsigned int status;

	status = UART_GET_MSR(port);
	if (status & URMS_URDCD)
		result |= TIOCM_CAR;
	if (status & URMS_URDSR)
		result |= TIOCM_DSR;
	if (status & URMS_URCTS)
		result |= TIOCM_CTS;
	if (status & URMS_URRI)
		result |= TIOCM_RI;

	return result;
}

static void ks8695uart_set_mctrl(struct uart_port *port, u_int mctrl)
{
	unsigned int mcr;

	mcr = UART_GET_MCR(port);
	if (mctrl & TIOCM_RTS)
		mcr |= URMC_URRTS;
	else
		mcr &= ~URMC_URRTS;

	if (mctrl & TIOCM_DTR)
		mcr |= URMC_URDTR;
	else
		mcr &= ~URMC_URDTR;

	UART_PUT_MCR(port, mcr);
}

static void ks8695uart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned int lcr;

	lcr = UART_GET_LCR(port);

	if (break_state == -1)
		lcr |= URLC_URSBC;
	else
		lcr &= ~URLC_URSBC;

	UART_PUT_LCR(port, lcr);
}

static int ks8695uart_startup(struct uart_port *port)
{
	int retval;

	set_irq_flags(KS8695_IRQ_UART_TX, IRQF_VALID | IRQF_NOAUTOEN);
	tx_enable(port, 0);
	rx_enable(port, 1);
	ms_enable(port, 1);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(KS8695_IRQ_UART_TX, ks8695uart_tx_chars, IRQF_DISABLED, "UART TX", port);
	if (retval)
		goto err_tx;

	retval = request_irq(KS8695_IRQ_UART_RX, ks8695uart_rx_chars, IRQF_DISABLED, "UART RX", port);
	if (retval)
		goto err_rx;

	retval = request_irq(KS8695_IRQ_UART_LINE_STATUS, ks8695uart_rx_chars, IRQF_DISABLED, "UART LineStatus", port);
	if (retval)
		goto err_ls;

	retval = request_irq(KS8695_IRQ_UART_MODEM_STATUS, ks8695uart_modem_status, IRQF_DISABLED, "UART ModemStatus", port);
	if (retval)
		goto err_ms;

	return 0;

err_ms:
	free_irq(KS8695_IRQ_UART_LINE_STATUS, port);
err_ls:
	free_irq(KS8695_IRQ_UART_RX, port);
err_rx:
	free_irq(KS8695_IRQ_UART_TX, port);
err_tx:
	return retval;
}

static void ks8695uart_shutdown(struct uart_port *port)
{
	/*
	 * Free the interrupt
	 */
	free_irq(KS8695_IRQ_UART_RX, port);
	free_irq(KS8695_IRQ_UART_TX, port);
	free_irq(KS8695_IRQ_UART_MODEM_STATUS, port);
	free_irq(KS8695_IRQ_UART_LINE_STATUS, port);

	/* disable break condition and fifos */
	UART_PUT_LCR(port, UART_GET_LCR(port) & ~URLC_URSBC);
	UART_PUT_FCR(port, UART_GET_FCR(port) & ~URFC_URFE);
}

static void ks8695uart_set_termios(struct uart_port *port, struct ktermios *termios, struct ktermios *old)
{
	unsigned int lcr, fcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = URCL_5;
		break;
	case CS6:
		lcr = URCL_6;
		break;
	case CS7:
		lcr = URCL_7;
		break;
	default:
		lcr = URCL_8;
		break;
	}

	/* stop bits */
	if (termios->c_cflag & CSTOPB)
		lcr |= URLC_URSB;

	/* parity */
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {	/* Mark or Space parity */
			if (termios->c_cflag & PARODD)
				lcr |= URPE_MARK;
			else
				lcr |= URPE_SPACE;
		}
		else if (termios->c_cflag & PARODD)
			lcr |= URPE_ODD;
		else
			lcr |= URPE_EVEN;
	}

	if (port->fifosize > 1)
		fcr = URFC_URFRT_8 | URFC_URTFR | URFC_URRFR | URFC_URFE;

	spin_lock_irqsave(&port->lock, flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = URLS_URROE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= (URLS_URFE | URLS_URPE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= URLS_URBI;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= (URLS_URFE | URLS_URPE);
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= URLS_URBI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= URLS_URROE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_DUMMY_LSR_RX;

	/* first, disable everything */
	if (UART_ENABLE_MS(port, termios->c_cflag))
		ks8695uart_enable_ms(port);
	else
		ks8695uart_disable_ms(port);

	/* Set baud rate */
	UART_PUT_BRDR(port, quot);

	UART_PUT_LCR(port, lcr);
	UART_PUT_FCR(port, fcr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *ks8695uart_type(struct uart_port *port)
{
	return port->type == PORT_KS8695 ? "KS8695" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void ks8695uart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int ks8695uart_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, UART_PORT_SIZE,
			"serial_ks8695") != NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void ks8695uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_KS8695;
		ks8695uart_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int ks8695uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_KS8695)
		ret = -EINVAL;
	if (ser->irq != port->irq)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops ks8695uart_pops = {
	.tx_empty	= ks8695uart_tx_empty,
	.set_mctrl	= ks8695uart_set_mctrl,
	.get_mctrl	= ks8695uart_get_mctrl,
	.stop_tx	= ks8695uart_stop_tx,
	.start_tx	= ks8695uart_start_tx,
	.stop_rx	= ks8695uart_stop_rx,
	.enable_ms	= ks8695uart_enable_ms,
	.break_ctl	= ks8695uart_break_ctl,
	.startup	= ks8695uart_startup,
	.shutdown	= ks8695uart_shutdown,
	.set_termios	= ks8695uart_set_termios,
	.type		= ks8695uart_type,
	.release_port	= ks8695uart_release_port,
	.request_port	= ks8695uart_request_port,
	.config_port	= ks8695uart_config_port,
	.verify_port	= ks8695uart_verify_port,
};

static struct uart_port ks8695uart_ports[SERIAL_KS8695_NR] = {
	{
		.membase	= (void *) KS8695_UART_VA,
		.mapbase	= KS8695_UART_VA,
		.iotype		= SERIAL_IO_MEM,
		.irq		= KS8695_IRQ_UART_TX,
		.uartclk	= KS8695_CLOCK_RATE * 16,
		.fifosize	= 16,
		.ops		= &ks8695uart_pops,
		.flags		= ASYNC_BOOT_AUTOCONF,
		.line		= 0,
	}
};

#ifdef CONFIG_SERIAL_KS8695_CONSOLE
static void ks8695_console_putchar(struct uart_port *port, int ch)
{
	while (!(UART_GET_LSR(port) & URLS_URTHRE))
		barrier();

	UART_PUT_CHAR(port, ch);
}

static void ks8695_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = ks8695uart_ports + co->index;

	uart_console_write(port, s, count, ks8695_console_putchar);
}

static void __init ks8695_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	unsigned int lcr;

	lcr = UART_GET_LCR(port);

	switch (lcr & URLC_PARITY) {
		case URPE_ODD:
			*parity = 'o';
			break;
		case URPE_EVEN:
			*parity = 'e';
			break;
		default:
			*parity = 'n';
	}

	switch (lcr & URLC_URCL) {
		case URCL_5:
			*bits = 5;
			break;
		case URCL_6:
			*bits = 6;
			break;
		case URCL_7:
			*bits = 7;
			break;
		default:
			*bits = 8;
	}

	*baud = port->uartclk / (UART_GET_BRDR(port) & 0x0FFF);
	*baud /= 16;
	*baud &= 0xFFFFFFF0;
}

static int __init ks8695_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(ks8695uart_ports, SERIAL_KS8695_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		ks8695_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver ks8695_reg;

static struct console ks8695_console = {
	.name		= SERIAL_KS8695_DEVNAME,
	.write		= ks8695_console_write,
	.device		= uart_console_device,
	.setup		= ks8695_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &ks8695_reg,
};

static int __init ks8695_console_init(void)
{
	add_preferred_console(SERIAL_KS8695_DEVNAME, 0, NULL);
	register_console(&ks8695_console);
	return 0;
}

console_initcall(ks8695_console_init);

#define KS8695_CONSOLE	&ks8695_console
#else
#define KS8695_CONSOLE	NULL
#endif

static struct uart_driver ks8695_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial_ks8695",
	.dev_name		= SERIAL_KS8695_DEVNAME,
	.major			= SERIAL_KS8695_MAJOR,
	.minor			= SERIAL_KS8695_MINOR,
	.nr			= SERIAL_KS8695_NR,
	.cons			= KS8695_CONSOLE,
};

static int __init ks8695uart_init(void)
{
	int i, ret;

	printk(KERN_INFO "Serial: Micrel KS8695 UART driver\n");

	ret = uart_register_driver(&ks8695_reg);
	if (ret)
		return ret;

	for (i = 0; i < SERIAL_KS8695_NR; i++)
		uart_add_one_port(&ks8695_reg, &ks8695uart_ports[0]);

	return 0;
}

static void __exit ks8695uart_exit(void)
{
	int i;

	for (i = 0; i < SERIAL_KS8695_NR; i++)
		uart_remove_one_port(&ks8695_reg, &ks8695uart_ports[0]);
	uart_unregister_driver(&ks8695_reg);
}

module_init(ks8695uart_init);
module_exit(ks8695uart_exit);

MODULE_DESCRIPTION("KS8695 serial port driver");
MODULE_AUTHOR("Micrel Inc.");
MODULE_LICENSE("GPL");
