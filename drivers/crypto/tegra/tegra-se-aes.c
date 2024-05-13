// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver to handle block cipher algorithms using NVIDIA Security Engine.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/gcm.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>

#include "tegra-se.h"

struct tegra_aes_ctx {
	struct tegra_se *se;
	u32 alg;
	u32 ivsize;
	u32 key1_id;
	u32 key2_id;
};

struct tegra_aes_reqctx {
	struct tegra_se_datbuf datbuf;
	bool encrypt;
	u32 config;
	u32 crypto_config;
	u32 len;
	u32 *iv;
};

struct tegra_aead_ctx {
	struct tegra_se *se;
	unsigned int authsize;
	u32 alg;
	u32 keylen;
	u32 key_id;
};

struct tegra_aead_reqctx {
	struct tegra_se_datbuf inbuf;
	struct tegra_se_datbuf outbuf;
	struct scatterlist *src_sg;
	struct scatterlist *dst_sg;
	unsigned int assoclen;
	unsigned int cryptlen;
	unsigned int authsize;
	bool encrypt;
	u32 config;
	u32 crypto_config;
	u32 key_id;
	u32 iv[4];
	u8 authdata[16];
};

struct tegra_cmac_ctx {
	struct tegra_se *se;
	unsigned int alg;
	u32 key_id;
	struct crypto_shash *fallback_tfm;
};

struct tegra_cmac_reqctx {
	struct scatterlist *src_sg;
	struct tegra_se_datbuf datbuf;
	struct tegra_se_datbuf residue;
	unsigned int total_len;
	unsigned int blk_size;
	unsigned int task;
	u32 crypto_config;
	u32 config;
	u32 key_id;
	u32 *iv;
	u32 result[CMAC_RESULT_REG_COUNT];
};

/* increment counter (128-bit int) */
static void ctr_iv_inc(__u8 *counter, __u8 bits, __u32 nums)
{
	do {
		--bits;
		nums += counter[bits];
		counter[bits] = nums & 0xff;
		nums >>= 8;
	} while (bits && nums);
}

static void tegra_cbc_iv_copyback(struct skcipher_request *req, struct tegra_aes_ctx *ctx)
{
	struct tegra_aes_reqctx *rctx = skcipher_request_ctx(req);
	unsigned int offset;

	offset = req->cryptlen - ctx->ivsize;

	if (rctx->encrypt)
		memcpy(req->iv, rctx->datbuf.buf + offset, ctx->ivsize);
	else
		scatterwalk_map_and_copy(req->iv, req->src, offset, ctx->ivsize, 0);
}

static void tegra_aes_update_iv(struct skcipher_request *req, struct tegra_aes_ctx *ctx)
{
	int num;

	if (ctx->alg == SE_ALG_CBC) {
		tegra_cbc_iv_copyback(req, ctx);
	} else if (ctx->alg == SE_ALG_CTR) {
		num = req->cryptlen / ctx->ivsize;
		if (req->cryptlen % ctx->ivsize)
			num++;

		ctr_iv_inc(req->iv, ctx->ivsize, num);
	}
}

static int tegra234_aes_crypto_cfg(u32 alg, bool encrypt)
{
	switch (alg) {
	case SE_ALG_CMAC:
	case SE_ALG_GMAC:
	case SE_ALG_GCM:
	case SE_ALG_GCM_FINAL:
		return 0;
	case SE_ALG_CBC:
		if (encrypt)
			return SE_CRYPTO_CFG_CBC_ENCRYPT;
		else
			return SE_CRYPTO_CFG_CBC_DECRYPT;
	case SE_ALG_ECB:
		if (encrypt)
			return SE_CRYPTO_CFG_ECB_ENCRYPT;
		else
			return SE_CRYPTO_CFG_ECB_DECRYPT;
	case SE_ALG_XTS:
		if (encrypt)
			return SE_CRYPTO_CFG_XTS_ENCRYPT;
		else
			return SE_CRYPTO_CFG_XTS_DECRYPT;

	case SE_ALG_CTR:
		return SE_CRYPTO_CFG_CTR;
	case SE_ALG_CBC_MAC:
		return SE_CRYPTO_CFG_CBC_MAC;

	default:
		break;
	}

	return -EINVAL;
}

static int tegra234_aes_cfg(u32 alg, bool encrypt)
{
	switch (alg) {
	case SE_ALG_CBC:
	case SE_ALG_ECB:
	case SE_ALG_XTS:
	case SE_ALG_CTR:
		if (encrypt)
			return SE_CFG_AES_ENCRYPT;
		else
			return SE_CFG_AES_DECRYPT;

	case SE_ALG_GMAC:
		if (encrypt)
			return SE_CFG_GMAC_ENCRYPT;
		else
			return SE_CFG_GMAC_DECRYPT;

	case SE_ALG_GCM:
		if (encrypt)
			return SE_CFG_GCM_ENCRYPT;
		else
			return SE_CFG_GCM_DECRYPT;

	case SE_ALG_GCM_FINAL:
		if (encrypt)
			return SE_CFG_GCM_FINAL_ENCRYPT;
		else
			return SE_CFG_GCM_FINAL_DECRYPT;

	case SE_ALG_CMAC:
		return SE_CFG_CMAC;

	case SE_ALG_CBC_MAC:
		return SE_AES_ENC_ALG_AES_ENC |
		       SE_AES_DST_HASH_REG;
	}
	return -EINVAL;
}

static unsigned int tegra_aes_prep_cmd(struct tegra_aes_ctx *ctx,
				       struct tegra_aes_reqctx *rctx)
{
	unsigned int data_count, res_bits, i = 0, j;
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	dma_addr_t addr = rctx->datbuf.addr;

	data_count = rctx->len / AES_BLOCK_SIZE;
	res_bits = (rctx->len % AES_BLOCK_SIZE) * 8;

	/*
	 * Hardware processes data_count + 1 blocks.
	 * Reduce 1 block if there is no residue
	 */
	if (!res_bits)
		data_count--;

	if (rctx->iv) {
		cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
		for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
			cpuvaddr[i++] = rctx->iv[j];
	}

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source address setting */
	cpuvaddr[i++] = lower_32_bits(addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(addr)) | SE_ADDR_HI_SZ(rctx->len);

	/* Destination address setting */
	cpuvaddr[i++] = lower_32_bits(addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(addr)) |
			SE_ADDR_HI_SZ(rctx->len);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_aes_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = container_of(areq, struct skcipher_request, base);
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct tegra_aes_reqctx *rctx = skcipher_request_ctx(req);
	struct tegra_se *se = ctx->se;
	unsigned int cmdlen;
	int ret;

	rctx->datbuf.buf = dma_alloc_coherent(se->dev, SE_AES_BUFLEN,
					      &rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf)
		return -ENOMEM;

	rctx->datbuf.size = SE_AES_BUFLEN;
	rctx->iv = (u32 *)req->iv;
	rctx->len = req->cryptlen;

	/* Pad input to AES Block size */
	if (ctx->alg != SE_ALG_XTS) {
		if (rctx->len % AES_BLOCK_SIZE)
			rctx->len += AES_BLOCK_SIZE - (rctx->len % AES_BLOCK_SIZE);
	}

	scatterwalk_map_and_copy(rctx->datbuf.buf, req->src, 0, req->cryptlen, 0);

	/* Prepare the command and submit for execution */
	cmdlen = tegra_aes_prep_cmd(ctx, rctx);
	ret = tegra_se_host1x_submit(se, cmdlen);

	/* Copy the result */
	tegra_aes_update_iv(req, ctx);
	scatterwalk_map_and_copy(rctx->datbuf.buf, req->dst, 0, req->cryptlen, 1);

	/* Free the buffer */
	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->datbuf.buf, rctx->datbuf.addr);

	crypto_finalize_skcipher_request(se->engine, req, ret);

	return 0;
}

static int tegra_aes_cra_init(struct crypto_skcipher *tfm)
{
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct tegra_se_alg *se_alg;
	const char *algname;
	int ret;

	se_alg = container_of(alg, struct tegra_se_alg, alg.skcipher.base);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct tegra_aes_reqctx));

	ctx->ivsize = crypto_skcipher_ivsize(tfm);
	ctx->se = se_alg->se_dev;
	ctx->key1_id = 0;
	ctx->key2_id = 0;

	algname = crypto_tfm_alg_name(&tfm->base);
	ret = se_algname_to_algid(algname);
	if (ret < 0) {
		dev_err(ctx->se->dev, "invalid algorithm\n");
		return ret;
	}

	ctx->alg = ret;

	return 0;
}

static void tegra_aes_cra_exit(struct crypto_skcipher *tfm)
{
	struct tegra_aes_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->key1_id)
		tegra_key_invalidate(ctx->se, ctx->key1_id, ctx->alg);

	if (ctx->key2_id)
		tegra_key_invalidate(ctx->se, ctx->key2_id, ctx->alg);
}

static int tegra_aes_setkey(struct crypto_skcipher *tfm,
			    const u8 *key, u32 keylen)
{
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_dbg(ctx->se->dev, "invalid key length (%d)\n", keylen);
		return -EINVAL;
	}

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key1_id);
}

static int tegra_xts_setkey(struct crypto_skcipher *tfm,
			    const u8 *key, u32 keylen)
{
	struct tegra_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 len = keylen / 2;
	int ret;

	ret = xts_verify_key(tfm, key, keylen);
	if (ret || aes_check_keylen(len)) {
		dev_dbg(ctx->se->dev, "invalid key length (%d)\n", keylen);
		return -EINVAL;
	}

	ret = tegra_key_submit(ctx->se, key, len,
			       ctx->alg, &ctx->key1_id);
	if (ret)
		return ret;

	return tegra_key_submit(ctx->se, key + len, len,
			       ctx->alg, &ctx->key2_id);

	return 0;
}

static int tegra_aes_kac_manifest(u32 user, u32 alg, u32 keylen)
{
	int manifest;

	manifest = SE_KAC_USER_NS;

	switch (alg) {
	case SE_ALG_CBC:
	case SE_ALG_ECB:
	case SE_ALG_CTR:
		manifest |= SE_KAC_ENC;
		break;
	case SE_ALG_XTS:
		manifest |= SE_KAC_XTS;
		break;
	case SE_ALG_GCM:
		manifest |= SE_KAC_GCM;
		break;
	case SE_ALG_CMAC:
		manifest |= SE_KAC_CMAC;
		break;
	case SE_ALG_CBC_MAC:
		manifest |= SE_KAC_ENC;
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
		manifest |= SE_KAC_SIZE_256;
		break;
	default:
		return -EINVAL;
	}

	return manifest;
}

static int tegra_aes_crypt(struct skcipher_request *req, bool encrypt)

{
	struct crypto_skcipher *tfm;
	struct tegra_aes_ctx *ctx;
	struct tegra_aes_reqctx *rctx;

	tfm = crypto_skcipher_reqtfm(req);
	ctx  = crypto_skcipher_ctx(tfm);
	rctx = skcipher_request_ctx(req);

	if (ctx->alg != SE_ALG_XTS) {
		if (!IS_ALIGNED(req->cryptlen, crypto_skcipher_blocksize(tfm))) {
			dev_dbg(ctx->se->dev, "invalid length (%d)", req->cryptlen);
			return -EINVAL;
		}
	} else if (req->cryptlen < XTS_BLOCK_SIZE) {
		dev_dbg(ctx->se->dev, "invalid length (%d)", req->cryptlen);
		return -EINVAL;
	}

	if (!req->cryptlen)
		return 0;

	rctx->encrypt = encrypt;
	rctx->config = tegra234_aes_cfg(ctx->alg, encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(ctx->alg, encrypt);
	rctx->crypto_config |= SE_AES_KEY_INDEX(ctx->key1_id);

	if (ctx->key2_id)
		rctx->crypto_config |= SE_AES_KEY2_INDEX(ctx->key2_id);

	return crypto_transfer_skcipher_request_to_engine(ctx->se->engine, req);
}

static int tegra_aes_encrypt(struct skcipher_request *req)
{
	return tegra_aes_crypt(req, true);
}

static int tegra_aes_decrypt(struct skcipher_request *req)
{
	return tegra_aes_crypt(req, false);
}

static struct tegra_se_alg tegra_aes_algs[] = {
	{
		.alg.skcipher.op.do_one_request	= tegra_aes_do_one_req,
		.alg.skcipher.base = {
			.init = tegra_aes_cra_init,
			.exit = tegra_aes_cra_exit,
			.setkey	= tegra_aes_setkey,
			.encrypt = tegra_aes_encrypt,
			.decrypt = tegra_aes_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize	= AES_BLOCK_SIZE,
			.base = {
				.cra_name = "cbc(aes)",
				.cra_driver_name = "cbc-aes-tegra",
				.cra_priority = 500,
				.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_ASYNC,
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_aes_ctx),
				.cra_alignmask = 0xf,
				.cra_module = THIS_MODULE,
			},
		}
	}, {
		.alg.skcipher.op.do_one_request	= tegra_aes_do_one_req,
		.alg.skcipher.base = {
			.init = tegra_aes_cra_init,
			.exit = tegra_aes_cra_exit,
			.setkey	= tegra_aes_setkey,
			.encrypt = tegra_aes_encrypt,
			.decrypt = tegra_aes_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.base = {
				.cra_name = "ecb(aes)",
				.cra_driver_name = "ecb-aes-tegra",
				.cra_priority = 500,
				.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_ASYNC,
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_aes_ctx),
				.cra_alignmask = 0xf,
				.cra_module = THIS_MODULE,
			},
		}
	}, {
		.alg.skcipher.op.do_one_request	= tegra_aes_do_one_req,
		.alg.skcipher.base = {
			.init = tegra_aes_cra_init,
			.exit = tegra_aes_cra_exit,
			.setkey = tegra_aes_setkey,
			.encrypt = tegra_aes_encrypt,
			.decrypt = tegra_aes_decrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize	= AES_BLOCK_SIZE,
			.base = {
				.cra_name = "ctr(aes)",
				.cra_driver_name = "ctr-aes-tegra",
				.cra_priority = 500,
				.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER | CRYPTO_ALG_ASYNC,
				.cra_blocksize = 1,
				.cra_ctxsize = sizeof(struct tegra_aes_ctx),
				.cra_alignmask = 0xf,
				.cra_module = THIS_MODULE,
			},
		}
	}, {
		.alg.skcipher.op.do_one_request	= tegra_aes_do_one_req,
		.alg.skcipher.base = {
			.init = tegra_aes_cra_init,
			.exit = tegra_aes_cra_exit,
			.setkey	= tegra_xts_setkey,
			.encrypt = tegra_aes_encrypt,
			.decrypt = tegra_aes_decrypt,
			.min_keysize = 2 * AES_MIN_KEY_SIZE,
			.max_keysize = 2 * AES_MAX_KEY_SIZE,
			.ivsize	= AES_BLOCK_SIZE,
			.base = {
				.cra_name = "xts(aes)",
				.cra_driver_name = "xts-aes-tegra",
				.cra_priority = 500,
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_ctxsize	   = sizeof(struct tegra_aes_ctx),
				.cra_alignmask	   = (__alignof__(u64) - 1),
				.cra_module	   = THIS_MODULE,
			},
		}
	},
};

static unsigned int tegra_gmac_prep_cmd(struct tegra_aead_ctx *ctx,
					struct tegra_aead_reqctx *rctx)
{
	unsigned int data_count, res_bits, i = 0;
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;

	data_count = (rctx->assoclen / AES_BLOCK_SIZE);
	res_bits = (rctx->assoclen % AES_BLOCK_SIZE) * 8;

	/*
	 * Hardware processes data_count + 1 blocks.
	 * Reduce 1 block if there is no residue
	 */
	if (!res_bits)
		data_count--;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 4);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->assoclen);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
			SE_AES_OP_INIT | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	return i;
}

static unsigned int tegra_gcm_crypt_prep_cmd(struct tegra_aead_ctx *ctx,
					     struct tegra_aead_reqctx *rctx)
{
	unsigned int data_count, res_bits, i = 0, j;
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr, op;

	data_count = (rctx->cryptlen / AES_BLOCK_SIZE);
	res_bits = (rctx->cryptlen % AES_BLOCK_SIZE) * 8;
	op = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
	     SE_AES_OP_LASTBUF | SE_AES_OP_START;

	/*
	 * If there is no assoc data,
	 * this will be the init command
	 */
	if (!rctx->assoclen)
		op |= SE_AES_OP_INIT;

	/*
	 * Hardware processes data_count + 1 blocks.
	 * Reduce 1 block if there is no residue
	 */
	if (!res_bits)
		data_count--;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source Address */
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->cryptlen);

	/* Destination Address */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->cryptlen);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);
	return i;
}

static int tegra_gcm_prep_final_cmd(struct tegra_se *se, u32 *cpuvaddr,
				    struct tegra_aead_reqctx *rctx)
{
	unsigned int i = 0, j;
	u32 op;

	op = SE_AES_OP_WRSTALL | SE_AES_OP_FINAL |
	     SE_AES_OP_LASTBUF | SE_AES_OP_START;

	/*
	 * Set init for zero sized vector
	 */
	if (!rctx->assoclen && !rctx->cryptlen)
		op |= SE_AES_OP_INIT;

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->aad_len, 2);
	cpuvaddr[i++] = rctx->assoclen * 8;
	cpuvaddr[i++] = 0;

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->cryp_msg_len, 2);
	cpuvaddr[i++] = rctx->cryptlen * 8;
	cpuvaddr[i++] = 0;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = 0;

	/* Destination Address */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(0x10); /* HW always generates 128-bit tag */

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n", rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_gcm_do_gmac(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	unsigned int cmdlen;

	scatterwalk_map_and_copy(rctx->inbuf.buf,
				 rctx->src_sg, 0, rctx->assoclen, 0);

	rctx->config = tegra234_aes_cfg(SE_ALG_GMAC, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_GMAC, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	cmdlen = tegra_gmac_prep_cmd(ctx, rctx);

	return tegra_se_host1x_submit(se, cmdlen);
}

static int tegra_gcm_do_crypt(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	int cmdlen, ret;

	scatterwalk_map_and_copy(rctx->inbuf.buf, rctx->src_sg,
				 rctx->assoclen, rctx->cryptlen, 0);

	rctx->config = tegra234_aes_cfg(SE_ALG_GCM, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_GCM, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	/* Prepare command and submit */
	cmdlen = tegra_gcm_crypt_prep_cmd(ctx, rctx);
	ret = tegra_se_host1x_submit(se, cmdlen);
	if (ret)
		return ret;

	/* Copy the result */
	scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
				 rctx->assoclen, rctx->cryptlen, 1);

	return 0;
}

static int tegra_gcm_do_final(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;
	int cmdlen, ret, offset;

	rctx->config = tegra234_aes_cfg(SE_ALG_GCM_FINAL, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_GCM_FINAL, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	/* Prepare command and submit */
	cmdlen = tegra_gcm_prep_final_cmd(se, cpuvaddr, rctx);
	ret = tegra_se_host1x_submit(se, cmdlen);
	if (ret)
		return ret;

	if (rctx->encrypt) {
		/* Copy the result */
		offset = rctx->assoclen + rctx->cryptlen;
		scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
					 offset, rctx->authsize, 1);
	}

	return 0;
}

static int tegra_gcm_do_verify(struct tegra_se *se, struct tegra_aead_reqctx *rctx)
{
	unsigned int offset;
	u8 mac[16];

	offset = rctx->assoclen + rctx->cryptlen;
	scatterwalk_map_and_copy(mac, rctx->src_sg, offset, rctx->authsize, 0);

	if (crypto_memneq(rctx->outbuf.buf, mac, rctx->authsize))
		return -EBADMSG;

	return 0;
}

static inline int tegra_ccm_check_iv(const u8 *iv)
{
	/* iv[0] gives value of q-1
	 * 2 <= q <= 8 as per NIST 800-38C notation
	 * 2 <= L <= 8, so 1 <= L' <= 7. as per rfc 3610 notation
	 */
	if (iv[0] < 1 || iv[0] > 7) {
		pr_debug("ccm_check_iv failed %d\n", iv[0]);
		return -EINVAL;
	}

	return 0;
}

static unsigned int tegra_cbcmac_prep_cmd(struct tegra_aead_ctx *ctx,
					  struct tegra_aead_reqctx *rctx)
{
	unsigned int data_count, i = 0;
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;

	data_count = (rctx->inbuf.size / AES_BLOCK_SIZE) - 1;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->inbuf.size);

	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(0x10); /* HW always generates 128 bit tag */

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL |
			SE_AES_OP_LASTBUF | SE_AES_OP_START;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	return i;
}

static unsigned int tegra_ctr_prep_cmd(struct tegra_aead_ctx *ctx,
				       struct tegra_aead_reqctx *rctx)
{
	unsigned int i = 0, j;
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr;

	cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
	cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
	for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
		cpuvaddr[i++] = rctx->iv[j];

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = (rctx->inbuf.size / AES_BLOCK_SIZE) - 1;
	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source address setting */
	cpuvaddr[i++] = lower_32_bits(rctx->inbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->inbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->inbuf.size);

	/* Destination address setting */
	cpuvaddr[i++] = lower_32_bits(rctx->outbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->outbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->inbuf.size);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = SE_AES_OP_WRSTALL | SE_AES_OP_LASTBUF |
			SE_AES_OP_START;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	dev_dbg(se->dev, "cfg %#x crypto cfg %#x\n",
		rctx->config, rctx->crypto_config);

	return i;
}

static int tegra_ccm_do_cbcmac(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	int cmdlen;

	rctx->config = tegra234_aes_cfg(SE_ALG_CBC_MAC, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_CBC_MAC,
						      rctx->encrypt) |
						      SE_AES_KEY_INDEX(ctx->key_id);

	/* Prepare command and submit */
	cmdlen = tegra_cbcmac_prep_cmd(ctx, rctx);

	return tegra_se_host1x_submit(se, cmdlen);
}

static int tegra_ccm_set_msg_len(u8 *block, unsigned int msglen, int csize)
{
	__be32 data;

	memset(block, 0, csize);
	block += csize;

	if (csize >= 4)
		csize = 4;
	else if (msglen > (1 << (8 * csize)))
		return -EOVERFLOW;

	data = cpu_to_be32(msglen);
	memcpy(block - csize, (u8 *)&data + 4 - csize, csize);

	return 0;
}

static int tegra_ccm_format_nonce(struct tegra_aead_reqctx *rctx, u8 *nonce)
{
	unsigned int q, t;
	u8 *q_ptr, *iv = (u8 *)rctx->iv;

	memcpy(nonce, rctx->iv, 16);

	/*** 1. Prepare Flags Octet ***/

	/* Encode t (mac length) */
	t = rctx->authsize;
	nonce[0] |= (((t - 2) / 2) << 3);

	/* Adata */
	if (rctx->assoclen)
		nonce[0] |= (1 << 6);

	/*** Encode Q - message length ***/
	q = iv[0] + 1;
	q_ptr = nonce + 16 - q;

	return tegra_ccm_set_msg_len(q_ptr, rctx->cryptlen, q);
}

static int tegra_ccm_format_adata(u8 *adata, unsigned int a)
{
	int len = 0;

	/* add control info for associated data
	 * RFC 3610 and NIST Special Publication 800-38C
	 */
	if (a < 65280) {
		*(__be16 *)adata = cpu_to_be16(a);
		len = 2;
	} else	{
		*(__be16 *)adata = cpu_to_be16(0xfffe);
		*(__be32 *)&adata[2] = cpu_to_be32(a);
		len = 6;
	}

	return len;
}

static int tegra_ccm_add_padding(u8 *buf, unsigned int len)
{
	unsigned int padlen = 16 - (len % 16);
	u8 padding[16] = {0};

	if (padlen == 16)
		return 0;

	memcpy(buf, padding, padlen);

	return padlen;
}

static int tegra_ccm_format_blocks(struct tegra_aead_reqctx *rctx)
{
	unsigned int alen = 0, offset = 0;
	u8 nonce[16], adata[16];
	int ret;

	ret = tegra_ccm_format_nonce(rctx, nonce);
	if (ret)
		return ret;

	memcpy(rctx->inbuf.buf, nonce, 16);
	offset = 16;

	if (rctx->assoclen) {
		alen = tegra_ccm_format_adata(adata, rctx->assoclen);
		memcpy(rctx->inbuf.buf + offset, adata, alen);
		offset += alen;

		scatterwalk_map_and_copy(rctx->inbuf.buf + offset,
					 rctx->src_sg, 0, rctx->assoclen, 0);

		offset += rctx->assoclen;
		offset += tegra_ccm_add_padding(rctx->inbuf.buf + offset,
					 rctx->assoclen + alen);
	}

	return offset;
}

static int tegra_ccm_mac_result(struct tegra_se *se, struct tegra_aead_reqctx *rctx)
{
	u32 result[16];
	int i, ret;

	/* Read and clear Result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		result[i] = readl(se->base + se->hw->regs->result + (i * 4));

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(0, se->base + se->hw->regs->result + (i * 4));

	if (rctx->encrypt) {
		memcpy(rctx->authdata, result, rctx->authsize);
	} else {
		ret = crypto_memneq(rctx->authdata, result, rctx->authsize);
		if (ret)
			return -EBADMSG;
	}

	return 0;
}

static int tegra_ccm_ctr_result(struct tegra_se *se, struct tegra_aead_reqctx *rctx)
{
	/* Copy result */
	scatterwalk_map_and_copy(rctx->outbuf.buf + 16, rctx->dst_sg,
				 rctx->assoclen, rctx->cryptlen, 1);

	if (rctx->encrypt)
		scatterwalk_map_and_copy(rctx->outbuf.buf, rctx->dst_sg,
					 rctx->assoclen + rctx->cryptlen,
					 rctx->authsize, 1);
	else
		memcpy(rctx->authdata, rctx->outbuf.buf, rctx->authsize);

	return 0;
}

static int tegra_ccm_compute_auth(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	struct scatterlist *sg;
	int offset, ret;

	offset = tegra_ccm_format_blocks(rctx);
	if (offset < 0)
		return -EINVAL;

	/* Copy plain text to the buffer */
	sg = rctx->encrypt ? rctx->src_sg : rctx->dst_sg;

	scatterwalk_map_and_copy(rctx->inbuf.buf + offset,
				 sg, rctx->assoclen,
				 rctx->cryptlen, 0);
	offset += rctx->cryptlen;
	offset += tegra_ccm_add_padding(rctx->inbuf.buf + offset, rctx->cryptlen);

	rctx->inbuf.size = offset;

	ret = tegra_ccm_do_cbcmac(ctx, rctx);
	if (ret)
		return ret;

	return tegra_ccm_mac_result(se, rctx);
}

static int tegra_ccm_do_ctr(struct tegra_aead_ctx *ctx, struct tegra_aead_reqctx *rctx)
{
	struct tegra_se *se = ctx->se;
	unsigned int cmdlen, offset = 0;
	struct scatterlist *sg = rctx->src_sg;
	int ret;

	rctx->config = tegra234_aes_cfg(SE_ALG_CTR, rctx->encrypt);
	rctx->crypto_config = tegra234_aes_crypto_cfg(SE_ALG_CTR, rctx->encrypt) |
			      SE_AES_KEY_INDEX(ctx->key_id);

	/* Copy authdata in the top of buffer for encryption/decryption */
	if (rctx->encrypt)
		memcpy(rctx->inbuf.buf, rctx->authdata, rctx->authsize);
	else
		scatterwalk_map_and_copy(rctx->inbuf.buf, sg,
					 rctx->assoclen + rctx->cryptlen,
					 rctx->authsize, 0);

	offset += rctx->authsize;
	offset += tegra_ccm_add_padding(rctx->inbuf.buf + offset, rctx->authsize);

	/* If there is no cryptlen, proceed to submit the task */
	if (rctx->cryptlen) {
		scatterwalk_map_and_copy(rctx->inbuf.buf + offset, sg,
					 rctx->assoclen, rctx->cryptlen, 0);
		offset += rctx->cryptlen;
		offset += tegra_ccm_add_padding(rctx->inbuf.buf + offset, rctx->cryptlen);
	}

	rctx->inbuf.size = offset;

	/* Prepare command and submit */
	cmdlen = tegra_ctr_prep_cmd(ctx, rctx);
	ret = tegra_se_host1x_submit(se, cmdlen);
	if (ret)
		return ret;

	return tegra_ccm_ctr_result(se, rctx);
}

static int tegra_ccm_crypt_init(struct aead_request *req, struct tegra_se *se,
				struct tegra_aead_reqctx *rctx)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	u8 *iv = (u8 *)rctx->iv;
	int ret, i;

	rctx->src_sg = req->src;
	rctx->dst_sg = req->dst;
	rctx->assoclen = req->assoclen;
	rctx->authsize = crypto_aead_authsize(tfm);

	memcpy(iv, req->iv, 16);

	ret = tegra_ccm_check_iv(iv);
	if (ret)
		return ret;

	/* Note: rfc 3610 and NIST 800-38C require counter (ctr_0) of
	 * zero to encrypt auth tag.
	 * req->iv has the formatted ctr_0 (i.e. Flags || N || 0).
	 */
	memset(iv + 15 - iv[0], 0, iv[0] + 1);

	/* Clear any previous result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(0, se->base + se->hw->regs->result + (i * 4));

	return 0;
}

static int tegra_ccm_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret;

	/* Allocate buffers required */
	rctx->inbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					     &rctx->inbuf.addr, GFP_KERNEL);
	if (!rctx->inbuf.buf)
		return -ENOMEM;

	rctx->inbuf.size = SE_AES_BUFLEN;

	rctx->outbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					      &rctx->outbuf.addr, GFP_KERNEL);
	if (!rctx->outbuf.buf) {
		ret = -ENOMEM;
		goto outbuf_err;
	}

	rctx->outbuf.size = SE_AES_BUFLEN;

	ret = tegra_ccm_crypt_init(req, se, rctx);
	if (ret)
		goto out;

	if (rctx->encrypt) {
		rctx->cryptlen = req->cryptlen;

		/* CBC MAC Operation */
		ret = tegra_ccm_compute_auth(ctx, rctx);
		if (ret)
			goto out;

		/* CTR operation */
		ret = tegra_ccm_do_ctr(ctx, rctx);
		if (ret)
			goto out;
	} else {
		rctx->cryptlen = req->cryptlen - ctx->authsize;
		if (ret)
			goto out;

		/* CTR operation */
		ret = tegra_ccm_do_ctr(ctx, rctx);
		if (ret)
			goto out;

		/* CBC MAC Operation */
		ret = tegra_ccm_compute_auth(ctx, rctx);
		if (ret)
			goto out;
	}

out:
	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->outbuf.buf, rctx->outbuf.addr);

outbuf_err:
	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->inbuf.buf, rctx->inbuf.addr);

	crypto_finalize_aead_request(ctx->se->engine, req, ret);

	return 0;
}

static int tegra_gcm_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request, base);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);
	int ret;

	/* Allocate buffers required */
	rctx->inbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					     &rctx->inbuf.addr, GFP_KERNEL);
	if (!rctx->inbuf.buf)
		return -ENOMEM;

	rctx->inbuf.size = SE_AES_BUFLEN;

	rctx->outbuf.buf = dma_alloc_coherent(ctx->se->dev, SE_AES_BUFLEN,
					      &rctx->outbuf.addr, GFP_KERNEL);
	if (!rctx->outbuf.buf) {
		ret = -ENOMEM;
		goto outbuf_err;
	}

	rctx->outbuf.size = SE_AES_BUFLEN;

	rctx->src_sg = req->src;
	rctx->dst_sg = req->dst;
	rctx->assoclen = req->assoclen;
	rctx->authsize = crypto_aead_authsize(tfm);

	if (rctx->encrypt)
		rctx->cryptlen = req->cryptlen;
	else
		rctx->cryptlen = req->cryptlen - ctx->authsize;

	memcpy(rctx->iv, req->iv, GCM_AES_IV_SIZE);
	rctx->iv[3] = (1 << 24);

	/* If there is associated data perform GMAC operation */
	if (rctx->assoclen) {
		ret = tegra_gcm_do_gmac(ctx, rctx);
		if (ret)
			goto out;
	}

	/* GCM Encryption/Decryption operation */
	if (rctx->cryptlen) {
		ret = tegra_gcm_do_crypt(ctx, rctx);
		if (ret)
			goto out;
	}

	/* GCM_FINAL operation */
	ret = tegra_gcm_do_final(ctx, rctx);
	if (ret)
		goto out;

	if (!rctx->encrypt)
		ret = tegra_gcm_do_verify(ctx->se, rctx);

out:
	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->outbuf.buf, rctx->outbuf.addr);

outbuf_err:
	dma_free_coherent(ctx->se->dev, SE_AES_BUFLEN,
			  rctx->inbuf.buf, rctx->inbuf.addr);

	/* Finalize the request if there are no errors */
	crypto_finalize_aead_request(ctx->se->engine, req, ret);

	return 0;
}

static int tegra_aead_cra_init(struct crypto_aead *tfm)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct tegra_se_alg *se_alg;
	const char *algname;
	int ret;

	algname = crypto_tfm_alg_name(&tfm->base);

	se_alg = container_of(alg, struct tegra_se_alg, alg.aead.base);

	crypto_aead_set_reqsize(tfm, sizeof(struct tegra_aead_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;

	ret = se_algname_to_algid(algname);
	if (ret < 0) {
		dev_err(ctx->se->dev, "invalid algorithm\n");
		return ret;
	}

	ctx->alg = ret;

	return 0;
}

static int tegra_ccm_setauthsize(struct crypto_aead *tfm,  unsigned int authsize)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);

	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	ctx->authsize = authsize;

	return 0;
}

static int tegra_gcm_setauthsize(struct crypto_aead *tfm,  unsigned int authsize)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	ret = crypto_gcm_check_authsize(authsize);
	if (ret)
		return ret;

	ctx->authsize = authsize;

	return 0;
}

static void tegra_aead_cra_exit(struct crypto_aead *tfm)
{
	struct tegra_aead_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->key_id)
		tegra_key_invalidate(ctx->se, ctx->key_id, ctx->alg);
}

static int tegra_aead_crypt(struct aead_request *req, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct tegra_aead_reqctx *rctx = aead_request_ctx(req);

	rctx->encrypt = encrypt;

	return crypto_transfer_aead_request_to_engine(ctx->se->engine, req);
}

static int tegra_aead_encrypt(struct aead_request *req)
{
	return tegra_aead_crypt(req, true);
}

static int tegra_aead_decrypt(struct aead_request *req)
{
	return tegra_aead_crypt(req, false);
}

static int tegra_aead_setkey(struct crypto_aead *tfm,
			     const u8 *key, u32 keylen)
{
	struct tegra_aead_ctx *ctx = crypto_aead_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_dbg(ctx->se->dev, "invalid key length (%d)\n", keylen);
		return -EINVAL;
	}

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
}

static unsigned int tegra_cmac_prep_cmd(struct tegra_cmac_ctx *ctx,
					struct tegra_cmac_reqctx *rctx)
{
	unsigned int data_count, res_bits = 0, i = 0, j;
	struct tegra_se *se = ctx->se;
	u32 *cpuvaddr = se->cmdbuf->addr, op;

	data_count = (rctx->datbuf.size / AES_BLOCK_SIZE);

	op = SE_AES_OP_WRSTALL | SE_AES_OP_START | SE_AES_OP_LASTBUF;

	if (!(rctx->task & SHA_UPDATE)) {
		op |= SE_AES_OP_FINAL;
		res_bits = (rctx->datbuf.size % AES_BLOCK_SIZE) * 8;
	}

	if (!res_bits && data_count)
		data_count--;

	if (rctx->task & SHA_FIRST) {
		rctx->task &= ~SHA_FIRST;

		cpuvaddr[i++] = host1x_opcode_setpayload(SE_CRYPTO_CTR_REG_COUNT);
		cpuvaddr[i++] = se_host1x_opcode_incr_w(se->hw->regs->linear_ctr);
		/* Load 0 IV */
		for (j = 0; j < SE_CRYPTO_CTR_REG_COUNT; j++)
			cpuvaddr[i++] = 0;
	}

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->last_blk, 1);
	cpuvaddr[i++] = SE_LAST_BLOCK_VAL(data_count) |
			SE_LAST_BLOCK_RES_BITS(res_bits);

	cpuvaddr[i++] = se_host1x_opcode_incr(se->hw->regs->config, 6);
	cpuvaddr[i++] = rctx->config;
	cpuvaddr[i++] = rctx->crypto_config;

	/* Source Address */
	cpuvaddr[i++] = lower_32_bits(rctx->datbuf.addr);
	cpuvaddr[i++] = SE_ADDR_HI_MSB(upper_32_bits(rctx->datbuf.addr)) |
			SE_ADDR_HI_SZ(rctx->datbuf.size);
	cpuvaddr[i++] = 0;
	cpuvaddr[i++] = SE_ADDR_HI_SZ(AES_BLOCK_SIZE);

	cpuvaddr[i++] = se_host1x_opcode_nonincr(se->hw->regs->op, 1);
	cpuvaddr[i++] = op;

	cpuvaddr[i++] = se_host1x_opcode_nonincr(host1x_uclass_incr_syncpt_r(), 1);
	cpuvaddr[i++] = host1x_uclass_incr_syncpt_cond_f(1) |
			host1x_uclass_incr_syncpt_indx_f(se->syncpt_id);

	return i;
}

static void tegra_cmac_copy_result(struct tegra_se *se, struct tegra_cmac_reqctx *rctx)
{
	int i;

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		rctx->result[i] = readl(se->base + se->hw->regs->result + (i * 4));
}

static void tegra_cmac_paste_result(struct tegra_se *se, struct tegra_cmac_reqctx *rctx)
{
	int i;

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(rctx->result[i],
		       se->base + se->hw->regs->result + (i * 4));
}

static int tegra_cmac_do_update(struct ahash_request *req)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	unsigned int nblks, nresidue, cmdlen;
	int ret;

	if (!req->nbytes)
		return 0;

	nresidue = (req->nbytes + rctx->residue.size) % rctx->blk_size;
	nblks = (req->nbytes + rctx->residue.size) / rctx->blk_size;

	/*
	 * Reserve the last block as residue during final() to process.
	 */
	if (!nresidue && nblks) {
		nresidue += rctx->blk_size;
		nblks--;
	}

	rctx->src_sg = req->src;
	rctx->datbuf.size = (req->nbytes + rctx->residue.size) - nresidue;
	rctx->total_len += rctx->datbuf.size;
	rctx->config = tegra234_aes_cfg(SE_ALG_CMAC, 0);
	rctx->crypto_config = SE_AES_KEY_INDEX(ctx->key_id);

	/*
	 * Keep one block and residue bytes in residue and
	 * return. The bytes will be processed in final()
	 */
	if (nblks < 1) {
		scatterwalk_map_and_copy(rctx->residue.buf + rctx->residue.size,
					 rctx->src_sg, 0, req->nbytes, 0);

		rctx->residue.size += req->nbytes;
		return 0;
	}

	/* Copy the previous residue first */
	if (rctx->residue.size)
		memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);

	scatterwalk_map_and_copy(rctx->datbuf.buf + rctx->residue.size,
				 rctx->src_sg, 0, req->nbytes - nresidue, 0);

	scatterwalk_map_and_copy(rctx->residue.buf, rctx->src_sg,
				 req->nbytes - nresidue, nresidue, 0);

	/* Update residue value with the residue after current block */
	rctx->residue.size = nresidue;

	/*
	 * If this is not the first 'update' call, paste the previous copied
	 * intermediate results to the registers so that it gets picked up.
	 * This is to support the import/export functionality.
	 */
	if (!(rctx->task & SHA_FIRST))
		tegra_cmac_paste_result(ctx->se, rctx);

	cmdlen = tegra_cmac_prep_cmd(ctx, rctx);

	ret = tegra_se_host1x_submit(se, cmdlen);
	/*
	 * If this is not the final update, copy the intermediate results
	 * from the registers so that it can be used in the next 'update'
	 * call. This is to support the import/export functionality.
	 */
	if (!(rctx->task & SHA_FINAL))
		tegra_cmac_copy_result(ctx->se, rctx);

	return ret;
}

static int tegra_cmac_do_final(struct ahash_request *req)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	u32 *result = (u32 *)req->result;
	int ret = 0, i, cmdlen;

	if (!req->nbytes && !rctx->total_len && ctx->fallback_tfm) {
		return crypto_shash_tfm_digest(ctx->fallback_tfm,
					rctx->datbuf.buf, 0, req->result);
	}

	memcpy(rctx->datbuf.buf, rctx->residue.buf, rctx->residue.size);
	rctx->datbuf.size = rctx->residue.size;
	rctx->total_len += rctx->residue.size;
	rctx->config = tegra234_aes_cfg(SE_ALG_CMAC, 0);

	/* Prepare command and submit */
	cmdlen = tegra_cmac_prep_cmd(ctx, rctx);
	ret = tegra_se_host1x_submit(se, cmdlen);
	if (ret)
		goto out;

	/* Read and clear Result register */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		result[i] = readl(se->base + se->hw->regs->result + (i * 4));

	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(0, se->base + se->hw->regs->result + (i * 4));

out:
	dma_free_coherent(se->dev, SE_SHA_BUFLEN,
			  rctx->datbuf.buf, rctx->datbuf.addr);
	dma_free_coherent(se->dev, crypto_ahash_blocksize(tfm) * 2,
			  rctx->residue.buf, rctx->residue.addr);
	return ret;
}

static int tegra_cmac_do_one_req(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int ret;

	if (rctx->task & SHA_UPDATE) {
		ret = tegra_cmac_do_update(req);
		rctx->task &= ~SHA_UPDATE;
	}

	if (rctx->task & SHA_FINAL) {
		ret = tegra_cmac_do_final(req);
		rctx->task &= ~SHA_FINAL;
	}

	crypto_finalize_hash_request(se->engine, req, ret);

	return 0;
}

static void tegra_cmac_init_fallback(struct crypto_ahash *tfm, struct tegra_cmac_ctx *ctx,
				     const char *algname)
{
	unsigned int statesize;

	ctx->fallback_tfm = crypto_alloc_shash(algname, 0, CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(ctx->fallback_tfm)) {
		dev_warn(ctx->se->dev, "failed to allocate fallback for %s\n", algname);
		ctx->fallback_tfm = NULL;
		return;
	}

	statesize = crypto_shash_statesize(ctx->fallback_tfm);

	if (statesize > sizeof(struct tegra_cmac_reqctx))
		crypto_ahash_set_statesize(tfm, statesize);
}

static int tegra_cmac_cra_init(struct crypto_tfm *tfm)
{
	struct tegra_cmac_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_ahash *ahash_tfm = __crypto_ahash_cast(tfm);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct tegra_se_alg *se_alg;
	const char *algname;
	int ret;

	algname = crypto_tfm_alg_name(tfm);
	se_alg = container_of(alg, struct tegra_se_alg, alg.ahash.base);

	crypto_ahash_set_reqsize(ahash_tfm, sizeof(struct tegra_cmac_reqctx));

	ctx->se = se_alg->se_dev;
	ctx->key_id = 0;

	ret = se_algname_to_algid(algname);
	if (ret < 0) {
		dev_err(ctx->se->dev, "invalid algorithm\n");
		return ret;
	}

	ctx->alg = ret;

	tegra_cmac_init_fallback(ahash_tfm, ctx, algname);

	return 0;
}

static void tegra_cmac_cra_exit(struct crypto_tfm *tfm)
{
	struct tegra_cmac_ctx *ctx = crypto_tfm_ctx(tfm);

	if (ctx->fallback_tfm)
		crypto_free_shash(ctx->fallback_tfm);

	tegra_key_invalidate(ctx->se, ctx->key_id, ctx->alg);
}

static int tegra_cmac_init(struct ahash_request *req)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_se *se = ctx->se;
	int i;

	rctx->total_len = 0;
	rctx->datbuf.size = 0;
	rctx->residue.size = 0;
	rctx->task = SHA_FIRST;
	rctx->blk_size = crypto_ahash_blocksize(tfm);

	rctx->residue.buf = dma_alloc_coherent(se->dev, rctx->blk_size * 2,
					       &rctx->residue.addr, GFP_KERNEL);
	if (!rctx->residue.buf)
		goto resbuf_fail;

	rctx->residue.size = 0;

	rctx->datbuf.buf = dma_alloc_coherent(se->dev, SE_SHA_BUFLEN,
					      &rctx->datbuf.addr, GFP_KERNEL);
	if (!rctx->datbuf.buf)
		goto datbuf_fail;

	rctx->datbuf.size = 0;

	/* Clear any previous result */
	for (i = 0; i < CMAC_RESULT_REG_COUNT; i++)
		writel(0, se->base + se->hw->regs->result + (i * 4));

	return 0;

datbuf_fail:
	dma_free_coherent(se->dev, rctx->blk_size, rctx->residue.buf,
			  rctx->residue.addr);
resbuf_fail:
	return -ENOMEM;
}

static int tegra_cmac_setkey(struct crypto_ahash *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);

	if (aes_check_keylen(keylen)) {
		dev_dbg(ctx->se->dev, "invalid key length (%d)\n", keylen);
		return -EINVAL;
	}

	if (ctx->fallback_tfm)
		crypto_shash_setkey(ctx->fallback_tfm, key, keylen);

	return tegra_key_submit(ctx->se, key, keylen, ctx->alg, &ctx->key_id);
}

static int tegra_cmac_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_UPDATE;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	rctx->task |= SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_digest(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct tegra_cmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	tegra_cmac_init(req);
	rctx->task |= SHA_UPDATE | SHA_FINAL;

	return crypto_transfer_hash_request_to_engine(ctx->se->engine, req);
}

static int tegra_cmac_export(struct ahash_request *req, void *out)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	memcpy(out, rctx, sizeof(*rctx));

	return 0;
}

static int tegra_cmac_import(struct ahash_request *req, const void *in)
{
	struct tegra_cmac_reqctx *rctx = ahash_request_ctx(req);

	memcpy(rctx, in, sizeof(*rctx));

	return 0;
}

static struct tegra_se_alg tegra_aead_algs[] = {
	{
		.alg.aead.op.do_one_request = tegra_gcm_do_one_req,
		.alg.aead.base = {
			.init = tegra_aead_cra_init,
			.exit = tegra_aead_cra_exit,
			.setkey = tegra_aead_setkey,
			.setauthsize = tegra_gcm_setauthsize,
			.encrypt = tegra_aead_encrypt,
			.decrypt = tegra_aead_decrypt,
			.maxauthsize = AES_BLOCK_SIZE,
			.ivsize	= GCM_AES_IV_SIZE,
			.base = {
				.cra_name = "gcm(aes)",
				.cra_driver_name = "gcm-aes-tegra",
				.cra_priority = 500,
				.cra_blocksize = 1,
				.cra_ctxsize = sizeof(struct tegra_aead_ctx),
				.cra_alignmask = 0xf,
				.cra_module = THIS_MODULE,
			},
		}
	}, {
		.alg.aead.op.do_one_request = tegra_ccm_do_one_req,
		.alg.aead.base = {
			.init = tegra_aead_cra_init,
			.exit = tegra_aead_cra_exit,
			.setkey	= tegra_aead_setkey,
			.setauthsize = tegra_ccm_setauthsize,
			.encrypt = tegra_aead_encrypt,
			.decrypt = tegra_aead_decrypt,
			.maxauthsize = AES_BLOCK_SIZE,
			.ivsize	= AES_BLOCK_SIZE,
			.chunksize = AES_BLOCK_SIZE,
			.base = {
				.cra_name = "ccm(aes)",
				.cra_driver_name = "ccm-aes-tegra",
				.cra_priority = 500,
				.cra_blocksize = 1,
				.cra_ctxsize = sizeof(struct tegra_aead_ctx),
				.cra_alignmask = 0xf,
				.cra_module = THIS_MODULE,
			},
		}
	}
};

static struct tegra_se_alg tegra_cmac_algs[] = {
	{
		.alg.ahash.op.do_one_request = tegra_cmac_do_one_req,
		.alg.ahash.base = {
			.init = tegra_cmac_init,
			.setkey	= tegra_cmac_setkey,
			.update = tegra_cmac_update,
			.final = tegra_cmac_final,
			.finup = tegra_cmac_finup,
			.digest = tegra_cmac_digest,
			.export = tegra_cmac_export,
			.import = tegra_cmac_import,
			.halg.digestsize = AES_BLOCK_SIZE,
			.halg.statesize = sizeof(struct tegra_cmac_reqctx),
			.halg.base = {
				.cra_name = "cmac(aes)",
				.cra_driver_name = "tegra-se-cmac",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AHASH,
				.cra_blocksize = AES_BLOCK_SIZE,
				.cra_ctxsize = sizeof(struct tegra_cmac_ctx),
				.cra_alignmask = 0,
				.cra_module = THIS_MODULE,
				.cra_init = tegra_cmac_cra_init,
				.cra_exit = tegra_cmac_cra_exit,
			}
		}
	}
};

int tegra_init_aes(struct tegra_se *se)
{
	struct aead_engine_alg *aead_alg;
	struct ahash_engine_alg *ahash_alg;
	struct skcipher_engine_alg *sk_alg;
	int i, ret;

	se->manifest = tegra_aes_kac_manifest;

	for (i = 0; i < ARRAY_SIZE(tegra_aes_algs); i++) {
		sk_alg = &tegra_aes_algs[i].alg.skcipher;
		tegra_aes_algs[i].se_dev = se;

		ret = crypto_engine_register_skcipher(sk_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				sk_alg->base.base.cra_name);
			goto err_aes;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_aead_algs); i++) {
		aead_alg = &tegra_aead_algs[i].alg.aead;
		tegra_aead_algs[i].se_dev = se;

		ret = crypto_engine_register_aead(aead_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				aead_alg->base.base.cra_name);
			goto err_aead;
		}
	}

	for (i = 0; i < ARRAY_SIZE(tegra_cmac_algs); i++) {
		ahash_alg = &tegra_cmac_algs[i].alg.ahash;
		tegra_cmac_algs[i].se_dev = se;

		ret = crypto_engine_register_ahash(ahash_alg);
		if (ret) {
			dev_err(se->dev, "failed to register %s\n",
				ahash_alg->base.halg.base.cra_name);
			goto err_cmac;
		}
	}

	return 0;

err_cmac:
	while (i--)
		crypto_engine_unregister_ahash(&tegra_cmac_algs[i].alg.ahash);

	i = ARRAY_SIZE(tegra_aead_algs);
err_aead:
	while (i--)
		crypto_engine_unregister_aead(&tegra_aead_algs[i].alg.aead);

	i = ARRAY_SIZE(tegra_aes_algs);
err_aes:
	while (i--)
		crypto_engine_unregister_skcipher(&tegra_aes_algs[i].alg.skcipher);

	return ret;
}

void tegra_deinit_aes(struct tegra_se *se)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra_aes_algs); i++)
		crypto_engine_unregister_skcipher(&tegra_aes_algs[i].alg.skcipher);

	for (i = 0; i < ARRAY_SIZE(tegra_aead_algs); i++)
		crypto_engine_unregister_aead(&tegra_aead_algs[i].alg.aead);

	for (i = 0; i < ARRAY_SIZE(tegra_cmac_algs); i++)
		crypto_engine_unregister_ahash(&tegra_cmac_algs[i].alg.ahash);
}
