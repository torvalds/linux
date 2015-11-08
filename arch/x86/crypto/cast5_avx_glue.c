/*
 * Glue Code for the AVX assembler implemention of the Cast5 Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/ablk_helper.h>
#include <crypto/algapi.h>
#include <crypto/cast5.h>
#include <crypto/cryptd.h>
#include <crypto/ctr.h>
#include <asm/fpu/api.h>
#include <asm/crypto/glue_helper.h>

#define CAST5_PARALLEL_BLOCKS 16

asmlinkage void cast5_ecb_enc_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_ecb_dec_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_cbc_dec_16way(struct cast5_ctx *ctx, u8 *dst,
				    const u8 *src);
asmlinkage void cast5_ctr_16way(struct cast5_ctx *ctx, u8 *dst, const u8 *src,
				__be64 *iv);

static inline bool cast5_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	return glue_fpu_begin(CAST5_BLOCK_SIZE, CAST5_PARALLEL_BLOCKS,
			      NULL, fpu_enabled, nbytes);
}

static inline void cast5_fpu_end(bool fpu_enabled)
{
	return glue_fpu_end(fpu_enabled);
}

static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
		     bool enc)
{
	bool fpu_enabled = false;
	struct cast5_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = CAST5_BLOCK_SIZE;
	unsigned int nbytes;
	void (*fn)(struct cast5_ctx *ctx, u8 *dst, const u8 *src);
	int err;

	fn = (enc) ? cast5_ecb_enc_16way : cast5_ecb_dec_16way;

	err = blkcipher_walk_virt(desc, walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

		fpu_enabled = cast5_fpu_begin(fpu_enabled, nbytes);

		/* Process multi-block batch */
		if (nbytes >= bsize * CAST5_PARALLEL_BLOCKS) {
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
		err = blkcipher_walk_done(desc, walk, nbytes);
	}

	cast5_fpu_end(fpu_enabled);
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
	struct cast5_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = CAST5_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u64 *src = (u64 *)walk->src.virt.addr;
	u64 *dst = (u64 *)walk->dst.virt.addr;
	u64 *iv = (u64 *)walk->iv;

	do {
		*dst = *src ^ *iv;
		__cast5_encrypt(ctx, (u8 *)dst, (u8 *)dst);
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
	struct cast5_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
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
		fpu_enabled = cast5_fpu_begin(fpu_enabled, nbytes);
		nbytes = __cbc_decrypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	cast5_fpu_end(fpu_enabled);
	return err;
}

static void ctr_crypt_final(struct blkcipher_desc *desc,
			    struct blkcipher_walk *walk)
{
	struct cast5_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 *ctrblk = walk->iv;
	u8 keystream[CAST5_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	__cast5_encrypt(ctx, keystream, ctrblk);
	crypto_xor(keystream, src, nbytes);
	memcpy(dst, keystream, nbytes);

	crypto_inc(ctrblk, CAST5_BLOCK_SIZE);
}

static unsigned int __ctr_crypt(struct blkcipher_desc *desc,
				struct blkcipher_walk *walk)
{
	struct cast5_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
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

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, CAST5_BLOCK_SIZE);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk.nbytes) >= CAST5_BLOCK_SIZE) {
		fpu_enabled = cast5_fpu_begin(fpu_enabled, nbytes);
		nbytes = __ctr_crypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	cast5_fpu_end(fpu_enabled);

	if (walk.nbytes) {
		ctr_crypt_final(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}


static struct crypto_alg cast5_algs[6] = { {
	.cra_name		= "__ecb-cast5-avx",
	.cra_driver_name	= "__driver-ecb-cast5-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= CAST5_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast5_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST5_MIN_KEY_SIZE,
			.max_keysize	= CAST5_MAX_KEY_SIZE,
			.setkey		= cast5_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-cast5-avx",
	.cra_driver_name	= "__driver-cbc-cast5-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= CAST5_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast5_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST5_MIN_KEY_SIZE,
			.max_keysize	= CAST5_MAX_KEY_SIZE,
			.setkey		= cast5_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-cast5-avx",
	.cra_driver_name	= "__driver-ctr-cast5-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct cast5_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAST5_MIN_KEY_SIZE,
			.max_keysize	= CAST5_MAX_KEY_SIZE,
			.ivsize		= CAST5_BLOCK_SIZE,
			.setkey		= cast5_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "ecb(cast5)",
	.cra_driver_name	= "ecb-cast5-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAST5_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST5_MIN_KEY_SIZE,
			.max_keysize	= CAST5_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(cast5)",
	.cra_driver_name	= "cbc-cast5-avx",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAST5_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAST5_MIN_KEY_SIZE,
			.max_keysize	= CAST5_MAX_KEY_SIZE,
			.ivsize		= CAST5_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(cast5)",
	.cra_driver_name	= "ctr-cast5-avx",
	.cra_priority		= 200,
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
			.min_keysize	= CAST5_MIN_KEY_SIZE,
			.max_keysize	= CAST5_MAX_KEY_SIZE,
			.ivsize		= CAST5_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
} };

static int __init cast5_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				&feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return crypto_register_algs(cast5_algs, ARRAY_SIZE(cast5_algs));
}

static void __exit cast5_exit(void)
{
	crypto_unregister_algs(cast5_algs, ARRAY_SIZE(cast5_algs));
}

module_init(cast5_init);
module_exit(cast5_exit);

MODULE_DESCRIPTION("Cast5 Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("cast5");
