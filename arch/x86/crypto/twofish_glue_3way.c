/*
 * Glue Code for 3-way parallel assembler optimized version of Twofish
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

#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <crypto/algapi.h>
#include <crypto/twofish.h>
#include <crypto/b128ops.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>

/* regular block cipher functions from twofish_x86_64 module */
asmlinkage void twofish_enc_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);
asmlinkage void twofish_dec_blk(struct twofish_ctx *ctx, u8 *dst,
				const u8 *src);

/* 3-way parallel cipher functions */
asmlinkage void __twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
				       const u8 *src, bool xor);
asmlinkage void twofish_dec_blk_3way(struct twofish_ctx *ctx, u8 *dst,
				     const u8 *src);

static inline void twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, false);
}

static inline void twofish_enc_blk_xor_3way(struct twofish_ctx *ctx, u8 *dst,
					    const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, true);
}

static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
		     void (*fn)(struct twofish_ctx *, u8 *, const u8 *),
		     void (*fn_3way)(struct twofish_ctx *, u8 *, const u8 *))
{
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes;
	int err;

	err = blkcipher_walk_virt(desc, walk);

	while ((nbytes = walk->nbytes)) {
		u8 *wsrc = walk->src.virt.addr;
		u8 *wdst = walk->dst.virt.addr;

		/* Process three block batch */
		if (nbytes >= bsize * 3) {
			do {
				fn_3way(ctx, wdst, wsrc);

				wsrc += bsize * 3;
				wdst += bsize * 3;
				nbytes -= bsize * 3;
			} while (nbytes >= bsize * 3);

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
	return ecb_crypt(desc, &walk, twofish_enc_blk, twofish_enc_blk_3way);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct blkcipher_walk walk;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	return ecb_crypt(desc, &walk, twofish_dec_blk, twofish_dec_blk_3way);
}

static struct crypto_alg blk_ecb_alg = {
	.cra_name		= "ecb(twofish)",
	.cra_driver_name	= "ecb-twofish-3way",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ecb_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
};

static unsigned int __cbc_encrypt(struct blkcipher_desc *desc,
				  struct blkcipher_walk *walk)
{
	struct twofish_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	unsigned int bsize = TF_BLOCK_SIZE;
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
	unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 ivs[3 - 1];
	u128 last_iv;

	/* Start of the last block. */
	src += nbytes / bsize - 1;
	dst += nbytes / bsize - 1;

	last_iv = *src;

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
	.cra_name		= "cbc(twofish)",
	.cra_driver_name	= "cbc-twofish-3way",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_cbc_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= twofish_setkey,
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
	unsigned int bsize = TF_BLOCK_SIZE;
	unsigned int nbytes = walk->nbytes;
	u128 *src = (u128 *)walk->src.virt.addr;
	u128 *dst = (u128 *)walk->dst.virt.addr;
	u128 ctrblk;
	be128 ctrblocks[3];

	be128_to_u128(&ctrblk, (be128 *)walk->iv);

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

			twofish_enc_blk_xor_3way(ctx, (u8 *)dst,
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
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, TF_BLOCK_SIZE);

	while ((nbytes = walk.nbytes) >= TF_BLOCK_SIZE) {
		nbytes = __ctr_crypt(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	if (walk.nbytes) {
		ctr_crypt_final(desc, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}

	return err;
}

static struct crypto_alg blk_ctr_alg = {
	.cra_name		= "ctr(twofish)",
	.cra_driver_name	= "ctr-twofish-3way",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ctr_alg.cra_list),
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
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = TF_BLOCK_SIZE;
	struct twofish_ctx *ctx = priv;
	int i;

	if (nbytes == 3 * bsize) {
		twofish_enc_blk_3way(ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		twofish_enc_blk(ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = TF_BLOCK_SIZE;
	struct twofish_ctx *ctx = priv;
	int i;

	if (nbytes == 3 * bsize) {
		twofish_dec_blk_3way(ctx, srcdst, srcdst);
		return;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		twofish_dec_blk(ctx, srcdst, srcdst);
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

	err = __twofish_setkey(&ctx->twofish_ctx, key, keylen - TF_BLOCK_SIZE,
			       &tfm->crt_flags);
	if (err)
		return err;

	return lrw_init_table(&ctx->lrw_table, key + keylen - TF_BLOCK_SIZE);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[3];
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &ctx->twofish_ctx,
		.crypt_fn = encrypt_callback,
	};

	return lrw_crypt(desc, dst, src, nbytes, &req);
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[3];
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &ctx->twofish_ctx,
		.crypt_fn = decrypt_callback,
	};

	return lrw_crypt(desc, dst, src, nbytes, &req);
}

static void lrw_exit_tfm(struct crypto_tfm *tfm)
{
	struct twofish_lrw_ctx *ctx = crypto_tfm_ctx(tfm);

	lrw_free_table(&ctx->lrw_table);
}

static struct crypto_alg blk_lrw_alg = {
	.cra_name		= "lrw(twofish)",
	.cra_driver_name	= "lrw-twofish-3way",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_lrw_alg.cra_list),
	.cra_exit		= lrw_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE + TF_BLOCK_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE + TF_BLOCK_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= lrw_twofish_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
};

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
	return __twofish_setkey(&ctx->tweak_ctx, key + keylen / 2, keylen / 2,
				flags);
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[3];
	struct xts_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.tweak_ctx = &ctx->tweak_ctx,
		.tweak_fn = XTS_TWEAK_CAST(twofish_enc_blk),
		.crypt_ctx = &ctx->crypt_ctx,
		.crypt_fn = encrypt_callback,
	};

	return xts_crypt(desc, dst, src, nbytes, &req);
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[3];
	struct xts_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.tweak_ctx = &ctx->tweak_ctx,
		.tweak_fn = XTS_TWEAK_CAST(twofish_enc_blk),
		.crypt_ctx = &ctx->crypt_ctx,
		.crypt_fn = decrypt_callback,
	};

	return xts_crypt(desc, dst, src, nbytes, &req);
}

static struct crypto_alg blk_xts_alg = {
	.cra_name		= "xts(twofish)",
	.cra_driver_name	= "xts-twofish-3way",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_xts_alg.cra_list),
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
};

int __init init(void)
{
	int err;

	err = crypto_register_alg(&blk_ecb_alg);
	if (err)
		goto ecb_err;
	err = crypto_register_alg(&blk_cbc_alg);
	if (err)
		goto cbc_err;
	err = crypto_register_alg(&blk_ctr_alg);
	if (err)
		goto ctr_err;
	err = crypto_register_alg(&blk_lrw_alg);
	if (err)
		goto blk_lrw_err;
	err = crypto_register_alg(&blk_xts_alg);
	if (err)
		goto blk_xts_err;

	return 0;

	crypto_unregister_alg(&blk_xts_alg);
blk_xts_err:
	crypto_unregister_alg(&blk_lrw_alg);
blk_lrw_err:
	crypto_unregister_alg(&blk_ctr_alg);
ctr_err:
	crypto_unregister_alg(&blk_cbc_alg);
cbc_err:
	crypto_unregister_alg(&blk_ecb_alg);
ecb_err:
	return err;
}

void __exit fini(void)
{
	crypto_unregister_alg(&blk_xts_alg);
	crypto_unregister_alg(&blk_lrw_alg);
	crypto_unregister_alg(&blk_ctr_alg);
	crypto_unregister_alg(&blk_cbc_alg);
	crypto_unregister_alg(&blk_ecb_alg);
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Twofish Cipher Algorithm, 3-way parallel asm optimized");
MODULE_ALIAS("twofish");
MODULE_ALIAS("twofish-asm");
