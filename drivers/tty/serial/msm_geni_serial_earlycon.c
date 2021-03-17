// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/serial_core.h>

#define SE_GENI_DMA_MODE_EN             0x258
#define SE_UART_TX_TRANS_CFG		0x25C
#define SE_UART_TX_WORD_LEN		0x268
#define SE_UART_TX_STOP_BIT_LEN		0x26C
#define SE_UART_TX_TRANS_LEN		0x270
#define SE_UART_TX_PARITY_CFG		0x2A4
/* SE_UART_TRANS_CFG */
#define UART_CTS_MASK		BIT(1)
/* UART M_CMD OP codes */
#define UART_START_TX		0x1

#define UART_OVERSAMPLING	32
#define DEF_FIFO_DEPTH_WORDS	16
#define DEF_TX_WM		2
#define DEF_FIFO_WIDTH_BITS	32

#define GENI_FORCE_DEFAULT_REG		0x20
#define GENI_OUTPUT_CTRL		0x24
#define GENI_CGC_CTRL			0x28
#define GENI_SER_M_CLK_CFG		0x48
#define GENI_FW_REVISION_RO		0x68

#define SE_GENI_TX_PACKING_CFG0		0x260
#define SE_GENI_TX_PACKING_CFG1		0x264
#define SE_GENI_M_CMD0			0x600
#define SE_GENI_M_CMD_CTRL_REG		0x604
#define SE_GENI_M_IRQ_STATUS		0x610
#define SE_GENI_M_IRQ_EN		0x614
#define SE_GENI_M_IRQ_CLEAR		0x618
#define SE_GENI_TX_FIFOn		0x700
#define SE_GENI_TX_WATERMARK_REG	0x80C

#define SE_IRQ_EN			0xE1C
#define SE_HW_PARAM_0			0xE24
#define SE_HW_PARAM_1			0xE28

/* GENI_OUTPUT_CTRL fields */
#define DEFAULT_IO_OUTPUT_CTRL_MSK	GENMASK(6, 0)

/* GENI_FORCE_DEFAULT_REG fields */
#define FORCE_DEFAULT	BIT(0)

/* GENI_CGC_CTRL fields */
#define CFG_AHB_CLK_CGC_ON		BIT(0)
#define CFG_AHB_WR_ACLK_CGC_ON		BIT(1)
#define DATA_AHB_CLK_CGC_ON		BIT(2)
#define SCLK_CGC_ON			BIT(3)
#define TX_CLK_CGC_ON			BIT(4)
#define RX_CLK_CGC_ON			BIT(5)
#define EXT_CLK_CGC_ON			BIT(6)
#define PROG_RAM_HCLK_OFF		BIT(8)
#define PROG_RAM_SCLK_OFF		BIT(9)
#define DEFAULT_CGC_EN			GENMASK(6, 0)

/* GENI_STATUS fields */
#define M_GENI_CMD_ACTIVE		BIT(0)

/* GENI_SER_M_CLK_CFG/GENI_SER_S_CLK_CFG */
#define SER_CLK_EN			BIT(0)
#define CLK_DIV_MSK			GENMASK(15, 4)
#define CLK_DIV_SHFT			4

/* CLK_CTRL_RO fields */

/* FIFO_IF_DISABLE_RO fields */
#define FIFO_IF_DISABLE			BIT(0)

/* FW_REVISION_RO fields */
#define FW_REV_PROTOCOL_MSK	GENMASK(15, 8)
#define FW_REV_PROTOCOL_SHFT	8
#define FW_REV_VERSION_MSK	GENMASK(7, 0)

/* GENI_CLK_SEL fields */
#define CLK_SEL_MSK		GENMASK(2, 0)

/* SE_GENI_DMA_MODE_EN */
#define GENI_DMA_MODE_EN	BIT(0)

/* GENI_M_CMD0 fields */
#define M_OPCODE_MSK		GENMASK(31, 27)
#define M_OPCODE_SHFT		27
#define M_PARAMS_MSK		GENMASK(26, 0)

/* GENI_M_CMD_CTRL_REG */
#define M_GENI_CMD_CANCEL	BIT(2)
#define M_GENI_CMD_ABORT	BIT(1)
#define M_GENI_DISABLE		BIT(0)

/* GENI_M_IRQ_EN fields */
#define M_CMD_DONE_EN		BIT(0)
#define M_CMD_OVERRUN_EN	BIT(1)
#define M_ILLEGAL_CMD_EN	BIT(2)
#define M_CMD_FAILURE_EN	BIT(3)
#define M_CMD_CANCEL_EN		BIT(4)
#define M_CMD_ABORT_EN		BIT(5)
#define M_TIMESTAMP_EN		BIT(6)
#define M_GP_SYNC_IRQ_0_EN	BIT(8)
#define M_IO_DATA_DEASSERT_EN	BIT(22)
#define M_IO_DATA_ASSERT_EN	BIT(23)
#define M_TX_FIFO_RD_ERR_EN	BIT(28)
#define M_TX_FIFO_WR_ERR_EN	BIT(29)
#define M_TX_FIFO_WATERMARK_EN	BIT(30)
#define M_SEC_IRQ_EN		BIT(31)
#define M_COMMON_GENI_M_IRQ_EN	(GENMASK(6, 1) | \
				M_IO_DATA_DEASSERT_EN | \
				M_IO_DATA_ASSERT_EN | M_TX_FIFO_RD_ERR_EN | \
				M_TX_FIFO_WR_ERR_EN)

/* GENI_TX_FIFO_STATUS fields */
#define TX_FIFO_WC		GENMASK(27, 0)

/* SE_IRQ_EN fields */
#define GENI_M_IRQ_EN		BIT(2)

#define UART_PROTOCOL	2

static int get_se_proto_earlycon(void __iomem *base)
{
	int proto;

	proto = ((readl_relaxed(base + GENI_FW_REVISION_RO)
			& FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT);
	return proto;
}

static void se_get_packing_config_earlycon(int bpw, int pack_words,
	bool msb_to_lsb, unsigned long *cfg0, unsigned long *cfg1)
{
	u32 cfg[4] = {0};
	int len, i;
	int temp_bpw = bpw;
	int idx_start = (msb_to_lsb ? (bpw - 1) : 0);
	int idx_delta = (msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE);
	int ceil_bpw = ((bpw & (BITS_PER_BYTE - 1)) ?
			((bpw & ~(BITS_PER_BYTE - 1)) + BITS_PER_BYTE) : bpw);
	int iter = (ceil_bpw * pack_words) >> 3;
	int idx = idx_start;

	if (iter <= 0 || iter > 4) {
		*cfg0 = 0;
		*cfg1 = 0;
		return;
	}

	for (i = 0; i < iter; i++) {
		len = (temp_bpw < BITS_PER_BYTE) ?
				(temp_bpw - 1) : BITS_PER_BYTE - 1;
		cfg[i] = ((idx << 5) | (msb_to_lsb << 4) | (len << 1));
		idx = ((temp_bpw - BITS_PER_BYTE) <= 0) ?
				((i + 1) * BITS_PER_BYTE) + idx_start :
				idx + idx_delta;
		temp_bpw = ((temp_bpw - BITS_PER_BYTE) <= 0) ?
				bpw : (temp_bpw - BITS_PER_BYTE);
	}
	cfg[iter - 1] |= 1;
	*cfg0 = cfg[0] | (cfg[1] << 10);
	*cfg1 = cfg[2] | (cfg[3] << 10);
}

static void se_io_init_earlycon(void __iomem *base)
{
	u32 io_op_ctrl;
	u32 geni_cgc_ctrl;

	geni_cgc_ctrl = readl_relaxed(base + GENI_CGC_CTRL);
	geni_cgc_ctrl |= DEFAULT_CGC_EN;
	io_op_ctrl = DEFAULT_IO_OUTPUT_CTRL_MSK;
	writel_relaxed(geni_cgc_ctrl, base + GENI_CGC_CTRL);

	writel_relaxed(io_op_ctrl, base + GENI_OUTPUT_CTRL);
	writel_relaxed(FORCE_DEFAULT, base + GENI_FORCE_DEFAULT_REG);
}

static void geni_se_select_fifo_mode_earlycon(void __iomem *base)
{
	u32 val;

	val = readl_relaxed(base + SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	writel_relaxed(val, base + SE_GENI_M_IRQ_EN);
}

static void msm_geni_serial_wr_char(struct uart_port *uport, int ch)
{
	writel_relaxed(ch, uport->membase + SE_GENI_TX_FIFOn);
}

static int msm_geni_serial_poll_bit(struct uart_port *uport,
					int offset, int bit_field, bool set)
{
	int iter = 0;
	bool met = false, cond = false;
	unsigned int reg, total_iter = 1000;

	while (iter < total_iter) {
		reg = readl_relaxed(uport->membase + offset);
		cond = reg & bit_field;
		if (cond == set) {
			met = true;
			break;
		}
		udelay(10);
		iter++;
	}
	return met;
}

static void msm_geni_serial_poll_abort_tx(struct uart_port *uport)
{
	int done = 0;
	u32 irq_clear = M_CMD_DONE_EN;

	done = msm_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
					M_CMD_DONE_EN, true);
	if (!done) {
		writel_relaxed(M_GENI_CMD_ABORT,
				uport->membase + SE_GENI_M_CMD_CTRL_REG);
		irq_clear |= M_CMD_ABORT_EN;
		msm_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
					M_CMD_ABORT_EN, true);
	}
	writel_relaxed(irq_clear, uport->membase + SE_GENI_M_IRQ_CLEAR);
}

static void msm_geni_serial_setup_tx(struct uart_port *uport,
				unsigned int xmit_size)
{
	u32 m_cmd = 0;

	writel_relaxed(xmit_size, uport->membase + SE_UART_TX_TRANS_LEN);
	m_cmd |= (UART_START_TX << M_OPCODE_SHFT);
	writel_relaxed(m_cmd, uport->membase + SE_GENI_M_CMD0);
}

static void
__msm_geni_serial_console_write(struct uart_port *uport, const char *s,
				unsigned int count)
{
	int new_line = 0;
	int i;
	int bytes_to_send = count;
	int fifo_depth = DEF_FIFO_DEPTH_WORDS;
	int tx_wm = DEF_TX_WM;

	for (i = 0; i < count; i++) {
		if (s[i] == '\n')
			new_line++;
	}

	bytes_to_send += new_line;
	writel_relaxed(tx_wm, uport->membase + SE_GENI_TX_WATERMARK_REG);
	msm_geni_serial_setup_tx(uport, bytes_to_send);
	i = 0;
	while (i < count) {
		u32 chars_to_write = 0;
		u32 avail_fifo_bytes = (fifo_depth - tx_wm);
		/*
		 * If the WM bit never set, then the Tx state machine is not
		 * in a valid state, so break, cancel/abort any existing
		 * command. Unfortunately the current data being written is
		 * lost.
		 */
		while (!msm_geni_serial_poll_bit(uport, SE_GENI_M_IRQ_STATUS,
						M_TX_FIFO_WATERMARK_EN, true))
			break;
		chars_to_write = min((unsigned int)(count - i),
					avail_fifo_bytes);
		if ((chars_to_write << 1) > avail_fifo_bytes)
			chars_to_write = (avail_fifo_bytes >> 1);
		uart_console_write(uport, (s + i), chars_to_write,
					msm_geni_serial_wr_char);
		writel_relaxed(M_TX_FIFO_WATERMARK_EN,
				uport->membase + SE_GENI_M_IRQ_CLEAR);
		i += chars_to_write;
	}
	msm_geni_serial_poll_abort_tx(uport);
}

static void
msm_geni_serial_early_console_write(struct console *con, const char *s,
					unsigned int n)
{
	struct earlycon_device *dev = con->data;

	__msm_geni_serial_console_write(&dev->port, s, n);
}

static int __init
msm_geni_serial_earlycon_setup(struct earlycon_device *dev,
				const char *opt)
{
	int ret = 0;
	u32 tx_trans_cfg = 0;
	u32 tx_parity_cfg = 0;
	u32 stop_bit = 0;
	u32 bits_per_char = 0;
	unsigned long cfg0, cfg1;
	struct uart_port *uport = &dev->port;

	if (!uport->membase) {
		ret = -ENOMEM;
		goto exit;
	}

	if (get_se_proto_earlycon(uport->membase) != UART_PROTOCOL) {
		ret = -ENXIO;
		goto exit;
	}

	/*
	 * Ignore Flow control.
	 * Disable Tx Parity.
	 * Don't check Parity during Rx.
	 * Disable Rx Parity.
	 * n = 8.
	 * Stop bit = 0.
	 * Stale timeout in bit-time (3 chars worth).
	 */
	tx_trans_cfg |= UART_CTS_MASK;
	tx_parity_cfg = 0;
	bits_per_char = 0x8;
	stop_bit = 0;

	msm_geni_serial_poll_abort_tx(uport);

	se_get_packing_config_earlycon(8, 1, false, &cfg0, &cfg1);

	se_io_init_earlycon(uport->membase);

	geni_se_select_fifo_mode_earlycon(uport->membase);
	writel_relaxed(cfg0, uport->membase + SE_GENI_TX_PACKING_CFG0);
	writel_relaxed(cfg1, uport->membase + SE_GENI_TX_PACKING_CFG1);
	writel_relaxed(tx_trans_cfg, uport->membase + SE_UART_TX_TRANS_CFG);
	writel_relaxed(tx_parity_cfg, uport->membase + SE_UART_TX_PARITY_CFG);
	writel_relaxed(bits_per_char, uport->membase + SE_UART_TX_WORD_LEN);
	writel_relaxed(stop_bit, uport->membase + SE_UART_TX_STOP_BIT_LEN);

	dev->con->write = msm_geni_serial_early_console_write;
	dev->con->setup = NULL;
exit:
	return ret;
}

OF_EARLYCON_DECLARE(msm_geni_serial, "qcom,msm-geni-console",
			msm_geni_serial_earlycon_setup);
