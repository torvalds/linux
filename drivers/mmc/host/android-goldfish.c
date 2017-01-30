/*
 *  Copyright 2007, Google Inc.
 *  Copyright 2012, Intel Inc.
 *
 *  based on omap.c driver, which was
 *  Copyright (C) 2004 Nokia Corporation
 *  Written by Tuukka Tikkanen and Juha Yrjölä <juha.yrjola@nokia.com>
 *  Misc hacks here and there by Tony Lindgren <tony@atomide.com>
 *  Other hacks (DMA, SD, etc) by David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/major.h>

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>

#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/scatterlist.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/types.h>
#include <asm/io.h>
#include <linux/uaccess.h>

#define DRIVER_NAME "goldfish_mmc"

#define BUFFER_SIZE   16384

#define GOLDFISH_MMC_READ(host, addr)   (readl(host->reg_base + addr))
#define GOLDFISH_MMC_WRITE(host, addr, x)   (writel(x, host->reg_base + addr))

enum {
	/* status register */
	MMC_INT_STATUS	        = 0x00,
	/* set this to enable IRQ */
	MMC_INT_ENABLE	        = 0x04,
	/* set this to specify buffer address */
	MMC_SET_BUFFER          = 0x08,

	/* MMC command number */
	MMC_CMD	                = 0x0C,

	/* MMC argument */
	MMC_ARG	                = 0x10,

	/* MMC response (or R2 bits 0 - 31) */
	MMC_RESP_0		        = 0x14,

	/* MMC R2 response bits 32 - 63 */
	MMC_RESP_1		        = 0x18,

	/* MMC R2 response bits 64 - 95 */
	MMC_RESP_2		        = 0x1C,

	/* MMC R2 response bits 96 - 127 */
	MMC_RESP_3		        = 0x20,

	MMC_BLOCK_LENGTH        = 0x24,
	MMC_BLOCK_COUNT         = 0x28,

	/* MMC state flags */
	MMC_STATE               = 0x2C,

	/* MMC_INT_STATUS bits */

	MMC_STAT_END_OF_CMD     = 1U << 0,
	MMC_STAT_END_OF_DATA    = 1U << 1,
	MMC_STAT_STATE_CHANGE   = 1U << 2,
	MMC_STAT_CMD_TIMEOUT    = 1U << 3,

	/* MMC_STATE bits */
	MMC_STATE_INSERTED     = 1U << 0,
	MMC_STATE_READ_ONLY    = 1U << 1,
};

/*
 * Command types
 */
#define OMAP_MMC_CMDTYPE_BC	0
#define OMAP_MMC_CMDTYPE_BCR	1
#define OMAP_MMC_CMDTYPE_AC	2
#define OMAP_MMC_CMDTYPE_ADTC	3


struct goldfish_mmc_host {
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	struct mmc_host		*mmc;
	struct device		*dev;
	unsigned char		id; /* 16xx chips have 2 MMC blocks */
	void			*virt_base;
	unsigned int		phys_base;
	int			irq;
	unsigned char		bus_mode;
	unsigned char		hw_bus_mode;

	unsigned int		sg_len;
	unsigned		dma_done:1;
	unsigned		dma_in_use:1;

	void __iomem		*reg_base;
};

static inline int
goldfish_mmc_cover_is_open(struct goldfish_mmc_host *host)
{
	return 0;
}

static ssize_t
goldfish_mmc_show_cover_switch(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct goldfish_mmc_host *host = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", goldfish_mmc_cover_is_open(host) ? "open" :
		       "closed");
}

static DEVICE_ATTR(cover_switch, S_IRUGO, goldfish_mmc_show_cover_switch, NULL);

static void
goldfish_mmc_start_command(struct goldfish_mmc_host *host, struct mmc_command *cmd)
{
	u32 cmdreg;
	u32 resptype;
	u32 cmdtype;

	host->cmd = cmd;

	resptype = 0;
	cmdtype = 0;

	/* Our hardware needs to know exact type */
	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		break;
	case MMC_RSP_R1:
	case MMC_RSP_R1B:
		/* resp 1, 1b, 6, 7 */
		resptype = 1;
		break;
	case MMC_RSP_R2:
		resptype = 2;
		break;
	case MMC_RSP_R3:
		resptype = 3;
		break;
	default:
		dev_err(mmc_dev(host->mmc),
			"Invalid response type: %04x\n", mmc_resp_type(cmd));
		break;
	}

	if (mmc_cmd_type(cmd) == MMC_CMD_ADTC)
		cmdtype = OMAP_MMC_CMDTYPE_ADTC;
	else if (mmc_cmd_type(cmd) == MMC_CMD_BC)
		cmdtype = OMAP_MMC_CMDTYPE_BC;
	else if (mmc_cmd_type(cmd) == MMC_CMD_BCR)
		cmdtype = OMAP_MMC_CMDTYPE_BCR;
	else
		cmdtype = OMAP_MMC_CMDTYPE_AC;

	cmdreg = cmd->opcode | (resptype << 8) | (cmdtype << 12);

	if (host->bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdreg |= 1 << 6;

	if (cmd->flags & MMC_RSP_BUSY)
		cmdreg |= 1 << 11;

	if (host->data && !(host->data->flags & MMC_DATA_WRITE))
		cmdreg |= 1 << 15;

	GOLDFISH_MMC_WRITE(host, MMC_ARG, cmd->arg);
	GOLDFISH_MMC_WRITE(host, MMC_CMD, cmdreg);
}

static void goldfish_mmc_xfer_done(struct goldfish_mmc_host *host,
				   struct mmc_data *data)
{
	if (host->dma_in_use) {
		enum dma_data_direction dma_data_dir;

		if (data->flags & MMC_DATA_WRITE)
			dma_data_dir = DMA_TO_DEVICE;
		else
			dma_data_dir = DMA_FROM_DEVICE;

		if (dma_data_dir == DMA_FROM_DEVICE) {
			/*
			 * We don't really have DMA, so we need
			 * to copy from our platform driver buffer
			 */
			uint8_t *dest = (uint8_t *)sg_virt(data->sg);
			memcpy(dest, host->virt_base, data->sg->length);
		}
		host->data->bytes_xfered += data->sg->length;
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->sg_len,
			     dma_data_dir);
	}

	host->data = NULL;
	host->sg_len = 0;

	/*
	 * NOTE:  MMC layer will sometimes poll-wait CMD13 next, issuing
	 * dozens of requests until the card finishes writing data.
	 * It'd be cheaper to just wait till an EOFB interrupt arrives...
	 */

	if (!data->stop) {
		host->mrq = NULL;
		mmc_request_done(host->mmc, data->mrq);
		return;
	}

	goldfish_mmc_start_command(host, data->stop);
}

static void goldfish_mmc_end_of_data(struct goldfish_mmc_host *host,
				     struct mmc_data *data)
{
	if (!host->dma_in_use) {
		goldfish_mmc_xfer_done(host, data);
		return;
	}
	if (host->dma_done)
		goldfish_mmc_xfer_done(host, data);
}

static void goldfish_mmc_cmd_done(struct goldfish_mmc_host *host,
				  struct mmc_command *cmd)
{
	host->cmd = NULL;
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			/* response type 2 */
			cmd->resp[3] =
				GOLDFISH_MMC_READ(host, MMC_RESP_0);
			cmd->resp[2] =
				GOLDFISH_MMC_READ(host, MMC_RESP_1);
			cmd->resp[1] =
				GOLDFISH_MMC_READ(host, MMC_RESP_2);
			cmd->resp[0] =
				GOLDFISH_MMC_READ(host, MMC_RESP_3);
		} else {
			/* response types 1, 1b, 3, 4, 5, 6 */
			cmd->resp[0] =
				GOLDFISH_MMC_READ(host, MMC_RESP_0);
		}
	}

	if (host->data == NULL || cmd->error) {
		host->mrq = NULL;
		mmc_request_done(host->mmc, cmd->mrq);
	}
}

static irqreturn_t goldfish_mmc_irq(int irq, void *dev_id)
{
	struct goldfish_mmc_host *host = (struct goldfish_mmc_host *)dev_id;
	u16 status;
	int end_command = 0;
	int end_transfer = 0;
	int transfer_error = 0;
	int state_changed = 0;
	int cmd_timeout = 0;

	while ((status = GOLDFISH_MMC_READ(host, MMC_INT_STATUS)) != 0) {
		GOLDFISH_MMC_WRITE(host, MMC_INT_STATUS, status);

		if (status & MMC_STAT_END_OF_CMD)
			end_command = 1;

		if (status & MMC_STAT_END_OF_DATA)
			end_transfer = 1;

		if (status & MMC_STAT_STATE_CHANGE)
			state_changed = 1;

                if (status & MMC_STAT_CMD_TIMEOUT) {
			end_command = 0;
			cmd_timeout = 1;
                }
	}

	if (cmd_timeout) {
		struct mmc_request *mrq = host->mrq;
		mrq->cmd->error = -ETIMEDOUT;
		host->mrq = NULL;
		mmc_request_done(host->mmc, mrq);
	}

	if (end_command)
		goldfish_mmc_cmd_done(host, host->cmd);

	if (transfer_error)
		goldfish_mmc_xfer_done(host, host->data);
	else if (end_transfer) {
		host->dma_done = 1;
		goldfish_mmc_end_of_data(host, host->data);
	} else if (host->data != NULL) {
		/*
		 * WORKAROUND -- after porting this driver from 2.6 to 3.4,
		 * during device initialization, cases where host->data is
		 * non-null but end_transfer is false would occur. Doing
		 * nothing in such cases results in no further interrupts,
		 * and initialization failure.
		 * TODO -- find the real cause.
		 */
		host->dma_done = 1;
		goldfish_mmc_end_of_data(host, host->data);
	}

	if (state_changed) {
		u32 state = GOLDFISH_MMC_READ(host, MMC_STATE);
		pr_info("%s: Card detect now %d\n", __func__,
			(state & MMC_STATE_INSERTED));
		mmc_detect_change(host->mmc, 0);
	}

	if (!end_command && !end_transfer &&
	    !transfer_error && !state_changed && !cmd_timeout) {
		status = GOLDFISH_MMC_READ(host, MMC_INT_STATUS);
		dev_info(mmc_dev(host->mmc),"spurious irq 0x%04x\n", status);
		if (status != 0) {
			GOLDFISH_MMC_WRITE(host, MMC_INT_STATUS, status);
			GOLDFISH_MMC_WRITE(host, MMC_INT_ENABLE, 0);
		}
	}

	return IRQ_HANDLED;
}

static void goldfish_mmc_prepare_data(struct goldfish_mmc_host *host,
				      struct mmc_request *req)
{
	struct mmc_data *data = req->data;
	int block_size;
	unsigned sg_len;
	enum dma_data_direction dma_data_dir;

	host->data = data;
	if (data == NULL) {
		GOLDFISH_MMC_WRITE(host, MMC_BLOCK_LENGTH, 0);
		GOLDFISH_MMC_WRITE(host, MMC_BLOCK_COUNT, 0);
		host->dma_in_use = 0;
		return;
	}

	block_size = data->blksz;

	GOLDFISH_MMC_WRITE(host, MMC_BLOCK_COUNT, data->blocks - 1);
	GOLDFISH_MMC_WRITE(host, MMC_BLOCK_LENGTH, block_size - 1);

	/*
	 * Cope with calling layer confusion; it issues "single
	 * block" writes using multi-block scatterlists.
	 */
	sg_len = (data->blocks == 1) ? 1 : data->sg_len;

	if (data->flags & MMC_DATA_WRITE)
		dma_data_dir = DMA_TO_DEVICE;
	else
		dma_data_dir = DMA_FROM_DEVICE;

	host->sg_len = dma_map_sg(mmc_dev(host->mmc), data->sg,
				  sg_len, dma_data_dir);
	host->dma_done = 0;
	host->dma_in_use = 1;

	if (dma_data_dir == DMA_TO_DEVICE) {
		/*
		 * We don't really have DMA, so we need to copy to our
		 * platform driver buffer
		 */
		const uint8_t *src = (uint8_t *)sg_virt(data->sg);
		memcpy(host->virt_base, src, data->sg->length);
	}
}

static void goldfish_mmc_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct goldfish_mmc_host *host = mmc_priv(mmc);

	WARN_ON(host->mrq != NULL);

	host->mrq = req;
	goldfish_mmc_prepare_data(host, req);
	goldfish_mmc_start_command(host, req->cmd);

	/*
	 * This is to avoid accidentally being detected as an SDIO card
	 * in mmc_attach_sdio().
	 */
	if (req->cmd->opcode == SD_IO_SEND_OP_COND &&
	    req->cmd->flags == (MMC_RSP_SPI_R4 | MMC_RSP_R4 | MMC_CMD_BCR))
		req->cmd->error = -EINVAL;
}

static void goldfish_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct goldfish_mmc_host *host = mmc_priv(mmc);

	host->bus_mode = ios->bus_mode;
	host->hw_bus_mode = host->bus_mode;
}

static int goldfish_mmc_get_ro(struct mmc_host *mmc)
{
	uint32_t state;
	struct goldfish_mmc_host *host = mmc_priv(mmc);

	state = GOLDFISH_MMC_READ(host, MMC_STATE);
	return ((state & MMC_STATE_READ_ONLY) != 0);
}

static const struct mmc_host_ops goldfish_mmc_ops = {
	.request	= goldfish_mmc_request,
	.set_ios	= goldfish_mmc_set_ios,
	.get_ro		= goldfish_mmc_get_ro,
};

static int goldfish_mmc_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct goldfish_mmc_host *host = NULL;
	struct resource *res;
	int ret = 0;
	int irq;
	dma_addr_t buf_addr;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (res == NULL || irq < 0)
		return -ENXIO;

	mmc = mmc_alloc_host(sizeof(struct goldfish_mmc_host), &pdev->dev);
	if (mmc == NULL) {
		ret = -ENOMEM;
		goto err_alloc_host_failed;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;

	pr_err("mmc: Mapping %lX to %lX\n", (long)res->start, (long)res->end);
	host->reg_base = ioremap(res->start, resource_size(res));
	if (host->reg_base == NULL) {
		ret = -ENOMEM;
		goto ioremap_failed;
	}
	host->virt_base = dma_alloc_coherent(&pdev->dev, BUFFER_SIZE,
					     &buf_addr, GFP_KERNEL);

	if (host->virt_base == 0) {
		ret = -ENOMEM;
		goto dma_alloc_failed;
	}
	host->phys_base = buf_addr;

	host->id = pdev->id;
	host->irq = irq;

	mmc->ops = &goldfish_mmc_ops;
	mmc->f_min = 400000;
	mmc->f_max = 24000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps = MMC_CAP_4_BIT_DATA;

	/* Use scatterlist DMA to reduce per-transfer costs.
	 * NOTE max_seg_size assumption that small blocks aren't
	 * normally used (except e.g. for reading SD registers).
	 */
	mmc->max_segs = 32;
	mmc->max_blk_size = 2048;	/* MMC_BLOCK_LENGTH is 11 bits (+1) */
	mmc->max_blk_count = 2048;	/* MMC_BLOCK_COUNT is 11 bits (+1) */
	mmc->max_req_size = BUFFER_SIZE;
	mmc->max_seg_size = mmc->max_req_size;

	ret = request_irq(host->irq, goldfish_mmc_irq, 0, DRIVER_NAME, host);
	if (ret) {
		dev_err(&pdev->dev, "Failed IRQ Adding goldfish MMC\n");
		goto err_request_irq_failed;
	}

	host->dev = &pdev->dev;
	platform_set_drvdata(pdev, host);

	ret = device_create_file(&pdev->dev, &dev_attr_cover_switch);
	if (ret)
		dev_warn(mmc_dev(host->mmc),
			 "Unable to create sysfs attributes\n");

	GOLDFISH_MMC_WRITE(host, MMC_SET_BUFFER, host->phys_base);
	GOLDFISH_MMC_WRITE(host, MMC_INT_ENABLE,
			   MMC_STAT_END_OF_CMD | MMC_STAT_END_OF_DATA |
			   MMC_STAT_STATE_CHANGE | MMC_STAT_CMD_TIMEOUT);

	mmc_add_host(mmc);
	return 0;

err_request_irq_failed:
	dma_free_coherent(&pdev->dev, BUFFER_SIZE, host->virt_base,
			  host->phys_base);
dma_alloc_failed:
	iounmap(host->reg_base);
ioremap_failed:
	mmc_free_host(host->mmc);
err_alloc_host_failed:
	return ret;
}

static int goldfish_mmc_remove(struct platform_device *pdev)
{
	struct goldfish_mmc_host *host = platform_get_drvdata(pdev);

	BUG_ON(host == NULL);

	mmc_remove_host(host->mmc);
	free_irq(host->irq, host);
	dma_free_coherent(&pdev->dev, BUFFER_SIZE, host->virt_base, host->phys_base);
	iounmap(host->reg_base);
	mmc_free_host(host->mmc);
	return 0;
}

static struct platform_driver goldfish_mmc_driver = {
	.probe		= goldfish_mmc_probe,
	.remove		= goldfish_mmc_remove,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(goldfish_mmc_driver);
MODULE_LICENSE("GPL v2");
