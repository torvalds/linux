/*
 * Copyright 2000-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/asn1t.h>

#if !(OPENSSL_API_COMPAT < 0x10200000L)
NON_EMPTY_TRANSLATION_UNIT
#else

#define COPY_SIZE(a, b) (sizeof(a) < sizeof(b) ? sizeof(a) : sizeof(b))

/*
 * Custom primitive type for long handling. This converts between an
 * ASN1_INTEGER and a long directly.
 */

static int long_new(ASN1_VALUE **pval, const ASN1_ITEM *it);
static void long_free(ASN1_VALUE **pval, const ASN1_ITEM *it);

static int long_i2c(ASN1_VALUE **pval, unsigned char *cont, int *putype,
                    const ASN1_ITEM *it);
static int long_c2i(ASN1_VALUE **pval, const unsigned char *cont, int len,
                    int utype, char *free_cont, const ASN1_ITEM *it);
static int long_print(BIO *out, ASN1_VALUE **pval, const ASN1_ITEM *it,
                      int indent, const ASN1_PCTX *pctx);

static ASN1_PRIMITIVE_FUNCS long_pf = {
    NULL, 0,
    long_new,
    long_free,
    long_free,                  /* Clear should set to initial value */
    long_c2i,
    long_i2c,
    long_print
};

ASN1_ITEM_start(LONG)
        ASN1_ITYPE_PRIMITIVE, V_ASN1_INTEGER, NULL, 0, &long_pf, ASN1_LONG_UNDEF, "LONG"
ASN1_ITEM_end(LONG)

ASN1_ITEM_start(ZLONG)
        ASN1_ITYPE_PRIMITIVE, V_ASN1_INTEGER, NULL, 0, &long_pf, 0, "ZLONG"
ASN1_ITEM_end(ZLONG)

static int long_new(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    memcpy(pval, &it->size, COPY_SIZE(*pval, it->size));
    return 1;
}

static void long_free(ASN1_VALUE **pval, const ASN1_ITEM *it)
{
    memcpy(pval, &it->size, COPY_SIZE(*pval, it->size));
}

/*
 * Originally BN_num_bits_word was called to perform this operation, but
 * trouble is that there is no guarantee that sizeof(long) equals to
 * sizeof(BN_ULONG). BN_ULONG is a configurable type that can be as wide
 * as long, but also double or half...
 */
static int num_bits_ulong(unsigned long value)
{
    size_t i;
    unsigned long ret = 0;

    /*
     * It is argued that *on average* constant counter loop performs
     * not worse [if not better] than one with conditional break or
     * mask-n-table-lookup-style, because of branch misprediction
     * penalties.
     */
    for (i = 0; i < sizeof(value) * 8; i++) {
        ret += (value != 0);
        value >>= 1;
    }

    return (int)ret;
}

static int long_i2c(ASN1_VALUE **pval, unsigned char *cont, int *putype,
                    const ASN1_ITEM *it)
{
    long ltmp;
    unsigned long utmp, sign;
    int clen, pad, i;

    memcpy(&ltmp, pval, COPY_SIZE(*pval, ltmp));
    if (ltmp == it->size)
        return -1;
    /*
     * Convert the long to positive: we subtract one if negative so we can
     * cleanly handle the padding if only the MSB of the leading octet is
     * set.
     */
    if (ltmp < 0) {
        sign = 0xff;
        utmp = 0 - (unsigned long)ltmp - 1;
    } else {
        sign = 0;
        utmp = ltmp;
    }
    clen = num_bits_ulong(utmp);
    /* If MSB of leading octet set we need to pad */
    if (!(clen & 0x7))
        pad = 1;
    else
        pad = 0;

    /* Convert number of bits to number of octets */
    clen = (clen + 7) >> 3;

    if (cont != NULL) {
        if (pad)
            *cont++ = (unsigned char)sign;
        for (i = clen - 1; i >= 0; i--) {
            cont[i] = (unsigned char)(utmp ^ sign);
            utmp >>= 8;
        }
    }
    return clen + pad;
}

static int long_c2i(ASN1_VALUE **pval, const unsigned char *cont, int len,
                    int utype, char *free_cont, const ASN1_ITEM *it)
{
    int i;
    long ltmp;
    unsigned long utmp = 0, sign = 0x100;

    if (len > 1) {
        /*
         * Check possible pad byte.  Worst case, we're skipping past actual
         * content, but since that's only with 0x00 and 0xff and we set neg
         * accordingly, the result will be correct in the end anyway.
         */
        switch (cont[0]) {
        case 0xff:
            cont++;
            len--;
            sign = 0xff;
            break;
        case 0:
            cont++;
            len--;
            sign = 0;
            break;
        }
    }
    if (len > (int)sizeof(long)) {
        ASN1err(ASN1_F_LONG_C2I, ASN1_R_INTEGER_TOO_LARGE_FOR_LONG);
        return 0;
    }

    if (sign == 0x100) {
        /* Is it negative? */
        if (len && (cont[0] & 0x80))
            sign = 0xff;
        else
            sign = 0;
    } else if (((sign ^ cont[0]) & 0x80) == 0) { /* same sign bit? */
        ASN1err(ASN1_F_LONG_C2I, ASN1_R_ILLEGAL_PADDING);
        return 0;
    }
    utmp = 0;
    for (i = 0; i < len; i++) {
        utmp <<= 8;
        utmp |= cont[i] ^ sign;
    }
    ltmp = (long)utmp;
    if (ltmp < 0) {
        ASN1err(ASN1_F_LONG_C2I, ASN1_R_INTEGER_TOO_LARGE_FOR_LONG);
        return 0;
    }
    if (sign)
        ltmp = -ltmp - 1;
    if (ltmp == it->size) {
        ASN1err(ASN1_F_LONG_C2I, ASN1_R_INTEGER_TOO_LARGE_FOR_LONG);
        return 0;
    }
    memcpy(pval, &ltmp, COPY_SIZE(*pval, ltmp));
    return 1;
}

static int long_print(BIO *out, ASN1_VALUE **pval, const ASN1_ITEM *it,
                      int indent, const ASN1_PCTX *pctx)
{
    long l;

    memcpy(&l, pval, COPY_SIZE(*pval, l));
    return BIO_printf(out, "%ld\n", l);
}
#endif
