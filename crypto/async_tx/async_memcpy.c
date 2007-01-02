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
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/async_tx.h>

/**
 * async_memcpy - attempt to copy memory with a dma engine.
 * @dest: destination page
 * @src: src page
 * @offset: offset in pages to start transaction
 * @len: length in bytes
 * @flags: ASYNC_TX_ASSUME_COHERENT, ASYNC_TX_ACK, ASYNC_TX_DEP_ACK,
 *	ASYNC_TX_KMAP_SRC, ASYNC_TX_KMAP_DST
 * @depend_tx: memcpy depends on the result of this transaction
 * @cb_fn: function to call when the memcpy completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_memcpy(struct page *dest, struct page *src, unsigned int dest_offset,
	unsigned int src_offset, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_MEMCPY);
	struct dma_device *device = chan ? chan->device : NULL;
	int int_en = cb_fn ? 1 : 0;
	struct dma_async_tx_descriptor *tx = device ?
		device->device_prep_dma_memcpy(chan, len,
		int_en) : NULL;

	if (tx) { /* run the memcpy asynchronously */
		dma_addr_t addr;
		enum dma_data_direction dir;

		pr_debug("%s: (async) len: %zu\n", __FUNCTION__, len);

		dir = (flags & ASYNC_TX_ASSUME_COHERENT) ?
			DMA_NONE : DMA_FROM_DEVICE;

		addr = dma_map_page(device->dev, dest, dest_offset, len, dir);
		tx->tx_set_dest(addr, tx, 0);

		dir = (flags & ASYNC_TX_ASSUME_COHERENT) ?
			DMA_NONE : DMA_TO_DEVICE;

		addr = dma_map_page(device->dev, src, src_offset, len, dir);
		tx->tx_set_src(addr, tx, 0);

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else { /* run the memcpy synchronously */
		void *dest_buf, *src_buf;
		pr_debug("%s: (sync) len: %zu\n", __FUNCTION__, len);

		/* wait for any prerequisite operations */
		if (depend_tx) {
			/* if ack is already set then we cannot be sure
			 * we are referring to the correct operation
			 */
			BUG_ON(depend_tx->ack);
			if (dma_wait_for_async_tx(depend_tx) == DMA_ERROR)
				panic("%s: DMA_ERROR waiting for depend_tx\n",
					__FUNCTION__);
		}

		if (flags & ASYNC_TX_KMAP_DST)
			dest_buf = kmap_atomic(dest, KM_USER0) + dest_offset;
		else
			dest_buf = page_address(dest) + dest_offset;

		if (flags & ASYNC_TX_KMAP_SRC)
			src_buf = kmap_atomic(src, KM_USER0) + src_offset;
		else
			src_buf = page_address(src) + src_offset;

		memcpy(dest_buf, src_buf, len);

		if (flags & ASYNC_TX_KMAP_DST)
			kunmap_atomic(dest_buf, KM_USER0);

		if (flags & ASYNC_TX_KMAP_SRC)
			kunmap_atomic(src_buf, KM_USER0);

		async_tx_sync_epilog(flags, depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_memcpy);

static int __init async_memcpy_init(void)
{
	return 0;
}

static void __exit async_memcpy_exit(void)
{
	do { } while (0);
}

module_init(async_memcpy_init);
module_exit(async_memcpy_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("asynchronous memcpy api");
MODULE_LICENSE("GPL");
