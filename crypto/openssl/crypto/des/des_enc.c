/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>
#include "des_locl.h"
#include "spr.h"

void DES_encrypt1(DES_LONG *data, DES_key_schedule *ks, int enc)
{
    register DES_LONG l, r, t, u;
    register DES_LONG *s;

    r = data[0];
    l = data[1];

    IP(r, l);
    /*
     * Things have been modified so that the initial rotate is done outside
     * the loop.  This required the DES_SPtrans values in sp.h to be rotated
     * 1 bit to the right. One perl script later and things have a 5% speed
     * up on a sparc2. Thanks to Richard Outerbridge for pointing this out.
     */
    /* clear the top bits on machines with 8byte longs */
    /* shift left by 2 */
    r = ROTATE(r, 29) & 0xffffffffL;
    l = ROTATE(l, 29) & 0xffffffffL;

    s = ks->ks->deslong;
    /*
     * I don't know if it is worth the effort of loop unrolling the inner
     * loop
     */
    if (enc) {
        D_ENCRYPT(l, r, 0);     /* 1 */
        D_ENCRYPT(r, l, 2);     /* 2 */
        D_ENCRYPT(l, r, 4);     /* 3 */
        D_ENCRYPT(r, l, 6);     /* 4 */
        D_ENCRYPT(l, r, 8);     /* 5 */
        D_ENCRYPT(r, l, 10);    /* 6 */
        D_ENCRYPT(l, r, 12);    /* 7 */
        D_ENCRYPT(r, l, 14);    /* 8 */
        D_ENCRYPT(l, r, 16);    /* 9 */
        D_ENCRYPT(r, l, 18);    /* 10 */
        D_ENCRYPT(l, r, 20);    /* 11 */
        D_ENCRYPT(r, l, 22);    /* 12 */
        D_ENCRYPT(l, r, 24);    /* 13 */
        D_ENCRYPT(r, l, 26);    /* 14 */
        D_ENCRYPT(l, r, 28);    /* 15 */
        D_ENCRYPT(r, l, 30);    /* 16 */
    } else {
        D_ENCRYPT(l, r, 30);    /* 16 */
        D_ENCRYPT(r, l, 28);    /* 15 */
        D_ENCRYPT(l, r, 26);    /* 14 */
        D_ENCRYPT(r, l, 24);    /* 13 */
        D_ENCRYPT(l, r, 22);    /* 12 */
        D_ENCRYPT(r, l, 20);    /* 11 */
        D_ENCRYPT(l, r, 18);    /* 10 */
        D_ENCRYPT(r, l, 16);    /* 9 */
        D_ENCRYPT(l, r, 14);    /* 8 */
        D_ENCRYPT(r, l, 12);    /* 7 */
        D_ENCRYPT(l, r, 10);    /* 6 */
        D_ENCRYPT(r, l, 8);     /* 5 */
        D_ENCRYPT(l, r, 6);     /* 4 */
        D_ENCRYPT(r, l, 4);     /* 3 */
        D_ENCRYPT(l, r, 2);     /* 2 */
        D_ENCRYPT(r, l, 0);     /* 1 */
    }

    /* rotate and clear the top bits on machines with 8byte longs */
    l = ROTATE(l, 3) & 0xffffffffL;
    r = ROTATE(r, 3) & 0xffffffffL;

    FP(r, l);
    data[0] = l;
    data[1] = r;
    l = r = t = u = 0;
}

void DES_encrypt2(DES_LONG *data, DES_key_schedule *ks, int enc)
{
    register DES_LONG l, r, t, u;
    register DES_LONG *s;

    r = data[0];
    l = data[1];

    /*
     * Things have been modified so that the initial rotate is done outside
     * the loop.  This required the DES_SPtrans values in sp.h to be rotated
     * 1 bit to the right. One perl script later and things have a 5% speed
     * up on a sparc2. Thanks to Richard Outerbridge for pointing this out.
     */
    /* clear the top bits on machines with 8byte longs */
    r = ROTATE(r, 29) & 0xffffffffL;
    l = ROTATE(l, 29) & 0xffffffffL;

    s = ks->ks->deslong;
    /*
     * I don't know if it is worth the effort of loop unrolling the inner
     * loop
     */
    if (enc) {
        D_ENCRYPT(l, r, 0);     /* 1 */
        D_ENCRYPT(r, l, 2);     /* 2 */
        D_ENCRYPT(l, r, 4);     /* 3 */
        D_ENCRYPT(r, l, 6);     /* 4 */
        D_ENCRYPT(l, r, 8);     /* 5 */
        D_ENCRYPT(r, l, 10);    /* 6 */
        D_ENCRYPT(l, r, 12);    /* 7 */
        D_ENCRYPT(r, l, 14);    /* 8 */
        D_ENCRYPT(l, r, 16);    /* 9 */
        D_ENCRYPT(r, l, 18);    /* 10 */
        D_ENCRYPT(l, r, 20);    /* 11 */
        D_ENCRYPT(r, l, 22);    /* 12 */
        D_ENCRYPT(l, r, 24);    /* 13 */
        D_ENCRYPT(r, l, 26);    /* 14 */
        D_ENCRYPT(l, r, 28);    /* 15 */
        D_ENCRYPT(r, l, 30);    /* 16 */
    } else {
        D_ENCRYPT(l, r, 30);    /* 16 */
        D_ENCRYPT(r, l, 28);    /* 15 */
        D_ENCRYPT(l, r, 26);    /* 14 */
        D_ENCRYPT(r, l, 24);    /* 13 */
        D_ENCRYPT(l, r, 22);    /* 12 */
        D_ENCRYPT(r, l, 20);    /* 11 */
        D_ENCRYPT(l, r, 18);    /* 10 */
        D_ENCRYPT(r, l, 16);    /* 9 */
        D_ENCRYPT(l, r, 14);    /* 8 */
        D_ENCRYPT(r, l, 12);    /* 7 */
        D_ENCRYPT(l, r, 10);    /* 6 */
        D_ENCRYPT(r, l, 8);     /* 5 */
        D_ENCRYPT(l, r, 6);     /* 4 */
        D_ENCRYPT(r, l, 4);     /* 3 */
        D_ENCRYPT(l, r, 2);     /* 2 */
        D_ENCRYPT(r, l, 0);     /* 1 */
    }
    /* rotate and clear the top bits on machines with 8byte longs */
    data[0] = ROTATE(l, 3) & 0xffffffffL;
    data[1] = ROTATE(r, 3) & 0xffffffffL;
    l = r = t = u = 0;
}

void DES_encrypt3(DES_LONG *data, DES_key_schedule *ks1,
                  DES_key_schedule *ks2, DES_key_schedule *ks3)
{
    register DES_LONG l, r;

    l = data[0];
    r = data[1];
    IP(l, r);
    data[0] = l;
    data[1] = r;
    DES_encrypt2((DES_LONG *)data, ks1, DES_ENCRYPT);
    DES_encrypt2((DES_LONG *)data, ks2, DES_DECRYPT);
    DES_encrypt2((DES_LONG *)data, ks3, DES_ENCRYPT);
    l = data[0];
    r = data[1];
    FP(r, l);
    data[0] = l;
    data[1] = r;
}

void DES_decrypt3(DES_LONG *data, DES_key_schedule *ks1,
                  DES_key_schedule *ks2, DES_key_schedule *ks3)
{
    register DES_LONG l, r;

    l = data[0];
    r = data[1];
    IP(l, r);
    data[0] = l;
    data[1] = r;
    DES_encrypt2((DES_LONG *)data, ks3, DES_DECRYPT);
    DES_encrypt2((DES_LONG *)data, ks2, DES_ENCRYPT);
    DES_encrypt2((DES_LONG *)data, ks1, DES_DECRYPT);
    l = data[0];
    r = data[1];
    FP(r, l);
    data[0] = l;
    data[1] = r;
}

#ifndef DES_DEFAULT_OPTIONS

# undef CBC_ENC_C__DONT_UPDATE_IV
# include "ncbc_enc.c"          /* DES_ncbc_encrypt */

void DES_ede3_cbc_encrypt(const unsigned char *input, unsigned char *output,
                          long length, DES_key_schedule *ks1,
                          DES_key_schedule *ks2, DES_key_schedule *ks3,
                          DES_cblock *ivec, int enc)
{
    register DES_LONG tin0, tin1;
    register DES_LONG tout0, tout1, xor0, xor1;
    register const unsigned char *in;
    unsigned char *out;
    register long l = length;
    DES_LONG tin[2];
    unsigned char *iv;

    in = input;
    out = output;
    iv = &(*ivec)[0];

    if (enc) {
        c2l(iv, tout0);
        c2l(iv, tout1);
        for (l -= 8; l >= 0; l -= 8) {
            c2l(in, tin0);
            c2l(in, tin1);
            tin0 ^= tout0;
            tin1 ^= tout1;

            tin[0] = tin0;
            tin[1] = tin1;
            DES_encrypt3((DES_LONG *)tin, ks1, ks2, ks3);
            tout0 = tin[0];
            tout1 = tin[1];

            l2c(tout0, out);
            l2c(tout1, out);
        }
        if (l != -8) {
            c2ln(in, tin0, tin1, l + 8);
            tin0 ^= tout0;
            tin1 ^= tout1;

            tin[0] = tin0;
            tin[1] = tin1;
            DES_encrypt3((DES_LONG *)tin, ks1, ks2, ks3);
            tout0 = tin[0];
            tout1 = tin[1];

            l2c(tout0, out);
            l2c(tout1, out);
        }
        iv = &(*ivec)[0];
        l2c(tout0, iv);
        l2c(tout1, iv);
    } else {
        register DES_LONG t0, t1;

        c2l(iv, xor0);
        c2l(iv, xor1);
        for (l -= 8; l >= 0; l -= 8) {
            c2l(in, tin0);
            c2l(in, tin1);

            t0 = tin0;
            t1 = tin1;

            tin[0] = tin0;
            tin[1] = tin1;
            DES_decrypt3((DES_LONG *)tin, ks1, ks2, ks3);
            tout0 = tin[0];
            tout1 = tin[1];

            tout0 ^= xor0;
            tout1 ^= xor1;
            l2c(tout0, out);
            l2c(tout1, out);
            xor0 = t0;
            xor1 = t1;
        }
        if (l != -8) {
            c2l(in, tin0);
            c2l(in, tin1);

            t0 = tin0;
            t1 = tin1;

            tin[0] = tin0;
            tin[1] = tin1;
            DES_decrypt3((DES_LONG *)tin, ks1, ks2, ks3);
            tout0 = tin[0];
            tout1 = tin[1];

            tout0 ^= xor0;
            tout1 ^= xor1;
            l2cn(tout0, tout1, out, l + 8);
            xor0 = t0;
            xor1 = t1;
        }

        iv = &(*ivec)[0];
        l2c(xor0, iv);
        l2c(xor1, iv);
    }
    tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
    tin[0] = tin[1] = 0;
}

#endif                          /* DES_DEFAULT_OPTIONS */
