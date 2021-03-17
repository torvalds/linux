// SPDX-License-Identifier: GPL-2.0+
/* Synopsys DesignWare 8250 library. */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>

#include "8250_dwlib.h"

/* Offsets for the DesignWare specific registers */
#define DW_UART_DLF	0xc0 /* Divisor Latch Fraction Register */
#define DW_UART_CPR	0xf4 /* Component Parameter Register */
#define DW_UART_UCV	0xf8 /* UART Component Version */

/* Component Parameter Register bits */
#define DW_UART_CPR_ABP_DATA_WIDTH	(3 << 0)
#define DW_UART_CPR_AFCE_MODE		(1 << 4)
#define DW_UART_CPR_THRE_MODE		(1 << 5)
#define DW_UART_CPR_SIR_MODE		(1 << 6)
#define DW_UART_CPR_SIR_LP_MODE		(1 << 7)
#define DW_UART_CPR_ADDITIONAL_FEATURES	(1 << 8)
#define DW_UART_CPR_FIFO_ACCESS		(1 << 9)
#define DW_UART_CPR_FIFO_STAT		(1 << 10)
#define DW_UART_CPR_SHADOW		(1 << 11)
#define DW_UART_CPR_ENCODED_PARMS	(1 << 12)
#define DW_UART_CPR_DMA_EXTRA		(1 << 13)
#define DW_UART_CPR_FIFO_MODE		(0xff << 16)

/* Helper for FIFO size calculation */
#define DW_UART_CPR_FIFO_SIZE(a)	(((a >> 16) & 0xff) * 16)

static inline u32 dw8250_readl_ext(struct uart_port *p, int offset)
{
	if (p->iotype == UPIO_MEM32BE)
		return ioread32be(p->membase + offset);
	return readl(p->membase + offset);
}

static inline void dw8250_writel_ext(struct uart_port *p, int offset, u32 reg)
{
	if (p->iotype == UPIO_MEM32BE)
		iowrite32be(reg, p->membase + offset);
	else
		writel(reg, p->membase + offset);
}

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

void dw8250_setup_port(struct uart_port *p)
{
	struct uart_8250_port *up = up_to_u8250p(p);
	u32 reg;

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
		struct dw8250_port_data *d = p->private_data;

		d->dlf_size = fls(reg);
		p->get_divisor = dw8250_get_divisor;
		p->set_divisor = dw8250_set_divisor;
	}

	reg = dw8250_readl_ext(p, DW_UART_CPR);
	if (!reg)
		return;

	/* Select the type based on FIFO */
	if (reg & DW_UART_CPR_FIFO_MODE) {
		p->type = PORT_16550A;
		p->flags |= UPF_FIXED_TYPE;
		p->fifosize = DW_UART_CPR_FIFO_SIZE(reg);
		up->capabilities = UART_CAP_FIFO;
	}

	if (reg & DW_UART_CPR_AFCE_MODE)
		up->capabilities |= UART_CAP_AFE;

	if (reg & DW_UART_CPR_SIR_MODE)
		up->capabilities |= UART_CAP_IRDA;
}
EXPORT_SYMBOL_GPL(dw8250_setup_port);
