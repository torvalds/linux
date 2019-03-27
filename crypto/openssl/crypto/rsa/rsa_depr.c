/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * NB: This file contains deprecated functions (compatibility wrappers to the
 * "new" versions).
 */

#include <openssl/opensslconf.h>
#if OPENSSL_API_COMPAT >= 0x00908000L
NON_EMPTY_TRANSLATION_UNIT

#else

# include <stdio.h>
# include <time.h>
# include "internal/cryptlib.h"
# include <openssl/bn.h>
# include <openssl/rsa.h>

RSA *RSA_generate_key(int bits, unsigned long e_value,
                      void (*callback) (int, int, void *), void *cb_arg)
{
    int i;
    BN_GENCB *cb = BN_GENCB_new();
    RSA *rsa = RSA_new();
    BIGNUM *e = BN_new();

    if (cb == NULL || rsa == NULL || e == NULL)
        goto err;

    /*
     * The problem is when building with 8, 16, or 32 BN_ULONG, unsigned long
     * can be larger
     */
    for (i = 0; i < (int)sizeof(unsigned long) * 8; i++) {
        if (e_value & (1UL << i))
            if (BN_set_bit(e, i) == 0)
                goto err;
    }

    BN_GENCB_set_old(cb, callback, cb_arg);

    if (RSA_generate_key_ex(rsa, bits, e, cb)) {
        BN_free(e);
        BN_GENCB_free(cb);
        return rsa;
    }
 err:
    BN_free(e);
    RSA_free(rsa);
    BN_GENCB_free(cb);
    return 0;
}
#endif
