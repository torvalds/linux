/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/prefetch.h>
#include "../dmaengine.h"
#include "registers.h"
#include "hw.h"
#include "dma.h"

#define MAX_SCF	1024

/* provide a lookup table for setting the source address in the base or
 * extended descriptor of an xor or pq descriptor
 */
static const u8 xor_idx_to_desc = 0xe0;
static const u8 xor_idx_to_field[] = { 1, 4, 5, 6, 7, 0, 1, 2 };
static const u8 pq_idx_to_desc = 0xf8;
static const u8 pq16_idx_to_desc[] = { 0, 0, 1, 1, 1, 1, 1, 1, 1,
				       2, 2, 2, 2, 2, 2, 2 };
static const u8 pq_idx_to_field[] = { 1, 4, 5, 0, 1, 2, 4, 5 };
static const u8 pq16_idx_to_field[] = { 1, 4, 1, 2, 3, 4, 5, 6, 7,
					0, 1, 2, 3, 4, 5, 6 };

static void xor_set_src(struct ioat_raw_descriptor *descs[2],
			dma_addr_t addr, u32 offset, int idx)
{
	struct ioat_raw_descriptor *raw = descs[xor_idx_to_desc >> idx & 1];

	raw->field[xor_idx_to_field[idx]] = addr + offset;
}

static dma_addr_t pq_get_src(struct ioat_raw_descriptor *descs[2], int idx)
{
	struct ioat_raw_descriptor *raw = descs[pq_idx_to_desc >> idx & 1];

	return raw->field[pq_idx_to_field[idx]];
}

static dma_addr_t pq16_get_src(struct ioat_raw_descriptor *desc[3], int idx)
{
	struct ioat_raw_descriptor *raw = desc[pq16_idx_to_desc[idx]];

	return raw->field[pq16_idx_to_field[idx]];
}

static void pq_set_src(struct ioat_raw_descriptor *descs[2],
		       dma_addr_t addr, u32 offset, u8 coef, int idx)
{
	struct ioat_pq_descriptor *pq = (struct ioat_pq_descriptor *) descs[0];
	struct ioat_raw_descriptor *raw = descs[pq_idx_to_desc >> idx & 1];

	raw->field[pq_idx_to_field[idx]] = addr + offset;
	pq->coef[idx] = coef;
}

static void pq16_set_src(struct ioat_raw_descriptor *desc[3],
			dma_addr_t addr, u32 offset, u8 coef, unsigned idx)
{
	struct ioat_pq_descriptor *pq = (struct ioat_pq_descriptor *)desc[0];
	struct ioat_pq16a_descriptor *pq16 =
		(struct ioat_pq16a_descriptor *)desc[1];
	struct ioat_raw_descriptor *raw = desc[pq16_idx_to_desc[idx]];

	raw->field[pq16_idx_to_field[idx]] = addr + offset;

	if (idx < 8)
		pq->coef[idx] = coef;
	else
		pq16->coef[idx - 8] = coef;
}

static struct ioat_sed_ent *
ioat3_alloc_sed(struct ioatdma_device *ioat_dma, unsigned int hw_pool)
{
	struct ioat_sed_ent *sed;
	gfp_t flags = __GFP_ZERO | GFP_ATOMIC;

	sed = kmem_cache_alloc(ioat_sed_cache, flags);
	if (!sed)
		return NULL;

	sed->hw_pool = hw_pool;
	sed->hw = dma_pool_alloc(ioat_dma->sed_hw_pool[hw_pool],
				 flags, &sed->dma);
	if (!sed->hw) {
		kmem_cache_free(ioat_sed_cache, sed);
		return NULL;
	}

	return sed;
}

struct dma_async_tx_descriptor *
ioat_dma_prep_memcpy_lock(struct dma_chan *c, dma_addr_t dma_dest,
			   dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_dma_descriptor *hw;
	struct ioat_ring_ent *desc;
	dma_addr_t dst = dma_dest;
	dma_addr_t src = dma_src;
	size_t total_len = len;
	int num_descs, idx, i;

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);
	if (likely(num_descs) &&
	    ioat_check_space_lock(ioat_chan, num_descs) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		size_t copy = min_t(size_t, len, 1 << ioat_chan->xfercap_log);

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		hw = desc->hw;

		hw->size = copy;
		hw->ctl = 0;
		hw->src_addr = src;
		hw->dst_addr = dst;

		len -= copy;
		dst += copy;
		src += copy;
		dump_desc_dbg(ioat_chan, desc);
	} while (++i < num_descs);

	desc->txd.flags = flags;
	desc->len = total_len;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	hw->ctl_f.compl_write = 1;
	dump_desc_dbg(ioat_chan, desc);
	/* we leave the channel locked to ensure in order submission */

	return &desc->txd;
}


static struct dma_async_tx_descriptor *
__ioat_prep_xor_lock(struct dma_chan *c, enum sum_check_flags *result,
		      dma_addr_t dest, dma_addr_t *src, unsigned int src_cnt,
		      size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent *compl_desc;
	struct ioat_ring_ent *desc;
	struct ioat_ring_ent *ext;
	size_t total_len = len;
	struct ioat_xor_descriptor *xor;
	struct ioat_xor_ext_descriptor *xor_ex = NULL;
	struct ioat_dma_descriptor *hw;
	int num_descs, with_ext, idx, i;
	u32 offset = 0;
	u8 op = result ? IOAT_OP_XOR_VAL : IOAT_OP_XOR;

	BUG_ON(src_cnt < 2);

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);
	/* we need 2x the number of descriptors to cover greater than 5
	 * sources
	 */
	if (src_cnt > 5) {
		with_ext = 1;
		num_descs *= 2;
	} else
		with_ext = 0;

	/* completion writes from the raid engine may pass completion
	 * writes from the legacy engine, so we need one extra null
	 * (legacy) descriptor to ensure all completion writes arrive in
	 * order.
	 */
	if (likely(num_descs) &&
	    ioat_check_space_lock(ioat_chan, num_descs+1) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t,
					 len, 1 << ioat_chan->xfercap_log);
		int s;

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		xor = desc->xor;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor xor_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat_get_ring_ent(ioat_chan, idx + i + 1);
		xor_ex = ext->xor_ex;

		descs[0] = (struct ioat_raw_descriptor *) xor;
		descs[1] = (struct ioat_raw_descriptor *) xor_ex;
		for (s = 0; s < src_cnt; s++)
			xor_set_src(descs, src[s], offset, s);
		xor->size = xfer_size;
		xor->dst_addr = dest + offset;
		xor->ctl = 0;
		xor->ctl_f.op = op;
		xor->ctl_f.src_cnt = src_cnt_to_hw(src_cnt);

		len -= xfer_size;
		offset += xfer_size;
		dump_desc_dbg(ioat_chan, desc);
	} while ((i += 1 + with_ext) < num_descs);

	/* last xor descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	xor->ctl_f.fence = !!(flags & DMA_PREP_FENCE);

	/* completion descriptor carries interrupt bit */
	compl_desc = ioat_get_ring_ent(ioat_chan, idx + i);
	compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
	hw = compl_desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	hw->ctl_f.compl_write = 1;
	hw->size = NULL_DESC_BUFFER_SIZE;
	dump_desc_dbg(ioat_chan, compl_desc);

	/* we leave the channel locked to ensure in order submission */
	return &compl_desc->txd;
}

struct dma_async_tx_descriptor *
ioat_prep_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
	       unsigned int src_cnt, size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	return __ioat_prep_xor_lock(chan, NULL, dest, src, src_cnt, len, flags);
}

struct dma_async_tx_descriptor *
ioat_prep_xor_val(struct dma_chan *chan, dma_addr_t *src,
		    unsigned int src_cnt, size_t len,
		    enum sum_check_flags *result, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*result = 0;

	return __ioat_prep_xor_lock(chan, result, src[0], &src[1],
				     src_cnt - 1, len, flags);
}

static void
dump_pq_desc_dbg(struct ioatdma_chan *ioat_chan, struct ioat_ring_ent *desc,
		 struct ioat_ring_ent *ext)
{
	struct device *dev = to_dev(ioat_chan);
	struct ioat_pq_descriptor *pq = desc->pq;
	struct ioat_pq_ext_descriptor *pq_ex = ext ? ext->pq_ex : NULL;
	struct ioat_raw_descriptor *descs[] = { (void *) pq, (void *) pq_ex };
	int src_cnt = src_cnt_to_sw(pq->ctl_f.src_cnt);
	int i;

	dev_dbg(dev, "desc[%d]: (%#llx->%#llx) flags: %#x"
		" sz: %#10.8x ctl: %#x (op: %#x int: %d compl: %d pq: '%s%s'"
		" src_cnt: %d)\n",
		desc_id(desc), (unsigned long long) desc->txd.phys,
		(unsigned long long) (pq_ex ? pq_ex->next : pq->next),
		desc->txd.flags, pq->size, pq->ctl, pq->ctl_f.op,
		pq->ctl_f.int_en, pq->ctl_f.compl_write,
		pq->ctl_f.p_disable ? "" : "p", pq->ctl_f.q_disable ? "" : "q",
		pq->ctl_f.src_cnt);
	for (i = 0; i < src_cnt; i++)
		dev_dbg(dev, "\tsrc[%d]: %#llx coef: %#x\n", i,
			(unsigned long long) pq_get_src(descs, i), pq->coef[i]);
	dev_dbg(dev, "\tP: %#llx\n", pq->p_addr);
	dev_dbg(dev, "\tQ: %#llx\n", pq->q_addr);
	dev_dbg(dev, "\tNEXT: %#llx\n", pq->next);
}

static void dump_pq16_desc_dbg(struct ioatdma_chan *ioat_chan,
			       struct ioat_ring_ent *desc)
{
	struct device *dev = to_dev(ioat_chan);
	struct ioat_pq_descriptor *pq = desc->pq;
	struct ioat_raw_descriptor *descs[] = { (void *)pq,
						(void *)pq,
						(void *)pq };
	int src_cnt = src16_cnt_to_sw(pq->ctl_f.src_cnt);
	int i;

	if (desc->sed) {
		descs[1] = (void *)desc->sed->hw;
		descs[2] = (void *)desc->sed->hw + 64;
	}

	dev_dbg(dev, "desc[%d]: (%#llx->%#llx) flags: %#x"
		" sz: %#x ctl: %#x (op: %#x int: %d compl: %d pq: '%s%s'"
		" src_cnt: %d)\n",
		desc_id(desc), (unsigned long long) desc->txd.phys,
		(unsigned long long) pq->next,
		desc->txd.flags, pq->size, pq->ctl,
		pq->ctl_f.op, pq->ctl_f.int_en,
		pq->ctl_f.compl_write,
		pq->ctl_f.p_disable ? "" : "p", pq->ctl_f.q_disable ? "" : "q",
		pq->ctl_f.src_cnt);
	for (i = 0; i < src_cnt; i++) {
		dev_dbg(dev, "\tsrc[%d]: %#llx coef: %#x\n", i,
			(unsigned long long) pq16_get_src(descs, i),
			pq->coef[i]);
	}
	dev_dbg(dev, "\tP: %#llx\n", pq->p_addr);
	dev_dbg(dev, "\tQ: %#llx\n", pq->q_addr);
}

static struct dma_async_tx_descriptor *
__ioat_prep_pq_lock(struct dma_chan *c, enum sum_check_flags *result,
		     const dma_addr_t *dst, const dma_addr_t *src,
		     unsigned int src_cnt, const unsigned char *scf,
		     size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *compl_desc;
	struct ioat_ring_ent *desc;
	struct ioat_ring_ent *ext;
	size_t total_len = len;
	struct ioat_pq_descriptor *pq;
	struct ioat_pq_ext_descriptor *pq_ex = NULL;
	struct ioat_dma_descriptor *hw;
	u32 offset = 0;
	u8 op = result ? IOAT_OP_PQ_VAL : IOAT_OP_PQ;
	int i, s, idx, with_ext, num_descs;
	int cb32 = (ioat_dma->version < IOAT_VER_3_3) ? 1 : 0;

	dev_dbg(to_dev(ioat_chan), "%s\n", __func__);
	/* the engine requires at least two sources (we provide
	 * at least 1 implied source in the DMA_PREP_CONTINUE case)
	 */
	BUG_ON(src_cnt + dmaf_continue(flags) < 2);

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);
	/* we need 2x the number of descriptors to cover greater than 3
	 * sources (we need 1 extra source in the q-only continuation
	 * case and 3 extra sources in the p+q continuation case.
	 */
	if (src_cnt + dmaf_p_disabled_continue(flags) > 3 ||
	    (dmaf_continue(flags) && !dmaf_p_disabled_continue(flags))) {
		with_ext = 1;
		num_descs *= 2;
	} else
		with_ext = 0;

	/* completion writes from the raid engine may pass completion
	 * writes from the legacy engine, so we need one extra null
	 * (legacy) descriptor to ensure all completion writes arrive in
	 * order.
	 */
	if (likely(num_descs) &&
	    ioat_check_space_lock(ioat_chan, num_descs + cb32) == 0)
		idx = ioat_chan->head;
	else
		return NULL;
	i = 0;
	do {
		struct ioat_raw_descriptor *descs[2];
		size_t xfer_size = min_t(size_t, len,
					 1 << ioat_chan->xfercap_log);

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		pq = desc->pq;

		/* save a branch by unconditionally retrieving the
		 * extended descriptor pq_set_src() knows to not write
		 * to it in the single descriptor case
		 */
		ext = ioat_get_ring_ent(ioat_chan, idx + i + with_ext);
		pq_ex = ext->pq_ex;

		descs[0] = (struct ioat_raw_descriptor *) pq;
		descs[1] = (struct ioat_raw_descriptor *) pq_ex;

		for (s = 0; s < src_cnt; s++)
			pq_set_src(descs, src[s], offset, scf[s], s);

		/* see the comment for dma_maxpq in include/linux/dmaengine.h */
		if (dmaf_p_disabled_continue(flags))
			pq_set_src(descs, dst[1], offset, 1, s++);
		else if (dmaf_continue(flags)) {
			pq_set_src(descs, dst[0], offset, 0, s++);
			pq_set_src(descs, dst[1], offset, 1, s++);
			pq_set_src(descs, dst[1], offset, 0, s++);
		}
		pq->size = xfer_size;
		pq->p_addr = dst[0] + offset;
		pq->q_addr = dst[1] + offset;
		pq->ctl = 0;
		pq->ctl_f.op = op;
		/* we turn on descriptor write back error status */
		if (ioat_dma->cap & IOAT_CAP_DWBES)
			pq->ctl_f.wb_en = result ? 1 : 0;
		pq->ctl_f.src_cnt = src_cnt_to_hw(s);
		pq->ctl_f.p_disable = !!(flags & DMA_PREP_PQ_DISABLE_P);
		pq->ctl_f.q_disable = !!(flags & DMA_PREP_PQ_DISABLE_Q);

		len -= xfer_size;
		offset += xfer_size;
	} while ((i += 1 + with_ext) < num_descs);

	/* last pq descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	pq->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	dump_pq_desc_dbg(ioat_chan, desc, ext);

	if (!cb32) {
		pq->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
		pq->ctl_f.compl_write = 1;
		compl_desc = desc;
	} else {
		/* completion descriptor carries interrupt bit */
		compl_desc = ioat_get_ring_ent(ioat_chan, idx + i);
		compl_desc->txd.flags = flags & DMA_PREP_INTERRUPT;
		hw = compl_desc->hw;
		hw->ctl = 0;
		hw->ctl_f.null = 1;
		hw->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
		hw->ctl_f.compl_write = 1;
		hw->size = NULL_DESC_BUFFER_SIZE;
		dump_desc_dbg(ioat_chan, compl_desc);
	}


	/* we leave the channel locked to ensure in order submission */
	return &compl_desc->txd;
}

static struct dma_async_tx_descriptor *
__ioat_prep_pq16_lock(struct dma_chan *c, enum sum_check_flags *result,
		       const dma_addr_t *dst, const dma_addr_t *src,
		       unsigned int src_cnt, const unsigned char *scf,
		       size_t len, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioatdma_device *ioat_dma = ioat_chan->ioat_dma;
	struct ioat_ring_ent *desc;
	size_t total_len = len;
	struct ioat_pq_descriptor *pq;
	u32 offset = 0;
	u8 op;
	int i, s, idx, num_descs;

	/* this function is only called with 9-16 sources */
	op = result ? IOAT_OP_PQ_VAL_16S : IOAT_OP_PQ_16S;

	dev_dbg(to_dev(ioat_chan), "%s\n", __func__);

	num_descs = ioat_xferlen_to_descs(ioat_chan, len);

	/*
	 * 16 source pq is only available on cb3.3 and has no completion
	 * write hw bug.
	 */
	if (num_descs && ioat_check_space_lock(ioat_chan, num_descs) == 0)
		idx = ioat_chan->head;
	else
		return NULL;

	i = 0;

	do {
		struct ioat_raw_descriptor *descs[4];
		size_t xfer_size = min_t(size_t, len,
					 1 << ioat_chan->xfercap_log);

		desc = ioat_get_ring_ent(ioat_chan, idx + i);
		pq = desc->pq;

		descs[0] = (struct ioat_raw_descriptor *) pq;

		desc->sed = ioat3_alloc_sed(ioat_dma, (src_cnt-2) >> 3);
		if (!desc->sed) {
			dev_err(to_dev(ioat_chan),
				"%s: no free sed entries\n", __func__);
			return NULL;
		}

		pq->sed_addr = desc->sed->dma;
		desc->sed->parent = desc;

		descs[1] = (struct ioat_raw_descriptor *)desc->sed->hw;
		descs[2] = (void *)descs[1] + 64;

		for (s = 0; s < src_cnt; s++)
			pq16_set_src(descs, src[s], offset, scf[s], s);

		/* see the comment for dma_maxpq in include/linux/dmaengine.h */
		if (dmaf_p_disabled_continue(flags))
			pq16_set_src(descs, dst[1], offset, 1, s++);
		else if (dmaf_continue(flags)) {
			pq16_set_src(descs, dst[0], offset, 0, s++);
			pq16_set_src(descs, dst[1], offset, 1, s++);
			pq16_set_src(descs, dst[1], offset, 0, s++);
		}

		pq->size = xfer_size;
		pq->p_addr = dst[0] + offset;
		pq->q_addr = dst[1] + offset;
		pq->ctl = 0;
		pq->ctl_f.op = op;
		pq->ctl_f.src_cnt = src16_cnt_to_hw(s);
		/* we turn on descriptor write back error status */
		if (ioat_dma->cap & IOAT_CAP_DWBES)
			pq->ctl_f.wb_en = result ? 1 : 0;
		pq->ctl_f.p_disable = !!(flags & DMA_PREP_PQ_DISABLE_P);
		pq->ctl_f.q_disable = !!(flags & DMA_PREP_PQ_DISABLE_Q);

		len -= xfer_size;
		offset += xfer_size;
	} while (++i < num_descs);

	/* last pq descriptor carries the unmap parameters and fence bit */
	desc->txd.flags = flags;
	desc->len = total_len;
	if (result)
		desc->result = result;
	pq->ctl_f.fence = !!(flags & DMA_PREP_FENCE);

	/* with cb3.3 we should be able to do completion w/o a null desc */
	pq->ctl_f.int_en = !!(flags & DMA_PREP_INTERRUPT);
	pq->ctl_f.compl_write = 1;

	dump_pq16_desc_dbg(ioat_chan, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

static int src_cnt_flags(unsigned int src_cnt, unsigned long flags)
{
	if (dmaf_p_disabled_continue(flags))
		return src_cnt + 1;
	else if (dmaf_continue(flags))
		return src_cnt + 3;
	else
		return src_cnt;
}

struct dma_async_tx_descriptor *
ioat_prep_pq(struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
	      unsigned int src_cnt, const unsigned char *scf, size_t len,
	      unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	/* specify valid address for disabled result */
	if (flags & DMA_PREP_PQ_DISABLE_P)
		dst[0] = dst[1];
	if (flags & DMA_PREP_PQ_DISABLE_Q)
		dst[1] = dst[0];

	/* handle the single source multiply case from the raid6
	 * recovery path
	 */
	if ((flags & DMA_PREP_PQ_DISABLE_P) && src_cnt == 1) {
		dma_addr_t single_source[2];
		unsigned char single_source_coef[2];

		BUG_ON(flags & DMA_PREP_PQ_DISABLE_Q);
		single_source[0] = src[0];
		single_source[1] = src[0];
		single_source_coef[0] = scf[0];
		single_source_coef[1] = 0;

		return src_cnt_flags(src_cnt, flags) > 8 ?
			__ioat_prep_pq16_lock(chan, NULL, dst, single_source,
					       2, single_source_coef, len,
					       flags) :
			__ioat_prep_pq_lock(chan, NULL, dst, single_source, 2,
					     single_source_coef, len, flags);

	} else {
		return src_cnt_flags(src_cnt, flags) > 8 ?
			__ioat_prep_pq16_lock(chan, NULL, dst, src, src_cnt,
					       scf, len, flags) :
			__ioat_prep_pq_lock(chan, NULL, dst, src, src_cnt,
					     scf, len, flags);
	}
}

struct dma_async_tx_descriptor *
ioat_prep_pq_val(struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
		  unsigned int src_cnt, const unsigned char *scf, size_t len,
		  enum sum_check_flags *pqres, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	/* specify valid address for disabled result */
	if (flags & DMA_PREP_PQ_DISABLE_P)
		pq[0] = pq[1];
	if (flags & DMA_PREP_PQ_DISABLE_Q)
		pq[1] = pq[0];

	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*pqres = 0;

	return src_cnt_flags(src_cnt, flags) > 8 ?
		__ioat_prep_pq16_lock(chan, pqres, pq, src, src_cnt, scf, len,
				       flags) :
		__ioat_prep_pq_lock(chan, pqres, pq, src, src_cnt, scf, len,
				     flags);
}

struct dma_async_tx_descriptor *
ioat_prep_pqxor(struct dma_chan *chan, dma_addr_t dst, dma_addr_t *src,
		 unsigned int src_cnt, size_t len, unsigned long flags)
{
	unsigned char scf[MAX_SCF];
	dma_addr_t pq[2];
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	if (src_cnt > MAX_SCF)
		return NULL;

	memset(scf, 0, src_cnt);
	pq[0] = dst;
	flags |= DMA_PREP_PQ_DISABLE_Q;
	pq[1] = dst; /* specify valid address for disabled result */

	return src_cnt_flags(src_cnt, flags) > 8 ?
		__ioat_prep_pq16_lock(chan, NULL, pq, src, src_cnt, scf, len,
				       flags) :
		__ioat_prep_pq_lock(chan, NULL, pq, src, src_cnt, scf, len,
				     flags);
}

struct dma_async_tx_descriptor *
ioat_prep_pqxor_val(struct dma_chan *chan, dma_addr_t *src,
		     unsigned int src_cnt, size_t len,
		     enum sum_check_flags *result, unsigned long flags)
{
	unsigned char scf[MAX_SCF];
	dma_addr_t pq[2];
	struct ioatdma_chan *ioat_chan = to_ioat_chan(chan);

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	if (src_cnt > MAX_SCF)
		return NULL;

	/* the cleanup routine only sets bits on validate failure, it
	 * does not clear bits on validate success... so clear it here
	 */
	*result = 0;

	memset(scf, 0, src_cnt);
	pq[0] = src[0];
	flags |= DMA_PREP_PQ_DISABLE_Q;
	pq[1] = pq[0]; /* specify valid address for disabled result */

	return src_cnt_flags(src_cnt, flags) > 8 ?
		__ioat_prep_pq16_lock(chan, result, pq, &src[1], src_cnt - 1,
				       scf, len, flags) :
		__ioat_prep_pq_lock(chan, result, pq, &src[1], src_cnt - 1,
				     scf, len, flags);
}

struct dma_async_tx_descriptor *
ioat_prep_interrupt_lock(struct dma_chan *c, unsigned long flags)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
	struct ioat_ring_ent *desc;
	struct ioat_dma_descriptor *hw;

	if (test_bit(IOAT_CHAN_DOWN, &ioat_chan->state))
		return NULL;

	if (ioat_check_space_lock(ioat_chan, 1) == 0)
		desc = ioat_get_ring_ent(ioat_chan, ioat_chan->head);
	else
		return NULL;

	hw = desc->hw;
	hw->ctl = 0;
	hw->ctl_f.null = 1;
	hw->ctl_f.int_en = 1;
	hw->ctl_f.fence = !!(flags & DMA_PREP_FENCE);
	hw->ctl_f.compl_write = 1;
	hw->size = NULL_DESC_BUFFER_SIZE;
	hw->src_addr = 0;
	hw->dst_addr = 0;

	desc->txd.flags = flags;
	desc->len = 1;

	dump_desc_dbg(ioat_chan, desc);

	/* we leave the channel locked to ensure in order submission */
	return &desc->txd;
}

