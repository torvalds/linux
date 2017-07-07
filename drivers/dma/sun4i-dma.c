/*
 * Copyright (C) 2014 Emilio López
 * Emilio López <emilio@elopez.com.ar>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "virt-dma.h"

/** Common macros to normal and dedicated DMA registers **/

#define SUN4I_DMA_CFG_LOADING			BIT(31)
#define SUN4I_DMA_CFG_DST_DATA_WIDTH(width)	((width) << 25)
#define SUN4I_DMA_CFG_DST_BURST_LENGTH(len)	((len) << 23)
#define SUN4I_DMA_CFG_DST_ADDR_MODE(mode)	((mode) << 21)
#define SUN4I_DMA_CFG_DST_DRQ_TYPE(type)	((type) << 16)
#define SUN4I_DMA_CFG_SRC_DATA_WIDTH(width)	((width) << 9)
#define SUN4I_DMA_CFG_SRC_BURST_LENGTH(len)	((len) << 7)
#define SUN4I_DMA_CFG_SRC_ADDR_MODE(mode)	((mode) << 5)
#define SUN4I_DMA_CFG_SRC_DRQ_TYPE(type)	(type)

/** Normal DMA register values **/

/* Normal DMA source/destination data request type values */
#define SUN4I_NDMA_DRQ_TYPE_SDRAM		0x16
#define SUN4I_NDMA_DRQ_TYPE_LIMIT		(0x1F + 1)

/** Normal DMA register layout **/

/* Dedicated DMA source/destination address mode values */
#define SUN4I_NDMA_ADDR_MODE_LINEAR		0
#define SUN4I_NDMA_ADDR_MODE_IO			1

/* Normal DMA configuration register layout */
#define SUN4I_NDMA_CFG_CONT_MODE		BIT(30)
#define SUN4I_NDMA_CFG_WAIT_STATE(n)		((n) << 27)
#define SUN4I_NDMA_CFG_DST_NON_SECURE		BIT(22)
#define SUN4I_NDMA_CFG_BYTE_COUNT_MODE_REMAIN	BIT(15)
#define SUN4I_NDMA_CFG_SRC_NON_SECURE		BIT(6)

/** Dedicated DMA register values **/

/* Dedicated DMA source/destination address mode values */
#define SUN4I_DDMA_ADDR_MODE_LINEAR		0
#define SUN4I_DDMA_ADDR_MODE_IO			1
#define SUN4I_DDMA_ADDR_MODE_HORIZONTAL_PAGE	2
#define SUN4I_DDMA_ADDR_MODE_VERTICAL_PAGE	3

/* Dedicated DMA source/destination data request type values */
#define SUN4I_DDMA_DRQ_TYPE_SDRAM		0x1
#define SUN4I_DDMA_DRQ_TYPE_LIMIT		(0x1F + 1)

/** Dedicated DMA register layout **/

/* Dedicated DMA configuration register layout */
#define SUN4I_DDMA_CFG_BUSY			BIT(30)
#define SUN4I_DDMA_CFG_CONT_MODE		BIT(29)
#define SUN4I_DDMA_CFG_DST_NON_SECURE		BIT(28)
#define SUN4I_DDMA_CFG_BYTE_COUNT_MODE_REMAIN	BIT(15)
#define SUN4I_DDMA_CFG_SRC_NON_SECURE		BIT(12)

/* Dedicated DMA parameter register layout */
#define SUN4I_DDMA_PARA_DST_DATA_BLK_SIZE(n)	(((n) - 1) << 24)
#define SUN4I_DDMA_PARA_DST_WAIT_CYCLES(n)	(((n) - 1) << 16)
#define SUN4I_DDMA_PARA_SRC_DATA_BLK_SIZE(n)	(((n) - 1) << 8)
#define SUN4I_DDMA_PARA_SRC_WAIT_CYCLES(n)	(((n) - 1) << 0)

/** DMA register offsets **/

/* General register offsets */
#define SUN4I_DMA_IRQ_ENABLE_REG		0x0
#define SUN4I_DMA_IRQ_PENDING_STATUS_REG	0x4

/* Normal DMA register offsets */
#define SUN4I_NDMA_CHANNEL_REG_BASE(n)		(0x100 + (n) * 0x20)
#define SUN4I_NDMA_CFG_REG			0x0
#define SUN4I_NDMA_SRC_ADDR_REG			0x4
#define SUN4I_NDMA_DST_ADDR_REG		0x8
#define SUN4I_NDMA_BYTE_COUNT_REG		0xC

/* Dedicated DMA register offsets */
#define SUN4I_DDMA_CHANNEL_REG_BASE(n)		(0x300 + (n) * 0x20)
#define SUN4I_DDMA_CFG_REG			0x0
#define SUN4I_DDMA_SRC_ADDR_REG			0x4
#define SUN4I_DDMA_DST_ADDR_REG		0x8
#define SUN4I_DDMA_BYTE_COUNT_REG		0xC
#define SUN4I_DDMA_PARA_REG			0x18

/** DMA Driver **/

/*
 * Normal DMA has 8 channels, and Dedicated DMA has another 8, so
 * that's 16 channels. As for endpoints, there's 29 and 21
 * respectively. Given that the Normal DMA endpoints (other than
 * SDRAM) can be used as tx/rx, we need 78 vchans in total
 */
#define SUN4I_NDMA_NR_MAX_CHANNELS	8
#define SUN4I_DDMA_NR_MAX_CHANNELS	8
#define SUN4I_DMA_NR_MAX_CHANNELS					\
	(SUN4I_NDMA_NR_MAX_CHANNELS + SUN4I_DDMA_NR_MAX_CHANNELS)
#define SUN4I_NDMA_NR_MAX_VCHANS	(29 * 2 - 1)
#define SUN4I_DDMA_NR_MAX_VCHANS	21
#define SUN4I_DMA_NR_MAX_VCHANS						\
	(SUN4I_NDMA_NR_MAX_VCHANS + SUN4I_DDMA_NR_MAX_VCHANS)

/* This set of SUN4I_DDMA timing parameters were found experimentally while
 * working with the SPI driver and seem to make it behave correctly */
#define SUN4I_DDMA_MAGIC_SPI_PARAMETERS \
	(SUN4I_DDMA_PARA_DST_DATA_BLK_SIZE(1) |			\
	 SUN4I_DDMA_PARA_SRC_DATA_BLK_SIZE(1) |				\
	 SUN4I_DDMA_PARA_DST_WAIT_CYCLES(2) |				\
	 SUN4I_DDMA_PARA_SRC_WAIT_CYCLES(2))

struct sun4i_dma_pchan {
	/* Register base of channel */
	void __iomem			*base;
	/* vchan currently being serviced */
	struct sun4i_dma_vchan		*vchan;
	/* Is this a dedicated pchan? */
	int				is_dedicated;
};

struct sun4i_dma_vchan {
	struct virt_dma_chan		vc;
	struct dma_slave_config		cfg;
	struct sun4i_dma_pchan		*pchan;
	struct sun4i_dma_promise	*processing;
	struct sun4i_dma_contract	*contract;
	u8				endpoint;
	int				is_dedicated;
};

struct sun4i_dma_promise {
	u32				cfg;
	u32				para;
	dma_addr_t			src;
	dma_addr_t			dst;
	size_t				len;
	struct list_head		list;
};

/* A contract is a set of promises */
struct sun4i_dma_contract {
	struct virt_dma_desc		vd;
	struct list_head		demands;
	struct list_head		completed_demands;
	int				is_cyclic;
};

struct sun4i_dma_dev {
	DECLARE_BITMAP(pchans_used, SUN4I_DMA_NR_MAX_CHANNELS);
	struct dma_device		slave;
	struct sun4i_dma_pchan		*pchans;
	struct sun4i_dma_vchan		*vchans;
	void __iomem			*base;
	struct clk			*clk;
	int				irq;
	spinlock_t			lock;
};

static struct sun4i_dma_dev *to_sun4i_dma_dev(struct dma_device *dev)
{
	return container_of(dev, struct sun4i_dma_dev, slave);
}

static struct sun4i_dma_vchan *to_sun4i_dma_vchan(struct dma_chan *chan)
{
	return container_of(chan, struct sun4i_dma_vchan, vc.chan);
}

static struct sun4i_dma_contract *to_sun4i_dma_contract(struct virt_dma_desc *vd)
{
	return container_of(vd, struct sun4i_dma_contract, vd);
}

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static int convert_burst(u32 maxburst)
{
	if (maxburst > 8)
		return -EINVAL;

	/* 1 -> 0, 4 -> 1, 8 -> 2 */
	return (maxburst >> 2);
}

static int convert_buswidth(enum dma_slave_buswidth addr_width)
{
	if (addr_width > DMA_SLAVE_BUSWIDTH_4_BYTES)
		return -EINVAL;

	/* 8 (1 byte) -> 0, 16 (2 bytes) -> 1, 32 (4 bytes) -> 2 */
	return (addr_width >> 1);
}

static void sun4i_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);

	vchan_free_chan_resources(&vchan->vc);
}

static struct sun4i_dma_pchan *find_and_use_pchan(struct sun4i_dma_dev *priv,
						  struct sun4i_dma_vchan *vchan)
{
	struct sun4i_dma_pchan *pchan = NULL, *pchans = priv->pchans;
	unsigned long flags;
	int i, max;

	/*
	 * pchans 0-SUN4I_NDMA_NR_MAX_CHANNELS are normal, and
	 * SUN4I_NDMA_NR_MAX_CHANNELS+ are dedicated ones
	 */
	if (vchan->is_dedicated) {
		i = SUN4I_NDMA_NR_MAX_CHANNELS;
		max = SUN4I_DMA_NR_MAX_CHANNELS;
	} else {
		i = 0;
		max = SUN4I_NDMA_NR_MAX_CHANNELS;
	}

	spin_lock_irqsave(&priv->lock, flags);
	for_each_clear_bit_from(i, priv->pchans_used, max) {
		pchan = &pchans[i];
		pchan->vchan = vchan;
		set_bit(i, priv->pchans_used);
		break;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return pchan;
}

static void release_pchan(struct sun4i_dma_dev *priv,
			  struct sun4i_dma_pchan *pchan)
{
	unsigned long flags;
	int nr = pchan - priv->pchans;

	spin_lock_irqsave(&priv->lock, flags);

	pchan->vchan = NULL;
	clear_bit(nr, priv->pchans_used);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void configure_pchan(struct sun4i_dma_pchan *pchan,
			    struct sun4i_dma_promise *d)
{
	/*
	 * Configure addresses and misc parameters depending on type
	 * SUN4I_DDMA has an extra field with timing parameters
	 */
	if (pchan->is_dedicated) {
		writel_relaxed(d->src, pchan->base + SUN4I_DDMA_SRC_ADDR_REG);
		writel_relaxed(d->dst, pchan->base + SUN4I_DDMA_DST_ADDR_REG);
		writel_relaxed(d->len, pchan->base + SUN4I_DDMA_BYTE_COUNT_REG);
		writel_relaxed(d->para, pchan->base + SUN4I_DDMA_PARA_REG);
		writel_relaxed(d->cfg, pchan->base + SUN4I_DDMA_CFG_REG);
	} else {
		writel_relaxed(d->src, pchan->base + SUN4I_NDMA_SRC_ADDR_REG);
		writel_relaxed(d->dst, pchan->base + SUN4I_NDMA_DST_ADDR_REG);
		writel_relaxed(d->len, pchan->base + SUN4I_NDMA_BYTE_COUNT_REG);
		writel_relaxed(d->cfg, pchan->base + SUN4I_NDMA_CFG_REG);
	}
}

static void set_pchan_interrupt(struct sun4i_dma_dev *priv,
				struct sun4i_dma_pchan *pchan,
				int half, int end)
{
	u32 reg;
	int pchan_number = pchan - priv->pchans;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	reg = readl_relaxed(priv->base + SUN4I_DMA_IRQ_ENABLE_REG);

	if (half)
		reg |= BIT(pchan_number * 2);
	else
		reg &= ~BIT(pchan_number * 2);

	if (end)
		reg |= BIT(pchan_number * 2 + 1);
	else
		reg &= ~BIT(pchan_number * 2 + 1);

	writel_relaxed(reg, priv->base + SUN4I_DMA_IRQ_ENABLE_REG);

	spin_unlock_irqrestore(&priv->lock, flags);
}

/**
 * Execute pending operations on a vchan
 *
 * When given a vchan, this function will try to acquire a suitable
 * pchan and, if successful, will configure it to fulfill a promise
 * from the next pending contract.
 *
 * This function must be called with &vchan->vc.lock held.
 */
static int __execute_vchan_pending(struct sun4i_dma_dev *priv,
				   struct sun4i_dma_vchan *vchan)
{
	struct sun4i_dma_promise *promise = NULL;
	struct sun4i_dma_contract *contract = NULL;
	struct sun4i_dma_pchan *pchan;
	struct virt_dma_desc *vd;
	int ret;

	lockdep_assert_held(&vchan->vc.lock);

	/* We need a pchan to do anything, so secure one if available */
	pchan = find_and_use_pchan(priv, vchan);
	if (!pchan)
		return -EBUSY;

	/*
	 * Channel endpoints must not be repeated, so if this vchan
	 * has already submitted some work, we can't do anything else
	 */
	if (vchan->processing) {
		dev_dbg(chan2dev(&vchan->vc.chan),
			"processing something to this endpoint already\n");
		ret = -EBUSY;
		goto release_pchan;
	}

	do {
		/* Figure out which contract we're working with today */
		vd = vchan_next_desc(&vchan->vc);
		if (!vd) {
			dev_dbg(chan2dev(&vchan->vc.chan),
				"No pending contract found");
			ret = 0;
			goto release_pchan;
		}

		contract = to_sun4i_dma_contract(vd);
		if (list_empty(&contract->demands)) {
			/* The contract has been completed so mark it as such */
			list_del(&contract->vd.node);
			vchan_cookie_complete(&contract->vd);
			dev_dbg(chan2dev(&vchan->vc.chan),
				"Empty contract found and marked complete");
		}
	} while (list_empty(&contract->demands));

	/* Now find out what we need to do */
	promise = list_first_entry(&contract->demands,
				   struct sun4i_dma_promise, list);
	vchan->processing = promise;

	/* ... and make it reality */
	if (promise) {
		vchan->contract = contract;
		vchan->pchan = pchan;
		set_pchan_interrupt(priv, pchan, contract->is_cyclic, 1);
		configure_pchan(pchan, promise);
	}

	return 0;

release_pchan:
	release_pchan(priv, pchan);
	return ret;
}

static int sanitize_config(struct dma_slave_config *sconfig,
			   enum dma_transfer_direction direction)
{
	switch (direction) {
	case DMA_MEM_TO_DEV:
		if ((sconfig->dst_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED) ||
		    !sconfig->dst_maxburst)
			return -EINVAL;

		if (sconfig->src_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
			sconfig->src_addr_width = sconfig->dst_addr_width;

		if (!sconfig->src_maxburst)
			sconfig->src_maxburst = sconfig->dst_maxburst;

		break;

	case DMA_DEV_TO_MEM:
		if ((sconfig->src_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED) ||
		    !sconfig->src_maxburst)
			return -EINVAL;

		if (sconfig->dst_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
			sconfig->dst_addr_width = sconfig->src_addr_width;

		if (!sconfig->dst_maxburst)
			sconfig->dst_maxburst = sconfig->src_maxburst;

		break;
	default:
		return 0;
	}

	return 0;
}

/**
 * Generate a promise, to be used in a normal DMA contract.
 *
 * A NDMA promise contains all the information required to program the
 * normal part of the DMA Engine and get data copied. A non-executed
 * promise will live in the demands list on a contract. Once it has been
 * completed, it will be moved to the completed demands list for later freeing.
 * All linked promises will be freed when the corresponding contract is freed
 */
static struct sun4i_dma_promise *
generate_ndma_promise(struct dma_chan *chan, dma_addr_t src, dma_addr_t dest,
		      size_t len, struct dma_slave_config *sconfig,
		      enum dma_transfer_direction direction)
{
	struct sun4i_dma_promise *promise;
	int ret;

	ret = sanitize_config(sconfig, direction);
	if (ret)
		return NULL;

	promise = kzalloc(sizeof(*promise), GFP_NOWAIT);
	if (!promise)
		return NULL;

	promise->src = src;
	promise->dst = dest;
	promise->len = len;
	promise->cfg = SUN4I_DMA_CFG_LOADING |
		SUN4I_NDMA_CFG_BYTE_COUNT_MODE_REMAIN;

	dev_dbg(chan2dev(chan),
		"src burst %d, dst burst %d, src buswidth %d, dst buswidth %d",
		sconfig->src_maxburst, sconfig->dst_maxburst,
		sconfig->src_addr_width, sconfig->dst_addr_width);

	/* Source burst */
	ret = convert_burst(sconfig->src_maxburst);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_SRC_BURST_LENGTH(ret);

	/* Destination burst */
	ret = convert_burst(sconfig->dst_maxburst);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_DST_BURST_LENGTH(ret);

	/* Source bus width */
	ret = convert_buswidth(sconfig->src_addr_width);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_SRC_DATA_WIDTH(ret);

	/* Destination bus width */
	ret = convert_buswidth(sconfig->dst_addr_width);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_DST_DATA_WIDTH(ret);

	return promise;

fail:
	kfree(promise);
	return NULL;
}

/**
 * Generate a promise, to be used in a dedicated DMA contract.
 *
 * A DDMA promise contains all the information required to program the
 * Dedicated part of the DMA Engine and get data copied. A non-executed
 * promise will live in the demands list on a contract. Once it has been
 * completed, it will be moved to the completed demands list for later freeing.
 * All linked promises will be freed when the corresponding contract is freed
 */
static struct sun4i_dma_promise *
generate_ddma_promise(struct dma_chan *chan, dma_addr_t src, dma_addr_t dest,
		      size_t len, struct dma_slave_config *sconfig)
{
	struct sun4i_dma_promise *promise;
	int ret;

	promise = kzalloc(sizeof(*promise), GFP_NOWAIT);
	if (!promise)
		return NULL;

	promise->src = src;
	promise->dst = dest;
	promise->len = len;
	promise->cfg = SUN4I_DMA_CFG_LOADING |
		SUN4I_DDMA_CFG_BYTE_COUNT_MODE_REMAIN;

	/* Source burst */
	ret = convert_burst(sconfig->src_maxburst);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_SRC_BURST_LENGTH(ret);

	/* Destination burst */
	ret = convert_burst(sconfig->dst_maxburst);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_DST_BURST_LENGTH(ret);

	/* Source bus width */
	ret = convert_buswidth(sconfig->src_addr_width);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_SRC_DATA_WIDTH(ret);

	/* Destination bus width */
	ret = convert_buswidth(sconfig->dst_addr_width);
	if (ret < 0)
		goto fail;
	promise->cfg |= SUN4I_DMA_CFG_DST_DATA_WIDTH(ret);

	return promise;

fail:
	kfree(promise);
	return NULL;
}

/**
 * Generate a contract
 *
 * Contracts function as DMA descriptors. As our hardware does not support
 * linked lists, we need to implement SG via software. We use a contract
 * to hold all the pieces of the request and process them serially one
 * after another. Each piece is represented as a promise.
 */
static struct sun4i_dma_contract *generate_dma_contract(void)
{
	struct sun4i_dma_contract *contract;

	contract = kzalloc(sizeof(*contract), GFP_NOWAIT);
	if (!contract)
		return NULL;

	INIT_LIST_HEAD(&contract->demands);
	INIT_LIST_HEAD(&contract->completed_demands);

	return contract;
}

/**
 * Get next promise on a cyclic transfer
 *
 * Cyclic contracts contain a series of promises which are executed on a
 * loop. This function returns the next promise from a cyclic contract,
 * so it can be programmed into the hardware.
 */
static struct sun4i_dma_promise *
get_next_cyclic_promise(struct sun4i_dma_contract *contract)
{
	struct sun4i_dma_promise *promise;

	promise = list_first_entry_or_null(&contract->demands,
					   struct sun4i_dma_promise, list);
	if (!promise) {
		list_splice_init(&contract->completed_demands,
				 &contract->demands);
		promise = list_first_entry(&contract->demands,
					   struct sun4i_dma_promise, list);
	}

	return promise;
}

/**
 * Free a contract and all its associated promises
 */
static void sun4i_dma_free_contract(struct virt_dma_desc *vd)
{
	struct sun4i_dma_contract *contract = to_sun4i_dma_contract(vd);
	struct sun4i_dma_promise *promise, *tmp;

	/* Free all the demands and completed demands */
	list_for_each_entry_safe(promise, tmp, &contract->demands, list)
		kfree(promise);

	list_for_each_entry_safe(promise, tmp, &contract->completed_demands, list)
		kfree(promise);

	kfree(contract);
}

static struct dma_async_tx_descriptor *
sun4i_dma_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest,
			  dma_addr_t src, size_t len, unsigned long flags)
{
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct sun4i_dma_promise *promise;
	struct sun4i_dma_contract *contract;

	contract = generate_dma_contract();
	if (!contract)
		return NULL;

	/*
	 * We can only do the copy to bus aligned addresses, so
	 * choose the best one so we get decent performance. We also
	 * maximize the burst size for this same reason.
	 */
	sconfig->src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sconfig->dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	sconfig->src_maxburst = 8;
	sconfig->dst_maxburst = 8;

	if (vchan->is_dedicated)
		promise = generate_ddma_promise(chan, src, dest, len, sconfig);
	else
		promise = generate_ndma_promise(chan, src, dest, len, sconfig,
						DMA_MEM_TO_MEM);

	if (!promise) {
		kfree(contract);
		return NULL;
	}

	/* Configure memcpy mode */
	if (vchan->is_dedicated) {
		promise->cfg |= SUN4I_DMA_CFG_SRC_DRQ_TYPE(SUN4I_DDMA_DRQ_TYPE_SDRAM) |
				SUN4I_DMA_CFG_DST_DRQ_TYPE(SUN4I_DDMA_DRQ_TYPE_SDRAM);
	} else {
		promise->cfg |= SUN4I_DMA_CFG_SRC_DRQ_TYPE(SUN4I_NDMA_DRQ_TYPE_SDRAM) |
				SUN4I_DMA_CFG_DST_DRQ_TYPE(SUN4I_NDMA_DRQ_TYPE_SDRAM);
	}

	/* Fill the contract with our only promise */
	list_add_tail(&promise->list, &contract->demands);

	/* And add it to the vchan */
	return vchan_tx_prep(&vchan->vc, &contract->vd, flags);
}

static struct dma_async_tx_descriptor *
sun4i_dma_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf, size_t len,
			  size_t period_len, enum dma_transfer_direction dir,
			  unsigned long flags)
{
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct sun4i_dma_promise *promise;
	struct sun4i_dma_contract *contract;
	dma_addr_t src, dest;
	u32 endpoints;
	int nr_periods, offset, plength, i;

	if (!is_slave_direction(dir)) {
		dev_err(chan2dev(chan), "Invalid DMA direction\n");
		return NULL;
	}

	if (vchan->is_dedicated) {
		/*
		 * As we are using this just for audio data, we need to use
		 * normal DMA. There is nothing stopping us from supporting
		 * dedicated DMA here as well, so if a client comes up and
		 * requires it, it will be simple to implement it.
		 */
		dev_err(chan2dev(chan),
			"Cyclic transfers are only supported on Normal DMA\n");
		return NULL;
	}

	contract = generate_dma_contract();
	if (!contract)
		return NULL;

	contract->is_cyclic = 1;

	/* Figure out the endpoints and the address we need */
	if (dir == DMA_MEM_TO_DEV) {
		src = buf;
		dest = sconfig->dst_addr;
		endpoints = SUN4I_DMA_CFG_SRC_DRQ_TYPE(SUN4I_NDMA_DRQ_TYPE_SDRAM) |
			    SUN4I_DMA_CFG_DST_DRQ_TYPE(vchan->endpoint) |
			    SUN4I_DMA_CFG_DST_ADDR_MODE(SUN4I_NDMA_ADDR_MODE_IO);
	} else {
		src = sconfig->src_addr;
		dest = buf;
		endpoints = SUN4I_DMA_CFG_SRC_DRQ_TYPE(vchan->endpoint) |
			    SUN4I_DMA_CFG_SRC_ADDR_MODE(SUN4I_NDMA_ADDR_MODE_IO) |
			    SUN4I_DMA_CFG_DST_DRQ_TYPE(SUN4I_NDMA_DRQ_TYPE_SDRAM);
	}

	/*
	 * We will be using half done interrupts to make two periods
	 * out of a promise, so we need to program the DMA engine less
	 * often
	 */

	/*
	 * The engine can interrupt on half-transfer, so we can use
	 * this feature to program the engine half as often as if we
	 * didn't use it (keep in mind the hardware doesn't support
	 * linked lists).
	 *
	 * Say you have a set of periods (| marks the start/end, I for
	 * interrupt, P for programming the engine to do a new
	 * transfer), the easy but slow way would be to do
	 *
	 *  |---|---|---|---| (periods / promises)
	 *  P  I,P I,P I,P  I
	 *
	 * Using half transfer interrupts you can do
	 *
	 *  |-------|-------| (promises as configured on hw)
	 *  |---|---|---|---| (periods)
	 *  P   I  I,P  I   I
	 *
	 * Which requires half the engine programming for the same
	 * functionality.
	 */
	nr_periods = DIV_ROUND_UP(len / period_len, 2);
	for (i = 0; i < nr_periods; i++) {
		/* Calculate the offset in the buffer and the length needed */
		offset = i * period_len * 2;
		plength = min((len - offset), (period_len * 2));
		if (dir == DMA_MEM_TO_DEV)
			src = buf + offset;
		else
			dest = buf + offset;

		/* Make the promise */
		promise = generate_ndma_promise(chan, src, dest,
						plength, sconfig, dir);
		if (!promise) {
			/* TODO: should we free everything? */
			return NULL;
		}
		promise->cfg |= endpoints;

		/* Then add it to the contract */
		list_add_tail(&promise->list, &contract->demands);
	}

	/* And add it to the vchan */
	return vchan_tx_prep(&vchan->vc, &contract->vd, flags);
}

static struct dma_async_tx_descriptor *
sun4i_dma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
			unsigned int sg_len, enum dma_transfer_direction dir,
			unsigned long flags, void *context)
{
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);
	struct dma_slave_config *sconfig = &vchan->cfg;
	struct sun4i_dma_promise *promise;
	struct sun4i_dma_contract *contract;
	u8 ram_type, io_mode, linear_mode;
	struct scatterlist *sg;
	dma_addr_t srcaddr, dstaddr;
	u32 endpoints, para;
	int i;

	if (!sgl)
		return NULL;

	if (!is_slave_direction(dir)) {
		dev_err(chan2dev(chan), "Invalid DMA direction\n");
		return NULL;
	}

	contract = generate_dma_contract();
	if (!contract)
		return NULL;

	if (vchan->is_dedicated) {
		io_mode = SUN4I_DDMA_ADDR_MODE_IO;
		linear_mode = SUN4I_DDMA_ADDR_MODE_LINEAR;
		ram_type = SUN4I_DDMA_DRQ_TYPE_SDRAM;
	} else {
		io_mode = SUN4I_NDMA_ADDR_MODE_IO;
		linear_mode = SUN4I_NDMA_ADDR_MODE_LINEAR;
		ram_type = SUN4I_NDMA_DRQ_TYPE_SDRAM;
	}

	if (dir == DMA_MEM_TO_DEV)
		endpoints = SUN4I_DMA_CFG_DST_DRQ_TYPE(vchan->endpoint) |
			    SUN4I_DMA_CFG_DST_ADDR_MODE(io_mode) |
			    SUN4I_DMA_CFG_SRC_DRQ_TYPE(ram_type) |
			    SUN4I_DMA_CFG_SRC_ADDR_MODE(linear_mode);
	else
		endpoints = SUN4I_DMA_CFG_DST_DRQ_TYPE(ram_type) |
			    SUN4I_DMA_CFG_DST_ADDR_MODE(linear_mode) |
			    SUN4I_DMA_CFG_SRC_DRQ_TYPE(vchan->endpoint) |
			    SUN4I_DMA_CFG_SRC_ADDR_MODE(io_mode);

	for_each_sg(sgl, sg, sg_len, i) {
		/* Figure out addresses */
		if (dir == DMA_MEM_TO_DEV) {
			srcaddr = sg_dma_address(sg);
			dstaddr = sconfig->dst_addr;
		} else {
			srcaddr = sconfig->src_addr;
			dstaddr = sg_dma_address(sg);
		}

		/*
		 * These are the magic DMA engine timings that keep SPI going.
		 * I haven't seen any interface on DMAEngine to configure
		 * timings, and so far they seem to work for everything we
		 * support, so I've kept them here. I don't know if other
		 * devices need different timings because, as usual, we only
		 * have the "para" bitfield meanings, but no comment on what
		 * the values should be when doing a certain operation :|
		 */
		para = SUN4I_DDMA_MAGIC_SPI_PARAMETERS;

		/* And make a suitable promise */
		if (vchan->is_dedicated)
			promise = generate_ddma_promise(chan, srcaddr, dstaddr,
							sg_dma_len(sg),
							sconfig);
		else
			promise = generate_ndma_promise(chan, srcaddr, dstaddr,
							sg_dma_len(sg),
							sconfig, dir);

		if (!promise)
			return NULL; /* TODO: should we free everything? */

		promise->cfg |= endpoints;
		promise->para = para;

		/* Then add it to the contract */
		list_add_tail(&promise->list, &contract->demands);
	}

	/*
	 * Once we've got all the promises ready, add the contract
	 * to the pending list on the vchan
	 */
	return vchan_tx_prep(&vchan->vc, &contract->vd, flags);
}

static int sun4i_dma_terminate_all(struct dma_chan *chan)
{
	struct sun4i_dma_dev *priv = to_sun4i_dma_dev(chan->device);
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);
	struct sun4i_dma_pchan *pchan = vchan->pchan;
	LIST_HEAD(head);
	unsigned long flags;

	spin_lock_irqsave(&vchan->vc.lock, flags);
	vchan_get_all_descriptors(&vchan->vc, &head);
	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	/*
	 * Clearing the configuration register will halt the pchan. Interrupts
	 * may still trigger, so don't forget to disable them.
	 */
	if (pchan) {
		if (pchan->is_dedicated)
			writel(0, pchan->base + SUN4I_DDMA_CFG_REG);
		else
			writel(0, pchan->base + SUN4I_NDMA_CFG_REG);
		set_pchan_interrupt(priv, pchan, 0, 0);
		release_pchan(priv, pchan);
	}

	spin_lock_irqsave(&vchan->vc.lock, flags);
	vchan_dma_desc_free_list(&vchan->vc, &head);
	/* Clear these so the vchan is usable again */
	vchan->processing = NULL;
	vchan->pchan = NULL;
	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return 0;
}

static int sun4i_dma_config(struct dma_chan *chan,
			    struct dma_slave_config *config)
{
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);

	memcpy(&vchan->cfg, config, sizeof(*config));

	return 0;
}

static struct dma_chan *sun4i_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct sun4i_dma_dev *priv = ofdma->of_dma_data;
	struct sun4i_dma_vchan *vchan;
	struct dma_chan *chan;
	u8 is_dedicated = dma_spec->args[0];
	u8 endpoint = dma_spec->args[1];

	/* Check if type is Normal or Dedicated */
	if (is_dedicated != 0 && is_dedicated != 1)
		return NULL;

	/* Make sure the endpoint looks sane */
	if ((is_dedicated && endpoint >= SUN4I_DDMA_DRQ_TYPE_LIMIT) ||
	    (!is_dedicated && endpoint >= SUN4I_NDMA_DRQ_TYPE_LIMIT))
		return NULL;

	chan = dma_get_any_slave_channel(&priv->slave);
	if (!chan)
		return NULL;

	/* Assign the endpoint to the vchan */
	vchan = to_sun4i_dma_vchan(chan);
	vchan->is_dedicated = is_dedicated;
	vchan->endpoint = endpoint;

	return chan;
}

static enum dma_status sun4i_dma_tx_status(struct dma_chan *chan,
					   dma_cookie_t cookie,
					   struct dma_tx_state *state)
{
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);
	struct sun4i_dma_pchan *pchan = vchan->pchan;
	struct sun4i_dma_contract *contract;
	struct sun4i_dma_promise *promise;
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	size_t bytes = 0;

	ret = dma_cookie_status(chan, cookie, state);
	if (!state || (ret == DMA_COMPLETE))
		return ret;

	spin_lock_irqsave(&vchan->vc.lock, flags);
	vd = vchan_find_desc(&vchan->vc, cookie);
	if (!vd)
		goto exit;
	contract = to_sun4i_dma_contract(vd);

	list_for_each_entry(promise, &contract->demands, list)
		bytes += promise->len;

	/*
	 * The hardware is configured to return the remaining byte
	 * quantity. If possible, replace the first listed element's
	 * full size with the actual remaining amount
	 */
	promise = list_first_entry_or_null(&contract->demands,
					   struct sun4i_dma_promise, list);
	if (promise && pchan) {
		bytes -= promise->len;
		if (pchan->is_dedicated)
			bytes += readl(pchan->base + SUN4I_DDMA_BYTE_COUNT_REG);
		else
			bytes += readl(pchan->base + SUN4I_NDMA_BYTE_COUNT_REG);
	}

exit:

	dma_set_residue(state, bytes);
	spin_unlock_irqrestore(&vchan->vc.lock, flags);

	return ret;
}

static void sun4i_dma_issue_pending(struct dma_chan *chan)
{
	struct sun4i_dma_dev *priv = to_sun4i_dma_dev(chan->device);
	struct sun4i_dma_vchan *vchan = to_sun4i_dma_vchan(chan);
	unsigned long flags;

	spin_lock_irqsave(&vchan->vc.lock, flags);

	/*
	 * If there are pending transactions for this vchan, push one of
	 * them into the engine to get the ball rolling.
	 */
	if (vchan_issue_pending(&vchan->vc))
		__execute_vchan_pending(priv, vchan);

	spin_unlock_irqrestore(&vchan->vc.lock, flags);
}

static irqreturn_t sun4i_dma_interrupt(int irq, void *dev_id)
{
	struct sun4i_dma_dev *priv = dev_id;
	struct sun4i_dma_pchan *pchans = priv->pchans, *pchan;
	struct sun4i_dma_vchan *vchan;
	struct sun4i_dma_contract *contract;
	struct sun4i_dma_promise *promise;
	unsigned long pendirq, irqs, disableirqs;
	int bit, i, free_room, allow_mitigation = 1;

	pendirq = readl_relaxed(priv->base + SUN4I_DMA_IRQ_PENDING_STATUS_REG);

handle_pending:

	disableirqs = 0;
	free_room = 0;

	for_each_set_bit(bit, &pendirq, 32) {
		pchan = &pchans[bit >> 1];
		vchan = pchan->vchan;
		if (!vchan) /* a terminated channel may still interrupt */
			continue;
		contract = vchan->contract;

		/*
		 * Disable the IRQ and free the pchan if it's an end
		 * interrupt (odd bit)
		 */
		if (bit & 1) {
			spin_lock(&vchan->vc.lock);

			/*
			 * Move the promise into the completed list now that
			 * we're done with it
			 */
			list_del(&vchan->processing->list);
			list_add_tail(&vchan->processing->list,
				      &contract->completed_demands);

			/*
			 * Cyclic DMA transfers are special:
			 * - There's always something we can dispatch
			 * - We need to run the callback
			 * - Latency is very important, as this is used by audio
			 * We therefore just cycle through the list and dispatch
			 * whatever we have here, reusing the pchan. There's
			 * no need to run the thread after this.
			 *
			 * For non-cyclic transfers we need to look around,
			 * so we can program some more work, or notify the
			 * client that their transfers have been completed.
			 */
			if (contract->is_cyclic) {
				promise = get_next_cyclic_promise(contract);
				vchan->processing = promise;
				configure_pchan(pchan, promise);
				vchan_cyclic_callback(&contract->vd);
			} else {
				vchan->processing = NULL;
				vchan->pchan = NULL;

				free_room = 1;
				disableirqs |= BIT(bit);
				release_pchan(priv, pchan);
			}

			spin_unlock(&vchan->vc.lock);
		} else {
			/* Half done interrupt */
			if (contract->is_cyclic)
				vchan_cyclic_callback(&contract->vd);
			else
				disableirqs |= BIT(bit);
		}
	}

	/* Disable the IRQs for events we handled */
	spin_lock(&priv->lock);
	irqs = readl_relaxed(priv->base + SUN4I_DMA_IRQ_ENABLE_REG);
	writel_relaxed(irqs & ~disableirqs,
		       priv->base + SUN4I_DMA_IRQ_ENABLE_REG);
	spin_unlock(&priv->lock);

	/* Writing 1 to the pending field will clear the pending interrupt */
	writel_relaxed(pendirq, priv->base + SUN4I_DMA_IRQ_PENDING_STATUS_REG);

	/*
	 * If a pchan was freed, we may be able to schedule something else,
	 * so have a look around
	 */
	if (free_room) {
		for (i = 0; i < SUN4I_DMA_NR_MAX_VCHANS; i++) {
			vchan = &priv->vchans[i];
			spin_lock(&vchan->vc.lock);
			__execute_vchan_pending(priv, vchan);
			spin_unlock(&vchan->vc.lock);
		}
	}

	/*
	 * Handle newer interrupts if some showed up, but only do it once
	 * to avoid a too long a loop
	 */
	if (allow_mitigation) {
		pendirq = readl_relaxed(priv->base +
					SUN4I_DMA_IRQ_PENDING_STATUS_REG);
		if (pendirq) {
			allow_mitigation = 0;
			goto handle_pending;
		}
	}

	return IRQ_HANDLED;
}

static int sun4i_dma_probe(struct platform_device *pdev)
{
	struct sun4i_dma_dev *priv;
	struct resource *res;
	int i, j, ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(&pdev->dev, "Cannot claim IRQ\n");
		return priv->irq;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "No clock specified\n");
		return PTR_ERR(priv->clk);
	}

	platform_set_drvdata(pdev, priv);
	spin_lock_init(&priv->lock);

	dma_cap_zero(priv->slave.cap_mask);
	dma_cap_set(DMA_PRIVATE, priv->slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, priv->slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, priv->slave.cap_mask);
	dma_cap_set(DMA_SLAVE, priv->slave.cap_mask);

	INIT_LIST_HEAD(&priv->slave.channels);
	priv->slave.device_free_chan_resources	= sun4i_dma_free_chan_resources;
	priv->slave.device_tx_status		= sun4i_dma_tx_status;
	priv->slave.device_issue_pending	= sun4i_dma_issue_pending;
	priv->slave.device_prep_slave_sg	= sun4i_dma_prep_slave_sg;
	priv->slave.device_prep_dma_memcpy	= sun4i_dma_prep_dma_memcpy;
	priv->slave.device_prep_dma_cyclic	= sun4i_dma_prep_dma_cyclic;
	priv->slave.device_config		= sun4i_dma_config;
	priv->slave.device_terminate_all	= sun4i_dma_terminate_all;
	priv->slave.copy_align			= 2;
	priv->slave.src_addr_widths		= BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
						  BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
						  BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	priv->slave.dst_addr_widths		= BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
						  BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
						  BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	priv->slave.directions			= BIT(DMA_DEV_TO_MEM) |
						  BIT(DMA_MEM_TO_DEV);
	priv->slave.residue_granularity		= DMA_RESIDUE_GRANULARITY_BURST;

	priv->slave.dev = &pdev->dev;

	priv->pchans = devm_kcalloc(&pdev->dev, SUN4I_DMA_NR_MAX_CHANNELS,
				    sizeof(struct sun4i_dma_pchan), GFP_KERNEL);
	priv->vchans = devm_kcalloc(&pdev->dev, SUN4I_DMA_NR_MAX_VCHANS,
				    sizeof(struct sun4i_dma_vchan), GFP_KERNEL);
	if (!priv->vchans || !priv->pchans)
		return -ENOMEM;

	/*
	 * [0..SUN4I_NDMA_NR_MAX_CHANNELS) are normal pchans, and
	 * [SUN4I_NDMA_NR_MAX_CHANNELS..SUN4I_DMA_NR_MAX_CHANNELS) are
	 * dedicated ones
	 */
	for (i = 0; i < SUN4I_NDMA_NR_MAX_CHANNELS; i++)
		priv->pchans[i].base = priv->base +
			SUN4I_NDMA_CHANNEL_REG_BASE(i);

	for (j = 0; i < SUN4I_DMA_NR_MAX_CHANNELS; i++, j++) {
		priv->pchans[i].base = priv->base +
			SUN4I_DDMA_CHANNEL_REG_BASE(j);
		priv->pchans[i].is_dedicated = 1;
	}

	for (i = 0; i < SUN4I_DMA_NR_MAX_VCHANS; i++) {
		struct sun4i_dma_vchan *vchan = &priv->vchans[i];

		spin_lock_init(&vchan->vc.lock);
		vchan->vc.desc_free = sun4i_dma_free_contract;
		vchan_init(&vchan->vc, &priv->slave);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't enable the clock\n");
		return ret;
	}

	/*
	 * Make sure the IRQs are all disabled and accounted for. The bootloader
	 * likes to leave these dirty
	 */
	writel(0, priv->base + SUN4I_DMA_IRQ_ENABLE_REG);
	writel(0xFFFFFFFF, priv->base + SUN4I_DMA_IRQ_PENDING_STATUS_REG);

	ret = devm_request_irq(&pdev->dev, priv->irq, sun4i_dma_interrupt,
			       0, dev_name(&pdev->dev), priv);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request IRQ\n");
		goto err_clk_disable;
	}

	ret = dma_async_device_register(&priv->slave);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to register DMA engine device\n");
		goto err_clk_disable;
	}

	ret = of_dma_controller_register(pdev->dev.of_node, sun4i_dma_of_xlate,
					 priv);
	if (ret) {
		dev_err(&pdev->dev, "of_dma_controller_register failed\n");
		goto err_dma_unregister;
	}

	dev_dbg(&pdev->dev, "Successfully probed SUN4I_DMA\n");

	return 0;

err_dma_unregister:
	dma_async_device_unregister(&priv->slave);
err_clk_disable:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int sun4i_dma_remove(struct platform_device *pdev)
{
	struct sun4i_dma_dev *priv = platform_get_drvdata(pdev);

	/* Disable IRQ so no more work is scheduled */
	disable_irq(priv->irq);

	of_dma_controller_free(pdev->dev.of_node);
	dma_async_device_unregister(&priv->slave);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id sun4i_dma_match[] = {
	{ .compatible = "allwinner,sun4i-a10-dma" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sun4i_dma_match);

static struct platform_driver sun4i_dma_driver = {
	.probe	= sun4i_dma_probe,
	.remove	= sun4i_dma_remove,
	.driver	= {
		.name		= "sun4i-dma",
		.of_match_table	= sun4i_dma_match,
	},
};

module_platform_driver(sun4i_dma_driver);

MODULE_DESCRIPTION("Allwinner A10 Dedicated DMA Controller Driver");
MODULE_AUTHOR("Emilio López <emilio@elopez.com.ar>");
MODULE_LICENSE("GPL");
