/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/rc4.h>
#include "rc4_locl.h"
#include <openssl/opensslv.h>

const char *RC4_options(void)
{
    if (sizeof(RC4_INT) == 1)
        return "rc4(char)";
    else
        return "rc4(int)";
}

/*-
 * RC4 as implemented from a posting from
 * Newsgroups: sci.crypt
 * Subject: RC4 Algorithm revealed.
 * Message-ID: <sternCvKL4B.Hyy@netcom.com>
 * Date: Wed, 14 Sep 1994 06:35:31 GMT
 */

void RC4_set_key(RC4_KEY *key, int len, const unsigned char *data)
{
    register RC4_INT tmp;
    register int id1, id2;
    register RC4_INT *d;
    unsigned int i;

    d = &(key->data[0]);
    key->x = 0;
    key->y = 0;
    id1 = id2 = 0;

#define SK_LOOP(d,n) { \
                tmp=d[(n)]; \
                id2 = (data[id1] + tmp + id2) & 0xff; \
                if (++id1 == len) id1=0; \
                d[(n)]=d[id2]; \
                d[id2]=tmp; }

    for (i = 0; i < 256; i++)
        d[i] = i;
    for (i = 0; i < 256; i += 4) {
        SK_LOOP(d, i + 0);
        SK_LOOP(d, i + 1);
        SK_LOOP(d, i + 2);
        SK_LOOP(d, i + 3);
    }
}
