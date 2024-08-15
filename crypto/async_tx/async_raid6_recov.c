// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Asynchronous RAID-6 recovery calculations ASYNC_TX API.
 * Copyright(c) 2009 Intel Corporation
 *
 * based on raid6recov.c:
 *   Copyright 2002 H. Peter Anvin
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/raid/pq.h>
#include <linux/async_tx.h>
#include <linux/dmaengine.h>

static struct dma_async_tx_descriptor *
async_sum_product(struct page *dest, unsigned int d_off,
		struct page **srcs, unsigned int *src_offs, unsigned char *coef,
		size_t len, struct async_submit_ctl *submit)
{
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_PQ,
						      &dest, 1, srcs, 2, len);
	struct dma_device *dma = chan ? chan->device : NULL;
	struct dmaengine_unmap_data *unmap = NULL;
	const u8 *amul, *bmul;
	u8 ax, bx;
	u8 *a, *b, *c;

	if (dma)
		unmap = dmaengine_get_unmap_data(dma->dev, 3, GFP_NOWAIT);

	if (unmap) {
		struct device *dev = dma->dev;
		dma_addr_t pq[2];
		struct dma_async_tx_descriptor *tx;
		enum dma_ctrl_flags dma_flags = DMA_PREP_PQ_DISABLE_P;

		if (submit->flags & ASYNC_TX_FENCE)
			dma_flags |= DMA_PREP_FENCE;
		unmap->addr[0] = dma_map_page(dev, srcs[0], src_offs[0],
						len, DMA_TO_DEVICE);
		unmap->addr[1] = dma_map_page(dev, srcs[1], src_offs[1],
						len, DMA_TO_DEVICE);
		unmap->to_cnt = 2;

		unmap->addr[2] = dma_map_page(dev, dest, d_off,
						len, DMA_BIDIRECTIONAL);
		unmap->bidi_cnt = 1;
		/* engine only looks at Q, but expects it to follow P */
		pq[1] = unmap->addr[2];

		unmap->len = len;
		tx = dma->device_prep_dma_pq(chan, pq, unmap->addr, 2, coef,
					     len, dma_flags);
		if (tx) {
			dma_set_unmap(tx, unmap);
			async_tx_submit(chan, tx, submit);
			dmaengine_unmap_put(unmap);
			return tx;
		}

		/* could not get a descriptor, unmap and fall through to
		 * the synchronous path
		 */
		dmaengine_unmap_put(unmap);
	}

	/* run the operation synchronously */
	async_tx_quiesce(&submit->depend_tx);
	amul = raid6_gfmul[coef[0]];
	bmul = raid6_gfmul[coef[1]];
	a = page_address(srcs[0]) + src_offs[0];
	b = page_address(srcs[1]) + src_offs[1];
	c = page_address(dest) + d_off;

	while (len--) {
		ax    = amul[*a++];
		bx    = bmul[*b++];
		*c++ = ax ^ bx;
	}

	return NULL;
}

static struct dma_async_tx_descriptor *
async_mult(struct page *dest, unsigned int d_off, struct page *src,
		unsigned int s_off, u8 coef, size_t len,
		struct async_submit_ctl *submit)
{
	struct dma_chan *chan = async_tx_find_channel(submit, DMA_PQ,
						      &dest, 1, &src, 1, len);
	struct dma_device *dma = chan ? chan->device : NULL;
	struct dmaengine_unmap_data *unmap = NULL;
	const u8 *qmul; /* Q multiplier table */
	u8 *d, *s;

	if (dma)
		unmap = dmaengine_get_unmap_data(dma->dev, 3, GFP_NOWAIT);

	if (unmap) {
		dma_addr_t dma_dest[2];
		struct device *dev = dma->dev;
		struct dma_async_tx_descriptor *tx;
		enum dma_ctrl_flags dma_flags = DMA_PREP_PQ_DISABLE_P;

		if (submit->flags & ASYNC_TX_FENCE)
			dma_flags |= DMA_PREP_FENCE;
		unmap->addr[0] = dma_map_page(dev, src, s_off,
						len, DMA_TO_DEVICE);
		unmap->to_cnt++;
		unmap->addr[1] = dma_map_page(dev, dest, d_off,
						len, DMA_BIDIRECTIONAL);
		dma_dest[1] = unmap->addr[1];
		unmap->bidi_cnt++;
		unmap->len = len;

		/* this looks funny, but the engine looks for Q at
		 * dma_dest[1] and ignores dma_dest[0] as a dest
		 * due to DMA_PREP_PQ_DISABLE_P
		 */
		tx = dma->device_prep_dma_pq(chan, dma_dest, unmap->addr,
					     1, &coef, len, dma_flags);

		if (tx) {
			dma_set_unmap(tx, unmap);
			dmaengine_unmap_put(unmap);
			async_tx_submit(chan, tx, submit);
			return tx;
		}

		/* could not get a descriptor, unmap and fall through to
		 * the synchronous path
		 */
		dmaengine_unmap_put(unmap);
	}

	/* no channel available, or failed to allocate a descriptor, so
	 * perform the operation synchronously
	 */
	async_tx_quiesce(&submit->depend_tx);
	qmul  = raid6_gfmul[coef];
	d = page_address(dest) + d_off;
	s = page_address(src) + s_off;

	while (len--)
		*d++ = qmul[*s++];

	return NULL;
}

static struct dma_async_tx_descriptor *
__2data_recov_4(int disks, size_t bytes, int faila, int failb,
		struct page **blocks, unsigned int *offs,
		struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *a, *b;
	unsigned int p_off, q_off, a_off, b_off;
	struct page *srcs[2];
	unsigned int src_offs[2];
	unsigned char coef[2];
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;

	p = blocks[disks-2];
	p_off = offs[disks-2];
	q = blocks[disks-1];
	q_off = offs[disks-1];

	a = blocks[faila];
	a_off = offs[faila];
	b = blocks[failb];
	b_off = offs[failb];

	/* in the 4 disk case P + Pxy == P and Q + Qxy == Q */
	/* Dx = A*(P+Pxy) + B*(Q+Qxy) */
	srcs[0] = p;
	src_offs[0] = p_off;
	srcs[1] = q;
	src_offs[1] = q_off;
	coef[0] = raid6_gfexi[failb-faila];
	coef[1] = raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]];
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_sum_product(b, b_off, srcs, src_offs, coef, bytes, submit);

	/* Dy = P+Pxy+Dx */
	srcs[0] = p;
	src_offs[0] = p_off;
	srcs[1] = b;
	src_offs[1] = b_off;
	init_async_submit(submit, flags | ASYNC_TX_XOR_ZERO_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor_offs(a, a_off, srcs, src_offs, 2, bytes, submit);

	return tx;

}

static struct dma_async_tx_descriptor *
__2data_recov_5(int disks, size_t bytes, int faila, int failb,
		struct page **blocks, unsigned int *offs,
		struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *g, *dp, *dq;
	unsigned int p_off, q_off, g_off, dp_off, dq_off;
	struct page *srcs[2];
	unsigned int src_offs[2];
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
	p_off = offs[disks-2];
	q = blocks[disks-1];
	q_off = offs[disks-1];
	g = blocks[good];
	g_off = offs[good];

	/* Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for delta p and
	 * delta q
	 */
	dp = blocks[faila];
	dp_off = offs[faila];
	dq = blocks[failb];
	dq_off = offs[failb];

	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_memcpy(dp, g, dp_off, g_off, bytes, submit);
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_mult(dq, dq_off, g, g_off,
			raid6_gfexp[good], bytes, submit);

	/* compute P + Pxy */
	srcs[0] = dp;
	src_offs[0] = dp_off;
	srcs[1] = p;
	src_offs[1] = p_off;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor_offs(dp, dp_off, srcs, src_offs, 2, bytes, submit);

	/* compute Q + Qxy */
	srcs[0] = dq;
	src_offs[0] = dq_off;
	srcs[1] = q;
	src_offs[1] = q_off;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor_offs(dq, dq_off, srcs, src_offs, 2, bytes, submit);

	/* Dx = A*(P+Pxy) + B*(Q+Qxy) */
	srcs[0] = dp;
	src_offs[0] = dp_off;
	srcs[1] = dq;
	src_offs[1] = dq_off;
	coef[0] = raid6_gfexi[failb-faila];
	coef[1] = raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]];
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_sum_product(dq, dq_off, srcs, src_offs, coef, bytes, submit);

	/* Dy = P+Pxy+Dx */
	srcs[0] = dp;
	src_offs[0] = dp_off;
	srcs[1] = dq;
	src_offs[1] = dq_off;
	init_async_submit(submit, flags | ASYNC_TX_XOR_DROP_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor_offs(dp, dp_off, srcs, src_offs, 2, bytes, submit);

	return tx;
}

static struct dma_async_tx_descriptor *
__2data_recov_n(int disks, size_t bytes, int faila, int failb,
	      struct page **blocks, unsigned int *offs,
		  struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *dp, *dq;
	unsigned int p_off, q_off, dp_off, dq_off;
	struct page *srcs[2];
	unsigned int src_offs[2];
	unsigned char coef[2];
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;

	p = blocks[disks-2];
	p_off = offs[disks-2];
	q = blocks[disks-1];
	q_off = offs[disks-1];

	/* Compute syndrome with zero for the missing data pages
	 * Use the dead data pages as temporary storage for
	 * delta p and delta q
	 */
	dp = blocks[faila];
	dp_off = offs[faila];
	blocks[faila] = NULL;
	blocks[disks-2] = dp;
	offs[disks-2] = dp_off;
	dq = blocks[failb];
	dq_off = offs[failb];
	blocks[failb] = NULL;
	blocks[disks-1] = dq;
	offs[disks-1] = dq_off;

	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_gen_syndrome(blocks, offs, disks, bytes, submit);

	/* Restore pointer table */
	blocks[faila]   = dp;
	offs[faila] = dp_off;
	blocks[failb]   = dq;
	offs[failb] = dq_off;
	blocks[disks-2] = p;
	offs[disks-2] = p_off;
	blocks[disks-1] = q;
	offs[disks-1] = q_off;

	/* compute P + Pxy */
	srcs[0] = dp;
	src_offs[0] = dp_off;
	srcs[1] = p;
	src_offs[1] = p_off;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor_offs(dp, dp_off, srcs, src_offs, 2, bytes, submit);

	/* compute Q + Qxy */
	srcs[0] = dq;
	src_offs[0] = dq_off;
	srcs[1] = q;
	src_offs[1] = q_off;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor_offs(dq, dq_off, srcs, src_offs, 2, bytes, submit);

	/* Dx = A*(P+Pxy) + B*(Q+Qxy) */
	srcs[0] = dp;
	src_offs[0] = dp_off;
	srcs[1] = dq;
	src_offs[1] = dq_off;
	coef[0] = raid6_gfexi[failb-faila];
	coef[1] = raid6_gfinv[raid6_gfexp[faila]^raid6_gfexp[failb]];
	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_sum_product(dq, dq_off, srcs, src_offs, coef, bytes, submit);

	/* Dy = P+Pxy+Dx */
	srcs[0] = dp;
	src_offs[0] = dp_off;
	srcs[1] = dq;
	src_offs[1] = dq_off;
	init_async_submit(submit, flags | ASYNC_TX_XOR_DROP_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor_offs(dp, dp_off, srcs, src_offs, 2, bytes, submit);

	return tx;
}

/**
 * async_raid6_2data_recov - asynchronously calculate two missing data blocks
 * @disks: number of disks in the RAID-6 array
 * @bytes: block size
 * @faila: first failed drive index
 * @failb: second failed drive index
 * @blocks: array of source pointers where the last two entries are p and q
 * @offs: array of offset for pages in blocks
 * @submit: submission/completion modifiers
 */
struct dma_async_tx_descriptor *
async_raid6_2data_recov(int disks, size_t bytes, int faila, int failb,
			struct page **blocks, unsigned int *offs,
			struct async_submit_ctl *submit)
{
	void *scribble = submit->scribble;
	int non_zero_srcs, i;

	BUG_ON(faila == failb);
	if (failb < faila)
		swap(faila, failb);

	pr_debug("%s: disks: %d len: %zu\n", __func__, disks, bytes);

	/* if a dma resource is not available or a scribble buffer is not
	 * available punt to the synchronous path.  In the 'dma not
	 * available' case be sure to use the scribble buffer to
	 * preserve the content of 'blocks' as the caller intended.
	 */
	if (!async_dma_find_channel(DMA_PQ) || !scribble) {
		void **ptrs = scribble ? scribble : (void **) blocks;

		async_tx_quiesce(&submit->depend_tx);
		for (i = 0; i < disks; i++)
			if (blocks[i] == NULL)
				ptrs[i] = (void *) raid6_empty_zero_page;
			else
				ptrs[i] = page_address(blocks[i]) + offs[i];

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
		return __2data_recov_4(disks, bytes, faila, failb,
				blocks, offs, submit);
	case 3:
		/* dma devices do not uniformly understand a single
		 * source pq operation (in contrast to the synchronous
		 * case), so explicitly handle the special case of a 5 disk
		 * array with 2 of 3 data disks missing.
		 */
		return __2data_recov_5(disks, bytes, faila, failb,
				blocks, offs, submit);
	default:
		return __2data_recov_n(disks, bytes, faila, failb,
				blocks, offs, submit);
	}
}
EXPORT_SYMBOL_GPL(async_raid6_2data_recov);

/**
 * async_raid6_datap_recov - asynchronously calculate a data and the 'p' block
 * @disks: number of disks in the RAID-6 array
 * @bytes: block size
 * @faila: failed drive index
 * @blocks: array of source pointers where the last two entries are p and q
 * @offs: array of offset for pages in blocks
 * @submit: submission/completion modifiers
 */
struct dma_async_tx_descriptor *
async_raid6_datap_recov(int disks, size_t bytes, int faila,
			struct page **blocks, unsigned int *offs,
			struct async_submit_ctl *submit)
{
	struct dma_async_tx_descriptor *tx = NULL;
	struct page *p, *q, *dq;
	unsigned int p_off, q_off, dq_off;
	u8 coef;
	enum async_tx_flags flags = submit->flags;
	dma_async_tx_callback cb_fn = submit->cb_fn;
	void *cb_param = submit->cb_param;
	void *scribble = submit->scribble;
	int good_srcs, good, i;
	struct page *srcs[2];
	unsigned int src_offs[2];

	pr_debug("%s: disks: %d len: %zu\n", __func__, disks, bytes);

	/* if a dma resource is not available or a scribble buffer is not
	 * available punt to the synchronous path.  In the 'dma not
	 * available' case be sure to use the scribble buffer to
	 * preserve the content of 'blocks' as the caller intended.
	 */
	if (!async_dma_find_channel(DMA_PQ) || !scribble) {
		void **ptrs = scribble ? scribble : (void **) blocks;

		async_tx_quiesce(&submit->depend_tx);
		for (i = 0; i < disks; i++)
			if (blocks[i] == NULL)
				ptrs[i] = (void*)raid6_empty_zero_page;
			else
				ptrs[i] = page_address(blocks[i]) + offs[i];

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
	p_off = offs[disks-2];
	q = blocks[disks-1];
	q_off = offs[disks-1];

	/* Compute syndrome with zero for the missing data page
	 * Use the dead data page as temporary storage for delta q
	 */
	dq = blocks[faila];
	dq_off = offs[faila];
	blocks[faila] = NULL;
	blocks[disks-1] = dq;
	offs[disks-1] = dq_off;

	/* in the 4-disk case we only need to perform a single source
	 * multiplication with the one good data block.
	 */
	if (good_srcs == 1) {
		struct page *g = blocks[good];
		unsigned int g_off = offs[good];

		init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL,
				  scribble);
		tx = async_memcpy(p, g, p_off, g_off, bytes, submit);

		init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL,
				  scribble);
		tx = async_mult(dq, dq_off, g, g_off,
				raid6_gfexp[good], bytes, submit);
	} else {
		init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL,
				  scribble);
		tx = async_gen_syndrome(blocks, offs, disks, bytes, submit);
	}

	/* Restore pointer table */
	blocks[faila]   = dq;
	offs[faila] = dq_off;
	blocks[disks-1] = q;
	offs[disks-1] = q_off;

	/* calculate g^{-faila} */
	coef = raid6_gfinv[raid6_gfexp[faila]];

	srcs[0] = dq;
	src_offs[0] = dq_off;
	srcs[1] = q;
	src_offs[1] = q_off;
	init_async_submit(submit, ASYNC_TX_FENCE|ASYNC_TX_XOR_DROP_DST, tx,
			  NULL, NULL, scribble);
	tx = async_xor_offs(dq, dq_off, srcs, src_offs, 2, bytes, submit);

	init_async_submit(submit, ASYNC_TX_FENCE, tx, NULL, NULL, scribble);
	tx = async_mult(dq, dq_off, dq, dq_off, coef, bytes, submit);

	srcs[0] = p;
	src_offs[0] = p_off;
	srcs[1] = dq;
	src_offs[1] = dq_off;
	init_async_submit(submit, flags | ASYNC_TX_XOR_DROP_DST, tx, cb_fn,
			  cb_param, scribble);
	tx = async_xor_offs(p, p_off, srcs, src_offs, 2, bytes, submit);

	return tx;
}
EXPORT_SYMBOL_GPL(async_raid6_datap_recov);

MODULE_AUTHOR("Dan Williams <dan.j.williams@intel.com>");
MODULE_DESCRIPTION("asynchronous RAID-6 recovery api");
MODULE_LICENSE("GPL");
