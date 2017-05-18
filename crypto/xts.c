/* XTS: as defined in IEEE1619/D16
 *	http://grouper.ieee.org/groups/1619/email/pdf00086.pdf
 *	(sector sizes which are not a multiple of 16 bytes are,
 *	however currently unsupported)
 *
 * Copyright (c) 2007 Rik Snel <rsnel@cube.dyndns.org>
 *
 * Based on ecb.c
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include <crypto/xts.h>
#include <crypto/b128ops.h>
#include <crypto/gf128mul.h>

#define XTS_BUFFER_SIZE 128u

struct priv {
	struct crypto_skcipher *child;
	struct crypto_cipher *tweak;
};

struct xts_instance_ctx {
	struct crypto_skcipher_spawn spawn;
	char name[CRYPTO_MAX_ALG_NAME];
};

struct rctx {
	le128 buf[XTS_BUFFER_SIZE / sizeof(le128)];

	le128 t;

	le128 *ext;

	struct scatterlist srcbuf[2];
	struct scatterlist dstbuf[2];
	struct scatterlist *src;
	struct scatterlist *dst;

	unsigned int left;

	struct skcipher_request subreq;
};

static int setkey(struct crypto_skcipher *parent, const u8 *key,
		  unsigned int keylen)
{
	struct priv *ctx = crypto_skcipher_ctx(parent);
	struct crypto_skcipher *child;
	struct crypto_cipher *tweak;
	int err;

	err = xts_verify_key(parent, key, keylen);
	if (err)
		return err;

	keylen /= 2;

	/* we need two cipher instances: one to compute the initial 'tweak'
	 * by encrypting the IV (usually the 'plain' iv) and the other
	 * one to encrypt and decrypt the data */

	/* tweak cipher, uses Key2 i.e. the second half of *key */
	tweak = ctx->tweak;
	crypto_cipher_clear_flags(tweak, CRYPTO_TFM_REQ_MASK);
	crypto_cipher_set_flags(tweak, crypto_skcipher_get_flags(parent) &
				       CRYPTO_TFM_REQ_MASK);
	err = crypto_cipher_setkey(tweak, key + keylen, keylen);
	crypto_skcipher_set_flags(parent, crypto_cipher_get_flags(tweak) &
					  CRYPTO_TFM_RES_MASK);
	if (err)
		return err;

	/* data cipher, uses Key1 i.e. the first half of *key */
	child = ctx->child;
	crypto_skcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(child, crypto_skcipher_get_flags(parent) &
					 CRYPTO_TFM_REQ_MASK);
	err = crypto_skcipher_setkey(child, key, keylen);
	crypto_skcipher_set_flags(parent, crypto_skcipher_get_flags(child) &
					  CRYPTO_TFM_RES_MASK);

	return err;
}

static int post_crypt(struct skcipher_request *req)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	le128 *buf = rctx->ext ?: rctx->buf;
	struct skcipher_request *subreq;
	const int bs = XTS_BLOCK_SIZE;
	struct skcipher_walk w;
	struct scatterlist *sg;
	unsigned offset;
	int err;

	subreq = &rctx->subreq;
	err = skcipher_walk_virt(&w, subreq, false);

	while (w.nbytes) {
		unsigned int avail = w.nbytes;
		le128 *wdst;

		wdst = w.dst.virt.addr;

		do {
			le128_xor(wdst, buf++, wdst);
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
	struct rctx *rctx = skcipher_request_ctx(req);
	le128 *buf = rctx->ext ?: rctx->buf;
	struct skcipher_request *subreq;
	const int bs = XTS_BLOCK_SIZE;
	struct skcipher_walk w;
	struct scatterlist *sg;
	unsigned cryptlen;
	unsigned offset;
	bool more;
	int err;

	subreq = &rctx->subreq;
	cryptlen = subreq->cryptlen;

	more = rctx->left > cryptlen;
	if (!more)
		cryptlen = rctx->left;

	skcipher_request_set_crypt(subreq, rctx->src, rctx->dst,
				   cryptlen, NULL);

	err = skcipher_walk_virt(&w, subreq, false);

	while (w.nbytes) {
		unsigned int avail = w.nbytes;
		le128 *wsrc;
		le128 *wdst;

		wsrc = w.src.virt.addr;
		wdst = w.dst.virt.addr;

		do {
			*buf++ = rctx->t;
			le128_xor(wdst++, &rctx->t, wsrc++);
			gf128mul_x_ble(&rctx->t, &rctx->t);
		} while ((avail -= bs) >= bs);

		err = skcipher_walk_done(&w, avail);
	}

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
	skcipher_request_set_tfm(subreq, ctx->child);
	skcipher_request_set_callback(subreq, req->base.flags, done, req);

	gfp = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL :
							   GFP_ATOMIC;
	rctx->ext = NULL;

	subreq->cryptlen = XTS_BUFFER_SIZE;
	if (req->cryptlen > XTS_BUFFER_SIZE) {
		unsigned int n = min(req->cryptlen, (unsigned int)PAGE_SIZE);

		rctx->ext = kmalloc(n, gfp);
		if (rctx->ext)
			subreq->cryptlen = n;
	}

	rctx->src = req->src;
	rctx->dst = req->dst;
	rctx->left = req->cryptlen;

	/* calculate first value of T */
	crypto_cipher_encrypt_one(ctx->tweak, (u8 *)&rctx->t, req->iv);

	return 0;
}

static void exit_crypt(struct skcipher_request *req)
{
	struct rctx *rctx = skcipher_request_ctx(req);

	rctx->left = 0;

	if (rctx->ext)
		kzfree(rctx->ext);
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

		if (err == -EINPROGRESS ||
		    (err == -EBUSY &&
		     req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
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

		if (err == -EINPROGRESS ||
		    (err == -EBUSY &&
		     req->base.flags & CRYPTO_TFM_REQ_MAY_BACKLOG))
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

int xts_crypt(struct blkcipher_desc *desc, struct scatterlist *sdst,
	      struct scatterlist *ssrc, unsigned int nbytes,
	      struct xts_crypt_req *req)
{
	const unsigned int bsize = XTS_BLOCK_SIZE;
	const unsigned int max_blks = req->tbuflen / bsize;
	struct blkcipher_walk walk;
	unsigned int nblocks;
	le128 *src, *dst, *t;
	le128 *t_buf = req->tbuf;
	int err, i;

	BUG_ON(max_blks < 1);

	blkcipher_walk_init(&walk, sdst, ssrc, nbytes);

	err = blkcipher_walk_virt(desc, &walk);
	nbytes = walk.nbytes;
	if (!nbytes)
		return err;

	nblocks = min(nbytes / bsize, max_blks);
	src = (le128 *)walk.src.virt.addr;
	dst = (le128 *)walk.dst.virt.addr;

	/* calculate first value of T */
	req->tweak_fn(req->tweak_ctx, (u8 *)&t_buf[0], walk.iv);

	i = 0;
	goto first;

	for (;;) {
		do {
			for (i = 0; i < nblocks; i++) {
				gf128mul_x_ble(&t_buf[i], t);
first:
				t = &t_buf[i];

				/* PP <- T xor P */
				le128_xor(dst + i, t, src + i);
			}

			/* CC <- E(Key2,PP) */
			req->crypt_fn(req->crypt_ctx, (u8 *)dst,
				      nblocks * bsize);

			/* C <- T xor CC */
			for (i = 0; i < nblocks; i++)
				le128_xor(dst + i, dst + i, &t_buf[i]);

			src += nblocks;
			dst += nblocks;
			nbytes -= nblocks * bsize;
			nblocks = min(nbytes / bsize, max_blks);
		} while (nblocks > 0);

		*(le128 *)walk.iv = *t;

		err = blkcipher_walk_done(desc, &walk, nbytes);
		nbytes = walk.nbytes;
		if (!nbytes)
			break;

		nblocks = min(nbytes / bsize, max_blks);
		src = (le128 *)walk.src.virt.addr;
		dst = (le128 *)walk.dst.virt.addr;
	}

	return err;
}
EXPORT_SYMBOL_GPL(xts_crypt);

static int init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_instance *inst = skcipher_alg_instance(tfm);
	struct xts_instance_ctx *ictx = skcipher_instance_ctx(inst);
	struct priv *ctx = crypto_skcipher_ctx(tfm);
	struct crypto_skcipher *child;
	struct crypto_cipher *tweak;

	child = crypto_spawn_skcipher(&ictx->spawn);
	if (IS_ERR(child))
		return PTR_ERR(child);

	ctx->child = child;

	tweak = crypto_alloc_cipher(ictx->name, 0, 0);
	if (IS_ERR(tweak)) {
		crypto_free_skcipher(ctx->child);
		return PTR_ERR(tweak);
	}

	ctx->tweak = tweak;

	crypto_skcipher_set_reqsize(tfm, crypto_skcipher_reqsize(child) +
					 sizeof(struct rctx));

	return 0;
}

static void exit_tfm(struct crypto_skcipher *tfm)
{
	struct priv *ctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(ctx->child);
	crypto_free_cipher(ctx->tweak);
}

static void free(struct skcipher_instance *inst)
{
	crypto_drop_skcipher(skcipher_instance_ctx(inst));
	kfree(inst);
}

static int create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct skcipher_instance *inst;
	struct crypto_attr_type *algt;
	struct xts_instance_ctx *ctx;
	struct skcipher_alg *alg;
	const char *cipher_name;
	u32 mask;
	int err;

	algt = crypto_get_attr_type(tb);
	if (IS_ERR(algt))
		return PTR_ERR(algt);

	if ((algt->type ^ CRYPTO_ALG_TYPE_SKCIPHER) & algt->mask)
		return -EINVAL;

	cipher_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(cipher_name))
		return PTR_ERR(cipher_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = skcipher_instance_ctx(inst);

	crypto_set_skcipher_spawn(&ctx->spawn, skcipher_crypto_instance(inst));

	mask = crypto_requires_off(algt->type, algt->mask,
				   CRYPTO_ALG_NEED_FALLBACK |
				   CRYPTO_ALG_ASYNC);

	err = crypto_grab_skcipher(&ctx->spawn, cipher_name, 0, mask);
	if (err == -ENOENT) {
		err = -ENAMETOOLONG;
		if (snprintf(ctx->name, CRYPTO_MAX_ALG_NAME, "ecb(%s)",
			     cipher_name) >= CRYPTO_MAX_ALG_NAME)
			goto err_free_inst;

		err = crypto_grab_skcipher(&ctx->spawn, ctx->name, 0, mask);
	}

	if (err)
		goto err_free_inst;

	alg = crypto_skcipher_spawn_alg(&ctx->spawn);

	err = -EINVAL;
	if (alg->base.cra_blocksize != XTS_BLOCK_SIZE)
		goto err_drop_spawn;

	if (crypto_skcipher_alg_ivsize(alg))
		goto err_drop_spawn;

	err = crypto_inst_setname(skcipher_crypto_instance(inst), "xts",
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

		len = strlcpy(ctx->name, cipher_name + 4, sizeof(ctx->name));
		if (len < 2 || len >= sizeof(ctx->name))
			goto err_drop_spawn;

		if (ctx->name[len - 1] != ')')
			goto err_drop_spawn;

		ctx->name[len - 1] = 0;

		if (snprintf(inst->alg.base.cra_name, CRYPTO_MAX_ALG_NAME,
			     "xts(%s)", ctx->name) >= CRYPTO_MAX_ALG_NAME)
			return -ENAMETOOLONG;
	} else
		goto err_drop_spawn;

	inst->alg.base.cra_flags = alg->base.cra_flags & CRYPTO_ALG_ASYNC;
	inst->alg.base.cra_priority = alg->base.cra_priority;
	inst->alg.base.cra_blocksize = XTS_BLOCK_SIZE;
	inst->alg.base.cra_alignmask = alg->base.cra_alignmask |
				       (__alignof__(u64) - 1);

	inst->alg.ivsize = XTS_BLOCK_SIZE;
	inst->alg.min_keysize = crypto_skcipher_alg_min_keysize(alg) * 2;
	inst->alg.max_keysize = crypto_skcipher_alg_max_keysize(alg) * 2;

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
	crypto_drop_skcipher(&ctx->spawn);
err_free_inst:
	kfree(inst);
	goto out;
}

static struct crypto_template crypto_tmpl = {
	.name = "xts",
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
MODULE_DESCRIPTION("XTS block cipher mode");
MODULE_ALIAS_CRYPTO("xts");
