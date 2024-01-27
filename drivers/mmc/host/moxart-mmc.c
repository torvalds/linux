/*
 * MOXA ART MMC host driver.
 *
 * Copyright (C) 2014 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
 *
 * Based on code from
 * Moxa Technologies Co., Ltd. <www.moxa.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sd.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/bitops.h>
#include <linux/of_dma.h>
#include <linux/spinlock.h>

#define REG_COMMAND		0
#define REG_ARGUMENT		4
#define REG_RESPONSE0		8
#define REG_RESPONSE1		12
#define REG_RESPONSE2		16
#define REG_RESPONSE3		20
#define REG_RESPONSE_COMMAND	24
#define REG_DATA_CONTROL	28
#define REG_DATA_TIMER		32
#define REG_DATA_LENGTH		36
#define REG_STATUS		40
#define REG_CLEAR		44
#define REG_INTERRUPT_MASK	48
#define REG_POWER_CONTROL	52
#define REG_CLOCK_CONTROL	56
#define REG_BUS_WIDTH		60
#define REG_DATA_WINDOW		64
#define REG_FEATURE		68
#define REG_REVISION		72

/* REG_COMMAND */
#define CMD_SDC_RESET		BIT(10)
#define CMD_EN			BIT(9)
#define CMD_APP_CMD		BIT(8)
#define CMD_LONG_RSP		BIT(7)
#define CMD_NEED_RSP		BIT(6)
#define CMD_IDX_MASK		0x3f

/* REG_RESPONSE_COMMAND */
#define RSP_CMD_APP		BIT(6)
#define RSP_CMD_IDX_MASK	0x3f

/* REG_DATA_CONTROL */
#define DCR_DATA_FIFO_RESET     BIT(8)
#define DCR_DATA_THRES          BIT(7)
#define DCR_DATA_EN		BIT(6)
#define DCR_DMA_EN		BIT(5)
#define DCR_DATA_WRITE		BIT(4)
#define DCR_BLK_SIZE		0x0f

/* REG_DATA_LENGTH */
#define DATA_LEN_MASK		0xffffff

/* REG_STATUS */
#define WRITE_PROT		BIT(12)
#define CARD_DETECT		BIT(11)
/* 1-10 below can be sent to either registers, interrupt or clear. */
#define CARD_CHANGE		BIT(10)
#define FIFO_ORUN		BIT(9)
#define FIFO_URUN		BIT(8)
#define DATA_END		BIT(7)
#define CMD_SENT		BIT(6)
#define DATA_CRC_OK		BIT(5)
#define RSP_CRC_OK		BIT(4)
#define DATA_TIMEOUT		BIT(3)
#define RSP_TIMEOUT		BIT(2)
#define DATA_CRC_FAIL		BIT(1)
#define RSP_CRC_FAIL		BIT(0)

#define MASK_RSP		(RSP_TIMEOUT | RSP_CRC_FAIL | \
				 RSP_CRC_OK  | CARD_DETECT  | CMD_SENT)

#define MASK_DATA		(DATA_CRC_OK   | DATA_END | \
				 DATA_CRC_FAIL | DATA_TIMEOUT)

#define MASK_INTR_PIO		(FIFO_URUN | FIFO_ORUN | CARD_CHANGE)

/* REG_POWER_CONTROL */
#define SD_POWER_ON		BIT(4)
#define SD_POWER_MASK		0x0f

/* REG_CLOCK_CONTROL */
#define CLK_HISPD		BIT(9)
#define CLK_OFF			BIT(8)
#define CLK_SD			BIT(7)
#define CLK_DIV_MASK		0x7f

/* REG_BUS_WIDTH */
#define BUS_WIDTH_4_SUPPORT	BIT(3)
#define BUS_WIDTH_4		BIT(2)
#define BUS_WIDTH_1		BIT(0)

#define MMC_VDD_360		23
#define MIN_POWER		(MMC_VDD_360 - SD_POWER_MASK)
#define MAX_RETRIES		500000

struct moxart_host {
	spinlock_t			lock;

	void __iomem			*base;

	phys_addr_t			reg_phys;

	struct dma_chan			*dma_chan_tx;
	struct dma_chan                 *dma_chan_rx;
	struct dma_async_tx_descriptor	*tx_desc;
	struct mmc_host			*mmc;
	struct mmc_request		*mrq;
	struct completion		dma_complete;
	struct completion		pio_complete;

	struct sg_mapping_iter		sg_miter;
	u32				data_len;
	u32				fifo_width;
	u32				timeout;
	u32				rate;

	long				sysclk;

	bool				have_dma;
	bool				is_removed;
};

static int moxart_wait_for_status(struct moxart_host *host,
				  u32 mask, u32 *status)
{
	int ret = -ETIMEDOUT;
	u32 i;

	for (i = 0; i < MAX_RETRIES; i++) {
		*status = readl(host->base + REG_STATUS);
		if (!(*status & mask)) {
			udelay(5);
			continue;
		}
		writel(*status & mask, host->base + REG_CLEAR);
		ret = 0;
		break;
	}

	if (ret)
		dev_err(mmc_dev(host->mmc), "timed out waiting for status\n");

	return ret;
}


static void moxart_send_command(struct moxart_host *host,
	struct mmc_command *cmd)
{
	u32 status, cmdctrl;

	writel(RSP_TIMEOUT  | RSP_CRC_OK |
	       RSP_CRC_FAIL | CMD_SENT, host->base + REG_CLEAR);
	writel(cmd->arg, host->base + REG_ARGUMENT);

	cmdctrl = cmd->opcode & CMD_IDX_MASK;
	if (cmdctrl == SD_APP_SET_BUS_WIDTH    || cmdctrl == SD_APP_OP_COND   ||
	    cmdctrl == SD_APP_SEND_SCR         || cmdctrl == SD_APP_SD_STATUS ||
	    cmdctrl == SD_APP_SEND_NUM_WR_BLKS)
		cmdctrl |= CMD_APP_CMD;

	if (cmd->flags & MMC_RSP_PRESENT)
		cmdctrl |= CMD_NEED_RSP;

	if (cmd->flags & MMC_RSP_136)
		cmdctrl |= CMD_LONG_RSP;

	writel(cmdctrl | CMD_EN, host->base + REG_COMMAND);

	if (moxart_wait_for_status(host, MASK_RSP, &status) == -ETIMEDOUT)
		cmd->error = -ETIMEDOUT;

	if (status & RSP_TIMEOUT) {
		cmd->error = -ETIMEDOUT;
		return;
	}
	if (status & RSP_CRC_FAIL) {
		cmd->error = -EIO;
		return;
	}
	if (status & RSP_CRC_OK) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[3] = readl(host->base + REG_RESPONSE0);
			cmd->resp[2] = readl(host->base + REG_RESPONSE1);
			cmd->resp[1] = readl(host->base + REG_RESPONSE2);
			cmd->resp[0] = readl(host->base + REG_RESPONSE3);
		} else {
			cmd->resp[0] = readl(host->base + REG_RESPONSE0);
		}
	}
}

static void moxart_dma_complete(void *param)
{
	struct moxart_host *host = param;

	complete(&host->dma_complete);
}

static bool moxart_use_dma(struct moxart_host *host)
{
	return (host->data_len > host->fifo_width) && host->have_dma;
}

static void moxart_transfer_dma(struct mmc_data *data, struct moxart_host *host)
{
	u32 len, dir_slave;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *dma_chan;

	if (host->data_len == data->bytes_xfered)
		return;

	if (data->flags & MMC_DATA_WRITE) {
		dma_chan = host->dma_chan_tx;
		dir_slave = DMA_MEM_TO_DEV;
	} else {
		dma_chan = host->dma_chan_rx;
		dir_slave = DMA_DEV_TO_MEM;
	}

	len = dma_map_sg(dma_chan->device->dev, data->sg,
			 data->sg_len, mmc_get_dma_dir(data));

	if (len > 0) {
		desc = dmaengine_prep_slave_sg(dma_chan, data->sg,
					       len, dir_slave,
					       DMA_PREP_INTERRUPT |
					       DMA_CTRL_ACK);
	} else {
		dev_err(mmc_dev(host->mmc), "dma_map_sg returned zero length\n");
	}

	if (desc) {
		host->tx_desc = desc;
		desc->callback = moxart_dma_complete;
		desc->callback_param = host;
		dmaengine_submit(desc);
		dma_async_issue_pending(dma_chan);
	}

	wait_for_completion_interruptible_timeout(&host->dma_complete,
						  host->timeout);

	data->bytes_xfered = host->data_len;

	dma_unmap_sg(dma_chan->device->dev,
		     data->sg, data->sg_len,
		     mmc_get_dma_dir(data));
}


static void moxart_transfer_pio(struct moxart_host *host)
{
	struct sg_mapping_iter *sgm = &host->sg_miter;
	struct mmc_data *data = host->mrq->cmd->data;
	u32 *sgp, len = 0, remain, status;

	if (host->data_len == data->bytes_xfered)
		return;

	/*
	 * By updating sgm->consumes this will get a proper pointer into the
	 * buffer at any time.
	 */
	if (!sg_miter_next(sgm)) {
		/* This shold not happen */
		dev_err(mmc_dev(host->mmc), "ran out of scatterlist prematurely\n");
		data->error = -EINVAL;
		complete(&host->pio_complete);
		return;
	}
	sgp = sgm->addr;
	remain = sgm->length;
	if (remain > host->data_len)
		remain = host->data_len;

	if (data->flags & MMC_DATA_WRITE) {
		while (remain > 0) {
			if (moxart_wait_for_status(host, FIFO_URUN, &status)
			     == -ETIMEDOUT) {
				data->error = -ETIMEDOUT;
				complete(&host->pio_complete);
				return;
			}
			for (len = 0; len < remain && len < host->fifo_width;) {
				iowrite32(*sgp, host->base + REG_DATA_WINDOW);
				sgp++;
				len += 4;
			}
			sgm->consumed += len;
			remain -= len;
		}

	} else {
		while (remain > 0) {
			if (moxart_wait_for_status(host, FIFO_ORUN, &status)
			    == -ETIMEDOUT) {
				data->error = -ETIMEDOUT;
				complete(&host->pio_complete);
				return;
			}
			for (len = 0; len < remain && len < host->fifo_width;) {
				*sgp = ioread32(host->base + REG_DATA_WINDOW);
				sgp++;
				len += 4;
			}
			sgm->consumed += len;
			remain -= len;
		}
	}

	data->bytes_xfered += sgm->consumed;
	if (host->data_len == data->bytes_xfered) {
		complete(&host->pio_complete);
		return;
	}
}

static void moxart_prepare_data(struct moxart_host *host)
{
	struct mmc_data *data = host->mrq->cmd->data;
	unsigned int flags = SG_MITER_ATOMIC; /* Used from IRQ */
	u32 datactrl;
	int blksz_bits;

	if (!data)
		return;

	host->data_len = data->blocks * data->blksz;
	blksz_bits = ffs(data->blksz) - 1;
	BUG_ON(1 << blksz_bits != data->blksz);

	datactrl = DCR_DATA_EN | (blksz_bits & DCR_BLK_SIZE);

	if (data->flags & MMC_DATA_WRITE) {
		flags |= SG_MITER_FROM_SG;
		datactrl |= DCR_DATA_WRITE;
	} else {
		flags |= SG_MITER_TO_SG;
	}

	if (moxart_use_dma(host))
		datactrl |= DCR_DMA_EN;
	else
		sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);

	writel(DCR_DATA_FIFO_RESET, host->base + REG_DATA_CONTROL);
	writel(MASK_DATA | FIFO_URUN | FIFO_ORUN, host->base + REG_CLEAR);
	writel(host->rate, host->base + REG_DATA_TIMER);
	writel(host->data_len, host->base + REG_DATA_LENGTH);
	writel(datactrl, host->base + REG_DATA_CONTROL);
}

static void moxart_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct moxart_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 status;

	spin_lock_irqsave(&host->lock, flags);

	init_completion(&host->dma_complete);
	init_completion(&host->pio_complete);

	host->mrq = mrq;

	if (readl(host->base + REG_STATUS) & CARD_DETECT) {
		mrq->cmd->error = -ETIMEDOUT;
		goto request_done;
	}

	moxart_prepare_data(host);
	moxart_send_command(host, host->mrq->cmd);

	if (mrq->cmd->data) {
		if (moxart_use_dma(host)) {

			writel(CARD_CHANGE, host->base + REG_INTERRUPT_MASK);

			spin_unlock_irqrestore(&host->lock, flags);

			moxart_transfer_dma(mrq->cmd->data, host);

			spin_lock_irqsave(&host->lock, flags);
		} else {

			writel(MASK_INTR_PIO, host->base + REG_INTERRUPT_MASK);

			spin_unlock_irqrestore(&host->lock, flags);

			/* PIO transfers start from interrupt. */
			wait_for_completion_interruptible_timeout(&host->pio_complete,
								  host->timeout);

			spin_lock_irqsave(&host->lock, flags);
		}

		if (host->is_removed) {
			dev_err(mmc_dev(host->mmc), "card removed\n");
			mrq->cmd->error = -ETIMEDOUT;
			goto request_done;
		}

		if (moxart_wait_for_status(host, MASK_DATA, &status)
		    == -ETIMEDOUT) {
			mrq->cmd->data->error = -ETIMEDOUT;
			goto request_done;
		}

		if (status & DATA_CRC_FAIL)
			mrq->cmd->data->error = -ETIMEDOUT;

		if (mrq->cmd->data->stop)
			moxart_send_command(host, mrq->cmd->data->stop);
	}

request_done:
	if (!moxart_use_dma(host))
		sg_miter_stop(&host->sg_miter);

	spin_unlock_irqrestore(&host->lock, flags);
	mmc_request_done(host->mmc, mrq);
}

static irqreturn_t moxart_irq(int irq, void *devid)
{
	struct moxart_host *host = (struct moxart_host *)devid;
	u32 status;

	spin_lock(&host->lock);

	status = readl(host->base + REG_STATUS);
	if (status & CARD_CHANGE) {
		host->is_removed = status & CARD_DETECT;
		if (host->is_removed && host->have_dma) {
			dmaengine_terminate_all(host->dma_chan_tx);
			dmaengine_terminate_all(host->dma_chan_rx);
		}
		host->mrq = NULL;
		writel(MASK_INTR_PIO, host->base + REG_CLEAR);
		writel(CARD_CHANGE, host->base + REG_INTERRUPT_MASK);
		mmc_detect_change(host->mmc, 0);
	}
	if (status & (FIFO_ORUN | FIFO_URUN) && host->mrq)
		moxart_transfer_pio(host);

	spin_unlock(&host->lock);

	return IRQ_HANDLED;
}

static void moxart_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct moxart_host *host = mmc_priv(mmc);
	unsigned long flags;
	u8 power, div;
	u32 ctrl;

	spin_lock_irqsave(&host->lock, flags);

	if (ios->clock) {
		for (div = 0; div < CLK_DIV_MASK; ++div) {
			if (ios->clock >= host->sysclk / (2 * (div + 1)))
				break;
		}
		ctrl = CLK_SD | div;
		host->rate = host->sysclk / (2 * (div + 1));
		if (host->rate > host->sysclk)
			ctrl |= CLK_HISPD;
		writel(ctrl, host->base + REG_CLOCK_CONTROL);
	}

	if (ios->power_mode == MMC_POWER_OFF) {
		writel(readl(host->base + REG_POWER_CONTROL) & ~SD_POWER_ON,
		       host->base + REG_POWER_CONTROL);
	} else {
		if (ios->vdd < MIN_POWER)
			power = 0;
		else
			power = ios->vdd - MIN_POWER;

		writel(SD_POWER_ON | (u32) power,
		       host->base + REG_POWER_CONTROL);
	}

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_4:
		writel(BUS_WIDTH_4, host->base + REG_BUS_WIDTH);
		break;
	default:
		writel(BUS_WIDTH_1, host->base + REG_BUS_WIDTH);
		break;
	}

	spin_unlock_irqrestore(&host->lock, flags);
}


static int moxart_get_ro(struct mmc_host *mmc)
{
	struct moxart_host *host = mmc_priv(mmc);

	return !!(readl(host->base + REG_STATUS) & WRITE_PROT);
}

static const struct mmc_host_ops moxart_ops = {
	.request = moxart_request,
	.set_ios = moxart_set_ios,
	.get_ro = moxart_get_ro,
};

static int moxart_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource res_mmc;
	struct mmc_host *mmc;
	struct moxart_host *host = NULL;
	struct dma_slave_config cfg;
	struct clk *clk;
	void __iomem *reg_mmc;
	int irq, ret;
	u32 i;

	mmc = mmc_alloc_host(sizeof(struct moxart_host), dev);
	if (!mmc) {
		dev_err(dev, "mmc_alloc_host failed\n");
		ret = -ENOMEM;
		goto out_mmc;
	}

	ret = of_address_to_resource(node, 0, &res_mmc);
	if (ret) {
		dev_err(dev, "of_address_to_resource failed\n");
		goto out_mmc;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq <= 0) {
		dev_err(dev, "irq_of_parse_and_map failed\n");
		ret = -EINVAL;
		goto out_mmc;
	}

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_mmc;
	}

	reg_mmc = devm_ioremap_resource(dev, &res_mmc);
	if (IS_ERR(reg_mmc)) {
		ret = PTR_ERR(reg_mmc);
		goto out_mmc;
	}

	ret = mmc_of_parse(mmc);
	if (ret)
		goto out_mmc;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->base = reg_mmc;
	host->reg_phys = res_mmc.start;
	host->timeout = msecs_to_jiffies(1000);
	host->sysclk = clk_get_rate(clk);
	host->fifo_width = readl(host->base + REG_FEATURE) << 2;
	host->dma_chan_tx = dma_request_chan(dev, "tx");
	host->dma_chan_rx = dma_request_chan(dev, "rx");

	spin_lock_init(&host->lock);

	mmc->ops = &moxart_ops;
	mmc->f_max = DIV_ROUND_CLOSEST(host->sysclk, 2);
	mmc->f_min = DIV_ROUND_CLOSEST(host->sysclk, CLK_DIV_MASK * 2);
	mmc->ocr_avail = 0xffff00;	/* Support 2.0v - 3.6v power. */
	mmc->max_blk_size = 2048; /* Max. block length in REG_DATA_CONTROL */
	mmc->max_req_size = DATA_LEN_MASK; /* bits 0-23 in REG_DATA_LENGTH */
	mmc->max_blk_count = mmc->max_req_size / 512;

	if (IS_ERR(host->dma_chan_tx) || IS_ERR(host->dma_chan_rx)) {
		if (PTR_ERR(host->dma_chan_tx) == -EPROBE_DEFER ||
		    PTR_ERR(host->dma_chan_rx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto out;
		}
		if (!IS_ERR(host->dma_chan_tx)) {
			dma_release_channel(host->dma_chan_tx);
			host->dma_chan_tx = NULL;
		}
		if (!IS_ERR(host->dma_chan_rx)) {
			dma_release_channel(host->dma_chan_rx);
			host->dma_chan_rx = NULL;
		}
		dev_dbg(dev, "PIO mode transfer enabled\n");
		host->have_dma = false;

		mmc->max_seg_size = mmc->max_req_size;
	} else {
		dev_dbg(dev, "DMA channels found (%p,%p)\n",
			 host->dma_chan_tx, host->dma_chan_rx);
		host->have_dma = true;

		memset(&cfg, 0, sizeof(cfg));
		cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;

		cfg.direction = DMA_MEM_TO_DEV;
		cfg.src_addr = 0;
		cfg.dst_addr = host->reg_phys + REG_DATA_WINDOW;
		dmaengine_slave_config(host->dma_chan_tx, &cfg);

		cfg.direction = DMA_DEV_TO_MEM;
		cfg.src_addr = host->reg_phys + REG_DATA_WINDOW;
		cfg.dst_addr = 0;
		dmaengine_slave_config(host->dma_chan_rx, &cfg);

		mmc->max_seg_size = min3(mmc->max_req_size,
			dma_get_max_seg_size(host->dma_chan_rx->device->dev),
			dma_get_max_seg_size(host->dma_chan_tx->device->dev));
	}

	if (readl(host->base + REG_BUS_WIDTH) & BUS_WIDTH_4_SUPPORT)
		mmc->caps |= MMC_CAP_4_BIT_DATA;

	writel(0, host->base + REG_INTERRUPT_MASK);

	writel(CMD_SDC_RESET, host->base + REG_COMMAND);
	for (i = 0; i < MAX_RETRIES; i++) {
		if (!(readl(host->base + REG_COMMAND) & CMD_SDC_RESET))
			break;
		udelay(5);
	}

	ret = devm_request_irq(dev, irq, moxart_irq, 0, "moxart-mmc", host);
	if (ret)
		goto out;

	dev_set_drvdata(dev, mmc);
	ret = mmc_add_host(mmc);
	if (ret)
		goto out;

	dev_dbg(dev, "IRQ=%d, FIFO is %d bytes\n", irq, host->fifo_width);

	return 0;

out:
	if (!IS_ERR_OR_NULL(host->dma_chan_tx))
		dma_release_channel(host->dma_chan_tx);
	if (!IS_ERR_OR_NULL(host->dma_chan_rx))
		dma_release_channel(host->dma_chan_rx);
out_mmc:
	if (mmc)
		mmc_free_host(mmc);
	return ret;
}

static void moxart_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = dev_get_drvdata(&pdev->dev);
	struct moxart_host *host = mmc_priv(mmc);

	if (!IS_ERR_OR_NULL(host->dma_chan_tx))
		dma_release_channel(host->dma_chan_tx);
	if (!IS_ERR_OR_NULL(host->dma_chan_rx))
		dma_release_channel(host->dma_chan_rx);
	mmc_remove_host(mmc);

	writel(0, host->base + REG_INTERRUPT_MASK);
	writel(0, host->base + REG_POWER_CONTROL);
	writel(readl(host->base + REG_CLOCK_CONTROL) | CLK_OFF,
	       host->base + REG_CLOCK_CONTROL);
	mmc_free_host(mmc);
}

static const struct of_device_id moxart_mmc_match[] = {
	{ .compatible = "moxa,moxart-mmc" },
	{ .compatible = "faraday,ftsdc010" },
	{ }
};
MODULE_DEVICE_TABLE(of, moxart_mmc_match);

static struct platform_driver moxart_mmc_driver = {
	.probe      = moxart_probe,
	.remove_new = moxart_remove,
	.driver     = {
		.name		= "mmc-moxart",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= moxart_mmc_match,
	},
};
module_platform_driver(moxart_mmc_driver);

MODULE_ALIAS("platform:mmc-moxart");
MODULE_DESCRIPTION("MOXA ART MMC driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jonas Jensen <jonas.jensen@gmail.com>");
