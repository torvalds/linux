/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>
#include <openssl/des.h>
#include <openssl/mdc2.h>

#undef c2l
#define c2l(c,l)        (l =((DES_LONG)(*((c)++)))    , \
                         l|=((DES_LONG)(*((c)++)))<< 8L, \
                         l|=((DES_LONG)(*((c)++)))<<16L, \
                         l|=((DES_LONG)(*((c)++)))<<24L)

#undef l2c
#define l2c(l,c)        (*((c)++)=(unsigned char)(((l)     )&0xff), \
                        *((c)++)=(unsigned char)(((l)>> 8L)&0xff), \
                        *((c)++)=(unsigned char)(((l)>>16L)&0xff), \
                        *((c)++)=(unsigned char)(((l)>>24L)&0xff))

static void mdc2_body(MDC2_CTX *c, const unsigned char *in, size_t len);
int MDC2_Init(MDC2_CTX *c)
{
    c->num = 0;
    c->pad_type = 1;
    memset(&(c->h[0]), 0x52, MDC2_BLOCK);
    memset(&(c->hh[0]), 0x25, MDC2_BLOCK);
    return 1;
}

int MDC2_Update(MDC2_CTX *c, const unsigned char *in, size_t len)
{
    size_t i, j;

    i = c->num;
    if (i != 0) {
        if (len < MDC2_BLOCK - i) {
            /* partial block */
            memcpy(&(c->data[i]), in, len);
            c->num += (int)len;
            return 1;
        } else {
            /* filled one */
            j = MDC2_BLOCK - i;
            memcpy(&(c->data[i]), in, j);
            len -= j;
            in += j;
            c->num = 0;
            mdc2_body(c, &(c->data[0]), MDC2_BLOCK);
        }
    }
    i = len & ~((size_t)MDC2_BLOCK - 1);
    if (i > 0)
        mdc2_body(c, in, i);
    j = len - i;
    if (j > 0) {
        memcpy(&(c->data[0]), &(in[i]), j);
        c->num = (int)j;
    }
    return 1;
}

static void mdc2_body(MDC2_CTX *c, const unsigned char *in, size_t len)
{
    register DES_LONG tin0, tin1;
    register DES_LONG ttin0, ttin1;
    DES_LONG d[2], dd[2];
    DES_key_schedule k;
    unsigned char *p;
    size_t i;

    for (i = 0; i < len; i += 8) {
        c2l(in, tin0);
        d[0] = dd[0] = tin0;
        c2l(in, tin1);
        d[1] = dd[1] = tin1;
        c->h[0] = (c->h[0] & 0x9f) | 0x40;
        c->hh[0] = (c->hh[0] & 0x9f) | 0x20;

        DES_set_odd_parity(&c->h);
        DES_set_key_unchecked(&c->h, &k);
        DES_encrypt1(d, &k, 1);

        DES_set_odd_parity(&c->hh);
        DES_set_key_unchecked(&c->hh, &k);
        DES_encrypt1(dd, &k, 1);

        ttin0 = tin0 ^ dd[0];
        ttin1 = tin1 ^ dd[1];
        tin0 ^= d[0];
        tin1 ^= d[1];

        p = c->h;
        l2c(tin0, p);
        l2c(ttin1, p);
        p = c->hh;
        l2c(ttin0, p);
        l2c(tin1, p);
    }
}

int MDC2_Final(unsigned char *md, MDC2_CTX *c)
{
    unsigned int i;
    int j;

    i = c->num;
    j = c->pad_type;
    if ((i > 0) || (j == 2)) {
        if (j == 2)
            c->data[i++] = 0x80;
        memset(&(c->data[i]), 0, MDC2_BLOCK - i);
        mdc2_body(c, c->data, MDC2_BLOCK);
    }
    memcpy(md, (char *)c->h, MDC2_BLOCK);
    memcpy(&(md[MDC2_BLOCK]), (char *)c->hh, MDC2_BLOCK);
    return 1;
}
