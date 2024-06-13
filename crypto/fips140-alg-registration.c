// SPDX-License-Identifier: GPL-2.0-only
/*
 * Block crypto operations until tests complete
 *
 * Copyright 2021 Google LLC
 *
 * This file defines the fips140_crypto_register_*() functions, to which all
 * calls to crypto_register_*() in the module are redirected.  These functions
 * override the tfm initialization function of each algorithm to insert a wait
 * for the module having completed its self-tests and integrity check.
 *
 * The exact field that we override depends on the algorithm type.  For
 * algorithm types that have a strongly-typed initialization function pointer
 * (e.g. skcipher), we must override that, since cra_init isn't guaranteed to be
 * called for those despite the field being present in the base struct.  For the
 * other algorithm types (e.g. "cipher") we must override cra_init.
 *
 * All of this applies to both normal algorithms and template instances.
 *
 * The purpose of all of this is to meet a FIPS requirement where the module
 * must not produce any output from cryptographic algorithms until it completes
 * its tests.  Technically this is impossible, but this solution meets the
 * intent of the requirement, assuming the user makes a supported sequence of
 * API calls.  Note that we can't simply run the tests before registering the
 * algorithms, as the algorithms must be registered in order to run the tests.
 *
 * It would be much easier to handle this in the kernel's crypto API framework.
 * Unfortunately, that was deemed insufficient because the module itself is
 * required to do the enforcement.  What is *actually* required is still very
 * vague, but the approach implemented here should meet the requirement.
 */

/*
 * This file is the one place in fips140.ko that needs to call the kernel's real
 * algorithm registration functions, so #undefine all the macros from
 * fips140-defs.h so that the "fips140_" prefix doesn't automatically get added.
 */
#undef aead_register_instance
#undef ahash_register_instance
#undef crypto_register_aead
#undef crypto_register_aeads
#undef crypto_register_ahash
#undef crypto_register_ahashes
#undef crypto_register_alg
#undef crypto_register_algs
#undef crypto_register_rng
#undef crypto_register_rngs
#undef crypto_register_shash
#undef crypto_register_shashes
#undef crypto_register_skcipher
#undef crypto_register_skciphers
#undef shash_register_instance
#undef skcipher_register_instance

#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/rng.h>
#include <crypto/internal/skcipher.h>
#include <linux/xarray.h>

#include "fips140-module.h"

/* Indicates whether the self-tests and integrity check have completed */
DECLARE_COMPLETION(fips140_tests_done);

/* The thread running the self-tests and integrity check */
struct task_struct *fips140_init_thread;

/*
 * Map from crypto_alg to original initialization function (possibly NULL)
 *
 * Note: unregistering an algorithm will leak its map entry, as we don't bother
 * to remove it.  This should be fine since fips140.ko can't be unloaded.  The
 * proper solution would be to store the original function pointer in a new
 * field in 'struct crypto_alg', but that would require kernel support.
 */
static DEFINE_XARRAY(fips140_init_func_map);

static bool fips140_ready(void)
{
	return completion_done(&fips140_tests_done);
}

/*
 * Wait until crypto operations are allowed to proceed.  Return true if the
 * tests are done, or false if the caller is the thread running the tests so it
 * is allowed to proceed anyway.
 */
static bool fips140_wait_until_ready(struct crypto_alg *alg)
{
	if (fips140_ready())
		return true;
	/*
	 * The thread running the tests must not wait.  Since tfms can only be
	 * allocated in task context, we can reliably determine whether the
	 * invocation is from that thread or not by checking 'current'.
	 */
	if (current == fips140_init_thread)
		return false;

	pr_info("blocking user of %s until tests complete\n",
		alg->cra_driver_name);
	wait_for_completion(&fips140_tests_done);
	pr_info("tests done, allowing %s to proceed\n", alg->cra_driver_name);
	return true;
}

static int fips140_store_init_function(struct crypto_alg *alg, void *func)
{
	void *ret;

	/*
	 * The XArray API requires 4-byte aligned values.  Although function
	 * pointers in general aren't guaranteed to be 4-byte aligned, it should
	 * be the case for the platforms this module is used on.
	 */
	if (WARN_ON((unsigned long)func & 3))
		return -EINVAL;

	ret = xa_store(&fips140_init_func_map, (unsigned long)alg, func,
		       GFP_KERNEL);
	return xa_err(ret);
}

/* Get the algorithm's original initialization function (possibly NULL) */
static void *fips140_load_init_function(struct crypto_alg *alg)
{
	return xa_load(&fips140_init_func_map, (unsigned long)alg);
}

/* tfm initialization function overrides */

static int fips140_alg_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	int (*cra_init)(struct crypto_tfm *tfm) =
		fips140_load_init_function(alg);

	if (fips140_wait_until_ready(alg))
		WRITE_ONCE(alg->cra_init, cra_init);
	return cra_init ? cra_init(tfm) : 0;
}

static int fips140_aead_init_tfm(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	int (*init)(struct crypto_aead *tfm) =
		fips140_load_init_function(&alg->base);

	if (fips140_wait_until_ready(&alg->base))
		WRITE_ONCE(alg->init, init);
	return init ? init(tfm) : 0;
}

static int fips140_ahash_init_tfm(struct crypto_ahash *tfm)
{
	struct hash_alg_common *halg = crypto_hash_alg_common(tfm);
	struct ahash_alg *alg = container_of(halg, struct ahash_alg, halg);
	int (*init_tfm)(struct crypto_ahash *tfm) =
		fips140_load_init_function(&halg->base);

	if (fips140_wait_until_ready(&halg->base))
		WRITE_ONCE(alg->init_tfm, init_tfm);
	return init_tfm ? init_tfm(tfm) : 0;
}

static int fips140_shash_init_tfm(struct crypto_shash *tfm)
{
	struct shash_alg *alg = crypto_shash_alg(tfm);
	int (*init_tfm)(struct crypto_shash *tfm) =
		fips140_load_init_function(&alg->base);

	if (fips140_wait_until_ready(&alg->base))
		WRITE_ONCE(alg->init_tfm, init_tfm);
	return init_tfm ? init_tfm(tfm) : 0;
}

static int fips140_skcipher_init_tfm(struct crypto_skcipher *tfm)
{
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	int (*init)(struct crypto_skcipher *tfm) =
		fips140_load_init_function(&alg->base);

	if (fips140_wait_until_ready(&alg->base))
		WRITE_ONCE(alg->init, init);
	return init ? init(tfm) : 0;
}

/* Single algorithm registration */

#define prepare_alg(alg, base_alg, field, wrapper_func)			\
({									\
	int err = 0;							\
									\
	if (!fips140_ready() && alg->field != wrapper_func) {		\
		err = fips140_store_init_function(base_alg, alg->field);\
		if (err == 0)						\
			alg->field = wrapper_func;			\
	}								\
	err;								\
})

static int fips140_prepare_alg(struct crypto_alg *alg)
{
	/*
	 * Override cra_init.  This is only for algorithm types like cipher and
	 * rng that don't have a strongly-typed initialization function.
	 */
	return prepare_alg(alg, alg, cra_init, fips140_alg_init_tfm);
}

static int fips140_prepare_aead_alg(struct aead_alg *alg)
{
	return prepare_alg(alg, &alg->base, init, fips140_aead_init_tfm);
}

static int fips140_prepare_ahash_alg(struct ahash_alg *alg)
{
	return prepare_alg(alg, &alg->halg.base, init_tfm,
			   fips140_ahash_init_tfm);
}

static int fips140_prepare_rng_alg(struct rng_alg *alg)
{
	/*
	 * rng doesn't have a strongly-typed initialization function, so we must
	 * treat rng algorithms as "generic" algorithms.
	 */
	return fips140_prepare_alg(&alg->base);
}

static int fips140_prepare_shash_alg(struct shash_alg *alg)
{
	return prepare_alg(alg, &alg->base, init_tfm, fips140_shash_init_tfm);
}

static int fips140_prepare_skcipher_alg(struct skcipher_alg *alg)
{
	return prepare_alg(alg, &alg->base, init, fips140_skcipher_init_tfm);
}

int fips140_crypto_register_alg(struct crypto_alg *alg)
{
	return fips140_prepare_alg(alg) ?: crypto_register_alg(alg);
}

int fips140_crypto_register_aead(struct aead_alg *alg)
{
	return fips140_prepare_aead_alg(alg) ?: crypto_register_aead(alg);
}

int fips140_crypto_register_ahash(struct ahash_alg *alg)
{
	return fips140_prepare_ahash_alg(alg) ?: crypto_register_ahash(alg);
}

int fips140_crypto_register_rng(struct rng_alg *alg)
{
	return fips140_prepare_rng_alg(alg) ?: crypto_register_rng(alg);
}

int fips140_crypto_register_shash(struct shash_alg *alg)
{
	return fips140_prepare_shash_alg(alg) ?: crypto_register_shash(alg);
}

int fips140_crypto_register_skcipher(struct skcipher_alg *alg)
{
	return fips140_prepare_skcipher_alg(alg) ?:
		crypto_register_skcipher(alg);
}

/* Instance registration */

int fips140_aead_register_instance(struct crypto_template *tmpl,
				   struct aead_instance *inst)
{
	return fips140_prepare_aead_alg(&inst->alg) ?:
		aead_register_instance(tmpl, inst);
}

int fips140_ahash_register_instance(struct crypto_template *tmpl,
				    struct ahash_instance *inst)
{
	return fips140_prepare_ahash_alg(&inst->alg) ?:
		ahash_register_instance(tmpl, inst);
}

int fips140_shash_register_instance(struct crypto_template *tmpl,
				    struct shash_instance *inst)
{
	return fips140_prepare_shash_alg(&inst->alg) ?:
		shash_register_instance(tmpl, inst);
}

int fips140_skcipher_register_instance(struct crypto_template *tmpl,
				       struct skcipher_instance *inst)
{
	return fips140_prepare_skcipher_alg(&inst->alg) ?:
		skcipher_register_instance(tmpl, inst);
}

/* Bulk algorithm registration */

int fips140_crypto_register_algs(struct crypto_alg *algs, int count)
{
	int i;
	int err;

	for (i = 0; i < count; i++) {
		err = fips140_prepare_alg(&algs[i]);
		if (err)
			return err;
	}

	return crypto_register_algs(algs, count);
}

int fips140_crypto_register_aeads(struct aead_alg *algs, int count)
{
	int i;
	int err;

	for (i = 0; i < count; i++) {
		err = fips140_prepare_aead_alg(&algs[i]);
		if (err)
			return err;
	}

	return crypto_register_aeads(algs, count);
}

int fips140_crypto_register_ahashes(struct ahash_alg *algs, int count)
{
	int i;
	int err;

	for (i = 0; i < count; i++) {
		err = fips140_prepare_ahash_alg(&algs[i]);
		if (err)
			return err;
	}

	return crypto_register_ahashes(algs, count);
}

int fips140_crypto_register_rngs(struct rng_alg *algs, int count)
{
	int i;
	int err;

	for (i = 0; i < count; i++) {
		err = fips140_prepare_rng_alg(&algs[i]);
		if (err)
			return err;
	}

	return crypto_register_rngs(algs, count);
}

int fips140_crypto_register_shashes(struct shash_alg *algs, int count)
{
	int i;
	int err;

	for (i = 0; i < count; i++) {
		err = fips140_prepare_shash_alg(&algs[i]);
		if (err)
			return err;
	}

	return crypto_register_shashes(algs, count);
}

int fips140_crypto_register_skciphers(struct skcipher_alg *algs, int count)
{
	int i;
	int err;

	for (i = 0; i < count; i++) {
		err = fips140_prepare_skcipher_alg(&algs[i]);
		if (err)
			return err;
	}

	return crypto_register_skciphers(algs, count);
}
