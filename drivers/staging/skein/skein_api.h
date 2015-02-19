/*
Copyright (c) 2010 Werner Dittmann

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

*/

#ifndef SKEINAPI_H
#define SKEINAPI_H

/**
 * @file skein_api.h
 * @brief A Skein API and its functions.
 * @{
 *
 * This API and the functions that implement this API simplify the usage
 * of Skein. The design and the way to use the functions follow the openSSL
 * design but at the same time take care of some Skein specific behaviour
 * and possibilities.
 *
 * The functions enable applications to create a normal Skein hashes and
 * message authentication codes (MAC).
 *
 * Using these functions is simple and straight forward:
 *
 * @code
 *
 * #include "skein_api.h"
 *
 * ...
 * struct skein_ctx ctx;             // a Skein hash or MAC context
 *
 * // prepare context, here for a Skein with a state size of 512 bits.
 * skein_ctx_prepare(&ctx, SKEIN_512);
 *
 * // Initialize the context to set the requested hash length in bits
 * // here request a output hash size of 31 bits (Skein supports variable
 * // output sizes even very strange sizes)
 * skein_init(&ctx, 31);
 *
 * // Now update Skein with any number of message bits. A function that
 * // takes a number of bytes is also available.
 * skein_update_bits(&ctx, message, msg_length);
 *
 * // Now get the result of the Skein hash. The output buffer must be
 * // large enough to hold the request number of output bits. The application
 * // may now extract the bits.
 * skein_final(&ctx, result);
 * ...
 * @endcode
 *
 * An application may use @c skein_reset to reset a Skein context and use
 * it for creation of another hash with the same Skein state size and output
 * bit length. In this case the API implementation restores some internal
 * internal state data and saves a full Skein initialization round.
 *
 * To create a MAC the application just uses @c skein_mac_init instead of
 * @c skein_init. All other functions calls remain the same.
 *
 */

#include <linux/types.h>
#include "skein_base.h"

/**
 * Which Skein size to use
 */
enum skein_size {
	SKEIN_256 = 256,     /*!< Skein with 256 bit state */
	SKEIN_512 = 512,     /*!< Skein with 512 bit state */
	SKEIN_1024 = 1024    /*!< Skein with 1024 bit state */
};

/**
 * Context for Skein.
 *
 * This structure was setup with some know-how of the internal
 * Skein structures, in particular ordering of header and size dependent
 * variables. If Skein implementation changes this, then adapt these
 * structures as well.
 */
struct skein_ctx {
	u64 skein_size;
	u64 x_save[SKEIN_MAX_STATE_WORDS];   /* save area for state variables */
	union {
		struct skein_ctx_hdr h;
		struct skein_256_ctx s256;
		struct skein_512_ctx s512;
		struct skein_1024_ctx s1024;
	} m;
};

/**
 * Prepare a Skein context.
 *
 * An application must call this function before it can use the Skein
 * context. The functions clears memory and initializes size dependent
 * variables.
 *
 * @param ctx
 *     Pointer to a Skein context.
 * @param size
 *     Which Skein size to use.
 * @return
 *     SKEIN_SUCESS of SKEIN_FAIL
 */
int skein_ctx_prepare(struct skein_ctx *ctx, enum skein_size size);

/**
 * Initialize a Skein context.
 *
 * Initializes the context with this data and saves the resulting Skein
 * state variables for further use.
 *
 * @param ctx
 *     Pointer to a Skein context.
 * @param hash_bit_len
 *     Number of MAC hash bits to compute
 * @return
 *     SKEIN_SUCESS of SKEIN_FAIL
 * @see skein_reset
 */
int skein_init(struct skein_ctx *ctx, size_t hash_bit_len);

/**
 * Resets a Skein context for further use.
 *
 * Restores the saved chaining variables to reset the Skein context.
 * Thus applications can reuse the same setup to  process several
 * messages. This saves a complete Skein initialization cycle.
 *
 * @param ctx
 *     Pointer to a pre-initialized Skein MAC context
 */
void skein_reset(struct skein_ctx *ctx);

/**
 * Initializes a Skein context for MAC usage.
 *
 * Initializes the context with this data and saves the resulting Skein
 * state variables for further use.
 *
 * Applications call the normal Skein functions to update the MAC and
 * get the final result.
 *
 * @param ctx
 *     Pointer to an empty or preinitialized Skein MAC context
 * @param key
 *     Pointer to key bytes or NULL
 * @param key_len
 *     Length of the key in bytes or zero
 * @param hash_bit_len
 *     Number of MAC hash bits to compute
 * @return
 *     SKEIN_SUCESS of SKEIN_FAIL
 */
int skein_mac_init(struct skein_ctx *ctx, const u8 *key, size_t key_len,
		   size_t hash_bit_len);

/**
 * Update Skein with the next part of the message.
 *
 * @param ctx
 *     Pointer to initialized Skein context
 * @param msg
 *     Pointer to the message.
 * @param msg_byte_cnt
 *     Length of the message in @b bytes
 * @return
 *     Success or error code.
 */
int skein_update(struct skein_ctx *ctx, const u8 *msg,
		 size_t msg_byte_cnt);

/**
 * Update the hash with a message bit string.
 *
 * Skein can handle data not only as bytes but also as bit strings of
 * arbitrary length (up to its maximum design size).
 *
 * @param ctx
 *     Pointer to initialized Skein context
 * @param msg
 *     Pointer to the message.
 * @param msg_bit_cnt
 *     Length of the message in @b bits.
 */
int skein_update_bits(struct skein_ctx *ctx, const u8 *msg,
		      size_t msg_bit_cnt);

/**
 * Finalize Skein and return the hash.
 *
 * Before an application can reuse a Skein setup the application must
 * reset the Skein context.
 *
 * @param ctx
 *     Pointer to initialized Skein context
 * @param hash
 *     Pointer to buffer that receives the hash. The buffer must be large
 *     enough to store @c hash_bit_len bits.
 * @return
 *     Success or error code.
 * @see skein_reset
 */
int skein_final(struct skein_ctx *ctx, u8 *hash);

/**
 * @}
 */
#endif
