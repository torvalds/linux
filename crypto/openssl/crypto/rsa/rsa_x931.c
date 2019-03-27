/*
 * Copyright 2005-2017 The OpenSSL Project Authors. All Rights Reserved.
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
#include <openssl/objects.h>

int RSA_padding_add_X931(unsigned char *to, int tlen,
                         const unsigned char *from, int flen)
{
    int j;
    unsigned char *p;

    /*
     * Absolute minimum amount of padding is 1 header nibble, 1 padding
     * nibble and 2 trailer bytes: but 1 hash if is already in 'from'.
     */

    j = tlen - flen - 2;

    if (j < 0) {
        RSAerr(RSA_F_RSA_PADDING_ADD_X931, RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
        return -1;
    }

    p = (unsigned char *)to;

    /* If no padding start and end nibbles are in one byte */
    if (j == 0) {
        *p++ = 0x6A;
    } else {
        *p++ = 0x6B;
        if (j > 1) {
            memset(p, 0xBB, j - 1);
            p += j - 1;
        }
        *p++ = 0xBA;
    }
    memcpy(p, from, (unsigned int)flen);
    p += flen;
    *p = 0xCC;
    return 1;
}

int RSA_padding_check_X931(unsigned char *to, int tlen,
                           const unsigned char *from, int flen, int num)
{
    int i = 0, j;
    const unsigned char *p;

    p = from;
    if ((num != flen) || ((*p != 0x6A) && (*p != 0x6B))) {
        RSAerr(RSA_F_RSA_PADDING_CHECK_X931, RSA_R_INVALID_HEADER);
        return -1;
    }

    if (*p++ == 0x6B) {
        j = flen - 3;
        for (i = 0; i < j; i++) {
            unsigned char c = *p++;
            if (c == 0xBA)
                break;
            if (c != 0xBB) {
                RSAerr(RSA_F_RSA_PADDING_CHECK_X931, RSA_R_INVALID_PADDING);
                return -1;
            }
        }

        j -= i;

        if (i == 0) {
            RSAerr(RSA_F_RSA_PADDING_CHECK_X931, RSA_R_INVALID_PADDING);
            return -1;
        }

    } else {
        j = flen - 2;
    }

    if (p[j] != 0xCC) {
        RSAerr(RSA_F_RSA_PADDING_CHECK_X931, RSA_R_INVALID_TRAILER);
        return -1;
    }

    memcpy(to, p, (unsigned int)j);

    return j;
}

/* Translate between X931 hash ids and NIDs */

int RSA_X931_hash_id(int nid)
{
    switch (nid) {
    case NID_sha1:
        return 0x33;

    case NID_sha256:
        return 0x34;

    case NID_sha384:
        return 0x36;

    case NID_sha512:
        return 0x35;

    }
    return -1;
}
