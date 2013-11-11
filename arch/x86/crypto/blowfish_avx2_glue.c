/*
 * Glue Code for x86_64/AVX2 assembler optimized version of Blowfish
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * CBC & ECB parts based on code (crypto/cbc.c,ecb.c) by:
 *   Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 * CTR part based on code (crypto/ctr.c) by:
 *   (C) Copyright IBM Corp. 2007 - Joy Latten <latten@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/blowfish.h>
#include <crypto/cryptd.h>
#include <crypto/ctr.h>
#include <asm/i387.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <asm/crypto/blowfish.h>
#include <asm/crypto/ablk_helper.h>
#include <crypto/scatterwalk.h>

#define BF_AVX2_PARALLEL_BLOCKS 32

/* 32-way AVX2 parallel cipher functions */
asmlinkage void blowfish_ecb_enc_32way(struct bf_ctx *ctx, u8 *dst,
				       const u8 *src);
asmlinkage void blowfish_ecb_dec_32way(struct bf_ctx *ctx, u8 *dst,
				       const u8 *src);
asmlinkage void blowfish_cbc_dec_32way(struct bf_ctx *ctx, u8 *dst,
				       const u8 *src);
asmlinkage void blowfish_ctr_32way(struct bf_ctx *ctx, u8 *dst, const u8 *src,
				   __be64 *iv);

static inline bool bf_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	if (fpu_enabled)
		return true;

	/* FPU is only used when chunk to be processed is large enough, so
	 * do not enable FPU until it is necessary.
	 */
	if (nbytes < BF_BLOCK_SIZE * BF_AVX2_PARALLEL_BLOCKS)
		return false;

	kernel_fpu_begin();
	return true;
}

static inline void bf_fpu_end(bool fpu_enabled)
{
	if (fpu_enabled)
		kernel_fpu_end();
}

static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
		     bool enc)
{
	bool fpu_enabled = false;
	struct bf_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = BF_BLOCK_SIZE;
	unsigned int nbytes;
	int err;

	err = blkcipher_walk_virt(desc, walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

		fpu_enabled = bf_fpu_begin(fpu_enabled, nbytes);

		/* Process multi-block AVX2 batch */
		if (nbytes >= bsize * BF_AVX2_PARALLEL_BLOCKS) {
			do {
				if (enc)
					blowfish_ecb_enc_32way(ctx, wdst, wsrc);
				else
					blowfish_ecb_dec_32way(ctx, wdst, wsrc);

				wsrc += bsize * BF_AVX2_PARALLEL_BLOCKS;
				wdst += bsize * BF_AVX2_PARALLEL_BLOCKS;
				nbytes -= bsize * BF_AVX2_PARALLEL_BLOCKS;
			} while (nbytes >= bsize * BF_AVX2_PARALLEL_BLOCKS);

			if (nbytes < bsize)
				goto done;
		}

		/* Process multi-block batch */
		if (nbytes >= bsize * BF_PARALLEL_BLOCKS) {
			do {
				if (enc)
					blowfish_enc_blk_4way(ctx, wdst, wsrc);
				else
					blowfish_dec_blk_4way(ctx, wdst, wsrc);

				wsrc += bsize * BF_PARALLEL_BLOCKS;
				wdst += bsize * BF_PARALLEL_BLOCKS;
				nbytes -= bsize * BF_PARALLEL_BLOCKS;
			} while (nbytes >= bsize * BF_PARALLEL_BLOCKS);

			if (nbytes < bsize)
				goto done;
		}

		/* Handle leftovers */
		do {
			if (enc)
				blowfish_enc_blk(ctx, wdst, wsrc);
			else
				blowfish_dec_blk(ctx, wdst, wsrc);

			wsrc += bsize;
			wdst += bsize;
			nbytes -= bsize;
		} while (nbytes >= bsize);

done:
		err = blkcipher_walk_done(desc, walk, nbytes);
	}

	bf_fpu_end(fpu_enabled);
	return err;
}

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_crypt(desc, &walk, true);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_crypt(desc, &walk, false);
}

static unsigned int __cbc_encrypt(struct blkcipher_desc *desc,
				  struct blkcipher_walk *walk)
{
	struct bf_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
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

static int cbc_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		nbytes = __cbc_encrypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static unsigned int __cbc_decrypt(struct blkcipher_desc *desc,
				  struct blkcipher_walk *walk)
{
	struct bf_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = BF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 last_iv;
	int i;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	/* Process multi-block AVX2 batch */
	if (nbytes >= bsize * BF_AVX2_PARALLEL_BLOCKS) {
		do {
			nbytes -= bsize * (BF_AVX2_PARALLEL_BLOCKS - 1);
			src -= BF_AVX2_PARALLEL_BLOCKS - 1;
			dst -= BF_AVX2_PARALLEL_BLOCKS - 1;

			blowfish_cbc_dec_32way(ctx, (u8 *)dst, (u8 *)src);

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			*dst ^= *(src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * BF_AVX2_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Process multi-block batch */
	if (nbytes >= bsize * BF_PARALLEL_BLOCKS) {
		u64 ivs[BF_PARALLEL_BLOCKS - 1];

		do {
			nbytes -= bsize * (BF_PARALLEL_BLOCKS - 1);
			src -= BF_PARALLEL_BLOCKS - 1;
			dst -= BF_PARALLEL_BLOCKS - 1;

			for (i = 0; i < BF_PARALLEL_BLOCKS - 1; i++)
				ivs[i] = src[i];

			blowfish_dec_blk_4way(ctx, (u8 *)dst, (u8 *)src);

			for (i = 0; i < BF_PARALLEL_BLOCKS - 1; i++)
				dst[i + 1] ^= ivs[i];

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			*dst ^= *(src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * BF_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
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

static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk.nbytes)) {
		fpu_enabled = bf_fpu_begin(fpu_enabled, nbytes);
		nbytes = __cbc_decrypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	bf_fpu_end(fpu_enabled);
	return err;
}

static void ctr_crypt_final(struct blkcipher_desc *desc,
			    struct blkcipher_walk *walk)
{
	struct bf_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 *ctrblk = walk->iv;
	u8 keystream[BF_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	blowfish_enc_blk(ctx, keystream, ctrblk);
	crypto_xor(keystream, src, nbytes);
	memcpy(dst, keystream, nbytes);

	crypto_inc(ctrblk, BF_BLOCK_SIZE);
}

static unsigned int __ctr_crypt(struct blkcipher_desc *desc,
				struct blkcipher_walk *walk)
{
	struct bf_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int bsize = BF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	int i;

	/* Process multi-block AVX2 batch */
	if (nbytes >= bsize * BF_AVX2_PARALLEL_BLOCKS) {
		do {
			blowfish_ctr_32way(ctx, (u8 *)dst, (u8 *)src,
					   (__be64 *)walk->iv);

			src += BF_AVX2_PARALLEL_BLOCKS;
			dst += BF_AVX2_PARALLEL_BLOCKS;
			nbytes -= bsize * BF_AVX2_PARALLEL_BLOCKS;
		} while (nbytes >= bsize * BF_AVX2_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Process four block batch */
	if (nbytes >= bsize * BF_PARALLEL_BLOCKS) {
		__be64 ctrblocks[BF_PARALLEL_BLOCKS];
		u64 ctrblk = be64_to_cpu(*(__be64 *)walk->iv);

		do {
			/* create ctrblks for parallel encrypt */
			for (i = 0; i < BF_PARALLEL_BLOCKS; i++) {
				if (dst != src)
					dst[i] = src[i];

				ctrblocks[i] = cpu_to_be64(ctrblk++);
			}

			blowfish_enc_blk_xor_4way(ctx, (u8 *)dst,
						  (u8 *)ctrblocks);

			src += BF_PARALLEL_BLOCKS;
			dst += BF_PARALLEL_BLOCKS;
			nbytes -= bsize * BF_PARALLEL_BLOCKS;
		} while (nbytes >= bsize * BF_PARALLEL_BLOCKS);

		*(__be64 *)walk->iv = cpu_to_be64(ctrblk);

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

		blowfish_enc_blk_xor(ctx, (u8 *)dst, (u8 *)&ctrblk);

		src += 1;
		dst += 1;
	} while ((nbytes -= bsize) >= bsize);

done:
	return nbytes;
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, BF_BLOCK_SIZE);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk.nbytes) >= BF_BLOCK_SIZE) {
		fpu_enabled = bf_fpu_begin(fpu_enabled, nbytes);
		nbytes = __ctr_crypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	bf_fpu_end(fpu_enabled);

	if (walk.nbytes) {
		ctr_crypt_final(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg bf_algs[6] = { {
	.cra_name		= "__ecb-blowfish-avx2",
	.cra_driver_name	= "__driver-ecb-blowfish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.setkey		= blowfish_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-blowfish-avx2",
	.cra_driver_name	= "__driver-cbc-blowfish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.setkey		= blowfish_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-blowfish-avx2",
	.cra_driver_name	= "__driver-ctr-blowfish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.ivsize		= BF_BLOCK_SIZE,
			.setkey		= blowfish_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "ecb(blowfish)",
	.cra_driver_name	= "ecb-blowfish-avx2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(blowfish)",
	.cra_driver_name	= "cbc-blowfish-avx2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.ivsize		= BF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(blowfish)",
	.cra_driver_name	= "ctr-blowfish-avx2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.ivsize		= BF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
} };


static int __init init(void)
{
	u64 xcr0;

	if (!cpu_has_avx2 || !cpu_has_osxsave) {
		pr_info("AVX2 instructions are not detected.\n");
		return -ENODEV;
	}

	xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
	if ((xcr0 & (XSTATE_SSE | XSTATE_YMM)) != (XSTATE_SSE | XSTATE_YMM)) {
		pr_info("AVX detected but unusable.\n");
		return -ENODEV;
	}

	return crypto_register_algs(bf_algs, ARRAY_SIZE(bf_algs));
}

static void __exit fini(void)
{
	crypto_unregister_algs(bf_algs, ARRAY_SIZE(bf_algs));
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blowfish Cipher Algorithm, AVX2 optimized");
MODULE_ALIAS("blowfish");
MODULE_ALIAS("blowfish-asm");
