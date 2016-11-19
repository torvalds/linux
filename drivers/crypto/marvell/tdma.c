/*
 * Provide TDMA helper functions used by cipher and hash algorithm
 * implementations.
 *
 * Author: Boris Brezillon <boris.brezillon@free-electrons.com>
 * Author: Arnaud Ebalard <arno@natisbad.org>
 *
 * This work is based on an initial version written by
 * Sebastian Andrzej Siewior < sebastian at breakpoint dot cc >
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "cesa.h"

bool mv_cesa_req_dma_iter_next_transfer(struct mv_cesa_dma_iter *iter,
					struct mv_cesa_sg_dma_iter *sgiter,
					unsigned int len)
{
	if (!sgiter->sg)
		return false;

	sgiter->op_offset += len;
	sgiter->offset += len;
	if (sgiter->offset == sg_dma_len(sgiter->sg)) {
		if (sg_is_last(sgiter->sg))
			return false;
		sgiter->offset = 0;
		sgiter->sg = sg_next(sgiter->sg);
	}

	if (sgiter->op_offset == iter->op_len)
		return false;

	return true;
}

void mv_cesa_dma_step(struct mv_cesa_req *dreq)
{
	struct mv_cesa_engine *engine = dreq->engine;

	writel_relaxed(0, engine->regs + CESA_SA_CFG);

	mv_cesa_set_int_mask(engine, CESA_SA_INT_ACC0_IDMA_DONE);
	writel_relaxed(CESA_TDMA_DST_BURST_128B | CESA_TDMA_SRC_BURST_128B |
		       CESA_TDMA_NO_BYTE_SWAP | CESA_TDMA_EN,
		       engine->regs + CESA_TDMA_CONTROL);

	writel_relaxed(CESA_SA_CFG_ACT_CH0_IDMA | CESA_SA_CFG_MULTI_PKT |
		       CESA_SA_CFG_CH0_W_IDMA | CESA_SA_CFG_PARA_DIS,
		       engine->regs + CESA_SA_CFG);
	writel_relaxed(dreq->chain.first->cur_dma,
		       engine->regs + CESA_TDMA_NEXT_ADDR);
	BUG_ON(readl(engine->regs + CESA_SA_CMD) &
	       CESA_SA_CMD_EN_CESA_SA_ACCL0);
	writel(CESA_SA_CMD_EN_CESA_SA_ACCL0, engine->regs + CESA_SA_CMD);
}

void mv_cesa_dma_cleanup(struct mv_cesa_req *dreq)
{
	struct mv_cesa_tdma_desc *tdma;

	for (tdma = dreq->chain.first; tdma;) {
		struct mv_cesa_tdma_desc *old_tdma = tdma;
		u32 type = tdma->flags & CESA_TDMA_TYPE_MSK;

		if (type == CESA_TDMA_OP)
			dma_pool_free(cesa_dev->dma->op_pool, tdma->op,
				      le32_to_cpu(tdma->src));
		else if (type == CESA_TDMA_IV)
			dma_pool_free(cesa_dev->dma->iv_pool, tdma->data,
				      le32_to_cpu(tdma->dst));

		tdma = tdma->next;
		dma_pool_free(cesa_dev->dma->tdma_desc_pool, old_tdma,
			      old_tdma->cur_dma);
	}

	dreq->chain.first = NULL;
	dreq->chain.last = NULL;
}

void mv_cesa_dma_prepare(struct mv_cesa_req *dreq,
			 struct mv_cesa_engine *engine)
{
	struct mv_cesa_tdma_desc *tdma;

	for (tdma = dreq->chain.first; tdma; tdma = tdma->next) {
		if (tdma->flags & CESA_TDMA_DST_IN_SRAM)
			tdma->dst = cpu_to_le32(tdma->dst + engine->sram_dma);

		if (tdma->flags & CESA_TDMA_SRC_IN_SRAM)
			tdma->src = cpu_to_le32(tdma->src + engine->sram_dma);

		if ((tdma->flags & CESA_TDMA_TYPE_MSK) == CESA_TDMA_OP)
			mv_cesa_adjust_op(engine, tdma->op);
	}
}

void mv_cesa_tdma_chain(struct mv_cesa_engine *engine,
			struct mv_cesa_req *dreq)
{
	if (engine->chain.first == NULL && engine->chain.last == NULL) {
		engine->chain.first = dreq->chain.first;
		engine->chain.last  = dreq->chain.last;
	} else {
		struct mv_cesa_tdma_desc *last;

		last = engine->chain.last;
		last->next = dreq->chain.first;
		engine->chain.last = dreq->chain.last;

		if (!(last->flags & CESA_TDMA_BREAK_CHAIN))
			last->next_dma = dreq->chain.first->cur_dma;
	}
}

int mv_cesa_tdma_process(struct mv_cesa_engine *engine, u32 status)
{
	struct crypto_async_request *req = NULL;
	struct mv_cesa_tdma_desc *tdma = NULL, *next = NULL;
	dma_addr_t tdma_cur;
	int res = 0;

	tdma_cur = readl(engine->regs + CESA_TDMA_CUR);

	for (tdma = engine->chain.first; tdma; tdma = next) {
		spin_lock_bh(&engine->lock);
		next = tdma->next;
		spin_unlock_bh(&engine->lock);

		if (tdma->flags & CESA_TDMA_END_OF_REQ) {
			struct crypto_async_request *backlog = NULL;
			struct mv_cesa_ctx *ctx;
			u32 current_status;

			spin_lock_bh(&engine->lock);
			/*
			 * if req is NULL, this means we're processing the
			 * request in engine->req.
			 */
			if (!req)
				req = engine->req;
			else
				req = mv_cesa_dequeue_req_locked(engine,
								 &backlog);

			/* Re-chaining to the next request */
			engine->chain.first = tdma->next;
			tdma->next = NULL;

			/* If this is the last request, clear the chain */
			if (engine->chain.first == NULL)
				engine->chain.last  = NULL;
			spin_unlock_bh(&engine->lock);

			ctx = crypto_tfm_ctx(req->tfm);
			current_status = (tdma->cur_dma == tdma_cur) ?
					  status : CESA_SA_INT_ACC0_IDMA_DONE;
			res = ctx->ops->process(req, current_status);
			ctx->ops->complete(req);

			if (res == 0)
				mv_cesa_engine_enqueue_complete_request(engine,
									req);

			if (backlog)
				backlog->complete(backlog, -EINPROGRESS);
		}

		if (res || tdma->cur_dma == tdma_cur)
			break;
	}

	/* Save the last request in error to engine->req, so that the core
	 * knows which request was fautly */
	if (res) {
		spin_lock_bh(&engine->lock);
		engine->req = req;
		spin_unlock_bh(&engine->lock);
	}

	return res;
}

static struct mv_cesa_tdma_desc *
mv_cesa_dma_add_desc(struct mv_cesa_tdma_chain *chain, gfp_t flags)
{
	struct mv_cesa_tdma_desc *new_tdma = NULL;
	dma_addr_t dma_handle;

	new_tdma = dma_pool_zalloc(cesa_dev->dma->tdma_desc_pool, flags,
				   &dma_handle);
	if (!new_tdma)
		return ERR_PTR(-ENOMEM);

	new_tdma->cur_dma = dma_handle;
	if (chain->last) {
		chain->last->next_dma = cpu_to_le32(dma_handle);
		chain->last->next = new_tdma;
	} else {
		chain->first = new_tdma;
	}

	chain->last = new_tdma;

	return new_tdma;
}

int mv_cesa_dma_add_iv_op(struct mv_cesa_tdma_chain *chain, dma_addr_t src,
			  u32 size, u32 flags, gfp_t gfp_flags)
{

	struct mv_cesa_tdma_desc *tdma;
	u8 *iv;
	dma_addr_t dma_handle;

	tdma = mv_cesa_dma_add_desc(chain, gfp_flags);
	if (IS_ERR(tdma))
		return PTR_ERR(tdma);

	iv = dma_pool_alloc(cesa_dev->dma->iv_pool, gfp_flags, &dma_handle);
	if (!iv)
		return -ENOMEM;

	tdma->byte_cnt = cpu_to_le32(size | BIT(31));
	tdma->src = src;
	tdma->dst = cpu_to_le32(dma_handle);
	tdma->data = iv;

	flags &= (CESA_TDMA_DST_IN_SRAM | CESA_TDMA_SRC_IN_SRAM);
	tdma->flags = flags | CESA_TDMA_IV;
	return 0;
}

struct mv_cesa_op_ctx *mv_cesa_dma_add_op(struct mv_cesa_tdma_chain *chain,
					const struct mv_cesa_op_ctx *op_templ,
					bool skip_ctx,
					gfp_t flags)
{
	struct mv_cesa_tdma_desc *tdma;
	struct mv_cesa_op_ctx *op;
	dma_addr_t dma_handle;
	unsigned int size;

	tdma = mv_cesa_dma_add_desc(chain, flags);
	if (IS_ERR(tdma))
		return ERR_CAST(tdma);

	op = dma_pool_alloc(cesa_dev->dma->op_pool, flags, &dma_handle);
	if (!op)
		return ERR_PTR(-ENOMEM);

	*op = *op_templ;

	size = skip_ctx ? sizeof(op->desc) : sizeof(*op);

	tdma = chain->last;
	tdma->op = op;
	tdma->byte_cnt = cpu_to_le32(size | BIT(31));
	tdma->src = cpu_to_le32(dma_handle);
	tdma->dst = CESA_SA_CFG_SRAM_OFFSET;
	tdma->flags = CESA_TDMA_DST_IN_SRAM | CESA_TDMA_OP;

	return op;
}

int mv_cesa_dma_add_data_transfer(struct mv_cesa_tdma_chain *chain,
				  dma_addr_t dst, dma_addr_t src, u32 size,
				  u32 flags, gfp_t gfp_flags)
{
	struct mv_cesa_tdma_desc *tdma;

	tdma = mv_cesa_dma_add_desc(chain, gfp_flags);
	if (IS_ERR(tdma))
		return PTR_ERR(tdma);

	tdma->byte_cnt = cpu_to_le32(size | BIT(31));
	tdma->src = src;
	tdma->dst = dst;

	flags &= (CESA_TDMA_DST_IN_SRAM | CESA_TDMA_SRC_IN_SRAM);
	tdma->flags = flags | CESA_TDMA_DATA;

	return 0;
}

int mv_cesa_dma_add_dummy_launch(struct mv_cesa_tdma_chain *chain, gfp_t flags)
{
	struct mv_cesa_tdma_desc *tdma;

	tdma = mv_cesa_dma_add_desc(chain, flags);
	if (IS_ERR(tdma))
		return PTR_ERR(tdma);

	return 0;
}

int mv_cesa_dma_add_dummy_end(struct mv_cesa_tdma_chain *chain, gfp_t flags)
{
	struct mv_cesa_tdma_desc *tdma;

	tdma = mv_cesa_dma_add_desc(chain, flags);
	if (IS_ERR(tdma))
		return PTR_ERR(tdma);

	tdma->byte_cnt = cpu_to_le32(BIT(31));

	return 0;
}

int mv_cesa_dma_add_op_transfers(struct mv_cesa_tdma_chain *chain,
				 struct mv_cesa_dma_iter *dma_iter,
				 struct mv_cesa_sg_dma_iter *sgiter,
				 gfp_t gfp_flags)
{
	u32 flags = sgiter->dir == DMA_TO_DEVICE ?
		    CESA_TDMA_DST_IN_SRAM : CESA_TDMA_SRC_IN_SRAM;
	unsigned int len;

	do {
		dma_addr_t dst, src;
		int ret;

		len = mv_cesa_req_dma_iter_transfer_len(dma_iter, sgiter);
		if (sgiter->dir == DMA_TO_DEVICE) {
			dst = CESA_SA_DATA_SRAM_OFFSET + sgiter->op_offset;
			src = sg_dma_address(sgiter->sg) + sgiter->offset;
		} else {
			dst = sg_dma_address(sgiter->sg) + sgiter->offset;
			src = CESA_SA_DATA_SRAM_OFFSET + sgiter->op_offset;
		}

		ret = mv_cesa_dma_add_data_transfer(chain, dst, src, len,
						    flags, gfp_flags);
		if (ret)
			return ret;

	} while (mv_cesa_req_dma_iter_next_transfer(dma_iter, sgiter, len));

	return 0;
}
