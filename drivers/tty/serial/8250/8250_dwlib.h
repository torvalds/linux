/* SPDX-License-Identifier: GPL-2.0+ */
/* Synopsys DesignWare 8250 library header file. */

#ifndef _SERIAL_8250_DWLIB_H_
#define _SERIAL_8250_DWLIB_H_

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/build_bug.h>
#include <linux/io.h>
#include <linux/types.h>

#include "8250.h"

/* Offsets for the DesignWare specific registers */
#define DW_UART_USR	0x1f /* UART Status Register */
#define DW_UART_DMASA	0xa8 /* DMA Software Ack */
#define DW_UART_TCR	0xac /* Transceiver Control Register (RS485) */
#define DW_UART_DE_EN	0xb0 /* Driver Output Enable Register */
#define DW_UART_RE_EN	0xb4 /* Receiver Output Enable Register */
#define DW_UART_DLF	0xc0 /* Divisor Latch Fraction Register */
#define DW_UART_RAR	0xc4 /* Receive Address Register */
#define DW_UART_TAR	0xc8 /* Transmit Address Register */
#define DW_UART_LCR_EXT	0xcc /* Line Extended Control Register */
#define DW_UART_CPR	0xf4 /* Component Parameter Register */
#define DW_UART_UCV	0xf8 /* UART Component Version */

/* Interrupt ID Register bits */
#define DW_UART_IIR_IID			GENMASK(3, 0)

/* Modem Control Register bits */
#define DW_UART_MCR_SIRE		BIT(6)

/* Line Status Register bits */
#define DW_UART_LSR_ADDR_RCVD		BIT(8)

/* UART Status Register bits */
#define DW_UART_USR_BUSY		BIT(0)

/* Transceiver Control Register bits */
#define DW_UART_TCR_RS485_EN		BIT(0)
#define DW_UART_TCR_RE_POL		BIT(1)
#define DW_UART_TCR_DE_POL		BIT(2)
#define DW_UART_TCR_XFER_MODE		GENMASK(4, 3)
#define DW_UART_TCR_XFER_MODE_DE_DURING_RE	FIELD_PREP(DW_UART_TCR_XFER_MODE, 0)
#define DW_UART_TCR_XFER_MODE_SW_DE_OR_RE	FIELD_PREP(DW_UART_TCR_XFER_MODE, 1)
#define DW_UART_TCR_XFER_MODE_DE_OR_RE		FIELD_PREP(DW_UART_TCR_XFER_MODE, 2)

/* Receive / Transmit Address Register bits */
#define DW_UART_ADDR_MASK		GENMASK(7, 0)

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

/* Helpers for FIFO size calculation */
#define DW_UART_CPR_FIFO_SIZE(a)	(FIELD_GET(DW_UART_CPR_FIFO_MODE, (a)) * 16)
#define DW_UART_CPR_FIFO_MODE_FROM_SIZE(size)			\
	(FIELD_PREP_CONST(DW_UART_CPR_FIFO_MODE,		\
			  BUILD_BUG_ON_ZERO((size) > 2048) +	\
			  BUILD_BUG_ON_ZERO((size) % 16) +	\
			  ((size) / 16)))

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

#endif /* _SERIAL_8250_DWLIB_H_ */
