// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CBC: Cipher Block Chaining mode
 *
 * Copyright (c) 2006-2016 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/algapi.h>
#include <crypto/cbc.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>

static inline void crypto_cbc_encrypt_one(struct crypto_skcipher *tfm,
					  const u8 *src, u8 *dst)
{
	crypto_cipher_encrypt_one(skcipher_cipher_simple(tfm), dst, src);
}

static int crypto_cbc_encrypt(struct skcipher_request *req)
{
	return crypto_cbc_encrypt_walk(req, crypto_cbc_encrypt_one);
}

static inline void crypto_cbc_decrypt_one(struct crypto_skcipher *tfm,
					  const u8 *src, u8 *dst)
{
	crypto_cipher_decrypt_one(skcipher_cipher_simple(tfm), dst, src);
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

static int crypto_cbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_alg *alg;
	int err;

	inst = skcipher_alloc_instance_simple(tmpl, tb, &alg);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	err = -EINVAL;
	if (!is_power_of_2(alg->cra_blocksize))
		goto out_free_inst;

	inst->alg.encrypt = crypto_cbc_encrypt;
	inst->alg.decrypt = crypto_cbc_decrypt;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto out_free_inst;
	goto out_put_alg;

out_free_inst:
	inst->free(inst);
out_put_alg:
	crypto_mod_put(alg);
	return err;
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

subsys_initcall(crypto_cbc_module_init);
module_exit(crypto_cbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CBC block cipher mode of operation");
MODULE_ALIAS_CRYPTO("cbc");
