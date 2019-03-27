/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* This must be the first #include file */
#include "../async_locl.h"

#ifdef ASYNC_NULL
int ASYNC_is_capable(void)
{
    return 0;
}

void async_local_cleanup(void)
{
}
#endif

