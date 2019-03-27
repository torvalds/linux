/*
 * Copyright 1998-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*-
 * #included by:
 *    cbc_enc.c  (DES_cbc_encrypt)
 *    des_enc.c  (DES_ncbc_encrypt)
 */

#include "des_locl.h"

#ifdef CBC_ENC_C__DONT_UPDATE_IV
void DES_cbc_encrypt(const unsigned char *in, unsigned char *out, long length,
                     DES_key_schedule *_schedule, DES_cblock *ivec, int enc)
#else
void DES_ncbc_encrypt(const unsigned char *in, unsigned char *out,
                      long length, DES_key_schedule *_schedule,
                      DES_cblock *ivec, int enc)
#endif
{
    register DES_LONG tin0, tin1;
    register DES_LONG tout0, tout1, xor0, xor1;
    register long l = length;
    DES_LONG tin[2];
    unsigned char *iv;

    iv = &(*ivec)[0];

    if (enc) {
        c2l(iv, tout0);
        c2l(iv, tout1);
        for (l -= 8; l >= 0; l -= 8) {
            c2l(in, tin0);
            c2l(in, tin1);
            tin0 ^= tout0;
            tin[0] = tin0;
            tin1 ^= tout1;
            tin[1] = tin1;
            DES_encrypt1((DES_LONG *)tin, _schedule, DES_ENCRYPT);
            tout0 = tin[0];
            l2c(tout0, out);
            tout1 = tin[1];
            l2c(tout1, out);
        }
        if (l != -8) {
            c2ln(in, tin0, tin1, l + 8);
            tin0 ^= tout0;
            tin[0] = tin0;
            tin1 ^= tout1;
            tin[1] = tin1;
            DES_encrypt1((DES_LONG *)tin, _schedule, DES_ENCRYPT);
            tout0 = tin[0];
            l2c(tout0, out);
            tout1 = tin[1];
            l2c(tout1, out);
        }
#ifndef CBC_ENC_C__DONT_UPDATE_IV
        iv = &(*ivec)[0];
        l2c(tout0, iv);
        l2c(tout1, iv);
#endif
    } else {
        c2l(iv, xor0);
        c2l(iv, xor1);
        for (l -= 8; l >= 0; l -= 8) {
            c2l(in, tin0);
            tin[0] = tin0;
            c2l(in, tin1);
            tin[1] = tin1;
            DES_encrypt1((DES_LONG *)tin, _schedule, DES_DECRYPT);
            tout0 = tin[0] ^ xor0;
            tout1 = tin[1] ^ xor1;
            l2c(tout0, out);
            l2c(tout1, out);
            xor0 = tin0;
            xor1 = tin1;
        }
        if (l != -8) {
            c2l(in, tin0);
            tin[0] = tin0;
            c2l(in, tin1);
            tin[1] = tin1;
            DES_encrypt1((DES_LONG *)tin, _schedule, DES_DECRYPT);
            tout0 = tin[0] ^ xor0;
            tout1 = tin[1] ^ xor1;
            l2cn(tout0, tout1, out, l + 8);
#ifndef CBC_ENC_C__DONT_UPDATE_IV
            xor0 = tin0;
            xor1 = tin1;
#endif
        }
#ifndef CBC_ENC_C__DONT_UPDATE_IV
        iv = &(*ivec)[0];
        l2c(xor0, iv);
        l2c(xor1, iv);
#endif
    }
    tin0 = tin1 = tout0 = tout1 = xor0 = xor1 = 0;
    tin[0] = tin[1] = 0;
}
