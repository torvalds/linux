/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2015-2016 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */
#include <openssl/crypto.h>
#include "word.h"
#include "field.h"

#include "point_448.h"
#include "ed448.h"
#include "curve448_lcl.h"

#define COFACTOR 4

#define C448_WNAF_FIXED_TABLE_BITS 5
#define C448_WNAF_VAR_TABLE_BITS 3

#define EDWARDS_D       (-39081)

static const curve448_scalar_t precomputed_scalarmul_adjustment = {
    {
        {
            SC_LIMB(0xc873d6d54a7bb0cf), SC_LIMB(0xe933d8d723a70aad),
            SC_LIMB(0xbb124b65129c96fd), SC_LIMB(0x00000008335dc163)
        }
    }
};

#define TWISTED_D (EDWARDS_D - 1)

#define WBITS C448_WORD_BITS   /* NB this may be different from ARCH_WORD_BITS */

/* Inverse. */
static void gf_invert(gf y, const gf x, int assert_nonzero)
{
    mask_t ret;
    gf t1, t2;

    gf_sqr(t1, x);              /* o^2 */
    ret = gf_isr(t2, t1);       /* +-1/sqrt(o^2) = +-1/o */
    (void)ret;
    if (assert_nonzero)
        assert(ret);
    gf_sqr(t1, t2);
    gf_mul(t2, t1, x);          /* not direct to y in case of alias. */
    gf_copy(y, t2);
}

/** identity = (0,1) */
const curve448_point_t curve448_point_identity =
    { {{{{0}}}, {{{1}}}, {{{1}}}, {{{0}}}} };

static void point_double_internal(curve448_point_t p, const curve448_point_t q,
                                  int before_double)
{
    gf a, b, c, d;

    gf_sqr(c, q->x);
    gf_sqr(a, q->y);
    gf_add_nr(d, c, a);         /* 2+e */
    gf_add_nr(p->t, q->y, q->x); /* 2+e */
    gf_sqr(b, p->t);
    gf_subx_nr(b, b, d, 3);     /* 4+e */
    gf_sub_nr(p->t, a, c);      /* 3+e */
    gf_sqr(p->x, q->z);
    gf_add_nr(p->z, p->x, p->x); /* 2+e */
    gf_subx_nr(a, p->z, p->t, 4); /* 6+e */
    if (GF_HEADROOM == 5)
        gf_weak_reduce(a);      /* or 1+e */
    gf_mul(p->x, a, b);
    gf_mul(p->z, p->t, a);
    gf_mul(p->y, p->t, d);
    if (!before_double)
        gf_mul(p->t, b, d);
}

void curve448_point_double(curve448_point_t p, const curve448_point_t q)
{
    point_double_internal(p, q, 0);
}

/* Operations on [p]niels */
static ossl_inline void cond_neg_niels(niels_t n, mask_t neg)
{
    gf_cond_swap(n->a, n->b, neg);
    gf_cond_neg(n->c, neg);
}

static void pt_to_pniels(pniels_t b, const curve448_point_t a)
{
    gf_sub(b->n->a, a->y, a->x);
    gf_add(b->n->b, a->x, a->y);
    gf_mulw(b->n->c, a->t, 2 * TWISTED_D);
    gf_add(b->z, a->z, a->z);
}

static void pniels_to_pt(curve448_point_t e, const pniels_t d)
{
    gf eu;

    gf_add(eu, d->n->b, d->n->a);
    gf_sub(e->y, d->n->b, d->n->a);
    gf_mul(e->t, e->y, eu);
    gf_mul(e->x, d->z, e->y);
    gf_mul(e->y, d->z, eu);
    gf_sqr(e->z, d->z);
}

static void niels_to_pt(curve448_point_t e, const niels_t n)
{
    gf_add(e->y, n->b, n->a);
    gf_sub(e->x, n->b, n->a);
    gf_mul(e->t, e->y, e->x);
    gf_copy(e->z, ONE);
}

static void add_niels_to_pt(curve448_point_t d, const niels_t e,
                            int before_double)
{
    gf a, b, c;

    gf_sub_nr(b, d->y, d->x);   /* 3+e */
    gf_mul(a, e->a, b);
    gf_add_nr(b, d->x, d->y);   /* 2+e */
    gf_mul(d->y, e->b, b);
    gf_mul(d->x, e->c, d->t);
    gf_add_nr(c, a, d->y);      /* 2+e */
    gf_sub_nr(b, d->y, a);      /* 3+e */
    gf_sub_nr(d->y, d->z, d->x); /* 3+e */
    gf_add_nr(a, d->x, d->z);   /* 2+e */
    gf_mul(d->z, a, d->y);
    gf_mul(d->x, d->y, b);
    gf_mul(d->y, a, c);
    if (!before_double)
        gf_mul(d->t, b, c);
}

static void sub_niels_from_pt(curve448_point_t d, const niels_t e,
                              int before_double)
{
    gf a, b, c;

    gf_sub_nr(b, d->y, d->x);   /* 3+e */
    gf_mul(a, e->b, b);
    gf_add_nr(b, d->x, d->y);   /* 2+e */
    gf_mul(d->y, e->a, b);
    gf_mul(d->x, e->c, d->t);
    gf_add_nr(c, a, d->y);      /* 2+e */
    gf_sub_nr(b, d->y, a);      /* 3+e */
    gf_add_nr(d->y, d->z, d->x); /* 2+e */
    gf_sub_nr(a, d->z, d->x);   /* 3+e */
    gf_mul(d->z, a, d->y);
    gf_mul(d->x, d->y, b);
    gf_mul(d->y, a, c);
    if (!before_double)
        gf_mul(d->t, b, c);
}

static void add_pniels_to_pt(curve448_point_t p, const pniels_t pn,
                             int before_double)
{
    gf L0;

    gf_mul(L0, p->z, pn->z);
    gf_copy(p->z, L0);
    add_niels_to_pt(p, pn->n, before_double);
}

static void sub_pniels_from_pt(curve448_point_t p, const pniels_t pn,
                               int before_double)
{
    gf L0;

    gf_mul(L0, p->z, pn->z);
    gf_copy(p->z, L0);
    sub_niels_from_pt(p, pn->n, before_double);
}

c448_bool_t curve448_point_eq(const curve448_point_t p,
                              const curve448_point_t q)
{
    mask_t succ;
    gf a, b;

    /* equality mod 2-torsion compares x/y */
    gf_mul(a, p->y, q->x);
    gf_mul(b, q->y, p->x);
    succ = gf_eq(a, b);

    return mask_to_bool(succ);
}

c448_bool_t curve448_point_valid(const curve448_point_t p)
{
    mask_t out;
    gf a, b, c;

    gf_mul(a, p->x, p->y);
    gf_mul(b, p->z, p->t);
    out = gf_eq(a, b);
    gf_sqr(a, p->x);
    gf_sqr(b, p->y);
    gf_sub(a, b, a);
    gf_sqr(b, p->t);
    gf_mulw(c, b, TWISTED_D);
    gf_sqr(b, p->z);
    gf_add(b, b, c);
    out &= gf_eq(a, b);
    out &= ~gf_eq(p->z, ZERO);
    return mask_to_bool(out);
}

static ossl_inline void constant_time_lookup_niels(niels_s * RESTRICT ni,
                                                   const niels_t * table,
                                                   int nelts, int idx)
{
    constant_time_lookup(ni, table, sizeof(niels_s), nelts, idx);
}

void curve448_precomputed_scalarmul(curve448_point_t out,
                                    const curve448_precomputed_s * table,
                                    const curve448_scalar_t scalar)
{
    unsigned int i, j, k;
    const unsigned int n = COMBS_N, t = COMBS_T, s = COMBS_S;
    niels_t ni;
    curve448_scalar_t scalar1x;

    curve448_scalar_add(scalar1x, scalar, precomputed_scalarmul_adjustment);
    curve448_scalar_halve(scalar1x, scalar1x);

    for (i = s; i > 0; i--) {
        if (i != s)
            point_double_internal(out, out, 0);

        for (j = 0; j < n; j++) {
            int tab = 0;
            mask_t invert;

            for (k = 0; k < t; k++) {
                unsigned int bit = (i - 1) + s * (k + j * t);

                if (bit < C448_SCALAR_BITS)
                    tab |=
                        (scalar1x->limb[bit / WBITS] >> (bit % WBITS) & 1) << k;
            }

            invert = (tab >> (t - 1)) - 1;
            tab ^= invert;
            tab &= (1 << (t - 1)) - 1;

            constant_time_lookup_niels(ni, &table->table[j << (t - 1)],
                                       1 << (t - 1), tab);

            cond_neg_niels(ni, invert);
            if ((i != s) || j != 0)
                add_niels_to_pt(out, ni, j == n - 1 && i != 1);
            else
                niels_to_pt(out, ni);
        }
    }

    OPENSSL_cleanse(ni, sizeof(ni));
    OPENSSL_cleanse(scalar1x, sizeof(scalar1x));
}

void curve448_point_mul_by_ratio_and_encode_like_eddsa(
                                    uint8_t enc[EDDSA_448_PUBLIC_BYTES],
                                    const curve448_point_t p)
{
    gf x, y, z, t;
    curve448_point_t q;

    /* The point is now on the twisted curve.  Move it to untwisted. */
    curve448_point_copy(q, p);

    {
        /* 4-isogeny: 2xy/(y^+x^2), (y^2-x^2)/(2z^2-y^2+x^2) */
        gf u;

        gf_sqr(x, q->x);
        gf_sqr(t, q->y);
        gf_add(u, x, t);
        gf_add(z, q->y, q->x);
        gf_sqr(y, z);
        gf_sub(y, y, u);
        gf_sub(z, t, x);
        gf_sqr(x, q->z);
        gf_add(t, x, x);
        gf_sub(t, t, z);
        gf_mul(x, t, y);
        gf_mul(y, z, u);
        gf_mul(z, u, t);
        OPENSSL_cleanse(u, sizeof(u));
    }

    /* Affinize */
    gf_invert(z, z, 1);
    gf_mul(t, x, z);
    gf_mul(x, y, z);

    /* Encode */
    enc[EDDSA_448_PRIVATE_BYTES - 1] = 0;
    gf_serialize(enc, x, 1);
    enc[EDDSA_448_PRIVATE_BYTES - 1] |= 0x80 & gf_lobit(t);

    OPENSSL_cleanse(x, sizeof(x));
    OPENSSL_cleanse(y, sizeof(y));
    OPENSSL_cleanse(z, sizeof(z));
    OPENSSL_cleanse(t, sizeof(t));
    curve448_point_destroy(q);
}

c448_error_t curve448_point_decode_like_eddsa_and_mul_by_ratio(
                                curve448_point_t p,
                                const uint8_t enc[EDDSA_448_PUBLIC_BYTES])
{
    uint8_t enc2[EDDSA_448_PUBLIC_BYTES];
    mask_t low;
    mask_t succ;

    memcpy(enc2, enc, sizeof(enc2));

    low = ~word_is_zero(enc2[EDDSA_448_PRIVATE_BYTES - 1] & 0x80);
    enc2[EDDSA_448_PRIVATE_BYTES - 1] &= ~0x80;

    succ = gf_deserialize(p->y, enc2, 1, 0);
    succ &= word_is_zero(enc2[EDDSA_448_PRIVATE_BYTES - 1]);

    gf_sqr(p->x, p->y);
    gf_sub(p->z, ONE, p->x);    /* num = 1-y^2 */
    gf_mulw(p->t, p->x, EDWARDS_D); /* dy^2 */
    gf_sub(p->t, ONE, p->t);    /* denom = 1-dy^2 or 1-d + dy^2 */

    gf_mul(p->x, p->z, p->t);
    succ &= gf_isr(p->t, p->x); /* 1/sqrt(num * denom) */

    gf_mul(p->x, p->t, p->z);   /* sqrt(num / denom) */
    gf_cond_neg(p->x, gf_lobit(p->x) ^ low);
    gf_copy(p->z, ONE);

    {
        gf a, b, c, d;

        /* 4-isogeny 2xy/(y^2-ax^2), (y^2+ax^2)/(2-y^2-ax^2) */
        gf_sqr(c, p->x);
        gf_sqr(a, p->y);
        gf_add(d, c, a);
        gf_add(p->t, p->y, p->x);
        gf_sqr(b, p->t);
        gf_sub(b, b, d);
        gf_sub(p->t, a, c);
        gf_sqr(p->x, p->z);
        gf_add(p->z, p->x, p->x);
        gf_sub(a, p->z, d);
        gf_mul(p->x, a, b);
        gf_mul(p->z, p->t, a);
        gf_mul(p->y, p->t, d);
        gf_mul(p->t, b, d);
        OPENSSL_cleanse(a, sizeof(a));
        OPENSSL_cleanse(b, sizeof(b));
        OPENSSL_cleanse(c, sizeof(c));
        OPENSSL_cleanse(d, sizeof(d));
    }

    OPENSSL_cleanse(enc2, sizeof(enc2));
    assert(curve448_point_valid(p) || ~succ);

    return c448_succeed_if(mask_to_bool(succ));
}

c448_error_t x448_int(uint8_t out[X_PUBLIC_BYTES],
                      const uint8_t base[X_PUBLIC_BYTES],
                      const uint8_t scalar[X_PRIVATE_BYTES])
{
    gf x1, x2, z2, x3, z3, t1, t2;
    int t;
    mask_t swap = 0;
    mask_t nz;

    (void)gf_deserialize(x1, base, 1, 0);
    gf_copy(x2, ONE);
    gf_copy(z2, ZERO);
    gf_copy(x3, x1);
    gf_copy(z3, ONE);

    for (t = X_PRIVATE_BITS - 1; t >= 0; t--) {
        uint8_t sb = scalar[t / 8];
        mask_t k_t;

        /* Scalar conditioning */
        if (t / 8 == 0)
            sb &= -(uint8_t)COFACTOR;
        else if (t == X_PRIVATE_BITS - 1)
            sb = -1;

        k_t = (sb >> (t % 8)) & 1;
        k_t = 0 - k_t;             /* set to all 0s or all 1s */

        swap ^= k_t;
        gf_cond_swap(x2, x3, swap);
        gf_cond_swap(z2, z3, swap);
        swap = k_t;

        /*
         * The "_nr" below skips coefficient reduction. In the following
         * comments, "2+e" is saying that the coefficients are at most 2+epsilon
         * times the reduction limit.
         */
        gf_add_nr(t1, x2, z2);  /* A = x2 + z2 */ /* 2+e */
        gf_sub_nr(t2, x2, z2);  /* B = x2 - z2 */ /* 3+e */
        gf_sub_nr(z2, x3, z3);  /* D = x3 - z3 */ /* 3+e */
        gf_mul(x2, t1, z2);     /* DA */
        gf_add_nr(z2, z3, x3);  /* C = x3 + z3 */ /* 2+e */
        gf_mul(x3, t2, z2);     /* CB */
        gf_sub_nr(z3, x2, x3);  /* DA-CB */ /* 3+e */
        gf_sqr(z2, z3);         /* (DA-CB)^2 */
        gf_mul(z3, x1, z2);     /* z3 = x1(DA-CB)^2 */
        gf_add_nr(z2, x2, x3);  /* (DA+CB) */ /* 2+e */
        gf_sqr(x3, z2);         /* x3 = (DA+CB)^2 */

        gf_sqr(z2, t1);         /* AA = A^2 */
        gf_sqr(t1, t2);         /* BB = B^2 */
        gf_mul(x2, z2, t1);     /* x2 = AA*BB */
        gf_sub_nr(t2, z2, t1);  /* E = AA-BB */ /* 3+e */

        gf_mulw(t1, t2, -EDWARDS_D); /* E*-d = a24*E */
        gf_add_nr(t1, t1, z2);  /* AA + a24*E */ /* 2+e */
        gf_mul(z2, t2, t1);     /* z2 = E(AA+a24*E) */
    }

    /* Finish */
    gf_cond_swap(x2, x3, swap);
    gf_cond_swap(z2, z3, swap);
    gf_invert(z2, z2, 0);
    gf_mul(x1, x2, z2);
    gf_serialize(out, x1, 1);
    nz = ~gf_eq(x1, ZERO);

    OPENSSL_cleanse(x1, sizeof(x1));
    OPENSSL_cleanse(x2, sizeof(x2));
    OPENSSL_cleanse(z2, sizeof(z2));
    OPENSSL_cleanse(x3, sizeof(x3));
    OPENSSL_cleanse(z3, sizeof(z3));
    OPENSSL_cleanse(t1, sizeof(t1));
    OPENSSL_cleanse(t2, sizeof(t2));

    return c448_succeed_if(mask_to_bool(nz));
}

void curve448_point_mul_by_ratio_and_encode_like_x448(uint8_t
                                                      out[X_PUBLIC_BYTES],
                                                      const curve448_point_t p)
{
    curve448_point_t q;

    curve448_point_copy(q, p);
    gf_invert(q->t, q->x, 0);   /* 1/x */
    gf_mul(q->z, q->t, q->y);   /* y/x */
    gf_sqr(q->y, q->z);         /* (y/x)^2 */
    gf_serialize(out, q->y, 1);
    curve448_point_destroy(q);
}

void x448_derive_public_key(uint8_t out[X_PUBLIC_BYTES],
                            const uint8_t scalar[X_PRIVATE_BYTES])
{
    /* Scalar conditioning */
    uint8_t scalar2[X_PRIVATE_BYTES];
    curve448_scalar_t the_scalar;
    curve448_point_t p;
    unsigned int i;

    memcpy(scalar2, scalar, sizeof(scalar2));
    scalar2[0] &= -(uint8_t)COFACTOR;

    scalar2[X_PRIVATE_BYTES - 1] &= ~((0u - 1u) << ((X_PRIVATE_BITS + 7) % 8));
    scalar2[X_PRIVATE_BYTES - 1] |= 1 << ((X_PRIVATE_BITS + 7) % 8);

    curve448_scalar_decode_long(the_scalar, scalar2, sizeof(scalar2));

    /* Compensate for the encoding ratio */
    for (i = 1; i < X448_ENCODE_RATIO; i <<= 1)
        curve448_scalar_halve(the_scalar, the_scalar);

    curve448_precomputed_scalarmul(p, curve448_precomputed_base, the_scalar);
    curve448_point_mul_by_ratio_and_encode_like_x448(out, p);
    curve448_point_destroy(p);
}

/* Control for variable-time scalar multiply algorithms. */
struct smvt_control {
    int power, addend;
};

#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 3))
# define NUMTRAILINGZEROS	__builtin_ctz
#else
# define NUMTRAILINGZEROS	numtrailingzeros
static uint32_t numtrailingzeros(uint32_t i)
{
    uint32_t tmp;
    uint32_t num = 31;

    if (i == 0)
        return 32;

    tmp = i << 16;
    if (tmp != 0) {
        i = tmp;
        num -= 16;
    }
    tmp = i << 8;
    if (tmp != 0) {
        i = tmp;
        num -= 8;
    }
    tmp = i << 4;
    if (tmp != 0) {
        i = tmp;
        num -= 4;
    }
    tmp = i << 2;
    if (tmp != 0) {
        i = tmp;
        num -= 2;
    }
    tmp = i << 1;
    if (tmp != 0)
        num--;

    return num;
}
#endif

static int recode_wnaf(struct smvt_control *control,
                       /* [nbits/(table_bits + 1) + 3] */
                       const curve448_scalar_t scalar,
                       unsigned int table_bits)
{
    unsigned int table_size = C448_SCALAR_BITS / (table_bits + 1) + 3;
    int position = table_size - 1; /* at the end */
    uint64_t current = scalar->limb[0] & 0xFFFF;
    uint32_t mask = (1 << (table_bits + 1)) - 1;
    unsigned int w;
    const unsigned int B_OVER_16 = sizeof(scalar->limb[0]) / 2;
    unsigned int n, i;

    /* place the end marker */
    control[position].power = -1;
    control[position].addend = 0;
    position--;

    /*
     * PERF: Could negate scalar if it's large.  But then would need more cases
     * in the actual code that uses it, all for an expected reduction of like
     * 1/5 op. Probably not worth it.
     */

    for (w = 1; w < (C448_SCALAR_BITS - 1) / 16 + 3; w++) {
        if (w < (C448_SCALAR_BITS - 1) / 16 + 1) {
            /* Refill the 16 high bits of current */
            current += (uint32_t)((scalar->limb[w / B_OVER_16]
                       >> (16 * (w % B_OVER_16))) << 16);
        }

        while (current & 0xFFFF) {
            uint32_t pos = NUMTRAILINGZEROS((uint32_t)current);
            uint32_t odd = (uint32_t)current >> pos;
            int32_t delta = odd & mask;

            assert(position >= 0);
            if (odd & (1 << (table_bits + 1)))
                delta -= (1 << (table_bits + 1));
            current -= delta * (1 << pos);
            control[position].power = pos + 16 * (w - 1);
            control[position].addend = delta;
            position--;
        }
        current >>= 16;
    }
    assert(current == 0);

    position++;
    n = table_size - position;
    for (i = 0; i < n; i++)
        control[i] = control[i + position];

    return n - 1;
}

static void prepare_wnaf_table(pniels_t * output,
                               const curve448_point_t working,
                               unsigned int tbits)
{
    curve448_point_t tmp;
    int i;
    pniels_t twop;

    pt_to_pniels(output[0], working);

    if (tbits == 0)
        return;

    curve448_point_double(tmp, working);
    pt_to_pniels(twop, tmp);

    add_pniels_to_pt(tmp, output[0], 0);
    pt_to_pniels(output[1], tmp);

    for (i = 2; i < 1 << tbits; i++) {
        add_pniels_to_pt(tmp, twop, 0);
        pt_to_pniels(output[i], tmp);
    }

    curve448_point_destroy(tmp);
    OPENSSL_cleanse(twop, sizeof(twop));
}

void curve448_base_double_scalarmul_non_secret(curve448_point_t combo,
                                               const curve448_scalar_t scalar1,
                                               const curve448_point_t base2,
                                               const curve448_scalar_t scalar2)
{
    const int table_bits_var = C448_WNAF_VAR_TABLE_BITS;
    const int table_bits_pre = C448_WNAF_FIXED_TABLE_BITS;
    struct smvt_control control_var[C448_SCALAR_BITS /
                                    (C448_WNAF_VAR_TABLE_BITS + 1) + 3];
    struct smvt_control control_pre[C448_SCALAR_BITS /
                                    (C448_WNAF_FIXED_TABLE_BITS + 1) + 3];
    int ncb_pre = recode_wnaf(control_pre, scalar1, table_bits_pre);
    int ncb_var = recode_wnaf(control_var, scalar2, table_bits_var);
    pniels_t precmp_var[1 << C448_WNAF_VAR_TABLE_BITS];
    int contp = 0, contv = 0, i;

    prepare_wnaf_table(precmp_var, base2, table_bits_var);
    i = control_var[0].power;

    if (i < 0) {
        curve448_point_copy(combo, curve448_point_identity);
        return;
    }
    if (i > control_pre[0].power) {
        pniels_to_pt(combo, precmp_var[control_var[0].addend >> 1]);
        contv++;
    } else if (i == control_pre[0].power && i >= 0) {
        pniels_to_pt(combo, precmp_var[control_var[0].addend >> 1]);
        add_niels_to_pt(combo, curve448_wnaf_base[control_pre[0].addend >> 1],
                        i);
        contv++;
        contp++;
    } else {
        i = control_pre[0].power;
        niels_to_pt(combo, curve448_wnaf_base[control_pre[0].addend >> 1]);
        contp++;
    }

    for (i--; i >= 0; i--) {
        int cv = (i == control_var[contv].power);
        int cp = (i == control_pre[contp].power);

        point_double_internal(combo, combo, i && !(cv || cp));

        if (cv) {
            assert(control_var[contv].addend);

            if (control_var[contv].addend > 0)
                add_pniels_to_pt(combo,
                                 precmp_var[control_var[contv].addend >> 1],
                                 i && !cp);
            else
                sub_pniels_from_pt(combo,
                                   precmp_var[(-control_var[contv].addend)
                                              >> 1], i && !cp);
            contv++;
        }

        if (cp) {
            assert(control_pre[contp].addend);

            if (control_pre[contp].addend > 0)
                add_niels_to_pt(combo,
                                curve448_wnaf_base[control_pre[contp].addend
                                                   >> 1], i);
            else
                sub_niels_from_pt(combo,
                                  curve448_wnaf_base[(-control_pre
                                                      [contp].addend) >> 1], i);
            contp++;
        }
    }

    /* This function is non-secret, but whatever this is cheap. */
    OPENSSL_cleanse(control_var, sizeof(control_var));
    OPENSSL_cleanse(control_pre, sizeof(control_pre));
    OPENSSL_cleanse(precmp_var, sizeof(precmp_var));

    assert(contv == ncb_var);
    (void)ncb_var;
    assert(contp == ncb_pre);
    (void)ncb_pre;
}

void curve448_point_destroy(curve448_point_t point)
{
    OPENSSL_cleanse(point, sizeof(curve448_point_t));
}

int X448(uint8_t out_shared_key[56], const uint8_t private_key[56],
         const uint8_t peer_public_value[56])
{
    return x448_int(out_shared_key, peer_public_value, private_key)
           == C448_SUCCESS;
}

void X448_public_from_private(uint8_t out_public_value[56],
                              const uint8_t private_key[56])
{
    x448_derive_public_key(out_public_value, private_key);
}
