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
	struct crypto_ablkcipher *ctr;
};

struct crypto_rfc4309_ctx {
	struct crypto_aead *child;
	u8 nonce[3];
};

struct crypto_ccm_req_priv_ctx {
	u8 odata[16];
	u8 idata[16];
	u8 auth_tag[16];
	u32 ilen;
	u32 flags;
	struct scatterlist src[2];
	struct scatterlist dst[2];
	struct ablkcipher_request abreq;
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
	struct crypto_ablkcipher *ctr = ctx->ctr;
	struct crypto_cipher *tfm = ctx->cipher;
	int err = 0;

	crypto_ablkcipher_clear_flags(ctr, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(ctr, crypto_aead_get_flags(aead) &
				    CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(ctr, key, keylen);
	crypto_aead_set_flags(aead, crypto_ablkcipher_get_flags(ctr) &
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
		get_data_to_compute(cipher, pctx, req->assoc, req->assoclen);
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
		scatterwalk_map_and_copy(odata, req->dst, req->cryptlen,
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

static int crypto_ccm_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct ablkcipher_request *abreq = &pctx->abreq;
	struct scatterlist *dst;
	unsigned int cryptlen = req->cryptlen;
	u8 *odata = pctx->odata;
	u8 *iv = req->iv;
	int err;

	err = crypto_ccm_check_iv(iv);
	if (err)
		return err;

	pctx->flags = aead_request_flags(req);

	err = crypto_ccm_auth(req, req->src, cryptlen);
	if (err)
		return err;

	 /* Note: rfc 3610 and NIST 800-38C require counter of
	 * zero to encrypt auth tag.
	 */
	memset(iv + 15 - iv[0], 0, iv[0] + 1);

	sg_init_table(pctx->src, 2);
	sg_set_buf(pctx->src, odata, 16);
	scatterwalk_sg_chain(pctx->src, 2, req->src);

	dst = pctx->src;
	if (req->src != req->dst) {
		sg_init_table(pctx->dst, 2);
		sg_set_buf(pctx->dst, odata, 16);
		scatterwalk_sg_chain(pctx->dst, 2, req->dst);
		dst = pctx->dst;
	}

	ablkcipher_request_set_tfm(abreq, ctx->ctr);
	ablkcipher_request_set_callback(abreq, pctx->flags,
					crypto_ccm_encrypt_done, req);
	ablkcipher_request_set_crypt(abreq, pctx->src, dst, cryptlen + 16, iv);
	err = crypto_ablkcipher_encrypt(abreq);
	if (err)
		return err;

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(odata, req->dst, cryptlen,
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

	if (!err) {
		err = crypto_ccm_auth(req, req->dst, cryptlen);
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
	struct ablkcipher_request *abreq = &pctx->abreq;
	struct scatterlist *dst;
	unsigned int authsize = crypto_aead_authsize(aead);
	unsigned int cryptlen = req->cryptlen;
	u8 *authtag = pctx->auth_tag;
	u8 *odata = pctx->odata;
	u8 *iv = req->iv;
	int err;

	if (cryptlen < authsize)
		return -EINVAL;
	cryptlen -= authsize;

	err = crypto_ccm_check_iv(iv);
	if (err)
		return err;

	pctx->flags = aead_request_flags(req);

	scatterwalk_map_and_copy(authtag, req->src, cryptlen, authsize, 0);

	memset(iv + 15 - iv[0], 0, iv[0] + 1);

	sg_init_table(pctx->src, 2);
	sg_set_buf(pctx->src, authtag, 16);
	scatterwalk_sg_chain(pctx->src, 2, req->src);

	dst = pctx->src;
	if (req->src != req->dst) {
		sg_init_table(pctx->dst, 2);
		sg_set_buf(pctx->dst, authtag, 16);
		scatterwalk_sg_chain(pctx->dst, 2, req->dst);
		dst = pctx->dst;
	}

	ablkcipher_request_set_tfm(abreq, ctx->ctr);
	ablkcipher_request_set_callback(abreq, pctx->flags,
					crypto_ccm_decrypt_done, req);
	ablkcipher_request_set_crypt(abreq, pctx->src, dst, cryptlen + 16, iv);
	err = crypto_ablkcipher_decrypt(abreq);
	if (err)
		return err;

	err = crypto_ccm_auth(req, req->dst, cryptlen);
	if (err)
		return err;

	/* verify */
	if (crypto_memneq(authtag, odata, authsize))
		return -EBADMSG;

	return err;
}

static int crypto_ccm_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct ccm_instance_ctx *ictx = crypto_instance_ctx(inst);
	struct crypto_ccm_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_cipher *cipher;
	struct crypto_ablkcipher *ctr;
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

	align = crypto_tfm_alg_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = align +
				sizeof(struct crypto_ccm_req_priv_ctx) +
				crypto_ablkcipher_reqsize(ctr);

	return 0;

err_free_cipher:
	crypto_free_cipher(cipher);
	return err;
}

static void crypto_ccm_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_ccm_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(ctx->cipher);
	crypto_free_ablkcipher(ctx->ctr);
}

static struct crypto_instance *crypto_ccm_alloc_common(struct rtattr **tb,
						       const char *full_name,
						       const char *ctr_name,
						       const char *cipher_name)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_alg *ctr;
	struct crypto_alg *cipher;
	struct ccm_instance_ctx *ictx;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	cipher = crypto_alg_mod_lookup(cipher_name,  CRYPTO_ALG_TYPE_CIPHER,
				       CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(cipher))
		return ERR_CAST(cipher);

	err = -EINVAL;
	if (cipher->cra_blocksize != 16)
		goto out_put_cipher;

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	err = -ENOMEM;
	if (!inst)
		goto out_put_cipher;

	ictx = crypto_instance_ctx(inst);

	err = crypto_init_spawn(&ictx->cipher, cipher, inst,
				CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto err_free_inst;

	crypto_set_skcipher_spawn(&ictx->ctr, inst);
	err = crypto_grab_skcipher(&ictx->ctr, ctr_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err)
		goto err_drop_cipher;

	ctr = crypto_skcipher_spawn_alg(&ictx->ctr);

	/* Not a stream cipher? */
	err = -EINVAL;
	if (ctr->cra_blocksize != 1)
		goto err_drop_ctr;

	/* We want the real thing! */
	if (ctr->cra_ablkcipher.ivsize != 16)
		goto err_drop_ctr;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "ccm_base(%s,%s)", ctr->cra_driver_name,
		     cipher->cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_drop_ctr;

	memcpy(inst->alg.cra_name, full_name, CRYPTO_MAX_ALG_NAME);

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= ctr->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = cipher->cra_priority + ctr->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = cipher->cra_alignmask | ctr->cra_alignmask |
				  (__alignof__(u32) - 1);
	inst->alg.cra_type = &crypto_aead_type;
	inst->alg.cra_aead.ivsize = 16;
	inst->alg.cra_aead.maxauthsize = 16;
	inst->alg.cra_ctxsize = sizeof(struct crypto_ccm_ctx);
	inst->alg.cra_init = crypto_ccm_init_tfm;
	inst->alg.cra_exit = crypto_ccm_exit_tfm;
	inst->alg.cra_aead.setkey = crypto_ccm_setkey;
	inst->alg.cra_aead.setauthsize = crypto_ccm_setauthsize;
	inst->alg.cra_aead.encrypt = crypto_ccm_encrypt;
	inst->alg.cra_aead.decrypt = crypto_ccm_decrypt;

out:
	crypto_mod_put(cipher);
	return inst;

err_drop_ctr:
	crypto_drop_skcipher(&ictx->ctr);
err_drop_cipher:
	crypto_drop_spawn(&ictx->cipher);
err_free_inst:
	kfree(inst);
out_put_cipher:
	inst = ERR_PTR(err);
	goto out;
}

static struct crypto_instance *crypto_ccm_alloc(struct rtattr **tb)
{
	const char *cipher_name;
	char ctr_name[CRYPTO_MAX_ALG_NAME];
	char full_name[CRYPTO_MAX_ALG_NAME];

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return ERR_CAST(cipher_name);

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)",
		     cipher_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "ccm(%s)", cipher_name) >=
	    CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	return crypto_ccm_alloc_common(tb, full_name, ctr_name, cipher_name);
}

static void crypto_ccm_free(struct crypto_instance *inst)
{
	struct ccm_instance_ctx *ctx = crypto_instance_ctx(inst);

	crypto_drop_spawn(&ctx->cipher);
	crypto_drop_skcipher(&ctx->ctr);
	kfree(inst);
}

static struct crypto_template crypto_ccm_tmpl = {
	.name = "ccm",
	.alloc = crypto_ccm_alloc,
	.free = crypto_ccm_free,
	.module = THIS_MODULE,
};

static struct crypto_instance *crypto_ccm_base_alloc(struct rtattr **tb)
{
	const char *ctr_name;
	const char *cipher_name;
	char full_name[CRYPTO_MAX_ALG_NAME];

	ctr_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ctr_name))
		return ERR_CAST(ctr_name);

	cipher_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(cipher_name))
		return ERR_CAST(cipher_name);

	if (snprintf(full_name, CRYPTO_MAX_ALG_NAME, "ccm_base(%s,%s)",
		     ctr_name, cipher_name) >= CRYPTO_MAX_ALG_NAME)
		return ERR_PTR(-ENAMETOOLONG);

	return crypto_ccm_alloc_common(tb, full_name, ctr_name, cipher_name);
}

static struct crypto_template crypto_ccm_base_tmpl = {
	.name = "ccm_base",
	.alloc = crypto_ccm_base_alloc,
	.free = crypto_ccm_free,
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
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(aead);
	struct crypto_aead *child = ctx->child;
	u8 *iv = PTR_ALIGN((u8 *)(subreq + 1) + crypto_aead_reqsize(child),
			   crypto_aead_alignmask(child) + 1);

	/* L' */
	iv[0] = 3;

	memcpy(iv + 1, ctx->nonce, 3);
	memcpy(iv + 4, req->iv, 8);

	aead_request_set_tfm(subreq, child);
	aead_request_set_callback(subreq, req->base.flags, req->base.complete,
				  req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen, iv);
	aead_request_set_assoc(subreq, req->assoc, req->assoclen);

	return subreq;
}

static int crypto_rfc4309_encrypt(struct aead_request *req)
{
	req = crypto_rfc4309_crypt(req);

	return crypto_aead_encrypt(req);
}

static int crypto_rfc4309_decrypt(struct aead_request *req)
{
	req = crypto_rfc4309_crypt(req);

	return crypto_aead_decrypt(req);
}

static int crypto_rfc4309_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_aead_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_rfc4309_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_aead *aead;
	unsigned long align;

	aead = crypto_spawn_aead(spawn);
	if (IS_ERR(aead))
		return PTR_ERR(aead);

	ctx->child = aead;

	align = crypto_aead_alignmask(aead);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	tfm->crt_aead.reqsize = sizeof(struct aead_request) +
				ALIGN(crypto_aead_reqsize(aead),
				      crypto_tfm_ctx_alignment()) +
				align + 16;

	return 0;
}

static void crypto_rfc4309_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_rfc4309_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static struct crypto_instance *crypto_rfc4309_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct crypto_alg *alg;
	const char *ccm_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & algt->mask)
		return ERR_PTR(-EINVAL);

	ccm_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ccm_name))
		return ERR_CAST(ccm_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);

	spawn = crypto_instance_ctx(inst);
	crypto_set_aead_spawn(spawn, inst);
	err = crypto_grab_aead(spawn, ccm_name, 0,
			       crypto_requires_sync(algt->type, algt->mask));
	if (err)
		goto out_free_inst;

	alg = crypto_aead_spawn_alg(spawn);

	err = -EINVAL;

	/* We only support 16-byte blocks. */
	if (alg->cra_aead.ivsize != 16)
		goto out_drop_alg;

	/* Not a stream cipher? */
	if (alg->cra_blocksize != 1)
		goto out_drop_alg;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4309(%s)", alg->cra_name) >= CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4309(%s)", alg->cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto out_drop_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_AEAD;
	inst->alg.cra_flags |= alg->cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = 1;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_nivaead_type;

	inst->alg.cra_aead.ivsize = 8;
	inst->alg.cra_aead.maxauthsize = 16;

	inst->alg.cra_ctxsize = sizeof(struct crypto_rfc4309_ctx);

	inst->alg.cra_init = crypto_rfc4309_init_tfm;
	inst->alg.cra_exit = crypto_rfc4309_exit_tfm;

	inst->alg.cra_aead.setkey = crypto_rfc4309_setkey;
	inst->alg.cra_aead.setauthsize = crypto_rfc4309_setauthsize;
	inst->alg.cra_aead.encrypt = crypto_rfc4309_encrypt;
	inst->alg.cra_aead.decrypt = crypto_rfc4309_decrypt;

	inst->alg.cra_aead.geniv = "seqiv";

out:
	return inst;

out_drop_alg:
	crypto_drop_aead(spawn);
out_free_inst:
	kfree(inst);
	inst = ERR_PTR(err);
	goto out;
}

static void crypto_rfc4309_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_rfc4309_tmpl = {
	.name = "rfc4309",
	.alloc = crypto_rfc4309_alloc,
	.free = crypto_rfc4309_free,
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
