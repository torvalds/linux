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
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <linux/pinctrl/consumer.h>

#include "sirfsoc_uart.h"

static unsigned int
sirfsoc_uart_pio_tx_chars(struct sirfsoc_uart_port *sirfport, int count);
static unsigned int
sirfsoc_uart_pio_rx_chars(struct uart_port *port, unsigned int max_rx_count);
static struct uart_driver sirfsoc_uart_drv;

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

static struct sirfsoc_uart_port sirfsoc_uart_ports[SIRFSOC_UART_NR] = {
	[0] = {
		.port = {
			.iotype		= UPIO_MEM,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 0,
		},
	},
	[1] = {
		.port = {
			.iotype		= UPIO_MEM,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 1,
		},
	},
	[2] = {
		.port = {
			.iotype		= UPIO_MEM,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 2,
		},
	},
	[3] = {
		.port = {
			.iotype		= UPIO_MEM,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 3,
		},
	},
	[4] = {
		.port = {
			.iotype		= UPIO_MEM,
			.flags		= UPF_BOOT_AUTOCONF,
			.line		= 4,
		},
	},
};

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

	return (reg & ufifo_st->ff_empty(port->line)) ? TIOCSER_TEMT : 0;
}

static unsigned int sirfsoc_uart_get_mctrl(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	if (!(sirfport->ms_enabled)) {
		goto cts_asserted;
	} else if (sirfport->hw_flow_ctrl) {
		if (!(rd_regl(port, ureg->sirfsoc_afc_ctrl) &
						SIRFUART_AFC_CTS_STATUS))
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
	if (sirfport->hw_flow_ctrl) {
		current_val = rd_regl(port, ureg->sirfsoc_afc_ctrl) & ~0xFF;
		val |= current_val;
		wr_regl(port, ureg->sirfsoc_afc_ctrl, val);
	}
}

static void sirfsoc_uart_stop_tx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned int regv;

	if (!sirfport->is_marco) {
		regv = rd_regl(port, ureg->sirfsoc_int_en_reg);
		wr_regl(port, ureg->sirfsoc_int_en_reg,
			regv & ~uint_en->sirfsoc_txfifo_empty_en);
	} else
		wr_regl(port, SIRFUART_INT_EN_CLR,
				uint_en->sirfsoc_txfifo_empty_en);

}

void sirfsoc_uart_start_tx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned long regv;

	sirfsoc_uart_pio_tx_chars(sirfport, 1);
	wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_START);
	if (!sirfport->is_marco) {
		regv = rd_regl(port, ureg->sirfsoc_int_en_reg);
		wr_regl(port, ureg->sirfsoc_int_en_reg, regv |
			uint_en->sirfsoc_txfifo_empty_en);
	} else
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				uint_en->sirfsoc_txfifo_empty_en);
}

static void sirfsoc_uart_stop_rx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned long reg;
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	if (!sirfport->is_marco) {
		reg = rd_regl(port, ureg->sirfsoc_int_en_reg);
		wr_regl(port, ureg->sirfsoc_int_en_reg,
			reg & ~(SIRFUART_RX_IO_INT_EN(port, uint_en)));
	} else
		wr_regl(port, SIRFUART_INT_EN_CLR,
				SIRFUART_RX_IO_INT_EN(port, uint_en));
}

static void sirfsoc_uart_disable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned long reg;

	sirfport->ms_enabled = 0;
	if (!sirfport->hw_flow_ctrl)
		return;

	reg = rd_regl(port, ureg->sirfsoc_afc_ctrl);
	wr_regl(port, ureg->sirfsoc_afc_ctrl, reg & ~0x3FF);
	if (!sirfport->is_marco) {
		reg = rd_regl(port, ureg->sirfsoc_int_en_reg);
		wr_regl(port, ureg->sirfsoc_int_en_reg,
			reg & ~uint_en->sirfsoc_cts_en);
	} else
		wr_regl(port, SIRFUART_INT_EN_CLR,
				uint_en->sirfsoc_cts_en);
}

static void sirfsoc_uart_enable_ms(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned long reg;
	unsigned long flg;

	if (!sirfport->hw_flow_ctrl)
		return;
	flg = SIRFUART_AFC_TX_EN | SIRFUART_AFC_RX_EN;
	reg = rd_regl(port, ureg->sirfsoc_afc_ctrl);
	wr_regl(port, ureg->sirfsoc_afc_ctrl, reg | flg);
	if (!sirfport->is_marco) {
		reg = rd_regl(port, ureg->sirfsoc_int_en_reg);
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				reg | uint_en->sirfsoc_cts_en);
	} else
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				uint_en->sirfsoc_cts_en);
	uart_handle_cts_change(port,
		!(rd_regl(port, ureg->sirfsoc_afc_ctrl) &
				SIRFUART_AFC_CTS_STATUS));
	sirfport->ms_enabled = 1;
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
					ufifo_st->ff_empty(port->line))) {
		ch = rd_regl(port, ureg->sirfsoc_rx_fifo_data) |
			SIRFUART_DUMMY_READ;
		if (unlikely(uart_handle_sysrq_char(port, ch)))
			continue;
		uart_insert_char(port, 0, 0, ch, TTY_NORMAL);
		rx_count++;
		if (rx_count >= max_rx_count)
			break;
	}

	port->icount.rx += rx_count;
	tty_flip_buffer_push(&port->state->port);

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
					ufifo_st->ff_full(port->line)) &&
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
	if (unlikely(intr_status & (SIRFUART_ERR_INT_STAT(port, uint_st)))) {
		if (intr_status & uint_st->sirfsoc_rxd_brk) {
			port->icount.brk++;
			if (uart_handle_break(port))
				goto recv_char;
		}
		if (intr_status & uint_st->sirfsoc_rx_oflow)
			port->icount.overrun++;
		if (intr_status & uint_st->sirfsoc_frm_err) {
			port->icount.frame++;
			flag = TTY_FRAME;
		}
		if (intr_status & uint_st->sirfsoc_parity_err)
			flag = TTY_PARITY;
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
		wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_START);
		intr_status &= port->read_status_mask;
		uart_insert_char(port, intr_status,
					uint_en->sirfsoc_rx_oflow_en, 0, flag);
		tty_flip_buffer_push(&state->port);
	}
recv_char:
	if ((sirfport->uart_reg->uart_type == SIRF_REAL_UART) &&
			(intr_status & SIRFUART_CTS_INT_ST(uint_st))) {
		cts_status = rd_regl(port, ureg->sirfsoc_afc_ctrl) &
					SIRFUART_AFC_CTS_STATUS;
		if (cts_status != 0)
			cts_status = 0;
		else
			cts_status = 1;
		uart_handle_cts_change(port, cts_status);
		wake_up_interruptible(&state->port.delta_msr_wait);
	}
	if (intr_status & SIRFUART_RX_IO_INT_ST(uint_st))
		sirfsoc_uart_pio_rx_chars(port, SIRFSOC_UART_IO_RX_MAX_CNT);
	if (intr_status & uint_st->sirfsoc_txfifo_empty) {
		if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
			spin_unlock(&port->lock);
			return IRQ_HANDLED;
		} else {
			sirfsoc_uart_pio_tx_chars(sirfport,
					SIRFSOC_UART_IO_TX_REASONABLE_CNT);
			if ((uart_circ_empty(xmit)) &&
				(rd_regl(port, ureg->sirfsoc_tx_fifo_status) &
						ufifo_st->ff_empty(port->line)))
				sirfsoc_uart_stop_tx(port);
		}
	}
	spin_unlock(&port->lock);
	return IRQ_HANDLED;
}

static void sirfsoc_uart_start_rx(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_int_en *uint_en = &sirfport->uart_reg->uart_int_en;
	unsigned long regv;
	if (!sirfport->is_marco) {
		regv = rd_regl(port, ureg->sirfsoc_int_en_reg);
		wr_regl(port, ureg->sirfsoc_int_en_reg, regv |
			SIRFUART_RX_IO_INT_EN(port, uint_en));
	} else
		wr_regl(port, ureg->sirfsoc_int_en_reg,
				SIRFUART_RX_IO_INT_EN(port, uint_en));
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_START);
}

static unsigned int
sirfsoc_usp_calc_sample_div(unsigned long set_rate,
		unsigned long ioclk_rate, unsigned long *sample_reg)
{
	unsigned long min_delta = ~0UL;
	unsigned short sample_div;
	unsigned long ioclk_div = 0;
	unsigned long temp_delta;

	for (sample_div = SIRF_MIN_SAMPLE_DIV;
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
	unsigned long	temp_reg_val, ioclk_rate;
	unsigned long	rx_time_out;
	int		threshold_div;
	int		temp;
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
	}
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART) {
		if (termios->c_iflag & INPCK)
			port->read_status_mask |= uint_en->sirfsoc_frm_err_en;
	}
	if (termios->c_iflag & (BRKINT | PARMRK))
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
			} else if (termios->c_cflag & PARODD) {
				config_reg |= SIRFUART_STICK_BIT_ODD;
			} else {
				config_reg |= SIRFUART_STICK_BIT_EVEN;
			}
		}
	}
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART) {
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
	}
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART) {
		clk_div_reg = sirfsoc_usp_calc_sample_div(baud_rate,
				ioclk_rate, &sample_div_reg);
		sample_div_reg--;
		set_baud = ((ioclk_rate / (clk_div_reg+1) - 1) /
				(sample_div_reg + 1));
		/* setting usp mode 2 */
		len_val = ((1 << 0) | (1 << 8));
		len_val |= ((clk_div_reg & 0x3ff) << 21);
		wr_regl(port, ureg->sirfsoc_mode2,
				len_val);

	}
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, set_baud, set_baud);
	/* set receive timeout && data bits len */
	rx_time_out = SIRFSOC_UART_RX_TIMEOUT(set_baud, 20000);
	rx_time_out = SIRFUART_RECV_TIMEOUT_VALUE(rx_time_out);
	temp_reg_val = rd_regl(port, ureg->sirfsoc_tx_fifo_op);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	wr_regl(port, ureg->sirfsoc_tx_fifo_op,
			(temp_reg_val & ~SIRFUART_FIFO_START));
	if (sirfport->uart_reg->uart_type == SIRF_REAL_UART) {
		config_reg |= SIRFUART_RECV_TIMEOUT(port, rx_time_out);
		wr_regl(port, ureg->sirfsoc_line_ctrl, config_reg);
	}
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART) {
		/*tx frame ctrl*/
		len_val = (data_bit_len - 1) << 0;
		len_val |= (data_bit_len + 1 + stop_bit_len - 1) << 16;
		len_val |= ((data_bit_len - 1) << 24);
		len_val |= (((clk_div_reg & 0xc00) >> 10) << 30);
		wr_regl(port, ureg->sirfsoc_tx_frame_ctrl, len_val);
		/*rx frame ctrl*/
		len_val = (data_bit_len - 1) << 0;
		len_val |= (data_bit_len + 1 + stop_bit_len - 1) << 8;
		len_val |= (data_bit_len - 1) << 16;
		len_val |= (((clk_div_reg & 0xf000) >> 12) << 24);
		wr_regl(port, ureg->sirfsoc_rx_frame_ctrl, len_val);
		/*async param*/
		wr_regl(port, ureg->sirfsoc_async_param_reg,
			(SIRFUART_RECV_TIMEOUT(port, rx_time_out)) |
			(sample_div_reg & 0x3f) << 16);
	}
	wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl, SIRFUART_IO_MODE);
	wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl, SIRFUART_IO_MODE);
	/* Reset Rx/Tx FIFO Threshold level for proper baudrate */
	if (set_baud < 1000000)
		threshold_div = 1;
	else
		threshold_div = 2;
	temp = SIRFUART_FIFO_THD(port);
	wr_regl(port, ureg->sirfsoc_tx_fifo_ctrl, temp / threshold_div);
	wr_regl(port, ureg->sirfsoc_rx_fifo_ctrl, temp / threshold_div);
	temp_reg_val |= SIRFUART_FIFO_START;
	wr_regl(port, ureg->sirfsoc_tx_fifo_op, temp_reg_val);
	uart_update_timeout(port, termios->c_cflag, set_baud);
	sirfsoc_uart_start_rx(port);
	wr_regl(port, ureg->sirfsoc_tx_rx_en, SIRFUART_TX_EN | SIRFUART_RX_EN);
	spin_unlock_irqrestore(&port->lock, flags);
}

static void startup_uart_controller(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	unsigned long temp_regv;
	int temp;
	temp_regv = rd_regl(port, ureg->sirfsoc_tx_dma_io_ctrl);
	wr_regl(port, ureg->sirfsoc_tx_dma_io_ctrl, temp_regv |
					SIRFUART_IO_MODE);
	temp_regv = rd_regl(port, ureg->sirfsoc_rx_dma_io_ctrl);
	wr_regl(port, ureg->sirfsoc_rx_dma_io_ctrl, temp_regv |
					SIRFUART_IO_MODE);
	wr_regl(port, ureg->sirfsoc_tx_dma_io_len, 0);
	wr_regl(port, ureg->sirfsoc_rx_dma_io_len, 0);
	wr_regl(port, ureg->sirfsoc_tx_rx_en, SIRFUART_RX_EN | SIRFUART_TX_EN);
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART)
		wr_regl(port, ureg->sirfsoc_mode1,
				SIRFSOC_USP_ENDIAN_CTRL_LSBF |
				SIRFSOC_USP_EN);
	wr_regl(port, ureg->sirfsoc_tx_fifo_op, SIRFUART_FIFO_RESET);
	wr_regl(port, ureg->sirfsoc_tx_fifo_op, 0);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, SIRFUART_FIFO_RESET);
	wr_regl(port, ureg->sirfsoc_rx_fifo_op, 0);
	temp = SIRFUART_FIFO_THD(port);
	wr_regl(port, ureg->sirfsoc_tx_fifo_ctrl, temp);
	wr_regl(port, ureg->sirfsoc_rx_fifo_ctrl, temp);
}

static int sirfsoc_uart_startup(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport	= to_sirfport(port);
	unsigned int index			= port->line;
	int ret;
	set_irq_flags(port->irq, IRQF_VALID | IRQF_NOAUTOEN);
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
	startup_uart_controller(port);
	enable_irq(port->irq);
irq_err:
	return ret;
}

static void sirfsoc_uart_shutdown(struct uart_port *port)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	if (!sirfport->is_marco)
		wr_regl(port, ureg->sirfsoc_int_en_reg, 0);
	else
		wr_regl(port, SIRFUART_INT_EN_CLR, ~0UL);

	free_irq(port->irq, sirfport);
	if (sirfport->ms_enabled) {
		sirfsoc_uart_disable_ms(port);
		sirfport->ms_enabled = 0;
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
	struct uart_port *port = &sirfsoc_uart_ports[co->index].port;
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	if (co->index < 0 || co->index >= SIRFSOC_UART_NR)
		return -EINVAL;

	if (!port->mapbase)
		return -ENODEV;

	/* enable usp in mode1 register */
	if (sirfport->uart_reg->uart_type == SIRF_USP_UART)
		wr_regl(port, ureg->sirfsoc_mode1, SIRFSOC_USP_EN |
				SIRFSOC_USP_ENDIAN_CTRL_LSBF);
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	port->cons = co;

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static void sirfsoc_uart_console_putchar(struct uart_port *port, int ch)
{
	struct sirfsoc_uart_port *sirfport = to_sirfport(port);
	struct sirfsoc_register *ureg = &sirfport->uart_reg->uart_reg;
	struct sirfsoc_fifo_status *ufifo_st = &sirfport->uart_reg->fifo_status;
	while (rd_regl(port,
		ureg->sirfsoc_tx_fifo_status) & ufifo_st->ff_full(port->line))
		cpu_relax();
	wr_regb(port, ureg->sirfsoc_tx_fifo_data, ch);
}

static void sirfsoc_uart_console_write(struct console *co, const char *s,
							unsigned int count)
{
	struct uart_port *port = &sirfsoc_uart_ports[co->index].port;
	uart_console_write(port, s, count, sirfsoc_uart_console_putchar);
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

static struct of_device_id sirfsoc_uart_ids[] = {
	{ .compatible = "sirf,prima2-uart", .data = &sirfsoc_uart,},
	{ .compatible = "sirf,marco-uart", .data = &sirfsoc_uart},
	{ .compatible = "sirf,prima2-usp-uart", .data = &sirfsoc_usp},
	{}
};
MODULE_DEVICE_TABLE(of, sirfsoc_uart_ids);

int sirfsoc_uart_probe(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport;
	struct uart_port *port;
	struct resource *res;
	int ret;
	const struct of_device_id *match;

	match = of_match_node(sirfsoc_uart_ids, pdev->dev.of_node);
	if (of_property_read_u32(pdev->dev.of_node, "cell-index", &pdev->id)) {
		dev_err(&pdev->dev,
			"Unable to find cell-index in uart node.\n");
		ret = -EFAULT;
		goto err;
	}
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,prima2-usp-uart"))
		pdev->id += ((struct sirfsoc_uart_register *)
				match->data)->uart_param.register_uart_nr;
	sirfport = &sirfsoc_uart_ports[pdev->id];
	port = &sirfport->port;
	port->dev = &pdev->dev;
	port->private_data = sirfport;
	sirfport->uart_reg = (struct sirfsoc_uart_register *)match->data;

	if (of_device_is_compatible(pdev->dev.of_node, "sirf,prima2-uart"))
		sirfport->uart_reg->uart_type = SIRF_REAL_UART;
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,prima2-usp-uart"))
		sirfport->uart_reg->uart_type =	SIRF_USP_UART;
	if (of_device_is_compatible(pdev->dev.of_node, "sirf,marco-uart"))
		sirfport->is_marco = true;

	if (of_find_property(pdev->dev.of_node, "hw_flow_ctrl", NULL))
		sirfport->hw_flow_ctrl = 1;

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
	port->membase = devm_ioremap(&pdev->dev, res->start, resource_size(res));
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

	if (sirfport->hw_flow_ctrl) {
		sirfport->p = pinctrl_get_select_default(&pdev->dev);
		ret = IS_ERR(sirfport->p);
		if (ret)
			goto err;
	}

	sirfport->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sirfport->clk)) {
		ret = PTR_ERR(sirfport->clk);
		goto clk_err;
	}
	clk_prepare_enable(sirfport->clk);
	port->uartclk = clk_get_rate(sirfport->clk);

	port->ops = &sirfsoc_uart_ops;
	spin_lock_init(&port->lock);

	platform_set_drvdata(pdev, sirfport);
	ret = uart_add_one_port(&sirfsoc_uart_drv, port);
	if (ret != 0) {
		dev_err(&pdev->dev, "Cannot add UART port(%d).\n", pdev->id);
		goto port_err;
	}

	return 0;

port_err:
	clk_disable_unprepare(sirfport->clk);
	clk_put(sirfport->clk);
clk_err:
	if (sirfport->hw_flow_ctrl)
		pinctrl_put(sirfport->p);
err:
	return ret;
}

static int sirfsoc_uart_remove(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;

	if (sirfport->hw_flow_ctrl)
		pinctrl_put(sirfport->p);
	clk_disable_unprepare(sirfport->clk);
	clk_put(sirfport->clk);
	uart_remove_one_port(&sirfsoc_uart_drv, port);
	return 0;
}

static int
sirfsoc_uart_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_suspend_port(&sirfsoc_uart_drv, port);
	return 0;
}

static int sirfsoc_uart_resume(struct platform_device *pdev)
{
	struct sirfsoc_uart_port *sirfport = platform_get_drvdata(pdev);
	struct uart_port *port = &sirfport->port;
	uart_resume_port(&sirfsoc_uart_drv, port);
	return 0;
}

static struct platform_driver sirfsoc_uart_driver = {
	.probe		= sirfsoc_uart_probe,
	.remove		= sirfsoc_uart_remove,
	.suspend	= sirfsoc_uart_suspend,
	.resume		= sirfsoc_uart_resume,
	.driver		= {
		.name	= SIRFUART_PORT_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = sirfsoc_uart_ids,
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
