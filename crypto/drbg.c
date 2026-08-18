/*
 * DRBG: Deterministic Random Bits Generator
 *       Implementation of the HMAC SHA-512 DRBG from NIST SP800-90A
 *
 * Copyright Stephan Mueller <smueller@chronox.de>, 2014
 * Copyright 2026 Google LLC
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * DRBG Usage
 * ==========
 * The SP 800-90A DRBG allows the user to specify a personalization string
 * for initialization as well as an additional information string for each
 * random number request. The following code fragments show how a caller
 * uses the kernel crypto API to use the full functionality of the DRBG.
 *
 * Usage without any additional data
 * ---------------------------------
 * struct crypto_rng *drng;
 * int err;
 * char data[DATALEN];
 *
 * drng = crypto_alloc_rng(drng_name, 0, 0);
 * err = crypto_rng_get_bytes(drng, data, DATALEN);
 * crypto_free_rng(drng);
 *
 *
 * Usage with personalization string during initialization
 * -------------------------------------------------------
 * struct crypto_rng *drng;
 * int err;
 * char data[DATALEN];
 * char personalization[11] = "some-string";
 *
 * drng = crypto_alloc_rng(drng_name, 0, 0);
 * // The reset completely re-initializes the DRBG with the provided
 * // personalization string
 * err = crypto_rng_reset(drng, personalization, strlen(personalization));
 * err = crypto_rng_get_bytes(drng, data, DATALEN);
 * crypto_free_rng(drng);
 *
 *
 * Usage with additional information string during random number request
 * ---------------------------------------------------------------------
 * struct crypto_rng *drng;
 * int err;
 * char data[DATALEN];
 * char addtl_string[11] = "some-string";
 *
 * drng = crypto_alloc_rng(drng_name, 0, 0);
 * err = crypto_rng_generate(drng, addtl_string, strlen(addtl_string),
			     data, DATALEN);
 * crypto_free_rng(drng);
 *
 *
 * Usage with personalization and additional information strings
 * -------------------------------------------------------------
 * Just mix both scenarios above.
 */

#include <crypto/internal/rng.h>
#include <crypto/sha2.h>
#include <linux/fips.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string_choices.h>
#include <linux/unaligned.h>

/* State length in bytes */
#define DRBG_STATE_LEN		SHA512_DIGEST_SIZE

/* Security strength in bytes */
#define DRBG_SEC_STRENGTH	(SHA512_DIGEST_SIZE / 2)

/*
 * Maximum number of requests before reseeding is forced.
 * SP800-90A allows this to be up to 2**48.  We use a lower value.
 */
#define DRBG_MAX_REQUESTS	4096

/*
 * Maximum number of random bytes that can be requested at once.
 * SP800-90A allows up to 2**19 bits, which is 2**16 bytes.
 */
#define DRBG_MAX_REQUEST_BYTES	(1 << 16)

/*
 * Maximum length of additional info and personalization strings, in bytes.
 * SP800-90A allows up to 2**35 bits, i.e. 2**32 bytes.  We use 2**32 - 2 bytes
 * so that the value never quite completely fills the range of a size_t,
 * allowing the health check to verify that larger values are rejected.
 */
#define DRBG_MAX_ADDTL_BYTES	(U32_MAX - 1)

struct drbg_state {
	struct mutex drbg_mutex;	/* lock around DRBG */
	u8 V[DRBG_STATE_LEN];		/* internal state -- 10.1.2.1 1a */
	struct hmac_sha512_key key;	/* current key -- 10.1.2.1 1b */
	/* Number of RNG requests since last reseed -- 10.1.2.1 1c */
	size_t reseed_ctr;
	bool instantiated;
	struct crypto_rng *jent;
	const u8 *test_entropy;
	size_t test_entropylen;
};

/******************************************************************
 * HMAC DRBG functions
 ******************************************************************/

/* update function of HMAC DRBG as defined in 10.1.2.2 */
static void drbg_hmac_update(struct drbg_state *drbg,
			     const u8 *data1, size_t data1_len,
			     const u8 *data2, size_t data2_len)
{
	struct hmac_sha512_ctx hmac_ctx;
	u8 new_key[DRBG_STATE_LEN];

	for (u8 i = 0; i < 2; i++) {
		/* 10.1.2.2 step 1 and 4 -- concatenation and HMAC for key */
		hmac_sha512_init(&hmac_ctx, &drbg->key);
		hmac_sha512_update(&hmac_ctx, drbg->V, DRBG_STATE_LEN);
		hmac_sha512_update(&hmac_ctx, &i, 1);
		hmac_sha512_update(&hmac_ctx, data1, data1_len);
		hmac_sha512_update(&hmac_ctx, data2, data2_len);
		hmac_sha512_final(&hmac_ctx, new_key);
		hmac_sha512_preparekey(&drbg->key, new_key, DRBG_STATE_LEN);

		/* 10.1.2.2 step 2 and 5 -- HMAC for V */
		hmac_sha512(&drbg->key, drbg->V, DRBG_STATE_LEN, drbg->V);

		/* 10.1.2.2 step 3 */
		if (data1_len == 0 && data2_len == 0)
			break;
	}
	memzero_explicit(new_key, sizeof(new_key));
}

/* generate function of HMAC DRBG as defined in 10.1.2.5 */
static void drbg_hmac_generate(struct drbg_state *drbg, u8 *out, size_t outlen,
			       const u8 *addtl1, size_t addtl1_len)
{
	u8 addtl2[32];
	size_t addtl2_len = 0;

	/*
	 * Append some bytes from get_random_bytes() to the additional input
	 * string, except when in test mode (as it would break the tests).
	 * Using a nonempty additional input string works around the forward
	 * secrecy bug in HMAC_DRBG described by Woodage & Shumow (2018)
	 * (https://eprint.iacr.org/2018/349.pdf).  Filling the string with
	 * get_random_bytes() rather than a fixed value is safer still, and in
	 * particular makes random.c reseeds be immediately reflected.
	 *
	 * Note that there's no need to pull bytes from jitterentropy here too,
	 * since FIPS doesn't require any entropy in the additional input.
	 */
	if (drbg->test_entropylen == 0) {
		get_random_bytes(addtl2, sizeof(addtl2));
		addtl2_len = sizeof(addtl2);
	}

	/* 10.1.2.5 step 2 */
	if (addtl1_len || addtl2_len)
		drbg_hmac_update(drbg, addtl1, addtl1_len, addtl2, addtl2_len);

	while (outlen) {
		size_t n = min(DRBG_STATE_LEN, outlen);

		/* 10.1.2.5 step 4.1 */
		hmac_sha512(&drbg->key, drbg->V, DRBG_STATE_LEN, drbg->V);

		/* 10.1.2.5 step 4.2 */
		memcpy(out, drbg->V, n);
		out += n;
		outlen -= n;
	}

	/* 10.1.2.5 step 6 */
	drbg_hmac_update(drbg, addtl1, addtl1_len, addtl2, addtl2_len);

	memzero_explicit(addtl2, sizeof(addtl2));
}

/*
 * Seeding or reseeding of the DRBG
 *
 * @drbg: DRBG state struct
 * @pers: personalization / additional information buffer
 * @pers_len: length of @pers in bytes
 * @reseed: false for initial seeding (instantiation), true for reseeding
 *
 * return:
 *	0 on success
 *	error value otherwise
 */
static int drbg_seed(struct drbg_state *drbg, const u8 *pers, size_t pers_len,
		     bool reseed)
	__must_hold(&drbg->drbg_mutex)
{
	int ret;
	u8 entropy_buf[(32 + 16) * 2];
	size_t entropylen;
	const u8 *entropy;

	/* 9.1 / 9.2 / 9.3.1 step 3 */
	if (pers_len > DRBG_MAX_ADDTL_BYTES) {
		pr_devel("DRBG: personalization string too long %zu\n",
			 pers_len);
		return -EINVAL;
	}

	if (drbg->test_entropylen) {
		entropy = drbg->test_entropy;
		entropylen = drbg->test_entropylen;
		pr_devel("DRBG: using test entropy\n");
	} else {
		/*
		 * Gather entropy equal to the security strength of the DRBG.
		 * With a derivation function, a nonce is required in addition
		 * to the entropy. A nonce must be at least 1/2 of the security
		 * strength of the DRBG in size. Thus, entropy + nonce is 3/2
		 * of the strength. The consideration of a nonce is only
		 * applicable during initial seeding.
		 */
		entropy = entropy_buf;
		if (!reseed)
			entropylen = ((DRBG_SEC_STRENGTH + 1) / 2) * 3;
		else
			entropylen = DRBG_SEC_STRENGTH;
		BUG_ON(entropylen * 2 > sizeof(entropy_buf));

		/* Get seed from in-kernel /dev/urandom */
		get_random_bytes(entropy_buf, entropylen);

		if (!drbg->jent) {
			pr_devel("DRBG: (re)seeding with %zu bytes of entropy\n",
				 entropylen);
		} else {
			/*
			 * Get seed from Jitter RNG, failures are
			 * fatal only in FIPS mode.
			 */
			ret = crypto_rng_get_bytes(drbg->jent,
						   &entropy_buf[entropylen],
						   entropylen);
			if (fips_enabled && ret) {
				pr_devel("DRBG: jent failed with %d\n", ret);

				/*
				 * Do not treat the transient failure of the
				 * Jitter RNG as an error that needs to be
				 * reported. The combined number of the
				 * maximum reseed threshold times the maximum
				 * number of Jitter RNG transient errors is
				 * less than the reseed threshold required by
				 * SP800-90A allowing us to treat the
				 * transient errors as such.
				 *
				 * However, we mandate that at least the first
				 * seeding operation must succeed with the
				 * Jitter RNG.
				 */
				if (!reseed || ret != -EAGAIN)
					goto out;
			}

			entropylen *= 2;
			pr_devel("DRBG: (re)seeding with %zu bytes of entropy\n",
				 entropylen);
		}
	}

	if (pers_len)
		pr_devel("DRBG: using personalization string\n");

	drbg_hmac_update(drbg, entropy, entropylen, pers, pers_len);
	drbg->reseed_ctr = 1;
	ret = 0;
out:
	memzero_explicit(entropy_buf, sizeof(entropy_buf));

	return ret;
}

/*
 * Generate random bytes from an SP800-90A DRBG.
 *
 * @drbg DRBG state handle
 * @out Buffer where to store the random bytes
 * @outlen Number of random bytes to generate
 * @addtl Optional additional input that is mixed into state
 * @addtl_len Length of @addtl in bytes, may be 0
 *
 * return: 0 when all bytes are generated; < 0 in case of an error
 */
static int drbg_generate(struct drbg_state *drbg, u8 *out, size_t outlen,
			 const u8 *addtl, size_t addtl_len)
	__must_hold(&drbg->drbg_mutex)
{
	int err;

	if (!drbg->instantiated) {
		pr_devel("DRBG: not yet instantiated\n");
		return -EINVAL;
	}
	if (out == NULL || outlen == 0) {
		pr_devel("DRBG: no output buffer provided\n");
		return -EINVAL;
	}
	if (addtl == NULL && addtl_len != 0) {
		pr_devel("DRBG: wrong format of additional information\n");
		return -EINVAL;
	}

	/* 9.3.1 step 2 */
	if (outlen > DRBG_MAX_REQUEST_BYTES) {
		pr_devel("DRBG: request length is too long %zu\n", outlen);
		return -EINVAL;
	}

	/* 9.3.1 step 3 is implicit with the chosen DRBG */

	/* 9.3.1 step 4 */
	if (addtl_len > DRBG_MAX_ADDTL_BYTES) {
		pr_devel("DRBG: additional information string too long %zu\n",
			 addtl_len);
		return -EINVAL;
	}
	/* 9.3.1 step 5 is implicit with the chosen DRBG */

	/*
	 * 9.3.1 step 6 and 9 supplemented by 9.3.2 step c is implemented
	 * here. The spec is a bit convoluted here, we make it simpler.
	 *
	 * We no longer try to detect when random.c has reseeded itself and call
	 * drbg_seed() then too, since drbg_hmac_generate() adds bytes from
	 * random.c to the additional input, which is a de facto reseed anyway.
	 */
	if (drbg->reseed_ctr > DRBG_MAX_REQUESTS) {
		pr_devel("DRBG: reseeding before generation\n");
		/* 9.3.1 steps 7.1 through 7.3 */
		err = drbg_seed(drbg, addtl, addtl_len, true);
		if (err)
			return err;
		/* 9.3.1 step 7.4 */
		addtl = NULL;
		addtl_len = 0;
	}

	/* 9.3.1 step 8 and 10 */
	drbg_hmac_generate(drbg, out, outlen, addtl, addtl_len);

	/* 10.1.2.5 step 7 */
	drbg->reseed_ctr++;

	/*
	 * Section 11.3.3 requires to re-perform self tests after some
	 * generated random numbers. The chosen value after which self
	 * test is performed is arbitrary, but it should be reasonable.
	 * However, we do not perform the self tests because of the following
	 * reasons: it is mathematically impossible that the initial self tests
	 * were successfully and the following are not. If the initial would
	 * pass and the following would not, the kernel integrity is violated.
	 * In this case, the entire kernel operation is questionable and it
	 * is unlikely that the integrity violation only affects the
	 * correct operation of the DRBG.
	 */

	return 0;
}

/***************************************************************
 * Kernel crypto API interface to DRBG
 ***************************************************************/

static int drbg_kcapi_init(struct crypto_tfm *tfm)
{
	struct drbg_state *drbg = crypto_tfm_ctx(tfm);

	mutex_init(&drbg->drbg_mutex);

	return 0;
}

/* Set test entropy in the DRBG. */
static void drbg_kcapi_set_entropy(struct crypto_rng *tfm,
				   const u8 *data, unsigned int len)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);

	mutex_lock(&drbg->drbg_mutex);
	drbg->test_entropy = data;
	drbg->test_entropylen = len;
	mutex_unlock(&drbg->drbg_mutex);
}

/* Seed (i.e. instantiate) or re-seed the DRBG. */
static int drbg_kcapi_seed(struct crypto_rng *tfm,
			   const u8 *pers, unsigned int pers_len)
{
	static const u8 initial_key[DRBG_STATE_LEN]; /* all zeroes */
	struct drbg_state *drbg = crypto_rng_ctx(tfm);
	int ret;

	pr_devel("DRBG: Initializing DRBG\n");
	guard(mutex)(&drbg->drbg_mutex);

	if (drbg->instantiated)
		return drbg_seed(drbg, pers, pers_len, /* reseed= */ true);

	/* 9.1 step 1 is implicit with the selected DRBG type */

	/*
	 * 9.1 step 2 is implicit, as this implementation doesn't support
	 * prediction resistance
	 */

	/* 9.1 step 4 is implicit in DRBG_SEC_STRENGTH */

	memset(drbg->V, 1, DRBG_STATE_LEN);
	hmac_sha512_preparekey(&drbg->key, initial_key, DRBG_STATE_LEN);

	/* Allocate jitterentropy_rng if not in test mode. */
	if (drbg->test_entropylen == 0) {
		drbg->jent = crypto_alloc_rng("jitterentropy_rng", 0, 0);
		if (IS_ERR(drbg->jent)) {
			ret = PTR_ERR(drbg->jent);
			drbg->jent = NULL;
			if (fips_enabled)
				return ret;
			pr_info("DRBG: Continuing without Jitter RNG\n");
		}
	}

	ret = drbg_seed(drbg, pers, pers_len, /* reseed= */ false);
	if (ret) {
		crypto_free_rng(drbg->jent);
		drbg->jent = NULL;
		return ret;
	}
	drbg->instantiated = true;
	return 0;
}

/*
 * Generate random numbers invoked by the kernel crypto API:
 *
 * src is additional input supplied to the RNG.
 * slen is the length of src.
 * dst is the output buffer where random data is to be stored.
 * dlen is the length of dst.
 */
static int drbg_kcapi_generate(struct crypto_rng *tfm,
			       const u8 *src, unsigned int slen,
			       u8 *dst, unsigned int dlen)
{
	struct drbg_state *drbg = crypto_rng_ctx(tfm);

	/*
	 * Break the request into multiple requests if needed, to avoid
	 * exceeding the maximum request length of the core algorithm.
	 */
	do {
		unsigned int n = min(dlen, DRBG_MAX_REQUEST_BYTES);
		int err;

		mutex_lock(&drbg->drbg_mutex);
		err = drbg_generate(drbg, dst, n, src, slen);
		mutex_unlock(&drbg->drbg_mutex);
		if (err < 0)
			return err;
		dst += n;
		dlen -= n;
	} while (dlen);
	return 0;
}

/* Uninstantiate the DRBG. */
static void drbg_kcapi_exit(struct crypto_tfm *tfm)
{
	struct drbg_state *drbg = crypto_tfm_ctx(tfm);

	crypto_free_rng(drbg->jent);
	memzero_explicit(drbg, sizeof(*drbg));
}

/*
 * Tests as defined in 11.3.2 in addition to the cipher tests: testing
 * of the error handling.
 *
 * Note: testing of failing seed source as defined in 11.3.2 is not applicable
 * as seed source of get_random_bytes does not fail.
 *
 * Note 2: There is no sensible way of testing the reseed counter
 * enforcement, so skip it.
 */
static inline int __init drbg_healthcheck_sanity(void)
{
#define OUTBUFLEN 16
	u8 buf[OUTBUFLEN];
	struct drbg_state *drbg = NULL;
	int ret;

	/* only perform test in FIPS mode */
	if (!fips_enabled)
		return 0;

	drbg = kzalloc_obj(struct drbg_state);
	if (!drbg)
		return -ENOMEM;

	guard(mutex_init)(&drbg->drbg_mutex);
	drbg->instantiated = true;

	/*
	 * if the following tests fail, it is likely that there is a buffer
	 * overflow as buf is much smaller than the requested or provided
	 * string lengths -- in case the error handling does not succeed
	 * we may get an OOPS. And we want to get an OOPS as this is a
	 * grave bug.
	 */

	/* overflow addtllen with additional info string */
	ret = drbg_generate(drbg, buf, OUTBUFLEN, buf,
			    DRBG_MAX_ADDTL_BYTES + 1);
	BUG_ON(ret == 0);
	/* overflow max_bits */
	ret = drbg_generate(drbg, buf, DRBG_MAX_REQUEST_BYTES + 1, NULL, 0);
	BUG_ON(ret == 0);

	/* overflow max addtllen with personalization string */
	ret = drbg_seed(drbg, buf, DRBG_MAX_ADDTL_BYTES + 1, false);
	BUG_ON(ret == 0);
	/* all tests passed */

	pr_devel("DRBG: Sanity tests for failure code paths successfully "
		 "completed\n");

	kfree(drbg);
	return 0;
}

static struct rng_alg drbg_alg = {
	.base.cra_name		= "stdrng",
	.base.cra_driver_name	= "drbg_nopr_hmac_sha512",
	.base.cra_priority	= 201,
	.base.cra_ctxsize	= sizeof(struct drbg_state),
	.base.cra_module	= THIS_MODULE,
	.base.cra_init		= drbg_kcapi_init,
	.set_ent		= drbg_kcapi_set_entropy,
	.seed			= drbg_kcapi_seed,
	.generate		= drbg_kcapi_generate,
	.base.cra_exit		= drbg_kcapi_exit,
};

static int __init drbg_init(void)
{
	int ret;

	ret = drbg_healthcheck_sanity();
	if (ret)
		return ret;

	/*
	 * In FIPS mode, boost the algorithm priority to ensure that when users
	 * request "stdrng", they really get the algorithm from here.
	 */
	if (fips_enabled)
		drbg_alg.base.cra_priority += 2000;

	return crypto_register_rng(&drbg_alg);
}

static void __exit drbg_exit(void)
{
	crypto_unregister_rng(&drbg_alg);
}

module_init(drbg_init);
module_exit(drbg_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("NIST SP800-90A Deterministic Random Bit Generator (DRBG)");
MODULE_ALIAS_CRYPTO("stdrng");
MODULE_ALIAS_CRYPTO("drbg_nopr_hmac_sha512");
