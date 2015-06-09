/*
 * Driver for CSR SiRFprimaII onboard UARTs.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/sysrq.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/dmaengine.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>

#include "sirfsoc_uart.h"

static unsigned int
sirfsoc_uart_pio_tx_chars(struct sirfsoc_uart_port *sirfport, int count);
static unsigned int
sirfsoc_uart_pio_rx_chars(struct uart_port *port, unsigned int max_rx_count);
static struct uart_driver sirfsoc_uart_drv;

static void sirfsoc_uart_tx_dma_complete_callback(void *param);
static const struct sirfsoc_baudrate_to_regv baudrate_to_regv[] = {
	{4000000, 2359296},
	{3500000, 1310721},
	{3000000, 1572865},
	{2500000, 1245186},
	{2000000, 1572866},
	{1500000, 1245188},
	{1152000, 1638404},
	{1000000, 1572869},
	{921600, 1114120},
	{576000, 1245196},
	{500000, 1245198},
	{460800, 1572876},
	{230400, 1310750},
	{115200, 1310781},
	{57600, 1310843},
	{38400, 1114328},
	{19200, 1114545},
	{9600, 1114979},
};

static struct sirfsoc_uart_port *sirf_ports[SIRFSOC_UART_NR];

static inline struct sirfsoc_uart_port *to_sirfport(struct uart_port *port)
{
	return container_of(port, struct sirfsoc_uart_port, port);
}

static inline unsigned int sirfsoc_uart_tx_empty(struct uart_port *port)
{
	unsigned long reg;
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_fifo_status *ufifo_st = &sirfport->uart_reg->fifo_status;
	reg = rd_regl(port, ureg->sirfsoc_tx_fifo_status);
	return (reg & ufifo_st->ff_empty(port)) ? TIOCSER_TEMT : 0;
}

static unsigned int sirfsoc_uart_get_mctrl(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	if (!sirfport->hw_flow_ctrl || !sirfport->ms_enabled)
		goto cts_asserted;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		if (!(rd_regl(port, ureg->sirfsoc_afc_ctrl) &
						SIRFUART_AFC_CTS_STATUS))
			goto cts_asserted;
		else
			goto cts_deasserted;
	} else {
		if (!gpio_get_value(sirfport->cts_gpio))
			goto cts_asserted;
		else
			goto cts_deasserted;
	}
cts_deasserted:
	return TIOCM_CAR | TIOCM_DSR;
cts_asserted:
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

static void sirfsoc_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	unsigned int assert = mctrl & TIOCM_RTS;
	unsigned int val = assert ? SIRFUART_AFC_CTRL_RX_THD : 0x0;
	unsigned int current_val;

	if (mctrl & TIOCM_LOOP) {
		if (sirfport->uart_reg->uart_type == SIRF_REAL_UART)
			wr_regl(port, ureg->sirfsoc_line_ctrl,
				rd_regl(port, ureg->sirfsoc_line_ctrl) |
				SIRFUART_LOOP_BACK);
		else
			wr_regl(port, ureg->sirfsoc_mode1,
				rd_regl(port, ureg->sirfsoc_mode1) |
				SIRFSOC_USP_LOOP_BACK_CTRL);
	} else {
		if (sirfport->uart_reg->uart_type == SIRF_REAL_UART)
			wr_regl(port, ureg->sirfsoc_line_ctrl,
				rd_regl(port, ureg->sirfsoc_line_ctrl) &
				~SIRFUART_LOOP_BACK);
		else
			wr_regl(port, ureg->sirfsoc_mode1,
				rd_regl(port, ureg->sirfsoc_mode1) &
				~SIRFSOC_USP_LOOP_BACK_CTRL);
	}

	if (!sirfport->hw_flow_ctrl || !sirfport->ms_enabled)
		return;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		current_val = rd_regl(port, ureg->sirfsoc_afc_ctrl) & ~0xFF;
		val |= current_val;
		wr_regl(port, ureg->sirfsoc_afc_ctrl, val);
	} else {
		if (!val)
			gpio_set_value(sirfport->rts_gpio, 1);
		else
			gpio_set_value(sirfport->rts_gpio, 0);
	}
}

static void sirfsoc_uart_stop_tx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;

	if (sirfport->tx_dma_chan) {
		if (sirfport->tx_dma_state == TX_DMA_RUNNING) {
			dmaengine_pause(sirfport->tx_dma_chan);
			sirfport->tx_dma_state = TX_DMA_PAUSE;
		} else {
			if (!sirfport->is_atlas7)
				wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg) &
				~uint_en->sirfsoc_txfifo_empty_en);
			else
				wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
				uint_en->sirfsoc_txfifo_empty_en);
		}
	} else {
		if (sirfport->uart_reg->uart_type == SIRF_USP_UART)
			wr_regl(port, ureg->sirfsoc_tx_rx_en, rd_regl(port,
				ureg->sirfsoc_tx_rx_en) & ~SIRFUART_TX_EN);
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg) &
				~uint_en->sirfsoc_txfifo_empty_en);
		else
			wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
				uint_en->sirfsoc_txfifo_empty_en);
	}
}

static void sirfsoc_uart_tx_with_dma(struct sirfsoc_uart_port *sirfport)
{
	struct uart_port *port = &sirfport->port;
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long tran_size;
	unsigned long tran_start;
	unsigned long pio_tx_size;

	tran_size = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
	tran_start = (unsigned long)(xmit->buf + xmit->tail);
	if (uart_circ_empty(xmit) || uart_tx_stopped(port) ||
			!tran_size)
		return;
	if (sirfport->tx_dma_state == TX_DMA_PAUSE) {
		dmaengine_resume(sirfport->tx_dma_chan);
		return;
	}
	if (sirfport->tx_dma_state == TX_DMA_RUNNING)
		return;
	if (!sirfport->is_atlas7)
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg)&
				~(uint_en->sirfsoc_txfifo_empty_en));
	else
		wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
				uint_en->sirfsoc_txfifo_empty_en);
	/*
	 * DMA requires buffer address and buffer length are both aligned with
	 * 4 bytes, so we use PIO for
	 * 1. if address is not aligned with 4bytes, use PIO for the first 1~3
	 * bytes, and move to DMA for the left part aligned with 4bytes
	 * 2. if buffer length is not aligned with 4bytes, use DMA for aligned
	 * part first, move to PIO for the left 1~3 bytes
	 */
	if (tran_size < 4 || BYTES_TO_ALIGN(tran_start)) {
		wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_STOP);
		wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl,
			rd_regl(port, ureg->sirfsoc_tx_dma_io_ctrl)|
			SIRFUART_IO_MODE);
		if (BYTES_TO_ALIGN(tran_start)) {
			pio_tx_size = sirfsoc_uart_pio_tx_chars(sirfport,
				BYTES_TO_ALIGN(tran_start));
			tran_size -= pio_tx_size;
		}
		if (tran_size < 4)
			sirfsoc_uart_pio_tx_chars(sirfport, tran_size);
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg)|
				uint_en->sirfsoc_txfifo_empty_en);
		else
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				uint_en->sirfsoc_txfifo_empty_en);
		wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_START);
	} else {
		/* tx transfer mode switch into dma mode */
		wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_STOP);
		wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl,
			rd_regl(port, ureg->sirfsoc_tx_dma_io_ctrl)&
			~SIRFUART_IO_MODE);
		wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_START);
		tran_size &= ~(0x3);

		sirfport->tx_dma_addr = dma_map_single(port->dev,
			xmit->buf + xmit->tail,
			tran_size, DMA_TO_DEVICE);
		sirfport->tx_dma_desc = dmaengine_prep_slave_single(
			sirfport->tx_dma_chan, sirfport->tx_dma_addr,
			tran_size, DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT);
		if (!sirfport->tx_dma_desc) {
			dev_err(port->dev, "DMA prep slave single fail\n");
			return;
		}
		sirfport->tx_dma_desc->callback =
			sirfsoc_uart_tx_dma_complete_callback;
		sirfport->tx_dma_desc->callback_param = (void *)sirfport;
		sirfport->transfer_size = tran_size;

		dmaengine_submit(sirfport->tx_dma_desc);
		dma_async_issue_pending(sirfport->tx_dma_chan);
		sirfport->tx_dma_state = TX_DMA_RUNNING;
	}
}

static void sirfsoc_uart_start_tx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	if (sirfport->tx_dma_chan)
		sirfsoc_uart_tx_with_dma(sirfport);
	else {
		if (sirfport->uart_reg->uart_type == SIRF_USP_UART)
			wr_regl(port, ureg->sirfsoc_tx_rx_en, rd_regl(port,
				ureg->sirfsoc_tx_rx_en) | SIRFUART_TX_EN);
		wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_STOP);
		sirfsoc_uart_pio_tx_chars(sirfport, port->fifosize);
		wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_START);
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
					rd_regl(port, ureg->sirfsoc_int_en_reg)|
					uint_en->sirfsoc_txfifo_empty_en);
		else
			wr_regl(port, ureg->sirfsoc_int_en_reg,
					uint_en->sirfsoc_txfifo_empty_en);
	}
}

static void sirfsoc_uart_stop_rx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;

	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	if (sirfport->rx_dma_chan) {
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg) &
				~(SIRFUART_RX_DMA_INT_EN(uint_en,
				sirfport->uart_reg->uart_type) |
				uint_en->sirfsoc_rx_done_en));
		else
			wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
				SIRFUART_RX_DMA_INT_EN(uint_en,
				sirfport->uart_reg->uart_type)|
				uint_en->sirfsoc_rx_done_en);
		dmaengine_terminate_all(sirfport->rx_dma_chan);
	} else {
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg)&
				~(SIRFUART_RX_IO_INT_EN(uint_en,
				sirfport->uart_reg->uart_type)));
		else
			wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
				SIRFUART_RX_IO_INT_EN(uint_en,
				sirfport->uart_reg->uart_type));
	}
}

static void sirfsoc_uart_disable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;

	if (!sirfport->hw_flow_ctrl)
		return;
	sirfport->ms_enabled = false;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		wr_regl(port, ureg->sirfsoc_afc_ctrl,
				rd_regl(port, ureg->sirfsoc_afc_ctrl) & ~0x3FF);
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
					rd_regl(port, ureg->sirfsoc_int_en_reg)&
					~uint_en->sirfsoc_cts_en);
		else
			wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
					uint_en->sirfsoc_cts_en);
	} else
		disable_irq(gpio_to_irq(sirfport->cts_gpio));
}

static irqreturn_t sirfsoc_uart_usp_cts_handler(int irq, void *dev_id)
{
	struct sirfsoc_uart_port *sirfport = (struct sirfsoc_uart_port *)dev_id;
	struct uart_port *port = &sirfport->port;
	spin_lock(&port->lock);
	if (gpio_is_valid(sirfport->cts_gpio) && sirfport->ms_enabled)
		uart_handle_cts_change(port,
				!gpio_get_value(sirfport->cts_gpio));
	spin_unlock(&port->lock);
	return IRQ_HANDLED;
}

static void sirfsoc_uart_enable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;

	if (!sirfport->hw_flow_ctrl)
		return;
	sirfport->ms_enabled = true;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		wr_regl(port, ureg->sirfsoc_afc_ctrl,
				rd_regl(port, ureg->sirfsoc_afc_ctrl) |
				SIRFUART_AFC_TX_EN | SIRFUART_AFC_RX_EN |
				SIRFUART_AFC_CTRL_RX_THD);
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
					rd_regl(port, ureg->sirfsoc_int_en_reg)
					| uint_en->sirfsoc_cts_en);
		else
			wr_regl(port, ureg->sirfsoc_int_en_reg,
					uint_en->sirfsoc_cts_en);
	} else
		enable_irq(gpio_to_irq(sirfport->cts_gpio));
}

static void sirfsoc_uart_break_ctl(struct uart_port *port, int break_state)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		unsigned long ulcon = rd_regl(port, ureg->sirfsoc_line_ctrl);
		if (break_state)
			ulcon |= SIRFUART_SET_BREAK;
		else
			ulcon &= ~SIRFUART_SET_BREAK;
		wr_regl(port, ureg->sirfsoc_line_ctrl, ulcon);
	}
}

static unsigned int
sirfsoc_uart_pio_rx_chars(struct uart_port *port, unsigned int max_rx_count)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_fifo_status *ufifo_st = &sirfport->uart_reg->fifo_status;
	unsigned int ch, rx_count = 0;
	struct tty_struct *tty;
	tty = tty_port_tty_get(&port->state->port);
	if (!tty)
		return -ENODEV;
	while (!(rd_regl(port, ureg->sirfsoc_rx_fifo_status) &
					ufifo_st->ff_empty(port))) {
		ch = rd_regl(port, ureg->sirfsoc_rx_fifo_data) |
			SIRFUART_DUMMY_READ;
		if (unlikely(uart_handle_sysrq_char(port, ch)))
			continue;
		uart_insert_char(port, 0, 0, ch, TTY_NORMAL);
		rx_count++;
		if (rx_count >= max_rx_count)
			break;
	}

	sirfport->rx_io_count += rx_count;
	port->icount.rx += rx_count;

	return rx_count;
}

static unsigned int
sirfsoc_uart_pio_tx_chars(struct sirfsoc_uart_port *sirfport, int count)
{
	struct uart_port *port = &sirfport->port;
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_fifo_status *ufifo_st = &sirfport->uart_reg->fifo_status;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int num_tx = 0;
	while (!uart_circ_empty(xmit) &&
		!(rd_regl(port, ureg->sirfsoc_tx_fifo_status) &
					ufifo_st->ff_full(port)) &&
		count--) {
		wr_regl(port, ureg->sirfsoc_tx_fifo_data,
				xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		num_tx++;
	}
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	return num_tx;
}

static void sirfsoc_uart_tx_dma_complete_callback(void *param)
{
	struct sirfsoc_uart_port *sirfport = (struct sirfsoc_uart_port *)param;
	struct uart_port *port = &sirfport->port;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	xmit->tail = (xmit->tail + sirfport->transfer_size) &
				(UART_XMIT_SIZE - 1);
	port->icount.tx += sirfport->transfer_size;
	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
	if (sirfport->tx_dma_addr)
		dma_unmap_single(port->dev, sirfport->tx_dma_addr,
				sirfport->transfer_size, DMA_TO_DEVICE);
	sirfport->tx_dma_state = TX_DMA_IDLE;
	sirfsoc_uart_tx_with_dma(sirfport);
	spin_unlock_irqrestore(&port->lock, flags);
}

static irqreturn_t sirfsoc_uart_isr(int irq, void *dev_id)
{
	unsigned long intr_status;
	unsigned long cts_status;
	unsigned long flag = TTY_NORMAL;
	struct sirfsoc_uart_port *sirfport = (struct sirfsoc_uart_port *)dev_id;
	struct uart_port *port = &sirfport->port;
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_fifo_status *ufifo_st = &sirfport->uart_reg->fifo_status;
	struct sirfsoc_int_status *uint_st = &sirfport->uart_reg->uart_int_st;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	struct uart_state *state = port->state;
	struct circ_buf *xmit = &port->state->xmit;
	spin_lock(&port->lock);
	intr_status = rd_regl(port, ureg->sirfsoc_int_st_reg);
	wr_regl(port, ureg->sirfsoc_int_st_reg, intr_status);
	intr_status &= rd_regl(port, ureg->sirfsoc_int_en_reg);
	if (unlikely(intr_status & (SIRFUART_ERR_INT_STAT(uint_st,
				sirfport->uart_reg->uart_type)))) {
		if (intr_status & uint_st->sirfsoc_rxd_brk) {
			port->icount.brk++;
			if (uart_handle_break(port))
				goto recv_char;
		}
		if (intr_status & uint_st->sirfsoc_rx_oflow) {
			port->icount.overrun++;
			flag = TTY_OVERRUN;
		}
		if (intr_status & uint_st->sirfsoc_frm_err) {
			port->icount.frame++;
			flag = TTY_FRAME;
		}
		if (intr_status & uint_st->sirfsoc_parity_err) {
			port->icount.parity++;
			flag = TTY_PARITY;
		}
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_START);
		intr_status &= port->read_status_mask;
		uart_insert_char(port, intr_status,
					uint_en->sirfsoc_rx_oflow_en, 0, flag);
	}
recv_char:
	if ((sirfport->uart_reg->uart_type == SIRF_REAL_UART) &&
			(intr_status & SIRFUART_CTS_INT_ST(uint_st)) &&
			!sirfport->tx_dma_state) {
		cts_status = rd_regl(port, ureg->sirfsoc_afc_ctrl) &
					SIRFUART_AFC_CTS_STATUS;
		if (cts_status != 0)
			cts_status = 0;
		else
			cts_status = 1;
		uart_handle_cts_change(port, cts_status);
		wake_up_interruptible(&state->port.delta_msr_wait);
	}
	if (!sirfport->rx_dma_chan &&
		(intr_status & SIRFUART_RX_IO_INT_ST(uint_st))) {
		/*
		 * chip will trigger continuous RX_TIMEOUT interrupt
		 * in RXFIFO empty and not trigger if RXFIFO recevice
		 * data in limit time, original method use RX_TIMEOUT
		 * will trigger lots of useless interrupt in RXFIFO
		 * empty.RXFIFO received one byte will trigger RX_DONE
		 * interrupt.use RX_DONE to wait for data received
		 * into RXFIFO, use RX_THD/RX_FULL for lots data receive
		 * and use RX_TIMEOUT for the last left data.
		 */
		if (intr_status & uint_st->sirfsoc_rx_done) {
			if (!sirfport->is_atlas7) {
				wr_regl(port, ureg->sirfsoc_int_en_reg,
					rd_regl(port, ureg->sirfsoc_int_en_reg)
					& ~(uint_en->sirfsoc_rx_done_en));
				wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg)
				| (uint_en->sirfsoc_rx_timeout_en));
			} else {
				wr_regl(port, ureg->sirfsoc_int_en_clr_reg,
					uint_en->sirfsoc_rx_done_en);
				wr_regl(port, ureg->sirfsoc_int_en_reg,
					uint_en->sirfsoc_rx_timeout_en);
			}
		} else {
			if (intr_status & uint_st->sirfsoc_rx_timeout) {
				if (!sirfport->is_atlas7) {
					wr_regl(port, ureg->sirfsoc_int_en_reg,
					rd_regl(port, ureg->sirfsoc_int_en_reg)
					& ~(uint_en->sirfsoc_rx_timeout_en));
					wr_regl(port, ureg->sirfsoc_int_en_reg,
					rd_regl(port, ureg->sirfsoc_int_en_reg)
					| (uint_en->sirfsoc_rx_done_en));
				} else {
					wr_regl(port,
						ureg->sirfsoc_int_en_clr_reg,
						uint_en->sirfsoc_rx_timeout_en);
					wr_regl(port, ureg->sirfsoc_int_en_reg,
						uint_en->sirfsoc_rx_done_en);
				}
			}
			sirfsoc_uart_pio_rx_chars(port, port->fifosize);
		}
	}
	spin_unlock(&port->lock);
	tty_flip_buffer_push(&state->port);
	spin_lock(&port->lock);
	if (intr_status & uint_st->sirfsoc_txfifo_empty) {
		if (sirfport->tx_dma_chan)
			sirfsoc_uart_tx_with_dma(sirfport);
		else {
			if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
				spin_unlock(&port->lock);
				return IRQ_HANDLED;
			} else {
				sirfsoc_uart_pio_tx_chars(sirfport,
						port->fifosize);
				if ((uart_circ_empty(xmit)) &&
				(rd_regl(port, ureg->sirfsoc_tx_fifo_status) &
				ufifo_st->ff_empty(port)))
					sirfsoc_uart_stop_tx(port);
			}
		}
	}
	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static void sirfsoc_uart_rx_dma_complete_callback(void *param)
{
}

/* submit rx dma task into dmaengine */
static void sirfsoc_uart_start_next_rx_dma(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	sirfport->rx_io_count = 0;
	wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl,
		rd_regl(port, ureg->sirfsoc_rx_dma_io_ctrl) &
		~SIRFUART_IO_MODE);
	sirfport->rx_dma_items.xmit.tail =
		sirfport->rx_dma_items.xmit.head = 0;
	sirfport->rx_dma_items.desc =
		dmaengine_prep_dma_cyclic(sirfport->rx_dma_chan,
		sirfport->rx_dma_items.dma_addr, SIRFSOC_RX_DMA_BUF_SIZE,
		SIRFSOC_RX_DMA_BUF_SIZE / 2,
		DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
	if (IS_ERR_OR_NULL(sirfport->rx_dma_items.desc)) {
		dev_err(port->dev, "DMA slave single fail\n");
		return;
	}
	sirfport->rx_dma_items.desc->callback =
		sirfsoc_uart_rx_dma_complete_callback;
	sirfport->rx_dma_items.desc->callback_param = sirfport;
	sirfport->rx_dma_items.cookie =
		dmaengine_submit(sirfport->rx_dma_items.desc);
	dma_async_issue_pending(sirfport->rx_dma_chan);
	if (!sirfport->is_atlas7)
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg) |
				SIRFUART_RX_DMA_INT_EN(uint_en,
				sirfport->uart_reg->uart_type));
	else
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				SIRFUART_RX_DMA_INT_EN(uint_en,
				sirfport->uart_reg->uart_type));
}

static void sirfsoc_uart_start_rx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;

	sirfport->rx_io_count = 0;
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_START);
	if (sirfport->rx_dma_chan)
		sirfsoc_uart_start_next_rx_dma(port);
	else {
		if (!sirfport->is_atlas7)
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				rd_regl(port, ureg->sirfsoc_int_en_reg) |
				SIRFUART_RX_IO_INT_EN(uint_en,
					sirfport->uart_reg->uart_type));
		else
			wr_regl(port, ureg->sirfsoc_int_en_reg,
				SIRFUART_RX_IO_INT_EN(uint_en,
					sirfport->uart_reg->uart_type));
	}
}

static unsigned int
sirfsoc_usp_calc_sample_div(unsigned long set_rate,
		unsigned long ioclk_rate, unsigned long *sample_reg)
{
	unsigned long min_delta = ~0UL;
	unsigned short sample_div;
	unsigned long ioclk_div = 0;
	unsigned long temp_delta;

	for (sample_div = SIRF_USP_MIN_SAMPLE_DIV;
			sample_div <= SIRF_MAX_SAMPLE_DIV; sample_div++) {
		temp_delta = ioclk_rate -
		(ioclk_rate + (set_rate * sample_div) / 2)
		/ (set_rate * sample_div) * set_rate * sample_div;

		temp_delta = (temp_delta > 0) ? temp_delta : -temp_delta;
		if (temp_delta < min_delta) {
			ioclk_div = (2 * ioclk_rate /
				(set_rate * sample_div) + 1) / 2 - 1;
			if (ioclk_div > SIRF_IOCLK_DIV_MAX)
				continue;
			min_delta = temp_delta;
			*sample_reg = sample_div;
			if (!temp_delta)
				break;
		}
	}
	return ioclk_div;
}

static unsigned int
sirfsoc_uart_calc_sample_div(unsigned long baud_rate,
			unsigned long ioclk_rate, unsigned long *set_baud)
{
	unsigned long min_delta = ~0UL;
	unsigned short sample_div;
	unsigned int regv = 0;
	unsigned long ioclk_div;
	unsigned long baud_tmp;
	int temp_delta;

	for (sample_div = SIRF_MIN_SAMPLE_DIV;
			sample_div <= SIRF_MAX_SAMPLE_DIV; sample_div++) {
		ioclk_div = (ioclk_rate / (baud_rate * (sample_div + 1))) - 1;
		if (ioclk_div > SIRF_IOCLK_DIV_MAX)
			continue;
		baud_tmp = ioclk_rate / ((ioclk_div + 1) * (sample_div + 1));
		temp_delta = baud_tmp - baud_rate;
		temp_delta = (temp_delta > 0) ? temp_delta : -temp_delta;
		if (temp_delta < min_delta) {
			regv = regv & (~SIRF_IOCLK_DIV_MASK);
			regv = regv | ioclk_div;
			regv = regv & (~SIRF_SAMPLE_DIV_MASK);
			regv = regv | (sample_div << SIRF_SAMPLE_DIV_SHIFT);
			min_delta = temp_delta;
			*set_baud = baud_tmp;
		}
	}
	return regv;
}

static void sirfsoc_uart_set_termios(struct uart_port *port,
				       struct ktermios *termios,
				       struct ktermios *old)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned long	config_reg = 0;
	unsigned long	baud_rate;
	unsigned long	set_baud;
	unsigned long	flags;
	unsigned long	ic;
	unsigned int	clk_div_reg = 0;
	unsigned long	txfifo_op_reg, ioclk_rate;
	unsigned long	rx_time_out;
	int		threshold_div;
	u32		data_bit_len, stop_bit_len, len_val;
	unsigned long	sample_div_reg = 0xf;
	ioclk_rate	= port->uartclk;

	switch (termios->c_cflag & CSIZE) {
	default:
	case CS8:
		data_bit_len = 8;
		config_reg |= SIRFUART_DATA_BIT_LEN_8;
		break;
	case CS7:
		data_bit_len = 7;
		config_reg |= SIRFUART_DATA_BIT_LEN_7;
		break;
	case CS6:
		data_bit_len = 6;
		config_reg |= SIRFUART_DATA_BIT_LEN_6;
		break;
	case CS5:
		data_bit_len = 5;
		config_reg |= SIRFUART_DATA_BIT_LEN_5;
		break;
	}
	if (termios->c_cflag & CSTOPB) {
		config_reg |= SIRFUART_STOP_BIT_LEN_2;
		stop_bit_len = 2;
	} else
		stop_bit_len = 1;

	spin_lock_irqsave(&port->lock, flags);
	port->read_status_mask = uint_en->sirfsoc_rx_oflow_en;
	port->ignore_status_mask = 0;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		if (termios->c_iflag & INPCK)
			port->read_status_mask |= uint_en->sirfsoc_frm_err_en |
				uint_en->sirfsoc_parity_err_en;
	} else {
		if (termios->c_iflag & INPCK)
			port->read_status_mask |= uint_en->sirfsoc_frm_err_en;
	}
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
			port->read_status_mask |= uint_en->sirfsoc_rxd_brk_en;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |=
				uint_en->sirfsoc_frm_err_en |
				uint_en->sirfsoc_parity_err_en;
		if (termios->c_cflag & PARENB) {
			if (termios->c_cflag & CMSPAR) {
				if (termios->c_cflag & PARODD)
					config_reg |= SIRFUART_STICK_BIT_MARK;
				else
					config_reg |= SIRFUART_STICK_BIT_SPACE;
			} else {
				if (termios->c_cflag & PARODD)
					config_reg |= SIRFUART_STICK_BIT_ODD;
				else
					config_reg |= SIRFUART_STICK_BIT_EVEN;
			}
		}
	} else {
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |=
				uint_en->sirfsoc_frm_err_en;
		if (termios->c_cflag & PARENB)
			dev_warn(port->dev,
					"USP-UART not support parity err\n");
	}
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |=
			uint_en->sirfsoc_rxd_brk_en;
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |=
				uint_en->sirfsoc_rx_oflow_en;
	}
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= SIRFUART_DUMMY_READ;
	/* Hardware Flow Control Settings */
	if (UART_ENABLE_MS(port, termios->c_cflag)) {
		if (!sirfport->ms_enabled)
			sirfsoc_uart_enable_ms(port);
	} else {
		if (sirfport->ms_enabled)
			sirfsoc_uart_disable_ms(port);
	}
	baud_rate = uart_get_baud_rate(port, termios, old, 0, 4000000);
	if (ioclk_rate == 150000000) {
		for (ic = 0; ic < SIRF_BAUD_RATE_SUPPORT_NR; ic++)
			if (baud_rate == baudrate_to_regv[ic].baud_rate)
				clk_div_reg = baudrate_to_regv[ic].reg_val;
	}
	set_baud = baud_rate;
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		if (unlikely(clk_div_reg == 0))
			clk_div_reg = sirfsoc_uart_calc_sample_div(baud_rate,
					ioclk_rate, &set_baud);
		wr_regl(port, ureg->sirfsoc_divisor, clk_div_reg);
	} else {
		clk_div_reg = sirfsoc_usp_calc_sample_div(baud_rate,
				ioclk_rate, &sample_div_reg);
		sample_div_reg--;
		set_baud = ((ioclk_rate / (clk_div_reg+1) - 1) /
				(sample_div_reg + 1));
		/* setting usp mode 2 */
		len_val = ((1 << SIRFSOC_USP_MODE2_RXD_DELAY_OFFSET) |
				(1 << SIRFSOC_USP_MODE2_TXD_DELAY_OFFSET));
		len_val |= ((clk_div_reg & SIRFSOC_USP_MODE2_CLK_DIVISOR_MASK)
				<< SIRFSOC_USP_MODE2_CLK_DIVISOR_OFFSET);
		wr_regl(port, ureg->sirfsoc_mode2, len_val);
	}
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, set_baud, set_baud);
	/* set receive timeout && data bits len */
	rx_time_out = SIRFSOC_UART_RX_TIMEOUT(set_baud, 20000);
	rx_time_out = SIRFUART_RECV_TIMEOUT_VALUE(rx_time_out);
	txfifo_op_reg = rd_regl(port, ureg->sirfsoc_tx_fifo_op);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_STOP);
	wr_regl(port, ureg->sirfsoc_tx_fifo_op,
			(txfifo_op_reg & ~SIRFUART_FIFO_START));
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		config_reg |= SIRFUART_UART_RECV_TIMEOUT(rx_time_out);
		wr_regl(port, ureg->sirfsoc_line_ctrl, config_reg);
	} else {
		/*tx frame ctrl*/
		len_val = (data_bit_len - 1) << SIRFSOC_USP_TX_DATA_LEN_OFFSET;
		len_val |= (data_bit_len + 1 + stop_bit_len - 1) <<
				SIRFSOC_USP_TX_FRAME_LEN_OFFSET;
		len_val |= ((data_bit_len - 1) <<
				SIRFSOC_USP_TX_SHIFTER_LEN_OFFSET);
		len_val |= (((clk_div_reg & 0xc00) >> 10) <<
				SIRFSOC_USP_TX_CLK_DIVISOR_OFFSET);
		wr_regl(port, ureg->sirfsoc_tx_frame_ctrl, len_val);
		/*rx frame ctrl*/
		len_val = (data_bit_len - 1) << SIRFSOC_USP_RX_DATA_LEN_OFFSET;
		len_val |= (data_bit_len + 1 + stop_bit_len - 1) <<
				SIRFSOC_USP_RX_FRAME_LEN_OFFSET;
		len_val |= (data_bit_len - 1) <<
				SIRFSOC_USP_RX_SHIFTER_LEN_OFFSET;
		len_val |= (((clk_div_reg & 0xf000) >> 12) <<
				SIRFSOC_USP_RX_CLK_DIVISOR_OFFSET);
		wr_regl(port, ureg->sirfsoc_rx_frame_ctrl, len_val);
		/*async param*/
		wr_regl(port, ureg->sirfsoc_async_param_reg,
			(SIRFUART_USP_RECV_TIMEOUT(rx_time_out)) |
			(sample_div_reg & SIRFSOC_USP_ASYNC_DIV2_MASK) <<
			SIRFSOC_USP_ASYNC_DIV2_OFFSET);
	}
	if (sirfport->tx_dma_chan)
		wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl, SIRFUART_DMA_MODE);
	else
		wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl, SIRFUART_IO_MODE);
	if (sirfport->rx_dma_chan)
		wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl, SIRFUART_DMA_MODE);
	else
		wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl, SIRFUART_IO_MODE);
	sirfport->rx_period_time = 20000000;
	/* Reset Rx/Tx FIFO Threshold level for proper baudrate */
	if (set_baud < 1000000)
		threshold_div = 1;
	else
		threshold_div = 2;
	wr_regl(port, ureg->sirfsoc_tx_fifo_ctrl,
				SIRFUART_FIFO_THD(port) / threshold_div);
	wr_regl(port, ureg->sirfsoc_rx_fifo_ctrl,
				SIRFUART_FIFO_THD(port) / threshold_div);
	txfifo_op_reg |= SIRFUART_FIFO_START;
	wr_regl(port, ureg->sirfsoc_tx_fifo_op, txfifo_op_reg);
	uart_update_timeout(port, termios->c_cflag, set_baud);
	sirfsoc_uart_start_rx(port);
	wr_regl(port, ureg->sirfsoc_tx_rx_en, SIRFUART_TX_EN | SIRFUART_RX_EN);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void sirfsoc_uart_pm(struct uart_port *port, unsigned int state,
			      unsigned int oldstate)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	if (!state)
		clk_prepare_enable(sirfport->clk);
	else
		clk_disable_unprepare(sirfport->clk);
}

static int sirfsoc_uart_startup(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport	= to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	unsigned int index			= port->line;
	int ret;
	irq_modify_status(port->irq, IRQ_NOREQUEST, IRQ_NOAUTOEN);
	ret = request_irq(port->irq,
				sirfsoc_uart_isr,
				0,
				SIRFUART_PORT_NAME,
				sirfport);
	if (ret != 0) {
		dev_err(port->dev, "UART%d request IRQ line (%d) failed.\n",
							index, port->irq);
		goto irq_err;
	}
	/* initial hardware settings */
	wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl,
		rd_regl(port, ureg->sirfsoc_tx_dma_io_ctrl) |
		SIRFUART_IO_MODE);
	wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl,
		rd_regl(port, ureg->sirfsoc_rx_dma_io_ctrl) |
		SIRFUART_IO_MODE);
	wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl,
		rd_regl(port, ureg->sirfsoc_rx_dma_io_ctrl) &
		~SIRFUART_RX_DMA_FLUSH);
	wr_regl(port, ureg->sirfsoc_tx_dma_io_len, 0);
	wr_regl(port, ureg->sirfsoc_rx_dma_io_len, 0);
	wr_regl(port, ureg->sirfsoc_tx_rx_en, SIRFUART_RX_EN | SIRFUART_TX_EN);
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART)
		wr_regl(port, ureg->sirfsoc_mode1,
			SIRFSOC_USP_ENDIAN_CTRL_LSBF |
			SIRFSOC_USP_EN);
	wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_RESET);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	wr_regl(port, ureg->sirfsoc_tx_fifo_ctrl, SIRFUART_FIFO_THD(port));
	wr_regl(port, ureg->sirfsoc_rx_fifo_ctrl, SIRFUART_FIFO_THD(port));
	if (sirfport->rx_dma_chan)
		wr_regl(port, ureg->sirfsoc_rx_fifo_level_chk,
			SIRFUART_RX_FIFO_CHK_SC(port->line, 0x4) |
			SIRFUART_RX_FIFO_CHK_LC(port->line, 0xe) |
			SIRFUART_RX_FIFO_CHK_HC(port->line, 0x1b));
	if (sirfport->tx_dma_chan) {
		sirfport->tx_dma_state = TX_DMA_IDLE;
		wr_regl(port, ureg->sirfsoc_tx_fifo_level_chk,
				SIRFUART_TX_FIFO_CHK_SC(port->line, 0x1b) |
				SIRFUART_TX_FIFO_CHK_LC(port->line, 0xe) |
				SIRFUART_TX_FIFO_CHK_HC(port->line, 0x4));
	}
	sirfport->ms_enabled = false;
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART &&
		sirfport->hw_flow_ctrl) {
		irq_modify_status(gpio_to_irq(sirfport->cts_gpio),
			IRQ_NOREQUEST, IRQ_NOAUTOEN);
		ret = request_irq(gpio_to_irq(sirfport->cts_gpio),
			sirfsoc_uart_usp_cts_handler, IRQF_TRIGGER_FALLING |
			IRQF_TRIGGER_RISING, "usp_cts_irq", sirfport);
		if (ret != 0) {
			dev_err(port->dev, "UART-USP:request gpio irq fail\n");
			goto init_rx_err;
		}
	}
	enable_irq(port->irq);
	if (sirfport->rx_dma_chan && !sirfport->is_hrt_enabled) {
		sirfport->is_hrt_enabled = true;
		sirfport->rx_period_time = 20000000;
		sirfport->rx_dma_items.xmit.tail =
			sirfport->rx_dma_items.xmit.head = 0;
		hrtimer_start(&sirfport->hrt,
			ns_to_ktime(sirfport->rx_period_time),
			HRTIMER_MODE_REL);
	}

	return 0;
init_rx_err:
	free_irq(port->irq, sirfport);
irq_err:
	return ret;
}

static void sirfsoc_uart_shutdown(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	if (!sirfport->is_atlas7)
		wr_regl(port, ureg->sirfsoc_int_en_reg, 0);
	else
		wr_regl(port, ureg->sirfsoc_int_en_clr_reg, ~0UL);

	free_irq(port->irq, sirfport);
	if (sirfport->ms_enabled)
		sirfsoc_uart_disable_ms(port);
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART &&
			sirfport->hw_flow_ctrl) {
		gpio_set_value(sirfport->rts_gpio, 1);
		free_irq(gpio_to_irq(sirfport->cts_gpio), sirfport);
	}
	if (sirfport->tx_dma_chan)
		sirfport->tx_dma_state = TX_DMA_IDLE;
	if (sirfport->rx_dma_chan && sirfport->is_hrt_enabled) {
		while ((rd_regl(port, ureg->sirfsoc_rx_fifo_status) &
			SIRFUART_RX_FIFO_MASK) > 0)
			;
		sirfport->is_hrt_enabled = false;
		hrtimer_cancel(&sirfport->hrt);
	}
}

static const char *sirfsoc_uart_type(struct uart_port *port)
{
	return port->type == SIRFSOC_PORT_TYPE ? SIRFUART_PORT_NAME : NULL;
}

static int sirfsoc_uart_request_port(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_uart_param *uart_param = &sirfport->uart_reg->uart_param;
	void *ret;
	ret = request_mem_region(port->mapbase,
		SIRFUART_MAP_SIZE, uart_param->port_name);
	return ret ? 0 : -EBUSY;
}

static void sirfsoc_uart_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, SIRFUART_MAP_SIZE);
}

static void sirfsoc_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = SIRFSOC_PORT_TYPE;
		sirfsoc_uart_request_port(port);
	}
}

static struct uart_ops sirfsoc_uart_ops = {
	.tx_empty	= sirfsoc_uart_tx_empty,
	.get_mctrl	= sirfsoc_uart_get_mctrl,
	.set_mctrl	= sirfsoc_uart_set_mctrl,
	.stop_tx	= sirfsoc_uart_stop_tx,
	.start_tx	= sirfsoc_uart_start_tx,
	.stop_rx	= sirfsoc_uart_stop_rx,
	.enable_ms	= sirfsoc_uart_enable_ms,
	.break_ctl	= sirfsoc_uart_break_ctl,
	.startup	= sirfsoc_uart_startup,
	.shutdown	= sirfsoc_uart_shutdown,
	.set_termios	= sirfsoc_uart_set_termios,
	.pm		= sirfsoc_uart_pm,
	.type		= sirfsoc_uart_type,
	.release_port	= sirfsoc_uart_release_port,
	.request_port	= sirfsoc_uart_request_port,
	.config_port	= sirfsoc_uart_config_port,
};

#ifdef CONFIG_SERIAL_SIRFSOC_CONSOLE
static int __init
sirfsoc_uart_console_setup(struct console *co, char *options)
{
	unsigned int baud = 115200;
	unsigned int bits = 8;
	unsigned int parity = 'n';
	unsigned int flow = 'n';
	struct sirfsoc_uart_port *sirfport;
	struct sirfsoc_register *ureg;
	if (co->index < 0 || co->index >= SIRFSOC_UART_NR)
		co->index = 1;
	sirfport = sirf_ports[co->index];
	if (!sirfport)
		return -ENODEV;
	ureg = &sirfport->uart_reg->uart_reg;
	if (!sirfport->port.mapbase)
		return -ENODEV;

	/* enable usp in mode1 register */
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART)
		wr_regl(&sirfport->port, ureg->sirfsoc_mode1, SIRFSOC_USP_EN |
				SIRFSOC_USP_ENDIAN_CTRL_LSBF);
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	sirfport->port.cons = co;

	/* default console tx/rx transfer using io mode */
	sirfport->rx_dma_chan = NULL;
	sirfport->tx_dma_chan = NULL;
	return uart_set_options(&sirfport->port, co, baud, parity, bits, flow);
}

static void sirfsoc_uart_console_putchar(struct uart_port *port, int ch)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_fifo_status *ufifo_st = &sirfport->uart_reg->fifo_status;
	while (rd_regl(port, ureg->sirfsoc_tx_fifo_status) &
		ufifo_st->ff_full(port))
		cpu_relax();
	wr_regl(port, ureg->sirfsoc_tx_fifo_data, ch);
}

static void sirfsoc_uart_console_write(struct console *co, const char *s,
							unsigned int count)
{
	struct sirfsoc_uart_port *sirfport = sirf_ports[co->index];

	uart_console_write(&sirfport->port, s, count,
			sirfsoc_uart_console_putchar);
}

static struct console sirfsoc_uart_console = {
	.name		= SIRFSOC_UART_NAME,
	.device		= uart_console_device,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.write		= sirfsoc_uart_console_write,
	.setup		= sirfsoc_uart_console_setup,
	.data           = &sirfsoc_uart_drv,
};

static int __init sirfsoc_uart_console_init(void)
{
	register_console(&sirfsoc_uart_console);
	return 0;
}
console_initcall(sirfsoc_uart_console_init);
#endif

static struct uart_driver sirfsoc_uart_drv = {
	.owner		= THIS_MODULE,
	.driver_name	= SIRFUART_PORT_NAME,
	.nr		= SIRFSOC_UART_NR,
	.dev_name	= SIRFSOC_UART_NAME,
	.major		= SIRFSOC_UART_MAJOR,
	.minor		= SIRFSOC_UART_MINOR,
#ifdef CONFIG_SERIAL_SIRFSOC_CONSOLE
	.cons			= &sirfsoc_uart_console,
#else
	.cons			= NULL,
#endif
};

static enum hrtimer_restart
	sirfsoc_uart_rx_dma_hrtimer_callback(struct hrtimer *hrt)
{
	struct sirfsoc_uart_port *sirfport;
	struct uart_port *port;
	int count, inserted;
	struct dma_tx_state tx_state;
	struct tty_struct *tty;
	struct sirfsoc_register *ureg;
	struct circ_buf *xmit;

	sirfport = container_of(hrt, struct sirfsoc_uart_port, hrt);
	port = &sirfport->port;
	inserted = 0;
	tty = port->state->port.tty;
	ureg = &sirfport->uart_reg->uart_reg;
	xmit = &sirfport->rx_dma_items.xmit;
	dmaengine_tx_status(sirfport->rx_dma_chan,
		sirfport->rx_dma_items.cookie, &tx_state);
	xmit->head = SIRFSOC_RX_DMA_BUF_SIZE - tx_state.residue;
	count = CIRC_CNT_TO_END(xmit->head, xmit->tail,
			SIRFSOC_RX_DMA_BUF_SIZE);
	while (count > 0) {
		inserted = tty_insert_flip_string(tty->port,
			(const unsigned char *)&xmit->buf[xmit->tail], count);
		if (!inserted)
			goto next_hrt;
		port->icount.rx += inserted;
		xmit->tail = (xmit->tail + inserted) &
				(SIRFSOC_RX_DMA_BUF_SIZE - 1);
		count = CIRC_CNT_TO_END(xmit->head, xmit->tail,
				SIRFSOC_RX_DMA_BUF_SIZE);
		tty_flip_buffer_push(tty->port);
	}
	/*
	 * if RX DMA buffer data have all push into tty buffer, and there is
	 * only little data(less than a dma transfer unit) left in rxfifo,
	 * fetch it out in pio mode and switch back to dma immediately
	 */
	if (!inserted && !count &&
		((rd_regl(port, ureg->sirfsoc_rx_fifo_status) &
		SIRFUART_RX_FIFO_MASK) > 0)) {
		/* switch to pio mode */
		wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl,
			rd_regl(port, ureg->sirfsoc_rx_dma_io_ctrl) |
			SIRFUART_IO_MODE);
		while ((rd_regl(port, ureg->sirfsoc_rx_fifo_status) &
			SIRFUART_RX_FIFO_MASK) > 0) {
			if (sirfsoc_uart_pio_rx_chars(port, 16) > 0)
				tty_flip_buffer_push(tty->port);
		}
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_START);
		/* switch back to dma mode */
		wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl,
			rd_regl(port, ureg->sirfsoc_rx_dma_io_ctrl) &
			~SIRFUART_IO_MODE);
	}
next_hrt:
	hrtimer_forward_now(hrt, ns_to_ktime(sirfport->rx_period_time));
	return HRTIMER_RESTART;
}

static struct of_device_id sirfsoc_uart_ids[] = {
	{ .compatible = "sirf,prima2-uart", .data = &sirfsoc_uart,},
	{ .compatible = "sirf,atlas7-uart", .data = &sirfsoc_uart},
	{ .compatible = "sirf,prima2-usp-uart", .data = &sirfsoc_usp},
	{ .compatible = "sirf,atlas7-usp-uart", .data = &sirfsoc_usp},
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_uart_ids);

static int sirfsoc_uart_probe(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport;
	struct uart_port *port;
	struct resource *res;
	int ret;
	struct dma_slave_config slv_cfg = {
		.src_maxburst = 2,
	};
	struct dma_slave_config tx_slv_cfg = {
		.dst_maxburst = 2,
	};
	const struct of_device_id *match;

	match = of_match_node(sirfsoc_uart_ids, pdev->dev.of_node);
	sirfport = devm_kzalloc(&pdev->dev, sizeof(*sirfport), GFP_KERNEL);
	if (!sirfport) {
		ret = -ENOMEM;
		goto err;
	}
	sirfport->port.line = of_alias_get_id(pdev->dev.of_node, "serial");
	sirf_ports[sirfport->port.line] = sirfport;
	sirfport->port.iotype = UPIO_MEM;
	sirfport->port.flags = UPF_BOOT_AUTOCONF;
	port = &sirfport->port;
	port->dev = &pdev->dev;
	port->private_data = sirfport;
	sirfport->uart_reg = (struct sirfsoc_uart_register *)match->data;

	sirfport->hw_flow_ctrl = of_property_read_bool(pdev->dev.of_node,
		"sirf,uart-has-rtscts");
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,prima2-uart") ||
		of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-uart"))
		sirfport->uart_reg->uart_type = SIRF_REAL_UART;
	if (of_device_is_compatible(pdev->dev.of_node,
		"sirf,prima2-usp-uart") || of_device_is_compatible(
		pdev->dev.of_node, "sirf,atlas7-usp-uart")) {
		sirfport->uart_reg->uart_type =	SIRF_USP_UART;
		if (!sirfport->hw_flow_ctrl)
			goto usp_no_flow_control;
		if (of_find_property(pdev->dev.of_node, "cts-gpios", NULL))
			sirfport->cts_gpio = of_get_named_gpio(
					pdev->dev.of_node, "cts-gpios", 0);
		else
			sirfport->cts_gpio = -1;
		if (of_find_property(pdev->dev.of_node, "rts-gpios", NULL))
			sirfport->rts_gpio = of_get_named_gpio(
					pdev->dev.of_node, "rts-gpios", 0);
		else
			sirfport->rts_gpio = -1;

		if ((!gpio_is_valid(sirfport->cts_gpio) ||
			 !gpio_is_valid(sirfport->rts_gpio))) {
			ret = -EINVAL;
			dev_err(&pdev->dev,
				"Usp flow control must have cts and rts gpio");
			goto err;
		}
		ret = devm_gpio_request(&pdev->dev, sirfport->cts_gpio,
				"usp-cts-gpio");
		if (ret) {
			dev_err(&pdev->dev, "Unable request cts gpio");
			goto err;
		}
		gpio_direction_input(sirfport->cts_gpio);
		ret = devm_gpio_request(&pdev->dev, sirfport->rts_gpio,
				"usp-rts-gpio");
		if (ret) {
			dev_err(&pdev->dev, "Unable request rts gpio");
			goto err;
		}
		gpio_direction_output(sirfport->rts_gpio, 1);
	}
usp_no_flow_control:
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-uart") ||
	    of_device_is_compatible(pdev->dev.of_node, "sirf,atlas7-usp-uart"))
		sirfport->is_atlas7 = true;

	if (of_property_read_u32(pdev->dev.of_node,
			"fifosize",
			&port->fifosize)) {
		dev_err(&pdev->dev,
			"Unable to find fifosize in uart node.\n");
		ret = -EFAULT;
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Insufficient resources.\n");
		ret = -EFAULT;
		goto err;
	}
	port->mapbase = res->start;
	port->membase = devm_ioremap(&pdev->dev,
			res->start, resource_size(res));
	if (!port->membase) {
		dev_err(&pdev->dev, "Cannot remap resource.\n");
		ret = -ENOMEM;
		goto err;
	}
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Insufficient resources.\n");
		ret = -EFAULT;
		goto err;
	}
	port->irq = res->start;

	sirfport->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sirfport->clk)) {
		ret = PTR_ERR(sirfport->clk);
		goto err;
	}
	port->uartclk = clk_get_rate(sirfport->clk);

	port->ops = &sirfsoc_uart_ops;
	spin_lock_init(&port->lock);

	platform_set_drvdata(pdev, sirfport);
	ret = uart_add_one_port(&sirfsoc_uart_drv, port);
	if (ret != 0) {
		dev_err(&pdev->dev, "Cannot add UART port(%d).\n", pdev->id);
		goto err;
	}

	sirfport->rx_dma_chan = dma_request_slave_channel(port->dev, "rx");
	sirfport->rx_dma_items.xmit.buf =
		dma_alloc_coherent(port->dev, SIRFSOC_RX_DMA_BUF_SIZE,
		&sirfport->rx_dma_items.dma_addr, GFP_KERNEL);
	if (!sirfport->rx_dma_items.xmit.buf) {
		dev_err(port->dev, "Uart alloc bufa failed\n");
		ret = -ENOMEM;
		goto alloc_coherent_err;
	}
	sirfport->rx_dma_items.xmit.head =
		sirfport->rx_dma_items.xmit.tail = 0;
	if (sirfport->rx_dma_chan)
		dmaengine_slave_config(sirfport->rx_dma_chan, &slv_cfg);
	sirfport->tx_dma_chan = dma_request_slave_channel(port->dev, "tx");
	if (sirfport->tx_dma_chan)
		dmaengine_slave_config(sirfport->tx_dma_chan, &tx_slv_cfg);
	if (sirfport->rx_dma_chan) {
		hrtimer_init(&sirfport->hrt, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		sirfport->hrt.function = sirfsoc_uart_rx_dma_hrtimer_callback;
		sirfport->is_hrt_enabled = false;
	}

	return 0;
alloc_coherent_err:
	dma_free_coherent(port->dev, SIRFSOC_RX_DMA_BUF_SIZE,
			sirfport->rx_dma_items.xmit.buf,
			sirfport->rx_dma_items.dma_addr);
	dma_release_channel(sirfport->rx_dma_chan);
err:
	return ret;
}

static int sirfsoc_uart_remove(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_remove_one_port(&sirfsoc_uart_drv, port);
	if (sirfport->rx_dma_chan) {
		dmaengine_terminate_all(sirfport->rx_dma_chan);
		dma_release_channel(sirfport->rx_dma_chan);
		dma_free_coherent(port->dev, SIRFSOC_RX_DMA_BUF_SIZE,
				sirfport->rx_dma_items.xmit.buf,
				sirfport->rx_dma_items.dma_addr);
	}
	if (sirfport->tx_dma_chan) {
		dmaengine_terminate_all(sirfport->tx_dma_chan);
		dma_release_channel(sirfport->tx_dma_chan);
	}
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int
sirfsoc_uart_suspend(struct device *pdev)
{
	struct sirfsoc_uart_port *sirfport = dev_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_suspend_port(&sirfsoc_uart_drv, port);
	return 0;
}

static int sirfsoc_uart_resume(struct device *pdev)
{
	struct sirfsoc_uart_port *sirfport = dev_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_resume_port(&sirfsoc_uart_drv, port);
	return 0;
}
#endif

static const struct dev_pm_ops sirfsoc_uart_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sirfsoc_uart_suspend, sirfsoc_uart_resume)
};

static struct platform_driver sirfsoc_uart_driver = {
	.probe		= sirfsoc_uart_probe,
	.remove		= sirfsoc_uart_remove,
	.driver		= {
		.name	= SIRFUART_PORT_NAME,
		.of_match_table = sirfsoc_uart_ids,
		.pm	= &sirfsoc_uart_pm_ops,
	},
};

static int __init sirfsoc_uart_init(void)
{
	int ret = 0;

	ret = uart_register_driver(&sirfsoc_uart_drv);
	if (ret)
		goto out;

	ret = platform_driver_register(&sirfsoc_uart_driver);
	if (ret)
		uart_unregister_driver(&sirfsoc_uart_drv);
out:
	return ret;
}
module_init(sirfsoc_uart_init);

static void __exit sirfsoc_uart_exit(void)
{
	platform_driver_unregister(&sirfsoc_uart_driver);
	uart_unregister_driver(&sirfsoc_uart_drv);
}
module_exit(sirfsoc_uart_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bin Shi <Bin.Shi@csr.com>, Rong Wang<Rong.Wang@csr.com>");
MODULE_DESCRIPTION("CSR SiRFprimaII Uart Driver");
