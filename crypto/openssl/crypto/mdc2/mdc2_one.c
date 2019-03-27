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
#include <openssl/mdc2.h>

unsigned char *MDC2(const unsigned char *d, size_t n, unsigned char *md)
{
    MDC2_CTX c;
    static unsigned char m[MDC2_DIGEST_LENGTH];

    if (md == NULL)
        md = m;
    if (!MDC2_Init(&c))
        return NULL;
    MDC2_Update(&c, d, n);
    MDC2_Final(md, &c);
    OPENSSL_cleanse(&c, sizeof(c)); /* security consideration */
    return md;
}
