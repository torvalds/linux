/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <assert.h>
#include <openssl/bn.h>
#include "internal/cryptlib.h"
#include "bn_lcl.h"

/* The old slow way */
#if 0
int BN_div(BIGNUM *dv, BIGNUM *rem, const BIGNUM *m, const BIGNUM *d,
           BN_CTX *ctx)
{
    int i, nm, nd;
    int ret = 0;
    BIGNUM *D;

    bn_check_top(m);
    bn_check_top(d);
    if (BN_is_zero(d)) {
        BNerr(BN_F_BN_DIV, BN_R_DIV_BY_ZERO);
        return 0;
    }

    if (BN_ucmp(m, d) < 0) {
        if (rem != NULL) {
            if (BN_copy(rem, m) == NULL)
                return 0;
        }
        if (dv != NULL)
            BN_zero(dv);
        return 1;
    }

    BN_CTX_start(ctx);
    D = BN_CTX_get(ctx);
    if (dv == NULL)
        dv = BN_CTX_get(ctx);
    if (rem == NULL)
        rem = BN_CTX_get(ctx);
    if (D == NULL || dv == NULL || rem == NULL)
        goto end;

    nd = BN_num_bits(d);
    nm = BN_num_bits(m);
    if (BN_copy(D, d) == NULL)
        goto end;
    if (BN_copy(rem, m) == NULL)
        goto end;

    /*
     * The next 2 are needed so we can do a dv->d[0]|=1 later since
     * BN_lshift1 will only work once there is a value :-)
     */
    BN_zero(dv);
    if (bn_wexpand(dv, 1) == NULL)
        goto end;
    dv->top = 1;

    if (!BN_lshift(D, D, nm - nd))
        goto end;
    for (i = nm - nd; i >= 0; i--) {
        if (!BN_lshift1(dv, dv))
            goto end;
        if (BN_ucmp(rem, D) >= 0) {
            dv->d[0] |= 1;
            if (!BN_usub(rem, rem, D))
                goto end;
        }
/* CAN IMPROVE (and have now :=) */
        if (!BN_rshift1(D, D))
            goto end;
    }
    rem->neg = BN_is_zero(rem) ? 0 : m->neg;
    dv->neg = m->neg ^ d->neg;
    ret = 1;
 end:
    BN_CTX_end(ctx);
    return ret;
}

#else

# if defined(BN_DIV3W)
BN_ULONG bn_div_3_words(const BN_ULONG *m, BN_ULONG d1, BN_ULONG d0);
# elif 0
/*
 * This is #if-ed away, because it's a reference for assembly implementations,
 * where it can and should be made constant-time. But if you want to test it,
 * just replace 0 with 1.
 */
#  if BN_BITS2 == 64 && defined(__SIZEOF_INT128__) && __SIZEOF_INT128__==16
#   undef BN_ULLONG
#   define BN_ULLONG __uint128_t
#   define BN_LLONG
#  endif

#  ifdef BN_LLONG
#   define BN_DIV3W
/*
 * Interface is somewhat quirky, |m| is pointer to most significant limb,
 * and less significant limb is referred at |m[-1]|. This means that caller
 * is responsible for ensuring that |m[-1]| is valid. Second condition that
 * has to be met is that |d0|'s most significant bit has to be set. Or in
 * other words divisor has to be "bit-aligned to the left." bn_div_fixed_top
 * does all this. The subroutine considers four limbs, two of which are
 * "overlapping," hence the name...
 */
static BN_ULONG bn_div_3_words(const BN_ULONG *m, BN_ULONG d1, BN_ULONG d0)
{
    BN_ULLONG R = ((BN_ULLONG)m[0] << BN_BITS2) | m[-1];
    BN_ULLONG D = ((BN_ULLONG)d0 << BN_BITS2) | d1;
    BN_ULONG Q = 0, mask;
    int i;

    for (i = 0; i < BN_BITS2; i++) {
        Q <<= 1;
        if (R >= D) {
            Q |= 1;
            R -= D;
        }
        D >>= 1;
    }

    mask = 0 - (Q >> (BN_BITS2 - 1));   /* does it overflow? */

    Q <<= 1;
    Q |= (R >= D);

    return (Q | mask) & BN_MASK2;
}
#  endif
# endif

static int bn_left_align(BIGNUM *num)
{
    BN_ULONG *d = num->d, n, m, rmask;
    int top = num->top;
    int rshift = BN_num_bits_word(d[top - 1]), lshift, i;

    lshift = BN_BITS2 - rshift;
    rshift %= BN_BITS2;            /* say no to undefined behaviour */
    rmask = (BN_ULONG)0 - rshift;  /* rmask = 0 - (rshift != 0) */
    rmask |= rmask >> 8;

    for (i = 0, m = 0; i < top; i++) {
        n = d[i];
        d[i] = ((n << lshift) | m) & BN_MASK2;
        m = (n >> rshift) & rmask;
    }

    return lshift;
}

# if !defined(OPENSSL_NO_ASM) && !defined(OPENSSL_NO_INLINE_ASM) \
    && !defined(PEDANTIC) && !defined(BN_DIV3W)
#  if defined(__GNUC__) && __GNUC__>=2
#   if defined(__i386) || defined (__i386__)
   /*-
    * There were two reasons for implementing this template:
    * - GNU C generates a call to a function (__udivdi3 to be exact)
    *   in reply to ((((BN_ULLONG)n0)<<BN_BITS2)|n1)/d0 (I fail to
    *   understand why...);
    * - divl doesn't only calculate quotient, but also leaves
    *   remainder in %edx which we can definitely use here:-)
    */
#    undef bn_div_words
#    define bn_div_words(n0,n1,d0)                \
        ({  asm volatile (                      \
                "divl   %4"                     \
                : "=a"(q), "=d"(rem)            \
                : "a"(n1), "d"(n0), "r"(d0)     \
                : "cc");                        \
            q;                                  \
        })
#    define REMAINDER_IS_ALREADY_CALCULATED
#   elif defined(__x86_64) && defined(SIXTY_FOUR_BIT_LONG)
   /*
    * Same story here, but it's 128-bit by 64-bit division. Wow!
    */
#    undef bn_div_words
#    define bn_div_words(n0,n1,d0)                \
        ({  asm volatile (                      \
                "divq   %4"                     \
                : "=a"(q), "=d"(rem)            \
                : "a"(n1), "d"(n0), "r"(d0)     \
                : "cc");                        \
            q;                                  \
        })
#    define REMAINDER_IS_ALREADY_CALCULATED
#   endif                       /* __<cpu> */
#  endif                        /* __GNUC__ */
# endif                         /* OPENSSL_NO_ASM */

/*-
 * BN_div computes  dv := num / divisor, rounding towards
 * zero, and sets up rm  such that  dv*divisor + rm = num  holds.
 * Thus:
 *     dv->neg == num->neg ^ divisor->neg  (unless the result is zero)
 *     rm->neg == num->neg                 (unless the remainder is zero)
 * If 'dv' or 'rm' is NULL, the respective value is not returned.
 */
int BN_div(BIGNUM *dv, BIGNUM *rm, const BIGNUM *num, const BIGNUM *divisor,
           BN_CTX *ctx)
{
    int ret;

    if (BN_is_zero(divisor)) {
        BNerr(BN_F_BN_DIV, BN_R_DIV_BY_ZERO);
        return 0;
    }

    /*
     * Invalid zero-padding would have particularly bad consequences so don't
     * just rely on bn_check_top() here (bn_check_top() works only for
     * BN_DEBUG builds)
     */
    if (divisor->d[divisor->top - 1] == 0) {
        BNerr(BN_F_BN_DIV, BN_R_NOT_INITIALIZED);
        return 0;
    }

    ret = bn_div_fixed_top(dv, rm, num, divisor, ctx);

    if (ret) {
        if (dv != NULL)
            bn_correct_top(dv);
        if (rm != NULL)
            bn_correct_top(rm);
    }

    return ret;
}

/*
 * It's argued that *length* of *significant* part of divisor is public.
 * Even if it's private modulus that is. Again, *length* is assumed
 * public, but not *value*. Former is likely to be pre-defined by
 * algorithm with bit granularity, though below subroutine is invariant
 * of limb length. Thanks to this assumption we can require that |divisor|
 * may not be zero-padded, yet claim this subroutine "constant-time"(*).
 * This is because zero-padded dividend, |num|, is tolerated, so that
 * caller can pass dividend of public length(*), but with smaller amount
 * of significant limbs. This naturally means that quotient, |dv|, would
 * contain correspongly less significant limbs as well, and will be zero-
 * padded accordingly. Returned remainder, |rm|, will have same bit length
 * as divisor, also zero-padded if needed. These actually leave sign bits
 * in ambiguous state. In sense that we try to avoid negative zeros, while
 * zero-padded zeros would retain sign.
 *
 * (*) "Constant-time-ness" has two pre-conditions:
 *
 *     - availability of constant-time bn_div_3_words;
 *     - dividend is at least as "wide" as divisor, limb-wise, zero-padded
 *       if so requied, which shouldn't be a privacy problem, because
 *       divisor's length is considered public;
 */
int bn_div_fixed_top(BIGNUM *dv, BIGNUM *rm, const BIGNUM *num,
                     const BIGNUM *divisor, BN_CTX *ctx)
{
    int norm_shift, i, j, loop;
    BIGNUM *tmp, *snum, *sdiv, *res;
    BN_ULONG *resp, *wnum, *wnumtop;
    BN_ULONG d0, d1;
    int num_n, div_n;

    assert(divisor->top > 0 && divisor->d[divisor->top - 1] != 0);

    bn_check_top(num);
    bn_check_top(divisor);
    bn_check_top(dv);
    bn_check_top(rm);

    BN_CTX_start(ctx);
    res = (dv == NULL) ? BN_CTX_get(ctx) : dv;
    tmp = BN_CTX_get(ctx);
    snum = BN_CTX_get(ctx);
    sdiv = BN_CTX_get(ctx);
    if (sdiv == NULL)
        goto err;

    /* First we normalise the numbers */
    if (!BN_copy(sdiv, divisor))
        goto err;
    norm_shift = bn_left_align(sdiv);
    sdiv->neg = 0;
    /*
     * Note that bn_lshift_fixed_top's output is always one limb longer
     * than input, even when norm_shift is zero. This means that amount of
     * inner loop iterations is invariant of dividend value, and that one
     * doesn't need to compare dividend and divisor if they were originally
     * of the same bit length.
     */
    if (!(bn_lshift_fixed_top(snum, num, norm_shift)))
        goto err;

    div_n = sdiv->top;
    num_n = snum->top;

    if (num_n <= div_n) {
        /* caller didn't pad dividend -> no constant-time guarantee... */
        if (bn_wexpand(snum, div_n + 1) == NULL)
            goto err;
        memset(&(snum->d[num_n]), 0, (div_n - num_n + 1) * sizeof(BN_ULONG));
        snum->top = num_n = div_n + 1;
    }

    loop = num_n - div_n;
    /*
     * Lets setup a 'window' into snum This is the part that corresponds to
     * the current 'area' being divided
     */
    wnum = &(snum->d[loop]);
    wnumtop = &(snum->d[num_n - 1]);

    /* Get the top 2 words of sdiv */
    d0 = sdiv->d[div_n - 1];
    d1 = (div_n == 1) ? 0 : sdiv->d[div_n - 2];

    /* Setup quotient */
    if (!bn_wexpand(res, loop))
        goto err;
    res->neg = (num->neg ^ divisor->neg);
    res->top = loop;
    res->flags |= BN_FLG_FIXED_TOP;
    resp = &(res->d[loop]);

    /* space for temp */
    if (!bn_wexpand(tmp, (div_n + 1)))
        goto err;

    for (i = 0; i < loop; i++, wnumtop--) {
        BN_ULONG q, l0;
        /*
         * the first part of the loop uses the top two words of snum and sdiv
         * to calculate a BN_ULONG q such that | wnum - sdiv * q | < sdiv
         */
# if defined(BN_DIV3W)
        q = bn_div_3_words(wnumtop, d1, d0);
# else
        BN_ULONG n0, n1, rem = 0;

        n0 = wnumtop[0];
        n1 = wnumtop[-1];
        if (n0 == d0)
            q = BN_MASK2;
        else {                  /* n0 < d0 */
            BN_ULONG n2 = (wnumtop == wnum) ? 0 : wnumtop[-2];
#  ifdef BN_LLONG
            BN_ULLONG t2;

#   if defined(BN_LLONG) && defined(BN_DIV2W) && !defined(bn_div_words)
            q = (BN_ULONG)(((((BN_ULLONG) n0) << BN_BITS2) | n1) / d0);
#   else
            q = bn_div_words(n0, n1, d0);
#   endif

#   ifndef REMAINDER_IS_ALREADY_CALCULATED
            /*
             * rem doesn't have to be BN_ULLONG. The least we
             * know it's less that d0, isn't it?
             */
            rem = (n1 - q * d0) & BN_MASK2;
#   endif
            t2 = (BN_ULLONG) d1 *q;

            for (;;) {
                if (t2 <= ((((BN_ULLONG) rem) << BN_BITS2) | n2))
                    break;
                q--;
                rem += d0;
                if (rem < d0)
                    break;      /* don't let rem overflow */
                t2 -= d1;
            }
#  else                         /* !BN_LLONG */
            BN_ULONG t2l, t2h;

            q = bn_div_words(n0, n1, d0);
#   ifndef REMAINDER_IS_ALREADY_CALCULATED
            rem = (n1 - q * d0) & BN_MASK2;
#   endif

#   if defined(BN_UMULT_LOHI)
            BN_UMULT_LOHI(t2l, t2h, d1, q);
#   elif defined(BN_UMULT_HIGH)
            t2l = d1 * q;
            t2h = BN_UMULT_HIGH(d1, q);
#   else
            {
                BN_ULONG ql, qh;
                t2l = LBITS(d1);
                t2h = HBITS(d1);
                ql = LBITS(q);
                qh = HBITS(q);
                mul64(t2l, t2h, ql, qh); /* t2=(BN_ULLONG)d1*q; */
            }
#   endif

            for (;;) {
                if ((t2h < rem) || ((t2h == rem) && (t2l <= n2)))
                    break;
                q--;
                rem += d0;
                if (rem < d0)
                    break;      /* don't let rem overflow */
                if (t2l < d1)
                    t2h--;
                t2l -= d1;
            }
#  endif                        /* !BN_LLONG */
        }
# endif                         /* !BN_DIV3W */

        l0 = bn_mul_words(tmp->d, sdiv->d, div_n, q);
        tmp->d[div_n] = l0;
        wnum--;
        /*
         * ignore top values of the bignums just sub the two BN_ULONG arrays
         * with bn_sub_words
         */
        l0 = bn_sub_words(wnum, wnum, tmp->d, div_n + 1);
        q -= l0;
        /*
         * Note: As we have considered only the leading two BN_ULONGs in
         * the calculation of q, sdiv * q might be greater than wnum (but
         * then (q-1) * sdiv is less or equal than wnum)
         */
        for (l0 = 0 - l0, j = 0; j < div_n; j++)
            tmp->d[j] = sdiv->d[j] & l0;
        l0 = bn_add_words(wnum, wnum, tmp->d, div_n);
        (*wnumtop) += l0;
        assert((*wnumtop) == 0);

        /* store part of the result */
        *--resp = q;
    }
    /* snum holds remainder, it's as wide as divisor */
    snum->neg = num->neg;
    snum->top = div_n;
    snum->flags |= BN_FLG_FIXED_TOP;
    if (rm != NULL)
        bn_rshift_fixed_top(rm, snum, norm_shift);
    BN_CTX_end(ctx);
    return 1;
 err:
    bn_check_top(rm);
    BN_CTX_end(ctx);
    return 0;
}
#endif
