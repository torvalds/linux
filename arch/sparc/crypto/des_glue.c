// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for DES encryption optimized for sparc64 crypto opcodes.
 *
 * Copyright (C) 2012 David S. Miller <davem@davemloft.net>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <crypto/algapi.h>
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>

#include <asm/fpumacro.h>
#include <asm/pstate.h>
#include <asm/elf.h>

#include "opcodes.h"

struct des_sparc64_ctx {
	u64 encrypt_expkey[DES_EXPKEY_WORDS / 2];
	u64 decrypt_expkey[DES_EXPKEY_WORDS / 2];
};

struct des3_ede_sparc64_ctx {
	u64 encrypt_expkey[DES3_EDE_EXPKEY_WORDS / 2];
	u64 decrypt_expkey[DES3_EDE_EXPKEY_WORDS / 2];
};

static void encrypt_to_decrypt(u64 *d, const u64 *e)
{
	const u64 *s = e + (DES_EXPKEY_WORDS / 2) - 1;
	int i;

	for (i = 0; i < DES_EXPKEY_WORDS / 2; i++)
		*d++ = *s--;
}

extern void des_sparc64_key_expand(const u32 *input_key, u64 *key);

static int des_set_key(struct crypto_tfm *tfm, const u8 *key,
		       unsigned int keylen)
{
	struct des_sparc64_ctx *dctx = crypto_tfm_ctx(tfm);
	int err;

	/* Even though we have special instructions for key expansion,
	 * we call des_verify_key() so that we don't have to write our own
	 * weak key detection code.
	 */
	err = crypto_des_verify_key(tfm, key);
	if (err)
		return err;

	des_sparc64_key_expand((const u32 *) key, &dctx->encrypt_expkey[0]);
	encrypt_to_decrypt(&dctx->decrypt_expkey[0], &dctx->encrypt_expkey[0]);

	return 0;
}

static int des_set_key_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				unsigned int keylen)
{
	return des_set_key(crypto_skcipher_tfm(tfm), key, keylen);
}

extern void des_sparc64_crypt(const u64 *key, const u64 *input,
			      u64 *output);

static void sparc_des_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct des_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);
	const u64 *K = ctx->encrypt_expkey;

	des_sparc64_crypt(K, (const u64 *) src, (u64 *) dst);
}

static void sparc_des_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct des_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);
	const u64 *K = ctx->decrypt_expkey;

	des_sparc64_crypt(K, (const u64 *) src, (u64 *) dst);
}

extern void des_sparc64_load_keys(const u64 *key);

extern void des_sparc64_ecb_crypt(const u64 *input, u64 *output,
				  unsigned int len);

static int __ecb_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct des_sparc64_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	if (encrypt)
		des_sparc64_load_keys(&ctx->encrypt_expkey[0]);
	else
		des_sparc64_load_keys(&ctx->decrypt_expkey[0]);
	while ((nbytes = walk.nbytes) != 0) {
		des_sparc64_ecb_crypt(walk.src.virt.addr, walk.dst.virt.addr,
				      round_down(nbytes, DES_BLOCK_SIZE));
		err = skcipher_walk_done(&walk, nbytes % DES_BLOCK_SIZE);
	}
	fprs_write(0);
	return err;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, true);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, false);
}

extern void des_sparc64_cbc_encrypt(const u64 *input, u64 *output,
				    unsigned int len, u64 *iv);

extern void des_sparc64_cbc_decrypt(const u64 *input, u64 *output,
				    unsigned int len, u64 *iv);

static int __cbc_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct des_sparc64_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	if (encrypt)
		des_sparc64_load_keys(&ctx->encrypt_expkey[0]);
	else
		des_sparc64_load_keys(&ctx->decrypt_expkey[0]);
	while ((nbytes = walk.nbytes) != 0) {
		if (encrypt)
			des_sparc64_cbc_encrypt(walk.src.virt.addr,
						walk.dst.virt.addr,
						round_down(nbytes,
							   DES_BLOCK_SIZE),
						walk.iv);
		else
			des_sparc64_cbc_decrypt(walk.src.virt.addr,
						walk.dst.virt.addr,
						round_down(nbytes,
							   DES_BLOCK_SIZE),
						walk.iv);
		err = skcipher_walk_done(&walk, nbytes % DES_BLOCK_SIZE);
	}
	fprs_write(0);
	return err;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	return __cbc_crypt(req, true);
}

static int cbc_decrypt(struct skcipher_request *req)
{
	return __cbc_crypt(req, false);
}

static int des3_ede_set_key(struct crypto_tfm *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct des3_ede_sparc64_ctx *dctx = crypto_tfm_ctx(tfm);
	u64 k1[DES_EXPKEY_WORDS / 2];
	u64 k2[DES_EXPKEY_WORDS / 2];
	u64 k3[DES_EXPKEY_WORDS / 2];
	int err;

	err = crypto_des3_ede_verify_key(tfm, key);
	if (err)
		return err;

	des_sparc64_key_expand((const u32 *)key, k1);
	key += DES_KEY_SIZE;
	des_sparc64_key_expand((const u32 *)key, k2);
	key += DES_KEY_SIZE;
	des_sparc64_key_expand((const u32 *)key, k3);

	memcpy(&dctx->encrypt_expkey[0], &k1[0], sizeof(k1));
	encrypt_to_decrypt(&dctx->encrypt_expkey[DES_EXPKEY_WORDS / 2], &k2[0]);
	memcpy(&dctx->encrypt_expkey[(DES_EXPKEY_WORDS / 2) * 2],
	       &k3[0], sizeof(k3));

	encrypt_to_decrypt(&dctx->decrypt_expkey[0], &k3[0]);
	memcpy(&dctx->decrypt_expkey[DES_EXPKEY_WORDS / 2],
	       &k2[0], sizeof(k2));
	encrypt_to_decrypt(&dctx->decrypt_expkey[(DES_EXPKEY_WORDS / 2) * 2],
			   &k1[0]);

	return 0;
}

static int des3_ede_set_key_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				     unsigned int keylen)
{
	return des3_ede_set_key(crypto_skcipher_tfm(tfm), key, keylen);
}

extern void des3_ede_sparc64_crypt(const u64 *key, const u64 *input,
				   u64 *output);

static void sparc_des3_ede_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct des3_ede_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);
	const u64 *K = ctx->encrypt_expkey;

	des3_ede_sparc64_crypt(K, (const u64 *) src, (u64 *) dst);
}

static void sparc_des3_ede_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct des3_ede_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);
	const u64 *K = ctx->decrypt_expkey;

	des3_ede_sparc64_crypt(K, (const u64 *) src, (u64 *) dst);
}

extern void des3_ede_sparc64_load_keys(const u64 *key);

extern void des3_ede_sparc64_ecb_crypt(const u64 *expkey, const u64 *input,
				       u64 *output, unsigned int len);

static int __ecb3_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct des3_ede_sparc64_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	const u64 *K;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	if (encrypt)
		K = &ctx->encrypt_expkey[0];
	else
		K = &ctx->decrypt_expkey[0];
	des3_ede_sparc64_load_keys(K);
	while ((nbytes = walk.nbytes) != 0) {
		des3_ede_sparc64_ecb_crypt(K, walk.src.virt.addr,
					   walk.dst.virt.addr,
					   round_down(nbytes, DES_BLOCK_SIZE));
		err = skcipher_walk_done(&walk, nbytes % DES_BLOCK_SIZE);
	}
	fprs_write(0);
	return err;
}

static int ecb3_encrypt(struct skcipher_request *req)
{
	return __ecb3_crypt(req, true);
}

static int ecb3_decrypt(struct skcipher_request *req)
{
	return __ecb3_crypt(req, false);
}

extern void des3_ede_sparc64_cbc_encrypt(const u64 *expkey, const u64 *input,
					 u64 *output, unsigned int len,
					 u64 *iv);

extern void des3_ede_sparc64_cbc_decrypt(const u64 *expkey, const u64 *input,
					 u64 *output, unsigned int len,
					 u64 *iv);

static int __cbc3_crypt(struct skcipher_request *req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct des3_ede_sparc64_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	const u64 *K;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, true);
	if (err)
		return err;

	if (encrypt)
		K = &ctx->encrypt_expkey[0];
	else
		K = &ctx->decrypt_expkey[0];
	des3_ede_sparc64_load_keys(K);
	while ((nbytes = walk.nbytes) != 0) {
		if (encrypt)
			des3_ede_sparc64_cbc_encrypt(K, walk.src.virt.addr,
						     walk.dst.virt.addr,
						     round_down(nbytes,
								DES_BLOCK_SIZE),
						     walk.iv);
		else
			des3_ede_sparc64_cbc_decrypt(K, walk.src.virt.addr,
						     walk.dst.virt.addr,
						     round_down(nbytes,
								DES_BLOCK_SIZE),
						     walk.iv);
		err = skcipher_walk_done(&walk, nbytes % DES_BLOCK_SIZE);
	}
	fprs_write(0);
	return err;
}

static int cbc3_encrypt(struct skcipher_request *req)
{
	return __cbc3_crypt(req, true);
}

static int cbc3_decrypt(struct skcipher_request *req)
{
	return __cbc3_crypt(req, false);
}

static struct crypto_alg cipher_algs[] = {
	{
		.cra_name		= "des",
		.cra_driver_name	= "des-sparc64",
		.cra_priority		= SPARC_CR_OPCODE_PRIORITY,
		.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct des_sparc64_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
		.cra_u	= {
			.cipher	= {
				.cia_min_keysize	= DES_KEY_SIZE,
				.cia_max_keysize	= DES_KEY_SIZE,
				.cia_setkey		= des_set_key,
				.cia_encrypt		= sparc_des_encrypt,
				.cia_decrypt		= sparc_des_decrypt
			}
		}
	}, {
		.cra_name		= "des3_ede",
		.cra_driver_name	= "des3_ede-sparc64",
		.cra_priority		= SPARC_CR_OPCODE_PRIORITY,
		.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
		.cra_blocksize		= DES3_EDE_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct des3_ede_sparc64_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
		.cra_u	= {
			.cipher	= {
				.cia_min_keysize	= DES3_EDE_KEY_SIZE,
				.cia_max_keysize	= DES3_EDE_KEY_SIZE,
				.cia_setkey		= des3_ede_set_key,
				.cia_encrypt		= sparc_des3_ede_encrypt,
				.cia_decrypt		= sparc_des3_ede_decrypt
			}
		}
	}
};

static struct skcipher_alg skcipher_algs[] = {
	{
		.base.cra_name		= "ecb(des)",
		.base.cra_driver_name	= "ecb-des-sparc64",
		.base.cra_priority	= SPARC_CR_OPCODE_PRIORITY,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct des_sparc64_ctx),
		.base.cra_alignmask	= 7,
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.setkey			= des_set_key_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "cbc(des)",
		.base.cra_driver_name	= "cbc-des-sparc64",
		.base.cra_priority	= SPARC_CR_OPCODE_PRIORITY,
		.base.cra_blocksize	= DES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct des_sparc64_ctx),
		.base.cra_alignmask	= 7,
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= DES_KEY_SIZE,
		.max_keysize		= DES_KEY_SIZE,
		.ivsize			= DES_BLOCK_SIZE,
		.setkey			= des_set_key_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}, {
		.base.cra_name		= "ecb(des3_ede)",
		.base.cra_driver_name	= "ecb-des3_ede-sparc64",
		.base.cra_priority	= SPARC_CR_OPCODE_PRIORITY,
		.base.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct des3_ede_sparc64_ctx),
		.base.cra_alignmask	= 7,
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.setkey			= des3_ede_set_key_skcipher,
		.encrypt		= ecb3_encrypt,
		.decrypt		= ecb3_decrypt,
	}, {
		.base.cra_name		= "cbc(des3_ede)",
		.base.cra_driver_name	= "cbc-des3_ede-sparc64",
		.base.cra_priority	= SPARC_CR_OPCODE_PRIORITY,
		.base.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct des3_ede_sparc64_ctx),
		.base.cra_alignmask	= 7,
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.ivsize			= DES3_EDE_BLOCK_SIZE,
		.setkey			= des3_ede_set_key_skcipher,
		.encrypt		= cbc3_encrypt,
		.decrypt		= cbc3_decrypt,
	}
};

static bool __init sparc64_has_des_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_DES))
		return false;

	return true;
}

static int __init des_sparc64_mod_init(void)
{
	int err;

	if (!sparc64_has_des_opcode()) {
		pr_info("sparc64 des opcodes not available.\n");
		return -ENODEV;
	}
	pr_info("Using sparc64 des opcodes optimized DES implementation\n");
	err = crypto_register_algs(cipher_algs, ARRAY_SIZE(cipher_algs));
	if (err)
		return err;
	err = crypto_register_skciphers(skcipher_algs,
					ARRAY_SIZE(skcipher_algs));
	if (err)
		crypto_unregister_algs(cipher_algs, ARRAY_SIZE(cipher_algs));
	return err;
}

static void __exit des_sparc64_mod_fini(void)
{
	crypto_unregister_algs(cipher_algs, ARRAY_SIZE(cipher_algs));
	crypto_unregister_skciphers(skcipher_algs, ARRAY_SIZE(skcipher_algs));
}

module_init(des_sparc64_mod_init);
module_exit(des_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms, sparc64 des opcode accelerated");

MODULE_ALIAS_CRYPTO("des");
MODULE_ALIAS_CRYPTO("des3_ede");

#include "crop_devid.c"
