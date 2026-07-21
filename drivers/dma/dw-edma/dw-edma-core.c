// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA core driver
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
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

enum dw_edma_irq_event {
	DW_EDMA_IRQ_DONE	= BIT(0),
	DW_EDMA_IRQ_ABORT	= BIT(1),
};

static inline
u64 dw_edma_get_pci_address(struct dw_edma_chan *chan, phys_addr_t cpu_addr)
{
	struct dw_edma_chip *chip = chan->dw->chip;

	if (chip->ops->pci_address)
		return chip->ops->pci_address(chip->dev, cpu_addr);

	return cpu_addr;
}

static struct dw_edma_desc *
dw_edma_alloc_desc(struct dw_edma_chan *chan, size_t nburst)
{
	struct dw_edma_desc *desc;

	desc = kzalloc_flex(*desc, burst, nburst, GFP_NOWAIT);
	if (unlikely(!desc))
		return NULL;

	desc->chan = chan;
	desc->nburst = nburst;
	desc->cb = true;

	return desc;
}

static void vchan_free_desc(struct virt_dma_desc *vdesc)
{
	kfree(vd2dw_edma_desc(vdesc));
}

static void dw_edma_core_start(struct dw_edma_desc *desc, bool first)
{
	struct dw_edma_chan *chan = desc->chan;
	size_t i = 0;

	if (chan->non_ll) {
		chan->dw->core->non_ll_start(chan, &desc->burst[desc->start_burst]);
		desc->done_burst = desc->start_burst;
		desc->start_burst += 1;
		return;
	}

	for (i = 0; i + desc->start_burst < desc->nburst; i++) {
		u32 idx = i + desc->start_burst;

		if (i == chan->ll_max)
			break;

		dw_edma_core_ll_data(chan, &desc->burst[idx],
				     i, desc->cb,
				     idx == desc->nburst - 1 || i == chan->ll_max - 1);
	}

	desc->done_burst = desc->start_burst;
	desc->start_burst += i;

	dw_edma_core_ll_link(chan, i, desc->cb, chan->ll_region.paddr);

	if (first)
		dw_edma_core_ch_enable(chan);

	dw_edma_core_ch_doorbell(chan);
}

static int dw_edma_start_transfer(struct dw_edma_chan *chan)
{
	struct dw_edma_desc *desc;
	struct virt_dma_desc *vd;

	vd = vchan_next_desc(&chan->vc);
	if (!vd)
		return 0;

	desc = vd2dw_edma_desc(vd);
	if (!desc)
		return 0;

	dw_edma_core_start(desc, !desc->start_burst);

	desc->cb = !desc->cb;

	return 1;
}

static void dw_edma_terminate_vdesc(struct virt_dma_desc *vd)
{
	list_del(&vd->node);
	dma_cookie_complete(&vd->tx);
	vchan_terminate_vdesc(vd);
}

static void dw_edma_terminate_vdesc_list(struct list_head *head)
{
	struct virt_dma_desc *vd, *_vd;

	list_for_each_entry_safe(vd, _vd, head, node)
		dw_edma_terminate_vdesc(vd);
}

/* Must be called with vc.lock held. */
static void dw_edma_terminate_all_descs(struct dw_edma_chan *chan)
{
	/*
	 * This order must not be reversed. Cookies are assigned when
	 * descriptors are submitted, so desc_issued contains older cookies
	 * than desc_submitted. Completing desc_submitted first could move
	 * chan->vc.chan.completed_cookie backwards when desc_issued is
	 * terminated afterwards.
	 */
	dw_edma_terminate_vdesc_list(&chan->vc.desc_issued);
	dw_edma_terminate_vdesc_list(&chan->vc.desc_submitted);
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

static enum dw_edma_ch_irq_mode
dw_edma_get_default_irq_mode(struct dw_edma_chan *chan)
{
	struct dw_edma_chip *chip = chan->dw->chip;

	return chip->flags & DW_EDMA_CHIP_LOCAL ? DW_EDMA_CH_IRQ_LOCAL :
						  DW_EDMA_CH_IRQ_REMOTE;
}

static int dw_edma_device_config(struct dma_chan *dchan,
				 struct dma_slave_config *config)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	bool cfg_non_ll;
	int non_ll = 0;

	chan->non_ll = false;
	if (chan->dw->chip->mf == EDMA_MF_HDMA_NATIVE) {
		if (config->peripheral_config &&
		    config->peripheral_size != sizeof(int)) {
			dev_err(dchan->device->dev,
				"config param peripheral size mismatch\n");
			return -EINVAL;
		}

		/*
		 * When there is no valid LLP base address available then the
		 * default DMA ops will use the non-LL mode.
		 *
		 * Cases where LL mode is enabled and client wants to use the
		 * non-LL mode then also client can do so via providing the
		 * peripheral_config param.
		 */
		cfg_non_ll = chan->dw->chip->cfg_non_ll;
		if (config->peripheral_config) {
			non_ll = *(int *)config->peripheral_config;

			if (cfg_non_ll && !non_ll) {
				dev_err(dchan->device->dev, "invalid configuration\n");
				return -EINVAL;
			}
		}

		if (cfg_non_ll || non_ll)
			chan->non_ll = true;
	} else if (config->peripheral_config) {
		dev_err(dchan->device->dev,
			"peripheral config param applicable only for HDMA\n");
		return -EINVAL;
	}

	memcpy(&chan->config, config, sizeof(*config));
	chan->configured = true;

	return 0;
}

static struct dma_slave_config *
dw_edma_device_get_config(struct dma_chan *dchan,
			  struct dma_slave_config *config)
{
	struct dw_edma_chan *chan;

	if (config)
		return config;

	chan = dchan2dw_edma_chan(dchan);

	return &chan->config;
}

static int dw_edma_device_pause(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	int err = 0;

	guard(spinlock_irqsave)(&chan->vc.lock);

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

	guard(spinlock_irqsave)(&chan->vc.lock);

	if (!chan->configured) {
		err = -EPERM;
	} else if (chan->status != EDMA_ST_PAUSE) {
		err = -EPERM;
	} else if (chan->request != EDMA_REQ_NONE) {
		err = -EPERM;
	} else {
		chan->status = EDMA_ST_BUSY;
		if (!dw_edma_start_transfer(chan))
			chan->status = EDMA_ST_IDLE;
	}

	return err;
}

static int dw_edma_device_terminate_all(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	int err = 0;

	guard(spinlock_irqsave)(&chan->vc.lock);

	if (!chan->configured) {
		dw_edma_terminate_all_descs(chan);
	} else if (chan->status == EDMA_ST_PAUSE) {
		dw_edma_terminate_all_descs(chan);
		chan->status = EDMA_ST_IDLE;
	} else if (chan->status == EDMA_ST_IDLE) {
		dw_edma_terminate_all_descs(chan);
	} else if (dw_edma_core_ch_status(chan) == DMA_COMPLETE) {
		/*
		 * The channel is in a false BUSY state, probably didn't
		 * receive or lost an interrupt
		 */
		dw_edma_terminate_all_descs(chan);
		chan->status = EDMA_ST_IDLE;
	} else if (chan->request > EDMA_REQ_PAUSE) {
		err = -EPERM;
	} else {
		chan->request = EDMA_REQ_STOP;
	}
	if (chan->status == EDMA_ST_IDLE)
		chan->request = EDMA_REQ_NONE;

	return err;
}

static void dw_edma_device_issue_pending(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	if (chan->configured && vchan_issue_pending(&chan->vc) &&
	    chan->request == EDMA_REQ_NONE &&
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

		residue = desc->alloc_sz;
		if (desc && desc->done_burst)
			residue -= desc->burst[desc->done_burst - 1].xfer_sz;
	}
	spin_unlock_irqrestore(&chan->vc.lock, flags);

ret_residue:
	dma_set_residue(txstate, residue);

	return ret;
}

static struct dma_async_tx_descriptor *
dw_edma_device_transfer(struct dw_edma_transfer *xfer,
			struct dma_slave_config *config)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(xfer->dchan);
	enum dma_transfer_direction dir = xfer->direction;
	struct scatterlist *sg = NULL;
	struct dw_edma_burst *burst;
	struct dw_edma_desc *desc;
	u64 src_addr, dst_addr;
	size_t fsz = 0;
	size_t cnt = 0;
	u32 i;

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

	if (xfer->type == EDMA_XFER_INTERLEAVED) {
		src_addr = xfer->xfer.il->src_start;
		dst_addr = xfer->xfer.il->dst_start;
	} else {
		src_addr = config->src_addr;
		dst_addr = config->dst_addr;
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

	desc = dw_edma_alloc_desc(chan, cnt);
	if (unlikely(!desc))
		return NULL;

	for (i = 0; i < cnt; i++) {
		if (xfer->type == EDMA_XFER_SCATTER_GATHER && !sg)
			break;

		burst = desc->burst + i;

		if (xfer->type == EDMA_XFER_CYCLIC)
			burst->sz = xfer->xfer.cyclic.len;
		else if (xfer->type == EDMA_XFER_SCATTER_GATHER)
			burst->sz = sg_dma_len(sg);
		else if (xfer->type == EDMA_XFER_INTERLEAVED)
			burst->sz = xfer->xfer.il->sgl[i % fsz].size;

		desc->alloc_sz += burst->sz;
		burst->xfer_sz = desc->alloc_sz;

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
}

static struct dma_async_tx_descriptor *
dw_edma_device_prep_config_sg(struct dma_chan *dchan, struct scatterlist *sgl,
			      unsigned int len,
			      enum dma_transfer_direction direction,
			      unsigned long flags,
			      struct dma_slave_config *config)
{
	struct dw_edma_transfer xfer;

	xfer.dchan = dchan;
	xfer.direction = direction;
	xfer.xfer.sg.sgl = sgl;
	xfer.xfer.sg.len = len;
	xfer.flags = flags;
	xfer.type = EDMA_XFER_SCATTER_GATHER;

	if (config && dw_edma_device_config(dchan, config))
		return NULL;

	return dw_edma_device_transfer(&xfer, dw_edma_device_get_config(dchan, config));
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

	return dw_edma_device_transfer(&xfer, dw_edma_device_get_config(dchan, NULL));
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

	return dw_edma_device_transfer(&xfer, dw_edma_device_get_config(dchan, NULL));
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
	if (desc) {
		residue = desc->alloc_sz;

		if (result == DMA_TRANS_NOERROR)
			residue -= desc->burst[desc->start_burst - 1].xfer_sz;
		else if (desc->done_burst)
			residue -= desc->burst[desc->done_burst - 1].xfer_sz;
	}

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
	if (chan->status == EDMA_ST_PAUSE) {
		spin_unlock_irqrestore(&chan->vc.lock, flags);
		return;
	}

	vd = vchan_next_desc(&chan->vc);
	if (vd) {
		switch (chan->request) {
		case EDMA_REQ_NONE:
		case EDMA_REQ_PAUSE:
			desc = vd2dw_edma_desc(vd);
			if (desc->start_burst >= desc->nburst) {
				dw_hdma_set_callback_result(vd,
							    DMA_TRANS_NOERROR);
				list_del(&vd->node);
				vchan_cookie_complete(vd);
			}

			if (chan->request == EDMA_REQ_PAUSE) {
				chan->request = EDMA_REQ_NONE;
				chan->status = EDMA_ST_PAUSE;
				break;
			}

			/* Continue transferring if there are remaining chunks or issued requests.
			 */
			chan->status = dw_edma_start_transfer(chan) ? EDMA_ST_BUSY : EDMA_ST_IDLE;
			break;

		case EDMA_REQ_STOP:
			dw_edma_terminate_all_descs(chan);
			chan->request = EDMA_REQ_NONE;
			chan->status = EDMA_ST_IDLE;
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
	if (vd && chan->request == EDMA_REQ_STOP) {
		dw_edma_terminate_all_descs(chan);
	} else if (vd) {
		dw_hdma_set_callback_result(vd, DMA_TRANS_ABORTED);
		list_del(&vd->node);
		vchan_cookie_complete(vd);
	}
	chan->request = EDMA_REQ_NONE;
	chan->status = EDMA_ST_IDLE;
	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static void dw_edma_irq_work(struct work_struct *work)
{
	struct dw_edma_chan *chan = container_of(work, struct dw_edma_chan,
						 irq_work);
	unsigned int events;

	do {
		events = atomic_xchg(&chan->irq_pending, 0);

		if (events & DW_EDMA_IRQ_DONE)
			dw_edma_done_interrupt(chan);
		if (events & DW_EDMA_IRQ_ABORT)
			dw_edma_abort_interrupt(chan);
	} while (atomic_read(&chan->irq_pending));
}

static void dw_edma_queue_irq_work(struct dw_edma_chan *chan,
				   enum dw_edma_irq_event event)
{
	atomic_or(event, &chan->irq_pending);
	queue_work(chan->dw->wq, &chan->irq_work);
}

static void dw_edma_done_interrupt_deferred(struct dw_edma_chan *chan)
{
	dw_edma_queue_irq_work(chan, DW_EDMA_IRQ_DONE);
}

static void dw_edma_abort_interrupt_deferred(struct dw_edma_chan *chan)
{
	dw_edma_queue_irq_work(chan, DW_EDMA_IRQ_ABORT);
}

static void dw_edma_emul_irq_ack(struct irq_data *d)
{
	struct dw_edma *dw = irq_data_get_irq_chip_data(d);

	dw_edma_core_ack_emulated_irq(dw);
}

/*
 * irq_chip implementation for interrupt-emulation doorbells.
 *
 * The emulated source has no mask/unmask mechanism. With handle_level_irq(),
 * the flow is therefore:
 *   1) .irq_ack() deasserts the source
 *   2) registered handlers (if any) are dispatched
 * Since deassertion is already done in .irq_ack(), handlers do not need to take
 * care of it, hence IRQCHIP_ONESHOT_SAFE.
 */
static struct irq_chip dw_edma_emul_irqchip = {
	.name		= "dw-edma-emul",
	.irq_ack	= dw_edma_emul_irq_ack,
	.flags		= IRQCHIP_ONESHOT_SAFE | IRQCHIP_SKIP_SET_WAKE,
};

static int dw_edma_emul_irq_alloc(struct dw_edma *dw)
{
	struct dw_edma_chip *chip = dw->chip;
	int virq;

	chip->db_irq = 0;
	chip->db_offset = ~0;

	if (chip->flags & DW_EDMA_CHIP_PARTIAL)
		return 0;

	/*
	 * Only meaningful when the core provides the deassert sequence
	 * for interrupt emulation.
	 */
	if (!dw->core->ack_emulated_irq)
		return 0;

	/*
	 * Allocate a single, requestable Linux virtual IRQ number.
	 * Use >= 1 so that 0 can remain a "not available" sentinel.
	 */
	virq = irq_alloc_desc(NUMA_NO_NODE);
	if (virq < 0)
		return virq;

	irq_set_chip_and_handler(virq, &dw_edma_emul_irqchip, handle_level_irq);
	irq_set_chip_data(virq, dw);
	irq_set_noprobe(virq);

	chip->db_irq = virq;
	chip->db_offset = dw_edma_core_db_offset(dw);

	return 0;
}

static void dw_edma_emul_irq_free(struct dw_edma *dw)
{
	struct dw_edma_chip *chip = dw->chip;

	if (!chip)
		return;
	if (chip->db_irq <= 0)
		return;

	irq_free_descs(chip->db_irq, 1);
	chip->db_irq = 0;
	chip->db_offset = ~0;
}

static inline irqreturn_t dw_edma_interrupt_emulated(void *data)
{
	struct dw_edma_irq *dw_irq = data;
	struct dw_edma *dw = dw_irq->dw;
	int db_irq = dw->chip->db_irq;

	if (db_irq > 0) {
		/*
		 * Interrupt emulation may assert the IRQ line without updating the
		 * normal DONE/ABORT status bits. With a shared IRQ handler we
		 * cannot reliably detect such events by status registers alone, so
		 * always perform the core-specific deassert sequence.
		 */
		generic_handle_irq(db_irq);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static inline irqreturn_t dw_edma_interrupt_write_inner(int irq, void *data)
{
	struct dw_edma_irq *dw_irq = data;

	return dw_edma_core_handle_int(dw_irq, EDMA_DIR_WRITE,
				       dw_edma_done_interrupt_deferred,
				       dw_edma_abort_interrupt_deferred);
}

static inline irqreturn_t dw_edma_interrupt_read_inner(int irq, void *data)
{
	struct dw_edma_irq *dw_irq = data;

	return dw_edma_core_handle_int(dw_irq, EDMA_DIR_READ,
				       dw_edma_done_interrupt_deferred,
				       dw_edma_abort_interrupt_deferred);
}

static inline irqreturn_t dw_edma_interrupt_write(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;

	ret |= dw_edma_interrupt_write_inner(irq, data);
	ret |= dw_edma_interrupt_emulated(data);

	return ret;
}

static inline irqreturn_t dw_edma_interrupt_read(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;

	ret |= dw_edma_interrupt_read_inner(irq, data);
	ret |= dw_edma_interrupt_emulated(data);

	return ret;
}

static inline irqreturn_t dw_edma_interrupt_common(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;

	ret |= dw_edma_interrupt_write_inner(irq, data);
	ret |= dw_edma_interrupt_read_inner(irq, data);
	ret |= dw_edma_interrupt_emulated(data);

	return ret;
}

static int dw_edma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	if (chan->status != EDMA_ST_IDLE)
		return -EBUSY;

	return 0;
}

static void dw_edma_wait_termination(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);
	unsigned long timeout = jiffies + msecs_to_jiffies(5000);
	bool stopping;

	/*
	 * A STOP may be deferred to a later interrupt while the channel is still
	 * running. Wait until that handler completes the termination.
	 */
	while (time_before(jiffies, timeout)) {
		scoped_guard(spinlock_irqsave, &chan->vc.lock)
			stopping = chan->request == EDMA_REQ_STOP;

		if (!stopping)
			return;

		fsleep(1000);
	}

	dev_warn(chan->dw->chip->dev,
		 "timeout waiting for channel termination\n");
}

static void dw_edma_device_synchronize(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	dw_edma_wait_termination(dchan);
	cancel_work_sync(&chan->irq_work);
	atomic_set(&chan->irq_pending, 0);
	vchan_synchronize(&chan->vc);
}

static void dw_edma_free_chan_resources(struct dma_chan *dchan)
{
	struct dw_edma_chan *chan = dchan2dw_edma_chan(dchan);

	dw_edma_device_terminate_all(dchan);
	dw_edma_device_synchronize(dchan);

	scoped_guard(spinlock_irqsave, &chan->vc.lock)
		chan->configured = false;

	vchan_free_chan_resources(&chan->vc);
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
		chan->func_no = chip->func_no;

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
		chan->irq_mode = dw_edma_get_default_irq_mode(chan);
		INIT_WORK(&chan->irq_work, dw_edma_irq_work);
		atomic_set(&chan->irq_pending, 0);

		if (chan->dir == EDMA_DIR_WRITE)
			chan->ll_region = chip->ll_region_wr[chan->id];
		else
			chan->ll_region = chip->ll_region_rd[chan->id];

		chan->ll_max = chan->ll_region.sz / EDMA_LL_SZ - 1;

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
			bitmap_set(irq->wr_mask, chan->id, 1);
		else
			bitmap_set(irq->rd_mask, chan->id, 1);

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
	dma->device_synchronize = dw_edma_device_synchronize;
	dma->device_issue_pending = dw_edma_device_issue_pending;
	dma->device_tx_status = dw_edma_device_tx_status;
	dma->device_prep_config_sg = dw_edma_device_prep_config_sg;
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

static int dw_edma_irq_request(struct dw_edma *dw,
			       u32 *wr_alloc, u32 *rd_alloc)
{
	struct dw_edma_chip *chip = dw->chip;
	struct device *dev = dw->chip->dev;
	struct msi_desc *msi_desc;
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
		dw->irq[0].dw = dw;
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

		for (i = 0; i < (*wr_alloc + *rd_alloc); i++) {
			irq = chip->ops->irq_vector(dev, i);
			dw->irq[i].dw = dw;
			err = request_irq(irq,
					  i < *wr_alloc ?
						dw_edma_interrupt_write :
						dw_edma_interrupt_read,
					  IRQF_SHARED, dw->name,
					  &dw->irq[i]);
			if (err)
				goto err_irq_free;
			msi_desc = irq_get_msi_desc(irq);
			if (msi_desc) {
				get_cached_msi_msg(irq, &dw->irq[i].msi);
				if (!msi_desc->pci.msi_attrib.is_msix)
					dw->irq[i].msi.data = dw->irq[0].msi.data + i;
			}
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

static int dw_edma_check_partial(struct dw_edma_chip *chip,
				 u16 hw_wr_ch_cnt, u16 hw_rd_ch_cnt)
{
	if (!(chip->flags & DW_EDMA_CHIP_PARTIAL))
		return 0;

	if (chip->mf != EDMA_MF_EDMA_UNROLL &&
	    chip->mf != EDMA_MF_HDMA_COMPAT)
		return 0;

	/*
	 * Direction-wide registers are shared by all channels in that
	 * direction, so a direction must have a single owner.
	 */
	if ((chip->ll_wr_cnt && chip->ll_wr_cnt != hw_wr_ch_cnt) ||
	    (chip->ll_rd_cnt && chip->ll_rd_cnt != hw_rd_ch_cnt))
		return -EOPNOTSUPP;

	return 0;
}

int dw_edma_probe(struct dw_edma_chip *chip)
{
	struct device *dev;
	struct dw_edma *dw;
	u16 hw_wr_ch_cnt;
	u16 hw_rd_ch_cnt;
	u32 wr_alloc = 0;
	u32 rd_alloc = 0;
	u16 max_wr_cnt;
	u16 max_rd_cnt;
	int i, err;

	if (!chip)
		return -EINVAL;

	dev = chip->dev;
	if (!dev || !chip->ops)
		return -EINVAL;

	if (chip->flags & DW_EDMA_CHIP_PARTIAL) {
		switch (chip->mf) {
		case EDMA_MF_EDMA_UNROLL:
		case EDMA_MF_HDMA_COMPAT:
		case EDMA_MF_HDMA_NATIVE:
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	dw = devm_kzalloc(dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw->chip = chip;

	if (dw->chip->mf == EDMA_MF_HDMA_NATIVE) {
		dw_hdma_v0_core_register(dw);
		max_wr_cnt = HDMA_MAX_WR_CH;
		max_rd_cnt = HDMA_MAX_RD_CH;
	} else {
		dw_edma_v0_core_register(dw);
		max_wr_cnt = EDMA_MAX_WR_CH;
		max_rd_cnt = EDMA_MAX_RD_CH;
	}

	raw_spin_lock_init(&dw->lock);

	/*
	 * chip->ll_*_cnt describes the channels exposed by this instance. Keep
	 * the usable hardware counts separate for partial ownership checks.
	 */
	hw_wr_ch_cnt = min(dw_edma_core_ch_count(dw, EDMA_DIR_WRITE),
			   max_wr_cnt);
	hw_rd_ch_cnt = min(dw_edma_core_ch_count(dw, EDMA_DIR_READ),
			   max_rd_cnt);

	err = dw_edma_check_partial(chip, hw_wr_ch_cnt, hw_rd_ch_cnt);
	if (err)
		return err;

	dw->wr_ch_cnt = min(chip->ll_wr_cnt, hw_wr_ch_cnt);
	dw->rd_ch_cnt = min(chip->ll_rd_cnt, hw_rd_ch_cnt);

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

	if (chip->flags & DW_EDMA_CHIP_PARTIAL) {
		/*
		 * Do not reset the shared controller, but drain stale state
		 * from resources represented by this instance.
		 */
		err = dw_edma_core_quiesce(dw);
		if (err)
			return err;
	} else {
		/* Disable eDMA only when this instance owns the controller. */
		dw_edma_core_off(dw);
	}

	/*
	 * Deferred IRQ works are queued from the hard IRQ handlers, so the
	 * workqueue must exist before any IRQ is requested.
	 */
	dw->wq = alloc_workqueue("dw-edma:%s", WQ_UNBOUND | WQ_HIGHPRI, 0,
				 dev_name(chip->dev));
	if (!dw->wq)
		return -ENOMEM;

	/* Request IRQs */
	err = dw_edma_irq_request(dw, &wr_alloc, &rd_alloc);
	if (err) {
		destroy_workqueue(dw->wq);
		return err;
	}

	/* Allocate a dedicated virtual IRQ for interrupt-emulation doorbells */
	err = dw_edma_emul_irq_alloc(dw);
	if (err)
		dev_warn(dev, "Failed to allocate emulation IRQ: %d\n", err);

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
	dw_edma_emul_irq_free(dw);
	destroy_workqueue(dw->wq);

	return err;
}
EXPORT_SYMBOL_GPL(dw_edma_probe);

int dw_edma_remove(struct dw_edma_chip *chip)
{
	struct dw_edma_chan *chan, *_chan;
	struct device *dev = chip->dev;
	struct dw_edma *dw = chip->dw;
	int i, err = 0;

	/* Skip removal if no private data found */
	if (!dw)
		return -ENODEV;

	if (chip->flags & DW_EDMA_CHIP_PARTIAL)
		err = dw_edma_core_quiesce(dw);
	else
		dw_edma_core_off(dw);

	/* Free irqs */
	for (i = (dw->nr_irqs - 1); i >= 0; i--)
		free_irq(chip->ops->irq_vector(dev, i), &dw->irq[i]);
	dw_edma_emul_irq_free(dw);

	for (i = 0; i < dw->wr_ch_cnt + dw->rd_ch_cnt; i++)
		cancel_work_sync(&dw->chan[i].irq_work);

	destroy_workqueue(dw->wq);

	/* Deregister eDMA device */
	dma_async_device_unregister(&dw->dma);
	list_for_each_entry_safe(chan, _chan, &dw->dma.channels,
				 vc.chan.device_node) {
		tasklet_kill(&chan->vc.task);
		list_del(&chan->vc.chan.device_node);
	}

	return err;
}
EXPORT_SYMBOL_GPL(dw_edma_remove);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare eDMA controller core driver");
MODULE_AUTHOR("Gustavo Pimentel <gustavo.pimentel@synopsys.com>");
