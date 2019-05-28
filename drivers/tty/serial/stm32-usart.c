// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics SA 2017
 * Authors:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 *	     Gerald Baeza <gerald.baeza@st.com>
 *
 * Inspired by st-asc.c from STMicroelectronics (c)
 */

#if defined(CONFIG_SERIAL_STM32_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/tty_flip.h>
#include <linux/tty.h>

#include "stm32-usart.h"

static void stm32_stop_tx(struct uart_port *port);
static void stm32_transmit_chars(struct uart_port *port);

static inline struct stm32_port *to_stm32_port(struct uart_port *port)
{
	return container_of(port, struct stm32_port, port);
}

static void stm32_set_bits(struct uart_port *port, u32 reg, u32 bits)
{
	u32 val;

	val = readl_relaxed(port->membase + reg);
	val |= bits;
	writel_relaxed(val, port->membase + reg);
}

static void stm32_clr_bits(struct uart_port *port, u32 reg, u32 bits)
{
	u32 val;

	val = readl_relaxed(port->membase + reg);
	val &= ~bits;
	writel_relaxed(val, port->membase + reg);
}

static void stm32_config_reg_rs485(u32 *cr1, u32 *cr3, u32 delay_ADE,
				   u32 delay_DDE, u32 baud)
{
	u32 rs485_deat_dedt;
	u32 rs485_deat_dedt_max = (USART_CR1_DEAT_MASK >> USART_CR1_DEAT_SHIFT);
	bool over8;

	*cr3 |= USART_CR3_DEM;
	over8 = *cr1 & USART_CR1_OVER8;

	if (over8)
		rs485_deat_dedt = delay_ADE * baud * 8;
	else
		rs485_deat_dedt = delay_ADE * baud * 16;

	rs485_deat_dedt = DIV_ROUND_CLOSEST(rs485_deat_dedt, 1000);
	rs485_deat_dedt = rs485_deat_dedt > rs485_deat_dedt_max ?
			  rs485_deat_dedt_max : rs485_deat_dedt;
	rs485_deat_dedt = (rs485_deat_dedt << USART_CR1_DEAT_SHIFT) &
			   USART_CR1_DEAT_MASK;
	*cr1 |= rs485_deat_dedt;

	if (over8)
		rs485_deat_dedt = delay_DDE * baud * 8;
	else
		rs485_deat_dedt = delay_DDE * baud * 16;

	rs485_deat_dedt = DIV_ROUND_CLOSEST(rs485_deat_dedt, 1000);
	rs485_deat_dedt = rs485_deat_dedt > rs485_deat_dedt_max ?
			  rs485_deat_dedt_max : rs485_deat_dedt;
	rs485_deat_dedt = (rs485_deat_dedt << USART_CR1_DEDT_SHIFT) &
			   USART_CR1_DEDT_MASK;
	*cr1 |= rs485_deat_dedt;
}

static int stm32_config_rs485(struct uart_port *port,
			      struct serial_rs485 *rs485conf)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct stm32_usart_config *cfg = &stm32_port->info->cfg;
	u32 usartdiv, baud, cr1, cr3;
	bool over8;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	stm32_clr_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));

	port->rs485 = *rs485conf;

	rs485conf->flags |= SER_RS485_RX_DURING_TX;

	if (rs485conf->flags & SER_RS485_ENABLED) {
		cr1 = readl_relaxed(port->membase + ofs->cr1);
		cr3 = readl_relaxed(port->membase + ofs->cr3);
		usartdiv = readl_relaxed(port->membase + ofs->brr);
		usartdiv = usartdiv & GENMASK(15, 0);
		over8 = cr1 & USART_CR1_OVER8;

		if (over8)
			usartdiv = usartdiv | (usartdiv & GENMASK(4, 0))
				   << USART_BRR_04_R_SHIFT;

		baud = DIV_ROUND_CLOSEST(port->uartclk, usartdiv);
		stm32_config_reg_rs485(&cr1, &cr3,
				       rs485conf->delay_rts_before_send,
				       rs485conf->delay_rts_after_send, baud);

		if (rs485conf->flags & SER_RS485_RTS_ON_SEND) {
			cr3 &= ~USART_CR3_DEP;
			rs485conf->flags &= ~SER_RS485_RTS_AFTER_SEND;
		} else {
			cr3 |= USART_CR3_DEP;
			rs485conf->flags |= SER_RS485_RTS_AFTER_SEND;
		}

		writel_relaxed(cr3, port->membase + ofs->cr3);
		writel_relaxed(cr1, port->membase + ofs->cr1);
	} else {
		stm32_clr_bits(port, ofs->cr3, USART_CR3_DEM | USART_CR3_DEP);
		stm32_clr_bits(port, ofs->cr1,
			       USART_CR1_DEDT_MASK | USART_CR1_DEAT_MASK);
	}

	stm32_set_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));
	spin_unlock_irqrestore(&port->lock, flags);

	return 0;
}

static int stm32_init_rs485(struct uart_port *port,
			    struct platform_device *pdev)
{
	struct serial_rs485 *rs485conf = &port->rs485;

	rs485conf->flags = 0;
	rs485conf->delay_rts_before_send = 0;
	rs485conf->delay_rts_after_send = 0;

	if (!pdev->dev.of_node)
		return -ENODEV;

	uart_get_rs485_mode(&pdev->dev, rs485conf);

	return 0;
}

static int stm32_pending_rx(struct uart_port *port, u32 *sr, int *last_res,
			    bool threaded)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	enum dma_status status;
	struct dma_tx_state state;

	*sr = readl_relaxed(port->membase + ofs->isr);

	if (threaded && stm32_port->rx_ch) {
		status = dmaengine_tx_status(stm32_port->rx_ch,
					     stm32_port->rx_ch->cookie,
					     &state);
		if ((status == DMA_IN_PROGRESS) &&
		    (*last_res != state.residue))
			return 1;
		else
			return 0;
	} else if (*sr & USART_SR_RXNE) {
		return 1;
	}
	return 0;
}

static unsigned long stm32_get_char(struct uart_port *port, u32 *sr,
				    int *last_res)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long c;

	if (stm32_port->rx_ch) {
		c = stm32_port->rx_buf[RX_BUF_L - (*last_res)--];
		if ((*last_res) == 0)
			*last_res = RX_BUF_L;
	} else {
		c = readl_relaxed(port->membase + ofs->rdr);
		/* apply RDR data mask */
		c &= stm32_port->rdr_mask;
	}

	return c;
}

static void stm32_receive_chars(struct uart_port *port, bool threaded)
{
	struct tty_port *tport = &port->state->port;
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long c;
	u32 sr;
	char flag;

	if (irqd_is_wakeup_set(irq_get_irq_data(port->irq)))
		pm_wakeup_event(tport->tty->dev, 0);

	while (stm32_pending_rx(port, &sr, &stm32_port->last_res, threaded)) {
		sr |= USART_SR_DUMMY_RX;
		flag = TTY_NORMAL;

		/*
		 * Status bits has to be cleared before reading the RDR:
		 * In FIFO mode, reading the RDR will pop the next data
		 * (if any) along with its status bits into the SR.
		 * Not doing so leads to misalignement between RDR and SR,
		 * and clear status bits of the next rx data.
		 *
		 * Clear errors flags for stm32f7 and stm32h7 compatible
		 * devices. On stm32f4 compatible devices, the error bit is
		 * cleared by the sequence [read SR - read DR].
		 */
		if ((sr & USART_SR_ERR_MASK) && ofs->icr != UNDEF_REG)
			stm32_clr_bits(port, ofs->icr, USART_ICR_ORECF |
				       USART_ICR_PECF | USART_ICR_FECF);

		c = stm32_get_char(port, &sr, &stm32_port->last_res);
		port->icount.rx++;
		if (sr & USART_SR_ERR_MASK) {
			if (sr & USART_SR_ORE) {
				port->icount.overrun++;
			} else if (sr & USART_SR_PE) {
				port->icount.parity++;
			} else if (sr & USART_SR_FE) {
				/* Break detection if character is null */
				if (!c) {
					port->icount.brk++;
					if (uart_handle_break(port))
						continue;
				} else {
					port->icount.frame++;
				}
			}

			sr &= port->read_status_mask;

			if (sr & USART_SR_PE) {
				flag = TTY_PARITY;
			} else if (sr & USART_SR_FE) {
				if (!c)
					flag = TTY_BREAK;
				else
					flag = TTY_FRAME;
			}
		}

		if (uart_handle_sysrq_char(port, c))
			continue;
		uart_insert_char(port, sr, USART_SR_ORE, c, flag);
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tport);
	spin_lock(&port->lock);
}

static void stm32_tx_dma_complete(void *arg)
{
	struct uart_port *port = arg;
	struct stm32_port *stm32port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32port->info->ofs;

	stm32_clr_bits(port, ofs->cr3, USART_CR3_DMAT);
	stm32port->tx_dma_busy = false;

	/* Let's see if we have pending data to send */
	stm32_transmit_chars(port);
}

static void stm32_transmit_chars_pio(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct circ_buf *xmit = &port->state->xmit;
	unsigned int isr;
	int ret;

	if (stm32_port->tx_dma_busy) {
		stm32_clr_bits(port, ofs->cr3, USART_CR3_DMAT);
		stm32_port->tx_dma_busy = false;
	}

	ret = readl_relaxed_poll_timeout_atomic(port->membase + ofs->isr,
						isr,
						(isr & USART_SR_TXE),
						10, 100000);

	if (ret)
		dev_err(port->dev, "tx empty not set\n");

	stm32_set_bits(port, ofs->cr1, USART_CR1_TXEIE);

	writel_relaxed(xmit->buf[xmit->tail], port->membase + ofs->tdr);
	xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
	port->icount.tx++;
}

static void stm32_transmit_chars_dma(struct uart_port *port)
{
	struct stm32_port *stm32port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32port->info->ofs;
	struct circ_buf *xmit = &port->state->xmit;
	struct dma_async_tx_descriptor *desc = NULL;
	dma_cookie_t cookie;
	unsigned int count, i;

	if (stm32port->tx_dma_busy)
		return;

	stm32port->tx_dma_busy = true;

	count = uart_circ_chars_pending(xmit);

	if (count > TX_BUF_L)
		count = TX_BUF_L;

	if (xmit->tail < xmit->head) {
		memcpy(&stm32port->tx_buf[0], &xmit->buf[xmit->tail], count);
	} else {
		size_t one = UART_XMIT_SIZE - xmit->tail;
		size_t two;

		if (one > count)
			one = count;
		two = count - one;

		memcpy(&stm32port->tx_buf[0], &xmit->buf[xmit->tail], one);
		if (two)
			memcpy(&stm32port->tx_buf[one], &xmit->buf[0], two);
	}

	desc = dmaengine_prep_slave_single(stm32port->tx_ch,
					   stm32port->tx_dma_buf,
					   count,
					   DMA_MEM_TO_DEV,
					   DMA_PREP_INTERRUPT);

	if (!desc) {
		for (i = count; i > 0; i--)
			stm32_transmit_chars_pio(port);
		return;
	}

	desc->callback = stm32_tx_dma_complete;
	desc->callback_param = port;

	/* Push current DMA TX transaction in the pending queue */
	cookie = dmaengine_submit(desc);

	/* Issue pending DMA TX requests */
	dma_async_issue_pending(stm32port->tx_ch);

	stm32_set_bits(port, ofs->cr3, USART_CR3_DMAT);

	xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE - 1);
	port->icount.tx += count;
}

static void stm32_transmit_chars(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		if (stm32_port->tx_dma_busy)
			stm32_clr_bits(port, ofs->cr3, USART_CR3_DMAT);
		writel_relaxed(port->x_char, port->membase + ofs->tdr);
		port->x_char = 0;
		port->icount.tx++;
		if (stm32_port->tx_dma_busy)
			stm32_set_bits(port, ofs->cr3, USART_CR3_DMAT);
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		stm32_clr_bits(port, ofs->cr1, USART_CR1_TXEIE);
		return;
	}

	if (ofs->icr == UNDEF_REG)
		stm32_clr_bits(port, ofs->isr, USART_SR_TC);
	else
		stm32_set_bits(port, ofs->icr, USART_ICR_TCCF);

	if (stm32_port->tx_ch)
		stm32_transmit_chars_dma(port);
	else
		stm32_transmit_chars_pio(port);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		stm32_clr_bits(port, ofs->cr1, USART_CR1_TXEIE);
}

static irqreturn_t stm32_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	u32 sr;

	spin_lock(&port->lock);

	sr = readl_relaxed(port->membase + ofs->isr);

	if ((sr & USART_SR_WUF) && (ofs->icr != UNDEF_REG))
		writel_relaxed(USART_ICR_WUCF,
			       port->membase + ofs->icr);

	if ((sr & USART_SR_RXNE) && !(stm32_port->rx_ch))
		stm32_receive_chars(port, false);

	if ((sr & USART_SR_TXE) && !(stm32_port->tx_ch))
		stm32_transmit_chars(port);

	spin_unlock(&port->lock);

	if (stm32_port->rx_ch)
		return IRQ_WAKE_THREAD;
	else
		return IRQ_HANDLED;
}

static irqreturn_t stm32_threaded_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	struct stm32_port *stm32_port = to_stm32_port(port);

	spin_lock(&port->lock);

	if (stm32_port->rx_ch)
		stm32_receive_chars(port, true);

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static unsigned int stm32_tx_empty(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	return readl_relaxed(port->membase + ofs->isr) & USART_SR_TXE;
}

static void stm32_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	if ((mctrl & TIOCM_RTS) && (port->status & UPSTAT_AUTORTS))
		stm32_set_bits(port, ofs->cr3, USART_CR3_RTSE);
	else
		stm32_clr_bits(port, ofs->cr3, USART_CR3_RTSE);
}

static unsigned int stm32_get_mctrl(struct uart_port *port)
{
	/* This routine is used to get signals of: DCD, DSR, RI, and CTS */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/* Transmit stop */
static void stm32_stop_tx(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	stm32_clr_bits(port, ofs->cr1, USART_CR1_TXEIE);
}

/* There are probably characters waiting to be transmitted. */
static void stm32_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (uart_circ_empty(xmit))
		return;

	stm32_transmit_chars(port);
}

/* Throttle the remote when input buffer is about to overflow. */
static void stm32_throttle(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	stm32_clr_bits(port, ofs->cr1, USART_CR1_RXNEIE);
	spin_unlock_irqrestore(&port->lock, flags);
}

/* Unthrottle the remote, the input buffer can now accept data. */
static void stm32_unthrottle(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	stm32_set_bits(port, ofs->cr1, USART_CR1_RXNEIE);
	spin_unlock_irqrestore(&port->lock, flags);
}

/* Receive stop */
static void stm32_stop_rx(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	stm32_clr_bits(port, ofs->cr1, USART_CR1_RXNEIE);
}

/* Handle breaks - ignored by us */
static void stm32_break_ctl(struct uart_port *port, int break_state)
{
}

static int stm32_startup(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	const char *name = to_platform_device(port->dev)->name;
	u32 val;
	int ret;

	ret = request_threaded_irq(port->irq, stm32_interrupt,
				   stm32_threaded_interrupt,
				   IRQF_NO_SUSPEND, name, port);
	if (ret)
		return ret;

	val = USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;
	if (stm32_port->fifoen)
		val |= USART_CR1_FIFOEN;
	stm32_set_bits(port, ofs->cr1, val);

	return 0;
}

static void stm32_shutdown(struct uart_port *port)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct stm32_usart_config *cfg = &stm32_port->info->cfg;
	u32 val, isr;
	int ret;

	val = USART_CR1_TXEIE | USART_CR1_RXNEIE | USART_CR1_TE | USART_CR1_RE;
	val |= BIT(cfg->uart_enable_bit);
	if (stm32_port->fifoen)
		val |= USART_CR1_FIFOEN;

	ret = readl_relaxed_poll_timeout(port->membase + ofs->isr,
					 isr, (isr & USART_SR_TC),
					 10, 100000);

	if (ret)
		dev_err(port->dev, "transmission complete not set\n");

	stm32_clr_bits(port, ofs->cr1, val);

	free_irq(port->irq, port);
}

static unsigned int stm32_get_databits(struct ktermios *termios)
{
	unsigned int bits;

	tcflag_t cflag = termios->c_cflag;

	switch (cflag & CSIZE) {
	/*
	 * CSIZE settings are not necessarily supported in hardware.
	 * CSIZE unsupported configurations are handled here to set word length
	 * to 8 bits word as default configuration and to print debug message.
	 */
	case CS5:
		bits = 5;
		break;
	case CS6:
		bits = 6;
		break;
	case CS7:
		bits = 7;
		break;
	/* default including CS8 */
	default:
		bits = 8;
		break;
	}

	return bits;
}

static void stm32_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct stm32_usart_config *cfg = &stm32_port->info->cfg;
	struct serial_rs485 *rs485conf = &port->rs485;
	unsigned int baud, bits;
	u32 usartdiv, mantissa, fraction, oversampling;
	tcflag_t cflag = termios->c_cflag;
	u32 cr1, cr2, cr3;
	unsigned long flags;

	if (!stm32_port->hw_flow_control)
		cflag &= ~CRTSCTS;

	baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk / 8);

	spin_lock_irqsave(&port->lock, flags);

	/* Stop serial port and reset value */
	writel_relaxed(0, port->membase + ofs->cr1);

	cr1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

	if (stm32_port->fifoen)
		cr1 |= USART_CR1_FIFOEN;
	cr2 = 0;
	cr3 = 0;

	if (cflag & CSTOPB)
		cr2 |= USART_CR2_STOP_2B;

	bits = stm32_get_databits(termios);
	stm32_port->rdr_mask = (BIT(bits) - 1);

	if (cflag & PARENB) {
		bits++;
		cr1 |= USART_CR1_PCE;
	}

	/*
	 * Word length configuration:
	 * CS8 + parity, 9 bits word aka [M1:M0] = 0b01
	 * CS7 or (CS6 + parity), 7 bits word aka [M1:M0] = 0b10
	 * CS8 or (CS7 + parity), 8 bits word aka [M1:M0] = 0b00
	 * M0 and M1 already cleared by cr1 initialization.
	 */
	if (bits == 9)
		cr1 |= USART_CR1_M0;
	else if ((bits == 7) && cfg->has_7bits_data)
		cr1 |= USART_CR1_M1;
	else if (bits != 8)
		dev_dbg(port->dev, "Unsupported data bits config: %u bits\n"
			, bits);

	if (cflag & PARODD)
		cr1 |= USART_CR1_PS;

	port->status &= ~(UPSTAT_AUTOCTS | UPSTAT_AUTORTS);
	if (cflag & CRTSCTS) {
		port->status |= UPSTAT_AUTOCTS | UPSTAT_AUTORTS;
		cr3 |= USART_CR3_CTSE | USART_CR3_RTSE;
	}

	usartdiv = DIV_ROUND_CLOSEST(port->uartclk, baud);

	/*
	 * The USART supports 16 or 8 times oversampling.
	 * By default we prefer 16 times oversampling, so that the receiver
	 * has a better tolerance to clock deviations.
	 * 8 times oversampling is only used to achieve higher speeds.
	 */
	if (usartdiv < 16) {
		oversampling = 8;
		cr1 |= USART_CR1_OVER8;
		stm32_set_bits(port, ofs->cr1, USART_CR1_OVER8);
	} else {
		oversampling = 16;
		cr1 &= ~USART_CR1_OVER8;
		stm32_clr_bits(port, ofs->cr1, USART_CR1_OVER8);
	}

	mantissa = (usartdiv / oversampling) << USART_BRR_DIV_M_SHIFT;
	fraction = usartdiv % oversampling;
	writel_relaxed(mantissa | fraction, port->membase + ofs->brr);

	uart_update_timeout(port, cflag, baud);

	port->read_status_mask = USART_SR_ORE;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= USART_SR_PE | USART_SR_FE;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		port->read_status_mask |= USART_SR_FE;

	/* Characters to ignore */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask = USART_SR_PE | USART_SR_FE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= USART_SR_FE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= USART_SR_ORE;
	}

	/* Ignore all characters if CREAD is not set */
	if ((termios->c_cflag & CREAD) == 0)
		port->ignore_status_mask |= USART_SR_DUMMY_RX;

	if (stm32_port->rx_ch)
		cr3 |= USART_CR3_DMAR;

	if (rs485conf->flags & SER_RS485_ENABLED) {
		stm32_config_reg_rs485(&cr1, &cr3,
				       rs485conf->delay_rts_before_send,
				       rs485conf->delay_rts_after_send, baud);
		if (rs485conf->flags & SER_RS485_RTS_ON_SEND) {
			cr3 &= ~USART_CR3_DEP;
			rs485conf->flags &= ~SER_RS485_RTS_AFTER_SEND;
		} else {
			cr3 |= USART_CR3_DEP;
			rs485conf->flags |= SER_RS485_RTS_AFTER_SEND;
		}

	} else {
		cr3 &= ~(USART_CR3_DEM | USART_CR3_DEP);
		cr1 &= ~(USART_CR1_DEDT_MASK | USART_CR1_DEAT_MASK);
	}

	writel_relaxed(cr3, port->membase + ofs->cr3);
	writel_relaxed(cr2, port->membase + ofs->cr2);
	writel_relaxed(cr1, port->membase + ofs->cr1);

	stm32_set_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));
	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *stm32_type(struct uart_port *port)
{
	return (port->type == PORT_STM32) ? DRIVER_NAME : NULL;
}

static void stm32_release_port(struct uart_port *port)
{
}

static int stm32_request_port(struct uart_port *port)
{
	return 0;
}

static void stm32_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_STM32;
}

static int
stm32_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* No user changeable parameters */
	return -EINVAL;
}

static void stm32_pm(struct uart_port *port, unsigned int state,
		unsigned int oldstate)
{
	struct stm32_port *stm32port = container_of(port,
			struct stm32_port, port);
	struct stm32_usart_offsets *ofs = &stm32port->info->ofs;
	struct stm32_usart_config *cfg = &stm32port->info->cfg;
	unsigned long flags = 0;

	switch (state) {
	case UART_PM_STATE_ON:
		clk_prepare_enable(stm32port->clk);
		break;
	case UART_PM_STATE_OFF:
		spin_lock_irqsave(&port->lock, flags);
		stm32_clr_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));
		spin_unlock_irqrestore(&port->lock, flags);
		clk_disable_unprepare(stm32port->clk);
		break;
	}
}

static const struct uart_ops stm32_uart_ops = {
	.tx_empty	= stm32_tx_empty,
	.set_mctrl	= stm32_set_mctrl,
	.get_mctrl	= stm32_get_mctrl,
	.stop_tx	= stm32_stop_tx,
	.start_tx	= stm32_start_tx,
	.throttle	= stm32_throttle,
	.unthrottle	= stm32_unthrottle,
	.stop_rx	= stm32_stop_rx,
	.break_ctl	= stm32_break_ctl,
	.startup	= stm32_startup,
	.shutdown	= stm32_shutdown,
	.set_termios	= stm32_set_termios,
	.pm		= stm32_pm,
	.type		= stm32_type,
	.release_port	= stm32_release_port,
	.request_port	= stm32_request_port,
	.config_port	= stm32_config_port,
	.verify_port	= stm32_verify_port,
};

static int stm32_init_port(struct stm32_port *stm32port,
			  struct platform_device *pdev)
{
	struct uart_port *port = &stm32port->port;
	struct resource *res;
	int ret;

	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &stm32_uart_ops;
	port->dev	= &pdev->dev;

	ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Can't get event IRQ: %d\n", ret);
		return ret ? ret : -ENODEV;
	}
	port->irq = ret;

	port->rs485_config = stm32_config_rs485;

	stm32_init_rs485(port, pdev);

	if (stm32port->info->cfg.has_wakeup) {
		stm32port->wakeirq = platform_get_irq(pdev, 1);
		if (stm32port->wakeirq <= 0 && stm32port->wakeirq != -ENXIO) {
			if (stm32port->wakeirq != -EPROBE_DEFER)
				dev_err(&pdev->dev,
					"Can't get event wake IRQ: %d\n",
					stm32port->wakeirq);
			return stm32port->wakeirq ? stm32port->wakeirq :
				-ENODEV;
		}
	}

	stm32port->fifoen = stm32port->info->cfg.has_fifo;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);
	port->mapbase = res->start;

	spin_lock_init(&port->lock);

	stm32port->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(stm32port->clk))
		return PTR_ERR(stm32port->clk);

	/* Ensure that clk rate is correct by enabling the clk */
	ret = clk_prepare_enable(stm32port->clk);
	if (ret)
		return ret;

	stm32port->port.uartclk = clk_get_rate(stm32port->clk);
	if (!stm32port->port.uartclk) {
		clk_disable_unprepare(stm32port->clk);
		ret = -EINVAL;
	}

	return ret;
}

static struct stm32_port *stm32_of_get_stm32_port(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int id;

	if (!np)
		return NULL;

	id = of_alias_get_id(np, "serial");
	if (id < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", id);
		return NULL;
	}

	if (WARN_ON(id >= STM32_MAX_PORTS))
		return NULL;

	stm32_ports[id].hw_flow_control = of_property_read_bool(np,
							"st,hw-flow-ctrl");
	stm32_ports[id].port.line = id;
	stm32_ports[id].last_res = RX_BUF_L;
	return &stm32_ports[id];
}

#ifdef CONFIG_OF
static const struct of_device_id stm32_match[] = {
	{ .compatible = "st,stm32-uart", .data = &stm32f4_info},
	{ .compatible = "st,stm32f7-uart", .data = &stm32f7_info},
	{ .compatible = "st,stm32h7-uart", .data = &stm32h7_info},
	{},
};

MODULE_DEVICE_TABLE(of, stm32_match);
#endif

static int stm32_of_dma_rx_probe(struct stm32_port *stm32port,
				 struct platform_device *pdev)
{
	struct stm32_usart_offsets *ofs = &stm32port->info->ofs;
	struct uart_port *port = &stm32port->port;
	struct device *dev = &pdev->dev;
	struct dma_slave_config config;
	struct dma_async_tx_descriptor *desc = NULL;
	dma_cookie_t cookie;
	int ret;

	/* Request DMA RX channel */
	stm32port->rx_ch = dma_request_slave_channel(dev, "rx");
	if (!stm32port->rx_ch) {
		dev_info(dev, "rx dma alloc failed\n");
		return -ENODEV;
	}
	stm32port->rx_buf = dma_alloc_coherent(&pdev->dev, RX_BUF_L,
						 &stm32port->rx_dma_buf,
						 GFP_KERNEL);
	if (!stm32port->rx_buf) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	/* Configure DMA channel */
	memset(&config, 0, sizeof(config));
	config.src_addr = port->mapbase + ofs->rdr;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;

	ret = dmaengine_slave_config(stm32port->rx_ch, &config);
	if (ret < 0) {
		dev_err(dev, "rx dma channel config failed\n");
		ret = -ENODEV;
		goto config_err;
	}

	/* Prepare a DMA cyclic transaction */
	desc = dmaengine_prep_dma_cyclic(stm32port->rx_ch,
					 stm32port->rx_dma_buf,
					 RX_BUF_L, RX_BUF_P, DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(dev, "rx dma prep cyclic failed\n");
		ret = -ENODEV;
		goto config_err;
	}

	/* No callback as dma buffer is drained on usart interrupt */
	desc->callback = NULL;
	desc->callback_param = NULL;

	/* Push current DMA transaction in the pending queue */
	cookie = dmaengine_submit(desc);

	/* Issue pending DMA requests */
	dma_async_issue_pending(stm32port->rx_ch);

	return 0;

config_err:
	dma_free_coherent(&pdev->dev,
			  RX_BUF_L, stm32port->rx_buf,
			  stm32port->rx_dma_buf);

alloc_err:
	dma_release_channel(stm32port->rx_ch);
	stm32port->rx_ch = NULL;

	return ret;
}

static int stm32_of_dma_tx_probe(struct stm32_port *stm32port,
				 struct platform_device *pdev)
{
	struct stm32_usart_offsets *ofs = &stm32port->info->ofs;
	struct uart_port *port = &stm32port->port;
	struct device *dev = &pdev->dev;
	struct dma_slave_config config;
	int ret;

	stm32port->tx_dma_busy = false;

	/* Request DMA TX channel */
	stm32port->tx_ch = dma_request_slave_channel(dev, "tx");
	if (!stm32port->tx_ch) {
		dev_info(dev, "tx dma alloc failed\n");
		return -ENODEV;
	}
	stm32port->tx_buf = dma_alloc_coherent(&pdev->dev, TX_BUF_L,
						 &stm32port->tx_dma_buf,
						 GFP_KERNEL);
	if (!stm32port->tx_buf) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	/* Configure DMA channel */
	memset(&config, 0, sizeof(config));
	config.dst_addr = port->mapbase + ofs->tdr;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;

	ret = dmaengine_slave_config(stm32port->tx_ch, &config);
	if (ret < 0) {
		dev_err(dev, "tx dma channel config failed\n");
		ret = -ENODEV;
		goto config_err;
	}

	return 0;

config_err:
	dma_free_coherent(&pdev->dev,
			  TX_BUF_L, stm32port->tx_buf,
			  stm32port->tx_dma_buf);

alloc_err:
	dma_release_channel(stm32port->tx_ch);
	stm32port->tx_ch = NULL;

	return ret;
}

static int stm32_serial_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct stm32_port *stm32port;
	int ret;

	stm32port = stm32_of_get_stm32_port(pdev);
	if (!stm32port)
		return -ENODEV;

	match = of_match_device(stm32_match, &pdev->dev);
	if (match && match->data)
		stm32port->info = (struct stm32_usart_info *)match->data;
	else
		return -EINVAL;

	ret = stm32_init_port(stm32port, pdev);
	if (ret)
		return ret;

	if (stm32port->wakeirq > 0) {
		ret = device_init_wakeup(&pdev->dev, true);
		if (ret)
			goto err_uninit;

		ret = dev_pm_set_dedicated_wake_irq(&pdev->dev,
						    stm32port->wakeirq);
		if (ret)
			goto err_nowup;

		device_set_wakeup_enable(&pdev->dev, false);
	}

	ret = uart_add_one_port(&stm32_usart_driver, &stm32port->port);
	if (ret)
		goto err_wirq;

	ret = stm32_of_dma_rx_probe(stm32port, pdev);
	if (ret)
		dev_info(&pdev->dev, "interrupt mode used for rx (no dma)\n");

	ret = stm32_of_dma_tx_probe(stm32port, pdev);
	if (ret)
		dev_info(&pdev->dev, "interrupt mode used for tx (no dma)\n");

	platform_set_drvdata(pdev, &stm32port->port);

	return 0;

err_wirq:
	if (stm32port->wakeirq > 0)
		dev_pm_clear_wake_irq(&pdev->dev);

err_nowup:
	if (stm32port->wakeirq > 0)
		device_init_wakeup(&pdev->dev, false);

err_uninit:
	clk_disable_unprepare(stm32port->clk);

	return ret;
}

static int stm32_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	stm32_clr_bits(port, ofs->cr3, USART_CR3_DMAR);

	if (stm32_port->rx_ch)
		dma_release_channel(stm32_port->rx_ch);

	if (stm32_port->rx_dma_buf)
		dma_free_coherent(&pdev->dev,
				  RX_BUF_L, stm32_port->rx_buf,
				  stm32_port->rx_dma_buf);

	stm32_clr_bits(port, ofs->cr3, USART_CR3_DMAT);

	if (stm32_port->tx_ch)
		dma_release_channel(stm32_port->tx_ch);

	if (stm32_port->tx_dma_buf)
		dma_free_coherent(&pdev->dev,
				  TX_BUF_L, stm32_port->tx_buf,
				  stm32_port->tx_dma_buf);

	if (stm32_port->wakeirq > 0) {
		dev_pm_clear_wake_irq(&pdev->dev);
		device_init_wakeup(&pdev->dev, false);
	}

	clk_disable_unprepare(stm32_port->clk);

	return uart_remove_one_port(&stm32_usart_driver, port);
}


#ifdef CONFIG_SERIAL_STM32_CONSOLE
static void stm32_console_putchar(struct uart_port *port, int ch)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;

	while (!(readl_relaxed(port->membase + ofs->isr) & USART_SR_TXE))
		cpu_relax();

	writel_relaxed(ch, port->membase + ofs->tdr);
}

static void stm32_console_write(struct console *co, const char *s, unsigned cnt)
{
	struct uart_port *port = &stm32_ports[co->index].port;
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct stm32_usart_config *cfg = &stm32_port->info->cfg;
	unsigned long flags;
	u32 old_cr1, new_cr1;
	int locked = 1;

	local_irq_save(flags);
	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&port->lock);
	else
		spin_lock(&port->lock);

	/* Save and disable interrupts, enable the transmitter */
	old_cr1 = readl_relaxed(port->membase + ofs->cr1);
	new_cr1 = old_cr1 & ~USART_CR1_IE_MASK;
	new_cr1 |=  USART_CR1_TE | BIT(cfg->uart_enable_bit);
	writel_relaxed(new_cr1, port->membase + ofs->cr1);

	uart_console_write(port, s, cnt, stm32_console_putchar);

	/* Restore interrupt state */
	writel_relaxed(old_cr1, port->membase + ofs->cr1);

	if (locked)
		spin_unlock(&port->lock);
	local_irq_restore(flags);
}

static int stm32_console_setup(struct console *co, char *options)
{
	struct stm32_port *stm32port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= STM32_MAX_PORTS)
		return -ENODEV;

	stm32port = &stm32_ports[co->index];

	/*
	 * This driver does not support early console initialization
	 * (use ARM early printk support instead), so we only expect
	 * this to be called during the uart port registration when the
	 * driver gets probed and the port should be mapped at that point.
	 */
	if (stm32port->port.mapbase == 0 || stm32port->port.membase == NULL)
		return -ENXIO;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&stm32port->port, co, baud, parity, bits, flow);
}

static struct console stm32_console = {
	.name		= STM32_SERIAL_NAME,
	.device		= uart_console_device,
	.write		= stm32_console_write,
	.setup		= stm32_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &stm32_usart_driver,
};

#define STM32_SERIAL_CONSOLE (&stm32_console)

#else
#define STM32_SERIAL_CONSOLE NULL
#endif /* CONFIG_SERIAL_STM32_CONSOLE */

static struct uart_driver stm32_usart_driver = {
	.driver_name	= DRIVER_NAME,
	.dev_name	= STM32_SERIAL_NAME,
	.major		= 0,
	.minor		= 0,
	.nr		= STM32_MAX_PORTS,
	.cons		= STM32_SERIAL_CONSOLE,
};

#ifdef CONFIG_PM_SLEEP
static void stm32_serial_enable_wakeup(struct uart_port *port, bool enable)
{
	struct stm32_port *stm32_port = to_stm32_port(port);
	struct stm32_usart_offsets *ofs = &stm32_port->info->ofs;
	struct stm32_usart_config *cfg = &stm32_port->info->cfg;
	u32 val;

	if (stm32_port->wakeirq <= 0)
		return;

	if (enable) {
		stm32_clr_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));
		stm32_set_bits(port, ofs->cr1, USART_CR1_UESM);
		val = readl_relaxed(port->membase + ofs->cr3);
		val &= ~USART_CR3_WUS_MASK;
		/* Enable Wake up interrupt from low power on start bit */
		val |= USART_CR3_WUS_START_BIT | USART_CR3_WUFIE;
		writel_relaxed(val, port->membase + ofs->cr3);
		stm32_set_bits(port, ofs->cr1, BIT(cfg->uart_enable_bit));
	} else {
		stm32_clr_bits(port, ofs->cr1, USART_CR1_UESM);
	}
}

static int stm32_serial_suspend(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);

	uart_suspend_port(&stm32_usart_driver, port);

	if (device_may_wakeup(dev))
		stm32_serial_enable_wakeup(port, true);
	else
		stm32_serial_enable_wakeup(port, false);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int stm32_serial_resume(struct device *dev)
{
	struct uart_port *port = dev_get_drvdata(dev);

	pinctrl_pm_select_default_state(dev);

	if (device_may_wakeup(dev))
		stm32_serial_enable_wakeup(port, false);

	return uart_resume_port(&stm32_usart_driver, port);
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops stm32_serial_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(stm32_serial_suspend, stm32_serial_resume)
};

static struct platform_driver stm32_serial_driver = {
	.probe		= stm32_serial_probe,
	.remove		= stm32_serial_remove,
	.driver	= {
		.name	= DRIVER_NAME,
		.pm	= &stm32_serial_pm_ops,
		.of_match_table = of_match_ptr(stm32_match),
	},
};

static int __init usart_init(void)
{
	static char banner[] __initdata = "STM32 USART driver initialized";
	int ret;

	pr_info("%s\n", banner);

	ret = uart_register_driver(&stm32_usart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&stm32_serial_driver);
	if (ret)
		uart_unregister_driver(&stm32_usart_driver);

	return ret;
}

static void __exit usart_exit(void)
{
	platform_driver_unregister(&stm32_serial_driver);
	uart_unregister_driver(&stm32_usart_driver);
}

module_init(usart_init);
module_exit(usart_exit);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_DESCRIPTION("STMicroelectronics STM32 serial port driver");
MODULE_LICENSE("GPL v2");
