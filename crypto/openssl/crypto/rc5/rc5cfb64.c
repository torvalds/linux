/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/rc5.h>
#include "rc5_locl.h"

/*
 * The input and output encrypted as though 64bit cfb mode is being used.
 * The extra state information to record how much of the 64bit block we have
 * used is contained in *num;
 */

void RC5_32_cfb64_encrypt(const unsigned char *in, unsigned char *out,
                          long length, RC5_32_KEY *schedule,
                          unsigned char *ivec, int *num, int encrypt)
{
    register unsigned long v0, v1, t;
    register int n = *num;
    register long l = length;
    unsigned long ti[2];
    unsigned char *iv, c, cc;

    iv = (unsigned char *)ivec;
    if (encrypt) {
        while (l--) {
            if (n == 0) {
                c2l(iv, v0);
                ti[0] = v0;
                c2l(iv, v1);
                ti[1] = v1;
                RC5_32_encrypt((unsigned long *)ti, schedule);
                iv = (unsigned char *)ivec;
                t = ti[0];
                l2c(t, iv);
                t = ti[1];
                l2c(t, iv);
                iv = (unsigned char *)ivec;
            }
            c = *(in++) ^ iv[n];
            *(out++) = c;
            iv[n] = c;
            n = (n + 1) & 0x07;
        }
    } else {
        while (l--) {
            if (n == 0) {
                c2l(iv, v0);
                ti[0] = v0;
                c2l(iv, v1);
                ti[1] = v1;
                RC5_32_encrypt((unsigned long *)ti, schedule);
                iv = (unsigned char *)ivec;
                t = ti[0];
                l2c(t, iv);
                t = ti[1];
                l2c(t, iv);
                iv = (unsigned char *)ivec;
            }
            cc = *(in++);
            c = iv[n];
            iv[n] = cc;
            *(out++) = c ^ cc;
            n = (n + 1) & 0x07;
        }
    }
    v0 = v1 = ti[0] = ti[1] = t = c = cc = 0;
    *num = n;
}
