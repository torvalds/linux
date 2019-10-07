// SPDX-License-Identifier: GPL-2.0-or-later
/* XTS: as defined in IEEE1619/D16
 *	http://grouper.ieee.org/groups/1619/email/pdf00086.pdf
 *
 * Copyright (c) 2007 Rik Snel <rsnel@cube.dyndns.org>
 *
 * Based on ecb.c
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
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

struct priv {
	struct crypto_skcipher *child;
	struct crypto_cipher *tweak;
};

struct xts_instance_ctx {
	struct crypto_skcipher_spawn spawn;
	char name[CRYPTO_MAX_ALG_NAME];
};

struct rctx {
	le128 t;
	struct scatterlist *tail;
	struct scatterlist sg[2];
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

/*
 * We compute the tweak masks twice (both before and after the ECB encryption or
 * decryption) to avoid having to allocate a temporary buffer and/or make
 * mutliple calls to the 'ecb(..)' instance, which usually would be slower than
 * just doing the gf128mul_x_ble() calls again.
 */
static int xor_tweak(struct skcipher_request *req, bool second_pass, bool enc)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const bool cts = (req->cryptlen % XTS_BLOCK_SIZE);
	const int bs = XTS_BLOCK_SIZE;
	struct skcipher_walk w;
	le128 t = rctx->t;
	int err;

	if (second_pass) {
		req = &rctx->subreq;
		/* set to our TFM to enforce correct alignment: */
		skcipher_request_set_tfm(req, tfm);
	}
	err = skcipher_walk_virt(&w, req, false);

	while (w.nbytes) {
		unsigned int avail = w.nbytes;
		le128 *wsrc;
		le128 *wdst;

		wsrc = w.src.virt.addr;
		wdst = w.dst.virt.addr;

		do {
			if (unlikely(cts) &&
			    w.total - w.nbytes + avail < 2 * XTS_BLOCK_SIZE) {
				if (!enc) {
					if (second_pass)
						rctx->t = t;
					gf128mul_x_ble(&t, &t);
				}
				le128_xor(wdst, &t, wsrc);
				if (enc && second_pass)
					gf128mul_x_ble(&rctx->t, &t);
				skcipher_walk_done(&w, avail - bs);
				return 0;
			}

			le128_xor(wdst++, &t, wsrc++);
			gf128mul_x_ble(&t, &t);
		} while ((avail -= bs) >= bs);

		err = skcipher_walk_done(&w, avail);
	}

	return err;
}

static int xor_tweak_pre(struct skcipher_request *req, bool enc)
{
	return xor_tweak(req, false, enc);
}

static int xor_tweak_post(struct skcipher_request *req, bool enc)
{
	return xor_tweak(req, true, enc);
}

static void cts_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;
	le128 b;

	if (!err) {
		struct rctx *rctx = skcipher_request_ctx(req);

		scatterwalk_map_and_copy(&b, rctx->tail, 0, XTS_BLOCK_SIZE, 0);
		le128_xor(&b, &rctx->t, &b);
		scatterwalk_map_and_copy(&b, rctx->tail, 0, XTS_BLOCK_SIZE, 1);
	}

	skcipher_request_complete(req, err);
}

static int cts_final(struct skcipher_request *req,
		     int (*crypt)(struct skcipher_request *req))
{
	struct priv *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	int offset = req->cryptlen & ~(XTS_BLOCK_SIZE - 1);
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq = &rctx->subreq;
	int tail = req->cryptlen % XTS_BLOCK_SIZE;
	le128 b[2];
	int err;

	rctx->tail = scatterwalk_ffwd(rctx->sg, req->dst,
				      offset - XTS_BLOCK_SIZE);

	scatterwalk_map_and_copy(b, rctx->tail, 0, XTS_BLOCK_SIZE, 0);
	memcpy(b + 1, b, tail);
	scatterwalk_map_and_copy(b, req->src, offset, tail, 0);

	le128_xor(b, &rctx->t, b);

	scatterwalk_map_and_copy(b, rctx->tail, 0, XTS_BLOCK_SIZE + tail, 1);

	skcipher_request_set_tfm(subreq, ctx->child);
	skcipher_request_set_callback(subreq, req->base.flags, cts_done, req);
	skcipher_request_set_crypt(subreq, rctx->tail, rctx->tail,
				   XTS_BLOCK_SIZE, NULL);

	err = crypt(subreq);
	if (err)
		return err;

	scatterwalk_map_and_copy(b, rctx->tail, 0, XTS_BLOCK_SIZE, 0);
	le128_xor(b, &rctx->t, b);
	scatterwalk_map_and_copy(b, rctx->tail, 0, XTS_BLOCK_SIZE, 1);

	return 0;
}

static void encrypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;

	if (!err) {
		struct rctx *rctx = skcipher_request_ctx(req);

		rctx->subreq.base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
		err = xor_tweak_post(req, true);

		if (!err && unlikely(req->cryptlen % XTS_BLOCK_SIZE)) {
			err = cts_final(req, crypto_skcipher_encrypt);
			if (err == -EINPROGRESS)
				return;
		}
	}

	skcipher_request_complete(req, err);
}

static void decrypt_done(struct crypto_async_request *areq, int err)
{
	struct skcipher_request *req = areq->data;

	if (!err) {
		struct rctx *rctx = skcipher_request_ctx(req);

		rctx->subreq.base.flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
		err = xor_tweak_post(req, false);

		if (!err && unlikely(req->cryptlen % XTS_BLOCK_SIZE)) {
			err = cts_final(req, crypto_skcipher_decrypt);
			if (err == -EINPROGRESS)
				return;
		}
	}

	skcipher_request_complete(req, err);
}

static int init_crypt(struct skcipher_request *req, crypto_completion_t compl)
{
	struct priv *ctx = crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq = &rctx->subreq;

	if (req->cryptlen < XTS_BLOCK_SIZE)
		return -EINVAL;

	skcipher_request_set_tfm(subreq, ctx->child);
	skcipher_request_set_callback(subreq, req->base.flags, compl, req);
	skcipher_request_set_crypt(subreq, req->dst, req->dst,
				   req->cryptlen & ~(XTS_BLOCK_SIZE - 1), NULL);

	/* calculate first value of T */
	crypto_cipher_encrypt_one(ctx->tweak, (u8 *)&rctx->t, req->iv);

	return 0;
}

static int encrypt(struct skcipher_request *req)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq = &rctx->subreq;
	int err;

	err = init_crypt(req, encrypt_done) ?:
	      xor_tweak_pre(req, true) ?:
	      crypto_skcipher_encrypt(subreq) ?:
	      xor_tweak_post(req, true);

	if (err || likely((req->cryptlen % XTS_BLOCK_SIZE) == 0))
		return err;

	return cts_final(req, crypto_skcipher_encrypt);
}

static int decrypt(struct skcipher_request *req)
{
	struct rctx *rctx = skcipher_request_ctx(req);
	struct skcipher_request *subreq = &rctx->subreq;
	int err;

	err = init_crypt(req, decrypt_done) ?:
	      xor_tweak_pre(req, false) ?:
	      crypto_skcipher_decrypt(subreq) ?:
	      xor_tweak_post(req, false);

	if (err || likely((req->cryptlen % XTS_BLOCK_SIZE) == 0))
		return err;

	return cts_final(req, crypto_skcipher_decrypt);
}

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
			     "xts(%s)", ctx->name) >= CRYPTO_MAX_ALG_NAME) {
			err = -ENAMETOOLONG;
			goto err_drop_spawn;
		}
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

subsys_initcall(crypto_module_init);
module_exit(crypto_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XTS block cipher mode");
MODULE_ALIAS_CRYPTO("xts");
