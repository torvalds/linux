/*
 * Asynchronous RAID-6 recovery calculations ASYNC_TX API.
 * Copyright(c) 2009 Intel Corporation
 *
 * based on raid6recov.c:
 *   Copyright 2002 H. Peter Anvin
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
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/raid/pq.h>
#include <linux/async_tx.h>

static struct dma_async_tx_descriptor *
async_sum_product(struct page *dest, struct page **srcs, unsigned char *coef,
		  size_t len, struct async_submit_ctl *submit)
{
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_PQ,
						      &dest, 1, srcs, 2, len);
	struct dma_device *dma = chan ? chan->device : NULL;
	const u8 *amul, *bmul;
	u8 ax, bx;
	u8 *a, *b, *c;

	if (dma) {
		dma_addr_t dma_dest[2];
		dma_addr_t dma_src[2];
		struct device *dev = dma->dev;
		struct dma_async_tx_descriptor *tx;
		enum dma_ctrl_flags dma_flags = DMA_PREP_PQ_DISABLE_P;

		if (submit->flags & ASYNC_TX_FENCE)
			dma_flags |= DMA_PREP_FENCE;
		dma_dest[1] = dma_map_page(dev, dest, 0, len, DMA_BIDIRECTIONAL);
		dma_src[0] = dma_map_page(dev, srcs[0], 0, len, DMA_TO_DEVICE);
		dma_src[1] = dma_map_page(dev, srcs[1], 0, len, DMA_TO_DEVICE);
		tx = dma->device_prep_dma_pq(chan, dma_dest, dma_src, 2, coef,
					     len, dma_flags);
		if (tx) {
			async_tx_submit(chan, tx, submit);
			return tx;
		}

		/* could not get a descriptor, unmap and fall through to
		 * the synchronous path
		 */
		dma_unmap_page(dev, dma_dest[1], len, DMA_BIDIRECTIONAL);
		dma_unmap_page(dev, dma_src[0], len, DMA_TO_DEVICE);
		dma_unmap_page(dev, dma_src[1], len, DMA_TO_DEVICE);
	}

	/* run the operation synchronously */
	async_tx_quiesce(&submit->depend_tx);
	amul = raid6_gfmul[coef[0]];
	bmul = raid6_gfmul[coef[1]];
	a = page_address(srcs[0]);
	b = page_address(srcs[1]);
	c = page_address(dest);

	while (len--) {
		ax    = amul[*a++];
		bx    = bmul[*b++];
		*c++ = ax ^ bx;
	}

	return NULL;
}

static struct dma_async_tx_descriptor *
async_mult(struct page *dest, struct page *src, u8 coef, size_t len,
	   struct async_submit_ctl *submit)
{
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_PQ,
						      &dest, 1, &src, 1, len);
	struct dma_device *dma = chan ? chan->device : NULL;
	const u8 *qmul; /* Q multiplier table */
	u8 *d, *s;

	if (dma) {
		dma_addr_t dma_dest[2];
		dma_addr_t dma_src[1];
		struct device *dev = dma->dev;
		struct dma_async_tx_descriptor *tx;
		enum dma_ctrl_flags dma_flags = DMA_PREP_PQ_DISABLE_P;

		if (submit->flags & ASYNC_TX_FENCE)
			dma_flags |= DMA_PREP_FENCE;
		dma_dest[1] = dma_map_page(dev, dest, 0, len, DMA_BIDIRECTIONAL);
		dma_src[0] = dma_map_page(dev, src, 0, len, DMA_TO_DEVICE);
		tx = dma->device_prep_dma_pq(chan, dma_dest, dma_src, 1, &coef,
					     len, dma_flags);
		if (tx) {
			async_tx_submit(chan, tx, submit);
			return tx;
		}

		/* could not get a descriptor, unmap and fall through to
		 * the synchronous path
		 */
		dma_unmap_page(dev, dma_dest[1], len, DMA_BIDIRECTIONAL);
		dma_unmap_page(dev, dma_src[0], len, DMA_TO_DEVICE);
	}

	/* no channel available, or failed to allocate a descriptor, so
	 * perform the operation synchronously
	 */
	async_tx_quiesce(&submit->depend_tx);
	qmul  = raid6_gfmul[coef];
	d = page_address(dest);
	s = page_address(src);

	while (len--)
		*d++ = qmul[*s++];

	return NULL;
}

static struct dma_async_tx_descriptor *
__2data_recov_4(int disks, size_t bytes, int faila, int failb,
		struct page **blocks, struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *a, *b;
	struct page *srcs[2];
	unsigned char coef[2];
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;

	p = blocks[disks-2];
	q = blocks[disks-1];

	a = blocks[faila];
	b = blocks[failb];

	/* in the 4 disk case P + Pxy == P and Q + Qxy == Q */
	/* Dx = A*(P+Pxy) + B*(Q+Qxy) */
	srcs[0] = p;
	srcs[1] = q;
	coef[0] = raid6_gfexi[failb-faila];
	coef[1] = raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]];
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_sum_product(b, srcs, coef, bytes, submit);

	/* Dy = P+Pxy+Dx */
	srcs[0] = p;
	srcs[1] = b;
	init_async_submit(submit, flags | ASYNC_TX_XOR_ZERO_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor(a, srcs, 0, 2, bytes, submit);

	return tx;

}

static struct dma_async_tx_descriptor *
__2data_recov_5(int disks, size_t bytes, int faila, int failb,
		struct page **blocks, struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *g, *dp, *dq;
	struct page *srcs[2];
	unsigned char coef[2];
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;
	int good_srcs, good, i;

	good_srcs = 0;
	good = -1;
	for (i = 0; i < disks-2; i++) {
		if (blocks[i] == NULL)
			continue;
		if (i == faila || i == failb)
			continue;
		good = i;
		good_srcs++;
	}
	BUG_ON(good_srcs > 1);

	p = blocks[disks-2];
	q = blocks[disks-1];
	g = blocks[good];

	/* Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for delta p and
	 * delta q
	 */
	dp = blocks[faila];
	dq = blocks[failb];

	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_memcpy(dp, g, 0, 0, bytes, submit);
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_mult(dq, g, raid6_gfexp[good], bytes, submit);

	/* compute P + Pxy */
	srcs[0] = dp;
	srcs[1] = p;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor(dp, srcs, 0, 2, bytes, submit);

	/* compute Q + Qxy */
	srcs[0] = dq;
	srcs[1] = q;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor(dq, srcs, 0, 2, bytes, submit);

	/* Dx = A*(P+Pxy) + B*(Q+Qxy) */
	srcs[0] = dp;
	srcs[1] = dq;
	coef[0] = raid6_gfexi[failb-faila];
	coef[1] = raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]];
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_sum_product(dq, srcs, coef, bytes, submit);

	/* Dy = P+Pxy+Dx */
	srcs[0] = dp;
	srcs[1] = dq;
	init_async_submit(submit, flags | ASYNC_TX_XOR_DROP_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor(dp, srcs, 0, 2, bytes, submit);

	return tx;
}

static struct dma_async_tx_descriptor *
__2data_recov_n(int disks, size_t bytes, int faila, int failb,
	      struct page **blocks, struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *dp, *dq;
	struct page *srcs[2];
	unsigned char coef[2];
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;

	p = blocks[disks-2];
	q = blocks[disks-1];

	/* Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for
	 * delta p and delta q
	 */
	dp = blocks[faila];
	blocks[faila] = NULL;
	blocks[disks-2] = dp;
	dq = blocks[failb];
	blocks[failb] = NULL;
	blocks[disks-1] = dq;

	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_gen_syndrome(blocks, 0, disks, bytes, submit);

	/* Restore pointer table */
	blocks[faila]   = dp;
	blocks[failb]   = dq;
	blocks[disks-2] = p;
	blocks[disks-1] = q;

	/* compute P + Pxy */
	srcs[0] = dp;
	srcs[1] = p;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor(dp, srcs, 0, 2, bytes, submit);

	/* compute Q + Qxy */
	srcs[0] = dq;
	srcs[1] = q;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor(dq, srcs, 0, 2, bytes, submit);

	/* Dx = A*(P+Pxy) + B*(Q+Qxy) */
	srcs[0] = dp;
	srcs[1] = dq;
	coef[0] = raid6_gfexi[failb-faila];
	coef[1] = raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]];
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_sum_product(dq, srcs, coef, bytes, submit);

	/* Dy = P+Pxy+Dx */
	srcs[0] = dp;
	srcs[1] = dq;
	init_async_submit(submit, flags | ASYNC_TX_XOR_DROP_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor(dp, srcs, 0, 2, bytes, submit);

	return tx;
}

/**
 * async_raid6_2data_recov - asynchronously calculate two missing data blocks
 * @disks: number of disks in the RAID-6 array
 * @bytes: block size
 * @faila: first failed drive index
 * @failb: second failed drive index
 * @blocks: array of source pointers where the last two entries are p and q
 * @submit: submission/completion modifiers
 */
struct dma_async_tx_descriptor *
async_raid6_2data_recov(int disks, size_t bytes, int faila, int failb,
			struct page **blocks, struct async_submit_ctl *submit)
{
	int non_zero_srcs, i;

	BUG_ON(faila == failb);
	if (failb < faila)
		swap(faila, failb);

	pr_debug("%s: disks: %d len: %zu\n", __func__, disks, bytes);

	/* we need to preserve the contents of 'blocks' for the async
	 * case, so punt to synchronous if a scribble buffer is not available
	 */
	if (!submit->scribble) {
		void **ptrs = (void **) blocks;

		async_tx_quiesce(&submit->depend_tx);
		for (i = 0; i < disks; i++)
			if (blocks[i] == NULL)
				ptrs[i] = (void *) raid6_empty_zero_page;
			else
				ptrs[i] = page_address(blocks[i]);

		raid6_2data_recov(disks, bytes, faila, failb, ptrs);

		async_tx_sync_epilog(submit);

		return NULL;
	}

	non_zero_srcs = 0;
	for (i = 0; i < disks-2 && non_zero_srcs < 4; i++)
		if (blocks[i])
			non_zero_srcs++;
	switch (non_zero_srcs) {
	case 0:
	case 1:
		/* There must be at least 2 sources - the failed devices. */
		BUG();

	case 2:
		/* dma devices do not uniformly understand a zero source pq
		 * operation (in contrast to the synchronous case), so
		 * explicitly handle the special case of a 4 disk array with
		 * both data disks missing.
		 */
		return __2data_recov_4(disks, bytes, faila, failb, blocks, submit);
	case 3:
		/* dma devices do not uniformly understand a single
		 * source pq operation (in contrast to the synchronous
		 * case), so explicitly handle the special case of a 5 disk
		 * array with 2 of 3 data disks missing.
		 */
		return __2data_recov_5(disks, bytes, faila, failb, blocks, submit);
	default:
		return __2data_recov_n(disks, bytes, faila, failb, blocks, submit);
	}
}
EXPORT_SYMBOL_GPL(async_raid6_2data_recov);

/**
 * async_raid6_datap_recov - asynchronously calculate a data and the 'p' block
 * @disks: number of disks in the RAID-6 array
 * @bytes: block size
 * @faila: failed drive index
 * @blocks: array of source pointers where the last two entries are p and q
 * @submit: submission/completion modifiers
 */
struct dma_async_tx_descriptor *
async_raid6_datap_recov(int disks, size_t bytes, int faila,
			struct page **blocks, struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *dq;
	u8 coef;
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;
	int good_srcs, good, i;
	struct page *srcs[2];

	pr_debug("%s: disks: %d len: %zu\n", __func__, disks, bytes);

	/* we need to preserve the contents of 'blocks' for the async
	 * case, so punt to synchronous if a scribble buffer is not available
	 */
	if (!scribble) {
		void **ptrs = (void **) blocks;

		async_tx_quiesce(&submit->depend_tx);
		for (i = 0; i < disks; i++)
			if (blocks[i] == NULL)
				ptrs[i] = (void*)raid6_empty_zero_page;
			else
				ptrs[i] = page_address(blocks[i]);

		raid6_datap_recov(disks, bytes, faila, ptrs);

		async_tx_sync_epilog(submit);

		return NULL;
	}

	good_srcs = 0;
	good = -1;
	for (i = 0; i < disks-2; i++) {
		if (i == faila)
			continue;
		if (blocks[i]) {
			good = i;
			good_srcs++;
			if (good_srcs > 1)
				break;
		}
	}
	BUG_ON(good_srcs == 0);

	p = blocks[disks-2];
	q = blocks[disks-1];

	/* Compute syndrome with zero for the missing data page
	 * Use the dead data page as temporary storage for delta q
	 */
	dq = blocks[faila];
	blocks[faila] = NULL;
	blocks[disks-1] = dq;

	/* in the 4-disk case we only need to perform a single source
	 * multiplication with the one good data block.
	 */
	if (good_srcs == 1) {
		struct page *g = blocks[good];

		init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL,
				  scribble);
		tx = async_memcpy(p, g, 0, 0, bytes, submit);

		init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL,
				  scribble);
		tx = async_mult(dq, g, raid6_gfexp[good], bytes, submit);
	} else {
		init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL,
				  scribble);
		tx = async_gen_syndrome(blocks, 0, disks, bytes, submit);
	}

	/* Restore pointer table */
	blocks[faila]   = dq;
	blocks[disks-1] = q;

	/* calculate g^{-faila} */
	coef = raid6_gfinv[raid6_gfexp[faila]];

	srcs[0] = dq;
	srcs[1] = q;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor(dq, srcs, 0, 2, bytes, submit);

	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_mult(dq, dq, coef, bytes, submit);

	srcs[0] = p;
	srcs[1] = dq;
	init_async_submit(submit, flags | ASYNC_TX_XOR_DROP_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor(p, srcs, 0, 2, bytes, submit);

	return tx;
}
EXPORT_SYMBOL_GPL(async_raid6_datap_recov);

MODULE_AUTHOR("Dan Williams <dan.j.williams@intel.com>");
MODULE_DESCRIPTION("asynchronous RAID-6 recovery api");
MODULE_LICENSE("GPL");
