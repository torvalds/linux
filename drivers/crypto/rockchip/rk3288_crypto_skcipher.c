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

static void rk_crypto_complete(struct crypto_async_request *base, int err)
{
	if (base->complete)
		base->complete(base, err);
}

static int rk_handle_req(struct rk_crypto_info *dev,
			 struct skcipher_request *req)
{
	if (rk_cipher_need_fallback(req))
		return rk_cipher_fallback(req);

	return dev->enqueue(dev, &req->base);
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

static void rk_ablk_hw_init(struct rk_crypto_info *dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(dev->async_req);
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(cipher);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(cipher);
	u32 ivsize, block, conf_reg = 0;

	block = crypto_tfm_alg_blocksize(tfm);
	ivsize = crypto_skcipher_ivsize(cipher);

	if (block == DES_BLOCK_SIZE) {
		rctx->mode |= RK_CRYPTO_TDES_FIFO_MODE |
			     RK_CRYPTO_TDES_BYTESWAP_KEY |
			     RK_CRYPTO_TDES_BYTESWAP_IV;
		CRYPTO_WRITE(dev, RK_CRYPTO_TDES_CTRL, rctx->mode);
		memcpy_toio(dev->reg + RK_CRYPTO_TDES_IV_0, req->iv, ivsize);
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
		memcpy_toio(dev->reg + RK_CRYPTO_AES_IV_0, req->iv, ivsize);
		memcpy_toio(ctx->dev->reg + RK_CRYPTO_AES_KEY_0, ctx->key, ctx->keylen);
	}
	conf_reg |= RK_CRYPTO_BYTESWAP_BTFIFO |
		    RK_CRYPTO_BYTESWAP_BRFIFO;
	CRYPTO_WRITE(dev, RK_CRYPTO_CONF, conf_reg);
	CRYPTO_WRITE(dev, RK_CRYPTO_INTENA,
		     RK_CRYPTO_BCDMA_ERR_ENA | RK_CRYPTO_BCDMA_DONE_ENA);
}

static void crypto_dma_start(struct rk_crypto_info *dev)
{
	CRYPTO_WRITE(dev, RK_CRYPTO_BRDMAS, dev->addr_in);
	CRYPTO_WRITE(dev, RK_CRYPTO_BRDMAL, dev->count / 4);
	CRYPTO_WRITE(dev, RK_CRYPTO_BTDMAS, dev->addr_out);
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, RK_CRYPTO_BLOCK_START |
		     _SBF(RK_CRYPTO_BLOCK_START, 16));
}

static int rk_set_data_start(struct rk_crypto_info *dev)
{
	int err;
	struct skcipher_request *req =
		skcipher_request_cast(dev->async_req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 ivsize = crypto_skcipher_ivsize(tfm);
	u8 *src_last_blk = page_address(sg_page(dev->sg_src)) +
		dev->sg_src->offset + dev->sg_src->length - ivsize;

	/* Store the iv that need to be updated in chain mode.
	 * And update the IV buffer to contain the next IV for decryption mode.
	 */
	if (rctx->mode & RK_CRYPTO_DEC) {
		memcpy(ctx->iv, src_last_blk, ivsize);
		sg_pcopy_to_buffer(dev->first, dev->src_nents, req->iv,
				   ivsize, dev->total - ivsize);
	}

	err = dev->load_data(dev, dev->sg_src, dev->sg_dst);
	if (!err)
		crypto_dma_start(dev);
	return err;
}

static int rk_ablk_start(struct rk_crypto_info *dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(dev->async_req);
	unsigned long flags;
	int err = 0;

	dev->left_bytes = req->cryptlen;
	dev->total = req->cryptlen;
	dev->sg_src = req->src;
	dev->first = req->src;
	dev->src_nents = sg_nents(req->src);
	dev->sg_dst = req->dst;
	dev->dst_nents = sg_nents(req->dst);
	dev->aligned = 1;

	spin_lock_irqsave(&dev->lock, flags);
	rk_ablk_hw_init(dev);
	err = rk_set_data_start(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	return err;
}

static void rk_iv_copyback(struct rk_crypto_info *dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(dev->async_req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 ivsize = crypto_skcipher_ivsize(tfm);

	/* Update the IV buffer to contain the next IV for encryption mode. */
	if (!(rctx->mode & RK_CRYPTO_DEC)) {
		if (dev->aligned) {
			memcpy(req->iv, sg_virt(dev->sg_dst) +
				dev->sg_dst->length - ivsize, ivsize);
		} else {
			memcpy(req->iv, dev->addr_vir +
				dev->count - ivsize, ivsize);
		}
	}
}

static void rk_update_iv(struct rk_crypto_info *dev)
{
	struct skcipher_request *req =
		skcipher_request_cast(dev->async_req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rk_cipher_rctx *rctx = skcipher_request_ctx(req);
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	u32 ivsize = crypto_skcipher_ivsize(tfm);
	u8 *new_iv = NULL;

	if (rctx->mode & RK_CRYPTO_DEC) {
		new_iv = ctx->iv;
	} else {
		new_iv = page_address(sg_page(dev->sg_dst)) +
			 dev->sg_dst->offset + dev->sg_dst->length - ivsize;
	}

	if (ivsize == DES_BLOCK_SIZE)
		memcpy_toio(dev->reg + RK_CRYPTO_TDES_IV_0, new_iv, ivsize);
	else if (ivsize == AES_BLOCK_SIZE)
		memcpy_toio(dev->reg + RK_CRYPTO_AES_IV_0, new_iv, ivsize);
}

/* return:
 *	true	some err was occurred
 *	fault	no err, continue
 */
static int rk_ablk_rx(struct rk_crypto_info *dev)
{
	int err = 0;
	struct skcipher_request *req =
		skcipher_request_cast(dev->async_req);

	dev->unload_data(dev);
	if (!dev->aligned) {
		if (!sg_pcopy_from_buffer(req->dst, dev->dst_nents,
					  dev->addr_vir, dev->count,
					  dev->total - dev->left_bytes -
					  dev->count)) {
			err = -EINVAL;
			goto out_rx;
		}
	}
	if (dev->left_bytes) {
		rk_update_iv(dev);
		if (dev->aligned) {
			if (sg_is_last(dev->sg_src)) {
				dev_err(dev->dev, "[%s:%d] Lack of data\n",
					__func__, __LINE__);
				err = -ENOMEM;
				goto out_rx;
			}
			dev->sg_src = sg_next(dev->sg_src);
			dev->sg_dst = sg_next(dev->sg_dst);
		}
		err = rk_set_data_start(dev);
	} else {
		rk_iv_copyback(dev);
		/* here show the calculation is over without any err */
		dev->complete(dev->async_req, 0);
		tasklet_schedule(&dev->queue_task);
	}
out_rx:
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
	ctx->dev->align_size = crypto_tfm_alg_alignmask(crypto_skcipher_tfm(tfm)) + 1;
	ctx->dev->start = rk_ablk_start;
	ctx->dev->update = rk_ablk_rx;
	ctx->dev->complete = rk_crypto_complete;
	ctx->dev->addr_vir = (char *)__get_free_page(GFP_KERNEL);
	if (!ctx->dev->addr_vir)
		return -ENOMEM;

	ctx->fallback_tfm = crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	tfm->reqsize = sizeof(struct rk_cipher_rctx) +
		crypto_skcipher_reqsize(ctx->fallback_tfm);

	return 0;
}

static void rk_ablk_exit_tfm(struct crypto_skcipher *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx->key, ctx->keylen);
	free_page((unsigned long)ctx->dev->addr_vir);
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
