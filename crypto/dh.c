// SPDX-License-Identifier: GPL-2.0-or-later
/*  Diffie-Hellman Key Agreement Method [RFC2631]
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */

#include <linux/module.h>
#include <crypto/internal/kpp.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <linux/mpi.h>

struct dh_ctx {
	MPI p;	/* Value is guaranteed to be set. */
	MPI q;	/* Value is optional. */
	MPI g;	/* Value is guaranteed to be set. */
	MPI xa;	/* Value is guaranteed to be set. */
};

static void dh_clear_ctx(struct dh_ctx *ctx)
{
	mpi_free(ctx->p);
	mpi_free(ctx->q);
	mpi_free(ctx->g);
	mpi_free(ctx->xa);
	memset(ctx, 0, sizeof(*ctx));
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
	if (dh_check_params_length(params->p_size << 3))
		return -EINVAL;

	ctx->p = mpi_read_raw_data(params->p, params->p_size);
	if (!ctx->p)
		return -EINVAL;

	if (params->q && params->q_size) {
		ctx->q = mpi_read_raw_data(params->q, params->q_size);
		if (!ctx->q)
			return -EINVAL;
	}

	ctx->g = mpi_read_raw_data(params->g, params->g_size);
	if (!ctx->g)
		return -EINVAL;

	return 0;
}

static int dh_set_secret(struct crypto_kpp *tfm, const void *buf,
			 unsigned int len)
{
	struct dh_ctx *ctx = dh_get_ctx(tfm);
	struct dh params;

	/* Free the old MPI key if any */
	dh_clear_ctx(ctx);

	if (crypto_dh_decode_key(buf, len, &params) < 0)
		goto err_clear_ctx;

	if (dh_set_params(ctx, &params) < 0)
		goto err_clear_ctx;

	ctx->xa = mpi_read_raw_data(params.key, params.key_size);
	if (!ctx->xa)
		goto err_clear_ctx;

	return 0;

err_clear_ctx:
	dh_clear_ctx(ctx);
	return -EINVAL;
}

/*
 * SP800-56A public key verification:
 *
 * * If Q is provided as part of the domain paramenters, a full validation
 *   according to SP800-56A section 5.6.2.3.1 is performed.
 *
 * * If Q is not provided, a partial validation according to SP800-56A section
 *   5.6.2.3.2 is performed.
 */
static int dh_is_pubkey_valid(struct dh_ctx *ctx, MPI y)
{
	if (unlikely(!ctx->p))
		return -EINVAL;

	/*
	 * Step 1: Verify that 2 <= y <= p - 2.
	 *
	 * The upper limit check is actually y < p instead of y < p - 1
	 * as the mpi_sub_ui function is yet missing.
	 */
	if (mpi_cmp_ui(y, 1) < 1 || mpi_cmp(y, ctx->p) >= 0)
		return -EINVAL;

	/* Step 2: Verify that 1 = y^q mod p */
	if (ctx->q) {
		MPI val = mpi_alloc(0);
		int ret;

		if (!val)
			return -ENOMEM;

		ret = mpi_powm(val, y, ctx->q, ctx->p);

		if (ret) {
			mpi_free(val);
			return ret;
		}

		ret = mpi_cmp_ui(val, 1);

		mpi_free(val);

		if (ret != 0)
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
			ret = -EINVAL;
			goto err_free_val;
		}
		ret = dh_is_pubkey_valid(ctx, base);
		if (ret)
			goto err_free_base;
	} else {
		base = ctx->g;
	}

	ret = _compute_val(ctx, base, val);
	if (ret)
		goto err_free_base;

	ret = mpi_write_to_sgl(val, req->dst, req->dst_len, &sign);
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

static unsigned int dh_max_size(struct crypto_kpp *tfm)
{
	struct dh_ctx *ctx = dh_get_ctx(tfm);

	return mpi_get_size(ctx->p);
}

static void dh_exit_tfm(struct crypto_kpp *tfm)
{
	struct dh_ctx *ctx = dh_get_ctx(tfm);

	dh_clear_ctx(ctx);
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

subsys_initcall(dh_init);
module_exit(dh_exit);
MODULE_ALIAS_CRYPTO("dh");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DH generic algorithm");
