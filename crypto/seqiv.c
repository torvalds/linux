/*
 * seqiv: Sequence Number IV Generator
 *
 * This generator generates an IV based on a sequence number by xoring it
 * with a salt.  This algorithm is mainly useful for CTR and similar modes.
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/null.h>
#include <crypto/rng.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

struct seqiv_ctx {
	spinlock_t lock;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

struct seqiv_aead_ctx {
	struct crypto_aead *child;
	spinlock_t lock;
	struct crypto_blkcipher *null;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

static int seqiv_aead_setkey(struct crypto_aead *tfm,
			     const u8 *key, unsigned int keylen)
{
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(tfm);

	return crypto_aead_setkey(ctx->child, key, keylen);
}

static int seqiv_aead_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(tfm);

	return crypto_aead_setauthsize(ctx->child, authsize);
}

static void seqiv_complete2(struct skcipher_givcrypt_request *req, int err)
{
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	struct crypto_ablkcipher *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = skcipher_givcrypt_reqtfm(req);
	memcpy(req->creq.info, subreq->info, crypto_ablkcipher_ivsize(geniv));

out:
	kfree(subreq->info);
}

static void seqiv_complete(struct crypto_async_request *base, int err)
{
	struct skcipher_givcrypt_request *req = base->data;

	seqiv_complete2(req, err);
	skcipher_givcrypt_complete(req, err);
}

static void seqiv_aead_complete2(struct aead_givcrypt_request *req, int err)
{
	struct aead_request *subreq = aead_givcrypt_reqctx(req);
	struct crypto_aead *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = aead_givcrypt_reqtfm(req);
	memcpy(req->areq.iv, subreq->iv, crypto_aead_ivsize(geniv));

out:
	kfree(subreq->iv);
}

static void seqiv_aead_complete(struct crypto_async_request *base, int err)
{
	struct aead_givcrypt_request *req = base->data;

	seqiv_aead_complete2(req, err);
	aead_givcrypt_complete(req, err);
}

static void seqiv_aead_encrypt_complete2(struct aead_request *req, int err)
{
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = crypto_aead_reqtfm(req);
	memcpy(req->iv, subreq->iv, crypto_aead_ivsize(geniv));

out:
	kzfree(subreq->iv);
}

static void seqiv_aead_encrypt_complete(struct crypto_async_request *base,
					int err)
{
	struct aead_request *req = base->data;

	seqiv_aead_encrypt_complete2(req, err);
	aead_request_complete(req, err);
}

static void seqiv_geniv(struct seqiv_ctx *ctx, u8 *info, u64 seq,
			unsigned int ivsize)
{
	unsigned int len = ivsize;

	if (ivsize > sizeof(u64)) {
		memset(info, 0, ivsize - sizeof(u64));
		len = sizeof(u64);
	}
	seq = cpu_to_be64(seq);
	memcpy(info + ivsize - len, &seq, len);
	crypto_xor(info, ctx->salt, ivsize);
}

static int seqiv_givencrypt(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	struct ablkcipher_request *subreq = skcipher_givcrypt_reqctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	ablkcipher_request_set_tfm(subreq, skcipher_geniv_cipher(geniv));

	compl = req->creq.base.complete;
	data = req->creq.base.data;
	info = req->creq.info;

	ivsize = crypto_ablkcipher_ivsize(geniv);

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_ablkcipher_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, req->creq.base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		compl = seqiv_complete;
		data = req;
	}

	ablkcipher_request_set_callback(subreq, req->creq.base.flags, compl,
					data);
	ablkcipher_request_set_crypt(subreq, req->creq.src, req->creq.dst,
				     req->creq.nbytes, info);

	seqiv_geniv(ctx, info, req->seq, ivsize);
	memcpy(req->giv, info, ivsize);

	err = crypto_ablkcipher_encrypt(subreq);
	if (unlikely(info != req->creq.info))
		seqiv_complete2(req, err);
	return err;
}

static int seqiv_aead_givencrypt(struct aead_givcrypt_request *req)
{
	struct crypto_aead *geniv = aead_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *areq = &req->areq;
	struct aead_request *subreq = aead_givcrypt_reqctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	aead_request_set_tfm(subreq, aead_geniv_base(geniv));

	compl = areq->base.complete;
	data = areq->base.data;
	info = areq->iv;

	ivsize = crypto_aead_ivsize(geniv);

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_aead_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, areq->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		compl = seqiv_aead_complete;
		data = req;
	}

	aead_request_set_callback(subreq, areq->base.flags, compl, data);
	aead_request_set_crypt(subreq, areq->src, areq->dst, areq->cryptlen,
			       info);
	aead_request_set_assoc(subreq, areq->assoc, areq->assoclen);

	seqiv_geniv(ctx, info, req->seq, ivsize);
	memcpy(req->giv, info, ivsize);

	err = crypto_aead_encrypt(subreq);
	if (unlikely(info != areq->iv))
		seqiv_aead_complete2(req, err);
	return err;
}

static int seqiv_aead_encrypt_compat(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	aead_request_set_tfm(subreq, ctx->child);

	compl = req->base.complete;
	data = req->base.data;
	info = req->iv;

	ivsize = crypto_aead_ivsize(geniv);

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_aead_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, req->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		memcpy(info, req->iv, ivsize);
		compl = seqiv_aead_encrypt_complete;
		data = req;
	}

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen - ivsize, info);
	aead_request_set_ad(subreq, req->assoclen, ivsize);

	crypto_xor(info, ctx->salt, ivsize);
	scatterwalk_map_and_copy(info, req->dst, req->assoclen, ivsize, 1);

	err = crypto_aead_encrypt(subreq);
	if (unlikely(info != req->iv))
		seqiv_aead_encrypt_complete2(req, err);
	return err;
}

static int seqiv_aead_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	aead_request_set_tfm(subreq, ctx->child);

	compl = req->base.complete;
	data = req->base.data;
	info = req->iv;

	ivsize = crypto_aead_ivsize(geniv);

	if (req->src != req->dst) {
		struct scatterlist src[2];
		struct scatterlist dst[2];
		struct blkcipher_desc desc = {
			.tfm = ctx->null,
		};

		err = crypto_blkcipher_encrypt(
			&desc,
			scatterwalk_ffwd(dst, req->dst,
					 req->assoclen + ivsize),
			scatterwalk_ffwd(src, req->src,
					 req->assoclen + ivsize),
			req->cryptlen - ivsize);
		if (err)
			return err;
	}

	if (unlikely(!IS_ALIGNED((unsigned long)info,
				 crypto_aead_alignmask(geniv) + 1))) {
		info = kmalloc(ivsize, req->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		memcpy(info, req->iv, ivsize);
		compl = seqiv_aead_encrypt_complete;
		data = req;
	}

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->dst, req->dst,
			       req->cryptlen - ivsize, info);
	aead_request_set_ad(subreq, req->assoclen + ivsize, 0);

	crypto_xor(info, ctx->salt, ivsize);
	scatterwalk_map_and_copy(info, req->dst, req->assoclen, ivsize, 1);

	err = crypto_aead_encrypt(subreq);
	if (unlikely(info != req->iv))
		seqiv_aead_encrypt_complete2(req, err);
	return err;
}

static int seqiv_aead_decrypt_compat(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize;

	aead_request_set_tfm(subreq, ctx->child);

	compl = req->base.complete;
	data = req->base.data;

	ivsize = crypto_aead_ivsize(geniv);

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen, ivsize);

	scatterwalk_map_and_copy(req->iv, req->src, req->assoclen, ivsize, 0);

	return crypto_aead_decrypt(subreq);
}

static int seqiv_aead_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize;

	aead_request_set_tfm(subreq, ctx->child);

	compl = req->base.complete;
	data = req->base.data;

	ivsize = crypto_aead_ivsize(geniv);

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen + ivsize, 0);

	scatterwalk_map_and_copy(req->iv, req->src, req->assoclen, ivsize, 0);
	if (req->src != req->dst)
		scatterwalk_map_and_copy(req->iv, req->dst,
					 req->assoclen, ivsize, 1);

	return crypto_aead_decrypt(subreq);
}

static int seqiv_givencrypt_first(struct skcipher_givcrypt_request *req)
{
	struct crypto_ablkcipher *geniv = skcipher_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (crypto_ablkcipher_crt(geniv)->givencrypt != seqiv_givencrypt_first)
		goto unlock;

	crypto_ablkcipher_crt(geniv)->givencrypt = seqiv_givencrypt;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_ablkcipher_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return seqiv_givencrypt(req);
}

static int seqiv_aead_givencrypt_first(struct aead_givcrypt_request *req)
{
	struct crypto_aead *geniv = aead_givcrypt_reqtfm(req);
	struct seqiv_ctx *ctx = crypto_aead_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (crypto_aead_crt(geniv)->givencrypt != seqiv_aead_givencrypt_first)
		goto unlock;

	crypto_aead_crt(geniv)->givencrypt = seqiv_aead_givencrypt;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return seqiv_aead_givencrypt(req);
}

static int seqiv_aead_encrypt_compat_first(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (geniv->encrypt != seqiv_aead_encrypt_compat_first)
		goto unlock;

	geniv->encrypt = seqiv_aead_encrypt_compat;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return seqiv_aead_encrypt_compat(req);
}

static int seqiv_aead_encrypt_first(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (geniv->encrypt != seqiv_aead_encrypt_first)
		goto unlock;

	geniv->encrypt = seqiv_aead_encrypt;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return seqiv_aead_encrypt(req);
}

static int seqiv_init(struct crypto_tfm *tfm)
{
	struct crypto_ablkcipher *geniv = __crypto_ablkcipher_cast(tfm);
	struct seqiv_ctx *ctx = crypto_ablkcipher_ctx(geniv);

	spin_lock_init(&ctx->lock);

	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request);

	return skcipher_geniv_init(tfm);
}

static int seqiv_old_aead_init(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct seqiv_ctx *ctx = crypto_aead_ctx(geniv);

	spin_lock_init(&ctx->lock);

	crypto_aead_set_reqsize(__crypto_aead_cast(tfm),
				sizeof(struct aead_request));

	return aead_geniv_init(tfm);
}

static int seqiv_aead_compat_init(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->lock);

	crypto_aead_set_reqsize(geniv, sizeof(struct aead_request));

	err = aead_geniv_init(tfm);

	ctx->child = geniv->child;
	geniv->child = geniv;

	return err;
}

static int seqiv_aead_init(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct seqiv_aead_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->lock);

	crypto_aead_set_reqsize(geniv, sizeof(struct aead_request));

	ctx->null = crypto_get_default_null_skcipher();
	err = PTR_ERR(ctx->null);
	if (IS_ERR(ctx->null))
		goto out;

	err = aead_geniv_init(tfm);
	if (err)
		goto drop_null;

	ctx->child = geniv->child;
	geniv->child = geniv;

out:
	return err;

drop_null:
	crypto_put_default_null_skcipher();
	goto out;
}

static void seqiv_aead_compat_exit(struct crypto_tfm *tfm)
{
	struct seqiv_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static void seqiv_aead_exit(struct crypto_tfm *tfm)
{
	struct seqiv_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
	crypto_put_default_null_skcipher();
}

static struct crypto_template seqiv_tmpl;

static struct crypto_instance *seqiv_ablkcipher_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;

	inst = skcipher_geniv_alloc(&seqiv_tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		goto out;

	if (inst->alg.cra_ablkcipher.ivsize < sizeof(u64)) {
		skcipher_geniv_free(inst);
		inst = ERR_PTR(-EINVAL);
		goto out;
	}

	inst->alg.cra_ablkcipher.givencrypt = seqiv_givencrypt_first;

	inst->alg.cra_init = seqiv_init;
	inst->alg.cra_exit = skcipher_geniv_exit;

	inst->alg.cra_ctxsize += inst->alg.cra_ablkcipher.ivsize;
	inst->alg.cra_ctxsize += sizeof(struct seqiv_ctx);

out:
	return inst;
}

static struct crypto_instance *seqiv_old_aead_alloc(struct aead_instance *aead)
{
	struct crypto_instance *inst = aead_crypto_instance(aead);

	if (inst->alg.cra_aead.ivsize < sizeof(u64)) {
		aead_geniv_free(aead);
		return ERR_PTR(-EINVAL);
	}

	inst->alg.cra_aead.givencrypt = seqiv_aead_givencrypt_first;

	inst->alg.cra_init = seqiv_old_aead_init;
	inst->alg.cra_exit = aead_geniv_exit;

	inst->alg.cra_ctxsize = inst->alg.cra_aead.ivsize;
	inst->alg.cra_ctxsize += sizeof(struct seqiv_ctx);

	return inst;
}

static struct crypto_instance *seqiv_aead_alloc(struct rtattr **tb)
{
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;

	inst = aead_geniv_alloc(&seqiv_tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		goto out;

	if (inst->alg.base.cra_aead.encrypt)
		return seqiv_old_aead_alloc(inst);

	if (inst->alg.ivsize < sizeof(u64)) {
		aead_geniv_free(inst);
		inst = ERR_PTR(-EINVAL);
		goto out;
	}

	spawn = aead_instance_ctx(inst);
	alg = crypto_spawn_aead_alg(spawn);

	inst->alg.setkey = seqiv_aead_setkey;
	inst->alg.setauthsize = seqiv_aead_setauthsize;
	inst->alg.encrypt = seqiv_aead_encrypt_first;
	inst->alg.decrypt = seqiv_aead_decrypt;

	inst->alg.base.cra_init = seqiv_aead_init;
	inst->alg.base.cra_exit = seqiv_aead_exit;

	inst->alg.base.cra_ctxsize = sizeof(struct seqiv_aead_ctx);
	inst->alg.base.cra_ctxsize += inst->alg.base.cra_aead.ivsize;

	if (alg->base.cra_aead.encrypt) {
		inst->alg.encrypt = seqiv_aead_encrypt_compat_first;
		inst->alg.decrypt = seqiv_aead_decrypt_compat;

		inst->alg.base.cra_init = seqiv_aead_compat_init;
		inst->alg.base.cra_exit = seqiv_aead_compat_exit;
	}

out:
	return aead_crypto_instance(inst);
}

static struct crypto_instance *seqiv_alloc(struct rtattr **tb)
{
	struct crypto_attr_type *algt;
	struct crypto_instance *inst;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return ERR_CAST(algt);

	err = crypto_get_default_rng();
	if (err)
		return ERR_PTR(err);

	if ((algt->type ^ CRYPTO_ALG_TYPE_AEAD) & CRYPTO_ALG_TYPE_MASK)
		inst = seqiv_ablkcipher_alloc(tb);
	else
		inst = seqiv_aead_alloc(tb);

	if (IS_ERR(inst))
		goto put_rng;

	inst->alg.cra_alignmask |= __alignof__(u32) - 1;

out:
	return inst;

put_rng:
	crypto_put_default_rng();
	goto out;
}

static void seqiv_free(struct crypto_instance *inst)
{
	if ((inst->alg.cra_flags ^ CRYPTO_ALG_TYPE_AEAD) & CRYPTO_ALG_TYPE_MASK)
		skcipher_geniv_free(inst);
	else
		aead_geniv_free(aead_instance(inst));
	crypto_put_default_rng();
}

static struct crypto_template seqiv_tmpl = {
	.name = "seqiv",
	.alloc = seqiv_alloc,
	.free = seqiv_free,
	.module = THIS_MODULE,
};

static int __init seqiv_module_init(void)
{
	return crypto_register_template(&seqiv_tmpl);
}

static void __exit seqiv_module_exit(void)
{
	crypto_unregister_template(&seqiv_tmpl);
}

module_init(seqiv_module_init);
module_exit(seqiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sequence Number IV Generator");
MODULE_ALIAS_CRYPTO("seqiv");
