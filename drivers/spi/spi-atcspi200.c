// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Andes ATCSPI200 SPI Controller
 *
 * Copyright (C) 2025 Andes Technology Corporation.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dev_printk.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/* Register definitions  */
#define ATCSPI_TRANS_FMT	0x10	/* SPI transfer format register */
#define ATCSPI_TRANS_CTRL	0x20	/* SPI transfer control register */
#define ATCSPI_CMD		0x24	/* SPI command register */
#define ATCSPI_ADDR		0x28	/* SPI address register */
#define ATCSPI_DATA		0x2C	/* SPI data register */
#define ATCSPI_CTRL		0x30	/* SPI control register */
#define ATCSPI_STATUS		0x34	/* SPI status register */
#define ATCSPI_TIMING		0x40	/* SPI interface timing register */
#define ATCSPI_CONFIG		0x7C	/* SPI configuration register */

/* Transfer format register */
#define TRANS_FMT_CPHA		BIT(0)
#define TRANS_FMT_CPOL		BIT(1)
#define TRANS_FMT_DATA_MERGE_EN	BIT(7)
#define TRANS_FMT_DATA_LEN_MASK	GENMASK(12, 8)
#define TRANS_FMT_ADDR_LEN_MASK	GENMASK(17, 16)
#define TRANS_FMT_DATA_LEN(x)	FIELD_PREP(TRANS_FMT_DATA_LEN_MASK, (x) - 1)
#define TRANS_FMT_ADDR_LEN(x)	FIELD_PREP(TRANS_FMT_ADDR_LEN_MASK, (x) - 1)

/* Transfer control register */
#define TRANS_MODE_MASK		GENMASK(27, 24)
#define TRANS_MODE_W_ONLY	FIELD_PREP(TRANS_MODE_MASK, 1)
#define TRANS_MODE_R_ONLY	FIELD_PREP(TRANS_MODE_MASK, 2)
#define TRANS_MODE_NONE_DATA	FIELD_PREP(TRANS_MODE_MASK, 7)
#define TRANS_MODE_DMY_READ	FIELD_PREP(TRANS_MODE_MASK, 9)
#define TRANS_FIELD_DECNZ(m, x)	((x) ? FIELD_PREP(m, (x) - 1) : 0)
#define TRANS_RD_TRANS_CNT(x)	TRANS_FIELD_DECNZ(GENMASK(8, 0), x)
#define TRANS_DUMMY_CNT(x)	TRANS_FIELD_DECNZ(GENMASK(10, 9), x)
#define TRANS_WR_TRANS_CNT(x)	TRANS_FIELD_DECNZ(GENMASK(20, 12), x)
#define TRANS_DUAL_QUAD(x)	FIELD_PREP(GENMASK(23, 22), (x))
#define TRANS_ADDR_FMT		BIT(28)
#define TRANS_ADDR_EN		BIT(29)
#define TRANS_CMD_EN		BIT(30)

/* Control register */
#define CTRL_SPI_RST		BIT(0)
#define CTRL_RX_FIFO_RST	BIT(1)
#define CTRL_TX_FIFO_RST	BIT(2)
#define CTRL_RX_DMA_EN		BIT(3)
#define CTRL_TX_DMA_EN		BIT(4)

/* Status register */
#define ATCSPI_ACTIVE		BIT(0)
#define ATCSPI_RX_EMPTY		BIT(14)
#define ATCSPI_TX_FULL		BIT(23)

/* Interface timing setting */
#define TIMING_SCLK_DIV_MASK	GENMASK(7, 0)
#define TIMING_SCLK_DIV_MAX	0xFE

/* Configuration register */
#define RXFIFO_SIZE(x)		FIELD_GET(GENMASK(3, 0), (x))
#define TXFIFO_SIZE(x)		FIELD_GET(GENMASK(7, 4), (x))

/* driver configurations */
#define ATCSPI_MAX_TRANS_LEN	512
#define ATCSPI_MAX_SPEED_HZ	50000000
#define ATCSPI_RDY_TIMEOUT_US	1000000
#define ATCSPI_XFER_TIMEOUT(n)	((n) * 10)
#define ATCSPI_MAX_CS_NUM	1
#define ATCSPI_DMA_THRESHOLD	256
#define ATCSPI_BITS_PER_UINT	8
#define ATCSPI_DATA_MERGE_EN	1
#define ATCSPI_DMA_SUPPORT	1

/**
 * struct atcspi_dev - Andes ATCSPI200 SPI controller private data
 * @host:           Pointer to the SPI controller structure.
 * @mutex_lock:     A mutex to protect concurrent access to the controller.
 * @dma_completion: A completion to signal the end of a DMA transfer.
 * @dev:            Pointer to the device structure.
 * @regmap:         Register map for accessing controller registers.
 * @clk:            Pointer to the controller's functional clock.
 * @dma_addr:       The physical address of the SPI data register for DMA.
 * @clk_rate:       The cached frequency of the functional clock.
 * @sclk_rate:      The target frequency for the SPI clock (SCLK).
 * @txfifo_size:    The size of the transmit FIFO in bytes.
 * @rxfifo_size:    The size of the receive FIFO in bytes.
 * @data_merge:     A flag indicating if the data merge mode is enabled for
 *                  the current transfer.
 * @use_dma:        Enable DMA mode if ATCSPI_DMA_SUPPORT is set and DMA is
 *                  successfully configured.
 */
struct atcspi_dev {
	struct spi_controller	*host;
	struct mutex		mutex_lock;
	struct completion	dma_completion;
	struct device		*dev;
	struct regmap		*regmap;
	struct clk		*clk;
	dma_addr_t		dma_addr;
	unsigned int		clk_rate;
	unsigned int		sclk_rate;
	unsigned int		txfifo_size;
	unsigned int		rxfifo_size;
	bool			data_merge;
	bool			use_dma;
};

static int atcspi_wait_fifo_ready(struct atcspi_dev *spi,
				  enum spi_mem_data_dir dir)
{
	unsigned int val;
	unsigned int mask;
	int ret;

	mask = (dir == SPI_MEM_DATA_OUT) ? ATCSPI_TX_FULL : ATCSPI_RX_EMPTY;
	ret = regmap_read_poll_timeout(spi->regmap,
				       ATCSPI_STATUS,
				       val,
				       !(val & mask),
				       0,
				       ATCSPI_RDY_TIMEOUT_US);
	if (ret)
		dev_info(spi->dev, "Timed out waiting for FIFO ready\n");

	return ret;
}

static int atcspi_xfer_data_poll(struct atcspi_dev *spi,
				 const struct spi_mem_op *op)
{
	void *rx_buf = op->data.buf.in;
	const void *tx_buf = op->data.buf.out;
	unsigned int val;
	int trans_bytes = op->data.nbytes;
	int num_byte;
	int ret = 0;

	num_byte = spi->data_merge ? 4 : 1;
	while (trans_bytes) {
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			ret = atcspi_wait_fifo_ready(spi, SPI_MEM_DATA_OUT);
			if (ret)
				return ret;

			if (spi->data_merge)
				val = *(unsigned int *)tx_buf;
			else
				val = *(unsigned char *)tx_buf;
			regmap_write(spi->regmap, ATCSPI_DATA, val);
			tx_buf = (unsigned char *)tx_buf + num_byte;
		} else {
			ret = atcspi_wait_fifo_ready(spi, SPI_MEM_DATA_IN);
			if (ret)
				return ret;

			regmap_read(spi->regmap, ATCSPI_DATA, &val);
			if (spi->data_merge)
				*(unsigned int *)rx_buf = val;
			else
				*(unsigned char *)rx_buf = (unsigned char)val;
			rx_buf = (unsigned char *)rx_buf + num_byte;
		}
		trans_bytes -= num_byte;
	}

	return ret;
}

static void atcspi_set_trans_ctl(struct atcspi_dev *spi,
				 const struct spi_mem_op *op)
{
	unsigned int tc = 0;

	if (op->cmd.nbytes)
		tc |= TRANS_CMD_EN;
	if (op->addr.nbytes)
		tc |= TRANS_ADDR_EN;
	if (op->addr.buswidth > 1)
		tc |= TRANS_ADDR_FMT;
	if (op->data.nbytes) {
		tc |= TRANS_DUAL_QUAD(ffs(op->data.buswidth) - 1);
		if (op->data.dir == SPI_MEM_DATA_IN) {
			if (op->dummy.nbytes)
				tc |= TRANS_MODE_DMY_READ |
				      TRANS_DUMMY_CNT(op->dummy.nbytes);
			else
				tc |= TRANS_MODE_R_ONLY;
			tc |= TRANS_RD_TRANS_CNT(op->data.nbytes);
		} else {
			tc |= TRANS_MODE_W_ONLY |
			      TRANS_WR_TRANS_CNT(op->data.nbytes);
		}
	} else {
		tc |= TRANS_MODE_NONE_DATA;
	}
	regmap_write(spi->regmap, ATCSPI_TRANS_CTRL, tc);
}

static void atcspi_set_trans_fmt(struct atcspi_dev *spi,
				 const struct spi_mem_op *op)
{
	unsigned int val;

	regmap_read(spi->regmap, ATCSPI_TRANS_FMT, &val);
	if (op->data.nbytes) {
		if (ATCSPI_DATA_MERGE_EN && ATCSPI_BITS_PER_UINT == 8 &&
		    !(op->data.nbytes % 4)) {
			val |= TRANS_FMT_DATA_MERGE_EN;
			spi->data_merge = true;
		} else {
			val &= ~TRANS_FMT_DATA_MERGE_EN;
			spi->data_merge = false;
		}
	}

	val = (val & ~TRANS_FMT_ADDR_LEN_MASK) |
	      TRANS_FMT_ADDR_LEN(op->addr.nbytes);
	regmap_write(spi->regmap, ATCSPI_TRANS_FMT, val);
}

static void atcspi_prepare_trans(struct atcspi_dev *spi,
				 const struct spi_mem_op *op)
{
	atcspi_set_trans_fmt(spi, op);
	atcspi_set_trans_ctl(spi, op);
	if (op->addr.nbytes)
		regmap_write(spi->regmap, ATCSPI_ADDR, op->addr.val);
	regmap_write(spi->regmap, ATCSPI_CMD, op->cmd.opcode);
}

static int atcspi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct atcspi_dev *spi;

	spi = spi_controller_get_devdata(mem->spi->controller);
	op->data.nbytes = min(op->data.nbytes, ATCSPI_MAX_TRANS_LEN);

	/* DMA needs to be aligned to 4 byte */
	if (spi->use_dma && op->data.nbytes >= ATCSPI_DMA_THRESHOLD)
		op->data.nbytes = ALIGN_DOWN(op->data.nbytes, 4);

	return 0;
}

static int atcspi_dma_config(struct atcspi_dev *spi, bool is_rx)
{
	struct dma_slave_config conf = { 0 };
	struct dma_chan *chan;

	if (is_rx) {
		chan = spi->host->dma_rx;
		conf.direction = DMA_DEV_TO_MEM;
		conf.src_addr = spi->dma_addr;
	} else {
		chan = spi->host->dma_tx;
		conf.direction = DMA_MEM_TO_DEV;
		conf.dst_addr = spi->dma_addr;
	}
	conf.dst_maxburst = spi->rxfifo_size / 2;
	conf.src_maxburst = spi->txfifo_size / 2;

	if (spi->data_merge) {
		conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	} else {
		conf.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
		conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	}

	return dmaengine_slave_config(chan, &conf);
}

static void atcspi_dma_callback(void *arg)
{
	struct completion *dma_completion = arg;

	complete(dma_completion);
}

static int atcspi_dma_trans(struct atcspi_dev *spi,
			    const struct spi_mem_op *op)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *dma_ch;
	struct sg_table sgt;
	enum dma_transfer_direction dma_dir;
	dma_cookie_t cookie;
	unsigned int ctrl;
	int timeout;
	int ret;

	regmap_read(spi->regmap, ATCSPI_CTRL, &ctrl);
	ctrl |= CTRL_TX_DMA_EN | CTRL_RX_DMA_EN;
	regmap_write(spi->regmap, ATCSPI_CTRL, ctrl);
	if (op->data.dir == SPI_MEM_DATA_IN) {
		ret = atcspi_dma_config(spi, TRUE);
		dma_dir = DMA_DEV_TO_MEM;
		dma_ch = spi->host->dma_rx;
	} else {
		ret = atcspi_dma_config(spi, FALSE);
		dma_dir = DMA_MEM_TO_DEV;
		dma_ch = spi->host->dma_tx;
	}
	if (ret)
		return ret;

	ret = spi_controller_dma_map_mem_op_data(spi->host, op, &sgt);
	if (ret)
		return ret;

	desc = dmaengine_prep_slave_sg(dma_ch, sgt.sgl, sgt.nents, dma_dir,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc) {
		ret = -ENOMEM;
		goto exit_unmap;
	}

	reinit_completion(&spi->dma_completion);
	desc->callback = atcspi_dma_callback;
	desc->callback_param = &spi->dma_completion;
	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		goto exit_unmap;

	dma_async_issue_pending(dma_ch);
	timeout = msecs_to_jiffies(ATCSPI_XFER_TIMEOUT(op->data.nbytes));
	if (!wait_for_completion_timeout(&spi->dma_completion, timeout)) {
		ret = -ETIMEDOUT;
		dmaengine_terminate_all(dma_ch);
	}

exit_unmap:
	spi_controller_dma_unmap_mem_op_data(spi->host, op, &sgt);

	return ret;
}

static int atcspi_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct spi_device *spi_dev = mem->spi;
	struct atcspi_dev *spi;
	unsigned int val;
	int ret;

	spi = spi_controller_get_devdata(spi_dev->controller);
	mutex_lock(&spi->mutex_lock);
	atcspi_prepare_trans(spi, op);
	if (op->data.nbytes) {
		if (spi->use_dma && op->data.nbytes >= ATCSPI_DMA_THRESHOLD)
			ret = atcspi_dma_trans(spi, op);
		else
			ret = atcspi_xfer_data_poll(spi, op);
		if (ret) {
			dev_info(spi->dev, "SPI transmission failed\n");
			goto exec_mem_exit;
		}
	}

	ret = regmap_read_poll_timeout(spi->regmap,
				       ATCSPI_STATUS,
				       val,
				       !(val & ATCSPI_ACTIVE),
				       0,
				       ATCSPI_RDY_TIMEOUT_US);
	if (ret)
		dev_info(spi->dev, "Timed out waiting for ATCSPI_ACTIVE\n");

exec_mem_exit:
	mutex_unlock(&spi->mutex_lock);

	return ret;
}

static const struct spi_controller_mem_ops atcspi_mem_ops = {
	.exec_op = atcspi_exec_mem_op,
	.adjust_op_size = atcspi_adjust_op_size,
};

static int atcspi_setup(struct atcspi_dev *spi)
{
	unsigned int ctrl_val;
	unsigned int val;
	int actual_spi_sclk_f;
	int ret;
	unsigned char div;

	ctrl_val = CTRL_TX_FIFO_RST | CTRL_RX_FIFO_RST | CTRL_SPI_RST;
	regmap_write(spi->regmap, ATCSPI_CTRL, ctrl_val);
	ret = regmap_read_poll_timeout(spi->regmap,
				       ATCSPI_CTRL,
				       val,
				       !(val & ctrl_val),
				       0,
				       ATCSPI_RDY_TIMEOUT_US);
	if (ret)
		return dev_err_probe(spi->dev, ret,
				     "Timed out waiting for ATCSPI_CTRL\n");

	val = TRANS_FMT_DATA_LEN(ATCSPI_BITS_PER_UINT) |
	      TRANS_FMT_CPHA | TRANS_FMT_CPOL;
	regmap_write(spi->regmap, ATCSPI_TRANS_FMT, val);

	regmap_read(spi->regmap, ATCSPI_CONFIG, &val);
	spi->txfifo_size = BIT(TXFIFO_SIZE(val) + 1);
	spi->rxfifo_size = BIT(RXFIFO_SIZE(val) + 1);

	regmap_read(spi->regmap, ATCSPI_TIMING, &val);
	val &= ~TIMING_SCLK_DIV_MASK;

	/*
	 * The SCLK_DIV value 0xFF is special and indicates that the
	 * SCLK rate should be the same as the SPI clock rate.
	 */
	if (spi->sclk_rate >= spi->clk_rate) {
		div = TIMING_SCLK_DIV_MASK;
	} else {
		/*
		 * The divider value is determined as follows:
		 * 1. If the divider can generate the exact target frequency,
		 *    use that setting.
		 * 2. If an exact match is not possible, select the closest
		 *    available setting that is lower than the target frequency.
		 */
		div = (spi->clk_rate + (spi->sclk_rate * 2 - 1)) /
		      (spi->sclk_rate * 2) - 1;

		/* Check if the actual SPI clock is lower than the target */
		actual_spi_sclk_f = spi->clk_rate / ((div + 1) * 2);
		if (actual_spi_sclk_f < spi->sclk_rate)
			dev_info(spi->dev,
				 "Clock adjusted %d to %d due to divider limitation",
				 spi->sclk_rate, actual_spi_sclk_f);

		if (div > TIMING_SCLK_DIV_MAX)
			return dev_err_probe(spi->dev, -EINVAL,
					     "Unsupported SPI clock %d\n",
					     spi->sclk_rate);
	}
	val |= div;
	regmap_write(spi->regmap, ATCSPI_TIMING, val);

	return ret;
}

static int atcspi_init_resources(struct platform_device *pdev,
				 struct atcspi_dev *spi,
				 struct resource **mem_res)
{
	void __iomem *base;
	const struct regmap_config atcspi_regmap_cfg = {
		.name = "atcspi",
		.reg_bits = 32,
		.val_bits = 32,
		.cache_type = REGCACHE_NONE,
		.reg_stride = 4,
		.pad_bits = 0,
		.max_register = ATCSPI_CONFIG
	};

	base = devm_platform_get_and_ioremap_resource(pdev, 0, mem_res);
	if (IS_ERR(base))
		return dev_err_probe(spi->dev, PTR_ERR(base),
				     "Failed to get ioremap resource\n");

	spi->regmap = devm_regmap_init_mmio(spi->dev, base,
					    &atcspi_regmap_cfg);
	if (IS_ERR(spi->regmap))
		return dev_err_probe(spi->dev, PTR_ERR(spi->regmap),
				     "Failed to init regmap\n");

	spi->clk = devm_clk_get(spi->dev, NULL);
	if (IS_ERR(spi->clk))
		return dev_err_probe(spi->dev, PTR_ERR(spi->clk),
				     "Failed to get SPI clock\n");

	spi->sclk_rate = ATCSPI_MAX_SPEED_HZ;
	return 0;
}

static int atcspi_configure_dma(struct atcspi_dev *spi)
{
	struct dma_chan *dma_chan;
	int ret = 0;

	dma_chan = devm_dma_request_chan(spi->dev, "rx");
	if (IS_ERR(dma_chan)) {
		ret = PTR_ERR(dma_chan);
		goto err_exit;
	}
	spi->host->dma_rx = dma_chan;

	dma_chan = devm_dma_request_chan(spi->dev, "tx");
	if (IS_ERR(dma_chan)) {
		ret = PTR_ERR(dma_chan);
		goto free_rx;
	}
	spi->host->dma_tx = dma_chan;
	init_completion(&spi->dma_completion);

	return ret;

free_rx:
	dma_release_channel(spi->host->dma_rx);
	spi->host->dma_rx = NULL;
err_exit:
	return ret;
}

static int atcspi_enable_clk(struct atcspi_dev *spi)
{
	int ret;

	ret = clk_prepare_enable(spi->clk);
	if (ret)
		return dev_err_probe(spi->dev, ret,
				     "Failed to enable clock\n");

	spi->clk_rate = clk_get_rate(spi->clk);
	if (!spi->clk_rate)
		return dev_err_probe(spi->dev, -EINVAL,
				     "Failed to get SPI clock rate\n");

	return 0;
}

static void atcspi_init_controller(struct platform_device *pdev,
				   struct atcspi_dev *spi,
				   struct spi_controller *host,
				   struct resource *mem_res)
{
	/* Get the physical address of the data register for DMA transfers. */
	spi->dma_addr = (dma_addr_t)(mem_res->start + ATCSPI_DATA);

	/* Initialize controller properties */
	host->bus_num = pdev->id;
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_RX_QUAD | SPI_TX_QUAD;
	host->num_chipselect = ATCSPI_MAX_CS_NUM;
	host->mem_ops = &atcspi_mem_ops;
	host->max_speed_hz = spi->sclk_rate;
}

static int atcspi_probe(struct platform_device *pdev)
{
	struct spi_controller *host;
	struct atcspi_dev *spi;
	struct resource *mem_res;
	int ret;

	host = spi_alloc_host(&pdev->dev, sizeof(*spi));
	if (!host)
		return -ENOMEM;

	spi = spi_controller_get_devdata(host);
	spi->host = host;
	spi->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, host);

	ret = atcspi_init_resources(pdev, spi, &mem_res);
	if (ret)
		goto free_controller;

	ret = atcspi_enable_clk(spi);
	if (ret)
		goto free_controller;

	atcspi_init_controller(pdev, spi, host, mem_res);

	ret = atcspi_setup(spi);
	if (ret)
		goto disable_clk;

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret) {
		dev_err_probe(spi->dev, ret,
			      "Failed to register SPI controller\n");
		goto disable_clk;
	}

	spi->use_dma = false;
	if (ATCSPI_DMA_SUPPORT) {
		ret = atcspi_configure_dma(spi);
		if (ret)
			dev_info(spi->dev,
				 "Failed to init DMA, fallback to PIO mode\n");
		else
			spi->use_dma = true;
	}
	mutex_init(&spi->mutex_lock);

	return 0;

disable_clk:
	clk_disable_unprepare(spi->clk);

free_controller:
	spi_controller_put(host);
	return ret;
}

static int atcspi_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct atcspi_dev *spi = spi_controller_get_devdata(host);

	spi_controller_suspend(host);

	clk_disable_unprepare(spi->clk);

	return 0;
}

static int atcspi_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct atcspi_dev *spi = spi_controller_get_devdata(host);
	int ret;

	ret = clk_prepare_enable(spi->clk);
	if (ret)
		return ret;

	ret = atcspi_setup(spi);
	if (ret)
		goto disable_clk;

	ret = spi_controller_resume(host);
	if (ret)
		goto disable_clk;

	return ret;

disable_clk:
	clk_disable_unprepare(spi->clk);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(atcspi_pm_ops, atcspi_suspend, atcspi_resume);

static const struct of_device_id atcspi_of_match[] = {
	{ .compatible = "andestech,qilai-spi", },
	{ .compatible = "andestech,ae350-spi", },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atcspi_of_match);

static struct platform_driver atcspi_driver = {
	.probe = atcspi_probe,
	.driver = {
		.name = "atcspi200",
		.owner	= THIS_MODULE,
		.of_match_table = atcspi_of_match,
		.pm = pm_sleep_ptr(&atcspi_pm_ops)
	}
};
module_platform_driver(atcspi_driver);

MODULE_AUTHOR("CL Wang <cl634@andestech.com>");
MODULE_DESCRIPTION("Andes ATCSPI200 SPI controller driver");
MODULE_LICENSE("GPL");
