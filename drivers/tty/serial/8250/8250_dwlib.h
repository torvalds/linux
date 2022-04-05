// SPDX-License-Identifier: GPL-2.0+
/* Synopsys DesignWare 8250 library header file. */

#include <linux/types.h>

#include "8250.h"

struct dw8250_port_data {
	/* Port properties */
	int			line;

	/* DMA operations */
	struct uart_8250_dma	dma;

	/* Hardware configuration */
	u8			dlf_size;
};

void dw8250_setup_port(struct uart_port *p);
