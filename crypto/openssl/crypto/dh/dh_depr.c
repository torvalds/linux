/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* This file contains deprecated functions as wrappers to the new ones */

#include <openssl/opensslconf.h>
#if OPENSSL_API_COMPAT >= 0x00908000L
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include "internal/cryptlib.h"
# include <openssl/bn.h>
# include <openssl/dh.h>

DH *DH_generate_parameters(int prime_len, int generator,
                           void (*callback) (int, int, void *), void *cb_arg)
{
    BN_GENCB *cb;
    DH *ret = NULL;

    if ((ret = DH_new()) == NULL)
        return NULL;
    cb = BN_GENCB_new();
    if (cb == NULL) {
        DH_free(ret);
        return NULL;
    }

    BN_GENCB_set_old(cb, callback, cb_arg);

    if (DH_generate_parameters_ex(ret, prime_len, generator, cb)) {
        BN_GENCB_free(cb);
        return ret;
    }
    BN_GENCB_free(cb);
    DH_free(ret);
    return NULL;
}
#endif
