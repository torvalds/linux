// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Passthrough DMA device driver
 * -- Based on the CCP driver
 *
 * Copyright (C) 2016,2021 Advanced Micro Devices, Inc.
 *
 * Author: Sanjay R Mehta <sanju.mehta@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include "ptdma.h"
#include "../dmaengine.h"
#include "../virt-dma.h"

static inline struct pt_dma_chan *to_pt_chan(struct dma_chan *dma_chan)
{
	return container_of(dma_chan, struct pt_dma_chan, vc.chan);
}

static inline struct pt_dma_desc *to_pt_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct pt_dma_desc, vd);
}

static void pt_free_chan_resources(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);

	vchan_free_chan_resources(&chan->vc);
}

static void pt_synchronize(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);

	vchan_synchronize(&chan->vc);
}

static void pt_do_cleanup(struct virt_dma_desc *vd)
{
	struct pt_dma_desc *desc = to_pt_desc(vd);
	struct pt_device *pt = desc->pt;

	kmem_cache_free(pt->dma_desc_cache, desc);
}

static int pt_dma_start_desc(struct pt_dma_desc *desc)
{
	struct pt_passthru_engine *pt_engine;
	struct pt_device *pt;
	struct pt_cmd *pt_cmd;
	struct pt_cmd_queue *cmd_q;

	desc->issued_to_hw = 1;

	pt_cmd = &desc->pt_cmd;
	pt = pt_cmd->pt;
	cmd_q = &pt->cmd_q;
	pt_engine = &pt_cmd->passthru;

	pt->tdata.cmd = pt_cmd;

	/* Execute the command */
	pt_cmd->ret = pt_core_perform_passthru(cmd_q, pt_engine);

	return 0;
}

static struct pt_dma_desc *pt_next_dma_desc(struct pt_dma_chan *chan)
{
	/* Get the next DMA descriptor on the active list */
	struct virt_dma_desc *vd = vchan_next_desc(&chan->vc);

	return vd ? to_pt_desc(vd) : NULL;
}

static struct pt_dma_desc *pt_handle_active_desc(struct pt_dma_chan *chan,
						 struct pt_dma_desc *desc)
{
	struct dma_async_tx_descriptor *tx_desc;
	struct virt_dma_desc *vd;
	unsigned long flags;

	/* Loop over descriptors until one is found with commands */
	do {
		if (desc) {
			if (!desc->issued_to_hw) {
				/* No errors, keep going */
				if (desc->status != DMA_ERROR)
					return desc;
			}

			tx_desc = &desc->vd.tx;
			vd = &desc->vd;
		} else {
			tx_desc = NULL;
		}

		spin_lock_irqsave(&chan->vc.lock, flags);

		if (desc) {
			if (desc->status != DMA_COMPLETE) {
				if (desc->status != DMA_ERROR)
					desc->status = DMA_COMPLETE;

				dma_cookie_complete(tx_desc);
				dma_descriptor_unmap(tx_desc);
				list_del(&desc->vd.node);
			} else {
				/* Don't handle it twice */
				tx_desc = NULL;
			}
		}

		desc = pt_next_dma_desc(chan);

		spin_unlock_irqrestore(&chan->vc.lock, flags);

		if (tx_desc) {
			dmaengine_desc_get_callback_invoke(tx_desc, NULL);
			dma_run_dependencies(tx_desc);
			vchan_vdesc_fini(vd);
		}
	} while (desc);

	return NULL;
}

static void pt_cmd_callback(void *data, int err)
{
	struct pt_dma_desc *desc = data;
	struct dma_chan *dma_chan;
	struct pt_dma_chan *chan;
	int ret;

	if (err == -EINPROGRESS)
		return;

	dma_chan = desc->vd.tx.chan;
	chan = to_pt_chan(dma_chan);

	if (err)
		desc->status = DMA_ERROR;

	while (true) {
		/* Check for DMA descriptor completion */
		desc = pt_handle_active_desc(chan, desc);

		/* Don't submit cmd if no descriptor or DMA is paused */
		if (!desc)
			break;

		ret = pt_dma_start_desc(desc);
		if (!ret)
			break;

		desc->status = DMA_ERROR;
	}
}

static struct pt_dma_desc *pt_alloc_dma_desc(struct pt_dma_chan *chan,
					     unsigned long flags)
{
	struct pt_dma_desc *desc;

	desc = kmem_cache_zalloc(chan->pt->dma_desc_cache, GFP_NOWAIT);
	if (!desc)
		return NULL;

	vchan_tx_prep(&chan->vc, &desc->vd, flags);

	desc->pt = chan->pt;
	desc->pt->cmd_q.int_en = !!(flags & DMA_PREP_INTERRUPT);
	desc->issued_to_hw = 0;
	desc->status = DMA_IN_PROGRESS;

	return desc;
}

static struct pt_dma_desc *pt_create_desc(struct dma_chan *dma_chan,
					  dma_addr_t dst,
					  dma_addr_t src,
					  unsigned int len,
					  unsigned long flags)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_passthru_engine *pt_engine;
	struct pt_dma_desc *desc;
	struct pt_cmd *pt_cmd;

	desc = pt_alloc_dma_desc(chan, flags);
	if (!desc)
		return NULL;

	pt_cmd = &desc->pt_cmd;
	pt_cmd->pt = chan->pt;
	pt_engine = &pt_cmd->passthru;
	pt_cmd->engine = PT_ENGINE_PASSTHRU;
	pt_engine->src_dma = src;
	pt_engine->dst_dma = dst;
	pt_engine->src_len = len;
	pt_cmd->pt_cmd_callback = pt_cmd_callback;
	pt_cmd->data = desc;

	desc->len = len;

	return desc;
}

static struct dma_async_tx_descriptor *
pt_prep_dma_memcpy(struct dma_chan *dma_chan, dma_addr_t dst,
		   dma_addr_t src, size_t len, unsigned long flags)
{
	struct pt_dma_desc *desc;

	desc = pt_create_desc(dma_chan, dst, src, len, flags);
	if (!desc)
		return NULL;

	return &desc->vd.tx;
}

static struct dma_async_tx_descriptor *
pt_prep_dma_interrupt(struct dma_chan *dma_chan, unsigned long flags)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_dma_desc *desc;

	desc = pt_alloc_dma_desc(chan, flags);
	if (!desc)
		return NULL;

	return &desc->vd.tx;
}

static void pt_issue_pending(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_dma_desc *desc;
	unsigned long flags;
	bool engine_is_idle = true;

	spin_lock_irqsave(&chan->vc.lock, flags);

	desc = pt_next_dma_desc(chan);
	if (desc)
		engine_is_idle = false;

	vchan_issue_pending(&chan->vc);

	desc = pt_next_dma_desc(chan);

	spin_unlock_irqrestore(&chan->vc.lock, flags);

	/* If there was nothing active, start processing */
	if (engine_is_idle)
		pt_cmd_callback(desc, 0);
}

static enum dma_status
pt_tx_status(struct dma_chan *c, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct pt_device *pt = to_pt_chan(c)->pt;
	struct pt_cmd_queue *cmd_q = &pt->cmd_q;

	pt_check_status_trans(pt, cmd_q);
	return dma_cookie_status(c, cookie, txstate);
}

static int pt_pause(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	pt_stop_queue(&chan->pt->cmd_q);
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	return 0;
}

static int pt_resume(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	struct pt_dma_desc *desc = NULL;
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);
	pt_start_queue(&chan->pt->cmd_q);
	desc = pt_next_dma_desc(chan);
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	/* If there was something active, re-start */
	if (desc)
		pt_cmd_callback(desc, 0);

	return 0;
}

static int pt_terminate_all(struct dma_chan *dma_chan)
{
	struct pt_dma_chan *chan = to_pt_chan(dma_chan);
	unsigned long flags;
	struct pt_cmd_queue *cmd_q = &chan->pt->cmd_q;
	LIST_HEAD(head);

	iowrite32(SUPPORTED_INTERRUPTS, cmd_q->reg_control + 0x0010);
	spin_lock_irqsave(&chan->vc.lock, flags);
	vchan_get_all_descriptors(&chan->vc, &head);
	spin_unlock_irqrestore(&chan->vc.lock, flags);

	vchan_dma_desc_free_list(&chan->vc, &head);
	vchan_free_chan_resources(&chan->vc);

	return 0;
}

int pt_dmaengine_register(struct pt_device *pt)
{
	struct pt_dma_chan *chan;
	struct dma_device *dma_dev = &pt->dma_dev;
	char *cmd_cache_name;
	char *desc_cache_name;
	int ret;

	pt->pt_dma_chan = devm_kzalloc(pt->dev, sizeof(*pt->pt_dma_chan),
				       GFP_KERNEL);
	if (!pt->pt_dma_chan)
		return -ENOMEM;

	cmd_cache_name = devm_kasprintf(pt->dev, GFP_KERNEL,
					"%s-dmaengine-cmd-cache",
					dev_name(pt->dev));
	if (!cmd_cache_name)
		return -ENOMEM;

	desc_cache_name = devm_kasprintf(pt->dev, GFP_KERNEL,
					 "%s-dmaengine-desc-cache",
					 dev_name(pt->dev));
	if (!desc_cache_name) {
		ret = -ENOMEM;
		goto err_cache;
	}

	pt->dma_desc_cache = kmem_cache_create(desc_cache_name,
					       sizeof(struct pt_dma_desc), 0,
					       SLAB_HWCACHE_ALIGN, NULL);
	if (!pt->dma_desc_cache) {
		ret = -ENOMEM;
		goto err_cache;
	}

	dma_dev->dev = pt->dev;
	dma_dev->src_addr_widths = DMA_SLAVE_BUSWIDTH_64_BYTES;
	dma_dev->dst_addr_widths = DMA_SLAVE_BUSWIDTH_64_BYTES;
	dma_dev->directions = DMA_MEM_TO_MEM;
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_INTERRUPT, dma_dev->cap_mask);

	/*
	 * PTDMA is intended to be used with the AMD NTB devices, hence
	 * marking it as DMA_PRIVATE.
	 */
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);

	INIT_LIST_HEAD(&dma_dev->channels);

	chan = pt->pt_dma_chan;
	chan->pt = pt;

	/* Set base and prep routines */
	dma_dev->device_free_chan_resources = pt_free_chan_resources;
	dma_dev->device_prep_dma_memcpy = pt_prep_dma_memcpy;
	dma_dev->device_prep_dma_interrupt = pt_prep_dma_interrupt;
	dma_dev->device_issue_pending = pt_issue_pending;
	dma_dev->device_tx_status = pt_tx_status;
	dma_dev->device_pause = pt_pause;
	dma_dev->device_resume = pt_resume;
	dma_dev->device_terminate_all = pt_terminate_all;
	dma_dev->device_synchronize = pt_synchronize;

	chan->vc.desc_free = pt_do_cleanup;
	vchan_init(&chan->vc, dma_dev);

	dma_set_mask_and_coherent(pt->dev, DMA_BIT_MASK(64));

	ret = dma_async_device_register(dma_dev);
	if (ret)
		goto err_reg;

	return 0;

err_reg:
	kmem_cache_destroy(pt->dma_desc_cache);

err_cache:
	kmem_cache_destroy(pt->dma_cmd_cache);

	return ret;
}

void pt_dmaengine_unregister(struct pt_device *pt)
{
	struct dma_device *dma_dev = &pt->dma_dev;

	dma_async_device_unregister(dma_dev);

	kmem_cache_destroy(pt->dma_desc_cache);
	kmem_cache_destroy(pt->dma_cmd_cache);
}
