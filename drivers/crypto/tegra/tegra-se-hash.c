// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver to handle HASH algorithms using NVIDIA Security Engine.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <crypto/aes.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <crypto/internal/des.h>
#include <crypto/engine.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/hash.h>

#include "tegra-se.h"

struct tegra_sha_ctx {
	struct tegra_se *se;
	unsigned int alg;
	bool fallback;
	u32 key_id;
	struct crypto_ahash *fallback_tfm;
};

struct tegra_sha_reqctx {
	struct scatterlist *src_sg;
	struct tegra_se_datbuf datbuf;
	struct tegra_se_datbuf residue;
	struct tegra_se_datbuf digest;
	struct tegra_se_datbuf intr_res;
	unsigned int alg;
	unsigned int config;
	unsigned int total_len;
	unsigned int blk_size;
	unsigned int task;
	u32 key_id;
	u32 result[HASH_RESULT_REG_COUNT];
	struct ahash_request fallback_req;
};

static int tegra_sha_get_config(u32 alg)
{
	int cfg = 0;

	switch (alg) {
	case SE_ALG_SHA1:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA1;
		break;

	case SE_ALG_HMAC_SHA224:
		cfg |= SE_SHA_ENC_ALG_HMAC;
		fallthrough;
	case SE_ALG_SHA224:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA224;
		break;

	case SE_ALG_HMAC_SHA256:
		cfg |= SE_SHA_ENC_ALG_HMAC;
		fallthrough;
	case SE_ALG_SHA256:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA256;
		break;

	case SE_ALG_HMAC_SHA384:
		cfg |= SE_SHA_ENC_ALG_HMAC;
		fallthrough;
	case SE_ALG_SHA384:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA384;
		break;

	case SE_ALG_HMAC_SHA512:
		cfg |= SE_SHA_ENC_ALG_HMAC;
		fallthrough;
	case SE_ALG_SHA512:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA512;
		break;

	case SE_ALG_SHA3_224:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA3_224;
		break;
	case SE_ALG_SHA3_256:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA3_256;
		break;
	case SE_ALG_SHA3_384:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA3_384;
		break;
	case SE_ALG_SHA3_512:
		cfg |= SE_SHA_ENC_ALG_SHA;
		cfg |= SE_SHA_ENC_MODE_SHA3_512;
		break;
	default:
		return -EINVAL;
	}

	return cfg;
}

static int tegra_sha_fallback_init(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

static int tegra_sha_fallback_update(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

static int tegra_sha_fallback_final(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_final(&rctx->fallback_req);
}

static int tegra_sha_fallback_finup(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int tegra_sha_fallback_digest(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_digest(&rctx->fallback_req);
}

static int tegra_sha_fallback_import(struct ahash_request *req, const void *in)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&rctx->fallback_req, in);
}

static int tegra_sha_fallback_export(struct ahash_request *req, void *out)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags &
					CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&rctx->fallback_req, out);
}

static int tegra_se_insert_hash_result(struct tegra_sha_ctx *ctx, u32 *cpuvaddr,
				       struct tegra_sha_reqctx *rctx)
{
	__be32 *res_be = (__be32 *)rctx->intr_res.buf;
	u32 *res = (u32 *)rctx->intr_res.buf;
	int i = 0, j;

	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = host1x_opcode_setpayload(HASH_RESULT_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(SE_SHA_HASH_RESULT);

	for (j = 0; j < HASH_RESULT_REG_COUNT; j++) {
		int idx = j;

		/*
		 * The initial, intermediate and final hash value of SHA-384, SHA-512
		 * in SHA_HASH_RESULT registers follow the below layout of bytes.
		 *
		 * +---------------+------------+
		 * | HASH_RESULT_0 | B4...B7    |
		 * +---------------+------------+
		 * | HASH_RESULT_1 | B0...B3    |
		 * +---------------+------------+
		 * | HASH_RESULT_2 | B12...B15  |
		 * +---------------+------------+
		 * | HASH_RESULT_3 | B8...B11   |
		 * +---------------+------------+
		 * |            ......          |
		 * +---------------+------------+
		 * | HASH_RESULT_14| B60...B63  |
		 * +---------------+------------+
		 * | HASH_RESULT_15| B56...B59  |
		 * +---------------+------------+
		 *
		 */
		if (ctx->alg == SE_ALG_SHA384 || ctx->alg == SE_ALG_SHA512)
			idx = (j % 2) ? j - 1 : j + 1;

		/* For SHA-1, SHA-224, SHA-256, SHA-384, SHA-512 the initial
		 * intermediate and final hash value when stored in
		 * SHA_HASH_RESULT registers, the byte order is NOT in
		 * little-endian.
		 */
		if (ctx->alg <= SE_ALG_SHA512)
			cpuvaddr[i++] = be32_to_cpu(res_be[idx]);
		else
			cpuvaddr[i++] = res[idx];
	}

	return i;
}

static int tegra_sha_prep_cmd(struct tegra_sha_ctx *ctx, u32 *cpuvaddr,
			      struct tegra_sha_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u64 msg_len, msg_left;
	int i = 0;

	msg_len = rctx->total_len * 8;
	msg_left = rctx->datbuf.size * 8;

	/*
	 * If IN_ADDR_HI_0.SZ > SHA_MSG_LEFT_[0-3] to the HASH engine,
	 * HW treats it as the last buffer and process the data.
	 * Therefore, add an extra byte to msg_left if it is not the
	 * last buffer.
	 */
	if (rctx->task & SHA_UPDATE) {
		msg_left += 8;
		msg_len += 8;
	}

	cpuvaddr[i++] = host1x_opcode_setpayload(8);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(SE_SHA_MSG_LENGTH);
	cpuvaddr[i++] = lower_32_bits(msg_len);
	cpuvaddr[i++] = upper_32_bits(msg_len);
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = lower_32_bits(msg_left);
	cpuvaddr[i++] = upper_32_bits(msg_left);
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = host1x_opcode_setpayload(2);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(SE_SHA_CFG);
	cpuvaddr[i++] = rctx->config;

	if (rctx->task & SHA_FIRST) {
		cpuvaddr[i++] = SE_SHA_TASK_HASH_INIT;
		rctx->task &= ~SHA_FIRST;
	} else {
		/*
		 * If it isn't the first task, program the HASH_RESULT register
		 * with the intermediate result from the previous task
		 */
		i += tegra_se_insert_hash_result(ctx, cpuvaddr + i, rctx);
	}

	cpuvaddr[i++] = host1x_opcode_setpayload(4);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(SE_SHA_IN_ADDR);
	cpuvaddr[i++] = rctx->datbuf.addr;
	cpuvaddr[i++] = (u32)(SE_ADDR_HI_MSB(upper_32_bits(rctx->datbuf.addr)) |
				SE_ADDR_HI_SZ(rctx->datbuf.size));

	if (rctx->task & SHA_UPDATE) {
		cpuvaddr[i++] = rctx->intr_res.addr;
		cpuvaddr[i++] = (u32)(SE_ADDR_HI_MSB(upper_32_bits(rctx->intr_res.addr)) |
					SE_ADDR_HI_SZ(rctx->intr_res.size));
	} else {
		cpuvaddr[i++] = rctx->digest.addr;
		cpuvaddr[i++] = (u32)(SE_ADDR_HI_MSB(upper_32_bits(rctx->digest.addr)) |
					SE_ADDR_HI_SZ(rctx->digest.size));
	}

	if (rctx->key_id) {
		cpuvaddr[i++] = host1x_opcode_setpayload(1);
		cpuvaddr[i++] = se_host1x_opcode_nonincr_w(SE_SHA_CRYPTO_CFG);
		cpuvaddr[i++] = SE_AES_KEY_INDEX(rctx->key_id);
	}

	cpuvaddr[i++] = host1x_opcode_setpayload(1);
	cpuvaddr[i++] = se_host1x_opcode_nonincr_w(SE_SHA_OPERATION);
	cpuvaddr[i++] = SE_SHA_OP_WRSTALL | SE_SHA_OP_START |
			SE_SHA_OP_LASTBUF;
	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "msg len %llu msg left %llu sz %zd cfg %#x",
		msg_len, msg_left, rctx->datbuf.size, rctx->config);

	return i;
}

static int tegra_sha_do_init(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;

	if (ctx->fallback)
		return tegra_sha_fallback_init(req);

	rctx->total_len = 0;
	rctx->datbuf.size = 0;
	rctx->residue.size = 0;
	rctx->key_id = ctx->key_id;
	rctx->task |= SHA_FIRST;
	rctx->alg = ctx->alg;
	rctx->blk_size = crypto_ahash_blocksize(tfm);
	rctx->digest.size = crypto_ahash_digestsize(tfm);

	rctx->digest.buf = dma_alloc_coherent(se->dev, rctx->digest.size,
					      &rctx->digest.addr, GFP_KERNEL);
	if (!rctx->digest.buf)
		goto digbuf_fail;

	rctx->residue.buf = dma_alloc_coherent(se->dev, rctx->blk_size,
					       &rctx->residue.addr, GFP_KERNEL);
	if (!rctx->residue.buf)
		goto resbuf_fail;

	rctx->intr_res.size = HASH_RESULT_REG_COUNT * 4;
	rctx->intr_res.buf = dma_alloc_coherent(se->dev, rctx->intr_res.size,
						&rctx->intr_res.addr, GFP_KERNEL);
	if (!rctx->intr_res.buf)
		goto intr_res_fail;

	return 0;

intr_res_fail:
	dma_free_coherent(se->dev, rctx->residue.size, rctx->residue.buf,
			  rctx->residue.addr);
resbuf_fail:
	dma_free_coherent(se->dev, rctx->digest.size, rctx->digest.buf,
			  rctx->digest.addr);
digbuf_fail:
	return -ENOMEM;
}

static int tegra_sha_do_update(struct ahash_request *req)
{
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(crypto_ahash_reqtfm(req));
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct tegra_se *se = ctx->se;
	unsigned int nblks, nresidue, size, ret;
	u32 *cpuvaddr = se->cmdbuf->addr;

	nresidue = (req->nbytes + rctx->residue.size) % rctx->blk_size;
	nblks = (req->nbytes + rctx->residue.size) / rctx->blk_size;

	/*
	 * If nbytes is a multiple of block size and there is no residue,
	 * then reserve the last block as residue during final() to process.
	 */
	if (!nresidue && nblks) {
		nresidue = rctx->blk_size;
		nblks--;
	}

	rctx->src_sg = req->src;
	rctx->datbuf.size = (req->nbytes + rctx->residue.size) - nresidue;

	/*
	 * If nbytes are less than a block size, copy it residue and
	 * return. The bytes will be processed in final()
	 */
	if (nblks < 1) {
		scatterwalk_map_and_copy(rctx->residue.buf + rctx->residue.size,
					 rctx->src_sg, 0, req->nbytes, 0);
		rctx->residue.size += req->nbytes;

		return 0;
	}

	rctx->datbuf.buf = dma_alloc_coherent(se->dev, rctx->datbuf.size,
					      &rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf)
		return -ENOMEM;

	/* Copy the previous residue first */
	if (rctx->residue.size)
		memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);

	scatterwalk_map_and_copy(rctx->datbuf.buf + rctx->residue.size,
				 rctx->src_sg, 0, req->nbytes - nresidue, 0);

	scatterwalk_map_and_copy(rctx->residue.buf, rctx->src_sg,
				 req->nbytes - nresidue, nresidue, 0);

	/* Update residue value with the residue after current block */
	rctx->residue.size = nresidue;
	rctx->total_len += rctx->datbuf.size;

	rctx->config = tegra_sha_get_config(rctx->alg) |
			SE_SHA_DST_MEMORY;

	size = tegra_sha_prep_cmd(ctx, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);

	dma_free_coherent(se->dev, rctx->datbuf.size,
			  rctx->datbuf.buf, rctx->datbuf.addr);

	return ret;
}

static int tegra_sha_do_final(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int size, ret = 0;

	if (rctx->residue.size) {
		rctx->datbuf.buf = dma_alloc_coherent(se->dev, rctx->residue.size,
						      &rctx->datbuf.addr, GFP_KERNEL);
		if (!rctx->datbuf.buf) {
			ret = -ENOMEM;
			goto out_free;
		}

		memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);
	}

	rctx->datbuf.size = rctx->residue.size;
	rctx->total_len += rctx->residue.size;

	rctx->config = tegra_sha_get_config(rctx->alg) |
		       SE_SHA_DST_MEMORY;

	size = tegra_sha_prep_cmd(ctx, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, se->cmdbuf, size);
	if (ret)
		goto out;

	/* Copy result */
	memcpy(req->result, rctx->digest.buf, rctx->digest.size);

out:
	if (rctx->residue.size)
		dma_free_coherent(se->dev, rctx->datbuf.size,
				  rctx->datbuf.buf, rctx->datbuf.addr);
out_free:
	dma_free_coherent(se->dev, crypto_ahash_blocksize(tfm),
			  rctx->residue.buf, rctx->residue.addr);
	dma_free_coherent(se->dev, rctx->digest.size, rctx->digest.buf,
			  rctx->digest.addr);

	dma_free_coherent(se->dev, rctx->intr_res.size, rctx->intr_res.buf,
			  rctx->intr_res.addr);

	return ret;
}

static int tegra_sha_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret = 0;

	if (rctx->task & SHA_INIT) {
		ret = tegra_sha_do_init(req);
		if (ret)
			goto out;

		rctx->task &= ~SHA_INIT;
	}

	if (rctx->task & SHA_UPDATE) {
		ret = tegra_sha_do_update(req);
		if (ret)
			goto out;

		rctx->task &= ~SHA_UPDATE;
	}

	if (rctx->task & SHA_FINAL) {
		ret = tegra_sha_do_final(req);
		if (ret)
			goto out;

		rctx->task &= ~SHA_FINAL;
	}

out:
	crypto_finalize_hash_request(se->engine, req, ret);

	return 0;
}

static void tegra_sha_init_fallback(struct crypto_ahash *tfm, struct tegra_sha_ctx *ctx,
				    const char *algname)
{
	unsigned int statesize;

	ctx->fallback_tfm = crypto_alloc_ahash(algname, 0, CRYPTO_ALG_ASYNC |
						CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(ctx->fallback_tfm)) {
		dev_warn(ctx->se->dev,
			 "failed to allocate fallback for %s\n", algname);
		ctx->fallback_tfm = NULL;
		return;
	}

	statesize = crypto_ahash_statesize(ctx->fallback_tfm);

	if (statesize > sizeof(struct tegra_sha_reqctx))
		crypto_ahash_set_statesize(tfm, statesize);

	/* Update reqsize if fallback is added */
	crypto_ahash_set_reqsize(tfm,
				 sizeof(struct tegra_sha_reqctx) +
			crypto_ahash_reqsize(ctx->fallback_tfm));
}

static int tegra_sha_cra_init(struct crypto_tfm *tfm)
{
	struct tegra_sha_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ahash *ahash_tfm = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct tegra_se_alg *se_alg;
	const char *algname;
	int ret;

	algname = crypto_tfm_alg_name(tfm);
	se_alg = container_of(alg, struct tegra_se_alg, alg.ahash.base);

	crypto_ahash_set_reqsize(ahash_tfm, sizeof(struct tegra_sha_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->fallback = false;
	ctx->key_id = 0;

	ret = se_algname_to_algid(algname);
	if (ret < 0) {
		dev_err(ctx->se->dev, "invalid algorithm\n");
		return ret;
	}

	if (se_alg->alg_base)
		tegra_sha_init_fallback(ahash_tfm, ctx, algname);

	ctx->alg = ret;

	return 0;
}

static void tegra_sha_cra_exit(struct crypto_tfm *tfm)
{
	struct tegra_sha_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->fallback_tfm)
		crypto_free_ahash(ctx->fallback_tfm);

	tegra_key_invalidate(ctx->se, ctx->key_id, ctx->alg);
}

static int tegra_hmac_fallback_setkey(struct tegra_sha_ctx *ctx, const u8 *key,
				      unsigned int keylen)
{
	if (!ctx->fallback_tfm) {
		dev_dbg(ctx->se->dev, "invalid key length (%d)\n", keylen);
		return -EINVAL;
	}

	ctx->fallback = true;
	return crypto_ahash_setkey(ctx->fallback_tfm, key, keylen);
}

static int tegra_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);
	int ret;

	if (aes_check_keylen(keylen))
		return tegra_hmac_fallback_setkey(ctx, key, keylen);

	ret = tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
	if (ret)
		return tegra_hmac_fallback_setkey(ctx, key, keylen);

	ctx->fallback = false;

	return 0;
}

static int tegra_sha_init(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	rctx->task = SHA_INIT;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sha_update(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	if (ctx->fallback)
		return tegra_sha_fallback_update(req);

	rctx->task |= SHA_UPDATE;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sha_final(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	if (ctx->fallback)
		return tegra_sha_fallback_final(req);

	rctx->task |= SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sha_finup(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	if (ctx->fallback)
		return tegra_sha_fallback_finup(req);

	rctx->task |= SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sha_digest(struct ahash_request *req)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	if (ctx->fallback)
		return tegra_sha_fallback_digest(req);

	rctx->task |= SHA_INIT | SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_sha_export(struct ahash_request *req, void *out)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	if (ctx->fallback)
		return tegra_sha_fallback_export(req, out);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int tegra_sha_import(struct ahash_request *req, const void *in)
{
	struct tegra_sha_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_sha_ctx *ctx = crypto_ahash_ctx(tfm);

	if (ctx->fallback)
		return tegra_sha_fallback_import(req, in);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static struct tegra_se_alg tegra_hash_algs[] = {
	{
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA1_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha1",
				.cra_driver_name = "tegra-se-sha1",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA1_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha224",
				.cra_driver_name = "tegra-se-sha224",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA256_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha256",
				.cra_driver_name = "tegra-se-sha256",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA384_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha384",
				.cra_driver_name = "tegra-se-sha384",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA512_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha512",
				.cra_driver_name = "tegra-se-sha512",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA3_224_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha3-224",
				.cra_driver_name = "tegra-se-sha3-224",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA3_224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA3_256_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha3-256",
				.cra_driver_name = "tegra-se-sha3-256",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA3_256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA3_384_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha3-384",
				.cra_driver_name = "tegra-se-sha3-384",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA3_384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.halg.digestsize = SHA3_512_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "sha3-512",
				.cra_driver_name = "tegra-se-sha3-512",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = SHA3_512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg_base = "sha224",
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.setkey = tegra_hmac_setkey,
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "hmac(sha224)",
				.cra_driver_name = "tegra-se-hmac-sha224",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA224_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg_base = "sha256",
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.setkey = tegra_hmac_setkey,
			.halg.digestsize = SHA256_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "hmac(sha256)",
				.cra_driver_name = "tegra-se-hmac-sha256",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA256_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg_base = "sha384",
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.setkey = tegra_hmac_setkey,
			.halg.digestsize = SHA384_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "hmac(sha384)",
				.cra_driver_name = "tegra-se-hmac-sha384",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA384_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}, {
		.alg_base = "sha512",
		.alg.ahash.op.do_one_request = tegra_sha_do_one_req,
		.alg.ahash.base = {
			.init = tegra_sha_init,
			.update = tegra_sha_update,
			.final = tegra_sha_final,
			.finup = tegra_sha_finup,
			.digest = tegra_sha_digest,
			.export = tegra_sha_export,
			.import = tegra_sha_import,
			.setkey = tegra_hmac_setkey,
			.halg.digestsize = SHA512_DIGEST_SIZE,
			.halg.statesize = sizeof(struct tegra_sha_reqctx),
			.halg.base = {
				.cra_name = "hmac(sha512)",
				.cra_driver_name = "tegra-se-hmac-sha512",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize = SHA512_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_sha_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_sha_cra_init,
				.cra_exit = tegra_sha_cra_exit,
			}
		}
	}
};

static int tegra_hash_kac_manifest(u32 user, u32 alg, u32 keylen)
{
	int manifest;

	manifest = SE_KAC_USER_NS;

	switch (alg) {
	case SE_ALG_HMAC_SHA224:
	case SE_ALG_HMAC_SHA256:
	case SE_ALG_HMAC_SHA384:
	case SE_ALG_HMAC_SHA512:
		manifest |= SE_KAC_HMAC;
		break;
	default:
		return -EINVAL;
	}

	switch (keylen) {
	case AES_KEYSIZE_128:
		manifest |= SE_KAC_SIZE_128;
		break;
	case AES_KEYSIZE_192:
		manifest |= SE_KAC_SIZE_192;
		break;
	case AES_KEYSIZE_256:
	default:
		manifest |= SE_KAC_SIZE_256;
		break;
	}

	return manifest;
}

int tegra_init_hash(struct tegra_se *se)
{
	struct ahash_engine_alg *alg;
	int i, ret;

	se->manifest = tegra_hash_kac_manifest;

	for (i = 0; i < ARRAY_SIZE(tegra_hash_algs); i++) {
		tegra_hash_algs[i].se_dev = se;
		alg = &tegra_hash_algs[i].alg.ahash;

		ret = crypto_engine_register_ahash(alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				alg->base.halg.base.cra_name);
			goto sha_err;
		}
	}

	return 0;

sha_err:
	while (i--)
		crypto_engine_unregister_ahash(&tegra_hash_algs[i].alg.ahash);

	return ret;
}

void tegra_deinit_hash(struct tegra_se *se)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra_hash_algs); i++)
		crypto_engine_unregister_ahash(&tegra_hash_algs[i].alg.ahash);
}
