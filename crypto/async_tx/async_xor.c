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
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/raid/xor.h>
#include <linux/async_tx.h>

/* do_async_xor - dma map the pages and perform the xor with an engine */
static __async_inline struct dma_async_tx_descriptor *
do_async_xor(struct dma_chan *chan, struct dmaengine_unmap_data *unmap,
	     struct async_submit_ctl *submit)
{
	struct dma_device *dma = chan->device;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_async_tx_callback cb_fn_orig = submit->cb_fn;
	void *cb_param_orig = submit->cb_param;
	enum async_tx_flags flags_orig = submit->flags;
	enum dma_ctrl_flags dma_flags = 0;
	int src_cnt = unmap->to_cnt;
	int xor_src_cnt;
	dma_addr_t dma_dest = unmap->addr[unmap->to_cnt];
	dma_addr_t *src_list = unmap->addr;

	while (src_cnt) {
		dma_addr_t tmp;

		submit->flags = flags_orig;
		xor_src_cnt = min(src_cnt, (int)dma->max_xor);
		/* if we are submitting additional xors, leave the chain open
		 * and clear the callback parameters
		 */
		if (src_cnt > xor_src_cnt) {
			submit->flags &= ~ASYNC_TX_ACK;
			submit->flags |= ASYNC_TX_FENCE;
			submit->cb_fn = NULL;
			submit->cb_param = NULL;
		} else {
			submit->cb_fn = cb_fn_orig;
			submit->cb_param = cb_param_orig;
		}
		if (submit->cb_fn)
			dma_flags |= DMA_PREP_INTERRUPT;
		if (submit->flags & ASYNC_TX_FENCE)
			dma_flags |= DMA_PREP_FENCE;

		/* Drivers force forward progress in case they can not provide a
		 * descriptor
		 */
		tmp = src_list[0];
		if (src_list > unmap->addr)
			src_list[0] = dma_dest;
		tx = dma->device_prep_dma_xor(chan, dma_dest, src_list,
					      xor_src_cnt, unmap->len,
					      dma_flags);

		if (unlikely(!tx))
			async_tx_quiesce(&submit->depend_tx);

		/* spin wait for the preceding transactions to complete */
		while (unlikely(!tx)) {
			dma_async_issue_pending(chan);
			tx = dma->device_prep_dma_xor(chan, dma_dest,
						      src_list,
						      xor_src_cnt, unmap->len,
						      dma_flags);
		}
		src_list[0] = tmp;

		dma_set_unmap(tx, unmap);
		async_tx_submit(chan, tx, submit);
		submit->depend_tx = tx;

		if (src_cnt > xor_src_cnt) {
			/* drop completed sources */
			src_cnt -= xor_src_cnt;
			/* use the intermediate result a source */
			src_cnt++;
			src_list += xor_src_cnt - 1;
		} else
			break;
	}

	return tx;
}

static void
do_sync_xor(struct page *dest, struct page **src_list, unsigned int offset,
	    int src_cnt, size_t len, struct async_submit_ctl *submit)
{
	int i;
	int xor_src_cnt = 0;
	int src_off = 0;
	void *dest_buf;
	void **srcs;

	if (submit->scribble)
		srcs = submit->scribble;
	else
		srcs = (void **) src_list;

	/* convert to buffer pointers */
	for (i = 0; i < src_cnt; i++)
		if (src_list[i])
			srcs[xor_src_cnt++] = page_address(src_list[i]) + offset;
	src_cnt = xor_src_cnt;
	/* set destination address */
	dest_buf = page_address(dest) + offset;

	if (submit->flags & ASYNC_TX_XOR_ZERO_DST)
		memset(dest_buf, 0, len);

	while (src_cnt > 0) {
		/* process up to 'MAX_XOR_BLOCKS' sources */
		xor_src_cnt = min(src_cnt, MAX_XOR_BLOCKS);
		xor_blocks(xor_src_cnt, len, dest_buf, &srcs[src_off]);

		/* drop completed sources */
		src_cnt -= xor_src_cnt;
		src_off += xor_src_cnt;
	}

	async_tx_sync_epilog(submit);
}

/**
 * async_xor - attempt to xor a set of blocks with a dma engine.
 * @dest: destination page
 * @src_list: array of source pages
 * @offset: common src/dst offset to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @submit: submission / completion modifiers
 *
 * honored flags: ASYNC_TX_ACK, ASYNC_TX_XOR_ZERO_DST, ASYNC_TX_XOR_DROP_DST
 *
 * xor_blocks always uses the dest as a source so the
 * ASYNC_TX_XOR_ZERO_DST flag must be set to not include dest data in
 * the calculation.  The assumption with dma eninges is that they only
 * use the destination buffer as a source when it is explicity specified
 * in the source list.
 *
 * src_list note: if the dest is also a source it must be at index zero.
 * The contents of this array will be overwritten if a scribble region
 * is not specified.
 */
struct dma_async_tx_descriptor *
async_xor(struct page *dest, struct page **src_list, unsigned int offset,
	  int src_cnt, size_t len, struct async_submit_ctl *submit)
{
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_XOR,
						      &dest, 1, src_list,
						      src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dmaengine_unmap_data *unmap = NULL;

	BUG_ON(src_cnt <= 1);

	if (device)
		unmap = dmaengine_get_unmap_data(device->dev, src_cnt+1, GFP_NOIO);

	if (unmap && is_dma_xor_aligned(device, offset, 0, len)) {
		struct dma_async_tx_descriptor *tx;
		int i, j;

		/* run the xor asynchronously */
		pr_debug("%s (async): len: %zu\n", __func__, len);

		unmap->len = len;
		for (i = 0, j = 0; i < src_cnt; i++) {
			if (!src_list[i])
				continue;
			unmap->to_cnt++;
			unmap->addr[j++] = dma_map_page(device->dev, src_list[i],
							offset, len, DMA_TO_DEVICE);
		}

		/* map it bidirectional as it may be re-used as a source */
		unmap->addr[j] = dma_map_page(device->dev, dest, offset, len,
					      DMA_BIDIRECTIONAL);
		unmap->bidi_cnt = 1;

		tx = do_async_xor(chan, unmap, submit);
		dmaengine_unmap_put(unmap);
		return tx;
	} else {
		dmaengine_unmap_put(unmap);
		/* run the xor synchronously */
		pr_debug("%s (sync): len: %zu\n", __func__, len);
		WARN_ONCE(chan, "%s: no space for dma address conversion\n",
			  __func__);

		/* in the sync case the dest is an implied source
		 * (assumes the dest is the first source)
		 */
		if (submit->flags & ASYNC_TX_XOR_DROP_DST) {
			src_cnt--;
			src_list++;
		}

		/* wait for any prerequisite operations */
		async_tx_quiesce(&submit->depend_tx);

		do_sync_xor(dest, src_list, offset, src_cnt, len, submit);

		return NULL;
	}
}
EXPORT_SYMBOL_GPL(async_xor);

static int page_is_zero(struct page *p, unsigned int offset, size_t len)
{
	return !memchr_inv(page_address(p) + offset, 0, len);
}

static inline struct dma_chan *
xor_val_chan(struct async_submit_ctl *submit, struct page *dest,
		 struct page **src_list, int src_cnt, size_t len)
{
	#ifdef CONFIG_ASYNC_TX_DISABLE_XOR_VAL_DMA
	return NULL;
	#endif
	return async_tx_find_channel(submit, DMA_XOR_VAL, &dest, 1, src_list,
				     src_cnt, len);
}

/**
 * async_xor_val - attempt a xor parity check with a dma engine.
 * @dest: destination page used if the xor is performed synchronously
 * @src_list: array of source pages
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @result: 0 if sum == 0 else non-zero
 * @submit: submission / completion modifiers
 *
 * honored flags: ASYNC_TX_ACK
 *
 * src_list note: if the dest is also a source it must be at index zero.
 * The contents of this array will be overwritten if a scribble region
 * is not specified.
 */
struct dma_async_tx_descriptor *
async_xor_val(struct page *dest, struct page **src_list, unsigned int offset,
	      int src_cnt, size_t len, enum sum_check_flags *result,
	      struct async_submit_ctl *submit)
{
	struct dma_chan *chan = xor_val_chan(submit, dest, src_list, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	struct dmaengine_unmap_data *unmap = NULL;

	BUG_ON(src_cnt <= 1);

	if (device)
		unmap = dmaengine_get_unmap_data(device->dev, src_cnt, GFP_NOIO);

	if (unmap && src_cnt <= device->max_xor &&
	    is_dma_xor_aligned(device, offset, 0, len)) {
		unsigned long dma_prep_flags = 0;
		int i;

		pr_debug("%s: (async) len: %zu\n", __func__, len);

		if (submit->cb_fn)
			dma_prep_flags |= DMA_PREP_INTERRUPT;
		if (submit->flags & ASYNC_TX_FENCE)
			dma_prep_flags |= DMA_PREP_FENCE;

		for (i = 0; i < src_cnt; i++) {
			unmap->addr[i] = dma_map_page(device->dev, src_list[i],
						      offset, len, DMA_TO_DEVICE);
			unmap->to_cnt++;
		}
		unmap->len = len;

		tx = device->device_prep_dma_xor_val(chan, unmap->addr, src_cnt,
						     len, result,
						     dma_prep_flags);
		if (unlikely(!tx)) {
			async_tx_quiesce(&submit->depend_tx);

			while (!tx) {
				dma_async_issue_pending(chan);
				tx = device->device_prep_dma_xor_val(chan,
					unmap->addr, src_cnt, len, result,
					dma_prep_flags);
			}
		}
		dma_set_unmap(tx, unmap);
		async_tx_submit(chan, tx, submit);
	} else {
		enum async_tx_flags flags_orig = submit->flags;

		pr_debug("%s: (sync) len: %zu\n", __func__, len);
		WARN_ONCE(device && src_cnt <= device->max_xor,
			  "%s: no space for dma address conversion\n",
			  __func__);

		submit->flags |= ASYNC_TX_XOR_DROP_DST;
		submit->flags &= ~ASYNC_TX_ACK;

		tx = async_xor(dest, src_list, offset, src_cnt, len, submit);

		async_tx_quiesce(&tx);

		*result = !page_is_zero(dest, offset, len) << SUM_CHECK_P;

		async_tx_sync_epilog(submit);
		submit->flags = flags_orig;
	}
	dmaengine_unmap_put(unmap);

	return tx;
}
EXPORT_SYMBOL_GPL(async_xor_val);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("asynchronous xor/xor-zero-sum api");
MODULE_LICENSE("GPL");
