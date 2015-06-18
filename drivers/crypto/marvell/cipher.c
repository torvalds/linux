/*
 * Cipher algorithms supported by the CESA: DES, 3DES and AES.
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

#include <crypto/aes.h>

#include "cesa.h"

struct mv_cesa_aes_ctx {
	struct mv_cesa_ctx base;
	struct crypto_aes_ctx aes;
};

static void mv_cesa_ablkcipher_std_step(struct ablkcipher_request *req)
{
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(req);
	struct mv_cesa_ablkcipher_std_req *sreq = &creq->req.std;
	struct mv_cesa_engine *engine = sreq->base.engine;
	size_t  len = min_t(size_t, req->nbytes - sreq->offset,
			    CESA_SA_SRAM_PAYLOAD_SIZE);

	len = sg_pcopy_to_buffer(req->src, creq->src_nents,
				 engine->sram + CESA_SA_DATA_SRAM_OFFSET,
				 len, sreq->offset);

	sreq->size = len;
	mv_cesa_set_crypt_op_len(&sreq->op, len);

	/* FIXME: only update enc_len field */
	if (!sreq->skip_ctx) {
		memcpy(engine->sram, &sreq->op, sizeof(sreq->op));
		sreq->skip_ctx = true;
	} else {
		memcpy(engine->sram, &sreq->op, sizeof(sreq->op.desc));
	}

	mv_cesa_set_int_mask(engine, CESA_SA_INT_ACCEL0_DONE);
	writel(CESA_SA_CFG_PARA_DIS, engine->regs + CESA_SA_CFG);
	writel(CESA_SA_CMD_EN_CESA_SA_ACCL0, engine->regs + CESA_SA_CMD);
}

static int mv_cesa_ablkcipher_std_process(struct ablkcipher_request *req,
					  u32 status)
{
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(req);
	struct mv_cesa_ablkcipher_std_req *sreq = &creq->req.std;
	struct mv_cesa_engine *engine = sreq->base.engine;
	size_t len;

	len = sg_pcopy_from_buffer(req->dst, creq->dst_nents,
				   engine->sram + CESA_SA_DATA_SRAM_OFFSET,
				   sreq->size, sreq->offset);

	sreq->offset += len;
	if (sreq->offset < req->nbytes)
		return -EINPROGRESS;

	return 0;
}

static int mv_cesa_ablkcipher_process(struct crypto_async_request *req,
				      u32 status)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(ablkreq);
	struct mv_cesa_ablkcipher_std_req *sreq = &creq->req.std;
	struct mv_cesa_engine *engine = sreq->base.engine;
	int ret;

	ret = mv_cesa_ablkcipher_std_process(ablkreq, status);
	if (ret)
		return ret;

	memcpy(ablkreq->info, engine->sram + CESA_SA_CRYPT_IV_SRAM_OFFSET,
	       crypto_ablkcipher_ivsize(crypto_ablkcipher_reqtfm(ablkreq)));

	return 0;
}

static void mv_cesa_ablkcipher_step(struct crypto_async_request *req)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);

	mv_cesa_ablkcipher_std_step(ablkreq);
}

static inline void
mv_cesa_ablkcipher_std_prepare(struct ablkcipher_request *req)
{
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(req);
	struct mv_cesa_ablkcipher_std_req *sreq = &creq->req.std;
	struct mv_cesa_engine *engine = sreq->base.engine;

	sreq->size = 0;
	sreq->offset = 0;
	mv_cesa_adjust_op(engine, &sreq->op);
	memcpy(engine->sram, &sreq->op, sizeof(sreq->op));
}

static inline void mv_cesa_ablkcipher_prepare(struct crypto_async_request *req,
					      struct mv_cesa_engine *engine)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(ablkreq);

	creq->req.base.engine = engine;

	mv_cesa_ablkcipher_std_prepare(ablkreq);
}

static inline void
mv_cesa_ablkcipher_req_cleanup(struct crypto_async_request *req)
{
}

static const struct mv_cesa_req_ops mv_cesa_ablkcipher_req_ops = {
	.step = mv_cesa_ablkcipher_step,
	.process = mv_cesa_ablkcipher_process,
	.prepare = mv_cesa_ablkcipher_prepare,
	.cleanup = mv_cesa_ablkcipher_req_cleanup,
};

static int mv_cesa_ablkcipher_cra_init(struct crypto_tfm *tfm)
{
	struct mv_cesa_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->base.ops = &mv_cesa_ablkcipher_req_ops;

	tfm->crt_ablkcipher.reqsize = sizeof(struct mv_cesa_ablkcipher_req);

	return 0;
}

static int mv_cesa_aes_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			      unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct mv_cesa_aes_ctx *ctx = crypto_tfm_ctx(tfm);
	int remaining;
	int offset;
	int ret;
	int i;

	ret = crypto_aes_expand_key(&ctx->aes, key, len);
	if (ret) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return ret;
	}

	remaining = (ctx->aes.key_length - 16) / 4;
	offset = ctx->aes.key_length + 24 - remaining;
	for (i = 0; i < remaining; i++)
		ctx->aes.key_dec[4 + i] =
			cpu_to_le32(ctx->aes.key_enc[offset + i]);

	return 0;
}

static inline int
mv_cesa_ablkcipher_std_req_init(struct ablkcipher_request *req,
				const struct mv_cesa_op_ctx *op_templ)
{
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(req);
	struct mv_cesa_ablkcipher_std_req *sreq = &creq->req.std;

	sreq->base.type = CESA_STD_REQ;
	sreq->op = *op_templ;
	sreq->skip_ctx = false;

	return 0;
}

static int mv_cesa_ablkcipher_req_init(struct ablkcipher_request *req,
				       struct mv_cesa_op_ctx *tmpl)
{
	struct mv_cesa_ablkcipher_req *creq = ablkcipher_request_ctx(req);
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	unsigned int blksize = crypto_ablkcipher_blocksize(tfm);

	if (!IS_ALIGNED(req->nbytes, blksize))
		return -EINVAL;

	creq->src_nents = sg_nents_for_len(req->src, req->nbytes);
	creq->dst_nents = sg_nents_for_len(req->dst, req->nbytes);

	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_OP_CRYPT_ONLY,
			      CESA_SA_DESC_CFG_OP_MSK);

	return mv_cesa_ablkcipher_std_req_init(req, tmpl);
}

static int mv_cesa_aes_op(struct ablkcipher_request *req,
			  struct mv_cesa_op_ctx *tmpl)
{
	struct mv_cesa_aes_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	int ret, i;
	u32 *key;
	u32 cfg;

	cfg = CESA_SA_DESC_CFG_CRYPTM_AES;

	if (mv_cesa_get_op_cfg(tmpl) & CESA_SA_DESC_CFG_DIR_DEC)
		key = ctx->aes.key_dec;
	else
		key = ctx->aes.key_enc;

	for (i = 0; i < ctx->aes.key_length / sizeof(u32); i++)
		tmpl->ctx.blkcipher.key[i] = cpu_to_le32(key[i]);

	if (ctx->aes.key_length == 24)
		cfg |= CESA_SA_DESC_CFG_AES_LEN_192;
	else if (ctx->aes.key_length == 32)
		cfg |= CESA_SA_DESC_CFG_AES_LEN_256;

	mv_cesa_update_op_cfg(tmpl, cfg,
			      CESA_SA_DESC_CFG_CRYPTM_MSK |
			      CESA_SA_DESC_CFG_AES_LEN_MSK);

	ret = mv_cesa_ablkcipher_req_init(req, tmpl);
	if (ret)
		return ret;

	return mv_cesa_queue_req(&req->base);
}

static int mv_cesa_ecb_aes_encrypt(struct ablkcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_aes_op(req, &tmpl);
}

static int mv_cesa_ecb_aes_decrypt(struct ablkcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl,
			   CESA_SA_DESC_CFG_CRYPTCM_ECB |
			   CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_aes_op(req, &tmpl);
}

struct crypto_alg mv_cesa_ecb_aes_alg = {
	.cra_name = "ecb(aes)",
	.cra_driver_name = "mv-ecb-aes",
	.cra_priority = 300,
	.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
		     CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct mv_cesa_aes_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_ablkcipher_type,
	.cra_module = THIS_MODULE,
	.cra_init = mv_cesa_ablkcipher_cra_init,
	.cra_u = {
		.ablkcipher = {
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.setkey = mv_cesa_aes_setkey,
			.encrypt = mv_cesa_ecb_aes_encrypt,
			.decrypt = mv_cesa_ecb_aes_decrypt,
		},
	},
};

static int mv_cesa_cbc_aes_op(struct ablkcipher_request *req,
			      struct mv_cesa_op_ctx *tmpl)
{
	mv_cesa_update_op_cfg(tmpl, CESA_SA_DESC_CFG_CRYPTCM_CBC,
			      CESA_SA_DESC_CFG_CRYPTCM_MSK);
	memcpy(tmpl->ctx.blkcipher.iv, req->info, AES_BLOCK_SIZE);

	return mv_cesa_aes_op(req, tmpl);
}

static int mv_cesa_cbc_aes_encrypt(struct ablkcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_DIR_ENC);

	return mv_cesa_cbc_aes_op(req, &tmpl);
}

static int mv_cesa_cbc_aes_decrypt(struct ablkcipher_request *req)
{
	struct mv_cesa_op_ctx tmpl;

	mv_cesa_set_op_cfg(&tmpl, CESA_SA_DESC_CFG_DIR_DEC);

	return mv_cesa_cbc_aes_op(req, &tmpl);
}

struct crypto_alg mv_cesa_cbc_aes_alg = {
	.cra_name = "cbc(aes)",
	.cra_driver_name = "mv-cbc-aes",
	.cra_priority = 300,
	.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
		     CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct mv_cesa_aes_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_ablkcipher_type,
	.cra_module = THIS_MODULE,
	.cra_init = mv_cesa_ablkcipher_cra_init,
	.cra_u = {
		.ablkcipher = {
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			.setkey = mv_cesa_aes_setkey,
			.encrypt = mv_cesa_cbc_aes_encrypt,
			.decrypt = mv_cesa_cbc_aes_decrypt,
		},
	},
};
