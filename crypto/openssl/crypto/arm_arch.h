/*
 * Copyright 2011-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef __ARM_ARCH_H__
# define __ARM_ARCH_H__

# if !defined(__ARM_ARCH__)
#  if defined(__CC_ARM)
#   define __ARM_ARCH__ __TARGET_ARCH_ARM
#   if defined(__BIG_ENDIAN)
#    define __ARMEB__
#   else
#    define __ARMEL__
#   endif
#  elif defined(__GNUC__)
#   if   defined(__aarch64__)
#    define __ARM_ARCH__ 8
#    if __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
#     define __ARMEB__
#    else
#     define __ARMEL__
#    endif
  /*
   * Why doesn't gcc define __ARM_ARCH__? Instead it defines
   * bunch of below macros. See all_architectures[] table in
   * gcc/config/arm/arm.c. On a side note it defines
   * __ARMEL__/__ARMEB__ for little-/big-endian.
   */
#   elif defined(__ARM_ARCH)
#    define __ARM_ARCH__ __ARM_ARCH
#   elif defined(__ARM_ARCH_8A__)
#    define __ARM_ARCH__ 8
#   elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__)     || \
        defined(__ARM_ARCH_7R__)|| defined(__ARM_ARCH_7M__)     || \
        defined(__ARM_ARCH_7EM__)
#    define __ARM_ARCH__ 7
#   elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__)     || \
        defined(__ARM_ARCH_6K__)|| defined(__ARM_ARCH_6M__)     || \
        defined(__ARM_ARCH_6Z__)|| defined(__ARM_ARCH_6ZK__)    || \
        defined(__ARM_ARCH_6T2__)
#    define __ARM_ARCH__ 6
#   elif defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5T__)     || \
        defined(__ARM_ARCH_5E__)|| defined(__ARM_ARCH_5TE__)    || \
        defined(__ARM_ARCH_5TEJ__)
#    define __ARM_ARCH__ 5
#   elif defined(__ARM_ARCH_4__) || defined(__ARM_ARCH_4T__)
#    define __ARM_ARCH__ 4
#   else
#    error "unsupported ARM architecture"
#   endif
#  endif
# endif

# if !defined(__ARM_MAX_ARCH__)
#  define __ARM_MAX_ARCH__ __ARM_ARCH__
# endif

# if __ARM_MAX_ARCH__<__ARM_ARCH__
#  error "__ARM_MAX_ARCH__ can't be less than __ARM_ARCH__"
# elif __ARM_MAX_ARCH__!=__ARM_ARCH__
#  if __ARM_ARCH__<7 && __ARM_MAX_ARCH__>=7 && defined(__ARMEB__)
#   error "can't build universal big-endian binary"
#  endif
# endif

# ifndef __ASSEMBLER__
extern unsigned int OPENSSL_armcap_P;
# endif

# define ARMV7_NEON      (1<<0)
# define ARMV7_TICK      (1<<1)
# define ARMV8_AES       (1<<2)
# define ARMV8_SHA1      (1<<3)
# define ARMV8_SHA256    (1<<4)
# define ARMV8_PMULL     (1<<5)
# define ARMV8_SHA512    (1<<6)

#endif
