/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslv.h>
#include <openssl/aes.h>
#include "aes_locl.h"

const char *AES_options(void)
{
#ifdef FULL_UNROLL
    return "aes(full)";
#else
    return "aes(partial)";
#endif
}
