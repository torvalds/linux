// SPDX-License-Identifier: GPL-2.0+
//
// Freescale i.MX7ULP LPSPI driver
//
// Copyright 2016 Freescale Semiconductor, Inc.
// Copyright 2018 NXP Semiconductors

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/dma/imx-dma.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/types.h>

#define DRIVER_NAME "fsl_lpspi"

#define FSL_LPSPI_RPM_TIMEOUT 50 /* 50ms */

/* The maximum bytes that edma can transfer once.*/
#define FSL_LPSPI_MAX_EDMA_BYTES  ((1 << 15) - 1)

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
#define DER_RDDE	BIT(1)
#define DER_TDDE	BIT(0)
#define CFGR1_PCSCFG	BIT(27)
#define CFGR1_PINCFG	(BIT(24)|BIT(25))
#define CFGR1_PCSPOL	BIT(8)
#define CFGR1_NOSTALL	BIT(3)
#define CFGR1_HOST	BIT(0)
#define FSR_TXCOUNT	(0xFF)
#define RSR_RXEMPTY	BIT(1)
#define TCR_CPOL	BIT(31)
#define TCR_CPHA	BIT(30)
#define TCR_CONT	BIT(21)
#define TCR_CONTC	BIT(20)
#define TCR_RXMSK	BIT(19)
#define TCR_TXMSK	BIT(18)

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
	unsigned long base_phys;
	struct clk *clk_ipg;
	struct clk *clk_per;
	bool is_target;
	bool is_only_cs1;
	bool is_first_byte;

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

	bool target_aborted;

	/* DMA */
	bool usedma;
	struct completion dma_rx_completion;
	struct completion dma_tx_completion;
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

static int fsl_lpspi_bytes_per_word(const int bpw)
{
	return DIV_ROUND_UP(bpw, BITS_PER_BYTE);
}

static bool fsl_lpspi_can_dma(struct spi_controller *controller,
			      struct spi_device *spi,
			      struct spi_transfer *transfer)
{
	unsigned int bytes_per_word;

	if (!controller->dma_rx)
		return false;

	bytes_per_word = fsl_lpspi_bytes_per_word(transfer->bits_per_word);

	switch (bytes_per_word) {
	case 1:
	case 2:
	case 4:
		break;
	default:
		return false;
	}

	return true;
}

static int lpspi_prepare_xfer_hardware(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);
	int ret;

	ret = pm_runtime_resume_and_get(fsl_lpspi->dev);
	if (ret < 0) {
		dev_err(fsl_lpspi->dev, "failed to enable clock\n");
		return ret;
	}

	return 0;
}

static int lpspi_unprepare_xfer_hardware(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	pm_runtime_mark_last_busy(fsl_lpspi->dev);
	pm_runtime_put_autosuspend(fsl_lpspi->dev);

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
		if (!fsl_lpspi->is_target) {
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

static void fsl_lpspi_set_cmd(struct fsl_lpspi_data *fsl_lpspi)
{
	u32 temp = 0;

	temp |= fsl_lpspi->config.bpw - 1;
	temp |= (fsl_lpspi->config.mode & 0x3) << 30;
	temp |= (fsl_lpspi->config.chip_select & 0x3) << 24;
	if (!fsl_lpspi->is_target) {
		temp |= fsl_lpspi->config.prescale << 27;
		/*
		 * Set TCR_CONT will keep SS asserted after current transfer.
		 * For the first transfer, clear TCR_CONTC to assert SS.
		 * For subsequent transfer, set TCR_CONTC to keep SS asserted.
		 */
		if (!fsl_lpspi->usedma) {
			temp |= TCR_CONT;
			if (fsl_lpspi->is_first_byte)
				temp &= ~TCR_CONTC;
			else
				temp |= TCR_CONTC;
		}
	}
	writel(temp, fsl_lpspi->base + IMX7ULP_TCR);

	dev_dbg(fsl_lpspi->dev, "TCR=0x%x\n", temp);
}

static void fsl_lpspi_set_watermark(struct fsl_lpspi_data *fsl_lpspi)
{
	u32 temp;

	if (!fsl_lpspi->usedma)
		temp = fsl_lpspi->watermark >> 1 |
		       (fsl_lpspi->watermark >> 1) << 16;
	else
		temp = fsl_lpspi->watermark >> 1;

	writel(temp, fsl_lpspi->base + IMX7ULP_FCR);

	dev_dbg(fsl_lpspi->dev, "FCR=0x%x\n", temp);
}

static int fsl_lpspi_set_bitrate(struct fsl_lpspi_data *fsl_lpspi)
{
	struct lpspi_config config = fsl_lpspi->config;
	unsigned int perclk_rate, scldiv;
	u8 prescale;

	perclk_rate = clk_get_rate(fsl_lpspi->clk_per);

	if (!config.speed_hz) {
		dev_err(fsl_lpspi->dev,
			"error: the transmission speed provided is 0!\n");
		return -EINVAL;
	}

	if (config.speed_hz > perclk_rate / 2) {
		dev_err(fsl_lpspi->dev,
		      "per-clk should be at least two times of transfer speed");
		return -EINVAL;
	}

	for (prescale = 0; prescale < 8; prescale++) {
		scldiv = perclk_rate / config.speed_hz / (1 << prescale) - 2;
		if (scldiv < 256) {
			fsl_lpspi->config.prescale = prescale;
			break;
		}
	}

	if (scldiv >= 256)
		return -EINVAL;

	writel(scldiv | (scldiv << 8) | ((scldiv >> 1) << 16),
					fsl_lpspi->base + IMX7ULP_CCR);

	dev_dbg(fsl_lpspi->dev, "perclk=%d, speed=%d, prescale=%d, scldiv=%d\n",
		perclk_rate, config.speed_hz, prescale, scldiv);

	return 0;
}

static int fsl_lpspi_dma_configure(struct spi_controller *controller)
{
	int ret;
	enum dma_slave_buswidth buswidth;
	struct dma_slave_config rx = {}, tx = {};
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	switch (fsl_lpspi_bytes_per_word(fsl_lpspi->config.bpw)) {
	case 4:
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	case 2:
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 1:
		buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;
		break;
	default:
		return -EINVAL;
	}

	tx.direction = DMA_MEM_TO_DEV;
	tx.dst_addr = fsl_lpspi->base_phys + IMX7ULP_TDR;
	tx.dst_addr_width = buswidth;
	tx.dst_maxburst = 1;
	ret = dmaengine_slave_config(controller->dma_tx, &tx);
	if (ret) {
		dev_err(fsl_lpspi->dev, "TX dma configuration failed with %d\n",
			ret);
		return ret;
	}

	rx.direction = DMA_DEV_TO_MEM;
	rx.src_addr = fsl_lpspi->base_phys + IMX7ULP_RDR;
	rx.src_addr_width = buswidth;
	rx.src_maxburst = 1;
	ret = dmaengine_slave_config(controller->dma_rx, &rx);
	if (ret) {
		dev_err(fsl_lpspi->dev, "RX dma configuration failed with %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int fsl_lpspi_config(struct fsl_lpspi_data *fsl_lpspi)
{
	u32 temp;
	int ret;

	if (!fsl_lpspi->is_target) {
		ret = fsl_lpspi_set_bitrate(fsl_lpspi);
		if (ret)
			return ret;
	}

	fsl_lpspi_set_watermark(fsl_lpspi);

	if (!fsl_lpspi->is_target)
		temp = CFGR1_HOST;
	else
		temp = CFGR1_PINCFG;
	if (fsl_lpspi->config.mode & SPI_CS_HIGH)
		temp |= CFGR1_PCSPOL;
	writel(temp, fsl_lpspi->base + IMX7ULP_CFGR1);

	temp = readl(fsl_lpspi->base + IMX7ULP_CR);
	temp |= CR_RRF | CR_RTF | CR_MEN;
	writel(temp, fsl_lpspi->base + IMX7ULP_CR);

	temp = 0;
	if (fsl_lpspi->usedma)
		temp = DER_TDDE | DER_RDDE;
	writel(temp, fsl_lpspi->base + IMX7ULP_DER);

	return 0;
}

static int fsl_lpspi_setup_transfer(struct spi_controller *controller,
				     struct spi_device *spi,
				     struct spi_transfer *t)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(spi->controller);

	if (t == NULL)
		return -EINVAL;

	fsl_lpspi->config.mode = spi->mode;
	fsl_lpspi->config.bpw = t->bits_per_word;
	fsl_lpspi->config.speed_hz = t->speed_hz;
	if (fsl_lpspi->is_only_cs1)
		fsl_lpspi->config.chip_select = 1;
	else
		fsl_lpspi->config.chip_select = spi_get_chipselect(spi, 0);

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

	if (fsl_lpspi_can_dma(controller, spi, t))
		fsl_lpspi->usedma = true;
	else
		fsl_lpspi->usedma = false;

	return fsl_lpspi_config(fsl_lpspi);
}

static int fsl_lpspi_target_abort(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	fsl_lpspi->target_aborted = true;
	if (!fsl_lpspi->usedma)
		complete(&fsl_lpspi->xfer_done);
	else {
		complete(&fsl_lpspi->dma_tx_completion);
		complete(&fsl_lpspi->dma_rx_completion);
	}

	return 0;
}

static int fsl_lpspi_wait_for_completion(struct spi_controller *controller)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	if (fsl_lpspi->is_target) {
		if (wait_for_completion_interruptible(&fsl_lpspi->xfer_done) ||
			fsl_lpspi->target_aborted) {
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

	if (!fsl_lpspi->usedma) {
		/* Disable all interrupt */
		fsl_lpspi_intctrl(fsl_lpspi, 0);
	}

	/* W1C for all flags in SR */
	temp = 0x3F << 8;
	writel(temp, fsl_lpspi->base + IMX7ULP_SR);

	/* Clear FIFO and disable module */
	temp = CR_RRF | CR_RTF;
	writel(temp, fsl_lpspi->base + IMX7ULP_CR);

	return 0;
}

static void fsl_lpspi_dma_rx_callback(void *cookie)
{
	struct fsl_lpspi_data *fsl_lpspi = (struct fsl_lpspi_data *)cookie;

	complete(&fsl_lpspi->dma_rx_completion);
}

static void fsl_lpspi_dma_tx_callback(void *cookie)
{
	struct fsl_lpspi_data *fsl_lpspi = (struct fsl_lpspi_data *)cookie;

	complete(&fsl_lpspi->dma_tx_completion);
}

static int fsl_lpspi_calculate_timeout(struct fsl_lpspi_data *fsl_lpspi,
				       int size)
{
	unsigned long timeout = 0;

	/* Time with actual data transfer and CS change delay related to HW */
	timeout = (8 + 4) * size / fsl_lpspi->config.speed_hz;

	/* Add extra second for scheduler related activities */
	timeout += 1;

	/* Double calculated timeout */
	return msecs_to_jiffies(2 * timeout * MSEC_PER_SEC);
}

static int fsl_lpspi_dma_transfer(struct spi_controller *controller,
				struct fsl_lpspi_data *fsl_lpspi,
				struct spi_transfer *transfer)
{
	struct dma_async_tx_descriptor *desc_tx, *desc_rx;
	unsigned long transfer_timeout;
	unsigned long timeout;
	struct sg_table *tx = &transfer->tx_sg, *rx = &transfer->rx_sg;
	int ret;

	ret = fsl_lpspi_dma_configure(controller);
	if (ret)
		return ret;

	desc_rx = dmaengine_prep_slave_sg(controller->dma_rx,
				rx->sgl, rx->nents, DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_rx)
		return -EINVAL;

	desc_rx->callback = fsl_lpspi_dma_rx_callback;
	desc_rx->callback_param = (void *)fsl_lpspi;
	dmaengine_submit(desc_rx);
	reinit_completion(&fsl_lpspi->dma_rx_completion);
	dma_async_issue_pending(controller->dma_rx);

	desc_tx = dmaengine_prep_slave_sg(controller->dma_tx,
				tx->sgl, tx->nents, DMA_MEM_TO_DEV,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc_tx) {
		dmaengine_terminate_all(controller->dma_tx);
		return -EINVAL;
	}

	desc_tx->callback = fsl_lpspi_dma_tx_callback;
	desc_tx->callback_param = (void *)fsl_lpspi;
	dmaengine_submit(desc_tx);
	reinit_completion(&fsl_lpspi->dma_tx_completion);
	dma_async_issue_pending(controller->dma_tx);

	fsl_lpspi->target_aborted = false;

	if (!fsl_lpspi->is_target) {
		transfer_timeout = fsl_lpspi_calculate_timeout(fsl_lpspi,
							       transfer->len);

		/* Wait eDMA to finish the data transfer.*/
		timeout = wait_for_completion_timeout(&fsl_lpspi->dma_tx_completion,
						      transfer_timeout);
		if (!timeout) {
			dev_err(fsl_lpspi->dev, "I/O Error in DMA TX\n");
			dmaengine_terminate_all(controller->dma_tx);
			dmaengine_terminate_all(controller->dma_rx);
			fsl_lpspi_reset(fsl_lpspi);
			return -ETIMEDOUT;
		}

		timeout = wait_for_completion_timeout(&fsl_lpspi->dma_rx_completion,
						      transfer_timeout);
		if (!timeout) {
			dev_err(fsl_lpspi->dev, "I/O Error in DMA RX\n");
			dmaengine_terminate_all(controller->dma_tx);
			dmaengine_terminate_all(controller->dma_rx);
			fsl_lpspi_reset(fsl_lpspi);
			return -ETIMEDOUT;
		}
	} else {
		if (wait_for_completion_interruptible(&fsl_lpspi->dma_tx_completion) ||
			fsl_lpspi->target_aborted) {
			dev_dbg(fsl_lpspi->dev,
				"I/O Error in DMA TX interrupted\n");
			dmaengine_terminate_all(controller->dma_tx);
			dmaengine_terminate_all(controller->dma_rx);
			fsl_lpspi_reset(fsl_lpspi);
			return -EINTR;
		}

		if (wait_for_completion_interruptible(&fsl_lpspi->dma_rx_completion) ||
			fsl_lpspi->target_aborted) {
			dev_dbg(fsl_lpspi->dev,
				"I/O Error in DMA RX interrupted\n");
			dmaengine_terminate_all(controller->dma_tx);
			dmaengine_terminate_all(controller->dma_rx);
			fsl_lpspi_reset(fsl_lpspi);
			return -EINTR;
		}
	}

	fsl_lpspi_reset(fsl_lpspi);

	return 0;
}

static void fsl_lpspi_dma_exit(struct spi_controller *controller)
{
	if (controller->dma_rx) {
		dma_release_channel(controller->dma_rx);
		controller->dma_rx = NULL;
	}

	if (controller->dma_tx) {
		dma_release_channel(controller->dma_tx);
		controller->dma_tx = NULL;
	}
}

static int fsl_lpspi_dma_init(struct device *dev,
			      struct fsl_lpspi_data *fsl_lpspi,
			      struct spi_controller *controller)
{
	int ret;

	/* Prepare for TX DMA: */
	controller->dma_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(controller->dma_tx)) {
		ret = PTR_ERR(controller->dma_tx);
		dev_dbg(dev, "can't get the TX DMA channel, error %d!\n", ret);
		controller->dma_tx = NULL;
		goto err;
	}

	/* Prepare for RX DMA: */
	controller->dma_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(controller->dma_rx)) {
		ret = PTR_ERR(controller->dma_rx);
		dev_dbg(dev, "can't get the RX DMA channel, error %d\n", ret);
		controller->dma_rx = NULL;
		goto err;
	}

	init_completion(&fsl_lpspi->dma_rx_completion);
	init_completion(&fsl_lpspi->dma_tx_completion);
	controller->can_dma = fsl_lpspi_can_dma;
	controller->max_dma_len = FSL_LPSPI_MAX_EDMA_BYTES;

	return 0;
err:
	fsl_lpspi_dma_exit(controller);
	return ret;
}

static int fsl_lpspi_pio_transfer(struct spi_controller *controller,
				  struct spi_transfer *t)
{
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);
	int ret;

	fsl_lpspi->tx_buf = t->tx_buf;
	fsl_lpspi->rx_buf = t->rx_buf;
	fsl_lpspi->remain = t->len;

	reinit_completion(&fsl_lpspi->xfer_done);
	fsl_lpspi->target_aborted = false;

	fsl_lpspi_write_tx_fifo(fsl_lpspi);

	ret = fsl_lpspi_wait_for_completion(controller);
	if (ret)
		return ret;

	fsl_lpspi_reset(fsl_lpspi);

	return 0;
}

static int fsl_lpspi_transfer_one(struct spi_controller *controller,
				  struct spi_device *spi,
				  struct spi_transfer *t)
{
	struct fsl_lpspi_data *fsl_lpspi =
					spi_controller_get_devdata(controller);
	int ret;

	fsl_lpspi->is_first_byte = true;
	ret = fsl_lpspi_setup_transfer(controller, spi, t);
	if (ret < 0)
		return ret;

	fsl_lpspi_set_cmd(fsl_lpspi);
	fsl_lpspi->is_first_byte = false;

	if (fsl_lpspi->usedma)
		ret = fsl_lpspi_dma_transfer(controller, fsl_lpspi, t);
	else
		ret = fsl_lpspi_pio_transfer(controller, t);
	if (ret < 0)
		return ret;

	return 0;
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
	    readl(fsl_lpspi->base + IMX7ULP_FSR) & FSR_TXCOUNT) {
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

#ifdef CONFIG_PM
static int fsl_lpspi_runtime_resume(struct device *dev)
{
	struct spi_controller *controller = dev_get_drvdata(dev);
	struct fsl_lpspi_data *fsl_lpspi;
	int ret;

	fsl_lpspi = spi_controller_get_devdata(controller);

	ret = clk_prepare_enable(fsl_lpspi->clk_per);
	if (ret)
		return ret;

	ret = clk_prepare_enable(fsl_lpspi->clk_ipg);
	if (ret) {
		clk_disable_unprepare(fsl_lpspi->clk_per);
		return ret;
	}

	return 0;
}

static int fsl_lpspi_runtime_suspend(struct device *dev)
{
	struct spi_controller *controller = dev_get_drvdata(dev);
	struct fsl_lpspi_data *fsl_lpspi;

	fsl_lpspi = spi_controller_get_devdata(controller);

	clk_disable_unprepare(fsl_lpspi->clk_per);
	clk_disable_unprepare(fsl_lpspi->clk_ipg);

	return 0;
}
#endif

static int fsl_lpspi_init_rpm(struct fsl_lpspi_data *fsl_lpspi)
{
	struct device *dev = fsl_lpspi->dev;

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, FSL_LPSPI_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(dev);

	return 0;
}

static int fsl_lpspi_probe(struct platform_device *pdev)
{
	struct fsl_lpspi_data *fsl_lpspi;
	struct spi_controller *controller;
	struct resource *res;
	int ret, irq;
	u32 num_cs;
	u32 temp;
	bool is_target;

	is_target = of_property_read_bool((&pdev->dev)->of_node, "spi-slave");
	if (is_target)
		controller = devm_spi_alloc_target(&pdev->dev,
						   sizeof(struct fsl_lpspi_data));
	else
		controller = devm_spi_alloc_host(&pdev->dev,
						 sizeof(struct fsl_lpspi_data));

	if (!controller)
		return -ENOMEM;

	platform_set_drvdata(pdev, controller);

	fsl_lpspi = spi_controller_get_devdata(controller);
	fsl_lpspi->dev = &pdev->dev;
	fsl_lpspi->is_target = is_target;
	fsl_lpspi->is_only_cs1 = of_property_read_bool((&pdev->dev)->of_node,
						"fsl,spi-only-use-cs1-sel");

	init_completion(&fsl_lpspi->xfer_done);

	fsl_lpspi->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(fsl_lpspi->base)) {
		ret = PTR_ERR(fsl_lpspi->base);
		return ret;
	}
	fsl_lpspi->base_phys = res->start;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, irq, fsl_lpspi_isr, 0,
			       dev_name(&pdev->dev), fsl_lpspi);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", irq, ret);
		return ret;
	}

	fsl_lpspi->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(fsl_lpspi->clk_per)) {
		ret = PTR_ERR(fsl_lpspi->clk_per);
		return ret;
	}

	fsl_lpspi->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(fsl_lpspi->clk_ipg)) {
		ret = PTR_ERR(fsl_lpspi->clk_ipg);
		return ret;
	}

	/* enable the clock */
	ret = fsl_lpspi_init_rpm(fsl_lpspi);
	if (ret)
		return ret;

	ret = pm_runtime_get_sync(fsl_lpspi->dev);
	if (ret < 0) {
		dev_err(fsl_lpspi->dev, "failed to enable clock\n");
		goto out_pm_get;
	}

	temp = readl(fsl_lpspi->base + IMX7ULP_PARAM);
	fsl_lpspi->txfifosize = 1 << (temp & 0x0f);
	fsl_lpspi->rxfifosize = 1 << ((temp >> 8) & 0x0f);
	if (of_property_read_u32((&pdev->dev)->of_node, "num-cs",
				 &num_cs)) {
		if (of_device_is_compatible(pdev->dev.of_node, "fsl,imx93-spi"))
			num_cs = ((temp >> 16) & 0xf);
		else
			num_cs = 1;
	}

	controller->bits_per_word_mask = SPI_BPW_RANGE_MASK(8, 32);
	controller->transfer_one = fsl_lpspi_transfer_one;
	controller->prepare_transfer_hardware = lpspi_prepare_xfer_hardware;
	controller->unprepare_transfer_hardware = lpspi_unprepare_xfer_hardware;
	controller->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	controller->flags = SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX;
	controller->dev.of_node = pdev->dev.of_node;
	controller->bus_num = pdev->id;
	controller->num_chipselect = num_cs;
	controller->target_abort = fsl_lpspi_target_abort;
	if (!fsl_lpspi->is_target)
		controller->use_gpio_descriptors = true;

	ret = fsl_lpspi_dma_init(&pdev->dev, fsl_lpspi, controller);
	if (ret == -EPROBE_DEFER)
		goto out_pm_get;
	if (ret < 0)
		dev_warn(&pdev->dev, "dma setup error %d, use pio\n", ret);
	else
		/*
		 * disable LPSPI module IRQ when enable DMA mode successfully,
		 * to prevent the unexpected LPSPI module IRQ events.
		 */
		disable_irq(irq);

	ret = devm_spi_register_controller(&pdev->dev, controller);
	if (ret < 0) {
		dev_err_probe(&pdev->dev, ret, "spi_register_controller error\n");
		goto free_dma;
	}

	pm_runtime_mark_last_busy(fsl_lpspi->dev);
	pm_runtime_put_autosuspend(fsl_lpspi->dev);

	return 0;

free_dma:
	fsl_lpspi_dma_exit(controller);
out_pm_get:
	pm_runtime_dont_use_autosuspend(fsl_lpspi->dev);
	pm_runtime_put_sync(fsl_lpspi->dev);
	pm_runtime_disable(fsl_lpspi->dev);

	return ret;
}

static void fsl_lpspi_remove(struct platform_device *pdev)
{
	struct spi_controller *controller = platform_get_drvdata(pdev);
	struct fsl_lpspi_data *fsl_lpspi =
				spi_controller_get_devdata(controller);

	fsl_lpspi_dma_exit(controller);

	pm_runtime_disable(fsl_lpspi->dev);
}

static int __maybe_unused fsl_lpspi_suspend(struct device *dev)
{
	pinctrl_pm_select_sleep_state(dev);
	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused fsl_lpspi_resume(struct device *dev)
{
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret) {
		dev_err(dev, "Error in resume: %d\n", ret);
		return ret;
	}

	pinctrl_pm_select_default_state(dev);

	return 0;
}

static const struct dev_pm_ops fsl_lpspi_pm_ops = {
	SET_RUNTIME_PM_OPS(fsl_lpspi_runtime_suspend,
				fsl_lpspi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(fsl_lpspi_suspend, fsl_lpspi_resume)
};

static struct platform_driver fsl_lpspi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = fsl_lpspi_dt_ids,
		.pm = &fsl_lpspi_pm_ops,
	},
	.probe = fsl_lpspi_probe,
	.remove_new = fsl_lpspi_remove,
};
module_platform_driver(fsl_lpspi_driver);

MODULE_DESCRIPTION("LPSPI Controller driver");
MODULE_AUTHOR("Gao Pan <pandy.gao@nxp.com>");
MODULE_LICENSE("GPL");
