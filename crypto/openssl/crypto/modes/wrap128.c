/*
 * Copyright 2013-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/**  Beware!
 *
 *  Following wrapping modes were designed for AES but this implementation
 *  allows you to use them for any 128 bit block cipher.
 */

#include "internal/cryptlib.h"
#include <openssl/modes.h>

/** RFC 3394 section 2.2.3.1 Default Initial Value */
static const unsigned char default_iv[] = {
    0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6,
};

/** RFC 5649 section 3 Alternative Initial Value 32-bit constant */
static const unsigned char default_aiv[] = {
    0xA6, 0x59, 0x59, 0xA6
};

/** Input size limit: lower than maximum of standards but far larger than
 *  anything that will be used in practice.
 */
#define CRYPTO128_WRAP_MAX (1UL << 31)

/** Wrapping according to RFC 3394 section 2.2.1.
 *
 *  @param[in]  key    Key value.
 *  @param[in]  iv     IV value. Length = 8 bytes. NULL = use default_iv.
 *  @param[in]  in     Plaintext as n 64-bit blocks, n >= 2.
 *  @param[in]  inlen  Length of in.
 *  @param[out] out    Ciphertext. Minimal buffer length = (inlen + 8) bytes.
 *                     Input and output buffers can overlap if block function
 *                     supports that.
 *  @param[in]  block  Block processing function.
 *  @return            0 if inlen does not consist of n 64-bit blocks, n >= 2.
 *                     or if inlen > CRYPTO128_WRAP_MAX.
 *                     Output length if wrapping succeeded.
 */
size_t CRYPTO_128_wrap(void *key, const unsigned char *iv,
                       unsigned char *out,
                       const unsigned char *in, size_t inlen,
                       block128_f block)
{
    unsigned char *A, B[16], *R;
    size_t i, j, t;
    if ((inlen & 0x7) || (inlen < 16) || (inlen > CRYPTO128_WRAP_MAX))
        return 0;
    A = B;
    t = 1;
    memmove(out + 8, in, inlen);
    if (!iv)
        iv = default_iv;

    memcpy(A, iv, 8);

    for (j = 0; j < 6; j++) {
        R = out + 8;
        for (i = 0; i < inlen; i += 8, t++, R += 8) {
            memcpy(B + 8, R, 8);
            block(B, B, key);
            A[7] ^= (unsigned char)(t & 0xff);
            if (t > 0xff) {
                A[6] ^= (unsigned char)((t >> 8) & 0xff);
                A[5] ^= (unsigned char)((t >> 16) & 0xff);
                A[4] ^= (unsigned char)((t >> 24) & 0xff);
            }
            memcpy(R, B + 8, 8);
        }
    }
    memcpy(out, A, 8);
    return inlen + 8;
}

/** Unwrapping according to RFC 3394 section 2.2.2 steps 1-2.
 *  The IV check (step 3) is responsibility of the caller.
 *
 *  @param[in]  key    Key value.
 *  @param[out] iv     Unchecked IV value. Minimal buffer length = 8 bytes.
 *  @param[out] out    Plaintext without IV.
 *                     Minimal buffer length = (inlen - 8) bytes.
 *                     Input and output buffers can overlap if block function
 *                     supports that.
 *  @param[in]  in     Ciphertext as n 64-bit blocks.
 *  @param[in]  inlen  Length of in.
 *  @param[in]  block  Block processing function.
 *  @return            0 if inlen is out of range [24, CRYPTO128_WRAP_MAX]
 *                     or if inlen is not a multiple of 8.
 *                     Output length otherwise.
 */
static size_t crypto_128_unwrap_raw(void *key, unsigned char *iv,
                                    unsigned char *out,
                                    const unsigned char *in, size_t inlen,
                                    block128_f block)
{
    unsigned char *A, B[16], *R;
    size_t i, j, t;
    inlen -= 8;
    if ((inlen & 0x7) || (inlen < 16) || (inlen > CRYPTO128_WRAP_MAX))
        return 0;
    A = B;
    t = 6 * (inlen >> 3);
    memcpy(A, in, 8);
    memmove(out, in + 8, inlen);
    for (j = 0; j < 6; j++) {
        R = out + inlen - 8;
        for (i = 0; i < inlen; i += 8, t--, R -= 8) {
            A[7] ^= (unsigned char)(t & 0xff);
            if (t > 0xff) {
                A[6] ^= (unsigned char)((t >> 8) & 0xff);
                A[5] ^= (unsigned char)((t >> 16) & 0xff);
                A[4] ^= (unsigned char)((t >> 24) & 0xff);
            }
            memcpy(B + 8, R, 8);
            block(B, B, key);
            memcpy(R, B + 8, 8);
        }
    }
    memcpy(iv, A, 8);
    return inlen;
}

/** Unwrapping according to RFC 3394 section 2.2.2, including the IV check.
 *  The first block of plaintext has to match the supplied IV, otherwise an
 *  error is returned.
 *
 *  @param[in]  key    Key value.
 *  @param[out] iv     IV value to match against. Length = 8 bytes.
 *                     NULL = use default_iv.
 *  @param[out] out    Plaintext without IV.
 *                     Minimal buffer length = (inlen - 8) bytes.
 *                     Input and output buffers can overlap if block function
 *                     supports that.
 *  @param[in]  in     Ciphertext as n 64-bit blocks.
 *  @param[in]  inlen  Length of in.
 *  @param[in]  block  Block processing function.
 *  @return            0 if inlen is out of range [24, CRYPTO128_WRAP_MAX]
 *                     or if inlen is not a multiple of 8
 *                     or if IV doesn't match expected value.
 *                     Output length otherwise.
 */
size_t CRYPTO_128_unwrap(void *key, const unsigned char *iv,
                         unsigned char *out, const unsigned char *in,
                         size_t inlen, block128_f block)
{
    size_t ret;
    unsigned char got_iv[8];

    ret = crypto_128_unwrap_raw(key, got_iv, out, in, inlen, block);
    if (ret == 0)
        return 0;

    if (!iv)
        iv = default_iv;
    if (CRYPTO_memcmp(got_iv, iv, 8)) {
        OPENSSL_cleanse(out, ret);
        return 0;
    }
    return ret;
}

/** Wrapping according to RFC 5649 section 4.1.
 *
 *  @param[in]  key    Key value.
 *  @param[in]  icv    (Non-standard) IV, 4 bytes. NULL = use default_aiv.
 *  @param[out] out    Ciphertext. Minimal buffer length = (inlen + 15) bytes.
 *                     Input and output buffers can overlap if block function
 *                     supports that.
 *  @param[in]  in     Plaintext as n 64-bit blocks, n >= 2.
 *  @param[in]  inlen  Length of in.
 *  @param[in]  block  Block processing function.
 *  @return            0 if inlen is out of range [1, CRYPTO128_WRAP_MAX].
 *                     Output length if wrapping succeeded.
 */
size_t CRYPTO_128_wrap_pad(void *key, const unsigned char *icv,
                           unsigned char *out,
                           const unsigned char *in, size_t inlen,
                           block128_f block)
{
    /* n: number of 64-bit blocks in the padded key data
     *
     * If length of plain text is not a multiple of 8, pad the plain text octet
     * string on the right with octets of zeros, where final length is the
     * smallest multiple of 8 that is greater than length of plain text.
     * If length of plain text is a multiple of 8, then there is no padding. */
    const size_t blocks_padded = (inlen + 7) / 8; /* CEILING(m/8) */
    const size_t padded_len = blocks_padded * 8;
    const size_t padding_len = padded_len - inlen;
    /* RFC 5649 section 3: Alternative Initial Value */
    unsigned char aiv[8];
    int ret;

    /* Section 1: use 32-bit fixed field for plaintext octet length */
    if (inlen == 0 || inlen >= CRYPTO128_WRAP_MAX)
        return 0;

    /* Section 3: Alternative Initial Value */
    if (!icv)
        memcpy(aiv, default_aiv, 4);
    else
        memcpy(aiv, icv, 4);    /* Standard doesn't mention this. */

    aiv[4] = (inlen >> 24) & 0xFF;
    aiv[5] = (inlen >> 16) & 0xFF;
    aiv[6] = (inlen >> 8) & 0xFF;
    aiv[7] = inlen & 0xFF;

    if (padded_len == 8) {
        /*
         * Section 4.1 - special case in step 2: If the padded plaintext
         * contains exactly eight octets, then prepend the AIV and encrypt
         * the resulting 128-bit block using AES in ECB mode.
         */
        memmove(out + 8, in, inlen);
        memcpy(out, aiv, 8);
        memset(out + 8 + inlen, 0, padding_len);
        block(out, out, key);
        ret = 16;               /* AIV + padded input */
    } else {
        memmove(out, in, inlen);
        memset(out + inlen, 0, padding_len); /* Section 4.1 step 1 */
        ret = CRYPTO_128_wrap(key, aiv, out, out, padded_len, block);
    }

    return ret;
}

/** Unwrapping according to RFC 5649 section 4.2.
 *
 *  @param[in]  key    Key value.
 *  @param[in]  icv    (Non-standard) IV, 4 bytes. NULL = use default_aiv.
 *  @param[out] out    Plaintext. Minimal buffer length = (inlen - 8) bytes.
 *                     Input and output buffers can overlap if block function
 *                     supports that.
 *  @param[in]  in     Ciphertext as n 64-bit blocks.
 *  @param[in]  inlen  Length of in.
 *  @param[in]  block  Block processing function.
 *  @return            0 if inlen is out of range [16, CRYPTO128_WRAP_MAX],
 *                     or if inlen is not a multiple of 8
 *                     or if IV and message length indicator doesn't match.
 *                     Output length if unwrapping succeeded and IV matches.
 */
size_t CRYPTO_128_unwrap_pad(void *key, const unsigned char *icv,
                             unsigned char *out,
                             const unsigned char *in, size_t inlen,
                             block128_f block)
{
    /* n: number of 64-bit blocks in the padded key data */
    size_t n = inlen / 8 - 1;
    size_t padded_len;
    size_t padding_len;
    size_t ptext_len;
    /* RFC 5649 section 3: Alternative Initial Value */
    unsigned char aiv[8];
    static unsigned char zeros[8] = { 0x0 };
    size_t ret;

    /* Section 4.2: Ciphertext length has to be (n+1) 64-bit blocks. */
    if ((inlen & 0x7) != 0 || inlen < 16 || inlen >= CRYPTO128_WRAP_MAX)
        return 0;

    if (inlen == 16) {
        /*
         * Section 4.2 - special case in step 1: When n=1, the ciphertext
         * contains exactly two 64-bit blocks and they are decrypted as a
         * single AES block using AES in ECB mode: AIV | P[1] = DEC(K, C[0] |
         * C[1])
         */
        unsigned char buff[16];

        block(in, buff, key);
        memcpy(aiv, buff, 8);
        /* Remove AIV */
        memcpy(out, buff + 8, 8);
        padded_len = 8;
        OPENSSL_cleanse(buff, inlen);
    } else {
        padded_len = inlen - 8;
        ret = crypto_128_unwrap_raw(key, aiv, out, in, inlen, block);
        if (padded_len != ret) {
            OPENSSL_cleanse(out, inlen);
            return 0;
        }
    }

    /*
     * Section 3: AIV checks: Check that MSB(32,A) = A65959A6. Optionally a
     * user-supplied value can be used (even if standard doesn't mention
     * this).
     */
    if ((!icv && CRYPTO_memcmp(aiv, default_aiv, 4))
        || (icv && CRYPTO_memcmp(aiv, icv, 4))) {
        OPENSSL_cleanse(out, inlen);
        return 0;
    }

    /*
     * Check that 8*(n-1) < LSB(32,AIV) <= 8*n. If so, let ptext_len =
     * LSB(32,AIV).
     */

    ptext_len =   ((unsigned int)aiv[4] << 24)
                | ((unsigned int)aiv[5] << 16)
                | ((unsigned int)aiv[6] <<  8)
                |  (unsigned int)aiv[7];
    if (8 * (n - 1) >= ptext_len || ptext_len > 8 * n) {
        OPENSSL_cleanse(out, inlen);
        return 0;
    }

    /*
     * Check that the rightmost padding_len octets of the output data are
     * zero.
     */
    padding_len = padded_len - ptext_len;
    if (CRYPTO_memcmp(out + ptext_len, zeros, padding_len) != 0) {
        OPENSSL_cleanse(out, inlen);
        return 0;
    }

    /* Section 4.2 step 3: Remove padding */
    return ptext_len;
}
