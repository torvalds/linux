// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Cirrus Logic EP93xx SPI controller.
 *
 * Copyright (C) 2010-2011 Mika Westerberg
 *
 * Explicit FIFO handling code was inspired by amba-pl022 driver.
 *
 * Chip select support using other than built-in GPIOs by H. Hartley Sweeten.
 *
 * For more information about the SPI controller see documentation on Cirrus
 * Logic web site:
 *     http://www.cirrus.com/en/pubs/manual/EP93xx_Users_Guide_UM1.pdf
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/scatterlist.h>
#include <linux/spi/spi.h>

#include <linux/platform_data/dma-ep93xx.h>
#include <linux/platform_data/spi-ep93xx.h>

#define SSPCR0			0x0000
#define SSPCR0_SPO		BIT(6)
#define SSPCR0_SPH		BIT(7)
#define SSPCR0_SCR_SHIFT	8

#define SSPCR1			0x0004
#define SSPCR1_RIE		BIT(0)
#define SSPCR1_TIE		BIT(1)
#define SSPCR1_RORIE		BIT(2)
#define SSPCR1_LBM		BIT(3)
#define SSPCR1_SSE		BIT(4)
#define SSPCR1_MS		BIT(5)
#define SSPCR1_SOD		BIT(6)

#define SSPDR			0x0008

#define SSPSR			0x000c
#define SSPSR_TFE		BIT(0)
#define SSPSR_TNF		BIT(1)
#define SSPSR_RNE		BIT(2)
#define SSPSR_RFF		BIT(3)
#define SSPSR_BSY		BIT(4)
#define SSPCPSR			0x0010

#define SSPIIR			0x0014
#define SSPIIR_RIS		BIT(0)
#define SSPIIR_TIS		BIT(1)
#define SSPIIR_RORIS		BIT(2)
#define SSPICR			SSPIIR

/* timeout in milliseconds */
#define SPI_TIMEOUT		5
/* maximum depth of RX/TX FIFO */
#define SPI_FIFO_SIZE		8

/**
 * struct ep93xx_spi - EP93xx SPI controller structure
 * @clk: clock for the controller
 * @mmio: pointer to ioremap()'d registers
 * @sspdr_phys: physical address of the SSPDR register
 * @tx: current byte in transfer to transmit
 * @rx: current byte in transfer to receive
 * @fifo_level: how full is FIFO (%0..%SPI_FIFO_SIZE - %1). Receiving one
 *              frame decreases this level and sending one frame increases it.
 * @dma_rx: RX DMA channel
 * @dma_tx: TX DMA channel
 * @dma_rx_data: RX parameters passed to the DMA engine
 * @dma_tx_data: TX parameters passed to the DMA engine
 * @rx_sgt: sg table for RX transfers
 * @tx_sgt: sg table for TX transfers
 * @zeropage: dummy page used as RX buffer when only TX buffer is passed in by
 *            the client
 */
struct ep93xx_spi {
	struct clk			*clk;
	void __iomem			*mmio;
	unsigned long			sspdr_phys;
	size_t				tx;
	size_t				rx;
	size_t				fifo_level;
	struct dma_chan			*dma_rx;
	struct dma_chan			*dma_tx;
	struct ep93xx_dma_data		dma_rx_data;
	struct ep93xx_dma_data		dma_tx_data;
	struct sg_table			rx_sgt;
	struct sg_table			tx_sgt;
	void				*zeropage;
};

/* converts bits per word to CR0.DSS value */
#define bits_per_word_to_dss(bpw)	((bpw) - 1)

/**
 * ep93xx_spi_calc_divisors() - calculates SPI clock divisors
 * @master: SPI master
 * @rate: desired SPI output clock rate
 * @div_cpsr: pointer to return the cpsr (pre-scaler) divider
 * @div_scr: pointer to return the scr divider
 */
static int ep93xx_spi_calc_divisors(struct spi_master *master,
				    u32 rate, u8 *div_cpsr, u8 *div_scr)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	unsigned long spi_clk_rate = clk_get_rate(espi->clk);
	int cpsr, scr;

	/*
	 * Make sure that max value is between values supported by the
	 * controller.
	 */
	rate = clamp(rate, master->min_speed_hz, master->max_speed_hz);

	/*
	 * Calculate divisors so that we can get speed according the
	 * following formula:
	 *	rate = spi_clock_rate / (cpsr * (1 + scr))
	 *
	 * cpsr must be even number and starts from 2, scr can be any number
	 * between 0 and 255.
	 */
	for (cpsr = 2; cpsr <= 254; cpsr += 2) {
		for (scr = 0; scr <= 255; scr++) {
			if ((spi_clk_rate / (cpsr * (scr + 1))) <= rate) {
				*div_scr = (u8)scr;
				*div_cpsr = (u8)cpsr;
				return 0;
			}
		}
	}

	return -EINVAL;
}

static int ep93xx_spi_chip_setup(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	u8 dss = bits_per_word_to_dss(xfer->bits_per_word);
	u8 div_cpsr = 0;
	u8 div_scr = 0;
	u16 cr0;
	int err;

	err = ep93xx_spi_calc_divisors(master, xfer->speed_hz,
				       &div_cpsr, &div_scr);
	if (err)
		return err;

	cr0 = div_scr << SSPCR0_SCR_SHIFT;
	if (spi->mode & SPI_CPOL)
		cr0 |= SSPCR0_SPO;
	if (spi->mode & SPI_CPHA)
		cr0 |= SSPCR0_SPH;
	cr0 |= dss;

	dev_dbg(&master->dev, "setup: mode %d, cpsr %d, scr %d, dss %d\n",
		spi->mode, div_cpsr, div_scr, dss);
	dev_dbg(&master->dev, "setup: cr0 %#x\n", cr0);

	writel(div_cpsr, espi->mmio + SSPCPSR);
	writel(cr0, espi->mmio + SSPCR0);

	return 0;
}

static void ep93xx_do_write(struct spi_master *master)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	struct spi_transfer *xfer = master->cur_msg->state;
	u32 val = 0;

	if (xfer->bits_per_word > 8) {
		if (xfer->tx_buf)
			val = ((u16 *)xfer->tx_buf)[espi->tx];
		espi->tx += 2;
	} else {
		if (xfer->tx_buf)
			val = ((u8 *)xfer->tx_buf)[espi->tx];
		espi->tx += 1;
	}
	writel(val, espi->mmio + SSPDR);
}

static void ep93xx_do_read(struct spi_master *master)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	struct spi_transfer *xfer = master->cur_msg->state;
	u32 val;

	val = readl(espi->mmio + SSPDR);
	if (xfer->bits_per_word > 8) {
		if (xfer->rx_buf)
			((u16 *)xfer->rx_buf)[espi->rx] = val;
		espi->rx += 2;
	} else {
		if (xfer->rx_buf)
			((u8 *)xfer->rx_buf)[espi->rx] = val;
		espi->rx += 1;
	}
}

/**
 * ep93xx_spi_read_write() - perform next RX/TX transfer
 * @espi: ep93xx SPI controller struct
 *
 * This function transfers next bytes (or half-words) to/from RX/TX FIFOs. If
 * called several times, the whole transfer will be completed. Returns
 * %-EINPROGRESS when current transfer was not yet completed otherwise %0.
 *
 * When this function is finished, RX FIFO should be empty and TX FIFO should be
 * full.
 */
static int ep93xx_spi_read_write(struct spi_master *master)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	struct spi_transfer *xfer = master->cur_msg->state;

	/* read as long as RX FIFO has frames in it */
	while ((readl(espi->mmio + SSPSR) & SSPSR_RNE)) {
		ep93xx_do_read(master);
		espi->fifo_level--;
	}

	/* write as long as TX FIFO has room */
	while (espi->fifo_level < SPI_FIFO_SIZE && espi->tx < xfer->len) {
		ep93xx_do_write(master);
		espi->fifo_level++;
	}

	if (espi->rx == xfer->len)
		return 0;

	return -EINPROGRESS;
}

static enum dma_transfer_direction
ep93xx_dma_data_to_trans_dir(enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		return DMA_MEM_TO_DEV;
	case DMA_FROM_DEVICE:
		return DMA_DEV_TO_MEM;
	default:
		return DMA_TRANS_NONE;
	}
}

/**
 * ep93xx_spi_dma_prepare() - prepares a DMA transfer
 * @master: SPI master
 * @dir: DMA transfer direction
 *
 * Function configures the DMA, maps the buffer and prepares the DMA
 * descriptor. Returns a valid DMA descriptor in case of success and ERR_PTR
 * in case of failure.
 */
static struct dma_async_tx_descriptor *
ep93xx_spi_dma_prepare(struct spi_master *master,
		       enum dma_data_direction dir)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	struct spi_transfer *xfer = master->cur_msg->state;
	struct dma_async_tx_descriptor *txd;
	enum dma_slave_buswidth buswidth;
	struct dma_slave_config conf;
	struct scatterlist *sg;
	struct sg_table *sgt;
	struct dma_chan *chan;
	const void *buf, *pbuf;
	size_t len = xfer->len;
	int i, ret, nents;

	if (xfer->bits_per_word > 8)
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
	else
		buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;

	memset(&conf, 0, sizeof(conf));
	conf.direction = ep93xx_dma_data_to_trans_dir(dir);

	if (dir == DMA_FROM_DEVICE) {
		chan = espi->dma_rx;
		buf = xfer->rx_buf;
		sgt = &espi->rx_sgt;

		conf.src_addr = espi->sspdr_phys;
		conf.src_addr_width = buswidth;
	} else {
		chan = espi->dma_tx;
		buf = xfer->tx_buf;
		sgt = &espi->tx_sgt;

		conf.dst_addr = espi->sspdr_phys;
		conf.dst_addr_width = buswidth;
	}

	ret = dmaengine_slave_config(chan, &conf);
	if (ret)
		return ERR_PTR(ret);

	/*
	 * We need to split the transfer into PAGE_SIZE'd chunks. This is
	 * because we are using @espi->zeropage to provide a zero RX buffer
	 * for the TX transfers and we have only allocated one page for that.
	 *
	 * For performance reasons we allocate a new sg_table only when
	 * needed. Otherwise we will re-use the current one. Eventually the
	 * last sg_table is released in ep93xx_spi_release_dma().
	 */

	nents = DIV_ROUND_UP(len, PAGE_SIZE);
	if (nents != sgt->nents) {
		sg_free_table(sgt);

		ret = sg_alloc_table(sgt, nents, GFP_KERNEL);
		if (ret)
			return ERR_PTR(ret);
	}

	pbuf = buf;
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		size_t bytes = min_t(size_t, len, PAGE_SIZE);

		if (buf) {
			sg_set_page(sg, virt_to_page(pbuf), bytes,
				    offset_in_page(pbuf));
		} else {
			sg_set_page(sg, virt_to_page(espi->zeropage),
				    bytes, 0);
		}

		pbuf += bytes;
		len -= bytes;
	}

	if (WARN_ON(len)) {
		dev_warn(&master->dev, "len = %zu expected 0!\n", len);
		return ERR_PTR(-EINVAL);
	}

	nents = dma_map_sg(chan->device->dev, sgt->sgl, sgt->nents, dir);
	if (!nents)
		return ERR_PTR(-ENOMEM);

	txd = dmaengine_prep_slave_sg(chan, sgt->sgl, nents, conf.direction,
				      DMA_CTRL_ACK);
	if (!txd) {
		dma_unmap_sg(chan->device->dev, sgt->sgl, sgt->nents, dir);
		return ERR_PTR(-ENOMEM);
	}
	return txd;
}

/**
 * ep93xx_spi_dma_finish() - finishes with a DMA transfer
 * @master: SPI master
 * @dir: DMA transfer direction
 *
 * Function finishes with the DMA transfer. After this, the DMA buffer is
 * unmapped.
 */
static void ep93xx_spi_dma_finish(struct spi_master *master,
				  enum dma_data_direction dir)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	struct dma_chan *chan;
	struct sg_table *sgt;

	if (dir == DMA_FROM_DEVICE) {
		chan = espi->dma_rx;
		sgt = &espi->rx_sgt;
	} else {
		chan = espi->dma_tx;
		sgt = &espi->tx_sgt;
	}

	dma_unmap_sg(chan->device->dev, sgt->sgl, sgt->nents, dir);
}

static void ep93xx_spi_dma_callback(void *callback_param)
{
	struct spi_master *master = callback_param;

	ep93xx_spi_dma_finish(master, DMA_TO_DEVICE);
	ep93xx_spi_dma_finish(master, DMA_FROM_DEVICE);

	spi_finalize_current_transfer(master);
}

static int ep93xx_spi_dma_transfer(struct spi_master *master)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	struct dma_async_tx_descriptor *rxd, *txd;

	rxd = ep93xx_spi_dma_prepare(master, DMA_FROM_DEVICE);
	if (IS_ERR(rxd)) {
		dev_err(&master->dev, "DMA RX failed: %ld\n", PTR_ERR(rxd));
		return PTR_ERR(rxd);
	}

	txd = ep93xx_spi_dma_prepare(master, DMA_TO_DEVICE);
	if (IS_ERR(txd)) {
		ep93xx_spi_dma_finish(master, DMA_FROM_DEVICE);
		dev_err(&master->dev, "DMA TX failed: %ld\n", PTR_ERR(txd));
		return PTR_ERR(txd);
	}

	/* We are ready when RX is done */
	rxd->callback = ep93xx_spi_dma_callback;
	rxd->callback_param = master;

	/* Now submit both descriptors and start DMA */
	dmaengine_submit(rxd);
	dmaengine_submit(txd);

	dma_async_issue_pending(espi->dma_rx);
	dma_async_issue_pending(espi->dma_tx);

	/* signal that we need to wait for completion */
	return 1;
}

static irqreturn_t ep93xx_spi_interrupt(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	u32 val;

	/*
	 * If we got ROR (receive overrun) interrupt we know that something is
	 * wrong. Just abort the message.
	 */
	if (readl(espi->mmio + SSPIIR) & SSPIIR_RORIS) {
		/* clear the overrun interrupt */
		writel(0, espi->mmio + SSPICR);
		dev_warn(&master->dev,
			 "receive overrun, aborting the message\n");
		master->cur_msg->status = -EIO;
	} else {
		/*
		 * Interrupt is either RX (RIS) or TX (TIS). For both cases we
		 * simply execute next data transfer.
		 */
		if (ep93xx_spi_read_write(master)) {
			/*
			 * In normal case, there still is some processing left
			 * for current transfer. Let's wait for the next
			 * interrupt then.
			 */
			return IRQ_HANDLED;
		}
	}

	/*
	 * Current transfer is finished, either with error or with success. In
	 * any case we disable interrupts and notify the worker to handle
	 * any post-processing of the message.
	 */
	val = readl(espi->mmio + SSPCR1);
	val &= ~(SSPCR1_RORIE | SSPCR1_TIE | SSPCR1_RIE);
	writel(val, espi->mmio + SSPCR1);

	spi_finalize_current_transfer(master);

	return IRQ_HANDLED;
}

static int ep93xx_spi_transfer_one(struct spi_master *master,
				   struct spi_device *spi,
				   struct spi_transfer *xfer)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	u32 val;
	int ret;

	ret = ep93xx_spi_chip_setup(master, spi, xfer);
	if (ret) {
		dev_err(&master->dev, "failed to setup chip for transfer\n");
		return ret;
	}

	master->cur_msg->state = xfer;
	espi->rx = 0;
	espi->tx = 0;

	/*
	 * There is no point of setting up DMA for the transfers which will
	 * fit into the FIFO and can be transferred with a single interrupt.
	 * So in these cases we will be using PIO and don't bother for DMA.
	 */
	if (espi->dma_rx && xfer->len > SPI_FIFO_SIZE)
		return ep93xx_spi_dma_transfer(master);

	/* Using PIO so prime the TX FIFO and enable interrupts */
	ep93xx_spi_read_write(master);

	val = readl(espi->mmio + SSPCR1);
	val |= (SSPCR1_RORIE | SSPCR1_TIE | SSPCR1_RIE);
	writel(val, espi->mmio + SSPCR1);

	/* signal that we need to wait for completion */
	return 1;
}

static int ep93xx_spi_prepare_message(struct spi_master *master,
				      struct spi_message *msg)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	unsigned long timeout;

	/*
	 * Just to be sure: flush any data from RX FIFO.
	 */
	timeout = jiffies + msecs_to_jiffies(SPI_TIMEOUT);
	while (readl(espi->mmio + SSPSR) & SSPSR_RNE) {
		if (time_after(jiffies, timeout)) {
			dev_warn(&master->dev,
				 "timeout while flushing RX FIFO\n");
			return -ETIMEDOUT;
		}
		readl(espi->mmio + SSPDR);
	}

	/*
	 * We explicitly handle FIFO level. This way we don't have to check TX
	 * FIFO status using %SSPSR_TNF bit which may cause RX FIFO overruns.
	 */
	espi->fifo_level = 0;

	return 0;
}

static int ep93xx_spi_prepare_hardware(struct spi_master *master)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	u32 val;
	int ret;

	ret = clk_enable(espi->clk);
	if (ret)
		return ret;

	val = readl(espi->mmio + SSPCR1);
	val |= SSPCR1_SSE;
	writel(val, espi->mmio + SSPCR1);

	return 0;
}

static int ep93xx_spi_unprepare_hardware(struct spi_master *master)
{
	struct ep93xx_spi *espi = spi_master_get_devdata(master);
	u32 val;

	val = readl(espi->mmio + SSPCR1);
	val &= ~SSPCR1_SSE;
	writel(val, espi->mmio + SSPCR1);

	clk_disable(espi->clk);

	return 0;
}

static bool ep93xx_spi_dma_filter(struct dma_chan *chan, void *filter_param)
{
	if (ep93xx_dma_chan_is_m2p(chan))
		return false;

	chan->private = filter_param;
	return true;
}

static int ep93xx_spi_setup_dma(struct ep93xx_spi *espi)
{
	dma_cap_mask_t mask;
	int ret;

	espi->zeropage = (void *)get_zeroed_page(GFP_KERNEL);
	if (!espi->zeropage)
		return -ENOMEM;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	espi->dma_rx_data.port = EP93XX_DMA_SSP;
	espi->dma_rx_data.direction = DMA_DEV_TO_MEM;
	espi->dma_rx_data.name = "ep93xx-spi-rx";

	espi->dma_rx = dma_request_channel(mask, ep93xx_spi_dma_filter,
					   &espi->dma_rx_data);
	if (!espi->dma_rx) {
		ret = -ENODEV;
		goto fail_free_page;
	}

	espi->dma_tx_data.port = EP93XX_DMA_SSP;
	espi->dma_tx_data.direction = DMA_MEM_TO_DEV;
	espi->dma_tx_data.name = "ep93xx-spi-tx";

	espi->dma_tx = dma_request_channel(mask, ep93xx_spi_dma_filter,
					   &espi->dma_tx_data);
	if (!espi->dma_tx) {
		ret = -ENODEV;
		goto fail_release_rx;
	}

	return 0;

fail_release_rx:
	dma_release_channel(espi->dma_rx);
	espi->dma_rx = NULL;
fail_free_page:
	free_page((unsigned long)espi->zeropage);

	return ret;
}

static void ep93xx_spi_release_dma(struct ep93xx_spi *espi)
{
	if (espi->dma_rx) {
		dma_release_channel(espi->dma_rx);
		sg_free_table(&espi->rx_sgt);
	}
	if (espi->dma_tx) {
		dma_release_channel(espi->dma_tx);
		sg_free_table(&espi->tx_sgt);
	}

	if (espi->zeropage)
		free_page((unsigned long)espi->zeropage);
}

static int ep93xx_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct ep93xx_spi_info *info;
	struct ep93xx_spi *espi;
	struct resource *res;
	int irq;
	int error;

	info = dev_get_platdata(&pdev->dev);
	if (!info) {
		dev_err(&pdev->dev, "missing platform data\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "unable to get iomem resource\n");
		return -ENODEV;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*espi));
	if (!master)
		return -ENOMEM;

	master->use_gpio_descriptors = true;
	master->prepare_transfer_hardware = ep93xx_spi_prepare_hardware;
	master->unprepare_transfer_hardware = ep93xx_spi_unprepare_hardware;
	master->prepare_message = ep93xx_spi_prepare_message;
	master->transfer_one = ep93xx_spi_transfer_one;
	master->bus_num = pdev->id;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);
	/*
	 * The SPI core will count the number of GPIO descriptors to figure
	 * out the number of chip selects available on the platform.
	 */
	master->num_chipselect = 0;

	platform_set_drvdata(pdev, master);

	espi = spi_master_get_devdata(master);

	espi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(espi->clk)) {
		dev_err(&pdev->dev, "unable to get spi clock\n");
		error = PTR_ERR(espi->clk);
		goto fail_release_master;
	}

	/*
	 * Calculate maximum and minimum supported clock rates
	 * for the controller.
	 */
	master->max_speed_hz = clk_get_rate(espi->clk) / 2;
	master->min_speed_hz = clk_get_rate(espi->clk) / (254 * 256);

	espi->sspdr_phys = res->start + SSPDR;

	espi->mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(espi->mmio)) {
		error = PTR_ERR(espi->mmio);
		goto fail_release_master;
	}

	error = devm_request_irq(&pdev->dev, irq, ep93xx_spi_interrupt,
				0, "ep93xx-spi", master);
	if (error) {
		dev_err(&pdev->dev, "failed to request irq\n");
		goto fail_release_master;
	}

	if (info->use_dma && ep93xx_spi_setup_dma(espi))
		dev_warn(&pdev->dev, "DMA setup failed. Falling back to PIO\n");

	/* make sure that the hardware is disabled */
	writel(0, espi->mmio + SSPCR1);

	error = devm_spi_register_master(&pdev->dev, master);
	if (error) {
		dev_err(&pdev->dev, "failed to register SPI master\n");
		goto fail_free_dma;
	}

	dev_info(&pdev->dev, "EP93xx SPI Controller at 0x%08lx irq %d\n",
		 (unsigned long)res->start, irq);

	return 0;

fail_free_dma:
	ep93xx_spi_release_dma(espi);
fail_release_master:
	spi_master_put(master);

	return error;
}

static int ep93xx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct ep93xx_spi *espi = spi_master_get_devdata(master);

	ep93xx_spi_release_dma(espi);

	return 0;
}

static struct platform_driver ep93xx_spi_driver = {
	.driver		= {
		.name	= "ep93xx-spi",
	},
	.probe		= ep93xx_spi_probe,
	.remove		= ep93xx_spi_remove,
};
module_platform_driver(ep93xx_spi_driver);

MODULE_DESCRIPTION("EP93xx SPI Controller driver");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@iki.fi>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-spi");
