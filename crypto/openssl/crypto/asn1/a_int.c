/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include "internal/numbers.h"
#include <limits.h>
#include <openssl/asn1.h>
#include <openssl/bn.h>
#include "asn1_locl.h"

ASN1_INTEGER *ASN1_INTEGER_dup(const ASN1_INTEGER *x)
{
    return ASN1_STRING_dup(x);
}

int ASN1_INTEGER_cmp(const ASN1_INTEGER *x, const ASN1_INTEGER *y)
{
    int neg, ret;
    /* Compare signs */
    neg = x->type & V_ASN1_NEG;
    if (neg != (y->type & V_ASN1_NEG)) {
        if (neg)
            return -1;
        else
            return 1;
    }

    ret = ASN1_STRING_cmp(x, y);

    if (neg)
        return -ret;
    else
        return ret;
}

/*-
 * This converts a big endian buffer and sign into its content encoding.
 * This is used for INTEGER and ENUMERATED types.
 * The internal representation is an ASN1_STRING whose data is a big endian
 * representation of the value, ignoring the sign. The sign is determined by
 * the type: if type & V_ASN1_NEG is true it is negative, otherwise positive.
 *
 * Positive integers are no problem: they are almost the same as the DER
 * encoding, except if the first byte is >= 0x80 we need to add a zero pad.
 *
 * Negative integers are a bit trickier...
 * The DER representation of negative integers is in 2s complement form.
 * The internal form is converted by complementing each octet and finally
 * adding one to the result. This can be done less messily with a little trick.
 * If the internal form has trailing zeroes then they will become FF by the
 * complement and 0 by the add one (due to carry) so just copy as many trailing
 * zeros to the destination as there are in the source. The carry will add one
 * to the last none zero octet: so complement this octet and add one and finally
 * complement any left over until you get to the start of the string.
 *
 * Padding is a little trickier too. If the first bytes is > 0x80 then we pad
 * with 0xff. However if the first byte is 0x80 and one of the following bytes
 * is non-zero we pad with 0xff. The reason for this distinction is that 0x80
 * followed by optional zeros isn't padded.
 */

/*
 * If |pad| is zero, the operation is effectively reduced to memcpy,
 * and if |pad| is 0xff, then it performs two's complement, ~dst + 1.
 * Note that in latter case sequence of zeros yields itself, and so
 * does 0x80 followed by any number of zeros. These properties are
 * used elsewhere below...
 */
static void twos_complement(unsigned char *dst, const unsigned char *src,
                            size_t len, unsigned char pad)
{
    unsigned int carry = pad & 1;

    /* Begin at the end of the encoding */
    dst += len;
    src += len;
    /* two's complement value: ~value + 1 */
    while (len-- != 0) {
        *(--dst) = (unsigned char)(carry += *(--src) ^ pad);
        carry >>= 8;
    }
}

static size_t i2c_ibuf(const unsigned char *b, size_t blen, int neg,
                       unsigned char **pp)
{
    unsigned int pad = 0;
    size_t ret, i;
    unsigned char *p, pb = 0;

    if (b != NULL && blen) {
        ret = blen;
        i = b[0];
        if (!neg && (i > 127)) {
            pad = 1;
            pb = 0;
        } else if (neg) {
            pb = 0xFF;
            if (i > 128) {
                pad = 1;
            } else if (i == 128) {
                /*
                 * Special case [of minimal negative for given length]:
                 * if any other bytes non zero we pad, otherwise we don't.
                 */
                for (pad = 0, i = 1; i < blen; i++)
                    pad |= b[i];
                pb = pad != 0 ? 0xffU : 0;
                pad = pb & 1;
            }
        }
        ret += pad;
    } else {
        ret = 1;
        blen = 0;   /* reduce '(b == NULL || blen == 0)' to '(blen == 0)' */
    }

    if (pp == NULL || (p = *pp) == NULL)
        return ret;

    /*
     * This magically handles all corner cases, such as '(b == NULL ||
     * blen == 0)', non-negative value, "negative" zero, 0x80 followed
     * by any number of zeros...
     */
    *p = pb;
    p += pad;       /* yes, p[0] can be written twice, but it's little
                     * price to pay for eliminated branches */
    twos_complement(p, b, blen, pb);

    *pp += ret;
    return ret;
}

/*
 * convert content octets into a big endian buffer. Returns the length
 * of buffer or 0 on error: for malformed INTEGER. If output buffer is
 * NULL just return length.
 */

static size_t c2i_ibuf(unsigned char *b, int *pneg,
                       const unsigned char *p, size_t plen)
{
    int neg, pad;
    /* Zero content length is illegal */
    if (plen == 0) {
        ASN1err(ASN1_F_C2I_IBUF, ASN1_R_ILLEGAL_ZERO_CONTENT);
        return 0;
    }
    neg = p[0] & 0x80;
    if (pneg)
        *pneg = neg;
    /* Handle common case where length is 1 octet separately */
    if (plen == 1) {
        if (b != NULL) {
            if (neg)
                b[0] = (p[0] ^ 0xFF) + 1;
            else
                b[0] = p[0];
        }
        return 1;
    }

    pad = 0;
    if (p[0] == 0) {
        pad = 1;
    } else if (p[0] == 0xFF) {
        size_t i;

        /*
         * Special case [of "one less minimal negative" for given length]:
         * if any other bytes non zero it was padded, otherwise not.
         */
        for (pad = 0, i = 1; i < plen; i++)
            pad |= p[i];
        pad = pad != 0 ? 1 : 0;
    }
    /* reject illegal padding: first two octets MSB can't match */
    if (pad && (neg == (p[1] & 0x80))) {
        ASN1err(ASN1_F_C2I_IBUF, ASN1_R_ILLEGAL_PADDING);
        return 0;
    }

    /* skip over pad */
    p += pad;
    plen -= pad;

    if (b != NULL)
        twos_complement(b, p, plen, neg ? 0xffU : 0);

    return plen;
}

int i2c_ASN1_INTEGER(ASN1_INTEGER *a, unsigned char **pp)
{
    return i2c_ibuf(a->data, a->length, a->type & V_ASN1_NEG, pp);
}

/* Convert big endian buffer into uint64_t, return 0 on error */
static int asn1_get_uint64(uint64_t *pr, const unsigned char *b, size_t blen)
{
    size_t i;
    uint64_t r;

    if (blen > sizeof(*pr)) {
        ASN1err(ASN1_F_ASN1_GET_UINT64, ASN1_R_TOO_LARGE);
        return 0;
    }
    if (b == NULL)
        return 0;
    for (r = 0, i = 0; i < blen; i++) {
        r <<= 8;
        r |= b[i];
    }
    *pr = r;
    return 1;
}

/*
 * Write uint64_t to big endian buffer and return offset to first
 * written octet. In other words it returns offset in range from 0
 * to 7, with 0 denoting 8 written octets and 7 - one.
 */
static size_t asn1_put_uint64(unsigned char b[sizeof(uint64_t)], uint64_t r)
{
    size_t off = sizeof(uint64_t);

    do {
        b[--off] = (unsigned char)r;
    } while (r >>= 8);

    return off;
}

/*
 * Absolute value of INT64_MIN: we can't just use -INT64_MIN as gcc produces
 * overflow warnings.
 */
#define ABS_INT64_MIN ((uint64_t)INT64_MAX + (-(INT64_MIN + INT64_MAX)))

/* signed version of asn1_get_uint64 */
static int asn1_get_int64(int64_t *pr, const unsigned char *b, size_t blen,
                          int neg)
{
    uint64_t r;
    if (asn1_get_uint64(&r, b, blen) == 0)
        return 0;
    if (neg) {
        if (r <= INT64_MAX) {
            /* Most significant bit is guaranteed to be clear, negation
             * is guaranteed to be meaningful in platform-neutral sense. */
            *pr = -(int64_t)r;
        } else if (r == ABS_INT64_MIN) {
            /* This never happens if INT64_MAX == ABS_INT64_MIN, e.g.
             * on ones'-complement system. */
            *pr = (int64_t)(0 - r);
        } else {
            ASN1err(ASN1_F_ASN1_GET_INT64, ASN1_R_TOO_SMALL);
            return 0;
        }
    } else {
        if (r <= INT64_MAX) {
            *pr = (int64_t)r;
        } else {
            ASN1err(ASN1_F_ASN1_GET_INT64, ASN1_R_TOO_LARGE);
            return 0;
        }
    }
    return 1;
}

/* Convert ASN1 INTEGER content octets to ASN1_INTEGER structure */
ASN1_INTEGER *c2i_ASN1_INTEGER(ASN1_INTEGER **a, const unsigned char **pp,
                               long len)
{
    ASN1_INTEGER *ret = NULL;
    size_t r;
    int neg;

    r = c2i_ibuf(NULL, NULL, *pp, len);

    if (r == 0)
        return NULL;

    if ((a == NULL) || ((*a) == NULL)) {
        ret = ASN1_INTEGER_new();
        if (ret == NULL)
            return NULL;
        ret->type = V_ASN1_INTEGER;
    } else
        ret = *a;

    if (ASN1_STRING_set(ret, NULL, r) == 0)
        goto err;

    c2i_ibuf(ret->data, &neg, *pp, len);

    if (neg)
        ret->type |= V_ASN1_NEG;

    *pp += len;
    if (a != NULL)
        (*a) = ret;
    return ret;
 err:
    ASN1err(ASN1_F_C2I_ASN1_INTEGER, ERR_R_MALLOC_FAILURE);
    if ((a == NULL) || (*a != ret))
        ASN1_INTEGER_free(ret);
    return NULL;
}

static int asn1_string_get_int64(int64_t *pr, const ASN1_STRING *a, int itype)
{
    if (a == NULL) {
        ASN1err(ASN1_F_ASN1_STRING_GET_INT64, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if ((a->type & ~V_ASN1_NEG) != itype) {
        ASN1err(ASN1_F_ASN1_STRING_GET_INT64, ASN1_R_WRONG_INTEGER_TYPE);
        return 0;
    }
    return asn1_get_int64(pr, a->data, a->length, a->type & V_ASN1_NEG);
}

static int asn1_string_set_int64(ASN1_STRING *a, int64_t r, int itype)
{
    unsigned char tbuf[sizeof(r)];
    size_t off;

    a->type = itype;
    if (r < 0) {
        /* Most obvious '-r' triggers undefined behaviour for most
         * common INT64_MIN. Even though below '0 - (uint64_t)r' can
         * appear two's-complement centric, it does produce correct/
         * expected result even on one's-complement. This is because
         * cast to unsigned has to change bit pattern... */
        off = asn1_put_uint64(tbuf, 0 - (uint64_t)r);
        a->type |= V_ASN1_NEG;
    } else {
        off = asn1_put_uint64(tbuf, r);
        a->type &= ~V_ASN1_NEG;
    }
    return ASN1_STRING_set(a, tbuf + off, sizeof(tbuf) - off);
}

static int asn1_string_get_uint64(uint64_t *pr, const ASN1_STRING *a,
                                  int itype)
{
    if (a == NULL) {
        ASN1err(ASN1_F_ASN1_STRING_GET_UINT64, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if ((a->type & ~V_ASN1_NEG) != itype) {
        ASN1err(ASN1_F_ASN1_STRING_GET_UINT64, ASN1_R_WRONG_INTEGER_TYPE);
        return 0;
    }
    if (a->type & V_ASN1_NEG) {
        ASN1err(ASN1_F_ASN1_STRING_GET_UINT64, ASN1_R_ILLEGAL_NEGATIVE_VALUE);
        return 0;
    }
    return asn1_get_uint64(pr, a->data, a->length);
}

static int asn1_string_set_uint64(ASN1_STRING *a, uint64_t r, int itype)
{
    unsigned char tbuf[sizeof(r)];
    size_t off;

    a->type = itype;
    off = asn1_put_uint64(tbuf, r);
    return ASN1_STRING_set(a, tbuf + off, sizeof(tbuf) - off);
}

/*
 * This is a version of d2i_ASN1_INTEGER that ignores the sign bit of ASN1
 * integers: some broken software can encode a positive INTEGER with its MSB
 * set as negative (it doesn't add a padding zero).
 */

ASN1_INTEGER *d2i_ASN1_UINTEGER(ASN1_INTEGER **a, const unsigned char **pp,
                                long length)
{
    ASN1_INTEGER *ret = NULL;
    const unsigned char *p;
    unsigned char *s;
    long len;
    int inf, tag, xclass;
    int i;

    if ((a == NULL) || ((*a) == NULL)) {
        if ((ret = ASN1_INTEGER_new()) == NULL)
            return NULL;
        ret->type = V_ASN1_INTEGER;
    } else
        ret = (*a);

    p = *pp;
    inf = ASN1_get_object(&p, &len, &tag, &xclass, length);
    if (inf & 0x80) {
        i = ASN1_R_BAD_OBJECT_HEADER;
        goto err;
    }

    if (tag != V_ASN1_INTEGER) {
        i = ASN1_R_EXPECTING_AN_INTEGER;
        goto err;
    }

    /*
     * We must OPENSSL_malloc stuff, even for 0 bytes otherwise it signifies
     * a missing NULL parameter.
     */
    s = OPENSSL_malloc((int)len + 1);
    if (s == NULL) {
        i = ERR_R_MALLOC_FAILURE;
        goto err;
    }
    ret->type = V_ASN1_INTEGER;
    if (len) {
        if ((*p == 0) && (len != 1)) {
            p++;
            len--;
        }
        memcpy(s, p, (int)len);
        p += len;
    }

    OPENSSL_free(ret->data);
    ret->data = s;
    ret->length = (int)len;
    if (a != NULL)
        (*a) = ret;
    *pp = p;
    return ret;
 err:
    ASN1err(ASN1_F_D2I_ASN1_UINTEGER, i);
    if ((a == NULL) || (*a != ret))
        ASN1_INTEGER_free(ret);
    return NULL;
}

static ASN1_STRING *bn_to_asn1_string(const BIGNUM *bn, ASN1_STRING *ai,
                                      int atype)
{
    ASN1_INTEGER *ret;
    int len;

    if (ai == NULL) {
        ret = ASN1_STRING_type_new(atype);
    } else {
        ret = ai;
        ret->type = atype;
    }

    if (ret == NULL) {
        ASN1err(ASN1_F_BN_TO_ASN1_STRING, ERR_R_NESTED_ASN1_ERROR);
        goto err;
    }

    if (BN_is_negative(bn) && !BN_is_zero(bn))
        ret->type |= V_ASN1_NEG_INTEGER;

    len = BN_num_bytes(bn);

    if (len == 0)
        len = 1;

    if (ASN1_STRING_set(ret, NULL, len) == 0) {
        ASN1err(ASN1_F_BN_TO_ASN1_STRING, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /* Correct zero case */
    if (BN_is_zero(bn))
        ret->data[0] = 0;
    else
        len = BN_bn2bin(bn, ret->data);
    ret->length = len;
    return ret;
 err:
    if (ret != ai)
        ASN1_INTEGER_free(ret);
    return NULL;
}

static BIGNUM *asn1_string_to_bn(const ASN1_INTEGER *ai, BIGNUM *bn,
                                 int itype)
{
    BIGNUM *ret;

    if ((ai->type & ~V_ASN1_NEG) != itype) {
        ASN1err(ASN1_F_ASN1_STRING_TO_BN, ASN1_R_WRONG_INTEGER_TYPE);
        return NULL;
    }

    ret = BN_bin2bn(ai->data, ai->length, bn);
    if (ret == NULL) {
        ASN1err(ASN1_F_ASN1_STRING_TO_BN, ASN1_R_BN_LIB);
        return NULL;
    }
    if (ai->type & V_ASN1_NEG)
        BN_set_negative(ret, 1);
    return ret;
}

int ASN1_INTEGER_get_int64(int64_t *pr, const ASN1_INTEGER *a)
{
    return asn1_string_get_int64(pr, a, V_ASN1_INTEGER);
}

int ASN1_INTEGER_set_int64(ASN1_INTEGER *a, int64_t r)
{
    return asn1_string_set_int64(a, r, V_ASN1_INTEGER);
}

int ASN1_INTEGER_get_uint64(uint64_t *pr, const ASN1_INTEGER *a)
{
    return asn1_string_get_uint64(pr, a, V_ASN1_INTEGER);
}

int ASN1_INTEGER_set_uint64(ASN1_INTEGER *a, uint64_t r)
{
    return asn1_string_set_uint64(a, r, V_ASN1_INTEGER);
}

int ASN1_INTEGER_set(ASN1_INTEGER *a, long v)
{
    return ASN1_INTEGER_set_int64(a, v);
}

long ASN1_INTEGER_get(const ASN1_INTEGER *a)
{
    int i;
    int64_t r;
    if (a == NULL)
        return 0;
    i = ASN1_INTEGER_get_int64(&r, a);
    if (i == 0)
        return -1;
    if (r > LONG_MAX || r < LONG_MIN)
        return -1;
    return (long)r;
}

ASN1_INTEGER *BN_to_ASN1_INTEGER(const BIGNUM *bn, ASN1_INTEGER *ai)
{
    return bn_to_asn1_string(bn, ai, V_ASN1_INTEGER);
}

BIGNUM *ASN1_INTEGER_to_BN(const ASN1_INTEGER *ai, BIGNUM *bn)
{
    return asn1_string_to_bn(ai, bn, V_ASN1_INTEGER);
}

int ASN1_ENUMERATED_get_int64(int64_t *pr, const ASN1_ENUMERATED *a)
{
    return asn1_string_get_int64(pr, a, V_ASN1_ENUMERATED);
}

int ASN1_ENUMERATED_set_int64(ASN1_ENUMERATED *a, int64_t r)
{
    return asn1_string_set_int64(a, r, V_ASN1_ENUMERATED);
}

int ASN1_ENUMERATED_set(ASN1_ENUMERATED *a, long v)
{
    return ASN1_ENUMERATED_set_int64(a, v);
}

long ASN1_ENUMERATED_get(const ASN1_ENUMERATED *a)
{
    int i;
    int64_t r;
    if (a == NULL)
        return 0;
    if ((a->type & ~V_ASN1_NEG) != V_ASN1_ENUMERATED)
        return -1;
    if (a->length > (int)sizeof(long))
        return 0xffffffffL;
    i = ASN1_ENUMERATED_get_int64(&r, a);
    if (i == 0)
        return -1;
    if (r > LONG_MAX || r < LONG_MIN)
        return -1;
    return (long)r;
}

ASN1_ENUMERATED *BN_to_ASN1_ENUMERATED(const BIGNUM *bn, ASN1_ENUMERATED *ai)
{
    return bn_to_asn1_string(bn, ai, V_ASN1_ENUMERATED);
}

BIGNUM *ASN1_ENUMERATED_to_BN(const ASN1_ENUMERATED *ai, BIGNUM *bn)
{
    return asn1_string_to_bn(ai, bn, V_ASN1_ENUMERATED);
}

/* Internal functions used by x_int64.c */
int c2i_uint64_int(uint64_t *ret, int *neg, const unsigned char **pp, long len)
{
    unsigned char buf[sizeof(uint64_t)];
    size_t buflen;

    buflen = c2i_ibuf(NULL, NULL, *pp, len);
    if (buflen == 0)
        return 0;
    if (buflen > sizeof(uint64_t)) {
        ASN1err(ASN1_F_C2I_UINT64_INT, ASN1_R_TOO_LARGE);
        return 0;
    }
    (void)c2i_ibuf(buf, neg, *pp, len);
    return asn1_get_uint64(ret, buf, buflen);
}

int i2c_uint64_int(unsigned char *p, uint64_t r, int neg)
{
    unsigned char buf[sizeof(uint64_t)];
    size_t off;

    off = asn1_put_uint64(buf, r);
    return i2c_ibuf(buf + off, sizeof(buf) - off, neg, &p);
}

