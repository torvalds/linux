/*
 * Copyright (c) 2013 - 2015 Linaro Ltd.
 * Copyright (c) 2013 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/dmaengine.h>
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

#define DRIVER_NAME		"k3-dma"
#define DMA_MAX_SIZE		0x1ffc
#define DMA_CYCLIC_MAX_PERIOD	0x1000
#define LLI_BLOCK_SIZE		(4 * PAGE_SIZE)

#define INT_STAT		0x00
#define INT_TC1			0x04
#define INT_TC2			0x08
#define INT_ERR1		0x0c
#define INT_ERR2		0x10
#define INT_TC1_MASK		0x18
#define INT_TC2_MASK		0x1c
#define INT_ERR1_MASK		0x20
#define INT_ERR2_MASK		0x24
#define INT_TC1_RAW		0x600
#define INT_TC2_RAW		0x608
#define INT_ERR1_RAW		0x610
#define INT_ERR2_RAW		0x618
#define CH_PRI			0x688
#define CH_STAT			0x690
#define CX_CUR_CNT		0x704
#define CX_LLI			0x800
#define CX_CNT1			0x80c
#define CX_CNT0			0x810
#define CX_SRC			0x814
#define CX_DST			0x818
#define CX_CFG			0x81c
#define AXI_CFG			0x820
#define AXI_CFG_DEFAULT		0x201201

#define CX_LLI_CHAIN_EN		0x2
#define CX_CFG_EN		0x1
#define CX_CFG_NODEIRQ		BIT(1)
#define CX_CFG_MEM2PER		(0x1 << 2)
#define CX_CFG_PER2MEM		(0x2 << 2)
#define CX_CFG_SRCINCR		(0x1 << 31)
#define CX_CFG_DSTINCR		(0x1 << 30)

struct k3_desc_hw {
	u32 lli;
	u32 reserved[3];
	u32 count;
	u32 saddr;
	u32 daddr;
	u32 config;
} __aligned(32);

struct k3_dma_desc_sw {
	struct virt_dma_desc	vd;
	dma_addr_t		desc_hw_lli;
	size_t			desc_num;
	size_t			size;
	struct k3_desc_hw	*desc_hw;
};

struct k3_dma_phy;

struct k3_dma_chan {
	u32			ccfg;
	struct virt_dma_chan	vc;
	struct k3_dma_phy	*phy;
	struct list_head	node;
	enum dma_transfer_direction dir;
	dma_addr_t		dev_addr;
	enum dma_status		status;
	bool			cyclic;
};

struct k3_dma_phy {
	u32			idx;
	void __iomem		*base;
	struct k3_dma_chan	*vchan;
	struct k3_dma_desc_sw	*ds_run;
	struct k3_dma_desc_sw	*ds_done;
};

struct k3_dma_dev {
	struct dma_device	slave;
	void __iomem		*base;
	struct tasklet_struct	task;
	spinlock_t		lock;
	struct list_head	chan_pending;
	struct k3_dma_phy	*phy;
	struct k3_dma_chan	*chans;
	struct clk		*clk;
	struct dma_pool		*pool;
	u32			dma_channels;
	u32			dma_requests;
	unsigned int		irq;
};

#define to_k3_dma(dmadev) container_of(dmadev, struct k3_dma_dev, slave)

static struct k3_dma_chan *to_k3_chan(struct dma_chan *chan)
{
	return container_of(chan, struct k3_dma_chan, vc.chan);
}

static void k3_dma_pause_dma(struct k3_dma_phy *phy, bool on)
{
	u32 val = 0;

	if (on) {
		val = readl_relaxed(phy->base + CX_CFG);
		val |= CX_CFG_EN;
		writel_relaxed(val, phy->base + CX_CFG);
	} else {
		val = readl_relaxed(phy->base + CX_CFG);
		val &= ~CX_CFG_EN;
		writel_relaxed(val, phy->base + CX_CFG);
	}
}

static void k3_dma_terminate_chan(struct k3_dma_phy *phy, struct k3_dma_dev *d)
{
	u32 val = 0;

	k3_dma_pause_dma(phy, false);

	val = 0x1 << phy->idx;
	writel_relaxed(val, d->base + INT_TC1_RAW);
	writel_relaxed(val, d->base + INT_TC2_RAW);
	writel_relaxed(val, d->base + INT_ERR1_RAW);
	writel_relaxed(val, d->base + INT_ERR2_RAW);
}

static void k3_dma_set_desc(struct k3_dma_phy *phy, struct k3_desc_hw *hw)
{
	writel_relaxed(hw->lli, phy->base + CX_LLI);
	writel_relaxed(hw->count, phy->base + CX_CNT0);
	writel_relaxed(hw->saddr, phy->base + CX_SRC);
	writel_relaxed(hw->daddr, phy->base + CX_DST);
	writel_relaxed(AXI_CFG_DEFAULT, phy->base + AXI_CFG);
	writel_relaxed(hw->config, phy->base + CX_CFG);
}

static u32 k3_dma_get_curr_cnt(struct k3_dma_dev *d, struct k3_dma_phy *phy)
{
	u32 cnt = 0;

	cnt = readl_relaxed(d->base + CX_CUR_CNT + phy->idx * 0x10);
	cnt &= 0xffff;
	return cnt;
}

static u32 k3_dma_get_curr_lli(struct k3_dma_phy *phy)
{
	return readl_relaxed(phy->base + CX_LLI);
}

static u32 k3_dma_get_chan_stat(struct k3_dma_dev *d)
{
	return readl_relaxed(d->base + CH_STAT);
}

static void k3_dma_enable_dma(struct k3_dma_dev *d, bool on)
{
	if (on) {
		/* set same priority */
		writel_relaxed(0x0, d->base + CH_PRI);

		/* unmask irq */
		writel_relaxed(0xffff, d->base + INT_TC1_MASK);
		writel_relaxed(0xffff, d->base + INT_TC2_MASK);
		writel_relaxed(0xffff, d->base + INT_ERR1_MASK);
		writel_relaxed(0xffff, d->base + INT_ERR2_MASK);
	} else {
		/* mask irq */
		writel_relaxed(0x0, d->base + INT_TC1_MASK);
		writel_relaxed(0x0, d->base + INT_TC2_MASK);
		writel_relaxed(0x0, d->base + INT_ERR1_MASK);
		writel_relaxed(0x0, d->base + INT_ERR2_MASK);
	}
}

static irqreturn_t k3_dma_int_handler(int irq, void *dev_id)
{
	struct k3_dma_dev *d = (struct k3_dma_dev *)dev_id;
	struct k3_dma_phy *p;
	struct k3_dma_chan *c;
	u32 stat = readl_relaxed(d->base + INT_STAT);
	u32 tc1  = readl_relaxed(d->base + INT_TC1);
	u32 tc2  = readl_relaxed(d->base + INT_TC2);
	u32 err1 = readl_relaxed(d->base + INT_ERR1);
	u32 err2 = readl_relaxed(d->base + INT_ERR2);
	u32 i, irq_chan = 0;

	while (stat) {
		i = __ffs(stat);
		stat &= ~BIT(i);
		if (likely(tc1 & BIT(i)) || (tc2 & BIT(i))) {
			unsigned long flags;

			p = &d->phy[i];
			c = p->vchan;
			if (c && (tc1 & BIT(i))) {
				spin_lock_irqsave(&c->vc.lock, flags);
				vchan_cookie_complete(&p->ds_run->vd);
				p->ds_done = p->ds_run;
				p->ds_run = NULL;
				spin_unlock_irqrestore(&c->vc.lock, flags);
			}
			if (c && (tc2 & BIT(i))) {
				spin_lock_irqsave(&c->vc.lock, flags);
				if (p->ds_run != NULL)
					vchan_cyclic_callback(&p->ds_run->vd);
				spin_unlock_irqrestore(&c->vc.lock, flags);
			}
			irq_chan |= BIT(i);
		}
		if (unlikely((err1 & BIT(i)) || (err2 & BIT(i))))
			dev_warn(d->slave.dev, "DMA ERR\n");
	}

	writel_relaxed(irq_chan, d->base + INT_TC1_RAW);
	writel_relaxed(irq_chan, d->base + INT_TC2_RAW);
	writel_relaxed(err1, d->base + INT_ERR1_RAW);
	writel_relaxed(err2, d->base + INT_ERR2_RAW);

	if (irq_chan)
		tasklet_schedule(&d->task);

	if (irq_chan || err1 || err2)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static int k3_dma_start_txd(struct k3_dma_chan *c)
{
	struct k3_dma_dev *d = to_k3_dma(c->vc.chan.device);
	struct virt_dma_desc *vd = vchan_next_desc(&c->vc);

	if (!c->phy)
		return -EAGAIN;

	if (BIT(c->phy->idx) & k3_dma_get_chan_stat(d))
		return -EAGAIN;

	if (vd) {
		struct k3_dma_desc_sw *ds =
			container_of(vd, struct k3_dma_desc_sw, vd);
		/*
		 * fetch and remove request from vc->desc_issued
		 * so vc->desc_issued only contains desc pending
		 */
		list_del(&ds->vd.node);

		c->phy->ds_run = ds;
		c->phy->ds_done = NULL;
		/* start dma */
		k3_dma_set_desc(c->phy, &ds->desc_hw[0]);
		return 0;
	}
	c->phy->ds_run = NULL;
	c->phy->ds_done = NULL;
	return -EAGAIN;
}

static void k3_dma_tasklet(unsigned long arg)
{
	struct k3_dma_dev *d = (struct k3_dma_dev *)arg;
	struct k3_dma_phy *p;
	struct k3_dma_chan *c, *cn;
	unsigned pch, pch_alloc = 0;

	/* check new dma request of running channel in vc->desc_issued */
	list_for_each_entry_safe(c, cn, &d->slave.channels, vc.chan.device_node) {
		spin_lock_irq(&c->vc.lock);
		p = c->phy;
		if (p && p->ds_done) {
			if (k3_dma_start_txd(c)) {
				/* No current txd associated with this channel */
				dev_dbg(d->slave.dev, "pchan %u: free\n", p->idx);
				/* Mark this channel free */
				c->phy = NULL;
				p->vchan = NULL;
			}
		}
		spin_unlock_irq(&c->vc.lock);
	}

	/* check new channel request in d->chan_pending */
	spin_lock_irq(&d->lock);
	for (pch = 0; pch < d->dma_channels; pch++) {
		p = &d->phy[pch];

		if (p->vchan == NULL && !list_empty(&d->chan_pending)) {
			c = list_first_entry(&d->chan_pending,
				struct k3_dma_chan, node);
			/* remove from d->chan_pending */
			list_del_init(&c->node);
			pch_alloc |= 1 << pch;
			/* Mark this channel allocated */
			p->vchan = c;
			c->phy = p;
			dev_dbg(d->slave.dev, "pchan %u: alloc vchan %p\n", pch, &c->vc);
		}
	}
	spin_unlock_irq(&d->lock);

	for (pch = 0; pch < d->dma_channels; pch++) {
		if (pch_alloc & (1 << pch)) {
			p = &d->phy[pch];
			c = p->vchan;
			if (c) {
				spin_lock_irq(&c->vc.lock);
				k3_dma_start_txd(c);
				spin_unlock_irq(&c->vc.lock);
			}
		}
	}
}

static void k3_dma_free_chan_resources(struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	unsigned long flags;

	spin_lock_irqsave(&d->lock, flags);
	list_del_init(&c->node);
	spin_unlock_irqrestore(&d->lock, flags);

	vchan_free_chan_resources(&c->vc);
	c->ccfg = 0;
}

static enum dma_status k3_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *state)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	struct k3_dma_phy *p;
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	size_t bytes = 0;

	ret = dma_cookie_status(&c->vc.chan, cookie, state);
	if (ret == DMA_COMPLETE)
		return ret;

	spin_lock_irqsave(&c->vc.lock, flags);
	p = c->phy;
	ret = c->status;

	/*
	 * If the cookie is on our issue queue, then the residue is
	 * its total size.
	 */
	vd = vchan_find_desc(&c->vc, cookie);
	if (vd && !c->cyclic) {
		bytes = container_of(vd, struct k3_dma_desc_sw, vd)->size;
	} else if ((!p) || (!p->ds_run)) {
		bytes = 0;
	} else {
		struct k3_dma_desc_sw *ds = p->ds_run;
		u32 clli = 0, index = 0;

		bytes = k3_dma_get_curr_cnt(d, p);
		clli = k3_dma_get_curr_lli(p);
		index = ((clli - ds->desc_hw_lli) /
				sizeof(struct k3_desc_hw)) + 1;
		for (; index < ds->desc_num; index++) {
			bytes += ds->desc_hw[index].count;
			/* end of lli */
			if (!ds->desc_hw[index].lli)
				break;
		}
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
	dma_set_residue(state, bytes);
	return ret;
}

static void k3_dma_issue_pending(struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	unsigned long flags;

	spin_lock_irqsave(&c->vc.lock, flags);
	/* add request to vc->desc_issued */
	if (vchan_issue_pending(&c->vc)) {
		spin_lock(&d->lock);
		if (!c->phy) {
			if (list_empty(&c->node)) {
				/* if new channel, add chan_pending */
				list_add_tail(&c->node, &d->chan_pending);
				/* check in tasklet */
				tasklet_schedule(&d->task);
				dev_dbg(d->slave.dev, "vchan %p: issued\n", &c->vc);
			}
		}
		spin_unlock(&d->lock);
	} else
		dev_dbg(d->slave.dev, "vchan %p: nothing to issue\n", &c->vc);
	spin_unlock_irqrestore(&c->vc.lock, flags);
}

static void k3_dma_fill_desc(struct k3_dma_desc_sw *ds, dma_addr_t dst,
			dma_addr_t src, size_t len, u32 num, u32 ccfg)
{
	if (num != ds->desc_num - 1)
		ds->desc_hw[num].lli = ds->desc_hw_lli + (num + 1) *
			sizeof(struct k3_desc_hw);

	ds->desc_hw[num].lli |= CX_LLI_CHAIN_EN;
	ds->desc_hw[num].count = len;
	ds->desc_hw[num].saddr = src;
	ds->desc_hw[num].daddr = dst;
	ds->desc_hw[num].config = ccfg;
}

static struct k3_dma_desc_sw *k3_dma_alloc_desc_resource(int num,
							struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_desc_sw *ds;
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	int lli_limit = LLI_BLOCK_SIZE / sizeof(struct k3_desc_hw);

	if (num > lli_limit) {
		dev_dbg(chan->device->dev, "vch %p: sg num %d exceed max %d\n",
			&c->vc, num, lli_limit);
		return NULL;
	}

	ds = kzalloc(sizeof(*ds), GFP_NOWAIT);
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

static struct dma_async_tx_descriptor *k3_dma_prep_memcpy(
	struct dma_chan *chan,	dma_addr_t dst, dma_addr_t src,
	size_t len, unsigned long flags)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_desc_sw *ds;
	size_t copy = 0;
	int num = 0;

	if (!len)
		return NULL;

	num = DIV_ROUND_UP(len, DMA_MAX_SIZE);

	ds = k3_dma_alloc_desc_resource(num, chan);
	if (!ds)
		return NULL;

	c->cyclic = 0;
	ds->size = len;
	num = 0;

	if (!c->ccfg) {
		/* default is memtomem, without calling device_config */
		c->ccfg = CX_CFG_SRCINCR | CX_CFG_DSTINCR | CX_CFG_EN;
		c->ccfg |= (0xf << 20) | (0xf << 24);	/* burst = 16 */
		c->ccfg |= (0x3 << 12) | (0x3 << 16);	/* width = 64 bit */
	}

	do {
		copy = min_t(size_t, len, DMA_MAX_SIZE);
		k3_dma_fill_desc(ds, dst, src, copy, num++, c->ccfg);

		if (c->dir == DMA_MEM_TO_DEV) {
			src += copy;
		} else if (c->dir == DMA_DEV_TO_MEM) {
			dst += copy;
		} else {
			src += copy;
			dst += copy;
		}
		len -= copy;
	} while (len);

	ds->desc_hw[num-1].lli = 0;	/* end of link */
	return vchan_tx_prep(&c->vc, &ds->vd, flags);
}

static struct dma_async_tx_descriptor *k3_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl, unsigned int sglen,
	enum dma_transfer_direction dir, unsigned long flags, void *context)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_desc_sw *ds;
	size_t len, avail, total = 0;
	struct scatterlist *sg;
	dma_addr_t addr, src = 0, dst = 0;
	int num = sglen, i;

	if (sgl == NULL)
		return NULL;

	c->cyclic = 0;

	for_each_sg(sgl, sg, sglen, i) {
		avail = sg_dma_len(sg);
		if (avail > DMA_MAX_SIZE)
			num += DIV_ROUND_UP(avail, DMA_MAX_SIZE) - 1;
	}

	ds = k3_dma_alloc_desc_resource(num, chan);
	if (!ds)
		return NULL;
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

			k3_dma_fill_desc(ds, dst, src, len, num++, c->ccfg);

			addr += len;
			avail -= len;
		} while (avail);
	}

	ds->desc_hw[num-1].lli = 0;	/* end of link */
	ds->size = total;
	return vchan_tx_prep(&c->vc, &ds->vd, flags);
}

static struct dma_async_tx_descriptor *
k3_dma_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr,
		       size_t buf_len, size_t period_len,
		       enum dma_transfer_direction dir,
		       unsigned long flags)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_desc_sw *ds;
	size_t len, avail, total = 0;
	dma_addr_t addr, src = 0, dst = 0;
	int num = 1, since = 0;
	size_t modulo = DMA_CYCLIC_MAX_PERIOD;
	u32 en_tc2 = 0;

	dev_dbg(chan->device->dev, "%s: buf %pad, dst %pad, buf len %zu, period_len = %zu, dir %d\n",
	       __func__, &buf_addr, &to_k3_chan(chan)->dev_addr,
	       buf_len, period_len, (int)dir);

	avail = buf_len;
	if (avail > modulo)
		num += DIV_ROUND_UP(avail, modulo) - 1;

	ds = k3_dma_alloc_desc_resource(num, chan);
	if (!ds)
		return NULL;

	c->cyclic = 1;
	addr = buf_addr;
	avail = buf_len;
	total = avail;
	num = 0;

	if (period_len < modulo)
		modulo = period_len;

	do {
		len = min_t(size_t, avail, modulo);

		if (dir == DMA_MEM_TO_DEV) {
			src = addr;
			dst = c->dev_addr;
		} else if (dir == DMA_DEV_TO_MEM) {
			src = c->dev_addr;
			dst = addr;
		}
		since += len;
		if (since >= period_len) {
			/* descriptor asks for TC2 interrupt on completion */
			en_tc2 = CX_CFG_NODEIRQ;
			since -= period_len;
		} else
			en_tc2 = 0;

		k3_dma_fill_desc(ds, dst, src, len, num++, c->ccfg | en_tc2);

		addr += len;
		avail -= len;
	} while (avail);

	/* "Cyclic" == end of link points back to start of link */
	ds->desc_hw[num - 1].lli |= ds->desc_hw_lli;

	ds->size = total;

	return vchan_tx_prep(&c->vc, &ds->vd, flags);
}

static int k3_dma_config(struct dma_chan *chan,
			 struct dma_slave_config *cfg)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	u32 maxburst = 0, val = 0;
	enum dma_slave_buswidth width = DMA_SLAVE_BUSWIDTH_UNDEFINED;

	if (cfg == NULL)
		return -EINVAL;
	c->dir = cfg->direction;
	if (c->dir == DMA_DEV_TO_MEM) {
		c->ccfg = CX_CFG_DSTINCR;
		c->dev_addr = cfg->src_addr;
		maxburst = cfg->src_maxburst;
		width = cfg->src_addr_width;
	} else if (c->dir == DMA_MEM_TO_DEV) {
		c->ccfg = CX_CFG_SRCINCR;
		c->dev_addr = cfg->dst_addr;
		maxburst = cfg->dst_maxburst;
		width = cfg->dst_addr_width;
	}
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		val =  __ffs(width);
		break;
	default:
		val = 3;
		break;
	}
	c->ccfg |= (val << 12) | (val << 16);

	if ((maxburst == 0) || (maxburst > 16))
		val = 15;
	else
		val = maxburst - 1;
	c->ccfg |= (val << 20) | (val << 24);
	c->ccfg |= CX_CFG_MEM2PER | CX_CFG_EN;

	/* specific request line */
	c->ccfg |= c->vc.chan.chan_id << 4;

	return 0;
}

static void k3_dma_free_desc(struct virt_dma_desc *vd)
{
	struct k3_dma_desc_sw *ds =
		container_of(vd, struct k3_dma_desc_sw, vd);
	struct k3_dma_dev *d = to_k3_dma(vd->tx.chan->device);

	dma_pool_free(d->pool, ds->desc_hw, ds->desc_hw_lli);
	kfree(ds);
}

static int k3_dma_terminate_all(struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	struct k3_dma_phy *p = c->phy;
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
		k3_dma_terminate_chan(p, d);
		c->phy = NULL;
		p->vchan = NULL;
		if (p->ds_run) {
			vchan_terminate_vdesc(&p->ds_run->vd);
			p->ds_run = NULL;
		}
		p->ds_done = NULL;
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);
	vchan_dma_desc_free_list(&c->vc, &head);

	return 0;
}

static void k3_dma_synchronize(struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);

	vchan_synchronize(&c->vc);
}

static int k3_dma_transfer_pause(struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	struct k3_dma_phy *p = c->phy;

	dev_dbg(d->slave.dev, "vchan %p: pause\n", &c->vc);
	if (c->status == DMA_IN_PROGRESS) {
		c->status = DMA_PAUSED;
		if (p) {
			k3_dma_pause_dma(p, false);
		} else {
			spin_lock(&d->lock);
			list_del_init(&c->node);
			spin_unlock(&d->lock);
		}
	}

	return 0;
}

static int k3_dma_transfer_resume(struct dma_chan *chan)
{
	struct k3_dma_chan *c = to_k3_chan(chan);
	struct k3_dma_dev *d = to_k3_dma(chan->device);
	struct k3_dma_phy *p = c->phy;
	unsigned long flags;

	dev_dbg(d->slave.dev, "vchan %p: resume\n", &c->vc);
	spin_lock_irqsave(&c->vc.lock, flags);
	if (c->status == DMA_PAUSED) {
		c->status = DMA_IN_PROGRESS;
		if (p) {
			k3_dma_pause_dma(p, true);
		} else if (!list_empty(&c->vc.desc_issued)) {
			spin_lock(&d->lock);
			list_add_tail(&c->node, &d->chan_pending);
			spin_unlock(&d->lock);
		}
	}
	spin_unlock_irqrestore(&c->vc.lock, flags);

	return 0;
}

static const struct of_device_id k3_pdma_dt_ids[] = {
	{ .compatible = "hisilicon,k3-dma-1.0", },
	{}
};
MODULE_DEVICE_TABLE(of, k3_pdma_dt_ids);

static struct dma_chan *k3_of_dma_simple_xlate(struct of_phandle_args *dma_spec,
						struct of_dma *ofdma)
{
	struct k3_dma_dev *d = ofdma->of_dma_data;
	unsigned int request = dma_spec->args[0];

	if (request > d->dma_requests)
		return NULL;

	return dma_get_slave_channel(&(d->chans[request].vc.chan));
}

static int k3_dma_probe(struct platform_device *op)
{
	struct k3_dma_dev *d;
	const struct of_device_id *of_id;
	struct resource *iores;
	int i, ret, irq = 0;

	iores = platform_get_resource(op, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	d = devm_kzalloc(&op->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->base = devm_ioremap_resource(&op->dev, iores);
	if (IS_ERR(d->base))
		return PTR_ERR(d->base);

	of_id = of_match_device(k3_pdma_dt_ids, &op->dev);
	if (of_id) {
		of_property_read_u32((&op->dev)->of_node,
				"dma-channels", &d->dma_channels);
		of_property_read_u32((&op->dev)->of_node,
				"dma-requests", &d->dma_requests);
	}

	d->clk = devm_clk_get(&op->dev, NULL);
	if (IS_ERR(d->clk)) {
		dev_err(&op->dev, "no dma clk\n");
		return PTR_ERR(d->clk);
	}

	irq = platform_get_irq(op, 0);
	ret = devm_request_irq(&op->dev, irq,
			k3_dma_int_handler, 0, DRIVER_NAME, d);
	if (ret)
		return ret;

	d->irq = irq;

	/* A DMA memory pool for LLIs, align on 32-byte boundary */
	d->pool = dmam_pool_create(DRIVER_NAME, &op->dev,
					LLI_BLOCK_SIZE, 32, 0);
	if (!d->pool)
		return -ENOMEM;

	/* init phy channel */
	d->phy = devm_kzalloc(&op->dev,
		d->dma_channels * sizeof(struct k3_dma_phy), GFP_KERNEL);
	if (d->phy == NULL)
		return -ENOMEM;

	for (i = 0; i < d->dma_channels; i++) {
		struct k3_dma_phy *p = &d->phy[i];

		p->idx = i;
		p->base = d->base + i * 0x40;
	}

	INIT_LIST_HEAD(&d->slave.channels);
	dma_cap_set(DMA_SLAVE, d->slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, d->slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, d->slave.cap_mask);
	d->slave.dev = &op->dev;
	d->slave.device_free_chan_resources = k3_dma_free_chan_resources;
	d->slave.device_tx_status = k3_dma_tx_status;
	d->slave.device_prep_dma_memcpy = k3_dma_prep_memcpy;
	d->slave.device_prep_slave_sg = k3_dma_prep_slave_sg;
	d->slave.device_prep_dma_cyclic = k3_dma_prep_dma_cyclic;
	d->slave.device_issue_pending = k3_dma_issue_pending;
	d->slave.device_config = k3_dma_config;
	d->slave.device_pause = k3_dma_transfer_pause;
	d->slave.device_resume = k3_dma_transfer_resume;
	d->slave.device_terminate_all = k3_dma_terminate_all;
	d->slave.device_synchronize = k3_dma_synchronize;
	d->slave.copy_align = DMAENGINE_ALIGN_8_BYTES;

	/* init virtual channel */
	d->chans = devm_kzalloc(&op->dev,
		d->dma_requests * sizeof(struct k3_dma_chan), GFP_KERNEL);
	if (d->chans == NULL)
		return -ENOMEM;

	for (i = 0; i < d->dma_requests; i++) {
		struct k3_dma_chan *c = &d->chans[i];

		c->status = DMA_IN_PROGRESS;
		INIT_LIST_HEAD(&c->node);
		c->vc.desc_free = k3_dma_free_desc;
		vchan_init(&c->vc, &d->slave);
	}

	/* Enable clock before accessing registers */
	ret = clk_prepare_enable(d->clk);
	if (ret < 0) {
		dev_err(&op->dev, "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}

	k3_dma_enable_dma(d, true);

	ret = dma_async_device_register(&d->slave);
	if (ret)
		goto dma_async_register_fail;

	ret = of_dma_controller_register((&op->dev)->of_node,
					k3_of_dma_simple_xlate, d);
	if (ret)
		goto of_dma_register_fail;

	spin_lock_init(&d->lock);
	INIT_LIST_HEAD(&d->chan_pending);
	tasklet_init(&d->task, k3_dma_tasklet, (unsigned long)d);
	platform_set_drvdata(op, d);
	dev_info(&op->dev, "initialized\n");

	return 0;

of_dma_register_fail:
	dma_async_device_unregister(&d->slave);
dma_async_register_fail:
	clk_disable_unprepare(d->clk);
	return ret;
}

static int k3_dma_remove(struct platform_device *op)
{
	struct k3_dma_chan *c, *cn;
	struct k3_dma_dev *d = platform_get_drvdata(op);

	dma_async_device_unregister(&d->slave);
	of_dma_controller_free((&op->dev)->of_node);

	devm_free_irq(&op->dev, d->irq, d);

	list_for_each_entry_safe(c, cn, &d->slave.channels, vc.chan.device_node) {
		list_del(&c->vc.chan.device_node);
		tasklet_kill(&c->vc.task);
	}
	tasklet_kill(&d->task);
	clk_disable_unprepare(d->clk);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int k3_dma_suspend_dev(struct device *dev)
{
	struct k3_dma_dev *d = dev_get_drvdata(dev);
	u32 stat = 0;

	stat = k3_dma_get_chan_stat(d);
	if (stat) {
		dev_warn(d->slave.dev,
			"chan %d is running fail to suspend\n", stat);
		return -1;
	}
	k3_dma_enable_dma(d, false);
	clk_disable_unprepare(d->clk);
	return 0;
}

static int k3_dma_resume_dev(struct device *dev)
{
	struct k3_dma_dev *d = dev_get_drvdata(dev);
	int ret = 0;

	ret = clk_prepare_enable(d->clk);
	if (ret < 0) {
		dev_err(d->slave.dev, "clk_prepare_enable failed: %d\n", ret);
		return ret;
	}
	k3_dma_enable_dma(d, true);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(k3_dma_pmops, k3_dma_suspend_dev, k3_dma_resume_dev);

static struct platform_driver k3_pdma_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
		.pm	= &k3_dma_pmops,
		.of_match_table = k3_pdma_dt_ids,
	},
	.probe		= k3_dma_probe,
	.remove		= k3_dma_remove,
};

module_platform_driver(k3_pdma_driver);

MODULE_DESCRIPTION("Hisilicon k3 DMA Driver");
MODULE_ALIAS("platform:k3dma");
MODULE_LICENSE("GPL v2");
