/*
 * Copyright (C) 2013-2014 Renesas Electronics Europe Ltd.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/virtio.h>
#include <linux/workqueue.h>

#define USDHI6_SD_CMD		0x0000
#define USDHI6_SD_PORT_SEL	0x0004
#define USDHI6_SD_ARG		0x0008
#define USDHI6_SD_STOP		0x0010
#define USDHI6_SD_SECCNT	0x0014
#define USDHI6_SD_RSP10		0x0018
#define USDHI6_SD_RSP32		0x0020
#define USDHI6_SD_RSP54		0x0028
#define USDHI6_SD_RSP76		0x0030
#define USDHI6_SD_INFO1		0x0038
#define USDHI6_SD_INFO2		0x003c
#define USDHI6_SD_INFO1_MASK	0x0040
#define USDHI6_SD_INFO2_MASK	0x0044
#define USDHI6_SD_CLK_CTRL	0x0048
#define USDHI6_SD_SIZE		0x004c
#define USDHI6_SD_OPTION	0x0050
#define USDHI6_SD_ERR_STS1	0x0058
#define USDHI6_SD_ERR_STS2	0x005c
#define USDHI6_SD_BUF0		0x0060
#define USDHI6_SDIO_MODE	0x0068
#define USDHI6_SDIO_INFO1	0x006c
#define USDHI6_SDIO_INFO1_MASK	0x0070
#define USDHI6_CC_EXT_MODE	0x01b0
#define USDHI6_SOFT_RST		0x01c0
#define USDHI6_VERSION		0x01c4
#define USDHI6_HOST_MODE	0x01c8
#define USDHI6_SDIF_MODE	0x01cc

#define USDHI6_SD_CMD_APP		0x0040
#define USDHI6_SD_CMD_MODE_RSP_AUTO	0x0000
#define USDHI6_SD_CMD_MODE_RSP_NONE	0x0300
#define USDHI6_SD_CMD_MODE_RSP_R1	0x0400	/* Also R5, R6, R7 */
#define USDHI6_SD_CMD_MODE_RSP_R1B	0x0500	/* R1b */
#define USDHI6_SD_CMD_MODE_RSP_R2	0x0600
#define USDHI6_SD_CMD_MODE_RSP_R3	0x0700	/* Also R4 */
#define USDHI6_SD_CMD_DATA		0x0800
#define USDHI6_SD_CMD_READ		0x1000
#define USDHI6_SD_CMD_MULTI		0x2000
#define USDHI6_SD_CMD_CMD12_AUTO_OFF	0x4000

#define USDHI6_CC_EXT_MODE_SDRW		BIT(1)

#define USDHI6_SD_INFO1_RSP_END		BIT(0)
#define USDHI6_SD_INFO1_ACCESS_END	BIT(2)
#define USDHI6_SD_INFO1_CARD_OUT	BIT(3)
#define USDHI6_SD_INFO1_CARD_IN		BIT(4)
#define USDHI6_SD_INFO1_CD		BIT(5)
#define USDHI6_SD_INFO1_WP		BIT(7)
#define USDHI6_SD_INFO1_D3_CARD_OUT	BIT(8)
#define USDHI6_SD_INFO1_D3_CARD_IN	BIT(9)

#define USDHI6_SD_INFO2_CMD_ERR		BIT(0)
#define USDHI6_SD_INFO2_CRC_ERR		BIT(1)
#define USDHI6_SD_INFO2_END_ERR		BIT(2)
#define USDHI6_SD_INFO2_TOUT		BIT(3)
#define USDHI6_SD_INFO2_IWA_ERR		BIT(4)
#define USDHI6_SD_INFO2_IRA_ERR		BIT(5)
#define USDHI6_SD_INFO2_RSP_TOUT	BIT(6)
#define USDHI6_SD_INFO2_SDDAT0		BIT(7)
#define USDHI6_SD_INFO2_BRE		BIT(8)
#define USDHI6_SD_INFO2_BWE		BIT(9)
#define USDHI6_SD_INFO2_SCLKDIVEN	BIT(13)
#define USDHI6_SD_INFO2_CBSY		BIT(14)
#define USDHI6_SD_INFO2_ILA		BIT(15)

#define USDHI6_SD_INFO1_CARD_INSERT (USDHI6_SD_INFO1_CARD_IN | USDHI6_SD_INFO1_D3_CARD_IN)
#define USDHI6_SD_INFO1_CARD_EJECT (USDHI6_SD_INFO1_CARD_OUT | USDHI6_SD_INFO1_D3_CARD_OUT)
#define USDHI6_SD_INFO1_CARD (USDHI6_SD_INFO1_CARD_INSERT | USDHI6_SD_INFO1_CARD_EJECT)
#define USDHI6_SD_INFO1_CARD_CD (USDHI6_SD_INFO1_CARD_IN | USDHI6_SD_INFO1_CARD_OUT)

#define USDHI6_SD_INFO2_ERR	(USDHI6_SD_INFO2_CMD_ERR |	\
	USDHI6_SD_INFO2_CRC_ERR | USDHI6_SD_INFO2_END_ERR |	\
	USDHI6_SD_INFO2_TOUT | USDHI6_SD_INFO2_IWA_ERR |	\
	USDHI6_SD_INFO2_IRA_ERR | USDHI6_SD_INFO2_RSP_TOUT |	\
	USDHI6_SD_INFO2_ILA)

#define USDHI6_SD_INFO1_IRQ	(USDHI6_SD_INFO1_RSP_END | USDHI6_SD_INFO1_ACCESS_END | \
				 USDHI6_SD_INFO1_CARD)

#define USDHI6_SD_INFO2_IRQ	(USDHI6_SD_INFO2_ERR | USDHI6_SD_INFO2_BRE | \
				 USDHI6_SD_INFO2_BWE | 0x0800 | USDHI6_SD_INFO2_ILA)

#define USDHI6_SD_CLK_CTRL_SCLKEN	BIT(8)

#define USDHI6_SD_STOP_STP		BIT(0)
#define USDHI6_SD_STOP_SEC		BIT(8)

#define USDHI6_SDIO_INFO1_IOIRQ		BIT(0)
#define USDHI6_SDIO_INFO1_EXPUB52	BIT(14)
#define USDHI6_SDIO_INFO1_EXWT		BIT(15)

#define USDHI6_SD_ERR_STS1_CRC_NO_ERROR	BIT(13)

#define USDHI6_SOFT_RST_RESERVED	(BIT(1) | BIT(2))
#define USDHI6_SOFT_RST_RESET		BIT(0)

#define USDHI6_SD_OPTION_TIMEOUT_SHIFT	4
#define USDHI6_SD_OPTION_TIMEOUT_MASK	(0xf << USDHI6_SD_OPTION_TIMEOUT_SHIFT)
#define USDHI6_SD_OPTION_WIDTH_1	BIT(15)

#define USDHI6_SD_PORT_SEL_PORTS_SHIFT	8

#define USDHI6_SD_CLK_CTRL_DIV_MASK	0xff

#define USDHI6_SDIO_INFO1_IRQ	(USDHI6_SDIO_INFO1_IOIRQ | 3 | \
				 USDHI6_SDIO_INFO1_EXPUB52 | USDHI6_SDIO_INFO1_EXWT)

#define USDHI6_MIN_DMA 64

enum usdhi6_wait_for {
	USDHI6_WAIT_FOR_REQUEST,
	USDHI6_WAIT_FOR_CMD,
	USDHI6_WAIT_FOR_MREAD,
	USDHI6_WAIT_FOR_MWRITE,
	USDHI6_WAIT_FOR_READ,
	USDHI6_WAIT_FOR_WRITE,
	USDHI6_WAIT_FOR_DATA_END,
	USDHI6_WAIT_FOR_STOP,
	USDHI6_WAIT_FOR_DMA,
};

struct usdhi6_page {
	struct page *page;
	void *mapped;		/* mapped page */
};

struct usdhi6_host {
	struct mmc_host *mmc;
	struct mmc_request *mrq;
	void __iomem *base;
	struct clk *clk;

	/* SG memory handling */

	/* Common for multiple and single block requests */
	struct usdhi6_page pg;	/* current page from an SG */
	void *blk_page;		/* either a mapped page, or the bounce buffer */
	size_t offset;		/* offset within a page, including sg->offset */

	/* Blocks, crossing a page boundary */
	size_t head_len;
	struct usdhi6_page head_pg;

	/* A bounce buffer for unaligned blocks or blocks, crossing a page boundary */
	struct scatterlist bounce_sg;
	u8 bounce_buf[512];

	/* Multiple block requests only */
	struct scatterlist *sg;	/* current SG segment */
	int page_idx;		/* page index within an SG segment */

	enum usdhi6_wait_for wait;
	u32 status_mask;
	u32 status2_mask;
	u32 sdio_mask;
	u32 io_error;
	u32 irq_status;
	unsigned long imclk;
	unsigned long rate;
	bool app_cmd;

	/* Timeout handling */
	struct delayed_work timeout_work;
	unsigned long timeout;

	/* DMA support */
	struct dma_chan *chan_rx;
	struct dma_chan *chan_tx;
	bool dma_active;
};

/*			I/O primitives					*/

static void usdhi6_write(struct usdhi6_host *host, u32 reg, u32 data)
{
	iowrite32(data, host->base + reg);
	dev_vdbg(mmc_dev(host->mmc), "%s(0x%p + 0x%x) = 0x%x\n", __func__,
		host->base, reg, data);
}

static void usdhi6_write16(struct usdhi6_host *host, u32 reg, u16 data)
{
	iowrite16(data, host->base + reg);
	dev_vdbg(mmc_dev(host->mmc), "%s(0x%p + 0x%x) = 0x%x\n", __func__,
		host->base, reg, data);
}

static u32 usdhi6_read(struct usdhi6_host *host, u32 reg)
{
	u32 data = ioread32(host->base + reg);
	dev_vdbg(mmc_dev(host->mmc), "%s(0x%p + 0x%x) = 0x%x\n", __func__,
		host->base, reg, data);
	return data;
}

static u16 usdhi6_read16(struct usdhi6_host *host, u32 reg)
{
	u16 data = ioread16(host->base + reg);
	dev_vdbg(mmc_dev(host->mmc), "%s(0x%p + 0x%x) = 0x%x\n", __func__,
		host->base, reg, data);
	return data;
}

static void usdhi6_irq_enable(struct usdhi6_host *host, u32 info1, u32 info2)
{
	host->status_mask = USDHI6_SD_INFO1_IRQ & ~info1;
	host->status2_mask = USDHI6_SD_INFO2_IRQ & ~info2;
	usdhi6_write(host, USDHI6_SD_INFO1_MASK, host->status_mask);
	usdhi6_write(host, USDHI6_SD_INFO2_MASK, host->status2_mask);
}

static void usdhi6_wait_for_resp(struct usdhi6_host *host)
{
	usdhi6_irq_enable(host, USDHI6_SD_INFO1_RSP_END |
			  USDHI6_SD_INFO1_ACCESS_END | USDHI6_SD_INFO1_CARD_CD,
			  USDHI6_SD_INFO2_ERR);
}

static void usdhi6_wait_for_brwe(struct usdhi6_host *host, bool read)
{
	usdhi6_irq_enable(host, USDHI6_SD_INFO1_ACCESS_END |
			  USDHI6_SD_INFO1_CARD_CD, USDHI6_SD_INFO2_ERR |
			  (read ? USDHI6_SD_INFO2_BRE : USDHI6_SD_INFO2_BWE));
}

static void usdhi6_only_cd(struct usdhi6_host *host)
{
	/* Mask all except card hotplug */
	usdhi6_irq_enable(host, USDHI6_SD_INFO1_CARD_CD, 0);
}

static void usdhi6_mask_all(struct usdhi6_host *host)
{
	usdhi6_irq_enable(host, 0, 0);
}

static int usdhi6_error_code(struct usdhi6_host *host)
{
	u32 err;

	usdhi6_write(host, USDHI6_SD_STOP, USDHI6_SD_STOP_STP);

	if (host->io_error &
	    (USDHI6_SD_INFO2_RSP_TOUT | USDHI6_SD_INFO2_TOUT)) {
		u32 rsp54 = usdhi6_read(host, USDHI6_SD_RSP54);
		int opc = host->mrq ? host->mrq->cmd->opcode : -1;

		err = usdhi6_read(host, USDHI6_SD_ERR_STS2);
		/* Response timeout is often normal, don't spam the log */
		if (host->wait == USDHI6_WAIT_FOR_CMD)
			dev_dbg(mmc_dev(host->mmc),
				"T-out sts 0x%x, resp 0x%x, state %u, CMD%d\n",
				err, rsp54, host->wait, opc);
		else
			dev_warn(mmc_dev(host->mmc),
				 "T-out sts 0x%x, resp 0x%x, state %u, CMD%d\n",
				 err, rsp54, host->wait, opc);
		return -ETIMEDOUT;
	}

	err = usdhi6_read(host, USDHI6_SD_ERR_STS1);
	if (err != USDHI6_SD_ERR_STS1_CRC_NO_ERROR)
		dev_warn(mmc_dev(host->mmc), "Err sts 0x%x, state %u, CMD%d\n",
			 err, host->wait, host->mrq ? host->mrq->cmd->opcode : -1);
	if (host->io_error & USDHI6_SD_INFO2_ILA)
		return -EILSEQ;

	return -EIO;
}

/*			Scatter-Gather management			*/

/*
 * In PIO mode we have to map each page separately, using kmap(). That way
 * adjacent pages are mapped to non-adjacent virtual addresses. That's why we
 * have to use a bounce buffer for blocks, crossing page boundaries. Such blocks
 * have been observed with an SDIO WiFi card (b43 driver).
 */
static void usdhi6_blk_bounce(struct usdhi6_host *host,
			      struct scatterlist *sg)
{
	struct mmc_data *data = host->mrq->data;
	size_t blk_head = host->head_len;

	dev_dbg(mmc_dev(host->mmc), "%s(): CMD%u of %u SG: %ux%u @ 0x%x\n",
		__func__, host->mrq->cmd->opcode, data->sg_len,
		data->blksz, data->blocks, sg->offset);

	host->head_pg.page	= host->pg.page;
	host->head_pg.mapped	= host->pg.mapped;
	host->pg.page		= nth_page(host->pg.page, 1);
	host->pg.mapped		= kmap(host->pg.page);

	host->blk_page = host->bounce_buf;
	host->offset = 0;

	if (data->flags & MMC_DATA_READ)
		return;

	memcpy(host->bounce_buf, host->head_pg.mapped + PAGE_SIZE - blk_head,
	       blk_head);
	memcpy(host->bounce_buf + blk_head, host->pg.mapped,
	       data->blksz - blk_head);
}

/* Only called for multiple block IO */
static void usdhi6_sg_prep(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_data *data = mrq->data;

	usdhi6_write(host, USDHI6_SD_SECCNT, data->blocks);

	host->sg = data->sg;
	/* TODO: if we always map, this is redundant */
	host->offset = host->sg->offset;
}

/* Map the first page in an SG segment: common for multiple and single block IO */
static void *usdhi6_sg_map(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;
	struct scatterlist *sg = data->sg_len > 1 ? host->sg : data->sg;
	size_t head = PAGE_SIZE - sg->offset;
	size_t blk_head = head % data->blksz;

	WARN(host->pg.page, "%p not properly unmapped!\n", host->pg.page);
	if (WARN(sg_dma_len(sg) % data->blksz,
		 "SG size %u isn't a multiple of block size %u\n",
		 sg_dma_len(sg), data->blksz))
		return NULL;

	host->pg.page = sg_page(sg);
	host->pg.mapped = kmap(host->pg.page);
	host->offset = sg->offset;

	/*
	 * Block size must be a power of 2 for multi-block transfers,
	 * therefore blk_head is equal for all pages in this SG
	 */
	host->head_len = blk_head;

	if (head < data->blksz)
		/*
		 * The first block in the SG crosses a page boundary.
		 * Max blksz = 512, so blocks can only span 2 pages
		 */
		usdhi6_blk_bounce(host, sg);
	else
		host->blk_page = host->pg.mapped;

	dev_dbg(mmc_dev(host->mmc), "Mapped %p (%lx) at %p + %u for CMD%u @ 0x%p\n",
		host->pg.page, page_to_pfn(host->pg.page), host->pg.mapped,
		sg->offset, host->mrq->cmd->opcode, host->mrq);

	return host->blk_page + host->offset;
}

/* Unmap the current page: common for multiple and single block IO */
static void usdhi6_sg_unmap(struct usdhi6_host *host, bool force)
{
	struct mmc_data *data = host->mrq->data;
	struct page *page = host->head_pg.page;

	if (page) {
		/* Previous block was cross-page boundary */
		struct scatterlist *sg = data->sg_len > 1 ?
			host->sg : data->sg;
		size_t blk_head = host->head_len;

		if (!data->error && data->flags & MMC_DATA_READ) {
			memcpy(host->head_pg.mapped + PAGE_SIZE - blk_head,
			       host->bounce_buf, blk_head);
			memcpy(host->pg.mapped, host->bounce_buf + blk_head,
			       data->blksz - blk_head);
		}

		flush_dcache_page(page);
		kunmap(page);

		host->head_pg.page = NULL;

		if (!force && sg_dma_len(sg) + sg->offset >
		    (host->page_idx << PAGE_SHIFT) + data->blksz - blk_head)
			/* More blocks in this SG, don't unmap the next page */
			return;
	}

	page = host->pg.page;
	if (!page)
		return;

	flush_dcache_page(page);
	kunmap(page);

	host->pg.page = NULL;
}

/* Called from MMC_WRITE_MULTIPLE_BLOCK or MMC_READ_MULTIPLE_BLOCK */
static void usdhi6_sg_advance(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;
	size_t done, total;

	/* New offset: set at the end of the previous block */
	if (host->head_pg.page) {
		/* Finished a cross-page block, jump to the new page */
		host->page_idx++;
		host->offset = data->blksz - host->head_len;
		host->blk_page = host->pg.mapped;
		usdhi6_sg_unmap(host, false);
	} else {
		host->offset += data->blksz;
		/* The completed block didn't cross a page boundary */
		if (host->offset == PAGE_SIZE) {
			/* If required, we'll map the page below */
			host->offset = 0;
			host->page_idx++;
		}
	}

	/*
	 * Now host->blk_page + host->offset point at the end of our last block
	 * and host->page_idx is the index of the page, in which our new block
	 * is located, if any
	 */

	done = (host->page_idx << PAGE_SHIFT) + host->offset;
	total = host->sg->offset + sg_dma_len(host->sg);

	dev_dbg(mmc_dev(host->mmc), "%s(): %zu of %zu @ %zu\n", __func__,
		done, total, host->offset);

	if (done < total && host->offset) {
		/* More blocks in this page */
		if (host->offset + data->blksz > PAGE_SIZE)
			/* We approached at a block, that spans 2 pages */
			usdhi6_blk_bounce(host, host->sg);

		return;
	}

	/* Finished current page or an SG segment */
	usdhi6_sg_unmap(host, false);

	if (done == total) {
		/*
		 * End of an SG segment or the complete SG: jump to the next
		 * segment, we'll map it later in usdhi6_blk_read() or
		 * usdhi6_blk_write()
		 */
		struct scatterlist *next = sg_next(host->sg);

		host->page_idx = 0;

		if (!next)
			host->wait = USDHI6_WAIT_FOR_DATA_END;
		host->sg = next;

		if (WARN(next && sg_dma_len(next) % data->blksz,
			 "SG size %u isn't a multiple of block size %u\n",
			 sg_dma_len(next), data->blksz))
			data->error = -EINVAL;

		return;
	}

	/* We cannot get here after crossing a page border */

	/* Next page in the same SG */
	host->pg.page = nth_page(sg_page(host->sg), host->page_idx);
	host->pg.mapped = kmap(host->pg.page);
	host->blk_page = host->pg.mapped;

	dev_dbg(mmc_dev(host->mmc), "Mapped %p (%lx) at %p for CMD%u @ 0x%p\n",
		host->pg.page, page_to_pfn(host->pg.page), host->pg.mapped,
		host->mrq->cmd->opcode, host->mrq);
}

/*			DMA handling					*/

static void usdhi6_dma_release(struct usdhi6_host *host)
{
	host->dma_active = false;
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
}

static void usdhi6_dma_stop_unmap(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;

	if (!host->dma_active)
		return;

	usdhi6_write(host, USDHI6_CC_EXT_MODE, 0);
	host->dma_active = false;

	if (data->flags & MMC_DATA_READ)
		dma_unmap_sg(host->chan_rx->device->dev, data->sg,
			     data->sg_len, DMA_FROM_DEVICE);
	else
		dma_unmap_sg(host->chan_tx->device->dev, data->sg,
			     data->sg_len, DMA_TO_DEVICE);
}

static void usdhi6_dma_complete(void *arg)
{
	struct usdhi6_host *host = arg;
	struct mmc_request *mrq = host->mrq;

	if (WARN(!mrq || !mrq->data, "%s: NULL data in DMA completion for %p!\n",
		 dev_name(mmc_dev(host->mmc)), mrq))
		return;

	dev_dbg(mmc_dev(host->mmc), "%s(): CMD%u DMA completed\n", __func__,
		mrq->cmd->opcode);

	usdhi6_dma_stop_unmap(host);
	usdhi6_wait_for_brwe(host, mrq->data->flags & MMC_DATA_READ);
}

static int usdhi6_dma_setup(struct usdhi6_host *host, struct dma_chan *chan,
			    enum dma_transfer_direction dir)
{
	struct mmc_data *data = host->mrq->data;
	struct scatterlist *sg = data->sg;
	struct dma_async_tx_descriptor *desc = NULL;
	dma_cookie_t cookie = -EINVAL;
	enum dma_data_direction data_dir;
	int ret;

	switch (dir) {
	case DMA_MEM_TO_DEV:
		data_dir = DMA_TO_DEVICE;
		break;
	case DMA_DEV_TO_MEM:
		data_dir = DMA_FROM_DEVICE;
		break;
	default:
		return -EINVAL;
	}

	ret = dma_map_sg(chan->device->dev, sg, data->sg_len, data_dir);
	if (ret > 0) {
		host->dma_active = true;
		desc = dmaengine_prep_slave_sg(chan, sg, ret, dir,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = usdhi6_dma_complete;
		desc->callback_param = host;
		cookie = dmaengine_submit(desc);
	}

	dev_dbg(mmc_dev(host->mmc), "%s(): mapped %d -> %d, cookie %d @ %p\n",
		__func__, data->sg_len, ret, cookie, desc);

	if (cookie < 0) {
		/* DMA failed, fall back to PIO */
		if (ret >= 0)
			ret = cookie;
		usdhi6_dma_release(host);
		dev_warn(mmc_dev(host->mmc),
			 "DMA failed: %d, falling back to PIO\n", ret);
	}

	return cookie;
}

static int usdhi6_dma_start(struct usdhi6_host *host)
{
	if (!host->chan_rx || !host->chan_tx)
		return -ENODEV;

	if (host->mrq->data->flags & MMC_DATA_READ)
		return usdhi6_dma_setup(host, host->chan_rx, DMA_DEV_TO_MEM);

	return usdhi6_dma_setup(host, host->chan_tx, DMA_MEM_TO_DEV);
}

static void usdhi6_dma_kill(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;

	dev_dbg(mmc_dev(host->mmc), "%s(): SG of %u: %ux%u\n",
		__func__, data->sg_len, data->blocks, data->blksz);
	/* Abort DMA */
	if (data->flags & MMC_DATA_READ)
		dmaengine_terminate_all(host->chan_rx);
	else
		dmaengine_terminate_all(host->chan_tx);
}

static void usdhi6_dma_check_error(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;

	dev_dbg(mmc_dev(host->mmc), "%s(): IO error %d, status 0x%x\n",
		__func__, host->io_error, usdhi6_read(host, USDHI6_SD_INFO1));

	if (host->io_error) {
		data->error = usdhi6_error_code(host);
		data->bytes_xfered = 0;
		usdhi6_dma_kill(host);
		usdhi6_dma_release(host);
		dev_warn(mmc_dev(host->mmc),
			 "DMA failed: %d, falling back to PIO\n", data->error);
		return;
	}

	/*
	 * The datasheet tells us to check a response from the card, whereas
	 * responses only come after the command phase, not after the data
	 * phase. Let's check anyway.
	 */
	if (host->irq_status & USDHI6_SD_INFO1_RSP_END)
		dev_warn(mmc_dev(host->mmc), "Unexpected response received!\n");
}

static void usdhi6_dma_kick(struct usdhi6_host *host)
{
	if (host->mrq->data->flags & MMC_DATA_READ)
		dma_async_issue_pending(host->chan_rx);
	else
		dma_async_issue_pending(host->chan_tx);
}

static void usdhi6_dma_request(struct usdhi6_host *host, phys_addr_t start)
{
	struct dma_slave_config cfg = {
		.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
	};
	int ret;

	host->chan_tx = dma_request_slave_channel(mmc_dev(host->mmc), "tx");
	dev_dbg(mmc_dev(host->mmc), "%s: TX: got channel %p\n", __func__,
		host->chan_tx);

	if (!host->chan_tx)
		return;

	cfg.direction = DMA_MEM_TO_DEV;
	cfg.dst_addr = start + USDHI6_SD_BUF0;
	cfg.dst_maxburst = 128;	/* 128 words * 4 bytes = 512 bytes */
	cfg.src_addr = 0;
	ret = dmaengine_slave_config(host->chan_tx, &cfg);
	if (ret < 0)
		goto e_release_tx;

	host->chan_rx = dma_request_slave_channel(mmc_dev(host->mmc), "rx");
	dev_dbg(mmc_dev(host->mmc), "%s: RX: got channel %p\n", __func__,
		host->chan_rx);

	if (!host->chan_rx)
		goto e_release_tx;

	cfg.direction = DMA_DEV_TO_MEM;
	cfg.src_addr = cfg.dst_addr;
	cfg.src_maxburst = 128;	/* 128 words * 4 bytes = 512 bytes */
	cfg.dst_addr = 0;
	ret = dmaengine_slave_config(host->chan_rx, &cfg);
	if (ret < 0)
		goto e_release_rx;

	return;

e_release_rx:
	dma_release_channel(host->chan_rx);
	host->chan_rx = NULL;
e_release_tx:
	dma_release_channel(host->chan_tx);
	host->chan_tx = NULL;
}

/*			API helpers					*/

static void usdhi6_clk_set(struct usdhi6_host *host, struct mmc_ios *ios)
{
	unsigned long rate = ios->clock;
	u32 val;
	unsigned int i;

	for (i = 1000; i; i--) {
		if (usdhi6_read(host, USDHI6_SD_INFO2) & USDHI6_SD_INFO2_SCLKDIVEN)
			break;
		usleep_range(10, 100);
	}

	if (!i) {
		dev_err(mmc_dev(host->mmc), "SD bus busy, clock set aborted\n");
		return;
	}

	val = usdhi6_read(host, USDHI6_SD_CLK_CTRL) & ~USDHI6_SD_CLK_CTRL_DIV_MASK;

	if (rate) {
		unsigned long new_rate;

		if (host->imclk <= rate) {
			if (ios->timing != MMC_TIMING_UHS_DDR50) {
				/* Cannot have 1-to-1 clock in DDR mode */
				new_rate = host->imclk;
				val |= 0xff;
			} else {
				new_rate = host->imclk / 2;
			}
		} else {
			unsigned long div =
				roundup_pow_of_two(DIV_ROUND_UP(host->imclk, rate));
			val |= div >> 2;
			new_rate = host->imclk / div;
		}

		if (host->rate == new_rate)
			return;

		host->rate = new_rate;

		dev_dbg(mmc_dev(host->mmc), "target %lu, div %u, set %lu\n",
			rate, (val & 0xff) << 2, new_rate);
	}

	/*
	 * if old or new rate is equal to input rate, have to switch the clock
	 * off before changing and on after
	 */
	if (host->imclk == rate || host->imclk == host->rate || !rate)
		usdhi6_write(host, USDHI6_SD_CLK_CTRL,
			     val & ~USDHI6_SD_CLK_CTRL_SCLKEN);

	if (!rate) {
		host->rate = 0;
		return;
	}

	usdhi6_write(host, USDHI6_SD_CLK_CTRL, val);

	if (host->imclk == rate || host->imclk == host->rate ||
	    !(val & USDHI6_SD_CLK_CTRL_SCLKEN))
		usdhi6_write(host, USDHI6_SD_CLK_CTRL,
			     val | USDHI6_SD_CLK_CTRL_SCLKEN);
}

static void usdhi6_set_power(struct usdhi6_host *host, struct mmc_ios *ios)
{
	struct mmc_host *mmc = host->mmc;

	if (!IS_ERR(mmc->supply.vmmc))
		/* Errors ignored... */
		mmc_regulator_set_ocr(mmc, mmc->supply.vmmc,
				      ios->power_mode ? ios->vdd : 0);
}

static int usdhi6_reset(struct usdhi6_host *host)
{
	int i;

	usdhi6_write(host, USDHI6_SOFT_RST, USDHI6_SOFT_RST_RESERVED);
	cpu_relax();
	usdhi6_write(host, USDHI6_SOFT_RST, USDHI6_SOFT_RST_RESERVED | USDHI6_SOFT_RST_RESET);
	for (i = 1000; i; i--)
		if (usdhi6_read(host, USDHI6_SOFT_RST) & USDHI6_SOFT_RST_RESET)
			break;

	return i ? 0 : -ETIMEDOUT;
}

static void usdhi6_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct usdhi6_host *host = mmc_priv(mmc);
	u32 option, mode;
	int ret;

	dev_dbg(mmc_dev(mmc), "%uHz, OCR: %u, power %u, bus-width %u, timing %u\n",
		ios->clock, ios->vdd, ios->power_mode, ios->bus_width, ios->timing);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		usdhi6_set_power(host, ios);
		usdhi6_only_cd(host);
		break;
	case MMC_POWER_UP:
		/*
		 * We only also touch USDHI6_SD_OPTION from .request(), which
		 * cannot race with MMC_POWER_UP
		 */
		ret = usdhi6_reset(host);
		if (ret < 0) {
			dev_err(mmc_dev(mmc), "Cannot reset the interface!\n");
		} else {
			usdhi6_set_power(host, ios);
			usdhi6_only_cd(host);
		}
		break;
	case MMC_POWER_ON:
		option = usdhi6_read(host, USDHI6_SD_OPTION);
		/*
		 * The eMMC standard only allows 4 or 8 bits in the DDR mode,
		 * the same probably holds for SD cards. We check here anyway,
		 * since the datasheet explicitly requires 4 bits for DDR.
		 */
		if (ios->bus_width == MMC_BUS_WIDTH_1) {
			if (ios->timing == MMC_TIMING_UHS_DDR50)
				dev_err(mmc_dev(mmc),
					"4 bits are required for DDR\n");
			option |= USDHI6_SD_OPTION_WIDTH_1;
			mode = 0;
		} else {
			option &= ~USDHI6_SD_OPTION_WIDTH_1;
			mode = ios->timing == MMC_TIMING_UHS_DDR50;
		}
		usdhi6_write(host, USDHI6_SD_OPTION, option);
		usdhi6_write(host, USDHI6_SDIF_MODE, mode);
		break;
	}

	if (host->rate != ios->clock)
		usdhi6_clk_set(host, ios);
}

/* This is data timeout. Response timeout is fixed to 640 clock cycles */
static void usdhi6_timeout_set(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;
	u32 val;
	unsigned long ticks;

	if (!mrq->data)
		ticks = host->rate / 1000 * mrq->cmd->busy_timeout;
	else
		ticks = host->rate / 1000000 * (mrq->data->timeout_ns / 1000) +
			mrq->data->timeout_clks;

	if (!ticks || ticks > 1 << 27)
		/* Max timeout */
		val = 14;
	else if (ticks < 1 << 13)
		/* Min timeout */
		val = 0;
	else
		val = order_base_2(ticks) - 13;

	dev_dbg(mmc_dev(host->mmc), "Set %s timeout %lu ticks @ %lu Hz\n",
		mrq->data ? "data" : "cmd", ticks, host->rate);

	/* Timeout Counter mask: 0xf0 */
	usdhi6_write(host, USDHI6_SD_OPTION, (val << USDHI6_SD_OPTION_TIMEOUT_SHIFT) |
		     (usdhi6_read(host, USDHI6_SD_OPTION) & ~USDHI6_SD_OPTION_TIMEOUT_MASK));
}

static void usdhi6_request_done(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_data *data = mrq->data;

	if (WARN(host->pg.page || host->head_pg.page,
		 "Page %p or %p not unmapped: wait %u, CMD%d(%c) @ +0x%zx %ux%u in SG%u!\n",
		 host->pg.page, host->head_pg.page, host->wait, mrq->cmd->opcode,
		 data ? (data->flags & MMC_DATA_READ ? 'R' : 'W') : '-',
		 data ? host->offset : 0, data ? data->blocks : 0,
		 data ? data->blksz : 0, data ? data->sg_len : 0))
		usdhi6_sg_unmap(host, true);

	if (mrq->cmd->error ||
	    (data && data->error) ||
	    (mrq->stop && mrq->stop->error))
		dev_dbg(mmc_dev(host->mmc), "%s(CMD%d: %ux%u): err %d %d %d\n",
			__func__, mrq->cmd->opcode, data ? data->blocks : 0,
			data ? data->blksz : 0,
			mrq->cmd->error,
			data ? data->error : 1,
			mrq->stop ? mrq->stop->error : 1);

	/* Disable DMA */
	usdhi6_write(host, USDHI6_CC_EXT_MODE, 0);
	host->wait = USDHI6_WAIT_FOR_REQUEST;
	host->mrq = NULL;

	mmc_request_done(host->mmc, mrq);
}

static int usdhi6_cmd_flags(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = mrq->cmd;
	u16 opc = cmd->opcode;

	if (host->app_cmd) {
		host->app_cmd = false;
		opc |= USDHI6_SD_CMD_APP;
	}

	if (mrq->data) {
		opc |= USDHI6_SD_CMD_DATA;

		if (mrq->data->flags & MMC_DATA_READ)
			opc |= USDHI6_SD_CMD_READ;

		if (cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
		    cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ||
		    (cmd->opcode == SD_IO_RW_EXTENDED &&
		     mrq->data->blocks > 1)) {
			opc |= USDHI6_SD_CMD_MULTI;
			if (!mrq->stop)
				opc |= USDHI6_SD_CMD_CMD12_AUTO_OFF;
		}

		switch (mmc_resp_type(cmd)) {
		case MMC_RSP_NONE:
			opc |= USDHI6_SD_CMD_MODE_RSP_NONE;
			break;
		case MMC_RSP_R1:
			opc |= USDHI6_SD_CMD_MODE_RSP_R1;
			break;
		case MMC_RSP_R1B:
			opc |= USDHI6_SD_CMD_MODE_RSP_R1B;
			break;
		case MMC_RSP_R2:
			opc |= USDHI6_SD_CMD_MODE_RSP_R2;
			break;
		case MMC_RSP_R3:
			opc |= USDHI6_SD_CMD_MODE_RSP_R3;
			break;
		default:
			dev_warn(mmc_dev(host->mmc),
				 "Unknown response type %d\n",
				 mmc_resp_type(cmd));
			return -EINVAL;
		}
	}

	return opc;
}

static int usdhi6_rq_start(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	int opc = usdhi6_cmd_flags(host);
	int i;

	if (opc < 0)
		return opc;

	for (i = 1000; i; i--) {
		if (!(usdhi6_read(host, USDHI6_SD_INFO2) & USDHI6_SD_INFO2_CBSY))
			break;
		usleep_range(10, 100);
	}

	if (!i) {
		dev_dbg(mmc_dev(host->mmc), "Command active, request aborted\n");
		return -EAGAIN;
	}

	if (data) {
		bool use_dma;
		int ret = 0;

		host->page_idx = 0;

		if (cmd->opcode == SD_IO_RW_EXTENDED && data->blocks > 1) {
			switch (data->blksz) {
			case 512:
				break;
			case 32:
			case 64:
			case 128:
			case 256:
				if (mrq->stop)
					ret = -EINVAL;
				break;
			default:
				ret = -EINVAL;
			}
		} else if ((cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
			    cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK) &&
			   data->blksz != 512) {
			ret = -EINVAL;
		}

		if (ret < 0) {
			dev_warn(mmc_dev(host->mmc), "%s(): %u blocks of %u bytes\n",
				 __func__, data->blocks, data->blksz);
			return -EINVAL;
		}

		if (cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
		    cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ||
		    (cmd->opcode == SD_IO_RW_EXTENDED &&
		     data->blocks > 1))
			usdhi6_sg_prep(host);

		usdhi6_write(host, USDHI6_SD_SIZE, data->blksz);

		if ((data->blksz >= USDHI6_MIN_DMA ||
		     data->blocks > 1) &&
		    (data->blksz % 4 ||
		     data->sg->offset % 4))
			dev_dbg(mmc_dev(host->mmc),
				"Bad SG of %u: %ux%u @ %u\n", data->sg_len,
				data->blksz, data->blocks, data->sg->offset);

		/* Enable DMA for USDHI6_MIN_DMA bytes or more */
		use_dma = data->blksz >= USDHI6_MIN_DMA &&
			!(data->blksz % 4) &&
			usdhi6_dma_start(host) >= DMA_MIN_COOKIE;

		if (use_dma)
			usdhi6_write(host, USDHI6_CC_EXT_MODE, USDHI6_CC_EXT_MODE_SDRW);

		dev_dbg(mmc_dev(host->mmc),
			"%s(): request opcode %u, %u blocks of %u bytes in %u segments, %s %s @+0x%x%s\n",
			__func__, cmd->opcode, data->blocks, data->blksz,
			data->sg_len, use_dma ? "DMA" : "PIO",
			data->flags & MMC_DATA_READ ? "read" : "write",
			data->sg->offset, mrq->stop ? " + stop" : "");
	} else {
		dev_dbg(mmc_dev(host->mmc), "%s(): request opcode %u\n",
			__func__, cmd->opcode);
	}

	/* We have to get a command completion interrupt with DMA too */
	usdhi6_wait_for_resp(host);

	host->wait = USDHI6_WAIT_FOR_CMD;
	schedule_delayed_work(&host->timeout_work, host->timeout);

	/* SEC bit is required to enable block counting by the core */
	usdhi6_write(host, USDHI6_SD_STOP,
		     data && data->blocks > 1 ? USDHI6_SD_STOP_SEC : 0);
	usdhi6_write(host, USDHI6_SD_ARG, cmd->arg);

	/* Kick command execution */
	usdhi6_write(host, USDHI6_SD_CMD, opc);

	return 0;
}

static void usdhi6_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct usdhi6_host *host = mmc_priv(mmc);
	int ret;

	cancel_delayed_work_sync(&host->timeout_work);

	host->mrq = mrq;
	host->sg = NULL;

	usdhi6_timeout_set(host);
	ret = usdhi6_rq_start(host);
	if (ret < 0) {
		mrq->cmd->error = ret;
		usdhi6_request_done(host);
	}
}

static int usdhi6_get_cd(struct mmc_host *mmc)
{
	struct usdhi6_host *host = mmc_priv(mmc);
	/* Read is atomic, no need to lock */
	u32 status = usdhi6_read(host, USDHI6_SD_INFO1) & USDHI6_SD_INFO1_CD;

/*
 *	level	status.CD	CD_ACTIVE_HIGH	card present
 *	1	0		0		0
 *	1	0		1		1
 *	0	1		0		1
 *	0	1		1		0
 */
	return !status ^ !(mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH);
}

static int usdhi6_get_ro(struct mmc_host *mmc)
{
	struct usdhi6_host *host = mmc_priv(mmc);
	/* No locking as above */
	u32 status = usdhi6_read(host, USDHI6_SD_INFO1) & USDHI6_SD_INFO1_WP;

/*
 *	level	status.WP	RO_ACTIVE_HIGH	card read-only
 *	1	0		0		0
 *	1	0		1		1
 *	0	1		0		1
 *	0	1		1		0
 */
	return !status ^ !(mmc->caps2 & MMC_CAP2_RO_ACTIVE_HIGH);
}

static void usdhi6_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct usdhi6_host *host = mmc_priv(mmc);

	dev_dbg(mmc_dev(mmc), "%s(): %sable\n", __func__, enable ? "en" : "dis");

	if (enable) {
		host->sdio_mask = USDHI6_SDIO_INFO1_IRQ & ~USDHI6_SDIO_INFO1_IOIRQ;
		usdhi6_write(host, USDHI6_SDIO_INFO1_MASK, host->sdio_mask);
		usdhi6_write(host, USDHI6_SDIO_MODE, 1);
	} else {
		usdhi6_write(host, USDHI6_SDIO_MODE, 0);
		usdhi6_write(host, USDHI6_SDIO_INFO1_MASK, USDHI6_SDIO_INFO1_IRQ);
		host->sdio_mask = USDHI6_SDIO_INFO1_IRQ;
	}
}

static struct mmc_host_ops usdhi6_ops = {
	.request	= usdhi6_request,
	.set_ios	= usdhi6_set_ios,
	.get_cd		= usdhi6_get_cd,
	.get_ro		= usdhi6_get_ro,
	.enable_sdio_irq = usdhi6_enable_sdio_irq,
};

/*			State machine handlers				*/

static void usdhi6_resp_cmd12(struct usdhi6_host *host)
{
	struct mmc_command *cmd = host->mrq->stop;
	cmd->resp[0] = usdhi6_read(host, USDHI6_SD_RSP10);
}

static void usdhi6_resp_read(struct usdhi6_host *host)
{
	struct mmc_command *cmd = host->mrq->cmd;
	u32 *rsp = cmd->resp, tmp = 0;
	int i;

/*
 * RSP10	39-8
 * RSP32	71-40
 * RSP54	103-72
 * RSP76	127-104
 * R2-type response:
 * resp[0]	= r[127..96]
 * resp[1]	= r[95..64]
 * resp[2]	= r[63..32]
 * resp[3]	= r[31..0]
 * Other responses:
 * resp[0]	= r[39..8]
 */

	if (mmc_resp_type(cmd) == MMC_RSP_NONE)
		return;

	if (!(host->irq_status & USDHI6_SD_INFO1_RSP_END)) {
		dev_err(mmc_dev(host->mmc),
			"CMD%d: response expected but is missing!\n", cmd->opcode);
		return;
	}

	if (mmc_resp_type(cmd) & MMC_RSP_136)
		for (i = 0; i < 4; i++) {
			if (i)
				rsp[3 - i] = tmp >> 24;
			tmp = usdhi6_read(host, USDHI6_SD_RSP10 + i * 8);
			rsp[3 - i] |= tmp << 8;
		}
	else if (cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
		 cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
		/* Read RSP54 to avoid conflict with auto CMD12 */
		rsp[0] = usdhi6_read(host, USDHI6_SD_RSP54);
	else
		rsp[0] = usdhi6_read(host, USDHI6_SD_RSP10);

	dev_dbg(mmc_dev(host->mmc), "Response 0x%x\n", rsp[0]);
}

static int usdhi6_blk_read(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;
	u32 *p;
	int i, rest;

	if (host->io_error) {
		data->error = usdhi6_error_code(host);
		goto error;
	}

	if (host->pg.page) {
		p = host->blk_page + host->offset;
	} else {
		p = usdhi6_sg_map(host);
		if (!p) {
			data->error = -ENOMEM;
			goto error;
		}
	}

	for (i = 0; i < data->blksz / 4; i++, p++)
		*p = usdhi6_read(host, USDHI6_SD_BUF0);

	rest = data->blksz % 4;
	for (i = 0; i < (rest + 1) / 2; i++) {
		u16 d = usdhi6_read16(host, USDHI6_SD_BUF0);
		((u8 *)p)[2 * i] = ((u8 *)&d)[0];
		if (rest > 1 && !i)
			((u8 *)p)[2 * i + 1] = ((u8 *)&d)[1];
	}

	return 0;

error:
	dev_dbg(mmc_dev(host->mmc), "%s(): %d\n", __func__, data->error);
	host->wait = USDHI6_WAIT_FOR_REQUEST;
	return data->error;
}

static int usdhi6_blk_write(struct usdhi6_host *host)
{
	struct mmc_data *data = host->mrq->data;
	u32 *p;
	int i, rest;

	if (host->io_error) {
		data->error = usdhi6_error_code(host);
		goto error;
	}

	if (host->pg.page) {
		p = host->blk_page + host->offset;
	} else {
		p = usdhi6_sg_map(host);
		if (!p) {
			data->error = -ENOMEM;
			goto error;
		}
	}

	for (i = 0; i < data->blksz / 4; i++, p++)
		usdhi6_write(host, USDHI6_SD_BUF0, *p);

	rest = data->blksz % 4;
	for (i = 0; i < (rest + 1) / 2; i++) {
		u16 d;
		((u8 *)&d)[0] = ((u8 *)p)[2 * i];
		if (rest > 1 && !i)
			((u8 *)&d)[1] = ((u8 *)p)[2 * i + 1];
		else
			((u8 *)&d)[1] = 0;
		usdhi6_write16(host, USDHI6_SD_BUF0, d);
	}

	return 0;

error:
	dev_dbg(mmc_dev(host->mmc), "%s(): %d\n", __func__, data->error);
	host->wait = USDHI6_WAIT_FOR_REQUEST;
	return data->error;
}

static int usdhi6_stop_cmd(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;

	switch (mrq->cmd->opcode) {
	case MMC_READ_MULTIPLE_BLOCK:
	case MMC_WRITE_MULTIPLE_BLOCK:
		if (mrq->stop->opcode == MMC_STOP_TRANSMISSION) {
			host->wait = USDHI6_WAIT_FOR_STOP;
			return 0;
		}
		/* Unsupported STOP command */
	default:
		dev_err(mmc_dev(host->mmc),
			"unsupported stop CMD%d for CMD%d\n",
			mrq->stop->opcode, mrq->cmd->opcode);
		mrq->stop->error = -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}

static bool usdhi6_end_cmd(struct usdhi6_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = mrq->cmd;

	if (host->io_error) {
		cmd->error = usdhi6_error_code(host);
		return false;
	}

	usdhi6_resp_read(host);

	if (!mrq->data)
		return false;

	if (host->dma_active) {
		usdhi6_dma_kick(host);
		if (!mrq->stop)
			host->wait = USDHI6_WAIT_FOR_DMA;
		else if (usdhi6_stop_cmd(host) < 0)
			return false;
	} else if (mrq->data->flags & MMC_DATA_READ) {
		if (cmd->opcode == MMC_READ_MULTIPLE_BLOCK ||
		    (cmd->opcode == SD_IO_RW_EXTENDED &&
		     mrq->data->blocks > 1))
			host->wait = USDHI6_WAIT_FOR_MREAD;
		else
			host->wait = USDHI6_WAIT_FOR_READ;
	} else {
		if (cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK ||
		    (cmd->opcode == SD_IO_RW_EXTENDED &&
		     mrq->data->blocks > 1))
			host->wait = USDHI6_WAIT_FOR_MWRITE;
		else
			host->wait = USDHI6_WAIT_FOR_WRITE;
	}

	return true;
}

static bool usdhi6_read_block(struct usdhi6_host *host)
{
	/* ACCESS_END IRQ is already unmasked */
	int ret = usdhi6_blk_read(host);

	/*
	 * Have to force unmapping both pages: the single block could have been
	 * cross-page, in which case for single-block IO host->page_idx == 0.
	 * So, if we don't force, the second page won't be unmapped.
	 */
	usdhi6_sg_unmap(host, true);

	if (ret < 0)
		return false;

	host->wait = USDHI6_WAIT_FOR_DATA_END;
	return true;
}

static bool usdhi6_mread_block(struct usdhi6_host *host)
{
	int ret = usdhi6_blk_read(host);

	if (ret < 0)
		return false;

	usdhi6_sg_advance(host);

	return !host->mrq->data->error &&
		(host->wait != USDHI6_WAIT_FOR_DATA_END || !host->mrq->stop);
}

static bool usdhi6_write_block(struct usdhi6_host *host)
{
	int ret = usdhi6_blk_write(host);

	/* See comment in usdhi6_read_block() */
	usdhi6_sg_unmap(host, true);

	if (ret < 0)
		return false;

	host->wait = USDHI6_WAIT_FOR_DATA_END;
	return true;
}

static bool usdhi6_mwrite_block(struct usdhi6_host *host)
{
	int ret = usdhi6_blk_write(host);

	if (ret < 0)
		return false;

	usdhi6_sg_advance(host);

	return !host->mrq->data->error &&
		(host->wait != USDHI6_WAIT_FOR_DATA_END || !host->mrq->stop);
}

/*			Interrupt & timeout handlers			*/

static irqreturn_t usdhi6_sd_bh(int irq, void *dev_id)
{
	struct usdhi6_host *host = dev_id;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;
	bool io_wait = false;

	cancel_delayed_work_sync(&host->timeout_work);

	mrq = host->mrq;
	if (!mrq)
		return IRQ_HANDLED;

	cmd = mrq->cmd;
	data = mrq->data;

	switch (host->wait) {
	case USDHI6_WAIT_FOR_REQUEST:
		/* We're too late, the timeout has already kicked in */
		return IRQ_HANDLED;
	case USDHI6_WAIT_FOR_CMD:
		/* Wait for data? */
		io_wait = usdhi6_end_cmd(host);
		break;
	case USDHI6_WAIT_FOR_MREAD:
		/* Wait for more data? */
		io_wait = usdhi6_mread_block(host);
		break;
	case USDHI6_WAIT_FOR_READ:
		/* Wait for data end? */
		io_wait = usdhi6_read_block(host);
		break;
	case USDHI6_WAIT_FOR_MWRITE:
		/* Wait data to write? */
		io_wait = usdhi6_mwrite_block(host);
		break;
	case USDHI6_WAIT_FOR_WRITE:
		/* Wait for data end? */
		io_wait = usdhi6_write_block(host);
		break;
	case USDHI6_WAIT_FOR_DMA:
		usdhi6_dma_check_error(host);
		break;
	case USDHI6_WAIT_FOR_STOP:
		usdhi6_write(host, USDHI6_SD_STOP, 0);
		if (host->io_error) {
			int ret = usdhi6_error_code(host);
			if (mrq->stop)
				mrq->stop->error = ret;
			else
				mrq->data->error = ret;
			dev_warn(mmc_dev(host->mmc), "%s(): %d\n", __func__, ret);
			break;
		}
		usdhi6_resp_cmd12(host);
		mrq->stop->error = 0;
		break;
	case USDHI6_WAIT_FOR_DATA_END:
		if (host->io_error) {
			mrq->data->error = usdhi6_error_code(host);
			dev_warn(mmc_dev(host->mmc), "%s(): %d\n", __func__,
				 mrq->data->error);
		}
		break;
	default:
		cmd->error = -EFAULT;
		dev_err(mmc_dev(host->mmc), "Invalid state %u\n", host->wait);
		usdhi6_request_done(host);
		return IRQ_HANDLED;
	}

	if (io_wait) {
		schedule_delayed_work(&host->timeout_work, host->timeout);
		/* Wait for more data or ACCESS_END */
		if (!host->dma_active)
			usdhi6_wait_for_brwe(host, mrq->data->flags & MMC_DATA_READ);
		return IRQ_HANDLED;
	}

	if (!cmd->error) {
		if (data) {
			if (!data->error) {
				if (host->wait != USDHI6_WAIT_FOR_STOP &&
				    host->mrq->stop &&
				    !host->mrq->stop->error &&
				    !usdhi6_stop_cmd(host)) {
					/* Sending STOP */
					usdhi6_wait_for_resp(host);

					schedule_delayed_work(&host->timeout_work,
							      host->timeout);

					return IRQ_HANDLED;
				}

				data->bytes_xfered = data->blocks * data->blksz;
			} else {
				/* Data error: might need to unmap the last page */
				dev_warn(mmc_dev(host->mmc), "%s(): data error %d\n",
					 __func__, data->error);
				usdhi6_sg_unmap(host, true);
			}
		} else if (cmd->opcode == MMC_APP_CMD) {
			host->app_cmd = true;
		}
	}

	usdhi6_request_done(host);

	return IRQ_HANDLED;
}

static irqreturn_t usdhi6_sd(int irq, void *dev_id)
{
	struct usdhi6_host *host = dev_id;
	u16 status, status2, error;

	status = usdhi6_read(host, USDHI6_SD_INFO1) & ~host->status_mask &
		~USDHI6_SD_INFO1_CARD;
	status2 = usdhi6_read(host, USDHI6_SD_INFO2) & ~host->status2_mask;

	usdhi6_only_cd(host);

	dev_dbg(mmc_dev(host->mmc),
		"IRQ status = 0x%08x, status2 = 0x%08x\n", status, status2);

	if (!status && !status2)
		return IRQ_NONE;

	error = status2 & USDHI6_SD_INFO2_ERR;

	/* Ack / clear interrupts */
	if (USDHI6_SD_INFO1_IRQ & status)
		usdhi6_write(host, USDHI6_SD_INFO1,
			     0xffff & ~(USDHI6_SD_INFO1_IRQ & status));

	if (USDHI6_SD_INFO2_IRQ & status2) {
		if (error)
			/* In error cases BWE and BRE aren't cleared automatically */
			status2 |= USDHI6_SD_INFO2_BWE | USDHI6_SD_INFO2_BRE;

		usdhi6_write(host, USDHI6_SD_INFO2,
			     0xffff & ~(USDHI6_SD_INFO2_IRQ & status2));
	}

	host->io_error = error;
	host->irq_status = status;

	if (error) {
		/* Don't pollute the log with unsupported command timeouts */
		if (host->wait != USDHI6_WAIT_FOR_CMD ||
		    error != USDHI6_SD_INFO2_RSP_TOUT)
			dev_warn(mmc_dev(host->mmc),
				 "%s(): INFO2 error bits 0x%08x\n",
				 __func__, error);
		else
			dev_dbg(mmc_dev(host->mmc),
				"%s(): INFO2 error bits 0x%08x\n",
				__func__, error);
	}

	return IRQ_WAKE_THREAD;
}

static irqreturn_t usdhi6_sdio(int irq, void *dev_id)
{
	struct usdhi6_host *host = dev_id;
	u32 status = usdhi6_read(host, USDHI6_SDIO_INFO1) & ~host->sdio_mask;

	dev_dbg(mmc_dev(host->mmc), "%s(): status 0x%x\n", __func__, status);

	if (!status)
		return IRQ_NONE;

	usdhi6_write(host, USDHI6_SDIO_INFO1, ~status);

	mmc_signal_sdio_irq(host->mmc);

	return IRQ_HANDLED;
}

static irqreturn_t usdhi6_cd(int irq, void *dev_id)
{
	struct usdhi6_host *host = dev_id;
	struct mmc_host *mmc = host->mmc;
	u16 status;

	/* We're only interested in hotplug events here */
	status = usdhi6_read(host, USDHI6_SD_INFO1) & ~host->status_mask &
		USDHI6_SD_INFO1_CARD;

	if (!status)
		return IRQ_NONE;

	/* Ack */
	usdhi6_write(host, USDHI6_SD_INFO1, ~status);

	if (!work_pending(&mmc->detect.work) &&
	    (((status & USDHI6_SD_INFO1_CARD_INSERT) &&
	      !mmc->card) ||
	     ((status & USDHI6_SD_INFO1_CARD_EJECT) &&
	      mmc->card)))
		mmc_detect_change(mmc, msecs_to_jiffies(100));

	return IRQ_HANDLED;
}

/*
 * Actually this should not be needed, if the built-in timeout works reliably in
 * the both PIO cases and DMA never fails. But if DMA does fail, a timeout
 * handler might be the only way to catch the error.
 */
static void usdhi6_timeout_work(struct work_struct *work)
{
	struct delayed_work *d = to_delayed_work(work);
	struct usdhi6_host *host = container_of(d, struct usdhi6_host, timeout_work);
	struct mmc_request *mrq = host->mrq;
	struct mmc_data *data = mrq ? mrq->data : NULL;
	struct scatterlist *sg;

	dev_warn(mmc_dev(host->mmc),
		 "%s timeout wait %u CMD%d: IRQ 0x%08x:0x%08x, last IRQ 0x%08x\n",
		 host->dma_active ? "DMA" : "PIO",
		 host->wait, mrq ? mrq->cmd->opcode : -1,
		 usdhi6_read(host, USDHI6_SD_INFO1),
		 usdhi6_read(host, USDHI6_SD_INFO2), host->irq_status);

	if (host->dma_active) {
		usdhi6_dma_kill(host);
		usdhi6_dma_stop_unmap(host);
	}

	switch (host->wait) {
	default:
		dev_err(mmc_dev(host->mmc), "Invalid state %u\n", host->wait);
		/* mrq can be NULL in this actually impossible case */
	case USDHI6_WAIT_FOR_CMD:
		usdhi6_error_code(host);
		if (mrq)
			mrq->cmd->error = -ETIMEDOUT;
		break;
	case USDHI6_WAIT_FOR_STOP:
		usdhi6_error_code(host);
		mrq->stop->error = -ETIMEDOUT;
		break;
	case USDHI6_WAIT_FOR_DMA:
	case USDHI6_WAIT_FOR_MREAD:
	case USDHI6_WAIT_FOR_MWRITE:
	case USDHI6_WAIT_FOR_READ:
	case USDHI6_WAIT_FOR_WRITE:
		sg = host->sg ?: data->sg;
		dev_dbg(mmc_dev(host->mmc),
			"%c: page #%u @ +0x%zx %ux%u in SG%u. Current SG %u bytes @ %u\n",
			data->flags & MMC_DATA_READ ? 'R' : 'W', host->page_idx,
			host->offset, data->blocks, data->blksz, data->sg_len,
			sg_dma_len(sg), sg->offset);
		usdhi6_sg_unmap(host, true);
		/*
		 * If USDHI6_WAIT_FOR_DATA_END times out, we have already unmapped
		 * the page
		 */
	case USDHI6_WAIT_FOR_DATA_END:
		usdhi6_error_code(host);
		data->error = -ETIMEDOUT;
	}

	if (mrq)
		usdhi6_request_done(host);
}

/*			 Probe / release				*/

static const struct of_device_id usdhi6_of_match[] = {
	{.compatible = "renesas,usdhi6rol0"},
	{}
};
MODULE_DEVICE_TABLE(of, usdhi6_of_match);

static int usdhi6_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mmc_host *mmc;
	struct usdhi6_host *host;
	struct resource *res;
	int irq_cd, irq_sd, irq_sdio;
	u32 version;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	irq_cd = platform_get_irq_byname(pdev, "card detect");
	irq_sd = platform_get_irq_byname(pdev, "data");
	irq_sdio = platform_get_irq_byname(pdev, "SDIO");
	if (irq_sd < 0 || irq_sdio < 0)
		return -ENODEV;

	mmc = mmc_alloc_host(sizeof(struct usdhi6_host), dev);
	if (!mmc)
		return -ENOMEM;

	ret = mmc_regulator_get_supply(mmc);
	if (ret == -EPROBE_DEFER)
		goto e_free_mmc;

	ret = mmc_of_parse(mmc);
	if (ret < 0)
		goto e_free_mmc;

	host		= mmc_priv(mmc);
	host->mmc	= mmc;
	host->wait	= USDHI6_WAIT_FOR_REQUEST;
	host->timeout	= msecs_to_jiffies(4000);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(host->base)) {
		ret = PTR_ERR(host->base);
		goto e_free_mmc;
	}

	host->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(host->clk)) {
		ret = PTR_ERR(host->clk);
		goto e_free_mmc;
	}

	host->imclk = clk_get_rate(host->clk);

	ret = clk_prepare_enable(host->clk);
	if (ret < 0)
		goto e_free_mmc;

	version = usdhi6_read(host, USDHI6_VERSION);
	if ((version & 0xfff) != 0xa0d) {
		dev_err(dev, "Version not recognized %x\n", version);
		goto e_clk_off;
	}

	dev_info(dev, "A USDHI6ROL0 SD host detected with %d ports\n",
		 usdhi6_read(host, USDHI6_SD_PORT_SEL) >> USDHI6_SD_PORT_SEL_PORTS_SHIFT);

	usdhi6_mask_all(host);

	if (irq_cd >= 0) {
		ret = devm_request_irq(dev, irq_cd, usdhi6_cd, 0,
				       dev_name(dev), host);
		if (ret < 0)
			goto e_clk_off;
	} else {
		mmc->caps |= MMC_CAP_NEEDS_POLL;
	}

	ret = devm_request_threaded_irq(dev, irq_sd, usdhi6_sd, usdhi6_sd_bh, 0,
			       dev_name(dev), host);
	if (ret < 0)
		goto e_clk_off;

	ret = devm_request_irq(dev, irq_sdio, usdhi6_sdio, 0,
			       dev_name(dev), host);
	if (ret < 0)
		goto e_clk_off;

	INIT_DELAYED_WORK(&host->timeout_work, usdhi6_timeout_work);

	usdhi6_dma_request(host, res->start);

	mmc->ops = &usdhi6_ops;
	mmc->caps |= MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED |
		MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50 | MMC_CAP_SDIO_IRQ;
	/* Set .max_segs to some random number. Feel free to adjust. */
	mmc->max_segs = 32;
	mmc->max_blk_size = 512;
	mmc->max_req_size = PAGE_CACHE_SIZE * mmc->max_segs;
	mmc->max_blk_count = mmc->max_req_size / mmc->max_blk_size;
	/*
	 * Setting .max_seg_size to 1 page would simplify our page-mapping code,
	 * But OTOH, having large segments makes DMA more efficient. We could
	 * check, whether we managed to get DMA and fall back to 1 page
	 * segments, but if we do manage to obtain DMA and then it fails at
	 * run-time and we fall back to PIO, we will continue getting large
	 * segments. So, we wouldn't be able to get rid of the code anyway.
	 */
	mmc->max_seg_size = mmc->max_req_size;
	if (!mmc->f_max)
		mmc->f_max = host->imclk;
	mmc->f_min = host->imclk / 512;

	platform_set_drvdata(pdev, host);

	ret = mmc_add_host(mmc);
	if (ret < 0)
		goto e_clk_off;

	return 0;

e_clk_off:
	clk_disable_unprepare(host->clk);
e_free_mmc:
	mmc_free_host(mmc);

	return ret;
}

static int usdhi6_remove(struct platform_device *pdev)
{
	struct usdhi6_host *host = platform_get_drvdata(pdev);

	mmc_remove_host(host->mmc);

	usdhi6_mask_all(host);
	cancel_delayed_work_sync(&host->timeout_work);
	usdhi6_dma_release(host);
	clk_disable_unprepare(host->clk);
	mmc_free_host(host->mmc);

	return 0;
}

static struct platform_driver usdhi6_driver = {
	.probe		= usdhi6_probe,
	.remove		= usdhi6_remove,
	.driver		= {
		.name	= "usdhi6rol0",
		.of_match_table = usdhi6_of_match,
	},
};

module_platform_driver(usdhi6_driver);

MODULE_DESCRIPTION("Renesas usdhi6rol0 SD/SDIO host driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:usdhi6rol0");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
