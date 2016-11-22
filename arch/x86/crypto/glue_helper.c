/*
 * Shared glue code for 128bit block ciphers
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
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

#include <linux/module.h>
#include <crypto/b128ops.h>
#include <crypto/internal/skcipher.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <asm/crypto/glue_helper.h>

static int __glue_ecb_crypt_128bit(const struct common_glue_ctx *gctx,
				   struct blkcipher_desc *desc,
				   struct blkcipher_walk *walk)
{
	void *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = 128 / 8;
	unsigned int nbytes, i, func_bytes;
	bool fpu_enabled = false;
	int err;

	err = blkcipher_walk_virt(desc, walk);

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
					     desc, fpu_enabled, nbytes);

		for (i = 0; i < gctx->num_funcs; i++) {
			func_bytes = bsize * gctx->funcs[i].num_blocks;

			/* Process multi-block batch */
			if (nbytes >= func_bytes) {
				do {
					gctx->funcs[i].fn_u.ecb(ctx, wdst,
								wsrc);

					wsrc += func_bytes;
					wdst += func_bytes;
					nbytes -= func_bytes;
				} while (nbytes >= func_bytes);

				if (nbytes < bsize)
					goto done;
			}
		}

done:
		err = blkcipher_walk_done(desc, walk, nbytes);
	}

	glue_fpu_end(fpu_enabled);
	return err;
}

int glue_ecb_crypt_128bit(const struct common_glue_ctx *gctx,
			  struct blkcipher_desc *desc, struct scatterlist *dst,
			  struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return __glue_ecb_crypt_128bit(gctx, desc, &walk);
}
EXPORT_SYMBOL_GPL(glue_ecb_crypt_128bit);

static unsigned int __glue_cbc_encrypt_128bit(const common_glue_func_t fn,
					      struct blkcipher_desc *desc,
					      struct blkcipher_walk *walk)
{
	void *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = 128 / 8;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 *iv = (u128 *)walk->iv;

	do {
		u128_xor(dst, src, iv);
		fn(ctx, (u8 *)dst, (u8 *)dst);
		iv = dst;

		src += 1;
		dst += 1;
		nbytes -= bsize;
	} while (nbytes >= bsize);

	*(u128 *)walk->iv = *iv;
	return nbytes;
}

int glue_cbc_encrypt_128bit(const common_glue_func_t fn,
			    struct blkcipher_desc *desc,
			    struct scatterlist *dst,
			    struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		nbytes = __glue_cbc_encrypt_128bit(fn, desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}
EXPORT_SYMBOL_GPL(glue_cbc_encrypt_128bit);

static unsigned int
__glue_cbc_decrypt_128bit(const struct common_glue_ctx *gctx,
			  struct blkcipher_desc *desc,
			  struct blkcipher_walk *walk)
{
	void *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = 128 / 8;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 last_iv;
	unsigned int num_blocks, func_bytes;
	unsigned int i;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	for (i = 0; i < gctx->num_funcs; i++) {
		num_blocks = gctx->funcs[i].num_blocks;
		func_bytes = bsize * num_blocks;

		/* Process multi-block batch */
		if (nbytes >= func_bytes) {
			do {
				nbytes -= func_bytes - bsize;
				src -= num_blocks - 1;
				dst -= num_blocks - 1;

				gctx->funcs[i].fn_u.cbc(ctx, dst, src);

				nbytes -= bsize;
				if (nbytes < bsize)
					goto done;

				u128_xor(dst, dst, src - 1);
				src -= 1;
				dst -= 1;
			} while (nbytes >= func_bytes);

			if (nbytes < bsize)
				goto done;
		}
	}

done:
	u128_xor(dst, dst, (u128 *)walk->iv);
	*(u128 *)walk->iv = last_iv;

	return nbytes;
}

int glue_cbc_decrypt_128bit(const struct common_glue_ctx *gctx,
			    struct blkcipher_desc *desc,
			    struct scatterlist *dst,
			    struct scatterlist *src, unsigned int nbytes)
{
	const unsigned int bsize = 128 / 8;
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while ((nbytes = walk.nbytes)) {
		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
					     desc, fpu_enabled, nbytes);
		nbytes = __glue_cbc_decrypt_128bit(gctx, desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	glue_fpu_end(fpu_enabled);
	return err;
}
EXPORT_SYMBOL_GPL(glue_cbc_decrypt_128bit);

static void glue_ctr_crypt_final_128bit(const common_glue_ctr_func_t fn_ctr,
					struct blkcipher_desc *desc,
					struct blkcipher_walk *walk)
{
	void *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 *src = (u8 *)walk->src.virt.addr;
	u8 *dst = (u8 *)walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;
	le128 ctrblk;
	u128 tmp;

	be128_to_le128(&ctrblk, (be128 *)walk->iv);

	memcpy(&tmp, src, nbytes);
	fn_ctr(ctx, &tmp, &tmp, &ctrblk);
	memcpy(dst, &tmp, nbytes);

	le128_to_be128((be128 *)walk->iv, &ctrblk);
}

static unsigned int __glue_ctr_crypt_128bit(const struct common_glue_ctx *gctx,
					    struct blkcipher_desc *desc,
					    struct blkcipher_walk *walk)
{
	const unsigned int bsize = 128 / 8;
	void *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	le128 ctrblk;
	unsigned int num_blocks, func_bytes;
	unsigned int i;

	be128_to_le128(&ctrblk, (be128 *)walk->iv);

	/* Process multi-block batch */
	for (i = 0; i < gctx->num_funcs; i++) {
		num_blocks = gctx->funcs[i].num_blocks;
		func_bytes = bsize * num_blocks;

		if (nbytes >= func_bytes) {
			do {
				gctx->funcs[i].fn_u.ctr(ctx, dst, src, &ctrblk);

				src += num_blocks;
				dst += num_blocks;
				nbytes -= func_bytes;
			} while (nbytes >= func_bytes);

			if (nbytes < bsize)
				goto done;
		}
	}

done:
	le128_to_be128((be128 *)walk->iv, &ctrblk);
	return nbytes;
}

int glue_ctr_crypt_128bit(const struct common_glue_ctx *gctx,
			  struct blkcipher_desc *desc, struct scatterlist *dst,
			  struct scatterlist *src, unsigned int nbytes)
{
	const unsigned int bsize = 128 / 8;
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, bsize);

	while ((nbytes = walk.nbytes) >= bsize) {
		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
					     desc, fpu_enabled, nbytes);
		nbytes = __glue_ctr_crypt_128bit(gctx, desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	glue_fpu_end(fpu_enabled);

	if (walk.nbytes) {
		glue_ctr_crypt_final_128bit(
			gctx->funcs[gctx->num_funcs - 1].fn_u.ctr, desc, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}
EXPORT_SYMBOL_GPL(glue_ctr_crypt_128bit);

static unsigned int __glue_xts_crypt_128bit(const struct common_glue_ctx *gctx,
					    void *ctx,
					    struct blkcipher_desc *desc,
					    struct blkcipher_walk *walk)
{
	const unsigned int bsize = 128 / 8;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	unsigned int num_blocks, func_bytes;
	unsigned int i;

	/* Process multi-block batch */
	for (i = 0; i < gctx->num_funcs; i++) {
		num_blocks = gctx->funcs[i].num_blocks;
		func_bytes = bsize * num_blocks;

		if (nbytes >= func_bytes) {
			do {
				gctx->funcs[i].fn_u.xts(ctx, dst, src,
							(le128 *)walk->iv);

				src += num_blocks;
				dst += num_blocks;
				nbytes -= func_bytes;
			} while (nbytes >= func_bytes);

			if (nbytes < bsize)
				goto done;
		}
	}

done:
	return nbytes;
}

static unsigned int __glue_xts_req_128bit(const struct common_glue_ctx *gctx,
					  void *ctx,
					  struct skcipher_walk *walk)
{
	const unsigned int bsize = 128 / 8;
	unsigned int nbytes = walk->nbytes;
	u128 *src = walk->src.virt.addr;
	u128 *dst = walk->dst.virt.addr;
	unsigned int num_blocks, func_bytes;
	unsigned int i;

	/* Process multi-block batch */
	for (i = 0; i < gctx->num_funcs; i++) {
		num_blocks = gctx->funcs[i].num_blocks;
		func_bytes = bsize * num_blocks;

		if (nbytes >= func_bytes) {
			do {
				gctx->funcs[i].fn_u.xts(ctx, dst, src,
							walk->iv);

				src += num_blocks;
				dst += num_blocks;
				nbytes -= func_bytes;
			} while (nbytes >= func_bytes);

			if (nbytes < bsize)
				goto done;
		}
	}

done:
	return nbytes;
}

/* for implementations implementing faster XTS IV generator */
int glue_xts_crypt_128bit(const struct common_glue_ctx *gctx,
			  struct blkcipher_desc *desc, struct scatterlist *dst,
			  struct scatterlist *src, unsigned int nbytes,
			  void (*tweak_fn)(void *ctx, u8 *dst, const u8 *src),
			  void *tweak_ctx, void *crypt_ctx)
{
	const unsigned int bsize = 128 / 8;
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);

	err = blkcipher_walk_virt(desc, &walk);
	nbytes = walk.nbytes;
	if (!nbytes)
		return err;

	/* set minimum length to bsize, for tweak_fn */
	fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
				     desc, fpu_enabled,
				     nbytes < bsize ? bsize : nbytes);

	/* calculate first value of T */
	tweak_fn(tweak_ctx, walk.iv, walk.iv);

	while (nbytes) {
		nbytes = __glue_xts_crypt_128bit(gctx, crypt_ctx, desc, &walk);

		err = blkcipher_walk_done(desc, &walk, nbytes);
		nbytes = walk.nbytes;
	}

	glue_fpu_end(fpu_enabled);

	return err;
}
EXPORT_SYMBOL_GPL(glue_xts_crypt_128bit);

int glue_xts_req_128bit(const struct common_glue_ctx *gctx,
			struct skcipher_request *req,
			common_glue_func_t tweak_fn, void *tweak_ctx,
			void *crypt_ctx)
{
	const unsigned int bsize = 128 / 8;
	struct skcipher_walk walk;
	bool fpu_enabled = false;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);
	nbytes = walk.nbytes;
	if (!nbytes)
		return err;

	/* set minimum length to bsize, for tweak_fn */
	fpu_enabled = glue_skwalk_fpu_begin(bsize, gctx->fpu_blocks_limit,
					    &walk, fpu_enabled,
					    nbytes < bsize ? bsize : nbytes);

	/* calculate first value of T */
	tweak_fn(tweak_ctx, walk.iv, walk.iv);

	while (nbytes) {
		nbytes = __glue_xts_req_128bit(gctx, crypt_ctx, &walk);

		err = skcipher_walk_done(&walk, nbytes);
		nbytes = walk.nbytes;
	}

	glue_fpu_end(fpu_enabled);

	return err;
}
EXPORT_SYMBOL_GPL(glue_xts_req_128bit);

void glue_xts_crypt_128bit_one(void *ctx, u128 *dst, const u128 *src, le128 *iv,
			       common_glue_func_t fn)
{
	le128 ivblk = *iv;

	/* generate next IV */
	le128_gf128mul_x_ble(iv, &ivblk);

	/* CC <- T xor C */
	u128_xor(dst, src, (u128 *)&ivblk);

	/* PP <- D(Key2,CC) */
	fn(ctx, (u8 *)dst, (u8 *)dst);

	/* P <- T xor PP */
	u128_xor(dst, dst, (u128 *)&ivblk);
}
EXPORT_SYMBOL_GPL(glue_xts_crypt_128bit_one);

MODULE_LICENSE("GPL");
