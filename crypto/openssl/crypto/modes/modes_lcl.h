/*
 * Copyright 2010-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/modes.h>

#if (defined(_WIN32) || defined(_WIN64)) && !defined(__MINGW32__)
typedef __int64 i64;
typedef unsigned __int64 u64;
# define U64(C) C##UI64
#elif defined(__arch64__)
typedef long i64;
typedef unsigned long u64;
# define U64(C) C##UL
#else
typedef long long i64;
typedef unsigned long long u64;
# define U64(C) C##ULL
#endif

typedef unsigned int u32;
typedef unsigned char u8;

#define STRICT_ALIGNMENT 1
#ifndef PEDANTIC
# if defined(__i386)    || defined(__i386__)    || \
     defined(__x86_64)  || defined(__x86_64__)  || \
     defined(_M_IX86)   || defined(_M_AMD64)    || defined(_M_X64) || \
     defined(__aarch64__)                       || \
     defined(__s390__)  || defined(__s390x__)
#  undef STRICT_ALIGNMENT
# endif
#endif

#if !defined(PEDANTIC) && !defined(OPENSSL_NO_ASM) && !defined(OPENSSL_NO_INLINE_ASM)
# if defined(__GNUC__) && __GNUC__>=2
#  if defined(__x86_64) || defined(__x86_64__)
#   define BSWAP8(x) ({ u64 ret_=(x);                   \
                        asm ("bswapq %0"                \
                        : "+r"(ret_));   ret_;          })
#   define BSWAP4(x) ({ u32 ret_=(x);                   \
                        asm ("bswapl %0"                \
                        : "+r"(ret_));   ret_;          })
#  elif (defined(__i386) || defined(__i386__)) && !defined(I386_ONLY)
#   define BSWAP8(x) ({ u32 lo_=(u64)(x)>>32,hi_=(x);   \
                        asm ("bswapl %0; bswapl %1"     \
                        : "+r"(hi_),"+r"(lo_));         \
                        (u64)hi_<<32|lo_;               })
#   define BSWAP4(x) ({ u32 ret_=(x);                   \
                        asm ("bswapl %0"                \
                        : "+r"(ret_));   ret_;          })
#  elif defined(__aarch64__)
#   define BSWAP8(x) ({ u64 ret_;                       \
                        asm ("rev %0,%1"                \
                        : "=r"(ret_) : "r"(x)); ret_;   })
#   define BSWAP4(x) ({ u32 ret_;                       \
                        asm ("rev %w0,%w1"              \
                        : "=r"(ret_) : "r"(x)); ret_;   })
#  elif (defined(__arm__) || defined(__arm)) && !defined(STRICT_ALIGNMENT)
#   define BSWAP8(x) ({ u32 lo_=(u64)(x)>>32,hi_=(x);   \
                        asm ("rev %0,%0; rev %1,%1"     \
                        : "+r"(hi_),"+r"(lo_));         \
                        (u64)hi_<<32|lo_;               })
#   define BSWAP4(x) ({ u32 ret_;                       \
                        asm ("rev %0,%1"                \
                        : "=r"(ret_) : "r"((u32)(x)));  \
                        ret_;                           })
#  endif
# elif defined(_MSC_VER)
#  if _MSC_VER>=1300
#   include <stdlib.h>
#   pragma intrinsic(_byteswap_uint64,_byteswap_ulong)
#   define BSWAP8(x)    _byteswap_uint64((u64)(x))
#   define BSWAP4(x)    _byteswap_ulong((u32)(x))
#  elif defined(_M_IX86)
__inline u32 _bswap4(u32 val)
{
_asm mov eax, val _asm bswap eax}
#   define BSWAP4(x)    _bswap4(x)
#  endif
# endif
#endif
#if defined(BSWAP4) && !defined(STRICT_ALIGNMENT)
# define GETU32(p)       BSWAP4(*(const u32 *)(p))
# define PUTU32(p,v)     *(u32 *)(p) = BSWAP4(v)
#else
# define GETU32(p)       ((u32)(p)[0]<<24|(u32)(p)[1]<<16|(u32)(p)[2]<<8|(u32)(p)[3])
# define PUTU32(p,v)     ((p)[0]=(u8)((v)>>24),(p)[1]=(u8)((v)>>16),(p)[2]=(u8)((v)>>8),(p)[3]=(u8)(v))
#endif
/*- GCM definitions */ typedef struct {
    u64 hi, lo;
} u128;

#ifdef  TABLE_BITS
# undef  TABLE_BITS
#endif
/*
 * Even though permitted values for TABLE_BITS are 8, 4 and 1, it should
 * never be set to 8 [or 1]. For further information see gcm128.c.
 */
#define TABLE_BITS 4

struct gcm128_context {
    /* Following 6 names follow names in GCM specification */
    union {
        u64 u[2];
        u32 d[4];
        u8 c[16];
        size_t t[16 / sizeof(size_t)];
    } Yi, EKi, EK0, len, Xi, H;
    /*
     * Relative position of Xi, H and pre-computed Htable is used in some
     * assembler modules, i.e. don't change the order!
     */
#if TABLE_BITS==8
    u128 Htable[256];
#else
    u128 Htable[16];
    void (*gmult) (u64 Xi[2], const u128 Htable[16]);
    void (*ghash) (u64 Xi[2], const u128 Htable[16], const u8 *inp,
                   size_t len);
#endif
    unsigned int mres, ares;
    block128_f block;
    void *key;
#if !defined(OPENSSL_SMALL_FOOTPRINT)
    unsigned char Xn[48];
#endif
};

struct xts128_context {
    void *key1, *key2;
    block128_f block1, block2;
};

struct ccm128_context {
    union {
        u64 u[2];
        u8 c[16];
    } nonce, cmac;
    u64 blocks;
    block128_f block;
    void *key;
};

#ifndef OPENSSL_NO_OCB

typedef union {
    u64 a[2];
    unsigned char c[16];
} OCB_BLOCK;
# define ocb_block16_xor(in1,in2,out) \
    ( (out)->a[0]=(in1)->a[0]^(in2)->a[0], \
      (out)->a[1]=(in1)->a[1]^(in2)->a[1] )
# if STRICT_ALIGNMENT
#  define ocb_block16_xor_misaligned(in1,in2,out) \
    ocb_block_xor((in1)->c,(in2)->c,16,(out)->c)
# else
#  define ocb_block16_xor_misaligned ocb_block16_xor
# endif

struct ocb128_context {
    /* Need both encrypt and decrypt key schedules for decryption */
    block128_f encrypt;
    block128_f decrypt;
    void *keyenc;
    void *keydec;
    ocb128_f stream;    /* direction dependent */
    /* Key dependent variables. Can be reused if key remains the same */
    size_t l_index;
    size_t max_l_index;
    OCB_BLOCK l_star;
    OCB_BLOCK l_dollar;
    OCB_BLOCK *l;
    /* Must be reset for each session */
    struct {
        u64 blocks_hashed;
        u64 blocks_processed;
        OCB_BLOCK offset_aad;
        OCB_BLOCK sum;
        OCB_BLOCK offset;
        OCB_BLOCK checksum;
    } sess;
};
#endif                          /* OPENSSL_NO_OCB */
