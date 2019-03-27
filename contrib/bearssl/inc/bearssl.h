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

#ifndef BR_BEARSSL_H__
#define BR_BEARSSL_H__

#include <stddef.h>
#include <stdint.h>

/** \mainpage BearSSL API
 *
 * # API Layout
 *
 * The functions and structures defined by the BearSSL API are located
 * in various header files:
 *
 * | Header file     | Elements                                          |
 * | :-------------- | :------------------------------------------------ |
 * | bearssl_hash.h  | Hash functions                                    |
 * | bearssl_hmac.h  | HMAC                                              |
 * | bearssl_kdf.h   | Key Derivation Functions                          |
 * | bearssl_rand.h  | Pseudorandom byte generators                      |
 * | bearssl_prf.h   | PRF implementations (for SSL/TLS)                 |
 * | bearssl_block.h | Symmetric encryption                              |
 * | bearssl_aead.h  | AEAD algorithms (combined encryption + MAC)       |
 * | bearssl_rsa.h   | RSA encryption and signatures                     |
 * | bearssl_ec.h    | Elliptic curves support (including ECDSA)         |
 * | bearssl_ssl.h   | SSL/TLS engine interface                          |
 * | bearssl_x509.h  | X.509 certificate decoding and validation         |
 * | bearssl_pem.h   | Base64/PEM decoding support functions             |
 *
 * Applications using BearSSL are supposed to simply include `bearssl.h`
 * as follows:
 *
 *     #include <bearssl.h>
 *
 * The `bearssl.h` file itself includes all the other header files. It is
 * possible to include specific header files, but it has no practical
 * advantage for the application. The API is separated into separate
 * header files only for documentation convenience.
 *
 *
 * # Conventions
 *
 * ## MUST and SHALL
 *
 * In all descriptions, the usual "MUST", "SHALL", "MAY",... terminology
 * is used. Failure to meet requirements expressed with a "MUST" or
 * "SHALL" implies undefined behaviour, which means that segmentation
 * faults, buffer overflows, and other similar adverse events, may occur.
 *
 * In general, BearSSL is not very forgiving of programming errors, and
 * does not include much failsafes or error reporting when the problem
 * does not arise from external transient conditions, and can be fixed
 * only in the application code. This is done so in order to make the
 * total code footprint lighter.
 *
 *
 * ## `NULL` values
 *
 * Function parameters with a pointer type shall not be `NULL` unless
 * explicitly authorised by the documentation. As an exception, when
 * the pointer aims at a sequence of bytes and is accompanied with
 * a length parameter, and the length is zero (meaning that there is
 * no byte at all to retrieve), then the pointer may be `NULL` even if
 * not explicitly allowed.
 *
 *
 * ## Memory Allocation
 *
 * BearSSL does not perform dynamic memory allocation. This implies that
 * for any functionality that requires a non-transient state, the caller
 * is responsible for allocating the relevant context structure. Such
 * allocation can be done in any appropriate area, including static data
 * segments, the heap, and the stack, provided that proper alignment is
 * respected. The header files define these context structures
 * (including size and contents), so the C compiler should handle
 * alignment automatically.
 *
 * Since there is no dynamic resource allocation, there is also nothing to
 * release. When the calling code is done with a BearSSL feature, it
 * may simple release the context structures it allocated itself, with
 * no "close function" to call. If the context structures were allocated
 * on the stack (as local variables), then even that release operation is
 * implicit.
 *
 *
 * ## Structure Contents
 *
 * Except when explicitly indicated, structure contents are opaque: they
 * are included in the header files so that calling code may know the
 * structure sizes and alignment requirements, but callers SHALL NOT
 * access individual fields directly. For fields that are supposed to
 * be read from or written to, the API defines accessor functions (the
 * simplest of these accessor functions are defined as `static inline`
 * functions, and the C compiler will optimise them away).
 *
 *
 * # API Usage
 *
 * BearSSL usage for running a SSL/TLS client or server is described
 * on the [BearSSL Web site](https://www.bearssl.org/api1.html). The
 * BearSSL source archive also comes with sample code.
 */

#include "bearssl_hash.h"
#include "bearssl_hmac.h"
#include "bearssl_kdf.h"
#include "bearssl_rand.h"
#include "bearssl_prf.h"
#include "bearssl_block.h"
#include "bearssl_aead.h"
#include "bearssl_rsa.h"
#include "bearssl_ec.h"
#include "bearssl_ssl.h"
#include "bearssl_x509.h"
#include "bearssl_pem.h"

/** \brief Type for a configuration option.
 *
 * A "configuration option" is a value that is selected when the BearSSL
 * library itself is compiled. Most options are boolean; their value is
 * then either 1 (option is enabled) or 0 (option is disabled). Some
 * values have other integer values. Option names correspond to macro
 * names. Some of the options can be explicitly set in the internal
 * `"config.h"` file.
 */
typedef struct {
	/** \brief Configurable option name. */
	const char *name;
	/** \brief Configurable option value. */
	long value;
} br_config_option;

/** \brief Get configuration report.
 *
 * This function returns compiled configuration options, each as a
 * 'long' value. Names match internal macro names, in particular those
 * that can be set in the `"config.h"` inner file. For boolean options,
 * the numerical value is 1 if enabled, 0 if disabled. For maximum
 * key sizes, values are expressed in bits.
 *
 * The returned array is terminated by an entry whose `name` is `NULL`.
 *
 * \return  the configuration report.
 */
const br_config_option *br_get_config(void);

#endif
