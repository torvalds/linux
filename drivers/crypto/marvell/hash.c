/*
 * Hash algorithms supported by the CESA: MD5, SHA1 and SHA256.
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

#include <crypto/hmac.h>
#include <crypto/md5.h>
#include <crypto/sha.h>

#include "cesa.h"

struct mv_cesa_ahash_dma_iter {
	struct mv_cesa_dma_iter base;
	struct mv_cesa_sg_dma_iter src;
};

static inline void
mv_cesa_ahash_req_iter_init(struct mv_cesa_ahash_dma_iter *iter,
			    struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	unsigned int len = req->nbytes + creq->cache_ptr;

	if (!creq->last_req)
		len &= ~CESA_HASH_BLOCK_SIZE_MSK;

	mv_cesa_req_dma_iter_init(&iter->base, len);
	mv_cesa_sg_dma_iter_init(&iter->src, req->src, DMA_TO_DEVICE);
	iter->src.op_offset = creq->cache_ptr;
}

static inline bool
mv_cesa_ahash_req_iter_next_op(struct mv_cesa_ahash_dma_iter *iter)
{
	iter->src.op_offset = 0;

	return mv_cesa_req_dma_iter_next_op(&iter->base);
}

static inline int
mv_cesa_ahash_dma_alloc_cache(struct mv_cesa_ahash_dma_req *req, gfp_t flags)
{
	req->cache = dma_pool_alloc(cesa_dev->dma->cache_pool, flags,
				    &req->cache_dma);
	if (!req->cache)
		return -ENOMEM;

	return 0;
}

static inline void
mv_cesa_ahash_dma_free_cache(struct mv_cesa_ahash_dma_req *req)
{
	if (!req->cache)
		return;

	dma_pool_free(cesa_dev->dma->cache_pool, req->cache,
		      req->cache_dma);
}

static int mv_cesa_ahash_dma_alloc_padding(struct mv_cesa_ahash_dma_req *req,
					   gfp_t flags)
{
	if (req->padding)
		return 0;

	req->padding = dma_pool_alloc(cesa_dev->dma->padding_pool, flags,
				      &req->padding_dma);
	if (!req->padding)
		return -ENOMEM;

	return 0;
}

static void mv_cesa_ahash_dma_free_padding(struct mv_cesa_ahash_dma_req *req)
{
	if (!req->padding)
		return;

	dma_pool_free(cesa_dev->dma->padding_pool, req->padding,
		      req->padding_dma);
	req->padding = NULL;
}

static inline void mv_cesa_ahash_dma_last_cleanup(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	mv_cesa_ahash_dma_free_padding(&creq->req.dma);
}

static inline void mv_cesa_ahash_dma_cleanup(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	dma_unmap_sg(cesa_dev->dev, req->src, creq->src_nents, DMA_TO_DEVICE);
	mv_cesa_ahash_dma_free_cache(&creq->req.dma);
	mv_cesa_dma_cleanup(&creq->base);
}

static inline void mv_cesa_ahash_cleanup(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_ahash_dma_cleanup(req);
}

static void mv_cesa_ahash_last_cleanup(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_ahash_dma_last_cleanup(req);
}

static int mv_cesa_ahash_pad_len(struct mv_cesa_ahash_req *creq)
{
	unsigned int index, padlen;

	index = creq->len & CESA_HASH_BLOCK_SIZE_MSK;
	padlen = (index < 56) ? (56 - index) : (64 + 56 - index);

	return padlen;
}

static int mv_cesa_ahash_pad_req(struct mv_cesa_ahash_req *creq, u8 *buf)
{
	unsigned int index, padlen;

	buf[0] = 0x80;
	/* Pad out to 56 mod 64 */
	index = creq->len & CESA_HASH_BLOCK_SIZE_MSK;
	padlen = mv_cesa_ahash_pad_len(creq);
	memset(buf + 1, 0, padlen - 1);

	if (creq->algo_le) {
		__le64 bits = cpu_to_le64(creq->len << 3);
		memcpy(buf + padlen, &bits, sizeof(bits));
	} else {
		__be64 bits = cpu_to_be64(creq->len << 3);
		memcpy(buf + padlen, &bits, sizeof(bits));
	}

	return padlen + 8;
}

static void mv_cesa_ahash_std_step(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_ahash_std_req *sreq = &creq->req.std;
	struct mv_cesa_engine *engine = creq->base.engine;
	struct mv_cesa_op_ctx *op;
	unsigned int new_cache_ptr = 0;
	u32 frag_mode;
	size_t  len;
	unsigned int digsize;
	int i;

	mv_cesa_adjust_op(engine, &creq->op_tmpl);
	memcpy_toio(engine->sram, &creq->op_tmpl, sizeof(creq->op_tmpl));

	if (!sreq->offset) {
		digsize = crypto_ahash_digestsize(crypto_ahash_reqtfm(req));
		for (i = 0; i < digsize / 4; i++)
			writel_relaxed(creq->state[i], engine->regs + CESA_IVDIG(i));
	}

	if (creq->cache_ptr)
		memcpy_toio(engine->sram + CESA_SA_DATA_SRAM_OFFSET,
			    creq->cache, creq->cache_ptr);

	len = min_t(size_t, req->nbytes + creq->cache_ptr - sreq->offset,
		    CESA_SA_SRAM_PAYLOAD_SIZE);

	if (!creq->last_req) {
		new_cache_ptr = len & CESA_HASH_BLOCK_SIZE_MSK;
		len &= ~CESA_HASH_BLOCK_SIZE_MSK;
	}

	if (len - creq->cache_ptr)
		sreq->offset += sg_pcopy_to_buffer(req->src, creq->src_nents,
						   engine->sram +
						   CESA_SA_DATA_SRAM_OFFSET +
						   creq->cache_ptr,
						   len - creq->cache_ptr,
						   sreq->offset);

	op = &creq->op_tmpl;

	frag_mode = mv_cesa_get_op_cfg(op) & CESA_SA_DESC_CFG_FRAG_MSK;

	if (creq->last_req && sreq->offset == req->nbytes &&
	    creq->len <= CESA_SA_DESC_MAC_SRC_TOTAL_LEN_MAX) {
		if (frag_mode == CESA_SA_DESC_CFG_FIRST_FRAG)
			frag_mode = CESA_SA_DESC_CFG_NOT_FRAG;
		else if (frag_mode == CESA_SA_DESC_CFG_MID_FRAG)
			frag_mode = CESA_SA_DESC_CFG_LAST_FRAG;
	}

	if (frag_mode == CESA_SA_DESC_CFG_NOT_FRAG ||
	    frag_mode == CESA_SA_DESC_CFG_LAST_FRAG) {
		if (len &&
		    creq->len <= CESA_SA_DESC_MAC_SRC_TOTAL_LEN_MAX) {
			mv_cesa_set_mac_op_total_len(op, creq->len);
		} else {
			int trailerlen = mv_cesa_ahash_pad_len(creq) + 8;

			if (len + trailerlen > CESA_SA_SRAM_PAYLOAD_SIZE) {
				len &= CESA_HASH_BLOCK_SIZE_MSK;
				new_cache_ptr = 64 - trailerlen;
				memcpy_fromio(creq->cache,
					      engine->sram +
					      CESA_SA_DATA_SRAM_OFFSET + len,
					      new_cache_ptr);
			} else {
				len += mv_cesa_ahash_pad_req(creq,
						engine->sram + len +
						CESA_SA_DATA_SRAM_OFFSET);
			}

			if (frag_mode == CESA_SA_DESC_CFG_LAST_FRAG)
				frag_mode = CESA_SA_DESC_CFG_MID_FRAG;
			else
				frag_mode = CESA_SA_DESC_CFG_FIRST_FRAG;
		}
	}

	mv_cesa_set_mac_op_frag_len(op, len);
	mv_cesa_update_op_cfg(op, frag_mode, CESA_SA_DESC_CFG_FRAG_MSK);

	/* FIXME: only update enc_len field */
	memcpy_toio(engine->sram, op, sizeof(*op));

	if (frag_mode == CESA_SA_DESC_CFG_FIRST_FRAG)
		mv_cesa_update_op_cfg(op, CESA_SA_DESC_CFG_MID_FRAG,
				      CESA_SA_DESC_CFG_FRAG_MSK);

	creq->cache_ptr = new_cache_ptr;

	mv_cesa_set_int_mask(engine, CESA_SA_INT_ACCEL0_DONE);
	writel_relaxed(CESA_SA_CFG_PARA_DIS, engine->regs + CESA_SA_CFG);
	BUG_ON(readl(engine->regs + CESA_SA_CMD) &
	       CESA_SA_CMD_EN_CESA_SA_ACCL0);
	writel(CESA_SA_CMD_EN_CESA_SA_ACCL0, engine->regs + CESA_SA_CMD);
}

static int mv_cesa_ahash_std_process(struct ahash_request *req, u32 status)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_ahash_std_req *sreq = &creq->req.std;

	if (sreq->offset < (req->nbytes - creq->cache_ptr))
		return -EINPROGRESS;

	return 0;
}

static inline void mv_cesa_ahash_dma_prepare(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_req *basereq = &creq->base;

	mv_cesa_dma_prepare(basereq, basereq->engine);
}

static void mv_cesa_ahash_std_prepare(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_ahash_std_req *sreq = &creq->req.std;

	sreq->offset = 0;
}

static void mv_cesa_ahash_dma_step(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_req *base = &creq->base;

	/* We must explicitly set the digest state. */
	if (base->chain.first->flags & CESA_TDMA_SET_STATE) {
		struct mv_cesa_engine *engine = base->engine;
		int i;

		/* Set the hash state in the IVDIG regs. */
		for (i = 0; i < ARRAY_SIZE(creq->state); i++)
			writel_relaxed(creq->state[i], engine->regs +
				       CESA_IVDIG(i));
	}

	mv_cesa_dma_step(base);
}

static void mv_cesa_ahash_step(struct crypto_async_request *req)
{
	struct ahash_request *ahashreq = ahash_request_cast(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(ahashreq);

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_ahash_dma_step(ahashreq);
	else
		mv_cesa_ahash_std_step(ahashreq);
}

static int mv_cesa_ahash_process(struct crypto_async_request *req, u32 status)
{
	struct ahash_request *ahashreq = ahash_request_cast(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(ahashreq);

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		return mv_cesa_dma_process(&creq->base, status);

	return mv_cesa_ahash_std_process(ahashreq, status);
}

static void mv_cesa_ahash_complete(struct crypto_async_request *req)
{
	struct ahash_request *ahashreq = ahash_request_cast(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(ahashreq);
	struct mv_cesa_engine *engine = creq->base.engine;
	unsigned int digsize;
	int i;

	digsize = crypto_ahash_digestsize(crypto_ahash_reqtfm(ahashreq));

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ &&
	    (creq->base.chain.last->flags & CESA_TDMA_TYPE_MSK) == CESA_TDMA_RESULT) {
		__le32 *data = NULL;

		/*
		 * Result is already in the correct endianess when the SA is
		 * used
		 */
		data = creq->base.chain.last->op->ctx.hash.hash;
		for (i = 0; i < digsize / 4; i++)
			creq->state[i] = cpu_to_le32(data[i]);

		memcpy(ahashreq->result, data, digsize);
	} else {
		for (i = 0; i < digsize / 4; i++)
			creq->state[i] = readl_relaxed(engine->regs +
						       CESA_IVDIG(i));
		if (creq->last_req) {
			/*
			* Hardware's MD5 digest is in little endian format, but
			* SHA in big endian format
			*/
			if (creq->algo_le) {
				__le32 *result = (void *)ahashreq->result;

				for (i = 0; i < digsize / 4; i++)
					result[i] = cpu_to_le32(creq->state[i]);
			} else {
				__be32 *result = (void *)ahashreq->result;

				for (i = 0; i < digsize / 4; i++)
					result[i] = cpu_to_be32(creq->state[i]);
			}
		}
	}

	atomic_sub(ahashreq->nbytes, &engine->load);
}

static void mv_cesa_ahash_prepare(struct crypto_async_request *req,
				  struct mv_cesa_engine *engine)
{
	struct ahash_request *ahashreq = ahash_request_cast(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(ahashreq);

	creq->base.engine = engine;

	if (mv_cesa_req_get_type(&creq->base) == CESA_DMA_REQ)
		mv_cesa_ahash_dma_prepare(ahashreq);
	else
		mv_cesa_ahash_std_prepare(ahashreq);
}

static void mv_cesa_ahash_req_cleanup(struct crypto_async_request *req)
{
	struct ahash_request *ahashreq = ahash_request_cast(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(ahashreq);

	if (creq->last_req)
		mv_cesa_ahash_last_cleanup(ahashreq);

	mv_cesa_ahash_cleanup(ahashreq);

	if (creq->cache_ptr)
		sg_pcopy_to_buffer(ahashreq->src, creq->src_nents,
				   creq->cache,
				   creq->cache_ptr,
				   ahashreq->nbytes - creq->cache_ptr);
}

static const struct mv_cesa_req_ops mv_cesa_ahash_req_ops = {
	.step = mv_cesa_ahash_step,
	.process = mv_cesa_ahash_process,
	.cleanup = mv_cesa_ahash_req_cleanup,
	.complete = mv_cesa_ahash_complete,
};

static void mv_cesa_ahash_init(struct ahash_request *req,
			      struct mv_cesa_op_ctx *tmpl, bool algo_le)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	memset(creq, 0, sizeof(*creq));
	mv_cesa_update_op_cfg(tmpl,
			      CESA_SA_DESC_CFG_OP_MAC_ONLY |
			      CESA_SA_DESC_CFG_FIRST_FRAG,
			      CESA_SA_DESC_CFG_OP_MSK |
			      CESA_SA_DESC_CFG_FRAG_MSK);
	mv_cesa_set_mac_op_total_len(tmpl, 0);
	mv_cesa_set_mac_op_frag_len(tmpl, 0);
	creq->op_tmpl = *tmpl;
	creq->len = 0;
	creq->algo_le = algo_le;
}

static inline int mv_cesa_ahash_cra_init(struct crypto_tfm *tfm)
{
	struct mv_cesa_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->base.ops = &mv_cesa_ahash_req_ops;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct mv_cesa_ahash_req));
	return 0;
}

static bool mv_cesa_ahash_cache_req(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	bool cached = false;

	if (creq->cache_ptr + req->nbytes < CESA_MAX_HASH_BLOCK_SIZE && !creq->last_req) {
		cached = true;

		if (!req->nbytes)
			return cached;

		sg_pcopy_to_buffer(req->src, creq->src_nents,
				   creq->cache + creq->cache_ptr,
				   req->nbytes, 0);

		creq->cache_ptr += req->nbytes;
	}

	return cached;
}

static struct mv_cesa_op_ctx *
mv_cesa_dma_add_frag(struct mv_cesa_tdma_chain *chain,
		     struct mv_cesa_op_ctx *tmpl, unsigned int frag_len,
		     gfp_t flags)
{
	struct mv_cesa_op_ctx *op;
	int ret;

	op = mv_cesa_dma_add_op(chain, tmpl, false, flags);
	if (IS_ERR(op))
		return op;

	/* Set the operation block fragment length. */
	mv_cesa_set_mac_op_frag_len(op, frag_len);

	/* Append dummy desc to launch operation */
	ret = mv_cesa_dma_add_dummy_launch(chain, flags);
	if (ret)
		return ERR_PTR(ret);

	if (mv_cesa_mac_op_is_first_frag(tmpl))
		mv_cesa_update_op_cfg(tmpl,
				      CESA_SA_DESC_CFG_MID_FRAG,
				      CESA_SA_DESC_CFG_FRAG_MSK);

	return op;
}

static int
mv_cesa_ahash_dma_add_cache(struct mv_cesa_tdma_chain *chain,
			    struct mv_cesa_ahash_req *creq,
			    gfp_t flags)
{
	struct mv_cesa_ahash_dma_req *ahashdreq = &creq->req.dma;
	int ret;

	if (!creq->cache_ptr)
		return 0;

	ret = mv_cesa_ahash_dma_alloc_cache(ahashdreq, flags);
	if (ret)
		return ret;

	memcpy(ahashdreq->cache, creq->cache, creq->cache_ptr);

	return mv_cesa_dma_add_data_transfer(chain,
					     CESA_SA_DATA_SRAM_OFFSET,
					     ahashdreq->cache_dma,
					     creq->cache_ptr,
					     CESA_TDMA_DST_IN_SRAM,
					     flags);
}

static struct mv_cesa_op_ctx *
mv_cesa_ahash_dma_last_req(struct mv_cesa_tdma_chain *chain,
			   struct mv_cesa_ahash_dma_iter *dma_iter,
			   struct mv_cesa_ahash_req *creq,
			   unsigned int frag_len, gfp_t flags)
{
	struct mv_cesa_ahash_dma_req *ahashdreq = &creq->req.dma;
	unsigned int len, trailerlen, padoff = 0;
	struct mv_cesa_op_ctx *op;
	int ret;

	/*
	 * If the transfer is smaller than our maximum length, and we have
	 * some data outstanding, we can ask the engine to finish the hash.
	 */
	if (creq->len <= CESA_SA_DESC_MAC_SRC_TOTAL_LEN_MAX && frag_len) {
		op = mv_cesa_dma_add_frag(chain, &creq->op_tmpl, frag_len,
					  flags);
		if (IS_ERR(op))
			return op;

		mv_cesa_set_mac_op_total_len(op, creq->len);
		mv_cesa_update_op_cfg(op, mv_cesa_mac_op_is_first_frag(op) ?
						CESA_SA_DESC_CFG_NOT_FRAG :
						CESA_SA_DESC_CFG_LAST_FRAG,
				      CESA_SA_DESC_CFG_FRAG_MSK);

		ret = mv_cesa_dma_add_result_op(chain,
						CESA_SA_CFG_SRAM_OFFSET,
						CESA_SA_DATA_SRAM_OFFSET,
						CESA_TDMA_SRC_IN_SRAM, flags);
		if (ret)
			return ERR_PTR(-ENOMEM);
		return op;
	}

	/*
	 * The request is longer than the engine can handle, or we have
	 * no data outstanding. Manually generate the padding, adding it
	 * as a "mid" fragment.
	 */
	ret = mv_cesa_ahash_dma_alloc_padding(ahashdreq, flags);
	if (ret)
		return ERR_PTR(ret);

	trailerlen = mv_cesa_ahash_pad_req(creq, ahashdreq->padding);

	len = min(CESA_SA_SRAM_PAYLOAD_SIZE - frag_len, trailerlen);
	if (len) {
		ret = mv_cesa_dma_add_data_transfer(chain,
						CESA_SA_DATA_SRAM_OFFSET +
						frag_len,
						ahashdreq->padding_dma,
						len, CESA_TDMA_DST_IN_SRAM,
						flags);
		if (ret)
			return ERR_PTR(ret);

		op = mv_cesa_dma_add_frag(chain, &creq->op_tmpl, frag_len + len,
					  flags);
		if (IS_ERR(op))
			return op;

		if (len == trailerlen)
			return op;

		padoff += len;
	}

	ret = mv_cesa_dma_add_data_transfer(chain,
					    CESA_SA_DATA_SRAM_OFFSET,
					    ahashdreq->padding_dma +
					    padoff,
					    trailerlen - padoff,
					    CESA_TDMA_DST_IN_SRAM,
					    flags);
	if (ret)
		return ERR_PTR(ret);

	return mv_cesa_dma_add_frag(chain, &creq->op_tmpl, trailerlen - padoff,
				    flags);
}

static int mv_cesa_ahash_dma_req_init(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		      GFP_KERNEL : GFP_ATOMIC;
	struct mv_cesa_req *basereq = &creq->base;
	struct mv_cesa_ahash_dma_iter iter;
	struct mv_cesa_op_ctx *op = NULL;
	unsigned int frag_len;
	bool set_state = false;
	int ret;
	u32 type;

	basereq->chain.first = NULL;
	basereq->chain.last = NULL;

	if (!mv_cesa_mac_op_is_first_frag(&creq->op_tmpl))
		set_state = true;

	if (creq->src_nents) {
		ret = dma_map_sg(cesa_dev->dev, req->src, creq->src_nents,
				 DMA_TO_DEVICE);
		if (!ret) {
			ret = -ENOMEM;
			goto err;
		}
	}

	mv_cesa_tdma_desc_iter_init(&basereq->chain);
	mv_cesa_ahash_req_iter_init(&iter, req);

	/*
	 * Add the cache (left-over data from a previous block) first.
	 * This will never overflow the SRAM size.
	 */
	ret = mv_cesa_ahash_dma_add_cache(&basereq->chain, creq, flags);
	if (ret)
		goto err_free_tdma;

	if (iter.src.sg) {
		/*
		 * Add all the new data, inserting an operation block and
		 * launch command between each full SRAM block-worth of
		 * data. We intentionally do not add the final op block.
		 */
		while (true) {
			ret = mv_cesa_dma_add_op_transfers(&basereq->chain,
							   &iter.base,
							   &iter.src, flags);
			if (ret)
				goto err_free_tdma;

			frag_len = iter.base.op_len;

			if (!mv_cesa_ahash_req_iter_next_op(&iter))
				break;

			op = mv_cesa_dma_add_frag(&basereq->chain, &creq->op_tmpl,
						  frag_len, flags);
			if (IS_ERR(op)) {
				ret = PTR_ERR(op);
				goto err_free_tdma;
			}
		}
	} else {
		/* Account for the data that was in the cache. */
		frag_len = iter.base.op_len;
	}

	/*
	 * At this point, frag_len indicates whether we have any data
	 * outstanding which needs an operation.  Queue up the final
	 * operation, which depends whether this is the final request.
	 */
	if (creq->last_req)
		op = mv_cesa_ahash_dma_last_req(&basereq->chain, &iter, creq,
						frag_len, flags);
	else if (frag_len)
		op = mv_cesa_dma_add_frag(&basereq->chain, &creq->op_tmpl,
					  frag_len, flags);

	if (IS_ERR(op)) {
		ret = PTR_ERR(op);
		goto err_free_tdma;
	}

	/*
	 * If results are copied via DMA, this means that this
	 * request can be directly processed by the engine,
	 * without partial updates. So we can chain it at the
	 * DMA level with other requests.
	 */
	type = basereq->chain.last->flags & CESA_TDMA_TYPE_MSK;

	if (op && type != CESA_TDMA_RESULT) {
		/* Add dummy desc to wait for crypto operation end */
		ret = mv_cesa_dma_add_dummy_end(&basereq->chain, flags);
		if (ret)
			goto err_free_tdma;
	}

	if (!creq->last_req)
		creq->cache_ptr = req->nbytes + creq->cache_ptr -
				  iter.base.len;
	else
		creq->cache_ptr = 0;

	basereq->chain.last->flags |= CESA_TDMA_END_OF_REQ;

	if (type != CESA_TDMA_RESULT)
		basereq->chain.last->flags |= CESA_TDMA_BREAK_CHAIN;

	if (set_state) {
		/*
		 * Put the CESA_TDMA_SET_STATE flag on the first tdma desc to
		 * let the step logic know that the IVDIG registers should be
		 * explicitly set before launching a TDMA chain.
		 */
		basereq->chain.first->flags |= CESA_TDMA_SET_STATE;
	}

	return 0;

err_free_tdma:
	mv_cesa_dma_cleanup(basereq);
	dma_unmap_sg(cesa_dev->dev, req->src, creq->src_nents, DMA_TO_DEVICE);

err:
	mv_cesa_ahash_last_cleanup(req);

	return ret;
}

static int mv_cesa_ahash_req_init(struct ahash_request *req, bool *cached)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	creq->src_nents = sg_nents_for_len(req->src, req->nbytes);
	if (creq->src_nents < 0) {
		dev_err(cesa_dev->dev, "Invalid number of src SG");
		return creq->src_nents;
	}

	*cached = mv_cesa_ahash_cache_req(req);

	if (*cached)
		return 0;

	if (cesa_dev->caps->has_tdma)
		return mv_cesa_ahash_dma_req_init(req);
	else
		return 0;
}

static int mv_cesa_ahash_queue_req(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_engine *engine;
	bool cached = false;
	int ret;

	ret = mv_cesa_ahash_req_init(req, &cached);
	if (ret)
		return ret;

	if (cached)
		return 0;

	engine = mv_cesa_select_engine(req->nbytes);
	mv_cesa_ahash_prepare(&req->base, engine);

	ret = mv_cesa_queue_req(&req->base, &creq->base);

	if (mv_cesa_req_needs_cleanup(&req->base, ret))
		mv_cesa_ahash_cleanup(req);

	return ret;
}

static int mv_cesa_ahash_update(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);

	creq->len += req->nbytes;

	return mv_cesa_ahash_queue_req(req);
}

static int mv_cesa_ahash_final(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_op_ctx *tmpl = &creq->op_tmpl;

	mv_cesa_set_mac_op_total_len(tmpl, creq->len);
	creq->last_req = true;
	req->nbytes = 0;

	return mv_cesa_ahash_queue_req(req);
}

static int mv_cesa_ahash_finup(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_op_ctx *tmpl = &creq->op_tmpl;

	creq->len += req->nbytes;
	mv_cesa_set_mac_op_total_len(tmpl, creq->len);
	creq->last_req = true;

	return mv_cesa_ahash_queue_req(req);
}

static int mv_cesa_ahash_export(struct ahash_request *req, void *hash,
				u64 *len, void *cache)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	unsigned int digsize = crypto_ahash_digestsize(ahash);
	unsigned int blocksize;

	blocksize = crypto_ahash_blocksize(ahash);

	*len = creq->len;
	memcpy(hash, creq->state, digsize);
	memset(cache, 0, blocksize);
	memcpy(cache, creq->cache, creq->cache_ptr);

	return 0;
}

static int mv_cesa_ahash_import(struct ahash_request *req, const void *hash,
				u64 len, const void *cache)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	unsigned int digsize = crypto_ahash_digestsize(ahash);
	unsigned int blocksize;
	unsigned int cache_ptr;
	int ret;

	ret = crypto_ahash_init(req);
	if (ret)
		return ret;

	blocksize = crypto_ahash_blocksize(ahash);
	if (len >= blocksize)
		mv_cesa_update_op_cfg(&creq->op_tmpl,
				      CESA_SA_DESC_CFG_MID_FRAG,
				      CESA_SA_DESC_CFG_FRAG_MSK);

	creq->len = len;
	memcpy(creq->state, hash, digsize);
	creq->cache_ptr = 0;

	cache_ptr = do_div(len, blocksize);
	if (!cache_ptr)
		return 0;

	memcpy(creq->cache, cache, cache_ptr);
	creq->cache_ptr = cache_ptr;

	return 0;
}

static int mv_cesa_md5_init(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_op_ctx tmpl = { };

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_MACM_MD5);

	mv_cesa_ahash_init(req, &tmpl, true);

	creq->state[0] = MD5_H0;
	creq->state[1] = MD5_H1;
	creq->state[2] = MD5_H2;
	creq->state[3] = MD5_H3;

	return 0;
}

static int mv_cesa_md5_export(struct ahash_request *req, void *out)
{
	struct md5_state *out_state = out;

	return mv_cesa_ahash_export(req, out_state->hash,
				    &out_state->byte_count, out_state->block);
}

static int mv_cesa_md5_import(struct ahash_request *req, const void *in)
{
	const struct md5_state *in_state = in;

	return mv_cesa_ahash_import(req, in_state->hash, in_state->byte_count,
				    in_state->block);
}

static int mv_cesa_md5_digest(struct ahash_request *req)
{
	int ret;

	ret = mv_cesa_md5_init(req);
	if (ret)
		return ret;

	return mv_cesa_ahash_finup(req);
}

struct ahash_alg mv_md5_alg = {
	.init = mv_cesa_md5_init,
	.update = mv_cesa_ahash_update,
	.final = mv_cesa_ahash_final,
	.finup = mv_cesa_ahash_finup,
	.digest = mv_cesa_md5_digest,
	.export = mv_cesa_md5_export,
	.import = mv_cesa_md5_import,
	.halg = {
		.digestsize = MD5_DIGEST_SIZE,
		.statesize = sizeof(struct md5_state),
		.base = {
			.cra_name = "md5",
			.cra_driver_name = "mv-md5",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct mv_cesa_hash_ctx),
			.cra_init = mv_cesa_ahash_cra_init,
			.cra_module = THIS_MODULE,
		 }
	}
};

static int mv_cesa_sha1_init(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_op_ctx tmpl = { };

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_MACM_SHA1);

	mv_cesa_ahash_init(req, &tmpl, false);

	creq->state[0] = SHA1_H0;
	creq->state[1] = SHA1_H1;
	creq->state[2] = SHA1_H2;
	creq->state[3] = SHA1_H3;
	creq->state[4] = SHA1_H4;

	return 0;
}

static int mv_cesa_sha1_export(struct ahash_request *req, void *out)
{
	struct sha1_state *out_state = out;

	return mv_cesa_ahash_export(req, out_state->state, &out_state->count,
				    out_state->buffer);
}

static int mv_cesa_sha1_import(struct ahash_request *req, const void *in)
{
	const struct sha1_state *in_state = in;

	return mv_cesa_ahash_import(req, in_state->state, in_state->count,
				    in_state->buffer);
}

static int mv_cesa_sha1_digest(struct ahash_request *req)
{
	int ret;

	ret = mv_cesa_sha1_init(req);
	if (ret)
		return ret;

	return mv_cesa_ahash_finup(req);
}

struct ahash_alg mv_sha1_alg = {
	.init = mv_cesa_sha1_init,
	.update = mv_cesa_ahash_update,
	.final = mv_cesa_ahash_final,
	.finup = mv_cesa_ahash_finup,
	.digest = mv_cesa_sha1_digest,
	.export = mv_cesa_sha1_export,
	.import = mv_cesa_sha1_import,
	.halg = {
		.digestsize = SHA1_DIGEST_SIZE,
		.statesize = sizeof(struct sha1_state),
		.base = {
			.cra_name = "sha1",
			.cra_driver_name = "mv-sha1",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = SHA1_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct mv_cesa_hash_ctx),
			.cra_init = mv_cesa_ahash_cra_init,
			.cra_module = THIS_MODULE,
		 }
	}
};

static int mv_cesa_sha256_init(struct ahash_request *req)
{
	struct mv_cesa_ahash_req *creq = ahash_request_ctx(req);
	struct mv_cesa_op_ctx tmpl = { };

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_MACM_SHA256);

	mv_cesa_ahash_init(req, &tmpl, false);

	creq->state[0] = SHA256_H0;
	creq->state[1] = SHA256_H1;
	creq->state[2] = SHA256_H2;
	creq->state[3] = SHA256_H3;
	creq->state[4] = SHA256_H4;
	creq->state[5] = SHA256_H5;
	creq->state[6] = SHA256_H6;
	creq->state[7] = SHA256_H7;

	return 0;
}

static int mv_cesa_sha256_digest(struct ahash_request *req)
{
	int ret;

	ret = mv_cesa_sha256_init(req);
	if (ret)
		return ret;

	return mv_cesa_ahash_finup(req);
}

static int mv_cesa_sha256_export(struct ahash_request *req, void *out)
{
	struct sha256_state *out_state = out;

	return mv_cesa_ahash_export(req, out_state->state, &out_state->count,
				    out_state->buf);
}

static int mv_cesa_sha256_import(struct ahash_request *req, const void *in)
{
	const struct sha256_state *in_state = in;

	return mv_cesa_ahash_import(req, in_state->state, in_state->count,
				    in_state->buf);
}

struct ahash_alg mv_sha256_alg = {
	.init = mv_cesa_sha256_init,
	.update = mv_cesa_ahash_update,
	.final = mv_cesa_ahash_final,
	.finup = mv_cesa_ahash_finup,
	.digest = mv_cesa_sha256_digest,
	.export = mv_cesa_sha256_export,
	.import = mv_cesa_sha256_import,
	.halg = {
		.digestsize = SHA256_DIGEST_SIZE,
		.statesize = sizeof(struct sha256_state),
		.base = {
			.cra_name = "sha256",
			.cra_driver_name = "mv-sha256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct mv_cesa_hash_ctx),
			.cra_init = mv_cesa_ahash_cra_init,
			.cra_module = THIS_MODULE,
		 }
	}
};

struct mv_cesa_ahash_result {
	struct completion completion;
	int error;
};

static void mv_cesa_hmac_ahash_complete(struct crypto_async_request *req,
					int error)
{
	struct mv_cesa_ahash_result *result = req->data;

	if (error == -EINPROGRESS)
		return;

	result->error = error;
	complete(&result->completion);
}

static int mv_cesa_ahmac_iv_state_init(struct ahash_request *req, u8 *pad,
				       void *state, unsigned int blocksize)
{
	struct mv_cesa_ahash_result result;
	struct scatterlist sg;
	int ret;

	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   mv_cesa_hmac_ahash_complete, &result);
	sg_init_one(&sg, pad, blocksize);
	ahash_request_set_crypt(req, &sg, pad, blocksize);
	init_completion(&result.completion);

	ret = crypto_ahash_init(req);
	if (ret)
		return ret;

	ret = crypto_ahash_update(req);
	if (ret && ret != -EINPROGRESS)
		return ret;

	wait_for_completion_interruptible(&result.completion);
	if (result.error)
		return result.error;

	ret = crypto_ahash_export(req, state);
	if (ret)
		return ret;

	return 0;
}

static int mv_cesa_ahmac_pad_init(struct ahash_request *req,
				  const u8 *key, unsigned int keylen,
				  u8 *ipad, u8 *opad,
				  unsigned int blocksize)
{
	struct mv_cesa_ahash_result result;
	struct scatterlist sg;
	int ret;
	int i;

	if (keylen <= blocksize) {
		memcpy(ipad, key, keylen);
	} else {
		u8 *keydup = kmemdup(key, keylen, GFP_KERNEL);

		if (!keydup)
			return -ENOMEM;

		ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					   mv_cesa_hmac_ahash_complete,
					   &result);
		sg_init_one(&sg, keydup, keylen);
		ahash_request_set_crypt(req, &sg, ipad, keylen);
		init_completion(&result.completion);

		ret = crypto_ahash_digest(req);
		if (ret == -EINPROGRESS) {
			wait_for_completion_interruptible(&result.completion);
			ret = result.error;
		}

		/* Set the memory region to 0 to avoid any leak. */
		memset(keydup, 0, keylen);
		kfree(keydup);

		if (ret)
			return ret;

		keylen = crypto_ahash_digestsize(crypto_ahash_reqtfm(req));
	}

	memset(ipad + keylen, 0, blocksize - keylen);
	memcpy(opad, ipad, blocksize);

	for (i = 0; i < blocksize; i++) {
		ipad[i] ^= HMAC_IPAD_VALUE;
		opad[i] ^= HMAC_OPAD_VALUE;
	}

	return 0;
}

static int mv_cesa_ahmac_setkey(const char *hash_alg_name,
				const u8 *key, unsigned int keylen,
				void *istate, void *ostate)
{
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	unsigned int blocksize;
	u8 *ipad = NULL;
	u8 *opad;
	int ret;

	tfm = crypto_alloc_ahash(hash_alg_name, CRYPTO_ALG_TYPE_AHASH,
				 CRYPTO_ALG_TYPE_AHASH_MASK);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto free_ahash;
	}

	crypto_ahash_clear_flags(tfm, ~0);

	blocksize = crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	ipad = kzalloc(2 * blocksize, GFP_KERNEL);
	if (!ipad) {
		ret = -ENOMEM;
		goto free_req;
	}

	opad = ipad + blocksize;

	ret = mv_cesa_ahmac_pad_init(req, key, keylen, ipad, opad, blocksize);
	if (ret)
		goto free_ipad;

	ret = mv_cesa_ahmac_iv_state_init(req, ipad, istate, blocksize);
	if (ret)
		goto free_ipad;

	ret = mv_cesa_ahmac_iv_state_init(req, opad, ostate, blocksize);

free_ipad:
	kfree(ipad);
free_req:
	ahash_request_free(req);
free_ahash:
	crypto_free_ahash(tfm);

	return ret;
}

static int mv_cesa_ahmac_cra_init(struct crypto_tfm *tfm)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->base.ops = &mv_cesa_ahash_req_ops;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct mv_cesa_ahash_req));
	return 0;
}

static int mv_cesa_ahmac_md5_init(struct ahash_request *req)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_cesa_op_ctx tmpl = { };

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_MACM_HMAC_MD5);
	memcpy(tmpl.ctx.hash.iv, ctx->iv, sizeof(ctx->iv));

	mv_cesa_ahash_init(req, &tmpl, true);

	return 0;
}

static int mv_cesa_ahmac_md5_setkey(struct crypto_ahash *tfm, const u8 *key,
				    unsigned int keylen)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct md5_state istate, ostate;
	int ret, i;

	ret = mv_cesa_ahmac_setkey("mv-md5", key, keylen, &istate, &ostate);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(istate.hash); i++)
		ctx->iv[i] = be32_to_cpu(istate.hash[i]);

	for (i = 0; i < ARRAY_SIZE(ostate.hash); i++)
		ctx->iv[i + 8] = be32_to_cpu(ostate.hash[i]);

	return 0;
}

static int mv_cesa_ahmac_md5_digest(struct ahash_request *req)
{
	int ret;

	ret = mv_cesa_ahmac_md5_init(req);
	if (ret)
		return ret;

	return mv_cesa_ahash_finup(req);
}

struct ahash_alg mv_ahmac_md5_alg = {
	.init = mv_cesa_ahmac_md5_init,
	.update = mv_cesa_ahash_update,
	.final = mv_cesa_ahash_final,
	.finup = mv_cesa_ahash_finup,
	.digest = mv_cesa_ahmac_md5_digest,
	.setkey = mv_cesa_ahmac_md5_setkey,
	.export = mv_cesa_md5_export,
	.import = mv_cesa_md5_import,
	.halg = {
		.digestsize = MD5_DIGEST_SIZE,
		.statesize = sizeof(struct md5_state),
		.base = {
			.cra_name = "hmac(md5)",
			.cra_driver_name = "mv-hmac-md5",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct mv_cesa_hmac_ctx),
			.cra_init = mv_cesa_ahmac_cra_init,
			.cra_module = THIS_MODULE,
		 }
	}
};

static int mv_cesa_ahmac_sha1_init(struct ahash_request *req)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_cesa_op_ctx tmpl = { };

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_MACM_HMAC_SHA1);
	memcpy(tmpl.ctx.hash.iv, ctx->iv, sizeof(ctx->iv));

	mv_cesa_ahash_init(req, &tmpl, false);

	return 0;
}

static int mv_cesa_ahmac_sha1_setkey(struct crypto_ahash *tfm, const u8 *key,
				     unsigned int keylen)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct sha1_state istate, ostate;
	int ret, i;

	ret = mv_cesa_ahmac_setkey("mv-sha1", key, keylen, &istate, &ostate);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(istate.state); i++)
		ctx->iv[i] = be32_to_cpu(istate.state[i]);

	for (i = 0; i < ARRAY_SIZE(ostate.state); i++)
		ctx->iv[i + 8] = be32_to_cpu(ostate.state[i]);

	return 0;
}

static int mv_cesa_ahmac_sha1_digest(struct ahash_request *req)
{
	int ret;

	ret = mv_cesa_ahmac_sha1_init(req);
	if (ret)
		return ret;

	return mv_cesa_ahash_finup(req);
}

struct ahash_alg mv_ahmac_sha1_alg = {
	.init = mv_cesa_ahmac_sha1_init,
	.update = mv_cesa_ahash_update,
	.final = mv_cesa_ahash_final,
	.finup = mv_cesa_ahash_finup,
	.digest = mv_cesa_ahmac_sha1_digest,
	.setkey = mv_cesa_ahmac_sha1_setkey,
	.export = mv_cesa_sha1_export,
	.import = mv_cesa_sha1_import,
	.halg = {
		.digestsize = SHA1_DIGEST_SIZE,
		.statesize = sizeof(struct sha1_state),
		.base = {
			.cra_name = "hmac(sha1)",
			.cra_driver_name = "mv-hmac-sha1",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = SHA1_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct mv_cesa_hmac_ctx),
			.cra_init = mv_cesa_ahmac_cra_init,
			.cra_module = THIS_MODULE,
		 }
	}
};

static int mv_cesa_ahmac_sha256_setkey(struct crypto_ahash *tfm, const u8 *key,
				       unsigned int keylen)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(crypto_ahash_tfm(tfm));
	struct sha256_state istate, ostate;
	int ret, i;

	ret = mv_cesa_ahmac_setkey("mv-sha256", key, keylen, &istate, &ostate);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(istate.state); i++)
		ctx->iv[i] = be32_to_cpu(istate.state[i]);

	for (i = 0; i < ARRAY_SIZE(ostate.state); i++)
		ctx->iv[i + 8] = be32_to_cpu(ostate.state[i]);

	return 0;
}

static int mv_cesa_ahmac_sha256_init(struct ahash_request *req)
{
	struct mv_cesa_hmac_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_cesa_op_ctx tmpl = { };

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_MACM_HMAC_SHA256);
	memcpy(tmpl.ctx.hash.iv, ctx->iv, sizeof(ctx->iv));

	mv_cesa_ahash_init(req, &tmpl, false);

	return 0;
}

static int mv_cesa_ahmac_sha256_digest(struct ahash_request *req)
{
	int ret;

	ret = mv_cesa_ahmac_sha256_init(req);
	if (ret)
		return ret;

	return mv_cesa_ahash_finup(req);
}

struct ahash_alg mv_ahmac_sha256_alg = {
	.init = mv_cesa_ahmac_sha256_init,
	.update = mv_cesa_ahash_update,
	.final = mv_cesa_ahash_final,
	.finup = mv_cesa_ahash_finup,
	.digest = mv_cesa_ahmac_sha256_digest,
	.setkey = mv_cesa_ahmac_sha256_setkey,
	.export = mv_cesa_sha256_export,
	.import = mv_cesa_sha256_import,
	.halg = {
		.digestsize = SHA256_DIGEST_SIZE,
		.statesize = sizeof(struct sha256_state),
		.base = {
			.cra_name = "hmac(sha256)",
			.cra_driver_name = "mv-hmac-sha256",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_ctxsize = sizeof(struct mv_cesa_hmac_ctx),
			.cra_init = mv_cesa_ahmac_cra_init,
			.cra_module = THIS_MODULE,
		 }
	}
};
