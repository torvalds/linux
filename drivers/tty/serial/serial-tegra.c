/*
 * serial_tegra.c
 *
 * High-speed serial driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2012-2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
#include <linux/serial.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include <linux/clk/tegra.h>

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

#define TEGRA_UART_MAXIMUM			5

/* Default UART setting when started: 115200 no parity, stop, 8 data bits */
#define TEGRA_UART_DEFAULT_BAUD			115200
#define TEGRA_UART_DEFAULT_LSR			UART_LCR_WLEN8

/* Tx transfer mode */
#define TEGRA_TX_PIO				1
#define TEGRA_TX_DMA				2

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
};

struct tegra_uart_port {
	struct uart_port			uport;
	const struct tegra_uart_chip_data	*cdata;

	struct clk				*uart_clk;
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
	int					dma_req_sel;

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
	int					tx_bytes_requested;
	int					rx_bytes_requested;
};

static void tegra_uart_start_next_tx(struct tegra_uart_port *tup);
static int tegra_uart_start_rx_dma(struct tegra_uart_port *tup);

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
	return;
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
	return;
}

static void tegra_uart_set_mctrl(struct uart_port *u, unsigned int mctrl)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	unsigned long mcr;
	int dtr_enable;

	mcr = tup->mcr_shadow;
	tup->rts_active = !!(mctrl & TIOCM_RTS);
	set_rts(tup, tup->rts_active);

	dtr_enable = !!(mctrl & TIOCM_DTR);
	set_dtr(tup, dtr_enable);
	return;
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

/* Wait for a symbol-time. */
static void tegra_uart_wait_sym_time(struct tegra_uart_port *tup,
		unsigned int syms)
{
	if (tup->current_baud)
		udelay(DIV_ROUND_UP(syms * tup->symb_bit * 1000000,
			tup->current_baud));
}

static void tegra_uart_fifo_reset(struct tegra_uart_port *tup, u8 fcr_bits)
{
	unsigned long fcr = tup->fcr_shadow;

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
	}

	/* Dummy read to ensure the write is posted */
	tegra_uart_read(tup, UART_SCR);

	/* Wait for the flush to propagate. */
	tegra_uart_wait_sym_time(tup, 1);
}

static int tegra_set_baudrate(struct tegra_uart_port *tup, unsigned int baud)
{
	unsigned long rate;
	unsigned int divisor;
	unsigned long lcr;
	int ret;

	if (tup->current_baud == baud)
		return 0;

	if (tup->cdata->support_clk_src_div) {
		rate = baud * 16;
		ret = clk_set_rate(tup->uart_clk, rate);
		if (ret < 0) {
			dev_err(tup->uport.dev,
				"clk_set_rate() failed for rate %lu\n", rate);
			return ret;
		}
		divisor = 1;
	} else {
		rate = clk_get_rate(tup->uart_clk);
		divisor = DIV_ROUND_CLOSEST(rate, baud * 16);
	}

	lcr = tup->lcr_shadow;
	lcr |= UART_LCR_DLAB;
	tegra_uart_write(tup, lcr, UART_LCR);

	tegra_uart_write(tup, divisor & 0xFF, UART_TX);
	tegra_uart_write(tup, ((divisor >> 8) & 0xFF), UART_IER);

	lcr &= ~UART_LCR_DLAB;
	tegra_uart_write(tup, lcr, UART_LCR);

	/* Dummy read to ensure the write is posted */
	tegra_uart_read(tup, UART_SCR);

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
			flag |= TTY_OVERRUN;
			tup->uport.icount.overrun++;
			dev_err(tup->uport.dev, "Got overrun errors\n");
		} else if (lsr & UART_LSR_PE) {
			/* Parity error */
			flag |= TTY_PARITY;
			tup->uport.icount.parity++;
			dev_err(tup->uport.dev, "Got Parity errors\n");
		} else if (lsr & UART_LSR_FE) {
			flag |= TTY_FRAME;
			tup->uport.icount.frame++;
			dev_err(tup->uport.dev, "Got frame errors\n");
		} else if (lsr & UART_LSR_BI) {
			dev_err(tup->uport.dev, "Got Break\n");
			tup->uport.icount.brk++;
			/* If FIFO read error without any data, reset Rx FIFO */
			if (!(lsr & UART_LSR_DR) && (lsr & UART_LSR_FIFOE))
				tegra_uart_fifo_reset(tup, UART_FCR_CLEAR_RCVR);
		}
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
	int count;

	dmaengine_tx_status(tup->tx_dma_chan, tup->rx_cookie, &state);
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

	dma_sync_single_for_device(tup->uport.dev, tup->tx_dma_buf_phys,
				UART_XMIT_SIZE, DMA_TO_DEVICE);

	tup->tx_bytes = count & ~(0xF);
	tx_phys_addr = tup->tx_dma_buf_phys + xmit->tail;
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

	tail = (unsigned long)&xmit->buf[xmit->tail];
	count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
	if (!count)
		return;

	if (count < TEGRA_UART_MIN_DMA)
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
	int count;

	dmaengine_terminate_all(tup->tx_dma_chan);
	dmaengine_tx_status(tup->tx_dma_chan, tup->tx_cookie, &state);
	count = tup->tx_bytes_requested - state.residue;
	async_tx_ack(tup->tx_dma_desc);
	xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE - 1);
	tup->tx_in_progress = 0;
	return;
}

static void tegra_uart_handle_tx_pio(struct tegra_uart_port *tup)
{
	struct circ_buf *xmit = &tup->uport.state->xmit;

	tegra_uart_fill_tx_fifo(tup, tup->tx_bytes);
	tup->tx_in_progress = 0;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&tup->uport);
	tegra_uart_start_next_tx(tup);
	return;
}

static void tegra_uart_handle_rx_pio(struct tegra_uart_port *tup,
		struct tty_port *tty)
{
	do {
		char flag = TTY_NORMAL;
		unsigned long lsr = 0;
		unsigned char ch;

		lsr = tegra_uart_read(tup, UART_LSR);
		if (!(lsr & UART_LSR_DR))
			break;

		flag = tegra_uart_decode_rx_error(tup, lsr);
		ch = (unsigned char) tegra_uart_read(tup, UART_RX);
		tup->uport.icount.rx++;

		if (!uart_handle_sysrq_char(&tup->uport, ch) && tty)
			tty_insert_flip_char(tty, ch, flag);
	} while (1);

	return;
}

static void tegra_uart_copy_rx_to_tty(struct tegra_uart_port *tup,
		struct tty_port *tty, int count)
{
	int copied;

	tup->uport.icount.rx += count;
	if (!tty) {
		dev_err(tup->uport.dev, "No tty port\n");
		return;
	}
	dma_sync_single_for_cpu(tup->uport.dev, tup->rx_dma_buf_phys,
				TEGRA_UART_RX_DMA_BUFFER_SIZE, DMA_FROM_DEVICE);
	copied = tty_insert_flip_string(tty,
			((unsigned char *)(tup->rx_dma_buf_virt)), count);
	if (copied != count) {
		WARN_ON(1);
		dev_err(tup->uport.dev, "RxData copy to tty layer failed\n");
	}
	dma_sync_single_for_device(tup->uport.dev, tup->rx_dma_buf_phys,
				TEGRA_UART_RX_DMA_BUFFER_SIZE, DMA_TO_DEVICE);
}

static void tegra_uart_rx_dma_complete(void *args)
{
	struct tegra_uart_port *tup = args;
	struct uart_port *u = &tup->uport;
	int count = tup->rx_bytes_requested;
	struct tty_struct *tty = tty_port_tty_get(&tup->uport.state->port);
	struct tty_port *port = &u->state->port;
	unsigned long flags;

	async_tx_ack(tup->rx_dma_desc);
	spin_lock_irqsave(&u->lock, flags);

	/* Deactivate flow control to stop sender */
	if (tup->rts_active)
		set_rts(tup, false);

	/* If we are here, DMA is stopped */
	if (count)
		tegra_uart_copy_rx_to_tty(tup, port, count);

	tegra_uart_handle_rx_pio(tup, port);
	if (tty) {
		tty_flip_buffer_push(port);
		tty_kref_put(tty);
	}
	tegra_uart_start_rx_dma(tup);

	/* Activate flow control to start transfer */
	if (tup->rts_active)
		set_rts(tup, true);

	spin_unlock_irqrestore(&u->lock, flags);
}

static void tegra_uart_handle_rx_dma(struct tegra_uart_port *tup)
{
	struct dma_tx_state state;
	struct tty_struct *tty = tty_port_tty_get(&tup->uport.state->port);
	struct tty_port *port = &tup->uport.state->port;
	int count;

	/* Deactivate flow control to stop sender */
	if (tup->rts_active)
		set_rts(tup, false);

	dmaengine_terminate_all(tup->rx_dma_chan);
	dmaengine_tx_status(tup->rx_dma_chan,  tup->rx_cookie, &state);
	count = tup->rx_bytes_requested - state.residue;

	/* If we are here, DMA is stopped */
	if (count)
		tegra_uart_copy_rx_to_tty(tup, port, count);

	tegra_uart_handle_rx_pio(tup, port);
	if (tty) {
		tty_flip_buffer_push(port);
		tty_kref_put(tty);
	}
	tegra_uart_start_rx_dma(tup);

	if (tup->rts_active)
		set_rts(tup, true);
}

static int tegra_uart_start_rx_dma(struct tegra_uart_port *tup)
{
	unsigned int count = TEGRA_UART_RX_DMA_BUFFER_SIZE;

	tup->rx_dma_desc = dmaengine_prep_slave_single(tup->rx_dma_chan,
				tup->rx_dma_buf_phys, count, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT);
	if (!tup->rx_dma_desc) {
		dev_err(tup->uport.dev, "Not able to get desc for Rx\n");
		return -EIO;
	}

	tup->rx_dma_desc->callback = tegra_uart_rx_dma_complete;
	tup->rx_dma_desc->callback_param = tup;
	dma_sync_single_for_device(tup->uport.dev, tup->rx_dma_buf_phys,
				count, DMA_TO_DEVICE);
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
	return;
}

static irqreturn_t tegra_uart_isr(int irq, void *data)
{
	struct tegra_uart_port *tup = data;
	struct uart_port *u = &tup->uport;
	unsigned long iir;
	unsigned long ier;
	bool is_rx_int = false;
	unsigned long flags;

	spin_lock_irqsave(&u->lock, flags);
	while (1) {
		iir = tegra_uart_read(tup, UART_IIR);
		if (iir & UART_IIR_NO_INT) {
			if (is_rx_int) {
				tegra_uart_handle_rx_dma(tup);
				if (tup->rx_in_progress) {
					ier = tup->ier_shadow;
					ier |= (UART_IER_RLSI | UART_IER_RTOIE |
						TEGRA_UART_IER_EORD);
					tup->ier_shadow = ier;
					tegra_uart_write(tup, ier, UART_IER);
				}
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
		case 2: /* Receive */
			if (!is_rx_int) {
				is_rx_int = true;
				/* Disable Rx interrupts */
				ier = tup->ier_shadow;
				ier |= UART_IER_RDI;
				tegra_uart_write(tup, ier, UART_IER);
				ier &= ~(UART_IER_RDI | UART_IER_RLSI |
					UART_IER_RTOIE | TEGRA_UART_IER_EORD);
				tup->ier_shadow = ier;
				tegra_uart_write(tup, ier, UART_IER);
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
	struct tty_struct *tty = tty_port_tty_get(&tup->uport.state->port);
	struct tty_port *port = &u->state->port;
	struct dma_tx_state state;
	unsigned long ier;
	int count;

	if (tup->rts_active)
		set_rts(tup, false);

	if (!tup->rx_in_progress)
		return;

	tegra_uart_wait_sym_time(tup, 1); /* wait a character interval */

	ier = tup->ier_shadow;
	ier &= ~(UART_IER_RDI | UART_IER_RLSI | UART_IER_RTOIE |
					TEGRA_UART_IER_EORD);
	tup->ier_shadow = ier;
	tegra_uart_write(tup, ier, UART_IER);
	tup->rx_in_progress = 0;
	if (tup->rx_dma_chan) {
		dmaengine_terminate_all(tup->rx_dma_chan);
		dmaengine_tx_status(tup->rx_dma_chan, tup->rx_cookie, &state);
		async_tx_ack(tup->rx_dma_desc);
		count = tup->rx_bytes_requested - state.residue;
		tegra_uart_copy_rx_to_tty(tup, port, count);
		tegra_uart_handle_rx_pio(tup, port);
	} else {
		tegra_uart_handle_rx_pio(tup, port);
	}
	if (tty) {
		tty_flip_buffer_push(port);
		tty_kref_put(tty);
	}
	return;
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
	tegra_periph_reset_assert(tup->uart_clk);
	udelay(10);
	tegra_periph_reset_deassert(tup->uart_clk);

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
	tup->fcr_shadow |= UART_FCR_R_TRIG_01;
	tup->fcr_shadow |= TEGRA_UART_TX_TRIG_16B;
	tegra_uart_write(tup, tup->fcr_shadow, UART_FCR);

	/*
	 * Initialize the UART with default configuration
	 * (115200, N, 8, 1) so that the receive DMA buffer may be
	 * enqueued
	 */
	tup->lcr_shadow = TEGRA_UART_DEFAULT_LSR;
	tegra_set_baudrate(tup, TEGRA_UART_DEFAULT_BAUD);
	tup->fcr_shadow |= UART_FCR_DMA_SELECT;
	tegra_uart_write(tup, tup->fcr_shadow, UART_FCR);

	ret = tegra_uart_start_rx_dma(tup);
	if (ret < 0) {
		dev_err(tup->uport.dev, "Not able to start Rx DMA\n");
		return ret;
	}
	tup->rx_in_progress = 1;

	/*
	 * Enable IE_RXS for the receive status interrupts like line errros.
	 * Enable IE_RX_TIMEOUT to get the bytes which cannot be DMA'd.
	 *
	 * If using DMA mode, enable EORD instead of receive interrupt which
	 * will interrupt after the UART is done with the receive instead of
	 * the interrupt when the FIFO "threshold" is reached.
	 *
	 * EORD is different interrupt than RX_TIMEOUT - RX_TIMEOUT occurs when
	 * the DATA is sitting in the FIFO and couldn't be transferred to the
	 * DMA as the DMA size alignment(4 bytes) is not met. EORD will be
	 * triggered when there is a pause of the incomming data stream for 4
	 * characters long.
	 *
	 * For pauses in the data which is not aligned to 4 bytes, we get
	 * both the EORD as well as RX_TIMEOUT - SW sees RX_TIMEOUT first
	 * then the EORD.
	 */
	tup->ier_shadow = UART_IER_RLSI | UART_IER_RTOIE | TEGRA_UART_IER_EORD;
	tegra_uart_write(tup, tup->ier_shadow, UART_IER);
	return 0;
}

static int tegra_uart_dma_channel_allocate(struct tegra_uart_port *tup,
			bool dma_to_memory)
{
	struct dma_chan *dma_chan;
	unsigned char *dma_buf;
	dma_addr_t dma_phys;
	int ret;
	struct dma_slave_config dma_sconfig;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_chan = dma_request_channel(mask, NULL, NULL);
	if (!dma_chan) {
		dev_err(tup->uport.dev,
			"Dma channel is not available, will try later\n");
		return -EPROBE_DEFER;
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
	} else {
		dma_phys = dma_map_single(tup->uport.dev,
			tup->uport.state->xmit.buf, UART_XMIT_SIZE,
			DMA_TO_DEVICE);
		dma_buf = tup->uport.state->xmit.buf;
	}

	dma_sconfig.slave_id = tup->dma_req_sel;
	if (dma_to_memory) {
		dma_sconfig.src_addr = tup->uport.mapbase;
		dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		dma_sconfig.src_maxburst = 4;
	} else {
		dma_sconfig.dst_addr = tup->uport.mapbase;
		dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		dma_sconfig.dst_maxburst = 16;
	}

	ret = dmaengine_slave_config(dma_chan, &dma_sconfig);
	if (ret < 0) {
		dev_err(tup->uport.dev,
			"Dma slave config failed, err = %d\n", ret);
		goto scrub;
	}

	if (dma_to_memory) {
		tup->rx_dma_chan = dma_chan;
		tup->rx_dma_buf_virt = dma_buf;
		tup->rx_dma_buf_phys = dma_phys;
	} else {
		tup->tx_dma_chan = dma_chan;
		tup->tx_dma_buf_virt = dma_buf;
		tup->tx_dma_buf_phys = dma_phys;
	}
	return 0;

scrub:
	dma_release_channel(dma_chan);
	return ret;
}

static void tegra_uart_dma_channel_free(struct tegra_uart_port *tup,
		bool dma_to_memory)
{
	struct dma_chan *dma_chan;

	if (dma_to_memory) {
		dma_free_coherent(tup->uport.dev, TEGRA_UART_RX_DMA_BUFFER_SIZE,
				tup->rx_dma_buf_virt, tup->rx_dma_buf_phys);
		dma_chan = tup->rx_dma_chan;
		tup->rx_dma_chan = NULL;
		tup->rx_dma_buf_phys = 0;
		tup->rx_dma_buf_virt = NULL;
	} else {
		dma_unmap_single(tup->uport.dev, tup->tx_dma_buf_phys,
			UART_XMIT_SIZE, DMA_TO_DEVICE);
		dma_chan = tup->tx_dma_chan;
		tup->tx_dma_chan = NULL;
		tup->tx_dma_buf_phys = 0;
		tup->tx_dma_buf_virt = NULL;
	}
	dma_release_channel(dma_chan);
}

static int tegra_uart_startup(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);
	int ret;

	ret = tegra_uart_dma_channel_allocate(tup, false);
	if (ret < 0) {
		dev_err(u->dev, "Tx Dma allocation failed, err = %d\n", ret);
		return ret;
	}

	ret = tegra_uart_dma_channel_allocate(tup, true);
	if (ret < 0) {
		dev_err(u->dev, "Rx Dma allocation failed, err = %d\n", ret);
		goto fail_rx_dma;
	}

	ret = tegra_uart_hw_init(tup);
	if (ret < 0) {
		dev_err(u->dev, "Uart HW init failed, err = %d\n", ret);
		goto fail_hw_init;
	}

	ret = request_irq(u->irq, tegra_uart_isr, IRQF_DISABLED,
				dev_name(u->dev), tup);
	if (ret < 0) {
		dev_err(u->dev, "Failed to register ISR for IRQ %d\n", u->irq);
		goto fail_hw_init;
	}
	return 0;

fail_hw_init:
	tegra_uart_dma_channel_free(tup, true);
fail_rx_dma:
	tegra_uart_dma_channel_free(tup, false);
	return ret;
}

static void tegra_uart_shutdown(struct uart_port *u)
{
	struct tegra_uart_port *tup = to_tegra_uport(u);

	tegra_uart_hw_deinit(tup);

	tup->rx_in_progress = 0;
	tup->tx_in_progress = 0;

	tegra_uart_dma_channel_free(tup, true);
	tegra_uart_dma_channel_free(tup, false);
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

	max_divider *= 16;
	spin_lock_irqsave(&u->lock, flags);

	/* Changing configuration, it is safe to stop any rx now */
	if (tup->rts_active)
		set_rts(tup, false);

	/* Clear all interrupts as configuration is going to be change */
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
	tegra_set_baudrate(tup, baud);
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

	/* Make sure all write has completed */
	tegra_uart_read(tup, UART_IER);

	/* Reenable interrupt */
	tegra_uart_write(tup, tup->ier_shadow, UART_IER);
	tegra_uart_read(tup, UART_IER);

	spin_unlock_irqrestore(&u->lock, flags);
	return;
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
	return;
}

static const char *tegra_uart_type(struct uart_port *u)
{
	return TEGRA_UART_TYPE;
}

static struct uart_ops tegra_uart_ops = {
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
	.cons		= 0,
	.nr		= TEGRA_UART_MAXIMUM,
};

static int tegra_uart_parse_dt(struct platform_device *pdev,
	struct tegra_uart_port *tup)
{
	struct device_node *np = pdev->dev.of_node;
	u32 of_dma[2];
	int port;

	if (of_property_read_u32_array(np, "nvidia,dma-request-selector",
				of_dma, 2) >= 0) {
		tup->dma_req_sel = of_dma[1];
	} else {
		dev_err(&pdev->dev, "missing dma requestor in device tree\n");
		return -EINVAL;
	}

	port = of_alias_get_id(np, "serial");
	if (port < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", port);
		return port;
	}
	tup->uport.line = port;

	tup->enable_modem_interrupt = of_property_read_bool(np,
					"nvidia,enable-modem-interrupt");
	return 0;
}

struct tegra_uart_chip_data tegra20_uart_chip_data = {
	.tx_fifo_full_status		= false,
	.allow_txfifo_reset_fifo_mode	= true,
	.support_clk_src_div		= false,
};

struct tegra_uart_chip_data tegra30_uart_chip_data = {
	.tx_fifo_full_status		= true,
	.allow_txfifo_reset_fifo_mode	= false,
	.support_clk_src_div		= true,
};

static struct of_device_id tegra_uart_of_match[] = {
	{
		.compatible	= "nvidia,tegra30-hsuart",
		.data		= &tegra30_uart_chip_data,
	}, {
		.compatible	= "nvidia,tegra20-hsuart",
		.data		= &tegra20_uart_chip_data,
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

	u->iotype = UPIO_MEM32;
	u->irq = platform_get_irq(pdev, 0);
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
