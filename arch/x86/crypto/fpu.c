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

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/i387.h>

struct crypto_fpu_ctx {
	struct crypto_blkcipher *child;
};

static int crypto_fpu_setkey(struct crypto_tfm *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_fpu_ctx *ctx = crypto_tfm_ctx(parent);
	struct crypto_blkcipher *child = ctx->child;
	int err;

	crypto_blkcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_blkcipher_set_flags(child, crypto_tfm_get_flags(parent) &
				   CRYPTO_TFM_REQ_MASK);
	err = crypto_blkcipher_setkey(child, key, keylen);
	crypto_tfm_set_flags(parent, crypto_blkcipher_get_flags(child) &
				     CRYPTO_TFM_RES_MASK);
	return err;
}

static int crypto_fpu_encrypt(struct blkcipher_desc *desc_in,
			      struct scatterlist *dst, struct scatterlist *src,
			      unsigned int nbytes)
{
	int err;
	struct crypto_fpu_ctx *ctx = crypto_blkcipher_ctx(desc_in->tfm);
	struct crypto_blkcipher *child = ctx->child;
	struct blkcipher_desc desc = {
		.tfm = child,
		.info = desc_in->info,
		.flags = desc_in->flags,
	};

	kernel_fpu_begin();
	err = crypto_blkcipher_crt(desc.tfm)->encrypt(&desc, dst, src, nbytes);
	kernel_fpu_end();
	return err;
}

static int crypto_fpu_decrypt(struct blkcipher_desc *desc_in,
			      struct scatterlist *dst, struct scatterlist *src,
			      unsigned int nbytes)
{
	int err;
	struct crypto_fpu_ctx *ctx = crypto_blkcipher_ctx(desc_in->tfm);
	struct crypto_blkcipher *child = ctx->child;
	struct blkcipher_desc desc = {
		.tfm = child,
		.info = desc_in->info,
		.flags = desc_in->flags,
	};

	kernel_fpu_begin();
	err = crypto_blkcipher_crt(desc.tfm)->decrypt(&desc, dst, src, nbytes);
	kernel_fpu_end();
	return err;
}

static int crypto_fpu_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = crypto_tfm_alg_instance(tfm);
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_fpu_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_blkcipher *cipher;

	cipher = crypto_spawn_blkcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_fpu_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_fpu_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_blkcipher(ctx->child);
}

static struct crypto_instance *crypto_fpu_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_BLKCIPHER);
	if (err)
		return ERR_PTR(err);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_BLKCIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	inst = crypto_alloc_instance("fpu", alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = alg->cra_flags;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = alg->cra_type;
	inst->alg.cra_blkcipher.ivsize = alg->cra_blkcipher.ivsize;
	inst->alg.cra_blkcipher.min_keysize = alg->cra_blkcipher.min_keysize;
	inst->alg.cra_blkcipher.max_keysize = alg->cra_blkcipher.max_keysize;
	inst->alg.cra_ctxsize = sizeof(struct crypto_fpu_ctx);
	inst->alg.cra_init = crypto_fpu_init_tfm;
	inst->alg.cra_exit = crypto_fpu_exit_tfm;
	inst->alg.cra_blkcipher.setkey = crypto_fpu_setkey;
	inst->alg.cra_blkcipher.encrypt = crypto_fpu_encrypt;
	inst->alg.cra_blkcipher.decrypt = crypto_fpu_decrypt;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static void crypto_fpu_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_fpu_tmpl = {
	.name = "fpu",
	.alloc = crypto_fpu_alloc,
	.free = crypto_fpu_free,
	.module = THIS_MODULE,
};

static int __init crypto_fpu_module_init(void)
{
	return crypto_register_template(&crypto_fpu_tmpl);
}

static void __exit crypto_fpu_module_exit(void)
{
	crypto_unregister_template(&crypto_fpu_tmpl);
}

module_init(crypto_fpu_module_init);
module_exit(crypto_fpu_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FPU block cipher wrapper");
