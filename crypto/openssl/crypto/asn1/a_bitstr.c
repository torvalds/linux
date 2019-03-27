/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <limits.h>
#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/asn1.h>
#include "asn1_locl.h"

int ASN1_BIT_STRING_set(ASN1_BIT_STRING *x, unsigned char *d, int len)
{
    return ASN1_STRING_set(x, d, len);
}

int i2c_ASN1_BIT_STRING(ASN1_BIT_STRING *a, unsigned char **pp)
{
    int ret, j, bits, len;
    unsigned char *p, *d;

    if (a == NULL)
        return 0;

    len = a->length;

    if (len > 0) {
        if (a->flags & ASN1_STRING_FLAG_BITS_LEFT) {
            bits = (int)a->flags & 0x07;
        } else {
            for (; len > 0; len--) {
                if (a->data[len - 1])
                    break;
            }
            j = a->data[len - 1];
            if (j & 0x01)
                bits = 0;
            else if (j & 0x02)
                bits = 1;
            else if (j & 0x04)
                bits = 2;
            else if (j & 0x08)
                bits = 3;
            else if (j & 0x10)
                bits = 4;
            else if (j & 0x20)
                bits = 5;
            else if (j & 0x40)
                bits = 6;
            else if (j & 0x80)
                bits = 7;
            else
                bits = 0;       /* should not happen */
        }
    } else
        bits = 0;

    ret = 1 + len;
    if (pp == NULL)
        return ret;

    p = *pp;

    *(p++) = (unsigned char)bits;
    d = a->data;
    if (len > 0) {
        memcpy(p, d, len);
        p += len;
        p[-1] &= (0xff << bits);
    }
    *pp = p;
    return ret;
}

ASN1_BIT_STRING *c2i_ASN1_BIT_STRING(ASN1_BIT_STRING **a,
                                     const unsigned char **pp, long len)
{
    ASN1_BIT_STRING *ret = NULL;
    const unsigned char *p;
    unsigned char *s;
    int i;

    if (len < 1) {
        i = ASN1_R_STRING_TOO_SHORT;
        goto err;
    }

    if (len > INT_MAX) {
        i = ASN1_R_STRING_TOO_LONG;
        goto err;
    }

    if ((a == NULL) || ((*a) == NULL)) {
        if ((ret = ASN1_BIT_STRING_new()) == NULL)
            return NULL;
    } else
        ret = (*a);

    p = *pp;
    i = *(p++);
    if (i > 7) {
        i = ASN1_R_INVALID_BIT_STRING_BITS_LEFT;
        goto err;
    }
    /*
     * We do this to preserve the settings.  If we modify the settings, via
     * the _set_bit function, we will recalculate on output
     */
    ret->flags &= ~(ASN1_STRING_FLAG_BITS_LEFT | 0x07); /* clear */
    ret->flags |= (ASN1_STRING_FLAG_BITS_LEFT | i); /* set */

    if (len-- > 1) {            /* using one because of the bits left byte */
        s = OPENSSL_malloc((int)len);
        if (s == NULL) {
            i = ERR_R_MALLOC_FAILURE;
            goto err;
        }
        memcpy(s, p, (int)len);
        s[len - 1] &= (0xff << i);
        p += len;
    } else
        s = NULL;

    ret->length = (int)len;
    OPENSSL_free(ret->data);
    ret->data = s;
    ret->type = V_ASN1_BIT_STRING;
    if (a != NULL)
        (*a) = ret;
    *pp = p;
    return ret;
 err:
    ASN1err(ASN1_F_C2I_ASN1_BIT_STRING, i);
    if ((a == NULL) || (*a != ret))
        ASN1_BIT_STRING_free(ret);
    return NULL;
}

/*
 * These next 2 functions from Goetz Babin-Ebell.
 */
int ASN1_BIT_STRING_set_bit(ASN1_BIT_STRING *a, int n, int value)
{
    int w, v, iv;
    unsigned char *c;

    w = n / 8;
    v = 1 << (7 - (n & 0x07));
    iv = ~v;
    if (!value)
        v = 0;

    if (a == NULL)
        return 0;

    a->flags &= ~(ASN1_STRING_FLAG_BITS_LEFT | 0x07); /* clear, set on write */

    if ((a->length < (w + 1)) || (a->data == NULL)) {
        if (!value)
            return 1;         /* Don't need to set */
        c = OPENSSL_clear_realloc(a->data, a->length, w + 1);
        if (c == NULL) {
            ASN1err(ASN1_F_ASN1_BIT_STRING_SET_BIT, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        if (w + 1 - a->length > 0)
            memset(c + a->length, 0, w + 1 - a->length);
        a->data = c;
        a->length = w + 1;
    }
    a->data[w] = ((a->data[w]) & iv) | v;
    while ((a->length > 0) && (a->data[a->length - 1] == 0))
        a->length--;
    return 1;
}

int ASN1_BIT_STRING_get_bit(const ASN1_BIT_STRING *a, int n)
{
    int w, v;

    w = n / 8;
    v = 1 << (7 - (n & 0x07));
    if ((a == NULL) || (a->length < (w + 1)) || (a->data == NULL))
        return 0;
    return ((a->data[w] & v) != 0);
}

/*
 * Checks if the given bit string contains only bits specified by
 * the flags vector. Returns 0 if there is at least one bit set in 'a'
 * which is not specified in 'flags', 1 otherwise.
 * 'len' is the length of 'flags'.
 */
int ASN1_BIT_STRING_check(const ASN1_BIT_STRING *a,
                          const unsigned char *flags, int flags_len)
{
    int i, ok;
    /* Check if there is one bit set at all. */
    if (!a || !a->data)
        return 1;

    /*
     * Check each byte of the internal representation of the bit string.
     */
    ok = 1;
    for (i = 0; i < a->length && ok; ++i) {
        unsigned char mask = i < flags_len ? ~flags[i] : 0xff;
        /* We are done if there is an unneeded bit set. */
        ok = (a->data[i] & mask) == 0;
    }
    return ok;
}
