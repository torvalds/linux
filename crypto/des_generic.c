// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * DES & Triple DES EDE Cipher Algorithms.
 *
 * Copyright (c) 2005 Dag Arne Osvik <da@osvik.no>
 */

#include <asm/byteorder.h>
#include <crypto/algapi.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <crypto/internal/des.h>

static int des_setkey(struct crypto_tfm *tfm, const u8 *key,
		      unsigned int keylen)
{
	struct des_ctx *dctx = crypto_tfm_ctx(tfm);
	int err;

	err = des_expand_key(dctx, key, keylen);
	if (err == -ENOKEY) {
		if (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)
			err = -EINVAL;
		else
			err = 0;
	}
	if (err)
		memset(dctx, 0, sizeof(*dctx));
	return err;
}

static void crypto_des_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	const struct des_ctx *dctx = crypto_tfm_ctx(tfm);

	des_encrypt(dctx, dst, src);
}

static void crypto_des_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	const struct des_ctx *dctx = crypto_tfm_ctx(tfm);

	des_decrypt(dctx, dst, src);
}

static int des3_ede_setkey(struct crypto_tfm *tfm, const u8 *key,
			   unsigned int keylen)
{
	struct des3_ede_ctx *dctx = crypto_tfm_ctx(tfm);
	int err;

	err = des3_ede_expand_key(dctx, key, keylen);
	if (err == -ENOKEY) {
		if (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)
			err = -EINVAL;
		else
			err = 0;
	}
	if (err)
		memset(dctx, 0, sizeof(*dctx));
	return err;
}

static void crypto_des3_ede_encrypt(struct crypto_tfm *tfm, u8 *dst,
				    const u8 *src)
{
	const struct des3_ede_ctx *dctx = crypto_tfm_ctx(tfm);

	des3_ede_encrypt(dctx, dst, src);
}

static void crypto_des3_ede_decrypt(struct crypto_tfm *tfm, u8 *dst,
				    const u8 *src)
{
	const struct des3_ede_ctx *dctx = crypto_tfm_ctx(tfm);

	des3_ede_decrypt(dctx, dst, src);
}

static struct crypto_alg des_algs[2] = { {
	.cra_name		=	"des",
	.cra_driver_name	=	"des-generic",
	.cra_priority		=	100,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES_KEY_SIZE,
	.cia_max_keysize	=	DES_KEY_SIZE,
	.cia_setkey		=	des_setkey,
	.cia_encrypt		=	crypto_des_encrypt,
	.cia_decrypt		=	crypto_des_decrypt } }
}, {
	.cra_name		=	"des3_ede",
	.cra_driver_name	=	"des3_ede-generic",
	.cra_priority		=	100,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES3_EDE_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct des3_ede_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{ .cipher = {
	.cia_min_keysize	=	DES3_EDE_KEY_SIZE,
	.cia_max_keysize	=	DES3_EDE_KEY_SIZE,
	.cia_setkey		=	des3_ede_setkey,
	.cia_encrypt		=	crypto_des3_ede_encrypt,
	.cia_decrypt		=	crypto_des3_ede_decrypt } }
} };

static int __init des_generic_mod_init(void)
{
	return crypto_register_algs(des_algs, ARRAY_SIZE(des_algs));
}

static void __exit des_generic_mod_fini(void)
{
	crypto_unregister_algs(des_algs, ARRAY_SIZE(des_algs));
}

subsys_initcall(des_generic_mod_init);
module_exit(des_generic_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
MODULE_AUTHOR("Dag Arne Osvik <da@osvik.no>");
MODULE_ALIAS_CRYPTO("des");
MODULE_ALIAS_CRYPTO("des-generic");
MODULE_ALIAS_CRYPTO("des3_ede");
MODULE_ALIAS_CRYPTO("des3_ede-generic");
