/*
 *  Driver for NEC VR4100 series Serial Interface Unit.
 *
 *  Copyright (C) 2004-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  Based on drivers/serial/8250.c, by Russell King.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if defined(CONFIG_SERIAL_VR41XX_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/console.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <asm/io.h>
#include <asm/vr41xx/siu.h>
#include <asm/vr41xx/vr41xx.h>

#define SIU_BAUD_BASE	1152000
#define SIU_MAJOR	204
#define SIU_MINOR_BASE	82

#define RX_MAX_COUNT	256
#define TX_MAX_COUNT	15

#define SIUIRSEL	0x08
 #define TMICMODE	0x20
 #define TMICTX		0x10
 #define IRMSEL		0x0c
 #define IRMSEL_HP	0x08
 #define IRMSEL_TEMIC	0x04
 #define IRMSEL_SHARP	0x00
 #define IRUSESEL	0x02
 #define SIRSEL		0x01

static struct uart_port siu_uart_ports[SIU_PORTS_MAX] = {
	[0 ... SIU_PORTS_MAX-1] = {
		.lock	= __SPIN_LOCK_UNLOCKED(siu_uart_ports->lock),
		.irq	= -1,
	},
};

#ifdef CONFIG_SERIAL_VR41XX_CONSOLE
static uint8_t lsr_break_flag[SIU_PORTS_MAX];
#endif

#define siu_read(port, offset)		readb((port)->membase + (offset))
#define siu_write(port, offset, value)	writeb((value), (port)->membase + (offset))

void vr41xx_select_siu_interface(siu_interface_t interface)
{
	struct uart_port *port;
	unsigned long flags;
	uint8_t irsel;

	port = &siu_uart_ports[0];

	spin_lock_irqsave(&port->lock, flags);

	irsel = siu_read(port, SIUIRSEL);
	if (interface == SIU_INTERFACE_IRDA)
		irsel |= SIRSEL;
	else
		irsel &= ~SIRSEL;
	siu_write(port, SIUIRSEL, irsel);

	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL_GPL(vr41xx_select_siu_interface);

void vr41xx_use_irda(irda_use_t use)
{
	struct uart_port *port;
	unsigned long flags;
	uint8_t irsel;

	port = &siu_uart_ports[0];

	spin_lock_irqsave(&port->lock, flags);

	irsel = siu_read(port, SIUIRSEL);
	if (use == FIR_USE_IRDA)
		irsel |= IRUSESEL;
	else
		irsel &= ~IRUSESEL;
	siu_write(port, SIUIRSEL, irsel);

	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL_GPL(vr41xx_use_irda);

void vr41xx_select_irda_module(irda_module_t module, irda_speed_t speed)
{
	struct uart_port *port;
	unsigned long flags;
	uint8_t irsel;

	port = &siu_uart_ports[0];

	spin_lock_irqsave(&port->lock, flags);

	irsel = siu_read(port, SIUIRSEL);
	irsel &= ~(IRMSEL | TMICTX | TMICMODE);
	switch (module) {
	case SHARP_IRDA:
		irsel |= IRMSEL_SHARP;
		break;
	case TEMIC_IRDA:
		irsel |= IRMSEL_TEMIC | TMICMODE;
		if (speed == IRDA_TX_4MBPS)
			irsel |= TMICTX;
		break;
	case HP_IRDA:
		irsel |= IRMSEL_HP;
		break;
	default:
		break;
	}
	siu_write(port, SIUIRSEL, irsel);

	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL_GPL(vr41xx_select_irda_module);

static inline void siu_clear_fifo(struct uart_port *port)
{
	siu_write(port, UART_FCR, UART_FCR_ENABLE_FIFO);
	siu_write(port, UART_FCR, UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR |
	                          UART_FCR_CLEAR_XMIT);
	siu_write(port, UART_FCR, 0);
}

static inline unsigned long siu_port_size(struct uart_port *port)
{
	switch (port->type) {
	case PORT_VR41XX_SIU:
		return 11UL;
	case PORT_VR41XX_DSIU:
		return 8UL;
	}

	return 0;
}

static inline unsigned int siu_check_type(struct uart_port *port)
{
	if (port->line == 0)
		return PORT_VR41XX_SIU;
	if (port->line == 1 && port->irq != -1)
		return PORT_VR41XX_DSIU;

	return PORT_UNKNOWN;
}

static inline const char *siu_type_name(struct uart_port *port)
{
	switch (port->type) {
	case PORT_VR41XX_SIU:
		return "SIU";
	case PORT_VR41XX_DSIU:
		return "DSIU";
	}

	return NULL;
}

static unsigned int siu_tx_empty(struct uart_port *port)
{
	uint8_t lsr;

	lsr = siu_read(port, UART_LSR);
	if (lsr & UART_LSR_TEMT)
		return TIOCSER_TEMT;

	return 0;
}

static void siu_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	uint8_t mcr = 0;

	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	siu_write(port, UART_MCR, mcr);
}

static unsigned int siu_get_mctrl(struct uart_port *port)
{
	uint8_t msr;
	unsigned int mctrl = 0;

	msr = siu_read(port, UART_MSR);
	if (msr & UART_MSR_DCD)
		mctrl |= TIOCM_CAR;
	if (msr & UART_MSR_RI)
		mctrl |= TIOCM_RNG;
	if (msr & UART_MSR_DSR)
		mctrl |= TIOCM_DSR;
	if (msr & UART_MSR_CTS)
		mctrl |= TIOCM_CTS;

	return mctrl;
}

static void siu_stop_tx(struct uart_port *port)
{
	unsigned long flags;
	uint8_t ier;

	spin_lock_irqsave(&port->lock, flags);

	ier = siu_read(port, UART_IER);
	ier &= ~UART_IER_THRI;
	siu_write(port, UART_IER, ier);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void siu_start_tx(struct uart_port *port)
{
	unsigned long flags;
	uint8_t ier;

	spin_lock_irqsave(&port->lock, flags);

	ier = siu_read(port, UART_IER);
	ier |= UART_IER_THRI;
	siu_write(port, UART_IER, ier);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void siu_stop_rx(struct uart_port *port)
{
	unsigned long flags;
	uint8_t ier;

	spin_lock_irqsave(&port->lock, flags);

	ier = siu_read(port, UART_IER);
	ier &= ~UART_IER_RLSI;
	siu_write(port, UART_IER, ier);

	port->read_status_mask &= ~UART_LSR_DR;

	spin_unlock_irqrestore(&port->lock, flags);
}

static void siu_enable_ms(struct uart_port *port)
{
	unsigned long flags;
	uint8_t ier;

	spin_lock_irqsave(&port->lock, flags);

	ier = siu_read(port, UART_IER);
	ier |= UART_IER_MSI;
	siu_write(port, UART_IER, ier);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void siu_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	uint8_t lcr;

	spin_lock_irqsave(&port->lock, flags);

	lcr = siu_read(port, UART_LCR);
	if (ctl == -1)
		lcr |= UART_LCR_SBC;
	else
		lcr &= ~UART_LCR_SBC;
	siu_write(port, UART_LCR, lcr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static inline void receive_chars(struct uart_port *port, uint8_t *status)
{
	struct tty_struct *tty;
	uint8_t lsr, ch;
	char flag;
	int max_count = RX_MAX_COUNT;

	tty = port->info->port.tty;
	lsr = *status;

	do {
		ch = siu_read(port, UART_RX);
		port->icount.rx++;
		flag = TTY_NORMAL;

#ifdef CONFIG_SERIAL_VR41XX_CONSOLE
		lsr |= lsr_break_flag[port->line];
		lsr_break_flag[port->line] = 0;
#endif
		if (unlikely(lsr & (UART_LSR_BI | UART_LSR_FE |
		                    UART_LSR_PE | UART_LSR_OE))) {
			if (lsr & UART_LSR_BI) {
				lsr &= ~(UART_LSR_FE | UART_LSR_PE);
				port->icount.brk++;

				if (uart_handle_break(port))
					goto ignore_char;
			}

			if (lsr & UART_LSR_FE)
				port->icount.frame++;
			if (lsr & UART_LSR_PE)
				port->icount.parity++;
			if (lsr & UART_LSR_OE)
				port->icount.overrun++;

			lsr &= port->read_status_mask;
			if (lsr & UART_LSR_BI)
				flag = TTY_BREAK;
			if (lsr & UART_LSR_FE)
				flag = TTY_FRAME;
			if (lsr & UART_LSR_PE)
				flag = TTY_PARITY;
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, lsr, UART_LSR_OE, ch, flag);

	ignore_char:
		lsr = siu_read(port, UART_LSR);
	} while ((lsr & UART_LSR_DR) && (max_count-- > 0));

	tty_flip_buffer_push(tty);

	*status = lsr;
}

static inline void check_modem_status(struct uart_port *port)
{
	uint8_t msr;

	msr = siu_read(port, UART_MSR);
	if ((msr & UART_MSR_ANY_DELTA) == 0)
		return;
	if (msr & UART_MSR_DDCD)
		uart_handle_dcd_change(port, msr & UART_MSR_DCD);
	if (msr & UART_MSR_TERI)
		port->icount.rng++;
	if (msr & UART_MSR_DDSR)
		port->icount.dsr++;
	if (msr & UART_MSR_DCTS)
		uart_handle_cts_change(port, msr & UART_MSR_CTS);

	wake_up_interruptible(&port->info->delta_msr_wait);
}

static inline void transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit;
	int max_count = TX_MAX_COUNT;

	xmit = &port->info->xmit;

	if (port->x_char) {
		siu_write(port, UART_TX, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		siu_stop_tx(port);
		return;
	}

	do {
		siu_write(port, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (max_count-- > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		siu_stop_tx(port);
}

static irqreturn_t siu_interrupt(int irq, void *dev_id)
{
	struct uart_port *port;
	uint8_t iir, lsr;

	port = (struct uart_port *)dev_id;

	iir = siu_read(port, UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return IRQ_NONE;

	lsr = siu_read(port, UART_LSR);
	if (lsr & UART_LSR_DR)
		receive_chars(port, &lsr);

	check_modem_status(port);

	if (lsr & UART_LSR_THRE)
		transmit_chars(port);

	return IRQ_HANDLED;
}

static int siu_startup(struct uart_port *port)
{
	int retval;

	if (port->membase == NULL)
		return -ENODEV;

	siu_clear_fifo(port);

	(void)siu_read(port, UART_LSR);
	(void)siu_read(port, UART_RX);
	(void)siu_read(port, UART_IIR);
	(void)siu_read(port, UART_MSR);

	if (siu_read(port, UART_LSR) == 0xff)
		return -ENODEV;

	retval = request_irq(port->irq, siu_interrupt, 0, siu_type_name(port), port);
	if (retval)
		return retval;

	if (port->type == PORT_VR41XX_DSIU)
		vr41xx_enable_dsiuint(DSIUINT_ALL);

	siu_write(port, UART_LCR, UART_LCR_WLEN8);

	spin_lock_irq(&port->lock);
	siu_set_mctrl(port, port->mctrl);
	spin_unlock_irq(&port->lock);

	siu_write(port, UART_IER, UART_IER_RLSI | UART_IER_RDI);

	(void)siu_read(port, UART_LSR);
	(void)siu_read(port, UART_RX);
	(void)siu_read(port, UART_IIR);
	(void)siu_read(port, UART_MSR);

	return 0;
}

static void siu_shutdown(struct uart_port *port)
{
	unsigned long flags;
	uint8_t lcr;

	siu_write(port, UART_IER, 0);

	spin_lock_irqsave(&port->lock, flags);

	port->mctrl &= ~TIOCM_OUT2;
	siu_set_mctrl(port, port->mctrl);

	spin_unlock_irqrestore(&port->lock, flags);

	lcr = siu_read(port, UART_LCR);
	lcr &= ~UART_LCR_SBC;
	siu_write(port, UART_LCR, lcr);

	siu_clear_fifo(port);

	(void)siu_read(port, UART_RX);

	if (port->type == PORT_VR41XX_DSIU)
		vr41xx_disable_dsiuint(DSIUINT_ALL);

	free_irq(port->irq, port);
}

static void siu_set_termios(struct uart_port *port, struct ktermios *new,
                            struct ktermios *old)
{
	tcflag_t c_cflag, c_iflag;
	uint8_t lcr, fcr, ier;
	unsigned int baud, quot;
	unsigned long flags;

	c_cflag = new->c_cflag;
	switch (c_cflag & CSIZE) {
	case CS5:
		lcr = UART_LCR_WLEN5;
		break;
	case CS6:
		lcr = UART_LCR_WLEN6;
		break;
	case CS7:
		lcr = UART_LCR_WLEN7;
		break;
	default:
		lcr = UART_LCR_WLEN8;
		break;
	}

	if (c_cflag & CSTOPB)
		lcr |= UART_LCR_STOP;
	if (c_cflag & PARENB)
		lcr |= UART_LCR_PARITY;
	if ((c_cflag & PARODD) != PARODD)
		lcr |= UART_LCR_EPAR;
	if (c_cflag & CMSPAR)
		lcr |= UART_LCR_SPAR;

	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);

	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_R_TRIG_10;

	spin_lock_irqsave(&port->lock, flags);

	uart_update_timeout(port, c_cflag, baud);

	c_iflag = new->c_iflag;

	port->read_status_mask = UART_LSR_THRE | UART_LSR_OE | UART_LSR_DR;
	if (c_iflag & INPCK)
		port->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UART_LSR_BI;

	port->ignore_status_mask = 0;
	if (c_iflag & IGNPAR)
		port->ignore_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (c_iflag & IGNBRK) {
		port->ignore_status_mask |= UART_LSR_BI;
		if (c_iflag & IGNPAR)
			port->ignore_status_mask |= UART_LSR_OE;
	}

	if ((c_cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_LSR_DR;

	ier = siu_read(port, UART_IER);
	ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(port, c_cflag))
		ier |= UART_IER_MSI;
	siu_write(port, UART_IER, ier);

	siu_write(port, UART_LCR, lcr | UART_LCR_DLAB);

	siu_write(port, UART_DLL, (uint8_t)quot);
	siu_write(port, UART_DLM, (uint8_t)(quot >> 8));

	siu_write(port, UART_LCR, lcr);

	siu_write(port, UART_FCR, fcr);

	siu_set_mctrl(port, port->mctrl);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void siu_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
	switch (state) {
	case 0:
		switch (port->type) {
		case PORT_VR41XX_SIU:
			vr41xx_supply_clock(SIU_CLOCK);
			break;
		case PORT_VR41XX_DSIU:
			vr41xx_supply_clock(DSIU_CLOCK);
			break;
		}
		break;
	case 3:
		switch (port->type) {
		case PORT_VR41XX_SIU:
			vr41xx_mask_clock(SIU_CLOCK);
			break;
		case PORT_VR41XX_DSIU:
			vr41xx_mask_clock(DSIU_CLOCK);
			break;
		}
		break;
	}
}

static const char *siu_type(struct uart_port *port)
{
	return siu_type_name(port);
}

static void siu_release_port(struct uart_port *port)
{
	unsigned long size;

	if (port->flags	& UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	size = siu_port_size(port);
	release_mem_region(port->mapbase, size);
}

static int siu_request_port(struct uart_port *port)
{
	unsigned long size;
	struct resource *res;

	size = siu_port_size(port);
	res = request_mem_region(port->mapbase, size, siu_type_name(port));
	if (res == NULL)
		return -EBUSY;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap(port->mapbase, size);
		if (port->membase == NULL) {
			release_resource(res);
			return -ENOMEM;
		}
	}

	return 0;
}

static void siu_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = siu_check_type(port);
		(void)siu_request_port(port);
	}
}

static int siu_verify_port(struct uart_port *port, struct serial_struct *serial)
{
	if (port->type != PORT_VR41XX_SIU && port->type != PORT_VR41XX_DSIU)
		return -EINVAL;
	if (port->irq != serial->irq)
		return -EINVAL;
	if (port->iotype != serial->io_type)
		return -EINVAL;
	if (port->mapbase != (unsigned long)serial->iomem_base)
		return -EINVAL;

	return 0;
}

static struct uart_ops siu_uart_ops = {
	.tx_empty	= siu_tx_empty,
	.set_mctrl	= siu_set_mctrl,
	.get_mctrl	= siu_get_mctrl,
	.stop_tx	= siu_stop_tx,
	.start_tx	= siu_start_tx,
	.stop_rx	= siu_stop_rx,
	.enable_ms	= siu_enable_ms,
	.break_ctl	= siu_break_ctl,
	.startup	= siu_startup,
	.shutdown	= siu_shutdown,
	.set_termios	= siu_set_termios,
	.pm		= siu_pm,
	.type		= siu_type,
	.release_port	= siu_release_port,
	.request_port	= siu_request_port,
	.config_port	= siu_config_port,
	.verify_port	= siu_verify_port,
};

static int siu_init_ports(struct platform_device *pdev)
{
	struct uart_port *port;
	struct resource *res;
	int *type = pdev->dev.platform_data;
	int i;

	if (!type)
		return 0;

	port = siu_uart_ports;
	for (i = 0; i < SIU_PORTS_MAX; i++) {
		port->type = type[i];
		if (port->type == PORT_UNKNOWN)
			continue;
		port->irq = platform_get_irq(pdev, i);
		port->uartclk = SIU_BAUD_BASE * 16;
		port->fifosize = 16;
		port->regshift = 0;
		port->iotype = UPIO_MEM;
		port->flags = UPF_IOREMAP | UPF_BOOT_AUTOCONF;
		port->line = i;
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		port->mapbase = res->start;
		port++;
	}

	return i;
}

#ifdef CONFIG_SERIAL_VR41XX_CONSOLE

#define BOTH_EMPTY	(UART_LSR_TEMT | UART_LSR_THRE)

static void wait_for_xmitr(struct uart_port *port)
{
	int timeout = 10000;
	uint8_t lsr, msr;

	do {
		lsr = siu_read(port, UART_LSR);
		if (lsr & UART_LSR_BI)
			lsr_break_flag[port->line] = UART_LSR_BI;

		if ((lsr & BOTH_EMPTY) == BOTH_EMPTY)
			break;
	} while (timeout-- > 0);

	if (port->flags & UPF_CONS_FLOW) {
		timeout = 1000000;

		do {
			msr = siu_read(port, UART_MSR);
			if ((msr & UART_MSR_CTS) != 0)
				break;
		} while (timeout-- > 0);
	}
}

static void siu_console_putchar(struct uart_port *port, int ch)
{
	wait_for_xmitr(port);
	siu_write(port, UART_TX, ch);
}

static void siu_console_write(struct console *con, const char *s, unsigned count)
{
	struct uart_port *port;
	uint8_t ier;

	port = &siu_uart_ports[con->index];

	ier = siu_read(port, UART_IER);
	siu_write(port, UART_IER, 0);

	uart_console_write(port, s, count, siu_console_putchar);

	wait_for_xmitr(port);
	siu_write(port, UART_IER, ier);
}

static int __init siu_console_setup(struct console *con, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int parity = 'n';
	int bits = 8;
	int flow = 'n';

	if (con->index >= SIU_PORTS_MAX)
		con->index = 0;

	port = &siu_uart_ports[con->index];
	if (port->membase == NULL) {
		if (port->mapbase == 0)
			return -ENODEV;
		port->membase = ioremap(port->mapbase, siu_port_size(port));
	}

	if (port->type == PORT_VR41XX_SIU)
		vr41xx_select_siu_interface(SIU_INTERFACE_RS232C);

	if (options != NULL)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, con, baud, parity, bits, flow);
}

static struct uart_driver siu_uart_driver;

static struct console siu_console = {
	.name	= "ttyVR",
	.write	= siu_console_write,
	.device	= uart_console_device,
	.setup	= siu_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &siu_uart_driver,
};

static int __devinit siu_console_init(void)
{
	struct uart_port *port;
	int i;

	for (i = 0; i < SIU_PORTS_MAX; i++) {
		port = &siu_uart_ports[i];
		port->ops = &siu_uart_ops;
	}

	register_console(&siu_console);

	return 0;
}

console_initcall(siu_console_init);

void __init vr41xx_siu_early_setup(struct uart_port *port)
{
	if (port->type == PORT_UNKNOWN)
		return;

	siu_uart_ports[port->line].line = port->line;
	siu_uart_ports[port->line].type = port->type;
	siu_uart_ports[port->line].uartclk = SIU_BAUD_BASE * 16;
	siu_uart_ports[port->line].mapbase = port->mapbase;
	siu_uart_ports[port->line].mapbase = port->mapbase;
	siu_uart_ports[port->line].ops = &siu_uart_ops;
}

#define SERIAL_VR41XX_CONSOLE	&siu_console
#else
#define SERIAL_VR41XX_CONSOLE	NULL
#endif

static struct uart_driver siu_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "SIU",
	.dev_name	= "ttyVR",
	.major		= SIU_MAJOR,
	.minor		= SIU_MINOR_BASE,
	.cons		= SERIAL_VR41XX_CONSOLE,
};

static int __devinit siu_probe(struct platform_device *dev)
{
	struct uart_port *port;
	int num, i, retval;

	num = siu_init_ports(dev);
	if (num <= 0)
		return -ENODEV;

	siu_uart_driver.nr = num;
	retval = uart_register_driver(&siu_uart_driver);
	if (retval)
		return retval;

	for (i = 0; i < num; i++) {
		port = &siu_uart_ports[i];
		port->ops = &siu_uart_ops;
		port->dev = &dev->dev;

		retval = uart_add_one_port(&siu_uart_driver, port);
		if (retval < 0) {
			port->dev = NULL;
			break;
		}
	}

	if (i == 0 && retval < 0) {
		uart_unregister_driver(&siu_uart_driver);
		return retval;
	}

	return 0;
}

static int __devexit siu_remove(struct platform_device *dev)
{
	struct uart_port *port;
	int i;

	for (i = 0; i < siu_uart_driver.nr; i++) {
		port = &siu_uart_ports[i];
		if (port->dev == &dev->dev) {
			uart_remove_one_port(&siu_uart_driver, port);
			port->dev = NULL;
		}
	}

	uart_unregister_driver(&siu_uart_driver);

	return 0;
}

static int siu_suspend(struct platform_device *dev, pm_message_t state)
{
	struct uart_port *port;
	int i;

	for (i = 0; i < siu_uart_driver.nr; i++) {
		port = &siu_uart_ports[i];
		if ((port->type == PORT_VR41XX_SIU ||
		     port->type == PORT_VR41XX_DSIU) && port->dev == &dev->dev)
			uart_suspend_port(&siu_uart_driver, port);

	}

	return 0;
}

static int siu_resume(struct platform_device *dev)
{
	struct uart_port *port;
	int i;

	for (i = 0; i < siu_uart_driver.nr; i++) {
		port = &siu_uart_ports[i];
		if ((port->type == PORT_VR41XX_SIU ||
		     port->type == PORT_VR41XX_DSIU) && port->dev == &dev->dev)
			uart_resume_port(&siu_uart_driver, port);
	}

	return 0;
}

static struct platform_driver siu_device_driver = {
	.probe		= siu_probe,
	.remove		= __devexit_p(siu_remove),
	.suspend	= siu_suspend,
	.resume		= siu_resume,
	.driver		= {
		.name	= "SIU",
		.owner	= THIS_MODULE,
	},
};

static int __init vr41xx_siu_init(void)
{
	return platform_driver_register(&siu_device_driver);
}

static void __exit vr41xx_siu_exit(void)
{
	platform_driver_unregister(&siu_device_driver);
}

module_init(vr41xx_siu_init);
module_exit(vr41xx_siu_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:SIU");
