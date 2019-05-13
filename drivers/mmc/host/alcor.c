// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oleksij Rempel <linux@rempel-privat.de>
 *
 * Driver for Alcor Micro AU6601 and AU6621 controllers
 */

/* Note: this driver was created without any documentation. Based
 * on sniffing, testing and in some cases mimic of original driver.
 * As soon as some one with documentation or more experience in SD/MMC, or
 * reverse engineering then me, please review this driver and question every
 * thing what I did. 2018 Oleksij Rempel <linux@rempel-privat.de>
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>

#include <linux/alcor_pci.h>

enum alcor_cookie {
	COOKIE_UNMAPPED,
	COOKIE_PRE_MAPPED,
	COOKIE_MAPPED,
};

struct alcor_pll_conf {
	unsigned int clk_src_freq;
	unsigned int clk_src_reg;
	unsigned int min_div;
	unsigned int max_div;
};

struct alcor_sdmmc_host {
	struct  device *dev;
	struct alcor_pci_priv *alcor_pci;

	struct mmc_host *mmc;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;
	unsigned int dma_on:1;

	struct mutex cmd_mutex;

	struct delayed_work timeout_work;

	struct sg_mapping_iter sg_miter;	/* SG state for PIO */
	struct scatterlist *sg;
	unsigned int blocks;		/* remaining PIO blocks */
	int sg_count;

	u32			irq_status_sd;
	unsigned char		cur_power_mode;
};

static const struct alcor_pll_conf alcor_pll_cfg[] = {
	/* MHZ,		CLK src,		max div, min div */
	{ 31250000,	AU6601_CLK_31_25_MHZ,	1,	511},
	{ 48000000,	AU6601_CLK_48_MHZ,	1,	511},
	{125000000,	AU6601_CLK_125_MHZ,	1,	511},
	{384000000,	AU6601_CLK_384_MHZ,	1,	511},
};

static inline void alcor_rmw8(struct alcor_sdmmc_host *host, unsigned int addr,
			       u8 clear, u8 set)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	u32 var;

	var = alcor_read8(priv, addr);
	var &= ~clear;
	var |= set;
	alcor_write8(priv, var, addr);
}

/* As soon as irqs are masked, some status updates may be missed.
 * Use this with care.
 */
static inline void alcor_mask_sd_irqs(struct alcor_sdmmc_host *host)
{
	struct alcor_pci_priv *priv = host->alcor_pci;

	alcor_write32(priv, 0, AU6601_REG_INT_ENABLE);
}

static inline void alcor_unmask_sd_irqs(struct alcor_sdmmc_host *host)
{
	struct alcor_pci_priv *priv = host->alcor_pci;

	alcor_write32(priv, AU6601_INT_CMD_MASK | AU6601_INT_DATA_MASK |
		  AU6601_INT_CARD_INSERT | AU6601_INT_CARD_REMOVE |
		  AU6601_INT_OVER_CURRENT_ERR,
		  AU6601_REG_INT_ENABLE);
}

static void alcor_reset(struct alcor_sdmmc_host *host, u8 val)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	int i;

	alcor_write8(priv, val | AU6601_BUF_CTRL_RESET,
		      AU6601_REG_SW_RESET);
	for (i = 0; i < 100; i++) {
		if (!(alcor_read8(priv, AU6601_REG_SW_RESET) & val))
			return;
		udelay(50);
	}
	dev_err(host->dev, "%s: timeout\n", __func__);
}

static void alcor_data_set_dma(struct alcor_sdmmc_host *host)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	u32 addr;

	if (!host->sg_count)
		return;

	if (!host->sg) {
		dev_err(host->dev, "have blocks, but no SG\n");
		return;
	}

	if (!sg_dma_len(host->sg)) {
		dev_err(host->dev, "DMA SG len == 0\n");
		return;
	}


	addr = (u32)sg_dma_address(host->sg);

	alcor_write32(priv, addr, AU6601_REG_SDMA_ADDR);
	host->sg = sg_next(host->sg);
	host->sg_count--;
}

static void alcor_trigger_data_transfer(struct alcor_sdmmc_host *host)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	struct mmc_data *data = host->data;
	u8 ctrl = 0;

	if (data->flags & MMC_DATA_WRITE)
		ctrl |= AU6601_DATA_WRITE;

	if (data->host_cookie == COOKIE_MAPPED) {
		alcor_data_set_dma(host);
		ctrl |= AU6601_DATA_DMA_MODE;
		host->dma_on = 1;
		alcor_write32(priv, data->sg_count * 0x1000,
			       AU6601_REG_BLOCK_SIZE);
	} else {
		alcor_write32(priv, data->blksz, AU6601_REG_BLOCK_SIZE);
	}

	alcor_write8(priv, ctrl | AU6601_DATA_START_XFER,
		      AU6601_DATA_XFER_CTRL);
}

static void alcor_trf_block_pio(struct alcor_sdmmc_host *host, bool read)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	size_t blksize, len;
	u8 *buf;

	if (!host->blocks)
		return;

	if (host->dma_on) {
		dev_err(host->dev, "configured DMA but got PIO request.\n");
		return;
	}

	if (!!(host->data->flags & MMC_DATA_READ) != read) {
		dev_err(host->dev, "got unexpected direction %i != %i\n",
			!!(host->data->flags & MMC_DATA_READ), read);
	}

	if (!sg_miter_next(&host->sg_miter))
		return;

	blksize = host->data->blksz;
	len = min(host->sg_miter.length, blksize);

	dev_dbg(host->dev, "PIO, %s block size: 0x%zx\n",
		read ? "read" : "write", blksize);

	host->sg_miter.consumed = len;
	host->blocks--;

	buf = host->sg_miter.addr;

	if (read)
		ioread32_rep(priv->iobase + AU6601_REG_BUFFER, buf, len >> 2);
	else
		iowrite32_rep(priv->iobase + AU6601_REG_BUFFER, buf, len >> 2);

	sg_miter_stop(&host->sg_miter);
}

static void alcor_prepare_sg_miter(struct alcor_sdmmc_host *host)
{
	unsigned int flags = SG_MITER_ATOMIC;
	struct mmc_data *data = host->data;

	if (data->flags & MMC_DATA_READ)
		flags |= SG_MITER_TO_SG;
	else
		flags |= SG_MITER_FROM_SG;
	sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
}

static void alcor_prepare_data(struct alcor_sdmmc_host *host,
			       struct mmc_command *cmd)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	struct mmc_data *data = cmd->data;

	if (!data)
		return;


	host->data = data;
	host->data->bytes_xfered = 0;
	host->blocks = data->blocks;
	host->sg = data->sg;
	host->sg_count = data->sg_count;
	dev_dbg(host->dev, "prepare DATA: sg %i, blocks: %i\n",
			host->sg_count, host->blocks);

	if (data->host_cookie != COOKIE_MAPPED)
		alcor_prepare_sg_miter(host);

	alcor_write8(priv, 0, AU6601_DATA_XFER_CTRL);
}

static void alcor_send_cmd(struct alcor_sdmmc_host *host,
			   struct mmc_command *cmd, bool set_timeout)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	unsigned long timeout = 0;
	u8 ctrl = 0;

	host->cmd = cmd;
	alcor_prepare_data(host, cmd);

	dev_dbg(host->dev, "send CMD. opcode: 0x%02x, arg; 0x%08x\n",
		cmd->opcode, cmd->arg);
	alcor_write8(priv, cmd->opcode | 0x40, AU6601_REG_CMD_OPCODE);
	alcor_write32be(priv, cmd->arg, AU6601_REG_CMD_ARG);

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		ctrl = AU6601_CMD_NO_RESP;
		break;
	case MMC_RSP_R1:
		ctrl = AU6601_CMD_6_BYTE_CRC;
		break;
	case MMC_RSP_R1B:
		ctrl = AU6601_CMD_6_BYTE_CRC | AU6601_CMD_STOP_WAIT_RDY;
		break;
	case MMC_RSP_R2:
		ctrl = AU6601_CMD_17_BYTE_CRC;
		break;
	case MMC_RSP_R3:
		ctrl = AU6601_CMD_6_BYTE_WO_CRC;
		break;
	default:
		dev_err(host->dev, "%s: cmd->flag (0x%02x) is not valid\n",
			mmc_hostname(host->mmc), mmc_resp_type(cmd));
		break;
	}

	if (set_timeout) {
		if (!cmd->data && cmd->busy_timeout)
			timeout = cmd->busy_timeout;
		else
			timeout = 10000;

		schedule_delayed_work(&host->timeout_work,
				      msecs_to_jiffies(timeout));
	}

	dev_dbg(host->dev, "xfer ctrl: 0x%02x; timeout: %lu\n", ctrl, timeout);
	alcor_write8(priv, ctrl | AU6601_CMD_START_XFER,
				 AU6601_CMD_XFER_CTRL);
}

static void alcor_request_complete(struct alcor_sdmmc_host *host,
				   bool cancel_timeout)
{
	struct mmc_request *mrq;

	/*
	 * If this work gets rescheduled while running, it will
	 * be run again afterwards but without any active request.
	 */
	if (!host->mrq)
		return;

	if (cancel_timeout)
		cancel_delayed_work(&host->timeout_work);

	mrq = host->mrq;

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;
	host->dma_on = 0;

	mmc_request_done(host->mmc, mrq);
}

static void alcor_finish_data(struct alcor_sdmmc_host *host)
{
	struct mmc_data *data;

	data = host->data;
	host->data = NULL;
	host->dma_on = 0;

	/*
	 * The specification states that the block count register must
	 * be updated, but it does not specify at what point in the
	 * data flow. That makes the register entirely useless to read
	 * back so we have to assume that nothing made it to the card
	 * in the event of an error.
	 */
	if (data->error)
		data->bytes_xfered = 0;
	else
		data->bytes_xfered = data->blksz * data->blocks;

	/*
	 * Need to send CMD12 if -
	 * a) open-ended multiblock transfer (no CMD23)
	 * b) error in multiblock transfer
	 */
	if (data->stop &&
	    (data->error ||
	     !host->mrq->sbc)) {

		/*
		 * The controller needs a reset of internal state machines
		 * upon error conditions.
		 */
		if (data->error)
			alcor_reset(host, AU6601_RESET_CMD | AU6601_RESET_DATA);

		alcor_unmask_sd_irqs(host);
		alcor_send_cmd(host, data->stop, false);
		return;
	}

	alcor_request_complete(host, 1);
}

static void alcor_err_irq(struct alcor_sdmmc_host *host, u32 intmask)
{
	dev_dbg(host->dev, "ERR IRQ %x\n", intmask);

	if (host->cmd) {
		if (intmask & AU6601_INT_CMD_TIMEOUT_ERR)
			host->cmd->error = -ETIMEDOUT;
		else
			host->cmd->error = -EILSEQ;
	}

	if (host->data) {
		if (intmask & AU6601_INT_DATA_TIMEOUT_ERR)
			host->data->error = -ETIMEDOUT;
		else
			host->data->error = -EILSEQ;

		host->data->bytes_xfered = 0;
	}

	alcor_reset(host, AU6601_RESET_CMD | AU6601_RESET_DATA);
	alcor_request_complete(host, 1);
}

static int alcor_cmd_irq_done(struct alcor_sdmmc_host *host, u32 intmask)
{
	struct alcor_pci_priv *priv = host->alcor_pci;

	intmask &= AU6601_INT_CMD_END;

	if (!intmask)
		return true;

	/* got CMD_END but no CMD is in progress, wake thread an process the
	 * error
	 */
	if (!host->cmd)
		return false;

	if (host->cmd->flags & MMC_RSP_PRESENT) {
		struct mmc_command *cmd = host->cmd;

		cmd->resp[0] = alcor_read32be(priv, AU6601_REG_CMD_RSP0);
		dev_dbg(host->dev, "RSP0: 0x%04x\n", cmd->resp[0]);
		if (host->cmd->flags & MMC_RSP_136) {
			cmd->resp[1] =
				alcor_read32be(priv, AU6601_REG_CMD_RSP1);
			cmd->resp[2] =
				alcor_read32be(priv, AU6601_REG_CMD_RSP2);
			cmd->resp[3] =
				alcor_read32be(priv, AU6601_REG_CMD_RSP3);
			dev_dbg(host->dev, "RSP1,2,3: 0x%04x 0x%04x 0x%04x\n",
				cmd->resp[1], cmd->resp[2], cmd->resp[3]);
		}

	}

	host->cmd->error = 0;

	/* Processed actual command. */
	if (!host->data)
		return false;

	alcor_trigger_data_transfer(host);
	host->cmd = NULL;
	return true;
}

static void alcor_cmd_irq_thread(struct alcor_sdmmc_host *host, u32 intmask)
{
	intmask &= AU6601_INT_CMD_END;

	if (!intmask)
		return;

	if (!host->cmd && intmask & AU6601_INT_CMD_END) {
		dev_dbg(host->dev, "Got command interrupt 0x%08x even though no command operation was in progress.\n",
			intmask);
	}

	/* Processed actual command. */
	if (!host->data)
		alcor_request_complete(host, 1);
	else
		alcor_trigger_data_transfer(host);
	host->cmd = NULL;
}

static int alcor_data_irq_done(struct alcor_sdmmc_host *host, u32 intmask)
{
	u32 tmp;

	intmask &= AU6601_INT_DATA_MASK;

	/* nothing here to do */
	if (!intmask)
		return 1;

	/* we was too fast and got DATA_END after it was processed?
	 * lets ignore it for now.
	 */
	if (!host->data && intmask == AU6601_INT_DATA_END)
		return 1;

	/* looks like an error, so lets handle it. */
	if (!host->data)
		return 0;

	tmp = intmask & (AU6601_INT_READ_BUF_RDY | AU6601_INT_WRITE_BUF_RDY
			 | AU6601_INT_DMA_END);
	switch (tmp) {
	case 0:
		break;
	case AU6601_INT_READ_BUF_RDY:
		alcor_trf_block_pio(host, true);
		return 1;
	case AU6601_INT_WRITE_BUF_RDY:
		alcor_trf_block_pio(host, false);
		return 1;
	case AU6601_INT_DMA_END:
		if (!host->sg_count)
			break;

		alcor_data_set_dma(host);
		break;
	default:
		dev_err(host->dev, "Got READ_BUF_RDY and WRITE_BUF_RDY at same time\n");
		break;
	}

	if (intmask & AU6601_INT_DATA_END) {
		if (!host->dma_on && host->blocks) {
			alcor_trigger_data_transfer(host);
			return 1;
		} else {
			return 0;
		}
	}

	return 1;
}

static void alcor_data_irq_thread(struct alcor_sdmmc_host *host, u32 intmask)
{
	intmask &= AU6601_INT_DATA_MASK;

	if (!intmask)
		return;

	if (!host->data) {
		dev_dbg(host->dev, "Got data interrupt 0x%08x even though no data operation was in progress.\n",
			intmask);
		alcor_reset(host, AU6601_RESET_DATA);
		return;
	}

	if (alcor_data_irq_done(host, intmask))
		return;

	if ((intmask & AU6601_INT_DATA_END) || !host->blocks ||
	    (host->dma_on && !host->sg_count))
		alcor_finish_data(host);
}

static void alcor_cd_irq(struct alcor_sdmmc_host *host, u32 intmask)
{
	dev_dbg(host->dev, "card %s\n",
		intmask & AU6601_INT_CARD_REMOVE ? "removed" : "inserted");

	if (host->mrq) {
		dev_dbg(host->dev, "cancel all pending tasks.\n");

		if (host->data)
			host->data->error = -ENOMEDIUM;

		if (host->cmd)
			host->cmd->error = -ENOMEDIUM;
		else
			host->mrq->cmd->error = -ENOMEDIUM;

		alcor_request_complete(host, 1);
	}

	mmc_detect_change(host->mmc, msecs_to_jiffies(1));
}

static irqreturn_t alcor_irq_thread(int irq, void *d)
{
	struct alcor_sdmmc_host *host = d;
	irqreturn_t ret = IRQ_HANDLED;
	u32 intmask, tmp;

	mutex_lock(&host->cmd_mutex);

	intmask = host->irq_status_sd;

	/* some thing bad */
	if (unlikely(!intmask || AU6601_INT_ALL_MASK == intmask)) {
		dev_dbg(host->dev, "unexpected IRQ: 0x%04x\n", intmask);
		ret = IRQ_NONE;
		goto exit;
	}

	tmp = intmask & (AU6601_INT_CMD_MASK | AU6601_INT_DATA_MASK);
	if (tmp) {
		if (tmp & AU6601_INT_ERROR_MASK)
			alcor_err_irq(host, tmp);
		else {
			alcor_cmd_irq_thread(host, tmp);
			alcor_data_irq_thread(host, tmp);
		}
		intmask &= ~(AU6601_INT_CMD_MASK | AU6601_INT_DATA_MASK);
	}

	if (intmask & (AU6601_INT_CARD_INSERT | AU6601_INT_CARD_REMOVE)) {
		alcor_cd_irq(host, intmask);
		intmask &= ~(AU6601_INT_CARD_INSERT | AU6601_INT_CARD_REMOVE);
	}

	if (intmask & AU6601_INT_OVER_CURRENT_ERR) {
		dev_warn(host->dev,
			 "warning: over current detected!\n");
		intmask &= ~AU6601_INT_OVER_CURRENT_ERR;
	}

	if (intmask)
		dev_dbg(host->dev, "got not handled IRQ: 0x%04x\n", intmask);

exit:
	mutex_unlock(&host->cmd_mutex);
	alcor_unmask_sd_irqs(host);
	return ret;
}


static irqreturn_t alcor_irq(int irq, void *d)
{
	struct alcor_sdmmc_host *host = d;
	struct alcor_pci_priv *priv = host->alcor_pci;
	u32 status, tmp;
	irqreturn_t ret;
	int cmd_done, data_done;

	status = alcor_read32(priv, AU6601_REG_INT_STATUS);
	if (!status)
		return IRQ_NONE;

	alcor_write32(priv, status, AU6601_REG_INT_STATUS);

	tmp = status & (AU6601_INT_READ_BUF_RDY | AU6601_INT_WRITE_BUF_RDY
			| AU6601_INT_DATA_END | AU6601_INT_DMA_END
			| AU6601_INT_CMD_END);
	if (tmp == status) {
		cmd_done = alcor_cmd_irq_done(host, tmp);
		data_done = alcor_data_irq_done(host, tmp);
		/* use fast path for simple tasks */
		if (cmd_done && data_done) {
			ret = IRQ_HANDLED;
			goto alcor_irq_done;
		}
	}

	host->irq_status_sd = status;
	ret = IRQ_WAKE_THREAD;
	alcor_mask_sd_irqs(host);
alcor_irq_done:
	return ret;
}

static void alcor_set_clock(struct alcor_sdmmc_host *host, unsigned int clock)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	int i, diff = 0x7fffffff, tmp_clock = 0;
	u16 clk_src = 0;
	u8 clk_div = 0;

	if (clock == 0) {
		alcor_write16(priv, 0, AU6601_CLK_SELECT);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(alcor_pll_cfg); i++) {
		unsigned int tmp_div, tmp_diff;
		const struct alcor_pll_conf *cfg = &alcor_pll_cfg[i];

		tmp_div = DIV_ROUND_UP(cfg->clk_src_freq, clock);
		if (cfg->min_div > tmp_div || tmp_div > cfg->max_div)
			continue;

		tmp_clock = DIV_ROUND_UP(cfg->clk_src_freq, tmp_div);
		tmp_diff = abs(clock - tmp_clock);

		if (tmp_diff >= 0 && tmp_diff < diff) {
			diff = tmp_diff;
			clk_src = cfg->clk_src_reg;
			clk_div = tmp_div;
		}
	}

	clk_src |= ((clk_div - 1) << 8);
	clk_src |= AU6601_CLK_ENABLE;

	dev_dbg(host->dev, "set freq %d cal freq %d, use div %d, mod %x\n",
			clock, tmp_clock, clk_div, clk_src);

	alcor_write16(priv, clk_src, AU6601_CLK_SELECT);

}

static void alcor_set_timing(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);

	if (ios->timing == MMC_TIMING_LEGACY) {
		alcor_rmw8(host, AU6601_CLK_DELAY,
			    AU6601_CLK_POSITIVE_EDGE_ALL, 0);
	} else {
		alcor_rmw8(host, AU6601_CLK_DELAY,
			    0, AU6601_CLK_POSITIVE_EDGE_ALL);
	}
}

static void alcor_set_bus_width(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct alcor_pci_priv *priv = host->alcor_pci;

	if (ios->bus_width == MMC_BUS_WIDTH_1) {
		alcor_write8(priv, 0, AU6601_REG_BUS_CTRL);
	} else if (ios->bus_width == MMC_BUS_WIDTH_4) {
		alcor_write8(priv, AU6601_BUS_WIDTH_4BIT,
			      AU6601_REG_BUS_CTRL);
	} else
		dev_err(host->dev, "Unknown BUS mode\n");

}

static int alcor_card_busy(struct mmc_host *mmc)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct alcor_pci_priv *priv = host->alcor_pci;
	u8 status;

	/* Check whether dat[0:3] low */
	status = alcor_read8(priv, AU6601_DATA_PIN_STATE);

	return !(status & AU6601_BUS_STAT_DAT_MASK);
}

static int alcor_get_cd(struct mmc_host *mmc)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct alcor_pci_priv *priv = host->alcor_pci;
	u8 detect;

	detect = alcor_read8(priv, AU6601_DETECT_STATUS)
		& AU6601_DETECT_STATUS_M;
	/* check if card is present then send command and data */
	return (detect == AU6601_SD_DETECTED);
}

static int alcor_get_ro(struct mmc_host *mmc)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct alcor_pci_priv *priv = host->alcor_pci;
	u8 status;

	/* get write protect pin status */
	status = alcor_read8(priv, AU6601_INTERFACE_MODE_CTRL);

	return !!(status & AU6601_SD_CARD_WP);
}

static void alcor_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);

	mutex_lock(&host->cmd_mutex);

	host->mrq = mrq;

	/* check if card is present then send command and data */
	if (alcor_get_cd(mmc))
		alcor_send_cmd(host, mrq->cmd, true);
	else {
		mrq->cmd->error = -ENOMEDIUM;
		alcor_request_complete(host, 1);
	}

	mutex_unlock(&host->cmd_mutex);
}

static void alcor_pre_req(struct mmc_host *mmc,
			   struct mmc_request *mrq)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;
	struct mmc_command *cmd = mrq->cmd;
	struct scatterlist *sg;
	unsigned int i, sg_len;

	if (!data || !cmd)
		return;

	data->host_cookie = COOKIE_UNMAPPED;

	/* FIXME: looks like the DMA engine works only with CMD18 */
	if (cmd->opcode != 18)
		return;
	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < AU6601_MAX_DMA_BLOCK_SIZE)
		return;

	if (data->blksz & 3)
		return;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->length != AU6601_MAX_DMA_BLOCK_SIZE)
			return;
	}

	/* This data might be unmapped at this time */

	sg_len = dma_map_sg(host->dev, data->sg, data->sg_len,
			    mmc_get_dma_dir(data));
	if (sg_len)
		data->host_cookie = COOKIE_MAPPED;

	data->sg_count = sg_len;
}

static void alcor_post_req(struct mmc_host *mmc,
			    struct mmc_request *mrq,
			    int err)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!data)
		return;

	if (data->host_cookie == COOKIE_MAPPED) {
		dma_unmap_sg(host->dev,
			     data->sg,
			     data->sg_len,
			     mmc_get_dma_dir(data));
	}

	data->host_cookie = COOKIE_UNMAPPED;
}

static void alcor_set_power_mode(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);
	struct alcor_pci_priv *priv = host->alcor_pci;

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		alcor_set_clock(host, ios->clock);
		/* set all pins to input */
		alcor_write8(priv, 0, AU6601_OUTPUT_ENABLE);
		/* turn of VDD */
		alcor_write8(priv, 0, AU6601_POWER_CONTROL);
		break;
	case MMC_POWER_UP:
		break;
	case MMC_POWER_ON:
		/* This is most trickiest part. The order and timings of
		 * instructions seems to play important role. Any changes may
		 * confuse internal state engine if this HW.
		 * FIXME: If we will ever get access to documentation, then this
		 * part should be reviewed again.
		 */

		/* enable SD card mode */
		alcor_write8(priv, AU6601_SD_CARD,
			      AU6601_ACTIVE_CTRL);
		/* set signal voltage to 3.3V */
		alcor_write8(priv, 0, AU6601_OPT);
		/* no documentation about clk delay, for now just try to mimic
		 * original driver.
		 */
		alcor_write8(priv, 0x20, AU6601_CLK_DELAY);
		/* set BUS width to 1 bit */
		alcor_write8(priv, 0, AU6601_REG_BUS_CTRL);
		/* set CLK first time */
		alcor_set_clock(host, ios->clock);
		/* power on VDD */
		alcor_write8(priv, AU6601_SD_CARD,
			      AU6601_POWER_CONTROL);
		/* wait until the CLK will get stable */
		mdelay(20);
		/* set CLK again, mimic original driver. */
		alcor_set_clock(host, ios->clock);

		/* enable output */
		alcor_write8(priv, AU6601_SD_CARD,
			      AU6601_OUTPUT_ENABLE);
		/* The clk will not work on au6621. We need to trigger data
		 * transfer.
		 */
		alcor_write8(priv, AU6601_DATA_WRITE,
			      AU6601_DATA_XFER_CTRL);
		/* configure timeout. Not clear what exactly it means. */
		alcor_write8(priv, 0x7d, AU6601_TIME_OUT_CTRL);
		mdelay(100);
		break;
	default:
		dev_err(host->dev, "Unknown power parameter\n");
	}
}

static void alcor_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);

	mutex_lock(&host->cmd_mutex);

	dev_dbg(host->dev, "set ios. bus width: %x, power mode: %x\n",
		ios->bus_width, ios->power_mode);

	if (ios->power_mode != host->cur_power_mode) {
		alcor_set_power_mode(mmc, ios);
		host->cur_power_mode = ios->power_mode;
	} else {
		alcor_set_timing(mmc, ios);
		alcor_set_bus_width(mmc, ios);
		alcor_set_clock(host, ios->clock);
	}

	mutex_unlock(&host->cmd_mutex);
}

static int alcor_signal_voltage_switch(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	struct alcor_sdmmc_host *host = mmc_priv(mmc);

	mutex_lock(&host->cmd_mutex);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		alcor_rmw8(host, AU6601_OPT, AU6601_OPT_SD_18V, 0);
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		alcor_rmw8(host, AU6601_OPT, 0, AU6601_OPT_SD_18V);
		break;
	default:
		/* No signal voltage switch required */
		break;
	}

	mutex_unlock(&host->cmd_mutex);
	return 0;
}

static const struct mmc_host_ops alcor_sdc_ops = {
	.card_busy	= alcor_card_busy,
	.get_cd		= alcor_get_cd,
	.get_ro		= alcor_get_ro,
	.post_req	= alcor_post_req,
	.pre_req	= alcor_pre_req,
	.request	= alcor_request,
	.set_ios	= alcor_set_ios,
	.start_signal_voltage_switch = alcor_signal_voltage_switch,
};

static void alcor_timeout_timer(struct work_struct *work)
{
	struct delayed_work *d = to_delayed_work(work);
	struct alcor_sdmmc_host *host = container_of(d, struct alcor_sdmmc_host,
						timeout_work);
	mutex_lock(&host->cmd_mutex);

	dev_dbg(host->dev, "triggered timeout\n");
	if (host->mrq) {
		dev_err(host->dev, "Timeout waiting for hardware interrupt.\n");

		if (host->data) {
			host->data->error = -ETIMEDOUT;
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->mrq->cmd->error = -ETIMEDOUT;
		}

		alcor_reset(host, AU6601_RESET_CMD | AU6601_RESET_DATA);
		alcor_request_complete(host, 0);
	}

	mutex_unlock(&host->cmd_mutex);
}

static void alcor_hw_init(struct alcor_sdmmc_host *host)
{
	struct alcor_pci_priv *priv = host->alcor_pci;
	struct alcor_dev_cfg *cfg = priv->cfg;

	/* FIXME: This part is a mimics HW init of original driver.
	 * If we will ever get access to documentation, then this part
	 * should be reviewed again.
	 */

	/* reset command state engine */
	alcor_reset(host, AU6601_RESET_CMD);

	alcor_write8(priv, 0, AU6601_DMA_BOUNDARY);
	/* enable sd card mode */
	alcor_write8(priv, AU6601_SD_CARD, AU6601_ACTIVE_CTRL);

	/* set BUS width to 1 bit */
	alcor_write8(priv, 0, AU6601_REG_BUS_CTRL);

	/* reset data state engine */
	alcor_reset(host, AU6601_RESET_DATA);
	/* Not sure if a voodoo with AU6601_DMA_BOUNDARY is really needed */
	alcor_write8(priv, 0, AU6601_DMA_BOUNDARY);

	alcor_write8(priv, 0, AU6601_INTERFACE_MODE_CTRL);
	/* not clear what we are doing here. */
	alcor_write8(priv, 0x44, AU6601_PAD_DRIVE0);
	alcor_write8(priv, 0x44, AU6601_PAD_DRIVE1);
	alcor_write8(priv, 0x00, AU6601_PAD_DRIVE2);

	/* for 6601 - dma_boundary; for 6621 - dma_page_cnt
	 * exact meaning of this register is not clear.
	 */
	alcor_write8(priv, cfg->dma, AU6601_DMA_BOUNDARY);

	/* make sure all pins are set to input and VDD is off */
	alcor_write8(priv, 0, AU6601_OUTPUT_ENABLE);
	alcor_write8(priv, 0, AU6601_POWER_CONTROL);

	alcor_write8(priv, AU6601_DETECT_EN, AU6601_DETECT_STATUS);
	/* now we should be safe to enable IRQs */
	alcor_unmask_sd_irqs(host);
}

static void alcor_hw_uninit(struct alcor_sdmmc_host *host)
{
	struct alcor_pci_priv *priv = host->alcor_pci;

	alcor_mask_sd_irqs(host);
	alcor_reset(host, AU6601_RESET_CMD | AU6601_RESET_DATA);

	alcor_write8(priv, 0, AU6601_DETECT_STATUS);

	alcor_write8(priv, 0, AU6601_OUTPUT_ENABLE);
	alcor_write8(priv, 0, AU6601_POWER_CONTROL);

	alcor_write8(priv, 0, AU6601_OPT);
}

static void alcor_init_mmc(struct alcor_sdmmc_host *host)
{
	struct mmc_host *mmc = host->mmc;

	mmc->f_min = AU6601_MIN_CLOCK;
	mmc->f_max = AU6601_MAX_CLOCK;
	mmc->ocr_avail = MMC_VDD_33_34;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SD_HIGHSPEED
		| MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50
		| MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_DDR50;
	mmc->caps2 = MMC_CAP2_NO_SDIO;
	mmc->ops = &alcor_sdc_ops;

	/* The hardware does DMA data transfer of 4096 bytes to/from a single
	 * buffer address. Scatterlists are not supported, but upon DMA
	 * completion (signalled via IRQ), the original vendor driver does
	 * then immediately set up another DMA transfer of the next 4096
	 * bytes.
	 *
	 * This means that we need to handle the I/O in 4096 byte chunks.
	 * Lacking a way to limit the sglist entries to 4096 bytes, we instead
	 * impose that only one segment is provided, with maximum size 4096,
	 * which also happens to be the minimum size. This means that the
	 * single-entry sglist handled by this driver can be handed directly
	 * to the hardware, nice and simple.
	 *
	 * Unfortunately though, that means we only do 4096 bytes I/O per
	 * MMC command. A future improvement would be to make the driver
	 * accept sg lists and entries of any size, and simply iterate
	 * through them 4096 bytes at a time.
	 */
	mmc->max_segs = AU6601_MAX_DMA_SEGMENTS;
	mmc->max_seg_size = AU6601_MAX_DMA_BLOCK_SIZE;
	mmc->max_req_size = mmc->max_seg_size;
}

static int alcor_pci_sdmmc_drv_probe(struct platform_device *pdev)
{
	struct alcor_pci_priv *priv = pdev->dev.platform_data;
	struct mmc_host *mmc;
	struct alcor_sdmmc_host *host;
	int ret;

	mmc = mmc_alloc_host(sizeof(*host), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "Can't allocate MMC\n");
		return -ENOMEM;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dev = &pdev->dev;
	host->cur_power_mode = MMC_POWER_UNDEFINED;
	host->alcor_pci = priv;

	/* make sure irqs are disabled */
	alcor_write32(priv, 0, AU6601_REG_INT_ENABLE);
	alcor_write32(priv, 0, AU6601_MS_INT_ENABLE);

	ret = devm_request_threaded_irq(&pdev->dev, priv->irq,
			alcor_irq, alcor_irq_thread, IRQF_SHARED,
			DRV_NAME_ALCOR_PCI_SDMMC, host);

	if (ret) {
		dev_err(&pdev->dev, "Failed to get irq for data line\n");
		return ret;
	}

	mutex_init(&host->cmd_mutex);
	INIT_DELAYED_WORK(&host->timeout_work, alcor_timeout_timer);

	alcor_init_mmc(host);
	alcor_hw_init(host);

	dev_set_drvdata(&pdev->dev, host);
	mmc_add_host(mmc);
	return 0;
}

static int alcor_pci_sdmmc_drv_remove(struct platform_device *pdev)
{
	struct alcor_sdmmc_host *host = dev_get_drvdata(&pdev->dev);

	if (cancel_delayed_work_sync(&host->timeout_work))
		alcor_request_complete(host, 0);

	alcor_hw_uninit(host);
	mmc_remove_host(host->mmc);
	mmc_free_host(host->mmc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int alcor_pci_sdmmc_suspend(struct device *dev)
{
	struct alcor_sdmmc_host *host = dev_get_drvdata(dev);

	if (cancel_delayed_work_sync(&host->timeout_work))
		alcor_request_complete(host, 0);

	alcor_hw_uninit(host);

	return 0;
}

static int alcor_pci_sdmmc_resume(struct device *dev)
{
	struct alcor_sdmmc_host *host = dev_get_drvdata(dev);

	alcor_hw_init(host);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(alcor_mmc_pm_ops, alcor_pci_sdmmc_suspend,
			 alcor_pci_sdmmc_resume);

static const struct platform_device_id alcor_pci_sdmmc_ids[] = {
	{
		.name = DRV_NAME_ALCOR_PCI_SDMMC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, alcor_pci_sdmmc_ids);

static struct platform_driver alcor_pci_sdmmc_driver = {
	.probe		= alcor_pci_sdmmc_drv_probe,
	.remove		= alcor_pci_sdmmc_drv_remove,
	.id_table	= alcor_pci_sdmmc_ids,
	.driver		= {
		.name	= DRV_NAME_ALCOR_PCI_SDMMC,
		.pm	= &alcor_mmc_pm_ops
	},
};
module_platform_driver(alcor_pci_sdmmc_driver);

MODULE_AUTHOR("Oleksij Rempel <linux@rempel-privat.de>");
MODULE_DESCRIPTION("PCI driver for Alcor Micro AU6601 Secure Digital Host Controller Interface");
MODULE_LICENSE("GPL");
