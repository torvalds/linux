/*
 *  m32r_sio.c
 *
 *  Driver for M32R serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *  Based on drivers/serial/8250.c.
 *
 *  Copyright (C) 2001  Russell King.
 *  Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * A note about mapbase / membase
 *
 *  mapbase is the physical address of the IO port.  Currently, we don't
 *  support this very well, and it may well be dropped from this driver
 *  in future.  As such, mapbase should be NULL.
 *
 *  membase is an 'ioremapped' cookie.  This is compatible with the old
 *  serial.c driver, and is currently the preferred form.
 */
#include <linux/config.h>

#if defined(CONFIG_SERIAL_M32R_SIO_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/delay.h>

#include <asm/m32r.h>
#include <asm/io.h>
#include <asm/irq.h>

#define PORT_M32R_BASE	PORT_M32R_SIO
#define PORT_INDEX(x)	(x - PORT_M32R_BASE + 1)
#define BAUD_RATE	115200

#include <linux/serial_core.h>
#include "m32r_sio.h"
#include "m32r_sio_reg.h"

/*
 * Debugging.
 */
#if 0
#define DEBUG_AUTOCONF(fmt...)	printk(fmt)
#else
#define DEBUG_AUTOCONF(fmt...)	do { } while (0)
#endif

#if 0
#define DEBUG_INTR(fmt...)	printk(fmt)
#else
#define DEBUG_INTR(fmt...)	do { } while (0)
#endif

#define PASS_LIMIT	256

/*
 * We default to IRQ0 for the "no irq" hack.   Some
 * machine types want others as well - they're free
 * to redefine this in their header file.
 */
#define is_real_interrupt(irq)	((irq) != 0)

#include <asm/serial.h>

/* Standard COM flags */
#define STD_COM_FLAGS (UPF_BOOT_AUTOCONF | UPF_SKIP_TEST)

/*
 * SERIAL_PORT_DFNS tells us about built-in ports that have no
 * standard enumeration mechanism.   Platforms that can find all
 * serial ports via mechanisms like ACPI or PCI need not supply it.
 */
#undef SERIAL_PORT_DFNS
#if defined(CONFIG_PLAT_USRV)

#define SERIAL_PORT_DFNS						\
       /* UART  CLK     PORT   IRQ            FLAGS */			\
	{ 0, BASE_BAUD, 0x3F8, PLD_IRQ_UART0, STD_COM_FLAGS }, /* ttyS0 */ \
	{ 0, BASE_BAUD, 0x2F8, PLD_IRQ_UART1, STD_COM_FLAGS }, /* ttyS1 */

#else /* !CONFIG_PLAT_USRV */

#if defined(CONFIG_SERIAL_M32R_PLDSIO)
#define SERIAL_PORT_DFNS						\
	{ 0, BASE_BAUD, ((unsigned long)PLD_ESIO0CR), PLD_IRQ_SIO0_RCV,	\
	  STD_COM_FLAGS }, /* ttyS0 */
#else
#define SERIAL_PORT_DFNS						\
	{ 0, BASE_BAUD, M32R_SIO_OFFSET, M32R_IRQ_SIO0_R,		\
	  STD_COM_FLAGS }, /* ttyS0 */
#endif

#endif /* !CONFIG_PLAT_USRV */

static struct old_serial_port old_serial_port[] = {
	SERIAL_PORT_DFNS	/* defined in asm/serial.h */
};

#define UART_NR	ARRAY_SIZE(old_serial_port)

struct uart_sio_port {
	struct uart_port	port;
	struct timer_list	timer;		/* "no irq" timer */
	struct list_head	list;		/* ports on this IRQ */
	unsigned short		rev;
	unsigned char		acr;
	unsigned char		ier;
	unsigned char		lcr;
	unsigned char		mcr_mask;	/* mask of user bits */
	unsigned char		mcr_force;	/* mask of forced bits */
	unsigned char		lsr_break_flag;

	/*
	 * We provide a per-port pm hook.
	 */
	void			(*pm)(struct uart_port *port,
				      unsigned int state, unsigned int old);
};

struct irq_info {
	spinlock_t		lock;
	struct list_head	*head;
};

static struct irq_info irq_lists[NR_IRQS];

/*
 * Here we define the default xmit fifo size used for each type of UART.
 */
static const struct serial_uart_config uart_config[] = {
	[PORT_UNKNOWN] = {
		.name			= "unknown",
		.dfl_xmit_fifo_size	= 1,
		.flags			= 0,
	},
	[PORT_INDEX(PORT_M32R_SIO)] = {
		.name			= "M32RSIO",
		.dfl_xmit_fifo_size	= 1,
		.flags			= 0,
	},
};

#ifdef CONFIG_SERIAL_M32R_PLDSIO

#define __sio_in(x) inw((unsigned long)(x))
#define __sio_out(v,x) outw((v),(unsigned long)(x))

static inline void sio_set_baud_rate(unsigned long baud)
{
	unsigned short sbaud;
	sbaud = (boot_cpu_data.bus_clock / (baud * 4))-1;
	__sio_out(sbaud, PLD_ESIO0BAUR);
}

static void sio_reset(void)
{
	unsigned short tmp;

	tmp = __sio_in(PLD_ESIO0RXB);
	tmp = __sio_in(PLD_ESIO0RXB);
	tmp = __sio_in(PLD_ESIO0CR);
	sio_set_baud_rate(BAUD_RATE);
	__sio_out(0x0300, PLD_ESIO0CR);
	__sio_out(0x0003, PLD_ESIO0CR);
}

static void sio_init(void)
{
	unsigned short tmp;

	tmp = __sio_in(PLD_ESIO0RXB);
	tmp = __sio_in(PLD_ESIO0RXB);
	tmp = __sio_in(PLD_ESIO0CR);
	__sio_out(0x0300, PLD_ESIO0CR);
	__sio_out(0x0003, PLD_ESIO0CR);
}

static void sio_error(int *status)
{
	printk("SIO0 error[%04x]\n", *status);
	do {
		sio_init();
	} while ((*status = __sio_in(PLD_ESIO0CR)) != 3);
}

#else /* not CONFIG_SERIAL_M32R_PLDSIO */

#define __sio_in(x) inl(x)
#define __sio_out(v,x) outl((v),(x))

static inline void sio_set_baud_rate(unsigned long baud)
{
	unsigned long i, j;

	i = boot_cpu_data.bus_clock / (baud * 16);
	j = (boot_cpu_data.bus_clock - (i * baud * 16)) / baud;
	i -= 1;
	j = (j + 1) >> 1;

	__sio_out(i, M32R_SIO0_BAUR_PORTL);
	__sio_out(j, M32R_SIO0_RBAUR_PORTL);
}

static void sio_reset(void)
{
	__sio_out(0x00000300, M32R_SIO0_CR_PORTL);	/* init status */
	__sio_out(0x00000800, M32R_SIO0_MOD1_PORTL);	/* 8bit        */
	__sio_out(0x00000080, M32R_SIO0_MOD0_PORTL);	/* 1stop non   */
	sio_set_baud_rate(BAUD_RATE);
	__sio_out(0x00000000, M32R_SIO0_TRCR_PORTL);
	__sio_out(0x00000003, M32R_SIO0_CR_PORTL);	/* RXCEN */
}

static void sio_init(void)
{
	unsigned int tmp;

	tmp = __sio_in(M32R_SIO0_RXB_PORTL);
	tmp = __sio_in(M32R_SIO0_RXB_PORTL);
	tmp = __sio_in(M32R_SIO0_STS_PORTL);
	__sio_out(0x00000003, M32R_SIO0_CR_PORTL);
}

static void sio_error(int *status)
{
	printk("SIO0 error[%04x]\n", *status);
	do {
		sio_init();
	} while ((*status = __sio_in(M32R_SIO0_CR_PORTL)) != 3);
}

#endif /* CONFIG_SERIAL_M32R_PLDSIO */

static _INLINE_ unsigned int sio_in(struct uart_sio_port *up, int offset)
{
	return __sio_in(up->port.iobase + offset);
}

static _INLINE_ void sio_out(struct uart_sio_port *up, int offset, int value)
{
	__sio_out(value, up->port.iobase + offset);
}

static _INLINE_ unsigned int serial_in(struct uart_sio_port *up, int offset)
{
	if (!offset)
		return 0;

	return __sio_in(offset);
}

static _INLINE_ void
serial_out(struct uart_sio_port *up, int offset, int value)
{
	if (!offset)
		return;

	__sio_out(value, offset);
}

static void m32r_sio_stop_tx(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	if (up->ier & UART_IER_THRI) {
		up->ier &= ~UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
}

static void m32r_sio_start_tx(struct uart_port *port)
{
#ifdef CONFIG_SERIAL_M32R_PLDSIO
	struct uart_sio_port *up = (struct uart_sio_port *)port;
	struct circ_buf *xmit = &up->port.info->xmit;

	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
	}
	while((serial_in(up, UART_LSR) & UART_EMPTY) != UART_EMPTY);
#else
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	if (!(up->ier & UART_IER_THRI)) {
		up->ier |= UART_IER_THRI;
		serial_out(up, UART_IER, up->ier);
	}
#endif
}

static void m32r_sio_stop_rx(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	up->ier &= ~UART_IER_RLSI;
	up->port.read_status_mask &= ~UART_LSR_DR;
	serial_out(up, UART_IER, up->ier);
}

static void m32r_sio_enable_ms(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	up->ier |= UART_IER_MSI;
	serial_out(up, UART_IER, up->ier);
}

static _INLINE_ void receive_chars(struct uart_sio_port *up, int *status,
	struct pt_regs *regs)
{
	struct tty_struct *tty = up->port.info->tty;
	unsigned char ch;
	unsigned char flag;
	int max_count = 256;

	do {
		ch = sio_in(up, SIORXB);
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

			if (up->port.line == up->port.cons->index) {
				/* Recover the break flag from console xmit */
				*status |= up->lsr_break_flag;
				up->lsr_break_flag = 0;
			}

			if (*status & UART_LSR_BI) {
				DEBUG_INTR("handling break....");
				flag = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				flag = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				flag = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(&up->port, ch, regs))
			goto ignore_char;
		if ((*status & up->port.ignore_status_mask) == 0)
			tty_insert_flip_char(tty, ch, flag);

		if (*status & UART_LSR_OE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			tty_insert_flip_char(tty, 0, TTY_OVERRUN);
		}
	ignore_char:
		*status = serial_in(up, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));
	tty_flip_buffer_push(tty);
}

static _INLINE_ void transmit_chars(struct uart_sio_port *up)
{
	struct circ_buf *xmit = &up->port.info->xmit;
	int count;

	if (up->port.x_char) {
#ifndef CONFIG_SERIAL_M32R_PLDSIO	/* XXX */
		serial_out(up, UART_TX, up->port.x_char);
#endif
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
		m32r_sio_stop_tx(&up->port);
		return;
	}

	count = up->port.fifosize;
	do {
		serial_out(up, UART_TX, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		if (uart_circ_empty(xmit))
			break;
		while (!serial_in(up, UART_LSR) & UART_LSR_THRE);

	} while (--count > 0);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	DEBUG_INTR("THRE...");

	if (uart_circ_empty(xmit))
		m32r_sio_stop_tx(&up->port);
}

/*
 * This handles the interrupt from one port.
 */
static inline void m32r_sio_handle_port(struct uart_sio_port *up,
	unsigned int status, struct pt_regs *regs)
{
	DEBUG_INTR("status = %x...", status);

	if (status & 0x04)
		receive_chars(up, &status, regs);
	if (status & 0x01)
		transmit_chars(up);
}

/*
 * This is the serial driver's interrupt routine.
 *
 * Arjan thinks the old way was overly complex, so it got simplified.
 * Alan disagrees, saying that need the complexity to handle the weird
 * nature of ISA shared interrupts.  (This is a special exception.)
 *
 * In order to handle ISA shared interrupts properly, we need to check
 * that all ports have been serviced, and therefore the ISA interrupt
 * line has been de-asserted.
 *
 * This means we need to loop through all ports. checking that they
 * don't have an interrupt pending.
 */
static irqreturn_t m32r_sio_interrupt(int irq, void *dev_id,
	struct pt_regs *regs)
{
	struct irq_info *i = dev_id;
	struct list_head *l, *end = NULL;
	int pass_counter = 0;

	DEBUG_INTR("m32r_sio_interrupt(%d)...", irq);

#ifdef CONFIG_SERIAL_M32R_PLDSIO
//	if (irq == PLD_IRQ_SIO0_SND)
//		irq = PLD_IRQ_SIO0_RCV;
#else
	if (irq == M32R_IRQ_SIO0_S)
		irq = M32R_IRQ_SIO0_R;
#endif

	spin_lock(&i->lock);

	l = i->head;
	do {
		struct uart_sio_port *up;
		unsigned int sts;

		up = list_entry(l, struct uart_sio_port, list);

		sts = sio_in(up, SIOSTS);
		if (sts & 0x5) {
			spin_lock(&up->port.lock);
			m32r_sio_handle_port(up, sts, regs);
			spin_unlock(&up->port.lock);

			end = NULL;
		} else if (end == NULL)
			end = l;

		l = l->next;

		if (l == i->head && pass_counter++ > PASS_LIMIT) {
			if (sts & 0xe0)
				sio_error(&sts);
			break;
		}
	} while (l != end);

	spin_unlock(&i->lock);

	DEBUG_INTR("end.\n");

	return IRQ_HANDLED;
}

/*
 * To support ISA shared interrupts, we need to have one interrupt
 * handler that ensures that the IRQ line has been deasserted
 * before returning.  Failing to do this will result in the IRQ
 * line being stuck active, and, since ISA irqs are edge triggered,
 * no more IRQs will be seen.
 */
static void serial_do_unlink(struct irq_info *i, struct uart_sio_port *up)
{
	spin_lock_irq(&i->lock);

	if (!list_empty(i->head)) {
		if (i->head == &up->list)
			i->head = i->head->next;
		list_del(&up->list);
	} else {
		BUG_ON(i->head != &up->list);
		i->head = NULL;
	}

	spin_unlock_irq(&i->lock);
}

static int serial_link_irq_chain(struct uart_sio_port *up)
{
	struct irq_info *i = irq_lists + up->port.irq;
	int ret, irq_flags = up->port.flags & UPF_SHARE_IRQ ? SA_SHIRQ : 0;

	spin_lock_irq(&i->lock);

	if (i->head) {
		list_add(&up->list, i->head);
		spin_unlock_irq(&i->lock);

		ret = 0;
	} else {
		INIT_LIST_HEAD(&up->list);
		i->head = &up->list;
		spin_unlock_irq(&i->lock);

		ret = request_irq(up->port.irq, m32r_sio_interrupt,
				  irq_flags, "SIO0-RX", i);
		ret |= request_irq(up->port.irq + 1, m32r_sio_interrupt,
				  irq_flags, "SIO0-TX", i);
		if (ret < 0)
			serial_do_unlink(i, up);
	}

	return ret;
}

static void serial_unlink_irq_chain(struct uart_sio_port *up)
{
	struct irq_info *i = irq_lists + up->port.irq;

	BUG_ON(i->head == NULL);

	if (list_empty(i->head)) {
		free_irq(up->port.irq, i);
		free_irq(up->port.irq + 1, i);
	}

	serial_do_unlink(i, up);
}

/*
 * This function is used to handle ports that do not have an interrupt.
 */
static void m32r_sio_timeout(unsigned long data)
{
	struct uart_sio_port *up = (struct uart_sio_port *)data;
	unsigned int timeout;
	unsigned int sts;

	sts = sio_in(up, SIOSTS);
	if (sts & 0x5) {
		spin_lock(&up->port.lock);
		m32r_sio_handle_port(up, sts, NULL);
		spin_unlock(&up->port.lock);
	}

	timeout = up->port.timeout;
	timeout = timeout > 6 ? (timeout / 2 - 2) : 1;
	mod_timer(&up->timer, jiffies + timeout);
}

static unsigned int m32r_sio_tx_empty(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;
	unsigned long flags;
	unsigned int ret;

	spin_lock_irqsave(&up->port.lock, flags);
	ret = serial_in(up, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
	spin_unlock_irqrestore(&up->port.lock, flags);

	return ret;
}

static unsigned int m32r_sio_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void m32r_sio_set_mctrl(struct uart_port *port, unsigned int mctrl)
{

}

static void m32r_sio_break_ctl(struct uart_port *port, int break_state)
{

}

static int m32r_sio_startup(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;
	int retval;

	sio_init();

	/*
	 * If the "interrupt" for this port doesn't correspond with any
	 * hardware interrupt, we use a timer-based system.  The original
	 * driver used to do this with IRQ0.
	 */
	if (!is_real_interrupt(up->port.irq)) {
		unsigned int timeout = up->port.timeout;

		timeout = timeout > 6 ? (timeout / 2 - 2) : 1;

		up->timer.data = (unsigned long)up;
		mod_timer(&up->timer, jiffies + timeout);
	} else {
		retval = serial_link_irq_chain(up);
		if (retval)
			return retval;
	}

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via set_termios(), which will be occurring imminently
	 * anyway, so we don't enable them here.
	 * - M32R_SIO: 0x0c
	 * - M32R_PLDSIO: 0x04
	 */
	up->ier = UART_IER_MSI | UART_IER_RLSI | UART_IER_RDI;
	sio_out(up, SIOTRCR, up->ier);

	/*
	 * And clear the interrupt registers again for luck.
	 */
	sio_reset();

	return 0;
}

static void m32r_sio_shutdown(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	/*
	 * Disable interrupts from this port
	 */
	up->ier = 0;
	sio_out(up, SIOTRCR, 0);

	/*
	 * Disable break condition and FIFOs
	 */

	sio_init();

	if (!is_real_interrupt(up->port.irq))
		del_timer_sync(&up->timer);
	else
		serial_unlink_irq_chain(up);
}

static unsigned int m32r_sio_get_divisor(struct uart_port *port,
	unsigned int baud)
{
	return uart_get_divisor(port, baud);
}

static void m32r_sio_set_termios(struct uart_port *port,
	struct termios *termios, struct termios *old)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;
	unsigned char cval = 0;
	unsigned long flags;
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
#ifdef CMSPAR
	if (termios->c_cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	/*
	 * Ask the core to calculate the divisor for us.
	 */
#ifdef CONFIG_SERIAL_M32R_PLDSIO
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/4);
#else
	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
#endif
	quot = m32r_sio_get_divisor(port, baud);

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	sio_set_baud_rate(baud);

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	up->port.read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= UART_LSR_BI;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((termios->c_cflag & CREAD) == 0)
		up->port.ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	up->ier &= ~UART_IER_MSI;
	if (UART_ENABLE_MS(&up->port, termios->c_cflag))
		up->ier |= UART_IER_MSI;

	serial_out(up, UART_IER, up->ier);

	up->lcr = cval;					/* Save LCR */
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static void m32r_sio_pm(struct uart_port *port, unsigned int state,
	unsigned int oldstate)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	if (up->pm)
		up->pm(port, state, oldstate);
}

/*
 * Resource handling.  This is complicated by the fact that resources
 * depend on the port type.  Maybe we should be claiming the standard
 * 8250 ports, and then trying to get other resources as necessary?
 */
static int
m32r_sio_request_std_resource(struct uart_sio_port *up, struct resource **res)
{
	unsigned int size = 8 << up->port.regshift;
#ifndef CONFIG_SERIAL_M32R_PLDSIO
	unsigned long start;
#endif
	int ret = 0;

	switch (up->port.iotype) {
	case UPIO_MEM:
		if (up->port.mapbase) {
#ifdef CONFIG_SERIAL_M32R_PLDSIO
			*res = request_mem_region(up->port.mapbase, size, "serial");
#else
			start = up->port.mapbase;
			*res = request_mem_region(start, size, "serial");
#endif
			if (!*res)
				ret = -EBUSY;
		}
		break;

	case UPIO_PORT:
		*res = request_region(up->port.iobase, size, "serial");
		if (!*res)
			ret = -EBUSY;
		break;
	}
	return ret;
}

static void m32r_sio_release_port(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;
	unsigned long start, offset = 0, size = 0;

	size <<= up->port.regshift;

	switch (up->port.iotype) {
	case UPIO_MEM:
		if (up->port.mapbase) {
			/*
			 * Unmap the area.
			 */
			iounmap(up->port.membase);
			up->port.membase = NULL;

			start = up->port.mapbase;

			if (size)
				release_mem_region(start + offset, size);
			release_mem_region(start, 8 << up->port.regshift);
		}
		break;

	case UPIO_PORT:
		start = up->port.iobase;

		if (size)
			release_region(start + offset, size);
		release_region(start + offset, 8 << up->port.regshift);
		break;

	default:
		break;
	}
}

static int m32r_sio_request_port(struct uart_port *port)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;
	struct resource *res = NULL;
	int ret = 0;

	ret = m32r_sio_request_std_resource(up, &res);

	/*
	 * If we have a mapbase, then request that as well.
	 */
	if (ret == 0 && up->port.flags & UPF_IOREMAP) {
		int size = res->end - res->start + 1;

		up->port.membase = ioremap(up->port.mapbase, size);
		if (!up->port.membase)
			ret = -ENOMEM;
	}

	if (ret < 0) {
		if (res)
			release_resource(res);
	}

	return ret;
}

static void m32r_sio_config_port(struct uart_port *port, int flags)
{
	struct uart_sio_port *up = (struct uart_sio_port *)port;

	spin_lock_irqsave(&up->port.lock, flags);

	up->port.type = (PORT_M32R_SIO - PORT_M32R_BASE + 1);
	up->port.fifosize = uart_config[up->port.type].dfl_xmit_fifo_size;

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int
m32r_sio_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->irq >= NR_IRQS || ser->irq < 0 ||
	    ser->baud_base < 9600 || ser->type < PORT_UNKNOWN ||
	    ser->type >= ARRAY_SIZE(uart_config))
		return -EINVAL;
	return 0;
}

static const char *
m32r_sio_type(struct uart_port *port)
{
	int type = port->type;

	if (type >= ARRAY_SIZE(uart_config))
		type = 0;
	return uart_config[type].name;
}

static struct uart_ops m32r_sio_pops = {
	.tx_empty	= m32r_sio_tx_empty,
	.set_mctrl	= m32r_sio_set_mctrl,
	.get_mctrl	= m32r_sio_get_mctrl,
	.stop_tx	= m32r_sio_stop_tx,
	.start_tx	= m32r_sio_start_tx,
	.stop_rx	= m32r_sio_stop_rx,
	.enable_ms	= m32r_sio_enable_ms,
	.break_ctl	= m32r_sio_break_ctl,
	.startup	= m32r_sio_startup,
	.shutdown	= m32r_sio_shutdown,
	.set_termios	= m32r_sio_set_termios,
	.pm		= m32r_sio_pm,
	.type		= m32r_sio_type,
	.release_port	= m32r_sio_release_port,
	.request_port	= m32r_sio_request_port,
	.config_port	= m32r_sio_config_port,
	.verify_port	= m32r_sio_verify_port,
};

static struct uart_sio_port m32r_sio_ports[UART_NR];

static void __init m32r_sio_init_ports(void)
{
	struct uart_sio_port *up;
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0, up = m32r_sio_ports; i < ARRAY_SIZE(old_serial_port);
	     i++, up++) {
		up->port.iobase   = old_serial_port[i].port;
		up->port.irq      = irq_canonicalize(old_serial_port[i].irq);
		up->port.uartclk  = old_serial_port[i].baud_base * 16;
		up->port.flags    = old_serial_port[i].flags;
		up->port.membase  = old_serial_port[i].iomem_base;
		up->port.iotype   = old_serial_port[i].io_type;
		up->port.regshift = old_serial_port[i].iomem_reg_shift;
		up->port.ops      = &m32r_sio_pops;
	}
}

static void __init m32r_sio_register_ports(struct uart_driver *drv)
{
	int i;

	m32r_sio_init_ports();

	for (i = 0; i < UART_NR; i++) {
		struct uart_sio_port *up = &m32r_sio_ports[i];

		up->port.line = i;
		up->port.ops = &m32r_sio_pops;
		init_timer(&up->timer);
		up->timer.function = m32r_sio_timeout;

		/*
		 * ALPHA_KLUDGE_MCR needs to be killed.
		 */
		up->mcr_mask = ~ALPHA_KLUDGE_MCR;
		up->mcr_force = ALPHA_KLUDGE_MCR;

		uart_add_one_port(drv, &up->port);
	}
}

#ifdef CONFIG_SERIAL_M32R_SIO_CONSOLE

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_sio_port *up)
{
	unsigned int status, tmout = 10000;

	/* Wait up to 10ms for the character(s) to be sent. */
	do {
		status = sio_in(up, SIOSTS);

		if (--tmout == 0)
			break;
		udelay(1);
	} while ((status & UART_EMPTY) != UART_EMPTY);

	/* Wait up to 1s for flow control if necessary */
	if (up->port.flags & UPF_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout)
			udelay(1);
	}
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void m32r_sio_console_write(struct console *co, const char *s,
	unsigned int count)
{
	struct uart_sio_port *up = &m32r_sio_ports[co->index];
	unsigned int ier;
	int i;

	/*
	 *	First save the UER then disable the interrupts
	 */
	ier = sio_in(up, SIOTRCR);
	sio_out(up, SIOTRCR, 0);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		wait_for_xmitr(up);

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		sio_out(up, SIOTXB, *s);

		if (*s == 10) {
			wait_for_xmitr(up);
			sio_out(up, SIOTXB, 13);
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(up);
	sio_out(up, SIOTRCR, ier);
}

static int __init m32r_sio_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	port = &m32r_sio_ports[co->index].port;

	/*
	 * Temporary fix.
	 */
	spin_lock_init(&port->lock);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver m32r_sio_reg;
static struct console m32r_sio_console = {
	.name		= "ttyS",
	.write		= m32r_sio_console_write,
	.device		= uart_console_device,
	.setup		= m32r_sio_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &m32r_sio_reg,
};

static int __init m32r_sio_console_init(void)
{
	sio_reset();
	sio_init();
	m32r_sio_init_ports();
	register_console(&m32r_sio_console);
	return 0;
}
console_initcall(m32r_sio_console_init);

#define M32R_SIO_CONSOLE	&m32r_sio_console
#else
#define M32R_SIO_CONSOLE	NULL
#endif

static struct uart_driver m32r_sio_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "sio",
	.devfs_name		= "tts/",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr			= UART_NR,
	.cons			= M32R_SIO_CONSOLE,
};

/**
 *	m32r_sio_suspend_port - suspend one serial port
 *	@line: serial line number
 *
 *	Suspend one serial port.
 */
void m32r_sio_suspend_port(int line)
{
	uart_suspend_port(&m32r_sio_reg, &m32r_sio_ports[line].port);
}

/**
 *	m32r_sio_resume_port - resume one serial port
 *	@line: serial line number
 *
 *	Resume one serial port.
 */
void m32r_sio_resume_port(int line)
{
	uart_resume_port(&m32r_sio_reg, &m32r_sio_ports[line].port);
}

static int __init m32r_sio_init(void)
{
	int ret, i;

	printk(KERN_INFO "Serial: M32R SIO driver $Revision: 1.11 $ ");

	for (i = 0; i < NR_IRQS; i++)
		spin_lock_init(&irq_lists[i].lock);

	ret = uart_register_driver(&m32r_sio_reg);
	if (ret >= 0)
		m32r_sio_register_ports(&m32r_sio_reg);

	return ret;
}

static void __exit m32r_sio_exit(void)
{
	int i;

	for (i = 0; i < UART_NR; i++)
		uart_remove_one_port(&m32r_sio_reg, &m32r_sio_ports[i].port);

	uart_unregister_driver(&m32r_sio_reg);
}

module_init(m32r_sio_init);
module_exit(m32r_sio_exit);

EXPORT_SYMBOL(m32r_sio_suspend_port);
EXPORT_SYMBOL(m32r_sio_resume_port);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic M32R SIO serial driver $Revision: 1.11 $");
