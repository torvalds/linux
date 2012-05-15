/*
 * SA11x0 DMAengine support
 *
 * Copyright (C) 2012 Russell King
 *   Derived in part from arch/arm/mach-sa1100/dma.c,
 *   Copyright (C) 2000, 2001 by Nicolas Pitre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sa11x0-dma.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define NR_PHY_CHAN	6
#define DMA_ALIGN	3
#define DMA_MAX_SIZE	0x1fff
#define DMA_CHUNK_SIZE	0x1000

#define DMA_DDAR	0x00
#define DMA_DCSR_S	0x04
#define DMA_DCSR_C	0x08
#define DMA_DCSR_R	0x0c
#define DMA_DBSA	0x10
#define DMA_DBTA	0x14
#define DMA_DBSB	0x18
#define DMA_DBTB	0x1c
#define DMA_SIZE	0x20

#define DCSR_RUN	(1 << 0)
#define DCSR_IE		(1 << 1)
#define DCSR_ERROR	(1 << 2)
#define DCSR_DONEA	(1 << 3)
#define DCSR_STRTA	(1 << 4)
#define DCSR_DONEB	(1 << 5)
#define DCSR_STRTB	(1 << 6)
#define DCSR_BIU	(1 << 7)

#define DDAR_RW		(1 << 0)	/* 0 = W, 1 = R */
#define DDAR_E		(1 << 1)	/* 0 = LE, 1 = BE */
#define DDAR_BS		(1 << 2)	/* 0 = BS4, 1 = BS8 */
#define DDAR_DW		(1 << 3)	/* 0 = 8b, 1 = 16b */
#define DDAR_Ser0UDCTr	(0x0 << 4)
#define DDAR_Ser0UDCRc	(0x1 << 4)
#define DDAR_Ser1SDLCTr	(0x2 << 4)
#define DDAR_Ser1SDLCRc	(0x3 << 4)
#define DDAR_Ser1UARTTr	(0x4 << 4)
#define DDAR_Ser1UARTRc	(0x5 << 4)
#define DDAR_Ser2ICPTr	(0x6 << 4)
#define DDAR_Ser2ICPRc	(0x7 << 4)
#define DDAR_Ser3UARTTr	(0x8 << 4)
#define DDAR_Ser3UARTRc	(0x9 << 4)
#define DDAR_Ser4MCP0Tr	(0xa << 4)
#define DDAR_Ser4MCP0Rc	(0xb << 4)
#define DDAR_Ser4MCP1Tr	(0xc << 4)
#define DDAR_Ser4MCP1Rc	(0xd << 4)
#define DDAR_Ser4SSPTr	(0xe << 4)
#define DDAR_Ser4SSPRc	(0xf << 4)

struct sa11x0_dma_sg {
	u32			addr;
	u32			len;
};

struct sa11x0_dma_desc {
	struct dma_async_tx_descriptor tx;
	u32			ddar;
	size_t			size;

	/* maybe protected by c->lock */
	struct list_head	node;
	unsigned		sglen;
	struct sa11x0_dma_sg	sg[0];
};

struct sa11x0_dma_phy;

struct sa11x0_dma_chan {
	struct dma_chan		chan;
	spinlock_t		lock;
	dma_cookie_t		lc;

	/* protected by c->lock */
	struct sa11x0_dma_phy	*phy;
	enum dma_status		status;
	struct list_head	desc_submitted;
	struct list_head	desc_issued;

	/* protected by d->lock */
	struct list_head	node;

	u32			ddar;
	const char		*name;
};

struct sa11x0_dma_phy {
	void __iomem		*base;
	struct sa11x0_dma_dev	*dev;
	unsigned		num;

	struct sa11x0_dma_chan	*vchan;

	/* Protected by c->lock */
	unsigned		sg_load;
	struct sa11x0_dma_desc	*txd_load;
	unsigned		sg_done;
	struct sa11x0_dma_desc	*txd_done;
#ifdef CONFIG_PM_SLEEP
	u32			dbs[2];
	u32			dbt[2];
	u32			dcsr;
#endif
};

struct sa11x0_dma_dev {
	struct dma_device	slave;
	void __iomem		*base;
	spinlock_t		lock;
	struct tasklet_struct	task;
	struct list_head	chan_pending;
	struct list_head	desc_complete;
	struct sa11x0_dma_phy	phy[NR_PHY_CHAN];
};

static struct sa11x0_dma_chan *to_sa11x0_dma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct sa11x0_dma_chan, chan);
}

static struct sa11x0_dma_dev *to_sa11x0_dma(struct dma_device *dmadev)
{
	return container_of(dmadev, struct sa11x0_dma_dev, slave);
}

static struct sa11x0_dma_desc *to_sa11x0_dma_tx(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct sa11x0_dma_desc, tx);
}

static struct sa11x0_dma_desc *sa11x0_dma_next_desc(struct sa11x0_dma_chan *c)
{
	if (list_empty(&c->desc_issued))
		return NULL;

	return list_first_entry(&c->desc_issued, struct sa11x0_dma_desc, node);
}

static void sa11x0_dma_start_desc(struct sa11x0_dma_phy *p, struct sa11x0_dma_desc *txd)
{
	list_del(&txd->node);
	p->txd_load = txd;
	p->sg_load = 0;

	dev_vdbg(p->dev->slave.dev, "pchan %u: txd %p[%x]: starting: DDAR:%x\n",
		p->num, txd, txd->tx.cookie, txd->ddar);
}

static void noinline sa11x0_dma_start_sg(struct sa11x0_dma_phy *p,
	struct sa11x0_dma_chan *c)
{
	struct sa11x0_dma_desc *txd = p->txd_load;
	struct sa11x0_dma_sg *sg;
	void __iomem *base = p->base;
	unsigned dbsx, dbtx;
	u32 dcsr;

	if (!txd)
		return;

	dcsr = readl_relaxed(base + DMA_DCSR_R);

	/* Don't try to load the next transfer if both buffers are started */
	if ((dcsr & (DCSR_STRTA | DCSR_STRTB)) == (DCSR_STRTA | DCSR_STRTB))
		return;

	if (p->sg_load == txd->sglen) {
		struct sa11x0_dma_desc *txn = sa11x0_dma_next_desc(c);

		/*
		 * We have reached the end of the current descriptor.
		 * Peek at the next descriptor, and if compatible with
		 * the current, start processing it.
		 */
		if (txn && txn->ddar == txd->ddar) {
			txd = txn;
			sa11x0_dma_start_desc(p, txn);
		} else {
			p->txd_load = NULL;
			return;
		}
	}

	sg = &txd->sg[p->sg_load++];

	/* Select buffer to load according to channel status */
	if (((dcsr & (DCSR_BIU | DCSR_STRTB)) == (DCSR_BIU | DCSR_STRTB)) ||
	    ((dcsr & (DCSR_BIU | DCSR_STRTA)) == 0)) {
		dbsx = DMA_DBSA;
		dbtx = DMA_DBTA;
		dcsr = DCSR_STRTA | DCSR_IE | DCSR_RUN;
	} else {
		dbsx = DMA_DBSB;
		dbtx = DMA_DBTB;
		dcsr = DCSR_STRTB | DCSR_IE | DCSR_RUN;
	}

	writel_relaxed(sg->addr, base + dbsx);
	writel_relaxed(sg->len, base + dbtx);
	writel(dcsr, base + DMA_DCSR_S);

	dev_dbg(p->dev->slave.dev, "pchan %u: load: DCSR:%02x DBS%c:%08x DBT%c:%08x\n",
		p->num, dcsr,
		'A' + (dbsx == DMA_DBSB), sg->addr,
		'A' + (dbtx == DMA_DBTB), sg->len);
}

static void noinline sa11x0_dma_complete(struct sa11x0_dma_phy *p,
	struct sa11x0_dma_chan *c)
{
	struct sa11x0_dma_desc *txd = p->txd_done;

	if (++p->sg_done == txd->sglen) {
		struct sa11x0_dma_dev *d = p->dev;

		dev_vdbg(d->slave.dev, "pchan %u: txd %p[%x]: completed\n",
			p->num, p->txd_done, p->txd_done->tx.cookie);

		c->lc = txd->tx.cookie;

		spin_lock(&d->lock);
		list_add_tail(&txd->node, &d->desc_complete);
		spin_unlock(&d->lock);

		p->sg_done = 0;
		p->txd_done = p->txd_load;

		tasklet_schedule(&d->task);
	}

	sa11x0_dma_start_sg(p, c);
}

static irqreturn_t sa11x0_dma_irq(int irq, void *dev_id)
{
	struct sa11x0_dma_phy *p = dev_id;
	struct sa11x0_dma_dev *d = p->dev;
	struct sa11x0_dma_chan *c;
	u32 dcsr;

	dcsr = readl_relaxed(p->base + DMA_DCSR_R);
	if (!(dcsr & (DCSR_ERROR | DCSR_DONEA | DCSR_DONEB)))
		return IRQ_NONE;

	/* Clear reported status bits */
	writel_relaxed(dcsr & (DCSR_ERROR | DCSR_DONEA | DCSR_DONEB),
		p->base + DMA_DCSR_C);

	dev_dbg(d->slave.dev, "pchan %u: irq: DCSR:%02x\n", p->num, dcsr);

	if (dcsr & DCSR_ERROR) {
		dev_err(d->slave.dev, "pchan %u: error. DCSR:%02x DDAR:%08x DBSA:%08x DBTA:%08x DBSB:%08x DBTB:%08x\n",
			p->num, dcsr,
			readl_relaxed(p->base + DMA_DDAR),
			readl_relaxed(p->base + DMA_DBSA),
			readl_relaxed(p->base + DMA_DBTA),
			readl_relaxed(p->base + DMA_DBSB),
			readl_relaxed(p->base + DMA_DBTB));
	}

	c = p->vchan;
	if (c) {
		unsigned long flags;

		spin_lock_irqsave(&c->lock, flags);
		/*
		 * Now that we're holding the lock, check that the vchan
		 * really is associated with this pchan before touching the
		 * hardware.  This should always succeed, because we won't
		 * change p->vchan or c->phy while the channel is actively
		 * transferring.
		 */
		if (c->phy == p) {
			if (dcsr & DCSR_DONEA)
				sa11x0_dma_complete(p, c);
			if (dcsr & DCSR_DONEB)
				sa11x0_dma_complete(p, c);
		}
		spin_unlock_irqrestore(&c->lock, flags);
	}

	return IRQ_HANDLED;
}

static void sa11x0_dma_start_txd(struct sa11x0_dma_chan *c)
{
	struct sa11x0_dma_desc *txd = sa11x0_dma_next_desc(c);

	/* If the issued list is empty, we have no further txds to process */
	if (txd) {
		struct sa11x0_dma_phy *p = c->phy;

		sa11x0_dma_start_desc(p, txd);
		p->txd_done = txd;
		p->sg_done = 0;

		/* The channel should not have any transfers started */
		WARN_ON(readl_relaxed(p->base + DMA_DCSR_R) &
				      (DCSR_STRTA | DCSR_STRTB));

		/* Clear the run and start bits before changing DDAR */
		writel_relaxed(DCSR_RUN | DCSR_STRTA | DCSR_STRTB,
			       p->base + DMA_DCSR_C);
		writel_relaxed(txd->ddar, p->base + DMA_DDAR);

		/* Try to start both buffers */
		sa11x0_dma_start_sg(p, c);
		sa11x0_dma_start_sg(p, c);
	}
}

static void sa11x0_dma_tasklet(unsigned long arg)
{
	struct sa11x0_dma_dev *d = (struct sa11x0_dma_dev *)arg;
	struct sa11x0_dma_phy *p;
	struct sa11x0_dma_chan *c;
	struct sa11x0_dma_desc *txd, *txn;
	LIST_HEAD(head);
	unsigned pch, pch_alloc = 0;

	dev_dbg(d->slave.dev, "tasklet enter\n");

	/* Get the completed tx descriptors */
	spin_lock_irq(&d->lock);
	list_splice_init(&d->desc_complete, &head);
	spin_unlock_irq(&d->lock);

	list_for_each_entry(txd, &head, node) {
		c = to_sa11x0_dma_chan(txd->tx.chan);

		dev_dbg(d->slave.dev, "vchan %p: txd %p[%x] completed\n",
			c, txd, txd->tx.cookie);

		spin_lock_irq(&c->lock);
		p = c->phy;
		if (p) {
			if (!p->txd_done)
				sa11x0_dma_start_txd(c);
			if (!p->txd_done) {
				/* No current txd associated with this channel */
				dev_dbg(d->slave.dev, "pchan %u: free\n", p->num);

				/* Mark this channel free */
				c->phy = NULL;
				p->vchan = NULL;
			}
		}
		spin_unlock_irq(&c->lock);
	}

	spin_lock_irq(&d->lock);
	for (pch = 0; pch < NR_PHY_CHAN; pch++) {
		p = &d->phy[pch];

		if (p->vchan == NULL && !list_empty(&d->chan_pending)) {
			c = list_first_entry(&d->chan_pending,
				struct sa11x0_dma_chan, node);
			list_del_init(&c->node);

			pch_alloc |= 1 << pch;

			/* Mark this channel allocated */
			p->vchan = c;

			dev_dbg(d->slave.dev, "pchan %u: alloc vchan %p\n", pch, c);
		}
	}
	spin_unlock_irq(&d->lock);

	for (pch = 0; pch < NR_PHY_CHAN; pch++) {
		if (pch_alloc & (1 << pch)) {
			p = &d->phy[pch];
			c = p->vchan;

			spin_lock_irq(&c->lock);
			c->phy = p;

			sa11x0_dma_start_txd(c);
			spin_unlock_irq(&c->lock);
		}
	}

	/* Now free the completed tx descriptor, and call their callbacks */
	list_for_each_entry_safe(txd, txn, &head, node) {
		dma_async_tx_callback callback = txd->tx.callback;
		void *callback_param = txd->tx.callback_param;

		dev_dbg(d->slave.dev, "txd %p[%x]: callback and free\n",
			txd, txd->tx.cookie);

		kfree(txd);

		if (callback)
			callback(callback_param);
	}

	dev_dbg(d->slave.dev, "tasklet exit\n");
}


static void sa11x0_dma_desc_free(struct sa11x0_dma_dev *d, struct list_head *head)
{
	struct sa11x0_dma_desc *txd, *txn;

	list_for_each_entry_safe(txd, txn, head, node) {
		dev_dbg(d->slave.dev, "txd %p: freeing\n", txd);
		kfree(txd);
	}
}

static int sa11x0_dma_alloc_chan_resources(struct dma_chan *chan)
{
	return 0;
}

static void sa11x0_dma_free_chan_resources(struct dma_chan *chan)
{
	struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(chan);
	struct sa11x0_dma_dev *d = to_sa11x0_dma(chan->device);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&c->lock, flags);
	spin_lock(&d->lock);
	list_del_init(&c->node);
	spin_unlock(&d->lock);

	list_splice_tail_init(&c->desc_submitted, &head);
	list_splice_tail_init(&c->desc_issued, &head);
	spin_unlock_irqrestore(&c->lock, flags);

	sa11x0_dma_desc_free(d, &head);
}

static dma_addr_t sa11x0_dma_pos(struct sa11x0_dma_phy *p)
{
	unsigned reg;
	u32 dcsr;

	dcsr = readl_relaxed(p->base + DMA_DCSR_R);

	if ((dcsr & (DCSR_BIU | DCSR_STRTA)) == DCSR_STRTA ||
	    (dcsr & (DCSR_BIU | DCSR_STRTB)) == DCSR_BIU)
		reg = DMA_DBSA;
	else
		reg = DMA_DBSB;

	return readl_relaxed(p->base + reg);
}

static enum dma_status sa11x0_dma_tx_status(struct dma_chan *chan,
	dma_cookie_t cookie, struct dma_tx_state *state)
{
	struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(chan);
	struct sa11x0_dma_dev *d = to_sa11x0_dma(chan->device);
	struct sa11x0_dma_phy *p;
	struct sa11x0_dma_desc *txd;
	dma_cookie_t last_used, last_complete;
	unsigned long flags;
	enum dma_status ret;
	size_t bytes = 0;

	last_used = c->chan.cookie;
	last_complete = c->lc;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS) {
		dma_set_tx_state(state, last_complete, last_used, 0);
		return ret;
	}

	spin_lock_irqsave(&c->lock, flags);
	p = c->phy;
	ret = c->status;
	if (p) {
		dma_addr_t addr = sa11x0_dma_pos(p);

		dev_vdbg(d->slave.dev, "tx_status: addr:%x\n", addr);

		txd = p->txd_done;
		if (txd) {
			unsigned i;

			for (i = 0; i < txd->sglen; i++) {
				dev_vdbg(d->slave.dev, "tx_status: [%u] %x+%x\n",
					i, txd->sg[i].addr, txd->sg[i].len);
				if (addr >= txd->sg[i].addr &&
				    addr < txd->sg[i].addr + txd->sg[i].len) {
					unsigned len;

					len = txd->sg[i].len -
						(addr - txd->sg[i].addr);
					dev_vdbg(d->slave.dev, "tx_status: [%u] +%x\n",
						i, len);
					bytes += len;
					i++;
					break;
				}
			}
			for (; i < txd->sglen; i++) {
				dev_vdbg(d->slave.dev, "tx_status: [%u] %x+%x ++\n",
					i, txd->sg[i].addr, txd->sg[i].len);
				bytes += txd->sg[i].len;
			}
		}
		if (txd != p->txd_load && p->txd_load)
			bytes += p->txd_load->size;
	}
	list_for_each_entry(txd, &c->desc_issued, node) {
		bytes += txd->size;
	}
	spin_unlock_irqrestore(&c->lock, flags);

	dma_set_tx_state(state, last_complete, last_used, bytes);

	dev_vdbg(d->slave.dev, "tx_status: bytes 0x%zx\n", bytes);

	return ret;
}

/*
 * Move pending txds to the issued list, and re-init pending list.
 * If not already pending, add this channel to the list of pending
 * channels and trigger the tasklet to run.
 */
static void sa11x0_dma_issue_pending(struct dma_chan *chan)
{
	struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(chan);
	struct sa11x0_dma_dev *d = to_sa11x0_dma(chan->device);
	unsigned long flags;

	spin_lock_irqsave(&c->lock, flags);
	list_splice_tail_init(&c->desc_submitted, &c->desc_issued);
	if (!list_empty(&c->desc_issued)) {
		spin_lock(&d->lock);
		if (!c->phy && list_empty(&c->node)) {
			list_add_tail(&c->node, &d->chan_pending);
			tasklet_schedule(&d->task);
			dev_dbg(d->slave.dev, "vchan %p: issued\n", c);
		}
		spin_unlock(&d->lock);
	} else
		dev_dbg(d->slave.dev, "vchan %p: nothing to issue\n", c);
	spin_unlock_irqrestore(&c->lock, flags);
}

static dma_cookie_t sa11x0_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(tx->chan);
	struct sa11x0_dma_desc *txd = to_sa11x0_dma_tx(tx);
	unsigned long flags;

	spin_lock_irqsave(&c->lock, flags);
	c->chan.cookie += 1;
	if (c->chan.cookie < 0)
		c->chan.cookie = 1;
	txd->tx.cookie = c->chan.cookie;

	list_add_tail(&txd->node, &c->desc_submitted);
	spin_unlock_irqrestore(&c->lock, flags);

	dev_dbg(tx->chan->device->dev, "vchan %p: txd %p[%x]: submitted\n",
		c, txd, txd->tx.cookie);

	return txd->tx.cookie;
}

static struct dma_async_tx_descriptor *sa11x0_dma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sg, unsigned int sglen,
	enum dma_transfer_direction dir, unsigned long flags)
{
	struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(chan);
	struct sa11x0_dma_desc *txd;
	struct scatterlist *sgent;
	unsigned i, j = sglen;
	size_t size = 0;

	/* SA11x0 channels can only operate in their native direction */
	if (dir != (c->ddar & DDAR_RW ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV)) {
		dev_err(chan->device->dev, "vchan %p: bad DMA direction: DDAR:%08x dir:%u\n",
			c, c->ddar, dir);
		return NULL;
	}

	/* Do not allow zero-sized txds */
	if (sglen == 0)
		return NULL;

	for_each_sg(sg, sgent, sglen, i) {
		dma_addr_t addr = sg_dma_address(sgent);
		unsigned int len = sg_dma_len(sgent);

		if (len > DMA_MAX_SIZE)
			j += DIV_ROUND_UP(len, DMA_MAX_SIZE & ~DMA_ALIGN) - 1;
		if (addr & DMA_ALIGN) {
			dev_dbg(chan->device->dev, "vchan %p: bad buffer alignment: %08x\n",
				c, addr);
			return NULL;
		}
	}

	txd = kzalloc(sizeof(*txd) + j * sizeof(txd->sg[0]), GFP_ATOMIC);
	if (!txd) {
		dev_dbg(chan->device->dev, "vchan %p: kzalloc failed\n", c);
		return NULL;
	}

	j = 0;
	for_each_sg(sg, sgent, sglen, i) {
		dma_addr_t addr = sg_dma_address(sgent);
		unsigned len = sg_dma_len(sgent);

		size += len;

		do {
			unsigned tlen = len;

			/*
			 * Check whether the transfer will fit.  If not, try
			 * to split the transfer up such that we end up with
			 * equal chunks - but make sure that we preserve the
			 * alignment.  This avoids small segments.
			 */
			if (tlen > DMA_MAX_SIZE) {
				unsigned mult = DIV_ROUND_UP(tlen,
					DMA_MAX_SIZE & ~DMA_ALIGN);

				tlen = (tlen / mult) & ~DMA_ALIGN;
			}

			txd->sg[j].addr = addr;
			txd->sg[j].len = tlen;

			addr += tlen;
			len -= tlen;
			j++;
		} while (len);
	}

	dma_async_tx_descriptor_init(&txd->tx, &c->chan);
	txd->tx.flags = flags;
	txd->tx.tx_submit = sa11x0_dma_tx_submit;
	txd->ddar = c->ddar;
	txd->size = size;
	txd->sglen = j;

	dev_dbg(chan->device->dev, "vchan %p: txd %p: size %u nr %u\n",
		c, txd, txd->size, txd->sglen);

	return &txd->tx;
}

static int sa11x0_dma_slave_config(struct sa11x0_dma_chan *c, struct dma_slave_config *cfg)
{
	u32 ddar = c->ddar & ((0xf << 4) | DDAR_RW);
	dma_addr_t addr;
	enum dma_slave_buswidth width;
	u32 maxburst;

	if (ddar & DDAR_RW) {
		addr = cfg->src_addr;
		width = cfg->src_addr_width;
		maxburst = cfg->src_maxburst;
	} else {
		addr = cfg->dst_addr;
		width = cfg->dst_addr_width;
		maxburst = cfg->dst_maxburst;
	}

	if ((width != DMA_SLAVE_BUSWIDTH_1_BYTE &&
	     width != DMA_SLAVE_BUSWIDTH_2_BYTES) ||
	    (maxburst != 4 && maxburst != 8))
		return -EINVAL;

	if (width == DMA_SLAVE_BUSWIDTH_2_BYTES)
		ddar |= DDAR_DW;
	if (maxburst == 8)
		ddar |= DDAR_BS;

	dev_dbg(c->chan.device->dev, "vchan %p: dma_slave_config addr %x width %u burst %u\n",
		c, addr, width, maxburst);

	c->ddar = ddar | (addr & 0xf0000000) | (addr & 0x003ffffc) << 6;

	return 0;
}

static int sa11x0_dma_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
	unsigned long arg)
{
	struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(chan);
	struct sa11x0_dma_dev *d = to_sa11x0_dma(chan->device);
	struct sa11x0_dma_phy *p;
	LIST_HEAD(head);
	unsigned long flags;
	int ret;

	switch (cmd) {
	case DMA_SLAVE_CONFIG:
		return sa11x0_dma_slave_config(c, (struct dma_slave_config *)arg);

	case DMA_TERMINATE_ALL:
		dev_dbg(d->slave.dev, "vchan %p: terminate all\n", c);
		/* Clear the tx descriptor lists */
		spin_lock_irqsave(&c->lock, flags);
		list_splice_tail_init(&c->desc_submitted, &head);
		list_splice_tail_init(&c->desc_issued, &head);

		p = c->phy;
		if (p) {
			struct sa11x0_dma_desc *txd, *txn;

			dev_dbg(d->slave.dev, "pchan %u: terminating\n", p->num);
			/* vchan is assigned to a pchan - stop the channel */
			writel(DCSR_RUN | DCSR_IE |
				DCSR_STRTA | DCSR_DONEA |
				DCSR_STRTB | DCSR_DONEB,
				p->base + DMA_DCSR_C);

			list_for_each_entry_safe(txd, txn, &d->desc_complete, node)
				if (txd->tx.chan == &c->chan)
					list_move(&txd->node, &head);

			if (p->txd_load) {
				if (p->txd_load != p->txd_done)
					list_add_tail(&p->txd_load->node, &head);
				p->txd_load = NULL;
			}
			if (p->txd_done) {
				list_add_tail(&p->txd_done->node, &head);
				p->txd_done = NULL;
			}
			c->phy = NULL;
			spin_lock(&d->lock);
			p->vchan = NULL;
			spin_unlock(&d->lock);
			tasklet_schedule(&d->task);
		}
		spin_unlock_irqrestore(&c->lock, flags);
		sa11x0_dma_desc_free(d, &head);
		ret = 0;
		break;

	case DMA_PAUSE:
		dev_dbg(d->slave.dev, "vchan %p: pause\n", c);
		spin_lock_irqsave(&c->lock, flags);
		if (c->status == DMA_IN_PROGRESS) {
			c->status = DMA_PAUSED;

			p = c->phy;
			if (p) {
				writel(DCSR_RUN | DCSR_IE, p->base + DMA_DCSR_C);
			} else {
				spin_lock(&d->lock);
				list_del_init(&c->node);
				spin_unlock(&d->lock);
			}
		}
		spin_unlock_irqrestore(&c->lock, flags);
		ret = 0;
		break;

	case DMA_RESUME:
		dev_dbg(d->slave.dev, "vchan %p: resume\n", c);
		spin_lock_irqsave(&c->lock, flags);
		if (c->status == DMA_PAUSED) {
			c->status = DMA_IN_PROGRESS;

			p = c->phy;
			if (p) {
				writel(DCSR_RUN | DCSR_IE, p->base + DMA_DCSR_S);
			} else if (!list_empty(&c->desc_issued)) {
				spin_lock(&d->lock);
				list_add_tail(&c->node, &d->chan_pending);
				spin_unlock(&d->lock);
			}
		}
		spin_unlock_irqrestore(&c->lock, flags);
		ret = 0;
		break;

	default:
		ret = -ENXIO;
		break;
	}

	return ret;
}

struct sa11x0_dma_channel_desc {
	u32 ddar;
	const char *name;
};

#define CD(d1, d2) { .ddar = DDAR_##d1 | d2, .name = #d1 }
static const struct sa11x0_dma_channel_desc chan_desc[] = {
	CD(Ser0UDCTr, 0),
	CD(Ser0UDCRc, DDAR_RW),
	CD(Ser1SDLCTr, 0),
	CD(Ser1SDLCRc, DDAR_RW),
	CD(Ser1UARTTr, 0),
	CD(Ser1UARTRc, DDAR_RW),
	CD(Ser2ICPTr, 0),
	CD(Ser2ICPRc, DDAR_RW),
	CD(Ser3UARTTr, 0),
	CD(Ser3UARTRc, DDAR_RW),
	CD(Ser4MCP0Tr, 0),
	CD(Ser4MCP0Rc, DDAR_RW),
	CD(Ser4MCP1Tr, 0),
	CD(Ser4MCP1Rc, DDAR_RW),
	CD(Ser4SSPTr, 0),
	CD(Ser4SSPRc, DDAR_RW),
};

static int __devinit sa11x0_dma_init_dmadev(struct dma_device *dmadev,
	struct device *dev)
{
	unsigned i;

	dmadev->chancnt = ARRAY_SIZE(chan_desc);
	INIT_LIST_HEAD(&dmadev->channels);
	dmadev->dev = dev;
	dmadev->device_alloc_chan_resources = sa11x0_dma_alloc_chan_resources;
	dmadev->device_free_chan_resources = sa11x0_dma_free_chan_resources;
	dmadev->device_control = sa11x0_dma_control;
	dmadev->device_tx_status = sa11x0_dma_tx_status;
	dmadev->device_issue_pending = sa11x0_dma_issue_pending;

	for (i = 0; i < dmadev->chancnt; i++) {
		struct sa11x0_dma_chan *c;

		c = kzalloc(sizeof(*c), GFP_KERNEL);
		if (!c) {
			dev_err(dev, "no memory for channel %u\n", i);
			return -ENOMEM;
		}

		c->chan.device = dmadev;
		c->status = DMA_IN_PROGRESS;
		c->ddar = chan_desc[i].ddar;
		c->name = chan_desc[i].name;
		spin_lock_init(&c->lock);
		INIT_LIST_HEAD(&c->desc_submitted);
		INIT_LIST_HEAD(&c->desc_issued);
		INIT_LIST_HEAD(&c->node);
		list_add_tail(&c->chan.device_node, &dmadev->channels);
	}

	return dma_async_device_register(dmadev);
}

static int sa11x0_dma_request_irq(struct platform_device *pdev, int nr,
	void *data)
{
	int irq = platform_get_irq(pdev, nr);

	if (irq <= 0)
		return -ENXIO;

	return request_irq(irq, sa11x0_dma_irq, 0, dev_name(&pdev->dev), data);
}

static void sa11x0_dma_free_irq(struct platform_device *pdev, int nr,
	void *data)
{
	int irq = platform_get_irq(pdev, nr);
	if (irq > 0)
		free_irq(irq, data);
}

static void sa11x0_dma_free_channels(struct dma_device *dmadev)
{
	struct sa11x0_dma_chan *c, *cn;

	list_for_each_entry_safe(c, cn, &dmadev->channels, chan.device_node) {
		list_del(&c->chan.device_node);
		kfree(c);
	}
}

static int __devinit sa11x0_dma_probe(struct platform_device *pdev)
{
	struct sa11x0_dma_dev *d;
	struct resource *res;
	unsigned i;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	spin_lock_init(&d->lock);
	INIT_LIST_HEAD(&d->chan_pending);
	INIT_LIST_HEAD(&d->desc_complete);

	d->base = ioremap(res->start, resource_size(res));
	if (!d->base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	tasklet_init(&d->task, sa11x0_dma_tasklet, (unsigned long)d);

	for (i = 0; i < NR_PHY_CHAN; i++) {
		struct sa11x0_dma_phy *p = &d->phy[i];

		p->dev = d;
		p->num = i;
		p->base = d->base + i * DMA_SIZE;
		writel_relaxed(DCSR_RUN | DCSR_IE | DCSR_ERROR |
			DCSR_DONEA | DCSR_STRTA | DCSR_DONEB | DCSR_STRTB,
			p->base + DMA_DCSR_C);
		writel_relaxed(0, p->base + DMA_DDAR);

		ret = sa11x0_dma_request_irq(pdev, i, p);
		if (ret) {
			while (i) {
				i--;
				sa11x0_dma_free_irq(pdev, i, &d->phy[i]);
			}
			goto err_irq;
		}
	}

	dma_cap_set(DMA_SLAVE, d->slave.cap_mask);
	d->slave.device_prep_slave_sg = sa11x0_dma_prep_slave_sg;
	ret = sa11x0_dma_init_dmadev(&d->slave, &pdev->dev);
	if (ret) {
		dev_warn(d->slave.dev, "failed to register slave async device: %d\n",
			ret);
		goto err_slave_reg;
	}

	platform_set_drvdata(pdev, d);
	return 0;

 err_slave_reg:
	sa11x0_dma_free_channels(&d->slave);
	for (i = 0; i < NR_PHY_CHAN; i++)
		sa11x0_dma_free_irq(pdev, i, &d->phy[i]);
 err_irq:
	tasklet_kill(&d->task);
	iounmap(d->base);
 err_ioremap:
	kfree(d);
 err_alloc:
	return ret;
}

static int __devexit sa11x0_dma_remove(struct platform_device *pdev)
{
	struct sa11x0_dma_dev *d = platform_get_drvdata(pdev);
	unsigned pch;

	dma_async_device_unregister(&d->slave);

	sa11x0_dma_free_channels(&d->slave);
	for (pch = 0; pch < NR_PHY_CHAN; pch++)
		sa11x0_dma_free_irq(pdev, pch, &d->phy[pch]);
	tasklet_kill(&d->task);
	iounmap(d->base);
	kfree(d);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sa11x0_dma_suspend(struct device *dev)
{
	struct sa11x0_dma_dev *d = dev_get_drvdata(dev);
	unsigned pch;

	for (pch = 0; pch < NR_PHY_CHAN; pch++) {
		struct sa11x0_dma_phy *p = &d->phy[pch];
		u32 dcsr, saved_dcsr;

		dcsr = saved_dcsr = readl_relaxed(p->base + DMA_DCSR_R);
		if (dcsr & DCSR_RUN) {
			writel(DCSR_RUN | DCSR_IE, p->base + DMA_DCSR_C);
			dcsr = readl_relaxed(p->base + DMA_DCSR_R);
		}

		saved_dcsr &= DCSR_RUN | DCSR_IE;
		if (dcsr & DCSR_BIU) {
			p->dbs[0] = readl_relaxed(p->base + DMA_DBSB);
			p->dbt[0] = readl_relaxed(p->base + DMA_DBTB);
			p->dbs[1] = readl_relaxed(p->base + DMA_DBSA);
			p->dbt[1] = readl_relaxed(p->base + DMA_DBTA);
			saved_dcsr |= (dcsr & DCSR_STRTA ? DCSR_STRTB : 0) |
				      (dcsr & DCSR_STRTB ? DCSR_STRTA : 0);
		} else {
			p->dbs[0] = readl_relaxed(p->base + DMA_DBSA);
			p->dbt[0] = readl_relaxed(p->base + DMA_DBTA);
			p->dbs[1] = readl_relaxed(p->base + DMA_DBSB);
			p->dbt[1] = readl_relaxed(p->base + DMA_DBTB);
			saved_dcsr |= dcsr & (DCSR_STRTA | DCSR_STRTB);
		}
		p->dcsr = saved_dcsr;

		writel(DCSR_STRTA | DCSR_STRTB, p->base + DMA_DCSR_C);
	}

	return 0;
}

static int sa11x0_dma_resume(struct device *dev)
{
	struct sa11x0_dma_dev *d = dev_get_drvdata(dev);
	unsigned pch;

	for (pch = 0; pch < NR_PHY_CHAN; pch++) {
		struct sa11x0_dma_phy *p = &d->phy[pch];
		struct sa11x0_dma_desc *txd = NULL;
		u32 dcsr = readl_relaxed(p->base + DMA_DCSR_R);

		WARN_ON(dcsr & (DCSR_BIU | DCSR_STRTA | DCSR_STRTB | DCSR_RUN));

		if (p->txd_done)
			txd = p->txd_done;
		else if (p->txd_load)
			txd = p->txd_load;

		if (!txd)
			continue;

		writel_relaxed(txd->ddar, p->base + DMA_DDAR);

		writel_relaxed(p->dbs[0], p->base + DMA_DBSA);
		writel_relaxed(p->dbt[0], p->base + DMA_DBTA);
		writel_relaxed(p->dbs[1], p->base + DMA_DBSB);
		writel_relaxed(p->dbt[1], p->base + DMA_DBTB);
		writel_relaxed(p->dcsr, p->base + DMA_DCSR_S);
	}

	return 0;
}
#endif

static const struct dev_pm_ops sa11x0_dma_pm_ops = {
	.suspend_noirq = sa11x0_dma_suspend,
	.resume_noirq = sa11x0_dma_resume,
	.freeze_noirq = sa11x0_dma_suspend,
	.thaw_noirq = sa11x0_dma_resume,
	.poweroff_noirq = sa11x0_dma_suspend,
	.restore_noirq = sa11x0_dma_resume,
};

static struct platform_driver sa11x0_dma_driver = {
	.driver = {
		.name	= "sa11x0-dma",
		.owner	= THIS_MODULE,
		.pm	= &sa11x0_dma_pm_ops,
	},
	.probe		= sa11x0_dma_probe,
	.remove		= __devexit_p(sa11x0_dma_remove),
};

bool sa11x0_dma_filter_fn(struct dma_chan *chan, void *param)
{
	if (chan->device->dev->driver == &sa11x0_dma_driver.driver) {
		struct sa11x0_dma_chan *c = to_sa11x0_dma_chan(chan);
		const char *p = param;

		return !strcmp(c->name, p);
	}
	return false;
}
EXPORT_SYMBOL(sa11x0_dma_filter_fn);

static int __init sa11x0_dma_init(void)
{
	return platform_driver_register(&sa11x0_dma_driver);
}
subsys_initcall(sa11x0_dma_init);

static void __exit sa11x0_dma_exit(void)
{
	platform_driver_unregister(&sa11x0_dma_driver);
}
module_exit(sa11x0_dma_exit);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("SA-11x0 DMA driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sa11x0-dma");
