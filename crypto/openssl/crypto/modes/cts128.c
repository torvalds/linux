/*
 * Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>
#include "modes_lcl.h"
#include <string.h>

/*
 * Trouble with Ciphertext Stealing, CTS, mode is that there is no
 * common official specification, but couple of cipher/application
 * specific ones: RFC2040 and RFC3962. Then there is 'Proposal to
 * Extend CBC Mode By "Ciphertext Stealing"' at NIST site, which
 * deviates from mentioned RFCs. Most notably it allows input to be
 * of block length and it doesn't flip the order of the last two
 * blocks. CTS is being discussed even in ECB context, but it's not
 * adopted for any known application. This implementation provides
 * two interfaces: one compliant with above mentioned RFCs and one
 * compliant with the NIST proposal, both extending CBC mode.
 */

size_t CRYPTO_cts128_encrypt_block(const unsigned char *in,
                                   unsigned char *out, size_t len,
                                   const void *key, unsigned char ivec[16],
                                   block128_f block)
{
    size_t residue, n;

    if (len <= 16)
        return 0;

    if ((residue = len % 16) == 0)
        residue = 16;

    len -= residue;

    CRYPTO_cbc128_encrypt(in, out, len, key, ivec, block);

    in += len;
    out += len;

    for (n = 0; n < residue; ++n)
        ivec[n] ^= in[n];
    (*block) (ivec, ivec, key);
    memcpy(out, out - 16, residue);
    memcpy(out - 16, ivec, 16);

    return len + residue;
}

size_t CRYPTO_nistcts128_encrypt_block(const unsigned char *in,
                                       unsigned char *out, size_t len,
                                       const void *key,
                                       unsigned char ivec[16],
                                       block128_f block)
{
    size_t residue, n;

    if (len < 16)
        return 0;

    residue = len % 16;

    len -= residue;

    CRYPTO_cbc128_encrypt(in, out, len, key, ivec, block);

    if (residue == 0)
        return len;

    in += len;
    out += len;

    for (n = 0; n < residue; ++n)
        ivec[n] ^= in[n];
    (*block) (ivec, ivec, key);
    memcpy(out - 16 + residue, ivec, 16);

    return len + residue;
}

size_t CRYPTO_cts128_encrypt(const unsigned char *in, unsigned char *out,
                             size_t len, const void *key,
                             unsigned char ivec[16], cbc128_f cbc)
{
    size_t residue;
    union {
        size_t align;
        unsigned char c[16];
    } tmp;

    if (len <= 16)
        return 0;

    if ((residue = len % 16) == 0)
        residue = 16;

    len -= residue;

    (*cbc) (in, out, len, key, ivec, 1);

    in += len;
    out += len;

#if defined(CBC_HANDLES_TRUNCATED_IO)
    memcpy(tmp.c, out - 16, 16);
    (*cbc) (in, out - 16, residue, key, ivec, 1);
    memcpy(out, tmp.c, residue);
#else
    memset(tmp.c, 0, sizeof(tmp));
    memcpy(tmp.c, in, residue);
    memcpy(out, out - 16, residue);
    (*cbc) (tmp.c, out - 16, 16, key, ivec, 1);
#endif
    return len + residue;
}

size_t CRYPTO_nistcts128_encrypt(const unsigned char *in, unsigned char *out,
                                 size_t len, const void *key,
                                 unsigned char ivec[16], cbc128_f cbc)
{
    size_t residue;
    union {
        size_t align;
        unsigned char c[16];
    } tmp;

    if (len < 16)
        return 0;

    residue = len % 16;

    len -= residue;

    (*cbc) (in, out, len, key, ivec, 1);

    if (residue == 0)
        return len;

    in += len;
    out += len;

#if defined(CBC_HANDLES_TRUNCATED_IO)
    (*cbc) (in, out - 16 + residue, residue, key, ivec, 1);
#else
    memset(tmp.c, 0, sizeof(tmp));
    memcpy(tmp.c, in, residue);
    (*cbc) (tmp.c, out - 16 + residue, 16, key, ivec, 1);
#endif
    return len + residue;
}

size_t CRYPTO_cts128_decrypt_block(const unsigned char *in,
                                   unsigned char *out, size_t len,
                                   const void *key, unsigned char ivec[16],
                                   block128_f block)
{
    size_t residue, n;
    union {
        size_t align;
        unsigned char c[32];
    } tmp;

    if (len <= 16)
        return 0;

    if ((residue = len % 16) == 0)
        residue = 16;

    len -= 16 + residue;

    if (len) {
        CRYPTO_cbc128_decrypt(in, out, len, key, ivec, block);
        in += len;
        out += len;
    }

    (*block) (in, tmp.c + 16, key);

    memcpy(tmp.c, tmp.c + 16, 16);
    memcpy(tmp.c, in + 16, residue);
    (*block) (tmp.c, tmp.c, key);

    for (n = 0; n < 16; ++n) {
        unsigned char c = in[n];
        out[n] = tmp.c[n] ^ ivec[n];
        ivec[n] = c;
    }
    for (residue += 16; n < residue; ++n)
        out[n] = tmp.c[n] ^ in[n];

    return 16 + len + residue;
}

size_t CRYPTO_nistcts128_decrypt_block(const unsigned char *in,
                                       unsigned char *out, size_t len,
                                       const void *key,
                                       unsigned char ivec[16],
                                       block128_f block)
{
    size_t residue, n;
    union {
        size_t align;
        unsigned char c[32];
    } tmp;

    if (len < 16)
        return 0;

    residue = len % 16;

    if (residue == 0) {
        CRYPTO_cbc128_decrypt(in, out, len, key, ivec, block);
        return len;
    }

    len -= 16 + residue;

    if (len) {
        CRYPTO_cbc128_decrypt(in, out, len, key, ivec, block);
        in += len;
        out += len;
    }

    (*block) (in + residue, tmp.c + 16, key);

    memcpy(tmp.c, tmp.c + 16, 16);
    memcpy(tmp.c, in, residue);
    (*block) (tmp.c, tmp.c, key);

    for (n = 0; n < 16; ++n) {
        unsigned char c = in[n];
        out[n] = tmp.c[n] ^ ivec[n];
        ivec[n] = in[n + residue];
        tmp.c[n] = c;
    }
    for (residue += 16; n < residue; ++n)
        out[n] = tmp.c[n] ^ tmp.c[n - 16];

    return 16 + len + residue;
}

size_t CRYPTO_cts128_decrypt(const unsigned char *in, unsigned char *out,
                             size_t len, const void *key,
                             unsigned char ivec[16], cbc128_f cbc)
{
    size_t residue;
    union {
        size_t align;
        unsigned char c[32];
    } tmp;

    if (len <= 16)
        return 0;

    if ((residue = len % 16) == 0)
        residue = 16;

    len -= 16 + residue;

    if (len) {
        (*cbc) (in, out, len, key, ivec, 0);
        in += len;
        out += len;
    }

    memset(tmp.c, 0, sizeof(tmp));
    /*
     * this places in[16] at &tmp.c[16] and decrypted block at &tmp.c[0]
     */
    (*cbc) (in, tmp.c, 16, key, tmp.c + 16, 0);

    memcpy(tmp.c, in + 16, residue);
#if defined(CBC_HANDLES_TRUNCATED_IO)
    (*cbc) (tmp.c, out, 16 + residue, key, ivec, 0);
#else
    (*cbc) (tmp.c, tmp.c, 32, key, ivec, 0);
    memcpy(out, tmp.c, 16 + residue);
#endif
    return 16 + len + residue;
}

size_t CRYPTO_nistcts128_decrypt(const unsigned char *in, unsigned char *out,
                                 size_t len, const void *key,
                                 unsigned char ivec[16], cbc128_f cbc)
{
    size_t residue;
    union {
        size_t align;
        unsigned char c[32];
    } tmp;

    if (len < 16)
        return 0;

    residue = len % 16;

    if (residue == 0) {
        (*cbc) (in, out, len, key, ivec, 0);
        return len;
    }

    len -= 16 + residue;

    if (len) {
        (*cbc) (in, out, len, key, ivec, 0);
        in += len;
        out += len;
    }

    memset(tmp.c, 0, sizeof(tmp));
    /*
     * this places in[16] at &tmp.c[16] and decrypted block at &tmp.c[0]
     */
    (*cbc) (in + residue, tmp.c, 16, key, tmp.c + 16, 0);

    memcpy(tmp.c, in, residue);
#if defined(CBC_HANDLES_TRUNCATED_IO)
    (*cbc) (tmp.c, out, 16 + residue, key, ivec, 0);
#else
    (*cbc) (tmp.c, tmp.c, 32, key, ivec, 0);
    memcpy(out, tmp.c, 16 + residue);
#endif
    return 16 + len + residue;
}
