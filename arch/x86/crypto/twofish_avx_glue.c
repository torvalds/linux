/*
 * Glue Code for AVX assembler version of Twofish Cipher
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Glue code based on serpent_sse2_glue.c by:
 *  Copyright (C) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
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
#include <crypto/algapi.h>
#include <crypto/twofish.h>
#include <crypto/cryptd.h>
#include <crypto/b128ops.h>
#include <crypto/ctr.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <asm/i387.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <crypto/scatterwalk.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>


#define TWOFISH_PARALLEL_BLOCKS 8

/* regular block cipher functions from twofish_x86_64 module */
asmlinkage void twofish_enc_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);
asmlinkage void twofish_dec_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);

/* 3-way parallel cipher functions from twofish_x86_64-3way module */
asmlinkage void __twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
				       const u8 *src, bool xor);
asmlinkage void twofish_dec_blk_3way(struct twofish_ctx *ctx, u8 *dst,
				     const u8 *src);

static inline void twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, false);
}

static inline void twofish_enc_blk_3way_xor(struct twofish_ctx *ctx, u8 *dst,
					    const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, true);
}

/* 8-way parallel cipher functions */
asmlinkage void __twofish_enc_blk_8way(struct twofish_ctx *ctx, u8 *dst,
				       const u8 *src, bool xor);
asmlinkage void twofish_dec_blk_8way(struct twofish_ctx *ctx, u8 *dst,
				     const u8 *src);

static inline void twofish_enc_blk_xway(struct twofish_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__twofish_enc_blk_8way(ctx, dst, src, false);
}

static inline void twofish_enc_blk_xway_xor(struct twofish_ctx *ctx, u8 *dst,
					    const u8 *src)
{
	__twofish_enc_blk_8way(ctx, dst, src, true);
}

static inline void twofish_dec_blk_xway(struct twofish_ctx *ctx, u8 *dst,
					const u8 *src)
{
	twofish_dec_blk_8way(ctx, dst, src);
}



struct async_twofish_ctx {
	struct cryptd_ablkcipher *cryptd_tfm;
};

static inline bool twofish_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	if (fpu_enabled)
		return true;

	/* AVX is only used when chunk to be processed is large enough, so
	 * do not enable FPU until it is necessary.
	 */
	if (nbytes < TF_BLOCK_SIZE * TWOFISH_PARALLEL_BLOCKS)
		return false;

	kernel_fpu_begin();
	return true;
}

static inline void twofish_fpu_end(bool fpu_enabled)
{
	if (fpu_enabled)
		kernel_fpu_end();
}

static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
		     bool enc)
{
	bool fpu_enabled = false;
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes;
	int err;

	err = blkcipher_walk_virt(desc, walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

		fpu_enabled = twofish_fpu_begin(fpu_enabled, nbytes);

		/* Process multi-block batch */
		if (nbytes >= bsize * TWOFISH_PARALLEL_BLOCKS) {
			do {
				if (enc)
					twofish_enc_blk_xway(ctx, wdst, wsrc);
				else
					twofish_dec_blk_xway(ctx, wdst, wsrc);

				wsrc += bsize * TWOFISH_PARALLEL_BLOCKS;
				wdst += bsize * TWOFISH_PARALLEL_BLOCKS;
				nbytes -= bsize * TWOFISH_PARALLEL_BLOCKS;
			} while (nbytes >= bsize * TWOFISH_PARALLEL_BLOCKS);

			if (nbytes < bsize)
				goto done;
		}

		/* Process three block batch */
		if (nbytes >= bsize * 3) {
			do {
				if (enc)
					twofish_enc_blk_3way(ctx, wdst, wsrc);
				else
					twofish_dec_blk_3way(ctx, wdst, wsrc);

				wsrc += bsize * 3;
				wdst += bsize * 3;
				nbytes -= bsize * 3;
			} while (nbytes >= bsize * 3);

			if (nbytes < bsize)
				goto done;
		}

		/* Handle leftovers */
		do {
			if (enc)
				twofish_enc_blk(ctx, wdst, wsrc);
			else
				twofish_dec_blk(ctx, wdst, wsrc);

			wsrc += bsize;
			wdst += bsize;
			nbytes -= bsize;
		} while (nbytes >= bsize);

done:
		err = blkcipher_walk_done(desc, walk, nbytes);
	}

	twofish_fpu_end(fpu_enabled);
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
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 *iv = (u128 *)walk->iv;

	do {
		u128_xor(dst, src, iv);
		twofish_enc_blk(ctx, (u8 *)dst, (u8 *)dst);
		iv = dst;

		src += 1;
		dst += 1;
		nbytes -= bsize;
	} while (nbytes >= bsize);

	u128_xor((u128 *)walk->iv, (u128 *)walk->iv, iv);
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
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 ivs[TWOFISH_PARALLEL_BLOCKS - 1];
	u128 last_iv;
	int i;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	/* Process multi-block batch */
	if (nbytes >= bsize * TWOFISH_PARALLEL_BLOCKS) {
		do {
			nbytes -= bsize * (TWOFISH_PARALLEL_BLOCKS - 1);
			src -= TWOFISH_PARALLEL_BLOCKS - 1;
			dst -= TWOFISH_PARALLEL_BLOCKS - 1;

			for (i = 0; i < TWOFISH_PARALLEL_BLOCKS - 1; i++)
				ivs[i] = src[i];

			twofish_dec_blk_xway(ctx, (u8 *)dst, (u8 *)src);

			for (i = 0; i < TWOFISH_PARALLEL_BLOCKS - 1; i++)
				u128_xor(dst + (i + 1), dst + (i + 1), ivs + i);

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			u128_xor(dst, dst, src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * TWOFISH_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Process three block batch */
	if (nbytes >= bsize * 3) {
		do {
			nbytes -= bsize * (3 - 1);
			src -= 3 - 1;
			dst -= 3 - 1;

			ivs[0] = src[0];
			ivs[1] = src[1];

			twofish_dec_blk_3way(ctx, (u8 *)dst, (u8 *)src);

			u128_xor(dst + 1, dst + 1, ivs + 0);
			u128_xor(dst + 2, dst + 2, ivs + 1);

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			u128_xor(dst, dst, src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * 3);

		if (nbytes < bsize)
			goto done;
	}

	/* Handle leftovers */
	for (;;) {
		twofish_dec_blk(ctx, (u8 *)dst, (u8 *)src);

		nbytes -= bsize;
		if (nbytes < bsize)
			break;

		u128_xor(dst, dst, src - 1);
		src -= 1;
		dst -= 1;
	}

done:
	u128_xor(dst, dst, (u128 *)walk->iv);
	*(u128 *)walk->iv = last_iv;

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
		fpu_enabled = twofish_fpu_begin(fpu_enabled, nbytes);
		nbytes = __cbc_decrypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	twofish_fpu_end(fpu_enabled);
	return err;
}

static inline void u128_to_be128(be128 *dst, const u128 *src)
{
	dst->a = cpu_to_be64(src->a);
	dst->b = cpu_to_be64(src->b);
}

static inline void be128_to_u128(u128 *dst, const be128 *src)
{
	dst->a = be64_to_cpu(src->a);
	dst->b = be64_to_cpu(src->b);
}

static inline void u128_inc(u128 *i)
{
	i->b++;
	if (!i->b)
		i->a++;
}

static void ctr_crypt_final(struct blkcipher_desc *desc,
			    struct blkcipher_walk *walk)
{
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 *ctrblk = walk->iv;
	u8 keystream[TF_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	twofish_enc_blk(ctx, keystream, ctrblk);
	crypto_xor(keystream, src, nbytes);
	memcpy(dst, keystream, nbytes);

	crypto_inc(ctrblk, TF_BLOCK_SIZE);
}

static unsigned int __ctr_crypt(struct blkcipher_desc *desc,
				struct blkcipher_walk *walk)
{
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 ctrblk;
	be128 ctrblocks[TWOFISH_PARALLEL_BLOCKS];
	int i;

	be128_to_u128(&ctrblk, (be128 *)walk->iv);

	/* Process multi-block batch */
	if (nbytes >= bsize * TWOFISH_PARALLEL_BLOCKS) {
		do {
			/* create ctrblks for parallel encrypt */
			for (i = 0; i < TWOFISH_PARALLEL_BLOCKS; i++) {
				if (dst != src)
					dst[i] = src[i];

				u128_to_be128(&ctrblocks[i], &ctrblk);
				u128_inc(&ctrblk);
			}

			twofish_enc_blk_xway_xor(ctx, (u8 *)dst,
						 (u8 *)ctrblocks);

			src += TWOFISH_PARALLEL_BLOCKS;
			dst += TWOFISH_PARALLEL_BLOCKS;
			nbytes -= bsize * TWOFISH_PARALLEL_BLOCKS;
		} while (nbytes >= bsize * TWOFISH_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Process three block batch */
	if (nbytes >= bsize * 3) {
		do {
			if (dst != src) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
			}

			/* create ctrblks for parallel encrypt */
			u128_to_be128(&ctrblocks[0], &ctrblk);
			u128_inc(&ctrblk);
			u128_to_be128(&ctrblocks[1], &ctrblk);
			u128_inc(&ctrblk);
			u128_to_be128(&ctrblocks[2], &ctrblk);
			u128_inc(&ctrblk);

			twofish_enc_blk_3way_xor(ctx, (u8 *)dst,
						 (u8 *)ctrblocks);

			src += 3;
			dst += 3;
			nbytes -= bsize * 3;
		} while (nbytes >= bsize * 3);

		if (nbytes < bsize)
			goto done;
	}

	/* Handle leftovers */
	do {
		if (dst != src)
			*dst = *src;

		u128_to_be128(&ctrblocks[0], &ctrblk);
		u128_inc(&ctrblk);

		twofish_enc_blk(ctx, (u8 *)ctrblocks, (u8 *)ctrblocks);
		u128_xor(dst, dst, (u128 *)ctrblocks);

		src += 1;
		dst += 1;
		nbytes -= bsize;
	} while (nbytes >= bsize);

done:
	u128_to_be128((be128 *)walk->iv, &ctrblk);
	return nbytes;
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	bool fpu_enabled = false;
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, TF_BLOCK_SIZE);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk.nbytes) >= TF_BLOCK_SIZE) {
		fpu_enabled = twofish_fpu_begin(fpu_enabled, nbytes);
		nbytes = __ctr_crypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	twofish_fpu_end(fpu_enabled);

	if (walk.nbytes) {
		ctr_crypt_final(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

struct crypt_priv {
	struct twofish_ctx *ctx;
	bool fpu_enabled;
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = TF_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = twofish_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes == bsize * TWOFISH_PARALLEL_BLOCKS) {
		twofish_enc_blk_xway(ctx->ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / (bsize * 3); i++, srcdst += bsize * 3)
		twofish_enc_blk_3way(ctx->ctx, srcdst, srcdst);

	nbytes %= bsize * 3;

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		twofish_enc_blk(ctx->ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = TF_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = twofish_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes == bsize * TWOFISH_PARALLEL_BLOCKS) {
		twofish_dec_blk_xway(ctx->ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / (bsize * 3); i++, srcdst += bsize * 3)
		twofish_dec_blk_3way(ctx->ctx, srcdst, srcdst);

	nbytes %= bsize * 3;

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		twofish_dec_blk(ctx->ctx, srcdst, srcdst);
}

struct twofish_lrw_ctx {
	struct lrw_table_ctx lrw_table;
	struct twofish_ctx twofish_ctx;
};

static int lrw_twofish_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct twofish_lrw_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	err = __twofish_setkey(&ctx->twofish_ctx, key,
				keylen - TF_BLOCK_SIZE, &tfm->crt_flags);
	if (err)
		return err;

	return lrw_init_table(&ctx->lrw_table, key + keylen -
						TF_BLOCK_SIZE);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[TWOFISH_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->twofish_ctx,
		.fpu_enabled = false,
	};
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = encrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = lrw_crypt(desc, dst, src, nbytes, &req);
	twofish_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[TWOFISH_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->twofish_ctx,
		.fpu_enabled = false,
	};
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = decrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = lrw_crypt(desc, dst, src, nbytes, &req);
	twofish_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static void lrw_exit_tfm(struct crypto_tfm *tfm)
{
	struct twofish_lrw_ctx *ctx = crypto_tfm_ctx(tfm);

	lrw_free_table(&ctx->lrw_table);
}

struct twofish_xts_ctx {
	struct twofish_ctx tweak_ctx;
	struct twofish_ctx crypt_ctx;
};

static int xts_twofish_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct twofish_xts_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;
	int err;

	/* key consists of keys of equal size concatenated, therefore
	 * the length must be even
	 */
	if (keylen % 2) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	/* first half of xts-key is for crypt */
	err = __twofish_setkey(&ctx->crypt_ctx, key, keylen / 2, flags);
	if (err)
		return err;

	/* second half of xts-key is for tweak */
	return __twofish_setkey(&ctx->tweak_ctx,
				key + keylen / 2, keylen / 2, flags);
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[TWOFISH_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->crypt_ctx,
		.fpu_enabled = false,
	};
	struct xts_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.tweak_ctx = &ctx->tweak_ctx,
		.tweak_fn = XTS_TWEAK_CAST(twofish_enc_blk),
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = encrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = xts_crypt(desc, dst, src, nbytes, &req);
	twofish_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[TWOFISH_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->crypt_ctx,
		.fpu_enabled = false,
	};
	struct xts_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.tweak_ctx = &ctx->tweak_ctx,
		.tweak_fn = XTS_TWEAK_CAST(twofish_enc_blk),
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = decrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = xts_crypt(desc, dst, src, nbytes, &req);
	twofish_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int ablk_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
			unsigned int key_len)
{
	struct async_twofish_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_ablkcipher *child = &ctx->cryptd_tfm->base;
	int err;

	crypto_ablkcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(child, crypto_ablkcipher_get_flags(tfm)
				    & CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(child, key, key_len);
	crypto_ablkcipher_set_flags(tfm, crypto_ablkcipher_get_flags(child)
				    & CRYPTO_TFM_RES_MASK);
	return err;
}

static int __ablk_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_twofish_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct blkcipher_desc desc;

	desc.tfm = cryptd_ablkcipher_child(ctx->cryptd_tfm);
	desc.info = req->info;
	desc.flags = 0;

	return crypto_blkcipher_crt(desc.tfm)->encrypt(
		&desc, req->dst, req->src, req->nbytes);
}

static int ablk_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_twofish_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (!irq_fpu_usable()) {
		struct ablkcipher_request *cryptd_req =
			ablkcipher_request_ctx(req);

		memcpy(cryptd_req, req, sizeof(*req));
		ablkcipher_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);

		return crypto_ablkcipher_encrypt(cryptd_req);
	} else {
		return __ablk_encrypt(req);
	}
}

static int ablk_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_twofish_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (!irq_fpu_usable()) {
		struct ablkcipher_request *cryptd_req =
			ablkcipher_request_ctx(req);

		memcpy(cryptd_req, req, sizeof(*req));
		ablkcipher_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);

		return crypto_ablkcipher_decrypt(cryptd_req);
	} else {
		struct blkcipher_desc desc;

		desc.tfm = cryptd_ablkcipher_child(ctx->cryptd_tfm);
		desc.info = req->info;
		desc.flags = 0;

		return crypto_blkcipher_crt(desc.tfm)->decrypt(
			&desc, req->dst, req->src, req->nbytes);
	}
}

static void ablk_exit(struct crypto_tfm *tfm)
{
	struct async_twofish_ctx *ctx = crypto_tfm_ctx(tfm);

	cryptd_free_ablkcipher(ctx->cryptd_tfm);
}

static int ablk_init(struct crypto_tfm *tfm)
{
	struct async_twofish_ctx *ctx = crypto_tfm_ctx(tfm);
	struct cryptd_ablkcipher *cryptd_tfm;
	char drv_name[CRYPTO_MAX_ALG_NAME];

	snprintf(drv_name, sizeof(drv_name), "__driver-%s",
					crypto_tfm_alg_driver_name(tfm));

	cryptd_tfm = cryptd_alloc_ablkcipher(drv_name, 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	ctx->cryptd_tfm = cryptd_tfm;
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request) +
		crypto_ablkcipher_reqsize(&cryptd_tfm->base);

	return 0;
}

static struct crypto_alg twofish_algs[10] = { {
	.cra_name		= "__ecb-twofish-avx",
	.cra_driver_name	= "__driver-ecb-twofish-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[0].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-twofish-avx",
	.cra_driver_name	= "__driver-cbc-twofish-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[1].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-twofish-avx",
	.cra_driver_name	= "__driver-ctr-twofish-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[2].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "__lrw-twofish-avx",
	.cra_driver_name	= "__driver-lrw-twofish-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[3].cra_list),
	.cra_exit		= lrw_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= lrw_twofish_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
}, {
	.cra_name		= "__xts-twofish-avx",
	.cra_driver_name	= "__driver-xts-twofish-avx",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[4].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE * 2,
			.max_keysize	= TF_MAX_KEY_SIZE * 2,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= xts_twofish_setkey,
			.encrypt	= xts_encrypt,
			.decrypt	= xts_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(twofish)",
	.cra_driver_name	= "ecb-twofish-avx",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[5].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(twofish)",
	.cra_driver_name	= "cbc-twofish-avx",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[6].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(twofish)",
	.cra_driver_name	= "ctr-twofish-avx",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[7].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
}, {
	.cra_name		= "lrw(twofish)",
	.cra_driver_name	= "lrw-twofish-avx",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[8].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "xts(twofish)",
	.cra_driver_name	= "xts-twofish-avx",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(twofish_algs[9].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE * 2,
			.max_keysize	= TF_MAX_KEY_SIZE * 2,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
} };

static int __init twofish_init(void)
{
	u64 xcr0;

	if (!cpu_has_avx || !cpu_has_osxsave) {
		printk(KERN_INFO "AVX instructions are not detected.\n");
		return -ENODEV;
	}

	xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
	if ((xcr0 & (XSTATE_SSE | XSTATE_YMM)) != (XSTATE_SSE | XSTATE_YMM)) {
		printk(KERN_INFO "AVX detected but unusable.\n");
		return -ENODEV;
	}

	return crypto_register_algs(twofish_algs, ARRAY_SIZE(twofish_algs));
}

static void __exit twofish_exit(void)
{
	crypto_unregister_algs(twofish_algs, ARRAY_SIZE(twofish_algs));
}

module_init(twofish_init);
module_exit(twofish_exit);

MODULE_DESCRIPTION("Twofish Cipher Algorithm, AVX optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("twofish");
