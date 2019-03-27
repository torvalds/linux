/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/e_os2.h>
#include <string.h>
#include <assert.h>

size_t SHA3_absorb(uint64_t A[5][5], const unsigned char *inp, size_t len,
                   size_t r);
void SHA3_squeeze(uint64_t A[5][5], unsigned char *out, size_t len, size_t r);

#if !defined(KECCAK1600_ASM) || !defined(SELFTEST)

/*
 * Choose some sensible defaults
 */
#if !defined(KECCAK_REF) && !defined(KECCAK_1X) && !defined(KECCAK_1X_ALT) && \
    !defined(KECCAK_2X) && !defined(KECCAK_INPLACE)
# define KECCAK_2X      /* default to KECCAK_2X variant */
#endif

#if defined(__i386) || defined(__i386__) || defined(_M_IX86)
# define KECCAK_COMPLEMENTING_TRANSFORM
#endif

#if defined(__x86_64__) || defined(__aarch64__) || \
    defined(__mips64) || defined(__ia64) || \
    (defined(__VMS) && !defined(__vax))
/*
 * These are available even in ILP32 flavours, but even then they are
 * capable of performing 64-bit operations as efficiently as in *P64.
 * Since it's not given that we can use sizeof(void *), just shunt it.
 */
# define BIT_INTERLEAVE (0)
#else
# define BIT_INTERLEAVE (sizeof(void *) < 8)
#endif

#define ROL32(a, offset) (((a) << (offset)) | ((a) >> ((32 - (offset)) & 31)))

static uint64_t ROL64(uint64_t val, int offset)
{
    if (offset == 0) {
        return val;
    } else if (!BIT_INTERLEAVE) {
        return (val << offset) | (val >> (64-offset));
    } else {
        uint32_t hi = (uint32_t)(val >> 32), lo = (uint32_t)val;

        if (offset & 1) {
            uint32_t tmp = hi;

            offset >>= 1;
            hi = ROL32(lo, offset);
            lo = ROL32(tmp, offset + 1);
        } else {
            offset >>= 1;
            lo = ROL32(lo, offset);
            hi = ROL32(hi, offset);
        }

        return ((uint64_t)hi << 32) | lo;
    }
}

static const unsigned char rhotates[5][5] = {
    {  0,  1, 62, 28, 27 },
    { 36, 44,  6, 55, 20 },
    {  3, 10, 43, 25, 39 },
    { 41, 45, 15, 21,  8 },
    { 18,  2, 61, 56, 14 }
};

static const uint64_t iotas[] = {
    BIT_INTERLEAVE ? 0x0000000000000001U : 0x0000000000000001U,
    BIT_INTERLEAVE ? 0x0000008900000000U : 0x0000000000008082U,
    BIT_INTERLEAVE ? 0x8000008b00000000U : 0x800000000000808aU,
    BIT_INTERLEAVE ? 0x8000808000000000U : 0x8000000080008000U,
    BIT_INTERLEAVE ? 0x0000008b00000001U : 0x000000000000808bU,
    BIT_INTERLEAVE ? 0x0000800000000001U : 0x0000000080000001U,
    BIT_INTERLEAVE ? 0x8000808800000001U : 0x8000000080008081U,
    BIT_INTERLEAVE ? 0x8000008200000001U : 0x8000000000008009U,
    BIT_INTERLEAVE ? 0x0000000b00000000U : 0x000000000000008aU,
    BIT_INTERLEAVE ? 0x0000000a00000000U : 0x0000000000000088U,
    BIT_INTERLEAVE ? 0x0000808200000001U : 0x0000000080008009U,
    BIT_INTERLEAVE ? 0x0000800300000000U : 0x000000008000000aU,
    BIT_INTERLEAVE ? 0x0000808b00000001U : 0x000000008000808bU,
    BIT_INTERLEAVE ? 0x8000000b00000001U : 0x800000000000008bU,
    BIT_INTERLEAVE ? 0x8000008a00000001U : 0x8000000000008089U,
    BIT_INTERLEAVE ? 0x8000008100000001U : 0x8000000000008003U,
    BIT_INTERLEAVE ? 0x8000008100000000U : 0x8000000000008002U,
    BIT_INTERLEAVE ? 0x8000000800000000U : 0x8000000000000080U,
    BIT_INTERLEAVE ? 0x0000008300000000U : 0x000000000000800aU,
    BIT_INTERLEAVE ? 0x8000800300000000U : 0x800000008000000aU,
    BIT_INTERLEAVE ? 0x8000808800000001U : 0x8000000080008081U,
    BIT_INTERLEAVE ? 0x8000008800000000U : 0x8000000000008080U,
    BIT_INTERLEAVE ? 0x0000800000000001U : 0x0000000080000001U,
    BIT_INTERLEAVE ? 0x8000808200000000U : 0x8000000080008008U
};

#if defined(KECCAK_REF)
/*
 * This is straightforward or "maximum clarity" implementation aiming
 * to resemble section 3.2 of the FIPS PUB 202 "SHA-3 Standard:
 * Permutation-Based Hash and Extendible-Output Functions" as much as
 * possible. With one caveat. Because of the way C stores matrices,
 * references to A[x,y] in the specification are presented as A[y][x].
 * Implementation unrolls inner x-loops so that modulo 5 operations are
 * explicitly pre-computed.
 */
static void Theta(uint64_t A[5][5])
{
    uint64_t C[5], D[5];
    size_t y;

    C[0] = A[0][0];
    C[1] = A[0][1];
    C[2] = A[0][2];
    C[3] = A[0][3];
    C[4] = A[0][4];

    for (y = 1; y < 5; y++) {
        C[0] ^= A[y][0];
        C[1] ^= A[y][1];
        C[2] ^= A[y][2];
        C[3] ^= A[y][3];
        C[4] ^= A[y][4];
    }

    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    for (y = 0; y < 5; y++) {
        A[y][0] ^= D[0];
        A[y][1] ^= D[1];
        A[y][2] ^= D[2];
        A[y][3] ^= D[3];
        A[y][4] ^= D[4];
    }
}

static void Rho(uint64_t A[5][5])
{
    size_t y;

    for (y = 0; y < 5; y++) {
        A[y][0] = ROL64(A[y][0], rhotates[y][0]);
        A[y][1] = ROL64(A[y][1], rhotates[y][1]);
        A[y][2] = ROL64(A[y][2], rhotates[y][2]);
        A[y][3] = ROL64(A[y][3], rhotates[y][3]);
        A[y][4] = ROL64(A[y][4], rhotates[y][4]);
    }
}

static void Pi(uint64_t A[5][5])
{
    uint64_t T[5][5];

    /*
     * T = A
     * A[y][x] = T[x][(3*y+x)%5]
     */
    memcpy(T, A, sizeof(T));

    A[0][0] = T[0][0];
    A[0][1] = T[1][1];
    A[0][2] = T[2][2];
    A[0][3] = T[3][3];
    A[0][4] = T[4][4];

    A[1][0] = T[0][3];
    A[1][1] = T[1][4];
    A[1][2] = T[2][0];
    A[1][3] = T[3][1];
    A[1][4] = T[4][2];

    A[2][0] = T[0][1];
    A[2][1] = T[1][2];
    A[2][2] = T[2][3];
    A[2][3] = T[3][4];
    A[2][4] = T[4][0];

    A[3][0] = T[0][4];
    A[3][1] = T[1][0];
    A[3][2] = T[2][1];
    A[3][3] = T[3][2];
    A[3][4] = T[4][3];

    A[4][0] = T[0][2];
    A[4][1] = T[1][3];
    A[4][2] = T[2][4];
    A[4][3] = T[3][0];
    A[4][4] = T[4][1];
}

static void Chi(uint64_t A[5][5])
{
    uint64_t C[5];
    size_t y;

    for (y = 0; y < 5; y++) {
        C[0] = A[y][0] ^ (~A[y][1] & A[y][2]);
        C[1] = A[y][1] ^ (~A[y][2] & A[y][3]);
        C[2] = A[y][2] ^ (~A[y][3] & A[y][4]);
        C[3] = A[y][3] ^ (~A[y][4] & A[y][0]);
        C[4] = A[y][4] ^ (~A[y][0] & A[y][1]);

        A[y][0] = C[0];
        A[y][1] = C[1];
        A[y][2] = C[2];
        A[y][3] = C[3];
        A[y][4] = C[4];
    }
}

static void Iota(uint64_t A[5][5], size_t i)
{
    assert(i < (sizeof(iotas) / sizeof(iotas[0])));
    A[0][0] ^= iotas[i];
}

static void KeccakF1600(uint64_t A[5][5])
{
    size_t i;

    for (i = 0; i < 24; i++) {
        Theta(A);
        Rho(A);
        Pi(A);
        Chi(A);
        Iota(A, i);
    }
}

#elif defined(KECCAK_1X)
/*
 * This implementation is optimization of above code featuring unroll
 * of even y-loops, their fusion and code motion. It also minimizes
 * temporary storage. Compiler would normally do all these things for
 * you, purpose of manual optimization is to provide "unobscured"
 * reference for assembly implementation [in case this approach is
 * chosen for implementation on some platform]. In the nutshell it's
 * equivalent of "plane-per-plane processing" approach discussed in
 * section 2.4 of "Keccak implementation overview".
 */
static void Round(uint64_t A[5][5], size_t i)
{
    uint64_t C[5], E[2];        /* registers */
    uint64_t D[5], T[2][5];     /* memory    */

    assert(i < (sizeof(iotas) / sizeof(iotas[0])));

    C[0] = A[0][0] ^ A[1][0] ^ A[2][0] ^ A[3][0] ^ A[4][0];
    C[1] = A[0][1] ^ A[1][1] ^ A[2][1] ^ A[3][1] ^ A[4][1];
    C[2] = A[0][2] ^ A[1][2] ^ A[2][2] ^ A[3][2] ^ A[4][2];
    C[3] = A[0][3] ^ A[1][3] ^ A[2][3] ^ A[3][3] ^ A[4][3];
    C[4] = A[0][4] ^ A[1][4] ^ A[2][4] ^ A[3][4] ^ A[4][4];

#if defined(__arm__)
    D[1] = E[0] = ROL64(C[2], 1) ^ C[0];
    D[4] = E[1] = ROL64(C[0], 1) ^ C[3];
    D[0] = C[0] = ROL64(C[1], 1) ^ C[4];
    D[2] = C[1] = ROL64(C[3], 1) ^ C[1];
    D[3] = C[2] = ROL64(C[4], 1) ^ C[2];

    T[0][0] = A[3][0] ^ C[0]; /* borrow T[0][0] */
    T[0][1] = A[0][1] ^ E[0]; /* D[1] */
    T[0][2] = A[0][2] ^ C[1]; /* D[2] */
    T[0][3] = A[0][3] ^ C[2]; /* D[3] */
    T[0][4] = A[0][4] ^ E[1]; /* D[4] */

    C[3] = ROL64(A[3][3] ^ C[2], rhotates[3][3]);   /* D[3] */
    C[4] = ROL64(A[4][4] ^ E[1], rhotates[4][4]);   /* D[4] */
    C[0] =       A[0][0] ^ C[0]; /* rotate by 0 */  /* D[0] */
    C[2] = ROL64(A[2][2] ^ C[1], rhotates[2][2]);   /* D[2] */
    C[1] = ROL64(A[1][1] ^ E[0], rhotates[1][1]);   /* D[1] */
#else
    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    T[0][0] = A[3][0] ^ D[0]; /* borrow T[0][0] */
    T[0][1] = A[0][1] ^ D[1];
    T[0][2] = A[0][2] ^ D[2];
    T[0][3] = A[0][3] ^ D[3];
    T[0][4] = A[0][4] ^ D[4];

    C[0] =       A[0][0] ^ D[0]; /* rotate by 0 */
    C[1] = ROL64(A[1][1] ^ D[1], rhotates[1][1]);
    C[2] = ROL64(A[2][2] ^ D[2], rhotates[2][2]);
    C[3] = ROL64(A[3][3] ^ D[3], rhotates[3][3]);
    C[4] = ROL64(A[4][4] ^ D[4], rhotates[4][4]);
#endif
    A[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
    A[0][1] = C[1] ^ (~C[2] & C[3]);
    A[0][2] = C[2] ^ (~C[3] & C[4]);
    A[0][3] = C[3] ^ (~C[4] & C[0]);
    A[0][4] = C[4] ^ (~C[0] & C[1]);

    T[1][0] = A[1][0] ^ (C[3] = D[0]);
    T[1][1] = A[2][1] ^ (C[4] = D[1]); /* borrow T[1][1] */
    T[1][2] = A[1][2] ^ (E[0] = D[2]);
    T[1][3] = A[1][3] ^ (E[1] = D[3]);
    T[1][4] = A[2][4] ^ (C[2] = D[4]); /* borrow T[1][4] */

    C[0] = ROL64(T[0][3],        rhotates[0][3]);
    C[1] = ROL64(A[1][4] ^ C[2], rhotates[1][4]);   /* D[4] */
    C[2] = ROL64(A[2][0] ^ C[3], rhotates[2][0]);   /* D[0] */
    C[3] = ROL64(A[3][1] ^ C[4], rhotates[3][1]);   /* D[1] */
    C[4] = ROL64(A[4][2] ^ E[0], rhotates[4][2]);   /* D[2] */

    A[1][0] = C[0] ^ (~C[1] & C[2]);
    A[1][1] = C[1] ^ (~C[2] & C[3]);
    A[1][2] = C[2] ^ (~C[3] & C[4]);
    A[1][3] = C[3] ^ (~C[4] & C[0]);
    A[1][4] = C[4] ^ (~C[0] & C[1]);

    C[0] = ROL64(T[0][1],        rhotates[0][1]);
    C[1] = ROL64(T[1][2],        rhotates[1][2]);
    C[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
    C[3] = ROL64(A[3][4] ^ D[4], rhotates[3][4]);
    C[4] = ROL64(A[4][0] ^ D[0], rhotates[4][0]);

    A[2][0] = C[0] ^ (~C[1] & C[2]);
    A[2][1] = C[1] ^ (~C[2] & C[3]);
    A[2][2] = C[2] ^ (~C[3] & C[4]);
    A[2][3] = C[3] ^ (~C[4] & C[0]);
    A[2][4] = C[4] ^ (~C[0] & C[1]);

    C[0] = ROL64(T[0][4],        rhotates[0][4]);
    C[1] = ROL64(T[1][0],        rhotates[1][0]);
    C[2] = ROL64(T[1][1],        rhotates[2][1]); /* originally A[2][1] */
    C[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
    C[4] = ROL64(A[4][3] ^ D[3], rhotates[4][3]);

    A[3][0] = C[0] ^ (~C[1] & C[2]);
    A[3][1] = C[1] ^ (~C[2] & C[3]);
    A[3][2] = C[2] ^ (~C[3] & C[4]);
    A[3][3] = C[3] ^ (~C[4] & C[0]);
    A[3][4] = C[4] ^ (~C[0] & C[1]);

    C[0] = ROL64(T[0][2],        rhotates[0][2]);
    C[1] = ROL64(T[1][3],        rhotates[1][3]);
    C[2] = ROL64(T[1][4],        rhotates[2][4]); /* originally A[2][4] */
    C[3] = ROL64(T[0][0],        rhotates[3][0]); /* originally A[3][0] */
    C[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);

    A[4][0] = C[0] ^ (~C[1] & C[2]);
    A[4][1] = C[1] ^ (~C[2] & C[3]);
    A[4][2] = C[2] ^ (~C[3] & C[4]);
    A[4][3] = C[3] ^ (~C[4] & C[0]);
    A[4][4] = C[4] ^ (~C[0] & C[1]);
}

static void KeccakF1600(uint64_t A[5][5])
{
    size_t i;

    for (i = 0; i < 24; i++) {
        Round(A, i);
    }
}

#elif defined(KECCAK_1X_ALT)
/*
 * This is variant of above KECCAK_1X that reduces requirement for
 * temporary storage even further, but at cost of more updates to A[][].
 * It's less suitable if A[][] is memory bound, but better if it's
 * register bound.
 */

static void Round(uint64_t A[5][5], size_t i)
{
    uint64_t C[5], D[5];

    assert(i < (sizeof(iotas) / sizeof(iotas[0])));

    C[0] = A[0][0] ^ A[1][0] ^ A[2][0] ^ A[3][0] ^ A[4][0];
    C[1] = A[0][1] ^ A[1][1] ^ A[2][1] ^ A[3][1] ^ A[4][1];
    C[2] = A[0][2] ^ A[1][2] ^ A[2][2] ^ A[3][2] ^ A[4][2];
    C[3] = A[0][3] ^ A[1][3] ^ A[2][3] ^ A[3][3] ^ A[4][3];
    C[4] = A[0][4] ^ A[1][4] ^ A[2][4] ^ A[3][4] ^ A[4][4];

    D[1] = C[0] ^  ROL64(C[2], 1);
    D[2] = C[1] ^  ROL64(C[3], 1);
    D[3] = C[2] ^= ROL64(C[4], 1);
    D[4] = C[3] ^= ROL64(C[0], 1);
    D[0] = C[4] ^= ROL64(C[1], 1);

    A[0][1] ^= D[1];
    A[1][1] ^= D[1];
    A[2][1] ^= D[1];
    A[3][1] ^= D[1];
    A[4][1] ^= D[1];

    A[0][2] ^= D[2];
    A[1][2] ^= D[2];
    A[2][2] ^= D[2];
    A[3][2] ^= D[2];
    A[4][2] ^= D[2];

    A[0][3] ^= C[2];
    A[1][3] ^= C[2];
    A[2][3] ^= C[2];
    A[3][3] ^= C[2];
    A[4][3] ^= C[2];

    A[0][4] ^= C[3];
    A[1][4] ^= C[3];
    A[2][4] ^= C[3];
    A[3][4] ^= C[3];
    A[4][4] ^= C[3];

    A[0][0] ^= C[4];
    A[1][0] ^= C[4];
    A[2][0] ^= C[4];
    A[3][0] ^= C[4];
    A[4][0] ^= C[4];

    C[1] = A[0][1];
    C[2] = A[0][2];
    C[3] = A[0][3];
    C[4] = A[0][4];

    A[0][1] = ROL64(A[1][1], rhotates[1][1]);
    A[0][2] = ROL64(A[2][2], rhotates[2][2]);
    A[0][3] = ROL64(A[3][3], rhotates[3][3]);
    A[0][4] = ROL64(A[4][4], rhotates[4][4]);

    A[1][1] = ROL64(A[1][4], rhotates[1][4]);
    A[2][2] = ROL64(A[2][3], rhotates[2][3]);
    A[3][3] = ROL64(A[3][2], rhotates[3][2]);
    A[4][4] = ROL64(A[4][1], rhotates[4][1]);

    A[1][4] = ROL64(A[4][2], rhotates[4][2]);
    A[2][3] = ROL64(A[3][4], rhotates[3][4]);
    A[3][2] = ROL64(A[2][1], rhotates[2][1]);
    A[4][1] = ROL64(A[1][3], rhotates[1][3]);

    A[4][2] = ROL64(A[2][4], rhotates[2][4]);
    A[3][4] = ROL64(A[4][3], rhotates[4][3]);
    A[2][1] = ROL64(A[1][2], rhotates[1][2]);
    A[1][3] = ROL64(A[3][1], rhotates[3][1]);

    A[2][4] = ROL64(A[4][0], rhotates[4][0]);
    A[4][3] = ROL64(A[3][0], rhotates[3][0]);
    A[1][2] = ROL64(A[2][0], rhotates[2][0]);
    A[3][1] = ROL64(A[1][0], rhotates[1][0]);

    A[1][0] = ROL64(C[3],    rhotates[0][3]);
    A[2][0] = ROL64(C[1],    rhotates[0][1]);
    A[3][0] = ROL64(C[4],    rhotates[0][4]);
    A[4][0] = ROL64(C[2],    rhotates[0][2]);

    C[0] = A[0][0];
    C[1] = A[1][0];
    D[0] = A[0][1];
    D[1] = A[1][1];

    A[0][0] ^= (~A[0][1] & A[0][2]);
    A[1][0] ^= (~A[1][1] & A[1][2]);
    A[0][1] ^= (~A[0][2] & A[0][3]);
    A[1][1] ^= (~A[1][2] & A[1][3]);
    A[0][2] ^= (~A[0][3] & A[0][4]);
    A[1][2] ^= (~A[1][3] & A[1][4]);
    A[0][3] ^= (~A[0][4] & C[0]);
    A[1][3] ^= (~A[1][4] & C[1]);
    A[0][4] ^= (~C[0]    & D[0]);
    A[1][4] ^= (~C[1]    & D[1]);

    C[2] = A[2][0];
    C[3] = A[3][0];
    D[2] = A[2][1];
    D[3] = A[3][1];

    A[2][0] ^= (~A[2][1] & A[2][2]);
    A[3][0] ^= (~A[3][1] & A[3][2]);
    A[2][1] ^= (~A[2][2] & A[2][3]);
    A[3][1] ^= (~A[3][2] & A[3][3]);
    A[2][2] ^= (~A[2][3] & A[2][4]);
    A[3][2] ^= (~A[3][3] & A[3][4]);
    A[2][3] ^= (~A[2][4] & C[2]);
    A[3][3] ^= (~A[3][4] & C[3]);
    A[2][4] ^= (~C[2]    & D[2]);
    A[3][4] ^= (~C[3]    & D[3]);

    C[4] = A[4][0];
    D[4] = A[4][1];

    A[4][0] ^= (~A[4][1] & A[4][2]);
    A[4][1] ^= (~A[4][2] & A[4][3]);
    A[4][2] ^= (~A[4][3] & A[4][4]);
    A[4][3] ^= (~A[4][4] & C[4]);
    A[4][4] ^= (~C[4]    & D[4]);
    A[0][0] ^= iotas[i];
}

static void KeccakF1600(uint64_t A[5][5])
{
    size_t i;

    for (i = 0; i < 24; i++) {
        Round(A, i);
    }
}

#elif defined(KECCAK_2X)
/*
 * This implementation is variant of KECCAK_1X above with outer-most
 * round loop unrolled twice. This allows to take temporary storage
 * out of round procedure and simplify references to it by alternating
 * it with actual data (see round loop below). Originally it was meant
 * rather as reference for an assembly implementation, but it seems to
 * play best with compilers [as well as provide best instruction per
 * processed byte ratio at minimal round unroll factor]...
 */
static void Round(uint64_t R[5][5], uint64_t A[5][5], size_t i)
{
    uint64_t C[5], D[5];

    assert(i < (sizeof(iotas) / sizeof(iotas[0])));

    C[0] = A[0][0] ^ A[1][0] ^ A[2][0] ^ A[3][0] ^ A[4][0];
    C[1] = A[0][1] ^ A[1][1] ^ A[2][1] ^ A[3][1] ^ A[4][1];
    C[2] = A[0][2] ^ A[1][2] ^ A[2][2] ^ A[3][2] ^ A[4][2];
    C[3] = A[0][3] ^ A[1][3] ^ A[2][3] ^ A[3][3] ^ A[4][3];
    C[4] = A[0][4] ^ A[1][4] ^ A[2][4] ^ A[3][4] ^ A[4][4];

    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    C[0] =       A[0][0] ^ D[0]; /* rotate by 0 */
    C[1] = ROL64(A[1][1] ^ D[1], rhotates[1][1]);
    C[2] = ROL64(A[2][2] ^ D[2], rhotates[2][2]);
    C[3] = ROL64(A[3][3] ^ D[3], rhotates[3][3]);
    C[4] = ROL64(A[4][4] ^ D[4], rhotates[4][4]);

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    R[0][0] = C[0] ^ ( C[1] | C[2]) ^ iotas[i];
    R[0][1] = C[1] ^ (~C[2] | C[3]);
    R[0][2] = C[2] ^ ( C[3] & C[4]);
    R[0][3] = C[3] ^ ( C[4] | C[0]);
    R[0][4] = C[4] ^ ( C[0] & C[1]);
#else
    R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
    R[0][1] = C[1] ^ (~C[2] & C[3]);
    R[0][2] = C[2] ^ (~C[3] & C[4]);
    R[0][3] = C[3] ^ (~C[4] & C[0]);
    R[0][4] = C[4] ^ (~C[0] & C[1]);
#endif

    C[0] = ROL64(A[0][3] ^ D[3], rhotates[0][3]);
    C[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
    C[2] = ROL64(A[2][0] ^ D[0], rhotates[2][0]);
    C[3] = ROL64(A[3][1] ^ D[1], rhotates[3][1]);
    C[4] = ROL64(A[4][2] ^ D[2], rhotates[4][2]);

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    R[1][0] = C[0] ^ (C[1] |  C[2]);
    R[1][1] = C[1] ^ (C[2] &  C[3]);
    R[1][2] = C[2] ^ (C[3] | ~C[4]);
    R[1][3] = C[3] ^ (C[4] |  C[0]);
    R[1][4] = C[4] ^ (C[0] &  C[1]);
#else
    R[1][0] = C[0] ^ (~C[1] & C[2]);
    R[1][1] = C[1] ^ (~C[2] & C[3]);
    R[1][2] = C[2] ^ (~C[3] & C[4]);
    R[1][3] = C[3] ^ (~C[4] & C[0]);
    R[1][4] = C[4] ^ (~C[0] & C[1]);
#endif

    C[0] = ROL64(A[0][1] ^ D[1], rhotates[0][1]);
    C[1] = ROL64(A[1][2] ^ D[2], rhotates[1][2]);
    C[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
    C[3] = ROL64(A[3][4] ^ D[4], rhotates[3][4]);
    C[4] = ROL64(A[4][0] ^ D[0], rhotates[4][0]);

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    R[2][0] =  C[0] ^ ( C[1] | C[2]);
    R[2][1] =  C[1] ^ ( C[2] & C[3]);
    R[2][2] =  C[2] ^ (~C[3] & C[4]);
    R[2][3] = ~C[3] ^ ( C[4] | C[0]);
    R[2][4] =  C[4] ^ ( C[0] & C[1]);
#else
    R[2][0] = C[0] ^ (~C[1] & C[2]);
    R[2][1] = C[1] ^ (~C[2] & C[3]);
    R[2][2] = C[2] ^ (~C[3] & C[4]);
    R[2][3] = C[3] ^ (~C[4] & C[0]);
    R[2][4] = C[4] ^ (~C[0] & C[1]);
#endif

    C[0] = ROL64(A[0][4] ^ D[4], rhotates[0][4]);
    C[1] = ROL64(A[1][0] ^ D[0], rhotates[1][0]);
    C[2] = ROL64(A[2][1] ^ D[1], rhotates[2][1]);
    C[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
    C[4] = ROL64(A[4][3] ^ D[3], rhotates[4][3]);

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    R[3][0] =  C[0] ^ ( C[1] & C[2]);
    R[3][1] =  C[1] ^ ( C[2] | C[3]);
    R[3][2] =  C[2] ^ (~C[3] | C[4]);
    R[3][3] = ~C[3] ^ ( C[4] & C[0]);
    R[3][4] =  C[4] ^ ( C[0] | C[1]);
#else
    R[3][0] = C[0] ^ (~C[1] & C[2]);
    R[3][1] = C[1] ^ (~C[2] & C[3]);
    R[3][2] = C[2] ^ (~C[3] & C[4]);
    R[3][3] = C[3] ^ (~C[4] & C[0]);
    R[3][4] = C[4] ^ (~C[0] & C[1]);
#endif

    C[0] = ROL64(A[0][2] ^ D[2], rhotates[0][2]);
    C[1] = ROL64(A[1][3] ^ D[3], rhotates[1][3]);
    C[2] = ROL64(A[2][4] ^ D[4], rhotates[2][4]);
    C[3] = ROL64(A[3][0] ^ D[0], rhotates[3][0]);
    C[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    R[4][0] =  C[0] ^ (~C[1] & C[2]);
    R[4][1] = ~C[1] ^ ( C[2] | C[3]);
    R[4][2] =  C[2] ^ ( C[3] & C[4]);
    R[4][3] =  C[3] ^ ( C[4] | C[0]);
    R[4][4] =  C[4] ^ ( C[0] & C[1]);
#else
    R[4][0] = C[0] ^ (~C[1] & C[2]);
    R[4][1] = C[1] ^ (~C[2] & C[3]);
    R[4][2] = C[2] ^ (~C[3] & C[4]);
    R[4][3] = C[3] ^ (~C[4] & C[0]);
    R[4][4] = C[4] ^ (~C[0] & C[1]);
#endif
}

static void KeccakF1600(uint64_t A[5][5])
{
    uint64_t T[5][5];
    size_t i;

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    A[0][1] = ~A[0][1];
    A[0][2] = ~A[0][2];
    A[1][3] = ~A[1][3];
    A[2][2] = ~A[2][2];
    A[3][2] = ~A[3][2];
    A[4][0] = ~A[4][0];
#endif

    for (i = 0; i < 24; i += 2) {
        Round(T, A, i);
        Round(A, T, i + 1);
    }

#ifdef KECCAK_COMPLEMENTING_TRANSFORM
    A[0][1] = ~A[0][1];
    A[0][2] = ~A[0][2];
    A[1][3] = ~A[1][3];
    A[2][2] = ~A[2][2];
    A[3][2] = ~A[3][2];
    A[4][0] = ~A[4][0];
#endif
}

#else   /* define KECCAK_INPLACE to compile this code path */
/*
 * This implementation is KECCAK_1X from above combined 4 times with
 * a twist that allows to omit temporary storage and perform in-place
 * processing. It's discussed in section 2.5 of "Keccak implementation
 * overview". It's likely to be best suited for processors with large
 * register bank... On the other hand processor with large register
 * bank can as well use KECCAK_1X_ALT, it would be as fast but much
 * more compact...
 */
static void FourRounds(uint64_t A[5][5], size_t i)
{
    uint64_t B[5], C[5], D[5];

    assert(i <= (sizeof(iotas) / sizeof(iotas[0]) - 4));

    /* Round 4*n */
    C[0] = A[0][0] ^ A[1][0] ^ A[2][0] ^ A[3][0] ^ A[4][0];
    C[1] = A[0][1] ^ A[1][1] ^ A[2][1] ^ A[3][1] ^ A[4][1];
    C[2] = A[0][2] ^ A[1][2] ^ A[2][2] ^ A[3][2] ^ A[4][2];
    C[3] = A[0][3] ^ A[1][3] ^ A[2][3] ^ A[3][3] ^ A[4][3];
    C[4] = A[0][4] ^ A[1][4] ^ A[2][4] ^ A[3][4] ^ A[4][4];

    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    B[0] =       A[0][0] ^ D[0]; /* rotate by 0 */
    B[1] = ROL64(A[1][1] ^ D[1], rhotates[1][1]);
    B[2] = ROL64(A[2][2] ^ D[2], rhotates[2][2]);
    B[3] = ROL64(A[3][3] ^ D[3], rhotates[3][3]);
    B[4] = ROL64(A[4][4] ^ D[4], rhotates[4][4]);

    C[0] = A[0][0] = B[0] ^ (~B[1] & B[2]) ^ iotas[i];
    C[1] = A[1][1] = B[1] ^ (~B[2] & B[3]);
    C[2] = A[2][2] = B[2] ^ (~B[3] & B[4]);
    C[3] = A[3][3] = B[3] ^ (~B[4] & B[0]);
    C[4] = A[4][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[0][3] ^ D[3], rhotates[0][3]);
    B[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
    B[2] = ROL64(A[2][0] ^ D[0], rhotates[2][0]);
    B[3] = ROL64(A[3][1] ^ D[1], rhotates[3][1]);
    B[4] = ROL64(A[4][2] ^ D[2], rhotates[4][2]);

    C[0] ^= A[2][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[3][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[4][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[0][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[1][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[0][1] ^ D[1], rhotates[0][1]);
    B[1] = ROL64(A[1][2] ^ D[2], rhotates[1][2]);
    B[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
    B[3] = ROL64(A[3][4] ^ D[4], rhotates[3][4]);
    B[4] = ROL64(A[4][0] ^ D[0], rhotates[4][0]);

    C[0] ^= A[4][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[0][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[1][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[2][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[3][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[0][4] ^ D[4], rhotates[0][4]);
    B[1] = ROL64(A[1][0] ^ D[0], rhotates[1][0]);
    B[2] = ROL64(A[2][1] ^ D[1], rhotates[2][1]);
    B[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
    B[4] = ROL64(A[4][3] ^ D[3], rhotates[4][3]);

    C[0] ^= A[1][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[2][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[3][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[4][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[0][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[0][2] ^ D[2], rhotates[0][2]);
    B[1] = ROL64(A[1][3] ^ D[3], rhotates[1][3]);
    B[2] = ROL64(A[2][4] ^ D[4], rhotates[2][4]);
    B[3] = ROL64(A[3][0] ^ D[0], rhotates[3][0]);
    B[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);

    C[0] ^= A[3][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[4][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[0][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[1][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[2][4] = B[4] ^ (~B[0] & B[1]);

    /* Round 4*n+1 */
    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    B[0] =       A[0][0] ^ D[0]; /* rotate by 0 */
    B[1] = ROL64(A[3][1] ^ D[1], rhotates[1][1]);
    B[2] = ROL64(A[1][2] ^ D[2], rhotates[2][2]);
    B[3] = ROL64(A[4][3] ^ D[3], rhotates[3][3]);
    B[4] = ROL64(A[2][4] ^ D[4], rhotates[4][4]);

    C[0] = A[0][0] = B[0] ^ (~B[1] & B[2]) ^ iotas[i + 1];
    C[1] = A[3][1] = B[1] ^ (~B[2] & B[3]);
    C[2] = A[1][2] = B[2] ^ (~B[3] & B[4]);
    C[3] = A[4][3] = B[3] ^ (~B[4] & B[0]);
    C[4] = A[2][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[3][3] ^ D[3], rhotates[0][3]);
    B[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
    B[2] = ROL64(A[4][0] ^ D[0], rhotates[2][0]);
    B[3] = ROL64(A[2][1] ^ D[1], rhotates[3][1]);
    B[4] = ROL64(A[0][2] ^ D[2], rhotates[4][2]);

    C[0] ^= A[4][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[2][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[0][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[3][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[1][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[1][1] ^ D[1], rhotates[0][1]);
    B[1] = ROL64(A[4][2] ^ D[2], rhotates[1][2]);
    B[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
    B[3] = ROL64(A[0][4] ^ D[4], rhotates[3][4]);
    B[4] = ROL64(A[3][0] ^ D[0], rhotates[4][0]);

    C[0] ^= A[3][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[1][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[4][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[2][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[0][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[4][4] ^ D[4], rhotates[0][4]);
    B[1] = ROL64(A[2][0] ^ D[0], rhotates[1][0]);
    B[2] = ROL64(A[0][1] ^ D[1], rhotates[2][1]);
    B[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
    B[4] = ROL64(A[1][3] ^ D[3], rhotates[4][3]);

    C[0] ^= A[2][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[0][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[3][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[1][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[4][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[2][2] ^ D[2], rhotates[0][2]);
    B[1] = ROL64(A[0][3] ^ D[3], rhotates[1][3]);
    B[2] = ROL64(A[3][4] ^ D[4], rhotates[2][4]);
    B[3] = ROL64(A[1][0] ^ D[0], rhotates[3][0]);
    B[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);

    C[0] ^= A[1][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[4][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[2][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[0][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[3][4] = B[4] ^ (~B[0] & B[1]);

    /* Round 4*n+2 */
    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    B[0] =       A[0][0] ^ D[0]; /* rotate by 0 */
    B[1] = ROL64(A[2][1] ^ D[1], rhotates[1][1]);
    B[2] = ROL64(A[4][2] ^ D[2], rhotates[2][2]);
    B[3] = ROL64(A[1][3] ^ D[3], rhotates[3][3]);
    B[4] = ROL64(A[3][4] ^ D[4], rhotates[4][4]);

    C[0] = A[0][0] = B[0] ^ (~B[1] & B[2]) ^ iotas[i + 2];
    C[1] = A[2][1] = B[1] ^ (~B[2] & B[3]);
    C[2] = A[4][2] = B[2] ^ (~B[3] & B[4]);
    C[3] = A[1][3] = B[3] ^ (~B[4] & B[0]);
    C[4] = A[3][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[4][3] ^ D[3], rhotates[0][3]);
    B[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
    B[2] = ROL64(A[3][0] ^ D[0], rhotates[2][0]);
    B[3] = ROL64(A[0][1] ^ D[1], rhotates[3][1]);
    B[4] = ROL64(A[2][2] ^ D[2], rhotates[4][2]);

    C[0] ^= A[3][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[0][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[2][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[4][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[1][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[3][1] ^ D[1], rhotates[0][1]);
    B[1] = ROL64(A[0][2] ^ D[2], rhotates[1][2]);
    B[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
    B[3] = ROL64(A[4][4] ^ D[4], rhotates[3][4]);
    B[4] = ROL64(A[1][0] ^ D[0], rhotates[4][0]);

    C[0] ^= A[1][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[3][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[0][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[2][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[4][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[2][4] ^ D[4], rhotates[0][4]);
    B[1] = ROL64(A[4][0] ^ D[0], rhotates[1][0]);
    B[2] = ROL64(A[1][1] ^ D[1], rhotates[2][1]);
    B[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
    B[4] = ROL64(A[0][3] ^ D[3], rhotates[4][3]);

    C[0] ^= A[4][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[1][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[3][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[0][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[2][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[1][2] ^ D[2], rhotates[0][2]);
    B[1] = ROL64(A[3][3] ^ D[3], rhotates[1][3]);
    B[2] = ROL64(A[0][4] ^ D[4], rhotates[2][4]);
    B[3] = ROL64(A[2][0] ^ D[0], rhotates[3][0]);
    B[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);

    C[0] ^= A[2][0] = B[0] ^ (~B[1] & B[2]);
    C[1] ^= A[4][1] = B[1] ^ (~B[2] & B[3]);
    C[2] ^= A[1][2] = B[2] ^ (~B[3] & B[4]);
    C[3] ^= A[3][3] = B[3] ^ (~B[4] & B[0]);
    C[4] ^= A[0][4] = B[4] ^ (~B[0] & B[1]);

    /* Round 4*n+3 */
    D[0] = ROL64(C[1], 1) ^ C[4];
    D[1] = ROL64(C[2], 1) ^ C[0];
    D[2] = ROL64(C[3], 1) ^ C[1];
    D[3] = ROL64(C[4], 1) ^ C[2];
    D[4] = ROL64(C[0], 1) ^ C[3];

    B[0] =       A[0][0] ^ D[0]; /* rotate by 0 */
    B[1] = ROL64(A[0][1] ^ D[1], rhotates[1][1]);
    B[2] = ROL64(A[0][2] ^ D[2], rhotates[2][2]);
    B[3] = ROL64(A[0][3] ^ D[3], rhotates[3][3]);
    B[4] = ROL64(A[0][4] ^ D[4], rhotates[4][4]);

    /* C[0] = */ A[0][0] = B[0] ^ (~B[1] & B[2]) ^ iotas[i + 3];
    /* C[1] = */ A[0][1] = B[1] ^ (~B[2] & B[3]);
    /* C[2] = */ A[0][2] = B[2] ^ (~B[3] & B[4]);
    /* C[3] = */ A[0][3] = B[3] ^ (~B[4] & B[0]);
    /* C[4] = */ A[0][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[1][3] ^ D[3], rhotates[0][3]);
    B[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);
    B[2] = ROL64(A[1][0] ^ D[0], rhotates[2][0]);
    B[3] = ROL64(A[1][1] ^ D[1], rhotates[3][1]);
    B[4] = ROL64(A[1][2] ^ D[2], rhotates[4][2]);

    /* C[0] ^= */ A[1][0] = B[0] ^ (~B[1] & B[2]);
    /* C[1] ^= */ A[1][1] = B[1] ^ (~B[2] & B[3]);
    /* C[2] ^= */ A[1][2] = B[2] ^ (~B[3] & B[4]);
    /* C[3] ^= */ A[1][3] = B[3] ^ (~B[4] & B[0]);
    /* C[4] ^= */ A[1][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[2][1] ^ D[1], rhotates[0][1]);
    B[1] = ROL64(A[2][2] ^ D[2], rhotates[1][2]);
    B[2] = ROL64(A[2][3] ^ D[3], rhotates[2][3]);
    B[3] = ROL64(A[2][4] ^ D[4], rhotates[3][4]);
    B[4] = ROL64(A[2][0] ^ D[0], rhotates[4][0]);

    /* C[0] ^= */ A[2][0] = B[0] ^ (~B[1] & B[2]);
    /* C[1] ^= */ A[2][1] = B[1] ^ (~B[2] & B[3]);
    /* C[2] ^= */ A[2][2] = B[2] ^ (~B[3] & B[4]);
    /* C[3] ^= */ A[2][3] = B[3] ^ (~B[4] & B[0]);
    /* C[4] ^= */ A[2][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[3][4] ^ D[4], rhotates[0][4]);
    B[1] = ROL64(A[3][0] ^ D[0], rhotates[1][0]);
    B[2] = ROL64(A[3][1] ^ D[1], rhotates[2][1]);
    B[3] = ROL64(A[3][2] ^ D[2], rhotates[3][2]);
    B[4] = ROL64(A[3][3] ^ D[3], rhotates[4][3]);

    /* C[0] ^= */ A[3][0] = B[0] ^ (~B[1] & B[2]);
    /* C[1] ^= */ A[3][1] = B[1] ^ (~B[2] & B[3]);
    /* C[2] ^= */ A[3][2] = B[2] ^ (~B[3] & B[4]);
    /* C[3] ^= */ A[3][3] = B[3] ^ (~B[4] & B[0]);
    /* C[4] ^= */ A[3][4] = B[4] ^ (~B[0] & B[1]);

    B[0] = ROL64(A[4][2] ^ D[2], rhotates[0][2]);
    B[1] = ROL64(A[4][3] ^ D[3], rhotates[1][3]);
    B[2] = ROL64(A[4][4] ^ D[4], rhotates[2][4]);
    B[3] = ROL64(A[4][0] ^ D[0], rhotates[3][0]);
    B[4] = ROL64(A[4][1] ^ D[1], rhotates[4][1]);

    /* C[0] ^= */ A[4][0] = B[0] ^ (~B[1] & B[2]);
    /* C[1] ^= */ A[4][1] = B[1] ^ (~B[2] & B[3]);
    /* C[2] ^= */ A[4][2] = B[2] ^ (~B[3] & B[4]);
    /* C[3] ^= */ A[4][3] = B[3] ^ (~B[4] & B[0]);
    /* C[4] ^= */ A[4][4] = B[4] ^ (~B[0] & B[1]);
}

static void KeccakF1600(uint64_t A[5][5])
{
    size_t i;

    for (i = 0; i < 24; i += 4) {
        FourRounds(A, i);
    }
}

#endif

static uint64_t BitInterleave(uint64_t Ai)
{
    if (BIT_INTERLEAVE) {
        uint32_t hi = (uint32_t)(Ai >> 32), lo = (uint32_t)Ai;
        uint32_t t0, t1;

        t0 = lo & 0x55555555;
        t0 |= t0 >> 1;  t0 &= 0x33333333;
        t0 |= t0 >> 2;  t0 &= 0x0f0f0f0f;
        t0 |= t0 >> 4;  t0 &= 0x00ff00ff;
        t0 |= t0 >> 8;  t0 &= 0x0000ffff;

        t1 = hi & 0x55555555;
        t1 |= t1 >> 1;  t1 &= 0x33333333;
        t1 |= t1 >> 2;  t1 &= 0x0f0f0f0f;
        t1 |= t1 >> 4;  t1 &= 0x00ff00ff;
        t1 |= t1 >> 8;  t1 <<= 16;

        lo &= 0xaaaaaaaa;
        lo |= lo << 1;  lo &= 0xcccccccc;
        lo |= lo << 2;  lo &= 0xf0f0f0f0;
        lo |= lo << 4;  lo &= 0xff00ff00;
        lo |= lo << 8;  lo >>= 16;

        hi &= 0xaaaaaaaa;
        hi |= hi << 1;  hi &= 0xcccccccc;
        hi |= hi << 2;  hi &= 0xf0f0f0f0;
        hi |= hi << 4;  hi &= 0xff00ff00;
        hi |= hi << 8;  hi &= 0xffff0000;

        Ai = ((uint64_t)(hi | lo) << 32) | (t1 | t0);
    }

    return Ai;
}

static uint64_t BitDeinterleave(uint64_t Ai)
{
    if (BIT_INTERLEAVE) {
        uint32_t hi = (uint32_t)(Ai >> 32), lo = (uint32_t)Ai;
        uint32_t t0, t1;

        t0 = lo & 0x0000ffff;
        t0 |= t0 << 8;  t0 &= 0x00ff00ff;
        t0 |= t0 << 4;  t0 &= 0x0f0f0f0f;
        t0 |= t0 << 2;  t0 &= 0x33333333;
        t0 |= t0 << 1;  t0 &= 0x55555555;

        t1 = hi << 16;
        t1 |= t1 >> 8;  t1 &= 0xff00ff00;
        t1 |= t1 >> 4;  t1 &= 0xf0f0f0f0;
        t1 |= t1 >> 2;  t1 &= 0xcccccccc;
        t1 |= t1 >> 1;  t1 &= 0xaaaaaaaa;

        lo >>= 16;
        lo |= lo << 8;  lo &= 0x00ff00ff;
        lo |= lo << 4;  lo &= 0x0f0f0f0f;
        lo |= lo << 2;  lo &= 0x33333333;
        lo |= lo << 1;  lo &= 0x55555555;

        hi &= 0xffff0000;
        hi |= hi >> 8;  hi &= 0xff00ff00;
        hi |= hi >> 4;  hi &= 0xf0f0f0f0;
        hi |= hi >> 2;  hi &= 0xcccccccc;
        hi |= hi >> 1;  hi &= 0xaaaaaaaa;

        Ai = ((uint64_t)(hi | lo) << 32) | (t1 | t0);
    }

    return Ai;
}

/*
 * SHA3_absorb can be called multiple times, but at each invocation
 * largest multiple of |r| out of |len| bytes are processed. Then
 * remaining amount of bytes is returned. This is done to spare caller
 * trouble of calculating the largest multiple of |r|. |r| can be viewed
 * as blocksize. It is commonly (1600 - 256*n)/8, e.g. 168, 136, 104,
 * 72, but can also be (1600 - 448)/8 = 144. All this means that message
 * padding and intermediate sub-block buffering, byte- or bitwise, is
 * caller's responsibility.
 */
size_t SHA3_absorb(uint64_t A[5][5], const unsigned char *inp, size_t len,
                   size_t r)
{
    uint64_t *A_flat = (uint64_t *)A;
    size_t i, w = r / 8;

    assert(r < (25 * sizeof(A[0][0])) && (r % 8) == 0);

    while (len >= r) {
        for (i = 0; i < w; i++) {
            uint64_t Ai = (uint64_t)inp[0]       | (uint64_t)inp[1] << 8  |
                          (uint64_t)inp[2] << 16 | (uint64_t)inp[3] << 24 |
                          (uint64_t)inp[4] << 32 | (uint64_t)inp[5] << 40 |
                          (uint64_t)inp[6] << 48 | (uint64_t)inp[7] << 56;
            inp += 8;

            A_flat[i] ^= BitInterleave(Ai);
        }
        KeccakF1600(A);
        len -= r;
    }

    return len;
}

/*
 * SHA3_squeeze is called once at the end to generate |out| hash value
 * of |len| bytes.
 */
void SHA3_squeeze(uint64_t A[5][5], unsigned char *out, size_t len, size_t r)
{
    uint64_t *A_flat = (uint64_t *)A;
    size_t i, w = r / 8;

    assert(r < (25 * sizeof(A[0][0])) && (r % 8) == 0);

    while (len != 0) {
        for (i = 0; i < w && len != 0; i++) {
            uint64_t Ai = BitDeinterleave(A_flat[i]);

            if (len < 8) {
                for (i = 0; i < len; i++) {
                    *out++ = (unsigned char)Ai;
                    Ai >>= 8;
                }
                return;
            }

            out[0] = (unsigned char)(Ai);
            out[1] = (unsigned char)(Ai >> 8);
            out[2] = (unsigned char)(Ai >> 16);
            out[3] = (unsigned char)(Ai >> 24);
            out[4] = (unsigned char)(Ai >> 32);
            out[5] = (unsigned char)(Ai >> 40);
            out[6] = (unsigned char)(Ai >> 48);
            out[7] = (unsigned char)(Ai >> 56);
            out += 8;
            len -= 8;
        }
        if (len)
            KeccakF1600(A);
    }
}
#endif

#ifdef SELFTEST
/*
 * Post-padding one-shot implementations would look as following:
 *
 * SHA3_224     SHA3_sponge(inp, len, out, 224/8, (1600-448)/8);
 * SHA3_256     SHA3_sponge(inp, len, out, 256/8, (1600-512)/8);
 * SHA3_384     SHA3_sponge(inp, len, out, 384/8, (1600-768)/8);
 * SHA3_512     SHA3_sponge(inp, len, out, 512/8, (1600-1024)/8);
 * SHAKE_128    SHA3_sponge(inp, len, out, d, (1600-256)/8);
 * SHAKE_256    SHA3_sponge(inp, len, out, d, (1600-512)/8);
 */

void SHA3_sponge(const unsigned char *inp, size_t len,
                 unsigned char *out, size_t d, size_t r)
{
    uint64_t A[5][5];

    memset(A, 0, sizeof(A));
    SHA3_absorb(A, inp, len, r);
    SHA3_squeeze(A, out, d, r);
}

# include <stdio.h>

int main()
{
    /*
     * This is 5-bit SHAKE128 test from http://csrc.nist.gov/groups/ST/toolkit/examples.html#aHashing
     */
    unsigned char test[168] = { '\xf3', '\x3' };
    unsigned char out[512];
    size_t i;
    static const unsigned char result[512] = {
        0x2E, 0x0A, 0xBF, 0xBA, 0x83, 0xE6, 0x72, 0x0B,
        0xFB, 0xC2, 0x25, 0xFF, 0x6B, 0x7A, 0xB9, 0xFF,
        0xCE, 0x58, 0xBA, 0x02, 0x7E, 0xE3, 0xD8, 0x98,
        0x76, 0x4F, 0xEF, 0x28, 0x7D, 0xDE, 0xCC, 0xCA,
        0x3E, 0x6E, 0x59, 0x98, 0x41, 0x1E, 0x7D, 0xDB,
        0x32, 0xF6, 0x75, 0x38, 0xF5, 0x00, 0xB1, 0x8C,
        0x8C, 0x97, 0xC4, 0x52, 0xC3, 0x70, 0xEA, 0x2C,
        0xF0, 0xAF, 0xCA, 0x3E, 0x05, 0xDE, 0x7E, 0x4D,
        0xE2, 0x7F, 0xA4, 0x41, 0xA9, 0xCB, 0x34, 0xFD,
        0x17, 0xC9, 0x78, 0xB4, 0x2D, 0x5B, 0x7E, 0x7F,
        0x9A, 0xB1, 0x8F, 0xFE, 0xFF, 0xC3, 0xC5, 0xAC,
        0x2F, 0x3A, 0x45, 0x5E, 0xEB, 0xFD, 0xC7, 0x6C,
        0xEA, 0xEB, 0x0A, 0x2C, 0xCA, 0x22, 0xEE, 0xF6,
        0xE6, 0x37, 0xF4, 0xCA, 0xBE, 0x5C, 0x51, 0xDE,
        0xD2, 0xE3, 0xFA, 0xD8, 0xB9, 0x52, 0x70, 0xA3,
        0x21, 0x84, 0x56, 0x64, 0xF1, 0x07, 0xD1, 0x64,
        0x96, 0xBB, 0x7A, 0xBF, 0xBE, 0x75, 0x04, 0xB6,
        0xED, 0xE2, 0xE8, 0x9E, 0x4B, 0x99, 0x6F, 0xB5,
        0x8E, 0xFD, 0xC4, 0x18, 0x1F, 0x91, 0x63, 0x38,
        0x1C, 0xBE, 0x7B, 0xC0, 0x06, 0xA7, 0xA2, 0x05,
        0x98, 0x9C, 0x52, 0x6C, 0xD1, 0xBD, 0x68, 0x98,
        0x36, 0x93, 0xB4, 0xBD, 0xC5, 0x37, 0x28, 0xB2,
        0x41, 0xC1, 0xCF, 0xF4, 0x2B, 0xB6, 0x11, 0x50,
        0x2C, 0x35, 0x20, 0x5C, 0xAB, 0xB2, 0x88, 0x75,
        0x56, 0x55, 0xD6, 0x20, 0xC6, 0x79, 0x94, 0xF0,
        0x64, 0x51, 0x18, 0x7F, 0x6F, 0xD1, 0x7E, 0x04,
        0x66, 0x82, 0xBA, 0x12, 0x86, 0x06, 0x3F, 0xF8,
        0x8F, 0xE2, 0x50, 0x8D, 0x1F, 0xCA, 0xF9, 0x03,
        0x5A, 0x12, 0x31, 0xAD, 0x41, 0x50, 0xA9, 0xC9,
        0xB2, 0x4C, 0x9B, 0x2D, 0x66, 0xB2, 0xAD, 0x1B,
        0xDE, 0x0B, 0xD0, 0xBB, 0xCB, 0x8B, 0xE0, 0x5B,
        0x83, 0x52, 0x29, 0xEF, 0x79, 0x19, 0x73, 0x73,
        0x23, 0x42, 0x44, 0x01, 0xE1, 0xD8, 0x37, 0xB6,
        0x6E, 0xB4, 0xE6, 0x30, 0xFF, 0x1D, 0xE7, 0x0C,
        0xB3, 0x17, 0xC2, 0xBA, 0xCB, 0x08, 0x00, 0x1D,
        0x34, 0x77, 0xB7, 0xA7, 0x0A, 0x57, 0x6D, 0x20,
        0x86, 0x90, 0x33, 0x58, 0x9D, 0x85, 0xA0, 0x1D,
        0xDB, 0x2B, 0x66, 0x46, 0xC0, 0x43, 0xB5, 0x9F,
        0xC0, 0x11, 0x31, 0x1D, 0xA6, 0x66, 0xFA, 0x5A,
        0xD1, 0xD6, 0x38, 0x7F, 0xA9, 0xBC, 0x40, 0x15,
        0xA3, 0x8A, 0x51, 0xD1, 0xDA, 0x1E, 0xA6, 0x1D,
        0x64, 0x8D, 0xC8, 0xE3, 0x9A, 0x88, 0xB9, 0xD6,
        0x22, 0xBD, 0xE2, 0x07, 0xFD, 0xAB, 0xC6, 0xF2,
        0x82, 0x7A, 0x88, 0x0C, 0x33, 0x0B, 0xBF, 0x6D,
        0xF7, 0x33, 0x77, 0x4B, 0x65, 0x3E, 0x57, 0x30,
        0x5D, 0x78, 0xDC, 0xE1, 0x12, 0xF1, 0x0A, 0x2C,
        0x71, 0xF4, 0xCD, 0xAD, 0x92, 0xED, 0x11, 0x3E,
        0x1C, 0xEA, 0x63, 0xB9, 0x19, 0x25, 0xED, 0x28,
        0x19, 0x1E, 0x6D, 0xBB, 0xB5, 0xAA, 0x5A, 0x2A,
        0xFD, 0xA5, 0x1F, 0xC0, 0x5A, 0x3A, 0xF5, 0x25,
        0x8B, 0x87, 0x66, 0x52, 0x43, 0x55, 0x0F, 0x28,
        0x94, 0x8A, 0xE2, 0xB8, 0xBE, 0xB6, 0xBC, 0x9C,
        0x77, 0x0B, 0x35, 0xF0, 0x67, 0xEA, 0xA6, 0x41,
        0xEF, 0xE6, 0x5B, 0x1A, 0x44, 0x90, 0x9D, 0x1B,
        0x14, 0x9F, 0x97, 0xEE, 0xA6, 0x01, 0x39, 0x1C,
        0x60, 0x9E, 0xC8, 0x1D, 0x19, 0x30, 0xF5, 0x7C,
        0x18, 0xA4, 0xE0, 0xFA, 0xB4, 0x91, 0xD1, 0xCA,
        0xDF, 0xD5, 0x04, 0x83, 0x44, 0x9E, 0xDC, 0x0F,
        0x07, 0xFF, 0xB2, 0x4D, 0x2C, 0x6F, 0x9A, 0x9A,
        0x3B, 0xFF, 0x39, 0xAE, 0x3D, 0x57, 0xF5, 0x60,
        0x65, 0x4D, 0x7D, 0x75, 0xC9, 0x08, 0xAB, 0xE6,
        0x25, 0x64, 0x75, 0x3E, 0xAC, 0x39, 0xD7, 0x50,
        0x3D, 0xA6, 0xD3, 0x7C, 0x2E, 0x32, 0xE1, 0xAF,
        0x3B, 0x8A, 0xEC, 0x8A, 0xE3, 0x06, 0x9C, 0xD9
    };

    test[167] = '\x80';
    SHA3_sponge(test, sizeof(test), out, sizeof(out), sizeof(test));

    /*
     * Rationale behind keeping output [formatted as below] is that
     * one should be able to redirect it to a file, then copy-n-paste
     * final "output val" from official example to another file, and
     * compare the two with diff(1).
     */
    for (i = 0; i < sizeof(out);) {
        printf("%02X", out[i]);
        printf(++i % 16 && i != sizeof(out) ? " " : "\n");
    }

    if (memcmp(out,result,sizeof(out))) {
        fprintf(stderr,"failure\n");
        return 1;
    } else {
        fprintf(stderr,"success\n");
        return 0;
    }
}
#endif
