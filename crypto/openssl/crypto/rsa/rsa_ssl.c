/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>
#include "internal/constant_time_locl.h"

int RSA_padding_add_SSLv23(unsigned char *to, int tlen,
                           const unsigned char *from, int flen)
{
    int i, j;
    unsigned char *p;

    if (flen > (tlen - 11)) {
        RSAerr(RSA_F_RSA_PADDING_ADD_SSLV23,
               RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
        return 0;
    }

    p = (unsigned char *)to;

    *(p++) = 0;
    *(p++) = 2;                 /* Public Key BT (Block Type) */

    /* pad out with non-zero random data */
    j = tlen - 3 - 8 - flen;

    if (RAND_bytes(p, j) <= 0)
        return 0;
    for (i = 0; i < j; i++) {
        if (*p == '\0')
            do {
                if (RAND_bytes(p, 1) <= 0)
                    return 0;
            } while (*p == '\0');
        p++;
    }

    memset(p, 3, 8);
    p += 8;
    *(p++) = '\0';

    memcpy(p, from, (unsigned int)flen);
    return 1;
}

/*
 * Copy of RSA_padding_check_PKCS1_type_2 with a twist that rejects padding
 * if nul delimiter is preceded by 8 consecutive 0x03 bytes. It also
 * preserves error code reporting for backward compatibility.
 */
int RSA_padding_check_SSLv23(unsigned char *to, int tlen,
                             const unsigned char *from, int flen, int num)
{
    int i;
    /* |em| is the encoded message, zero-padded to exactly |num| bytes */
    unsigned char *em = NULL;
    unsigned int good, found_zero_byte, mask, threes_in_row;
    int zero_index = 0, msg_index, mlen = -1, err;

    if (flen < 10) {
        RSAerr(RSA_F_RSA_PADDING_CHECK_SSLV23, RSA_R_DATA_TOO_SMALL);
        return -1;
    }

    em = OPENSSL_malloc(num);
    if (em == NULL) {
        RSAerr(RSA_F_RSA_PADDING_CHECK_SSLV23, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    /*
     * Caller is encouraged to pass zero-padded message created with
     * BN_bn2binpad. Trouble is that since we can't read out of |from|'s
     * bounds, it's impossible to have an invariant memory access pattern
     * in case |from| was not zero-padded in advance.
     */
    for (from += flen, em += num, i = 0; i < num; i++) {
        mask = ~constant_time_is_zero(flen);
        flen -= 1 & mask;
        from -= 1 & mask;
        *--em = *from & mask;
    }
    from = em;

    good = constant_time_is_zero(from[0]);
    good &= constant_time_eq(from[1], 2);
    err = constant_time_select_int(good, 0, RSA_R_BLOCK_TYPE_IS_NOT_02);
    mask = ~good;

    /* scan over padding data */
    found_zero_byte = 0;
    threes_in_row = 0;
    for (i = 2; i < num; i++) {
        unsigned int equals0 = constant_time_is_zero(from[i]);

        zero_index = constant_time_select_int(~found_zero_byte & equals0,
                                              i, zero_index);
        found_zero_byte |= equals0;

        threes_in_row += 1 & ~found_zero_byte;
        threes_in_row &= found_zero_byte | constant_time_eq(from[i], 3);
    }

    /*
     * PS must be at least 8 bytes long, and it starts two bytes into |from|.
     * If we never found a 0-byte, then |zero_index| is 0 and the check
     * also fails.
     */
    good &= constant_time_ge(zero_index, 2 + 8);
    err = constant_time_select_int(mask | good, err,
                                   RSA_R_NULL_BEFORE_BLOCK_MISSING);
    mask = ~good;

    good &= constant_time_lt(threes_in_row, 8);
    err = constant_time_select_int(mask | good, err,
                                   RSA_R_SSLV3_ROLLBACK_ATTACK);
    mask = ~good;

    /*
     * Skip the zero byte. This is incorrect if we never found a zero-byte
     * but in this case we also do not copy the message out.
     */
    msg_index = zero_index + 1;
    mlen = num - msg_index;

    /*
     * For good measure, do this check in constant time as well.
     */
    good &= constant_time_ge(tlen, mlen);
    err = constant_time_select_int(mask | good, err, RSA_R_DATA_TOO_LARGE);

    /*
     * Even though we can't fake result's length, we can pretend copying
     * |tlen| bytes where |mlen| bytes would be real. Last |tlen| of |num|
     * bytes are viewed as circular buffer with start at |tlen|-|mlen'|,
     * where |mlen'| is "saturated" |mlen| value. Deducing information
     * about failure or |mlen| would take attacker's ability to observe
     * memory access pattern with byte granularity *as it occurs*. It
     * should be noted that failure is indistinguishable from normal
     * operation if |tlen| is fixed by protocol.
     */
    tlen = constant_time_select_int(constant_time_lt(num, tlen), num, tlen);
    msg_index = constant_time_select_int(good, msg_index, num - tlen);
    mlen = num - msg_index;
    for (from += msg_index, mask = good, i = 0; i < tlen; i++) {
        unsigned int equals = constant_time_eq(i, mlen);

        from -= tlen & equals;  /* if (i == mlen) rewind   */
        mask &= mask ^ equals;  /* if (i == mlen) mask = 0 */
        to[i] = constant_time_select_8(mask, from[i], to[i]);
    }

    OPENSSL_clear_free(em, num);
    RSAerr(RSA_F_RSA_PADDING_CHECK_SSLV23, err);
    err_clear_last_constant_time(1 & good);

    return constant_time_select_int(good, mlen, -1);
}
