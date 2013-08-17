/*
 * Topcliff PCH DMA controller driver
 * Copyright (c) 2010 Intel Corporation
 * Copyright (C) 2011 LAPIS Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pch_dma.h>

#include "dmaengine.h"

#define DRV_NAME "pch-dma"

#define DMA_CTL0_DISABLE		0x0
#define DMA_CTL0_SG			0x1
#define DMA_CTL0_ONESHOT		0x2
#define DMA_CTL0_MODE_MASK_BITS		0x3
#define DMA_CTL0_DIR_SHIFT_BITS		2
#define DMA_CTL0_BITS_PER_CH		4

#define DMA_CTL2_START_SHIFT_BITS	8
#define DMA_CTL2_IRQ_ENABLE_MASK	((1UL << DMA_CTL2_START_SHIFT_BITS) - 1)

#define DMA_STATUS_IDLE			0x0
#define DMA_STATUS_DESC_READ		0x1
#define DMA_STATUS_WAIT			0x2
#define DMA_STATUS_ACCESS		0x3
#define DMA_STATUS_BITS_PER_CH		2
#define DMA_STATUS_MASK_BITS		0x3
#define DMA_STATUS_SHIFT_BITS		16
#define DMA_STATUS_IRQ(x)		(0x1 << (x))
#define DMA_STATUS0_ERR(x)		(0x1 << ((x) + 8))
#define DMA_STATUS2_ERR(x)		(0x1 << (x))

#define DMA_DESC_WIDTH_SHIFT_BITS	12
#define DMA_DESC_WIDTH_1_BYTE		(0x3 << DMA_DESC_WIDTH_SHIFT_BITS)
#define DMA_DESC_WIDTH_2_BYTES		(0x2 << DMA_DESC_WIDTH_SHIFT_BITS)
#define DMA_DESC_WIDTH_4_BYTES		(0x0 << DMA_DESC_WIDTH_SHIFT_BITS)
#define DMA_DESC_MAX_COUNT_1_BYTE	0x3FF
#define DMA_DESC_MAX_COUNT_2_BYTES	0x3FF
#define DMA_DESC_MAX_COUNT_4_BYTES	0x7FF
#define DMA_DESC_END_WITHOUT_IRQ	0x0
#define DMA_DESC_END_WITH_IRQ		0x1
#define DMA_DESC_FOLLOW_WITHOUT_IRQ	0x2
#define DMA_DESC_FOLLOW_WITH_IRQ	0x3

#define MAX_CHAN_NR			12

#define DMA_MASK_CTL0_MODE	0x33333333
#define DMA_MASK_CTL2_MODE	0x00003333

static unsigned int init_nr_desc_per_channel = 64;
module_param(init_nr_desc_per_channel, uint, 0644);
MODULE_PARM_DESC(init_nr_desc_per_channel,
		 "initial descriptors per channel (default: 64)");

struct pch_dma_desc_regs {
	u32	dev_addr;
	u32	mem_addr;
	u32	size;
	u32	next;
};

struct pch_dma_regs {
	u32	dma_ctl0;
	u32	dma_ctl1;
	u32	dma_ctl2;
	u32	dma_ctl3;
	u32	dma_sts0;
	u32	dma_sts1;
	u32	dma_sts2;
	u32	reserved3;
	struct pch_dma_desc_regs desc[MAX_CHAN_NR];
};

struct pch_dma_desc {
	struct pch_dma_desc_regs regs;
	struct dma_async_tx_descriptor txd;
	struct list_head	desc_node;
	struct list_head	tx_list;
};

struct pch_dma_chan {
	struct dma_chan		chan;
	void __iomem *membase;
	enum dma_transfer_direction dir;
	struct tasklet_struct	tasklet;
	unsigned long		err_status;

	spinlock_t		lock;

	struct list_head	active_list;
	struct list_head	queue;
	struct list_head	free_list;
	unsigned int		descs_allocated;
};

#define PDC_DEV_ADDR	0x00
#define PDC_MEM_ADDR	0x04
#define PDC_SIZE	0x08
#define PDC_NEXT	0x0C

#define channel_readl(pdc, name) \
	readl((pdc)->membase + PDC_##name)
#define channel_writel(pdc, name, val) \
	writel((val), (pdc)->membase + PDC_##name)

struct pch_dma {
	struct dma_device	dma;
	void __iomem *membase;
	struct pci_pool		*pool;
	struct pch_dma_regs	regs;
	struct pch_dma_desc_regs ch_regs[MAX_CHAN_NR];
	struct pch_dma_chan	channels[MAX_CHAN_NR];
};

#define PCH_DMA_CTL0	0x00
#define PCH_DMA_CTL1	0x04
#define PCH_DMA_CTL2	0x08
#define PCH_DMA_CTL3	0x0C
#define PCH_DMA_STS0	0x10
#define PCH_DMA_STS1	0x14
#define PCH_DMA_STS2	0x18

#define dma_readl(pd, name) \
	readl((pd)->membase + PCH_DMA_##name)
#define dma_writel(pd, name, val) \
	writel((val), (pd)->membase + PCH_DMA_##name)

static inline
struct pch_dma_desc *to_pd_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct pch_dma_desc, txd);
}

static inline struct pch_dma_chan *to_pd_chan(struct dma_chan *chan)
{
	return container_of(chan, struct pch_dma_chan, chan);
}

static inline struct pch_dma *to_pd(struct dma_device *ddev)
{
	return container_of(ddev, struct pch_dma, dma);
}

static inline struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static inline struct device *chan2parent(struct dma_chan *chan)
{
	return chan->dev->device.parent;
}

static inline
struct pch_dma_desc *pdc_first_active(struct pch_dma_chan *pd_chan)
{
	return list_first_entry(&pd_chan->active_list,
				struct pch_dma_desc, desc_node);
}

static inline
struct pch_dma_desc *pdc_first_queued(struct pch_dma_chan *pd_chan)
{
	return list_first_entry(&pd_chan->queue,
				struct pch_dma_desc, desc_node);
}

static void pdc_enable_irq(struct dma_chan *chan, int enable)
{
	struct pch_dma *pd = to_pd(chan->device);
	u32 val;
	int pos;

	if (chan->chan_id < 8)
		pos = chan->chan_id;
	else
		pos = chan->chan_id + 8;

	val = dma_readl(pd, CTL2);

	if (enable)
		val |= 0x1 << pos;
	else
		val &= ~(0x1 << pos);

	dma_writel(pd, CTL2, val);

	dev_dbg(chan2dev(chan), "pdc_enable_irq: chan %d -> %x\n",
		chan->chan_id, val);
}

static void pdc_set_dir(struct dma_chan *chan)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);
	struct pch_dma *pd = to_pd(chan->device);
	u32 val;
	u32 mask_mode;
	u32 mask_ctl;

	if (chan->chan_id < 8) {
		val = dma_readl(pd, CTL0);

		mask_mode = DMA_CTL0_MODE_MASK_BITS <<
					(DMA_CTL0_BITS_PER_CH * chan->chan_id);
		mask_ctl = DMA_MASK_CTL0_MODE & ~(DMA_CTL0_MODE_MASK_BITS <<
				       (DMA_CTL0_BITS_PER_CH * chan->chan_id));
		val &= mask_mode;
		if (pd_chan->dir == DMA_MEM_TO_DEV)
			val |= 0x1 << (DMA_CTL0_BITS_PER_CH * chan->chan_id +
				       DMA_CTL0_DIR_SHIFT_BITS);
		else
			val &= ~(0x1 << (DMA_CTL0_BITS_PER_CH * chan->chan_id +
					 DMA_CTL0_DIR_SHIFT_BITS));

		val |= mask_ctl;
		dma_writel(pd, CTL0, val);
	} else {
		int ch = chan->chan_id - 8; /* ch8-->0 ch9-->1 ... ch11->3 */
		val = dma_readl(pd, CTL3);

		mask_mode = DMA_CTL0_MODE_MASK_BITS <<
						(DMA_CTL0_BITS_PER_CH * ch);
		mask_ctl = DMA_MASK_CTL2_MODE & ~(DMA_CTL0_MODE_MASK_BITS <<
						 (DMA_CTL0_BITS_PER_CH * ch));
		val &= mask_mode;
		if (pd_chan->dir == DMA_MEM_TO_DEV)
			val |= 0x1 << (DMA_CTL0_BITS_PER_CH * ch +
				       DMA_CTL0_DIR_SHIFT_BITS);
		else
			val &= ~(0x1 << (DMA_CTL0_BITS_PER_CH * ch +
					 DMA_CTL0_DIR_SHIFT_BITS));
		val |= mask_ctl;
		dma_writel(pd, CTL3, val);
	}

	dev_dbg(chan2dev(chan), "pdc_set_dir: chan %d -> %x\n",
		chan->chan_id, val);
}

static void pdc_set_mode(struct dma_chan *chan, u32 mode)
{
	struct pch_dma *pd = to_pd(chan->device);
	u32 val;
	u32 mask_ctl;
	u32 mask_dir;

	if (chan->chan_id < 8) {
		mask_ctl = DMA_MASK_CTL0_MODE & ~(DMA_CTL0_MODE_MASK_BITS <<
			   (DMA_CTL0_BITS_PER_CH * chan->chan_id));
		mask_dir = 1 << (DMA_CTL0_BITS_PER_CH * chan->chan_id +\
				 DMA_CTL0_DIR_SHIFT_BITS);
		val = dma_readl(pd, CTL0);
		val &= mask_dir;
		val |= mode << (DMA_CTL0_BITS_PER_CH * chan->chan_id);
		val |= mask_ctl;
		dma_writel(pd, CTL0, val);
	} else {
		int ch = chan->chan_id - 8; /* ch8-->0 ch9-->1 ... ch11->3 */
		mask_ctl = DMA_MASK_CTL2_MODE & ~(DMA_CTL0_MODE_MASK_BITS <<
						 (DMA_CTL0_BITS_PER_CH * ch));
		mask_dir = 1 << (DMA_CTL0_BITS_PER_CH * ch +\
				 DMA_CTL0_DIR_SHIFT_BITS);
		val = dma_readl(pd, CTL3);
		val &= mask_dir;
		val |= mode << (DMA_CTL0_BITS_PER_CH * ch);
		val |= mask_ctl;
		dma_writel(pd, CTL3, val);
	}

	dev_dbg(chan2dev(chan), "pdc_set_mode: chan %d -> %x\n",
		chan->chan_id, val);
}

static u32 pdc_get_status0(struct pch_dma_chan *pd_chan)
{
	struct pch_dma *pd = to_pd(pd_chan->chan.device);
	u32 val;

	val = dma_readl(pd, STS0);
	return DMA_STATUS_MASK_BITS & (val >> (DMA_STATUS_SHIFT_BITS +
			DMA_STATUS_BITS_PER_CH * pd_chan->chan.chan_id));
}

static u32 pdc_get_status2(struct pch_dma_chan *pd_chan)
{
	struct pch_dma *pd = to_pd(pd_chan->chan.device);
	u32 val;

	val = dma_readl(pd, STS2);
	return DMA_STATUS_MASK_BITS & (val >> (DMA_STATUS_SHIFT_BITS +
			DMA_STATUS_BITS_PER_CH * (pd_chan->chan.chan_id - 8)));
}

static bool pdc_is_idle(struct pch_dma_chan *pd_chan)
{
	u32 sts;

	if (pd_chan->chan.chan_id < 8)
		sts = pdc_get_status0(pd_chan);
	else
		sts = pdc_get_status2(pd_chan);


	if (sts == DMA_STATUS_IDLE)
		return true;
	else
		return false;
}

static void pdc_dostart(struct pch_dma_chan *pd_chan, struct pch_dma_desc* desc)
{
	if (!pdc_is_idle(pd_chan)) {
		dev_err(chan2dev(&pd_chan->chan),
			"BUG: Attempt to start non-idle channel\n");
		return;
	}

	dev_dbg(chan2dev(&pd_chan->chan), "chan %d -> dev_addr: %x\n",
		pd_chan->chan.chan_id, desc->regs.dev_addr);
	dev_dbg(chan2dev(&pd_chan->chan), "chan %d -> mem_addr: %x\n",
		pd_chan->chan.chan_id, desc->regs.mem_addr);
	dev_dbg(chan2dev(&pd_chan->chan), "chan %d -> size: %x\n",
		pd_chan->chan.chan_id, desc->regs.size);
	dev_dbg(chan2dev(&pd_chan->chan), "chan %d -> next: %x\n",
		pd_chan->chan.chan_id, desc->regs.next);

	if (list_empty(&desc->tx_list)) {
		channel_writel(pd_chan, DEV_ADDR, desc->regs.dev_addr);
		channel_writel(pd_chan, MEM_ADDR, desc->regs.mem_addr);
		channel_writel(pd_chan, SIZE, desc->regs.size);
		channel_writel(pd_chan, NEXT, desc->regs.next);
		pdc_set_mode(&pd_chan->chan, DMA_CTL0_ONESHOT);
	} else {
		channel_writel(pd_chan, NEXT, desc->txd.phys);
		pdc_set_mode(&pd_chan->chan, DMA_CTL0_SG);
	}
}

static void pdc_chain_complete(struct pch_dma_chan *pd_chan,
			       struct pch_dma_desc *desc)
{
	struct dma_async_tx_descriptor *txd = &desc->txd;
	dma_async_tx_callback callback = txd->callback;
	void *param = txd->callback_param;

	list_splice_init(&desc->tx_list, &pd_chan->free_list);
	list_move(&desc->desc_node, &pd_chan->free_list);

	if (callback)
		callback(param);
}

static void pdc_complete_all(struct pch_dma_chan *pd_chan)
{
	struct pch_dma_desc *desc, *_d;
	LIST_HEAD(list);

	BUG_ON(!pdc_is_idle(pd_chan));

	if (!list_empty(&pd_chan->queue))
		pdc_dostart(pd_chan, pdc_first_queued(pd_chan));

	list_splice_init(&pd_chan->active_list, &list);
	list_splice_init(&pd_chan->queue, &pd_chan->active_list);

	list_for_each_entry_safe(desc, _d, &list, desc_node)
		pdc_chain_complete(pd_chan, desc);
}

static void pdc_handle_error(struct pch_dma_chan *pd_chan)
{
	struct pch_dma_desc *bad_desc;

	bad_desc = pdc_first_active(pd_chan);
	list_del(&bad_desc->desc_node);

	list_splice_init(&pd_chan->queue, pd_chan->active_list.prev);

	if (!list_empty(&pd_chan->active_list))
		pdc_dostart(pd_chan, pdc_first_active(pd_chan));

	dev_crit(chan2dev(&pd_chan->chan), "Bad descriptor submitted\n");
	dev_crit(chan2dev(&pd_chan->chan), "descriptor cookie: %d\n",
		 bad_desc->txd.cookie);

	pdc_chain_complete(pd_chan, bad_desc);
}

static void pdc_advance_work(struct pch_dma_chan *pd_chan)
{
	if (list_empty(&pd_chan->active_list) ||
		list_is_singular(&pd_chan->active_list)) {
		pdc_complete_all(pd_chan);
	} else {
		pdc_chain_complete(pd_chan, pdc_first_active(pd_chan));
		pdc_dostart(pd_chan, pdc_first_active(pd_chan));
	}
}

static dma_cookie_t pd_tx_submit(struct dma_async_tx_descriptor *txd)
{
	struct pch_dma_desc *desc = to_pd_desc(txd);
	struct pch_dma_chan *pd_chan = to_pd_chan(txd->chan);
	dma_cookie_t cookie;

	spin_lock(&pd_chan->lock);
	cookie = dma_cookie_assign(txd);

	if (list_empty(&pd_chan->active_list)) {
		list_add_tail(&desc->desc_node, &pd_chan->active_list);
		pdc_dostart(pd_chan, desc);
	} else {
		list_add_tail(&desc->desc_node, &pd_chan->queue);
	}

	spin_unlock(&pd_chan->lock);
	return 0;
}

static struct pch_dma_desc *pdc_alloc_desc(struct dma_chan *chan, gfp_t flags)
{
	struct pch_dma_desc *desc = NULL;
	struct pch_dma *pd = to_pd(chan->device);
	dma_addr_t addr;

	desc = pci_pool_alloc(pd->pool, flags, &addr);
	if (desc) {
		memset(desc, 0, sizeof(struct pch_dma_desc));
		INIT_LIST_HEAD(&desc->tx_list);
		dma_async_tx_descriptor_init(&desc->txd, chan);
		desc->txd.tx_submit = pd_tx_submit;
		desc->txd.flags = DMA_CTRL_ACK;
		desc->txd.phys = addr;
	}

	return desc;
}

static struct pch_dma_desc *pdc_desc_get(struct pch_dma_chan *pd_chan)
{
	struct pch_dma_desc *desc, *_d;
	struct pch_dma_desc *ret = NULL;
	int i = 0;

	spin_lock(&pd_chan->lock);
	list_for_each_entry_safe(desc, _d, &pd_chan->free_list, desc_node) {
		i++;
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(chan2dev(&pd_chan->chan), "desc %p not ACKed\n", desc);
	}
	spin_unlock(&pd_chan->lock);
	dev_dbg(chan2dev(&pd_chan->chan), "scanned %d descriptors\n", i);

	if (!ret) {
		ret = pdc_alloc_desc(&pd_chan->chan, GFP_NOIO);
		if (ret) {
			spin_lock(&pd_chan->lock);
			pd_chan->descs_allocated++;
			spin_unlock(&pd_chan->lock);
		} else {
			dev_err(chan2dev(&pd_chan->chan),
				"failed to alloc desc\n");
		}
	}

	return ret;
}

static void pdc_desc_put(struct pch_dma_chan *pd_chan,
			 struct pch_dma_desc *desc)
{
	if (desc) {
		spin_lock(&pd_chan->lock);
		list_splice_init(&desc->tx_list, &pd_chan->free_list);
		list_add(&desc->desc_node, &pd_chan->free_list);
		spin_unlock(&pd_chan->lock);
	}
}

static int pd_alloc_chan_resources(struct dma_chan *chan)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);
	struct pch_dma_desc *desc;
	LIST_HEAD(tmp_list);
	int i;

	if (!pdc_is_idle(pd_chan)) {
		dev_dbg(chan2dev(chan), "DMA channel not idle ?\n");
		return -EIO;
	}

	if (!list_empty(&pd_chan->free_list))
		return pd_chan->descs_allocated;

	for (i = 0; i < init_nr_desc_per_channel; i++) {
		desc = pdc_alloc_desc(chan, GFP_KERNEL);

		if (!desc) {
			dev_warn(chan2dev(chan),
				"Only allocated %d initial descriptors\n", i);
			break;
		}

		list_add_tail(&desc->desc_node, &tmp_list);
	}

	spin_lock_irq(&pd_chan->lock);
	list_splice(&tmp_list, &pd_chan->free_list);
	pd_chan->descs_allocated = i;
	dma_cookie_init(chan);
	spin_unlock_irq(&pd_chan->lock);

	pdc_enable_irq(chan, 1);

	return pd_chan->descs_allocated;
}

static void pd_free_chan_resources(struct dma_chan *chan)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);
	struct pch_dma *pd = to_pd(chan->device);
	struct pch_dma_desc *desc, *_d;
	LIST_HEAD(tmp_list);

	BUG_ON(!pdc_is_idle(pd_chan));
	BUG_ON(!list_empty(&pd_chan->active_list));
	BUG_ON(!list_empty(&pd_chan->queue));

	spin_lock_irq(&pd_chan->lock);
	list_splice_init(&pd_chan->free_list, &tmp_list);
	pd_chan->descs_allocated = 0;
	spin_unlock_irq(&pd_chan->lock);

	list_for_each_entry_safe(desc, _d, &tmp_list, desc_node)
		pci_pool_free(pd->pool, desc, desc->txd.phys);

	pdc_enable_irq(chan, 0);
}

static enum dma_status pd_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
				    struct dma_tx_state *txstate)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);
	enum dma_status ret;

	spin_lock_irq(&pd_chan->lock);
	ret = dma_cookie_status(chan, cookie, txstate);
	spin_unlock_irq(&pd_chan->lock);

	return ret;
}

static void pd_issue_pending(struct dma_chan *chan)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);

	if (pdc_is_idle(pd_chan)) {
		spin_lock(&pd_chan->lock);
		pdc_advance_work(pd_chan);
		spin_unlock(&pd_chan->lock);
	}
}

static struct dma_async_tx_descriptor *pd_prep_slave_sg(struct dma_chan *chan,
			struct scatterlist *sgl, unsigned int sg_len,
			enum dma_transfer_direction direction, unsigned long flags,
			void *context)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);
	struct pch_dma_slave *pd_slave = chan->private;
	struct pch_dma_desc *first = NULL;
	struct pch_dma_desc *prev = NULL;
	struct pch_dma_desc *desc = NULL;
	struct scatterlist *sg;
	dma_addr_t reg;
	int i;

	if (unlikely(!sg_len)) {
		dev_info(chan2dev(chan), "prep_slave_sg: length is zero!\n");
		return NULL;
	}

	if (direction == DMA_DEV_TO_MEM)
		reg = pd_slave->rx_reg;
	else if (direction == DMA_MEM_TO_DEV)
		reg = pd_slave->tx_reg;
	else
		return NULL;

	pd_chan->dir = direction;
	pdc_set_dir(chan);

	for_each_sg(sgl, sg, sg_len, i) {
		desc = pdc_desc_get(pd_chan);

		if (!desc)
			goto err_desc_get;

		desc->regs.dev_addr = reg;
		desc->regs.mem_addr = sg_phys(sg);
		desc->regs.size = sg_dma_len(sg);
		desc->regs.next = DMA_DESC_FOLLOW_WITHOUT_IRQ;

		switch (pd_slave->width) {
		case PCH_DMA_WIDTH_1_BYTE:
			if (desc->regs.size > DMA_DESC_MAX_COUNT_1_BYTE)
				goto err_desc_get;
			desc->regs.size |= DMA_DESC_WIDTH_1_BYTE;
			break;
		case PCH_DMA_WIDTH_2_BYTES:
			if (desc->regs.size > DMA_DESC_MAX_COUNT_2_BYTES)
				goto err_desc_get;
			desc->regs.size |= DMA_DESC_WIDTH_2_BYTES;
			break;
		case PCH_DMA_WIDTH_4_BYTES:
			if (desc->regs.size > DMA_DESC_MAX_COUNT_4_BYTES)
				goto err_desc_get;
			desc->regs.size |= DMA_DESC_WIDTH_4_BYTES;
			break;
		default:
			goto err_desc_get;
		}

		if (!first) {
			first = desc;
		} else {
			prev->regs.next |= desc->txd.phys;
			list_add_tail(&desc->desc_node, &first->tx_list);
		}

		prev = desc;
	}

	if (flags & DMA_PREP_INTERRUPT)
		desc->regs.next = DMA_DESC_END_WITH_IRQ;
	else
		desc->regs.next = DMA_DESC_END_WITHOUT_IRQ;

	first->txd.cookie = -EBUSY;
	desc->txd.flags = flags;

	return &first->txd;

err_desc_get:
	dev_err(chan2dev(chan), "failed to get desc or wrong parameters\n");
	pdc_desc_put(pd_chan, first);
	return NULL;
}

static int pd_device_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			     unsigned long arg)
{
	struct pch_dma_chan *pd_chan = to_pd_chan(chan);
	struct pch_dma_desc *desc, *_d;
	LIST_HEAD(list);

	if (cmd != DMA_TERMINATE_ALL)
		return -ENXIO;

	spin_lock_irq(&pd_chan->lock);

	pdc_set_mode(&pd_chan->chan, DMA_CTL0_DISABLE);

	list_splice_init(&pd_chan->active_list, &list);
	list_splice_init(&pd_chan->queue, &list);

	list_for_each_entry_safe(desc, _d, &list, desc_node)
		pdc_chain_complete(pd_chan, desc);

	spin_unlock_irq(&pd_chan->lock);

	return 0;
}

static void pdc_tasklet(unsigned long data)
{
	struct pch_dma_chan *pd_chan = (struct pch_dma_chan *)data;
	unsigned long flags;

	if (!pdc_is_idle(pd_chan)) {
		dev_err(chan2dev(&pd_chan->chan),
			"BUG: handle non-idle channel in tasklet\n");
		return;
	}

	spin_lock_irqsave(&pd_chan->lock, flags);
	if (test_and_clear_bit(0, &pd_chan->err_status))
		pdc_handle_error(pd_chan);
	else
		pdc_advance_work(pd_chan);
	spin_unlock_irqrestore(&pd_chan->lock, flags);
}

static irqreturn_t pd_irq(int irq, void *devid)
{
	struct pch_dma *pd = (struct pch_dma *)devid;
	struct pch_dma_chan *pd_chan;
	u32 sts0;
	u32 sts2;
	int i;
	int ret0 = IRQ_NONE;
	int ret2 = IRQ_NONE;

	sts0 = dma_readl(pd, STS0);
	sts2 = dma_readl(pd, STS2);

	dev_dbg(pd->dma.dev, "pd_irq sts0: %x\n", sts0);

	for (i = 0; i < pd->dma.chancnt; i++) {
		pd_chan = &pd->channels[i];

		if (i < 8) {
			if (sts0 & DMA_STATUS_IRQ(i)) {
				if (sts0 & DMA_STATUS0_ERR(i))
					set_bit(0, &pd_chan->err_status);

				tasklet_schedule(&pd_chan->tasklet);
				ret0 = IRQ_HANDLED;
			}
		} else {
			if (sts2 & DMA_STATUS_IRQ(i - 8)) {
				if (sts2 & DMA_STATUS2_ERR(i))
					set_bit(0, &pd_chan->err_status);

				tasklet_schedule(&pd_chan->tasklet);
				ret2 = IRQ_HANDLED;
			}
		}
	}

	/* clear interrupt bits in status register */
	if (ret0)
		dma_writel(pd, STS0, sts0);
	if (ret2)
		dma_writel(pd, STS2, sts2);

	return ret0 | ret2;
}

#ifdef	CONFIG_PM
static void pch_dma_save_regs(struct pch_dma *pd)
{
	struct pch_dma_chan *pd_chan;
	struct dma_chan *chan, *_c;
	int i = 0;

	pd->regs.dma_ctl0 = dma_readl(pd, CTL0);
	pd->regs.dma_ctl1 = dma_readl(pd, CTL1);
	pd->regs.dma_ctl2 = dma_readl(pd, CTL2);
	pd->regs.dma_ctl3 = dma_readl(pd, CTL3);

	list_for_each_entry_safe(chan, _c, &pd->dma.channels, device_node) {
		pd_chan = to_pd_chan(chan);

		pd->ch_regs[i].dev_addr = channel_readl(pd_chan, DEV_ADDR);
		pd->ch_regs[i].mem_addr = channel_readl(pd_chan, MEM_ADDR);
		pd->ch_regs[i].size = channel_readl(pd_chan, SIZE);
		pd->ch_regs[i].next = channel_readl(pd_chan, NEXT);

		i++;
	}
}

static void pch_dma_restore_regs(struct pch_dma *pd)
{
	struct pch_dma_chan *pd_chan;
	struct dma_chan *chan, *_c;
	int i = 0;

	dma_writel(pd, CTL0, pd->regs.dma_ctl0);
	dma_writel(pd, CTL1, pd->regs.dma_ctl1);
	dma_writel(pd, CTL2, pd->regs.dma_ctl2);
	dma_writel(pd, CTL3, pd->regs.dma_ctl3);

	list_for_each_entry_safe(chan, _c, &pd->dma.channels, device_node) {
		pd_chan = to_pd_chan(chan);

		channel_writel(pd_chan, DEV_ADDR, pd->ch_regs[i].dev_addr);
		channel_writel(pd_chan, MEM_ADDR, pd->ch_regs[i].mem_addr);
		channel_writel(pd_chan, SIZE, pd->ch_regs[i].size);
		channel_writel(pd_chan, NEXT, pd->ch_regs[i].next);

		i++;
	}
}

static int pch_dma_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct pch_dma *pd = pci_get_drvdata(pdev);

	if (pd)
		pch_dma_save_regs(pd);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int pch_dma_resume(struct pci_dev *pdev)
{
	struct pch_dma *pd = pci_get_drvdata(pdev);
	int err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	err = pci_enable_device(pdev);
	if (err) {
		dev_dbg(&pdev->dev, "failed to enable device\n");
		return err;
	}

	if (pd)
		pch_dma_restore_regs(pd);

	return 0;
}
#endif

static int __devinit pch_dma_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct pch_dma *pd;
	struct pch_dma_regs *regs;
	unsigned int nr_channels;
	int err;
	int i;

	nr_channels = id->driver_data;
	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pci_set_drvdata(pdev, pd);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device\n");
		goto err_free_mem;
	}

	if (!(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "Cannot find proper base address\n");
		goto err_disable_pdev;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "Cannot set proper DMA config\n");
		goto err_free_res;
	}

	regs = pd->membase = pci_iomap(pdev, 1, 0);
	if (!pd->membase) {
		dev_err(&pdev->dev, "Cannot map MMIO registers\n");
		err = -ENOMEM;
		goto err_free_res;
	}

	pci_set_master(pdev);

	err = request_irq(pdev->irq, pd_irq, IRQF_SHARED, DRV_NAME, pd);
	if (err) {
		dev_err(&pdev->dev, "Failed to request IRQ\n");
		goto err_iounmap;
	}

	pd->pool = pci_pool_create("pch_dma_desc_pool", pdev,
				   sizeof(struct pch_dma_desc), 4, 0);
	if (!pd->pool) {
		dev_err(&pdev->dev, "Failed to alloc DMA descriptors\n");
		err = -ENOMEM;
		goto err_free_irq;
	}

	pd->dma.dev = &pdev->dev;

	INIT_LIST_HEAD(&pd->dma.channels);

	for (i = 0; i < nr_channels; i++) {
		struct pch_dma_chan *pd_chan = &pd->channels[i];

		pd_chan->chan.device = &pd->dma;
		dma_cookie_init(&pd_chan->chan);

		pd_chan->membase = &regs->desc[i];

		spin_lock_init(&pd_chan->lock);

		INIT_LIST_HEAD(&pd_chan->active_list);
		INIT_LIST_HEAD(&pd_chan->queue);
		INIT_LIST_HEAD(&pd_chan->free_list);

		tasklet_init(&pd_chan->tasklet, pdc_tasklet,
			     (unsigned long)pd_chan);
		list_add_tail(&pd_chan->chan.device_node, &pd->dma.channels);
	}

	dma_cap_zero(pd->dma.cap_mask);
	dma_cap_set(DMA_PRIVATE, pd->dma.cap_mask);
	dma_cap_set(DMA_SLAVE, pd->dma.cap_mask);

	pd->dma.device_alloc_chan_resources = pd_alloc_chan_resources;
	pd->dma.device_free_chan_resources = pd_free_chan_resources;
	pd->dma.device_tx_status = pd_tx_status;
	pd->dma.device_issue_pending = pd_issue_pending;
	pd->dma.device_prep_slave_sg = pd_prep_slave_sg;
	pd->dma.device_control = pd_device_control;

	err = dma_async_device_register(&pd->dma);
	if (err) {
		dev_err(&pdev->dev, "Failed to register DMA device\n");
		goto err_free_pool;
	}

	return 0;

err_free_pool:
	pci_pool_destroy(pd->pool);
err_free_irq:
	free_irq(pdev->irq, pd);
err_iounmap:
	pci_iounmap(pdev, pd->membase);
err_free_res:
	pci_release_regions(pdev);
err_disable_pdev:
	pci_disable_device(pdev);
err_free_mem:
	return err;
}

static void __devexit pch_dma_remove(struct pci_dev *pdev)
{
	struct pch_dma *pd = pci_get_drvdata(pdev);
	struct pch_dma_chan *pd_chan;
	struct dma_chan *chan, *_c;

	if (pd) {
		dma_async_device_unregister(&pd->dma);

		list_for_each_entry_safe(chan, _c, &pd->dma.channels,
					 device_node) {
			pd_chan = to_pd_chan(chan);

			tasklet_disable(&pd_chan->tasklet);
			tasklet_kill(&pd_chan->tasklet);
		}

		pci_pool_destroy(pd->pool);
		free_irq(pdev->irq, pd);
		pci_iounmap(pdev, pd->membase);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		kfree(pd);
	}
}

/* PCI Device ID of DMA device */
#define PCI_VENDOR_ID_ROHM             0x10DB
#define PCI_DEVICE_ID_EG20T_PCH_DMA_8CH        0x8810
#define PCI_DEVICE_ID_EG20T_PCH_DMA_4CH        0x8815
#define PCI_DEVICE_ID_ML7213_DMA1_8CH	0x8026
#define PCI_DEVICE_ID_ML7213_DMA2_8CH	0x802B
#define PCI_DEVICE_ID_ML7213_DMA3_4CH	0x8034
#define PCI_DEVICE_ID_ML7213_DMA4_12CH	0x8032
#define PCI_DEVICE_ID_ML7223_DMA1_4CH	0x800B
#define PCI_DEVICE_ID_ML7223_DMA2_4CH	0x800E
#define PCI_DEVICE_ID_ML7223_DMA3_4CH	0x8017
#define PCI_DEVICE_ID_ML7223_DMA4_4CH	0x803B
#define PCI_DEVICE_ID_ML7831_DMA1_8CH	0x8810
#define PCI_DEVICE_ID_ML7831_DMA2_4CH	0x8815

DEFINE_PCI_DEVICE_TABLE(pch_dma_id_table) = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_EG20T_PCH_DMA_8CH), 8 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_EG20T_PCH_DMA_4CH), 4 },
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7213_DMA1_8CH), 8}, /* UART Video */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7213_DMA2_8CH), 8}, /* PCMIF SPI */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7213_DMA3_4CH), 4}, /* FPGA */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7213_DMA4_12CH), 12}, /* I2S */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7223_DMA1_4CH), 4}, /* UART */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7223_DMA2_4CH), 4}, /* Video SPI */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7223_DMA3_4CH), 4}, /* Security */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7223_DMA4_4CH), 4}, /* FPGA */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7831_DMA1_8CH), 8}, /* UART */
	{ PCI_VDEVICE(ROHM, PCI_DEVICE_ID_ML7831_DMA2_4CH), 4}, /* SPI */
	{ 0, },
};

static struct pci_driver pch_dma_driver = {
	.name		= DRV_NAME,
	.id_table	= pch_dma_id_table,
	.probe		= pch_dma_probe,
	.remove		= __devexit_p(pch_dma_remove),
#ifdef CONFIG_PM
	.suspend	= pch_dma_suspend,
	.resume		= pch_dma_resume,
#endif
};

static int __init pch_dma_init(void)
{
	return pci_register_driver(&pch_dma_driver);
}

static void __exit pch_dma_exit(void)
{
	pci_unregister_driver(&pch_dma_driver);
}

module_init(pch_dma_init);
module_exit(pch_dma_exit);

MODULE_DESCRIPTION("Intel EG20T PCH / LAPIS Semicon ML7213/ML7223/ML7831 IOH "
		   "DMA controller driver");
MODULE_AUTHOR("Yong Wang <yong.y.wang@intel.com>");
MODULE_LICENSE("GPL v2");
