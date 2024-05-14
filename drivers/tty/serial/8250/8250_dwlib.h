/* SPDX-License-Identifier: GPL-2.0+ */
/* Synopsys DesignWare 8250 library header file. */

#include <linux/io.h>
#include <linux/types.h>

#include "8250.h"

struct dw8250_port_data {
	/* Port properties */
	int			line;

	/* DMA operations */
	struct uart_8250_dma	dma;

	/* Hardware configuration */
	u32			cpr_value;
	u8			dlf_size;

	/* RS485 variables */
	bool			hw_rs485_support;
};

void dw8250_do_set_termios(struct uart_port *p, struct ktermios *termios, const struct ktermios *old);
void dw8250_setup_port(struct uart_port *p);

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
