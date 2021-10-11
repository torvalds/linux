/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 Google LLC
 *
 * This file is automatically included by all files built into fips140.ko, via
 * the "-include" compiler flag.  It redirects all calls to algorithm
 * registration functions to the wrapper functions defined within the module.
 */

#define aead_register_instance		fips140_aead_register_instance
#define ahash_register_instance		fips140_ahash_register_instance
#define crypto_register_aead		fips140_crypto_register_aead
#define crypto_register_aeads		fips140_crypto_register_aeads
#define crypto_register_ahash		fips140_crypto_register_ahash
#define crypto_register_ahashes		fips140_crypto_register_ahashes
#define crypto_register_alg		fips140_crypto_register_alg
#define crypto_register_algs		fips140_crypto_register_algs
#define crypto_register_rng		fips140_crypto_register_rng
#define crypto_register_rngs		fips140_crypto_register_rngs
#define crypto_register_shash		fips140_crypto_register_shash
#define crypto_register_shashes		fips140_crypto_register_shashes
#define crypto_register_skcipher	fips140_crypto_register_skcipher
#define crypto_register_skciphers	fips140_crypto_register_skciphers
#define shash_register_instance		fips140_shash_register_instance
#define skcipher_register_instance	fips140_skcipher_register_instance
