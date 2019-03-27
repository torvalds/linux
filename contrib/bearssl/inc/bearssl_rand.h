/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef BR_BEARSSL_RAND_H__
#define BR_BEARSSL_RAND_H__

#include <stddef.h>
#include <stdint.h>

#include "bearssl_block.h"
#include "bearssl_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file bearssl_rand.h
 *
 * # Pseudo-Random Generators
 *
 * A PRNG is a state-based engine that outputs pseudo-random bytes on
 * demand. It is initialized with an initial seed, and additional seed
 * bytes can be added afterwards. Bytes produced depend on the seeds and
 * also on the exact sequence of calls (including sizes requested for
 * each call).
 *
 *
 * ## Procedural and OOP API
 *
 * For the PRNG of name "`xxx`", two API are provided. The _procedural_
 * API defined a context structure `br_xxx_context` and three functions:
 *
 *   - `br_xxx_init()`
 *
 *     Initialise the context with an initial seed.
 *
 *   - `br_xxx_generate()`
 *
 *     Produce some pseudo-random bytes.
 *
 *   - `br_xxx_update()`
 *
 *     Inject some additional seed.
 *
 * The initialisation function sets the first context field (`vtable`)
 * to a pointer to the vtable that supports the OOP API. The OOP API
 * provides access to the same functions through function pointers,
 * named `init()`, `generate()` and `update()`.
 *
 * Note that the context initialisation method may accept additional
 * parameters, provided as a 'const void *' pointer at API level. These
 * additional parameters depend on the implemented PRNG.
 *
 *
 * ## HMAC_DRBG
 *
 * HMAC_DRBG is defined in [NIST SP 800-90A Revision
 * 1](http://nvlpubs.nist.gov/nistpubs/SpecialPublications/NIST.SP.800-90Ar1.pdf).
 * It uses HMAC repeatedly, over some configurable underlying hash
 * function. In BearSSL, it is implemented under the "`hmac_drbg`" name.
 * The "extra parameters" pointer for context initialisation should be
 * set to a pointer to the vtable for the underlying hash function (e.g.
 * pointer to `br_sha256_vtable` to use HMAC_DRBG with SHA-256).
 *
 * According to the NIST standard, each request shall produce up to
 * 2<sup>19</sup> bits (i.e. 64 kB of data); moreover, the context shall
 * be reseeded at least once every 2<sup>48</sup> requests. This
 * implementation does not maintain the reseed counter (the threshold is
 * too high to be reached in practice) and does not object to producing
 * more than 64 kB in a single request; thus, the code cannot fail,
 * which corresponds to the fact that the API has no room for error
 * codes. However, this implies that requesting more than 64 kB in one
 * `generate()` request, or making more than 2<sup>48</sup> requests
 * without reseeding, is formally out of NIST specification. There is
 * no currently known security penalty for exceeding the NIST limits,
 * and, in any case, HMAC_DRBG usage in implementing SSL/TLS always
 * stays much below these thresholds.
 *
 *
 * ## AESCTR_DRBG
 *
 * AESCTR_DRBG is a custom PRNG based on AES-128 in CTR mode. This is
 * meant to be used only in situations where you are desperate for
 * speed, and have an hardware-optimized AES/CTR implementation. Whether
 * this will yield perceptible improvements depends on what you use the
 * pseudorandom bytes for, and how many you want; for instance, RSA key
 * pair generation uses a substantial amount of randomness, and using
 * AESCTR_DRBG instead of HMAC_DRBG yields a 15 to 20% increase in key
 * generation speed on a recent x86 CPU (Intel Core i7-6567U at 3.30 GHz).
 *
 * Internally, it uses CTR mode with successive counter values, starting
 * at zero (counter value expressed over 128 bits, big-endian convention).
 * The counter is not allowed to reach 32768; thus, every 32768*16 bytes
 * at most, the `update()` function is run (on an empty seed, if none is
 * provided). The `update()` function computes the new AES-128 key by
 * applying a custom hash function to the concatenation of a state-dependent
 * word (encryption of an all-one block with the current key) and the new
 * seed. The custom hash function uses Hirose's construction over AES-256;
 * see the comments in `aesctr_drbg.c` for details.
 *
 * This DRBG does not follow an existing standard, and thus should be
 * considered as inadequate for production use until it has been properly
 * analysed.
 */

/**
 * \brief Class type for PRNG implementations.
 *
 * A `br_prng_class` instance references the methods implementing a PRNG.
 * Constant instances of this structure are defined for each implemented
 * PRNG. Such instances are also called "vtables".
 */
typedef struct br_prng_class_ br_prng_class;
struct br_prng_class_ {
	/**
	 * \brief Size (in bytes) of the context structure appropriate for
	 * running this PRNG.
	 */
	size_t context_size;

	/**
	 * \brief Initialisation method.
	 *
	 * The context to initialise is provided as a pointer to its
	 * first field (the vtable pointer); this function sets that
	 * first field to a pointer to the vtable.
	 *
	 * The extra parameters depend on the implementation; each
	 * implementation defines what kind of extra parameters it
	 * expects (if any).
	 *
	 * Requirements on the initial seed depend on the implemented
	 * PRNG.
	 *
	 * \param ctx        PRNG context to initialise.
	 * \param params     extra parameters for the PRNG.
	 * \param seed       initial seed.
	 * \param seed_len   initial seed length (in bytes).
	 */
	void (*init)(const br_prng_class **ctx, const void *params,
		const void *seed, size_t seed_len);

	/**
	 * \brief Random bytes generation.
	 *
	 * This method produces `len` pseudorandom bytes, in the `out`
	 * buffer. The context is updated accordingly.
	 *
	 * \param ctx   PRNG context.
	 * \param out   output buffer.
	 * \param len   number of pseudorandom bytes to produce.
	 */
	void (*generate)(const br_prng_class **ctx, void *out, size_t len);

	/**
	 * \brief Inject additional seed bytes.
	 *
	 * The provided seed bytes are added into the PRNG internal
	 * entropy pool.
	 *
	 * \param ctx        PRNG context.
	 * \param seed       additional seed.
	 * \param seed_len   additional seed length (in bytes).
	 */
	void (*update)(const br_prng_class **ctx,
		const void *seed, size_t seed_len);
};

/**
 * \brief Context for HMAC_DRBG.
 *
 * The context contents are opaque, except the first field, which
 * supports OOP.
 */
typedef struct {
	/**
	 * \brief Pointer to the vtable.
	 *
	 * This field is set with the initialisation method/function.
	 */
	const br_prng_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	unsigned char K[64];
	unsigned char V[64];
	const br_hash_class *digest_class;
#endif
} br_hmac_drbg_context;

/**
 * \brief Statically allocated, constant vtable for HMAC_DRBG.
 */
extern const br_prng_class br_hmac_drbg_vtable;

/**
 * \brief HMAC_DRBG initialisation.
 *
 * The context to initialise is provided as a pointer to its first field
 * (the vtable pointer); this function sets that first field to a
 * pointer to the vtable.
 *
 * The `seed` value is what is called, in NIST terminology, the
 * concatenation of the "seed", "nonce" and "personalization string", in
 * that order.
 *
 * The `digest_class` parameter defines the underlying hash function.
 * Formally, the NIST standard specifies that the hash function shall
 * be only SHA-1 or one of the SHA-2 functions. This implementation also
 * works with any other implemented hash function (such as MD5), but
 * this is non-standard and therefore not recommended.
 *
 * \param ctx            HMAC_DRBG context to initialise.
 * \param digest_class   vtable for the underlying hash function.
 * \param seed           initial seed.
 * \param seed_len       initial seed length (in bytes).
 */
void br_hmac_drbg_init(br_hmac_drbg_context *ctx,
	const br_hash_class *digest_class, const void *seed, size_t seed_len);

/**
 * \brief Random bytes generation with HMAC_DRBG.
 *
 * This method produces `len` pseudorandom bytes, in the `out`
 * buffer. The context is updated accordingly. Formally, requesting
 * more than 65536 bytes in one request falls out of specification
 * limits (but it won't fail).
 *
 * \param ctx   HMAC_DRBG context.
 * \param out   output buffer.
 * \param len   number of pseudorandom bytes to produce.
 */
void br_hmac_drbg_generate(br_hmac_drbg_context *ctx, void *out, size_t len);

/**
 * \brief Inject additional seed bytes in HMAC_DRBG.
 *
 * The provided seed bytes are added into the HMAC_DRBG internal
 * entropy pool. The process does not _replace_ existing entropy,
 * thus pushing non-random bytes (i.e. bytes which are known to the
 * attackers) does not degrade the overall quality of generated bytes.
 *
 * \param ctx        HMAC_DRBG context.
 * \param seed       additional seed.
 * \param seed_len   additional seed length (in bytes).
 */
void br_hmac_drbg_update(br_hmac_drbg_context *ctx,
	const void *seed, size_t seed_len);

/**
 * \brief Get the hash function implementation used by a given instance of
 * HMAC_DRBG.
 *
 * This calls MUST NOT be performed on a context which was not
 * previously initialised.
 *
 * \param ctx   HMAC_DRBG context.
 * \return  the hash function vtable.
 */
static inline const br_hash_class *
br_hmac_drbg_get_hash(const br_hmac_drbg_context *ctx)
{
	return ctx->digest_class;
}

/**
 * \brief Type for a provider of entropy seeds.
 *
 * A "seeder" is a function that is able to obtain random values from
 * some source and inject them as entropy seed in a PRNG. A seeder
 * shall guarantee that the total entropy of the injected seed is large
 * enough to seed a PRNG for purposes of cryptographic key generation
 * (i.e. at least 128 bits).
 *
 * A seeder may report a failure to obtain adequate entropy. Seeders
 * shall endeavour to fix themselves transient errors by trying again;
 * thus, callers may consider reported errors as permanent.
 *
 * \param ctx   PRNG context to seed.
 * \return  1 on success, 0 on error.
 */
typedef int (*br_prng_seeder)(const br_prng_class **ctx);

/**
 * \brief Get a seeder backed by the operating system or hardware.
 *
 * Get a seeder that feeds on RNG facilities provided by the current
 * operating system or hardware. If no such facility is known, then 0
 * is returned.
 *
 * If `name` is not `NULL`, then `*name` is set to a symbolic string
 * that identifies the seeder implementation. If no seeder is returned
 * and `name` is not `NULL`, then `*name` is set to a pointer to the
 * constant string `"none"`.
 *
 * \param name   receiver for seeder name, or `NULL`.
 * \return  the system seeder, if available, or 0.
 */
br_prng_seeder br_prng_seeder_system(const char **name);

/**
 * \brief Context for AESCTR_DRBG.
 *
 * The context contents are opaque, except the first field, which
 * supports OOP.
 */
typedef struct {
	/**
	 * \brief Pointer to the vtable.
	 *
	 * This field is set with the initialisation method/function.
	 */
	const br_prng_class *vtable;
#ifndef BR_DOXYGEN_IGNORE
	br_aes_gen_ctr_keys sk;
	uint32_t cc;
#endif
} br_aesctr_drbg_context;

/**
 * \brief Statically allocated, constant vtable for AESCTR_DRBG.
 */
extern const br_prng_class br_aesctr_drbg_vtable;

/**
 * \brief AESCTR_DRBG initialisation.
 *
 * The context to initialise is provided as a pointer to its first field
 * (the vtable pointer); this function sets that first field to a
 * pointer to the vtable.
 *
 * The internal AES key is first set to the all-zero key; then, the
 * `br_aesctr_drbg_update()` function is called with the provided `seed`.
 * The call is performed even if the seed length (`seed_len`) is zero.
 *
 * The `aesctr` parameter defines the underlying AES/CTR implementation.
 *
 * \param ctx        AESCTR_DRBG context to initialise.
 * \param aesctr     vtable for the AES/CTR implementation.
 * \param seed       initial seed (can be `NULL` if `seed_len` is zero).
 * \param seed_len   initial seed length (in bytes).
 */
void br_aesctr_drbg_init(br_aesctr_drbg_context *ctx,
	const br_block_ctr_class *aesctr, const void *seed, size_t seed_len);

/**
 * \brief Random bytes generation with AESCTR_DRBG.
 *
 * This method produces `len` pseudorandom bytes, in the `out`
 * buffer. The context is updated accordingly.
 *
 * \param ctx   AESCTR_DRBG context.
 * \param out   output buffer.
 * \param len   number of pseudorandom bytes to produce.
 */
void br_aesctr_drbg_generate(br_aesctr_drbg_context *ctx,
	void *out, size_t len);

/**
 * \brief Inject additional seed bytes in AESCTR_DRBG.
 *
 * The provided seed bytes are added into the AESCTR_DRBG internal
 * entropy pool. The process does not _replace_ existing entropy,
 * thus pushing non-random bytes (i.e. bytes which are known to the
 * attackers) does not degrade the overall quality of generated bytes.
 *
 * \param ctx        AESCTR_DRBG context.
 * \param seed       additional seed.
 * \param seed_len   additional seed length (in bytes).
 */
void br_aesctr_drbg_update(br_aesctr_drbg_context *ctx,
	const void *seed, size_t seed_len);

#ifdef __cplusplus
}
#endif

#endif
