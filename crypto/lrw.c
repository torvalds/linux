/* LRW: as defined by Cyril Guyot in
 *	http://grouper.ieee.org/groups/1619/email/pdf00017.pdf
 *
 * Copyright (c) 2006 Rik Snel <rsnel@cube.dyndns.org>
 *
 * Based om ecb.c
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

struct priv {
	struct crypto_cipher *child;
	/* optimizes multiplying a random (non incrementing, as at the
	 * start of a new sector) value with key2, we could also have
	 * used 4k optimization tables or no optimization at all. In the
	 * latter case we would have to store key2 here */
	struct gf128mul_64k *table;
	/* stores:
	 *  key2*{ 0,0,...0,0,0,0,1 }, key2*{ 0,0,...0,0,0,1,1 },
	 *  key2*{ 0,0,...0,0,1,1,1 }, key2*{ 0,0,...0,1,1,1,1 }
	 *  key2*{ 0,0,...1,1,1,1,1 }, etc
	 * needed for optimized multiplication of incrementing values
	 * with key2 */
	be128 mulinc[128];
};

static inline void setbit128_bbe(void *b, int bit)
{
	__set_bit(bit ^ 0x78, b);
}

static int setkey(struct crypto_tfm *parent, const u8 *key,
		  unsigned int keylen)
{
	struct priv *ctx = crypto_tfm_ctx(parent);
	struct crypto_cipher *child = ctx->child;
	int err, i;
	be128 tmp = { 0 };
	int bsize = crypto_cipher_blocksize(child);

	crypto_cipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(child, crypto_tfm_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	if ((err = crypto_cipher_setkey(child, key, keylen - bsize)))
		return err;
	crypto_tfm_set_flags(parent, crypto_cipher_get_flags(child) &
				     CRYPTO_TFM_RES_MASK);

	if (ctx->table)
		gf128mul_free_64k(ctx->table);

	/* initialize multiplication table for Key2 */
	ctx->table = gf128mul_init_64k_bbe((be128 *)(key + keylen - bsize));
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

struct sinfo {
	be128 t;
	struct crypto_tfm *tfm;
	void (*fn)(struct crypto_tfm *, u8 *, const u8 *);
};

static inline void inc(be128 *iv)
{
	if (!(iv->b = cpu_to_be64(be64_to_cpu(iv->b) + 1)))
		iv->a = cpu_to_be64(be64_to_cpu(iv->a) + 1);
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

	return x;
}

static int crypt(struct blkcipher_desc *d,
		 struct blkcipher_walk *w, struct priv *ctx,
		 void (*fn)(struct crypto_tfm *, u8 *, const u8 *))
{
	int err;
	unsigned int avail;
	const int bs = crypto_cipher_blocksize(ctx->child);
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
	gf128mul_64k_bbe(&s.t, ctx->table);

	goto first;

	for (;;) {
		do {
			/* T <- I*Key2, using the optimization
			 * discussed in the specification */
			be128_xor(&s.t, &s.t, &ctx->mulinc[get_index128(iv)]);
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

static int init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct priv *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;

	tfm = crypto_spawn_tfm(spawn);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	if (crypto_tfm_alg_blocksize(tfm) != 16) {
		*flags |= CRYPTO_TFM_RES_BAD_BLOCK_LEN;
		return -EINVAL;
	}

	ctx->child = crypto_cipher_cast(tfm);
	return 0;
}

static void exit_tfm(struct crypto_tfm *tfm)
{
	struct priv *ctx = crypto_tfm_ctx(tfm);
	if (ctx->table)
		gf128mul_free_64k(ctx->table);
	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *alloc(void *param, unsigned int len)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;

	alg = crypto_get_attr_alg(param, len, CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK | CRYPTO_ALG_ASYNC);
	if (IS_ERR(alg))
		return ERR_PTR(PTR_ERR(alg));

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
