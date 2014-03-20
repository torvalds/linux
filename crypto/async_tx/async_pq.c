/*
 * Copyright(c) 2007 Yuri Tikhonov <yur@emcraft.com>
 * Copyright(c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/raid/pq.h>
#include <linux/async_tx.h>
#include <linux/gfp.h>

/**
 * pq_scribble_page - space to hold throwaway P or Q buffer for
 * synchronous gen_syndrome
 */
static struct page *pq_scribble_page;

/* the struct page *blocks[] parameter passed to async_gen_syndrome()
 * and async_syndrome_val() contains the 'P' destination address at
 * blocks[disks-2] and the 'Q' destination address at blocks[disks-1]
 *
 * note: these are macros as they are used as lvalues
 */
#define P(b, d) (b[d-2])
#define Q(b, d) (b[d-1])

/**
 * do_async_gen_syndrome - asynchronously calculate P and/or Q
 */
static __async_inline struct dma_async_tx_descriptor *
do_async_gen_syndrome(struct dma_chan *chan,
		      const unsigned char *scfs, int disks,
		      struct dmaengine_unmap_data *unmap,
		      enum dma_ctrl_flags dma_flags,
		      struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct dma_device *dma = chan->device;
	enum async_tx_flags flags_orig = submit->flags;
	dma_async_tx_callback cb_fn_orig = submit->cb_fn;
	dma_async_tx_callback cb_param_orig = submit->cb_param;
	int src_cnt = disks - 2;
	unsigned short pq_src_cnt;
	dma_addr_t dma_dest[2];
	int src_off = 0;

	if (submit->flags & ASYNC_TX_FENCE)
		dma_flags |= DMA_PREP_FENCE;

	while (src_cnt > 0) {
		submit->flags = flags_orig;
		pq_src_cnt = min(src_cnt, dma_maxpq(dma, dma_flags));
		/* if we are submitting additional pqs, leave the chain open,
		 * clear the callback parameters, and leave the destination
		 * buffers mapped
		 */
		if (src_cnt > pq_src_cnt) {
			submit->flags &= ~ASYNC_TX_ACK;
			submit->flags |= ASYNC_TX_FENCE;
			submit->cb_fn = NULL;
			submit->cb_param = NULL;
		} else {
			submit->cb_fn = cb_fn_orig;
			submit->cb_param = cb_param_orig;
			if (cb_fn_orig)
				dma_flags |= DMA_PREP_INTERRUPT;
		}

		/* Drivers force forward progress in case they can not provide
		 * a descriptor
		 */
		for (;;) {
			dma_dest[0] = unmap->addr[disks - 2];
			dma_dest[1] = unmap->addr[disks - 1];
			tx = dma->device_prep_dma_pq(chan, dma_dest,
						     &unmap->addr[src_off],
						     pq_src_cnt,
						     &scfs[src_off], unmap->len,
						     dma_flags);
			if (likely(tx))
				break;
			async_tx_quiesce(&submit->depend_tx);
			dma_async_issue_pending(chan);
		}

		dma_set_unmap(tx, unmap);
		async_tx_submit(chan, tx, submit);
		submit->depend_tx = tx;

		/* drop completed sources */
		src_cnt -= pq_src_cnt;
		src_off += pq_src_cnt;

		dma_flags |= DMA_PREP_CONTINUE;
	}

	return tx;
}

/**
 * do_sync_gen_syndrome - synchronously calculate a raid6 syndrome
 */
static void
do_sync_gen_syndrome(struct page **blocks, unsigned int offset, int disks,
		     size_t len, struct async_submit_ctl *submit)
{
	void **srcs;
	int i;

	if (submit->scribble)
		srcs = submit->scribble;
	else
		srcs = (void **) blocks;

	for (i = 0; i < disks; i++) {
		if (blocks[i] == NULL) {
			BUG_ON(i > disks - 3); /* P or Q can't be zero */
			srcs[i] = (void*)raid6_empty_zero_page;
		} else
			srcs[i] = page_address(blocks[i]) + offset;
	}
	raid6_call.gen_syndrome(disks, len, srcs);
	async_tx_sync_epilog(submit);
}

/**
 * async_gen_syndrome - asynchronously calculate a raid6 syndrome
 * @blocks: source blocks from idx 0..disks-3, P @ disks-2 and Q @ disks-1
 * @offset: common offset into each block (src and dest) to start transaction
 * @disks: number of blocks (including missing P or Q, see below)
 * @len: length of operation in bytes
 * @submit: submission/completion modifiers
 *
 * General note: This routine assumes a field of GF(2^8) with a
 * primitive polynomial of 0x11d and a generator of {02}.
 *
 * 'disks' note: callers can optionally omit either P or Q (but not
 * both) from the calculation by setting blocks[disks-2] or
 * blocks[disks-1] to NULL.  When P or Q is omitted 'len' must be <=
 * PAGE_SIZE as a temporary buffer of this size is used in the
 * synchronous path.  'disks' always accounts for both destination
 * buffers.  If any source buffers (blocks[i] where i < disks - 2) are
 * set to NULL those buffers will be replaced with the raid6_zero_page
 * in the synchronous path and omitted in the hardware-asynchronous
 * path.
 */
struct dma_async_tx_descriptor *
async_gen_syndrome(struct page **blocks, unsigned int offset, int disks,
		   size_t len, struct async_submit_ctl *submit)
{
	int src_cnt = disks - 2;
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_PQ,
						      &P(blocks, disks), 2,
						      blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dmaengine_unmap_data *unmap = NULL;

	BUG_ON(disks > 255 || !(P(blocks, disks) || Q(blocks, disks)));

	if (device)
		unmap = dmaengine_get_unmap_data(device->dev, disks, GFP_NOIO);

	if (unmap &&
	    (src_cnt <= dma_maxpq(device, 0) ||
	     dma_maxpq(device, DMA_PREP_CONTINUE) > 0) &&
	    is_dma_pq_aligned(device, offset, 0, len)) {
		struct dma_async_tx_descriptor *tx;
		enum dma_ctrl_flags dma_flags = 0;
		unsigned char coefs[src_cnt];
		int i, j;

		/* run the p+q asynchronously */
		pr_debug("%s: (async) disks: %d len: %zu\n",
			 __func__, disks, len);

		/* convert source addresses being careful to collapse 'empty'
		 * sources and update the coefficients accordingly
		 */
		unmap->len = len;
		for (i = 0, j = 0; i < src_cnt; i++) {
			if (blocks[i] == NULL)
				continue;
			unmap->addr[j] = dma_map_page(device->dev, blocks[i], offset,
						      len, DMA_TO_DEVICE);
			coefs[j] = raid6_gfexp[i];
			unmap->to_cnt++;
			j++;
		}

		/*
		 * DMAs use destinations as sources,
		 * so use BIDIRECTIONAL mapping
		 */
		unmap->bidi_cnt++;
		if (P(blocks, disks))
			unmap->addr[j++] = dma_map_page(device->dev, P(blocks, disks),
							offset, len, DMA_BIDIRECTIONAL);
		else {
			unmap->addr[j++] = 0;
			dma_flags |= DMA_PREP_PQ_DISABLE_P;
		}

		unmap->bidi_cnt++;
		if (Q(blocks, disks))
			unmap->addr[j++] = dma_map_page(device->dev, Q(blocks, disks),
						       offset, len, DMA_BIDIRECTIONAL);
		else {
			unmap->addr[j++] = 0;
			dma_flags |= DMA_PREP_PQ_DISABLE_Q;
		}

		tx = do_async_gen_syndrome(chan, coefs, j, unmap, dma_flags, submit);
		dmaengine_unmap_put(unmap);
		return tx;
	}

	dmaengine_unmap_put(unmap);

	/* run the pq synchronously */
	pr_debug("%s: (sync) disks: %d len: %zu\n", __func__, disks, len);

	/* wait for any prerequisite operations */
	async_tx_quiesce(&submit->depend_tx);

	if (!P(blocks, disks)) {
		P(blocks, disks) = pq_scribble_page;
		BUG_ON(len + offset > PAGE_SIZE);
	}
	if (!Q(blocks, disks)) {
		Q(blocks, disks) = pq_scribble_page;
		BUG_ON(len + offset > PAGE_SIZE);
	}
	do_sync_gen_syndrome(blocks, offset, disks, len, submit);

	return NULL;
}
EXPORT_SYMBOL_GPL(async_gen_syndrome);

static inline struct dma_chan *
pq_val_chan(struct async_submit_ctl *submit, struct page **blocks, int disks, size_t len)
{
	#ifdef CONFIG_ASYNC_TX_DISABLE_PQ_VAL_DMA
	return NULL;
	#endif
	return async_tx_find_channel(submit, DMA_PQ_VAL, NULL, 0,  blocks,
				     disks, len);
}

/**
 * async_syndrome_val - asynchronously validate a raid6 syndrome
 * @blocks: source blocks from idx 0..disks-3, P @ disks-2 and Q @ disks-1
 * @offset: common offset into each block (src and dest) to start transaction
 * @disks: number of blocks (including missing P or Q, see below)
 * @len: length of operation in bytes
 * @pqres: on val failure SUM_CHECK_P_RESULT and/or SUM_CHECK_Q_RESULT are set
 * @spare: temporary result buffer for the synchronous case
 * @submit: submission / completion modifiers
 *
 * The same notes from async_gen_syndrome apply to the 'blocks',
 * and 'disks' parameters of this routine.  The synchronous path
 * requires a temporary result buffer and submit->scribble to be
 * specified.
 */
struct dma_async_tx_descriptor *
async_syndrome_val(struct page **blocks, unsigned int offset, int disks,
		   size_t len, enum sum_check_flags *pqres, struct page *spare,
		   struct async_submit_ctl *submit)
{
	struct dma_chan *chan = pq_val_chan(submit, blocks, disks, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx;
	unsigned char coefs[disks-2];
	enum dma_ctrl_flags dma_flags = submit->cb_fn ? DMA_PREP_INTERRUPT : 0;
	struct dmaengine_unmap_data *unmap = NULL;

	BUG_ON(disks < 4);

	if (device)
		unmap = dmaengine_get_unmap_data(device->dev, disks, GFP_NOIO);

	if (unmap && disks <= dma_maxpq(device, 0) &&
	    is_dma_pq_aligned(device, offset, 0, len)) {
		struct device *dev = device->dev;
		dma_addr_t pq[2];
		int i, j = 0, src_cnt = 0;

		pr_debug("%s: (async) disks: %d len: %zu\n",
			 __func__, disks, len);

		unmap->len = len;
		for (i = 0; i < disks-2; i++)
			if (likely(blocks[i])) {
				unmap->addr[j] = dma_map_page(dev, blocks[i],
							      offset, len,
							      DMA_TO_DEVICE);
				coefs[j] = raid6_gfexp[i];
				unmap->to_cnt++;
				src_cnt++;
				j++;
			}

		if (!P(blocks, disks)) {
			pq[0] = 0;
			dma_flags |= DMA_PREP_PQ_DISABLE_P;
		} else {
			pq[0] = dma_map_page(dev, P(blocks, disks),
					     offset, len,
					     DMA_TO_DEVICE);
			unmap->addr[j++] = pq[0];
			unmap->to_cnt++;
		}
		if (!Q(blocks, disks)) {
			pq[1] = 0;
			dma_flags |= DMA_PREP_PQ_DISABLE_Q;
		} else {
			pq[1] = dma_map_page(dev, Q(blocks, disks),
					     offset, len,
					     DMA_TO_DEVICE);
			unmap->addr[j++] = pq[1];
			unmap->to_cnt++;
		}

		if (submit->flags & ASYNC_TX_FENCE)
			dma_flags |= DMA_PREP_FENCE;
		for (;;) {
			tx = device->device_prep_dma_pq_val(chan, pq,
							    unmap->addr,
							    src_cnt,
							    coefs,
							    len, pqres,
							    dma_flags);
			if (likely(tx))
				break;
			async_tx_quiesce(&submit->depend_tx);
			dma_async_issue_pending(chan);
		}

		dma_set_unmap(tx, unmap);
		async_tx_submit(chan, tx, submit);

		return tx;
	} else {
		struct page *p_src = P(blocks, disks);
		struct page *q_src = Q(blocks, disks);
		enum async_tx_flags flags_orig = submit->flags;
		dma_async_tx_callback cb_fn_orig = submit->cb_fn;
		void *scribble = submit->scribble;
		void *cb_param_orig = submit->cb_param;
		void *p, *q, *s;

		pr_debug("%s: (sync) disks: %d len: %zu\n",
			 __func__, disks, len);

		/* caller must provide a temporary result buffer and
		 * allow the input parameters to be preserved
		 */
		BUG_ON(!spare || !scribble);

		/* wait for any prerequisite operations */
		async_tx_quiesce(&submit->depend_tx);

		/* recompute p and/or q into the temporary buffer and then
		 * check to see the result matches the current value
		 */
		tx = NULL;
		*pqres = 0;
		if (p_src) {
			init_async_submit(submit, ASYNC_TX_XOR_ZERO_DST, NULL,
					  NULL, NULL, scribble);
			tx = async_xor(spare, blocks, offset, disks-2, len, submit);
			async_tx_quiesce(&tx);
			p = page_address(p_src) + offset;
			s = page_address(spare) + offset;
			*pqres |= !!memcmp(p, s, len) << SUM_CHECK_P;
		}

		if (q_src) {
			P(blocks, disks) = NULL;
			Q(blocks, disks) = spare;
			init_async_submit(submit, 0, NULL, NULL, NULL, scribble);
			tx = async_gen_syndrome(blocks, offset, disks, len, submit);
			async_tx_quiesce(&tx);
			q = page_address(q_src) + offset;
			s = page_address(spare) + offset;
			*pqres |= !!memcmp(q, s, len) << SUM_CHECK_Q;
		}

		/* restore P, Q and submit */
		P(blocks, disks) = p_src;
		Q(blocks, disks) = q_src;

		submit->cb_fn = cb_fn_orig;
		submit->cb_param = cb_param_orig;
		submit->flags = flags_orig;
		async_tx_sync_epilog(submit);

		return NULL;
	}
}
EXPORT_SYMBOL_GPL(async_syndrome_val);

static int __init async_pq_init(void)
{
	pq_scribble_page = alloc_page(GFP_KERNEL);

	if (pq_scribble_page)
		return 0;

	pr_err("%s: failed to allocate required spare page\n", __func__);

	return -ENOMEM;
}

static void __exit async_pq_exit(void)
{
	put_page(pq_scribble_page);
}

module_init(async_pq_init);
module_exit(async_pq_exit);

MODULE_DESCRIPTION("asynchronous raid6 syndrome generation/validation");
MODULE_LICENSE("GPL");
