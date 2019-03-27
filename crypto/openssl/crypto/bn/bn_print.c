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
#include <limits.h>
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include "bn_lcl.h"

static const char Hex[] = "0123456789ABCDEF";

/* Must 'OPENSSL_free' the returned data */
char *BN_bn2hex(const BIGNUM *a)
{
    int i, j, v, z = 0;
    char *buf;
    char *p;

    if (BN_is_zero(a))
        return OPENSSL_strdup("0");
    buf = OPENSSL_malloc(a->top * BN_BYTES * 2 + 2);
    if (buf == NULL) {
        BNerr(BN_F_BN_BN2HEX, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    p = buf;
    if (a->neg)
        *p++ = '-';
    for (i = a->top - 1; i >= 0; i--) {
        for (j = BN_BITS2 - 8; j >= 0; j -= 8) {
            /* strip leading zeros */
            v = (int)((a->d[i] >> j) & 0xff);
            if (z || v != 0) {
                *p++ = Hex[v >> 4];
                *p++ = Hex[v & 0x0f];
                z = 1;
            }
        }
    }
    *p = '\0';
 err:
    return buf;
}

/* Must 'OPENSSL_free' the returned data */
char *BN_bn2dec(const BIGNUM *a)
{
    int i = 0, num, ok = 0, n, tbytes;
    char *buf = NULL;
    char *p;
    BIGNUM *t = NULL;
    BN_ULONG *bn_data = NULL, *lp;
    int bn_data_num;

    /*-
     * get an upper bound for the length of the decimal integer
     * num <= (BN_num_bits(a) + 1) * log(2)
     *     <= 3 * BN_num_bits(a) * 0.101 + log(2) + 1     (rounding error)
     *     <= 3 * BN_num_bits(a) / 10 + 3 * BN_num_bits / 1000 + 1 + 1
     */
    i = BN_num_bits(a) * 3;
    num = (i / 10 + i / 1000 + 1) + 1;
    tbytes = num + 3;   /* negative and terminator and one spare? */
    bn_data_num = num / BN_DEC_NUM + 1;
    bn_data = OPENSSL_malloc(bn_data_num * sizeof(BN_ULONG));
    buf = OPENSSL_malloc(tbytes);
    if (buf == NULL || bn_data == NULL) {
        BNerr(BN_F_BN_BN2DEC, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if ((t = BN_dup(a)) == NULL)
        goto err;

    p = buf;
    lp = bn_data;
    if (BN_is_zero(t)) {
        *p++ = '0';
        *p++ = '\0';
    } else {
        if (BN_is_negative(t))
            *p++ = '-';

        while (!BN_is_zero(t)) {
            if (lp - bn_data >= bn_data_num)
                goto err;
            *lp = BN_div_word(t, BN_DEC_CONV);
            if (*lp == (BN_ULONG)-1)
                goto err;
            lp++;
        }
        lp--;
        /*
         * We now have a series of blocks, BN_DEC_NUM chars in length, where
         * the last one needs truncation. The blocks need to be reversed in
         * order.
         */
        n = BIO_snprintf(p, tbytes - (size_t)(p - buf), BN_DEC_FMT1, *lp);
        if (n < 0)
            goto err;
        p += n;
        while (lp != bn_data) {
            lp--;
            n = BIO_snprintf(p, tbytes - (size_t)(p - buf), BN_DEC_FMT2, *lp);
            if (n < 0)
                goto err;
            p += n;
        }
    }
    ok = 1;
 err:
    OPENSSL_free(bn_data);
    BN_free(t);
    if (ok)
        return buf;
    OPENSSL_free(buf);
    return NULL;
}

int BN_hex2bn(BIGNUM **bn, const char *a)
{
    BIGNUM *ret = NULL;
    BN_ULONG l = 0;
    int neg = 0, h, m, i, j, k, c;
    int num;

    if (a == NULL || *a == '\0')
        return 0;

    if (*a == '-') {
        neg = 1;
        a++;
    }

    for (i = 0; i <= INT_MAX / 4 && ossl_isxdigit(a[i]); i++)
        continue;

    if (i == 0 || i > INT_MAX / 4)
        goto err;

    num = i + neg;
    if (bn == NULL)
        return num;

    /* a is the start of the hex digits, and it is 'i' long */
    if (*bn == NULL) {
        if ((ret = BN_new()) == NULL)
            return 0;
    } else {
        ret = *bn;
        BN_zero(ret);
    }

    /* i is the number of hex digits */
    if (bn_expand(ret, i * 4) == NULL)
        goto err;

    j = i;                      /* least significant 'hex' */
    m = 0;
    h = 0;
    while (j > 0) {
        m = (BN_BYTES * 2 <= j) ? BN_BYTES * 2 : j;
        l = 0;
        for (;;) {
            c = a[j - m];
            k = OPENSSL_hexchar2int(c);
            if (k < 0)
                k = 0;          /* paranoia */
            l = (l << 4) | k;

            if (--m <= 0) {
                ret->d[h++] = l;
                break;
            }
        }
        j -= BN_BYTES * 2;
    }
    ret->top = h;
    bn_correct_top(ret);

    *bn = ret;
    bn_check_top(ret);
    /* Don't set the negative flag if it's zero. */
    if (ret->top != 0)
        ret->neg = neg;
    return num;
 err:
    if (*bn == NULL)
        BN_free(ret);
    return 0;
}

int BN_dec2bn(BIGNUM **bn, const char *a)
{
    BIGNUM *ret = NULL;
    BN_ULONG l = 0;
    int neg = 0, i, j;
    int num;

    if (a == NULL || *a == '\0')
        return 0;
    if (*a == '-') {
        neg = 1;
        a++;
    }

    for (i = 0; i <= INT_MAX / 4 && ossl_isdigit(a[i]); i++)
        continue;

    if (i == 0 || i > INT_MAX / 4)
        goto err;

    num = i + neg;
    if (bn == NULL)
        return num;

    /*
     * a is the start of the digits, and it is 'i' long. We chop it into
     * BN_DEC_NUM digits at a time
     */
    if (*bn == NULL) {
        if ((ret = BN_new()) == NULL)
            return 0;
    } else {
        ret = *bn;
        BN_zero(ret);
    }

    /* i is the number of digits, a bit of an over expand */
    if (bn_expand(ret, i * 4) == NULL)
        goto err;

    j = BN_DEC_NUM - i % BN_DEC_NUM;
    if (j == BN_DEC_NUM)
        j = 0;
    l = 0;
    while (--i >= 0) {
        l *= 10;
        l += *a - '0';
        a++;
        if (++j == BN_DEC_NUM) {
            if (!BN_mul_word(ret, BN_DEC_CONV)
                || !BN_add_word(ret, l))
                goto err;
            l = 0;
            j = 0;
        }
    }

    bn_correct_top(ret);
    *bn = ret;
    bn_check_top(ret);
    /* Don't set the negative flag if it's zero. */
    if (ret->top != 0)
        ret->neg = neg;
    return num;
 err:
    if (*bn == NULL)
        BN_free(ret);
    return 0;
}

int BN_asc2bn(BIGNUM **bn, const char *a)
{
    const char *p = a;

    if (*p == '-')
        p++;

    if (p[0] == '0' && (p[1] == 'X' || p[1] == 'x')) {
        if (!BN_hex2bn(bn, p + 2))
            return 0;
    } else {
        if (!BN_dec2bn(bn, p))
            return 0;
    }
    /* Don't set the negative flag if it's zero. */
    if (*a == '-' && (*bn)->top != 0)
        (*bn)->neg = 1;
    return 1;
}

# ifndef OPENSSL_NO_STDIO
int BN_print_fp(FILE *fp, const BIGNUM *a)
{
    BIO *b;
    int ret;

    if ((b = BIO_new(BIO_s_file())) == NULL)
        return 0;
    BIO_set_fp(b, fp, BIO_NOCLOSE);
    ret = BN_print(b, a);
    BIO_free(b);
    return ret;
}
# endif

int BN_print(BIO *bp, const BIGNUM *a)
{
    int i, j, v, z = 0;
    int ret = 0;

    if ((a->neg) && BIO_write(bp, "-", 1) != 1)
        goto end;
    if (BN_is_zero(a) && BIO_write(bp, "0", 1) != 1)
        goto end;
    for (i = a->top - 1; i >= 0; i--) {
        for (j = BN_BITS2 - 4; j >= 0; j -= 4) {
            /* strip leading zeros */
            v = (int)((a->d[i] >> j) & 0x0f);
            if (z || v != 0) {
                if (BIO_write(bp, &Hex[v], 1) != 1)
                    goto end;
                z = 1;
            }
        }
    }
    ret = 1;
 end:
    return ret;
}

char *BN_options(void)
{
    static int init = 0;
    static char data[16];

    if (!init) {
        init++;
#ifdef BN_LLONG
        BIO_snprintf(data, sizeof(data), "bn(%zu,%zu)",
                     sizeof(BN_ULLONG) * 8, sizeof(BN_ULONG) * 8);
#else
        BIO_snprintf(data, sizeof(data), "bn(%zu,%zu)",
                     sizeof(BN_ULONG) * 8, sizeof(BN_ULONG) * 8);
#endif
    }
    return data;
}
