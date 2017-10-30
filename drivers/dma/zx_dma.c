/*
 * Copyright 2015 Linaro.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/of_dma.h>

#include "virt-dma.h"

#define DRIVER_NAME		"zx-dma"
#define DMA_ALIGN		4
#define DMA_MAX_SIZE		(0x10000 - 512)
#define LLI_BLOCK_SIZE		(4 * PAGE_SIZE)

#define REG_ZX_SRC_ADDR			0x00
#define REG_ZX_DST_ADDR			0x04
#define REG_ZX_TX_X_COUNT		0x08
#define REG_ZX_TX_ZY_COUNT		0x0c
#define REG_ZX_SRC_ZY_STEP		0x10
#define REG_ZX_DST_ZY_STEP		0x14
#define REG_ZX_LLI_ADDR			0x1c
#define REG_ZX_CTRL			0x20
#define REG_ZX_TC_IRQ			0x800
#define REG_ZX_SRC_ERR_IRQ		0x804
#define REG_ZX_DST_ERR_IRQ		0x808
#define REG_ZX_CFG_ERR_IRQ		0x80c
#define REG_ZX_TC_IRQ_RAW		0x810
#define REG_ZX_SRC_ERR_IRQ_RAW		0x814
#define REG_ZX_DST_ERR_IRQ_RAW		0x818
#define REG_ZX_CFG_ERR_IRQ_RAW		0x81c
#define REG_ZX_STATUS			0x820
#define REG_ZX_DMA_GRP_PRIO		0x824
#define REG_ZX_DMA_ARB			0x828

#define ZX_FORCE_CLOSE			BIT(31)
#define ZX_DST_BURST_WIDTH(x)		(((x) & 0x7) << 13)
#define ZX_MAX_BURST_LEN		16
#define ZX_SRC_BURST_LEN(x)		(((x) & 0xf) << 9)
#define ZX_SRC_BURST_WIDTH(x)		(((x) & 0x7) << 6)
#define ZX_IRQ_ENABLE_ALL		(3 << 4)
#define ZX_DST_FIFO_MODE		BIT(3)
#define ZX_SRC_FIFO_MODE		BIT(2)
#define ZX_SOFT_REQ			BIT(1)
#define ZX_CH_ENABLE			BIT(0)

#define ZX_DMA_BUSWIDTHS \
	(BIT(DMA_SLAVE_BUSWIDTH_UNDEFINED) | \
	BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
	BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) | \
	BIT(DMA_SLAVE_BUSWIDTH_8_BYTES))

enum zx_dma_burst_width {
	ZX_DMA_WIDTH_8BIT	= 0,
	ZX_DMA_WIDTH_16BIT	= 1,
	ZX_DMA_WIDTH_32BIT	= 2,
	ZX_DMA_WIDTH_64BIT	= 3,
};

struct zx_desc_hw {
	u32 saddr;
	u32 daddr;
	u32 src_x;
	u32 src_zy;
	u32 src_zy_step;
	u32 dst_zy_step;
	u32 reserved1;
	u32 lli;
	u32 ctr;
	u32 reserved[7]; /* pack as hardware registers region size */
} __aligned(32);

struct zx_dma_desc_sw {
	struct virt_dma_desc	vd;
	dma_addr_t		desc_hw_lli;
	size_t			desc_num;
	size_t			size;
	struct zx_desc_hw	*desc_hw;
};

struct zx_dma_phy;

struct zx_dma_chan {
	struct dma_slave_config slave_cfg;
	int			id; /* Request phy chan id */
	u32			ccfg;
	u32			cyclic;
	struct virt_dma_chan	vc;
	struct zx_dma_phy	*phy;
	struct list_head	node;
	dma_addr_t		dev_addr;
	enum dma_status		status;
};

struct zx_dma_phy {
	u32			idx;
	void __iomem		*base;
	struct zx_dma_chan	*vchan;
	struct zx_dma_desc_sw	*ds_run;
	struct zx_dma_desc_sw	*ds_done;
};

struct zx_dma_dev {
	struct dma_device	slave;
	void __iomem		*base;
	spinlock_t		lock; /* lock for ch and phy */
	struct list_head	chan_pending;
	struct zx_dma_phy	*phy;
	struct zx_dma_chan	*chans;
	struct clk		*clk;
	struct dma_pool		*pool;
	u32			dma_channels;
	u32			dma_requests;
	int 			irq;
};

#define to_zx_dma(dmadev) container_of(dmadev, struct zx_dma_dev, slave)

static struct zx_dma_chan *to_zx_chan(struct dma_chan *chan)
{
	return container_of(chan, struct zx_dma_chan, vc.chan);
}

static void zx_dma_terminate_chan(struct zx_dma_phy *phy, struct zx_dma_dev *d)
{
	u32 val = 0;

	val = readl_relaxed(phy->base + REG_ZX_CTRL);
	val &= ~ZX_CH_ENABLE;
	val |= ZX_FORCE_CLOSE;
	writel_relaxed(val, phy->base + REG_ZX_CTRL);

	val = 0x1 << phy->idx;
	writel_relaxed(val, d->base + REG_ZX_TC_IRQ_RAW);
	writel_relaxed(val, d->base + REG_ZX_SRC_ERR_IRQ_RAW);
	writel_relaxed(val, d->base + REG_ZX_DST_ERR_IRQ_RAW);
	writel_relaxed(val, d->base + REG_ZX_CFG_ERR_IRQ_RAW);
}

static void zx_dma_set_desc(struct zx_dma_phy *phy, struct zx_desc_hw *hw)
{
	writel_relaxed(hw->saddr, phy->base + REG_ZX_SRC_ADDR);
	writel_relaxed(hw->daddr, phy->base + REG_ZX_DST_ADDR);
	writel_relaxed(hw->src_x, phy->base + REG_ZX_TX_X_COUNT);
	writel_relaxed(0, phy->base + REG_ZX_TX_ZY_COUNT);
	writel_relaxed(0, phy->base + REG_ZX_SRC_ZY_STEP);
	writel_relaxed(0, phy->base + REG_ZX_DST_ZY_STEP);
	writel_relaxed(hw->lli, phy->base + REG_ZX_LLI_ADDR);
	writel_relaxed(hw->ctr, phy->base + REG_ZX_CTRL);
}

static u32 zx_dma_get_curr_lli(struct zx_dma_phy *phy)
{
	return readl_relaxed(phy->base + REG_ZX_LLI_ADDR);
}

static u32 zx_dma_get_chan_stat(struct zx_dma_dev *d)
{
	return readl_relaxed(d->base + REG_ZX_STATUS);
}

static void zx_dma_init_state(struct zx_dma_dev *d)
{
	/* set same priority */
	writel_relaxed(0x0, d->base + REG_ZX_DMA_ARB);
	/* clear all irq */
	writel_relaxed(0xffffffff, d->base + REG_ZX_TC_IRQ_RAW);
	writel_relaxed(0xffffffff, d->base + REG_ZX_SRC_ERR_IRQ_RAW);
	writel_relaxed(0xffffffff, d->base + REG_ZX_DST_ERR_IRQ_RAW);
	writel_relaxed(0xffffffff, d->base + REG_ZX_CFG_ERR_IRQ_RAW);
}

static int zx_dma_start_txd(struct zx_dma_chan *c)
{
	struct zx_dma_dev *d = to_zx_dma(c->vc.chan.device);
	struct virt_dma_desc *vd = vchan_next_desc(&c->vc);

	if (!c->phy)
		return -EAGAIN;

	if (BIT(c->phy->idx) & zx_dma_get_chan_stat(d))
		return -EAGAIN;

	if (vd) {
		struct zx_dma_desc_sw *ds =
			container_of(vd, struct zx_dma_desc_sw, vd);
		/*
		 * fetch and remove request from vc->desc_issued
		 * so vc->desc_issued only contains desc pending
		 */
		list_del(&ds->vd.node);
		c->phy->ds_run = ds;
		c->phy->ds_done = NULL;
		/* start dma */
		zx_dma_set_desc(c->phy, ds->desc_hw);
		return 0;
	}
	c->phy->ds_done = NULL;
	c->phy->ds_run = NULL;
	return -EAGAIN;
}

static void zx_dma_task(struct zx_dma_dev *d)
{
	struct zx_dma_phy *p;
	struct zx_dma_chan *c, *cn;
	unsigned pch, pch_alloc = 0;
	unsigned long flags;

	/* check new dma request of running channel in vc->desc_issued */
	list_for_each_entry_safe(c, cn, &d->slave.channels,
				 vc.chan.device_node) {
		spin_lock_irqsave(&c->vc.lock, flags);
		p = c->phy;
		if (p && p->ds_done && zx_dma_start_txd(c)) {
			/* No current txd associated with this channel */
			dev_dbg(d->slave.dev, "pchan %u: free\n", p->idx);
			/* Mark this channel free */
			c->phy = NULL;
			p->vchan = NULL;
		}
		spin_unlock_irqrestore(&c->vc.lock, flags);
	}

	/* check new channel request in d->chan_pending */
	spin_lock_irqsave(&d->lock, flags);
	while (!list_empty(&d->chan_pending)) {
		c = list_first_entry(&d->chan_pending,
				     struct zx_dma_chan, node);
		p = &d->phy[c->id];
		if (!p->vchan) {
			/* remove from d->chan_pending */
			list_del_init(&c->node);
			pch_alloc |= 1 << c->id;
			/* Mark this channel allocated */
			p->vchan = c;
			c->phy = p;
		} else {
			dev_dbg(d->slave.dev, "pchan %u: busy!\n", c->id);
		}
	}
	spin_unlock_irqrestore(&d->lock, flags);

	for (pch = 0; pch < d->dma_channels; pch++) {
		if (pch_alloc & (1 << pch)) {
			p = &d->phy[pch];
			c = p->vchan;
			if (c) {
				spin_lock_irqsave(&c->vc.lock, flags);
				zx_dma_start_txd(c);
				spin_unlock_irqrestore(&c->vc.lock, flags);
			}
		}
	}
}

static irqreturn_t zx_dma_int_handler(int irq, void *dev_id)
{
	struct zx_dma_dev *d = (struct zx_dma_dev *)dev_id;
	struct zx_dma_phy *p;
	struct zx_dma_chan *c;
	u32 tc = readl_relaxed(d->base + REG_ZX_TC_IRQ);
	u32 serr = readl_relaxed(d->base + REG_ZX_SRC_ERR_IRQ);
	u32 derr = readl_relaxed(d->base + REG_ZX_DST_ERR_IRQ);
	u32 cfg = readl_relaxed(d->base + REG_ZX_CFG_ERR_IRQ);
	u32 i, irq_chan = 0, task = 0;

	while (tc) {
		i = __ffs(tc);
		tc &= ~BIT(i);
		p = &d->phy[i];
		c = p->vchan;
		if (c) {
			unsigned long flags;

			spin_lock_irqsave(&c->vc.lock, flags);
			if (c->cyclic) {
				vchan_cyclic_callback(&p->ds_run->vd);
			} else {
				vchan_cookie_complete(&p->ds_run->vd);
				p->ds_done = p->ds_run;
				task = 1;
			}
			spin_unlock_irqrestore(&c->vc.lock, flags);
			irq_chan |= BIT(i);
		}
	}

	if (serr || derr || cfg)
		dev_warn(d->slave.dev, "DMA ERR src 0x%x, dst 0x%x, cfg 0x%x\n",
			 serr, derr, cfg);

	writel_relaxed(irq_chan, d->base + REG_ZX_TC_IRQ_RAW);
	writel_relaxed(serr, d->base + REG_ZX_SRC_ERR_IRQ_RAW);
	writel_relaxed(derr, d->base + REG_ZX_DST_ERR_IRQ_RAW);
	writel_relaxed(cfg, d->base + REG_ZX_CFG_ERR_IRQ_RAW);

	if (task)
		zx_dma_task(d);
	return IRQ_HANDLED;
}

static void zx_dma_free_chan_resources(struct dma_chan *chan)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_dev *d = to_zx_dma(chan->device);
	unsigned long flags;

	spin_lock_irqsave(&d->lock, flags);
	list_del_init(&c->node);
	spin_unlock_irqrestore(&d->lock, flags);

	vchan_free_chan_resources(&c->vc);
	c->ccfg = 0;
}

static enum dma_status zx_dma_tx_status(struct dma_chan *chan,
					dma_cookie_t cookie,
					struct dma_tx_state *state)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_phy *p;
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	size_t bytes = 0;

	ret = dma_cookie_status(&c->vc.chan, cookie, state);
	if (ret == DMA_COMPLETE || !state)
		return ret;

	spin_lock_irqsave(&c->vc.lock, flags);
	p = c->phy;
	ret = c->status;

	/*
	 * If the cookie is on our issue queue, then the residue is
	 * its total size.
	 */
	vd = vchan_find_desc(&c->vc, cookie);
	if (vd) {
		bytes = container_of(vd, struct zx_dma_desc_sw, vd)->size;
	} else if ((!p) || (!p->ds_run)) {
		bytes = 0;
	} else {
		struct zx_dma_desc_sw *ds = p->ds_run;
		u32 clli = 0, index = 0;

		bytes = 0;
		clli = zx_dma_get_curr_lli(p);
		index = (clli - ds->desc_hw_lli) /
				sizeof(struct zx_desc_hw) + 1;
		for (; index < ds->desc_num; index++) {
			bytes += ds->desc_hw[index].src_x;
			/* end of lli */
			if (!ds->desc_hw[index].lli)
				break;
		}
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
	dma_set_residue(state, bytes);
	return ret;
}

static void zx_dma_issue_pending(struct dma_chan *chan)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_dev *d = to_zx_dma(chan->device);
	unsigned long flags;
	int issue = 0;

	spin_lock_irqsave(&c->vc.lock, flags);
	/* add request to vc->desc_issued */
	if (vchan_issue_pending(&c->vc)) {
		spin_lock(&d->lock);
		if (!c->phy && list_empty(&c->node)) {
			/* if new channel, add chan_pending */
			list_add_tail(&c->node, &d->chan_pending);
			issue = 1;
			dev_dbg(d->slave.dev, "vchan %p: issued\n", &c->vc);
		}
		spin_unlock(&d->lock);
	} else {
		dev_dbg(d->slave.dev, "vchan %p: nothing to issue\n", &c->vc);
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);

	if (issue)
		zx_dma_task(d);
}

static void zx_dma_fill_desc(struct zx_dma_desc_sw *ds, dma_addr_t dst,
			     dma_addr_t src, size_t len, u32 num, u32 ccfg)
{
	if ((num + 1) < ds->desc_num)
		ds->desc_hw[num].lli = ds->desc_hw_lli + (num + 1) *
			sizeof(struct zx_desc_hw);
	ds->desc_hw[num].saddr = src;
	ds->desc_hw[num].daddr = dst;
	ds->desc_hw[num].src_x = len;
	ds->desc_hw[num].ctr = ccfg;
}

static struct zx_dma_desc_sw *zx_alloc_desc_resource(int num,
						     struct dma_chan *chan)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_desc_sw *ds;
	struct zx_dma_dev *d = to_zx_dma(chan->device);
	int lli_limit = LLI_BLOCK_SIZE / sizeof(struct zx_desc_hw);

	if (num > lli_limit) {
		dev_dbg(chan->device->dev, "vch %p: sg num %d exceed max %d\n",
			&c->vc, num, lli_limit);
		return NULL;
	}

	ds = kzalloc(sizeof(*ds), GFP_ATOMIC);
	if (!ds)
		return NULL;

	ds->desc_hw = dma_pool_zalloc(d->pool, GFP_NOWAIT, &ds->desc_hw_lli);
	if (!ds->desc_hw) {
		dev_dbg(chan->device->dev, "vch %p: dma alloc fail\n", &c->vc);
		kfree(ds);
		return NULL;
	}
	ds->desc_num = num;
	return ds;
}

static enum zx_dma_burst_width zx_dma_burst_width(enum dma_slave_buswidth width)
{
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return ffs(width) - 1;
	default:
		return ZX_DMA_WIDTH_32BIT;
	}
}

static int zx_pre_config(struct zx_dma_chan *c, enum dma_transfer_direction dir)
{
	struct dma_slave_config *cfg = &c->slave_cfg;
	enum zx_dma_burst_width src_width;
	enum zx_dma_burst_width dst_width;
	u32 maxburst = 0;

	switch (dir) {
	case DMA_MEM_TO_MEM:
		c->ccfg = ZX_CH_ENABLE | ZX_SOFT_REQ
			| ZX_SRC_BURST_LEN(ZX_MAX_BURST_LEN - 1)
			| ZX_SRC_BURST_WIDTH(ZX_DMA_WIDTH_32BIT)
			| ZX_DST_BURST_WIDTH(ZX_DMA_WIDTH_32BIT);
		break;
	case DMA_MEM_TO_DEV:
		c->dev_addr = cfg->dst_addr;
		/* dst len is calculated from src width, len and dst width.
		 * We need make sure dst len not exceed MAX LEN.
		 * Trailing single transaction that does not fill a full
		 * burst also require identical src/dst data width.
		 */
		dst_width = zx_dma_burst_width(cfg->dst_addr_width);
		maxburst = cfg->dst_maxburst;
		maxburst = maxburst < ZX_MAX_BURST_LEN ?
				maxburst : ZX_MAX_BURST_LEN;
		c->ccfg = ZX_DST_FIFO_MODE | ZX_CH_ENABLE
			| ZX_SRC_BURST_LEN(maxburst - 1)
			| ZX_SRC_BURST_WIDTH(dst_width)
			| ZX_DST_BURST_WIDTH(dst_width);
		break;
	case DMA_DEV_TO_MEM:
		c->dev_addr = cfg->src_addr;
		src_width = zx_dma_burst_width(cfg->src_addr_width);
		maxburst = cfg->src_maxburst;
		maxburst = maxburst < ZX_MAX_BURST_LEN ?
				maxburst : ZX_MAX_BURST_LEN;
		c->ccfg = ZX_SRC_FIFO_MODE | ZX_CH_ENABLE
			| ZX_SRC_BURST_LEN(maxburst - 1)
			| ZX_SRC_BURST_WIDTH(src_width)
			| ZX_DST_BURST_WIDTH(src_width);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct dma_async_tx_descriptor *zx_dma_prep_memcpy(
	struct dma_chan *chan,	dma_addr_t dst, dma_addr_t src,
	size_t len, unsigned long flags)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_desc_sw *ds;
	size_t copy = 0;
	int num = 0;

	if (!len)
		return NULL;

	if (zx_pre_config(c, DMA_MEM_TO_MEM))
		return NULL;

	num = DIV_ROUND_UP(len, DMA_MAX_SIZE);

	ds = zx_alloc_desc_resource(num, chan);
	if (!ds)
		return NULL;

	ds->size = len;
	num = 0;

	do {
		copy = min_t(size_t, len, DMA_MAX_SIZE);
		zx_dma_fill_desc(ds, dst, src, copy, num++, c->ccfg);

		src += copy;
		dst += copy;
		len -= copy;
	} while (len);

	c->cyclic = 0;
	ds->desc_hw[num - 1].lli = 0;	/* end of link */
	ds->desc_hw[num - 1].ctr |= ZX_IRQ_ENABLE_ALL;
	return vchan_tx_prep(&c->vc, &ds->vd, flags);
}

static struct dma_async_tx_descriptor *zx_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned int sglen,
	enum dma_transfer_direction dir, unsigned long flags, void *context)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_desc_sw *ds;
	size_t len, avail, total = 0;
	struct scatterlist *sg;
	dma_addr_t addr, src = 0, dst = 0;
	int num = sglen, i;

	if (!sgl)
		return NULL;

	if (zx_pre_config(c, dir))
		return NULL;

	for_each_sg(sgl, sg, sglen, i) {
		avail = sg_dma_len(sg);
		if (avail > DMA_MAX_SIZE)
			num += DIV_ROUND_UP(avail, DMA_MAX_SIZE) - 1;
	}

	ds = zx_alloc_desc_resource(num, chan);
	if (!ds)
		return NULL;

	c->cyclic = 0;
	num = 0;
	for_each_sg(sgl, sg, sglen, i) {
		addr = sg_dma_address(sg);
		avail = sg_dma_len(sg);
		total += avail;

		do {
			len = min_t(size_t, avail, DMA_MAX_SIZE);

			if (dir == DMA_MEM_TO_DEV) {
				src = addr;
				dst = c->dev_addr;
			} else if (dir == DMA_DEV_TO_MEM) {
				src = c->dev_addr;
				dst = addr;
			}

			zx_dma_fill_desc(ds, dst, src, len, num++, c->ccfg);

			addr += len;
			avail -= len;
		} while (avail);
	}

	ds->desc_hw[num - 1].lli = 0;	/* end of link */
	ds->desc_hw[num - 1].ctr |= ZX_IRQ_ENABLE_ALL;
	ds->size = total;
	return vchan_tx_prep(&c->vc, &ds->vd, flags);
}

static struct dma_async_tx_descriptor *zx_dma_prep_dma_cyclic(
		struct dma_chan *chan, dma_addr_t dma_addr, size_t buf_len,
		size_t period_len, enum dma_transfer_direction dir,
		unsigned long flags)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_desc_sw *ds;
	dma_addr_t src = 0, dst = 0;
	int num_periods = buf_len / period_len;
	int buf = 0, num = 0;

	if (period_len > DMA_MAX_SIZE) {
		dev_err(chan->device->dev, "maximum period size exceeded\n");
		return NULL;
	}

	if (zx_pre_config(c, dir))
		return NULL;

	ds = zx_alloc_desc_resource(num_periods, chan);
	if (!ds)
		return NULL;
	c->cyclic = 1;

	while (buf < buf_len) {
		if (dir == DMA_MEM_TO_DEV) {
			src = dma_addr;
			dst = c->dev_addr;
		} else if (dir == DMA_DEV_TO_MEM) {
			src = c->dev_addr;
			dst = dma_addr;
		}
		zx_dma_fill_desc(ds, dst, src, period_len, num++,
				 c->ccfg | ZX_IRQ_ENABLE_ALL);
		dma_addr += period_len;
		buf += period_len;
	}

	ds->desc_hw[num - 1].lli = ds->desc_hw_lli;
	ds->size = buf_len;
	return vchan_tx_prep(&c->vc, &ds->vd, flags);
}

static int zx_dma_config(struct dma_chan *chan,
			 struct dma_slave_config *cfg)
{
	struct zx_dma_chan *c = to_zx_chan(chan);

	if (!cfg)
		return -EINVAL;

	memcpy(&c->slave_cfg, cfg, sizeof(*cfg));

	return 0;
}

static int zx_dma_terminate_all(struct dma_chan *chan)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	struct zx_dma_dev *d = to_zx_dma(chan->device);
	struct zx_dma_phy *p = c->phy;
	unsigned long flags;
	LIST_HEAD(head);

	dev_dbg(d->slave.dev, "vchan %p: terminate all\n", &c->vc);

	/* Prevent this channel being scheduled */
	spin_lock(&d->lock);
	list_del_init(&c->node);
	spin_unlock(&d->lock);

	/* Clear the tx descriptor lists */
	spin_lock_irqsave(&c->vc.lock, flags);
	vchan_get_all_descriptors(&c->vc, &head);
	if (p) {
		/* vchan is assigned to a pchan - stop the channel */
		zx_dma_terminate_chan(p, d);
		c->phy = NULL;
		p->vchan = NULL;
		p->ds_run = NULL;
		p->ds_done = NULL;
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
	vchan_dma_desc_free_list(&c->vc, &head);

	return 0;
}

static int zx_dma_transfer_pause(struct dma_chan *chan)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	u32 val = 0;

	val = readl_relaxed(c->phy->base + REG_ZX_CTRL);
	val &= ~ZX_CH_ENABLE;
	writel_relaxed(val, c->phy->base + REG_ZX_CTRL);

	return 0;
}

static int zx_dma_transfer_resume(struct dma_chan *chan)
{
	struct zx_dma_chan *c = to_zx_chan(chan);
	u32 val = 0;

	val = readl_relaxed(c->phy->base + REG_ZX_CTRL);
	val |= ZX_CH_ENABLE;
	writel_relaxed(val, c->phy->base + REG_ZX_CTRL);

	return 0;
}

static void zx_dma_free_desc(struct virt_dma_desc *vd)
{
	struct zx_dma_desc_sw *ds =
		container_of(vd, struct zx_dma_desc_sw, vd);
	struct zx_dma_dev *d = to_zx_dma(vd->tx.chan->device);

	dma_pool_free(d->pool, ds->desc_hw, ds->desc_hw_lli);
	kfree(ds);
}

static const struct of_device_id zx6702_dma_dt_ids[] = {
	{ .compatible = "zte,zx296702-dma", },
	{}
};
MODULE_DEVICE_TABLE(of, zx6702_dma_dt_ids);

static struct dma_chan *zx_of_dma_simple_xlate(struct of_phandle_args *dma_spec,
					       struct of_dma *ofdma)
{
	struct zx_dma_dev *d = ofdma->of_dma_data;
	unsigned int request = dma_spec->args[0];
	struct dma_chan *chan;
	struct zx_dma_chan *c;

	if (request >= d->dma_requests)
		return NULL;

	chan = dma_get_any_slave_channel(&d->slave);
	if (!chan) {
		dev_err(d->slave.dev, "get channel fail in %s.\n", __func__);
		return NULL;
	}
	c = to_zx_chan(chan);
	c->id = request;
	dev_info(d->slave.dev, "zx_dma: pchan %u: alloc vchan %p\n",
		 c->id, &c->vc);
	return chan;
}

static int zx_dma_probe(struct platform_device *op)
{
	struct zx_dma_dev *d;
	struct resource *iores;
	int i, ret = 0;

	iores = platform_get_resource(op, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	d = devm_kzalloc(&op->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->base = devm_ioremap_resource(&op->dev, iores);
	if (IS_ERR(d->base))
		return PTR_ERR(d->base);

	of_property_read_u32((&op->dev)->of_node,
			     "dma-channels", &d->dma_channels);
	of_property_read_u32((&op->dev)->of_node,
			     "dma-requests", &d->dma_requests);
	if (!d->dma_requests || !d->dma_channels)
		return -EINVAL;

	d->clk = devm_clk_get(&op->dev, NULL);
	if (IS_ERR(d->clk)) {
		dev_err(&op->dev, "no dma clk\n");
		return PTR_ERR(d->clk);
	}

	d->irq = platform_get_irq(op, 0);
	ret = devm_request_irq(&op->dev, d->irq, zx_dma_int_handler,
			       0, DRIVER_NAME, d);
	if (ret)
		return ret;

	/* A DMA memory pool for LLIs, align on 32-byte boundary */
	d->pool = dmam_pool_create(DRIVER_NAME, &op->dev,
			LLI_BLOCK_SIZE, 32, 0);
	if (!d->pool)
		return -ENOMEM;

	/* init phy channel */
	d->phy = devm_kzalloc(&op->dev,
		d->dma_channels * sizeof(struct zx_dma_phy), GFP_KERNEL);
	if (!d->phy)
		return -ENOMEM;

	for (i = 0; i < d->dma_channels; i++) {
		struct zx_dma_phy *p = &d->phy[i];

		p->idx = i;
		p->base = d->base + i * 0x40;
	}

	INIT_LIST_HEAD(&d->slave.channels);
	dma_cap_set(DMA_SLAVE, d->slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, d->slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, d->slave.cap_mask);
	dma_cap_set(DMA_PRIVATE, d->slave.cap_mask);
	d->slave.dev = &op->dev;
	d->slave.device_free_chan_resources = zx_dma_free_chan_resources;
	d->slave.device_tx_status = zx_dma_tx_status;
	d->slave.device_prep_dma_memcpy = zx_dma_prep_memcpy;
	d->slave.device_prep_slave_sg = zx_dma_prep_slave_sg;
	d->slave.device_prep_dma_cyclic = zx_dma_prep_dma_cyclic;
	d->slave.device_issue_pending = zx_dma_issue_pending;
	d->slave.device_config = zx_dma_config;
	d->slave.device_terminate_all = zx_dma_terminate_all;
	d->slave.device_pause = zx_dma_transfer_pause;
	d->slave.device_resume = zx_dma_transfer_resume;
	d->slave.copy_align = DMA_ALIGN;
	d->slave.src_addr_widths = ZX_DMA_BUSWIDTHS;
	d->slave.dst_addr_widths = ZX_DMA_BUSWIDTHS;
	d->slave.directions = BIT(DMA_MEM_TO_MEM) | BIT(DMA_MEM_TO_DEV)
			| BIT(DMA_DEV_TO_MEM);
	d->slave.residue_granularity = DMA_RESIDUE_GRANULARITY_SEGMENT;

	/* init virtual channel */
	d->chans = devm_kzalloc(&op->dev,
		d->dma_requests * sizeof(struct zx_dma_chan), GFP_KERNEL);
	if (!d->chans)
		return -ENOMEM;

	for (i = 0; i < d->dma_requests; i++) {
		struct zx_dma_chan *c = &d->chans[i];

		c->status = DMA_IN_PROGRESS;
		INIT_LIST_HEAD(&c->node);
		c->vc.desc_free = zx_dma_free_desc;
		vchan_init(&c->vc, &d->slave);
	}

	/* Enable clock before accessing registers */
	ret = clk_prepare_enable(d->clk);
	if (ret < 0) {
		dev_err(&op->dev, "clk_prepare_enable failed: %d\n", ret);
		goto zx_dma_out;
	}

	zx_dma_init_state(d);

	spin_lock_init(&d->lock);
	INIT_LIST_HEAD(&d->chan_pending);
	platform_set_drvdata(op, d);

	ret = dma_async_device_register(&d->slave);
	if (ret)
		goto clk_dis;

	ret = of_dma_controller_register((&op->dev)->of_node,
					 zx_of_dma_simple_xlate, d);
	if (ret)
		goto of_dma_register_fail;

	dev_info(&op->dev, "initialized\n");
	return 0;

of_dma_register_fail:
	dma_async_device_unregister(&d->slave);
clk_dis:
	clk_disable_unprepare(d->clk);
zx_dma_out:
	return ret;
}

static int zx_dma_remove(struct platform_device *op)
{
	struct zx_dma_chan *c, *cn;
	struct zx_dma_dev *d = platform_get_drvdata(op);

	/* explictly free the irq */
	devm_free_irq(&op->dev, d->irq, d);

	dma_async_device_unregister(&d->slave);
	of_dma_controller_free((&op->dev)->of_node);

	list_for_each_entry_safe(c, cn, &d->slave.channels,
				 vc.chan.device_node) {
		list_del(&c->vc.chan.device_node);
	}
	clk_disable_unprepare(d->clk);
	dmam_pool_destroy(d->pool);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int zx_dma_suspend_dev(struct device *dev)
{
	struct zx_dma_dev *d = dev_get_drvdata(dev);
	u32 stat = 0;

	stat = zx_dma_get_chan_stat(d);
	if (stat) {
		dev_warn(d->slave.dev,
			 "chan %d is running fail to suspend\n", stat);
		return -1;
	}
	clk_disable_unprepare(d->clk);
	return 0;
}

static int zx_dma_resume_dev(struct device *dev)
{
	struct zx_dma_dev *d = dev_get_drvdata(dev);
	int ret = 0;

	ret = clk_prepare_enable(d->clk);
	if (ret < 0) {
		dev_err(d->slave.dev, "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}
	zx_dma_init_state(d);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(zx_dma_pmops, zx_dma_suspend_dev, zx_dma_resume_dev);

static struct platform_driver zx_pdma_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.pm	= &zx_dma_pmops,
		.of_match_table = zx6702_dma_dt_ids,
	},
	.probe		= zx_dma_probe,
	.remove		= zx_dma_remove,
};

module_platform_driver(zx_pdma_driver);

MODULE_DESCRIPTION("ZTE ZX296702 DMA Driver");
MODULE_AUTHOR("Jun Nie jun.nie@linaro.org");
MODULE_LICENSE("GPL v2");
