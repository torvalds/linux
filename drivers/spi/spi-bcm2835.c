/*
 * Driver for Broadcom BCM2835 SPI Controllers
 *
 * Copyright (C) 2012 Chris Boot
 * Copyright (C) 2013 Stephen Warren
 * Copyright (C) 2015 Martin Sperl
 *
 * This driver is inspired by:
 * spi-ath79.c, Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 * spi-atmel.c, Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/page.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/spi/spi.h>

/* SPI register offsets */
#define BCM2835_SPI_CS			0x00
#define BCM2835_SPI_FIFO		0x04
#define BCM2835_SPI_CLK			0x08
#define BCM2835_SPI_DLEN		0x0c
#define BCM2835_SPI_LTOH		0x10
#define BCM2835_SPI_DC			0x14

/* Bitfields in CS */
#define BCM2835_SPI_CS_LEN_LONG		0x02000000
#define BCM2835_SPI_CS_DMA_LEN		0x01000000
#define BCM2835_SPI_CS_CSPOL2		0x00800000
#define BCM2835_SPI_CS_CSPOL1		0x00400000
#define BCM2835_SPI_CS_CSPOL0		0x00200000
#define BCM2835_SPI_CS_RXF		0x00100000
#define BCM2835_SPI_CS_RXR		0x00080000
#define BCM2835_SPI_CS_TXD		0x00040000
#define BCM2835_SPI_CS_RXD		0x00020000
#define BCM2835_SPI_CS_DONE		0x00010000
#define BCM2835_SPI_CS_LEN		0x00002000
#define BCM2835_SPI_CS_REN		0x00001000
#define BCM2835_SPI_CS_ADCS		0x00000800
#define BCM2835_SPI_CS_INTR		0x00000400
#define BCM2835_SPI_CS_INTD		0x00000200
#define BCM2835_SPI_CS_DMAEN		0x00000100
#define BCM2835_SPI_CS_TA		0x00000080
#define BCM2835_SPI_CS_CSPOL		0x00000040
#define BCM2835_SPI_CS_CLEAR_RX		0x00000020
#define BCM2835_SPI_CS_CLEAR_TX		0x00000010
#define BCM2835_SPI_CS_CPOL		0x00000008
#define BCM2835_SPI_CS_CPHA		0x00000004
#define BCM2835_SPI_CS_CS_10		0x00000002
#define BCM2835_SPI_CS_CS_01		0x00000001

#define BCM2835_SPI_POLLING_LIMIT_US	30
#define BCM2835_SPI_POLLING_JIFFIES	2
#define BCM2835_SPI_DMA_MIN_LENGTH	96
#define BCM2835_SPI_MODE_BITS	(SPI_CPOL | SPI_CPHA | SPI_CS_HIGH \
				| SPI_NO_CS | SPI_3WIRE)

#define DRV_NAME	"spi-bcm2835"

struct bcm2835_spi {
	void __iomem *regs;
	struct clk *clk;
	int irq;
	const u8 *tx_buf;
	u8 *rx_buf;
	int tx_len;
	int rx_len;
	unsigned int dma_pending;
};

static inline u32 bcm2835_rd(struct bcm2835_spi *bs, unsigned reg)
{
	return readl(bs->regs + reg);
}

static inline void bcm2835_wr(struct bcm2835_spi *bs, unsigned reg, u32 val)
{
	writel(val, bs->regs + reg);
}

static inline void bcm2835_rd_fifo(struct bcm2835_spi *bs)
{
	u8 byte;

	while ((bs->rx_len) &&
	       (bcm2835_rd(bs, BCM2835_SPI_CS) & BCM2835_SPI_CS_RXD)) {
		byte = bcm2835_rd(bs, BCM2835_SPI_FIFO);
		if (bs->rx_buf)
			*bs->rx_buf++ = byte;
		bs->rx_len--;
	}
}

static inline void bcm2835_wr_fifo(struct bcm2835_spi *bs)
{
	u8 byte;

	while ((bs->tx_len) &&
	       (bcm2835_rd(bs, BCM2835_SPI_CS) & BCM2835_SPI_CS_TXD)) {
		byte = bs->tx_buf ? *bs->tx_buf++ : 0;
		bcm2835_wr(bs, BCM2835_SPI_FIFO, byte);
		bs->tx_len--;
	}
}

static void bcm2835_spi_reset_hw(struct spi_master *master)
{
	struct bcm2835_spi *bs = spi_master_get_devdata(master);
	u32 cs = bcm2835_rd(bs, BCM2835_SPI_CS);

	/* Disable SPI interrupts and transfer */
	cs &= ~(BCM2835_SPI_CS_INTR |
		BCM2835_SPI_CS_INTD |
		BCM2835_SPI_CS_DMAEN |
		BCM2835_SPI_CS_TA);
	/* and reset RX/TX FIFOS */
	cs |= BCM2835_SPI_CS_CLEAR_RX | BCM2835_SPI_CS_CLEAR_TX;

	/* and reset the SPI_HW */
	bcm2835_wr(bs, BCM2835_SPI_CS, cs);
	/* as well as DLEN */
	bcm2835_wr(bs, BCM2835_SPI_DLEN, 0);
}

static irqreturn_t bcm2835_spi_interrupt(int irq, void *dev_id)
{
	struct spi_master *master = dev_id;
	struct bcm2835_spi *bs = spi_master_get_devdata(master);

	/* Read as many bytes as possible from FIFO */
	bcm2835_rd_fifo(bs);
	/* Write as many bytes as possible to FIFO */
	bcm2835_wr_fifo(bs);

	if (!bs->rx_len) {
		/* Transfer complete - reset SPI HW */
		bcm2835_spi_reset_hw(master);
		/* wake up the framework */
		complete(&master->xfer_completion);
	}

	return IRQ_HANDLED;
}

static int bcm2835_spi_transfer_one_irq(struct spi_master *master,
					struct spi_device *spi,
					struct spi_transfer *tfr,
					u32 cs)
{
	struct bcm2835_spi *bs = spi_master_get_devdata(master);

	/* fill in fifo if we have gpio-cs
	 * note that there have been rare events where the native-CS
	 * flapped for <1us which may change the behaviour
	 * with gpio-cs this does not happen, so it is implemented
	 * only for this case
	 */
	if (gpio_is_valid(spi->cs_gpio)) {
		/* enable HW block, but without interrupts enabled
		 * this would triggern an immediate interrupt
		 */
		bcm2835_wr(bs, BCM2835_SPI_CS,
			   cs | BCM2835_SPI_CS_TA);
		/* fill in tx fifo as much as possible */
		bcm2835_wr_fifo(bs);
	}

	/*
	 * Enable the HW block. This will immediately trigger a DONE (TX
	 * empty) interrupt, upon which we will fill the TX FIFO with the
	 * first TX bytes. Pre-filling the TX FIFO here to avoid the
	 * interrupt doesn't work:-(
	 */
	cs |= BCM2835_SPI_CS_INTR | BCM2835_SPI_CS_INTD | BCM2835_SPI_CS_TA;
	bcm2835_wr(bs, BCM2835_SPI_CS, cs);

	/* signal that we need to wait for completion */
	return 1;
}

/*
 * DMA support
 *
 * this implementation has currently a few issues in so far as it does
 * not work arrount limitations of the HW.
 *
 * the main one being that DMA transfers are limited to 16 bit
 * (so 0 to 65535 bytes) by the SPI HW due to BCM2835_SPI_DLEN
 *
 * also we currently assume that the scatter-gather fragments are
 * all multiple of 4 (except the last) - otherwise we would need
 * to reset the FIFO before subsequent transfers...
 * this also means that tx/rx transfers sg's need to be of equal size!
 *
 * there may be a few more border-cases we may need to address as well
 * but unfortunately this would mean splitting up the scatter-gather
 * list making it slightly unpractical...
 */
static void bcm2835_spi_dma_done(void *data)
{
	struct spi_master *master = data;
	struct bcm2835_spi *bs = spi_master_get_devdata(master);

	/* reset fifo and HW */
	bcm2835_spi_reset_hw(master);

	/* and terminate tx-dma as we do not have an irq for it
	 * because when the rx dma will terminate and this callback
	 * is called the tx-dma must have finished - can't get to this
	 * situation otherwise...
	 */
	if (cmpxchg(&bs->dma_pending, true, false)) {
		dmaengine_terminate_all(master->dma_tx);
	}

	/* and mark as completed */;
	complete(&master->xfer_completion);
}

static int bcm2835_spi_prepare_sg(struct spi_master *master,
				  struct spi_transfer *tfr,
				  bool is_tx)
{
	struct dma_chan *chan;
	struct scatterlist *sgl;
	unsigned int nents;
	enum dma_transfer_direction dir;
	unsigned long flags;

	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;

	if (is_tx) {
		dir   = DMA_MEM_TO_DEV;
		chan  = master->dma_tx;
		nents = tfr->tx_sg.nents;
		sgl   = tfr->tx_sg.sgl;
		flags = 0 /* no  tx interrupt */;

	} else {
		dir   = DMA_DEV_TO_MEM;
		chan  = master->dma_rx;
		nents = tfr->rx_sg.nents;
		sgl   = tfr->rx_sg.sgl;
		flags = DMA_PREP_INTERRUPT;
	}
	/* prepare the channel */
	desc = dmaengine_prep_slave_sg(chan, sgl, nents, dir, flags);
	if (!desc)
		return -EINVAL;

	/* set callback for rx */
	if (!is_tx) {
		desc->callback = bcm2835_spi_dma_done;
		desc->callback_param = master;
	}

	/* submit it to DMA-engine */
	cookie = dmaengine_submit(desc);

	return dma_submit_error(cookie);
}

static inline int bcm2835_check_sg_length(struct sg_table *sgt)
{
	int i;
	struct scatterlist *sgl;

	/* check that the sg entries are word-sized (except for last) */
	for_each_sg(sgt->sgl, sgl, (int)sgt->nents - 1, i) {
		if (sg_dma_len(sgl) % 4)
			return -EFAULT;
	}

	return 0;
}

static int bcm2835_spi_transfer_one_dma(struct spi_master *master,
					struct spi_device *spi,
					struct spi_transfer *tfr,
					u32 cs)
{
	struct bcm2835_spi *bs = spi_master_get_devdata(master);
	int ret;

	/* check that the scatter gather segments are all a multiple of 4 */
	if (bcm2835_check_sg_length(&tfr->tx_sg) ||
	    bcm2835_check_sg_length(&tfr->rx_sg)) {
		dev_warn_once(&spi->dev,
			      "scatter gather segment length is not a multiple of 4 - falling back to interrupt mode\n");
		return bcm2835_spi_transfer_one_irq(master, spi, tfr, cs);
	}

	/* setup tx-DMA */
	ret = bcm2835_spi_prepare_sg(master, tfr, true);
	if (ret)
		return ret;

	/* start TX early */
	dma_async_issue_pending(master->dma_tx);

	/* mark as dma pending */
	bs->dma_pending = 1;

	/* set the DMA length */
	bcm2835_wr(bs, BCM2835_SPI_DLEN, tfr->len);

	/* start the HW */
	bcm2835_wr(bs, BCM2835_SPI_CS,
		   cs | BCM2835_SPI_CS_TA | BCM2835_SPI_CS_DMAEN);

	/* setup rx-DMA late - to run transfers while
	 * mapping of the rx buffers still takes place
	 * this saves 10us or more.
	 */
	ret = bcm2835_spi_prepare_sg(master, tfr, false);
	if (ret) {
		/* need to reset on errors */
		dmaengine_terminate_all(master->dma_tx);
		bs->dma_pending = false;
		bcm2835_spi_reset_hw(master);
		return ret;
	}

	/* start rx dma late */
	dma_async_issue_pending(master->dma_rx);

	/* wait for wakeup in framework */
	return 1;
}

static bool bcm2835_spi_can_dma(struct spi_master *master,
				struct spi_device *spi,
				struct spi_transfer *tfr)
{
	/* only run for gpio_cs */
	if (!gpio_is_valid(spi->cs_gpio))
		return false;

	/* we start DMA efforts only on bigger transfers */
	if (tfr->len < BCM2835_SPI_DMA_MIN_LENGTH)
		return false;

	/* BCM2835_SPI_DLEN has defined a max transfer size as
	 * 16 bit, so max is 65535
	 * we can revisit this by using an alternative transfer
	 * method - ideally this would get done without any more
	 * interaction...
	 */
	if (tfr->len > 65535) {
		dev_warn_once(&spi->dev,
			      "transfer size of %d too big for dma-transfer\n",
			      tfr->len);
		return false;
	}

	/* if we run rx/tx_buf with word aligned addresses then we are OK */
	if ((((size_t)tfr->rx_buf & 3) == 0) &&
	    (((size_t)tfr->tx_buf & 3) == 0))
		return true;

	/* otherwise we only allow transfers within the same page
	 * to avoid wasting time on dma_mapping when it is not practical
	 */
	if (((size_t)tfr->tx_buf & (PAGE_SIZE - 1)) + tfr->len > PAGE_SIZE) {
		dev_warn_once(&spi->dev,
			      "Unaligned spi tx-transfer bridging page\n");
		return false;
	}
	if (((size_t)tfr->rx_buf & (PAGE_SIZE - 1)) + tfr->len > PAGE_SIZE) {
		dev_warn_once(&spi->dev,
			      "Unaligned spi rx-transfer bridging page\n");
		return false;
	}

	/* return OK */
	return true;
}

static void bcm2835_dma_release(struct spi_master *master)
{
	if (master->dma_tx) {
		dmaengine_terminate_all(master->dma_tx);
		dma_release_channel(master->dma_tx);
		master->dma_tx = NULL;
	}
	if (master->dma_rx) {
		dmaengine_terminate_all(master->dma_rx);
		dma_release_channel(master->dma_rx);
		master->dma_rx = NULL;
	}
}

static void bcm2835_dma_init(struct spi_master *master, struct device *dev)
{
	struct dma_slave_config slave_config;
	const __be32 *addr;
	dma_addr_t dma_reg_base;
	int ret;

	/* base address in dma-space */
	addr = of_get_address(master->dev.of_node, 0, NULL, NULL);
	if (!addr) {
		dev_err(dev, "could not get DMA-register address - not using dma mode\n");
		goto err;
	}
	dma_reg_base = be32_to_cpup(addr);

	/* get tx/rx dma */
	master->dma_tx = dma_request_slave_channel(dev, "tx");
	if (!master->dma_tx) {
		dev_err(dev, "no tx-dma configuration found - not using dma mode\n");
		goto err;
	}
	master->dma_rx = dma_request_slave_channel(dev, "rx");
	if (!master->dma_rx) {
		dev_err(dev, "no rx-dma configuration found - not using dma mode\n");
		goto err_release;
	}

	/* configure DMAs */
	slave_config.direction = DMA_MEM_TO_DEV;
	slave_config.dst_addr = (u32)(dma_reg_base + BCM2835_SPI_FIFO);
	slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	ret = dmaengine_slave_config(master->dma_tx, &slave_config);
	if (ret)
		goto err_config;

	slave_config.direction = DMA_DEV_TO_MEM;
	slave_config.src_addr = (u32)(dma_reg_base + BCM2835_SPI_FIFO);
	slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

	ret = dmaengine_slave_config(master->dma_rx, &slave_config);
	if (ret)
		goto err_config;

	/* all went well, so set can_dma */
	master->can_dma = bcm2835_spi_can_dma;
	master->max_dma_len = 65535; /* limitation by BCM2835_SPI_DLEN */
	/* need to do TX AND RX DMA, so we need dummy buffers */
	master->flags = SPI_MASTER_MUST_RX | SPI_MASTER_MUST_TX;

	return;

err_config:
	dev_err(dev, "issue configuring dma: %d - not using DMA mode\n",
		ret);
err_release:
	bcm2835_dma_release(master);
err:
	return;
}

static int bcm2835_spi_transfer_one_poll(struct spi_master *master,
					 struct spi_device *spi,
					 struct spi_transfer *tfr,
					 u32 cs,
					 unsigned long long xfer_time_us)
{
	struct bcm2835_spi *bs = spi_master_get_devdata(master);
	unsigned long timeout;

	/* enable HW block without interrupts */
	bcm2835_wr(bs, BCM2835_SPI_CS, cs | BCM2835_SPI_CS_TA);

	/* fill in the fifo before timeout calculations
	 * if we are interrupted here, then the data is
	 * getting transferred by the HW while we are interrupted
	 */
	bcm2835_wr_fifo(bs);

	/* set the timeout */
	timeout = jiffies + BCM2835_SPI_POLLING_JIFFIES;

	/* loop until finished the transfer */
	while (bs->rx_len) {
		/* fill in tx fifo with remaining data */
		bcm2835_wr_fifo(bs);

		/* read from fifo as much as possible */
		bcm2835_rd_fifo(bs);

		/* if there is still data pending to read
		 * then check the timeout
		 */
		if (bs->rx_len && time_after(jiffies, timeout)) {
			dev_dbg_ratelimited(&spi->dev,
					    "timeout period reached: jiffies: %lu remaining tx/rx: %d/%d - falling back to interrupt mode\n",
					    jiffies - timeout,
					    bs->tx_len, bs->rx_len);
			/* fall back to interrupt mode */
			return bcm2835_spi_transfer_one_irq(master, spi,
							    tfr, cs);
		}
	}

	/* Transfer complete - reset SPI HW */
	bcm2835_spi_reset_hw(master);
	/* and return without waiting for completion */
	return 0;
}

static int bcm2835_spi_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *tfr)
{
	struct bcm2835_spi *bs = spi_master_get_devdata(master);
	unsigned long spi_hz, clk_hz, cdiv;
	unsigned long spi_used_hz;
	unsigned long long xfer_time_us;
	u32 cs = bcm2835_rd(bs, BCM2835_SPI_CS);

	/* set clock */
	spi_hz = tfr->speed_hz;
	clk_hz = clk_get_rate(bs->clk);

	if (spi_hz >= clk_hz / 2) {
		cdiv = 2; /* clk_hz/2 is the fastest we can go */
	} else if (spi_hz) {
		/* CDIV must be a multiple of two */
		cdiv = DIV_ROUND_UP(clk_hz, spi_hz);
		cdiv += (cdiv % 2);

		if (cdiv >= 65536)
			cdiv = 0; /* 0 is the slowest we can go */
	} else {
		cdiv = 0; /* 0 is the slowest we can go */
	}
	spi_used_hz = cdiv ? (clk_hz / cdiv) : (clk_hz / 65536);
	bcm2835_wr(bs, BCM2835_SPI_CLK, cdiv);

	/* handle all the 3-wire mode */
	if (spi->mode & SPI_3WIRE && tfr->rx_buf &&
	    tfr->rx_buf != master->dummy_rx)
		cs |= BCM2835_SPI_CS_REN;
	else
		cs &= ~BCM2835_SPI_CS_REN;

	/* for gpio_cs set dummy CS so that no HW-CS get changed
	 * we can not run this in bcm2835_spi_set_cs, as it does
	 * not get called for cs_gpio cases, so we need to do it here
	 */
	if (gpio_is_valid(spi->cs_gpio) || (spi->mode & SPI_NO_CS))
		cs |= BCM2835_SPI_CS_CS_10 | BCM2835_SPI_CS_CS_01;

	/* set transmit buffers and length */
	bs->tx_buf = tfr->tx_buf;
	bs->rx_buf = tfr->rx_buf;
	bs->tx_len = tfr->len;
	bs->rx_len = tfr->len;

	/* calculate the estimated time in us the transfer runs */
	xfer_time_us = (unsigned long long)tfr->len
		* 9 /* clocks/byte - SPI-HW waits 1 clock after each byte */
		* 1000000;
	do_div(xfer_time_us, spi_used_hz);

	/* for short requests run polling*/
	if (xfer_time_us <= BCM2835_SPI_POLLING_LIMIT_US)
		return bcm2835_spi_transfer_one_poll(master, spi, tfr,
						     cs, xfer_time_us);

	/* run in dma mode if conditions are right */
	if (master->can_dma && bcm2835_spi_can_dma(master, spi, tfr))
		return bcm2835_spi_transfer_one_dma(master, spi, tfr, cs);

	/* run in interrupt-mode */
	return bcm2835_spi_transfer_one_irq(master, spi, tfr, cs);
}

static int bcm2835_spi_prepare_message(struct spi_master *master,
				       struct spi_message *msg)
{
	struct spi_device *spi = msg->spi;
	struct bcm2835_spi *bs = spi_master_get_devdata(master);
	u32 cs = bcm2835_rd(bs, BCM2835_SPI_CS);

	cs &= ~(BCM2835_SPI_CS_CPOL | BCM2835_SPI_CS_CPHA);

	if (spi->mode & SPI_CPOL)
		cs |= BCM2835_SPI_CS_CPOL;
	if (spi->mode & SPI_CPHA)
		cs |= BCM2835_SPI_CS_CPHA;

	bcm2835_wr(bs, BCM2835_SPI_CS, cs);

	return 0;
}

static void bcm2835_spi_handle_err(struct spi_master *master,
				   struct spi_message *msg)
{
	struct bcm2835_spi *bs = spi_master_get_devdata(master);

	/* if an error occurred and we have an active dma, then terminate */
	if (cmpxchg(&bs->dma_pending, true, false)) {
		dmaengine_terminate_all(master->dma_tx);
		dmaengine_terminate_all(master->dma_rx);
	}
	/* and reset */
	bcm2835_spi_reset_hw(master);
}

static void bcm2835_spi_set_cs(struct spi_device *spi, bool gpio_level)
{
	/*
	 * we can assume that we are "native" as per spi_set_cs
	 *   calling us ONLY when cs_gpio is not set
	 * we can also assume that we are CS < 3 as per bcm2835_spi_setup
	 *   we would not get called because of error handling there.
	 * the level passed is the electrical level not enabled/disabled
	 *   so it has to get translated back to enable/disable
	 *   see spi_set_cs in spi.c for the implementation
	 */

	struct spi_master *master = spi->master;
	struct bcm2835_spi *bs = spi_master_get_devdata(master);
	u32 cs = bcm2835_rd(bs, BCM2835_SPI_CS);
	bool enable;

	/* calculate the enable flag from the passed gpio_level */
	enable = (spi->mode & SPI_CS_HIGH) ? gpio_level : !gpio_level;

	/* set flags for "reverse" polarity in the registers */
	if (spi->mode & SPI_CS_HIGH) {
		/* set the correct CS-bits */
		cs |= BCM2835_SPI_CS_CSPOL;
		cs |= BCM2835_SPI_CS_CSPOL0 << spi->chip_select;
	} else {
		/* clean the CS-bits */
		cs &= ~BCM2835_SPI_CS_CSPOL;
		cs &= ~(BCM2835_SPI_CS_CSPOL0 << spi->chip_select);
	}

	/* select the correct chip_select depending on disabled/enabled */
	if (enable) {
		/* set cs correctly */
		if (spi->mode & SPI_NO_CS) {
			/* use the "undefined" chip-select */
			cs |= BCM2835_SPI_CS_CS_10 | BCM2835_SPI_CS_CS_01;
		} else {
			/* set the chip select */
			cs &= ~(BCM2835_SPI_CS_CS_10 | BCM2835_SPI_CS_CS_01);
			cs |= spi->chip_select;
		}
	} else {
		/* disable CSPOL which puts HW-CS into deselected state */
		cs &= ~BCM2835_SPI_CS_CSPOL;
		/* use the "undefined" chip-select as precaution */
		cs |= BCM2835_SPI_CS_CS_10 | BCM2835_SPI_CS_CS_01;
	}

	/* finally set the calculated flags in SPI_CS */
	bcm2835_wr(bs, BCM2835_SPI_CS, cs);
}

static int chip_match_name(struct gpio_chip *chip, void *data)
{
	return !strcmp(chip->label, data);
}

static int bcm2835_spi_setup(struct spi_device *spi)
{
	int err;
	struct gpio_chip *chip;
	/*
	 * sanity checking the native-chipselects
	 */
	if (spi->mode & SPI_NO_CS)
		return 0;
	if (gpio_is_valid(spi->cs_gpio))
		return 0;
	if (spi->chip_select > 1) {
		/* error in the case of native CS requested with CS > 1
		 * officially there is a CS2, but it is not documented
		 * which GPIO is connected with that...
		 */
		dev_err(&spi->dev,
			"setup: only two native chip-selects are supported\n");
		return -EINVAL;
	}
	/* now translate native cs to GPIO */

	/* get the gpio chip for the base */
	chip = gpiochip_find("pinctrl-bcm2835", chip_match_name);
	if (!chip)
		return 0;

	/* and calculate the real CS */
	spi->cs_gpio = chip->base + 8 - spi->chip_select;

	/* and set up the "mode" and level */
	dev_info(&spi->dev, "setting up native-CS%i as GPIO %i\n",
		 spi->chip_select, spi->cs_gpio);

	/* set up GPIO as output and pull to the correct level */
	err = gpio_direction_output(spi->cs_gpio,
				    (spi->mode & SPI_CS_HIGH) ? 0 : 1);
	if (err) {
		dev_err(&spi->dev,
			"could not set CS%i gpio %i as output: %i",
			spi->chip_select, spi->cs_gpio, err);
		return err;
	}

	return 0;
}

static int bcm2835_spi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct bcm2835_spi *bs;
	struct resource *res;
	int err;

	master = spi_alloc_master(&pdev->dev, sizeof(*bs));
	if (!master) {
		dev_err(&pdev->dev, "spi_alloc_master() failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);

	master->mode_bits = BCM2835_SPI_MODE_BITS;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->num_chipselect = 3;
	master->setup = bcm2835_spi_setup;
	master->set_cs = bcm2835_spi_set_cs;
	master->transfer_one = bcm2835_spi_transfer_one;
	master->handle_err = bcm2835_spi_handle_err;
	master->prepare_message = bcm2835_spi_prepare_message;
	master->dev.of_node = pdev->dev.of_node;

	bs = spi_master_get_devdata(master);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bs->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(bs->regs)) {
		err = PTR_ERR(bs->regs);
		goto out_master_put;
	}

	bs->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(bs->clk)) {
		err = PTR_ERR(bs->clk);
		dev_err(&pdev->dev, "could not get clk: %d\n", err);
		goto out_master_put;
	}

	bs->irq = platform_get_irq(pdev, 0);
	if (bs->irq <= 0) {
		dev_err(&pdev->dev, "could not get IRQ: %d\n", bs->irq);
		err = bs->irq ? bs->irq : -ENODEV;
		goto out_master_put;
	}

	clk_prepare_enable(bs->clk);

	bcm2835_dma_init(master, &pdev->dev);

	/* initialise the hardware with the default polarities */
	bcm2835_wr(bs, BCM2835_SPI_CS,
		   BCM2835_SPI_CS_CLEAR_RX | BCM2835_SPI_CS_CLEAR_TX);

	err = devm_request_irq(&pdev->dev, bs->irq, bcm2835_spi_interrupt, 0,
			       dev_name(&pdev->dev), master);
	if (err) {
		dev_err(&pdev->dev, "could not request IRQ: %d\n", err);
		goto out_clk_disable;
	}

	err = devm_spi_register_master(&pdev->dev, master);
	if (err) {
		dev_err(&pdev->dev, "could not register SPI master: %d\n", err);
		goto out_clk_disable;
	}

	return 0;

out_clk_disable:
	clk_disable_unprepare(bs->clk);
out_master_put:
	spi_master_put(master);
	return err;
}

static int bcm2835_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct bcm2835_spi *bs = spi_master_get_devdata(master);

	/* Clear FIFOs, and disable the HW block */
	bcm2835_wr(bs, BCM2835_SPI_CS,
		   BCM2835_SPI_CS_CLEAR_RX | BCM2835_SPI_CS_CLEAR_TX);

	clk_disable_unprepare(bs->clk);

	bcm2835_dma_release(master);

	return 0;
}

static const struct of_device_id bcm2835_spi_match[] = {
	{ .compatible = "brcm,bcm2835-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, bcm2835_spi_match);

static struct platform_driver bcm2835_spi_driver = {
	.driver		= {
		.name		= DRV_NAME,
		.of_match_table	= bcm2835_spi_match,
	},
	.probe		= bcm2835_spi_probe,
	.remove		= bcm2835_spi_remove,
};
module_platform_driver(bcm2835_spi_driver);

MODULE_DESCRIPTION("SPI controller driver for Broadcom BCM2835");
MODULE_AUTHOR("Chris Boot <bootc@bootc.net>");
MODULE_LICENSE("GPL v2");
