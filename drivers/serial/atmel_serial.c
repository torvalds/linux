/*
 *  linux/drivers/char/at91_serial.c
 *
 *  Driver for Atmel AT91 / AT32 Serial ports
 *  Copyright (C) 2003 Rick Bronson
 *
 *  Based on drivers/char/serial_sa1100.c, by Deep Blue Solutions Ltd.
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/tty_flip.h>
#include <linux/platform_device.h>

#include <asm/io.h>

#include <asm/mach/serial_at91.h>
#include <asm/arch/board.h>
#include <asm/arch/at91_pdc.h>
#ifdef CONFIG_ARM
#include <asm/arch/system.h>
#include <asm/arch/gpio.h>
#endif

#include "atmel_serial.h"

#if defined(CONFIG_SERIAL_ATMEL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#ifdef CONFIG_SERIAL_ATMEL_TTYAT

/* Use device name ttyAT, major 204 and minor 154-169.  This is necessary if we
 * should coexist with the 8250 driver, such as if we have an external 16C550
 * UART. */
#define SERIAL_ATMEL_MAJOR	204
#define MINOR_START		154
#define ATMEL_DEVICENAME	"ttyAT"

#else

/* Use device name ttyS, major 4, minor 64-68.  This is the usual serial port
 * name, but it is legally reserved for the 8250 driver. */
#define SERIAL_ATMEL_MAJOR	TTY_MAJOR
#define MINOR_START		64
#define ATMEL_DEVICENAME	"ttyS"

#endif

#define ATMEL_ISR_PASS_LIMIT	256

#define UART_PUT_CR(port,v)	writel(v, (port)->membase + ATMEL_US_CR)
#define UART_GET_MR(port)	readl((port)->membase + ATMEL_US_MR)
#define UART_PUT_MR(port,v)	writel(v, (port)->membase + ATMEL_US_MR)
#define UART_PUT_IER(port,v)	writel(v, (port)->membase + ATMEL_US_IER)
#define UART_PUT_IDR(port,v)	writel(v, (port)->membase + ATMEL_US_IDR)
#define UART_GET_IMR(port)	readl((port)->membase + ATMEL_US_IMR)
#define UART_GET_CSR(port)	readl((port)->membase + ATMEL_US_CSR)
#define UART_GET_CHAR(port)	readl((port)->membase + ATMEL_US_RHR)
#define UART_PUT_CHAR(port,v)	writel(v, (port)->membase + ATMEL_US_THR)
#define UART_GET_BRGR(port)	readl((port)->membase + ATMEL_US_BRGR)
#define UART_PUT_BRGR(port,v)	writel(v, (port)->membase + ATMEL_US_BRGR)
#define UART_PUT_RTOR(port,v)	writel(v, (port)->membase + ATMEL_US_RTOR)

// #define UART_GET_CR(port)	readl((port)->membase + ATMEL_US_CR)		// is write-only

 /* PDC registers */
#define UART_PUT_PTCR(port,v)	writel(v, (port)->membase + ATMEL_PDC_PTCR)
#define UART_GET_PTSR(port)	readl((port)->membase + ATMEL_PDC_PTSR)

#define UART_PUT_RPR(port,v)	writel(v, (port)->membase + ATMEL_PDC_RPR)
#define UART_GET_RPR(port)	readl((port)->membase + ATMEL_PDC_RPR)
#define UART_PUT_RCR(port,v)	writel(v, (port)->membase + ATMEL_PDC_RCR)
#define UART_PUT_RNPR(port,v)	writel(v, (port)->membase + ATMEL_PDC_RNPR)
#define UART_PUT_RNCR(port,v)	writel(v, (port)->membase + ATMEL_PDC_RNCR)

#define UART_PUT_TPR(port,v)	writel(v, (port)->membase + ATMEL_PDC_TPR)
#define UART_PUT_TCR(port,v)	writel(v, (port)->membase + ATMEL_PDC_TCR)
//#define UART_PUT_TNPR(port,v)	writel(v, (port)->membase + ATMEL_PDC_TNPR)
//#define UART_PUT_TNCR(port,v)	writel(v, (port)->membase + ATMEL_PDC_TNCR)

static int (*atmel_open_hook)(struct uart_port *);
static void (*atmel_close_hook)(struct uart_port *);

/*
 * We wrap our port structure around the generic uart_port.
 */
struct atmel_uart_port {
	struct uart_port	uart;		/* uart */
	struct clk		*clk;		/* uart clock */
	unsigned short		suspended;	/* is port suspended? */
};

static struct atmel_uart_port atmel_ports[ATMEL_MAX_UART];

#ifdef SUPPORT_SYSRQ
static struct console atmel_console;
#endif

/*
 * Return TIOCSER_TEMT when transmitter FIFO and Shift register is empty.
 */
static u_int atmel_tx_empty(struct uart_port *port)
{
	return (UART_GET_CSR(port) & ATMEL_US_TXEMPTY) ? TIOCSER_TEMT : 0;
}

/*
 * Set state of the modem control output lines
 */
static void atmel_set_mctrl(struct uart_port *port, u_int mctrl)
{
	unsigned int control = 0;
	unsigned int mode;

#ifdef CONFIG_ARM
	if (cpu_is_at91rm9200()) {
		/*
		 * AT91RM9200 Errata #39: RTS0 is not internally connected to PA21.
		 *  We need to drive the pin manually.
		 */
		if (port->mapbase == AT91RM9200_BASE_US0) {
			if (mctrl & TIOCM_RTS)
				at91_set_gpio_value(AT91_PIN_PA21, 0);
			else
				at91_set_gpio_value(AT91_PIN_PA21, 1);
		}
	}
#endif

	if (mctrl & TIOCM_RTS)
		control |= ATMEL_US_RTSEN;
	else
		control |= ATMEL_US_RTSDIS;

	if (mctrl & TIOCM_DTR)
		control |= ATMEL_US_DTREN;
	else
		control |= ATMEL_US_DTRDIS;

	UART_PUT_CR(port, control);

	/* Local loopback mode? */
	mode = UART_GET_MR(port) & ~ATMEL_US_CHMODE;
	if (mctrl & TIOCM_LOOP)
		mode |= ATMEL_US_CHMODE_LOC_LOOP;
	else
		mode |= ATMEL_US_CHMODE_NORMAL;
	UART_PUT_MR(port, mode);
}

/*
 * Get state of the modem control input lines
 */
static u_int atmel_get_mctrl(struct uart_port *port)
{
	unsigned int status, ret = 0;

	status = UART_GET_CSR(port);

	/*
	 * The control signals are active low.
	 */
	if (!(status & ATMEL_US_DCD))
		ret |= TIOCM_CD;
	if (!(status & ATMEL_US_CTS))
		ret |= TIOCM_CTS;
	if (!(status & ATMEL_US_DSR))
		ret |= TIOCM_DSR;
	if (!(status & ATMEL_US_RI))
		ret |= TIOCM_RI;

	return ret;
}

/*
 * Stop transmitting.
 */
static void atmel_stop_tx(struct uart_port *port)
{
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	UART_PUT_IDR(port, ATMEL_US_TXRDY);
}

/*
 * Start transmitting.
 */
static void atmel_start_tx(struct uart_port *port)
{
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	UART_PUT_IER(port, ATMEL_US_TXRDY);
}

/*
 * Stop receiving - port is in process of being closed.
 */
static void atmel_stop_rx(struct uart_port *port)
{
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	UART_PUT_IDR(port, ATMEL_US_RXRDY);
}

/*
 * Enable modem status interrupts
 */
static void atmel_enable_ms(struct uart_port *port)
{
	UART_PUT_IER(port, ATMEL_US_RIIC | ATMEL_US_DSRIC | ATMEL_US_DCDIC | ATMEL_US_CTSIC);
}

/*
 * Control the transmission of a break signal
 */
static void atmel_break_ctl(struct uart_port *port, int break_state)
{
	if (break_state != 0)
		UART_PUT_CR(port, ATMEL_US_STTBRK);	/* start break */
	else
		UART_PUT_CR(port, ATMEL_US_STPBRK);	/* stop break */
}

/*
 * Characters received (called from interrupt handler)
 */
static void atmel_rx_chars(struct uart_port *port)
{
	struct tty_struct *tty = port->info->tty;
	unsigned int status, ch, flg;

	status = UART_GET_CSR(port);
	while (status & ATMEL_US_RXRDY) {
		ch = UART_GET_CHAR(port);

		port->icount.rx++;

		flg = TTY_NORMAL;

		/*
		 * note that the error handling code is
		 * out of the main execution path
		 */
		if (unlikely(status & (ATMEL_US_PARE | ATMEL_US_FRAME | ATMEL_US_OVRE | ATMEL_US_RXBRK))) {
			UART_PUT_CR(port, ATMEL_US_RSTSTA);	/* clear error */
			if (status & ATMEL_US_RXBRK) {
				status &= ~(ATMEL_US_PARE | ATMEL_US_FRAME);	/* ignore side-effect */
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			}
			if (status & ATMEL_US_PARE)
				port->icount.parity++;
			if (status & ATMEL_US_FRAME)
				port->icount.frame++;
			if (status & ATMEL_US_OVRE)
				port->icount.overrun++;

			status &= port->read_status_mask;

			if (status & ATMEL_US_RXBRK)
				flg = TTY_BREAK;
			else if (status & ATMEL_US_PARE)
				flg = TTY_PARITY;
			else if (status & ATMEL_US_FRAME)
				flg = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, status, ATMEL_US_OVRE, ch, flg);

	ignore_char:
		status = UART_GET_CSR(port);
	}

	tty_flip_buffer_push(tty);
}

/*
 * Transmit characters (called from interrupt handler)
 */
static void atmel_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->info->xmit;

	if (port->x_char) {
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		atmel_stop_tx(port);
		return;
	}

	while (UART_GET_CSR(port) & ATMEL_US_TXRDY) {
		UART_PUT_CHAR(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		atmel_stop_tx(port);
}

/*
 * Interrupt handler
 */
static irqreturn_t atmel_interrupt(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;
	unsigned int status, pending, pass_counter = 0;

	status = UART_GET_CSR(port);
	pending = status & UART_GET_IMR(port);
	while (pending) {
		/* Interrupt receive */
		if (pending & ATMEL_US_RXRDY)
			atmel_rx_chars(port);

		// TODO: All reads to CSR will clear these interrupts!
		if (pending & ATMEL_US_RIIC) port->icount.rng++;
		if (pending & ATMEL_US_DSRIC) port->icount.dsr++;
		if (pending & ATMEL_US_DCDIC)
			uart_handle_dcd_change(port, !(status & ATMEL_US_DCD));
		if (pending & ATMEL_US_CTSIC)
			uart_handle_cts_change(port, !(status & ATMEL_US_CTS));
		if (pending & (ATMEL_US_RIIC | ATMEL_US_DSRIC | ATMEL_US_DCDIC | ATMEL_US_CTSIC))
			wake_up_interruptible(&port->info->delta_msr_wait);

		/* Interrupt transmit */
		if (pending & ATMEL_US_TXRDY)
			atmel_tx_chars(port);

		if (pass_counter++ > ATMEL_ISR_PASS_LIMIT)
			break;

		status = UART_GET_CSR(port);
		pending = status & UART_GET_IMR(port);
	}
	return IRQ_HANDLED;
}

/*
 * Perform initialization and enable port for reception
 */
static int atmel_startup(struct uart_port *port)
{
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;
	int retval;

	/*
	 * Ensure that no interrupts are enabled otherwise when
	 * request_irq() is called we could get stuck trying to
	 * handle an unexpected interrupt
	 */
	UART_PUT_IDR(port, -1);

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, atmel_interrupt, IRQF_SHARED, "atmel_serial", port);
	if (retval) {
		printk("atmel_serial: atmel_startup - Can't get irq\n");
		return retval;
	}

	/*
	 * If there is a specific "open" function (to register
	 * control line interrupts)
	 */
	if (atmel_open_hook) {
		retval = atmel_open_hook(port);
		if (retval) {
			free_irq(port->irq, port);
			return retval;
		}
	}

	/*
	 * Finally, enable the serial port
	 */
	UART_PUT_CR(port, ATMEL_US_RSTSTA | ATMEL_US_RSTRX);
	UART_PUT_CR(port, ATMEL_US_TXEN | ATMEL_US_RXEN);		/* enable xmit & rcvr */

	UART_PUT_IER(port, ATMEL_US_RXRDY);		/* enable receive only */

	return 0;
}

/*
 * Disable the port
 */
static void atmel_shutdown(struct uart_port *port)
{
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	/*
	 * Disable all interrupts, port and break condition.
	 */
	UART_PUT_CR(port, ATMEL_US_RSTSTA);
	UART_PUT_IDR(port, -1);

	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, port);

	/*
	 * If there is a specific "close" function (to unregister
	 * control line interrupts)
	 */
	if (atmel_close_hook)
		atmel_close_hook(port);
}

/*
 * Power / Clock management.
 */
static void atmel_serial_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	switch (state) {
		case 0:
			/*
			 * Enable the peripheral clock for this serial port.
			 * This is called on uart_open() or a resume event.
			 */
			clk_enable(atmel_port->clk);
			break;
		case 3:
			/*
			 * Disable the peripheral clock for this serial port.
			 * This is called on uart_close() or a suspend event.
			 */
			clk_disable(atmel_port->clk);
			break;
		default:
			printk(KERN_ERR "atmel_serial: unknown pm %d\n", state);
	}
}

/*
 * Change the port parameters
 */
static void atmel_set_termios(struct uart_port *port, struct termios * termios, struct termios * old)
{
	unsigned long flags;
	unsigned int mode, imr, quot, baud;

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	/* Get current mode register */
	mode = UART_GET_MR(port) & ~(ATMEL_US_CHRL | ATMEL_US_NBSTOP | ATMEL_US_PAR);

	/* byte size */
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		mode |= ATMEL_US_CHRL_5;
		break;
	case CS6:
		mode |= ATMEL_US_CHRL_6;
		break;
	case CS7:
		mode |= ATMEL_US_CHRL_7;
		break;
	default:
		mode |= ATMEL_US_CHRL_8;
		break;
	}

	/* stop bits */
	if (termios->c_cflag & CSTOPB)
		mode |= ATMEL_US_NBSTOP_2;

	/* parity */
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & CMSPAR) {			/* Mark or Space parity */
			if (termios->c_cflag & PARODD)
				mode |= ATMEL_US_PAR_MARK;
			else
				mode |= ATMEL_US_PAR_SPACE;
		}
		else if (termios->c_cflag & PARODD)
			mode |= ATMEL_US_PAR_ODD;
		else
			mode |= ATMEL_US_PAR_EVEN;
	}
	else
		mode |= ATMEL_US_PAR_NONE;

	spin_lock_irqsave(&port->lock, flags);

	port->read_status_mask = ATMEL_US_OVRE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= (ATMEL_US_FRAME | ATMEL_US_PARE);
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= ATMEL_US_RXBRK;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= (ATMEL_US_FRAME | ATMEL_US_PARE);
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= ATMEL_US_RXBRK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= ATMEL_US_OVRE;
	}

	// TODO: Ignore all characters if CREAD is set.

	/* update the per-port timeout */
	uart_update_timeout(port, termios->c_cflag, baud);

	/* disable interrupts and drain transmitter */
	imr = UART_GET_IMR(port);	/* get interrupt mask */
	UART_PUT_IDR(port, -1);		/* disable all interrupts */
	while (!(UART_GET_CSR(port) & ATMEL_US_TXEMPTY)) { barrier(); }

	/* disable receiver and transmitter */
	UART_PUT_CR(port, ATMEL_US_TXDIS | ATMEL_US_RXDIS);

	/* set the parity, stop bits and data size */
	UART_PUT_MR(port, mode);

	/* set the baud rate */
	UART_PUT_BRGR(port, quot);
	UART_PUT_CR(port, ATMEL_US_RSTSTA | ATMEL_US_RSTRX);
	UART_PUT_CR(port, ATMEL_US_TXEN | ATMEL_US_RXEN);

	/* restore interrupts */
	UART_PUT_IER(port, imr);

	/* CTS flow-control and modem-status interrupts */
	if (UART_ENABLE_MS(port, termios->c_cflag))
		port->ops->enable_ms(port);

	spin_unlock_irqrestore(&port->lock, flags);
}

/*
 * Return string describing the specified port
 */
static const char *atmel_type(struct uart_port *port)
{
	return (port->type == PORT_ATMEL) ? "ATMEL_SERIAL" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'.
 */
static void atmel_release_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	int size = pdev->resource[0].end - pdev->resource[0].start + 1;

	release_mem_region(port->mapbase, size);

	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}
}

/*
 * Request the memory region(s) being used by 'port'.
 */
static int atmel_request_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	int size = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(port->mapbase, size, "atmel_serial"))
		return -EBUSY;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap(port->mapbase, size);
		if (port->membase == NULL) {
			release_mem_region(port->mapbase, size);
			return -ENOMEM;
		}
	}

	return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void atmel_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_ATMEL;
		atmel_request_port(port);
	}
}

/*
 * Verify the new serial_struct (for TIOCSSERIAL).
 */
static int atmel_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_ATMEL)
		ret = -EINVAL;
	if (port->irq != ser->irq)
		ret = -EINVAL;
	if (ser->io_type != SERIAL_IO_MEM)
		ret = -EINVAL;
	if (port->uartclk / 16 != ser->baud_base)
		ret = -EINVAL;
	if ((void *)port->mapbase != ser->iomem_base)
		ret = -EINVAL;
	if (port->iobase != ser->port)
		ret = -EINVAL;
	if (ser->hub6 != 0)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops atmel_pops = {
	.tx_empty	= atmel_tx_empty,
	.set_mctrl	= atmel_set_mctrl,
	.get_mctrl	= atmel_get_mctrl,
	.stop_tx	= atmel_stop_tx,
	.start_tx	= atmel_start_tx,
	.stop_rx	= atmel_stop_rx,
	.enable_ms	= atmel_enable_ms,
	.break_ctl	= atmel_break_ctl,
	.startup	= atmel_startup,
	.shutdown	= atmel_shutdown,
	.set_termios	= atmel_set_termios,
	.type		= atmel_type,
	.release_port	= atmel_release_port,
	.request_port	= atmel_request_port,
	.config_port	= atmel_config_port,
	.verify_port	= atmel_verify_port,
	.pm		= atmel_serial_pm,
};

/*
 * Configure the port from the platform device resource info.
 */
static void __devinit atmel_init_port(struct atmel_uart_port *atmel_port, struct platform_device *pdev)
{
	struct uart_port *port = &atmel_port->uart;
	struct atmel_uart_data *data = pdev->dev.platform_data;

	port->iotype	= UPIO_MEM;
	port->flags     = UPF_BOOT_AUTOCONF;
	port->ops	= &atmel_pops;
	port->fifosize  = 1;
	port->line	= pdev->id;
	port->dev	= &pdev->dev;

	port->mapbase	= pdev->resource[0].start;
	port->irq	= pdev->resource[1].start;

	if (data->regs)
		/* Already mapped by setup code */
		port->membase = data->regs;
	else {
		port->flags	|= UPF_IOREMAP;
		port->membase	= NULL;
	}

	if (!atmel_port->clk) {		/* for console, the clock could already be configured */
		atmel_port->clk = clk_get(&pdev->dev, "usart");
		clk_enable(atmel_port->clk);
		port->uartclk = clk_get_rate(atmel_port->clk);
	}
}

/*
 * Register board-specific modem-control line handlers.
 */
void __init atmel_register_uart_fns(struct atmel_port_fns *fns)
{
	if (fns->enable_ms)
		atmel_pops.enable_ms = fns->enable_ms;
	if (fns->get_mctrl)
		atmel_pops.get_mctrl = fns->get_mctrl;
	if (fns->set_mctrl)
		atmel_pops.set_mctrl = fns->set_mctrl;
	atmel_open_hook		= fns->open;
	atmel_close_hook	= fns->close;
	atmel_pops.pm		= fns->pm;
	atmel_pops.set_wake	= fns->set_wake;
}


#ifdef CONFIG_SERIAL_ATMEL_CONSOLE
static void atmel_console_putchar(struct uart_port *port, int ch)
{
	while (!(UART_GET_CSR(port) & ATMEL_US_TXRDY))
		barrier();
	UART_PUT_CHAR(port, ch);
}

/*
 * Interrupts are disabled on entering
 */
static void atmel_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = &atmel_ports[co->index].uart;
	unsigned int status, imr;

	/*
	 *	First, save IMR and then disable interrupts
	 */
	imr = UART_GET_IMR(port);	/* get interrupt mask */
	UART_PUT_IDR(port, ATMEL_US_RXRDY | ATMEL_US_TXRDY);

	uart_console_write(port, s, count, atmel_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore IMR
	 */
	do {
		status = UART_GET_CSR(port);
	} while (!(status & ATMEL_US_TXRDY));
	UART_PUT_IER(port, imr);	/* set interrupts back the way they were */
}

/*
 * If the port was already initialised (eg, by a boot loader), try to determine
 * the current setup.
 */
static void __init atmel_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	unsigned int mr, quot;

// TODO: CR is a write-only register
//	unsigned int cr;
//
//	cr = UART_GET_CR(port) & (ATMEL_US_RXEN | ATMEL_US_TXEN);
//	if (cr == (ATMEL_US_RXEN | ATMEL_US_TXEN)) {
//		/* ok, the port was enabled */
//	}

	mr = UART_GET_MR(port) & ATMEL_US_CHRL;
	if (mr == ATMEL_US_CHRL_8)
		*bits = 8;
	else
		*bits = 7;

	mr = UART_GET_MR(port) & ATMEL_US_PAR;
	if (mr == ATMEL_US_PAR_EVEN)
		*parity = 'e';
	else if (mr == ATMEL_US_PAR_ODD)
		*parity = 'o';

	/*
	 * The serial core only rounds down when matching this to a
	 * supported baud rate. Make sure we don't end up slightly
	 * lower than one of those, as it would make us fall through
	 * to a much lower baud rate than we really want.
	 */
	quot = UART_GET_BRGR(port);
	*baud = port->uartclk / (16 * (quot - 1));
}

static int __init atmel_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &atmel_ports[co->index].uart;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (port->membase == 0)		/* Port not initialized yet - delay setup */
		return -ENODEV;

	UART_PUT_IDR(port, -1);				/* disable interrupts */
	UART_PUT_CR(port, ATMEL_US_RSTSTA | ATMEL_US_RSTRX);
	UART_PUT_CR(port, ATMEL_US_TXEN | ATMEL_US_RXEN);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		atmel_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver atmel_uart;

static struct console atmel_console = {
	.name		= ATMEL_DEVICENAME,
	.write		= atmel_console_write,
	.device		= uart_console_device,
	.setup		= atmel_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &atmel_uart,
};

#define ATMEL_CONSOLE_DEVICE	&atmel_console

/*
 * Early console initialization (before VM subsystem initialized).
 */
static int __init atmel_console_init(void)
{
	if (atmel_default_console_device) {
		add_preferred_console(ATMEL_DEVICENAME, atmel_default_console_device->id, NULL);
		atmel_init_port(&(atmel_ports[atmel_default_console_device->id]), atmel_default_console_device);
		register_console(&atmel_console);
	}

	return 0;
}
console_initcall(atmel_console_init);

/*
 * Late console initialization.
 */
static int __init atmel_late_console_init(void)
{
	if (atmel_default_console_device && !(atmel_console.flags & CON_ENABLED))
		register_console(&atmel_console);

	return 0;
}
core_initcall(atmel_late_console_init);

#else
#define ATMEL_CONSOLE_DEVICE	NULL
#endif

static struct uart_driver atmel_uart = {
	.owner			= THIS_MODULE,
	.driver_name		= "atmel_serial",
	.dev_name		= ATMEL_DEVICENAME,
	.major			= SERIAL_ATMEL_MAJOR,
	.minor			= MINOR_START,
	.nr			= ATMEL_MAX_UART,
	.cons			= ATMEL_CONSOLE_DEVICE,
};

#ifdef CONFIG_PM
static int atmel_serial_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	if (device_may_wakeup(&pdev->dev) && !at91_suspend_entering_slow_clock())
		enable_irq_wake(port->irq);
	else {
		disable_irq_wake(port->irq);
		uart_suspend_port(&atmel_uart, port);
		atmel_port->suspended = 1;
	}

	return 0;
}

static int atmel_serial_resume(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;

	if (atmel_port->suspended) {
		uart_resume_port(&atmel_uart, port);
		atmel_port->suspended = 0;
	}

	return 0;
}
#else
#define atmel_serial_suspend NULL
#define atmel_serial_resume NULL
#endif

static int __devinit atmel_serial_probe(struct platform_device *pdev)
{
	struct atmel_uart_port *port;
	int ret;

	port = &atmel_ports[pdev->id];
	atmel_init_port(port, pdev);

	ret = uart_add_one_port(&atmel_uart, &port->uart);
	if (!ret) {
		device_init_wakeup(&pdev->dev, 1);
		platform_set_drvdata(pdev, port);
	}

	return ret;
}

static int __devexit atmel_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct atmel_uart_port *atmel_port = (struct atmel_uart_port *) port;
	int ret = 0;

	clk_disable(atmel_port->clk);
	clk_put(atmel_port->clk);

	device_init_wakeup(&pdev->dev, 0);
	platform_set_drvdata(pdev, NULL);

	if (port) {
		ret = uart_remove_one_port(&atmel_uart, port);
		kfree(port);
	}

	return ret;
}

static struct platform_driver atmel_serial_driver = {
	.probe		= atmel_serial_probe,
	.remove		= __devexit_p(atmel_serial_remove),
	.suspend	= atmel_serial_suspend,
	.resume		= atmel_serial_resume,
	.driver		= {
		.name	= "atmel_usart",
		.owner	= THIS_MODULE,
	},
};

static int __init atmel_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&atmel_uart);
	if (ret)
		return ret;

	ret = platform_driver_register(&atmel_serial_driver);
	if (ret)
		uart_unregister_driver(&atmel_uart);

	return ret;
}

static void __exit atmel_serial_exit(void)
{
	platform_driver_unregister(&atmel_serial_driver);
	uart_unregister_driver(&atmel_uart);
}

module_init(atmel_serial_init);
module_exit(atmel_serial_exit);

MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("Atmel AT91 / AT32 serial port driver");
MODULE_LICENSE("GPL");
