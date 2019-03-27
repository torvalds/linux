/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <time.h>
#include "internal/cryptlib.h"
#include <openssl/bn.h>
#include "dsa_locl.h"

static int dsa_builtin_keygen(DSA *dsa);

int DSA_generate_key(DSA *dsa)
{
    if (dsa->meth->dsa_keygen)
        return dsa->meth->dsa_keygen(dsa);
    return dsa_builtin_keygen(dsa);
}

static int dsa_builtin_keygen(DSA *dsa)
{
    int ok = 0;
    BN_CTX *ctx = NULL;
    BIGNUM *pub_key = NULL, *priv_key = NULL;

    if ((ctx = BN_CTX_new()) == NULL)
        goto err;

    if (dsa->priv_key == NULL) {
        if ((priv_key = BN_secure_new()) == NULL)
            goto err;
    } else
        priv_key = dsa->priv_key;

    do
        if (!BN_priv_rand_range(priv_key, dsa->q))
            goto err;
    while (BN_is_zero(priv_key)) ;

    if (dsa->pub_key == NULL) {
        if ((pub_key = BN_new()) == NULL)
            goto err;
    } else
        pub_key = dsa->pub_key;

    {
        BIGNUM *prk = BN_new();

        if (prk == NULL)
            goto err;
        BN_with_flags(prk, priv_key, BN_FLG_CONSTTIME);

        if (!BN_mod_exp(pub_key, dsa->g, prk, dsa->p, ctx)) {
            BN_free(prk);
            goto err;
        }
        /* We MUST free prk before any further use of priv_key */
        BN_free(prk);
    }

    dsa->priv_key = priv_key;
    dsa->pub_key = pub_key;
    ok = 1;

 err:
    if (pub_key != dsa->pub_key)
        BN_free(pub_key);
    if (priv_key != dsa->priv_key)
        BN_free(priv_key);
    BN_CTX_free(ctx);
    return ok;
}
