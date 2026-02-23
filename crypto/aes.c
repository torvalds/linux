// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Crypto API support for AES block cipher
 *
 * Copyright 2026 Google LLC
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/module.h>

static_assert(__alignof__(struct aes_key) <= CRYPTO_MINALIGN);

static int crypto_aes_setkey(struct crypto_tfm *tfm, const u8 *in_key,
			     unsigned int key_len)
{
	struct aes_key *key = crypto_tfm_ctx(tfm);

	return aes_preparekey(key, in_key, key_len);
}

static void crypto_aes_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct aes_key *key = crypto_tfm_ctx(tfm);

	aes_encrypt(key, out, in);
}

static void crypto_aes_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct aes_key *key = crypto_tfm_ctx(tfm);

	aes_decrypt(key, out, in);
}

static struct crypto_alg alg = {
	.cra_name = "aes",
	.cra_driver_name = "aes-lib",
	.cra_priority = 100,
	.cra_flags = CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct aes_key),
	.cra_module = THIS_MODULE,
	.cra_u = { .cipher = { .cia_min_keysize = AES_MIN_KEY_SIZE,
			       .cia_max_keysize = AES_MAX_KEY_SIZE,
			       .cia_setkey = crypto_aes_setkey,
			       .cia_encrypt = crypto_aes_encrypt,
			       .cia_decrypt = crypto_aes_decrypt } }
};

static int __init crypto_aes_mod_init(void)
{
	return crypto_register_alg(&alg);
}
module_init(crypto_aes_mod_init);

static void __exit crypto_aes_mod_exit(void)
{
	crypto_unregister_alg(&alg);
}
module_exit(crypto_aes_mod_exit);

MODULE_DESCRIPTION("Crypto API support for AES block cipher");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("aes");
MODULE_ALIAS_CRYPTO("aes-lib");
