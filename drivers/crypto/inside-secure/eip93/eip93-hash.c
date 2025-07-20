// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024
 *
 * Christian Marangi <ansuelsmth@gmail.com
 */

#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/md5.h>
#include <crypto/hmac.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include "eip93-cipher.h"
#include "eip93-hash.h"
#include "eip93-main.h"
#include "eip93-common.h"
#include "eip93-regs.h"

static void eip93_hash_free_data_blocks(struct ahash_request *req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct eip93_device *eip93 = ctx->eip93;
	struct mkt_hash_block *block, *tmp;

	list_for_each_entry_safe(block, tmp, &rctx->blocks, list) {
		dma_unmap_single(eip93->dev, block->data_dma,
				 SHA256_BLOCK_SIZE, DMA_TO_DEVICE);
		kfree(block);
	}
	if (!list_empty(&rctx->blocks))
		INIT_LIST_HEAD(&rctx->blocks);

	if (rctx->finalize)
		dma_unmap_single(eip93->dev, rctx->data_dma,
				 rctx->data_used,
				 DMA_TO_DEVICE);
}

static void eip93_hash_free_sa_record(struct ahash_request *req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct eip93_device *eip93 = ctx->eip93;

	if (IS_HMAC(ctx->flags))
		dma_unmap_single(eip93->dev, rctx->sa_record_hmac_base,
				 sizeof(rctx->sa_record_hmac), DMA_TO_DEVICE);

	dma_unmap_single(eip93->dev, rctx->sa_record_base,
			 sizeof(rctx->sa_record), DMA_TO_DEVICE);
}

void eip93_hash_handle_result(struct crypto_async_request *async, int err)
{
	struct ahash_request *req = ahash_request_cast(async);
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	int digestsize = crypto_ahash_digestsize(ahash);
	struct sa_state *sa_state = &rctx->sa_state;
	struct eip93_device *eip93 = ctx->eip93;
	int i;

	dma_unmap_single(eip93->dev, rctx->sa_state_base,
			 sizeof(*sa_state), DMA_FROM_DEVICE);

	/*
	 * With partial_hash assume SHA256_DIGEST_SIZE buffer is passed.
	 * This is to handle SHA224 that have a 32 byte intermediate digest.
	 */
	if (rctx->partial_hash)
		digestsize = SHA256_DIGEST_SIZE;

	if (rctx->finalize || rctx->partial_hash) {
		/* bytes needs to be swapped for req->result */
		if (!IS_HASH_MD5(ctx->flags)) {
			for (i = 0; i < digestsize / sizeof(u32); i++) {
				u32 *digest = (u32 *)sa_state->state_i_digest;

				digest[i] = be32_to_cpu((__be32 __force)digest[i]);
			}
		}

		memcpy(req->result, sa_state->state_i_digest, digestsize);
	}

	eip93_hash_free_sa_record(req);
	eip93_hash_free_data_blocks(req);

	ahash_request_complete(req, err);
}

static void eip93_hash_init_sa_state_digest(u32 hash, u8 *digest)
{
	static const u32 sha256_init[] = {
		SHA256_H0, SHA256_H1, SHA256_H2, SHA256_H3,
		SHA256_H4, SHA256_H5, SHA256_H6, SHA256_H7
	};
	static const u32 sha224_init[] = {
		SHA224_H0, SHA224_H1, SHA224_H2, SHA224_H3,
		SHA224_H4, SHA224_H5, SHA224_H6, SHA224_H7
	};
	static const u32 sha1_init[] = {
		SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4
	};
	static const u32 md5_init[] = {
		MD5_H0, MD5_H1, MD5_H2, MD5_H3
	};

	/* Init HASH constant */
	switch (hash) {
	case EIP93_HASH_SHA256:
		memcpy(digest, sha256_init, sizeof(sha256_init));
		return;
	case EIP93_HASH_SHA224:
		memcpy(digest, sha224_init, sizeof(sha224_init));
		return;
	case EIP93_HASH_SHA1:
		memcpy(digest, sha1_init, sizeof(sha1_init));
		return;
	case EIP93_HASH_MD5:
		memcpy(digest, md5_init, sizeof(md5_init));
		return;
	default: /* Impossible */
		return;
	}
}

static void eip93_hash_export_sa_state(struct ahash_request *req,
				       struct eip93_hash_export_state *state)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct sa_state *sa_state = &rctx->sa_state;

	/*
	 * EIP93 have special handling for state_byte_cnt in sa_state.
	 * Even if a zero packet is passed (and a BADMSG is returned),
	 * state_byte_cnt is incremented to the digest handled (with the hash
	 * primitive). This is problematic with export/import as EIP93
	 * expect 0 state_byte_cnt for the very first iteration.
	 */
	if (!rctx->len)
		memset(state->state_len, 0, sizeof(u32) * 2);
	else
		memcpy(state->state_len, sa_state->state_byte_cnt,
		       sizeof(u32) * 2);
	memcpy(state->state_hash, sa_state->state_i_digest,
	       SHA256_DIGEST_SIZE);
	state->len = rctx->len;
	state->data_used = rctx->data_used;
}

static void __eip93_hash_init(struct ahash_request *req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct sa_record *sa_record = &rctx->sa_record;
	int digestsize;

	digestsize = crypto_ahash_digestsize(ahash);

	eip93_set_sa_record(sa_record, 0, ctx->flags);
	sa_record->sa_cmd0_word |= EIP93_SA_CMD_HASH_FROM_STATE;
	sa_record->sa_cmd0_word |= EIP93_SA_CMD_SAVE_HASH;
	sa_record->sa_cmd0_word &= ~EIP93_SA_CMD_OPCODE;
	sa_record->sa_cmd0_word |= FIELD_PREP(EIP93_SA_CMD_OPCODE,
					      EIP93_SA_CMD_OPCODE_BASIC_OUT_HASH);
	sa_record->sa_cmd0_word &= ~EIP93_SA_CMD_DIGEST_LENGTH;
	sa_record->sa_cmd0_word |= FIELD_PREP(EIP93_SA_CMD_DIGEST_LENGTH,
					      digestsize / sizeof(u32));

	/*
	 * HMAC special handling
	 * Enabling CMD_HMAC force the inner hash to be always finalized.
	 * This cause problems on handling message > 64 byte as we
	 * need to produce intermediate inner hash on sending intermediate
	 * 64 bytes blocks.
	 *
	 * To handle this, enable CMD_HMAC only on the last block.
	 * We make a duplicate of sa_record and on the last descriptor,
	 * we pass a dedicated sa_record with CMD_HMAC enabled to make
	 * EIP93 apply the outer hash.
	 */
	if (IS_HMAC(ctx->flags)) {
		struct sa_record *sa_record_hmac = &rctx->sa_record_hmac;

		memcpy(sa_record_hmac, sa_record, sizeof(*sa_record));
		/* Copy pre-hashed opad for HMAC */
		memcpy(sa_record_hmac->sa_o_digest, ctx->opad, SHA256_DIGEST_SIZE);

		/* Disable HMAC for hash normal sa_record */
		sa_record->sa_cmd1_word &= ~EIP93_SA_CMD_HMAC;
	}

	rctx->len = 0;
	rctx->data_used = 0;
	rctx->partial_hash = false;
	rctx->finalize = false;
	INIT_LIST_HEAD(&rctx->blocks);
}

static int eip93_send_hash_req(struct crypto_async_request *async, u8 *data,
			       dma_addr_t *data_dma, u32 len, bool last)
{
	struct ahash_request *req = ahash_request_cast(async);
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct eip93_device *eip93 = ctx->eip93;
	struct eip93_descriptor cdesc = { };
	dma_addr_t src_addr;
	int ret;

	/* Map block data to DMA */
	src_addr = dma_map_single(eip93->dev, data, len, DMA_TO_DEVICE);
	ret = dma_mapping_error(eip93->dev, src_addr);
	if (ret)
		return ret;

	cdesc.pe_ctrl_stat_word = FIELD_PREP(EIP93_PE_CTRL_PE_READY_DES_TRING_OWN,
					     EIP93_PE_CTRL_HOST_READY);
	cdesc.sa_addr = rctx->sa_record_base;
	cdesc.arc4_addr = 0;

	cdesc.state_addr = rctx->sa_state_base;
	cdesc.src_addr = src_addr;
	cdesc.pe_length_word = FIELD_PREP(EIP93_PE_LENGTH_HOST_PE_READY,
					  EIP93_PE_LENGTH_HOST_READY);
	cdesc.pe_length_word |= FIELD_PREP(EIP93_PE_LENGTH_LENGTH,
					   len);

	cdesc.user_id |= FIELD_PREP(EIP93_PE_USER_ID_DESC_FLAGS, EIP93_DESC_HASH);

	if (last) {
		int crypto_async_idr;

		if (rctx->finalize && !rctx->partial_hash) {
			/* For last block, pass sa_record with CMD_HMAC enabled */
			if (IS_HMAC(ctx->flags)) {
				struct sa_record *sa_record_hmac = &rctx->sa_record_hmac;

				rctx->sa_record_hmac_base = dma_map_single(eip93->dev,
									   sa_record_hmac,
									   sizeof(*sa_record_hmac),
									   DMA_TO_DEVICE);
				ret = dma_mapping_error(eip93->dev, rctx->sa_record_hmac_base);
				if (ret)
					return ret;

				cdesc.sa_addr = rctx->sa_record_hmac_base;
			}

			cdesc.pe_ctrl_stat_word |= EIP93_PE_CTRL_PE_HASH_FINAL;
		}

		scoped_guard(spinlock_bh, &eip93->ring->idr_lock)
			crypto_async_idr = idr_alloc(&eip93->ring->crypto_async_idr, async, 0,
						     EIP93_RING_NUM - 1, GFP_ATOMIC);

		cdesc.user_id |= FIELD_PREP(EIP93_PE_USER_ID_CRYPTO_IDR, (u16)crypto_async_idr) |
				 FIELD_PREP(EIP93_PE_USER_ID_DESC_FLAGS, EIP93_DESC_LAST);
	}

again:
	scoped_guard(spinlock_irqsave, &eip93->ring->write_lock)
		ret = eip93_put_descriptor(eip93, &cdesc);
	if (ret) {
		usleep_range(EIP93_RING_BUSY_DELAY,
			     EIP93_RING_BUSY_DELAY * 2);
		goto again;
	}

	/* Writing new descriptor count starts DMA action */
	writel(1, eip93->base + EIP93_REG_PE_CD_COUNT);

	*data_dma = src_addr;
	return 0;
}

static int eip93_hash_init(struct ahash_request *req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct sa_state *sa_state = &rctx->sa_state;

	memset(sa_state->state_byte_cnt, 0, sizeof(u32) * 2);
	eip93_hash_init_sa_state_digest(ctx->flags & EIP93_HASH_MASK,
					sa_state->state_i_digest);

	__eip93_hash_init(req);

	/* For HMAC setup the initial block for ipad */
	if (IS_HMAC(ctx->flags)) {
		memcpy(rctx->data, ctx->ipad, SHA256_BLOCK_SIZE);

		rctx->data_used = SHA256_BLOCK_SIZE;
		rctx->len += SHA256_BLOCK_SIZE;
	}

	return 0;
}

/*
 * With complete_req true, we wait for the engine to consume all the block in list,
 * else we just queue the block to the engine as final() will wait. This is useful
 * for finup().
 */
static int __eip93_hash_update(struct ahash_request *req, bool complete_req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_async_request *async = &req->base;
	unsigned int read, to_consume = req->nbytes;
	unsigned int max_read, consumed = 0;
	struct mkt_hash_block *block;
	bool wait_req = false;
	int offset;
	int ret;

	/* Get the offset and available space to fill req data */
	offset = rctx->data_used;
	max_read = SHA256_BLOCK_SIZE - offset;

	/* Consume req in block of SHA256_BLOCK_SIZE.
	 * to_read is initially set to space available in the req data
	 * and then reset to SHA256_BLOCK_SIZE.
	 */
	while (to_consume > max_read) {
		block = kzalloc(sizeof(*block), GFP_ATOMIC);
		if (!block) {
			ret = -ENOMEM;
			goto free_blocks;
		}

		read = sg_pcopy_to_buffer(req->src, sg_nents(req->src),
					  block->data + offset,
					  max_read, consumed);

		/*
		 * For first iteration only, copy req data to block
		 * and reset offset and max_read for next iteration.
		 */
		if (offset > 0) {
			memcpy(block->data, rctx->data, offset);
			offset = 0;
			max_read = SHA256_BLOCK_SIZE;
		}

		list_add(&block->list, &rctx->blocks);
		to_consume -= read;
		consumed += read;
	}

	/* Write the remaining data to req data */
	read = sg_pcopy_to_buffer(req->src, sg_nents(req->src),
				  rctx->data + offset, to_consume,
				  consumed);
	rctx->data_used = offset + read;

	/* Update counter with processed bytes */
	rctx->len += read + consumed;

	/* Consume all the block added to list */
	list_for_each_entry_reverse(block, &rctx->blocks, list) {
		wait_req = complete_req &&
			    list_is_first(&block->list, &rctx->blocks);

		ret = eip93_send_hash_req(async, block->data,
					  &block->data_dma,
					  SHA256_BLOCK_SIZE, wait_req);
		if (ret)
			goto free_blocks;
	}

	return wait_req ? -EINPROGRESS : 0;

free_blocks:
	eip93_hash_free_data_blocks(req);

	return ret;
}

static int eip93_hash_update(struct ahash_request *req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct sa_record *sa_record = &rctx->sa_record;
	struct sa_state *sa_state = &rctx->sa_state;
	struct eip93_device *eip93 = ctx->eip93;
	int ret;

	if (!req->nbytes)
		return 0;

	rctx->sa_state_base = dma_map_single(eip93->dev, sa_state,
					     sizeof(*sa_state),
					     DMA_TO_DEVICE);
	ret = dma_mapping_error(eip93->dev, rctx->sa_state_base);
	if (ret)
		return ret;

	rctx->sa_record_base = dma_map_single(eip93->dev, sa_record,
					      sizeof(*sa_record),
					      DMA_TO_DEVICE);
	ret = dma_mapping_error(eip93->dev, rctx->sa_record_base);
	if (ret)
		goto free_sa_state;

	ret = __eip93_hash_update(req, true);
	if (ret && ret != -EINPROGRESS)
		goto free_sa_record;

	return ret;

free_sa_record:
	dma_unmap_single(eip93->dev, rctx->sa_record_base,
			 sizeof(*sa_record), DMA_TO_DEVICE);

free_sa_state:
	dma_unmap_single(eip93->dev, rctx->sa_state_base,
			 sizeof(*sa_state), DMA_TO_DEVICE);

	return ret;
}

/*
 * With map_data true, we map the sa_record and sa_state. This is needed
 * for finup() as the they are mapped before calling update()
 */
static int __eip93_hash_final(struct ahash_request *req, bool map_dma)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct crypto_async_request *async = &req->base;
	struct sa_record *sa_record = &rctx->sa_record;
	struct sa_state *sa_state = &rctx->sa_state;
	struct eip93_device *eip93 = ctx->eip93;
	int ret;

	/* EIP93 can't handle zero bytes hash */
	if (!rctx->len && !IS_HMAC(ctx->flags)) {
		switch ((ctx->flags & EIP93_HASH_MASK)) {
		case EIP93_HASH_SHA256:
			memcpy(req->result, sha256_zero_message_hash,
			       SHA256_DIGEST_SIZE);
			break;
		case EIP93_HASH_SHA224:
			memcpy(req->result, sha224_zero_message_hash,
			       SHA224_DIGEST_SIZE);
			break;
		case EIP93_HASH_SHA1:
			memcpy(req->result, sha1_zero_message_hash,
			       SHA1_DIGEST_SIZE);
			break;
		case EIP93_HASH_MD5:
			memcpy(req->result, md5_zero_message_hash,
			       MD5_DIGEST_SIZE);
			break;
		default: /* Impossible */
			return -EINVAL;
		}

		return 0;
	}

	/* Signal interrupt from engine is for last block */
	rctx->finalize = true;

	if (map_dma) {
		rctx->sa_state_base = dma_map_single(eip93->dev, sa_state,
						     sizeof(*sa_state),
						     DMA_TO_DEVICE);
		ret = dma_mapping_error(eip93->dev, rctx->sa_state_base);
		if (ret)
			return ret;

		rctx->sa_record_base = dma_map_single(eip93->dev, sa_record,
						      sizeof(*sa_record),
						      DMA_TO_DEVICE);
		ret = dma_mapping_error(eip93->dev, rctx->sa_record_base);
		if (ret)
			goto free_sa_state;
	}

	/* Send last block */
	ret = eip93_send_hash_req(async, rctx->data, &rctx->data_dma,
				  rctx->data_used, true);
	if (ret)
		goto free_blocks;

	return -EINPROGRESS;

free_blocks:
	eip93_hash_free_data_blocks(req);

	dma_unmap_single(eip93->dev, rctx->sa_record_base,
			 sizeof(*sa_record), DMA_TO_DEVICE);

free_sa_state:
	dma_unmap_single(eip93->dev, rctx->sa_state_base,
			 sizeof(*sa_state), DMA_TO_DEVICE);

	return ret;
}

static int eip93_hash_final(struct ahash_request *req)
{
	return __eip93_hash_final(req, true);
}

static int eip93_hash_finup(struct ahash_request *req)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct eip93_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct sa_record *sa_record = &rctx->sa_record;
	struct sa_state *sa_state = &rctx->sa_state;
	struct eip93_device *eip93 = ctx->eip93;
	int ret;

	if (rctx->len + req->nbytes || IS_HMAC(ctx->flags)) {
		rctx->sa_state_base = dma_map_single(eip93->dev, sa_state,
						     sizeof(*sa_state),
						     DMA_TO_DEVICE);
		ret = dma_mapping_error(eip93->dev, rctx->sa_state_base);
		if (ret)
			return ret;

		rctx->sa_record_base = dma_map_single(eip93->dev, sa_record,
						      sizeof(*sa_record),
						      DMA_TO_DEVICE);
		ret = dma_mapping_error(eip93->dev, rctx->sa_record_base);
		if (ret)
			goto free_sa_state;

		ret = __eip93_hash_update(req, false);
		if (ret)
			goto free_sa_record;
	}

	return __eip93_hash_final(req, false);

free_sa_record:
	dma_unmap_single(eip93->dev, rctx->sa_record_base,
			 sizeof(*sa_record), DMA_TO_DEVICE);
free_sa_state:
	dma_unmap_single(eip93->dev, rctx->sa_state_base,
			 sizeof(*sa_state), DMA_TO_DEVICE);

	return ret;
}

static int eip93_hash_hmac_setkey(struct crypto_ahash *ahash, const u8 *key,
				  u32 keylen)
{
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	struct crypto_tfm *tfm = crypto_ahash_tfm(ahash);
	struct eip93_hash_ctx *ctx = crypto_tfm_ctx(tfm);

	return eip93_hmac_setkey(ctx->flags, key, keylen, digestsize,
				 ctx->ipad, ctx->opad, true);
}

static int eip93_hash_cra_init(struct crypto_tfm *tfm)
{
	struct eip93_hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct eip93_alg_template *tmpl = container_of(tfm->__crt_alg,
				struct eip93_alg_template, alg.ahash.halg.base);

	crypto_ahash_set_reqsize_dma(__crypto_ahash_cast(tfm),
				     sizeof(struct eip93_hash_reqctx));

	ctx->eip93 = tmpl->eip93;
	ctx->flags = tmpl->flags;

	return 0;
}

static int eip93_hash_digest(struct ahash_request *req)
{
	int ret;

	ret = eip93_hash_init(req);
	if (ret)
		return ret;

	return eip93_hash_finup(req);
}

static int eip93_hash_import(struct ahash_request *req, const void *in)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	const struct eip93_hash_export_state *state = in;
	struct sa_state *sa_state = &rctx->sa_state;

	memcpy(sa_state->state_byte_cnt, state->state_len, sizeof(u32) * 2);
	memcpy(sa_state->state_i_digest, state->state_hash, SHA256_DIGEST_SIZE);

	__eip93_hash_init(req);

	rctx->len = state->len;
	rctx->data_used = state->data_used;

	/* Skip copying data if we have nothing to copy */
	if (rctx->len)
		memcpy(rctx->data, state->data, rctx->data_used);

	return 0;
}

static int eip93_hash_export(struct ahash_request *req, void *out)
{
	struct eip93_hash_reqctx *rctx = ahash_request_ctx_dma(req);
	struct eip93_hash_export_state *state = out;

	/* Save the first block in state data */
	if (rctx->len)
		memcpy(state->data, rctx->data, rctx->data_used);

	eip93_hash_export_sa_state(req, state);

	return 0;
}

struct eip93_alg_template eip93_alg_md5 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_MD5,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "md5",
				.cra_driver_name = "md5-eip93",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_sha1 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_SHA1,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "sha1",
				.cra_driver_name = "sha1-eip93",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_sha224 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_SHA224,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "sha224",
				.cra_driver_name = "sha224-eip93",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_sha256 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_SHA256,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "sha256",
				.cra_driver_name = "sha256-eip93",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_hmac_md5 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_MD5,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.setkey = eip93_hash_hmac_setkey,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = MD5_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "hmac(md5)",
				.cra_driver_name = "hmac(md5-eip93)",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = MD5_HMAC_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_hmac_sha1 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA1,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.setkey = eip93_hash_hmac_setkey,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = SHA1_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "hmac(sha1)",
				.cra_driver_name = "hmac(sha1-eip93)",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_hmac_sha224 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA224,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.setkey = eip93_hash_hmac_setkey,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = SHA224_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "hmac(sha224)",
				.cra_driver_name = "hmac(sha224-eip93)",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};

struct eip93_alg_template eip93_alg_hmac_sha256 = {
	.type = EIP93_ALG_TYPE_HASH,
	.flags = EIP93_HASH_HMAC | EIP93_HASH_SHA256,
	.alg.ahash = {
		.init = eip93_hash_init,
		.update = eip93_hash_update,
		.final = eip93_hash_final,
		.finup = eip93_hash_finup,
		.digest = eip93_hash_digest,
		.setkey = eip93_hash_hmac_setkey,
		.export = eip93_hash_export,
		.import = eip93_hash_import,
		.halg = {
			.digestsize = SHA256_DIGEST_SIZE,
			.statesize = sizeof(struct eip93_hash_export_state),
			.base = {
				.cra_name = "hmac(sha256)",
				.cra_driver_name = "hmac(sha256-eip93)",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_KERN_DRIVER_ONLY |
						CRYPTO_ALG_ALLOCATES_MEMORY,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct eip93_hash_ctx),
				.cra_init = eip93_hash_cra_init,
				.cra_module = THIS_MODULE,
			},
		},
	},
};
