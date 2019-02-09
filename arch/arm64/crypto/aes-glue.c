/*
 * linux/arch/arm64/crypto/aes-glue.c - wrapper code for ARMv8 AES
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/neon.h>
#include <asm/hwcap.h>
#include <asm/simd.h>
#include <crypto/aes.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <crypto/xts.h>

#include "aes-ce-setkey.h"
#include "aes-ctr-fallback.h"

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
#define aes_mac_update		ce_aes_mac_update
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
#define aes_mac_update		neon_aes_mac_update
MODULE_DESCRIPTION("AES-ECB/CBC/CTR/XTS using ARMv8 NEON");
MODULE_ALIAS_CRYPTO("ecb(aes)");
MODULE_ALIAS_CRYPTO("cbc(aes)");
MODULE_ALIAS_CRYPTO("ctr(aes)");
MODULE_ALIAS_CRYPTO("xts(aes)");
MODULE_ALIAS_CRYPTO("cmac(aes)");
MODULE_ALIAS_CRYPTO("xcbc(aes)");
MODULE_ALIAS_CRYPTO("cbcmac(aes)");
#endif

MODULE_AUTHOR("Ard Biesheuvel <ard.biesheuvel@linaro.org>");
MODULE_LICENSE("GPL v2");

/* defined in aes-modes.S */
asmlinkage void aes_ecb_encrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks);
asmlinkage void aes_ecb_decrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks);

asmlinkage void aes_cbc_encrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, u8 iv[]);
asmlinkage void aes_cbc_decrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, u8 iv[]);

asmlinkage void aes_ctr_encrypt(u8 out[], u8 const in[], u8 const rk[],
				int rounds, int blocks, u8 ctr[]);

asmlinkage void aes_xts_encrypt(u8 out[], u8 const in[], u8 const rk1[],
				int rounds, int blocks, u8 const rk2[], u8 iv[],
				int first);
asmlinkage void aes_xts_decrypt(u8 out[], u8 const in[], u8 const rk1[],
				int rounds, int blocks, u8 const rk2[], u8 iv[],
				int first);

asmlinkage void aes_mac_update(u8 const in[], u32 const rk[], int rounds,
			       int blocks, u8 dg[], int enc_before,
			       int enc_after);

struct crypto_aes_xts_ctx {
	struct crypto_aes_ctx key1;
	struct crypto_aes_ctx __aligned(8) key2;
};

struct mac_tfm_ctx {
	struct crypto_aes_ctx key;
	u8 __aligned(8) consts[];
};

struct mac_desc_ctx {
	unsigned int len;
	u8 dg[AES_BLOCK_SIZE];
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
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_ecb_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_enc, rounds, blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_ecb_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_dec, rounds, blocks);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_cbc_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_enc, rounds, blocks, walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_cbc_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_dec, rounds, blocks, walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	return err;
}

static int ctr_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, rounds = 6 + ctx->key_length / 4;
	struct skcipher_walk walk;
	int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	while ((blocks = (walk.nbytes / AES_BLOCK_SIZE))) {
		kernel_neon_begin();
		aes_ctr_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key_enc, rounds, blocks, walk.iv);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}
	if (walk.nbytes) {
		u8 __aligned(8) tail[AES_BLOCK_SIZE];
		unsigned int nbytes = walk.nbytes;
		u8 *tdst = walk.dst.virt.addr;
		u8 *tsrc = walk.src.virt.addr;

		/*
		 * Tell aes_ctr_encrypt() to process a tail block.
		 */
		blocks = -1;

		kernel_neon_begin();
		aes_ctr_encrypt(tail, NULL, (u8 *)ctx->key_enc, rounds,
				blocks, walk.iv);
		kernel_neon_end();
		crypto_xor_cpy(tdst, tsrc, tail, nbytes);
		err = skcipher_walk_done(&walk, 0);
	}

	return err;
}

static int ctr_encrypt_sync(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (!may_use_simd())
		return aes_ctr_encrypt_fallback(ctx, req);

	return ctr_encrypt(req);
}

static int xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key1.key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		kernel_neon_begin();
		aes_xts_encrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key1.key_enc, rounds, blocks,
				(u8 *)ctx->key2.key_enc, walk.iv, first);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}

	return err;
}

static int xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_xts_ctx *ctx = crypto_skcipher_ctx(tfm);
	int err, first, rounds = 6 + ctx->key1.key_length / 4;
	struct skcipher_walk walk;
	unsigned int blocks;

	err = skcipher_walk_virt(&walk, req, false);

	for (first = 1; (blocks = (walk.nbytes / AES_BLOCK_SIZE)); first = 0) {
		kernel_neon_begin();
		aes_xts_decrypt(walk.dst.virt.addr, walk.src.virt.addr,
				(u8 *)ctx->key1.key_dec, rounds, blocks,
				(u8 *)ctx->key2.key_enc, walk.iv, first);
		kernel_neon_end();
		err = skcipher_walk_done(&walk, walk.nbytes % AES_BLOCK_SIZE);
	}

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
		.cra_name		= "ctr(aes)",
		.cra_driver_name	= "ctr-aes-" MODE,
		.cra_priority		= PRIO - 1,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.chunksize	= AES_BLOCK_SIZE,
	.setkey		= skcipher_aes_setkey,
	.encrypt	= ctr_encrypt_sync,
	.decrypt	= ctr_encrypt_sync,
}, {
	.base = {
		.cra_name		= "__xts(aes)",
		.cra_driver_name	= "__xts-aes-" MODE,
		.cra_priority		= PRIO,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct crypto_aes_xts_ctx),
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= 2 * AES_MIN_KEY_SIZE,
	.max_keysize	= 2 * AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.setkey		= xts_set_key,
	.encrypt	= xts_encrypt,
	.decrypt	= xts_decrypt,
} };

static int cbcmac_setkey(struct crypto_shash *tfm, const u8 *in_key,
			 unsigned int key_len)
{
	struct mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	int err;

	err = aes_expandkey(&ctx->key, in_key, key_len);
	if (err)
		crypto_shash_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);

	return err;
}

static void cmac_gf128_mul_by_x(be128 *y, const be128 *x)
{
	u64 a = be64_to_cpu(x->a);
	u64 b = be64_to_cpu(x->b);

	y->a = cpu_to_be64((a << 1) | (b >> 63));
	y->b = cpu_to_be64((b << 1) ^ ((a >> 63) ? 0x87 : 0));
}

static int cmac_setkey(struct crypto_shash *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	struct mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	be128 *consts = (be128 *)ctx->consts;
	u8 *rk = (u8 *)ctx->key.key_enc;
	int rounds = 6 + key_len / 4;
	int err;

	err = cbcmac_setkey(tfm, in_key, key_len);
	if (err)
		return err;

	/* encrypt the zero vector */
	kernel_neon_begin();
	aes_ecb_encrypt(ctx->consts, (u8[AES_BLOCK_SIZE]){}, rk, rounds, 1);
	kernel_neon_end();

	cmac_gf128_mul_by_x(consts, consts);
	cmac_gf128_mul_by_x(consts + 1, consts);

	return 0;
}

static int xcbc_setkey(struct crypto_shash *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	static u8 const ks[3][AES_BLOCK_SIZE] = {
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x1 },
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x2 },
		{ [0 ... AES_BLOCK_SIZE - 1] = 0x3 },
	};

	struct mac_tfm_ctx *ctx = crypto_shash_ctx(tfm);
	u8 *rk = (u8 *)ctx->key.key_enc;
	int rounds = 6 + key_len / 4;
	u8 key[AES_BLOCK_SIZE];
	int err;

	err = cbcmac_setkey(tfm, in_key, key_len);
	if (err)
		return err;

	kernel_neon_begin();
	aes_ecb_encrypt(key, ks[0], rk, rounds, 1);
	aes_ecb_encrypt(ctx->consts, ks[1], rk, rounds, 2);
	kernel_neon_end();

	return cbcmac_setkey(tfm, key, sizeof(key));
}

static int mac_init(struct shash_desc *desc)
{
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);

	memset(ctx->dg, 0, AES_BLOCK_SIZE);
	ctx->len = 0;

	return 0;
}

static void mac_do_update(struct crypto_aes_ctx *ctx, u8 const in[], int blocks,
			  u8 dg[], int enc_before, int enc_after)
{
	int rounds = 6 + ctx->key_length / 4;

	if (may_use_simd()) {
		kernel_neon_begin();
		aes_mac_update(in, ctx->key_enc, rounds, blocks, dg, enc_before,
			       enc_after);
		kernel_neon_end();
	} else {
		if (enc_before)
			__aes_arm64_encrypt(ctx->key_enc, dg, dg, rounds);

		while (blocks--) {
			crypto_xor(dg, in, AES_BLOCK_SIZE);
			in += AES_BLOCK_SIZE;

			if (blocks || enc_after)
				__aes_arm64_encrypt(ctx->key_enc, dg, dg,
						    rounds);
		}
	}
}

static int mac_update(struct shash_desc *desc, const u8 *p, unsigned int len)
{
	struct mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);

	while (len > 0) {
		unsigned int l;

		if ((ctx->len % AES_BLOCK_SIZE) == 0 &&
		    (ctx->len + len) > AES_BLOCK_SIZE) {

			int blocks = len / AES_BLOCK_SIZE;

			len %= AES_BLOCK_SIZE;

			mac_do_update(&tctx->key, p, blocks, ctx->dg,
				      (ctx->len != 0), (len != 0));

			p += blocks * AES_BLOCK_SIZE;

			if (!len) {
				ctx->len = AES_BLOCK_SIZE;
				break;
			}
			ctx->len = 0;
		}

		l = min(len, AES_BLOCK_SIZE - ctx->len);

		if (l <= AES_BLOCK_SIZE) {
			crypto_xor(ctx->dg + ctx->len, p, l);
			ctx->len += l;
			len -= l;
			p += l;
		}
	}

	return 0;
}

static int cbcmac_final(struct shash_desc *desc, u8 *out)
{
	struct mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);

	mac_do_update(&tctx->key, NULL, 0, ctx->dg, 1, 0);

	memcpy(out, ctx->dg, AES_BLOCK_SIZE);

	return 0;
}

static int cmac_final(struct shash_desc *desc, u8 *out)
{
	struct mac_tfm_ctx *tctx = crypto_shash_ctx(desc->tfm);
	struct mac_desc_ctx *ctx = shash_desc_ctx(desc);
	u8 *consts = tctx->consts;

	if (ctx->len != AES_BLOCK_SIZE) {
		ctx->dg[ctx->len] ^= 0x80;
		consts += AES_BLOCK_SIZE;
	}

	mac_do_update(&tctx->key, consts, 1, ctx->dg, 0, 1);

	memcpy(out, ctx->dg, AES_BLOCK_SIZE);

	return 0;
}

static struct shash_alg mac_algs[] = { {
	.base.cra_name		= "cmac(aes)",
	.base.cra_driver_name	= "cmac-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mac_tfm_ctx) +
				  2 * AES_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= AES_BLOCK_SIZE,
	.init			= mac_init,
	.update			= mac_update,
	.final			= cmac_final,
	.setkey			= cmac_setkey,
	.descsize		= sizeof(struct mac_desc_ctx),
}, {
	.base.cra_name		= "xcbc(aes)",
	.base.cra_driver_name	= "xcbc-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct mac_tfm_ctx) +
				  2 * AES_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,

	.digestsize		= AES_BLOCK_SIZE,
	.init			= mac_init,
	.update			= mac_update,
	.final			= cmac_final,
	.setkey			= xcbc_setkey,
	.descsize		= sizeof(struct mac_desc_ctx),
}, {
	.base.cra_name		= "cbcmac(aes)",
	.base.cra_driver_name	= "cbcmac-aes-" MODE,
	.base.cra_priority	= PRIO,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct mac_tfm_ctx),
	.base.cra_module	= THIS_MODULE,

	.digestsize		= AES_BLOCK_SIZE,
	.init			= mac_init,
	.update			= mac_update,
	.final			= cbcmac_final,
	.setkey			= cbcmac_setkey,
	.descsize		= sizeof(struct mac_desc_ctx),
} };

static struct simd_skcipher_alg *aes_simd_algs[ARRAY_SIZE(aes_algs)];

static void aes_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aes_simd_algs); i++)
		if (aes_simd_algs[i])
			simd_skcipher_free(aes_simd_algs[i]);

	crypto_unregister_shashes(mac_algs, ARRAY_SIZE(mac_algs));
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

	err = crypto_register_shashes(mac_algs, ARRAY_SIZE(mac_algs));
	if (err)
		goto unregister_ciphers;

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
unregister_ciphers:
	crypto_unregister_skciphers(aes_algs, ARRAY_SIZE(aes_algs));
	return err;
}

#ifdef USE_V8_CRYPTO_EXTENSIONS
module_cpu_feature_match(AES, aes_init);
#else
module_init(aes_init);
EXPORT_SYMBOL(neon_aes_ecb_encrypt);
EXPORT_SYMBOL(neon_aes_cbc_encrypt);
#endif
module_exit(aes_exit);
