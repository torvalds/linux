/*
 * Copyright (C) 2017 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
/* Interface glue between bbox code and minimally tweaked matrixssl
 * code. All C files (matrixssl and bbox (ones which need TLS))
 * include this file, and guaranteed to see a consistent API,
 * defines, types, etc.
 */
#include "libbb.h"


/* Config tweaks */
#define HAVE_NATIVE_INT64
#undef  USE_1024_KEY_SPEED_OPTIMIZATIONS
#undef  USE_2048_KEY_SPEED_OPTIMIZATIONS
#define USE_AES
#undef  USE_AES_CBC_EXTERNAL
#undef  USE_AES_CCM
#undef  USE_AES_GCM
#undef  USE_3DES
#undef  USE_ARC4
#undef  USE_IDEA
#undef  USE_RC2
#undef  USE_SEED
/* pstm: multiprecision numbers */
#undef  DISABLE_PSTM
#if defined(__GNUC__) && defined(__i386__)
  /* PSTM_X86 works correctly. +25 bytes. */
# define PSTM_32BIT
# define PSTM_X86
#endif
//#if defined(__GNUC__) && defined(__x86_64__)
//  /* PSTM_X86_64 works correctly, but +782 bytes. */
//  /* Looks like most of the growth is because of PSTM_64BIT. */
//# define PSTM_64BIT
//# define PSTM_X86_64
//#endif
//#if SOME_COND #define PSTM_MIPS, #define PSTM_32BIT
//#if SOME_COND #define PSTM_ARM,  #define PSTM_32BIT


#define PS_SUCCESS              0
#define PS_FAILURE              -1
#define PS_ARG_FAIL             -6      /* Failure due to bad function param */
#define PS_PLATFORM_FAIL        -7      /* Failure as a result of system call error */
#define PS_MEM_FAIL             -8      /* Failure to allocate requested memory */
#define PS_LIMIT_FAIL           -9      /* Failure on sanity/limit tests */

#define PS_TRUE         1
#define PS_FALSE        0

#if BB_BIG_ENDIAN
# define ENDIAN_BIG     1
# undef  ENDIAN_LITTLE
//#????  ENDIAN_32BITWORD
// controls only STORE32L, which we don't use
#else
# define ENDIAN_LITTLE  1
# undef  ENDIAN_BIG
#endif

typedef uint64_t uint64;
typedef  int64_t  int64;
typedef uint32_t uint32;
typedef  int32_t  int32;
typedef uint16_t uint16;
typedef  int16_t  int16;

//typedef char psPool_t;

//#ifdef PS_PUBKEY_OPTIMIZE_FOR_SMALLER_RAM
#define PS_EXPTMOD_WINSIZE   3
//#ifdef PS_PUBKEY_OPTIMIZE_FOR_FASTER_SPEED
//#define PS_EXPTMOD_WINSIZE 5

#define PUBKEY_TYPE     0x01
#define PRIVKEY_TYPE    0x02

void tls_get_random(void *buf, unsigned len);

#define matrixCryptoGetPrngData(buf, len, userPtr) (tls_get_random(buf, len), PS_SUCCESS)

#define psFree(p, pool)    free(p)
#define psTraceCrypto(...) bb_error_msg_and_die(__VA_ARGS__)

/* Secure zerofill */
#define memset_s(A,B,C,D) memset((A),(C),(D))
/* Constant time memory comparison */
#define memcmpct(s1, s2, len) memcmp((s1), (s2), (len))
#undef  min
#define min(x, y) ((x) < (y) ? (x) : (y))


#include "tls_pstm.h"
#include "tls_rsa.h"
#include "tls_symmetric.h"
#include "tls_aes.h"
