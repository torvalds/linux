// SPDX-License-Identifier: GPL-2.0

#include <crypto/ctr.h>
#include <crypto/des.h>
#include <linux/dma-mapping.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/des.h>
#include <linux/platform_device.h>

#include "spacc_device.h"
#include "spacc_core.h"

static LIST_HEAD(spacc_cipher_alg_list);
static DEFINE_MUTEX(spacc_cipher_alg_mutex);

static struct mode_tab possible_ciphers[] = {
	/* {keylen, MODE_TAB_CIPH(name, id, iv_len, blk_len)} */

	/* SM4 */
	{ MODE_TAB_CIPH("cbc(sm4)", SM4_CBC, 16,  16), .keylen[0] = 16,
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 16 },
	{ MODE_TAB_CIPH("ecb(sm4)", SM4_ECB, 0,  16), .keylen[0] = 16,
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 16 },
	{ MODE_TAB_CIPH("ctr(sm4)", SM4_CTR, 16,  1), .keylen[0] = 16,
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 16 },
	{ MODE_TAB_CIPH("xts(sm4)", SM4_XTS, 16,  16), .keylen[0] = 32,
	.chunksize = 16, .walksize = 16, .min_keysize = 32, .max_keysize = 32 },
	{ MODE_TAB_CIPH("cts(cbc(sm4))", SM4_CS3, 16,  16), .keylen[0] = 16,
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 16 },

	/* AES */
	{ MODE_TAB_CIPH("cbc(aes)", AES_CBC, 16,  16), .keylen = { 16, 24, 32 },
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 32 },
	{ MODE_TAB_CIPH("ecb(aes)", AES_ECB, 0,  16), .keylen = { 16, 24, 32 },
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 32 },
	{ MODE_TAB_CIPH("xts(aes)", AES_XTS, 16,  16), .keylen = { 32, 48, 64 },
	.chunksize = 16, .walksize = 16, .min_keysize = 32, .max_keysize = 64 },
	{ MODE_TAB_CIPH("cts(cbc(aes))", AES_CS3, 16,  16),
	.keylen = { 16, 24, 32 }, .chunksize = 16, .walksize = 16,
	.min_keysize = 16, .max_keysize = 32 },
	{ MODE_TAB_CIPH("ctr(aes)", AES_CTR, 16,  1), .keylen = { 16, 24, 32 },
	.chunksize = 16, .walksize = 16, .min_keysize = 16, .max_keysize = 32 },

	/* CHACHA20 */
	{ MODE_TAB_CIPH("chacha20", CHACHA20_STREAM, 16, 1), .keylen[0] = 32,
	.chunksize = 64, .walksize = 64, .min_keysize = 32, .max_keysize = 32 },

	/* DES */
	{ MODE_TAB_CIPH("ecb(des)", DES_ECB, 0,  8), .keylen[0] = 8,
	.chunksize = 8, .walksize = 8, .min_keysize = 8, .max_keysize = 8},
	{ MODE_TAB_CIPH("cbc(des)", DES_CBC, 8,  8), .keylen[0] = 8,
	.chunksize = 8, .walksize = 8, .min_keysize = 8, .max_keysize = 8},
	{ MODE_TAB_CIPH("ecb(des3_ede)", 3DES_ECB, 0,  8), .keylen[0] = 24,
	.chunksize = 8, .walksize = 8, .min_keysize = 24, .max_keysize = 24 },
	{ MODE_TAB_CIPH("cbc(des3_ede)", 3DES_CBC, 8,  8), .keylen[0] = 24,
	.chunksize = 8, .walksize = 8, .min_keysize = 24, .max_keysize = 24 },
};

static int spacc_skcipher_fallback(unsigned char *name,
				   struct skcipher_request *req, int enc_dec)
{
	int ret = 0;
	struct crypto_skcipher *reqtfm   = crypto_skcipher_reqtfm(req);
	struct spacc_crypto_ctx *tctx    = crypto_skcipher_ctx(reqtfm);
	struct  spacc_crypto_reqctx *ctx = skcipher_request_ctx(req);

	tctx->fb.cipher = crypto_alloc_skcipher(name,
						CRYPTO_ALG_TYPE_SKCIPHER,
						CRYPTO_ALG_NEED_FALLBACK);

	crypto_skcipher_set_reqsize(reqtfm,
				    sizeof(struct spacc_crypto_reqctx) +
				    crypto_skcipher_reqsize(tctx->fb.cipher));
	ret = crypto_skcipher_setkey(tctx->fb.cipher, tctx->cipher_key,
				     tctx->key_len);
	if (ret)
		return ret;

	skcipher_request_set_tfm(&ctx->fb.cipher_req, tctx->fb.cipher);
	skcipher_request_set_crypt(&ctx->fb.cipher_req, req->src, req->dst,
				   req->cryptlen, req->iv);

	if (enc_dec)
		ret = crypto_skcipher_decrypt(&ctx->fb.cipher_req);
	else
		ret = crypto_skcipher_encrypt(&ctx->fb.cipher_req);

	crypto_free_skcipher(tctx->fb.cipher);
	tctx->fb.cipher = NULL;

	kfree(tctx->cipher_key);
	tctx->cipher_key = NULL;

	return ret;
}

static void spacc_cipher_cleanup_dma(struct device *dev,
				     struct skcipher_request *req)
{
	struct spacc_crypto_reqctx *ctx = skcipher_request_ctx(req);
	struct spacc_crypto_ctx *tctx = ctx->ccb.tctx;

	if (req->dst != req->src) {
		if (ctx->src_nents) {
			dma_unmap_sg(dev, req->src, ctx->src_nents,
					DMA_TO_DEVICE);
			pdu_ddt_free(&ctx->src);
		}

		if (ctx->dst_nents) {
			dma_unmap_sg(dev, req->dst, ctx->dst_nents,
					DMA_FROM_DEVICE);
			pdu_ddt_free(&ctx->dst);
		}
	} else {
		if (ctx->src_nents) {
			dma_unmap_sg(dev, req->src, ctx->src_nents,
					DMA_TO_DEVICE);
			pdu_ddt_free(&ctx->src);
		}
	}

	kfree(tctx->cipher_key);
	tctx->cipher_key = NULL;
}

static void spacc_cipher_cb(void *spacc, void *tfm)
{
	int err = -1;
	struct cipher_cb_data *cb = tfm;
	struct spacc_crypto_reqctx *ctx = skcipher_request_ctx(cb->req);

	u32 status_reg = readl(cb->spacc->regmap + SPACC_REG_STATUS);
	u32 status_ret = (status_reg >> 24) & 0x03;

	if (ctx->mode == CRYPTO_MODE_DES_CBC ||
	    ctx->mode == CRYPTO_MODE_3DES_CBC) {
		spacc_read_context(cb->spacc, cb->tctx->handle,
					SPACC_CRYPTO_OPERATION, NULL, 0,
					cb->req->iv, 8);
	} else if (ctx->mode != CRYPTO_MODE_DES_ECB  &&
		   ctx->mode != CRYPTO_MODE_3DES_ECB &&
		   ctx->mode != CRYPTO_MODE_SM4_ECB  &&
		   ctx->mode != CRYPTO_MODE_AES_ECB  &&
		   ctx->mode != CRYPTO_MODE_SM4_XTS  &&
		   ctx->mode != CRYPTO_MODE_KASUMI_ECB) {
		if (status_ret == 0x3) {
			err = -EINVAL;
			goto CALLBACK_ERR;
		}
		spacc_read_context(cb->spacc, cb->tctx->handle,
				   SPACC_CRYPTO_OPERATION, NULL, 0,
				   cb->req->iv, 16);
	}

	if (ctx->mode != CRYPTO_MODE_DES_ECB  &&
	    ctx->mode != CRYPTO_MODE_DES_CBC  &&
	    ctx->mode != CRYPTO_MODE_3DES_ECB &&
	    ctx->mode != CRYPTO_MODE_3DES_CBC) {
		if (status_ret == 0x03) {
			err = -EINVAL;
			goto CALLBACK_ERR;
		}
	}

	if (ctx->mode == CRYPTO_MODE_SM4_ECB && status_ret == 0x03) {
		err = -EINVAL;
		goto CALLBACK_ERR;
	}

	if (cb->req->dst != cb->req->src)
		dma_sync_sg_for_cpu(cb->tctx->dev, cb->req->dst, ctx->dst_nents,
			    DMA_FROM_DEVICE);

	err = cb->spacc->job[cb->new_handle].job_err;

CALLBACK_ERR:
	spacc_cipher_cleanup_dma(cb->tctx->dev, cb->req);
	spacc_close(cb->spacc, cb->new_handle);
	skcipher_request_complete(cb->req, err);
}

static int spacc_cipher_init_dma(struct device *dev,
				 struct skcipher_request *req)
{
	struct spacc_crypto_reqctx *ctx = skcipher_request_ctx(req);
	int rc;

	if (req->src == req->dst) {
		rc = spacc_sg_to_ddt(dev, req->src, req->cryptlen, &ctx->src,
				DMA_TO_DEVICE);
		if (rc < 0) {
			pdu_ddt_free(&ctx->src);
			return rc;
		}
		ctx->src_nents = rc;
	} else {
		rc = spacc_sg_to_ddt(dev, req->src, req->cryptlen, &ctx->src,
				DMA_TO_DEVICE);
		if (rc < 0) {
			pdu_ddt_free(&ctx->src);
			return rc;
		}
		ctx->src_nents = rc;

		rc = spacc_sg_to_ddt(dev, req->dst, req->cryptlen, &ctx->dst,
				DMA_FROM_DEVICE);
		if (rc < 0) {
			pdu_ddt_free(&ctx->dst);
			return rc;
		}
		ctx->dst_nents = rc;
	}

	return 0;
}

static int spacc_cipher_cra_init(struct crypto_tfm *tfm)
{
	struct spacc_crypto_ctx *tctx = crypto_tfm_ctx(tfm);
	const struct spacc_alg *salg = spacc_tfm_skcipher(tfm);

	tctx->keylen	 = 0;
	tctx->cipher_key = NULL;
	tctx->handle	 = -1;
	tctx->ctx_valid	 = false;
	tctx->dev	 = get_device(salg->dev[0]);

	crypto_skcipher_set_reqsize(__crypto_skcipher_cast(tfm),
				    sizeof(struct spacc_crypto_reqctx));

	return 0;
}

static void spacc_cipher_cra_exit(struct crypto_tfm *tfm)
{
	struct spacc_crypto_ctx *tctx = crypto_tfm_ctx(tfm);
	struct spacc_priv *priv = dev_get_drvdata(tctx->dev);


	if (tctx->handle >= 0)
		spacc_close(&priv->spacc, tctx->handle);

	put_device(tctx->dev);
}


static int spacc_check_keylen(const struct spacc_alg *salg, unsigned int keylen)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(salg->mode->keylen); i++)
		if (salg->mode->keylen[i] == keylen)
			return 0;

	return -EINVAL;
}

static int spacc_cipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			       unsigned int keylen)
{
	int ret = 0, rc = 0, err;
	const struct spacc_alg *salg    =  spacc_tfm_skcipher(&tfm->base);
	struct spacc_crypto_ctx *tctx   = crypto_skcipher_ctx(tfm);
	struct spacc_priv *priv         = dev_get_drvdata(tctx->dev);
	struct spacc_crypto_reqctx *ctx	= crypto_skcipher_ctx(tfm);

	err = spacc_check_keylen(salg, keylen);
	if (err)
		return err;

	ctx->mode        = salg->mode->id;
	tctx->key_len    = keylen;
	tctx->cipher_key = kmalloc(keylen, GFP_KERNEL);
	memcpy(tctx->cipher_key, key, keylen);

	if (tctx->handle >= 0) {
		spacc_close(&priv->spacc, tctx->handle);
		put_device(tctx->dev);
		tctx->handle = -1;
		tctx->dev    = NULL;
	}

	priv = NULL;
	priv = dev_get_drvdata(salg->dev[0]);
	tctx->dev = get_device(salg->dev[0]);
	ret = spacc_isenabled(&priv->spacc, salg->mode->id,
				       keylen);
	if (ret)
		tctx->handle = spacc_open(&priv->spacc, salg->mode->id,
					  CRYPTO_MODE_NULL, -1, 0,
					  spacc_cipher_cb, tfm);

	if (tctx->handle < 0) {
		put_device(salg->dev[0]);
		dev_dbg(salg->dev[0], "failed to open SPAcc context\n");
		return -EINVAL;
	}

	/* Weak key Implementation for DES_ECB */
	if (salg->mode->id == CRYPTO_MODE_DES_ECB) {
		err = verify_skcipher_des_key(tfm, key);
		if (err)
			return -EINVAL;
	}

	if (salg->mode->id == CRYPTO_MODE_SM4_F8 ||
	    salg->mode->id == CRYPTO_MODE_AES_F8) {
		/* f8 mode requires an IV of 128-bits and a key-salt mask,
		 * equivalent in size to the key.
		 * AES-F8 or SM4-F8 mode has a SALTKEY prepended to the base
		 * key.
		 */
		rc = spacc_write_context(&priv->spacc, tctx->handle,
					 SPACC_CRYPTO_OPERATION, key, 16,
					 NULL, 0);
	} else {
		rc = spacc_write_context(&priv->spacc, tctx->handle,
					 SPACC_CRYPTO_OPERATION, key, keylen,
					 NULL, 0);
	}

	if (rc < 0) {
		dev_dbg(salg->dev[0], "failed with SPAcc write context\n");
		return -EINVAL;
	}

	return 0;
}

static int spacc_cipher_process(struct skcipher_request *req, int enc_dec)
{
	u8 ivc1[16];
	unsigned char *name;
	unsigned int len = 0;
	u32 num_iv = 0, diff;
	u64 num_iv64 = 0, diff64;
	unsigned char chacha20_iv[16];
	int rc = 0, ret = 0, i = 0, j = 0;
	struct crypto_skcipher *reqtfm  = crypto_skcipher_reqtfm(req);
	struct spacc_crypto_ctx *tctx	= crypto_skcipher_ctx(reqtfm);
	struct spacc_crypto_reqctx *ctx	= skcipher_request_ctx(req);
	struct spacc_priv *priv		= dev_get_drvdata(tctx->dev);
	const struct spacc_alg *salg	= spacc_tfm_skcipher(&reqtfm->base);
	struct spacc_device *device_h	= &priv->spacc;

	len = ctx->spacc_cipher_cryptlen / 16;

	if (req->cryptlen == 0) {
		if (salg->mode->id == CRYPTO_MODE_SM4_CS3  ||
		    salg->mode->id == CRYPTO_MODE_SM4_XTS  ||
		    salg->mode->id == CRYPTO_MODE_AES_XTS  ||
		    salg->mode->id == CRYPTO_MODE_AES_CS3)
			return -EINVAL;
		else
			return 0;
	}

	/* Given IV - <1st 4-bytes as counter value>
	 *            <last 12-bytes as nonce>
	 * Reversing the order of nonce & counter as,
	 *            <1st 12-bytes as nonce>
	 *            <last 4-bytes as counter>
	 * and then write to HW context,
	 * ex:
	 * Given IV - 2a000000000000000000000000000002
	 * Reverse order - 0000000000000000000000020000002a
	 */
	if (salg->mode->id == CRYPTO_MODE_CHACHA20_STREAM) {
		for (i = 4; i < 16; i++) {
			chacha20_iv[j] = req->iv[i];
			j++;
		}

		j = j + 3;

		for (i = 0; i <= 3; i++) {
			chacha20_iv[j] = req->iv[i];
			j--;
		}
		memcpy(req->iv, chacha20_iv, 16);
	}

	if (salg->mode->id == CRYPTO_MODE_SM4_CFB) {
		if (req->cryptlen % 16 != 0) {
			name = salg->calg->cra_name;
			ret = spacc_skcipher_fallback(name, req, enc_dec);
			return ret;
		}
	}

	if (salg->mode->id == CRYPTO_MODE_SM4_XTS ||
	    salg->mode->id == CRYPTO_MODE_SM4_CS3 ||
	    salg->mode->id == CRYPTO_MODE_AES_XTS ||
	    salg->mode->id == CRYPTO_MODE_AES_CS3) {
		if (req->cryptlen == 16) {
			name = salg->calg->cra_name;
			ret = spacc_skcipher_fallback(name, req, enc_dec);
			return ret;
		}
	}

	if (salg->mode->id == CRYPTO_MODE_AES_CTR ||
	    salg->mode->id == CRYPTO_MODE_SM4_CTR) {
		/* copy the IV to local buffer */
		for (i = 0; i < 16; i++)
			ivc1[i] = req->iv[i];

		/* 32-bit counter width */
		if (readl(device_h->regmap + SPACC_REG_VERSION_EXT_3) & (0x2)) {

			for (i = 12; i < 16; i++) {
				num_iv <<= 8;
				num_iv |= ivc1[i];
			}

			diff = SPACC_CTR_IV_MAX32 - num_iv;

			if (len > diff) {
				name = salg->calg->cra_name;
				ret = spacc_skcipher_fallback(name,
							      req, enc_dec);
				return ret;
			}
		} else if (readl(device_h->regmap + SPACC_REG_VERSION_EXT_3)
			  & (0x3)) { /* 64-bit counter width */

			for (i = 8; i < 16; i++) {
				num_iv64 <<= 8;
				num_iv64 |= ivc1[i];
			}

			diff64 = SPACC_CTR_IV_MAX64 - num_iv64;

			if (len > diff64) {
				name = salg->calg->cra_name;
				ret = spacc_skcipher_fallback(name,
							      req, enc_dec);
				return ret;
			}
		} else if (readl(device_h->regmap + SPACC_REG_VERSION_EXT_3)
			   & (0x1)) { /* 16-bit counter width */

			for (i = 14; i < 16; i++) {
				num_iv <<= 8;
				num_iv |= ivc1[i];
			}

			diff = SPACC_CTR_IV_MAX16 - num_iv;

			if (len > diff) {
				name = salg->calg->cra_name;
				ret = spacc_skcipher_fallback(name,
							      req, enc_dec);
				return ret;
			}
		} else if (readl(device_h->regmap + SPACC_REG_VERSION_EXT_3)
			   & (0x0)) { /* 8-bit counter width */

			for (i = 15; i < 16; i++) {
				num_iv <<= 8;
				num_iv |= ivc1[i];
			}

			diff = SPACC_CTR_IV_MAX8 - num_iv;

			if (len > diff) {
				name = salg->calg->cra_name;
				ret = spacc_skcipher_fallback(name,
							      req, enc_dec);
				return ret;
			}
		}
	}

	if (salg->mode->id == CRYPTO_MODE_DES_CBC ||
	    salg->mode->id == CRYPTO_MODE_3DES_CBC)
		rc = spacc_write_context(&priv->spacc, tctx->handle,
					 SPACC_CRYPTO_OPERATION, NULL, 0,
					 req->iv, 8);
	else if (salg->mode->id != CRYPTO_MODE_DES_ECB  &&
		 salg->mode->id != CRYPTO_MODE_3DES_ECB &&
		 salg->mode->id != CRYPTO_MODE_SM4_ECB  &&
		 salg->mode->id != CRYPTO_MODE_AES_ECB  &&
		 salg->mode->id != CRYPTO_MODE_KASUMI_ECB)
		rc = spacc_write_context(&priv->spacc, tctx->handle,
					 SPACC_CRYPTO_OPERATION, NULL, 0,
					 req->iv, 16);

	if (rc < 0)
		pr_err("ERR: spacc_write_context\n");

	/* Initialize the DMA */
	rc = spacc_cipher_init_dma(tctx->dev, req);

	ctx->ccb.new_handle = spacc_clone_handle(&priv->spacc, tctx->handle,
						 &ctx->ccb);
	if (ctx->ccb.new_handle < 0) {
		spacc_cipher_cleanup_dma(tctx->dev, req);
		dev_dbg(salg->dev[0], "failed to clone handle\n");
		return -EINVAL;
	}

	/* copying the data to clone handle */
	ctx->ccb.tctx  = tctx;
	ctx->ccb.ctx   = ctx;
	ctx->ccb.req   = req;
	ctx->ccb.spacc = &priv->spacc;

	if (salg->mode->id == CRYPTO_MODE_SM4_CS3) {
		int handle = ctx->ccb.new_handle;

		if (handle < 0 || handle > SPACC_MAX_JOBS)
			return -ENXIO;

		device_h->job[handle].auxinfo_cs_mode = 3;
	}

	if (enc_dec) {  /* for decrypt */
		rc = spacc_set_operation(&priv->spacc, ctx->ccb.new_handle, 1,
					 ICV_IGNORE, IP_ICV_IGNORE, 0, 0, 0);
		spacc_set_key_exp(&priv->spacc, ctx->ccb.new_handle);
	} else {       /* for encrypt */
		rc = spacc_set_operation(&priv->spacc, ctx->ccb.new_handle, 0,
					 ICV_IGNORE, IP_ICV_IGNORE, 0, 0, 0);
	}

	rc = spacc_packet_enqueue_ddt(&priv->spacc, ctx->ccb.new_handle,
				      &ctx->src,
				      (req->dst == req->src) ? &ctx->src :
				      &ctx->dst,
				      req->cryptlen,
				      0, 0, 0, 0, 0);
	if (rc < 0) {
		spacc_cipher_cleanup_dma(tctx->dev, req);
		spacc_close(&priv->spacc, ctx->ccb.new_handle);

		if (rc != -EBUSY && rc < 0) {
			dev_err(tctx->dev,
				"failed to enqueue job, ERR: %d\n", rc);
			return rc;
		} else if (!(req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
			return -EBUSY;
		}
	}

	priv->spacc.job[tctx->handle].first_use  = 0;
	priv->spacc.job[tctx->handle].ctrl &=
		~(1UL << priv->spacc.config.ctrl_map[SPACC_CTRL_KEY_EXP]);

	return -EINPROGRESS;
}

static int spacc_cipher_encrypt(struct skcipher_request *req)
{
	int rv = 0;
	struct spacc_crypto_reqctx *ctx = skcipher_request_ctx(req);

	ctx->spacc_cipher_cryptlen = req->cryptlen;

	/* enc_dec - 0(encrypt), 1(decrypt) */
	rv = spacc_cipher_process(req, 0);

	return rv;
}

static int spacc_cipher_decrypt(struct skcipher_request *req)
{
	int rv = 0;
	struct spacc_crypto_reqctx *ctx = skcipher_request_ctx(req);

	ctx->spacc_cipher_cryptlen = req->cryptlen;

	/* enc_dec - 0(encrypt), 1(decrypt) */
	rv = spacc_cipher_process(req, 1);

	return rv;
}

static struct skcipher_alg spacc_skcipher_alg = {
	.setkey = spacc_cipher_setkey,
	.encrypt = spacc_cipher_encrypt,
	.decrypt = spacc_cipher_decrypt,
	/*
	 * Chunksize: Equal to the block size except for stream cipher
	 * such as CTR where it is set to the underlying block size.
	 *
	 * Walksize: Equal to the chunk size except in cases where the
	 * algorithm is considerably more efficient if it can operate on
	 * multiple chunks in parallel. Should be a multiple of chunksize.
	 */
	.min_keysize	= 16,
	.max_keysize	= 64,
	.ivsize		= 16,
	.chunksize	= 16,
	.walksize	= 16,
	.base = {
		.cra_flags = CRYPTO_ALG_TYPE_SKCIPHER	 |
			     CRYPTO_ALG_ASYNC		 |
			     CRYPTO_ALG_ALLOCATES_MEMORY |
			     CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize	= 16,
		.cra_ctxsize	= sizeof(struct spacc_crypto_ctx),
		.cra_priority	= 300,
		.cra_init	= spacc_cipher_cra_init,
		.cra_exit	= spacc_cipher_cra_exit,
		.cra_module	= THIS_MODULE,
	},
};

static void spacc_init_calg(struct crypto_alg *calg,
			    const struct mode_tab *mode)
{

	strscpy(calg->cra_name, mode->name, sizeof(mode->name) - 1);
	calg->cra_name[sizeof(mode->name) - 1] = '\0';

	strscpy(calg->cra_driver_name, "spacc-");
	strcat(calg->cra_driver_name, mode->name);
	calg->cra_driver_name[sizeof(calg->cra_driver_name) - 1] = '\0';
	calg->cra_blocksize = mode->blocklen;
}

static int spacc_register_cipher(struct spacc_alg *salg,
				 unsigned int algo_idx)
{
	int rc;

	salg->calg         = &salg->alg.skcipher.base;
	salg->alg.skcipher = spacc_skcipher_alg;

	/* this function will assign mode->name to calg->cra_name &
	 * calg->cra_driver_name
	 */
	spacc_init_calg(salg->calg, salg->mode);
	salg->alg.skcipher.ivsize = salg->mode->ivlen;
	salg->alg.skcipher.base.cra_blocksize = salg->mode->blocklen;

	salg->alg.skcipher.chunksize   = possible_ciphers[algo_idx].chunksize;
	salg->alg.skcipher.walksize    = possible_ciphers[algo_idx].walksize;
	salg->alg.skcipher.min_keysize = possible_ciphers[algo_idx].min_keysize;
	salg->alg.skcipher.max_keysize = possible_ciphers[algo_idx].max_keysize;

	rc = crypto_register_skcipher(&salg->alg.skcipher);
	if (rc < 0)
		return rc;

	mutex_lock(&spacc_cipher_alg_mutex);
	list_add(&salg->list, &spacc_cipher_alg_list);
	mutex_unlock(&spacc_cipher_alg_mutex);

	return 0;
}

int probe_ciphers(struct platform_device *spacc_pdev)
{
	int rc;
	unsigned int i, y;
	int registered = 0;
	struct spacc_alg *salg;
	struct spacc_priv *priv = dev_get_drvdata(&spacc_pdev->dev);

	for (i = 0; i < ARRAY_SIZE(possible_ciphers); i++)
		possible_ciphers[i].valid = 0;

	for (i = 0; i < ARRAY_SIZE(possible_ciphers) &&
	     (possible_ciphers[i].valid == 0); i++) {
		for (y = 0; y < 3; y++) {
			if (spacc_isenabled(&priv->spacc,
					    possible_ciphers[i].id & 0xFF,
					    possible_ciphers[i].keylen[y])) {
				salg = kmalloc(sizeof(*salg), GFP_KERNEL);
				if (!salg)
					return -ENOMEM;

				salg->mode = &possible_ciphers[i];
				salg->dev[0] = &spacc_pdev->dev;

				if (possible_ciphers[i].valid == 0) {
					rc = spacc_register_cipher(salg, i);
					if (rc < 0) {
						kfree(salg);
						continue;
					}
				}
				dev_dbg(&spacc_pdev->dev, "registered %s\n",
					 possible_ciphers[i].name);
				registered++;
				possible_ciphers[i].valid = 1;
			}
		}
	}

	return registered;
}

int spacc_unregister_cipher_algs(void)
{
	struct spacc_alg *salg, *tmp;

	mutex_lock(&spacc_cipher_alg_mutex);

	list_for_each_entry_safe(salg, tmp, &spacc_cipher_alg_list, list) {
		crypto_unregister_skcipher(&salg->alg.skcipher);
		list_del(&salg->list);
		kfree(salg);
	}

	mutex_unlock(&spacc_cipher_alg_mutex);

	return 0;
}
