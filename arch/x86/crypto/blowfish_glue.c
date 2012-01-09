/*
 * Glue Code for assembler optimized version of Blowfish
 *
 * Copyright (c) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

#include <crypto/blowfish.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <crypto/algapi.h>

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

static struct crypto_alg bf_alg = {
	.cra_name		=	"blowfish",
	.cra_driver_name	=	"blowfish-asm",
	.cra_priority		=	200,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	BF_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct bf_ctx),
	.cra_alignmask		=	3,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(bf_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	BF_MIN_KEY_SIZE,
			.cia_max_keysize	=	BF_MAX_KEY_SIZE,
			.cia_setkey		=	blowfish_setkey,
			.cia_encrypt		=	blowfish_encrypt,
			.cia_decrypt		=	blowfish_decrypt,
		}
	}
};

static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
		     void (*fn)(struct bf_ctx *, u8 *, const u8 *),
		     void (*fn_4way)(struct bf_ctx *, u8 *, const u8 *))
{
	struct bf_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int bsize = BF_BLOCK_SIZE;
	unsigned int nbytes;
	int err;

	err = blkcipher_walk_virt(desc, walk);

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

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
		err = blkcipher_walk_done(desc, walk, nbytes);
	}

	return err;
}

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_crypt(desc, &walk, blowfish_enc_blk, blowfish_enc_blk_4way);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_crypt(desc, &walk, blowfish_dec_blk, blowfish_dec_blk_4way);
}

static struct crypto_alg blk_ecb_alg = {
	.cra_name		= "ecb(blowfish)",
	.cra_driver_name	= "ecb-blowfish-asm",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ecb_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.setkey		= blowfish_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
};

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
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		nbytes = __cbc_decrypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static struct crypto_alg blk_cbc_alg = {
	.cra_name		= "cbc(blowfish)",
	.cra_driver_name	= "cbc-blowfish-asm",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= BF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_cbc_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= BF_MIN_KEY_SIZE,
			.max_keysize	= BF_MAX_KEY_SIZE,
			.ivsize		= BF_BLOCK_SIZE,
			.setkey		= blowfish_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
};

static void ctr_crypt_final(struct bf_ctx *ctx, struct blkcipher_walk *walk)
{
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
	u64 ctrblk = be64_to_cpu(*(__be64 *)walk->iv);
	__be64 ctrblocks[4];

	/* Process four block batch */
	if (nbytes >= bsize * 4) {
		do {
			if (dst != src) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = src[3];
			}

			/* create ctrblks for parallel encrypt */
			ctrblocks[0] = cpu_to_be64(ctrblk++);
			ctrblocks[1] = cpu_to_be64(ctrblk++);
			ctrblocks[2] = cpu_to_be64(ctrblk++);
			ctrblocks[3] = cpu_to_be64(ctrblk++);

			blowfish_enc_blk_xor_4way(ctx, (u8 *)dst,
						  (u8 *)ctrblocks);

			src += 4;
			dst += 4;
		} while ((nbytes -= bsize * 4) >= bsize * 4);

		if (nbytes < bsize)
			goto done;
	}

	/* Handle leftovers */
	do {
		if (dst != src)
			*dst = *src;

		ctrblocks[0] = cpu_to_be64(ctrblk++);

		blowfish_enc_blk_xor(ctx, (u8 *)dst, (u8 *)ctrblocks);

		src += 1;
		dst += 1;
	} while ((nbytes -= bsize) >= bsize);

done:
	*(__be64 *)walk->iv = cpu_to_be64(ctrblk);
	return nbytes;
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, BF_BLOCK_SIZE);

	while ((nbytes = walk.nbytes) >= BF_BLOCK_SIZE) {
		nbytes = __ctr_crypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	if (walk.nbytes) {
		ctr_crypt_final(crypto_blkcipher_ctx(desc->tfm), &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg blk_ctr_alg = {
	.cra_name		= "ctr(blowfish)",
	.cra_driver_name	= "ctr-blowfish-asm",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct bf_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ctr_alg.cra_list),
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
};

static int __init init(void)
{
	int err;

	err = crypto_register_alg(&bf_alg);
	if (err)
		goto bf_err;
	err = crypto_register_alg(&blk_ecb_alg);
	if (err)
		goto ecb_err;
	err = crypto_register_alg(&blk_cbc_alg);
	if (err)
		goto cbc_err;
	err = crypto_register_alg(&blk_ctr_alg);
	if (err)
		goto ctr_err;

	return 0;

ctr_err:
	crypto_unregister_alg(&blk_cbc_alg);
cbc_err:
	crypto_unregister_alg(&blk_ecb_alg);
ecb_err:
	crypto_unregister_alg(&bf_alg);
bf_err:
	return err;
}

static void __exit fini(void)
{
	crypto_unregister_alg(&blk_ctr_alg);
	crypto_unregister_alg(&blk_cbc_alg);
	crypto_unregister_alg(&blk_ecb_alg);
	crypto_unregister_alg(&bf_alg);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Blowfish Cipher Algorithm, asm optimized");
MODULE_ALIAS("blowfish");
MODULE_ALIAS("blowfish-asm");
