// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2013 Freescale Semiconductor, Inc.
// Copyright 2020 NXP
//
// Freescale DSPI driver
// This file contains a driver for the Freescale DSPI

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
#include <linux/spi/spi-fsl-dspi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/time.h>

#define DRIVER_NAME "fsl-dspi"

#ifdef CONFIG_M5441x
#define DSPI_FIFO_SIZE			16
#else
#define DSPI_FIFO_SIZE			4
#endif
#define DSPI_DMA_BUFSIZE		(DSPI_FIFO_SIZE * 1024)

#define SPI_MCR		0x00
#define SPI_MCR_MASTER		(1 << 31)
#define SPI_MCR_PCSIS		(0x3F << 16)
#define SPI_MCR_CLR_TXF	(1 << 11)
#define SPI_MCR_CLR_RXF	(1 << 10)
#define SPI_MCR_XSPI		(1 << 3)
#define SPI_MCR_DIS_TXF		(1 << 13)
#define SPI_MCR_DIS_RXF		(1 << 12)
#define SPI_MCR_HALT		(1 << 0)

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
#define SPI_SR_CLEAR		0x9aaf0000

#define SPI_RSER_TFFFE		BIT(25)
#define SPI_RSER_TFFFD		BIT(24)
#define SPI_RSER_RFDFE		BIT(17)
#define SPI_RSER_RFDFD		BIT(16)

#define SPI_RSER		0x30
#define SPI_RSER_EOQFE		0x10000000
#define SPI_RSER_TCFQE		0x80000000

#define SPI_PUSHR		0x34
#define SPI_PUSHR_CMD_CONT	(1 << 15)
#define SPI_PUSHR_CONT		(SPI_PUSHR_CMD_CONT << 16)
#define SPI_PUSHR_CMD_CTAS(x)	(((x) & 0x0003) << 12)
#define SPI_PUSHR_CTAS(x)	(SPI_PUSHR_CMD_CTAS(x) << 16)
#define SPI_PUSHR_CMD_EOQ	(1 << 11)
#define SPI_PUSHR_EOQ		(SPI_PUSHR_CMD_EOQ << 16)
#define SPI_PUSHR_CMD_CTCNT	(1 << 10)
#define SPI_PUSHR_CTCNT		(SPI_PUSHR_CMD_CTCNT << 16)
#define SPI_PUSHR_CMD_PCS(x)	((1 << x) & 0x003f)
#define SPI_PUSHR_PCS(x)	(SPI_PUSHR_CMD_PCS(x) << 16)
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

#define SPI_CTARE(x)		(0x11c + (((x) & 0x3) * 4))
#define SPI_CTARE_FMSZE(x)	(((x) & 0x1) << 16)
#define SPI_CTARE_DTCP(x)	((x) & 0x7ff)

#define SPI_SREX		0x13c

#define SPI_FRAME_BITS(bits)	SPI_CTAR_FMSZ((bits) - 1)
#define SPI_FRAME_BITS_MASK	SPI_CTAR_FMSZ(0xf)
#define SPI_FRAME_BITS_16	SPI_CTAR_FMSZ(0xf)
#define SPI_FRAME_BITS_8	SPI_CTAR_FMSZ(0x7)

#define SPI_FRAME_EBITS(bits)	SPI_CTARE_FMSZE(((bits) - 1) >> 4)
#define SPI_FRAME_EBITS_MASK	SPI_CTARE_FMSZE(1)

/* Register offsets for regmap_pushr */
#define PUSHR_CMD		0x0
#define PUSHR_TX		0x2

#define SPI_CS_INIT		0x01
#define SPI_CS_ASSERT		0x02
#define SPI_CS_DROP		0x04

#define DMA_COMPLETION_TIMEOUT	msecs_to_jiffies(3000)

struct chip_data {
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
	bool xspi_mode;
};

static const struct fsl_dspi_devtype_data vf610_data = {
	.trans_mode = DSPI_DMA_MODE,
	.max_clock_factor = 2,
};

static const struct fsl_dspi_devtype_data ls1021a_v1_data = {
	.trans_mode = DSPI_TCFQ_MODE,
	.max_clock_factor = 8,
	.xspi_mode = true,
};

static const struct fsl_dspi_devtype_data ls2085a_data = {
	.trans_mode = DSPI_TCFQ_MODE,
	.max_clock_factor = 8,
};

static const struct fsl_dspi_devtype_data coldfire_data = {
	.trans_mode = DSPI_EOQ_MODE,
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
	struct regmap		*regmap_pushr;
	int			irq;
	struct clk		*clk;

	struct spi_transfer	*cur_transfer;
	struct spi_message	*cur_msg;
	struct chip_data	*cur_chip;
	size_t			len;
	const void		*tx;
	void			*rx;
	void			*rx_end;
	u16			void_write_data;
	u16			tx_cmd;
	u8			bits_per_word;
	u8			bytes_per_word;
	const struct fsl_dspi_devtype_data *devtype_data;

	wait_queue_head_t	waitq;
	u32			waitflags;

	struct fsl_dspi_dma	*dma;
};

static u32 dspi_pop_tx(struct fsl_dspi *dspi)
{
	u32 txdata = 0;

	if (dspi->tx) {
		if (dspi->bytes_per_word == 1)
			txdata = *(u8 *)dspi->tx;
		else if (dspi->bytes_per_word == 2)
			txdata = *(u16 *)dspi->tx;
		else  /* dspi->bytes_per_word == 4 */
			txdata = *(u32 *)dspi->tx;
		dspi->tx += dspi->bytes_per_word;
	}
	dspi->len -= dspi->bytes_per_word;
	return txdata;
}

static u32 dspi_pop_tx_pushr(struct fsl_dspi *dspi)
{
	u16 cmd = dspi->tx_cmd, data = dspi_pop_tx(dspi);

	if (dspi->len > 0)
		cmd |= SPI_PUSHR_CMD_CONT;
	return cmd << 16 | data;
}

static void dspi_push_rx(struct fsl_dspi *dspi, u32 rxdata)
{
	if (!dspi->rx)
		return;

	/* Mask of undefined bits */
	rxdata &= (1 << dspi->bits_per_word) - 1;

	if (dspi->bytes_per_word == 1)
		*(u8 *)dspi->rx = rxdata;
	else if (dspi->bytes_per_word == 2)
		*(u16 *)dspi->rx = rxdata;
	else /* dspi->bytes_per_word == 4 */
		*(u32 *)dspi->rx = rxdata;
	dspi->rx += dspi->bytes_per_word;
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
	int i;

	if (dspi->rx) {
		for (i = 0; i < dma->curr_xfer_len; i++)
			dspi_push_rx(dspi, dspi->dma->rx_dma_buf[i]);
	}

	complete(&dma->cmd_rx_complete);
}

static int dspi_next_xfer_dma_submit(struct fsl_dspi *dspi)
{
	struct fsl_dspi_dma *dma = dspi->dma;
	struct device *dev = &dspi->pdev->dev;
	int time_left;
	int i;

	for (i = 0; i < dma->curr_xfer_len; i++)
		dspi->dma->tx_dma_buf[i] = dspi_pop_tx_pushr(dspi);

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
	struct spi_message *message = dspi->cur_msg;
	int curr_remaining_bytes;
	int bytes_per_buffer;
	int ret = 0;

	curr_remaining_bytes = dspi->len;
	bytes_per_buffer = DSPI_DMA_BUFSIZE / DSPI_FIFO_SIZE;
	while (curr_remaining_bytes) {
		/* Check if current transfer fits the DMA buffer */
		dma->curr_xfer_len = curr_remaining_bytes
			/ dspi->bytes_per_word;
		if (dma->curr_xfer_len > bytes_per_buffer)
			dma->curr_xfer_len = bytes_per_buffer;

		ret = dspi_next_xfer_dma_submit(dspi);
		if (ret) {
			dev_err(dev, "DMA transfer failed\n");
			goto exit;

		} else {
			const int len =
				dma->curr_xfer_len * dspi->bytes_per_word;
			curr_remaining_bytes -= len;
			message->actual_length += len;
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

static void fifo_write(struct fsl_dspi *dspi)
{
	regmap_write(dspi->regmap, SPI_PUSHR, dspi_pop_tx_pushr(dspi));
}

static void cmd_fifo_write(struct fsl_dspi *dspi)
{
	u16 cmd = dspi->tx_cmd;

	if (dspi->len > 0)
		cmd |= SPI_PUSHR_CMD_CONT;
	regmap_write(dspi->regmap_pushr, PUSHR_CMD, cmd);
}

static void tx_fifo_write(struct fsl_dspi *dspi, u16 txdata)
{
	regmap_write(dspi->regmap_pushr, PUSHR_TX, txdata);
}

static void dspi_tcfq_write(struct fsl_dspi *dspi)
{
	/* Clear transfer count */
	dspi->tx_cmd |= SPI_PUSHR_CMD_CTCNT;

	if (dspi->devtype_data->xspi_mode && dspi->bits_per_word > 16) {
		/* Write two TX FIFO entries first, and then the corresponding
		 * CMD FIFO entry.
		 */
		u32 data = dspi_pop_tx(dspi);

		if (dspi->cur_chip->ctar_val & SPI_CTAR_LSBFE(1)) {
			/* LSB */
			tx_fifo_write(dspi, data & 0xFFFF);
			tx_fifo_write(dspi, data >> 16);
		} else {
			/* MSB */
			tx_fifo_write(dspi, data >> 16);
			tx_fifo_write(dspi, data & 0xFFFF);
		}
		cmd_fifo_write(dspi);
	} else {
		/* Write one entry to both TX FIFO and CMD FIFO
		 * simultaneously.
		 */
		fifo_write(dspi);
	}
}

static u32 fifo_read(struct fsl_dspi *dspi)
{
	u32 rxdata = 0;

	regmap_read(dspi->regmap, SPI_POPR, &rxdata);
	return rxdata;
}

static void dspi_tcfq_read(struct fsl_dspi *dspi)
{
	dspi_push_rx(dspi, fifo_read(dspi));
}

static void dspi_eoq_write(struct fsl_dspi *dspi)
{
	int fifo_size = DSPI_FIFO_SIZE;
	u16 xfer_cmd = dspi->tx_cmd;

	/* Fill TX FIFO with as many transfers as possible */
	while (dspi->len && fifo_size--) {
		dspi->tx_cmd = xfer_cmd;
		/* Request EOQF for last transfer in FIFO */
		if (dspi->len == dspi->bytes_per_word || fifo_size == 0)
			dspi->tx_cmd |= SPI_PUSHR_CMD_EOQ;
		/* Clear transfer count for first transfer in FIFO */
		if (fifo_size == (DSPI_FIFO_SIZE - 1))
			dspi->tx_cmd |= SPI_PUSHR_CMD_CTCNT;
		/* Write combined TX FIFO and CMD FIFO entry */
		fifo_write(dspi);
	}
}

static void dspi_eoq_read(struct fsl_dspi *dspi)
{
	int fifo_size = DSPI_FIFO_SIZE;

	/* Read one FIFO entry at and push to rx buffer */
	while ((dspi->rx < dspi->rx_end) && fifo_size--)
		dspi_push_rx(dspi, fifo_read(dspi));
}

static int dspi_transfer_one_message(struct spi_master *master,
		struct spi_message *message)
{
	struct fsl_dspi *dspi = spi_master_get_devdata(master);
	struct spi_device *spi = message->spi;
	struct spi_transfer *transfer;
	int status = 0;
	enum dspi_trans_mode trans_mode;

	message->actual_length = 0;

	list_for_each_entry(transfer, &message->transfers, transfer_list) {
		dspi->cur_transfer = transfer;
		dspi->cur_msg = message;
		dspi->cur_chip = spi_get_ctldata(spi);
		/* Prepare command word for CMD FIFO */
		dspi->tx_cmd = SPI_PUSHR_CMD_CTAS(0) |
			SPI_PUSHR_CMD_PCS(spi->chip_select);
		if (list_is_last(&dspi->cur_transfer->transfer_list,
				 &dspi->cur_msg->transfers)) {
			/* Leave PCS activated after last transfer when
			 * cs_change is set.
			 */
			if (transfer->cs_change)
				dspi->tx_cmd |= SPI_PUSHR_CMD_CONT;
		} else {
			/* Keep PCS active between transfers in same message
			 * when cs_change is not set, and de-activate PCS
			 * between transfers in the same message when
			 * cs_change is set.
			 */
			if (!transfer->cs_change)
				dspi->tx_cmd |= SPI_PUSHR_CMD_CONT;
		}

		dspi->void_write_data = dspi->cur_chip->void_write_data;

		dspi->tx = transfer->tx_buf;
		dspi->rx = transfer->rx_buf;
		dspi->rx_end = dspi->rx + transfer->len;
		dspi->len = transfer->len;
		/* Validated transfer specific frame size (defaults applied) */
		dspi->bits_per_word = transfer->bits_per_word;
		if (transfer->bits_per_word <= 8)
			dspi->bytes_per_word = 1;
		else if (transfer->bits_per_word <= 16)
			dspi->bytes_per_word = 2;
		else
			dspi->bytes_per_word = 4;

		regmap_update_bits(dspi->regmap, SPI_MCR,
				   SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF,
				   SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF);
		regmap_write(dspi->regmap, SPI_CTAR(0),
			     dspi->cur_chip->ctar_val |
			     SPI_FRAME_BITS(transfer->bits_per_word));
		if (dspi->devtype_data->xspi_mode)
			regmap_write(dspi->regmap, SPI_CTARE(0),
				     SPI_FRAME_EBITS(transfer->bits_per_word)
				     | SPI_CTARE_DTCP(1));

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
	struct fsl_dspi_platform_data *pdata;
	u32 cs_sck_delay = 0, sck_cs_delay = 0;
	unsigned char br = 0, pbr = 0, pcssck = 0, cssck = 0;
	unsigned char pasc = 0, asc = 0;
	unsigned long clkrate;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (chip == NULL) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;
	}

	pdata = dev_get_platdata(&dspi->pdev->dev);

	if (!pdata) {
		of_property_read_u32(spi->dev.of_node, "fsl,spi-cs-sck-delay",
				&cs_sck_delay);

		of_property_read_u32(spi->dev.of_node, "fsl,spi-sck-cs-delay",
				&sck_cs_delay);
	} else {
		cs_sck_delay = pdata->cs_sck_delay;
		sck_cs_delay = pdata->sck_cs_delay;
	}

	chip->void_write_data = 0;

	clkrate = clk_get_rate(dspi->clk);
	hz_to_spi_baud(&pbr, &br, spi->max_speed_hz, clkrate);

	/* Set PCS to SCK delay scale values */
	ns_delay_scale(&pcssck, &cssck, cs_sck_delay, clkrate);

	/* Set After SCK delay scale values */
	ns_delay_scale(&pasc, &asc, sck_cs_delay, clkrate);

	chip->ctar_val = SPI_CTAR_CPOL(spi->mode & SPI_CPOL ? 1 : 0)
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
	u16 spi_tcnt;

	regmap_read(dspi->regmap, SPI_SR, &spi_sr);
	regmap_write(dspi->regmap, SPI_SR, spi_sr);


	if (spi_sr & (SPI_SR_EOQF | SPI_SR_TCFQF)) {
		/* Get transfer counter (in number of SPI transfers). It was
		 * reset to 0 when transfer(s) were started.
		 */
		regmap_read(dspi->regmap, SPI_TCR, &spi_tcr);
		spi_tcnt = SPI_TCR_GET_TCNT(spi_tcr);
		/* Update total number of bytes that were transferred */
		msg->actual_length += spi_tcnt * dspi->bytes_per_word;

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

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static const struct of_device_id fsl_dspi_dt_ids[] = {
	{ .compatible = "fsl,vf610-dspi", .data = &vf610_data, },
	{ .compatible = "fsl,ls1021a-v1.0-dspi", .data = &ls1021a_v1_data, },
	{ .compatible = "fsl,ls2085a-dspi", .data = &ls2085a_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_dspi_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int dspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct fsl_dspi *dspi = spi_master_get_devdata(master);

	if (dspi->irq)
		disable_irq(dspi->irq);
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
	if (dspi->irq)
		enable_irq(dspi->irq);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(dspi_pm, dspi_suspend, dspi_resume);

static const struct regmap_range dspi_volatile_ranges[] = {
	regmap_reg_range(SPI_MCR, SPI_TCR),
	regmap_reg_range(SPI_SR, SPI_SR),
	regmap_reg_range(SPI_PUSHR, SPI_RXFR3),
};

static const struct regmap_access_table dspi_volatile_table = {
	.yes_ranges     = dspi_volatile_ranges,
	.n_yes_ranges   = ARRAY_SIZE(dspi_volatile_ranges),
};

static const struct regmap_config dspi_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x88,
	.volatile_table = &dspi_volatile_table,
};

static const struct regmap_range dspi_xspi_volatile_ranges[] = {
	regmap_reg_range(SPI_MCR, SPI_TCR),
	regmap_reg_range(SPI_SR, SPI_SR),
	regmap_reg_range(SPI_PUSHR, SPI_RXFR3),
	regmap_reg_range(SPI_SREX, SPI_SREX),
};

static const struct regmap_access_table dspi_xspi_volatile_table = {
	.yes_ranges     = dspi_xspi_volatile_ranges,
	.n_yes_ranges   = ARRAY_SIZE(dspi_xspi_volatile_ranges),
};

static const struct regmap_config dspi_xspi_regmap_config[] = {
	{
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.max_register = 0x13c,
		.volatile_table = &dspi_xspi_volatile_table,
	},
	{
		.name = "pushr",
		.reg_bits = 16,
		.val_bits = 16,
		.reg_stride = 2,
		.max_register = 0x2,
	},
};

static void dspi_init(struct fsl_dspi *dspi)
{
	regmap_write(dspi->regmap, SPI_MCR, SPI_MCR_MASTER | SPI_MCR_PCSIS |
		     (dspi->devtype_data->xspi_mode ? SPI_MCR_XSPI : 0));
	regmap_write(dspi->regmap, SPI_SR, SPI_SR_CLEAR);
	if (dspi->devtype_data->xspi_mode)
		regmap_write(dspi->regmap, SPI_CTARE(0),
			     SPI_CTARE_FMSZE(0) | SPI_CTARE_DTCP(1));
}

static int dspi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct spi_master *master;
	struct fsl_dspi *dspi;
	struct resource *res;
	const struct regmap_config *regmap_config;
	void __iomem *base;
	struct fsl_dspi_platform_data *pdata;
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
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		master->num_chipselect = pdata->cs_num;
		master->bus_num = pdata->bus_num;

		dspi->devtype_data = &coldfire_data;
	} else {

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
	}

	if (dspi->devtype_data->xspi_mode)
		master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	else
		master->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out_master_put;
	}

	if (dspi->devtype_data->xspi_mode)
		regmap_config = &dspi_xspi_regmap_config[0];
	else
		regmap_config = &dspi_regmap_config;
	dspi->regmap = devm_regmap_init_mmio(&pdev->dev, base, regmap_config);
	if (IS_ERR(dspi->regmap)) {
		dev_err(&pdev->dev, "failed to init regmap: %ld\n",
				PTR_ERR(dspi->regmap));
		ret = PTR_ERR(dspi->regmap);
		goto out_master_put;
	}

	if (dspi->devtype_data->xspi_mode) {
		dspi->regmap_pushr = devm_regmap_init_mmio(
			&pdev->dev, base + SPI_PUSHR,
			&dspi_xspi_regmap_config[1]);
		if (IS_ERR(dspi->regmap_pushr)) {
			dev_err(&pdev->dev,
				"failed to init pushr regmap: %ld\n",
				PTR_ERR(dspi->regmap_pushr));
			ret = PTR_ERR(dspi->regmap_pushr);
			goto out_master_put;
		}
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

	ret = request_threaded_irq(dspi->irq, dspi_interrupt, NULL,
				   IRQF_SHARED, pdev->name, dspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to attach DSPI interrupt\n");
		goto out_clk_put;
	}

	if (dspi->devtype_data->trans_mode == DSPI_DMA_MODE) {
		ret = dspi_request_dma(dspi, res->start);
		if (ret < 0) {
			dev_err(&pdev->dev, "can't get dma channels\n");
			goto out_free_irq;
		}
	}

	master->max_speed_hz =
		clk_get_rate(dspi->clk) / dspi->devtype_data->max_clock_factor;

	init_waitqueue_head(&dspi->waitq);
	platform_set_drvdata(pdev, master);

	ret = spi_register_master(master);
	if (ret != 0) {
		dev_err(&pdev->dev, "Problem registering DSPI master\n");
		goto out_free_irq;
	}

	return ret;

out_free_irq:
	if (dspi->irq)
		free_irq(dspi->irq, dspi);
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
	spi_unregister_controller(dspi->master);

	/* Disable RX and TX */
	regmap_update_bits(dspi->regmap, SPI_MCR,
			   SPI_MCR_DIS_TXF | SPI_MCR_DIS_RXF,
			   SPI_MCR_DIS_TXF | SPI_MCR_DIS_RXF);

	/* Stop Running */
	regmap_update_bits(dspi->regmap, SPI_MCR, SPI_MCR_HALT, SPI_MCR_HALT);

	dspi_release_dma(dspi);
	if (dspi->irq)
		free_irq(dspi->irq, dspi);
	clk_disable_unprepare(dspi->clk);

	return 0;
}

static void dspi_shutdown(struct platform_device *pdev)
{
	dspi_remove(pdev);
}

static struct platform_driver fsl_dspi_driver = {
	.driver.name    = DRIVER_NAME,
	.driver.of_match_table = fsl_dspi_dt_ids,
	.driver.owner   = THIS_MODULE,
	.driver.pm = &dspi_pm,
	.probe          = dspi_probe,
	.remove		= dspi_remove,
	.shutdown	= dspi_shutdown,
};
module_platform_driver(fsl_dspi_driver);

MODULE_DESCRIPTION("Freescale DSPI Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
