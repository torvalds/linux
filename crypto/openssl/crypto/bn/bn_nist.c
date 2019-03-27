/*
 * Copyright 2002-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "bn_lcl.h"
#include "internal/cryptlib.h"

#define BN_NIST_192_TOP (192+BN_BITS2-1)/BN_BITS2
#define BN_NIST_224_TOP (224+BN_BITS2-1)/BN_BITS2
#define BN_NIST_256_TOP (256+BN_BITS2-1)/BN_BITS2
#define BN_NIST_384_TOP (384+BN_BITS2-1)/BN_BITS2
#define BN_NIST_521_TOP (521+BN_BITS2-1)/BN_BITS2

/* pre-computed tables are "carry-less" values of modulus*(i+1) */
#if BN_BITS2 == 64
static const BN_ULONG _nist_p_192[][BN_NIST_192_TOP] = {
    {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL},
    {0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFDULL, 0xFFFFFFFFFFFFFFFFULL},
    {0xFFFFFFFFFFFFFFFDULL, 0xFFFFFFFFFFFFFFFCULL, 0xFFFFFFFFFFFFFFFFULL}
};

static const BN_ULONG _nist_p_192_sqr[] = {
    0x0000000000000001ULL, 0x0000000000000002ULL, 0x0000000000000001ULL,
    0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFDULL, 0xFFFFFFFFFFFFFFFFULL
};

static const BN_ULONG _nist_p_224[][BN_NIST_224_TOP] = {
    {0x0000000000000001ULL, 0xFFFFFFFF00000000ULL,
     0xFFFFFFFFFFFFFFFFULL, 0x00000000FFFFFFFFULL},
    {0x0000000000000002ULL, 0xFFFFFFFE00000000ULL,
     0xFFFFFFFFFFFFFFFFULL, 0x00000001FFFFFFFFULL} /* this one is
                                                    * "carry-full" */
};

static const BN_ULONG _nist_p_224_sqr[] = {
    0x0000000000000001ULL, 0xFFFFFFFE00000000ULL,
    0xFFFFFFFFFFFFFFFFULL, 0x0000000200000000ULL,
    0x0000000000000000ULL, 0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL
};

static const BN_ULONG _nist_p_256[][BN_NIST_256_TOP] = {
    {0xFFFFFFFFFFFFFFFFULL, 0x00000000FFFFFFFFULL,
     0x0000000000000000ULL, 0xFFFFFFFF00000001ULL},
    {0xFFFFFFFFFFFFFFFEULL, 0x00000001FFFFFFFFULL,
     0x0000000000000000ULL, 0xFFFFFFFE00000002ULL},
    {0xFFFFFFFFFFFFFFFDULL, 0x00000002FFFFFFFFULL,
     0x0000000000000000ULL, 0xFFFFFFFD00000003ULL},
    {0xFFFFFFFFFFFFFFFCULL, 0x00000003FFFFFFFFULL,
     0x0000000000000000ULL, 0xFFFFFFFC00000004ULL},
    {0xFFFFFFFFFFFFFFFBULL, 0x00000004FFFFFFFFULL,
     0x0000000000000000ULL, 0xFFFFFFFB00000005ULL},
};

static const BN_ULONG _nist_p_256_sqr[] = {
    0x0000000000000001ULL, 0xFFFFFFFE00000000ULL,
    0xFFFFFFFFFFFFFFFFULL, 0x00000001FFFFFFFEULL,
    0x00000001FFFFFFFEULL, 0x00000001FFFFFFFEULL,
    0xFFFFFFFE00000001ULL, 0xFFFFFFFE00000002ULL
};

static const BN_ULONG _nist_p_384[][BN_NIST_384_TOP] = {
    {0x00000000FFFFFFFFULL, 0xFFFFFFFF00000000ULL, 0xFFFFFFFFFFFFFFFEULL,
     0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
    {0x00000001FFFFFFFEULL, 0xFFFFFFFE00000000ULL, 0xFFFFFFFFFFFFFFFDULL,
     0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
    {0x00000002FFFFFFFDULL, 0xFFFFFFFD00000000ULL, 0xFFFFFFFFFFFFFFFCULL,
     0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
    {0x00000003FFFFFFFCULL, 0xFFFFFFFC00000000ULL, 0xFFFFFFFFFFFFFFFBULL,
     0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
    {0x00000004FFFFFFFBULL, 0xFFFFFFFB00000000ULL, 0xFFFFFFFFFFFFFFFAULL,
     0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
};

static const BN_ULONG _nist_p_384_sqr[] = {
    0xFFFFFFFE00000001ULL, 0x0000000200000000ULL, 0xFFFFFFFE00000000ULL,
    0x0000000200000000ULL, 0x0000000000000001ULL, 0x0000000000000000ULL,
    0x00000001FFFFFFFEULL, 0xFFFFFFFE00000000ULL, 0xFFFFFFFFFFFFFFFDULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
};

static const BN_ULONG _nist_p_521[] =
    { 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
    0x00000000000001FFULL
};

static const BN_ULONG _nist_p_521_sqr[] = {
    0x0000000000000001ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0xFFFFFFFFFFFFFC00ULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0x000000000003FFFFULL
};
#elif BN_BITS2 == 32
static const BN_ULONG _nist_p_192[][BN_NIST_192_TOP] = {
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}
};

static const BN_ULONG _nist_p_192_sqr[] = {
    0x00000001, 0x00000000, 0x00000002, 0x00000000, 0x00000001, 0x00000000,
    0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

static const BN_ULONG _nist_p_224[][BN_NIST_224_TOP] = {
    {0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0x00000002, 0x00000000, 0x00000000, 0xFFFFFFFE,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}
};

static const BN_ULONG _nist_p_224_sqr[] = {
    0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFE,
    0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000002,
    0x00000000, 0x00000000, 0xFFFFFFFE, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF
};

static const BN_ULONG _nist_p_256[][BN_NIST_256_TOP] = {
    {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
     0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF},
    {0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000001,
     0x00000000, 0x00000000, 0x00000002, 0xFFFFFFFE},
    {0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000002,
     0x00000000, 0x00000000, 0x00000003, 0xFFFFFFFD},
    {0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000003,
     0x00000000, 0x00000000, 0x00000004, 0xFFFFFFFC},
    {0xFFFFFFFB, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000004,
     0x00000000, 0x00000000, 0x00000005, 0xFFFFFFFB},
};

static const BN_ULONG _nist_p_256_sqr[] = {
    0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFE,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0x00000001,
    0xFFFFFFFE, 0x00000001, 0xFFFFFFFE, 0x00000001,
    0x00000001, 0xFFFFFFFE, 0x00000002, 0xFFFFFFFE
};

static const BN_ULONG _nist_p_384[][BN_NIST_384_TOP] = {
    {0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFE, 0x00000001, 0x00000000, 0xFFFFFFFE, 0xFFFFFFFD, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFD, 0x00000002, 0x00000000, 0xFFFFFFFD, 0xFFFFFFFC, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFC, 0x00000003, 0x00000000, 0xFFFFFFFC, 0xFFFFFFFB, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
    {0xFFFFFFFB, 0x00000004, 0x00000000, 0xFFFFFFFB, 0xFFFFFFFA, 0xFFFFFFFF,
     0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
};

static const BN_ULONG _nist_p_384_sqr[] = {
    0x00000001, 0xFFFFFFFE, 0x00000000, 0x00000002, 0x00000000, 0xFFFFFFFE,
    0x00000000, 0x00000002, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0xFFFFFFFE, 0x00000001, 0x00000000, 0xFFFFFFFE, 0xFFFFFFFD, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};

static const BN_ULONG _nist_p_521[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0x000001FF
};

static const BN_ULONG _nist_p_521_sqr[] = {
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFFFFFC00, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0x0003FFFF
};
#else
# error "unsupported BN_BITS2"
#endif

static const BIGNUM _bignum_nist_p_192 = {
    (BN_ULONG *)_nist_p_192[0],
    BN_NIST_192_TOP,
    BN_NIST_192_TOP,
    0,
    BN_FLG_STATIC_DATA
};

static const BIGNUM _bignum_nist_p_224 = {
    (BN_ULONG *)_nist_p_224[0],
    BN_NIST_224_TOP,
    BN_NIST_224_TOP,
    0,
    BN_FLG_STATIC_DATA
};

static const BIGNUM _bignum_nist_p_256 = {
    (BN_ULONG *)_nist_p_256[0],
    BN_NIST_256_TOP,
    BN_NIST_256_TOP,
    0,
    BN_FLG_STATIC_DATA
};

static const BIGNUM _bignum_nist_p_384 = {
    (BN_ULONG *)_nist_p_384[0],
    BN_NIST_384_TOP,
    BN_NIST_384_TOP,
    0,
    BN_FLG_STATIC_DATA
};

static const BIGNUM _bignum_nist_p_521 = {
    (BN_ULONG *)_nist_p_521,
    BN_NIST_521_TOP,
    BN_NIST_521_TOP,
    0,
    BN_FLG_STATIC_DATA
};

const BIGNUM *BN_get0_nist_prime_192(void)
{
    return &_bignum_nist_p_192;
}

const BIGNUM *BN_get0_nist_prime_224(void)
{
    return &_bignum_nist_p_224;
}

const BIGNUM *BN_get0_nist_prime_256(void)
{
    return &_bignum_nist_p_256;
}

const BIGNUM *BN_get0_nist_prime_384(void)
{
    return &_bignum_nist_p_384;
}

const BIGNUM *BN_get0_nist_prime_521(void)
{
    return &_bignum_nist_p_521;
}

static void nist_cp_bn_0(BN_ULONG *dst, const BN_ULONG *src, int top, int max)
{
    int i;

#ifdef BN_DEBUG
    (void)ossl_assert(top <= max);
#endif
    for (i = 0; i < top; i++)
        dst[i] = src[i];
    for (; i < max; i++)
        dst[i] = 0;
}

static void nist_cp_bn(BN_ULONG *dst, const BN_ULONG *src, int top)
{
    int i;

    for (i = 0; i < top; i++)
        dst[i] = src[i];
}

#if BN_BITS2 == 64
# define bn_cp_64(to, n, from, m)        (to)[n] = (m>=0)?((from)[m]):0;
# define bn_64_set_0(to, n)              (to)[n] = (BN_ULONG)0;
/*
 * two following macros are implemented under assumption that they
 * are called in a sequence with *ascending* n, i.e. as they are...
 */
# define bn_cp_32_naked(to, n, from, m)  (((n)&1)?(to[(n)/2]|=((m)&1)?(from[(m)/2]&BN_MASK2h):(from[(m)/2]<<32))\
                                                :(to[(n)/2] =((m)&1)?(from[(m)/2]>>32):(from[(m)/2]&BN_MASK2l)))
# define bn_32_set_0(to, n)              (((n)&1)?(to[(n)/2]&=BN_MASK2l):(to[(n)/2]=0));
# define bn_cp_32(to,n,from,m)           ((m)>=0)?bn_cp_32_naked(to,n,from,m):bn_32_set_0(to,n)
# if defined(L_ENDIAN)
#  if defined(__arch64__)
#   define NIST_INT64 long
#  else
#   define NIST_INT64 long long
#  endif
# endif
#else
# define bn_cp_64(to, n, from, m) \
        { \
        bn_cp_32(to, (n)*2, from, (m)*2); \
        bn_cp_32(to, (n)*2+1, from, (m)*2+1); \
        }
# define bn_64_set_0(to, n) \
        { \
        bn_32_set_0(to, (n)*2); \
        bn_32_set_0(to, (n)*2+1); \
        }
# define bn_cp_32(to, n, from, m)        (to)[n] = (m>=0)?((from)[m]):0;
# define bn_32_set_0(to, n)              (to)[n] = (BN_ULONG)0;
# if defined(_WIN32) && !defined(__GNUC__)
#  define NIST_INT64 __int64
# elif defined(BN_LLONG)
#  define NIST_INT64 long long
# endif
#endif                          /* BN_BITS2 != 64 */

#define nist_set_192(to, from, a1, a2, a3) \
        { \
        bn_cp_64(to, 0, from, (a3) - 3) \
        bn_cp_64(to, 1, from, (a2) - 3) \
        bn_cp_64(to, 2, from, (a1) - 3) \
        }

int BN_nist_mod_192(BIGNUM *r, const BIGNUM *a, const BIGNUM *field,
                    BN_CTX *ctx)
{
    int top = a->top, i;
    int carry;
    register BN_ULONG *r_d, *a_d = a->d;
    union {
        BN_ULONG bn[BN_NIST_192_TOP];
        unsigned int ui[BN_NIST_192_TOP * sizeof(BN_ULONG) /
                        sizeof(unsigned int)];
    } buf;
    BN_ULONG c_d[BN_NIST_192_TOP], *res;
    PTR_SIZE_INT mask;
    static const BIGNUM _bignum_nist_p_192_sqr = {
        (BN_ULONG *)_nist_p_192_sqr,
        OSSL_NELEM(_nist_p_192_sqr),
        OSSL_NELEM(_nist_p_192_sqr),
        0, BN_FLG_STATIC_DATA
    };

    field = &_bignum_nist_p_192; /* just to make sure */

    if (BN_is_negative(a) || BN_ucmp(a, &_bignum_nist_p_192_sqr) >= 0)
        return BN_nnmod(r, a, field, ctx);

    i = BN_ucmp(field, a);
    if (i == 0) {
        BN_zero(r);
        return 1;
    } else if (i > 0)
        return (r == a) ? 1 : (BN_copy(r, a) != NULL);

    if (r != a) {
        if (!bn_wexpand(r, BN_NIST_192_TOP))
            return 0;
        r_d = r->d;
        nist_cp_bn(r_d, a_d, BN_NIST_192_TOP);
    } else
        r_d = a_d;

    nist_cp_bn_0(buf.bn, a_d + BN_NIST_192_TOP, top - BN_NIST_192_TOP,
                 BN_NIST_192_TOP);

#if defined(NIST_INT64)
    {
        NIST_INT64 acc;         /* accumulator */
        unsigned int *rp = (unsigned int *)r_d;
        const unsigned int *bp = (const unsigned int *)buf.ui;

        acc = rp[0];
        acc += bp[3 * 2 - 6];
        acc += bp[5 * 2 - 6];
        rp[0] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[1];
        acc += bp[3 * 2 - 5];
        acc += bp[5 * 2 - 5];
        rp[1] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[2];
        acc += bp[3 * 2 - 6];
        acc += bp[4 * 2 - 6];
        acc += bp[5 * 2 - 6];
        rp[2] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[3];
        acc += bp[3 * 2 - 5];
        acc += bp[4 * 2 - 5];
        acc += bp[5 * 2 - 5];
        rp[3] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[4];
        acc += bp[4 * 2 - 6];
        acc += bp[5 * 2 - 6];
        rp[4] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[5];
        acc += bp[4 * 2 - 5];
        acc += bp[5 * 2 - 5];
        rp[5] = (unsigned int)acc;

        carry = (int)(acc >> 32);
    }
#else
    {
        BN_ULONG t_d[BN_NIST_192_TOP];

        nist_set_192(t_d, buf.bn, 0, 3, 3);
        carry = (int)bn_add_words(r_d, r_d, t_d, BN_NIST_192_TOP);
        nist_set_192(t_d, buf.bn, 4, 4, 0);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_192_TOP);
        nist_set_192(t_d, buf.bn, 5, 5, 5)
            carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_192_TOP);
    }
#endif
    if (carry > 0)
        carry =
            (int)bn_sub_words(r_d, r_d, _nist_p_192[carry - 1],
                              BN_NIST_192_TOP);
    else
        carry = 1;

    /*
     * we need 'if (carry==0 || result>=modulus) result-=modulus;'
     * as comparison implies subtraction, we can write
     * 'tmp=result-modulus; if (!carry || !borrow) result=tmp;'
     * this is what happens below, but without explicit if:-) a.
     */
    mask =
        0 - (PTR_SIZE_INT) bn_sub_words(c_d, r_d, _nist_p_192[0],
                                        BN_NIST_192_TOP);
    mask &= 0 - (PTR_SIZE_INT) carry;
    res = c_d;
    res = (BN_ULONG *)
        (((PTR_SIZE_INT) res & ~mask) | ((PTR_SIZE_INT) r_d & mask));
    nist_cp_bn(r_d, res, BN_NIST_192_TOP);
    r->top = BN_NIST_192_TOP;
    bn_correct_top(r);

    return 1;
}

typedef BN_ULONG (*bn_addsub_f) (BN_ULONG *, const BN_ULONG *,
                                 const BN_ULONG *, int);

#define nist_set_224(to, from, a1, a2, a3, a4, a5, a6, a7) \
        { \
        bn_cp_32(to, 0, from, (a7) - 7) \
        bn_cp_32(to, 1, from, (a6) - 7) \
        bn_cp_32(to, 2, from, (a5) - 7) \
        bn_cp_32(to, 3, from, (a4) - 7) \
        bn_cp_32(to, 4, from, (a3) - 7) \
        bn_cp_32(to, 5, from, (a2) - 7) \
        bn_cp_32(to, 6, from, (a1) - 7) \
        }

int BN_nist_mod_224(BIGNUM *r, const BIGNUM *a, const BIGNUM *field,
                    BN_CTX *ctx)
{
    int top = a->top, i;
    int carry;
    BN_ULONG *r_d, *a_d = a->d;
    union {
        BN_ULONG bn[BN_NIST_224_TOP];
        unsigned int ui[BN_NIST_224_TOP * sizeof(BN_ULONG) /
                        sizeof(unsigned int)];
    } buf;
    BN_ULONG c_d[BN_NIST_224_TOP], *res;
    PTR_SIZE_INT mask;
    union {
        bn_addsub_f f;
        PTR_SIZE_INT p;
    } u;
    static const BIGNUM _bignum_nist_p_224_sqr = {
        (BN_ULONG *)_nist_p_224_sqr,
        OSSL_NELEM(_nist_p_224_sqr),
        OSSL_NELEM(_nist_p_224_sqr),
        0, BN_FLG_STATIC_DATA
    };

    field = &_bignum_nist_p_224; /* just to make sure */

    if (BN_is_negative(a) || BN_ucmp(a, &_bignum_nist_p_224_sqr) >= 0)
        return BN_nnmod(r, a, field, ctx);

    i = BN_ucmp(field, a);
    if (i == 0) {
        BN_zero(r);
        return 1;
    } else if (i > 0)
        return (r == a) ? 1 : (BN_copy(r, a) != NULL);

    if (r != a) {
        if (!bn_wexpand(r, BN_NIST_224_TOP))
            return 0;
        r_d = r->d;
        nist_cp_bn(r_d, a_d, BN_NIST_224_TOP);
    } else
        r_d = a_d;

#if BN_BITS2==64
    /* copy upper 256 bits of 448 bit number ... */
    nist_cp_bn_0(c_d, a_d + (BN_NIST_224_TOP - 1),
                 top - (BN_NIST_224_TOP - 1), BN_NIST_224_TOP);
    /* ... and right shift by 32 to obtain upper 224 bits */
    nist_set_224(buf.bn, c_d, 14, 13, 12, 11, 10, 9, 8);
    /* truncate lower part to 224 bits too */
    r_d[BN_NIST_224_TOP - 1] &= BN_MASK2l;
#else
    nist_cp_bn_0(buf.bn, a_d + BN_NIST_224_TOP, top - BN_NIST_224_TOP,
                 BN_NIST_224_TOP);
#endif

#if defined(NIST_INT64) && BN_BITS2!=64
    {
        NIST_INT64 acc;         /* accumulator */
        unsigned int *rp = (unsigned int *)r_d;
        const unsigned int *bp = (const unsigned int *)buf.ui;

        acc = rp[0];
        acc -= bp[7 - 7];
        acc -= bp[11 - 7];
        rp[0] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[1];
        acc -= bp[8 - 7];
        acc -= bp[12 - 7];
        rp[1] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[2];
        acc -= bp[9 - 7];
        acc -= bp[13 - 7];
        rp[2] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[3];
        acc += bp[7 - 7];
        acc += bp[11 - 7];
        acc -= bp[10 - 7];
        rp[3] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[4];
        acc += bp[8 - 7];
        acc += bp[12 - 7];
        acc -= bp[11 - 7];
        rp[4] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[5];
        acc += bp[9 - 7];
        acc += bp[13 - 7];
        acc -= bp[12 - 7];
        rp[5] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[6];
        acc += bp[10 - 7];
        acc -= bp[13 - 7];
        rp[6] = (unsigned int)acc;

        carry = (int)(acc >> 32);
# if BN_BITS2==64
        rp[7] = carry;
# endif
    }
#else
    {
        BN_ULONG t_d[BN_NIST_224_TOP];

        nist_set_224(t_d, buf.bn, 10, 9, 8, 7, 0, 0, 0);
        carry = (int)bn_add_words(r_d, r_d, t_d, BN_NIST_224_TOP);
        nist_set_224(t_d, buf.bn, 0, 13, 12, 11, 0, 0, 0);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_224_TOP);
        nist_set_224(t_d, buf.bn, 13, 12, 11, 10, 9, 8, 7);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_224_TOP);
        nist_set_224(t_d, buf.bn, 0, 0, 0, 0, 13, 12, 11);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_224_TOP);

# if BN_BITS2==64
        carry = (int)(r_d[BN_NIST_224_TOP - 1] >> 32);
# endif
    }
#endif
    u.f = bn_sub_words;
    if (carry > 0) {
        carry =
            (int)bn_sub_words(r_d, r_d, _nist_p_224[carry - 1],
                              BN_NIST_224_TOP);
#if BN_BITS2==64
        carry = (int)(~(r_d[BN_NIST_224_TOP - 1] >> 32)) & 1;
#endif
    } else if (carry < 0) {
        /*
         * it's a bit more complicated logic in this case. if bn_add_words
         * yields no carry, then result has to be adjusted by unconditionally
         * *adding* the modulus. but if it does, then result has to be
         * compared to the modulus and conditionally adjusted by
         * *subtracting* the latter.
         */
        carry =
            (int)bn_add_words(r_d, r_d, _nist_p_224[-carry - 1],
                              BN_NIST_224_TOP);
        mask = 0 - (PTR_SIZE_INT) carry;
        u.p = ((PTR_SIZE_INT) bn_sub_words & mask) |
            ((PTR_SIZE_INT) bn_add_words & ~mask);
    } else
        carry = 1;

    /* otherwise it's effectively same as in BN_nist_mod_192... */
    mask =
        0 - (PTR_SIZE_INT) (*u.f) (c_d, r_d, _nist_p_224[0], BN_NIST_224_TOP);
    mask &= 0 - (PTR_SIZE_INT) carry;
    res = c_d;
    res = (BN_ULONG *)(((PTR_SIZE_INT) res & ~mask) |
                       ((PTR_SIZE_INT) r_d & mask));
    nist_cp_bn(r_d, res, BN_NIST_224_TOP);
    r->top = BN_NIST_224_TOP;
    bn_correct_top(r);

    return 1;
}

#define nist_set_256(to, from, a1, a2, a3, a4, a5, a6, a7, a8) \
        { \
        bn_cp_32(to, 0, from, (a8) - 8) \
        bn_cp_32(to, 1, from, (a7) - 8) \
        bn_cp_32(to, 2, from, (a6) - 8) \
        bn_cp_32(to, 3, from, (a5) - 8) \
        bn_cp_32(to, 4, from, (a4) - 8) \
        bn_cp_32(to, 5, from, (a3) - 8) \
        bn_cp_32(to, 6, from, (a2) - 8) \
        bn_cp_32(to, 7, from, (a1) - 8) \
        }

int BN_nist_mod_256(BIGNUM *r, const BIGNUM *a, const BIGNUM *field,
                    BN_CTX *ctx)
{
    int i, top = a->top;
    int carry = 0;
    register BN_ULONG *a_d = a->d, *r_d;
    union {
        BN_ULONG bn[BN_NIST_256_TOP];
        unsigned int ui[BN_NIST_256_TOP * sizeof(BN_ULONG) /
                        sizeof(unsigned int)];
    } buf;
    BN_ULONG c_d[BN_NIST_256_TOP], *res;
    PTR_SIZE_INT mask;
    union {
        bn_addsub_f f;
        PTR_SIZE_INT p;
    } u;
    static const BIGNUM _bignum_nist_p_256_sqr = {
        (BN_ULONG *)_nist_p_256_sqr,
        OSSL_NELEM(_nist_p_256_sqr),
        OSSL_NELEM(_nist_p_256_sqr),
        0, BN_FLG_STATIC_DATA
    };

    field = &_bignum_nist_p_256; /* just to make sure */

    if (BN_is_negative(a) || BN_ucmp(a, &_bignum_nist_p_256_sqr) >= 0)
        return BN_nnmod(r, a, field, ctx);

    i = BN_ucmp(field, a);
    if (i == 0) {
        BN_zero(r);
        return 1;
    } else if (i > 0)
        return (r == a) ? 1 : (BN_copy(r, a) != NULL);

    if (r != a) {
        if (!bn_wexpand(r, BN_NIST_256_TOP))
            return 0;
        r_d = r->d;
        nist_cp_bn(r_d, a_d, BN_NIST_256_TOP);
    } else
        r_d = a_d;

    nist_cp_bn_0(buf.bn, a_d + BN_NIST_256_TOP, top - BN_NIST_256_TOP,
                 BN_NIST_256_TOP);

#if defined(NIST_INT64)
    {
        NIST_INT64 acc;         /* accumulator */
        unsigned int *rp = (unsigned int *)r_d;
        const unsigned int *bp = (const unsigned int *)buf.ui;

        acc = rp[0];
        acc += bp[8 - 8];
        acc += bp[9 - 8];
        acc -= bp[11 - 8];
        acc -= bp[12 - 8];
        acc -= bp[13 - 8];
        acc -= bp[14 - 8];
        rp[0] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[1];
        acc += bp[9 - 8];
        acc += bp[10 - 8];
        acc -= bp[12 - 8];
        acc -= bp[13 - 8];
        acc -= bp[14 - 8];
        acc -= bp[15 - 8];
        rp[1] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[2];
        acc += bp[10 - 8];
        acc += bp[11 - 8];
        acc -= bp[13 - 8];
        acc -= bp[14 - 8];
        acc -= bp[15 - 8];
        rp[2] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[3];
        acc += bp[11 - 8];
        acc += bp[11 - 8];
        acc += bp[12 - 8];
        acc += bp[12 - 8];
        acc += bp[13 - 8];
        acc -= bp[15 - 8];
        acc -= bp[8 - 8];
        acc -= bp[9 - 8];
        rp[3] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[4];
        acc += bp[12 - 8];
        acc += bp[12 - 8];
        acc += bp[13 - 8];
        acc += bp[13 - 8];
        acc += bp[14 - 8];
        acc -= bp[9 - 8];
        acc -= bp[10 - 8];
        rp[4] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[5];
        acc += bp[13 - 8];
        acc += bp[13 - 8];
        acc += bp[14 - 8];
        acc += bp[14 - 8];
        acc += bp[15 - 8];
        acc -= bp[10 - 8];
        acc -= bp[11 - 8];
        rp[5] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[6];
        acc += bp[14 - 8];
        acc += bp[14 - 8];
        acc += bp[15 - 8];
        acc += bp[15 - 8];
        acc += bp[14 - 8];
        acc += bp[13 - 8];
        acc -= bp[8 - 8];
        acc -= bp[9 - 8];
        rp[6] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[7];
        acc += bp[15 - 8];
        acc += bp[15 - 8];
        acc += bp[15 - 8];
        acc += bp[8 - 8];
        acc -= bp[10 - 8];
        acc -= bp[11 - 8];
        acc -= bp[12 - 8];
        acc -= bp[13 - 8];
        rp[7] = (unsigned int)acc;

        carry = (int)(acc >> 32);
    }
#else
    {
        BN_ULONG t_d[BN_NIST_256_TOP];

        /*
         * S1
         */
        nist_set_256(t_d, buf.bn, 15, 14, 13, 12, 11, 0, 0, 0);
        /*
         * S2
         */
        nist_set_256(c_d, buf.bn, 0, 15, 14, 13, 12, 0, 0, 0);
        carry = (int)bn_add_words(t_d, t_d, c_d, BN_NIST_256_TOP);
        /* left shift */
        {
            register BN_ULONG *ap, t, c;
            ap = t_d;
            c = 0;
            for (i = BN_NIST_256_TOP; i != 0; --i) {
                t = *ap;
                *(ap++) = ((t << 1) | c) & BN_MASK2;
                c = (t & BN_TBIT) ? 1 : 0;
            }
            carry <<= 1;
            carry |= c;
        }
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_256_TOP);
        /*
         * S3
         */
        nist_set_256(t_d, buf.bn, 15, 14, 0, 0, 0, 10, 9, 8);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_256_TOP);
        /*
         * S4
         */
        nist_set_256(t_d, buf.bn, 8, 13, 15, 14, 13, 11, 10, 9);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_256_TOP);
        /*
         * D1
         */
        nist_set_256(t_d, buf.bn, 10, 8, 0, 0, 0, 13, 12, 11);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_256_TOP);
        /*
         * D2
         */
        nist_set_256(t_d, buf.bn, 11, 9, 0, 0, 15, 14, 13, 12);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_256_TOP);
        /*
         * D3
         */
        nist_set_256(t_d, buf.bn, 12, 0, 10, 9, 8, 15, 14, 13);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_256_TOP);
        /*
         * D4
         */
        nist_set_256(t_d, buf.bn, 13, 0, 11, 10, 9, 0, 15, 14);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_256_TOP);

    }
#endif
    /* see BN_nist_mod_224 for explanation */
    u.f = bn_sub_words;
    if (carry > 0)
        carry =
            (int)bn_sub_words(r_d, r_d, _nist_p_256[carry - 1],
                              BN_NIST_256_TOP);
    else if (carry < 0) {
        carry =
            (int)bn_add_words(r_d, r_d, _nist_p_256[-carry - 1],
                              BN_NIST_256_TOP);
        mask = 0 - (PTR_SIZE_INT) carry;
        u.p = ((PTR_SIZE_INT) bn_sub_words & mask) |
            ((PTR_SIZE_INT) bn_add_words & ~mask);
    } else
        carry = 1;

    mask =
        0 - (PTR_SIZE_INT) (*u.f) (c_d, r_d, _nist_p_256[0], BN_NIST_256_TOP);
    mask &= 0 - (PTR_SIZE_INT) carry;
    res = c_d;
    res = (BN_ULONG *)(((PTR_SIZE_INT) res & ~mask) |
                       ((PTR_SIZE_INT) r_d & mask));
    nist_cp_bn(r_d, res, BN_NIST_256_TOP);
    r->top = BN_NIST_256_TOP;
    bn_correct_top(r);

    return 1;
}

#define nist_set_384(to,from,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12) \
        { \
        bn_cp_32(to, 0, from,  (a12) - 12) \
        bn_cp_32(to, 1, from,  (a11) - 12) \
        bn_cp_32(to, 2, from,  (a10) - 12) \
        bn_cp_32(to, 3, from,  (a9) - 12)  \
        bn_cp_32(to, 4, from,  (a8) - 12)  \
        bn_cp_32(to, 5, from,  (a7) - 12)  \
        bn_cp_32(to, 6, from,  (a6) - 12)  \
        bn_cp_32(to, 7, from,  (a5) - 12)  \
        bn_cp_32(to, 8, from,  (a4) - 12)  \
        bn_cp_32(to, 9, from,  (a3) - 12)  \
        bn_cp_32(to, 10, from, (a2) - 12)  \
        bn_cp_32(to, 11, from, (a1) - 12)  \
        }

int BN_nist_mod_384(BIGNUM *r, const BIGNUM *a, const BIGNUM *field,
                    BN_CTX *ctx)
{
    int i, top = a->top;
    int carry = 0;
    register BN_ULONG *r_d, *a_d = a->d;
    union {
        BN_ULONG bn[BN_NIST_384_TOP];
        unsigned int ui[BN_NIST_384_TOP * sizeof(BN_ULONG) /
                        sizeof(unsigned int)];
    } buf;
    BN_ULONG c_d[BN_NIST_384_TOP], *res;
    PTR_SIZE_INT mask;
    union {
        bn_addsub_f f;
        PTR_SIZE_INT p;
    } u;
    static const BIGNUM _bignum_nist_p_384_sqr = {
        (BN_ULONG *)_nist_p_384_sqr,
        OSSL_NELEM(_nist_p_384_sqr),
        OSSL_NELEM(_nist_p_384_sqr),
        0, BN_FLG_STATIC_DATA
    };

    field = &_bignum_nist_p_384; /* just to make sure */

    if (BN_is_negative(a) || BN_ucmp(a, &_bignum_nist_p_384_sqr) >= 0)
        return BN_nnmod(r, a, field, ctx);

    i = BN_ucmp(field, a);
    if (i == 0) {
        BN_zero(r);
        return 1;
    } else if (i > 0)
        return (r == a) ? 1 : (BN_copy(r, a) != NULL);

    if (r != a) {
        if (!bn_wexpand(r, BN_NIST_384_TOP))
            return 0;
        r_d = r->d;
        nist_cp_bn(r_d, a_d, BN_NIST_384_TOP);
    } else
        r_d = a_d;

    nist_cp_bn_0(buf.bn, a_d + BN_NIST_384_TOP, top - BN_NIST_384_TOP,
                 BN_NIST_384_TOP);

#if defined(NIST_INT64)
    {
        NIST_INT64 acc;         /* accumulator */
        unsigned int *rp = (unsigned int *)r_d;
        const unsigned int *bp = (const unsigned int *)buf.ui;

        acc = rp[0];
        acc += bp[12 - 12];
        acc += bp[21 - 12];
        acc += bp[20 - 12];
        acc -= bp[23 - 12];
        rp[0] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[1];
        acc += bp[13 - 12];
        acc += bp[22 - 12];
        acc += bp[23 - 12];
        acc -= bp[12 - 12];
        acc -= bp[20 - 12];
        rp[1] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[2];
        acc += bp[14 - 12];
        acc += bp[23 - 12];
        acc -= bp[13 - 12];
        acc -= bp[21 - 12];
        rp[2] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[3];
        acc += bp[15 - 12];
        acc += bp[12 - 12];
        acc += bp[20 - 12];
        acc += bp[21 - 12];
        acc -= bp[14 - 12];
        acc -= bp[22 - 12];
        acc -= bp[23 - 12];
        rp[3] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[4];
        acc += bp[21 - 12];
        acc += bp[21 - 12];
        acc += bp[16 - 12];
        acc += bp[13 - 12];
        acc += bp[12 - 12];
        acc += bp[20 - 12];
        acc += bp[22 - 12];
        acc -= bp[15 - 12];
        acc -= bp[23 - 12];
        acc -= bp[23 - 12];
        rp[4] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[5];
        acc += bp[22 - 12];
        acc += bp[22 - 12];
        acc += bp[17 - 12];
        acc += bp[14 - 12];
        acc += bp[13 - 12];
        acc += bp[21 - 12];
        acc += bp[23 - 12];
        acc -= bp[16 - 12];
        rp[5] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[6];
        acc += bp[23 - 12];
        acc += bp[23 - 12];
        acc += bp[18 - 12];
        acc += bp[15 - 12];
        acc += bp[14 - 12];
        acc += bp[22 - 12];
        acc -= bp[17 - 12];
        rp[6] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[7];
        acc += bp[19 - 12];
        acc += bp[16 - 12];
        acc += bp[15 - 12];
        acc += bp[23 - 12];
        acc -= bp[18 - 12];
        rp[7] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[8];
        acc += bp[20 - 12];
        acc += bp[17 - 12];
        acc += bp[16 - 12];
        acc -= bp[19 - 12];
        rp[8] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[9];
        acc += bp[21 - 12];
        acc += bp[18 - 12];
        acc += bp[17 - 12];
        acc -= bp[20 - 12];
        rp[9] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[10];
        acc += bp[22 - 12];
        acc += bp[19 - 12];
        acc += bp[18 - 12];
        acc -= bp[21 - 12];
        rp[10] = (unsigned int)acc;
        acc >>= 32;

        acc += rp[11];
        acc += bp[23 - 12];
        acc += bp[20 - 12];
        acc += bp[19 - 12];
        acc -= bp[22 - 12];
        rp[11] = (unsigned int)acc;

        carry = (int)(acc >> 32);
    }
#else
    {
        BN_ULONG t_d[BN_NIST_384_TOP];

        /*
         * S1
         */
        nist_set_256(t_d, buf.bn, 0, 0, 0, 0, 0, 23 - 4, 22 - 4, 21 - 4);
        /* left shift */
        {
            register BN_ULONG *ap, t, c;
            ap = t_d;
            c = 0;
            for (i = 3; i != 0; --i) {
                t = *ap;
                *(ap++) = ((t << 1) | c) & BN_MASK2;
                c = (t & BN_TBIT) ? 1 : 0;
            }
            *ap = c;
        }
        carry =
            (int)bn_add_words(r_d + (128 / BN_BITS2), r_d + (128 / BN_BITS2),
                              t_d, BN_NIST_256_TOP);
        /*
         * S2
         */
        carry += (int)bn_add_words(r_d, r_d, buf.bn, BN_NIST_384_TOP);
        /*
         * S3
         */
        nist_set_384(t_d, buf.bn, 20, 19, 18, 17, 16, 15, 14, 13, 12, 23, 22,
                     21);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_384_TOP);
        /*
         * S4
         */
        nist_set_384(t_d, buf.bn, 19, 18, 17, 16, 15, 14, 13, 12, 20, 0, 23,
                     0);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_384_TOP);
        /*
         * S5
         */
        nist_set_384(t_d, buf.bn, 0, 0, 0, 0, 23, 22, 21, 20, 0, 0, 0, 0);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_384_TOP);
        /*
         * S6
         */
        nist_set_384(t_d, buf.bn, 0, 0, 0, 0, 0, 0, 23, 22, 21, 0, 0, 20);
        carry += (int)bn_add_words(r_d, r_d, t_d, BN_NIST_384_TOP);
        /*
         * D1
         */
        nist_set_384(t_d, buf.bn, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12,
                     23);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_384_TOP);
        /*
         * D2
         */
        nist_set_384(t_d, buf.bn, 0, 0, 0, 0, 0, 0, 0, 23, 22, 21, 20, 0);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_384_TOP);
        /*
         * D3
         */
        nist_set_384(t_d, buf.bn, 0, 0, 0, 0, 0, 0, 0, 23, 23, 0, 0, 0);
        carry -= (int)bn_sub_words(r_d, r_d, t_d, BN_NIST_384_TOP);

    }
#endif
    /* see BN_nist_mod_224 for explanation */
    u.f = bn_sub_words;
    if (carry > 0)
        carry =
            (int)bn_sub_words(r_d, r_d, _nist_p_384[carry - 1],
                              BN_NIST_384_TOP);
    else if (carry < 0) {
        carry =
            (int)bn_add_words(r_d, r_d, _nist_p_384[-carry - 1],
                              BN_NIST_384_TOP);
        mask = 0 - (PTR_SIZE_INT) carry;
        u.p = ((PTR_SIZE_INT) bn_sub_words & mask) |
            ((PTR_SIZE_INT) bn_add_words & ~mask);
    } else
        carry = 1;

    mask =
        0 - (PTR_SIZE_INT) (*u.f) (c_d, r_d, _nist_p_384[0], BN_NIST_384_TOP);
    mask &= 0 - (PTR_SIZE_INT) carry;
    res = c_d;
    res = (BN_ULONG *)(((PTR_SIZE_INT) res & ~mask) |
                       ((PTR_SIZE_INT) r_d & mask));
    nist_cp_bn(r_d, res, BN_NIST_384_TOP);
    r->top = BN_NIST_384_TOP;
    bn_correct_top(r);

    return 1;
}

#define BN_NIST_521_RSHIFT      (521%BN_BITS2)
#define BN_NIST_521_LSHIFT      (BN_BITS2-BN_NIST_521_RSHIFT)
#define BN_NIST_521_TOP_MASK    ((BN_ULONG)BN_MASK2>>BN_NIST_521_LSHIFT)

int BN_nist_mod_521(BIGNUM *r, const BIGNUM *a, const BIGNUM *field,
                    BN_CTX *ctx)
{
    int top = a->top, i;
    BN_ULONG *r_d, *a_d = a->d, t_d[BN_NIST_521_TOP], val, tmp, *res;
    PTR_SIZE_INT mask;
    static const BIGNUM _bignum_nist_p_521_sqr = {
        (BN_ULONG *)_nist_p_521_sqr,
        OSSL_NELEM(_nist_p_521_sqr),
        OSSL_NELEM(_nist_p_521_sqr),
        0, BN_FLG_STATIC_DATA
    };

    field = &_bignum_nist_p_521; /* just to make sure */

    if (BN_is_negative(a) || BN_ucmp(a, &_bignum_nist_p_521_sqr) >= 0)
        return BN_nnmod(r, a, field, ctx);

    i = BN_ucmp(field, a);
    if (i == 0) {
        BN_zero(r);
        return 1;
    } else if (i > 0)
        return (r == a) ? 1 : (BN_copy(r, a) != NULL);

    if (r != a) {
        if (!bn_wexpand(r, BN_NIST_521_TOP))
            return 0;
        r_d = r->d;
        nist_cp_bn(r_d, a_d, BN_NIST_521_TOP);
    } else
        r_d = a_d;

    /* upper 521 bits, copy ... */
    nist_cp_bn_0(t_d, a_d + (BN_NIST_521_TOP - 1),
                 top - (BN_NIST_521_TOP - 1), BN_NIST_521_TOP);
    /* ... and right shift */
    for (val = t_d[0], i = 0; i < BN_NIST_521_TOP - 1; i++) {
#if 0
        /*
         * MSC ARM compiler [version 2013, presumably even earlier,
         * much earlier] miscompiles this code, but not one in
         * #else section. See RT#3541.
         */
        tmp = val >> BN_NIST_521_RSHIFT;
        val = t_d[i + 1];
        t_d[i] = (tmp | val << BN_NIST_521_LSHIFT) & BN_MASK2;
#else
        t_d[i] = (val >> BN_NIST_521_RSHIFT |
                  (tmp = t_d[i + 1]) << BN_NIST_521_LSHIFT) & BN_MASK2;
        val = tmp;
#endif
    }
    t_d[i] = val >> BN_NIST_521_RSHIFT;
    /* lower 521 bits */
    r_d[i] &= BN_NIST_521_TOP_MASK;

    bn_add_words(r_d, r_d, t_d, BN_NIST_521_TOP);
    mask =
        0 - (PTR_SIZE_INT) bn_sub_words(t_d, r_d, _nist_p_521,
                                        BN_NIST_521_TOP);
    res = t_d;
    res = (BN_ULONG *)(((PTR_SIZE_INT) res & ~mask) |
                       ((PTR_SIZE_INT) r_d & mask));
    nist_cp_bn(r_d, res, BN_NIST_521_TOP);
    r->top = BN_NIST_521_TOP;
    bn_correct_top(r);

    return 1;
}

int (*BN_nist_mod_func(const BIGNUM *p)) (BIGNUM *r, const BIGNUM *a,
                                          const BIGNUM *field, BN_CTX *ctx) {
    if (BN_ucmp(&_bignum_nist_p_192, p) == 0)
        return BN_nist_mod_192;
    if (BN_ucmp(&_bignum_nist_p_224, p) == 0)
        return BN_nist_mod_224;
    if (BN_ucmp(&_bignum_nist_p_256, p) == 0)
        return BN_nist_mod_256;
    if (BN_ucmp(&_bignum_nist_p_384, p) == 0)
        return BN_nist_mod_384;
    if (BN_ucmp(&_bignum_nist_p_521, p) == 0)
        return BN_nist_mod_521;
    return 0;
}
