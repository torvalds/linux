/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "des_locl.h"

/*
 * The input and output are loaded in multiples of 8 bits. What this means is
 * that if you have numbits=12 and length=2 the first 12 bits will be
 * retrieved from the first byte and half the second.  The second 12 bits
 * will come from the 3rd and half the 4th byte.
 */
void DES_ofb_encrypt(const unsigned char *in, unsigned char *out, int numbits,
                     long length, DES_key_schedule *schedule,
                     DES_cblock *ivec)
{
    register DES_LONG d0, d1, vv0, vv1, v0, v1, n = (numbits + 7) / 8;
    register DES_LONG mask0, mask1;
    register long l = length;
    register int num = numbits;
    DES_LONG ti[2];
    unsigned char *iv;

    if (num > 64)
        return;
    if (num > 32) {
        mask0 = 0xffffffffL;
        if (num >= 64)
            mask1 = mask0;
        else
            mask1 = (1L << (num - 32)) - 1;
    } else {
        if (num == 32)
            mask0 = 0xffffffffL;
        else
            mask0 = (1L << num) - 1;
        mask1 = 0x00000000L;
    }

    iv = &(*ivec)[0];
    c2l(iv, v0);
    c2l(iv, v1);
    ti[0] = v0;
    ti[1] = v1;
    while (l-- > 0) {
        ti[0] = v0;
        ti[1] = v1;
        DES_encrypt1((DES_LONG *)ti, schedule, DES_ENCRYPT);
        vv0 = ti[0];
        vv1 = ti[1];
        c2ln(in, d0, d1, n);
        in += n;
        d0 = (d0 ^ vv0) & mask0;
        d1 = (d1 ^ vv1) & mask1;
        l2cn(d0, d1, out, n);
        out += n;

        if (num == 32) {
            v0 = v1;
            v1 = vv0;
        } else if (num == 64) {
            v0 = vv0;
            v1 = vv1;
        } else if (num > 32) {  /* && num != 64 */
            v0 = ((v1 >> (num - 32)) | (vv0 << (64 - num))) & 0xffffffffL;
            v1 = ((vv0 >> (num - 32)) | (vv1 << (64 - num))) & 0xffffffffL;
        } else {                /* num < 32 */

            v0 = ((v0 >> num) | (v1 << (32 - num))) & 0xffffffffL;
            v1 = ((v1 >> num) | (vv0 << (32 - num))) & 0xffffffffL;
        }
    }
    iv = &(*ivec)[0];
    l2c(v0, iv);
    l2c(v1, iv);
    v0 = v1 = d0 = d1 = ti[0] = ti[1] = vv0 = vv1 = 0;
}
