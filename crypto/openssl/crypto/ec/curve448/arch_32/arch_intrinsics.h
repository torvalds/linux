/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2016 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */

#ifndef HEADER_ARCH_32_ARCH_INTRINSICS_H
# define HEADER_ARCH_32_ARCH_INTRINSICS_H

#include "internal/constant_time_locl.h"

# define ARCH_WORD_BITS 32

#define word_is_zero(a)     constant_time_is_zero_32(a)

static ossl_inline uint64_t widemul(uint32_t a, uint32_t b)
{
    return ((uint64_t)a) * b;
}

#endif                          /* HEADER_ARCH_32_ARCH_INTRINSICS_H */
