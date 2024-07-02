// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ECB: Electronic CodeBook mode
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/internal/cipher.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

static int crypto_ecb_crypt(struct crypto_cipher *cipher, const u8 *src,
			    u8 *dst, unsigned nbytes, bool final,
			    void (*fn)(struct crypto_tfm *, u8 *, const u8 *))
{
	const unsigned int bsize = crypto_cipher_blocksize(cipher);

	while (nbytes >= bsize) {
		fn(crypto_cipher_tfm(cipher), dst, src);

		src += bsize;
		dst += bsize;

		nbytes -= bsize;
	}

	return nbytes && final ? -EINVAL : nbytes;
}

static int crypto_ecb_encrypt2(struct crypto_lskcipher *tfm, const u8 *src,
			       u8 *dst, unsigned len, u8 *iv, u32 flags)
{
	struct crypto_cipher **ctx = crypto_lskcipher_ctx(tfm);
	struct crypto_cipher *cipher = *ctx;

	return crypto_ecb_crypt(cipher, src, dst, len,
				flags & CRYPTO_LSKCIPHER_FLAG_FINAL,
				crypto_cipher_alg(cipher)->cia_encrypt);
}

static int crypto_ecb_decrypt2(struct crypto_lskcipher *tfm, const u8 *src,
			       u8 *dst, unsigned len, u8 *iv, u32 flags)
{
	struct crypto_cipher **ctx = crypto_lskcipher_ctx(tfm);
	struct crypto_cipher *cipher = *ctx;

	return crypto_ecb_crypt(cipher, src, dst, len,
				flags & CRYPTO_LSKCIPHER_FLAG_FINAL,
				crypto_cipher_alg(cipher)->cia_decrypt);
}

static int lskcipher_setkey_simple2(struct crypto_lskcipher *tfm,
				    const u8 *key, unsigned int keylen)
{
	struct crypto_cipher **ctx = crypto_lskcipher_ctx(tfm);
	struct crypto_cipher *cipher = *ctx;

	crypto_cipher_clear_flags(cipher, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(cipher, crypto_lskcipher_get_flags(tfm) &
				CRYPTO_TFM_REQ_MASK);
	return crypto_cipher_setkey(cipher, key, keylen);
}

static int lskcipher_init_tfm_simple2(struct crypto_lskcipher *tfm)
{
	struct lskcipher_instance *inst = lskcipher_alg_instance(tfm);
	struct crypto_cipher **ctx = crypto_lskcipher_ctx(tfm);
	struct crypto_cipher_spawn *spawn;
	struct crypto_cipher *cipher;

	spawn = lskcipher_instance_ctx(inst);
	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	*ctx = cipher;
	return 0;
}

static void lskcipher_exit_tfm_simple2(struct crypto_lskcipher *tfm)
{
	struct crypto_cipher **ctx = crypto_lskcipher_ctx(tfm);

	crypto_free_cipher(*ctx);
}

static void lskcipher_free_instance_simple2(struct lskcipher_instance *inst)
{
	crypto_drop_cipher(lskcipher_instance_ctx(inst));
	kfree(inst);
}

static struct lskcipher_instance *lskcipher_alloc_instance_simple2(
	struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_cipher_spawn *spawn;
	struct lskcipher_instance *inst;
	struct crypto_alg *cipher_alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_LSKCIPHER, &mask);
	if (err)
		return ERR_PTR(err);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return ERR_PTR(-ENOMEM);
	spawn = lskcipher_instance_ctx(inst);

	err = crypto_grab_cipher(spawn, lskcipher_crypto_instance(inst),
				 crypto_attr_alg_name(tb[1]), 0, mask);
	if (err)
		goto err_free_inst;
	cipher_alg = crypto_spawn_cipher_alg(spawn);

	err = crypto_inst_setname(lskcipher_crypto_instance(inst), tmpl->name,
				  cipher_alg);
	if (err)
		goto err_free_inst;

	inst->free = lskcipher_free_instance_simple2;

	/* Default algorithm properties, can be overridden */
	inst->alg.co.base.cra_blocksize = cipher_alg->cra_blocksize;
	inst->alg.co.base.cra_alignmask = cipher_alg->cra_alignmask;
	inst->alg.co.base.cra_priority = cipher_alg->cra_priority;
	inst->alg.co.min_keysize = cipher_alg->cra_cipher.cia_min_keysize;
	inst->alg.co.max_keysize = cipher_alg->cra_cipher.cia_max_keysize;
	inst->alg.co.ivsize = cipher_alg->cra_blocksize;

	/* Use struct crypto_cipher * by default, can be overridden */
	inst->alg.co.base.cra_ctxsize = sizeof(struct crypto_cipher *);
	inst->alg.setkey = lskcipher_setkey_simple2;
	inst->alg.init = lskcipher_init_tfm_simple2;
	inst->alg.exit = lskcipher_exit_tfm_simple2;

	return inst;

err_free_inst:
	lskcipher_free_instance_simple2(inst);
	return ERR_PTR(err);
}

static int crypto_ecb_create2(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct lskcipher_instance *inst;
	int err;

	inst = lskcipher_alloc_instance_simple2(tmpl, tb);
	if (IS_ERR(inst))
		return PTR_ERR(inst);

	/* ECB mode doesn't take an IV */
	inst->alg.co.ivsize = 0;

	inst->alg.encrypt = crypto_ecb_encrypt2;
	inst->alg.decrypt = crypto_ecb_decrypt2;

	err = lskcipher_register_instance(tmpl, inst);
	if (err)
		inst->free(inst);

	return err;
}

static int crypto_ecb_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_lskcipher_spawn *spawn;
	struct lskcipher_alg *cipher_alg;
	struct lskcipher_instance *inst;
	int err;

	inst = lskcipher_alloc_instance_simple(tmpl, tb);
	if (IS_ERR(inst)) {
		err = crypto_ecb_create2(tmpl, tb);
		return err;
	}

	spawn = lskcipher_instance_ctx(inst);
	cipher_alg = crypto_lskcipher_spawn_alg(spawn);

	/* ECB mode doesn't take an IV */
	inst->alg.co.ivsize = 0;
	if (cipher_alg->co.ivsize)
		return -EINVAL;

	inst->alg.co.base.cra_ctxsize = cipher_alg->co.base.cra_ctxsize;
	inst->alg.setkey = cipher_alg->setkey;
	inst->alg.encrypt = cipher_alg->encrypt;
	inst->alg.decrypt = cipher_alg->decrypt;
	inst->alg.init = cipher_alg->init;
	inst->alg.exit = cipher_alg->exit;

	err = lskcipher_register_instance(tmpl, inst);
	if (err)
		inst->free(inst);

	return err;
}

static struct crypto_template crypto_ecb_tmpl = {
	.name = "ecb",
	.create = crypto_ecb_create,
	.module = THIS_MODULE,
};

static int __init crypto_ecb_module_init(void)
{
	return crypto_register_template(&crypto_ecb_tmpl);
}

static void __exit crypto_ecb_module_exit(void)
{
	crypto_unregister_template(&crypto_ecb_tmpl);
}

subsys_initcall(crypto_ecb_module_init);
module_exit(crypto_ecb_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ECB block cipher mode of operation");
MODULE_ALIAS_CRYPTO("ecb");
MODULE_IMPORT_NS(CRYPTO_INTERNAL);
