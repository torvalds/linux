// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018, The Linux foundation. All rights reserved.

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>


#define QSPI_NUM_CS		2
#define QSPI_BYTES_PER_WORD	4

#define MSTR_CONFIG		0x0000
#define FULL_CYCLE_MODE		BIT(3)
#define FB_CLK_EN		BIT(4)
#define PIN_HOLDN		BIT(6)
#define PIN_WPN			BIT(7)
#define DMA_ENABLE		BIT(8)
#define BIG_ENDIAN_MODE		BIT(9)
#define SPI_MODE_MSK		0xc00
#define SPI_MODE_SHFT		10
#define CHIP_SELECT_NUM		BIT(12)
#define SBL_EN			BIT(13)
#define LPA_BASE_MSK		0x3c000
#define LPA_BASE_SHFT		14
#define TX_DATA_DELAY_MSK	0xc0000
#define TX_DATA_DELAY_SHFT	18
#define TX_CLK_DELAY_MSK	0x300000
#define TX_CLK_DELAY_SHFT	20
#define TX_CS_N_DELAY_MSK	0xc00000
#define TX_CS_N_DELAY_SHFT	22
#define TX_DATA_OE_DELAY_MSK	0x3000000
#define TX_DATA_OE_DELAY_SHFT	24

#define AHB_MASTER_CFG				0x0004
#define HMEM_TYPE_START_MID_TRANS_MSK		0x7
#define HMEM_TYPE_START_MID_TRANS_SHFT		0
#define HMEM_TYPE_LAST_TRANS_MSK		0x38
#define HMEM_TYPE_LAST_TRANS_SHFT		3
#define USE_HMEMTYPE_LAST_ON_DESC_OR_CHAIN_MSK	0xc0
#define USE_HMEMTYPE_LAST_ON_DESC_OR_CHAIN_SHFT	6
#define HMEMTYPE_READ_TRANS_MSK			0x700
#define HMEMTYPE_READ_TRANS_SHFT		8
#define HSHARED					BIT(11)
#define HINNERSHARED				BIT(12)

#define MSTR_INT_EN		0x000C
#define MSTR_INT_STATUS		0x0010
#define RESP_FIFO_UNDERRUN	BIT(0)
#define RESP_FIFO_NOT_EMPTY	BIT(1)
#define RESP_FIFO_RDY		BIT(2)
#define HRESP_FROM_NOC_ERR	BIT(3)
#define WR_FIFO_EMPTY		BIT(9)
#define WR_FIFO_FULL		BIT(10)
#define WR_FIFO_OVERRUN		BIT(11)
#define TRANSACTION_DONE	BIT(16)
#define QSPI_ERR_IRQS		(RESP_FIFO_UNDERRUN | HRESP_FROM_NOC_ERR | \
				 WR_FIFO_OVERRUN)
#define QSPI_ALL_IRQS		(QSPI_ERR_IRQS | RESP_FIFO_RDY | \
				 WR_FIFO_EMPTY | WR_FIFO_FULL | \
				 TRANSACTION_DONE)

#define PIO_XFER_CTRL		0x0014
#define REQUEST_COUNT_MSK	0xffff

#define PIO_XFER_CFG		0x0018
#define TRANSFER_DIRECTION	BIT(0)
#define MULTI_IO_MODE_MSK	0xe
#define MULTI_IO_MODE_SHFT	1
#define TRANSFER_FRAGMENT	BIT(8)
#define SDR_1BIT		1
#define SDR_2BIT		2
#define SDR_4BIT		3
#define DDR_1BIT		5
#define DDR_2BIT		6
#define DDR_4BIT		7
#define DMA_DESC_SINGLE_SPI	1
#define DMA_DESC_DUAL_SPI	2
#define DMA_DESC_QUAD_SPI	3

#define PIO_XFER_STATUS		0x001c
#define WR_FIFO_BYTES_MSK	0xffff0000
#define WR_FIFO_BYTES_SHFT	16

#define PIO_DATAOUT_1B		0x0020
#define PIO_DATAOUT_4B		0x0024

#define RD_FIFO_CFG		0x0028
#define CONTINUOUS_MODE		BIT(0)

#define RD_FIFO_STATUS	0x002c
#define FIFO_EMPTY	BIT(11)
#define WR_CNTS_MSK	0x7f0
#define WR_CNTS_SHFT	4
#define RDY_64BYTE	BIT(3)
#define RDY_32BYTE	BIT(2)
#define RDY_16BYTE	BIT(1)
#define FIFO_RDY	BIT(0)

#define RD_FIFO_RESET		0x0030
#define RESET_FIFO		BIT(0)

#define CUR_MEM_ADDR		0x0048
#define HW_VERSION		0x004c
#define RD_FIFO			0x0050
#define SAMPLING_CLK_CFG	0x0090
#define SAMPLING_CLK_STATUS	0x0094


enum qspi_dir {
	QSPI_READ,
	QSPI_WRITE,
};

struct qspi_xfer {
	union {
		const void *tx_buf;
		void *rx_buf;
	};
	unsigned int rem_bytes;
	unsigned int buswidth;
	enum qspi_dir dir;
	bool is_last;
};

enum qspi_clocks {
	QSPI_CLK_CORE,
	QSPI_CLK_IFACE,
	QSPI_NUM_CLKS
};

struct qcom_qspi {
	void __iomem *base;
	struct device *dev;
	struct clk_bulk_data *clks;
	struct qspi_xfer xfer;
	struct icc_path *icc_path_cpu_to_qspi;
	unsigned long last_speed;
	/* Lock to protect data accessed by IRQs */
	spinlock_t lock;
};

static u32 qspi_buswidth_to_iomode(struct qcom_qspi *ctrl,
				   unsigned int buswidth)
{
	switch (buswidth) {
	case 1:
		return SDR_1BIT << MULTI_IO_MODE_SHFT;
	case 2:
		return SDR_2BIT << MULTI_IO_MODE_SHFT;
	case 4:
		return SDR_4BIT << MULTI_IO_MODE_SHFT;
	default:
		dev_warn_once(ctrl->dev,
				"Unexpected bus width: %u\n", buswidth);
		return SDR_1BIT << MULTI_IO_MODE_SHFT;
	}
}

static void qcom_qspi_pio_xfer_cfg(struct qcom_qspi *ctrl)
{
	u32 pio_xfer_cfg;
	const struct qspi_xfer *xfer;

	xfer = &ctrl->xfer;
	pio_xfer_cfg = readl(ctrl->base + PIO_XFER_CFG);
	pio_xfer_cfg &= ~TRANSFER_DIRECTION;
	pio_xfer_cfg |= xfer->dir;
	if (xfer->is_last)
		pio_xfer_cfg &= ~TRANSFER_FRAGMENT;
	else
		pio_xfer_cfg |= TRANSFER_FRAGMENT;
	pio_xfer_cfg &= ~MULTI_IO_MODE_MSK;
	pio_xfer_cfg |= qspi_buswidth_to_iomode(ctrl, xfer->buswidth);

	writel(pio_xfer_cfg, ctrl->base + PIO_XFER_CFG);
}

static void qcom_qspi_pio_xfer_ctrl(struct qcom_qspi *ctrl)
{
	u32 pio_xfer_ctrl;

	pio_xfer_ctrl = readl(ctrl->base + PIO_XFER_CTRL);
	pio_xfer_ctrl &= ~REQUEST_COUNT_MSK;
	pio_xfer_ctrl |= ctrl->xfer.rem_bytes;
	writel(pio_xfer_ctrl, ctrl->base + PIO_XFER_CTRL);
}

static void qcom_qspi_pio_xfer(struct qcom_qspi *ctrl)
{
	u32 ints;

	qcom_qspi_pio_xfer_cfg(ctrl);

	/* Ack any previous interrupts that might be hanging around */
	writel(QSPI_ALL_IRQS, ctrl->base + MSTR_INT_STATUS);

	/* Setup new interrupts */
	if (ctrl->xfer.dir == QSPI_WRITE)
		ints = QSPI_ERR_IRQS | WR_FIFO_EMPTY;
	else
		ints = QSPI_ERR_IRQS | RESP_FIFO_RDY;
	writel(ints, ctrl->base + MSTR_INT_EN);

	/* Kick off the transfer */
	qcom_qspi_pio_xfer_ctrl(ctrl);
}

static void qcom_qspi_handle_err(struct spi_master *master,
				 struct spi_message *msg)
{
	struct qcom_qspi *ctrl = spi_master_get_devdata(master);
	unsigned long flags;

	spin_lock_irqsave(&ctrl->lock, flags);
	writel(0, ctrl->base + MSTR_INT_EN);
	ctrl->xfer.rem_bytes = 0;
	spin_unlock_irqrestore(&ctrl->lock, flags);
}

static int qcom_qspi_set_speed(struct qcom_qspi *ctrl, unsigned long speed_hz)
{
	int ret;
	unsigned int avg_bw_cpu;

	if (speed_hz == ctrl->last_speed)
		return 0;

	/* In regular operation (SBL_EN=1) core must be 4x transfer clock */
	ret = dev_pm_opp_set_rate(ctrl->dev, speed_hz * 4);
	if (ret) {
		dev_err(ctrl->dev, "Failed to set core clk %d\n", ret);
		return ret;
	}

	/*
	 * Set BW quota for CPU as driver supports FIFO mode only.
	 * We don't have explicit peak requirement so keep it equal to avg_bw.
	 */
	avg_bw_cpu = Bps_to_icc(speed_hz);
	ret = icc_set_bw(ctrl->icc_path_cpu_to_qspi, avg_bw_cpu, avg_bw_cpu);
	if (ret) {
		dev_err(ctrl->dev, "%s: ICC BW voting failed for cpu: %d\n",
			__func__, ret);
		return ret;
	}

	ctrl->last_speed = speed_hz;

	return 0;
}

static int qcom_qspi_transfer_one(struct spi_master *master,
				  struct spi_device *slv,
				  struct spi_transfer *xfer)
{
	struct qcom_qspi *ctrl = spi_master_get_devdata(master);
	int ret;
	unsigned long speed_hz;
	unsigned long flags;

	speed_hz = slv->max_speed_hz;
	if (xfer->speed_hz)
		speed_hz = xfer->speed_hz;

	ret = qcom_qspi_set_speed(ctrl, speed_hz);
	if (ret)
		return ret;

	spin_lock_irqsave(&ctrl->lock, flags);

	/* We are half duplex, so either rx or tx will be set */
	if (xfer->rx_buf) {
		ctrl->xfer.dir = QSPI_READ;
		ctrl->xfer.buswidth = xfer->rx_nbits;
		ctrl->xfer.rx_buf = xfer->rx_buf;
	} else {
		ctrl->xfer.dir = QSPI_WRITE;
		ctrl->xfer.buswidth = xfer->tx_nbits;
		ctrl->xfer.tx_buf = xfer->tx_buf;
	}
	ctrl->xfer.is_last = list_is_last(&xfer->transfer_list,
					  &master->cur_msg->transfers);
	ctrl->xfer.rem_bytes = xfer->len;
	qcom_qspi_pio_xfer(ctrl);

	spin_unlock_irqrestore(&ctrl->lock, flags);

	/* We'll call spi_finalize_current_transfer() when done */
	return 1;
}

static int qcom_qspi_prepare_message(struct spi_master *master,
				     struct spi_message *message)
{
	u32 mstr_cfg;
	struct qcom_qspi *ctrl;
	int tx_data_oe_delay = 1;
	int tx_data_delay = 1;
	unsigned long flags;

	ctrl = spi_master_get_devdata(master);
	spin_lock_irqsave(&ctrl->lock, flags);

	mstr_cfg = readl(ctrl->base + MSTR_CONFIG);
	mstr_cfg &= ~CHIP_SELECT_NUM;
	if (spi_get_chipselect(message->spi, 0))
		mstr_cfg |= CHIP_SELECT_NUM;

	mstr_cfg |= FB_CLK_EN | PIN_WPN | PIN_HOLDN | SBL_EN | FULL_CYCLE_MODE;
	mstr_cfg &= ~(SPI_MODE_MSK | TX_DATA_OE_DELAY_MSK | TX_DATA_DELAY_MSK);
	mstr_cfg |= message->spi->mode << SPI_MODE_SHFT;
	mstr_cfg |= tx_data_oe_delay << TX_DATA_OE_DELAY_SHFT;
	mstr_cfg |= tx_data_delay << TX_DATA_DELAY_SHFT;
	mstr_cfg &= ~DMA_ENABLE;

	writel(mstr_cfg, ctrl->base + MSTR_CONFIG);
	spin_unlock_irqrestore(&ctrl->lock, flags);

	return 0;
}

static irqreturn_t pio_read(struct qcom_qspi *ctrl)
{
	u32 rd_fifo_status;
	u32 rd_fifo;
	unsigned int wr_cnts;
	unsigned int bytes_to_read;
	unsigned int words_to_read;
	u32 *word_buf;
	u8 *byte_buf;
	int i;

	rd_fifo_status = readl(ctrl->base + RD_FIFO_STATUS);

	if (!(rd_fifo_status & FIFO_RDY)) {
		dev_dbg(ctrl->dev, "Spurious IRQ %#x\n", rd_fifo_status);
		return IRQ_NONE;
	}

	wr_cnts = (rd_fifo_status & WR_CNTS_MSK) >> WR_CNTS_SHFT;
	wr_cnts = min(wr_cnts, ctrl->xfer.rem_bytes);

	words_to_read = wr_cnts / QSPI_BYTES_PER_WORD;
	bytes_to_read = wr_cnts % QSPI_BYTES_PER_WORD;

	if (words_to_read) {
		word_buf = ctrl->xfer.rx_buf;
		ctrl->xfer.rem_bytes -= words_to_read * QSPI_BYTES_PER_WORD;
		ioread32_rep(ctrl->base + RD_FIFO, word_buf, words_to_read);
		ctrl->xfer.rx_buf = word_buf + words_to_read;
	}

	if (bytes_to_read) {
		byte_buf = ctrl->xfer.rx_buf;
		rd_fifo = readl(ctrl->base + RD_FIFO);
		ctrl->xfer.rem_bytes -= bytes_to_read;
		for (i = 0; i < bytes_to_read; i++)
			*byte_buf++ = rd_fifo >> (i * BITS_PER_BYTE);
		ctrl->xfer.rx_buf = byte_buf;
	}

	return IRQ_HANDLED;
}

static irqreturn_t pio_write(struct qcom_qspi *ctrl)
{
	const void *xfer_buf = ctrl->xfer.tx_buf;
	const int *word_buf;
	const char *byte_buf;
	unsigned int wr_fifo_bytes;
	unsigned int wr_fifo_words;
	unsigned int wr_size;
	unsigned int rem_words;

	wr_fifo_bytes = readl(ctrl->base + PIO_XFER_STATUS);
	wr_fifo_bytes >>= WR_FIFO_BYTES_SHFT;

	if (ctrl->xfer.rem_bytes < QSPI_BYTES_PER_WORD) {
		/* Process the last 1-3 bytes */
		wr_size = min(wr_fifo_bytes, ctrl->xfer.rem_bytes);
		ctrl->xfer.rem_bytes -= wr_size;

		byte_buf = xfer_buf;
		while (wr_size--)
			writel(*byte_buf++,
			       ctrl->base + PIO_DATAOUT_1B);
		ctrl->xfer.tx_buf = byte_buf;
	} else {
		/*
		 * Process all the whole words; to keep things simple we'll
		 * just wait for the next interrupt to handle the last 1-3
		 * bytes if we don't have an even number of words.
		 */
		rem_words = ctrl->xfer.rem_bytes / QSPI_BYTES_PER_WORD;
		wr_fifo_words = wr_fifo_bytes / QSPI_BYTES_PER_WORD;

		wr_size = min(rem_words, wr_fifo_words);
		ctrl->xfer.rem_bytes -= wr_size * QSPI_BYTES_PER_WORD;

		word_buf = xfer_buf;
		iowrite32_rep(ctrl->base + PIO_DATAOUT_4B, word_buf, wr_size);
		ctrl->xfer.tx_buf = word_buf + wr_size;

	}

	return IRQ_HANDLED;
}

static irqreturn_t qcom_qspi_irq(int irq, void *dev_id)
{
	u32 int_status;
	struct qcom_qspi *ctrl = dev_id;
	irqreturn_t ret = IRQ_NONE;

	spin_lock(&ctrl->lock);

	int_status = readl(ctrl->base + MSTR_INT_STATUS);
	writel(int_status, ctrl->base + MSTR_INT_STATUS);

	if (ctrl->xfer.dir == QSPI_WRITE) {
		if (int_status & WR_FIFO_EMPTY)
			ret = pio_write(ctrl);
	} else {
		if (int_status & RESP_FIFO_RDY)
			ret = pio_read(ctrl);
	}

	if (int_status & QSPI_ERR_IRQS) {
		if (int_status & RESP_FIFO_UNDERRUN)
			dev_err(ctrl->dev, "IRQ error: FIFO underrun\n");
		if (int_status & WR_FIFO_OVERRUN)
			dev_err(ctrl->dev, "IRQ error: FIFO overrun\n");
		if (int_status & HRESP_FROM_NOC_ERR)
			dev_err(ctrl->dev, "IRQ error: NOC response error\n");
		ret = IRQ_HANDLED;
	}

	if (!ctrl->xfer.rem_bytes) {
		writel(0, ctrl->base + MSTR_INT_EN);
		spi_finalize_current_transfer(dev_get_drvdata(ctrl->dev));
	}

	spin_unlock(&ctrl->lock);
	return ret;
}

static int qcom_qspi_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev;
	struct spi_master *master;
	struct qcom_qspi *ctrl;

	dev = &pdev->dev;

	master = devm_spi_alloc_master(dev, sizeof(*ctrl));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	ctrl = spi_master_get_devdata(master);

	spin_lock_init(&ctrl->lock);
	ctrl->dev = dev;
	ctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	ctrl->clks = devm_kcalloc(dev, QSPI_NUM_CLKS,
				  sizeof(*ctrl->clks), GFP_KERNEL);
	if (!ctrl->clks)
		return -ENOMEM;

	ctrl->clks[QSPI_CLK_CORE].id = "core";
	ctrl->clks[QSPI_CLK_IFACE].id = "iface";
	ret = devm_clk_bulk_get(dev, QSPI_NUM_CLKS, ctrl->clks);
	if (ret)
		return ret;

	ctrl->icc_path_cpu_to_qspi = devm_of_icc_get(dev, "qspi-config");
	if (IS_ERR(ctrl->icc_path_cpu_to_qspi))
		return dev_err_probe(dev, PTR_ERR(ctrl->icc_path_cpu_to_qspi),
				     "Failed to get cpu path\n");

	/* Set BW vote for register access */
	ret = icc_set_bw(ctrl->icc_path_cpu_to_qspi, Bps_to_icc(1000),
				Bps_to_icc(1000));
	if (ret) {
		dev_err(ctrl->dev, "%s: ICC BW voting failed for cpu: %d\n",
				__func__, ret);
		return ret;
	}

	ret = icc_disable(ctrl->icc_path_cpu_to_qspi);
	if (ret) {
		dev_err(ctrl->dev, "%s: ICC disable failed for cpu: %d\n",
				__func__, ret);
		return ret;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	ret = devm_request_irq(dev, ret, qcom_qspi_irq, 0, dev_name(dev), ctrl);
	if (ret) {
		dev_err(dev, "Failed to request irq %d\n", ret);
		return ret;
	}

	master->max_speed_hz = 300000000;
	master->num_chipselect = QSPI_NUM_CS;
	master->bus_num = -1;
	master->dev.of_node = pdev->dev.of_node;
	master->mode_bits = SPI_MODE_0 |
			    SPI_TX_DUAL | SPI_RX_DUAL |
			    SPI_TX_QUAD | SPI_RX_QUAD;
	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->prepare_message = qcom_qspi_prepare_message;
	master->transfer_one = qcom_qspi_transfer_one;
	master->handle_err = qcom_qspi_handle_err;
	master->auto_runtime_pm = true;

	ret = devm_pm_opp_set_clkname(&pdev->dev, "core");
	if (ret)
		return ret;
	/* OPP table is optional */
	ret = devm_pm_opp_of_add_table(&pdev->dev);
	if (ret && ret != -ENODEV) {
		dev_err(&pdev->dev, "invalid OPP table in device tree\n");
		return ret;
	}

	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_autosuspend_delay(dev, 250);
	pm_runtime_enable(dev);

	ret = spi_register_master(master);
	if (!ret)
		return 0;

	pm_runtime_disable(dev);

	return ret;
}

static void qcom_qspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);

	/* Unregister _before_ disabling pm_runtime() so we stop transfers */
	spi_unregister_master(master);

	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused qcom_qspi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct qcom_qspi *ctrl = spi_master_get_devdata(master);
	int ret;

	/* Drop the performance state vote */
	dev_pm_opp_set_rate(dev, 0);
	clk_bulk_disable_unprepare(QSPI_NUM_CLKS, ctrl->clks);

	ret = icc_disable(ctrl->icc_path_cpu_to_qspi);
	if (ret) {
		dev_err_ratelimited(ctrl->dev, "%s: ICC disable failed for cpu: %d\n",
			__func__, ret);
		return ret;
	}

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused qcom_qspi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct qcom_qspi *ctrl = spi_master_get_devdata(master);
	int ret;

	pinctrl_pm_select_default_state(dev);

	ret = icc_enable(ctrl->icc_path_cpu_to_qspi);
	if (ret) {
		dev_err_ratelimited(ctrl->dev, "%s: ICC enable failed for cpu: %d\n",
			__func__, ret);
		return ret;
	}

	ret = clk_bulk_prepare_enable(QSPI_NUM_CLKS, ctrl->clks);
	if (ret)
		return ret;

	return dev_pm_opp_set_rate(dev, ctrl->last_speed * 4);
}

static int __maybe_unused qcom_qspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret;

	ret = spi_master_suspend(master);
	if (ret)
		return ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		spi_master_resume(master);

	return ret;
}

static int __maybe_unused qcom_qspi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	ret = spi_master_resume(master);
	if (ret)
		pm_runtime_force_suspend(dev);

	return ret;
}

static const struct dev_pm_ops qcom_qspi_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(qcom_qspi_runtime_suspend,
			   qcom_qspi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(qcom_qspi_suspend, qcom_qspi_resume)
};

static const struct of_device_id qcom_qspi_dt_match[] = {
	{ .compatible = "qcom,qspi-v1", },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_qspi_dt_match);

static struct platform_driver qcom_qspi_driver = {
	.driver = {
		.name		= "qcom_qspi",
		.pm		= &qcom_qspi_dev_pm_ops,
		.of_match_table = qcom_qspi_dt_match,
	},
	.probe = qcom_qspi_probe,
	.remove_new = qcom_qspi_remove,
};
module_platform_driver(qcom_qspi_driver);

MODULE_DESCRIPTION("SPI driver for QSPI cores");
MODULE_LICENSE("GPL v2");
