
#ifndef THREEFISHAPI_H
#define THREEFISHAPI_H

/**
 * @file threefishApi.h
 * @brief A Threefish cipher API and its functions.
 * @{
 *
 * This API and the functions that implement this API simplify the usage
 * of the Threefish cipher. The design and the way to use the functions 
 * follow the openSSL design but at the same time take care of some Threefish
 * specific behaviour and possibilities.
 *
 * These are the low level functions that deal with Threefisch blocks only.
 * Implementations for cipher modes such as ECB, CFB, or CBC may use these 
 * functions.
 * 
@code
    // Threefish cipher context data
    ThreefishKey_t keyCtx;

    // Initialize the context
    threefishSetKey(&keyCtx, Threefish512, key, tweak);

    // Encrypt
    threefishEncryptBlockBytes(&keyCtx, input, cipher);
@endcode
 */

#include <skein.h>
#include <stdint.h>

#define KeyScheduleConst 0x1BD11BDAA9FC1A22L

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Which Threefish size to use
     */
    typedef enum ThreefishSize {
        Threefish256 = 256,     /*!< Skein with 256 bit state */
        Threefish512 = 512,     /*!< Skein with 512 bit state */
        Threefish1024 = 1024    /*!< Skein with 1024 bit state */
    } ThreefishSize_t;
    
    /**
     * Context for Threefish key and tweak words.
     * 
     * This structure was setup with some know-how of the internal
     * Skein structures, in particular ordering of header and size dependent
     * variables. If Skein implementation changes this, the adapt these
     * structures as well.
     */
    typedef struct ThreefishKey {
        u64b_t stateSize;
        u64b_t key[SKEIN_MAX_STATE_WORDS+1];   /* max number of key words*/
        u64b_t tweak[3];
    } ThreefishKey_t;

    /**
     * Set Threefish key and tweak data.
     * 
     * This function sets the key and tweak data for the Threefish cipher of
     * the given size. The key data must have the same length (number of bits)
     * as the state size 
     *
     * @param keyCtx
     *     Pointer to a Threefish key structure.
     * @param size
     *     Which Skein size to use.
     * @param keyData
     *     Pointer to the key words (word has 64 bits).
     * @param tweak
     *     Pointer to the two tweak words (word has 64 bits).
     */
    void threefishSetKey(ThreefishKey_t* keyCtx, ThreefishSize_t stateSize, uint64_t* keyData, uint64_t* tweak);
    
    /**
     * Encrypt Threefisch block (bytes).
     * 
     * The buffer must have at least the same length (number of bits) aas the 
     * state size for this key. The function uses the first @c stateSize bits
     * of the input buffer, encrypts them and stores the result in the output
     * buffer.
     * 
     * @param keyCtx
     *     Pointer to a Threefish key structure.
     * @param in
     *     Poionter to plaintext data buffer.
     * @param out
     *     Pointer to cipher buffer.
     */
    void threefishEncryptBlockBytes(ThreefishKey_t* keyCtx, uint8_t* in, uint8_t* out);
    
    /**
     * Encrypt Threefisch block (words).
     * 
     * The buffer must have at least the same length (number of bits) aas the 
     * state size for this key. The function uses the first @c stateSize bits
     * of the input buffer, encrypts them and stores the result in the output
     * buffer.
     * 
     * The wordsize ist set to 64 bits.
     * 
     * @param keyCtx
     *     Pointer to a Threefish key structure.
     * @param in
     *     Poionter to plaintext data buffer.
     * @param out
     *     Pointer to cipher buffer.
     */
    void threefishEncryptBlockWords(ThreefishKey_t* keyCtx, uint64_t* in, uint64_t* out);

    /**
     * Decrypt Threefisch block (bytes).
     * 
     * The buffer must have at least the same length (number of bits) aas the 
     * state size for this key. The function uses the first @c stateSize bits
     * of the input buffer, decrypts them and stores the result in the output
     * buffer
     * 
     * @param keyCtx
     *     Pointer to a Threefish key structure.
     * @param in
     *     Poionter to cipher data buffer.
     * @param out
     *     Pointer to plaintext buffer.
     */
    void threefishDecryptBlockBytes(ThreefishKey_t* keyCtx, uint8_t* in, uint8_t* out);

    /**
     * Decrypt Threefisch block (words).
     * 
     * The buffer must have at least the same length (number of bits) aas the 
     * state size for this key. The function uses the first @c stateSize bits
     * of the input buffer, encrypts them and stores the result in the output
     * buffer.
     * 
     * The wordsize ist set to 64 bits.
     * 
     * @param keyCtx
     *     Pointer to a Threefish key structure.
     * @param in
     *     Poionter to cipher data buffer.
     * @param out
     *     Pointer to plaintext buffer.
     */
    void threefishDecryptBlockWords(ThreefishKey_t* keyCtx, uint64_t* in, uint64_t* out);

    void threefishEncrypt256(ThreefishKey_t* keyCtx, uint64_t* input, uint64_t* output);
    void threefishEncrypt512(ThreefishKey_t* keyCtx, uint64_t* input, uint64_t* output);
    void threefishEncrypt1024(ThreefishKey_t* keyCtx, uint64_t* input, uint64_t* output);
    void threefishDecrypt256(ThreefishKey_t* keyCtx, uint64_t* input, uint64_t* output);
    void threefishDecrypt512(ThreefishKey_t* keyCtx, uint64_t* input, uint64_t* output);
    void threefishDecrypt1024(ThreefishKey_t* keyCtx, uint64_t* input, uint64_t* output);
#ifdef __cplusplus
}
#endif

/**
 * @}
 */
#endif
