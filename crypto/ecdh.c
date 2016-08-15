/* ECDH key-agreement protocol
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvator Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <crypto/internal/kpp.h>
#include <crypto/kpp.h>
#include <crypto/ecdh.h>
#include <linux/scatterlist.h>
#include "ecc.h"

struct ecdh_ctx {
	unsigned int curve_id;
	unsigned int ndigits;
	u64 private_key[ECC_MAX_DIGITS];
	u64 public_key[2 * ECC_MAX_DIGITS];
	u64 shared_secret[ECC_MAX_DIGITS];
};

static inline struct ecdh_ctx *ecdh_get_ctx(struct crypto_kpp *tfm)
{
	return kpp_tfm_ctx(tfm);
}

static unsigned int ecdh_supported_curve(unsigned int curve_id)
{
	switch (curve_id) {
	case ECC_CURVE_NIST_P192: return 3;
	case ECC_CURVE_NIST_P256: return 4;
	default: return 0;
	}
}

static int ecdh_set_secret(struct crypto_kpp *tfm, void *buf, unsigned int len)
{
	struct ecdh_ctx *ctx = ecdh_get_ctx(tfm);
	struct ecdh params;
	unsigned int ndigits;

	if (crypto_ecdh_decode_key(buf, len, &params) < 0)
		return -EINVAL;

	ndigits = ecdh_supported_curve(params.curve_id);
	if (!ndigits)
		return -EINVAL;

	ctx->curve_id = params.curve_id;
	ctx->ndigits = ndigits;

	if (ecc_is_key_valid(ctx->curve_id, ctx->ndigits,
			     (const u8 *)params.key, params.key_size) < 0)
		return -EINVAL;

	memcpy(ctx->private_key, params.key, params.key_size);

	return 0;
}

static int ecdh_compute_value(struct kpp_request *req)
{
	int ret = 0;
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct ecdh_ctx *ctx = ecdh_get_ctx(tfm);
	size_t copied, nbytes;
	void *buf;

	nbytes = ctx->ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	if (req->src) {
		copied = sg_copy_to_buffer(req->src, 1, ctx->public_key,
					   2 * nbytes);
		if (copied != 2 * nbytes)
			return -EINVAL;

		ret = crypto_ecdh_shared_secret(ctx->curve_id, ctx->ndigits,
					 (const u8 *)ctx->private_key, nbytes,
					 (const u8 *)ctx->public_key, 2 * nbytes,
					 (u8 *)ctx->shared_secret, nbytes);

		buf = ctx->shared_secret;
	} else {
		ret = ecdh_make_pub_key(ctx->curve_id, ctx->ndigits,
					(const u8 *)ctx->private_key, nbytes,
					(u8 *)ctx->public_key,
					sizeof(ctx->public_key));
		buf = ctx->public_key;
		/* Public part is a point thus it has both coordinates */
		nbytes *= 2;
	}

	if (ret < 0)
		return ret;

	copied = sg_copy_from_buffer(req->dst, 1, buf, nbytes);
	if (copied != nbytes)
		return -EINVAL;

	return ret;
}

static int ecdh_max_size(struct crypto_kpp *tfm)
{
	struct ecdh_ctx *ctx = ecdh_get_ctx(tfm);
	int nbytes = ctx->ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	/* Public key is made of two coordinates */
	return 2 * nbytes;
}

static void no_exit_tfm(struct crypto_kpp *tfm)
{
	return;
}

static struct kpp_alg ecdh = {
	.set_secret = ecdh_set_secret,
	.generate_public_key = ecdh_compute_value,
	.compute_shared_secret = ecdh_compute_value,
	.max_size = ecdh_max_size,
	.exit = no_exit_tfm,
	.base = {
		.cra_name = "ecdh",
		.cra_driver_name = "ecdh-generic",
		.cra_priority = 100,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct ecdh_ctx),
	},
};

static int ecdh_init(void)
{
	return crypto_register_kpp(&ecdh);
}

static void ecdh_exit(void)
{
	crypto_unregister_kpp(&ecdh);
}

module_init(ecdh_init);
module_exit(ecdh_exit);
MODULE_ALIAS_CRYPTO("ecdh");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ECDH generic algorithm");
