// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue Code for the AVX assembler implementation of the Cast5 Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 */

#include <asm/crypto/glue_helper.h>
#include <crypto/algapi.h>
#include <crypto/cast5.h>
#include <crypto/internal/simd.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>

#define CAST5_PARALLEL_BLOCKS 16

asmlinkage void cast5_ecb_enc_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_ecb_dec_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_cbc_dec_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_ctr_16way(struct cast5_ctx *ctx, u8 *dst, const u8 *src,
				__be64 *iv);

static int cast5_setkey_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	return cast5_setkey(&tfm->base, key, keylen);
}

static inline bool cast5_fpu_begin(bool fpu_enabled, struct skcipher_walk *walk,
				   unsigned int nbytes)
{
	return glue_fpu_begin(CAST5_BLOCK_SIZE, CAST5_PARALLEL_BLOCKS,
			      walk, fpu_enabled, nbytes);
}

static inline void cast5_fpu_end(bool fpu_enabled)
{
	return glue_fpu_end(fpu_enabled);
}

static int ecb_crypt(struct skcipher_request *req, bool enc)
{
	bool fpu_enabled = false;
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cast5_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	const unsigned int bsize = CAST5_BLOCK_SIZE;
	unsigned int nbytes;
	void (*fn)(struct cast5_ctx *ctx, u8 *dst, const u8 *src);
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		u8 *wsrc = walk.src.virt.addr;
		u8 *wdst = walk.dst.virt.addr;

		fpu_enabled = cast5_fpu_begin(fpu_enabled, &walk, nbytes);

		/* Process multi-block batch */
		if (nbytes >= bsize * CAST5_PARALLEL_BLOCKS) {
			fn = (enc) ? cast5_ecb_enc_16way : cast5_ecb_dec_16way;
			do {
				fn(ctx, wdst, wsrc);

				wsrc += bsize * CAST5_PARALLEL_BLOCKS;
				wdst += bsize * CAST5_PARALLEL_BLOCKS;
				nbytes -= bsize * CAST5_PARALLEL_BLOCKS;
			} while (nbytes >= bsize * CAST5_PARALLEL_BLOCKS);

			if (nbytes < bsize)
				goto done;
		}

		fn = (enc) ? __cast5_encrypt : __cast5_decrypt;

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

	cast5_fpu_end(fpu_enabled);
	return err;
}

static int ecb_encrypt(struct skcipher_request *req)
{
	return ecb_crypt(req, true);
}

static int ecb_decrypt(struct skcipher_request *req)
{
	return ecb_crypt(req, false);
}

static int cbc_encrypt(struct skcipher_request *req)
{
	const unsigned int bsize = CAST5_BLOCK_SIZE;
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cast5_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		u64 *src = (u64 *)walk.src.virt.addr;
		u64 *dst = (u64 *)walk.dst.virt.addr;
		u64 *iv = (u64 *)walk.iv;

		do {
			*dst = *src ^ *iv;
			__cast5_encrypt(ctx, (u8 *)dst, (u8 *)dst);
			iv = dst;
			src++;
			dst++;
			nbytes -= bsize;
		} while (nbytes >= bsize);

		*(u64 *)walk.iv = *iv;
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static unsigned int __cbc_decrypt(struct cast5_ctx *ctx,
				  struct skcipher_walk *walk)
{
	const unsigned int bsize = CAST5_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 last_iv;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	/* Process multi-block batch */
	if (nbytes >= bsize * CAST5_PARALLEL_BLOCKS) {
		do {
			nbytes -= bsize * (CAST5_PARALLEL_BLOCKS - 1);
			src -= CAST5_PARALLEL_BLOCKS - 1;
			dst -= CAST5_PARALLEL_BLOCKS - 1;

			cast5_cbc_dec_16way(ctx, (u8 *)dst, (u8 *)src);

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			*dst ^= *(src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * CAST5_PARALLEL_BLOCKS);
	}

	/* Handle leftovers */
	for (;;) {
		__cast5_decrypt(ctx, (u8 *)dst, (u8 *)src);

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
	struct cast5_ctx *ctx = crypto_skcipher_ctx(tfm);
	bool fpu_enabled = false;
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		fpu_enabled = cast5_fpu_begin(fpu_enabled, &walk, nbytes);
		nbytes = __cbc_decrypt(ctx, &walk);
		err = skcipher_walk_done(&walk, nbytes);
	}

	cast5_fpu_end(fpu_enabled);
	return err;
}

static void ctr_crypt_final(struct skcipher_walk *walk, struct cast5_ctx *ctx)
{
	u8 *ctrblk = walk->iv;
	u8 keystream[CAST5_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	__cast5_encrypt(ctx, keystream, ctrblk);
	crypto_xor_cpy(dst, keystream, src, nbytes);

	crypto_inc(ctrblk, CAST5_BLOCK_SIZE);
}

static unsigned int __ctr_crypt(struct skcipher_walk *walk,
				struct cast5_ctx *ctx)
{
	const unsigned int bsize = CAST5_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;

	/* Process multi-block batch */
	if (nbytes >= bsize * CAST5_PARALLEL_BLOCKS) {
		do {
			cast5_ctr_16way(ctx, (u8 *)dst, (u8 *)src,
					(__be64 *)walk->iv);

			src += CAST5_PARALLEL_BLOCKS;
			dst += CAST5_PARALLEL_BLOCKS;
			nbytes -= bsize * CAST5_PARALLEL_BLOCKS;
		} while (nbytes >= bsize * CAST5_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Handle leftovers */
	do {
		u64 ctrblk;

		if (dst != src)
			*dst = *src;

		ctrblk = *(u64 *)walk->iv;
		be64_add_cpu((__be64 *)walk->iv, 1);

		__cast5_encrypt(ctx, (u8 *)&ctrblk, (u8 *)&ctrblk);
		*dst ^= ctrblk;

		src += 1;
		dst += 1;
		nbytes -= bsize;
	} while (nbytes >= bsize);

done:
	return nbytes;
}

static int ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct cast5_ctx *ctx = crypto_skcipher_ctx(tfm);
	bool fpu_enabled = false;
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) >= CAST5_BLOCK_SIZE) {
		fpu_enabled = cast5_fpu_begin(fpu_enabled, &walk, nbytes);
		nbytes = __ctr_crypt(&walk, ctx);
		err = skcipher_walk_done(&walk, nbytes);
	}

	cast5_fpu_end(fpu_enabled);

	if (walk.nbytes) {
		ctr_crypt_final(&walk, ctx);
		err = skcipher_walk_done(&walk, 0);
	}

	return err;
}

static struct skcipher_alg cast5_algs[] = {
	{
		.base.cra_name		= "__ecb(cast5)",
		.base.cra_driver_name	= "__ecb-cast5-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST5_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast5_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST5_MIN_KEY_SIZE,
		.max_keysize		= CAST5_MAX_KEY_SIZE,
		.setkey			= cast5_setkey_skcipher,
		.encrypt		= ecb_encrypt,
		.decrypt		= ecb_decrypt,
	}, {
		.base.cra_name		= "__cbc(cast5)",
		.base.cra_driver_name	= "__cbc-cast5-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= CAST5_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct cast5_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST5_MIN_KEY_SIZE,
		.max_keysize		= CAST5_MAX_KEY_SIZE,
		.ivsize			= CAST5_BLOCK_SIZE,
		.setkey			= cast5_setkey_skcipher,
		.encrypt		= cbc_encrypt,
		.decrypt		= cbc_decrypt,
	}, {
		.base.cra_name		= "__ctr(cast5)",
		.base.cra_driver_name	= "__ctr-cast5-avx",
		.base.cra_priority	= 200,
		.base.cra_flags		= CRYPTO_ALG_INTERNAL,
		.base.cra_blocksize	= 1,
		.base.cra_ctxsize	= sizeof(struct cast5_ctx),
		.base.cra_module	= THIS_MODULE,
		.min_keysize		= CAST5_MIN_KEY_SIZE,
		.max_keysize		= CAST5_MAX_KEY_SIZE,
		.ivsize			= CAST5_BLOCK_SIZE,
		.chunksize		= CAST5_BLOCK_SIZE,
		.setkey			= cast5_setkey_skcipher,
		.encrypt		= ctr_crypt,
		.decrypt		= ctr_crypt,
	}
};

static struct simd_skcipher_alg *cast5_simd_algs[ARRAY_SIZE(cast5_algs)];

static int __init cast5_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return simd_register_skciphers_compat(cast5_algs,
					      ARRAY_SIZE(cast5_algs),
					      cast5_simd_algs);
}

static void __exit cast5_exit(void)
{
	simd_unregister_skciphers(cast5_algs, ARRAY_SIZE(cast5_algs),
				  cast5_simd_algs);
}

module_init(cast5_init);
module_exit(cast5_exit);

MODULE_DESCRIPTION("Cast5 Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("cast5");
