// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2012 Marvell International Ltd.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/platform_data/mmp_dma.h>
#include <linux/dmapool.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/of.h>

#include "dmaengine.h"

#define DCSR		0x0000
#define DALGN		0x00a0
#define DINT		0x00f0
#define DDADR		0x0200
#define DSADR(n)	(0x0204 + ((n) << 4))
#define DTADR(n)	(0x0208 + ((n) << 4))
#define DCMD		0x020c

#define DCSR_RUN	BIT(31)	/* Run Bit (read / write) */
#define DCSR_NODESC	BIT(30)	/* No-Descriptor Fetch (read / write) */
#define DCSR_STOPIRQEN	BIT(29)	/* Stop Interrupt Enable (read / write) */
#define DCSR_REQPEND	BIT(8)	/* Request Pending (read-only) */
#define DCSR_STOPSTATE	BIT(3)	/* Stop State (read-only) */
#define DCSR_ENDINTR	BIT(2)	/* End Interrupt (read / write) */
#define DCSR_STARTINTR	BIT(1)	/* Start Interrupt (read / write) */
#define DCSR_BUSERR	BIT(0)	/* Bus Error Interrupt (read / write) */

#define DCSR_EORIRQEN	BIT(28)	/* End of Receive Interrupt Enable (R/W) */
#define DCSR_EORJMPEN	BIT(27)	/* Jump to next descriptor on EOR */
#define DCSR_EORSTOPEN	BIT(26)	/* STOP on an EOR */
#define DCSR_SETCMPST	BIT(25)	/* Set Descriptor Compare Status */
#define DCSR_CLRCMPST	BIT(24)	/* Clear Descriptor Compare Status */
#define DCSR_CMPST	BIT(10)	/* The Descriptor Compare Status */
#define DCSR_EORINTR	BIT(9)	/* The end of Receive */

#define DRCMR(n)	((((n) < 64) ? 0x0100 : 0x1100) + (((n) & 0x3f) << 2))
#define DRCMR_MAPVLD	BIT(7)	/* Map Valid (read / write) */
#define DRCMR_CHLNUM	0x1f	/* mask for Channel Number (read / write) */

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor (mask) */
#define DDADR_STOP	BIT(0)	/* Stop (read / write) */

#define DCMD_INCSRCADDR	BIT(31)	/* Source Address Increment Setting. */
#define DCMD_INCTRGADDR	BIT(30)	/* Target Address Increment Setting. */
#define DCMD_FLOWSRC	BIT(29)	/* Flow Control by the source. */
#define DCMD_FLOWTRG	BIT(28)	/* Flow Control by the target. */
#define DCMD_STARTIRQEN	BIT(22)	/* Start Interrupt Enable */
#define DCMD_ENDIRQEN	BIT(21)	/* End Interrupt Enable */
#define DCMD_ENDIAN	BIT(18)	/* Device Endian-ness. */
#define DCMD_BURST8	(1 << 16)	/* 8 byte burst */
#define DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define DCMD_WIDTH1	(1 << 14)	/* 1 byte width */
#define DCMD_WIDTH2	(2 << 14)	/* 2 byte width (HalfWord) */
#define DCMD_WIDTH4	(3 << 14)	/* 4 byte width (Word) */
#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */

#define PDMA_MAX_DESC_BYTES	DCMD_LENGTH

struct mmp_pdma_desc_hw {
	u32 ddadr;	/* Points to the next descriptor + flags */
	u32 dsadr;	/* DSADR value for the current transfer */
	u32 dtadr;	/* DTADR value for the current transfer */
	u32 dcmd;	/* DCMD value for the current transfer */
} __aligned(32);

struct mmp_pdma_desc_sw {
	struct mmp_pdma_desc_hw desc;
	struct list_head node;
	struct list_head tx_list;
	struct dma_async_tx_descriptor async_tx;
};

struct mmp_pdma_phy;

struct mmp_pdma_chan {
	struct device *dev;
	struct dma_chan chan;
	struct dma_async_tx_descriptor desc;
	struct mmp_pdma_phy *phy;
	enum dma_transfer_direction dir;
	struct dma_slave_config slave_config;

	struct mmp_pdma_desc_sw *cyclic_first;	/* first desc_sw if channel
						 * is in cyclic mode */

	/* channel's basic info */
	struct tasklet_struct tasklet;
	u32 dcmd;
	u32 drcmr;
	u32 dev_addr;

	/* list for desc */
	spinlock_t desc_lock;		/* Descriptor list lock */
	struct list_head chain_pending;	/* Link descriptors queue for pending */
	struct list_head chain_running;	/* Link descriptors queue for running */
	bool idle;			/* channel statue machine */
	bool byte_align;

	struct dma_pool *desc_pool;	/* Descriptors pool */
};

struct mmp_pdma_phy {
	int idx;
	void __iomem *base;
	struct mmp_pdma_chan *vchan;
};

struct mmp_pdma_device {
	int				dma_channels;
	void __iomem			*base;
	struct device			*dev;
	struct dma_device		device;
	struct mmp_pdma_phy		*phy;
	spinlock_t phy_lock; /* protect alloc/free phy channels */
};

#define tx_to_mmp_pdma_desc(tx)					\
	container_of(tx, struct mmp_pdma_desc_sw, async_tx)
#define to_mmp_pdma_desc(lh)					\
	container_of(lh, struct mmp_pdma_desc_sw, node)
#define to_mmp_pdma_chan(dchan)					\
	container_of(dchan, struct mmp_pdma_chan, chan)
#define to_mmp_pdma_dev(dmadev)					\
	container_of(dmadev, struct mmp_pdma_device, device)

static int mmp_pdma_config_write(struct dma_chan *dchan,
			   struct dma_slave_config *cfg,
			   enum dma_transfer_direction direction);

static void set_desc(struct mmp_pdma_phy *phy, dma_addr_t addr)
{
	u32 reg = (phy->idx << 4) + DDADR;

	writel(addr, phy->base + reg);
}

static void enable_chan(struct mmp_pdma_phy *phy)
{
	u32 reg, dalgn;

	if (!phy->vchan)
		return;

	reg = DRCMR(phy->vchan->drcmr);
	writel(DRCMR_MAPVLD | phy->idx, phy->base + reg);

	dalgn = readl(phy->base + DALGN);
	if (phy->vchan->byte_align)
		dalgn |= 1 << phy->idx;
	else
		dalgn &= ~(1 << phy->idx);
	writel(dalgn, phy->base + DALGN);

	reg = (phy->idx << 2) + DCSR;
	writel(readl(phy->base + reg) | DCSR_RUN, phy->base + reg);
}

static void disable_chan(struct mmp_pdma_phy *phy)
{
	u32 reg;

	if (!phy)
		return;

	reg = (phy->idx << 2) + DCSR;
	writel(readl(phy->base + reg) & ~DCSR_RUN, phy->base + reg);
}

static int clear_chan_irq(struct mmp_pdma_phy *phy)
{
	u32 dcsr;
	u32 dint = readl(phy->base + DINT);
	u32 reg = (phy->idx << 2) + DCSR;

	if (!(dint & BIT(phy->idx)))
		return -EAGAIN;

	/* clear irq */
	dcsr = readl(phy->base + reg);
	writel(dcsr, phy->base + reg);
	if ((dcsr & DCSR_BUSERR) && (phy->vchan))
		dev_warn(phy->vchan->dev, "DCSR_BUSERR\n");

	return 0;
}

static irqreturn_t mmp_pdma_chan_handler(int irq, void *dev_id)
{
	struct mmp_pdma_phy *phy = dev_id;

	if (clear_chan_irq(phy) != 0)
		return IRQ_NONE;

	tasklet_schedule(&phy->vchan->tasklet);
	return IRQ_HANDLED;
}

static irqreturn_t mmp_pdma_int_handler(int irq, void *dev_id)
{
	struct mmp_pdma_device *pdev = dev_id;
	struct mmp_pdma_phy *phy;
	u32 dint = readl(pdev->base + DINT);
	int i, ret;
	int irq_num = 0;

	while (dint) {
		i = __ffs(dint);
		/* only handle interrupts belonging to pdma driver*/
		if (i >= pdev->dma_channels)
			break;
		dint &= (dint - 1);
		phy = &pdev->phy[i];
		ret = mmp_pdma_chan_handler(irq, phy);
		if (ret == IRQ_HANDLED)
			irq_num++;
	}

	if (irq_num)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

/* lookup free phy channel as descending priority */
static struct mmp_pdma_phy *lookup_phy(struct mmp_pdma_chan *pchan)
{
	int prio, i;
	struct mmp_pdma_device *pdev = to_mmp_pdma_dev(pchan->chan.device);
	struct mmp_pdma_phy *phy, *found = NULL;
	unsigned long flags;

	/*
	 * dma channel priorities
	 * ch 0 - 3,  16 - 19  <--> (0)
	 * ch 4 - 7,  20 - 23  <--> (1)
	 * ch 8 - 11, 24 - 27  <--> (2)
	 * ch 12 - 15, 28 - 31  <--> (3)
	 */

	spin_lock_irqsave(&pdev->phy_lock, flags);
	for (prio = 0; prio <= ((pdev->dma_channels - 1) & 0xf) >> 2; prio++) {
		for (i = 0; i < pdev->dma_channels; i++) {
			if (prio != (i & 0xf) >> 2)
				continue;
			phy = &pdev->phy[i];
			if (!phy->vchan) {
				phy->vchan = pchan;
				found = phy;
				goto out_unlock;
			}
		}
	}

out_unlock:
	spin_unlock_irqrestore(&pdev->phy_lock, flags);
	return found;
}

static void mmp_pdma_free_phy(struct mmp_pdma_chan *pchan)
{
	struct mmp_pdma_device *pdev = to_mmp_pdma_dev(pchan->chan.device);
	unsigned long flags;
	u32 reg;

	if (!pchan->phy)
		return;

	/* clear the channel mapping in DRCMR */
	reg = DRCMR(pchan->drcmr);
	writel(0, pchan->phy->base + reg);

	spin_lock_irqsave(&pdev->phy_lock, flags);
	pchan->phy->vchan = NULL;
	pchan->phy = NULL;
	spin_unlock_irqrestore(&pdev->phy_lock, flags);
}

/*
 * start_pending_queue - transfer any pending transactions
 * pending list ==> running list
 */
static void start_pending_queue(struct mmp_pdma_chan *chan)
{
	struct mmp_pdma_desc_sw *desc;

	/* still in running, irq will start the pending list */
	if (!chan->idle) {
		dev_dbg(chan->dev, "DMA controller still busy\n");
		return;
	}

	if (list_empty(&chan->chain_pending)) {
		/* chance to re-fetch phy channel with higher prio */
		mmp_pdma_free_phy(chan);
		dev_dbg(chan->dev, "no pending list\n");
		return;
	}

	if (!chan->phy) {
		chan->phy = lookup_phy(chan);
		if (!chan->phy) {
			dev_dbg(chan->dev, "no free dma channel\n");
			return;
		}
	}

	/*
	 * pending -> running
	 * reintilize pending list
	 */
	desc = list_first_entry(&chan->chain_pending,
				struct mmp_pdma_desc_sw, node);
	list_splice_tail_init(&chan->chain_pending, &chan->chain_running);

	/*
	 * Program the descriptor's address into the DMA controller,
	 * then start the DMA transaction
	 */
	set_desc(chan->phy, desc->async_tx.phys);
	enable_chan(chan->phy);
	chan->idle = false;
}


/* desc->tx_list ==> pending list */
static dma_cookie_t mmp_pdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(tx->chan);
	struct mmp_pdma_desc_sw *desc = tx_to_mmp_pdma_desc(tx);
	struct mmp_pdma_desc_sw *child;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;

	spin_lock_irqsave(&chan->desc_lock, flags);

	list_for_each_entry(child, &desc->tx_list, node) {
		cookie = dma_cookie_assign(&child->async_tx);
	}

	/* softly link to pending list - desc->tx_list ==> pending list */
	list_splice_tail_init(&desc->tx_list, &chan->chain_pending);

	spin_unlock_irqrestore(&chan->desc_lock, flags);

	return cookie;
}

static struct mmp_pdma_desc_sw *
mmp_pdma_alloc_descriptor(struct mmp_pdma_chan *chan)
{
	struct mmp_pdma_desc_sw *desc;
	dma_addr_t pdesc;

	desc = dma_pool_zalloc(chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (!desc) {
		dev_err(chan->dev, "out of memory for link descriptor\n");
		return NULL;
	}

	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->chan);
	/* each desc has submit */
	desc->async_tx.tx_submit = mmp_pdma_tx_submit;
	desc->async_tx.phys = pdesc;

	return desc;
}

/*
 * mmp_pdma_alloc_chan_resources - Allocate resources for DMA channel.
 *
 * This function will create a dma pool for descriptor allocation.
 * Request irq only when channel is requested
 * Return - The number of allocated descriptors.
 */

static int mmp_pdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);

	if (chan->desc_pool)
		return 1;

	chan->desc_pool = dma_pool_create(dev_name(&dchan->dev->device),
					  chan->dev,
					  sizeof(struct mmp_pdma_desc_sw),
					  __alignof__(struct mmp_pdma_desc_sw),
					  0);
	if (!chan->desc_pool) {
		dev_err(chan->dev, "unable to allocate descriptor pool\n");
		return -ENOMEM;
	}

	mmp_pdma_free_phy(chan);
	chan->idle = true;
	chan->dev_addr = 0;
	return 1;
}

static void mmp_pdma_free_desc_list(struct mmp_pdma_chan *chan,
				    struct list_head *list)
{
	struct mmp_pdma_desc_sw *desc, *_desc;

	list_for_each_entry_safe(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void mmp_pdma_free_chan_resources(struct dma_chan *dchan)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->desc_lock, flags);
	mmp_pdma_free_desc_list(chan, &chan->chain_pending);
	mmp_pdma_free_desc_list(chan, &chan->chain_running);
	spin_unlock_irqrestore(&chan->desc_lock, flags);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
	chan->idle = true;
	chan->dev_addr = 0;
	mmp_pdma_free_phy(chan);
	return;
}

static struct dma_async_tx_descriptor *
mmp_pdma_prep_memcpy(struct dma_chan *dchan,
		     dma_addr_t dma_dst, dma_addr_t dma_src,
		     size_t len, unsigned long flags)
{
	struct mmp_pdma_chan *chan;
	struct mmp_pdma_desc_sw *first = NULL, *prev = NULL, *new;
	size_t copy = 0;

	if (!dchan)
		return NULL;

	if (!len)
		return NULL;

	chan = to_mmp_pdma_chan(dchan);
	chan->byte_align = false;

	if (!chan->dir) {
		chan->dir = DMA_MEM_TO_MEM;
		chan->dcmd = DCMD_INCTRGADDR | DCMD_INCSRCADDR;
		chan->dcmd |= DCMD_BURST32;
	}

	do {
		/* Allocate the link descriptor from DMA pool */
		new = mmp_pdma_alloc_descriptor(chan);
		if (!new) {
			dev_err(chan->dev, "no memory for desc\n");
			goto fail;
		}

		copy = min_t(size_t, len, PDMA_MAX_DESC_BYTES);
		if (dma_src & 0x7 || dma_dst & 0x7)
			chan->byte_align = true;

		new->desc.dcmd = chan->dcmd | (DCMD_LENGTH & copy);
		new->desc.dsadr = dma_src;
		new->desc.dtadr = dma_dst;

		if (!first)
			first = new;
		else
			prev->desc.ddadr = new->async_tx.phys;

		new->async_tx.cookie = 0;
		async_tx_ack(&new->async_tx);

		prev = new;
		len -= copy;

		if (chan->dir == DMA_MEM_TO_DEV) {
			dma_src += copy;
		} else if (chan->dir == DMA_DEV_TO_MEM) {
			dma_dst += copy;
		} else if (chan->dir == DMA_MEM_TO_MEM) {
			dma_src += copy;
			dma_dst += copy;
		}

		/* Insert the link descriptor to the LD ring */
		list_add_tail(&new->node, &first->tx_list);
	} while (len);

	first->async_tx.flags = flags; /* client is in control of this ack */
	first->async_tx.cookie = -EBUSY;

	/* last desc and fire IRQ */
	new->desc.ddadr = DDADR_STOP;
	new->desc.dcmd |= DCMD_ENDIRQEN;

	chan->cyclic_first = NULL;

	return &first->async_tx;

fail:
	if (first)
		mmp_pdma_free_desc_list(chan, &first->tx_list);
	return NULL;
}

static struct dma_async_tx_descriptor *
mmp_pdma_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl,
		       unsigned int sg_len, enum dma_transfer_direction dir,
		       unsigned long flags, void *context)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);
	struct mmp_pdma_desc_sw *first = NULL, *prev = NULL, *new = NULL;
	size_t len, avail;
	struct scatterlist *sg;
	dma_addr_t addr;
	int i;

	if ((sgl == NULL) || (sg_len == 0))
		return NULL;

	chan->byte_align = false;

	mmp_pdma_config_write(dchan, &chan->slave_config, dir);

	for_each_sg(sgl, sg, sg_len, i) {
		addr = sg_dma_address(sg);
		avail = sg_dma_len(sgl);

		do {
			len = min_t(size_t, avail, PDMA_MAX_DESC_BYTES);
			if (addr & 0x7)
				chan->byte_align = true;

			/* allocate and populate the descriptor */
			new = mmp_pdma_alloc_descriptor(chan);
			if (!new) {
				dev_err(chan->dev, "no memory for desc\n");
				goto fail;
			}

			new->desc.dcmd = chan->dcmd | (DCMD_LENGTH & len);
			if (dir == DMA_MEM_TO_DEV) {
				new->desc.dsadr = addr;
				new->desc.dtadr = chan->dev_addr;
			} else {
				new->desc.dsadr = chan->dev_addr;
				new->desc.dtadr = addr;
			}

			if (!first)
				first = new;
			else
				prev->desc.ddadr = new->async_tx.phys;

			new->async_tx.cookie = 0;
			async_tx_ack(&new->async_tx);
			prev = new;

			/* Insert the link descriptor to the LD ring */
			list_add_tail(&new->node, &first->tx_list);

			/* update metadata */
			addr += len;
			avail -= len;
		} while (avail);
	}

	first->async_tx.cookie = -EBUSY;
	first->async_tx.flags = flags;

	/* last desc and fire IRQ */
	new->desc.ddadr = DDADR_STOP;
	new->desc.dcmd |= DCMD_ENDIRQEN;

	chan->dir = dir;
	chan->cyclic_first = NULL;

	return &first->async_tx;

fail:
	if (first)
		mmp_pdma_free_desc_list(chan, &first->tx_list);
	return NULL;
}

static struct dma_async_tx_descriptor *
mmp_pdma_prep_dma_cyclic(struct dma_chan *dchan,
			 dma_addr_t buf_addr, size_t len, size_t period_len,
			 enum dma_transfer_direction direction,
			 unsigned long flags)
{
	struct mmp_pdma_chan *chan;
	struct mmp_pdma_desc_sw *first = NULL, *prev = NULL, *new;
	dma_addr_t dma_src, dma_dst;

	if (!dchan || !len || !period_len)
		return NULL;

	/* the buffer length must be a multiple of period_len */
	if (len % period_len != 0)
		return NULL;

	if (period_len > PDMA_MAX_DESC_BYTES)
		return NULL;

	chan = to_mmp_pdma_chan(dchan);
	mmp_pdma_config_write(dchan, &chan->slave_config, direction);

	switch (direction) {
	case DMA_MEM_TO_DEV:
		dma_src = buf_addr;
		dma_dst = chan->dev_addr;
		break;
	case DMA_DEV_TO_MEM:
		dma_dst = buf_addr;
		dma_src = chan->dev_addr;
		break;
	default:
		dev_err(chan->dev, "Unsupported direction for cyclic DMA\n");
		return NULL;
	}

	chan->dir = direction;

	do {
		/* Allocate the link descriptor from DMA pool */
		new = mmp_pdma_alloc_descriptor(chan);
		if (!new) {
			dev_err(chan->dev, "no memory for desc\n");
			goto fail;
		}

		new->desc.dcmd = (chan->dcmd | DCMD_ENDIRQEN |
				  (DCMD_LENGTH & period_len));
		new->desc.dsadr = dma_src;
		new->desc.dtadr = dma_dst;

		if (!first)
			first = new;
		else
			prev->desc.ddadr = new->async_tx.phys;

		new->async_tx.cookie = 0;
		async_tx_ack(&new->async_tx);

		prev = new;
		len -= period_len;

		if (chan->dir == DMA_MEM_TO_DEV)
			dma_src += period_len;
		else
			dma_dst += period_len;

		/* Insert the link descriptor to the LD ring */
		list_add_tail(&new->node, &first->tx_list);
	} while (len);

	first->async_tx.flags = flags; /* client is in control of this ack */
	first->async_tx.cookie = -EBUSY;

	/* make the cyclic link */
	new->desc.ddadr = first->async_tx.phys;
	chan->cyclic_first = first;

	return &first->async_tx;

fail:
	if (first)
		mmp_pdma_free_desc_list(chan, &first->tx_list);
	return NULL;
}

static int mmp_pdma_config_write(struct dma_chan *dchan,
			   struct dma_slave_config *cfg,
			   enum dma_transfer_direction direction)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);
	u32 maxburst = 0, addr = 0;
	enum dma_slave_buswidth width = DMA_SLAVE_BUSWIDTH_UNDEFINED;

	if (!dchan)
		return -EINVAL;

	if (direction == DMA_DEV_TO_MEM) {
		chan->dcmd = DCMD_INCTRGADDR | DCMD_FLOWSRC;
		maxburst = cfg->src_maxburst;
		width = cfg->src_addr_width;
		addr = cfg->src_addr;
	} else if (direction == DMA_MEM_TO_DEV) {
		chan->dcmd = DCMD_INCSRCADDR | DCMD_FLOWTRG;
		maxburst = cfg->dst_maxburst;
		width = cfg->dst_addr_width;
		addr = cfg->dst_addr;
	}

	if (width == DMA_SLAVE_BUSWIDTH_1_BYTE)
		chan->dcmd |= DCMD_WIDTH1;
	else if (width == DMA_SLAVE_BUSWIDTH_2_BYTES)
		chan->dcmd |= DCMD_WIDTH2;
	else if (width == DMA_SLAVE_BUSWIDTH_4_BYTES)
		chan->dcmd |= DCMD_WIDTH4;

	if (maxburst == 8)
		chan->dcmd |= DCMD_BURST8;
	else if (maxburst == 16)
		chan->dcmd |= DCMD_BURST16;
	else if (maxburst == 32)
		chan->dcmd |= DCMD_BURST32;

	chan->dir = direction;
	chan->dev_addr = addr;

	return 0;
}

static int mmp_pdma_config(struct dma_chan *dchan,
			   struct dma_slave_config *cfg)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);

	memcpy(&chan->slave_config, cfg, sizeof(*cfg));
	return 0;
}

static int mmp_pdma_terminate_all(struct dma_chan *dchan)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);
	unsigned long flags;

	if (!dchan)
		return -EINVAL;

	disable_chan(chan->phy);
	mmp_pdma_free_phy(chan);
	spin_lock_irqsave(&chan->desc_lock, flags);
	mmp_pdma_free_desc_list(chan, &chan->chain_pending);
	mmp_pdma_free_desc_list(chan, &chan->chain_running);
	spin_unlock_irqrestore(&chan->desc_lock, flags);
	chan->idle = true;

	return 0;
}

static unsigned int mmp_pdma_residue(struct mmp_pdma_chan *chan,
				     dma_cookie_t cookie)
{
	struct mmp_pdma_desc_sw *sw;
	u32 curr, residue = 0;
	bool passed = false;
	bool cyclic = chan->cyclic_first != NULL;

	/*
	 * If the channel does not have a phy pointer anymore, it has already
	 * been completed. Therefore, its residue is 0.
	 */
	if (!chan->phy)
		return 0;

	if (chan->dir == DMA_DEV_TO_MEM)
		curr = readl(chan->phy->base + DTADR(chan->phy->idx));
	else
		curr = readl(chan->phy->base + DSADR(chan->phy->idx));

	list_for_each_entry(sw, &chan->chain_running, node) {
		u32 start, end, len;

		if (chan->dir == DMA_DEV_TO_MEM)
			start = sw->desc.dtadr;
		else
			start = sw->desc.dsadr;

		len = sw->desc.dcmd & DCMD_LENGTH;
		end = start + len;

		/*
		 * 'passed' will be latched once we found the descriptor which
		 * lies inside the boundaries of the curr pointer. All
		 * descriptors that occur in the list _after_ we found that
		 * partially handled descriptor are still to be processed and
		 * are hence added to the residual bytes counter.
		 */

		if (passed) {
			residue += len;
		} else if (curr >= start && curr <= end) {
			residue += end - curr;
			passed = true;
		}

		/*
		 * Descriptors that have the ENDIRQEN bit set mark the end of a
		 * transaction chain, and the cookie assigned with it has been
		 * returned previously from mmp_pdma_tx_submit().
		 *
		 * In case we have multiple transactions in the running chain,
		 * and the cookie does not match the one the user asked us
		 * about, reset the state variables and start over.
		 *
		 * This logic does not apply to cyclic transactions, where all
		 * descriptors have the ENDIRQEN bit set, and for which we
		 * can't have multiple transactions on one channel anyway.
		 */
		if (cyclic || !(sw->desc.dcmd & DCMD_ENDIRQEN))
			continue;

		if (sw->async_tx.cookie == cookie) {
			return residue;
		} else {
			residue = 0;
			passed = false;
		}
	}

	/* We should only get here in case of cyclic transactions */
	return residue;
}

static enum dma_status mmp_pdma_tx_status(struct dma_chan *dchan,
					  dma_cookie_t cookie,
					  struct dma_tx_state *txstate)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);
	enum dma_status ret;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (likely(ret != DMA_ERROR))
		dma_set_residue(txstate, mmp_pdma_residue(chan, cookie));

	return ret;
}

/*
 * mmp_pdma_issue_pending - Issue the DMA start command
 * pending list ==> running list
 */
static void mmp_pdma_issue_pending(struct dma_chan *dchan)
{
	struct mmp_pdma_chan *chan = to_mmp_pdma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->desc_lock, flags);
	start_pending_queue(chan);
	spin_unlock_irqrestore(&chan->desc_lock, flags);
}

/*
 * dma_do_tasklet
 * Do call back
 * Start pending list
 */
static void dma_do_tasklet(struct tasklet_struct *t)
{
	struct mmp_pdma_chan *chan = from_tasklet(chan, t, tasklet);
	struct mmp_pdma_desc_sw *desc, *_desc;
	LIST_HEAD(chain_cleanup);
	unsigned long flags;
	struct dmaengine_desc_callback cb;

	if (chan->cyclic_first) {
		spin_lock_irqsave(&chan->desc_lock, flags);
		desc = chan->cyclic_first;
		dmaengine_desc_get_callback(&desc->async_tx, &cb);
		spin_unlock_irqrestore(&chan->desc_lock, flags);

		dmaengine_desc_callback_invoke(&cb, NULL);

		return;
	}

	/* submit pending list; callback for each desc; free desc */
	spin_lock_irqsave(&chan->desc_lock, flags);

	list_for_each_entry_safe(desc, _desc, &chan->chain_running, node) {
		/*
		 * move the descriptors to a temporary list so we can drop
		 * the lock during the entire cleanup operation
		 */
		list_move(&desc->node, &chain_cleanup);

		/*
		 * Look for the first list entry which has the ENDIRQEN flag
		 * set. That is the descriptor we got an interrupt for, so
		 * complete that transaction and its cookie.
		 */
		if (desc->desc.dcmd & DCMD_ENDIRQEN) {
			dma_cookie_t cookie = desc->async_tx.cookie;
			dma_cookie_complete(&desc->async_tx);
			dev_dbg(chan->dev, "completed_cookie=%d\n", cookie);
			break;
		}
	}

	/*
	 * The hardware is idle and ready for more when the
	 * chain_running list is empty.
	 */
	chan->idle = list_empty(&chan->chain_running);

	/* Start any pending transactions automatically */
	start_pending_queue(chan);
	spin_unlock_irqrestore(&chan->desc_lock, flags);

	/* Run the callback for each descriptor, in order */
	list_for_each_entry_safe(desc, _desc, &chain_cleanup, node) {
		struct dma_async_tx_descriptor *txd = &desc->async_tx;

		/* Remove from the list of transactions */
		list_del(&desc->node);
		/* Run the link descriptor callback function */
		dmaengine_desc_get_callback(txd, &cb);
		dmaengine_desc_callback_invoke(&cb, NULL);

		dma_pool_free(chan->desc_pool, desc, txd->phys);
	}
}

static int mmp_pdma_remove(struct platform_device *op)
{
	struct mmp_pdma_device *pdev = platform_get_drvdata(op);
	struct mmp_pdma_phy *phy;
	int i, irq = 0, irq_num = 0;

	if (op->dev.of_node)
		of_dma_controller_free(op->dev.of_node);

	for (i = 0; i < pdev->dma_channels; i++) {
		if (platform_get_irq(op, i) > 0)
			irq_num++;
	}

	if (irq_num != pdev->dma_channels) {
		irq = platform_get_irq(op, 0);
		devm_free_irq(&op->dev, irq, pdev);
	} else {
		for (i = 0; i < pdev->dma_channels; i++) {
			phy = &pdev->phy[i];
			irq = platform_get_irq(op, i);
			devm_free_irq(&op->dev, irq, phy);
		}
	}

	dma_async_device_unregister(&pdev->device);
	return 0;
}

static int mmp_pdma_chan_init(struct mmp_pdma_device *pdev, int idx, int irq)
{
	struct mmp_pdma_phy *phy  = &pdev->phy[idx];
	struct mmp_pdma_chan *chan;
	int ret;

	chan = devm_kzalloc(pdev->dev, sizeof(*chan), GFP_KERNEL);
	if (chan == NULL)
		return -ENOMEM;

	phy->idx = idx;
	phy->base = pdev->base;

	if (irq) {
		ret = devm_request_irq(pdev->dev, irq, mmp_pdma_chan_handler,
				       IRQF_SHARED, "pdma", phy);
		if (ret) {
			dev_err(pdev->dev, "channel request irq fail!\n");
			return ret;
		}
	}

	spin_lock_init(&chan->desc_lock);
	chan->dev = pdev->dev;
	chan->chan.device = &pdev->device;
	tasklet_setup(&chan->tasklet, dma_do_tasklet);
	INIT_LIST_HEAD(&chan->chain_pending);
	INIT_LIST_HEAD(&chan->chain_running);

	/* register virt channel to dma engine */
	list_add_tail(&chan->chan.device_node, &pdev->device.channels);

	return 0;
}

static const struct of_device_id mmp_pdma_dt_ids[] = {
	{ .compatible = "marvell,pdma-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, mmp_pdma_dt_ids);

static struct dma_chan *mmp_pdma_dma_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct mmp_pdma_device *d = ofdma->of_dma_data;
	struct dma_chan *chan;

	chan = dma_get_any_slave_channel(&d->device);
	if (!chan)
		return NULL;

	to_mmp_pdma_chan(chan)->drcmr = dma_spec->args[0];

	return chan;
}

static int mmp_pdma_probe(struct platform_device *op)
{
	struct mmp_pdma_device *pdev;
	const struct of_device_id *of_id;
	struct mmp_dma_platdata *pdata = dev_get_platdata(&op->dev);
	struct resource *iores;
	int i, ret, irq = 0;
	int dma_channels = 0, irq_num = 0;
	const enum dma_slave_buswidth widths =
		DMA_SLAVE_BUSWIDTH_1_BYTE   | DMA_SLAVE_BUSWIDTH_2_BYTES |
		DMA_SLAVE_BUSWIDTH_4_BYTES;

	pdev = devm_kzalloc(&op->dev, sizeof(*pdev), GFP_KERNEL);
	if (!pdev)
		return -ENOMEM;

	pdev->dev = &op->dev;

	spin_lock_init(&pdev->phy_lock);

	iores = platform_get_resource(op, IORESOURCE_MEM, 0);
	pdev->base = devm_ioremap_resource(pdev->dev, iores);
	if (IS_ERR(pdev->base))
		return PTR_ERR(pdev->base);

	of_id = of_match_device(mmp_pdma_dt_ids, pdev->dev);
	if (of_id)
		of_property_read_u32(pdev->dev->of_node, "#dma-channels",
				     &dma_channels);
	else if (pdata && pdata->dma_channels)
		dma_channels = pdata->dma_channels;
	else
		dma_channels = 32;	/* default 32 channel */
	pdev->dma_channels = dma_channels;

	for (i = 0; i < dma_channels; i++) {
		if (platform_get_irq_optional(op, i) > 0)
			irq_num++;
	}

	pdev->phy = devm_kcalloc(pdev->dev, dma_channels, sizeof(*pdev->phy),
				 GFP_KERNEL);
	if (pdev->phy == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&pdev->device.channels);

	if (irq_num != dma_channels) {
		/* all chan share one irq, demux inside */
		irq = platform_get_irq(op, 0);
		ret = devm_request_irq(pdev->dev, irq, mmp_pdma_int_handler,
				       IRQF_SHARED, "pdma", pdev);
		if (ret)
			return ret;
	}

	for (i = 0; i < dma_channels; i++) {
		irq = (irq_num != dma_channels) ? 0 : platform_get_irq(op, i);
		ret = mmp_pdma_chan_init(pdev, i, irq);
		if (ret)
			return ret;
	}

	dma_cap_set(DMA_SLAVE, pdev->device.cap_mask);
	dma_cap_set(DMA_MEMCPY, pdev->device.cap_mask);
	dma_cap_set(DMA_CYCLIC, pdev->device.cap_mask);
	dma_cap_set(DMA_PRIVATE, pdev->device.cap_mask);
	pdev->device.dev = &op->dev;
	pdev->device.device_alloc_chan_resources = mmp_pdma_alloc_chan_resources;
	pdev->device.device_free_chan_resources = mmp_pdma_free_chan_resources;
	pdev->device.device_tx_status = mmp_pdma_tx_status;
	pdev->device.device_prep_dma_memcpy = mmp_pdma_prep_memcpy;
	pdev->device.device_prep_slave_sg = mmp_pdma_prep_slave_sg;
	pdev->device.device_prep_dma_cyclic = mmp_pdma_prep_dma_cyclic;
	pdev->device.device_issue_pending = mmp_pdma_issue_pending;
	pdev->device.device_config = mmp_pdma_config;
	pdev->device.device_terminate_all = mmp_pdma_terminate_all;
	pdev->device.copy_align = DMAENGINE_ALIGN_8_BYTES;
	pdev->device.src_addr_widths = widths;
	pdev->device.dst_addr_widths = widths;
	pdev->device.directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	pdev->device.residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	if (pdev->dev->coherent_dma_mask)
		dma_set_mask(pdev->dev, pdev->dev->coherent_dma_mask);
	else
		dma_set_mask(pdev->dev, DMA_BIT_MASK(64));

	ret = dma_async_device_register(&pdev->device);
	if (ret) {
		dev_err(pdev->device.dev, "unable to register\n");
		return ret;
	}

	if (op->dev.of_node) {
		/* Device-tree DMA controller registration */
		ret = of_dma_controller_register(op->dev.of_node,
						 mmp_pdma_dma_xlate, pdev);
		if (ret < 0) {
			dev_err(&op->dev, "of_dma_controller_register failed\n");
			dma_async_device_unregister(&pdev->device);
			return ret;
		}
	}

	platform_set_drvdata(op, pdev);
	dev_info(pdev->device.dev, "initialized %d channels\n", dma_channels);
	return 0;
}

static const struct platform_device_id mmp_pdma_id_table[] = {
	{ "mmp-pdma", },
	{ },
};

static struct platform_driver mmp_pdma_driver = {
	.driver		= {
		.name	= "mmp-pdma",
		.of_match_table = mmp_pdma_dt_ids,
	},
	.id_table	= mmp_pdma_id_table,
	.probe		= mmp_pdma_probe,
	.remove		= mmp_pdma_remove,
};

module_platform_driver(mmp_pdma_driver);

MODULE_DESCRIPTION("MARVELL MMP Peripheral DMA Driver");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL v2");
