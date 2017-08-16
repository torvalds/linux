/*
 * FPU: Wrapper for blkcipher touching fpu
 *
 * Copyright (c) Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/fpu/api.h>

struct crypto_fpu_ctx {
	struct crypto_skcipher *child;
};

static int crypto_fpu_setkey(struct crypto_skcipher *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_fpu_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_skcipher *child = ctx->child;
	int err;

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, crypto_skcipher_get_flags(parent) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(child, key, keylen);
	crypto_skcipher_set_flags(parent, crypto_skcipher_get_flags(child) &
					  CRYPTO_TFM_RES_MASK);
	return err;
}

static int crypto_fpu_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_fpu_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child = ctx->child;
	SKCIPHER_REQUEST_ON_STACK(subreq, child);
	int err;

	skcipher_request_set_tfm(subreq, child);
	skcipher_request_set_callback(subreq, 0, NULL, NULL);
	skcipher_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
				   req->iv);

	kernel_fpu_begin();
	err = crypto_skcipher_encrypt(subreq);
	kernel_fpu_end();

	skcipher_request_zero(subreq);
	return err;
}

static int crypto_fpu_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_fpu_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child = ctx->child;
	SKCIPHER_REQUEST_ON_STACK(subreq, child);
	int err;

	skcipher_request_set_tfm(subreq, child);
	skcipher_request_set_callback(subreq, 0, NULL, NULL);
	skcipher_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
				   req->iv);

	kernel_fpu_begin();
	err = crypto_skcipher_decrypt(subreq);
	kernel_fpu_end();

	skcipher_request_zero(subreq);
	return err;
}

static int crypto_fpu_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_fpu_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher_spawn *spawn;
	struct crypto_skcipher *cipher;

	spawn = skcipher_instance_ctx(inst);
	cipher = crypto_spawn_skcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	return 0;
}

static void crypto_fpu_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_fpu_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->child);
}

static void crypto_fpu_free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int crypto_fpu_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_skcipher_spawn *spawn;
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct skcipher_alg *alg;
	const char *cipher_name;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ (CRYPTO_ALG_INTERNAL | CRYPTO_ALG_TYPE_SKCIPHER)) &
	    algt->mask)
		return -EINVAL;

	if (!(algt->mask & CRYPTO_ALG_INTERNAL))
		return -EINVAL;

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = skcipher_instance_ctx(inst);

	crypto_set_skcipher_spawn(spawn, skcipher_crypto_instance(inst));
	err = crypto_grab_skcipher(spawn, cipher_name, CRYPTO_ALG_INTERNAL,
				   CRYPTO_ALG_INTERNAL | CRYPTO_ALG_ASYNC);
	if (err)
		goto out_free_inst;

	alg = crypto_skcipher_spawn_alg(spawn);

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "fpu",
				  &alg->base);
	if (err)
		goto out_drop_skcipher;

	inst->alg.base.cra_flags = CRYPTO_ALG_INTERNAL;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = alg->base.cra_blocksize;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask;

	inst->alg.ivsize = crypto_skcipher_alg_ivsize(alg);
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(alg);
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(alg);

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_fpu_ctx);

	inst->alg.init = crypto_fpu_init_tfm;
	inst->alg.exit = crypto_fpu_exit_tfm;

	inst->alg.setkey = crypto_fpu_setkey;
	inst->alg.encrypt = crypto_fpu_encrypt;
	inst->alg.decrypt = crypto_fpu_decrypt;

	inst->free = crypto_fpu_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto out_drop_skcipher;

out:
	return err;

out_drop_skcipher:
	crypto_drop_skcipher(spawn);
out_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_fpu_tmpl = {
	.name = "fpu",
	.create = crypto_fpu_create,
	.module = THIS_MODULE,
};

int __init crypto_fpu_init(void)
{
	return crypto_register_template(&crypto_fpu_tmpl);
}

void crypto_fpu_exit(void)
{
	crypto_unregister_template(&crypto_fpu_tmpl);
}

MODULE_ALIAS_CRYPTO("fpu");
