/*
 * Copyright 2015-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/async.h>

/*
 * If we haven't managed to detect any other async architecture then we default
 * to NULL.
 */
#ifndef ASYNC_ARCH
# define ASYNC_NULL
# define ASYNC_ARCH

typedef struct async_fibre_st {
    int dummy;
} async_fibre;


# define async_fibre_swapcontext(o,n,r)         0
# define async_fibre_makecontext(c)             0
# define async_fibre_free(f)
# define async_fibre_init_dispatcher(f)

#endif
