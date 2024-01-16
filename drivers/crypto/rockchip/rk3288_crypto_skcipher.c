// SPDX-License-Identifier: GPL-2.0-only
/*
 * Crypto acceleration support for Rockchip RK3288
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * Some ideas are from marvell-cesa.c and s5p-sss.c driver.
 */
#include <linux/device.h>
#include <crypto/scatterwalk.h>
#include "rk3288_crypto.h"

#define RK_CRYPTO_DEC			BIT(0)

static int rk_cipher_need_fallback(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	unsigned int bs = crypto_skcipher_blocksize(tfm);
	struct scatterlist *sgs, *sgd;
	unsigned int stodo, dtodo, len;

	if (!req->cryptlen)
		return true;

	len = req->cryptlen;
	sgs = req->src;
	sgd = req->dst;
	while (sgs && sgd) {
		if (!IS_ALIGNED(sgs->offset, sizeof(u32))) {
			return true;
		}
		if (!IS_ALIGNED(sgd->offset, sizeof(u32))) {
			return true;
		}
		stodo = min(len, sgs->length);
		if (stodo % bs) {
			return true;
		}
		dtodo = min(len, sgd->length);
		if (dtodo % bs) {
			return true;
		}
		if (stodo != dtodo) {
			return true;
		}
		len -= stodo;
		sgs = sg_next(sgs);
		sgd = sg_next(sgd);
	}
	return false;
}

static int rk_cipher_fallback(struct skcipher_request *areq)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct rk_cipher_ctx *op = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(areq);
	int err;

	skcipher_request_set_tfm(&rctx->fallback_req, op->fallback_tfm);
	skcipher_request_set_callback(&rctx->fallback_req, areq->base.flags,
				      areq->base.complete, areq->base.data);
	skcipher_request_set_crypt(&rctx->fallback_req, areq->src, areq->dst,
				   areq->cryptlen, areq->iv);
	if (rctx->mode & RK_CRYPTO_DEC)
		err = crypto_skcipher_decrypt(&rctx->fallback_req);
	else
		err = crypto_skcipher_encrypt(&rctx->fallback_req);
	return err;
}

static int rk_handle_req(struct rk_crypto_info *dev,
			 struct skcipher_request *req)
{
	struct crypto_engine *engine = dev->engine;

	if (rk_cipher_need_fallback(req))
		return rk_cipher_fallback(req);

	return crypto_transfer_skcipher_request_to_engine(engine, req);
}

static int rk_aes_setkey(struct crypto_skcipher *cipher,
			 const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;
	ctx->keylen = keylen;
	memcpy(ctx->key, key, keylen);

	return crypto_skcipher_setkey(ctx->fallback_tfm, key, keylen);
}

static int rk_des_setkey(struct crypto_skcipher *cipher,
			 const u8 *key, unsigned int keylen)
{
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	int err;

	err = verify_skcipher_des_key(cipher, key);
	if (err)
		return err;

	ctx->keylen = keylen;
	memcpy(ctx->key, key, keylen);

	return crypto_skcipher_setkey(ctx->fallback_tfm, key, keylen);
}

static int rk_tdes_setkey(struct crypto_skcipher *cipher,
			  const u8 *key, unsigned int keylen)
{
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	int err;

	err = verify_skcipher_des3_key(cipher, key);
	if (err)
		return err;

	ctx->keylen = keylen;
	memcpy(ctx->key, key, keylen);

	return crypto_skcipher_setkey(ctx->fallback_tfm, key, keylen);
}

static int rk_aes_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_AES_ECB_MODE;
	return rk_handle_req(dev, req);
}

static int rk_aes_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_AES_ECB_MODE | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_aes_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_AES_CBC_MODE;
	return rk_handle_req(dev, req);
}

static int rk_aes_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_AES_CBC_MODE | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = 0;
	return rk_handle_req(dev, req);
}

static int rk_des_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_TDES_CHAINMODE_CBC;
	return rk_handle_req(dev, req);
}

static int rk_des_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_TDES_CHAINMODE_CBC | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_TDES_SELECT;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_TDES_SELECT | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_TDES_SELECT | RK_CRYPTO_TDES_CHAINMODE_CBC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_crypto_info *dev = ctx->dev;

	rctx->mode = RK_CRYPTO_TDES_SELECT | RK_CRYPTO_TDES_CHAINMODE_CBC |
		    RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static void rk_ablk_hw_init(struct rk_crypto_info *dev, struct skcipher_request *req)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	u32 block, conf_reg = 0;

	block = crypto_tfm_alg_blocksize(tfm);

	if (block == DES_BLOCK_SIZE) {
		rctx->mode |= RK_CRYPTO_TDES_FIFO_MODE |
			     RK_CRYPTO_TDES_BYTESWAP_KEY |
			     RK_CRYPTO_TDES_BYTESWAP_IV;
		CRYPTO_WRITE(dev, RK_CRYPTO_TDES_CTRL, rctx->mode);
		memcpy_toio(ctx->dev->reg + RK_CRYPTO_TDES_KEY1_0, ctx->key, ctx->keylen);
		conf_reg = RK_CRYPTO_DESSEL;
	} else {
		rctx->mode |= RK_CRYPTO_AES_FIFO_MODE |
			     RK_CRYPTO_AES_KEY_CHANGE |
			     RK_CRYPTO_AES_BYTESWAP_KEY |
			     RK_CRYPTO_AES_BYTESWAP_IV;
		if (ctx->keylen == AES_KEYSIZE_192)
			rctx->mode |= RK_CRYPTO_AES_192BIT_key;
		else if (ctx->keylen == AES_KEYSIZE_256)
			rctx->mode |= RK_CRYPTO_AES_256BIT_key;
		CRYPTO_WRITE(dev, RK_CRYPTO_AES_CTRL, rctx->mode);
		memcpy_toio(ctx->dev->reg + RK_CRYPTO_AES_KEY_0, ctx->key, ctx->keylen);
	}
	conf_reg |= RK_CRYPTO_BYTESWAP_BTFIFO |
		    RK_CRYPTO_BYTESWAP_BRFIFO;
	CRYPTO_WRITE(dev, RK_CRYPTO_CONF, conf_reg);
	CRYPTO_WRITE(dev, RK_CRYPTO_INTENA,
		     RK_CRYPTO_BCDMA_ERR_ENA | RK_CRYPTO_BCDMA_DONE_ENA);
}

static void crypto_dma_start(struct rk_crypto_info *dev,
			     struct scatterlist *sgs,
			     struct scatterlist *sgd, unsigned int todo)
{
	CRYPTO_WRITE(dev, RK_CRYPTO_BRDMAS, sg_dma_address(sgs));
	CRYPTO_WRITE(dev, RK_CRYPTO_BRDMAL, todo);
	CRYPTO_WRITE(dev, RK_CRYPTO_BTDMAS, sg_dma_address(sgd));
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, RK_CRYPTO_BLOCK_START |
		     _SBF(RK_CRYPTO_BLOCK_START, 16));
}

static int rk_cipher_run(struct crypto_engine *engine, void *async_req)
{
	struct skcipher_request *areq = container_of(async_req, struct skcipher_request, base);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(areq);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(areq);
	struct scatterlist *sgs, *sgd;
	int err = 0;
	int ivsize = crypto_skcipher_ivsize(tfm);
	int offset;
	u8 iv[AES_BLOCK_SIZE];
	u8 biv[AES_BLOCK_SIZE];
	u8 *ivtouse = areq->iv;
	unsigned int len = areq->cryptlen;
	unsigned int todo;

	ivsize = crypto_skcipher_ivsize(tfm);
	if (areq->iv && crypto_skcipher_ivsize(tfm) > 0) {
		if (rctx->mode & RK_CRYPTO_DEC) {
			offset = areq->cryptlen - ivsize;
			scatterwalk_map_and_copy(rctx->backup_iv, areq->src,
						 offset, ivsize, 0);
		}
	}

	sgs = areq->src;
	sgd = areq->dst;

	while (sgs && sgd && len) {
		if (!sgs->length) {
			sgs = sg_next(sgs);
			sgd = sg_next(sgd);
			continue;
		}
		if (rctx->mode & RK_CRYPTO_DEC) {
			/* we backup last block of source to be used as IV at next step */
			offset = sgs->length - ivsize;
			scatterwalk_map_and_copy(biv, sgs, offset, ivsize, 0);
		}
		if (sgs == sgd) {
			err = dma_map_sg(ctx->dev->dev, sgs, 1, DMA_BIDIRECTIONAL);
			if (err <= 0) {
				err = -EINVAL;
				goto theend_iv;
			}
		} else {
			err = dma_map_sg(ctx->dev->dev, sgs, 1, DMA_TO_DEVICE);
			if (err <= 0) {
				err = -EINVAL;
				goto theend_iv;
			}
			err = dma_map_sg(ctx->dev->dev, sgd, 1, DMA_FROM_DEVICE);
			if (err <= 0) {
				err = -EINVAL;
				goto theend_sgs;
			}
		}
		err = 0;
		rk_ablk_hw_init(ctx->dev, areq);
		if (ivsize) {
			if (ivsize == DES_BLOCK_SIZE)
				memcpy_toio(ctx->dev->reg + RK_CRYPTO_TDES_IV_0, ivtouse, ivsize);
			else
				memcpy_toio(ctx->dev->reg + RK_CRYPTO_AES_IV_0, ivtouse, ivsize);
		}
		reinit_completion(&ctx->dev->complete);
		ctx->dev->status = 0;

		todo = min(sg_dma_len(sgs), len);
		len -= todo;
		crypto_dma_start(ctx->dev, sgs, sgd, todo / 4);
		wait_for_completion_interruptible_timeout(&ctx->dev->complete,
							  msecs_to_jiffies(2000));
		if (!ctx->dev->status) {
			dev_err(ctx->dev->dev, "DMA timeout\n");
			err = -EFAULT;
			goto theend;
		}
		if (sgs == sgd) {
			dma_unmap_sg(ctx->dev->dev, sgs, 1, DMA_BIDIRECTIONAL);
		} else {
			dma_unmap_sg(ctx->dev->dev, sgs, 1, DMA_TO_DEVICE);
			dma_unmap_sg(ctx->dev->dev, sgd, 1, DMA_FROM_DEVICE);
		}
		if (rctx->mode & RK_CRYPTO_DEC) {
			memcpy(iv, biv, ivsize);
			ivtouse = iv;
		} else {
			offset = sgd->length - ivsize;
			scatterwalk_map_and_copy(iv, sgd, offset, ivsize, 0);
			ivtouse = iv;
		}
		sgs = sg_next(sgs);
		sgd = sg_next(sgd);
	}

	if (areq->iv && ivsize > 0) {
		offset = areq->cryptlen - ivsize;
		if (rctx->mode & RK_CRYPTO_DEC) {
			memcpy(areq->iv, rctx->backup_iv, ivsize);
			memzero_explicit(rctx->backup_iv, ivsize);
		} else {
			scatterwalk_map_and_copy(areq->iv, areq->dst, offset,
						 ivsize, 0);
		}
	}

theend:
	local_bh_disable();
	crypto_finalize_skcipher_request(engine, areq, err);
	local_bh_enable();
	return 0;

theend_sgs:
	if (sgs == sgd) {
		dma_unmap_sg(ctx->dev->dev, sgs, 1, DMA_BIDIRECTIONAL);
	} else {
		dma_unmap_sg(ctx->dev->dev, sgs, 1, DMA_TO_DEVICE);
		dma_unmap_sg(ctx->dev->dev, sgd, 1, DMA_FROM_DEVICE);
	}
theend_iv:
	return err;
}

static int rk_ablk_init_tfm(struct crypto_skcipher *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct rk_crypto_tmp *algt;

	algt = container_of(alg, struct rk_crypto_tmp, alg.skcipher);

	ctx->dev = algt->dev;

	ctx->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	tfm->reqsize = sizeof(struct rk_cipher_rctx) +
		crypto_skcipher_reqsize(ctx->fallback_tfm);

	ctx->enginectx.op.do_one_request = rk_cipher_run;

	return 0;
}

static void rk_ablk_exit_tfm(struct crypto_skcipher *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx->key, ctx->keylen);
	crypto_free_skcipher(ctx->fallback_tfm);
}

struct rk_crypto_tmp rk_ecb_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.skcipher = {
		.base.cra_name		= "ecb(aes)",
		.base.cra_driver_name	= "ecb-aes-rk",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct rk_cipher_ctx),
		.base.cra_alignmask	= 0x0f,
		.base.cra_module	= THIS_MODULE,

		.init			= rk_ablk_init_tfm,
		.exit			= rk_ablk_exit_tfm,
		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.setkey			= rk_aes_setkey,
		.encrypt		= rk_aes_ecb_encrypt,
		.decrypt		= rk_aes_ecb_decrypt,
	}
};

struct rk_crypto_tmp rk_cbc_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.skcipher = {
		.base.cra_name		= "cbc(aes)",
		.base.cra_driver_name	= "cbc-aes-rk",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct rk_cipher_ctx),
		.base.cra_alignmask	= 0x0f,
		.base.cra_module	= THIS_MODULE,

		.init			= rk_ablk_init_tfm,
		.exit			= rk_ablk_exit_tfm,
		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
		.setkey			= rk_aes_setkey,
		.encrypt		= rk_aes_cbc_encrypt,
		.decrypt		= rk_aes_cbc_decrypt,
	}
};

struct rk_crypto_tmp rk_ecb_des_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.skcipher = {
		.base.cra_name		= "ecb(des)",
		.base.cra_driver_name	= "ecb-des-rk",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct rk_cipher_ctx),
		.base.cra_alignmask	= 0x07,
		.base.cra_module	= THIS_MODULE,

		.init			= rk_ablk_init_tfm,
		.exit			= rk_ablk_exit_tfm,
		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.setkey			= rk_des_setkey,
		.encrypt		= rk_des_ecb_encrypt,
		.decrypt		= rk_des_ecb_decrypt,
	}
};

struct rk_crypto_tmp rk_cbc_des_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.skcipher = {
		.base.cra_name		= "cbc(des)",
		.base.cra_driver_name	= "cbc-des-rk",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct rk_cipher_ctx),
		.base.cra_alignmask	= 0x07,
		.base.cra_module	= THIS_MODULE,

		.init			= rk_ablk_init_tfm,
		.exit			= rk_ablk_exit_tfm,
		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.ivsize			= DES_BLOCK_SIZE,
		.setkey			= rk_des_setkey,
		.encrypt		= rk_des_cbc_encrypt,
		.decrypt		= rk_des_cbc_decrypt,
	}
};

struct rk_crypto_tmp rk_ecb_des3_ede_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.skcipher = {
		.base.cra_name		= "ecb(des3_ede)",
		.base.cra_driver_name	= "ecb-des3-ede-rk",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct rk_cipher_ctx),
		.base.cra_alignmask	= 0x07,
		.base.cra_module	= THIS_MODULE,

		.init			= rk_ablk_init_tfm,
		.exit			= rk_ablk_exit_tfm,
		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.setkey			= rk_tdes_setkey,
		.encrypt		= rk_des3_ede_ecb_encrypt,
		.decrypt		= rk_des3_ede_ecb_decrypt,
	}
};

struct rk_crypto_tmp rk_cbc_des3_ede_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.skcipher = {
		.base.cra_name		= "cbc(des3_ede)",
		.base.cra_driver_name	= "cbc-des3-ede-rk",
		.base.cra_priority	= 300,
		.base.cra_flags		= CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct rk_cipher_ctx),
		.base.cra_alignmask	= 0x07,
		.base.cra_module	= THIS_MODULE,

		.init			= rk_ablk_init_tfm,
		.exit			= rk_ablk_exit_tfm,
		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.ivsize			= DES_BLOCK_SIZE,
		.setkey			= rk_tdes_setkey,
		.encrypt		= rk_des3_ede_cbc_encrypt,
		.decrypt		= rk_des3_ede_cbc_decrypt,
	}
};
