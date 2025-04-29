// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AEAD wrapper for Kerberos 5 RFC3961 simplified profile.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * Derived from authenc:
 * Copyright (c) 2007-2015 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct krb5enc_instance_ctx {
	struct crypto_ahash_spawn auth;
	struct crypto_skcipher_spawn enc;
	unsigned int reqoff;
};

struct krb5enc_ctx {
	struct crypto_ahash *auth;
	struct crypto_skcipher *enc;
};

struct krb5enc_request_ctx {
	struct scatterlist src[2];
	struct scatterlist dst[2];
	char tail[];
};

static void krb5enc_request_complete(struct aead_request *req, int err)
{
	if (err != -EINPROGRESS)
		aead_request_complete(req, err);
}

/**
 * crypto_krb5enc_extractkeys - Extract Ke and Ki keys from the key blob.
 * @keys: Where to put the key sizes and pointers
 * @key: Encoded key material
 * @keylen: Amount of key material
 *
 * Decode the key blob we're given.  It starts with an rtattr that indicates
 * the format and the length.  Format CRYPTO_AUTHENC_KEYA_PARAM is:
 *
 *	rtattr || __be32 enckeylen || authkey || enckey
 *
 * Note that the rtattr is in cpu-endian form, unlike enckeylen.  This must be
 * handled correctly in static testmgr data.
 */
int crypto_krb5enc_extractkeys(struct crypto_authenc_keys *keys, const u8 *key,
			       unsigned int keylen)
{
	struct rtattr *rta = (struct rtattr *)key;
	struct crypto_authenc_key_param *param;

	if (!RTA_OK(rta, keylen))
		return -EINVAL;
	if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
		return -EINVAL;

	/*
	 * RTA_OK() didn't align the rtattr's payload when validating that it
	 * fits in the buffer.  Yet, the keys should start on the next 4-byte
	 * aligned boundary.  To avoid confusion, require that the rtattr
	 * payload be exactly the param struct, which has a 4-byte aligned size.
	 */
	if (RTA_PAYLOAD(rta) != sizeof(*param))
		return -EINVAL;
	BUILD_BUG_ON(sizeof(*param) % RTA_ALIGNTO);

	param = RTA_DATA(rta);
	keys->enckeylen = be32_to_cpu(param->enckeylen);

	key += rta->rta_len;
	keylen -= rta->rta_len;

	if (keylen < keys->enckeylen)
		return -EINVAL;

	keys->authkeylen = keylen - keys->enckeylen;
	keys->authkey = key;
	keys->enckey = key + keys->authkeylen;
	return 0;
}
EXPORT_SYMBOL(crypto_krb5enc_extractkeys);

static int krb5enc_setkey(struct crypto_aead *krb5enc, const u8 *key,
			  unsigned int keylen)
{
	struct crypto_authenc_keys keys;
	struct krb5enc_ctx *ctx = crypto_aead_ctx(krb5enc);
	struct crypto_skcipher *enc = ctx->enc;
	struct crypto_ahash *auth = ctx->auth;
	unsigned int flags = crypto_aead_get_flags(krb5enc);
	int err = -EINVAL;

	if (crypto_krb5enc_extractkeys(&keys, key, keylen) != 0)
		goto out;

	crypto_ahash_clear_flags(auth, CRYPTO_TFM_REQ_MASK);
	crypto_ahash_set_flags(auth, flags & CRYPTO_TFM_REQ_MASK);
	err = crypto_ahash_setkey(auth, keys.authkey, keys.authkeylen);
	if (err)
		goto out;

	crypto_skcipher_clear_flags(enc, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(enc, flags & CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(enc, keys.enckey, keys.enckeylen);
out:
	memzero_explicit(&keys, sizeof(keys));
	return err;
}

static void krb5enc_encrypt_done(void *data, int err)
{
	struct aead_request *req = data;

	krb5enc_request_complete(req, err);
}

/*
 * Start the encryption of the plaintext.  We skip over the associated data as
 * that only gets included in the hash.
 */
static int krb5enc_dispatch_encrypt(struct aead_request *req,
				    unsigned int flags)
{
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);
	struct aead_instance *inst = aead_alg_instance(krb5enc);
	struct krb5enc_ctx *ctx = crypto_aead_ctx(krb5enc);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct krb5enc_request_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_skcipher *enc = ctx->enc;
	struct skcipher_request *skreq = (void *)(areq_ctx->tail +
						  ictx->reqoff);
	struct scatterlist *src, *dst;

	src = scatterwalk_ffwd(areq_ctx->src, req->src, req->assoclen);
	if (req->src == req->dst)
		dst = src;
	else
		dst = scatterwalk_ffwd(areq_ctx->dst, req->dst, req->assoclen);

	skcipher_request_set_tfm(skreq, enc);
	skcipher_request_set_callback(skreq, aead_request_flags(req),
				      krb5enc_encrypt_done, req);
	skcipher_request_set_crypt(skreq, src, dst, req->cryptlen, req->iv);

	return crypto_skcipher_encrypt(skreq);
}

/*
 * Insert the hash into the checksum field in the destination buffer directly
 * after the encrypted region.
 */
static void krb5enc_insert_checksum(struct aead_request *req, u8 *hash)
{
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);

	scatterwalk_map_and_copy(hash, req->dst,
				 req->assoclen + req->cryptlen,
				 crypto_aead_authsize(krb5enc), 1);
}

/*
 * Upon completion of an asynchronous digest, transfer the hash to the checksum
 * field.
 */
static void krb5enc_encrypt_ahash_done(void *data, int err)
{
	struct aead_request *req = data;
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);
	struct aead_instance *inst = aead_alg_instance(krb5enc);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct krb5enc_request_ctx *areq_ctx = aead_request_ctx(req);
	struct ahash_request *ahreq = (void *)(areq_ctx->tail + ictx->reqoff);

	if (err)
		return krb5enc_request_complete(req, err);

	krb5enc_insert_checksum(req, ahreq->result);

	err = krb5enc_dispatch_encrypt(req, 0);
	if (err != -EINPROGRESS)
		aead_request_complete(req, err);
}

/*
 * Start the digest of the plaintext for encryption.  In theory, this could be
 * run in parallel with the encryption, provided the src and dst buffers don't
 * overlap.
 */
static int krb5enc_dispatch_encrypt_hash(struct aead_request *req)
{
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);
	struct aead_instance *inst = aead_alg_instance(krb5enc);
	struct krb5enc_ctx *ctx = crypto_aead_ctx(krb5enc);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct crypto_ahash *auth = ctx->auth;
	struct krb5enc_request_ctx *areq_ctx = aead_request_ctx(req);
	struct ahash_request *ahreq = (void *)(areq_ctx->tail + ictx->reqoff);
	u8 *hash = areq_ctx->tail;
	int err;

	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   krb5enc_encrypt_ahash_done, req);
	ahash_request_set_tfm(ahreq, auth);
	ahash_request_set_crypt(ahreq, req->src, hash, req->assoclen + req->cryptlen);

	err = crypto_ahash_digest(ahreq);
	if (err)
		return err;

	krb5enc_insert_checksum(req, hash);
	return 0;
}

/*
 * Process an encryption operation.  We can perform the cipher and the hash in
 * parallel, provided the src and dst buffers are separate.
 */
static int krb5enc_encrypt(struct aead_request *req)
{
	int err;

	err = krb5enc_dispatch_encrypt_hash(req);
	if (err < 0)
		return err;

	return krb5enc_dispatch_encrypt(req, aead_request_flags(req));
}

static int krb5enc_verify_hash(struct aead_request *req)
{
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);
	struct aead_instance *inst = aead_alg_instance(krb5enc);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct krb5enc_request_ctx *areq_ctx = aead_request_ctx(req);
	struct ahash_request *ahreq = (void *)(areq_ctx->tail + ictx->reqoff);
	unsigned int authsize = crypto_aead_authsize(krb5enc);
	u8 *calc_hash = areq_ctx->tail;
	u8 *msg_hash  = areq_ctx->tail + authsize;

	scatterwalk_map_and_copy(msg_hash, req->src, ahreq->nbytes, authsize, 0);

	if (crypto_memneq(msg_hash, calc_hash, authsize))
		return -EBADMSG;
	return 0;
}

static void krb5enc_decrypt_hash_done(void *data, int err)
{
	struct aead_request *req = data;

	if (err)
		return krb5enc_request_complete(req, err);

	err = krb5enc_verify_hash(req);
	krb5enc_request_complete(req, err);
}

/*
 * Dispatch the hashing of the plaintext after we've done the decryption.
 */
static int krb5enc_dispatch_decrypt_hash(struct aead_request *req)
{
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);
	struct aead_instance *inst = aead_alg_instance(krb5enc);
	struct krb5enc_ctx *ctx = crypto_aead_ctx(krb5enc);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct krb5enc_request_ctx *areq_ctx = aead_request_ctx(req);
	struct ahash_request *ahreq = (void *)(areq_ctx->tail + ictx->reqoff);
	struct crypto_ahash *auth = ctx->auth;
	unsigned int authsize = crypto_aead_authsize(krb5enc);
	u8 *hash = areq_ctx->tail;
	int err;

	ahash_request_set_tfm(ahreq, auth);
	ahash_request_set_crypt(ahreq, req->dst, hash,
				req->assoclen + req->cryptlen - authsize);
	ahash_request_set_callback(ahreq, aead_request_flags(req),
				   krb5enc_decrypt_hash_done, req);

	err = crypto_ahash_digest(ahreq);
	if (err < 0)
		return err;

	return krb5enc_verify_hash(req);
}

/*
 * Dispatch the decryption of the ciphertext.
 */
static int krb5enc_dispatch_decrypt(struct aead_request *req)
{
	struct crypto_aead *krb5enc = crypto_aead_reqtfm(req);
	struct aead_instance *inst = aead_alg_instance(krb5enc);
	struct krb5enc_ctx *ctx = crypto_aead_ctx(krb5enc);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct krb5enc_request_ctx *areq_ctx = aead_request_ctx(req);
	struct skcipher_request *skreq = (void *)(areq_ctx->tail +
						  ictx->reqoff);
	unsigned int authsize = crypto_aead_authsize(krb5enc);
	struct scatterlist *src, *dst;

	src = scatterwalk_ffwd(areq_ctx->src, req->src, req->assoclen);
	dst = src;

	if (req->src != req->dst)
		dst = scatterwalk_ffwd(areq_ctx->dst, req->dst, req->assoclen);

	skcipher_request_set_tfm(skreq, ctx->enc);
	skcipher_request_set_callback(skreq, aead_request_flags(req),
				      req->base.complete, req->base.data);
	skcipher_request_set_crypt(skreq, src, dst,
				   req->cryptlen - authsize, req->iv);

	return crypto_skcipher_decrypt(skreq);
}

static int krb5enc_decrypt(struct aead_request *req)
{
	int err;

	err = krb5enc_dispatch_decrypt(req);
	if (err < 0)
		return err;

	return krb5enc_dispatch_decrypt_hash(req);
}

static int krb5enc_init_tfm(struct crypto_aead *tfm)
{
	struct aead_instance *inst = aead_alg_instance(tfm);
	struct krb5enc_instance_ctx *ictx = aead_instance_ctx(inst);
	struct krb5enc_ctx *ctx = crypto_aead_ctx(tfm);
	struct crypto_ahash *auth;
	struct crypto_skcipher *enc;
	int err;

	auth = crypto_spawn_ahash(&ictx->auth);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	enc = crypto_spawn_skcipher(&ictx->enc);
	err = PTR_ERR(enc);
	if (IS_ERR(enc))
		goto err_free_ahash;

	ctx->auth = auth;
	ctx->enc = enc;

	crypto_aead_set_reqsize(
		tfm,
		sizeof(struct krb5enc_request_ctx) +
		ictx->reqoff + /* Space for two checksums */
		umax(sizeof(struct ahash_request) + crypto_ahash_reqsize(auth),
		     sizeof(struct skcipher_request) + crypto_skcipher_reqsize(enc)));

	return 0;

err_free_ahash:
	crypto_free_ahash(auth);
	return err;
}

static void krb5enc_exit_tfm(struct crypto_aead *tfm)
{
	struct krb5enc_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_ahash(ctx->auth);
	crypto_free_skcipher(ctx->enc);
}

static void krb5enc_free(struct aead_instance *inst)
{
	struct krb5enc_instance_ctx *ctx = aead_instance_ctx(inst);

	crypto_drop_skcipher(&ctx->enc);
	crypto_drop_ahash(&ctx->auth);
	kfree(inst);
}

/*
 * Create an instance of a template for a specific hash and cipher pair.
 */
static int krb5enc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct krb5enc_instance_ctx *ictx;
	struct skcipher_alg_common *enc;
	struct hash_alg_common *auth;
	struct aead_instance *inst;
	struct crypto_alg *auth_base;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_AEAD, &mask);
	if (err) {
		pr_err("attr_type failed\n");
		return err;
	}

	inst = kzalloc(sizeof(*inst) + sizeof(*ictx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	ictx = aead_instance_ctx(inst);

	err = crypto_grab_ahash(&ictx->auth, aead_crypto_instance(inst),
				crypto_attr_alg_name(tb[1]), 0, mask);
	if (err) {
		pr_err("grab ahash failed\n");
		goto err_free_inst;
	}
	auth = crypto_spawn_ahash_alg(&ictx->auth);
	auth_base = &auth->base;

	err = crypto_grab_skcipher(&ictx->enc, aead_crypto_instance(inst),
				   crypto_attr_alg_name(tb[2]), 0, mask);
	if (err) {
		pr_err("grab skcipher failed\n");
		goto err_free_inst;
	}
	enc = crypto_spawn_skcipher_alg_common(&ictx->enc);

	ictx->reqoff = 2 * auth->digestsize;

	err = -ENAMETOOLONG;
	if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
		     "krb5enc(%s,%s)", auth_base->cra_name,
		     enc->base.cra_name) >=
	    CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	if (snprintf(inst->alg.base.cra_driver_name, CRYPTO_MAX_ALG_NAME,
		     "krb5enc(%s,%s)", auth_base->cra_driver_name,
		     enc->base.cra_driver_name) >= CRYPTO_MAX_ALG_NAME)
		goto err_free_inst;

	inst->alg.base.cra_priority = enc->base.cra_priority * 10 +
				      auth_base->cra_priority;
	inst->alg.base.cra_blocksize = enc->base.cra_blocksize;
	inst->alg.base.cra_alignmask = enc->base.cra_alignmask;
	inst->alg.base.cra_ctxsize = sizeof(struct krb5enc_ctx);

	inst->alg.ivsize = enc->ivsize;
	inst->alg.chunksize = enc->chunksize;
	inst->alg.maxauthsize = auth->digestsize;

	inst->alg.init = krb5enc_init_tfm;
	inst->alg.exit = krb5enc_exit_tfm;

	inst->alg.setkey = krb5enc_setkey;
	inst->alg.encrypt = krb5enc_encrypt;
	inst->alg.decrypt = krb5enc_decrypt;

	inst->free = krb5enc_free;

	err = aead_register_instance(tmpl, inst);
	if (err) {
		pr_err("ref failed\n");
		goto err_free_inst;
	}

	return 0;

err_free_inst:
	krb5enc_free(inst);
	return err;
}

static struct crypto_template crypto_krb5enc_tmpl = {
	.name = "krb5enc",
	.create = krb5enc_create,
	.module = THIS_MODULE,
};

static int __init crypto_krb5enc_module_init(void)
{
	return crypto_register_template(&crypto_krb5enc_tmpl);
}

static void __exit crypto_krb5enc_module_exit(void)
{
	crypto_unregister_template(&crypto_krb5enc_tmpl);
}

subsys_initcall(crypto_krb5enc_module_init);
module_exit(crypto_krb5enc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple AEAD wrapper for Kerberos 5 RFC3961");
MODULE_ALIAS_CRYPTO("krb5enc");
