/*  Diffie-Hellman Key Agreement Method [RFC2631]
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <crypto/internal/kpp.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <linux/mpi.h>

struct dh_ctx {
	MPI p;
	MPI g;
	MPI xa;
};

static inline void dh_clear_params(struct dh_ctx *ctx)
{
	mpi_free(ctx->p);
	mpi_free(ctx->g);
	ctx->p = NULL;
	ctx->g = NULL;
}

static void dh_free_ctx(struct dh_ctx *ctx)
{
	dh_clear_params(ctx);
	mpi_free(ctx->xa);
	ctx->xa = NULL;
}

/*
 * If base is g we compute the public key
 *	ya = g^xa mod p; [RFC2631 sec 2.1.1]
 * else if base if the counterpart public key we compute the shared secret
 *	ZZ = yb^xa mod p; [RFC2631 sec 2.1.1]
 */
static int _compute_val(const struct dh_ctx *ctx, MPI base, MPI val)
{
	/* val = base^xa mod p */
	return mpi_powm(val, base, ctx->xa, ctx->p);
}

static inline struct dh_ctx *dh_get_ctx(struct crypto_kpp *tfm)
{
	return kpp_tfm_ctx(tfm);
}

static int dh_check_params_length(unsigned int p_len)
{
	return (p_len < 1536) ? -EINVAL : 0;
}

static int dh_set_params(struct dh_ctx *ctx, struct dh *params)
{
	if (unlikely(!params->p || !params->g))
		return -EINVAL;

	if (dh_check_params_length(params->p_size << 3))
		return -EINVAL;

	ctx->p = mpi_read_raw_data(params->p, params->p_size);
	if (!ctx->p)
		return -EINVAL;

	ctx->g = mpi_read_raw_data(params->g, params->g_size);
	if (!ctx->g) {
		mpi_free(ctx->p);
		return -EINVAL;
	}

	return 0;
}

static int dh_set_secret(struct crypto_kpp *tfm, void *buf, unsigned int len)
{
	struct dh_ctx *ctx = dh_get_ctx(tfm);
	struct dh params;

	if (crypto_dh_decode_key(buf, len, &params) < 0)
		return -EINVAL;

	if (dh_set_params(ctx, &params) < 0)
		return -EINVAL;

	ctx->xa = mpi_read_raw_data(params.key, params.key_size);
	if (!ctx->xa) {
		dh_clear_params(ctx);
		return -EINVAL;
	}

	return 0;
}

static int dh_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct dh_ctx *ctx = dh_get_ctx(tfm);
	MPI base, val = mpi_alloc(0);
	int ret = 0;
	int sign;

	if (!val)
		return -ENOMEM;

	if (unlikely(!ctx->xa)) {
		ret = -EINVAL;
		goto err_free_val;
	}

	if (req->src) {
		base = mpi_read_raw_from_sgl(req->src, req->src_len);
		if (!base) {
			ret = EINVAL;
			goto err_free_val;
		}
	} else {
		base = ctx->g;
	}

	ret = _compute_val(ctx, base, val);
	if (ret)
		goto err_free_base;

	ret = mpi_write_to_sgl(val, req->dst, &req->dst_len, &sign);
	if (ret)
		goto err_free_base;

	if (sign < 0)
		ret = -EBADMSG;
err_free_base:
	if (req->src)
		mpi_free(base);
err_free_val:
	mpi_free(val);
	return ret;
}

static int dh_max_size(struct crypto_kpp *tfm)
{
	struct dh_ctx *ctx = dh_get_ctx(tfm);

	return mpi_get_size(ctx->p);
}

static void dh_exit_tfm(struct crypto_kpp *tfm)
{
	struct dh_ctx *ctx = dh_get_ctx(tfm);

	dh_free_ctx(ctx);
}

static struct kpp_alg dh = {
	.set_secret = dh_set_secret,
	.generate_public_key = dh_compute_value,
	.compute_shared_secret = dh_compute_value,
	.max_size = dh_max_size,
	.exit = dh_exit_tfm,
	.base = {
		.cra_name = "dh",
		.cra_driver_name = "dh-generic",
		.cra_priority = 100,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct dh_ctx),
	},
};

static int dh_init(void)
{
	return crypto_register_kpp(&dh);
}

static void dh_exit(void)
{
	crypto_unregister_kpp(&dh);
}

module_init(dh_init);
module_exit(dh_exit);
MODULE_ALIAS_CRYPTO("dh");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DH generic algorithm");
