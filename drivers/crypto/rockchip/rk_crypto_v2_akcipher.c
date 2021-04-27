// SPDX-License-Identifier: GPL-2.0
/*
 * RSA acceleration support for Rockchip crypto v2
 *
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd.
 *
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 * Some ideas are from marvell/cesa.c and s5p-sss.c driver.
 */

#include <linux/slab.h>

#include "rk_crypto_core.h"
#include "rk_crypto_v2.h"
#include "rk_crypto_v2_reg.h"

#define BG_WORDS2BYTES(words)	((words) * sizeof(u32))
#define BG_BYTES2WORDS(bytes)	(((bytes) + sizeof(u32) - 1) / sizeof(u32))

static void rk_rsa_adjust_rsa_key(struct rsa_key *key)
{
	if (key->n_sz && key->n && !key->n[0]) {
		key->n++;
		key->n_sz--;
	}

	if (key->e_sz && key->e && !key->e[0]) {
		key->e++;
		key->e_sz--;
	}

	if (key->d_sz && key->d && !key->d[0]) {
		key->d++;
		key->d_sz--;
	}
}

static void rk_rsa_clear_ctx(struct rk_rsa_ctx *ctx)
{
	/* Free the old key if any */
	rk_bn_free(ctx->n);
	ctx->n = NULL;

	rk_bn_free(ctx->e);
	ctx->e = NULL;

	rk_bn_free(ctx->d);
	ctx->d = NULL;
}

static int rk_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			 unsigned int keylen, bool private)
{
	struct rk_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsa_key rsa_key;
	int ret = -ENOMEM;

	rk_rsa_clear_ctx(ctx);

	memset(&rsa_key, 0x00, sizeof(rsa_key));

	if (private)
		ret = rsa_parse_priv_key(&rsa_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&rsa_key, key, keylen);

	if (ret < 0)
		goto error;

	rk_rsa_adjust_rsa_key(&rsa_key);

	ctx->n = rk_bn_alloc(rsa_key.n_sz);
	if (!ctx->n)
		goto error;

	ctx->e = rk_bn_alloc(rsa_key.e_sz);
	if (!ctx->e)
		goto error;

	rk_bn_set_data(ctx->n, rsa_key.n, rsa_key.n_sz, RK_BG_BIG_ENDIAN);
	rk_bn_set_data(ctx->e, rsa_key.e, rsa_key.e_sz, RK_BG_BIG_ENDIAN);

	CRYPTO_DUMPHEX("n = ", ctx->n->data, BG_WORDS2BYTES(ctx->n->n_words));
	CRYPTO_DUMPHEX("e = ", ctx->e->data, BG_WORDS2BYTES(ctx->e->n_words));

	if (private) {
		ctx->d = rk_bn_alloc(rsa_key.d_sz);
		if (!ctx->d)
			goto error;

		rk_bn_set_data(ctx->d, rsa_key.d, rsa_key.d_sz, RK_BG_BIG_ENDIAN);

		CRYPTO_DUMPHEX("d = ", ctx->d->data, BG_WORDS2BYTES(ctx->d->n_words));
	}

	return 0;
error:
	rk_rsa_clear_ctx(ctx);
	return ret;
}

static unsigned int rk_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct rk_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	CRYPTO_TRACE();

	return rk_bn_get_size(ctx->n);
}

static int rk_rsa_setpubkey(struct crypto_akcipher *tfm, const void *key,
			    unsigned int keylen)
{
	CRYPTO_TRACE();

	return rk_rsa_setkey(tfm, key, keylen, false);
}

static int rk_rsa_setprivkey(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen)
{
	CRYPTO_TRACE();

	return rk_rsa_setkey(tfm, key, keylen, true);
}

static int rk_rsa_calc(struct akcipher_request *req, bool encypt)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct rk_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rk_bignum *in = NULL, *out = NULL;
	u32 key_byte_size;
	u8 *tmp_buf = NULL;
	int ret = -ENOMEM;

	CRYPTO_TRACE();

	if (unlikely(!ctx->n || !ctx->e))
		return -EINVAL;

	if (!encypt && !ctx->d)
		return -EINVAL;

	key_byte_size = rk_bn_get_size(ctx->n);

	if (req->dst_len < key_byte_size) {
		req->dst_len = key_byte_size;
		return -EOVERFLOW;
	}

	in = rk_bn_alloc(key_byte_size);
	if (!in)
		goto exit;

	out = rk_bn_alloc(key_byte_size);
	if (!in)
		goto exit;

	tmp_buf = kzalloc(key_byte_size, GFP_KERNEL);
	if (!tmp_buf)
		goto exit;

	if (!sg_copy_to_buffer(req->src, sg_nents(req->src), tmp_buf, req->src_len)) {
		dev_err(ctx->rk_dev->dev, "[%s:%d] sg copy err\n",
			__func__, __LINE__);
		ret =  -EINVAL;
		goto exit;
	}

	ret = rk_bn_set_data(in, tmp_buf, req->src_len, RK_BG_BIG_ENDIAN);
	if (ret)
		goto exit;

	CRYPTO_DUMPHEX("in = ", in->data, BG_WORDS2BYTES(in->n_words));

	if (encypt)
		ret = rk_pka_expt_mod(in, ctx->e, ctx->n, out);
	else
		ret = rk_pka_expt_mod(in, ctx->d, ctx->n, out);
	if (ret)
		goto exit;

	CRYPTO_DUMPHEX("out = ", out->data, BG_WORDS2BYTES(out->n_words));

	ret = rk_bn_get_data(out, tmp_buf, key_byte_size, RK_BG_BIG_ENDIAN);
	if (ret)
		goto exit;

	CRYPTO_DUMPHEX("tmp_buf = ", tmp_buf, key_byte_size);

	if (!sg_copy_from_buffer(req->dst, sg_nents(req->dst), tmp_buf, key_byte_size)) {
		dev_err(ctx->rk_dev->dev, "[%s:%d] sg copy err\n",
			__func__, __LINE__);
		ret =  -EINVAL;
		goto exit;
	}

	req->dst_len = key_byte_size;

	CRYPTO_TRACE("ret = %d", ret);
exit:
	kfree(tmp_buf);

	rk_bn_free(in);
	rk_bn_free(out);

	return ret;
}

static int rk_rsa_enc(struct akcipher_request *req)
{
	CRYPTO_TRACE();

	return rk_rsa_calc(req, true);
}

static int rk_rsa_dec(struct akcipher_request *req)
{
	CRYPTO_TRACE();

	return rk_rsa_calc(req, false);
}

static int rk_rsa_start(struct rk_crypto_dev *rk_dev)
{
	CRYPTO_TRACE();

	return -ENOSYS;
}

static int rk_rsa_crypto_rx(struct rk_crypto_dev *rk_dev)
{
	CRYPTO_TRACE();

	return -ENOSYS;
}

static void rk_rsa_complete(struct crypto_async_request *base, int err)
{
	if (base->complete)
		base->complete(base, err);
}

static int rk_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct rk_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *alg = __crypto_akcipher_alg(tfm->base.__crt_alg);
	struct rk_crypto_algt *algt;
	struct rk_crypto_dev *rk_dev;
	struct rk_alg_ctx *alg_ctx = &ctx->algs_ctx;

	CRYPTO_TRACE();

	memset(ctx, 0x00, sizeof(*ctx));

	algt = container_of(alg, struct rk_crypto_algt, alg.asym);
	rk_dev = algt->rk_dev;

	if (!rk_dev->request_crypto)
		return -EFAULT;

	rk_dev->request_crypto(rk_dev, "rsa");

	alg_ctx->align_size     = crypto_tfm_alg_alignmask(&tfm->base) + 1;

	alg_ctx->ops.start      = rk_rsa_start;
	alg_ctx->ops.update     = rk_rsa_crypto_rx;
	alg_ctx->ops.complete   = rk_rsa_complete;

	ctx->rk_dev = rk_dev;

	rk_pka_set_crypto_base(ctx->rk_dev->pka_reg);

	return 0;
}

static void rk_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct rk_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	CRYPTO_TRACE();

	rk_rsa_clear_ctx(ctx);

	ctx->rk_dev->release_crypto(ctx->rk_dev, "rsa");
}

struct rk_crypto_algt rk_v2_asym_rsa = {
	.name = "rsa",
	.type = ALG_TYPE_ASYM,
	.alg.asym = {
		.encrypt = rk_rsa_enc,
		.decrypt = rk_rsa_dec,
		.sign = rk_rsa_dec,
		.verify = rk_rsa_enc,
		.set_pub_key = rk_rsa_setpubkey,
		.set_priv_key = rk_rsa_setprivkey,
		.max_size = rk_rsa_max_size,
		.init = rk_rsa_init_tfm,
		.exit = rk_rsa_exit_tfm,
		.reqsize = 64,
		.base = {
			.cra_name = "rsa",
			.cra_driver_name = "rsa-rk",
			.cra_priority = RK_CRYPTO_PRIORITY,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct rk_rsa_ctx),
		},
	},
};

