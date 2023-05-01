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
#include <crypto/rng.h>
#include <linux/mpi.h>

struct dh_ctx {
	MPI p;	/* Value is guaranteed to be set. */
	MPI g;	/* Value is guaranteed to be set. */
	MPI xa;	/* Value is guaranteed to be set. */
};

static void dh_clear_ctx(struct dh_ctx *ctx)
{
	mpi_free(ctx->p);
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
 * * For the safe-prime groups in FIPS mode, Q can be computed
 *   trivially from P and a full validation according to SP800-56A
 *   section 5.6.2.3.1 is performed.
 *
 * * For all other sets of group parameters, only a partial validation
 *   according to SP800-56A section 5.6.2.3.2 is performed.
 */
static int dh_is_pubkey_valid(struct dh_ctx *ctx, MPI y)
{
	if (unlikely(!ctx->p))
		return -EINVAL;

	/*
	 * Step 1: Verify that 2 <= y <= p - 2.
	 *
	 * The upper limit check is actually y < p instead of y < p - 1
	 * in order to save one mpi_sub_ui() invocation here. Note that
	 * p - 1 is the non-trivial element of the subgroup of order 2 and
	 * thus, the check on y^q below would fail if y == p - 1.
	 */
	if (mpi_cmp_ui(y, 1) < 1 || mpi_cmp(y, ctx->p) >= 0)
		return -EINVAL;

	/*
	 * Step 2: Verify that 1 = y^q mod p
	 *
	 * For the safe-prime groups q = (p - 1)/2.
	 */
	if (fips_enabled) {
		MPI val, q;
		int ret;

		val = mpi_alloc(0);
		if (!val)
			return -ENOMEM;

		q = mpi_alloc(mpi_get_nlimbs(ctx->p));
		if (!q) {
			mpi_free(val);
			return -ENOMEM;
		}

		/*
		 * ->p is odd, so no need to explicitly subtract one
		 * from it before shifting to the right.
		 */
		mpi_rshift(q, ctx->p, 1);

		ret = mpi_powm(val, y, q, ctx->p);
		mpi_free(q);
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

static int dh_safe_prime_init_tfm(struct crypto_kpp *tfm)
{
	struct dh_safe_prime_instance_ctx *inst_ctx =
		dh_safe_prime_instance_ctx(tfm);
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);

	tfm_ctx->dh_tfm = crypto_spawn_kpp(&inst_ctx->dh_spawn);
	if (IS_ERR(tfm_ctx->dh_tfm))
		return PTR_ERR(tfm_ctx->dh_tfm);

	kpp_set_reqsize(tfm, sizeof(struct kpp_request) +
			     crypto_kpp_reqsize(tfm_ctx->dh_tfm));

	return 0;
}

static void dh_safe_prime_exit_tfm(struct crypto_kpp *tfm)
{
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);

	crypto_free_kpp(tfm_ctx->dh_tfm);
}

static u64 __add_u64_to_be(__be64 *dst, unsigned int n, u64 val)
{
	unsigned int i;

	for (i = n; val && i > 0; --i) {
		u64 tmp = be64_to_cpu(dst[i - 1]);

		tmp += val;
		val = tmp >= val ? 0 : 1;
		dst[i - 1] = cpu_to_be64(tmp);
	}

	return val;
}

static void *dh_safe_prime_gen_privkey(const struct dh_safe_prime *safe_prime,
				       unsigned int *key_size)
{
	unsigned int n, oversampling_size;
	__be64 *key;
	int err;
	u64 h, o;

	/*
	 * Generate a private key following NIST SP800-56Ar3,
	 * sec. 5.6.1.1.1 and 5.6.1.1.3 resp..
	 *
	 * 5.6.1.1.1: choose key length N such that
	 * 2 * ->max_strength <= N <= log2(q) + 1 = ->p_size * 8 - 1
	 * with q = (p - 1) / 2 for the safe-prime groups.
	 * Choose the lower bound's next power of two for N in order to
	 * avoid excessively large private keys while still
	 * maintaining some extra reserve beyond the bare minimum in
	 * most cases. Note that for each entry in safe_prime_groups[],
	 * the following holds for such N:
	 * - N >= 256, in particular it is a multiple of 2^6 = 64
	 *   bits and
	 * - N < log2(q) + 1, i.e. N respects the upper bound.
	 */
	n = roundup_pow_of_two(2 * safe_prime->max_strength);
	WARN_ON_ONCE(n & ((1u << 6) - 1));
	n >>= 6; /* Convert N into units of u64. */

	/*
	 * Reserve one extra u64 to hold the extra random bits
	 * required as per 5.6.1.1.3.
	 */
	oversampling_size = (n + 1) * sizeof(__be64);
	key = kmalloc(oversampling_size, GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);

	/*
	 * 5.6.1.1.3, step 3 (and implicitly step 4): obtain N + 64
	 * random bits and interpret them as a big endian integer.
	 */
	err = -EFAULT;
	if (crypto_get_default_rng())
		goto out_err;

	err = crypto_rng_get_bytes(crypto_default_rng, (u8 *)key,
				   oversampling_size);
	crypto_put_default_rng();
	if (err)
		goto out_err;

	/*
	 * 5.6.1.1.3, step 5 is implicit: 2^N < q and thus,
	 * M = min(2^N, q) = 2^N.
	 *
	 * For step 6, calculate
	 * key = (key[] mod (M - 1)) + 1 = (key[] mod (2^N - 1)) + 1.
	 *
	 * In order to avoid expensive divisions, note that
	 * 2^N mod (2^N - 1) = 1 and thus, for any integer h,
	 * 2^N * h mod (2^N - 1) = h mod (2^N - 1) always holds.
	 * The big endian integer key[] composed of n + 1 64bit words
	 * may be written as key[] = h * 2^N + l, with h = key[0]
	 * representing the 64 most significant bits and l
	 * corresponding to the remaining 2^N bits. With the remark
	 * from above,
	 * h * 2^N + l mod (2^N - 1) = l + h mod (2^N - 1).
	 * As both, l and h are less than 2^N, their sum after
	 * this first reduction is guaranteed to be <= 2^(N + 1) - 2.
	 * Or equivalently, that their sum can again be written as
	 * h' * 2^N + l' with h' now either zero or one and if one,
	 * then l' <= 2^N - 2. Thus, all bits at positions >= N will
	 * be zero after a second reduction:
	 * h' * 2^N + l' mod (2^N - 1) = l' + h' mod (2^N - 1).
	 * At this point, it is still possible that
	 * l' + h' = 2^N - 1, i.e. that l' + h' mod (2^N - 1)
	 * is zero. This condition will be detected below by means of
	 * the final increment overflowing in this case.
	 */
	h = be64_to_cpu(key[0]);
	h = __add_u64_to_be(key + 1, n, h);
	h = __add_u64_to_be(key + 1, n, h);
	WARN_ON_ONCE(h);

	/* Increment to obtain the final result. */
	o = __add_u64_to_be(key + 1, n, 1);
	/*
	 * The overflow bit o from the increment is either zero or
	 * one. If zero, key[1:n] holds the final result in big-endian
	 * order. If one, key[1:n] is zero now, but needs to be set to
	 * one, c.f. above.
	 */
	if (o)
		key[n] = cpu_to_be64(1);

	/* n is in units of u64, convert to bytes. */
	*key_size = n << 3;
	/* Strip the leading extra __be64, which is (virtually) zero by now. */
	memmove(key, &key[1], *key_size);

	return key;

out_err:
	kfree_sensitive(key);
	return ERR_PTR(err);
}

static int dh_safe_prime_set_secret(struct crypto_kpp *tfm, const void *buffer,
				    unsigned int len)
{
	struct dh_safe_prime_instance_ctx *inst_ctx =
		dh_safe_prime_instance_ctx(tfm);
	struct dh_safe_prime_tfm_ctx *tfm_ctx = kpp_tfm_ctx(tfm);
	struct dh params = {};
	void *buf = NULL, *key = NULL;
	unsigned int buf_size;
	int err;

	if (buffer) {
		err = __crypto_dh_decode_key(buffer, len, &params);
		if (err)
			return err;
		if (params.p_size || params.g_size)
			return -EINVAL;
	}

	params.p = inst_ctx->safe_prime->p;
	params.p_size = inst_ctx->safe_prime->p_size;
	params.g = safe_prime_g;
	params.g_size = sizeof(safe_prime_g);

	if (!params.key_size) {
		key = dh_safe_prime_gen_privkey(inst_ctx->safe_prime,
						&params.key_size);
		if (IS_ERR(key))
			return PTR_ERR(key);
		params.key = key;
	}

	buf_size = crypto_dh_key_len(&params);
	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto out;
	}

	err = crypto_dh_encode_key(buf, buf_size, &params);
	if (err)
		goto out;

	err = crypto_kpp_set_secret(tfm_ctx->dh_tfm, buf, buf_size);
out:
	kfree_sensitive(buf);
	kfree_sensitive(key);
	return err;
}

static void dh_safe_prime_complete_req(void *data, int err)
{
	struct kpp_request *req = data;

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

#ifdef CONFIG_CRYPTO_DH_RFC7919_GROUPS

static const struct dh_safe_prime ffdhe2048_prime = {
	.max_strength = 112,
	.p_size = 256,
	.p =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xad\xf8\x54\x58\xa2\xbb\x4a\x9a"
	"\xaf\xdc\x56\x20\x27\x3d\x3c\xf1\xd8\xb9\xc5\x83\xce\x2d\x36\x95"
	"\xa9\xe1\x36\x41\x14\x64\x33\xfb\xcc\x93\x9d\xce\x24\x9b\x3e\xf9"
	"\x7d\x2f\xe3\x63\x63\x0c\x75\xd8\xf6\x81\xb2\x02\xae\xc4\x61\x7a"
	"\xd3\xdf\x1e\xd5\xd5\xfd\x65\x61\x24\x33\xf5\x1f\x5f\x06\x6e\xd0"
	"\x85\x63\x65\x55\x3d\xed\x1a\xf3\xb5\x57\x13\x5e\x7f\x57\xc9\x35"
	"\x98\x4f\x0c\x70\xe0\xe6\x8b\x77\xe2\xa6\x89\xda\xf3\xef\xe8\x72"
	"\x1d\xf1\x58\xa1\x36\xad\xe7\x35\x30\xac\xca\x4f\x48\x3a\x79\x7a"
	"\xbc\x0a\xb1\x82\xb3\x24\xfb\x61\xd1\x08\xa9\x4b\xb2\xc8\xe3\xfb"
	"\xb9\x6a\xda\xb7\x60\xd7\xf4\x68\x1d\x4f\x42\xa3\xde\x39\x4d\xf4"
	"\xae\x56\xed\xe7\x63\x72\xbb\x19\x0b\x07\xa7\xc8\xee\x0a\x6d\x70"
	"\x9e\x02\xfc\xe1\xcd\xf7\xe2\xec\xc0\x34\x04\xcd\x28\x34\x2f\x61"
	"\x91\x72\xfe\x9c\xe9\x85\x83\xff\x8e\x4f\x12\x32\xee\xf2\x81\x83"
	"\xc3\xfe\x3b\x1b\x4c\x6f\xad\x73\x3b\xb5\xfc\xbc\x2e\xc2\x20\x05"
	"\xc5\x8e\xf1\x83\x7d\x16\x83\xb2\xc6\xf3\x4a\x26\xc1\xb2\xef\xfa"
	"\x88\x6b\x42\x38\x61\x28\x5c\x97\xff\xff\xff\xff\xff\xff\xff\xff",
};

static const struct dh_safe_prime ffdhe3072_prime = {
	.max_strength = 128,
	.p_size = 384,
	.p =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xad\xf8\x54\x58\xa2\xbb\x4a\x9a"
	"\xaf\xdc\x56\x20\x27\x3d\x3c\xf1\xd8\xb9\xc5\x83\xce\x2d\x36\x95"
	"\xa9\xe1\x36\x41\x14\x64\x33\xfb\xcc\x93\x9d\xce\x24\x9b\x3e\xf9"
	"\x7d\x2f\xe3\x63\x63\x0c\x75\xd8\xf6\x81\xb2\x02\xae\xc4\x61\x7a"
	"\xd3\xdf\x1e\xd5\xd5\xfd\x65\x61\x24\x33\xf5\x1f\x5f\x06\x6e\xd0"
	"\x85\x63\x65\x55\x3d\xed\x1a\xf3\xb5\x57\x13\x5e\x7f\x57\xc9\x35"
	"\x98\x4f\x0c\x70\xe0\xe6\x8b\x77\xe2\xa6\x89\xda\xf3\xef\xe8\x72"
	"\x1d\xf1\x58\xa1\x36\xad\xe7\x35\x30\xac\xca\x4f\x48\x3a\x79\x7a"
	"\xbc\x0a\xb1\x82\xb3\x24\xfb\x61\xd1\x08\xa9\x4b\xb2\xc8\xe3\xfb"
	"\xb9\x6a\xda\xb7\x60\xd7\xf4\x68\x1d\x4f\x42\xa3\xde\x39\x4d\xf4"
	"\xae\x56\xed\xe7\x63\x72\xbb\x19\x0b\x07\xa7\xc8\xee\x0a\x6d\x70"
	"\x9e\x02\xfc\xe1\xcd\xf7\xe2\xec\xc0\x34\x04\xcd\x28\x34\x2f\x61"
	"\x91\x72\xfe\x9c\xe9\x85\x83\xff\x8e\x4f\x12\x32\xee\xf2\x81\x83"
	"\xc3\xfe\x3b\x1b\x4c\x6f\xad\x73\x3b\xb5\xfc\xbc\x2e\xc2\x20\x05"
	"\xc5\x8e\xf1\x83\x7d\x16\x83\xb2\xc6\xf3\x4a\x26\xc1\xb2\xef\xfa"
	"\x88\x6b\x42\x38\x61\x1f\xcf\xdc\xde\x35\x5b\x3b\x65\x19\x03\x5b"
	"\xbc\x34\xf4\xde\xf9\x9c\x02\x38\x61\xb4\x6f\xc9\xd6\xe6\xc9\x07"
	"\x7a\xd9\x1d\x26\x91\xf7\xf7\xee\x59\x8c\xb0\xfa\xc1\x86\xd9\x1c"
	"\xae\xfe\x13\x09\x85\x13\x92\x70\xb4\x13\x0c\x93\xbc\x43\x79\x44"
	"\xf4\xfd\x44\x52\xe2\xd7\x4d\xd3\x64\xf2\xe2\x1e\x71\xf5\x4b\xff"
	"\x5c\xae\x82\xab\x9c\x9d\xf6\x9e\xe8\x6d\x2b\xc5\x22\x36\x3a\x0d"
	"\xab\xc5\x21\x97\x9b\x0d\xea\xda\x1d\xbf\x9a\x42\xd5\xc4\x48\x4e"
	"\x0a\xbc\xd0\x6b\xfa\x53\xdd\xef\x3c\x1b\x20\xee\x3f\xd5\x9d\x7c"
	"\x25\xe4\x1d\x2b\x66\xc6\x2e\x37\xff\xff\xff\xff\xff\xff\xff\xff",
};

static const struct dh_safe_prime ffdhe4096_prime = {
	.max_strength = 152,
	.p_size = 512,
	.p =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xad\xf8\x54\x58\xa2\xbb\x4a\x9a"
	"\xaf\xdc\x56\x20\x27\x3d\x3c\xf1\xd8\xb9\xc5\x83\xce\x2d\x36\x95"
	"\xa9\xe1\x36\x41\x14\x64\x33\xfb\xcc\x93\x9d\xce\x24\x9b\x3e\xf9"
	"\x7d\x2f\xe3\x63\x63\x0c\x75\xd8\xf6\x81\xb2\x02\xae\xc4\x61\x7a"
	"\xd3\xdf\x1e\xd5\xd5\xfd\x65\x61\x24\x33\xf5\x1f\x5f\x06\x6e\xd0"
	"\x85\x63\x65\x55\x3d\xed\x1a\xf3\xb5\x57\x13\x5e\x7f\x57\xc9\x35"
	"\x98\x4f\x0c\x70\xe0\xe6\x8b\x77\xe2\xa6\x89\xda\xf3\xef\xe8\x72"
	"\x1d\xf1\x58\xa1\x36\xad\xe7\x35\x30\xac\xca\x4f\x48\x3a\x79\x7a"
	"\xbc\x0a\xb1\x82\xb3\x24\xfb\x61\xd1\x08\xa9\x4b\xb2\xc8\xe3\xfb"
	"\xb9\x6a\xda\xb7\x60\xd7\xf4\x68\x1d\x4f\x42\xa3\xde\x39\x4d\xf4"
	"\xae\x56\xed\xe7\x63\x72\xbb\x19\x0b\x07\xa7\xc8\xee\x0a\x6d\x70"
	"\x9e\x02\xfc\xe1\xcd\xf7\xe2\xec\xc0\x34\x04\xcd\x28\x34\x2f\x61"
	"\x91\x72\xfe\x9c\xe9\x85\x83\xff\x8e\x4f\x12\x32\xee\xf2\x81\x83"
	"\xc3\xfe\x3b\x1b\x4c\x6f\xad\x73\x3b\xb5\xfc\xbc\x2e\xc2\x20\x05"
	"\xc5\x8e\xf1\x83\x7d\x16\x83\xb2\xc6\xf3\x4a\x26\xc1\xb2\xef\xfa"
	"\x88\x6b\x42\x38\x61\x1f\xcf\xdc\xde\x35\x5b\x3b\x65\x19\x03\x5b"
	"\xbc\x34\xf4\xde\xf9\x9c\x02\x38\x61\xb4\x6f\xc9\xd6\xe6\xc9\x07"
	"\x7a\xd9\x1d\x26\x91\xf7\xf7\xee\x59\x8c\xb0\xfa\xc1\x86\xd9\x1c"
	"\xae\xfe\x13\x09\x85\x13\x92\x70\xb4\x13\x0c\x93\xbc\x43\x79\x44"
	"\xf4\xfd\x44\x52\xe2\xd7\x4d\xd3\x64\xf2\xe2\x1e\x71\xf5\x4b\xff"
	"\x5c\xae\x82\xab\x9c\x9d\xf6\x9e\xe8\x6d\x2b\xc5\x22\x36\x3a\x0d"
	"\xab\xc5\x21\x97\x9b\x0d\xea\xda\x1d\xbf\x9a\x42\xd5\xc4\x48\x4e"
	"\x0a\xbc\xd0\x6b\xfa\x53\xdd\xef\x3c\x1b\x20\xee\x3f\xd5\x9d\x7c"
	"\x25\xe4\x1d\x2b\x66\x9e\x1e\xf1\x6e\x6f\x52\xc3\x16\x4d\xf4\xfb"
	"\x79\x30\xe9\xe4\xe5\x88\x57\xb6\xac\x7d\x5f\x42\xd6\x9f\x6d\x18"
	"\x77\x63\xcf\x1d\x55\x03\x40\x04\x87\xf5\x5b\xa5\x7e\x31\xcc\x7a"
	"\x71\x35\xc8\x86\xef\xb4\x31\x8a\xed\x6a\x1e\x01\x2d\x9e\x68\x32"
	"\xa9\x07\x60\x0a\x91\x81\x30\xc4\x6d\xc7\x78\xf9\x71\xad\x00\x38"
	"\x09\x29\x99\xa3\x33\xcb\x8b\x7a\x1a\x1d\xb9\x3d\x71\x40\x00\x3c"
	"\x2a\x4e\xce\xa9\xf9\x8d\x0a\xcc\x0a\x82\x91\xcd\xce\xc9\x7d\xcf"
	"\x8e\xc9\xb5\x5a\x7f\x88\xa4\x6b\x4d\xb5\xa8\x51\xf4\x41\x82\xe1"
	"\xc6\x8a\x00\x7e\x5e\x65\x5f\x6a\xff\xff\xff\xff\xff\xff\xff\xff",
};

static const struct dh_safe_prime ffdhe6144_prime = {
	.max_strength = 176,
	.p_size = 768,
	.p =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xad\xf8\x54\x58\xa2\xbb\x4a\x9a"
	"\xaf\xdc\x56\x20\x27\x3d\x3c\xf1\xd8\xb9\xc5\x83\xce\x2d\x36\x95"
	"\xa9\xe1\x36\x41\x14\x64\x33\xfb\xcc\x93\x9d\xce\x24\x9b\x3e\xf9"
	"\x7d\x2f\xe3\x63\x63\x0c\x75\xd8\xf6\x81\xb2\x02\xae\xc4\x61\x7a"
	"\xd3\xdf\x1e\xd5\xd5\xfd\x65\x61\x24\x33\xf5\x1f\x5f\x06\x6e\xd0"
	"\x85\x63\x65\x55\x3d\xed\x1a\xf3\xb5\x57\x13\x5e\x7f\x57\xc9\x35"
	"\x98\x4f\x0c\x70\xe0\xe6\x8b\x77\xe2\xa6\x89\xda\xf3\xef\xe8\x72"
	"\x1d\xf1\x58\xa1\x36\xad\xe7\x35\x30\xac\xca\x4f\x48\x3a\x79\x7a"
	"\xbc\x0a\xb1\x82\xb3\x24\xfb\x61\xd1\x08\xa9\x4b\xb2\xc8\xe3\xfb"
	"\xb9\x6a\xda\xb7\x60\xd7\xf4\x68\x1d\x4f\x42\xa3\xde\x39\x4d\xf4"
	"\xae\x56\xed\xe7\x63\x72\xbb\x19\x0b\x07\xa7\xc8\xee\x0a\x6d\x70"
	"\x9e\x02\xfc\xe1\xcd\xf7\xe2\xec\xc0\x34\x04\xcd\x28\x34\x2f\x61"
	"\x91\x72\xfe\x9c\xe9\x85\x83\xff\x8e\x4f\x12\x32\xee\xf2\x81\x83"
	"\xc3\xfe\x3b\x1b\x4c\x6f\xad\x73\x3b\xb5\xfc\xbc\x2e\xc2\x20\x05"
	"\xc5\x8e\xf1\x83\x7d\x16\x83\xb2\xc6\xf3\x4a\x26\xc1\xb2\xef\xfa"
	"\x88\x6b\x42\x38\x61\x1f\xcf\xdc\xde\x35\x5b\x3b\x65\x19\x03\x5b"
	"\xbc\x34\xf4\xde\xf9\x9c\x02\x38\x61\xb4\x6f\xc9\xd6\xe6\xc9\x07"
	"\x7a\xd9\x1d\x26\x91\xf7\xf7\xee\x59\x8c\xb0\xfa\xc1\x86\xd9\x1c"
	"\xae\xfe\x13\x09\x85\x13\x92\x70\xb4\x13\x0c\x93\xbc\x43\x79\x44"
	"\xf4\xfd\x44\x52\xe2\xd7\x4d\xd3\x64\xf2\xe2\x1e\x71\xf5\x4b\xff"
	"\x5c\xae\x82\xab\x9c\x9d\xf6\x9e\xe8\x6d\x2b\xc5\x22\x36\x3a\x0d"
	"\xab\xc5\x21\x97\x9b\x0d\xea\xda\x1d\xbf\x9a\x42\xd5\xc4\x48\x4e"
	"\x0a\xbc\xd0\x6b\xfa\x53\xdd\xef\x3c\x1b\x20\xee\x3f\xd5\x9d\x7c"
	"\x25\xe4\x1d\x2b\x66\x9e\x1e\xf1\x6e\x6f\x52\xc3\x16\x4d\xf4\xfb"
	"\x79\x30\xe9\xe4\xe5\x88\x57\xb6\xac\x7d\x5f\x42\xd6\x9f\x6d\x18"
	"\x77\x63\xcf\x1d\x55\x03\x40\x04\x87\xf5\x5b\xa5\x7e\x31\xcc\x7a"
	"\x71\x35\xc8\x86\xef\xb4\x31\x8a\xed\x6a\x1e\x01\x2d\x9e\x68\x32"
	"\xa9\x07\x60\x0a\x91\x81\x30\xc4\x6d\xc7\x78\xf9\x71\xad\x00\x38"
	"\x09\x29\x99\xa3\x33\xcb\x8b\x7a\x1a\x1d\xb9\x3d\x71\x40\x00\x3c"
	"\x2a\x4e\xce\xa9\xf9\x8d\x0a\xcc\x0a\x82\x91\xcd\xce\xc9\x7d\xcf"
	"\x8e\xc9\xb5\x5a\x7f\x88\xa4\x6b\x4d\xb5\xa8\x51\xf4\x41\x82\xe1"
	"\xc6\x8a\x00\x7e\x5e\x0d\xd9\x02\x0b\xfd\x64\xb6\x45\x03\x6c\x7a"
	"\x4e\x67\x7d\x2c\x38\x53\x2a\x3a\x23\xba\x44\x42\xca\xf5\x3e\xa6"
	"\x3b\xb4\x54\x32\x9b\x76\x24\xc8\x91\x7b\xdd\x64\xb1\xc0\xfd\x4c"
	"\xb3\x8e\x8c\x33\x4c\x70\x1c\x3a\xcd\xad\x06\x57\xfc\xcf\xec\x71"
	"\x9b\x1f\x5c\x3e\x4e\x46\x04\x1f\x38\x81\x47\xfb\x4c\xfd\xb4\x77"
	"\xa5\x24\x71\xf7\xa9\xa9\x69\x10\xb8\x55\x32\x2e\xdb\x63\x40\xd8"
	"\xa0\x0e\xf0\x92\x35\x05\x11\xe3\x0a\xbe\xc1\xff\xf9\xe3\xa2\x6e"
	"\x7f\xb2\x9f\x8c\x18\x30\x23\xc3\x58\x7e\x38\xda\x00\x77\xd9\xb4"
	"\x76\x3e\x4e\x4b\x94\xb2\xbb\xc1\x94\xc6\x65\x1e\x77\xca\xf9\x92"
	"\xee\xaa\xc0\x23\x2a\x28\x1b\xf6\xb3\xa7\x39\xc1\x22\x61\x16\x82"
	"\x0a\xe8\xdb\x58\x47\xa6\x7c\xbe\xf9\xc9\x09\x1b\x46\x2d\x53\x8c"
	"\xd7\x2b\x03\x74\x6a\xe7\x7f\x5e\x62\x29\x2c\x31\x15\x62\xa8\x46"
	"\x50\x5d\xc8\x2d\xb8\x54\x33\x8a\xe4\x9f\x52\x35\xc9\x5b\x91\x17"
	"\x8c\xcf\x2d\xd5\xca\xce\xf4\x03\xec\x9d\x18\x10\xc6\x27\x2b\x04"
	"\x5b\x3b\x71\xf9\xdc\x6b\x80\xd6\x3f\xdd\x4a\x8e\x9a\xdb\x1e\x69"
	"\x62\xa6\x95\x26\xd4\x31\x61\xc1\xa4\x1d\x57\x0d\x79\x38\xda\xd4"
	"\xa4\x0e\x32\x9c\xd0\xe4\x0e\x65\xff\xff\xff\xff\xff\xff\xff\xff",
};

static const struct dh_safe_prime ffdhe8192_prime = {
	.max_strength = 200,
	.p_size = 1024,
	.p =
	"\xff\xff\xff\xff\xff\xff\xff\xff\xad\xf8\x54\x58\xa2\xbb\x4a\x9a"
	"\xaf\xdc\x56\x20\x27\x3d\x3c\xf1\xd8\xb9\xc5\x83\xce\x2d\x36\x95"
	"\xa9\xe1\x36\x41\x14\x64\x33\xfb\xcc\x93\x9d\xce\x24\x9b\x3e\xf9"
	"\x7d\x2f\xe3\x63\x63\x0c\x75\xd8\xf6\x81\xb2\x02\xae\xc4\x61\x7a"
	"\xd3\xdf\x1e\xd5\xd5\xfd\x65\x61\x24\x33\xf5\x1f\x5f\x06\x6e\xd0"
	"\x85\x63\x65\x55\x3d\xed\x1a\xf3\xb5\x57\x13\x5e\x7f\x57\xc9\x35"
	"\x98\x4f\x0c\x70\xe0\xe6\x8b\x77\xe2\xa6\x89\xda\xf3\xef\xe8\x72"
	"\x1d\xf1\x58\xa1\x36\xad\xe7\x35\x30\xac\xca\x4f\x48\x3a\x79\x7a"
	"\xbc\x0a\xb1\x82\xb3\x24\xfb\x61\xd1\x08\xa9\x4b\xb2\xc8\xe3\xfb"
	"\xb9\x6a\xda\xb7\x60\xd7\xf4\x68\x1d\x4f\x42\xa3\xde\x39\x4d\xf4"
	"\xae\x56\xed\xe7\x63\x72\xbb\x19\x0b\x07\xa7\xc8\xee\x0a\x6d\x70"
	"\x9e\x02\xfc\xe1\xcd\xf7\xe2\xec\xc0\x34\x04\xcd\x28\x34\x2f\x61"
	"\x91\x72\xfe\x9c\xe9\x85\x83\xff\x8e\x4f\x12\x32\xee\xf2\x81\x83"
	"\xc3\xfe\x3b\x1b\x4c\x6f\xad\x73\x3b\xb5\xfc\xbc\x2e\xc2\x20\x05"
	"\xc5\x8e\xf1\x83\x7d\x16\x83\xb2\xc6\xf3\x4a\x26\xc1\xb2\xef\xfa"
	"\x88\x6b\x42\x38\x61\x1f\xcf\xdc\xde\x35\x5b\x3b\x65\x19\x03\x5b"
	"\xbc\x34\xf4\xde\xf9\x9c\x02\x38\x61\xb4\x6f\xc9\xd6\xe6\xc9\x07"
	"\x7a\xd9\x1d\x26\x91\xf7\xf7\xee\x59\x8c\xb0\xfa\xc1\x86\xd9\x1c"
	"\xae\xfe\x13\x09\x85\x13\x92\x70\xb4\x13\x0c\x93\xbc\x43\x79\x44"
	"\xf4\xfd\x44\x52\xe2\xd7\x4d\xd3\x64\xf2\xe2\x1e\x71\xf5\x4b\xff"
	"\x5c\xae\x82\xab\x9c\x9d\xf6\x9e\xe8\x6d\x2b\xc5\x22\x36\x3a\x0d"
	"\xab\xc5\x21\x97\x9b\x0d\xea\xda\x1d\xbf\x9a\x42\xd5\xc4\x48\x4e"
	"\x0a\xbc\xd0\x6b\xfa\x53\xdd\xef\x3c\x1b\x20\xee\x3f\xd5\x9d\x7c"
	"\x25\xe4\x1d\x2b\x66\x9e\x1e\xf1\x6e\x6f\x52\xc3\x16\x4d\xf4\xfb"
	"\x79\x30\xe9\xe4\xe5\x88\x57\xb6\xac\x7d\x5f\x42\xd6\x9f\x6d\x18"
	"\x77\x63\xcf\x1d\x55\x03\x40\x04\x87\xf5\x5b\xa5\x7e\x31\xcc\x7a"
	"\x71\x35\xc8\x86\xef\xb4\x31\x8a\xed\x6a\x1e\x01\x2d\x9e\x68\x32"
	"\xa9\x07\x60\x0a\x91\x81\x30\xc4\x6d\xc7\x78\xf9\x71\xad\x00\x38"
	"\x09\x29\x99\xa3\x33\xcb\x8b\x7a\x1a\x1d\xb9\x3d\x71\x40\x00\x3c"
	"\x2a\x4e\xce\xa9\xf9\x8d\x0a\xcc\x0a\x82\x91\xcd\xce\xc9\x7d\xcf"
	"\x8e\xc9\xb5\x5a\x7f\x88\xa4\x6b\x4d\xb5\xa8\x51\xf4\x41\x82\xe1"
	"\xc6\x8a\x00\x7e\x5e\x0d\xd9\x02\x0b\xfd\x64\xb6\x45\x03\x6c\x7a"
	"\x4e\x67\x7d\x2c\x38\x53\x2a\x3a\x23\xba\x44\x42\xca\xf5\x3e\xa6"
	"\x3b\xb4\x54\x32\x9b\x76\x24\xc8\x91\x7b\xdd\x64\xb1\xc0\xfd\x4c"
	"\xb3\x8e\x8c\x33\x4c\x70\x1c\x3a\xcd\xad\x06\x57\xfc\xcf\xec\x71"
	"\x9b\x1f\x5c\x3e\x4e\x46\x04\x1f\x38\x81\x47\xfb\x4c\xfd\xb4\x77"
	"\xa5\x24\x71\xf7\xa9\xa9\x69\x10\xb8\x55\x32\x2e\xdb\x63\x40\xd8"
	"\xa0\x0e\xf0\x92\x35\x05\x11\xe3\x0a\xbe\xc1\xff\xf9\xe3\xa2\x6e"
	"\x7f\xb2\x9f\x8c\x18\x30\x23\xc3\x58\x7e\x38\xda\x00\x77\xd9\xb4"
	"\x76\x3e\x4e\x4b\x94\xb2\xbb\xc1\x94\xc6\x65\x1e\x77\xca\xf9\x92"
	"\xee\xaa\xc0\x23\x2a\x28\x1b\xf6\xb3\xa7\x39\xc1\x22\x61\x16\x82"
	"\x0a\xe8\xdb\x58\x47\xa6\x7c\xbe\xf9\xc9\x09\x1b\x46\x2d\x53\x8c"
	"\xd7\x2b\x03\x74\x6a\xe7\x7f\x5e\x62\x29\x2c\x31\x15\x62\xa8\x46"
	"\x50\x5d\xc8\x2d\xb8\x54\x33\x8a\xe4\x9f\x52\x35\xc9\x5b\x91\x17"
	"\x8c\xcf\x2d\xd5\xca\xce\xf4\x03\xec\x9d\x18\x10\xc6\x27\x2b\x04"
	"\x5b\x3b\x71\xf9\xdc\x6b\x80\xd6\x3f\xdd\x4a\x8e\x9a\xdb\x1e\x69"
	"\x62\xa6\x95\x26\xd4\x31\x61\xc1\xa4\x1d\x57\x0d\x79\x38\xda\xd4"
	"\xa4\x0e\x32\x9c\xcf\xf4\x6a\xaa\x36\xad\x00\x4c\xf6\x00\xc8\x38"
	"\x1e\x42\x5a\x31\xd9\x51\xae\x64\xfd\xb2\x3f\xce\xc9\x50\x9d\x43"
	"\x68\x7f\xeb\x69\xed\xd1\xcc\x5e\x0b\x8c\xc3\xbd\xf6\x4b\x10\xef"
	"\x86\xb6\x31\x42\xa3\xab\x88\x29\x55\x5b\x2f\x74\x7c\x93\x26\x65"
	"\xcb\x2c\x0f\x1c\xc0\x1b\xd7\x02\x29\x38\x88\x39\xd2\xaf\x05\xe4"
	"\x54\x50\x4a\xc7\x8b\x75\x82\x82\x28\x46\xc0\xba\x35\xc3\x5f\x5c"
	"\x59\x16\x0c\xc0\x46\xfd\x82\x51\x54\x1f\xc6\x8c\x9c\x86\xb0\x22"
	"\xbb\x70\x99\x87\x6a\x46\x0e\x74\x51\xa8\xa9\x31\x09\x70\x3f\xee"
	"\x1c\x21\x7e\x6c\x38\x26\xe5\x2c\x51\xaa\x69\x1e\x0e\x42\x3c\xfc"
	"\x99\xe9\xe3\x16\x50\xc1\x21\x7b\x62\x48\x16\xcd\xad\x9a\x95\xf9"
	"\xd5\xb8\x01\x94\x88\xd9\xc0\xa0\xa1\xfe\x30\x75\xa5\x77\xe2\x31"
	"\x83\xf8\x1d\x4a\x3f\x2f\xa4\x57\x1e\xfc\x8c\xe0\xba\x8a\x4f\xe8"
	"\xb6\x85\x5d\xfe\x72\xb0\xa6\x6e\xde\xd2\xfb\xab\xfb\xe5\x8a\x30"
	"\xfa\xfa\xbe\x1c\x5d\x71\xa8\x7e\x2f\x74\x1e\xf8\xc1\xfe\x86\xfe"
	"\xa6\xbb\xfd\xe5\x30\x67\x7f\x0d\x97\xd1\x1d\x49\xf7\xa8\x44\x3d"
	"\x08\x22\xe5\x06\xa9\xf4\x61\x4e\x01\x1e\x2a\x94\x83\x8f\xf8\x8c"
	"\xd6\x8c\x8b\xb7\xc5\xc6\x42\x4c\xff\xff\xff\xff\xff\xff\xff\xff",
};

static int dh_ffdhe2048_create(struct crypto_template *tmpl,
			       struct rtattr **tb)
{
	return  __dh_safe_prime_create(tmpl, tb, &ffdhe2048_prime);
}

static int dh_ffdhe3072_create(struct crypto_template *tmpl,
			       struct rtattr **tb)
{
	return  __dh_safe_prime_create(tmpl, tb, &ffdhe3072_prime);
}

static int dh_ffdhe4096_create(struct crypto_template *tmpl,
			       struct rtattr **tb)
{
	return  __dh_safe_prime_create(tmpl, tb, &ffdhe4096_prime);
}

static int dh_ffdhe6144_create(struct crypto_template *tmpl,
			       struct rtattr **tb)
{
	return  __dh_safe_prime_create(tmpl, tb, &ffdhe6144_prime);
}

static int dh_ffdhe8192_create(struct crypto_template *tmpl,
			       struct rtattr **tb)
{
	return  __dh_safe_prime_create(tmpl, tb, &ffdhe8192_prime);
}

static struct crypto_template crypto_ffdhe_templates[] = {
	{
		.name = "ffdhe2048",
		.create = dh_ffdhe2048_create,
		.module = THIS_MODULE,
	},
	{
		.name = "ffdhe3072",
		.create = dh_ffdhe3072_create,
		.module = THIS_MODULE,
	},
	{
		.name = "ffdhe4096",
		.create = dh_ffdhe4096_create,
		.module = THIS_MODULE,
	},
	{
		.name = "ffdhe6144",
		.create = dh_ffdhe6144_create,
		.module = THIS_MODULE,
	},
	{
		.name = "ffdhe8192",
		.create = dh_ffdhe8192_create,
		.module = THIS_MODULE,
	},
};

#else /* ! CONFIG_CRYPTO_DH_RFC7919_GROUPS */

static struct crypto_template crypto_ffdhe_templates[] = {};

#endif /* CONFIG_CRYPTO_DH_RFC7919_GROUPS */


static int __init dh_init(void)
{
	int err;

	err = crypto_register_kpp(&dh);
	if (err)
		return err;

	err = crypto_register_templates(crypto_ffdhe_templates,
					ARRAY_SIZE(crypto_ffdhe_templates));
	if (err) {
		crypto_unregister_kpp(&dh);
		return err;
	}

	return 0;
}

static void __exit dh_exit(void)
{
	crypto_unregister_templates(crypto_ffdhe_templates,
				    ARRAY_SIZE(crypto_ffdhe_templates));
	crypto_unregister_kpp(&dh);
}

subsys_initcall(dh_init);
module_exit(dh_exit);
MODULE_ALIAS_CRYPTO("dh");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DH generic algorithm");
