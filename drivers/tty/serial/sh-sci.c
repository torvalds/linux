/*
 * SuperH on-chip serial module support.  (SCI with no FIFO / with FIFO)
 *
 *  Copyright (C) 2002 - 2011  Paul Mundt
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

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>
#include <linux/serial.h>
#include <linux/serial_sci.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysrq.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#ifdef CONFIG_SUPERH
#include <asm/sh_bios.h>
#endif

#include "sh-sci.h"

/* Offsets into the sci_port->irqs array */
enum {
	SCIx_ERI_IRQ,
	SCIx_RXI_IRQ,
	SCIx_TXI_IRQ,
	SCIx_BRI_IRQ,
	SCIx_NR_IRQS,

	SCIx_MUX_IRQ = SCIx_NR_IRQS,	/* special case */
};

#define SCIx_IRQ_IS_MUXED(port)			\
	((port)->irqs[SCIx_ERI_IRQ] ==	\
	 (port)->irqs[SCIx_RXI_IRQ]) ||	\
	((port)->irqs[SCIx_ERI_IRQ] &&	\
	 ((port)->irqs[SCIx_RXI_IRQ] < 0))

struct sci_port {
	struct uart_port	port;

	/* Platform configuration */
	struct plat_sci_port	*cfg;
	unsigned int		overrun_reg;
	unsigned int		overrun_mask;
	unsigned int		error_mask;
	unsigned int		error_clear;
	unsigned int		sampling_rate;
	resource_size_t		reg_size;

	/* Break timer */
	struct timer_list	break_timer;
	int			break_flag;

	/* Interface clock */
	struct clk		*iclk;
	/* Function clock */
	struct clk		*fclk;

	int			irqs[SCIx_NR_IRQS];
	char			*irqstr[SCIx_NR_IRQS];

	struct dma_chan			*chan_tx;
	struct dma_chan			*chan_rx;

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	dma_cookie_t			cookie_tx;
	dma_cookie_t			cookie_rx[2];
	dma_cookie_t			active_rx;
	dma_addr_t			tx_dma_addr;
	unsigned int			tx_dma_len;
	struct scatterlist		sg_rx[2];
	void				*rx_buf[2];
	size_t				buf_len_rx;
	struct work_struct		work_tx;
	struct timer_list		rx_timer;
	unsigned int			rx_timeout;
#endif
};

#define SCI_NPORTS CONFIG_SERIAL_SH_SCI_NR_UARTS

static struct sci_port sci_ports[SCI_NPORTS];
static struct uart_driver sci_uart_driver;

static inline struct sci_port *
to_sci_port(struct uart_port *uart)
{
	return container_of(uart, struct sci_port, port);
}

struct plat_sci_reg {
	u8 offset, size;
};

/* Helper for invalidating specific entries of an inherited map. */
#define sci_reg_invalid	{ .offset = 0, .size = 0 }

static const struct plat_sci_reg sci_regmap[SCIx_NR_REGTYPES][SCIx_NR_REGS] = {
	[SCIx_PROBE_REGTYPE] = {
		[0 ... SCIx_NR_REGS - 1] = sci_reg_invalid,
	},

	/*
	 * Common SCI definitions, dependent on the port's regshift
	 * value.
	 */
	[SCIx_SCI_REGTYPE] = {
		[SCSMR]		= { 0x00,  8 },
		[SCBRR]		= { 0x01,  8 },
		[SCSCR]		= { 0x02,  8 },
		[SCxTDR]	= { 0x03,  8 },
		[SCxSR]		= { 0x04,  8 },
		[SCxRDR]	= { 0x05,  8 },
		[SCFCR]		= sci_reg_invalid,
		[SCFDR]		= sci_reg_invalid,
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common definitions for legacy IrDA ports.
	 */
	[SCIx_IRDA_REGTYPE] = {
		[SCSMR]		= { 0x00,  8 },
		[SCBRR]		= { 0x02,  8 },
		[SCSCR]		= { 0x04,  8 },
		[SCxTDR]	= { 0x06,  8 },
		[SCxSR]		= { 0x08, 16 },
		[SCxRDR]	= { 0x0a,  8 },
		[SCFCR]		= { 0x0c,  8 },
		[SCFDR]		= { 0x0e, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SCIFA definitions.
	 */
	[SCIx_SCIFA_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x20,  8 },
		[SCxSR]		= { 0x14, 16 },
		[SCxRDR]	= { 0x24,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= { 0x30, 16 },
		[SCPDR]		= { 0x34, 16 },
	},

	/*
	 * Common SCIFB definitions.
	 */
	[SCIx_SCIFB_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x40,  8 },
		[SCxSR]		= { 0x14, 16 },
		[SCxRDR]	= { 0x60,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= sci_reg_invalid,
		[SCTFDR]	= { 0x38, 16 },
		[SCRFDR]	= { 0x3c, 16 },
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= { 0x30, 16 },
		[SCPDR]		= { 0x34, 16 },
	},

	/*
	 * Common SH-2(A) SCIF definitions for ports with FIFO data
	 * count registers.
	 */
	[SCIx_SH2_SCIF_FIFODATA_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= { 0x20, 16 },
		[SCLSR]		= { 0x24, 16 },
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-3 SCIF definitions.
	 */
	[SCIx_SH3_SCIF_REGTYPE] = {
		[SCSMR]		= { 0x00,  8 },
		[SCBRR]		= { 0x02,  8 },
		[SCSCR]		= { 0x04,  8 },
		[SCxTDR]	= { 0x06,  8 },
		[SCxSR]		= { 0x08, 16 },
		[SCxRDR]	= { 0x0a,  8 },
		[SCFCR]		= { 0x0c,  8 },
		[SCFDR]		= { 0x0e, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-4(A) SCIF(B) definitions.
	 */
	[SCIx_SH4_SCIF_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= { 0x20, 16 },
		[SCLSR]		= { 0x24, 16 },
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common HSCIF definitions.
	 */
	[SCIx_HSCIF_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= { 0x20, 16 },
		[SCLSR]		= { 0x24, 16 },
		[HSSRR]		= { 0x40, 16 },
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-4(A) SCIF(B) definitions for ports without an SCSPTR
	 * register.
	 */
	[SCIx_SH4_SCIF_NO_SCSPTR_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= { 0x24, 16 },
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * Common SH-4(A) SCIF(B) definitions for ports with FIFO data
	 * count registers.
	 */
	[SCIx_SH4_SCIF_FIFODATA_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x0c,  8 },
		[SCxSR]		= { 0x10, 16 },
		[SCxRDR]	= { 0x14,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= { 0x1c, 16 },	/* aliased to SCFDR */
		[SCRFDR]	= { 0x20, 16 },
		[SCSPTR]	= { 0x24, 16 },
		[SCLSR]		= { 0x28, 16 },
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},

	/*
	 * SH7705-style SCIF(B) ports, lacking both SCSPTR and SCLSR
	 * registers.
	 */
	[SCIx_SH7705_SCIF_REGTYPE] = {
		[SCSMR]		= { 0x00, 16 },
		[SCBRR]		= { 0x04,  8 },
		[SCSCR]		= { 0x08, 16 },
		[SCxTDR]	= { 0x20,  8 },
		[SCxSR]		= { 0x14, 16 },
		[SCxRDR]	= { 0x24,  8 },
		[SCFCR]		= { 0x18, 16 },
		[SCFDR]		= { 0x1c, 16 },
		[SCTFDR]	= sci_reg_invalid,
		[SCRFDR]	= sci_reg_invalid,
		[SCSPTR]	= sci_reg_invalid,
		[SCLSR]		= sci_reg_invalid,
		[HSSRR]		= sci_reg_invalid,
		[SCPCR]		= sci_reg_invalid,
		[SCPDR]		= sci_reg_invalid,
	},
};

#define sci_getreg(up, offset)		(sci_regmap[to_sci_port(up)->cfg->regtype] + offset)

/*
 * The "offset" here is rather misleading, in that it refers to an enum
 * value relative to the port mapping rather than the fixed offset
 * itself, which needs to be manually retrieved from the platform's
 * register map for the given port.
 */
static unsigned int sci_serial_in(struct uart_port *p, int offset)
{
	const struct plat_sci_reg *reg = sci_getreg(p, offset);

	if (reg->size == 8)
		return ioread8(p->membase + (reg->offset << p->regshift));
	else if (reg->size == 16)
		return ioread16(p->membase + (reg->offset << p->regshift));
	else
		WARN(1, "Invalid register access\n");

	return 0;
}

static void sci_serial_out(struct uart_port *p, int offset, int value)
{
	const struct plat_sci_reg *reg = sci_getreg(p, offset);

	if (reg->size == 8)
		iowrite8(value, p->membase + (reg->offset << p->regshift));
	else if (reg->size == 16)
		iowrite16(value, p->membase + (reg->offset << p->regshift));
	else
		WARN(1, "Invalid register access\n");
}

static int sci_probe_regmap(struct plat_sci_port *cfg)
{
	switch (cfg->type) {
	case PORT_SCI:
		cfg->regtype = SCIx_SCI_REGTYPE;
		break;
	case PORT_IRDA:
		cfg->regtype = SCIx_IRDA_REGTYPE;
		break;
	case PORT_SCIFA:
		cfg->regtype = SCIx_SCIFA_REGTYPE;
		break;
	case PORT_SCIFB:
		cfg->regtype = SCIx_SCIFB_REGTYPE;
		break;
	case PORT_SCIF:
		/*
		 * The SH-4 is a bit of a misnomer here, although that's
		 * where this particular port layout originated. This
		 * configuration (or some slight variation thereof)
		 * remains the dominant model for all SCIFs.
		 */
		cfg->regtype = SCIx_SH4_SCIF_REGTYPE;
		break;
	case PORT_HSCIF:
		cfg->regtype = SCIx_HSCIF_REGTYPE;
		break;
	default:
		pr_err("Can't probe register map for given port\n");
		return -EINVAL;
	}

	return 0;
}

static void sci_port_enable(struct sci_port *sci_port)
{
	if (!sci_port->port.dev)
		return;

	pm_runtime_get_sync(sci_port->port.dev);

	clk_prepare_enable(sci_port->iclk);
	sci_port->port.uartclk = clk_get_rate(sci_port->iclk);
	clk_prepare_enable(sci_port->fclk);
}

static void sci_port_disable(struct sci_port *sci_port)
{
	if (!sci_port->port.dev)
		return;

	/* Cancel the break timer to ensure that the timer handler will not try
	 * to access the hardware with clocks and power disabled. Reset the
	 * break flag to make the break debouncing state machine ready for the
	 * next break.
	 */
	del_timer_sync(&sci_port->break_timer);
	sci_port->break_flag = 0;

	clk_disable_unprepare(sci_port->fclk);
	clk_disable_unprepare(sci_port->iclk);

	pm_runtime_put_sync(sci_port->port.dev);
}

static inline unsigned long port_rx_irq_mask(struct uart_port *port)
{
	/*
	 * Not all ports (such as SCIFA) will support REIE. Rather than
	 * special-casing the port type, we check the port initialization
	 * IRQ enable mask to see whether the IRQ is desired at all. If
	 * it's unset, it's logically inferred that there's no point in
	 * testing for it.
	 */
	return SCSCR_RIE | (to_sci_port(port)->cfg->scscr & SCSCR_REIE);
}

static void sci_start_tx(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	unsigned short ctrl;

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
		u16 new, scr = serial_port_in(port, SCSCR);
		if (s->chan_tx)
			new = scr | SCSCR_TDRQE;
		else
			new = scr & ~SCSCR_TDRQE;
		if (new != scr)
			serial_port_out(port, SCSCR, new);
	}

	if (s->chan_tx && !uart_circ_empty(&s->port.state->xmit) &&
	    dma_submit_error(s->cookie_tx)) {
		s->cookie_tx = 0;
		schedule_work(&s->work_tx);
	}
#endif

	if (!s->chan_tx || port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
		/* Set TIE (Transmit Interrupt Enable) bit in SCSCR */
		ctrl = serial_port_in(port, SCSCR);
		serial_port_out(port, SCSCR, ctrl | SCSCR_TIE);
	}
}

static void sci_stop_tx(struct uart_port *port)
{
	unsigned short ctrl;

	/* Clear TIE (Transmit Interrupt Enable) bit in SCSCR */
	ctrl = serial_port_in(port, SCSCR);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		ctrl &= ~SCSCR_TDRQE;

	ctrl &= ~SCSCR_TIE;

	serial_port_out(port, SCSCR, ctrl);
}

static void sci_start_rx(struct uart_port *port)
{
	unsigned short ctrl;

	ctrl = serial_port_in(port, SCSCR) | port_rx_irq_mask(port);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		ctrl &= ~SCSCR_RDRQE;

	serial_port_out(port, SCSCR, ctrl);
}

static void sci_stop_rx(struct uart_port *port)
{
	unsigned short ctrl;

	ctrl = serial_port_in(port, SCSCR);

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		ctrl &= ~SCSCR_RDRQE;

	ctrl &= ~port_rx_irq_mask(port);

	serial_port_out(port, SCSCR, ctrl);
}

static void sci_clear_SCxSR(struct uart_port *port, unsigned int mask)
{
	if (port->type == PORT_SCI) {
		/* Just store the mask */
		serial_port_out(port, SCxSR, mask);
	} else if (to_sci_port(port)->overrun_mask == SCIFA_ORER) {
		/* SCIFA/SCIFB and SCIF on SH7705/SH7720/SH7721 */
		/* Only clear the status bits we want to clear */
		serial_port_out(port, SCxSR,
				serial_port_in(port, SCxSR) & mask);
	} else {
		/* Store the mask, clear parity/framing errors */
		serial_port_out(port, SCxSR, mask & ~(SCIF_FERC | SCIF_PERC));
	}
}

#if defined(CONFIG_CONSOLE_POLL) || defined(CONFIG_SERIAL_SH_SCI_CONSOLE)

#ifdef CONFIG_CONSOLE_POLL
static int sci_poll_get_char(struct uart_port *port)
{
	unsigned short status;
	int c;

	do {
		status = serial_port_in(port, SCxSR);
		if (status & SCxSR_ERRORS(port)) {
			sci_clear_SCxSR(port, SCxSR_ERROR_CLEAR(port));
			continue;
		}
		break;
	} while (1);

	if (!(status & SCxSR_RDxF(port)))
		return NO_POLL_CHAR;

	c = serial_port_in(port, SCxRDR);

	/* Dummy read */
	serial_port_in(port, SCxSR);
	sci_clear_SCxSR(port, SCxSR_RDxF_CLEAR(port));

	return c;
}
#endif

static void sci_poll_put_char(struct uart_port *port, unsigned char c)
{
	unsigned short status;

	do {
		status = serial_port_in(port, SCxSR);
	} while (!(status & SCxSR_TDxE(port)));

	serial_port_out(port, SCxTDR, c);
	sci_clear_SCxSR(port, SCxSR_TDxE_CLEAR(port) & ~SCxSR_TEND(port));
}
#endif /* CONFIG_CONSOLE_POLL || CONFIG_SERIAL_SH_SCI_CONSOLE */

static void sci_init_pins(struct uart_port *port, unsigned int cflag)
{
	struct sci_port *s = to_sci_port(port);
	const struct plat_sci_reg *reg = sci_regmap[s->cfg->regtype] + SCSPTR;

	/*
	 * Use port-specific handler if provided.
	 */
	if (s->cfg->ops && s->cfg->ops->init_pins) {
		s->cfg->ops->init_pins(port, cflag);
		return;
	}

	/*
	 * For the generic path SCSPTR is necessary. Bail out if that's
	 * unavailable, too.
	 */
	if (!reg->size)
		return;

	if ((s->cfg->capabilities & SCIx_HAVE_RTSCTS) &&
	    ((!(cflag & CRTSCTS)))) {
		unsigned short status;

		status = serial_port_in(port, SCSPTR);
		status &= ~SCSPTR_CTSIO;
		status |= SCSPTR_RTSIO;
		serial_port_out(port, SCSPTR, status); /* Set RTS = 1 */
	}
}

static int sci_txfill(struct uart_port *port)
{
	const struct plat_sci_reg *reg;

	reg = sci_getreg(port, SCTFDR);
	if (reg->size)
		return serial_port_in(port, SCTFDR) & ((port->fifosize << 1) - 1);

	reg = sci_getreg(port, SCFDR);
	if (reg->size)
		return serial_port_in(port, SCFDR) >> 8;

	return !(serial_port_in(port, SCxSR) & SCI_TDRE);
}

static int sci_txroom(struct uart_port *port)
{
	return port->fifosize - sci_txfill(port);
}

static int sci_rxfill(struct uart_port *port)
{
	const struct plat_sci_reg *reg;

	reg = sci_getreg(port, SCRFDR);
	if (reg->size)
		return serial_port_in(port, SCRFDR) & ((port->fifosize << 1) - 1);

	reg = sci_getreg(port, SCFDR);
	if (reg->size)
		return serial_port_in(port, SCFDR) & ((port->fifosize << 1) - 1);

	return (serial_port_in(port, SCxSR) & SCxSR_RDxF(port)) != 0;
}

/*
 * SCI helper for checking the state of the muxed port/RXD pins.
 */
static inline int sci_rxd_in(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);

	if (s->cfg->port_reg <= 0)
		return 1;

	/* Cast for ARM damage */
	return !!__raw_readb((void __iomem *)(uintptr_t)s->cfg->port_reg);
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

	status = serial_port_in(port, SCxSR);
	if (!(status & SCxSR_TDxE(port))) {
		ctrl = serial_port_in(port, SCSCR);
		if (uart_circ_empty(xmit))
			ctrl &= ~SCSCR_TIE;
		else
			ctrl |= SCSCR_TIE;
		serial_port_out(port, SCSCR, ctrl);
		return;
	}

	count = sci_txroom(port);

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

		serial_port_out(port, SCxTDR, c);

		port->icount.tx++;
	} while (--count > 0);

	sci_clear_SCxSR(port, SCxSR_TDxE_CLEAR(port));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (uart_circ_empty(xmit)) {
		sci_stop_tx(port);
	} else {
		ctrl = serial_port_in(port, SCSCR);

		if (port->type != PORT_SCI) {
			serial_port_in(port, SCxSR); /* Dummy read */
			sci_clear_SCxSR(port, SCxSR_TDxE_CLEAR(port));
		}

		ctrl |= SCSCR_TIE;
		serial_port_out(port, SCSCR, ctrl);
	}
}

/* On SH3, SCIF may read end-of-break as a space->mark char */
#define STEPFN(c)  ({int __c = (c); (((__c-1)|(__c)) == -1); })

static void sci_receive_chars(struct uart_port *port)
{
	struct sci_port *sci_port = to_sci_port(port);
	struct tty_port *tport = &port->state->port;
	int i, count, copied = 0;
	unsigned short status;
	unsigned char flag;

	status = serial_port_in(port, SCxSR);
	if (!(status & SCxSR_RDxF(port)))
		return;

	while (1) {
		/* Don't copy more bytes than there is room for in the buffer */
		count = tty_buffer_request_room(tport, sci_rxfill(port));

		/* If for any reason we can't copy more data, we're done! */
		if (count == 0)
			break;

		if (port->type == PORT_SCI) {
			char c = serial_port_in(port, SCxRDR);
			if (uart_handle_sysrq_char(port, c) ||
			    sci_port->break_flag)
				count = 0;
			else
				tty_insert_flip_char(tport, c, TTY_NORMAL);
		} else {
			for (i = 0; i < count; i++) {
				char c = serial_port_in(port, SCxRDR);

				status = serial_port_in(port, SCxSR);
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
				if (status & SCxSR_FER(port)) {
					flag = TTY_FRAME;
					port->icount.frame++;
					dev_notice(port->dev, "frame error\n");
				} else if (status & SCxSR_PER(port)) {
					flag = TTY_PARITY;
					port->icount.parity++;
					dev_notice(port->dev, "parity error\n");
				} else
					flag = TTY_NORMAL;

				tty_insert_flip_char(tport, c, flag);
			}
		}

		serial_port_in(port, SCxSR); /* dummy read */
		sci_clear_SCxSR(port, SCxSR_RDxF_CLEAR(port));

		copied += count;
		port->icount.rx += count;
	}

	if (copied) {
		/* Tell the rest of the system the news. New characters! */
		tty_flip_buffer_push(tport);
	} else {
		/* TTY buffers full; read from RX reg to prevent lockup */
		serial_port_in(port, SCxRDR);
		serial_port_in(port, SCxSR); /* dummy read */
		sci_clear_SCxSR(port, SCxSR_RDxF_CLEAR(port));
	}
}

#define SCI_BREAK_JIFFIES (HZ/20)

/*
 * The sci generates interrupts during the break,
 * 1 per millisecond or so during the break period, for 9600 baud.
 * So dont bother disabling interrupts.
 * But dont want more than 1 break event.
 * Use a kernel timer to periodically poll the rx line until
 * the break is finished.
 */
static inline void sci_schedule_break_timer(struct sci_port *port)
{
	mod_timer(&port->break_timer, jiffies + SCI_BREAK_JIFFIES);
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

static int sci_handle_errors(struct uart_port *port)
{
	int copied = 0;
	unsigned short status = serial_port_in(port, SCxSR);
	struct tty_port *tport = &port->state->port;
	struct sci_port *s = to_sci_port(port);

	/* Handle overruns */
	if (status & s->overrun_mask) {
		port->icount.overrun++;

		/* overrun error */
		if (tty_insert_flip_char(tport, 0, TTY_OVERRUN))
			copied++;

		dev_notice(port->dev, "overrun error\n");
	}

	if (status & SCxSR_FER(port)) {
		if (sci_rxd_in(port) == 0) {
			/* Notify of BREAK */
			struct sci_port *sci_port = to_sci_port(port);

			if (!sci_port->break_flag) {
				port->icount.brk++;

				sci_port->break_flag = 1;
				sci_schedule_break_timer(sci_port);

				/* Do sysrq handling. */
				if (uart_handle_break(port))
					return 0;

				dev_dbg(port->dev, "BREAK detected\n");

				if (tty_insert_flip_char(tport, 0, TTY_BREAK))
					copied++;
			}

		} else {
			/* frame error */
			port->icount.frame++;

			if (tty_insert_flip_char(tport, 0, TTY_FRAME))
				copied++;

			dev_notice(port->dev, "frame error\n");
		}
	}

	if (status & SCxSR_PER(port)) {
		/* parity error */
		port->icount.parity++;

		if (tty_insert_flip_char(tport, 0, TTY_PARITY))
			copied++;

		dev_notice(port->dev, "parity error\n");
	}

	if (copied)
		tty_flip_buffer_push(tport);

	return copied;
}

static int sci_handle_fifo_overrun(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	struct sci_port *s = to_sci_port(port);
	const struct plat_sci_reg *reg;
	int copied = 0;
	u16 status;

	reg = sci_getreg(port, s->overrun_reg);
	if (!reg->size)
		return 0;

	status = serial_port_in(port, s->overrun_reg);
	if (status & s->overrun_mask) {
		status &= ~s->overrun_mask;
		serial_port_out(port, s->overrun_reg, status);

		port->icount.overrun++;

		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
		tty_flip_buffer_push(tport);

		dev_dbg(port->dev, "overrun error\n");
		copied++;
	}

	return copied;
}

static int sci_handle_breaks(struct uart_port *port)
{
	int copied = 0;
	unsigned short status = serial_port_in(port, SCxSR);
	struct tty_port *tport = &port->state->port;
	struct sci_port *s = to_sci_port(port);

	if (uart_handle_break(port))
		return 0;

	if (!s->break_flag && status & SCxSR_BRK(port)) {
#if defined(CONFIG_CPU_SH3)
		/* Debounce break */
		s->break_flag = 1;
#endif

		port->icount.brk++;

		/* Notify of BREAK */
		if (tty_insert_flip_char(tport, 0, TTY_BREAK))
			copied++;

		dev_dbg(port->dev, "BREAK detected\n");
	}

	if (copied)
		tty_flip_buffer_push(tport);

	copied += sci_handle_fifo_overrun(port);

	return copied;
}

#ifdef CONFIG_SERIAL_SH_SCI_DMA
static void sci_dma_tx_complete(void *arg)
{
	struct sci_port *s = arg;
	struct uart_port *port = &s->port;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long flags;

	dev_dbg(port->dev, "%s(%d)\n", __func__, port->line);

	spin_lock_irqsave(&port->lock, flags);

	xmit->tail += s->tx_dma_len;
	xmit->tail &= UART_XMIT_SIZE - 1;

	port->icount.tx += s->tx_dma_len;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (!uart_circ_empty(xmit)) {
		s->cookie_tx = 0;
		schedule_work(&s->work_tx);
	} else {
		s->cookie_tx = -EINVAL;
		if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
			u16 ctrl = serial_port_in(port, SCSCR);
			serial_port_out(port, SCSCR, ctrl & ~SCSCR_TIE);
		}
	}

	spin_unlock_irqrestore(&port->lock, flags);
}

/* Locking: called with port lock held */
static int sci_dma_rx_push(struct sci_port *s, void *buf, size_t count)
{
	struct uart_port *port = &s->port;
	struct tty_port *tport = &port->state->port;
	int copied;

	copied = tty_insert_flip_string(tport, buf, count);
	if (copied < count) {
		dev_warn(port->dev, "Rx overrun: dropping %zu bytes\n",
			 count - copied);
		port->icount.buf_overrun++;
	}

	port->icount.rx += copied;

	return copied;
}

static int sci_dma_rx_find_active(struct sci_port *s)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(s->cookie_rx); i++)
		if (s->active_rx == s->cookie_rx[i])
			return i;

	dev_err(s->port.dev, "%s: Rx cookie %d not found!\n", __func__,
		s->active_rx);
	return -1;
}

static void sci_rx_dma_release(struct sci_port *s, bool enable_pio)
{
	struct dma_chan *chan = s->chan_rx;
	struct uart_port *port = &s->port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	s->chan_rx = NULL;
	s->cookie_rx[0] = s->cookie_rx[1] = -EINVAL;
	spin_unlock_irqrestore(&port->lock, flags);
	dmaengine_terminate_all(chan);
	dma_free_coherent(chan->device->dev, s->buf_len_rx * 2, s->rx_buf[0],
			  sg_dma_address(&s->sg_rx[0]));
	dma_release_channel(chan);
	if (enable_pio)
		sci_start_rx(port);
}

static void sci_dma_rx_complete(void *arg)
{
	struct sci_port *s = arg;
	struct dma_chan *chan = s->chan_rx;
	struct uart_port *port = &s->port;
	struct dma_async_tx_descriptor *desc;
	unsigned long flags;
	int active, count = 0;

	dev_dbg(port->dev, "%s(%d) active cookie %d\n", __func__, port->line,
		s->active_rx);

	spin_lock_irqsave(&port->lock, flags);

	active = sci_dma_rx_find_active(s);
	if (active >= 0)
		count = sci_dma_rx_push(s, s->rx_buf[active], s->buf_len_rx);

	mod_timer(&s->rx_timer, jiffies + s->rx_timeout);

	if (count)
		tty_flip_buffer_push(&port->state->port);

	desc = dmaengine_prep_slave_sg(s->chan_rx, &s->sg_rx[active], 1,
				       DMA_DEV_TO_MEM,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		goto fail;

	desc->callback = sci_dma_rx_complete;
	desc->callback_param = s;
	s->cookie_rx[active] = dmaengine_submit(desc);
	if (dma_submit_error(s->cookie_rx[active]))
		goto fail;

	s->active_rx = s->cookie_rx[!active];

	dma_async_issue_pending(chan);

	dev_dbg(port->dev, "%s: cookie %d #%d, new active cookie %d\n",
		__func__, s->cookie_rx[active], active, s->active_rx);
	spin_unlock_irqrestore(&port->lock, flags);
	return;

fail:
	spin_unlock_irqrestore(&port->lock, flags);
	dev_warn(port->dev, "Failed submitting Rx DMA descriptor\n");
	sci_rx_dma_release(s, true);
}

static void sci_tx_dma_release(struct sci_port *s, bool enable_pio)
{
	struct dma_chan *chan = s->chan_tx;
	struct uart_port *port = &s->port;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	s->chan_tx = NULL;
	s->cookie_tx = -EINVAL;
	spin_unlock_irqrestore(&port->lock, flags);
	dmaengine_terminate_all(chan);
	dma_unmap_single(chan->device->dev, s->tx_dma_addr, UART_XMIT_SIZE,
			 DMA_TO_DEVICE);
	dma_release_channel(chan);
	if (enable_pio)
		sci_start_tx(port);
}

static void sci_submit_rx(struct sci_port *s)
{
	struct dma_chan *chan = s->chan_rx;
	int i;

	for (i = 0; i < 2; i++) {
		struct scatterlist *sg = &s->sg_rx[i];
		struct dma_async_tx_descriptor *desc;

		desc = dmaengine_prep_slave_sg(chan,
			sg, 1, DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc)
			goto fail;

		desc->callback = sci_dma_rx_complete;
		desc->callback_param = s;
		s->cookie_rx[i] = dmaengine_submit(desc);
		if (dma_submit_error(s->cookie_rx[i]))
			goto fail;

		dev_dbg(s->port.dev, "%s(): cookie %d to #%d\n", __func__,
			s->cookie_rx[i], i);
	}

	s->active_rx = s->cookie_rx[0];

	dma_async_issue_pending(chan);
	return;

fail:
	if (i)
		dmaengine_terminate_all(chan);
	for (i = 0; i < 2; i++)
		s->cookie_rx[i] = -EINVAL;
	s->active_rx = -EINVAL;
	dev_warn(s->port.dev, "Failed to re-start Rx DMA, using PIO\n");
	sci_rx_dma_release(s, true);
}

static void work_fn_tx(struct work_struct *work)
{
	struct sci_port *s = container_of(work, struct sci_port, work_tx);
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *chan = s->chan_tx;
	struct uart_port *port = &s->port;
	struct circ_buf *xmit = &port->state->xmit;
	dma_addr_t buf;

	/*
	 * DMA is idle now.
	 * Port xmit buffer is already mapped, and it is one page... Just adjust
	 * offsets and lengths. Since it is a circular buffer, we have to
	 * transmit till the end, and then the rest. Take the port lock to get a
	 * consistent xmit buffer state.
	 */
	spin_lock_irq(&port->lock);
	buf = s->tx_dma_addr + (xmit->tail & (UART_XMIT_SIZE - 1));
	s->tx_dma_len = min_t(unsigned int,
		CIRC_CNT(xmit->head, xmit->tail, UART_XMIT_SIZE),
		CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE));
	spin_unlock_irq(&port->lock);

	desc = dmaengine_prep_slave_single(chan, buf, s->tx_dma_len,
					   DMA_MEM_TO_DEV,
					   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		dev_warn(port->dev, "Failed preparing Tx DMA descriptor\n");
		/* switch to PIO */
		sci_tx_dma_release(s, true);
		return;
	}

	dma_sync_single_for_device(chan->device->dev, buf, s->tx_dma_len,
				   DMA_TO_DEVICE);

	spin_lock_irq(&port->lock);
	desc->callback = sci_dma_tx_complete;
	desc->callback_param = s;
	spin_unlock_irq(&port->lock);
	s->cookie_tx = dmaengine_submit(desc);
	if (dma_submit_error(s->cookie_tx)) {
		dev_warn(port->dev, "Failed submitting Tx DMA descriptor\n");
		/* switch to PIO */
		sci_tx_dma_release(s, true);
		return;
	}

	dev_dbg(port->dev, "%s: %p: %d...%d, cookie %d\n",
		__func__, xmit->buf, xmit->tail, xmit->head, s->cookie_tx);

	dma_async_issue_pending(chan);
}

static void rx_timer_fn(unsigned long arg)
{
	struct sci_port *s = (struct sci_port *)arg;
	struct dma_chan *chan = s->chan_rx;
	struct uart_port *port = &s->port;
	struct dma_tx_state state;
	enum dma_status status;
	unsigned long flags;
	unsigned int read;
	int active, count;
	u16 scr;

	spin_lock_irqsave(&port->lock, flags);

	dev_dbg(port->dev, "DMA Rx timed out\n");

	active = sci_dma_rx_find_active(s);
	if (active < 0) {
		spin_unlock_irqrestore(&port->lock, flags);
		return;
	}

	status = dmaengine_tx_status(s->chan_rx, s->active_rx, &state);
	if (status == DMA_COMPLETE) {
		dev_dbg(port->dev, "Cookie %d #%d has already completed\n",
			s->active_rx, active);
		spin_unlock_irqrestore(&port->lock, flags);

		/* Let packet complete handler take care of the packet */
		return;
	}

	dmaengine_pause(chan);

	/*
	 * sometimes DMA transfer doesn't stop even if it is stopped and
	 * data keeps on coming until transaction is complete so check
	 * for DMA_COMPLETE again
	 * Let packet complete handler take care of the packet
	 */
	status = dmaengine_tx_status(s->chan_rx, s->active_rx, &state);
	if (status == DMA_COMPLETE) {
		spin_unlock_irqrestore(&port->lock, flags);
		dev_dbg(port->dev, "Transaction complete after DMA engine was stopped");
		return;
	}

	/* Handle incomplete DMA receive */
	dmaengine_terminate_all(s->chan_rx);
	read = sg_dma_len(&s->sg_rx[active]) - state.residue;
	dev_dbg(port->dev, "Read %u bytes with cookie %d\n", read,
		s->active_rx);

	if (read) {
		count = sci_dma_rx_push(s, s->rx_buf[active], read);
		if (count)
			tty_flip_buffer_push(&port->state->port);
	}

	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
		sci_submit_rx(s);

	/* Direct new serial port interrupts back to CPU */
	scr = serial_port_in(port, SCSCR);
	if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
		scr &= ~SCSCR_RDRQE;
		enable_irq(s->irqs[SCIx_RXI_IRQ]);
	}
	serial_port_out(port, SCSCR, scr | SCSCR_RIE);

	spin_unlock_irqrestore(&port->lock, flags);
}

static struct dma_chan *sci_request_dma_chan(struct uart_port *port,
					     enum dma_transfer_direction dir,
					     unsigned int id)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;
	struct dma_slave_config cfg;
	int ret;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	chan = dma_request_slave_channel_compat(mask, shdma_chan_filter,
					(void *)(unsigned long)id, port->dev,
					dir == DMA_MEM_TO_DEV ? "tx" : "rx");
	if (!chan) {
		dev_warn(port->dev,
			 "dma_request_slave_channel_compat failed\n");
		return NULL;
	}

	memset(&cfg, 0, sizeof(cfg));
	cfg.direction = dir;
	if (dir == DMA_MEM_TO_DEV) {
		cfg.dst_addr = port->mapbase +
			(sci_getreg(port, SCxTDR)->offset << port->regshift);
		cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	} else {
		cfg.src_addr = port->mapbase +
			(sci_getreg(port, SCxRDR)->offset << port->regshift);
		cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	}

	ret = dmaengine_slave_config(chan, &cfg);
	if (ret) {
		dev_warn(port->dev, "dmaengine_slave_config failed %d\n", ret);
		dma_release_channel(chan);
		return NULL;
	}

	return chan;
}

static void sci_request_dma(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	struct dma_chan *chan;

	dev_dbg(port->dev, "%s: port %d\n", __func__, port->line);

	if (!port->dev->of_node &&
	    (s->cfg->dma_slave_tx <= 0 || s->cfg->dma_slave_rx <= 0))
		return;

	s->cookie_tx = -EINVAL;
	chan = sci_request_dma_chan(port, DMA_MEM_TO_DEV, s->cfg->dma_slave_tx);
	dev_dbg(port->dev, "%s: TX: got channel %p\n", __func__, chan);
	if (chan) {
		s->chan_tx = chan;
		/* UART circular tx buffer is an aligned page. */
		s->tx_dma_addr = dma_map_single(chan->device->dev,
						port->state->xmit.buf,
						UART_XMIT_SIZE,
						DMA_TO_DEVICE);
		if (dma_mapping_error(chan->device->dev, s->tx_dma_addr)) {
			dev_warn(port->dev, "Failed mapping Tx DMA descriptor\n");
			dma_release_channel(chan);
			s->chan_tx = NULL;
		} else {
			dev_dbg(port->dev, "%s: mapped %lu@%p to %pad\n",
				__func__, UART_XMIT_SIZE,
				port->state->xmit.buf, &s->tx_dma_addr);
		}

		INIT_WORK(&s->work_tx, work_fn_tx);
	}

	chan = sci_request_dma_chan(port, DMA_DEV_TO_MEM, s->cfg->dma_slave_rx);
	dev_dbg(port->dev, "%s: RX: got channel %p\n", __func__, chan);
	if (chan) {
		unsigned int i;
		dma_addr_t dma;
		void *buf;

		s->chan_rx = chan;

		s->buf_len_rx = 2 * max_t(size_t, 16, port->fifosize);
		buf = dma_alloc_coherent(chan->device->dev, s->buf_len_rx * 2,
					 &dma, GFP_KERNEL);
		if (!buf) {
			dev_warn(port->dev,
				 "Failed to allocate Rx dma buffer, using PIO\n");
			dma_release_channel(chan);
			s->chan_rx = NULL;
			return;
		}

		for (i = 0; i < 2; i++) {
			struct scatterlist *sg = &s->sg_rx[i];

			sg_init_table(sg, 1);
			s->rx_buf[i] = buf;
			sg_dma_address(sg) = dma;
			sg_dma_len(sg) = s->buf_len_rx;

			buf += s->buf_len_rx;
			dma += s->buf_len_rx;
		}

		setup_timer(&s->rx_timer, rx_timer_fn, (unsigned long)s);

		if (port->type == PORT_SCIFA || port->type == PORT_SCIFB)
			sci_submit_rx(s);
	}
}

static void sci_free_dma(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);

	if (s->chan_tx)
		sci_tx_dma_release(s, false);
	if (s->chan_rx)
		sci_rx_dma_release(s, false);
}

static void sci_flush_buffer(struct uart_port *port)
{
	/*
	 * In uart_flush_buffer(), the xmit circular buffer has just been
	 * cleared, so we have to reset tx_dma_len accordingly.
	 */
	to_sci_port(port)->tx_dma_len = 0;
}
#else /* !CONFIG_SERIAL_SH_SCI_DMA */
static inline void sci_request_dma(struct uart_port *port)
{
}

static inline void sci_free_dma(struct uart_port *port)
{
}

#define sci_flush_buffer	NULL
#endif /* !CONFIG_SERIAL_SH_SCI_DMA */

static irqreturn_t sci_rx_interrupt(int irq, void *ptr)
{
#ifdef CONFIG_SERIAL_SH_SCI_DMA
	struct uart_port *port = ptr;
	struct sci_port *s = to_sci_port(port);

	if (s->chan_rx) {
		u16 scr = serial_port_in(port, SCSCR);
		u16 ssr = serial_port_in(port, SCxSR);

		/* Disable future Rx interrupts */
		if (port->type == PORT_SCIFA || port->type == PORT_SCIFB) {
			disable_irq_nosync(irq);
			scr |= SCSCR_RDRQE;
		} else {
			scr &= ~SCSCR_RIE;
			sci_submit_rx(s);
		}
		serial_port_out(port, SCSCR, scr);
		/* Clear current interrupt */
		serial_port_out(port, SCxSR,
				ssr & ~(SCIF_DR | SCxSR_RDxF(port)));
		dev_dbg(port->dev, "Rx IRQ %lu: setup t-out in %u jiffies\n",
			jiffies, s->rx_timeout);
		mod_timer(&s->rx_timer, jiffies + s->rx_timeout);

		return IRQ_HANDLED;
	}
#endif

	/* I think sci_receive_chars has to be called irrespective
	 * of whether the I_IXOFF is set, otherwise, how is the interrupt
	 * to be disabled?
	 */
	sci_receive_chars(ptr);

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
	struct sci_port *s = to_sci_port(port);

	/* Handle errors */
	if (port->type == PORT_SCI) {
		if (sci_handle_errors(port)) {
			/* discard character in rx buffer */
			serial_port_in(port, SCxSR);
			sci_clear_SCxSR(port, SCxSR_RDxF_CLEAR(port));
		}
	} else {
		sci_handle_fifo_overrun(port);
		if (!s->chan_rx)
			sci_receive_chars(ptr);
	}

	sci_clear_SCxSR(port, SCxSR_ERROR_CLEAR(port));

	/* Kick the transmission */
	if (!s->chan_tx)
		sci_tx_interrupt(irq, ptr);

	return IRQ_HANDLED;
}

static irqreturn_t sci_br_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;

	/* Handle BREAKs */
	sci_handle_breaks(port);
	sci_clear_SCxSR(port, SCxSR_BREAK_CLEAR(port));

	return IRQ_HANDLED;
}

static irqreturn_t sci_mpxed_interrupt(int irq, void *ptr)
{
	unsigned short ssr_status, scr_status, err_enabled, orer_status = 0;
	struct uart_port *port = ptr;
	struct sci_port *s = to_sci_port(port);
	irqreturn_t ret = IRQ_NONE;

	ssr_status = serial_port_in(port, SCxSR);
	scr_status = serial_port_in(port, SCSCR);
	if (s->overrun_reg == SCxSR)
		orer_status = ssr_status;
	else {
		if (sci_getreg(port, s->overrun_reg)->size)
			orer_status = serial_port_in(port, s->overrun_reg);
	}

	err_enabled = scr_status & port_rx_irq_mask(port);

	/* Tx Interrupt */
	if ((ssr_status & SCxSR_TDxE(port)) && (scr_status & SCSCR_TIE) &&
	    !s->chan_tx)
		ret = sci_tx_interrupt(irq, ptr);

	/*
	 * Rx Interrupt: if we're using DMA, the DMA controller clears RDF /
	 * DR flags
	 */
	if (((ssr_status & SCxSR_RDxF(port)) || s->chan_rx) &&
	    (scr_status & SCSCR_RIE))
		ret = sci_rx_interrupt(irq, ptr);

	/* Error Interrupt */
	if ((ssr_status & SCxSR_ERRORS(port)) && err_enabled)
		ret = sci_er_interrupt(irq, ptr);

	/* Break Interrupt */
	if ((ssr_status & SCxSR_BRK(port)) && err_enabled)
		ret = sci_br_interrupt(irq, ptr);

	/* Overrun Interrupt */
	if (orer_status & s->overrun_mask) {
		sci_handle_fifo_overrun(port);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static const struct sci_irq_desc {
	const char	*desc;
	irq_handler_t	handler;
} sci_irq_desc[] = {
	/*
	 * Split out handlers, the default case.
	 */
	[SCIx_ERI_IRQ] = {
		.desc = "rx err",
		.handler = sci_er_interrupt,
	},

	[SCIx_RXI_IRQ] = {
		.desc = "rx full",
		.handler = sci_rx_interrupt,
	},

	[SCIx_TXI_IRQ] = {
		.desc = "tx empty",
		.handler = sci_tx_interrupt,
	},

	[SCIx_BRI_IRQ] = {
		.desc = "break",
		.handler = sci_br_interrupt,
	},

	/*
	 * Special muxed handler.
	 */
	[SCIx_MUX_IRQ] = {
		.desc = "mux",
		.handler = sci_mpxed_interrupt,
	},
};

static int sci_request_irq(struct sci_port *port)
{
	struct uart_port *up = &port->port;
	int i, j, ret = 0;

	for (i = j = 0; i < SCIx_NR_IRQS; i++, j++) {
		const struct sci_irq_desc *desc;
		int irq;

		if (SCIx_IRQ_IS_MUXED(port)) {
			i = SCIx_MUX_IRQ;
			irq = up->irq;
		} else {
			irq = port->irqs[i];

			/*
			 * Certain port types won't support all of the
			 * available interrupt sources.
			 */
			if (unlikely(irq < 0))
				continue;
		}

		desc = sci_irq_desc + i;
		port->irqstr[j] = kasprintf(GFP_KERNEL, "%s:%s",
					    dev_name(up->dev), desc->desc);
		if (!port->irqstr[j])
			goto out_nomem;

		ret = request_irq(irq, desc->handler, up->irqflags,
				  port->irqstr[j], port);
		if (unlikely(ret)) {
			dev_err(up->dev, "Can't allocate %s IRQ\n", desc->desc);
			goto out_noirq;
		}
	}

	return 0;

out_noirq:
	while (--i >= 0)
		free_irq(port->irqs[i], port);

out_nomem:
	while (--j >= 0)
		kfree(port->irqstr[j]);

	return ret;
}

static void sci_free_irq(struct sci_port *port)
{
	int i;

	/*
	 * Intentionally in reverse order so we iterate over the muxed
	 * IRQ first.
	 */
	for (i = 0; i < SCIx_NR_IRQS; i++) {
		int irq = port->irqs[i];

		/*
		 * Certain port types won't support all of the available
		 * interrupt sources.
		 */
		if (unlikely(irq < 0))
			continue;

		free_irq(port->irqs[i], port);
		kfree(port->irqstr[i]);

		if (SCIx_IRQ_IS_MUXED(port)) {
			/* If there's only one IRQ, we're done. */
			return;
		}
	}
}

static unsigned int sci_tx_empty(struct uart_port *port)
{
	unsigned short status = serial_port_in(port, SCxSR);
	unsigned short in_tx_fifo = sci_txfill(port);

	return (status & SCxSR_TEND(port)) && !in_tx_fifo ? TIOCSER_TEMT : 0;
}

/*
 * Modem control is a bit of a mixed bag for SCI(F) ports. Generally
 * CTS/RTS is supported in hardware by at least one port and controlled
 * via SCSPTR (SCxPCR for SCIFA/B parts), or external pins (presently
 * handled via the ->init_pins() op, which is a bit of a one-way street,
 * lacking any ability to defer pin control -- this will later be
 * converted over to the GPIO framework).
 *
 * Other modes (such as loopback) are supported generically on certain
 * port types, but not others. For these it's sufficient to test for the
 * existence of the support register and simply ignore the port type.
 */
static void sci_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	if (mctrl & TIOCM_LOOP) {
		const struct plat_sci_reg *reg;

		/*
		 * Standard loopback mode for SCFCR ports.
		 */
		reg = sci_getreg(port, SCFCR);
		if (reg->size)
			serial_port_out(port, SCFCR,
					serial_port_in(port, SCFCR) |
					SCFCR_LOOP);
	}
}

static unsigned int sci_get_mctrl(struct uart_port *port)
{
	/*
	 * CTS/RTS is handled in hardware when supported, while nothing
	 * else is wired up. Keep it simple and simply assert DSR/CAR.
	 */
	return TIOCM_DSR | TIOCM_CAR;
}

static void sci_break_ctl(struct uart_port *port, int break_state)
{
	struct sci_port *s = to_sci_port(port);
	const struct plat_sci_reg *reg = sci_regmap[s->cfg->regtype] + SCSPTR;
	unsigned short scscr, scsptr;

	/* check wheter the port has SCSPTR */
	if (!reg->size) {
		/*
		 * Not supported by hardware. Most parts couple break and rx
		 * interrupts together, with break detection always enabled.
		 */
		return;
	}

	scsptr = serial_port_in(port, SCSPTR);
	scscr = serial_port_in(port, SCSCR);

	if (break_state == -1) {
		scsptr = (scsptr | SCSPTR_SPB2IO) & ~SCSPTR_SPB2DT;
		scscr &= ~SCSCR_TE;
	} else {
		scsptr = (scsptr | SCSPTR_SPB2DT) & ~SCSPTR_SPB2IO;
		scscr |= SCSCR_TE;
	}

	serial_port_out(port, SCSPTR, scsptr);
	serial_port_out(port, SCSCR, scscr);
}

static int sci_startup(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	unsigned long flags;
	int ret;

	dev_dbg(port->dev, "%s(%d)\n", __func__, port->line);

	sci_request_dma(port);

	ret = sci_request_irq(s);
	if (unlikely(ret < 0)) {
		sci_free_dma(port);
		return ret;
	}

	spin_lock_irqsave(&port->lock, flags);
	sci_start_tx(port);
	sci_start_rx(port);
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static void sci_shutdown(struct uart_port *port)
{
	struct sci_port *s = to_sci_port(port);
	unsigned long flags;

	dev_dbg(port->dev, "%s(%d)\n", __func__, port->line);

	spin_lock_irqsave(&port->lock, flags);
	sci_stop_rx(port);
	sci_stop_tx(port);
	spin_unlock_irqrestore(&port->lock, flags);

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	if (s->chan_rx) {
		dev_dbg(port->dev, "%s(%d) deleting rx_timer\n", __func__,
			port->line);
		del_timer_sync(&s->rx_timer);
	}
#endif

	sci_free_irq(s);
	sci_free_dma(port);
}

static unsigned int sci_scbrr_calc(struct sci_port *s, unsigned int bps,
				   unsigned long freq)
{
	if (s->sampling_rate)
		return DIV_ROUND_CLOSEST(freq, s->sampling_rate * bps) - 1;

	/* Warn, but use a safe default */
	WARN_ON(1);

	return ((freq + 16 * bps) / (32 * bps) - 1);
}

/* calculate frame length from SMR */
static int sci_baud_calc_frame_len(unsigned int smr_val)
{
	int len = 10;

	if (smr_val & SCSMR_CHR)
		len--;
	if (smr_val & SCSMR_PE)
		len++;
	if (smr_val & SCSMR_STOP)
		len++;

	return len;
}


/* calculate sample rate, BRR, and clock select for HSCIF */
static void sci_baud_calc_hscif(unsigned int bps, unsigned long freq,
				int *brr, unsigned int *srr,
				unsigned int *cks, int frame_len)
{
	int sr, c, br, err, recv_margin;
	int min_err = 1000; /* 100% */
	int recv_max_margin = 0;

	/* Find the combination of sample rate and clock select with the
	   smallest deviation from the desired baud rate. */
	for (sr = 8; sr <= 32; sr++) {
		for (c = 0; c <= 3; c++) {
			/* integerized formulas from HSCIF documentation */
			br = DIV_ROUND_CLOSEST(freq, (sr *
					      (1 << (2 * c + 1)) * bps)) - 1;
			br = clamp(br, 0, 255);
			err = DIV_ROUND_CLOSEST(freq, ((br + 1) * bps * sr *
					       (1 << (2 * c + 1)) / 1000)) -
					       1000;
			/* Calc recv margin
			 * M: Receive margin (%)
			 * N: Ratio of bit rate to clock (N = sampling rate)
			 * D: Clock duty (D = 0 to 1.0)
			 * L: Frame length (L = 9 to 12)
			 * F: Absolute value of clock frequency deviation
			 *
			 *  M = |(0.5 - 1 / 2 * N) - ((L - 0.5) * F) -
			 *      (|D - 0.5| / N * (1 + F))|
			 *  NOTE: Usually, treat D for 0.5, F is 0 by this
			 *        calculation.
			 */
			recv_margin = abs((500 -
					DIV_ROUND_CLOSEST(1000, sr << 1)) / 10);
			if (abs(min_err) > abs(err)) {
				min_err = err;
				recv_max_margin = recv_margin;
			} else if ((min_err == err) &&
				   (recv_margin > recv_max_margin))
				recv_max_margin = recv_margin;
			else
				continue;

			*brr = br;
			*srr = sr - 1;
			*cks = c;
		}
	}

	if (min_err == 1000) {
		WARN_ON(1);
		/* use defaults */
		*brr = 255;
		*srr = 15;
		*cks = 0;
	}
}

static void sci_reset(struct uart_port *port)
{
	const struct plat_sci_reg *reg;
	unsigned int status;

	do {
		status = serial_port_in(port, SCxSR);
	} while (!(status & SCxSR_TEND(port)));

	serial_port_out(port, SCSCR, 0x00);	/* TE=0, RE=0, CKE1=0 */

	reg = sci_getreg(port, SCFCR);
	if (reg->size)
		serial_port_out(port, SCFCR, SCFCR_RFRST | SCFCR_TFRST);
}

static void sci_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	struct sci_port *s = to_sci_port(port);
	const struct plat_sci_reg *reg;
	unsigned int baud, smr_val = 0, max_baud, cks = 0;
	int t = -1;
	unsigned int srr = 15;

	if ((termios->c_cflag & CSIZE) == CS7)
		smr_val |= SCSMR_CHR;
	if (termios->c_cflag & PARENB)
		smr_val |= SCSMR_PE;
	if (termios->c_cflag & PARODD)
		smr_val |= SCSMR_PE | SCSMR_ODD;
	if (termios->c_cflag & CSTOPB)
		smr_val |= SCSMR_STOP;

	/*
	 * earlyprintk comes here early on with port->uartclk set to zero.
	 * the clock framework is not up and running at this point so here
	 * we assume that 115200 is the maximum baud rate. please note that
	 * the baud rate is not programmed during earlyprintk - it is assumed
	 * that the previous boot loader has enabled required clocks and
	 * setup the baud rate generator hardware for us already.
	 */
	max_baud = port->uartclk ? port->uartclk / 16 : 115200;

	baud = uart_get_baud_rate(port, termios, old, 0, max_baud);
	if (likely(baud && port->uartclk)) {
		if (s->cfg->type == PORT_HSCIF) {
			int frame_len = sci_baud_calc_frame_len(smr_val);
			sci_baud_calc_hscif(baud, port->uartclk, &t, &srr,
					    &cks, frame_len);
		} else {
			t = sci_scbrr_calc(s, baud, port->uartclk);
			for (cks = 0; t >= 256 && cks <= 3; cks++)
				t >>= 2;
		}
	}

	sci_port_enable(s);

	sci_reset(port);

	smr_val |= serial_port_in(port, SCSMR) & SCSMR_CKS;

	uart_update_timeout(port, termios->c_cflag, baud);

	dev_dbg(port->dev, "%s: SMR %x, cks %x, t %x, SCSCR %x\n",
		__func__, smr_val, cks, t, s->cfg->scscr);

	if (t >= 0) {
		serial_port_out(port, SCSMR, (smr_val & ~SCSMR_CKS) | cks);
		serial_port_out(port, SCBRR, t);
		reg = sci_getreg(port, HSSRR);
		if (reg->size)
			serial_port_out(port, HSSRR, srr | HSCIF_SRE);
		udelay((1000000+(baud-1)) / baud); /* Wait one bit interval */
	} else
		serial_port_out(port, SCSMR, smr_val);

	sci_init_pins(port, termios->c_cflag);

	reg = sci_getreg(port, SCFCR);
	if (reg->size) {
		unsigned short ctrl = serial_port_in(port, SCFCR);

		if (s->cfg->capabilities & SCIx_HAVE_RTSCTS) {
			if (termios->c_cflag & CRTSCTS)
				ctrl |= SCFCR_MCE;
			else
				ctrl &= ~SCFCR_MCE;
		}

		/*
		 * As we've done a sci_reset() above, ensure we don't
		 * interfere with the FIFOs while toggling MCE. As the
		 * reset values could still be set, simply mask them out.
		 */
		ctrl &= ~(SCFCR_RFRST | SCFCR_TFRST);

		serial_port_out(port, SCFCR, ctrl);
	}

	serial_port_out(port, SCSCR, s->cfg->scscr);

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	/*
	 * Calculate delay for 2 DMA buffers (4 FIFO).
	 * See serial_core.c::uart_update_timeout().
	 * With 10 bits (CS8), 250Hz, 115200 baud and 64 bytes FIFO, the above
	 * function calculates 1 jiffie for the data plus 5 jiffies for the
	 * "slop(e)." Then below we calculate 5 jiffies (20ms) for 2 DMA
	 * buffers (4 FIFO sizes), but when performing a faster transfer, the
	 * value obtained by this formula is too small. Therefore, if the value
	 * is smaller than 20ms, use 20ms as the timeout value for DMA.
	 */
	if (s->chan_rx) {
		unsigned int bits;

		/* byte size and parity */
		switch (termios->c_cflag & CSIZE) {
		case CS5:
			bits = 7;
			break;
		case CS6:
			bits = 8;
			break;
		case CS7:
			bits = 9;
			break;
		default:
			bits = 10;
			break;
		}

		if (termios->c_cflag & CSTOPB)
			bits++;
		if (termios->c_cflag & PARENB)
			bits++;
		s->rx_timeout = DIV_ROUND_UP((s->buf_len_rx * 2 * bits * HZ) /
					     (baud / 10), 10);
		dev_dbg(port->dev, "DMA Rx t-out %ums, tty t-out %u jiffies\n",
			s->rx_timeout * 1000 / HZ, port->timeout);
		if (s->rx_timeout < msecs_to_jiffies(20))
			s->rx_timeout = msecs_to_jiffies(20);
	}
#endif

	if ((termios->c_cflag & CREAD) != 0)
		sci_start_rx(port);

	sci_port_disable(s);
}

static void sci_pm(struct uart_port *port, unsigned int state,
		   unsigned int oldstate)
{
	struct sci_port *sci_port = to_sci_port(port);

	switch (state) {
	case UART_PM_STATE_OFF:
		sci_port_disable(sci_port);
		break;
	default:
		sci_port_enable(sci_port);
		break;
	}
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
	case PORT_SCIFB:
		return "scifb";
	case PORT_HSCIF:
		return "hscif";
	}

	return NULL;
}

static int sci_remap_port(struct uart_port *port)
{
	struct sci_port *sport = to_sci_port(port);

	/*
	 * Nothing to do if there's already an established membase.
	 */
	if (port->membase)
		return 0;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap_nocache(port->mapbase, sport->reg_size);
		if (unlikely(!port->membase)) {
			dev_err(port->dev, "can't remap port#%d\n", port->line);
			return -ENXIO;
		}
	} else {
		/*
		 * For the simple (and majority of) cases where we don't
		 * need to do any remapping, just cast the cookie
		 * directly.
		 */
		port->membase = (void __iomem *)(uintptr_t)port->mapbase;
	}

	return 0;
}

static void sci_release_port(struct uart_port *port)
{
	struct sci_port *sport = to_sci_port(port);

	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	release_mem_region(port->mapbase, sport->reg_size);
}

static int sci_request_port(struct uart_port *port)
{
	struct resource *res;
	struct sci_port *sport = to_sci_port(port);
	int ret;

	res = request_mem_region(port->mapbase, sport->reg_size,
				 dev_name(port->dev));
	if (unlikely(res == NULL)) {
		dev_err(port->dev, "request_mem_region failed.");
		return -EBUSY;
	}

	ret = sci_remap_port(port);
	if (unlikely(ret != 0)) {
		release_resource(res);
		return ret;
	}

	return 0;
}

static void sci_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		struct sci_port *sport = to_sci_port(port);

		port->type = sport->cfg->type;
		sci_request_port(port);
	}
}

static int sci_verify_port(struct uart_port *port, struct serial_struct *ser)
{
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
	.break_ctl	= sci_break_ctl,
	.startup	= sci_startup,
	.shutdown	= sci_shutdown,
	.flush_buffer	= sci_flush_buffer,
	.set_termios	= sci_set_termios,
	.pm		= sci_pm,
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

static int sci_init_single(struct platform_device *dev,
			   struct sci_port *sci_port, unsigned int index,
			   struct plat_sci_port *p, bool early)
{
	struct uart_port *port = &sci_port->port;
	const struct resource *res;
	unsigned int i;
	int ret;

	sci_port->cfg	= p;

	port->ops	= &sci_uart_ops;
	port->iotype	= UPIO_MEM;
	port->line	= index;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENOMEM;

	port->mapbase = res->start;
	sci_port->reg_size = resource_size(res);

	for (i = 0; i < ARRAY_SIZE(sci_port->irqs); ++i)
		sci_port->irqs[i] = platform_get_irq(dev, i);

	/* The SCI generates several interrupts. They can be muxed together or
	 * connected to different interrupt lines. In the muxed case only one
	 * interrupt resource is specified. In the non-muxed case three or four
	 * interrupt resources are specified, as the BRI interrupt is optional.
	 */
	if (sci_port->irqs[0] < 0)
		return -ENXIO;

	if (sci_port->irqs[1] < 0) {
		sci_port->irqs[1] = sci_port->irqs[0];
		sci_port->irqs[2] = sci_port->irqs[0];
		sci_port->irqs[3] = sci_port->irqs[0];
	}

	if (p->regtype == SCIx_PROBE_REGTYPE) {
		ret = sci_probe_regmap(p);
		if (unlikely(ret))
			return ret;
	}

	switch (p->type) {
	case PORT_SCIFB:
		port->fifosize = 256;
		sci_port->overrun_reg = SCxSR;
		sci_port->overrun_mask = SCIFA_ORER;
		sci_port->sampling_rate = 16;
		break;
	case PORT_HSCIF:
		port->fifosize = 128;
		sci_port->overrun_reg = SCLSR;
		sci_port->overrun_mask = SCLSR_ORER;
		sci_port->sampling_rate = 0;
		break;
	case PORT_SCIFA:
		port->fifosize = 64;
		sci_port->overrun_reg = SCxSR;
		sci_port->overrun_mask = SCIFA_ORER;
		sci_port->sampling_rate = 16;
		break;
	case PORT_SCIF:
		port->fifosize = 16;
		if (p->regtype == SCIx_SH7705_SCIF_REGTYPE) {
			sci_port->overrun_reg = SCxSR;
			sci_port->overrun_mask = SCIFA_ORER;
			sci_port->sampling_rate = 16;
		} else {
			sci_port->overrun_reg = SCLSR;
			sci_port->overrun_mask = SCLSR_ORER;
			sci_port->sampling_rate = 32;
		}
		break;
	default:
		port->fifosize = 1;
		sci_port->overrun_reg = SCxSR;
		sci_port->overrun_mask = SCI_ORER;
		sci_port->sampling_rate = 32;
		break;
	}

	/* SCIFA on sh7723 and sh7724 need a custom sampling rate that doesn't
	 * match the SoC datasheet, this should be investigated. Let platform
	 * data override the sampling rate for now.
	 */
	if (p->sampling_rate)
		sci_port->sampling_rate = p->sampling_rate;

	if (!early) {
		sci_port->iclk = clk_get(&dev->dev, "sci_ick");
		if (IS_ERR(sci_port->iclk)) {
			sci_port->iclk = clk_get(&dev->dev, "peripheral_clk");
			if (IS_ERR(sci_port->iclk)) {
				dev_err(&dev->dev, "can't get iclk\n");
				return PTR_ERR(sci_port->iclk);
			}
		}

		/*
		 * The function clock is optional, ignore it if we can't
		 * find it.
		 */
		sci_port->fclk = clk_get(&dev->dev, "sci_fck");
		if (IS_ERR(sci_port->fclk))
			sci_port->fclk = NULL;

		port->dev = &dev->dev;

		pm_runtime_enable(&dev->dev);
	}

	sci_port->break_timer.data = (unsigned long)sci_port;
	sci_port->break_timer.function = sci_break_timer;
	init_timer(&sci_port->break_timer);

	/*
	 * Establish some sensible defaults for the error detection.
	 */
	if (p->type == PORT_SCI) {
		sci_port->error_mask = SCI_DEFAULT_ERROR_MASK;
		sci_port->error_clear = SCI_ERROR_CLEAR;
	} else {
		sci_port->error_mask = SCIF_DEFAULT_ERROR_MASK;
		sci_port->error_clear = SCIF_ERROR_CLEAR;
	}

	/*
	 * Make the error mask inclusive of overrun detection, if
	 * supported.
	 */
	if (sci_port->overrun_reg == SCxSR) {
		sci_port->error_mask |= sci_port->overrun_mask;
		sci_port->error_clear &= ~sci_port->overrun_mask;
	}

	port->type		= p->type;
	port->flags		= UPF_FIXED_PORT | p->flags;
	port->regshift		= p->regshift;

	/*
	 * The UART port needs an IRQ value, so we peg this to the RX IRQ
	 * for the multi-IRQ ports, which is where we are primarily
	 * concerned with the shutdown path synchronization.
	 *
	 * For the muxed case there's nothing more to do.
	 */
	port->irq		= sci_port->irqs[SCIx_RXI_IRQ];
	port->irqflags		= 0;

	port->serial_in		= sci_serial_in;
	port->serial_out	= sci_serial_out;

	if (p->dma_slave_tx > 0 && p->dma_slave_rx > 0)
		dev_dbg(port->dev, "DMA tx %d, rx %d\n",
			p->dma_slave_tx, p->dma_slave_rx);

	return 0;
}

static void sci_cleanup_single(struct sci_port *port)
{
	clk_put(port->iclk);
	clk_put(port->fclk);

	pm_runtime_disable(port->port.dev);
}

#ifdef CONFIG_SERIAL_SH_SCI_CONSOLE
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
	struct sci_port *sci_port = &sci_ports[co->index];
	struct uart_port *port = &sci_port->port;
	unsigned short bits, ctrl;
	unsigned long flags;
	int locked = 1;

	local_irq_save(flags);
	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&port->lock);
	else
		spin_lock(&port->lock);

	/* first save the SCSCR then disable the interrupts */
	ctrl = serial_port_in(port, SCSCR);
	serial_port_out(port, SCSCR, sci_port->cfg->scscr);

	uart_console_write(port, s, count, serial_console_putchar);

	/* wait until fifo is empty and last bit has been transmitted */
	bits = SCxSR_TDxE(port) | SCxSR_TEND(port);
	while ((serial_port_in(port, SCxSR) & bits) != bits)
		cpu_relax();

	/* restore the SCSCR */
	serial_port_out(port, SCSCR, ctrl);

	if (locked)
		spin_unlock(&port->lock);
	local_irq_restore(flags);
}

static int serial_console_setup(struct console *co, char *options)
{
	struct sci_port *sci_port;
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	/*
	 * Refuse to handle any bogus ports.
	 */
	if (co->index < 0 || co->index >= SCI_NPORTS)
		return -ENODEV;

	sci_port = &sci_ports[co->index];
	port = &sci_port->port;

	/*
	 * Refuse to handle uninitialized ports.
	 */
	if (!port->ops)
		return -ENODEV;

	ret = sci_remap_port(port);
	if (unlikely(ret != 0))
		return ret;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console serial_console = {
	.name		= "ttySC",
	.device		= uart_console_device,
	.write		= serial_console_write,
	.setup		= serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &sci_uart_driver,
};

static struct console early_serial_console = {
	.name           = "early_ttySC",
	.write          = serial_console_write,
	.flags          = CON_PRINTBUFFER,
	.index		= -1,
};

static char early_serial_buf[32];

static int sci_probe_earlyprintk(struct platform_device *pdev)
{
	struct plat_sci_port *cfg = dev_get_platdata(&pdev->dev);

	if (early_serial_console.data)
		return -EEXIST;

	early_serial_console.index = pdev->id;

	sci_init_single(pdev, &sci_ports[pdev->id], pdev->id, cfg, true);

	serial_console_setup(&early_serial_console, early_serial_buf);

	if (!strstr(early_serial_buf, "keep"))
		early_serial_console.flags |= CON_BOOT;

	register_console(&early_serial_console);
	return 0;
}

#define SCI_CONSOLE	(&serial_console)

#else
static inline int sci_probe_earlyprintk(struct platform_device *pdev)
{
	return -EINVAL;
}

#define SCI_CONSOLE	NULL

#endif /* CONFIG_SERIAL_SH_SCI_CONSOLE */

static const char banner[] __initconst = "SuperH (H)SCI(F) driver initialized";

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
	struct sci_port *port = platform_get_drvdata(dev);

	uart_remove_one_port(&sci_uart_driver, &port->port);

	sci_cleanup_single(port);

	return 0;
}

struct sci_port_info {
	unsigned int type;
	unsigned int regtype;
};

static const struct of_device_id of_sci_match[] = {
	{
		.compatible = "renesas,scif",
		.data = &(const struct sci_port_info) {
			.type = PORT_SCIF,
			.regtype = SCIx_SH4_SCIF_REGTYPE,
		},
	}, {
		.compatible = "renesas,scifa",
		.data = &(const struct sci_port_info) {
			.type = PORT_SCIFA,
			.regtype = SCIx_SCIFA_REGTYPE,
		},
	}, {
		.compatible = "renesas,scifb",
		.data = &(const struct sci_port_info) {
			.type = PORT_SCIFB,
			.regtype = SCIx_SCIFB_REGTYPE,
		},
	}, {
		.compatible = "renesas,hscif",
		.data = &(const struct sci_port_info) {
			.type = PORT_HSCIF,
			.regtype = SCIx_HSCIF_REGTYPE,
		},
	}, {
		.compatible = "renesas,sci",
		.data = &(const struct sci_port_info) {
			.type = PORT_SCI,
			.regtype = SCIx_SCI_REGTYPE,
		},
	}, {
		/* Terminator */
	},
};
MODULE_DEVICE_TABLE(of, of_sci_match);

static struct plat_sci_port *
sci_parse_dt(struct platform_device *pdev, unsigned int *dev_id)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct sci_port_info *info;
	struct plat_sci_port *p;
	int id;

	if (!IS_ENABLED(CONFIG_OF) || !np)
		return NULL;

	match = of_match_node(of_sci_match, pdev->dev.of_node);
	if (!match)
		return NULL;

	info = match->data;

	p = devm_kzalloc(&pdev->dev, sizeof(struct plat_sci_port), GFP_KERNEL);
	if (!p)
		return NULL;

	/* Get the line number for the aliases node. */
	id = of_alias_get_id(np, "serial");
	if (id < 0) {
		dev_err(&pdev->dev, "failed to get alias id (%d)\n", id);
		return NULL;
	}

	*dev_id = id;

	p->flags = UPF_IOREMAP | UPF_BOOT_AUTOCONF;
	p->type = info->type;
	p->regtype = info->regtype;
	p->scscr = SCSCR_RE | SCSCR_TE;

	return p;
}

static int sci_probe_single(struct platform_device *dev,
				      unsigned int index,
				      struct plat_sci_port *p,
				      struct sci_port *sciport)
{
	int ret;

	/* Sanity check */
	if (unlikely(index >= SCI_NPORTS)) {
		dev_notice(&dev->dev, "Attempting to register port %d when only %d are available\n",
			   index+1, SCI_NPORTS);
		dev_notice(&dev->dev, "Consider bumping CONFIG_SERIAL_SH_SCI_NR_UARTS!\n");
		return -EINVAL;
	}

	ret = sci_init_single(dev, sciport, index, p, false);
	if (ret)
		return ret;

	ret = uart_add_one_port(&sci_uart_driver, &sciport->port);
	if (ret) {
		sci_cleanup_single(sciport);
		return ret;
	}

	return 0;
}

static int sci_probe(struct platform_device *dev)
{
	struct plat_sci_port *p;
	struct sci_port *sp;
	unsigned int dev_id;
	int ret;

	/*
	 * If we've come here via earlyprintk initialization, head off to
	 * the special early probe. We don't have sufficient device state
	 * to make it beyond this yet.
	 */
	if (is_early_platform_device(dev))
		return sci_probe_earlyprintk(dev);

	if (dev->dev.of_node) {
		p = sci_parse_dt(dev, &dev_id);
		if (p == NULL)
			return -EINVAL;
	} else {
		p = dev->dev.platform_data;
		if (p == NULL) {
			dev_err(&dev->dev, "no platform data supplied\n");
			return -EINVAL;
		}

		dev_id = dev->id;
	}

	sp = &sci_ports[dev_id];
	platform_set_drvdata(dev, sp);

	ret = sci_probe_single(dev, dev_id, p, sp);
	if (ret)
		return ret;

#ifdef CONFIG_SH_STANDARD_BIOS
	sh_bios_gdb_detach();
#endif

	return 0;
}

static __maybe_unused int sci_suspend(struct device *dev)
{
	struct sci_port *sport = dev_get_drvdata(dev);

	if (sport)
		uart_suspend_port(&sci_uart_driver, &sport->port);

	return 0;
}

static __maybe_unused int sci_resume(struct device *dev)
{
	struct sci_port *sport = dev_get_drvdata(dev);

	if (sport)
		uart_resume_port(&sci_uart_driver, &sport->port);

	return 0;
}

static SIMPLE_DEV_PM_OPS(sci_dev_pm_ops, sci_suspend, sci_resume);

static struct platform_driver sci_driver = {
	.probe		= sci_probe,
	.remove		= sci_remove,
	.driver		= {
		.name	= "sh-sci",
		.pm	= &sci_dev_pm_ops,
		.of_match_table = of_match_ptr(of_sci_match),
	},
};

static int __init sci_init(void)
{
	int ret;

	pr_info("%s\n", banner);

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

#ifdef CONFIG_SERIAL_SH_SCI_CONSOLE
early_platform_init_buffer("earlyprintk", &sci_driver,
			   early_serial_buf, ARRAY_SIZE(early_serial_buf));
#endif
module_init(sci_init);
module_exit(sci_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sh-sci");
MODULE_AUTHOR("Paul Mundt");
MODULE_DESCRIPTION("SuperH (H)SCI(F) serial driver");
