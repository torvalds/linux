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
#include <asm/serpent.h>
#include <crypto/scatterwalk.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

struct async_serpent_ctx {
	struct cryptd_ablkcipher *cryptd_tfm;
};

static inline bool serpent_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	if (fpu_enabled)
		return true;

	/* SSE2 is only used when chunk to be processed is large enough, so
	 * do not enable FPU until it is necessary.
	 */
	if (nbytes < SERPENT_BLOCK_SIZE * SERPENT_PARALLEL_BLOCKS)
		return false;

	kernel_fpu_begin();
	return true;
}

static inline void serpent_fpu_end(bool fpu_enabled)
{
	if (fpu_enabled)
		kernel_fpu_end();
}

static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
		     bool enc)
{
	bool fpu_enabled = false;
	struct serpent_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	unsigned int nbytes;
	int err;

	err = blkcipher_walk_virt(desc, walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

		fpu_enabled = serpent_fpu_begin(fpu_enabled, nbytes);

		/* Process multi-block batch */
		if (nbytes >= bsize * SERPENT_PARALLEL_BLOCKS) {
			do {
				if (enc)
					serpent_enc_blk_xway(ctx, wdst, wsrc);
				else
					serpent_dec_blk_xway(ctx, wdst, wsrc);

				wsrc += bsize * SERPENT_PARALLEL_BLOCKS;
				wdst += bsize * SERPENT_PARALLEL_BLOCKS;
				nbytes -= bsize * SERPENT_PARALLEL_BLOCKS;
			} while (nbytes >= bsize * SERPENT_PARALLEL_BLOCKS);

			if (nbytes < bsize)
				goto done;
		}

		/* Handle leftovers */
		do {
			if (enc)
				__serpent_encrypt(ctx, wdst, wsrc);
			else
				__serpent_decrypt(ctx, wdst, wsrc);

			wsrc += bsize;
			wdst += bsize;
			nbytes -= bsize;
		} while (nbytes >= bsize);

done:
		err = blkcipher_walk_done(desc, walk, nbytes);
	}

	serpent_fpu_end(fpu_enabled);
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

static struct crypto_alg blk_ecb_alg = {
	.cra_name		= "__ecb-serpent-sse2",
	.cra_driver_name	= "__driver-ecb-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ecb_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
};

static unsigned int __cbc_encrypt(struct blkcipher_desc *desc,
				  struct blkcipher_walk *walk)
{
	struct serpent_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 *iv = (u128 *)walk->iv;

	do {
		u128_xor(dst, src, iv);
		__serpent_encrypt(ctx, (u8 *)dst, (u8 *)dst);
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
	struct serpent_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 ivs[SERPENT_PARALLEL_BLOCKS - 1];
	u128 last_iv;
	int i;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

	/* Process multi-block batch */
	if (nbytes >= bsize * SERPENT_PARALLEL_BLOCKS) {
		do {
			nbytes -= bsize * (SERPENT_PARALLEL_BLOCKS - 1);
			src -= SERPENT_PARALLEL_BLOCKS - 1;
			dst -= SERPENT_PARALLEL_BLOCKS - 1;

			for (i = 0; i < SERPENT_PARALLEL_BLOCKS - 1; i++)
				ivs[i] = src[i];

			serpent_dec_blk_xway(ctx, (u8 *)dst, (u8 *)src);

			for (i = 0; i < SERPENT_PARALLEL_BLOCKS - 1; i++)
				u128_xor(dst + (i + 1), dst + (i + 1), ivs + i);

			nbytes -= bsize;
			if (nbytes < bsize)
				goto done;

			u128_xor(dst, dst, src - 1);
			src -= 1;
			dst -= 1;
		} while (nbytes >= bsize * SERPENT_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Handle leftovers */
	for (;;) {
		__serpent_decrypt(ctx, (u8 *)dst, (u8 *)src);

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
		fpu_enabled = serpent_fpu_begin(fpu_enabled, nbytes);
		nbytes = __cbc_decrypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	serpent_fpu_end(fpu_enabled);
	return err;
}

static struct crypto_alg blk_cbc_alg = {
	.cra_name		= "__cbc-serpent-sse2",
	.cra_driver_name	= "__driver-cbc-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_cbc_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
};

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
	struct serpent_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	u8 *ctrblk = walk->iv;
	u8 keystream[SERPENT_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	__serpent_encrypt(ctx, keystream, ctrblk);
	crypto_xor(keystream, src, nbytes);
	memcpy(dst, keystream, nbytes);

	crypto_inc(ctrblk, SERPENT_BLOCK_SIZE);
}

static unsigned int __ctr_crypt(struct blkcipher_desc *desc,
				struct blkcipher_walk *walk)
{
	struct serpent_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 ctrblk;
	be128 ctrblocks[SERPENT_PARALLEL_BLOCKS];
	int i;

	be128_to_u128(&ctrblk, (be128 *)walk->iv);

	/* Process multi-block batch */
	if (nbytes >= bsize * SERPENT_PARALLEL_BLOCKS) {
		do {
			/* create ctrblks for parallel encrypt */
			for (i = 0; i < SERPENT_PARALLEL_BLOCKS; i++) {
				if (dst != src)
					dst[i] = src[i];

				u128_to_be128(&ctrblocks[i], &ctrblk);
				u128_inc(&ctrblk);
			}

			serpent_enc_blk_xway_xor(ctx, (u8 *)dst,
						 (u8 *)ctrblocks);

			src += SERPENT_PARALLEL_BLOCKS;
			dst += SERPENT_PARALLEL_BLOCKS;
			nbytes -= bsize * SERPENT_PARALLEL_BLOCKS;
		} while (nbytes >= bsize * SERPENT_PARALLEL_BLOCKS);

		if (nbytes < bsize)
			goto done;
	}

	/* Handle leftovers */
	do {
		if (dst != src)
			*dst = *src;

		u128_to_be128(&ctrblocks[0], &ctrblk);
		u128_inc(&ctrblk);

		__serpent_encrypt(ctx, (u8 *)ctrblocks, (u8 *)ctrblocks);
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
	err = blkcipher_walk_virt_block(desc, &walk, SERPENT_BLOCK_SIZE);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	while ((nbytes = walk.nbytes) >= SERPENT_BLOCK_SIZE) {
		fpu_enabled = serpent_fpu_begin(fpu_enabled, nbytes);
		nbytes = __ctr_crypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	serpent_fpu_end(fpu_enabled);

	if (walk.nbytes) {
		ctr_crypt_final(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg blk_ctr_alg = {
	.cra_name		= "__ctr-serpent-sse2",
	.cra_driver_name	= "__driver-ctr-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ctr_alg.cra_list),
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
};

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

static struct crypto_alg blk_lrw_alg = {
	.cra_name		= "__lrw-serpent-sse2",
	.cra_driver_name	= "__driver-lrw-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_lrw_alg.cra_list),
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
};

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

static struct crypto_alg blk_xts_alg = {
	.cra_name		= "__xts-serpent-sse2",
	.cra_driver_name	= "__driver-xts-serpent-sse2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_xts_alg.cra_list),
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
};

static int ablk_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
			unsigned int key_len)
{
	struct async_serpent_ctx *ctx = crypto_ablkcipher_ctx(tfm);
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
	struct async_serpent_ctx *ctx = crypto_ablkcipher_ctx(tfm);
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
	struct async_serpent_ctx *ctx = crypto_ablkcipher_ctx(tfm);

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
	struct async_serpent_ctx *ctx = crypto_ablkcipher_ctx(tfm);

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
	struct async_serpent_ctx *ctx = crypto_tfm_ctx(tfm);

	cryptd_free_ablkcipher(ctx->cryptd_tfm);
}

static void ablk_init_common(struct crypto_tfm *tfm,
			     struct cryptd_ablkcipher *cryptd_tfm)
{
	struct async_serpent_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->cryptd_tfm = cryptd_tfm;
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request) +
		crypto_ablkcipher_reqsize(&cryptd_tfm->base);
}

static int ablk_ecb_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-ecb-serpent-sse2", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_ecb_alg = {
	.cra_name		= "ecb(serpent)",
	.cra_driver_name	= "ecb-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_ecb_alg.cra_list),
	.cra_init		= ablk_ecb_init,
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
};

static int ablk_cbc_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-cbc-serpent-sse2", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_cbc_alg = {
	.cra_name		= "cbc(serpent)",
	.cra_driver_name	= "cbc-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_cbc_alg.cra_list),
	.cra_init		= ablk_cbc_init,
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
};

static int ablk_ctr_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-ctr-serpent-sse2", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_ctr_alg = {
	.cra_name		= "ctr(serpent)",
	.cra_driver_name	= "ctr-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_ctr_alg.cra_list),
	.cra_init		= ablk_ctr_init,
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
};

static int ablk_lrw_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-lrw-serpent-sse2", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_lrw_alg = {
	.cra_name		= "lrw(serpent)",
	.cra_driver_name	= "lrw-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_lrw_alg.cra_list),
	.cra_init		= ablk_lrw_init,
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
};

static int ablk_xts_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-xts-serpent-sse2", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_xts_alg = {
	.cra_name		= "xts(serpent)",
	.cra_driver_name	= "xts-serpent-sse2",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_xts_alg.cra_list),
	.cra_init		= ablk_xts_init,
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
};

static int __init serpent_sse2_init(void)
{
	int err;

	if (!cpu_has_xmm2) {
		printk(KERN_INFO "SSE2 instructions are not detected.\n");
		return -ENODEV;
	}

	err = crypto_register_alg(&blk_ecb_alg);
	if (err)
		goto blk_ecb_err;
	err = crypto_register_alg(&blk_cbc_alg);
	if (err)
		goto blk_cbc_err;
	err = crypto_register_alg(&blk_ctr_alg);
	if (err)
		goto blk_ctr_err;
	err = crypto_register_alg(&ablk_ecb_alg);
	if (err)
		goto ablk_ecb_err;
	err = crypto_register_alg(&ablk_cbc_alg);
	if (err)
		goto ablk_cbc_err;
	err = crypto_register_alg(&ablk_ctr_alg);
	if (err)
		goto ablk_ctr_err;
	err = crypto_register_alg(&blk_lrw_alg);
	if (err)
		goto blk_lrw_err;
	err = crypto_register_alg(&ablk_lrw_alg);
	if (err)
		goto ablk_lrw_err;
	err = crypto_register_alg(&blk_xts_alg);
	if (err)
		goto blk_xts_err;
	err = crypto_register_alg(&ablk_xts_alg);
	if (err)
		goto ablk_xts_err;
	return err;

	crypto_unregister_alg(&ablk_xts_alg);
ablk_xts_err:
	crypto_unregister_alg(&blk_xts_alg);
blk_xts_err:
	crypto_unregister_alg(&ablk_lrw_alg);
ablk_lrw_err:
	crypto_unregister_alg(&blk_lrw_alg);
blk_lrw_err:
	crypto_unregister_alg(&ablk_ctr_alg);
ablk_ctr_err:
	crypto_unregister_alg(&ablk_cbc_alg);
ablk_cbc_err:
	crypto_unregister_alg(&ablk_ecb_alg);
ablk_ecb_err:
	crypto_unregister_alg(&blk_ctr_alg);
blk_ctr_err:
	crypto_unregister_alg(&blk_cbc_alg);
blk_cbc_err:
	crypto_unregister_alg(&blk_ecb_alg);
blk_ecb_err:
	return err;
}

static void __exit serpent_sse2_exit(void)
{
	crypto_unregister_alg(&ablk_xts_alg);
	crypto_unregister_alg(&blk_xts_alg);
	crypto_unregister_alg(&ablk_lrw_alg);
	crypto_unregister_alg(&blk_lrw_alg);
	crypto_unregister_alg(&ablk_ctr_alg);
	crypto_unregister_alg(&ablk_cbc_alg);
	crypto_unregister_alg(&ablk_ecb_alg);
	crypto_unregister_alg(&blk_ctr_alg);
	crypto_unregister_alg(&blk_cbc_alg);
	crypto_unregister_alg(&blk_ecb_alg);
}

module_init(serpent_sse2_init);
module_exit(serpent_sse2_exit);

MODULE_DESCRIPTION("Serpent Cipher Algorithm, SSE2 optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("serpent");
