/*
 * CCM: Counter with CBC-MAC
 *
 * (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "internal.h"

struct ccm_instance_ctx {
	struct crypto_skcipher_spawn ctr;
	struct crypto_spawn cipher;
};

struct crypto_ccm_ctx {
	struct crypto_cipher *cipher;
	struct crypto_skcipher *ctr;
};

struct crypto_rfc4309_ctx {
	struct crypto_aead *child;
	u8 nonce[3];
};

struct crypto_rfc4309_req_ctx {
	struct scatterlist src[3];
	struct scatterlist dst[3];
	struct aead_request subreq;
};

struct crypto_ccm_req_priv_ctx {
	u8 odata[16];
	u8 idata[16];
	u8 auth_tag[16];
	u32 ilen;
	u32 flags;
	struct scatterlist src[3];
	struct scatterlist dst[3];
	struct skcipher_request skreq;
};

static inline struct crypto_ccm_req_priv_ctx *crypto_ccm_reqctx(
	struct aead_request *req)
{
	unsigned long align = crypto_aead_alignmask(crypto_aead_reqtfm(req));

	return (void *)PTR_ALIGN((u8 *)aead_request_ctx(req), align + 1);
}

static int set_msg_len(u8 *block, unsigned int msglen, int csize)
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

static int crypto_ccm_setkey(struct crypto_aead *aead, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_skcipher *ctr = ctx->ctr;
	struct crypto_cipher *tfm = ctx->cipher;
	int err = 0;

	crypto_skcipher_clear_flags(ctr, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctr, crypto_aead_get_flags(aead) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(ctr, key, keylen);
	crypto_aead_set_flags(aead, crypto_skcipher_get_flags(ctr) &
			      CRYPTO_TFM_RES_MASK);
	if (err)
		goto out;

	crypto_cipher_clear_flags(tfm, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(tfm, crypto_aead_get_flags(aead) &
				    CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(tfm, key, keylen);
	crypto_aead_set_flags(aead, crypto_cipher_get_flags(tfm) &
			      CRYPTO_TFM_RES_MASK);

out:
	return err;
}

static int crypto_ccm_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
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

	return 0;
}

static int format_input(u8 *info, struct aead_request *req,
			unsigned int cryptlen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int lp = req->iv[0];
	unsigned int l = lp + 1;
	unsigned int m;

	m = crypto_aead_authsize(aead);

	memcpy(info, req->iv, 16);

	/* format control info per RFC 3610 and
	 * NIST Special Publication 800-38C
	 */
	*info |= (8 * ((m - 2) / 2));
	if (req->assoclen)
		*info |= 64;

	return set_msg_len(info + 16 - l, cryptlen, l);
}

static int format_adata(u8 *adata, unsigned int a)
{
	int len = 0;

	/* add control info for associated data
	 * RFC 3610 and NIST Special Publication 800-38C
	 */
	if (a < 65280) {
		*(__be16 *)adata = cpu_to_be16(a);
		len = 2;
	} else  {
		*(__be16 *)adata = cpu_to_be16(0xfffe);
		*(__be32 *)&adata[2] = cpu_to_be32(a);
		len = 6;
	}

	return len;
}

static void compute_mac(struct crypto_cipher *tfm, u8 *data, int n,
		       struct crypto_ccm_req_priv_ctx *pctx)
{
	unsigned int bs = 16;
	u8 *odata = pctx->odata;
	u8 *idata = pctx->idata;
	int datalen, getlen;

	datalen = n;

	/* first time in here, block may be partially filled. */
	getlen = bs - pctx->ilen;
	if (datalen >= getlen) {
		memcpy(idata + pctx->ilen, data, getlen);
		crypto_xor(odata, idata, bs);
		crypto_cipher_encrypt_one(tfm, odata, odata);
		datalen -= getlen;
		data += getlen;
		pctx->ilen = 0;
	}

	/* now encrypt rest of data */
	while (datalen >= bs) {
		crypto_xor(odata, data, bs);
		crypto_cipher_encrypt_one(tfm, odata, odata);

		datalen -= bs;
		data += bs;
	}

	/* check and see if there's leftover data that wasn't
	 * enough to fill a block.
	 */
	if (datalen) {
		memcpy(idata + pctx->ilen, data, datalen);
		pctx->ilen += datalen;
	}
}

static void get_data_to_compute(struct crypto_cipher *tfm,
			       struct crypto_ccm_req_priv_ctx *pctx,
			       struct scatterlist *sg, unsigned int len)
{
	struct scatter_walk walk;
	u8 *data_src;
	int n;

	scatterwalk_start(&walk, sg);

	while (len) {
		n = scatterwalk_clamp(&walk, len);
		if (!n) {
			scatterwalk_start(&walk, sg_next(walk.sg));
			n = scatterwalk_clamp(&walk, len);
		}
		data_src = scatterwalk_map(&walk);

		compute_mac(tfm, data_src, n, pctx);
		len -= n;

		scatterwalk_unmap(data_src);
		scatterwalk_advance(&walk, n);
		scatterwalk_done(&walk, 0, len);
		if (len)
			crypto_yield(pctx->flags);
	}

	/* any leftover needs padding and then encrypted */
	if (pctx->ilen) {
		int padlen;
		u8 *odata = pctx->odata;
		u8 *idata = pctx->idata;

		padlen = 16 - pctx->ilen;
		memset(idata + pctx->ilen, 0, padlen);
		crypto_xor(odata, idata, 16);
		crypto_cipher_encrypt_one(tfm, odata, odata);
		pctx->ilen = 0;
	}
}

static int crypto_ccm_auth(struct aead_request *req, struct scatterlist *plain,
			   unsigned int cryptlen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct crypto_cipher *cipher = ctx->cipher;
	unsigned int assoclen = req->assoclen;
	u8 *odata = pctx->odata;
	u8 *idata = pctx->idata;
	int err;

	/* format control data for input */
	err = format_input(odata, req, cryptlen);
	if (err)
		goto out;

	/* encrypt first block to use as start in computing mac  */
	crypto_cipher_encrypt_one(cipher, odata, odata);

	/* format associated data and compute into mac */
	if (assoclen) {
		pctx->ilen = format_adata(idata, assoclen);
		get_data_to_compute(cipher, pctx, req->src, req->assoclen);
	} else {
		pctx->ilen = 0;
	}

	/* compute plaintext into mac */
	if (cryptlen)
		get_data_to_compute(cipher, pctx, plain, cryptlen);

out:
	return err;
}

static void crypto_ccm_encrypt_done(struct crypto_async_request *areq, int err)
{
	struct aead_request *req = areq->data;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	u8 *odata = pctx->odata;

	if (!err)
		scatterwalk_map_and_copy(odata, req->dst,
					 req->assoclen + req->cryptlen,
					 crypto_aead_authsize(aead), 1);
	aead_request_complete(req, err);
}

static inline int crypto_ccm_check_iv(const u8 *iv)
{
	/* 2 <= L <= 8, so 1 <= L' <= 7. */
	if (1 > iv[0] || iv[0] > 7)
		return -EINVAL;

	return 0;
}

static int crypto_ccm_init_crypt(struct aead_request *req, u8 *tag)
{
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct scatterlist *sg;
	u8 *iv = req->iv;
	int err;

	err = crypto_ccm_check_iv(iv);
	if (err)
		return err;

	pctx->flags = aead_request_flags(req);

	 /* Note: rfc 3610 and NIST 800-38C require counter of
	 * zero to encrypt auth tag.
	 */
	memset(iv + 15 - iv[0], 0, iv[0] + 1);

	sg_init_table(pctx->src, 3);
	sg_set_buf(pctx->src, tag, 16);
	sg = scatterwalk_ffwd(pctx->src + 1, req->src, req->assoclen);
	if (sg != pctx->src + 1)
		sg_chain(pctx->src, 2, sg);

	if (req->src != req->dst) {
		sg_init_table(pctx->dst, 3);
		sg_set_buf(pctx->dst, tag, 16);
		sg = scatterwalk_ffwd(pctx->dst + 1, req->dst, req->assoclen);
		if (sg != pctx->dst + 1)
			sg_chain(pctx->dst, 2, sg);
	}

	return 0;
}

static int crypto_ccm_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct skcipher_request *skreq = &pctx->skreq;
	struct scatterlist *dst;
	unsigned int cryptlen = req->cryptlen;
	u8 *odata = pctx->odata;
	u8 *iv = req->iv;
	int err;

	err = crypto_ccm_init_crypt(req, odata);
	if (err)
		return err;

	err = crypto_ccm_auth(req, sg_next(pctx->src), cryptlen);
	if (err)
		return err;

	dst = pctx->src;
	if (req->src != req->dst)
		dst = pctx->dst;

	skcipher_request_set_tfm(skreq, ctx->ctr);
	skcipher_request_set_callback(skreq, pctx->flags,
				      crypto_ccm_encrypt_done, req);
	skcipher_request_set_crypt(skreq, pctx->src, dst, cryptlen + 16, iv);
	err = crypto_skcipher_encrypt(skreq);
	if (err)
		return err;

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(odata, sg_next(dst), cryptlen,
				 crypto_aead_authsize(aead), 1);
	return err;
}

static void crypto_ccm_decrypt_done(struct crypto_async_request *areq,
				   int err)
{
	struct aead_request *req = areq->data;
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen - authsize;
	struct scatterlist *dst;

	pctx->flags = 0;

	dst = sg_next(req->src == req->dst ? pctx->src : pctx->dst);

	if (!err) {
		err = crypto_ccm_auth(req, dst, cryptlen);
		if (!err && crypto_memneq(pctx->auth_tag, pctx->odata, authsize))
			err = -EBADMSG;
	}
	aead_request_complete(req, err);
}

static int crypto_ccm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct skcipher_request *skreq = &pctx->skreq;
	struct scatterlist *dst;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen;
	u8 *authtag = pctx->auth_tag;
	u8 *odata = pctx->odata;
	u8 *iv = req->iv;
	int err;

	cryptlen -= authsize;

	err = crypto_ccm_init_crypt(req, authtag);
	if (err)
		return err;

	scatterwalk_map_and_copy(authtag, sg_next(pctx->src), cryptlen,
				 authsize, 0);

	dst = pctx->src;
	if (req->src != req->dst)
		dst = pctx->dst;

	skcipher_request_set_tfm(skreq, ctx->ctr);
	skcipher_request_set_callback(skreq, pctx->flags,
				      crypto_ccm_decrypt_done, req);
	skcipher_request_set_crypt(skreq, pctx->src, dst, cryptlen + 16, iv);
	err = crypto_skcipher_decrypt(skreq);
	if (err)
		return err;

	err = crypto_ccm_auth(req, sg_next(dst), cryptlen);
	if (err)
		return err;

	/* verify */
	if (crypto_memneq(authtag, odata, authsize))
		return -EBADMSG;

	return err;
}

static int crypto_ccm_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct ccm_instance_ctx *ictx = aead_instance_ctx(inst);
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_cipher *cipher;
	struct crypto_skcipher *ctr;
	unsigned long align;
	int err;

	cipher = crypto_spawn_cipher(&ictx->cipher);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctr = crypto_spawn_skcipher(&ictx->ctr);
	err = PTR_ERR(ctr);
	if (IS_ERR(ctr))
		goto err_free_cipher;

	ctx->cipher = cipher;
	ctx->ctr = ctr;

	align = crypto_aead_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(
		tfm,
		align + sizeof(struct crypto_ccm_req_priv_ctx) +
		crypto_skcipher_reqsize(ctr));

	return 0;

err_free_cipher:
	crypto_free_cipher(cipher);
	return err;
}

static void crypto_ccm_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_cipher(ctx->cipher);
	crypto_free_skcipher(ctx->ctr);
}

static void crypto_ccm_free(struct aead_instance *inst)
{
	struct ccm_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_spawn(&ctx->cipher);
	crypto_drop_skcipher(&ctx->ctr);
	kfree(inst);
}

static int crypto_ccm_create_common(struct crypto_template *tmpl,
				    struct rtattr **tb,
				    const char *full_name,
				    const char *ctr_name,
				    const char *cipher_name)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct skcipher_alg *ctr;
	struct crypto_alg *cipher;
	struct ccm_instance_ctx *ictx;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return -EINVAL;

	cipher = crypto_alg_mod_lookup(cipher_name,  CRYPTO_ALG_TYPE_CIPHER,
				       CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	err = -EINVAL;
	if (cipher->cra_blocksize != 16)
		goto out_put_cipher;

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_cipher;

	ictx = aead_instance_ctx(inst);

	err = crypto_init_spawn(&ictx->cipher, cipher,
				aead_crypto_instance(inst),
				CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ictx->ctr, aead_crypto_instance(inst));
	err = crypto_grab_skcipher(&ictx->ctr, ctr_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_cipher;

	ctr = crypto_spawn_skcipher_alg(&ictx->ctr);

	/* Not a stream cipher? */
	err = -EINVAL;
	if (ctr->base.cra_blocksize != 1)
		goto err_drop_ctr;

	/* We want the real thing! */
	if (crypto_skcipher_alg_ivsize(ctr) != 16)
		goto err_drop_ctr;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "ccm_base(%s,%s)", ctr->base.cra_driver_name,
		     cipher->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_ctr;

	memcpy(inst->alg.base.cra_name, full_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.base.cra_flags = ctr->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = (cipher->cra_priority +
				       ctr->base.cra_priority) / 2;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = cipher->cra_alignmask |
				       ctr->base.cra_alignmask |
				       (__alignof__(u32) - 1);
	inst->alg.ivsize = 16;
	inst->alg.chunksize = crypto_skcipher_alg_chunksize(ctr);
	inst->alg.maxauthsize = 16;
	inst->alg.base.cra_ctxsize = sizeof(struct crypto_ccm_ctx);
	inst->alg.init = crypto_ccm_init_tfm;
	inst->alg.exit = crypto_ccm_exit_tfm;
	inst->alg.setkey = crypto_ccm_setkey;
	inst->alg.setauthsize = crypto_ccm_setauthsize;
	inst->alg.encrypt = crypto_ccm_encrypt;
	inst->alg.decrypt = crypto_ccm_decrypt;

	inst->free = crypto_ccm_free;

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto err_drop_ctr;

out_put_cipher:
	crypto_mod_put(cipher);
	return err;

err_drop_ctr:
	crypto_drop_skcipher(&ictx->ctr);
err_drop_cipher:
	crypto_drop_spawn(&ictx->cipher);
err_free_inst:
	kfree(inst);
	goto out_put_cipher;
}

static int crypto_ccm_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	const char *cipher_name;
	char ctr_name[CRYPTO_MAX_ALG_NAME];
	char full_name[CRYPTO_MAX_ALG_NAME];

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)",
		     cipher_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "ccm(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return crypto_ccm_create_common(tmpl, tb, full_name, ctr_name,
					cipher_name);
}

static struct crypto_template crypto_ccm_tmpl = {
	.name = "ccm",
	.create = crypto_ccm_create,
	.module = THIS_MODULE,
};

static int crypto_ccm_base_create(struct crypto_template *tmpl,
				  struct rtattr **tb)
{
	const char *ctr_name;
	const char *cipher_name;
	char full_name[CRYPTO_MAX_ALG_NAME];

	ctr_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ctr_name))
		return PTR_ERR(ctr_name);

	cipher_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "ccm_base(%s,%s)",
		     ctr_name, cipher_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return crypto_ccm_create_common(tmpl, tb, full_name, ctr_name,
					cipher_name);
}

static struct crypto_template crypto_ccm_base_tmpl = {
	.name = "ccm_base",
	.create = crypto_ccm_base_create,
	.module = THIS_MODULE,
};

static int crypto_rfc4309_setkey(struct crypto_aead *parent, const u8 *key,
				 unsigned int keylen)
{
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(parent);
	struct crypto_aead *child = ctx->child;
	int err;

	if (keylen < 3)
		return -EINVAL;

	keylen -= 3;
	memcpy(ctx->nonce, key + keylen, 3);

	crypto_aead_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(child, crypto_aead_get_flags(parent) &
				     CRYPTO_TFM_REQ_MASK);
	err = crypto_aead_setkey(child, key, keylen);
	crypto_aead_set_flags(parent, crypto_aead_get_flags(child) &
				      CRYPTO_TFM_RES_MASK);

	return err;
}

static int crypto_rfc4309_setauthsize(struct crypto_aead *parent,
				      unsigned int authsize)
{
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(parent);

	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return crypto_aead_setauthsize(ctx->child, authsize);
}

static struct aead_request *crypto_rfc4309_crypt(struct aead_request *req)
{
	struct crypto_rfc4309_req_ctx *rctx = aead_request_ctx(req);
	struct aead_request *subreq = &rctx->subreq;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_aead *child = ctx->child;
	struct scatterlist *sg;
	u8 *iv = PTR_ALIGN((u8 *)(subreq + 1) + crypto_aead_reqsize(child),
			   crypto_aead_alignmask(child) + 1);

	/* L' */
	iv[0] = 3;

	memcpy(iv + 1, ctx->nonce, 3);
	memcpy(iv + 4, req->iv, 8);

	scatterwalk_map_and_copy(iv + 16, req->src, 0, req->assoclen - 8, 0);

	sg_init_table(rctx->src, 3);
	sg_set_buf(rctx->src, iv + 16, req->assoclen - 8);
	sg = scatterwalk_ffwd(rctx->src + 1, req->src, req->assoclen);
	if (sg != rctx->src + 1)
		sg_chain(rctx->src, 2, sg);

	if (req->src != req->dst) {
		sg_init_table(rctx->dst, 3);
		sg_set_buf(rctx->dst, iv + 16, req->assoclen - 8);
		sg = scatterwalk_ffwd(rctx->dst + 1, req->dst, req->assoclen);
		if (sg != rctx->dst + 1)
			sg_chain(rctx->dst, 2, sg);
	}

	aead_request_set_tfm(subreq, child);
	aead_request_set_callback(subreq, req->base.flags, req->base.complete,
				  req->base.data);
	aead_request_set_crypt(subreq, rctx->src,
			       req->src == req->dst ? rctx->src : rctx->dst,
			       req->cryptlen, iv);
	aead_request_set_ad(subreq, req->assoclen - 8);

	return subreq;
}

static int crypto_rfc4309_encrypt(struct aead_request *req)
{
	if (req->assoclen != 16 && req->assoclen != 20)
		return -EINVAL;

	req = crypto_rfc4309_crypt(req);

	return crypto_aead_encrypt(req);
}

static int crypto_rfc4309_decrypt(struct aead_request *req)
{
	if (req->assoclen != 16 && req->assoclen != 20)
		return -EINVAL;

	req = crypto_rfc4309_crypt(req);

	return crypto_aead_decrypt(req);
}

static int crypto_rfc4309_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct crypto_aead_spawn *spawn = aead_instance_ctx(inst);
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_aead *aead;
	unsigned long align;

	aead = crypto_spawn_aead(spawn);
	if (IS_ERR(aead))
		return PTR_ERR(aead);

	ctx->child = aead;

	align = crypto_aead_alignmask(aead);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(
		tfm,
		sizeof(struct crypto_rfc4309_req_ctx) +
		ALIGN(crypto_aead_reqsize(aead), crypto_tfm_ctx_alignment()) +
		align + 32);

	return 0;
}

static void crypto_rfc4309_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static void crypto_rfc4309_free(struct aead_instance *inst)
{
	crypto_drop_aead(aead_instance_ctx(inst));
	kfree(inst);
}

static int crypto_rfc4309_create(struct crypto_template *tmpl,
				 struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	const char *ccm_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return -EINVAL;

	ccm_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ccm_name))
		return PTR_ERR(ccm_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = aead_instance_ctx(inst);
	crypto_set_aead_spawn(spawn, aead_crypto_instance(inst));
	err = crypto_grab_aead(spawn, ccm_name, 0,
			       crypto_requires_sync(algt->type, algt->mask));
	if (err)
		goto out_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	err = -EINVAL;

	/* We only support 16-byte blocks. */
	if (crypto_aead_alg_ivsize(alg) != 16)
		goto out_drop_alg;

	/* Not a stream cipher? */
	if (alg->base.cra_blocksize != 1)
		goto out_drop_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4309(%s)", alg->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4309(%s)", alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_drop_alg;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;

	inst->alg.ivsize = 8;
	inst->alg.chunksize = crypto_aead_alg_chunksize(alg);
	inst->alg.maxauthsize = 16;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_rfc4309_ctx);

	inst->alg.init = crypto_rfc4309_init_tfm;
	inst->alg.exit = crypto_rfc4309_exit_tfm;

	inst->alg.setkey = crypto_rfc4309_setkey;
	inst->alg.setauthsize = crypto_rfc4309_setauthsize;
	inst->alg.encrypt = crypto_rfc4309_encrypt;
	inst->alg.decrypt = crypto_rfc4309_decrypt;

	inst->free = crypto_rfc4309_free;

	err = aead_register_instance(tmpl, inst);
	if (err)
		goto out_drop_alg;

out:
	return err;

out_drop_alg:
	crypto_drop_aead(spawn);
out_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_rfc4309_tmpl = {
	.name = "rfc4309",
	.create = crypto_rfc4309_create,
	.module = THIS_MODULE,
};

static int __init crypto_ccm_module_init(void)
{
	int err;

	err = crypto_register_template(&crypto_ccm_base_tmpl);
	if (err)
		goto out;

	err = crypto_register_template(&crypto_ccm_tmpl);
	if (err)
		goto out_undo_base;

	err = crypto_register_template(&crypto_rfc4309_tmpl);
	if (err)
		goto out_undo_ccm;

out:
	return err;

out_undo_ccm:
	crypto_unregister_template(&crypto_ccm_tmpl);
out_undo_base:
	crypto_unregister_template(&crypto_ccm_base_tmpl);
	goto out;
}

static void __exit crypto_ccm_module_exit(void)
{
	crypto_unregister_template(&crypto_rfc4309_tmpl);
	crypto_unregister_template(&crypto_ccm_tmpl);
	crypto_unregister_template(&crypto_ccm_base_tmpl);
}

module_init(crypto_ccm_module_init);
module_exit(crypto_ccm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Counter with CBC MAC");
MODULE_ALIAS_CRYPTO("ccm_base");
MODULE_ALIAS_CRYPTO("rfc4309");
MODULE_ALIAS_CRYPTO("ccm");
