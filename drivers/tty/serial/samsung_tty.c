// SPDX-License-Identifier: GPL-2.0
/*
 * Driver core for Samsung SoC onboard UARTs.
 *
 * Ben Dooks, Copyright (c) 2003-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 */

/* Note on 2410 error handling
 *
 * The s3c2410 manual has a love/hate affair with the contents of the
 * UERSTAT register in the UART blocks, and keeps marking some of the
 * error bits as reserved. Having checked with the s3c2410x01,
 * it copes with BREAKs properly, so I am happy to ignore the RESERVED
 * feature from the latter versions of the manual.
 *
 * If it becomes aparrent that latter versions of the 2410 remove these
 * bits, then action will have to be taken to differentiate the versions
 * and change the policy on BREAK
 *
 * BJD, 04-Nov-2004
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/serial_s3c.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/of.h>
#include <asm/irq.h>

/* UART name and device definitions */

#define S3C24XX_SERIAL_NAME	"ttySAC"
#define S3C24XX_SERIAL_MAJOR	204
#define S3C24XX_SERIAL_MINOR	64

#ifdef CONFIG_ARM64
#define UART_NR			12
#else
#define UART_NR			CONFIG_SERIAL_SAMSUNG_UARTS
#endif

#define S3C24XX_TX_PIO			1
#define S3C24XX_TX_DMA			2
#define S3C24XX_RX_PIO			1
#define S3C24XX_RX_DMA			2

/* flag to ignore all characters coming in */
#define RXSTAT_DUMMY_READ (0x10000000)

enum s3c24xx_port_type {
	TYPE_S3C24XX,
	TYPE_S3C6400,
	TYPE_APPLE_S5L,
};

struct s3c24xx_uart_info {
	const char		*name;
	enum s3c24xx_port_type	type;
	unsigned int		port_type;
	unsigned int		fifosize;
	unsigned long		rx_fifomask;
	unsigned long		rx_fifoshift;
	unsigned long		rx_fifofull;
	unsigned long		tx_fifomask;
	unsigned long		tx_fifoshift;
	unsigned long		tx_fifofull;
	unsigned int		def_clk_sel;
	unsigned long		num_clks;
	unsigned long		clksel_mask;
	unsigned long		clksel_shift;
	unsigned long		ucon_mask;

	/* uart port features */

	unsigned int		has_divslot:1;
};

struct s3c24xx_serial_drv_data {
	const struct s3c24xx_uart_info	info;
	const struct s3c2410_uartcfg	def_cfg;
	const unsigned int		fifosize[UART_NR];
};

struct s3c24xx_uart_dma {
	unsigned int			rx_chan_id;
	unsigned int			tx_chan_id;

	struct dma_slave_config		rx_conf;
	struct dma_slave_config		tx_conf;

	struct dma_chan			*rx_chan;
	struct dma_chan			*tx_chan;

	dma_addr_t			rx_addr;
	dma_addr_t			tx_addr;

	dma_cookie_t			rx_cookie;
	dma_cookie_t			tx_cookie;

	char				*rx_buf;

	dma_addr_t			tx_transfer_addr;

	size_t				rx_size;
	size_t				tx_size;

	struct dma_async_tx_descriptor	*tx_desc;
	struct dma_async_tx_descriptor	*rx_desc;

	int				tx_bytes_requested;
	int				rx_bytes_requested;
};

struct s3c24xx_uart_port {
	unsigned char			rx_claimed;
	unsigned char			tx_claimed;
	unsigned char			rx_enabled;
	unsigned char			tx_enabled;
	unsigned int			pm_level;
	unsigned long			baudclk_rate;
	unsigned int			min_dma_size;

	unsigned int			rx_irq;
	unsigned int			tx_irq;

	unsigned int			tx_in_progress;
	unsigned int			tx_mode;
	unsigned int			rx_mode;

	const struct s3c24xx_uart_info	*info;
	struct clk			*clk;
	struct clk			*baudclk;
	struct uart_port		port;
	const struct s3c24xx_serial_drv_data	*drv_data;

	/* reference to platform data */
	const struct s3c2410_uartcfg	*cfg;

	struct s3c24xx_uart_dma		*dma;
};

static void s3c24xx_serial_tx_chars(struct s3c24xx_uart_port *ourport);

/* conversion functions */

#define s3c24xx_dev_to_port(__dev) dev_get_drvdata(__dev)

/* register access controls */

#define portaddr(port, reg) ((port)->membase + (reg))
#define portaddrl(port, reg) \
	((unsigned long *)(unsigned long)((port)->membase + (reg)))

static u32 rd_reg(const struct uart_port *port, u32 reg)
{
	switch (port->iotype) {
	case UPIO_MEM:
		return readb_relaxed(portaddr(port, reg));
	case UPIO_MEM32:
		return readl_relaxed(portaddr(port, reg));
	default:
		return 0;
	}
	return 0;
}

#define rd_regl(port, reg) (readl_relaxed(portaddr(port, reg)))

static void wr_reg(const struct uart_port *port, u32 reg, u32 val)
{
	switch (port->iotype) {
	case UPIO_MEM:
		writeb_relaxed(val, portaddr(port, reg));
		break;
	case UPIO_MEM32:
		writel_relaxed(val, portaddr(port, reg));
		break;
	}
}

#define wr_regl(port, reg, val) writel_relaxed(val, portaddr(port, reg))

/* Byte-order aware bit setting/clearing functions. */

static inline void s3c24xx_set_bit(const struct uart_port *port, int idx,
				   unsigned int reg)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = rd_regl(port, reg);
	val |= (1 << idx);
	wr_regl(port, reg, val);
	local_irq_restore(flags);
}

static inline void s3c24xx_clear_bit(const struct uart_port *port, int idx,
				     unsigned int reg)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = rd_regl(port, reg);
	val &= ~(1 << idx);
	wr_regl(port, reg, val);
	local_irq_restore(flags);
}

static inline struct s3c24xx_uart_port *to_ourport(struct uart_port *port)
{
	return container_of(port, struct s3c24xx_uart_port, port);
}

/* translate a port to the device name */

static inline const char *s3c24xx_serial_portname(const struct uart_port *port)
{
	return to_platform_device(port->dev)->name;
}

static int s3c24xx_serial_txempty_nofifo(const struct uart_port *port)
{
	return rd_regl(port, S3C2410_UTRSTAT) & S3C2410_UTRSTAT_TXE;
}

static void s3c24xx_serial_rx_enable(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	unsigned long flags;
	unsigned int ucon, ufcon;
	int count = 10000;

	spin_lock_irqsave(&port->lock, flags);

	while (--count && !s3c24xx_serial_txempty_nofifo(port))
		udelay(100);

	ufcon = rd_regl(port, S3C2410_UFCON);
	ufcon |= S3C2410_UFCON_RESETRX;
	wr_regl(port, S3C2410_UFCON, ufcon);

	ucon = rd_regl(port, S3C2410_UCON);
	ucon |= S3C2410_UCON_RXIRQMODE;
	wr_regl(port, S3C2410_UCON, ucon);

	ourport->rx_enabled = 1;
	spin_unlock_irqrestore(&port->lock, flags);
}

static void s3c24xx_serial_rx_disable(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	unsigned long flags;
	unsigned int ucon;

	spin_lock_irqsave(&port->lock, flags);

	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= ~S3C2410_UCON_RXIRQMODE;
	wr_regl(port, S3C2410_UCON, ucon);

	ourport->rx_enabled = 0;
	spin_unlock_irqrestore(&port->lock, flags);
}

static void s3c24xx_serial_stop_tx(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	struct s3c24xx_uart_dma *dma = ourport->dma;
	struct dma_tx_state state;
	int count;

	if (!ourport->tx_enabled)
		return;

	switch (ourport->info->type) {
	case TYPE_S3C6400:
		s3c24xx_set_bit(port, S3C64XX_UINTM_TXD, S3C64XX_UINTM);
		break;
	case TYPE_APPLE_S5L:
		s3c24xx_clear_bit(port, APPLE_S5L_UCON_TXTHRESH_ENA, S3C2410_UCON);
		break;
	default:
		disable_irq_nosync(ourport->tx_irq);
		break;
	}

	if (dma && dma->tx_chan && ourport->tx_in_progress == S3C24XX_TX_DMA) {
		dmaengine_pause(dma->tx_chan);
		dmaengine_tx_status(dma->tx_chan, dma->tx_cookie, &state);
		dmaengine_terminate_all(dma->tx_chan);
		dma_sync_single_for_cpu(dma->tx_chan->device->dev,
					dma->tx_transfer_addr, dma->tx_size,
					DMA_TO_DEVICE);
		async_tx_ack(dma->tx_desc);
		count = dma->tx_bytes_requested - state.residue;
		uart_xmit_advance(port, count);
	}

	ourport->tx_enabled = 0;
	ourport->tx_in_progress = 0;

	if (port->flags & UPF_CONS_FLOW)
		s3c24xx_serial_rx_enable(port);

	ourport->tx_mode = 0;
}

static void s3c24xx_serial_start_next_tx(struct s3c24xx_uart_port *ourport);

static void s3c24xx_serial_tx_dma_complete(void *args)
{
	struct s3c24xx_uart_port *ourport = args;
	struct uart_port *port = &ourport->port;
	struct circ_buf *xmit = &port->state->xmit;
	struct s3c24xx_uart_dma *dma = ourport->dma;
	struct dma_tx_state state;
	unsigned long flags;
	int count;

	dmaengine_tx_status(dma->tx_chan, dma->tx_cookie, &state);
	count = dma->tx_bytes_requested - state.residue;
	async_tx_ack(dma->tx_desc);

	dma_sync_single_for_cpu(dma->tx_chan->device->dev,
				dma->tx_transfer_addr, dma->tx_size,
				DMA_TO_DEVICE);

	spin_lock_irqsave(&port->lock, flags);

	uart_xmit_advance(port, count);
	ourport->tx_in_progress = 0;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	s3c24xx_serial_start_next_tx(ourport);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void enable_tx_dma(struct s3c24xx_uart_port *ourport)
{
	const struct uart_port *port = &ourport->port;
	u32 ucon;

	/* Mask Tx interrupt */
	switch (ourport->info->type) {
	case TYPE_S3C6400:
		s3c24xx_set_bit(port, S3C64XX_UINTM_TXD, S3C64XX_UINTM);
		break;
	case TYPE_APPLE_S5L:
		WARN_ON(1); // No DMA
		break;
	default:
		disable_irq_nosync(ourport->tx_irq);
		break;
	}

	/* Enable tx dma mode */
	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= ~(S3C64XX_UCON_TXBURST_MASK | S3C64XX_UCON_TXMODE_MASK);
	ucon |= S3C64XX_UCON_TXBURST_1;
	ucon |= S3C64XX_UCON_TXMODE_DMA;
	wr_regl(port,  S3C2410_UCON, ucon);

	ourport->tx_mode = S3C24XX_TX_DMA;
}

static void enable_tx_pio(struct s3c24xx_uart_port *ourport)
{
	const struct uart_port *port = &ourport->port;
	u32 ucon, ufcon;

	/* Set ufcon txtrig */
	ourport->tx_in_progress = S3C24XX_TX_PIO;
	ufcon = rd_regl(port, S3C2410_UFCON);
	wr_regl(port,  S3C2410_UFCON, ufcon);

	/* Enable tx pio mode */
	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= ~(S3C64XX_UCON_TXMODE_MASK);
	ucon |= S3C64XX_UCON_TXMODE_CPU;
	wr_regl(port,  S3C2410_UCON, ucon);

	/* Unmask Tx interrupt */
	switch (ourport->info->type) {
	case TYPE_S3C6400:
		s3c24xx_clear_bit(port, S3C64XX_UINTM_TXD,
				  S3C64XX_UINTM);
		break;
	case TYPE_APPLE_S5L:
		ucon |= APPLE_S5L_UCON_TXTHRESH_ENA_MSK;
		wr_regl(port, S3C2410_UCON, ucon);
		break;
	default:
		enable_irq(ourport->tx_irq);
		break;
	}

	ourport->tx_mode = S3C24XX_TX_PIO;

	/*
	 * The Apple version only has edge triggered TX IRQs, so we need
	 * to kick off the process by sending some characters here.
	 */
	if (ourport->info->type == TYPE_APPLE_S5L)
		s3c24xx_serial_tx_chars(ourport);
}

static void s3c24xx_serial_start_tx_pio(struct s3c24xx_uart_port *ourport)
{
	if (ourport->tx_mode != S3C24XX_TX_PIO)
		enable_tx_pio(ourport);
}

static int s3c24xx_serial_start_tx_dma(struct s3c24xx_uart_port *ourport,
				      unsigned int count)
{
	struct uart_port *port = &ourport->port;
	struct circ_buf *xmit = &port->state->xmit;
	struct s3c24xx_uart_dma *dma = ourport->dma;

	if (ourport->tx_mode != S3C24XX_TX_DMA)
		enable_tx_dma(ourport);

	dma->tx_size = count & ~(dma_get_cache_alignment() - 1);
	dma->tx_transfer_addr = dma->tx_addr + xmit->tail;

	dma_sync_single_for_device(dma->tx_chan->device->dev,
				   dma->tx_transfer_addr, dma->tx_size,
				   DMA_TO_DEVICE);

	dma->tx_desc = dmaengine_prep_slave_single(dma->tx_chan,
				dma->tx_transfer_addr, dma->tx_size,
				DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
	if (!dma->tx_desc) {
		dev_err(ourport->port.dev, "Unable to get desc for Tx\n");
		return -EIO;
	}

	dma->tx_desc->callback = s3c24xx_serial_tx_dma_complete;
	dma->tx_desc->callback_param = ourport;
	dma->tx_bytes_requested = dma->tx_size;

	ourport->tx_in_progress = S3C24XX_TX_DMA;
	dma->tx_cookie = dmaengine_submit(dma->tx_desc);
	dma_async_issue_pending(dma->tx_chan);
	return 0;
}

static void s3c24xx_serial_start_next_tx(struct s3c24xx_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long count;

	/* Get data size up to the end of buffer */
	count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);

	if (!count) {
		s3c24xx_serial_stop_tx(port);
		return;
	}

	if (!ourport->dma || !ourport->dma->tx_chan ||
	    count < ourport->min_dma_size ||
	    xmit->tail & (dma_get_cache_alignment() - 1))
		s3c24xx_serial_start_tx_pio(ourport);
	else
		s3c24xx_serial_start_tx_dma(ourport, count);
}

static void s3c24xx_serial_start_tx(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	struct circ_buf *xmit = &port->state->xmit;

	if (!ourport->tx_enabled) {
		if (port->flags & UPF_CONS_FLOW)
			s3c24xx_serial_rx_disable(port);

		ourport->tx_enabled = 1;
		if (!ourport->dma || !ourport->dma->tx_chan)
			s3c24xx_serial_start_tx_pio(ourport);
	}

	if (ourport->dma && ourport->dma->tx_chan) {
		if (!uart_circ_empty(xmit) && !ourport->tx_in_progress)
			s3c24xx_serial_start_next_tx(ourport);
	}
}

static void s3c24xx_uart_copy_rx_to_tty(struct s3c24xx_uart_port *ourport,
		struct tty_port *tty, int count)
{
	struct s3c24xx_uart_dma *dma = ourport->dma;
	int copied;

	if (!count)
		return;

	dma_sync_single_for_cpu(dma->rx_chan->device->dev, dma->rx_addr,
				dma->rx_size, DMA_FROM_DEVICE);

	ourport->port.icount.rx += count;
	if (!tty) {
		dev_err(ourport->port.dev, "No tty port\n");
		return;
	}
	copied = tty_insert_flip_string(tty,
			((unsigned char *)(ourport->dma->rx_buf)), count);
	if (copied != count) {
		WARN_ON(1);
		dev_err(ourport->port.dev, "RxData copy to tty layer failed\n");
	}
}

static void s3c24xx_serial_stop_rx(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	struct s3c24xx_uart_dma *dma = ourport->dma;
	struct tty_port *t = &port->state->port;
	struct dma_tx_state state;
	enum dma_status dma_status;
	unsigned int received;

	if (ourport->rx_enabled) {
		dev_dbg(port->dev, "stopping rx\n");
		switch (ourport->info->type) {
		case TYPE_S3C6400:
			s3c24xx_set_bit(port, S3C64XX_UINTM_RXD,
					S3C64XX_UINTM);
			break;
		case TYPE_APPLE_S5L:
			s3c24xx_clear_bit(port, APPLE_S5L_UCON_RXTHRESH_ENA, S3C2410_UCON);
			s3c24xx_clear_bit(port, APPLE_S5L_UCON_RXTO_ENA, S3C2410_UCON);
			break;
		default:
			disable_irq_nosync(ourport->rx_irq);
			break;
		}
		ourport->rx_enabled = 0;
	}
	if (dma && dma->rx_chan) {
		dmaengine_pause(dma->tx_chan);
		dma_status = dmaengine_tx_status(dma->rx_chan,
				dma->rx_cookie, &state);
		if (dma_status == DMA_IN_PROGRESS ||
			dma_status == DMA_PAUSED) {
			received = dma->rx_bytes_requested - state.residue;
			dmaengine_terminate_all(dma->rx_chan);
			s3c24xx_uart_copy_rx_to_tty(ourport, t, received);
		}
	}
}

static inline const struct s3c24xx_uart_info
	*s3c24xx_port_to_info(struct uart_port *port)
{
	return to_ourport(port)->info;
}

static inline const struct s3c2410_uartcfg
	*s3c24xx_port_to_cfg(const struct uart_port *port)
{
	const struct s3c24xx_uart_port *ourport;

	if (port->dev == NULL)
		return NULL;

	ourport = container_of(port, struct s3c24xx_uart_port, port);
	return ourport->cfg;
}

static int s3c24xx_serial_rx_fifocnt(const struct s3c24xx_uart_port *ourport,
				     unsigned long ufstat)
{
	const struct s3c24xx_uart_info *info = ourport->info;

	if (ufstat & info->rx_fifofull)
		return ourport->port.fifosize;

	return (ufstat & info->rx_fifomask) >> info->rx_fifoshift;
}

static void s3c64xx_start_rx_dma(struct s3c24xx_uart_port *ourport);
static void s3c24xx_serial_rx_dma_complete(void *args)
{
	struct s3c24xx_uart_port *ourport = args;
	struct uart_port *port = &ourport->port;

	struct s3c24xx_uart_dma *dma = ourport->dma;
	struct tty_port *t = &port->state->port;
	struct tty_struct *tty = tty_port_tty_get(&ourport->port.state->port);

	struct dma_tx_state state;
	unsigned long flags;
	int received;

	dmaengine_tx_status(dma->rx_chan,  dma->rx_cookie, &state);
	received  = dma->rx_bytes_requested - state.residue;
	async_tx_ack(dma->rx_desc);

	spin_lock_irqsave(&port->lock, flags);

	if (received)
		s3c24xx_uart_copy_rx_to_tty(ourport, t, received);

	if (tty) {
		tty_flip_buffer_push(t);
		tty_kref_put(tty);
	}

	s3c64xx_start_rx_dma(ourport);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void s3c64xx_start_rx_dma(struct s3c24xx_uart_port *ourport)
{
	struct s3c24xx_uart_dma *dma = ourport->dma;

	dma_sync_single_for_device(dma->rx_chan->device->dev, dma->rx_addr,
				   dma->rx_size, DMA_FROM_DEVICE);

	dma->rx_desc = dmaengine_prep_slave_single(dma->rx_chan,
				dma->rx_addr, dma->rx_size, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT);
	if (!dma->rx_desc) {
		dev_err(ourport->port.dev, "Unable to get desc for Rx\n");
		return;
	}

	dma->rx_desc->callback = s3c24xx_serial_rx_dma_complete;
	dma->rx_desc->callback_param = ourport;
	dma->rx_bytes_requested = dma->rx_size;

	dma->rx_cookie = dmaengine_submit(dma->rx_desc);
	dma_async_issue_pending(dma->rx_chan);
}

/* ? - where has parity gone?? */
#define S3C2410_UERSTAT_PARITY (0x1000)

static void enable_rx_dma(struct s3c24xx_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	unsigned int ucon;

	/* set Rx mode to DMA mode */
	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= ~(S3C64XX_UCON_RXBURST_MASK |
			S3C64XX_UCON_TIMEOUT_MASK |
			S3C64XX_UCON_EMPTYINT_EN |
			S3C64XX_UCON_DMASUS_EN |
			S3C64XX_UCON_TIMEOUT_EN |
			S3C64XX_UCON_RXMODE_MASK);
	ucon |= S3C64XX_UCON_RXBURST_1 |
			0xf << S3C64XX_UCON_TIMEOUT_SHIFT |
			S3C64XX_UCON_EMPTYINT_EN |
			S3C64XX_UCON_TIMEOUT_EN |
			S3C64XX_UCON_RXMODE_DMA;
	wr_regl(port, S3C2410_UCON, ucon);

	ourport->rx_mode = S3C24XX_RX_DMA;
}

static void enable_rx_pio(struct s3c24xx_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	unsigned int ucon;

	/* set Rx mode to DMA mode */
	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= ~S3C64XX_UCON_RXMODE_MASK;
	ucon |= S3C64XX_UCON_RXMODE_CPU;

	/* Apple types use these bits for IRQ masks */
	if (ourport->info->type != TYPE_APPLE_S5L) {
		ucon &= ~(S3C64XX_UCON_TIMEOUT_MASK |
				S3C64XX_UCON_EMPTYINT_EN |
				S3C64XX_UCON_DMASUS_EN |
				S3C64XX_UCON_TIMEOUT_EN);
		ucon |= 0xf << S3C64XX_UCON_TIMEOUT_SHIFT |
				S3C64XX_UCON_TIMEOUT_EN;
	}
	wr_regl(port, S3C2410_UCON, ucon);

	ourport->rx_mode = S3C24XX_RX_PIO;
}

static void s3c24xx_serial_rx_drain_fifo(struct s3c24xx_uart_port *ourport);

static irqreturn_t s3c24xx_serial_rx_chars_dma(void *dev_id)
{
	unsigned int utrstat, received;
	struct s3c24xx_uart_port *ourport = dev_id;
	struct uart_port *port = &ourport->port;
	struct s3c24xx_uart_dma *dma = ourport->dma;
	struct tty_struct *tty = tty_port_tty_get(&ourport->port.state->port);
	struct tty_port *t = &port->state->port;
	struct dma_tx_state state;

	utrstat = rd_regl(port, S3C2410_UTRSTAT);
	rd_regl(port, S3C2410_UFSTAT);

	spin_lock(&port->lock);

	if (!(utrstat & S3C2410_UTRSTAT_TIMEOUT)) {
		s3c64xx_start_rx_dma(ourport);
		if (ourport->rx_mode == S3C24XX_RX_PIO)
			enable_rx_dma(ourport);
		goto finish;
	}

	if (ourport->rx_mode == S3C24XX_RX_DMA) {
		dmaengine_pause(dma->rx_chan);
		dmaengine_tx_status(dma->rx_chan, dma->rx_cookie, &state);
		dmaengine_terminate_all(dma->rx_chan);
		received = dma->rx_bytes_requested - state.residue;
		s3c24xx_uart_copy_rx_to_tty(ourport, t, received);

		enable_rx_pio(ourport);
	}

	s3c24xx_serial_rx_drain_fifo(ourport);

	if (tty) {
		tty_flip_buffer_push(t);
		tty_kref_put(tty);
	}

	wr_regl(port, S3C2410_UTRSTAT, S3C2410_UTRSTAT_TIMEOUT);

finish:
	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static void s3c24xx_serial_rx_drain_fifo(struct s3c24xx_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	unsigned int ufcon, ch, flag, ufstat, uerstat;
	unsigned int fifocnt = 0;
	int max_count = port->fifosize;

	while (max_count-- > 0) {
		/*
		 * Receive all characters known to be in FIFO
		 * before reading FIFO level again
		 */
		if (fifocnt == 0) {
			ufstat = rd_regl(port, S3C2410_UFSTAT);
			fifocnt = s3c24xx_serial_rx_fifocnt(ourport, ufstat);
			if (fifocnt == 0)
				break;
		}
		fifocnt--;

		uerstat = rd_regl(port, S3C2410_UERSTAT);
		ch = rd_reg(port, S3C2410_URXH);

		if (port->flags & UPF_CONS_FLOW) {
			int txe = s3c24xx_serial_txempty_nofifo(port);

			if (ourport->rx_enabled) {
				if (!txe) {
					ourport->rx_enabled = 0;
					continue;
				}
			} else {
				if (txe) {
					ufcon = rd_regl(port, S3C2410_UFCON);
					ufcon |= S3C2410_UFCON_RESETRX;
					wr_regl(port, S3C2410_UFCON, ufcon);
					ourport->rx_enabled = 1;
					return;
				}
				continue;
			}
		}

		/* insert the character into the buffer */

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(uerstat & S3C2410_UERSTAT_ANY)) {
			dev_dbg(port->dev,
				"rxerr: port ch=0x%02x, rxs=0x%08x\n",
				ch, uerstat);

			/* check for break */
			if (uerstat & S3C2410_UERSTAT_BREAK) {
				dev_dbg(port->dev, "break!\n");
				port->icount.brk++;
				if (uart_handle_break(port))
					continue; /* Ignore character */
			}

			if (uerstat & S3C2410_UERSTAT_FRAME)
				port->icount.frame++;
			if (uerstat & S3C2410_UERSTAT_OVERRUN)
				port->icount.overrun++;

			uerstat &= port->read_status_mask;

			if (uerstat & S3C2410_UERSTAT_BREAK)
				flag = TTY_BREAK;
			else if (uerstat & S3C2410_UERSTAT_PARITY)
				flag = TTY_PARITY;
			else if (uerstat & (S3C2410_UERSTAT_FRAME |
					    S3C2410_UERSTAT_OVERRUN))
				flag = TTY_FRAME;
		}

		if (uart_handle_sysrq_char(port, ch))
			continue; /* Ignore character */

		uart_insert_char(port, uerstat, S3C2410_UERSTAT_OVERRUN,
				 ch, flag);
	}

	tty_flip_buffer_push(&port->state->port);
}

static irqreturn_t s3c24xx_serial_rx_chars_pio(void *dev_id)
{
	struct s3c24xx_uart_port *ourport = dev_id;
	struct uart_port *port = &ourport->port;

	spin_lock(&port->lock);
	s3c24xx_serial_rx_drain_fifo(ourport);
	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static irqreturn_t s3c24xx_serial_rx_irq(int irq, void *dev_id)
{
	struct s3c24xx_uart_port *ourport = dev_id;

	if (ourport->dma && ourport->dma->rx_chan)
		return s3c24xx_serial_rx_chars_dma(dev_id);
	return s3c24xx_serial_rx_chars_pio(dev_id);
}

static void s3c24xx_serial_tx_chars(struct s3c24xx_uart_port *ourport)
{
	struct uart_port *port = &ourport->port;
	struct circ_buf *xmit = &port->state->xmit;
	int count, dma_count = 0;

	count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);

	if (ourport->dma && ourport->dma->tx_chan &&
	    count >= ourport->min_dma_size) {
		int align = dma_get_cache_alignment() -
			(xmit->tail & (dma_get_cache_alignment() - 1));
		if (count - align >= ourport->min_dma_size) {
			dma_count = count - align;
			count = align;
		}
	}

	if (port->x_char) {
		wr_reg(port, S3C2410_UTXH, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	/* if there isn't anything more to transmit, or the uart is now
	 * stopped, disable the uart and exit
	 */

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		s3c24xx_serial_stop_tx(port);
		return;
	}

	/* try and drain the buffer... */

	if (count > port->fifosize) {
		count = port->fifosize;
		dma_count = 0;
	}

	while (!uart_circ_empty(xmit) && count > 0) {
		if (rd_regl(port, S3C2410_UFSTAT) & ourport->info->tx_fifofull)
			break;

		wr_reg(port, S3C2410_UTXH, xmit->buf[xmit->tail]);
		uart_xmit_advance(port, 1);
		count--;
	}

	if (!count && dma_count) {
		s3c24xx_serial_start_tx_dma(ourport, dma_count);
		return;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		s3c24xx_serial_stop_tx(port);
}

static irqreturn_t s3c24xx_serial_tx_irq(int irq, void *id)
{
	struct s3c24xx_uart_port *ourport = id;
	struct uart_port *port = &ourport->port;

	spin_lock(&port->lock);

	s3c24xx_serial_tx_chars(ourport);

	spin_unlock(&port->lock);
	return IRQ_HANDLED;
}

/* interrupt handler for s3c64xx and later SoC's.*/
static irqreturn_t s3c64xx_serial_handle_irq(int irq, void *id)
{
	const struct s3c24xx_uart_port *ourport = id;
	const struct uart_port *port = &ourport->port;
	unsigned int pend = rd_regl(port, S3C64XX_UINTP);
	irqreturn_t ret = IRQ_HANDLED;

	if (pend & S3C64XX_UINTM_RXD_MSK) {
		ret = s3c24xx_serial_rx_irq(irq, id);
		wr_regl(port, S3C64XX_UINTP, S3C64XX_UINTM_RXD_MSK);
	}
	if (pend & S3C64XX_UINTM_TXD_MSK) {
		ret = s3c24xx_serial_tx_irq(irq, id);
		wr_regl(port, S3C64XX_UINTP, S3C64XX_UINTM_TXD_MSK);
	}
	return ret;
}

/* interrupt handler for Apple SoC's.*/
static irqreturn_t apple_serial_handle_irq(int irq, void *id)
{
	const struct s3c24xx_uart_port *ourport = id;
	const struct uart_port *port = &ourport->port;
	unsigned int pend = rd_regl(port, S3C2410_UTRSTAT);
	irqreturn_t ret = IRQ_NONE;

	if (pend & (APPLE_S5L_UTRSTAT_RXTHRESH | APPLE_S5L_UTRSTAT_RXTO)) {
		wr_regl(port, S3C2410_UTRSTAT,
			APPLE_S5L_UTRSTAT_RXTHRESH | APPLE_S5L_UTRSTAT_RXTO);
		ret = s3c24xx_serial_rx_irq(irq, id);
	}
	if (pend & APPLE_S5L_UTRSTAT_TXTHRESH) {
		wr_regl(port, S3C2410_UTRSTAT, APPLE_S5L_UTRSTAT_TXTHRESH);
		ret = s3c24xx_serial_tx_irq(irq, id);
	}

	return ret;
}

static unsigned int s3c24xx_serial_tx_empty(struct uart_port *port)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);
	unsigned long ufstat = rd_regl(port, S3C2410_UFSTAT);
	unsigned long ufcon = rd_regl(port, S3C2410_UFCON);

	if (ufcon & S3C2410_UFCON_FIFOMODE) {
		if ((ufstat & info->tx_fifomask) != 0 ||
		    (ufstat & info->tx_fifofull))
			return 0;

		return 1;
	}

	return s3c24xx_serial_txempty_nofifo(port);
}

/* no modem control lines */
static unsigned int s3c24xx_serial_get_mctrl(struct uart_port *port)
{
	unsigned int umstat = rd_reg(port, S3C2410_UMSTAT);

	if (umstat & S3C2410_UMSTAT_CTS)
		return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
	else
		return TIOCM_CAR | TIOCM_DSR;
}

static void s3c24xx_serial_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned int umcon = rd_regl(port, S3C2410_UMCON);
	unsigned int ucon = rd_regl(port, S3C2410_UCON);

	if (mctrl & TIOCM_RTS)
		umcon |= S3C2410_UMCOM_RTS_LOW;
	else
		umcon &= ~S3C2410_UMCOM_RTS_LOW;

	wr_regl(port, S3C2410_UMCON, umcon);

	if (mctrl & TIOCM_LOOP)
		ucon |= S3C2410_UCON_LOOPBACK;
	else
		ucon &= ~S3C2410_UCON_LOOPBACK;

	wr_regl(port, S3C2410_UCON, ucon);
}

static void s3c24xx_serial_break_ctl(struct uart_port *port, int break_state)
{
	unsigned long flags;
	unsigned int ucon;

	spin_lock_irqsave(&port->lock, flags);

	ucon = rd_regl(port, S3C2410_UCON);

	if (break_state)
		ucon |= S3C2410_UCON_SBREAK;
	else
		ucon &= ~S3C2410_UCON_SBREAK;

	wr_regl(port, S3C2410_UCON, ucon);

	spin_unlock_irqrestore(&port->lock, flags);
}

static int s3c24xx_serial_request_dma(struct s3c24xx_uart_port *p)
{
	struct s3c24xx_uart_dma	*dma = p->dma;
	struct dma_slave_caps dma_caps;
	const char *reason = NULL;
	int ret;

	/* Default slave configuration parameters */
	dma->rx_conf.direction		= DMA_DEV_TO_MEM;
	dma->rx_conf.src_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma->rx_conf.src_addr		= p->port.mapbase + S3C2410_URXH;
	dma->rx_conf.src_maxburst	= 1;

	dma->tx_conf.direction		= DMA_MEM_TO_DEV;
	dma->tx_conf.dst_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma->tx_conf.dst_addr		= p->port.mapbase + S3C2410_UTXH;
	dma->tx_conf.dst_maxburst	= 1;

	dma->rx_chan = dma_request_chan(p->port.dev, "rx");

	if (IS_ERR(dma->rx_chan)) {
		reason = "DMA RX channel request failed";
		ret = PTR_ERR(dma->rx_chan);
		goto err_warn;
	}

	ret = dma_get_slave_caps(dma->rx_chan, &dma_caps);
	if (ret < 0 ||
	    dma_caps.residue_granularity < DMA_RESIDUE_GRANULARITY_BURST) {
		reason = "insufficient DMA RX engine capabilities";
		ret = -EOPNOTSUPP;
		goto err_release_rx;
	}

	dmaengine_slave_config(dma->rx_chan, &dma->rx_conf);

	dma->tx_chan = dma_request_chan(p->port.dev, "tx");
	if (IS_ERR(dma->tx_chan)) {
		reason = "DMA TX channel request failed";
		ret = PTR_ERR(dma->tx_chan);
		goto err_release_rx;
	}

	ret = dma_get_slave_caps(dma->tx_chan, &dma_caps);
	if (ret < 0 ||
	    dma_caps.residue_granularity < DMA_RESIDUE_GRANULARITY_BURST) {
		reason = "insufficient DMA TX engine capabilities";
		ret = -EOPNOTSUPP;
		goto err_release_tx;
	}

	dmaengine_slave_config(dma->tx_chan, &dma->tx_conf);

	/* RX buffer */
	dma->rx_size = PAGE_SIZE;

	dma->rx_buf = kmalloc(dma->rx_size, GFP_KERNEL);
	if (!dma->rx_buf) {
		ret = -ENOMEM;
		goto err_release_tx;
	}

	dma->rx_addr = dma_map_single(dma->rx_chan->device->dev, dma->rx_buf,
				      dma->rx_size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dma->rx_chan->device->dev, dma->rx_addr)) {
		reason = "DMA mapping error for RX buffer";
		ret = -EIO;
		goto err_free_rx;
	}

	/* TX buffer */
	dma->tx_addr = dma_map_single(dma->tx_chan->device->dev,
				      p->port.state->xmit.buf, UART_XMIT_SIZE,
				      DMA_TO_DEVICE);
	if (dma_mapping_error(dma->tx_chan->device->dev, dma->tx_addr)) {
		reason = "DMA mapping error for TX buffer";
		ret = -EIO;
		goto err_unmap_rx;
	}

	return 0;

err_unmap_rx:
	dma_unmap_single(dma->rx_chan->device->dev, dma->rx_addr,
			 dma->rx_size, DMA_FROM_DEVICE);
err_free_rx:
	kfree(dma->rx_buf);
err_release_tx:
	dma_release_channel(dma->tx_chan);
err_release_rx:
	dma_release_channel(dma->rx_chan);
err_warn:
	if (reason)
		dev_warn(p->port.dev, "%s, DMA will not be used\n", reason);
	return ret;
}

static void s3c24xx_serial_release_dma(struct s3c24xx_uart_port *p)
{
	struct s3c24xx_uart_dma	*dma = p->dma;

	if (dma->rx_chan) {
		dmaengine_terminate_all(dma->rx_chan);
		dma_unmap_single(dma->rx_chan->device->dev, dma->rx_addr,
				 dma->rx_size, DMA_FROM_DEVICE);
		kfree(dma->rx_buf);
		dma_release_channel(dma->rx_chan);
		dma->rx_chan = NULL;
	}

	if (dma->tx_chan) {
		dmaengine_terminate_all(dma->tx_chan);
		dma_unmap_single(dma->tx_chan->device->dev, dma->tx_addr,
				 UART_XMIT_SIZE, DMA_TO_DEVICE);
		dma_release_channel(dma->tx_chan);
		dma->tx_chan = NULL;
	}
}

static void s3c24xx_serial_shutdown(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);

	if (ourport->tx_claimed) {
		free_irq(ourport->tx_irq, ourport);
		ourport->tx_enabled = 0;
		ourport->tx_claimed = 0;
		ourport->tx_mode = 0;
	}

	if (ourport->rx_claimed) {
		free_irq(ourport->rx_irq, ourport);
		ourport->rx_claimed = 0;
		ourport->rx_enabled = 0;
	}

	if (ourport->dma)
		s3c24xx_serial_release_dma(ourport);

	ourport->tx_in_progress = 0;
}

static void s3c64xx_serial_shutdown(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);

	ourport->tx_enabled = 0;
	ourport->tx_mode = 0;
	ourport->rx_enabled = 0;

	free_irq(port->irq, ourport);

	wr_regl(port, S3C64XX_UINTP, 0xf);
	wr_regl(port, S3C64XX_UINTM, 0xf);

	if (ourport->dma)
		s3c24xx_serial_release_dma(ourport);

	ourport->tx_in_progress = 0;
}

static void apple_s5l_serial_shutdown(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);

	unsigned int ucon;

	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= ~(APPLE_S5L_UCON_TXTHRESH_ENA_MSK |
		  APPLE_S5L_UCON_RXTHRESH_ENA_MSK |
		  APPLE_S5L_UCON_RXTO_ENA_MSK);
	wr_regl(port, S3C2410_UCON, ucon);

	wr_regl(port, S3C2410_UTRSTAT, APPLE_S5L_UTRSTAT_ALL_FLAGS);

	free_irq(port->irq, ourport);

	ourport->tx_enabled = 0;
	ourport->tx_mode = 0;
	ourport->rx_enabled = 0;

	if (ourport->dma)
		s3c24xx_serial_release_dma(ourport);

	ourport->tx_in_progress = 0;
}

static int s3c24xx_serial_startup(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	int ret;

	ourport->rx_enabled = 1;

	ret = request_irq(ourport->rx_irq, s3c24xx_serial_rx_irq, 0,
			  s3c24xx_serial_portname(port), ourport);

	if (ret != 0) {
		dev_err(port->dev, "cannot get irq %d\n", ourport->rx_irq);
		return ret;
	}

	ourport->rx_claimed = 1;

	dev_dbg(port->dev, "requesting tx irq...\n");

	ourport->tx_enabled = 1;

	ret = request_irq(ourport->tx_irq, s3c24xx_serial_tx_irq, 0,
			  s3c24xx_serial_portname(port), ourport);

	if (ret) {
		dev_err(port->dev, "cannot get irq %d\n", ourport->tx_irq);
		goto err;
	}

	ourport->tx_claimed = 1;

	/* the port reset code should have done the correct
	 * register setup for the port controls
	 */

	return ret;

err:
	s3c24xx_serial_shutdown(port);
	return ret;
}

static int s3c64xx_serial_startup(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	unsigned long flags;
	unsigned int ufcon;
	int ret;

	wr_regl(port, S3C64XX_UINTM, 0xf);
	if (ourport->dma) {
		ret = s3c24xx_serial_request_dma(ourport);
		if (ret < 0) {
			devm_kfree(port->dev, ourport->dma);
			ourport->dma = NULL;
		}
	}

	ret = request_irq(port->irq, s3c64xx_serial_handle_irq, IRQF_SHARED,
			  s3c24xx_serial_portname(port), ourport);
	if (ret) {
		dev_err(port->dev, "cannot get irq %d\n", port->irq);
		return ret;
	}

	/* For compatibility with s3c24xx Soc's */
	ourport->rx_enabled = 1;
	ourport->tx_enabled = 0;

	spin_lock_irqsave(&port->lock, flags);

	ufcon = rd_regl(port, S3C2410_UFCON);
	ufcon |= S3C2410_UFCON_RESETRX | S5PV210_UFCON_RXTRIG8;
	if (!uart_console(port))
		ufcon |= S3C2410_UFCON_RESETTX;
	wr_regl(port, S3C2410_UFCON, ufcon);

	enable_rx_pio(ourport);

	spin_unlock_irqrestore(&port->lock, flags);

	/* Enable Rx Interrupt */
	s3c24xx_clear_bit(port, S3C64XX_UINTM_RXD, S3C64XX_UINTM);

	return ret;
}

static int apple_s5l_serial_startup(struct uart_port *port)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	unsigned long flags;
	unsigned int ufcon;
	int ret;

	wr_regl(port, S3C2410_UTRSTAT, APPLE_S5L_UTRSTAT_ALL_FLAGS);

	ret = request_irq(port->irq, apple_serial_handle_irq, 0,
			  s3c24xx_serial_portname(port), ourport);
	if (ret) {
		dev_err(port->dev, "cannot get irq %d\n", port->irq);
		return ret;
	}

	/* For compatibility with s3c24xx Soc's */
	ourport->rx_enabled = 1;
	ourport->tx_enabled = 0;

	spin_lock_irqsave(&port->lock, flags);

	ufcon = rd_regl(port, S3C2410_UFCON);
	ufcon |= S3C2410_UFCON_RESETRX | S5PV210_UFCON_RXTRIG8;
	if (!uart_console(port))
		ufcon |= S3C2410_UFCON_RESETTX;
	wr_regl(port, S3C2410_UFCON, ufcon);

	enable_rx_pio(ourport);

	spin_unlock_irqrestore(&port->lock, flags);

	/* Enable Rx Interrupt */
	s3c24xx_set_bit(port, APPLE_S5L_UCON_RXTHRESH_ENA, S3C2410_UCON);
	s3c24xx_set_bit(port, APPLE_S5L_UCON_RXTO_ENA, S3C2410_UCON);

	return ret;
}

/* power power management control */

static void s3c24xx_serial_pm(struct uart_port *port, unsigned int level,
			      unsigned int old)
{
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	int timeout = 10000;

	ourport->pm_level = level;

	switch (level) {
	case 3:
		while (--timeout && !s3c24xx_serial_txempty_nofifo(port))
			udelay(100);

		if (!IS_ERR(ourport->baudclk))
			clk_disable_unprepare(ourport->baudclk);

		clk_disable_unprepare(ourport->clk);
		break;

	case 0:
		clk_prepare_enable(ourport->clk);

		if (!IS_ERR(ourport->baudclk))
			clk_prepare_enable(ourport->baudclk);
		break;
	default:
		dev_err(port->dev, "s3c24xx_serial: unknown pm %d\n", level);
	}
}

/* baud rate calculation
 *
 * The UARTs on the S3C2410/S3C2440 can take their clocks from a number
 * of different sources, including the peripheral clock ("pclk") and an
 * external clock ("uclk"). The S3C2440 also adds the core clock ("fclk")
 * with a programmable extra divisor.
 *
 * The following code goes through the clock sources, and calculates the
 * baud clocks (and the resultant actual baud rates) and then tries to
 * pick the closest one and select that.
 *
 */

#define MAX_CLK_NAME_LENGTH 15

static inline int s3c24xx_serial_getsource(struct uart_port *port)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);
	unsigned int ucon;

	if (info->num_clks == 1)
		return 0;

	ucon = rd_regl(port, S3C2410_UCON);
	ucon &= info->clksel_mask;
	return ucon >> info->clksel_shift;
}

static void s3c24xx_serial_setsource(struct uart_port *port,
			unsigned int clk_sel)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);
	unsigned int ucon;

	if (info->num_clks == 1)
		return;

	ucon = rd_regl(port, S3C2410_UCON);
	if ((ucon & info->clksel_mask) >> info->clksel_shift == clk_sel)
		return;

	ucon &= ~info->clksel_mask;
	ucon |= clk_sel << info->clksel_shift;
	wr_regl(port, S3C2410_UCON, ucon);
}

static unsigned int s3c24xx_serial_getclk(struct s3c24xx_uart_port *ourport,
			unsigned int req_baud, struct clk **best_clk,
			unsigned int *clk_num)
{
	const struct s3c24xx_uart_info *info = ourport->info;
	struct clk *clk;
	unsigned long rate;
	unsigned int cnt, baud, quot, best_quot = 0;
	char clkname[MAX_CLK_NAME_LENGTH];
	int calc_deviation, deviation = (1 << 30) - 1;

	for (cnt = 0; cnt < info->num_clks; cnt++) {
		/* Keep selected clock if provided */
		if (ourport->cfg->clk_sel &&
			!(ourport->cfg->clk_sel & (1 << cnt)))
			continue;

		sprintf(clkname, "clk_uart_baud%d", cnt);
		clk = clk_get(ourport->port.dev, clkname);
		if (IS_ERR(clk))
			continue;

		rate = clk_get_rate(clk);
		if (!rate) {
			dev_err(ourport->port.dev,
				"Failed to get clock rate for %s.\n", clkname);
			clk_put(clk);
			continue;
		}

		if (ourport->info->has_divslot) {
			unsigned long div = rate / req_baud;

			/* The UDIVSLOT register on the newer UARTs allows us to
			 * get a divisor adjustment of 1/16th on the baud clock.
			 *
			 * We don't keep the UDIVSLOT value (the 16ths we
			 * calculated by not multiplying the baud by 16) as it
			 * is easy enough to recalculate.
			 */

			quot = div / 16;
			baud = rate / div;
		} else {
			quot = (rate + (8 * req_baud)) / (16 * req_baud);
			baud = rate / (quot * 16);
		}
		quot--;

		calc_deviation = abs(req_baud - baud);

		if (calc_deviation < deviation) {
			/*
			 * If we find a better clk, release the previous one, if
			 * any.
			 */
			if (!IS_ERR(*best_clk))
				clk_put(*best_clk);
			*best_clk = clk;
			best_quot = quot;
			*clk_num = cnt;
			deviation = calc_deviation;
		} else {
			clk_put(clk);
		}
	}

	return best_quot;
}

/* udivslot_table[]
 *
 * This table takes the fractional value of the baud divisor and gives
 * the recommended setting for the UDIVSLOT register.
 */
static const u16 udivslot_table[16] = {
	[0] = 0x0000,
	[1] = 0x0080,
	[2] = 0x0808,
	[3] = 0x0888,
	[4] = 0x2222,
	[5] = 0x4924,
	[6] = 0x4A52,
	[7] = 0x54AA,
	[8] = 0x5555,
	[9] = 0xD555,
	[10] = 0xD5D5,
	[11] = 0xDDD5,
	[12] = 0xDDDD,
	[13] = 0xDFDD,
	[14] = 0xDFDF,
	[15] = 0xFFDF,
};

static void s3c24xx_serial_set_termios(struct uart_port *port,
				       struct ktermios *termios,
				       const struct ktermios *old)
{
	const struct s3c2410_uartcfg *cfg = s3c24xx_port_to_cfg(port);
	struct s3c24xx_uart_port *ourport = to_ourport(port);
	struct clk *clk = ERR_PTR(-EINVAL);
	unsigned long flags;
	unsigned int baud, quot, clk_sel = 0;
	unsigned int ulcon;
	unsigned int umcon;
	unsigned int udivslot = 0;

	/*
	 * We don't support modem control lines.
	 */
	termios->c_cflag &= ~(HUPCL | CMSPAR);
	termios->c_cflag |= CLOCAL;

	/*
	 * Ask the core to calculate the divisor for us.
	 */

	baud = uart_get_baud_rate(port, termios, old, 0, 3000000);
	quot = s3c24xx_serial_getclk(ourport, baud, &clk, &clk_sel);
	if (baud == 38400 && (port->flags & UPF_SPD_MASK) == UPF_SPD_CUST)
		quot = port->custom_divisor;
	if (IS_ERR(clk))
		return;

	/* check to see if we need  to change clock source */

	if (ourport->baudclk != clk) {
		clk_prepare_enable(clk);

		s3c24xx_serial_setsource(port, clk_sel);

		if (!IS_ERR(ourport->baudclk)) {
			clk_disable_unprepare(ourport->baudclk);
			ourport->baudclk = ERR_PTR(-EINVAL);
		}

		ourport->baudclk = clk;
		ourport->baudclk_rate = clk ? clk_get_rate(clk) : 0;
	}

	if (ourport->info->has_divslot) {
		unsigned int div = ourport->baudclk_rate / baud;

		if (cfg->has_fracval) {
			udivslot = (div & 15);
			dev_dbg(port->dev, "fracval = %04x\n", udivslot);
		} else {
			udivslot = udivslot_table[div & 15];
			dev_dbg(port->dev, "udivslot = %04x (div %d)\n",
				udivslot, div & 15);
		}
	}

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		dev_dbg(port->dev, "config: 5bits/char\n");
		ulcon = S3C2410_LCON_CS5;
		break;
	case CS6:
		dev_dbg(port->dev, "config: 6bits/char\n");
		ulcon = S3C2410_LCON_CS6;
		break;
	case CS7:
		dev_dbg(port->dev, "config: 7bits/char\n");
		ulcon = S3C2410_LCON_CS7;
		break;
	case CS8:
	default:
		dev_dbg(port->dev, "config: 8bits/char\n");
		ulcon = S3C2410_LCON_CS8;
		break;
	}

	/* preserve original lcon IR settings */
	ulcon |= (cfg->ulcon & S3C2410_LCON_IRM);

	if (termios->c_cflag & CSTOPB)
		ulcon |= S3C2410_LCON_STOPB;

	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & PARODD)
			ulcon |= S3C2410_LCON_PODD;
		else
			ulcon |= S3C2410_LCON_PEVEN;
	} else {
		ulcon |= S3C2410_LCON_PNONE;
	}

	spin_lock_irqsave(&port->lock, flags);

	dev_dbg(port->dev,
		"setting ulcon to %08x, brddiv to %d, udivslot %08x\n",
		ulcon, quot, udivslot);

	wr_regl(port, S3C2410_ULCON, ulcon);
	wr_regl(port, S3C2410_UBRDIV, quot);

	port->status &= ~UPSTAT_AUTOCTS;

	umcon = rd_regl(port, S3C2410_UMCON);
	if (termios->c_cflag & CRTSCTS) {
		umcon |= S3C2410_UMCOM_AFC;
		/* Disable RTS when RX FIFO contains 63 bytes */
		umcon &= ~S3C2412_UMCON_AFC_8;
		port->status = UPSTAT_AUTOCTS;
	} else {
		umcon &= ~S3C2410_UMCOM_AFC;
	}
	wr_regl(port, S3C2410_UMCON, umcon);

	if (ourport->info->has_divslot)
		wr_regl(port, S3C2443_DIVSLOT, udivslot);

	dev_dbg(port->dev,
		"uart: ulcon = 0x%08x, ucon = 0x%08x, ufcon = 0x%08x\n",
		rd_regl(port, S3C2410_ULCON),
		rd_regl(port, S3C2410_UCON),
		rd_regl(port, S3C2410_UFCON));

	/*
	 * Update the per-port timeout.
	 */
	uart_update_timeout(port, termios->c_cflag, baud);

	/*
	 * Which character status flags are we interested in?
	 */
	port->read_status_mask = S3C2410_UERSTAT_OVERRUN;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= S3C2410_UERSTAT_FRAME |
			S3C2410_UERSTAT_PARITY;
	/*
	 * Which character status flags should we ignore?
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= S3C2410_UERSTAT_OVERRUN;
	if (termios->c_iflag & IGNBRK && termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= S3C2410_UERSTAT_FRAME;

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= RXSTAT_DUMMY_READ;

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *s3c24xx_serial_type(struct uart_port *port)
{
	const struct s3c24xx_uart_port *ourport = to_ourport(port);

	switch (ourport->info->type) {
	case TYPE_S3C24XX:
		return "S3C24XX";
	case TYPE_S3C6400:
		return "S3C6400/10";
	case TYPE_APPLE_S5L:
		return "APPLE S5L";
	default:
		return NULL;
	}
}

static void s3c24xx_serial_config_port(struct uart_port *port, int flags)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);

	if (flags & UART_CONFIG_TYPE)
		port->type = info->port_type;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int
s3c24xx_serial_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);

	if (ser->type != PORT_UNKNOWN && ser->type != info->port_type)
		return -EINVAL;

	return 0;
}

#ifdef CONFIG_SERIAL_SAMSUNG_CONSOLE

static struct console s3c24xx_serial_console;

static void __init s3c24xx_serial_register_console(void)
{
	register_console(&s3c24xx_serial_console);
}

static void s3c24xx_serial_unregister_console(void)
{
	if (console_is_registered(&s3c24xx_serial_console))
		unregister_console(&s3c24xx_serial_console);
}

#define S3C24XX_SERIAL_CONSOLE &s3c24xx_serial_console
#else
static inline void s3c24xx_serial_register_console(void) { }
static inline void s3c24xx_serial_unregister_console(void) { }
#define S3C24XX_SERIAL_CONSOLE NULL
#endif

#if defined(CONFIG_SERIAL_SAMSUNG_CONSOLE) && defined(CONFIG_CONSOLE_POLL)
static int s3c24xx_serial_get_poll_char(struct uart_port *port);
static void s3c24xx_serial_put_poll_char(struct uart_port *port,
			 unsigned char c);
#endif

static const struct uart_ops s3c24xx_serial_ops = {
	.pm		= s3c24xx_serial_pm,
	.tx_empty	= s3c24xx_serial_tx_empty,
	.get_mctrl	= s3c24xx_serial_get_mctrl,
	.set_mctrl	= s3c24xx_serial_set_mctrl,
	.stop_tx	= s3c24xx_serial_stop_tx,
	.start_tx	= s3c24xx_serial_start_tx,
	.stop_rx	= s3c24xx_serial_stop_rx,
	.break_ctl	= s3c24xx_serial_break_ctl,
	.startup	= s3c24xx_serial_startup,
	.shutdown	= s3c24xx_serial_shutdown,
	.set_termios	= s3c24xx_serial_set_termios,
	.type		= s3c24xx_serial_type,
	.config_port	= s3c24xx_serial_config_port,
	.verify_port	= s3c24xx_serial_verify_port,
#if defined(CONFIG_SERIAL_SAMSUNG_CONSOLE) && defined(CONFIG_CONSOLE_POLL)
	.poll_get_char = s3c24xx_serial_get_poll_char,
	.poll_put_char = s3c24xx_serial_put_poll_char,
#endif
};

static const struct uart_ops s3c64xx_serial_ops = {
	.pm		= s3c24xx_serial_pm,
	.tx_empty	= s3c24xx_serial_tx_empty,
	.get_mctrl	= s3c24xx_serial_get_mctrl,
	.set_mctrl	= s3c24xx_serial_set_mctrl,
	.stop_tx	= s3c24xx_serial_stop_tx,
	.start_tx	= s3c24xx_serial_start_tx,
	.stop_rx	= s3c24xx_serial_stop_rx,
	.break_ctl	= s3c24xx_serial_break_ctl,
	.startup	= s3c64xx_serial_startup,
	.shutdown	= s3c64xx_serial_shutdown,
	.set_termios	= s3c24xx_serial_set_termios,
	.type		= s3c24xx_serial_type,
	.config_port	= s3c24xx_serial_config_port,
	.verify_port	= s3c24xx_serial_verify_port,
#if defined(CONFIG_SERIAL_SAMSUNG_CONSOLE) && defined(CONFIG_CONSOLE_POLL)
	.poll_get_char = s3c24xx_serial_get_poll_char,
	.poll_put_char = s3c24xx_serial_put_poll_char,
#endif
};

static const struct uart_ops apple_s5l_serial_ops = {
	.pm		= s3c24xx_serial_pm,
	.tx_empty	= s3c24xx_serial_tx_empty,
	.get_mctrl	= s3c24xx_serial_get_mctrl,
	.set_mctrl	= s3c24xx_serial_set_mctrl,
	.stop_tx	= s3c24xx_serial_stop_tx,
	.start_tx	= s3c24xx_serial_start_tx,
	.stop_rx	= s3c24xx_serial_stop_rx,
	.break_ctl	= s3c24xx_serial_break_ctl,
	.startup	= apple_s5l_serial_startup,
	.shutdown	= apple_s5l_serial_shutdown,
	.set_termios	= s3c24xx_serial_set_termios,
	.type		= s3c24xx_serial_type,
	.config_port	= s3c24xx_serial_config_port,
	.verify_port	= s3c24xx_serial_verify_port,
#if defined(CONFIG_SERIAL_SAMSUNG_CONSOLE) && defined(CONFIG_CONSOLE_POLL)
	.poll_get_char = s3c24xx_serial_get_poll_char,
	.poll_put_char = s3c24xx_serial_put_poll_char,
#endif
};

static struct uart_driver s3c24xx_uart_drv = {
	.owner		= THIS_MODULE,
	.driver_name	= "s3c2410_serial",
	.nr		= UART_NR,
	.cons		= S3C24XX_SERIAL_CONSOLE,
	.dev_name	= S3C24XX_SERIAL_NAME,
	.major		= S3C24XX_SERIAL_MAJOR,
	.minor		= S3C24XX_SERIAL_MINOR,
};

static struct s3c24xx_uart_port s3c24xx_serial_ports[UART_NR];

static void s3c24xx_serial_init_port_default(int index) {
	struct uart_port *port = &s3c24xx_serial_ports[index].port;

	spin_lock_init(&port->lock);

	port->iotype = UPIO_MEM;
	port->uartclk = 0;
	port->fifosize = 16;
	port->ops = &s3c24xx_serial_ops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->line = index;
}

/* s3c24xx_serial_resetport
 *
 * reset the fifos and other the settings.
 */

static void s3c24xx_serial_resetport(struct uart_port *port,
				     const struct s3c2410_uartcfg *cfg)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);
	unsigned long ucon = rd_regl(port, S3C2410_UCON);

	ucon &= (info->clksel_mask | info->ucon_mask);
	wr_regl(port, S3C2410_UCON, ucon | cfg->ucon);

	/* reset both fifos */
	wr_regl(port, S3C2410_UFCON, cfg->ufcon | S3C2410_UFCON_RESETBOTH);
	wr_regl(port, S3C2410_UFCON, cfg->ufcon);

	/* some delay is required after fifo reset */
	udelay(1);
}

static int s3c24xx_serial_enable_baudclk(struct s3c24xx_uart_port *ourport)
{
	struct device *dev = ourport->port.dev;
	const struct s3c24xx_uart_info *info = ourport->info;
	char clk_name[MAX_CLK_NAME_LENGTH];
	unsigned int clk_sel;
	struct clk *clk;
	int clk_num;
	int ret;

	clk_sel = ourport->cfg->clk_sel ? : info->def_clk_sel;
	for (clk_num = 0; clk_num < info->num_clks; clk_num++) {
		if (!(clk_sel & (1 << clk_num)))
			continue;

		sprintf(clk_name, "clk_uart_baud%d", clk_num);
		clk = clk_get(dev, clk_name);
		if (IS_ERR(clk))
			continue;

		ret = clk_prepare_enable(clk);
		if (ret) {
			clk_put(clk);
			continue;
		}

		ourport->baudclk = clk;
		ourport->baudclk_rate = clk_get_rate(clk);
		s3c24xx_serial_setsource(&ourport->port, clk_num);

		return 0;
	}

	return -EINVAL;
}

/* s3c24xx_serial_init_port
 *
 * initialise a single serial port from the platform device given
 */

static int s3c24xx_serial_init_port(struct s3c24xx_uart_port *ourport,
				    struct platform_device *platdev)
{
	struct uart_port *port = &ourport->port;
	const struct s3c2410_uartcfg *cfg = ourport->cfg;
	struct resource *res;
	int ret;

	if (platdev == NULL)
		return -ENODEV;

	if (port->mapbase != 0)
		return -EINVAL;

	/* setup info for port */
	port->dev	= &platdev->dev;

	port->uartclk = 1;

	if (cfg->uart_flags & UPF_CONS_FLOW) {
		dev_dbg(port->dev, "enabling flow control\n");
		port->flags |= UPF_CONS_FLOW;
	}

	/* sort our the physical and virtual addresses for each UART */

	res = platform_get_resource(platdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(port->dev, "failed to find memory resource for uart\n");
		return -EINVAL;
	}

	dev_dbg(port->dev, "resource %pR)\n", res);

	port->membase = devm_ioremap_resource(port->dev, res);
	if (IS_ERR(port->membase)) {
		dev_err(port->dev, "failed to remap controller address\n");
		return -EBUSY;
	}

	port->mapbase = res->start;
	ret = platform_get_irq(platdev, 0);
	if (ret < 0) {
		port->irq = 0;
	} else {
		port->irq = ret;
		ourport->rx_irq = ret;
		ourport->tx_irq = ret + 1;
	}

	switch (ourport->info->type) {
	case TYPE_S3C24XX:
		ret = platform_get_irq(platdev, 1);
		if (ret > 0)
			ourport->tx_irq = ret;
		break;
	default:
		break;
	}

	/*
	 * DMA is currently supported only on DT platforms, if DMA properties
	 * are specified.
	 */
	if (platdev->dev.of_node && of_find_property(platdev->dev.of_node,
						     "dmas", NULL)) {
		ourport->dma = devm_kzalloc(port->dev,
					    sizeof(*ourport->dma),
					    GFP_KERNEL);
		if (!ourport->dma) {
			ret = -ENOMEM;
			goto err;
		}
	}

	ourport->clk	= clk_get(&platdev->dev, "uart");
	if (IS_ERR(ourport->clk)) {
		pr_err("%s: Controller clock not found\n",
				dev_name(&platdev->dev));
		ret = PTR_ERR(ourport->clk);
		goto err;
	}

	ret = clk_prepare_enable(ourport->clk);
	if (ret) {
		pr_err("uart: clock failed to prepare+enable: %d\n", ret);
		clk_put(ourport->clk);
		goto err;
	}

	ret = s3c24xx_serial_enable_baudclk(ourport);
	if (ret)
		pr_warn("uart: failed to enable baudclk\n");

	/* Keep all interrupts masked and cleared */
	switch (ourport->info->type) {
	case TYPE_S3C6400:
		wr_regl(port, S3C64XX_UINTM, 0xf);
		wr_regl(port, S3C64XX_UINTP, 0xf);
		wr_regl(port, S3C64XX_UINTSP, 0xf);
		break;
	case TYPE_APPLE_S5L: {
		unsigned int ucon;

		ucon = rd_regl(port, S3C2410_UCON);
		ucon &= ~(APPLE_S5L_UCON_TXTHRESH_ENA_MSK |
			APPLE_S5L_UCON_RXTHRESH_ENA_MSK |
			APPLE_S5L_UCON_RXTO_ENA_MSK);
		wr_regl(port, S3C2410_UCON, ucon);

		wr_regl(port, S3C2410_UTRSTAT, APPLE_S5L_UTRSTAT_ALL_FLAGS);
		break;
	}
	default:
		break;
	}

	dev_dbg(port->dev, "port: map=%pa, mem=%p, irq=%d (%d,%d), clock=%u\n",
		&port->mapbase, port->membase, port->irq,
		ourport->rx_irq, ourport->tx_irq, port->uartclk);

	/* reset the fifos (and setup the uart) */
	s3c24xx_serial_resetport(port, cfg);

	return 0;

err:
	port->mapbase = 0;
	return ret;
}

/* Device driver serial port probe */

static int probe_index;

static inline const struct s3c24xx_serial_drv_data *
s3c24xx_get_driver_data(struct platform_device *pdev)
{
	if (dev_of_node(&pdev->dev))
		return of_device_get_match_data(&pdev->dev);

	return (struct s3c24xx_serial_drv_data *)
			platform_get_device_id(pdev)->driver_data;
}

static int s3c24xx_serial_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct s3c24xx_uart_port *ourport;
	int index = probe_index;
	int ret, prop = 0;

	if (np) {
		ret = of_alias_get_id(np, "serial");
		if (ret >= 0)
			index = ret;
	}

	if (index >= ARRAY_SIZE(s3c24xx_serial_ports)) {
		dev_err(&pdev->dev, "serial%d out of range\n", index);
		return -EINVAL;
	}
	ourport = &s3c24xx_serial_ports[index];

	s3c24xx_serial_init_port_default(index);

	ourport->drv_data = s3c24xx_get_driver_data(pdev);
	if (!ourport->drv_data) {
		dev_err(&pdev->dev, "could not find driver data\n");
		return -ENODEV;
	}

	ourport->baudclk = ERR_PTR(-EINVAL);
	ourport->info = &ourport->drv_data->info;
	ourport->cfg = (dev_get_platdata(&pdev->dev)) ?
			dev_get_platdata(&pdev->dev) :
			&ourport->drv_data->def_cfg;

	switch (ourport->info->type) {
	case TYPE_S3C24XX:
		ourport->port.ops = &s3c24xx_serial_ops;
		break;
	case TYPE_S3C6400:
		ourport->port.ops = &s3c64xx_serial_ops;
		break;
	case TYPE_APPLE_S5L:
		ourport->port.ops = &apple_s5l_serial_ops;
		break;
	}

	if (np) {
		of_property_read_u32(np,
			"samsung,uart-fifosize", &ourport->port.fifosize);

		if (of_property_read_u32(np, "reg-io-width", &prop) == 0) {
			switch (prop) {
			case 1:
				ourport->port.iotype = UPIO_MEM;
				break;
			case 4:
				ourport->port.iotype = UPIO_MEM32;
				break;
			default:
				dev_warn(&pdev->dev, "unsupported reg-io-width (%d)\n",
						prop);
				return -EINVAL;
			}
		}
	}

	if (ourport->drv_data->fifosize[index])
		ourport->port.fifosize = ourport->drv_data->fifosize[index];
	else if (ourport->info->fifosize)
		ourport->port.fifosize = ourport->info->fifosize;
	ourport->port.has_sysrq = IS_ENABLED(CONFIG_SERIAL_SAMSUNG_CONSOLE);

	/*
	 * DMA transfers must be aligned at least to cache line size,
	 * so find minimal transfer size suitable for DMA mode
	 */
	ourport->min_dma_size = max_t(int, ourport->port.fifosize,
				    dma_get_cache_alignment());

	dev_dbg(&pdev->dev, "%s: initialising port %p...\n", __func__, ourport);

	ret = s3c24xx_serial_init_port(ourport, pdev);
	if (ret < 0)
		return ret;

	if (!s3c24xx_uart_drv.state) {
		ret = uart_register_driver(&s3c24xx_uart_drv);
		if (ret < 0) {
			pr_err("Failed to register Samsung UART driver\n");
			return ret;
		}
	}

	dev_dbg(&pdev->dev, "%s: adding port\n", __func__);
	uart_add_one_port(&s3c24xx_uart_drv, &ourport->port);
	platform_set_drvdata(pdev, &ourport->port);

	/*
	 * Deactivate the clock enabled in s3c24xx_serial_init_port here,
	 * so that a potential re-enablement through the pm-callback overlaps
	 * and keeps the clock enabled in this case.
	 */
	clk_disable_unprepare(ourport->clk);
	if (!IS_ERR(ourport->baudclk))
		clk_disable_unprepare(ourport->baudclk);

	probe_index++;

	return 0;
}

static int s3c24xx_serial_remove(struct platform_device *dev)
{
	struct uart_port *port = s3c24xx_dev_to_port(&dev->dev);

	if (port) {
		uart_remove_one_port(&s3c24xx_uart_drv, port);
	}

	uart_unregister_driver(&s3c24xx_uart_drv);

	return 0;
}

/* UART power management code */
#ifdef CONFIG_PM_SLEEP
static int s3c24xx_serial_suspend(struct device *dev)
{
	struct uart_port *port = s3c24xx_dev_to_port(dev);

	if (port)
		uart_suspend_port(&s3c24xx_uart_drv, port);

	return 0;
}

static int s3c24xx_serial_resume(struct device *dev)
{
	struct uart_port *port = s3c24xx_dev_to_port(dev);
	struct s3c24xx_uart_port *ourport = to_ourport(port);

	if (port) {
		clk_prepare_enable(ourport->clk);
		if (!IS_ERR(ourport->baudclk))
			clk_prepare_enable(ourport->baudclk);
		s3c24xx_serial_resetport(port, s3c24xx_port_to_cfg(port));
		if (!IS_ERR(ourport->baudclk))
			clk_disable_unprepare(ourport->baudclk);
		clk_disable_unprepare(ourport->clk);

		uart_resume_port(&s3c24xx_uart_drv, port);
	}

	return 0;
}

static int s3c24xx_serial_resume_noirq(struct device *dev)
{
	struct uart_port *port = s3c24xx_dev_to_port(dev);
	struct s3c24xx_uart_port *ourport = to_ourport(port);

	if (port) {
		/* restore IRQ mask */
		switch (ourport->info->type) {
		case TYPE_S3C6400: {
			unsigned int uintm = 0xf;

			if (ourport->tx_enabled)
				uintm &= ~S3C64XX_UINTM_TXD_MSK;
			if (ourport->rx_enabled)
				uintm &= ~S3C64XX_UINTM_RXD_MSK;
			clk_prepare_enable(ourport->clk);
			if (!IS_ERR(ourport->baudclk))
				clk_prepare_enable(ourport->baudclk);
			wr_regl(port, S3C64XX_UINTM, uintm);
			if (!IS_ERR(ourport->baudclk))
				clk_disable_unprepare(ourport->baudclk);
			clk_disable_unprepare(ourport->clk);
			break;
		}
		case TYPE_APPLE_S5L: {
			unsigned int ucon;
			int ret;

			ret = clk_prepare_enable(ourport->clk);
			if (ret) {
				dev_err(dev, "clk_enable clk failed: %d\n", ret);
				return ret;
			}
			if (!IS_ERR(ourport->baudclk)) {
				ret = clk_prepare_enable(ourport->baudclk);
				if (ret) {
					dev_err(dev, "clk_enable baudclk failed: %d\n", ret);
					clk_disable_unprepare(ourport->clk);
					return ret;
				}
			}

			ucon = rd_regl(port, S3C2410_UCON);

			ucon &= ~(APPLE_S5L_UCON_TXTHRESH_ENA_MSK |
				  APPLE_S5L_UCON_RXTHRESH_ENA_MSK |
				  APPLE_S5L_UCON_RXTO_ENA_MSK);

			if (ourport->tx_enabled)
				ucon |= APPLE_S5L_UCON_TXTHRESH_ENA_MSK;
			if (ourport->rx_enabled)
				ucon |= APPLE_S5L_UCON_RXTHRESH_ENA_MSK |
					APPLE_S5L_UCON_RXTO_ENA_MSK;

			wr_regl(port, S3C2410_UCON, ucon);

			if (!IS_ERR(ourport->baudclk))
				clk_disable_unprepare(ourport->baudclk);
			clk_disable_unprepare(ourport->clk);
			break;
		}
		default:
			break;
		}
	}

	return 0;
}

static const struct dev_pm_ops s3c24xx_serial_pm_ops = {
	.suspend = s3c24xx_serial_suspend,
	.resume = s3c24xx_serial_resume,
	.resume_noirq = s3c24xx_serial_resume_noirq,
};
#define SERIAL_SAMSUNG_PM_OPS	(&s3c24xx_serial_pm_ops)

#else /* !CONFIG_PM_SLEEP */

#define SERIAL_SAMSUNG_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

/* Console code */

#ifdef CONFIG_SERIAL_SAMSUNG_CONSOLE

static struct uart_port *cons_uart;

static int
s3c24xx_serial_console_txrdy(struct uart_port *port, unsigned int ufcon)
{
	const struct s3c24xx_uart_info *info = s3c24xx_port_to_info(port);
	unsigned long ufstat, utrstat;

	if (ufcon & S3C2410_UFCON_FIFOMODE) {
		/* fifo mode - check amount of data in fifo registers... */

		ufstat = rd_regl(port, S3C2410_UFSTAT);
		return (ufstat & info->tx_fifofull) ? 0 : 1;
	}

	/* in non-fifo mode, we go and use the tx buffer empty */

	utrstat = rd_regl(port, S3C2410_UTRSTAT);
	return (utrstat & S3C2410_UTRSTAT_TXE) ? 1 : 0;
}

static bool
s3c24xx_port_configured(unsigned int ucon)
{
	/* consider the serial port configured if the tx/rx mode set */
	return (ucon & 0xf) != 0;
}

#ifdef CONFIG_CONSOLE_POLL
/*
 * Console polling routines for writing and reading from the uart while
 * in an interrupt or debug context.
 */

static int s3c24xx_serial_get_poll_char(struct uart_port *port)
{
	const struct s3c24xx_uart_port *ourport = to_ourport(port);
	unsigned int ufstat;

	ufstat = rd_regl(port, S3C2410_UFSTAT);
	if (s3c24xx_serial_rx_fifocnt(ourport, ufstat) == 0)
		return NO_POLL_CHAR;

	return rd_reg(port, S3C2410_URXH);
}

static void s3c24xx_serial_put_poll_char(struct uart_port *port,
		unsigned char c)
{
	unsigned int ufcon = rd_regl(port, S3C2410_UFCON);
	unsigned int ucon = rd_regl(port, S3C2410_UCON);

	/* not possible to xmit on unconfigured port */
	if (!s3c24xx_port_configured(ucon))
		return;

	while (!s3c24xx_serial_console_txrdy(port, ufcon))
		cpu_relax();
	wr_reg(port, S3C2410_UTXH, c);
}

#endif /* CONFIG_CONSOLE_POLL */

static void
s3c24xx_serial_console_putchar(struct uart_port *port, unsigned char ch)
{
	unsigned int ufcon = rd_regl(port, S3C2410_UFCON);

	while (!s3c24xx_serial_console_txrdy(port, ufcon))
		cpu_relax();
	wr_reg(port, S3C2410_UTXH, ch);
}

static void
s3c24xx_serial_console_write(struct console *co, const char *s,
			     unsigned int count)
{
	unsigned int ucon = rd_regl(cons_uart, S3C2410_UCON);
	unsigned long flags;
	bool locked = true;

	/* not possible to xmit on unconfigured port */
	if (!s3c24xx_port_configured(ucon))
		return;

	if (cons_uart->sysrq)
		locked = false;
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&cons_uart->lock, flags);
	else
		spin_lock_irqsave(&cons_uart->lock, flags);

	uart_console_write(cons_uart, s, count, s3c24xx_serial_console_putchar);

	if (locked)
		spin_unlock_irqrestore(&cons_uart->lock, flags);
}

/* Shouldn't be __init, as it can be instantiated from other module */
static void
s3c24xx_serial_get_options(struct uart_port *port, int *baud,
			   int *parity, int *bits)
{
	struct clk *clk;
	unsigned int ulcon;
	unsigned int ucon;
	unsigned int ubrdiv;
	unsigned long rate;
	unsigned int clk_sel;
	char clk_name[MAX_CLK_NAME_LENGTH];

	ulcon  = rd_regl(port, S3C2410_ULCON);
	ucon   = rd_regl(port, S3C2410_UCON);
	ubrdiv = rd_regl(port, S3C2410_UBRDIV);

	if (s3c24xx_port_configured(ucon)) {
		switch (ulcon & S3C2410_LCON_CSMASK) {
		case S3C2410_LCON_CS5:
			*bits = 5;
			break;
		case S3C2410_LCON_CS6:
			*bits = 6;
			break;
		case S3C2410_LCON_CS7:
			*bits = 7;
			break;
		case S3C2410_LCON_CS8:
		default:
			*bits = 8;
			break;
		}

		switch (ulcon & S3C2410_LCON_PMASK) {
		case S3C2410_LCON_PEVEN:
			*parity = 'e';
			break;

		case S3C2410_LCON_PODD:
			*parity = 'o';
			break;

		case S3C2410_LCON_PNONE:
		default:
			*parity = 'n';
		}

		/* now calculate the baud rate */

		clk_sel = s3c24xx_serial_getsource(port);
		sprintf(clk_name, "clk_uart_baud%d", clk_sel);

		clk = clk_get(port->dev, clk_name);
		if (!IS_ERR(clk))
			rate = clk_get_rate(clk);
		else
			rate = 1;

		*baud = rate / (16 * (ubrdiv + 1));
		dev_dbg(port->dev, "calculated baud %d\n", *baud);
	}
}

/* Shouldn't be __init, as it can be instantiated from other module */
static int
s3c24xx_serial_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/* is this a valid port */

	if (co->index == -1 || co->index >= UART_NR)
		co->index = 0;

	port = &s3c24xx_serial_ports[co->index].port;

	/* is the port configured? */

	if (port->mapbase == 0x0)
		return -ENODEV;

	cons_uart = port;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		s3c24xx_serial_get_options(port, &baud, &parity, &bits);

	dev_dbg(port->dev, "baud %d\n", baud);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console s3c24xx_serial_console = {
	.name		= S3C24XX_SERIAL_NAME,
	.device		= uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.write		= s3c24xx_serial_console_write,
	.setup		= s3c24xx_serial_console_setup,
	.data		= &s3c24xx_uart_drv,
};
#endif /* CONFIG_SERIAL_SAMSUNG_CONSOLE */

#if defined(CONFIG_CPU_S3C6400) || defined(CONFIG_CPU_S3C6410)
static const struct s3c24xx_serial_drv_data s3c6400_serial_drv_data = {
	.info = {
		.name		= "Samsung S3C6400 UART",
		.type		= TYPE_S3C6400,
		.port_type	= PORT_S3C6400,
		.fifosize	= 64,
		.has_divslot	= 1,
		.rx_fifomask	= S3C2440_UFSTAT_RXMASK,
		.rx_fifoshift	= S3C2440_UFSTAT_RXSHIFT,
		.rx_fifofull	= S3C2440_UFSTAT_RXFULL,
		.tx_fifofull	= S3C2440_UFSTAT_TXFULL,
		.tx_fifomask	= S3C2440_UFSTAT_TXMASK,
		.tx_fifoshift	= S3C2440_UFSTAT_TXSHIFT,
		.def_clk_sel	= S3C2410_UCON_CLKSEL2,
		.num_clks	= 4,
		.clksel_mask	= S3C6400_UCON_CLKMASK,
		.clksel_shift	= S3C6400_UCON_CLKSHIFT,
	},
	.def_cfg = {
		.ucon		= S3C2410_UCON_DEFAULT,
		.ufcon		= S3C2410_UFCON_DEFAULT,
	},
};
#define S3C6400_SERIAL_DRV_DATA (&s3c6400_serial_drv_data)
#else
#define S3C6400_SERIAL_DRV_DATA NULL
#endif

#ifdef CONFIG_CPU_S5PV210
static const struct s3c24xx_serial_drv_data s5pv210_serial_drv_data = {
	.info = {
		.name		= "Samsung S5PV210 UART",
		.type		= TYPE_S3C6400,
		.port_type	= PORT_S3C6400,
		.has_divslot	= 1,
		.rx_fifomask	= S5PV210_UFSTAT_RXMASK,
		.rx_fifoshift	= S5PV210_UFSTAT_RXSHIFT,
		.rx_fifofull	= S5PV210_UFSTAT_RXFULL,
		.tx_fifofull	= S5PV210_UFSTAT_TXFULL,
		.tx_fifomask	= S5PV210_UFSTAT_TXMASK,
		.tx_fifoshift	= S5PV210_UFSTAT_TXSHIFT,
		.def_clk_sel	= S3C2410_UCON_CLKSEL0,
		.num_clks	= 2,
		.clksel_mask	= S5PV210_UCON_CLKMASK,
		.clksel_shift	= S5PV210_UCON_CLKSHIFT,
	},
	.def_cfg = {
		.ucon		= S5PV210_UCON_DEFAULT,
		.ufcon		= S5PV210_UFCON_DEFAULT,
	},
	.fifosize = { 256, 64, 16, 16 },
};
#define S5PV210_SERIAL_DRV_DATA (&s5pv210_serial_drv_data)
#else
#define S5PV210_SERIAL_DRV_DATA	NULL
#endif

#if defined(CONFIG_ARCH_EXYNOS)
#define EXYNOS_COMMON_SERIAL_DRV_DATA()				\
	.info = {						\
		.name		= "Samsung Exynos UART",	\
		.type		= TYPE_S3C6400,			\
		.port_type	= PORT_S3C6400,			\
		.has_divslot	= 1,				\
		.rx_fifomask	= S5PV210_UFSTAT_RXMASK,	\
		.rx_fifoshift	= S5PV210_UFSTAT_RXSHIFT,	\
		.rx_fifofull	= S5PV210_UFSTAT_RXFULL,	\
		.tx_fifofull	= S5PV210_UFSTAT_TXFULL,	\
		.tx_fifomask	= S5PV210_UFSTAT_TXMASK,	\
		.tx_fifoshift	= S5PV210_UFSTAT_TXSHIFT,	\
		.def_clk_sel	= S3C2410_UCON_CLKSEL0,		\
		.num_clks	= 1,				\
		.clksel_mask	= 0,				\
		.clksel_shift	= 0,				\
	},							\
	.def_cfg = {						\
		.ucon		= S5PV210_UCON_DEFAULT,		\
		.ufcon		= S5PV210_UFCON_DEFAULT,	\
		.has_fracval	= 1,				\
	}							\

static const struct s3c24xx_serial_drv_data exynos4210_serial_drv_data = {
	EXYNOS_COMMON_SERIAL_DRV_DATA(),
	.fifosize = { 256, 64, 16, 16 },
};

static const struct s3c24xx_serial_drv_data exynos5433_serial_drv_data = {
	EXYNOS_COMMON_SERIAL_DRV_DATA(),
	.fifosize = { 64, 256, 16, 256 },
};

static const struct s3c24xx_serial_drv_data exynos850_serial_drv_data = {
	EXYNOS_COMMON_SERIAL_DRV_DATA(),
	.fifosize = { 256, 64, 64, 64 },
};

#define EXYNOS4210_SERIAL_DRV_DATA (&exynos4210_serial_drv_data)
#define EXYNOS5433_SERIAL_DRV_DATA (&exynos5433_serial_drv_data)
#define EXYNOS850_SERIAL_DRV_DATA (&exynos850_serial_drv_data)

#else
#define EXYNOS4210_SERIAL_DRV_DATA NULL
#define EXYNOS5433_SERIAL_DRV_DATA NULL
#define EXYNOS850_SERIAL_DRV_DATA NULL
#endif

#ifdef CONFIG_ARCH_APPLE
static const struct s3c24xx_serial_drv_data s5l_serial_drv_data = {
	.info = {
		.name		= "Apple S5L UART",
		.type		= TYPE_APPLE_S5L,
		.port_type	= PORT_8250,
		.fifosize	= 16,
		.rx_fifomask	= S3C2410_UFSTAT_RXMASK,
		.rx_fifoshift	= S3C2410_UFSTAT_RXSHIFT,
		.rx_fifofull	= S3C2410_UFSTAT_RXFULL,
		.tx_fifofull	= S3C2410_UFSTAT_TXFULL,
		.tx_fifomask	= S3C2410_UFSTAT_TXMASK,
		.tx_fifoshift	= S3C2410_UFSTAT_TXSHIFT,
		.def_clk_sel	= S3C2410_UCON_CLKSEL0,
		.num_clks	= 1,
		.clksel_mask	= 0,
		.clksel_shift	= 0,
		.ucon_mask	= APPLE_S5L_UCON_MASK,
	},
	.def_cfg = {
		.ucon		= APPLE_S5L_UCON_DEFAULT,
		.ufcon		= S3C2410_UFCON_DEFAULT,
	},
};
#define S5L_SERIAL_DRV_DATA (&s5l_serial_drv_data)
#else
#define S5L_SERIAL_DRV_DATA NULL
#endif

#if defined(CONFIG_ARCH_ARTPEC)
static const struct s3c24xx_serial_drv_data artpec8_serial_drv_data = {
	.info = {
		.name		= "Axis ARTPEC-8 UART",
		.type		= TYPE_S3C6400,
		.port_type	= PORT_S3C6400,
		.fifosize	= 64,
		.has_divslot	= 1,
		.rx_fifomask	= S5PV210_UFSTAT_RXMASK,
		.rx_fifoshift	= S5PV210_UFSTAT_RXSHIFT,
		.rx_fifofull	= S5PV210_UFSTAT_RXFULL,
		.tx_fifofull	= S5PV210_UFSTAT_TXFULL,
		.tx_fifomask	= S5PV210_UFSTAT_TXMASK,
		.tx_fifoshift	= S5PV210_UFSTAT_TXSHIFT,
		.def_clk_sel	= S3C2410_UCON_CLKSEL0,
		.num_clks	= 1,
		.clksel_mask	= 0,
		.clksel_shift	= 0,
	},
	.def_cfg = {
		.ucon		= S5PV210_UCON_DEFAULT,
		.ufcon		= S5PV210_UFCON_DEFAULT,
		.has_fracval	= 1,
	}
};
#define ARTPEC8_SERIAL_DRV_DATA (&artpec8_serial_drv_data)
#else
#define ARTPEC8_SERIAL_DRV_DATA (NULL)
#endif

static const struct platform_device_id s3c24xx_serial_driver_ids[] = {
	{
		.name		= "s3c6400-uart",
		.driver_data	= (kernel_ulong_t)S3C6400_SERIAL_DRV_DATA,
	}, {
		.name		= "s5pv210-uart",
		.driver_data	= (kernel_ulong_t)S5PV210_SERIAL_DRV_DATA,
	}, {
		.name		= "exynos4210-uart",
		.driver_data	= (kernel_ulong_t)EXYNOS4210_SERIAL_DRV_DATA,
	}, {
		.name		= "exynos5433-uart",
		.driver_data	= (kernel_ulong_t)EXYNOS5433_SERIAL_DRV_DATA,
	}, {
		.name		= "s5l-uart",
		.driver_data	= (kernel_ulong_t)S5L_SERIAL_DRV_DATA,
	}, {
		.name		= "exynos850-uart",
		.driver_data	= (kernel_ulong_t)EXYNOS850_SERIAL_DRV_DATA,
	}, {
		.name		= "artpec8-uart",
		.driver_data	= (kernel_ulong_t)ARTPEC8_SERIAL_DRV_DATA,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, s3c24xx_serial_driver_ids);

#ifdef CONFIG_OF
static const struct of_device_id s3c24xx_uart_dt_match[] = {
	{ .compatible = "samsung,s3c6400-uart",
		.data = S3C6400_SERIAL_DRV_DATA },
	{ .compatible = "samsung,s5pv210-uart",
		.data = S5PV210_SERIAL_DRV_DATA },
	{ .compatible = "samsung,exynos4210-uart",
		.data = EXYNOS4210_SERIAL_DRV_DATA },
	{ .compatible = "samsung,exynos5433-uart",
		.data = EXYNOS5433_SERIAL_DRV_DATA },
	{ .compatible = "apple,s5l-uart",
		.data = S5L_SERIAL_DRV_DATA },
	{ .compatible = "samsung,exynos850-uart",
		.data = EXYNOS850_SERIAL_DRV_DATA },
	{ .compatible = "axis,artpec8-uart",
		.data = ARTPEC8_SERIAL_DRV_DATA },
	{},
};
MODULE_DEVICE_TABLE(of, s3c24xx_uart_dt_match);
#endif

static struct platform_driver samsung_serial_driver = {
	.probe		= s3c24xx_serial_probe,
	.remove		= s3c24xx_serial_remove,
	.id_table	= s3c24xx_serial_driver_ids,
	.driver		= {
		.name	= "samsung-uart",
		.pm	= SERIAL_SAMSUNG_PM_OPS,
		.of_match_table	= of_match_ptr(s3c24xx_uart_dt_match),
	},
};

static int __init samsung_serial_init(void)
{
	int ret;

	s3c24xx_serial_register_console();

	ret = platform_driver_register(&samsung_serial_driver);
	if (ret) {
		s3c24xx_serial_unregister_console();
		return ret;
	}

	return 0;
}

static void __exit samsung_serial_exit(void)
{
	platform_driver_unregister(&samsung_serial_driver);
	s3c24xx_serial_unregister_console();
}

module_init(samsung_serial_init);
module_exit(samsung_serial_exit);

#ifdef CONFIG_SERIAL_SAMSUNG_CONSOLE
/*
 * Early console.
 */

static void wr_reg_barrier(const struct uart_port *port, u32 reg, u32 val)
{
	switch (port->iotype) {
	case UPIO_MEM:
		writeb(val, portaddr(port, reg));
		break;
	case UPIO_MEM32:
		writel(val, portaddr(port, reg));
		break;
	}
}

struct samsung_early_console_data {
	u32 txfull_mask;
	u32 rxfifo_mask;
};

static void samsung_early_busyuart(const struct uart_port *port)
{
	while (!(readl(port->membase + S3C2410_UTRSTAT) & S3C2410_UTRSTAT_TXFE))
		;
}

static void samsung_early_busyuart_fifo(const struct uart_port *port)
{
	const struct samsung_early_console_data *data = port->private_data;

	while (readl(port->membase + S3C2410_UFSTAT) & data->txfull_mask)
		;
}

static void samsung_early_putc(struct uart_port *port, unsigned char c)
{
	if (readl(port->membase + S3C2410_UFCON) & S3C2410_UFCON_FIFOMODE)
		samsung_early_busyuart_fifo(port);
	else
		samsung_early_busyuart(port);

	wr_reg_barrier(port, S3C2410_UTXH, c);
}

static void samsung_early_write(struct console *con, const char *s,
				unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, samsung_early_putc);
}

static int samsung_early_read(struct console *con, char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;
	const struct samsung_early_console_data *data = dev->port.private_data;
	int ch, ufstat, num_read = 0;

	while (num_read < n) {
		ufstat = rd_regl(&dev->port, S3C2410_UFSTAT);
		if (!(ufstat & data->rxfifo_mask))
			break;
		ch = rd_reg(&dev->port, S3C2410_URXH);
		if (ch == NO_POLL_CHAR)
			break;

		s[num_read++] = ch;
	}

	return num_read;
}

static int __init samsung_early_console_setup(struct earlycon_device *device,
					      const char *opt)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = samsung_early_write;
	device->con->read = samsung_early_read;
	return 0;
}

/* S3C2410 */
static struct samsung_early_console_data s3c2410_early_console_data = {
	.txfull_mask = S3C2410_UFSTAT_TXFULL,
	.rxfifo_mask = S3C2410_UFSTAT_RXFULL | S3C2410_UFSTAT_RXMASK,
};

static int __init s3c2410_early_console_setup(struct earlycon_device *device,
					      const char *opt)
{
	device->port.private_data = &s3c2410_early_console_data;
	return samsung_early_console_setup(device, opt);
}

OF_EARLYCON_DECLARE(s3c2410, "samsung,s3c2410-uart",
			s3c2410_early_console_setup);

/* S3C2412, S3C2440, S3C64xx */
static struct samsung_early_console_data s3c2440_early_console_data = {
	.txfull_mask = S3C2440_UFSTAT_TXFULL,
	.rxfifo_mask = S3C2440_UFSTAT_RXFULL | S3C2440_UFSTAT_RXMASK,
};

static int __init s3c2440_early_console_setup(struct earlycon_device *device,
					      const char *opt)
{
	device->port.private_data = &s3c2440_early_console_data;
	return samsung_early_console_setup(device, opt);
}

OF_EARLYCON_DECLARE(s3c2412, "samsung,s3c2412-uart",
			s3c2440_early_console_setup);
OF_EARLYCON_DECLARE(s3c2440, "samsung,s3c2440-uart",
			s3c2440_early_console_setup);
OF_EARLYCON_DECLARE(s3c6400, "samsung,s3c6400-uart",
			s3c2440_early_console_setup);

/* S5PV210, Exynos */
static struct samsung_early_console_data s5pv210_early_console_data = {
	.txfull_mask = S5PV210_UFSTAT_TXFULL,
	.rxfifo_mask = S5PV210_UFSTAT_RXFULL | S5PV210_UFSTAT_RXMASK,
};

static int __init s5pv210_early_console_setup(struct earlycon_device *device,
					      const char *opt)
{
	device->port.private_data = &s5pv210_early_console_data;
	return samsung_early_console_setup(device, opt);
}

OF_EARLYCON_DECLARE(s5pv210, "samsung,s5pv210-uart",
			s5pv210_early_console_setup);
OF_EARLYCON_DECLARE(exynos4210, "samsung,exynos4210-uart",
			s5pv210_early_console_setup);
OF_EARLYCON_DECLARE(artpec8, "axis,artpec8-uart",
			s5pv210_early_console_setup);

/* Apple S5L */
static int __init apple_s5l_early_console_setup(struct earlycon_device *device,
						const char *opt)
{
	/* Close enough to S3C2410 for earlycon... */
	device->port.private_data = &s3c2410_early_console_data;

#ifdef CONFIG_ARM64
	/* ... but we need to override the existing fixmap entry as nGnRnE */
	__set_fixmap(FIX_EARLYCON_MEM_BASE, device->port.mapbase,
		     __pgprot(PROT_DEVICE_nGnRnE));
#endif
	return samsung_early_console_setup(device, opt);
}

OF_EARLYCON_DECLARE(s5l, "apple,s5l-uart", apple_s5l_early_console_setup);
#endif

MODULE_ALIAS("platform:samsung-uart");
MODULE_DESCRIPTION("Samsung SoC Serial port driver");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_LICENSE("GPL v2");
