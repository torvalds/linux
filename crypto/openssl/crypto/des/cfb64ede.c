/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "des_locl.h"

/*
 * The input and output encrypted as though 64bit cfb mode is being used.
 * The extra state information to record how much of the 64bit block we have
 * used is contained in *num;
 */

void DES_ede3_cfb64_encrypt(const unsigned char *in, unsigned char *out,
                            long length, DES_key_schedule *ks1,
                            DES_key_schedule *ks2, DES_key_schedule *ks3,
                            DES_cblock *ivec, int *num, int enc)
{
    register DES_LONG v0, v1;
    register long l = length;
    register int n = *num;
    DES_LONG ti[2];
    unsigned char *iv, c, cc;

    iv = &(*ivec)[0];
    if (enc) {
        while (l--) {
            if (n == 0) {
                c2l(iv, v0);
                c2l(iv, v1);

                ti[0] = v0;
                ti[1] = v1;
                DES_encrypt3(ti, ks1, ks2, ks3);
                v0 = ti[0];
                v1 = ti[1];

                iv = &(*ivec)[0];
                l2c(v0, iv);
                l2c(v1, iv);
                iv = &(*ivec)[0];
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
                c2l(iv, v1);

                ti[0] = v0;
                ti[1] = v1;
                DES_encrypt3(ti, ks1, ks2, ks3);
                v0 = ti[0];
                v1 = ti[1];

                iv = &(*ivec)[0];
                l2c(v0, iv);
                l2c(v1, iv);
                iv = &(*ivec)[0];
            }
            cc = *(in++);
            c = iv[n];
            iv[n] = cc;
            *(out++) = c ^ cc;
            n = (n + 1) & 0x07;
        }
    }
    v0 = v1 = ti[0] = ti[1] = c = cc = 0;
    *num = n;
}

/*
 * This is compatible with the single key CFB-r for DES, even thought that's
 * not what EVP needs.
 */

void DES_ede3_cfb_encrypt(const unsigned char *in, unsigned char *out,
                          int numbits, long length, DES_key_schedule *ks1,
                          DES_key_schedule *ks2, DES_key_schedule *ks3,
                          DES_cblock *ivec, int enc)
{
    register DES_LONG d0, d1, v0, v1;
    register unsigned long l = length, n = ((unsigned int)numbits + 7) / 8;
    register int num = numbits, i;
    DES_LONG ti[2];
    unsigned char *iv;
    unsigned char ovec[16];

    if (num > 64)
        return;
    iv = &(*ivec)[0];
    c2l(iv, v0);
    c2l(iv, v1);
    if (enc) {
        while (l >= n) {
            l -= n;
            ti[0] = v0;
            ti[1] = v1;
            DES_encrypt3(ti, ks1, ks2, ks3);
            c2ln(in, d0, d1, n);
            in += n;
            d0 ^= ti[0];
            d1 ^= ti[1];
            l2cn(d0, d1, out, n);
            out += n;
            /*
             * 30-08-94 - eay - changed because l>>32 and l<<32 are bad under
             * gcc :-(
             */
            if (num == 32) {
                v0 = v1;
                v1 = d0;
            } else if (num == 64) {
                v0 = d0;
                v1 = d1;
            } else {
                iv = &ovec[0];
                l2c(v0, iv);
                l2c(v1, iv);
                l2c(d0, iv);
                l2c(d1, iv);
                /* shift ovec left most of the bits... */
                memmove(ovec, ovec + num / 8, 8 + (num % 8 ? 1 : 0));
                /* now the remaining bits */
                if (num % 8 != 0)
                    for (i = 0; i < 8; ++i) {
                        ovec[i] <<= num % 8;
                        ovec[i] |= ovec[i + 1] >> (8 - num % 8);
                    }
                iv = &ovec[0];
                c2l(iv, v0);
                c2l(iv, v1);
            }
        }
    } else {
        while (l >= n) {
            l -= n;
            ti[0] = v0;
            ti[1] = v1;
            DES_encrypt3(ti, ks1, ks2, ks3);
            c2ln(in, d0, d1, n);
            in += n;
            /*
             * 30-08-94 - eay - changed because l>>32 and l<<32 are bad under
             * gcc :-(
             */
            if (num == 32) {
                v0 = v1;
                v1 = d0;
            } else if (num == 64) {
                v0 = d0;
                v1 = d1;
            } else {
                iv = &ovec[0];
                l2c(v0, iv);
                l2c(v1, iv);
                l2c(d0, iv);
                l2c(d1, iv);
                /* shift ovec left most of the bits... */
                memmove(ovec, ovec + num / 8, 8 + (num % 8 ? 1 : 0));
                /* now the remaining bits */
                if (num % 8 != 0)
                    for (i = 0; i < 8; ++i) {
                        ovec[i] <<= num % 8;
                        ovec[i] |= ovec[i + 1] >> (8 - num % 8);
                    }
                iv = &ovec[0];
                c2l(iv, v0);
                c2l(iv, v1);
            }
            d0 ^= ti[0];
            d1 ^= ti[1];
            l2cn(d0, d1, out, n);
            out += n;
        }
    }
    iv = &(*ivec)[0];
    l2c(v0, iv);
    l2c(v1, iv);
    v0 = v1 = d0 = d1 = ti[0] = ti[1] = 0;
}
