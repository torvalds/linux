// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA core driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/dma/edma.h>
#include <linux/dma-mapping.h>

#include "dw-edma-core.h"
#include "dw-edma-v0-core.h"
#include "../dmaengine.h"
#include "../virt-dma.h"

static inline
struct device *dchan2dev(struct dma_chan *dchan)
{
	return &dchan->dev->device;
}

static inline
struct device *chan2dev(struct dw_edma_chan *chan)
{
	return &chan->vc.chan.dev->device;
}

static inline
struct dw_edma_desc *vd2dw_edma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct dw_edma_desc, vd);
}

static struct dw_edma_burst *dw_edma_alloc_burst(struct dw_edma_chunk *chunk)
{
	struct dw_edma_burst *burst;

	burst = kzalloc(sizeof(*burst), GFP_NOWAIT);
	if (unlikely(!burst))
		return NULL;

	INIT_LIST_HEAD(&burst->list);
	if (chunk->burst) {
		/* Create and add new element into the linked list */
		chunk->bursts_alloc++;
		list_add_tail(&burst->list, &chunk->burst->list);
	} else {
		/* List head */
		chunk->bursts_alloc = 0;
		chunk->burst = burst;
	}

	return burst;
}

static struct dw_edma_chunk *dw_edma_alloc_chunk(struct dw_edma_desc *desc)
{
	struct dw_edma_chan *chan = desc->chan;
	struct dw_edma *dw = chan->chip->dw;
	struct dw_edma_chunk *chunk;

	chunk = kzalloc(sizeof(*chunk), GFP_NOWAIT);
	if (unlikely(!chunk))
		return NULL;

	INIT_LIST_HEAD(&chunk->list);
	chunk->chan = chan;
	/* Toggling change bit (CB) in each chunk, this is a mechanism to
	 * inform the eDMA HW block that this is a new linked list ready
	 * to be consumed.
	 *  - Odd chunks originate CB equal to 0
	 *  - Even chunks originate CB equal to 1
	 */
	chunk->cb = !(desc->chunks_alloc % 2);
	chunk->ll_region.paddr = dw->ll_region.paddr + chan->ll_off;
	chunk->ll_region.vaddr = dw->ll_region.vaddr + chan->ll_off;

	if (desc->chunk) {
		/* Create and add new element into the linked list */
		desc->chunks_alloc++;
		list_add_tail(&chunk->list, &desc->chunk->list);
		if (!dw_edma_alloc_burst(chunk)) {
			kfree(chunk);
			return NULL;
		}
	} else {
		/* List head */
		chunk->burst = NULL;
		desc->chunks_alloc = 0;
		desc->chunk = chunk;
	}

	return chunk;
}

static struct dw_edma_desc *dw_edma_alloc_desc(struct dw_edma_chan *chan)
{
	struct dw_edma_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (unlikely(!desc))
		return NULL;

	desc->chan = chan;
	if (!dw_edma_alloc_chunk(desc)) {
		kfree(desc);
		return NULL;
	}

	return desc;
}

static void dw_edma_free_burst(struct dw_edma_chunk *chunk)
{
	struct dw_edma_burst *child, *_next;

	/* Remove all the list elements */
	list_for_each_entry_safe(child, _next, &chunk->burst->list, list) {
		list_del(&child->list);
		kfree(child);
		chunk->bursts_alloc--;
	}

	/* Remove the list head */
	kfree(child);
	chunk->burst = NULL;
}

static void dw_edma_free_chunk(struct dw_edma_desc *desc)
{
	struct dw_edma_chunk *child, *_next;

	if (!desc->chunk)
		return;

	/* Remove all the list elements */
	list_for_each_entry_safe(child, _next, &desc->chunk->list, list) {
		dw_edma_free_burst(child);
		list_del(&child->list);
		kfree(child);
		desc->chunks_alloc--;
	}

	/* Remove the list head */
	kfree(child);
	desc->chunk = NULL;
}

static void dw_edma_free_desc(struct dw_edma_desc *desc)
{
	dw_edma_free_chunk(desc);
	kfree(desc);
}

static void vchan_free_desc(struct virt_dma_desc *vdesc)
{
	dw_edma_free_desc(vd2dw_edma_desc(vdesc));
}

static void dw_edma_start_transfer(struct dw_edma_chan *chan)
{
	struct dw_edma_chunk *child;
	struct dw_edma_desc *desc;
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&chan->vc);
	if (!vd)
		return;

	desc = vd2dw_edma_desc(vd);
	if (!desc)
		return;

	child = list_first_entry_or_null(&desc->chunk->list,
					 struct dw_edma_chunk, list);
	if (!child)
		return;

	dw_edma_v0_core_start(child, !desc->xfer_sz);
	desc->xfer_sz += child->ll_region.sz;
	dw_edma_free_burst(child);
	list_del(&child->list);
	kfree(child);
	desc->chunks_alloc--;
}

static int dw_edma_device_config(struct dma_chan *dchan,
				 struct dma_slave_config *config)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	memcpy(&chan->config, config, sizeof(*config));
	chan->configured = true;

	return 0;
}

static int dw_edma_device_pause(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	int err = 0;

	if (!chan->configured)
		err = -EPERM;
	else if (chan->status != EDMA_ST_BUSY)
		err = -EPERM;
	else if (chan->request != EDMA_REQ_NONE)
		err = -EPERM;
	else
		chan->request = EDMA_REQ_PAUSE;

	return err;
}

static int dw_edma_device_resume(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	int err = 0;

	if (!chan->configured) {
		err = -EPERM;
	} else if (chan->status != EDMA_ST_PAUSE) {
		err = -EPERM;
	} else if (chan->request != EDMA_REQ_NONE) {
		err = -EPERM;
	} else {
		chan->status = EDMA_ST_BUSY;
		dw_edma_start_transfer(chan);
	}

	return err;
}

static int dw_edma_device_terminate_all(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	int err = 0;
	LIST_HEAD(head);

	if (!chan->configured) {
		/* Do nothing */
	} else if (chan->status == EDMA_ST_PAUSE) {
		chan->status = EDMA_ST_IDLE;
		chan->configured = false;
	} else if (chan->status == EDMA_ST_IDLE) {
		chan->configured = false;
	} else if (dw_edma_v0_core_ch_status(chan) == DMA_COMPLETE) {
		/*
		 * The channel is in a false BUSY state, probably didn't
		 * receive or lost an interrupt
		 */
		chan->status = EDMA_ST_IDLE;
		chan->configured = false;
	} else if (chan->request > EDMA_REQ_PAUSE) {
		err = -EPERM;
	} else {
		chan->request = EDMA_REQ_STOP;
	}

	return err;
}

static void dw_edma_device_issue_pending(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	if (chan->configured && chan->request == EDMA_REQ_NONE &&
	    chan->status == EDMA_ST_IDLE && vchan_issue_pending(&chan->vc)) {
		chan->status = EDMA_ST_BUSY;
		dw_edma_start_transfer(chan);
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static enum dma_status
dw_edma_device_tx_status(struct dma_chan *dchan, dma_cookie_t cookie,
			 struct dma_tx_state *txstate)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	struct dw_edma_desc *desc;
	struct virt_dma_desc *vd;
	unsigned long flags;
	enum dma_status ret;
	u32 residue = 0;

	ret = dma_cookie_status(dchan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	if (ret == DMA_IN_PROGRESS && chan->status == EDMA_ST_PAUSE)
		ret = DMA_PAUSED;

	if (!txstate)
		goto ret_residue;

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_find_desc(&chan->vc, cookie);
	if (vd) {
		desc = vd2dw_edma_desc(vd);
		if (desc)
			residue = desc->alloc_sz - desc->xfer_sz;
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);

ret_residue:
	dma_set_residue(txstate, residue);

	return ret;
}

static struct dma_async_tx_descriptor *
dw_edma_device_transfer(struct dw_edma_transfer *xfer)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(xfer->dchan);
	enum dma_transfer_direction dir = xfer->direction;
	phys_addr_t src_addr, dst_addr;
	struct scatterlist *sg = NULL;
	struct dw_edma_chunk *chunk;
	struct dw_edma_burst *burst;
	struct dw_edma_desc *desc;
	u32 cnt;
	int i;

	if (!chan->configured)
		return NULL;

	switch (chan->config.direction) {
	case DMA_DEV_TO_MEM: /* local dma */
		if (dir == DMA_DEV_TO_MEM && chan->dir == EDMA_DIR_READ)
			break;
		return NULL;
	case DMA_MEM_TO_DEV: /* local dma */
		if (dir == DMA_MEM_TO_DEV && chan->dir == EDMA_DIR_WRITE)
			break;
		return NULL;
	default: /* remote dma */
		if (dir == DMA_MEM_TO_DEV && chan->dir == EDMA_DIR_READ)
			break;
		if (dir == DMA_DEV_TO_MEM && chan->dir == EDMA_DIR_WRITE)
			break;
		return NULL;
	}

	if (xfer->cyclic) {
		if (!xfer->xfer.cyclic.len || !xfer->xfer.cyclic.cnt)
			return NULL;
	} else {
		if (xfer->xfer.sg.len < 1)
			return NULL;
	}

	desc = dw_edma_alloc_desc(chan);
	if (unlikely(!desc))
		goto err_alloc;

	chunk = dw_edma_alloc_chunk(desc);
	if (unlikely(!chunk))
		goto err_alloc;

	src_addr = chan->config.src_addr;
	dst_addr = chan->config.dst_addr;

	if (xfer->cyclic) {
		cnt = xfer->xfer.cyclic.cnt;
	} else {
		cnt = xfer->xfer.sg.len;
		sg = xfer->xfer.sg.sgl;
	}

	for (i = 0; i < cnt; i++) {
		if (!xfer->cyclic && !sg)
			break;

		if (chunk->bursts_alloc == chan->ll_max) {
			chunk = dw_edma_alloc_chunk(desc);
			if (unlikely(!chunk))
				goto err_alloc;
		}

		burst = dw_edma_alloc_burst(chunk);
		if (unlikely(!burst))
			goto err_alloc;

		if (xfer->cyclic)
			burst->sz = xfer->xfer.cyclic.len;
		else
			burst->sz = sg_dma_len(sg);

		chunk->ll_region.sz += burst->sz;
		desc->alloc_sz += burst->sz;

		if (chan->dir == EDMA_DIR_WRITE) {
			burst->sar = src_addr;
			if (xfer->cyclic) {
				burst->dar = xfer->xfer.cyclic.paddr;
			} else {
				burst->dar = dst_addr;
				/* Unlike the typical assumption by other
				 * drivers/IPs the peripheral memory isn't
				 * a FIFO memory, in this case, it's a
				 * linear memory and that why the source
				 * and destination addresses are increased
				 * by the same portion (data length)
				 */
			}
		} else {
			burst->dar = dst_addr;
			if (xfer->cyclic) {
				burst->sar = xfer->xfer.cyclic.paddr;
			} else {
				burst->sar = src_addr;
				/* Unlike the typical assumption by other
				 * drivers/IPs the peripheral memory isn't
				 * a FIFO memory, in this case, it's a
				 * linear memory and that why the source
				 * and destination addresses are increased
				 * by the same portion (data length)
				 */
			}
		}

		if (!xfer->cyclic) {
			src_addr += sg_dma_len(sg);
			dst_addr += sg_dma_len(sg);
			sg = sg_next(sg);
		}
	}

	return vchan_tx_prep(&chan->vc, &desc->vd, xfer->flags);

err_alloc:
	if (desc)
		dw_edma_free_desc(desc);

	return NULL;
}

static struct dma_async_tx_descriptor *
dw_edma_device_prep_slave_sg(struct dma_chan *dchan, struct scatterlist *sgl,
			     unsigned int len,
			     enum dma_transfer_direction direction,
			     unsigned long flags, void *context)
{
	struct dw_edma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = direction;
	xfer.xfer.sg.sgl = sgl;
	xfer.xfer.sg.len = len;
	xfer.flags = flags;
	xfer.cyclic = false;

	return dw_edma_device_transfer(&xfer);
}

static struct dma_async_tx_descriptor *
dw_edma_device_prep_dma_cyclic(struct dma_chan *dchan, dma_addr_t paddr,
			       size_t len, size_t count,
			       enum dma_transfer_direction direction,
			       unsigned long flags)
{
	struct dw_edma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = direction;
	xfer.xfer.cyclic.paddr = paddr;
	xfer.xfer.cyclic.len = len;
	xfer.xfer.cyclic.cnt = count;
	xfer.flags = flags;
	xfer.cyclic = true;

	return dw_edma_device_transfer(&xfer);
}

static void dw_edma_done_interrupt(struct dw_edma_chan *chan)
{
	struct dw_edma_desc *desc;
	struct virt_dma_desc *vd;
	unsigned long flags;

	dw_edma_v0_core_clear_done_int(chan);

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		switch (chan->request) {
		case EDMA_REQ_NONE:
			desc = vd2dw_edma_desc(vd);
			if (desc->chunks_alloc) {
				chan->status = EDMA_ST_BUSY;
				dw_edma_start_transfer(chan);
			} else {
				list_del(&vd->node);
				vchan_cookie_complete(vd);
				chan->status = EDMA_ST_IDLE;
			}
			break;

		case EDMA_REQ_STOP:
			list_del(&vd->node);
			vchan_cookie_complete(vd);
			chan->request = EDMA_REQ_NONE;
			chan->status = EDMA_ST_IDLE;
			break;

		case EDMA_REQ_PAUSE:
			chan->request = EDMA_REQ_NONE;
			chan->status = EDMA_ST_PAUSE;
			break;

		default:
			break;
		}
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static void dw_edma_abort_interrupt(struct dw_edma_chan *chan)
{
	struct virt_dma_desc *vd;
	unsigned long flags;

	dw_edma_v0_core_clear_abort_int(chan);

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		list_del(&vd->node);
		vchan_cookie_complete(vd);
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
	chan->request = EDMA_REQ_NONE;
	chan->status = EDMA_ST_IDLE;
}

static irqreturn_t dw_edma_interrupt(int irq, void *data, bool write)
{
	struct dw_edma_irq *dw_irq = data;
	struct dw_edma *dw = dw_irq->dw;
	unsigned long total, pos, val;
	unsigned long off;
	u32 mask;

	if (write) {
		total = dw->wr_ch_cnt;
		off = 0;
		mask = dw_irq->wr_mask;
	} else {
		total = dw->rd_ch_cnt;
		off = dw->wr_ch_cnt;
		mask = dw_irq->rd_mask;
	}

	val = dw_edma_v0_core_status_done_int(dw, write ?
							  EDMA_DIR_WRITE :
							  EDMA_DIR_READ);
	val &= mask;
	for_each_set_bit(pos, &val, total) {
		struct dw_edma_chan *chan = &dw->chan[pos + off];

		dw_edma_done_interrupt(chan);
	}

	val = dw_edma_v0_core_status_abort_int(dw, write ?
							   EDMA_DIR_WRITE :
							   EDMA_DIR_READ);
	val &= mask;
	for_each_set_bit(pos, &val, total) {
		struct dw_edma_chan *chan = &dw->chan[pos + off];

		dw_edma_abort_interrupt(chan);
	}

	return IRQ_HANDLED;
}

static inline irqreturn_t dw_edma_interrupt_write(int irq, void *data)
{
	return dw_edma_interrupt(irq, data, true);
}

static inline irqreturn_t dw_edma_interrupt_read(int irq, void *data)
{
	return dw_edma_interrupt(irq, data, false);
}

static irqreturn_t dw_edma_interrupt_common(int irq, void *data)
{
	dw_edma_interrupt(irq, data, true);
	dw_edma_interrupt(irq, data, false);

	return IRQ_HANDLED;
}

static int dw_edma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	if (chan->status != EDMA_ST_IDLE)
		return -EBUSY;

	pm_runtime_get(chan->chip->dev);

	return 0;
}

static void dw_edma_free_chan_resources(struct dma_chan *dchan)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	int ret;

	while (time_before(jiffies, timeout)) {
		ret = dw_edma_device_terminate_all(dchan);
		if (!ret)
			break;

		if (time_after_eq(jiffies, timeout))
			return;

		cpu_relax();
	}

	pm_runtime_put(chan->chip->dev);
}

static int dw_edma_channel_setup(struct dw_edma_chip *chip, bool write,
				 u32 wr_alloc, u32 rd_alloc)
{
	struct dw_edma_region *dt_region;
	struct device *dev = chip->dev;
	struct dw_edma *dw = chip->dw;
	struct dw_edma_chan *chan;
	size_t ll_chunk, dt_chunk;
	struct dw_edma_irq *irq;
	struct dma_device *dma;
	u32 i, j, cnt, ch_cnt;
	u32 alloc, off_alloc;
	int err = 0;
	u32 pos;

	ch_cnt = dw->wr_ch_cnt + dw->rd_ch_cnt;
	ll_chunk = dw->ll_region.sz;
	dt_chunk = dw->dt_region.sz;

	/* Calculate linked list chunk for each channel */
	ll_chunk /= roundup_pow_of_two(ch_cnt);

	/* Calculate linked list chunk for each channel */
	dt_chunk /= roundup_pow_of_two(ch_cnt);

	if (write) {
		i = 0;
		cnt = dw->wr_ch_cnt;
		dma = &dw->wr_edma;
		alloc = wr_alloc;
		off_alloc = 0;
	} else {
		i = dw->wr_ch_cnt;
		cnt = dw->rd_ch_cnt;
		dma = &dw->rd_edma;
		alloc = rd_alloc;
		off_alloc = wr_alloc;
	}

	INIT_LIST_HEAD(&dma->channels);
	for (j = 0; (alloc || dw->nr_irqs == 1) && j < cnt; j++, i++) {
		chan = &dw->chan[i];

		dt_region = devm_kzalloc(dev, sizeof(*dt_region), GFP_KERNEL);
		if (!dt_region)
			return -ENOMEM;

		chan->vc.chan.private = dt_region;

		chan->chip = chip;
		chan->id = j;
		chan->dir = write ? EDMA_DIR_WRITE : EDMA_DIR_READ;
		chan->configured = false;
		chan->request = EDMA_REQ_NONE;
		chan->status = EDMA_ST_IDLE;

		chan->ll_off = (ll_chunk * i);
		chan->ll_max = (ll_chunk / EDMA_LL_SZ) - 1;

		chan->dt_off = (dt_chunk * i);

		dev_vdbg(dev, "L. List:\tChannel %s[%u] off=0x%.8lx, max_cnt=%u\n",
			 write ? "write" : "read", j,
			 chan->ll_off, chan->ll_max);

		if (dw->nr_irqs == 1)
			pos = 0;
		else
			pos = off_alloc + (j % alloc);

		irq = &dw->irq[pos];

		if (write)
			irq->wr_mask |= BIT(j);
		else
			irq->rd_mask |= BIT(j);

		irq->dw = dw;
		memcpy(&chan->msi, &irq->msi, sizeof(chan->msi));

		dev_vdbg(dev, "MSI:\t\tChannel %s[%u] addr=0x%.8x%.8x, data=0x%.8x\n",
			 write ? "write" : "read", j,
			 chan->msi.address_hi, chan->msi.address_lo,
			 chan->msi.data);

		chan->vc.desc_free = vchan_free_desc;
		vchan_init(&chan->vc, dma);

		dt_region->paddr = dw->dt_region.paddr + chan->dt_off;
		dt_region->vaddr = dw->dt_region.vaddr + chan->dt_off;
		dt_region->sz = dt_chunk;

		dev_vdbg(dev, "Data:\tChannel %s[%u] off=0x%.8lx\n",
			 write ? "write" : "read", j, chan->dt_off);

		dw_edma_v0_core_device_config(chan);
	}

	/* Set DMA channel capabilities */
	dma_cap_zero(dma->cap_mask);
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);
	dma->directions = BIT(write ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV);
	dma->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dma->chancnt = cnt;

	/* Set DMA channel callbacks */
	dma->dev = chip->dev;
	dma->device_alloc_chan_resources = dw_edma_alloc_chan_resources;
	dma->device_free_chan_resources = dw_edma_free_chan_resources;
	dma->device_config = dw_edma_device_config;
	dma->device_pause = dw_edma_device_pause;
	dma->device_resume = dw_edma_device_resume;
	dma->device_terminate_all = dw_edma_device_terminate_all;
	dma->device_issue_pending = dw_edma_device_issue_pending;
	dma->device_tx_status = dw_edma_device_tx_status;
	dma->device_prep_slave_sg = dw_edma_device_prep_slave_sg;
	dma->device_prep_dma_cyclic = dw_edma_device_prep_dma_cyclic;

	dma_set_max_seg_size(dma->dev, U32_MAX);

	/* Register DMA device */
	err = dma_async_device_register(dma);

	return err;
}

static inline void dw_edma_dec_irq_alloc(int *nr_irqs, u32 *alloc, u16 cnt)
{
	if (*nr_irqs && *alloc < cnt) {
		(*alloc)++;
		(*nr_irqs)--;
	}
}

static inline void dw_edma_add_irq_mask(u32 *mask, u32 alloc, u16 cnt)
{
	while (*mask * alloc < cnt)
		(*mask)++;
}

static int dw_edma_irq_request(struct dw_edma_chip *chip,
			       u32 *wr_alloc, u32 *rd_alloc)
{
	struct device *dev = chip->dev;
	struct dw_edma *dw = chip->dw;
	u32 wr_mask = 1;
	u32 rd_mask = 1;
	int i, err = 0;
	u32 ch_cnt;
	int irq;

	ch_cnt = dw->wr_ch_cnt + dw->rd_ch_cnt;

	if (dw->nr_irqs < 1)
		return -EINVAL;

	if (dw->nr_irqs == 1) {
		/* Common IRQ shared among all channels */
		irq = dw->ops->irq_vector(dev, 0);
		err = request_irq(irq, dw_edma_interrupt_common,
				  IRQF_SHARED, dw->name, &dw->irq[0]);
		if (err) {
			dw->nr_irqs = 0;
			return err;
		}

		if (irq_get_msi_desc(irq))
			get_cached_msi_msg(irq, &dw->irq[0].msi);
	} else {
		/* Distribute IRQs equally among all channels */
		int tmp = dw->nr_irqs;

		while (tmp && (*wr_alloc + *rd_alloc) < ch_cnt) {
			dw_edma_dec_irq_alloc(&tmp, wr_alloc, dw->wr_ch_cnt);
			dw_edma_dec_irq_alloc(&tmp, rd_alloc, dw->rd_ch_cnt);
		}

		dw_edma_add_irq_mask(&wr_mask, *wr_alloc, dw->wr_ch_cnt);
		dw_edma_add_irq_mask(&rd_mask, *rd_alloc, dw->rd_ch_cnt);

		for (i = 0; i < (*wr_alloc + *rd_alloc); i++) {
			irq = dw->ops->irq_vector(dev, i);
			err = request_irq(irq,
					  i < *wr_alloc ?
						dw_edma_interrupt_write :
						dw_edma_interrupt_read,
					  IRQF_SHARED, dw->name,
					  &dw->irq[i]);
			if (err) {
				dw->nr_irqs = i;
				return err;
			}

			if (irq_get_msi_desc(irq))
				get_cached_msi_msg(irq, &dw->irq[i].msi);
		}

		dw->nr_irqs = i;
	}

	return err;
}

int dw_edma_probe(struct dw_edma_chip *chip)
{
	struct device *dev;
	struct dw_edma *dw;
	u32 wr_alloc = 0;
	u32 rd_alloc = 0;
	int i, err;

	if (!chip)
		return -EINVAL;

	dev = chip->dev;
	if (!dev)
		return -EINVAL;

	dw = chip->dw;
	if (!dw || !dw->irq || !dw->ops || !dw->ops->irq_vector)
		return -EINVAL;

	raw_spin_lock_init(&dw->lock);

	/* Find out how many write channels are supported by hardware */
	dw->wr_ch_cnt = dw_edma_v0_core_ch_count(dw, EDMA_DIR_WRITE);
	if (!dw->wr_ch_cnt)
		return -EINVAL;

	/* Find out how many read channels are supported by hardware */
	dw->rd_ch_cnt = dw_edma_v0_core_ch_count(dw, EDMA_DIR_READ);
	if (!dw->rd_ch_cnt)
		return -EINVAL;

	dev_vdbg(dev, "Channels:\twrite=%d, read=%d\n",
		 dw->wr_ch_cnt, dw->rd_ch_cnt);

	/* Allocate channels */
	dw->chan = devm_kcalloc(dev, dw->wr_ch_cnt + dw->rd_ch_cnt,
				sizeof(*dw->chan), GFP_KERNEL);
	if (!dw->chan)
		return -ENOMEM;

	snprintf(dw->name, sizeof(dw->name), "dw-edma-core:%d", chip->id);

	/* Disable eDMA, only to establish the ideal initial conditions */
	dw_edma_v0_core_off(dw);

	/* Request IRQs */
	err = dw_edma_irq_request(chip, &wr_alloc, &rd_alloc);
	if (err)
		return err;

	/* Setup write channels */
	err = dw_edma_channel_setup(chip, true, wr_alloc, rd_alloc);
	if (err)
		goto err_irq_free;

	/* Setup read channels */
	err = dw_edma_channel_setup(chip, false, wr_alloc, rd_alloc);
	if (err)
		goto err_irq_free;

	/* Power management */
	pm_runtime_enable(dev);

	/* Turn debugfs on */
	dw_edma_v0_core_debugfs_on(chip);

	return 0;

err_irq_free:
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(dw->ops->irq_vector(dev, i), &dw->irq[i]);

	dw->nr_irqs = 0;

	return err;
}
EXPORT_SYMBOL_GPL(dw_edma_probe);

int dw_edma_remove(struct dw_edma_chip *chip)
{
	struct dw_edma_chan *chan, *_chan;
	struct device *dev = chip->dev;
	struct dw_edma *dw = chip->dw;
	int i;

	/* Disable eDMA */
	dw_edma_v0_core_off(dw);

	/* Free irqs */
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(dw->ops->irq_vector(dev, i), &dw->irq[i]);

	/* Power management */
	pm_runtime_disable(dev);

	list_for_each_entry_safe(chan, _chan, &dw->wr_edma.channels,
				 vc.chan.device_node) {
		list_del(&chan->vc.chan.device_node);
		tasklet_kill(&chan->vc.task);
	}

	list_for_each_entry_safe(chan, _chan, &dw->rd_edma.channels,
				 vc.chan.device_node) {
		list_del(&chan->vc.chan.device_node);
		tasklet_kill(&chan->vc.task);
	}

	/* Deregister eDMA device */
	dma_async_device_unregister(&dw->wr_edma);
	dma_async_device_unregister(&dw->rd_edma);

	/* Turn debugfs off */
	dw_edma_v0_core_debugfs_off();

	return 0;
}
EXPORT_SYMBOL_GPL(dw_edma_remove);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare eDMA controller core driver");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");
