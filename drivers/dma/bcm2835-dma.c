/*
 * BCM2835 DMA engine support
 *
 * This driver only supports cyclic DMA transfers
 * as needed for the I2S module.
 *
 * Author:      Florian Meier <florian.meier@koalo.de>
 *              Copyright 2013
 *
 * Based on
 *	OMAP DMAengine support by Russell King
 *
 *	BCM2708 DMA Driver
 *	Copyright (C) 2010 Broadcom
 *
 *	Raspberry Pi PCM I2S ALSA Driver
 *	Copyright (c) by Phil Poole 2013
 *
 *	MARVELL MMP Peripheral DMA Driver
 *	Copyright 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_dma.h>

#include "virt-dma.h"

struct bcm2835_dmadev {
	struct dma_device ddev;
	spinlock_t lock;
	void __iomem *base;
	struct device_dma_parameters dma_parms;
};

struct bcm2835_dma_cb {
	uint32_t info;
	uint32_t src;
	uint32_t dst;
	uint32_t length;
	uint32_t stride;
	uint32_t next;
	uint32_t pad[2];
};

struct bcm2835_cb_entry {
	struct bcm2835_dma_cb *cb;
	dma_addr_t paddr;
};

struct bcm2835_chan {
	struct virt_dma_chan vc;
	struct list_head node;

	struct dma_slave_config	cfg;
	bool cyclic;
	unsigned int dreq;

	int ch;
	struct bcm2835_desc *desc;
	struct dma_pool *cb_pool;

	void __iomem *chan_base;
	int irq_number;
};

struct bcm2835_desc {
	struct bcm2835_chan *c;
	struct virt_dma_desc vd;
	enum dma_transfer_direction dir;

	struct bcm2835_cb_entry *cb_list;

	unsigned int frames;
	size_t size;
};

#define BCM2835_DMA_CS		0x00
#define BCM2835_DMA_ADDR	0x04
#define BCM2835_DMA_TI		0x08
#define BCM2835_DMA_SOURCE_AD	0x0c
#define BCM2835_DMA_DEST_AD	0x10
#define BCM2835_DMA_LEN		0x14
#define BCM2835_DMA_STRIDE	0x18
#define BCM2835_DMA_NEXTCB	0x1c
#define BCM2835_DMA_DEBUG	0x20

/* DMA CS Control and Status bits */
#define BCM2835_DMA_ACTIVE	BIT(0)  /* activate the DMA */
#define BCM2835_DMA_END		BIT(1)  /* current CB has ended */
#define BCM2835_DMA_INT		BIT(2)  /* interrupt status */
#define BCM2835_DMA_DREQ	BIT(3)  /* DREQ state */
#define BCM2835_DMA_ISPAUSED	BIT(4)  /* Pause requested or not active */
#define BCM2835_DMA_ISHELD	BIT(5)  /* Is held by DREQ flow control */
#define BCM2835_DMA_WAITING_FOR_WRITES BIT(6) /* waiting for last
					       * AXI-write to ack
					       */
#define BCM2835_DMA_ERR		BIT(8)
#define BCM2835_DMA_PRIORITY(x) ((x & 15) << 16) /* AXI priority */
#define BCM2835_DMA_PANIC_PRIORITY(x) ((x & 15) << 20) /* panic priority */
/* current value of TI.BCM2835_DMA_WAIT_RESP */
#define BCM2835_DMA_WAIT_FOR_WRITES BIT(28)
#define BCM2835_DMA_DIS_DEBUG	BIT(29) /* disable debug pause signal */
#define BCM2835_DMA_ABORT	BIT(30) /* Stop current CB, go to next, WO */
#define BCM2835_DMA_RESET	BIT(31) /* WO, self clearing */

/* Transfer information bits - also bcm2835_cb.info field */
#define BCM2835_DMA_INT_EN	BIT(0)
#define BCM2835_DMA_TDMODE	BIT(1) /* 2D-Mode */
#define BCM2835_DMA_WAIT_RESP	BIT(3) /* wait for AXI-write to be acked */
#define BCM2835_DMA_D_INC	BIT(4)
#define BCM2835_DMA_D_WIDTH	BIT(5) /* 128bit writes if set */
#define BCM2835_DMA_D_DREQ	BIT(6) /* enable DREQ for destination */
#define BCM2835_DMA_D_IGNORE	BIT(7) /* ignore destination writes */
#define BCM2835_DMA_S_INC	BIT(8)
#define BCM2835_DMA_S_WIDTH	BIT(9) /* 128bit writes if set */
#define BCM2835_DMA_S_DREQ	BIT(10) /* enable SREQ for source */
#define BCM2835_DMA_S_IGNORE	BIT(11) /* ignore source reads - read 0 */
#define BCM2835_DMA_BURST_LENGTH(x) ((x & 15) << 12)
#define BCM2835_DMA_PER_MAP(x)	((x & 31) << 16) /* REQ source */
#define BCM2835_DMA_WAIT(x)	((x & 31) << 21) /* add DMA-wait cycles */
#define BCM2835_DMA_NO_WIDE_BURSTS BIT(26) /* no 2 beat write bursts */

/* debug register bits */
#define BCM2835_DMA_DEBUG_LAST_NOT_SET_ERR	BIT(0)
#define BCM2835_DMA_DEBUG_FIFO_ERR		BIT(1)
#define BCM2835_DMA_DEBUG_READ_ERR		BIT(2)
#define BCM2835_DMA_DEBUG_OUTSTANDING_WRITES_SHIFT 4
#define BCM2835_DMA_DEBUG_OUTSTANDING_WRITES_BITS 4
#define BCM2835_DMA_DEBUG_ID_SHIFT		16
#define BCM2835_DMA_DEBUG_ID_BITS		9
#define BCM2835_DMA_DEBUG_STATE_SHIFT		16
#define BCM2835_DMA_DEBUG_STATE_BITS		9
#define BCM2835_DMA_DEBUG_VERSION_SHIFT		25
#define BCM2835_DMA_DEBUG_VERSION_BITS		3
#define BCM2835_DMA_DEBUG_LITE			BIT(28)

/* shared registers for all dma channels */
#define BCM2835_DMA_INT_STATUS         0xfe0
#define BCM2835_DMA_ENABLE             0xff0

#define BCM2835_DMA_DATA_TYPE_S8	1
#define BCM2835_DMA_DATA_TYPE_S16	2
#define BCM2835_DMA_DATA_TYPE_S32	4
#define BCM2835_DMA_DATA_TYPE_S128	16

/* Valid only for channels 0 - 14, 15 has its own base address */
#define BCM2835_DMA_CHAN(n)	((n) << 8) /* Base address */
#define BCM2835_DMA_CHANIO(base, n) ((base) + BCM2835_DMA_CHAN(n))

static inline struct bcm2835_dmadev *to_bcm2835_dma_dev(struct dma_device *d)
{
	return container_of(d, struct bcm2835_dmadev, ddev);
}

static inline struct bcm2835_chan *to_bcm2835_dma_chan(struct dma_chan *c)
{
	return container_of(c, struct bcm2835_chan, vc.chan);
}

static inline struct bcm2835_desc *to_bcm2835_dma_desc(
		struct dma_async_tx_descriptor *t)
{
	return container_of(t, struct bcm2835_desc, vd.tx);
}

static void bcm2835_dma_desc_free(struct virt_dma_desc *vd)
{
	struct bcm2835_desc *desc = container_of(vd, struct bcm2835_desc, vd);
	int i;

	for (i = 0; i < desc->frames; i++)
		dma_pool_free(desc->c->cb_pool, desc->cb_list[i].cb,
			      desc->cb_list[i].paddr);

	kfree(desc->cb_list);
	kfree(desc);
}

static int bcm2835_dma_abort(void __iomem *chan_base)
{
	unsigned long cs;
	long int timeout = 10000;

	cs = readl(chan_base + BCM2835_DMA_CS);
	if (!(cs & BCM2835_DMA_ACTIVE))
		return 0;

	/* Write 0 to the active bit - Pause the DMA */
	writel(0, chan_base + BCM2835_DMA_CS);

	/* Wait for any current AXI transfer to complete */
	while ((cs & BCM2835_DMA_ISPAUSED) && --timeout) {
		cpu_relax();
		cs = readl(chan_base + BCM2835_DMA_CS);
	}

	/* We'll un-pause when we set of our next DMA */
	if (!timeout)
		return -ETIMEDOUT;

	if (!(cs & BCM2835_DMA_ACTIVE))
		return 0;

	/* Terminate the control block chain */
	writel(0, chan_base + BCM2835_DMA_NEXTCB);

	/* Abort the whole DMA */
	writel(BCM2835_DMA_ABORT | BCM2835_DMA_ACTIVE,
	       chan_base + BCM2835_DMA_CS);

	return 0;
}

static void bcm2835_dma_start_desc(struct bcm2835_chan *c)
{
	struct virt_dma_desc *vd = vchan_next_desc(&c->vc);
	struct bcm2835_desc *d;

	if (!vd) {
		c->desc = NULL;
		return;
	}

	list_del(&vd->node);

	c->desc = d = to_bcm2835_dma_desc(&vd->tx);

	writel(d->cb_list[0].paddr, c->chan_base + BCM2835_DMA_ADDR);
	writel(BCM2835_DMA_ACTIVE, c->chan_base + BCM2835_DMA_CS);
}

static irqreturn_t bcm2835_dma_callback(int irq, void *data)
{
	struct bcm2835_chan *c = data;
	struct bcm2835_desc *d;
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);

	/* Acknowledge interrupt */
	writel(BCM2835_DMA_INT, c->chan_base + BCM2835_DMA_CS);

	d = c->desc;

	if (d) {
		/* TODO Only works for cyclic DMA */
		vchan_cyclic_callback(&d->vd);
	}

	/* Keep the DMA engine running */
	writel(BCM2835_DMA_ACTIVE, c->chan_base + BCM2835_DMA_CS);

	spin_unlock_irqrestore(&c->vc.lock, flags);

	return IRQ_HANDLED;
}

static int bcm2835_dma_alloc_chan_resources(struct dma_chan *chan)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);
	struct device *dev = c->vc.chan.device->dev;

	dev_dbg(dev, "Allocating DMA channel %d\n", c->ch);

	c->cb_pool = dma_pool_create(dev_name(dev), dev,
				     sizeof(struct bcm2835_dma_cb), 0, 0);
	if (!c->cb_pool) {
		dev_err(dev, "unable to allocate descriptor pool\n");
		return -ENOMEM;
	}

	return request_irq(c->irq_number,
			bcm2835_dma_callback, 0, "DMA IRQ", c);
}

static void bcm2835_dma_free_chan_resources(struct dma_chan *chan)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);

	vchan_free_chan_resources(&c->vc);
	free_irq(c->irq_number, c);
	dma_pool_destroy(c->cb_pool);

	dev_dbg(c->vc.chan.device->dev, "Freeing DMA channel %u\n", c->ch);
}

static size_t bcm2835_dma_desc_size(struct bcm2835_desc *d)
{
	return d->size;
}

static size_t bcm2835_dma_desc_size_pos(struct bcm2835_desc *d, dma_addr_t addr)
{
	unsigned int i;
	size_t size;

	for (size = i = 0; i < d->frames; i++) {
		struct bcm2835_dma_cb *control_block = d->cb_list[i].cb;
		size_t this_size = control_block->length;
		dma_addr_t dma;

		if (d->dir == DMA_DEV_TO_MEM)
			dma = control_block->dst;
		else
			dma = control_block->src;

		if (size)
			size += this_size;
		else if (addr >= dma && addr < dma + this_size)
			size += dma + this_size - addr;
	}

	return size;
}

static enum dma_status bcm2835_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&c->vc.lock, flags);
	vd = vchan_find_desc(&c->vc, cookie);
	if (vd) {
		txstate->residue =
			bcm2835_dma_desc_size(to_bcm2835_dma_desc(&vd->tx));
	} else if (c->desc && c->desc->vd.tx.cookie == cookie) {
		struct bcm2835_desc *d = c->desc;
		dma_addr_t pos;

		if (d->dir == DMA_MEM_TO_DEV)
			pos = readl(c->chan_base + BCM2835_DMA_SOURCE_AD);
		else if (d->dir == DMA_DEV_TO_MEM)
			pos = readl(c->chan_base + BCM2835_DMA_DEST_AD);
		else
			pos = 0;

		txstate->residue = bcm2835_dma_desc_size_pos(d, pos);
	} else {
		txstate->residue = 0;
	}

	spin_unlock_irqrestore(&c->vc.lock, flags);

	return ret;
}

static void bcm2835_dma_issue_pending(struct dma_chan *chan)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);
	unsigned long flags;

	c->cyclic = true; /* Nothing else is implemented */

	spin_lock_irqsave(&c->vc.lock, flags);
	if (vchan_issue_pending(&c->vc) && !c->desc)
		bcm2835_dma_start_desc(c);

	spin_unlock_irqrestore(&c->vc.lock, flags);
}

static struct dma_async_tx_descriptor *bcm2835_dma_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);
	enum dma_slave_buswidth dev_width;
	struct bcm2835_desc *d;
	dma_addr_t dev_addr;
	unsigned int es, sync_type;
	unsigned int frame;
	int i;

	/* Grab configuration */
	if (!is_slave_direction(direction)) {
		dev_err(chan->device->dev, "%s: bad direction?\n", __func__);
		return NULL;
	}

	if (direction == DMA_DEV_TO_MEM) {
		dev_addr = c->cfg.src_addr;
		dev_width = c->cfg.src_addr_width;
		sync_type = BCM2835_DMA_S_DREQ;
	} else {
		dev_addr = c->cfg.dst_addr;
		dev_width = c->cfg.dst_addr_width;
		sync_type = BCM2835_DMA_D_DREQ;
	}

	/* Bus width translates to the element size (ES) */
	switch (dev_width) {
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		es = BCM2835_DMA_DATA_TYPE_S32;
		break;
	default:
		return NULL;
	}

	/* Now allocate and setup the descriptor. */
	d = kzalloc(sizeof(*d), GFP_NOWAIT);
	if (!d)
		return NULL;

	d->c = c;
	d->dir = direction;
	d->frames = buf_len / period_len;

	d->cb_list = kcalloc(d->frames, sizeof(*d->cb_list), GFP_KERNEL);
	if (!d->cb_list) {
		kfree(d);
		return NULL;
	}
	/* Allocate memory for control blocks */
	for (i = 0; i < d->frames; i++) {
		struct bcm2835_cb_entry *cb_entry = &d->cb_list[i];

		cb_entry->cb = dma_pool_zalloc(c->cb_pool, GFP_ATOMIC,
					       &cb_entry->paddr);
		if (!cb_entry->cb)
			goto error_cb;
	}

	/*
	 * Iterate over all frames, create a control block
	 * for each frame and link them together.
	 */
	for (frame = 0; frame < d->frames; frame++) {
		struct bcm2835_dma_cb *control_block = d->cb_list[frame].cb;

		/* Setup adresses */
		if (d->dir == DMA_DEV_TO_MEM) {
			control_block->info = BCM2835_DMA_D_INC;
			control_block->src = dev_addr;
			control_block->dst = buf_addr + frame * period_len;
		} else {
			control_block->info = BCM2835_DMA_S_INC;
			control_block->src = buf_addr + frame * period_len;
			control_block->dst = dev_addr;
		}

		/* Enable interrupt */
		control_block->info |= BCM2835_DMA_INT_EN;

		/* Setup synchronization */
		if (sync_type != 0)
			control_block->info |= sync_type;

		/* Setup DREQ channel */
		if (c->dreq != 0)
			control_block->info |=
				BCM2835_DMA_PER_MAP(c->dreq);

		/* Length of a frame */
		control_block->length = period_len;
		d->size += control_block->length;

		/*
		 * Next block is the next frame.
		 * This DMA engine driver currently only supports cyclic DMA.
		 * Therefore, wrap around at number of frames.
		 */
		control_block->next = d->cb_list[((frame + 1) % d->frames)].paddr;
	}

	return vchan_tx_prep(&c->vc, &d->vd, flags);
error_cb:
	i--;
	for (; i >= 0; i--) {
		struct bcm2835_cb_entry *cb_entry = &d->cb_list[i];

		dma_pool_free(c->cb_pool, cb_entry->cb, cb_entry->paddr);
	}

	kfree(d->cb_list);
	kfree(d);
	return NULL;
}

static int bcm2835_dma_slave_config(struct dma_chan *chan,
				    struct dma_slave_config *cfg)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);

	if ((cfg->direction == DMA_DEV_TO_MEM &&
	     cfg->src_addr_width != DMA_SLAVE_BUSWIDTH_4_BYTES) ||
	    (cfg->direction == DMA_MEM_TO_DEV &&
	     cfg->dst_addr_width != DMA_SLAVE_BUSWIDTH_4_BYTES) ||
	    !is_slave_direction(cfg->direction)) {
		return -EINVAL;
	}

	c->cfg = *cfg;

	return 0;
}

static int bcm2835_dma_terminate_all(struct dma_chan *chan)
{
	struct bcm2835_chan *c = to_bcm2835_dma_chan(chan);
	struct bcm2835_dmadev *d = to_bcm2835_dma_dev(c->vc.chan.device);
	unsigned long flags;
	int timeout = 10000;
	LIST_HEAD(head);

	spin_lock_irqsave(&c->vc.lock, flags);

	/* Prevent this channel being scheduled */
	spin_lock(&d->lock);
	list_del_init(&c->node);
	spin_unlock(&d->lock);

	/*
	 * Stop DMA activity: we assume the callback will not be called
	 * after bcm_dma_abort() returns (even if it does, it will see
	 * c->desc is NULL and exit.)
	 */
	if (c->desc) {
		bcm2835_dma_desc_free(&c->desc->vd);
		c->desc = NULL;
		bcm2835_dma_abort(c->chan_base);

		/* Wait for stopping */
		while (--timeout) {
			if (!(readl(c->chan_base + BCM2835_DMA_CS) &
						BCM2835_DMA_ACTIVE))
				break;

			cpu_relax();
		}

		if (!timeout)
			dev_err(d->ddev.dev, "DMA transfer could not be terminated\n");
	}

	vchan_get_all_descriptors(&c->vc, &head);
	spin_unlock_irqrestore(&c->vc.lock, flags);
	vchan_dma_desc_free_list(&c->vc, &head);

	return 0;
}

static int bcm2835_dma_chan_init(struct bcm2835_dmadev *d, int chan_id, int irq)
{
	struct bcm2835_chan *c;

	c = devm_kzalloc(d->ddev.dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	c->vc.desc_free = bcm2835_dma_desc_free;
	vchan_init(&c->vc, &d->ddev);
	INIT_LIST_HEAD(&c->node);

	c->chan_base = BCM2835_DMA_CHANIO(d->base, chan_id);
	c->ch = chan_id;
	c->irq_number = irq;

	return 0;
}

static void bcm2835_dma_free(struct bcm2835_dmadev *od)
{
	struct bcm2835_chan *c, *next;

	list_for_each_entry_safe(c, next, &od->ddev.channels,
				 vc.chan.device_node) {
		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
	}
}

static const struct of_device_id bcm2835_dma_of_match[] = {
	{ .compatible = "brcm,bcm2835-dma", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_dma_of_match);

static struct dma_chan *bcm2835_dma_xlate(struct of_phandle_args *spec,
					   struct of_dma *ofdma)
{
	struct bcm2835_dmadev *d = ofdma->of_dma_data;
	struct dma_chan *chan;

	chan = dma_get_any_slave_channel(&d->ddev);
	if (!chan)
		return NULL;

	/* Set DREQ from param */
	to_bcm2835_dma_chan(chan)->dreq = spec->args[0];

	return chan;
}

static int bcm2835_dma_probe(struct platform_device *pdev)
{
	struct bcm2835_dmadev *od;
	struct resource *res;
	void __iomem *base;
	int rc;
	int i;
	int irq;
	uint32_t chans_available;

	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	od = devm_kzalloc(&pdev->dev, sizeof(*od), GFP_KERNEL);
	if (!od)
		return -ENOMEM;

	pdev->dev.dma_parms = &od->dma_parms;
	dma_set_max_seg_size(&pdev->dev, 0x3FFFFFFF);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	od->base = base;

	dma_cap_set(DMA_SLAVE, od->ddev.cap_mask);
	dma_cap_set(DMA_PRIVATE, od->ddev.cap_mask);
	dma_cap_set(DMA_CYCLIC, od->ddev.cap_mask);
	od->ddev.device_alloc_chan_resources = bcm2835_dma_alloc_chan_resources;
	od->ddev.device_free_chan_resources = bcm2835_dma_free_chan_resources;
	od->ddev.device_tx_status = bcm2835_dma_tx_status;
	od->ddev.device_issue_pending = bcm2835_dma_issue_pending;
	od->ddev.device_prep_dma_cyclic = bcm2835_dma_prep_dma_cyclic;
	od->ddev.device_config = bcm2835_dma_slave_config;
	od->ddev.device_terminate_all = bcm2835_dma_terminate_all;
	od->ddev.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	od->ddev.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	od->ddev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	od->ddev.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	od->ddev.dev = &pdev->dev;
	INIT_LIST_HEAD(&od->ddev.channels);
	spin_lock_init(&od->lock);

	platform_set_drvdata(pdev, od);

	/* Request DMA channel mask from device tree */
	if (of_property_read_u32(pdev->dev.of_node,
			"brcm,dma-channel-mask",
			&chans_available)) {
		dev_err(&pdev->dev, "Failed to get channel mask\n");
		rc = -EINVAL;
		goto err_no_dma;
	}

	for (i = 0; i < pdev->num_resources; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			break;

		if (chans_available & (1 << i)) {
			rc = bcm2835_dma_chan_init(od, i, irq);
			if (rc)
				goto err_no_dma;
		}
	}

	dev_dbg(&pdev->dev, "Initialized %i DMA channels\n", i);

	/* Device-tree DMA controller registration */
	rc = of_dma_controller_register(pdev->dev.of_node,
			bcm2835_dma_xlate, od);
	if (rc) {
		dev_err(&pdev->dev, "Failed to register DMA controller\n");
		goto err_no_dma;
	}

	rc = dma_async_device_register(&od->ddev);
	if (rc) {
		dev_err(&pdev->dev,
			"Failed to register slave DMA engine device: %d\n", rc);
		goto err_no_dma;
	}

	dev_dbg(&pdev->dev, "Load BCM2835 DMA engine driver\n");

	return 0;

err_no_dma:
	bcm2835_dma_free(od);
	return rc;
}

static int bcm2835_dma_remove(struct platform_device *pdev)
{
	struct bcm2835_dmadev *od = platform_get_drvdata(pdev);

	dma_async_device_unregister(&od->ddev);
	bcm2835_dma_free(od);

	return 0;
}

static struct platform_driver bcm2835_dma_driver = {
	.probe	= bcm2835_dma_probe,
	.remove	= bcm2835_dma_remove,
	.driver = {
		.name = "bcm2835-dma",
		.of_match_table = of_match_ptr(bcm2835_dma_of_match),
	},
};

module_platform_driver(bcm2835_dma_driver);

MODULE_ALIAS("platform:bcm2835-dma");
MODULE_DESCRIPTION("BCM2835 DMA engine driver");
MODULE_AUTHOR("Florian Meier <florian.meier@koalo.de>");
MODULE_LICENSE("GPL v2");
