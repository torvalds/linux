// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for assembler optimized version of 3DES
 *
 * Copyright © 2014 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * CBC & ECB parts based on code (crypto/cbc.c,ecb.c) by:
 *   Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/algapi.h>
#include <crypto/des.h>
#include <crypto/internal/skcipher.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

struct des3_ede_x86_ctx {
	struct des3_ede_ctx enc;
	struct des3_ede_ctx dec;
};

/* regular block cipher functions */
asmlinkage void des3_ede_x86_64_crypt_blk(const u32 *expkey, u8 *dst,
					  const u8 *src);

/* 3-way parallel cipher functions */
asmlinkage void des3_ede_x86_64_crypt_blk_3way(const u32 *expkey, u8 *dst,
					       const u8 *src);

static inline void des3_ede_enc_blk(struct des3_ede_x86_ctx *ctx, u8 *dst,
				    const u8 *src)
{
	u32 *enc_ctx = ctx->enc.expkey;

	des3_ede_x86_64_crypt_blk(enc_ctx, dst, src);
}

static inline void des3_ede_dec_blk(struct des3_ede_x86_ctx *ctx, u8 *dst,
				    const u8 *src)
{
	u32 *dec_ctx = ctx->dec.expkey;

	des3_ede_x86_64_crypt_blk(dec_ctx, dst, src);
}

static inline void des3_ede_dec_blk_3way(struct des3_ede_x86_ctx *ctx, u8 *dst,
					 const u8 *src)
{
	u32 *dec_ctx = ctx->dec.expkey;

	des3_ede_x86_64_crypt_blk_3way(dec_ctx, dst, src);
}

static void des3_ede_x86_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	des3_ede_enc_blk(crypto_tfm_ctx(tfm), dst, src);
}

static void des3_ede_x86_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	des3_ede_dec_blk(crypto_tfm_ctx(tfm), dst, src);
}

static int ecb_crypt(struct skcipher_request *req, const u32 *expkey)
{
	const unsigned int bsize = DES3_EDE_BLOCK_SIZE;
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		u8 *wsrc = walk.src.virt.addr;
		u8 *wdst = walk.dst.virt.addr;

		/* Process four block batch */
		if (nbytes >= bsize * 3) {
			do {
				des3_ede_x86_64_crypt_blk_3way(expkey, wdst,
							       wsrc);

				wsrc += bsize * 3;
				wdst += bsize * 3;
				nbytes -= bsize * 3;
			} while (nbytes >= bsize * 3);

			if (nbytes < bsize)
				goto done;
		}

		/* Handle leftovers */
		do {
			des3_ede_x86_64_crypt_blk(expkey, wdst, wsrc);

			wsrc += bsize;
			wdst += bsize;
			nbytes -= bsize;
		} while (nbytes >= bsize);

done:
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct des3_ede_x86_ctx *ctx = crypto_skcipher_ctx(tfm);

	return ecb_crypt(req, ctx->enc.expkey);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct des3_ede_x86_ctx *ctx = crypto_skcipher_ctx(tfm);

	return ecb_crypt(req, ctx->dec.expkey);
}

static unsigned int __cbc_encrypt(struct des3_ede_x86_ctx *ctx,
				  struct skcipher_walk *walk)
{
	unsigned int bsize = DES3_EDE_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 *iv = (u64 *)walk->iv;

	do {
		*dst = *src ^ *iv;
		des3_ede_enc_blk(ctx, (u8 *)dst, (u8 *)dst);
		iv = dst;

		src += 1;
		dst += 1;
		nbytes -= bsize;
	} while (nbytes >= bsize);

	*(u64 *)walk->iv = *iv;
	return nbytes;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct des3_ede_x86_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		nbytes = __cbc_encrypt(ctx, &walk);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static unsigned int __cbc_decrypt(struct des3_ede_x86_ctx *ctx,
				  struct skcipher_walk *walk)
{
	unsigned int bsize = DES3_EDE_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 ivs[3 - 1];
	u64 last_iv;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	/* Process four block batch */
	if (nbytes >= bsize * 3) {
		do {
			nbytes -= bsize * 3 - bsize;
			src -= 3 - 1;
			dst -= 3 - 1;

			ivs[0] = src[0];
			ivs[1] = src[1];

			des3_ede_dec_blk_3way(ctx, (u8 *)dst, (u8 *)src);

			dst[1] ^= ivs[0];
			dst[2] ^= ivs[1];

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			*dst ^= *(src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * 3);
	}

	/* Handle leftovers */
	for (;;) {
		des3_ede_dec_blk(ctx, (u8 *)dst, (u8 *)src);

		nbytes -= bsize;
		if (nbytes < bsize)
			break;

		*dst ^= *(src - 1);
		src -= 1;
		dst -= 1;
	}

done:
	*dst ^= *(u64 *)walk->iv;
	*(u64 *)walk->iv = last_iv;

	return nbytes;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct des3_ede_x86_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		nbytes = __cbc_decrypt(ctx, &walk);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int des3_ede_x86_setkey(struct crypto_tfm *tfm, const u8 *key,
			       unsigned int keylen)
{
	struct des3_ede_x86_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 i, j, tmp;
	int err;

	err = des3_ede_expand_key(&ctx->enc, key, keylen);
	if (err == -ENOKEY) {
		if (crypto_tfm_get_flags(tfm) & CRYPTO_TFM_REQ_FORBID_WEAK_KEYS)
			err = -EINVAL;
		else
			err = 0;
	}

	if (err) {
		memset(ctx, 0, sizeof(*ctx));
		return err;
	}

	/* Fix encryption context for this implementation and form decryption
	 * context. */
	j = DES3_EDE_EXPKEY_WORDS - 2;
	for (i = 0; i < DES3_EDE_EXPKEY_WORDS; i += 2, j -= 2) {
		tmp = ror32(ctx->enc.expkey[i + 1], 4);
		ctx->enc.expkey[i + 1] = tmp;

		ctx->dec.expkey[j + 0] = ctx->enc.expkey[i + 0];
		ctx->dec.expkey[j + 1] = tmp;
	}

	return 0;
}

static int des3_ede_x86_setkey_skcipher(struct crypto_skcipher *tfm,
					const u8 *key,
					unsigned int keylen)
{
	return des3_ede_x86_setkey(&tfm->base, key, keylen);
}

static struct crypto_alg des3_ede_cipher = {
	.cra_name		= "des3_ede",
	.cra_driver_name	= "des3_ede-asm",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= DES3_EDE_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct des3_ede_x86_ctx),
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.cipher = {
			.cia_min_keysize	= DES3_EDE_KEY_SIZE,
			.cia_max_keysize	= DES3_EDE_KEY_SIZE,
			.cia_setkey		= des3_ede_x86_setkey,
			.cia_encrypt		= des3_ede_x86_encrypt,
			.cia_decrypt		= des3_ede_x86_decrypt,
		}
	}
};

static struct skcipher_alg des3_ede_skciphers[] = {
	{
		.base.cra_name		= "ecb(des3_ede)",
		.base.cra_driver_name	= "ecb-des3_ede-asm",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct des3_ede_x86_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.setkey			= des3_ede_x86_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "cbc(des3_ede)",
		.base.cra_driver_name	= "cbc-des3_ede-asm",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct des3_ede_x86_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= DES3_EDE_KEY_SIZE,
		.max_keysize		= DES3_EDE_KEY_SIZE,
		.ivsize			= DES3_EDE_BLOCK_SIZE,
		.setkey			= des3_ede_x86_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}
};

static bool is_blacklisted_cpu(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return false;

	if (boot_cpu_data.x86 == 0x0f) {
		/*
		 * On Pentium 4, des3_ede-x86_64 is slower than generic C
		 * implementation because use of 64bit rotates (which are really
		 * slow on P4). Therefore blacklist P4s.
		 */
		return true;
	}

	return false;
}

static int force;
module_param(force, int, 0);
MODULE_PARM_DESC(force, "Force module load, ignore CPU blacklist");

static int __init des3_ede_x86_init(void)
{
	int err;

	if (!force && is_blacklisted_cpu()) {
		pr_info("des3_ede-x86_64: performance on this CPU would be suboptimal: disabling des3_ede-x86_64.\n");
		return -ENODEV;
	}

	err = crypto_register_alg(&des3_ede_cipher);
	if (err)
		return err;

	err = crypto_register_skciphers(des3_ede_skciphers,
					ARRAY_SIZE(des3_ede_skciphers));
	if (err)
		crypto_unregister_alg(&des3_ede_cipher);

	return err;
}

static void __exit des3_ede_x86_fini(void)
{
	crypto_unregister_alg(&des3_ede_cipher);
	crypto_unregister_skciphers(des3_ede_skciphers,
				    ARRAY_SIZE(des3_ede_skciphers));
}

module_init(des3_ede_x86_init);
module_exit(des3_ede_x86_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Triple DES EDE Cipher Algorithm, asm optimized");
MODULE_ALIAS_CRYPTO("des3_ede");
MODULE_ALIAS_CRYPTO("des3_ede-asm");
MODULE_AUTHOR("Jussi Kivilinna <jussi.kivilinna@iki.fi>");
