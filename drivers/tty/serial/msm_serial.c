/*
 * Driver for msm7k serial device and console
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Robert Love <rlove@google.com>
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#if defined(CONFIG_SERIAL_MSM_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
# define SUPPORT_SYSRQ
#endif

#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "msm_serial.h"

enum {
	UARTDM_1P1 = 1,
	UARTDM_1P2,
	UARTDM_1P3,
	UARTDM_1P4,
};

struct msm_port {
	struct uart_port	uart;
	char			name[16];
	struct clk		*clk;
	struct clk		*pclk;
	unsigned int		imr;
	int			is_uartdm;
	unsigned int		old_snap_state;
};

static inline void wait_for_xmitr(struct uart_port *port)
{
	while (!(msm_read(port, UART_SR) & UART_SR_TX_EMPTY)) {
		if (msm_read(port, UART_ISR) & UART_ISR_TX_READY)
			break;
		udelay(1);
	}
	msm_write(port, UART_CR_CMD_RESET_TX_READY, UART_CR);
}

static void msm_stop_tx(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	msm_port->imr &= ~UART_IMR_TXLEV;
	msm_write(port, msm_port->imr, UART_IMR);
}

static void msm_start_tx(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	msm_port->imr |= UART_IMR_TXLEV;
	msm_write(port, msm_port->imr, UART_IMR);
}

static void msm_stop_rx(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	msm_port->imr &= ~(UART_IMR_RXLEV | UART_IMR_RXSTALE);
	msm_write(port, msm_port->imr, UART_IMR);
}

static void msm_enable_ms(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	msm_port->imr |= UART_IMR_DELTA_CTS;
	msm_write(port, msm_port->imr, UART_IMR);
}

static void handle_rx_dm(struct uart_port *port, unsigned int misr)
{
	struct tty_port *tport = &port->state->port;
	unsigned int sr;
	int count = 0;
	struct msm_port *msm_port = UART_TO_MSM(port);

	if ((msm_read(port, UART_SR) & UART_SR_OVERRUN)) {
		port->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
		msm_write(port, UART_CR_CMD_RESET_ERR, UART_CR);
	}

	if (misr & UART_IMR_RXSTALE) {
		count = msm_read(port, UARTDM_RX_TOTAL_SNAP) -
			msm_port->old_snap_state;
		msm_port->old_snap_state = 0;
	} else {
		count = 4 * (msm_read(port, UART_RFWR));
		msm_port->old_snap_state += count;
	}

	/* TODO: Precise error reporting */

	port->icount.rx += count;

	while (count > 0) {
		unsigned char buf[4];

		sr = msm_read(port, UART_SR);
		if ((sr & UART_SR_RX_READY) == 0) {
			msm_port->old_snap_state -= count;
			break;
		}
		ioread32_rep(port->membase + UARTDM_RF, buf, 1);
		if (sr & UART_SR_RX_BREAK) {
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
		} else if (sr & UART_SR_PAR_FRAME_ERR)
			port->icount.frame++;

		/* TODO: handle sysrq */
		tty_insert_flip_string(tport, buf, min(count, 4));
		count -= 4;
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tport);
	spin_lock(&port->lock);

	if (misr & (UART_IMR_RXSTALE))
		msm_write(port, UART_CR_CMD_RESET_STALE_INT, UART_CR);
	msm_write(port, 0xFFFFFF, UARTDM_DMRX);
	msm_write(port, UART_CR_CMD_STALE_EVENT_ENABLE, UART_CR);
}

static void handle_rx(struct uart_port *port)
{
	struct tty_port *tport = &port->state->port;
	unsigned int sr;

	/*
	 * Handle overrun. My understanding of the hardware is that overrun
	 * is not tied to the RX buffer, so we handle the case out of band.
	 */
	if ((msm_read(port, UART_SR) & UART_SR_OVERRUN)) {
		port->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
		msm_write(port, UART_CR_CMD_RESET_ERR, UART_CR);
	}

	/* and now the main RX loop */
	while ((sr = msm_read(port, UART_SR)) & UART_SR_RX_READY) {
		unsigned int c;
		char flag = TTY_NORMAL;

		c = msm_read(port, UART_RF);

		if (sr & UART_SR_RX_BREAK) {
			port->icount.brk++;
			if (uart_handle_break(port))
				continue;
		} else if (sr & UART_SR_PAR_FRAME_ERR) {
			port->icount.frame++;
		} else {
			port->icount.rx++;
		}

		/* Mask conditions we're ignorning. */
		sr &= port->read_status_mask;

		if (sr & UART_SR_RX_BREAK) {
			flag = TTY_BREAK;
		} else if (sr & UART_SR_PAR_FRAME_ERR) {
			flag = TTY_FRAME;
		}

		if (!uart_handle_sysrq_char(port, c))
			tty_insert_flip_char(tport, c, flag);
	}

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tport);
	spin_lock(&port->lock);
}

static void reset_dm_count(struct uart_port *port, int count)
{
	wait_for_xmitr(port);
	msm_write(port, count, UARTDM_NCF_TX);
	msm_read(port, UARTDM_NCF_TX);
}

static void handle_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	struct msm_port *msm_port = UART_TO_MSM(port);
	unsigned int tx_count, num_chars;
	unsigned int tf_pointer = 0;
	void __iomem *tf;

	if (msm_port->is_uartdm)
		tf = port->membase + UARTDM_TF;
	else
		tf = port->membase + UART_TF;

	tx_count = uart_circ_chars_pending(xmit);
	tx_count = min3(tx_count, (unsigned int)UART_XMIT_SIZE - xmit->tail,
			port->fifosize);

	if (port->x_char) {
		if (msm_port->is_uartdm)
			reset_dm_count(port, tx_count + 1);

		iowrite8_rep(tf, &port->x_char, 1);
		port->icount.tx++;
		port->x_char = 0;
	} else if (tx_count && msm_port->is_uartdm) {
		reset_dm_count(port, tx_count);
	}

	while (tf_pointer < tx_count) {
		int i;
		char buf[4] = { 0 };

		if (!(msm_read(port, UART_SR) & UART_SR_TX_READY))
			break;

		if (msm_port->is_uartdm)
			num_chars = min(tx_count - tf_pointer,
					(unsigned int)sizeof(buf));
		else
			num_chars = 1;

		for (i = 0; i < num_chars; i++) {
			buf[i] = xmit->buf[xmit->tail + i];
			port->icount.tx++;
		}

		iowrite32_rep(tf, buf, 1);
		xmit->tail = (xmit->tail + num_chars) & (UART_XMIT_SIZE - 1);
		tf_pointer += num_chars;
	}

	/* disable tx interrupts if nothing more to send */
	if (uart_circ_empty(xmit))
		msm_stop_tx(port);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);
}

static void handle_delta_cts(struct uart_port *port)
{
	msm_write(port, UART_CR_CMD_RESET_CTS, UART_CR);
	port->icount.cts++;
	wake_up_interruptible(&port->state->port.delta_msr_wait);
}

static irqreturn_t msm_irq(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	struct msm_port *msm_port = UART_TO_MSM(port);
	unsigned int misr;

	spin_lock(&port->lock);
	misr = msm_read(port, UART_MISR);
	msm_write(port, 0, UART_IMR); /* disable interrupt */

	if (misr & (UART_IMR_RXLEV | UART_IMR_RXSTALE)) {
		if (msm_port->is_uartdm)
			handle_rx_dm(port, misr);
		else
			handle_rx(port);
	}
	if (misr & UART_IMR_TXLEV)
		handle_tx(port);
	if (misr & UART_IMR_DELTA_CTS)
		handle_delta_cts(port);

	msm_write(port, msm_port->imr, UART_IMR); /* restore interrupt */
	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static unsigned int msm_tx_empty(struct uart_port *port)
{
	return (msm_read(port, UART_SR) & UART_SR_TX_EMPTY) ? TIOCSER_TEMT : 0;
}

static unsigned int msm_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_CTS | TIOCM_DSR | TIOCM_RTS;
}


static void msm_reset(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	/* reset everything */
	msm_write(port, UART_CR_CMD_RESET_RX, UART_CR);
	msm_write(port, UART_CR_CMD_RESET_TX, UART_CR);
	msm_write(port, UART_CR_CMD_RESET_ERR, UART_CR);
	msm_write(port, UART_CR_CMD_RESET_BREAK_INT, UART_CR);
	msm_write(port, UART_CR_CMD_RESET_CTS, UART_CR);
	msm_write(port, UART_CR_CMD_SET_RFR, UART_CR);

	/* Disable DM modes */
	if (msm_port->is_uartdm)
		msm_write(port, 0, UARTDM_DMEN);
}

static void msm_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned int mr;
	mr = msm_read(port, UART_MR1);

	if (!(mctrl & TIOCM_RTS)) {
		mr &= ~UART_MR1_RX_RDY_CTL;
		msm_write(port, mr, UART_MR1);
		msm_write(port, UART_CR_CMD_RESET_RFR, UART_CR);
	} else {
		mr |= UART_MR1_RX_RDY_CTL;
		msm_write(port, mr, UART_MR1);
	}
}

static void msm_break_ctl(struct uart_port *port, int break_ctl)
{
	if (break_ctl)
		msm_write(port, UART_CR_CMD_START_BREAK, UART_CR);
	else
		msm_write(port, UART_CR_CMD_STOP_BREAK, UART_CR);
}

struct msm_baud_map {
	u16	divisor;
	u8	code;
	u8	rxstale;
};

static const struct msm_baud_map *
msm_find_best_baud(struct uart_port *port, unsigned int baud)
{
	unsigned int i, divisor;
	const struct msm_baud_map *entry;
	static const struct msm_baud_map table[] = {
		{ 1536, 0x00,  1 },
		{  768, 0x11,  1 },
		{  384, 0x22,  1 },
		{  192, 0x33,  1 },
		{   96, 0x44,  1 },
		{   48, 0x55,  1 },
		{   32, 0x66,  1 },
		{   24, 0x77,  1 },
		{   16, 0x88,  1 },
		{   12, 0x99,  6 },
		{    8, 0xaa,  6 },
		{    6, 0xbb,  6 },
		{    4, 0xcc,  6 },
		{    3, 0xdd,  8 },
		{    2, 0xee, 16 },
		{    1, 0xff, 31 },
	};

	divisor = uart_get_divisor(port, baud);

	for (i = 0, entry = table; i < ARRAY_SIZE(table); i++, entry++)
		if (entry->divisor <= divisor)
			break;

	return entry; /* Default to smallest divider */
}

static int msm_set_baud_rate(struct uart_port *port, unsigned int baud)
{
	unsigned int rxstale, watermark;
	struct msm_port *msm_port = UART_TO_MSM(port);
	const struct msm_baud_map *entry;

	entry = msm_find_best_baud(port, baud);

	if (msm_port->is_uartdm)
		msm_write(port, UART_CR_CMD_RESET_RX, UART_CR);

	msm_write(port, entry->code, UART_CSR);

	/* RX stale watermark */
	rxstale = entry->rxstale;
	watermark = UART_IPR_STALE_LSB & rxstale;
	watermark |= UART_IPR_RXSTALE_LAST;
	watermark |= UART_IPR_STALE_TIMEOUT_MSB & (rxstale << 2);
	msm_write(port, watermark, UART_IPR);

	/* set RX watermark */
	watermark = (port->fifosize * 3) / 4;
	msm_write(port, watermark, UART_RFWR);

	/* set TX watermark */
	msm_write(port, 10, UART_TFWR);

	if (msm_port->is_uartdm) {
		msm_write(port, UART_CR_CMD_RESET_STALE_INT, UART_CR);
		msm_write(port, 0xFFFFFF, UARTDM_DMRX);
		msm_write(port, UART_CR_CMD_STALE_EVENT_ENABLE, UART_CR);
	}

	return baud;
}


static void msm_init_clock(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	clk_prepare_enable(msm_port->clk);
	clk_prepare_enable(msm_port->pclk);
	msm_serial_set_mnd_regs(port);
}

static int msm_startup(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);
	unsigned int data, rfr_level;
	int ret;

	snprintf(msm_port->name, sizeof(msm_port->name),
		 "msm_serial%d", port->line);

	ret = request_irq(port->irq, msm_irq, IRQF_TRIGGER_HIGH,
			  msm_port->name, port);
	if (unlikely(ret))
		return ret;

	msm_init_clock(port);

	if (likely(port->fifosize > 12))
		rfr_level = port->fifosize - 12;
	else
		rfr_level = port->fifosize;

	/* set automatic RFR level */
	data = msm_read(port, UART_MR1);
	data &= ~UART_MR1_AUTO_RFR_LEVEL1;
	data &= ~UART_MR1_AUTO_RFR_LEVEL0;
	data |= UART_MR1_AUTO_RFR_LEVEL1 & (rfr_level << 2);
	data |= UART_MR1_AUTO_RFR_LEVEL0 & rfr_level;
	msm_write(port, data, UART_MR1);

	/* make sure that RXSTALE count is non-zero */
	data = msm_read(port, UART_IPR);
	if (unlikely(!data)) {
		data |= UART_IPR_RXSTALE_LAST;
		data |= UART_IPR_STALE_LSB;
		msm_write(port, data, UART_IPR);
	}

	data = 0;
	if (!port->cons || (port->cons && !(port->cons->flags & CON_ENABLED))) {
		msm_write(port, UART_CR_CMD_PROTECTION_EN, UART_CR);
		msm_reset(port);
		data = UART_CR_TX_ENABLE;
	}

	data |= UART_CR_RX_ENABLE;
	msm_write(port, data, UART_CR);	/* enable TX & RX */

	/* Make sure IPR is not 0 to start with*/
	if (msm_port->is_uartdm)
		msm_write(port, UART_IPR_STALE_LSB, UART_IPR);

	/* turn on RX and CTS interrupts */
	msm_port->imr = UART_IMR_RXLEV | UART_IMR_RXSTALE |
			UART_IMR_CURRENT_CTS;

	if (msm_port->is_uartdm) {
		msm_write(port, 0xFFFFFF, UARTDM_DMRX);
		msm_write(port, UART_CR_CMD_RESET_STALE_INT, UART_CR);
		msm_write(port, UART_CR_CMD_STALE_EVENT_ENABLE, UART_CR);
	}

	msm_write(port, msm_port->imr, UART_IMR);
	return 0;
}

static void msm_shutdown(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	msm_port->imr = 0;
	msm_write(port, 0, UART_IMR); /* disable interrupts */

	clk_disable_unprepare(msm_port->clk);

	free_irq(port->irq, port);
}

static void msm_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	unsigned long flags;
	unsigned int baud, mr;

	spin_lock_irqsave(&port->lock, flags);

	/* calculate and set baud rate */
	baud = uart_get_baud_rate(port, termios, old, 300, 115200);
	baud = msm_set_baud_rate(port, baud);
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, baud, baud);

	/* calculate parity */
	mr = msm_read(port, UART_MR2);
	mr &= ~UART_MR2_PARITY_MODE;
	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & PARODD)
			mr |= UART_MR2_PARITY_MODE_ODD;
		else if (termios->c_cflag & CMSPAR)
			mr |= UART_MR2_PARITY_MODE_SPACE;
		else
			mr |= UART_MR2_PARITY_MODE_EVEN;
	}

	/* calculate bits per char */
	mr &= ~UART_MR2_BITS_PER_CHAR;
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		mr |= UART_MR2_BITS_PER_CHAR_5;
		break;
	case CS6:
		mr |= UART_MR2_BITS_PER_CHAR_6;
		break;
	case CS7:
		mr |= UART_MR2_BITS_PER_CHAR_7;
		break;
	case CS8:
	default:
		mr |= UART_MR2_BITS_PER_CHAR_8;
		break;
	}

	/* calculate stop bits */
	mr &= ~(UART_MR2_STOP_BIT_LEN_ONE | UART_MR2_STOP_BIT_LEN_TWO);
	if (termios->c_cflag & CSTOPB)
		mr |= UART_MR2_STOP_BIT_LEN_TWO;
	else
		mr |= UART_MR2_STOP_BIT_LEN_ONE;

	/* set parity, bits per char, and stop bit */
	msm_write(port, mr, UART_MR2);

	/* calculate and set hardware flow control */
	mr = msm_read(port, UART_MR1);
	mr &= ~(UART_MR1_CTS_CTL | UART_MR1_RX_RDY_CTL);
	if (termios->c_cflag & CRTSCTS) {
		mr |= UART_MR1_CTS_CTL;
		mr |= UART_MR1_RX_RDY_CTL;
	}
	msm_write(port, mr, UART_MR1);

	/* Configure status bits to ignore based on termio flags. */
	port->read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= UART_SR_PAR_FRAME_ERR;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		port->read_status_mask |= UART_SR_RX_BREAK;

	uart_update_timeout(port, termios->c_cflag, baud);

	spin_unlock_irqrestore(&port->lock, flags);
}

static const char *msm_type(struct uart_port *port)
{
	return "MSM";
}

static void msm_release_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *uart_resource;
	resource_size_t size;

	uart_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!uart_resource))
		return;
	size = resource_size(uart_resource);

	release_mem_region(port->mapbase, size);
	iounmap(port->membase);
	port->membase = NULL;
}

static int msm_request_port(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *uart_resource;
	resource_size_t size;
	int ret;

	uart_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!uart_resource))
		return -ENXIO;

	size = resource_size(uart_resource);

	if (!request_mem_region(port->mapbase, size, "msm_serial"))
		return -EBUSY;

	port->membase = ioremap(port->mapbase, size);
	if (!port->membase) {
		ret = -EBUSY;
		goto fail_release_port;
	}

	return 0;

fail_release_port:
	release_mem_region(port->mapbase, size);
	return ret;
}

static void msm_config_port(struct uart_port *port, int flags)
{
	int ret;
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_MSM;
		ret = msm_request_port(port);
		if (ret)
			return;
	}
}

static int msm_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (unlikely(ser->type != PORT_UNKNOWN && ser->type != PORT_MSM))
		return -EINVAL;
	if (unlikely(port->irq != ser->irq))
		return -EINVAL;
	return 0;
}

static void msm_power(struct uart_port *port, unsigned int state,
		      unsigned int oldstate)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	switch (state) {
	case 0:
		clk_prepare_enable(msm_port->clk);
		clk_prepare_enable(msm_port->pclk);
		break;
	case 3:
		clk_disable_unprepare(msm_port->clk);
		clk_disable_unprepare(msm_port->pclk);
		break;
	default:
		printk(KERN_ERR "msm_serial: Unknown PM state %d\n", state);
	}
}

#ifdef CONFIG_CONSOLE_POLL
static int msm_poll_init(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);

	/* Enable single character mode on RX FIFO */
	if (msm_port->is_uartdm >= UARTDM_1P4)
		msm_write(port, UARTDM_DMEN_RX_SC_ENABLE, UARTDM_DMEN);

	return 0;
}

static int msm_poll_get_char_single(struct uart_port *port)
{
	struct msm_port *msm_port = UART_TO_MSM(port);
	unsigned int rf_reg = msm_port->is_uartdm ? UARTDM_RF : UART_RF;

	if (!(msm_read(port, UART_SR) & UART_SR_RX_READY))
		return NO_POLL_CHAR;
	else
		return msm_read(port, rf_reg) & 0xff;
}

static int msm_poll_get_char_dm_1p3(struct uart_port *port)
{
	int c;
	static u32 slop;
	static int count;
	unsigned char *sp = (unsigned char *)&slop;

	/* Check if a previous read had more than one char */
	if (count) {
		c = sp[sizeof(slop) - count];
		count--;
	/* Or if FIFO is empty */
	} else if (!(msm_read(port, UART_SR) & UART_SR_RX_READY)) {
		/*
		 * If RX packing buffer has less than a word, force stale to
		 * push contents into RX FIFO
		 */
		count = msm_read(port, UARTDM_RXFS);
		count = (count >> UARTDM_RXFS_BUF_SHIFT) & UARTDM_RXFS_BUF_MASK;
		if (count) {
			msm_write(port, UART_CR_CMD_FORCE_STALE, UART_CR);
			slop = msm_read(port, UARTDM_RF);
			c = sp[0];
			count--;
		} else {
			c = NO_POLL_CHAR;
		}
	/* FIFO has a word */
	} else {
		slop = msm_read(port, UARTDM_RF);
		c = sp[0];
		count = sizeof(slop) - 1;
	}

	return c;
}

static int msm_poll_get_char(struct uart_port *port)
{
	u32 imr;
	int c;
	struct msm_port *msm_port = UART_TO_MSM(port);

	/* Disable all interrupts */
	imr = msm_read(port, UART_IMR);
	msm_write(port, 0, UART_IMR);

	if (msm_port->is_uartdm == UARTDM_1P3)
		c = msm_poll_get_char_dm_1p3(port);
	else
		c = msm_poll_get_char_single(port);

	/* Enable interrupts */
	msm_write(port, imr, UART_IMR);

	return c;
}

static void msm_poll_put_char(struct uart_port *port, unsigned char c)
{
	u32 imr;
	struct msm_port *msm_port = UART_TO_MSM(port);

	/* Disable all interrupts */
	imr = msm_read(port, UART_IMR);
	msm_write(port, 0, UART_IMR);

	if (msm_port->is_uartdm)
		reset_dm_count(port, 1);

	/* Wait until FIFO is empty */
	while (!(msm_read(port, UART_SR) & UART_SR_TX_READY))
		cpu_relax();

	/* Write a character */
	msm_write(port, c, msm_port->is_uartdm ? UARTDM_TF : UART_TF);

	/* Wait until FIFO is empty */
	while (!(msm_read(port, UART_SR) & UART_SR_TX_READY))
		cpu_relax();

	/* Enable interrupts */
	msm_write(port, imr, UART_IMR);

	return;
}
#endif

static struct uart_ops msm_uart_pops = {
	.tx_empty = msm_tx_empty,
	.set_mctrl = msm_set_mctrl,
	.get_mctrl = msm_get_mctrl,
	.stop_tx = msm_stop_tx,
	.start_tx = msm_start_tx,
	.stop_rx = msm_stop_rx,
	.enable_ms = msm_enable_ms,
	.break_ctl = msm_break_ctl,
	.startup = msm_startup,
	.shutdown = msm_shutdown,
	.set_termios = msm_set_termios,
	.type = msm_type,
	.release_port = msm_release_port,
	.request_port = msm_request_port,
	.config_port = msm_config_port,
	.verify_port = msm_verify_port,
	.pm = msm_power,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init = msm_poll_init,
	.poll_get_char	= msm_poll_get_char,
	.poll_put_char	= msm_poll_put_char,
#endif
};

static struct msm_port msm_uart_ports[] = {
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &msm_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 64,
			.line = 0,
		},
	},
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &msm_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 64,
			.line = 1,
		},
	},
	{
		.uart = {
			.iotype = UPIO_MEM,
			.ops = &msm_uart_pops,
			.flags = UPF_BOOT_AUTOCONF,
			.fifosize = 64,
			.line = 2,
		},
	},
};

#define UART_NR	ARRAY_SIZE(msm_uart_ports)

static inline struct uart_port *get_port_from_line(unsigned int line)
{
	return &msm_uart_ports[line].uart;
}

#ifdef CONFIG_SERIAL_MSM_CONSOLE
static void msm_console_write(struct console *co, const char *s,
			      unsigned int count)
{
	int i;
	struct uart_port *port;
	struct msm_port *msm_port;
	int num_newlines = 0;
	bool replaced = false;
	void __iomem *tf;

	BUG_ON(co->index < 0 || co->index >= UART_NR);

	port = get_port_from_line(co->index);
	msm_port = UART_TO_MSM(port);

	if (msm_port->is_uartdm)
		tf = port->membase + UARTDM_TF;
	else
		tf = port->membase + UART_TF;

	/* Account for newlines that will get a carriage return added */
	for (i = 0; i < count; i++)
		if (s[i] == '\n')
			num_newlines++;
	count += num_newlines;

	spin_lock(&port->lock);
	if (msm_port->is_uartdm)
		reset_dm_count(port, count);

	i = 0;
	while (i < count) {
		int j;
		unsigned int num_chars;
		char buf[4] = { 0 };

		if (msm_port->is_uartdm)
			num_chars = min(count - i, (unsigned int)sizeof(buf));
		else
			num_chars = 1;

		for (j = 0; j < num_chars; j++) {
			char c = *s;

			if (c == '\n' && !replaced) {
				buf[j] = '\r';
				j++;
				replaced = true;
			}
			if (j < num_chars) {
				buf[j] = c;
				s++;
				replaced = false;
			}
		}

		while (!(msm_read(port, UART_SR) & UART_SR_TX_READY))
			cpu_relax();

		iowrite32_rep(tf, buf, 1);
		i += num_chars;
	}
	spin_unlock(&port->lock);
}

static int __init msm_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	struct msm_port *msm_port;
	int baud = 0, flow, bits, parity;

	if (unlikely(co->index >= UART_NR || co->index < 0))
		return -ENXIO;

	port = get_port_from_line(co->index);
	msm_port = UART_TO_MSM(port);

	if (unlikely(!port->membase))
		return -ENXIO;

	msm_init_clock(port);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	bits = 8;
	parity = 'n';
	flow = 'n';
	msm_write(port, UART_MR2_BITS_PER_CHAR_8 | UART_MR2_STOP_BIT_LEN_ONE,
		  UART_MR2);	/* 8N1 */

	if (baud < 300 || baud > 115200)
		baud = 115200;
	msm_set_baud_rate(port, baud);

	msm_reset(port);

	if (msm_port->is_uartdm) {
		msm_write(port, UART_CR_CMD_PROTECTION_EN, UART_CR);
		msm_write(port, UART_CR_TX_ENABLE, UART_CR);
	}

	printk(KERN_INFO "msm_serial: console setup on port #%d\n", port->line);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct uart_driver msm_uart_driver;

static struct console msm_console = {
	.name = "ttyMSM",
	.write = msm_console_write,
	.device = uart_console_device,
	.setup = msm_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &msm_uart_driver,
};

#define MSM_CONSOLE	(&msm_console)

#else
#define MSM_CONSOLE	NULL
#endif

static struct uart_driver msm_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "msm_serial",
	.dev_name = "ttyMSM",
	.nr = UART_NR,
	.cons = MSM_CONSOLE,
};

static atomic_t msm_uart_next_id = ATOMIC_INIT(0);

static const struct of_device_id msm_uartdm_table[] = {
	{ .compatible = "qcom,msm-uartdm-v1.1", .data = (void *)UARTDM_1P1 },
	{ .compatible = "qcom,msm-uartdm-v1.2", .data = (void *)UARTDM_1P2 },
	{ .compatible = "qcom,msm-uartdm-v1.3", .data = (void *)UARTDM_1P3 },
	{ .compatible = "qcom,msm-uartdm-v1.4", .data = (void *)UARTDM_1P4 },
	{ }
};

static int msm_serial_probe(struct platform_device *pdev)
{
	struct msm_port *msm_port;
	struct resource *resource;
	struct uart_port *port;
	const struct of_device_id *id;
	int irq;

	if (pdev->id == -1)
		pdev->id = atomic_inc_return(&msm_uart_next_id) - 1;

	if (unlikely(pdev->id < 0 || pdev->id >= UART_NR))
		return -ENXIO;

	printk(KERN_INFO "msm_serial: detected port #%d\n", pdev->id);

	port = get_port_from_line(pdev->id);
	port->dev = &pdev->dev;
	msm_port = UART_TO_MSM(port);

	id = of_match_device(msm_uartdm_table, &pdev->dev);
	if (id)
		msm_port->is_uartdm = (unsigned long)id->data;
	else
		msm_port->is_uartdm = 0;

	msm_port->clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(msm_port->clk))
		return PTR_ERR(msm_port->clk);

	if (msm_port->is_uartdm) {
		msm_port->pclk = devm_clk_get(&pdev->dev, "iface");
		if (IS_ERR(msm_port->pclk))
			return PTR_ERR(msm_port->pclk);

		clk_set_rate(msm_port->clk, 1843200);
	}

	port->uartclk = clk_get_rate(msm_port->clk);
	printk(KERN_INFO "uartclk = %d\n", port->uartclk);


	resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!resource))
		return -ENXIO;
	port->mapbase = resource->start;

	irq = platform_get_irq(pdev, 0);
	if (unlikely(irq < 0))
		return -ENXIO;
	port->irq = irq;

	platform_set_drvdata(pdev, port);

	return uart_add_one_port(&msm_uart_driver, port);
}

static int msm_serial_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	uart_remove_one_port(&msm_uart_driver, port);

	return 0;
}

static const struct of_device_id msm_match_table[] = {
	{ .compatible = "qcom,msm-uart" },
	{ .compatible = "qcom,msm-uartdm" },
	{}
};

static struct platform_driver msm_platform_driver = {
	.remove = msm_serial_remove,
	.probe = msm_serial_probe,
	.driver = {
		.name = "msm_serial",
		.owner = THIS_MODULE,
		.of_match_table = msm_match_table,
	},
};

static int __init msm_serial_init(void)
{
	int ret;

	ret = uart_register_driver(&msm_uart_driver);
	if (unlikely(ret))
		return ret;

	ret = platform_driver_register(&msm_platform_driver);
	if (unlikely(ret))
		uart_unregister_driver(&msm_uart_driver);

	printk(KERN_INFO "msm_serial: driver initialized\n");

	return ret;
}

static void __exit msm_serial_exit(void)
{
#ifdef CONFIG_SERIAL_MSM_CONSOLE
	unregister_console(&msm_console);
#endif
	platform_driver_unregister(&msm_platform_driver);
	uart_unregister_driver(&msm_uart_driver);
}

module_init(msm_serial_init);
module_exit(msm_serial_exit);

MODULE_AUTHOR("Robert Love <rlove@google.com>");
MODULE_DESCRIPTION("Driver for msm7x serial device");
MODULE_LICENSE("GPL");
