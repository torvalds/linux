// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2018, The Linux foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* Disable MMIO tracing to prevent excessive logging of unwanted MMIO traces */
#define __DISABLE_TRACE_MMIO__

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/soc/qcom/geni-se.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <dt-bindings/interconnect/qcom,icc.h>

/* UART specific GENI registers */
#define SE_UART_LOOPBACK_CFG		0x22c
#define SE_UART_IO_MACRO_CTRL		0x240
#define SE_UART_TX_TRANS_CFG		0x25c
#define SE_UART_TX_WORD_LEN		0x268
#define SE_UART_TX_STOP_BIT_LEN		0x26c
#define SE_UART_TX_TRANS_LEN		0x270
#define SE_UART_RX_TRANS_CFG		0x280
#define SE_UART_RX_WORD_LEN		0x28c
#define SE_UART_RX_STALE_CNT		0x294
#define SE_UART_TX_PARITY_CFG		0x2a4
#define SE_UART_RX_PARITY_CFG		0x2a8
#define SE_UART_MANUAL_RFR		0x2ac

/* SE_UART_TRANS_CFG */
#define UART_TX_PAR_EN			BIT(0)
#define UART_CTS_MASK			BIT(1)

/* SE_UART_TX_STOP_BIT_LEN */
#define TX_STOP_BIT_LEN_1		0
#define TX_STOP_BIT_LEN_2		2

/* SE_UART_RX_TRANS_CFG */
#define UART_RX_PAR_EN			BIT(3)

/* SE_UART_RX_WORD_LEN */
#define RX_WORD_LEN_MASK		GENMASK(9, 0)

/* SE_UART_RX_STALE_CNT */
#define RX_STALE_CNT			GENMASK(23, 0)

/* SE_UART_TX_PARITY_CFG/RX_PARITY_CFG */
#define PAR_CALC_EN			BIT(0)
#define PAR_EVEN			0x00
#define PAR_ODD				0x01
#define PAR_SPACE			0x10

/* SE_UART_MANUAL_RFR register fields */
#define UART_MANUAL_RFR_EN		BIT(31)
#define UART_RFR_NOT_READY		BIT(1)
#define UART_RFR_READY			BIT(0)

/* UART M_CMD OP codes */
#define UART_START_TX			0x1
/* UART S_CMD OP codes */
#define UART_START_READ			0x1
#define UART_PARAM			0x1
#define UART_PARAM_RFR_OPEN		BIT(7)

#define UART_OVERSAMPLING		32
#define STALE_TIMEOUT			16
#define DEFAULT_BITS_PER_CHAR		10
#define GENI_UART_CONS_PORTS		1
#define DEF_FIFO_DEPTH_WORDS		16
#define DEF_TX_WM			2
#define DEF_FIFO_WIDTH_BITS		32
#define UART_RX_WM			2

/* SE_UART_LOOPBACK_CFG */
#define RX_TX_SORTED			BIT(0)
#define CTS_RTS_SORTED			BIT(1)
#define RX_TX_CTS_RTS_SORTED		(RX_TX_SORTED | CTS_RTS_SORTED)

/* UART pin swap value */
#define DEFAULT_IO_MACRO_IO0_IO1_MASK	GENMASK(3, 0)
#define IO_MACRO_IO0_SEL		0x3
#define DEFAULT_IO_MACRO_IO2_IO3_MASK	GENMASK(15, 4)
#define IO_MACRO_IO2_IO3_SWAP		0x4640

/* We always configure 4 bytes per FIFO word */
#define BYTES_PER_FIFO_WORD		4U

#define DMA_RX_BUF_SIZE		2048

static DEFINE_IDA(port_ida);

struct qcom_geni_device_data {
	bool console;
	enum geni_se_xfer_mode mode;
};

struct qcom_geni_private_data {
	/* NOTE: earlycon port will have NULL here */
	struct uart_driver *drv;

	u32 poll_cached_bytes;
	unsigned int poll_cached_bytes_cnt;

	u32 write_cached_bytes;
	unsigned int write_cached_bytes_cnt;
};

struct qcom_geni_serial_port {
	struct uart_port uport;
	struct geni_se se;
	const char *name;
	u32 tx_fifo_depth;
	u32 tx_fifo_width;
	u32 rx_fifo_depth;
	dma_addr_t tx_dma_addr;
	dma_addr_t rx_dma_addr;
	bool setup;
	unsigned long poll_timeout_us;
	unsigned long clk_rate;
	void *rx_buf;
	u32 loopback;
	bool brk;

	unsigned int tx_remaining;
	unsigned int tx_queued;
	int wakeup_irq;
	bool rx_tx_swap;
	bool cts_rts_swap;

	struct qcom_geni_private_data private_data;
	const struct qcom_geni_device_data *dev_data;
};

static const struct uart_ops qcom_geni_console_pops;
static const struct uart_ops qcom_geni_uart_pops;
static struct uart_driver qcom_geni_console_driver;
static struct uart_driver qcom_geni_uart_driver;

static void __qcom_geni_serial_cancel_tx_cmd(struct uart_port *uport);
static void qcom_geni_serial_cancel_tx_cmd(struct uart_port *uport);
static int qcom_geni_serial_port_setup(struct uart_port *uport);

static inline struct qcom_geni_serial_port *to_dev_port(struct uart_port *uport)
{
	return container_of(uport, struct qcom_geni_serial_port, uport);
}

static struct qcom_geni_serial_port qcom_geni_console_port = {
	.uport = {
		.iotype = UPIO_MEM,
		.ops = &qcom_geni_console_pops,
		.flags = UPF_BOOT_AUTOCONF,
		.line = 0,
	},
};

static const struct serial_rs485 qcom_geni_rs485_supported = {
	.flags = SER_RS485_ENABLED | SER_RS485_RTS_AFTER_SEND | SER_RS485_RTS_ON_SEND,
};

/**
 * qcom_geni_set_rs485_mode - Set RTS pin state for RS485 mode
 * @uport: UART port
 * @flag: RS485 flag to determine RTS polarity
 *
 * Enables manual RTS control for RS485. Sets RTS to READY or NOT_READY
 * based on the specified flag if RS485 mode is enabled.
 */
static void qcom_geni_set_rs485_mode(struct uart_port *uport, u32 flag)
{
	if (!(uport->rs485.flags & SER_RS485_ENABLED))
		return;

	u32 rfr = UART_MANUAL_RFR_EN;

	if (uport->rs485.flags & flag)
		rfr |= UART_RFR_NOT_READY;
	else
		rfr |= UART_RFR_READY;

	writel(rfr, uport->membase + SE_UART_MANUAL_RFR);
}

static int qcom_geni_serial_request_port(struct uart_port *uport)
{
	struct platform_device *pdev = to_platform_device(uport->dev);
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	uport->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(uport->membase))
		return PTR_ERR(uport->membase);
	port->se.base = uport->membase;
	return 0;
}

static void qcom_geni_serial_config_port(struct uart_port *uport, int cfg_flags)
{
	if (cfg_flags & UART_CONFIG_TYPE) {
		uport->type = PORT_MSM;
		qcom_geni_serial_request_port(uport);
	}
}

static unsigned int qcom_geni_serial_get_mctrl(struct uart_port *uport)
{
	unsigned int mctrl = TIOCM_DSR | TIOCM_CAR;
	u32 geni_ios;

	if (uart_console(uport)) {
		mctrl |= TIOCM_CTS;
	} else {
		geni_ios = readl(uport->membase + SE_GENI_IOS);
		if (!(geni_ios & IO2_DATA_IN))
			mctrl |= TIOCM_CTS;
	}

	return mctrl;
}

static void qcom_geni_serial_set_mctrl(struct uart_port *uport,
							unsigned int mctrl)
{
	u32 uart_manual_rfr = 0;
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	if (uart_console(uport))
		return;

	if (mctrl & TIOCM_LOOP)
		port->loopback = RX_TX_CTS_RTS_SORTED;

	if (!(mctrl & TIOCM_RTS) && !uport->suspended)
		uart_manual_rfr = UART_MANUAL_RFR_EN | UART_RFR_NOT_READY;
	writel(uart_manual_rfr, uport->membase + SE_UART_MANUAL_RFR);
}

static const char *qcom_geni_serial_get_type(struct uart_port *uport)
{
	return "MSM";
}

static struct qcom_geni_serial_port *get_port_from_line(int line, bool console, struct device *dev)
{
	struct qcom_geni_serial_port *port;
	int nr_ports = console ? GENI_UART_CONS_PORTS : CONFIG_SERIAL_QCOM_GENI_UART_PORTS;

	if (console) {
		if (line < 0 || line >= nr_ports)
			return ERR_PTR(-ENXIO);

		port = &qcom_geni_console_port;
	} else {
		int max_alias_num = of_alias_get_highest_id("serial");

		if (line < 0 || line >= nr_ports)
			line = ida_alloc_range(&port_ida, max_alias_num + 1,
					       nr_ports - 1, GFP_KERNEL);
		else
			line = ida_alloc_range(&port_ida, line,
					       nr_ports - 1, GFP_KERNEL);

		if (line < 0)
			return ERR_PTR(-ENXIO);

		port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
		if (!port)
			return ERR_PTR(-ENOMEM);

		port->uport.iotype = UPIO_MEM;
		port->uport.ops = &qcom_geni_uart_pops;
		port->uport.flags = UPF_BOOT_AUTOCONF;
		port->uport.line = line;
	}
	return port;
}

static bool qcom_geni_serial_main_active(struct uart_port *uport)
{
	return readl(uport->membase + SE_GENI_STATUS) & M_GENI_CMD_ACTIVE;
}

static bool qcom_geni_serial_secondary_active(struct uart_port *uport)
{
	return readl(uport->membase + SE_GENI_STATUS) & S_GENI_CMD_ACTIVE;
}

static bool qcom_geni_serial_poll_bitfield(struct uart_port *uport,
					   unsigned int offset, u32 field, u32 val)
{
	u32 reg;
	struct qcom_geni_serial_port *port;
	unsigned long timeout_us = 20000;
	struct qcom_geni_private_data *private_data = uport->private_data;

	if (private_data->drv) {
		port = to_dev_port(uport);
		if (port->poll_timeout_us)
			timeout_us = port->poll_timeout_us;
	}

	/*
	 * Use custom implementation instead of readl_poll_atomic since ktimer
	 * is not ready at the time of early console.
	 */
	timeout_us = DIV_ROUND_UP(timeout_us, 10) * 10;
	while (timeout_us) {
		reg = readl(uport->membase + offset);
		if ((reg & field) == val)
			return true;
		udelay(10);
		timeout_us -= 10;
	}
	return false;
}

static bool qcom_geni_serial_poll_bit(struct uart_port *uport,
				      unsigned int offset, u32 field, bool set)
{
	return qcom_geni_serial_poll_bitfield(uport, offset, field, set ? field : 0);
}

static void qcom_geni_serial_setup_tx(struct uart_port *uport, u32 xmit_size)
{
	u32 m_cmd;

	writel(xmit_size, uport->membase + SE_UART_TX_TRANS_LEN);
	m_cmd = UART_START_TX << M_OPCODE_SHFT;
	writel(m_cmd, uport->membase + SE_GENI_M_CMD0);
}

static void qcom_geni_serial_poll_tx_done(struct uart_port *uport)
{
	int done;

	done = qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_DONE_EN, true);
	if (!done) {
		writel(M_GENI_CMD_ABORT, uport->membase +
						SE_GENI_M_CMD_CTRL_REG);
		qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
							M_CMD_ABORT_EN, true);
		writel(M_CMD_ABORT_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
	}
}

static void qcom_geni_serial_abort_rx(struct uart_port *uport)
{
	u32 irq_clear = S_CMD_DONE_EN | S_CMD_ABORT_EN;

	writel(S_GENI_CMD_ABORT, uport->membase + SE_GENI_S_CMD_CTRL_REG);
	qcom_geni_serial_poll_bit(uport, SE_GENI_S_CMD_CTRL_REG,
					S_GENI_CMD_ABORT, false);
	writel(irq_clear, uport->membase + SE_GENI_S_IRQ_CLEAR);
	writel(FORCE_DEFAULT, uport->membase + GENI_FORCE_DEFAULT_REG);
}

#ifdef CONFIG_CONSOLE_POLL
static int qcom_geni_serial_get_char(struct uart_port *uport)
{
	struct qcom_geni_private_data *private_data = uport->private_data;
	u32 status;
	u32 word_cnt;
	int ret;

	if (!private_data->poll_cached_bytes_cnt) {
		status = readl(uport->membase + SE_GENI_M_IRQ_STATUS);
		writel(status, uport->membase + SE_GENI_M_IRQ_CLEAR);

		status = readl(uport->membase + SE_GENI_S_IRQ_STATUS);
		writel(status, uport->membase + SE_GENI_S_IRQ_CLEAR);

		status = readl(uport->membase + SE_GENI_RX_FIFO_STATUS);
		word_cnt = status & RX_FIFO_WC_MSK;
		if (!word_cnt)
			return NO_POLL_CHAR;

		if (word_cnt == 1 && (status & RX_LAST))
			/*
			 * NOTE: If RX_LAST_BYTE_VALID is 0 it needs to be
			 * treated as if it was BYTES_PER_FIFO_WORD.
			 */
			private_data->poll_cached_bytes_cnt =
				(status & RX_LAST_BYTE_VALID_MSK) >>
				RX_LAST_BYTE_VALID_SHFT;

		if (private_data->poll_cached_bytes_cnt == 0)
			private_data->poll_cached_bytes_cnt = BYTES_PER_FIFO_WORD;

		private_data->poll_cached_bytes =
			readl(uport->membase + SE_GENI_RX_FIFOn);
	}

	private_data->poll_cached_bytes_cnt--;
	ret = private_data->poll_cached_bytes & 0xff;
	private_data->poll_cached_bytes >>= 8;

	return ret;
}

static void qcom_geni_serial_poll_put_char(struct uart_port *uport,
							unsigned char c)
{
	if (qcom_geni_serial_main_active(uport)) {
		qcom_geni_serial_poll_tx_done(uport);
		__qcom_geni_serial_cancel_tx_cmd(uport);
	}

	writel(M_CMD_DONE_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
	qcom_geni_serial_setup_tx(uport, 1);
	writel(c, uport->membase + SE_GENI_TX_FIFOn);
	qcom_geni_serial_poll_tx_done(uport);
}

static int qcom_geni_serial_poll_init(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	int ret;

	if (!port->setup) {
		ret = qcom_geni_serial_port_setup(uport);
		if (ret)
			return ret;
	}

	if (!qcom_geni_serial_secondary_active(uport))
		geni_se_setup_s_cmd(&port->se, UART_START_READ, 0);

	return 0;
}
#endif

#ifdef CONFIG_SERIAL_QCOM_GENI_CONSOLE
static void qcom_geni_serial_drain_fifo(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	qcom_geni_serial_poll_bitfield(uport, SE_GENI_M_GP_LENGTH, GP_LENGTH,
			port->tx_queued);
}

static void qcom_geni_serial_wr_char(struct uart_port *uport, unsigned char ch)
{
	struct qcom_geni_private_data *private_data = uport->private_data;

	private_data->write_cached_bytes =
		(private_data->write_cached_bytes >> 8) | (ch << 24);
	private_data->write_cached_bytes_cnt++;

	if (private_data->write_cached_bytes_cnt == BYTES_PER_FIFO_WORD) {
		writel(private_data->write_cached_bytes,
		       uport->membase + SE_GENI_TX_FIFOn);
		private_data->write_cached_bytes_cnt = 0;
	}
}

static void
__qcom_geni_serial_console_write(struct uart_port *uport, const char *s,
				 unsigned int count)
{
	struct qcom_geni_private_data *private_data = uport->private_data;

	int i;
	u32 bytes_to_send = count;

	for (i = 0; i < count; i++) {
		/*
		 * uart_console_write() adds a carriage return for each newline.
		 * Account for additional bytes to be written.
		 */
		if (s[i] == '\n')
			bytes_to_send++;
	}

	writel(DEF_TX_WM, uport->membase + SE_GENI_TX_WATERMARK_REG);
	writel(M_CMD_DONE_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
	qcom_geni_serial_setup_tx(uport, bytes_to_send);
	for (i = 0; i < count; ) {
		size_t chars_to_write = 0;
		size_t avail = DEF_FIFO_DEPTH_WORDS - DEF_TX_WM;

		/*
		 * If the WM bit never set, then the Tx state machine is not
		 * in a valid state, so break, cancel/abort any existing
		 * command. Unfortunately the current data being written is
		 * lost.
		 */
		if (!qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_TX_FIFO_WATERMARK_EN, true))
			break;
		chars_to_write = min_t(size_t, count - i, avail / 2);
		uart_console_write(uport, s + i, chars_to_write,
						qcom_geni_serial_wr_char);
		writel(M_TX_FIFO_WATERMARK_EN, uport->membase +
							SE_GENI_M_IRQ_CLEAR);
		i += chars_to_write;
	}

	if (private_data->write_cached_bytes_cnt) {
		private_data->write_cached_bytes >>= BITS_PER_BYTE *
			(BYTES_PER_FIFO_WORD - private_data->write_cached_bytes_cnt);
		writel(private_data->write_cached_bytes,
		       uport->membase + SE_GENI_TX_FIFOn);
		private_data->write_cached_bytes_cnt = 0;
	}

	qcom_geni_serial_poll_tx_done(uport);
}

static void qcom_geni_serial_console_write(struct console *co, const char *s,
			      unsigned int count)
{
	struct uart_port *uport;
	struct qcom_geni_serial_port *port;
	u32 m_irq_en, s_irq_en;
	bool locked = true;
	unsigned long flags;

	WARN_ON(co->index < 0 || co->index >= GENI_UART_CONS_PORTS);

	port = get_port_from_line(co->index, true, NULL);
	if (IS_ERR(port))
		return;

	uport = &port->uport;
	if (oops_in_progress)
		locked = uart_port_trylock_irqsave(uport, &flags);
	else
		uart_port_lock_irqsave(uport, &flags);

	m_irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
	s_irq_en = readl(uport->membase + SE_GENI_S_IRQ_EN);
	writel(0, uport->membase + SE_GENI_M_IRQ_EN);
	writel(0, uport->membase + SE_GENI_S_IRQ_EN);

	if (qcom_geni_serial_main_active(uport)) {
		/* Wait for completion or drain FIFO */
		if (!locked || port->tx_remaining == 0)
			qcom_geni_serial_poll_tx_done(uport);
		else
			qcom_geni_serial_drain_fifo(uport);

		qcom_geni_serial_cancel_tx_cmd(uport);
	}

	__qcom_geni_serial_console_write(uport, s, count);

	writel(m_irq_en, uport->membase + SE_GENI_M_IRQ_EN);
	writel(s_irq_en, uport->membase + SE_GENI_S_IRQ_EN);

	if (locked)
		uart_port_unlock_irqrestore(uport, flags);
}

static void handle_rx_console(struct uart_port *uport, u32 bytes, bool drop)
{
	u32 i;
	unsigned char buf[sizeof(u32)];
	struct tty_port *tport;
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	tport = &uport->state->port;
	for (i = 0; i < bytes; ) {
		int c;
		int chunk = min_t(int, bytes - i, BYTES_PER_FIFO_WORD);

		ioread32_rep(uport->membase + SE_GENI_RX_FIFOn, buf, 1);
		i += chunk;
		if (drop)
			continue;

		for (c = 0; c < chunk; c++) {
			int sysrq;

			uport->icount.rx++;
			if (port->brk && buf[c] == 0) {
				port->brk = false;
				if (uart_handle_break(uport))
					continue;
			}

			sysrq = uart_prepare_sysrq_char(uport, buf[c]);

			if (!sysrq)
				tty_insert_flip_char(tport, buf[c], TTY_NORMAL);
		}
	}
	if (!drop)
		tty_flip_buffer_push(tport);
}
#else
static void handle_rx_console(struct uart_port *uport, u32 bytes, bool drop)
{

}
#endif /* CONFIG_SERIAL_QCOM_GENI_CONSOLE */

static void handle_rx_uart(struct uart_port *uport, u32 bytes)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	struct tty_port *tport = &uport->state->port;
	int ret;

	ret = tty_insert_flip_string(tport, port->rx_buf, bytes);
	if (ret != bytes) {
		dev_err_ratelimited(uport->dev, "failed to push data (%d < %u)\n",
				ret, bytes);
	}
	uport->icount.rx += ret;
	tty_flip_buffer_push(tport);
}

static unsigned int qcom_geni_serial_tx_empty(struct uart_port *uport)
{
	return !readl(uport->membase + SE_GENI_TX_FIFO_STATUS);
}

static void qcom_geni_serial_stop_tx_dma(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	bool done;

	if (!qcom_geni_serial_main_active(uport))
		return;

	if (port->tx_dma_addr) {
		geni_se_tx_dma_unprep(&port->se, port->tx_dma_addr,
				      port->tx_remaining);
		port->tx_dma_addr = 0;
		port->tx_remaining = 0;
	}

	geni_se_cancel_m_cmd(&port->se);

	done = qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
					 M_CMD_CANCEL_EN, true);
	if (!done) {
		geni_se_abort_m_cmd(&port->se);
		done = qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						 M_CMD_ABORT_EN, true);
		if (!done)
			dev_err_ratelimited(uport->dev, "M_CMD_ABORT_EN not set");
		writel(M_CMD_ABORT_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
	}

	writel(M_CMD_CANCEL_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
}

static void qcom_geni_serial_start_tx_dma(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	struct tty_port *tport = &uport->state->port;
	unsigned int xmit_size;
	u8 *tail;
	int ret;

	if (port->tx_dma_addr)
		return;

	if (kfifo_is_empty(&tport->xmit_fifo))
		return;

	xmit_size = kfifo_out_linear_ptr(&tport->xmit_fifo, &tail,
			UART_XMIT_SIZE);

	qcom_geni_set_rs485_mode(uport, SER_RS485_RTS_ON_SEND);

	qcom_geni_serial_setup_tx(uport, xmit_size);

	ret = geni_se_tx_dma_prep(&port->se, tail, xmit_size,
				  &port->tx_dma_addr);
	if (ret) {
		dev_err(uport->dev, "unable to start TX SE DMA: %d\n", ret);
		qcom_geni_serial_stop_tx_dma(uport);
		return;
	}

	port->tx_remaining = xmit_size;
}

static void qcom_geni_serial_start_tx_fifo(struct uart_port *uport)
{
	unsigned char c;
	u32 irq_en;

	/*
	 * Start a new transfer in case the previous command was cancelled and
	 * left data in the FIFO which may prevent the watermark interrupt
	 * from triggering. Note that the stale data is discarded.
	 */
	if (!qcom_geni_serial_main_active(uport) &&
	    !qcom_geni_serial_tx_empty(uport)) {
		if (uart_fifo_out(uport, &c, 1) == 1) {
			writel(M_CMD_DONE_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
			qcom_geni_serial_setup_tx(uport, 1);
			writel(c, uport->membase + SE_GENI_TX_FIFOn);
		}
	}

	irq_en = readl(uport->membase +	SE_GENI_M_IRQ_EN);
	irq_en |= M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN;
	writel(DEF_TX_WM, uport->membase + SE_GENI_TX_WATERMARK_REG);
	writel(irq_en, uport->membase +	SE_GENI_M_IRQ_EN);
}

static void qcom_geni_serial_stop_tx_fifo(struct uart_port *uport)
{
	u32 irq_en;

	irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
	irq_en &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN);
	writel(0, uport->membase + SE_GENI_TX_WATERMARK_REG);
	writel(irq_en, uport->membase + SE_GENI_M_IRQ_EN);
}

static void __qcom_geni_serial_cancel_tx_cmd(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	geni_se_cancel_m_cmd(&port->se);
	if (!qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_CANCEL_EN, true)) {
		geni_se_abort_m_cmd(&port->se);
		qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_ABORT_EN, true);
		writel(M_CMD_ABORT_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
	}
	writel(M_CMD_CANCEL_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
}

static void qcom_geni_serial_cancel_tx_cmd(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	if (!qcom_geni_serial_main_active(uport))
		return;

	__qcom_geni_serial_cancel_tx_cmd(uport);

	port->tx_remaining = 0;
	port->tx_queued = 0;
}

static void qcom_geni_serial_handle_rx_fifo(struct uart_port *uport, bool drop)
{
	u32 status;
	u32 word_cnt;
	u32 last_word_byte_cnt;
	u32 last_word_partial;
	u32 total_bytes;

	status = readl(uport->membase +	SE_GENI_RX_FIFO_STATUS);
	word_cnt = status & RX_FIFO_WC_MSK;
	last_word_partial = status & RX_LAST;
	last_word_byte_cnt = (status & RX_LAST_BYTE_VALID_MSK) >>
						RX_LAST_BYTE_VALID_SHFT;

	if (!word_cnt)
		return;
	total_bytes = BYTES_PER_FIFO_WORD * (word_cnt - 1);
	if (last_word_partial && last_word_byte_cnt)
		total_bytes += last_word_byte_cnt;
	else
		total_bytes += BYTES_PER_FIFO_WORD;
	handle_rx_console(uport, total_bytes, drop);
}

static void qcom_geni_serial_stop_rx_fifo(struct uart_port *uport)
{
	u32 irq_en;
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	u32 s_irq_status;

	irq_en = readl(uport->membase + SE_GENI_S_IRQ_EN);
	irq_en &= ~(S_RX_FIFO_WATERMARK_EN | S_RX_FIFO_LAST_EN);
	writel(irq_en, uport->membase + SE_GENI_S_IRQ_EN);

	irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
	irq_en &= ~(M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
	writel(irq_en, uport->membase + SE_GENI_M_IRQ_EN);

	if (!qcom_geni_serial_secondary_active(uport))
		return;

	geni_se_cancel_s_cmd(&port->se);
	qcom_geni_serial_poll_bit(uport, SE_GENI_S_IRQ_STATUS,
					S_CMD_CANCEL_EN, true);
	/*
	 * If timeout occurs secondary engine remains active
	 * and Abort sequence is executed.
	 */
	s_irq_status = readl(uport->membase + SE_GENI_S_IRQ_STATUS);
	/* Flush the Rx buffer */
	if (s_irq_status & S_RX_FIFO_LAST_EN)
		qcom_geni_serial_handle_rx_fifo(uport, true);
	writel(s_irq_status, uport->membase + SE_GENI_S_IRQ_CLEAR);

	if (qcom_geni_serial_secondary_active(uport))
		qcom_geni_serial_abort_rx(uport);
}

static void qcom_geni_serial_start_rx_fifo(struct uart_port *uport)
{
	u32 irq_en;
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	if (qcom_geni_serial_secondary_active(uport))
		qcom_geni_serial_stop_rx_fifo(uport);

	geni_se_setup_s_cmd(&port->se, UART_START_READ, 0);

	irq_en = readl(uport->membase + SE_GENI_S_IRQ_EN);
	irq_en |= S_RX_FIFO_WATERMARK_EN | S_RX_FIFO_LAST_EN;
	writel(irq_en, uport->membase + SE_GENI_S_IRQ_EN);

	irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
	irq_en |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
	writel(irq_en, uport->membase + SE_GENI_M_IRQ_EN);
}

static void qcom_geni_serial_stop_rx_dma(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	bool done;

	if (!qcom_geni_serial_secondary_active(uport))
		return;

	geni_se_cancel_s_cmd(&port->se);
	done = qcom_geni_serial_poll_bit(uport, SE_DMA_RX_IRQ_STAT,
			RX_EOT, true);
	if (done) {
		writel(RX_EOT | RX_DMA_DONE,
				uport->membase + SE_DMA_RX_IRQ_CLR);
	} else {
		qcom_geni_serial_abort_rx(uport);

		writel(1, uport->membase + SE_DMA_RX_FSM_RST);
		qcom_geni_serial_poll_bit(uport, SE_DMA_RX_IRQ_STAT,
				RX_RESET_DONE, true);
		writel(RX_RESET_DONE | RX_DMA_DONE,
				uport->membase + SE_DMA_RX_IRQ_CLR);
	}

	if (port->rx_dma_addr) {
		geni_se_rx_dma_unprep(&port->se, port->rx_dma_addr,
				      DMA_RX_BUF_SIZE);
		port->rx_dma_addr = 0;
	}
}

static void qcom_geni_serial_start_rx_dma(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	int ret;

	if (qcom_geni_serial_secondary_active(uport))
		qcom_geni_serial_stop_rx_dma(uport);

	geni_se_setup_s_cmd(&port->se, UART_START_READ, UART_PARAM_RFR_OPEN);

	ret = geni_se_rx_dma_prep(&port->se, port->rx_buf,
				  DMA_RX_BUF_SIZE,
				  &port->rx_dma_addr);
	if (ret) {
		dev_err(uport->dev, "unable to start RX SE DMA: %d\n", ret);
		qcom_geni_serial_stop_rx_dma(uport);
	}
}

static void qcom_geni_serial_handle_rx_dma(struct uart_port *uport, bool drop)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	u32 rx_in;
	int ret;

	if (!qcom_geni_serial_secondary_active(uport))
		return;

	if (!port->rx_dma_addr)
		return;

	geni_se_rx_dma_unprep(&port->se, port->rx_dma_addr, DMA_RX_BUF_SIZE);
	port->rx_dma_addr = 0;

	rx_in = readl(uport->membase + SE_DMA_RX_LEN_IN);
	if (!rx_in) {
		dev_warn(uport->dev, "serial engine reports 0 RX bytes in!\n");
		return;
	}

	if (!drop)
		handle_rx_uart(uport, rx_in);

	ret = geni_se_rx_dma_prep(&port->se, port->rx_buf,
				  DMA_RX_BUF_SIZE,
				  &port->rx_dma_addr);
	if (ret) {
		dev_err(uport->dev, "unable to start RX SE DMA: %d\n", ret);
		qcom_geni_serial_stop_rx_dma(uport);
	}
}

static void qcom_geni_serial_start_rx(struct uart_port *uport)
{
	uport->ops->start_rx(uport);
}

static void qcom_geni_serial_stop_rx(struct uart_port *uport)
{
	uport->ops->stop_rx(uport);
}

static void qcom_geni_serial_stop_tx(struct uart_port *uport)
{
	uport->ops->stop_tx(uport);
}

static void qcom_geni_serial_send_chunk_fifo(struct uart_port *uport,
					     unsigned int chunk)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	unsigned int tx_bytes, remaining = chunk;
	u8 buf[BYTES_PER_FIFO_WORD];

	while (remaining) {
		memset(buf, 0, sizeof(buf));
		tx_bytes = min(remaining, BYTES_PER_FIFO_WORD);

		uart_fifo_out(uport, buf, tx_bytes);

		iowrite32_rep(uport->membase + SE_GENI_TX_FIFOn, buf, 1);

		remaining -= tx_bytes;
		port->tx_remaining -= tx_bytes;
	}
}

static void qcom_geni_serial_handle_tx_fifo(struct uart_port *uport,
					    bool done, bool active)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	struct tty_port *tport = &uport->state->port;
	size_t avail;
	size_t pending;
	u32 status;
	u32 irq_en;
	unsigned int chunk;

	status = readl(uport->membase + SE_GENI_TX_FIFO_STATUS);

	/* Complete the current tx command before taking newly added data */
	if (active)
		pending = port->tx_remaining;
	else
		pending = kfifo_len(&tport->xmit_fifo);

	/* All data has been transmitted or command has been cancelled */
	if (!pending && done) {
		qcom_geni_serial_stop_tx_fifo(uport);
		goto out_write_wakeup;
	}

	if (active)
		avail = port->tx_fifo_depth - (status & TX_FIFO_WC);
	else
		avail = port->tx_fifo_depth;

	avail *= BYTES_PER_FIFO_WORD;

	chunk = min(avail, pending);
	if (!chunk)
		goto out_write_wakeup;

	if (!active) {
		qcom_geni_serial_setup_tx(uport, pending);
		port->tx_remaining = pending;
		port->tx_queued = 0;

		irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
		if (!(irq_en & M_TX_FIFO_WATERMARK_EN))
			writel(irq_en | M_TX_FIFO_WATERMARK_EN,
					uport->membase + SE_GENI_M_IRQ_EN);
	}

	qcom_geni_serial_send_chunk_fifo(uport, chunk);
	port->tx_queued += chunk;

	/*
	 * The tx fifo watermark is level triggered and latched. Though we had
	 * cleared it in qcom_geni_serial_isr it will have already reasserted
	 * so we must clear it again here after our writes.
	 */
	writel(M_TX_FIFO_WATERMARK_EN,
			uport->membase + SE_GENI_M_IRQ_CLEAR);

out_write_wakeup:
	if (!port->tx_remaining) {
		irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
		if (irq_en & M_TX_FIFO_WATERMARK_EN)
			writel(irq_en & ~M_TX_FIFO_WATERMARK_EN,
					uport->membase + SE_GENI_M_IRQ_EN);
	}

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(uport);
}

static void qcom_geni_serial_handle_tx_dma(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	struct tty_port *tport = &uport->state->port;

	uart_xmit_advance(uport, port->tx_remaining);
	geni_se_tx_dma_unprep(&port->se, port->tx_dma_addr, port->tx_remaining);
	port->tx_dma_addr = 0;
	port->tx_remaining = 0;

	if (!kfifo_is_empty(&tport->xmit_fifo))
		qcom_geni_serial_start_tx_dma(uport);

	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
		uart_write_wakeup(uport);
}

static irqreturn_t qcom_geni_serial_isr(int isr, void *dev)
{
	u32 m_irq_en;
	u32 m_irq_status;
	u32 s_irq_status;
	u32 geni_status;
	u32 dma;
	u32 dma_tx_status;
	u32 dma_rx_status;
	struct uart_port *uport = dev;
	bool drop_rx = false;
	struct tty_port *tport = &uport->state->port;
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	if (uport->suspended)
		return IRQ_NONE;

	uart_port_lock(uport);

	m_irq_status = readl(uport->membase + SE_GENI_M_IRQ_STATUS);
	s_irq_status = readl(uport->membase + SE_GENI_S_IRQ_STATUS);
	dma_tx_status = readl(uport->membase + SE_DMA_TX_IRQ_STAT);
	dma_rx_status = readl(uport->membase + SE_DMA_RX_IRQ_STAT);
	geni_status = readl(uport->membase + SE_GENI_STATUS);
	dma = readl(uport->membase + SE_GENI_DMA_MODE_EN);
	m_irq_en = readl(uport->membase + SE_GENI_M_IRQ_EN);
	writel(m_irq_status, uport->membase + SE_GENI_M_IRQ_CLEAR);
	writel(s_irq_status, uport->membase + SE_GENI_S_IRQ_CLEAR);
	writel(dma_tx_status, uport->membase + SE_DMA_TX_IRQ_CLR);
	writel(dma_rx_status, uport->membase + SE_DMA_RX_IRQ_CLR);

	if (WARN_ON(m_irq_status & M_ILLEGAL_CMD_EN))
		goto out_unlock;

	if (s_irq_status & S_RX_FIFO_WR_ERR_EN) {
		uport->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
	}

	if (s_irq_status & (S_GP_IRQ_0_EN | S_GP_IRQ_1_EN)) {
		if (s_irq_status & S_GP_IRQ_0_EN)
			uport->icount.parity++;
		drop_rx = true;
	} else if (s_irq_status & (S_GP_IRQ_2_EN | S_GP_IRQ_3_EN)) {
		uport->icount.brk++;
		port->brk = true;
	}

	if (dma) {
		if (dma_tx_status & TX_DMA_DONE) {
			qcom_geni_serial_handle_tx_dma(uport);
			qcom_geni_set_rs485_mode(uport, SER_RS485_RTS_AFTER_SEND);
	}

		if (dma_rx_status) {
			if (dma_rx_status & RX_RESET_DONE)
				goto out_unlock;

			if (dma_rx_status & RX_DMA_PARITY_ERR) {
				uport->icount.parity++;
				drop_rx = true;
			}

			if (dma_rx_status & RX_DMA_BREAK)
				uport->icount.brk++;

			if (dma_rx_status & (RX_DMA_DONE | RX_EOT))
				qcom_geni_serial_handle_rx_dma(uport, drop_rx);
		}
	} else {
		if (m_irq_status & m_irq_en &
		    (M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN))
			qcom_geni_serial_handle_tx_fifo(uport,
					m_irq_status & M_CMD_DONE_EN,
					geni_status & M_GENI_CMD_ACTIVE);

		if (s_irq_status & (S_RX_FIFO_WATERMARK_EN | S_RX_FIFO_LAST_EN))
			qcom_geni_serial_handle_rx_fifo(uport, drop_rx);
	}

out_unlock:
	uart_unlock_and_check_sysrq(uport);

	return IRQ_HANDLED;
}

static int setup_fifos(struct qcom_geni_serial_port *port)
{
	struct uart_port *uport;
	u32 old_rx_fifo_depth = port->rx_fifo_depth;

	uport = &port->uport;
	port->tx_fifo_depth = geni_se_get_tx_fifo_depth(&port->se);
	port->tx_fifo_width = geni_se_get_tx_fifo_width(&port->se);
	port->rx_fifo_depth = geni_se_get_rx_fifo_depth(&port->se);
	uport->fifosize =
		(port->tx_fifo_depth * port->tx_fifo_width) / BITS_PER_BYTE;

	if (port->rx_buf && (old_rx_fifo_depth != port->rx_fifo_depth) && port->rx_fifo_depth) {
		/*
		 * Use krealloc rather than krealloc_array because rx_buf is
		 * accessed as 1 byte entries as well as 4 byte entries so it's
		 * not necessarily an array.
		 */
		port->rx_buf = devm_krealloc(uport->dev, port->rx_buf,
					     port->rx_fifo_depth * sizeof(u32),
					     GFP_KERNEL);
		if (!port->rx_buf)
			return -ENOMEM;
	}

	return 0;
}


static void qcom_geni_serial_shutdown(struct uart_port *uport)
{
	disable_irq(uport->irq);

	uart_port_lock_irq(uport);
	qcom_geni_serial_stop_tx(uport);
	qcom_geni_serial_stop_rx(uport);

	qcom_geni_serial_cancel_tx_cmd(uport);
	uart_port_unlock_irq(uport);
}

static void qcom_geni_serial_flush_buffer(struct uart_port *uport)
{
	qcom_geni_serial_cancel_tx_cmd(uport);
}

static int qcom_geni_serial_port_setup(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	u32 rxstale = DEFAULT_BITS_PER_CHAR * STALE_TIMEOUT;
	u32 proto;
	u32 pin_swap;
	int ret;

	proto = geni_se_read_proto(&port->se);
	if (proto == GENI_SE_INVALID_PROTO) {
		ret = geni_load_se_firmware(&port->se, GENI_SE_UART);
		if (ret) {
			dev_err(uport->dev, "UART firmware load failed ret: %d\n", ret);
			return ret;
		}
	} else if (proto != GENI_SE_UART) {
		dev_err(uport->dev, "Invalid FW loaded, proto: %d\n", proto);
		return -ENXIO;
	}

	qcom_geni_serial_stop_rx(uport);

	ret = setup_fifos(port);
	if (ret)
		return ret;

	writel(rxstale, uport->membase + SE_UART_RX_STALE_CNT);

	pin_swap = readl(uport->membase + SE_UART_IO_MACRO_CTRL);
	if (port->rx_tx_swap) {
		pin_swap &= ~DEFAULT_IO_MACRO_IO2_IO3_MASK;
		pin_swap |= IO_MACRO_IO2_IO3_SWAP;
	}
	if (port->cts_rts_swap) {
		pin_swap &= ~DEFAULT_IO_MACRO_IO0_IO1_MASK;
		pin_swap |= IO_MACRO_IO0_SEL;
	}
	/* Configure this register if RX-TX, CTS-RTS pins are swapped */
	if (port->rx_tx_swap || port->cts_rts_swap)
		writel(pin_swap, uport->membase + SE_UART_IO_MACRO_CTRL);

	/*
	 * Make an unconditional cancel on the main sequencer to reset
	 * it else we could end up in data loss scenarios.
	 */
	if (uart_console(uport))
		qcom_geni_serial_poll_tx_done(uport);
	geni_se_config_packing(&port->se, BITS_PER_BYTE, BYTES_PER_FIFO_WORD,
			       false, true, true);
	geni_se_init(&port->se, UART_RX_WM, port->rx_fifo_depth - 2);
	geni_se_select_mode(&port->se, port->dev_data->mode);
	port->setup = true;

	return 0;
}

static int qcom_geni_serial_startup(struct uart_port *uport)
{
	int ret;
	struct qcom_geni_serial_port *port = to_dev_port(uport);

	if (!port->setup) {
		ret = qcom_geni_serial_port_setup(uport);
		if (ret)
			return ret;
	}

	uart_port_lock_irq(uport);
	qcom_geni_serial_start_rx(uport);
	uart_port_unlock_irq(uport);

	enable_irq(uport->irq);

	return 0;
}

static int geni_serial_set_rate(struct uart_port *uport, unsigned int baud)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	unsigned long clk_rate;
	unsigned int avg_bw_core, clk_idx;
	unsigned int clk_div;
	u32 ver, sampling_rate;
	u32 ser_clk_cfg;
	int ret;

	sampling_rate = UART_OVERSAMPLING;
	/* Sampling rate is halved for IP versions >= 2.5 */
	ver = geni_se_get_qup_hw_version(&port->se);
	if (ver >= QUP_SE_VERSION_2_5)
		sampling_rate /= 2;

	ret = geni_se_clk_freq_match(&port->se, baud * sampling_rate, &clk_idx, &clk_rate, false);
	if (ret) {
		dev_err(port->se.dev, "Failed to find src clk for baud rate: %d ret: %d\n",
			baud, ret);
		return ret;
	}

	clk_div = DIV_ROUND_UP(clk_rate, baud * sampling_rate);
	/* Check if calculated divider exceeds maximum allowed value */
	if (clk_div > (CLK_DIV_MSK >> CLK_DIV_SHFT)) {
		dev_err(port->se.dev, "Calculated clock divider %u exceeds maximum\n", clk_div);
		return -EINVAL;
	}

	dev_dbg(port->se.dev, "desired_rate = %u, clk_rate = %lu, clk_div = %u\n, clk_idx = %u\n",
		baud * sampling_rate, clk_rate, clk_div, clk_idx);

	uport->uartclk = clk_rate;
	port->clk_rate = clk_rate;
	dev_pm_opp_set_rate(uport->dev, clk_rate);
	ser_clk_cfg = SER_CLK_EN;
	ser_clk_cfg |= clk_div << CLK_DIV_SHFT;

	/*
	 * Bump up BW vote on CPU and CORE path as driver supports FIFO mode
	 * only.
	 */
	avg_bw_core = (baud > 115200) ? Bps_to_icc(CORE_2X_50_MHZ)
						: GENI_DEFAULT_BW;
	port->se.icc_paths[GENI_TO_CORE].avg_bw = avg_bw_core;
	port->se.icc_paths[CPU_TO_GENI].avg_bw = Bps_to_icc(baud);
	geni_icc_set_bw(&port->se);

	writel(ser_clk_cfg, uport->membase + GENI_SER_M_CLK_CFG);
	writel(ser_clk_cfg, uport->membase + GENI_SER_S_CLK_CFG);
	/* Configure clock selection register with the selected clock index */
	writel(clk_idx & CLK_SEL_MSK, uport->membase + SE_GENI_CLK_SEL);
	return 0;
}

static void qcom_geni_serial_set_termios(struct uart_port *uport,
					 struct ktermios *termios,
					 const struct ktermios *old)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	unsigned int baud;
	unsigned long timeout;
	u32 bits_per_char;
	u32 tx_trans_cfg;
	u32 tx_parity_cfg;
	u32 rx_trans_cfg;
	u32 rx_parity_cfg;
	u32 stop_bit_len;
	int ret = 0;

	/* baud rate */
	baud = uart_get_baud_rate(uport, termios, old, 300, 8000000);

	ret = geni_serial_set_rate(uport, baud);
	if (ret)
		return;

	/* parity */
	tx_trans_cfg = readl(uport->membase + SE_UART_TX_TRANS_CFG);
	tx_parity_cfg = readl(uport->membase + SE_UART_TX_PARITY_CFG);
	rx_trans_cfg = readl(uport->membase + SE_UART_RX_TRANS_CFG);
	rx_parity_cfg = readl(uport->membase + SE_UART_RX_PARITY_CFG);
	if (termios->c_cflag & PARENB) {
		tx_trans_cfg |= UART_TX_PAR_EN;
		rx_trans_cfg |= UART_RX_PAR_EN;
		tx_parity_cfg |= PAR_CALC_EN;
		rx_parity_cfg |= PAR_CALC_EN;
		if (termios->c_cflag & PARODD) {
			tx_parity_cfg |= PAR_ODD;
			rx_parity_cfg |= PAR_ODD;
		} else if (termios->c_cflag & CMSPAR) {
			tx_parity_cfg |= PAR_SPACE;
			rx_parity_cfg |= PAR_SPACE;
		} else {
			tx_parity_cfg |= PAR_EVEN;
			rx_parity_cfg |= PAR_EVEN;
		}
	} else {
		tx_trans_cfg &= ~UART_TX_PAR_EN;
		rx_trans_cfg &= ~UART_RX_PAR_EN;
		tx_parity_cfg &= ~PAR_CALC_EN;
		rx_parity_cfg &= ~PAR_CALC_EN;
	}

	/* bits per char */
	bits_per_char = tty_get_char_size(termios->c_cflag);

	/* stop bits */
	if (termios->c_cflag & CSTOPB)
		stop_bit_len = TX_STOP_BIT_LEN_2;
	else
		stop_bit_len = TX_STOP_BIT_LEN_1;

	/* flow control, clear the CTS_MASK bit if using flow control. */
	if (termios->c_cflag & CRTSCTS)
		tx_trans_cfg &= ~UART_CTS_MASK;
	else
		tx_trans_cfg |= UART_CTS_MASK;

	if (baud) {
		uart_update_timeout(uport, termios->c_cflag, baud);

		/*
		 * Make sure that qcom_geni_serial_poll_bitfield() waits for
		 * the FIFO, two-word intermediate transfer register and shift
		 * register to clear.
		 *
		 * Note that uart_fifo_timeout() also adds a 20 ms margin.
		 */
		timeout = jiffies_to_usecs(uart_fifo_timeout(uport));
		timeout += 3 * timeout / port->tx_fifo_depth;
		WRITE_ONCE(port->poll_timeout_us, timeout);
	}

	if (!uart_console(uport))
		writel(port->loopback,
				uport->membase + SE_UART_LOOPBACK_CFG);
	writel(tx_trans_cfg, uport->membase + SE_UART_TX_TRANS_CFG);
	writel(tx_parity_cfg, uport->membase + SE_UART_TX_PARITY_CFG);
	writel(rx_trans_cfg, uport->membase + SE_UART_RX_TRANS_CFG);
	writel(rx_parity_cfg, uport->membase + SE_UART_RX_PARITY_CFG);
	writel(bits_per_char, uport->membase + SE_UART_TX_WORD_LEN);
	writel(bits_per_char, uport->membase + SE_UART_RX_WORD_LEN);
	writel(stop_bit_len, uport->membase + SE_UART_TX_STOP_BIT_LEN);
}

#ifdef CONFIG_SERIAL_QCOM_GENI_CONSOLE
static int qcom_geni_console_setup(struct console *co, char *options)
{
	struct uart_port *uport;
	struct qcom_geni_serial_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	if (co->index >= GENI_UART_CONS_PORTS  || co->index < 0)
		return -ENXIO;

	port = get_port_from_line(co->index, true, NULL);
	if (IS_ERR(port)) {
		pr_err("Invalid line %d\n", co->index);
		return PTR_ERR(port);
	}

	uport = &port->uport;

	if (unlikely(!uport->membase))
		return -ENXIO;

	if (!port->setup) {
		ret = qcom_geni_serial_port_setup(uport);
		if (ret)
			return ret;
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(uport, co, baud, parity, bits, flow);
}

static void qcom_geni_serial_earlycon_write(struct console *con,
					const char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;

	__qcom_geni_serial_console_write(&dev->port, s, n);
}

#ifdef CONFIG_CONSOLE_POLL
static int qcom_geni_serial_earlycon_read(struct console *con,
					  char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;
	struct uart_port *uport = &dev->port;
	int num_read = 0;
	int ch;

	while (num_read < n) {
		ch = qcom_geni_serial_get_char(uport);
		if (ch == NO_POLL_CHAR)
			break;
		s[num_read++] = ch;
	}

	return num_read;
}

static void __init qcom_geni_serial_enable_early_read(struct geni_se *se,
						      struct console *con)
{
	geni_se_setup_s_cmd(se, UART_START_READ, 0);
	con->read = qcom_geni_serial_earlycon_read;
}
#else
static inline void qcom_geni_serial_enable_early_read(struct geni_se *se,
						      struct console *con) { }
#endif

static struct qcom_geni_private_data earlycon_private_data;

static int __init qcom_geni_serial_earlycon_setup(struct earlycon_device *dev,
								const char *opt)
{
	struct uart_port *uport = &dev->port;
	u32 tx_trans_cfg;
	u32 tx_parity_cfg = 0;	/* Disable Tx Parity */
	u32 rx_trans_cfg = 0;
	u32 rx_parity_cfg = 0;	/* Disable Rx Parity */
	u32 stop_bit_len = 0;	/* Default stop bit length - 1 bit */
	u32 bits_per_char;
	struct geni_se se;

	if (!uport->membase)
		return -EINVAL;

	uport->private_data = &earlycon_private_data;

	memset(&se, 0, sizeof(se));
	se.base = uport->membase;
	if (geni_se_read_proto(&se) != GENI_SE_UART)
		return -ENXIO;
	/*
	 * Ignore Flow control.
	 * n = 8.
	 */
	tx_trans_cfg = UART_CTS_MASK;
	bits_per_char = BITS_PER_BYTE;

	/*
	 * Make an unconditional cancel on the main sequencer to reset
	 * it else we could end up in data loss scenarios.
	 */
	qcom_geni_serial_poll_tx_done(uport);
	qcom_geni_serial_abort_rx(uport);
	geni_se_config_packing(&se, BITS_PER_BYTE, BYTES_PER_FIFO_WORD,
			       false, true, true);
	geni_se_init(&se, DEF_FIFO_DEPTH_WORDS / 2, DEF_FIFO_DEPTH_WORDS - 2);
	geni_se_select_mode(&se, GENI_SE_FIFO);

	writel(tx_trans_cfg, uport->membase + SE_UART_TX_TRANS_CFG);
	writel(tx_parity_cfg, uport->membase + SE_UART_TX_PARITY_CFG);
	writel(rx_trans_cfg, uport->membase + SE_UART_RX_TRANS_CFG);
	writel(rx_parity_cfg, uport->membase + SE_UART_RX_PARITY_CFG);
	writel(bits_per_char, uport->membase + SE_UART_TX_WORD_LEN);
	writel(bits_per_char, uport->membase + SE_UART_RX_WORD_LEN);
	writel(stop_bit_len, uport->membase + SE_UART_TX_STOP_BIT_LEN);

	dev->con->write = qcom_geni_serial_earlycon_write;
	dev->con->setup = NULL;
	qcom_geni_serial_enable_early_read(&se, dev->con);

	return 0;
}
OF_EARLYCON_DECLARE(qcom_geni, "qcom,geni-debug-uart",
				qcom_geni_serial_earlycon_setup);

static int __init console_register(struct uart_driver *drv)
{
	return uart_register_driver(drv);
}

static void console_unregister(struct uart_driver *drv)
{
	uart_unregister_driver(drv);
}

static struct console cons_ops = {
	.name = "ttyMSM",
	.write = qcom_geni_serial_console_write,
	.device = uart_console_device,
	.setup = qcom_geni_console_setup,
	.flags = CON_PRINTBUFFER,
	.index = -1,
	.data = &qcom_geni_console_driver,
};

static struct uart_driver qcom_geni_console_driver = {
	.owner = THIS_MODULE,
	.driver_name = "qcom_geni_console",
	.dev_name = "ttyMSM",
	.nr =  GENI_UART_CONS_PORTS,
	.cons = &cons_ops,
};
#else
static int console_register(struct uart_driver *drv)
{
	return 0;
}

static void console_unregister(struct uart_driver *drv)
{
}
#endif /* CONFIG_SERIAL_QCOM_GENI_CONSOLE */

static struct uart_driver qcom_geni_uart_driver = {
	.owner = THIS_MODULE,
	.driver_name = "qcom_geni_uart",
	.dev_name = "ttyHS",
	.nr = CONFIG_SERIAL_QCOM_GENI_UART_PORTS,
};

static int geni_serial_resources_on(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	int ret;

	ret = geni_icc_enable(&port->se);
	if (ret)
		return ret;

	ret = geni_se_resources_on(&port->se);
	if (ret) {
		geni_icc_disable(&port->se);
		return ret;
	}

	if (port->clk_rate)
		dev_pm_opp_set_rate(uport->dev, port->clk_rate);

	return 0;
}

static int geni_serial_resources_off(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport);
	int ret;

	dev_pm_opp_set_rate(uport->dev, 0);
	ret = geni_se_resources_off(&port->se);
	if (ret)
		return ret;

	geni_icc_disable(&port->se);

	return 0;
}

static int geni_serial_resource_init(struct qcom_geni_serial_port *port)
{
	int ret;

	port->se.clk = devm_clk_get(port->se.dev, "se");
	if (IS_ERR(port->se.clk)) {
		ret = PTR_ERR(port->se.clk);
		dev_err(port->se.dev, "Err getting SE Core clk %d\n", ret);
		return ret;
	}

	ret = geni_icc_get(&port->se, NULL);
	if (ret)
		return ret;

	port->se.icc_paths[GENI_TO_CORE].avg_bw = GENI_DEFAULT_BW;
	port->se.icc_paths[CPU_TO_GENI].avg_bw = GENI_DEFAULT_BW;

	/* Set BW for register access */
	ret = geni_icc_set_bw(&port->se);
	if (ret)
		return ret;

	ret = devm_pm_opp_set_clkname(port->se.dev, "se");
	if (ret)
		return ret;

	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(port->se.dev);
	if (ret && ret != -ENODEV) {
		dev_err(port->se.dev, "invalid OPP table in device tree\n");
		return ret;
	}

	return 0;
}

static void qcom_geni_serial_pm(struct uart_port *uport,
		unsigned int new_state, unsigned int old_state)
{

	/* If we've never been called, treat it as off */
	if (old_state == UART_PM_STATE_UNDEFINED)
		old_state = UART_PM_STATE_OFF;

	if (new_state == UART_PM_STATE_ON && old_state == UART_PM_STATE_OFF)
		geni_serial_resources_on(uport);
	else if (new_state == UART_PM_STATE_OFF &&
		 old_state == UART_PM_STATE_ON)
		geni_serial_resources_off(uport);

}

/**
 * qcom_geni_rs485_config - Configure RS485 settings for the UART port
 * @uport: Pointer to the UART port structure
 * @termios: Pointer to the termios structure
 * @rs485: Pointer to the RS485 configuration structure
 * This function configures the RTS (Request to Send) pin behavior for RS485 mode.
 * When RS485 mode is enabled, the RTS pin is kept in default ACTIVE HIGH state.
 * Return: Always returns 0.
 */

static int qcom_geni_rs485_config(struct uart_port *uport,
				  struct ktermios *termios, struct serial_rs485 *rs485)
{
	qcom_geni_set_rs485_mode(uport, SER_RS485_ENABLED);

	return 0;
}

static const struct uart_ops qcom_geni_console_pops = {
	.tx_empty = qcom_geni_serial_tx_empty,
	.stop_tx = qcom_geni_serial_stop_tx_fifo,
	.start_tx = qcom_geni_serial_start_tx_fifo,
	.stop_rx = qcom_geni_serial_stop_rx_fifo,
	.start_rx = qcom_geni_serial_start_rx_fifo,
	.set_termios = qcom_geni_serial_set_termios,
	.startup = qcom_geni_serial_startup,
	.request_port = qcom_geni_serial_request_port,
	.config_port = qcom_geni_serial_config_port,
	.shutdown = qcom_geni_serial_shutdown,
	.flush_buffer = qcom_geni_serial_flush_buffer,
	.type = qcom_geni_serial_get_type,
	.set_mctrl = qcom_geni_serial_set_mctrl,
	.get_mctrl = qcom_geni_serial_get_mctrl,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= qcom_geni_serial_get_char,
	.poll_put_char	= qcom_geni_serial_poll_put_char,
	.poll_init = qcom_geni_serial_poll_init,
#endif
	.pm = qcom_geni_serial_pm,
};

static const struct uart_ops qcom_geni_uart_pops = {
	.tx_empty = qcom_geni_serial_tx_empty,
	.stop_tx = qcom_geni_serial_stop_tx_dma,
	.start_tx = qcom_geni_serial_start_tx_dma,
	.start_rx = qcom_geni_serial_start_rx_dma,
	.stop_rx = qcom_geni_serial_stop_rx_dma,
	.set_termios = qcom_geni_serial_set_termios,
	.startup = qcom_geni_serial_startup,
	.request_port = qcom_geni_serial_request_port,
	.config_port = qcom_geni_serial_config_port,
	.shutdown = qcom_geni_serial_shutdown,
	.type = qcom_geni_serial_get_type,
	.set_mctrl = qcom_geni_serial_set_mctrl,
	.get_mctrl = qcom_geni_serial_get_mctrl,
	.pm = qcom_geni_serial_pm,
};

static int qcom_geni_serial_probe(struct platform_device *pdev)
{
	int ret = 0;
	int line;
	struct qcom_geni_serial_port *port;
	struct uart_port *uport;
	struct resource *res;
	int irq;
	struct uart_driver *drv;
	const struct qcom_geni_device_data *data;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -EINVAL;

	if (data->console) {
		drv = &qcom_geni_console_driver;
		line = of_alias_get_id(pdev->dev.of_node, "serial");
	} else {
		drv = &qcom_geni_uart_driver;
		line = of_alias_get_id(pdev->dev.of_node, "serial");
		if (line == -ENODEV) /* compat with non-standard aliases */
			line = of_alias_get_id(pdev->dev.of_node, "hsuart");
	}

	port = get_port_from_line(line, data->console, &pdev->dev);
	if (IS_ERR(port)) {
		dev_err(&pdev->dev, "Invalid line %d\n", line);
		return PTR_ERR(port);
	}

	uport = &port->uport;
	/* Don't allow 2 drivers to access the same port */
	if (uport->private_data)
		return -ENODEV;

	uport->dev = &pdev->dev;
	port->dev_data = data;
	port->se.dev = &pdev->dev;
	port->se.wrapper = dev_get_drvdata(pdev->dev.parent);

	ret = geni_serial_resource_init(port);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	uport->mapbase = res->start;

	uport->rs485_config = qcom_geni_rs485_config;
	uport->rs485_supported = qcom_geni_rs485_supported;
	port->tx_fifo_depth = DEF_FIFO_DEPTH_WORDS;
	port->rx_fifo_depth = DEF_FIFO_DEPTH_WORDS;
	port->tx_fifo_width = DEF_FIFO_WIDTH_BITS;

	if (!data->console) {
		port->rx_buf = devm_kzalloc(uport->dev,
					    DMA_RX_BUF_SIZE, GFP_KERNEL);
		if (!port->rx_buf)
			return -ENOMEM;
	}

	port->name = devm_kasprintf(uport->dev, GFP_KERNEL,
			"qcom_geni_serial_%s%d",
			uart_console(uport) ? "console" : "uart", uport->line);
	if (!port->name)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	uport->irq = irq;
	uport->has_sysrq = IS_ENABLED(CONFIG_SERIAL_QCOM_GENI_CONSOLE);

	if (!data->console)
		port->wakeup_irq = platform_get_irq_optional(pdev, 1);

	if (of_property_read_bool(pdev->dev.of_node, "rx-tx-swap"))
		port->rx_tx_swap = true;

	if (of_property_read_bool(pdev->dev.of_node, "cts-rts-swap"))
		port->cts_rts_swap = true;

	port->private_data.drv = drv;
	uport->private_data = &port->private_data;
	platform_set_drvdata(pdev, port);

	irq_set_status_flags(uport->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(uport->dev, uport->irq, qcom_geni_serial_isr,
			IRQF_TRIGGER_HIGH, port->name, uport);
	if (ret) {
		dev_err(uport->dev, "Failed to get IRQ ret %d\n", ret);
		return ret;
	}

	ret = uart_get_rs485_mode(uport);
	if (ret)
		return ret;

	ret = uart_add_one_port(drv, uport);
	if (ret)
		return ret;

	if (port->wakeup_irq > 0) {
		device_init_wakeup(&pdev->dev, true);
		ret = dev_pm_set_dedicated_wake_irq(&pdev->dev,
						port->wakeup_irq);
		if (ret) {
			device_init_wakeup(&pdev->dev, false);
			ida_free(&port_ida, uport->line);
			uart_remove_one_port(drv, uport);
			return ret;
		}
	}

	return 0;
}

static void qcom_geni_serial_remove(struct platform_device *pdev)
{
	struct qcom_geni_serial_port *port = platform_get_drvdata(pdev);
	struct uart_port *uport = &port->uport;
	struct uart_driver *drv = port->private_data.drv;

	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
	ida_free(&port_ida, uport->line);
	uart_remove_one_port(drv, &port->uport);
}

static int qcom_geni_serial_suspend(struct device *dev)
{
	struct qcom_geni_serial_port *port = dev_get_drvdata(dev);
	struct uart_port *uport = &port->uport;
	struct qcom_geni_private_data *private_data = uport->private_data;

	/*
	 * This is done so we can hit the lowest possible state in suspend
	 * even with no_console_suspend
	 */
	if (uart_console(uport)) {
		geni_icc_set_tag(&port->se, QCOM_ICC_TAG_ACTIVE_ONLY);
		geni_icc_set_bw(&port->se);
	}
	return uart_suspend_port(private_data->drv, uport);
}

static int qcom_geni_serial_resume(struct device *dev)
{
	int ret;
	struct qcom_geni_serial_port *port = dev_get_drvdata(dev);
	struct uart_port *uport = &port->uport;
	struct qcom_geni_private_data *private_data = uport->private_data;

	ret = uart_resume_port(private_data->drv, uport);
	if (uart_console(uport)) {
		geni_icc_set_tag(&port->se, QCOM_ICC_TAG_ALWAYS);
		geni_icc_set_bw(&port->se);
	}
	return ret;
}

static const struct qcom_geni_device_data qcom_geni_console_data = {
	.console = true,
	.mode = GENI_SE_FIFO,
};

static const struct qcom_geni_device_data qcom_geni_uart_data = {
	.console = false,
	.mode = GENI_SE_DMA,
};

static const struct dev_pm_ops qcom_geni_serial_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(qcom_geni_serial_suspend, qcom_geni_serial_resume)
};

static const struct of_device_id qcom_geni_serial_match_table[] = {
	{
		.compatible = "qcom,geni-debug-uart",
		.data = &qcom_geni_console_data,
	},
	{
		.compatible = "qcom,geni-uart",
		.data = &qcom_geni_uart_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_geni_serial_match_table);

static struct platform_driver qcom_geni_serial_platform_driver = {
	.remove = qcom_geni_serial_remove,
	.probe = qcom_geni_serial_probe,
	.driver = {
		.name = "qcom_geni_serial",
		.of_match_table = qcom_geni_serial_match_table,
		.pm = &qcom_geni_serial_pm_ops,
	},
};

static int __init qcom_geni_serial_init(void)
{
	int ret;

	ret = console_register(&qcom_geni_console_driver);
	if (ret)
		return ret;

	ret = uart_register_driver(&qcom_geni_uart_driver);
	if (ret) {
		console_unregister(&qcom_geni_console_driver);
		return ret;
	}

	ret = platform_driver_register(&qcom_geni_serial_platform_driver);
	if (ret) {
		console_unregister(&qcom_geni_console_driver);
		uart_unregister_driver(&qcom_geni_uart_driver);
	}
	return ret;
}
module_init(qcom_geni_serial_init);

static void __exit qcom_geni_serial_exit(void)
{
	platform_driver_unregister(&qcom_geni_serial_platform_driver);
	console_unregister(&qcom_geni_console_driver);
	uart_unregister_driver(&qcom_geni_uart_driver);
}
module_exit(qcom_geni_serial_exit);

MODULE_DESCRIPTION("Serial driver for GENI based QUP cores");
MODULE_LICENSE("GPL v2");
