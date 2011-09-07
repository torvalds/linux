/*
 * Texas Instruments CPDMA Driver
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include "davinci_cpdma.h"

/* DMA Registers */
#define CPDMA_TXIDVER		0x00
#define CPDMA_TXCONTROL		0x04
#define CPDMA_TXTEARDOWN	0x08
#define CPDMA_RXIDVER		0x10
#define CPDMA_RXCONTROL		0x14
#define CPDMA_SOFTRESET		0x1c
#define CPDMA_RXTEARDOWN	0x18
#define CPDMA_TXINTSTATRAW	0x80
#define CPDMA_TXINTSTATMASKED	0x84
#define CPDMA_TXINTMASKSET	0x88
#define CPDMA_TXINTMASKCLEAR	0x8c
#define CPDMA_MACINVECTOR	0x90
#define CPDMA_MACEOIVECTOR	0x94
#define CPDMA_RXINTSTATRAW	0xa0
#define CPDMA_RXINTSTATMASKED	0xa4
#define CPDMA_RXINTMASKSET	0xa8
#define CPDMA_RXINTMASKCLEAR	0xac
#define CPDMA_DMAINTSTATRAW	0xb0
#define CPDMA_DMAINTSTATMASKED	0xb4
#define CPDMA_DMAINTMASKSET	0xb8
#define CPDMA_DMAINTMASKCLEAR	0xbc
#define CPDMA_DMAINT_HOSTERR	BIT(1)

/* the following exist only if has_ext_regs is set */
#define CPDMA_DMACONTROL	0x20
#define CPDMA_DMASTATUS		0x24
#define CPDMA_RXBUFFOFS		0x28
#define CPDMA_EM_CONTROL	0x2c

/* Descriptor mode bits */
#define CPDMA_DESC_SOP		BIT(31)
#define CPDMA_DESC_EOP		BIT(30)
#define CPDMA_DESC_OWNER	BIT(29)
#define CPDMA_DESC_EOQ		BIT(28)
#define CPDMA_DESC_TD_COMPLETE	BIT(27)
#define CPDMA_DESC_PASS_CRC	BIT(26)

#define CPDMA_TEARDOWN_VALUE	0xfffffffc

struct cpdma_desc {
	/* hardware fields */
	u32			hw_next;
	u32			hw_buffer;
	u32			hw_len;
	u32			hw_mode;
	/* software fields */
	void			*sw_token;
	u32			sw_buffer;
	u32			sw_len;
};

struct cpdma_desc_pool {
	u32			phys;
	u32			hw_addr;
	void __iomem		*iomap;		/* ioremap map */
	void			*cpumap;	/* dma_alloc map */
	int			desc_size, mem_size;
	int			num_desc, used_desc;
	unsigned long		*bitmap;
	struct device		*dev;
	spinlock_t		lock;
};

enum cpdma_state {
	CPDMA_STATE_IDLE,
	CPDMA_STATE_ACTIVE,
	CPDMA_STATE_TEARDOWN,
};

const char *cpdma_state_str[] = { "idle", "active", "teardown" };

struct cpdma_ctlr {
	enum cpdma_state	state;
	struct cpdma_params	params;
	struct device		*dev;
	struct cpdma_desc_pool	*pool;
	spinlock_t		lock;
	struct cpdma_chan	*channels[2 * CPDMA_MAX_CHANNELS];
};

struct cpdma_chan {
	enum cpdma_state		state;
	struct cpdma_ctlr		*ctlr;
	int				chan_num;
	spinlock_t			lock;
	struct cpdma_desc __iomem	*head, *tail;
	int				count;
	void __iomem			*hdp, *cp, *rxfree;
	u32				mask;
	cpdma_handler_fn		handler;
	enum dma_data_direction		dir;
	struct cpdma_chan_stats		stats;
	/* offsets into dmaregs */
	int	int_set, int_clear, td;
};

/* The following make access to common cpdma_ctlr params more readable */
#define dmaregs		params.dmaregs
#define num_chan	params.num_chan

/* various accessors */
#define dma_reg_read(ctlr, ofs)		__raw_readl((ctlr)->dmaregs + (ofs))
#define chan_read(chan, fld)		__raw_readl((chan)->fld)
#define desc_read(desc, fld)		__raw_readl(&(desc)->fld)
#define dma_reg_write(ctlr, ofs, v)	__raw_writel(v, (ctlr)->dmaregs + (ofs))
#define chan_write(chan, fld, v)	__raw_writel(v, (chan)->fld)
#define desc_write(desc, fld, v)	__raw_writel((u32)(v), &(desc)->fld)

/*
 * Utility constructs for a cpdma descriptor pool.  Some devices (e.g. davinci
 * emac) have dedicated on-chip memory for these descriptors.  Some other
 * devices (e.g. cpsw switches) use plain old memory.  Descriptor pools
 * abstract out these details
 */
static struct cpdma_desc_pool *
cpdma_desc_pool_create(struct device *dev, u32 phys, u32 hw_addr,
				int size, int align)
{
	int bitmap_size;
	struct cpdma_desc_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	spin_lock_init(&pool->lock);

	pool->dev	= dev;
	pool->mem_size	= size;
	pool->desc_size	= ALIGN(sizeof(struct cpdma_desc), align);
	pool->num_desc	= size / pool->desc_size;

	bitmap_size  = (pool->num_desc / BITS_PER_LONG) * sizeof(long);
	pool->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!pool->bitmap)
		goto fail;

	if (phys) {
		pool->phys  = phys;
		pool->iomap = ioremap(phys, size);
		pool->hw_addr = hw_addr;
	} else {
		pool->cpumap = dma_alloc_coherent(dev, size, &pool->phys,
						  GFP_KERNEL);
		pool->iomap = pool->cpumap;
		pool->hw_addr = pool->phys;
	}

	if (pool->iomap)
		return pool;

fail:
	kfree(pool->bitmap);
	kfree(pool);
	return NULL;
}

static void cpdma_desc_pool_destroy(struct cpdma_desc_pool *pool)
{
	unsigned long flags;

	if (!pool)
		return;

	spin_lock_irqsave(&pool->lock, flags);
	WARN_ON(pool->used_desc);
	kfree(pool->bitmap);
	if (pool->cpumap) {
		dma_free_coherent(pool->dev, pool->mem_size, pool->cpumap,
				  pool->phys);
	} else {
		iounmap(pool->iomap);
	}
	spin_unlock_irqrestore(&pool->lock, flags);
	kfree(pool);
}

static inline dma_addr_t desc_phys(struct cpdma_desc_pool *pool,
		  struct cpdma_desc __iomem *desc)
{
	if (!desc)
		return 0;
	return pool->hw_addr + (__force dma_addr_t)desc -
			    (__force dma_addr_t)pool->iomap;
}

static inline struct cpdma_desc __iomem *
desc_from_phys(struct cpdma_desc_pool *pool, dma_addr_t dma)
{
	return dma ? pool->iomap + dma - pool->hw_addr : NULL;
}

static struct cpdma_desc __iomem *
cpdma_desc_alloc(struct cpdma_desc_pool *pool, int num_desc)
{
	unsigned long flags;
	int index;
	struct cpdma_desc __iomem *desc = NULL;

	spin_lock_irqsave(&pool->lock, flags);

	index = bitmap_find_next_zero_area(pool->bitmap, pool->num_desc, 0,
					   num_desc, 0);
	if (index < pool->num_desc) {
		bitmap_set(pool->bitmap, index, num_desc);
		desc = pool->iomap + pool->desc_size * index;
		pool->used_desc++;
	}

	spin_unlock_irqrestore(&pool->lock, flags);
	return desc;
}

static void cpdma_desc_free(struct cpdma_desc_pool *pool,
			    struct cpdma_desc __iomem *desc, int num_desc)
{
	unsigned long flags, index;

	index = ((unsigned long)desc - (unsigned long)pool->iomap) /
		pool->desc_size;
	spin_lock_irqsave(&pool->lock, flags);
	bitmap_clear(pool->bitmap, index, num_desc);
	pool->used_desc--;
	spin_unlock_irqrestore(&pool->lock, flags);
}

struct cpdma_ctlr *cpdma_ctlr_create(struct cpdma_params *params)
{
	struct cpdma_ctlr *ctlr;

	ctlr = kzalloc(sizeof(*ctlr), GFP_KERNEL);
	if (!ctlr)
		return NULL;

	ctlr->state = CPDMA_STATE_IDLE;
	ctlr->params = *params;
	ctlr->dev = params->dev;
	spin_lock_init(&ctlr->lock);

	ctlr->pool = cpdma_desc_pool_create(ctlr->dev,
					    ctlr->params.desc_mem_phys,
					    ctlr->params.desc_hw_addr,
					    ctlr->params.desc_mem_size,
					    ctlr->params.desc_align);
	if (!ctlr->pool) {
		kfree(ctlr);
		return NULL;
	}

	if (WARN_ON(ctlr->num_chan > CPDMA_MAX_CHANNELS))
		ctlr->num_chan = CPDMA_MAX_CHANNELS;
	return ctlr;
}

int cpdma_ctlr_start(struct cpdma_ctlr *ctlr)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_IDLE) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return -EBUSY;
	}

	if (ctlr->params.has_soft_reset) {
		unsigned long timeout = jiffies + HZ/10;

		dma_reg_write(ctlr, CPDMA_SOFTRESET, 1);
		while (time_before(jiffies, timeout)) {
			if (dma_reg_read(ctlr, CPDMA_SOFTRESET) == 0)
				break;
		}
		WARN_ON(!time_before(jiffies, timeout));
	}

	for (i = 0; i < ctlr->num_chan; i++) {
		__raw_writel(0, ctlr->params.txhdp + 4 * i);
		__raw_writel(0, ctlr->params.rxhdp + 4 * i);
		__raw_writel(0, ctlr->params.txcp + 4 * i);
		__raw_writel(0, ctlr->params.rxcp + 4 * i);
	}

	dma_reg_write(ctlr, CPDMA_RXINTMASKCLEAR, 0xffffffff);
	dma_reg_write(ctlr, CPDMA_TXINTMASKCLEAR, 0xffffffff);

	dma_reg_write(ctlr, CPDMA_TXCONTROL, 1);
	dma_reg_write(ctlr, CPDMA_RXCONTROL, 1);

	ctlr->state = CPDMA_STATE_ACTIVE;

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		if (ctlr->channels[i])
			cpdma_chan_start(ctlr->channels[i]);
	}
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}

int cpdma_ctlr_stop(struct cpdma_ctlr *ctlr)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return -EINVAL;
	}

	ctlr->state = CPDMA_STATE_TEARDOWN;

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		if (ctlr->channels[i])
			cpdma_chan_stop(ctlr->channels[i]);
	}

	dma_reg_write(ctlr, CPDMA_RXINTMASKCLEAR, 0xffffffff);
	dma_reg_write(ctlr, CPDMA_TXINTMASKCLEAR, 0xffffffff);

	dma_reg_write(ctlr, CPDMA_TXCONTROL, 0);
	dma_reg_write(ctlr, CPDMA_RXCONTROL, 0);

	ctlr->state = CPDMA_STATE_IDLE;

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}

int cpdma_ctlr_dump(struct cpdma_ctlr *ctlr)
{
	struct device *dev = ctlr->dev;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ctlr->lock, flags);

	dev_info(dev, "CPDMA: state: %s", cpdma_state_str[ctlr->state]);

	dev_info(dev, "CPDMA: txidver: %x",
		 dma_reg_read(ctlr, CPDMA_TXIDVER));
	dev_info(dev, "CPDMA: txcontrol: %x",
		 dma_reg_read(ctlr, CPDMA_TXCONTROL));
	dev_info(dev, "CPDMA: txteardown: %x",
		 dma_reg_read(ctlr, CPDMA_TXTEARDOWN));
	dev_info(dev, "CPDMA: rxidver: %x",
		 dma_reg_read(ctlr, CPDMA_RXIDVER));
	dev_info(dev, "CPDMA: rxcontrol: %x",
		 dma_reg_read(ctlr, CPDMA_RXCONTROL));
	dev_info(dev, "CPDMA: softreset: %x",
		 dma_reg_read(ctlr, CPDMA_SOFTRESET));
	dev_info(dev, "CPDMA: rxteardown: %x",
		 dma_reg_read(ctlr, CPDMA_RXTEARDOWN));
	dev_info(dev, "CPDMA: txintstatraw: %x",
		 dma_reg_read(ctlr, CPDMA_TXINTSTATRAW));
	dev_info(dev, "CPDMA: txintstatmasked: %x",
		 dma_reg_read(ctlr, CPDMA_TXINTSTATMASKED));
	dev_info(dev, "CPDMA: txintmaskset: %x",
		 dma_reg_read(ctlr, CPDMA_TXINTMASKSET));
	dev_info(dev, "CPDMA: txintmaskclear: %x",
		 dma_reg_read(ctlr, CPDMA_TXINTMASKCLEAR));
	dev_info(dev, "CPDMA: macinvector: %x",
		 dma_reg_read(ctlr, CPDMA_MACINVECTOR));
	dev_info(dev, "CPDMA: maceoivector: %x",
		 dma_reg_read(ctlr, CPDMA_MACEOIVECTOR));
	dev_info(dev, "CPDMA: rxintstatraw: %x",
		 dma_reg_read(ctlr, CPDMA_RXINTSTATRAW));
	dev_info(dev, "CPDMA: rxintstatmasked: %x",
		 dma_reg_read(ctlr, CPDMA_RXINTSTATMASKED));
	dev_info(dev, "CPDMA: rxintmaskset: %x",
		 dma_reg_read(ctlr, CPDMA_RXINTMASKSET));
	dev_info(dev, "CPDMA: rxintmaskclear: %x",
		 dma_reg_read(ctlr, CPDMA_RXINTMASKCLEAR));
	dev_info(dev, "CPDMA: dmaintstatraw: %x",
		 dma_reg_read(ctlr, CPDMA_DMAINTSTATRAW));
	dev_info(dev, "CPDMA: dmaintstatmasked: %x",
		 dma_reg_read(ctlr, CPDMA_DMAINTSTATMASKED));
	dev_info(dev, "CPDMA: dmaintmaskset: %x",
		 dma_reg_read(ctlr, CPDMA_DMAINTMASKSET));
	dev_info(dev, "CPDMA: dmaintmaskclear: %x",
		 dma_reg_read(ctlr, CPDMA_DMAINTMASKCLEAR));

	if (!ctlr->params.has_ext_regs) {
		dev_info(dev, "CPDMA: dmacontrol: %x",
			 dma_reg_read(ctlr, CPDMA_DMACONTROL));
		dev_info(dev, "CPDMA: dmastatus: %x",
			 dma_reg_read(ctlr, CPDMA_DMASTATUS));
		dev_info(dev, "CPDMA: rxbuffofs: %x",
			 dma_reg_read(ctlr, CPDMA_RXBUFFOFS));
	}

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++)
		if (ctlr->channels[i])
			cpdma_chan_dump(ctlr->channels[i]);

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}

int cpdma_ctlr_destroy(struct cpdma_ctlr *ctlr)
{
	unsigned long flags;
	int ret = 0, i;

	if (!ctlr)
		return -EINVAL;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_IDLE)
		cpdma_ctlr_stop(ctlr);

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		if (ctlr->channels[i])
			cpdma_chan_destroy(ctlr->channels[i]);
	}

	cpdma_desc_pool_destroy(ctlr->pool);
	spin_unlock_irqrestore(&ctlr->lock, flags);
	kfree(ctlr);
	return ret;
}

int cpdma_ctlr_int_ctrl(struct cpdma_ctlr *ctlr, bool enable)
{
	unsigned long flags;
	int i, reg;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (ctlr->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&ctlr->lock, flags);
		return -EINVAL;
	}

	reg = enable ? CPDMA_DMAINTMASKSET : CPDMA_DMAINTMASKCLEAR;
	dma_reg_write(ctlr, reg, CPDMA_DMAINT_HOSTERR);

	for (i = 0; i < ARRAY_SIZE(ctlr->channels); i++) {
		if (ctlr->channels[i])
			cpdma_chan_int_ctrl(ctlr->channels[i], enable);
	}

	spin_unlock_irqrestore(&ctlr->lock, flags);
	return 0;
}

void cpdma_ctlr_eoi(struct cpdma_ctlr *ctlr)
{
	dma_reg_write(ctlr, CPDMA_MACEOIVECTOR, 0);
}

struct cpdma_chan *cpdma_chan_create(struct cpdma_ctlr *ctlr, int chan_num,
				     cpdma_handler_fn handler)
{
	struct cpdma_chan *chan;
	int ret, offset = (chan_num % CPDMA_MAX_CHANNELS) * 4;
	unsigned long flags;

	if (__chan_linear(chan_num) >= ctlr->num_chan)
		return NULL;

	ret = -ENOMEM;
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		goto err_chan_alloc;

	spin_lock_irqsave(&ctlr->lock, flags);
	ret = -EBUSY;
	if (ctlr->channels[chan_num])
		goto err_chan_busy;

	chan->ctlr	= ctlr;
	chan->state	= CPDMA_STATE_IDLE;
	chan->chan_num	= chan_num;
	chan->handler	= handler;

	if (is_rx_chan(chan)) {
		chan->hdp	= ctlr->params.rxhdp + offset;
		chan->cp	= ctlr->params.rxcp + offset;
		chan->rxfree	= ctlr->params.rxfree + offset;
		chan->int_set	= CPDMA_RXINTMASKSET;
		chan->int_clear	= CPDMA_RXINTMASKCLEAR;
		chan->td	= CPDMA_RXTEARDOWN;
		chan->dir	= DMA_FROM_DEVICE;
	} else {
		chan->hdp	= ctlr->params.txhdp + offset;
		chan->cp	= ctlr->params.txcp + offset;
		chan->int_set	= CPDMA_TXINTMASKSET;
		chan->int_clear	= CPDMA_TXINTMASKCLEAR;
		chan->td	= CPDMA_TXTEARDOWN;
		chan->dir	= DMA_TO_DEVICE;
	}
	chan->mask = BIT(chan_linear(chan));

	spin_lock_init(&chan->lock);

	ctlr->channels[chan_num] = chan;
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return chan;

err_chan_busy:
	spin_unlock_irqrestore(&ctlr->lock, flags);
	kfree(chan);
err_chan_alloc:
	return ERR_PTR(ret);
}

int cpdma_chan_destroy(struct cpdma_chan *chan)
{
	struct cpdma_ctlr *ctlr = chan->ctlr;
	unsigned long flags;

	if (!chan)
		return -EINVAL;

	spin_lock_irqsave(&ctlr->lock, flags);
	if (chan->state != CPDMA_STATE_IDLE)
		cpdma_chan_stop(chan);
	ctlr->channels[chan->chan_num] = NULL;
	spin_unlock_irqrestore(&ctlr->lock, flags);
	kfree(chan);
	return 0;
}

int cpdma_chan_get_stats(struct cpdma_chan *chan,
			 struct cpdma_chan_stats *stats)
{
	unsigned long flags;
	if (!chan)
		return -EINVAL;
	spin_lock_irqsave(&chan->lock, flags);
	memcpy(stats, &chan->stats, sizeof(*stats));
	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}

int cpdma_chan_dump(struct cpdma_chan *chan)
{
	unsigned long flags;
	struct device *dev = chan->ctlr->dev;

	spin_lock_irqsave(&chan->lock, flags);

	dev_info(dev, "channel %d (%s %d) state %s",
		 chan->chan_num, is_rx_chan(chan) ? "rx" : "tx",
		 chan_linear(chan), cpdma_state_str[chan->state]);
	dev_info(dev, "\thdp: %x\n", chan_read(chan, hdp));
	dev_info(dev, "\tcp: %x\n", chan_read(chan, cp));
	if (chan->rxfree) {
		dev_info(dev, "\trxfree: %x\n",
			 chan_read(chan, rxfree));
	}

	dev_info(dev, "\tstats head_enqueue: %d\n",
		 chan->stats.head_enqueue);
	dev_info(dev, "\tstats tail_enqueue: %d\n",
		 chan->stats.tail_enqueue);
	dev_info(dev, "\tstats pad_enqueue: %d\n",
		 chan->stats.pad_enqueue);
	dev_info(dev, "\tstats misqueued: %d\n",
		 chan->stats.misqueued);
	dev_info(dev, "\tstats desc_alloc_fail: %d\n",
		 chan->stats.desc_alloc_fail);
	dev_info(dev, "\tstats pad_alloc_fail: %d\n",
		 chan->stats.pad_alloc_fail);
	dev_info(dev, "\tstats runt_receive_buff: %d\n",
		 chan->stats.runt_receive_buff);
	dev_info(dev, "\tstats runt_transmit_buff: %d\n",
		 chan->stats.runt_transmit_buff);
	dev_info(dev, "\tstats empty_dequeue: %d\n",
		 chan->stats.empty_dequeue);
	dev_info(dev, "\tstats busy_dequeue: %d\n",
		 chan->stats.busy_dequeue);
	dev_info(dev, "\tstats good_dequeue: %d\n",
		 chan->stats.good_dequeue);
	dev_info(dev, "\tstats requeue: %d\n",
		 chan->stats.requeue);
	dev_info(dev, "\tstats teardown_dequeue: %d\n",
		 chan->stats.teardown_dequeue);

	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}

static void __cpdma_chan_submit(struct cpdma_chan *chan,
				struct cpdma_desc __iomem *desc)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc __iomem	*prev = chan->tail;
	struct cpdma_desc_pool		*pool = ctlr->pool;
	dma_addr_t			desc_dma;
	u32				mode;

	desc_dma = desc_phys(pool, desc);

	/* simple case - idle channel */
	if (!chan->head) {
		chan->stats.head_enqueue++;
		chan->head = desc;
		chan->tail = desc;
		if (chan->state == CPDMA_STATE_ACTIVE)
			chan_write(chan, hdp, desc_dma);
		return;
	}

	/* first chain the descriptor at the tail of the list */
	desc_write(prev, hw_next, desc_dma);
	chan->tail = desc;
	chan->stats.tail_enqueue++;

	/* next check if EOQ has been triggered already */
	mode = desc_read(prev, hw_mode);
	if (((mode & (CPDMA_DESC_EOQ | CPDMA_DESC_OWNER)) == CPDMA_DESC_EOQ) &&
	    (chan->state == CPDMA_STATE_ACTIVE)) {
		desc_write(prev, hw_mode, mode & ~CPDMA_DESC_EOQ);
		chan_write(chan, hdp, desc_dma);
		chan->stats.misqueued++;
	}
}

int cpdma_chan_submit(struct cpdma_chan *chan, void *token, void *data,
		      int len, gfp_t gfp_mask)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc __iomem	*desc;
	dma_addr_t			buffer;
	unsigned long			flags;
	u32				mode;
	int				ret = 0;

	spin_lock_irqsave(&chan->lock, flags);

	if (chan->state == CPDMA_STATE_TEARDOWN) {
		ret = -EINVAL;
		goto unlock_ret;
	}

	desc = cpdma_desc_alloc(ctlr->pool, 1);
	if (!desc) {
		chan->stats.desc_alloc_fail++;
		ret = -ENOMEM;
		goto unlock_ret;
	}

	if (len < ctlr->params.min_packet_size) {
		len = ctlr->params.min_packet_size;
		chan->stats.runt_transmit_buff++;
	}

	buffer = dma_map_single(ctlr->dev, data, len, chan->dir);
	mode = CPDMA_DESC_OWNER | CPDMA_DESC_SOP | CPDMA_DESC_EOP;

	desc_write(desc, hw_next,   0);
	desc_write(desc, hw_buffer, buffer);
	desc_write(desc, hw_len,    len);
	desc_write(desc, hw_mode,   mode | len);
	desc_write(desc, sw_token,  token);
	desc_write(desc, sw_buffer, buffer);
	desc_write(desc, sw_len,    len);

	__cpdma_chan_submit(chan, desc);

	if (chan->state == CPDMA_STATE_ACTIVE && chan->rxfree)
		chan_write(chan, rxfree, 1);

	chan->count++;

unlock_ret:
	spin_unlock_irqrestore(&chan->lock, flags);
	return ret;
}

static void __cpdma_chan_free(struct cpdma_chan *chan,
			      struct cpdma_desc __iomem *desc,
			      int outlen, int status)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc_pool		*pool = ctlr->pool;
	dma_addr_t			buff_dma;
	int				origlen;
	void				*token;

	token      = (void *)desc_read(desc, sw_token);
	buff_dma   = desc_read(desc, sw_buffer);
	origlen    = desc_read(desc, sw_len);

	dma_unmap_single(ctlr->dev, buff_dma, origlen, chan->dir);
	cpdma_desc_free(pool, desc, 1);
	(*chan->handler)(token, outlen, status);
}

static int __cpdma_chan_process(struct cpdma_chan *chan)
{
	struct cpdma_ctlr		*ctlr = chan->ctlr;
	struct cpdma_desc __iomem	*desc;
	int				status, outlen;
	struct cpdma_desc_pool		*pool = ctlr->pool;
	dma_addr_t			desc_dma;
	unsigned long			flags;

	spin_lock_irqsave(&chan->lock, flags);

	desc = chan->head;
	if (!desc) {
		chan->stats.empty_dequeue++;
		status = -ENOENT;
		goto unlock_ret;
	}
	desc_dma = desc_phys(pool, desc);

	status	= __raw_readl(&desc->hw_mode);
	outlen	= status & 0x7ff;
	if (status & CPDMA_DESC_OWNER) {
		chan->stats.busy_dequeue++;
		status = -EBUSY;
		goto unlock_ret;
	}
	status	= status & (CPDMA_DESC_EOQ | CPDMA_DESC_TD_COMPLETE);

	chan->head = desc_from_phys(pool, desc_read(desc, hw_next));
	chan_write(chan, cp, desc_dma);
	chan->count--;
	chan->stats.good_dequeue++;

	if (status & CPDMA_DESC_EOQ) {
		chan->stats.requeue++;
		chan_write(chan, hdp, desc_phys(pool, chan->head));
	}

	spin_unlock_irqrestore(&chan->lock, flags);

	__cpdma_chan_free(chan, desc, outlen, status);
	return status;

unlock_ret:
	spin_unlock_irqrestore(&chan->lock, flags);
	return status;
}

int cpdma_chan_process(struct cpdma_chan *chan, int quota)
{
	int used = 0, ret = 0;

	if (chan->state != CPDMA_STATE_ACTIVE)
		return -EINVAL;

	while (used < quota) {
		ret = __cpdma_chan_process(chan);
		if (ret < 0)
			break;
		used++;
	}
	return used;
}

int cpdma_chan_start(struct cpdma_chan *chan)
{
	struct cpdma_ctlr	*ctlr = chan->ctlr;
	struct cpdma_desc_pool	*pool = ctlr->pool;
	unsigned long		flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->state != CPDMA_STATE_IDLE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EBUSY;
	}
	if (ctlr->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EINVAL;
	}
	dma_reg_write(ctlr, chan->int_set, chan->mask);
	chan->state = CPDMA_STATE_ACTIVE;
	if (chan->head) {
		chan_write(chan, hdp, desc_phys(pool, chan->head));
		if (chan->rxfree)
			chan_write(chan, rxfree, chan->count);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}

int cpdma_chan_stop(struct cpdma_chan *chan)
{
	struct cpdma_ctlr	*ctlr = chan->ctlr;
	struct cpdma_desc_pool	*pool = ctlr->pool;
	unsigned long		flags;
	int			ret;
	unsigned long		timeout;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EINVAL;
	}

	chan->state = CPDMA_STATE_TEARDOWN;
	dma_reg_write(ctlr, chan->int_clear, chan->mask);

	/* trigger teardown */
	dma_reg_write(ctlr, chan->td, chan->chan_num);

	/* wait for teardown complete */
	timeout = jiffies + HZ/10;	/* 100 msec */
	while (time_before(jiffies, timeout)) {
		u32 cp = chan_read(chan, cp);
		if ((cp & CPDMA_TEARDOWN_VALUE) == CPDMA_TEARDOWN_VALUE)
			break;
		cpu_relax();
	}
	WARN_ON(!time_before(jiffies, timeout));
	chan_write(chan, cp, CPDMA_TEARDOWN_VALUE);

	/* handle completed packets */
	do {
		ret = __cpdma_chan_process(chan);
		if (ret < 0)
			break;
	} while ((ret & CPDMA_DESC_TD_COMPLETE) == 0);

	/* remaining packets haven't been tx/rx'ed, clean them up */
	while (chan->head) {
		struct cpdma_desc __iomem *desc = chan->head;
		dma_addr_t next_dma;

		next_dma = desc_read(desc, hw_next);
		chan->head = desc_from_phys(pool, next_dma);
		chan->stats.teardown_dequeue++;

		/* issue callback without locks held */
		spin_unlock_irqrestore(&chan->lock, flags);
		__cpdma_chan_free(chan, desc, 0, -ENOSYS);
		spin_lock_irqsave(&chan->lock, flags);
	}

	chan->state = CPDMA_STATE_IDLE;
	spin_unlock_irqrestore(&chan->lock, flags);
	return 0;
}

int cpdma_chan_int_ctrl(struct cpdma_chan *chan, bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (chan->state != CPDMA_STATE_ACTIVE) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return -EINVAL;
	}

	dma_reg_write(chan->ctlr, enable ? chan->int_set : chan->int_clear,
		      chan->mask);
	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

struct cpdma_control_info {
	u32		reg;
	u32		shift, mask;
	int		access;
#define ACCESS_RO	BIT(0)
#define ACCESS_WO	BIT(1)
#define ACCESS_RW	(ACCESS_RO | ACCESS_WO)
};

struct cpdma_control_info controls[] = {
	[CPDMA_CMD_IDLE]	  = {CPDMA_DMACONTROL,	3,  1,      ACCESS_WO},
	[CPDMA_COPY_ERROR_FRAMES] = {CPDMA_DMACONTROL,	4,  1,      ACCESS_RW},
	[CPDMA_RX_OFF_LEN_UPDATE] = {CPDMA_DMACONTROL,	2,  1,      ACCESS_RW},
	[CPDMA_RX_OWNERSHIP_FLIP] = {CPDMA_DMACONTROL,	1,  1,      ACCESS_RW},
	[CPDMA_TX_PRIO_FIXED]	  = {CPDMA_DMACONTROL,	0,  1,      ACCESS_RW},
	[CPDMA_STAT_IDLE]	  = {CPDMA_DMASTATUS,	31, 1,      ACCESS_RO},
	[CPDMA_STAT_TX_ERR_CODE]  = {CPDMA_DMASTATUS,	20, 0xf,    ACCESS_RW},
	[CPDMA_STAT_TX_ERR_CHAN]  = {CPDMA_DMASTATUS,	16, 0x7,    ACCESS_RW},
	[CPDMA_STAT_RX_ERR_CODE]  = {CPDMA_DMASTATUS,	12, 0xf,    ACCESS_RW},
	[CPDMA_STAT_RX_ERR_CHAN]  = {CPDMA_DMASTATUS,	8,  0x7,    ACCESS_RW},
	[CPDMA_RX_BUFFER_OFFSET]  = {CPDMA_RXBUFFOFS,	0,  0xffff, ACCESS_RW},
};

int cpdma_control_get(struct cpdma_ctlr *ctlr, int control)
{
	unsigned long flags;
	struct cpdma_control_info *info = &controls[control];
	int ret;

	spin_lock_irqsave(&ctlr->lock, flags);

	ret = -ENOTSUPP;
	if (!ctlr->params.has_ext_regs)
		goto unlock_ret;

	ret = -EINVAL;
	if (ctlr->state != CPDMA_STATE_ACTIVE)
		goto unlock_ret;

	ret = -ENOENT;
	if (control < 0 || control >= ARRAY_SIZE(controls))
		goto unlock_ret;

	ret = -EPERM;
	if ((info->access & ACCESS_RO) != ACCESS_RO)
		goto unlock_ret;

	ret = (dma_reg_read(ctlr, info->reg) >> info->shift) & info->mask;

unlock_ret:
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return ret;
}

int cpdma_control_set(struct cpdma_ctlr *ctlr, int control, int value)
{
	unsigned long flags;
	struct cpdma_control_info *info = &controls[control];
	int ret;
	u32 val;

	spin_lock_irqsave(&ctlr->lock, flags);

	ret = -ENOTSUPP;
	if (!ctlr->params.has_ext_regs)
		goto unlock_ret;

	ret = -EINVAL;
	if (ctlr->state != CPDMA_STATE_ACTIVE)
		goto unlock_ret;

	ret = -ENOENT;
	if (control < 0 || control >= ARRAY_SIZE(controls))
		goto unlock_ret;

	ret = -EPERM;
	if ((info->access & ACCESS_WO) != ACCESS_WO)
		goto unlock_ret;

	val  = dma_reg_read(ctlr, info->reg);
	val &= ~(info->mask << info->shift);
	val |= (value & info->mask) << info->shift;
	dma_reg_write(ctlr, info->reg, val);
	ret = 0;

unlock_ret:
	spin_unlock_irqrestore(&ctlr->lock, flags);
	return ret;
}
