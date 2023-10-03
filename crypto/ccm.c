// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CCM: Counter with CBC-MAC
 *
 * (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

struct ccm_instance_ctx {
	struct crypto_skcipher_spawn ctr;
	struct crypto_ahash_spawn mac;
};

struct crypto_ccm_ctx {
	struct crypto_ahash *mac;
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
	u32 flags;
	struct scatterlist src[3];
	struct scatterlist dst[3];
	union {
		struct ahash_request ahreq;
		struct skcipher_request skreq;
	};
};

struct cbcmac_tfm_ctx {
	struct crypto_cipher *child;
};

struct cbcmac_desc_ctx {
	unsigned int len;
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
	struct crypto_ahash *mac = ctx->mac;
	int err;

	crypto_skcipher_clear_flags(ctr, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(ctr, crypto_aead_get_flags(aead) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(ctr, key, keylen);
	if (err)
		return err;

	crypto_ahash_clear_flags(mac, CRYPTO_TFM_REQ_MASK);
	crypto_ahash_set_flags(mac, crypto_aead_get_flags(aead) &
				    CRYPTO_TFM_REQ_MASK);
	return crypto_ahash_setkey(mac, key, keylen);
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

static int crypto_ccm_auth(struct aead_request *req, struct scatterlist *plain,
			   unsigned int cryptlen)
{
	struct crypto_ccm_req_priv_ctx *pctx = crypto_ccm_reqctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(aead);
	struct ahash_request *ahreq = &pctx->ahreq;
	unsigned int assoclen = req->assoclen;
	struct scatterlist sg[3];
	u8 *odata = pctx->odata;
	u8 *idata = pctx->idata;
	int ilen, err;

	/* format control data for input */
	err = format_input(odata, req, cryptlen);
	if (err)
		goto out;

	sg_init_table(sg, 3);
	sg_set_buf(&sg[0], odata, 16);

	/* format associated data and compute into mac */
	if (assoclen) {
		ilen = format_adata(idata, assoclen);
		sg_set_buf(&sg[1], idata, ilen);
		sg_chain(sg, 3, req->src);
	} else {
		ilen = 0;
		sg_chain(sg, 2, req->src);
	}

	ahash_request_set_tfm(ahreq, ctx->mac);
	ahash_request_set_callback(ahreq, pctx->flags, NULL, NULL);
	ahash_request_set_crypt(ahreq, sg, NULL, assoclen + ilen + 16);
	err = crypto_ahash_init(ahreq);
	if (err)
		goto out;
	err = crypto_ahash_update(ahreq);
	if (err)
		goto out;

	/* we need to pad the MAC input to a round multiple of the block size */
	ilen = 16 - (assoclen + ilen) % 16;
	if (ilen < 16) {
		memset(idata, 0, ilen);
		sg_init_table(sg, 2);
		sg_set_buf(&sg[0], idata, ilen);
		if (plain)
			sg_chain(sg, 2, plain);
		plain = sg;
		cryptlen += ilen;
	}

	ahash_request_set_crypt(ahreq, plain, odata, cryptlen);
	err = crypto_ahash_finup(ahreq);
out:
	return err;
}

static void crypto_ccm_encrypt_done(void *data, int err)
{
	struct aead_request *req = data;
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

static void crypto_ccm_decrypt_done(void *data, int err)
{
	struct aead_request *req = data;
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
	u8 *iv = pctx->idata;
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

	memcpy(iv, req->iv, 16);

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
	struct crypto_ahash *mac;
	struct crypto_skcipher *ctr;
	unsigned long align;
	int err;

	mac = crypto_spawn_ahash(&ictx->mac);
	if (IS_ERR(mac))
		return PTR_ERR(mac);

	ctr = crypto_spawn_skcipher(&ictx->ctr);
	err = PTR_ERR(ctr);
	if (IS_ERR(ctr))
		goto err_free_mac;

	ctx->mac = mac;
	ctx->ctr = ctr;

	align = crypto_aead_alignmask(tfm);
	align &= ~(crypto_tfm_ctx_alignment() - 1);
	crypto_aead_set_reqsize(
		tfm,
		align + sizeof(struct crypto_ccm_req_priv_ctx) +
		max(crypto_ahash_reqsize(mac), crypto_skcipher_reqsize(ctr)));

	return 0;

err_free_mac:
	crypto_free_ahash(mac);
	return err;
}

static void crypto_ccm_exit_tfm(struct crypto_aead *tfm)
{
	struct crypto_ccm_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_ahash(ctx->mac);
	crypto_free_skcipher(ctx->ctr);
}

static void crypto_ccm_free(struct aead_instance *inst)
{
	struct ccm_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_ahash(&ctx->mac);
	crypto_drop_skcipher(&ctx->ctr);
	kfree(inst);
}

static int crypto_ccm_create_common(struct crypto_template *tmpl,
				    struct rtattr **tb,
				    const char *ctr_name,
				    const char *mac_name)
{
	struct skcipher_alg_common *ctr;
	u32 mask;
	struct aead_instance *inst;
	struct ccm_instance_ctx *ictx;
	struct hash_alg_common *mac;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	ictx = aead_instance_ctx(inst);

	err = crypto_grab_ahash(&ictx->mac, aead_crypto_instance(inst),
				mac_name, 0, mask | CRYPTO_ALG_ASYNC);
	if (err)
		goto err_free_inst;
	mac = crypto_spawn_ahash_alg(&ictx->mac);

	err = -EINVAL;
	if (strncmp(mac->base.cra_name, "cbcmac(", 7) != 0 ||
	    mac->digestsize != 16)
		goto err_free_inst;

	err = crypto_grab_skcipher(&ictx->ctr, aead_crypto_instance(inst),
				   ctr_name, 0, mask);
	if (err)
		goto err_free_inst;
	ctr = crypto_spawn_skcipher_alg_common(&ictx->ctr);

	/* The skcipher algorithm must be CTR mode, using 16-byte blocks. */
	err = -EINVAL;
	if (strncmp(ctr->base.cra_name, "ctr(", 4) != 0 ||
	    ctr->ivsize != 16 || ctr->base.cra_blocksize != 1)
		goto err_free_inst;

	/* ctr and cbcmac must use the same underlying block cipher. */
	if (strcmp(ctr->base.cra_name + 4, mac->base.cra_name + 7) != 0)
		goto err_free_inst;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "ccm(%s", ctr->base.cra_name + 4) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "ccm_base(%s,%s)", ctr->base.cra_driver_name,
		     mac->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = (mac->base.cra_priority +
				       ctr->base.cra_priority) / 2;
	inst->alg.base.cra_blocksize = 1;
	inst->alg.base.cra_alignmask = mac->base.cra_alignmask |
				       ctr->base.cra_alignmask;
	inst->alg.ivsize = 16;
	inst->alg.chunksize = ctr->chunksize;
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
	if (err) {
err_free_inst:
		crypto_ccm_free(inst);
	}
	return err;
}

static int crypto_ccm_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	const char *cipher_name;
	char ctr_name[CRYPTO_MAX_ALG_NAME];
	char mac_name[CRYPTO_MAX_ALG_NAME];

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	if (snprintf(ctr_name, CRYPTO_MAX_ALG_NAME, "ctr(%s)",
		     cipher_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	if (snprintf(mac_name, CRYPTO_MAX_ALG_NAME, "cbcmac(%s)",
		     cipher_name) >= CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	return crypto_ccm_create_common(tmpl, tb, ctr_name, mac_name);
}

static int crypto_ccm_base_create(struct crypto_template *tmpl,
				  struct rtattr **tb)
{
	const char *ctr_name;
	const char *mac_name;

	ctr_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(ctr_name))
		return PTR_ERR(ctr_name);

	mac_name = crypto_attr_alg_name(tb[2]);
	if (IS_ERR(mac_name))
		return PTR_ERR(mac_name);

	return crypto_ccm_create_common(tmpl, tb, ctr_name, mac_name);
}

static int crypto_rfc4309_setkey(struct crypto_aead *parent, const u8 *key,
				 unsigned int keylen)
{
	struct crypto_rfc4309_ctx *ctx = crypto_aead_ctx(parent);
	struct crypto_aead *child = ctx->child;

	if (keylen < 3)
		return -EINVAL;

	keylen -= 3;
	memcpy(ctx->nonce, key + keylen, 3);

	crypto_aead_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(child, crypto_aead_get_flags(parent) &
				     CRYPTO_TFM_REQ_MASK);
	return crypto_aead_setkey(child, key, keylen);
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
	u32 mask;
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = aead_instance_ctx(inst);
	err = crypto_grab_aead(spawn, aead_crypto_instance(inst),
			       crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;

	alg = crypto_spawn_aead_alg(spawn);

	err = -EINVAL;

	/* We only support 16-byte blocks. */
	if (crypto_aead_alg_ivsize(alg) != 16)
		goto err_free_inst;

	/* Not a stream cipher? */
	if (alg->base.cra_blocksize != 1)
		goto err_free_inst;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4309(%s)", alg->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME ||
	    snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "rfc4309(%s)", alg->base.cra_driver_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

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
	if (err) {
err_free_inst:
		crypto_rfc4309_free(inst);
	}
	return err;
}

static int crypto_cbcmac_digest_setkey(struct crypto_shash *parent,
				     const u8 *inkey, unsigned int keylen)
{
	struct cbcmac_tfm_ctx *ctx = crypto_shash_ctx(parent);

	return crypto_cipher_setkey(ctx->child, inkey, keylen);
}

static int crypto_cbcmac_digest_init(struct shash_desc *pdesc)
{
	struct cbcmac_desc_ctx *ctx = shash_desc_ctx(pdesc);
	int bs = crypto_shash_digestsize(pdesc->tfm);
	u8 *dg = (u8 *)ctx + crypto_shash_descsize(pdesc->tfm) - bs;

	ctx->len = 0;
	memset(dg, 0, bs);

	return 0;
}

static int crypto_cbcmac_digest_update(struct shash_desc *pdesc, const u8 *p,
				       unsigned int len)
{
	struct crypto_shash *parent = pdesc->tfm;
	struct cbcmac_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct cbcmac_desc_ctx *ctx = shash_desc_ctx(pdesc);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_digestsize(parent);
	u8 *dg = (u8 *)ctx + crypto_shash_descsize(parent) - bs;

	while (len > 0) {
		unsigned int l = min(len, bs - ctx->len);

		crypto_xor(dg + ctx->len, p, l);
		ctx->len +=l;
		len -= l;
		p += l;

		if (ctx->len == bs) {
			crypto_cipher_encrypt_one(tfm, dg, dg);
			ctx->len = 0;
		}
	}

	return 0;
}

static int crypto_cbcmac_digest_final(struct shash_desc *pdesc, u8 *out)
{
	struct crypto_shash *parent = pdesc->tfm;
	struct cbcmac_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct cbcmac_desc_ctx *ctx = shash_desc_ctx(pdesc);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_digestsize(parent);
	u8 *dg = (u8 *)ctx + crypto_shash_descsize(parent) - bs;

	if (ctx->len)
		crypto_cipher_encrypt_one(tfm, dg, dg);

	memcpy(out, dg, bs);
	return 0;
}

static int cbcmac_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_cipher *cipher;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_cipher_spawn *spawn = crypto_instance_ctx(inst);
	struct cbcmac_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	return 0;
};

static void cbcmac_exit_tfm(struct crypto_tfm *tfm)
{
	struct cbcmac_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_cipher(ctx->child);
}

static int cbcmac_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_cipher_spawn *spawn;
	struct crypto_alg *alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH, &mask);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	spawn = shash_instance_ctx(inst);

	err = crypto_grab_cipher(spawn, shash_crypto_instance(inst),
				 crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;
	alg = crypto_spawn_cipher_alg(spawn);

	err = crypto_inst_setname(shash_crypto_instance(inst), tmpl->name, alg);
	if (err)
		goto err_free_inst;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = 1;

	inst->alg.digestsize = alg->cra_blocksize;
	inst->alg.descsize = ALIGN(sizeof(struct cbcmac_desc_ctx),
				   alg->cra_alignmask + 1) +
			     alg->cra_blocksize;

	inst->alg.base.cra_ctxsize = sizeof(struct cbcmac_tfm_ctx);
	inst->alg.base.cra_init = cbcmac_init_tfm;
	inst->alg.base.cra_exit = cbcmac_exit_tfm;

	inst->alg.init = crypto_cbcmac_digest_init;
	inst->alg.update = crypto_cbcmac_digest_update;
	inst->alg.final = crypto_cbcmac_digest_final;
	inst->alg.setkey = crypto_cbcmac_digest_setkey;

	inst->free = shash_free_singlespawn_instance;

	err = shash_register_instance(tmpl, inst);
	if (err) {
err_free_inst:
		shash_free_singlespawn_instance(inst);
	}
	return err;
}

static struct crypto_template crypto_ccm_tmpls[] = {
	{
		.name = "cbcmac",
		.create = cbcmac_create,
		.module = THIS_MODULE,
	}, {
		.name = "ccm_base",
		.create = crypto_ccm_base_create,
		.module = THIS_MODULE,
	}, {
		.name = "ccm",
		.create = crypto_ccm_create,
		.module = THIS_MODULE,
	}, {
		.name = "rfc4309",
		.create = crypto_rfc4309_create,
		.module = THIS_MODULE,
	},
};

static int __init crypto_ccm_module_init(void)
{
	return crypto_register_templates(crypto_ccm_tmpls,
					 ARRAY_SIZE(crypto_ccm_tmpls));
}

static void __exit crypto_ccm_module_exit(void)
{
	crypto_unregister_templates(crypto_ccm_tmpls,
				    ARRAY_SIZE(crypto_ccm_tmpls));
}

subsys_initcall(crypto_ccm_module_init);
module_exit(crypto_ccm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Counter with CBC MAC");
MODULE_ALIAS_CRYPTO("ccm_base");
MODULE_ALIAS_CRYPTO("rfc4309");
MODULE_ALIAS_CRYPTO("ccm");
MODULE_ALIAS_CRYPTO("cbcmac");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
