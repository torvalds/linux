/*
 * Copyright 2002-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Support for deprecated functions goes here - static linkage will only
 * slurp this code if applications are using them directly.
 */

#include <openssl/opensslconf.h>
#if OPENSSL_API_COMPAT >= 0x00908000L
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include <time.h>
# include "internal/cryptlib.h"
# include "bn_lcl.h"

BIGNUM *BN_generate_prime(BIGNUM *ret, int bits, int safe,
                          const BIGNUM *add, const BIGNUM *rem,
                          void (*callback) (int, int, void *), void *cb_arg)
{
    BN_GENCB cb;
    BIGNUM *rnd = NULL;

    BN_GENCB_set_old(&cb, callback, cb_arg);

    if (ret == NULL) {
        if ((rnd = BN_new()) == NULL)
            goto err;
    } else
        rnd = ret;
    if (!BN_generate_prime_ex(rnd, bits, safe, add, rem, &cb))
        goto err;

    /* we have a prime :-) */
    return rnd;
 err:
    BN_free(rnd);
    return NULL;
}

int BN_is_prime(const BIGNUM *a, int checks,
                void (*callback) (int, int, void *), BN_CTX *ctx_passed,
                void *cb_arg)
{
    BN_GENCB cb;
    BN_GENCB_set_old(&cb, callback, cb_arg);
    return BN_is_prime_ex(a, checks, ctx_passed, &cb);
}

int BN_is_prime_fasttest(const BIGNUM *a, int checks,
                         void (*callback) (int, int, void *),
                         BN_CTX *ctx_passed, void *cb_arg,
                         int do_trial_division)
{
    BN_GENCB cb;
    BN_GENCB_set_old(&cb, callback, cb_arg);
    return BN_is_prime_fasttest_ex(a, checks, ctx_passed,
                                   do_trial_division, &cb);
}
#endif
