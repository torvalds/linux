// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for assembler optimized version of Blowfish
 *
 * Copyright (c) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * CBC & ECB parts based on code (crypto/cbc.c,ecb.c) by:
 *   Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <crypto/algapi.h>
#include <crypto/blowfish.h>
#include <crypto/internal/skcipher.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>

/* regular block cipher functions */
asmlinkage void __blowfish_enc_blk(struct bf_ctx *ctx, u8 *dst, const u8 *src,
				   bool xor);
asmlinkage void blowfish_dec_blk(struct bf_ctx *ctx, u8 *dst, const u8 *src);

/* 4-way parallel cipher functions */
asmlinkage void __blowfish_enc_blk_4way(struct bf_ctx *ctx, u8 *dst,
					const u8 *src, bool xor);
asmlinkage void blowfish_dec_blk_4way(struct bf_ctx *ctx, u8 *dst,
				      const u8 *src);

static inline void blowfish_enc_blk(struct bf_ctx *ctx, u8 *dst, const u8 *src)
{
	__blowfish_enc_blk(ctx, dst, src, false);
}

static inline void blowfish_enc_blk_xor(struct bf_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__blowfish_enc_blk(ctx, dst, src, true);
}

static inline void blowfish_enc_blk_4way(struct bf_ctx *ctx, u8 *dst,
					 const u8 *src)
{
	__blowfish_enc_blk_4way(ctx, dst, src, false);
}

static inline void blowfish_enc_blk_xor_4way(struct bf_ctx *ctx, u8 *dst,
				      const u8 *src)
{
	__blowfish_enc_blk_4way(ctx, dst, src, true);
}

static void blowfish_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	blowfish_enc_blk(crypto_tfm_ctx(tfm), dst, src);
}

static void blowfish_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	blowfish_dec_blk(crypto_tfm_ctx(tfm), dst, src);
}

static int blowfish_setkey_skcipher(struct crypto_skcipher *tfm,
				    const u8 *key, unsigned int keylen)
{
	return blowfish_setkey(&tfm->base, key, keylen);
}

static int ecb_crypt(struct skcipher_request *req,
		     void (*fn)(struct bf_ctx *, u8 *, const u8 *),
		     void (*fn_4way)(struct bf_ctx *, u8 *, const u8 *))
{
	unsigned int bsize = BF_BLOCK_SIZE;
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct bf_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		u8 *wsrc = walk.src.virt.addr;
		u8 *wdst = walk.dst.virt.addr;

		/* Process four block batch */
		if (nbytes >= bsize * 4) {
			do {
				fn_4way(ctx, wdst, wsrc);

				wsrc += bsize * 4;
				wdst += bsize * 4;
				nbytes -= bsize * 4;
			} while (nbytes >= bsize * 4);

			if (nbytes < bsize)
				goto done;
		}

		/* Handle leftovers */
		do {
			fn(ctx, wdst, wsrc);

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
	return ecb_crypt(req, blowfish_enc_blk, blowfish_enc_blk_4way);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return ecb_crypt(req, blowfish_dec_blk, blowfish_dec_blk_4way);
}

static unsigned int __cbc_encrypt(struct bf_ctx *ctx,
				  struct skcipher_walk *walk)
{
	unsigned int bsize = BF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 *iv = (u64 *)walk->iv;

	do {
		*dst = *src ^ *iv;
		blowfish_enc_blk(ctx, (u8 *)dst, (u8 *)dst);
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
	struct bf_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		nbytes = __cbc_encrypt(ctx, &walk);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static unsigned int __cbc_decrypt(struct bf_ctx *ctx,
				  struct skcipher_walk *walk)
{
	unsigned int bsize = BF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 ivs[4 - 1];
	u64 last_iv;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	/* Process four block batch */
	if (nbytes >= bsize * 4) {
		do {
			nbytes -= bsize * 4 - bsize;
			src -= 4 - 1;
			dst -= 4 - 1;

			ivs[0] = src[0];
			ivs[1] = src[1];
			ivs[2] = src[2];

			blowfish_dec_blk_4way(ctx, (u8 *)dst, (u8 *)src);

			dst[1] ^= ivs[0];
			dst[2] ^= ivs[1];
			dst[3] ^= ivs[2];

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			*dst ^= *(src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * 4);
	}

	/* Handle leftovers */
	for (;;) {
		blowfish_dec_blk(ctx, (u8 *)dst, (u8 *)src);

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
	struct bf_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		nbytes = __cbc_decrypt(ctx, &walk);
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static struct crypto_alg bf_cipher_alg = {
	.cra_name		= "blowfish",
	.cra_driver_name	= "blowfish-asm",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.cipher = {
			.cia_min_keysize	= BF_MIN_KEY_SIZE,
			.cia_max_keysize	= BF_MAX_KEY_SIZE,
			.cia_setkey		= blowfish_setkey,
			.cia_encrypt		= blowfish_encrypt,
			.cia_decrypt		= blowfish_decrypt,
		}
	}
};

static struct skcipher_alg bf_skcipher_algs[] = {
	{
		.base.cra_name		= "ecb(blowfish)",
		.base.cra_driver_name	= "ecb-blowfish-asm",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= BF_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct bf_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= BF_MIN_KEY_SIZE,
		.max_keysize		= BF_MAX_KEY_SIZE,
		.setkey			= blowfish_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "cbc(blowfish)",
		.base.cra_driver_name	= "cbc-blowfish-asm",
		.base.cra_priority	= 300,
		.base.cra_blocksize	= BF_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct bf_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= BF_MIN_KEY_SIZE,
		.max_keysize		= BF_MAX_KEY_SIZE,
		.ivsize			= BF_BLOCK_SIZE,
		.setkey			= blowfish_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	},
};

static bool is_blacklisted_cpu(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return false;

	if (boot_cpu_data.x86 == 0x0f) {
		/*
		 * On Pentium 4, blowfish-x86_64 is slower than generic C
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

static int __init init(void)
{
	int err;

	if (!force && is_blacklisted_cpu()) {
		printk(KERN_INFO
			"blowfish-x86_64: performance on this CPU "
			"would be suboptimal: disabling "
			"blowfish-x86_64.\n");
		return -ENODEV;
	}

	err = crypto_register_alg(&bf_cipher_alg);
	if (err)
		return err;

	err = crypto_register_skciphers(bf_skcipher_algs,
					ARRAY_SIZE(bf_skcipher_algs));
	if (err)
		crypto_unregister_alg(&bf_cipher_alg);

	return err;
}

static void __exit fini(void)
{
	crypto_unregister_alg(&bf_cipher_alg);
	crypto_unregister_skciphers(bf_skcipher_algs,
				    ARRAY_SIZE(bf_skcipher_algs));
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blowfish Cipher Algorithm, asm optimized");
MODULE_ALIAS_CRYPTO("blowfish");
MODULE_ALIAS_CRYPTO("blowfish-asm");
