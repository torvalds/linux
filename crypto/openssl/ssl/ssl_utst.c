/*
 * Copyright 2014-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "ssl_locl.h"

#ifndef OPENSSL_NO_UNIT_TEST

static const struct openssl_ssl_test_functions ssl_test_functions = {
    ssl_init_wbio_buffer,
    ssl3_setup_buffers,
};

const struct openssl_ssl_test_functions *SSL_test_functions(void)
{
    return &ssl_test_functions;
}

#endif
