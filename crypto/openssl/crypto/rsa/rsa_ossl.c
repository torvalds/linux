/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include "internal/bn_int.h"
#include "rsa_locl.h"
#include "internal/constant_time_locl.h"

static int rsa_ossl_public_encrypt(int flen, const unsigned char *from,
                                  unsigned char *to, RSA *rsa, int padding);
static int rsa_ossl_private_encrypt(int flen, const unsigned char *from,
                                   unsigned char *to, RSA *rsa, int padding);
static int rsa_ossl_public_decrypt(int flen, const unsigned char *from,
                                  unsigned char *to, RSA *rsa, int padding);
static int rsa_ossl_private_decrypt(int flen, const unsigned char *from,
                                   unsigned char *to, RSA *rsa, int padding);
static int rsa_ossl_mod_exp(BIGNUM *r0, const BIGNUM *i, RSA *rsa,
                           BN_CTX *ctx);
static int rsa_ossl_init(RSA *rsa);
static int rsa_ossl_finish(RSA *rsa);
static RSA_METHOD rsa_pkcs1_ossl_meth = {
    "OpenSSL PKCS#1 RSA",
    rsa_ossl_public_encrypt,
    rsa_ossl_public_decrypt,     /* signature verification */
    rsa_ossl_private_encrypt,    /* signing */
    rsa_ossl_private_decrypt,
    rsa_ossl_mod_exp,
    BN_mod_exp_mont,            /* XXX probably we should not use Montgomery
                                 * if e == 3 */
    rsa_ossl_init,
    rsa_ossl_finish,
    RSA_FLAG_FIPS_METHOD,       /* flags */
    NULL,
    0,                          /* rsa_sign */
    0,                          /* rsa_verify */
    NULL,                       /* rsa_keygen */
    NULL                        /* rsa_multi_prime_keygen */
};

static const RSA_METHOD *default_RSA_meth = &rsa_pkcs1_ossl_meth;

void RSA_set_default_method(const RSA_METHOD *meth)
{
    default_RSA_meth = meth;
}

const RSA_METHOD *RSA_get_default_method(void)
{
    return default_RSA_meth;
}

const RSA_METHOD *RSA_PKCS1_OpenSSL(void)
{
    return &rsa_pkcs1_ossl_meth;
}

const RSA_METHOD *RSA_null_method(void)
{
    return NULL;
}

static int rsa_ossl_public_encrypt(int flen, const unsigned char *from,
                                  unsigned char *to, RSA *rsa, int padding)
{
    BIGNUM *f, *ret;
    int i, num = 0, r = -1;
    unsigned char *buf = NULL;
    BN_CTX *ctx = NULL;

    if (BN_num_bits(rsa->n) > OPENSSL_RSA_MAX_MODULUS_BITS) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_ENCRYPT, RSA_R_MODULUS_TOO_LARGE);
        return -1;
    }

    if (BN_ucmp(rsa->n, rsa->e) <= 0) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_ENCRYPT, RSA_R_BAD_E_VALUE);
        return -1;
    }

    /* for large moduli, enforce exponent limit */
    if (BN_num_bits(rsa->n) > OPENSSL_RSA_SMALL_MODULUS_BITS) {
        if (BN_num_bits(rsa->e) > OPENSSL_RSA_MAX_PUBEXP_BITS) {
            RSAerr(RSA_F_RSA_OSSL_PUBLIC_ENCRYPT, RSA_R_BAD_E_VALUE);
            return -1;
        }
    }

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;
    BN_CTX_start(ctx);
    f = BN_CTX_get(ctx);
    ret = BN_CTX_get(ctx);
    num = BN_num_bytes(rsa->n);
    buf = OPENSSL_malloc(num);
    if (ret == NULL || buf == NULL) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_ENCRYPT, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    switch (padding) {
    case RSA_PKCS1_PADDING:
        i = RSA_padding_add_PKCS1_type_2(buf, num, from, flen);
        break;
    case RSA_PKCS1_OAEP_PADDING:
        i = RSA_padding_add_PKCS1_OAEP(buf, num, from, flen, NULL, 0);
        break;
    case RSA_SSLV23_PADDING:
        i = RSA_padding_add_SSLv23(buf, num, from, flen);
        break;
    case RSA_NO_PADDING:
        i = RSA_padding_add_none(buf, num, from, flen);
        break;
    default:
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_ENCRYPT, RSA_R_UNKNOWN_PADDING_TYPE);
        goto err;
    }
    if (i <= 0)
        goto err;

    if (BN_bin2bn(buf, num, f) == NULL)
        goto err;

    if (BN_ucmp(f, rsa->n) >= 0) {
        /* usually the padding functions would catch this */
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_ENCRYPT,
               RSA_R_DATA_TOO_LARGE_FOR_MODULUS);
        goto err;
    }

    if (rsa->flags & RSA_FLAG_CACHE_PUBLIC)
        if (!BN_MONT_CTX_set_locked(&rsa->_method_mod_n, rsa->lock,
                                    rsa->n, ctx))
            goto err;

    if (!rsa->meth->bn_mod_exp(ret, f, rsa->e, rsa->n, ctx,
                               rsa->_method_mod_n))
        goto err;

    /*
     * BN_bn2binpad puts in leading 0 bytes if the number is less than
     * the length of the modulus.
     */
    r = BN_bn2binpad(ret, to, num);
 err:
    if (ctx != NULL)
        BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    OPENSSL_clear_free(buf, num);
    return r;
}

static BN_BLINDING *rsa_get_blinding(RSA *rsa, int *local, BN_CTX *ctx)
{
    BN_BLINDING *ret;

    CRYPTO_THREAD_write_lock(rsa->lock);

    if (rsa->blinding == NULL) {
        rsa->blinding = RSA_setup_blinding(rsa, ctx);
    }

    ret = rsa->blinding;
    if (ret == NULL)
        goto err;

    if (BN_BLINDING_is_current_thread(ret)) {
        /* rsa->blinding is ours! */

        *local = 1;
    } else {
        /* resort to rsa->mt_blinding instead */

        /*
         * instructs rsa_blinding_convert(), rsa_blinding_invert() that the
         * BN_BLINDING is shared, meaning that accesses require locks, and
         * that the blinding factor must be stored outside the BN_BLINDING
         */
        *local = 0;

        if (rsa->mt_blinding == NULL) {
            rsa->mt_blinding = RSA_setup_blinding(rsa, ctx);
        }
        ret = rsa->mt_blinding;
    }

 err:
    CRYPTO_THREAD_unlock(rsa->lock);
    return ret;
}

static int rsa_blinding_convert(BN_BLINDING *b, BIGNUM *f, BIGNUM *unblind,
                                BN_CTX *ctx)
{
    if (unblind == NULL) {
        /*
         * Local blinding: store the unblinding factor in BN_BLINDING.
         */
        return BN_BLINDING_convert_ex(f, NULL, b, ctx);
    } else {
        /*
         * Shared blinding: store the unblinding factor outside BN_BLINDING.
         */
        int ret;

        BN_BLINDING_lock(b);
        ret = BN_BLINDING_convert_ex(f, unblind, b, ctx);
        BN_BLINDING_unlock(b);

        return ret;
    }
}

static int rsa_blinding_invert(BN_BLINDING *b, BIGNUM *f, BIGNUM *unblind,
                               BN_CTX *ctx)
{
    /*
     * For local blinding, unblind is set to NULL, and BN_BLINDING_invert_ex
     * will use the unblinding factor stored in BN_BLINDING. If BN_BLINDING
     * is shared between threads, unblind must be non-null:
     * BN_BLINDING_invert_ex will then use the local unblinding factor, and
     * will only read the modulus from BN_BLINDING. In both cases it's safe
     * to access the blinding without a lock.
     */
    return BN_BLINDING_invert_ex(f, unblind, b, ctx);
}

/* signing */
static int rsa_ossl_private_encrypt(int flen, const unsigned char *from,
                                   unsigned char *to, RSA *rsa, int padding)
{
    BIGNUM *f, *ret, *res;
    int i, num = 0, r = -1;
    unsigned char *buf = NULL;
    BN_CTX *ctx = NULL;
    int local_blinding = 0;
    /*
     * Used only if the blinding structure is shared. A non-NULL unblind
     * instructs rsa_blinding_convert() and rsa_blinding_invert() to store
     * the unblinding factor outside the blinding structure.
     */
    BIGNUM *unblind = NULL;
    BN_BLINDING *blinding = NULL;

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;
    BN_CTX_start(ctx);
    f = BN_CTX_get(ctx);
    ret = BN_CTX_get(ctx);
    num = BN_num_bytes(rsa->n);
    buf = OPENSSL_malloc(num);
    if (ret == NULL || buf == NULL) {
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_ENCRYPT, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    switch (padding) {
    case RSA_PKCS1_PADDING:
        i = RSA_padding_add_PKCS1_type_1(buf, num, from, flen);
        break;
    case RSA_X931_PADDING:
        i = RSA_padding_add_X931(buf, num, from, flen);
        break;
    case RSA_NO_PADDING:
        i = RSA_padding_add_none(buf, num, from, flen);
        break;
    case RSA_SSLV23_PADDING:
    default:
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_ENCRYPT, RSA_R_UNKNOWN_PADDING_TYPE);
        goto err;
    }
    if (i <= 0)
        goto err;

    if (BN_bin2bn(buf, num, f) == NULL)
        goto err;

    if (BN_ucmp(f, rsa->n) >= 0) {
        /* usually the padding functions would catch this */
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_ENCRYPT,
               RSA_R_DATA_TOO_LARGE_FOR_MODULUS);
        goto err;
    }

    if (rsa->flags & RSA_FLAG_CACHE_PUBLIC)
        if (!BN_MONT_CTX_set_locked(&rsa->_method_mod_n, rsa->lock,
                                    rsa->n, ctx))
            goto err;

    if (!(rsa->flags & RSA_FLAG_NO_BLINDING)) {
        blinding = rsa_get_blinding(rsa, &local_blinding, ctx);
        if (blinding == NULL) {
            RSAerr(RSA_F_RSA_OSSL_PRIVATE_ENCRYPT, ERR_R_INTERNAL_ERROR);
            goto err;
        }
    }

    if (blinding != NULL) {
        if (!local_blinding && ((unblind = BN_CTX_get(ctx)) == NULL)) {
            RSAerr(RSA_F_RSA_OSSL_PRIVATE_ENCRYPT, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (!rsa_blinding_convert(blinding, f, unblind, ctx))
            goto err;
    }

    if ((rsa->flags & RSA_FLAG_EXT_PKEY) ||
        (rsa->version == RSA_ASN1_VERSION_MULTI) ||
        ((rsa->p != NULL) &&
         (rsa->q != NULL) &&
         (rsa->dmp1 != NULL) && (rsa->dmq1 != NULL) && (rsa->iqmp != NULL))) {
        if (!rsa->meth->rsa_mod_exp(ret, f, rsa, ctx))
            goto err;
    } else {
        BIGNUM *d = BN_new();
        if (d == NULL) {
            RSAerr(RSA_F_RSA_OSSL_PRIVATE_ENCRYPT, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        BN_with_flags(d, rsa->d, BN_FLG_CONSTTIME);

        if (!rsa->meth->bn_mod_exp(ret, f, d, rsa->n, ctx,
                                   rsa->_method_mod_n)) {
            BN_free(d);
            goto err;
        }
        /* We MUST free d before any further use of rsa->d */
        BN_free(d);
    }

    if (blinding)
        if (!rsa_blinding_invert(blinding, ret, unblind, ctx))
            goto err;

    if (padding == RSA_X931_PADDING) {
        if (!BN_sub(f, rsa->n, ret))
            goto err;
        if (BN_cmp(ret, f) > 0)
            res = f;
        else
            res = ret;
    } else {
        res = ret;
    }

    /*
     * BN_bn2binpad puts in leading 0 bytes if the number is less than
     * the length of the modulus.
     */
    r = BN_bn2binpad(res, to, num);
 err:
    if (ctx != NULL)
        BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    OPENSSL_clear_free(buf, num);
    return r;
}

static int rsa_ossl_private_decrypt(int flen, const unsigned char *from,
                                   unsigned char *to, RSA *rsa, int padding)
{
    BIGNUM *f, *ret;
    int j, num = 0, r = -1;
    unsigned char *buf = NULL;
    BN_CTX *ctx = NULL;
    int local_blinding = 0;
    /*
     * Used only if the blinding structure is shared. A non-NULL unblind
     * instructs rsa_blinding_convert() and rsa_blinding_invert() to store
     * the unblinding factor outside the blinding structure.
     */
    BIGNUM *unblind = NULL;
    BN_BLINDING *blinding = NULL;

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;
    BN_CTX_start(ctx);
    f = BN_CTX_get(ctx);
    ret = BN_CTX_get(ctx);
    num = BN_num_bytes(rsa->n);
    buf = OPENSSL_malloc(num);
    if (ret == NULL || buf == NULL) {
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /*
     * This check was for equality but PGP does evil things and chops off the
     * top '0' bytes
     */
    if (flen > num) {
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT,
               RSA_R_DATA_GREATER_THAN_MOD_LEN);
        goto err;
    }

    /* make data into a big number */
    if (BN_bin2bn(from, (int)flen, f) == NULL)
        goto err;

    if (BN_ucmp(f, rsa->n) >= 0) {
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT,
               RSA_R_DATA_TOO_LARGE_FOR_MODULUS);
        goto err;
    }

    if (!(rsa->flags & RSA_FLAG_NO_BLINDING)) {
        blinding = rsa_get_blinding(rsa, &local_blinding, ctx);
        if (blinding == NULL) {
            RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT, ERR_R_INTERNAL_ERROR);
            goto err;
        }
    }

    if (blinding != NULL) {
        if (!local_blinding && ((unblind = BN_CTX_get(ctx)) == NULL)) {
            RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        if (!rsa_blinding_convert(blinding, f, unblind, ctx))
            goto err;
    }

    /* do the decrypt */
    if ((rsa->flags & RSA_FLAG_EXT_PKEY) ||
        (rsa->version == RSA_ASN1_VERSION_MULTI) ||
        ((rsa->p != NULL) &&
         (rsa->q != NULL) &&
         (rsa->dmp1 != NULL) && (rsa->dmq1 != NULL) && (rsa->iqmp != NULL))) {
        if (!rsa->meth->rsa_mod_exp(ret, f, rsa, ctx))
            goto err;
    } else {
        BIGNUM *d = BN_new();
        if (d == NULL) {
            RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        BN_with_flags(d, rsa->d, BN_FLG_CONSTTIME);

        if (rsa->flags & RSA_FLAG_CACHE_PUBLIC)
            if (!BN_MONT_CTX_set_locked(&rsa->_method_mod_n, rsa->lock,
                                        rsa->n, ctx)) {
                BN_free(d);
                goto err;
            }
        if (!rsa->meth->bn_mod_exp(ret, f, d, rsa->n, ctx,
                                   rsa->_method_mod_n)) {
            BN_free(d);
            goto err;
        }
        /* We MUST free d before any further use of rsa->d */
        BN_free(d);
    }

    if (blinding)
        if (!rsa_blinding_invert(blinding, ret, unblind, ctx))
            goto err;

    j = BN_bn2binpad(ret, buf, num);

    switch (padding) {
    case RSA_PKCS1_PADDING:
        r = RSA_padding_check_PKCS1_type_2(to, num, buf, j, num);
        break;
    case RSA_PKCS1_OAEP_PADDING:
        r = RSA_padding_check_PKCS1_OAEP(to, num, buf, j, num, NULL, 0);
        break;
    case RSA_SSLV23_PADDING:
        r = RSA_padding_check_SSLv23(to, num, buf, j, num);
        break;
    case RSA_NO_PADDING:
        memcpy(to, buf, (r = j));
        break;
    default:
        RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT, RSA_R_UNKNOWN_PADDING_TYPE);
        goto err;
    }
    RSAerr(RSA_F_RSA_OSSL_PRIVATE_DECRYPT, RSA_R_PADDING_CHECK_FAILED);
    err_clear_last_constant_time(r >= 0);

 err:
    if (ctx != NULL)
        BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    OPENSSL_clear_free(buf, num);
    return r;
}

/* signature verification */
static int rsa_ossl_public_decrypt(int flen, const unsigned char *from,
                                  unsigned char *to, RSA *rsa, int padding)
{
    BIGNUM *f, *ret;
    int i, num = 0, r = -1;
    unsigned char *buf = NULL;
    BN_CTX *ctx = NULL;

    if (BN_num_bits(rsa->n) > OPENSSL_RSA_MAX_MODULUS_BITS) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, RSA_R_MODULUS_TOO_LARGE);
        return -1;
    }

    if (BN_ucmp(rsa->n, rsa->e) <= 0) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, RSA_R_BAD_E_VALUE);
        return -1;
    }

    /* for large moduli, enforce exponent limit */
    if (BN_num_bits(rsa->n) > OPENSSL_RSA_SMALL_MODULUS_BITS) {
        if (BN_num_bits(rsa->e) > OPENSSL_RSA_MAX_PUBEXP_BITS) {
            RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, RSA_R_BAD_E_VALUE);
            return -1;
        }
    }

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;
    BN_CTX_start(ctx);
    f = BN_CTX_get(ctx);
    ret = BN_CTX_get(ctx);
    num = BN_num_bytes(rsa->n);
    buf = OPENSSL_malloc(num);
    if (ret == NULL || buf == NULL) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /*
     * This check was for equality but PGP does evil things and chops off the
     * top '0' bytes
     */
    if (flen > num) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, RSA_R_DATA_GREATER_THAN_MOD_LEN);
        goto err;
    }

    if (BN_bin2bn(from, flen, f) == NULL)
        goto err;

    if (BN_ucmp(f, rsa->n) >= 0) {
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT,
               RSA_R_DATA_TOO_LARGE_FOR_MODULUS);
        goto err;
    }

    if (rsa->flags & RSA_FLAG_CACHE_PUBLIC)
        if (!BN_MONT_CTX_set_locked(&rsa->_method_mod_n, rsa->lock,
                                    rsa->n, ctx))
            goto err;

    if (!rsa->meth->bn_mod_exp(ret, f, rsa->e, rsa->n, ctx,
                               rsa->_method_mod_n))
        goto err;

    if ((padding == RSA_X931_PADDING) && ((bn_get_words(ret)[0] & 0xf) != 12))
        if (!BN_sub(ret, rsa->n, ret))
            goto err;

    i = BN_bn2binpad(ret, buf, num);

    switch (padding) {
    case RSA_PKCS1_PADDING:
        r = RSA_padding_check_PKCS1_type_1(to, num, buf, i, num);
        break;
    case RSA_X931_PADDING:
        r = RSA_padding_check_X931(to, num, buf, i, num);
        break;
    case RSA_NO_PADDING:
        memcpy(to, buf, (r = i));
        break;
    default:
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, RSA_R_UNKNOWN_PADDING_TYPE);
        goto err;
    }
    if (r < 0)
        RSAerr(RSA_F_RSA_OSSL_PUBLIC_DECRYPT, RSA_R_PADDING_CHECK_FAILED);

 err:
    if (ctx != NULL)
        BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    OPENSSL_clear_free(buf, num);
    return r;
}

static int rsa_ossl_mod_exp(BIGNUM *r0, const BIGNUM *I, RSA *rsa, BN_CTX *ctx)
{
    BIGNUM *r1, *m1, *vrfy, *r2, *m[RSA_MAX_PRIME_NUM - 2];
    int ret = 0, i, ex_primes = 0, smooth = 0;
    RSA_PRIME_INFO *pinfo;

    BN_CTX_start(ctx);

    r1 = BN_CTX_get(ctx);
    r2 = BN_CTX_get(ctx);
    m1 = BN_CTX_get(ctx);
    vrfy = BN_CTX_get(ctx);
    if (vrfy == NULL)
        goto err;

    if (rsa->version == RSA_ASN1_VERSION_MULTI
        && ((ex_primes = sk_RSA_PRIME_INFO_num(rsa->prime_infos)) <= 0
             || ex_primes > RSA_MAX_PRIME_NUM - 2))
        goto err;

    if (rsa->flags & RSA_FLAG_CACHE_PRIVATE) {
        BIGNUM *factor = BN_new();

        if (factor == NULL)
            goto err;

        /*
         * Make sure BN_mod_inverse in Montgomery initialization uses the
         * BN_FLG_CONSTTIME flag
         */
        if (!(BN_with_flags(factor, rsa->p, BN_FLG_CONSTTIME),
              BN_MONT_CTX_set_locked(&rsa->_method_mod_p, rsa->lock,
                                     factor, ctx))
            || !(BN_with_flags(factor, rsa->q, BN_FLG_CONSTTIME),
                 BN_MONT_CTX_set_locked(&rsa->_method_mod_q, rsa->lock,
                                        factor, ctx))) {
            BN_free(factor);
            goto err;
        }
        for (i = 0; i < ex_primes; i++) {
            pinfo = sk_RSA_PRIME_INFO_value(rsa->prime_infos, i);
            BN_with_flags(factor, pinfo->r, BN_FLG_CONSTTIME);
            if (!BN_MONT_CTX_set_locked(&pinfo->m, rsa->lock, factor, ctx)) {
                BN_free(factor);
                goto err;
            }
        }
        /*
         * We MUST free |factor| before any further use of the prime factors
         */
        BN_free(factor);

        smooth = (ex_primes == 0)
                 && (rsa->meth->bn_mod_exp == BN_mod_exp_mont)
                 && (BN_num_bits(rsa->q) == BN_num_bits(rsa->p));
    }

    if (rsa->flags & RSA_FLAG_CACHE_PUBLIC)
        if (!BN_MONT_CTX_set_locked(&rsa->_method_mod_n, rsa->lock,
                                    rsa->n, ctx))
            goto err;

    if (smooth) {
        /*
         * Conversion from Montgomery domain, a.k.a. Montgomery reduction,
         * accepts values in [0-m*2^w) range. w is m's bit width rounded up
         * to limb width. So that at the very least if |I| is fully reduced,
         * i.e. less than p*q, we can count on from-to round to perform
         * below modulo operations on |I|. Unlike BN_mod it's constant time.
         */
        if (/* m1 = I moq q */
            !bn_from_mont_fixed_top(m1, I, rsa->_method_mod_q, ctx)
            || !bn_to_mont_fixed_top(m1, m1, rsa->_method_mod_q, ctx)
            /* m1 = m1^dmq1 mod q */
            || !BN_mod_exp_mont_consttime(m1, m1, rsa->dmq1, rsa->q, ctx,
                                          rsa->_method_mod_q)
            /* r1 = I mod p */
            || !bn_from_mont_fixed_top(r1, I, rsa->_method_mod_p, ctx)
            || !bn_to_mont_fixed_top(r1, r1, rsa->_method_mod_p, ctx)
            /* r1 = r1^dmp1 mod p */
            || !BN_mod_exp_mont_consttime(r1, r1, rsa->dmp1, rsa->p, ctx,
                                          rsa->_method_mod_p)
            /* r1 = (r1 - m1) mod p */
            /*
             * bn_mod_sub_fixed_top is not regular modular subtraction,
             * it can tolerate subtrahend to be larger than modulus, but
             * not bit-wise wider. This makes up for uncommon q>p case,
             * when |m1| can be larger than |rsa->p|.
             */
            || !bn_mod_sub_fixed_top(r1, r1, m1, rsa->p)

            /* r1 = r1 * iqmp mod p */
            || !bn_to_mont_fixed_top(r1, r1, rsa->_method_mod_p, ctx)
            || !bn_mul_mont_fixed_top(r1, r1, rsa->iqmp, rsa->_method_mod_p,
                                      ctx)
            /* r0 = r1 * q + m1 */
            || !bn_mul_fixed_top(r0, r1, rsa->q, ctx)
            || !bn_mod_add_fixed_top(r0, r0, m1, rsa->n))
            goto err;

        goto tail;
    }

    /* compute I mod q */
    {
        BIGNUM *c = BN_new();
        if (c == NULL)
            goto err;
        BN_with_flags(c, I, BN_FLG_CONSTTIME);

        if (!BN_mod(r1, c, rsa->q, ctx)) {
            BN_free(c);
            goto err;
        }

        {
            BIGNUM *dmq1 = BN_new();
            if (dmq1 == NULL) {
                BN_free(c);
                goto err;
            }
            BN_with_flags(dmq1, rsa->dmq1, BN_FLG_CONSTTIME);

            /* compute r1^dmq1 mod q */
            if (!rsa->meth->bn_mod_exp(m1, r1, dmq1, rsa->q, ctx,
                                       rsa->_method_mod_q)) {
                BN_free(c);
                BN_free(dmq1);
                goto err;
            }
            /* We MUST free dmq1 before any further use of rsa->dmq1 */
            BN_free(dmq1);
        }

        /* compute I mod p */
        if (!BN_mod(r1, c, rsa->p, ctx)) {
            BN_free(c);
            goto err;
        }
        /* We MUST free c before any further use of I */
        BN_free(c);
    }

    {
        BIGNUM *dmp1 = BN_new();
        if (dmp1 == NULL)
            goto err;
        BN_with_flags(dmp1, rsa->dmp1, BN_FLG_CONSTTIME);

        /* compute r1^dmp1 mod p */
        if (!rsa->meth->bn_mod_exp(r0, r1, dmp1, rsa->p, ctx,
                                   rsa->_method_mod_p)) {
            BN_free(dmp1);
            goto err;
        }
        /* We MUST free dmp1 before any further use of rsa->dmp1 */
        BN_free(dmp1);
    }

    /*
     * calculate m_i in multi-prime case
     *
     * TODO:
     * 1. squash the following two loops and calculate |m_i| there.
     * 2. remove cc and reuse |c|.
     * 3. remove |dmq1| and |dmp1| in previous block and use |di|.
     *
     * If these things are done, the code will be more readable.
     */
    if (ex_primes > 0) {
        BIGNUM *di = BN_new(), *cc = BN_new();

        if (cc == NULL || di == NULL) {
            BN_free(cc);
            BN_free(di);
            goto err;
        }

        for (i = 0; i < ex_primes; i++) {
            /* prepare m_i */
            if ((m[i] = BN_CTX_get(ctx)) == NULL) {
                BN_free(cc);
                BN_free(di);
                goto err;
            }

            pinfo = sk_RSA_PRIME_INFO_value(rsa->prime_infos, i);

            /* prepare c and d_i */
            BN_with_flags(cc, I, BN_FLG_CONSTTIME);
            BN_with_flags(di, pinfo->d, BN_FLG_CONSTTIME);

            if (!BN_mod(r1, cc, pinfo->r, ctx)) {
                BN_free(cc);
                BN_free(di);
                goto err;
            }
            /* compute r1 ^ d_i mod r_i */
            if (!rsa->meth->bn_mod_exp(m[i], r1, di, pinfo->r, ctx, pinfo->m)) {
                BN_free(cc);
                BN_free(di);
                goto err;
            }
        }

        BN_free(cc);
        BN_free(di);
    }

    if (!BN_sub(r0, r0, m1))
        goto err;
    /*
     * This will help stop the size of r0 increasing, which does affect the
     * multiply if it optimised for a power of 2 size
     */
    if (BN_is_negative(r0))
        if (!BN_add(r0, r0, rsa->p))
            goto err;

    if (!BN_mul(r1, r0, rsa->iqmp, ctx))
        goto err;

    {
        BIGNUM *pr1 = BN_new();
        if (pr1 == NULL)
            goto err;
        BN_with_flags(pr1, r1, BN_FLG_CONSTTIME);

        if (!BN_mod(r0, pr1, rsa->p, ctx)) {
            BN_free(pr1);
            goto err;
        }
        /* We MUST free pr1 before any further use of r1 */
        BN_free(pr1);
    }

    /*
     * If p < q it is occasionally possible for the correction of adding 'p'
     * if r0 is negative above to leave the result still negative. This can
     * break the private key operations: the following second correction
     * should *always* correct this rare occurrence. This will *never* happen
     * with OpenSSL generated keys because they ensure p > q [steve]
     */
    if (BN_is_negative(r0))
        if (!BN_add(r0, r0, rsa->p))
            goto err;
    if (!BN_mul(r1, r0, rsa->q, ctx))
        goto err;
    if (!BN_add(r0, r1, m1))
        goto err;

    /* add m_i to m in multi-prime case */
    if (ex_primes > 0) {
        BIGNUM *pr2 = BN_new();

        if (pr2 == NULL)
            goto err;

        for (i = 0; i < ex_primes; i++) {
            pinfo = sk_RSA_PRIME_INFO_value(rsa->prime_infos, i);
            if (!BN_sub(r1, m[i], r0)) {
                BN_free(pr2);
                goto err;
            }

            if (!BN_mul(r2, r1, pinfo->t, ctx)) {
                BN_free(pr2);
                goto err;
            }

            BN_with_flags(pr2, r2, BN_FLG_CONSTTIME);

            if (!BN_mod(r1, pr2, pinfo->r, ctx)) {
                BN_free(pr2);
                goto err;
            }

            if (BN_is_negative(r1))
                if (!BN_add(r1, r1, pinfo->r)) {
                    BN_free(pr2);
                    goto err;
                }
            if (!BN_mul(r1, r1, pinfo->pp, ctx)) {
                BN_free(pr2);
                goto err;
            }
            if (!BN_add(r0, r0, r1)) {
                BN_free(pr2);
                goto err;
            }
        }
        BN_free(pr2);
    }

 tail:
    if (rsa->e && rsa->n) {
        if (rsa->meth->bn_mod_exp == BN_mod_exp_mont) {
            if (!BN_mod_exp_mont(vrfy, r0, rsa->e, rsa->n, ctx,
                                 rsa->_method_mod_n))
                goto err;
        } else {
            bn_correct_top(r0);
            if (!rsa->meth->bn_mod_exp(vrfy, r0, rsa->e, rsa->n, ctx,
                                       rsa->_method_mod_n))
                goto err;
        }
        /*
         * If 'I' was greater than (or equal to) rsa->n, the operation will
         * be equivalent to using 'I mod n'. However, the result of the
         * verify will *always* be less than 'n' so we don't check for
         * absolute equality, just congruency.
         */
        if (!BN_sub(vrfy, vrfy, I))
            goto err;
        if (BN_is_zero(vrfy)) {
            bn_correct_top(r0);
            ret = 1;
            goto err;   /* not actually error */
        }
        if (!BN_mod(vrfy, vrfy, rsa->n, ctx))
            goto err;
        if (BN_is_negative(vrfy))
            if (!BN_add(vrfy, vrfy, rsa->n))
                goto err;
        if (!BN_is_zero(vrfy)) {
            /*
             * 'I' and 'vrfy' aren't congruent mod n. Don't leak
             * miscalculated CRT output, just do a raw (slower) mod_exp and
             * return that instead.
             */

            BIGNUM *d = BN_new();
            if (d == NULL)
                goto err;
            BN_with_flags(d, rsa->d, BN_FLG_CONSTTIME);

            if (!rsa->meth->bn_mod_exp(r0, I, d, rsa->n, ctx,
                                       rsa->_method_mod_n)) {
                BN_free(d);
                goto err;
            }
            /* We MUST free d before any further use of rsa->d */
            BN_free(d);
        }
    }
    /*
     * It's unfortunate that we have to bn_correct_top(r0). What hopefully
     * saves the day is that correction is highly unlike, and private key
     * operations are customarily performed on blinded message. Which means
     * that attacker won't observe correlation with chosen plaintext.
     * Secondly, remaining code would still handle it in same computational
     * time and even conceal memory access pattern around corrected top.
     */
    bn_correct_top(r0);
    ret = 1;
 err:
    BN_CTX_end(ctx);
    return ret;
}

static int rsa_ossl_init(RSA *rsa)
{
    rsa->flags |= RSA_FLAG_CACHE_PUBLIC | RSA_FLAG_CACHE_PRIVATE;
    return 1;
}

static int rsa_ossl_finish(RSA *rsa)
{
    int i;
    RSA_PRIME_INFO *pinfo;

    BN_MONT_CTX_free(rsa->_method_mod_n);
    BN_MONT_CTX_free(rsa->_method_mod_p);
    BN_MONT_CTX_free(rsa->_method_mod_q);
    for (i = 0; i < sk_RSA_PRIME_INFO_num(rsa->prime_infos); i++) {
        pinfo = sk_RSA_PRIME_INFO_value(rsa->prime_infos, i);
        BN_MONT_CTX_free(pinfo->m);
    }
    return 1;
}
