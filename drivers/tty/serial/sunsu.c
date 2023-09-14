// SPDX-License-Identifier: GPL-2.0
/*
 * su.c: Small serial driver for keyboard/mouse interface on sparc32/PCI
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 1998-1999  Pete Zaitcev   (zaitcev@yahoo.com)
 *
 * This is mainly a variation of 8250.c, credits go to authors mentioned
 * therein.  In fact this driver should be merged into the generic 8250.c
 * infrastructure perhaps using a 8250_sparc.c module.
 *
 * Fixed to use tty_get_baud_rate().
 *   Theodore Ts'o <tytso@mit.edu>, 2001-Oct-12
 *
 * Converted to new 2.5.x UART layer.
 *   David S. Miller (davem@davemloft.net), 2002-Jul-29
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/slab.h>
#ifdef CONFIG_SERIO
#include <linux/serio.h>
#endif
#include <linux/serial_reg.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <linux/io.h>
#include <asm/irq.h>
#include <asm/setup.h>

#include <linux/serial_core.h>
#include <linux/sunserialcore.h>

/* We are on a NS PC87303 clocked with 24.0 MHz, which results
 * in a UART clock of 1.8462 MHz.
 */
#define SU_BASE_BAUD	(1846200 / 16)

enum su_type { SU_PORT_NONE, SU_PORT_MS, SU_PORT_KBD, SU_PORT_PORT };
static char *su_typev[] = { "su(???)", "su(mouse)", "su(kbd)", "su(serial)" };

struct serial_uart_config {
	char	*name;
	int	dfl_xmit_fifo_size;
	int	flags;
};

/*
 * Here we define the default xmit fifo size used for each type of UART.
 */
static const struct serial_uart_config uart_config[] = {
	{ "unknown",	1,	0 },
	{ "8250",	1,	0 },
	{ "16450",	1,	0 },
	{ "16550",	1,	0 },
	{ "16550A",	16,	UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "Cirrus",	1, 	0 },
	{ "ST16650",	1,	UART_CLEAR_FIFO | UART_STARTECH },
	{ "ST16650V2",	32,	UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
	{ "TI16750",	64,	UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "Startech",	1,	0 },
	{ "16C950/954",	128,	UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "ST16654",	64,	UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
	{ "XR16850",	128,	UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
	{ "RSA",	2048,	UART_CLEAR_FIFO | UART_USE_FIFO }
};

struct uart_sunsu_port {
	struct uart_port	port;
	unsigned char		acr;
	unsigned char		ier;
	unsigned short		rev;
	unsigned char		lcr;
	unsigned int		lsr_break_flag;
	unsigned int		cflag;

	/* Probing information.  */
	enum su_type		su_type;
	unsigned int		type_probed;	/* XXX Stupid */
	unsigned long		reg_size;

#ifdef CONFIG_SERIO
	struct serio		serio;
	int			serio_open;
#endif
};

static unsigned int serial_in(struct uart_sunsu_port *up, int offset)
{
	offset <<= up->port.regshift;

	switch (up->port.iotype) {
	case UPIO_HUB6:
		outb(up->port.hub6 - 1 + offset, up->port.iobase);
		return inb(up->port.iobase + 1);

	case UPIO_MEM:
		return readb(up->port.membase + offset);

	default:
		return inb(up->port.iobase + offset);
	}
}

static void serial_out(struct uart_sunsu_port *up, int offset, int value)
{
#ifndef CONFIG_SPARC64
	/*
	 * MrCoffee has weird schematics: IRQ4 & P10(?) pins of SuperIO are
	 * connected with a gate then go to SlavIO. When IRQ4 goes tristated
	 * gate outputs a logical one. Since we use level triggered interrupts
	 * we have lockup and watchdog reset. We cannot mask IRQ because
	 * keyboard shares IRQ with us (Word has it as Bob Smelik's design).
	 * This problem is similar to what Alpha people suffer, see
	 * 8250_alpha.c.
	 */
	if (offset == UART_MCR)
		value |= UART_MCR_OUT2;
#endif
	offset <<= up->port.regshift;

	switch (up->port.iotype) {
	case UPIO_HUB6:
		outb(up->port.hub6 - 1 + offset, up->port.iobase);
		outb(value, up->port.iobase + 1);
		break;

	case UPIO_MEM:
		writeb(value, up->port.membase + offset);
		break;

	default:
		outb(value, up->port.iobase + offset);
	}
}

/*
 * We used to support using pause I/O for certain machines.  We
 * haven't supported this for a while, but just in case it's badly
 * needed for certain old 386 machines, I've left these #define's
 * in....
 */
#define serial_inp(up, offset)		serial_in(up, offset)
#define serial_outp(up, offset, value)	serial_out(up, offset, value)


/*
 * For the 16C950
 */
static void serial_icr_write(struct uart_sunsu_port *up, int offset, int value)
{
	serial_out(up, UART_SCR, offset);
	serial_out(up, UART_ICR, value);
}

#if 0 /* Unused currently */
static unsigned int serial_icr_read(struct uart_sunsu_port *up, int offset)
{
	unsigned int value;

	serial_icr_write(up, UART_ACR, up->acr | UART_ACR_ICRRD);
	serial_out(up, UART_SCR, offset);
	value = serial_in(up, UART_ICR);
	serial_icr_write(up, UART_ACR, up->acr);

	return value;
}
#endif

#ifdef CONFIG_SERIAL_8250_RSA
/*
 * Attempts to turn on the RSA FIFO.  Returns zero on failure.
 * We set the port uart clock rate if we succeed.
 */
static int __enable_rsa(struct uart_sunsu_port *up)
{
	unsigned char mode;
	int result;

	mode = serial_inp(up, UART_RSA_MSR);
	result = mode & UART_RSA_MSR_FIFO;

	if (!result) {
		serial_outp(up, UART_RSA_MSR, mode | UART_RSA_MSR_FIFO);
		mode = serial_inp(up, UART_RSA_MSR);
		result = mode & UART_RSA_MSR_FIFO;
	}

	if (result)
		up->port.uartclk = SERIAL_RSA_BAUD_BASE * 16;

	return result;
}

static void enable_rsa(struct uart_sunsu_port *up)
{
	if (up->port.type == PORT_RSA) {
		if (up->port.uartclk != SERIAL_RSA_BAUD_BASE * 16) {
			uart_port_lock_irq(&up->port);
			__enable_rsa(up);
			uart_port_unlock_irq(&up->port);
		}
		if (up->port.uartclk == SERIAL_RSA_BAUD_BASE * 16)
			serial_outp(up, UART_RSA_FRR, 0);
	}
}

/*
 * Attempts to turn off the RSA FIFO.  Returns zero on failure.
 * It is unknown why interrupts were disabled in here.  However,
 * the caller is expected to preserve this behaviour by grabbing
 * the spinlock before calling this function.
 */
static void disable_rsa(struct uart_sunsu_port *up)
{
	unsigned char mode;
	int result;

	if (up->port.type == PORT_RSA &&
	    up->port.uartclk == SERIAL_RSA_BAUD_BASE * 16) {
		uart_port_lock_irq(&up->port);

		mode = serial_inp(up, UART_RSA_MSR);
		result = !(mode & UART_RSA_MSR_FIFO);

		if (!result) {
			serial_outp(up, UART_RSA_MSR, mode & ~UART_RSA_MSR_FIFO);
			mode = serial_inp(up, UART_RSA_MSR);
			result = !(mode & UART_RSA_MSR_FIFO);
		}

		if (result)
			up->port.uartclk = SERIAL_RSA_BAUD_BASE_LO * 16;
		uart_port_unlock_irq(&up->port);
	}
}
#endif /* CONFIG_SERIAL_8250_RSA */

static inline void __stop_tx(struct uart_sunsu_port *p)
{
	if (p->ier & UART_IER_THRI) {
		p->ier &= ~UART_IER_THRI;
		serial_out(p, UART_IER, p->ier);
	}
}

static void sunsu_stop_tx(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);

	__stop_tx(up);

	/*
	 * We really want to stop the transmitter from sending.
	 */
	if (up->port.type == PORT_16C950) {
		up->acr |= UART_ACR_TXDIS;
		serial_icr_write(up, UART_ACR, up->acr);
	}
}

static void sunsu_start_tx(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);

	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}

	/*
	 * Re-enable the transmitter if we disabled it.
	 */
	if (up->port.type == PORT_16C950 && up->acr & UART_ACR_TXDIS) {
		up->acr &= ~UART_ACR_TXDIS;
		serial_icr_write(up, UART_ACR, up->acr);
	}
}

static void sunsu_stop_rx(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);

	up->ier &= ~UART_IER_RLSI;
	up->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(up, UART_IER, up->ier);
}

static void sunsu_enable_ms(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned long flags;

	uart_port_lock_irqsave(&up->port, &flags);
	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
	uart_port_unlock_irqrestore(&up->port, flags);
}

static void
receive_chars(struct uart_sunsu_port *up, unsigned char *status)
{
	struct tty_port *port = &up->port.state->port;
	unsigned char ch, flag;
	int max_count = 256;
	int saw_console_brk = 0;

	do {
		ch = serial_inp(up, UART_RX);
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(*status & (UART_LSR_BI | UART_LSR_PE |
				       UART_LSR_FE | UART_LSR_OE))) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				up->port.icount.brk++;
				if (up->port.cons != NULL &&
				    up->port.line == up->port.cons->index)
					saw_console_brk = 1;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				if (uart_handle_break(&up->port))
					goto ignore_char;
			} else if (*status & UART_LSR_PE)
				up->port.icount.parity++;
			else if (*status & UART_LSR_FE)
				up->port.icount.frame++;
			if (*status & UART_LSR_OE)
				up->port.icount.overrun++;

			/*
			 * Mask off conditions which should be ingored.
			 */
			*status &= up->port.read_status_mask;

			if (up->port.cons != NULL &&
			    up->port.line == up->port.cons->index) {
				/* Recover the break flag from console xmit */
				*status |= up->lsr_break_flag;
				up->lsr_break_flag = 0;
			}

			if (*status & UART_LSR_BI) {
				flag = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				flag = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch))
			goto ignore_char;
		if ((*status & up->port.ignore_status_mask) == 0)
			tty_insert_flip_char(port, ch, flag);
		if (*status & UART_LSR_OE)
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			 tty_insert_flip_char(port, 0, TTY_OVERRUN);
	ignore_char:
		*status = serial_inp(up, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));

	if (saw_console_brk)
		sun_do_break();
}

static void transmit_chars(struct uart_sunsu_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count;

	if (up->port.x_char) {
		serial_outp(up, UART_TX, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}
	if (uart_tx_stopped(&up->port)) {
		sunsu_stop_tx(&up->port);
		return;
	}
	if (uart_circ_empty(xmit)) {
		__stop_tx(up);
		return;
	}

	count = up->port.fifosize;
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		uart_xmit_advance(&up->port, 1);
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		__stop_tx(up);
}

static void check_modem_status(struct uart_sunsu_port *up)
{
	int status;

	status = serial_in(up, UART_MSR);

	if ((status & UART_MSR_ANY_DELTA) == 0)
		return;

	if (status & UART_MSR_TERI)
		up->port.icount.rng++;
	if (status & UART_MSR_DDSR)
		up->port.icount.dsr++;
	if (status & UART_MSR_DDCD)
		uart_handle_dcd_change(&up->port, status & UART_MSR_DCD);
	if (status & UART_MSR_DCTS)
		uart_handle_cts_change(&up->port, status & UART_MSR_CTS);

	wake_up_interruptible(&up->port.state->port.delta_msr_wait);
}

static irqreturn_t sunsu_serial_interrupt(int irq, void *dev_id)
{
	struct uart_sunsu_port *up = dev_id;
	unsigned long flags;
	unsigned char status;

	uart_port_lock_irqsave(&up->port, &flags);

	do {
		status = serial_inp(up, UART_LSR);
		if (status & UART_LSR_DR)
			receive_chars(up, &status);
		check_modem_status(up);
		if (status & UART_LSR_THRE)
			transmit_chars(up);

		tty_flip_buffer_push(&up->port.state->port);

	} while (!(serial_in(up, UART_IIR) & UART_IIR_NO_INT));

	uart_port_unlock_irqrestore(&up->port, flags);

	return IRQ_HANDLED;
}

/* Separate interrupt handling path for keyboard/mouse ports.  */

static void
sunsu_change_speed(struct uart_port *port, unsigned int cflag,
		   unsigned int iflag, unsigned int quot);

static void sunsu_change_mouse_baud(struct uart_sunsu_port *up)
{
	unsigned int cur_cflag = up->cflag;
	int quot, new_baud;

	up->cflag &= ~CBAUD;
	up->cflag |= suncore_mouse_baud_cflag_next(cur_cflag, &new_baud);

	quot = up->port.uartclk / (16 * new_baud);

	sunsu_change_speed(&up->port, up->cflag, 0, quot);
}

static void receive_kbd_ms_chars(struct uart_sunsu_port *up, int is_break)
{
	do {
		unsigned char ch = serial_inp(up, UART_RX);

		/* Stop-A is handled by drivers/char/keyboard.c now. */
		if (up->su_type == SU_PORT_KBD) {
#ifdef CONFIG_SERIO
			serio_interrupt(&up->serio, ch, 0);
#endif
		} else if (up->su_type == SU_PORT_MS) {
			int ret = suncore_mouse_baud_detection(ch, is_break);

			switch (ret) {
			case 2:
				sunsu_change_mouse_baud(up);
				fallthrough;
			case 1:
				break;

			case 0:
#ifdef CONFIG_SERIO
				serio_interrupt(&up->serio, ch, 0);
#endif
				break;
			}
		}
	} while (serial_in(up, UART_LSR) & UART_LSR_DR);
}

static irqreturn_t sunsu_kbd_ms_interrupt(int irq, void *dev_id)
{
	struct uart_sunsu_port *up = dev_id;

	if (!(serial_in(up, UART_IIR) & UART_IIR_NO_INT)) {
		unsigned char status = serial_inp(up, UART_LSR);

		if ((status & UART_LSR_DR) || (status & UART_LSR_BI))
			receive_kbd_ms_chars(up, (status & UART_LSR_BI) != 0);
	}

	return IRQ_HANDLED;
}

static unsigned int sunsu_tx_empty(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned long flags;
	unsigned int ret;

	uart_port_lock_irqsave(&up->port, &flags);
	ret = serial_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	uart_port_unlock_irqrestore(&up->port, flags);

	return ret;
}

static unsigned int sunsu_get_mctrl(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned char status;
	unsigned int ret;

	status = serial_in(up, UART_MSR);

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

static void sunsu_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
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

	serial_out(up, UART_MCR, mcr);
}

static void sunsu_break_ctl(struct uart_port *port, int break_state)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned long flags;

	uart_port_lock_irqsave(&up->port, &flags);
	if (break_state == -1)
		up->lcr |= UART_LCR_SBC;
	else
		up->lcr &= ~UART_LCR_SBC;
	serial_out(up, UART_LCR, up->lcr);
	uart_port_unlock_irqrestore(&up->port, flags);
}

static int sunsu_startup(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned long flags;
	int retval;

	if (up->port.type == PORT_16C950) {
		/* Wake up and initialize UART */
		up->acr = 0;
		serial_outp(up, UART_LCR, 0xBF);
		serial_outp(up, UART_EFR, UART_EFR_ECB);
		serial_outp(up, UART_IER, 0);
		serial_outp(up, UART_LCR, 0);
		serial_icr_write(up, UART_CSR, 0); /* Reset the UART */
		serial_outp(up, UART_LCR, 0xBF);
		serial_outp(up, UART_EFR, UART_EFR_ECB);
		serial_outp(up, UART_LCR, 0);
	}

#ifdef CONFIG_SERIAL_8250_RSA
	/*
	 * If this is an RSA port, see if we can kick it up to the
	 * higher speed clock.
	 */
	enable_rsa(up);
#endif

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reenabled in set_termios())
	 */
	if (uart_config[up->port.type].flags & UART_CLEAR_FIFO) {
		serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO);
		serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
		serial_outp(up, UART_FCR, 0);
	}

	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_inp(up, UART_LSR);
	(void) serial_inp(up, UART_RX);
	(void) serial_inp(up, UART_IIR);
	(void) serial_inp(up, UART_MSR);

	/*
	 * At this point, there's no way the LSR could still be 0xff;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (!(up->port.flags & UPF_BUGGY_UART) &&
	    (serial_inp(up, UART_LSR) == 0xff)) {
		printk("ttyS%d: LSR safety check engaged!\n", up->port.line);
		return -ENODEV;
	}

	if (up->su_type != SU_PORT_PORT) {
		retval = request_irq(up->port.irq, sunsu_kbd_ms_interrupt,
				     IRQF_SHARED, su_typev[up->su_type], up);
	} else {
		retval = request_irq(up->port.irq, sunsu_serial_interrupt,
				     IRQF_SHARED, su_typev[up->su_type], up);
	}
	if (retval) {
		printk("su: Cannot register IRQ %d\n", up->port.irq);
		return retval;
	}

	/*
	 * Now, initialize the UART
	 */
	serial_outp(up, UART_LCR, UART_LCR_WLEN8);

	uart_port_lock_irqsave(&up->port, &flags);

	up->port.mctrl |= TIOCM_OUT2;

	sunsu_set_mctrl(&up->port, up->port.mctrl);
	uart_port_unlock_irqrestore(&up->port, flags);

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 */
	up->ier = UART_IER_RLSI | UART_IER_RDI;
	serial_outp(up, UART_IER, up->ier);

	if (up->port.flags & UPF_FOURPORT) {
		unsigned int icp;
		/*
		 * Enable interrupts on the AST Fourport board
		 */
		icp = (up->port.iobase & 0xfe0) | 0x01f;
		outb_p(0x80, icp);
		(void) inb_p(icp);
	}

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) serial_inp(up, UART_LSR);
	(void) serial_inp(up, UART_RX);
	(void) serial_inp(up, UART_IIR);
	(void) serial_inp(up, UART_MSR);

	return 0;
}

static void sunsu_shutdown(struct uart_port *port)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned long flags;

	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	serial_outp(up, UART_IER, 0);

	uart_port_lock_irqsave(&up->port, &flags);
	if (up->port.flags & UPF_FOURPORT) {
		/* reset interrupts on the AST Fourport board */
		inb((up->port.iobase & 0xfe0) | 0x1f);
		up->port.mctrl |= TIOCM_OUT1;
	} else
		up->port.mctrl &= ~TIOCM_OUT2;

	sunsu_set_mctrl(&up->port, up->port.mctrl);
	uart_port_unlock_irqrestore(&up->port, flags);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_out(up, UART_LCR, serial_inp(up, UART_LCR) & ~UART_LCR_SBC);
	serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO |
				  UART_FCR_CLEAR_RCVR |
				  UART_FCR_CLEAR_XMIT);
	serial_outp(up, UART_FCR, 0);

#ifdef CONFIG_SERIAL_8250_RSA
	/*
	 * Reset the RSA board back to 115kbps compat mode.
	 */
	disable_rsa(up);
#endif

	/*
	 * Read data port to reset things.
	 */
	(void) serial_in(up, UART_RX);

	free_irq(up->port.irq, up);
}

static void
sunsu_change_speed(struct uart_port *port, unsigned int cflag,
		   unsigned int iflag, unsigned int quot)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);
	unsigned char cval, fcr = 0;
	unsigned long flags;

	switch (cflag & CSIZE) {
	case CS5:
		cval = 0x00;
		break;
	case CS6:
		cval = 0x01;
		break;
	case CS7:
		cval = 0x02;
		break;
	default:
	case CS8:
		cval = 0x03;
		break;
	}

	if (cflag & CSTOPB)
		cval |= 0x04;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;

	/*
	 * Work around a bug in the Oxford Semiconductor 952 rev B
	 * chip which causes it to seriously miscalculate baud rates
	 * when DLL is 0.
	 */
	if ((quot & 0xff) == 0 && up->port.type == PORT_16C950 &&
	    up->rev == 0x5201)
		quot ++;

	if (uart_config[up->port.type].flags & UART_USE_FIFO) {
		if ((up->port.uartclk / quot) < (2400 * 16))
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
#ifdef CONFIG_SERIAL_8250_RSA
		else if (up->port.type == PORT_RSA)
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_14;
#endif
		else
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;
	}
	if (up->port.type == PORT_16750)
		fcr |= UART_FCR7_64BYTE;

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	uart_port_lock_irqsave(&up->port, &flags);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, cflag, (port->uartclk / (16 * quot)));

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (iflag & (IGNBRK | BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(&up->port, cflag))
		up->ier |= UART_IER_MSI;

	serial_out(up, UART_IER, up->ier);

	if (uart_config[up->port.type].flags & UART_STARTECH) {
		serial_outp(up, UART_LCR, 0xBF);
		serial_outp(up, UART_EFR, cflag & CRTSCTS ? UART_EFR_CTS :0);
	}
	serial_outp(up, UART_LCR, cval | UART_LCR_DLAB);/* set DLAB */
	serial_outp(up, UART_DLL, quot & 0xff);		/* LS of divisor */
	serial_outp(up, UART_DLM, quot >> 8);		/* MS of divisor */
	if (up->port.type == PORT_16750)
		serial_outp(up, UART_FCR, fcr);		/* set fcr */
	serial_outp(up, UART_LCR, cval);		/* reset DLAB */
	up->lcr = cval;					/* Save LCR */
	if (up->port.type != PORT_16750) {
		if (fcr & UART_FCR_ENABLE_FIFO) {
			/* emulated UARTs (Lucent Venus 167x) need two steps */
			serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO);
		}
		serial_outp(up, UART_FCR, fcr);		/* set fcr */
	}

	up->cflag = cflag;

	uart_port_unlock_irqrestore(&up->port, flags);
}

static void
sunsu_set_termios(struct uart_port *port, struct ktermios *termios,
		  const struct ktermios *old)
{
	unsigned int baud, quot;

	/*
	 * Ask the core to calculate the divisor for us.
	 */
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 
	quot = uart_get_divisor(port, baud);

	sunsu_change_speed(port, termios->c_cflag, termios->c_iflag, quot);
}

static void sunsu_release_port(struct uart_port *port)
{
}

static int sunsu_request_port(struct uart_port *port)
{
	return 0;
}

static void sunsu_config_port(struct uart_port *port, int flags)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);

	if (flags & UART_CONFIG_TYPE) {
		/*
		 * We are supposed to call autoconfig here, but this requires
		 * splitting all the OBP probing crap from the UART probing.
		 * We'll do it when we kill sunsu.c altogether.
		 */
		port->type = up->type_probed;	/* XXX */
	}
}

static int
sunsu_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	return -EINVAL;
}

static const char *
sunsu_type(struct uart_port *port)
{
	int type = port->type;

	if (type >= ARRAY_SIZE(uart_config))
		type = 0;
	return uart_config[type].name;
}

static const struct uart_ops sunsu_pops = {
	.tx_empty	= sunsu_tx_empty,
	.set_mctrl	= sunsu_set_mctrl,
	.get_mctrl	= sunsu_get_mctrl,
	.stop_tx	= sunsu_stop_tx,
	.start_tx	= sunsu_start_tx,
	.stop_rx	= sunsu_stop_rx,
	.enable_ms	= sunsu_enable_ms,
	.break_ctl	= sunsu_break_ctl,
	.startup	= sunsu_startup,
	.shutdown	= sunsu_shutdown,
	.set_termios	= sunsu_set_termios,
	.type		= sunsu_type,
	.release_port	= sunsu_release_port,
	.request_port	= sunsu_request_port,
	.config_port	= sunsu_config_port,
	.verify_port	= sunsu_verify_port,
};

#define UART_NR	4

static struct uart_sunsu_port sunsu_ports[UART_NR];
static int nr_inst; /* Number of already registered ports */

#ifdef CONFIG_SERIO

static DEFINE_SPINLOCK(sunsu_serio_lock);

static int sunsu_serio_write(struct serio *serio, unsigned char ch)
{
	struct uart_sunsu_port *up = serio->port_data;
	unsigned long flags;
	int lsr;

	spin_lock_irqsave(&sunsu_serio_lock, flags);

	do {
		lsr = serial_in(up, UART_LSR);
	} while (!(lsr & UART_LSR_THRE));

	/* Send the character out. */
	serial_out(up, UART_TX, ch);

	spin_unlock_irqrestore(&sunsu_serio_lock, flags);

	return 0;
}

static int sunsu_serio_open(struct serio *serio)
{
	struct uart_sunsu_port *up = serio->port_data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sunsu_serio_lock, flags);
	if (!up->serio_open) {
		up->serio_open = 1;
		ret = 0;
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore(&sunsu_serio_lock, flags);

	return ret;
}

static void sunsu_serio_close(struct serio *serio)
{
	struct uart_sunsu_port *up = serio->port_data;
	unsigned long flags;

	spin_lock_irqsave(&sunsu_serio_lock, flags);
	up->serio_open = 0;
	spin_unlock_irqrestore(&sunsu_serio_lock, flags);
}

#endif /* CONFIG_SERIO */

static void sunsu_autoconfig(struct uart_sunsu_port *up)
{
	unsigned char status1, status2, scratch, scratch2, scratch3;
	unsigned char save_lcr, save_mcr;
	unsigned long flags;

	if (up->su_type == SU_PORT_NONE)
		return;

	up->type_probed = PORT_UNKNOWN;
	up->port.iotype = UPIO_MEM;

	uart_port_lock_irqsave(&up->port, &flags);

	if (!(up->port.flags & UPF_BUGGY_UART)) {
		/*
		 * Do a simple existence test first; if we fail this, there's
		 * no point trying anything else.
		 *
		 * 0x80 is used as a nonsense port to prevent against false
		 * positives due to ISA bus float.  The assumption is that
		 * 0x80 is a non-existent port; which should be safe since
		 * include/asm/io.h also makes this assumption.
		 */
		scratch = serial_inp(up, UART_IER);
		serial_outp(up, UART_IER, 0);
#ifdef __i386__
		outb(0xff, 0x080);
#endif
		scratch2 = serial_inp(up, UART_IER);
		serial_outp(up, UART_IER, 0x0f);
#ifdef __i386__
		outb(0, 0x080);
#endif
		scratch3 = serial_inp(up, UART_IER);
		serial_outp(up, UART_IER, scratch);
		if (scratch2 != 0 || scratch3 != 0x0F)
			goto out;	/* We failed; there's nothing here */
	}

	save_mcr = serial_in(up, UART_MCR);
	save_lcr = serial_in(up, UART_LCR);

	/* 
	 * Check to see if a UART is really there.  Certain broken
	 * internal modems based on the Rockwell chipset fail this
	 * test, because they apparently don't implement the loopback
	 * test mode.  So this test is skipped on the COM 1 through
	 * COM 4 ports.  This *should* be safe, since no board
	 * manufacturer would be stupid enough to design a board
	 * that conflicts with COM 1-4 --- we hope!
	 */
	if (!(up->port.flags & UPF_SKIP_TEST)) {
		serial_outp(up, UART_MCR, UART_MCR_LOOP | 0x0A);
		status1 = serial_inp(up, UART_MSR) & 0xF0;
		serial_outp(up, UART_MCR, save_mcr);
		if (status1 != 0x90)
			goto out;	/* We failed loopback test */
	}
	serial_outp(up, UART_LCR, 0xBF);	/* set up for StarTech test */
	serial_outp(up, UART_EFR, 0);		/* EFR is the same as FCR */
	serial_outp(up, UART_LCR, 0);
	serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	scratch = serial_in(up, UART_IIR) >> 6;
	switch (scratch) {
		case 0:
			up->port.type = PORT_16450;
			break;
		case 1:
			up->port.type = PORT_UNKNOWN;
			break;
		case 2:
			up->port.type = PORT_16550;
			break;
		case 3:
			up->port.type = PORT_16550A;
			break;
	}
	if (up->port.type == PORT_16550A) {
		/* Check for Startech UART's */
		serial_outp(up, UART_LCR, UART_LCR_DLAB);
		if (serial_in(up, UART_EFR) == 0) {
			up->port.type = PORT_16650;
		} else {
			serial_outp(up, UART_LCR, 0xBF);
			if (serial_in(up, UART_EFR) == 0)
				up->port.type = PORT_16650V2;
		}
	}
	if (up->port.type == PORT_16550A) {
		/* Check for TI 16750 */
		serial_outp(up, UART_LCR, save_lcr | UART_LCR_DLAB);
		serial_outp(up, UART_FCR,
			    UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
		scratch = serial_in(up, UART_IIR) >> 5;
		if (scratch == 7) {
			/*
			 * If this is a 16750, and not a cheap UART
			 * clone, then it should only go into 64 byte
			 * mode if the UART_FCR7_64BYTE bit was set
			 * while UART_LCR_DLAB was latched.
			 */
 			serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO);
			serial_outp(up, UART_LCR, 0);
			serial_outp(up, UART_FCR,
				    UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
			scratch = serial_in(up, UART_IIR) >> 5;
			if (scratch == 6)
				up->port.type = PORT_16750;
		}
		serial_outp(up, UART_FCR, UART_FCR_ENABLE_FIFO);
	}
	serial_outp(up, UART_LCR, save_lcr);
	if (up->port.type == PORT_16450) {
		scratch = serial_in(up, UART_SCR);
		serial_outp(up, UART_SCR, 0xa5);
		status1 = serial_in(up, UART_SCR);
		serial_outp(up, UART_SCR, 0x5a);
		status2 = serial_in(up, UART_SCR);
		serial_outp(up, UART_SCR, scratch);

		if ((status1 != 0xa5) || (status2 != 0x5a))
			up->port.type = PORT_8250;
	}

	up->port.fifosize = uart_config[up->port.type].dfl_xmit_fifo_size;

	if (up->port.type == PORT_UNKNOWN)
		goto out;
	up->type_probed = up->port.type;	/* XXX */

	/*
	 * Reset the UART.
	 */
#ifdef CONFIG_SERIAL_8250_RSA
	if (up->port.type == PORT_RSA)
		serial_outp(up, UART_RSA_FRR, 0);
#endif
	serial_outp(up, UART_MCR, save_mcr);
	serial_outp(up, UART_FCR, (UART_FCR_ENABLE_FIFO |
				     UART_FCR_CLEAR_RCVR |
				     UART_FCR_CLEAR_XMIT));
	serial_outp(up, UART_FCR, 0);
	(void)serial_in(up, UART_RX);
	serial_outp(up, UART_IER, 0);

out:
	uart_port_unlock_irqrestore(&up->port, flags);
}

static struct uart_driver sunsu_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "sunsu",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
};

static int sunsu_kbd_ms_init(struct uart_sunsu_port *up)
{
	int quot, baud;
#ifdef CONFIG_SERIO
	struct serio *serio;
#endif

	if (up->su_type == SU_PORT_KBD) {
		up->cflag = B1200 | CS8 | CLOCAL | CREAD;
		baud = 1200;
	} else {
		up->cflag = B4800 | CS8 | CLOCAL | CREAD;
		baud = 4800;
	}
	quot = up->port.uartclk / (16 * baud);

	sunsu_autoconfig(up);
	if (up->port.type == PORT_UNKNOWN)
		return -ENODEV;

	printk("%pOF: %s port at %llx, irq %u\n",
	       up->port.dev->of_node,
	       (up->su_type == SU_PORT_KBD) ? "Keyboard" : "Mouse",
	       (unsigned long long) up->port.mapbase,
	       up->port.irq);

#ifdef CONFIG_SERIO
	serio = &up->serio;
	serio->port_data = up;

	serio->id.type = SERIO_RS232;
	if (up->su_type == SU_PORT_KBD) {
		serio->id.proto = SERIO_SUNKBD;
		strscpy(serio->name, "sukbd", sizeof(serio->name));
	} else {
		serio->id.proto = SERIO_SUN;
		serio->id.extra = 1;
		strscpy(serio->name, "sums", sizeof(serio->name));
	}
	strscpy(serio->phys,
		(!(up->port.line & 1) ? "su/serio0" : "su/serio1"),
		sizeof(serio->phys));

	serio->write = sunsu_serio_write;
	serio->open = sunsu_serio_open;
	serio->close = sunsu_serio_close;
	serio->dev.parent = up->port.dev;

	serio_register_port(serio);
#endif

	sunsu_change_speed(&up->port, up->cflag, 0, quot);

	sunsu_startup(&up->port);
	return 0;
}

/*
 * ------------------------------------------------------------
 * Serial console driver
 * ------------------------------------------------------------
 */

#ifdef CONFIG_SERIAL_SUNSU_CONSOLE

/*
 *	Wait for transmitter & holding register to empty
 */
static void wait_for_xmitr(struct uart_sunsu_port *up)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = serial_in(up, UART_LSR);

		if (status & UART_LSR_BI)
			up->lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
		udelay(1);
	} while (!uart_lsr_tx_empty(status));

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
		       ((serial_in(up, UART_MSR) & UART_MSR_CTS) == 0))
			udelay(1);
	}
}

static void sunsu_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct uart_sunsu_port *up =
		container_of(port, struct uart_sunsu_port, port);

	wait_for_xmitr(up);
	serial_out(up, UART_TX, ch);
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void sunsu_console_write(struct console *co, const char *s,
				unsigned int count)
{
	struct uart_sunsu_port *up = &sunsu_ports[co->index];
	unsigned long flags;
	unsigned int ier;
	int locked = 1;

	if (up->port.sysrq || oops_in_progress)
		locked = uart_port_trylock_irqsave(&up->port, &flags);
	else
		uart_port_lock_irqsave(&up->port, &flags);

	/*
	 *	First save the UER then disable the interrupts
	 */
	ier = serial_in(up, UART_IER);
	serial_out(up, UART_IER, 0);

	uart_console_write(&up->port, s, count, sunsu_console_putchar);

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up);
	serial_out(up, UART_IER, ier);

	if (locked)
		uart_port_unlock_irqrestore(&up->port, flags);
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first su_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init sunsu_console_setup(struct console *co, char *options)
{
	static struct ktermios dummy;
	struct ktermios termios;
	struct uart_port *port;

	printk("Console: ttyS%d (SU)\n",
	       (sunsu_reg.minor - 64) + co->index);

	if (co->index > nr_inst)
		return -ENODEV;
	port = &sunsu_ports[co->index].port;

	/*
	 * Temporary fix.
	 */
	spin_lock_init(&port->lock);

	/* Get firmware console settings.  */
	sunserial_console_termios(co, port->dev->of_node);

	memset(&termios, 0, sizeof(struct ktermios));
	termios.c_cflag = co->cflag;
	port->mctrl |= TIOCM_DTR;
	port->ops->set_termios(port, &termios, &dummy);

	return 0;
}

static struct console sunsu_console = {
	.name	=	"ttyS",
	.write	=	sunsu_console_write,
	.device	=	uart_console_device,
	.setup	=	sunsu_console_setup,
	.flags	=	CON_PRINTBUFFER,
	.index	=	-1,
	.data	=	&sunsu_reg,
};

/*
 *	Register console.
 */

static inline struct console *SUNSU_CONSOLE(void)
{
	return &sunsu_console;
}
#else
#define SUNSU_CONSOLE()			(NULL)
#define sunsu_serial_console_init()	do { } while (0)
#endif

static enum su_type su_get_type(struct device_node *dp)
{
	struct device_node *ap = of_find_node_by_path("/aliases");
	enum su_type rc = SU_PORT_PORT;

	if (ap) {
		const char *keyb = of_get_property(ap, "keyboard", NULL);
		const char *ms = of_get_property(ap, "mouse", NULL);
		struct device_node *match;

		if (keyb) {
			match = of_find_node_by_path(keyb);

			/*
			 * The pointer is used as an identifier not
			 * as a pointer, we can drop the refcount on
			 * the of__node immediately after getting it.
			 */
			of_node_put(match);

			if (dp == match) {
				rc = SU_PORT_KBD;
				goto out;
			}
		}
		if (ms) {
			match = of_find_node_by_path(ms);

			of_node_put(match);

			if (dp == match) {
				rc = SU_PORT_MS;
				goto out;
			}
		}
	}

out:
	of_node_put(ap);
	return rc;
}

static int su_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	struct uart_sunsu_port *up;
	struct resource *rp;
	enum su_type type;
	bool ignore_line;
	int err;

	type = su_get_type(dp);
	if (type == SU_PORT_PORT) {
		if (nr_inst >= UART_NR)
			return -EINVAL;
		up = &sunsu_ports[nr_inst];
	} else {
		up = kzalloc(sizeof(*up), GFP_KERNEL);
		if (!up)
			return -ENOMEM;
	}

	up->port.line = nr_inst;

	spin_lock_init(&up->port.lock);

	up->su_type = type;

	rp = &op->resource[0];
	up->port.mapbase = rp->start;
	up->reg_size = resource_size(rp);
	up->port.membase = of_ioremap(rp, 0, up->reg_size, "su");
	if (!up->port.membase) {
		if (type != SU_PORT_PORT)
			kfree(up);
		return -ENOMEM;
	}

	up->port.irq = op->archdata.irqs[0];

	up->port.dev = &op->dev;

	up->port.type = PORT_UNKNOWN;
	up->port.uartclk = (SU_BASE_BAUD * 16);
	up->port.has_sysrq = IS_ENABLED(CONFIG_SERIAL_SUNSU_CONSOLE);

	err = 0;
	if (up->su_type == SU_PORT_KBD || up->su_type == SU_PORT_MS) {
		err = sunsu_kbd_ms_init(up);
		if (err) {
			of_iounmap(&op->resource[0],
				   up->port.membase, up->reg_size);
			kfree(up);
			return err;
		}
		platform_set_drvdata(op, up);

		nr_inst++;

		return 0;
	}

	up->port.flags |= UPF_BOOT_AUTOCONF;

	sunsu_autoconfig(up);

	err = -ENODEV;
	if (up->port.type == PORT_UNKNOWN)
		goto out_unmap;

	up->port.ops = &sunsu_pops;

	ignore_line = false;
	if (of_node_name_eq(dp, "rsc-console") ||
	    of_node_name_eq(dp, "lom-console"))
		ignore_line = true;

	sunserial_console_match(SUNSU_CONSOLE(), dp,
				&sunsu_reg, up->port.line,
				ignore_line);
	err = uart_add_one_port(&sunsu_reg, &up->port);
	if (err)
		goto out_unmap;

	platform_set_drvdata(op, up);

	nr_inst++;

	return 0;

out_unmap:
	of_iounmap(&op->resource[0], up->port.membase, up->reg_size);
	kfree(up);
	return err;
}

static int su_remove(struct platform_device *op)
{
	struct uart_sunsu_port *up = platform_get_drvdata(op);
	bool kbdms = false;

	if (up->su_type == SU_PORT_MS ||
	    up->su_type == SU_PORT_KBD)
		kbdms = true;

	if (kbdms) {
#ifdef CONFIG_SERIO
		serio_unregister_port(&up->serio);
#endif
	} else if (up->port.type != PORT_UNKNOWN)
		uart_remove_one_port(&sunsu_reg, &up->port);

	if (up->port.membase)
		of_iounmap(&op->resource[0], up->port.membase, up->reg_size);

	if (kbdms)
		kfree(up);

	return 0;
}

static const struct of_device_id su_match[] = {
	{
		.name = "su",
	},
	{
		.name = "su_pnp",
	},
	{
		.name = "serial",
		.compatible = "su",
	},
	{
		.type = "serial",
		.compatible = "su",
	},
	{},
};
MODULE_DEVICE_TABLE(of, su_match);

static struct platform_driver su_driver = {
	.driver = {
		.name = "su",
		.of_match_table = su_match,
	},
	.probe		= su_probe,
	.remove		= su_remove,
};

static int __init sunsu_init(void)
{
	struct device_node *dp;
	int err;
	int num_uart = 0;

	for_each_node_by_name(dp, "su") {
		if (su_get_type(dp) == SU_PORT_PORT)
			num_uart++;
	}
	for_each_node_by_name(dp, "su_pnp") {
		if (su_get_type(dp) == SU_PORT_PORT)
			num_uart++;
	}
	for_each_node_by_name(dp, "serial") {
		if (of_device_is_compatible(dp, "su")) {
			if (su_get_type(dp) == SU_PORT_PORT)
				num_uart++;
		}
	}
	for_each_node_by_type(dp, "serial") {
		if (of_device_is_compatible(dp, "su")) {
			if (su_get_type(dp) == SU_PORT_PORT)
				num_uart++;
		}
	}

	if (num_uart) {
		err = sunserial_register_minors(&sunsu_reg, num_uart);
		if (err)
			return err;
	}

	err = platform_driver_register(&su_driver);
	if (err && num_uart)
		sunserial_unregister_minors(&sunsu_reg, num_uart);

	return err;
}

static void __exit sunsu_exit(void)
{
	platform_driver_unregister(&su_driver);
	if (sunsu_reg.nr)
		sunserial_unregister_minors(&sunsu_reg, sunsu_reg.nr);
}

module_init(sunsu_init);
module_exit(sunsu_exit);

MODULE_AUTHOR("Eddie C. Dost, Peter Zaitcev, and David S. Miller");
MODULE_DESCRIPTION("Sun SU serial port driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
