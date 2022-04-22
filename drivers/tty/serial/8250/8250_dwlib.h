/* SPDX-License-Identifier: GPL-2.0+ */
/* Synopsys DesignWare 8250 library header file. */

#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "8250.h"

struct clk;
struct reset_control;

struct dw8250_port_data {
	/* Port properties */
	int			line;

	/* DMA operations */
	struct uart_8250_dma	dma;

	/* Hardware configuration */
	u8			dlf_size;
};

struct dw8250_platform_data {
	u8 usr_reg;
	u32 cpr_val;
	unsigned int quirks;
};

struct dw8250_data {
	struct dw8250_port_data	data;
	const struct dw8250_platform_data *pdata;

	int			msr_mask_on;
	int			msr_mask_off;
	struct clk		*clk;
	struct clk		*pclk;
	struct notifier_block	clk_notifier;
	struct work_struct	clk_work;
	struct reset_control	*rst;

	unsigned int		skip_autocfg:1;
	unsigned int		uart_16550_compatible:1;
};

void dw8250_do_set_termios(struct uart_port *p, struct ktermios *termios, struct ktermios *old);
void dw8250_setup_port(struct uart_port *p);

static inline struct dw8250_data *to_dw8250_data(struct dw8250_port_data *data)
{
	return container_of(data, struct dw8250_data, data);
}
