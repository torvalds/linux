/*
 * Portions copyright (C) 2003 Russell King, PXA MMCI Driver
 * Portions copyright (C) 2004-2005 Pierre Ossman, W83L51xD SD/MMC driver
 *
 * Copyright 2008 Embedded Alley Solutions, Inc.
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/highmem.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/fsl/mxs-dma.h>
#include <linux/pinctrl/consumer.h>
#include <linux/stmp_device.h>
#include <linux/mmc/mxs-mmc.h>

#define DRIVER_NAME	"mxs-mmc"

/* card detect polling timeout */
#define MXS_MMC_DETECT_TIMEOUT			(HZ/2)

#define ssp_is_old(host)	((host)->devid == IMX23_MMC)

/* SSP registers */
#define HW_SSP_CTRL0				0x000
#define  BM_SSP_CTRL0_RUN			(1 << 29)
#define  BM_SSP_CTRL0_SDIO_IRQ_CHECK		(1 << 28)
#define  BM_SSP_CTRL0_IGNORE_CRC		(1 << 26)
#define  BM_SSP_CTRL0_READ			(1 << 25)
#define  BM_SSP_CTRL0_DATA_XFER			(1 << 24)
#define  BP_SSP_CTRL0_BUS_WIDTH			(22)
#define  BM_SSP_CTRL0_BUS_WIDTH			(0x3 << 22)
#define  BM_SSP_CTRL0_WAIT_FOR_IRQ		(1 << 21)
#define  BM_SSP_CTRL0_LONG_RESP			(1 << 19)
#define  BM_SSP_CTRL0_GET_RESP			(1 << 17)
#define  BM_SSP_CTRL0_ENABLE			(1 << 16)
#define  BP_SSP_CTRL0_XFER_COUNT		(0)
#define  BM_SSP_CTRL0_XFER_COUNT		(0xffff)
#define HW_SSP_CMD0				0x010
#define  BM_SSP_CMD0_DBL_DATA_RATE_EN		(1 << 25)
#define  BM_SSP_CMD0_SLOW_CLKING_EN		(1 << 22)
#define  BM_SSP_CMD0_CONT_CLKING_EN		(1 << 21)
#define  BM_SSP_CMD0_APPEND_8CYC		(1 << 20)
#define  BP_SSP_CMD0_BLOCK_SIZE			(16)
#define  BM_SSP_CMD0_BLOCK_SIZE			(0xf << 16)
#define  BP_SSP_CMD0_BLOCK_COUNT		(8)
#define  BM_SSP_CMD0_BLOCK_COUNT		(0xff << 8)
#define  BP_SSP_CMD0_CMD			(0)
#define  BM_SSP_CMD0_CMD			(0xff)
#define HW_SSP_CMD1				0x020
#define HW_SSP_XFER_SIZE			0x030
#define HW_SSP_BLOCK_SIZE			0x040
#define  BP_SSP_BLOCK_SIZE_BLOCK_COUNT		(4)
#define  BM_SSP_BLOCK_SIZE_BLOCK_COUNT		(0xffffff << 4)
#define  BP_SSP_BLOCK_SIZE_BLOCK_SIZE		(0)
#define  BM_SSP_BLOCK_SIZE_BLOCK_SIZE		(0xf)
#define HW_SSP_TIMING(h)			(ssp_is_old(h) ? 0x050 : 0x070)
#define  BP_SSP_TIMING_TIMEOUT			(16)
#define  BM_SSP_TIMING_TIMEOUT			(0xffff << 16)
#define  BP_SSP_TIMING_CLOCK_DIVIDE		(8)
#define  BM_SSP_TIMING_CLOCK_DIVIDE		(0xff << 8)
#define  BP_SSP_TIMING_CLOCK_RATE		(0)
#define  BM_SSP_TIMING_CLOCK_RATE		(0xff)
#define HW_SSP_CTRL1(h)				(ssp_is_old(h) ? 0x060 : 0x080)
#define  BM_SSP_CTRL1_SDIO_IRQ			(1 << 31)
#define  BM_SSP_CTRL1_SDIO_IRQ_EN		(1 << 30)
#define  BM_SSP_CTRL1_RESP_ERR_IRQ		(1 << 29)
#define  BM_SSP_CTRL1_RESP_ERR_IRQ_EN		(1 << 28)
#define  BM_SSP_CTRL1_RESP_TIMEOUT_IRQ		(1 << 27)
#define  BM_SSP_CTRL1_RESP_TIMEOUT_IRQ_EN	(1 << 26)
#define  BM_SSP_CTRL1_DATA_TIMEOUT_IRQ		(1 << 25)
#define  BM_SSP_CTRL1_DATA_TIMEOUT_IRQ_EN	(1 << 24)
#define  BM_SSP_CTRL1_DATA_CRC_IRQ		(1 << 23)
#define  BM_SSP_CTRL1_DATA_CRC_IRQ_EN		(1 << 22)
#define  BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ		(1 << 21)
#define  BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ_EN	(1 << 20)
#define  BM_SSP_CTRL1_RECV_TIMEOUT_IRQ		(1 << 17)
#define  BM_SSP_CTRL1_RECV_TIMEOUT_IRQ_EN	(1 << 16)
#define  BM_SSP_CTRL1_FIFO_OVERRUN_IRQ		(1 << 15)
#define  BM_SSP_CTRL1_FIFO_OVERRUN_IRQ_EN	(1 << 14)
#define  BM_SSP_CTRL1_DMA_ENABLE		(1 << 13)
#define  BM_SSP_CTRL1_POLARITY			(1 << 9)
#define  BP_SSP_CTRL1_WORD_LENGTH		(4)
#define  BM_SSP_CTRL1_WORD_LENGTH		(0xf << 4)
#define  BP_SSP_CTRL1_SSP_MODE			(0)
#define  BM_SSP_CTRL1_SSP_MODE			(0xf)
#define HW_SSP_SDRESP0(h)			(ssp_is_old(h) ? 0x080 : 0x0a0)
#define HW_SSP_SDRESP1(h)			(ssp_is_old(h) ? 0x090 : 0x0b0)
#define HW_SSP_SDRESP2(h)			(ssp_is_old(h) ? 0x0a0 : 0x0c0)
#define HW_SSP_SDRESP3(h)			(ssp_is_old(h) ? 0x0b0 : 0x0d0)
#define HW_SSP_STATUS(h)			(ssp_is_old(h) ? 0x0c0 : 0x100)
#define  BM_SSP_STATUS_CARD_DETECT		(1 << 28)
#define  BM_SSP_STATUS_SDIO_IRQ			(1 << 17)

#define BF_SSP(value, field)	(((value) << BP_SSP_##field) & BM_SSP_##field)

#define MXS_MMC_IRQ_BITS	(BM_SSP_CTRL1_SDIO_IRQ		| \
				 BM_SSP_CTRL1_RESP_ERR_IRQ	| \
				 BM_SSP_CTRL1_RESP_TIMEOUT_IRQ	| \
				 BM_SSP_CTRL1_DATA_TIMEOUT_IRQ	| \
				 BM_SSP_CTRL1_DATA_CRC_IRQ	| \
				 BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ	| \
				 BM_SSP_CTRL1_RECV_TIMEOUT_IRQ  | \
				 BM_SSP_CTRL1_FIFO_OVERRUN_IRQ)

#define SSP_PIO_NUM	3

enum mxs_mmc_id {
	IMX23_MMC,
	IMX28_MMC,
};

struct mxs_mmc_host {
	struct mmc_host			*mmc;
	struct mmc_request		*mrq;
	struct mmc_command		*cmd;
	struct mmc_data			*data;

	void __iomem			*base;
	int				dma_channel;
	struct clk			*clk;
	unsigned int			clk_rate;

	struct dma_chan         	*dmach;
	struct mxs_dma_data		dma_data;
	unsigned int			dma_dir;
	enum dma_transfer_direction	slave_dirn;
	u32				ssp_pio_words[SSP_PIO_NUM];

	enum mxs_mmc_id			devid;
	unsigned char			bus_width;
	spinlock_t			lock;
	int				sdio_irq_en;
	int				wp_gpio;
};

static int mxs_mmc_get_ro(struct mmc_host *mmc)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);

	if (!gpio_is_valid(host->wp_gpio))
		return -EINVAL;

	return gpio_get_value(host->wp_gpio);
}

static int mxs_mmc_get_cd(struct mmc_host *mmc)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);

	return !(readl(host->base + HW_SSP_STATUS(host)) &
		 BM_SSP_STATUS_CARD_DETECT);
}

static void mxs_mmc_reset(struct mxs_mmc_host *host)
{
	u32 ctrl0, ctrl1;

	stmp_reset_block(host->base);

	ctrl0 = BM_SSP_CTRL0_IGNORE_CRC;
	ctrl1 = BF_SSP(0x3, CTRL1_SSP_MODE) |
		BF_SSP(0x7, CTRL1_WORD_LENGTH) |
		BM_SSP_CTRL1_DMA_ENABLE |
		BM_SSP_CTRL1_POLARITY |
		BM_SSP_CTRL1_RECV_TIMEOUT_IRQ_EN |
		BM_SSP_CTRL1_DATA_CRC_IRQ_EN |
		BM_SSP_CTRL1_DATA_TIMEOUT_IRQ_EN |
		BM_SSP_CTRL1_RESP_TIMEOUT_IRQ_EN |
		BM_SSP_CTRL1_RESP_ERR_IRQ_EN;

	writel(BF_SSP(0xffff, TIMING_TIMEOUT) |
	       BF_SSP(2, TIMING_CLOCK_DIVIDE) |
	       BF_SSP(0, TIMING_CLOCK_RATE),
	       host->base + HW_SSP_TIMING(host));

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		ctrl1 |= BM_SSP_CTRL1_SDIO_IRQ_EN;
	}

	writel(ctrl0, host->base + HW_SSP_CTRL0);
	writel(ctrl1, host->base + HW_SSP_CTRL1(host));
}

static void mxs_mmc_start_cmd(struct mxs_mmc_host *host,
			      struct mmc_command *cmd);

static void mxs_mmc_request_done(struct mxs_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = host->data;
	struct mmc_request *mrq = host->mrq;

	if (mmc_resp_type(cmd) & MMC_RSP_PRESENT) {
		if (mmc_resp_type(cmd) & MMC_RSP_136) {
			cmd->resp[3] = readl(host->base + HW_SSP_SDRESP0(host));
			cmd->resp[2] = readl(host->base + HW_SSP_SDRESP1(host));
			cmd->resp[1] = readl(host->base + HW_SSP_SDRESP2(host));
			cmd->resp[0] = readl(host->base + HW_SSP_SDRESP3(host));
		} else {
			cmd->resp[0] = readl(host->base + HW_SSP_SDRESP0(host));
		}
	}

	if (data) {
		dma_unmap_sg(mmc_dev(host->mmc), data->sg,
			     data->sg_len, host->dma_dir);
		/*
		 * If there was an error on any block, we mark all
		 * data blocks as being in error.
		 */
		if (!data->error)
			data->bytes_xfered = data->blocks * data->blksz;
		else
			data->bytes_xfered = 0;

		host->data = NULL;
		if (mrq->stop) {
			mxs_mmc_start_cmd(host, mrq->stop);
			return;
		}
	}

	host->mrq = NULL;
	mmc_request_done(host->mmc, mrq);
}

static void mxs_mmc_dma_irq_callback(void *param)
{
	struct mxs_mmc_host *host = param;

	mxs_mmc_request_done(host);
}

static irqreturn_t mxs_mmc_irq_handler(int irq, void *dev_id)
{
	struct mxs_mmc_host *host = dev_id;
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = host->data;
	u32 stat;

	spin_lock(&host->lock);

	stat = readl(host->base + HW_SSP_CTRL1(host));
	writel(stat & MXS_MMC_IRQ_BITS,
	       host->base + HW_SSP_CTRL1(host) + STMP_OFFSET_REG_CLR);

	if ((stat & BM_SSP_CTRL1_SDIO_IRQ) && (stat & BM_SSP_CTRL1_SDIO_IRQ_EN))
		mmc_signal_sdio_irq(host->mmc);

	spin_unlock(&host->lock);

	if (stat & BM_SSP_CTRL1_RESP_TIMEOUT_IRQ)
		cmd->error = -ETIMEDOUT;
	else if (stat & BM_SSP_CTRL1_RESP_ERR_IRQ)
		cmd->error = -EIO;

	if (data) {
		if (stat & (BM_SSP_CTRL1_DATA_TIMEOUT_IRQ |
			    BM_SSP_CTRL1_RECV_TIMEOUT_IRQ))
			data->error = -ETIMEDOUT;
		else if (stat & BM_SSP_CTRL1_DATA_CRC_IRQ)
			data->error = -EILSEQ;
		else if (stat & (BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ |
				 BM_SSP_CTRL1_FIFO_OVERRUN_IRQ))
			data->error = -EIO;
	}

	return IRQ_HANDLED;
}

static struct dma_async_tx_descriptor *mxs_mmc_prep_dma(
	struct mxs_mmc_host *host, unsigned long flags)
{
	struct dma_async_tx_descriptor *desc;
	struct mmc_data *data = host->data;
	struct scatterlist * sgl;
	unsigned int sg_len;

	if (data) {
		/* data */
		dma_map_sg(mmc_dev(host->mmc), data->sg,
			   data->sg_len, host->dma_dir);
		sgl = data->sg;
		sg_len = data->sg_len;
	} else {
		/* pio */
		sgl = (struct scatterlist *) host->ssp_pio_words;
		sg_len = SSP_PIO_NUM;
	}

	desc = dmaengine_prep_slave_sg(host->dmach,
				sgl, sg_len, host->slave_dirn, flags);
	if (desc) {
		desc->callback = mxs_mmc_dma_irq_callback;
		desc->callback_param = host;
	} else {
		if (data)
			dma_unmap_sg(mmc_dev(host->mmc), data->sg,
				     data->sg_len, host->dma_dir);
	}

	return desc;
}

static void mxs_mmc_bc(struct mxs_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct dma_async_tx_descriptor *desc;
	u32 ctrl0, cmd0, cmd1;

	ctrl0 = BM_SSP_CTRL0_ENABLE | BM_SSP_CTRL0_IGNORE_CRC;
	cmd0 = BF_SSP(cmd->opcode, CMD0_CMD) | BM_SSP_CMD0_APPEND_8CYC;
	cmd1 = cmd->arg;

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		cmd0 |= BM_SSP_CMD0_CONT_CLKING_EN | BM_SSP_CMD0_SLOW_CLKING_EN;
	}

	host->ssp_pio_words[0] = ctrl0;
	host->ssp_pio_words[1] = cmd0;
	host->ssp_pio_words[2] = cmd1;
	host->dma_dir = DMA_NONE;
	host->slave_dirn = DMA_TRANS_NONE;
	desc = mxs_mmc_prep_dma(host, DMA_CTRL_ACK);
	if (!desc)
		goto out;

	dmaengine_submit(desc);
	dma_async_issue_pending(host->dmach);
	return;

out:
	dev_warn(mmc_dev(host->mmc),
		 "%s: failed to prep dma\n", __func__);
}

static void mxs_mmc_ac(struct mxs_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct dma_async_tx_descriptor *desc;
	u32 ignore_crc, get_resp, long_resp;
	u32 ctrl0, cmd0, cmd1;

	ignore_crc = (mmc_resp_type(cmd) & MMC_RSP_CRC) ?
			0 : BM_SSP_CTRL0_IGNORE_CRC;
	get_resp = (mmc_resp_type(cmd) & MMC_RSP_PRESENT) ?
			BM_SSP_CTRL0_GET_RESP : 0;
	long_resp = (mmc_resp_type(cmd) & MMC_RSP_136) ?
			BM_SSP_CTRL0_LONG_RESP : 0;

	ctrl0 = BM_SSP_CTRL0_ENABLE | ignore_crc | get_resp | long_resp;
	cmd0 = BF_SSP(cmd->opcode, CMD0_CMD);
	cmd1 = cmd->arg;

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		cmd0 |= BM_SSP_CMD0_CONT_CLKING_EN | BM_SSP_CMD0_SLOW_CLKING_EN;
	}

	host->ssp_pio_words[0] = ctrl0;
	host->ssp_pio_words[1] = cmd0;
	host->ssp_pio_words[2] = cmd1;
	host->dma_dir = DMA_NONE;
	host->slave_dirn = DMA_TRANS_NONE;
	desc = mxs_mmc_prep_dma(host, DMA_CTRL_ACK);
	if (!desc)
		goto out;

	dmaengine_submit(desc);
	dma_async_issue_pending(host->dmach);
	return;

out:
	dev_warn(mmc_dev(host->mmc),
		 "%s: failed to prep dma\n", __func__);
}

static unsigned short mxs_ns_to_ssp_ticks(unsigned clock_rate, unsigned ns)
{
	const unsigned int ssp_timeout_mul = 4096;
	/*
	 * Calculate ticks in ms since ns are large numbers
	 * and might overflow
	 */
	const unsigned int clock_per_ms = clock_rate / 1000;
	const unsigned int ms = ns / 1000;
	const unsigned int ticks = ms * clock_per_ms;
	const unsigned int ssp_ticks = ticks / ssp_timeout_mul;

	WARN_ON(ssp_ticks == 0);
	return ssp_ticks;
}

static void mxs_mmc_adtc(struct mxs_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;
	struct dma_async_tx_descriptor *desc;
	struct scatterlist *sgl = data->sg, *sg;
	unsigned int sg_len = data->sg_len;
	int i;

	unsigned short dma_data_dir, timeout;
	enum dma_transfer_direction slave_dirn;
	unsigned int data_size = 0, log2_blksz;
	unsigned int blocks = data->blocks;

	u32 ignore_crc, get_resp, long_resp, read;
	u32 ctrl0, cmd0, cmd1, val;

	ignore_crc = (mmc_resp_type(cmd) & MMC_RSP_CRC) ?
			0 : BM_SSP_CTRL0_IGNORE_CRC;
	get_resp = (mmc_resp_type(cmd) & MMC_RSP_PRESENT) ?
			BM_SSP_CTRL0_GET_RESP : 0;
	long_resp = (mmc_resp_type(cmd) & MMC_RSP_136) ?
			BM_SSP_CTRL0_LONG_RESP : 0;

	if (data->flags & MMC_DATA_WRITE) {
		dma_data_dir = DMA_TO_DEVICE;
		slave_dirn = DMA_MEM_TO_DEV;
		read = 0;
	} else {
		dma_data_dir = DMA_FROM_DEVICE;
		slave_dirn = DMA_DEV_TO_MEM;
		read = BM_SSP_CTRL0_READ;
	}

	ctrl0 = BF_SSP(host->bus_width, CTRL0_BUS_WIDTH) |
		ignore_crc | get_resp | long_resp |
		BM_SSP_CTRL0_DATA_XFER | read |
		BM_SSP_CTRL0_WAIT_FOR_IRQ |
		BM_SSP_CTRL0_ENABLE;

	cmd0 = BF_SSP(cmd->opcode, CMD0_CMD);

	/* get logarithm to base 2 of block size for setting register */
	log2_blksz = ilog2(data->blksz);

	/*
	 * take special care of the case that data size from data->sg
	 * is not equal to blocks x blksz
	 */
	for_each_sg(sgl, sg, sg_len, i)
		data_size += sg->length;

	if (data_size != data->blocks * data->blksz)
		blocks = 1;

	/* xfer count, block size and count need to be set differently */
	if (ssp_is_old(host)) {
		ctrl0 |= BF_SSP(data_size, CTRL0_XFER_COUNT);
		cmd0 |= BF_SSP(log2_blksz, CMD0_BLOCK_SIZE) |
			BF_SSP(blocks - 1, CMD0_BLOCK_COUNT);
	} else {
		writel(data_size, host->base + HW_SSP_XFER_SIZE);
		writel(BF_SSP(log2_blksz, BLOCK_SIZE_BLOCK_SIZE) |
		       BF_SSP(blocks - 1, BLOCK_SIZE_BLOCK_COUNT),
		       host->base + HW_SSP_BLOCK_SIZE);
	}

	if ((cmd->opcode == MMC_STOP_TRANSMISSION) ||
	    (cmd->opcode == SD_IO_RW_EXTENDED))
		cmd0 |= BM_SSP_CMD0_APPEND_8CYC;

	cmd1 = cmd->arg;

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		cmd0 |= BM_SSP_CMD0_CONT_CLKING_EN | BM_SSP_CMD0_SLOW_CLKING_EN;
	}

	/* set the timeout count */
	timeout = mxs_ns_to_ssp_ticks(host->clk_rate, data->timeout_ns);
	val = readl(host->base + HW_SSP_TIMING(host));
	val &= ~(BM_SSP_TIMING_TIMEOUT);
	val |= BF_SSP(timeout, TIMING_TIMEOUT);
	writel(val, host->base + HW_SSP_TIMING(host));

	/* pio */
	host->ssp_pio_words[0] = ctrl0;
	host->ssp_pio_words[1] = cmd0;
	host->ssp_pio_words[2] = cmd1;
	host->dma_dir = DMA_NONE;
	host->slave_dirn = DMA_TRANS_NONE;
	desc = mxs_mmc_prep_dma(host, 0);
	if (!desc)
		goto out;

	/* append data sg */
	WARN_ON(host->data != NULL);
	host->data = data;
	host->dma_dir = dma_data_dir;
	host->slave_dirn = slave_dirn;
	desc = mxs_mmc_prep_dma(host, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!desc)
		goto out;

	dmaengine_submit(desc);
	dma_async_issue_pending(host->dmach);
	return;
out:
	dev_warn(mmc_dev(host->mmc),
		 "%s: failed to prep dma\n", __func__);
}

static void mxs_mmc_start_cmd(struct mxs_mmc_host *host,
			      struct mmc_command *cmd)
{
	host->cmd = cmd;

	switch (mmc_cmd_type(cmd)) {
	case MMC_CMD_BC:
		mxs_mmc_bc(host);
		break;
	case MMC_CMD_BCR:
		mxs_mmc_ac(host);
		break;
	case MMC_CMD_AC:
		mxs_mmc_ac(host);
		break;
	case MMC_CMD_ADTC:
		mxs_mmc_adtc(host);
		break;
	default:
		dev_warn(mmc_dev(host->mmc),
			 "%s: unknown MMC command\n", __func__);
		break;
	}
}

static void mxs_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);
	host->mrq = mrq;
	mxs_mmc_start_cmd(host, mrq->cmd);
}

static void mxs_mmc_set_clk_rate(struct mxs_mmc_host *host, unsigned int rate)
{
	unsigned int ssp_clk, ssp_sck;
	u32 clock_divide, clock_rate;
	u32 val;

	ssp_clk = clk_get_rate(host->clk);

	for (clock_divide = 2; clock_divide <= 254; clock_divide += 2) {
		clock_rate = DIV_ROUND_UP(ssp_clk, rate * clock_divide);
		clock_rate = (clock_rate > 0) ? clock_rate - 1 : 0;
		if (clock_rate <= 255)
			break;
	}

	if (clock_divide > 254) {
		dev_err(mmc_dev(host->mmc),
			"%s: cannot set clock to %d\n", __func__, rate);
		return;
	}

	ssp_sck = ssp_clk / clock_divide / (1 + clock_rate);

	val = readl(host->base + HW_SSP_TIMING(host));
	val &= ~(BM_SSP_TIMING_CLOCK_DIVIDE | BM_SSP_TIMING_CLOCK_RATE);
	val |= BF_SSP(clock_divide, TIMING_CLOCK_DIVIDE);
	val |= BF_SSP(clock_rate, TIMING_CLOCK_RATE);
	writel(val, host->base + HW_SSP_TIMING(host));

	host->clk_rate = ssp_sck;

	dev_dbg(mmc_dev(host->mmc),
		"%s: clock_divide %d, clock_rate %d, ssp_clk %d, rate_actual %d, rate_requested %d\n",
		__func__, clock_divide, clock_rate, ssp_clk, ssp_sck, rate);
}

static void mxs_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);

	if (ios->bus_width == MMC_BUS_WIDTH_8)
		host->bus_width = 2;
	else if (ios->bus_width == MMC_BUS_WIDTH_4)
		host->bus_width = 1;
	else
		host->bus_width = 0;

	if (ios->clock)
		mxs_mmc_set_clk_rate(host, ios->clock);
}

static void mxs_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	host->sdio_irq_en = enable;

	if (enable) {
		writel(BM_SSP_CTRL0_SDIO_IRQ_CHECK,
		       host->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);
		writel(BM_SSP_CTRL1_SDIO_IRQ_EN,
		       host->base + HW_SSP_CTRL1(host) + STMP_OFFSET_REG_SET);

		if (readl(host->base + HW_SSP_STATUS(host)) &
				BM_SSP_STATUS_SDIO_IRQ)
			mmc_signal_sdio_irq(host->mmc);

	} else {
		writel(BM_SSP_CTRL0_SDIO_IRQ_CHECK,
		       host->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_CLR);
		writel(BM_SSP_CTRL1_SDIO_IRQ_EN,
		       host->base + HW_SSP_CTRL1(host) + STMP_OFFSET_REG_CLR);
	}

	spin_unlock_irqrestore(&host->lock, flags);
}

static const struct mmc_host_ops mxs_mmc_ops = {
	.request = mxs_mmc_request,
	.get_ro = mxs_mmc_get_ro,
	.get_cd = mxs_mmc_get_cd,
	.set_ios = mxs_mmc_set_ios,
	.enable_sdio_irq = mxs_mmc_enable_sdio_irq,
};

static bool mxs_mmc_dma_filter(struct dma_chan *chan, void *param)
{
	struct mxs_mmc_host *host = param;

	if (!mxs_dma_is_apbh(chan))
		return false;

	if (chan->chan_id != host->dma_channel)
		return false;

	chan->private = &host->dma_data;

	return true;
}

static struct platform_device_id mxs_mmc_ids[] = {
	{
		.name = "imx23-mmc",
		.driver_data = IMX23_MMC,
	}, {
		.name = "imx28-mmc",
		.driver_data = IMX28_MMC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, mxs_mmc_ids);

static const struct of_device_id mxs_mmc_dt_ids[] = {
	{ .compatible = "fsl,imx23-mmc", .data = (void *) IMX23_MMC, },
	{ .compatible = "fsl,imx28-mmc", .data = (void *) IMX28_MMC, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_mmc_dt_ids);

static int mxs_mmc_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(mxs_mmc_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct mxs_mmc_host *host;
	struct mmc_host *mmc;
	struct resource *iores, *dmares;
	struct mxs_mmc_platform_data *pdata;
	struct pinctrl *pinctrl;
	int ret = 0, irq_err, irq_dma;
	dma_cap_mask_t mask;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dmares = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	irq_err = platform_get_irq(pdev, 0);
	irq_dma = platform_get_irq(pdev, 1);
	if (!iores || irq_err < 0 || irq_dma < 0)
		return -EINVAL;

	mmc = mmc_alloc_host(sizeof(struct mxs_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->base = devm_request_and_ioremap(&pdev->dev, iores);
	if (!host->base) {
		ret = -EADDRNOTAVAIL;
		goto out_mmc_free;
	}

	if (np) {
		host->devid = (enum mxs_mmc_id) of_id->data;
		/*
		 * TODO: This is a temporary solution and should be changed
		 * to use generic DMA binding later when the helpers get in.
		 */
		ret = of_property_read_u32(np, "fsl,ssp-dma-channel",
					   &host->dma_channel);
		if (ret) {
			dev_err(mmc_dev(host->mmc),
				"failed to get dma channel\n");
			goto out_mmc_free;
		}
	} else {
		host->devid = pdev->id_entry->driver_data;
		host->dma_channel = dmares->start;
	}

	host->mmc = mmc;
	host->sdio_irq_en = 0;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		ret = PTR_ERR(pinctrl);
		goto out_mmc_free;
	}

	host->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		goto out_mmc_free;
	}
	clk_prepare_enable(host->clk);

	mxs_mmc_reset(host);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	host->dma_data.chan_irq = irq_dma;
	host->dmach = dma_request_channel(mask, mxs_mmc_dma_filter, host);
	if (!host->dmach) {
		dev_err(mmc_dev(host->mmc),
			"%s: failed to request dma\n", __func__);
		goto out_clk_put;
	}

	/* set mmc core parameters */
	mmc->ops = &mxs_mmc_ops;
	mmc->caps = MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED |
		    MMC_CAP_SDIO_IRQ | MMC_CAP_NEEDS_POLL;

	pdata =	mmc_dev(host->mmc)->platform_data;
	if (!pdata) {
		u32 bus_width = 0;
		of_property_read_u32(np, "bus-width", &bus_width);
		if (bus_width == 4)
			mmc->caps |= MMC_CAP_4_BIT_DATA;
		else if (bus_width == 8)
			mmc->caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA;
		host->wp_gpio = of_get_named_gpio(np, "wp-gpios", 0);
	} else {
		if (pdata->flags & SLOTF_8_BIT_CAPABLE)
			mmc->caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA;
		if (pdata->flags & SLOTF_4_BIT_CAPABLE)
			mmc->caps |= MMC_CAP_4_BIT_DATA;
		host->wp_gpio = pdata->wp_gpio;
	}

	mmc->f_min = 400000;
	mmc->f_max = 288000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	mmc->max_segs = 52;
	mmc->max_blk_size = 1 << 0xf;
	mmc->max_blk_count = (ssp_is_old(host)) ? 0xff : 0xffffff;
	mmc->max_req_size = (ssp_is_old(host)) ? 0xffff : 0xffffffff;
	mmc->max_seg_size = dma_get_max_seg_size(host->dmach->device->dev);

	platform_set_drvdata(pdev, mmc);

	ret = devm_request_irq(&pdev->dev, irq_err, mxs_mmc_irq_handler, 0,
			       DRIVER_NAME, host);
	if (ret)
		goto out_free_dma;

	spin_lock_init(&host->lock);

	ret = mmc_add_host(mmc);
	if (ret)
		goto out_free_dma;

	dev_info(mmc_dev(host->mmc), "initialized\n");

	return 0;

out_free_dma:
	if (host->dmach)
		dma_release_channel(host->dmach);
out_clk_put:
	clk_disable_unprepare(host->clk);
	clk_put(host->clk);
out_mmc_free:
	mmc_free_host(mmc);
	return ret;
}

static int mxs_mmc_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct mxs_mmc_host *host = mmc_priv(mmc);

	mmc_remove_host(mmc);

	platform_set_drvdata(pdev, NULL);

	if (host->dmach)
		dma_release_channel(host->dmach);

	clk_disable_unprepare(host->clk);
	clk_put(host->clk);

	mmc_free_host(mmc);

	return 0;
}

#ifdef CONFIG_PM
static int mxs_mmc_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct mxs_mmc_host *host = mmc_priv(mmc);
	int ret = 0;

	ret = mmc_suspend_host(mmc);

	clk_disable_unprepare(host->clk);

	return ret;
}

static int mxs_mmc_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct mxs_mmc_host *host = mmc_priv(mmc);
	int ret = 0;

	clk_prepare_enable(host->clk);

	ret = mmc_resume_host(mmc);

	return ret;
}

static const struct dev_pm_ops mxs_mmc_pm_ops = {
	.suspend	= mxs_mmc_suspend,
	.resume		= mxs_mmc_resume,
};
#endif

static struct platform_driver mxs_mmc_driver = {
	.probe		= mxs_mmc_probe,
	.remove		= mxs_mmc_remove,
	.id_table	= mxs_mmc_ids,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &mxs_mmc_pm_ops,
		.of_match_table = mxs_mmc_dt_ids,
#endif
	},
};

module_platform_driver(mxs_mmc_driver);

MODULE_DESCRIPTION("FREESCALE MXS MMC peripheral");
MODULE_AUTHOR("Freescale Semiconductor");
MODULE_LICENSE("GPL");
