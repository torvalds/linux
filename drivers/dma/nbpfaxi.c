/*
 * Copyright (C) 2013-2014 Renesas Electronics Europe Ltd.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/dma/nbpfaxi.h>

#include "dmaengine.h"

#define NBPF_REG_CHAN_OFFSET	0
#define NBPF_REG_CHAN_SIZE	0x40

/* Channel Current Transaction Byte register */
#define NBPF_CHAN_CUR_TR_BYTE	0x20

/* Channel Status register */
#define NBPF_CHAN_STAT	0x24
#define NBPF_CHAN_STAT_EN	1
#define NBPF_CHAN_STAT_TACT	4
#define NBPF_CHAN_STAT_ERR	0x10
#define NBPF_CHAN_STAT_END	0x20
#define NBPF_CHAN_STAT_TC	0x40
#define NBPF_CHAN_STAT_DER	0x400

/* Channel Control register */
#define NBPF_CHAN_CTRL	0x28
#define NBPF_CHAN_CTRL_SETEN	1
#define NBPF_CHAN_CTRL_CLREN	2
#define NBPF_CHAN_CTRL_STG	4
#define NBPF_CHAN_CTRL_SWRST	8
#define NBPF_CHAN_CTRL_CLRRQ	0x10
#define NBPF_CHAN_CTRL_CLREND	0x20
#define NBPF_CHAN_CTRL_CLRTC	0x40
#define NBPF_CHAN_CTRL_SETSUS	0x100
#define NBPF_CHAN_CTRL_CLRSUS	0x200

/* Channel Configuration register */
#define NBPF_CHAN_CFG	0x2c
#define NBPF_CHAN_CFG_SEL	7		/* terminal SELect: 0..7 */
#define NBPF_CHAN_CFG_REQD	8		/* REQuest Direction: DMAREQ is 0: input, 1: output */
#define NBPF_CHAN_CFG_LOEN	0x10		/* LOw ENable: low DMA request line is: 0: inactive, 1: active */
#define NBPF_CHAN_CFG_HIEN	0x20		/* HIgh ENable: high DMA request line is: 0: inactive, 1: active */
#define NBPF_CHAN_CFG_LVL	0x40		/* LeVeL: DMA request line is sensed as 0: edge, 1: level */
#define NBPF_CHAN_CFG_AM	0x700		/* ACK Mode: 0: Pulse mode, 1: Level mode, b'1x: Bus Cycle */
#define NBPF_CHAN_CFG_SDS	0xf000		/* Source Data Size: 0: 8 bits,... , 7: 1024 bits */
#define NBPF_CHAN_CFG_DDS	0xf0000		/* Destination Data Size: as above */
#define NBPF_CHAN_CFG_SAD	0x100000	/* Source ADdress counting: 0: increment, 1: fixed */
#define NBPF_CHAN_CFG_DAD	0x200000	/* Destination ADdress counting: 0: increment, 1: fixed */
#define NBPF_CHAN_CFG_TM	0x400000	/* Transfer Mode: 0: single, 1: block TM */
#define NBPF_CHAN_CFG_DEM	0x1000000	/* DMAEND interrupt Mask */
#define NBPF_CHAN_CFG_TCM	0x2000000	/* DMATCO interrupt Mask */
#define NBPF_CHAN_CFG_SBE	0x8000000	/* Sweep Buffer Enable */
#define NBPF_CHAN_CFG_RSEL	0x10000000	/* RM: Register Set sELect */
#define NBPF_CHAN_CFG_RSW	0x20000000	/* RM: Register Select sWitch */
#define NBPF_CHAN_CFG_REN	0x40000000	/* RM: Register Set Enable */
#define NBPF_CHAN_CFG_DMS	0x80000000	/* 0: register mode (RM), 1: link mode (LM) */

#define NBPF_CHAN_NXLA	0x38
#define NBPF_CHAN_CRLA	0x3c

/* Link Header field */
#define NBPF_HEADER_LV	1
#define NBPF_HEADER_LE	2
#define NBPF_HEADER_WBD	4
#define NBPF_HEADER_DIM	8

#define NBPF_CTRL	0x300
#define NBPF_CTRL_PR	1		/* 0: fixed priority, 1: round robin */
#define NBPF_CTRL_LVINT	2		/* DMAEND and DMAERR signalling: 0: pulse, 1: level */

#define NBPF_DSTAT_ER	0x314
#define NBPF_DSTAT_END	0x318

#define NBPF_DMA_BUSWIDTHS \
	(BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) | \
	 BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
	 BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

struct nbpf_config {
	int num_channels;
	int buffer_size;
};

/*
 * We've got 3 types of objects, used to describe DMA transfers:
 * 1. high-level descriptor, containing a struct dma_async_tx_descriptor object
 *	in it, used to communicate with the user
 * 2. hardware DMA link descriptors, that we pass to DMAC for DMA transfer
 *	queuing, these must be DMAable, using either the streaming DMA API or
 *	allocated from coherent memory - one per SG segment
 * 3. one per SG segment descriptors, used to manage HW link descriptors from
 *	(2). They do not have to be DMAable. They can either be (a) allocated
 *	together with link descriptors as mixed (DMA / CPU) objects, or (b)
 *	separately. Even if allocated separately it would be best to link them
 *	to link descriptors once during channel resource allocation and always
 *	use them as a single object.
 * Therefore for both cases (a) and (b) at run-time objects (2) and (3) shall be
 * treated as a single SG segment descriptor.
 */

struct nbpf_link_reg {
	u32	header;
	u32	src_addr;
	u32	dst_addr;
	u32	transaction_size;
	u32	config;
	u32	interval;
	u32	extension;
	u32	next;
} __packed;

struct nbpf_device;
struct nbpf_channel;
struct nbpf_desc;

struct nbpf_link_desc {
	struct nbpf_link_reg *hwdesc;
	dma_addr_t hwdesc_dma_addr;
	struct nbpf_desc *desc;
	struct list_head node;
};

/**
 * struct nbpf_desc - DMA transfer descriptor
 * @async_tx:	dmaengine object
 * @user_wait:	waiting for a user ack
 * @length:	total transfer length
 * @sg:		list of hardware descriptors, represented by struct nbpf_link_desc
 * @node:	member in channel descriptor lists
 */
struct nbpf_desc {
	struct dma_async_tx_descriptor async_tx;
	bool user_wait;
	size_t length;
	struct nbpf_channel *chan;
	struct list_head sg;
	struct list_head node;
};

/* Take a wild guess: allocate 4 segments per descriptor */
#define NBPF_SEGMENTS_PER_DESC 4
#define NBPF_DESCS_PER_PAGE ((PAGE_SIZE - sizeof(struct list_head)) /	\
	(sizeof(struct nbpf_desc) +					\
	 NBPF_SEGMENTS_PER_DESC *					\
	 (sizeof(struct nbpf_link_desc) + sizeof(struct nbpf_link_reg))))
#define NBPF_SEGMENTS_PER_PAGE (NBPF_SEGMENTS_PER_DESC * NBPF_DESCS_PER_PAGE)

struct nbpf_desc_page {
	struct list_head node;
	struct nbpf_desc desc[NBPF_DESCS_PER_PAGE];
	struct nbpf_link_desc ldesc[NBPF_SEGMENTS_PER_PAGE];
	struct nbpf_link_reg hwdesc[NBPF_SEGMENTS_PER_PAGE];
};

/**
 * struct nbpf_channel - one DMAC channel
 * @dma_chan:	standard dmaengine channel object
 * @base:	register address base
 * @nbpf:	DMAC
 * @name:	IRQ name
 * @irq:	IRQ number
 * @slave_addr:	address for slave DMA
 * @slave_width:slave data size in bytes
 * @slave_burst:maximum slave burst size in bytes
 * @terminal:	DMA terminal, assigned to this channel
 * @dmarq_cfg:	DMA request line configuration - high / low, edge / level for NBPF_CHAN_CFG
 * @flags:	configuration flags from DT
 * @lock:	protect descriptor lists
 * @free_links:	list of free link descriptors
 * @free:	list of free descriptors
 * @queued:	list of queued descriptors
 * @active:	list of descriptors, scheduled for processing
 * @done:	list of completed descriptors, waiting post-processing
 * @desc_page:	list of additionally allocated descriptor pages - if any
 */
struct nbpf_channel {
	struct dma_chan dma_chan;
	void __iomem *base;
	struct nbpf_device *nbpf;
	char name[16];
	int irq;
	dma_addr_t slave_src_addr;
	size_t slave_src_width;
	size_t slave_src_burst;
	dma_addr_t slave_dst_addr;
	size_t slave_dst_width;
	size_t slave_dst_burst;
	unsigned int terminal;
	u32 dmarq_cfg;
	unsigned long flags;
	spinlock_t lock;
	struct list_head free_links;
	struct list_head free;
	struct list_head queued;
	struct list_head active;
	struct list_head done;
	struct list_head desc_page;
	struct nbpf_desc *running;
	bool paused;
};

struct nbpf_device {
	struct dma_device dma_dev;
	void __iomem *base;
	struct clk *clk;
	const struct nbpf_config *config;
	struct nbpf_channel chan[];
};

enum nbpf_model {
	NBPF1B4,
	NBPF1B8,
	NBPF1B16,
	NBPF4B4,
	NBPF4B8,
	NBPF4B16,
	NBPF8B4,
	NBPF8B8,
	NBPF8B16,
};

static struct nbpf_config nbpf_cfg[] = {
	[NBPF1B4] = {
		.num_channels = 1,
		.buffer_size = 4,
	},
	[NBPF1B8] = {
		.num_channels = 1,
		.buffer_size = 8,
	},
	[NBPF1B16] = {
		.num_channels = 1,
		.buffer_size = 16,
	},
	[NBPF4B4] = {
		.num_channels = 4,
		.buffer_size = 4,
	},
	[NBPF4B8] = {
		.num_channels = 4,
		.buffer_size = 8,
	},
	[NBPF4B16] = {
		.num_channels = 4,
		.buffer_size = 16,
	},
	[NBPF8B4] = {
		.num_channels = 8,
		.buffer_size = 4,
	},
	[NBPF8B8] = {
		.num_channels = 8,
		.buffer_size = 8,
	},
	[NBPF8B16] = {
		.num_channels = 8,
		.buffer_size = 16,
	},
};

#define nbpf_to_chan(d) container_of(d, struct nbpf_channel, dma_chan)

/*
 * dmaengine drivers seem to have a lot in common and instead of sharing more
 * code, they reimplement those common algorithms independently. In this driver
 * we try to separate the hardware-specific part from the (largely) generic
 * part. This improves code readability and makes it possible in the future to
 * reuse the generic code in form of a helper library. That generic code should
 * be suitable for various DMA controllers, using transfer descriptors in RAM
 * and pushing one SG list at a time to the DMA controller.
 */

/*		Hardware-specific part		*/

static inline u32 nbpf_chan_read(struct nbpf_channel *chan,
				 unsigned int offset)
{
	u32 data = ioread32(chan->base + offset);
	dev_dbg(chan->dma_chan.device->dev, "%s(0x%p + 0x%x) = 0x%x\n",
		__func__, chan->base, offset, data);
	return data;
}

static inline void nbpf_chan_write(struct nbpf_channel *chan,
				   unsigned int offset, u32 data)
{
	iowrite32(data, chan->base + offset);
	dev_dbg(chan->dma_chan.device->dev, "%s(0x%p + 0x%x) = 0x%x\n",
		__func__, chan->base, offset, data);
}

static inline u32 nbpf_read(struct nbpf_device *nbpf,
			    unsigned int offset)
{
	u32 data = ioread32(nbpf->base + offset);
	dev_dbg(nbpf->dma_dev.dev, "%s(0x%p + 0x%x) = 0x%x\n",
		__func__, nbpf->base, offset, data);
	return data;
}

static inline void nbpf_write(struct nbpf_device *nbpf,
			      unsigned int offset, u32 data)
{
	iowrite32(data, nbpf->base + offset);
	dev_dbg(nbpf->dma_dev.dev, "%s(0x%p + 0x%x) = 0x%x\n",
		__func__, nbpf->base, offset, data);
}

static void nbpf_chan_halt(struct nbpf_channel *chan)
{
	nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_CLREN);
}

static bool nbpf_status_get(struct nbpf_channel *chan)
{
	u32 status = nbpf_read(chan->nbpf, NBPF_DSTAT_END);

	return status & BIT(chan - chan->nbpf->chan);
}

static void nbpf_status_ack(struct nbpf_channel *chan)
{
	nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_CLREND);
}

static u32 nbpf_error_get(struct nbpf_device *nbpf)
{
	return nbpf_read(nbpf, NBPF_DSTAT_ER);
}

struct nbpf_channel *nbpf_error_get_channel(struct nbpf_device *nbpf, u32 error)
{
	return nbpf->chan + __ffs(error);
}

static void nbpf_error_clear(struct nbpf_channel *chan)
{
	u32 status;
	int i;

	/* Stop the channel, make sure DMA has been aborted */
	nbpf_chan_halt(chan);

	for (i = 1000; i; i--) {
		status = nbpf_chan_read(chan, NBPF_CHAN_STAT);
		if (!(status & NBPF_CHAN_STAT_TACT))
			break;
		cpu_relax();
	}

	if (!i)
		dev_err(chan->dma_chan.device->dev,
			"%s(): abort timeout, channel status 0x%x\n", __func__, status);

	nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_SWRST);
}

static int nbpf_start(struct nbpf_desc *desc)
{
	struct nbpf_channel *chan = desc->chan;
	struct nbpf_link_desc *ldesc = list_first_entry(&desc->sg, struct nbpf_link_desc, node);

	nbpf_chan_write(chan, NBPF_CHAN_NXLA, (u32)ldesc->hwdesc_dma_addr);
	nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_SETEN | NBPF_CHAN_CTRL_CLRSUS);
	chan->paused = false;

	/* Software trigger MEMCPY - only MEMCPY uses the block mode */
	if (ldesc->hwdesc->config & NBPF_CHAN_CFG_TM)
		nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_STG);

	dev_dbg(chan->nbpf->dma_dev.dev, "%s(): next 0x%x, cur 0x%x\n", __func__,
		nbpf_chan_read(chan, NBPF_CHAN_NXLA), nbpf_chan_read(chan, NBPF_CHAN_CRLA));

	return 0;
}

static void nbpf_chan_prepare(struct nbpf_channel *chan)
{
	chan->dmarq_cfg = (chan->flags & NBPF_SLAVE_RQ_HIGH ? NBPF_CHAN_CFG_HIEN : 0) |
		(chan->flags & NBPF_SLAVE_RQ_LOW ? NBPF_CHAN_CFG_LOEN : 0) |
		(chan->flags & NBPF_SLAVE_RQ_LEVEL ?
		 NBPF_CHAN_CFG_LVL | (NBPF_CHAN_CFG_AM & 0x200) : 0) |
		chan->terminal;
}

static void nbpf_chan_prepare_default(struct nbpf_channel *chan)
{
	/* Don't output DMAACK */
	chan->dmarq_cfg = NBPF_CHAN_CFG_AM & 0x400;
	chan->terminal = 0;
	chan->flags = 0;
}

static void nbpf_chan_configure(struct nbpf_channel *chan)
{
	/*
	 * We assume, that only the link mode and DMA request line configuration
	 * have to be set in the configuration register manually. Dynamic
	 * per-transfer configuration will be loaded from transfer descriptors.
	 */
	nbpf_chan_write(chan, NBPF_CHAN_CFG, NBPF_CHAN_CFG_DMS | chan->dmarq_cfg);
}

static u32 nbpf_xfer_ds(struct nbpf_device *nbpf, size_t size)
{
	/* Maximum supported bursts depend on the buffer size */
	return min_t(int, __ffs(size), ilog2(nbpf->config->buffer_size * 8));
}

static size_t nbpf_xfer_size(struct nbpf_device *nbpf,
			     enum dma_slave_buswidth width, u32 burst)
{
	size_t size;

	if (!burst)
		burst = 1;

	switch (width) {
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		size = 8 * burst;
		break;

	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		size = 4 * burst;
		break;

	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		size = 2 * burst;
		break;

	default:
		pr_warn("%s(): invalid bus width %u\n", __func__, width);
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
		size = burst;
	}

	return nbpf_xfer_ds(nbpf, size);
}

/*
 * We need a way to recognise slaves, whose data is sent "raw" over the bus,
 * i.e. it isn't known in advance how many bytes will be received. Therefore
 * the slave driver has to provide a "large enough" buffer and either read the
 * buffer, when it is full, or detect, that some data has arrived, then wait for
 * a timeout, if no more data arrives - receive what's already there. We want to
 * handle such slaves in a special way to allow an optimised mode for other
 * users, for whom the amount of data is known in advance. So far there's no way
 * to recognise such slaves. We use a data-width check to distinguish between
 * the SD host and the PL011 UART.
 */

static int nbpf_prep_one(struct nbpf_link_desc *ldesc,
			 enum dma_transfer_direction direction,
			 dma_addr_t src, dma_addr_t dst, size_t size, bool last)
{
	struct nbpf_link_reg *hwdesc = ldesc->hwdesc;
	struct nbpf_desc *desc = ldesc->desc;
	struct nbpf_channel *chan = desc->chan;
	struct device *dev = chan->dma_chan.device->dev;
	size_t mem_xfer, slave_xfer;
	bool can_burst;

	hwdesc->header = NBPF_HEADER_WBD | NBPF_HEADER_LV |
		(last ? NBPF_HEADER_LE : 0);

	hwdesc->src_addr = src;
	hwdesc->dst_addr = dst;
	hwdesc->transaction_size = size;

	/*
	 * set config: SAD, DAD, DDS, SDS, etc.
	 * Note on transfer sizes: the DMAC can perform unaligned DMA transfers,
	 * but it is important to have transaction size a multiple of both
	 * receiver and transmitter transfer sizes. It is also possible to use
	 * different RAM and device transfer sizes, and it does work well with
	 * some devices, e.g. with V08R07S01E SD host controllers, which can use
	 * 128 byte transfers. But this doesn't work with other devices,
	 * especially when the transaction size is unknown. This is the case,
	 * e.g. with serial drivers like amba-pl011.c. For reception it sets up
	 * the transaction size of 4K and if fewer bytes are received, it
	 * pauses DMA and reads out data received via DMA as well as those left
	 * in the Rx FIFO. For this to work with the RAM side using burst
	 * transfers we enable the SBE bit and terminate the transfer in our
	 * DMA_PAUSE handler.
	 */
	mem_xfer = nbpf_xfer_ds(chan->nbpf, size);

	switch (direction) {
	case DMA_DEV_TO_MEM:
		can_burst = chan->slave_src_width >= 3;
		slave_xfer = min(mem_xfer, can_burst ?
				 chan->slave_src_burst : chan->slave_src_width);
		/*
		 * Is the slave narrower than 64 bits, i.e. isn't using the full
		 * bus width and cannot use bursts?
		 */
		if (mem_xfer > chan->slave_src_burst && !can_burst)
			mem_xfer = chan->slave_src_burst;
		/* Device-to-RAM DMA is unreliable without REQD set */
		hwdesc->config = NBPF_CHAN_CFG_SAD | (NBPF_CHAN_CFG_DDS & (mem_xfer << 16)) |
			(NBPF_CHAN_CFG_SDS & (slave_xfer << 12)) | NBPF_CHAN_CFG_REQD |
			NBPF_CHAN_CFG_SBE;
		break;

	case DMA_MEM_TO_DEV:
		slave_xfer = min(mem_xfer, chan->slave_dst_width >= 3 ?
				 chan->slave_dst_burst : chan->slave_dst_width);
		hwdesc->config = NBPF_CHAN_CFG_DAD | (NBPF_CHAN_CFG_SDS & (mem_xfer << 12)) |
			(NBPF_CHAN_CFG_DDS & (slave_xfer << 16)) | NBPF_CHAN_CFG_REQD;
		break;

	case DMA_MEM_TO_MEM:
		hwdesc->config = NBPF_CHAN_CFG_TCM | NBPF_CHAN_CFG_TM |
			(NBPF_CHAN_CFG_SDS & (mem_xfer << 12)) |
			(NBPF_CHAN_CFG_DDS & (mem_xfer << 16));
		break;

	default:
		return -EINVAL;
	}

	hwdesc->config |= chan->dmarq_cfg | (last ? 0 : NBPF_CHAN_CFG_DEM) |
		NBPF_CHAN_CFG_DMS;

	dev_dbg(dev, "%s(): desc @ %pad: hdr 0x%x, cfg 0x%x, %zu @ %pad -> %pad\n",
		__func__, &ldesc->hwdesc_dma_addr, hwdesc->header,
		hwdesc->config, size, &src, &dst);

	dma_sync_single_for_device(dev, ldesc->hwdesc_dma_addr, sizeof(*hwdesc),
				   DMA_TO_DEVICE);

	return 0;
}

static size_t nbpf_bytes_left(struct nbpf_channel *chan)
{
	return nbpf_chan_read(chan, NBPF_CHAN_CUR_TR_BYTE);
}

static void nbpf_configure(struct nbpf_device *nbpf)
{
	nbpf_write(nbpf, NBPF_CTRL, NBPF_CTRL_LVINT);
}

static void nbpf_pause(struct nbpf_channel *chan)
{
	nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_SETSUS);
	/* See comment in nbpf_prep_one() */
	nbpf_chan_write(chan, NBPF_CHAN_CTRL, NBPF_CHAN_CTRL_CLREN);
}

/*		Generic part			*/

/* DMA ENGINE functions */
static void nbpf_issue_pending(struct dma_chan *dchan)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	unsigned long flags;

	dev_dbg(dchan->device->dev, "Entry %s()\n", __func__);

	spin_lock_irqsave(&chan->lock, flags);
	if (list_empty(&chan->queued))
		goto unlock;

	list_splice_tail_init(&chan->queued, &chan->active);

	if (!chan->running) {
		struct nbpf_desc *desc = list_first_entry(&chan->active,
						struct nbpf_desc, node);
		if (!nbpf_start(desc))
			chan->running = desc;
	}

unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

static enum dma_status nbpf_tx_status(struct dma_chan *dchan,
		dma_cookie_t cookie, struct dma_tx_state *state)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	enum dma_status status = dma_cookie_status(dchan, cookie, state);

	if (state) {
		dma_cookie_t running;
		unsigned long flags;

		spin_lock_irqsave(&chan->lock, flags);
		running = chan->running ? chan->running->async_tx.cookie : -EINVAL;

		if (cookie == running) {
			state->residue = nbpf_bytes_left(chan);
			dev_dbg(dchan->device->dev, "%s(): residue %u\n", __func__,
				state->residue);
		} else if (status == DMA_IN_PROGRESS) {
			struct nbpf_desc *desc;
			bool found = false;

			list_for_each_entry(desc, &chan->active, node)
				if (desc->async_tx.cookie == cookie) {
					found = true;
					break;
				}

			if (!found)
				list_for_each_entry(desc, &chan->queued, node)
					if (desc->async_tx.cookie == cookie) {
						found = true;
						break;

					}

			state->residue = found ? desc->length : 0;
		}

		spin_unlock_irqrestore(&chan->lock, flags);
	}

	if (chan->paused)
		status = DMA_PAUSED;

	return status;
}

static dma_cookie_t nbpf_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct nbpf_desc *desc = container_of(tx, struct nbpf_desc, async_tx);
	struct nbpf_channel *chan = desc->chan;
	unsigned long flags;
	dma_cookie_t cookie;

	spin_lock_irqsave(&chan->lock, flags);
	cookie = dma_cookie_assign(tx);
	list_add_tail(&desc->node, &chan->queued);
	spin_unlock_irqrestore(&chan->lock, flags);

	dev_dbg(chan->dma_chan.device->dev, "Entry %s(%d)\n", __func__, cookie);

	return cookie;
}

static int nbpf_desc_page_alloc(struct nbpf_channel *chan)
{
	struct dma_chan *dchan = &chan->dma_chan;
	struct nbpf_desc_page *dpage = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	struct nbpf_link_desc *ldesc;
	struct nbpf_link_reg *hwdesc;
	struct nbpf_desc *desc;
	LIST_HEAD(head);
	LIST_HEAD(lhead);
	int i;
	struct device *dev = dchan->device->dev;

	if (!dpage)
		return -ENOMEM;

	dev_dbg(dev, "%s(): alloc %lu descriptors, %lu segments, total alloc %zu\n",
		__func__, NBPF_DESCS_PER_PAGE, NBPF_SEGMENTS_PER_PAGE, sizeof(*dpage));

	for (i = 0, ldesc = dpage->ldesc, hwdesc = dpage->hwdesc;
	     i < ARRAY_SIZE(dpage->ldesc);
	     i++, ldesc++, hwdesc++) {
		ldesc->hwdesc = hwdesc;
		list_add_tail(&ldesc->node, &lhead);
		ldesc->hwdesc_dma_addr = dma_map_single(dchan->device->dev,
					hwdesc, sizeof(*hwdesc), DMA_TO_DEVICE);

		dev_dbg(dev, "%s(): mapped 0x%p to %pad\n", __func__,
			hwdesc, &ldesc->hwdesc_dma_addr);
	}

	for (i = 0, desc = dpage->desc;
	     i < ARRAY_SIZE(dpage->desc);
	     i++, desc++) {
		dma_async_tx_descriptor_init(&desc->async_tx, dchan);
		desc->async_tx.tx_submit = nbpf_tx_submit;
		desc->chan = chan;
		INIT_LIST_HEAD(&desc->sg);
		list_add_tail(&desc->node, &head);
	}

	/*
	 * This function cannot be called from interrupt context, so, no need to
	 * save flags
	 */
	spin_lock_irq(&chan->lock);
	list_splice_tail(&lhead, &chan->free_links);
	list_splice_tail(&head, &chan->free);
	list_add(&dpage->node, &chan->desc_page);
	spin_unlock_irq(&chan->lock);

	return ARRAY_SIZE(dpage->desc);
}

static void nbpf_desc_put(struct nbpf_desc *desc)
{
	struct nbpf_channel *chan = desc->chan;
	struct nbpf_link_desc *ldesc, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	list_for_each_entry_safe(ldesc, tmp, &desc->sg, node)
		list_move(&ldesc->node, &chan->free_links);

	list_add(&desc->node, &chan->free);
	spin_unlock_irqrestore(&chan->lock, flags);
}

static void nbpf_scan_acked(struct nbpf_channel *chan)
{
	struct nbpf_desc *desc, *tmp;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->lock, flags);
	list_for_each_entry_safe(desc, tmp, &chan->done, node)
		if (async_tx_test_ack(&desc->async_tx) && desc->user_wait) {
			list_move(&desc->node, &head);
			desc->user_wait = false;
		}
	spin_unlock_irqrestore(&chan->lock, flags);

	list_for_each_entry_safe(desc, tmp, &head, node) {
		list_del(&desc->node);
		nbpf_desc_put(desc);
	}
}

/*
 * We have to allocate descriptors with the channel lock dropped. This means,
 * before we re-acquire the lock buffers can be taken already, so we have to
 * re-check after re-acquiring the lock and possibly retry, if buffers are gone
 * again.
 */
static struct nbpf_desc *nbpf_desc_get(struct nbpf_channel *chan, size_t len)
{
	struct nbpf_desc *desc = NULL;
	struct nbpf_link_desc *ldesc, *prev = NULL;

	nbpf_scan_acked(chan);

	spin_lock_irq(&chan->lock);

	do {
		int i = 0, ret;

		if (list_empty(&chan->free)) {
			/* No more free descriptors */
			spin_unlock_irq(&chan->lock);
			ret = nbpf_desc_page_alloc(chan);
			if (ret < 0)
				return NULL;
			spin_lock_irq(&chan->lock);
			continue;
		}
		desc = list_first_entry(&chan->free, struct nbpf_desc, node);
		list_del(&desc->node);

		do {
			if (list_empty(&chan->free_links)) {
				/* No more free link descriptors */
				spin_unlock_irq(&chan->lock);
				ret = nbpf_desc_page_alloc(chan);
				if (ret < 0) {
					nbpf_desc_put(desc);
					return NULL;
				}
				spin_lock_irq(&chan->lock);
				continue;
			}

			ldesc = list_first_entry(&chan->free_links,
						 struct nbpf_link_desc, node);
			ldesc->desc = desc;
			if (prev)
				prev->hwdesc->next = (u32)ldesc->hwdesc_dma_addr;

			prev = ldesc;
			list_move_tail(&ldesc->node, &desc->sg);

			i++;
		} while (i < len);
	} while (!desc);

	prev->hwdesc->next = 0;

	spin_unlock_irq(&chan->lock);

	return desc;
}

static void nbpf_chan_idle(struct nbpf_channel *chan)
{
	struct nbpf_desc *desc, *tmp;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&chan->lock, flags);

	list_splice_init(&chan->done, &head);
	list_splice_init(&chan->active, &head);
	list_splice_init(&chan->queued, &head);

	chan->running = NULL;

	spin_unlock_irqrestore(&chan->lock, flags);

	list_for_each_entry_safe(desc, tmp, &head, node) {
		dev_dbg(chan->nbpf->dma_dev.dev, "%s(): force-free desc %p cookie %d\n",
			__func__, desc, desc->async_tx.cookie);
		list_del(&desc->node);
		nbpf_desc_put(desc);
	}
}

static int nbpf_control(struct dma_chan *dchan, enum dma_ctrl_cmd cmd,
			unsigned long arg)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	struct dma_slave_config *config;

	dev_dbg(dchan->device->dev, "Entry %s(%d)\n", __func__, cmd);

	switch (cmd) {
	case DMA_TERMINATE_ALL:
		dev_dbg(dchan->device->dev, "Terminating\n");
		nbpf_chan_halt(chan);
		nbpf_chan_idle(chan);
		break;

	case DMA_SLAVE_CONFIG:
		if (!arg)
			return -EINVAL;
		config = (struct dma_slave_config *)arg;

		/*
		 * We could check config->slave_id to match chan->terminal here,
		 * but with DT they would be coming from the same source, so
		 * such a check would be superflous
		 */

		chan->slave_dst_addr = config->dst_addr;
		chan->slave_dst_width = nbpf_xfer_size(chan->nbpf,
						       config->dst_addr_width, 1);
		chan->slave_dst_burst = nbpf_xfer_size(chan->nbpf,
						       config->dst_addr_width,
						       config->dst_maxburst);
		chan->slave_src_addr = config->src_addr;
		chan->slave_src_width = nbpf_xfer_size(chan->nbpf,
						       config->src_addr_width, 1);
		chan->slave_src_burst = nbpf_xfer_size(chan->nbpf,
						       config->src_addr_width,
						       config->src_maxburst);
		break;

	case DMA_PAUSE:
		chan->paused = true;
		nbpf_pause(chan);
		break;

	default:
		return -ENXIO;
	}

	return 0;
}

static struct dma_async_tx_descriptor *nbpf_prep_sg(struct nbpf_channel *chan,
		struct scatterlist *src_sg, struct scatterlist *dst_sg,
		size_t len, enum dma_transfer_direction direction,
		unsigned long flags)
{
	struct nbpf_link_desc *ldesc;
	struct scatterlist *mem_sg;
	struct nbpf_desc *desc;
	bool inc_src, inc_dst;
	size_t data_len = 0;
	int i = 0;

	switch (direction) {
	case DMA_DEV_TO_MEM:
		mem_sg = dst_sg;
		inc_src = false;
		inc_dst = true;
		break;

	case DMA_MEM_TO_DEV:
		mem_sg = src_sg;
		inc_src = true;
		inc_dst = false;
		break;

	default:
	case DMA_MEM_TO_MEM:
		mem_sg = src_sg;
		inc_src = true;
		inc_dst = true;
	}

	desc = nbpf_desc_get(chan, len);
	if (!desc)
		return NULL;

	desc->async_tx.flags = flags;
	desc->async_tx.cookie = -EBUSY;
	desc->user_wait = false;

	/*
	 * This is a private descriptor list, and we own the descriptor. No need
	 * to lock.
	 */
	list_for_each_entry(ldesc, &desc->sg, node) {
		int ret = nbpf_prep_one(ldesc, direction,
					sg_dma_address(src_sg),
					sg_dma_address(dst_sg),
					sg_dma_len(mem_sg),
					i == len - 1);
		if (ret < 0) {
			nbpf_desc_put(desc);
			return NULL;
		}
		data_len += sg_dma_len(mem_sg);
		if (inc_src)
			src_sg = sg_next(src_sg);
		if (inc_dst)
			dst_sg = sg_next(dst_sg);
		mem_sg = direction == DMA_DEV_TO_MEM ? dst_sg : src_sg;
		i++;
	}

	desc->length = data_len;

	/* The user has to return the descriptor to us ASAP via .tx_submit() */
	return &desc->async_tx;
}

static struct dma_async_tx_descriptor *nbpf_prep_memcpy(
	struct dma_chan *dchan, dma_addr_t dst, dma_addr_t src,
	size_t len, unsigned long flags)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	struct scatterlist dst_sg;
	struct scatterlist src_sg;

	sg_init_table(&dst_sg, 1);
	sg_init_table(&src_sg, 1);

	sg_dma_address(&dst_sg) = dst;
	sg_dma_address(&src_sg) = src;

	sg_dma_len(&dst_sg) = len;
	sg_dma_len(&src_sg) = len;

	dev_dbg(dchan->device->dev, "%s(): %zu @ %pad -> %pad\n",
		__func__, len, &src, &dst);

	return nbpf_prep_sg(chan, &src_sg, &dst_sg, 1,
			    DMA_MEM_TO_MEM, flags);
}

static struct dma_async_tx_descriptor *nbpf_prep_memcpy_sg(
	struct dma_chan *dchan,
	struct scatterlist *dst_sg, unsigned int dst_nents,
	struct scatterlist *src_sg, unsigned int src_nents,
	unsigned long flags)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);

	if (dst_nents != src_nents)
		return NULL;

	return nbpf_prep_sg(chan, src_sg, dst_sg, src_nents,
			    DMA_MEM_TO_MEM, flags);
}

static struct dma_async_tx_descriptor *nbpf_prep_slave_sg(
	struct dma_chan *dchan, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags, void *context)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	struct scatterlist slave_sg;

	dev_dbg(dchan->device->dev, "Entry %s()\n", __func__);

	sg_init_table(&slave_sg, 1);

	switch (direction) {
	case DMA_MEM_TO_DEV:
		sg_dma_address(&slave_sg) = chan->slave_dst_addr;
		return nbpf_prep_sg(chan, sgl, &slave_sg, sg_len,
				    direction, flags);

	case DMA_DEV_TO_MEM:
		sg_dma_address(&slave_sg) = chan->slave_src_addr;
		return nbpf_prep_sg(chan, &slave_sg, sgl, sg_len,
				    direction, flags);

	default:
		return NULL;
	}
}

static int nbpf_alloc_chan_resources(struct dma_chan *dchan)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	int ret;

	INIT_LIST_HEAD(&chan->free);
	INIT_LIST_HEAD(&chan->free_links);
	INIT_LIST_HEAD(&chan->queued);
	INIT_LIST_HEAD(&chan->active);
	INIT_LIST_HEAD(&chan->done);

	ret = nbpf_desc_page_alloc(chan);
	if (ret < 0)
		return ret;

	dev_dbg(dchan->device->dev, "Entry %s(): terminal %u\n", __func__,
		chan->terminal);

	nbpf_chan_configure(chan);

	return ret;
}

static void nbpf_free_chan_resources(struct dma_chan *dchan)
{
	struct nbpf_channel *chan = nbpf_to_chan(dchan);
	struct nbpf_desc_page *dpage, *tmp;

	dev_dbg(dchan->device->dev, "Entry %s()\n", __func__);

	nbpf_chan_halt(chan);
	nbpf_chan_idle(chan);
	/* Clean up for if a channel is re-used for MEMCPY after slave DMA */
	nbpf_chan_prepare_default(chan);

	list_for_each_entry_safe(dpage, tmp, &chan->desc_page, node) {
		struct nbpf_link_desc *ldesc;
		int i;
		list_del(&dpage->node);
		for (i = 0, ldesc = dpage->ldesc;
		     i < ARRAY_SIZE(dpage->ldesc);
		     i++, ldesc++)
			dma_unmap_single(dchan->device->dev, ldesc->hwdesc_dma_addr,
					 sizeof(*ldesc->hwdesc), DMA_TO_DEVICE);
		free_page((unsigned long)dpage);
	}
}

static int nbpf_slave_caps(struct dma_chan *dchan,
			   struct dma_slave_caps *caps)
{
	caps->src_addr_widths = NBPF_DMA_BUSWIDTHS;
	caps->dstn_addr_widths = NBPF_DMA_BUSWIDTHS;
	caps->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	caps->cmd_pause = false;
	caps->cmd_terminate = true;

	return 0;
}

static struct dma_chan *nbpf_of_xlate(struct of_phandle_args *dma_spec,
				      struct of_dma *ofdma)
{
	struct nbpf_device *nbpf = ofdma->of_dma_data;
	struct dma_chan *dchan;
	struct nbpf_channel *chan;

	if (dma_spec->args_count != 2)
		return NULL;

	dchan = dma_get_any_slave_channel(&nbpf->dma_dev);
	if (!dchan)
		return NULL;

	dev_dbg(dchan->device->dev, "Entry %s(%s)\n", __func__,
		dma_spec->np->name);

	chan = nbpf_to_chan(dchan);

	chan->terminal = dma_spec->args[0];
	chan->flags = dma_spec->args[1];

	nbpf_chan_prepare(chan);
	nbpf_chan_configure(chan);

	return dchan;
}

static irqreturn_t nbpf_chan_irqt(int irq, void *dev)
{
	struct nbpf_channel *chan = dev;
	struct nbpf_desc *desc, *tmp;
	dma_async_tx_callback callback;
	void *param;

	while (!list_empty(&chan->done)) {
		bool found = false, must_put, recycling = false;

		spin_lock_irq(&chan->lock);

		list_for_each_entry_safe(desc, tmp, &chan->done, node) {
			if (!desc->user_wait) {
				/* Newly completed descriptor, have to process */
				found = true;
				break;
			} else if (async_tx_test_ack(&desc->async_tx)) {
				/*
				 * This descriptor was waiting for a user ACK,
				 * it can be recycled now.
				 */
				list_del(&desc->node);
				spin_unlock_irq(&chan->lock);
				nbpf_desc_put(desc);
				recycling = true;
				break;
			}
		}

		if (recycling)
			continue;

		if (!found) {
			/* This can happen if TERMINATE_ALL has been called */
			spin_unlock_irq(&chan->lock);
			break;
		}

		dma_cookie_complete(&desc->async_tx);

		/*
		 * With released lock we cannot dereference desc, maybe it's
		 * still on the "done" list
		 */
		if (async_tx_test_ack(&desc->async_tx)) {
			list_del(&desc->node);
			must_put = true;
		} else {
			desc->user_wait = true;
			must_put = false;
		}

		callback = desc->async_tx.callback;
		param = desc->async_tx.callback_param;

		/* ack and callback completed descriptor */
		spin_unlock_irq(&chan->lock);

		if (callback)
			callback(param);

		if (must_put)
			nbpf_desc_put(desc);
	}

	return IRQ_HANDLED;
}

static irqreturn_t nbpf_chan_irq(int irq, void *dev)
{
	struct nbpf_channel *chan = dev;
	bool done = nbpf_status_get(chan);
	struct nbpf_desc *desc;
	irqreturn_t ret;

	if (!done)
		return IRQ_NONE;

	nbpf_status_ack(chan);

	dev_dbg(&chan->dma_chan.dev->device, "%s()\n", __func__);

	spin_lock(&chan->lock);
	desc = chan->running;
	if (WARN_ON(!desc)) {
		ret = IRQ_NONE;
		goto unlock;
	} else {
		ret = IRQ_WAKE_THREAD;
	}

	list_move_tail(&desc->node, &chan->done);
	chan->running = NULL;

	if (!list_empty(&chan->active)) {
		desc = list_first_entry(&chan->active,
					struct nbpf_desc, node);
		if (!nbpf_start(desc))
			chan->running = desc;
	}

unlock:
	spin_unlock(&chan->lock);

	return ret;
}

static irqreturn_t nbpf_err_irq(int irq, void *dev)
{
	struct nbpf_device *nbpf = dev;
	u32 error = nbpf_error_get(nbpf);

	dev_warn(nbpf->dma_dev.dev, "DMA error IRQ %u\n", irq);

	if (!error)
		return IRQ_NONE;

	do {
		struct nbpf_channel *chan = nbpf_error_get_channel(nbpf, error);
		/* On error: abort all queued transfers, no callback */
		nbpf_error_clear(chan);
		nbpf_chan_idle(chan);
		error = nbpf_error_get(nbpf);
	} while (error);

	return IRQ_HANDLED;
}

static int nbpf_chan_probe(struct nbpf_device *nbpf, int n)
{
	struct dma_device *dma_dev = &nbpf->dma_dev;
	struct nbpf_channel *chan = nbpf->chan + n;
	int ret;

	chan->nbpf = nbpf;
	chan->base = nbpf->base + NBPF_REG_CHAN_OFFSET + NBPF_REG_CHAN_SIZE * n;
	INIT_LIST_HEAD(&chan->desc_page);
	spin_lock_init(&chan->lock);
	chan->dma_chan.device = dma_dev;
	dma_cookie_init(&chan->dma_chan);
	nbpf_chan_prepare_default(chan);

	dev_dbg(dma_dev->dev, "%s(): channel %d: -> %p\n", __func__, n, chan->base);

	snprintf(chan->name, sizeof(chan->name), "nbpf %d", n);

	ret = devm_request_threaded_irq(dma_dev->dev, chan->irq,
			nbpf_chan_irq, nbpf_chan_irqt, IRQF_SHARED,
			chan->name, chan);
	if (ret < 0)
		return ret;

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->dma_chan.device_node,
		      &dma_dev->channels);

	return 0;
}

static const struct of_device_id nbpf_match[] = {
	{.compatible = "renesas,nbpfaxi64dmac1b4",	.data = &nbpf_cfg[NBPF1B4]},
	{.compatible = "renesas,nbpfaxi64dmac1b8",	.data = &nbpf_cfg[NBPF1B8]},
	{.compatible = "renesas,nbpfaxi64dmac1b16",	.data = &nbpf_cfg[NBPF1B16]},
	{.compatible = "renesas,nbpfaxi64dmac4b4",	.data = &nbpf_cfg[NBPF4B4]},
	{.compatible = "renesas,nbpfaxi64dmac4b8",	.data = &nbpf_cfg[NBPF4B8]},
	{.compatible = "renesas,nbpfaxi64dmac4b16",	.data = &nbpf_cfg[NBPF4B16]},
	{.compatible = "renesas,nbpfaxi64dmac8b4",	.data = &nbpf_cfg[NBPF8B4]},
	{.compatible = "renesas,nbpfaxi64dmac8b8",	.data = &nbpf_cfg[NBPF8B8]},
	{.compatible = "renesas,nbpfaxi64dmac8b16",	.data = &nbpf_cfg[NBPF8B16]},
	{}
};
MODULE_DEVICE_TABLE(of, nbpf_match);

static int nbpf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id = of_match_device(nbpf_match, dev);
	struct device_node *np = dev->of_node;
	struct nbpf_device *nbpf;
	struct dma_device *dma_dev;
	struct resource *iomem, *irq_res;
	const struct nbpf_config *cfg;
	int num_channels;
	int ret, irq, eirq, i;
	int irqbuf[9] /* maximum 8 channels + error IRQ */;
	unsigned int irqs = 0;

	BUILD_BUG_ON(sizeof(struct nbpf_desc_page) > PAGE_SIZE);

	/* DT only */
	if (!np || !of_id || !of_id->data)
		return -ENODEV;

	cfg = of_id->data;
	num_channels = cfg->num_channels;

	nbpf = devm_kzalloc(dev, sizeof(*nbpf) + num_channels *
			    sizeof(nbpf->chan[0]), GFP_KERNEL);
	if (!nbpf) {
		dev_err(dev, "Memory allocation failed\n");
		return -ENOMEM;
	}
	dma_dev = &nbpf->dma_dev;
	dma_dev->dev = dev;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nbpf->base = devm_ioremap_resource(dev, iomem);
	if (IS_ERR(nbpf->base))
		return PTR_ERR(nbpf->base);

	nbpf->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(nbpf->clk))
		return PTR_ERR(nbpf->clk);

	nbpf->config = cfg;

	for (i = 0; irqs < ARRAY_SIZE(irqbuf); i++) {
		irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!irq_res)
			break;

		for (irq = irq_res->start; irq <= irq_res->end;
		     irq++, irqs++)
			irqbuf[irqs] = irq;
	}

	/*
	 * 3 IRQ resource schemes are supported:
	 * 1. 1 shared IRQ for error and all channels
	 * 2. 2 IRQs: one for error and one shared for all channels
	 * 3. 1 IRQ for error and an own IRQ for each channel
	 */
	if (irqs != 1 && irqs != 2 && irqs != num_channels + 1)
		return -ENXIO;

	if (irqs == 1) {
		eirq = irqbuf[0];

		for (i = 0; i <= num_channels; i++)
			nbpf->chan[i].irq = irqbuf[0];
	} else {
		eirq = platform_get_irq_byname(pdev, "error");
		if (eirq < 0)
			return eirq;

		if (irqs == num_channels + 1) {
			struct nbpf_channel *chan;

			for (i = 0, chan = nbpf->chan; i <= num_channels;
			     i++, chan++) {
				/* Skip the error IRQ */
				if (irqbuf[i] == eirq)
					i++;
				chan->irq = irqbuf[i];
			}

			if (chan != nbpf->chan + num_channels)
				return -EINVAL;
		} else {
			/* 2 IRQs and more than one channel */
			if (irqbuf[0] == eirq)
				irq = irqbuf[1];
			else
				irq = irqbuf[0];

			for (i = 0; i <= num_channels; i++)
				nbpf->chan[i].irq = irq;
		}
	}

	ret = devm_request_irq(dev, eirq, nbpf_err_irq,
			       IRQF_SHARED, "dma error", nbpf);
	if (ret < 0)
		return ret;

	INIT_LIST_HEAD(&dma_dev->channels);

	/* Create DMA Channel */
	for (i = 0; i < num_channels; i++) {
		ret = nbpf_chan_probe(nbpf, i);
		if (ret < 0)
			return ret;
	}

	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);
	dma_cap_set(DMA_SG, dma_dev->cap_mask);

	/* Common and MEMCPY operations */
	dma_dev->device_alloc_chan_resources
		= nbpf_alloc_chan_resources;
	dma_dev->device_free_chan_resources = nbpf_free_chan_resources;
	dma_dev->device_prep_dma_sg = nbpf_prep_memcpy_sg;
	dma_dev->device_prep_dma_memcpy = nbpf_prep_memcpy;
	dma_dev->device_tx_status = nbpf_tx_status;
	dma_dev->device_issue_pending = nbpf_issue_pending;
	dma_dev->device_slave_caps = nbpf_slave_caps;

	/*
	 * If we drop support for unaligned MEMCPY buffer addresses and / or
	 * lengths by setting
	 * dma_dev->copy_align = 4;
	 * then we can set transfer length to 4 bytes in nbpf_prep_one() for
	 * DMA_MEM_TO_MEM
	 */

	/* Compulsory for DMA_SLAVE fields */
	dma_dev->device_prep_slave_sg = nbpf_prep_slave_sg;
	dma_dev->device_control = nbpf_control;

	platform_set_drvdata(pdev, nbpf);

	ret = clk_prepare_enable(nbpf->clk);
	if (ret < 0)
		return ret;

	nbpf_configure(nbpf);

	ret = dma_async_device_register(dma_dev);
	if (ret < 0)
		goto e_clk_off;

	ret = of_dma_controller_register(np, nbpf_of_xlate, nbpf);
	if (ret < 0)
		goto e_dma_dev_unreg;

	return 0;

e_dma_dev_unreg:
	dma_async_device_unregister(dma_dev);
e_clk_off:
	clk_disable_unprepare(nbpf->clk);

	return ret;
}

static int nbpf_remove(struct platform_device *pdev)
{
	struct nbpf_device *nbpf = platform_get_drvdata(pdev);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&nbpf->dma_dev);
	clk_disable_unprepare(nbpf->clk);

	return 0;
}

static struct platform_device_id nbpf_ids[] = {
	{"nbpfaxi64dmac1b4",	(kernel_ulong_t)&nbpf_cfg[NBPF1B4]},
	{"nbpfaxi64dmac1b8",	(kernel_ulong_t)&nbpf_cfg[NBPF1B8]},
	{"nbpfaxi64dmac1b16",	(kernel_ulong_t)&nbpf_cfg[NBPF1B16]},
	{"nbpfaxi64dmac4b4",	(kernel_ulong_t)&nbpf_cfg[NBPF4B4]},
	{"nbpfaxi64dmac4b8",	(kernel_ulong_t)&nbpf_cfg[NBPF4B8]},
	{"nbpfaxi64dmac4b16",	(kernel_ulong_t)&nbpf_cfg[NBPF4B16]},
	{"nbpfaxi64dmac8b4",	(kernel_ulong_t)&nbpf_cfg[NBPF8B4]},
	{"nbpfaxi64dmac8b8",	(kernel_ulong_t)&nbpf_cfg[NBPF8B8]},
	{"nbpfaxi64dmac8b16",	(kernel_ulong_t)&nbpf_cfg[NBPF8B16]},
	{},
};
MODULE_DEVICE_TABLE(platform, nbpf_ids);

#ifdef CONFIG_PM_RUNTIME
static int nbpf_runtime_suspend(struct device *dev)
{
	struct nbpf_device *nbpf = platform_get_drvdata(to_platform_device(dev));
	clk_disable_unprepare(nbpf->clk);
	return 0;
}

static int nbpf_runtime_resume(struct device *dev)
{
	struct nbpf_device *nbpf = platform_get_drvdata(to_platform_device(dev));
	return clk_prepare_enable(nbpf->clk);
}
#endif

static const struct dev_pm_ops nbpf_pm_ops = {
	SET_RUNTIME_PM_OPS(nbpf_runtime_suspend, nbpf_runtime_resume, NULL)
};

static struct platform_driver nbpf_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "dma-nbpf",
		.of_match_table = nbpf_match,
		.pm = &nbpf_pm_ops,
	},
	.id_table = nbpf_ids,
	.probe = nbpf_probe,
	.remove = nbpf_remove,
};

module_platform_driver(nbpf_driver);

MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_DESCRIPTION("dmaengine driver for NBPFAXI64* DMACs");
MODULE_LICENSE("GPL v2");
