/*
 * xor offload engine api
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
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/raid/xor.h>
#include <linux/async_tx.h>

/* do_async_xor - dma map the pages and perform the xor with an engine.
 * 	This routine is marked __always_inline so it can be compiled away
 * 	when CONFIG_DMA_ENGINE=n
 */
static __always_inline struct dma_async_tx_descriptor *
do_async_xor(struct dma_device *device,
	struct dma_chan *chan, struct page *dest, struct page **src_list,
	unsigned int offset, unsigned int src_cnt, size_t len,
	enum async_tx_flags flags, struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	dma_addr_t dma_dest;
	dma_addr_t *dma_src = (dma_addr_t *) src_list;
	struct dma_async_tx_descriptor *tx;
	int i;
	unsigned long dma_prep_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;

	pr_debug("%s: len: %zu\n", __func__, len);

	dma_dest = dma_map_page(device->dev, dest, offset, len,
				DMA_FROM_DEVICE);

	for (i = 0; i < src_cnt; i++)
		dma_src[i] = dma_map_page(device->dev, src_list[i], offset,
					  len, DMA_TO_DEVICE);

	/* Since we have clobbered the src_list we are committed
	 * to doing this asynchronously.  Drivers force forward progress
	 * in case they can not provide a descriptor
	 */
	tx = device->device_prep_dma_xor(chan, dma_dest, dma_src, src_cnt, len,
					 dma_prep_flags);
	if (!tx) {
		if (depend_tx)
			dma_wait_for_async_tx(depend_tx);

		while (!tx)
			tx = device->device_prep_dma_xor(chan, dma_dest,
							 dma_src, src_cnt, len,
							 dma_prep_flags);
	}

	async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);

	return tx;
}

static void
do_sync_xor(struct page *dest, struct page **src_list, unsigned int offset,
	unsigned int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	void *_dest;
	int i;

	pr_debug("%s: len: %zu\n", __func__, len);

	/* reuse the 'src_list' array to convert to buffer pointers */
	for (i = 0; i < src_cnt; i++)
		src_list[i] = (struct page *)
			(page_address(src_list[i]) + offset);

	/* set destination address */
	_dest = page_address(dest) + offset;

	if (flags & ASYNC_TX_XOR_ZERO_DST)
		memset(_dest, 0, len);

	xor_blocks(src_cnt, len, _dest,
		(void **) src_list);

	async_tx_sync_epilog(flags, depend_tx, cb_fn, cb_param);
}

/**
 * async_xor - attempt to xor a set of blocks with a dma engine.
 *	xor_blocks always uses the dest as a source so the ASYNC_TX_XOR_ZERO_DST
 *	flag must be set to not include dest data in the calculation.  The
 *	assumption with dma eninges is that they only use the destination
 *	buffer as a source when it is explicity specified in the source list.
 * @dest: destination page
 * @src_list: array of source pages (if the dest is also a source it must be
 *	at index zero).  The contents of this array may be overwritten.
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @flags: ASYNC_TX_XOR_ZERO_DST, ASYNC_TX_XOR_DROP_DEST,
 *	ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: xor depends on the result of this transaction.
 * @cb_fn: function to call when the xor completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_xor(struct page *dest, struct page **src_list, unsigned int offset,
	int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_XOR,
						      &dest, 1, src_list,
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_async_tx_callback _cb_fn;
	void *_cb_param;
	unsigned long local_flags;
	int xor_src_cnt;
	int i = 0, src_off = 0;

	BUG_ON(src_cnt <= 1);

	while (src_cnt) {
		local_flags = flags;
		if (device) { /* run the xor asynchronously */
			xor_src_cnt = min(src_cnt, device->max_xor);
			/* if we are submitting additional xors
			 * only set the callback on the last transaction
			 */
			if (src_cnt > xor_src_cnt) {
				local_flags &= ~ASYNC_TX_ACK;
				_cb_fn = NULL;
				_cb_param = NULL;
			} else {
				_cb_fn = cb_fn;
				_cb_param = cb_param;
			}

			tx = do_async_xor(device, chan, dest,
					  &src_list[src_off], offset,
					  xor_src_cnt, len, local_flags,
					  depend_tx, _cb_fn, _cb_param);
		} else { /* run the xor synchronously */
			/* in the sync case the dest is an implied source
			 * (assumes the dest is at the src_off index)
			 */
			if (flags & ASYNC_TX_XOR_DROP_DST) {
				src_cnt--;
				src_off++;
			}

			/* process up to 'MAX_XOR_BLOCKS' sources */
			xor_src_cnt = min(src_cnt, MAX_XOR_BLOCKS);

			/* if we are submitting additional xors
			 * only set the callback on the last transaction
			 */
			if (src_cnt > xor_src_cnt) {
				local_flags &= ~ASYNC_TX_ACK;
				_cb_fn = NULL;
				_cb_param = NULL;
			} else {
				_cb_fn = cb_fn;
				_cb_param = cb_param;
			}

			/* wait for any prerequisite operations */
			if (depend_tx) {
				/* if ack is already set then we cannot be sure
				 * we are referring to the correct operation
				 */
				BUG_ON(async_tx_test_ack(depend_tx));
				if (dma_wait_for_async_tx(depend_tx) ==
					DMA_ERROR)
					panic("%s: DMA_ERROR waiting for "
						"depend_tx\n",
						__func__);
			}

			do_sync_xor(dest, &src_list[src_off], offset,
				xor_src_cnt, len, local_flags, depend_tx,
				_cb_fn, _cb_param);
		}

		/* the previous tx is hidden from the client,
		 * so ack it
		 */
		if (i && depend_tx)
			async_tx_ack(depend_tx);

		depend_tx = tx;

		if (src_cnt > xor_src_cnt) {
			/* drop completed sources */
			src_cnt -= xor_src_cnt;
			src_off += xor_src_cnt;

			/* unconditionally preserve the destination */
			flags &= ~ASYNC_TX_XOR_ZERO_DST;

			/* use the intermediate result a source, but remember
			 * it's dropped, because it's implied, in the sync case
			 */
			src_list[--src_off] = dest;
			src_cnt++;
			flags |= ASYNC_TX_XOR_DROP_DST;
		} else
			src_cnt = 0;
		i++;
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_xor);

static int page_is_zero(struct page *p, unsigned int offset, size_t len)
{
	char *a = page_address(p) + offset;
	return ((*(u32 *) a) == 0 &&
		memcmp(a, a + 4, len - 4) == 0);
}

/**
 * async_xor_zero_sum - attempt a xor parity check with a dma engine.
 * @dest: destination page used if the xor is performed synchronously
 * @src_list: array of source pages.  The dest page must be listed as a source
 * 	at index zero.  The contents of this array may be overwritten.
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @result: 0 if sum == 0 else non-zero
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: xor depends on the result of this transaction.
 * @cb_fn: function to call when the xor completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_xor_zero_sum(struct page *dest, struct page **src_list,
	unsigned int offset, int src_cnt, size_t len,
	u32 *result, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_ZERO_SUM,
						      &dest, 1, src_list,
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;

	BUG_ON(src_cnt <= 1);

	if (device && src_cnt <= device->max_xor) {
		dma_addr_t *dma_src = (dma_addr_t *) src_list;
		unsigned long dma_prep_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;
		int i;

		pr_debug("%s: (async) len: %zu\n", __func__, len);

		for (i = 0; i < src_cnt; i++)
			dma_src[i] = dma_map_page(device->dev, src_list[i],
						  offset, len, DMA_TO_DEVICE);

		tx = device->device_prep_dma_zero_sum(chan, dma_src, src_cnt,
						      len, result,
						      dma_prep_flags);
		if (!tx) {
			if (depend_tx)
				dma_wait_for_async_tx(depend_tx);

			while (!tx)
				tx = device->device_prep_dma_zero_sum(chan,
					dma_src, src_cnt, len, result,
					dma_prep_flags);
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		unsigned long xor_flags = flags;

		pr_debug("%s: (sync) len: %zu\n", __func__, len);

		xor_flags |= ASYNC_TX_XOR_DROP_DST;
		xor_flags &= ~ASYNC_TX_ACK;

		tx = async_xor(dest, src_list, offset, src_cnt, len, xor_flags,
			depend_tx, NULL, NULL);

		if (tx) {
			if (dma_wait_for_async_tx(tx) == DMA_ERROR)
				panic("%s: DMA_ERROR waiting for tx\n",
					__func__);
			async_tx_ack(tx);
		}

		*result = page_is_zero(dest, offset, len) ? 0 : 1;

		tx = NULL;

		async_tx_sync_epilog(flags, depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_xor_zero_sum);

static int __init async_xor_init(void)
{
	#ifdef CONFIG_DMA_ENGINE
	/* To conserve stack space the input src_list (array of page pointers)
	 * is reused to hold the array of dma addresses passed to the driver.
	 * This conversion is only possible when dma_addr_t is less than the
	 * the size of a pointer.  HIGHMEM64G is known to violate this
	 * assumption.
	 */
	BUILD_BUG_ON(sizeof(dma_addr_t) > sizeof(struct page *));
	#endif

	return 0;
}

static void __exit async_xor_exit(void)
{
	do { } while (0);
}

module_init(async_xor_init);
module_exit(async_xor_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("asynchronous xor/xor-zero-sum api");
MODULE_LICENSE("GPL");
