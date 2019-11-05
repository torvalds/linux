// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Support for OMAP AES GCM HW acceleration.
 *
 * Copyright (c) 2016 Texas Instruments Incorporated
 */

#include <linux/errno.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/omap-dma.h>
#include <linux/interrupt.h>
#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <crypto/internal/aead.h>

#include "omap-crypto.h"
#include "omap-aes.h"

static int omap_aes_gcm_handle_queue(struct omap_aes_dev *dd,
				     struct aead_request *req);

static void omap_aes_gcm_finish_req(struct omap_aes_dev *dd, int ret)
{
	struct aead_request *req = dd->aead_req;

	dd->flags &= ~FLAGS_BUSY;
	dd->in_sg = NULL;
	dd->out_sg = NULL;

	req->base.complete(&req->base, ret);
}

static void omap_aes_gcm_done_task(struct omap_aes_dev *dd)
{
	u8 *tag;
	int alen, clen, i, ret = 0, nsg;
	struct omap_aes_reqctx *rctx;

	alen = ALIGN(dd->assoc_len, AES_BLOCK_SIZE);
	clen = ALIGN(dd->total, AES_BLOCK_SIZE);
	rctx = aead_request_ctx(dd->aead_req);

	nsg = !!(dd->assoc_len && dd->total);

	dma_sync_sg_for_device(dd->dev, dd->out_sg, dd->out_sg_len,
			       DMA_FROM_DEVICE);
	dma_unmap_sg(dd->dev, dd->in_sg, dd->in_sg_len, DMA_TO_DEVICE);
	dma_unmap_sg(dd->dev, dd->out_sg, dd->out_sg_len, DMA_FROM_DEVICE);
	omap_aes_crypt_dma_stop(dd);

	omap_crypto_cleanup(dd->out_sg, dd->orig_out,
			    dd->aead_req->assoclen, dd->total,
			    FLAGS_OUT_DATA_ST_SHIFT, dd->flags);

	if (dd->flags & FLAGS_ENCRYPT)
		scatterwalk_map_and_copy(rctx->auth_tag,
					 dd->aead_req->dst,
					 dd->total + dd->aead_req->assoclen,
					 dd->authsize, 1);

	omap_crypto_cleanup(&dd->in_sgl[0], NULL, 0, alen,
			    FLAGS_ASSOC_DATA_ST_SHIFT, dd->flags);

	omap_crypto_cleanup(&dd->in_sgl[nsg], NULL, 0, clen,
			    FLAGS_IN_DATA_ST_SHIFT, dd->flags);

	if (!(dd->flags & FLAGS_ENCRYPT)) {
		tag = (u8 *)rctx->auth_tag;
		for (i = 0; i < dd->authsize; i++) {
			if (tag[i]) {
				dev_err(dd->dev, "GCM decryption: Tag Message is wrong\n");
				ret = -EBADMSG;
			}
		}
	}

	omap_aes_gcm_finish_req(dd, ret);
	omap_aes_gcm_handle_queue(dd, NULL);
}

static int omap_aes_gcm_copy_buffers(struct omap_aes_dev *dd,
				     struct aead_request *req)
{
	int alen, clen, cryptlen, assoclen, ret;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int authlen = crypto_aead_authsize(aead);
	struct scatterlist *tmp, sg_arr[2];
	int nsg;
	u16 flags;

	assoclen = req->assoclen;
	cryptlen = req->cryptlen;

	if (dd->flags & FLAGS_RFC4106_GCM)
		assoclen -= 8;

	if (!(dd->flags & FLAGS_ENCRYPT))
		cryptlen -= authlen;

	alen = ALIGN(assoclen, AES_BLOCK_SIZE);
	clen = ALIGN(cryptlen, AES_BLOCK_SIZE);

	nsg = !!(assoclen && cryptlen);

	omap_aes_clear_copy_flags(dd);

	sg_init_table(dd->in_sgl, nsg + 1);
	if (assoclen) {
		tmp = req->src;
		ret = omap_crypto_align_sg(&tmp, assoclen,
					   AES_BLOCK_SIZE, dd->in_sgl,
					   OMAP_CRYPTO_COPY_DATA |
					   OMAP_CRYPTO_ZERO_BUF |
					   OMAP_CRYPTO_FORCE_SINGLE_ENTRY,
					   FLAGS_ASSOC_DATA_ST_SHIFT,
					   &dd->flags);
	}

	if (cryptlen) {
		tmp = scatterwalk_ffwd(sg_arr, req->src, req->assoclen);

		ret = omap_crypto_align_sg(&tmp, cryptlen,
					   AES_BLOCK_SIZE, &dd->in_sgl[nsg],
					   OMAP_CRYPTO_COPY_DATA |
					   OMAP_CRYPTO_ZERO_BUF |
					   OMAP_CRYPTO_FORCE_SINGLE_ENTRY,
					   FLAGS_IN_DATA_ST_SHIFT,
					   &dd->flags);
	}

	dd->in_sg = dd->in_sgl;
	dd->total = cryptlen;
	dd->assoc_len = assoclen;
	dd->authsize = authlen;

	dd->out_sg = req->dst;
	dd->orig_out = req->dst;

	dd->out_sg = scatterwalk_ffwd(sg_arr, req->dst, assoclen);

	flags = 0;
	if (req->src == req->dst || dd->out_sg == sg_arr)
		flags |= OMAP_CRYPTO_FORCE_COPY;

	if (cryptlen) {
		ret = omap_crypto_align_sg(&dd->out_sg, cryptlen,
					   AES_BLOCK_SIZE, &dd->out_sgl,
					   flags,
					   FLAGS_OUT_DATA_ST_SHIFT, &dd->flags);
		if (ret)
			return ret;
	}

	dd->in_sg_len = sg_nents_for_len(dd->in_sg, alen + clen);
	dd->out_sg_len = sg_nents_for_len(dd->out_sg, clen);

	return 0;
}

static void omap_aes_gcm_complete(struct crypto_async_request *req, int err)
{
	struct omap_aes_gcm_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static int do_encrypt_iv(struct aead_request *req, u32 *tag, u32 *iv)
{
	struct scatterlist iv_sg, tag_sg;
	struct skcipher_request *sk_req;
	struct omap_aes_gcm_result result;
	struct omap_aes_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	int ret = 0;

	sk_req = skcipher_request_alloc(ctx->ctr, GFP_KERNEL);
	if (!sk_req) {
		pr_err("skcipher: Failed to allocate request\n");
		return -ENOMEM;
	}

	init_completion(&result.completion);

	sg_init_one(&iv_sg, iv, AES_BLOCK_SIZE);
	sg_init_one(&tag_sg, tag, AES_BLOCK_SIZE);
	skcipher_request_set_callback(sk_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      omap_aes_gcm_complete, &result);
	ret = crypto_skcipher_setkey(ctx->ctr, (u8 *)ctx->key, ctx->keylen);
	skcipher_request_set_crypt(sk_req, &iv_sg, &tag_sg, AES_BLOCK_SIZE,
				   NULL);
	ret = crypto_skcipher_encrypt(sk_req);
	switch (ret) {
	case 0:
		break;
	case -EINPROGRESS:
	case -EBUSY:
		ret = wait_for_completion_interruptible(&result.completion);
		if (!ret) {
			ret = result.err;
			if (!ret) {
				reinit_completion(&result.completion);
				break;
			}
		}
		/* fall through */
	default:
		pr_err("Encryption of IV failed for GCM mode\n");
		break;
	}

	skcipher_request_free(sk_req);
	return ret;
}

void omap_aes_gcm_dma_out_callback(void *data)
{
	struct omap_aes_dev *dd = data;
	struct omap_aes_reqctx *rctx;
	int i, val;
	u32 *auth_tag, tag[4];

	if (!(dd->flags & FLAGS_ENCRYPT))
		scatterwalk_map_and_copy(tag, dd->aead_req->src,
					 dd->total + dd->aead_req->assoclen,
					 dd->authsize, 0);

	rctx = aead_request_ctx(dd->aead_req);
	auth_tag = (u32 *)rctx->auth_tag;
	for (i = 0; i < 4; i++) {
		val = omap_aes_read(dd, AES_REG_TAG_N(dd, i));
		auth_tag[i] = val ^ auth_tag[i];
		if (!(dd->flags & FLAGS_ENCRYPT))
			auth_tag[i] = auth_tag[i] ^ tag[i];
	}

	omap_aes_gcm_done_task(dd);
}

static int omap_aes_gcm_handle_queue(struct omap_aes_dev *dd,
				     struct aead_request *req)
{
	struct omap_aes_ctx *ctx;
	struct aead_request *backlog;
	struct omap_aes_reqctx *rctx;
	unsigned long flags;
	int err, ret = 0;

	spin_lock_irqsave(&dd->lock, flags);
	if (req)
		ret = aead_enqueue_request(&dd->aead_queue, req);
	if (dd->flags & FLAGS_BUSY) {
		spin_unlock_irqrestore(&dd->lock, flags);
		return ret;
	}

	backlog = aead_get_backlog(&dd->aead_queue);
	req = aead_dequeue_request(&dd->aead_queue);
	if (req)
		dd->flags |= FLAGS_BUSY;
	spin_unlock_irqrestore(&dd->lock, flags);

	if (!req)
		return ret;

	if (backlog)
		backlog->base.complete(&backlog->base, -EINPROGRESS);

	ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	rctx = aead_request_ctx(req);

	dd->ctx = ctx;
	rctx->dd = dd;
	dd->aead_req = req;

	rctx->mode &= FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~FLAGS_MODE_MASK) | rctx->mode;

	err = omap_aes_gcm_copy_buffers(dd, req);
	if (err)
		return err;

	err = omap_aes_write_ctrl(dd);
	if (!err) {
		if (dd->in_sg_len && dd->out_sg_len)
			err = omap_aes_crypt_dma_start(dd);
		else
			omap_aes_gcm_dma_out_callback(dd);
	}

	if (err) {
		omap_aes_gcm_finish_req(dd, err);
		omap_aes_gcm_handle_queue(dd, NULL);
	}

	return ret;
}

static int omap_aes_gcm_crypt(struct aead_request *req, unsigned long mode)
{
	struct omap_aes_reqctx *rctx = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int authlen = crypto_aead_authsize(aead);
	struct omap_aes_dev *dd;
	__be32 counter = cpu_to_be32(1);
	int err, assoclen;

	memset(rctx->auth_tag, 0, sizeof(rctx->auth_tag));
	memcpy(rctx->iv + GCM_AES_IV_SIZE, &counter, 4);

	err = do_encrypt_iv(req, (u32 *)rctx->auth_tag, (u32 *)rctx->iv);
	if (err)
		return err;

	if (mode & FLAGS_RFC4106_GCM)
		assoclen = req->assoclen - 8;
	else
		assoclen = req->assoclen;
	if (assoclen + req->cryptlen == 0) {
		scatterwalk_map_and_copy(rctx->auth_tag, req->dst, 0, authlen,
					 1);
		return 0;
	}

	dd = omap_aes_find_dev(rctx);
	if (!dd)
		return -ENODEV;
	rctx->mode = mode;

	return omap_aes_gcm_handle_queue(dd, req);
}

int omap_aes_gcm_encrypt(struct aead_request *req)
{
	struct omap_aes_reqctx *rctx = aead_request_ctx(req);

	memcpy(rctx->iv, req->iv, GCM_AES_IV_SIZE);
	return omap_aes_gcm_crypt(req, FLAGS_ENCRYPT | FLAGS_GCM);
}

int omap_aes_gcm_decrypt(struct aead_request *req)
{
	struct omap_aes_reqctx *rctx = aead_request_ctx(req);

	memcpy(rctx->iv, req->iv, GCM_AES_IV_SIZE);
	return omap_aes_gcm_crypt(req, FLAGS_GCM);
}

int omap_aes_4106gcm_encrypt(struct aead_request *req)
{
	struct omap_aes_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct omap_aes_reqctx *rctx = aead_request_ctx(req);

	memcpy(rctx->iv, ctx->nonce, 4);
	memcpy(rctx->iv + 4, req->iv, 8);
	return omap_aes_gcm_crypt(req, FLAGS_ENCRYPT | FLAGS_GCM |
				  FLAGS_RFC4106_GCM);
}

int omap_aes_4106gcm_decrypt(struct aead_request *req)
{
	struct omap_aes_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct omap_aes_reqctx *rctx = aead_request_ctx(req);

	memcpy(rctx->iv, ctx->nonce, 4);
	memcpy(rctx->iv + 4, req->iv, 8);
	return omap_aes_gcm_crypt(req, FLAGS_GCM | FLAGS_RFC4106_GCM);
}

int omap_aes_gcm_setkey(struct crypto_aead *tfm, const u8 *key,
			unsigned int keylen)
{
	struct omap_aes_ctx *ctx = crypto_aead_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

int omap_aes_4106gcm_setkey(struct crypto_aead *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct omap_aes_ctx *ctx = crypto_aead_ctx(tfm);

	if (keylen < 4)
		return -EINVAL;

	keylen -= 4;
	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->key, key, keylen);
	memcpy(ctx->nonce, key + keylen, 4);
	ctx->keylen = keylen;

	return 0;
}
