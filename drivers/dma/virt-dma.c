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

	list_add_tail(&vd->node, &vc->desc_submitted);
	spin_unlock_irqrestore(&vc->lock, flags);

	dev_dbg(vc->chan.device->dev, "vchan %p: txd %p[%x]: submitted\n",
		vc, vd, cookie);

	return cookie;
}
EXPORT_SYMBOL_GPL(vchan_tx_submit);

/*
 * This tasklet handles the completion of a DMA descriptor by
 * calling its callback and freeing it.
 */
static void vchan_complete(unsigned long arg)
{
	struct virt_dma_chan *vc = (struct virt_dma_chan *)arg;
	LIST_HEAD(head);

	spin_lock_irq(&vc->lock);
	list_splice_tail_init(&vc->desc_completed, &head);
	spin_unlock_irq(&vc->lock);

	while (!list_empty(&head)) {
		struct virt_dma_desc *vd = list_first_entry(&head,
				struct virt_dma_desc, node);
		dma_async_tx_callback cb = vd->tx.callback;
		void *cb_data = vd->tx.callback_param;

		list_del(&vd->node);

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
		list_del(&vd->node);
		dev_dbg(vc->chan.device->dev, "txd %p: freeing\n", vd);
		vc->desc_free(vd);
	}
}
EXPORT_SYMBOL_GPL(vchan_dma_desc_free_list);

void vchan_init(struct virt_dma_chan *vc, struct dma_device *dmadev)
{
	dma_cookie_init(&vc->chan);

	spin_lock_init(&vc->lock);
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
