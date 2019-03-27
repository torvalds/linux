/*
 * Copyright 2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef HEADER_NELEM_H
# define HEADER_NELEM_H

# define OSSL_NELEM(x)    (sizeof(x)/sizeof((x)[0]))
#endif
