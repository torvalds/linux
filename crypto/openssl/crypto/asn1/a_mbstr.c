/*
 * Copyright 1999-2017 The OpenSSL Project Authors. All Rights Reserved.
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

static int traverse_string(const unsigned char *p, int len, int inform,
                           int (*rfunc) (unsigned long value, void *in),
                           void *arg);
static int in_utf8(unsigned long value, void *arg);
static int out_utf8(unsigned long value, void *arg);
static int type_str(unsigned long value, void *arg);
static int cpy_asc(unsigned long value, void *arg);
static int cpy_bmp(unsigned long value, void *arg);
static int cpy_univ(unsigned long value, void *arg);
static int cpy_utf8(unsigned long value, void *arg);

/*
 * These functions take a string in UTF8, ASCII or multibyte form and a mask
 * of permissible ASN1 string types. It then works out the minimal type
 * (using the order Numeric < Printable < IA5 < T61 < BMP < Universal < UTF8)
 * and creates a string of the correct type with the supplied data. Yes this is
 * horrible: it has to be :-( The 'ncopy' form checks minimum and maximum
 * size limits too.
 */

int ASN1_mbstring_copy(ASN1_STRING **out, const unsigned char *in, int len,
                       int inform, unsigned long mask)
{
    return ASN1_mbstring_ncopy(out, in, len, inform, mask, 0, 0);
}

int ASN1_mbstring_ncopy(ASN1_STRING **out, const unsigned char *in, int len,
                        int inform, unsigned long mask,
                        long minsize, long maxsize)
{
    int str_type;
    int ret;
    char free_out;
    int outform, outlen = 0;
    ASN1_STRING *dest;
    unsigned char *p;
    int nchar;
    char strbuf[32];
    int (*cpyfunc) (unsigned long, void *) = NULL;
    if (len == -1)
        len = strlen((const char *)in);
    if (!mask)
        mask = DIRSTRING_TYPE;

    /* First do a string check and work out the number of characters */
    switch (inform) {

    case MBSTRING_BMP:
        if (len & 1) {
            ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY,
                    ASN1_R_INVALID_BMPSTRING_LENGTH);
            return -1;
        }
        nchar = len >> 1;
        break;

    case MBSTRING_UNIV:
        if (len & 3) {
            ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY,
                    ASN1_R_INVALID_UNIVERSALSTRING_LENGTH);
            return -1;
        }
        nchar = len >> 2;
        break;

    case MBSTRING_UTF8:
        nchar = 0;
        /* This counts the characters and does utf8 syntax checking */
        ret = traverse_string(in, len, MBSTRING_UTF8, in_utf8, &nchar);
        if (ret < 0) {
            ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ASN1_R_INVALID_UTF8STRING);
            return -1;
        }
        break;

    case MBSTRING_ASC:
        nchar = len;
        break;

    default:
        ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ASN1_R_UNKNOWN_FORMAT);
        return -1;
    }

    if ((minsize > 0) && (nchar < minsize)) {
        ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ASN1_R_STRING_TOO_SHORT);
        BIO_snprintf(strbuf, sizeof(strbuf), "%ld", minsize);
        ERR_add_error_data(2, "minsize=", strbuf);
        return -1;
    }

    if ((maxsize > 0) && (nchar > maxsize)) {
        ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ASN1_R_STRING_TOO_LONG);
        BIO_snprintf(strbuf, sizeof(strbuf), "%ld", maxsize);
        ERR_add_error_data(2, "maxsize=", strbuf);
        return -1;
    }

    /* Now work out minimal type (if any) */
    if (traverse_string(in, len, inform, type_str, &mask) < 0) {
        ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ASN1_R_ILLEGAL_CHARACTERS);
        return -1;
    }

    /* Now work out output format and string type */
    outform = MBSTRING_ASC;
    if (mask & B_ASN1_NUMERICSTRING)
        str_type = V_ASN1_NUMERICSTRING;
    else if (mask & B_ASN1_PRINTABLESTRING)
        str_type = V_ASN1_PRINTABLESTRING;
    else if (mask & B_ASN1_IA5STRING)
        str_type = V_ASN1_IA5STRING;
    else if (mask & B_ASN1_T61STRING)
        str_type = V_ASN1_T61STRING;
    else if (mask & B_ASN1_BMPSTRING) {
        str_type = V_ASN1_BMPSTRING;
        outform = MBSTRING_BMP;
    } else if (mask & B_ASN1_UNIVERSALSTRING) {
        str_type = V_ASN1_UNIVERSALSTRING;
        outform = MBSTRING_UNIV;
    } else {
        str_type = V_ASN1_UTF8STRING;
        outform = MBSTRING_UTF8;
    }
    if (!out)
        return str_type;
    if (*out) {
        free_out = 0;
        dest = *out;
        OPENSSL_free(dest->data);
        dest->data = NULL;
        dest->length = 0;
        dest->type = str_type;
    } else {
        free_out = 1;
        dest = ASN1_STRING_type_new(str_type);
        if (dest == NULL) {
            ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        *out = dest;
    }
    /* If both the same type just copy across */
    if (inform == outform) {
        if (!ASN1_STRING_set(dest, in, len)) {
            ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ERR_R_MALLOC_FAILURE);
            return -1;
        }
        return str_type;
    }

    /* Work out how much space the destination will need */
    switch (outform) {
    case MBSTRING_ASC:
        outlen = nchar;
        cpyfunc = cpy_asc;
        break;

    case MBSTRING_BMP:
        outlen = nchar << 1;
        cpyfunc = cpy_bmp;
        break;

    case MBSTRING_UNIV:
        outlen = nchar << 2;
        cpyfunc = cpy_univ;
        break;

    case MBSTRING_UTF8:
        outlen = 0;
        traverse_string(in, len, inform, out_utf8, &outlen);
        cpyfunc = cpy_utf8;
        break;
    }
    if ((p = OPENSSL_malloc(outlen + 1)) == NULL) {
        if (free_out)
            ASN1_STRING_free(dest);
        ASN1err(ASN1_F_ASN1_MBSTRING_NCOPY, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    dest->length = outlen;
    dest->data = p;
    p[outlen] = 0;
    traverse_string(in, len, inform, cpyfunc, &p);
    return str_type;
}

/*
 * This function traverses a string and passes the value of each character to
 * an optional function along with a void * argument.
 */

static int traverse_string(const unsigned char *p, int len, int inform,
                           int (*rfunc) (unsigned long value, void *in),
                           void *arg)
{
    unsigned long value;
    int ret;
    while (len) {
        if (inform == MBSTRING_ASC) {
            value = *p++;
            len--;
        } else if (inform == MBSTRING_BMP) {
            value = *p++ << 8;
            value |= *p++;
            len -= 2;
        } else if (inform == MBSTRING_UNIV) {
            value = ((unsigned long)*p++) << 24;
            value |= ((unsigned long)*p++) << 16;
            value |= *p++ << 8;
            value |= *p++;
            len -= 4;
        } else {
            ret = UTF8_getc(p, len, &value);
            if (ret < 0)
                return -1;
            len -= ret;
            p += ret;
        }
        if (rfunc) {
            ret = rfunc(value, arg);
            if (ret <= 0)
                return ret;
        }
    }
    return 1;
}

/* Various utility functions for traverse_string */

/* Just count number of characters */

static int in_utf8(unsigned long value, void *arg)
{
    int *nchar;
    nchar = arg;
    (*nchar)++;
    return 1;
}

/* Determine size of output as a UTF8 String */

static int out_utf8(unsigned long value, void *arg)
{
    int *outlen;
    outlen = arg;
    *outlen += UTF8_putc(NULL, -1, value);
    return 1;
}

/*
 * Determine the "type" of a string: check each character against a supplied
 * "mask".
 */

static int type_str(unsigned long value, void *arg)
{
    unsigned long types = *((unsigned long *)arg);
    const int native = value > INT_MAX ? INT_MAX : ossl_fromascii(value);

    if ((types & B_ASN1_NUMERICSTRING) && !(ossl_isdigit(native)
                                            || native == ' '))
        types &= ~B_ASN1_NUMERICSTRING;
    if ((types & B_ASN1_PRINTABLESTRING) && !ossl_isasn1print(native))
        types &= ~B_ASN1_PRINTABLESTRING;
    if ((types & B_ASN1_IA5STRING) && !ossl_isascii(native))
        types &= ~B_ASN1_IA5STRING;
    if ((types & B_ASN1_T61STRING) && (value > 0xff))
        types &= ~B_ASN1_T61STRING;
    if ((types & B_ASN1_BMPSTRING) && (value > 0xffff))
        types &= ~B_ASN1_BMPSTRING;
    if (!types)
        return -1;
    *((unsigned long *)arg) = types;
    return 1;
}

/* Copy one byte per character ASCII like strings */

static int cpy_asc(unsigned long value, void *arg)
{
    unsigned char **p, *q;
    p = arg;
    q = *p;
    *q = (unsigned char)value;
    (*p)++;
    return 1;
}

/* Copy two byte per character BMPStrings */

static int cpy_bmp(unsigned long value, void *arg)
{
    unsigned char **p, *q;
    p = arg;
    q = *p;
    *q++ = (unsigned char)((value >> 8) & 0xff);
    *q = (unsigned char)(value & 0xff);
    *p += 2;
    return 1;
}

/* Copy four byte per character UniversalStrings */

static int cpy_univ(unsigned long value, void *arg)
{
    unsigned char **p, *q;
    p = arg;
    q = *p;
    *q++ = (unsigned char)((value >> 24) & 0xff);
    *q++ = (unsigned char)((value >> 16) & 0xff);
    *q++ = (unsigned char)((value >> 8) & 0xff);
    *q = (unsigned char)(value & 0xff);
    *p += 4;
    return 1;
}

/* Copy to a UTF8String */

static int cpy_utf8(unsigned long value, void *arg)
{
    unsigned char **p;
    int ret;
    p = arg;
    /* We already know there is enough room so pass 0xff as the length */
    ret = UTF8_putc(*p, 0xff, value);
    *p += ret;
    return 1;
}
