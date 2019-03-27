/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * From "Message Authentication" R.R. Jueneman, S.M. Matyas, C.H. Meyer IEEE
 * Communications Magazine Sept 1985 Vol. 23 No. 9 p 29-40 This module in
 * only based on the code in this paper and is almost definitely not the same
 * as the MIT implementation.
 */
#include "des_locl.h"

#define Q_B0(a) (((DES_LONG)(a)))
#define Q_B1(a) (((DES_LONG)(a))<<8)
#define Q_B2(a) (((DES_LONG)(a))<<16)
#define Q_B3(a) (((DES_LONG)(a))<<24)

/* used to scramble things a bit */
/* Got the value MIT uses via brute force :-) 2/10/90 eay */
#define NOISE   ((DES_LONG)83653421L)

DES_LONG DES_quad_cksum(const unsigned char *input, DES_cblock output[],
                        long length, int out_count, DES_cblock *seed)
{
    DES_LONG z0, z1, t0, t1;
    int i;
    long l;
    const unsigned char *cp;
    DES_LONG *lp;

    if (out_count < 1)
        out_count = 1;
    lp = (DES_LONG *)&(output[0])[0];

    z0 = Q_B0((*seed)[0]) | Q_B1((*seed)[1]) | Q_B2((*seed)[2]) |
        Q_B3((*seed)[3]);
    z1 = Q_B0((*seed)[4]) | Q_B1((*seed)[5]) | Q_B2((*seed)[6]) |
        Q_B3((*seed)[7]);

    for (i = 0; ((i < 4) && (i < out_count)); i++) {
        cp = input;
        l = length;
        while (l > 0) {
            if (l > 1) {
                t0 = (DES_LONG)(*(cp++));
                t0 |= (DES_LONG)Q_B1(*(cp++));
                l--;
            } else
                t0 = (DES_LONG)(*(cp++));
            l--;
            /* add */
            t0 += z0;
            t0 &= 0xffffffffL;
            t1 = z1;
            /* square, well sort of square */
            z0 = ((((t0 * t0) & 0xffffffffL) + ((t1 * t1) & 0xffffffffL))
                  & 0xffffffffL) % 0x7fffffffL;
            z1 = ((t0 * ((t1 + NOISE) & 0xffffffffL)) & 0xffffffffL) %
                0x7fffffffL;
        }
        if (lp != NULL) {
            /*
             * The MIT library assumes that the checksum is composed of
             * 2*out_count 32 bit ints
             */
            *lp++ = z0;
            *lp++ = z1;
        }
    }
    return z0;
}
