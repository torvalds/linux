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
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/dma/edma.h>
#include <linux/dma-mapping.h>
#include <linux/string_choices.h>

#include "dw-edma-core.h"
#include "dw-edma-v0-core.h"
#include "dw-hdma-v0-core.h"
#include "../dmaengine.h"
#include "../virt-dma.h"

static inline
struct dw_edma_desc *vd2dw_edma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct dw_edma_desc, vd);
}

static inline
u64 dw_edma_get_pci_address(struct dw_edma_chan *chan, phys_addr_t cpu_addr)
{
	struct dw_edma_chip *chip = chan->dw->chip;

	if (chip->ops->pci_address)
		return chip->ops->pci_address(chip->dev, cpu_addr);

	return cpu_addr;
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
	struct dw_edma_chip *chip = desc->chan->dw->chip;
	struct dw_edma_chan *chan = desc->chan;
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
	if (chan->dir == EDMA_DIR_WRITE) {
		chunk->ll_region.paddr = chip->ll_region_wr[chan->id].paddr;
		chunk->ll_region.vaddr = chip->ll_region_wr[chan->id].vaddr;
	} else {
		chunk->ll_region.paddr = chip->ll_region_rd[chan->id].paddr;
		chunk->ll_region.vaddr = chip->ll_region_rd[chan->id].vaddr;
	}

	if (desc->chunk) {
		/* Create and add new element into the linked list */
		if (!dw_edma_alloc_burst(chunk)) {
			kfree(chunk);
			return NULL;
		}
		desc->chunks_alloc++;
		list_add_tail(&chunk->list, &desc->chunk->list);
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

static int dw_edma_start_transfer(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;
	struct dw_edma_chunk *child;
	struct dw_edma_desc *desc;
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&chan->vc);
	if (!vd)
		return 0;

	desc = vd2dw_edma_desc(vd);
	if (!desc)
		return 0;

	child = list_first_entry_or_null(&desc->chunk->list,
					 struct dw_edma_chunk, list);
	if (!child)
		return 0;

	dw_edma_core_start(dw, child, !desc->xfer_sz);
	desc->xfer_sz += child->ll_region.sz;
	dw_edma_free_burst(child);
	list_del(&child->list);
	kfree(child);
	desc->chunks_alloc--;

	return 1;
}

static void dw_edma_device_caps(struct dma_chan *dchan,
				struct dma_slave_caps *caps)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	if (chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		if (chan->dir == EDMA_DIR_READ)
			caps->directions = BIT(DMA_DEV_TO_MEM);
		else
			caps->directions = BIT(DMA_MEM_TO_DEV);
	} else {
		if (chan->dir == EDMA_DIR_WRITE)
			caps->directions = BIT(DMA_DEV_TO_MEM);
		else
			caps->directions = BIT(DMA_MEM_TO_DEV);
	}
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

	if (!chan->configured) {
		/* Do nothing */
	} else if (chan->status == EDMA_ST_PAUSE) {
		chan->status = EDMA_ST_IDLE;
		chan->configured = false;
	} else if (chan->status == EDMA_ST_IDLE) {
		chan->configured = false;
	} else if (dw_edma_core_ch_status(chan) == DMA_COMPLETE) {
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

	if (!chan->configured)
		return;

	spin_lock_irqsave(&chan->vc.lock, flags);
	if (vchan_issue_pending(&chan->vc) && chan->request == EDMA_REQ_NONE &&
	    chan->status == EDMA_ST_IDLE) {
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
	struct scatterlist *sg = NULL;
	struct dw_edma_chunk *chunk;
	struct dw_edma_burst *burst;
	struct dw_edma_desc *desc;
	u64 src_addr, dst_addr;
	size_t fsz = 0;
	u32 cnt = 0;
	int i;

	if (!chan->configured)
		return NULL;

	/*
	 * Local Root Port/End-point              Remote End-point
	 * +-----------------------+ PCIe bus +----------------------+
	 * |                       |    +-+   |                      |
	 * |    DEV_TO_MEM   Rx Ch <----+ +---+ Tx Ch  DEV_TO_MEM    |
	 * |                       |    | |   |                      |
	 * |    MEM_TO_DEV   Tx Ch +----+ +---> Rx Ch  MEM_TO_DEV    |
	 * |                       |    +-+   |                      |
	 * +-----------------------+          +----------------------+
	 *
	 * 1. Normal logic:
	 * If eDMA is embedded into the DW PCIe RP/EP and controlled from the
	 * CPU/Application side, the Rx channel (EDMA_DIR_READ) will be used
	 * for the device read operations (DEV_TO_MEM) and the Tx channel
	 * (EDMA_DIR_WRITE) - for the write operations (MEM_TO_DEV).
	 *
	 * 2. Inverted logic:
	 * If eDMA is embedded into a Remote PCIe EP and is controlled by the
	 * MWr/MRd TLPs sent from the CPU's PCIe host controller, the Tx
	 * channel (EDMA_DIR_WRITE) will be used for the device read operations
	 * (DEV_TO_MEM) and the Rx channel (EDMA_DIR_READ) - for the write
	 * operations (MEM_TO_DEV).
	 *
	 * It is the client driver responsibility to choose a proper channel
	 * for the DMA transfers.
	 */
	if (chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		if ((chan->dir == EDMA_DIR_READ && dir != DMA_DEV_TO_MEM) ||
		    (chan->dir == EDMA_DIR_WRITE && dir != DMA_MEM_TO_DEV))
			return NULL;
	} else {
		if ((chan->dir == EDMA_DIR_WRITE && dir != DMA_DEV_TO_MEM) ||
		    (chan->dir == EDMA_DIR_READ && dir != DMA_MEM_TO_DEV))
			return NULL;
	}

	if (xfer->type == EDMA_XFER_CYCLIC) {
		if (!xfer->xfer.cyclic.len || !xfer->xfer.cyclic.cnt)
			return NULL;
	} else if (xfer->type == EDMA_XFER_SCATTER_GATHER) {
		if (xfer->xfer.sg.len < 1)
			return NULL;
	} else if (xfer->type == EDMA_XFER_INTERLEAVED) {
		if (!xfer->xfer.il->numf || xfer->xfer.il->frame_size < 1)
			return NULL;
		if (!xfer->xfer.il->src_inc || !xfer->xfer.il->dst_inc)
			return NULL;
	} else {
		return NULL;
	}

	desc = dw_edma_alloc_desc(chan);
	if (unlikely(!desc))
		goto err_alloc;

	chunk = dw_edma_alloc_chunk(desc);
	if (unlikely(!chunk))
		goto err_alloc;

	if (xfer->type == EDMA_XFER_INTERLEAVED) {
		src_addr = xfer->xfer.il->src_start;
		dst_addr = xfer->xfer.il->dst_start;
	} else {
		src_addr = chan->config.src_addr;
		dst_addr = chan->config.dst_addr;
	}

	if (dir == DMA_DEV_TO_MEM)
		src_addr = dw_edma_get_pci_address(chan, (phys_addr_t)src_addr);
	else
		dst_addr = dw_edma_get_pci_address(chan, (phys_addr_t)dst_addr);

	if (xfer->type == EDMA_XFER_CYCLIC) {
		cnt = xfer->xfer.cyclic.cnt;
	} else if (xfer->type == EDMA_XFER_SCATTER_GATHER) {
		cnt = xfer->xfer.sg.len;
		sg = xfer->xfer.sg.sgl;
	} else if (xfer->type == EDMA_XFER_INTERLEAVED) {
		cnt = xfer->xfer.il->numf * xfer->xfer.il->frame_size;
		fsz = xfer->xfer.il->frame_size;
	}

	for (i = 0; i < cnt; i++) {
		if (xfer->type == EDMA_XFER_SCATTER_GATHER && !sg)
			break;

		if (chunk->bursts_alloc == chan->ll_max) {
			chunk = dw_edma_alloc_chunk(desc);
			if (unlikely(!chunk))
				goto err_alloc;
		}

		burst = dw_edma_alloc_burst(chunk);
		if (unlikely(!burst))
			goto err_alloc;

		if (xfer->type == EDMA_XFER_CYCLIC)
			burst->sz = xfer->xfer.cyclic.len;
		else if (xfer->type == EDMA_XFER_SCATTER_GATHER)
			burst->sz = sg_dma_len(sg);
		else if (xfer->type == EDMA_XFER_INTERLEAVED)
			burst->sz = xfer->xfer.il->sgl[i % fsz].size;

		chunk->ll_region.sz += burst->sz;
		desc->alloc_sz += burst->sz;

		if (dir == DMA_DEV_TO_MEM) {
			burst->sar = src_addr;
			if (xfer->type == EDMA_XFER_CYCLIC) {
				burst->dar = xfer->xfer.cyclic.paddr;
			} else if (xfer->type == EDMA_XFER_SCATTER_GATHER) {
				src_addr += sg_dma_len(sg);
				burst->dar = sg_dma_address(sg);
				/* Unlike the typical assumption by other
				 * drivers/IPs the peripheral memory isn't
				 * a FIFO memory, in this case, it's a
				 * linear memory and that why the source
				 * and destination addresses are increased
				 * by the same portion (data length)
				 */
			} else if (xfer->type == EDMA_XFER_INTERLEAVED) {
				burst->dar = dst_addr;
			}
		} else {
			burst->dar = dst_addr;
			if (xfer->type == EDMA_XFER_CYCLIC) {
				burst->sar = xfer->xfer.cyclic.paddr;
			} else if (xfer->type == EDMA_XFER_SCATTER_GATHER) {
				dst_addr += sg_dma_len(sg);
				burst->sar = sg_dma_address(sg);
				/* Unlike the typical assumption by other
				 * drivers/IPs the peripheral memory isn't
				 * a FIFO memory, in this case, it's a
				 * linear memory and that why the source
				 * and destination addresses are increased
				 * by the same portion (data length)
				 */
			}  else if (xfer->type == EDMA_XFER_INTERLEAVED) {
				burst->sar = src_addr;
			}
		}

		if (xfer->type == EDMA_XFER_SCATTER_GATHER) {
			sg = sg_next(sg);
		} else if (xfer->type == EDMA_XFER_INTERLEAVED) {
			struct dma_interleaved_template *il = xfer->xfer.il;
			struct data_chunk *dc = &il->sgl[i % fsz];

			src_addr += burst->sz;
			if (il->src_sgl)
				src_addr += dmaengine_get_src_icg(il, dc);

			dst_addr += burst->sz;
			if (il->dst_sgl)
				dst_addr += dmaengine_get_dst_icg(il, dc);
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
	xfer.type = EDMA_XFER_SCATTER_GATHER;

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
	xfer.type = EDMA_XFER_CYCLIC;

	return dw_edma_device_transfer(&xfer);
}

static struct dma_async_tx_descriptor *
dw_edma_device_prep_interleaved_dma(struct dma_chan *dchan,
				    struct dma_interleaved_template *ilt,
				    unsigned long flags)
{
	struct dw_edma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = ilt->dir;
	xfer.xfer.il = ilt;
	xfer.flags = flags;
	xfer.type = EDMA_XFER_INTERLEAVED;

	return dw_edma_device_transfer(&xfer);
}

static void dw_hdma_set_callback_result(struct virt_dma_desc *vd,
					enum dmaengine_tx_result result)
{
	u32 residue = 0;
	struct dw_edma_desc *desc;
	struct dmaengine_result *res;

	if (!vd->tx.callback_result)
		return;

	desc = vd2dw_edma_desc(vd);
	if (desc)
		residue = desc->alloc_sz - desc->xfer_sz;

	res = &vd->tx_result;
	res->result = result;
	res->residue = residue;
}

static void dw_edma_done_interrupt(struct dw_edma_chan *chan)
{
	struct dw_edma_desc *desc;
	struct virt_dma_desc *vd;
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		switch (chan->request) {
		case EDMA_REQ_NONE:
			desc = vd2dw_edma_desc(vd);
			if (!desc->chunks_alloc) {
				dw_hdma_set_callback_result(vd,
							    DMA_TRANS_NOERROR);
				list_del(&vd->node);
				vchan_cookie_complete(vd);
			}

			/* Continue transferring if there are remaining chunks or issued requests.
			 */
			chan->status = dw_edma_start_transfer(chan) ? EDMA_ST_BUSY : EDMA_ST_IDLE;
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

	spin_lock_irqsave(&chan->vc.lock, flags);
	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		dw_hdma_set_callback_result(vd, DMA_TRANS_ABORTED);
		list_del(&vd->node);
		vchan_cookie_complete(vd);
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);
	chan->request = EDMA_REQ_NONE;
	chan->status = EDMA_ST_IDLE;
}

static inline irqreturn_t dw_edma_interrupt_write(int irq, void *data)
{
	struct dw_edma_irq *dw_irq = data;

	return dw_edma_core_handle_int(dw_irq, EDMA_DIR_WRITE,
				       dw_edma_done_interrupt,
				       dw_edma_abort_interrupt);
}

static inline irqreturn_t dw_edma_interrupt_read(int irq, void *data)
{
	struct dw_edma_irq *dw_irq = data;

	return dw_edma_core_handle_int(dw_irq, EDMA_DIR_READ,
				       dw_edma_done_interrupt,
				       dw_edma_abort_interrupt);
}

static irqreturn_t dw_edma_interrupt_common(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;

	ret |= dw_edma_interrupt_write(irq, data);
	ret |= dw_edma_interrupt_read(irq, data);

	return ret;
}

static int dw_edma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	if (chan->status != EDMA_ST_IDLE)
		return -EBUSY;

	return 0;
}

static void dw_edma_free_chan_resources(struct dma_chan *dchan)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);
	int ret;

	while (time_before(jiffies, timeout)) {
		ret = dw_edma_device_terminate_all(dchan);
		if (!ret)
			break;

		if (time_after_eq(jiffies, timeout))
			return;

		cpu_relax();
	}
}

static int dw_edma_channel_setup(struct dw_edma *dw, u32 wr_alloc, u32 rd_alloc)
{
	struct dw_edma_chip *chip = dw->chip;
	struct device *dev = chip->dev;
	struct dw_edma_chan *chan;
	struct dw_edma_irq *irq;
	struct dma_device *dma;
	u32 i, ch_cnt;
	u32 pos;

	ch_cnt = dw->wr_ch_cnt + dw->rd_ch_cnt;
	dma = &dw->dma;

	INIT_LIST_HEAD(&dma->channels);

	for (i = 0; i < ch_cnt; i++) {
		chan = &dw->chan[i];

		chan->dw = dw;

		if (i < dw->wr_ch_cnt) {
			chan->id = i;
			chan->dir = EDMA_DIR_WRITE;
		} else {
			chan->id = i - dw->wr_ch_cnt;
			chan->dir = EDMA_DIR_READ;
		}

		chan->configured = false;
		chan->request = EDMA_REQ_NONE;
		chan->status = EDMA_ST_IDLE;

		if (chan->dir == EDMA_DIR_WRITE)
			chan->ll_max = (chip->ll_region_wr[chan->id].sz / EDMA_LL_SZ);
		else
			chan->ll_max = (chip->ll_region_rd[chan->id].sz / EDMA_LL_SZ);
		chan->ll_max -= 1;

		dev_vdbg(dev, "L. List:\tChannel %s[%u] max_cnt=%u\n",
			 str_write_read(chan->dir == EDMA_DIR_WRITE),
			 chan->id, chan->ll_max);

		if (dw->nr_irqs == 1)
			pos = 0;
		else if (chan->dir == EDMA_DIR_WRITE)
			pos = chan->id % wr_alloc;
		else
			pos = wr_alloc + chan->id % rd_alloc;

		irq = &dw->irq[pos];

		if (chan->dir == EDMA_DIR_WRITE)
			irq->wr_mask |= BIT(chan->id);
		else
			irq->rd_mask |= BIT(chan->id);

		irq->dw = dw;
		memcpy(&chan->msi, &irq->msi, sizeof(chan->msi));

		dev_vdbg(dev, "MSI:\t\tChannel %s[%u] addr=0x%.8x%.8x, data=0x%.8x\n",
			 str_write_read(chan->dir == EDMA_DIR_WRITE),
			 chan->id,
			 chan->msi.address_hi, chan->msi.address_lo,
			 chan->msi.data);

		chan->vc.desc_free = vchan_free_desc;
		chan->vc.chan.private = chan->dir == EDMA_DIR_WRITE ?
					&dw->chip->dt_region_wr[chan->id] :
					&dw->chip->dt_region_rd[chan->id];

		vchan_init(&chan->vc, dma);

		dw_edma_core_ch_config(chan);
	}

	/* Set DMA channel capabilities */
	dma_cap_zero(dma->cap_mask);
	dma_cap_set(DMA_SLAVE, dma->cap_mask);
	dma_cap_set(DMA_CYCLIC, dma->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma->cap_mask);
	dma_cap_set(DMA_INTERLEAVE, dma->cap_mask);
	dma->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dma->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dma->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	/* Set DMA channel callbacks */
	dma->dev = chip->dev;
	dma->device_alloc_chan_resources = dw_edma_alloc_chan_resources;
	dma->device_free_chan_resources = dw_edma_free_chan_resources;
	dma->device_caps = dw_edma_device_caps;
	dma->device_config = dw_edma_device_config;
	dma->device_pause = dw_edma_device_pause;
	dma->device_resume = dw_edma_device_resume;
	dma->device_terminate_all = dw_edma_device_terminate_all;
	dma->device_issue_pending = dw_edma_device_issue_pending;
	dma->device_tx_status = dw_edma_device_tx_status;
	dma->device_prep_slave_sg = dw_edma_device_prep_slave_sg;
	dma->device_prep_dma_cyclic = dw_edma_device_prep_dma_cyclic;
	dma->device_prep_interleaved_dma = dw_edma_device_prep_interleaved_dma;

	dma_set_max_seg_size(dma->dev, U32_MAX);

	/* Register DMA device */
	return dma_async_device_register(dma);
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

static int dw_edma_irq_request(struct dw_edma *dw,
			       u32 *wr_alloc, u32 *rd_alloc)
{
	struct dw_edma_chip *chip = dw->chip;
	struct device *dev = dw->chip->dev;
	u32 wr_mask = 1;
	u32 rd_mask = 1;
	int i, err = 0;
	u32 ch_cnt;
	int irq;

	ch_cnt = dw->wr_ch_cnt + dw->rd_ch_cnt;

	if (chip->nr_irqs < 1 || !chip->ops->irq_vector)
		return -EINVAL;

	dw->irq = devm_kcalloc(dev, chip->nr_irqs, sizeof(*dw->irq), GFP_KERNEL);
	if (!dw->irq)
		return -ENOMEM;

	if (chip->nr_irqs == 1) {
		/* Common IRQ shared among all channels */
		irq = chip->ops->irq_vector(dev, 0);
		err = request_irq(irq, dw_edma_interrupt_common,
				  IRQF_SHARED, dw->name, &dw->irq[0]);
		if (err) {
			dw->nr_irqs = 0;
			return err;
		}

		if (irq_get_msi_desc(irq))
			get_cached_msi_msg(irq, &dw->irq[0].msi);

		dw->nr_irqs = 1;
	} else {
		/* Distribute IRQs equally among all channels */
		int tmp = chip->nr_irqs;

		while (tmp && (*wr_alloc + *rd_alloc) < ch_cnt) {
			dw_edma_dec_irq_alloc(&tmp, wr_alloc, dw->wr_ch_cnt);
			dw_edma_dec_irq_alloc(&tmp, rd_alloc, dw->rd_ch_cnt);
		}

		dw_edma_add_irq_mask(&wr_mask, *wr_alloc, dw->wr_ch_cnt);
		dw_edma_add_irq_mask(&rd_mask, *rd_alloc, dw->rd_ch_cnt);

		for (i = 0; i < (*wr_alloc + *rd_alloc); i++) {
			irq = chip->ops->irq_vector(dev, i);
			err = request_irq(irq,
					  i < *wr_alloc ?
						dw_edma_interrupt_write :
						dw_edma_interrupt_read,
					  IRQF_SHARED, dw->name,
					  &dw->irq[i]);
			if (err)
				goto err_irq_free;

			if (irq_get_msi_desc(irq))
				get_cached_msi_msg(irq, &dw->irq[i].msi);
		}

		dw->nr_irqs = i;
	}

	return 0;

err_irq_free:
	for  (i--; i >= 0; i--) {
		irq = chip->ops->irq_vector(dev, i);
		free_irq(irq, &dw->irq[i]);
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
	if (!dev || !chip->ops)
		return -EINVAL;

	dw = devm_kzalloc(dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw->chip = chip;

	if (dw->chip->mf == EDMA_MF_HDMA_NATIVE)
		dw_hdma_v0_core_register(dw);
	else
		dw_edma_v0_core_register(dw);

	raw_spin_lock_init(&dw->lock);

	dw->wr_ch_cnt = min_t(u16, chip->ll_wr_cnt,
			      dw_edma_core_ch_count(dw, EDMA_DIR_WRITE));
	dw->wr_ch_cnt = min_t(u16, dw->wr_ch_cnt, EDMA_MAX_WR_CH);

	dw->rd_ch_cnt = min_t(u16, chip->ll_rd_cnt,
			      dw_edma_core_ch_count(dw, EDMA_DIR_READ));
	dw->rd_ch_cnt = min_t(u16, dw->rd_ch_cnt, EDMA_MAX_RD_CH);

	if (!dw->wr_ch_cnt && !dw->rd_ch_cnt)
		return -EINVAL;

	dev_vdbg(dev, "Channels:\twrite=%d, read=%d\n",
		 dw->wr_ch_cnt, dw->rd_ch_cnt);

	/* Allocate channels */
	dw->chan = devm_kcalloc(dev, dw->wr_ch_cnt + dw->rd_ch_cnt,
				sizeof(*dw->chan), GFP_KERNEL);
	if (!dw->chan)
		return -ENOMEM;

	snprintf(dw->name, sizeof(dw->name), "dw-edma-core:%s",
		 dev_name(chip->dev));

	/* Disable eDMA, only to establish the ideal initial conditions */
	dw_edma_core_off(dw);

	/* Request IRQs */
	err = dw_edma_irq_request(dw, &wr_alloc, &rd_alloc);
	if (err)
		return err;

	/* Setup write/read channels */
	err = dw_edma_channel_setup(dw, wr_alloc, rd_alloc);
	if (err)
		goto err_irq_free;

	/* Turn debugfs on */
	dw_edma_core_debugfs_on(dw);

	chip->dw = dw;

	return 0;

err_irq_free:
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(chip->ops->irq_vector(dev, i), &dw->irq[i]);

	return err;
}
EXPORT_SYMBOL_GPL(dw_edma_probe);

int dw_edma_remove(struct dw_edma_chip *chip)
{
	struct dw_edma_chan *chan, *_chan;
	struct device *dev = chip->dev;
	struct dw_edma *dw = chip->dw;
	int i;

	/* Skip removal if no private data found */
	if (!dw)
		return -ENODEV;

	/* Disable eDMA */
	dw_edma_core_off(dw);

	/* Free irqs */
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(chip->ops->irq_vector(dev, i), &dw->irq[i]);

	/* Deregister eDMA device */
	dma_async_device_unregister(&dw->dma);
	list_for_each_entry_safe(chan, _chan, &dw->dma.channels,
				 vc.chan.device_node) {
		tasklet_kill(&chan->vc.task);
		list_del(&chan->vc.chan.device_node);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dw_edma_remove);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare eDMA controller core driver");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");
