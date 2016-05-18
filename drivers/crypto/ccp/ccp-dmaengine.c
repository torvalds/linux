/*
 * AMD Cryptographic Coprocessor (CCP) driver
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dmaengine.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/ccp.h>

#include "ccp-dev.h"
#include "../../dma/dmaengine.h"

#define CCP_DMA_WIDTH(_mask)		\
({					\
	u64 mask = _mask + 1;		\
	(mask == 0) ? 64 : fls64(mask);	\
})

static void ccp_free_cmd_resources(struct ccp_device *ccp,
				   struct list_head *list)
{
	struct ccp_dma_cmd *cmd, *ctmp;

	list_for_each_entry_safe(cmd, ctmp, list, entry) {
		list_del(&cmd->entry);
		kmem_cache_free(ccp->dma_cmd_cache, cmd);
	}
}

static void ccp_free_desc_resources(struct ccp_device *ccp,
				    struct list_head *list)
{
	struct ccp_dma_desc *desc, *dtmp;

	list_for_each_entry_safe(desc, dtmp, list, entry) {
		ccp_free_cmd_resources(ccp, &desc->active);
		ccp_free_cmd_resources(ccp, &desc->pending);

		list_del(&desc->entry);
		kmem_cache_free(ccp->dma_desc_cache, desc);
	}
}

static void ccp_free_chan_resources(struct dma_chan *dma_chan)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	unsigned long flags;

	dev_dbg(chan->ccp->dev, "%s - chan=%p\n", __func__, chan);

	spin_lock_irqsave(&chan->lock, flags);

	ccp_free_desc_resources(chan->ccp, &chan->complete);
	ccp_free_desc_resources(chan->ccp, &chan->active);
	ccp_free_desc_resources(chan->ccp, &chan->pending);

	spin_unlock_irqrestore(&chan->lock, flags);
}

static void ccp_cleanup_desc_resources(struct ccp_device *ccp,
				       struct list_head *list)
{
	struct ccp_dma_desc *desc, *dtmp;

	list_for_each_entry_safe_reverse(desc, dtmp, list, entry) {
		if (!async_tx_test_ack(&desc->tx_desc))
			continue;

		dev_dbg(ccp->dev, "%s - desc=%p\n", __func__, desc);

		ccp_free_cmd_resources(ccp, &desc->active);
		ccp_free_cmd_resources(ccp, &desc->pending);

		list_del(&desc->entry);
		kmem_cache_free(ccp->dma_desc_cache, desc);
	}
}

static void ccp_do_cleanup(unsigned long data)
{
	struct ccp_dma_chan *chan = (struct ccp_dma_chan *)data;
	unsigned long flags;

	dev_dbg(chan->ccp->dev, "%s - chan=%s\n", __func__,
		dma_chan_name(&chan->dma_chan));

	spin_lock_irqsave(&chan->lock, flags);

	ccp_cleanup_desc_resources(chan->ccp, &chan->complete);

	spin_unlock_irqrestore(&chan->lock, flags);
}

static int ccp_issue_next_cmd(struct ccp_dma_desc *desc)
{
	struct ccp_dma_cmd *cmd;
	int ret;

	cmd = list_first_entry(&desc->pending, struct ccp_dma_cmd, entry);
	list_move(&cmd->entry, &desc->active);

	dev_dbg(desc->ccp->dev, "%s - tx %d, cmd=%p\n", __func__,
		desc->tx_desc.cookie, cmd);

	ret = ccp_enqueue_cmd(&cmd->ccp_cmd);
	if (!ret || (ret == -EINPROGRESS) || (ret == -EBUSY))
		return 0;

	dev_dbg(desc->ccp->dev, "%s - error: ret=%d, tx %d, cmd=%p\n", __func__,
		ret, desc->tx_desc.cookie, cmd);

	return ret;
}

static void ccp_free_active_cmd(struct ccp_dma_desc *desc)
{
	struct ccp_dma_cmd *cmd;

	cmd = list_first_entry_or_null(&desc->active, struct ccp_dma_cmd,
				       entry);
	if (!cmd)
		return;

	dev_dbg(desc->ccp->dev, "%s - freeing tx %d cmd=%p\n",
		__func__, desc->tx_desc.cookie, cmd);

	list_del(&cmd->entry);
	kmem_cache_free(desc->ccp->dma_cmd_cache, cmd);
}

static struct ccp_dma_desc *__ccp_next_dma_desc(struct ccp_dma_chan *chan,
						struct ccp_dma_desc *desc)
{
	/* Move current DMA descriptor to the complete list */
	if (desc)
		list_move(&desc->entry, &chan->complete);

	/* Get the next DMA descriptor on the active list */
	desc = list_first_entry_or_null(&chan->active, struct ccp_dma_desc,
					entry);

	return desc;
}

static struct ccp_dma_desc *ccp_handle_active_desc(struct ccp_dma_chan *chan,
						   struct ccp_dma_desc *desc)
{
	struct dma_async_tx_descriptor *tx_desc;
	unsigned long flags;

	/* Loop over descriptors until one is found with commands */
	do {
		if (desc) {
			/* Remove the DMA command from the list and free it */
			ccp_free_active_cmd(desc);

			if (!list_empty(&desc->pending)) {
				/* No errors, keep going */
				if (desc->status != DMA_ERROR)
					return desc;

				/* Error, free remaining commands and move on */
				ccp_free_cmd_resources(desc->ccp,
						       &desc->pending);
			}

			tx_desc = &desc->tx_desc;
		} else {
			tx_desc = NULL;
		}

		spin_lock_irqsave(&chan->lock, flags);

		if (desc) {
			if (desc->status != DMA_ERROR)
				desc->status = DMA_COMPLETE;

			dev_dbg(desc->ccp->dev,
				"%s - tx %d complete, status=%u\n", __func__,
				desc->tx_desc.cookie, desc->status);

			dma_cookie_complete(tx_desc);
		}

		desc = __ccp_next_dma_desc(chan, desc);

		spin_unlock_irqrestore(&chan->lock, flags);

		if (tx_desc) {
			if (tx_desc->callback &&
			    (tx_desc->flags & DMA_PREP_INTERRUPT))
				tx_desc->callback(tx_desc->callback_param);

			dma_run_dependencies(tx_desc);
		}
	} while (desc);

	return NULL;
}

static struct ccp_dma_desc *__ccp_pending_to_active(struct ccp_dma_chan *chan)
{
	struct ccp_dma_desc *desc;

	if (list_empty(&chan->pending))
		return NULL;

	desc = list_empty(&chan->active)
		? list_first_entry(&chan->pending, struct ccp_dma_desc, entry)
		: NULL;

	list_splice_tail_init(&chan->pending, &chan->active);

	return desc;
}

static void ccp_cmd_callback(void *data, int err)
{
	struct ccp_dma_desc *desc = data;
	struct ccp_dma_chan *chan;
	int ret;

	if (err == -EINPROGRESS)
		return;

	chan = container_of(desc->tx_desc.chan, struct ccp_dma_chan,
			    dma_chan);

	dev_dbg(chan->ccp->dev, "%s - tx %d callback, err=%d\n",
		__func__, desc->tx_desc.cookie, err);

	if (err)
		desc->status = DMA_ERROR;

	while (true) {
		/* Check for DMA descriptor completion */
		desc = ccp_handle_active_desc(chan, desc);

		/* Don't submit cmd if no descriptor or DMA is paused */
		if (!desc || (chan->status == DMA_PAUSED))
			break;

		ret = ccp_issue_next_cmd(desc);
		if (!ret)
			break;

		desc->status = DMA_ERROR;
	}

	tasklet_schedule(&chan->cleanup_tasklet);
}

static dma_cookie_t ccp_tx_submit(struct dma_async_tx_descriptor *tx_desc)
{
	struct ccp_dma_desc *desc = container_of(tx_desc, struct ccp_dma_desc,
						 tx_desc);
	struct ccp_dma_chan *chan;
	dma_cookie_t cookie;
	unsigned long flags;

	chan = container_of(tx_desc->chan, struct ccp_dma_chan, dma_chan);

	spin_lock_irqsave(&chan->lock, flags);

	cookie = dma_cookie_assign(tx_desc);
	list_add_tail(&desc->entry, &chan->pending);

	spin_unlock_irqrestore(&chan->lock, flags);

	dev_dbg(chan->ccp->dev, "%s - added tx descriptor %d to pending list\n",
		__func__, cookie);

	return cookie;
}

static struct ccp_dma_cmd *ccp_alloc_dma_cmd(struct ccp_dma_chan *chan)
{
	struct ccp_dma_cmd *cmd;

	cmd = kmem_cache_alloc(chan->ccp->dma_cmd_cache, GFP_NOWAIT);
	if (cmd)
		memset(cmd, 0, sizeof(*cmd));

	return cmd;
}

static struct ccp_dma_desc *ccp_alloc_dma_desc(struct ccp_dma_chan *chan,
					       unsigned long flags)
{
	struct ccp_dma_desc *desc;

	desc = kmem_cache_alloc(chan->ccp->dma_desc_cache, GFP_NOWAIT);
	if (!desc)
		return NULL;

	memset(desc, 0, sizeof(*desc));

	dma_async_tx_descriptor_init(&desc->tx_desc, &chan->dma_chan);
	desc->tx_desc.flags = flags;
	desc->tx_desc.tx_submit = ccp_tx_submit;
	desc->ccp = chan->ccp;
	INIT_LIST_HEAD(&desc->pending);
	INIT_LIST_HEAD(&desc->active);
	desc->status = DMA_IN_PROGRESS;

	return desc;
}

static struct ccp_dma_desc *ccp_create_desc(struct dma_chan *dma_chan,
					    struct scatterlist *dst_sg,
					    unsigned int dst_nents,
					    struct scatterlist *src_sg,
					    unsigned int src_nents,
					    unsigned long flags)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_device *ccp = chan->ccp;
	struct ccp_dma_desc *desc;
	struct ccp_dma_cmd *cmd;
	struct ccp_cmd *ccp_cmd;
	struct ccp_passthru_nomap_engine *ccp_pt;
	unsigned int src_offset, src_len;
	unsigned int dst_offset, dst_len;
	unsigned int len;
	unsigned long sflags;
	size_t total_len;

	if (!dst_sg || !src_sg)
		return NULL;

	if (!dst_nents || !src_nents)
		return NULL;

	desc = ccp_alloc_dma_desc(chan, flags);
	if (!desc)
		return NULL;

	total_len = 0;

	src_len = sg_dma_len(src_sg);
	src_offset = 0;

	dst_len = sg_dma_len(dst_sg);
	dst_offset = 0;

	while (true) {
		if (!src_len) {
			src_nents--;
			if (!src_nents)
				break;

			src_sg = sg_next(src_sg);
			if (!src_sg)
				break;

			src_len = sg_dma_len(src_sg);
			src_offset = 0;
			continue;
		}

		if (!dst_len) {
			dst_nents--;
			if (!dst_nents)
				break;

			dst_sg = sg_next(dst_sg);
			if (!dst_sg)
				break;

			dst_len = sg_dma_len(dst_sg);
			dst_offset = 0;
			continue;
		}

		len = min(dst_len, src_len);

		cmd = ccp_alloc_dma_cmd(chan);
		if (!cmd)
			goto err;

		ccp_cmd = &cmd->ccp_cmd;
		ccp_pt = &ccp_cmd->u.passthru_nomap;
		ccp_cmd->flags = CCP_CMD_MAY_BACKLOG;
		ccp_cmd->flags |= CCP_CMD_PASSTHRU_NO_DMA_MAP;
		ccp_cmd->engine = CCP_ENGINE_PASSTHRU;
		ccp_pt->bit_mod = CCP_PASSTHRU_BITWISE_NOOP;
		ccp_pt->byte_swap = CCP_PASSTHRU_BYTESWAP_NOOP;
		ccp_pt->src_dma = sg_dma_address(src_sg) + src_offset;
		ccp_pt->dst_dma = sg_dma_address(dst_sg) + dst_offset;
		ccp_pt->src_len = len;
		ccp_pt->final = 1;
		ccp_cmd->callback = ccp_cmd_callback;
		ccp_cmd->data = desc;

		list_add_tail(&cmd->entry, &desc->pending);

		dev_dbg(ccp->dev,
			"%s - cmd=%p, src=%pad, dst=%pad, len=%llu\n", __func__,
			cmd, &ccp_pt->src_dma,
			&ccp_pt->dst_dma, ccp_pt->src_len);

		total_len += len;

		src_len -= len;
		src_offset += len;

		dst_len -= len;
		dst_offset += len;
	}

	desc->len = total_len;

	if (list_empty(&desc->pending))
		goto err;

	dev_dbg(ccp->dev, "%s - desc=%p\n", __func__, desc);

	spin_lock_irqsave(&chan->lock, sflags);

	list_add_tail(&desc->entry, &chan->pending);

	spin_unlock_irqrestore(&chan->lock, sflags);

	return desc;

err:
	ccp_free_cmd_resources(ccp, &desc->pending);
	kmem_cache_free(ccp->dma_desc_cache, desc);

	return NULL;
}

static struct dma_async_tx_descriptor *ccp_prep_dma_memcpy(
	struct dma_chan *dma_chan, dma_addr_t dst, dma_addr_t src, size_t len,
	unsigned long flags)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_dma_desc *desc;
	struct scatterlist dst_sg, src_sg;

	dev_dbg(chan->ccp->dev,
		"%s - src=%pad, dst=%pad, len=%zu, flags=%#lx\n",
		__func__, &src, &dst, len, flags);

	sg_init_table(&dst_sg, 1);
	sg_dma_address(&dst_sg) = dst;
	sg_dma_len(&dst_sg) = len;

	sg_init_table(&src_sg, 1);
	sg_dma_address(&src_sg) = src;
	sg_dma_len(&src_sg) = len;

	desc = ccp_create_desc(dma_chan, &dst_sg, 1, &src_sg, 1, flags);
	if (!desc)
		return NULL;

	return &desc->tx_desc;
}

static struct dma_async_tx_descriptor *ccp_prep_dma_sg(
	struct dma_chan *dma_chan, struct scatterlist *dst_sg,
	unsigned int dst_nents, struct scatterlist *src_sg,
	unsigned int src_nents, unsigned long flags)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_dma_desc *desc;

	dev_dbg(chan->ccp->dev,
		"%s - src=%p, src_nents=%u dst=%p, dst_nents=%u, flags=%#lx\n",
		__func__, src_sg, src_nents, dst_sg, dst_nents, flags);

	desc = ccp_create_desc(dma_chan, dst_sg, dst_nents, src_sg, src_nents,
			       flags);
	if (!desc)
		return NULL;

	return &desc->tx_desc;
}

static struct dma_async_tx_descriptor *ccp_prep_dma_interrupt(
	struct dma_chan *dma_chan, unsigned long flags)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_dma_desc *desc;

	desc = ccp_alloc_dma_desc(chan, flags);
	if (!desc)
		return NULL;

	return &desc->tx_desc;
}

static void ccp_issue_pending(struct dma_chan *dma_chan)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_dma_desc *desc;
	unsigned long flags;

	dev_dbg(chan->ccp->dev, "%s\n", __func__);

	spin_lock_irqsave(&chan->lock, flags);

	desc = __ccp_pending_to_active(chan);

	spin_unlock_irqrestore(&chan->lock, flags);

	/* If there was nothing active, start processing */
	if (desc)
		ccp_cmd_callback(desc, 0);
}

static enum dma_status ccp_tx_status(struct dma_chan *dma_chan,
				     dma_cookie_t cookie,
				     struct dma_tx_state *state)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_dma_desc *desc;
	enum dma_status ret;
	unsigned long flags;

	if (chan->status == DMA_PAUSED) {
		ret = DMA_PAUSED;
		goto out;
	}

	ret = dma_cookie_status(dma_chan, cookie, state);
	if (ret == DMA_COMPLETE) {
		spin_lock_irqsave(&chan->lock, flags);

		/* Get status from complete chain, if still there */
		list_for_each_entry(desc, &chan->complete, entry) {
			if (desc->tx_desc.cookie != cookie)
				continue;

			ret = desc->status;
			break;
		}

		spin_unlock_irqrestore(&chan->lock, flags);
	}

out:
	dev_dbg(chan->ccp->dev, "%s - %u\n", __func__, ret);

	return ret;
}

static int ccp_pause(struct dma_chan *dma_chan)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);

	chan->status = DMA_PAUSED;

	/*TODO: Wait for active DMA to complete before returning? */

	return 0;
}

static int ccp_resume(struct dma_chan *dma_chan)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	struct ccp_dma_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	desc = list_first_entry_or_null(&chan->active, struct ccp_dma_desc,
					entry);

	spin_unlock_irqrestore(&chan->lock, flags);

	/* Indicate the channel is running again */
	chan->status = DMA_IN_PROGRESS;

	/* If there was something active, re-start */
	if (desc)
		ccp_cmd_callback(desc, 0);

	return 0;
}

static int ccp_terminate_all(struct dma_chan *dma_chan)
{
	struct ccp_dma_chan *chan = container_of(dma_chan, struct ccp_dma_chan,
						 dma_chan);
	unsigned long flags;

	dev_dbg(chan->ccp->dev, "%s\n", __func__);

	/*TODO: Wait for active DMA to complete before continuing */

	spin_lock_irqsave(&chan->lock, flags);

	/*TODO: Purge the complete list? */
	ccp_free_desc_resources(chan->ccp, &chan->active);
	ccp_free_desc_resources(chan->ccp, &chan->pending);

	spin_unlock_irqrestore(&chan->lock, flags);

	return 0;
}

int ccp_dmaengine_register(struct ccp_device *ccp)
{
	struct ccp_dma_chan *chan;
	struct dma_device *dma_dev = &ccp->dma_dev;
	struct dma_chan *dma_chan;
	char *dma_cmd_cache_name;
	char *dma_desc_cache_name;
	unsigned int i;
	int ret;

	ccp->ccp_dma_chan = devm_kcalloc(ccp->dev, ccp->cmd_q_count,
					 sizeof(*(ccp->ccp_dma_chan)),
					 GFP_KERNEL);
	if (!ccp->ccp_dma_chan)
		return -ENOMEM;

	dma_cmd_cache_name = devm_kasprintf(ccp->dev, GFP_KERNEL,
					    "%s-dmaengine-cmd-cache",
					    ccp->name);
	if (!dma_cmd_cache_name)
		return -ENOMEM;

	ccp->dma_cmd_cache = kmem_cache_create(dma_cmd_cache_name,
					       sizeof(struct ccp_dma_cmd),
					       sizeof(void *),
					       SLAB_HWCACHE_ALIGN, NULL);
	if (!ccp->dma_cmd_cache)
		return -ENOMEM;

	dma_desc_cache_name = devm_kasprintf(ccp->dev, GFP_KERNEL,
					     "%s-dmaengine-desc-cache",
					     ccp->name);
	if (!dma_cmd_cache_name)
		return -ENOMEM;
	ccp->dma_desc_cache = kmem_cache_create(dma_desc_cache_name,
						sizeof(struct ccp_dma_desc),
						sizeof(void *),
						SLAB_HWCACHE_ALIGN, NULL);
	if (!ccp->dma_desc_cache) {
		ret = -ENOMEM;
		goto err_cache;
	}

	dma_dev->dev = ccp->dev;
	dma_dev->src_addr_widths = CCP_DMA_WIDTH(dma_get_mask(ccp->dev));
	dma_dev->dst_addr_widths = CCP_DMA_WIDTH(dma_get_mask(ccp->dev));
	dma_dev->directions = DMA_MEM_TO_MEM;
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_SG, dma_dev->cap_mask);
	dma_cap_set(DMA_INTERRUPT, dma_dev->cap_mask);

	INIT_LIST_HEAD(&dma_dev->channels);
	for (i = 0; i < ccp->cmd_q_count; i++) {
		chan = ccp->ccp_dma_chan + i;
		dma_chan = &chan->dma_chan;

		chan->ccp = ccp;

		spin_lock_init(&chan->lock);
		INIT_LIST_HEAD(&chan->pending);
		INIT_LIST_HEAD(&chan->active);
		INIT_LIST_HEAD(&chan->complete);

		tasklet_init(&chan->cleanup_tasklet, ccp_do_cleanup,
			     (unsigned long)chan);

		dma_chan->device = dma_dev;
		dma_cookie_init(dma_chan);

		list_add_tail(&dma_chan->device_node, &dma_dev->channels);
	}

	dma_dev->device_free_chan_resources = ccp_free_chan_resources;
	dma_dev->device_prep_dma_memcpy = ccp_prep_dma_memcpy;
	dma_dev->device_prep_dma_sg = ccp_prep_dma_sg;
	dma_dev->device_prep_dma_interrupt = ccp_prep_dma_interrupt;
	dma_dev->device_issue_pending = ccp_issue_pending;
	dma_dev->device_tx_status = ccp_tx_status;
	dma_dev->device_pause = ccp_pause;
	dma_dev->device_resume = ccp_resume;
	dma_dev->device_terminate_all = ccp_terminate_all;

	ret = dma_async_device_register(dma_dev);
	if (ret)
		goto err_reg;

	return 0;

err_reg:
	kmem_cache_destroy(ccp->dma_desc_cache);

err_cache:
	kmem_cache_destroy(ccp->dma_cmd_cache);

	return ret;
}

void ccp_dmaengine_unregister(struct ccp_device *ccp)
{
	struct dma_device *dma_dev = &ccp->dma_dev;

	dma_async_device_unregister(dma_dev);

	kmem_cache_destroy(ccp->dma_desc_cache);
	kmem_cache_destroy(ccp->dma_cmd_cache);
}
