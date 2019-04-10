// SPDX-License-Identifier: GPL-2.0+
//
// Freescale i.MX7ULP LPSPI driver
//
// Copyright 2016 Freescale Semiconductor, Inc.
// Copyright 2018 NXP Semiconductors

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/types.h>

#define DRIVER_NAME "fsl_lpspi"

/* i.MX7ULP LPSPI registers */
#define IMX7ULP_VERID	0x0
#define IMX7ULP_PARAM	0x4
#define IMX7ULP_CR	0x10
#define IMX7ULP_SR	0x14
#define IMX7ULP_IER	0x18
#define IMX7ULP_DER	0x1c
#define IMX7ULP_CFGR0	0x20
#define IMX7ULP_CFGR1	0x24
#define IMX7ULP_DMR0	0x30
#define IMX7ULP_DMR1	0x34
#define IMX7ULP_CCR	0x40
#define IMX7ULP_FCR	0x58
#define IMX7ULP_FSR	0x5c
#define IMX7ULP_TCR	0x60
#define IMX7ULP_TDR	0x64
#define IMX7ULP_RSR	0x70
#define IMX7ULP_RDR	0x74

/* General control register field define */
#define CR_RRF		BIT(9)
#define CR_RTF		BIT(8)
#define CR_RST		BIT(1)
#define CR_MEN		BIT(0)
#define SR_MBF		BIT(24)
#define SR_TCF		BIT(10)
#define SR_FCF		BIT(9)
#define SR_RDF		BIT(1)
#define SR_TDF		BIT(0)
#define IER_TCIE	BIT(10)
#define IER_FCIE	BIT(9)
#define IER_RDIE	BIT(1)
#define IER_TDIE	BIT(0)
#define CFGR1_PCSCFG	BIT(27)
#define CFGR1_PINCFG	(BIT(24)|BIT(25))
#define CFGR1_PCSPOL	BIT(8)
#define CFGR1_NOSTALL	BIT(3)
#define CFGR1_MASTER	BIT(0)
#define FSR_RXCOUNT	(BIT(16)|BIT(17)|BIT(18))
#define RSR_RXEMPTY	BIT(1)
#define TCR_CPOL	BIT(31)
#define TCR_CPHA	BIT(30)
#define TCR_CONT	BIT(21)
#define TCR_CONTC	BIT(20)
#define TCR_RXMSK	BIT(19)
#define TCR_TXMSK	BIT(18)

static int clkdivs[] = {1, 2, 4, 8, 16, 32, 64, 128};

struct lpspi_config {
	u8 bpw;
	u8 chip_select;
	u8 prescale;
	u16 mode;
	u32 speed_hz;
};

struct fsl_lpspi_data {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	bool is_slave;

	void *rx_buf;
	const void *tx_buf;
	void (*tx)(struct fsl_lpspi_data *);
	void (*rx)(struct fsl_lpspi_data *);

	u32 remain;
	u8 watermark;
	u8 txfifosize;
	u8 rxfifosize;

	struct lpspi_config config;
	struct completion xfer_done;

	bool slave_aborted;
};

static const struct of_device_id fsl_lpspi_dt_ids[] = {
	{ .compatible = "fsl,imx7ulp-spi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_lpspi_dt_ids);

#define LPSPI_BUF_RX(type)						\
static void fsl_lpspi_buf_rx_##type(struct fsl_lpspi_data *fsl_lpspi)	\
{									\
	unsigned int val = readl(fsl_lpspi->base + IMX7ULP_RDR);	\
									\
	if (fsl_lpspi->rx_buf) {					\
		*(type *)fsl_lpspi->rx_buf = val;			\
		fsl_lpspi->rx_buf += sizeof(type);                      \
	}								\
}

#define LPSPI_BUF_TX(type)						\
static void fsl_lpspi_buf_tx_##type(struct fsl_lpspi_data *fsl_lpspi)	\
{									\
	type val = 0;							\
									\
	if (fsl_lpspi->tx_buf) {					\
		val = *(type *)fsl_lpspi->tx_buf;			\
		fsl_lpspi->tx_buf += sizeof(type);			\
	}								\
									\
	fsl_lpspi->remain -= sizeof(type);				\
	writel(val, fsl_lpspi->base + IMX7ULP_TDR);			\
}

LPSPI_BUF_RX(u8)
LPSPI_BUF_TX(u8)
LPSPI_BUF_RX(u16)
LPSPI_BUF_TX(u16)
LPSPI_BUF_RX(u32)
LPSPI_BUF_TX(u32)

static void fsl_lpspi_intctrl(struct fsl_lpspi_data *fsl_lpspi,
			      unsigned int enable)
{
	writel(enable, fsl_lpspi->base + IMX7ULP_IER);
}

static int lpspi_prepare_xfer_hardware(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	return clk_prepare_enable(fsl_lpspi->clk);
}

static int lpspi_unprepare_xfer_hardware(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	clk_disable_unprepare(fsl_lpspi->clk);

	return 0;
}

static void fsl_lpspi_write_tx_fifo(struct fsl_lpspi_data *fsl_lpspi)
{
	u8 txfifo_cnt;
	u32 temp;

	txfifo_cnt = readl(fsl_lpspi->base + IMX7ULP_FSR) & 0xff;

	while (txfifo_cnt < fsl_lpspi->txfifosize) {
		if (!fsl_lpspi->remain)
			break;
		fsl_lpspi->tx(fsl_lpspi);
		txfifo_cnt++;
	}

	if (txfifo_cnt < fsl_lpspi->txfifosize) {
		if (!fsl_lpspi->is_slave) {
			temp = readl(fsl_lpspi->base + IMX7ULP_TCR);
			temp &= ~TCR_CONTC;
			writel(temp, fsl_lpspi->base + IMX7ULP_TCR);
		}

		fsl_lpspi_intctrl(fsl_lpspi, IER_FCIE);
	} else
		fsl_lpspi_intctrl(fsl_lpspi, IER_TDIE);
}

static void fsl_lpspi_read_rx_fifo(struct fsl_lpspi_data *fsl_lpspi)
{
	while (!(readl(fsl_lpspi->base + IMX7ULP_RSR) & RSR_RXEMPTY))
		fsl_lpspi->rx(fsl_lpspi);
}

static void fsl_lpspi_set_cmd(struct fsl_lpspi_data *fsl_lpspi,
			      bool is_first_xfer)
{
	u32 temp = 0;

	temp |= fsl_lpspi->config.bpw - 1;
	temp |= (fsl_lpspi->config.mode & 0x3) << 30;
	if (!fsl_lpspi->is_slave) {
		temp |= fsl_lpspi->config.prescale << 27;
		temp |= (fsl_lpspi->config.chip_select & 0x3) << 24;

		/*
		 * Set TCR_CONT will keep SS asserted after current transfer.
		 * For the first transfer, clear TCR_CONTC to assert SS.
		 * For subsequent transfer, set TCR_CONTC to keep SS asserted.
		 */
		temp |= TCR_CONT;
		if (is_first_xfer)
			temp &= ~TCR_CONTC;
		else
			temp |= TCR_CONTC;
	}
	writel(temp, fsl_lpspi->base + IMX7ULP_TCR);

	dev_dbg(fsl_lpspi->dev, "TCR=0x%x\n", temp);
}

static void fsl_lpspi_set_watermark(struct fsl_lpspi_data *fsl_lpspi)
{
	u32 temp;

	temp = fsl_lpspi->watermark >> 1 | (fsl_lpspi->watermark >> 1) << 16;

	writel(temp, fsl_lpspi->base + IMX7ULP_FCR);

	dev_dbg(fsl_lpspi->dev, "FCR=0x%x\n", temp);
}

static int fsl_lpspi_set_bitrate(struct fsl_lpspi_data *fsl_lpspi)
{
	struct lpspi_config config = fsl_lpspi->config;
	unsigned int perclk_rate, scldiv;
	u8 prescale;

	perclk_rate = clk_get_rate(fsl_lpspi->clk);
	for (prescale = 0; prescale < 8; prescale++) {
		scldiv = perclk_rate /
			 (clkdivs[prescale] * config.speed_hz) - 2;
		if (scldiv < 256) {
			fsl_lpspi->config.prescale = prescale;
			break;
		}
	}

	if (prescale == 8 && scldiv >= 256)
		return -EINVAL;

	writel(scldiv | (scldiv << 8) | ((scldiv >> 1) << 16),
					fsl_lpspi->base + IMX7ULP_CCR);

	dev_dbg(fsl_lpspi->dev, "perclk=%d, speed=%d, prescale =%d, scldiv=%d\n",
		perclk_rate, config.speed_hz, prescale, scldiv);

	return 0;
}

static int fsl_lpspi_config(struct fsl_lpspi_data *fsl_lpspi)
{
	u32 temp;
	int ret;

	if (!fsl_lpspi->is_slave) {
		ret = fsl_lpspi_set_bitrate(fsl_lpspi);
		if (ret)
			return ret;
	}

	fsl_lpspi_set_watermark(fsl_lpspi);

	if (!fsl_lpspi->is_slave)
		temp = CFGR1_MASTER;
	else
		temp = CFGR1_PINCFG;
	if (fsl_lpspi->config.mode & SPI_CS_HIGH)
		temp |= CFGR1_PCSPOL;
	writel(temp, fsl_lpspi->base + IMX7ULP_CFGR1);

	temp = readl(fsl_lpspi->base + IMX7ULP_CR);
	temp |= CR_RRF | CR_RTF | CR_MEN;
	writel(temp, fsl_lpspi->base + IMX7ULP_CR);

	return 0;
}

static void fsl_lpspi_setup_transfer(struct spi_device *spi,
				     struct spi_transfer *t)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(spi->controller);

	fsl_lpspi->config.mode = spi->mode;
	fsl_lpspi->config.bpw = t ? t->bits_per_word : spi->bits_per_word;
	fsl_lpspi->config.speed_hz = t ? t->speed_hz : spi->max_speed_hz;
	fsl_lpspi->config.chip_select = spi->chip_select;

	if (!fsl_lpspi->config.speed_hz)
		fsl_lpspi->config.speed_hz = spi->max_speed_hz;
	if (!fsl_lpspi->config.bpw)
		fsl_lpspi->config.bpw = spi->bits_per_word;

	/* Initialize the functions for transfer */
	if (fsl_lpspi->config.bpw <= 8) {
		fsl_lpspi->rx = fsl_lpspi_buf_rx_u8;
		fsl_lpspi->tx = fsl_lpspi_buf_tx_u8;
	} else if (fsl_lpspi->config.bpw <= 16) {
		fsl_lpspi->rx = fsl_lpspi_buf_rx_u16;
		fsl_lpspi->tx = fsl_lpspi_buf_tx_u16;
	} else {
		fsl_lpspi->rx = fsl_lpspi_buf_rx_u32;
		fsl_lpspi->tx = fsl_lpspi_buf_tx_u32;
	}

	if (t->len <= fsl_lpspi->txfifosize)
		fsl_lpspi->watermark = t->len;
	else
		fsl_lpspi->watermark = fsl_lpspi->txfifosize;

	fsl_lpspi_config(fsl_lpspi);
}

static int fsl_lpspi_slave_abort(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	fsl_lpspi->slave_aborted = true;
	complete(&fsl_lpspi->xfer_done);
	return 0;
}

static int fsl_lpspi_wait_for_completion(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	if (fsl_lpspi->is_slave) {
		if (wait_for_completion_interruptible(&fsl_lpspi->xfer_done) ||
			fsl_lpspi->slave_aborted) {
			dev_dbg(fsl_lpspi->dev, "interrupted\n");
			return -EINTR;
		}
	} else {
		if (!wait_for_completion_timeout(&fsl_lpspi->xfer_done, HZ)) {
			dev_dbg(fsl_lpspi->dev, "wait for completion timeout\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static int fsl_lpspi_reset(struct fsl_lpspi_data *fsl_lpspi)
{
	u32 temp;

	/* Disable all interrupt */
	fsl_lpspi_intctrl(fsl_lpspi, 0);

	/* W1C for all flags in SR */
	temp = 0x3F << 8;
	writel(temp, fsl_lpspi->base + IMX7ULP_SR);

	/* Clear FIFO and disable module */
	temp = CR_RRF | CR_RTF;
	writel(temp, fsl_lpspi->base + IMX7ULP_CR);

	return 0;
}

static int fsl_lpspi_transfer_one(struct spi_controller *controller,
				  struct spi_device *spi,
				  struct spi_transfer *t)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);
	int ret;

	fsl_lpspi->tx_buf = t->tx_buf;
	fsl_lpspi->rx_buf = t->rx_buf;
	fsl_lpspi->remain = t->len;

	reinit_completion(&fsl_lpspi->xfer_done);
	fsl_lpspi->slave_aborted = false;

	fsl_lpspi_write_tx_fifo(fsl_lpspi);

	ret = fsl_lpspi_wait_for_completion(controller);
	if (ret)
		return ret;

	fsl_lpspi_reset(fsl_lpspi);

	return 0;
}

static int fsl_lpspi_transfer_one_msg(struct spi_controller *controller,
				      struct spi_message *msg)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);
	struct spi_device *spi = msg->spi;
	struct spi_transfer *xfer;
	bool is_first_xfer = true;
	int ret = 0;

	msg->status = 0;
	msg->actual_length = 0;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		fsl_lpspi_setup_transfer(spi, xfer);
		fsl_lpspi_set_cmd(fsl_lpspi, is_first_xfer);

		is_first_xfer = false;

		ret = fsl_lpspi_transfer_one(controller, spi, xfer);
		if (ret < 0)
			goto complete;

		msg->actual_length += xfer->len;
	}

complete:
	msg->status = ret;
	spi_finalize_current_message(controller);

	return ret;
}

static irqreturn_t fsl_lpspi_isr(int irq, void *dev_id)
{
	u32 temp_SR, temp_IER;
	struct fsl_lpspi_data *fsl_lpspi = dev_id;

	temp_IER = readl(fsl_lpspi->base + IMX7ULP_IER);
	fsl_lpspi_intctrl(fsl_lpspi, 0);
	temp_SR = readl(fsl_lpspi->base + IMX7ULP_SR);

	fsl_lpspi_read_rx_fifo(fsl_lpspi);

	if ((temp_SR & SR_TDF) && (temp_IER & IER_TDIE)) {
		fsl_lpspi_write_tx_fifo(fsl_lpspi);
		return IRQ_HANDLED;
	}

	if (temp_SR & SR_MBF ||
	    readl(fsl_lpspi->base + IMX7ULP_FSR) & FSR_RXCOUNT) {
		writel(SR_FCF, fsl_lpspi->base + IMX7ULP_SR);
		fsl_lpspi_intctrl(fsl_lpspi, IER_FCIE);
		return IRQ_HANDLED;
	}

	if (temp_SR & SR_FCF && (temp_IER & IER_FCIE)) {
		writel(SR_FCF, fsl_lpspi->base + IMX7ULP_SR);
			complete(&fsl_lpspi->xfer_done);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int fsl_lpspi_probe(struct platform_device *pdev)
{
	struct fsl_lpspi_data *fsl_lpspi;
	struct spi_controller *controller;
	struct resource *res;
	int ret, irq;
	u32 temp;

	if (of_property_read_bool((&pdev->dev)->of_node, "spi-slave"))
		controller = spi_alloc_slave(&pdev->dev,
					sizeof(struct fsl_lpspi_data));
	else
		controller = spi_alloc_master(&pdev->dev,
					sizeof(struct fsl_lpspi_data));

	if (!controller)
		return -ENOMEM;

	platform_set_drvdata(pdev, controller);

	controller->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 32);
	controller->bus_num = pdev->id;

	fsl_lpspi = spi_controller_get_devdata(controller);
	fsl_lpspi->dev = &pdev->dev;
	fsl_lpspi->is_slave = of_property_read_bool((&pdev->dev)->of_node,
						    "spi-slave");

	controller->transfer_one_message = fsl_lpspi_transfer_one_msg;
	controller->prepare_transfer_hardware = lpspi_prepare_xfer_hardware;
	controller->unprepare_transfer_hardware = lpspi_unprepare_xfer_hardware;
	controller->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	controller->flags = SPI_MASTER_MUST_RX | SPI_MASTER_MUST_TX;
	controller->dev.of_node = pdev->dev.of_node;
	controller->bus_num = pdev->id;
	controller->slave_abort = fsl_lpspi_slave_abort;

	init_completion(&fsl_lpspi->xfer_done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	fsl_lpspi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(fsl_lpspi->base)) {
		ret = PTR_ERR(fsl_lpspi->base);
		goto out_controller_put;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out_controller_put;
	}

	ret = devm_request_irq(&pdev->dev, irq, fsl_lpspi_isr, 0,
			       dev_name(&pdev->dev), fsl_lpspi);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", irq, ret);
		goto out_controller_put;
	}

	fsl_lpspi->clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(fsl_lpspi->clk)) {
		ret = PTR_ERR(fsl_lpspi->clk);
		goto out_controller_put;
	}

	ret = clk_prepare_enable(fsl_lpspi->clk);
	if (ret) {
		dev_err(&pdev->dev, "can't enable lpspi clock, ret=%d\n", ret);
		goto out_controller_put;
	}

	temp = readl(fsl_lpspi->base + IMX7ULP_PARAM);
	fsl_lpspi->txfifosize = 1 << (temp & 0x0f);
	fsl_lpspi->rxfifosize = 1 << ((temp >> 8) & 0x0f);

	clk_disable_unprepare(fsl_lpspi->clk);

	ret = devm_spi_register_controller(&pdev->dev, controller);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_controller error.\n");
		goto out_controller_put;
	}

	return 0;

out_controller_put:
	spi_controller_put(controller);

	return ret;
}

static int fsl_lpspi_remove(struct platform_device *pdev)
{
	struct spi_controller *controller = platform_get_drvdata(pdev);
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	clk_disable_unprepare(fsl_lpspi->clk);

	return 0;
}

static struct platform_driver fsl_lpspi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = fsl_lpspi_dt_ids,
	},
	.probe = fsl_lpspi_probe,
	.remove = fsl_lpspi_remove,
};
module_platform_driver(fsl_lpspi_driver);

MODULE_DESCRIPTION("LPSPI Controller driver");
MODULE_AUTHOR("Gao Pan <pandy.gao@nxp.com>");
MODULE_LICENSE("GPL");
