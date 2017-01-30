/*
 * linux/arch/arm64/crypto/aes-glue.c - wrapper code for ARMv8 AES
 *
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/neon.h>
#include <asm/hwcap.h>
#include <crypto/aes.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <crypto/xts.h>

#include "aes-ce-setkey.h"

#ifdef USE_V8_CRYPTO_EXTENSIONS
#define MODE			"ce"
#define PRIO			300
#define aes_setkey		ce_aes_setkey
#define aes_expandkey		ce_aes_expandkey
#define aes_ecb_encrypt		ce_aes_ecb_encrypt
#define aes_ecb_decrypt		ce_aes_ecb_decrypt
#define aes_cbc_encrypt		ce_aes_cbc_encrypt
#define aes_cbc_decrypt		ce_aes_cbc_decrypt
#define aes_ctr_encrypt		ce_aes_ctr_encrypt
#define aes_xts_encrypt		ce_aes_xts_encrypt
#define aes_xts_decrypt		ce_aes_xts_decrypt
MODULE_DESCRIPTION("AES-ECB/CBC/CTR/XTS using ARMv8 Crypto Extensions");
#else
#define MODE			"neon"
#define PRIO			200
#define aes_setkey		crypto_aes_set_key
#define aes_expandkey		crypto_aes_expand_key
#define aes_ecb_encrypt		neon_aes_ecb_encrypt
#define aes_ecb_decrypt		neon_aes_ecb_decrypt
#define aes_cbc_encrypt		neon_aes_cbc_encrypt
#define aes_cbc_decrypt		neon_aes_cbc_decrypt
#define aes_ctr_encrypt		neon_aes_ctr_encrypt
#define aes_xts_encrypt		neon_aes_xts_encrypt
#define aes_xts_decrypt		neon_aes_xts_decrypt
MODULE_DESCRIPTION("AES-ECB/CBC/CTR/XTS using ARMv8 NEON");
MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");
#endif

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

/* defined in aes-modes.S */
asmlinkage void aes_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, int first);
asmlinkage void aes_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, int first);

asmlinkage void aes_cbc_encrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, u8 iv[], int first);
asmlinkage void aes_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, u8 iv[], int first);

asmlinkage void aes_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, u8 ctr[], int first);

asmlinkage void aes_xts_encrypt(u8 out[], u8 const in[], u8 const rk1[],
				int rounds, int blocks, u8 const rk2[], u8 iv[],
				int first);
asmlinkage void aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[],
				int rounds, int blocks, u8 const rk2[], u8 iv[],
				int first);

struct crypto_aes_xts_ctx {
	struct crypto_aes_ctx key1;
	struct crypto_aes_ctx __aligned(8) key2;
};

static int skcipher_aes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			       unsigned int key_len)
{
	return aes_setkey(crypto_skcipher_tfm(tfm), in_key, key_len);
}

static int xts_set_key(struct crypto_skcipher *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = xts_verify_key(tfm, in_key, key_len);
	if (ret)
		return ret;

	ret = aes_expandkey(&ctx->key1, in_key, key_len / 2);
	if (!ret)
		ret = aes_expandkey(&ctx->key2, &in_key[key_len / 2],
				    key_len / 2);
	if (!ret)
		return 0;

	crypto_skcipher_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		aes_ecb_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_enc, rounds, blocks, first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	kernel_neon_end();
	return err;
}

static int ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		aes_ecb_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_dec, rounds, blocks, first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	kernel_neon_end();
	return err;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		aes_cbc_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_enc, rounds, blocks, walk.iv,
				first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	kernel_neon_end();
	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		aes_cbc_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_dec, rounds, blocks, walk.iv,
				first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	kernel_neon_end();
	return err;
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	first = 1;
	kernel_neon_begin();
	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		aes_ctr_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_enc, rounds, blocks, walk.iv,
				first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
		first = 0;
	}
	if (walk.nbytes) {
		u8 __aligned(8) tail[AES_BLOCK_SIZE];
		unsigned int nbytes = walk.nbytes;
		u8 *tdst = walk.dst.virt.addr;
		u8 *tsrc = walk.src.virt.addr;

		/*
		 * Minimum alignment is 8 bytes, so if nbytes is <= 8, we need
		 * to tell aes_ctr_encrypt() to only read half a block.
		 */
		blocks = (nbytes <= 8) ? -1 : 1;

		aes_ctr_encrypt(tail, tsrc, (u8 *)ctx->key_enc, rounds,
				blocks, walk.iv, first);
		memcpy(tdst, tail, nbytes);
		err = skcipher_walk_done(&walk, 0);
	}
	kernel_neon_end();

	return err;
}

static int xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key1.key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		aes_xts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key1.key_enc, rounds, blocks,
				(u8 *)ctx->key2.key_enc, walk.iv, first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	kernel_neon_end();

	return err;
}

static int xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key1.key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		aes_xts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key1.key_dec, rounds, blocks,
				(u8 *)ctx->key2.key_enc, walk.iv, first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	kernel_neon_end();

	return err;
}

static struct skcipher_alg aes_algs[] = { {
	.base = {
		.cra_name		= "__ecb(aes)",
		.cra_driver_name	= "__ecb-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= ecb_encrypt,
	.decrypt	= ecb_decrypt,
}, {
	.base = {
		.cra_name		= "__cbc(aes)",
		.cra_driver_name	= "__cbc-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= cbc_encrypt,
	.decrypt	= cbc_decrypt,
}, {
	.base = {
		.cra_name		= "__ctr(aes)",
		.cra_driver_name	= "__ctr-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.chunksize	= AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= ctr_encrypt,
	.decrypt	= ctr_encrypt,
}, {
	.base = {
		.cra_name		= "__xts(aes)",
		.cra_driver_name	= "__xts-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_xts_ctx),
		.cra_alignmask		= 7,
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= 2 * AES_MIN_KEY_SIZE,
	.max_keysize	= 2 * AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.setkey		= xts_set_key,
	.encrypt	= xts_encrypt,
	.decrypt	= xts_decrypt,
} };

static struct simd_skcipher_alg *aes_simd_algs[ARRAY_SIZE(aes_algs)];

static void aes_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_simd_algs) && aes_simd_algs[i]; i++)
		simd_skcipher_free(aes_simd_algs[i]);

	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
}

static int __init aes_init(void)
{
	struct simd_skcipher_alg *simd;
	const char *basename;
	const char *algname;
	const char *drvname;
	int err;
	int i;

	err = crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		algname = aes_algs[i].base.cra_name + 2;
		drvname = aes_algs[i].base.cra_driver_name + 2;
		basename = aes_algs[i].base.cra_driver_name;
		simd = simd_skcipher_create_compat(algname, drvname, basename);
		err = PTR_ERR(simd);
		if (IS_ERR(simd))
			goto unregister_simds;

		aes_simd_algs[i] = simd;
	}

	return 0;

unregister_simds:
	aes_exit();
	return err;
}

#ifdef USE_V8_CRYPTO_EXTENSIONS
module_cpu_feature_match(AES, aes_init);
#else
module_init(aes_init);
#endif
module_exit(aes_exit);
