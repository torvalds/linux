/*
 * CBC: Cipher Block Chaining mode
 *
 * Copyright (c) 2006-2016 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/algapi.h>
#include <crypto/cbc.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/slab.h>

struct crypto_cbc_ctx {
	struct crypto_cipher *child;
};

static int crypto_cbc_setkey(struct crypto_skcipher *parent, const u8 *key,
			     unsigned int keylen)
{
	struct crypto_cbc_ctx *ctx = crypto_skcipher_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_skcipher_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen);
	crypto_skcipher_set_flags(parent, crypto_cipher_get_flags(child) &
					  CRYPTO_TFM_RES_MASK);
	return err;
}

static inline void crypto_cbc_encrypt_one(struct crypto_skcipher *tfm,
					  const u8 *src, u8 *dst)
{
	struct crypto_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_cipher_encrypt_one(ctx->child, dst, src);
}

static int crypto_cbc_encrypt(struct skcipher_request *req)
{
	return crypto_cbc_encrypt_walk(req, crypto_cbc_encrypt_one);
}

static inline void crypto_cbc_decrypt_one(struct crypto_skcipher *tfm,
					  const u8 *src, u8 *dst)
{
	struct crypto_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_cipher_decrypt_one(ctx->child, dst, src);
}

static int crypto_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		err = crypto_cbc_decrypt_blocks(&walk, tfm,
						crypto_cbc_decrypt_one);
		err = skcipher_walk_done(&walk, err);
	}

	return err;
}

static int crypto_cbc_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_spawn *spawn = skcipher_instance_ctx(inst);
	struct crypto_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_cipher *cipher;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;
	return 0;
}

static void crypto_cbc_exit_tfm(struct crypto_skcipher *tfm)
{
	struct crypto_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_cipher(ctx->child);
}

static void crypto_cbc_free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int crypto_cbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct crypto_spawn *spawn;
	struct crypto_alg *alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SKCIPHER);
	if (err)
		return err;

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	algt = crypto_get_attr_type(tb);
	err = PTR_ERR(algt);
	if (IS_ERR(algt))
		goto err_free_inst;

	mask = CRYPTO_ALG_TYPE_MASK |
		crypto_requires_off(algt->type, algt->mask,
				    CRYPTO_ALG_NEED_FALLBACK);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER, mask);
	err = PTR_ERR(alg);
	if (IS_ERR(alg))
		goto err_free_inst;

	spawn = skcipher_instance_ctx(inst);
	err = crypto_init_spawn(spawn, alg, skcipher_crypto_instance(inst),
				CRYPTO_ALG_TYPE_MASK);
	crypto_mod_put(alg);
	if (err)
		goto err_free_inst;

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "cbc", alg);
	if (err)
		goto err_drop_spawn;

	err = -EINVAL;
	if (!is_power_of_2(alg->cra_blocksize))
		goto err_drop_spawn;

	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;
	inst->alg.base.cra_alignmask = alg->cra_alignmask;

	inst->alg.ivsize = alg->cra_blocksize;
	inst->alg.min_keysize = alg->cra_cipher.cia_min_keysize;
	inst->alg.max_keysize = alg->cra_cipher.cia_max_keysize;

	inst->alg.base.cra_ctxsize = sizeof(struct crypto_cbc_ctx);

	inst->alg.init = crypto_cbc_init_tfm;
	inst->alg.exit = crypto_cbc_exit_tfm;

	inst->alg.setkey = crypto_cbc_setkey;
	inst->alg.encrypt = crypto_cbc_encrypt;
	inst->alg.decrypt = crypto_cbc_decrypt;

	inst->free = crypto_cbc_free;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto err_drop_spawn;

out:
	return err;

err_drop_spawn:
	crypto_drop_spawn(spawn);
err_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_cbc_tmpl = {
	.name = "cbc",
	.create = crypto_cbc_create,
	.module = THIS_MODULE,
};

static int __init crypto_cbc_module_init(void)
{
	return crypto_register_template(&crypto_cbc_tmpl);
}

static void __exit crypto_cbc_module_exit(void)
{
	crypto_unregister_template(&crypto_cbc_tmpl);
}

module_init(crypto_cbc_module_init);
module_exit(crypto_cbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CBC block cipher algorithm");
MODULE_ALIAS_CRYPTO("cbc");
