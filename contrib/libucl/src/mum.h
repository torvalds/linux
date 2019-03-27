/* Copyright (c) 2016 Vladimir Makarov <vmakarov@gcc.gnu.org>

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

/* This file implements MUM (MUltiply and Mix) hashing.  We randomize
   input data by 64x64-bit multiplication and mixing hi- and low-parts
   of the multiplication result by using an addition and then mix it
   into the current state.  We use prime numbers randomly generated
   with the equal probability of their bit values for the
   multiplication.  When all primes are used once, the state is
   randomized and the same prime numbers are used again for data
   randomization.

   The MUM hashing passes all SMHasher tests.  Pseudo Random Number
   Generator based on MUM also passes NIST Statistical Test Suite for
   Random and Pseudorandom Number Generators for Cryptographic
   Applications (version 2.2.1) with 1000 bitstreams each containing
   1M bits.  MUM hashing is also faster Spooky64 and City64 on small
   strings (at least upto 512-bit) on Haswell and Power7.  The MUM bulk
   speed (speed on very long data) is bigger than Spooky and City on
   Power7.  On Haswell the bulk speed is bigger than Spooky one and
   close to City speed.  */

#ifndef __MUM_HASH__
#define __MUM_HASH__

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _MSC_VER
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif

/* Macro saying to use 128-bit integers implemented by GCC for some
   targets.  */
#ifndef _MUM_USE_INT128
/* In GCC uint128_t is defined if HOST_BITS_PER_WIDE_INT >= 64.
   HOST_WIDE_INT is long if HOST_BITS_PER_LONG > HOST_BITS_PER_INT,
   otherwise int. */
#if defined(__GNUC__) && UINT_MAX != ULONG_MAX
#define _MUM_USE_INT128 1
#else
#define _MUM_USE_INT128 0
#endif
#endif

#if 0
#if defined(__GNUC__) && ((__GNUC__ == 4) &&  (__GNUC_MINOR__ >= 9) || (__GNUC__ > 4))
#define _MUM_FRESH_GCC
#endif
#endif

#if defined(__GNUC__) && !defined(__llvm__) && defined(_MUM_FRESH_GCC)
#define _MUM_ATTRIBUTE_UNUSED  __attribute__((unused))
#define _MUM_OPTIMIZE(opts) __attribute__((__optimize__ (opts)))
#define _MUM_TARGET(opts) __attribute__((__target__ (opts)))
#else
#define _MUM_ATTRIBUTE_UNUSED
#define _MUM_OPTIMIZE(opts)
#define _MUM_TARGET(opts)
#endif


/* Here are different primes randomly generated with the equal
   probability of their bit values.  They are used to randomize input
   values.  */
static uint64_t _mum_hash_step_prime = 0x2e0bb864e9ea7df5ULL;
static uint64_t _mum_key_step_prime = 0xcdb32970830fcaa1ULL;
static uint64_t _mum_block_start_prime = 0xc42b5e2e6480b23bULL;
static uint64_t _mum_unroll_prime = 0x7b51ec3d22f7096fULL;
static uint64_t _mum_tail_prime = 0xaf47d47c99b1461bULL;
static uint64_t _mum_finish_prime1 = 0xa9a7ae7ceff79f3fULL;
static uint64_t _mum_finish_prime2 = 0xaf47d47c99b1461bULL;

static uint64_t _mum_primes [] = {
  0X9ebdcae10d981691, 0X32b9b9b97a27ac7d, 0X29b5584d83d35bbd, 0X4b04e0e61401255f,
  0X25e8f7b1f1c9d027, 0X80d4c8c000f3e881, 0Xbd1255431904b9dd, 0X8a3bd4485eee6d81,
  0X3bc721b2aad05197, 0X71b1a19b907d6e33, 0X525e6c1084a8534b, 0X9e4c2cd340c1299f,
  0Xde3add92e94caa37, 0X7e14eadb1f65311d, 0X3f5aa40f89812853, 0X33b15a3b587d15c9,
};

/* Multiply 64-bit V and P and return sum of high and low parts of the
   result.  */
static inline uint64_t
_mum (uint64_t v, uint64_t p) {
  uint64_t hi, lo;
#if _MUM_USE_INT128
#if defined(__aarch64__)
  /* AARCH64 needs 2 insns to calculate 128-bit result of the
     multiplication.  If we use a generic code we actually call a
     function doing 128x128->128 bit multiplication.  The function is
     very slow.  */
  lo = v * p, hi;
  asm ("umulh %0, %1, %2" : "=r" (hi) : "r" (v), "r" (p));
#else
  __uint128_t r = (__uint128_t) v * (__uint128_t) p;
  hi = (uint64_t) (r >> 64);
  lo = (uint64_t) r;
#endif
#else
  /* Implementation of 64x64->128-bit multiplication by four 32x32->64
     bit multiplication.  */
  uint64_t hv = v >> 32, hp = p >> 32;
  uint64_t lv = (uint32_t) v, lp = (uint32_t) p;
  uint64_t rh =  hv * hp;
  uint64_t rm_0 = hv * lp;
  uint64_t rm_1 = hp * lv;
  uint64_t rl =  lv * lp;
  uint64_t t, carry = 0;

  /* We could ignore a carry bit here if we did not care about the
     same hash for 32-bit and 64-bit targets.  */
  t = rl + (rm_0 << 32);
#ifdef MUM_TARGET_INDEPENDENT_HASH
  carry = t < rl;
#endif
  lo = t + (rm_1 << 32);
#ifdef MUM_TARGET_INDEPENDENT_HASH
  carry += lo < t;
#endif
  hi = rh + (rm_0 >> 32) + (rm_1 >> 32) + carry;
#endif
  /* We could use XOR here too but, for some reasons, on Haswell and
     Power7 using an addition improves hashing performance by 10% for
     small strings.  */
  return hi + lo;
}

#if defined(_MSC_VER)
#define _mum_bswap_32(x) _byteswap_uint32_t (x)
#define _mum_bswap_64(x) _byteswap_uint64_t (x)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define _mum_bswap_32(x) OSSwapInt32 (x)
#define _mum_bswap_64(x) OSSwapInt64 (x)
#elif defined(__GNUC__)
#define _mum_bswap32(x) __builtin_bswap32 (x)
#define _mum_bswap64(x) __builtin_bswap64 (x)
#else
#include <byteswap.h>
#define _mum_bswap32(x) bswap32 (x)
#define _mum_bswap64(x) bswap64 (x)
#endif

static inline uint64_t
_mum_le (uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || !defined(MUM_TARGET_INDEPENDENT_HASH)
  return v;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return _mum_bswap64 (v);
#else
#error "Unknown endianess"
#endif
}

static inline uint32_t
_mum_le32 (uint32_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ || !defined(MUM_TARGET_INDEPENDENT_HASH)
  return v;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  return _mum_bswap32 (v);
#else
#error "Unknown endianess"
#endif
}

/* Macro defining how many times the most nested loop in
   _mum_hash_aligned will be unrolled by the compiler (although it can
   make an own decision:).  Use only a constant here to help a
   compiler to unroll a major loop.

   The macro value affects the result hash for strings > 128 bit.  The
   unroll factor greatly affects the hashing speed.  We prefer the
   speed.  */
#ifndef _MUM_UNROLL_FACTOR_POWER
#if defined(__PPC64__) && !defined(MUM_TARGET_INDEPENDENT_HASH)
#define _MUM_UNROLL_FACTOR_POWER 3
#elif defined(__aarch64__) && !defined(MUM_TARGET_INDEPENDENT_HASH)
#define _MUM_UNROLL_FACTOR_POWER 4
#else
#define _MUM_UNROLL_FACTOR_POWER 2
#endif
#endif

#if _MUM_UNROLL_FACTOR_POWER < 1
#error "too small unroll factor"
#elif _MUM_UNROLL_FACTOR_POWER > 4
#error "We have not enough primes for such unroll factor"
#endif

#define _MUM_UNROLL_FACTOR (1 << _MUM_UNROLL_FACTOR_POWER)

static inline uint64_t _MUM_OPTIMIZE("unroll-loops")
_mum_hash_aligned (uint64_t start, const void *key, size_t len) {
  uint64_t result = start;
  const unsigned char *str = (const unsigned char *) key;
  uint64_t u64;
  int i;
  size_t n;

  result = _mum (result, _mum_block_start_prime);
  while  (len > _MUM_UNROLL_FACTOR * sizeof (uint64_t)) {
    /* This loop could be vectorized when we have vector insns for
       64x64->128-bit multiplication.  AVX2 currently only have a
       vector insn for 4 32x32->64-bit multiplication.  */
    for (i = 0; i < _MUM_UNROLL_FACTOR; i++)
      result ^= _mum (_mum_le (((uint64_t *) str)[i]), _mum_primes[i]);
    len -= _MUM_UNROLL_FACTOR * sizeof (uint64_t);
    str += _MUM_UNROLL_FACTOR * sizeof (uint64_t);
    /* We will use the same prime numbers on the next iterations --
       randomize the state.  */
    result = _mum (result, _mum_unroll_prime);
  }
  n = len / sizeof (uint64_t);
  for (i = 0; i < (int)n; i++)
    result ^= _mum (_mum_le (((uint64_t *) str)[i]), _mum_primes[i]);
  len -= n * sizeof (uint64_t); str += n * sizeof (uint64_t);
  switch (len) {
  case 7:
    u64 = _mum_le32 (*(uint32_t *) str);
    u64 |= (uint64_t) str[4] << 32;
    u64 |= (uint64_t) str[5] << 40;
    u64 |= (uint64_t) str[6] << 48;
    return result ^ _mum (u64, _mum_tail_prime);
  case 6:
    u64 = _mum_le32 (*(uint32_t *) str);
    u64 |= (uint64_t) str[4] << 32;
    u64 |= (uint64_t) str[5] << 40;
    return result ^ _mum (u64, _mum_tail_prime);
  case 5:
    u64 = _mum_le32 (*(uint32_t *) str);
    u64 |= (uint64_t) str[4] << 32;
    return result ^ _mum (u64, _mum_tail_prime);
  case 4:
    u64 = _mum_le32 (*(uint32_t *) str);
    return result ^ _mum (u64, _mum_tail_prime);
  case 3:
    u64 = str[0];
    u64 |= (uint64_t) str[1] << 8;
    u64 |= (uint64_t) str[2] << 16;
    return result ^ _mum (u64, _mum_tail_prime);
  case 2:
    u64 = str[0];
    u64 |= (uint64_t) str[1] << 8;
    return result ^ _mum (u64, _mum_tail_prime);
  case 1:
    u64 = str[0];
    return result ^ _mum (u64, _mum_tail_prime);
  }
  return result;
}

/* Final randomization of H.  */
static inline uint64_t
_mum_final (uint64_t h) {
  h ^= _mum (h, _mum_finish_prime1);
  h ^= _mum (h, _mum_finish_prime2);
  return h;
}

#if defined(__x86_64__) && defined(_MUM_FRESH_GCC)

/* We want to use AVX2 insn MULX instead of generic x86-64 MULQ where
   it is possible.  Although on modern Intel processors MULQ takes
   3-cycles vs. 4 for MULX, MULX permits more freedom in insn
   scheduling as it uses less fixed registers.  */
static inline uint64_t _MUM_TARGET("arch=haswell")
_mum_hash_avx2 (const void * key, size_t len, uint64_t seed) {
  return _mum_final (_mum_hash_aligned (seed + len, key, len));
}
#endif

#ifndef _MUM_UNALIGNED_ACCESS
#if defined(__x86_64__) || defined(__i386__) || defined(__PPC64__) \
    || defined(__s390__) || defined(__m32c__) || defined(cris)     \
    || defined(__CR16__) || defined(__vax__) || defined(__m68k__) \
    || defined(__aarch64__)
#define _MUM_UNALIGNED_ACCESS 1
#else
#define _MUM_UNALIGNED_ACCESS 0
#endif
#endif

/* When we need an aligned access to data being hashed we move part of
   the unaligned data to an aligned block of given size and then
   process it, repeating processing the data by the block.  */
#ifndef _MUM_BLOCK_LEN
#define _MUM_BLOCK_LEN 1024
#endif

#if _MUM_BLOCK_LEN < 8
#error "too small block length"
#endif

static inline uint64_t
#if defined(__x86_64__)
_MUM_TARGET("inline-all-stringops")
#endif
_mum_hash_default (const void *key, size_t len, uint64_t seed) {
  uint64_t result;
  const unsigned char *str = (const unsigned char *) key;
  size_t block_len;
  uint64_t buf[_MUM_BLOCK_LEN / sizeof (uint64_t)];

  result = seed + len;
  if (_MUM_UNALIGNED_ACCESS || ((size_t) str & 0x7) == 0)
    result = _mum_hash_aligned (result, key, len);
  else {
    while (len != 0) {
      block_len = len < _MUM_BLOCK_LEN ? len : _MUM_BLOCK_LEN;
      memmove (buf, str, block_len);
      result = _mum_hash_aligned (result, buf, block_len);
      len -= block_len;
      str += block_len;
    }
  }
  return _mum_final (result);
}

static inline uint64_t
_mum_next_factor (void) {
  uint64_t start = 0;
  int i;

  for (i = 0; i < 8; i++)
    start = (start << 8) | rand() % 256;
  return start;
}

/* ++++++++++++++++++++++++++ Interface functions: +++++++++++++++++++  */

/* Set random multiplicators depending on SEED.  */
static inline void
mum_hash_randomize (uint64_t seed) {
  int i;

  srand (seed);
  _mum_hash_step_prime = _mum_next_factor ();
  _mum_key_step_prime = _mum_next_factor ();
  _mum_finish_prime1 = _mum_next_factor ();
  _mum_finish_prime2 = _mum_next_factor ();
  _mum_block_start_prime = _mum_next_factor ();
  _mum_unroll_prime = _mum_next_factor ();
  _mum_tail_prime = _mum_next_factor ();
  for (i = 0; i < (int)(sizeof (_mum_primes) / sizeof (uint64_t)); i++)
    _mum_primes[i] = _mum_next_factor ();
}

/* Start hashing data with SEED.  Return the state.  */
static inline uint64_t
mum_hash_init (uint64_t seed) {
  return seed;
}

/* Process data KEY with the state H and return the updated state.  */
static inline uint64_t
mum_hash_step (uint64_t h, uint64_t key)
{
  return _mum (h, _mum_hash_step_prime) ^ _mum (key, _mum_key_step_prime);
}

/* Return the result of hashing using the current state H.  */
static inline uint64_t
mum_hash_finish (uint64_t h) {
  return _mum_final (h);
}

/* Fast hashing of KEY with SEED.  The hash is always the same for the
   same key on any target. */
static inline size_t
mum_hash64 (uint64_t key, uint64_t seed) {
  return mum_hash_finish (mum_hash_step (mum_hash_init (seed), key));
}

/* Hash data KEY of length LEN and SEED.  The hash depends on the
   target endianess and the unroll factor.  */
static inline uint64_t
mum_hash (const void *key, size_t len, uint64_t seed) {
#if defined(__x86_64__) && defined(_MUM_FRESH_GCC)
  static int avx2_support = 0;

  if (avx2_support > 0)
    return _mum_hash_avx2 (key, len, seed);
  else if (! avx2_support) {
    __builtin_cpu_init ();
    avx2_support =  __builtin_cpu_supports ("avx2") ? 1 : -1;
    if (avx2_support > 0)
      return _mum_hash_avx2 (key, len, seed);
  }
#endif
  return _mum_hash_default (key, len, seed);
}

#endif
