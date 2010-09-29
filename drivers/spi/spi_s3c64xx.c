/* linux/drivers/spi/spi_s3c64xx.c
 *
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include <mach/dma.h>
#include <plat/s3c64xx-spi.h>

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
#define S3C64XX_SPI_PSR_MASK 		0xff

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

#define S3C64XX_SPI_ACT(c) writel(0, (c)->regs + S3C64XX_SPI_SLAVE_SEL)

#define S3C64XX_SPI_DEACT(c) writel(S3C64XX_SPI_SLAVE_SIG_INACT, \
					(c)->regs + S3C64XX_SPI_SLAVE_SEL)

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

#define S3C64XX_SPI_ST_TRLCNTZ(v, i) ((((v) >> (i)->rx_lvl_offset) & \
					(((i)->fifo_lvl_mask + 1))) \
					? 1 : 0)

#define S3C64XX_SPI_ST_TX_DONE(v, i) ((((v) >> (i)->rx_lvl_offset) & \
					(((i)->fifo_lvl_mask + 1) << 1)) \
					? 1 : 0)
#define TX_FIFO_LVL(v, i) (((v) >> 6) & (i)->fifo_lvl_mask)
#define RX_FIFO_LVL(v, i) (((v) >> (i)->rx_lvl_offset) & (i)->fifo_lvl_mask)

#define S3C64XX_SPI_MAX_TRAILCNT	0x3ff
#define S3C64XX_SPI_TRAILCNT_OFF	19

#define S3C64XX_SPI_TRAILCNT		S3C64XX_SPI_MAX_TRAILCNT

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

#define SUSPND    (1<<0)
#define SPIBUSY   (1<<1)
#define RXBUSY    (1<<2)
#define TXBUSY    (1<<3)

/**
 * struct s3c64xx_spi_driver_data - Runtime info holder for SPI driver.
 * @clk: Pointer to the spi clock.
 * @src_clk: Pointer to the clock used to generate SPI signals.
 * @master: Pointer to the SPI Protocol master.
 * @workqueue: Work queue for the SPI xfer requests.
 * @cntrlr_info: Platform specific data for the controller this driver manages.
 * @tgl_spi: Pointer to the last CS left untoggled by the cs_change hint.
 * @work: Work
 * @queue: To log SPI xfer requests.
 * @lock: Controller specific lock.
 * @state: Set of FLAGS to indicate status.
 * @rx_dmach: Controller's DMA channel for Rx.
 * @tx_dmach: Controller's DMA channel for Tx.
 * @sfr_start: BUS address of SPI controller regs.
 * @regs: Pointer to ioremap'ed controller registers.
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
	struct workqueue_struct	        *workqueue;
	struct s3c64xx_spi_info  *cntrlr_info;
	struct spi_device               *tgl_spi;
	struct work_struct              work;
	struct list_head                queue;
	spinlock_t                      lock;
	enum dma_ch                     rx_dmach;
	enum dma_ch                     tx_dmach;
	unsigned long                   sfr_start;
	struct completion               xfer_completion;
	unsigned                        state;
	unsigned                        cur_mode, cur_bpw;
	unsigned                        cur_speed;
};

static struct s3c2410_dma_client s3c64xx_spi_dma_client = {
	.name = "samsung-spi-dma",
};

static void flush_fifo(struct s3c64xx_spi_driver_data *sdd)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	void __iomem *regs = sdd->regs;
	unsigned long loops;
	u32 val;

	writel(0, regs + S3C64XX_SPI_PACKET_CNT);

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val |= S3C64XX_SPI_CH_SW_RST;
	val &= ~S3C64XX_SPI_CH_HS_EN;
	writel(val, regs + S3C64XX_SPI_CH_CFG);

	/* Flush TxFIFO*/
	loops = msecs_to_loops(1);
	do {
		val = readl(regs + S3C64XX_SPI_STATUS);
	} while (TX_FIFO_LVL(val, sci) && loops--);

	if (loops == 0)
		dev_warn(&sdd->pdev->dev, "Timed out flushing TX FIFO\n");

	/* Flush RxFIFO*/
	loops = msecs_to_loops(1);
	do {
		val = readl(regs + S3C64XX_SPI_STATUS);
		if (RX_FIFO_LVL(val, sci))
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

	val = readl(regs + S3C64XX_SPI_CH_CFG);
	val &= ~(S3C64XX_SPI_CH_RXCH_ON | S3C64XX_SPI_CH_TXCH_ON);
	writel(val, regs + S3C64XX_SPI_CH_CFG);
}

static void enable_datapath(struct s3c64xx_spi_driver_data *sdd,
				struct spi_device *spi,
				struct spi_transfer *xfer, int dma_mode)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
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
			s3c2410_dma_config(sdd->tx_dmach, 1);
			s3c2410_dma_enqueue(sdd->tx_dmach, (void *)sdd,
						xfer->tx_dma, xfer->len);
			s3c2410_dma_ctrl(sdd->tx_dmach, S3C2410_DMAOP_START);
		} else {
			unsigned char *buf = (unsigned char *) xfer->tx_buf;
			int i = 0;
			while (i < xfer->len)
				writeb(buf[i++], regs + S3C64XX_SPI_TX_DATA);
		}
	}

	if (xfer->rx_buf != NULL) {
		sdd->state |= RXBUSY;

		if (sci->high_speed && sdd->cur_speed >= 30000000UL
					&& !(sdd->cur_mode & SPI_CPHA))
			chcfg |= S3C64XX_SPI_CH_HS_EN;

		if (dma_mode) {
			modecfg |= S3C64XX_SPI_MODE_RXDMA_ON;
			chcfg |= S3C64XX_SPI_CH_RXCH_ON;
			writel(((xfer->len * 8 / sdd->cur_bpw) & 0xffff)
					| S3C64XX_SPI_PACKET_CNT_EN,
					regs + S3C64XX_SPI_PACKET_CNT);
			s3c2410_dma_config(sdd->rx_dmach, 1);
			s3c2410_dma_enqueue(sdd->rx_dmach, (void *)sdd,
						xfer->rx_dma, xfer->len);
			s3c2410_dma_ctrl(sdd->rx_dmach, S3C2410_DMAOP_START);
		}
	}

	writel(modecfg, regs + S3C64XX_SPI_MODE_CFG);
	writel(chcfg, regs + S3C64XX_SPI_CH_CFG);
}

static inline void enable_cs(struct s3c64xx_spi_driver_data *sdd,
						struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs;

	if (sdd->tgl_spi != NULL) { /* If last device toggled after mssg */
		if (sdd->tgl_spi != spi) { /* if last mssg on diff device */
			/* Deselect the last toggled device */
			cs = sdd->tgl_spi->controller_data;
			cs->set_level(cs->line,
					spi->mode & SPI_CS_HIGH ? 0 : 1);
		}
		sdd->tgl_spi = NULL;
	}

	cs = spi->controller_data;
	cs->set_level(cs->line, spi->mode & SPI_CS_HIGH ? 1 : 0);
}

static int wait_for_xfer(struct s3c64xx_spi_driver_data *sdd,
				struct spi_transfer *xfer, int dma_mode)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	void __iomem *regs = sdd->regs;
	unsigned long val;
	int ms;

	/* millisecs to xfer 'len' bytes @ 'cur_speed' */
	ms = xfer->len * 8 * 1000 / sdd->cur_speed;
	ms += 10; /* some tolerance */

	if (dma_mode) {
		val = msecs_to_jiffies(ms) + 10;
		val = wait_for_completion_timeout(&sdd->xfer_completion, val);
	} else {
		u32 status;
		val = msecs_to_loops(ms);
		do {
			status = readl(regs + S3C64XX_SPI_STATUS);
		} while (RX_FIFO_LVL(status, sci) < xfer->len && --val);
	}

	if (!val)
		return -EIO;

	if (dma_mode) {
		u32 status;

		/*
		 * DmaTx returns after simply writing data in the FIFO,
		 * w/o waiting for real transmission on the bus to finish.
		 * DmaRx returns only after Dma read data from FIFO which
		 * needs bus transmission to finish, so we don't worry if
		 * Xfer involved Rx(with or without Tx).
		 */
		if (xfer->rx_buf == NULL) {
			val = msecs_to_loops(10);
			status = readl(regs + S3C64XX_SPI_STATUS);
			while ((TX_FIFO_LVL(status, sci)
				|| !S3C64XX_SPI_ST_TX_DONE(status, sci))
					&& --val) {
				cpu_relax();
				status = readl(regs + S3C64XX_SPI_STATUS);
			}

			if (!val)
				return -EIO;
		}
	} else {
		unsigned char *buf;
		int i;

		/* If it was only Tx */
		if (xfer->rx_buf == NULL) {
			sdd->state &= ~TXBUSY;
			return 0;
		}

		i = 0;
		buf = xfer->rx_buf;
		while (i < xfer->len)
			buf[i++] = readb(regs + S3C64XX_SPI_RX_DATA);

		sdd->state &= ~RXBUSY;
	}

	return 0;
}

static inline void disable_cs(struct s3c64xx_spi_driver_data *sdd,
						struct spi_device *spi)
{
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;

	if (sdd->tgl_spi == spi)
		sdd->tgl_spi = NULL;

	cs->set_level(cs->line, spi->mode & SPI_CS_HIGH ? 0 : 1);
}

static void s3c64xx_spi_config(struct s3c64xx_spi_driver_data *sdd)
{
	void __iomem *regs = sdd->regs;
	u32 val;

	/* Disable Clock */
	val = readl(regs + S3C64XX_SPI_CLK_CFG);
	val &= ~S3C64XX_SPI_ENCLK_ENABLE;
	writel(val, regs + S3C64XX_SPI_CLK_CFG);

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
		break;
	case 16:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_HALFWORD;
		break;
	default:
		val |= S3C64XX_SPI_MODE_BUS_TSZ_BYTE;
		break;
	}
	val |= S3C64XX_SPI_MODE_CH_TSZ_BYTE; /* Always 8bits wide */

	writel(val, regs + S3C64XX_SPI_MODE_CFG);

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

static void s3c64xx_spi_dma_rxcb(struct s3c2410_dma_chan *chan, void *buf_id,
				 int size, enum s3c2410_dma_buffresult res)
{
	struct s3c64xx_spi_driver_data *sdd = buf_id;
	unsigned long flags;

	spin_lock_irqsave(&sdd->lock, flags);

	if (res == S3C2410_RES_OK)
		sdd->state &= ~RXBUSY;
	else
		dev_err(&sdd->pdev->dev, "DmaAbrtRx-%d\n", size);

	/* If the other done */
	if (!(sdd->state & TXBUSY))
		complete(&sdd->xfer_completion);

	spin_unlock_irqrestore(&sdd->lock, flags);
}

static void s3c64xx_spi_dma_txcb(struct s3c2410_dma_chan *chan, void *buf_id,
				 int size, enum s3c2410_dma_buffresult res)
{
	struct s3c64xx_spi_driver_data *sdd = buf_id;
	unsigned long flags;

	spin_lock_irqsave(&sdd->lock, flags);

	if (res == S3C2410_RES_OK)
		sdd->state &= ~TXBUSY;
	else
		dev_err(&sdd->pdev->dev, "DmaAbrtTx-%d \n", size);

	/* If the other done */
	if (!(sdd->state & RXBUSY))
		complete(&sdd->xfer_completion);

	spin_unlock_irqrestore(&sdd->lock, flags);
}

#define XFER_DMAADDR_INVALID DMA_BIT_MASK(32)

static int s3c64xx_spi_map_mssg(struct s3c64xx_spi_driver_data *sdd,
						struct spi_message *msg)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	struct device *dev = &sdd->pdev->dev;
	struct spi_transfer *xfer;

	if (msg->is_dma_mapped)
		return 0;

	/* First mark all xfer unmapped */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		xfer->rx_dma = XFER_DMAADDR_INVALID;
		xfer->tx_dma = XFER_DMAADDR_INVALID;
	}

	/* Map until end or first fail */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		if (xfer->len <= ((sci->fifo_lvl_mask >> 1) + 1))
			continue;

		if (xfer->tx_buf != NULL) {
			xfer->tx_dma = dma_map_single(dev,
					(void *)xfer->tx_buf, xfer->len,
					DMA_TO_DEVICE);
			if (dma_mapping_error(dev, xfer->tx_dma)) {
				dev_err(dev, "dma_map_single Tx failed\n");
				xfer->tx_dma = XFER_DMAADDR_INVALID;
				return -ENOMEM;
			}
		}

		if (xfer->rx_buf != NULL) {
			xfer->rx_dma = dma_map_single(dev, xfer->rx_buf,
						xfer->len, DMA_FROM_DEVICE);
			if (dma_mapping_error(dev, xfer->rx_dma)) {
				dev_err(dev, "dma_map_single Rx failed\n");
				dma_unmap_single(dev, xfer->tx_dma,
						xfer->len, DMA_TO_DEVICE);
				xfer->tx_dma = XFER_DMAADDR_INVALID;
				xfer->rx_dma = XFER_DMAADDR_INVALID;
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static void s3c64xx_spi_unmap_mssg(struct s3c64xx_spi_driver_data *sdd,
						struct spi_message *msg)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	struct device *dev = &sdd->pdev->dev;
	struct spi_transfer *xfer;

	if (msg->is_dma_mapped)
		return;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		if (xfer->len <= ((sci->fifo_lvl_mask >> 1) + 1))
			continue;

		if (xfer->rx_buf != NULL
				&& xfer->rx_dma != XFER_DMAADDR_INVALID)
			dma_unmap_single(dev, xfer->rx_dma,
						xfer->len, DMA_FROM_DEVICE);

		if (xfer->tx_buf != NULL
				&& xfer->tx_dma != XFER_DMAADDR_INVALID)
			dma_unmap_single(dev, xfer->tx_dma,
						xfer->len, DMA_TO_DEVICE);
	}
}

static void handle_msg(struct s3c64xx_spi_driver_data *sdd,
					struct spi_message *msg)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	struct spi_device *spi = msg->spi;
	struct s3c64xx_spi_csinfo *cs = spi->controller_data;
	struct spi_transfer *xfer;
	int status = 0, cs_toggle = 0;
	u32 speed;
	u8 bpw;

	/* If Master's(controller) state differs from that needed by Slave */
	if (sdd->cur_speed != spi->max_speed_hz
			|| sdd->cur_mode != spi->mode
			|| sdd->cur_bpw != spi->bits_per_word) {
		sdd->cur_bpw = spi->bits_per_word;
		sdd->cur_speed = spi->max_speed_hz;
		sdd->cur_mode = spi->mode;
		s3c64xx_spi_config(sdd);
	}

	/* Map all the transfers if needed */
	if (s3c64xx_spi_map_mssg(sdd, msg)) {
		dev_err(&spi->dev,
			"Xfer: Unable to map message buffers!\n");
		status = -ENOMEM;
		goto out;
	}

	/* Configure feedback delay */
	writel(cs->fb_delay & 0x3, sdd->regs + S3C64XX_SPI_FB_CLK);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {

		unsigned long flags;
		int use_dma;

		INIT_COMPLETION(sdd->xfer_completion);

		/* Only BPW and Speed may change across transfers */
		bpw = xfer->bits_per_word ? : spi->bits_per_word;
		speed = xfer->speed_hz ? : spi->max_speed_hz;

		if (bpw != sdd->cur_bpw || speed != sdd->cur_speed) {
			sdd->cur_bpw = bpw;
			sdd->cur_speed = speed;
			s3c64xx_spi_config(sdd);
		}

		/* Polling method for xfers not bigger than FIFO capacity */
		if (xfer->len <= ((sci->fifo_lvl_mask >> 1) + 1))
			use_dma = 0;
		else
			use_dma = 1;

		spin_lock_irqsave(&sdd->lock, flags);

		/* Pending only which is to be done */
		sdd->state &= ~RXBUSY;
		sdd->state &= ~TXBUSY;

		enable_datapath(sdd, spi, xfer, use_dma);

		/* Slave Select */
		enable_cs(sdd, spi);

		/* Start the signals */
		S3C64XX_SPI_ACT(sdd);

		spin_unlock_irqrestore(&sdd->lock, flags);

		status = wait_for_xfer(sdd, xfer, use_dma);

		/* Quiese the signals */
		S3C64XX_SPI_DEACT(sdd);

		if (status) {
			dev_err(&spi->dev, "I/O Error: "
				"rx-%d tx-%d res:rx-%c tx-%c len-%d\n",
				xfer->rx_buf ? 1 : 0, xfer->tx_buf ? 1 : 0,
				(sdd->state & RXBUSY) ? 'f' : 'p',
				(sdd->state & TXBUSY) ? 'f' : 'p',
				xfer->len);

			if (use_dma) {
				if (xfer->tx_buf != NULL
						&& (sdd->state & TXBUSY))
					s3c2410_dma_ctrl(sdd->tx_dmach,
							S3C2410_DMAOP_FLUSH);
				if (xfer->rx_buf != NULL
						&& (sdd->state & RXBUSY))
					s3c2410_dma_ctrl(sdd->rx_dmach,
							S3C2410_DMAOP_FLUSH);
			}

			goto out;
		}

		if (xfer->delay_usecs)
			udelay(xfer->delay_usecs);

		if (xfer->cs_change) {
			/* Hint that the next mssg is gonna be
			   for the same device */
			if (list_is_last(&xfer->transfer_list,
						&msg->transfers))
				cs_toggle = 1;
			else
				disable_cs(sdd, spi);
		}

		msg->actual_length += xfer->len;

		flush_fifo(sdd);
	}

out:
	if (!cs_toggle || status)
		disable_cs(sdd, spi);
	else
		sdd->tgl_spi = spi;

	s3c64xx_spi_unmap_mssg(sdd, msg);

	msg->status = status;

	if (msg->complete)
		msg->complete(msg->context);
}

static int acquire_dma(struct s3c64xx_spi_driver_data *sdd)
{
	if (s3c2410_dma_request(sdd->rx_dmach,
					&s3c64xx_spi_dma_client, NULL) < 0) {
		dev_err(&sdd->pdev->dev, "cannot get RxDMA\n");
		return 0;
	}
	s3c2410_dma_set_buffdone_fn(sdd->rx_dmach, s3c64xx_spi_dma_rxcb);
	s3c2410_dma_devconfig(sdd->rx_dmach, S3C2410_DMASRC_HW,
					sdd->sfr_start + S3C64XX_SPI_RX_DATA);

	if (s3c2410_dma_request(sdd->tx_dmach,
					&s3c64xx_spi_dma_client, NULL) < 0) {
		dev_err(&sdd->pdev->dev, "cannot get TxDMA\n");
		s3c2410_dma_free(sdd->rx_dmach, &s3c64xx_spi_dma_client);
		return 0;
	}
	s3c2410_dma_set_buffdone_fn(sdd->tx_dmach, s3c64xx_spi_dma_txcb);
	s3c2410_dma_devconfig(sdd->tx_dmach, S3C2410_DMASRC_MEM,
					sdd->sfr_start + S3C64XX_SPI_TX_DATA);

	return 1;
}

static void s3c64xx_spi_work(struct work_struct *work)
{
	struct s3c64xx_spi_driver_data *sdd = container_of(work,
					struct s3c64xx_spi_driver_data, work);
	unsigned long flags;

	/* Acquire DMA channels */
	while (!acquire_dma(sdd))
		msleep(10);

	spin_lock_irqsave(&sdd->lock, flags);

	while (!list_empty(&sdd->queue)
				&& !(sdd->state & SUSPND)) {

		struct spi_message *msg;

		msg = container_of(sdd->queue.next, struct spi_message, queue);

		list_del_init(&msg->queue);

		/* Set Xfer busy flag */
		sdd->state |= SPIBUSY;

		spin_unlock_irqrestore(&sdd->lock, flags);

		handle_msg(sdd, msg);

		spin_lock_irqsave(&sdd->lock, flags);

		sdd->state &= ~SPIBUSY;
	}

	spin_unlock_irqrestore(&sdd->lock, flags);

	/* Free DMA channels */
	s3c2410_dma_free(sdd->tx_dmach, &s3c64xx_spi_dma_client);
	s3c2410_dma_free(sdd->rx_dmach, &s3c64xx_spi_dma_client);
}

static int s3c64xx_spi_transfer(struct spi_device *spi,
						struct spi_message *msg)
{
	struct s3c64xx_spi_driver_data *sdd;
	unsigned long flags;

	sdd = spi_master_get_devdata(spi->master);

	spin_lock_irqsave(&sdd->lock, flags);

	if (sdd->state & SUSPND) {
		spin_unlock_irqrestore(&sdd->lock, flags);
		return -ESHUTDOWN;
	}

	msg->status = -EINPROGRESS;
	msg->actual_length = 0;

	list_add_tail(&msg->queue, &sdd->queue);

	queue_work(sdd->workqueue, &sdd->work);

	spin_unlock_irqrestore(&sdd->lock, flags);

	return 0;
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
	struct spi_message *msg;
	u32 psr, speed;
	unsigned long flags;
	int err = 0;

	if (cs == NULL || cs->set_level == NULL) {
		dev_err(&spi->dev, "No CS for SPI(%d)\n", spi->chip_select);
		return -ENODEV;
	}

	sdd = spi_master_get_devdata(spi->master);
	sci = sdd->cntrlr_info;

	spin_lock_irqsave(&sdd->lock, flags);

	list_for_each_entry(msg, &sdd->queue, queue) {
		/* Is some mssg is already queued for this device */
		if (msg->spi == spi) {
			dev_err(&spi->dev,
				"setup: attempt while mssg in queue!\n");
			spin_unlock_irqrestore(&sdd->lock, flags);
			return -EBUSY;
		}
	}

	if (sdd->state & SUSPND) {
		spin_unlock_irqrestore(&sdd->lock, flags);
		dev_err(&spi->dev,
			"setup: SPI-%d not active!\n", spi->master->bus_num);
		return -ESHUTDOWN;
	}

	spin_unlock_irqrestore(&sdd->lock, flags);

	if (spi->bits_per_word != 8
			&& spi->bits_per_word != 16
			&& spi->bits_per_word != 32) {
		dev_err(&spi->dev, "setup: %dbits/wrd not supported!\n",
							spi->bits_per_word);
		err = -EINVAL;
		goto setup_exit;
	}

	/* Check if we can provide the requested rate */
	speed = clk_get_rate(sdd->src_clk) / 2 / (0 + 1); /* Max possible */

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
	if (spi->max_speed_hz >= speed)
		spi->max_speed_hz = speed;
	else
		err = -EINVAL;

setup_exit:

	/* setup() returns with device de-selected */
	disable_cs(sdd, spi);

	return err;
}

static void s3c64xx_spi_hwinit(struct s3c64xx_spi_driver_data *sdd, int channel)
{
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	void __iomem *regs = sdd->regs;
	unsigned int val;

	sdd->cur_speed = 0;

	S3C64XX_SPI_DEACT(sdd);

	/* Disable Interrupts - we use Polling if not DMA mode */
	writel(0, regs + S3C64XX_SPI_INT_EN);

	writel(sci->src_clk_nr << S3C64XX_SPI_CLKSEL_SRCSHFT,
				regs + S3C64XX_SPI_CLK_CFG);
	writel(0, regs + S3C64XX_SPI_MODE_CFG);
	writel(0, regs + S3C64XX_SPI_PACKET_CNT);

	/* Clear any irq pending bits */
	writel(readl(regs + S3C64XX_SPI_PENDING_CLR),
				regs + S3C64XX_SPI_PENDING_CLR);

	writel(0, regs + S3C64XX_SPI_SWAP_CFG);

	val = readl(regs + S3C64XX_SPI_MODE_CFG);
	val &= ~S3C64XX_SPI_MODE_4BURST;
	val &= ~(S3C64XX_SPI_MAX_TRAILCNT << S3C64XX_SPI_TRAILCNT_OFF);
	val |= (S3C64XX_SPI_TRAILCNT << S3C64XX_SPI_TRAILCNT_OFF);
	writel(val, regs + S3C64XX_SPI_MODE_CFG);

	flush_fifo(sdd);
}

static int __init s3c64xx_spi_probe(struct platform_device *pdev)
{
	struct resource	*mem_res, *dmatx_res, *dmarx_res;
	struct s3c64xx_spi_driver_data *sdd;
	struct s3c64xx_spi_info *sci;
	struct spi_master *master;
	int ret;

	if (pdev->id < 0) {
		dev_err(&pdev->dev,
				"Invalid platform device id-%d\n", pdev->id);
		return -ENODEV;
	}

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "platform_data missing!\n");
		return -ENODEV;
	}

	sci = pdev->dev.platform_data;
	if (!sci->src_clk_name) {
		dev_err(&pdev->dev,
			"Board init must call s3c64xx_spi_set_info()\n");
		return -EINVAL;
	}

	/* Check for availability of necessary resource */

	dmatx_res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (dmatx_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI-Tx dma resource\n");
		return -ENXIO;
	}

	dmarx_res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (dmarx_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI-Rx dma resource\n");
		return -ENXIO;
	}

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res == NULL) {
		dev_err(&pdev->dev, "Unable to get SPI MEM resource\n");
		return -ENXIO;
	}

	master = spi_alloc_master(&pdev->dev,
				sizeof(struct s3c64xx_spi_driver_data));
	if (master == NULL) {
		dev_err(&pdev->dev, "Unable to allocate SPI Master\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, master);

	sdd = spi_master_get_devdata(master);
	sdd->master = master;
	sdd->cntrlr_info = sci;
	sdd->pdev = pdev;
	sdd->sfr_start = mem_res->start;
	sdd->tx_dmach = dmatx_res->start;
	sdd->rx_dmach = dmarx_res->start;

	sdd->cur_bpw = 8;

	master->bus_num = pdev->id;
	master->setup = s3c64xx_spi_setup;
	master->transfer = s3c64xx_spi_transfer;
	master->num_chipselect = sci->num_cs;
	master->dma_alignment = 8;
	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	if (request_mem_region(mem_res->start,
			resource_size(mem_res), pdev->name) == NULL) {
		dev_err(&pdev->dev, "Req mem region failed\n");
		ret = -ENXIO;
		goto err0;
	}

	sdd->regs = ioremap(mem_res->start, resource_size(mem_res));
	if (sdd->regs == NULL) {
		dev_err(&pdev->dev, "Unable to remap IO\n");
		ret = -ENXIO;
		goto err1;
	}

	if (sci->cfg_gpio == NULL || sci->cfg_gpio(pdev)) {
		dev_err(&pdev->dev, "Unable to config gpio\n");
		ret = -EBUSY;
		goto err2;
	}

	/* Setup clocks */
	sdd->clk = clk_get(&pdev->dev, "spi");
	if (IS_ERR(sdd->clk)) {
		dev_err(&pdev->dev, "Unable to acquire clock 'spi'\n");
		ret = PTR_ERR(sdd->clk);
		goto err3;
	}

	if (clk_enable(sdd->clk)) {
		dev_err(&pdev->dev, "Couldn't enable clock 'spi'\n");
		ret = -EBUSY;
		goto err4;
	}

	sdd->src_clk = clk_get(&pdev->dev, sci->src_clk_name);
	if (IS_ERR(sdd->src_clk)) {
		dev_err(&pdev->dev,
			"Unable to acquire clock '%s'\n", sci->src_clk_name);
		ret = PTR_ERR(sdd->src_clk);
		goto err5;
	}

	if (clk_enable(sdd->src_clk)) {
		dev_err(&pdev->dev, "Couldn't enable clock '%s'\n",
							sci->src_clk_name);
		ret = -EBUSY;
		goto err6;
	}

	sdd->workqueue = create_singlethread_workqueue(
						dev_name(master->dev.parent));
	if (sdd->workqueue == NULL) {
		dev_err(&pdev->dev, "Unable to create workqueue\n");
		ret = -ENOMEM;
		goto err7;
	}

	/* Setup Deufult Mode */
	s3c64xx_spi_hwinit(sdd, pdev->id);

	spin_lock_init(&sdd->lock);
	init_completion(&sdd->xfer_completion);
	INIT_WORK(&sdd->work, s3c64xx_spi_work);
	INIT_LIST_HEAD(&sdd->queue);

	if (spi_register_master(master)) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		ret = -EBUSY;
		goto err8;
	}

	dev_dbg(&pdev->dev, "Samsung SoC SPI Driver loaded for Bus SPI-%d "
					"with %d Slaves attached\n",
					pdev->id, master->num_chipselect);
	dev_dbg(&pdev->dev, "\tIOmem=[0x%x-0x%x]\tDMA=[Rx-%d, Tx-%d]\n",
					mem_res->end, mem_res->start,
					sdd->rx_dmach, sdd->tx_dmach);

	return 0;

err8:
	destroy_workqueue(sdd->workqueue);
err7:
	clk_disable(sdd->src_clk);
err6:
	clk_put(sdd->src_clk);
err5:
	clk_disable(sdd->clk);
err4:
	clk_put(sdd->clk);
err3:
err2:
	iounmap((void *) sdd->regs);
err1:
	release_mem_region(mem_res->start, resource_size(mem_res));
err0:
	platform_set_drvdata(pdev, NULL);
	spi_master_put(master);

	return ret;
}

static int s3c64xx_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct resource	*mem_res;
	unsigned long flags;

	spin_lock_irqsave(&sdd->lock, flags);
	sdd->state |= SUSPND;
	spin_unlock_irqrestore(&sdd->lock, flags);

	while (sdd->state & SPIBUSY)
		msleep(10);

	spi_unregister_master(master);

	destroy_workqueue(sdd->workqueue);

	clk_disable(sdd->src_clk);
	clk_put(sdd->src_clk);

	clk_disable(sdd->clk);
	clk_put(sdd->clk);

	iounmap((void *) sdd->regs);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem_res != NULL)
		release_mem_region(mem_res->start, resource_size(mem_res));

	platform_set_drvdata(pdev, NULL);
	spi_master_put(master);

	return 0;
}

#ifdef CONFIG_PM
static int s3c64xx_spi_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	unsigned long flags;

	spin_lock_irqsave(&sdd->lock, flags);
	sdd->state |= SUSPND;
	spin_unlock_irqrestore(&sdd->lock, flags);

	while (sdd->state & SPIBUSY)
		msleep(10);

	/* Disable the clock */
	clk_disable(sdd->src_clk);
	clk_disable(sdd->clk);

	sdd->cur_speed = 0; /* Output Clock is stopped */

	return 0;
}

static int s3c64xx_spi_resume(struct platform_device *pdev)
{
	struct spi_master *master = spi_master_get(platform_get_drvdata(pdev));
	struct s3c64xx_spi_driver_data *sdd = spi_master_get_devdata(master);
	struct s3c64xx_spi_info *sci = sdd->cntrlr_info;
	unsigned long flags;

	sci->cfg_gpio(pdev);

	/* Enable the clock */
	clk_enable(sdd->src_clk);
	clk_enable(sdd->clk);

	s3c64xx_spi_hwinit(sdd, pdev->id);

	spin_lock_irqsave(&sdd->lock, flags);
	sdd->state &= ~SUSPND;
	spin_unlock_irqrestore(&sdd->lock, flags);

	return 0;
}
#else
#define s3c64xx_spi_suspend	NULL
#define s3c64xx_spi_resume	NULL
#endif /* CONFIG_PM */

static struct platform_driver s3c64xx_spi_driver = {
	.driver = {
		.name	= "s3c64xx-spi",
		.owner = THIS_MODULE,
	},
	.remove = s3c64xx_spi_remove,
	.suspend = s3c64xx_spi_suspend,
	.resume = s3c64xx_spi_resume,
};
MODULE_ALIAS("platform:s3c64xx-spi");

static int __init s3c64xx_spi_init(void)
{
	return platform_driver_probe(&s3c64xx_spi_driver, s3c64xx_spi_probe);
}
subsys_initcall(s3c64xx_spi_init);

static void __exit s3c64xx_spi_exit(void)
{
	platform_driver_unregister(&s3c64xx_spi_driver);
}
module_exit(s3c64xx_spi_exit);

MODULE_AUTHOR("Jaswinder Singh <jassi.brar@samsung.com>");
MODULE_DESCRIPTION("S3C64XX SPI Controller Driver");
MODULE_LICENSE("GPL");
