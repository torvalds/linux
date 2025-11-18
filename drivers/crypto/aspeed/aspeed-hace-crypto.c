// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2021 Aspeed Technology Inc.
 */

#include "aspeed-hace.h"
#include <crypto/des.h>
#include <crypto/engine.h>
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>

#ifdef CONFIG_CRYPTO_DEV_ASPEED_HACE_CRYPTO_DEBUG
#define CIPHER_DBG(h, fmt, ...)	\
	dev_info((h)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#else
#define CIPHER_DBG(h, fmt, ...)	\
	dev_dbg((h)->dev, "%s() " fmt, __func__, ##__VA_ARGS__)
#endif

static int aspeed_crypto_do_fallback(struct skcipher_request *areq)
{
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(areq);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	skcipher_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	skcipher_request_set_callback(&rctx->fallback_req, areq->base.flags,
				      areq->base.complete, areq->base.data);
	skcipher_request_set_crypt(&rctx->fallback_req, areq->src, areq->dst,
				   areq->cryptlen, areq->iv);

	if (rctx->enc_cmd & HACE_CMD_ENCRYPT)
		err = crypto_skcipher_encrypt(&rctx->fallback_req);
	else
		err = crypto_skcipher_decrypt(&rctx->fallback_req);

	return err;
}

static bool aspeed_crypto_need_fallback(struct skcipher_request *areq)
{
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(areq);

	if (areq->cryptlen == 0)
		return true;

	if ((rctx->enc_cmd & HACE_CMD_DES_SELECT) &&
	    !IS_ALIGNED(areq->cryptlen, DES_BLOCK_SIZE))
		return true;

	if ((!(rctx->enc_cmd & HACE_CMD_DES_SELECT)) &&
	    !IS_ALIGNED(areq->cryptlen, AES_BLOCK_SIZE))
		return true;

	return false;
}

static int aspeed_hace_crypto_handle_queue(struct aspeed_hace_dev *hace_dev,
					   struct skcipher_request *req)
{
	if (hace_dev->version == AST2500_VERSION &&
	    aspeed_crypto_need_fallback(req)) {
		CIPHER_DBG(hace_dev, "SW fallback\n");
		return aspeed_crypto_do_fallback(req);
	}

	return crypto_transfer_skcipher_request_to_engine(
			hace_dev->crypt_engine_crypto, req);
}

static int aspeed_crypto_do_request(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	struct aspeed_engine_crypto *crypto_engine;
	int rc;

	crypto_engine = &hace_dev->crypto_engine;
	crypto_engine->req = req;
	crypto_engine->flags |= CRYPTO_FLAGS_BUSY;

	rc = ctx->start(hace_dev);

	if (rc != -EINPROGRESS)
		return -EIO;

	return 0;
}

static int aspeed_sk_complete(struct aspeed_hace_dev *hace_dev, int err)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_cipher_reqctx *rctx;
	struct skcipher_request *req;

	CIPHER_DBG(hace_dev, "\n");

	req = crypto_engine->req;
	rctx = skcipher_request_ctx(req);

	if (rctx->enc_cmd & HACE_CMD_IV_REQUIRE) {
		if (rctx->enc_cmd & HACE_CMD_DES_SELECT)
			memcpy(req->iv, crypto_engine->cipher_ctx +
			       DES_KEY_SIZE, DES_KEY_SIZE);
		else
			memcpy(req->iv, crypto_engine->cipher_ctx,
			       AES_BLOCK_SIZE);
	}

	crypto_engine->flags &= ~CRYPTO_FLAGS_BUSY;

	crypto_finalize_skcipher_request(hace_dev->crypt_engine_crypto, req,
					 err);

	return err;
}

static int aspeed_sk_transfer_sg(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct device *dev = hace_dev->dev;
	struct aspeed_cipher_reqctx *rctx;
	struct skcipher_request *req;

	CIPHER_DBG(hace_dev, "\n");

	req = crypto_engine->req;
	rctx = skcipher_request_ctx(req);

	if (req->src == req->dst) {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(dev, req->src, rctx->src_nents, DMA_TO_DEVICE);
		dma_unmap_sg(dev, req->dst, rctx->dst_nents, DMA_FROM_DEVICE);
	}

	return aspeed_sk_complete(hace_dev, 0);
}

static int aspeed_sk_transfer(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_cipher_reqctx *rctx;
	struct skcipher_request *req;
	struct scatterlist *out_sg;
	int nbytes = 0;
	int rc = 0;

	req = crypto_engine->req;
	rctx = skcipher_request_ctx(req);
	out_sg = req->dst;

	/* Copy output buffer to dst scatter-gather lists */
	nbytes = sg_copy_from_buffer(out_sg, rctx->dst_nents,
				     crypto_engine->cipher_addr, req->cryptlen);
	if (!nbytes) {
		dev_warn(hace_dev->dev, "invalid sg copy, %s:0x%x, %s:0x%x\n",
			 "nbytes", nbytes, "cryptlen", req->cryptlen);
		rc = -EINVAL;
	}

	CIPHER_DBG(hace_dev, "%s:%d, %s:%d, %s:%d, %s:%p\n",
		   "nbytes", nbytes, "req->cryptlen", req->cryptlen,
		   "nb_out_sg", rctx->dst_nents,
		   "cipher addr", crypto_engine->cipher_addr);

	return aspeed_sk_complete(hace_dev, rc);
}

static int aspeed_sk_start(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_cipher_reqctx *rctx;
	struct skcipher_request *req;
	struct scatterlist *in_sg;
	int nbytes;

	req = crypto_engine->req;
	rctx = skcipher_request_ctx(req);
	in_sg = req->src;

	nbytes = sg_copy_to_buffer(in_sg, rctx->src_nents,
				   crypto_engine->cipher_addr, req->cryptlen);

	CIPHER_DBG(hace_dev, "%s:%d, %s:%d, %s:%d, %s:%p\n",
		   "nbytes", nbytes, "req->cryptlen", req->cryptlen,
		   "nb_in_sg", rctx->src_nents,
		   "cipher addr", crypto_engine->cipher_addr);

	if (!nbytes) {
		dev_warn(hace_dev->dev, "invalid sg copy, %s:0x%x, %s:0x%x\n",
			 "nbytes", nbytes, "cryptlen", req->cryptlen);
		return -EINVAL;
	}

	crypto_engine->resume = aspeed_sk_transfer;

	/* Trigger engines */
	ast_hace_write(hace_dev, crypto_engine->cipher_dma_addr,
		       ASPEED_HACE_SRC);
	ast_hace_write(hace_dev, crypto_engine->cipher_dma_addr,
		       ASPEED_HACE_DEST);
	ast_hace_write(hace_dev, req->cryptlen, ASPEED_HACE_DATA_LEN);
	ast_hace_write(hace_dev, rctx->enc_cmd, ASPEED_HACE_CMD);

	return -EINPROGRESS;
}

static int aspeed_sk_start_sg(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_sg_list *src_list, *dst_list;
	dma_addr_t src_dma_addr, dst_dma_addr;
	struct aspeed_cipher_reqctx *rctx;
	struct skcipher_request *req;
	struct scatterlist *s;
	int src_sg_len;
	int dst_sg_len;
	int total, i;
	int rc;

	CIPHER_DBG(hace_dev, "\n");

	req = crypto_engine->req;
	rctx = skcipher_request_ctx(req);

	rctx->enc_cmd |= HACE_CMD_DES_SG_CTRL | HACE_CMD_SRC_SG_CTRL |
			 HACE_CMD_AES_KEY_HW_EXP | HACE_CMD_MBUS_REQ_SYNC_EN;

	/* BIDIRECTIONAL */
	if (req->dst == req->src) {
		src_sg_len = dma_map_sg(hace_dev->dev, req->src,
					rctx->src_nents, DMA_BIDIRECTIONAL);
		dst_sg_len = src_sg_len;
		if (!src_sg_len) {
			dev_warn(hace_dev->dev, "dma_map_sg() src error\n");
			return -EINVAL;
		}

	} else {
		src_sg_len = dma_map_sg(hace_dev->dev, req->src,
					rctx->src_nents, DMA_TO_DEVICE);
		if (!src_sg_len) {
			dev_warn(hace_dev->dev, "dma_map_sg() src error\n");
			return -EINVAL;
		}

		dst_sg_len = dma_map_sg(hace_dev->dev, req->dst,
					rctx->dst_nents, DMA_FROM_DEVICE);
		if (!dst_sg_len) {
			dev_warn(hace_dev->dev, "dma_map_sg() dst error\n");
			rc = -EINVAL;
			goto free_req_src;
		}
	}

	src_list = (struct aspeed_sg_list *)crypto_engine->cipher_addr;
	src_dma_addr = crypto_engine->cipher_dma_addr;
	total = req->cryptlen;

	for_each_sg(req->src, s, src_sg_len, i) {
		u32 phy_addr = sg_dma_address(s);
		u32 len = sg_dma_len(s);

		if (total > len)
			total -= len;
		else {
			/* last sg list */
			len = total;
			len |= BIT(31);
			total = 0;
		}

		src_list[i].phy_addr = cpu_to_le32(phy_addr);
		src_list[i].len = cpu_to_le32(len);
	}

	if (total != 0) {
		rc = -EINVAL;
		goto free_req;
	}

	if (req->dst == req->src) {
		dst_list = src_list;
		dst_dma_addr = src_dma_addr;

	} else {
		dst_list = (struct aspeed_sg_list *)crypto_engine->dst_sg_addr;
		dst_dma_addr = crypto_engine->dst_sg_dma_addr;
		total = req->cryptlen;

		for_each_sg(req->dst, s, dst_sg_len, i) {
			u32 phy_addr = sg_dma_address(s);
			u32 len = sg_dma_len(s);

			if (total > len)
				total -= len;
			else {
				/* last sg list */
				len = total;
				len |= BIT(31);
				total = 0;
			}

			dst_list[i].phy_addr = cpu_to_le32(phy_addr);
			dst_list[i].len = cpu_to_le32(len);

		}

		dst_list[dst_sg_len].phy_addr = 0;
		dst_list[dst_sg_len].len = 0;
	}

	if (total != 0) {
		rc = -EINVAL;
		goto free_req;
	}

	crypto_engine->resume = aspeed_sk_transfer_sg;

	/* Memory barrier to ensure all data setup before engine starts */
	mb();

	/* Trigger engines */
	ast_hace_write(hace_dev, src_dma_addr, ASPEED_HACE_SRC);
	ast_hace_write(hace_dev, dst_dma_addr, ASPEED_HACE_DEST);
	ast_hace_write(hace_dev, req->cryptlen, ASPEED_HACE_DATA_LEN);
	ast_hace_write(hace_dev, rctx->enc_cmd, ASPEED_HACE_CMD);

	return -EINPROGRESS;

free_req:
	if (req->dst == req->src) {
		dma_unmap_sg(hace_dev->dev, req->src, rctx->src_nents,
			     DMA_BIDIRECTIONAL);

	} else {
		dma_unmap_sg(hace_dev->dev, req->dst, rctx->dst_nents,
			     DMA_FROM_DEVICE);
		dma_unmap_sg(hace_dev->dev, req->src, rctx->src_nents,
			     DMA_TO_DEVICE);
	}

	return rc;

free_req_src:
	dma_unmap_sg(hace_dev->dev, req->src, rctx->src_nents, DMA_TO_DEVICE);

	return rc;
}

static int aspeed_hace_skcipher_trigger(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_engine_crypto *crypto_engine = &hace_dev->crypto_engine;
	struct aspeed_cipher_reqctx *rctx;
	struct crypto_skcipher *cipher;
	struct aspeed_cipher_ctx *ctx;
	struct skcipher_request *req;

	CIPHER_DBG(hace_dev, "\n");

	req = crypto_engine->req;
	rctx = skcipher_request_ctx(req);
	cipher = crypto_skcipher_reqtfm(req);
	ctx = crypto_skcipher_ctx(cipher);

	/* enable interrupt */
	rctx->enc_cmd |= HACE_CMD_ISR_EN;

	rctx->dst_nents = sg_nents(req->dst);
	rctx->src_nents = sg_nents(req->src);

	ast_hace_write(hace_dev, crypto_engine->cipher_ctx_dma,
		       ASPEED_HACE_CONTEXT);

	if (rctx->enc_cmd & HACE_CMD_IV_REQUIRE) {
		if (rctx->enc_cmd & HACE_CMD_DES_SELECT)
			memcpy(crypto_engine->cipher_ctx + DES_BLOCK_SIZE,
			       req->iv, DES_BLOCK_SIZE);
		else
			memcpy(crypto_engine->cipher_ctx, req->iv,
			       AES_BLOCK_SIZE);
	}

	if (hace_dev->version == AST2600_VERSION) {
		memcpy(crypto_engine->cipher_ctx + 16, ctx->key, ctx->key_len);

		return aspeed_sk_start_sg(hace_dev);
	}

	memcpy(crypto_engine->cipher_ctx + 16, ctx->key, AES_MAX_KEYLENGTH);

	return aspeed_sk_start(hace_dev);
}

static int aspeed_des_crypt(struct skcipher_request *req, u32 cmd)
{
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	u32 crypto_alg = cmd & HACE_CMD_OP_MODE_MASK;

	CIPHER_DBG(hace_dev, "\n");

	if (crypto_alg == HACE_CMD_CBC || crypto_alg == HACE_CMD_ECB) {
		if (!IS_ALIGNED(req->cryptlen, DES_BLOCK_SIZE))
			return -EINVAL;
	}

	rctx->enc_cmd = cmd | HACE_CMD_DES_SELECT | HACE_CMD_RI_WO_DATA_ENABLE |
			HACE_CMD_DES | HACE_CMD_CONTEXT_LOAD_ENABLE |
			HACE_CMD_CONTEXT_SAVE_ENABLE;

	return aspeed_hace_crypto_handle_queue(hace_dev, req);
}

static int aspeed_des_setkey(struct crypto_skcipher *cipher, const u8 *key,
			     unsigned int keylen)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	int rc;

	CIPHER_DBG(hace_dev, "keylen: %d bits\n", keylen);

	if (keylen != DES_KEY_SIZE && keylen != DES3_EDE_KEY_SIZE) {
		dev_warn(hace_dev->dev, "invalid keylen: %d bits\n", keylen);
		return -EINVAL;
	}

	if (keylen == DES_KEY_SIZE) {
		rc = crypto_des_verify_key(tfm, key);
		if (rc)
			return rc;

	} else if (keylen == DES3_EDE_KEY_SIZE) {
		rc = crypto_des3_ede_verify_key(tfm, key);
		if (rc)
			return rc;
	}

	memcpy(ctx->key, key, keylen);
	ctx->key_len = keylen;

	crypto_skcipher_clear_flags(ctx->fallback_tfm, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctx->fallback_tfm, cipher->base.crt_flags &
				  CRYPTO_TFM_REQ_MASK);

	return crypto_skcipher_setkey(ctx->fallback_tfm, key, keylen);
}

static int aspeed_tdes_ctr_decrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CTR |
				HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ctr_encrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CTR |
				HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_cbc_decrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CBC |
				HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_cbc_encrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CBC |
				HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ecb_decrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_ECB |
				HACE_CMD_TRIPLE_DES);
}

static int aspeed_tdes_ecb_encrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_ECB |
				HACE_CMD_TRIPLE_DES);
}

static int aspeed_des_ctr_decrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CTR |
				HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ctr_encrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CTR |
				HACE_CMD_SINGLE_DES);
}

static int aspeed_des_cbc_decrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CBC |
				HACE_CMD_SINGLE_DES);
}

static int aspeed_des_cbc_encrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CBC |
				HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ecb_decrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_ECB |
				HACE_CMD_SINGLE_DES);
}

static int aspeed_des_ecb_encrypt(struct skcipher_request *req)
{
	return aspeed_des_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_ECB |
				HACE_CMD_SINGLE_DES);
}

static int aspeed_aes_crypt(struct skcipher_request *req, u32 cmd)
{
	struct aspeed_cipher_reqctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	u32 crypto_alg = cmd & HACE_CMD_OP_MODE_MASK;

	if (crypto_alg == HACE_CMD_CBC || crypto_alg == HACE_CMD_ECB) {
		if (!IS_ALIGNED(req->cryptlen, AES_BLOCK_SIZE))
			return -EINVAL;
	}

	CIPHER_DBG(hace_dev, "%s\n",
		   (cmd & HACE_CMD_ENCRYPT) ? "encrypt" : "decrypt");

	cmd |= HACE_CMD_AES_SELECT | HACE_CMD_RI_WO_DATA_ENABLE |
	       HACE_CMD_CONTEXT_LOAD_ENABLE | HACE_CMD_CONTEXT_SAVE_ENABLE;

	switch (ctx->key_len) {
	case AES_KEYSIZE_128:
		cmd |= HACE_CMD_AES128;
		break;
	case AES_KEYSIZE_192:
		cmd |= HACE_CMD_AES192;
		break;
	case AES_KEYSIZE_256:
		cmd |= HACE_CMD_AES256;
		break;
	default:
		return -EINVAL;
	}

	rctx->enc_cmd = cmd;

	return aspeed_hace_crypto_handle_queue(hace_dev, req);
}

static int aspeed_aes_setkey(struct crypto_skcipher *cipher, const u8 *key,
			     unsigned int keylen)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;
	struct crypto_aes_ctx gen_aes_key;

	CIPHER_DBG(hace_dev, "keylen: %d bits\n", (keylen * 8));

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	if (ctx->hace_dev->version == AST2500_VERSION) {
		aes_expandkey(&gen_aes_key, key, keylen);
		memcpy(ctx->key, gen_aes_key.key_enc, AES_MAX_KEYLENGTH);

	} else {
		memcpy(ctx->key, key, keylen);
	}

	ctx->key_len = keylen;

	crypto_skcipher_clear_flags(ctx->fallback_tfm, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctx->fallback_tfm, cipher->base.crt_flags &
				  CRYPTO_TFM_REQ_MASK);

	return crypto_skcipher_setkey(ctx->fallback_tfm, key, keylen);
}

static int aspeed_aes_ctr_decrypt(struct skcipher_request *req)
{
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CTR);
}

static int aspeed_aes_ctr_encrypt(struct skcipher_request *req)
{
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CTR);
}

static int aspeed_aes_cbc_decrypt(struct skcipher_request *req)
{
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_CBC);
}

static int aspeed_aes_cbc_encrypt(struct skcipher_request *req)
{
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_CBC);
}

static int aspeed_aes_ecb_decrypt(struct skcipher_request *req)
{
	return aspeed_aes_crypt(req, HACE_CMD_DECRYPT | HACE_CMD_ECB);
}

static int aspeed_aes_ecb_encrypt(struct skcipher_request *req)
{
	return aspeed_aes_crypt(req, HACE_CMD_ENCRYPT | HACE_CMD_ECB);
}

static int aspeed_crypto_cra_init(struct crypto_skcipher *tfm)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct aspeed_hace_alg *crypto_alg;


	crypto_alg = container_of(alg, struct aspeed_hace_alg, alg.skcipher.base);
	ctx->hace_dev = crypto_alg->hace_dev;
	ctx->start = aspeed_hace_skcipher_trigger;

	CIPHER_DBG(ctx->hace_dev, "%s\n", name);

	ctx->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->hace_dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	crypto_skcipher_set_reqsize(tfm, sizeof(struct aspeed_cipher_reqctx) +
			 crypto_skcipher_reqsize(ctx->fallback_tfm));

	return 0;
}

static void aspeed_crypto_cra_exit(struct crypto_skcipher *tfm)
{
	struct aspeed_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;

	CIPHER_DBG(hace_dev, "%s\n", crypto_tfm_alg_name(&tfm->base));
	crypto_free_skcipher(ctx->fallback_tfm);
}

static struct aspeed_hace_alg aspeed_crypto_algs[] = {
	{
		.alg.skcipher.base = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_ecb_encrypt,
			.decrypt	= aspeed_aes_ecb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(aes)",
				.cra_driver_name	= "aspeed-ecb-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize		= AES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_cbc_encrypt,
			.decrypt	= aspeed_aes_cbc_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cbc(aes)",
				.cra_driver_name	= "aspeed-cbc-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize		= AES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_ecb_encrypt,
			.decrypt	= aspeed_des_ecb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(des)",
				.cra_driver_name	= "aspeed-ecb-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_cbc_encrypt,
			.decrypt	= aspeed_des_cbc_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cbc(des)",
				.cra_driver_name	= "aspeed-cbc-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_ecb_encrypt,
			.decrypt	= aspeed_tdes_ecb_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ecb(des3_ede)",
				.cra_driver_name	= "aspeed-ecb-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_cbc_encrypt,
			.decrypt	= aspeed_tdes_cbc_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "cbc(des3_ede)",
				.cra_driver_name	= "aspeed-cbc-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC |
							  CRYPTO_ALG_NEED_FALLBACK,
				.cra_blocksize		= DES_BLOCK_SIZE,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
};

static struct aspeed_hace_alg aspeed_crypto_algs_g6[] = {
	{
		.alg.skcipher.base = {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aspeed_aes_setkey,
			.encrypt	= aspeed_aes_ctr_encrypt,
			.decrypt	= aspeed_aes_ctr_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ctr(aes)",
				.cra_driver_name	= "aspeed-ctr-aes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_des_ctr_encrypt,
			.decrypt	= aspeed_des_ctr_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ctr(des)",
				.cra_driver_name	= "aspeed-ctr-des",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},
	{
		.alg.skcipher.base = {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.setkey		= aspeed_des_setkey,
			.encrypt	= aspeed_tdes_ctr_encrypt,
			.decrypt	= aspeed_tdes_ctr_decrypt,
			.init		= aspeed_crypto_cra_init,
			.exit		= aspeed_crypto_cra_exit,
			.base = {
				.cra_name		= "ctr(des3_ede)",
				.cra_driver_name	= "aspeed-ctr-tdes",
				.cra_priority		= 300,
				.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
							  CRYPTO_ALG_ASYNC,
				.cra_blocksize		= 1,
				.cra_ctxsize		= sizeof(struct aspeed_cipher_ctx),
				.cra_alignmask		= 0x0f,
				.cra_module		= THIS_MODULE,
			}
		},
		.alg.skcipher.op = {
			.do_one_request = aspeed_crypto_do_request,
		},
	},

};

void aspeed_unregister_hace_crypto_algs(struct aspeed_hace_dev *hace_dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aspeed_crypto_algs); i++)
		crypto_engine_unregister_skcipher(&aspeed_crypto_algs[i].alg.skcipher);

	if (hace_dev->version != AST2600_VERSION)
		return;

	for (i = 0; i < ARRAY_SIZE(aspeed_crypto_algs_g6); i++)
		crypto_engine_unregister_skcipher(&aspeed_crypto_algs_g6[i].alg.skcipher);
}

void aspeed_register_hace_crypto_algs(struct aspeed_hace_dev *hace_dev)
{
	int rc, i;

	CIPHER_DBG(hace_dev, "\n");

	for (i = 0; i < ARRAY_SIZE(aspeed_crypto_algs); i++) {
		aspeed_crypto_algs[i].hace_dev = hace_dev;
		rc = crypto_engine_register_skcipher(&aspeed_crypto_algs[i].alg.skcipher);
		if (rc) {
			CIPHER_DBG(hace_dev, "Failed to register %s\n",
				   aspeed_crypto_algs[i].alg.skcipher.base.base.cra_name);
		}
	}

	if (hace_dev->version != AST2600_VERSION)
		return;

	for (i = 0; i < ARRAY_SIZE(aspeed_crypto_algs_g6); i++) {
		aspeed_crypto_algs_g6[i].hace_dev = hace_dev;
		rc = crypto_engine_register_skcipher(&aspeed_crypto_algs_g6[i].alg.skcipher);
		if (rc) {
			CIPHER_DBG(hace_dev, "Failed to register %s\n",
				   aspeed_crypto_algs_g6[i].alg.skcipher.base.base.cra_name);
		}
	}
}
