/*
 * Glue Code for SSE2 assembler versions of Serpent Cipher
 *
 * Copyright (c) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * Glue code based on aesni-intel_glue.c by:
 *  Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
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
#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/serpent.h>
#include <crypto/cryptd.h>
#include <crypto/b128ops.h>
#include <crypto/ctr.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <asm/i387.h>
#include <asm/serpent-sse2.h>
#include <asm/crypto/ablk_helper.h>
#include <crypto/scatterwalk.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

typedef void (*common_glue_func_t)(void *ctx, u8 *dst, const u8 *src);
typedef void (*common_glue_cbc_func_t)(void *ctx, u128 *dst, const u128 *src);
typedef void (*common_glue_ctr_func_t)(void *ctx, u128 *dst, const u128 *src,
				       u128 *iv);

#define GLUE_FUNC_CAST(fn) ((common_glue_func_t)(fn))
#define GLUE_CBC_FUNC_CAST(fn) ((common_glue_cbc_func_t)(fn))
#define GLUE_CTR_FUNC_CAST(fn) ((common_glue_ctr_func_t)(fn))

struct common_glue_func_entry {
	unsigned int num_blocks; /* number of blocks that @fn will process */
	union {
		common_glue_func_t ecb;
		common_glue_cbc_func_t cbc;
		common_glue_ctr_func_t ctr;
	} fn_u;
};

struct common_glue_ctx {
	unsigned int num_funcs;
	int fpu_blocks_limit; /* -1 means fpu not needed at all */

	/*
	 * First funcs entry must have largest num_blocks and last funcs entry
	 * must have num_blocks == 1!
	 */
	struct common_glue_func_entry funcs[];
};

static inline bool glue_fpu_begin(unsigned int bsize, int fpu_blocks_limit,
				  struct blkcipher_desc *desc,
				  bool fpu_enabled, unsigned int nbytes)
{
	if (likely(fpu_blocks_limit < 0))
		return false;

	if (fpu_enabled)
		return true;

	/*
	 * Vector-registers are only used when chunk to be processed is large
	 * enough, so do not enable FPU until it is necessary.
	 */
	if (nbytes < bsize * (unsigned int)fpu_blocks_limit)
		return false;

	if (desc) {
		/* prevent sleeping if FPU is in use */
		desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	}

	kernel_fpu_begin();
	return true;
}

static inline void glue_fpu_end(bool fpu_enabled)
{
	if (fpu_enabled)
		kernel_fpu_end();
}

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

	u128_xor((u128 *)walk->iv, (u128 *)walk->iv, iv);
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

static void glue_ctr_crypt_final_128bit(const common_glue_ctr_func_t fn_ctr,
					struct blkcipher_desc *desc,
					struct blkcipher_walk *walk)
{
	void *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 *src = (u8 *)walk->src.virt.addr;
	u8 *dst = (u8 *)walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;
	u128 ctrblk;
	u128 tmp;

	be128_to_u128(&ctrblk, (be128 *)walk->iv);

	memcpy(&tmp, src, nbytes);
	fn_ctr(ctx, &tmp, &tmp, &ctrblk);
	memcpy(dst, &tmp, nbytes);

	u128_to_be128((be128 *)walk->iv, &ctrblk);
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
	u128 ctrblk;
	unsigned int num_blocks, func_bytes;
	unsigned int i;

	be128_to_u128(&ctrblk, (be128 *)walk->iv);

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
	u128_to_be128((be128 *)walk->iv, &ctrblk);
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

static void serpent_decrypt_cbc_xway(void *ctx, u128 *dst, const u128 *src)
{
	u128 ivs[SERPENT_PARALLEL_BLOCKS - 1];
	unsigned int j;

	for (j = 0; j < SERPENT_PARALLEL_BLOCKS - 1; j++)
		ivs[j] = src[j];

	serpent_dec_blk_xway(ctx, (u8 *)dst, (u8 *)src);

	for (j = 0; j < SERPENT_PARALLEL_BLOCKS - 1; j++)
		u128_xor(dst + (j + 1), dst + (j + 1), ivs + j);
}

static void serpent_crypt_ctr(void *ctx, u128 *dst, const u128 *src, u128 *iv)
{
	be128 ctrblk;

	u128_to_be128(&ctrblk, iv);
	u128_inc(iv);

	__serpent_encrypt(ctx, (u8 *)&ctrblk, (u8 *)&ctrblk);
	u128_xor(dst, src, (u128 *)&ctrblk);
}

static void serpent_crypt_ctr_xway(void *ctx, u128 *dst, const u128 *src,
				   u128 *iv)
{
	be128 ctrblks[SERPENT_PARALLEL_BLOCKS];
	unsigned int i;

	for (i = 0; i < SERPENT_PARALLEL_BLOCKS; i++) {
		if (dst != src)
			dst[i] = src[i];

		u128_to_be128(&ctrblks[i], iv);
		u128_inc(iv);
	}

	serpent_enc_blk_xway_xor(ctx, (u8 *)dst, (u8 *)ctrblks);
}

static const struct common_glue_ctx serpent_enc = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_enc_blk_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_encrypt) }
	} }
};

static const struct common_glue_ctx serpent_ctr = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_crypt_ctr_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_crypt_ctr) }
	} }
};

static const struct common_glue_ctx serpent_dec = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_dec_blk_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_decrypt) }
	} }
};

static const struct common_glue_ctx serpent_dec_cbc = {
	.num_funcs = 2,
	.fpu_blocks_limit = SERPENT_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = SERPENT_PARALLEL_BLOCKS,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(serpent_decrypt_cbc_xway) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(__serpent_decrypt) }
	} }
};

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&serpent_enc, desc, dst, src, nbytes);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&serpent_dec, desc, dst, src, nbytes);
}

static int cbc_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_encrypt_128bit(GLUE_FUNC_CAST(__serpent_encrypt), desc,
				     dst, src, nbytes);
}

static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_decrypt_128bit(&serpent_dec_cbc, desc, dst, src,
				       nbytes);
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	return glue_ctr_crypt_128bit(&serpent_ctr, desc, dst, src, nbytes);
}

static inline bool serpent_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	return glue_fpu_begin(SERPENT_BLOCK_SIZE, SERPENT_PARALLEL_BLOCKS,
			      NULL, fpu_enabled, nbytes);
}

static inline void serpent_fpu_end(bool fpu_enabled)
{
	glue_fpu_end(fpu_enabled);
}

struct crypt_priv {
	struct serpent_ctx *ctx;
	bool fpu_enabled;
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = serpent_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes == bsize * SERPENT_PARALLEL_BLOCKS) {
		serpent_enc_blk_xway(ctx->ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		__serpent_encrypt(ctx->ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = serpent_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes == bsize * SERPENT_PARALLEL_BLOCKS) {
		serpent_dec_blk_xway(ctx->ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		__serpent_decrypt(ctx->ctx, srcdst, srcdst);
}

struct serpent_lrw_ctx {
	struct lrw_table_ctx lrw_table;
	struct serpent_ctx serpent_ctx;
};

static int lrw_serpent_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct serpent_lrw_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	err = __serpent_setkey(&ctx->serpent_ctx, key, keylen -
							SERPENT_BLOCK_SIZE);
	if (err)
		return err;

	return lrw_init_table(&ctx->lrw_table, key + keylen -
						SERPENT_BLOCK_SIZE);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[SERPENT_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->serpent_ctx,
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
	serpent_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[SERPENT_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->serpent_ctx,
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
	serpent_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static void lrw_exit_tfm(struct crypto_tfm *tfm)
{
	struct serpent_lrw_ctx *ctx = crypto_tfm_ctx(tfm);

	lrw_free_table(&ctx->lrw_table);
}

struct serpent_xts_ctx {
	struct serpent_ctx tweak_ctx;
	struct serpent_ctx crypt_ctx;
};

static int xts_serpent_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct serpent_xts_ctx *ctx = crypto_tfm_ctx(tfm);
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
	err = __serpent_setkey(&ctx->crypt_ctx, key, keylen / 2);
	if (err)
		return err;

	/* second half of xts-key is for tweak */
	return __serpent_setkey(&ctx->tweak_ctx, key + keylen / 2, keylen / 2);
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[SERPENT_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->crypt_ctx,
		.fpu_enabled = false,
	};
	struct xts_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.tweak_ctx = &ctx->tweak_ctx,
		.tweak_fn = XTS_TWEAK_CAST(__serpent_encrypt),
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = encrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = xts_crypt(desc, dst, src, nbytes, &req);
	serpent_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[SERPENT_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->crypt_ctx,
		.fpu_enabled = false,
	};
	struct xts_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.tweak_ctx = &ctx->tweak_ctx,
		.tweak_fn = XTS_TWEAK_CAST(__serpent_encrypt),
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = decrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = xts_crypt(desc, dst, src, nbytes, &req);
	serpent_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static struct crypto_alg serpent_algs[10] = { {
	.cra_name		= "__ecb-serpent-sse2",
	.cra_driver_name	= "__driver-ecb-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[0].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-serpent-sse2",
	.cra_driver_name	= "__driver-cbc-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[1].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-serpent-sse2",
	.cra_driver_name	= "__driver-ctr-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[2].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "__lrw-serpent-sse2",
	.cra_driver_name	= "__driver-lrw-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[3].cra_list),
	.cra_exit		= lrw_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= lrw_serpent_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
}, {
	.cra_name		= "__xts-serpent-sse2",
	.cra_driver_name	= "__driver-xts-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[4].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE * 2,
			.max_keysize	= SERPENT_MAX_KEY_SIZE * 2,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= xts_serpent_setkey,
			.encrypt	= xts_encrypt,
			.decrypt	= xts_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(serpent)",
	.cra_driver_name	= "ecb-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[5].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(serpent)",
	.cra_driver_name	= "cbc-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[6].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(serpent)",
	.cra_driver_name	= "ctr-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[7].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
}, {
	.cra_name		= "lrw(serpent)",
	.cra_driver_name	= "lrw-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[8].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "xts(serpent)",
	.cra_driver_name	= "xts-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(serpent_algs[9].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE * 2,
			.max_keysize	= SERPENT_MAX_KEY_SIZE * 2,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
} };

static int __init serpent_sse2_init(void)
{
	if (!cpu_has_xmm2) {
		printk(KERN_INFO "SSE2 instructions are not detected.\n");
		return -ENODEV;
	}

	return crypto_register_algs(serpent_algs, ARRAY_SIZE(serpent_algs));
}

static void __exit serpent_sse2_exit(void)
{
	crypto_unregister_algs(serpent_algs, ARRAY_SIZE(serpent_algs));
}

module_init(serpent_sse2_init);
module_exit(serpent_sse2_exit);

MODULE_DESCRIPTION("Serpent Cipher Algorithm, SSE2 optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("serpent");
