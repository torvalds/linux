/*
 * Copyright (C) Maxime Coquelin 2015
 * Authors:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 *	     Gerald Baeza <gerald.baeza@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Inspired by st-asc.c from STMicroelectronics (c)
 */

#if defined(CONFIG_SERIAL_STM32_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/serial_core.h>
#include <linux/clk.h>

#define DRIVER_NAME "stm32-usart"

struct stm32_usart_offsets {
	u8 cr1;
	u8 cr2;
	u8 cr3;
	u8 brr;
	u8 gtpr;
	u8 rtor;
	u8 rqr;
	u8 isr;
	u8 icr;
	u8 rdr;
	u8 tdr;
};

struct stm32_usart_config {
	u8 uart_enable_bit; /* USART_CR1_UE */
	bool has_7bits_data;
};

struct stm32_usart_info {
	struct stm32_usart_offsets ofs;
	struct stm32_usart_config cfg;
};

#define UNDEF_REG ~0

/* Register offsets */
struct stm32_usart_info stm32f4_info = {
	.ofs = {
		.isr	= 0x00,
		.rdr	= 0x04,
		.tdr	= 0x04,
		.brr	= 0x08,
		.cr1	= 0x0c,
		.cr2	= 0x10,
		.cr3	= 0x14,
		.gtpr	= 0x18,
		.rtor	= UNDEF_REG,
		.rqr	= UNDEF_REG,
		.icr	= UNDEF_REG,
	},
	.cfg = {
		.uart_enable_bit = 13,
		.has_7bits_data = false,
	}
};

struct stm32_usart_info stm32f7_info = {
	.ofs = {
		.cr1	= 0x00,
		.cr2	= 0x04,
		.cr3	= 0x08,
		.brr	= 0x0c,
		.gtpr	= 0x10,
		.rtor	= 0x14,
		.rqr	= 0x18,
		.isr	= 0x1c,
		.icr	= 0x20,
		.rdr	= 0x24,
		.tdr	= 0x28,
	},
	.cfg = {
		.uart_enable_bit = 0,
		.has_7bits_data = true,
	}
};

/* USART_SR (F4) / USART_ISR (F7) */
#define USART_SR_PE		BIT(0)
#define USART_SR_FE		BIT(1)
#define USART_SR_NF		BIT(2)
#define USART_SR_ORE		BIT(3)
#define USART_SR_IDLE		BIT(4)
#define USART_SR_RXNE		BIT(5)
#define USART_SR_TC		BIT(6)
#define USART_SR_TXE		BIT(7)
#define USART_SR_LBD		BIT(8)
#define USART_SR_CTSIF		BIT(9)
#define USART_SR_CTS		BIT(10)		/* F7 */
#define USART_SR_RTOF		BIT(11)		/* F7 */
#define USART_SR_EOBF		BIT(12)		/* F7 */
#define USART_SR_ABRE		BIT(14)		/* F7 */
#define USART_SR_ABRF		BIT(15)		/* F7 */
#define USART_SR_BUSY		BIT(16)		/* F7 */
#define USART_SR_CMF		BIT(17)		/* F7 */
#define USART_SR_SBKF		BIT(18)		/* F7 */
#define USART_SR_TEACK		BIT(21)		/* F7 */
#define USART_SR_ERR_MASK	(USART_SR_LBD | USART_SR_ORE | \
				 USART_SR_FE | USART_SR_PE)
/* Dummy bits */
#define USART_SR_DUMMY_RX	BIT(16)

/* USART_DR */
#define USART_DR_MASK		GENMASK(8, 0)

/* USART_BRR */
#define USART_BRR_DIV_F_MASK	GENMASK(3, 0)
#define USART_BRR_DIV_M_MASK	GENMASK(15, 4)
#define USART_BRR_DIV_M_SHIFT	4

/* USART_CR1 */
#define USART_CR1_SBK		BIT(0)
#define USART_CR1_RWU		BIT(1)		/* F4 */
#define USART_CR1_RE		BIT(2)
#define USART_CR1_TE		BIT(3)
#define USART_CR1_IDLEIE	BIT(4)
#define USART_CR1_RXNEIE	BIT(5)
#define USART_CR1_TCIE		BIT(6)
#define USART_CR1_TXEIE		BIT(7)
#define USART_CR1_PEIE		BIT(8)
#define USART_CR1_PS		BIT(9)
#define USART_CR1_PCE		BIT(10)
#define USART_CR1_WAKE		BIT(11)
#define USART_CR1_M		BIT(12)
#define USART_CR1_M0		BIT(12)		/* F7 */
#define USART_CR1_MME		BIT(13)		/* F7 */
#define USART_CR1_CMIE		BIT(14)		/* F7 */
#define USART_CR1_OVER8		BIT(15)
#define USART_CR1_DEDT_MASK	GENMASK(20, 16)	/* F7 */
#define USART_CR1_DEAT_MASK	GENMASK(25, 21)	/* F7 */
#define USART_CR1_RTOIE		BIT(26)		/* F7 */
#define USART_CR1_EOBIE		BIT(27)		/* F7 */
#define USART_CR1_M1		BIT(28)		/* F7 */
#define USART_CR1_IE_MASK	(GENMASK(8, 4) | BIT(14) | BIT(26) | BIT(27))

/* USART_CR2 */
#define USART_CR2_ADD_MASK	GENMASK(3, 0)	/* F4 */
#define USART_CR2_ADDM7		BIT(4)		/* F7 */
#define USART_CR2_LBDL		BIT(5)
#define USART_CR2_LBDIE		BIT(6)
#define USART_CR2_LBCL		BIT(8)
#define USART_CR2_CPHA		BIT(9)
#define USART_CR2_CPOL		BIT(10)
#define USART_CR2_CLKEN		BIT(11)
#define USART_CR2_STOP_2B	BIT(13)
#define USART_CR2_STOP_MASK	GENMASK(13, 12)
#define USART_CR2_LINEN		BIT(14)
#define USART_CR2_SWAP		BIT(15)		/* F7 */
#define USART_CR2_RXINV		BIT(16)		/* F7 */
#define USART_CR2_TXINV		BIT(17)		/* F7 */
#define USART_CR2_DATAINV	BIT(18)		/* F7 */
#define USART_CR2_MSBFIRST	BIT(19)		/* F7 */
#define USART_CR2_ABREN		BIT(20)		/* F7 */
#define USART_CR2_ABRMOD_MASK	GENMASK(22, 21)	/* F7 */
#define USART_CR2_RTOEN		BIT(23)		/* F7 */
#define USART_CR2_ADD_F7_MASK	GENMASK(31, 24)	/* F7 */

/* USART_CR3 */
#define USART_CR3_EIE		BIT(0)
#define USART_CR3_IREN		BIT(1)
#define USART_CR3_IRLP		BIT(2)
#define USART_CR3_HDSEL		BIT(3)
#define USART_CR3_NACK		BIT(4)
#define USART_CR3_SCEN		BIT(5)
#define USART_CR3_DMAR		BIT(6)
#define USART_CR3_DMAT		BIT(7)
#define USART_CR3_RTSE		BIT(8)
#define USART_CR3_CTSE		BIT(9)
#define USART_CR3_CTSIE		BIT(10)
#define USART_CR3_ONEBIT	BIT(11)
#define USART_CR3_OVRDIS	BIT(12)		/* F7 */
#define USART_CR3_DDRE		BIT(13)		/* F7 */
#define USART_CR3_DEM		BIT(14)		/* F7 */
#define USART_CR3_DEP		BIT(15)		/* F7 */
#define USART_CR3_SCARCNT_MASK	GENMASK(19, 17)	/* F7 */

/* USART_GTPR */
#define USART_GTPR_PSC_MASK	GENMASK(7, 0)
#define USART_GTPR_GT_MASK	GENMASK(15, 8)

/* USART_RTOR */
#define USART_RTOR_RTO_MASK	GENMASK(23, 0)	/* F7 */
#define USART_RTOR_BLEN_MASK	GENMASK(31, 24)	/* F7 */

/* USART_RQR */
#define USART_RQR_ABRRQ		BIT(0)		/* F7 */
#define USART_RQR_SBKRQ		BIT(1)		/* F7 */
#define USART_RQR_MMRQ		BIT(2)		/* F7 */
#define USART_RQR_RXFRQ		BIT(3)		/* F7 */
#define USART_RQR_TXFRQ		BIT(4)		/* F7 */

/* USART_ICR */
#define USART_ICR_PECF		BIT(0)		/* F7 */
#define USART_ICR_FFECF		BIT(1)		/* F7 */
#define USART_ICR_NCF		BIT(2)		/* F7 */
#define USART_ICR_ORECF		BIT(3)		/* F7 */
#define USART_ICR_IDLECF	BIT(4)		/* F7 */
#define USART_ICR_TCCF		BIT(6)		/* F7 */
#define USART_ICR_LBDCF		BIT(8)		/* F7 */
#define USART_ICR_CTSCF		BIT(9)		/* F7 */
#define USART_ICR_RTOCF		BIT(11)		/* F7 */
#define USART_ICR_EOBCF		BIT(12)		/* F7 */
#define USART_ICR_CMCF		BIT(17)		/* F7 */

#define STM32_SERIAL_NAME "ttyS"
#define STM32_MAX_PORTS 6

struct stm32_port {
	struct uart_port port;
	struct clk *clk;
	struct stm32_usart_info *info;
	bool hw_flow_control;
};

static struct stm32_port stm32_ports[STM32_MAX_PORTS];
static struct uart_driver stm32_usart_driver;

static void stm32_stop_tx(struct uart_port *port);

static inline struct stm32_port *to_stm32_port(struct uart_port *port)
{
	return container_of(port, struct stm32_port, port);
}

static void stm32_set_bits(struct uart_port *port, u32 reg, u32 bits)
{
	u32 val;

	val = readl_relaxed(port->membase + reg);
	val |= bits;
	writel_relaxed(val, port->membase + reg);
}

static void stm32_clr_bits(struct uart_port *port, u32 reg, u32 bits)
{
	u32 val;

	val = readl_relaxed(port->membase + reg);
	val &= ~bits;
	writel_relaxed(val, port->membase + reg);
}

static void stm32_receive_chars(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long c;
	u32 sr;
	char flag;

	if (port->irq_wake)
		pm_wakeup_event(tport->tty->dev, 0);

	while ((sr = readl_relaxed(port->membase + ofs->isr)) & USART_SR_RXNE) {
		sr |= USART_SR_DUMMY_RX;
		c = readl_relaxed(port->membase + ofs->rdr);
		flag = TTY_NORMAL;
		port->icount.rx++;

		if (sr & USART_SR_ERR_MASK) {
			if (sr & USART_SR_LBD) {
				port->icount.brk++;
				if (uart_handle_break(port))
					continue;
			} else if (sr & USART_SR_ORE) {
				if (ofs->icr != UNDEF_REG)
					writel_relaxed(USART_ICR_ORECF,
						       port->membase +
						       ofs->icr);
				port->icount.overrun++;
			} else if (sr & USART_SR_PE) {
				port->icount.parity++;
			} else if (sr & USART_SR_FE) {
				port->icount.frame++;
			}

			sr &= port->read_status_mask;

			if (sr & USART_SR_LBD)
				flag = TTY_BREAK;
			else if (sr & USART_SR_PE)
				flag = TTY_PARITY;
			else if (sr & USART_SR_FE)
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, c))
			continue;
		uart_insert_char(port, sr, USART_SR_ORE, c, flag);
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tport);
	spin_lock(&port->lock);
}

static void stm32_transmit_chars(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		writel_relaxed(port->x_char, port->membase + ofs->tdr);
		port->x_char = 0;
		port->icount.tx++;
		return;
	}

	if (uart_tx_stopped(port)) {
		stm32_stop_tx(port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		stm32_stop_tx(port);
		return;
	}

	writel_relaxed(xmit->buf[xmit->tail], port->membase + ofs->tdr);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		stm32_stop_tx(port);
}

static irqreturn_t stm32_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	u32 sr;

	spin_lock(&port->lock);

	sr = readl_relaxed(port->membase + ofs->isr);

	if (sr & USART_SR_RXNE)
		stm32_receive_chars(port);

	if (sr & USART_SR_TXE)
		stm32_transmit_chars(port);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static unsigned int stm32_tx_empty(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	return readl_relaxed(port->membase + ofs->isr) & USART_SR_TXE;
}

static void stm32_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	if ((mctrl & TIOCM_RTS) && (port->status & UPSTAT_AUTORTS))
		stm32_set_bits(port, ofs->cr3, USART_CR3_RTSE);
	else
		stm32_clr_bits(port, ofs->cr3, USART_CR3_RTSE);
}

static unsigned int stm32_get_mctrl(struct uart_port *port)
{
	/* This routine is used to get signals of: DCD, DSR, RI, and CTS */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/* Transmit stop */
static void stm32_stop_tx(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	stm32_clr_bits(port, ofs->cr1, USART_CR1_TXEIE);
}

/* There are probably characters waiting to be transmitted. */
static void stm32_start_tx(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_circ_empty(xmit))
		return;

	stm32_set_bits(port, ofs->cr1, USART_CR1_TXEIE | USART_CR1_TE);
}

/* Throttle the remote when input buffer is about to overflow. */
static void stm32_throttle(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	stm32_clr_bits(port, ofs->cr1, USART_CR1_RXNEIE);
	spin_unlock_irqrestore(&port->lock, flags);
}

/* Unthrottle the remote, the input buffer can now accept data. */
static void stm32_unthrottle(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	stm32_set_bits(port, ofs->cr1, USART_CR1_RXNEIE);
	spin_unlock_irqrestore(&port->lock, flags);
}

/* Receive stop */
static void stm32_stop_rx(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	stm32_clr_bits(port, ofs->cr1, USART_CR1_RXNEIE);
}

/* Handle breaks - ignored by us */
static void stm32_break_ctl(struct uart_port *port, int break_state)
{
}

static int stm32_startup(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	const char *name = to_platform_device(port->dev)->name;
	u32 val;
	int ret;

	ret = request_irq(port->irq, stm32_interrupt, 0, name, port);
	if (ret)
		return ret;

	val = USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;
	stm32_set_bits(port, ofs->cr1, val);

	return 0;
}

static void stm32_shutdown(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	u32 val;

	val = USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;
	stm32_set_bits(port, ofs->cr1, val);

	free_irq(port->irq, port);
}

static void stm32_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct stm32_usart_config *cfg = &stm32_port->info->cfg;
	unsigned int baud;
	u32 usartdiv, mantissa, fraction, oversampling;
	tcflag_t cflag = termios->c_cflag;
	u32 cr1, cr2, cr3;
	unsigned long flags;

	if (!stm32_port->hw_flow_control)
		cflag &= ~CRTSCTS;

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 8);

	spin_lock_irqsave(&port->lock, flags);

	/* Stop serial port and reset value */
	writel_relaxed(0, port->membase + ofs->cr1);

	cr1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
	cr1 |= BIT(cfg->uart_enable_bit);
	cr2 = 0;
	cr3 = 0;

	if (cflag & CSTOPB)
		cr2 |= USART_CR2_STOP_2B;

	if (cflag & PARENB) {
		cr1 |= USART_CR1_PCE;
		if ((cflag & CSIZE) == CS8) {
			if (cfg->has_7bits_data)
				cr1 |= USART_CR1_M0;
			else
				cr1 |= USART_CR1_M;
		}
	}

	if (cflag & PARODD)
		cr1 |= USART_CR1_PS;

	port->status &= ~(UPSTAT_AUTOCTS | UPSTAT_AUTORTS);
	if (cflag & CRTSCTS) {
		port->status |= UPSTAT_AUTOCTS | UPSTAT_AUTORTS;
		cr3 |= USART_CR3_CTSE;
	}

	usartdiv = DIV_ROUND_CLOSEST(port->uartclk, baud);

	/*
	 * The USART supports 16 or 8 times oversampling.
	 * By default we prefer 16 times oversampling, so that the receiver
	 * has a better tolerance to clock deviations.
	 * 8 times oversampling is only used to achieve higher speeds.
	 */
	if (usartdiv < 16) {
		oversampling = 8;
		stm32_set_bits(port, ofs->cr1, USART_CR1_OVER8);
	} else {
		oversampling = 16;
		stm32_clr_bits(port, ofs->cr1, USART_CR1_OVER8);
	}

	mantissa = (usartdiv / oversampling) << USART_BRR_DIV_M_SHIFT;
	fraction = usartdiv % oversampling;
	writel_relaxed(mantissa | fraction, port->membase + ofs->brr);

	uart_update_timeout(port, cflag, baud);

	port->read_status_mask = USART_SR_ORE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= USART_SR_PE | USART_SR_FE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		port->read_status_mask |= USART_SR_LBD;

	/* Characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask = USART_SR_PE | USART_SR_FE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= USART_SR_LBD;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= USART_SR_ORE;
	}

	/* Ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= USART_SR_DUMMY_RX;

	writel_relaxed(cr3, port->membase + ofs->cr3);
	writel_relaxed(cr2, port->membase + ofs->cr2);
	writel_relaxed(cr1, port->membase + ofs->cr1);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *stm32_type(struct uart_port *port)
{
	return (port->type == PORT_STM32) ? DRIVER_NAME : NULL;
}

static void stm32_release_port(struct uart_port *port)
{
}

static int stm32_request_port(struct uart_port *port)
{
	return 0;
}

static void stm32_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_STM32;
}

static int
stm32_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* No user changeable parameters */
	return -EINVAL;
}

static void stm32_pm(struct uart_port *port, unsigned int state,
		unsigned int oldstate)
{
	struct stm32_port *stm32port = container_of(port,
			struct stm32_port, port);
	struct stm32_usart_offsets *ofs = &stm32port->info->ofs;
	struct stm32_usart_config *cfg = &stm32port->info->cfg;
	unsigned long flags = 0;

	switch (state) {
	case UART_PM_STATE_ON:
		clk_prepare_enable(stm32port->clk);
		break;
	case UART_PM_STATE_OFF:
		spin_lock_irqsave(&port->lock, flags);
		stm32_clr_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));
		spin_unlock_irqrestore(&port->lock, flags);
		clk_disable_unprepare(stm32port->clk);
		break;
	}
}

static const struct uart_ops stm32_uart_ops = {
	.tx_empty	= stm32_tx_empty,
	.set_mctrl	= stm32_set_mctrl,
	.get_mctrl	= stm32_get_mctrl,
	.stop_tx	= stm32_stop_tx,
	.start_tx	= stm32_start_tx,
	.throttle	= stm32_throttle,
	.unthrottle	= stm32_unthrottle,
	.stop_rx	= stm32_stop_rx,
	.break_ctl	= stm32_break_ctl,
	.startup	= stm32_startup,
	.shutdown	= stm32_shutdown,
	.set_termios	= stm32_set_termios,
	.pm		= stm32_pm,
	.type		= stm32_type,
	.release_port	= stm32_release_port,
	.request_port	= stm32_request_port,
	.config_port	= stm32_config_port,
	.verify_port	= stm32_verify_port,
};

static int stm32_init_port(struct stm32_port *stm32port,
			  struct platform_device *pdev)
{
	struct uart_port *port = &stm32port->port;
	struct resource *res;
	int ret;

	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &stm32_uart_ops;
	port->dev	= &pdev->dev;
	port->irq	= platform_get_irq(pdev, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);
	port->mapbase = res->start;

	spin_lock_init(&port->lock);

	stm32port->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(stm32port->clk))
		return PTR_ERR(stm32port->clk);

	/* Ensure that clk rate is correct by enabling the clk */
	ret = clk_prepare_enable(stm32port->clk);
	if (ret)
		return ret;

	stm32port->port.uartclk = clk_get_rate(stm32port->clk);
	if (!stm32port->port.uartclk)
		ret = -EINVAL;

	clk_disable_unprepare(stm32port->clk);

	return ret;
}

static struct stm32_port *stm32_of_get_stm32_port(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int id;

	if (!np)
		return NULL;

	id = of_alias_get_id(np, "serial");
	if (id < 0)
		id = 0;

	if (WARN_ON(id >= STM32_MAX_PORTS))
		return NULL;

	stm32_ports[id].hw_flow_control = of_property_read_bool(np,
							"auto-flow-control");
	stm32_ports[id].port.line = id;
	return &stm32_ports[id];
}

#ifdef CONFIG_OF
static const struct of_device_id stm32_match[] = {
	{ .compatible = "st,stm32-usart", .data = &stm32f4_info},
	{ .compatible = "st,stm32-uart", .data = &stm32f4_info},
	{ .compatible = "st,stm32f7-usart", .data = &stm32f7_info},
	{ .compatible = "st,stm32f7-uart", .data = &stm32f7_info},
	{},
};

MODULE_DEVICE_TABLE(of, stm32_match);
#endif

static int stm32_serial_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct stm32_port *stm32port;
	int ret;

	stm32port = stm32_of_get_stm32_port(pdev);
	if (!stm32port)
		return -ENODEV;

	match = of_match_device(stm32_match, &pdev->dev);
	if (match && match->data)
		stm32port->info = (struct stm32_usart_info *)match->data;
	else
		return -EINVAL;

	ret = stm32_init_port(stm32port, pdev);
	if (ret)
		return ret;

	ret = uart_add_one_port(&stm32_usart_driver, &stm32port->port);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, &stm32port->port);

	return 0;
}

static int stm32_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	return uart_remove_one_port(&stm32_usart_driver, port);
}


#ifdef CONFIG_SERIAL_STM32_CONSOLE
static void stm32_console_putchar(struct uart_port *port, int ch)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	while (!(readl_relaxed(port->membase + ofs->isr) & USART_SR_TXE))
		cpu_relax();

	writel_relaxed(ch, port->membase + ofs->tdr);
}

static void stm32_console_write(struct console *co, const char *s, unsigned cnt)
{
	struct uart_port *port = &stm32_ports[co->index].port;
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long flags;
	u32 old_cr1, new_cr1;
	int locked = 1;

	local_irq_save(flags);
	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&port->lock);
	else
		spin_lock(&port->lock);

	/* Save and disable interrupts */
	old_cr1 = readl_relaxed(port->membase + ofs->cr1);
	new_cr1 = old_cr1 & ~USART_CR1_IE_MASK;
	writel_relaxed(new_cr1, port->membase + ofs->cr1);

	uart_console_write(port, s, cnt, stm32_console_putchar);

	/* Restore interrupt state */
	writel_relaxed(old_cr1, port->membase + ofs->cr1);

	if (locked)
		spin_unlock(&port->lock);
	local_irq_restore(flags);
}

static int stm32_console_setup(struct console *co, char *options)
{
	struct stm32_port *stm32port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= STM32_MAX_PORTS)
		return -ENODEV;

	stm32port = &stm32_ports[co->index];

	/*
	 * This driver does not support early console initialization
	 * (use ARM early printk support instead), so we only expect
	 * this to be called during the uart port registration when the
	 * driver gets probed and the port should be mapped at that point.
	 */
	if (stm32port->port.mapbase == 0 || stm32port->port.membase == NULL)
		return -ENXIO;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&stm32port->port, co, baud, parity, bits, flow);
}

static struct console stm32_console = {
	.name		= STM32_SERIAL_NAME,
	.device		= uart_console_device,
	.write		= stm32_console_write,
	.setup		= stm32_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &stm32_usart_driver,
};

#define STM32_SERIAL_CONSOLE (&stm32_console)

#else
#define STM32_SERIAL_CONSOLE NULL
#endif /* CONFIG_SERIAL_STM32_CONSOLE */

static struct uart_driver stm32_usart_driver = {
	.driver_name	= DRIVER_NAME,
	.dev_name	= STM32_SERIAL_NAME,
	.major		= 0,
	.minor		= 0,
	.nr		= STM32_MAX_PORTS,
	.cons		= STM32_SERIAL_CONSOLE,
};

static struct platform_driver stm32_serial_driver = {
	.probe		= stm32_serial_probe,
	.remove		= stm32_serial_remove,
	.driver	= {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(stm32_match),
	},
};

static int __init usart_init(void)
{
	static char banner[] __initdata = "STM32 USART driver initialized";
	int ret;

	pr_info("%s\n", banner);

	ret = uart_register_driver(&stm32_usart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&stm32_serial_driver);
	if (ret)
		uart_unregister_driver(&stm32_usart_driver);

	return ret;
}

static void __exit usart_exit(void)
{
	platform_driver_unregister(&stm32_serial_driver);
	uart_unregister_driver(&stm32_usart_driver);
}

module_init(usart_init);
module_exit(usart_exit);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_DESCRIPTION("STMicroelectronics STM32 serial port driver");
MODULE_LICENSE("GPL v2");
