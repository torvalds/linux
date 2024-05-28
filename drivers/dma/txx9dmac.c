// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the TXx9 SoC DMA Controller
 *
 * Copyright (C) 2009 Atsushi Nemoto
 */
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>

#include "dmaengine.h"
#include "txx9dmac.h"

static struct txx9dmac_chan *to_txx9dmac_chan(struct dma_chan *chan)
{
	return container_of(chan, struct txx9dmac_chan, chan);
}

static struct txx9dmac_cregs __iomem *__dma_regs(const struct txx9dmac_chan *dc)
{
	return dc->ch_regs;
}

static struct txx9dmac_cregs32 __iomem *__dma_regs32(
	const struct txx9dmac_chan *dc)
{
	return dc->ch_regs;
}

#define channel64_readq(dc, name) \
	__raw_readq(&(__dma_regs(dc)->name))
#define channel64_writeq(dc, name, val) \
	__raw_writeq((val), &(__dma_regs(dc)->name))
#define channel64_readl(dc, name) \
	__raw_readl(&(__dma_regs(dc)->name))
#define channel64_writel(dc, name, val) \
	__raw_writel((val), &(__dma_regs(dc)->name))

#define channel32_readl(dc, name) \
	__raw_readl(&(__dma_regs32(dc)->name))
#define channel32_writel(dc, name, val) \
	__raw_writel((val), &(__dma_regs32(dc)->name))

#define channel_readq(dc, name) channel64_readq(dc, name)
#define channel_writeq(dc, name, val) channel64_writeq(dc, name, val)
#define channel_readl(dc, name) \
	(is_dmac64(dc) ? \
	 channel64_readl(dc, name) : channel32_readl(dc, name))
#define channel_writel(dc, name, val) \
	(is_dmac64(dc) ? \
	 channel64_writel(dc, name, val) : channel32_writel(dc, name, val))

static dma_addr_t channel64_read_CHAR(const struct txx9dmac_chan *dc)
{
	if (sizeof(__dma_regs(dc)->CHAR) == sizeof(u64))
		return channel64_readq(dc, CHAR);
	else
		return channel64_readl(dc, CHAR);
}

static void channel64_write_CHAR(const struct txx9dmac_chan *dc, dma_addr_t val)
{
	if (sizeof(__dma_regs(dc)->CHAR) == sizeof(u64))
		channel64_writeq(dc, CHAR, val);
	else
		channel64_writel(dc, CHAR, val);
}

static void channel64_clear_CHAR(const struct txx9dmac_chan *dc)
{
#if defined(CONFIG_32BIT) && !defined(CONFIG_PHYS_ADDR_T_64BIT)
	channel64_writel(dc, CHAR, 0);
	channel64_writel(dc, __pad_CHAR, 0);
#else
	channel64_writeq(dc, CHAR, 0);
#endif
}

static dma_addr_t channel_read_CHAR(const struct txx9dmac_chan *dc)
{
	if (is_dmac64(dc))
		return channel64_read_CHAR(dc);
	else
		return channel32_readl(dc, CHAR);
}

static void channel_write_CHAR(const struct txx9dmac_chan *dc, dma_addr_t val)
{
	if (is_dmac64(dc))
		channel64_write_CHAR(dc, val);
	else
		channel32_writel(dc, CHAR, val);
}

static struct txx9dmac_regs __iomem *__txx9dmac_regs(
	const struct txx9dmac_dev *ddev)
{
	return ddev->regs;
}

static struct txx9dmac_regs32 __iomem *__txx9dmac_regs32(
	const struct txx9dmac_dev *ddev)
{
	return ddev->regs;
}

#define dma64_readl(ddev, name) \
	__raw_readl(&(__txx9dmac_regs(ddev)->name))
#define dma64_writel(ddev, name, val) \
	__raw_writel((val), &(__txx9dmac_regs(ddev)->name))

#define dma32_readl(ddev, name) \
	__raw_readl(&(__txx9dmac_regs32(ddev)->name))
#define dma32_writel(ddev, name, val) \
	__raw_writel((val), &(__txx9dmac_regs32(ddev)->name))

#define dma_readl(ddev, name) \
	(__is_dmac64(ddev) ? \
	dma64_readl(ddev, name) : dma32_readl(ddev, name))
#define dma_writel(ddev, name, val) \
	(__is_dmac64(ddev) ? \
	dma64_writel(ddev, name, val) : dma32_writel(ddev, name, val))

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}
static struct device *chan2parent(struct dma_chan *chan)
{
	return chan->dev->device.parent;
}

static struct txx9dmac_desc *
txd_to_txx9dmac_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct txx9dmac_desc, txd);
}

static dma_addr_t desc_read_CHAR(const struct txx9dmac_chan *dc,
				 const struct txx9dmac_desc *desc)
{
	return is_dmac64(dc) ? desc->hwdesc.CHAR : desc->hwdesc32.CHAR;
}

static void desc_write_CHAR(const struct txx9dmac_chan *dc,
			    struct txx9dmac_desc *desc, dma_addr_t val)
{
	if (is_dmac64(dc))
		desc->hwdesc.CHAR = val;
	else
		desc->hwdesc32.CHAR = val;
}

#define TXX9_DMA_MAX_COUNT	0x04000000

#define TXX9_DMA_INITIAL_DESC_COUNT	64

static struct txx9dmac_desc *txx9dmac_first_active(struct txx9dmac_chan *dc)
{
	return list_entry(dc->active_list.next,
			  struct txx9dmac_desc, desc_node);
}

static struct txx9dmac_desc *txx9dmac_last_active(struct txx9dmac_chan *dc)
{
	return list_entry(dc->active_list.prev,
			  struct txx9dmac_desc, desc_node);
}

static struct txx9dmac_desc *txx9dmac_first_queued(struct txx9dmac_chan *dc)
{
	return list_entry(dc->queue.next, struct txx9dmac_desc, desc_node);
}

static struct txx9dmac_desc *txx9dmac_last_child(struct txx9dmac_desc *desc)
{
	if (!list_empty(&desc->tx_list))
		desc = list_entry(desc->tx_list.prev, typeof(*desc), desc_node);
	return desc;
}

static dma_cookie_t txx9dmac_tx_submit(struct dma_async_tx_descriptor *tx);

static struct txx9dmac_desc *txx9dmac_desc_alloc(struct txx9dmac_chan *dc,
						 gfp_t flags)
{
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_desc *desc;

	desc = kzalloc(sizeof(*desc), flags);
	if (!desc)
		return NULL;
	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->txd, &dc->chan);
	desc->txd.tx_submit = txx9dmac_tx_submit;
	/* txd.flags will be overwritten in prep funcs */
	desc->txd.flags = DMA_CTRL_ACK;
	desc->txd.phys = dma_map_single(chan2parent(&dc->chan), &desc->hwdesc,
					ddev->descsize, DMA_TO_DEVICE);
	return desc;
}

static struct txx9dmac_desc *txx9dmac_desc_get(struct txx9dmac_chan *dc)
{
	struct txx9dmac_desc *desc, *_desc;
	struct txx9dmac_desc *ret = NULL;
	unsigned int i = 0;

	spin_lock_bh(&dc->lock);
	list_for_each_entry_safe(desc, _desc, &dc->free_list, desc_node) {
		if (async_tx_test_ack(&desc->txd)) {
			list_del(&desc->desc_node);
			ret = desc;
			break;
		}
		dev_dbg(chan2dev(&dc->chan), "desc %p not ACKed\n", desc);
		i++;
	}
	spin_unlock_bh(&dc->lock);

	dev_vdbg(chan2dev(&dc->chan), "scanned %u descriptors on freelist\n",
		 i);
	if (!ret) {
		ret = txx9dmac_desc_alloc(dc, GFP_ATOMIC);
		if (ret) {
			spin_lock_bh(&dc->lock);
			dc->descs_allocated++;
			spin_unlock_bh(&dc->lock);
		} else
			dev_err(chan2dev(&dc->chan),
				"not enough descriptors available\n");
	}
	return ret;
}

static void txx9dmac_sync_desc_for_cpu(struct txx9dmac_chan *dc,
				       struct txx9dmac_desc *desc)
{
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_desc *child;

	list_for_each_entry(child, &desc->tx_list, desc_node)
		dma_sync_single_for_cpu(chan2parent(&dc->chan),
				child->txd.phys, ddev->descsize,
				DMA_TO_DEVICE);
	dma_sync_single_for_cpu(chan2parent(&dc->chan),
			desc->txd.phys, ddev->descsize,
			DMA_TO_DEVICE);
}

/*
 * Move a descriptor, including any children, to the free list.
 * `desc' must not be on any lists.
 */
static void txx9dmac_desc_put(struct txx9dmac_chan *dc,
			      struct txx9dmac_desc *desc)
{
	if (desc) {
		struct txx9dmac_desc *child;

		txx9dmac_sync_desc_for_cpu(dc, desc);

		spin_lock_bh(&dc->lock);
		list_for_each_entry(child, &desc->tx_list, desc_node)
			dev_vdbg(chan2dev(&dc->chan),
				 "moving child desc %p to freelist\n",
				 child);
		list_splice_init(&desc->tx_list, &dc->free_list);
		dev_vdbg(chan2dev(&dc->chan), "moving desc %p to freelist\n",
			 desc);
		list_add(&desc->desc_node, &dc->free_list);
		spin_unlock_bh(&dc->lock);
	}
}

/*----------------------------------------------------------------------*/

static void txx9dmac_dump_regs(struct txx9dmac_chan *dc)
{
	if (is_dmac64(dc))
		dev_err(chan2dev(&dc->chan),
			"  CHAR: %#llx SAR: %#llx DAR: %#llx CNTR: %#x"
			" SAIR: %#x DAIR: %#x CCR: %#x CSR: %#x\n",
			(u64)channel64_read_CHAR(dc),
			channel64_readq(dc, SAR),
			channel64_readq(dc, DAR),
			channel64_readl(dc, CNTR),
			channel64_readl(dc, SAIR),
			channel64_readl(dc, DAIR),
			channel64_readl(dc, CCR),
			channel64_readl(dc, CSR));
	else
		dev_err(chan2dev(&dc->chan),
			"  CHAR: %#x SAR: %#x DAR: %#x CNTR: %#x"
			" SAIR: %#x DAIR: %#x CCR: %#x CSR: %#x\n",
			channel32_readl(dc, CHAR),
			channel32_readl(dc, SAR),
			channel32_readl(dc, DAR),
			channel32_readl(dc, CNTR),
			channel32_readl(dc, SAIR),
			channel32_readl(dc, DAIR),
			channel32_readl(dc, CCR),
			channel32_readl(dc, CSR));
}

static void txx9dmac_reset_chan(struct txx9dmac_chan *dc)
{
	channel_writel(dc, CCR, TXX9_DMA_CCR_CHRST);
	if (is_dmac64(dc)) {
		channel64_clear_CHAR(dc);
		channel_writeq(dc, SAR, 0);
		channel_writeq(dc, DAR, 0);
	} else {
		channel_writel(dc, CHAR, 0);
		channel_writel(dc, SAR, 0);
		channel_writel(dc, DAR, 0);
	}
	channel_writel(dc, CNTR, 0);
	channel_writel(dc, SAIR, 0);
	channel_writel(dc, DAIR, 0);
	channel_writel(dc, CCR, 0);
}

/* Called with dc->lock held and bh disabled */
static void txx9dmac_dostart(struct txx9dmac_chan *dc,
			     struct txx9dmac_desc *first)
{
	struct txx9dmac_slave *ds = dc->chan.private;
	u32 sai, dai;

	dev_vdbg(chan2dev(&dc->chan), "dostart %u %p\n",
		 first->txd.cookie, first);
	/* ASSERT:  channel is idle */
	if (channel_readl(dc, CSR) & TXX9_DMA_CSR_XFACT) {
		dev_err(chan2dev(&dc->chan),
			"BUG: Attempted to start non-idle channel\n");
		txx9dmac_dump_regs(dc);
		/* The tasklet will hopefully advance the queue... */
		return;
	}

	if (is_dmac64(dc)) {
		channel64_writel(dc, CNTR, 0);
		channel64_writel(dc, CSR, 0xffffffff);
		if (ds) {
			if (ds->tx_reg) {
				sai = ds->reg_width;
				dai = 0;
			} else {
				sai = 0;
				dai = ds->reg_width;
			}
		} else {
			sai = 8;
			dai = 8;
		}
		channel64_writel(dc, SAIR, sai);
		channel64_writel(dc, DAIR, dai);
		/* All 64-bit DMAC supports SMPCHN */
		channel64_writel(dc, CCR, dc->ccr);
		/* Writing a non zero value to CHAR will assert XFACT */
		channel64_write_CHAR(dc, first->txd.phys);
	} else {
		channel32_writel(dc, CNTR, 0);
		channel32_writel(dc, CSR, 0xffffffff);
		if (ds) {
			if (ds->tx_reg) {
				sai = ds->reg_width;
				dai = 0;
			} else {
				sai = 0;
				dai = ds->reg_width;
			}
		} else {
			sai = 4;
			dai = 4;
		}
		channel32_writel(dc, SAIR, sai);
		channel32_writel(dc, DAIR, dai);
		if (txx9_dma_have_SMPCHN()) {
			channel32_writel(dc, CCR, dc->ccr);
			/* Writing a non zero value to CHAR will assert XFACT */
			channel32_writel(dc, CHAR, first->txd.phys);
		} else {
			channel32_writel(dc, CHAR, first->txd.phys);
			channel32_writel(dc, CCR, dc->ccr);
		}
	}
}

/*----------------------------------------------------------------------*/

static void
txx9dmac_descriptor_complete(struct txx9dmac_chan *dc,
			     struct txx9dmac_desc *desc)
{
	struct dmaengine_desc_callback cb;
	struct dma_async_tx_descriptor *txd = &desc->txd;

	dev_vdbg(chan2dev(&dc->chan), "descriptor %u %p complete\n",
		 txd->cookie, desc);

	dma_cookie_complete(txd);
	dmaengine_desc_get_callback(txd, &cb);

	txx9dmac_sync_desc_for_cpu(dc, desc);
	list_splice_init(&desc->tx_list, &dc->free_list);
	list_move(&desc->desc_node, &dc->free_list);

	dma_descriptor_unmap(txd);
	/*
	 * The API requires that no submissions are done from a
	 * callback, so we don't need to drop the lock here
	 */
	dmaengine_desc_callback_invoke(&cb, NULL);
	dma_run_dependencies(txd);
}

static void txx9dmac_dequeue(struct txx9dmac_chan *dc, struct list_head *list)
{
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_desc *desc;
	struct txx9dmac_desc *prev = NULL;

	BUG_ON(!list_empty(list));
	do {
		desc = txx9dmac_first_queued(dc);
		if (prev) {
			desc_write_CHAR(dc, prev, desc->txd.phys);
			dma_sync_single_for_device(chan2parent(&dc->chan),
				prev->txd.phys, ddev->descsize,
				DMA_TO_DEVICE);
		}
		prev = txx9dmac_last_child(desc);
		list_move_tail(&desc->desc_node, list);
		/* Make chain-completion interrupt happen */
		if ((desc->txd.flags & DMA_PREP_INTERRUPT) &&
		    !txx9dmac_chan_INTENT(dc))
			break;
	} while (!list_empty(&dc->queue));
}

static void txx9dmac_complete_all(struct txx9dmac_chan *dc)
{
	struct txx9dmac_desc *desc, *_desc;
	LIST_HEAD(list);

	/*
	 * Submit queued descriptors ASAP, i.e. before we go through
	 * the completed ones.
	 */
	list_splice_init(&dc->active_list, &list);
	if (!list_empty(&dc->queue)) {
		txx9dmac_dequeue(dc, &dc->active_list);
		txx9dmac_dostart(dc, txx9dmac_first_active(dc));
	}

	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		txx9dmac_descriptor_complete(dc, desc);
}

static void txx9dmac_dump_desc(struct txx9dmac_chan *dc,
			       struct txx9dmac_hwdesc *desc)
{
	if (is_dmac64(dc)) {
#ifdef TXX9_DMA_USE_SIMPLE_CHAIN
		dev_crit(chan2dev(&dc->chan),
			 "  desc: ch%#llx s%#llx d%#llx c%#x\n",
			 (u64)desc->CHAR, desc->SAR, desc->DAR, desc->CNTR);
#else
		dev_crit(chan2dev(&dc->chan),
			 "  desc: ch%#llx s%#llx d%#llx c%#x"
			 " si%#x di%#x cc%#x cs%#x\n",
			 (u64)desc->CHAR, desc->SAR, desc->DAR, desc->CNTR,
			 desc->SAIR, desc->DAIR, desc->CCR, desc->CSR);
#endif
	} else {
		struct txx9dmac_hwdesc32 *d = (struct txx9dmac_hwdesc32 *)desc;
#ifdef TXX9_DMA_USE_SIMPLE_CHAIN
		dev_crit(chan2dev(&dc->chan),
			 "  desc: ch%#x s%#x d%#x c%#x\n",
			 d->CHAR, d->SAR, d->DAR, d->CNTR);
#else
		dev_crit(chan2dev(&dc->chan),
			 "  desc: ch%#x s%#x d%#x c%#x"
			 " si%#x di%#x cc%#x cs%#x\n",
			 d->CHAR, d->SAR, d->DAR, d->CNTR,
			 d->SAIR, d->DAIR, d->CCR, d->CSR);
#endif
	}
}

static void txx9dmac_handle_error(struct txx9dmac_chan *dc, u32 csr)
{
	struct txx9dmac_desc *bad_desc;
	struct txx9dmac_desc *child;
	u32 errors;

	/*
	 * The descriptor currently at the head of the active list is
	 * borked. Since we don't have any way to report errors, we'll
	 * just have to scream loudly and try to carry on.
	 */
	dev_crit(chan2dev(&dc->chan), "Abnormal Chain Completion\n");
	txx9dmac_dump_regs(dc);

	bad_desc = txx9dmac_first_active(dc);
	list_del_init(&bad_desc->desc_node);

	/* Clear all error flags and try to restart the controller */
	errors = csr & (TXX9_DMA_CSR_ABCHC |
			TXX9_DMA_CSR_CFERR | TXX9_DMA_CSR_CHERR |
			TXX9_DMA_CSR_DESERR | TXX9_DMA_CSR_SORERR);
	channel_writel(dc, CSR, errors);

	if (list_empty(&dc->active_list) && !list_empty(&dc->queue))
		txx9dmac_dequeue(dc, &dc->active_list);
	if (!list_empty(&dc->active_list))
		txx9dmac_dostart(dc, txx9dmac_first_active(dc));

	dev_crit(chan2dev(&dc->chan),
		 "Bad descriptor submitted for DMA! (cookie: %d)\n",
		 bad_desc->txd.cookie);
	txx9dmac_dump_desc(dc, &bad_desc->hwdesc);
	list_for_each_entry(child, &bad_desc->tx_list, desc_node)
		txx9dmac_dump_desc(dc, &child->hwdesc);
	/* Pretend the descriptor completed successfully */
	txx9dmac_descriptor_complete(dc, bad_desc);
}

static void txx9dmac_scan_descriptors(struct txx9dmac_chan *dc)
{
	dma_addr_t chain;
	struct txx9dmac_desc *desc, *_desc;
	struct txx9dmac_desc *child;
	u32 csr;

	if (is_dmac64(dc)) {
		chain = channel64_read_CHAR(dc);
		csr = channel64_readl(dc, CSR);
		channel64_writel(dc, CSR, csr);
	} else {
		chain = channel32_readl(dc, CHAR);
		csr = channel32_readl(dc, CSR);
		channel32_writel(dc, CSR, csr);
	}
	/* For dynamic chain, we should look at XFACT instead of NCHNC */
	if (!(csr & (TXX9_DMA_CSR_XFACT | TXX9_DMA_CSR_ABCHC))) {
		/* Everything we've submitted is done */
		txx9dmac_complete_all(dc);
		return;
	}
	if (!(csr & TXX9_DMA_CSR_CHNEN))
		chain = 0;	/* last descriptor of this chain */

	dev_vdbg(chan2dev(&dc->chan), "scan_descriptors: char=%#llx\n",
		 (u64)chain);

	list_for_each_entry_safe(desc, _desc, &dc->active_list, desc_node) {
		if (desc_read_CHAR(dc, desc) == chain) {
			/* This one is currently in progress */
			if (csr & TXX9_DMA_CSR_ABCHC)
				goto scan_done;
			return;
		}

		list_for_each_entry(child, &desc->tx_list, desc_node)
			if (desc_read_CHAR(dc, child) == chain) {
				/* Currently in progress */
				if (csr & TXX9_DMA_CSR_ABCHC)
					goto scan_done;
				return;
			}

		/*
		 * No descriptors so far seem to be in progress, i.e.
		 * this one must be done.
		 */
		txx9dmac_descriptor_complete(dc, desc);
	}
scan_done:
	if (csr & TXX9_DMA_CSR_ABCHC) {
		txx9dmac_handle_error(dc, csr);
		return;
	}

	dev_err(chan2dev(&dc->chan),
		"BUG: All descriptors done, but channel not idle!\n");

	/* Try to continue after resetting the channel... */
	txx9dmac_reset_chan(dc);

	if (!list_empty(&dc->queue)) {
		txx9dmac_dequeue(dc, &dc->active_list);
		txx9dmac_dostart(dc, txx9dmac_first_active(dc));
	}
}

static void txx9dmac_chan_tasklet(struct tasklet_struct *t)
{
	int irq;
	u32 csr;
	struct txx9dmac_chan *dc;

	dc = from_tasklet(dc, t, tasklet);
	csr = channel_readl(dc, CSR);
	dev_vdbg(chan2dev(&dc->chan), "tasklet: status=%x\n", csr);

	spin_lock(&dc->lock);
	if (csr & (TXX9_DMA_CSR_ABCHC | TXX9_DMA_CSR_NCHNC |
		   TXX9_DMA_CSR_NTRNFC))
		txx9dmac_scan_descriptors(dc);
	spin_unlock(&dc->lock);
	irq = dc->irq;

	enable_irq(irq);
}

static irqreturn_t txx9dmac_chan_interrupt(int irq, void *dev_id)
{
	struct txx9dmac_chan *dc = dev_id;

	dev_vdbg(chan2dev(&dc->chan), "interrupt: status=%#x\n",
			channel_readl(dc, CSR));

	tasklet_schedule(&dc->tasklet);
	/*
	 * Just disable the interrupts. We'll turn them back on in the
	 * softirq handler.
	 */
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}

static void txx9dmac_tasklet(struct tasklet_struct *t)
{
	int irq;
	u32 csr;
	struct txx9dmac_chan *dc;

	struct txx9dmac_dev *ddev = from_tasklet(ddev, t, tasklet);
	u32 mcr;
	int i;

	mcr = dma_readl(ddev, MCR);
	dev_vdbg(ddev->chan[0]->dma.dev, "tasklet: mcr=%x\n", mcr);
	for (i = 0; i < TXX9_DMA_MAX_NR_CHANNELS; i++) {
		if ((mcr >> (24 + i)) & 0x11) {
			dc = ddev->chan[i];
			csr = channel_readl(dc, CSR);
			dev_vdbg(chan2dev(&dc->chan), "tasklet: status=%x\n",
				 csr);
			spin_lock(&dc->lock);
			if (csr & (TXX9_DMA_CSR_ABCHC | TXX9_DMA_CSR_NCHNC |
				   TXX9_DMA_CSR_NTRNFC))
				txx9dmac_scan_descriptors(dc);
			spin_unlock(&dc->lock);
		}
	}
	irq = ddev->irq;

	enable_irq(irq);
}

static irqreturn_t txx9dmac_interrupt(int irq, void *dev_id)
{
	struct txx9dmac_dev *ddev = dev_id;

	dev_vdbg(ddev->chan[0]->dma.dev, "interrupt: status=%#x\n",
			dma_readl(ddev, MCR));

	tasklet_schedule(&ddev->tasklet);
	/*
	 * Just disable the interrupts. We'll turn them back on in the
	 * softirq handler.
	 */
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}

/*----------------------------------------------------------------------*/

static dma_cookie_t txx9dmac_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct txx9dmac_desc *desc = txd_to_txx9dmac_desc(tx);
	struct txx9dmac_chan *dc = to_txx9dmac_chan(tx->chan);
	dma_cookie_t cookie;

	spin_lock_bh(&dc->lock);
	cookie = dma_cookie_assign(tx);

	dev_vdbg(chan2dev(tx->chan), "tx_submit: queued %u %p\n",
		 desc->txd.cookie, desc);

	list_add_tail(&desc->desc_node, &dc->queue);
	spin_unlock_bh(&dc->lock);

	return cookie;
}

static struct dma_async_tx_descriptor *
txx9dmac_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_desc *desc;
	struct txx9dmac_desc *first;
	struct txx9dmac_desc *prev;
	size_t xfer_count;
	size_t offset;

	dev_vdbg(chan2dev(chan), "prep_dma_memcpy d%#llx s%#llx l%#zx f%#lx\n",
		 (u64)dest, (u64)src, len, flags);

	if (unlikely(!len)) {
		dev_dbg(chan2dev(chan), "prep_dma_memcpy: length is zero!\n");
		return NULL;
	}

	prev = first = NULL;

	for (offset = 0; offset < len; offset += xfer_count) {
		xfer_count = min_t(size_t, len - offset, TXX9_DMA_MAX_COUNT);
		/*
		 * Workaround for ERT-TX49H2-033, ERT-TX49H3-020,
		 * ERT-TX49H4-016 (slightly conservative)
		 */
		if (__is_dmac64(ddev)) {
			if (xfer_count > 0x100 &&
			    (xfer_count & 0xff) >= 0xfa &&
			    (xfer_count & 0xff) <= 0xff)
				xfer_count -= 0x20;
		} else {
			if (xfer_count > 0x80 &&
			    (xfer_count & 0x7f) >= 0x7e &&
			    (xfer_count & 0x7f) <= 0x7f)
				xfer_count -= 0x20;
		}

		desc = txx9dmac_desc_get(dc);
		if (!desc) {
			txx9dmac_desc_put(dc, first);
			return NULL;
		}

		if (__is_dmac64(ddev)) {
			desc->hwdesc.SAR = src + offset;
			desc->hwdesc.DAR = dest + offset;
			desc->hwdesc.CNTR = xfer_count;
			txx9dmac_desc_set_nosimple(ddev, desc, 8, 8,
					dc->ccr | TXX9_DMA_CCR_XFACT);
		} else {
			desc->hwdesc32.SAR = src + offset;
			desc->hwdesc32.DAR = dest + offset;
			desc->hwdesc32.CNTR = xfer_count;
			txx9dmac_desc_set_nosimple(ddev, desc, 4, 4,
					dc->ccr | TXX9_DMA_CCR_XFACT);
		}

		/*
		 * The descriptors on tx_list are not reachable from
		 * the dc->queue list or dc->active_list after a
		 * submit.  If we put all descriptors on active_list,
		 * calling of callback on the completion will be more
		 * complex.
		 */
		if (!first) {
			first = desc;
		} else {
			desc_write_CHAR(dc, prev, desc->txd.phys);
			dma_sync_single_for_device(chan2parent(&dc->chan),
					prev->txd.phys, ddev->descsize,
					DMA_TO_DEVICE);
			list_add_tail(&desc->desc_node, &first->tx_list);
		}
		prev = desc;
	}

	/* Trigger interrupt after last block */
	if (flags & DMA_PREP_INTERRUPT)
		txx9dmac_desc_set_INTENT(ddev, prev);

	desc_write_CHAR(dc, prev, 0);
	dma_sync_single_for_device(chan2parent(&dc->chan),
			prev->txd.phys, ddev->descsize,
			DMA_TO_DEVICE);

	first->txd.flags = flags;
	first->len = len;

	return &first->txd;
}

static struct dma_async_tx_descriptor *
txx9dmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *context)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_slave *ds = chan->private;
	struct txx9dmac_desc *prev;
	struct txx9dmac_desc *first;
	unsigned int i;
	struct scatterlist *sg;

	dev_vdbg(chan2dev(chan), "prep_dma_slave\n");

	BUG_ON(!ds || !ds->reg_width);
	if (ds->tx_reg)
		BUG_ON(direction != DMA_MEM_TO_DEV);
	else
		BUG_ON(direction != DMA_DEV_TO_MEM);
	if (unlikely(!sg_len))
		return NULL;

	prev = first = NULL;

	for_each_sg(sgl, sg, sg_len, i) {
		struct txx9dmac_desc *desc;
		dma_addr_t mem;
		u32 sai, dai;

		desc = txx9dmac_desc_get(dc);
		if (!desc) {
			txx9dmac_desc_put(dc, first);
			return NULL;
		}

		mem = sg_dma_address(sg);

		if (__is_dmac64(ddev)) {
			if (direction == DMA_MEM_TO_DEV) {
				desc->hwdesc.SAR = mem;
				desc->hwdesc.DAR = ds->tx_reg;
			} else {
				desc->hwdesc.SAR = ds->rx_reg;
				desc->hwdesc.DAR = mem;
			}
			desc->hwdesc.CNTR = sg_dma_len(sg);
		} else {
			if (direction == DMA_MEM_TO_DEV) {
				desc->hwdesc32.SAR = mem;
				desc->hwdesc32.DAR = ds->tx_reg;
			} else {
				desc->hwdesc32.SAR = ds->rx_reg;
				desc->hwdesc32.DAR = mem;
			}
			desc->hwdesc32.CNTR = sg_dma_len(sg);
		}
		if (direction == DMA_MEM_TO_DEV) {
			sai = ds->reg_width;
			dai = 0;
		} else {
			sai = 0;
			dai = ds->reg_width;
		}
		txx9dmac_desc_set_nosimple(ddev, desc, sai, dai,
					dc->ccr | TXX9_DMA_CCR_XFACT);

		if (!first) {
			first = desc;
		} else {
			desc_write_CHAR(dc, prev, desc->txd.phys);
			dma_sync_single_for_device(chan2parent(&dc->chan),
					prev->txd.phys,
					ddev->descsize,
					DMA_TO_DEVICE);
			list_add_tail(&desc->desc_node, &first->tx_list);
		}
		prev = desc;
	}

	/* Trigger interrupt after last block */
	if (flags & DMA_PREP_INTERRUPT)
		txx9dmac_desc_set_INTENT(ddev, prev);

	desc_write_CHAR(dc, prev, 0);
	dma_sync_single_for_device(chan2parent(&dc->chan),
			prev->txd.phys, ddev->descsize,
			DMA_TO_DEVICE);

	first->txd.flags = flags;
	first->len = 0;

	return &first->txd;
}

static int txx9dmac_terminate_all(struct dma_chan *chan)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);
	struct txx9dmac_desc *desc, *_desc;
	LIST_HEAD(list);

	dev_vdbg(chan2dev(chan), "terminate_all\n");
	spin_lock_bh(&dc->lock);

	txx9dmac_reset_chan(dc);

	/* active_list entries will end up before queued entries */
	list_splice_init(&dc->queue, &list);
	list_splice_init(&dc->active_list, &list);

	spin_unlock_bh(&dc->lock);

	/* Flush all pending and queued descriptors */
	list_for_each_entry_safe(desc, _desc, &list, desc_node)
		txx9dmac_descriptor_complete(dc, desc);

	return 0;
}

static enum dma_status
txx9dmac_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		   struct dma_tx_state *txstate)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);
	enum dma_status ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return DMA_COMPLETE;

	spin_lock_bh(&dc->lock);
	txx9dmac_scan_descriptors(dc);
	spin_unlock_bh(&dc->lock);

	return dma_cookie_status(chan, cookie, txstate);
}

static void txx9dmac_chain_dynamic(struct txx9dmac_chan *dc,
				   struct txx9dmac_desc *prev)
{
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_desc *desc;
	LIST_HEAD(list);

	prev = txx9dmac_last_child(prev);
	txx9dmac_dequeue(dc, &list);
	desc = list_entry(list.next, struct txx9dmac_desc, desc_node);
	desc_write_CHAR(dc, prev, desc->txd.phys);
	dma_sync_single_for_device(chan2parent(&dc->chan),
				   prev->txd.phys, ddev->descsize,
				   DMA_TO_DEVICE);
	if (!(channel_readl(dc, CSR) & TXX9_DMA_CSR_CHNEN) &&
	    channel_read_CHAR(dc) == prev->txd.phys)
		/* Restart chain DMA */
		channel_write_CHAR(dc, desc->txd.phys);
	list_splice_tail(&list, &dc->active_list);
}

static void txx9dmac_issue_pending(struct dma_chan *chan)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);

	spin_lock_bh(&dc->lock);

	if (!list_empty(&dc->active_list))
		txx9dmac_scan_descriptors(dc);
	if (!list_empty(&dc->queue)) {
		if (list_empty(&dc->active_list)) {
			txx9dmac_dequeue(dc, &dc->active_list);
			txx9dmac_dostart(dc, txx9dmac_first_active(dc));
		} else if (txx9_dma_have_SMPCHN()) {
			struct txx9dmac_desc *prev = txx9dmac_last_active(dc);

			if (!(prev->txd.flags & DMA_PREP_INTERRUPT) ||
			    txx9dmac_chan_INTENT(dc))
				txx9dmac_chain_dynamic(dc, prev);
		}
	}

	spin_unlock_bh(&dc->lock);
}

static int txx9dmac_alloc_chan_resources(struct dma_chan *chan)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);
	struct txx9dmac_slave *ds = chan->private;
	struct txx9dmac_desc *desc;
	int i;

	dev_vdbg(chan2dev(chan), "alloc_chan_resources\n");

	/* ASSERT:  channel is idle */
	if (channel_readl(dc, CSR) & TXX9_DMA_CSR_XFACT) {
		dev_dbg(chan2dev(chan), "DMA channel not idle?\n");
		return -EIO;
	}

	dma_cookie_init(chan);

	dc->ccr = TXX9_DMA_CCR_IMMCHN | TXX9_DMA_CCR_INTENE | CCR_LE;
	txx9dmac_chan_set_SMPCHN(dc);
	if (!txx9_dma_have_SMPCHN() || (dc->ccr & TXX9_DMA_CCR_SMPCHN))
		dc->ccr |= TXX9_DMA_CCR_INTENC;
	if (chan->device->device_prep_dma_memcpy) {
		if (ds)
			return -EINVAL;
		dc->ccr |= TXX9_DMA_CCR_XFSZ_X8;
	} else {
		if (!ds ||
		    (ds->tx_reg && ds->rx_reg) || (!ds->tx_reg && !ds->rx_reg))
			return -EINVAL;
		dc->ccr |= TXX9_DMA_CCR_EXTRQ |
			TXX9_DMA_CCR_XFSZ(__ffs(ds->reg_width));
		txx9dmac_chan_set_INTENT(dc);
	}

	spin_lock_bh(&dc->lock);
	i = dc->descs_allocated;
	while (dc->descs_allocated < TXX9_DMA_INITIAL_DESC_COUNT) {
		spin_unlock_bh(&dc->lock);

		desc = txx9dmac_desc_alloc(dc, GFP_KERNEL);
		if (!desc) {
			dev_info(chan2dev(chan),
				"only allocated %d descriptors\n", i);
			spin_lock_bh(&dc->lock);
			break;
		}
		txx9dmac_desc_put(dc, desc);

		spin_lock_bh(&dc->lock);
		i = ++dc->descs_allocated;
	}
	spin_unlock_bh(&dc->lock);

	dev_dbg(chan2dev(chan),
		"alloc_chan_resources allocated %d descriptors\n", i);

	return i;
}

static void txx9dmac_free_chan_resources(struct dma_chan *chan)
{
	struct txx9dmac_chan *dc = to_txx9dmac_chan(chan);
	struct txx9dmac_dev *ddev = dc->ddev;
	struct txx9dmac_desc *desc, *_desc;
	LIST_HEAD(list);

	dev_dbg(chan2dev(chan), "free_chan_resources (descs allocated=%u)\n",
			dc->descs_allocated);

	/* ASSERT:  channel is idle */
	BUG_ON(!list_empty(&dc->active_list));
	BUG_ON(!list_empty(&dc->queue));
	BUG_ON(channel_readl(dc, CSR) & TXX9_DMA_CSR_XFACT);

	spin_lock_bh(&dc->lock);
	list_splice_init(&dc->free_list, &list);
	dc->descs_allocated = 0;
	spin_unlock_bh(&dc->lock);

	list_for_each_entry_safe(desc, _desc, &list, desc_node) {
		dev_vdbg(chan2dev(chan), "  freeing descriptor %p\n", desc);
		dma_unmap_single(chan2parent(chan), desc->txd.phys,
				 ddev->descsize, DMA_TO_DEVICE);
		kfree(desc);
	}

	dev_vdbg(chan2dev(chan), "free_chan_resources done\n");
}

/*----------------------------------------------------------------------*/

static void txx9dmac_off(struct txx9dmac_dev *ddev)
{
	dma_writel(ddev, MCR, 0);
}

static int __init txx9dmac_chan_probe(struct platform_device *pdev)
{
	struct txx9dmac_chan_platform_data *cpdata =
			dev_get_platdata(&pdev->dev);
	struct platform_device *dmac_dev = cpdata->dmac_dev;
	struct txx9dmac_platform_data *pdata = dev_get_platdata(&dmac_dev->dev);
	struct txx9dmac_chan *dc;
	int err;
	int ch = pdev->id % TXX9_DMA_MAX_NR_CHANNELS;
	int irq;

	dc = devm_kzalloc(&pdev->dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	dc->dma.dev = &pdev->dev;
	dc->dma.device_alloc_chan_resources = txx9dmac_alloc_chan_resources;
	dc->dma.device_free_chan_resources = txx9dmac_free_chan_resources;
	dc->dma.device_terminate_all = txx9dmac_terminate_all;
	dc->dma.device_tx_status = txx9dmac_tx_status;
	dc->dma.device_issue_pending = txx9dmac_issue_pending;
	if (pdata && pdata->memcpy_chan == ch) {
		dc->dma.device_prep_dma_memcpy = txx9dmac_prep_dma_memcpy;
		dma_cap_set(DMA_MEMCPY, dc->dma.cap_mask);
	} else {
		dc->dma.device_prep_slave_sg = txx9dmac_prep_slave_sg;
		dma_cap_set(DMA_SLAVE, dc->dma.cap_mask);
		dma_cap_set(DMA_PRIVATE, dc->dma.cap_mask);
	}

	INIT_LIST_HEAD(&dc->dma.channels);
	dc->ddev = platform_get_drvdata(dmac_dev);
	if (dc->ddev->irq < 0) {
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;
		tasklet_setup(&dc->tasklet, txx9dmac_chan_tasklet);
		dc->irq = irq;
		err = devm_request_irq(&pdev->dev, dc->irq,
			txx9dmac_chan_interrupt, 0, dev_name(&pdev->dev), dc);
		if (err)
			return err;
	} else
		dc->irq = -1;
	dc->ddev->chan[ch] = dc;
	dc->chan.device = &dc->dma;
	list_add_tail(&dc->chan.device_node, &dc->chan.device->channels);
	dma_cookie_init(&dc->chan);

	if (is_dmac64(dc))
		dc->ch_regs = &__txx9dmac_regs(dc->ddev)->CHAN[ch];
	else
		dc->ch_regs = &__txx9dmac_regs32(dc->ddev)->CHAN[ch];
	spin_lock_init(&dc->lock);

	INIT_LIST_HEAD(&dc->active_list);
	INIT_LIST_HEAD(&dc->queue);
	INIT_LIST_HEAD(&dc->free_list);

	txx9dmac_reset_chan(dc);

	platform_set_drvdata(pdev, dc);

	err = dma_async_device_register(&dc->dma);
	if (err)
		return err;
	dev_dbg(&pdev->dev, "TXx9 DMA Channel (dma%d%s%s)\n",
		dc->dma.dev_id,
		dma_has_cap(DMA_MEMCPY, dc->dma.cap_mask) ? " memcpy" : "",
		dma_has_cap(DMA_SLAVE, dc->dma.cap_mask) ? " slave" : "");

	return 0;
}

static void txx9dmac_chan_remove(struct platform_device *pdev)
{
	struct txx9dmac_chan *dc = platform_get_drvdata(pdev);


	dma_async_device_unregister(&dc->dma);
	if (dc->irq >= 0) {
		devm_free_irq(&pdev->dev, dc->irq, dc);
		tasklet_kill(&dc->tasklet);
	}
	dc->ddev->chan[pdev->id % TXX9_DMA_MAX_NR_CHANNELS] = NULL;
}

static int __init txx9dmac_probe(struct platform_device *pdev)
{
	struct txx9dmac_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct resource *io;
	struct txx9dmac_dev *ddev;
	u32 mcr;
	int err;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -EINVAL;

	ddev = devm_kzalloc(&pdev->dev, sizeof(*ddev), GFP_KERNEL);
	if (!ddev)
		return -ENOMEM;

	if (!devm_request_mem_region(&pdev->dev, io->start, resource_size(io),
				     dev_name(&pdev->dev)))
		return -EBUSY;

	ddev->regs = devm_ioremap(&pdev->dev, io->start, resource_size(io));
	if (!ddev->regs)
		return -ENOMEM;
	ddev->have_64bit_regs = pdata->have_64bit_regs;
	if (__is_dmac64(ddev))
		ddev->descsize = sizeof(struct txx9dmac_hwdesc);
	else
		ddev->descsize = sizeof(struct txx9dmac_hwdesc32);

	/* force dma off, just in case */
	txx9dmac_off(ddev);

	ddev->irq = platform_get_irq(pdev, 0);
	if (ddev->irq >= 0) {
		tasklet_setup(&ddev->tasklet, txx9dmac_tasklet);
		err = devm_request_irq(&pdev->dev, ddev->irq,
			txx9dmac_interrupt, 0, dev_name(&pdev->dev), ddev);
		if (err)
			return err;
	}

	mcr = TXX9_DMA_MCR_MSTEN | MCR_LE;
	if (pdata && pdata->memcpy_chan >= 0)
		mcr |= TXX9_DMA_MCR_FIFUM(pdata->memcpy_chan);
	dma_writel(ddev, MCR, mcr);

	platform_set_drvdata(pdev, ddev);
	return 0;
}

static void txx9dmac_remove(struct platform_device *pdev)
{
	struct txx9dmac_dev *ddev = platform_get_drvdata(pdev);

	txx9dmac_off(ddev);
	if (ddev->irq >= 0) {
		devm_free_irq(&pdev->dev, ddev->irq, ddev);
		tasklet_kill(&ddev->tasklet);
	}
}

static void txx9dmac_shutdown(struct platform_device *pdev)
{
	struct txx9dmac_dev *ddev = platform_get_drvdata(pdev);

	txx9dmac_off(ddev);
}

static int txx9dmac_suspend_noirq(struct device *dev)
{
	struct txx9dmac_dev *ddev = dev_get_drvdata(dev);

	txx9dmac_off(ddev);
	return 0;
}

static int txx9dmac_resume_noirq(struct device *dev)
{
	struct txx9dmac_dev *ddev = dev_get_drvdata(dev);
	struct txx9dmac_platform_data *pdata = dev_get_platdata(dev);
	u32 mcr;

	mcr = TXX9_DMA_MCR_MSTEN | MCR_LE;
	if (pdata && pdata->memcpy_chan >= 0)
		mcr |= TXX9_DMA_MCR_FIFUM(pdata->memcpy_chan);
	dma_writel(ddev, MCR, mcr);
	return 0;

}

static const struct dev_pm_ops txx9dmac_dev_pm_ops = {
	.suspend_noirq = txx9dmac_suspend_noirq,
	.resume_noirq = txx9dmac_resume_noirq,
};

static struct platform_driver txx9dmac_chan_driver = {
	.remove_new	= txx9dmac_chan_remove,
	.driver = {
		.name	= "txx9dmac-chan",
	},
};

static struct platform_driver txx9dmac_driver = {
	.remove_new	= txx9dmac_remove,
	.shutdown	= txx9dmac_shutdown,
	.driver = {
		.name	= "txx9dmac",
		.pm	= &txx9dmac_dev_pm_ops,
	},
};

static int __init txx9dmac_init(void)
{
	int rc;

	rc = platform_driver_probe(&txx9dmac_driver, txx9dmac_probe);
	if (!rc) {
		rc = platform_driver_probe(&txx9dmac_chan_driver,
					   txx9dmac_chan_probe);
		if (rc)
			platform_driver_unregister(&txx9dmac_driver);
	}
	return rc;
}
module_init(txx9dmac_init);

static void __exit txx9dmac_exit(void)
{
	platform_driver_unregister(&txx9dmac_chan_driver);
	platform_driver_unregister(&txx9dmac_driver);
}
module_exit(txx9dmac_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TXx9 DMA Controller driver");
MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_ALIAS("platform:txx9dmac");
MODULE_ALIAS("platform:txx9dmac-chan");
