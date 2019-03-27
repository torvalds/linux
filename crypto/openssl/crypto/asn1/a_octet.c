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
#include <openssl/asn1.h>

ASN1_OCTET_STRING *ASN1_OCTET_STRING_dup(const ASN1_OCTET_STRING *x)
{
    return ASN1_STRING_dup(x);
}

int ASN1_OCTET_STRING_cmp(const ASN1_OCTET_STRING *a,
                          const ASN1_OCTET_STRING *b)
{
    return ASN1_STRING_cmp(a, b);
}

int ASN1_OCTET_STRING_set(ASN1_OCTET_STRING *x, const unsigned char *d,
                          int len)
{
    return ASN1_STRING_set(x, d, len);
}
