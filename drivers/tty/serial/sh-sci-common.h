/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SH_SCI_COMMON_H__
#define __SH_SCI_COMMON_H__

#include <linux/serial_core.h>

/* Private port IDs */
enum SCI_PORT_TYPE {
	SCI_PORT_RSCI = BIT(7) | 0,
};

enum SCI_CLKS {
	SCI_FCK,		/* Functional Clock */
	SCI_SCK,		/* Optional External Clock */
	SCI_BRG_INT,		/* Optional BRG Internal Clock Source */
	SCI_SCIF_CLK,		/* Optional BRG External Clock Source */
	SCI_NUM_CLKS
};

/* Offsets into the sci_port->irqs array */
enum {
	SCIx_ERI_IRQ,
	SCIx_RXI_IRQ,
	SCIx_TXI_IRQ,
	SCIx_BRI_IRQ,
	SCIx_DRI_IRQ,
	SCIx_TEI_IRQ,
	SCIx_NR_IRQS,

	SCIx_MUX_IRQ = SCIx_NR_IRQS,	/* special case */
};

/* Bit x set means sampling rate x + 1 is supported */
#define SCI_SR(x)		BIT((x) - 1)
#define SCI_SR_RANGE(x, y)	GENMASK((y) - 1, (x) - 1)

void sci_release_port(struct uart_port *port);
int sci_request_port(struct uart_port *port);
void sci_config_port(struct uart_port *port, int flags);
int sci_verify_port(struct uart_port *port, struct serial_struct *ser);
void sci_pm(struct uart_port *port, unsigned int state,
		   unsigned int oldstate);

struct plat_sci_reg {
	u8 offset;
	u8 size;
};

struct sci_port_params_bits {
	unsigned int rxtx_enable;
	unsigned int te_clear;
	unsigned int poll_sent_bits;
};

struct sci_common_regs {
	unsigned int status;
	unsigned int control;
};

/* The actual number of needed registers. This is used by sci only */
#define SCI_NR_REGS 20

struct sci_port_params {
	const struct plat_sci_reg regs[SCI_NR_REGS];
	const struct sci_common_regs *common_regs;
	const struct sci_port_params_bits *param_bits;
	unsigned int fifosize;
	unsigned int overrun_reg;
	unsigned int overrun_mask;
	unsigned int sampling_rate_mask;
	unsigned int error_mask;
	unsigned int error_clear;
};

struct sci_port_ops {
	u32 (*read_reg)(struct uart_port *port, int reg);
	void (*write_reg)(struct uart_port *port, int reg, int value);
	void (*clear_SCxSR)(struct uart_port *port, unsigned int mask);

	void (*transmit_chars)(struct uart_port *port);
	void (*receive_chars)(struct uart_port *port);

	void (*poll_put_char)(struct uart_port *port, unsigned char c);

	int (*set_rtrg)(struct uart_port *port, int rx_trig);
	int (*rtrg_enabled)(struct uart_port *port);

	void (*shutdown_complete)(struct uart_port *port);

	void (*prepare_console_write)(struct uart_port *port, u32 ctrl);
	void (*console_save)(struct uart_port *port);
	void (*console_restore)(struct uart_port *port);
	size_t (*suspend_regs_size)(void);
};

struct sci_of_data {
	const struct sci_port_params *params;
	const struct uart_ops *uart_ops;
	const struct sci_port_ops *ops;
	unsigned short regtype;
	unsigned short type;
};

struct sci_port {
	struct uart_port	port;

	/* Platform configuration */
	const struct sci_port_params *params;
	const struct plat_sci_port *cfg;

	unsigned int		sampling_rate_mask;
	resource_size_t		reg_size;
	struct mctrl_gpios	*gpios;

	/* Clocks */
	struct clk		*clks[SCI_NUM_CLKS];
	unsigned long		clk_rates[SCI_NUM_CLKS];

	int			irqs[SCIx_NR_IRQS];
	char			*irqstr[SCIx_NR_IRQS];

	struct dma_chan			*chan_tx;
	struct dma_chan			*chan_rx;

	struct reset_control		*rstc;
	struct sci_suspend_regs		*suspend_regs;

#ifdef CONFIG_SERIAL_SH_SCI_DMA
	struct dma_chan			*chan_tx_saved;
	struct dma_chan			*chan_rx_saved;
	dma_cookie_t			cookie_tx;
	dma_cookie_t			cookie_rx[2];
	dma_cookie_t			active_rx;
	dma_addr_t			tx_dma_addr;
	unsigned int			tx_dma_len;
	struct scatterlist		sg_rx[2];
	void				*rx_buf[2];
	size_t				buf_len_rx;
	struct work_struct		work_tx;
	struct hrtimer			rx_timer;
	unsigned int			rx_timeout;	/* microseconds */
#endif
	unsigned int			rx_frame;
	int				rx_trigger;
	struct timer_list		rx_fifo_timer;
	int				rx_fifo_timeout;
	u16				hscif_tot;

	u8				type;
	u8				regtype;

	const struct sci_port_ops *ops;

	bool has_rtscts;
	bool autorts;
	bool tx_occurred;
};

#define to_sci_port(uart) container_of((uart), struct sci_port, port)

void sci_port_disable(struct sci_port *sci_port);
void sci_port_enable(struct sci_port *sci_port);

int sci_startup(struct uart_port *port);
void sci_shutdown(struct uart_port *port);

#define min_sr(_port)		ffs((_port)->sampling_rate_mask)
#define max_sr(_port)		fls((_port)->sampling_rate_mask)

#ifdef CONFIG_SERIAL_SH_SCI_EARLYCON
int __init scix_early_console_setup(struct earlycon_device *device, const struct sci_of_data *data);
#endif

#endif /* __SH_SCI_COMMON_H__ */
