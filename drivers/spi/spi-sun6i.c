// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 - 2014 Allwinner Tech
 * Pan Nan <pannan@allwinnertech.com>
 *
 * Copyright (C) 2014 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/dmaengine.h>

#include <linux/spi/spi.h>

#define SUN6I_AUTOSUSPEND_TIMEOUT	2000

#define SUN6I_FIFO_DEPTH		128
#define SUN8I_FIFO_DEPTH		64

#define SUN6I_GBL_CTL_REG		0x04
#define SUN6I_GBL_CTL_BUS_ENABLE		BIT(0)
#define SUN6I_GBL_CTL_MASTER			BIT(1)
#define SUN6I_GBL_CTL_TP			BIT(7)
#define SUN6I_GBL_CTL_RST			BIT(31)

#define SUN6I_TFR_CTL_REG		0x08
#define SUN6I_TFR_CTL_CPHA			BIT(0)
#define SUN6I_TFR_CTL_CPOL			BIT(1)
#define SUN6I_TFR_CTL_SPOL			BIT(2)
#define SUN6I_TFR_CTL_CS_MASK			0x30
#define SUN6I_TFR_CTL_CS(cs)			(((cs) << 4) & SUN6I_TFR_CTL_CS_MASK)
#define SUN6I_TFR_CTL_CS_MANUAL			BIT(6)
#define SUN6I_TFR_CTL_CS_LEVEL			BIT(7)
#define SUN6I_TFR_CTL_DHB			BIT(8)
#define SUN6I_TFR_CTL_SDC			BIT(11)
#define SUN6I_TFR_CTL_FBS			BIT(12)
#define SUN6I_TFR_CTL_SDM			BIT(13)
#define SUN6I_TFR_CTL_XCH			BIT(31)

#define SUN6I_INT_CTL_REG		0x10
#define SUN6I_INT_CTL_RF_RDY			BIT(0)
#define SUN6I_INT_CTL_TF_ERQ			BIT(4)
#define SUN6I_INT_CTL_RF_OVF			BIT(8)
#define SUN6I_INT_CTL_TC			BIT(12)

#define SUN6I_INT_STA_REG		0x14

#define SUN6I_FIFO_CTL_REG		0x18
#define SUN6I_FIFO_CTL_RF_RDY_TRIG_LEVEL_MASK	0xff
#define SUN6I_FIFO_CTL_RF_DRQ_EN		BIT(8)
#define SUN6I_FIFO_CTL_RF_RDY_TRIG_LEVEL_BITS	0
#define SUN6I_FIFO_CTL_RF_RST			BIT(15)
#define SUN6I_FIFO_CTL_TF_ERQ_TRIG_LEVEL_MASK	0xff
#define SUN6I_FIFO_CTL_TF_ERQ_TRIG_LEVEL_BITS	16
#define SUN6I_FIFO_CTL_TF_DRQ_EN		BIT(24)
#define SUN6I_FIFO_CTL_TF_RST			BIT(31)

#define SUN6I_FIFO_STA_REG		0x1c
#define SUN6I_FIFO_STA_RF_CNT_MASK		GENMASK(7, 0)
#define SUN6I_FIFO_STA_TF_CNT_MASK		GENMASK(23, 16)

#define SUN6I_CLK_CTL_REG		0x24
#define SUN6I_CLK_CTL_CDR2_MASK			0xff
#define SUN6I_CLK_CTL_CDR2(div)			(((div) & SUN6I_CLK_CTL_CDR2_MASK) << 0)
#define SUN6I_CLK_CTL_CDR1_MASK			0xf
#define SUN6I_CLK_CTL_CDR1(div)			(((div) & SUN6I_CLK_CTL_CDR1_MASK) << 8)
#define SUN6I_CLK_CTL_DRS			BIT(12)

#define SUN6I_MAX_XFER_SIZE		0xffffff

#define SUN6I_BURST_CNT_REG		0x30

#define SUN6I_XMIT_CNT_REG		0x34

#define SUN6I_BURST_CTL_CNT_REG		0x38
#define SUN6I_BURST_CTL_CNT_STC_MASK		GENMASK(23, 0)
#define SUN6I_BURST_CTL_CNT_DRM			BIT(28)
#define SUN6I_BURST_CTL_CNT_QUAD_EN		BIT(29)

#define SUN6I_TXDATA_REG		0x200
#define SUN6I_RXDATA_REG		0x300

struct sun6i_spi_cfg {
	unsigned long		fifo_depth;
	bool			has_clk_ctl;
	u32			mode_bits;
};

struct sun6i_spi {
	struct spi_master	*master;
	void __iomem		*base_addr;
	dma_addr_t		dma_addr_rx;
	dma_addr_t		dma_addr_tx;
	struct clk		*hclk;
	struct clk		*mclk;
	struct reset_control	*rstc;

	struct completion	done;

	const u8		*tx_buf;
	u8			*rx_buf;
	int			len;
	const struct sun6i_spi_cfg *cfg;
};

static inline u32 sun6i_spi_read(struct sun6i_spi *sspi, u32 reg)
{
	return readl(sspi->base_addr + reg);
}

static inline void sun6i_spi_write(struct sun6i_spi *sspi, u32 reg, u32 value)
{
	writel(value, sspi->base_addr + reg);
}

static inline u32 sun6i_spi_get_rx_fifo_count(struct sun6i_spi *sspi)
{
	u32 reg = sun6i_spi_read(sspi, SUN6I_FIFO_STA_REG);

	return FIELD_GET(SUN6I_FIFO_STA_RF_CNT_MASK, reg);
}

static inline u32 sun6i_spi_get_tx_fifo_count(struct sun6i_spi *sspi)
{
	u32 reg = sun6i_spi_read(sspi, SUN6I_FIFO_STA_REG);

	return FIELD_GET(SUN6I_FIFO_STA_TF_CNT_MASK, reg);
}

static inline void sun6i_spi_disable_interrupt(struct sun6i_spi *sspi, u32 mask)
{
	u32 reg = sun6i_spi_read(sspi, SUN6I_INT_CTL_REG);

	reg &= ~mask;
	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, reg);
}

static inline void sun6i_spi_drain_fifo(struct sun6i_spi *sspi)
{
	u32 len;
	u8 byte;

	/* See how much data is available */
	len = sun6i_spi_get_rx_fifo_count(sspi);

	while (len--) {
		byte = readb(sspi->base_addr + SUN6I_RXDATA_REG);
		if (sspi->rx_buf)
			*sspi->rx_buf++ = byte;
	}
}

static inline void sun6i_spi_fill_fifo(struct sun6i_spi *sspi)
{
	u32 cnt;
	int len;
	u8 byte;

	/* See how much data we can fit */
	cnt = sspi->cfg->fifo_depth - sun6i_spi_get_tx_fifo_count(sspi);

	len = min((int)cnt, sspi->len);

	while (len--) {
		byte = sspi->tx_buf ? *sspi->tx_buf++ : 0;
		writeb(byte, sspi->base_addr + SUN6I_TXDATA_REG);
		sspi->len--;
	}
}

static void sun6i_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct sun6i_spi *sspi = spi_master_get_devdata(spi->master);
	u32 reg;

	reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);
	reg &= ~SUN6I_TFR_CTL_CS_MASK;
	reg |= SUN6I_TFR_CTL_CS(spi_get_chipselect(spi, 0));

	if (enable)
		reg |= SUN6I_TFR_CTL_CS_LEVEL;
	else
		reg &= ~SUN6I_TFR_CTL_CS_LEVEL;

	sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg);
}

static size_t sun6i_spi_max_transfer_size(struct spi_device *spi)
{
	return SUN6I_MAX_XFER_SIZE - 1;
}

static int sun6i_spi_prepare_dma(struct sun6i_spi *sspi,
				 struct spi_transfer *tfr)
{
	struct dma_async_tx_descriptor *rxdesc, *txdesc;
	struct spi_master *master = sspi->master;

	rxdesc = NULL;
	if (tfr->rx_buf) {
		struct dma_slave_config rxconf = {
			.direction = DMA_DEV_TO_MEM,
			.src_addr = sspi->dma_addr_rx,
			.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
			.src_maxburst = 8,
		};

		dmaengine_slave_config(master->dma_rx, &rxconf);

		rxdesc = dmaengine_prep_slave_sg(master->dma_rx,
						 tfr->rx_sg.sgl,
						 tfr->rx_sg.nents,
						 DMA_DEV_TO_MEM,
						 DMA_PREP_INTERRUPT);
		if (!rxdesc)
			return -EINVAL;
	}

	txdesc = NULL;
	if (tfr->tx_buf) {
		struct dma_slave_config txconf = {
			.direction = DMA_MEM_TO_DEV,
			.dst_addr = sspi->dma_addr_tx,
			.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
			.dst_maxburst = 8,
		};

		dmaengine_slave_config(master->dma_tx, &txconf);

		txdesc = dmaengine_prep_slave_sg(master->dma_tx,
						 tfr->tx_sg.sgl,
						 tfr->tx_sg.nents,
						 DMA_MEM_TO_DEV,
						 DMA_PREP_INTERRUPT);
		if (!txdesc) {
			if (rxdesc)
				dmaengine_terminate_sync(master->dma_rx);
			return -EINVAL;
		}
	}

	if (tfr->rx_buf) {
		dmaengine_submit(rxdesc);
		dma_async_issue_pending(master->dma_rx);
	}

	if (tfr->tx_buf) {
		dmaengine_submit(txdesc);
		dma_async_issue_pending(master->dma_tx);
	}

	return 0;
}

static int sun6i_spi_transfer_one(struct spi_master *master,
				  struct spi_device *spi,
				  struct spi_transfer *tfr)
{
	struct sun6i_spi *sspi = spi_master_get_devdata(master);
	unsigned int div, div_cdr1, div_cdr2, timeout;
	unsigned int start, end, tx_time;
	unsigned int trig_level;
	unsigned int tx_len = 0, rx_len = 0, nbits = 0;
	bool use_dma;
	int ret = 0;
	u32 reg;

	if (tfr->len > SUN6I_MAX_XFER_SIZE)
		return -EINVAL;

	reinit_completion(&sspi->done);
	sspi->tx_buf = tfr->tx_buf;
	sspi->rx_buf = tfr->rx_buf;
	sspi->len = tfr->len;
	use_dma = master->can_dma ? master->can_dma(master, spi, tfr) : false;

	/* Clear pending interrupts */
	sun6i_spi_write(sspi, SUN6I_INT_STA_REG, ~0);

	/* Reset FIFO */
	sun6i_spi_write(sspi, SUN6I_FIFO_CTL_REG,
			SUN6I_FIFO_CTL_RF_RST | SUN6I_FIFO_CTL_TF_RST);

	reg = 0;

	if (!use_dma) {
		/*
		 * Setup FIFO interrupt trigger level
		 * Here we choose 3/4 of the full fifo depth, as it's
		 * the hardcoded value used in old generation of Allwinner
		 * SPI controller. (See spi-sun4i.c)
		 */
		trig_level = sspi->cfg->fifo_depth / 4 * 3;
	} else {
		/*
		 * Setup FIFO DMA request trigger level
		 * We choose 1/2 of the full fifo depth, that value will
		 * be used as DMA burst length.
		 */
		trig_level = sspi->cfg->fifo_depth / 2;

		if (tfr->tx_buf)
			reg |= SUN6I_FIFO_CTL_TF_DRQ_EN;
		if (tfr->rx_buf)
			reg |= SUN6I_FIFO_CTL_RF_DRQ_EN;
	}

	reg |= (trig_level << SUN6I_FIFO_CTL_RF_RDY_TRIG_LEVEL_BITS) |
	       (trig_level << SUN6I_FIFO_CTL_TF_ERQ_TRIG_LEVEL_BITS);

	sun6i_spi_write(sspi, SUN6I_FIFO_CTL_REG, reg);

	/*
	 * Setup the transfer control register: Chip Select,
	 * polarities, etc.
	 */
	reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);

	if (spi->mode & SPI_CPOL)
		reg |= SUN6I_TFR_CTL_CPOL;
	else
		reg &= ~SUN6I_TFR_CTL_CPOL;

	if (spi->mode & SPI_CPHA)
		reg |= SUN6I_TFR_CTL_CPHA;
	else
		reg &= ~SUN6I_TFR_CTL_CPHA;

	if (spi->mode & SPI_LSB_FIRST)
		reg |= SUN6I_TFR_CTL_FBS;
	else
		reg &= ~SUN6I_TFR_CTL_FBS;

	/*
	 * If it's a TX only transfer, we don't want to fill the RX
	 * FIFO with bogus data
	 */
	if (sspi->rx_buf) {
		reg &= ~SUN6I_TFR_CTL_DHB;
		rx_len = tfr->len;
	} else {
		reg |= SUN6I_TFR_CTL_DHB;
	}

	/* We want to control the chip select manually */
	reg |= SUN6I_TFR_CTL_CS_MANUAL;

	sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg);

	if (sspi->cfg->has_clk_ctl) {
		unsigned int mclk_rate = clk_get_rate(sspi->mclk);

		/* Ensure that we have a parent clock fast enough */
		if (mclk_rate < (2 * tfr->speed_hz)) {
			clk_set_rate(sspi->mclk, 2 * tfr->speed_hz);
			mclk_rate = clk_get_rate(sspi->mclk);
		}

		/*
		 * Setup clock divider.
		 *
		 * We have two choices there. Either we can use the clock
		 * divide rate 1, which is calculated thanks to this formula:
		 * SPI_CLK = MOD_CLK / (2 ^ cdr)
		 * Or we can use CDR2, which is calculated with the formula:
		 * SPI_CLK = MOD_CLK / (2 * (cdr + 1))
		 * Wether we use the former or the latter is set through the
		 * DRS bit.
		 *
		 * First try CDR2, and if we can't reach the expected
		 * frequency, fall back to CDR1.
		 */
		div_cdr1 = DIV_ROUND_UP(mclk_rate, tfr->speed_hz);
		div_cdr2 = DIV_ROUND_UP(div_cdr1, 2);
		if (div_cdr2 <= (SUN6I_CLK_CTL_CDR2_MASK + 1)) {
			reg = SUN6I_CLK_CTL_CDR2(div_cdr2 - 1) | SUN6I_CLK_CTL_DRS;
			tfr->effective_speed_hz = mclk_rate / (2 * div_cdr2);
		} else {
			div = min(SUN6I_CLK_CTL_CDR1_MASK, order_base_2(div_cdr1));
			reg = SUN6I_CLK_CTL_CDR1(div);
			tfr->effective_speed_hz = mclk_rate / (1 << div);
		}

		sun6i_spi_write(sspi, SUN6I_CLK_CTL_REG, reg);
	} else {
		clk_set_rate(sspi->mclk, tfr->speed_hz);
		tfr->effective_speed_hz = clk_get_rate(sspi->mclk);

		/*
		 * Configure work mode.
		 *
		 * There are three work modes depending on the controller clock
		 * frequency:
		 * - normal sample mode           : CLK <= 24MHz SDM=1 SDC=0
		 * - delay half-cycle sample mode : CLK <= 40MHz SDM=0 SDC=0
		 * - delay one-cycle sample mode  : CLK >= 80MHz SDM=0 SDC=1
		 */
		reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);
		reg &= ~(SUN6I_TFR_CTL_SDM | SUN6I_TFR_CTL_SDC);

		if (tfr->effective_speed_hz <= 24000000)
			reg |= SUN6I_TFR_CTL_SDM;
		else if (tfr->effective_speed_hz >= 80000000)
			reg |= SUN6I_TFR_CTL_SDC;

		sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg);
	}

	/* Finally enable the bus - doing so before might raise SCK to HIGH */
	reg = sun6i_spi_read(sspi, SUN6I_GBL_CTL_REG);
	reg |= SUN6I_GBL_CTL_BUS_ENABLE;
	sun6i_spi_write(sspi, SUN6I_GBL_CTL_REG, reg);

	/* Setup the transfer now... */
	if (sspi->tx_buf) {
		tx_len = tfr->len;
		nbits = tfr->tx_nbits;
	} else if (tfr->rx_buf) {
		nbits = tfr->rx_nbits;
	}

	switch (nbits) {
	case SPI_NBITS_DUAL:
		reg = SUN6I_BURST_CTL_CNT_DRM;
		break;
	case SPI_NBITS_QUAD:
		reg = SUN6I_BURST_CTL_CNT_QUAD_EN;
		break;
	case SPI_NBITS_SINGLE:
	default:
		reg = FIELD_PREP(SUN6I_BURST_CTL_CNT_STC_MASK, tx_len);
	}

	/* Setup the counters */
	sun6i_spi_write(sspi, SUN6I_BURST_CTL_CNT_REG, reg);
	sun6i_spi_write(sspi, SUN6I_BURST_CNT_REG, tfr->len);
	sun6i_spi_write(sspi, SUN6I_XMIT_CNT_REG, tx_len);

	if (!use_dma) {
		/* Fill the TX FIFO */
		sun6i_spi_fill_fifo(sspi);
	} else {
		ret = sun6i_spi_prepare_dma(sspi, tfr);
		if (ret) {
			dev_warn(&master->dev,
				 "%s: prepare DMA failed, ret=%d",
				 dev_name(&spi->dev), ret);
			return ret;
		}
	}

	/* Enable the interrupts */
	reg = SUN6I_INT_CTL_TC;

	if (!use_dma) {
		if (rx_len > sspi->cfg->fifo_depth)
			reg |= SUN6I_INT_CTL_RF_RDY;
		if (tx_len > sspi->cfg->fifo_depth)
			reg |= SUN6I_INT_CTL_TF_ERQ;
	}

	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, reg);

	/* Start the transfer */
	reg = sun6i_spi_read(sspi, SUN6I_TFR_CTL_REG);
	sun6i_spi_write(sspi, SUN6I_TFR_CTL_REG, reg | SUN6I_TFR_CTL_XCH);

	tx_time = spi_controller_xfer_timeout(master, tfr);
	start = jiffies;
	timeout = wait_for_completion_timeout(&sspi->done,
					      msecs_to_jiffies(tx_time));
	end = jiffies;
	if (!timeout) {
		dev_warn(&master->dev,
			 "%s: timeout transferring %u bytes@%iHz for %i(%i)ms",
			 dev_name(&spi->dev), tfr->len, tfr->speed_hz,
			 jiffies_to_msecs(end - start), tx_time);
		ret = -ETIMEDOUT;
	}

	sun6i_spi_write(sspi, SUN6I_INT_CTL_REG, 0);

	if (ret && use_dma) {
		dmaengine_terminate_sync(master->dma_rx);
		dmaengine_terminate_sync(master->dma_tx);
	}

	return ret;
}

static irqreturn_t sun6i_spi_handler(int irq, void *dev_id)
{
	struct sun6i_spi *sspi = dev_id;
	u32 status = sun6i_spi_read(sspi, SUN6I_INT_STA_REG);

	/* Transfer complete */
	if (status & SUN6I_INT_CTL_TC) {
		sun6i_spi_write(sspi, SUN6I_INT_STA_REG, SUN6I_INT_CTL_TC);
		sun6i_spi_drain_fifo(sspi);
		complete(&sspi->done);
		return IRQ_HANDLED;
	}

	/* Receive FIFO 3/4 full */
	if (status & SUN6I_INT_CTL_RF_RDY) {
		sun6i_spi_drain_fifo(sspi);
		/* Only clear the interrupt _after_ draining the FIFO */
		sun6i_spi_write(sspi, SUN6I_INT_STA_REG, SUN6I_INT_CTL_RF_RDY);
		return IRQ_HANDLED;
	}

	/* Transmit FIFO 3/4 empty */
	if (status & SUN6I_INT_CTL_TF_ERQ) {
		sun6i_spi_fill_fifo(sspi);

		if (!sspi->len)
			/* nothing left to transmit */
			sun6i_spi_disable_interrupt(sspi, SUN6I_INT_CTL_TF_ERQ);

		/* Only clear the interrupt _after_ re-seeding the FIFO */
		sun6i_spi_write(sspi, SUN6I_INT_STA_REG, SUN6I_INT_CTL_TF_ERQ);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int sun6i_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct sun6i_spi *sspi = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(sspi->hclk);
	if (ret) {
		dev_err(dev, "Couldn't enable AHB clock\n");
		goto out;
	}

	ret = clk_prepare_enable(sspi->mclk);
	if (ret) {
		dev_err(dev, "Couldn't enable module clock\n");
		goto err;
	}

	ret = reset_control_deassert(sspi->rstc);
	if (ret) {
		dev_err(dev, "Couldn't deassert the device from reset\n");
		goto err2;
	}

	sun6i_spi_write(sspi, SUN6I_GBL_CTL_REG,
			SUN6I_GBL_CTL_MASTER | SUN6I_GBL_CTL_TP);

	return 0;

err2:
	clk_disable_unprepare(sspi->mclk);
err:
	clk_disable_unprepare(sspi->hclk);
out:
	return ret;
}

static int sun6i_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct sun6i_spi *sspi = spi_master_get_devdata(master);

	reset_control_assert(sspi->rstc);
	clk_disable_unprepare(sspi->mclk);
	clk_disable_unprepare(sspi->hclk);

	return 0;
}

static bool sun6i_spi_can_dma(struct spi_master *master,
			      struct spi_device *spi,
			      struct spi_transfer *xfer)
{
	struct sun6i_spi *sspi = spi_master_get_devdata(master);

	/*
	 * If the number of spi words to transfer is less or equal than
	 * the fifo length we can just fill the fifo and wait for a single
	 * irq, so don't bother setting up dma
	 */
	return xfer->len > sspi->cfg->fifo_depth;
}

static int sun6i_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct sun6i_spi *sspi;
	struct resource *mem;
	int ret = 0, irq;

	master = spi_alloc_master(&pdev->dev, sizeof(struct sun6i_spi));
	if (!master) {
		dev_err(&pdev->dev, "Unable to allocate SPI Master\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);
	sspi = spi_master_get_devdata(master);

	sspi->base_addr = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(sspi->base_addr)) {
		ret = PTR_ERR(sspi->base_addr);
		goto err_free_master;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto err_free_master;
	}

	ret = devm_request_irq(&pdev->dev, irq, sun6i_spi_handler,
			       0, "sun6i-spi", sspi);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request IRQ\n");
		goto err_free_master;
	}

	sspi->master = master;
	sspi->cfg = of_device_get_match_data(&pdev->dev);

	master->max_speed_hz = 100 * 1000 * 1000;
	master->min_speed_hz = 3 * 1000;
	master->use_gpio_descriptors = true;
	master->set_cs = sun6i_spi_set_cs;
	master->transfer_one = sun6i_spi_transfer_one;
	master->num_chipselect = 4;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST |
			    sspi->cfg->mode_bits;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->dev.of_node = pdev->dev.of_node;
	master->auto_runtime_pm = true;
	master->max_transfer_size = sun6i_spi_max_transfer_size;

	sspi->hclk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(sspi->hclk)) {
		dev_err(&pdev->dev, "Unable to acquire AHB clock\n");
		ret = PTR_ERR(sspi->hclk);
		goto err_free_master;
	}

	sspi->mclk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(sspi->mclk)) {
		dev_err(&pdev->dev, "Unable to acquire module clock\n");
		ret = PTR_ERR(sspi->mclk);
		goto err_free_master;
	}

	init_completion(&sspi->done);

	sspi->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(sspi->rstc)) {
		dev_err(&pdev->dev, "Couldn't get reset controller\n");
		ret = PTR_ERR(sspi->rstc);
		goto err_free_master;
	}

	master->dma_tx = dma_request_chan(&pdev->dev, "tx");
	if (IS_ERR(master->dma_tx)) {
		/* Check tx to see if we need defer probing driver */
		if (PTR_ERR(master->dma_tx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_free_master;
		}
		dev_warn(&pdev->dev, "Failed to request TX DMA channel\n");
		master->dma_tx = NULL;
	}

	master->dma_rx = dma_request_chan(&pdev->dev, "rx");
	if (IS_ERR(master->dma_rx)) {
		if (PTR_ERR(master->dma_rx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto err_free_dma_tx;
		}
		dev_warn(&pdev->dev, "Failed to request RX DMA channel\n");
		master->dma_rx = NULL;
	}

	if (master->dma_tx && master->dma_rx) {
		sspi->dma_addr_tx = mem->start + SUN6I_TXDATA_REG;
		sspi->dma_addr_rx = mem->start + SUN6I_RXDATA_REG;
		master->can_dma = sun6i_spi_can_dma;
	}

	/*
	 * This wake-up/shutdown pattern is to be able to have the
	 * device woken up, even if runtime_pm is disabled
	 */
	ret = sun6i_spi_runtime_resume(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't resume the device\n");
		goto err_free_dma_rx;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, SUN6I_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		goto err_pm_disable;
	}

	return 0;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	sun6i_spi_runtime_suspend(&pdev->dev);
err_free_dma_rx:
	if (master->dma_rx)
		dma_release_channel(master->dma_rx);
err_free_dma_tx:
	if (master->dma_tx)
		dma_release_channel(master->dma_tx);
err_free_master:
	spi_master_put(master);
	return ret;
}

static void sun6i_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);

	pm_runtime_force_suspend(&pdev->dev);

	if (master->dma_tx)
		dma_release_channel(master->dma_tx);
	if (master->dma_rx)
		dma_release_channel(master->dma_rx);
}

static const struct sun6i_spi_cfg sun6i_a31_spi_cfg = {
	.fifo_depth	= SUN6I_FIFO_DEPTH,
	.has_clk_ctl	= true,
};

static const struct sun6i_spi_cfg sun8i_h3_spi_cfg = {
	.fifo_depth	= SUN8I_FIFO_DEPTH,
	.has_clk_ctl	= true,
};

static const struct sun6i_spi_cfg sun50i_r329_spi_cfg = {
	.fifo_depth	= SUN8I_FIFO_DEPTH,
	.mode_bits	= SPI_RX_DUAL | SPI_TX_DUAL | SPI_RX_QUAD | SPI_TX_QUAD,
};

static const struct of_device_id sun6i_spi_match[] = {
	{ .compatible = "allwinner,sun6i-a31-spi", .data = &sun6i_a31_spi_cfg },
	{ .compatible = "allwinner,sun8i-h3-spi",  .data = &sun8i_h3_spi_cfg },
	{
		.compatible = "allwinner,sun50i-r329-spi",
		.data = &sun50i_r329_spi_cfg
	},
	{}
};
MODULE_DEVICE_TABLE(of, sun6i_spi_match);

static const struct dev_pm_ops sun6i_spi_pm_ops = {
	.runtime_resume		= sun6i_spi_runtime_resume,
	.runtime_suspend	= sun6i_spi_runtime_suspend,
};

static struct platform_driver sun6i_spi_driver = {
	.probe	= sun6i_spi_probe,
	.remove_new = sun6i_spi_remove,
	.driver	= {
		.name		= "sun6i-spi",
		.of_match_table	= sun6i_spi_match,
		.pm		= &sun6i_spi_pm_ops,
	},
};
module_platform_driver(sun6i_spi_driver);

MODULE_AUTHOR("Pan Nan <pannan@allwinnertech.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A31 SPI controller driver");
MODULE_LICENSE("GPL");
