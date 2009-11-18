/*
 * drivers/serial/sh-sci.c
 *
 * SuperH on-chip serial module support.  (SCI with no FIFO / with FIFO)
 *
 *  Copyright (C) 2002 - 2008  Paul Mundt
 *  Modified to support SH7720 SCIF. Markus Brunner, Mark Jonas (Jul 2007).
 *
 * based off of the old drivers/char/sh-sci.c by:
 *
 *   Copyright (C) 1999, 2000  Niibe Yutaka
 *   Copyright (C) 2000  Sugioka Toshinobu
 *   Modified to support multiple serial ports. Stuart Menefy (May 2000).
 *   Modified to support SecureEdge. David McCullough (2002)
 *   Modified to support SH7300 SCIF. Takashi Kusuda (Jun 2003).
 *   Removed SH7300 support (Jul 2007).
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#if defined(CONFIG_SERIAL_SH_SCI_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#undef DEBUG

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <linux/serial_sci.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/list.h>

#ifdef CONFIG_SUPERH
#include <asm/clock.h>
#include <asm/sh_bios.h>
#endif

#ifdef CONFIG_H8300
#include <asm/gpio.h>
#endif

#include "sh-sci.h"

struct sci_port {
	struct uart_port	port;

	/* Port type */
	unsigned int		type;

	/* Port IRQs: ERI, RXI, TXI, BRI (optional) */
	unsigned int		irqs[SCIx_NR_IRQS];

	/* Port enable callback */
	void			(*enable)(struct uart_port *port);

	/* Port disable callback */
	void			(*disable)(struct uart_port *port);

	/* Break timer */
	struct timer_list	break_timer;
	int			break_flag;

#ifdef CONFIG_HAVE_CLK
	/* Interface clock */
	struct clk		*iclk;
	/* Data clock */
	struct clk		*dclk;
#endif
	struct list_head	node;
};

struct sh_sci_priv {
	spinlock_t lock;
	struct list_head ports;

#ifdef CONFIG_HAVE_CLK
	struct notifier_block clk_nb;
#endif
};

/* Function prototypes */
static void sci_stop_tx(struct uart_port *port);

#define SCI_NPORTS CONFIG_SERIAL_SH_SCI_NR_UARTS

static struct sci_port sci_ports[SCI_NPORTS];
static struct uart_driver sci_uart_driver;

static inline struct sci_port *
to_sci_port(struct uart_port *uart)
{
	return container_of(uart, struct sci_port, port);
}

#if defined(CONFIG_CONSOLE_POLL) || defined(CONFIG_SERIAL_SH_SCI_CONSOLE)

#ifdef CONFIG_CONSOLE_POLL
static inline void handle_error(struct uart_port *port)
{
	/* Clear error flags */
	sci_out(port, SCxSR, SCxSR_ERROR_CLEAR(port));
}

static int sci_poll_get_char(struct uart_port *port)
{
	unsigned short status;
	int c;

	do {
		status = sci_in(port, SCxSR);
		if (status & SCxSR_ERRORS(port)) {
			handle_error(port);
			continue;
		}
	} while (!(status & SCxSR_RDxF(port)));

	c = sci_in(port, SCxRDR);

	/* Dummy read */
	sci_in(port, SCxSR);
	sci_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));

	return c;
}
#endif

static void sci_poll_put_char(struct uart_port *port, unsigned char c)
{
	unsigned short status;

	do {
		status = sci_in(port, SCxSR);
	} while (!(status & SCxSR_TDxE(port)));

	sci_out(port, SCxTDR, c);
	sci_out(port, SCxSR, SCxSR_TDxE_CLEAR(port) & ~SCxSR_TEND(port));
}
#endif /* CONFIG_CONSOLE_POLL || CONFIG_SERIAL_SH_SCI_CONSOLE */

#if defined(__H8300S__)
enum { sci_disable, sci_enable };

static void h8300_sci_config(struct uart_port *port, unsigned int ctrl)
{
	volatile unsigned char *mstpcrl = (volatile unsigned char *)MSTPCRL;
	int ch = (port->mapbase  - SMR0) >> 3;
	unsigned char mask = 1 << (ch+1);

	if (ctrl == sci_disable)
		*mstpcrl |= mask;
	else
		*mstpcrl &= ~mask;
}

static void h8300_sci_enable(struct uart_port *port)
{
	h8300_sci_config(port, sci_enable);
}

static void h8300_sci_disable(struct uart_port *port)
{
	h8300_sci_config(port, sci_disable);
}
#endif

#if defined(__H8300H__) || defined(__H8300S__)
static void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	int ch = (port->mapbase - SMR0) >> 3;

	/* set DDR regs */
	H8300_GPIO_DDR(h8300_sci_pins[ch].port,
		       h8300_sci_pins[ch].rx,
		       H8300_GPIO_INPUT);
	H8300_GPIO_DDR(h8300_sci_pins[ch].port,
		       h8300_sci_pins[ch].tx,
		       H8300_GPIO_OUTPUT);

	/* tx mark output*/
	H8300_SCI_DR(ch) |= h8300_sci_pins[ch].tx;
}
#elif defined(CONFIG_CPU_SUBTYPE_SH7710) || defined(CONFIG_CPU_SUBTYPE_SH7712)
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	if (port->mapbase == 0xA4400000) {
		__raw_writew(__raw_readw(PACR) & 0xffc0, PACR);
		__raw_writew(__raw_readw(PBCR) & 0x0fff, PBCR);
	} else if (port->mapbase == 0xA4410000)
		__raw_writew(__raw_readw(PBCR) & 0xf003, PBCR);
}
#elif defined(CONFIG_CPU_SUBTYPE_SH7720) || defined(CONFIG_CPU_SUBTYPE_SH7721)
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	unsigned short data;

	if (cflag & CRTSCTS) {
		/* enable RTS/CTS */
		if (port->mapbase == 0xa4430000) { /* SCIF0 */
			/* Clear PTCR bit 9-2; enable all scif pins but sck */
			data = __raw_readw(PORT_PTCR);
			__raw_writew((data & 0xfc03), PORT_PTCR);
		} else if (port->mapbase == 0xa4438000) { /* SCIF1 */
			/* Clear PVCR bit 9-2 */
			data = __raw_readw(PORT_PVCR);
			__raw_writew((data & 0xfc03), PORT_PVCR);
		}
	} else {
		if (port->mapbase == 0xa4430000) { /* SCIF0 */
			/* Clear PTCR bit 5-2; enable only tx and rx  */
			data = __raw_readw(PORT_PTCR);
			__raw_writew((data & 0xffc3), PORT_PTCR);
		} else if (port->mapbase == 0xa4438000) { /* SCIF1 */
			/* Clear PVCR bit 5-2 */
			data = __raw_readw(PORT_PVCR);
			__raw_writew((data & 0xffc3), PORT_PVCR);
		}
	}
}
#elif defined(CONFIG_CPU_SH3)
/* For SH7705, SH7706, SH7707, SH7709, SH7709A, SH7729 */
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	unsigned short data;

	/* We need to set SCPCR to enable RTS/CTS */
	data = __raw_readw(SCPCR);
	/* Clear out SCP7MD1,0, SCP6MD1,0, SCP4MD1,0*/
	__raw_writew(data & 0x0fcf, SCPCR);

	if (!(cflag & CRTSCTS)) {
		/* We need to set SCPCR to enable RTS/CTS */
		data = __raw_readw(SCPCR);
		/* Clear out SCP7MD1,0, SCP4MD1,0,
		   Set SCP6MD1,0 = {01} (output)  */
		__raw_writew((data & 0x0fcf) | 0x1000, SCPCR);

		data = ctrl_inb(SCPDR);
		/* Set /RTS2 (bit6) = 0 */
		ctrl_outb(data & 0xbf, SCPDR);
	}
}
#elif defined(CONFIG_CPU_SUBTYPE_SH7722)
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	unsigned short data;

	if (port->mapbase == 0xffe00000) {
		data = __raw_readw(PSCR);
		data &= ~0x03cf;
		if (!(cflag & CRTSCTS))
			data |= 0x0340;

		__raw_writew(data, PSCR);
	}
}
#elif defined(CONFIG_CPU_SUBTYPE_SH7757) || \
      defined(CONFIG_CPU_SUBTYPE_SH7763) || \
      defined(CONFIG_CPU_SUBTYPE_SH7780) || \
      defined(CONFIG_CPU_SUBTYPE_SH7785) || \
      defined(CONFIG_CPU_SUBTYPE_SH7786) || \
      defined(CONFIG_CPU_SUBTYPE_SHX3)
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	if (!(cflag & CRTSCTS))
		__raw_writew(0x0080, SCSPTR0); /* Set RTS = 1 */
}
#elif defined(CONFIG_CPU_SH4) && !defined(CONFIG_CPU_SH4A)
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	if (!(cflag & CRTSCTS))
		__raw_writew(0x0080, SCSPTR2); /* Set RTS = 1 */
}
#else
static inline void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	/* Nothing to do */
}
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7760) || \
    defined(CONFIG_CPU_SUBTYPE_SH7780) || \
    defined(CONFIG_CPU_SUBTYPE_SH7785) || \
    defined(CONFIG_CPU_SUBTYPE_SH7786)
static inline int scif_txroom(struct uart_port *port)
{
	return SCIF_TXROOM_MAX - (sci_in(port, SCTFDR) & 0xff);
}

static inline int scif_rxroom(struct uart_port *port)
{
	return sci_in(port, SCRFDR) & 0xff;
}
#elif defined(CONFIG_CPU_SUBTYPE_SH7763)
static inline int scif_txroom(struct uart_port *port)
{
	if ((port->mapbase == 0xffe00000) ||
	    (port->mapbase == 0xffe08000)) {
		/* SCIF0/1*/
		return SCIF_TXROOM_MAX - (sci_in(port, SCTFDR) & 0xff);
	} else {
		/* SCIF2 */
		return SCIF2_TXROOM_MAX - (sci_in(port, SCFDR) >> 8);
	}
}

static inline int scif_rxroom(struct uart_port *port)
{
	if ((port->mapbase == 0xffe00000) ||
	    (port->mapbase == 0xffe08000)) {
		/* SCIF0/1*/
		return sci_in(port, SCRFDR) & 0xff;
	} else {
		/* SCIF2 */
		return sci_in(port, SCFDR) & SCIF2_RFDC_MASK;
	}
}
#else
static inline int scif_txroom(struct uart_port *port)
{
	return SCIF_TXROOM_MAX - (sci_in(port, SCFDR) >> 8);
}

static inline int scif_rxroom(struct uart_port *port)
{
	return sci_in(port, SCFDR) & SCIF_RFDC_MASK;
}
#endif

static inline int sci_txroom(struct uart_port *port)
{
	return (sci_in(port, SCxSR) & SCI_TDRE) != 0;
}

static inline int sci_rxroom(struct uart_port *port)
{
	return (sci_in(port, SCxSR) & SCxSR_RDxF(port)) != 0;
}

/* ********************************************************************** *
 *                   the interrupt related routines                       *
 * ********************************************************************** */

static void sci_transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int stopped = uart_tx_stopped(port);
	unsigned short status;
	unsigned short ctrl;
	int count;

	status = sci_in(port, SCxSR);
	if (!(status & SCxSR_TDxE(port))) {
		ctrl = sci_in(port, SCSCR);
		if (uart_circ_empty(xmit))
			ctrl &= ~SCI_CTRL_FLAGS_TIE;
		else
			ctrl |= SCI_CTRL_FLAGS_TIE;
		sci_out(port, SCSCR, ctrl);
		return;
	}

	if (port->type == PORT_SCI)
		count = sci_txroom(port);
	else
		count = scif_txroom(port);

	do {
		unsigned char c;

		if (port->x_char) {
			c = port->x_char;
			port->x_char = 0;
		} else if (!uart_circ_empty(xmit) && !stopped) {
			c = xmit->buf[xmit->tail];
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		} else {
			break;
		}

		sci_out(port, SCxTDR, c);

		port->icount.tx++;
	} while (--count > 0);

	sci_out(port, SCxSR, SCxSR_TDxE_CLEAR(port));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (uart_circ_empty(xmit)) {
		sci_stop_tx(port);
	} else {
		ctrl = sci_in(port, SCSCR);

		if (port->type != PORT_SCI) {
			sci_in(port, SCxSR); /* Dummy read */
			sci_out(port, SCxSR, SCxSR_TDxE_CLEAR(port));
		}

		ctrl |= SCI_CTRL_FLAGS_TIE;
		sci_out(port, SCSCR, ctrl);
	}
}

/* On SH3, SCIF may read end-of-break as a space->mark char */
#define STEPFN(c)  ({int __c = (c); (((__c-1)|(__c)) == -1); })

static inline void sci_receive_chars(struct uart_port *port)
{
	struct sci_port *sci_port = to_sci_port(port);
	struct tty_struct *tty = port->state->port.tty;
	int i, count, copied = 0;
	unsigned short status;
	unsigned char flag;

	status = sci_in(port, SCxSR);
	if (!(status & SCxSR_RDxF(port)))
		return;

	while (1) {
		if (port->type == PORT_SCI)
			count = sci_rxroom(port);
		else
			count = scif_rxroom(port);

		/* Don't copy more bytes than there is room for in the buffer */
		count = tty_buffer_request_room(tty, count);

		/* If for any reason we can't copy more data, we're done! */
		if (count == 0)
			break;

		if (port->type == PORT_SCI) {
			char c = sci_in(port, SCxRDR);
			if (uart_handle_sysrq_char(port, c) ||
			    sci_port->break_flag)
				count = 0;
			else
				tty_insert_flip_char(tty, c, TTY_NORMAL);
		} else {
			for (i = 0; i < count; i++) {
				char c = sci_in(port, SCxRDR);
				status = sci_in(port, SCxSR);
#if defined(CONFIG_CPU_SH3)
				/* Skip "chars" during break */
				if (sci_port->break_flag) {
					if ((c == 0) &&
					    (status & SCxSR_FER(port))) {
						count--; i--;
						continue;
					}

					/* Nonzero => end-of-break */
					dev_dbg(port->dev, "debounce<%02x>\n", c);
					sci_port->break_flag = 0;

					if (STEPFN(c)) {
						count--; i--;
						continue;
					}
				}
#endif /* CONFIG_CPU_SH3 */
				if (uart_handle_sysrq_char(port, c)) {
					count--; i--;
					continue;
				}

				/* Store data and status */
				if (status&SCxSR_FER(port)) {
					flag = TTY_FRAME;
					dev_notice(port->dev, "frame error\n");
				} else if (status&SCxSR_PER(port)) {
					flag = TTY_PARITY;
					dev_notice(port->dev, "parity error\n");
				} else
					flag = TTY_NORMAL;

				tty_insert_flip_char(tty, c, flag);
			}
		}

		sci_in(port, SCxSR); /* dummy read */
		sci_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));

		copied += count;
		port->icount.rx += count;
	}

	if (copied) {
		/* Tell the rest of the system the news. New characters! */
		tty_flip_buffer_push(tty);
	} else {
		sci_in(port, SCxSR); /* dummy read */
		sci_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));
	}
}

#define SCI_BREAK_JIFFIES (HZ/20)
/* The sci generates interrupts during the break,
 * 1 per millisecond or so during the break period, for 9600 baud.
 * So dont bother disabling interrupts.
 * But dont want more than 1 break event.
 * Use a kernel timer to periodically poll the rx line until
 * the break is finished.
 */
static void sci_schedule_break_timer(struct sci_port *port)
{
	port->break_timer.expires = jiffies + SCI_BREAK_JIFFIES;
	add_timer(&port->break_timer);
}
/* Ensure that two consecutive samples find the break over. */
static void sci_break_timer(unsigned long data)
{
	struct sci_port *port = (struct sci_port *)data;

	if (sci_rxd_in(&port->port) == 0) {
		port->break_flag = 1;
		sci_schedule_break_timer(port);
	} else if (port->break_flag == 1) {
		/* break is over. */
		port->break_flag = 2;
		sci_schedule_break_timer(port);
	} else
		port->break_flag = 0;
}

static inline int sci_handle_errors(struct uart_port *port)
{
	int copied = 0;
	unsigned short status = sci_in(port, SCxSR);
	struct tty_struct *tty = port->state->port.tty;

	if (status & SCxSR_ORER(port)) {
		/* overrun error */
		if (tty_insert_flip_char(tty, 0, TTY_OVERRUN))
			copied++;

		dev_notice(port->dev, "overrun error");
	}

	if (status & SCxSR_FER(port)) {
		if (sci_rxd_in(port) == 0) {
			/* Notify of BREAK */
			struct sci_port *sci_port = to_sci_port(port);

			if (!sci_port->break_flag) {
				sci_port->break_flag = 1;
				sci_schedule_break_timer(sci_port);

				/* Do sysrq handling. */
				if (uart_handle_break(port))
					return 0;

				dev_dbg(port->dev, "BREAK detected\n");

				if (tty_insert_flip_char(tty, 0, TTY_BREAK))
					copied++;
			}

		} else {
			/* frame error */
			if (tty_insert_flip_char(tty, 0, TTY_FRAME))
				copied++;

			dev_notice(port->dev, "frame error\n");
		}
	}

	if (status & SCxSR_PER(port)) {
		/* parity error */
		if (tty_insert_flip_char(tty, 0, TTY_PARITY))
			copied++;

		dev_notice(port->dev, "parity error");
	}

	if (copied)
		tty_flip_buffer_push(tty);

	return copied;
}

static inline int sci_handle_fifo_overrun(struct uart_port *port)
{
	struct tty_struct *tty = port->state->port.tty;
	int copied = 0;

	if (port->type != PORT_SCIF)
		return 0;

	if ((sci_in(port, SCLSR) & SCIF_ORER) != 0) {
		sci_out(port, SCLSR, 0);

		tty_insert_flip_char(tty, 0, TTY_OVERRUN);
		tty_flip_buffer_push(tty);

		dev_notice(port->dev, "overrun error\n");
		copied++;
	}

	return copied;
}

static inline int sci_handle_breaks(struct uart_port *port)
{
	int copied = 0;
	unsigned short status = sci_in(port, SCxSR);
	struct tty_struct *tty = port->state->port.tty;
	struct sci_port *s = to_sci_port(port);

	if (uart_handle_break(port))
		return 0;

	if (!s->break_flag && status & SCxSR_BRK(port)) {
#if defined(CONFIG_CPU_SH3)
		/* Debounce break */
		s->break_flag = 1;
#endif
		/* Notify of BREAK */
		if (tty_insert_flip_char(tty, 0, TTY_BREAK))
			copied++;

		dev_dbg(port->dev, "BREAK detected\n");
	}

	if (copied)
		tty_flip_buffer_push(tty);

	copied += sci_handle_fifo_overrun(port);

	return copied;
}

static irqreturn_t sci_rx_interrupt(int irq, void *port)
{
	/* I think sci_receive_chars has to be called irrespective
	 * of whether the I_IXOFF is set, otherwise, how is the interrupt
	 * to be disabled?
	 */
	sci_receive_chars(port);

	return IRQ_HANDLED;
}

static irqreturn_t sci_tx_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	sci_transmit_chars(port);
	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t sci_er_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;

	/* Handle errors */
	if (port->type == PORT_SCI) {
		if (sci_handle_errors(port)) {
			/* discard character in rx buffer */
			sci_in(port, SCxSR);
			sci_out(port, SCxSR, SCxSR_RDxF_CLEAR(port));
		}
	} else {
		sci_handle_fifo_overrun(port);
		sci_rx_interrupt(irq, ptr);
	}

	sci_out(port, SCxSR, SCxSR_ERROR_CLEAR(port));

	/* Kick the transmission */
	sci_tx_interrupt(irq, ptr);

	return IRQ_HANDLED;
}

static irqreturn_t sci_br_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;

	/* Handle BREAKs */
	sci_handle_breaks(port);
	sci_out(port, SCxSR, SCxSR_BREAK_CLEAR(port));

	return IRQ_HANDLED;
}

static irqreturn_t sci_mpxed_interrupt(int irq, void *ptr)
{
	unsigned short ssr_status, scr_status, err_enabled;
	struct uart_port *port = ptr;
	irqreturn_t ret = IRQ_NONE;

	ssr_status = sci_in(port, SCxSR);
	scr_status = sci_in(port, SCSCR);
	err_enabled = scr_status & (SCI_CTRL_FLAGS_REIE | SCI_CTRL_FLAGS_RIE);

	/* Tx Interrupt */
	if ((ssr_status & SCxSR_TDxE(port)) && (scr_status & SCI_CTRL_FLAGS_TIE))
		ret = sci_tx_interrupt(irq, ptr);
	/* Rx Interrupt */
	if ((ssr_status & SCxSR_RDxF(port)) && (scr_status & SCI_CTRL_FLAGS_RIE))
		ret = sci_rx_interrupt(irq, ptr);
	/* Error Interrupt */
	if ((ssr_status & SCxSR_ERRORS(port)) && err_enabled)
		ret = sci_er_interrupt(irq, ptr);
	/* Break Interrupt */
	if ((ssr_status & SCxSR_BRK(port)) && err_enabled)
		ret = sci_br_interrupt(irq, ptr);

	return ret;
}

#ifdef CONFIG_HAVE_CLK
/*
 * Here we define a transistion notifier so that we can update all of our
 * ports' baud rate when the peripheral clock changes.
 */
static int sci_notifier(struct notifier_block *self,
			unsigned long phase, void *p)
{
	struct sh_sci_priv *priv = container_of(self,
						struct sh_sci_priv, clk_nb);
	struct sci_port *sci_port;
	unsigned long flags;

	if ((phase == CPUFREQ_POSTCHANGE) ||
	    (phase == CPUFREQ_RESUMECHANGE)) {
		spin_lock_irqsave(&priv->lock, flags);
		list_for_each_entry(sci_port, &priv->ports, node)
			sci_port->port.uartclk = clk_get_rate(sci_port->dclk);

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return NOTIFY_OK;
}

static void sci_clk_enable(struct uart_port *port)
{
	struct sci_port *sci_port = to_sci_port(port);

	clk_enable(sci_port->dclk);
	sci_port->port.uartclk = clk_get_rate(sci_port->dclk);

	if (sci_port->iclk)
		clk_enable(sci_port->iclk);
}

static void sci_clk_disable(struct uart_port *port)
{
	struct sci_port *sci_port = to_sci_port(port);

	if (sci_port->iclk)
		clk_disable(sci_port->iclk);

	clk_disable(sci_port->dclk);
}
#endif

static int sci_request_irq(struct sci_port *port)
{
	int i;
	irqreturn_t (*handlers[4])(int irq, void *ptr) = {
		sci_er_interrupt, sci_rx_interrupt, sci_tx_interrupt,
		sci_br_interrupt,
	};
	const char *desc[] = { "SCI Receive Error", "SCI Receive Data Full",
			       "SCI Transmit Data Empty", "SCI Break" };

	if (port->irqs[0] == port->irqs[1]) {
		if (unlikely(!port->irqs[0]))
			return -ENODEV;

		if (request_irq(port->irqs[0], sci_mpxed_interrupt,
				IRQF_DISABLED, "sci", port)) {
			dev_err(port->port.dev, "Can't allocate IRQ\n");
			return -ENODEV;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(handlers); i++) {
			if (unlikely(!port->irqs[i]))
				continue;

			if (request_irq(port->irqs[i], handlers[i],
					IRQF_DISABLED, desc[i], port)) {
				dev_err(port->port.dev, "Can't allocate IRQ\n");
				return -ENODEV;
			}
		}
	}

	return 0;
}

static void sci_free_irq(struct sci_port *port)
{
	int i;

	if (port->irqs[0] == port->irqs[1])
		free_irq(port->irqs[0], port);
	else {
		for (i = 0; i < ARRAY_SIZE(port->irqs); i++) {
			if (!port->irqs[i])
				continue;

			free_irq(port->irqs[i], port);
		}
	}
}

static unsigned int sci_tx_empty(struct uart_port *port)
{
	/* Can't detect */
	return TIOCSER_TEMT;
}

static void sci_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* This routine is used for seting signals of: DTR, DCD, CTS/RTS */
	/* We use SCIF's hardware for CTS/RTS, so don't need any for that. */
	/* If you have signals for DTR and DCD, please implement here. */
}

static unsigned int sci_get_mctrl(struct uart_port *port)
{
	/* This routine is used for geting signals of: DTR, DCD, DSR, RI,
	   and CTS/RTS */

	return TIOCM_DTR | TIOCM_RTS | TIOCM_DSR;
}

static void sci_start_tx(struct uart_port *port)
{
	unsigned short ctrl;

	/* Set TIE (Transmit Interrupt Enable) bit in SCSCR */
	ctrl = sci_in(port, SCSCR);
	ctrl |= SCI_CTRL_FLAGS_TIE;
	sci_out(port, SCSCR, ctrl);
}

static void sci_stop_tx(struct uart_port *port)
{
	unsigned short ctrl;

	/* Clear TIE (Transmit Interrupt Enable) bit in SCSCR */
	ctrl = sci_in(port, SCSCR);
	ctrl &= ~SCI_CTRL_FLAGS_TIE;
	sci_out(port, SCSCR, ctrl);
}

static void sci_start_rx(struct uart_port *port, unsigned int tty_start)
{
	unsigned short ctrl;

	/* Set RIE (Receive Interrupt Enable) bit in SCSCR */
	ctrl = sci_in(port, SCSCR);
	ctrl |= SCI_CTRL_FLAGS_RIE | SCI_CTRL_FLAGS_REIE;
	sci_out(port, SCSCR, ctrl);
}

static void sci_stop_rx(struct uart_port *port)
{
	unsigned short ctrl;

	/* Clear RIE (Receive Interrupt Enable) bit in SCSCR */
	ctrl = sci_in(port, SCSCR);
	ctrl &= ~(SCI_CTRL_FLAGS_RIE | SCI_CTRL_FLAGS_REIE);
	sci_out(port, SCSCR, ctrl);
}

static void sci_enable_ms(struct uart_port *port)
{
	/* Nothing here yet .. */
}

static void sci_break_ctl(struct uart_port *port, int break_state)
{
	/* Nothing here yet .. */
}

static int sci_startup(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);

	if (s->enable)
		s->enable(port);

	sci_request_irq(s);
	sci_start_tx(port);
	sci_start_rx(port, 1);

	return 0;
}

static void sci_shutdown(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);

	sci_stop_rx(port);
	sci_stop_tx(port);
	sci_free_irq(s);

	if (s->disable)
		s->disable(port);
}

static void sci_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	unsigned int status, baud, smr_val;
	int t = -1;

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16);
	if (likely(baud))
		t = SCBRR_VALUE(baud, port->uartclk);

	do {
		status = sci_in(port, SCxSR);
	} while (!(status & SCxSR_TEND(port)));

	sci_out(port, SCSCR, 0x00);	/* TE=0, RE=0, CKE1=0 */

	if (port->type != PORT_SCI)
		sci_out(port, SCFCR, SCFCR_RFRST | SCFCR_TFRST);

	smr_val = sci_in(port, SCSMR) & 3;
	if ((termios->c_cflag & CSIZE) == CS7)
		smr_val |= 0x40;
	if (termios->c_cflag & PARENB)
		smr_val |= 0x20;
	if (termios->c_cflag & PARODD)
		smr_val |= 0x30;
	if (termios->c_cflag & CSTOPB)
		smr_val |= 0x08;

	uart_update_timeout(port, termios->c_cflag, baud);

	sci_out(port, SCSMR, smr_val);

	if (t > 0) {
		if (t >= 256) {
			sci_out(port, SCSMR, (sci_in(port, SCSMR) & ~3) | 1);
			t >>= 2;
		} else
			sci_out(port, SCSMR, sci_in(port, SCSMR) & ~3);

		sci_out(port, SCBRR, t);
		udelay((1000000+(baud-1)) / baud); /* Wait one bit interval */
	}

	sci_init_pins(port, termios->c_cflag);
	sci_out(port, SCFCR, (termios->c_cflag & CRTSCTS) ? SCFCR_MCE : 0);

	sci_out(port, SCSCR, SCSCR_INIT(port));

	if ((termios->c_cflag & CREAD) != 0)
		sci_start_rx(port, 0);
}

static const char *sci_type(struct uart_port *port)
{
	switch (port->type) {
	case PORT_IRDA:
		return "irda";
	case PORT_SCI:
		return "sci";
	case PORT_SCIF:
		return "scif";
	case PORT_SCIFA:
		return "scifa";
	}

	return NULL;
}

static void sci_release_port(struct uart_port *port)
{
	/* Nothing here yet .. */
}

static int sci_request_port(struct uart_port *port)
{
	/* Nothing here yet .. */
	return 0;
}

static void sci_config_port(struct uart_port *port, int flags)
{
	struct sci_port *s = to_sci_port(port);

	port->type = s->type;

	if (port->membase)
		return;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap_nocache(port->mapbase, 0x40);

		if (IS_ERR(port->membase))
			dev_err(port->dev, "can't remap port#%d\n", port->line);
	} else {
		/*
		 * For the simple (and majority of) cases where we don't
		 * need to do any remapping, just cast the cookie
		 * directly.
		 */
		port->membase = (void __iomem *)port->mapbase;
	}
}

static int sci_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	struct sci_port *s = to_sci_port(port);

	if (ser->irq != s->irqs[SCIx_TXI_IRQ] || ser->irq > nr_irqs)
		return -EINVAL;
	if (ser->baud_base < 2400)
		/* No paper tape reader for Mitch.. */
		return -EINVAL;

	return 0;
}

static struct uart_ops sci_uart_ops = {
	.tx_empty	= sci_tx_empty,
	.set_mctrl	= sci_set_mctrl,
	.get_mctrl	= sci_get_mctrl,
	.start_tx	= sci_start_tx,
	.stop_tx	= sci_stop_tx,
	.stop_rx	= sci_stop_rx,
	.enable_ms	= sci_enable_ms,
	.break_ctl	= sci_break_ctl,
	.startup	= sci_startup,
	.shutdown	= sci_shutdown,
	.set_termios	= sci_set_termios,
	.type		= sci_type,
	.release_port	= sci_release_port,
	.request_port	= sci_request_port,
	.config_port	= sci_config_port,
	.verify_port	= sci_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= sci_poll_get_char,
	.poll_put_char	= sci_poll_put_char,
#endif
};

static void __devinit sci_init_single(struct platform_device *dev,
				      struct sci_port *sci_port,
				      unsigned int index,
				      struct plat_sci_port *p)
{
	sci_port->port.ops	= &sci_uart_ops;
	sci_port->port.iotype	= UPIO_MEM;
	sci_port->port.line	= index;
	sci_port->port.fifosize	= 1;

#if defined(__H8300H__) || defined(__H8300S__)
#ifdef __H8300S__
	sci_port->enable	= h8300_sci_enable;
	sci_port->disable	= h8300_sci_disable;
#endif
	sci_port->port.uartclk	= CONFIG_CPU_CLOCK;
#elif defined(CONFIG_HAVE_CLK)
	sci_port->iclk		= p->clk ? clk_get(&dev->dev, p->clk) : NULL;
	sci_port->dclk		= clk_get(&dev->dev, "peripheral_clk");
	sci_port->enable	= sci_clk_enable;
	sci_port->disable	= sci_clk_disable;
#else
#error "Need a valid uartclk"
#endif

	sci_port->break_timer.data = (unsigned long)sci_port;
	sci_port->break_timer.function = sci_break_timer;
	init_timer(&sci_port->break_timer);

	sci_port->port.mapbase	= p->mapbase;
	sci_port->port.membase	= p->membase;

	sci_port->port.irq	= p->irqs[SCIx_TXI_IRQ];
	sci_port->port.flags	= p->flags;
	sci_port->port.dev	= &dev->dev;
	sci_port->type		= sci_port->port.type = p->type;

	memcpy(&sci_port->irqs, &p->irqs, sizeof(p->irqs));

}

#ifdef CONFIG_SERIAL_SH_SCI_CONSOLE
static struct tty_driver *serial_console_device(struct console *co, int *index)
{
	struct uart_driver *p = &sci_uart_driver;
	*index = co->index;
	return p->tty_driver;
}

static void serial_console_putchar(struct uart_port *port, int ch)
{
	sci_poll_put_char(port, ch);
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void serial_console_write(struct console *co, const char *s,
				 unsigned count)
{
	struct uart_port *port = co->data;
	struct sci_port *sci_port = to_sci_port(port);
	unsigned short bits;

	if (sci_port->enable)
		sci_port->enable(port);

	uart_console_write(port, s, count, serial_console_putchar);

	/* wait until fifo is empty and last bit has been transmitted */
	bits = SCxSR_TDxE(port) | SCxSR_TEND(port);
	while ((sci_in(port, SCxSR) & bits) != bits)
		cpu_relax();

	if (sci_port->disable)
		sci_port->disable(port);
}

static int __init serial_console_setup(struct console *co, char *options)
{
	struct sci_port *sci_port;
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= SCI_NPORTS)
		co->index = 0;

	sci_port = &sci_ports[co->index];
	port = &sci_port->port;
	co->data = port;

	/*
	 * Also need to check port->type, we don't actually have any
	 * UPIO_PORT ports, but uart_report_port() handily misreports
	 * it anyways if we don't have a port available by the time this is
	 * called.
	 */
	if (!port->type)
		return -ENODEV;

	sci_config_port(port, 0);

	if (sci_port->enable)
		sci_port->enable(port);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(port, co, baud, parity, bits, flow);
#if defined(__H8300H__) || defined(__H8300S__)
	/* disable rx interrupt */
	if (ret == 0)
		sci_stop_rx(port);
#endif
	/* TODO: disable clock */
	return ret;
}

static struct console serial_console = {
	.name		= "ttySC",
	.device		= serial_console_device,
	.write		= serial_console_write,
	.setup		= serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static int __init sci_console_init(void)
{
	register_console(&serial_console);
	return 0;
}
console_initcall(sci_console_init);
#endif /* CONFIG_SERIAL_SH_SCI_CONSOLE */

#if defined(CONFIG_SERIAL_SH_SCI_CONSOLE)
#define SCI_CONSOLE	(&serial_console)
#else
#define SCI_CONSOLE	0
#endif

static char banner[] __initdata =
	KERN_INFO "SuperH SCI(F) driver initialized\n";

static struct uart_driver sci_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "sci",
	.dev_name	= "ttySC",
	.major		= SCI_MAJOR,
	.minor		= SCI_MINOR_START,
	.nr		= SCI_NPORTS,
	.cons		= SCI_CONSOLE,
};


static int sci_remove(struct platform_device *dev)
{
	struct sh_sci_priv *priv = platform_get_drvdata(dev);
	struct sci_port *p;
	unsigned long flags;

#ifdef CONFIG_HAVE_CLK
	cpufreq_unregister_notifier(&priv->clk_nb, CPUFREQ_TRANSITION_NOTIFIER);
#endif

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(p, &priv->ports, node)
		uart_remove_one_port(&sci_uart_driver, &p->port);

	spin_unlock_irqrestore(&priv->lock, flags);

	kfree(priv);
	return 0;
}

static int __devinit sci_probe_single(struct platform_device *dev,
				      unsigned int index,
				      struct plat_sci_port *p,
				      struct sci_port *sciport)
{
	struct sh_sci_priv *priv = platform_get_drvdata(dev);
	unsigned long flags;
	int ret;

	/* Sanity check */
	if (unlikely(index >= SCI_NPORTS)) {
		dev_notice(&dev->dev, "Attempting to register port "
			   "%d when only %d are available.\n",
			   index+1, SCI_NPORTS);
		dev_notice(&dev->dev, "Consider bumping "
			   "CONFIG_SERIAL_SH_SCI_NR_UARTS!\n");
		return 0;
	}

	sci_init_single(dev, sciport, index, p);

	ret = uart_add_one_port(&sci_uart_driver, &sciport->port);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&sciport->node);

	spin_lock_irqsave(&priv->lock, flags);
	list_add(&sciport->node, &priv->ports);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/*
 * Register a set of serial devices attached to a platform device.  The
 * list is terminated with a zero flags entry, which means we expect
 * all entries to have at least UPF_BOOT_AUTOCONF set. Platforms that need
 * remapping (such as sh64) should also set UPF_IOREMAP.
 */
static int __devinit sci_probe(struct platform_device *dev)
{
	struct plat_sci_port *p = dev->dev.platform_data;
	struct sh_sci_priv *priv;
	int i, ret = -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->ports);
	spin_lock_init(&priv->lock);
	platform_set_drvdata(dev, priv);

#ifdef CONFIG_HAVE_CLK
	priv->clk_nb.notifier_call = sci_notifier;
	cpufreq_register_notifier(&priv->clk_nb, CPUFREQ_TRANSITION_NOTIFIER);
#endif

	if (dev->id != -1) {
		ret = sci_probe_single(dev, dev->id, p, &sci_ports[dev->id]);
		if (ret)
			goto err_unreg;
	} else {
		for (i = 0; p && p->flags != 0; p++, i++) {
			ret = sci_probe_single(dev, i, p, &sci_ports[i]);
			if (ret)
				goto err_unreg;
		}
	}

#ifdef CONFIG_SH_STANDARD_BIOS
	sh_bios_gdb_detach();
#endif

	return 0;

err_unreg:
	sci_remove(dev);
	return ret;
}

static int sci_suspend(struct device *dev)
{
	struct sh_sci_priv *priv = dev_get_drvdata(dev);
	struct sci_port *p;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(p, &priv->ports, node)
		uart_suspend_port(&sci_uart_driver, &p->port);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int sci_resume(struct device *dev)
{
	struct sh_sci_priv *priv = dev_get_drvdata(dev);
	struct sci_port *p;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(p, &priv->ports, node)
		uart_resume_port(&sci_uart_driver, &p->port);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static struct dev_pm_ops sci_dev_pm_ops = {
	.suspend	= sci_suspend,
	.resume		= sci_resume,
};

static struct platform_driver sci_driver = {
	.probe		= sci_probe,
	.remove		= __devexit_p(sci_remove),
	.driver		= {
		.name	= "sh-sci",
		.owner	= THIS_MODULE,
		.pm	= &sci_dev_pm_ops,
	},
};

static int __init sci_init(void)
{
	int ret;

	printk(banner);

	ret = uart_register_driver(&sci_uart_driver);
	if (likely(ret == 0)) {
		ret = platform_driver_register(&sci_driver);
		if (unlikely(ret))
			uart_unregister_driver(&sci_uart_driver);
	}

	return ret;
}

static void __exit sci_exit(void)
{
	platform_driver_unregister(&sci_driver);
	uart_unregister_driver(&sci_uart_driver);
}

module_init(sci_init);
module_exit(sci_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sh-sci");
