/*
 * Virtual DMA channel support for DMAengine
 *
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include "virt-dma.h"

static struct virt_dma_desc *to_virt_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct virt_dma_desc, tx);
}

dma_cookie_t vchan_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct virt_dma_chan *vc = to_virt_chan(tx->chan);
	struct virt_dma_desc *vd = to_virt_desc(tx);
	unsigned long flags;
	dma_cookie_t cookie;

	spin_lock_irqsave(&vc->lock, flags);
	cookie = dma_cookie_assign(tx);

	list_move_tail(&vd->node, &vc->desc_submitted);
	spin_unlock_irqrestore(&vc->lock, flags);

	dev_dbg(vc->chan.device->dev, "vchan %p: txd %p[%x]: submitted\n",
		vc, vd, cookie);

	return cookie;
}
EXPORT_SYMBOL_GPL(vchan_tx_submit);

/**
 * vchan_tx_desc_free - free a reusable descriptor
 * @tx: the transfer
 *
 * This function frees a previously allocated reusable descriptor. The only
 * other way is to clear the DMA_CTRL_REUSE flag and submit one last time the
 * transfer.
 *
 * Returns 0 upon success
 */
int vchan_tx_desc_free(struct dma_async_tx_descriptor *tx)
{
	struct virt_dma_chan *vc = to_virt_chan(tx->chan);
	struct virt_dma_desc *vd = to_virt_desc(tx);
	unsigned long flags;

	spin_lock_irqsave(&vc->lock, flags);
	list_del(&vd->node);
	spin_unlock_irqrestore(&vc->lock, flags);

	dev_dbg(vc->chan.device->dev, "vchan %p: txd %p[%x]: freeing\n",
		vc, vd, vd->tx.cookie);
	vc->desc_free(vd);
	return 0;
}
EXPORT_SYMBOL_GPL(vchan_tx_desc_free);

struct virt_dma_desc *vchan_find_desc(struct virt_dma_chan *vc,
	dma_cookie_t cookie)
{
	struct virt_dma_desc *vd;

	list_for_each_entry(vd, &vc->desc_issued, node)
		if (vd->tx.cookie == cookie)
			return vd;

	return NULL;
}
EXPORT_SYMBOL_GPL(vchan_find_desc);

/*
 * This tasklet handles the completion of a DMA descriptor by
 * calling its callback and freeing it.
 */
static void vchan_complete(unsigned long arg)
{
	struct virt_dma_chan *vc = (struct virt_dma_chan *)arg;
	struct virt_dma_desc *vd;
	dma_async_tx_callback cb = NULL;
	void *cb_data = NULL;
	LIST_HEAD(head);

	spin_lock_irq(&vc->lock);
	list_splice_tail_init(&vc->desc_completed, &head);
	vd = vc->cyclic;
	if (vd) {
		vc->cyclic = NULL;
		cb = vd->tx.callback;
		cb_data = vd->tx.callback_param;
	}
	spin_unlock_irq(&vc->lock);

	if (cb)
		cb(cb_data);

	while (!list_empty(&head)) {
		vd = list_first_entry(&head, struct virt_dma_desc, node);
		cb = vd->tx.callback;
		cb_data = vd->tx.callback_param;

		list_del(&vd->node);
		if (dmaengine_desc_test_reuse(&vd->tx))
			list_add(&vd->node, &vc->desc_allocated);
		else
			vc->desc_free(vd);

		if (cb)
			cb(cb_data);
	}
}

void vchan_dma_desc_free_list(struct virt_dma_chan *vc, struct list_head *head)
{
	while (!list_empty(head)) {
		struct virt_dma_desc *vd = list_first_entry(head,
			struct virt_dma_desc, node);
		if (dmaengine_desc_test_reuse(&vd->tx)) {
			list_move_tail(&vd->node, &vc->desc_allocated);
		} else {
			dev_dbg(vc->chan.device->dev, "txd %p: freeing\n", vd);
			list_del(&vd->node);
			vc->desc_free(vd);
		}
	}
}
EXPORT_SYMBOL_GPL(vchan_dma_desc_free_list);

void vchan_init(struct virt_dma_chan *vc, struct dma_device *dmadev)
{
	dma_cookie_init(&vc->chan);

	spin_lock_init(&vc->lock);
	INIT_LIST_HEAD(&vc->desc_allocated);
	INIT_LIST_HEAD(&vc->desc_submitted);
	INIT_LIST_HEAD(&vc->desc_issued);
	INIT_LIST_HEAD(&vc->desc_completed);

	tasklet_init(&vc->task, vchan_complete, (unsigned long)vc);

	vc->chan.device = dmadev;
	list_add_tail(&vc->chan.device_node, &dmadev->channels);
}
EXPORT_SYMBOL_GPL(vchan_init);

MODULE_AUTHOR("Russell King");
MODULE_LICENSE("GPL");
