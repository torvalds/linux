/****************************************************************************/

/*
 *	mcf.c -- Freescale ColdFire UART driver
 *
 *	(C) Copyright 2003-2007, Greg Ungerer <gerg@snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/****************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/nettel.h>

/****************************************************************************/

/*
 *	Some boards implement the DTR/DCD lines using GPIO lines, most
 *	don't. Dummy out the access macros for those that don't. Those
 *	that do should define these macros somewhere in there board
 *	specific inlude files.
 */
#if !defined(mcf_getppdcd)
#define	mcf_getppdcd(p)		(1)
#endif
#if !defined(mcf_getppdtr)
#define	mcf_getppdtr(p)		(1)
#endif
#if !defined(mcf_setppdtr)
#define	mcf_setppdtr(p, v)	do { } while (0)
#endif

/****************************************************************************/

/*
 *	Local per-uart structure.
 */
struct mcf_uart {
	struct uart_port	port;
	unsigned int		sigs;		/* Local copy of line sigs */
	unsigned char		imr;		/* Local IMR mirror */
};

/****************************************************************************/

static unsigned int mcf_tx_empty(struct uart_port *port)
{
	return (readb(port->membase + MCFUART_USR) & MCFUART_USR_TXEMPTY) ?
		TIOCSER_TEMT : 0;
}

/****************************************************************************/

static unsigned int mcf_get_mctrl(struct uart_port *port)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;
	unsigned int sigs;

	spin_lock_irqsave(&port->lock, flags);
	sigs = (readb(port->membase + MCFUART_UIPR) & MCFUART_UIPR_CTS) ?
		0 : TIOCM_CTS;
	sigs |= (pp->sigs & TIOCM_RTS);
	sigs |= (mcf_getppdcd(port->line) ? TIOCM_CD : 0);
	sigs |= (mcf_getppdtr(port->line) ? TIOCM_DTR : 0);
	spin_unlock_irqrestore(&port->lock, flags);
	return sigs;
}

/****************************************************************************/

static void mcf_set_mctrl(struct uart_port *port, unsigned int sigs)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	pp->sigs = sigs;
	mcf_setppdtr(port->line, (sigs & TIOCM_DTR));
	if (sigs & TIOCM_RTS)
		writeb(MCFUART_UOP_RTS, port->membase + MCFUART_UOP1);
	else
		writeb(MCFUART_UOP_RTS, port->membase + MCFUART_UOP0);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_start_tx(struct uart_port *port)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	pp->imr |= MCFUART_UIR_TXREADY;
	writeb(pp->imr, port->membase + MCFUART_UIMR);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_stop_tx(struct uart_port *port)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	pp->imr &= ~MCFUART_UIR_TXREADY;
	writeb(pp->imr, port->membase + MCFUART_UIMR);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_stop_rx(struct uart_port *port)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	pp->imr &= ~MCFUART_UIR_RXREADY;
	writeb(pp->imr, port->membase + MCFUART_UIMR);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	if (break_state == -1)
		writeb(MCFUART_UCR_CMDBREAKSTART, port->membase + MCFUART_UCR);
	else
		writeb(MCFUART_UCR_CMDBREAKSTOP, port->membase + MCFUART_UCR);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_enable_ms(struct uart_port *port)
{
}

/****************************************************************************/

static int mcf_startup(struct uart_port *port)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* Reset UART, get it into known state... */
	writeb(MCFUART_UCR_CMDRESETRX, port->membase + MCFUART_UCR);
	writeb(MCFUART_UCR_CMDRESETTX, port->membase + MCFUART_UCR);

	/* Enable the UART transmitter and receiver */
	writeb(MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE,
		port->membase + MCFUART_UCR);

	/* Enable RX interrupts now */
	pp->imr = MCFUART_UIR_RXREADY;
	writeb(pp->imr, port->membase + MCFUART_UIMR);

	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

/****************************************************************************/

static void mcf_shutdown(struct uart_port *port)
{
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* Disable all interrupts now */
	pp->imr = 0;
	writeb(pp->imr, port->membase + MCFUART_UIMR);

	/* Disable UART transmitter and receiver */
	writeb(MCFUART_UCR_CMDRESETRX, port->membase + MCFUART_UCR);
	writeb(MCFUART_UCR_CMDRESETTX, port->membase + MCFUART_UCR);

	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_set_termios(struct uart_port *port, struct ktermios *termios,
	struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, baudclk;
#if defined(CONFIG_M5272)
	unsigned int baudfr;
#endif
	unsigned char mr1, mr2;

	baud = uart_get_baud_rate(port, termios, old, 0, 230400);
#if defined(CONFIG_M5272)
	baudclk = (MCF_BUSCLK / baud) / 32;
	baudfr = (((MCF_BUSCLK / baud) + 1) / 2) % 16;
#else
	baudclk = ((MCF_BUSCLK / baud) + 16) / 32;
#endif

	mr1 = MCFUART_MR1_RXIRQRDY | MCFUART_MR1_RXERRCHAR;
	mr2 = 0;

	switch (termios->c_cflag & CSIZE) {
	case CS5: mr1 |= MCFUART_MR1_CS5; break;
	case CS6: mr1 |= MCFUART_MR1_CS6; break;
	case CS7: mr1 |= MCFUART_MR1_CS7; break;
	case CS8:
	default:  mr1 |= MCFUART_MR1_CS8; break;
	}

	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {
			if (termios->c_cflag & PARODD)
				mr1 |= MCFUART_MR1_PARITYMARK;
			else
				mr1 |= MCFUART_MR1_PARITYSPACE;
		} else {
			if (termios->c_cflag & PARODD)
				mr1 |= MCFUART_MR1_PARITYODD;
			else
				mr1 |= MCFUART_MR1_PARITYEVEN;
		}
	} else {
		mr1 |= MCFUART_MR1_PARITYNONE;
	}

	if (termios->c_cflag & CSTOPB)
		mr2 |= MCFUART_MR2_STOP2;
	else
		mr2 |= MCFUART_MR2_STOP1;

	if (termios->c_cflag & CRTSCTS) {
		mr1 |= MCFUART_MR1_RXRTS;
		mr2 |= MCFUART_MR2_TXCTS;
	}

	spin_lock_irqsave(&port->lock, flags);
	writeb(MCFUART_UCR_CMDRESETRX, port->membase + MCFUART_UCR);
	writeb(MCFUART_UCR_CMDRESETTX, port->membase + MCFUART_UCR);
	writeb(MCFUART_UCR_CMDRESETMRPTR, port->membase + MCFUART_UCR);
	writeb(mr1, port->membase + MCFUART_UMR);
	writeb(mr2, port->membase + MCFUART_UMR);
	writeb((baudclk & 0xff00) >> 8, port->membase + MCFUART_UBG1);
	writeb((baudclk & 0xff), port->membase + MCFUART_UBG2);
#if defined(CONFIG_M5272)
	writeb((baudfr & 0x0f), port->membase + MCFUART_UFPD);
#endif
	writeb(MCFUART_UCSR_RXCLKTIMER | MCFUART_UCSR_TXCLKTIMER,
		port->membase + MCFUART_UCSR);
	writeb(MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE,
		port->membase + MCFUART_UCR);
	spin_unlock_irqrestore(&port->lock, flags);
}

/****************************************************************************/

static void mcf_rx_chars(struct mcf_uart *pp)
{
	struct uart_port *port = &pp->port;
	unsigned char status, ch, flag;

	while ((status = readb(port->membase + MCFUART_USR)) & MCFUART_USR_RXREADY) {
		ch = readb(port->membase + MCFUART_URB);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (status & MCFUART_USR_RXERR) {
			writeb(MCFUART_UCR_CMDRESETERR,
				port->membase + MCFUART_UCR);

			if (status & MCFUART_USR_RXBREAK) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (status & MCFUART_USR_RXPARITY) {
				port->icount.parity++;
			} else if (status & MCFUART_USR_RXOVERRUN) {
				port->icount.overrun++;
			} else if (status & MCFUART_USR_RXFRAMING) {
				port->icount.frame++;
			}

			status &= port->read_status_mask;

			if (status & MCFUART_USR_RXBREAK)
				flag = TTY_BREAK;
			else if (status & MCFUART_USR_RXPARITY)
				flag = TTY_PARITY;
			else if (status & MCFUART_USR_RXFRAMING)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue;
		uart_insert_char(port, status, MCFUART_USR_RXOVERRUN, ch, flag);
	}

	tty_flip_buffer_push(port->info->port.tty);
}

/****************************************************************************/

static void mcf_tx_chars(struct mcf_uart *pp)
{
	struct uart_port *port = &pp->port;
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		/* Send special char - probably flow control */
		writeb(port->x_char, port->membase + MCFUART_UTB);
		port->x_char = 0;
		port->icount.tx++;
		return;
	}

	while (readb(port->membase + MCFUART_USR) & MCFUART_USR_TXREADY) {
		if (xmit->head == xmit->tail)
			break;
		writeb(xmit->buf[xmit->tail], port->membase + MCFUART_UTB);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE -1);
		port->icount.tx++;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (xmit->head == xmit->tail) {
		pp->imr &= ~MCFUART_UIR_TXREADY;
		writeb(pp->imr, port->membase + MCFUART_UIMR);
	}
}

/****************************************************************************/

static irqreturn_t mcf_interrupt(int irq, void *data)
{
	struct uart_port *port = data;
	struct mcf_uart *pp = container_of(port, struct mcf_uart, port);
	unsigned int isr;

	isr = readb(port->membase + MCFUART_UISR) & pp->imr;
	if (isr & MCFUART_UIR_RXREADY)
		mcf_rx_chars(pp);
	if (isr & MCFUART_UIR_TXREADY)
		mcf_tx_chars(pp);
	return IRQ_HANDLED;
}

/****************************************************************************/

static void mcf_config_port(struct uart_port *port, int flags)
{
	port->type = PORT_MCF;

	/* Clear mask, so no surprise interrupts. */
	writeb(0, port->membase + MCFUART_UIMR);

	if (request_irq(port->irq, mcf_interrupt, IRQF_DISABLED, "UART", port))
		printk(KERN_ERR "MCF: unable to attach ColdFire UART %d "
			"interrupt vector=%d\n", port->line, port->irq);
}

/****************************************************************************/

static const char *mcf_type(struct uart_port *port)
{
	return (port->type == PORT_MCF) ? "ColdFire UART" : NULL;
}

/****************************************************************************/

static int mcf_request_port(struct uart_port *port)
{
	/* UARTs always present */
	return 0;
}

/****************************************************************************/

static void mcf_release_port(struct uart_port *port)
{
	/* Nothing to release... */
}

/****************************************************************************/

static int mcf_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if ((ser->type != PORT_UNKNOWN) && (ser->type != PORT_MCF))
		return -EINVAL;
	return 0;
}

/****************************************************************************/

/*
 *	Define the basic serial functions we support.
 */
static struct uart_ops mcf_uart_ops = {
	.tx_empty	= mcf_tx_empty,
	.get_mctrl	= mcf_get_mctrl,
	.set_mctrl	= mcf_set_mctrl,
	.start_tx	= mcf_start_tx,
	.stop_tx	= mcf_stop_tx,
	.stop_rx	= mcf_stop_rx,
	.enable_ms	= mcf_enable_ms,
	.break_ctl	= mcf_break_ctl,
	.startup	= mcf_startup,
	.shutdown	= mcf_shutdown,
	.set_termios	= mcf_set_termios,
	.type		= mcf_type,
	.request_port	= mcf_request_port,
	.release_port	= mcf_release_port,
	.config_port	= mcf_config_port,
	.verify_port	= mcf_verify_port,
};

static struct mcf_uart mcf_ports[3];

#define	MCF_MAXPORTS	ARRAY_SIZE(mcf_ports)

/****************************************************************************/
#if defined(CONFIG_SERIAL_MCF_CONSOLE)
/****************************************************************************/

int __init early_mcf_setup(struct mcf_platform_uart *platp)
{
	struct uart_port *port;
	int i;

	for (i = 0; ((i < MCF_MAXPORTS) && (platp[i].mapbase)); i++) {
		port = &mcf_ports[i].port;

		port->line = i;
		port->type = PORT_MCF;
		port->mapbase = platp[i].mapbase;
		port->membase = (platp[i].membase) ? platp[i].membase :
			(unsigned char __iomem *) port->mapbase;
		port->iotype = SERIAL_IO_MEM;
		port->irq = platp[i].irq;
		port->uartclk = MCF_BUSCLK;
		port->flags = ASYNC_BOOT_AUTOCONF;
		port->ops = &mcf_uart_ops;
	}

	return 0;
}

/****************************************************************************/

static void mcf_console_putc(struct console *co, const char c)
{
	struct uart_port *port = &(mcf_ports + co->index)->port;
	int i;

	for (i = 0; (i < 0x10000); i++) {
		if (readb(port->membase + MCFUART_USR) & MCFUART_USR_TXREADY)
			break;
	}
	writeb(c, port->membase + MCFUART_UTB);
	for (i = 0; (i < 0x10000); i++) {
		if (readb(port->membase + MCFUART_USR) & MCFUART_USR_TXREADY)
			break;
	}
}

/****************************************************************************/

static void mcf_console_write(struct console *co, const char *s, unsigned int count)
{
	for (; (count); count--, s++) {
		mcf_console_putc(co, *s);
		if (*s == '\n')
			mcf_console_putc(co, '\r');
	}
}

/****************************************************************************/

static int __init mcf_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = CONFIG_SERIAL_MCF_BAUDRATE;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if ((co->index < 0) || (co->index >= MCF_MAXPORTS))
		co->index = 0;
	port = &mcf_ports[co->index].port;
	if (port->membase == 0)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

/****************************************************************************/

static struct uart_driver mcf_driver;

static struct console mcf_console = {
	.name		= "ttyS",
	.write		= mcf_console_write,
	.device		= uart_console_device,
	.setup		= mcf_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &mcf_driver,
};

static int __init mcf_console_init(void)
{
	register_console(&mcf_console);
	return 0;
}

console_initcall(mcf_console_init);

#define	MCF_CONSOLE	&mcf_console

/****************************************************************************/
#else
/****************************************************************************/

#define	MCF_CONSOLE	NULL

/****************************************************************************/
#endif /* CONFIG_MCF_CONSOLE */
/****************************************************************************/

/*
 *	Define the mcf UART driver structure.
 */
static struct uart_driver mcf_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "mcf",
	.dev_name	= "ttyS",
	.major		= TTY_MAJOR,
	.minor		= 64,
	.nr		= MCF_MAXPORTS,
	.cons		= MCF_CONSOLE,
};

/****************************************************************************/

static int __devinit mcf_probe(struct platform_device *pdev)
{
	struct mcf_platform_uart *platp = pdev->dev.platform_data;
	struct uart_port *port;
	int i;

	for (i = 0; ((i < MCF_MAXPORTS) && (platp[i].mapbase)); i++) {
		port = &mcf_ports[i].port;

		port->line = i;
		port->type = PORT_MCF;
		port->mapbase = platp[i].mapbase;
		port->membase = (platp[i].membase) ? platp[i].membase :
			(unsigned char __iomem *) platp[i].mapbase;
		port->iotype = SERIAL_IO_MEM;
		port->irq = platp[i].irq;
		port->uartclk = MCF_BUSCLK;
		port->ops = &mcf_uart_ops;
		port->flags = ASYNC_BOOT_AUTOCONF;

		uart_add_one_port(&mcf_driver, port);
	}

	return 0;
}

/****************************************************************************/

static int mcf_remove(struct platform_device *pdev)
{
	struct uart_port *port;
	int i;

	for (i = 0; (i < MCF_MAXPORTS); i++) {
		port = &mcf_ports[i].port;
		if (port)
			uart_remove_one_port(&mcf_driver, port);
	}

	return 0;
}

/****************************************************************************/

static struct platform_driver mcf_platform_driver = {
	.probe		= mcf_probe,
	.remove		= __devexit_p(mcf_remove),
	.driver		= {
		.name	= "mcfuart",
		.owner	= THIS_MODULE,
	},
};

/****************************************************************************/

static int __init mcf_init(void)
{
	int rc;

	printk("ColdFire internal UART serial driver\n");

	rc = uart_register_driver(&mcf_driver);
	if (rc)
		return rc;
	rc = platform_driver_register(&mcf_platform_driver);
	if (rc)
		return rc;
	return 0;
}

/****************************************************************************/

static void __exit mcf_exit(void)
{
	platform_driver_unregister(&mcf_platform_driver);
	uart_unregister_driver(&mcf_driver);
}

/****************************************************************************/

module_init(mcf_init);
module_exit(mcf_exit);

MODULE_AUTHOR("Greg Ungerer <gerg@snapgear.com>");
MODULE_DESCRIPTION("Freescale ColdFire UART driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mcfuart");

/****************************************************************************/
