/*
 * Copyright 1998-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>
#include "internal/cryptlib.h"
#include "bn_lcl.h"

#define BN_BLINDING_COUNTER     32

struct bn_blinding_st {
    BIGNUM *A;
    BIGNUM *Ai;
    BIGNUM *e;
    BIGNUM *mod;                /* just a reference */
    CRYPTO_THREAD_ID tid;
    int counter;
    unsigned long flags;
    BN_MONT_CTX *m_ctx;
    int (*bn_mod_exp) (BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
                       const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx);
    CRYPTO_RWLOCK *lock;
};

BN_BLINDING *BN_BLINDING_new(const BIGNUM *A, const BIGNUM *Ai, BIGNUM *mod)
{
    BN_BLINDING *ret = NULL;

    bn_check_top(mod);

    if ((ret = OPENSSL_zalloc(sizeof(*ret))) == NULL) {
        BNerr(BN_F_BN_BLINDING_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ret->lock = CRYPTO_THREAD_lock_new();
    if (ret->lock == NULL) {
        BNerr(BN_F_BN_BLINDING_NEW, ERR_R_MALLOC_FAILURE);
        OPENSSL_free(ret);
        return NULL;
    }

    BN_BLINDING_set_current_thread(ret);

    if (A != NULL) {
        if ((ret->A = BN_dup(A)) == NULL)
            goto err;
    }

    if (Ai != NULL) {
        if ((ret->Ai = BN_dup(Ai)) == NULL)
            goto err;
    }

    /* save a copy of mod in the BN_BLINDING structure */
    if ((ret->mod = BN_dup(mod)) == NULL)
        goto err;

    if (BN_get_flags(mod, BN_FLG_CONSTTIME) != 0)
        BN_set_flags(ret->mod, BN_FLG_CONSTTIME);

    /*
     * Set the counter to the special value -1 to indicate that this is
     * never-used fresh blinding that does not need updating before first
     * use.
     */
    ret->counter = -1;

    return ret;

 err:
    BN_BLINDING_free(ret);
    return NULL;
}

void BN_BLINDING_free(BN_BLINDING *r)
{
    if (r == NULL)
        return;
    BN_free(r->A);
    BN_free(r->Ai);
    BN_free(r->e);
    BN_free(r->mod);
    CRYPTO_THREAD_lock_free(r->lock);
    OPENSSL_free(r);
}

int BN_BLINDING_update(BN_BLINDING *b, BN_CTX *ctx)
{
    int ret = 0;

    if ((b->A == NULL) || (b->Ai == NULL)) {
        BNerr(BN_F_BN_BLINDING_UPDATE, BN_R_NOT_INITIALIZED);
        goto err;
    }

    if (b->counter == -1)
        b->counter = 0;

    if (++b->counter == BN_BLINDING_COUNTER && b->e != NULL &&
        !(b->flags & BN_BLINDING_NO_RECREATE)) {
        /* re-create blinding parameters */
        if (!BN_BLINDING_create_param(b, NULL, NULL, ctx, NULL, NULL))
            goto err;
    } else if (!(b->flags & BN_BLINDING_NO_UPDATE)) {
        if (b->m_ctx != NULL) {
            if (!bn_mul_mont_fixed_top(b->Ai, b->Ai, b->Ai, b->m_ctx, ctx)
                || !bn_mul_mont_fixed_top(b->A, b->A, b->A, b->m_ctx, ctx))
                goto err;
        } else {
            if (!BN_mod_mul(b->Ai, b->Ai, b->Ai, b->mod, ctx)
                || !BN_mod_mul(b->A, b->A, b->A, b->mod, ctx))
                goto err;
        }
    }

    ret = 1;
 err:
    if (b->counter == BN_BLINDING_COUNTER)
        b->counter = 0;
    return ret;
}

int BN_BLINDING_convert(BIGNUM *n, BN_BLINDING *b, BN_CTX *ctx)
{
    return BN_BLINDING_convert_ex(n, NULL, b, ctx);
}

int BN_BLINDING_convert_ex(BIGNUM *n, BIGNUM *r, BN_BLINDING *b, BN_CTX *ctx)
{
    int ret = 1;

    bn_check_top(n);

    if ((b->A == NULL) || (b->Ai == NULL)) {
        BNerr(BN_F_BN_BLINDING_CONVERT_EX, BN_R_NOT_INITIALIZED);
        return 0;
    }

    if (b->counter == -1)
        /* Fresh blinding, doesn't need updating. */
        b->counter = 0;
    else if (!BN_BLINDING_update(b, ctx))
        return 0;

    if (r != NULL && (BN_copy(r, b->Ai) == NULL))
        return 0;

    if (b->m_ctx != NULL)
        ret = BN_mod_mul_montgomery(n, n, b->A, b->m_ctx, ctx);
    else
        ret = BN_mod_mul(n, n, b->A, b->mod, ctx);

    return ret;
}

int BN_BLINDING_invert(BIGNUM *n, BN_BLINDING *b, BN_CTX *ctx)
{
    return BN_BLINDING_invert_ex(n, NULL, b, ctx);
}

int BN_BLINDING_invert_ex(BIGNUM *n, const BIGNUM *r, BN_BLINDING *b,
                          BN_CTX *ctx)
{
    int ret;

    bn_check_top(n);

    if (r == NULL && (r = b->Ai) == NULL) {
        BNerr(BN_F_BN_BLINDING_INVERT_EX, BN_R_NOT_INITIALIZED);
        return 0;
    }

    if (b->m_ctx != NULL) {
        /* ensure that BN_mod_mul_montgomery takes pre-defined path */
        if (n->dmax >= r->top) {
            size_t i, rtop = r->top, ntop = n->top;
            BN_ULONG mask;

            for (i = 0; i < rtop; i++) {
                mask = (BN_ULONG)0 - ((i - ntop) >> (8 * sizeof(i) - 1));
                n->d[i] &= mask;
            }
            mask = (BN_ULONG)0 - ((rtop - ntop) >> (8 * sizeof(ntop) - 1));
            /* always true, if (rtop >= ntop) n->top = r->top; */
            n->top = (int)(rtop & ~mask) | (ntop & mask);
            n->flags |= (BN_FLG_FIXED_TOP & ~mask);
        }
        ret = BN_mod_mul_montgomery(n, n, r, b->m_ctx, ctx);
    } else {
        ret = BN_mod_mul(n, n, r, b->mod, ctx);
    }

    bn_check_top(n);
    return ret;
}

int BN_BLINDING_is_current_thread(BN_BLINDING *b)
{
    return CRYPTO_THREAD_compare_id(CRYPTO_THREAD_get_current_id(), b->tid);
}

void BN_BLINDING_set_current_thread(BN_BLINDING *b)
{
    b->tid = CRYPTO_THREAD_get_current_id();
}

int BN_BLINDING_lock(BN_BLINDING *b)
{
    return CRYPTO_THREAD_write_lock(b->lock);
}

int BN_BLINDING_unlock(BN_BLINDING *b)
{
    return CRYPTO_THREAD_unlock(b->lock);
}

unsigned long BN_BLINDING_get_flags(const BN_BLINDING *b)
{
    return b->flags;
}

void BN_BLINDING_set_flags(BN_BLINDING *b, unsigned long flags)
{
    b->flags = flags;
}

BN_BLINDING *BN_BLINDING_create_param(BN_BLINDING *b,
                                      const BIGNUM *e, BIGNUM *m, BN_CTX *ctx,
                                      int (*bn_mod_exp) (BIGNUM *r,
                                                         const BIGNUM *a,
                                                         const BIGNUM *p,
                                                         const BIGNUM *m,
                                                         BN_CTX *ctx,
                                                         BN_MONT_CTX *m_ctx),
                                      BN_MONT_CTX *m_ctx)
{
    int retry_counter = 32;
    BN_BLINDING *ret = NULL;

    if (b == NULL)
        ret = BN_BLINDING_new(NULL, NULL, m);
    else
        ret = b;

    if (ret == NULL)
        goto err;

    if (ret->A == NULL && (ret->A = BN_new()) == NULL)
        goto err;
    if (ret->Ai == NULL && (ret->Ai = BN_new()) == NULL)
        goto err;

    if (e != NULL) {
        BN_free(ret->e);
        ret->e = BN_dup(e);
    }
    if (ret->e == NULL)
        goto err;

    if (bn_mod_exp != NULL)
        ret->bn_mod_exp = bn_mod_exp;
    if (m_ctx != NULL)
        ret->m_ctx = m_ctx;

    do {
        int rv;
        if (!BN_priv_rand_range(ret->A, ret->mod))
            goto err;
        if (int_bn_mod_inverse(ret->Ai, ret->A, ret->mod, ctx, &rv))
            break;

        /*
         * this should almost never happen for good RSA keys
         */
        if (!rv)
            goto err;

        if (retry_counter-- == 0) {
            BNerr(BN_F_BN_BLINDING_CREATE_PARAM, BN_R_TOO_MANY_ITERATIONS);
            goto err;
        }
    } while (1);

    if (ret->bn_mod_exp != NULL && ret->m_ctx != NULL) {
        if (!ret->bn_mod_exp(ret->A, ret->A, ret->e, ret->mod, ctx, ret->m_ctx))
            goto err;
    } else {
        if (!BN_mod_exp(ret->A, ret->A, ret->e, ret->mod, ctx))
            goto err;
    }

    if (ret->m_ctx != NULL) {
        if (!bn_to_mont_fixed_top(ret->Ai, ret->Ai, ret->m_ctx, ctx)
            || !bn_to_mont_fixed_top(ret->A, ret->A, ret->m_ctx, ctx))
            goto err;
    }

    return ret;
 err:
    if (b == NULL) {
        BN_BLINDING_free(ret);
        ret = NULL;
    }

    return ret;
}
