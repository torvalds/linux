/*
 * Copyright (C) 2009 Samsung Electronics Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/platform_data/spi-s3c64xx.h>

#define MAX_SPI_PORTS		6
#define S3C64XX_SPI_QUIRK_POLL		(1 << 0)
#define S3C64XX_SPI_QUIRK_CS_AUTO	(1 << 1)

/* Registers and bit-fields */

#define S3C64XX_SPI_CH_CFG		0x00
#define S3C64XX_SPI_CLK_CFG		0x04
#define S3C64XX_SPI_MODE_CFG	0x08
#define S3C64XX_SPI_SLAVE_SEL	0x0C
#define S3C64XX_SPI_INT_EN		0x10
#define S3C64XX_SPI_STATUS		0x14
#define S3C64XX_SPI_TX_DATA		0x18
#define S3C64XX_SPI_RX_DATA		0x1C
#define S3C64XX_SPI_PACKET_CNT	0x20
#define S3C64XX_SPI_PENDING_CLR	0x24
#define S3C64XX_SPI_SWAP_CFG	0x28
#define S3C64XX_SPI_FB_CLK		0x2C

#define S3C64XX_SPI_CH_HS_EN		(1<<6)	/* High Speed Enable */
#define S3C64XX_SPI_CH_SW_RST		(1<<5)
#define S3C64XX_SPI_CH_SLAVE		(1<<4)
#define S3C64XX_SPI_CPOL_L		(1<<3)
#define S3C64XX_SPI_CPHA_B		(1<<2)
#define S3C64XX_SPI_CH_RXCH_ON		(1<<1)
#define S3C64XX_SPI_CH_TXCH_ON		(1<<0)

#define S3C64XX_SPI_CLKSEL_SRCMSK	(3<<9)
#define S3C64XX_SPI_CLKSEL_SRCSHFT	9
#define S3C64XX_SPI_ENCLK_ENABLE	(1<<8)
#define S3C64XX_SPI_PSR_MASK		0xff

#define S3C64XX_SPI_MODE_CH_TSZ_BYTE		(0<<29)
#define S3C64XX_SPI_MODE_CH_TSZ_HALFWORD	(1<<29)
#define S3C64XX_SPI_MODE_CH_TSZ_WORD		(2<<29)
#define S3C64XX_SPI_MODE_CH_TSZ_MASK		(3<<29)
#define S3C64XX_SPI_MODE_BUS_TSZ_BYTE		(0<<17)
#define S3C64XX_SPI_MODE_BUS_TSZ_HALFWORD	(1<<17)
#define S3C64XX_SPI_MODE_BUS_TSZ_WORD		(2<<17)
#define S3C64XX_SPI_MODE_BUS_TSZ_MASK		(3<<17)
#define S3C64XX_SPI_MODE_RXDMA_ON		(1<<2)
#define S3C64XX_SPI_MODE_TXDMA_ON		(1<<1)
#define S3C64XX_SPI_MODE_4BURST			(1<<0)

#define S3C64XX_SPI_SLAVE_AUTO			(1<<1)
#define S3C64XX_SPI_SLAVE_SIG_INACT		(1<<0)
#define S3C64XX_SPI_SLAVE_NSC_CNT_2		(2<<4)

#define S3C64XX_SPI_INT_TRAILING_EN		(1<<6)
#define S3C64XX_SPI_INT_RX_OVERRUN_EN		(1<<5)
#define S3C64XX_SPI_INT_RX_UNDERRUN_EN		(1<<4)
#define S3C64XX_SPI_INT_TX_OVERRUN_EN		(1<<3)
#define S3C64XX_SPI_INT_TX_UNDERRUN_EN		(1<<2)
#define S3C64XX_SPI_INT_RX_FIFORDY_EN		(1<<1)
#define S3C64XX_SPI_INT_TX_FIFORDY_EN		(1<<0)

#define S3C64XX_SPI_ST_RX_OVERRUN_ERR		(1<<5)
#define S3C64XX_SPI_ST_RX_UNDERRUN_ERR	(1<<4)
#define S3C64XX_SPI_ST_TX_OVERRUN_ERR		(1<<3)
#define S3C64XX_SPI_ST_TX_UNDERRUN_ERR	(1<<2)
#define S3C64XX_SPI_ST_RX_FIFORDY		(1<<1)
#define S3C64XX_SPI_ST_TX_FIFORDY		(1<<0)

#define S3C64XX_SPI_PACKET_CNT_EN		(1<<16)

#define S3C64XX_SPI_PND_TX_UNDERRUN_CLR		(1<<4)
#define S3C64XX_SPI_PND_TX_OVERRUN_CLR		(1<<3)
#define S3C64XX_SPI_PND_RX_UNDERRUN_CLR		(1<<2)
#define S3C64XX_SPI_PND_RX_OVERRUN_CLR		(1<<1)
#define S3C64XX_SPI_PND_TRAILING_CLR		(1<<0)

#define S3C64XX_SPI_SWAP_RX_HALF_WORD		(1<<7)
#define S3C64XX_SPI_SWAP_RX_BYTE		(1<<6)
#define S3C64XX_SPI_SWAP_RX_BIT			(1<<5)
#define S3C64XX_SPI_SWAP_RX_EN			(1<<4)
#define S3C64XX_SPI_SWAP_TX_HALF_WORD		(1<<3)
#define S3C64XX_SPI_SWAP_TX_BYTE		(1<<2)
#define S3C64XX_SPI_SWAP_TX_BIT			(1<<1)
#define S3C64XX_SPI_SWAP_TX_EN			(1<<0)

#define S3C64XX_SPI_FBCLK_MSK		(3<<0)

#define FIFO_LVL_MASK(i) ((i)->port_conf->fifo_lvl_mask[i->port_id])
#define S3C64XX_SPI_ST_TX_DONE(v, i) (((v) & \
				(1 << (i)->port_conf->tx_st_done)) ? 1 : 0)
#define TX_FIFO_LVL(v, i) (((v) >> 6) & FIFO_LVL_MASK(i))
#define RX_FIFO_LVL(v, i) (((v) >> (i)->port_conf->rx_lvl_offset) & \
					FIFO_LVL_MASK(i))

#define S3C64XX_SPI_MAX_TRAILCNT	0x3ff
#define S3C64XX_SPI_TRAILCNT_OFF	19

#define S3C64XX_SPI_TRAILCNT		S3C64XX_SPI_MAX_TRAILCNT

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)
#define is_polling(x)	(x->port_conf->quirks & S3C64XX_SPI_QUIRK_POLL)

#define RXBUSY    (1<<2)
#define TXBUSY    (1<<3)

struct s3c64xx_spi_dma_data {
	struct dma_chan *ch;
	enum dma_transfer_direction direction;
	unsigned int dmach;
};

/**
 * struct s3c64xx_spi_info - SPI Controller hardware info
 * @fifo_lvl_mask: Bit-mask for {TX|RX}_FIFO_LVL bits in SPI_STATUS register.
 * @rx_lvl_offset: Bit offset of RX_FIFO_LVL bits in SPI_STATUS regiter.
 * @tx_st_done: Bit offset of TX_DONE bit in SPI_STATUS regiter.
 * @high_speed: True, if the controller supports HIGH_SPEED_EN bit.
 * @clk_from_cmu: True, if the controller does not include a clock mux and
 *	prescaler unit.
 *
 * The Samsung s3c64xx SPI controller are used on various Samsung SoC's but
 * differ in some aspects such as the size of the fifo and spi bus clock
 * setup. Such differences are specified to the driver using this structure
 * which is provided as driver data to the driver.
 */
struct s3c64xx_spi_port_config {
	int	fifo_lvl_mask[MAX_SPI_PORTS];
	int	rx_lvl_offset;
	int	tx_st_done;
	int	quirks;
	bool	high_speed;
	bool	clk_from_cmu;
};

/**
 * struct s3c64xx_spi_driver_data - Runtime info holder for SPI driver.
 * @clk: Pointer to the spi clock.
 * @src_clk: Pointer to the clock used to generate SPI signals.
 * @master: Pointer to the SPI Protocol master.
 * @cntrlr_info: Platform specific data for the controller this driver manages.
 * @tgl_spi: Pointer to the last CS left untoggled by the cs_change hint.
 * @lock: Controller specific lock.
 * @state: Set of FLAGS to indicate status.
 * @rx_dmach: Controller's DMA channel for Rx.
 * @tx_dmach: Controller's DMA channel for Tx.
 * @sfr_start: BUS address of SPI controller regs.
 * @regs: Pointer to ioremap'ed controller registers.
 * @irq: interrupt
 * @xfer_completion: To indicate completion of xfer task.
 * @cur_mode: Stores the active configuration of the controller.
 * @cur_bpw: Stores the active bits per word settings.
 * @cur_speed: Stores the active xfer clock speed.
 */
struct s3c64xx_spi_driver_data {
	void __iomem                    *regs;
	struct clk                      *clk;
	struct clk                      *src_clk;
	struct platform_device          *pdev;
	struct spi_master               *master;
	struct s3c64xx_spi_info  *cntrlr_info;
	struct spi_device               *tgl_spi;
	spinlock_t                      lock;
	unsigned long                   sfr_start;
	struct completion               xfer_completion;
	unsigned                        state;
	unsigned                        cur_mode, cur_bpw;
	unsigned                        cur_speed;
	struct s3c64xx_spi_dma_data	rx_dma;
	struct s3c64xx_spi_dma_data	tx_dma;
	struct s3c64xx_spi_port_config	*port_conf;
	unsigned int			port_id;
};

static void flush_fifo(struct s3c64xx_spi_driver_data *sdd)
{
	void __iomem *regs = sdd->regs;
	unsigned long loops;
	u32 val;

	writel(0, regs + S3C64XX_SPI_PACKET_CNT);

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~(S3C64XX_SPI_CH_RXCH_ON | S3C64XX_SPI_CH_TXCH_ON);
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val |= S3C64XX_SPI_CH_SW_RST;
	val &= ~S3C64XX_SPI_CH_HS_EN;
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	/* Flush TxFIFO*/
	loops = msecs_to_loops(1);
	do {
		val = readl(regs + S3C64XX_SPI_STATUS);
	} while (TX_FIFO_LVL(val, sdd) && loops--);

	if (loops == 0)
		dev_warn(&sdd->pdev->dev, "Timed out flushing TX FIFO\n");

	/* Flush RxFIFO*/
	loops = msecs_to_loops(1);
	do {
		val = readl(regs + S3C64XX_SPI_STATUS);
		if (RX_FIFO_LVL(val, sdd))
			readl(regs + S3C64XX_SPI_RX_DATA);
		else
			break;
	} while (loops--);

	if (loops == 0)
		dev_warn(&sdd->pdev->dev, "Timed out flushing RX FIFO\n");

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~S3C64XX_SPI_CH_SW_RST;
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~(S3C64XX_SPI_MODE_TXDMA_ON | S3C64XX_SPI_MODE_RXDMA_ON);
	writel(val, regs + S3C64XX_SPI_MODE_CFG);
}

static void s3c64xx_spi_dmacb(void *data)
{
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_dma_data *dma = data;
	unsigned long flags;

	if (dma->direction == DMA_DEV_TO_MEM)
		sdd = container_of(data,
			struct s3c64xx_spi_driver_data, rx_dma);
	else
		sdd = container_of(data,
			struct s3c64xx_spi_driver_data, tx_dma);

	spin_lock_irqsave(&sdd->lock, flags);

	if (dma->direction == DMA_DEV_TO_MEM) {
		sdd->state &= ~RXBUSY;
		if (!(sdd->state & TXBUSY))
			complete(&sdd->xfer_completion);
	} else {
		sdd->state &= ~TXBUSY;
		if (!(sdd->state & RXBUSY))
			complete(&sdd->xfer_completion);
	}

	spin_unlock_irqrestore(&sdd->lock, flags);
}

static void prepare_dma(struct s3c64xx_spi_dma_data *dma,
			struct sg_table *sgt)
{
	struct s3c64xx_spi_driver_data *sdd;
	struct dma_slave_config config;
	struct dma_async_tx_descriptor *desc;

	memset(&config, 0, sizeof(config));

	if (dma->direction == DMA_DEV_TO_MEM) {
		sdd = container_of((void *)dma,
			struct s3c64xx_spi_driver_data, rx_dma);
		config.direction = dma->direction;
		config.src_addr = sdd->sfr_start + S3C64XX_SPI_RX_DATA;
		config.src_addr_width = sdd->cur_bpw / 8;
		config.src_maxburst = 1;
		dmaengine_slave_config(dma->ch, &config);
	} else {
		sdd = container_of((void *)dma,
			struct s3c64xx_spi_driver_data, tx_dma);
		config.direction = dma->direction;
		config.dst_addr = sdd->sfr_start + S3C64XX_SPI_TX_DATA;
		config.dst_addr_width = sdd->cur_bpw / 8;
		config.dst_maxburst = 1;
		dmaengine_slave_config(dma->ch, &config);
	}

	desc = dmaengine_prep_slave_sg(dma->ch, sgt->sgl, sgt->nents,
				       dma->direction, DMA_PREP_INTERRUPT);

	desc->callback = s3c64xx_spi_dmacb;
	desc->callback_param = dma;

	dmaengine_submit(desc);
	dma_async_issue_pending(dma->ch);
}

static int s3c64xx_spi_prepare_transfer(struct spi_master *spi)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(spi);
	dma_filter_fn filter = sdd->cntrlr_info->filter;
	struct device *dev = &sdd->pdev->dev;
	dma_cap_mask_t mask;
	int ret;

	if (!is_polling(sdd)) {
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		/* Acquire DMA channels */
		sdd->rx_dma.ch = dma_request_slave_channel_compat(mask, filter,
				   (void *)sdd->rx_dma.dmach, dev, "rx");
		if (!sdd->rx_dma.ch) {
			dev_err(dev, "Failed to get RX DMA channel\n");
			ret = -EBUSY;
			goto out;
		}
		spi->dma_rx = sdd->rx_dma.ch;

		sdd->tx_dma.ch = dma_request_slave_channel_compat(mask, filter,
				   (void *)sdd->tx_dma.dmach, dev, "tx");
		if (!sdd->tx_dma.ch) {
			dev_err(dev, "Failed to get TX DMA channel\n");
			ret = -EBUSY;
			goto out_rx;
		}
		spi->dma_tx = sdd->tx_dma.ch;
	}

	return 0;

out_rx:
	dma_release_channel(sdd->rx_dma.ch);
out:
	return ret;
}

static int s3c64xx_spi_unprepare_transfer(struct spi_master *spi)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(spi);

	/* Free DMA channels */
	if (!is_polling(sdd)) {
		dma_release_channel(sdd->rx_dma.ch);
		dma_release_channel(sdd->tx_dma.ch);
	}

	return 0;
}

static bool s3c64xx_spi_can_dma(struct spi_master *master,
				struct spi_device *spi,
				struct spi_transfer *xfer)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);

	return xfer->len > (FIFO_LVL_MASK(sdd) >> 1) + 1;
}

static void enable_datapath(struct s3c64xx_spi_driver_data *sdd,
				struct spi_device *spi,
				struct spi_transfer *xfer, int dma_mode)
{
	void __iomem *regs = sdd->regs;
	u32 modecfg, chcfg;

	modecfg = readl(regs + S3C64XX_SPI_MODE_CFG);
	modecfg &= ~(S3C64XX_SPI_MODE_TXDMA_ON | S3C64XX_SPI_MODE_RXDMA_ON);

	chcfg = readl(regs + S3C64XX_SPI_CH_CFG);
	chcfg &= ~S3C64XX_SPI_CH_TXCH_ON;

	if (dma_mode) {
		chcfg &= ~S3C64XX_SPI_CH_RXCH_ON;
	} else {
		/* Always shift in data in FIFO, even if xfer is Tx only,
		 * this helps setting PCKT_CNT value for generating clocks
		 * as exactly needed.
		 */
		chcfg |= S3C64XX_SPI_CH_RXCH_ON;
		writel(((xfer->len * 8 / sdd->cur_bpw) & 0xffff)
					| S3C64XX_SPI_PACKET_CNT_EN,
					regs + S3C64XX_SPI_PACKET_CNT);
	}

	if (xfer->tx_buf != NULL) {
		sdd->state |= TXBUSY;
		chcfg |= S3C64XX_SPI_CH_TXCH_ON;
		if (dma_mode) {
			modecfg |= S3C64XX_SPI_MODE_TXDMA_ON;
			prepare_dma(&sdd->tx_dma, &xfer->tx_sg);
		} else {
			switch (sdd->cur_bpw) {
			case 32:
				iowrite32_rep(regs + S3C64XX_SPI_TX_DATA,
					xfer->tx_buf, xfer->len / 4);
				break;
			case 16:
				iowrite16_rep(regs + S3C64XX_SPI_TX_DATA,
					xfer->tx_buf, xfer->len / 2);
				break;
			default:
				iowrite8_rep(regs + S3C64XX_SPI_TX_DATA,
					xfer->tx_buf, xfer->len);
				break;
			}
		}
	}

	if (xfer->rx_buf != NULL) {
		sdd->state |= RXBUSY;

		if (sdd->port_conf->high_speed && sdd->cur_speed >= 30000000UL
					&& !(sdd->cur_mode & SPI_CPHA))
			chcfg |= S3C64XX_SPI_CH_HS_EN;

		if (dma_mode) {
			modecfg |= S3C64XX_SPI_MODE_RXDMA_ON;
			chcfg |= S3C64XX_SPI_CH_RXCH_ON;
			writel(((xfer->len * 8 / sdd->cur_bpw) & 0xffff)
					| S3C64XX_SPI_PACKET_CNT_EN,
					regs + S3C64XX_SPI_PACKET_CNT);
			prepare_dma(&sdd->rx_dma, &xfer->rx_sg);
		}
	}

	writel(modecfg, regs + S3C64XX_SPI_MODE_CFG);
	writel(chcfg, regs + S3C64XX_SPI_CH_CFG);
}

static u32 s3c64xx_spi_wait_for_timeout(struct s3c64xx_spi_driver_data *sdd,
					int timeout_ms)
{
	void __iomem *regs = sdd->regs;
	unsigned long val = 1;
	u32 status;

	/* max fifo depth available */
	u32 max_fifo = (FIFO_LVL_MASK(sdd) >> 1) + 1;

	if (timeout_ms)
		val = msecs_to_loops(timeout_ms);

	do {
		status = readl(regs + S3C64XX_SPI_STATUS);
	} while (RX_FIFO_LVL(status, sdd) < max_fifo && --val);

	/* return the actual received data length */
	return RX_FIFO_LVL(status, sdd);
}

static int wait_for_dma(struct s3c64xx_spi_driver_data *sdd,
			struct spi_transfer *xfer)
{
	void __iomem *regs = sdd->regs;
	unsigned long val;
	u32 status;
	int ms;

	/* millisecs to xfer 'len' bytes @ 'cur_speed' */
	ms = xfer->len * 8 * 1000 / sdd->cur_speed;
	ms += 10; /* some tolerance */

	val = msecs_to_jiffies(ms) + 10;
	val = wait_for_completion_timeout(&sdd->xfer_completion, val);

	/*
	 * If the previous xfer was completed within timeout, then
	 * proceed further else return -EIO.
	 * DmaTx returns after simply writing data in the FIFO,
	 * w/o waiting for real transmission on the bus to finish.
	 * DmaRx returns only after Dma read data from FIFO which
	 * needs bus transmission to finish, so we don't worry if
	 * Xfer involved Rx(with or without Tx).
	 */
	if (val && !xfer->rx_buf) {
		val = msecs_to_loops(10);
		status = readl(regs + S3C64XX_SPI_STATUS);
		while ((TX_FIFO_LVL(status, sdd)
			|| !S3C64XX_SPI_ST_TX_DONE(status, sdd))
		       && --val) {
			cpu_relax();
			status = readl(regs + S3C64XX_SPI_STATUS);
		}

	}

	/* If timed out while checking rx/tx status return error */
	if (!val)
		return -EIO;

	return 0;
}

static int wait_for_pio(struct s3c64xx_spi_driver_data *sdd,
			struct spi_transfer *xfer)
{
	void __iomem *regs = sdd->regs;
	unsigned long val;
	u32 status;
	int loops;
	u32 cpy_len;
	u8 *buf;
	int ms;

	/* millisecs to xfer 'len' bytes @ 'cur_speed' */
	ms = xfer->len * 8 * 1000 / sdd->cur_speed;
	ms += 10; /* some tolerance */

	val = msecs_to_loops(ms);
	do {
		status = readl(regs + S3C64XX_SPI_STATUS);
	} while (RX_FIFO_LVL(status, sdd) < xfer->len && --val);


	/* If it was only Tx */
	if (!xfer->rx_buf) {
		sdd->state &= ~TXBUSY;
		return 0;
	}

	/*
	 * If the receive length is bigger than the controller fifo
	 * size, calculate the loops and read the fifo as many times.
	 * loops = length / max fifo size (calculated by using the
	 * fifo mask).
	 * For any size less than the fifo size the below code is
	 * executed atleast once.
	 */
	loops = xfer->len / ((FIFO_LVL_MASK(sdd) >> 1) + 1);
	buf = xfer->rx_buf;
	do {
		/* wait for data to be received in the fifo */
		cpy_len = s3c64xx_spi_wait_for_timeout(sdd,
						       (loops ? ms : 0));

		switch (sdd->cur_bpw) {
		case 32:
			ioread32_rep(regs + S3C64XX_SPI_RX_DATA,
				     buf, cpy_len / 4);
			break;
		case 16:
			ioread16_rep(regs + S3C64XX_SPI_RX_DATA,
				     buf, cpy_len / 2);
			break;
		default:
			ioread8_rep(regs + S3C64XX_SPI_RX_DATA,
				    buf, cpy_len);
			break;
		}

		buf = buf + cpy_len;
	} while (loops--);
	sdd->state &= ~RXBUSY;

	return 0;
}

static void s3c64xx_spi_config(struct s3c64xx_spi_driver_data *sdd)
{
	void __iomem *regs = sdd->regs;
	u32 val;

	/* Disable Clock */
	if (sdd->port_conf->clk_from_cmu) {
		clk_disable_unprepare(sdd->src_clk);
	} else {
		val = readl(regs + S3C64XX_SPI_CLK_CFG);
		val &= ~S3C64XX_SPI_ENCLK_ENABLE;
		writel(val, regs + S3C64XX_SPI_CLK_CFG);
	}

	/* Set Polarity and Phase */
	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~(S3C64XX_SPI_CH_SLAVE |
			S3C64XX_SPI_CPOL_L |
			S3C64XX_SPI_CPHA_B);

	if (sdd->cur_mode & SPI_CPOL)
		val |= S3C64XX_SPI_CPOL_L;

	if (sdd->cur_mode & SPI_CPHA)
		val |= S3C64XX_SPI_CPHA_B;

	writel(val, regs + S3C64XX_SPI_CH_CFG);

	/* Set Channel & DMA Mode */
	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~(S3C64XX_SPI_MODE_BUS_TSZ_MASK
			| S3C64XX_SPI_MODE_CH_TSZ_MASK);

	switch (sdd->cur_bpw) {
	case 32:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_WORD;
		val |= S3C64XX_SPI_MODE_CH_TSZ_WORD;
		break;
	case 16:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_HALFWORD;
		val |= S3C64XX_SPI_MODE_CH_TSZ_HALFWORD;
		break;
	default:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_BYTE;
		val |= S3C64XX_SPI_MODE_CH_TSZ_BYTE;
		break;
	}

	writel(val, regs + S3C64XX_SPI_MODE_CFG);

	if (sdd->port_conf->clk_from_cmu) {
		/* Configure Clock */
		/* There is half-multiplier before the SPI */
		clk_set_rate(sdd->src_clk, sdd->cur_speed * 2);
		/* Enable Clock */
		clk_prepare_enable(sdd->src_clk);
	} else {
		/* Configure Clock */
		val = readl(regs + S3C64XX_SPI_CLK_CFG);
		val &= ~S3C64XX_SPI_PSR_MASK;
		val |= ((clk_get_rate(sdd->src_clk) / sdd->cur_speed / 2 - 1)
				& S3C64XX_SPI_PSR_MASK);
		writel(val, regs + S3C64XX_SPI_CLK_CFG);

		/* Enable Clock */
		val = readl(regs + S3C64XX_SPI_CLK_CFG);
		val |= S3C64XX_SPI_ENCLK_ENABLE;
		writel(val, regs + S3C64XX_SPI_CLK_CFG);
	}
}

#define XFER_DMAADDR_INVALID DMA_BIT_MASK(32)

static int s3c64xx_spi_prepare_message(struct spi_master *master,
				       struct spi_message *msg)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;

	/* If Master's(controller) state differs from that needed by Slave */
	if (sdd->cur_speed != spi->max_speed_hz
			|| sdd->cur_mode != spi->mode
			|| sdd->cur_bpw != spi->bits_per_word) {
		sdd->cur_bpw = spi->bits_per_word;
		sdd->cur_speed = spi->max_speed_hz;
		sdd->cur_mode = spi->mode;
		s3c64xx_spi_config(sdd);
	}

	/* Configure feedback delay */
	writel(cs->fb_delay & 0x3, sdd->regs + S3C64XX_SPI_FB_CLK);

	return 0;
}

static int s3c64xx_spi_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	int status;
	u32 speed;
	u8 bpw;
	unsigned long flags;
	int use_dma;

	reinit_completion(&sdd->xfer_completion);

	/* Only BPW and Speed may change across transfers */
	bpw = xfer->bits_per_word;
	speed = xfer->speed_hz ? : spi->max_speed_hz;

	if (bpw != sdd->cur_bpw || speed != sdd->cur_speed) {
		sdd->cur_bpw = bpw;
		sdd->cur_speed = speed;
		s3c64xx_spi_config(sdd);
	}

	/* Polling method for xfers not bigger than FIFO capacity */
	use_dma = 0;
	if (!is_polling(sdd) &&
	    (sdd->rx_dma.ch && sdd->tx_dma.ch &&
	     (xfer->len > ((FIFO_LVL_MASK(sdd) >> 1) + 1))))
		use_dma = 1;

	spin_lock_irqsave(&sdd->lock, flags);

	/* Pending only which is to be done */
	sdd->state &= ~RXBUSY;
	sdd->state &= ~TXBUSY;

	enable_datapath(sdd, spi, xfer, use_dma);

	/* Start the signals */
	if (!(sdd->port_conf->quirks & S3C64XX_SPI_QUIRK_CS_AUTO))
		writel(0, sdd->regs + S3C64XX_SPI_SLAVE_SEL);
	else
		writel(readl(sdd->regs + S3C64XX_SPI_SLAVE_SEL)
			| S3C64XX_SPI_SLAVE_AUTO | S3C64XX_SPI_SLAVE_NSC_CNT_2,
			sdd->regs + S3C64XX_SPI_SLAVE_SEL);

	spin_unlock_irqrestore(&sdd->lock, flags);

	if (use_dma)
		status = wait_for_dma(sdd, xfer);
	else
		status = wait_for_pio(sdd, xfer);

	if (status) {
		dev_err(&spi->dev, "I/O Error: rx-%d tx-%d res:rx-%c tx-%c len-%d\n",
			xfer->rx_buf ? 1 : 0, xfer->tx_buf ? 1 : 0,
			(sdd->state & RXBUSY) ? 'f' : 'p',
			(sdd->state & TXBUSY) ? 'f' : 'p',
			xfer->len);

		if (use_dma) {
			if (xfer->tx_buf != NULL
			    && (sdd->state & TXBUSY))
				dmaengine_terminate_all(sdd->tx_dma.ch);
			if (xfer->rx_buf != NULL
			    && (sdd->state & RXBUSY))
				dmaengine_terminate_all(sdd->rx_dma.ch);
		}
	} else {
		flush_fifo(sdd);
	}

	return status;
}

static struct s3c64xx_spi_csinfo *s3c64xx_get_slave_ctrldata(
				struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs;
	struct device_node *slave_np, *data_np = NULL;
	u32 fb_delay = 0;

	slave_np = spi->dev.of_node;
	if (!slave_np) {
		dev_err(&spi->dev, "device node not found\n");
		return ERR_PTR(-EINVAL);
	}

	data_np = of_get_child_by_name(slave_np, "controller-data");
	if (!data_np) {
		dev_err(&spi->dev, "child node 'controller-data' not found\n");
		return ERR_PTR(-EINVAL);
	}

	cs = kzalloc(sizeof(*cs), GFP_KERNEL);
	if (!cs) {
		of_node_put(data_np);
		return ERR_PTR(-ENOMEM);
	}

	of_property_read_u32(data_np, "samsung,spi-feedback-delay", &fb_delay);
	cs->fb_delay = fb_delay;
	of_node_put(data_np);
	return cs;
}

/*
 * Here we only check the validity of requested configuration
 * and save the configuration in a local data-structure.
 * The controller is actually configured only just before we
 * get a message to transfer.
 */
static int s3c64xx_spi_setup(struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_info *sci;
	int err;

	sdd = spi_master_get_devdata(spi->master);
	if (spi->dev.of_node) {
		cs = s3c64xx_get_slave_ctrldata(spi);
		spi->controller_data = cs;
	} else if (cs) {
		/* On non-DT platforms the SPI core will set spi->cs_gpio
		 * to -ENOENT. The GPIO pin used to drive the chip select
		 * is defined by using platform data so spi->cs_gpio value
		 * has to be override to have the proper GPIO pin number.
		 */
		spi->cs_gpio = cs->line;
	}

	if (IS_ERR_OR_NULL(cs)) {
		dev_err(&spi->dev, "No CS for SPI(%d)\n", spi->chip_select);
		return -ENODEV;
	}

	if (!spi_get_ctldata(spi)) {
		if (gpio_is_valid(spi->cs_gpio)) {
			err = gpio_request_one(spi->cs_gpio, GPIOF_OUT_INIT_HIGH,
					       dev_name(&spi->dev));
			if (err) {
				dev_err(&spi->dev,
					"Failed to get /CS gpio [%d]: %d\n",
					spi->cs_gpio, err);
				goto err_gpio_req;
			}
		}

		spi_set_ctldata(spi, cs);
	}

	sci = sdd->cntrlr_info;

	pm_runtime_get_sync(&sdd->pdev->dev);

	/* Check if we can provide the requested rate */
	if (!sdd->port_conf->clk_from_cmu) {
		u32 psr, speed;

		/* Max possible */
		speed = clk_get_rate(sdd->src_clk) / 2 / (0 + 1);

		if (spi->max_speed_hz > speed)
			spi->max_speed_hz = speed;

		psr = clk_get_rate(sdd->src_clk) / 2 / spi->max_speed_hz - 1;
		psr &= S3C64XX_SPI_PSR_MASK;
		if (psr == S3C64XX_SPI_PSR_MASK)
			psr--;

		speed = clk_get_rate(sdd->src_clk) / 2 / (psr + 1);
		if (spi->max_speed_hz < speed) {
			if (psr+1 < S3C64XX_SPI_PSR_MASK) {
				psr++;
			} else {
				err = -EINVAL;
				goto setup_exit;
			}
		}

		speed = clk_get_rate(sdd->src_clk) / 2 / (psr + 1);
		if (spi->max_speed_hz >= speed) {
			spi->max_speed_hz = speed;
		} else {
			dev_err(&spi->dev, "Can't set %dHz transfer speed\n",
				spi->max_speed_hz);
			err = -EINVAL;
			goto setup_exit;
		}
	}

	pm_runtime_put(&sdd->pdev->dev);
	if (!(sdd->port_conf->quirks & S3C64XX_SPI_QUIRK_CS_AUTO))
		writel(S3C64XX_SPI_SLAVE_SIG_INACT, sdd->regs + S3C64XX_SPI_SLAVE_SEL);
	return 0;

setup_exit:
	pm_runtime_put(&sdd->pdev->dev);
	/* setup() returns with device de-selected */
	if (!(sdd->port_conf->quirks & S3C64XX_SPI_QUIRK_CS_AUTO))
		writel(S3C64XX_SPI_SLAVE_SIG_INACT, sdd->regs + S3C64XX_SPI_SLAVE_SEL);

	if (gpio_is_valid(spi->cs_gpio))
		gpio_free(spi->cs_gpio);
	spi_set_ctldata(spi, NULL);

err_gpio_req:
	if (spi->dev.of_node)
		kfree(cs);

	return err;
}

static void s3c64xx_spi_cleanup(struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs = spi_get_ctldata(spi);

	if (gpio_is_valid(spi->cs_gpio)) {
		gpio_free(spi->cs_gpio);
		if (spi->dev.of_node)
			kfree(cs);
		else {
			/* On non-DT platforms, the SPI core sets
			 * spi->cs_gpio to -ENOENT and .setup()
			 * overrides it with the GPIO pin value
			 * passed using platform data.
			 */
			spi->cs_gpio = -ENOENT;
		}
	}

	spi_set_ctldata(spi, NULL);
}

static irqreturn_t s3c64xx_spi_irq(int irq, void *data)
{
	struct s3c64xx_spi_driver_data *sdd = data;
	struct spi_master *spi = sdd->master;
	unsigned int val, clr = 0;

	val = readl(sdd->regs + S3C64XX_SPI_STATUS);

	if (val & S3C64XX_SPI_ST_RX_OVERRUN_ERR) {
		clr = S3C64XX_SPI_PND_RX_OVERRUN_CLR;
		dev_err(&spi->dev, "RX overrun\n");
	}
	if (val & S3C64XX_SPI_ST_RX_UNDERRUN_ERR) {
		clr |= S3C64XX_SPI_PND_RX_UNDERRUN_CLR;
		dev_err(&spi->dev, "RX underrun\n");
	}
	if (val & S3C64XX_SPI_ST_TX_OVERRUN_ERR) {
		clr |= S3C64XX_SPI_PND_TX_OVERRUN_CLR;
		dev_err(&spi->dev, "TX overrun\n");
	}
	if (val & S3C64XX_SPI_ST_TX_UNDERRUN_ERR) {
		clr |= S3C64XX_SPI_PND_TX_UNDERRUN_CLR;
		dev_err(&spi->dev, "TX underrun\n");
	}

	/* Clear the pending irq by setting and then clearing it */
	writel(clr, sdd->regs + S3C64XX_SPI_PENDING_CLR);
	writel(0, sdd->regs + S3C64XX_SPI_PENDING_CLR);

	return IRQ_HANDLED;
}

static void s3c64xx_spi_hwinit(struct s3c64xx_spi_driver_data *sdd, int channel)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	void __iomem *regs = sdd->regs;
	unsigned int val;

	sdd->cur_speed = 0;

	if (!(sdd->port_conf->quirks & S3C64XX_SPI_QUIRK_CS_AUTO))
		writel(S3C64XX_SPI_SLAVE_SIG_INACT, sdd->regs + S3C64XX_SPI_SLAVE_SEL);

	/* Disable Interrupts - we use Polling if not DMA mode */
	writel(0, regs + S3C64XX_SPI_INT_EN);

	if (!sdd->port_conf->clk_from_cmu)
		writel(sci->src_clk_nr << S3C64XX_SPI_CLKSEL_SRCSHFT,
				regs + S3C64XX_SPI_CLK_CFG);
	writel(0, regs + S3C64XX_SPI_MODE_CFG);
	writel(0, regs + S3C64XX_SPI_PACKET_CNT);

	/* Clear any irq pending bits, should set and clear the bits */
	val = S3C64XX_SPI_PND_RX_OVERRUN_CLR |
		S3C64XX_SPI_PND_RX_UNDERRUN_CLR |
		S3C64XX_SPI_PND_TX_OVERRUN_CLR |
		S3C64XX_SPI_PND_TX_UNDERRUN_CLR;
	writel(val, regs + S3C64XX_SPI_PENDING_CLR);
	writel(0, regs + S3C64XX_SPI_PENDING_CLR);

	writel(0, regs + S3C64XX_SPI_SWAP_CFG);

	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~S3C64XX_SPI_MODE_4BURST;
	val &= ~(S3C64XX_SPI_MAX_TRAILCNT << S3C64XX_SPI_TRAILCNT_OFF);
	val |= (S3C64XX_SPI_TRAILCNT << S3C64XX_SPI_TRAILCNT_OFF);
	writel(val, regs + S3C64XX_SPI_MODE_CFG);

	flush_fifo(sdd);
}

#ifdef CONFIG_OF
static struct s3c64xx_spi_info *s3c64xx_spi_parse_dt(struct device *dev)
{
	struct s3c64xx_spi_info *sci;
	u32 temp;

	sci = devm_kzalloc(dev, sizeof(*sci), GFP_KERNEL);
	if (!sci)
		return ERR_PTR(-ENOMEM);

	if (of_property_read_u32(dev->of_node, "samsung,spi-src-clk", &temp)) {
		dev_warn(dev, "spi bus clock parent not specified, using clock at index 0 as parent\n");
		sci->src_clk_nr = 0;
	} else {
		sci->src_clk_nr = temp;
	}

	if (of_property_read_u32(dev->of_node, "num-cs", &temp)) {
		dev_warn(dev, "number of chip select lines not specified, assuming 1 chip select line\n");
		sci->num_cs = 1;
	} else {
		sci->num_cs = temp;
	}

	return sci;
}
#else
static struct s3c64xx_spi_info *s3c64xx_spi_parse_dt(struct device *dev)
{
	return dev_get_platdata(dev);
}
#endif

static const struct of_device_id s3c64xx_spi_dt_match[];

static inline struct s3c64xx_spi_port_config *s3c64xx_spi_get_port_config(
						struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(s3c64xx_spi_dt_match, pdev->dev.of_node);
		return (struct s3c64xx_spi_port_config *)match->data;
	}
#endif
	return (struct s3c64xx_spi_port_config *)
			 platform_get_device_id(pdev)->driver_data;
}

static int s3c64xx_spi_probe(struct platform_device *pdev)
{
	struct resource	*mem_res;
	struct resource	*res;
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_info *sci = dev_get_platdata(&pdev->dev);
	struct spi_master *master;
	int ret, irq;
	char clk_name[16];

	if (!sci && pdev->dev.of_node) {
		sci = s3c64xx_spi_parse_dt(&pdev->dev);
		if (IS_ERR(sci))
			return PTR_ERR(sci);
	}

	if (!sci) {
		dev_err(&pdev->dev, "platform_data missing!\n");
		return -ENODEV;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI MEM resource\n");
		return -ENXIO;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_warn(&pdev->dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}

	master = spi_alloc_master(&pdev->dev,
				sizeof(struct s3c64xx_spi_driver_data));
	if (master == NULL) {
		dev_err(&pdev->dev, "Unable to allocate SPI Master\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);

	sdd = spi_master_get_devdata(master);
	sdd->port_conf = s3c64xx_spi_get_port_config(pdev);
	sdd->master = master;
	sdd->cntrlr_info = sci;
	sdd->pdev = pdev;
	sdd->sfr_start = mem_res->start;
	if (pdev->dev.of_node) {
		ret = of_alias_get_id(pdev->dev.of_node, "spi");
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to get alias id, errno %d\n",
				ret);
			goto err0;
		}
		sdd->port_id = ret;
	} else {
		sdd->port_id = pdev->id;
	}

	sdd->cur_bpw = 8;

	if (!sdd->pdev->dev.of_node) {
		res = platform_get_resource(pdev, IORESOURCE_DMA,  0);
		if (!res) {
			dev_warn(&pdev->dev, "Unable to get SPI tx dma resource. Switching to poll mode\n");
			sdd->port_conf->quirks = S3C64XX_SPI_QUIRK_POLL;
		} else
			sdd->tx_dma.dmach = res->start;

		res = platform_get_resource(pdev, IORESOURCE_DMA,  1);
		if (!res) {
			dev_warn(&pdev->dev, "Unable to get SPI rx dma resource. Switching to poll mode\n");
			sdd->port_conf->quirks = S3C64XX_SPI_QUIRK_POLL;
		} else
			sdd->rx_dma.dmach = res->start;
	}

	sdd->tx_dma.direction = DMA_MEM_TO_DEV;
	sdd->rx_dma.direction = DMA_DEV_TO_MEM;

	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = sdd->port_id;
	master->setup = s3c64xx_spi_setup;
	master->cleanup = s3c64xx_spi_cleanup;
	master->prepare_transfer_hardware = s3c64xx_spi_prepare_transfer;
	master->prepare_message = s3c64xx_spi_prepare_message;
	master->transfer_one = s3c64xx_spi_transfer_one;
	master->unprepare_transfer_hardware = s3c64xx_spi_unprepare_transfer;
	master->num_chipselect = sci->num_cs;
	master->dma_alignment = 8;
	master->bits_per_word_mask = SPI_BPW_MASK(32) | SPI_BPW_MASK(16) |
					SPI_BPW_MASK(8);
	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->auto_runtime_pm = true;
	if (!is_polling(sdd))
		master->can_dma = s3c64xx_spi_can_dma;

	sdd->regs = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(sdd->regs)) {
		ret = PTR_ERR(sdd->regs);
		goto err0;
	}

	if (sci->cfg_gpio && sci->cfg_gpio()) {
		dev_err(&pdev->dev, "Unable to config gpio\n");
		ret = -EBUSY;
		goto err0;
	}

	/* Setup clocks */
	sdd->clk = devm_clk_get(&pdev->dev, "spi");
	if (IS_ERR(sdd->clk)) {
		dev_err(&pdev->dev, "Unable to acquire clock 'spi'\n");
		ret = PTR_ERR(sdd->clk);
		goto err0;
	}

	if (clk_prepare_enable(sdd->clk)) {
		dev_err(&pdev->dev, "Couldn't enable clock 'spi'\n");
		ret = -EBUSY;
		goto err0;
	}

	sprintf(clk_name, "spi_busclk%d", sci->src_clk_nr);
	sdd->src_clk = devm_clk_get(&pdev->dev, clk_name);
	if (IS_ERR(sdd->src_clk)) {
		dev_err(&pdev->dev,
			"Unable to acquire clock '%s'\n", clk_name);
		ret = PTR_ERR(sdd->src_clk);
		goto err2;
	}

	if (clk_prepare_enable(sdd->src_clk)) {
		dev_err(&pdev->dev, "Couldn't enable clock '%s'\n", clk_name);
		ret = -EBUSY;
		goto err2;
	}

	/* Setup Deufult Mode */
	s3c64xx_spi_hwinit(sdd, sdd->port_id);

	spin_lock_init(&sdd->lock);
	init_completion(&sdd->xfer_completion);

	ret = devm_request_irq(&pdev->dev, irq, s3c64xx_spi_irq, 0,
				"spi-s3c64xx", sdd);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n",
			irq, ret);
		goto err3;
	}

	writel(S3C64XX_SPI_INT_RX_OVERRUN_EN | S3C64XX_SPI_INT_RX_UNDERRUN_EN |
	       S3C64XX_SPI_INT_TX_OVERRUN_EN | S3C64XX_SPI_INT_TX_UNDERRUN_EN,
	       sdd->regs + S3C64XX_SPI_INT_EN);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret != 0) {
		dev_err(&pdev->dev, "cannot register SPI master: %d\n", ret);
		goto err3;
	}

	dev_dbg(&pdev->dev, "Samsung SoC SPI Driver loaded for Bus SPI-%d with %d Slaves attached\n",
					sdd->port_id, master->num_chipselect);
	dev_dbg(&pdev->dev, "\tIOmem=[%pR]\tDMA=[Rx-%d, Tx-%d]\n",
					mem_res,
					sdd->rx_dma.dmach, sdd->tx_dma.dmach);

	return 0;

err3:
	clk_disable_unprepare(sdd->src_clk);
err2:
	clk_disable_unprepare(sdd->clk);
err0:
	spi_master_put(master);

	return ret;
}

static int s3c64xx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);

	pm_runtime_disable(&pdev->dev);

	writel(0, sdd->regs + S3C64XX_SPI_INT_EN);

	clk_disable_unprepare(sdd->src_clk);

	clk_disable_unprepare(sdd->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int s3c64xx_spi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);

	int ret = spi_master_suspend(master);
	if (ret)
		return ret;

	if (!pm_runtime_suspended(dev)) {
		clk_disable_unprepare(sdd->clk);
		clk_disable_unprepare(sdd->src_clk);
	}

	sdd->cur_speed = 0; /* Output Clock is stopped */

	return 0;
}

static int s3c64xx_spi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;

	if (sci->cfg_gpio)
		sci->cfg_gpio();

	if (!pm_runtime_suspended(dev)) {
		clk_prepare_enable(sdd->src_clk);
		clk_prepare_enable(sdd->clk);
	}

	s3c64xx_spi_hwinit(sdd, sdd->port_id);

	return spi_master_resume(master);
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int s3c64xx_spi_runtime_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);

	clk_disable_unprepare(sdd->clk);
	clk_disable_unprepare(sdd->src_clk);

	return 0;
}

static int s3c64xx_spi_runtime_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(sdd->src_clk);
	if (ret != 0)
		return ret;

	ret = clk_prepare_enable(sdd->clk);
	if (ret != 0) {
		clk_disable_unprepare(sdd->src_clk);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops s3c64xx_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(s3c64xx_spi_suspend, s3c64xx_spi_resume)
	SET_RUNTIME_PM_OPS(s3c64xx_spi_runtime_suspend,
			   s3c64xx_spi_runtime_resume, NULL)
};

static struct s3c64xx_spi_port_config s3c2443_spi_port_config = {
	.fifo_lvl_mask	= { 0x7f },
	.rx_lvl_offset	= 13,
	.tx_st_done	= 21,
	.high_speed	= true,
};

static struct s3c64xx_spi_port_config s3c6410_spi_port_config = {
	.fifo_lvl_mask	= { 0x7f, 0x7F },
	.rx_lvl_offset	= 13,
	.tx_st_done	= 21,
};

static struct s3c64xx_spi_port_config s5pv210_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
};

static struct s3c64xx_spi_port_config exynos4_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F, 0x7F },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
	.clk_from_cmu	= true,
};

static struct s3c64xx_spi_port_config exynos5440_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff },
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
	.clk_from_cmu	= true,
	.quirks		= S3C64XX_SPI_QUIRK_POLL,
};

static struct s3c64xx_spi_port_config exynos7_spi_port_config = {
	.fifo_lvl_mask	= { 0x1ff, 0x7F, 0x7F, 0x7F, 0x7F, 0x1ff},
	.rx_lvl_offset	= 15,
	.tx_st_done	= 25,
	.high_speed	= true,
	.clk_from_cmu	= true,
	.quirks		= S3C64XX_SPI_QUIRK_CS_AUTO,
};

static struct platform_device_id s3c64xx_spi_driver_ids[] = {
	{
		.name		= "s3c2443-spi",
		.driver_data	= (kernel_ulong_t)&s3c2443_spi_port_config,
	}, {
		.name		= "s3c6410-spi",
		.driver_data	= (kernel_ulong_t)&s3c6410_spi_port_config,
	}, {
		.name		= "s5pv210-spi",
		.driver_data	= (kernel_ulong_t)&s5pv210_spi_port_config,
	}, {
		.name		= "exynos4210-spi",
		.driver_data	= (kernel_ulong_t)&exynos4_spi_port_config,
	},
	{ },
};

static const struct of_device_id s3c64xx_spi_dt_match[] = {
	{ .compatible = "samsung,s3c2443-spi",
			.data = (void *)&s3c2443_spi_port_config,
	},
	{ .compatible = "samsung,s3c6410-spi",
			.data = (void *)&s3c6410_spi_port_config,
	},
	{ .compatible = "samsung,s5pv210-spi",
			.data = (void *)&s5pv210_spi_port_config,
	},
	{ .compatible = "samsung,exynos4210-spi",
			.data = (void *)&exynos4_spi_port_config,
	},
	{ .compatible = "samsung,exynos5440-spi",
			.data = (void *)&exynos5440_spi_port_config,
	},
	{ .compatible = "samsung,exynos7-spi",
			.data = (void *)&exynos7_spi_port_config,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, s3c64xx_spi_dt_match);

static struct platform_driver s3c64xx_spi_driver = {
	.driver = {
		.name	= "s3c64xx-spi",
		.pm = &s3c64xx_spi_pm,
		.of_match_table = of_match_ptr(s3c64xx_spi_dt_match),
	},
	.probe = s3c64xx_spi_probe,
	.remove = s3c64xx_spi_remove,
	.id_table = s3c64xx_spi_driver_ids,
};
MODULE_ALIAS("platform:s3c64xx-spi");

module_platform_driver(s3c64xx_spi_driver);

MODULE_AUTHOR("Jaswinder Singh <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("S3C64XX SPI Controller Driver");
MODULE_LICENSE("GPL");
