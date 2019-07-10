// SPDX-License-Identifier: GPL-2.0-only
/*
 * copy offload engine support
 *
 * Copyright © 2006, Intel Corporation.
 *
 *      Dan Williams <dan.j.williams@intel.com>
 *
 *      with architecture considerations by:
 *      Neil Brown <neilb@suse.de>
 *      Jeff Garzik <jeff@garzik.org>
 */
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/async_tx.h>

/**
 * async_memcpy - attempt to copy memory with a dma engine.
 * @dest: destination page
 * @src: src page
 * @dest_offset: offset into 'dest' to start transaction
 * @src_offset: offset into 'src' to start transaction
 * @len: length in bytes
 * @submit: submission / completion modifiers
 *
 * honored flags: ASYNC_TX_ACK
 */
struct dma_async_tx_descriptor *
async_memcpy(struct page *dest, struct page *src, unsigned int dest_offset,
	     unsigned int src_offset, size_t len,
	     struct async_submit_ctl *submit)
{
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_MEMCPY,
						      &dest, 1, &src, 1, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	struct dmaengine_unmap_data *unmap = NULL;

	if (device)
		unmap = dmaengine_get_unmap_data(device->dev, 2, GFP_NOWAIT);

	if (unmap && is_dma_copy_aligned(device, src_offset, dest_offset, len)) {
		unsigned long dma_prep_flags = 0;

		if (submit->cb_fn)
			dma_prep_flags |= DMA_PREP_INTERRUPT;
		if (submit->flags & ASYNC_TX_FENCE)
			dma_prep_flags |= DMA_PREP_FENCE;

		unmap->to_cnt = 1;
		unmap->addr[0] = dma_map_page(device->dev, src, src_offset, len,
					      DMA_TO_DEVICE);
		unmap->from_cnt = 1;
		unmap->addr[1] = dma_map_page(device->dev, dest, dest_offset, len,
					      DMA_FROM_DEVICE);
		unmap->len = len;

		tx = device->device_prep_dma_memcpy(chan, unmap->addr[1],
						    unmap->addr[0], len,
						    dma_prep_flags);
	}

	if (tx) {
		pr_debug("%s: (async) len: %zu\n", __func__, len);

		dma_set_unmap(tx, unmap);
		async_tx_submit(chan, tx, submit);
	} else {
		void *dest_buf, *src_buf;
		pr_debug("%s: (sync) len: %zu\n", __func__, len);

		/* wait for any prerequisite operations */
		async_tx_quiesce(&submit->depend_tx);

		dest_buf = kmap_atomic(dest) + dest_offset;
		src_buf = kmap_atomic(src) + src_offset;

		memcpy(dest_buf, src_buf, len);

		kunmap_atomic(src_buf);
		kunmap_atomic(dest_buf);

		async_tx_sync_epilog(submit);
	}

	dmaengine_unmap_put(unmap);

	return tx;
}
EXPORT_SYMBOL_GPL(async_memcpy);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("asynchronous memcpy api");
MODULE_LICENSE("GPL");
