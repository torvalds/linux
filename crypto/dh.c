// SPDX-License-Identifier: GPL-2.0-or-later
/*  Diffie-Hellman Key Agreement Method [RFC2631]
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */

#include <linux/fips.h>
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
	if (fips_enabled)
		return (p_len < 2048) ? -EINVAL : 0;

	return (p_len < 1536) ? -EINVAL : 0;
}

static int dh_set_params(struct dh_ctx *ctx, struct dh *params)
{
	if (dh_check_params_length(params->p_size << 3))
		return -EINVAL;

	ctx->p = mpi_read_raw_data(params->p, params->p_size);
	if (!ctx->p)
		return -EINVAL;

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

	if (fips_enabled) {
		/* SP800-56A rev3 5.7.1.1 check: Validation of shared secret */
		if (req->src) {
			MPI pone;

			/* z <= 1 */
			if (mpi_cmp_ui(val, 1) < 1) {
				ret = -EBADMSG;
				goto err_free_base;
			}

			/* z == p - 1 */
			pone = mpi_alloc(0);

			if (!pone) {
				ret = -ENOMEM;
				goto err_free_base;
			}

			ret = mpi_sub_ui(pone, ctx->p, 1);
			if (!ret && !mpi_cmp(pone, val))
				ret = -EBADMSG;

			mpi_free(pone);

			if (ret)
				goto err_free_base;

		/* SP800-56A rev 3 5.6.2.1.3 key check */
		} else {
			if (dh_is_pubkey_valid(ctx, val)) {
				ret = -EAGAIN;
				goto err_free_val;
			}
		}
	}

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


struct dh_safe_prime {
	unsigned int max_strength;
	unsigned int p_size;
	const char *p;
};

static const char safe_prime_g[]  = { 2 };

struct dh_safe_prime_instance_ctx {
	struct crypto_kpp_spawn dh_spawn;
	const struct dh_safe_prime *safe_prime;
};

struct dh_safe_prime_tfm_ctx {
	struct crypto_kpp *dh_tfm;
};

static void dh_safe_prime_free_instance(struct kpp_instance *inst)
{
	struct dh_safe_prime_instance_ctx *ctx = kpp_instance_ctx(inst);

	crypto_drop_kpp(&ctx->dh_spawn);
	kfree(inst);
}

static inline struct dh_safe_prime_instance_ctx *dh_safe_prime_instance_ctx(
	struct crypto_kpp *tfm)
{
	return kpp_instance_ctx(kpp_alg_instance(tfm));
}

static inline struct kpp_alg *dh_safe_prime_dh_alg(
	struct dh_safe_prime_tfm_ctx *ctx)
{
	return crypto_kpp_alg(ctx->dh_tfm);
}

static int dh_safe_prime_init_tfm(struct crypto_kpp *tfm)
{
	struct dh_safe_prime_instance_ctx *inst_ctx =
		dh_safe_prime_instance_ctx(tfm);
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);

	tfm_ctx->dh_tfm = crypto_spawn_kpp(&inst_ctx->dh_spawn);
	if (IS_ERR(tfm_ctx->dh_tfm))
		return PTR_ERR(tfm_ctx->dh_tfm);

	return 0;
}

static void dh_safe_prime_exit_tfm(struct crypto_kpp *tfm)
{
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);

	crypto_free_kpp(tfm_ctx->dh_tfm);
}

static int dh_safe_prime_set_secret(struct crypto_kpp *tfm, const void *buffer,
				    unsigned int len)
{
	struct dh_safe_prime_instance_ctx *inst_ctx =
		dh_safe_prime_instance_ctx(tfm);
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);
	struct dh params;
	void *buf;
	unsigned int buf_size;
	int err;

	err = __crypto_dh_decode_key(buffer, len, &params);
	if (err)
		return err;

	if (params.p_size || params.g_size)
		return -EINVAL;

	params.p = inst_ctx->safe_prime->p;
	params.p_size = inst_ctx->safe_prime->p_size;
	params.g = safe_prime_g;
	params.g_size = sizeof(safe_prime_g);

	buf_size = crypto_dh_key_len(&params);
	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	err = crypto_dh_encode_key(buf, buf_size, &params);
	if (err)
		goto out;

	err = crypto_kpp_set_secret(tfm_ctx->dh_tfm, buf, buf_size);
out:
	kfree_sensitive(buf);
	return err;
}

static void dh_safe_prime_complete_req(struct crypto_async_request *dh_req,
				       int err)
{
	struct kpp_request *req = dh_req->data;

	kpp_request_complete(req, err);
}

static struct kpp_request *dh_safe_prime_prepare_dh_req(struct kpp_request *req)
{
	struct dh_safe_prime_tfm_ctx *tfm_ctx =
		kpp_tfm_ctx(crypto_kpp_reqtfm(req));
	struct kpp_request *dh_req = kpp_request_ctx(req);

	kpp_request_set_tfm(dh_req, tfm_ctx->dh_tfm);
	kpp_request_set_callback(dh_req, req->base.flags,
				 dh_safe_prime_complete_req, req);

	kpp_request_set_input(dh_req, req->src, req->src_len);
	kpp_request_set_output(dh_req, req->dst, req->dst_len);

	return dh_req;
}

static int dh_safe_prime_generate_public_key(struct kpp_request *req)
{
	struct kpp_request *dh_req = dh_safe_prime_prepare_dh_req(req);

	return crypto_kpp_generate_public_key(dh_req);
}

static int dh_safe_prime_compute_shared_secret(struct kpp_request *req)
{
	struct kpp_request *dh_req = dh_safe_prime_prepare_dh_req(req);

	return crypto_kpp_compute_shared_secret(dh_req);
}

static unsigned int dh_safe_prime_max_size(struct crypto_kpp *tfm)
{
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);

	return crypto_kpp_maxsize(tfm_ctx->dh_tfm);
}

static int __maybe_unused __dh_safe_prime_create(
	struct crypto_template *tmpl, struct rtattr **tb,
	const struct dh_safe_prime *safe_prime)
{
	struct kpp_instance *inst;
	struct dh_safe_prime_instance_ctx *ctx;
	const char *dh_name;
	struct kpp_alg *dh_alg;
	u32 mask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_KPP, &mask);
	if (err)
		return err;

	dh_name = crypto_attr_alg_name(tb[1]);
	if (IS_ERR(dh_name))
		return PTR_ERR(dh_name);

	inst = kzalloc(sizeof(*inst) + sizeof(*ctx), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	ctx = kpp_instance_ctx(inst);

	err = crypto_grab_kpp(&ctx->dh_spawn, kpp_crypto_instance(inst),
			      dh_name, 0, mask);
	if (err)
		goto err_free_inst;

	err = -EINVAL;
	dh_alg = crypto_spawn_kpp_alg(&ctx->dh_spawn);
	if (strcmp(dh_alg->base.cra_name, "dh"))
		goto err_free_inst;

	ctx->safe_prime = safe_prime;

	err = crypto_inst_setname(kpp_crypto_instance(inst),
				  tmpl->name, &dh_alg->base);
	if (err)
		goto err_free_inst;

	inst->alg.set_secret = dh_safe_prime_set_secret;
	inst->alg.generate_public_key = dh_safe_prime_generate_public_key;
	inst->alg.compute_shared_secret = dh_safe_prime_compute_shared_secret;
	inst->alg.max_size = dh_safe_prime_max_size;
	inst->alg.init = dh_safe_prime_init_tfm;
	inst->alg.exit = dh_safe_prime_exit_tfm;
	inst->alg.reqsize = sizeof(struct kpp_request) + dh_alg->reqsize;
	inst->alg.base.cra_priority = dh_alg->base.cra_priority;
	inst->alg.base.cra_module = THIS_MODULE;
	inst->alg.base.cra_ctxsize = sizeof(struct dh_safe_prime_tfm_ctx);

	inst->free = dh_safe_prime_free_instance;

	err = kpp_register_instance(tmpl, inst);
	if (err)
		goto err_free_inst;

	return 0;

err_free_inst:
	dh_safe_prime_free_instance(inst);

	return err;
}

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
