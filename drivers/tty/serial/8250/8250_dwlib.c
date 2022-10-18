// SPDX-License-Identifier: GPL-2.0+
/* Synopsys DesignWare 8250 library. */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/property.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>

#include "8250_dwlib.h"

/* Offsets for the DesignWare specific registers */
#define DW_UART_TCR	0xac /* Transceiver Control Register (RS485) */
#define DW_UART_DE_EN	0xb0 /* Driver Output Enable Register */
#define DW_UART_RE_EN	0xb4 /* Receiver Output Enable Register */
#define DW_UART_DLF	0xc0 /* Divisor Latch Fraction Register */
#define DW_UART_RAR	0xc4 /* Receive Address Register */
#define DW_UART_TAR	0xc8 /* Transmit Address Register */
#define DW_UART_LCR_EXT	0xcc /* Line Extended Control Register */
#define DW_UART_CPR	0xf4 /* Component Parameter Register */
#define DW_UART_UCV	0xf8 /* UART Component Version */

/* Receive / Transmit Address Register bits */
#define DW_UART_ADDR_MASK		GENMASK(7, 0)

/* Line Status Register bits */
#define DW_UART_LSR_ADDR_RCVD		BIT(8)

/* Transceiver Control Register bits */
#define DW_UART_TCR_RS485_EN		BIT(0)
#define DW_UART_TCR_RE_POL		BIT(1)
#define DW_UART_TCR_DE_POL		BIT(2)
#define DW_UART_TCR_XFER_MODE		GENMASK(4, 3)
#define DW_UART_TCR_XFER_MODE_DE_DURING_RE	FIELD_PREP(DW_UART_TCR_XFER_MODE, 0)
#define DW_UART_TCR_XFER_MODE_SW_DE_OR_RE	FIELD_PREP(DW_UART_TCR_XFER_MODE, 1)
#define DW_UART_TCR_XFER_MODE_DE_OR_RE		FIELD_PREP(DW_UART_TCR_XFER_MODE, 2)

/* Line Extended Control Register bits */
#define DW_UART_LCR_EXT_DLS_E		BIT(0)
#define DW_UART_LCR_EXT_ADDR_MATCH	BIT(1)
#define DW_UART_LCR_EXT_SEND_ADDR	BIT(2)
#define DW_UART_LCR_EXT_TRANSMIT_MODE	BIT(3)

/* Component Parameter Register bits */
#define DW_UART_CPR_ABP_DATA_WIDTH	GENMASK(1, 0)
#define DW_UART_CPR_AFCE_MODE		BIT(4)
#define DW_UART_CPR_THRE_MODE		BIT(5)
#define DW_UART_CPR_SIR_MODE		BIT(6)
#define DW_UART_CPR_SIR_LP_MODE		BIT(7)
#define DW_UART_CPR_ADDITIONAL_FEATURES	BIT(8)
#define DW_UART_CPR_FIFO_ACCESS		BIT(9)
#define DW_UART_CPR_FIFO_STAT		BIT(10)
#define DW_UART_CPR_SHADOW		BIT(11)
#define DW_UART_CPR_ENCODED_PARMS	BIT(12)
#define DW_UART_CPR_DMA_EXTRA		BIT(13)
#define DW_UART_CPR_FIFO_MODE		GENMASK(23, 16)

/* Helper for FIFO size calculation */
#define DW_UART_CPR_FIFO_SIZE(a)	(FIELD_GET(DW_UART_CPR_FIFO_MODE, (a)) * 16)

/*
 * divisor = div(I) + div(F)
 * "I" means integer, "F" means fractional
 * quot = div(I) = clk / (16 * baud)
 * frac = div(F) * 2^dlf_size
 *
 * let rem = clk % (16 * baud)
 * we have: div(F) * (16 * baud) = rem
 * so frac = 2^dlf_size * rem / (16 * baud) = (rem << dlf_size) / (16 * baud)
 */
static unsigned int dw8250_get_divisor(struct uart_port *p, unsigned int baud,
				       unsigned int *frac)
{
	unsigned int quot, rem, base_baud = baud * 16;
	struct dw8250_port_data *d = p->private_data;

	quot = p->uartclk / base_baud;
	rem = p->uartclk % base_baud;
	*frac = DIV_ROUND_CLOSEST(rem << d->dlf_size, base_baud);

	return quot;
}

static void dw8250_set_divisor(struct uart_port *p, unsigned int baud,
			       unsigned int quot, unsigned int quot_frac)
{
	dw8250_writel_ext(p, DW_UART_DLF, quot_frac);
	serial8250_do_set_divisor(p, baud, quot, quot_frac);
}

void dw8250_do_set_termios(struct uart_port *p, struct ktermios *termios,
			   const struct ktermios *old)
{
	p->status &= ~UPSTAT_AUTOCTS;
	if (termios->c_cflag & CRTSCTS)
		p->status |= UPSTAT_AUTOCTS;

	serial8250_do_set_termios(p, termios, old);

	/* Filter addresses which have 9th bit set */
	p->ignore_status_mask |= DW_UART_LSR_ADDR_RCVD;
	p->read_status_mask |= DW_UART_LSR_ADDR_RCVD;
}
EXPORT_SYMBOL_GPL(dw8250_do_set_termios);

/*
 * Wait until re is de-asserted for sure. An ongoing receive will keep
 * re asserted until end of frame. Without BUSY indication available,
 * only available course of action is to wait for the time it takes to
 * receive one frame (there might nothing to receive but w/o BUSY the
 * driver cannot know).
 */
static void dw8250_wait_re_deassert(struct uart_port *p)
{
	ndelay(p->frame_time);
}

static void dw8250_update_rar(struct uart_port *p, u32 addr)
{
	u32 re_en = dw8250_readl_ext(p, DW_UART_RE_EN);

	/*
	 * RAR shouldn't be changed while receiving. Thus, de-assert RE_EN
	 * if asserted and wait.
	 */
	if (re_en)
		dw8250_writel_ext(p, DW_UART_RE_EN, 0);
	dw8250_wait_re_deassert(p);
	dw8250_writel_ext(p, DW_UART_RAR, addr);
	if (re_en)
		dw8250_writel_ext(p, DW_UART_RE_EN, re_en);
}

static void dw8250_rs485_set_addr(struct uart_port *p, struct serial_rs485 *rs485,
				  struct ktermios *termios)
{
	u32 lcr = dw8250_readl_ext(p, DW_UART_LCR_EXT);

	if (rs485->flags & SER_RS485_ADDRB) {
		lcr |= DW_UART_LCR_EXT_DLS_E;
		if (termios)
			termios->c_cflag |= ADDRB;

		if (rs485->flags & SER_RS485_ADDR_RECV) {
			u32 delta = p->rs485.flags ^ rs485->flags;

			/*
			 * rs485 (param) is equal to uart_port's rs485 only during init
			 * (during init, delta is not yet applicable).
			 */
			if (unlikely(&p->rs485 == rs485))
				delta = rs485->flags;

			if ((delta & SER_RS485_ADDR_RECV) ||
			    (p->rs485.addr_recv != rs485->addr_recv))
				dw8250_update_rar(p, rs485->addr_recv);
			lcr |= DW_UART_LCR_EXT_ADDR_MATCH;
		} else {
			lcr &= ~DW_UART_LCR_EXT_ADDR_MATCH;
		}
		if (rs485->flags & SER_RS485_ADDR_DEST) {
			/*
			 * Don't skip writes here as another endpoint could
			 * have changed communication line's destination
			 * address in between.
			 */
			dw8250_writel_ext(p, DW_UART_TAR, rs485->addr_dest);
			lcr |= DW_UART_LCR_EXT_SEND_ADDR;
		}
	} else {
		lcr = 0;
	}
	dw8250_writel_ext(p, DW_UART_LCR_EXT, lcr);
}

static int dw8250_rs485_config(struct uart_port *p, struct ktermios *termios,
			       struct serial_rs485 *rs485)
{
	u32 tcr;

	tcr = dw8250_readl_ext(p, DW_UART_TCR);
	tcr &= ~DW_UART_TCR_XFER_MODE;

	if (rs485->flags & SER_RS485_ENABLED) {
		tcr |= DW_UART_TCR_RS485_EN;

		if (rs485->flags & SER_RS485_RX_DURING_TX)
			tcr |= DW_UART_TCR_XFER_MODE_DE_DURING_RE;
		else
			tcr |= DW_UART_TCR_XFER_MODE_DE_OR_RE;
		dw8250_writel_ext(p, DW_UART_DE_EN, 1);
		dw8250_writel_ext(p, DW_UART_RE_EN, 1);
	} else {
		if (termios)
			termios->c_cflag &= ~ADDRB;

		tcr &= ~DW_UART_TCR_RS485_EN;
	}

	/* Reset to default polarity */
	tcr |= DW_UART_TCR_DE_POL;
	tcr &= ~DW_UART_TCR_RE_POL;

	if (!(rs485->flags & SER_RS485_RTS_ON_SEND))
		tcr &= ~DW_UART_TCR_DE_POL;
	if (device_property_read_bool(p->dev, "rs485-rx-active-high"))
		tcr |= DW_UART_TCR_RE_POL;

	dw8250_writel_ext(p, DW_UART_TCR, tcr);

	/* Addressing mode can only be set up after TCR */
	if (rs485->flags & SER_RS485_ENABLED)
		dw8250_rs485_set_addr(p, rs485, termios);

	return 0;
}

/*
 * Tests if RE_EN register can have non-zero value to see if RS-485 HW support
 * is present.
 */
static bool dw8250_detect_rs485_hw(struct uart_port *p)
{
	u32 reg;

	dw8250_writel_ext(p, DW_UART_RE_EN, 1);
	reg = dw8250_readl_ext(p, DW_UART_RE_EN);
	dw8250_writel_ext(p, DW_UART_RE_EN, 0);
	return reg;
}

static const struct serial_rs485 dw8250_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RX_DURING_TX | SER_RS485_RTS_ON_SEND |
		 SER_RS485_RTS_AFTER_SEND | SER_RS485_ADDRB | SER_RS485_ADDR_RECV |
		 SER_RS485_ADDR_DEST,
};

void dw8250_setup_port(struct uart_port *p)
{
	struct dw8250_port_data *pd = p->private_data;
	struct dw8250_data *data = to_dw8250_data(pd);
	struct uart_8250_port *up = up_to_u8250p(p);
	u32 reg;

	pd->hw_rs485_support = dw8250_detect_rs485_hw(p);
	if (pd->hw_rs485_support) {
		p->rs485_config = dw8250_rs485_config;
		up->lsr_save_mask = LSR_SAVE_FLAGS | DW_UART_LSR_ADDR_RCVD;
		p->rs485_supported = dw8250_rs485_supported;
	} else {
		p->rs485_config = serial8250_em485_config;
		p->rs485_supported = serial8250_em485_supported;
		up->rs485_start_tx = serial8250_em485_start_tx;
		up->rs485_stop_tx = serial8250_em485_stop_tx;
	}
	up->capabilities |= UART_CAP_NOTEMT;

	/*
	 * If the Component Version Register returns zero, we know that
	 * ADDITIONAL_FEATURES are not enabled. No need to go any further.
	 */
	reg = dw8250_readl_ext(p, DW_UART_UCV);
	if (!reg)
		return;

	dev_dbg(p->dev, "Designware UART version %c.%c%c\n",
		(reg >> 24) & 0xff, (reg >> 16) & 0xff, (reg >> 8) & 0xff);

	dw8250_writel_ext(p, DW_UART_DLF, ~0U);
	reg = dw8250_readl_ext(p, DW_UART_DLF);
	dw8250_writel_ext(p, DW_UART_DLF, 0);

	if (reg) {
		pd->dlf_size = fls(reg);
		p->get_divisor = dw8250_get_divisor;
		p->set_divisor = dw8250_set_divisor;
	}

	reg = dw8250_readl_ext(p, DW_UART_CPR);
	if (!reg) {
		reg = data->pdata->cpr_val;
		dev_dbg(p->dev, "CPR is not available, using 0x%08x instead\n", reg);
	}
	if (!reg)
		return;

	/* Select the type based on FIFO */
	if (reg & DW_UART_CPR_FIFO_MODE) {
		p->type = PORT_16550A;
		p->flags |= UPF_FIXED_TYPE;
		p->fifosize = DW_UART_CPR_FIFO_SIZE(reg);
		up->capabilities = UART_CAP_FIFO | UART_CAP_NOTEMT;
	}

	if (reg & DW_UART_CPR_AFCE_MODE)
		up->capabilities |= UART_CAP_AFE;

	if (reg & DW_UART_CPR_SIR_MODE)
		up->capabilities |= UART_CAP_IRDA;
}
EXPORT_SYMBOL_GPL(dw8250_setup_port);
