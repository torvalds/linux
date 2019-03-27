/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include "bn_lcl.h"

/* r must not be a */
/*
 * I've just gone over this and it is now %20 faster on x86 - eay - 27 Jun 96
 */
int BN_sqr(BIGNUM *r, const BIGNUM *a, BN_CTX *ctx)
{
    int ret = bn_sqr_fixed_top(r, a, ctx);

    bn_correct_top(r);
    bn_check_top(r);

    return ret;
}

int bn_sqr_fixed_top(BIGNUM *r, const BIGNUM *a, BN_CTX *ctx)
{
    int max, al;
    int ret = 0;
    BIGNUM *tmp, *rr;

    bn_check_top(a);

    al = a->top;
    if (al <= 0) {
        r->top = 0;
        r->neg = 0;
        return 1;
    }

    BN_CTX_start(ctx);
    rr = (a != r) ? r : BN_CTX_get(ctx);
    tmp = BN_CTX_get(ctx);
    if (rr == NULL || tmp == NULL)
        goto err;

    max = 2 * al;               /* Non-zero (from above) */
    if (bn_wexpand(rr, max) == NULL)
        goto err;

    if (al == 4) {
#ifndef BN_SQR_COMBA
        BN_ULONG t[8];
        bn_sqr_normal(rr->d, a->d, 4, t);
#else
        bn_sqr_comba4(rr->d, a->d);
#endif
    } else if (al == 8) {
#ifndef BN_SQR_COMBA
        BN_ULONG t[16];
        bn_sqr_normal(rr->d, a->d, 8, t);
#else
        bn_sqr_comba8(rr->d, a->d);
#endif
    } else {
#if defined(BN_RECURSION)
        if (al < BN_SQR_RECURSIVE_SIZE_NORMAL) {
            BN_ULONG t[BN_SQR_RECURSIVE_SIZE_NORMAL * 2];
            bn_sqr_normal(rr->d, a->d, al, t);
        } else {
            int j, k;

            j = BN_num_bits_word((BN_ULONG)al);
            j = 1 << (j - 1);
            k = j + j;
            if (al == j) {
                if (bn_wexpand(tmp, k * 2) == NULL)
                    goto err;
                bn_sqr_recursive(rr->d, a->d, al, tmp->d);
            } else {
                if (bn_wexpand(tmp, max) == NULL)
                    goto err;
                bn_sqr_normal(rr->d, a->d, al, tmp->d);
            }
        }
#else
        if (bn_wexpand(tmp, max) == NULL)
            goto err;
        bn_sqr_normal(rr->d, a->d, al, tmp->d);
#endif
    }

    rr->neg = 0;
    rr->top = max;
    rr->flags |= BN_FLG_FIXED_TOP;
    if (r != rr && BN_copy(r, rr) == NULL)
        goto err;

    ret = 1;
 err:
    bn_check_top(rr);
    bn_check_top(tmp);
    BN_CTX_end(ctx);
    return ret;
}

/* tmp must have 2*n words */
void bn_sqr_normal(BN_ULONG *r, const BN_ULONG *a, int n, BN_ULONG *tmp)
{
    int i, j, max;
    const BN_ULONG *ap;
    BN_ULONG *rp;

    max = n * 2;
    ap = a;
    rp = r;
    rp[0] = rp[max - 1] = 0;
    rp++;
    j = n;

    if (--j > 0) {
        ap++;
        rp[j] = bn_mul_words(rp, ap, j, ap[-1]);
        rp += 2;
    }

    for (i = n - 2; i > 0; i--) {
        j--;
        ap++;
        rp[j] = bn_mul_add_words(rp, ap, j, ap[-1]);
        rp += 2;
    }

    bn_add_words(r, r, r, max);

    /* There will not be a carry */

    bn_sqr_words(tmp, a, n);

    bn_add_words(r, r, tmp, max);
}

#ifdef BN_RECURSION
/*-
 * r is 2*n words in size,
 * a and b are both n words in size.    (There's not actually a 'b' here ...)
 * n must be a power of 2.
 * We multiply and return the result.
 * t must be 2*n words in size
 * We calculate
 * a[0]*b[0]
 * a[0]*b[0]+a[1]*b[1]+(a[0]-a[1])*(b[1]-b[0])
 * a[1]*b[1]
 */
void bn_sqr_recursive(BN_ULONG *r, const BN_ULONG *a, int n2, BN_ULONG *t)
{
    int n = n2 / 2;
    int zero, c1;
    BN_ULONG ln, lo, *p;

    if (n2 == 4) {
# ifndef BN_SQR_COMBA
        bn_sqr_normal(r, a, 4, t);
# else
        bn_sqr_comba4(r, a);
# endif
        return;
    } else if (n2 == 8) {
# ifndef BN_SQR_COMBA
        bn_sqr_normal(r, a, 8, t);
# else
        bn_sqr_comba8(r, a);
# endif
        return;
    }
    if (n2 < BN_SQR_RECURSIVE_SIZE_NORMAL) {
        bn_sqr_normal(r, a, n2, t);
        return;
    }
    /* r=(a[0]-a[1])*(a[1]-a[0]) */
    c1 = bn_cmp_words(a, &(a[n]), n);
    zero = 0;
    if (c1 > 0)
        bn_sub_words(t, a, &(a[n]), n);
    else if (c1 < 0)
        bn_sub_words(t, &(a[n]), a, n);
    else
        zero = 1;

    /* The result will always be negative unless it is zero */
    p = &(t[n2 * 2]);

    if (!zero)
        bn_sqr_recursive(&(t[n2]), t, n, p);
    else
        memset(&t[n2], 0, sizeof(*t) * n2);
    bn_sqr_recursive(r, a, n, p);
    bn_sqr_recursive(&(r[n2]), &(a[n]), n, p);

    /*-
     * t[32] holds (a[0]-a[1])*(a[1]-a[0]), it is negative or zero
     * r[10] holds (a[0]*b[0])
     * r[32] holds (b[1]*b[1])
     */

    c1 = (int)(bn_add_words(t, r, &(r[n2]), n2));

    /* t[32] is negative */
    c1 -= (int)(bn_sub_words(&(t[n2]), t, &(t[n2]), n2));

    /*-
     * t[32] holds (a[0]-a[1])*(a[1]-a[0])+(a[0]*a[0])+(a[1]*a[1])
     * r[10] holds (a[0]*a[0])
     * r[32] holds (a[1]*a[1])
     * c1 holds the carry bits
     */
    c1 += (int)(bn_add_words(&(r[n]), &(r[n]), &(t[n2]), n2));
    if (c1) {
        p = &(r[n + n2]);
        lo = *p;
        ln = (lo + c1) & BN_MASK2;
        *p = ln;

        /*
         * The overflow will stop before we over write words we should not
         * overwrite
         */
        if (ln < (BN_ULONG)c1) {
            do {
                p++;
                lo = *p;
                ln = (lo + 1) & BN_MASK2;
                *p = ln;
            } while (ln == 0);
        }
    }
}
#endif
