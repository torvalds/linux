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

#include <crypto/internal/geniv.h>
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

struct echainiv_ctx {
	/* aead_geniv_ctx must be first the element */
	struct aead_geniv_ctx geniv;
	struct crypto_blkcipher *null;
	u8 salt[] __attribute__ ((aligned(__alignof__(u32))));
};

static DEFINE_PER_CPU(u32 [MAX_IV_SIZE / sizeof(u32)], echainiv_iv);

/* We don't care if we get preempted and read/write IVs from the next CPU. */
static void echainiv_read_iv(u8 *dst, unsigned size)
{
	u32 *a = (u32 *)dst;
	u32 __percpu *b = echainiv_iv;

	for (; size >= 4; size -= 4) {
		*a++ = this_cpu_read(*b);
		b++;
	}
}

static void echainiv_write_iv(const u8 *src, unsigned size)
{
	const u32 *a = (const u32 *)src;
	u32 __percpu *b = echainiv_iv;

	for (; size >= 4; size -= 4) {
		this_cpu_write(*b, *a);
		a++;
		b++;
	}
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

static int echainiv_encrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	u8 *info;
	unsigned int ivsize = crypto_aead_ivsize(geniv);
	int err;

	if (req->cryptlen < ivsize)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->geniv.child);

	compl = echainiv_encrypt_complete;
	data = req;
	info = req->iv;

	if (req->src != req->dst) {
		struct blkcipher_desc desc = {
			.tfm = ctx->null,
		};

		err = crypto_blkcipher_encrypt(
			&desc, req->dst, req->src,
			req->assoclen + req->cryptlen);
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
			       req->cryptlen, info);
	aead_request_set_ad(subreq, req->assoclen);

	crypto_xor(info, ctx->salt, ivsize);
	scatterwalk_map_and_copy(info, req->dst, req->assoclen, ivsize, 1);
	echainiv_read_iv(info, ivsize);

	err = crypto_aead_encrypt(subreq);
	echainiv_encrypt_complete2(req, err);
	return err;
}

static int echainiv_decrypt(struct aead_request *req)
{
	struct crypto_aead *geniv = crypto_aead_reqtfm(req);
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	struct aead_request *subreq = aead_request_ctx(req);
	crypto_completion_t compl;
	void *data;
	unsigned int ivsize = crypto_aead_ivsize(geniv);

	if (req->cryptlen < ivsize)
		return -EINVAL;

	aead_request_set_tfm(subreq, ctx->geniv.child);

	compl = req->base.complete;
	data = req->base.data;

	aead_request_set_callback(subreq, req->base.flags, compl, data);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen - ivsize, req->iv);
	aead_request_set_ad(subreq, req->assoclen + ivsize);

	scatterwalk_map_and_copy(req->iv, req->src, req->assoclen, ivsize, 0);

	return crypto_aead_decrypt(subreq);
}

static int echainiv_init(struct crypto_aead *geniv)
{
	struct echainiv_ctx *ctx = crypto_aead_ctx(geniv);
	int err;

	spin_lock_init(&ctx->geniv.lock);

	crypto_aead_set_reqsize(geniv, sizeof(struct aead_request));

	err = crypto_get_default_rng();
	if (err)
		goto out;

	err = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   crypto_aead_ivsize(geniv));
	crypto_put_default_rng();
	if (err)
		goto out;

	ctx->null = crypto_get_default_null_skcipher();
	err = PTR_ERR(ctx->null);
	if (IS_ERR(ctx->null))
		goto out;

	err = aead_geniv_init(crypto_aead_tfm(geniv));
	if (err)
		goto drop_null;

	ctx->geniv.child = geniv->child;
	geniv->child = geniv;

out:
	return err;

drop_null:
	crypto_put_default_null_skcipher();
	goto out;
}

static void echainiv_exit(struct crypto_aead *tfm)
{
	struct echainiv_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->geniv.child);
	crypto_put_default_null_skcipher();
}

static int echainiv_aead_create(struct crypto_template *tmpl,
				struct rtattr **tb)
{
	struct aead_instance *inst;
	struct crypto_aead_spawn *spawn;
	struct aead_alg *alg;
	int err;

	inst = aead_geniv_alloc(tmpl, tb, 0, 0);

	if (IS_ERR(inst))
		return PTR_ERR(inst);

	spawn = aead_instance_ctx(inst);
	alg = crypto_spawn_aead_alg(spawn);

	if (alg->base.cra_aead.encrypt)
		goto done;

	err = -EINVAL;
	if (inst->alg.ivsize & (sizeof(u32) - 1) ||
	    inst->alg.ivsize > MAX_IV_SIZE)
		goto free_inst;

	inst->alg.encrypt = echainiv_encrypt;
	inst->alg.decrypt = echainiv_decrypt;

	inst->alg.init = echainiv_init;
	inst->alg.exit = echainiv_exit;

	inst->alg.base.cra_alignmask |= __alignof__(u32) - 1;
	inst->alg.base.cra_ctxsize = sizeof(struct echainiv_ctx);
	inst->alg.base.cra_ctxsize += inst->alg.ivsize;

	inst->free = aead_geniv_free;

done:
	err = aead_register_instance(tmpl, inst);
	if (err)
		goto free_inst;

out:
	return err;

free_inst:
	aead_geniv_free(inst);
	goto out;
}

static void echainiv_free(struct crypto_instance *inst)
{
	aead_geniv_free(aead_instance(inst));
}

static struct crypto_template echainiv_tmpl = {
	.name = "echainiv",
	.create = echainiv_aead_create,
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
