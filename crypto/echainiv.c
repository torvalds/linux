/*
 * echainiv: Encrypted Chain IV Generator
 *
 * This generator generates an IV based on a sequence number by xoring it
 * with a salt and then encrypting it with the same key as used to encrypt
 * the plain text.  This algorithm requires that the block size be equal
 * to the IV size.  It is mainly useful for CBC.
 *
 * This generator can only be used by algorithms where authentication
 * is performed after encryption (i.e., authenc).
 *
 * Copyright (c) 2015 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/aead.h>
#include <crypto/null.h>
#include <crypto/rng.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#define MAX_IV_SIZE 16

struct echainiv_request_ctx {
	struct scatterlist src[2];
	struct scatterlist dst[2];
	struct scatterlist ivbuf[2];
	struct scatterlist *ivsg;
	struct aead_givcrypt_request subreq;
};

struct echainiv_ctx {
	struct crypto_aead *child;
	spinlock_t lock;
	struct crypto_blkcipher *null;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

static DEFINE_PER_CPU(u32 [MAX_IV_SIZE / sizeof(u32)], echainiv_iv);

static int echainiv_setkey(struct crypto_aead *tfm,
			      const u8 *key, unsigned int keylen)
{
	struct echainiv_ctx *ctx = crypto_aead_ctx(tfm);

	return crypto_aead_setkey(ctx->child, key, keylen);
}

static int echainiv_setauthsize(struct crypto_aead *tfm,
				  unsigned int authsize)
{
	struct echainiv_ctx *ctx = crypto_aead_ctx(tfm);

	return crypto_aead_setauthsize(ctx->child, authsize);
}

/* We don't care if we get preempted and read/write IVs from the next CPU. */
void echainiv_read_iv(u8 *dst, unsigned size)
{
	u32 *a = (u32 *)dst;
	u32 __percpu *b = echainiv_iv;

	for (; size >= 4; size -= 4) {
		*a++ = this_cpu_read(*b);
		b++;
	}
}

void echainiv_write_iv(const u8 *src, unsigned size)
{
	const u32 *a = (const u32 *)src;
	u32 __percpu *b = echainiv_iv;

	for (; size >= 4; size -= 4) {
		this_cpu_write(*b, *a);
		a++;
		b++;
	}
}

static void echainiv_encrypt_compat_complete2(struct aead_request *req,
						 int err)
{
	struct echainiv_request_ctx *rctx = aead_request_ctx(req);
	struct aead_givcrypt_request *subreq = &rctx->subreq;
	struct crypto_aead *geniv;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = crypto_aead_reqtfm(req);
	scatterwalk_map_and_copy(subreq->giv, rctx->ivsg, 0,
				 crypto_aead_ivsize(geniv), 1);

out:
	kzfree(subreq->giv);
}

static void echainiv_encrypt_compat_complete(
	struct crypto_async_request *base, int err)
{
	struct aead_request *req = base->data;

	echainiv_encrypt_compat_complete2(req, err);
	aead_request_complete(req, err);
}

static void echainiv_encrypt_complete2(struct aead_request *req, int err)
{
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *geniv;
	unsigned int ivsize;

	if (err == -EINPROGRESS)
		return;

	if (err)
		goto out;

	geniv = crypto_aead_reqtfm(req);
	ivsize = crypto_aead_ivsize(geniv);

	echainiv_write_iv(subreq->iv, ivsize);

	if (req->iv != subreq->iv)
		memcpy(req->iv, subreq->iv, ivsize);

out:
	if (req->iv != subreq->iv)
		kzfree(subreq->iv);
}

static void echainiv_encrypt_complete(struct crypto_async_request *base,
					 int err)
{
	struct aead_request *req = base->data;

	echainiv_encrypt_complete2(req, err);
	aead_request_complete(req, err);
}

static int echainiv_encrypt_compat(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	struct echainiv_request_ctx *rctx = aead_request_ctx(req);
	struct aead_givcrypt_request *subreq = &rctx->subreq;
	unsigned int ivsize = crypto_aead_ivsize(geniv);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	__be64 seq;
	int err;

	compl = req->base.complete;
	data = req->base.data;

	rctx->ivsg = scatterwalk_ffwd(rctx->ivbuf, req->dst, req->assoclen);
	info = PageHighMem(sg_page(rctx->ivsg)) ? NULL : sg_virt(rctx->ivsg);

	if (!info) {
		info = kmalloc(ivsize, req->base.flags &
				       CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL:
								  GFP_ATOMIC);
		if (!info)
			return -ENOMEM;

		compl = echainiv_encrypt_compat_complete;
		data = req;
	}

	memcpy(&seq, req->iv + ivsize - sizeof(seq), sizeof(seq));

	aead_givcrypt_set_tfm(subreq, ctx->child);
	aead_givcrypt_set_callback(subreq, req->base.flags,
				   req->base.complete, req->base.data);
	aead_givcrypt_set_crypt(subreq,
				scatterwalk_ffwd(rctx->src, req->src,
						 req->assoclen + ivsize),
				scatterwalk_ffwd(rctx->dst, rctx->ivsg,
						 ivsize),
				req->cryptlen - ivsize, req->iv);
	aead_givcrypt_set_assoc(subreq, req->src, req->assoclen);
	aead_givcrypt_set_giv(subreq, info, be64_to_cpu(seq));

	err = crypto_aead_givencrypt(subreq);
	if (unlikely(PageHighMem(sg_page(rctx->ivsg))))
		echainiv_encrypt_compat_complete2(req, err);
	return err;
}

static int echainiv_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize;
	int err;

	aead_request_set_tfm(subreq, ctx->child);

	compl = echainiv_encrypt_complete;
	data = req;
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
	}

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->dst, req->dst,
			       req->cryptlen - ivsize, info);
	aead_request_set_ad(subreq, req->assoclen + ivsize, 0);

	crypto_xor(info, ctx->salt, ivsize);
	scatterwalk_map_and_copy(info, req->dst, req->assoclen, ivsize, 1);
	echainiv_read_iv(info, ivsize);

	err = crypto_aead_encrypt(subreq);
	echainiv_encrypt_complete2(req, err);
	return err;
}

static int echainiv_decrypt_compat(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
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

static int echainiv_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
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

static int echainiv_encrypt_compat_first(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (geniv->encrypt != echainiv_encrypt_compat_first)
		goto unlock;

	geniv->encrypt = echainiv_encrypt_compat;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return echainiv_encrypt_compat(req);
}

static int echainiv_encrypt_first(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	int err = 0;

	spin_lock_bh(&ctx->lock);
	if (geniv->encrypt != echainiv_encrypt_first)
		goto unlock;

	geniv->encrypt = echainiv_encrypt;
	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));

unlock:
	spin_unlock_bh(&ctx->lock);

	if (err)
		return err;

	return echainiv_encrypt(req);
}

static int echainiv_compat_init(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->lock);

	crypto_aead_set_reqsize(geniv, sizeof(struct echainiv_request_ctx));

	err = aead_geniv_init(tfm);

	ctx->child = geniv->child;
	geniv->child = geniv;

	return err;
}

static int echainiv_init(struct crypto_tfm *tfm)
{
	struct crypto_aead *geniv = __crypto_aead_cast(tfm);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
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

static void echainiv_compat_exit(struct crypto_tfm *tfm)
{
	struct echainiv_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
}

static void echainiv_exit(struct crypto_tfm *tfm)
{
	struct echainiv_ctx *ctx = crypto_tfm_ctx(tfm);

	crypto_free_aead(ctx->child);
	crypto_put_default_null_skcipher();
}

static struct crypto_template echainiv_tmpl;

static struct crypto_instance *echainiv_aead_alloc(struct rtattr **tb)
{
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;

	inst = aead_geniv_alloc(&echainiv_tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		goto out;

	if (inst->alg.ivsize < sizeof(u64) ||
	    inst->alg.ivsize & (sizeof(u32) - 1) ||
	    inst->alg.ivsize > MAX_IV_SIZE) {
		aead_geniv_free(inst);
		inst = ERR_PTR(-EINVAL);
		goto out;
	}

	spawn = aead_instance_ctx(inst);
	alg = crypto_spawn_aead_alg(spawn);

	inst->alg.setkey = echainiv_setkey;
	inst->alg.setauthsize = echainiv_setauthsize;
	inst->alg.encrypt = echainiv_encrypt_first;
	inst->alg.decrypt = echainiv_decrypt;

	inst->alg.base.cra_init = echainiv_init;
	inst->alg.base.cra_exit = echainiv_exit;

	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;
	inst->alg.base.cra_ctxsize = sizeof(struct echainiv_ctx);
	inst->alg.base.cra_ctxsize += inst->alg.base.cra_aead.ivsize;

	if (alg->base.cra_aead.encrypt) {
		inst->alg.encrypt = echainiv_encrypt_compat_first;
		inst->alg.decrypt = echainiv_decrypt_compat;

		inst->alg.base.cra_init = echainiv_compat_init;
		inst->alg.base.cra_exit = echainiv_compat_exit;
	}

out:
	return aead_crypto_instance(inst);
}

static struct crypto_instance *echainiv_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	int err;

	err = crypto_get_default_rng();
	if (err)
		return ERR_PTR(err);

	inst = echainiv_aead_alloc(tb);

	if (IS_ERR(inst))
		goto put_rng;

out:
	return inst;

put_rng:
	crypto_put_default_rng();
	goto out;
}

static void echainiv_free(struct crypto_instance *inst)
{
	aead_geniv_free(aead_instance(inst));
	crypto_put_default_rng();
}

static struct crypto_template echainiv_tmpl = {
	.name = "echainiv",
	.alloc = echainiv_alloc,
	.free = echainiv_free,
	.module = THIS_MODULE,
};

static int __init echainiv_module_init(void)
{
	return crypto_register_template(&echainiv_tmpl);
}

static void __exit echainiv_module_exit(void)
{
	crypto_unregister_template(&echainiv_tmpl);
}

module_init(echainiv_module_init);
module_exit(echainiv_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Encrypted Chain IV Generator");
MODULE_ALIAS_CRYPTO("echainiv");
