/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include "dsa_locl.h"

int DSA_do_verify(const unsigned char *dgst, int dgst_len, DSA_SIG *sig,
                  DSA *dsa)
{
    return dsa->meth->dsa_do_verify(dgst, dgst_len, sig, dsa);
}
