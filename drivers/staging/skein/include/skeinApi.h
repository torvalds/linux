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
 * @file skeinApi.h
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
 * #include <skeinApi.h>
 * 
 * ...
 * SkeinCtx_t ctx;             // a Skein hash or MAC context
 * 
 * // prepare context, here for a Skein with a state size of 512 bits.
 * skeinCtxPrepare(&ctx, Skein512);
 * 
 * // Initialize the context to set the requested hash length in bits
 * // here request a output hash size of 31 bits (Skein supports variable
 * // output sizes even very strange sizes)
 * skeinInit(&ctx, 31);
 * 
 * // Now update Skein with any number of message bits. A function that
 * // takes a number of bytes is also available.
 * skeinUpdateBits(&ctx, message, msgLength);
 * 
 * // Now get the result of the Skein hash. The output buffer must be
 * // large enough to hold the request number of output bits. The application
 * // may now extract the bits.
 * skeinFinal(&ctx, result);
 * ...
 * @endcode
 * 
 * An application may use @c skeinReset to reset a Skein context and use
 * it for creation of another hash with the same Skein state size and output
 * bit length. In this case the API implementation restores some internal
 * internal state data and saves a full Skein initialization round.
 * 
 * To create a MAC the application just uses @c skeinMacInit instead of 
 * @c skeinInit. All other functions calls remain the same.
 * 
 */

#include <skein.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Which Skein size to use
     */
    typedef enum SkeinSize {
        Skein256 = 256,     /*!< Skein with 256 bit state */
        Skein512 = 512,     /*!< Skein with 512 bit state */
        Skein1024 = 1024    /*!< Skein with 1024 bit state */
    } SkeinSize_t;

    /**
     * Context for Skein.
     *
     * This structure was setup with some know-how of the internal
     * Skein structures, in particular ordering of header and size dependent
     * variables. If Skein implementation changes this, then adapt these
     * structures as well.
     */
    typedef struct SkeinCtx {
        u64b_t skeinSize;
        u64b_t  XSave[SKEIN_MAX_STATE_WORDS];   /* save area for state variables */
        union {
            Skein_Ctxt_Hdr_t h;
            Skein_256_Ctxt_t s256;
            Skein_512_Ctxt_t s512;
            Skein1024_Ctxt_t s1024;
        } m;
    } SkeinCtx_t;

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
    int skeinCtxPrepare(SkeinCtx_t* ctx, SkeinSize_t size);

    /**
     * Initialize a Skein context.
     *
     * Initializes the context with this data and saves the resulting Skein 
     * state variables for further use.
     *
     * @param ctx
     *     Pointer to a Skein context.
     * @param hashBitLen
     *     Number of MAC hash bits to compute
     * @return
     *     SKEIN_SUCESS of SKEIN_FAIL
     * @see skeinReset
     */
    int skeinInit(SkeinCtx_t* ctx, size_t hashBitLen);

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
    void skeinReset(SkeinCtx_t* ctx);
    
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
     * @param keyLen
     *     Length of the key in bytes or zero
     * @param hashBitLen
     *     Number of MAC hash bits to compute
     * @return
     *     SKEIN_SUCESS of SKEIN_FAIL
     */
    int skeinMacInit(SkeinCtx_t* ctx, const uint8_t *key, size_t keyLen,
                     size_t hashBitLen);

    /**
     * Update Skein with the next part of the message.
     *
     * @param ctx
     *     Pointer to initialized Skein context
     * @param msg
     *     Pointer to the message.
     * @param msgByteCnt
     *     Length of the message in @b bytes
     * @return
     *     Success or error code.
     */
    int skeinUpdate(SkeinCtx_t *ctx, const uint8_t *msg,
                    size_t msgByteCnt);

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
     * @param msgBitCnt
     *     Length of the message in @b bits.
     */
    int skeinUpdateBits(SkeinCtx_t *ctx, const uint8_t *msg,
                        size_t msgBitCnt);

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
     *     enough to store @c hashBitLen bits.
     * @return
     *     Success or error code.
     * @see skeinReset
     */
    int skeinFinal(SkeinCtx_t* ctx, uint8_t* hash);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */
#endif
