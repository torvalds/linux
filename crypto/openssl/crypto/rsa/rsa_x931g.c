/*
 * Copyright 1995-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <openssl/err.h>
#include <openssl/bn.h>
#include "rsa_locl.h"

/* X9.31 RSA key derivation and generation */

int RSA_X931_derive_ex(RSA *rsa, BIGNUM *p1, BIGNUM *p2, BIGNUM *q1,
                       BIGNUM *q2, const BIGNUM *Xp1, const BIGNUM *Xp2,
                       const BIGNUM *Xp, const BIGNUM *Xq1, const BIGNUM *Xq2,
                       const BIGNUM *Xq, const BIGNUM *e, BN_GENCB *cb)
{
    BIGNUM *r0 = NULL, *r1 = NULL, *r2 = NULL, *r3 = NULL;
    BN_CTX *ctx = NULL, *ctx2 = NULL;
    int ret = 0;

    if (!rsa)
        goto err;

    ctx = BN_CTX_new();
    if (ctx == NULL)
        goto err;
    BN_CTX_start(ctx);

    r0 = BN_CTX_get(ctx);
    r1 = BN_CTX_get(ctx);
    r2 = BN_CTX_get(ctx);
    r3 = BN_CTX_get(ctx);

    if (r3 == NULL)
        goto err;
    if (!rsa->e) {
        rsa->e = BN_dup(e);
        if (!rsa->e)
            goto err;
    } else {
        e = rsa->e;
    }

    /*
     * If not all parameters present only calculate what we can. This allows
     * test programs to output selective parameters.
     */

    if (Xp && rsa->p == NULL) {
        rsa->p = BN_new();
        if (rsa->p == NULL)
            goto err;

        if (!BN_X931_derive_prime_ex(rsa->p, p1, p2,
                                     Xp, Xp1, Xp2, e, ctx, cb))
            goto err;
    }

    if (Xq && rsa->q == NULL) {
        rsa->q = BN_new();
        if (rsa->q == NULL)
            goto err;
        if (!BN_X931_derive_prime_ex(rsa->q, q1, q2,
                                     Xq, Xq1, Xq2, e, ctx, cb))
            goto err;
    }

    if (rsa->p == NULL || rsa->q == NULL) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        return 2;
    }

    /*
     * Since both primes are set we can now calculate all remaining
     * components.
     */

    /* calculate n */
    rsa->n = BN_new();
    if (rsa->n == NULL)
        goto err;
    if (!BN_mul(rsa->n, rsa->p, rsa->q, ctx))
        goto err;

    /* calculate d */
    if (!BN_sub(r1, rsa->p, BN_value_one()))
        goto err;               /* p-1 */
    if (!BN_sub(r2, rsa->q, BN_value_one()))
        goto err;               /* q-1 */
    if (!BN_mul(r0, r1, r2, ctx))
        goto err;               /* (p-1)(q-1) */

    if (!BN_gcd(r3, r1, r2, ctx))
        goto err;

    if (!BN_div(r0, NULL, r0, r3, ctx))
        goto err;               /* LCM((p-1)(q-1)) */

    ctx2 = BN_CTX_new();
    if (ctx2 == NULL)
        goto err;

    rsa->d = BN_mod_inverse(NULL, rsa->e, r0, ctx2); /* d */
    if (rsa->d == NULL)
        goto err;

    /* calculate d mod (p-1) */
    rsa->dmp1 = BN_new();
    if (rsa->dmp1 == NULL)
        goto err;
    if (!BN_mod(rsa->dmp1, rsa->d, r1, ctx))
        goto err;

    /* calculate d mod (q-1) */
    rsa->dmq1 = BN_new();
    if (rsa->dmq1 == NULL)
        goto err;
    if (!BN_mod(rsa->dmq1, rsa->d, r2, ctx))
        goto err;

    /* calculate inverse of q mod p */
    rsa->iqmp = BN_mod_inverse(NULL, rsa->q, rsa->p, ctx2);
    if (rsa->iqmp == NULL)
        goto err;

    ret = 1;
 err:
    if (ctx)
        BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    BN_CTX_free(ctx2);

    return ret;

}

int RSA_X931_generate_key_ex(RSA *rsa, int bits, const BIGNUM *e,
                             BN_GENCB *cb)
{
    int ok = 0;
    BIGNUM *Xp = NULL, *Xq = NULL;
    BN_CTX *ctx = NULL;

    ctx = BN_CTX_new();
    if (ctx == NULL)
        goto error;

    BN_CTX_start(ctx);
    Xp = BN_CTX_get(ctx);
    Xq = BN_CTX_get(ctx);
    if (Xq == NULL)
        goto error;
    if (!BN_X931_generate_Xpq(Xp, Xq, bits, ctx))
        goto error;

    rsa->p = BN_new();
    rsa->q = BN_new();
    if (rsa->p == NULL || rsa->q == NULL)
        goto error;

    /* Generate two primes from Xp, Xq */

    if (!BN_X931_generate_prime_ex(rsa->p, NULL, NULL, NULL, NULL, Xp,
                                   e, ctx, cb))
        goto error;

    if (!BN_X931_generate_prime_ex(rsa->q, NULL, NULL, NULL, NULL, Xq,
                                   e, ctx, cb))
        goto error;

    /*
     * Since rsa->p and rsa->q are valid this call will just derive remaining
     * RSA components.
     */

    if (!RSA_X931_derive_ex(rsa, NULL, NULL, NULL, NULL,
                            NULL, NULL, NULL, NULL, NULL, NULL, e, cb))
        goto error;

    ok = 1;

 error:
    if (ctx)
        BN_CTX_end(ctx);
    BN_CTX_free(ctx);

    if (ok)
        return 1;

    return 0;

}
