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
 * The input and output encrypted as though 64bit ofb mode is being used.
 * The extra state information to record how much of the 64bit block we have
 * used is contained in *num;
 */
void DES_ede3_ofb64_encrypt(register const unsigned char *in,
                            register unsigned char *out, long length,
                            DES_key_schedule *k1, DES_key_schedule *k2,
                            DES_key_schedule *k3, DES_cblock *ivec, int *num)
{
    register DES_LONG v0, v1;
    register int n = *num;
    register long l = length;
    DES_cblock d;
    register char *dp;
    DES_LONG ti[2];
    unsigned char *iv;
    int save = 0;

    iv = &(*ivec)[0];
    c2l(iv, v0);
    c2l(iv, v1);
    ti[0] = v0;
    ti[1] = v1;
    dp = (char *)d;
    l2c(v0, dp);
    l2c(v1, dp);
    while (l--) {
        if (n == 0) {
            /* ti[0]=v0; */
            /* ti[1]=v1; */
            DES_encrypt3(ti, k1, k2, k3);
            v0 = ti[0];
            v1 = ti[1];

            dp = (char *)d;
            l2c(v0, dp);
            l2c(v1, dp);
            save++;
        }
        *(out++) = *(in++) ^ d[n];
        n = (n + 1) & 0x07;
    }
    if (save) {
        iv = &(*ivec)[0];
        l2c(v0, iv);
        l2c(v1, iv);
    }
    v0 = v1 = ti[0] = ti[1] = 0;
    *num = n;
}
