/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2014 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */

#ifndef HEADER_WORD_H
# define HEADER_WORD_H

# include <string.h>
# include <assert.h>
# include <stdlib.h>
# include <openssl/e_os2.h>
# include "arch_intrinsics.h"
# include "curve448utils.h"

# if (ARCH_WORD_BITS == 64)
typedef uint64_t word_t, mask_t;
typedef __uint128_t dword_t;
typedef int32_t hsword_t;
typedef int64_t sword_t;
typedef __int128_t dsword_t;
# elif (ARCH_WORD_BITS == 32)
typedef uint32_t word_t, mask_t;
typedef uint64_t dword_t;
typedef int16_t hsword_t;
typedef int32_t sword_t;
typedef int64_t dsword_t;
# else
#  error "For now, we only support 32- and 64-bit architectures."
# endif

/*
 * Scalar limbs are keyed off of the API word size instead of the arch word
 * size.
 */
# if C448_WORD_BITS == 64
#  define SC_LIMB(x) (x)
# elif C448_WORD_BITS == 32
#  define SC_LIMB(x) ((uint32_t)(x)),((x) >> 32)
# else
#  error "For now we only support 32- and 64-bit architectures."
# endif

/*
 * The plan on booleans: The external interface uses c448_bool_t, but this
 * might be a different size than our particular arch's word_t (and thus
 * mask_t).  Also, the caller isn't guaranteed to pass it as nonzero.  So
 * bool_to_mask converts word sizes and checks nonzero. On the flip side,
 * mask_t is always -1 or 0, but it might be a different size than
 * c448_bool_t. On the third hand, we have success vs boolean types, but
 * that's handled in common.h: it converts between c448_bool_t and
 * c448_error_t.
 */
static ossl_inline c448_bool_t mask_to_bool(mask_t m)
{
    return (c448_sword_t)(sword_t)m;
}

static ossl_inline mask_t bool_to_mask(c448_bool_t m)
{
    /* On most arches this will be optimized to a simple cast. */
    mask_t ret = 0;
    unsigned int i;
    unsigned int limit = sizeof(c448_bool_t) / sizeof(mask_t);

    if (limit < 1)
        limit = 1;
    for (i = 0; i < limit; i++)
        ret |= ~word_is_zero(m >> (i * 8 * sizeof(word_t)));

    return ret;
}

#endif                          /* HEADER_WORD_H */
