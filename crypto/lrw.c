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

#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include <crypto/b128ops.h>
#include <crypto/gf128mul.h>
#include <crypto/lrw.h>

#define LRW_BUFFER_SIZE 128u

struct priv {
	struct crypto_skcipher *child;
	struct lrw_table_ctx table;
};

struct rctx {
	be128 buf[LRW_BUFFER_SIZE / sizeof(be128)];

	be128 t;

	be128 *ext;

	struct scatterlist srcbuf[2];
	struct scatterlist dstbuf[2];
	struct scatterlist *src;
	struct scatterlist *dst;

	unsigned int left;

	struct skcipher_request subreq;
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

static int setkey(struct crypto_skcipher *parent, const u8 *key,
		  unsigned int keylen)
{
	struct priv *ctx = crypto_skcipher_ctx(parent);
	struct crypto_skcipher *child = ctx->child;
	int err, bsize = LRW_BLOCK_SIZE;
	const u8 *tweak = key + keylen - bsize;

	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, crypto_skcipher_get_flags(parent) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(child, key, keylen - bsize);
	crypto_skcipher_set_flags(parent, crypto_skcipher_get_flags(child) &
					  CRYPTO_TFM_RES_MASK);
	if (err)
		return err;

	return lrw_init_table(&ctx->table, tweak);
}

static inline void inc(be128 *iv)
{
	be64_add_cpu(&iv->b, 1);
	if (!iv->b)
		be64_add_cpu(&iv->a, 1);
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

static int post_crypt(struct skcipher_request *req)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	be128 *buf = rctx->ext ?: rctx->buf;
	struct skcipher_request *subreq;
	const int bs = LRW_BLOCK_SIZE;
	struct skcipher_walk w;
	struct scatterlist *sg;
	unsigned offset;
	int err;

	subreq = &rctx->subreq;
	err = skcipher_walk_virt(&w, subreq, false);

	while (w.nbytes) {
		unsigned int avail = w.nbytes;
		be128 *wdst;

		wdst = w.dst.virt.addr;

		do {
			be128_xor(wdst, buf++, wdst);
			wdst++;
		} while ((avail -= bs) >= bs);

		err = skcipher_walk_done(&w, avail);
	}

	rctx->left -= subreq->cryptlen;

	if (err || !rctx->left)
		goto out;

	rctx->dst = rctx->dstbuf;

	scatterwalk_done(&w.out, 0, 1);
	sg = w.out.sg;
	offset = w.out.offset;

	if (rctx->dst != sg) {
		rctx->dst[0] = *sg;
		sg_unmark_end(rctx->dst);
		scatterwalk_crypto_chain(rctx->dst, sg_next(sg), 0, 2);
	}
	rctx->dst[0].length -= offset - sg->offset;
	rctx->dst[0].offset = offset;

out:
	return err;
}

static int pre_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct rctx *rctx = skcipher_request_ctx(req);
	struct priv *ctx = crypto_skcipher_ctx(tfm);
	be128 *buf = rctx->ext ?: rctx->buf;
	struct skcipher_request *subreq;
	const int bs = LRW_BLOCK_SIZE;
	struct skcipher_walk w;
	struct scatterlist *sg;
	unsigned cryptlen;
	unsigned offset;
	be128 *iv;
	bool more;
	int err;

	subreq = &rctx->subreq;
	skcipher_request_set_tfm(subreq, tfm);

	cryptlen = subreq->cryptlen;
	more = rctx->left > cryptlen;
	if (!more)
		cryptlen = rctx->left;

	skcipher_request_set_crypt(subreq, rctx->src, rctx->dst,
				   cryptlen, req->iv);

	err = skcipher_walk_virt(&w, subreq, false);
	iv = w.iv;

	while (w.nbytes) {
		unsigned int avail = w.nbytes;
		be128 *wsrc;
		be128 *wdst;

		wsrc = w.src.virt.addr;
		wdst = w.dst.virt.addr;

		do {
			*buf++ = rctx->t;
			be128_xor(wdst++, &rctx->t, wsrc++);

			/* T <- I*Key2, using the optimization
			 * discussed in the specification */
			be128_xor(&rctx->t, &rctx->t,
				  &ctx->table.mulinc[get_index128(iv)]);
			inc(iv);
		} while ((avail -= bs) >= bs);

		err = skcipher_walk_done(&w, avail);
	}

	skcipher_request_set_tfm(subreq, ctx->child);
	skcipher_request_set_crypt(subreq, rctx->dst, rctx->dst,
				   cryptlen, NULL);

	if (err || !more)
		goto out;

	rctx->src = rctx->srcbuf;

	scatterwalk_done(&w.in, 0, 1);
	sg = w.in.sg;
	offset = w.in.offset;

	if (rctx->src != sg) {
		rctx->src[0] = *sg;
		sg_unmark_end(rctx->src);
		scatterwalk_crypto_chain(rctx->src, sg_next(sg), 0, 2);
	}
	rctx->src[0].length -= offset - sg->offset;
	rctx->src[0].offset = offset;

out:
	return err;
}

static int init_crypt(struct skcipher_request *req, crypto_completion_t done)
{
	struct priv *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq;
	gfp_t gfp;

	subreq = &rctx->subreq;
	skcipher_request_set_callback(subreq, req->base.flags, done, req);

	gfp = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
							   GFP_ATOMIC;
	rctx->ext = NULL;

	subreq->cryptlen = LRW_BUFFER_SIZE;
	if (req->cryptlen > LRW_BUFFER_SIZE) {
		unsigned int n = min(req->cryptlen, (unsigned int)PAGE_SIZE);

		rctx->ext = kmalloc(n, gfp);
		if (rctx->ext)
			subreq->cryptlen = n;
	}

	rctx->src = req->src;
	rctx->dst = req->dst;
	rctx->left = req->cryptlen;

	/* calculate first value of T */
	memcpy(&rctx->t, req->iv, sizeof(rctx->t));

	/* T <- I*Key2 */
	gf128mul_64k_bbe(&rctx->t, ctx->table.table);

	return 0;
}

static void exit_crypt(struct skcipher_request *req)
{
	struct rctx *rctx = skcipher_request_ctx(req);

	rctx->left = 0;

	if (rctx->ext)
		kfree(rctx->ext);
}

static int do_encrypt(struct skcipher_request *req, int err)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq;

	subreq = &rctx->subreq;

	while (!err && rctx->left) {
		err = pre_crypt(req) ?:
		      crypto_skcipher_encrypt(subreq) ?:
		      post_crypt(req);

		if (err == -EINPROGRESS || err == -EBUSY)
			return err;
	}

	exit_crypt(req);
	return err;
}

static void encrypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;
	struct skcipher_request *subreq;
	struct rctx *rctx;

	rctx = skcipher_request_ctx(req);

	if (err == -EINPROGRESS) {
		if (rctx->left != req->cryptlen)
			return;
		goto out;
	}

	subreq = &rctx->subreq;
	subreq->base.flags &= CRYPTO_TFM_REQ_MAY_BACKLOG;

	err = do_encrypt(req, err ?: post_crypt(req));
	if (rctx->left)
		return;

out:
	skcipher_request_complete(req, err);
}

static int encrypt(struct skcipher_request *req)
{
	return do_encrypt(req, init_crypt(req, encrypt_done));
}

static int do_decrypt(struct skcipher_request *req, int err)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq;

	subreq = &rctx->subreq;

	while (!err && rctx->left) {
		err = pre_crypt(req) ?:
		      crypto_skcipher_decrypt(subreq) ?:
		      post_crypt(req);

		if (err == -EINPROGRESS || err == -EBUSY)
			return err;
	}

	exit_crypt(req);
	return err;
}

static void decrypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;
	struct skcipher_request *subreq;
	struct rctx *rctx;

	rctx = skcipher_request_ctx(req);

	if (err == -EINPROGRESS) {
		if (rctx->left != req->cryptlen)
			return;
		goto out;
	}

	subreq = &rctx->subreq;
	subreq->base.flags &= CRYPTO_TFM_REQ_MAY_BACKLOG;

	err = do_decrypt(req, err ?: post_crypt(req));
	if (rctx->left)
		return;

out:
	skcipher_request_complete(req, err);
}

static int decrypt(struct skcipher_request *req)
{
	return do_decrypt(req, init_crypt(req, decrypt_done));
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

static int init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct crypto_skcipher_spawn *spawn = skcipher_instance_ctx(inst);
	struct priv *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *cipher;

	cipher = crypto_spawn_skcipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	ctx->child = cipher;

	crypto_skcipher_set_reqsize(tfm, crypto_skcipher_reqsize(cipher) +
					 sizeof(struct rctx));

	return 0;
}

static void exit_tfm(struct crypto_skcipher *tfm)
{
	struct priv *ctx = crypto_skcipher_ctx(tfm);

	lrw_free_table(&ctx->table);
	crypto_free_skcipher(ctx->child);
}

static void free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct crypto_skcipher_spawn *spawn;
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct skcipher_alg *alg;
	const char *cipher_name;
	char ecb_name[CRYPTO_MAX_ALG_NAME];
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_SKCIPHER) & algt->mask)
		return -EINVAL;

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*spawn), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	spawn = skcipher_instance_ctx(inst);

	crypto_set_skcipher_spawn(spawn, skcipher_crypto_instance(inst));
	err = crypto_grab_skcipher(spawn, cipher_name, 0,
				   crypto_requires_sync(algt->type,
							algt->mask));
	if (err == -ENOENT) {
		err = -ENAMETOOLONG;
		if (snprintf(ecb_name, CRYPTO_MAX_ALG_NAME, "ecb(%s)",
			     cipher_name) >= CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;

		err = crypto_grab_skcipher(spawn, ecb_name, 0,
					   crypto_requires_sync(algt->type,
								algt->mask));
	}

	if (err)
		goto err_free_inst;

	alg = crypto_skcipher_spawn_alg(spawn);

	err = -EINVAL;
	if (alg->base.cra_blocksize != LRW_BLOCK_SIZE)
		goto err_drop_spawn;

	if (crypto_skcipher_alg_ivsize(alg))
		goto err_drop_spawn;

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "lrw",
				  &alg->base);
	if (err)
		goto err_drop_spawn;

	err = -EINVAL;
	cipher_name = alg->base.cra_name;

	/* Alas we screwed up the naming so we have to mangle the
	 * cipher name.
	 */
	if (!strncmp(cipher_name, "ecb(", 4)) {
		unsigned len;

		len = strlcpy(ecb_name, cipher_name + 4, sizeof(ecb_name));
		if (len < 2 || len >= sizeof(ecb_name))
			goto err_drop_spawn;

		if (ecb_name[len - 1] != ')')
			goto err_drop_spawn;

		ecb_name[len - 1] = 0;

		if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
			     "lrw(%s)", ecb_name) >= CRYPTO_MAX_ALG_NAME) {
			err = -ENAMETOOLONG;
			goto err_drop_spawn;
		}
	} else
		goto err_drop_spawn;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = LRW_BLOCK_SIZE;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask |
				       (__alignof__(u64) - 1);

	inst->alg.ivsize = LRW_BLOCK_SIZE;
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(alg) +
				LRW_BLOCK_SIZE;
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(alg) +
				LRW_BLOCK_SIZE;

	inst->alg.base.cra_ctxsize = sizeof(struct priv);

	inst->alg.init = init_tfm;
	inst->alg.exit = exit_tfm;

	inst->alg.setkey = setkey;
	inst->alg.encrypt = encrypt;
	inst->alg.decrypt = decrypt;

	inst->free = free;

	err = skcipher_register_instance(tmpl, inst);
	if (err)
		goto err_drop_spawn;

out:
	return err;

err_drop_spawn:
	crypto_drop_skcipher(spawn);
err_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_tmpl = {
	.name = "lrw",
	.create = create,
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
