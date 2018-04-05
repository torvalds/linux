// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018, The Linux foundation. All rights reserved.

#include <linux/clk.h>
#include <linux/console.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/qcom-geni-se.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* UART specific GENI registers */
#define SE_UART_TX_TRANS_CFG		0x25c
#define SE_UART_TX_WORD_LEN		0x268
#define SE_UART_TX_STOP_BIT_LEN		0x26c
#define SE_UART_TX_TRANS_LEN		0x270
#define SE_UART_RX_TRANS_CFG		0x280
#define SE_UART_RX_WORD_LEN		0x28c
#define SE_UART_RX_STALE_CNT		0x294
#define SE_UART_TX_PARITY_CFG		0x2a4
#define SE_UART_RX_PARITY_CFG		0x2a8

/* SE_UART_TRANS_CFG */
#define UART_TX_PAR_EN		BIT(0)
#define UART_CTS_MASK		BIT(1)

/* SE_UART_TX_WORD_LEN */
#define TX_WORD_LEN_MSK		GENMASK(9, 0)

/* SE_UART_TX_STOP_BIT_LEN */
#define TX_STOP_BIT_LEN_MSK	GENMASK(23, 0)
#define TX_STOP_BIT_LEN_1	0
#define TX_STOP_BIT_LEN_1_5	1
#define TX_STOP_BIT_LEN_2	2

/* SE_UART_TX_TRANS_LEN */
#define TX_TRANS_LEN_MSK	GENMASK(23, 0)

/* SE_UART_RX_TRANS_CFG */
#define UART_RX_INS_STATUS_BIT	BIT(2)
#define UART_RX_PAR_EN		BIT(3)

/* SE_UART_RX_WORD_LEN */
#define RX_WORD_LEN_MASK	GENMASK(9, 0)

/* SE_UART_RX_STALE_CNT */
#define RX_STALE_CNT		GENMASK(23, 0)

/* SE_UART_TX_PARITY_CFG/RX_PARITY_CFG */
#define PAR_CALC_EN		BIT(0)
#define PAR_MODE_MSK		GENMASK(2, 1)
#define PAR_MODE_SHFT		1
#define PAR_EVEN		0x00
#define PAR_ODD			0x01
#define PAR_SPACE		0x10
#define PAR_MARK		0x11

/* UART M_CMD OP codes */
#define UART_START_TX		0x1
#define UART_START_BREAK	0x4
#define UART_STOP_BREAK		0x5
/* UART S_CMD OP codes */
#define UART_START_READ		0x1
#define UART_PARAM		0x1

#define UART_OVERSAMPLING	32
#define STALE_TIMEOUT		16
#define DEFAULT_BITS_PER_CHAR	10
#define GENI_UART_CONS_PORTS	1
#define DEF_FIFO_DEPTH_WORDS	16
#define DEF_TX_WM		2
#define DEF_FIFO_WIDTH_BITS	32
#define UART_CONSOLE_RX_WM	2

#ifdef CONFIG_CONSOLE_POLL
#define RX_BYTES_PW 1
#else
#define RX_BYTES_PW 4
#endif

struct qcom_geni_serial_port {
	struct uart_port uport;
	struct geni_se se;
	char name[20];
	u32 tx_fifo_depth;
	u32 tx_fifo_width;
	u32 rx_fifo_depth;
	u32 tx_wm;
	u32 rx_wm;
	u32 rx_rfr;
	enum geni_se_xfer_mode xfer_mode;
	bool setup;
	int (*handle_rx)(struct uart_port *uport, u32 bytes, bool drop);
	unsigned int xmit_size;
	unsigned int baud;
	unsigned int tx_bytes_pw;
	unsigned int rx_bytes_pw;
	bool brk;
};

static const struct uart_ops qcom_geni_serial_pops;
static struct uart_driver qcom_geni_console_driver;
static int handle_rx_console(struct uart_port *uport, u32 bytes, bool drop);
static unsigned int qcom_geni_serial_tx_empty(struct uart_port *port);
static void qcom_geni_serial_stop_rx(struct uart_port *uport);

static const unsigned long root_freq[] = {7372800, 14745600, 19200000, 29491200,
					32000000, 48000000, 64000000, 80000000,
					96000000, 100000000};

#define to_dev_port(ptr, member) \
		container_of(ptr, struct qcom_geni_serial_port, member)

static struct qcom_geni_serial_port qcom_geni_console_port;

static int qcom_geni_serial_request_port(struct uart_port *uport)
{
	struct platform_device *pdev = to_platform_device(uport->dev);
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	uport->membase = devm_ioremap_resource(&pdev->dev, res);
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

static unsigned int qcom_geni_cons_get_mctrl(struct uart_port *uport)
{
	return TIOCM_DSR | TIOCM_CAR | TIOCM_CTS;
}

static void qcom_geni_cons_set_mctrl(struct uart_port *uport,
							unsigned int mctrl)
{
}

static const char *qcom_geni_serial_get_type(struct uart_port *uport)
{
	return "MSM";
}

static struct qcom_geni_serial_port *get_port_from_line(int line)
{
	if (line < 0 || line >= GENI_UART_CONS_PORTS)
		return ERR_PTR(-ENXIO);
	return &qcom_geni_console_port;
}

static bool qcom_geni_serial_poll_bit(struct uart_port *uport,
				int offset, int field, bool set)
{
	u32 reg;
	struct qcom_geni_serial_port *port;
	unsigned int baud;
	unsigned int fifo_bits;
	unsigned long timeout_us = 20000;

	/* Ensure polling is not re-ordered before the prior writes/reads */
	mb();

	if (uport->private_data) {
		port = to_dev_port(uport, uport);
		baud = port->baud;
		if (!baud)
			baud = 115200;
		fifo_bits = port->tx_fifo_depth * port->tx_fifo_width;
		/*
		 * Total polling iterations based on FIFO worth of bytes to be
		 * sent at current baud. Add a little fluff to the wait.
		 */
		timeout_us = ((fifo_bits * USEC_PER_SEC) / baud) + 500;
	}

	return !readl_poll_timeout_atomic(uport->membase + offset, reg,
			 (bool)(reg & field) == set, 10, timeout_us);
}

static void qcom_geni_serial_setup_tx(struct uart_port *uport, u32 xmit_size)
{
	u32 m_cmd;

	writel_relaxed(xmit_size, uport->membase + SE_UART_TX_TRANS_LEN);
	m_cmd = UART_START_TX << M_OPCODE_SHFT;
	writel(m_cmd, uport->membase + SE_GENI_M_CMD0);
}

static void qcom_geni_serial_poll_tx_done(struct uart_port *uport)
{
	int done;
	u32 irq_clear = M_CMD_DONE_EN;

	done = qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_DONE_EN, true);
	if (!done) {
		writel_relaxed(M_GENI_CMD_ABORT, uport->membase +
						SE_GENI_M_CMD_CTRL_REG);
		irq_clear |= M_CMD_ABORT_EN;
		qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
							M_CMD_ABORT_EN, true);
	}
	writel_relaxed(irq_clear, uport->membase + SE_GENI_M_IRQ_CLEAR);
}

static void qcom_geni_serial_abort_rx(struct uart_port *uport)
{
	u32 irq_clear = S_CMD_DONE_EN | S_CMD_ABORT_EN;

	writel(S_GENI_CMD_ABORT, uport->membase + SE_GENI_S_CMD_CTRL_REG);
	qcom_geni_serial_poll_bit(uport, SE_GENI_S_CMD_CTRL_REG,
					S_GENI_CMD_ABORT, false);
	writel_relaxed(irq_clear, uport->membase + SE_GENI_S_IRQ_CLEAR);
	writel_relaxed(FORCE_DEFAULT, uport->membase + GENI_FORCE_DEFAULT_REG);
}

#ifdef CONFIG_CONSOLE_POLL
static int qcom_geni_serial_get_char(struct uart_port *uport)
{
	u32 rx_fifo;
	u32 status;

	status = readl_relaxed(uport->membase + SE_GENI_M_IRQ_STATUS);
	writel_relaxed(status, uport->membase + SE_GENI_M_IRQ_CLEAR);

	status = readl_relaxed(uport->membase + SE_GENI_S_IRQ_STATUS);
	writel_relaxed(status, uport->membase + SE_GENI_S_IRQ_CLEAR);

	/*
	 * Ensure the writes to clear interrupts is not re-ordered after
	 * reading the data.
	 */
	mb();

	status = readl_relaxed(uport->membase + SE_GENI_RX_FIFO_STATUS);
	if (!(status & RX_FIFO_WC_MSK))
		return NO_POLL_CHAR;

	rx_fifo = readl(uport->membase + SE_GENI_RX_FIFOn);
	return rx_fifo & 0xff;
}

static void qcom_geni_serial_poll_put_char(struct uart_port *uport,
							unsigned char c)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	writel_relaxed(port->tx_wm, uport->membase + SE_GENI_TX_WATERMARK_REG);
	qcom_geni_serial_setup_tx(uport, 1);
	WARN_ON(!qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_TX_FIFO_WATERMARK_EN, true));
	writel_relaxed(c, uport->membase + SE_GENI_TX_FIFOn);
	writel_relaxed(M_TX_FIFO_WATERMARK_EN, uport->membase +
							SE_GENI_M_IRQ_CLEAR);
	qcom_geni_serial_poll_tx_done(uport);
}
#endif

#ifdef CONFIG_SERIAL_QCOM_GENI_CONSOLE
static void qcom_geni_serial_wr_char(struct uart_port *uport, int ch)
{
	writel_relaxed(ch, uport->membase + SE_GENI_TX_FIFOn);
}

static void
__qcom_geni_serial_console_write(struct uart_port *uport, const char *s,
				 unsigned int count)
{
	int i;
	u32 bytes_to_send = count;

	for (i = 0; i < count; i++) {
		if (s[i] == '\n')
			bytes_to_send++;
	}

	writel_relaxed(DEF_TX_WM, uport->membase + SE_GENI_TX_WATERMARK_REG);
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
		chars_to_write = min_t(size_t, (size_t)(count - i), avail / 2);
		uart_console_write(uport, s + i, chars_to_write,
						qcom_geni_serial_wr_char);
		writel_relaxed(M_TX_FIFO_WATERMARK_EN, uport->membase +
							SE_GENI_M_IRQ_CLEAR);
		i += chars_to_write;
	}
	qcom_geni_serial_poll_tx_done(uport);
}

static void qcom_geni_serial_console_write(struct console *co, const char *s,
			      unsigned int count)
{
	struct uart_port *uport;
	struct qcom_geni_serial_port *port;
	bool locked = true;
	unsigned long flags;

	WARN_ON(co->index < 0 || co->index >= GENI_UART_CONS_PORTS);

	port = get_port_from_line(co->index);
	if (IS_ERR(port))
		return;

	uport = &port->uport;
	if (oops_in_progress)
		locked = spin_trylock_irqsave(&uport->lock, flags);
	else
		spin_lock_irqsave(&uport->lock, flags);

	/* Cancel the current write to log the fault */
	if (!locked) {
		geni_se_cancel_m_cmd(&port->se);
		if (!qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_CANCEL_EN, true)) {
			geni_se_abort_m_cmd(&port->se);
			qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
							M_CMD_ABORT_EN, true);
			writel_relaxed(M_CMD_ABORT_EN, uport->membase +
							SE_GENI_M_IRQ_CLEAR);
		}
		writel_relaxed(M_CMD_CANCEL_EN, uport->membase +
							SE_GENI_M_IRQ_CLEAR);
	}

	__qcom_geni_serial_console_write(uport, s, count);
	if (locked)
		spin_unlock_irqrestore(&uport->lock, flags);
}

static int handle_rx_console(struct uart_port *uport, u32 bytes, bool drop)
{
	u32 i;
	unsigned char buf[sizeof(u32)];
	struct tty_port *tport;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	tport = &uport->state->port;
	for (i = 0; i < bytes; ) {
		int c;
		int chunk = min_t(int, bytes - i, port->rx_bytes_pw);

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

			sysrq = uart_handle_sysrq_char(uport, buf[c]);
			if (!sysrq)
				tty_insert_flip_char(tport, buf[c], TTY_NORMAL);
		}
	}
	if (!drop)
		tty_flip_buffer_push(tport);
	return 0;
}
#else
static int handle_rx_console(struct uart_port *uport, u32 bytes, bool drop)
{
	return -EPERM;
}

#endif /* CONFIG_SERIAL_QCOM_GENI_CONSOLE */

static void qcom_geni_serial_start_tx(struct uart_port *uport)
{
	u32 irq_en;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);
	u32 status;

	if (port->xfer_mode == GENI_SE_FIFO) {
		status = readl_relaxed(uport->membase + SE_GENI_STATUS);
		if (status & M_GENI_CMD_ACTIVE)
			return;

		if (!qcom_geni_serial_tx_empty(uport))
			return;

		/*
		 * Ensure writing to IRQ_EN & watermark registers are not
		 * re-ordered before checking the status of the Serial
		 * Engine and TX FIFO
		 */
		mb();

		irq_en = readl_relaxed(uport->membase +	SE_GENI_M_IRQ_EN);
		irq_en |= M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN;

		writel_relaxed(port->tx_wm, uport->membase +
						SE_GENI_TX_WATERMARK_REG);
		writel_relaxed(irq_en, uport->membase +	SE_GENI_M_IRQ_EN);
	}
}

static void qcom_geni_serial_stop_tx(struct uart_port *uport)
{
	u32 irq_en;
	u32 status;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	irq_en = readl_relaxed(uport->membase + SE_GENI_M_IRQ_EN);
	irq_en &= ~M_CMD_DONE_EN;
	if (port->xfer_mode == GENI_SE_FIFO) {
		irq_en &= ~M_TX_FIFO_WATERMARK_EN;
		writel_relaxed(0, uport->membase +
				     SE_GENI_TX_WATERMARK_REG);
	}
	port->xmit_size = 0;
	writel_relaxed(irq_en, uport->membase + SE_GENI_M_IRQ_EN);
	status = readl_relaxed(uport->membase + SE_GENI_STATUS);
	/* Possible stop tx is called multiple times. */
	if (!(status & M_GENI_CMD_ACTIVE))
		return;

	/*
	 * Ensure cancel command write is not re-ordered before checking
	 * the status of the Primary Sequencer.
	 */
	mb();

	geni_se_cancel_m_cmd(&port->se);
	if (!qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_CANCEL_EN, true)) {
		geni_se_abort_m_cmd(&port->se);
		qcom_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_CMD_ABORT_EN, true);
		writel_relaxed(M_CMD_ABORT_EN, uport->membase +
							SE_GENI_M_IRQ_CLEAR);
	}
	writel_relaxed(M_CMD_CANCEL_EN, uport->membase + SE_GENI_M_IRQ_CLEAR);
}

static void qcom_geni_serial_start_rx(struct uart_port *uport)
{
	u32 irq_en;
	u32 status;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	status = readl_relaxed(uport->membase + SE_GENI_STATUS);
	if (status & S_GENI_CMD_ACTIVE)
		qcom_geni_serial_stop_rx(uport);

	/*
	 * Ensure setup command write is not re-ordered before checking
	 * the status of the Secondary Sequencer.
	 */
	mb();

	geni_se_setup_s_cmd(&port->se, UART_START_READ, 0);

	if (port->xfer_mode == GENI_SE_FIFO) {
		irq_en = readl_relaxed(uport->membase + SE_GENI_S_IRQ_EN);
		irq_en |= S_RX_FIFO_WATERMARK_EN | S_RX_FIFO_LAST_EN;
		writel_relaxed(irq_en, uport->membase + SE_GENI_S_IRQ_EN);

		irq_en = readl_relaxed(uport->membase + SE_GENI_M_IRQ_EN);
		irq_en |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
		writel_relaxed(irq_en, uport->membase + SE_GENI_M_IRQ_EN);
	}
}

static void qcom_geni_serial_stop_rx(struct uart_port *uport)
{
	u32 irq_en;
	u32 status;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);
	u32 irq_clear = S_CMD_DONE_EN;

	if (port->xfer_mode == GENI_SE_FIFO) {
		irq_en = readl_relaxed(uport->membase + SE_GENI_S_IRQ_EN);
		irq_en &= ~(S_RX_FIFO_WATERMARK_EN | S_RX_FIFO_LAST_EN);
		writel_relaxed(irq_en, uport->membase + SE_GENI_S_IRQ_EN);

		irq_en = readl_relaxed(uport->membase + SE_GENI_M_IRQ_EN);
		irq_en &= ~(M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
		writel_relaxed(irq_en, uport->membase + SE_GENI_M_IRQ_EN);
	}

	status = readl_relaxed(uport->membase + SE_GENI_STATUS);
	/* Possible stop rx is called multiple times. */
	if (!(status & S_GENI_CMD_ACTIVE))
		return;

	/*
	 * Ensure cancel command write is not re-ordered before checking
	 * the status of the Secondary Sequencer.
	 */
	mb();

	geni_se_cancel_s_cmd(&port->se);
	qcom_geni_serial_poll_bit(uport, SE_GENI_S_CMD_CTRL_REG,
					S_GENI_CMD_CANCEL, false);
	status = readl_relaxed(uport->membase + SE_GENI_STATUS);
	writel_relaxed(irq_clear, uport->membase + SE_GENI_S_IRQ_CLEAR);
	if (status & S_GENI_CMD_ACTIVE)
		qcom_geni_serial_abort_rx(uport);
}

static void qcom_geni_serial_handle_rx(struct uart_port *uport, bool drop)
{
	u32 status;
	u32 word_cnt;
	u32 last_word_byte_cnt;
	u32 last_word_partial;
	u32 total_bytes;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	status = readl_relaxed(uport->membase +	SE_GENI_RX_FIFO_STATUS);
	word_cnt = status & RX_FIFO_WC_MSK;
	last_word_partial = status & RX_LAST;
	last_word_byte_cnt = (status & RX_LAST_BYTE_VALID_MSK) >>
						RX_LAST_BYTE_VALID_SHFT;

	if (!word_cnt)
		return;
	total_bytes = port->rx_bytes_pw * (word_cnt - 1);
	if (last_word_partial && last_word_byte_cnt)
		total_bytes += last_word_byte_cnt;
	else
		total_bytes += port->rx_bytes_pw;
	port->handle_rx(uport, total_bytes, drop);
}

static void qcom_geni_serial_handle_tx(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);
	struct circ_buf *xmit = &uport->state->xmit;
	size_t avail;
	size_t remaining;
	int i;
	u32 status;
	unsigned int chunk;
	int tail;

	chunk = uart_circ_chars_pending(xmit);
	status = readl_relaxed(uport->membase + SE_GENI_TX_FIFO_STATUS);
	/* Both FIFO and framework buffer are drained */
	if (chunk == port->xmit_size && !status) {
		port->xmit_size = 0;
		uart_circ_clear(xmit);
		qcom_geni_serial_stop_tx(uport);
		goto out_write_wakeup;
	}
	chunk -= port->xmit_size;

	avail = (port->tx_fifo_depth - port->tx_wm) * port->tx_bytes_pw;
	tail = (xmit->tail + port->xmit_size) & (UART_XMIT_SIZE - 1);
	if (chunk > (UART_XMIT_SIZE - tail))
		chunk = UART_XMIT_SIZE - tail;
	if (chunk > avail)
		chunk = avail;

	if (!chunk)
		goto out_write_wakeup;

	qcom_geni_serial_setup_tx(uport, chunk);

	remaining = chunk;
	for (i = 0; i < chunk; ) {
		unsigned int tx_bytes;
		unsigned int buf = 0;
		int c;

		tx_bytes = min_t(size_t, remaining, (size_t)port->tx_bytes_pw);
		for (c = 0; c < tx_bytes ; c++)
			buf |= (xmit->buf[tail + c] << (c * BITS_PER_BYTE));

		writel_relaxed(buf, uport->membase + SE_GENI_TX_FIFOn);

		i += tx_bytes;
		tail = (tail + tx_bytes) & (UART_XMIT_SIZE - 1);
		uport->icount.tx += tx_bytes;
		remaining -= tx_bytes;
	}
	qcom_geni_serial_poll_tx_done(uport);
	port->xmit_size += chunk;
out_write_wakeup:
	uart_write_wakeup(uport);
}

static irqreturn_t qcom_geni_serial_isr(int isr, void *dev)
{
	unsigned int m_irq_status;
	unsigned int s_irq_status;
	struct uart_port *uport = dev;
	unsigned long flags;
	unsigned int m_irq_en;
	bool drop_rx = false;
	struct tty_port *tport = &uport->state->port;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	if (uport->suspended)
		return IRQ_HANDLED;

	spin_lock_irqsave(&uport->lock, flags);
	m_irq_status = readl_relaxed(uport->membase + SE_GENI_M_IRQ_STATUS);
	s_irq_status = readl_relaxed(uport->membase + SE_GENI_S_IRQ_STATUS);
	m_irq_en = readl_relaxed(uport->membase + SE_GENI_M_IRQ_EN);
	writel_relaxed(m_irq_status, uport->membase + SE_GENI_M_IRQ_CLEAR);
	writel_relaxed(s_irq_status, uport->membase + SE_GENI_S_IRQ_CLEAR);

	if (WARN_ON(m_irq_status & M_ILLEGAL_CMD_EN))
		goto out_unlock;

	if (s_irq_status & S_RX_FIFO_WR_ERR_EN) {
		uport->icount.overrun++;
		tty_insert_flip_char(tport, 0, TTY_OVERRUN);
	}

	if (m_irq_status & (M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN) &&
	    m_irq_en & (M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN))
		qcom_geni_serial_handle_tx(uport);

	if (s_irq_status & S_GP_IRQ_0_EN || s_irq_status & S_GP_IRQ_1_EN) {
		if (s_irq_status & S_GP_IRQ_0_EN)
			uport->icount.parity++;
		drop_rx = true;
	} else if (s_irq_status & S_GP_IRQ_2_EN ||
					s_irq_status & S_GP_IRQ_3_EN) {
		uport->icount.brk++;
		port->brk = true;
	}

	if (s_irq_status & S_RX_FIFO_WATERMARK_EN ||
					s_irq_status & S_RX_FIFO_LAST_EN)
		qcom_geni_serial_handle_rx(uport, drop_rx);

out_unlock:
	spin_unlock_irqrestore(&uport->lock, flags);
	return IRQ_HANDLED;
}

static int get_tx_fifo_size(struct qcom_geni_serial_port *port)
{
	struct uart_port *uport;

	if (!port)
		return -ENODEV;

	uport = &port->uport;
	port->tx_fifo_depth = geni_se_get_tx_fifo_depth(&port->se);
	port->tx_fifo_width = geni_se_get_tx_fifo_width(&port->se);
	port->rx_fifo_depth = geni_se_get_rx_fifo_depth(&port->se);
	uport->fifosize =
		(port->tx_fifo_depth * port->tx_fifo_width) / BITS_PER_BYTE;
	return 0;
}

static void set_rfr_wm(struct qcom_geni_serial_port *port)
{
	/*
	 * Set RFR (Flow off) to FIFO_DEPTH - 2.
	 * RX WM level at 10% RX_FIFO_DEPTH.
	 * TX WM level at 10% TX_FIFO_DEPTH.
	 */
	port->rx_rfr = port->rx_fifo_depth - 2;
	port->rx_wm = UART_CONSOLE_RX_WM;
	port->tx_wm = DEF_TX_WM;
}

static void qcom_geni_serial_shutdown(struct uart_port *uport)
{
	unsigned long flags;

	/* Stop the console before stopping the current tx */
	console_stop(uport->cons);

	disable_irq(uport->irq);
	free_irq(uport->irq, uport);
	spin_lock_irqsave(&uport->lock, flags);
	qcom_geni_serial_stop_tx(uport);
	qcom_geni_serial_stop_rx(uport);
	spin_unlock_irqrestore(&uport->lock, flags);
}

static int qcom_geni_serial_port_setup(struct uart_port *uport)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);
	unsigned int rxstale = DEFAULT_BITS_PER_CHAR * STALE_TIMEOUT;

	set_rfr_wm(port);
	writel_relaxed(rxstale, uport->membase + SE_UART_RX_STALE_CNT);
	/*
	 * Make an unconditional cancel on the main sequencer to reset
	 * it else we could end up in data loss scenarios.
	 */
	port->xfer_mode = GENI_SE_FIFO;
	qcom_geni_serial_poll_tx_done(uport);
	geni_se_config_packing(&port->se, BITS_PER_BYTE, port->tx_bytes_pw,
						false, true, false);
	geni_se_config_packing(&port->se, BITS_PER_BYTE, port->rx_bytes_pw,
						false, false, true);
	geni_se_init(&port->se, port->rx_wm, port->rx_rfr);
	geni_se_select_mode(&port->se, port->xfer_mode);
	port->setup = true;
	return 0;
}

static int qcom_geni_serial_startup(struct uart_port *uport)
{
	int ret;
	u32 proto;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	scnprintf(port->name, sizeof(port->name),
		  "qcom_serial_geni%d",	uport->line);

	proto = geni_se_read_proto(&port->se);
	if (proto != GENI_SE_UART) {
		dev_err(uport->dev, "Invalid FW loaded, proto: %d\n", proto);
		return -ENXIO;
	}

	get_tx_fifo_size(port);
	if (!port->setup) {
		ret = qcom_geni_serial_port_setup(uport);
		if (ret)
			return ret;
	}

	ret = request_irq(uport->irq, qcom_geni_serial_isr, IRQF_TRIGGER_HIGH,
							port->name, uport);
	if (ret)
		dev_err(uport->dev, "Failed to get IRQ ret %d\n", ret);
	return ret;
}

static unsigned long get_clk_cfg(unsigned long clk_freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(root_freq); i++) {
		if (!(root_freq[i] % clk_freq))
			return root_freq[i];
	}
	return 0;
}

static unsigned long get_clk_div_rate(unsigned int baud, unsigned int *clk_div)
{
	unsigned long ser_clk;
	unsigned long desired_clk;

	desired_clk = baud * UART_OVERSAMPLING;
	ser_clk = get_clk_cfg(desired_clk);
	if (!ser_clk) {
		pr_err("%s: Can't find matching DFS entry for baud %d\n",
								__func__, baud);
		return ser_clk;
	}

	*clk_div = ser_clk / desired_clk;
	return ser_clk;
}

static void qcom_geni_serial_set_termios(struct uart_port *uport,
				struct ktermios *termios, struct ktermios *old)
{
	unsigned int baud;
	unsigned int bits_per_char;
	unsigned int tx_trans_cfg;
	unsigned int tx_parity_cfg;
	unsigned int rx_trans_cfg;
	unsigned int rx_parity_cfg;
	unsigned int stop_bit_len;
	unsigned int clk_div;
	unsigned long ser_clk_cfg;
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);
	unsigned long clk_rate;

	qcom_geni_serial_stop_rx(uport);
	/* baud rate */
	baud = uart_get_baud_rate(uport, termios, old, 300, 4000000);
	port->baud = baud;
	clk_rate = get_clk_div_rate(baud, &clk_div);
	if (!clk_rate)
		goto out_restart_rx;

	uport->uartclk = clk_rate;
	clk_set_rate(port->se.clk, clk_rate);
	ser_clk_cfg = SER_CLK_EN;
	ser_clk_cfg |= clk_div << CLK_DIV_SHFT;

	/* parity */
	tx_trans_cfg = readl_relaxed(uport->membase + SE_UART_TX_TRANS_CFG);
	tx_parity_cfg = readl_relaxed(uport->membase + SE_UART_TX_PARITY_CFG);
	rx_trans_cfg = readl_relaxed(uport->membase + SE_UART_RX_TRANS_CFG);
	rx_parity_cfg = readl_relaxed(uport->membase + SE_UART_RX_PARITY_CFG);
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
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		bits_per_char = 5;
		break;
	case CS6:
		bits_per_char = 6;
		break;
	case CS7:
		bits_per_char = 7;
		break;
	case CS8:
	default:
		bits_per_char = 8;
		break;
	}

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

	if (baud)
		uart_update_timeout(uport, termios->c_cflag, baud);

	writel_relaxed(tx_trans_cfg, uport->membase + SE_UART_TX_TRANS_CFG);
	writel_relaxed(tx_parity_cfg, uport->membase + SE_UART_TX_PARITY_CFG);
	writel_relaxed(rx_trans_cfg, uport->membase + SE_UART_RX_TRANS_CFG);
	writel_relaxed(rx_parity_cfg, uport->membase + SE_UART_RX_PARITY_CFG);
	writel_relaxed(bits_per_char, uport->membase + SE_UART_TX_WORD_LEN);
	writel_relaxed(bits_per_char, uport->membase + SE_UART_RX_WORD_LEN);
	writel_relaxed(stop_bit_len, uport->membase + SE_UART_TX_STOP_BIT_LEN);
	writel_relaxed(ser_clk_cfg, uport->membase + GENI_SER_M_CLK_CFG);
	writel_relaxed(ser_clk_cfg, uport->membase + GENI_SER_S_CLK_CFG);
out_restart_rx:
	qcom_geni_serial_start_rx(uport);
}

static unsigned int qcom_geni_serial_tx_empty(struct uart_port *uport)
{
	return !readl_relaxed(uport->membase + SE_GENI_TX_FIFO_STATUS);
}

#ifdef CONFIG_SERIAL_QCOM_GENI_CONSOLE
static int __init qcom_geni_console_setup(struct console *co, char *options)
{
	struct uart_port *uport;
	struct qcom_geni_serial_port *port;
	int baud;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (co->index >= GENI_UART_CONS_PORTS  || co->index < 0)
		return -ENXIO;

	port = get_port_from_line(co->index);
	if (IS_ERR(port)) {
		pr_err("Invalid line %d(%d)\n", co->index, (int)PTR_ERR(port));
		return PTR_ERR(port);
	}

	uport = &port->uport;

	if (unlikely(!uport->membase))
		return -ENXIO;

	if (geni_se_resources_on(&port->se)) {
		dev_err(port->se.dev, "Error turning on resources\n");
		return -ENXIO;
	}

	if (unlikely(geni_se_read_proto(&port->se) != GENI_SE_UART)) {
		geni_se_resources_off(&port->se);
		return -ENXIO;
	}

	if (!port->setup) {
		port->tx_bytes_pw = 1;
		port->rx_bytes_pw = RX_BYTES_PW;
		qcom_geni_serial_stop_rx(uport);
		qcom_geni_serial_port_setup(uport);
	}

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(uport, co, baud, parity, bits, flow);
}

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

static void qcom_geni_serial_cons_pm(struct uart_port *uport,
		unsigned int new_state, unsigned int old_state)
{
	struct qcom_geni_serial_port *port = to_dev_port(uport, uport);

	if (unlikely(!uart_console(uport)))
		return;

	if (new_state == UART_PM_STATE_ON && old_state == UART_PM_STATE_OFF)
		geni_se_resources_on(&port->se);
	else if (new_state == UART_PM_STATE_OFF &&
			old_state == UART_PM_STATE_ON)
		geni_se_resources_off(&port->se);
}

static const struct uart_ops qcom_geni_console_pops = {
	.tx_empty = qcom_geni_serial_tx_empty,
	.stop_tx = qcom_geni_serial_stop_tx,
	.start_tx = qcom_geni_serial_start_tx,
	.stop_rx = qcom_geni_serial_stop_rx,
	.set_termios = qcom_geni_serial_set_termios,
	.startup = qcom_geni_serial_startup,
	.request_port = qcom_geni_serial_request_port,
	.config_port = qcom_geni_serial_config_port,
	.shutdown = qcom_geni_serial_shutdown,
	.type = qcom_geni_serial_get_type,
	.set_mctrl = qcom_geni_cons_set_mctrl,
	.get_mctrl = qcom_geni_cons_get_mctrl,
#ifdef CONFIG_CONSOLE_POLL
	.poll_get_char	= qcom_geni_serial_get_char,
	.poll_put_char	= qcom_geni_serial_poll_put_char,
#endif
	.pm = qcom_geni_serial_cons_pm,
};

static int qcom_geni_serial_probe(struct platform_device *pdev)
{
	int ret = 0;
	int line = -1;
	struct qcom_geni_serial_port *port;
	struct uart_port *uport;
	struct resource *res;

	if (pdev->dev.of_node)
		line = of_alias_get_id(pdev->dev.of_node, "serial");
	else
		line = pdev->id;

	if (line < 0 || line >= GENI_UART_CONS_PORTS)
		return -ENXIO;
	port = get_port_from_line(line);
	if (IS_ERR(port)) {
		ret = PTR_ERR(port);
		dev_err(&pdev->dev, "Invalid line %d(%d)\n", line, ret);
		return ret;
	}

	uport = &port->uport;
	/* Don't allow 2 drivers to access the same port */
	if (uport->private_data)
		return -ENODEV;

	uport->dev = &pdev->dev;
	port->se.dev = &pdev->dev;
	port->se.wrapper = dev_get_drvdata(pdev->dev.parent);
	port->se.clk = devm_clk_get(&pdev->dev, "se");
	if (IS_ERR(port->se.clk)) {
		ret = PTR_ERR(port->se.clk);
		dev_err(&pdev->dev, "Err getting SE Core clk %d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	uport->mapbase = res->start;

	port->tx_fifo_depth = DEF_FIFO_DEPTH_WORDS;
	port->rx_fifo_depth = DEF_FIFO_DEPTH_WORDS;
	port->tx_fifo_width = DEF_FIFO_WIDTH_BITS;

	uport->irq = platform_get_irq(pdev, 0);
	if (uport->irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ %d\n", uport->irq);
		return uport->irq;
	}

	uport->private_data = &qcom_geni_console_driver;
	platform_set_drvdata(pdev, port);
	port->handle_rx = handle_rx_console;
	port->setup = false;
	return uart_add_one_port(&qcom_geni_console_driver, uport);
}

static int qcom_geni_serial_remove(struct platform_device *pdev)
{
	struct qcom_geni_serial_port *port = platform_get_drvdata(pdev);
	struct uart_driver *drv = port->uport.private_data;

	uart_remove_one_port(drv, &port->uport);
	return 0;
}

static int __maybe_unused qcom_geni_serial_sys_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_geni_serial_port *port = platform_get_drvdata(pdev);
	struct uart_port *uport = &port->uport;

	uart_suspend_port(uport->private_data, uport);
	return 0;
}

static int __maybe_unused qcom_geni_serial_sys_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct qcom_geni_serial_port *port = platform_get_drvdata(pdev);
	struct uart_port *uport = &port->uport;

	if (console_suspend_enabled && uport->suspended) {
		uart_resume_port(uport->private_data, uport);
		disable_irq(uport->irq);
	}
	return 0;
}

static const struct dev_pm_ops qcom_geni_serial_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(qcom_geni_serial_sys_suspend_noirq,
					qcom_geni_serial_sys_resume_noirq)
};

static const struct of_device_id qcom_geni_serial_match_table[] = {
	{ .compatible = "qcom,geni-debug-uart", },
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

	qcom_geni_console_port.uport.iotype = UPIO_MEM;
	qcom_geni_console_port.uport.ops = &qcom_geni_console_pops;
	qcom_geni_console_port.uport.flags = UPF_BOOT_AUTOCONF;
	qcom_geni_console_port.uport.line = 0;

	ret = console_register(&qcom_geni_console_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&qcom_geni_serial_platform_driver);
	if (ret)
		console_unregister(&qcom_geni_console_driver);
	return ret;
}
module_init(qcom_geni_serial_init);

static void __exit qcom_geni_serial_exit(void)
{
	platform_driver_unregister(&qcom_geni_serial_platform_driver);
	console_unregister(&qcom_geni_console_driver);
}
module_exit(qcom_geni_serial_exit);

MODULE_DESCRIPTION("Serial driver for GENI based QUP cores");
MODULE_LICENSE("GPL v2");
