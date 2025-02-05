// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Meta, Inc */
#include <linux/types.h>
#include <linux/module.h>
#include <linux/bpf_crypto.h>
#include <crypto/skcipher.h>

static void *bpf_crypto_lskcipher_alloc_tfm(const char *algo)
{
	return crypto_alloc_lskcipher(algo, 0, 0);
}

static void bpf_crypto_lskcipher_free_tfm(void *tfm)
{
	crypto_free_lskcipher(tfm);
}

static int bpf_crypto_lskcipher_has_algo(const char *algo)
{
	return crypto_has_skcipher(algo, CRYPTO_ALG_TYPE_LSKCIPHER, CRYPTO_ALG_TYPE_MASK);
}

static int bpf_crypto_lskcipher_setkey(void *tfm, const u8 *key, unsigned int keylen)
{
	return crypto_lskcipher_setkey(tfm, key, keylen);
}

static u32 bpf_crypto_lskcipher_get_flags(void *tfm)
{
	return crypto_lskcipher_get_flags(tfm);
}

static unsigned int bpf_crypto_lskcipher_ivsize(void *tfm)
{
	return crypto_lskcipher_ivsize(tfm);
}

static unsigned int bpf_crypto_lskcipher_statesize(void *tfm)
{
	return crypto_lskcipher_statesize(tfm);
}

static int bpf_crypto_lskcipher_encrypt(void *tfm, const u8 *src, u8 *dst,
					unsigned int len, u8 *siv)
{
	return crypto_lskcipher_encrypt(tfm, src, dst, len, siv);
}

static int bpf_crypto_lskcipher_decrypt(void *tfm, const u8 *src, u8 *dst,
					unsigned int len, u8 *siv)
{
	return crypto_lskcipher_decrypt(tfm, src, dst, len, siv);
}

static const struct bpf_crypto_type bpf_crypto_lskcipher_type = {
	.alloc_tfm	= bpf_crypto_lskcipher_alloc_tfm,
	.free_tfm	= bpf_crypto_lskcipher_free_tfm,
	.has_algo	= bpf_crypto_lskcipher_has_algo,
	.setkey		= bpf_crypto_lskcipher_setkey,
	.encrypt	= bpf_crypto_lskcipher_encrypt,
	.decrypt	= bpf_crypto_lskcipher_decrypt,
	.ivsize		= bpf_crypto_lskcipher_ivsize,
	.statesize	= bpf_crypto_lskcipher_statesize,
	.get_flags	= bpf_crypto_lskcipher_get_flags,
	.owner		= THIS_MODULE,
	.name		= "skcipher",
};

static int __init bpf_crypto_skcipher_init(void)
{
	return bpf_crypto_register_type(&bpf_crypto_lskcipher_type);
}

static void __exit bpf_crypto_skcipher_exit(void)
{
	int err = bpf_crypto_unregister_type(&bpf_crypto_lskcipher_type);
	WARN_ON_ONCE(err);
}

module_init(bpf_crypto_skcipher_init);
module_exit(bpf_crypto_skcipher_exit);
MODULE_LICENSE("GPL");
