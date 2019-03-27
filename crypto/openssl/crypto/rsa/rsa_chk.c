/*
 * Copyright 1999-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/bn.h>
#include <openssl/err.h>
#include "rsa_locl.h"

int RSA_check_key(const RSA *key)
{
    return RSA_check_key_ex(key, NULL);
}

int RSA_check_key_ex(const RSA *key, BN_GENCB *cb)
{
    BIGNUM *i, *j, *k, *l, *m;
    BN_CTX *ctx;
    int ret = 1, ex_primes = 0, idx;
    RSA_PRIME_INFO *pinfo;

    if (key->p == NULL || key->q == NULL || key->n == NULL
            || key->e == NULL || key->d == NULL) {
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_VALUE_MISSING);
        return 0;
    }

    /* multi-prime? */
    if (key->version == RSA_ASN1_VERSION_MULTI) {
        ex_primes = sk_RSA_PRIME_INFO_num(key->prime_infos);
        if (ex_primes <= 0
                || (ex_primes + 2) > rsa_multip_cap(BN_num_bits(key->n))) {
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_INVALID_MULTI_PRIME_KEY);
            return 0;
        }
    }

    i = BN_new();
    j = BN_new();
    k = BN_new();
    l = BN_new();
    m = BN_new();
    ctx = BN_CTX_new();
    if (i == NULL || j == NULL || k == NULL || l == NULL
            || m == NULL || ctx == NULL) {
        ret = -1;
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (BN_is_one(key->e)) {
        ret = 0;
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_BAD_E_VALUE);
    }
    if (!BN_is_odd(key->e)) {
        ret = 0;
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_BAD_E_VALUE);
    }

    /* p prime? */
    if (BN_is_prime_ex(key->p, BN_prime_checks, NULL, cb) != 1) {
        ret = 0;
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_P_NOT_PRIME);
    }

    /* q prime? */
    if (BN_is_prime_ex(key->q, BN_prime_checks, NULL, cb) != 1) {
        ret = 0;
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_Q_NOT_PRIME);
    }

    /* r_i prime? */
    for (idx = 0; idx < ex_primes; idx++) {
        pinfo = sk_RSA_PRIME_INFO_value(key->prime_infos, idx);
        if (BN_is_prime_ex(pinfo->r, BN_prime_checks, NULL, cb) != 1) {
            ret = 0;
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_MP_R_NOT_PRIME);
        }
    }

    /* n = p*q * r_3...r_i? */
    if (!BN_mul(i, key->p, key->q, ctx)) {
        ret = -1;
        goto err;
    }
    for (idx = 0; idx < ex_primes; idx++) {
        pinfo = sk_RSA_PRIME_INFO_value(key->prime_infos, idx);
        if (!BN_mul(i, i, pinfo->r, ctx)) {
            ret = -1;
            goto err;
        }
    }
    if (BN_cmp(i, key->n) != 0) {
        ret = 0;
        if (ex_primes)
            RSAerr(RSA_F_RSA_CHECK_KEY_EX,
                   RSA_R_N_DOES_NOT_EQUAL_PRODUCT_OF_PRIMES);
        else
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_N_DOES_NOT_EQUAL_P_Q);
    }

    /* d*e = 1  mod \lambda(n)? */
    if (!BN_sub(i, key->p, BN_value_one())) {
        ret = -1;
        goto err;
    }
    if (!BN_sub(j, key->q, BN_value_one())) {
        ret = -1;
        goto err;
    }

    /* now compute k = \lambda(n) = LCM(i, j, r_3 - 1...) */
    if (!BN_mul(l, i, j, ctx)) {
        ret = -1;
        goto err;
    }
    if (!BN_gcd(m, i, j, ctx)) {
        ret = -1;
        goto err;
    }
    for (idx = 0; idx < ex_primes; idx++) {
        pinfo = sk_RSA_PRIME_INFO_value(key->prime_infos, idx);
        if (!BN_sub(k, pinfo->r, BN_value_one())) {
            ret = -1;
            goto err;
        }
        if (!BN_mul(l, l, k, ctx)) {
            ret = -1;
            goto err;
        }
        if (!BN_gcd(m, m, k, ctx)) {
            ret = -1;
            goto err;
        }
    }
    if (!BN_div(k, NULL, l, m, ctx)) { /* remainder is 0 */
        ret = -1;
        goto err;
    }
    if (!BN_mod_mul(i, key->d, key->e, k, ctx)) {
        ret = -1;
        goto err;
    }

    if (!BN_is_one(i)) {
        ret = 0;
        RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_D_E_NOT_CONGRUENT_TO_1);
    }

    if (key->dmp1 != NULL && key->dmq1 != NULL && key->iqmp != NULL) {
        /* dmp1 = d mod (p-1)? */
        if (!BN_sub(i, key->p, BN_value_one())) {
            ret = -1;
            goto err;
        }
        if (!BN_mod(j, key->d, i, ctx)) {
            ret = -1;
            goto err;
        }
        if (BN_cmp(j, key->dmp1) != 0) {
            ret = 0;
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_DMP1_NOT_CONGRUENT_TO_D);
        }

        /* dmq1 = d mod (q-1)? */
        if (!BN_sub(i, key->q, BN_value_one())) {
            ret = -1;
            goto err;
        }
        if (!BN_mod(j, key->d, i, ctx)) {
            ret = -1;
            goto err;
        }
        if (BN_cmp(j, key->dmq1) != 0) {
            ret = 0;
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_DMQ1_NOT_CONGRUENT_TO_D);
        }

        /* iqmp = q^-1 mod p? */
        if (!BN_mod_inverse(i, key->q, key->p, ctx)) {
            ret = -1;
            goto err;
        }
        if (BN_cmp(i, key->iqmp) != 0) {
            ret = 0;
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_IQMP_NOT_INVERSE_OF_Q);
        }
    }

    for (idx = 0; idx < ex_primes; idx++) {
        pinfo = sk_RSA_PRIME_INFO_value(key->prime_infos, idx);
        /* d_i = d mod (r_i - 1)? */
        if (!BN_sub(i, pinfo->r, BN_value_one())) {
            ret = -1;
            goto err;
        }
        if (!BN_mod(j, key->d, i, ctx)) {
            ret = -1;
            goto err;
        }
        if (BN_cmp(j, pinfo->d) != 0) {
            ret = 0;
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_MP_EXPONENT_NOT_CONGRUENT_TO_D);
        }
        /* t_i = R_i ^ -1 mod r_i ? */
        if (!BN_mod_inverse(i, pinfo->pp, pinfo->r, ctx)) {
            ret = -1;
            goto err;
        }
        if (BN_cmp(i, pinfo->t) != 0) {
            ret = 0;
            RSAerr(RSA_F_RSA_CHECK_KEY_EX, RSA_R_MP_COEFFICIENT_NOT_INVERSE_OF_R);
        }
    }

 err:
    BN_free(i);
    BN_free(j);
    BN_free(k);
    BN_free(l);
    BN_free(m);
    BN_CTX_free(ctx);
    return ret;
}
