/*
 * Copyright 2001-2019 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/err.h>
#include <openssl/symhacks.h>

#include "ec_lcl.h"

const EC_METHOD *EC_GFp_simple_method(void)
{
    static const EC_METHOD ret = {
        EC_FLAGS_DEFAULT_OCT,
        NID_X9_62_prime_field,
        ec_GFp_simple_group_init,
        ec_GFp_simple_group_finish,
        ec_GFp_simple_group_clear_finish,
        ec_GFp_simple_group_copy,
        ec_GFp_simple_group_set_curve,
        ec_GFp_simple_group_get_curve,
        ec_GFp_simple_group_get_degree,
        ec_group_simple_order_bits,
        ec_GFp_simple_group_check_discriminant,
        ec_GFp_simple_point_init,
        ec_GFp_simple_point_finish,
        ec_GFp_simple_point_clear_finish,
        ec_GFp_simple_point_copy,
        ec_GFp_simple_point_set_to_infinity,
        ec_GFp_simple_set_Jprojective_coordinates_GFp,
        ec_GFp_simple_get_Jprojective_coordinates_GFp,
        ec_GFp_simple_point_set_affine_coordinates,
        ec_GFp_simple_point_get_affine_coordinates,
        0, 0, 0,
        ec_GFp_simple_add,
        ec_GFp_simple_dbl,
        ec_GFp_simple_invert,
        ec_GFp_simple_is_at_infinity,
        ec_GFp_simple_is_on_curve,
        ec_GFp_simple_cmp,
        ec_GFp_simple_make_affine,
        ec_GFp_simple_points_make_affine,
        0 /* mul */ ,
        0 /* precompute_mult */ ,
        0 /* have_precompute_mult */ ,
        ec_GFp_simple_field_mul,
        ec_GFp_simple_field_sqr,
        0 /* field_div */ ,
        ec_GFp_simple_field_inv,
        0 /* field_encode */ ,
        0 /* field_decode */ ,
        0,                      /* field_set_to_one */
        ec_key_simple_priv2oct,
        ec_key_simple_oct2priv,
        0, /* set private */
        ec_key_simple_generate_key,
        ec_key_simple_check_key,
        ec_key_simple_generate_public_key,
        0, /* keycopy */
        0, /* keyfinish */
        ecdh_simple_compute_key,
        0, /* field_inverse_mod_ord */
        ec_GFp_simple_blind_coordinates,
        ec_GFp_simple_ladder_pre,
        ec_GFp_simple_ladder_step,
        ec_GFp_simple_ladder_post
    };

    return &ret;
}

/*
 * Most method functions in this file are designed to work with
 * non-trivial representations of field elements if necessary
 * (see ecp_mont.c): while standard modular addition and subtraction
 * are used, the field_mul and field_sqr methods will be used for
 * multiplication, and field_encode and field_decode (if defined)
 * will be used for converting between representations.
 *
 * Functions ec_GFp_simple_points_make_affine() and
 * ec_GFp_simple_point_get_affine_coordinates() specifically assume
 * that if a non-trivial representation is used, it is a Montgomery
 * representation (i.e. 'encoding' means multiplying by some factor R).
 */

int ec_GFp_simple_group_init(EC_GROUP *group)
{
    group->field = BN_new();
    group->a = BN_new();
    group->b = BN_new();
    if (group->field == NULL || group->a == NULL || group->b == NULL) {
        BN_free(group->field);
        BN_free(group->a);
        BN_free(group->b);
        return 0;
    }
    group->a_is_minus3 = 0;
    return 1;
}

void ec_GFp_simple_group_finish(EC_GROUP *group)
{
    BN_free(group->field);
    BN_free(group->a);
    BN_free(group->b);
}

void ec_GFp_simple_group_clear_finish(EC_GROUP *group)
{
    BN_clear_free(group->field);
    BN_clear_free(group->a);
    BN_clear_free(group->b);
}

int ec_GFp_simple_group_copy(EC_GROUP *dest, const EC_GROUP *src)
{
    if (!BN_copy(dest->field, src->field))
        return 0;
    if (!BN_copy(dest->a, src->a))
        return 0;
    if (!BN_copy(dest->b, src->b))
        return 0;

    dest->a_is_minus3 = src->a_is_minus3;

    return 1;
}

int ec_GFp_simple_group_set_curve(EC_GROUP *group,
                                  const BIGNUM *p, const BIGNUM *a,
                                  const BIGNUM *b, BN_CTX *ctx)
{
    int ret = 0;
    BN_CTX *new_ctx = NULL;
    BIGNUM *tmp_a;

    /* p must be a prime > 3 */
    if (BN_num_bits(p) <= 2 || !BN_is_odd(p)) {
        ECerr(EC_F_EC_GFP_SIMPLE_GROUP_SET_CURVE, EC_R_INVALID_FIELD);
        return 0;
    }

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    BN_CTX_start(ctx);
    tmp_a = BN_CTX_get(ctx);
    if (tmp_a == NULL)
        goto err;

    /* group->field */
    if (!BN_copy(group->field, p))
        goto err;
    BN_set_negative(group->field, 0);

    /* group->a */
    if (!BN_nnmod(tmp_a, a, p, ctx))
        goto err;
    if (group->meth->field_encode) {
        if (!group->meth->field_encode(group, group->a, tmp_a, ctx))
            goto err;
    } else if (!BN_copy(group->a, tmp_a))
        goto err;

    /* group->b */
    if (!BN_nnmod(group->b, b, p, ctx))
        goto err;
    if (group->meth->field_encode)
        if (!group->meth->field_encode(group, group->b, group->b, ctx))
            goto err;

    /* group->a_is_minus3 */
    if (!BN_add_word(tmp_a, 3))
        goto err;
    group->a_is_minus3 = (0 == BN_cmp(tmp_a, group->field));

    ret = 1;

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_group_get_curve(const EC_GROUP *group, BIGNUM *p, BIGNUM *a,
                                  BIGNUM *b, BN_CTX *ctx)
{
    int ret = 0;
    BN_CTX *new_ctx = NULL;

    if (p != NULL) {
        if (!BN_copy(p, group->field))
            return 0;
    }

    if (a != NULL || b != NULL) {
        if (group->meth->field_decode) {
            if (ctx == NULL) {
                ctx = new_ctx = BN_CTX_new();
                if (ctx == NULL)
                    return 0;
            }
            if (a != NULL) {
                if (!group->meth->field_decode(group, a, group->a, ctx))
                    goto err;
            }
            if (b != NULL) {
                if (!group->meth->field_decode(group, b, group->b, ctx))
                    goto err;
            }
        } else {
            if (a != NULL) {
                if (!BN_copy(a, group->a))
                    goto err;
            }
            if (b != NULL) {
                if (!BN_copy(b, group->b))
                    goto err;
            }
        }
    }

    ret = 1;

 err:
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_group_get_degree(const EC_GROUP *group)
{
    return BN_num_bits(group->field);
}

int ec_GFp_simple_group_check_discriminant(const EC_GROUP *group, BN_CTX *ctx)
{
    int ret = 0;
    BIGNUM *a, *b, *order, *tmp_1, *tmp_2;
    const BIGNUM *p = group->field;
    BN_CTX *new_ctx = NULL;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL) {
            ECerr(EC_F_EC_GFP_SIMPLE_GROUP_CHECK_DISCRIMINANT,
                  ERR_R_MALLOC_FAILURE);
            goto err;
        }
    }
    BN_CTX_start(ctx);
    a = BN_CTX_get(ctx);
    b = BN_CTX_get(ctx);
    tmp_1 = BN_CTX_get(ctx);
    tmp_2 = BN_CTX_get(ctx);
    order = BN_CTX_get(ctx);
    if (order == NULL)
        goto err;

    if (group->meth->field_decode) {
        if (!group->meth->field_decode(group, a, group->a, ctx))
            goto err;
        if (!group->meth->field_decode(group, b, group->b, ctx))
            goto err;
    } else {
        if (!BN_copy(a, group->a))
            goto err;
        if (!BN_copy(b, group->b))
            goto err;
    }

    /*-
     * check the discriminant:
     * y^2 = x^3 + a*x + b is an elliptic curve <=> 4*a^3 + 27*b^2 != 0 (mod p)
     * 0 =< a, b < p
     */
    if (BN_is_zero(a)) {
        if (BN_is_zero(b))
            goto err;
    } else if (!BN_is_zero(b)) {
        if (!BN_mod_sqr(tmp_1, a, p, ctx))
            goto err;
        if (!BN_mod_mul(tmp_2, tmp_1, a, p, ctx))
            goto err;
        if (!BN_lshift(tmp_1, tmp_2, 2))
            goto err;
        /* tmp_1 = 4*a^3 */

        if (!BN_mod_sqr(tmp_2, b, p, ctx))
            goto err;
        if (!BN_mul_word(tmp_2, 27))
            goto err;
        /* tmp_2 = 27*b^2 */

        if (!BN_mod_add(a, tmp_1, tmp_2, p, ctx))
            goto err;
        if (BN_is_zero(a))
            goto err;
    }
    ret = 1;

 err:
    if (ctx != NULL)
        BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_point_init(EC_POINT *point)
{
    point->X = BN_new();
    point->Y = BN_new();
    point->Z = BN_new();
    point->Z_is_one = 0;

    if (point->X == NULL || point->Y == NULL || point->Z == NULL) {
        BN_free(point->X);
        BN_free(point->Y);
        BN_free(point->Z);
        return 0;
    }
    return 1;
}

void ec_GFp_simple_point_finish(EC_POINT *point)
{
    BN_free(point->X);
    BN_free(point->Y);
    BN_free(point->Z);
}

void ec_GFp_simple_point_clear_finish(EC_POINT *point)
{
    BN_clear_free(point->X);
    BN_clear_free(point->Y);
    BN_clear_free(point->Z);
    point->Z_is_one = 0;
}

int ec_GFp_simple_point_copy(EC_POINT *dest, const EC_POINT *src)
{
    if (!BN_copy(dest->X, src->X))
        return 0;
    if (!BN_copy(dest->Y, src->Y))
        return 0;
    if (!BN_copy(dest->Z, src->Z))
        return 0;
    dest->Z_is_one = src->Z_is_one;
    dest->curve_name = src->curve_name;

    return 1;
}

int ec_GFp_simple_point_set_to_infinity(const EC_GROUP *group,
                                        EC_POINT *point)
{
    point->Z_is_one = 0;
    BN_zero(point->Z);
    return 1;
}

int ec_GFp_simple_set_Jprojective_coordinates_GFp(const EC_GROUP *group,
                                                  EC_POINT *point,
                                                  const BIGNUM *x,
                                                  const BIGNUM *y,
                                                  const BIGNUM *z,
                                                  BN_CTX *ctx)
{
    BN_CTX *new_ctx = NULL;
    int ret = 0;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    if (x != NULL) {
        if (!BN_nnmod(point->X, x, group->field, ctx))
            goto err;
        if (group->meth->field_encode) {
            if (!group->meth->field_encode(group, point->X, point->X, ctx))
                goto err;
        }
    }

    if (y != NULL) {
        if (!BN_nnmod(point->Y, y, group->field, ctx))
            goto err;
        if (group->meth->field_encode) {
            if (!group->meth->field_encode(group, point->Y, point->Y, ctx))
                goto err;
        }
    }

    if (z != NULL) {
        int Z_is_one;

        if (!BN_nnmod(point->Z, z, group->field, ctx))
            goto err;
        Z_is_one = BN_is_one(point->Z);
        if (group->meth->field_encode) {
            if (Z_is_one && (group->meth->field_set_to_one != 0)) {
                if (!group->meth->field_set_to_one(group, point->Z, ctx))
                    goto err;
            } else {
                if (!group->
                    meth->field_encode(group, point->Z, point->Z, ctx))
                    goto err;
            }
        }
        point->Z_is_one = Z_is_one;
    }

    ret = 1;

 err:
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_get_Jprojective_coordinates_GFp(const EC_GROUP *group,
                                                  const EC_POINT *point,
                                                  BIGNUM *x, BIGNUM *y,
                                                  BIGNUM *z, BN_CTX *ctx)
{
    BN_CTX *new_ctx = NULL;
    int ret = 0;

    if (group->meth->field_decode != 0) {
        if (ctx == NULL) {
            ctx = new_ctx = BN_CTX_new();
            if (ctx == NULL)
                return 0;
        }

        if (x != NULL) {
            if (!group->meth->field_decode(group, x, point->X, ctx))
                goto err;
        }
        if (y != NULL) {
            if (!group->meth->field_decode(group, y, point->Y, ctx))
                goto err;
        }
        if (z != NULL) {
            if (!group->meth->field_decode(group, z, point->Z, ctx))
                goto err;
        }
    } else {
        if (x != NULL) {
            if (!BN_copy(x, point->X))
                goto err;
        }
        if (y != NULL) {
            if (!BN_copy(y, point->Y))
                goto err;
        }
        if (z != NULL) {
            if (!BN_copy(z, point->Z))
                goto err;
        }
    }

    ret = 1;

 err:
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_point_set_affine_coordinates(const EC_GROUP *group,
                                               EC_POINT *point,
                                               const BIGNUM *x,
                                               const BIGNUM *y, BN_CTX *ctx)
{
    if (x == NULL || y == NULL) {
        /*
         * unlike for projective coordinates, we do not tolerate this
         */
        ECerr(EC_F_EC_GFP_SIMPLE_POINT_SET_AFFINE_COORDINATES,
              ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }

    return EC_POINT_set_Jprojective_coordinates_GFp(group, point, x, y,
                                                    BN_value_one(), ctx);
}

int ec_GFp_simple_point_get_affine_coordinates(const EC_GROUP *group,
                                               const EC_POINT *point,
                                               BIGNUM *x, BIGNUM *y,
                                               BN_CTX *ctx)
{
    BN_CTX *new_ctx = NULL;
    BIGNUM *Z, *Z_1, *Z_2, *Z_3;
    const BIGNUM *Z_;
    int ret = 0;

    if (EC_POINT_is_at_infinity(group, point)) {
        ECerr(EC_F_EC_GFP_SIMPLE_POINT_GET_AFFINE_COORDINATES,
              EC_R_POINT_AT_INFINITY);
        return 0;
    }

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    BN_CTX_start(ctx);
    Z = BN_CTX_get(ctx);
    Z_1 = BN_CTX_get(ctx);
    Z_2 = BN_CTX_get(ctx);
    Z_3 = BN_CTX_get(ctx);
    if (Z_3 == NULL)
        goto err;

    /* transform  (X, Y, Z)  into  (x, y) := (X/Z^2, Y/Z^3) */

    if (group->meth->field_decode) {
        if (!group->meth->field_decode(group, Z, point->Z, ctx))
            goto err;
        Z_ = Z;
    } else {
        Z_ = point->Z;
    }

    if (BN_is_one(Z_)) {
        if (group->meth->field_decode) {
            if (x != NULL) {
                if (!group->meth->field_decode(group, x, point->X, ctx))
                    goto err;
            }
            if (y != NULL) {
                if (!group->meth->field_decode(group, y, point->Y, ctx))
                    goto err;
            }
        } else {
            if (x != NULL) {
                if (!BN_copy(x, point->X))
                    goto err;
            }
            if (y != NULL) {
                if (!BN_copy(y, point->Y))
                    goto err;
            }
        }
    } else {
        if (!group->meth->field_inv(group, Z_1, Z_, ctx)) {
            ECerr(EC_F_EC_GFP_SIMPLE_POINT_GET_AFFINE_COORDINATES,
                  ERR_R_BN_LIB);
            goto err;
        }

        if (group->meth->field_encode == 0) {
            /* field_sqr works on standard representation */
            if (!group->meth->field_sqr(group, Z_2, Z_1, ctx))
                goto err;
        } else {
            if (!BN_mod_sqr(Z_2, Z_1, group->field, ctx))
                goto err;
        }

        if (x != NULL) {
            /*
             * in the Montgomery case, field_mul will cancel out Montgomery
             * factor in X:
             */
            if (!group->meth->field_mul(group, x, point->X, Z_2, ctx))
                goto err;
        }

        if (y != NULL) {
            if (group->meth->field_encode == 0) {
                /*
                 * field_mul works on standard representation
                 */
                if (!group->meth->field_mul(group, Z_3, Z_2, Z_1, ctx))
                    goto err;
            } else {
                if (!BN_mod_mul(Z_3, Z_2, Z_1, group->field, ctx))
                    goto err;
            }

            /*
             * in the Montgomery case, field_mul will cancel out Montgomery
             * factor in Y:
             */
            if (!group->meth->field_mul(group, y, point->Y, Z_3, ctx))
                goto err;
        }
    }

    ret = 1;

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_add(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
                      const EC_POINT *b, BN_CTX *ctx)
{
    int (*field_mul) (const EC_GROUP *, BIGNUM *, const BIGNUM *,
                      const BIGNUM *, BN_CTX *);
    int (*field_sqr) (const EC_GROUP *, BIGNUM *, const BIGNUM *, BN_CTX *);
    const BIGNUM *p;
    BN_CTX *new_ctx = NULL;
    BIGNUM *n0, *n1, *n2, *n3, *n4, *n5, *n6;
    int ret = 0;

    if (a == b)
        return EC_POINT_dbl(group, r, a, ctx);
    if (EC_POINT_is_at_infinity(group, a))
        return EC_POINT_copy(r, b);
    if (EC_POINT_is_at_infinity(group, b))
        return EC_POINT_copy(r, a);

    field_mul = group->meth->field_mul;
    field_sqr = group->meth->field_sqr;
    p = group->field;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    BN_CTX_start(ctx);
    n0 = BN_CTX_get(ctx);
    n1 = BN_CTX_get(ctx);
    n2 = BN_CTX_get(ctx);
    n3 = BN_CTX_get(ctx);
    n4 = BN_CTX_get(ctx);
    n5 = BN_CTX_get(ctx);
    n6 = BN_CTX_get(ctx);
    if (n6 == NULL)
        goto end;

    /*
     * Note that in this function we must not read components of 'a' or 'b'
     * once we have written the corresponding components of 'r'. ('r' might
     * be one of 'a' or 'b'.)
     */

    /* n1, n2 */
    if (b->Z_is_one) {
        if (!BN_copy(n1, a->X))
            goto end;
        if (!BN_copy(n2, a->Y))
            goto end;
        /* n1 = X_a */
        /* n2 = Y_a */
    } else {
        if (!field_sqr(group, n0, b->Z, ctx))
            goto end;
        if (!field_mul(group, n1, a->X, n0, ctx))
            goto end;
        /* n1 = X_a * Z_b^2 */

        if (!field_mul(group, n0, n0, b->Z, ctx))
            goto end;
        if (!field_mul(group, n2, a->Y, n0, ctx))
            goto end;
        /* n2 = Y_a * Z_b^3 */
    }

    /* n3, n4 */
    if (a->Z_is_one) {
        if (!BN_copy(n3, b->X))
            goto end;
        if (!BN_copy(n4, b->Y))
            goto end;
        /* n3 = X_b */
        /* n4 = Y_b */
    } else {
        if (!field_sqr(group, n0, a->Z, ctx))
            goto end;
        if (!field_mul(group, n3, b->X, n0, ctx))
            goto end;
        /* n3 = X_b * Z_a^2 */

        if (!field_mul(group, n0, n0, a->Z, ctx))
            goto end;
        if (!field_mul(group, n4, b->Y, n0, ctx))
            goto end;
        /* n4 = Y_b * Z_a^3 */
    }

    /* n5, n6 */
    if (!BN_mod_sub_quick(n5, n1, n3, p))
        goto end;
    if (!BN_mod_sub_quick(n6, n2, n4, p))
        goto end;
    /* n5 = n1 - n3 */
    /* n6 = n2 - n4 */

    if (BN_is_zero(n5)) {
        if (BN_is_zero(n6)) {
            /* a is the same point as b */
            BN_CTX_end(ctx);
            ret = EC_POINT_dbl(group, r, a, ctx);
            ctx = NULL;
            goto end;
        } else {
            /* a is the inverse of b */
            BN_zero(r->Z);
            r->Z_is_one = 0;
            ret = 1;
            goto end;
        }
    }

    /* 'n7', 'n8' */
    if (!BN_mod_add_quick(n1, n1, n3, p))
        goto end;
    if (!BN_mod_add_quick(n2, n2, n4, p))
        goto end;
    /* 'n7' = n1 + n3 */
    /* 'n8' = n2 + n4 */

    /* Z_r */
    if (a->Z_is_one && b->Z_is_one) {
        if (!BN_copy(r->Z, n5))
            goto end;
    } else {
        if (a->Z_is_one) {
            if (!BN_copy(n0, b->Z))
                goto end;
        } else if (b->Z_is_one) {
            if (!BN_copy(n0, a->Z))
                goto end;
        } else {
            if (!field_mul(group, n0, a->Z, b->Z, ctx))
                goto end;
        }
        if (!field_mul(group, r->Z, n0, n5, ctx))
            goto end;
    }
    r->Z_is_one = 0;
    /* Z_r = Z_a * Z_b * n5 */

    /* X_r */
    if (!field_sqr(group, n0, n6, ctx))
        goto end;
    if (!field_sqr(group, n4, n5, ctx))
        goto end;
    if (!field_mul(group, n3, n1, n4, ctx))
        goto end;
    if (!BN_mod_sub_quick(r->X, n0, n3, p))
        goto end;
    /* X_r = n6^2 - n5^2 * 'n7' */

    /* 'n9' */
    if (!BN_mod_lshift1_quick(n0, r->X, p))
        goto end;
    if (!BN_mod_sub_quick(n0, n3, n0, p))
        goto end;
    /* n9 = n5^2 * 'n7' - 2 * X_r */

    /* Y_r */
    if (!field_mul(group, n0, n0, n6, ctx))
        goto end;
    if (!field_mul(group, n5, n4, n5, ctx))
        goto end;               /* now n5 is n5^3 */
    if (!field_mul(group, n1, n2, n5, ctx))
        goto end;
    if (!BN_mod_sub_quick(n0, n0, n1, p))
        goto end;
    if (BN_is_odd(n0))
        if (!BN_add(n0, n0, p))
            goto end;
    /* now  0 <= n0 < 2*p,  and n0 is even */
    if (!BN_rshift1(r->Y, n0))
        goto end;
    /* Y_r = (n6 * 'n9' - 'n8' * 'n5^3') / 2 */

    ret = 1;

 end:
    if (ctx)                    /* otherwise we already called BN_CTX_end */
        BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_dbl(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
                      BN_CTX *ctx)
{
    int (*field_mul) (const EC_GROUP *, BIGNUM *, const BIGNUM *,
                      const BIGNUM *, BN_CTX *);
    int (*field_sqr) (const EC_GROUP *, BIGNUM *, const BIGNUM *, BN_CTX *);
    const BIGNUM *p;
    BN_CTX *new_ctx = NULL;
    BIGNUM *n0, *n1, *n2, *n3;
    int ret = 0;

    if (EC_POINT_is_at_infinity(group, a)) {
        BN_zero(r->Z);
        r->Z_is_one = 0;
        return 1;
    }

    field_mul = group->meth->field_mul;
    field_sqr = group->meth->field_sqr;
    p = group->field;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    BN_CTX_start(ctx);
    n0 = BN_CTX_get(ctx);
    n1 = BN_CTX_get(ctx);
    n2 = BN_CTX_get(ctx);
    n3 = BN_CTX_get(ctx);
    if (n3 == NULL)
        goto err;

    /*
     * Note that in this function we must not read components of 'a' once we
     * have written the corresponding components of 'r'. ('r' might the same
     * as 'a'.)
     */

    /* n1 */
    if (a->Z_is_one) {
        if (!field_sqr(group, n0, a->X, ctx))
            goto err;
        if (!BN_mod_lshift1_quick(n1, n0, p))
            goto err;
        if (!BN_mod_add_quick(n0, n0, n1, p))
            goto err;
        if (!BN_mod_add_quick(n1, n0, group->a, p))
            goto err;
        /* n1 = 3 * X_a^2 + a_curve */
    } else if (group->a_is_minus3) {
        if (!field_sqr(group, n1, a->Z, ctx))
            goto err;
        if (!BN_mod_add_quick(n0, a->X, n1, p))
            goto err;
        if (!BN_mod_sub_quick(n2, a->X, n1, p))
            goto err;
        if (!field_mul(group, n1, n0, n2, ctx))
            goto err;
        if (!BN_mod_lshift1_quick(n0, n1, p))
            goto err;
        if (!BN_mod_add_quick(n1, n0, n1, p))
            goto err;
        /*-
         * n1 = 3 * (X_a + Z_a^2) * (X_a - Z_a^2)
         *    = 3 * X_a^2 - 3 * Z_a^4
         */
    } else {
        if (!field_sqr(group, n0, a->X, ctx))
            goto err;
        if (!BN_mod_lshift1_quick(n1, n0, p))
            goto err;
        if (!BN_mod_add_quick(n0, n0, n1, p))
            goto err;
        if (!field_sqr(group, n1, a->Z, ctx))
            goto err;
        if (!field_sqr(group, n1, n1, ctx))
            goto err;
        if (!field_mul(group, n1, n1, group->a, ctx))
            goto err;
        if (!BN_mod_add_quick(n1, n1, n0, p))
            goto err;
        /* n1 = 3 * X_a^2 + a_curve * Z_a^4 */
    }

    /* Z_r */
    if (a->Z_is_one) {
        if (!BN_copy(n0, a->Y))
            goto err;
    } else {
        if (!field_mul(group, n0, a->Y, a->Z, ctx))
            goto err;
    }
    if (!BN_mod_lshift1_quick(r->Z, n0, p))
        goto err;
    r->Z_is_one = 0;
    /* Z_r = 2 * Y_a * Z_a */

    /* n2 */
    if (!field_sqr(group, n3, a->Y, ctx))
        goto err;
    if (!field_mul(group, n2, a->X, n3, ctx))
        goto err;
    if (!BN_mod_lshift_quick(n2, n2, 2, p))
        goto err;
    /* n2 = 4 * X_a * Y_a^2 */

    /* X_r */
    if (!BN_mod_lshift1_quick(n0, n2, p))
        goto err;
    if (!field_sqr(group, r->X, n1, ctx))
        goto err;
    if (!BN_mod_sub_quick(r->X, r->X, n0, p))
        goto err;
    /* X_r = n1^2 - 2 * n2 */

    /* n3 */
    if (!field_sqr(group, n0, n3, ctx))
        goto err;
    if (!BN_mod_lshift_quick(n3, n0, 3, p))
        goto err;
    /* n3 = 8 * Y_a^4 */

    /* Y_r */
    if (!BN_mod_sub_quick(n0, n2, r->X, p))
        goto err;
    if (!field_mul(group, n0, n1, n0, ctx))
        goto err;
    if (!BN_mod_sub_quick(r->Y, n0, n3, p))
        goto err;
    /* Y_r = n1 * (n2 - X_r) - n3 */

    ret = 1;

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_invert(const EC_GROUP *group, EC_POINT *point, BN_CTX *ctx)
{
    if (EC_POINT_is_at_infinity(group, point) || BN_is_zero(point->Y))
        /* point is its own inverse */
        return 1;

    return BN_usub(point->Y, group->field, point->Y);
}

int ec_GFp_simple_is_at_infinity(const EC_GROUP *group, const EC_POINT *point)
{
    return BN_is_zero(point->Z);
}

int ec_GFp_simple_is_on_curve(const EC_GROUP *group, const EC_POINT *point,
                              BN_CTX *ctx)
{
    int (*field_mul) (const EC_GROUP *, BIGNUM *, const BIGNUM *,
                      const BIGNUM *, BN_CTX *);
    int (*field_sqr) (const EC_GROUP *, BIGNUM *, const BIGNUM *, BN_CTX *);
    const BIGNUM *p;
    BN_CTX *new_ctx = NULL;
    BIGNUM *rh, *tmp, *Z4, *Z6;
    int ret = -1;

    if (EC_POINT_is_at_infinity(group, point))
        return 1;

    field_mul = group->meth->field_mul;
    field_sqr = group->meth->field_sqr;
    p = group->field;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return -1;
    }

    BN_CTX_start(ctx);
    rh = BN_CTX_get(ctx);
    tmp = BN_CTX_get(ctx);
    Z4 = BN_CTX_get(ctx);
    Z6 = BN_CTX_get(ctx);
    if (Z6 == NULL)
        goto err;

    /*-
     * We have a curve defined by a Weierstrass equation
     *      y^2 = x^3 + a*x + b.
     * The point to consider is given in Jacobian projective coordinates
     * where  (X, Y, Z)  represents  (x, y) = (X/Z^2, Y/Z^3).
     * Substituting this and multiplying by  Z^6  transforms the above equation into
     *      Y^2 = X^3 + a*X*Z^4 + b*Z^6.
     * To test this, we add up the right-hand side in 'rh'.
     */

    /* rh := X^2 */
    if (!field_sqr(group, rh, point->X, ctx))
        goto err;

    if (!point->Z_is_one) {
        if (!field_sqr(group, tmp, point->Z, ctx))
            goto err;
        if (!field_sqr(group, Z4, tmp, ctx))
            goto err;
        if (!field_mul(group, Z6, Z4, tmp, ctx))
            goto err;

        /* rh := (rh + a*Z^4)*X */
        if (group->a_is_minus3) {
            if (!BN_mod_lshift1_quick(tmp, Z4, p))
                goto err;
            if (!BN_mod_add_quick(tmp, tmp, Z4, p))
                goto err;
            if (!BN_mod_sub_quick(rh, rh, tmp, p))
                goto err;
            if (!field_mul(group, rh, rh, point->X, ctx))
                goto err;
        } else {
            if (!field_mul(group, tmp, Z4, group->a, ctx))
                goto err;
            if (!BN_mod_add_quick(rh, rh, tmp, p))
                goto err;
            if (!field_mul(group, rh, rh, point->X, ctx))
                goto err;
        }

        /* rh := rh + b*Z^6 */
        if (!field_mul(group, tmp, group->b, Z6, ctx))
            goto err;
        if (!BN_mod_add_quick(rh, rh, tmp, p))
            goto err;
    } else {
        /* point->Z_is_one */

        /* rh := (rh + a)*X */
        if (!BN_mod_add_quick(rh, rh, group->a, p))
            goto err;
        if (!field_mul(group, rh, rh, point->X, ctx))
            goto err;
        /* rh := rh + b */
        if (!BN_mod_add_quick(rh, rh, group->b, p))
            goto err;
    }

    /* 'lh' := Y^2 */
    if (!field_sqr(group, tmp, point->Y, ctx))
        goto err;

    ret = (0 == BN_ucmp(tmp, rh));

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_cmp(const EC_GROUP *group, const EC_POINT *a,
                      const EC_POINT *b, BN_CTX *ctx)
{
    /*-
     * return values:
     *  -1   error
     *   0   equal (in affine coordinates)
     *   1   not equal
     */

    int (*field_mul) (const EC_GROUP *, BIGNUM *, const BIGNUM *,
                      const BIGNUM *, BN_CTX *);
    int (*field_sqr) (const EC_GROUP *, BIGNUM *, const BIGNUM *, BN_CTX *);
    BN_CTX *new_ctx = NULL;
    BIGNUM *tmp1, *tmp2, *Za23, *Zb23;
    const BIGNUM *tmp1_, *tmp2_;
    int ret = -1;

    if (EC_POINT_is_at_infinity(group, a)) {
        return EC_POINT_is_at_infinity(group, b) ? 0 : 1;
    }

    if (EC_POINT_is_at_infinity(group, b))
        return 1;

    if (a->Z_is_one && b->Z_is_one) {
        return ((BN_cmp(a->X, b->X) == 0) && BN_cmp(a->Y, b->Y) == 0) ? 0 : 1;
    }

    field_mul = group->meth->field_mul;
    field_sqr = group->meth->field_sqr;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return -1;
    }

    BN_CTX_start(ctx);
    tmp1 = BN_CTX_get(ctx);
    tmp2 = BN_CTX_get(ctx);
    Za23 = BN_CTX_get(ctx);
    Zb23 = BN_CTX_get(ctx);
    if (Zb23 == NULL)
        goto end;

    /*-
     * We have to decide whether
     *     (X_a/Z_a^2, Y_a/Z_a^3) = (X_b/Z_b^2, Y_b/Z_b^3),
     * or equivalently, whether
     *     (X_a*Z_b^2, Y_a*Z_b^3) = (X_b*Z_a^2, Y_b*Z_a^3).
     */

    if (!b->Z_is_one) {
        if (!field_sqr(group, Zb23, b->Z, ctx))
            goto end;
        if (!field_mul(group, tmp1, a->X, Zb23, ctx))
            goto end;
        tmp1_ = tmp1;
    } else
        tmp1_ = a->X;
    if (!a->Z_is_one) {
        if (!field_sqr(group, Za23, a->Z, ctx))
            goto end;
        if (!field_mul(group, tmp2, b->X, Za23, ctx))
            goto end;
        tmp2_ = tmp2;
    } else
        tmp2_ = b->X;

    /* compare  X_a*Z_b^2  with  X_b*Z_a^2 */
    if (BN_cmp(tmp1_, tmp2_) != 0) {
        ret = 1;                /* points differ */
        goto end;
    }

    if (!b->Z_is_one) {
        if (!field_mul(group, Zb23, Zb23, b->Z, ctx))
            goto end;
        if (!field_mul(group, tmp1, a->Y, Zb23, ctx))
            goto end;
        /* tmp1_ = tmp1 */
    } else
        tmp1_ = a->Y;
    if (!a->Z_is_one) {
        if (!field_mul(group, Za23, Za23, a->Z, ctx))
            goto end;
        if (!field_mul(group, tmp2, b->Y, Za23, ctx))
            goto end;
        /* tmp2_ = tmp2 */
    } else
        tmp2_ = b->Y;

    /* compare  Y_a*Z_b^3  with  Y_b*Z_a^3 */
    if (BN_cmp(tmp1_, tmp2_) != 0) {
        ret = 1;                /* points differ */
        goto end;
    }

    /* points are equal */
    ret = 0;

 end:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_make_affine(const EC_GROUP *group, EC_POINT *point,
                              BN_CTX *ctx)
{
    BN_CTX *new_ctx = NULL;
    BIGNUM *x, *y;
    int ret = 0;

    if (point->Z_is_one || EC_POINT_is_at_infinity(group, point))
        return 1;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    BN_CTX_start(ctx);
    x = BN_CTX_get(ctx);
    y = BN_CTX_get(ctx);
    if (y == NULL)
        goto err;

    if (!EC_POINT_get_affine_coordinates(group, point, x, y, ctx))
        goto err;
    if (!EC_POINT_set_affine_coordinates(group, point, x, y, ctx))
        goto err;
    if (!point->Z_is_one) {
        ECerr(EC_F_EC_GFP_SIMPLE_MAKE_AFFINE, ERR_R_INTERNAL_ERROR);
        goto err;
    }

    ret = 1;

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

int ec_GFp_simple_points_make_affine(const EC_GROUP *group, size_t num,
                                     EC_POINT *points[], BN_CTX *ctx)
{
    BN_CTX *new_ctx = NULL;
    BIGNUM *tmp, *tmp_Z;
    BIGNUM **prod_Z = NULL;
    size_t i;
    int ret = 0;

    if (num == 0)
        return 1;

    if (ctx == NULL) {
        ctx = new_ctx = BN_CTX_new();
        if (ctx == NULL)
            return 0;
    }

    BN_CTX_start(ctx);
    tmp = BN_CTX_get(ctx);
    tmp_Z = BN_CTX_get(ctx);
    if (tmp_Z == NULL)
        goto err;

    prod_Z = OPENSSL_malloc(num * sizeof(prod_Z[0]));
    if (prod_Z == NULL)
        goto err;
    for (i = 0; i < num; i++) {
        prod_Z[i] = BN_new();
        if (prod_Z[i] == NULL)
            goto err;
    }

    /*
     * Set each prod_Z[i] to the product of points[0]->Z .. points[i]->Z,
     * skipping any zero-valued inputs (pretend that they're 1).
     */

    if (!BN_is_zero(points[0]->Z)) {
        if (!BN_copy(prod_Z[0], points[0]->Z))
            goto err;
    } else {
        if (group->meth->field_set_to_one != 0) {
            if (!group->meth->field_set_to_one(group, prod_Z[0], ctx))
                goto err;
        } else {
            if (!BN_one(prod_Z[0]))
                goto err;
        }
    }

    for (i = 1; i < num; i++) {
        if (!BN_is_zero(points[i]->Z)) {
            if (!group->
                meth->field_mul(group, prod_Z[i], prod_Z[i - 1], points[i]->Z,
                                ctx))
                goto err;
        } else {
            if (!BN_copy(prod_Z[i], prod_Z[i - 1]))
                goto err;
        }
    }

    /*
     * Now use a single explicit inversion to replace every non-zero
     * points[i]->Z by its inverse.
     */

    if (!group->meth->field_inv(group, tmp, prod_Z[num - 1], ctx)) {
        ECerr(EC_F_EC_GFP_SIMPLE_POINTS_MAKE_AFFINE, ERR_R_BN_LIB);
        goto err;
    }
    if (group->meth->field_encode != 0) {
        /*
         * In the Montgomery case, we just turned R*H (representing H) into
         * 1/(R*H), but we need R*(1/H) (representing 1/H); i.e. we need to
         * multiply by the Montgomery factor twice.
         */
        if (!group->meth->field_encode(group, tmp, tmp, ctx))
            goto err;
        if (!group->meth->field_encode(group, tmp, tmp, ctx))
            goto err;
    }

    for (i = num - 1; i > 0; --i) {
        /*
         * Loop invariant: tmp is the product of the inverses of points[0]->Z
         * .. points[i]->Z (zero-valued inputs skipped).
         */
        if (!BN_is_zero(points[i]->Z)) {
            /*
             * Set tmp_Z to the inverse of points[i]->Z (as product of Z
             * inverses 0 .. i, Z values 0 .. i - 1).
             */
            if (!group->
                meth->field_mul(group, tmp_Z, prod_Z[i - 1], tmp, ctx))
                goto err;
            /*
             * Update tmp to satisfy the loop invariant for i - 1.
             */
            if (!group->meth->field_mul(group, tmp, tmp, points[i]->Z, ctx))
                goto err;
            /* Replace points[i]->Z by its inverse. */
            if (!BN_copy(points[i]->Z, tmp_Z))
                goto err;
        }
    }

    if (!BN_is_zero(points[0]->Z)) {
        /* Replace points[0]->Z by its inverse. */
        if (!BN_copy(points[0]->Z, tmp))
            goto err;
    }

    /* Finally, fix up the X and Y coordinates for all points. */

    for (i = 0; i < num; i++) {
        EC_POINT *p = points[i];

        if (!BN_is_zero(p->Z)) {
            /* turn  (X, Y, 1/Z)  into  (X/Z^2, Y/Z^3, 1) */

            if (!group->meth->field_sqr(group, tmp, p->Z, ctx))
                goto err;
            if (!group->meth->field_mul(group, p->X, p->X, tmp, ctx))
                goto err;

            if (!group->meth->field_mul(group, tmp, tmp, p->Z, ctx))
                goto err;
            if (!group->meth->field_mul(group, p->Y, p->Y, tmp, ctx))
                goto err;

            if (group->meth->field_set_to_one != 0) {
                if (!group->meth->field_set_to_one(group, p->Z, ctx))
                    goto err;
            } else {
                if (!BN_one(p->Z))
                    goto err;
            }
            p->Z_is_one = 1;
        }
    }

    ret = 1;

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    if (prod_Z != NULL) {
        for (i = 0; i < num; i++) {
            if (prod_Z[i] == NULL)
                break;
            BN_clear_free(prod_Z[i]);
        }
        OPENSSL_free(prod_Z);
    }
    return ret;
}

int ec_GFp_simple_field_mul(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
                            const BIGNUM *b, BN_CTX *ctx)
{
    return BN_mod_mul(r, a, b, group->field, ctx);
}

int ec_GFp_simple_field_sqr(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
                            BN_CTX *ctx)
{
    return BN_mod_sqr(r, a, group->field, ctx);
}

/*-
 * Computes the multiplicative inverse of a in GF(p), storing the result in r.
 * If a is zero (or equivalent), you'll get a EC_R_CANNOT_INVERT error.
 * Since we don't have a Mont structure here, SCA hardening is with blinding.
 */
int ec_GFp_simple_field_inv(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
                            BN_CTX *ctx)
{
    BIGNUM *e = NULL;
    BN_CTX *new_ctx = NULL;
    int ret = 0;

    if (ctx == NULL && (ctx = new_ctx = BN_CTX_secure_new()) == NULL)
        return 0;

    BN_CTX_start(ctx);
    if ((e = BN_CTX_get(ctx)) == NULL)
        goto err;

    do {
        if (!BN_priv_rand_range(e, group->field))
        goto err;
    } while (BN_is_zero(e));

    /* r := a * e */
    if (!group->meth->field_mul(group, r, a, e, ctx))
        goto err;
    /* r := 1/(a * e) */
    if (!BN_mod_inverse(r, r, group->field, ctx)) {
        ECerr(EC_F_EC_GFP_SIMPLE_FIELD_INV, EC_R_CANNOT_INVERT);
        goto err;
    }
    /* r := e/(a * e) = 1/a */
    if (!group->meth->field_mul(group, r, r, e, ctx))
        goto err;

    ret = 1;

 err:
    BN_CTX_end(ctx);
    BN_CTX_free(new_ctx);
    return ret;
}

/*-
 * Apply randomization of EC point projective coordinates:
 *
 *   (X, Y ,Z ) = (lambda^2*X, lambda^3*Y, lambda*Z)
 *   lambda = [1,group->field)
 *
 */
int ec_GFp_simple_blind_coordinates(const EC_GROUP *group, EC_POINT *p,
                                    BN_CTX *ctx)
{
    int ret = 0;
    BIGNUM *lambda = NULL;
    BIGNUM *temp = NULL;

    BN_CTX_start(ctx);
    lambda = BN_CTX_get(ctx);
    temp = BN_CTX_get(ctx);
    if (temp == NULL) {
        ECerr(EC_F_EC_GFP_SIMPLE_BLIND_COORDINATES, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /* make sure lambda is not zero */
    do {
        if (!BN_priv_rand_range(lambda, group->field)) {
            ECerr(EC_F_EC_GFP_SIMPLE_BLIND_COORDINATES, ERR_R_BN_LIB);
            goto err;
        }
    } while (BN_is_zero(lambda));

    /* if field_encode defined convert between representations */
    if (group->meth->field_encode != NULL
        && !group->meth->field_encode(group, lambda, lambda, ctx))
        goto err;
    if (!group->meth->field_mul(group, p->Z, p->Z, lambda, ctx))
        goto err;
    if (!group->meth->field_sqr(group, temp, lambda, ctx))
        goto err;
    if (!group->meth->field_mul(group, p->X, p->X, temp, ctx))
        goto err;
    if (!group->meth->field_mul(group, temp, temp, lambda, ctx))
        goto err;
    if (!group->meth->field_mul(group, p->Y, p->Y, temp, ctx))
        goto err;
    p->Z_is_one = 0;

    ret = 1;

 err:
    BN_CTX_end(ctx);
    return ret;
}

/*-
 * Set s := p, r := 2p.
 *
 * For doubling we use Formula 3 from Izu-Takagi "A fast parallel elliptic curve
 * multiplication resistant against side channel attacks" appendix, as described
 * at
 * https://hyperelliptic.org/EFD/g1p/auto-shortw-xz.html#doubling-dbl-2002-it-2
 *
 * The input point p will be in randomized Jacobian projective coords:
 *      x = X/Z**2, y=Y/Z**3
 *
 * The output points p, s, and r are converted to standard (homogeneous)
 * projective coords:
 *      x = X/Z, y=Y/Z
 */
int ec_GFp_simple_ladder_pre(const EC_GROUP *group,
                             EC_POINT *r, EC_POINT *s,
                             EC_POINT *p, BN_CTX *ctx)
{
    BIGNUM *t1, *t2, *t3, *t4, *t5, *t6 = NULL;

    t1 = r->Z;
    t2 = r->Y;
    t3 = s->X;
    t4 = r->X;
    t5 = s->Y;
    t6 = s->Z;

    /* convert p: (X,Y,Z) -> (XZ,Y,Z**3) */
    if (!group->meth->field_mul(group, p->X, p->X, p->Z, ctx)
        || !group->meth->field_sqr(group, t1, p->Z, ctx)
        || !group->meth->field_mul(group, p->Z, p->Z, t1, ctx)
        /* r := 2p */
        || !group->meth->field_sqr(group, t2, p->X, ctx)
        || !group->meth->field_sqr(group, t3, p->Z, ctx)
        || !group->meth->field_mul(group, t4, t3, group->a, ctx)
        || !BN_mod_sub_quick(t5, t2, t4, group->field)
        || !BN_mod_add_quick(t2, t2, t4, group->field)
        || !group->meth->field_sqr(group, t5, t5, ctx)
        || !group->meth->field_mul(group, t6, t3, group->b, ctx)
        || !group->meth->field_mul(group, t1, p->X, p->Z, ctx)
        || !group->meth->field_mul(group, t4, t1, t6, ctx)
        || !BN_mod_lshift_quick(t4, t4, 3, group->field)
        /* r->X coord output */
        || !BN_mod_sub_quick(r->X, t5, t4, group->field)
        || !group->meth->field_mul(group, t1, t1, t2, ctx)
        || !group->meth->field_mul(group, t2, t3, t6, ctx)
        || !BN_mod_add_quick(t1, t1, t2, group->field)
        /* r->Z coord output */
        || !BN_mod_lshift_quick(r->Z, t1, 2, group->field)
        || !EC_POINT_copy(s, p))
        return 0;

    r->Z_is_one = 0;
    s->Z_is_one = 0;
    p->Z_is_one = 0;

    return 1;
}

/*-
 * Differential addition-and-doubling using  Eq. (9) and (10) from Izu-Takagi
 * "A fast parallel elliptic curve multiplication resistant against side channel
 * attacks", as described at
 * https://hyperelliptic.org/EFD/g1p/auto-shortw-xz.html#ladder-ladd-2002-it-4
 */
int ec_GFp_simple_ladder_step(const EC_GROUP *group,
                              EC_POINT *r, EC_POINT *s,
                              EC_POINT *p, BN_CTX *ctx)
{
    int ret = 0;
    BIGNUM *t0, *t1, *t2, *t3, *t4, *t5, *t6, *t7 = NULL;

    BN_CTX_start(ctx);
    t0 = BN_CTX_get(ctx);
    t1 = BN_CTX_get(ctx);
    t2 = BN_CTX_get(ctx);
    t3 = BN_CTX_get(ctx);
    t4 = BN_CTX_get(ctx);
    t5 = BN_CTX_get(ctx);
    t6 = BN_CTX_get(ctx);
    t7 = BN_CTX_get(ctx);

    if (t7 == NULL
        || !group->meth->field_mul(group, t0, r->X, s->X, ctx)
        || !group->meth->field_mul(group, t1, r->Z, s->Z, ctx)
        || !group->meth->field_mul(group, t2, r->X, s->Z, ctx)
        || !group->meth->field_mul(group, t3, r->Z, s->X, ctx)
        || !group->meth->field_mul(group, t4, group->a, t1, ctx)
        || !BN_mod_add_quick(t0, t0, t4, group->field)
        || !BN_mod_add_quick(t4, t3, t2, group->field)
        || !group->meth->field_mul(group, t0, t4, t0, ctx)
        || !group->meth->field_sqr(group, t1, t1, ctx)
        || !BN_mod_lshift_quick(t7, group->b, 2, group->field)
        || !group->meth->field_mul(group, t1, t7, t1, ctx)
        || !BN_mod_lshift1_quick(t0, t0, group->field)
        || !BN_mod_add_quick(t0, t1, t0, group->field)
        || !BN_mod_sub_quick(t1, t2, t3, group->field)
        || !group->meth->field_sqr(group, t1, t1, ctx)
        || !group->meth->field_mul(group, t3, t1, p->X, ctx)
        || !group->meth->field_mul(group, t0, p->Z, t0, ctx)
        /* s->X coord output */
        || !BN_mod_sub_quick(s->X, t0, t3, group->field)
        /* s->Z coord output */
        || !group->meth->field_mul(group, s->Z, p->Z, t1, ctx)
        || !group->meth->field_sqr(group, t3, r->X, ctx)
        || !group->meth->field_sqr(group, t2, r->Z, ctx)
        || !group->meth->field_mul(group, t4, t2, group->a, ctx)
        || !BN_mod_add_quick(t5, r->X, r->Z, group->field)
        || !group->meth->field_sqr(group, t5, t5, ctx)
        || !BN_mod_sub_quick(t5, t5, t3, group->field)
        || !BN_mod_sub_quick(t5, t5, t2, group->field)
        || !BN_mod_sub_quick(t6, t3, t4, group->field)
        || !group->meth->field_sqr(group, t6, t6, ctx)
        || !group->meth->field_mul(group, t0, t2, t5, ctx)
        || !group->meth->field_mul(group, t0, t7, t0, ctx)
        /* r->X coord output */
        || !BN_mod_sub_quick(r->X, t6, t0, group->field)
        || !BN_mod_add_quick(t6, t3, t4, group->field)
        || !group->meth->field_sqr(group, t3, t2, ctx)
        || !group->meth->field_mul(group, t7, t3, t7, ctx)
        || !group->meth->field_mul(group, t5, t5, t6, ctx)
        || !BN_mod_lshift1_quick(t5, t5, group->field)
        /* r->Z coord output */
        || !BN_mod_add_quick(r->Z, t7, t5, group->field))
        goto err;

    ret = 1;

 err:
    BN_CTX_end(ctx);
    return ret;
}

/*-
 * Recovers the y-coordinate of r using Eq. (8) from Brier-Joye, "Weierstrass
 * Elliptic Curves and Side-Channel Attacks", modified to work in projective
 * coordinates and return r in Jacobian projective coordinates.
 *
 * X4 = two*Y1*X2*Z3*Z2*Z1;
 * Y4 = two*b*Z3*SQR(Z2*Z1) + Z3*(a*Z2*Z1+X1*X2)*(X1*Z2+X2*Z1) - X3*SQR(X1*Z2-X2*Z1);
 * Z4 = two*Y1*Z3*SQR(Z2)*Z1;
 *
 * Z4 != 0 because:
 *  - Z1==0 implies p is at infinity, which would have caused an early exit in
 *    the caller;
 *  - Z2==0 implies r is at infinity (handled by the BN_is_zero(r->Z) branch);
 *  - Z3==0 implies s is at infinity (handled by the BN_is_zero(s->Z) branch);
 *  - Y1==0 implies p has order 2, so either r or s are infinity and handled by
 *    one of the BN_is_zero(...) branches.
 */
int ec_GFp_simple_ladder_post(const EC_GROUP *group,
                              EC_POINT *r, EC_POINT *s,
                              EC_POINT *p, BN_CTX *ctx)
{
    int ret = 0;
    BIGNUM *t0, *t1, *t2, *t3, *t4, *t5, *t6 = NULL;

    if (BN_is_zero(r->Z))
        return EC_POINT_set_to_infinity(group, r);

    if (BN_is_zero(s->Z)) {
        /* (X,Y,Z) -> (XZ,YZ**2,Z) */
        if (!group->meth->field_mul(group, r->X, p->X, p->Z, ctx)
            || !group->meth->field_sqr(group, r->Z, p->Z, ctx)
            || !group->meth->field_mul(group, r->Y, p->Y, r->Z, ctx)
            || !BN_copy(r->Z, p->Z)
            || !EC_POINT_invert(group, r, ctx))
            return 0;
        return 1;
    }

    BN_CTX_start(ctx);
    t0 = BN_CTX_get(ctx);
    t1 = BN_CTX_get(ctx);
    t2 = BN_CTX_get(ctx);
    t3 = BN_CTX_get(ctx);
    t4 = BN_CTX_get(ctx);
    t5 = BN_CTX_get(ctx);
    t6 = BN_CTX_get(ctx);

    if (t6 == NULL
        || !BN_mod_lshift1_quick(t0, p->Y, group->field)
        || !group->meth->field_mul(group, t1, r->X, p->Z, ctx)
        || !group->meth->field_mul(group, t2, r->Z, s->Z, ctx)
        || !group->meth->field_mul(group, t2, t1, t2, ctx)
        || !group->meth->field_mul(group, t3, t2, t0, ctx)
        || !group->meth->field_mul(group, t2, r->Z, p->Z, ctx)
        || !group->meth->field_sqr(group, t4, t2, ctx)
        || !BN_mod_lshift1_quick(t5, group->b, group->field)
        || !group->meth->field_mul(group, t4, t4, t5, ctx)
        || !group->meth->field_mul(group, t6, t2, group->a, ctx)
        || !group->meth->field_mul(group, t5, r->X, p->X, ctx)
        || !BN_mod_add_quick(t5, t6, t5, group->field)
        || !group->meth->field_mul(group, t6, r->Z, p->X, ctx)
        || !BN_mod_add_quick(t2, t6, t1, group->field)
        || !group->meth->field_mul(group, t5, t5, t2, ctx)
        || !BN_mod_sub_quick(t6, t6, t1, group->field)
        || !group->meth->field_sqr(group, t6, t6, ctx)
        || !group->meth->field_mul(group, t6, t6, s->X, ctx)
        || !BN_mod_add_quick(t4, t5, t4, group->field)
        || !group->meth->field_mul(group, t4, t4, s->Z, ctx)
        || !BN_mod_sub_quick(t4, t4, t6, group->field)
        || !group->meth->field_sqr(group, t5, r->Z, ctx)
        || !group->meth->field_mul(group, r->Z, p->Z, s->Z, ctx)
        || !group->meth->field_mul(group, r->Z, t5, r->Z, ctx)
        || !group->meth->field_mul(group, r->Z, r->Z, t0, ctx)
        /* t3 := X, t4 := Y */
        /* (X,Y,Z) -> (XZ,YZ**2,Z) */
        || !group->meth->field_mul(group, r->X, t3, r->Z, ctx)
        || !group->meth->field_sqr(group, t3, r->Z, ctx)
        || !group->meth->field_mul(group, r->Y, t4, t3, ctx))
        goto err;

    ret = 1;

 err:
    BN_CTX_end(ctx);
    return ret;
}
