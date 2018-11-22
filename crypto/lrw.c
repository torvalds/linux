/* LRW: as defined by Cyril Guyot in
 *	http://grouper.ieee.org/groups/1619/email/pdf00017.pdf
 *
 * Copyright (c) 2006 Rik Snel <rsnel@cube.dyndns.org>
 *
 * Based on ecb.c
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
/* This implementation is checked against the test vectors in the above
 * document and by a test vector provided by Ken Buchanan at
 * http://www.mail-archive.com/stds-p1619@listserv.ieee.org/msg00173.html
 *
 * The test vectors are included in the testing module tcrypt.[ch] */

#include <crypto/algapi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include <crypto/b128ops.h>
#include <crypto/gf128mul.h>
#include <crypto/lrw.h>

struct priv {
	struct crypto_cipher *child;
	struct lrw_table_ctx table;
};

static inline void setbit128_bbe(void *b, int bit)
{
	__set_bit(bit ^ (0x80 -
#ifdef __BIG_ENDIAN
			 BITS_PER_LONG
#else
			 BITS_PER_BYTE
#endif
			), b);
}

int lrw_init_table(struct lrw_table_ctx *ctx, const u8 *tweak)
{
	be128 tmp = { 0 };
	int i;

	if (ctx->table)
		gf128mul_free_64k(ctx->table);

	/* initialize multiplication table for Key2 */
	ctx->table = gf128mul_init_64k_bbe((be128 *)tweak);
	if (!ctx->table)
		return -ENOMEM;

	/* initialize optimization table */
	for (i = 0; i < 128; i++) {
		setbit128_bbe(&tmp, i);
		ctx->mulinc[i] = tmp;
		gf128mul_64k_bbe(&ctx->mulinc[i], ctx->table);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(lrw_init_table);

void lrw_free_table(struct lrw_table_ctx *ctx)
{
	if (ctx->table)
		gf128mul_free_64k(ctx->table);
}
EXPORT_SYMBOL_GPL(lrw_free_table);

static int setkey(struct crypto_tfm *parent, const u8 *key,
		  unsigned int keylen)
{
	struct priv *ctx = crypto_tfm_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err, bsize = LRW_BLOCK_SIZE;
	const u8 *tweak = key + keylen - bsize;

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_tfm_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(child, key, keylen - bsize);
	if (err)
		return err;
	crypto_tfm_set_flags(parent, crypto_cipher_get_flags(child) &
				     CRYPTO_TFM_RES_MASK);

	return lrw_init_table(&ctx->table, tweak);
}

struct sinfo {
	be128 t;
	struct crypto_tfm *tfm;
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *);
};

static inline void inc(be128 *iv)
{
	be64_add_cpu(&iv->b, 1);
	if (!iv->b)
		be64_add_cpu(&iv->a, 1);
}

static inline void lrw_round(struct sinfo *s, void *dst, const void *src)
{
	be128_xor(dst, &s->t, src);		/* PP <- T xor P */
	s->fn(s->tfm, dst, dst);		/* CC <- E(Key2,PP) */
	be128_xor(dst, dst, &s->t);		/* C <- T xor CC */
}

/* this returns the number of consequative 1 bits starting
 * from the right, get_index128(00 00 00 00 00 00 ... 00 00 10 FB) = 2 */
static inline int get_index128(be128 *block)
{
	int x;
	__be32 *p = (__be32 *) block;

	for (p += 3, x = 0; x < 128; p--, x += 32) {
		u32 val = be32_to_cpup(p);

		if (!~val)
			continue;

		return x + ffz(val);
	}

	/*
	 * If we get here, then x == 128 and we are incrementing the counter
	 * from all ones to all zeros. This means we must return index 127, i.e.
	 * the one corresponding to key2*{ 1,...,1 }.
	 */
	return 127;
}

static int crypt(struct blkcipher_desc *d,
		 struct blkcipher_walk *w, struct priv *ctx,
		 void (*fn)(struct crypto_tfm *, u8 *, const u8 *))
{
	int err;
	unsigned int avail;
	const int bs = LRW_BLOCK_SIZE;
	struct sinfo s = {
		.tfm = crypto_cipher_tfm(ctx->child),
		.fn = fn
	};
	be128 *iv;
	u8 *wsrc;
	u8 *wdst;

	err = blkcipher_walk_virt(d, w);
	if (!(avail = w->nbytes))
		return err;

	wsrc = w->src.virt.addr;
	wdst = w->dst.virt.addr;

	/* calculate first value of T */
	iv = (be128 *)w->iv;
	s.t = *iv;

	/* T <- I*Key2 */
	gf128mul_64k_bbe(&s.t, ctx->table.table);

	goto first;

	for (;;) {
		do {
			/* T <- I*Key2, using the optimization
			 * discussed in the specification */
			be128_xor(&s.t, &s.t,
				  &ctx->table.mulinc[get_index128(iv)]);
			inc(iv);

first:
			lrw_round(&s, wdst, wsrc);

			wsrc += bs;
			wdst += bs;
		} while ((avail -= bs) >= bs);

		err = blkcipher_walk_done(d, w, avail);
		if (!(avail = w->nbytes))
			break;

		wsrc = w->src.virt.addr;
		wdst = w->dst.virt.addr;
	}

	return err;
}

static int encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		   struct scatterlist *src, unsigned int nbytes)
{
	struct priv *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk w;

	blkcipher_walk_init(&w, dst, src, nbytes);
	return crypt(desc, &w, ctx,
		     crypto_cipher_alg(ctx->child)->cia_encrypt);
}

static int decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		   struct scatterlist *src, unsigned int nbytes)
{
	struct priv *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk w;

	blkcipher_walk_init(&w, dst, src, nbytes);
	return crypt(desc, &w, ctx,
		     crypto_cipher_alg(ctx->child)->cia_decrypt);
}

int lrw_crypt(struct blkcipher_desc *desc, struct scatterlist *sdst,
	      struct scatterlist *ssrc, unsigned int nbytes,
	      struct lrw_crypt_req *req)
{
	const unsigned int bsize = LRW_BLOCK_SIZE;
	const unsigned int max_blks = req->tbuflen / bsize;
	struct lrw_table_ctx *ctx = req->table_ctx;
	struct blkcipher_walk walk;
	unsigned int nblocks;
	be128 *iv, *src, *dst, *t;
	be128 *t_buf = req->tbuf;
	int err, i;

	BUG_ON(max_blks < 1);

	blkcipher_walk_init(&walk, sdst, ssrc, nbytes);

	err = blkcipher_walk_virt(desc, &walk);
	nbytes = walk.nbytes;
	if (!nbytes)
		return err;

	nblocks = min(walk.nbytes / bsize, max_blks);
	src = (be128 *)walk.src.virt.addr;
	dst = (be128 *)walk.dst.virt.addr;

	/* calculate first value of T */
	iv = (be128 *)walk.iv;
	t_buf[0] = *iv;

	/* T <- I*Key2 */
	gf128mul_64k_bbe(&t_buf[0], ctx->table);

	i = 0;
	goto first;

	for (;;) {
		do {
			for (i = 0; i < nblocks; i++) {
				/* T <- I*Key2, using the optimization
				 * discussed in the specification */
				be128_xor(&t_buf[i], t,
						&ctx->mulinc[get_index128(iv)]);
				inc(iv);
first:
				t = &t_buf[i];

				/* PP <- T xor P */
				be128_xor(dst + i, t, src + i);
			}

			/* CC <- E(Key2,PP) */
			req->crypt_fn(req->crypt_ctx, (u8 *)dst,
				      nblocks * bsize);

			/* C <- T xor CC */
			for (i = 0; i < nblocks; i++)
				be128_xor(dst + i, dst + i, &t_buf[i]);

			src += nblocks;
			dst += nblocks;
			nbytes -= nblocks * bsize;
			nblocks = min(nbytes / bsize, max_blks);
		} while (nblocks > 0);

		err = blkcipher_walk_done(desc, &walk, nbytes);
		nbytes = walk.nbytes;
		if (!nbytes)
			break;

		nblocks = min(nbytes / bsize, max_blks);
		src = (be128 *)walk.src.virt.addr;
		dst = (be128 *)walk.dst.virt.addr;
	}

	return err;
}
EXPORT_SYMBOL_GPL(lrw_crypt);

static int init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_cipher *cipher;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct priv *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	if (crypto_cipher_blocksize(cipher) != LRW_BLOCK_SIZE) {
		*flags |= CRYPTO_TFM_RES_BAD_BLOCK_LEN;
		crypto_free_cipher(cipher);
		return -EINVAL;
	}

	ctx->child = cipher;
	return 0;
}

static void exit_tfm(struct crypto_tfm *tfm)
{
	struct priv *ctx = crypto_tfm_ctx(tfm);

	lrw_free_table(&ctx->table);
	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_BLKCIPHER);
	if (err)
		return ERR_PTR(err);

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);

	inst = crypto_alloc_instance("lrw", alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_BLKCIPHER;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;

	if (alg->cra_alignmask < 7) inst->alg.cra_alignmask = 7;
	else inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_blkcipher_type;

	if (!(alg->cra_blocksize % 4))
		inst->alg.cra_alignmask |= 3;
	inst->alg.cra_blkcipher.ivsize = alg->cra_blocksize;
	inst->alg.cra_blkcipher.min_keysize =
		alg->cra_cipher.cia_min_keysize + alg->cra_blocksize;
	inst->alg.cra_blkcipher.max_keysize =
		alg->cra_cipher.cia_max_keysize + alg->cra_blocksize;

	inst->alg.cra_ctxsize = sizeof(struct priv);

	inst->alg.cra_init = init_tfm;
	inst->alg.cra_exit = exit_tfm;

	inst->alg.cra_blkcipher.setkey = setkey;
	inst->alg.cra_blkcipher.encrypt = encrypt;
	inst->alg.cra_blkcipher.decrypt = decrypt;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static void free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
}

static struct crypto_template crypto_tmpl = {
	.name = "lrw",
	.alloc = alloc,
	.free = free,
	.module = THIS_MODULE,
};

static int __init crypto_module_init(void)
{
	return crypto_register_template(&crypto_tmpl);
}

static void __exit crypto_module_exit(void)
{
	crypto_unregister_template(&crypto_tmpl);
}

module_init(crypto_module_init);
module_exit(crypto_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LRW block cipher mode");
MODULE_ALIAS_CRYPTO("lrw");
