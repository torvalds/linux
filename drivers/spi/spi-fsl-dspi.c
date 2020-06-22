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
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-fsl-dspi.h>

#define DRIVER_NAME			"fsl-dspi"

#define SPI_MCR				0x00
#define SPI_MCR_MASTER			BIT(31)
#define SPI_MCR_PCSIS(x)		((x) << 16)
#define SPI_MCR_CLR_TXF			BIT(11)
#define SPI_MCR_CLR_RXF			BIT(10)
#define SPI_MCR_XSPI			BIT(3)
#define SPI_MCR_DIS_TXF			BIT(13)
#define SPI_MCR_DIS_RXF			BIT(12)
#define SPI_MCR_HALT			BIT(0)

#define SPI_TCR				0x08
#define SPI_TCR_GET_TCNT(x)		(((x) & GENMASK(31, 16)) >> 16)

#define SPI_CTAR(x)			(0x0c + (((x) & GENMASK(1, 0)) * 4))
#define SPI_CTAR_FMSZ(x)		(((x) << 27) & GENMASK(30, 27))
#define SPI_CTAR_CPOL			BIT(26)
#define SPI_CTAR_CPHA			BIT(25)
#define SPI_CTAR_LSBFE			BIT(24)
#define SPI_CTAR_PCSSCK(x)		(((x) << 22) & GENMASK(23, 22))
#define SPI_CTAR_PASC(x)		(((x) << 20) & GENMASK(21, 20))
#define SPI_CTAR_PDT(x)			(((x) << 18) & GENMASK(19, 18))
#define SPI_CTAR_PBR(x)			(((x) << 16) & GENMASK(17, 16))
#define SPI_CTAR_CSSCK(x)		(((x) << 12) & GENMASK(15, 12))
#define SPI_CTAR_ASC(x)			(((x) << 8) & GENMASK(11, 8))
#define SPI_CTAR_DT(x)			(((x) << 4) & GENMASK(7, 4))
#define SPI_CTAR_BR(x)			((x) & GENMASK(3, 0))
#define SPI_CTAR_SCALE_BITS		0xf

#define SPI_CTAR0_SLAVE			0x0c

#define SPI_SR				0x2c
#define SPI_SR_TCFQF			BIT(31)
#define SPI_SR_EOQF			BIT(28)
#define SPI_SR_TFUF			BIT(27)
#define SPI_SR_TFFF			BIT(25)
#define SPI_SR_CMDTCF			BIT(23)
#define SPI_SR_SPEF			BIT(21)
#define SPI_SR_RFOF			BIT(19)
#define SPI_SR_TFIWF			BIT(18)
#define SPI_SR_RFDF			BIT(17)
#define SPI_SR_CMDFFF			BIT(16)
#define SPI_SR_CLEAR			(SPI_SR_TCFQF | SPI_SR_EOQF | \
					SPI_SR_TFUF | SPI_SR_TFFF | \
					SPI_SR_CMDTCF | SPI_SR_SPEF | \
					SPI_SR_RFOF | SPI_SR_TFIWF | \
					SPI_SR_RFDF | SPI_SR_CMDFFF)

#define SPI_RSER_TFFFE			BIT(25)
#define SPI_RSER_TFFFD			BIT(24)
#define SPI_RSER_RFDFE			BIT(17)
#define SPI_RSER_RFDFD			BIT(16)

#define SPI_RSER			0x30
#define SPI_RSER_TCFQE			BIT(31)
#define SPI_RSER_EOQFE			BIT(28)
#define SPI_RSER_CMDTCFE		BIT(23)

#define SPI_PUSHR			0x34
#define SPI_PUSHR_CMD_CONT		BIT(15)
#define SPI_PUSHR_CMD_CTAS(x)		(((x) << 12 & GENMASK(14, 12)))
#define SPI_PUSHR_CMD_EOQ		BIT(11)
#define SPI_PUSHR_CMD_CTCNT		BIT(10)
#define SPI_PUSHR_CMD_PCS(x)		(BIT(x) & GENMASK(5, 0))

#define SPI_PUSHR_SLAVE			0x34

#define SPI_POPR			0x38

#define SPI_TXFR0			0x3c
#define SPI_TXFR1			0x40
#define SPI_TXFR2			0x44
#define SPI_TXFR3			0x48
#define SPI_RXFR0			0x7c
#define SPI_RXFR1			0x80
#define SPI_RXFR2			0x84
#define SPI_RXFR3			0x88

#define SPI_CTARE(x)			(0x11c + (((x) & GENMASK(1, 0)) * 4))
#define SPI_CTARE_FMSZE(x)		(((x) & 0x1) << 16)
#define SPI_CTARE_DTCP(x)		((x) & 0x7ff)

#define SPI_SREX			0x13c

#define SPI_FRAME_BITS(bits)		SPI_CTAR_FMSZ((bits) - 1)
#define SPI_FRAME_EBITS(bits)		SPI_CTARE_FMSZE(((bits) - 1) >> 4)

#define DMA_COMPLETION_TIMEOUT		msecs_to_jiffies(3000)

struct chip_data {
	u32			ctar_val;
};

enum dspi_trans_mode {
	DSPI_EOQ_MODE = 0,
	DSPI_XSPI_MODE,
	DSPI_DMA_MODE,
};

struct fsl_dspi_devtype_data {
	enum dspi_trans_mode	trans_mode;
	u8			max_clock_factor;
	int			fifo_size;
};

enum {
	LS1021A,
	LS1012A,
	LS1028A,
	LS1043A,
	LS1046A,
	LS2080A,
	LS2085A,
	LX2160A,
	MCF5441X,
	VF610,
};

static const struct fsl_dspi_devtype_data devtype_data[] = {
	[VF610] = {
		.trans_mode		= DSPI_DMA_MODE,
		.max_clock_factor	= 2,
		.fifo_size		= 4,
	},
	[LS1021A] = {
		/* Has A-011218 DMA erratum */
		.trans_mode		= DSPI_XSPI_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 4,
	},
	[LS1012A] = {
		/* Has A-011218 DMA erratum */
		.trans_mode		= DSPI_XSPI_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 16,
	},
	[LS1028A] = {
		.trans_mode		= DSPI_XSPI_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 4,
	},
	[LS1043A] = {
		/* Has A-011218 DMA erratum */
		.trans_mode		= DSPI_XSPI_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 16,
	},
	[LS1046A] = {
		/* Has A-011218 DMA erratum */
		.trans_mode		= DSPI_XSPI_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 16,
	},
	[LS2080A] = {
		.trans_mode		= DSPI_DMA_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 4,
	},
	[LS2085A] = {
		.trans_mode		= DSPI_DMA_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 4,
	},
	[LX2160A] = {
		.trans_mode		= DSPI_DMA_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 4,
	},
	[MCF5441X] = {
		.trans_mode		= DSPI_EOQ_MODE,
		.max_clock_factor	= 8,
		.fifo_size		= 16,
	},
};

struct fsl_dspi_dma {
	u32					*tx_dma_buf;
	struct dma_chan				*chan_tx;
	dma_addr_t				tx_dma_phys;
	struct completion			cmd_tx_complete;
	struct dma_async_tx_descriptor		*tx_desc;

	u32					*rx_dma_buf;
	struct dma_chan				*chan_rx;
	dma_addr_t				rx_dma_phys;
	struct completion			cmd_rx_complete;
	struct dma_async_tx_descriptor		*rx_desc;
};

struct fsl_dspi {
	struct spi_controller			*ctlr;
	struct platform_device			*pdev;

	struct regmap				*regmap;
	struct regmap				*regmap_pushr;
	int					irq;
	struct clk				*clk;

	struct spi_transfer			*cur_transfer;
	struct spi_message			*cur_msg;
	struct chip_data			*cur_chip;
	size_t					progress;
	size_t					len;
	const void				*tx;
	void					*rx;
	u16					tx_cmd;
	const struct fsl_dspi_devtype_data	*devtype_data;

	struct completion			xfer_done;

	struct fsl_dspi_dma			*dma;

	int					oper_word_size;
	int					oper_bits_per_word;

	int					words_in_flight;

	/*
	 * Offsets for CMD and TXDATA within SPI_PUSHR when accessed
	 * individually (in XSPI mode)
	 */
	int					pushr_cmd;
	int					pushr_tx;

	void (*host_to_dev)(struct fsl_dspi *dspi, u32 *txdata);
	void (*dev_to_host)(struct fsl_dspi *dspi, u32 rxdata);
};

static void dspi_native_host_to_dev(struct fsl_dspi *dspi, u32 *txdata)
{
	switch (dspi->oper_word_size) {
	case 1:
		*txdata = *(u8 *)dspi->tx;
		break;
	case 2:
		*txdata = *(u16 *)dspi->tx;
		break;
	case 4:
		*txdata = *(u32 *)dspi->tx;
		break;
	}
	dspi->tx += dspi->oper_word_size;
}

static void dspi_native_dev_to_host(struct fsl_dspi *dspi, u32 rxdata)
{
	switch (dspi->oper_word_size) {
	case 1:
		*(u8 *)dspi->rx = rxdata;
		break;
	case 2:
		*(u16 *)dspi->rx = rxdata;
		break;
	case 4:
		*(u32 *)dspi->rx = rxdata;
		break;
	}
	dspi->rx += dspi->oper_word_size;
}

static void dspi_8on32_host_to_dev(struct fsl_dspi *dspi, u32 *txdata)
{
	*txdata = cpu_to_be32(*(u32 *)dspi->tx);
	dspi->tx += sizeof(u32);
}

static void dspi_8on32_dev_to_host(struct fsl_dspi *dspi, u32 rxdata)
{
	*(u32 *)dspi->rx = be32_to_cpu(rxdata);
	dspi->rx += sizeof(u32);
}

static void dspi_8on16_host_to_dev(struct fsl_dspi *dspi, u32 *txdata)
{
	*txdata = cpu_to_be16(*(u16 *)dspi->tx);
	dspi->tx += sizeof(u16);
}

static void dspi_8on16_dev_to_host(struct fsl_dspi *dspi, u32 rxdata)
{
	*(u16 *)dspi->rx = be16_to_cpu(rxdata);
	dspi->rx += sizeof(u16);
}

static void dspi_16on32_host_to_dev(struct fsl_dspi *dspi, u32 *txdata)
{
	u16 hi = *(u16 *)dspi->tx;
	u16 lo = *(u16 *)(dspi->tx + 2);

	*txdata = (u32)hi << 16 | lo;
	dspi->tx += sizeof(u32);
}

static void dspi_16on32_dev_to_host(struct fsl_dspi *dspi, u32 rxdata)
{
	u16 hi = rxdata & 0xffff;
	u16 lo = rxdata >> 16;

	*(u16 *)dspi->rx = lo;
	*(u16 *)(dspi->rx + 2) = hi;
	dspi->rx += sizeof(u32);
}

/*
 * Pop one word from the TX buffer for pushing into the
 * PUSHR register (TX FIFO)
 */
static u32 dspi_pop_tx(struct fsl_dspi *dspi)
{
	u32 txdata = 0;

	if (dspi->tx)
		dspi->host_to_dev(dspi, &txdata);
	dspi->len -= dspi->oper_word_size;
	return txdata;
}

/* Prepare one TX FIFO entry (txdata plus cmd) */
static u32 dspi_pop_tx_pushr(struct fsl_dspi *dspi)
{
	u16 cmd = dspi->tx_cmd, data = dspi_pop_tx(dspi);

	if (spi_controller_is_slave(dspi->ctlr))
		return data;

	if (dspi->len > 0)
		cmd |= SPI_PUSHR_CMD_CONT;
	return cmd << 16 | data;
}

/* Push one word to the RX buffer from the POPR register (RX FIFO) */
static void dspi_push_rx(struct fsl_dspi *dspi, u32 rxdata)
{
	if (!dspi->rx)
		return;
	dspi->dev_to_host(dspi, rxdata);
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
		for (i = 0; i < dspi->words_in_flight; i++)
			dspi_push_rx(dspi, dspi->dma->rx_dma_buf[i]);
	}

	complete(&dma->cmd_rx_complete);
}

static int dspi_next_xfer_dma_submit(struct fsl_dspi *dspi)
{
	struct device *dev = &dspi->pdev->dev;
	struct fsl_dspi_dma *dma = dspi->dma;
	int time_left;
	int i;

	for (i = 0; i < dspi->words_in_flight; i++)
		dspi->dma->tx_dma_buf[i] = dspi_pop_tx_pushr(dspi);

	dma->tx_desc = dmaengine_prep_slave_single(dma->chan_tx,
					dma->tx_dma_phys,
					dspi->words_in_flight *
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
					dspi->words_in_flight *
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

	if (spi_controller_is_slave(dspi->ctlr)) {
		wait_for_completion_interruptible(&dspi->dma->cmd_rx_complete);
		return 0;
	}

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

static void dspi_setup_accel(struct fsl_dspi *dspi);

static int dspi_dma_xfer(struct fsl_dspi *dspi)
{
	struct spi_message *message = dspi->cur_msg;
	struct device *dev = &dspi->pdev->dev;
	int ret = 0;

	/*
	 * dspi->len gets decremented by dspi_pop_tx_pushr in
	 * dspi_next_xfer_dma_submit
	 */
	while (dspi->len) {
		/* Figure out operational bits-per-word for this chunk */
		dspi_setup_accel(dspi);

		dspi->words_in_flight = dspi->len / dspi->oper_word_size;
		if (dspi->words_in_flight > dspi->devtype_data->fifo_size)
			dspi->words_in_flight = dspi->devtype_data->fifo_size;

		message->actual_length += dspi->words_in_flight *
					  dspi->oper_word_size;

		ret = dspi_next_xfer_dma_submit(dspi);
		if (ret) {
			dev_err(dev, "DMA transfer failed\n");
			break;
		}
	}

	return ret;
}

static int dspi_request_dma(struct fsl_dspi *dspi, phys_addr_t phy_addr)
{
	int dma_bufsize = dspi->devtype_data->fifo_size * 2;
	struct device *dev = &dspi->pdev->dev;
	struct dma_slave_config cfg;
	struct fsl_dspi_dma *dma;
	int ret;

	dma = devm_kzalloc(dev, sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->chan_rx = dma_request_chan(dev, "rx");
	if (IS_ERR(dma->chan_rx)) {
		dev_err(dev, "rx dma channel not available\n");
		ret = PTR_ERR(dma->chan_rx);
		return ret;
	}

	dma->chan_tx = dma_request_chan(dev, "tx");
	if (IS_ERR(dma->chan_tx)) {
		dev_err(dev, "tx dma channel not available\n");
		ret = PTR_ERR(dma->chan_tx);
		goto err_tx_channel;
	}

	dma->tx_dma_buf = dma_alloc_coherent(dma->chan_tx->device->dev,
					     dma_bufsize, &dma->tx_dma_phys,
					     GFP_KERNEL);
	if (!dma->tx_dma_buf) {
		ret = -ENOMEM;
		goto err_tx_dma_buf;
	}

	dma->rx_dma_buf = dma_alloc_coherent(dma->chan_rx->device->dev,
					     dma_bufsize, &dma->rx_dma_phys,
					     GFP_KERNEL);
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
	dma_free_coherent(dma->chan_rx->device->dev,
			  dma_bufsize, dma->rx_dma_buf, dma->rx_dma_phys);
err_rx_dma_buf:
	dma_free_coherent(dma->chan_tx->device->dev,
			  dma_bufsize, dma->tx_dma_buf, dma->tx_dma_phys);
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
	int dma_bufsize = dspi->devtype_data->fifo_size * 2;
	struct fsl_dspi_dma *dma = dspi->dma;

	if (!dma)
		return;

	if (dma->chan_tx) {
		dma_free_coherent(dma->chan_tx->device->dev, dma_bufsize,
				  dma->tx_dma_buf, dma->tx_dma_phys);
		dma_release_channel(dma->chan_tx);
	}

	if (dma->chan_rx) {
		dma_free_coherent(dma->chan_rx->device->dev, dma_bufsize,
				  dma->rx_dma_buf, dma->rx_dma_phys);
		dma_release_channel(dma->chan_rx);
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
	int scale_needed, scale, minscale = INT_MAX;
	int pscale_tbl[4] = {1, 3, 5, 7};
	u32 remainder;
	int i, j;

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

static void dspi_pushr_write(struct fsl_dspi *dspi)
{
	regmap_write(dspi->regmap, SPI_PUSHR, dspi_pop_tx_pushr(dspi));
}

static void dspi_pushr_cmd_write(struct fsl_dspi *dspi, u16 cmd)
{
	/*
	 * The only time when the PCS doesn't need continuation after this word
	 * is when it's last. We need to look ahead, because we actually call
	 * dspi_pop_tx (the function that decrements dspi->len) _after_
	 * dspi_pushr_cmd_write with XSPI mode. As for how much in advance? One
	 * word is enough. If there's more to transmit than that,
	 * dspi_xspi_write will know to split the FIFO writes in 2, and
	 * generate a new PUSHR command with the final word that will have PCS
	 * deasserted (not continued) here.
	 */
	if (dspi->len > dspi->oper_word_size)
		cmd |= SPI_PUSHR_CMD_CONT;
	regmap_write(dspi->regmap_pushr, dspi->pushr_cmd, cmd);
}

static void dspi_pushr_txdata_write(struct fsl_dspi *dspi, u16 txdata)
{
	regmap_write(dspi->regmap_pushr, dspi->pushr_tx, txdata);
}

static void dspi_xspi_fifo_write(struct fsl_dspi *dspi, int num_words)
{
	int num_bytes = num_words * dspi->oper_word_size;
	u16 tx_cmd = dspi->tx_cmd;

	/*
	 * If the PCS needs to de-assert (i.e. we're at the end of the buffer
	 * and cs_change does not want the PCS to stay on), then we need a new
	 * PUSHR command, since this one (for the body of the buffer)
	 * necessarily has the CONT bit set.
	 * So send one word less during this go, to force a split and a command
	 * with a single word next time, when CONT will be unset.
	 */
	if (!(dspi->tx_cmd & SPI_PUSHR_CMD_CONT) && num_bytes == dspi->len)
		tx_cmd |= SPI_PUSHR_CMD_EOQ;

	/* Update CTARE */
	regmap_write(dspi->regmap, SPI_CTARE(0),
		     SPI_FRAME_EBITS(dspi->oper_bits_per_word) |
		     SPI_CTARE_DTCP(num_words));

	/*
	 * Write the CMD FIFO entry first, and then the two
	 * corresponding TX FIFO entries (or one...).
	 */
	dspi_pushr_cmd_write(dspi, tx_cmd);

	/* Fill TX FIFO with as many transfers as possible */
	while (num_words--) {
		u32 data = dspi_pop_tx(dspi);

		dspi_pushr_txdata_write(dspi, data & 0xFFFF);
		if (dspi->oper_bits_per_word > 16)
			dspi_pushr_txdata_write(dspi, data >> 16);
	}
}

static void dspi_eoq_fifo_write(struct fsl_dspi *dspi, int num_words)
{
	u16 xfer_cmd = dspi->tx_cmd;

	/* Fill TX FIFO with as many transfers as possible */
	while (num_words--) {
		dspi->tx_cmd = xfer_cmd;
		/* Request EOQF for last transfer in FIFO */
		if (num_words == 0)
			dspi->tx_cmd |= SPI_PUSHR_CMD_EOQ;
		/* Write combined TX FIFO and CMD FIFO entry */
		dspi_pushr_write(dspi);
	}
}

static u32 dspi_popr_read(struct fsl_dspi *dspi)
{
	u32 rxdata = 0;

	regmap_read(dspi->regmap, SPI_POPR, &rxdata);
	return rxdata;
}

static void dspi_fifo_read(struct fsl_dspi *dspi)
{
	int num_fifo_entries = dspi->words_in_flight;

	/* Read one FIFO entry and push to rx buffer */
	while (num_fifo_entries--)
		dspi_push_rx(dspi, dspi_popr_read(dspi));
}

static void dspi_setup_accel(struct fsl_dspi *dspi)
{
	struct spi_transfer *xfer = dspi->cur_transfer;
	bool odd = !!(dspi->len & 1);

	/* No accel for frames not multiple of 8 bits at the moment */
	if (xfer->bits_per_word % 8)
		goto no_accel;

	if (!odd && dspi->len <= dspi->devtype_data->fifo_size * 2) {
		dspi->oper_bits_per_word = 16;
	} else if (odd && dspi->len <= dspi->devtype_data->fifo_size) {
		dspi->oper_bits_per_word = 8;
	} else {
		/* Start off with maximum supported by hardware */
		if (dspi->devtype_data->trans_mode == DSPI_XSPI_MODE)
			dspi->oper_bits_per_word = 32;
		else
			dspi->oper_bits_per_word = 16;

		/*
		 * And go down only if the buffer can't be sent with
		 * words this big
		 */
		do {
			if (dspi->len >= DIV_ROUND_UP(dspi->oper_bits_per_word, 8))
				break;

			dspi->oper_bits_per_word /= 2;
		} while (dspi->oper_bits_per_word > 8);
	}

	if (xfer->bits_per_word == 8 && dspi->oper_bits_per_word == 32) {
		dspi->dev_to_host = dspi_8on32_dev_to_host;
		dspi->host_to_dev = dspi_8on32_host_to_dev;
	} else if (xfer->bits_per_word == 8 && dspi->oper_bits_per_word == 16) {
		dspi->dev_to_host = dspi_8on16_dev_to_host;
		dspi->host_to_dev = dspi_8on16_host_to_dev;
	} else if (xfer->bits_per_word == 16 && dspi->oper_bits_per_word == 32) {
		dspi->dev_to_host = dspi_16on32_dev_to_host;
		dspi->host_to_dev = dspi_16on32_host_to_dev;
	} else {
no_accel:
		dspi->dev_to_host = dspi_native_dev_to_host;
		dspi->host_to_dev = dspi_native_host_to_dev;
		dspi->oper_bits_per_word = xfer->bits_per_word;
	}

	dspi->oper_word_size = DIV_ROUND_UP(dspi->oper_bits_per_word, 8);

	/*
	 * Update CTAR here (code is common for EOQ, XSPI and DMA modes).
	 * We will update CTARE in the portion specific to XSPI, when we
	 * also know the preload value (DTCP).
	 */
	regmap_write(dspi->regmap, SPI_CTAR(0),
		     dspi->cur_chip->ctar_val |
		     SPI_FRAME_BITS(dspi->oper_bits_per_word));
}

static void dspi_fifo_write(struct fsl_dspi *dspi)
{
	int num_fifo_entries = dspi->devtype_data->fifo_size;
	struct spi_transfer *xfer = dspi->cur_transfer;
	struct spi_message *msg = dspi->cur_msg;
	int num_words, num_bytes;

	dspi_setup_accel(dspi);

	/* In XSPI mode each 32-bit word occupies 2 TX FIFO entries */
	if (dspi->oper_word_size == 4)
		num_fifo_entries /= 2;

	/*
	 * Integer division intentionally trims off odd (or non-multiple of 4)
	 * numbers of bytes at the end of the buffer, which will be sent next
	 * time using a smaller oper_word_size.
	 */
	num_words = dspi->len / dspi->oper_word_size;
	if (num_words > num_fifo_entries)
		num_words = num_fifo_entries;

	/* Update total number of bytes that were transferred */
	num_bytes = num_words * dspi->oper_word_size;
	msg->actual_length += num_bytes;
	dspi->progress += num_bytes / DIV_ROUND_UP(xfer->bits_per_word, 8);

	/*
	 * Update shared variable for use in the next interrupt (both in
	 * dspi_fifo_read and in dspi_fifo_write).
	 */
	dspi->words_in_flight = num_words;

	spi_take_timestamp_pre(dspi->ctlr, xfer, dspi->progress, !dspi->irq);

	if (dspi->devtype_data->trans_mode == DSPI_EOQ_MODE)
		dspi_eoq_fifo_write(dspi, num_words);
	else
		dspi_xspi_fifo_write(dspi, num_words);
	/*
	 * Everything after this point is in a potential race with the next
	 * interrupt, so we must never use dspi->words_in_flight again since it
	 * might already be modified by the next dspi_fifo_write.
	 */

	spi_take_timestamp_post(dspi->ctlr, dspi->cur_transfer,
				dspi->progress, !dspi->irq);
}

static int dspi_rxtx(struct fsl_dspi *dspi)
{
	dspi_fifo_read(dspi);

	if (!dspi->len)
		/* Success! */
		return 0;

	dspi_fifo_write(dspi);

	return -EINPROGRESS;
}

static int dspi_poll(struct fsl_dspi *dspi)
{
	int tries = 1000;
	u32 spi_sr;

	do {
		regmap_read(dspi->regmap, SPI_SR, &spi_sr);
		regmap_write(dspi->regmap, SPI_SR, spi_sr);

		if (spi_sr & (SPI_SR_EOQF | SPI_SR_CMDTCF))
			break;
	} while (--tries);

	if (!tries)
		return -ETIMEDOUT;

	return dspi_rxtx(dspi);
}

static irqreturn_t dspi_interrupt(int irq, void *dev_id)
{
	struct fsl_dspi *dspi = (struct fsl_dspi *)dev_id;
	u32 spi_sr;

	regmap_read(dspi->regmap, SPI_SR, &spi_sr);
	regmap_write(dspi->regmap, SPI_SR, spi_sr);

	if (!(spi_sr & (SPI_SR_EOQF | SPI_SR_CMDTCF)))
		return IRQ_NONE;

	if (dspi_rxtx(dspi) == 0)
		complete(&dspi->xfer_done);

	return IRQ_HANDLED;
}

static int dspi_transfer_one_message(struct spi_controller *ctlr,
				     struct spi_message *message)
{
	struct fsl_dspi *dspi = spi_controller_get_devdata(ctlr);
	struct spi_device *spi = message->spi;
	struct spi_transfer *transfer;
	int status = 0;

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

		dspi->tx = transfer->tx_buf;
		dspi->rx = transfer->rx_buf;
		dspi->len = transfer->len;
		dspi->progress = 0;

		regmap_update_bits(dspi->regmap, SPI_MCR,
				   SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF,
				   SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF);

		spi_take_timestamp_pre(dspi->ctlr, dspi->cur_transfer,
				       dspi->progress, !dspi->irq);

		if (dspi->devtype_data->trans_mode == DSPI_DMA_MODE) {
			status = dspi_dma_xfer(dspi);
		} else {
			dspi_fifo_write(dspi);

			if (dspi->irq) {
				wait_for_completion(&dspi->xfer_done);
				reinit_completion(&dspi->xfer_done);
			} else {
				do {
					status = dspi_poll(dspi);
				} while (status == -EINPROGRESS);
			}
		}
		if (status)
			break;

		spi_transfer_delay_exec(transfer);
	}

	message->status = status;
	spi_finalize_current_message(ctlr);

	return status;
}

static int dspi_setup(struct spi_device *spi)
{
	struct fsl_dspi *dspi = spi_controller_get_devdata(spi->controller);
	unsigned char br = 0, pbr = 0, pcssck = 0, cssck = 0;
	u32 cs_sck_delay = 0, sck_cs_delay = 0;
	struct fsl_dspi_platform_data *pdata;
	unsigned char pasc = 0, asc = 0;
	struct chip_data *chip;
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

	clkrate = clk_get_rate(dspi->clk);
	hz_to_spi_baud(&pbr, &br, spi->max_speed_hz, clkrate);

	/* Set PCS to SCK delay scale values */
	ns_delay_scale(&pcssck, &cssck, cs_sck_delay, clkrate);

	/* Set After SCK delay scale values */
	ns_delay_scale(&pasc, &asc, sck_cs_delay, clkrate);

	chip->ctar_val = 0;
	if (spi->mode & SPI_CPOL)
		chip->ctar_val |= SPI_CTAR_CPOL;
	if (spi->mode & SPI_CPHA)
		chip->ctar_val |= SPI_CTAR_CPHA;

	if (!spi_controller_is_slave(dspi->ctlr)) {
		chip->ctar_val |= SPI_CTAR_PCSSCK(pcssck) |
				  SPI_CTAR_CSSCK(cssck) |
				  SPI_CTAR_PASC(pasc) |
				  SPI_CTAR_ASC(asc) |
				  SPI_CTAR_PBR(pbr) |
				  SPI_CTAR_BR(br);

		if (spi->mode & SPI_LSB_FIRST)
			chip->ctar_val |= SPI_CTAR_LSBFE;
	}

	spi_set_ctldata(spi, chip);

	return 0;
}

static void dspi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata((struct spi_device *)spi);

	dev_dbg(&spi->dev, "spi_device %u.%u cleanup\n",
		spi->controller->bus_num, spi->chip_select);

	kfree(chip);
}

static const struct of_device_id fsl_dspi_dt_ids[] = {
	{
		.compatible = "fsl,vf610-dspi",
		.data = &devtype_data[VF610],
	}, {
		.compatible = "fsl,ls1021a-v1.0-dspi",
		.data = &devtype_data[LS1021A],
	}, {
		.compatible = "fsl,ls1012a-dspi",
		.data = &devtype_data[LS1012A],
	}, {
		.compatible = "fsl,ls1028a-dspi",
		.data = &devtype_data[LS1028A],
	}, {
		.compatible = "fsl,ls1043a-dspi",
		.data = &devtype_data[LS1043A],
	}, {
		.compatible = "fsl,ls1046a-dspi",
		.data = &devtype_data[LS1046A],
	}, {
		.compatible = "fsl,ls2080a-dspi",
		.data = &devtype_data[LS2080A],
	}, {
		.compatible = "fsl,ls2085a-dspi",
		.data = &devtype_data[LS2085A],
	}, {
		.compatible = "fsl,lx2160a-dspi",
		.data = &devtype_data[LX2160A],
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_dspi_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int dspi_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct fsl_dspi *dspi = spi_controller_get_devdata(ctlr);

	if (dspi->irq)
		disable_irq(dspi->irq);
	spi_controller_suspend(ctlr);
	clk_disable_unprepare(dspi->clk);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int dspi_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct fsl_dspi *dspi = spi_controller_get_devdata(ctlr);
	int ret;

	pinctrl_pm_select_default_state(dev);

	ret = clk_prepare_enable(dspi->clk);
	if (ret)
		return ret;
	spi_controller_resume(ctlr);
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
	.yes_ranges	= dspi_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(dspi_volatile_ranges),
};

static const struct regmap_config dspi_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x88,
	.volatile_table	= &dspi_volatile_table,
};

static const struct regmap_range dspi_xspi_volatile_ranges[] = {
	regmap_reg_range(SPI_MCR, SPI_TCR),
	regmap_reg_range(SPI_SR, SPI_SR),
	regmap_reg_range(SPI_PUSHR, SPI_RXFR3),
	regmap_reg_range(SPI_SREX, SPI_SREX),
};

static const struct regmap_access_table dspi_xspi_volatile_table = {
	.yes_ranges	= dspi_xspi_volatile_ranges,
	.n_yes_ranges	= ARRAY_SIZE(dspi_xspi_volatile_ranges),
};

static const struct regmap_config dspi_xspi_regmap_config[] = {
	{
		.reg_bits	= 32,
		.val_bits	= 32,
		.reg_stride	= 4,
		.max_register	= 0x13c,
		.volatile_table	= &dspi_xspi_volatile_table,
	},
	{
		.name		= "pushr",
		.reg_bits	= 16,
		.val_bits	= 16,
		.reg_stride	= 2,
		.max_register	= 0x2,
	},
};

static int dspi_init(struct fsl_dspi *dspi)
{
	unsigned int mcr;

	/* Set idle states for all chip select signals to high */
	mcr = SPI_MCR_PCSIS(GENMASK(dspi->ctlr->num_chipselect - 1, 0));

	if (dspi->devtype_data->trans_mode == DSPI_XSPI_MODE)
		mcr |= SPI_MCR_XSPI;
	if (!spi_controller_is_slave(dspi->ctlr))
		mcr |= SPI_MCR_MASTER;

	regmap_write(dspi->regmap, SPI_MCR, mcr);
	regmap_write(dspi->regmap, SPI_SR, SPI_SR_CLEAR);

	switch (dspi->devtype_data->trans_mode) {
	case DSPI_EOQ_MODE:
		regmap_write(dspi->regmap, SPI_RSER, SPI_RSER_EOQFE);
		break;
	case DSPI_XSPI_MODE:
		regmap_write(dspi->regmap, SPI_RSER, SPI_RSER_CMDTCFE);
		break;
	case DSPI_DMA_MODE:
		regmap_write(dspi->regmap, SPI_RSER,
			     SPI_RSER_TFFFE | SPI_RSER_TFFFD |
			     SPI_RSER_RFDFE | SPI_RSER_RFDFD);
		break;
	default:
		dev_err(&dspi->pdev->dev, "unsupported trans_mode %u\n",
			dspi->devtype_data->trans_mode);
		return -EINVAL;
	}

	return 0;
}

static int dspi_slave_abort(struct spi_master *master)
{
	struct fsl_dspi *dspi = spi_master_get_devdata(master);

	/*
	 * Terminate all pending DMA transactions for the SPI working
	 * in SLAVE mode.
	 */
	if (dspi->devtype_data->trans_mode == DSPI_DMA_MODE) {
		dmaengine_terminate_sync(dspi->dma->chan_rx);
		dmaengine_terminate_sync(dspi->dma->chan_tx);
	}

	/* Clear the internal DSPI RX and TX FIFO buffers */
	regmap_update_bits(dspi->regmap, SPI_MCR,
			   SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF,
			   SPI_MCR_CLR_TXF | SPI_MCR_CLR_RXF);

	return 0;
}

/*
 * EOQ mode will inevitably deassert its PCS signal on last word in a queue
 * (hardware limitation), so we need to inform the spi_device that larger
 * buffers than the FIFO size are going to have the chip select randomly
 * toggling, so it has a chance to adapt its message sizes.
 */
static size_t dspi_max_message_size(struct spi_device *spi)
{
	struct fsl_dspi *dspi = spi_controller_get_devdata(spi->controller);

	if (dspi->devtype_data->trans_mode == DSPI_EOQ_MODE)
		return dspi->devtype_data->fifo_size;

	return SIZE_MAX;
}

static int dspi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct regmap_config *regmap_config;
	struct fsl_dspi_platform_data *pdata;
	struct spi_controller *ctlr;
	int ret, cs_num, bus_num = -1;
	struct fsl_dspi *dspi;
	struct resource *res;
	void __iomem *base;
	bool big_endian;

	ctlr = spi_alloc_master(&pdev->dev, sizeof(struct fsl_dspi));
	if (!ctlr)
		return -ENOMEM;

	dspi = spi_controller_get_devdata(ctlr);
	dspi->pdev = pdev;
	dspi->ctlr = ctlr;

	ctlr->setup = dspi_setup;
	ctlr->transfer_one_message = dspi_transfer_one_message;
	ctlr->max_message_size = dspi_max_message_size;
	ctlr->dev.of_node = pdev->dev.of_node;

	ctlr->cleanup = dspi_cleanup;
	ctlr->slave_abort = dspi_slave_abort;
	ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		ctlr->num_chipselect = pdata->cs_num;
		ctlr->bus_num = pdata->bus_num;

		/* Only Coldfire uses platform data */
		dspi->devtype_data = &devtype_data[MCF5441X];
		big_endian = true;
	} else {

		ret = of_property_read_u32(np, "spi-num-chipselects", &cs_num);
		if (ret < 0) {
			dev_err(&pdev->dev, "can't get spi-num-chipselects\n");
			goto out_ctlr_put;
		}
		ctlr->num_chipselect = cs_num;

		of_property_read_u32(np, "bus-num", &bus_num);
		ctlr->bus_num = bus_num;

		if (of_property_read_bool(np, "spi-slave"))
			ctlr->slave = true;

		dspi->devtype_data = of_device_get_match_data(&pdev->dev);
		if (!dspi->devtype_data) {
			dev_err(&pdev->dev, "can't get devtype_data\n");
			ret = -EFAULT;
			goto out_ctlr_put;
		}

		big_endian = of_device_is_big_endian(np);
	}
	if (big_endian) {
		dspi->pushr_cmd = 0;
		dspi->pushr_tx = 2;
	} else {
		dspi->pushr_cmd = 2;
		dspi->pushr_tx = 0;
	}

	if (dspi->devtype_data->trans_mode == DSPI_XSPI_MODE)
		ctlr->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 32);
	else
		ctlr->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out_ctlr_put;
	}

	if (dspi->devtype_data->trans_mode == DSPI_XSPI_MODE)
		regmap_config = &dspi_xspi_regmap_config[0];
	else
		regmap_config = &dspi_regmap_config;
	dspi->regmap = devm_regmap_init_mmio(&pdev->dev, base, regmap_config);
	if (IS_ERR(dspi->regmap)) {
		dev_err(&pdev->dev, "failed to init regmap: %ld\n",
				PTR_ERR(dspi->regmap));
		ret = PTR_ERR(dspi->regmap);
		goto out_ctlr_put;
	}

	if (dspi->devtype_data->trans_mode == DSPI_XSPI_MODE) {
		dspi->regmap_pushr = devm_regmap_init_mmio(
			&pdev->dev, base + SPI_PUSHR,
			&dspi_xspi_regmap_config[1]);
		if (IS_ERR(dspi->regmap_pushr)) {
			dev_err(&pdev->dev,
				"failed to init pushr regmap: %ld\n",
				PTR_ERR(dspi->regmap_pushr));
			ret = PTR_ERR(dspi->regmap_pushr);
			goto out_ctlr_put;
		}
	}

	dspi->clk = devm_clk_get(&pdev->dev, "dspi");
	if (IS_ERR(dspi->clk)) {
		ret = PTR_ERR(dspi->clk);
		dev_err(&pdev->dev, "unable to get clock\n");
		goto out_ctlr_put;
	}
	ret = clk_prepare_enable(dspi->clk);
	if (ret)
		goto out_ctlr_put;

	ret = dspi_init(dspi);
	if (ret)
		goto out_clk_put;

	dspi->irq = platform_get_irq(pdev, 0);
	if (dspi->irq <= 0) {
		dev_info(&pdev->dev,
			 "can't get platform irq, using poll mode\n");
		dspi->irq = 0;
		goto poll_mode;
	}

	ret = request_threaded_irq(dspi->irq, dspi_interrupt, NULL,
				   IRQF_SHARED, pdev->name, dspi);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to attach DSPI interrupt\n");
		goto out_clk_put;
	}

	init_completion(&dspi->xfer_done);

poll_mode:

	if (dspi->devtype_data->trans_mode == DSPI_DMA_MODE) {
		ret = dspi_request_dma(dspi, res->start);
		if (ret < 0) {
			dev_err(&pdev->dev, "can't get dma channels\n");
			goto out_free_irq;
		}
	}

	ctlr->max_speed_hz =
		clk_get_rate(dspi->clk) / dspi->devtype_data->max_clock_factor;

	if (dspi->devtype_data->trans_mode != DSPI_DMA_MODE)
		ctlr->ptp_sts_supported = true;

	platform_set_drvdata(pdev, ctlr);

	ret = spi_register_controller(ctlr);
	if (ret != 0) {
		dev_err(&pdev->dev, "Problem registering DSPI ctlr\n");
		goto out_free_irq;
	}

	return ret;

out_free_irq:
	if (dspi->irq)
		free_irq(dspi->irq, dspi);
out_clk_put:
	clk_disable_unprepare(dspi->clk);
out_ctlr_put:
	spi_controller_put(ctlr);

	return ret;
}

static int dspi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct fsl_dspi *dspi = spi_controller_get_devdata(ctlr);

	/* Disconnect from the SPI framework */
	spi_unregister_controller(dspi->ctlr);

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
	.driver.name		= DRIVER_NAME,
	.driver.of_match_table	= fsl_dspi_dt_ids,
	.driver.owner		= THIS_MODULE,
	.driver.pm		= &dspi_pm,
	.probe			= dspi_probe,
	.remove			= dspi_remove,
	.shutdown		= dspi_shutdown,
};
module_platform_driver(fsl_dspi_driver);

MODULE_DESCRIPTION("Freescale DSPI Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
