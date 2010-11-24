/*
 * MMCIF eMMC driver.
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 * Yusuke Goda <yusuke.goda.sx@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 *
 * TODO
 *  1. DMA
 *  2. Power management
 *  3. Handle MMC errors better
 *
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>

#define DRIVER_NAME	"sh_mmcif"
#define DRIVER_VERSION	"2010-04-28"

/* CE_CMD_SET */
#define CMD_MASK		0x3f000000
#define CMD_SET_RTYP_NO		((0 << 23) | (0 << 22))
#define CMD_SET_RTYP_6B		((0 << 23) | (1 << 22)) /* R1/R1b/R3/R4/R5 */
#define CMD_SET_RTYP_17B	((1 << 23) | (0 << 22)) /* R2 */
#define CMD_SET_RBSY		(1 << 21) /* R1b */
#define CMD_SET_CCSEN		(1 << 20)
#define CMD_SET_WDAT		(1 << 19) /* 1: on data, 0: no data */
#define CMD_SET_DWEN		(1 << 18) /* 1: write, 0: read */
#define CMD_SET_CMLTE		(1 << 17) /* 1: multi block trans, 0: single */
#define CMD_SET_CMD12EN		(1 << 16) /* 1: CMD12 auto issue */
#define CMD_SET_RIDXC_INDEX	((0 << 15) | (0 << 14)) /* index check */
#define CMD_SET_RIDXC_BITS	((0 << 15) | (1 << 14)) /* check bits check */
#define CMD_SET_RIDXC_NO	((1 << 15) | (0 << 14)) /* no check */
#define CMD_SET_CRC7C		((0 << 13) | (0 << 12)) /* CRC7 check*/
#define CMD_SET_CRC7C_BITS	((0 << 13) | (1 << 12)) /* check bits check*/
#define CMD_SET_CRC7C_INTERNAL	((1 << 13) | (0 << 12)) /* internal CRC7 check*/
#define CMD_SET_CRC16C		(1 << 10) /* 0: CRC16 check*/
#define CMD_SET_CRCSTE		(1 << 8) /* 1: not receive CRC status */
#define CMD_SET_TBIT		(1 << 7) /* 1: tran mission bit "Low" */
#define CMD_SET_OPDM		(1 << 6) /* 1: open/drain */
#define CMD_SET_CCSH		(1 << 5)
#define CMD_SET_DATW_1		((0 << 1) | (0 << 0)) /* 1bit */
#define CMD_SET_DATW_4		((0 << 1) | (1 << 0)) /* 4bit */
#define CMD_SET_DATW_8		((1 << 1) | (0 << 0)) /* 8bit */

/* CE_CMD_CTRL */
#define CMD_CTRL_BREAK		(1 << 0)

/* CE_BLOCK_SET */
#define BLOCK_SIZE_MASK		0x0000ffff

/* CE_INT */
#define INT_CCSDE		(1 << 29)
#define INT_CMD12DRE		(1 << 26)
#define INT_CMD12RBE		(1 << 25)
#define INT_CMD12CRE		(1 << 24)
#define INT_DTRANE		(1 << 23)
#define INT_BUFRE		(1 << 22)
#define INT_BUFWEN		(1 << 21)
#define INT_BUFREN		(1 << 20)
#define INT_CCSRCV		(1 << 19)
#define INT_RBSYE		(1 << 17)
#define INT_CRSPE		(1 << 16)
#define INT_CMDVIO		(1 << 15)
#define INT_BUFVIO		(1 << 14)
#define INT_WDATERR		(1 << 11)
#define INT_RDATERR		(1 << 10)
#define INT_RIDXERR		(1 << 9)
#define INT_RSPERR		(1 << 8)
#define INT_CCSTO		(1 << 5)
#define INT_CRCSTO		(1 << 4)
#define INT_WDATTO		(1 << 3)
#define INT_RDATTO		(1 << 2)
#define INT_RBSYTO		(1 << 1)
#define INT_RSPTO		(1 << 0)
#define INT_ERR_STS		(INT_CMDVIO | INT_BUFVIO | INT_WDATERR |  \
				 INT_RDATERR | INT_RIDXERR | INT_RSPERR | \
				 INT_CCSTO | INT_CRCSTO | INT_WDATTO |	  \
				 INT_RDATTO | INT_RBSYTO | INT_RSPTO)

/* CE_INT_MASK */
#define MASK_ALL		0x00000000
#define MASK_MCCSDE		(1 << 29)
#define MASK_MCMD12DRE		(1 << 26)
#define MASK_MCMD12RBE		(1 << 25)
#define MASK_MCMD12CRE		(1 << 24)
#define MASK_MDTRANE		(1 << 23)
#define MASK_MBUFRE		(1 << 22)
#define MASK_MBUFWEN		(1 << 21)
#define MASK_MBUFREN		(1 << 20)
#define MASK_MCCSRCV		(1 << 19)
#define MASK_MRBSYE		(1 << 17)
#define MASK_MCRSPE		(1 << 16)
#define MASK_MCMDVIO		(1 << 15)
#define MASK_MBUFVIO		(1 << 14)
#define MASK_MWDATERR		(1 << 11)
#define MASK_MRDATERR		(1 << 10)
#define MASK_MRIDXERR		(1 << 9)
#define MASK_MRSPERR		(1 << 8)
#define MASK_MCCSTO		(1 << 5)
#define MASK_MCRCSTO		(1 << 4)
#define MASK_MWDATTO		(1 << 3)
#define MASK_MRDATTO		(1 << 2)
#define MASK_MRBSYTO		(1 << 1)
#define MASK_MRSPTO		(1 << 0)

/* CE_HOST_STS1 */
#define STS1_CMDSEQ		(1 << 31)

/* CE_HOST_STS2 */
#define STS2_CRCSTE		(1 << 31)
#define STS2_CRC16E		(1 << 30)
#define STS2_AC12CRCE		(1 << 29)
#define STS2_RSPCRC7E		(1 << 28)
#define STS2_CRCSTEBE		(1 << 27)
#define STS2_RDATEBE		(1 << 26)
#define STS2_AC12REBE		(1 << 25)
#define STS2_RSPEBE		(1 << 24)
#define STS2_AC12IDXE		(1 << 23)
#define STS2_RSPIDXE		(1 << 22)
#define STS2_CCSTO		(1 << 15)
#define STS2_RDATTO		(1 << 14)
#define STS2_DATBSYTO		(1 << 13)
#define STS2_CRCSTTO		(1 << 12)
#define STS2_AC12BSYTO		(1 << 11)
#define STS2_RSPBSYTO		(1 << 10)
#define STS2_AC12RSPTO		(1 << 9)
#define STS2_RSPTO		(1 << 8)
#define STS2_CRC_ERR		(STS2_CRCSTE | STS2_CRC16E |		\
				 STS2_AC12CRCE | STS2_RSPCRC7E | STS2_CRCSTEBE)
#define STS2_TIMEOUT_ERR	(STS2_CCSTO | STS2_RDATTO |		\
				 STS2_DATBSYTO | STS2_CRCSTTO |		\
				 STS2_AC12BSYTO | STS2_RSPBSYTO |	\
				 STS2_AC12RSPTO | STS2_RSPTO)

#define CLKDEV_EMMC_DATA	52000000 /* 52MHz */
#define CLKDEV_MMC_DATA		20000000 /* 20MHz */
#define CLKDEV_INIT		400000   /* 400 KHz */

struct sh_mmcif_host {
	struct mmc_host *mmc;
	struct mmc_data *data;
	struct platform_device *pd;
	struct clk *hclk;
	unsigned int clk;
	int bus_width;
	bool sd_error;
	long timeout;
	void __iomem *addr;
	struct completion intr_wait;

	/* DMA support */
	struct dma_chan		*chan_rx;
	struct dma_chan		*chan_tx;
	struct completion	dma_complete;
	unsigned int            dma_sglen;
};

static inline void sh_mmcif_bitset(struct sh_mmcif_host *host,
					unsigned int reg, u32 val)
{
	writel(val | readl(host->addr + reg), host->addr + reg);
}

static inline void sh_mmcif_bitclr(struct sh_mmcif_host *host,
					unsigned int reg, u32 val)
{
	writel(~val & readl(host->addr + reg), host->addr + reg);
}

#ifdef CONFIG_SH_MMCIF_DMA
static void mmcif_dma_complete(void *arg)
{
	struct sh_mmcif_host *host = arg;
	dev_dbg(&host->pd->dev, "Command completed\n");

	if (WARN(!host->data, "%s: NULL data in DMA completion!\n",
		 dev_name(&host->pd->dev)))
		return;

	if (host->data->flags & MMC_DATA_READ)
		dma_unmap_sg(&host->pd->dev, host->data->sg, host->dma_sglen,
			     DMA_FROM_DEVICE);
	else
		dma_unmap_sg(&host->pd->dev, host->data->sg, host->dma_sglen,
			     DMA_TO_DEVICE);

	complete(&host->dma_complete);
}

static void sh_mmcif_start_dma_rx(struct sh_mmcif_host *host)
{
	struct scatterlist *sg = host->data->sg;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_rx;
	dma_cookie_t cookie = -EINVAL;
	int ret;

	ret = dma_map_sg(&host->pd->dev, sg, host->data->sg_len, DMA_FROM_DEVICE);
	if (ret > 0) {
		host->dma_sglen = ret;
		desc = chan->device->device_prep_slave_sg(chan, sg, ret,
			DMA_FROM_DEVICE, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = mmcif_dma_complete;
		desc->callback_param = host;
		cookie = desc->tx_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		} else {
			sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN);
			chan->device->device_issue_pending(chan);
		}
	}
	dev_dbg(&host->pd->dev, "%s(): mapped %d -> %d, cookie %d\n",
		__func__, host->data->sg_len, ret, cookie);

	if (!desc) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = -EIO;
		host->chan_rx = NULL;
		host->dma_sglen = 0;
		dma_release_channel(chan);
		/* Free the Tx channel too */
		chan = host->chan_tx;
		if (chan) {
			host->chan_tx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pd->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
		sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
	}

	dev_dbg(&host->pd->dev, "%s(): desc %p, cookie %d, sg[%d]\n", __func__,
		desc, cookie, host->data->sg_len);
}

static void sh_mmcif_start_dma_tx(struct sh_mmcif_host *host)
{
	struct scatterlist *sg = host->data->sg;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *chan = host->chan_tx;
	dma_cookie_t cookie = -EINVAL;
	int ret;

	ret = dma_map_sg(&host->pd->dev, sg, host->data->sg_len, DMA_TO_DEVICE);
	if (ret > 0) {
		host->dma_sglen = ret;
		desc = chan->device->device_prep_slave_sg(chan, sg, ret,
			DMA_TO_DEVICE, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = mmcif_dma_complete;
		desc->callback_param = host;
		cookie = desc->tx_submit(desc);
		if (cookie < 0) {
			desc = NULL;
			ret = cookie;
		} else {
			sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAWEN);
			chan->device->device_issue_pending(chan);
		}
	}
	dev_dbg(&host->pd->dev, "%s(): mapped %d -> %d, cookie %d\n",
		__func__, host->data->sg_len, ret, cookie);

	if (!desc) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = -EIO;
		host->chan_tx = NULL;
		host->dma_sglen = 0;
		dma_release_channel(chan);
		/* Free the Rx channel too */
		chan = host->chan_rx;
		if (chan) {
			host->chan_rx = NULL;
			dma_release_channel(chan);
		}
		dev_warn(&host->pd->dev,
			 "DMA failed: %d, falling back to PIO\n", ret);
		sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
	}

	dev_dbg(&host->pd->dev, "%s(): desc %p, cookie %d\n", __func__,
		desc, cookie);
}

static bool sh_mmcif_filter(struct dma_chan *chan, void *arg)
{
	dev_dbg(chan->device->dev, "%s: slave data %p\n", __func__, arg);
	chan->private = arg;
	return true;
}

static void sh_mmcif_request_dma(struct sh_mmcif_host *host,
				 struct sh_mmcif_plat_data *pdata)
{
	host->dma_sglen = 0;

	/* We can only either use DMA for both Tx and Rx or not use it at all */
	if (pdata->dma) {
		dma_cap_mask_t mask;

		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);

		host->chan_tx = dma_request_channel(mask, sh_mmcif_filter,
						    &pdata->dma->chan_priv_tx);
		dev_dbg(&host->pd->dev, "%s: TX: got channel %p\n", __func__,
			host->chan_tx);

		if (!host->chan_tx)
			return;

		host->chan_rx = dma_request_channel(mask, sh_mmcif_filter,
						    &pdata->dma->chan_priv_rx);
		dev_dbg(&host->pd->dev, "%s: RX: got channel %p\n", __func__,
			host->chan_rx);

		if (!host->chan_rx) {
			dma_release_channel(host->chan_tx);
			host->chan_tx = NULL;
			return;
		}

		init_completion(&host->dma_complete);
	}
}

static void sh_mmcif_release_dma(struct sh_mmcif_host *host)
{
	sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC, BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
	/* Descriptors are freed automatically */
	if (host->chan_tx) {
		struct dma_chan *chan = host->chan_tx;
		host->chan_tx = NULL;
		dma_release_channel(chan);
	}
	if (host->chan_rx) {
		struct dma_chan *chan = host->chan_rx;
		host->chan_rx = NULL;
		dma_release_channel(chan);
	}

	host->dma_sglen = 0;
}
#else
static void sh_mmcif_start_dma_tx(struct sh_mmcif_host *host)
{
}

static void sh_mmcif_start_dma_rx(struct sh_mmcif_host *host)
{
}

static void sh_mmcif_request_dma(struct sh_mmcif_host *host,
				 struct sh_mmcif_plat_data *pdata)
{
	/* host->chan_tx, host->chan_tx and host->dma_sglen are all zero */
}

static void sh_mmcif_release_dma(struct sh_mmcif_host *host)
{
}
#endif

static void sh_mmcif_clock_control(struct sh_mmcif_host *host, unsigned int clk)
{
	struct sh_mmcif_plat_data *p = host->pd->dev.platform_data;

	sh_mmcif_bitclr(host, MMCIF_CE_CLK_CTRL, CLK_ENABLE);
	sh_mmcif_bitclr(host, MMCIF_CE_CLK_CTRL, CLK_CLEAR);

	if (!clk)
		return;
	if (p->sup_pclk && clk == host->clk)
		sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_SUP_PCLK);
	else
		sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_CLEAR &
			(ilog2(__rounddown_pow_of_two(host->clk / clk)) << 16));

	sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, CLK_ENABLE);
}

static void sh_mmcif_sync_reset(struct sh_mmcif_host *host)
{
	u32 tmp;

	tmp = 0x010f0000 & sh_mmcif_readl(host->addr, MMCIF_CE_CLK_CTRL);

	sh_mmcif_writel(host->addr, MMCIF_CE_VERSION, SOFT_RST_ON);
	sh_mmcif_writel(host->addr, MMCIF_CE_VERSION, SOFT_RST_OFF);
	sh_mmcif_bitset(host, MMCIF_CE_CLK_CTRL, tmp |
		SRSPTO_256 | SRBSYTO_29 | SRWDTO_29 | SCCSTO_29);
	/* byte swap on */
	sh_mmcif_bitset(host, MMCIF_CE_BUF_ACC, BUF_ACC_ATYP);
}

static int sh_mmcif_error_manage(struct sh_mmcif_host *host)
{
	u32 state1, state2;
	int ret, timeout = 10000000;

	host->sd_error = false;

	state1 = sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS1);
	state2 = sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS2);
	dev_dbg(&host->pd->dev, "ERR HOST_STS1 = %08x\n", state1);
	dev_dbg(&host->pd->dev, "ERR HOST_STS2 = %08x\n", state2);

	if (state1 & STS1_CMDSEQ) {
		sh_mmcif_bitset(host, MMCIF_CE_CMD_CTRL, CMD_CTRL_BREAK);
		sh_mmcif_bitset(host, MMCIF_CE_CMD_CTRL, ~CMD_CTRL_BREAK);
		while (1) {
			timeout--;
			if (timeout < 0) {
				dev_err(&host->pd->dev,
					"Forceed end of command sequence timeout err\n");
				return -EIO;
			}
			if (!(sh_mmcif_readl(host->addr, MMCIF_CE_HOST_STS1)
								& STS1_CMDSEQ))
				break;
			mdelay(1);
		}
		sh_mmcif_sync_reset(host);
		dev_dbg(&host->pd->dev, "Forced end of command sequence\n");
		return -EIO;
	}

	if (state2 & STS2_CRC_ERR) {
		dev_dbg(&host->pd->dev, ": Happened CRC error\n");
		ret = -EIO;
	} else if (state2 & STS2_TIMEOUT_ERR) {
		dev_dbg(&host->pd->dev, ": Happened Timeout error\n");
		ret = -ETIMEDOUT;
	} else {
		dev_dbg(&host->pd->dev, ": Happened End/Index error\n");
		ret = -EIO;
	}
	return ret;
}

static int sh_mmcif_single_read(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, *p = sg_virt(data->sg);

	/* buf read enable */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	blocksize = (BLOCK_SIZE_MASK &
			sh_mmcif_readl(host->addr, MMCIF_CE_BLOCK_SET)) + 3;
	for (i = 0; i < blocksize / 4; i++)
		*p++ = sh_mmcif_readl(host->addr, MMCIF_CE_DATA);

	/* buffer read end */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	return 0;
}

static int sh_mmcif_multi_read(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, j, sec, *p;

	blocksize = BLOCK_SIZE_MASK & sh_mmcif_readl(host->addr,
						     MMCIF_CE_BLOCK_SET);
	for (j = 0; j < data->sg_len; j++) {
		p = sg_virt(data->sg);
		for (sec = 0; sec < data->sg->length / blocksize; sec++) {
			sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
			/* buf read enable */
			time = wait_for_completion_interruptible_timeout(&host->intr_wait,
				host->timeout);

			if (time <= 0 || host->sd_error)
				return sh_mmcif_error_manage(host);

			for (i = 0; i < blocksize / 4; i++)
				*p++ = sh_mmcif_readl(host->addr,
						      MMCIF_CE_DATA);
		}
		if (j < data->sg_len - 1)
			data->sg++;
	}
	return 0;
}

static int sh_mmcif_single_write(struct sh_mmcif_host *host,
					struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 blocksize, i, *p = sg_virt(data->sg);

	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);

	/* buf write enable */
	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	blocksize = (BLOCK_SIZE_MASK &
			sh_mmcif_readl(host->addr, MMCIF_CE_BLOCK_SET)) + 3;
	for (i = 0; i < blocksize / 4; i++)
		sh_mmcif_writel(host->addr, MMCIF_CE_DATA, *p++);

	/* buffer write end */
	sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error)
		return sh_mmcif_error_manage(host);

	return 0;
}

static int sh_mmcif_multi_write(struct sh_mmcif_host *host,
						struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;
	long time;
	u32 i, sec, j, blocksize, *p;

	blocksize = BLOCK_SIZE_MASK & sh_mmcif_readl(host->addr,
						     MMCIF_CE_BLOCK_SET);

	for (j = 0; j < data->sg_len; j++) {
		p = sg_virt(data->sg);
		for (sec = 0; sec < data->sg->length / blocksize; sec++) {
			sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);
			/* buf write enable*/
			time = wait_for_completion_interruptible_timeout(&host->intr_wait,
				host->timeout);

			if (time <= 0 || host->sd_error)
				return sh_mmcif_error_manage(host);

			for (i = 0; i < blocksize / 4; i++)
				sh_mmcif_writel(host->addr,
						MMCIF_CE_DATA, *p++);
		}
		if (j < data->sg_len - 1)
			data->sg++;
	}
	return 0;
}

static void sh_mmcif_get_response(struct sh_mmcif_host *host,
						struct mmc_command *cmd)
{
	if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP3);
		cmd->resp[1] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP2);
		cmd->resp[2] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP1);
		cmd->resp[3] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP0);
	} else
		cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP0);
}

static void sh_mmcif_get_cmd12response(struct sh_mmcif_host *host,
						struct mmc_command *cmd)
{
	cmd->resp[0] = sh_mmcif_readl(host->addr, MMCIF_CE_RESP_CMD12);
}

static u32 sh_mmcif_set_cmd(struct sh_mmcif_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd, u32 opc)
{
	u32 tmp = 0;

	/* Response Type check */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		tmp |= CMD_SET_RTYP_NO;
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
	case MMC_RSP_R3:
		tmp |= CMD_SET_RTYP_6B;
		break;
	case MMC_RSP_R2:
		tmp |= CMD_SET_RTYP_17B;
		break;
	default:
		dev_err(&host->pd->dev, "Unsupported response type.\n");
		break;
	}
	switch (opc) {
	/* RBSY */
	case MMC_SWITCH:
	case MMC_STOP_TRANSMISSION:
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
	case MMC_ERASE:
	case MMC_GEN_CMD:
		tmp |= CMD_SET_RBSY;
		break;
	}
	/* WDAT / DATW */
	if (host->data) {
		tmp |= CMD_SET_WDAT;
		switch (host->bus_width) {
		case MMC_BUS_WIDTH_1:
			tmp |= CMD_SET_DATW_1;
			break;
		case MMC_BUS_WIDTH_4:
			tmp |= CMD_SET_DATW_4;
			break;
		case MMC_BUS_WIDTH_8:
			tmp |= CMD_SET_DATW_8;
			break;
		default:
			dev_err(&host->pd->dev, "Unsupported bus width.\n");
			break;
		}
	}
	/* DWEN */
	if (opc == MMC_WRITE_BLOCK || opc == MMC_WRITE_MULTIPLE_BLOCK)
		tmp |= CMD_SET_DWEN;
	/* CMLTE/CMD12EN */
	if (opc == MMC_READ_MULTIPLE_BLOCK || opc == MMC_WRITE_MULTIPLE_BLOCK) {
		tmp |= CMD_SET_CMLTE | CMD_SET_CMD12EN;
		sh_mmcif_bitset(host, MMCIF_CE_BLOCK_SET,
					mrq->data->blocks << 16);
	}
	/* RIDXC[1:0] check bits */
	if (opc == MMC_SEND_OP_COND || opc == MMC_ALL_SEND_CID ||
	    opc == MMC_SEND_CSD || opc == MMC_SEND_CID)
		tmp |= CMD_SET_RIDXC_BITS;
	/* RCRC7C[1:0] check bits */
	if (opc == MMC_SEND_OP_COND)
		tmp |= CMD_SET_CRC7C_BITS;
	/* RCRC7C[1:0] internal CRC7 */
	if (opc == MMC_ALL_SEND_CID ||
		opc == MMC_SEND_CSD || opc == MMC_SEND_CID)
		tmp |= CMD_SET_CRC7C_INTERNAL;

	return opc = ((opc << 24) | tmp);
}

static int sh_mmcif_data_trans(struct sh_mmcif_host *host,
				struct mmc_request *mrq, u32 opc)
{
	int ret;

	switch (opc) {
	case MMC_READ_MULTIPLE_BLOCK:
		ret = sh_mmcif_multi_read(host, mrq);
		break;
	case MMC_WRITE_MULTIPLE_BLOCK:
		ret = sh_mmcif_multi_write(host, mrq);
		break;
	case MMC_WRITE_BLOCK:
		ret = sh_mmcif_single_write(host, mrq);
		break;
	case MMC_READ_SINGLE_BLOCK:
	case MMC_SEND_EXT_CSD:
		ret = sh_mmcif_single_read(host, mrq);
		break;
	default:
		dev_err(&host->pd->dev, "UNSUPPORTED CMD = d'%08d\n", opc);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static void sh_mmcif_start_cmd(struct sh_mmcif_host *host,
			struct mmc_request *mrq, struct mmc_command *cmd)
{
	long time;
	int ret = 0, mask = 0;
	u32 opc = cmd->opcode;

	switch (opc) {
	/* respons busy check */
	case MMC_SWITCH:
	case MMC_STOP_TRANSMISSION:
	case MMC_SET_WRITE_PROT:
	case MMC_CLR_WRITE_PROT:
	case MMC_ERASE:
	case MMC_GEN_CMD:
		mask = MASK_MRBSYE;
		break;
	default:
		mask = MASK_MCRSPE;
		break;
	}
	mask |=	MASK_MCMDVIO | MASK_MBUFVIO | MASK_MWDATERR |
		MASK_MRDATERR | MASK_MRIDXERR | MASK_MRSPERR |
		MASK_MCCSTO | MASK_MCRCSTO | MASK_MWDATTO |
		MASK_MRDATTO | MASK_MRBSYTO | MASK_MRSPTO;

	if (host->data) {
		sh_mmcif_writel(host->addr, MMCIF_CE_BLOCK_SET, 0);
		sh_mmcif_writel(host->addr, MMCIF_CE_BLOCK_SET,
				mrq->data->blksz);
	}
	opc = sh_mmcif_set_cmd(host, mrq, cmd, opc);

	sh_mmcif_writel(host->addr, MMCIF_CE_INT, 0xD80430C0);
	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, mask);
	/* set arg */
	sh_mmcif_writel(host->addr, MMCIF_CE_ARG, cmd->arg);
	/* set cmd */
	sh_mmcif_writel(host->addr, MMCIF_CE_CMD_SET, opc);

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
		host->timeout);
	if (time <= 0) {
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}
	if (host->sd_error) {
		switch (cmd->opcode) {
		case MMC_ALL_SEND_CID:
		case MMC_SELECT_CARD:
		case MMC_APP_CMD:
			cmd->error = -ETIMEDOUT;
			break;
		default:
			dev_dbg(&host->pd->dev, "Cmd(d'%d) err\n",
					cmd->opcode);
			cmd->error = sh_mmcif_error_manage(host);
			break;
		}
		host->sd_error = false;
		return;
	}
	if (!(cmd->flags & MMC_RSP_PRESENT)) {
		cmd->error = 0;
		return;
	}
	sh_mmcif_get_response(host, cmd);
	if (host->data) {
		if (!host->dma_sglen) {
			ret = sh_mmcif_data_trans(host, mrq, cmd->opcode);
		} else {
			long time =
				wait_for_completion_interruptible_timeout(&host->dma_complete,
									  host->timeout);
			if (!time)
				ret = -ETIMEDOUT;
			else if (time < 0)
				ret = time;
			sh_mmcif_bitclr(host, MMCIF_CE_BUF_ACC,
					BUF_ACC_DMAREN | BUF_ACC_DMAWEN);
			host->dma_sglen = 0;
		}
		if (ret < 0)
			mrq->data->bytes_xfered = 0;
		else
			mrq->data->bytes_xfered =
				mrq->data->blocks * mrq->data->blksz;
	}
	cmd->error = ret;
}

static void sh_mmcif_stop_cmd(struct sh_mmcif_host *host,
		struct mmc_request *mrq, struct mmc_command *cmd)
{
	long time;

	if (mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MCMD12DRE);
	else if (mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
		sh_mmcif_bitset(host, MMCIF_CE_INT_MASK, MASK_MCMD12RBE);
	else {
		dev_err(&host->pd->dev, "unsupported stop cmd\n");
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}

	time = wait_for_completion_interruptible_timeout(&host->intr_wait,
			host->timeout);
	if (time <= 0 || host->sd_error) {
		cmd->error = sh_mmcif_error_manage(host);
		return;
	}
	sh_mmcif_get_cmd12response(host, cmd);
	cmd->error = 0;
}

static void sh_mmcif_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);

	switch (mrq->cmd->opcode) {
	/* MMCIF does not support SD/SDIO command */
	case SD_IO_SEND_OP_COND:
	case MMC_APP_CMD:
		mrq->cmd->error = -ETIMEDOUT;
		mmc_request_done(mmc, mrq);
		return;
	case MMC_SEND_EXT_CSD: /* = SD_SEND_IF_COND (8) */
		if (!mrq->data) {
			/* send_if_cond cmd (not support) */
			mrq->cmd->error = -ETIMEDOUT;
			mmc_request_done(mmc, mrq);
			return;
		}
		break;
	default:
		break;
	}
	host->data = mrq->data;
	if (mrq->data) {
		if (mrq->data->flags & MMC_DATA_READ) {
			if (host->chan_rx)
				sh_mmcif_start_dma_rx(host);
		} else {
			if (host->chan_tx)
				sh_mmcif_start_dma_tx(host);
		}
	}
	sh_mmcif_start_cmd(host, mrq, mrq->cmd);
	host->data = NULL;

	if (mrq->cmd->error != 0) {
		mmc_request_done(mmc, mrq);
		return;
	}
	if (mrq->stop)
		sh_mmcif_stop_cmd(host, mrq, mrq->stop);
	mmc_request_done(mmc, mrq);
}

static void sh_mmcif_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	struct sh_mmcif_plat_data *p = host->pd->dev.platform_data;

	if (ios->power_mode == MMC_POWER_OFF) {
		/* clock stop */
		sh_mmcif_clock_control(host, 0);
		if (p->down_pwr)
			p->down_pwr(host->pd);
		return;
	} else if (ios->power_mode == MMC_POWER_UP) {
		if (p->set_pwr)
			p->set_pwr(host->pd, ios->power_mode);
	}

	if (ios->clock)
		sh_mmcif_clock_control(host, ios->clock);

	host->bus_width = ios->bus_width;
}

static int sh_mmcif_get_cd(struct mmc_host *mmc)
{
	struct sh_mmcif_host *host = mmc_priv(mmc);
	struct sh_mmcif_plat_data *p = host->pd->dev.platform_data;

	if (!p->get_cd)
		return -ENOSYS;
	else
		return p->get_cd(host->pd);
}

static struct mmc_host_ops sh_mmcif_ops = {
	.request	= sh_mmcif_request,
	.set_ios	= sh_mmcif_set_ios,
	.get_cd		= sh_mmcif_get_cd,
};

static void sh_mmcif_detect(struct mmc_host *mmc)
{
	mmc_detect_change(mmc, 0);
}

static irqreturn_t sh_mmcif_intr(int irq, void *dev_id)
{
	struct sh_mmcif_host *host = dev_id;
	u32 state;
	int err = 0;

	state = sh_mmcif_readl(host->addr, MMCIF_CE_INT);

	if (state & INT_RBSYE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
				~(INT_RBSYE | INT_CRSPE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MRBSYE);
	} else if (state & INT_CRSPE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_CRSPE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCRSPE);
	} else if (state & INT_BUFREN) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFREN);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFREN);
	} else if (state & INT_BUFWEN) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFWEN);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFWEN);
	} else if (state & INT_CMD12DRE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
			~(INT_CMD12DRE | INT_CMD12RBE |
			  INT_CMD12CRE | INT_BUFRE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCMD12DRE);
	} else if (state & INT_BUFRE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_BUFRE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MBUFRE);
	} else if (state & INT_DTRANE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~INT_DTRANE);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MDTRANE);
	} else if (state & INT_CMD12RBE) {
		sh_mmcif_writel(host->addr, MMCIF_CE_INT,
				~(INT_CMD12RBE | INT_CMD12CRE));
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, MASK_MCMD12RBE);
	} else if (state & INT_ERR_STS) {
		/* err interrupts */
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~state);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, state);
		err = 1;
	} else {
		dev_dbg(&host->pd->dev, "Not support int\n");
		sh_mmcif_writel(host->addr, MMCIF_CE_INT, ~state);
		sh_mmcif_bitclr(host, MMCIF_CE_INT_MASK, state);
		err = 1;
	}
	if (err) {
		host->sd_error = true;
		dev_dbg(&host->pd->dev, "int err state = %08x\n", state);
	}
	if (state & ~(INT_CMD12RBE | INT_CMD12CRE))
		complete(&host->intr_wait);
	else
		dev_dbg(&host->pd->dev, "Unexpected IRQ 0x%x\n", state);

	return IRQ_HANDLED;
}

static int __devinit sh_mmcif_probe(struct platform_device *pdev)
{
	int ret = 0, irq[2];
	struct mmc_host *mmc;
	struct sh_mmcif_host *host;
	struct sh_mmcif_plat_data *pd;
	struct resource *res;
	void __iomem *reg;
	char clk_name[8];

	irq[0] = platform_get_irq(pdev, 0);
	irq[1] = platform_get_irq(pdev, 1);
	if (irq[0] < 0 || irq[1] < 0) {
		dev_err(&pdev->dev, "Get irq error\n");
		return -ENXIO;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "platform_get_resource error.\n");
		return -ENXIO;
	}
	reg = ioremap(res->start, resource_size(res));
	if (!reg) {
		dev_err(&pdev->dev, "ioremap error.\n");
		return -ENOMEM;
	}
	pd = pdev->dev.platform_data;
	if (!pd) {
		dev_err(&pdev->dev, "sh_mmcif plat data error.\n");
		ret = -ENXIO;
		goto clean_up;
	}
	mmc = mmc_alloc_host(sizeof(struct sh_mmcif_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto clean_up;
	}
	host		= mmc_priv(mmc);
	host->mmc	= mmc;
	host->addr	= reg;
	host->timeout	= 1000;

	snprintf(clk_name, sizeof(clk_name), "mmc%d", pdev->id);
	host->hclk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(host->hclk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(host->hclk);
		goto clean_up1;
	}
	clk_enable(host->hclk);
	host->clk = clk_get_rate(host->hclk);
	host->pd = pdev;

	init_completion(&host->intr_wait);

	mmc->ops = &sh_mmcif_ops;
	mmc->f_max = host->clk;
	/* close to 400KHz */
	if (mmc->f_max < 51200000)
		mmc->f_min = mmc->f_max / 128;
	else if (mmc->f_max < 102400000)
		mmc->f_min = mmc->f_max / 256;
	else
		mmc->f_min = mmc->f_max / 512;
	if (pd->ocr)
		mmc->ocr_avail = pd->ocr;
	mmc->caps = MMC_CAP_MMC_HIGHSPEED;
	if (pd->caps)
		mmc->caps |= pd->caps;
	mmc->max_segs = 32;
	mmc->max_blk_size = 512;
	mmc->max_req_size = PAGE_CACHE_SIZE * mmc->max_segs;
	mmc->max_blk_count = mmc->max_req_size / mmc->max_blk_size;
	mmc->max_seg_size = mmc->max_req_size;

	sh_mmcif_sync_reset(host);
	platform_set_drvdata(pdev, host);

	/* See if we also get DMA */
	sh_mmcif_request_dma(host, pd);

	mmc_add_host(mmc);

	ret = request_irq(irq[0], sh_mmcif_intr, 0, "sh_mmc:error", host);
	if (ret) {
		dev_err(&pdev->dev, "request_irq error (sh_mmc:error)\n");
		goto clean_up2;
	}
	ret = request_irq(irq[1], sh_mmcif_intr, 0, "sh_mmc:int", host);
	if (ret) {
		free_irq(irq[0], host);
		dev_err(&pdev->dev, "request_irq error (sh_mmc:int)\n");
		goto clean_up2;
	}

	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, MASK_ALL);
	sh_mmcif_detect(host->mmc);

	dev_info(&pdev->dev, "driver version %s\n", DRIVER_VERSION);
	dev_dbg(&pdev->dev, "chip ver H'%04x\n",
		sh_mmcif_readl(host->addr, MMCIF_CE_VERSION) & 0x0000ffff);
	return ret;

clean_up2:
	clk_disable(host->hclk);
clean_up1:
	mmc_free_host(mmc);
clean_up:
	if (reg)
		iounmap(reg);
	return ret;
}

static int __devexit sh_mmcif_remove(struct platform_device *pdev)
{
	struct sh_mmcif_host *host = platform_get_drvdata(pdev);
	int irq[2];

	mmc_remove_host(host->mmc);
	sh_mmcif_release_dma(host);

	if (host->addr)
		iounmap(host->addr);

	sh_mmcif_writel(host->addr, MMCIF_CE_INT_MASK, MASK_ALL);

	irq[0] = platform_get_irq(pdev, 0);
	irq[1] = platform_get_irq(pdev, 1);

	free_irq(irq[0], host);
	free_irq(irq[1], host);

	platform_set_drvdata(pdev, NULL);

	clk_disable(host->hclk);
	mmc_free_host(host->mmc);

	return 0;
}

static struct platform_driver sh_mmcif_driver = {
	.probe		= sh_mmcif_probe,
	.remove		= sh_mmcif_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __init sh_mmcif_init(void)
{
	return platform_driver_register(&sh_mmcif_driver);
}

static void __exit sh_mmcif_exit(void)
{
	platform_driver_unregister(&sh_mmcif_driver);
}

module_init(sh_mmcif_init);
module_exit(sh_mmcif_exit);


MODULE_DESCRIPTION("SuperH on-chip MMC/eMMC interface driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Yusuke Goda <yusuke.goda.sx@renesas.com>");
