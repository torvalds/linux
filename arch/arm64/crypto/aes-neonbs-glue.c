/*
 * Bit sliced AES using NEON instructions
 *
 * Copyright (C) 2016 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/aes.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/xts.h>
#include <linux/module.h>

#include "aes-ctr-fallback.h"

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");

asmlinkage void aesbs_convert_key(u8 out[], u32 const rk[], int rounds);

asmlinkage void aesbs_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks);
asmlinkage void aesbs_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks);

asmlinkage void aesbs_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);

asmlinkage void aesbs_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[], u8 final[]);

asmlinkage void aesbs_xts_encrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);
asmlinkage void aesbs_xts_decrypt(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]);

/* borrowed from aes-neon-blk.ko */
asmlinkage void neon_aes_ecb_encrypt(u8 out[], u8 const in[], u32 const rk[],
				     int rounds, int blocks, int first);
asmlinkage void neon_aes_cbc_encrypt(u8 out[], u8 const in[], u32 const rk[],
				     int rounds, int blocks, u8 iv[],
				     int first);

struct aesbs_ctx {
	u8	rk[13 * (8 * AES_BLOCK_SIZE) + 32];
	int	rounds;
} __aligned(AES_BLOCK_SIZE);

struct aesbs_cbc_ctx {
	struct aesbs_ctx	key;
	u32			enc[AES_MAX_KEYLENGTH_U32];
};

struct aesbs_ctr_ctx {
	struct aesbs_ctx	key;		/* must be first member */
	struct crypto_aes_ctx	fallback;
};

struct aesbs_xts_ctx {
	struct aesbs_ctx	key;
	u32			twkey[AES_MAX_KEYLENGTH_U32];
};

static int aesbs_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			unsigned int key_len)
{
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = crypto_aes_expand_key(&rk, in_key, key_len);
	if (err)
		return err;

	ctx->rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->rk, rk.key_enc, ctx->rounds);
	kernel_neon_end();

	return 0;
}

static int __ecb_crypt(struct skcipher_request *req,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		fn(walk.dst.virt.addr, walk.src.virt.addr, ctx->rk,
		   ctx->rounds, blocks);
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}
	kernel_neon_end();

	return err;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, aesbs_ecb_encrypt);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return __ecb_crypt(req, aesbs_ecb_decrypt);
}

static int aesbs_cbc_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = crypto_aes_expand_key(&rk, in_key, key_len);
	if (err)
		return err;

	ctx->key.rounds = 6 + key_len / 4;

	memcpy(ctx->enc, rk.key_enc, sizeof(ctx->enc));

	kernel_neon_begin();
	aesbs_convert_key(ctx->key.rk, rk.key_enc, ctx->key.rounds);
	kernel_neon_end();

	return 0;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err, first = 1;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		/* fall back to the non-bitsliced NEON implementation */
		neon_aes_cbc_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				     ctx->enc, ctx->key.rounds, blocks, walk.iv,
				     first);
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
		first = 0;
	}
	kernel_neon_end();
	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_cbc_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		aesbs_cbc_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				  ctx->key.rk, ctx->key.rounds, blocks,
				  walk.iv);
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}
	kernel_neon_end();

	return err;
}

static int aesbs_ctr_setkey_sync(struct crypto_skcipher *tfm, const u8 *in_key,
				 unsigned int key_len)
{
	struct aesbs_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err;

	err = crypto_aes_expand_key(&ctx->fallback, in_key, key_len);
	if (err)
		return err;

	ctx->key.rounds = 6 + key_len / 4;

	kernel_neon_begin();
	aesbs_convert_key(ctx->key.rk, ctx->fallback.key_enc, ctx->key.rounds);
	kernel_neon_end();

	return 0;
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	u8 buf[AES_BLOCK_SIZE];
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();
	while (walk.nbytes > 0) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;
		u8 *final = (walk.total % AES_BLOCK_SIZE) ? buf : NULL;

		if (walk.nbytes < walk.total) {
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);
			final = NULL;
		}

		aesbs_ctr_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				  ctx->rk, ctx->rounds, blocks, walk.iv, final);

		if (final) {
			u8 *dst = walk.dst.virt.addr + blocks * AES_BLOCK_SIZE;
			u8 *src = walk.src.virt.addr + blocks * AES_BLOCK_SIZE;

			crypto_xor_cpy(dst, src, final,
				       walk.total % AES_BLOCK_SIZE);

			err = skcipher_walk_done(&walk, 0);
			break;
		}
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}
	kernel_neon_end();

	return err;
}

static int aesbs_xts_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			    unsigned int key_len)
{
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_aes_ctx rk;
	int err;

	err = xts_verify_key(tfm, in_key, key_len);
	if (err)
		return err;

	key_len /= 2;
	err = crypto_aes_expand_key(&rk, in_key + key_len, key_len);
	if (err)
		return err;

	memcpy(ctx->twkey, rk.key_enc, sizeof(ctx->twkey));

	return aesbs_setkey(tfm, in_key, key_len);
}

static int ctr_encrypt_sync(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_ctr_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (!may_use_simd())
		return aes_ctr_encrypt_fallback(&ctx->fallback, req);

	return ctr_encrypt(req);
}

static int __xts_crypt(struct skcipher_request *req,
		       void (*fn)(u8 out[], u8 const in[], u8 const rk[],
				  int rounds, int blocks, u8 iv[]))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct aesbs_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, true);

	kernel_neon_begin();

	neon_aes_ecb_encrypt(walk.iv, walk.iv, ctx->twkey,
			     ctx->key.rounds, 1, 1);

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		unsigned int blocks = walk.nbytes / AES_BLOCK_SIZE;

		if (walk.nbytes < walk.total)
			blocks = round_down(blocks,
					    walk.stride / AES_BLOCK_SIZE);

		fn(walk.dst.virt.addr, walk.src.virt.addr, ctx->key.rk,
		   ctx->key.rounds, blocks, walk.iv);
		err = skcipher_walk_done(&walk,
					 walk.nbytes - blocks * AES_BLOCK_SIZE);
	}
	kernel_neon_end();

	return err;
}

static int xts_encrypt(struct skcipher_request *req)
{
	return __xts_crypt(req, aesbs_xts_encrypt);
}

static int xts_decrypt(struct skcipher_request *req)
{
	return __xts_crypt(req, aesbs_xts_decrypt);
}

static struct skcipher_alg aes_algs[] = { {
	.base.cra_name		= "__ecb(aes)",
	.base.cra_driver_name	= "__ecb-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.setkey			= aesbs_setkey,
	.encrypt		= ecb_encrypt,
	.decrypt		= ecb_decrypt,
}, {
	.base.cra_name		= "__cbc(aes)",
	.base.cra_driver_name	= "__cbc-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_cbc_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_cbc_setkey,
	.encrypt		= cbc_encrypt,
	.decrypt		= cbc_decrypt,
}, {
	.base.cra_name		= "__ctr(aes)",
	.base.cra_driver_name	= "__ctr-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_setkey,
	.encrypt		= ctr_encrypt,
	.decrypt		= ctr_encrypt,
}, {
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "ctr-aes-neonbs",
	.base.cra_priority	= 250 - 1,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct aesbs_ctr_ctx),
	.base.cra_module	= THIS_MODULE,

	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_ctr_setkey_sync,
	.encrypt		= ctr_encrypt_sync,
	.decrypt		= ctr_encrypt_sync,
}, {
	.base.cra_name		= "__xts(aes)",
	.base.cra_driver_name	= "__xts-aes-neonbs",
	.base.cra_priority	= 250,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct aesbs_xts_ctx),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,

	.min_keysize		= 2 * AES_MIN_KEY_SIZE,
	.max_keysize		= 2 * AES_MAX_KEY_SIZE,
	.walksize		= 8 * AES_BLOCK_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= aesbs_xts_setkey,
	.encrypt		= xts_encrypt,
	.decrypt		= xts_decrypt,
} };

static struct simd_skcipher_alg *aes_simd_algs[ARRAY_SIZE(aes_algs)];

static void aes_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_simd_algs); i++)
		if (aes_simd_algs[i])
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

	if (!(elf_hwcap & HWCAP_ASIMD))
		return -ENODEV;

	err = crypto_register_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(aes_algs); i++) {
		if (!(aes_algs[i].base.cra_flags & CRYPTO_ALG_INTERNAL))
			continue;

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

module_init(aes_init);
module_exit(aes_exit);
