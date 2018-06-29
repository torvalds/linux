/*
 * drivers/spi/spi-fsl-dspi.c
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * Freescale DSPI driver
 * This file contains a driver for the Freescale DSPI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/time.h>

#define DRIVER_NAME "fsl-dspi"

#define TRAN_STATE_RX_VOID		0x01
#define TRAN_STATE_TX_VOID		0x02
#define TRAN_STATE_WORD_ODD_NUM	0x04

#define DSPI_FIFO_SIZE			4
#define DSPI_DMA_BUFSIZE		(DSPI_FIFO_SIZE * 1024)

#define SPI_MCR		0x00
#define SPI_MCR_MASTER		(1 << 31)
#define SPI_MCR_PCSIS		(0x3F << 16)
#define SPI_MCR_CLR_TXF	(1 << 11)
#define SPI_MCR_CLR_RXF	(1 << 10)

#define SPI_TCR			0x08
#define SPI_TCR_GET_TCNT(x)	(((x) & 0xffff0000) >> 16)

#define SPI_CTAR(x)		(0x0c + (((x) & 0x3) * 4))
#define SPI_CTAR_FMSZ(x)	(((x) & 0x0000000f) << 27)
#define SPI_CTAR_CPOL(x)	((x) << 26)
#define SPI_CTAR_CPHA(x)	((x) << 25)
#define SPI_CTAR_LSBFE(x)	((x) << 24)
#define SPI_CTAR_PCSSCK(x)	(((x) & 0x00000003) << 22)
#define SPI_CTAR_PASC(x)	(((x) & 0x00000003) << 20)
#define SPI_CTAR_PDT(x)	(((x) & 0x00000003) << 18)
#define SPI_CTAR_PBR(x)	(((x) & 0x00000003) << 16)
#define SPI_CTAR_CSSCK(x)	(((x) & 0x0000000f) << 12)
#define SPI_CTAR_ASC(x)	(((x) & 0x0000000f) << 8)
#define SPI_CTAR_DT(x)		(((x) & 0x0000000f) << 4)
#define SPI_CTAR_BR(x)		((x) & 0x0000000f)
#define SPI_CTAR_SCALE_BITS	0xf

#define SPI_CTAR0_SLAVE	0x0c

#define SPI_SR			0x2c
#define SPI_SR_EOQF		0x10000000
#define SPI_SR_TCFQF		0x80000000
#define SPI_SR_CLEAR		0xdaad0000

#define SPI_RSER_TFFFE		BIT(25)
#define SPI_RSER_TFFFD		BIT(24)
#define SPI_RSER_RFDFE		BIT(17)
#define SPI_RSER_RFDFD		BIT(16)

#define SPI_RSER		0x30
#define SPI_RSER_EOQFE		0x10000000
#define SPI_RSER_TCFQE		0x80000000

#define SPI_PUSHR		0x34
#define SPI_PUSHR_CONT		(1 << 31)
#define SPI_PUSHR_CTAS(x)	(((x) & 0x00000003) << 28)
#define SPI_PUSHR_EOQ		(1 << 27)
#define SPI_PUSHR_CTCNT	(1 << 26)
#define SPI_PUSHR_PCS(x)	(((1 << x) & 0x0000003f) << 16)
#define SPI_PUSHR_TXDATA(x)	((x) & 0x0000ffff)

#define SPI_PUSHR_SLAVE	0x34

#define SPI_POPR		0x38
#define SPI_POPR_RXDATA(x)	((x) & 0x0000ffff)

#define SPI_TXFR0		0x3c
#define SPI_TXFR1		0x40
#define SPI_TXFR2		0x44
#define SPI_TXFR3		0x48
#define SPI_RXFR0		0x7c
#define SPI_RXFR1		0x80
#define SPI_RXFR2		0x84
#define SPI_RXFR3		0x88

#define SPI_FRAME_BITS(bits)	SPI_CTAR_FMSZ((bits) - 1)
#define SPI_FRAME_BITS_MASK	SPI_CTAR_FMSZ(0xf)
#define SPI_FRAME_BITS_16	SPI_CTAR_FMSZ(0xf)
#define SPI_FRAME_BITS_8	SPI_CTAR_FMSZ(0x7)

#define SPI_CS_INIT		0x01
#define SPI_CS_ASSERT		0x02
#define SPI_CS_DROP		0x04

#define SPI_TCR_TCNT_MAX	0x10000

#define DMA_COMPLETION_TIMEOUT	msecs_to_jiffies(3000)

struct chip_data {
	u32 mcr_val;
	u32 ctar_val;
	u16 void_write_data;
};

enum dspi_trans_mode {
	DSPI_EOQ_MODE = 0,
	DSPI_TCFQ_MODE,
	DSPI_DMA_MODE,
};

struct fsl_dspi_devtype_data {
	enum dspi_trans_mode trans_mode;
	u8 max_clock_factor;
};

static const struct fsl_dspi_devtype_data vf610_data = {
	.trans_mode = DSPI_DMA_MODE,
	.max_clock_factor = 2,
};

static const struct fsl_dspi_devtype_data ls1021a_v1_data = {
	.trans_mode = DSPI_TCFQ_MODE,
	.max_clock_factor = 8,
};

static const struct fsl_dspi_devtype_data ls2085a_data = {
	.trans_mode = DSPI_TCFQ_MODE,
	.max_clock_factor = 8,
};

struct fsl_dspi_dma {
	/* Length of transfer in words of DSPI_FIFO_SIZE */
	u32 curr_xfer_len;

	u32 *tx_dma_buf;
	struct dma_chan *chan_tx;
	dma_addr_t tx_dma_phys;
	struct completion cmd_tx_complete;
	struct dma_async_tx_descriptor *tx_desc;

	u32 *rx_dma_buf;
	struct dma_chan *chan_rx;
	dma_addr_t rx_dma_phys;
	struct completion cmd_rx_complete;
	struct dma_async_tx_descriptor *rx_desc;
};

struct fsl_dspi {
	struct spi_master	*master;
	struct platform_device	*pdev;

	struct regmap		*regmap;
	int			irq;
	struct clk		*clk;

	struct spi_transfer	*cur_transfer;
	struct spi_message	*cur_msg;
	struct chip_data	*cur_chip;
	size_t			len;
	void			*tx;
	void			*tx_end;
	void			*rx;
	void			*rx_end;
	char			dataflags;
	u8			cs;
	u16			void_write_data;
	u32			cs_change;
	const struct fsl_dspi_devtype_data *devtype_data;

	wait_queue_head_t	waitq;
	u32			waitflags;

	u32			spi_tcnt;
	struct fsl_dspi_dma	*dma;
};

static u32 dspi_data_to_pushr(struct fsl_dspi *dspi, int tx_word);

static inline int is_double_byte_mode(struct fsl_dspi *dspi)
{
	unsigned int val;

	regmap_read(dspi->regmap, SPI_CTAR(0), &val);

	return ((val & SPI_FRAME_BITS_MASK) == SPI_FRAME_BITS(8)) ? 0 : 1;
}

static void dspi_tx_dma_callback(void *arg)
{
	struct fsl_dspi *dspi = arg;
	struct fsl_dspi_dma *dma = dspi->dma;

	complete(&dma->cmd_tx_complete);
}

static void dspi_rx_dma_callback(void *arg)
{
	struct fsl_dspi *dspi = arg;
	struct fsl_dspi_dma *dma = dspi->dma;
	int rx_word;
	int i;
	u16 d;

	rx_word = is_double_byte_mode(dspi);

	if (!(dspi->dataflags & TRAN_STATE_RX_VOID)) {
		for (i = 0; i < dma->curr_xfer_len; i++) {
			d = dspi->dma->rx_dma_buf[i];
			rx_word ? (*(u16 *)dspi->rx = d) :
						(*(u8 *)dspi->rx = d);
			dspi->rx += rx_word + 1;
		}
	}

	complete(&dma->cmd_rx_complete);
}

static int dspi_next_xfer_dma_submit(struct fsl_dspi *dspi)
{
	struct fsl_dspi_dma *dma = dspi->dma;
	struct device *dev = &dspi->pdev->dev;
	int time_left;
	int tx_word;
	int i;

	tx_word = is_double_byte_mode(dspi);

	for (i = 0; i < dma->curr_xfer_len; i++) {
		dspi->dma->tx_dma_buf[i] = dspi_data_to_pushr(dspi, tx_word);
		if ((dspi->cs_change) && (!dspi->len))
			dspi->dma->tx_dma_buf[i] &= ~SPI_PUSHR_CONT;
	}

	dma->tx_desc = dmaengine_prep_slave_single(dma->chan_tx,
					dma->tx_dma_phys,
					dma->curr_xfer_len *
					DMA_SLAVE_BUSWIDTH_4_BYTES,
					DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!dma->tx_desc) {
		dev_err(dev, "Not able to get desc for DMA xfer\n");
		return -EIO;
	}

	dma->tx_desc->callback = dspi_tx_dma_callback;
	dma->tx_desc->callback_param = dspi;
	if (dma_submit_error(dmaengine_submit(dma->tx_desc))) {
		dev_err(dev, "DMA submit failed\n");
		return -EINVAL;
	}

	dma->rx_desc = dmaengine_prep_slave_single(dma->chan_rx,
					dma->rx_dma_phys,
					dma->curr_xfer_len *
					DMA_SLAVE_BUSWIDTH_4_BYTES,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!dma->rx_desc) {
		dev_err(dev, "Not able to get desc for DMA xfer\n");
		return -EIO;
	}

	dma->rx_desc->callback = dspi_rx_dma_callback;
	dma->rx_desc->callback_param = dspi;
	if (dma_submit_error(dmaengine_submit(dma->rx_desc))) {
		dev_err(dev, "DMA submit failed\n");
		return -EINVAL;
	}

	reinit_completion(&dspi->dma->cmd_rx_complete);
	reinit_completion(&dspi->dma->cmd_tx_complete);

	dma_async_issue_pending(dma->chan_rx);
	dma_async_issue_pending(dma->chan_tx);

	time_left = wait_for_completion_timeout(&dspi->dma->cmd_tx_complete,
					DMA_COMPLETION_TIMEOUT);
	if (time_left == 0) {
		dev_err(dev, "DMA tx timeout\n");
		dmaengine_terminate_all(dma->chan_tx);
		dmaengine_terminate_all(dma->chan_rx);
		return -ETIMEDOUT;
	}

	time_left = wait_for_completion_timeout(&dspi->dma->cmd_rx_complete,
					DMA_COMPLETION_TIMEOUT);
	if (time_left == 0) {
		dev_err(dev, "DMA rx timeout\n");
		dmaengine_terminate_all(dma->chan_tx);
		dmaengine_terminate_all(dma->chan_rx);
		return -ETIMEDOUT;
	}

	return 0;
}

static int dspi_dma_xfer(struct fsl_dspi *dspi)
{
	struct fsl_dspi_dma *dma = dspi->dma;
	struct device *dev = &dspi->pdev->dev;
	int curr_remaining_bytes;
	int bytes_per_buffer;
	int word = 1;
	int ret = 0;

	if (is_double_byte_mode(dspi))
		word = 2;
	curr_remaining_bytes = dspi->len;
	bytes_per_buffer = DSPI_DMA_BUFSIZE / DSPI_FIFO_SIZE;
	while (curr_remaining_bytes) {
		/* Check if current transfer fits the DMA buffer */
		dma->curr_xfer_len = curr_remaining_bytes / word;
		if (dma->curr_xfer_len > bytes_per_buffer)
			dma->curr_xfer_len = bytes_per_buffer;

		ret = dspi_next_xfer_dma_submit(dspi);
		if (ret) {
			dev_err(dev, "DMA transfer failed\n");
			goto exit;

		} else {
			curr_remaining_bytes -= dma->curr_xfer_len * word;
			if (curr_remaining_bytes < 0)
				curr_remaining_bytes = 0;
		}
	}

exit:
	return ret;
}

static int dspi_request_dma(struct fsl_dspi *dspi, phys_addr_t phy_addr)
{
	struct fsl_dspi_dma *dma;
	struct dma_slave_config cfg;
	struct device *dev = &dspi->pdev->dev;
	int ret;

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->chan_rx = dma_request_slave_channel(dev, "rx");
	if (!dma->chan_rx) {
		dev_err(dev, "rx dma channel not available\n");
		ret = -ENODEV;
		return ret;
	}

	dma->chan_tx = dma_request_slave_channel(dev, "tx");
	if (!dma->chan_tx) {
		dev_err(dev, "tx dma channel not available\n");
		ret = -ENODEV;
		goto err_tx_channel;
	}

	dma->tx_dma_buf = dma_alloc_coherent(dev, DSPI_DMA_BUFSIZE,
					&dma->tx_dma_phys, GFP_KERNEL);
	if (!dma->tx_dma_buf) {
		ret = -ENOMEM;
		goto err_tx_dma_buf;
	}

	dma->rx_dma_buf = dma_alloc_coherent(dev, DSPI_DMA_BUFSIZE,
					&dma->rx_dma_phys, GFP_KERNEL);
	if (!dma->rx_dma_buf) {
		ret = -ENOMEM;
		goto err_rx_dma_buf;
	}

	cfg.src_addr = phy_addr + SPI_POPR;
	cfg.dst_addr = phy_addr + SPI_PUSHR;
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.src_maxburst = 1;
	cfg.dst_maxburst = 1;

	cfg.direction = DMA_DEV_TO_MEM;
	ret = dmaengine_slave_config(dma->chan_rx, &cfg);
	if (ret) {
		dev_err(dev, "can't configure rx dma channel\n");
		ret = -EINVAL;
		goto err_slave_config;
	}

	cfg.direction = DMA_MEM_TO_DEV;
	ret = dmaengine_slave_config(dma->chan_tx, &cfg);
	if (ret) {
		dev_err(dev, "can't configure tx dma channel\n");
		ret = -EINVAL;
		goto err_slave_config;
	}

	dspi->dma = dma;
	init_completion(&dma->cmd_tx_complete);
	init_completion(&dma->cmd_rx_complete);

	return 0;

err_slave_config:
	dma_free_coherent(dev, DSPI_DMA_BUFSIZE,
			dma->rx_dma_buf, dma->rx_dma_phys);
err_rx_dma_buf:
	dma_free_coherent(dev, DSPI_DMA_BUFSIZE,
			dma->tx_dma_buf, dma->tx_dma_phys);
err_tx_dma_buf:
	dma_release_channel(dma->chan_tx);
err_tx_channel:
	dma_release_channel(dma->chan_rx);

	devm_kfree(dev, dma);
	dspi->dma = NULL;

	return ret;
}

static void dspi_release_dma(struct fsl_dspi *dspi)
{
	struct fsl_dspi_dma *dma = dspi->dma;
	struct device *dev = &dspi->pdev->dev;

	if (dma) {
		if (dma->chan_tx) {
			dma_unmap_single(dev, dma->tx_dma_phys,
					DSPI_DMA_BUFSIZE, DMA_TO_DEVICE);
			dma_release_channel(dma->chan_tx);
		}

		if (dma->chan_rx) {
			dma_unmap_single(dev, dma->rx_dma_phys,
					DSPI_DMA_BUFSIZE, DMA_FROM_DEVICE);
			dma_release_channel(dma->chan_rx);
		}
	}
}

static void hz_to_spi_baud(char *pbr, char *br, int speed_hz,
		unsigned long clkrate)
{
	/* Valid baud rate pre-scaler values */
	int pbr_tbl[4] = {2, 3, 5, 7};
	int brs[16] = {	2,	4,	6,	8,
		16,	32,	64,	128,
		256,	512,	1024,	2048,
		4096,	8192,	16384,	32768 };
	int scale_needed, scale, minscale = INT_MAX;
	int i, j;

	scale_needed = clkrate / speed_hz;
	if (clkrate % speed_hz)
		scale_needed++;

	for (i = 0; i < ARRAY_SIZE(brs); i++)
		for (j = 0; j < ARRAY_SIZE(pbr_tbl); j++) {
			scale = brs[i] * pbr_tbl[j];
			if (scale >= scale_needed) {
				if (scale < minscale) {
					minscale = scale;
					*br = i;
					*pbr = j;
				}
				break;
			}
		}

	if (minscale == INT_MAX) {
		pr_warn("Can not find valid baud rate,speed_hz is %d,clkrate is %ld, we use the max prescaler value.\n",
			speed_hz, clkrate);
		*pbr = ARRAY_SIZE(pbr_tbl) - 1;
		*br =  ARRAY_SIZE(brs) - 1;
	}
}

static void ns_delay_scale(char *psc, char *sc, int delay_ns,
		unsigned long clkrate)
{
	int pscale_tbl[4] = {1, 3, 5, 7};
	int scale_needed, scale, minscale = INT_MAX;
	int i, j;
	u32 remainder;

	scale_needed = div_u64_rem((u64)delay_ns * clkrate, NSEC_PER_SEC,
			&remainder);
	if (remainder)
		scale_needed++;

	for (i = 0; i < ARRAY_SIZE(pscale_tbl); i++)
		for (j = 0; j <= SPI_CTAR_SCALE_BITS; j++) {
			scale = pscale_tbl[i] * (2 << j);
			if (scale >= scale_needed) {
				if (scale < minscale) {
					minscale = scale;
					*psc = i;
					*sc = j;
				}
				break;
			}
		}

	if (minscale == INT_MAX) {
		pr_warn("Cannot find correct scale values for %dns delay at clkrate %ld, using max prescaler value",
			delay_ns, clkrate);
		*psc = ARRAY_SIZE(pscale_tbl) - 1;
		*sc = SPI_CTAR_SCALE_BITS;
	}
}

static u32 dspi_data_to_pushr(struct fsl_dspi *dspi, int tx_word)
{
	u16 d16;

	if (!(dspi->dataflags & TRAN_STATE_TX_VOID))
		d16 = tx_word ? *(u16 *)dspi->tx : *(u8 *)dspi->tx;
	else
		d16 = dspi->void_write_data;

	dspi->tx += tx_word + 1;
	dspi->len -= tx_word + 1;

	return	SPI_PUSHR_TXDATA(d16) |
		SPI_PUSHR_PCS(dspi->cs) |
		SPI_PUSHR_CTAS(0) |
		SPI_PUSHR_CONT;
}

static void dspi_data_from_popr(struct fsl_dspi *dspi, int rx_word)
{
	u16 d;
	unsigned int val;

	regmap_read(dspi->regmap, SPI_POPR, &val);
	d = SPI_POPR_RXDATA(val);

	if (!(dspi->dataflags & TRAN_STATE_RX_VOID))
		rx_word ? (*(u16 *)dspi->rx = d) : (*(u8 *)dspi->rx = d);

	dspi->rx += rx_word + 1;
}

static int dspi_eoq_write(struct fsl_dspi *dspi)
{
	int tx_count = 0;
	int tx_word;
	u32 dspi_pushr = 0;

	tx_word = is_double_byte_mode(dspi);

	while (dspi->len && (tx_count < DSPI_FIFO_SIZE)) {
		/* If we are in word mode, only have a single byte to transfer
		 * switch to byte mode temporarily.  Will switch back at the
		 * end of the transfer.
		 */
		if (tx_word && (dspi->len == 1)) {
			dspi->dataflags |= TRAN_STATE_WORD_ODD_NUM;
			regmap_update_bits(dspi->regmap, SPI_CTAR(0),
					SPI_FRAME_BITS_MASK, SPI_FRAME_BITS(8));
			tx_word = 0;
		}

		dspi_pushr = dspi_data_to_pushr(dspi, tx_word);

		if (dspi->len == 0 || tx_count == DSPI_FIFO_SIZE - 1) {
			/* last transfer in the transfer */
			dspi_pushr |= SPI_PUSHR_EOQ;
			if ((dspi->cs_change) && (!dspi->len))
				dspi_pushr &= ~SPI_PUSHR_CONT;
		} else if (tx_word && (dspi->len == 1))
			dspi_pushr |= SPI_PUSHR_EOQ;

		regmap_write(dspi->regmap, SPI_PUSHR, dspi_pushr);

		tx_count++;
	}

	return tx_count * (tx_word + 1);
}

static int dspi_eoq_read(struct fsl_dspi *dspi)
{
	int rx_count = 0;
	int rx_word = is_double_byte_mode(dspi);

	while ((dspi->rx < dspi->rx_end)
			&& (rx_count < DSPI_FIFO_SIZE)) {
		if (rx_word && (dspi->rx_end - dspi->rx) == 1)
			rx_word = 0;

		dspi_data_from_popr(dspi, rx_word);
		rx_count++;
	}

	return rx_count;
}

static int dspi_tcfq_write(struct fsl_dspi *dspi)
{
	int tx_word;
	u32 dspi_pushr = 0;

	tx_word = is_double_byte_mode(dspi);

	if (tx_word && (dspi->len == 1)) {
		dspi->dataflags |= TRAN_STATE_WORD_ODD_NUM;
		regmap_update_bits(dspi->regmap, SPI_CTAR(0),
				SPI_FRAME_BITS_MASK, SPI_FRAME_BITS(8));
		tx_word = 0;
	}

	dspi_pushr = dspi_data_to_pushr(dspi, tx_word);

	if ((dspi->cs_change) && (!dspi->len))
		dspi_pushr &= ~SPI_PUSHR_CONT;

	regmap_write(dspi->regmap, SPI_PUSHR, dspi_pushr);

	return tx_word + 1;
}

static void dspi_tcfq_read(struct fsl_dspi *dspi)
{
	int rx_word = is_double_byte_mode(dspi);

	if (rx_word && (dspi->rx_end - dspi->rx) == 1)
		rx_word = 0;

	dspi_data_from_popr(dspi, rx_word);
}

static int dspi_transfer_one_message(struct spi_master *master,
		struct spi_message *message)
{
	struct fsl_dspi *dspi = spi_master_get_devdata(master);
	struct spi_device *spi = message->spi;
	struct spi_transfer *transfer;
	int status = 0;
	enum dspi_trans_mode trans_mode;
	u32 spi_tcr;

	regmap_read(dspi->regmap, SPI_TCR, &spi_tcr);
	dspi->spi_tcnt = SPI_TCR_GET_TCNT(spi_tcr);

	message->actual_length = 0;

	list_for_each_entry(transfer, &message->transfers, transfer_list) {
		dspi->cur_transfer = transfer;
		dspi->cur_msg = message;
		dspi->cur_chip = spi_get_ctldata(spi);
		dspi->cs = spi->chip_select;
		dspi->cs_change = 0;
		if (list_is_last(&dspi->cur_transfer->transfer_list,
				 &dspi->cur_msg->transfers) || transfer->cs_change)
			dspi->cs_change = 1;
		dspi->void_write_data = dspi->cur_chip->void_write_data;

		dspi->dataflags = 0;
		dspi->tx = (void *)transfer->tx_buf;
		dspi->tx_end = dspi->tx + transfer->len;
		dspi->rx = transfer->rx_buf;
		dspi->rx_end = dspi->rx + transfer->len;
		dspi->len = transfer->len;

		if (!dspi->rx)
			dspi->dataflags |= TRAN_STATE_RX_VOID;

		if (!dspi->tx)
			dspi->dataflags |= TRAN_STATE_TX_VOID;

		regmap_write(dspi->regmap, SPI_MCR, dspi->cur_chip->mcr_val);
		regmap_update_bits(dspi->regmap, SPI_MCR,
				SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF,
				SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF);
		regmap_write(dspi->regmap, SPI_CTAR(0),
				dspi->cur_chip->ctar_val);

		trans_mode = dspi->devtype_data->trans_mode;
		switch (trans_mode) {
		case DSPI_EOQ_MODE:
			regmap_write(dspi->regmap, SPI_RSER, SPI_RSER_EOQFE);
			dspi_eoq_write(dspi);
			break;
		case DSPI_TCFQ_MODE:
			regmap_write(dspi->regmap, SPI_RSER, SPI_RSER_TCFQE);
			dspi_tcfq_write(dspi);
			break;
		case DSPI_DMA_MODE:
			regmap_write(dspi->regmap, SPI_RSER,
				SPI_RSER_TFFFE | SPI_RSER_TFFFD |
				SPI_RSER_RFDFE | SPI_RSER_RFDFD);
			status = dspi_dma_xfer(dspi);
			break;
		default:
			dev_err(&dspi->pdev->dev, "unsupported trans_mode %u\n",
				trans_mode);
			status = -EINVAL;
			goto out;
		}

		if (trans_mode != DSPI_DMA_MODE) {
			if (wait_event_interruptible(dspi->waitq,
						dspi->waitflags))
				dev_err(&dspi->pdev->dev,
					"wait transfer complete fail!\n");
			dspi->waitflags = 0;
		}

		if (transfer->delay_usecs)
			udelay(transfer->delay_usecs);
	}

out:
	message->status = status;
	spi_finalize_current_message(master);

	return status;
}

static int dspi_setup(struct spi_device *spi)
{
	struct chip_data *chip;
	struct fsl_dspi *dspi = spi_master_get_devdata(spi->master);
	u32 cs_sck_delay = 0, sck_cs_delay = 0;
	unsigned char br = 0, pbr = 0, pcssck = 0, cssck = 0;
	unsigned char pasc = 0, asc = 0, fmsz = 0;
	unsigned long clkrate;

	if ((spi->bits_per_word >= 4) && (spi->bits_per_word <= 16)) {
		fmsz = spi->bits_per_word - 1;
	} else {
		pr_err("Invalid wordsize\n");
		return -ENODEV;
	}

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (chip == NULL) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;
	}

	of_property_read_u32(spi->dev.of_node, "fsl,spi-cs-sck-delay",
			&cs_sck_delay);

	of_property_read_u32(spi->dev.of_node, "fsl,spi-sck-cs-delay",
			&sck_cs_delay);

	chip->mcr_val = SPI_MCR_MASTER | SPI_MCR_PCSIS |
		SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF;

	chip->void_write_data = 0;

	clkrate = clk_get_rate(dspi->clk);
	hz_to_spi_baud(&pbr, &br, spi->max_speed_hz, clkrate);

	/* Set PCS to SCK delay scale values */
	ns_delay_scale(&pcssck, &cssck, cs_sck_delay, clkrate);

	/* Set After SCK delay scale values */
	ns_delay_scale(&pasc, &asc, sck_cs_delay, clkrate);

	chip->ctar_val =  SPI_CTAR_FMSZ(fmsz)
		| SPI_CTAR_CPOL(spi->mode & SPI_CPOL ? 1 : 0)
		| SPI_CTAR_CPHA(spi->mode & SPI_CPHA ? 1 : 0)
		| SPI_CTAR_LSBFE(spi->mode & SPI_LSB_FIRST ? 1 : 0)
		| SPI_CTAR_PCSSCK(pcssck)
		| SPI_CTAR_CSSCK(cssck)
		| SPI_CTAR_PASC(pasc)
		| SPI_CTAR_ASC(asc)
		| SPI_CTAR_PBR(pbr)
		| SPI_CTAR_BR(br);

	spi_set_ctldata(spi, chip);

	return 0;
}

static void dspi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata((struct spi_device *)spi);

	dev_dbg(&spi->dev, "spi_device %u.%u cleanup\n",
			spi->master->bus_num, spi->chip_select);

	kfree(chip);
}

static irqreturn_t dspi_interrupt(int irq, void *dev_id)
{
	struct fsl_dspi *dspi = (struct fsl_dspi *)dev_id;
	struct spi_message *msg = dspi->cur_msg;
	enum dspi_trans_mode trans_mode;
	u32 spi_sr, spi_tcr;
	u32 spi_tcnt, tcnt_diff;
	int tx_word;

	regmap_read(dspi->regmap, SPI_SR, &spi_sr);
	regmap_write(dspi->regmap, SPI_SR, spi_sr);


	if (spi_sr & (SPI_SR_EOQF | SPI_SR_TCFQF)) {
		tx_word = is_double_byte_mode(dspi);

		regmap_read(dspi->regmap, SPI_TCR, &spi_tcr);
		spi_tcnt = SPI_TCR_GET_TCNT(spi_tcr);
		/*
		 * The width of SPI Transfer Counter in SPI_TCR is 16bits,
		 * so the max couner is 65535. When the counter reach 65535,
		 * it will wrap around, counter reset to zero.
		 * spi_tcnt my be less than dspi->spi_tcnt, it means the
		 * counter already wrapped around.
		 * SPI Transfer Counter is a counter of transmitted frames.
		 * The size of frame maybe two bytes.
		 */
		tcnt_diff = ((spi_tcnt + SPI_TCR_TCNT_MAX) - dspi->spi_tcnt)
			% SPI_TCR_TCNT_MAX;
		tcnt_diff *= (tx_word + 1);
		if (dspi->dataflags & TRAN_STATE_WORD_ODD_NUM)
			tcnt_diff--;

		msg->actual_length += tcnt_diff;

		dspi->spi_tcnt = spi_tcnt;

		trans_mode = dspi->devtype_data->trans_mode;
		switch (trans_mode) {
		case DSPI_EOQ_MODE:
			dspi_eoq_read(dspi);
			break;
		case DSPI_TCFQ_MODE:
			dspi_tcfq_read(dspi);
			break;
		default:
			dev_err(&dspi->pdev->dev, "unsupported trans_mode %u\n",
				trans_mode);
				return IRQ_HANDLED;
		}

		if (!dspi->len) {
			if (dspi->dataflags & TRAN_STATE_WORD_ODD_NUM) {
				regmap_update_bits(dspi->regmap,
						   SPI_CTAR(0),
						   SPI_FRAME_BITS_MASK,
						   SPI_FRAME_BITS(16));
				dspi->dataflags &= ~TRAN_STATE_WORD_ODD_NUM;
			}

			dspi->waitflags = 1;
			wake_up_interruptible(&dspi->waitq);
		} else {
			switch (trans_mode) {
			case DSPI_EOQ_MODE:
				dspi_eoq_write(dspi);
				break;
			case DSPI_TCFQ_MODE:
				dspi_tcfq_write(dspi);
				break;
			default:
				dev_err(&dspi->pdev->dev,
					"unsupported trans_mode %u\n",
					trans_mode);
			}
		}
	}

	return IRQ_HANDLED;
}

static const struct of_device_id fsl_dspi_dt_ids[] = {
	{ .compatible = "fsl,vf610-dspi", .data = (void *)&vf610_data, },
	{ .compatible = "fsl,ls1021a-v1.0-dspi",
		.data = (void *)&ls1021a_v1_data, },
	{ .compatible = "fsl,ls2085a-dspi", .data = (void *)&ls2085a_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_dspi_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int dspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct fsl_dspi *dspi = spi_master_get_devdata(master);

	spi_master_suspend(master);
	clk_disable_unprepare(dspi->clk);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int dspi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct fsl_dspi *dspi = spi_master_get_devdata(master);
	int ret;

	pinctrl_pm_select_default_state(dev);

	ret = clk_prepare_enable(dspi->clk);
	if (ret)
		return ret;
	spi_master_resume(master);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(dspi_pm, dspi_suspend, dspi_resume);

static const struct regmap_config dspi_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x88,
};

static void dspi_init(struct fsl_dspi *dspi)
{
	regmap_write(dspi->regmap, SPI_SR, SPI_SR_CLEAR);
}

static int dspi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct fsl_dspi *dspi;
	struct resource *res;
	void __iomem *base;
	int ret = 0, cs_num, bus_num;

	master = spi_alloc_master(&pdev->dev, sizeof(struct fsl_dspi));
	if (!master)
		return -ENOMEM;

	dspi = spi_master_get_devdata(master);
	dspi->pdev = pdev;
	dspi->master = master;

	master->transfer = NULL;
	master->setup = dspi_setup;
	master->transfer_one_message = dspi_transfer_one_message;
	master->dev.of_node = pdev->dev.of_node;

	master->cleanup = dspi_cleanup;
	master->mode_bits = SPI_CPOL | SPI_CPHA;
	master->bits_per_word_mask = SPI_BPW_MASK(4) | SPI_BPW_MASK(8) |
					SPI_BPW_MASK(16);

	ret = of_property_read_u32(np, "spi-num-chipselects", &cs_num);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't get spi-num-chipselects\n");
		goto out_master_put;
	}
	master->num_chipselect = cs_num;

	ret = of_property_read_u32(np, "bus-num", &bus_num);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't get bus-num\n");
		goto out_master_put;
	}
	master->bus_num = bus_num;

	dspi->devtype_data = of_device_get_match_data(&pdev->dev);
	if (!dspi->devtype_data) {
		dev_err(&pdev->dev, "can't get devtype_data\n");
		ret = -EFAULT;
		goto out_master_put;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out_master_put;
	}

	dspi->regmap = devm_regmap_init_mmio_clk(&pdev->dev, NULL, base,
						&dspi_regmap_config);
	if (IS_ERR(dspi->regmap)) {
		dev_err(&pdev->dev, "failed to init regmap: %ld\n",
				PTR_ERR(dspi->regmap));
		ret = PTR_ERR(dspi->regmap);
		goto out_master_put;
	}

	dspi->clk = devm_clk_get(&pdev->dev, "dspi");
	if (IS_ERR(dspi->clk)) {
		ret = PTR_ERR(dspi->clk);
		dev_err(&pdev->dev, "unable to get clock\n");
		goto out_master_put;
	}
	ret = clk_prepare_enable(dspi->clk);
	if (ret)
		goto out_master_put;

	dspi_init(dspi);
	dspi->irq = platform_get_irq(pdev, 0);
	if (dspi->irq < 0) {
		dev_err(&pdev->dev, "can't get platform irq\n");
		ret = dspi->irq;
		goto out_clk_put;
	}

	ret = devm_request_irq(&pdev->dev, dspi->irq, dspi_interrupt, 0,
			pdev->name, dspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to attach DSPI interrupt\n");
		goto out_clk_put;
	}

	if (dspi->devtype_data->trans_mode == DSPI_DMA_MODE) {
		ret = dspi_request_dma(dspi, res->start);
		if (ret < 0) {
			dev_err(&pdev->dev, "can't get dma channels\n");
			goto out_clk_put;
		}
	}

	master->max_speed_hz =
		clk_get_rate(dspi->clk) / dspi->devtype_data->max_clock_factor;

	init_waitqueue_head(&dspi->waitq);
	platform_set_drvdata(pdev, master);

	ret = spi_register_master(master);
	if (ret != 0) {
		dev_err(&pdev->dev, "Problem registering DSPI master\n");
		goto out_clk_put;
	}

	return ret;

out_clk_put:
	clk_disable_unprepare(dspi->clk);
out_master_put:
	spi_master_put(master);

	return ret;
}

static int dspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct fsl_dspi *dspi = spi_master_get_devdata(master);

	/* Disconnect from the SPI framework */
	dspi_release_dma(dspi);
	clk_disable_unprepare(dspi->clk);
	spi_unregister_master(dspi->master);

	return 0;
}

static struct platform_driver fsl_dspi_driver = {
	.driver.name    = DRIVER_NAME,
	.driver.of_match_table = fsl_dspi_dt_ids,
	.driver.owner   = THIS_MODULE,
	.driver.pm = &dspi_pm,
	.probe          = dspi_probe,
	.remove		= dspi_remove,
};
module_platform_driver(fsl_dspi_driver);

MODULE_DESCRIPTION("Freescale DSPI Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
