/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 Google LLC
 *
 * This file is automatically included by all files built into fips140.ko, via
 * the "-include" compiler flag.
 */

/*
 * fips140.ko is built from various unmodified or minimally modified kernel
 * source files, many of which are normally meant to be buildable into different
 * modules themselves.  That results in conflicting instances of module_init()
 * and related macros such as MODULE_LICENSE().
 *
 * To solve that, we undefine MODULE to trick the kernel headers into thinking
 * the code is being compiled as built-in.  That causes module_init() and
 * related macros to be expanded as they would be for built-in code; e.g.,
 * module_init() adds the function to the .initcalls section of the binary.
 *
 * The .c file that contains the real module_init() for fips140.ko is then
 * responsible for redefining MODULE, and the real module_init() is responsible
 * for executing all the initcalls that were collected into .initcalls.
 */
#undef MODULE

/*
 * Defining KBUILD_MODFILE is also required, since the kernel headers expect it
 * to be defined when code that can be a module is compiled as built-in.
 */
#define KBUILD_MODFILE "crypto/fips140"

/*
 * Disable symbol exports by default.  fips140.ko includes various files that
 * use EXPORT_SYMBOL*(), but it's unwanted to export any symbols from fips140.ko
 * except where explicitly needed for FIPS certification reasons.
 */
#define __DISABLE_EXPORTS

/*
 * Redirect all calls to algorithm registration functions to the wrapper
 * functions defined within the module.
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
