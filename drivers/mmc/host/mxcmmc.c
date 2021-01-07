// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/mmc/host/mxcmmc.c - Freescale i.MX MMCI driver
 *
 *  This is a driver for the SDHC controller found in Freescale MX2/MX3
 *  SoCs. It is basically the same hardware as found on MX1 (imxmmc.c).
 *  Unlike the hardware found on MX1, this hardware just works and does
 *  not need all the quirks found in imxmmc.c, hence the separate driver.
 *
 *  Copyright (C) 2008 Sascha Hauer, Pengutronix <s.hauer@pengutronix.de>
 *  Copyright (C) 2006 Pavel Pisa, PiKRON <ppisa@pikron.com>
 *
 *  derived from pxamci.c by Russell King
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/dmaengine.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/mmc/slot-gpio.h>

#include <asm/dma.h>
#include <asm/irq.h>
#include <linux/platform_data/mmc-mxcmmc.h>

#include <linux/platform_data/dma-imx.h>

#define DRIVER_NAME "mxc-mmc"
#define MXCMCI_TIMEOUT_MS 10000

#define MMC_REG_STR_STP_CLK		0x00
#define MMC_REG_STATUS			0x04
#define MMC_REG_CLK_RATE		0x08
#define MMC_REG_CMD_DAT_CONT		0x0C
#define MMC_REG_RES_TO			0x10
#define MMC_REG_READ_TO			0x14
#define MMC_REG_BLK_LEN			0x18
#define MMC_REG_NOB			0x1C
#define MMC_REG_REV_NO			0x20
#define MMC_REG_INT_CNTR		0x24
#define MMC_REG_CMD			0x28
#define MMC_REG_ARG			0x2C
#define MMC_REG_RES_FIFO		0x34
#define MMC_REG_BUFFER_ACCESS		0x38

#define STR_STP_CLK_RESET               (1 << 3)
#define STR_STP_CLK_START_CLK           (1 << 1)
#define STR_STP_CLK_STOP_CLK            (1 << 0)

#define STATUS_CARD_INSERTION		(1 << 31)
#define STATUS_CARD_REMOVAL		(1 << 30)
#define STATUS_YBUF_EMPTY		(1 << 29)
#define STATUS_XBUF_EMPTY		(1 << 28)
#define STATUS_YBUF_FULL		(1 << 27)
#define STATUS_XBUF_FULL		(1 << 26)
#define STATUS_BUF_UND_RUN		(1 << 25)
#define STATUS_BUF_OVFL			(1 << 24)
#define STATUS_SDIO_INT_ACTIVE		(1 << 14)
#define STATUS_END_CMD_RESP		(1 << 13)
#define STATUS_WRITE_OP_DONE		(1 << 12)
#define STATUS_DATA_TRANS_DONE		(1 << 11)
#define STATUS_READ_OP_DONE		(1 << 11)
#define STATUS_WR_CRC_ERROR_CODE_MASK	(3 << 10)
#define STATUS_CARD_BUS_CLK_RUN		(1 << 8)
#define STATUS_BUF_READ_RDY		(1 << 7)
#define STATUS_BUF_WRITE_RDY		(1 << 6)
#define STATUS_RESP_CRC_ERR		(1 << 5)
#define STATUS_CRC_READ_ERR		(1 << 3)
#define STATUS_CRC_WRITE_ERR		(1 << 2)
#define STATUS_TIME_OUT_RESP		(1 << 1)
#define STATUS_TIME_OUT_READ		(1 << 0)
#define STATUS_ERR_MASK			0x2f

#define CMD_DAT_CONT_CMD_RESP_LONG_OFF	(1 << 12)
#define CMD_DAT_CONT_STOP_READWAIT	(1 << 11)
#define CMD_DAT_CONT_START_READWAIT	(1 << 10)
#define CMD_DAT_CONT_BUS_WIDTH_4	(2 << 8)
#define CMD_DAT_CONT_INIT		(1 << 7)
#define CMD_DAT_CONT_WRITE		(1 << 4)
#define CMD_DAT_CONT_DATA_ENABLE	(1 << 3)
#define CMD_DAT_CONT_RESPONSE_48BIT_CRC	(1 << 0)
#define CMD_DAT_CONT_RESPONSE_136BIT	(2 << 0)
#define CMD_DAT_CONT_RESPONSE_48BIT	(3 << 0)

#define INT_SDIO_INT_WKP_EN		(1 << 18)
#define INT_CARD_INSERTION_WKP_EN	(1 << 17)
#define INT_CARD_REMOVAL_WKP_EN		(1 << 16)
#define INT_CARD_INSERTION_EN		(1 << 15)
#define INT_CARD_REMOVAL_EN		(1 << 14)
#define INT_SDIO_IRQ_EN			(1 << 13)
#define INT_DAT0_EN			(1 << 12)
#define INT_BUF_READ_EN			(1 << 4)
#define INT_BUF_WRITE_EN		(1 << 3)
#define INT_END_CMD_RES_EN		(1 << 2)
#define INT_WRITE_OP_DONE_EN		(1 << 1)
#define INT_READ_OP_EN			(1 << 0)

enum mxcmci_type {
	IMX21_MMC,
	IMX31_MMC,
	MPC512X_MMC,
};

struct mxcmci_host {
	struct mmc_host		*mmc;
	void __iomem		*base;
	dma_addr_t		phys_base;
	int			detect_irq;
	struct dma_chan		*dma;
	struct dma_async_tx_descriptor *desc;
	int			do_dma;
	int			default_irq_mask;
	int			use_sdio;
	unsigned int		power_mode;
	struct imxmmc_platform_data *pdata;

	struct mmc_request	*req;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	unsigned int		datasize;
	unsigned int		dma_dir;

	u16			rev_no;
	unsigned int		cmdat;

	struct clk		*clk_ipg;
	struct clk		*clk_per;

	int			clock;

	struct work_struct	datawork;
	spinlock_t		lock;

	int			burstlen;
	int			dmareq;
	struct dma_slave_config dma_slave_config;
	struct imx_dma_data	dma_data;

	struct timer_list	watchdog;
	enum mxcmci_type	devtype;
};

static const struct of_device_id mxcmci_of_match[] = {
	{
		.compatible = "fsl,imx21-mmc",
		.data = (void *) IMX21_MMC,
	}, {
		.compatible = "fsl,imx31-mmc",
		.data = (void *) IMX31_MMC,
	}, {
		.compatible = "fsl,mpc5121-sdhc",
		.data = (void *) MPC512X_MMC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, mxcmci_of_match);

static inline int is_imx31_mmc(struct mxcmci_host *host)
{
	return host->devtype == IMX31_MMC;
}

static inline int is_mpc512x_mmc(struct mxcmci_host *host)
{
	return host->devtype == MPC512X_MMC;
}

static inline u32 mxcmci_readl(struct mxcmci_host *host, int reg)
{
	if (IS_ENABLED(CONFIG_PPC_MPC512x))
		return ioread32be(host->base + reg);
	else
		return readl(host->base + reg);
}

static inline void mxcmci_writel(struct mxcmci_host *host, u32 val, int reg)
{
	if (IS_ENABLED(CONFIG_PPC_MPC512x))
		iowrite32be(val, host->base + reg);
	else
		writel(val, host->base + reg);
}

static inline u16 mxcmci_readw(struct mxcmci_host *host, int reg)
{
	if (IS_ENABLED(CONFIG_PPC_MPC512x))
		return ioread32be(host->base + reg);
	else
		return readw(host->base + reg);
}

static inline void mxcmci_writew(struct mxcmci_host *host, u16 val, int reg)
{
	if (IS_ENABLED(CONFIG_PPC_MPC512x))
		iowrite32be(val, host->base + reg);
	else
		writew(val, host->base + reg);
}

static void mxcmci_set_clk_rate(struct mxcmci_host *host, unsigned int clk_ios);

static void mxcmci_set_power(struct mxcmci_host *host, unsigned int vdd)
{
	if (!IS_ERR(host->mmc->supply.vmmc)) {
		if (host->power_mode == MMC_POWER_UP)
			mmc_regulator_set_ocr(host->mmc,
					      host->mmc->supply.vmmc, vdd);
		else if (host->power_mode == MMC_POWER_OFF)
			mmc_regulator_set_ocr(host->mmc,
					      host->mmc->supply.vmmc, 0);
	}

	if (host->pdata && host->pdata->setpower)
		host->pdata->setpower(mmc_dev(host->mmc), vdd);
}

static inline int mxcmci_use_dma(struct mxcmci_host *host)
{
	return host->do_dma;
}

static void mxcmci_softreset(struct mxcmci_host *host)
{
	int i;

	dev_dbg(mmc_dev(host->mmc), "mxcmci_softreset\n");

	/* reset sequence */
	mxcmci_writew(host, STR_STP_CLK_RESET, MMC_REG_STR_STP_CLK);
	mxcmci_writew(host, STR_STP_CLK_RESET | STR_STP_CLK_START_CLK,
			MMC_REG_STR_STP_CLK);

	for (i = 0; i < 8; i++)
		mxcmci_writew(host, STR_STP_CLK_START_CLK, MMC_REG_STR_STP_CLK);

	mxcmci_writew(host, 0xff, MMC_REG_RES_TO);
}

#if IS_ENABLED(CONFIG_PPC_MPC512x)
static inline void buffer_swap32(u32 *buf, int len)
{
	int i;

	for (i = 0; i < ((len + 3) / 4); i++) {
		*buf = swab32(*buf);
		buf++;
	}
}

static void mxcmci_swap_buffers(struct mmc_data *data)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(data->sg, sg, data->sg_len, i)
		buffer_swap32(sg_virt(sg), sg->length);
}
#else
static inline void mxcmci_swap_buffers(struct mmc_data *data) {}
#endif

static int mxcmci_setup_data(struct mxcmci_host *host, struct mmc_data *data)
{
	unsigned int nob = data->blocks;
	unsigned int blksz = data->blksz;
	unsigned int datasize = nob * blksz;
	struct scatterlist *sg;
	enum dma_transfer_direction slave_dirn;
	int i, nents;

	host->data = data;
	data->bytes_xfered = 0;

	mxcmci_writew(host, nob, MMC_REG_NOB);
	mxcmci_writew(host, blksz, MMC_REG_BLK_LEN);
	host->datasize = datasize;

	if (!mxcmci_use_dma(host))
		return 0;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3 || sg->length < 512) {
			host->do_dma = 0;
			return 0;
		}
	}

	if (data->flags & MMC_DATA_READ) {
		host->dma_dir = DMA_FROM_DEVICE;
		slave_dirn = DMA_DEV_TO_MEM;
	} else {
		host->dma_dir = DMA_TO_DEVICE;
		slave_dirn = DMA_MEM_TO_DEV;

		mxcmci_swap_buffers(data);
	}

	nents = dma_map_sg(host->dma->device->dev, data->sg,
				     data->sg_len,  host->dma_dir);
	if (nents != data->sg_len)
		return -EINVAL;

	host->desc = dmaengine_prep_slave_sg(host->dma,
		data->sg, data->sg_len, slave_dirn,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!host->desc) {
		dma_unmap_sg(host->dma->device->dev, data->sg, data->sg_len,
				host->dma_dir);
		host->do_dma = 0;
		return 0; /* Fall back to PIO */
	}
	wmb();

	dmaengine_submit(host->desc);
	dma_async_issue_pending(host->dma);

	mod_timer(&host->watchdog, jiffies + msecs_to_jiffies(MXCMCI_TIMEOUT_MS));

	return 0;
}

static void mxcmci_cmd_done(struct mxcmci_host *host, unsigned int stat);
static void mxcmci_data_done(struct mxcmci_host *host, unsigned int stat);

static void mxcmci_dma_callback(void *data)
{
	struct mxcmci_host *host = data;
	u32 stat;

	del_timer(&host->watchdog);

	stat = mxcmci_readl(host, MMC_REG_STATUS);

	dev_dbg(mmc_dev(host->mmc), "%s: 0x%08x\n", __func__, stat);

	mxcmci_data_done(host, stat);
}

static int mxcmci_start_cmd(struct mxcmci_host *host, struct mmc_command *cmd,
		unsigned int cmdat)
{
	u32 int_cntr = host->default_irq_mask;
	unsigned long flags;

	WARN_ON(host->cmd != NULL);
	host->cmd = cmd;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1: /* short CRC, OPCODE */
	case MMC_RSP_R1B:/* short CRC, OPCODE, BUSY */
		cmdat |= CMD_DAT_CONT_RESPONSE_48BIT_CRC;
		break;
	case MMC_RSP_R2: /* long 136 bit + CRC */
		cmdat |= CMD_DAT_CONT_RESPONSE_136BIT;
		break;
	case MMC_RSP_R3: /* short */
		cmdat |= CMD_DAT_CONT_RESPONSE_48BIT;
		break;
	case MMC_RSP_NONE:
		break;
	default:
		dev_err(mmc_dev(host->mmc), "unhandled response type 0x%x\n",
				mmc_resp_type(cmd));
		cmd->error = -EINVAL;
		return -EINVAL;
	}

	int_cntr = INT_END_CMD_RES_EN;

	if (mxcmci_use_dma(host)) {
		if (host->dma_dir == DMA_FROM_DEVICE) {
			host->desc->callback = mxcmci_dma_callback;
			host->desc->callback_param = host;
		} else {
			int_cntr |= INT_WRITE_OP_DONE_EN;
		}
	}

	spin_lock_irqsave(&host->lock, flags);
	if (host->use_sdio)
		int_cntr |= INT_SDIO_IRQ_EN;
	mxcmci_writel(host, int_cntr, MMC_REG_INT_CNTR);
	spin_unlock_irqrestore(&host->lock, flags);

	mxcmci_writew(host, cmd->opcode, MMC_REG_CMD);
	mxcmci_writel(host, cmd->arg, MMC_REG_ARG);
	mxcmci_writew(host, cmdat, MMC_REG_CMD_DAT_CONT);

	return 0;
}

static void mxcmci_finish_request(struct mxcmci_host *host,
		struct mmc_request *req)
{
	u32 int_cntr = host->default_irq_mask;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);
	if (host->use_sdio)
		int_cntr |= INT_SDIO_IRQ_EN;
	mxcmci_writel(host, int_cntr, MMC_REG_INT_CNTR);
	spin_unlock_irqrestore(&host->lock, flags);

	host->req = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmc_request_done(host->mmc, req);
}

static int mxcmci_finish_data(struct mxcmci_host *host, unsigned int stat)
{
	struct mmc_data *data = host->data;
	int data_error;

	if (mxcmci_use_dma(host)) {
		dma_unmap_sg(host->dma->device->dev, data->sg, data->sg_len,
				host->dma_dir);
		mxcmci_swap_buffers(data);
	}

	if (stat & STATUS_ERR_MASK) {
		dev_dbg(mmc_dev(host->mmc), "request failed. status: 0x%08x\n",
				stat);
		if (stat & STATUS_CRC_READ_ERR) {
			dev_err(mmc_dev(host->mmc), "%s: -EILSEQ\n", __func__);
			data->error = -EILSEQ;
		} else if (stat & STATUS_CRC_WRITE_ERR) {
			u32 err_code = (stat >> 9) & 0x3;
			if (err_code == 2) { /* No CRC response */
				dev_err(mmc_dev(host->mmc),
					"%s: No CRC -ETIMEDOUT\n", __func__);
				data->error = -ETIMEDOUT;
			} else {
				dev_err(mmc_dev(host->mmc),
					"%s: -EILSEQ\n", __func__);
				data->error = -EILSEQ;
			}
		} else if (stat & STATUS_TIME_OUT_READ) {
			dev_err(mmc_dev(host->mmc),
				"%s: read -ETIMEDOUT\n", __func__);
			data->error = -ETIMEDOUT;
		} else {
			dev_err(mmc_dev(host->mmc), "%s: -EIO\n", __func__);
			data->error = -EIO;
		}
	} else {
		data->bytes_xfered = host->datasize;
	}

	data_error = data->error;

	host->data = NULL;

	return data_error;
}

static void mxcmci_read_response(struct mxcmci_host *host, unsigned int stat)
{
	struct mmc_command *cmd = host->cmd;
	int i;
	u32 a, b, c;

	if (!cmd)
		return;

	if (stat & STATUS_TIME_OUT_RESP) {
		dev_dbg(mmc_dev(host->mmc), "CMD TIMEOUT\n");
		cmd->error = -ETIMEDOUT;
	} else if (stat & STATUS_RESP_CRC_ERR && cmd->flags & MMC_RSP_CRC) {
		dev_dbg(mmc_dev(host->mmc), "cmd crc error\n");
		cmd->error = -EILSEQ;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			for (i = 0; i < 4; i++) {
				a = mxcmci_readw(host, MMC_REG_RES_FIFO);
				b = mxcmci_readw(host, MMC_REG_RES_FIFO);
				cmd->resp[i] = a << 16 | b;
			}
		} else {
			a = mxcmci_readw(host, MMC_REG_RES_FIFO);
			b = mxcmci_readw(host, MMC_REG_RES_FIFO);
			c = mxcmci_readw(host, MMC_REG_RES_FIFO);
			cmd->resp[0] = a << 24 | b << 8 | c >> 8;
		}
	}
}

static int mxcmci_poll_status(struct mxcmci_host *host, u32 mask)
{
	u32 stat;
	unsigned long timeout = jiffies + HZ;

	do {
		stat = mxcmci_readl(host, MMC_REG_STATUS);
		if (stat & STATUS_ERR_MASK)
			return stat;
		if (time_after(jiffies, timeout)) {
			mxcmci_softreset(host);
			mxcmci_set_clk_rate(host, host->clock);
			return STATUS_TIME_OUT_READ;
		}
		if (stat & mask)
			return 0;
		cpu_relax();
	} while (1);
}

static int mxcmci_pull(struct mxcmci_host *host, void *_buf, int bytes)
{
	unsigned int stat;
	u32 *buf = _buf;

	while (bytes > 3) {
		stat = mxcmci_poll_status(host,
				STATUS_BUF_READ_RDY | STATUS_READ_OP_DONE);
		if (stat)
			return stat;
		*buf++ = cpu_to_le32(mxcmci_readl(host, MMC_REG_BUFFER_ACCESS));
		bytes -= 4;
	}

	if (bytes) {
		u8 *b = (u8 *)buf;
		u32 tmp;

		stat = mxcmci_poll_status(host,
				STATUS_BUF_READ_RDY | STATUS_READ_OP_DONE);
		if (stat)
			return stat;
		tmp = cpu_to_le32(mxcmci_readl(host, MMC_REG_BUFFER_ACCESS));
		memcpy(b, &tmp, bytes);
	}

	return 0;
}

static int mxcmci_push(struct mxcmci_host *host, void *_buf, int bytes)
{
	unsigned int stat;
	u32 *buf = _buf;

	while (bytes > 3) {
		stat = mxcmci_poll_status(host, STATUS_BUF_WRITE_RDY);
		if (stat)
			return stat;
		mxcmci_writel(host, cpu_to_le32(*buf++), MMC_REG_BUFFER_ACCESS);
		bytes -= 4;
	}

	if (bytes) {
		u8 *b = (u8 *)buf;
		u32 tmp;

		stat = mxcmci_poll_status(host, STATUS_BUF_WRITE_RDY);
		if (stat)
			return stat;

		memcpy(&tmp, b, bytes);
		mxcmci_writel(host, cpu_to_le32(tmp), MMC_REG_BUFFER_ACCESS);
	}

	return mxcmci_poll_status(host, STATUS_BUF_WRITE_RDY);
}

static int mxcmci_transfer_data(struct mxcmci_host *host)
{
	struct mmc_data *data = host->req->data;
	struct scatterlist *sg;
	int stat, i;

	host->data = data;
	host->datasize = 0;

	if (data->flags & MMC_DATA_READ) {
		for_each_sg(data->sg, sg, data->sg_len, i) {
			stat = mxcmci_pull(host, sg_virt(sg), sg->length);
			if (stat)
				return stat;
			host->datasize += sg->length;
		}
	} else {
		for_each_sg(data->sg, sg, data->sg_len, i) {
			stat = mxcmci_push(host, sg_virt(sg), sg->length);
			if (stat)
				return stat;
			host->datasize += sg->length;
		}
		stat = mxcmci_poll_status(host, STATUS_WRITE_OP_DONE);
		if (stat)
			return stat;
	}
	return 0;
}

static void mxcmci_datawork(struct work_struct *work)
{
	struct mxcmci_host *host = container_of(work, struct mxcmci_host,
						  datawork);
	int datastat = mxcmci_transfer_data(host);

	mxcmci_writel(host, STATUS_READ_OP_DONE | STATUS_WRITE_OP_DONE,
		MMC_REG_STATUS);
	mxcmci_finish_data(host, datastat);

	if (host->req->stop) {
		if (mxcmci_start_cmd(host, host->req->stop, 0)) {
			mxcmci_finish_request(host, host->req);
			return;
		}
	} else {
		mxcmci_finish_request(host, host->req);
	}
}

static void mxcmci_data_done(struct mxcmci_host *host, unsigned int stat)
{
	struct mmc_request *req;
	int data_error;
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	if (!host->data) {
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	if (!host->req) {
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	req = host->req;
	if (!req->stop)
		host->req = NULL; /* we will handle finish req below */

	data_error = mxcmci_finish_data(host, stat);

	spin_unlock_irqrestore(&host->lock, flags);

	if (data_error)
		return;

	mxcmci_read_response(host, stat);
	host->cmd = NULL;

	if (req->stop) {
		if (mxcmci_start_cmd(host, req->stop, 0)) {
			mxcmci_finish_request(host, req);
			return;
		}
	} else {
		mxcmci_finish_request(host, req);
	}
}

static void mxcmci_cmd_done(struct mxcmci_host *host, unsigned int stat)
{
	mxcmci_read_response(host, stat);
	host->cmd = NULL;

	if (!host->data && host->req) {
		mxcmci_finish_request(host, host->req);
		return;
	}

	/* For the DMA case the DMA engine handles the data transfer
	 * automatically. For non DMA we have to do it ourselves.
	 * Don't do it in interrupt context though.
	 */
	if (!mxcmci_use_dma(host) && host->data)
		schedule_work(&host->datawork);

}

static irqreturn_t mxcmci_irq(int irq, void *devid)
{
	struct mxcmci_host *host = devid;
	bool sdio_irq;
	u32 stat;

	stat = mxcmci_readl(host, MMC_REG_STATUS);
	mxcmci_writel(host,
		stat & ~(STATUS_SDIO_INT_ACTIVE | STATUS_DATA_TRANS_DONE |
			 STATUS_WRITE_OP_DONE),
		MMC_REG_STATUS);

	dev_dbg(mmc_dev(host->mmc), "%s: 0x%08x\n", __func__, stat);

	spin_lock(&host->lock);
	sdio_irq = (stat & STATUS_SDIO_INT_ACTIVE) && host->use_sdio;
	spin_unlock(&host->lock);

	if (mxcmci_use_dma(host) && (stat & (STATUS_WRITE_OP_DONE)))
		mxcmci_writel(host, STATUS_WRITE_OP_DONE, MMC_REG_STATUS);

	if (sdio_irq) {
		mxcmci_writel(host, STATUS_SDIO_INT_ACTIVE, MMC_REG_STATUS);
		mmc_signal_sdio_irq(host->mmc);
	}

	if (stat & STATUS_END_CMD_RESP)
		mxcmci_cmd_done(host, stat);

	if (mxcmci_use_dma(host) && (stat & STATUS_WRITE_OP_DONE)) {
		del_timer(&host->watchdog);
		mxcmci_data_done(host, stat);
	}

	if (host->default_irq_mask &&
		  (stat & (STATUS_CARD_INSERTION | STATUS_CARD_REMOVAL)))
		mmc_detect_change(host->mmc, msecs_to_jiffies(200));

	return IRQ_HANDLED;
}

static void mxcmci_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct mxcmci_host *host = mmc_priv(mmc);
	unsigned int cmdat = host->cmdat;
	int error;

	WARN_ON(host->req != NULL);

	host->req = req;
	host->cmdat &= ~CMD_DAT_CONT_INIT;

	if (host->dma)
		host->do_dma = 1;

	if (req->data) {
		error = mxcmci_setup_data(host, req->data);
		if (error) {
			req->cmd->error = error;
			goto out;
		}


		cmdat |= CMD_DAT_CONT_DATA_ENABLE;

		if (req->data->flags & MMC_DATA_WRITE)
			cmdat |= CMD_DAT_CONT_WRITE;
	}

	error = mxcmci_start_cmd(host, req->cmd, cmdat);

out:
	if (error)
		mxcmci_finish_request(host, req);
}

static void mxcmci_set_clk_rate(struct mxcmci_host *host, unsigned int clk_ios)
{
	unsigned int divider;
	int prescaler = 0;
	unsigned int clk_in = clk_get_rate(host->clk_per);

	while (prescaler <= 0x800) {
		for (divider = 1; divider <= 0xF; divider++) {
			int x;

			x = (clk_in / (divider + 1));

			if (prescaler)
				x /= (prescaler * 2);

			if (x <= clk_ios)
				break;
		}
		if (divider < 0x10)
			break;

		if (prescaler == 0)
			prescaler = 1;
		else
			prescaler <<= 1;
	}

	mxcmci_writew(host, (prescaler << 4) | divider, MMC_REG_CLK_RATE);

	dev_dbg(mmc_dev(host->mmc), "scaler: %d divider: %d in: %d out: %d\n",
			prescaler, divider, clk_in, clk_ios);
}

static int mxcmci_setup_dma(struct mmc_host *mmc)
{
	struct mxcmci_host *host = mmc_priv(mmc);
	struct dma_slave_config *config = &host->dma_slave_config;

	config->dst_addr = host->phys_base + MMC_REG_BUFFER_ACCESS;
	config->src_addr = host->phys_base + MMC_REG_BUFFER_ACCESS;
	config->dst_addr_width = 4;
	config->src_addr_width = 4;
	config->dst_maxburst = host->burstlen;
	config->src_maxburst = host->burstlen;
	config->device_fc = false;

	return dmaengine_slave_config(host->dma, config);
}

static void mxcmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mxcmci_host *host = mmc_priv(mmc);
	int burstlen, ret;

	/*
	 * use burstlen of 64 (16 words) in 4 bit mode (--> reg value  0)
	 * use burstlen of 16 (4 words) in 1 bit mode (--> reg value 16)
	 */
	if (ios->bus_width == MMC_BUS_WIDTH_4)
		burstlen = 16;
	else
		burstlen = 4;

	if (mxcmci_use_dma(host) && burstlen != host->burstlen) {
		host->burstlen = burstlen;
		ret = mxcmci_setup_dma(mmc);
		if (ret) {
			dev_err(mmc_dev(host->mmc),
				"failed to config DMA channel. Falling back to PIO\n");
			dma_release_channel(host->dma);
			host->do_dma = 0;
			host->dma = NULL;
		}
	}

	if (ios->bus_width == MMC_BUS_WIDTH_4)
		host->cmdat |= CMD_DAT_CONT_BUS_WIDTH_4;
	else
		host->cmdat &= ~CMD_DAT_CONT_BUS_WIDTH_4;

	if (host->power_mode != ios->power_mode) {
		host->power_mode = ios->power_mode;
		mxcmci_set_power(host, ios->vdd);

		if (ios->power_mode == MMC_POWER_ON)
			host->cmdat |= CMD_DAT_CONT_INIT;
	}

	if (ios->clock) {
		mxcmci_set_clk_rate(host, ios->clock);
		mxcmci_writew(host, STR_STP_CLK_START_CLK, MMC_REG_STR_STP_CLK);
	} else {
		mxcmci_writew(host, STR_STP_CLK_STOP_CLK, MMC_REG_STR_STP_CLK);
	}

	host->clock = ios->clock;
}

static irqreturn_t mxcmci_detect_irq(int irq, void *data)
{
	struct mmc_host *mmc = data;

	dev_dbg(mmc_dev(mmc), "%s\n", __func__);

	mmc_detect_change(mmc, msecs_to_jiffies(250));
	return IRQ_HANDLED;
}

static int mxcmci_get_ro(struct mmc_host *mmc)
{
	struct mxcmci_host *host = mmc_priv(mmc);

	if (host->pdata && host->pdata->get_ro)
		return !!host->pdata->get_ro(mmc_dev(mmc));
	/*
	 * If board doesn't support read only detection (no mmc_gpio
	 * context or gpio is invalid), then let the mmc core decide
	 * what to do.
	 */
	return mmc_gpio_get_ro(mmc);
}

static void mxcmci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct mxcmci_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 int_cntr;

	spin_lock_irqsave(&host->lock, flags);
	host->use_sdio = enable;
	int_cntr = mxcmci_readl(host, MMC_REG_INT_CNTR);

	if (enable)
		int_cntr |= INT_SDIO_IRQ_EN;
	else
		int_cntr &= ~INT_SDIO_IRQ_EN;

	mxcmci_writel(host, int_cntr, MMC_REG_INT_CNTR);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void mxcmci_init_card(struct mmc_host *host, struct mmc_card *card)
{
	struct mxcmci_host *mxcmci = mmc_priv(host);

	/*
	 * MX3 SoCs have a silicon bug which corrupts CRC calculation of
	 * multi-block transfers when connected SDIO peripheral doesn't
	 * drive the BUSY line as required by the specs.
	 * One way to prevent this is to only allow 1-bit transfers.
	 */

	if (is_imx31_mmc(mxcmci) && card->type == MMC_TYPE_SDIO)
		host->caps &= ~MMC_CAP_4_BIT_DATA;
	else
		host->caps |= MMC_CAP_4_BIT_DATA;
}

static bool filter(struct dma_chan *chan, void *param)
{
	struct mxcmci_host *host = param;

	if (!imx_dma_is_general_purpose(chan))
		return false;

	chan->private = &host->dma_data;

	return true;
}

static void mxcmci_watchdog(struct timer_list *t)
{
	struct mxcmci_host *host = from_timer(host, t, watchdog);
	struct mmc_request *req = host->req;
	unsigned int stat = mxcmci_readl(host, MMC_REG_STATUS);

	if (host->dma_dir == DMA_FROM_DEVICE) {
		dmaengine_terminate_all(host->dma);
		dev_err(mmc_dev(host->mmc),
			"%s: read time out (status = 0x%08x)\n",
			__func__, stat);
	} else {
		dev_err(mmc_dev(host->mmc),
			"%s: write time out (status = 0x%08x)\n",
			__func__, stat);
		mxcmci_softreset(host);
	}

	/* Mark transfer as erroneus and inform the upper layers */

	if (host->data)
		host->data->error = -ETIMEDOUT;
	host->req = NULL;
	host->cmd = NULL;
	host->data = NULL;
	mmc_request_done(host->mmc, req);
}

static const struct mmc_host_ops mxcmci_ops = {
	.request		= mxcmci_request,
	.set_ios		= mxcmci_set_ios,
	.get_ro			= mxcmci_get_ro,
	.enable_sdio_irq	= mxcmci_enable_sdio_irq,
	.init_card		= mxcmci_init_card,
};

static int mxcmci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct mxcmci_host *host;
	struct resource *res;
	int ret = 0, irq;
	bool dat3_card_detect = false;
	dma_cap_mask_t mask;
	struct imxmmc_platform_data *pdata = pdev->dev.platform_data;

	pr_info("i.MX/MPC512x SDHC driver\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	mmc = mmc_alloc_host(sizeof(*host), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);

	host->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto out_free;
	}

	host->phys_base = res->start;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto out_free;
	mmc->ops = &mxcmci_ops;

	/* For devicetree parsing, the bus width is read from devicetree */
	if (pdata)
		mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ;
	else
		mmc->caps |= MMC_CAP_SDIO_IRQ;

	/* MMC core transfer sizes tunable parameters */
	mmc->max_blk_size = 2048;
	mmc->max_blk_count = 65535;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;

	host->devtype = (enum mxcmci_type)of_device_get_match_data(&pdev->dev);

	/* adjust max_segs after devtype detection */
	if (!is_mpc512x_mmc(host))
		mmc->max_segs = 64;

	host->mmc = mmc;
	host->pdata = pdata;
	spin_lock_init(&host->lock);

	if (pdata)
		dat3_card_detect = pdata->dat3_card_detect;
	else if (mmc_card_is_removable(mmc)
			&& !of_property_read_bool(pdev->dev.of_node, "cd-gpios"))
		dat3_card_detect = true;

	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		goto out_free;

	if (!mmc->ocr_avail) {
		if (pdata && pdata->ocr_avail)
			mmc->ocr_avail = pdata->ocr_avail;
		else
			mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	}

	if (dat3_card_detect)
		host->default_irq_mask =
			INT_CARD_INSERTION_EN | INT_CARD_REMOVAL_EN;
	else
		host->default_irq_mask = 0;

	host->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(host->clk_ipg)) {
		ret = PTR_ERR(host->clk_ipg);
		goto out_free;
	}

	host->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(host->clk_per)) {
		ret = PTR_ERR(host->clk_per);
		goto out_free;
	}

	ret = clk_prepare_enable(host->clk_per);
	if (ret)
		goto out_free;

	ret = clk_prepare_enable(host->clk_ipg);
	if (ret)
		goto out_clk_per_put;

	mxcmci_softreset(host);

	host->rev_no = mxcmci_readw(host, MMC_REG_REV_NO);
	if (host->rev_no != 0x400) {
		ret = -ENODEV;
		dev_err(mmc_dev(host->mmc), "wrong rev.no. 0x%08x. aborting.\n",
			host->rev_no);
		goto out_clk_put;
	}

	mmc->f_min = clk_get_rate(host->clk_per) >> 16;
	mmc->f_max = clk_get_rate(host->clk_per) >> 1;

	/* recommended in data sheet */
	mxcmci_writew(host, 0x2db4, MMC_REG_READ_TO);

	mxcmci_writel(host, host->default_irq_mask, MMC_REG_INT_CNTR);

	if (!host->pdata) {
		host->dma = dma_request_chan(&pdev->dev, "rx-tx");
		if (IS_ERR(host->dma)) {
			if (PTR_ERR(host->dma) == -EPROBE_DEFER) {
				ret = -EPROBE_DEFER;
				goto out_clk_put;
			}

			/* Ignore errors to fall back to PIO mode */
			host->dma = NULL;
		}
	} else {
		res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
		if (res) {
			host->dmareq = res->start;
			host->dma_data.peripheral_type = IMX_DMATYPE_SDHC;
			host->dma_data.priority = DMA_PRIO_LOW;
			host->dma_data.dma_request = host->dmareq;
			dma_cap_zero(mask);
			dma_cap_set(DMA_SLAVE, mask);
			host->dma = dma_request_channel(mask, filter, host);
		}
	}
	if (host->dma)
		mmc->max_seg_size = dma_get_max_seg_size(
				host->dma->device->dev);
	else
		dev_info(mmc_dev(host->mmc), "dma not available. Using PIO\n");

	INIT_WORK(&host->datawork, mxcmci_datawork);

	ret = devm_request_irq(&pdev->dev, irq, mxcmci_irq, 0,
			       dev_name(&pdev->dev), host);
	if (ret)
		goto out_free_dma;

	platform_set_drvdata(pdev, mmc);

	if (host->pdata && host->pdata->init) {
		ret = host->pdata->init(&pdev->dev, mxcmci_detect_irq,
				host->mmc);
		if (ret)
			goto out_free_dma;
	}

	timer_setup(&host->watchdog, mxcmci_watchdog, 0);

	mmc_add_host(mmc);

	return 0;

out_free_dma:
	if (host->dma)
		dma_release_channel(host->dma);

out_clk_put:
	clk_disable_unprepare(host->clk_ipg);
out_clk_per_put:
	clk_disable_unprepare(host->clk_per);

out_free:
	mmc_free_host(mmc);

	return ret;
}

static int mxcmci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct mxcmci_host *host = mmc_priv(mmc);

	mmc_remove_host(mmc);

	if (host->pdata && host->pdata->exit)
		host->pdata->exit(&pdev->dev, mmc);

	if (host->dma)
		dma_release_channel(host->dma);

	clk_disable_unprepare(host->clk_per);
	clk_disable_unprepare(host->clk_ipg);

	mmc_free_host(mmc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mxcmci_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct mxcmci_host *host = mmc_priv(mmc);

	clk_disable_unprepare(host->clk_per);
	clk_disable_unprepare(host->clk_ipg);
	return 0;
}

static int mxcmci_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct mxcmci_host *host = mmc_priv(mmc);
	int ret;

	ret = clk_prepare_enable(host->clk_per);
	if (ret)
		return ret;

	ret = clk_prepare_enable(host->clk_ipg);
	if (ret)
		clk_disable_unprepare(host->clk_per);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(mxcmci_pm_ops, mxcmci_suspend, mxcmci_resume);

static struct platform_driver mxcmci_driver = {
	.probe		= mxcmci_probe,
	.remove		= mxcmci_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.pm	= &mxcmci_pm_ops,
		.of_match_table	= mxcmci_of_match,
	}
};

module_platform_driver(mxcmci_driver);

MODULE_DESCRIPTION("i.MX Multimedia Card Interface Driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxc-mmc");
