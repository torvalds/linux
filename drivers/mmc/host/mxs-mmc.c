// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Portions copyright (C) 2003 Russell King, PXA MMCI Driver
 * Portions copyright (C) 2004-2005 Pierre Ossman, W83L51xD SD/MMC driver
 *
 * Copyright 2008 Embedded Alley Solutions, Inc.
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dma/mxs-dma.h>
#include <linux/highmem.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/stmp_device.h>
#include <linux/spi/mxs-spi.h>

#define DRIVER_NAME	"mxs-mmc"

#define MXS_MMC_IRQ_BITS	(BM_SSP_CTRL1_SDIO_IRQ		| \
				 BM_SSP_CTRL1_RESP_ERR_IRQ	| \
				 BM_SSP_CTRL1_RESP_TIMEOUT_IRQ	| \
				 BM_SSP_CTRL1_DATA_TIMEOUT_IRQ	| \
				 BM_SSP_CTRL1_DATA_CRC_IRQ	| \
				 BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ	| \
				 BM_SSP_CTRL1_RECV_TIMEOUT_IRQ  | \
				 BM_SSP_CTRL1_FIFO_OVERRUN_IRQ)

/* card detect polling timeout */
#define MXS_MMC_DETECT_TIMEOUT			(HZ/2)

struct mxs_mmc_host {
	struct mxs_ssp			ssp;

	struct mmc_host			*mmc;
	struct mmc_request		*mrq;
	struct mmc_command		*cmd;
	struct mmc_data			*data;

	unsigned char			bus_width;
	spinlock_t			lock;
	int				sdio_irq_en;
	bool				broken_cd;
};

static int mxs_mmc_get_cd(struct mmc_host *mmc)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);
	struct mxs_ssp *ssp = &host->ssp;
	int present, ret;

	if (host->broken_cd)
		return -ENOSYS;

	ret = mmc_gpio_get_cd(mmc);
	if (ret >= 0)
		return ret;

	present = mmc->caps & MMC_CAP_NEEDS_POLL ||
		!(readl(ssp->base + HW_SSP_STATUS(ssp)) &
			BM_SSP_STATUS_CARD_DETECT);

	if (mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH)
		present = !present;

	return present;
}

static int mxs_mmc_reset(struct mxs_mmc_host *host)
{
	struct mxs_ssp *ssp = &host->ssp;
	u32 ctrl0, ctrl1;
	int ret;

	ret = stmp_reset_block(ssp->base);
	if (ret)
		return ret;

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
	       ssp->base + HW_SSP_TIMING(ssp));

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		ctrl1 |= BM_SSP_CTRL1_SDIO_IRQ_EN;
	}

	writel(ctrl0, ssp->base + HW_SSP_CTRL0);
	writel(ctrl1, ssp->base + HW_SSP_CTRL1(ssp));
	return 0;
}

static void mxs_mmc_start_cmd(struct mxs_mmc_host *host,
			      struct mmc_command *cmd);

static void mxs_mmc_request_done(struct mxs_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = host->data;
	struct mmc_request *mrq = host->mrq;
	struct mxs_ssp *ssp = &host->ssp;

	if (mmc_resp_type(cmd) & MMC_RSP_PRESENT) {
		if (mmc_resp_type(cmd) & MMC_RSP_136) {
			cmd->resp[3] = readl(ssp->base + HW_SSP_SDRESP0(ssp));
			cmd->resp[2] = readl(ssp->base + HW_SSP_SDRESP1(ssp));
			cmd->resp[1] = readl(ssp->base + HW_SSP_SDRESP2(ssp));
			cmd->resp[0] = readl(ssp->base + HW_SSP_SDRESP3(ssp));
		} else {
			cmd->resp[0] = readl(ssp->base + HW_SSP_SDRESP0(ssp));
		}
	}

	if (cmd == mrq->sbc) {
		/* Finished CMD23, now send actual command. */
		mxs_mmc_start_cmd(host, mrq->cmd);
		return;
	} else if (data) {
		dma_unmap_sg(mmc_dev(host->mmc), data->sg,
			     data->sg_len, ssp->dma_dir);
		/*
		 * If there was an error on any block, we mark all
		 * data blocks as being in error.
		 */
		if (!data->error)
			data->bytes_xfered = data->blocks * data->blksz;
		else
			data->bytes_xfered = 0;

		host->data = NULL;
		if (data->stop && (data->error || !mrq->sbc)) {
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
	struct mxs_ssp *ssp = &host->ssp;
	u32 stat;

	spin_lock(&host->lock);

	stat = readl(ssp->base + HW_SSP_CTRL1(ssp));
	writel(stat & MXS_MMC_IRQ_BITS,
	       ssp->base + HW_SSP_CTRL1(ssp) + STMP_OFFSET_REG_CLR);

	spin_unlock(&host->lock);

	if ((stat & BM_SSP_CTRL1_SDIO_IRQ) && (stat & BM_SSP_CTRL1_SDIO_IRQ_EN))
		mmc_signal_sdio_irq(host->mmc);

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
	struct mxs_ssp *ssp = &host->ssp;
	struct dma_async_tx_descriptor *desc;
	struct mmc_data *data = host->data;
	struct scatterlist * sgl;
	unsigned int sg_len;

	if (data) {
		/* data */
		dma_map_sg(mmc_dev(host->mmc), data->sg,
			   data->sg_len, ssp->dma_dir);
		sgl = data->sg;
		sg_len = data->sg_len;
	} else {
		/* pio */
		sgl = (struct scatterlist *) ssp->ssp_pio_words;
		sg_len = SSP_PIO_NUM;
	}

	desc = dmaengine_prep_slave_sg(ssp->dmach,
				sgl, sg_len, ssp->slave_dirn, flags);
	if (desc) {
		desc->callback = mxs_mmc_dma_irq_callback;
		desc->callback_param = host;
	} else {
		if (data)
			dma_unmap_sg(mmc_dev(host->mmc), data->sg,
				     data->sg_len, ssp->dma_dir);
	}

	return desc;
}

static void mxs_mmc_bc(struct mxs_mmc_host *host)
{
	struct mxs_ssp *ssp = &host->ssp;
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

	ssp->ssp_pio_words[0] = ctrl0;
	ssp->ssp_pio_words[1] = cmd0;
	ssp->ssp_pio_words[2] = cmd1;
	ssp->dma_dir = DMA_NONE;
	ssp->slave_dirn = DMA_TRANS_NONE;
	desc = mxs_mmc_prep_dma(host, MXS_DMA_CTRL_WAIT4END);
	if (!desc)
		goto out;

	dmaengine_submit(desc);
	dma_async_issue_pending(ssp->dmach);
	return;

out:
	dev_warn(mmc_dev(host->mmc),
		 "%s: failed to prep dma\n", __func__);
}

static void mxs_mmc_ac(struct mxs_mmc_host *host)
{
	struct mxs_ssp *ssp = &host->ssp;
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

	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		cmd0 |= BM_SSP_CMD0_APPEND_8CYC;

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		cmd0 |= BM_SSP_CMD0_CONT_CLKING_EN | BM_SSP_CMD0_SLOW_CLKING_EN;
	}

	ssp->ssp_pio_words[0] = ctrl0;
	ssp->ssp_pio_words[1] = cmd0;
	ssp->ssp_pio_words[2] = cmd1;
	ssp->dma_dir = DMA_NONE;
	ssp->slave_dirn = DMA_TRANS_NONE;
	desc = mxs_mmc_prep_dma(host, MXS_DMA_CTRL_WAIT4END);
	if (!desc)
		goto out;

	dmaengine_submit(desc);
	dma_async_issue_pending(ssp->dmach);
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
	unsigned int i;

	unsigned short dma_data_dir, timeout;
	enum dma_transfer_direction slave_dirn;
	unsigned int data_size = 0, log2_blksz;
	unsigned int blocks = data->blocks;

	struct mxs_ssp *ssp = &host->ssp;

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
	if (ssp_is_old(ssp)) {
		ctrl0 |= BF_SSP(data_size, CTRL0_XFER_COUNT);
		cmd0 |= BF_SSP(log2_blksz, CMD0_BLOCK_SIZE) |
			BF_SSP(blocks - 1, CMD0_BLOCK_COUNT);
	} else {
		writel(data_size, ssp->base + HW_SSP_XFER_SIZE);
		writel(BF_SSP(log2_blksz, BLOCK_SIZE_BLOCK_SIZE) |
		       BF_SSP(blocks - 1, BLOCK_SIZE_BLOCK_COUNT),
		       ssp->base + HW_SSP_BLOCK_SIZE);
	}

	if (cmd->opcode == SD_IO_RW_EXTENDED)
		cmd0 |= BM_SSP_CMD0_APPEND_8CYC;

	cmd1 = cmd->arg;

	if (host->sdio_irq_en) {
		ctrl0 |= BM_SSP_CTRL0_SDIO_IRQ_CHECK;
		cmd0 |= BM_SSP_CMD0_CONT_CLKING_EN | BM_SSP_CMD0_SLOW_CLKING_EN;
	}

	/* set the timeout count */
	timeout = mxs_ns_to_ssp_ticks(ssp->clk_rate, data->timeout_ns);
	val = readl(ssp->base + HW_SSP_TIMING(ssp));
	val &= ~(BM_SSP_TIMING_TIMEOUT);
	val |= BF_SSP(timeout, TIMING_TIMEOUT);
	writel(val, ssp->base + HW_SSP_TIMING(ssp));

	/* pio */
	ssp->ssp_pio_words[0] = ctrl0;
	ssp->ssp_pio_words[1] = cmd0;
	ssp->ssp_pio_words[2] = cmd1;
	ssp->dma_dir = DMA_NONE;
	ssp->slave_dirn = DMA_TRANS_NONE;
	desc = mxs_mmc_prep_dma(host, 0);
	if (!desc)
		goto out;

	/* append data sg */
	WARN_ON(host->data != NULL);
	host->data = data;
	ssp->dma_dir = dma_data_dir;
	ssp->slave_dirn = slave_dirn;
	desc = mxs_mmc_prep_dma(host, DMA_PREP_INTERRUPT | MXS_DMA_CTRL_WAIT4END);
	if (!desc)
		goto out;

	dmaengine_submit(desc);
	dma_async_issue_pending(ssp->dmach);
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

	if (mrq->sbc)
		mxs_mmc_start_cmd(host, mrq->sbc);
	else
		mxs_mmc_start_cmd(host, mrq->cmd);
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
		mxs_ssp_set_clk_rate(&host->ssp, ios->clock);
}

static void mxs_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct mxs_mmc_host *host = mmc_priv(mmc);
	struct mxs_ssp *ssp = &host->ssp;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	host->sdio_irq_en = enable;

	if (enable) {
		writel(BM_SSP_CTRL0_SDIO_IRQ_CHECK,
		       ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_SET);
		writel(BM_SSP_CTRL1_SDIO_IRQ_EN,
		       ssp->base + HW_SSP_CTRL1(ssp) + STMP_OFFSET_REG_SET);
	} else {
		writel(BM_SSP_CTRL0_SDIO_IRQ_CHECK,
		       ssp->base + HW_SSP_CTRL0 + STMP_OFFSET_REG_CLR);
		writel(BM_SSP_CTRL1_SDIO_IRQ_EN,
		       ssp->base + HW_SSP_CTRL1(ssp) + STMP_OFFSET_REG_CLR);
	}

	spin_unlock_irqrestore(&host->lock, flags);

	if (enable && readl(ssp->base + HW_SSP_STATUS(ssp)) &
			BM_SSP_STATUS_SDIO_IRQ)
		mmc_signal_sdio_irq(host->mmc);

}

static const struct mmc_host_ops mxs_mmc_ops = {
	.request = mxs_mmc_request,
	.get_ro = mmc_gpio_get_ro,
	.get_cd = mxs_mmc_get_cd,
	.set_ios = mxs_mmc_set_ios,
	.enable_sdio_irq = mxs_mmc_enable_sdio_irq,
};

static const struct of_device_id mxs_mmc_dt_ids[] = {
	{ .compatible = "fsl,imx23-mmc", .data = (void *) IMX23_SSP, },
	{ .compatible = "fsl,imx28-mmc", .data = (void *) IMX28_SSP, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_mmc_dt_ids);

static int mxs_mmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mxs_mmc_host *host;
	struct mmc_host *mmc;
	int ret = 0, irq_err;
	struct regulator *reg_vmmc;
	struct mxs_ssp *ssp;

	irq_err = platform_get_irq(pdev, 0);
	if (irq_err < 0)
		return irq_err;

	mmc = mmc_alloc_host(sizeof(struct mxs_mmc_host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	ssp = &host->ssp;
	ssp->dev = &pdev->dev;
	ssp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ssp->base)) {
		ret = PTR_ERR(ssp->base);
		goto out_mmc_free;
	}

	ssp->devid = (enum mxs_ssp_id)of_device_get_match_data(&pdev->dev);

	host->mmc = mmc;
	host->sdio_irq_en = 0;

	reg_vmmc = devm_regulator_get(&pdev->dev, "vmmc");
	if (!IS_ERR(reg_vmmc)) {
		ret = regulator_enable(reg_vmmc);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to enable vmmc regulator: %d\n", ret);
			goto out_mmc_free;
		}
	}

	ssp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ssp->clk)) {
		ret = PTR_ERR(ssp->clk);
		goto out_mmc_free;
	}
	ret = clk_prepare_enable(ssp->clk);
	if (ret)
		goto out_mmc_free;

	ret = mxs_mmc_reset(host);
	if (ret) {
		dev_err(&pdev->dev, "Failed to reset mmc: %d\n", ret);
		goto out_clk_disable;
	}

	ssp->dmach = dma_request_chan(&pdev->dev, "rx-tx");
	if (IS_ERR(ssp->dmach)) {
		dev_err(mmc_dev(host->mmc),
			"%s: failed to request dma\n", __func__);
		ret = PTR_ERR(ssp->dmach);
		goto out_clk_disable;
	}

	/* set mmc core parameters */
	mmc->ops = &mxs_mmc_ops;
	mmc->caps = MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED |
		    MMC_CAP_SDIO_IRQ | MMC_CAP_NEEDS_POLL | MMC_CAP_CMD23;

	host->broken_cd = of_property_read_bool(np, "broken-cd");

	mmc->f_min = 400000;
	mmc->f_max = 288000000;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto out_clk_disable;

	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	mmc->max_segs = 52;
	mmc->max_blk_size = 1 << 0xf;
	mmc->max_blk_count = (ssp_is_old(ssp)) ? 0xff : 0xffffff;
	mmc->max_req_size = (ssp_is_old(ssp)) ? 0xffff : 0xffffffff;
	mmc->max_seg_size = dma_get_max_seg_size(ssp->dmach->device->dev);

	platform_set_drvdata(pdev, mmc);

	spin_lock_init(&host->lock);

	ret = devm_request_irq(&pdev->dev, irq_err, mxs_mmc_irq_handler, 0,
			       dev_name(&pdev->dev), host);
	if (ret)
		goto out_free_dma;

	ret = mmc_add_host(mmc);
	if (ret)
		goto out_free_dma;

	dev_info(mmc_dev(host->mmc), "initialized\n");

	return 0;

out_free_dma:
	dma_release_channel(ssp->dmach);
out_clk_disable:
	clk_disable_unprepare(ssp->clk);
out_mmc_free:
	mmc_free_host(mmc);
	return ret;
}

static int mxs_mmc_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct mxs_mmc_host *host = mmc_priv(mmc);
	struct mxs_ssp *ssp = &host->ssp;

	mmc_remove_host(mmc);

	if (ssp->dmach)
		dma_release_channel(ssp->dmach);

	clk_disable_unprepare(ssp->clk);

	mmc_free_host(mmc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mxs_mmc_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct mxs_mmc_host *host = mmc_priv(mmc);
	struct mxs_ssp *ssp = &host->ssp;

	clk_disable_unprepare(ssp->clk);
	return 0;
}

static int mxs_mmc_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct mxs_mmc_host *host = mmc_priv(mmc);
	struct mxs_ssp *ssp = &host->ssp;

	return clk_prepare_enable(ssp->clk);
}
#endif

static SIMPLE_DEV_PM_OPS(mxs_mmc_pm_ops, mxs_mmc_suspend, mxs_mmc_resume);

static struct platform_driver mxs_mmc_driver = {
	.probe		= mxs_mmc_probe,
	.remove		= mxs_mmc_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm	= &mxs_mmc_pm_ops,
		.of_match_table = mxs_mmc_dt_ids,
	},
};

module_platform_driver(mxs_mmc_driver);

MODULE_DESCRIPTION("FREESCALE MXS MMC peripheral");
MODULE_AUTHOR("Freescale Semiconductor");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
