/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/md2.h>

/*
 * This is a separate file so that #defines in cryptlib.h can map my MD
 * functions to different names
 */

unsigned char *MD2(const unsigned char *d, size_t n, unsigned char *md)
{
    MD2_CTX c;
    static unsigned char m[MD2_DIGEST_LENGTH];

    if (md == NULL)
        md = m;
    if (!MD2_Init(&c))
        return NULL;
#ifndef CHARSET_EBCDIC
    MD2_Update(&c, d, n);
#else
    {
        char temp[1024];
        unsigned long chunk;

        while (n > 0) {
            chunk = (n > sizeof(temp)) ? sizeof(temp) : n;
            ebcdic2ascii(temp, d, chunk);
            MD2_Update(&c, temp, chunk);
            n -= chunk;
            d += chunk;
        }
    }
#endif
    MD2_Final(md, &c);
    OPENSSL_cleanse(&c, sizeof(c)); /* Security consideration */
    return md;
}
