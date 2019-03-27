/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "des_locl.h"

DES_LONG DES_cbc_cksum(const unsigned char *in, DES_cblock *output,
                       long length, DES_key_schedule *schedule,
                       const_DES_cblock *ivec)
{
    register DES_LONG tout0, tout1, tin0, tin1;
    register long l = length;
    DES_LONG tin[2];
    unsigned char *out = &(*output)[0];
    const unsigned char *iv = &(*ivec)[0];

    c2l(iv, tout0);
    c2l(iv, tout1);
    for (; l > 0; l -= 8) {
        if (l >= 8) {
            c2l(in, tin0);
            c2l(in, tin1);
        } else
            c2ln(in, tin0, tin1, l);

        tin0 ^= tout0;
        tin[0] = tin0;
        tin1 ^= tout1;
        tin[1] = tin1;
        DES_encrypt1((DES_LONG *)tin, schedule, DES_ENCRYPT);
        tout0 = tin[0];
        tout1 = tin[1];
    }
    if (out != NULL) {
        l2c(tout0, out);
        l2c(tout1, out);
    }
    tout0 = tin0 = tin1 = tin[0] = tin[1] = 0;
    /*
     * Transform the data in tout1 so that it will match the return value
     * that the MIT Kerberos mit_des_cbc_cksum API returns.
     */
    tout1 = ((tout1 >> 24L) & 0x000000FF)
        | ((tout1 >> 8L) & 0x0000FF00)
        | ((tout1 << 8L) & 0x00FF0000)
        | ((tout1 << 24L) & 0xFF000000);
    return tout1;
}
