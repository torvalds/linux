// SPDX-License-Identifier: (GPL-2.0)
/*
 * Microchip coreQSPI QSPI controller driver
 *
 * Copyright (C) 2018-2022 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Naga Sureshkumar Relli <nagasuresh.relli@microchip.com>
 *
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/*
 * QSPI Control register mask defines
 */
#define CONTROL_ENABLE		BIT(0)
#define CONTROL_MASTER		BIT(1)
#define CONTROL_XIP		BIT(2)
#define CONTROL_XIPADDR		BIT(3)
#define CONTROL_CLKIDLE		BIT(10)
#define CONTROL_SAMPLE_MASK	GENMASK(12, 11)
#define CONTROL_MODE0		BIT(13)
#define CONTROL_MODE12_MASK	GENMASK(15, 14)
#define CONTROL_MODE12_EX_RO	BIT(14)
#define CONTROL_MODE12_EX_RW	BIT(15)
#define CONTROL_MODE12_FULL	GENMASK(15, 14)
#define CONTROL_FLAGSX4		BIT(16)
#define CONTROL_CLKRATE_MASK	GENMASK(27, 24)
#define CONTROL_CLKRATE_SHIFT	24

/*
 * QSPI Frames register mask defines
 */
#define FRAMES_TOTALBYTES_MASK	GENMASK(15, 0)
#define FRAMES_CMDBYTES_MASK	GENMASK(24, 16)
#define FRAMES_CMDBYTES_SHIFT	16
#define FRAMES_SHIFT		25
#define FRAMES_IDLE_MASK	GENMASK(29, 26)
#define FRAMES_IDLE_SHIFT	26
#define FRAMES_FLAGBYTE		BIT(30)
#define FRAMES_FLAGWORD		BIT(31)

/*
 * QSPI Interrupt Enable register mask defines
 */
#define IEN_TXDONE		BIT(0)
#define IEN_RXDONE		BIT(1)
#define IEN_RXAVAILABLE		BIT(2)
#define IEN_TXAVAILABLE		BIT(3)
#define IEN_RXFIFOEMPTY		BIT(4)
#define IEN_TXFIFOFULL		BIT(5)

/*
 * QSPI Status register mask defines
 */
#define STATUS_TXDONE		BIT(0)
#define STATUS_RXDONE		BIT(1)
#define STATUS_RXAVAILABLE	BIT(2)
#define STATUS_TXAVAILABLE	BIT(3)
#define STATUS_RXFIFOEMPTY	BIT(4)
#define STATUS_TXFIFOFULL	BIT(5)
#define STATUS_READY		BIT(7)
#define STATUS_FLAGSX4		BIT(8)
#define STATUS_MASK		GENMASK(8, 0)

#define BYTESUPPER_MASK		GENMASK(31, 16)
#define BYTESLOWER_MASK		GENMASK(15, 0)

#define MAX_DIVIDER		16
#define MIN_DIVIDER		0
#define MAX_DATA_CMD_LEN	256

/* QSPI ready time out value */
#define TIMEOUT_MS		500

/*
 * QSPI Register offsets.
 */
#define REG_CONTROL		(0x00)
#define REG_FRAMES		(0x04)
#define REG_IEN			(0x0c)
#define REG_STATUS		(0x10)
#define REG_DIRECT_ACCESS	(0x14)
#define REG_UPPER_ACCESS	(0x18)
#define REG_RX_DATA		(0x40)
#define REG_TX_DATA		(0x44)
#define REG_X4_RX_DATA		(0x48)
#define REG_X4_TX_DATA		(0x4c)
#define REG_FRAMESUP		(0x50)

/**
 * struct mchp_coreqspi - Defines qspi driver instance
 * @regs:              Virtual address of the QSPI controller registers
 * @clk:               QSPI Operating clock
 * @data_completion:   completion structure
 * @op_lock:           lock access to the device
 * @txbuf:             TX buffer
 * @rxbuf:             RX buffer
 * @irq:               IRQ number
 * @tx_len:            Number of bytes left to transfer
 * @rx_len:            Number of bytes left to receive
 */
struct mchp_coreqspi {
	void __iomem *regs;
	struct clk *clk;
	struct completion data_completion;
	struct mutex op_lock; /* lock access to the device */
	u8 *txbuf;
	u8 *rxbuf;
	int irq;
	int tx_len;
	int rx_len;
};

static int mchp_coreqspi_set_mode(struct mchp_coreqspi *qspi, const struct spi_mem_op *op)
{
	u32 control = readl_relaxed(qspi->regs + REG_CONTROL);

	/*
	 * The operating mode can be configured based on the command that needs to be send.
	 * bits[15:14]: Sets whether multiple bit SPI operates in normal, extended or full modes.
	 *		00: Normal (single DQ0 TX and single DQ1 RX lines)
	 *		01: Extended RO (command and address bytes on DQ0 only)
	 *		10: Extended RW (command byte on DQ0 only)
	 *		11: Full. (command and address are on all DQ lines)
	 * bit[13]:	Sets whether multiple bit SPI uses 2 or 4 bits of data
	 *		0: 2-bits (BSPI)
	 *		1: 4-bits (QSPI)
	 */
	if (op->data.buswidth == 4 || op->data.buswidth == 2) {
		control &= ~CONTROL_MODE12_MASK;
		if (op->cmd.buswidth == 1 && (op->addr.buswidth == 1 || op->addr.buswidth == 0))
			control |= CONTROL_MODE12_EX_RO;
		else if (op->cmd.buswidth == 1)
			control |= CONTROL_MODE12_EX_RW;
		else
			control |= CONTROL_MODE12_FULL;

		control |= CONTROL_MODE0;
	} else {
		control &= ~(CONTROL_MODE12_MASK |
			     CONTROL_MODE0);
	}

	writel_relaxed(control, qspi->regs + REG_CONTROL);

	return 0;
}

static inline void mchp_coreqspi_read_op(struct mchp_coreqspi *qspi)
{
	u32 control, data;

	if (!qspi->rx_len)
		return;

	control = readl_relaxed(qspi->regs + REG_CONTROL);

	/*
	 * Read 4-bytes from the SPI FIFO in single transaction and then read
	 * the reamaining data byte wise.
	 */
	control |= CONTROL_FLAGSX4;
	writel_relaxed(control, qspi->regs + REG_CONTROL);

	while (qspi->rx_len >= 4) {
		while (readl_relaxed(qspi->regs + REG_STATUS) & STATUS_RXFIFOEMPTY)
			;
		data = readl_relaxed(qspi->regs + REG_X4_RX_DATA);
		*(u32 *)qspi->rxbuf = data;
		qspi->rxbuf += 4;
		qspi->rx_len -= 4;
	}

	control &= ~CONTROL_FLAGSX4;
	writel_relaxed(control, qspi->regs + REG_CONTROL);

	while (qspi->rx_len--) {
		while (readl_relaxed(qspi->regs + REG_STATUS) & STATUS_RXFIFOEMPTY)
			;
		data = readl_relaxed(qspi->regs + REG_RX_DATA);
		*qspi->rxbuf++ = (data & 0xFF);
	}
}

static inline void mchp_coreqspi_write_op(struct mchp_coreqspi *qspi, bool word)
{
	u32 control, data;

	control = readl_relaxed(qspi->regs + REG_CONTROL);
	control |= CONTROL_FLAGSX4;
	writel_relaxed(control, qspi->regs + REG_CONTROL);

	while (qspi->tx_len >= 4) {
		while (readl_relaxed(qspi->regs + REG_STATUS) & STATUS_TXFIFOFULL)
			;
		data = *(u32 *)qspi->txbuf;
		qspi->txbuf += 4;
		qspi->tx_len -= 4;
		writel_relaxed(data, qspi->regs + REG_X4_TX_DATA);
	}

	control &= ~CONTROL_FLAGSX4;
	writel_relaxed(control, qspi->regs + REG_CONTROL);

	while (qspi->tx_len--) {
		while (readl_relaxed(qspi->regs + REG_STATUS) & STATUS_TXFIFOFULL)
			;
		data =  *qspi->txbuf++;
		writel_relaxed(data, qspi->regs + REG_TX_DATA);
	}
}

static void mchp_coreqspi_enable_ints(struct mchp_coreqspi *qspi)
{
	u32 mask = IEN_TXDONE |
		   IEN_RXDONE |
		   IEN_RXAVAILABLE;

	writel_relaxed(mask, qspi->regs + REG_IEN);
}

static void mchp_coreqspi_disable_ints(struct mchp_coreqspi *qspi)
{
	writel_relaxed(0, qspi->regs + REG_IEN);
}

static irqreturn_t mchp_coreqspi_isr(int irq, void *dev_id)
{
	struct mchp_coreqspi *qspi = (struct mchp_coreqspi *)dev_id;
	irqreturn_t ret = IRQ_NONE;
	int intfield = readl_relaxed(qspi->regs + REG_STATUS) & STATUS_MASK;

	if (intfield == 0)
		return ret;

	if (intfield & IEN_TXDONE) {
		writel_relaxed(IEN_TXDONE, qspi->regs + REG_STATUS);
		ret = IRQ_HANDLED;
	}

	if (intfield & IEN_RXAVAILABLE) {
		writel_relaxed(IEN_RXAVAILABLE, qspi->regs + REG_STATUS);
		mchp_coreqspi_read_op(qspi);
		ret = IRQ_HANDLED;
	}

	if (intfield & IEN_RXDONE) {
		writel_relaxed(IEN_RXDONE, qspi->regs + REG_STATUS);
		complete(&qspi->data_completion);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int mchp_coreqspi_setup_clock(struct mchp_coreqspi *qspi, struct spi_device *spi)
{
	unsigned long clk_hz;
	u32 control, baud_rate_val = 0;

	clk_hz = clk_get_rate(qspi->clk);
	if (!clk_hz)
		return -EINVAL;

	baud_rate_val = DIV_ROUND_UP(clk_hz, 2 * spi->max_speed_hz);
	if (baud_rate_val > MAX_DIVIDER || baud_rate_val < MIN_DIVIDER) {
		dev_err(&spi->dev,
			"could not configure the clock for spi clock %d Hz & system clock %ld Hz\n",
			spi->max_speed_hz, clk_hz);
		return -EINVAL;
	}

	control = readl_relaxed(qspi->regs + REG_CONTROL);
	control |= baud_rate_val << CONTROL_CLKRATE_SHIFT;
	writel_relaxed(control, qspi->regs + REG_CONTROL);
	control = readl_relaxed(qspi->regs + REG_CONTROL);

	if ((spi->mode & SPI_CPOL) && (spi->mode & SPI_CPHA))
		control |= CONTROL_CLKIDLE;
	else
		control &= ~CONTROL_CLKIDLE;

	writel_relaxed(control, qspi->regs + REG_CONTROL);

	return 0;
}

static int mchp_coreqspi_setup_op(struct spi_device *spi_dev)
{
	struct spi_controller *ctlr = spi_dev->master;
	struct mchp_coreqspi *qspi = spi_controller_get_devdata(ctlr);
	u32 control = readl_relaxed(qspi->regs + REG_CONTROL);

	control |= (CONTROL_MASTER | CONTROL_ENABLE);
	control &= ~CONTROL_CLKIDLE;
	writel_relaxed(control, qspi->regs + REG_CONTROL);

	return 0;
}

static inline void mchp_coreqspi_config_op(struct mchp_coreqspi *qspi, const struct spi_mem_op *op)
{
	u32 idle_cycles = 0;
	int total_bytes, cmd_bytes, frames, ctrl;

	cmd_bytes = op->cmd.nbytes + op->addr.nbytes;
	total_bytes = cmd_bytes + op->data.nbytes;

	/*
	 * As per the coreQSPI IP spec,the number of command and data bytes are
	 * controlled by the frames register for each SPI sequence. This supports
	 * the SPI flash memory read and writes sequences as below. so configure
	 * the cmd and total bytes accordingly.
	 * ---------------------------------------------------------------------
	 * TOTAL BYTES  |  CMD BYTES | What happens                             |
	 * ______________________________________________________________________
	 *              |            |                                          |
	 *     1        |   1        | The SPI core will transmit a single byte |
	 *              |            | and receive data is discarded            |
	 *              |            |                                          |
	 *     1        |   0        | The SPI core will transmit a single byte |
	 *              |            | and return a single byte                 |
	 *              |            |                                          |
	 *     10       |   4        | The SPI core will transmit 4 command     |
	 *              |            | bytes discarding the receive data and    |
	 *              |            | transmits 6 dummy bytes returning the 6  |
	 *              |            | received bytes and return a single byte  |
	 *              |            |                                          |
	 *     10       |   10       | The SPI core will transmit 10 command    |
	 *              |            |                                          |
	 *     10       |    0       | The SPI core will transmit 10 command    |
	 *              |            | bytes and returning 10 received bytes    |
	 * ______________________________________________________________________
	 */
	if (!(op->data.dir == SPI_MEM_DATA_IN))
		cmd_bytes = total_bytes;

	frames = total_bytes & BYTESUPPER_MASK;
	writel_relaxed(frames, qspi->regs + REG_FRAMESUP);
	frames = total_bytes & BYTESLOWER_MASK;
	frames |= cmd_bytes << FRAMES_CMDBYTES_SHIFT;

	if (op->dummy.buswidth)
		idle_cycles = op->dummy.nbytes * 8 / op->dummy.buswidth;

	frames |= idle_cycles << FRAMES_IDLE_SHIFT;
	ctrl = readl_relaxed(qspi->regs + REG_CONTROL);

	if (ctrl & CONTROL_MODE12_MASK)
		frames |= (1 << FRAMES_SHIFT);

	frames |= FRAMES_FLAGWORD;
	writel_relaxed(frames, qspi->regs + REG_FRAMES);
}

static int mchp_qspi_wait_for_ready(struct spi_mem *mem)
{
	struct mchp_coreqspi *qspi = spi_controller_get_devdata
				    (mem->spi->master);
	u32 status;
	int ret;

	ret = readl_poll_timeout(qspi->regs + REG_STATUS, status,
				 (status & STATUS_READY), 0,
				 TIMEOUT_MS);
	if (ret) {
		dev_err(&mem->spi->dev,
			"Timeout waiting on QSPI ready.\n");
		return -ETIMEDOUT;
	}

	return ret;
}

static int mchp_coreqspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct mchp_coreqspi *qspi = spi_controller_get_devdata
				    (mem->spi->master);
	u32 address = op->addr.val;
	u8 opcode = op->cmd.opcode;
	u8 opaddr[5];
	int err, i;

	mutex_lock(&qspi->op_lock);
	err = mchp_qspi_wait_for_ready(mem);
	if (err)
		goto error;

	err = mchp_coreqspi_setup_clock(qspi, mem->spi);
	if (err)
		goto error;

	err = mchp_coreqspi_set_mode(qspi, op);
	if (err)
		goto error;

	reinit_completion(&qspi->data_completion);
	mchp_coreqspi_config_op(qspi, op);
	if (op->cmd.opcode) {
		qspi->txbuf = &opcode;
		qspi->rxbuf = NULL;
		qspi->tx_len = op->cmd.nbytes;
		qspi->rx_len = 0;
		mchp_coreqspi_write_op(qspi, false);
	}

	qspi->txbuf = &opaddr[0];
	if (op->addr.nbytes) {
		for (i = 0; i < op->addr.nbytes; i++)
			qspi->txbuf[i] = address >> (8 * (op->addr.nbytes - i - 1));

		qspi->rxbuf = NULL;
		qspi->tx_len = op->addr.nbytes;
		qspi->rx_len = 0;
		mchp_coreqspi_write_op(qspi, false);
	}

	if (op->data.nbytes) {
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			qspi->txbuf = (u8 *)op->data.buf.out;
			qspi->rxbuf = NULL;
			qspi->rx_len = 0;
			qspi->tx_len = op->data.nbytes;
			mchp_coreqspi_write_op(qspi, true);
		} else {
			qspi->txbuf = NULL;
			qspi->rxbuf = (u8 *)op->data.buf.in;
			qspi->rx_len = op->data.nbytes;
			qspi->tx_len = 0;
		}
	}

	mchp_coreqspi_enable_ints(qspi);

	if (!wait_for_completion_timeout(&qspi->data_completion, msecs_to_jiffies(1000)))
		err = -ETIMEDOUT;

error:
	mutex_unlock(&qspi->op_lock);
	mchp_coreqspi_disable_ints(qspi);

	return err;
}

static bool mchp_coreqspi_supports_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if ((op->data.buswidth == 4 || op->data.buswidth == 2) &&
	    (op->cmd.buswidth == 1 && (op->addr.buswidth == 1 || op->addr.buswidth == 0))) {
		/*
		 * If the command and address are on DQ0 only, then this
		 * controller doesn't support sending data on dual and
		 * quad lines. but it supports reading data on dual and
		 * quad lines with same configuration as command and
		 * address on DQ0.
		 * i.e. The control register[15:13] :EX_RO(read only) is
		 * meant only for the command and address are on DQ0 but
		 * not to write data, it is just to read.
		 * Ex: 0x34h is Quad Load Program Data which is not
		 * supported. Then the spi-mem layer will iterate over
		 * each command and it will chose the supported one.
		 */
		if (op->data.dir == SPI_MEM_DATA_OUT)
			return false;
	}

	return true;
}

static int mchp_coreqspi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	if (op->data.dir == SPI_MEM_DATA_OUT || op->data.dir == SPI_MEM_DATA_IN) {
		if (op->data.nbytes > MAX_DATA_CMD_LEN)
			op->data.nbytes = MAX_DATA_CMD_LEN;
	}

	return 0;
}

static const struct spi_controller_mem_ops mchp_coreqspi_mem_ops = {
	.adjust_op_size = mchp_coreqspi_adjust_op_size,
	.supports_op = mchp_coreqspi_supports_op,
	.exec_op = mchp_coreqspi_exec_op,
};

static int mchp_coreqspi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct mchp_coreqspi *qspi;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	ctlr = devm_spi_alloc_master(&pdev->dev, sizeof(*qspi));
	if (!ctlr)
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "unable to allocate master for QSPI controller\n");

	qspi = spi_controller_get_devdata(ctlr);
	platform_set_drvdata(pdev, qspi);

	qspi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qspi->regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(qspi->regs),
				     "failed to map registers\n");

	qspi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(qspi->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(qspi->clk),
				     "could not get clock\n");

	ret = clk_prepare_enable(qspi->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to enable clock\n");

	init_completion(&qspi->data_completion);
	mutex_init(&qspi->op_lock);

	qspi->irq = platform_get_irq(pdev, 0);
	if (qspi->irq < 0) {
		ret = qspi->irq;
		goto out;
	}

	ret = devm_request_irq(&pdev->dev, qspi->irq, mchp_coreqspi_isr,
			       IRQF_SHARED, pdev->name, qspi);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed %d\n", ret);
		goto out;
	}

	ctlr->bits_per_word_mask = SPI_BPW_MASK(8);
	ctlr->mem_ops = &mchp_coreqspi_mem_ops;
	ctlr->setup = mchp_coreqspi_setup_op;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_DUAL | SPI_RX_QUAD |
			  SPI_TX_DUAL | SPI_TX_QUAD;
	ctlr->dev.of_node = np;

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret) {
		dev_err_probe(&pdev->dev, ret,
			      "spi_register_controller failed\n");
		goto out;
	}

	return 0;

out:
	clk_disable_unprepare(qspi->clk);

	return ret;
}

static void mchp_coreqspi_remove(struct platform_device *pdev)
{
	struct mchp_coreqspi *qspi = platform_get_drvdata(pdev);
	u32 control = readl_relaxed(qspi->regs + REG_CONTROL);

	mchp_coreqspi_disable_ints(qspi);
	control &= ~CONTROL_ENABLE;
	writel_relaxed(control, qspi->regs + REG_CONTROL);
	clk_disable_unprepare(qspi->clk);
}

static const struct of_device_id mchp_coreqspi_of_match[] = {
	{ .compatible = "microchip,coreqspi-rtl-v2" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mchp_coreqspi_of_match);

static struct platform_driver mchp_coreqspi_driver = {
	.probe = mchp_coreqspi_probe,
	.driver = {
		.name = "microchip,coreqspi",
		.of_match_table = mchp_coreqspi_of_match,
	},
	.remove_new = mchp_coreqspi_remove,
};
module_platform_driver(mchp_coreqspi_driver);

MODULE_AUTHOR("Naga Sureshkumar Relli <nagasuresh.relli@microchip.com");
MODULE_DESCRIPTION("Microchip coreQSPI QSPI controller driver");
MODULE_LICENSE("GPL");
