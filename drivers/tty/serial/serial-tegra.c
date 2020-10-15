// SPDX-License-Identifier: GPL-2.0
/*
 * serial_tegra.c
 *
 * High-speed serial driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2012-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#define TEGRA_UART_TYPE				"TEGRA_UART"
#define TX_EMPTY_STATUS				(UART_LSR_TEMT | UART_LSR_THRE)
#define BYTES_TO_ALIGN(x)			((unsigned long)(x) & 0x3)

#define TEGRA_UART_RX_DMA_BUFFER_SIZE		4096
#define TEGRA_UART_LSR_TXFIFO_FULL		0x100
#define TEGRA_UART_IER_EORD			0x20
#define TEGRA_UART_MCR_RTS_EN			0x40
#define TEGRA_UART_MCR_CTS_EN			0x20
#define TEGRA_UART_LSR_ANY			(UART_LSR_OE | UART_LSR_BI | \
						UART_LSR_PE | UART_LSR_FE)
#define TEGRA_UART_IRDA_CSR			0x08
#define TEGRA_UART_SIR_ENABLED			0x80

#define TEGRA_UART_TX_PIO			1
#define TEGRA_UART_TX_DMA			2
#define TEGRA_UART_MIN_DMA			16
#define TEGRA_UART_FIFO_SIZE			32

/*
 * Tx fifo trigger level setting in tegra uart is in
 * reverse way then conventional uart.
 */
#define TEGRA_UART_TX_TRIG_16B			0x00
#define TEGRA_UART_TX_TRIG_8B			0x10
#define TEGRA_UART_TX_TRIG_4B			0x20
#define TEGRA_UART_TX_TRIG_1B			0x30

#define TEGRA_UART_MAXIMUM			8

/* Default UART setting when started: 115200 no parity, stop, 8 data bits */
#define TEGRA_UART_DEFAULT_BAUD			115200
#define TEGRA_UART_DEFAULT_LSR			UART_LCR_WLEN8

/* Tx transfer mode */
#define TEGRA_TX_PIO				1
#define TEGRA_TX_DMA				2

#define TEGRA_UART_FCR_IIR_FIFO_EN		0x40

/**
 * tegra_uart_chip_data: SOC specific data.
 *
 * @tx_fifo_full_status: Status flag available for checking tx fifo full.
 * @allow_txfifo_reset_fifo_mode: allow_tx fifo reset with fifo mode or not.
 *			Tegra30 does not allow this.
 * @support_clk_src_div: Clock source support the clock divider.
 */
struct tegra_uart_chip_data {
	bool	tx_fifo_full_status;
	bool	allow_txfifo_reset_fifo_mode;
	bool	support_clk_src_div;
	bool	fifo_mode_enable_status;
	int	uart_max_port;
	int	max_dma_burst_bytes;
	int	error_tolerance_low_range;
	int	error_tolerance_high_range;
};

struct tegra_baud_tolerance {
	u32 lower_range_baud;
	u32 upper_range_baud;
	s32 tolerance;
};

struct tegra_uart_port {
	struct uart_port			uport;
	const struct tegra_uart_chip_data	*cdata;

	struct clk				*uart_clk;
	struct reset_control			*rst;
	unsigned int				current_baud;

	/* Register shadow */
	unsigned long				fcr_shadow;
	unsigned long				mcr_shadow;
	unsigned long				lcr_shadow;
	unsigned long				ier_shadow;
	bool					rts_active;

	int					tx_in_progress;
	unsigned int				tx_bytes;

	bool					enable_modem_interrupt;

	bool					rx_timeout;
	int					rx_in_progress;
	int					symb_bit;

	struct dma_chan				*rx_dma_chan;
	struct dma_chan				*tx_dma_chan;
	dma_addr_t				rx_dma_buf_phys;
	dma_addr_t				tx_dma_buf_phys;
	unsigned char				*rx_dma_buf_virt;
	unsigned char				*tx_dma_buf_virt;
	struct dma_async_tx_descriptor		*tx_dma_desc;
	struct dma_async_tx_descriptor		*rx_dma_desc;
	dma_cookie_t				tx_cookie;
	dma_cookie_t				rx_cookie;
	unsigned int				tx_bytes_requested;
	unsigned int				rx_bytes_requested;
	struct tegra_baud_tolerance		*baud_tolerance;
	int					n_adjustable_baud_rates;
	int					required_rate;
	int					configured_rate;
	bool					use_rx_pio;
	bool					use_tx_pio;
	bool					rx_dma_active;
};

static void tegra_uart_start_next_tx(struct tegra_uart_port *tup);
static int tegra_uart_start_rx_dma(struct tegra_uart_port *tup);
static void tegra_uart_dma_channel_free(struct tegra_uart_port *tup,
					bool dma_to_memory);

static inline unsigned long tegra_uart_read(struct tegra_uart_port *tup,
		unsigned long reg)
{
	return readl(tup->uport.membase + (reg << tup->uport.regshift));
}

static inline void tegra_uart_write(struct tegra_uart_port *tup, unsigned val,
	unsigned long reg)
{
	writel(val, tup->uport.membase + (reg << tup->uport.regshift));
}

static inline struct tegra_uart_port *to_tegra_uport(struct uart_port *u)
{
	return container_of(u, struct tegra_uart_port, uport);
}

static unsigned int tegra_uart_get_mctrl(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);

	/*
	 * RI - Ring detector is active
	 * CD/DCD/CAR - Carrier detect is always active. For some reason
	 *	linux has different names for carrier detect.
	 * DSR - Data Set ready is active as the hardware doesn't support it.
	 *	Don't know if the linux support this yet?
	 * CTS - Clear to send. Always set to active, as the hardware handles
	 *	CTS automatically.
	 */
	if (tup->enable_modem_interrupt)
		return TIOCM_RI | TIOCM_CD | TIOCM_DSR | TIOCM_CTS;
	return TIOCM_CTS;
}

static void set_rts(struct tegra_uart_port *tup, bool active)
{
	unsigned long mcr;

	mcr = tup->mcr_shadow;
	if (active)
		mcr |= TEGRA_UART_MCR_RTS_EN;
	else
		mcr &= ~TEGRA_UART_MCR_RTS_EN;
	if (mcr != tup->mcr_shadow) {
		tegra_uart_write(tup, mcr, UART_MCR);
		tup->mcr_shadow = mcr;
	}
}

static void set_dtr(struct tegra_uart_port *tup, bool active)
{
	unsigned long mcr;

	mcr = tup->mcr_shadow;
	if (active)
		mcr |= UART_MCR_DTR;
	else
		mcr &= ~UART_MCR_DTR;
	if (mcr != tup->mcr_shadow) {
		tegra_uart_write(tup, mcr, UART_MCR);
		tup->mcr_shadow = mcr;
	}
}

static void set_loopbk(struct tegra_uart_port *tup, bool active)
{
	unsigned long mcr = tup->mcr_shadow;

	if (active)
		mcr |= UART_MCR_LOOP;
	else
		mcr &= ~UART_MCR_LOOP;

	if (mcr != tup->mcr_shadow) {
		tegra_uart_write(tup, mcr, UART_MCR);
		tup->mcr_shadow = mcr;
	}
}

static void tegra_uart_set_mctrl(struct uart_port *u, unsigned int mctrl)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	int enable;

	tup->rts_active = !!(mctrl & TIOCM_RTS);
	set_rts(tup, tup->rts_active);

	enable = !!(mctrl & TIOCM_DTR);
	set_dtr(tup, enable);

	enable = !!(mctrl & TIOCM_LOOP);
	set_loopbk(tup, enable);
}

static void tegra_uart_break_ctl(struct uart_port *u, int break_ctl)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	unsigned long lcr;

	lcr = tup->lcr_shadow;
	if (break_ctl)
		lcr |= UART_LCR_SBC;
	else
		lcr &= ~UART_LCR_SBC;
	tegra_uart_write(tup, lcr, UART_LCR);
	tup->lcr_shadow = lcr;
}

/**
 * tegra_uart_wait_cycle_time: Wait for N UART clock periods
 *
 * @tup:	Tegra serial port data structure.
 * @cycles:	Number of clock periods to wait.
 *
 * Tegra UARTs are clocked at 16X the baud/bit rate and hence the UART
 * clock speed is 16X the current baud rate.
 */
static void tegra_uart_wait_cycle_time(struct tegra_uart_port *tup,
				       unsigned int cycles)
{
	if (tup->current_baud)
		udelay(DIV_ROUND_UP(cycles * 1000000, tup->current_baud * 16));
}

/* Wait for a symbol-time. */
static void tegra_uart_wait_sym_time(struct tegra_uart_port *tup,
		unsigned int syms)
{
	if (tup->current_baud)
		udelay(DIV_ROUND_UP(syms * tup->symb_bit * 1000000,
			tup->current_baud));
}

static int tegra_uart_wait_fifo_mode_enabled(struct tegra_uart_port *tup)
{
	unsigned long iir;
	unsigned int tmout = 100;

	do {
		iir = tegra_uart_read(tup, UART_IIR);
		if (iir & TEGRA_UART_FCR_IIR_FIFO_EN)
			return 0;
		udelay(1);
	} while (--tmout);

	return -ETIMEDOUT;
}

static void tegra_uart_fifo_reset(struct tegra_uart_port *tup, u8 fcr_bits)
{
	unsigned long fcr = tup->fcr_shadow;
	unsigned int lsr, tmout = 10000;

	if (tup->rts_active)
		set_rts(tup, false);

	if (tup->cdata->allow_txfifo_reset_fifo_mode) {
		fcr |= fcr_bits & (UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
		tegra_uart_write(tup, fcr, UART_FCR);
	} else {
		fcr &= ~UART_FCR_ENABLE_FIFO;
		tegra_uart_write(tup, fcr, UART_FCR);
		udelay(60);
		fcr |= fcr_bits & (UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
		tegra_uart_write(tup, fcr, UART_FCR);
		fcr |= UART_FCR_ENABLE_FIFO;
		tegra_uart_write(tup, fcr, UART_FCR);
		if (tup->cdata->fifo_mode_enable_status)
			tegra_uart_wait_fifo_mode_enabled(tup);
	}

	/* Dummy read to ensure the write is posted */
	tegra_uart_read(tup, UART_SCR);

	/*
	 * For all tegra devices (up to t210), there is a hardware issue that
	 * requires software to wait for 32 UART clock periods for the flush
	 * to propagate, otherwise data could be lost.
	 */
	tegra_uart_wait_cycle_time(tup, 32);

	do {
		lsr = tegra_uart_read(tup, UART_LSR);
		if ((lsr | UART_LSR_TEMT) && !(lsr & UART_LSR_DR))
			break;
		udelay(1);
	} while (--tmout);

	if (tup->rts_active)
		set_rts(tup, true);
}

static long tegra_get_tolerance_rate(struct tegra_uart_port *tup,
				     unsigned int baud, long rate)
{
	int i;

	for (i = 0; i < tup->n_adjustable_baud_rates; ++i) {
		if (baud >= tup->baud_tolerance[i].lower_range_baud &&
		    baud <= tup->baud_tolerance[i].upper_range_baud)
			return (rate + (rate *
				tup->baud_tolerance[i].tolerance) / 10000);
	}

	return rate;
}

static int tegra_check_rate_in_range(struct tegra_uart_port *tup)
{
	long diff;

	diff = ((long)(tup->configured_rate - tup->required_rate) * 10000)
		/ tup->required_rate;
	if (diff < (tup->cdata->error_tolerance_low_range * 100) ||
	    diff > (tup->cdata->error_tolerance_high_range * 100)) {
		dev_err(tup->uport.dev,
			"configured baud rate is out of range by %ld", diff);
		return -EIO;
	}

	return 0;
}

static int tegra_set_baudrate(struct tegra_uart_port *tup, unsigned int baud)
{
	unsigned long rate;
	unsigned int divisor;
	unsigned long lcr;
	unsigned long flags;
	int ret;

	if (tup->current_baud == baud)
		return 0;

	if (tup->cdata->support_clk_src_div) {
		rate = baud * 16;
		tup->required_rate = rate;

		if (tup->n_adjustable_baud_rates)
			rate = tegra_get_tolerance_rate(tup, baud, rate);

		ret = clk_set_rate(tup->uart_clk, rate);
		if (ret < 0) {
			dev_err(tup->uport.dev,
				"clk_set_rate() failed for rate %lu\n", rate);
			return ret;
		}
		tup->configured_rate = clk_get_rate(tup->uart_clk);
		divisor = 1;
		ret = tegra_check_rate_in_range(tup);
		if (ret < 0)
			return ret;
	} else {
		rate = clk_get_rate(tup->uart_clk);
		divisor = DIV_ROUND_CLOSEST(rate, baud * 16);
	}

	spin_lock_irqsave(&tup->uport.lock, flags);
	lcr = tup->lcr_shadow;
	lcr |= UART_LCR_DLAB;
	tegra_uart_write(tup, lcr, UART_LCR);

	tegra_uart_write(tup, divisor & 0xFF, UART_TX);
	tegra_uart_write(tup, ((divisor >> 8) & 0xFF), UART_IER);

	lcr &= ~UART_LCR_DLAB;
	tegra_uart_write(tup, lcr, UART_LCR);

	/* Dummy read to ensure the write is posted */
	tegra_uart_read(tup, UART_SCR);
	spin_unlock_irqrestore(&tup->uport.lock, flags);

	tup->current_baud = baud;

	/* wait two character intervals at new rate */
	tegra_uart_wait_sym_time(tup, 2);
	return 0;
}

static char tegra_uart_decode_rx_error(struct tegra_uart_port *tup,
			unsigned long lsr)
{
	char flag = TTY_NORMAL;

	if (unlikely(lsr & TEGRA_UART_LSR_ANY)) {
		if (lsr & UART_LSR_OE) {
			/* Overrrun error */
			flag = TTY_OVERRUN;
			tup->uport.icount.overrun++;
			dev_dbg(tup->uport.dev, "Got overrun errors\n");
		} else if (lsr & UART_LSR_PE) {
			/* Parity error */
			flag = TTY_PARITY;
			tup->uport.icount.parity++;
			dev_dbg(tup->uport.dev, "Got Parity errors\n");
		} else if (lsr & UART_LSR_FE) {
			flag = TTY_FRAME;
			tup->uport.icount.frame++;
			dev_dbg(tup->uport.dev, "Got frame errors\n");
		} else if (lsr & UART_LSR_BI) {
			/*
			 * Break error
			 * If FIFO read error without any data, reset Rx FIFO
			 */
			if (!(lsr & UART_LSR_DR) && (lsr & UART_LSR_FIFOE))
				tegra_uart_fifo_reset(tup, UART_FCR_CLEAR_RCVR);
			if (tup->uport.ignore_status_mask & UART_LSR_BI)
				return TTY_BREAK;
			flag = TTY_BREAK;
			tup->uport.icount.brk++;
			dev_dbg(tup->uport.dev, "Got Break\n");
		}
		uart_insert_char(&tup->uport, lsr, UART_LSR_OE, 0, flag);
	}

	return flag;
}

static int tegra_uart_request_port(struct uart_port *u)
{
	return 0;
}

static void tegra_uart_release_port(struct uart_port *u)
{
	/* Nothing to do here */
}

static void tegra_uart_fill_tx_fifo(struct tegra_uart_port *tup, int max_bytes)
{
	struct circ_buf *xmit = &tup->uport.state->xmit;
	int i;

	for (i = 0; i < max_bytes; i++) {
		BUG_ON(uart_circ_empty(xmit));
		if (tup->cdata->tx_fifo_full_status) {
			unsigned long lsr = tegra_uart_read(tup, UART_LSR);
			if ((lsr & TEGRA_UART_LSR_TXFIFO_FULL))
				break;
		}
		tegra_uart_write(tup, xmit->buf[xmit->tail], UART_TX);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		tup->uport.icount.tx++;
	}
}

static void tegra_uart_start_pio_tx(struct tegra_uart_port *tup,
		unsigned int bytes)
{
	if (bytes > TEGRA_UART_MIN_DMA)
		bytes = TEGRA_UART_MIN_DMA;

	tup->tx_in_progress = TEGRA_UART_TX_PIO;
	tup->tx_bytes = bytes;
	tup->ier_shadow |= UART_IER_THRI;
	tegra_uart_write(tup, tup->ier_shadow, UART_IER);
}

static void tegra_uart_tx_dma_complete(void *args)
{
	struct tegra_uart_port *tup = args;
	struct circ_buf *xmit = &tup->uport.state->xmit;
	struct dma_tx_state state;
	unsigned long flags;
	unsigned int count;

	dmaengine_tx_status(tup->tx_dma_chan, tup->tx_cookie, &state);
	count = tup->tx_bytes_requested - state.residue;
	async_tx_ack(tup->tx_dma_desc);
	spin_lock_irqsave(&tup->uport.lock, flags);
	xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE - 1);
	tup->tx_in_progress = 0;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&tup->uport);
	tegra_uart_start_next_tx(tup);
	spin_unlock_irqrestore(&tup->uport.lock, flags);
}

static int tegra_uart_start_tx_dma(struct tegra_uart_port *tup,
		unsigned long count)
{
	struct circ_buf *xmit = &tup->uport.state->xmit;
	dma_addr_t tx_phys_addr;

	tup->tx_bytes = count & ~(0xF);
	tx_phys_addr = tup->tx_dma_buf_phys + xmit->tail;

	dma_sync_single_for_device(tup->uport.dev, tx_phys_addr,
				   tup->tx_bytes, DMA_TO_DEVICE);

	tup->tx_dma_desc = dmaengine_prep_slave_single(tup->tx_dma_chan,
				tx_phys_addr, tup->tx_bytes, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT);
	if (!tup->tx_dma_desc) {
		dev_err(tup->uport.dev, "Not able to get desc for Tx\n");
		return -EIO;
	}

	tup->tx_dma_desc->callback = tegra_uart_tx_dma_complete;
	tup->tx_dma_desc->callback_param = tup;
	tup->tx_in_progress = TEGRA_UART_TX_DMA;
	tup->tx_bytes_requested = tup->tx_bytes;
	tup->tx_cookie = dmaengine_submit(tup->tx_dma_desc);
	dma_async_issue_pending(tup->tx_dma_chan);
	return 0;
}

static void tegra_uart_start_next_tx(struct tegra_uart_port *tup)
{
	unsigned long tail;
	unsigned long count;
	struct circ_buf *xmit = &tup->uport.state->xmit;

	if (!tup->current_baud)
		return;

	tail = (unsigned long)&xmit->buf[xmit->tail];
	count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
	if (!count)
		return;

	if (tup->use_tx_pio || count < TEGRA_UART_MIN_DMA)
		tegra_uart_start_pio_tx(tup, count);
	else if (BYTES_TO_ALIGN(tail) > 0)
		tegra_uart_start_pio_tx(tup, BYTES_TO_ALIGN(tail));
	else
		tegra_uart_start_tx_dma(tup, count);
}

/* Called by serial core driver with u->lock taken. */
static void tegra_uart_start_tx(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	struct circ_buf *xmit = &u->state->xmit;

	if (!uart_circ_empty(xmit) && !tup->tx_in_progress)
		tegra_uart_start_next_tx(tup);
}

static unsigned int tegra_uart_tx_empty(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	unsigned int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&u->lock, flags);
	if (!tup->tx_in_progress) {
		unsigned long lsr = tegra_uart_read(tup, UART_LSR);
		if ((lsr & TX_EMPTY_STATUS) == TX_EMPTY_STATUS)
			ret = TIOCSER_TEMT;
	}
	spin_unlock_irqrestore(&u->lock, flags);
	return ret;
}

static void tegra_uart_stop_tx(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	struct circ_buf *xmit = &tup->uport.state->xmit;
	struct dma_tx_state state;
	unsigned int count;

	if (tup->tx_in_progress != TEGRA_UART_TX_DMA)
		return;

	dmaengine_terminate_all(tup->tx_dma_chan);
	dmaengine_tx_status(tup->tx_dma_chan, tup->tx_cookie, &state);
	count = tup->tx_bytes_requested - state.residue;
	async_tx_ack(tup->tx_dma_desc);
	xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE - 1);
	tup->tx_in_progress = 0;
}

static void tegra_uart_handle_tx_pio(struct tegra_uart_port *tup)
{
	struct circ_buf *xmit = &tup->uport.state->xmit;

	tegra_uart_fill_tx_fifo(tup, tup->tx_bytes);
	tup->tx_in_progress = 0;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&tup->uport);
	tegra_uart_start_next_tx(tup);
}

static void tegra_uart_handle_rx_pio(struct tegra_uart_port *tup,
		struct tty_port *port)
{
	do {
		char flag = TTY_NORMAL;
		unsigned long lsr = 0;
		unsigned char ch;

		lsr = tegra_uart_read(tup, UART_LSR);
		if (!(lsr & UART_LSR_DR))
			break;

		flag = tegra_uart_decode_rx_error(tup, lsr);
		if (flag != TTY_NORMAL)
			continue;

		ch = (unsigned char) tegra_uart_read(tup, UART_RX);
		tup->uport.icount.rx++;

		if (uart_handle_sysrq_char(&tup->uport, ch))
			continue;

		if (tup->uport.ignore_status_mask & UART_LSR_DR)
			continue;

		tty_insert_flip_char(port, ch, flag);
	} while (1);
}

static void tegra_uart_copy_rx_to_tty(struct tegra_uart_port *tup,
				      struct tty_port *port,
				      unsigned int count)
{
	int copied;

	/* If count is zero, then there is no data to be copied */
	if (!count)
		return;

	tup->uport.icount.rx += count;

	if (tup->uport.ignore_status_mask & UART_LSR_DR)
		return;

	dma_sync_single_for_cpu(tup->uport.dev, tup->rx_dma_buf_phys,
				count, DMA_FROM_DEVICE);
	copied = tty_insert_flip_string(port,
			((unsigned char *)(tup->rx_dma_buf_virt)), count);
	if (copied != count) {
		WARN_ON(1);
		dev_err(tup->uport.dev, "RxData copy to tty layer failed\n");
	}
	dma_sync_single_for_device(tup->uport.dev, tup->rx_dma_buf_phys,
				   count, DMA_TO_DEVICE);
}

static void do_handle_rx_pio(struct tegra_uart_port *tup)
{
	struct tty_struct *tty = tty_port_tty_get(&tup->uport.state->port);
	struct tty_port *port = &tup->uport.state->port;

	tegra_uart_handle_rx_pio(tup, port);
	if (tty) {
		tty_flip_buffer_push(port);
		tty_kref_put(tty);
	}
}

static void tegra_uart_rx_buffer_push(struct tegra_uart_port *tup,
				      unsigned int residue)
{
	struct tty_port *port = &tup->uport.state->port;
	unsigned int count;

	async_tx_ack(tup->rx_dma_desc);
	count = tup->rx_bytes_requested - residue;

	/* If we are here, DMA is stopped */
	tegra_uart_copy_rx_to_tty(tup, port, count);

	do_handle_rx_pio(tup);
}

static void tegra_uart_rx_dma_complete(void *args)
{
	struct tegra_uart_port *tup = args;
	struct uart_port *u = &tup->uport;
	unsigned long flags;
	struct dma_tx_state state;
	enum dma_status status;

	spin_lock_irqsave(&u->lock, flags);

	status = dmaengine_tx_status(tup->rx_dma_chan, tup->rx_cookie, &state);

	if (status == DMA_IN_PROGRESS) {
		dev_dbg(tup->uport.dev, "RX DMA is in progress\n");
		goto done;
	}

	/* Deactivate flow control to stop sender */
	if (tup->rts_active)
		set_rts(tup, false);

	tup->rx_dma_active = false;
	tegra_uart_rx_buffer_push(tup, 0);
	tegra_uart_start_rx_dma(tup);

	/* Activate flow control to start transfer */
	if (tup->rts_active)
		set_rts(tup, true);

done:
	spin_unlock_irqrestore(&u->lock, flags);
}

static void tegra_uart_terminate_rx_dma(struct tegra_uart_port *tup)
{
	struct dma_tx_state state;

	if (!tup->rx_dma_active) {
		do_handle_rx_pio(tup);
		return;
	}

	dmaengine_terminate_all(tup->rx_dma_chan);
	dmaengine_tx_status(tup->rx_dma_chan, tup->rx_cookie, &state);

	tegra_uart_rx_buffer_push(tup, state.residue);
	tup->rx_dma_active = false;
}

static void tegra_uart_handle_rx_dma(struct tegra_uart_port *tup)
{
	/* Deactivate flow control to stop sender */
	if (tup->rts_active)
		set_rts(tup, false);

	tegra_uart_terminate_rx_dma(tup);

	if (tup->rts_active)
		set_rts(tup, true);
}

static int tegra_uart_start_rx_dma(struct tegra_uart_port *tup)
{
	unsigned int count = TEGRA_UART_RX_DMA_BUFFER_SIZE;

	if (tup->rx_dma_active)
		return 0;

	tup->rx_dma_desc = dmaengine_prep_slave_single(tup->rx_dma_chan,
				tup->rx_dma_buf_phys, count, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT);
	if (!tup->rx_dma_desc) {
		dev_err(tup->uport.dev, "Not able to get desc for Rx\n");
		return -EIO;
	}

	tup->rx_dma_active = true;
	tup->rx_dma_desc->callback = tegra_uart_rx_dma_complete;
	tup->rx_dma_desc->callback_param = tup;
	tup->rx_bytes_requested = count;
	tup->rx_cookie = dmaengine_submit(tup->rx_dma_desc);
	dma_async_issue_pending(tup->rx_dma_chan);
	return 0;
}

static void tegra_uart_handle_modem_signal_change(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	unsigned long msr;

	msr = tegra_uart_read(tup, UART_MSR);
	if (!(msr & UART_MSR_ANY_DELTA))
		return;

	if (msr & UART_MSR_TERI)
		tup->uport.icount.rng++;
	if (msr & UART_MSR_DDSR)
		tup->uport.icount.dsr++;
	/* We may only get DDCD when HW init and reset */
	if (msr & UART_MSR_DDCD)
		uart_handle_dcd_change(&tup->uport, msr & UART_MSR_DCD);
	/* Will start/stop_tx accordingly */
	if (msr & UART_MSR_DCTS)
		uart_handle_cts_change(&tup->uport, msr & UART_MSR_CTS);
}

static irqreturn_t tegra_uart_isr(int irq, void *data)
{
	struct tegra_uart_port *tup = data;
	struct uart_port *u = &tup->uport;
	unsigned long iir;
	unsigned long ier;
	bool is_rx_start = false;
	bool is_rx_int = false;
	unsigned long flags;

	spin_lock_irqsave(&u->lock, flags);
	while (1) {
		iir = tegra_uart_read(tup, UART_IIR);
		if (iir & UART_IIR_NO_INT) {
			if (!tup->use_rx_pio && is_rx_int) {
				tegra_uart_handle_rx_dma(tup);
				if (tup->rx_in_progress) {
					ier = tup->ier_shadow;
					ier |= (UART_IER_RLSI | UART_IER_RTOIE |
						TEGRA_UART_IER_EORD | UART_IER_RDI);
					tup->ier_shadow = ier;
					tegra_uart_write(tup, ier, UART_IER);
				}
			} else if (is_rx_start) {
				tegra_uart_start_rx_dma(tup);
			}
			spin_unlock_irqrestore(&u->lock, flags);
			return IRQ_HANDLED;
		}

		switch ((iir >> 1) & 0x7) {
		case 0: /* Modem signal change interrupt */
			tegra_uart_handle_modem_signal_change(u);
			break;

		case 1: /* Transmit interrupt only triggered when using PIO */
			tup->ier_shadow &= ~UART_IER_THRI;
			tegra_uart_write(tup, tup->ier_shadow, UART_IER);
			tegra_uart_handle_tx_pio(tup);
			break;

		case 4: /* End of data */
		case 6: /* Rx timeout */
			if (!tup->use_rx_pio) {
				is_rx_int = tup->rx_in_progress;
				/* Disable Rx interrupts */
				ier = tup->ier_shadow;
				ier &= ~(UART_IER_RDI | UART_IER_RLSI |
					UART_IER_RTOIE | TEGRA_UART_IER_EORD);
				tup->ier_shadow = ier;
				tegra_uart_write(tup, ier, UART_IER);
				break;
			}
			fallthrough;
		case 2: /* Receive */
			if (!tup->use_rx_pio) {
				is_rx_start = tup->rx_in_progress;
				tup->ier_shadow  &= ~UART_IER_RDI;
				tegra_uart_write(tup, tup->ier_shadow,
						 UART_IER);
			} else {
				do_handle_rx_pio(tup);
			}
			break;

		case 3: /* Receive error */
			tegra_uart_decode_rx_error(tup,
					tegra_uart_read(tup, UART_LSR));
			break;

		case 5: /* break nothing to handle */
		case 7: /* break nothing to handle */
			break;
		}
	}
}

static void tegra_uart_stop_rx(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	struct tty_port *port = &tup->uport.state->port;
	unsigned long ier;

	if (tup->rts_active)
		set_rts(tup, false);

	if (!tup->rx_in_progress)
		return;

	tegra_uart_wait_sym_time(tup, 1); /* wait one character interval */

	ier = tup->ier_shadow;
	ier &= ~(UART_IER_RDI | UART_IER_RLSI | UART_IER_RTOIE |
					TEGRA_UART_IER_EORD);
	tup->ier_shadow = ier;
	tegra_uart_write(tup, ier, UART_IER);
	tup->rx_in_progress = 0;

	if (!tup->use_rx_pio)
		tegra_uart_terminate_rx_dma(tup);
	else
		tegra_uart_handle_rx_pio(tup, port);
}

static void tegra_uart_hw_deinit(struct tegra_uart_port *tup)
{
	unsigned long flags;
	unsigned long char_time = DIV_ROUND_UP(10000000, tup->current_baud);
	unsigned long fifo_empty_time = tup->uport.fifosize * char_time;
	unsigned long wait_time;
	unsigned long lsr;
	unsigned long msr;
	unsigned long mcr;

	/* Disable interrupts */
	tegra_uart_write(tup, 0, UART_IER);

	lsr = tegra_uart_read(tup, UART_LSR);
	if ((lsr & UART_LSR_TEMT) != UART_LSR_TEMT) {
		msr = tegra_uart_read(tup, UART_MSR);
		mcr = tegra_uart_read(tup, UART_MCR);
		if ((mcr & TEGRA_UART_MCR_CTS_EN) && (msr & UART_MSR_CTS))
			dev_err(tup->uport.dev,
				"Tx Fifo not empty, CTS disabled, waiting\n");

		/* Wait for Tx fifo to be empty */
		while ((lsr & UART_LSR_TEMT) != UART_LSR_TEMT) {
			wait_time = min(fifo_empty_time, 100lu);
			udelay(wait_time);
			fifo_empty_time -= wait_time;
			if (!fifo_empty_time) {
				msr = tegra_uart_read(tup, UART_MSR);
				mcr = tegra_uart_read(tup, UART_MCR);
				if ((mcr & TEGRA_UART_MCR_CTS_EN) &&
					(msr & UART_MSR_CTS))
					dev_err(tup->uport.dev,
						"Slave not ready\n");
				break;
			}
			lsr = tegra_uart_read(tup, UART_LSR);
		}
	}

	spin_lock_irqsave(&tup->uport.lock, flags);
	/* Reset the Rx and Tx FIFOs */
	tegra_uart_fifo_reset(tup, UART_FCR_CLEAR_XMIT | UART_FCR_CLEAR_RCVR);
	tup->current_baud = 0;
	spin_unlock_irqrestore(&tup->uport.lock, flags);

	tup->rx_in_progress = 0;
	tup->tx_in_progress = 0;

	if (!tup->use_rx_pio)
		tegra_uart_dma_channel_free(tup, true);
	if (!tup->use_tx_pio)
		tegra_uart_dma_channel_free(tup, false);

	clk_disable_unprepare(tup->uart_clk);
}

static int tegra_uart_hw_init(struct tegra_uart_port *tup)
{
	int ret;

	tup->fcr_shadow = 0;
	tup->mcr_shadow = 0;
	tup->lcr_shadow = 0;
	tup->ier_shadow = 0;
	tup->current_baud = 0;

	clk_prepare_enable(tup->uart_clk);

	/* Reset the UART controller to clear all previous status.*/
	reset_control_assert(tup->rst);
	udelay(10);
	reset_control_deassert(tup->rst);

	tup->rx_in_progress = 0;
	tup->tx_in_progress = 0;

	/*
	 * Set the trigger level
	 *
	 * For PIO mode:
	 *
	 * For receive, this will interrupt the CPU after that many number of
	 * bytes are received, for the remaining bytes the receive timeout
	 * interrupt is received. Rx high watermark is set to 4.
	 *
	 * For transmit, if the trasnmit interrupt is enabled, this will
	 * interrupt the CPU when the number of entries in the FIFO reaches the
	 * low watermark. Tx low watermark is set to 16 bytes.
	 *
	 * For DMA mode:
	 *
	 * Set the Tx trigger to 16. This should match the DMA burst size that
	 * programmed in the DMA registers.
	 */
	tup->fcr_shadow = UART_FCR_ENABLE_FIFO;

	if (tup->use_rx_pio) {
		tup->fcr_shadow |= UART_FCR_R_TRIG_11;
	} else {
		if (tup->cdata->max_dma_burst_bytes == 8)
			tup->fcr_shadow |= UART_FCR_R_TRIG_10;
		else
			tup->fcr_shadow |= UART_FCR_R_TRIG_01;
	}

	tup->fcr_shadow |= TEGRA_UART_TX_TRIG_16B;
	tegra_uart_write(tup, tup->fcr_shadow, UART_FCR);

	/* Dummy read to ensure the write is posted */
	tegra_uart_read(tup, UART_SCR);

	if (tup->cdata->fifo_mode_enable_status) {
		ret = tegra_uart_wait_fifo_mode_enabled(tup);
		dev_err(tup->uport.dev, "FIFO mode not enabled\n");
		if (ret < 0)
			return ret;
	} else {
		/*
		 * For all tegra devices (up to t210), there is a hardware
		 * issue that requires software to wait for 3 UART clock
		 * periods after enabling the TX fifo, otherwise data could
		 * be lost.
		 */
		tegra_uart_wait_cycle_time(tup, 3);
	}

	/*
	 * Initialize the UART with default configuration
	 * (115200, N, 8, 1) so that the receive DMA buffer may be
	 * enqueued
	 */
	ret = tegra_set_baudrate(tup, TEGRA_UART_DEFAULT_BAUD);
	if (ret < 0) {
		dev_err(tup->uport.dev, "Failed to set baud rate\n");
		return ret;
	}
	if (!tup->use_rx_pio) {
		tup->lcr_shadow = TEGRA_UART_DEFAULT_LSR;
		tup->fcr_shadow |= UART_FCR_DMA_SELECT;
		tegra_uart_write(tup, tup->fcr_shadow, UART_FCR);
	} else {
		tegra_uart_write(tup, tup->fcr_shadow, UART_FCR);
	}
	tup->rx_in_progress = 1;

	/*
	 * Enable IE_RXS for the receive status interrupts like line errros.
	 * Enable IE_RX_TIMEOUT to get the bytes which cannot be DMA'd.
	 *
	 * EORD is different interrupt than RX_TIMEOUT - RX_TIMEOUT occurs when
	 * the DATA is sitting in the FIFO and couldn't be transferred to the
	 * DMA as the DMA size alignment (4 bytes) is not met. EORD will be
	 * triggered when there is a pause of the incomming data stream for 4
	 * characters long.
	 *
	 * For pauses in the data which is not aligned to 4 bytes, we get
	 * both the EORD as well as RX_TIMEOUT - SW sees RX_TIMEOUT first
	 * then the EORD.
	 */
	tup->ier_shadow = UART_IER_RLSI | UART_IER_RTOIE | UART_IER_RDI;

	/*
	 * If using DMA mode, enable EORD interrupt to notify about RX
	 * completion.
	 */
	if (!tup->use_rx_pio)
		tup->ier_shadow |= TEGRA_UART_IER_EORD;

	tegra_uart_write(tup, tup->ier_shadow, UART_IER);
	return 0;
}

static void tegra_uart_dma_channel_free(struct tegra_uart_port *tup,
		bool dma_to_memory)
{
	if (dma_to_memory) {
		dmaengine_terminate_all(tup->rx_dma_chan);
		dma_release_channel(tup->rx_dma_chan);
		dma_free_coherent(tup->uport.dev, TEGRA_UART_RX_DMA_BUFFER_SIZE,
				tup->rx_dma_buf_virt, tup->rx_dma_buf_phys);
		tup->rx_dma_chan = NULL;
		tup->rx_dma_buf_phys = 0;
		tup->rx_dma_buf_virt = NULL;
	} else {
		dmaengine_terminate_all(tup->tx_dma_chan);
		dma_release_channel(tup->tx_dma_chan);
		dma_unmap_single(tup->uport.dev, tup->tx_dma_buf_phys,
			UART_XMIT_SIZE, DMA_TO_DEVICE);
		tup->tx_dma_chan = NULL;
		tup->tx_dma_buf_phys = 0;
		tup->tx_dma_buf_virt = NULL;
	}
}

static int tegra_uart_dma_channel_allocate(struct tegra_uart_port *tup,
			bool dma_to_memory)
{
	struct dma_chan *dma_chan;
	unsigned char *dma_buf;
	dma_addr_t dma_phys;
	int ret;
	struct dma_slave_config dma_sconfig;

	dma_chan = dma_request_chan(tup->uport.dev, dma_to_memory ? "rx" : "tx");
	if (IS_ERR(dma_chan)) {
		ret = PTR_ERR(dma_chan);
		dev_err(tup->uport.dev,
			"DMA channel alloc failed: %d\n", ret);
		return ret;
	}

	if (dma_to_memory) {
		dma_buf = dma_alloc_coherent(tup->uport.dev,
				TEGRA_UART_RX_DMA_BUFFER_SIZE,
				 &dma_phys, GFP_KERNEL);
		if (!dma_buf) {
			dev_err(tup->uport.dev,
				"Not able to allocate the dma buffer\n");
			dma_release_channel(dma_chan);
			return -ENOMEM;
		}
		dma_sync_single_for_device(tup->uport.dev, dma_phys,
					   TEGRA_UART_RX_DMA_BUFFER_SIZE,
					   DMA_TO_DEVICE);
		dma_sconfig.src_addr = tup->uport.mapbase;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		dma_sconfig.src_maxburst = tup->cdata->max_dma_burst_bytes;
		tup->rx_dma_chan = dma_chan;
		tup->rx_dma_buf_virt = dma_buf;
		tup->rx_dma_buf_phys = dma_phys;
	} else {
		dma_phys = dma_map_single(tup->uport.dev,
			tup->uport.state->xmit.buf, UART_XMIT_SIZE,
			DMA_TO_DEVICE);
		if (dma_mapping_error(tup->uport.dev, dma_phys)) {
			dev_err(tup->uport.dev, "dma_map_single tx failed\n");
			dma_release_channel(dma_chan);
			return -ENOMEM;
		}
		dma_buf = tup->uport.state->xmit.buf;
		dma_sconfig.dst_addr = tup->uport.mapbase;
		dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		dma_sconfig.dst_maxburst = 16;
		tup->tx_dma_chan = dma_chan;
		tup->tx_dma_buf_virt = dma_buf;
		tup->tx_dma_buf_phys = dma_phys;
	}

	ret = dmaengine_slave_config(dma_chan, &dma_sconfig);
	if (ret < 0) {
		dev_err(tup->uport.dev,
			"Dma slave config failed, err = %d\n", ret);
		tegra_uart_dma_channel_free(tup, dma_to_memory);
		return ret;
	}

	return 0;
}

static int tegra_uart_startup(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	int ret;

	if (!tup->use_tx_pio) {
		ret = tegra_uart_dma_channel_allocate(tup, false);
		if (ret < 0) {
			dev_err(u->dev, "Tx Dma allocation failed, err = %d\n",
				ret);
			return ret;
		}
	}

	if (!tup->use_rx_pio) {
		ret = tegra_uart_dma_channel_allocate(tup, true);
		if (ret < 0) {
			dev_err(u->dev, "Rx Dma allocation failed, err = %d\n",
				ret);
			goto fail_rx_dma;
		}
	}

	ret = tegra_uart_hw_init(tup);
	if (ret < 0) {
		dev_err(u->dev, "Uart HW init failed, err = %d\n", ret);
		goto fail_hw_init;
	}

	ret = request_irq(u->irq, tegra_uart_isr, 0,
				dev_name(u->dev), tup);
	if (ret < 0) {
		dev_err(u->dev, "Failed to register ISR for IRQ %d\n", u->irq);
		goto fail_hw_init;
	}
	return 0;

fail_hw_init:
	if (!tup->use_rx_pio)
		tegra_uart_dma_channel_free(tup, true);
fail_rx_dma:
	if (!tup->use_tx_pio)
		tegra_uart_dma_channel_free(tup, false);
	return ret;
}

/*
 * Flush any TX data submitted for DMA and PIO. Called when the
 * TX circular buffer is reset.
 */
static void tegra_uart_flush_buffer(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);

	tup->tx_bytes = 0;
	if (tup->tx_dma_chan)
		dmaengine_terminate_all(tup->tx_dma_chan);
}

static void tegra_uart_shutdown(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);

	tegra_uart_hw_deinit(tup);
	free_irq(u->irq, tup);
}

static void tegra_uart_enable_ms(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);

	if (tup->enable_modem_interrupt) {
		tup->ier_shadow |= UART_IER_MSI;
		tegra_uart_write(tup, tup->ier_shadow, UART_IER);
	}
}

static void tegra_uart_set_termios(struct uart_port *u,
		struct ktermios *termios, struct ktermios *oldtermios)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	unsigned int baud;
	unsigned long flags;
	unsigned int lcr;
	int symb_bit = 1;
	struct clk *parent_clk = clk_get_parent(tup->uart_clk);
	unsigned long parent_clk_rate = clk_get_rate(parent_clk);
	int max_divider = (tup->cdata->support_clk_src_div) ? 0x7FFF : 0xFFFF;
	int ret;

	max_divider *= 16;
	spin_lock_irqsave(&u->lock, flags);

	/* Changing configuration, it is safe to stop any rx now */
	if (tup->rts_active)
		set_rts(tup, false);

	/* Clear all interrupts as configuration is going to be changed */
	tegra_uart_write(tup, tup->ier_shadow | UART_IER_RDI, UART_IER);
	tegra_uart_read(tup, UART_IER);
	tegra_uart_write(tup, 0, UART_IER);
	tegra_uart_read(tup, UART_IER);

	/* Parity */
	lcr = tup->lcr_shadow;
	lcr &= ~UART_LCR_PARITY;

	/* CMSPAR isn't supported by this driver */
	termios->c_cflag &= ~CMSPAR;

	if ((termios->c_cflag & PARENB) == PARENB) {
		symb_bit++;
		if (termios->c_cflag & PARODD) {
			lcr |= UART_LCR_PARITY;
			lcr &= ~UART_LCR_EPAR;
			lcr &= ~UART_LCR_SPAR;
		} else {
			lcr |= UART_LCR_PARITY;
			lcr |= UART_LCR_EPAR;
			lcr &= ~UART_LCR_SPAR;
		}
	}

	lcr &= ~UART_LCR_WLEN8;
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr |= UART_LCR_WLEN5;
		symb_bit += 5;
		break;
	case CS6:
		lcr |= UART_LCR_WLEN6;
		symb_bit += 6;
		break;
	case CS7:
		lcr |= UART_LCR_WLEN7;
		symb_bit += 7;
		break;
	default:
		lcr |= UART_LCR_WLEN8;
		symb_bit += 8;
		break;
	}

	/* Stop bits */
	if (termios->c_cflag & CSTOPB) {
		lcr |= UART_LCR_STOP;
		symb_bit += 2;
	} else {
		lcr &= ~UART_LCR_STOP;
		symb_bit++;
	}

	tegra_uart_write(tup, lcr, UART_LCR);
	tup->lcr_shadow = lcr;
	tup->symb_bit = symb_bit;

	/* Baud rate. */
	baud = uart_get_baud_rate(u, termios, oldtermios,
			parent_clk_rate/max_divider,
			parent_clk_rate/16);
	spin_unlock_irqrestore(&u->lock, flags);
	ret = tegra_set_baudrate(tup, baud);
	if (ret < 0) {
		dev_err(tup->uport.dev, "Failed to set baud rate\n");
		return;
	}
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);
	spin_lock_irqsave(&u->lock, flags);

	/* Flow control */
	if (termios->c_cflag & CRTSCTS)	{
		tup->mcr_shadow |= TEGRA_UART_MCR_CTS_EN;
		tup->mcr_shadow &= ~TEGRA_UART_MCR_RTS_EN;
		tegra_uart_write(tup, tup->mcr_shadow, UART_MCR);
		/* if top layer has asked to set rts active then do so here */
		if (tup->rts_active)
			set_rts(tup, true);
	} else {
		tup->mcr_shadow &= ~TEGRA_UART_MCR_CTS_EN;
		tup->mcr_shadow &= ~TEGRA_UART_MCR_RTS_EN;
		tegra_uart_write(tup, tup->mcr_shadow, UART_MCR);
	}

	/* update the port timeout based on new settings */
	uart_update_timeout(u, termios->c_cflag, baud);

	/* Make sure all writes have completed */
	tegra_uart_read(tup, UART_IER);

	/* Re-enable interrupt */
	tegra_uart_write(tup, tup->ier_shadow, UART_IER);
	tegra_uart_read(tup, UART_IER);

	tup->uport.ignore_status_mask = 0;
	/* Ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		tup->uport.ignore_status_mask |= UART_LSR_DR;
	if (termios->c_iflag & IGNBRK)
		tup->uport.ignore_status_mask |= UART_LSR_BI;

	spin_unlock_irqrestore(&u->lock, flags);
}

static const char *tegra_uart_type(struct uart_port *u)
{
	return TEGRA_UART_TYPE;
}

static const struct uart_ops tegra_uart_ops = {
	.tx_empty	= tegra_uart_tx_empty,
	.set_mctrl	= tegra_uart_set_mctrl,
	.get_mctrl	= tegra_uart_get_mctrl,
	.stop_tx	= tegra_uart_stop_tx,
	.start_tx	= tegra_uart_start_tx,
	.stop_rx	= tegra_uart_stop_rx,
	.flush_buffer	= tegra_uart_flush_buffer,
	.enable_ms	= tegra_uart_enable_ms,
	.break_ctl	= tegra_uart_break_ctl,
	.startup	= tegra_uart_startup,
	.shutdown	= tegra_uart_shutdown,
	.set_termios	= tegra_uart_set_termios,
	.type		= tegra_uart_type,
	.request_port	= tegra_uart_request_port,
	.release_port	= tegra_uart_release_port,
};

static struct uart_driver tegra_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "tegra_hsuart",
	.dev_name	= "ttyTHS",
	.cons		= NULL,
	.nr		= TEGRA_UART_MAXIMUM,
};

static int tegra_uart_parse_dt(struct platform_device *pdev,
	struct tegra_uart_port *tup)
{
	struct device_node *np = pdev->dev.of_node;
	int port;
	int ret;
	int index;
	u32 pval;
	int count;
	int n_entries;

	port = of_alias_get_id(np, "serial");
	if (port < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", port);
		return port;
	}
	tup->uport.line = port;

	tup->enable_modem_interrupt = of_property_read_bool(np,
					"nvidia,enable-modem-interrupt");

	index = of_property_match_string(np, "dma-names", "rx");
	if (index < 0) {
		tup->use_rx_pio = true;
		dev_info(&pdev->dev, "RX in PIO mode\n");
	}
	index = of_property_match_string(np, "dma-names", "tx");
	if (index < 0) {
		tup->use_tx_pio = true;
		dev_info(&pdev->dev, "TX in PIO mode\n");
	}

	n_entries = of_property_count_u32_elems(np, "nvidia,adjust-baud-rates");
	if (n_entries > 0) {
		tup->n_adjustable_baud_rates = n_entries / 3;
		tup->baud_tolerance =
		devm_kzalloc(&pdev->dev, (tup->n_adjustable_baud_rates) *
			     sizeof(*tup->baud_tolerance), GFP_KERNEL);
		if (!tup->baud_tolerance)
			return -ENOMEM;
		for (count = 0, index = 0; count < n_entries; count += 3,
		     index++) {
			ret =
			of_property_read_u32_index(np,
						   "nvidia,adjust-baud-rates",
						   count, &pval);
			if (!ret)
				tup->baud_tolerance[index].lower_range_baud =
				pval;
			ret =
			of_property_read_u32_index(np,
						   "nvidia,adjust-baud-rates",
						   count + 1, &pval);
			if (!ret)
				tup->baud_tolerance[index].upper_range_baud =
				pval;
			ret =
			of_property_read_u32_index(np,
						   "nvidia,adjust-baud-rates",
						   count + 2, &pval);
			if (!ret)
				tup->baud_tolerance[index].tolerance =
				(s32)pval;
		}
	} else {
		tup->n_adjustable_baud_rates = 0;
	}

	return 0;
}

static struct tegra_uart_chip_data tegra20_uart_chip_data = {
	.tx_fifo_full_status		= false,
	.allow_txfifo_reset_fifo_mode	= true,
	.support_clk_src_div		= false,
	.fifo_mode_enable_status	= false,
	.uart_max_port			= 5,
	.max_dma_burst_bytes		= 4,
	.error_tolerance_low_range	= 0,
	.error_tolerance_high_range	= 4,
};

static struct tegra_uart_chip_data tegra30_uart_chip_data = {
	.tx_fifo_full_status		= true,
	.allow_txfifo_reset_fifo_mode	= false,
	.support_clk_src_div		= true,
	.fifo_mode_enable_status	= false,
	.uart_max_port			= 5,
	.max_dma_burst_bytes		= 4,
	.error_tolerance_low_range	= 0,
	.error_tolerance_high_range	= 4,
};

static struct tegra_uart_chip_data tegra186_uart_chip_data = {
	.tx_fifo_full_status		= true,
	.allow_txfifo_reset_fifo_mode	= false,
	.support_clk_src_div		= true,
	.fifo_mode_enable_status	= true,
	.uart_max_port			= 8,
	.max_dma_burst_bytes		= 8,
	.error_tolerance_low_range	= 0,
	.error_tolerance_high_range	= 4,
};

static struct tegra_uart_chip_data tegra194_uart_chip_data = {
	.tx_fifo_full_status		= true,
	.allow_txfifo_reset_fifo_mode	= false,
	.support_clk_src_div		= true,
	.fifo_mode_enable_status	= true,
	.uart_max_port			= 8,
	.max_dma_burst_bytes		= 8,
	.error_tolerance_low_range	= -2,
	.error_tolerance_high_range	= 2,
};

static const struct of_device_id tegra_uart_of_match[] = {
	{
		.compatible	= "nvidia,tegra30-hsuart",
		.data		= &tegra30_uart_chip_data,
	}, {
		.compatible	= "nvidia,tegra20-hsuart",
		.data		= &tegra20_uart_chip_data,
	}, {
		.compatible     = "nvidia,tegra186-hsuart",
		.data		= &tegra186_uart_chip_data,
	}, {
		.compatible     = "nvidia,tegra194-hsuart",
		.data		= &tegra194_uart_chip_data,
	}, {
	},
};
MODULE_DEVICE_TABLE(of, tegra_uart_of_match);

static int tegra_uart_probe(struct platform_device *pdev)
{
	struct tegra_uart_port *tup;
	struct uart_port *u;
	struct resource *resource;
	int ret;
	const struct tegra_uart_chip_data *cdata;
	const struct of_device_id *match;

	match = of_match_device(tegra_uart_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "Error: No device match found\n");
		return -ENODEV;
	}
	cdata = match->data;

	tup = devm_kzalloc(&pdev->dev, sizeof(*tup), GFP_KERNEL);
	if (!tup) {
		dev_err(&pdev->dev, "Failed to allocate memory for tup\n");
		return -ENOMEM;
	}

	ret = tegra_uart_parse_dt(pdev, tup);
	if (ret < 0)
		return ret;

	u = &tup->uport;
	u->dev = &pdev->dev;
	u->ops = &tegra_uart_ops;
	u->type = PORT_TEGRA;
	u->fifosize = 32;
	tup->cdata = cdata;

	platform_set_drvdata(pdev, tup);
	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!resource) {
		dev_err(&pdev->dev, "No IO memory resource\n");
		return -ENODEV;
	}

	u->mapbase = resource->start;
	u->membase = devm_ioremap_resource(&pdev->dev, resource);
	if (IS_ERR(u->membase))
		return PTR_ERR(u->membase);

	tup->uart_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(tup->uart_clk)) {
		dev_err(&pdev->dev, "Couldn't get the clock\n");
		return PTR_ERR(tup->uart_clk);
	}

	tup->rst = devm_reset_control_get_exclusive(&pdev->dev, "serial");
	if (IS_ERR(tup->rst)) {
		dev_err(&pdev->dev, "Couldn't get the reset\n");
		return PTR_ERR(tup->rst);
	}

	u->iotype = UPIO_MEM32;
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	u->irq = ret;
	u->regshift = 2;
	ret = uart_add_one_port(&tegra_uart_driver, u);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add uart port, err %d\n", ret);
		return ret;
	}
	return ret;
}

static int tegra_uart_remove(struct platform_device *pdev)
{
	struct tegra_uart_port *tup = platform_get_drvdata(pdev);
	struct uart_port *u = &tup->uport;

	uart_remove_one_port(&tegra_uart_driver, u);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_uart_suspend(struct device *dev)
{
	struct tegra_uart_port *tup = dev_get_drvdata(dev);
	struct uart_port *u = &tup->uport;

	return uart_suspend_port(&tegra_uart_driver, u);
}

static int tegra_uart_resume(struct device *dev)
{
	struct tegra_uart_port *tup = dev_get_drvdata(dev);
	struct uart_port *u = &tup->uport;

	return uart_resume_port(&tegra_uart_driver, u);
}
#endif

static const struct dev_pm_ops tegra_uart_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra_uart_suspend, tegra_uart_resume)
};

static struct platform_driver tegra_uart_platform_driver = {
	.probe		= tegra_uart_probe,
	.remove		= tegra_uart_remove,
	.driver		= {
		.name	= "serial-tegra",
		.of_match_table = tegra_uart_of_match,
		.pm	= &tegra_uart_pm_ops,
	},
};

static int __init tegra_uart_init(void)
{
	int ret;
	struct device_node *node;
	const struct of_device_id *match = NULL;
	const struct tegra_uart_chip_data *cdata = NULL;

	node = of_find_matching_node(NULL, tegra_uart_of_match);
	if (node)
		match = of_match_node(tegra_uart_of_match, node);
	if (match)
		cdata = match->data;
	if (cdata)
		tegra_uart_driver.nr = cdata->uart_max_port;

	ret = uart_register_driver(&tegra_uart_driver);
	if (ret < 0) {
		pr_err("Could not register %s driver\n",
		       tegra_uart_driver.driver_name);
		return ret;
	}

	ret = platform_driver_register(&tegra_uart_platform_driver);
	if (ret < 0) {
		pr_err("Uart platform driver register failed, e = %d\n", ret);
		uart_unregister_driver(&tegra_uart_driver);
		return ret;
	}
	return 0;
}

static void __exit tegra_uart_exit(void)
{
	pr_info("Unloading tegra uart driver\n");
	platform_driver_unregister(&tegra_uart_platform_driver);
	uart_unregister_driver(&tegra_uart_driver);
}

module_init(tegra_uart_init);
module_exit(tegra_uart_exit);

MODULE_ALIAS("platform:serial-tegra");
MODULE_DESCRIPTION("High speed UART driver for tegra chipset");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
