/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright 2015 Cryptography Research, Inc.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 *
 * Originally written by Mike Hamburg
 */

#ifndef HEADER_CURVE448UTILS_H
# define HEADER_CURVE448UTILS_H

# include <openssl/e_os2.h>

/*
 * Internal word types. Somewhat tricky.  This could be decided separately per
 * platform.  However, the structs do need to be all the same size and
 * alignment on a given platform to support dynamic linking, since even if you
 * header was built with eg arch_neon, you might end up linking a library built
 * with arch_arm32.
 */
# ifndef C448_WORD_BITS
#  if (defined(__SIZEOF_INT128__) && (__SIZEOF_INT128__ == 16)) \
      && !defined(__sparc__)
#   define C448_WORD_BITS 64      /* The number of bits in a word */
#  else
#   define C448_WORD_BITS 32      /* The number of bits in a word */
#  endif
# endif

# if C448_WORD_BITS == 64
/* Word size for internal computations */
typedef uint64_t c448_word_t;
/* Signed word size for internal computations */
typedef int64_t c448_sword_t;
/* "Boolean" type, will be set to all-zero or all-one (i.e. -1u) */
typedef uint64_t c448_bool_t;
/* Double-word size for internal computations */
typedef __uint128_t c448_dword_t;
/* Signed double-word size for internal computations */
typedef __int128_t c448_dsword_t;
# elif C448_WORD_BITS == 32
/* Word size for internal computations */
typedef uint32_t c448_word_t;
/* Signed word size for internal computations */
typedef int32_t c448_sword_t;
/* "Boolean" type, will be set to all-zero or all-one (i.e. -1u) */
typedef uint32_t c448_bool_t;
/* Double-word size for internal computations */
typedef uint64_t c448_dword_t;
/* Signed double-word size for internal computations */
typedef int64_t c448_dsword_t;
# else
#  error "Only supporting C448_WORD_BITS = 32 or 64 for now"
# endif

/* C448_TRUE = -1 so that C448_TRUE & x = x */
# define C448_TRUE      (0 - (c448_bool_t)1)

/* C448_FALSE = 0 so that C448_FALSE & x = 0 */
# define C448_FALSE     0

/* Another boolean type used to indicate success or failure. */
typedef enum {
    C448_SUCCESS = -1, /**< The operation succeeded. */
    C448_FAILURE = 0   /**< The operation failed. */
} c448_error_t;

/* Return success if x is true */
static ossl_inline c448_error_t c448_succeed_if(c448_bool_t x)
{
    return (c448_error_t) x;
}

#endif                          /* __C448_COMMON_H__ */
