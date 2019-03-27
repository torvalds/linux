/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/ctype.h"
#include "internal/cryptlib.h"
#include <openssl/asn1.h>

int ASN1_PRINTABLE_type(const unsigned char *s, int len)
{
    int c;
    int ia5 = 0;
    int t61 = 0;

    if (len <= 0)
        len = -1;
    if (s == NULL)
        return V_ASN1_PRINTABLESTRING;

    while ((*s) && (len-- != 0)) {
        c = *(s++);
        if (!ossl_isasn1print(c))
            ia5 = 1;
        if (!ossl_isascii(c))
            t61 = 1;
    }
    if (t61)
        return V_ASN1_T61STRING;
    if (ia5)
        return V_ASN1_IA5STRING;
    return V_ASN1_PRINTABLESTRING;
}

int ASN1_UNIVERSALSTRING_to_string(ASN1_UNIVERSALSTRING *s)
{
    int i;
    unsigned char *p;

    if (s->type != V_ASN1_UNIVERSALSTRING)
        return 0;
    if ((s->length % 4) != 0)
        return 0;
    p = s->data;
    for (i = 0; i < s->length; i += 4) {
        if ((p[0] != '\0') || (p[1] != '\0') || (p[2] != '\0'))
            break;
        else
            p += 4;
    }
    if (i < s->length)
        return 0;
    p = s->data;
    for (i = 3; i < s->length; i += 4) {
        *(p++) = s->data[i];
    }
    *(p) = '\0';
    s->length /= 4;
    s->type = ASN1_PRINTABLE_type(s->data, s->length);
    return 1;
}

int ASN1_STRING_print(BIO *bp, const ASN1_STRING *v)
{
    int i, n;
    char buf[80];
    const char *p;

    if (v == NULL)
        return 0;
    n = 0;
    p = (const char *)v->data;
    for (i = 0; i < v->length; i++) {
        if ((p[i] > '~') || ((p[i] < ' ') &&
                             (p[i] != '\n') && (p[i] != '\r')))
            buf[n] = '.';
        else
            buf[n] = p[i];
        n++;
        if (n >= 80) {
            if (BIO_write(bp, buf, n) <= 0)
                return 0;
            n = 0;
        }
    }
    if (n > 0)
        if (BIO_write(bp, buf, n) <= 0)
            return 0;
    return 1;
}
