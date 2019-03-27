/*
 * Copyright 2010-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>
#include "modes_lcl.h"
#include <string.h>

#if defined(BSWAP4) && defined(STRICT_ALIGNMENT)
/* redefine, because alignment is ensured */
# undef  GETU32
# define GETU32(p)       BSWAP4(*(const u32 *)(p))
# undef  PUTU32
# define PUTU32(p,v)     *(u32 *)(p) = BSWAP4(v)
#endif

#define PACK(s)         ((size_t)(s)<<(sizeof(size_t)*8-16))
#define REDUCE1BIT(V)   do { \
        if (sizeof(size_t)==8) { \
                u64 T = U64(0xe100000000000000) & (0-(V.lo&1)); \
                V.lo  = (V.hi<<63)|(V.lo>>1); \
                V.hi  = (V.hi>>1 )^T; \
        } \
        else { \
                u32 T = 0xe1000000U & (0-(u32)(V.lo&1)); \
                V.lo  = (V.hi<<63)|(V.lo>>1); \
                V.hi  = (V.hi>>1 )^((u64)T<<32); \
        } \
} while(0)

/*-
 * Even though permitted values for TABLE_BITS are 8, 4 and 1, it should
 * never be set to 8. 8 is effectively reserved for testing purposes.
 * TABLE_BITS>1 are lookup-table-driven implementations referred to as
 * "Shoup's" in GCM specification. In other words OpenSSL does not cover
 * whole spectrum of possible table driven implementations. Why? In
 * non-"Shoup's" case memory access pattern is segmented in such manner,
 * that it's trivial to see that cache timing information can reveal
 * fair portion of intermediate hash value. Given that ciphertext is
 * always available to attacker, it's possible for him to attempt to
 * deduce secret parameter H and if successful, tamper with messages
 * [which is nothing but trivial in CTR mode]. In "Shoup's" case it's
 * not as trivial, but there is no reason to believe that it's resistant
 * to cache-timing attack. And the thing about "8-bit" implementation is
 * that it consumes 16 (sixteen) times more memory, 4KB per individual
 * key + 1KB shared. Well, on pros side it should be twice as fast as
 * "4-bit" version. And for gcc-generated x86[_64] code, "8-bit" version
 * was observed to run ~75% faster, closer to 100% for commercial
 * compilers... Yet "4-bit" procedure is preferred, because it's
 * believed to provide better security-performance balance and adequate
 * all-round performance. "All-round" refers to things like:
 *
 * - shorter setup time effectively improves overall timing for
 *   handling short messages;
 * - larger table allocation can become unbearable because of VM
 *   subsystem penalties (for example on Windows large enough free
 *   results in VM working set trimming, meaning that consequent
 *   malloc would immediately incur working set expansion);
 * - larger table has larger cache footprint, which can affect
 *   performance of other code paths (not necessarily even from same
 *   thread in Hyper-Threading world);
 *
 * Value of 1 is not appropriate for performance reasons.
 */
#if     TABLE_BITS==8

static void gcm_init_8bit(u128 Htable[256], u64 H[2])
{
    int i, j;
    u128 V;

    Htable[0].hi = 0;
    Htable[0].lo = 0;
    V.hi = H[0];
    V.lo = H[1];

    for (Htable[128] = V, i = 64; i > 0; i >>= 1) {
        REDUCE1BIT(V);
        Htable[i] = V;
    }

    for (i = 2; i < 256; i <<= 1) {
        u128 *Hi = Htable + i, H0 = *Hi;
        for (j = 1; j < i; ++j) {
            Hi[j].hi = H0.hi ^ Htable[j].hi;
            Hi[j].lo = H0.lo ^ Htable[j].lo;
        }
    }
}

static void gcm_gmult_8bit(u64 Xi[2], const u128 Htable[256])
{
    u128 Z = { 0, 0 };
    const u8 *xi = (const u8 *)Xi + 15;
    size_t rem, n = *xi;
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    static const size_t rem_8bit[256] = {
        PACK(0x0000), PACK(0x01C2), PACK(0x0384), PACK(0x0246),
        PACK(0x0708), PACK(0x06CA), PACK(0x048C), PACK(0x054E),
        PACK(0x0E10), PACK(0x0FD2), PACK(0x0D94), PACK(0x0C56),
        PACK(0x0918), PACK(0x08DA), PACK(0x0A9C), PACK(0x0B5E),
        PACK(0x1C20), PACK(0x1DE2), PACK(0x1FA4), PACK(0x1E66),
        PACK(0x1B28), PACK(0x1AEA), PACK(0x18AC), PACK(0x196E),
        PACK(0x1230), PACK(0x13F2), PACK(0x11B4), PACK(0x1076),
        PACK(0x1538), PACK(0x14FA), PACK(0x16BC), PACK(0x177E),
        PACK(0x3840), PACK(0x3982), PACK(0x3BC4), PACK(0x3A06),
        PACK(0x3F48), PACK(0x3E8A), PACK(0x3CCC), PACK(0x3D0E),
        PACK(0x3650), PACK(0x3792), PACK(0x35D4), PACK(0x3416),
        PACK(0x3158), PACK(0x309A), PACK(0x32DC), PACK(0x331E),
        PACK(0x2460), PACK(0x25A2), PACK(0x27E4), PACK(0x2626),
        PACK(0x2368), PACK(0x22AA), PACK(0x20EC), PACK(0x212E),
        PACK(0x2A70), PACK(0x2BB2), PACK(0x29F4), PACK(0x2836),
        PACK(0x2D78), PACK(0x2CBA), PACK(0x2EFC), PACK(0x2F3E),
        PACK(0x7080), PACK(0x7142), PACK(0x7304), PACK(0x72C6),
        PACK(0x7788), PACK(0x764A), PACK(0x740C), PACK(0x75CE),
        PACK(0x7E90), PACK(0x7F52), PACK(0x7D14), PACK(0x7CD6),
        PACK(0x7998), PACK(0x785A), PACK(0x7A1C), PACK(0x7BDE),
        PACK(0x6CA0), PACK(0x6D62), PACK(0x6F24), PACK(0x6EE6),
        PACK(0x6BA8), PACK(0x6A6A), PACK(0x682C), PACK(0x69EE),
        PACK(0x62B0), PACK(0x6372), PACK(0x6134), PACK(0x60F6),
        PACK(0x65B8), PACK(0x647A), PACK(0x663C), PACK(0x67FE),
        PACK(0x48C0), PACK(0x4902), PACK(0x4B44), PACK(0x4A86),
        PACK(0x4FC8), PACK(0x4E0A), PACK(0x4C4C), PACK(0x4D8E),
        PACK(0x46D0), PACK(0x4712), PACK(0x4554), PACK(0x4496),
        PACK(0x41D8), PACK(0x401A), PACK(0x425C), PACK(0x439E),
        PACK(0x54E0), PACK(0x5522), PACK(0x5764), PACK(0x56A6),
        PACK(0x53E8), PACK(0x522A), PACK(0x506C), PACK(0x51AE),
        PACK(0x5AF0), PACK(0x5B32), PACK(0x5974), PACK(0x58B6),
        PACK(0x5DF8), PACK(0x5C3A), PACK(0x5E7C), PACK(0x5FBE),
        PACK(0xE100), PACK(0xE0C2), PACK(0xE284), PACK(0xE346),
        PACK(0xE608), PACK(0xE7CA), PACK(0xE58C), PACK(0xE44E),
        PACK(0xEF10), PACK(0xEED2), PACK(0xEC94), PACK(0xED56),
        PACK(0xE818), PACK(0xE9DA), PACK(0xEB9C), PACK(0xEA5E),
        PACK(0xFD20), PACK(0xFCE2), PACK(0xFEA4), PACK(0xFF66),
        PACK(0xFA28), PACK(0xFBEA), PACK(0xF9AC), PACK(0xF86E),
        PACK(0xF330), PACK(0xF2F2), PACK(0xF0B4), PACK(0xF176),
        PACK(0xF438), PACK(0xF5FA), PACK(0xF7BC), PACK(0xF67E),
        PACK(0xD940), PACK(0xD882), PACK(0xDAC4), PACK(0xDB06),
        PACK(0xDE48), PACK(0xDF8A), PACK(0xDDCC), PACK(0xDC0E),
        PACK(0xD750), PACK(0xD692), PACK(0xD4D4), PACK(0xD516),
        PACK(0xD058), PACK(0xD19A), PACK(0xD3DC), PACK(0xD21E),
        PACK(0xC560), PACK(0xC4A2), PACK(0xC6E4), PACK(0xC726),
        PACK(0xC268), PACK(0xC3AA), PACK(0xC1EC), PACK(0xC02E),
        PACK(0xCB70), PACK(0xCAB2), PACK(0xC8F4), PACK(0xC936),
        PACK(0xCC78), PACK(0xCDBA), PACK(0xCFFC), PACK(0xCE3E),
        PACK(0x9180), PACK(0x9042), PACK(0x9204), PACK(0x93C6),
        PACK(0x9688), PACK(0x974A), PACK(0x950C), PACK(0x94CE),
        PACK(0x9F90), PACK(0x9E52), PACK(0x9C14), PACK(0x9DD6),
        PACK(0x9898), PACK(0x995A), PACK(0x9B1C), PACK(0x9ADE),
        PACK(0x8DA0), PACK(0x8C62), PACK(0x8E24), PACK(0x8FE6),
        PACK(0x8AA8), PACK(0x8B6A), PACK(0x892C), PACK(0x88EE),
        PACK(0x83B0), PACK(0x8272), PACK(0x8034), PACK(0x81F6),
        PACK(0x84B8), PACK(0x857A), PACK(0x873C), PACK(0x86FE),
        PACK(0xA9C0), PACK(0xA802), PACK(0xAA44), PACK(0xAB86),
        PACK(0xAEC8), PACK(0xAF0A), PACK(0xAD4C), PACK(0xAC8E),
        PACK(0xA7D0), PACK(0xA612), PACK(0xA454), PACK(0xA596),
        PACK(0xA0D8), PACK(0xA11A), PACK(0xA35C), PACK(0xA29E),
        PACK(0xB5E0), PACK(0xB422), PACK(0xB664), PACK(0xB7A6),
        PACK(0xB2E8), PACK(0xB32A), PACK(0xB16C), PACK(0xB0AE),
        PACK(0xBBF0), PACK(0xBA32), PACK(0xB874), PACK(0xB9B6),
        PACK(0xBCF8), PACK(0xBD3A), PACK(0xBF7C), PACK(0xBEBE)
    };

    while (1) {
        Z.hi ^= Htable[n].hi;
        Z.lo ^= Htable[n].lo;

        if ((u8 *)Xi == xi)
            break;

        n = *(--xi);

        rem = (size_t)Z.lo & 0xff;
        Z.lo = (Z.hi << 56) | (Z.lo >> 8);
        Z.hi = (Z.hi >> 8);
        if (sizeof(size_t) == 8)
            Z.hi ^= rem_8bit[rem];
        else
            Z.hi ^= (u64)rem_8bit[rem] << 32;
    }

    if (is_endian.little) {
# ifdef BSWAP8
        Xi[0] = BSWAP8(Z.hi);
        Xi[1] = BSWAP8(Z.lo);
# else
        u8 *p = (u8 *)Xi;
        u32 v;
        v = (u32)(Z.hi >> 32);
        PUTU32(p, v);
        v = (u32)(Z.hi);
        PUTU32(p + 4, v);
        v = (u32)(Z.lo >> 32);
        PUTU32(p + 8, v);
        v = (u32)(Z.lo);
        PUTU32(p + 12, v);
# endif
    } else {
        Xi[0] = Z.hi;
        Xi[1] = Z.lo;
    }
}

# define GCM_MUL(ctx)      gcm_gmult_8bit(ctx->Xi.u,ctx->Htable)

#elif   TABLE_BITS==4

static void gcm_init_4bit(u128 Htable[16], u64 H[2])
{
    u128 V;
# if defined(OPENSSL_SMALL_FOOTPRINT)
    int i;
# endif

    Htable[0].hi = 0;
    Htable[0].lo = 0;
    V.hi = H[0];
    V.lo = H[1];

# if defined(OPENSSL_SMALL_FOOTPRINT)
    for (Htable[8] = V, i = 4; i > 0; i >>= 1) {
        REDUCE1BIT(V);
        Htable[i] = V;
    }

    for (i = 2; i < 16; i <<= 1) {
        u128 *Hi = Htable + i;
        int j;
        for (V = *Hi, j = 1; j < i; ++j) {
            Hi[j].hi = V.hi ^ Htable[j].hi;
            Hi[j].lo = V.lo ^ Htable[j].lo;
        }
    }
# else
    Htable[8] = V;
    REDUCE1BIT(V);
    Htable[4] = V;
    REDUCE1BIT(V);
    Htable[2] = V;
    REDUCE1BIT(V);
    Htable[1] = V;
    Htable[3].hi = V.hi ^ Htable[2].hi, Htable[3].lo = V.lo ^ Htable[2].lo;
    V = Htable[4];
    Htable[5].hi = V.hi ^ Htable[1].hi, Htable[5].lo = V.lo ^ Htable[1].lo;
    Htable[6].hi = V.hi ^ Htable[2].hi, Htable[6].lo = V.lo ^ Htable[2].lo;
    Htable[7].hi = V.hi ^ Htable[3].hi, Htable[7].lo = V.lo ^ Htable[3].lo;
    V = Htable[8];
    Htable[9].hi = V.hi ^ Htable[1].hi, Htable[9].lo = V.lo ^ Htable[1].lo;
    Htable[10].hi = V.hi ^ Htable[2].hi, Htable[10].lo = V.lo ^ Htable[2].lo;
    Htable[11].hi = V.hi ^ Htable[3].hi, Htable[11].lo = V.lo ^ Htable[3].lo;
    Htable[12].hi = V.hi ^ Htable[4].hi, Htable[12].lo = V.lo ^ Htable[4].lo;
    Htable[13].hi = V.hi ^ Htable[5].hi, Htable[13].lo = V.lo ^ Htable[5].lo;
    Htable[14].hi = V.hi ^ Htable[6].hi, Htable[14].lo = V.lo ^ Htable[6].lo;
    Htable[15].hi = V.hi ^ Htable[7].hi, Htable[15].lo = V.lo ^ Htable[7].lo;
# endif
# if defined(GHASH_ASM) && (defined(__arm__) || defined(__arm))
    /*
     * ARM assembler expects specific dword order in Htable.
     */
    {
        int j;
        const union {
            long one;
            char little;
        } is_endian = { 1 };

        if (is_endian.little)
            for (j = 0; j < 16; ++j) {
                V = Htable[j];
                Htable[j].hi = V.lo;
                Htable[j].lo = V.hi;
        } else
            for (j = 0; j < 16; ++j) {
                V = Htable[j];
                Htable[j].hi = V.lo << 32 | V.lo >> 32;
                Htable[j].lo = V.hi << 32 | V.hi >> 32;
            }
    }
# endif
}

# ifndef GHASH_ASM
static const size_t rem_4bit[16] = {
    PACK(0x0000), PACK(0x1C20), PACK(0x3840), PACK(0x2460),
    PACK(0x7080), PACK(0x6CA0), PACK(0x48C0), PACK(0x54E0),
    PACK(0xE100), PACK(0xFD20), PACK(0xD940), PACK(0xC560),
    PACK(0x9180), PACK(0x8DA0), PACK(0xA9C0), PACK(0xB5E0)
};

static void gcm_gmult_4bit(u64 Xi[2], const u128 Htable[16])
{
    u128 Z;
    int cnt = 15;
    size_t rem, nlo, nhi;
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    nlo = ((const u8 *)Xi)[15];
    nhi = nlo >> 4;
    nlo &= 0xf;

    Z.hi = Htable[nlo].hi;
    Z.lo = Htable[nlo].lo;

    while (1) {
        rem = (size_t)Z.lo & 0xf;
        Z.lo = (Z.hi << 60) | (Z.lo >> 4);
        Z.hi = (Z.hi >> 4);
        if (sizeof(size_t) == 8)
            Z.hi ^= rem_4bit[rem];
        else
            Z.hi ^= (u64)rem_4bit[rem] << 32;

        Z.hi ^= Htable[nhi].hi;
        Z.lo ^= Htable[nhi].lo;

        if (--cnt < 0)
            break;

        nlo = ((const u8 *)Xi)[cnt];
        nhi = nlo >> 4;
        nlo &= 0xf;

        rem = (size_t)Z.lo & 0xf;
        Z.lo = (Z.hi << 60) | (Z.lo >> 4);
        Z.hi = (Z.hi >> 4);
        if (sizeof(size_t) == 8)
            Z.hi ^= rem_4bit[rem];
        else
            Z.hi ^= (u64)rem_4bit[rem] << 32;

        Z.hi ^= Htable[nlo].hi;
        Z.lo ^= Htable[nlo].lo;
    }

    if (is_endian.little) {
#  ifdef BSWAP8
        Xi[0] = BSWAP8(Z.hi);
        Xi[1] = BSWAP8(Z.lo);
#  else
        u8 *p = (u8 *)Xi;
        u32 v;
        v = (u32)(Z.hi >> 32);
        PUTU32(p, v);
        v = (u32)(Z.hi);
        PUTU32(p + 4, v);
        v = (u32)(Z.lo >> 32);
        PUTU32(p + 8, v);
        v = (u32)(Z.lo);
        PUTU32(p + 12, v);
#  endif
    } else {
        Xi[0] = Z.hi;
        Xi[1] = Z.lo;
    }
}

#  if !defined(OPENSSL_SMALL_FOOTPRINT)
/*
 * Streamed gcm_mult_4bit, see CRYPTO_gcm128_[en|de]crypt for
 * details... Compiler-generated code doesn't seem to give any
 * performance improvement, at least not on x86[_64]. It's here
 * mostly as reference and a placeholder for possible future
 * non-trivial optimization[s]...
 */
static void gcm_ghash_4bit(u64 Xi[2], const u128 Htable[16],
                           const u8 *inp, size_t len)
{
    u128 Z;
    int cnt;
    size_t rem, nlo, nhi;
    const union {
        long one;
        char little;
    } is_endian = { 1 };

#   if 1
    do {
        cnt = 15;
        nlo = ((const u8 *)Xi)[15];
        nlo ^= inp[15];
        nhi = nlo >> 4;
        nlo &= 0xf;

        Z.hi = Htable[nlo].hi;
        Z.lo = Htable[nlo].lo;

        while (1) {
            rem = (size_t)Z.lo & 0xf;
            Z.lo = (Z.hi << 60) | (Z.lo >> 4);
            Z.hi = (Z.hi >> 4);
            if (sizeof(size_t) == 8)
                Z.hi ^= rem_4bit[rem];
            else
                Z.hi ^= (u64)rem_4bit[rem] << 32;

            Z.hi ^= Htable[nhi].hi;
            Z.lo ^= Htable[nhi].lo;

            if (--cnt < 0)
                break;

            nlo = ((const u8 *)Xi)[cnt];
            nlo ^= inp[cnt];
            nhi = nlo >> 4;
            nlo &= 0xf;

            rem = (size_t)Z.lo & 0xf;
            Z.lo = (Z.hi << 60) | (Z.lo >> 4);
            Z.hi = (Z.hi >> 4);
            if (sizeof(size_t) == 8)
                Z.hi ^= rem_4bit[rem];
            else
                Z.hi ^= (u64)rem_4bit[rem] << 32;

            Z.hi ^= Htable[nlo].hi;
            Z.lo ^= Htable[nlo].lo;
        }
#   else
    /*
     * Extra 256+16 bytes per-key plus 512 bytes shared tables
     * [should] give ~50% improvement... One could have PACK()-ed
     * the rem_8bit even here, but the priority is to minimize
     * cache footprint...
     */
    u128 Hshr4[16];             /* Htable shifted right by 4 bits */
    u8 Hshl4[16];               /* Htable shifted left by 4 bits */
    static const unsigned short rem_8bit[256] = {
        0x0000, 0x01C2, 0x0384, 0x0246, 0x0708, 0x06CA, 0x048C, 0x054E,
        0x0E10, 0x0FD2, 0x0D94, 0x0C56, 0x0918, 0x08DA, 0x0A9C, 0x0B5E,
        0x1C20, 0x1DE2, 0x1FA4, 0x1E66, 0x1B28, 0x1AEA, 0x18AC, 0x196E,
        0x1230, 0x13F2, 0x11B4, 0x1076, 0x1538, 0x14FA, 0x16BC, 0x177E,
        0x3840, 0x3982, 0x3BC4, 0x3A06, 0x3F48, 0x3E8A, 0x3CCC, 0x3D0E,
        0x3650, 0x3792, 0x35D4, 0x3416, 0x3158, 0x309A, 0x32DC, 0x331E,
        0x2460, 0x25A2, 0x27E4, 0x2626, 0x2368, 0x22AA, 0x20EC, 0x212E,
        0x2A70, 0x2BB2, 0x29F4, 0x2836, 0x2D78, 0x2CBA, 0x2EFC, 0x2F3E,
        0x7080, 0x7142, 0x7304, 0x72C6, 0x7788, 0x764A, 0x740C, 0x75CE,
        0x7E90, 0x7F52, 0x7D14, 0x7CD6, 0x7998, 0x785A, 0x7A1C, 0x7BDE,
        0x6CA0, 0x6D62, 0x6F24, 0x6EE6, 0x6BA8, 0x6A6A, 0x682C, 0x69EE,
        0x62B0, 0x6372, 0x6134, 0x60F6, 0x65B8, 0x647A, 0x663C, 0x67FE,
        0x48C0, 0x4902, 0x4B44, 0x4A86, 0x4FC8, 0x4E0A, 0x4C4C, 0x4D8E,
        0x46D0, 0x4712, 0x4554, 0x4496, 0x41D8, 0x401A, 0x425C, 0x439E,
        0x54E0, 0x5522, 0x5764, 0x56A6, 0x53E8, 0x522A, 0x506C, 0x51AE,
        0x5AF0, 0x5B32, 0x5974, 0x58B6, 0x5DF8, 0x5C3A, 0x5E7C, 0x5FBE,
        0xE100, 0xE0C2, 0xE284, 0xE346, 0xE608, 0xE7CA, 0xE58C, 0xE44E,
        0xEF10, 0xEED2, 0xEC94, 0xED56, 0xE818, 0xE9DA, 0xEB9C, 0xEA5E,
        0xFD20, 0xFCE2, 0xFEA4, 0xFF66, 0xFA28, 0xFBEA, 0xF9AC, 0xF86E,
        0xF330, 0xF2F2, 0xF0B4, 0xF176, 0xF438, 0xF5FA, 0xF7BC, 0xF67E,
        0xD940, 0xD882, 0xDAC4, 0xDB06, 0xDE48, 0xDF8A, 0xDDCC, 0xDC0E,
        0xD750, 0xD692, 0xD4D4, 0xD516, 0xD058, 0xD19A, 0xD3DC, 0xD21E,
        0xC560, 0xC4A2, 0xC6E4, 0xC726, 0xC268, 0xC3AA, 0xC1EC, 0xC02E,
        0xCB70, 0xCAB2, 0xC8F4, 0xC936, 0xCC78, 0xCDBA, 0xCFFC, 0xCE3E,
        0x9180, 0x9042, 0x9204, 0x93C6, 0x9688, 0x974A, 0x950C, 0x94CE,
        0x9F90, 0x9E52, 0x9C14, 0x9DD6, 0x9898, 0x995A, 0x9B1C, 0x9ADE,
        0x8DA0, 0x8C62, 0x8E24, 0x8FE6, 0x8AA8, 0x8B6A, 0x892C, 0x88EE,
        0x83B0, 0x8272, 0x8034, 0x81F6, 0x84B8, 0x857A, 0x873C, 0x86FE,
        0xA9C0, 0xA802, 0xAA44, 0xAB86, 0xAEC8, 0xAF0A, 0xAD4C, 0xAC8E,
        0xA7D0, 0xA612, 0xA454, 0xA596, 0xA0D8, 0xA11A, 0xA35C, 0xA29E,
        0xB5E0, 0xB422, 0xB664, 0xB7A6, 0xB2E8, 0xB32A, 0xB16C, 0xB0AE,
        0xBBF0, 0xBA32, 0xB874, 0xB9B6, 0xBCF8, 0xBD3A, 0xBF7C, 0xBEBE
    };
    /*
     * This pre-processing phase slows down procedure by approximately
     * same time as it makes each loop spin faster. In other words
     * single block performance is approximately same as straightforward
     * "4-bit" implementation, and then it goes only faster...
     */
    for (cnt = 0; cnt < 16; ++cnt) {
        Z.hi = Htable[cnt].hi;
        Z.lo = Htable[cnt].lo;
        Hshr4[cnt].lo = (Z.hi << 60) | (Z.lo >> 4);
        Hshr4[cnt].hi = (Z.hi >> 4);
        Hshl4[cnt] = (u8)(Z.lo << 4);
    }

    do {
        for (Z.lo = 0, Z.hi = 0, cnt = 15; cnt; --cnt) {
            nlo = ((const u8 *)Xi)[cnt];
            nlo ^= inp[cnt];
            nhi = nlo >> 4;
            nlo &= 0xf;

            Z.hi ^= Htable[nlo].hi;
            Z.lo ^= Htable[nlo].lo;

            rem = (size_t)Z.lo & 0xff;

            Z.lo = (Z.hi << 56) | (Z.lo >> 8);
            Z.hi = (Z.hi >> 8);

            Z.hi ^= Hshr4[nhi].hi;
            Z.lo ^= Hshr4[nhi].lo;
            Z.hi ^= (u64)rem_8bit[rem ^ Hshl4[nhi]] << 48;
        }

        nlo = ((const u8 *)Xi)[0];
        nlo ^= inp[0];
        nhi = nlo >> 4;
        nlo &= 0xf;

        Z.hi ^= Htable[nlo].hi;
        Z.lo ^= Htable[nlo].lo;

        rem = (size_t)Z.lo & 0xf;

        Z.lo = (Z.hi << 60) | (Z.lo >> 4);
        Z.hi = (Z.hi >> 4);

        Z.hi ^= Htable[nhi].hi;
        Z.lo ^= Htable[nhi].lo;
        Z.hi ^= ((u64)rem_8bit[rem << 4]) << 48;
#   endif

        if (is_endian.little) {
#   ifdef BSWAP8
            Xi[0] = BSWAP8(Z.hi);
            Xi[1] = BSWAP8(Z.lo);
#   else
            u8 *p = (u8 *)Xi;
            u32 v;
            v = (u32)(Z.hi >> 32);
            PUTU32(p, v);
            v = (u32)(Z.hi);
            PUTU32(p + 4, v);
            v = (u32)(Z.lo >> 32);
            PUTU32(p + 8, v);
            v = (u32)(Z.lo);
            PUTU32(p + 12, v);
#   endif
        } else {
            Xi[0] = Z.hi;
            Xi[1] = Z.lo;
        }
    } while (inp += 16, len -= 16);
}
#  endif
# else
void gcm_gmult_4bit(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_4bit(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                    size_t len);
# endif

# define GCM_MUL(ctx)      gcm_gmult_4bit(ctx->Xi.u,ctx->Htable)
# if defined(GHASH_ASM) || !defined(OPENSSL_SMALL_FOOTPRINT)
#  define GHASH(ctx,in,len) gcm_ghash_4bit((ctx)->Xi.u,(ctx)->Htable,in,len)
/*
 * GHASH_CHUNK is "stride parameter" missioned to mitigate cache trashing
 * effect. In other words idea is to hash data while it's still in L1 cache
 * after encryption pass...
 */
#  define GHASH_CHUNK       (3*1024)
# endif

#else                           /* TABLE_BITS */

static void gcm_gmult_1bit(u64 Xi[2], const u64 H[2])
{
    u128 V, Z = { 0, 0 };
    long X;
    int i, j;
    const long *xi = (const long *)Xi;
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    V.hi = H[0];                /* H is in host byte order, no byte swapping */
    V.lo = H[1];

    for (j = 0; j < 16 / sizeof(long); ++j) {
        if (is_endian.little) {
            if (sizeof(long) == 8) {
# ifdef BSWAP8
                X = (long)(BSWAP8(xi[j]));
# else
                const u8 *p = (const u8 *)(xi + j);
                X = (long)((u64)GETU32(p) << 32 | GETU32(p + 4));
# endif
            } else {
                const u8 *p = (const u8 *)(xi + j);
                X = (long)GETU32(p);
            }
        } else
            X = xi[j];

        for (i = 0; i < 8 * sizeof(long); ++i, X <<= 1) {
            u64 M = (u64)(X >> (8 * sizeof(long) - 1));
            Z.hi ^= V.hi & M;
            Z.lo ^= V.lo & M;

            REDUCE1BIT(V);
        }
    }

    if (is_endian.little) {
# ifdef BSWAP8
        Xi[0] = BSWAP8(Z.hi);
        Xi[1] = BSWAP8(Z.lo);
# else
        u8 *p = (u8 *)Xi;
        u32 v;
        v = (u32)(Z.hi >> 32);
        PUTU32(p, v);
        v = (u32)(Z.hi);
        PUTU32(p + 4, v);
        v = (u32)(Z.lo >> 32);
        PUTU32(p + 8, v);
        v = (u32)(Z.lo);
        PUTU32(p + 12, v);
# endif
    } else {
        Xi[0] = Z.hi;
        Xi[1] = Z.lo;
    }
}

# define GCM_MUL(ctx)      gcm_gmult_1bit(ctx->Xi.u,ctx->H.u)

#endif

#if     TABLE_BITS==4 && (defined(GHASH_ASM) || defined(OPENSSL_CPUID_OBJ))
# if    !defined(I386_ONLY) && \
        (defined(__i386)        || defined(__i386__)    || \
         defined(__x86_64)      || defined(__x86_64__)  || \
         defined(_M_IX86)       || defined(_M_AMD64)    || defined(_M_X64))
#  define GHASH_ASM_X86_OR_64
#  define GCM_FUNCREF_4BIT
extern unsigned int OPENSSL_ia32cap_P[];

void gcm_init_clmul(u128 Htable[16], const u64 Xi[2]);
void gcm_gmult_clmul(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_clmul(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                     size_t len);

#  if defined(__i386) || defined(__i386__) || defined(_M_IX86)
#   define gcm_init_avx   gcm_init_clmul
#   define gcm_gmult_avx  gcm_gmult_clmul
#   define gcm_ghash_avx  gcm_ghash_clmul
#  else
void gcm_init_avx(u128 Htable[16], const u64 Xi[2]);
void gcm_gmult_avx(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_avx(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                   size_t len);
#  endif

#  if   defined(__i386) || defined(__i386__) || defined(_M_IX86)
#   define GHASH_ASM_X86
void gcm_gmult_4bit_mmx(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_4bit_mmx(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                        size_t len);

void gcm_gmult_4bit_x86(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_4bit_x86(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                        size_t len);
#  endif
# elif defined(__arm__) || defined(__arm) || defined(__aarch64__)
#  include "arm_arch.h"
#  if __ARM_MAX_ARCH__>=7
#   define GHASH_ASM_ARM
#   define GCM_FUNCREF_4BIT
#   define PMULL_CAPABLE        (OPENSSL_armcap_P & ARMV8_PMULL)
#   if defined(__arm__) || defined(__arm)
#    define NEON_CAPABLE        (OPENSSL_armcap_P & ARMV7_NEON)
#   endif
void gcm_init_neon(u128 Htable[16], const u64 Xi[2]);
void gcm_gmult_neon(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_neon(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                    size_t len);
void gcm_init_v8(u128 Htable[16], const u64 Xi[2]);
void gcm_gmult_v8(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_v8(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                  size_t len);
#  endif
# elif defined(__sparc__) || defined(__sparc)
#  include "sparc_arch.h"
#  define GHASH_ASM_SPARC
#  define GCM_FUNCREF_4BIT
extern unsigned int OPENSSL_sparcv9cap_P[];
void gcm_init_vis3(u128 Htable[16], const u64 Xi[2]);
void gcm_gmult_vis3(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_vis3(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                    size_t len);
# elif defined(OPENSSL_CPUID_OBJ) && (defined(__powerpc__) || defined(__ppc__) || defined(_ARCH_PPC))
#  include "ppc_arch.h"
#  define GHASH_ASM_PPC
#  define GCM_FUNCREF_4BIT
void gcm_init_p8(u128 Htable[16], const u64 Xi[2]);
void gcm_gmult_p8(u64 Xi[2], const u128 Htable[16]);
void gcm_ghash_p8(u64 Xi[2], const u128 Htable[16], const u8 *inp,
                  size_t len);
# endif
#endif

#ifdef GCM_FUNCREF_4BIT
# undef  GCM_MUL
# define GCM_MUL(ctx)           (*gcm_gmult_p)(ctx->Xi.u,ctx->Htable)
# ifdef GHASH
#  undef  GHASH
#  define GHASH(ctx,in,len)     (*gcm_ghash_p)(ctx->Xi.u,ctx->Htable,in,len)
# endif
#endif

void CRYPTO_gcm128_init(GCM128_CONTEXT *ctx, void *key, block128_f block)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    memset(ctx, 0, sizeof(*ctx));
    ctx->block = block;
    ctx->key = key;

    (*block) (ctx->H.c, ctx->H.c, key);

    if (is_endian.little) {
        /* H is stored in host byte order */
#ifdef BSWAP8
        ctx->H.u[0] = BSWAP8(ctx->H.u[0]);
        ctx->H.u[1] = BSWAP8(ctx->H.u[1]);
#else
        u8 *p = ctx->H.c;
        u64 hi, lo;
        hi = (u64)GETU32(p) << 32 | GETU32(p + 4);
        lo = (u64)GETU32(p + 8) << 32 | GETU32(p + 12);
        ctx->H.u[0] = hi;
        ctx->H.u[1] = lo;
#endif
    }
#if     TABLE_BITS==8
    gcm_init_8bit(ctx->Htable, ctx->H.u);
#elif   TABLE_BITS==4
# if    defined(GHASH)
#  define CTX__GHASH(f) (ctx->ghash = (f))
# else
#  define CTX__GHASH(f) (ctx->ghash = NULL)
# endif
# if    defined(GHASH_ASM_X86_OR_64)
#  if   !defined(GHASH_ASM_X86) || defined(OPENSSL_IA32_SSE2)
    if (OPENSSL_ia32cap_P[1] & (1 << 1)) { /* check PCLMULQDQ bit */
        if (((OPENSSL_ia32cap_P[1] >> 22) & 0x41) == 0x41) { /* AVX+MOVBE */
            gcm_init_avx(ctx->Htable, ctx->H.u);
            ctx->gmult = gcm_gmult_avx;
            CTX__GHASH(gcm_ghash_avx);
        } else {
            gcm_init_clmul(ctx->Htable, ctx->H.u);
            ctx->gmult = gcm_gmult_clmul;
            CTX__GHASH(gcm_ghash_clmul);
        }
        return;
    }
#  endif
    gcm_init_4bit(ctx->Htable, ctx->H.u);
#  if   defined(GHASH_ASM_X86)  /* x86 only */
#   if  defined(OPENSSL_IA32_SSE2)
    if (OPENSSL_ia32cap_P[0] & (1 << 25)) { /* check SSE bit */
#   else
    if (OPENSSL_ia32cap_P[0] & (1 << 23)) { /* check MMX bit */
#   endif
        ctx->gmult = gcm_gmult_4bit_mmx;
        CTX__GHASH(gcm_ghash_4bit_mmx);
    } else {
        ctx->gmult = gcm_gmult_4bit_x86;
        CTX__GHASH(gcm_ghash_4bit_x86);
    }
#  else
    ctx->gmult = gcm_gmult_4bit;
    CTX__GHASH(gcm_ghash_4bit);
#  endif
# elif  defined(GHASH_ASM_ARM)
#  ifdef PMULL_CAPABLE
    if (PMULL_CAPABLE) {
        gcm_init_v8(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_v8;
        CTX__GHASH(gcm_ghash_v8);
    } else
#  endif
#  ifdef NEON_CAPABLE
    if (NEON_CAPABLE) {
        gcm_init_neon(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_neon;
        CTX__GHASH(gcm_ghash_neon);
    } else
#  endif
    {
        gcm_init_4bit(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_4bit;
        CTX__GHASH(gcm_ghash_4bit);
    }
# elif  defined(GHASH_ASM_SPARC)
    if (OPENSSL_sparcv9cap_P[0] & SPARCV9_VIS3) {
        gcm_init_vis3(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_vis3;
        CTX__GHASH(gcm_ghash_vis3);
    } else {
        gcm_init_4bit(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_4bit;
        CTX__GHASH(gcm_ghash_4bit);
    }
# elif  defined(GHASH_ASM_PPC)
    if (OPENSSL_ppccap_P & PPC_CRYPTO207) {
        gcm_init_p8(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_p8;
        CTX__GHASH(gcm_ghash_p8);
    } else {
        gcm_init_4bit(ctx->Htable, ctx->H.u);
        ctx->gmult = gcm_gmult_4bit;
        CTX__GHASH(gcm_ghash_4bit);
    }
# else
    gcm_init_4bit(ctx->Htable, ctx->H.u);
# endif
# undef CTX__GHASH
#endif
}

void CRYPTO_gcm128_setiv(GCM128_CONTEXT *ctx, const unsigned char *iv,
                         size_t len)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    unsigned int ctr;
#ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
#endif

    ctx->len.u[0] = 0;          /* AAD length */
    ctx->len.u[1] = 0;          /* message length */
    ctx->ares = 0;
    ctx->mres = 0;

    if (len == 12) {
        memcpy(ctx->Yi.c, iv, 12);
        ctx->Yi.c[12] = 0;
        ctx->Yi.c[13] = 0;
        ctx->Yi.c[14] = 0;
        ctx->Yi.c[15] = 1;
        ctr = 1;
    } else {
        size_t i;
        u64 len0 = len;

        /* Borrow ctx->Xi to calculate initial Yi */
        ctx->Xi.u[0] = 0;
        ctx->Xi.u[1] = 0;

        while (len >= 16) {
            for (i = 0; i < 16; ++i)
                ctx->Xi.c[i] ^= iv[i];
            GCM_MUL(ctx);
            iv += 16;
            len -= 16;
        }
        if (len) {
            for (i = 0; i < len; ++i)
                ctx->Xi.c[i] ^= iv[i];
            GCM_MUL(ctx);
        }
        len0 <<= 3;
        if (is_endian.little) {
#ifdef BSWAP8
            ctx->Xi.u[1] ^= BSWAP8(len0);
#else
            ctx->Xi.c[8] ^= (u8)(len0 >> 56);
            ctx->Xi.c[9] ^= (u8)(len0 >> 48);
            ctx->Xi.c[10] ^= (u8)(len0 >> 40);
            ctx->Xi.c[11] ^= (u8)(len0 >> 32);
            ctx->Xi.c[12] ^= (u8)(len0 >> 24);
            ctx->Xi.c[13] ^= (u8)(len0 >> 16);
            ctx->Xi.c[14] ^= (u8)(len0 >> 8);
            ctx->Xi.c[15] ^= (u8)(len0);
#endif
        } else {
            ctx->Xi.u[1] ^= len0;
        }

        GCM_MUL(ctx);

        if (is_endian.little)
#ifdef BSWAP4
            ctr = BSWAP4(ctx->Xi.d[3]);
#else
            ctr = GETU32(ctx->Xi.c + 12);
#endif
        else
            ctr = ctx->Xi.d[3];

        /* Copy borrowed Xi to Yi */
        ctx->Yi.u[0] = ctx->Xi.u[0];
        ctx->Yi.u[1] = ctx->Xi.u[1];
    }

    ctx->Xi.u[0] = 0;
    ctx->Xi.u[1] = 0;

    (*ctx->block) (ctx->Yi.c, ctx->EK0.c, ctx->key);
    ++ctr;
    if (is_endian.little)
#ifdef BSWAP4
        ctx->Yi.d[3] = BSWAP4(ctr);
#else
        PUTU32(ctx->Yi.c + 12, ctr);
#endif
    else
        ctx->Yi.d[3] = ctr;
}

int CRYPTO_gcm128_aad(GCM128_CONTEXT *ctx, const unsigned char *aad,
                      size_t len)
{
    size_t i;
    unsigned int n;
    u64 alen = ctx->len.u[0];
#ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
# ifdef GHASH
    void (*gcm_ghash_p) (u64 Xi[2], const u128 Htable[16],
                         const u8 *inp, size_t len) = ctx->ghash;
# endif
#endif

    if (ctx->len.u[1])
        return -2;

    alen += len;
    if (alen > (U64(1) << 61) || (sizeof(len) == 8 && alen < len))
        return -1;
    ctx->len.u[0] = alen;

    n = ctx->ares;
    if (n) {
        while (n && len) {
            ctx->Xi.c[n] ^= *(aad++);
            --len;
            n = (n + 1) % 16;
        }
        if (n == 0)
            GCM_MUL(ctx);
        else {
            ctx->ares = n;
            return 0;
        }
    }
#ifdef GHASH
    if ((i = (len & (size_t)-16))) {
        GHASH(ctx, aad, i);
        aad += i;
        len -= i;
    }
#else
    while (len >= 16) {
        for (i = 0; i < 16; ++i)
            ctx->Xi.c[i] ^= aad[i];
        GCM_MUL(ctx);
        aad += 16;
        len -= 16;
    }
#endif
    if (len) {
        n = (unsigned int)len;
        for (i = 0; i < len; ++i)
            ctx->Xi.c[i] ^= aad[i];
    }

    ctx->ares = n;
    return 0;
}

int CRYPTO_gcm128_encrypt(GCM128_CONTEXT *ctx,
                          const unsigned char *in, unsigned char *out,
                          size_t len)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    unsigned int n, ctr, mres;
    size_t i;
    u64 mlen = ctx->len.u[1];
    block128_f block = ctx->block;
    void *key = ctx->key;
#ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
# if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
    void (*gcm_ghash_p) (u64 Xi[2], const u128 Htable[16],
                         const u8 *inp, size_t len) = ctx->ghash;
# endif
#endif

    mlen += len;
    if (mlen > ((U64(1) << 36) - 32) || (sizeof(len) == 8 && mlen < len))
        return -1;
    ctx->len.u[1] = mlen;

    mres = ctx->mres;

    if (ctx->ares) {
        /* First call to encrypt finalizes GHASH(AAD) */
#if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
        if (len == 0) {
            GCM_MUL(ctx);
            ctx->ares = 0;
            return 0;
        }
        memcpy(ctx->Xn, ctx->Xi.c, sizeof(ctx->Xi));
        ctx->Xi.u[0] = 0;
        ctx->Xi.u[1] = 0;
        mres = sizeof(ctx->Xi);
#else
        GCM_MUL(ctx);
#endif
        ctx->ares = 0;
    }

    if (is_endian.little)
#ifdef BSWAP4
        ctr = BSWAP4(ctx->Yi.d[3]);
#else
        ctr = GETU32(ctx->Yi.c + 12);
#endif
    else
        ctr = ctx->Yi.d[3];

    n = mres % 16;
#if !defined(OPENSSL_SMALL_FOOTPRINT)
    if (16 % sizeof(size_t) == 0) { /* always true actually */
        do {
            if (n) {
# if defined(GHASH)
                while (n && len) {
                    ctx->Xn[mres++] = *(out++) = *(in++) ^ ctx->EKi.c[n];
                    --len;
                    n = (n + 1) % 16;
                }
                if (n == 0) {
                    GHASH(ctx, ctx->Xn, mres);
                    mres = 0;
                } else {
                    ctx->mres = mres;
                    return 0;
                }
# else
                while (n && len) {
                    ctx->Xi.c[n] ^= *(out++) = *(in++) ^ ctx->EKi.c[n];
                    --len;
                    n = (n + 1) % 16;
                }
                if (n == 0) {
                    GCM_MUL(ctx);
                    mres = 0;
                } else {
                    ctx->mres = n;
                    return 0;
                }
# endif
            }
# if defined(STRICT_ALIGNMENT)
            if (((size_t)in | (size_t)out) % sizeof(size_t) != 0)
                break;
# endif
# if defined(GHASH)
            if (len >= 16 && mres) {
                GHASH(ctx, ctx->Xn, mres);
                mres = 0;
            }
#  if defined(GHASH_CHUNK)
            while (len >= GHASH_CHUNK) {
                size_t j = GHASH_CHUNK;

                while (j) {
                    size_t *out_t = (size_t *)out;
                    const size_t *in_t = (const size_t *)in;

                    (*block) (ctx->Yi.c, ctx->EKi.c, key);
                    ++ctr;
                    if (is_endian.little)
#   ifdef BSWAP4
                        ctx->Yi.d[3] = BSWAP4(ctr);
#   else
                        PUTU32(ctx->Yi.c + 12, ctr);
#   endif
                    else
                        ctx->Yi.d[3] = ctr;
                    for (i = 0; i < 16 / sizeof(size_t); ++i)
                        out_t[i] = in_t[i] ^ ctx->EKi.t[i];
                    out += 16;
                    in += 16;
                    j -= 16;
                }
                GHASH(ctx, out - GHASH_CHUNK, GHASH_CHUNK);
                len -= GHASH_CHUNK;
            }
#  endif
            if ((i = (len & (size_t)-16))) {
                size_t j = i;

                while (len >= 16) {
                    size_t *out_t = (size_t *)out;
                    const size_t *in_t = (const size_t *)in;

                    (*block) (ctx->Yi.c, ctx->EKi.c, key);
                    ++ctr;
                    if (is_endian.little)
#  ifdef BSWAP4
                        ctx->Yi.d[3] = BSWAP4(ctr);
#  else
                        PUTU32(ctx->Yi.c + 12, ctr);
#  endif
                    else
                        ctx->Yi.d[3] = ctr;
                    for (i = 0; i < 16 / sizeof(size_t); ++i)
                        out_t[i] = in_t[i] ^ ctx->EKi.t[i];
                    out += 16;
                    in += 16;
                    len -= 16;
                }
                GHASH(ctx, out - j, j);
            }
# else
            while (len >= 16) {
                size_t *out_t = (size_t *)out;
                const size_t *in_t = (const size_t *)in;

                (*block) (ctx->Yi.c, ctx->EKi.c, key);
                ++ctr;
                if (is_endian.little)
#  ifdef BSWAP4
                    ctx->Yi.d[3] = BSWAP4(ctr);
#  else
                    PUTU32(ctx->Yi.c + 12, ctr);
#  endif
                else
                    ctx->Yi.d[3] = ctr;
                for (i = 0; i < 16 / sizeof(size_t); ++i)
                    ctx->Xi.t[i] ^= out_t[i] = in_t[i] ^ ctx->EKi.t[i];
                GCM_MUL(ctx);
                out += 16;
                in += 16;
                len -= 16;
            }
# endif
            if (len) {
                (*block) (ctx->Yi.c, ctx->EKi.c, key);
                ++ctr;
                if (is_endian.little)
# ifdef BSWAP4
                    ctx->Yi.d[3] = BSWAP4(ctr);
# else
                    PUTU32(ctx->Yi.c + 12, ctr);
# endif
                else
                    ctx->Yi.d[3] = ctr;
# if defined(GHASH)
                while (len--) {
                    ctx->Xn[mres++] = out[n] = in[n] ^ ctx->EKi.c[n];
                    ++n;
                }
# else
                while (len--) {
                    ctx->Xi.c[n] ^= out[n] = in[n] ^ ctx->EKi.c[n];
                    ++n;
                }
                mres = n;
# endif
            }

            ctx->mres = mres;
            return 0;
        } while (0);
    }
#endif
    for (i = 0; i < len; ++i) {
        if (n == 0) {
            (*block) (ctx->Yi.c, ctx->EKi.c, key);
            ++ctr;
            if (is_endian.little)
#ifdef BSWAP4
                ctx->Yi.d[3] = BSWAP4(ctr);
#else
                PUTU32(ctx->Yi.c + 12, ctr);
#endif
            else
                ctx->Yi.d[3] = ctr;
        }
#if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
        ctx->Xn[mres++] = out[i] = in[i] ^ ctx->EKi.c[n];
        n = (n + 1) % 16;
        if (mres == sizeof(ctx->Xn)) {
            GHASH(ctx,ctx->Xn,sizeof(ctx->Xn));
            mres = 0;
        }
#else
        ctx->Xi.c[n] ^= out[i] = in[i] ^ ctx->EKi.c[n];
        mres = n = (n + 1) % 16;
        if (n == 0)
            GCM_MUL(ctx);
#endif
    }

    ctx->mres = mres;
    return 0;
}

int CRYPTO_gcm128_decrypt(GCM128_CONTEXT *ctx,
                          const unsigned char *in, unsigned char *out,
                          size_t len)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    unsigned int n, ctr, mres;
    size_t i;
    u64 mlen = ctx->len.u[1];
    block128_f block = ctx->block;
    void *key = ctx->key;
#ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
# if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
    void (*gcm_ghash_p) (u64 Xi[2], const u128 Htable[16],
                         const u8 *inp, size_t len) = ctx->ghash;
# endif
#endif

    mlen += len;
    if (mlen > ((U64(1) << 36) - 32) || (sizeof(len) == 8 && mlen < len))
        return -1;
    ctx->len.u[1] = mlen;

    mres = ctx->mres;

    if (ctx->ares) {
        /* First call to decrypt finalizes GHASH(AAD) */
#if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
        if (len == 0) {
            GCM_MUL(ctx);
            ctx->ares = 0;
            return 0;
        }
        memcpy(ctx->Xn, ctx->Xi.c, sizeof(ctx->Xi));
        ctx->Xi.u[0] = 0;
        ctx->Xi.u[1] = 0;
        mres = sizeof(ctx->Xi);
#else
        GCM_MUL(ctx);
#endif
        ctx->ares = 0;
    }

    if (is_endian.little)
#ifdef BSWAP4
        ctr = BSWAP4(ctx->Yi.d[3]);
#else
        ctr = GETU32(ctx->Yi.c + 12);
#endif
    else
        ctr = ctx->Yi.d[3];

    n = mres % 16;
#if !defined(OPENSSL_SMALL_FOOTPRINT)
    if (16 % sizeof(size_t) == 0) { /* always true actually */
        do {
            if (n) {
# if defined(GHASH)
                while (n && len) {
                    *(out++) = (ctx->Xn[mres++] = *(in++)) ^ ctx->EKi.c[n];
                    --len;
                    n = (n + 1) % 16;
                }
                if (n == 0) {
                    GHASH(ctx, ctx->Xn, mres);
                    mres = 0;
                } else {
                    ctx->mres = mres;
                    return 0;
                }
# else
                while (n && len) {
                    u8 c = *(in++);
                    *(out++) = c ^ ctx->EKi.c[n];
                    ctx->Xi.c[n] ^= c;
                    --len;
                    n = (n + 1) % 16;
                }
                if (n == 0) {
                    GCM_MUL(ctx);
                    mres = 0;
                } else {
                    ctx->mres = n;
                    return 0;
                }
# endif
            }
# if defined(STRICT_ALIGNMENT)
            if (((size_t)in | (size_t)out) % sizeof(size_t) != 0)
                break;
# endif
# if defined(GHASH)
            if (len >= 16 && mres) {
                GHASH(ctx, ctx->Xn, mres);
                mres = 0;
            }
#  if defined(GHASH_CHUNK)
            while (len >= GHASH_CHUNK) {
                size_t j = GHASH_CHUNK;

                GHASH(ctx, in, GHASH_CHUNK);
                while (j) {
                    size_t *out_t = (size_t *)out;
                    const size_t *in_t = (const size_t *)in;

                    (*block) (ctx->Yi.c, ctx->EKi.c, key);
                    ++ctr;
                    if (is_endian.little)
#   ifdef BSWAP4
                        ctx->Yi.d[3] = BSWAP4(ctr);
#   else
                        PUTU32(ctx->Yi.c + 12, ctr);
#   endif
                    else
                        ctx->Yi.d[3] = ctr;
                    for (i = 0; i < 16 / sizeof(size_t); ++i)
                        out_t[i] = in_t[i] ^ ctx->EKi.t[i];
                    out += 16;
                    in += 16;
                    j -= 16;
                }
                len -= GHASH_CHUNK;
            }
#  endif
            if ((i = (len & (size_t)-16))) {
                GHASH(ctx, in, i);
                while (len >= 16) {
                    size_t *out_t = (size_t *)out;
                    const size_t *in_t = (const size_t *)in;

                    (*block) (ctx->Yi.c, ctx->EKi.c, key);
                    ++ctr;
                    if (is_endian.little)
#  ifdef BSWAP4
                        ctx->Yi.d[3] = BSWAP4(ctr);
#  else
                        PUTU32(ctx->Yi.c + 12, ctr);
#  endif
                    else
                        ctx->Yi.d[3] = ctr;
                    for (i = 0; i < 16 / sizeof(size_t); ++i)
                        out_t[i] = in_t[i] ^ ctx->EKi.t[i];
                    out += 16;
                    in += 16;
                    len -= 16;
                }
            }
# else
            while (len >= 16) {
                size_t *out_t = (size_t *)out;
                const size_t *in_t = (const size_t *)in;

                (*block) (ctx->Yi.c, ctx->EKi.c, key);
                ++ctr;
                if (is_endian.little)
#  ifdef BSWAP4
                    ctx->Yi.d[3] = BSWAP4(ctr);
#  else
                    PUTU32(ctx->Yi.c + 12, ctr);
#  endif
                else
                    ctx->Yi.d[3] = ctr;
                for (i = 0; i < 16 / sizeof(size_t); ++i) {
                    size_t c = in[i];
                    out[i] = c ^ ctx->EKi.t[i];
                    ctx->Xi.t[i] ^= c;
                }
                GCM_MUL(ctx);
                out += 16;
                in += 16;
                len -= 16;
            }
# endif
            if (len) {
                (*block) (ctx->Yi.c, ctx->EKi.c, key);
                ++ctr;
                if (is_endian.little)
# ifdef BSWAP4
                    ctx->Yi.d[3] = BSWAP4(ctr);
# else
                    PUTU32(ctx->Yi.c + 12, ctr);
# endif
                else
                    ctx->Yi.d[3] = ctr;
# if defined(GHASH)
                while (len--) {
                    out[n] = (ctx->Xn[mres++] = in[n]) ^ ctx->EKi.c[n];
                    ++n;
                }
# else
                while (len--) {
                    u8 c = in[n];
                    ctx->Xi.c[n] ^= c;
                    out[n] = c ^ ctx->EKi.c[n];
                    ++n;
                }
                mres = n;
# endif
            }

            ctx->mres = mres;
            return 0;
        } while (0);
    }
#endif
    for (i = 0; i < len; ++i) {
        u8 c;
        if (n == 0) {
            (*block) (ctx->Yi.c, ctx->EKi.c, key);
            ++ctr;
            if (is_endian.little)
#ifdef BSWAP4
                ctx->Yi.d[3] = BSWAP4(ctr);
#else
                PUTU32(ctx->Yi.c + 12, ctr);
#endif
            else
                ctx->Yi.d[3] = ctr;
        }
#if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
        out[i] = (ctx->Xn[mres++] = c = in[i]) ^ ctx->EKi.c[n];
        n = (n + 1) % 16;
        if (mres == sizeof(ctx->Xn)) {
            GHASH(ctx,ctx->Xn,sizeof(ctx->Xn));
            mres = 0;
        }
#else
        c = in[i];
        out[i] = c ^ ctx->EKi.c[n];
        ctx->Xi.c[n] ^= c;
        mres = n = (n + 1) % 16;
        if (n == 0)
            GCM_MUL(ctx);
#endif
    }

    ctx->mres = mres;
    return 0;
}

int CRYPTO_gcm128_encrypt_ctr32(GCM128_CONTEXT *ctx,
                                const unsigned char *in, unsigned char *out,
                                size_t len, ctr128_f stream)
{
#if defined(OPENSSL_SMALL_FOOTPRINT)
    return CRYPTO_gcm128_encrypt(ctx, in, out, len);
#else
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    unsigned int n, ctr, mres;
    size_t i;
    u64 mlen = ctx->len.u[1];
    void *key = ctx->key;
# ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
#  ifdef GHASH
    void (*gcm_ghash_p) (u64 Xi[2], const u128 Htable[16],
                         const u8 *inp, size_t len) = ctx->ghash;
#  endif
# endif

    mlen += len;
    if (mlen > ((U64(1) << 36) - 32) || (sizeof(len) == 8 && mlen < len))
        return -1;
    ctx->len.u[1] = mlen;

    mres = ctx->mres;

    if (ctx->ares) {
        /* First call to encrypt finalizes GHASH(AAD) */
#if defined(GHASH)
        if (len == 0) {
            GCM_MUL(ctx);
            ctx->ares = 0;
            return 0;
        }
        memcpy(ctx->Xn, ctx->Xi.c, sizeof(ctx->Xi));
        ctx->Xi.u[0] = 0;
        ctx->Xi.u[1] = 0;
        mres = sizeof(ctx->Xi);
#else
        GCM_MUL(ctx);
#endif
        ctx->ares = 0;
    }

    if (is_endian.little)
# ifdef BSWAP4
        ctr = BSWAP4(ctx->Yi.d[3]);
# else
        ctr = GETU32(ctx->Yi.c + 12);
# endif
    else
        ctr = ctx->Yi.d[3];

    n = mres % 16;
    if (n) {
# if defined(GHASH)
        while (n && len) {
            ctx->Xn[mres++] = *(out++) = *(in++) ^ ctx->EKi.c[n];
            --len;
            n = (n + 1) % 16;
        }
        if (n == 0) {
            GHASH(ctx, ctx->Xn, mres);
            mres = 0;
        } else {
            ctx->mres = mres;
            return 0;
        }
# else
        while (n && len) {
            ctx->Xi.c[n] ^= *(out++) = *(in++) ^ ctx->EKi.c[n];
            --len;
            n = (n + 1) % 16;
        }
        if (n == 0) {
            GCM_MUL(ctx);
            mres = 0;
        } else {
            ctx->mres = n;
            return 0;
        }
# endif
    }
# if defined(GHASH)
        if (len >= 16 && mres) {
            GHASH(ctx, ctx->Xn, mres);
            mres = 0;
        }
#  if defined(GHASH_CHUNK)
    while (len >= GHASH_CHUNK) {
        (*stream) (in, out, GHASH_CHUNK / 16, key, ctx->Yi.c);
        ctr += GHASH_CHUNK / 16;
        if (is_endian.little)
#   ifdef BSWAP4
            ctx->Yi.d[3] = BSWAP4(ctr);
#   else
            PUTU32(ctx->Yi.c + 12, ctr);
#   endif
        else
            ctx->Yi.d[3] = ctr;
        GHASH(ctx, out, GHASH_CHUNK);
        out += GHASH_CHUNK;
        in += GHASH_CHUNK;
        len -= GHASH_CHUNK;
    }
#  endif
# endif
    if ((i = (len & (size_t)-16))) {
        size_t j = i / 16;

        (*stream) (in, out, j, key, ctx->Yi.c);
        ctr += (unsigned int)j;
        if (is_endian.little)
# ifdef BSWAP4
            ctx->Yi.d[3] = BSWAP4(ctr);
# else
            PUTU32(ctx->Yi.c + 12, ctr);
# endif
        else
            ctx->Yi.d[3] = ctr;
        in += i;
        len -= i;
# if defined(GHASH)
        GHASH(ctx, out, i);
        out += i;
# else
        while (j--) {
            for (i = 0; i < 16; ++i)
                ctx->Xi.c[i] ^= out[i];
            GCM_MUL(ctx);
            out += 16;
        }
# endif
    }
    if (len) {
        (*ctx->block) (ctx->Yi.c, ctx->EKi.c, key);
        ++ctr;
        if (is_endian.little)
# ifdef BSWAP4
            ctx->Yi.d[3] = BSWAP4(ctr);
# else
            PUTU32(ctx->Yi.c + 12, ctr);
# endif
        else
            ctx->Yi.d[3] = ctr;
        while (len--) {
# if defined(GHASH)
            ctx->Xn[mres++] = out[n] = in[n] ^ ctx->EKi.c[n];
# else
            ctx->Xi.c[mres++] ^= out[n] = in[n] ^ ctx->EKi.c[n];
# endif
            ++n;
        }
    }

    ctx->mres = mres;
    return 0;
#endif
}

int CRYPTO_gcm128_decrypt_ctr32(GCM128_CONTEXT *ctx,
                                const unsigned char *in, unsigned char *out,
                                size_t len, ctr128_f stream)
{
#if defined(OPENSSL_SMALL_FOOTPRINT)
    return CRYPTO_gcm128_decrypt(ctx, in, out, len);
#else
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    unsigned int n, ctr, mres;
    size_t i;
    u64 mlen = ctx->len.u[1];
    void *key = ctx->key;
# ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
#  ifdef GHASH
    void (*gcm_ghash_p) (u64 Xi[2], const u128 Htable[16],
                         const u8 *inp, size_t len) = ctx->ghash;
#  endif
# endif

    mlen += len;
    if (mlen > ((U64(1) << 36) - 32) || (sizeof(len) == 8 && mlen < len))
        return -1;
    ctx->len.u[1] = mlen;

    mres = ctx->mres;

    if (ctx->ares) {
        /* First call to decrypt finalizes GHASH(AAD) */
# if defined(GHASH)
        if (len == 0) {
            GCM_MUL(ctx);
            ctx->ares = 0;
            return 0;
        }
        memcpy(ctx->Xn, ctx->Xi.c, sizeof(ctx->Xi));
        ctx->Xi.u[0] = 0;
        ctx->Xi.u[1] = 0;
        mres = sizeof(ctx->Xi);
# else
        GCM_MUL(ctx);
# endif
        ctx->ares = 0;
    }

    if (is_endian.little)
# ifdef BSWAP4
        ctr = BSWAP4(ctx->Yi.d[3]);
# else
        ctr = GETU32(ctx->Yi.c + 12);
# endif
    else
        ctr = ctx->Yi.d[3];

    n = mres % 16;
    if (n) {
# if defined(GHASH)
        while (n && len) {
            *(out++) = (ctx->Xn[mres++] = *(in++)) ^ ctx->EKi.c[n];
            --len;
            n = (n + 1) % 16;
        }
        if (n == 0) {
            GHASH(ctx, ctx->Xn, mres);
            mres = 0;
        } else {
            ctx->mres = mres;
            return 0;
        }
# else
        while (n && len) {
            u8 c = *(in++);
            *(out++) = c ^ ctx->EKi.c[n];
            ctx->Xi.c[n] ^= c;
            --len;
            n = (n + 1) % 16;
        }
        if (n == 0) {
            GCM_MUL(ctx);
            mres = 0;
        } else {
            ctx->mres = n;
            return 0;
        }
# endif
    }
# if defined(GHASH)
    if (len >= 16 && mres) {
        GHASH(ctx, ctx->Xn, mres);
        mres = 0;
    }
#  if defined(GHASH_CHUNK)
    while (len >= GHASH_CHUNK) {
        GHASH(ctx, in, GHASH_CHUNK);
        (*stream) (in, out, GHASH_CHUNK / 16, key, ctx->Yi.c);
        ctr += GHASH_CHUNK / 16;
        if (is_endian.little)
#   ifdef BSWAP4
            ctx->Yi.d[3] = BSWAP4(ctr);
#   else
            PUTU32(ctx->Yi.c + 12, ctr);
#   endif
        else
            ctx->Yi.d[3] = ctr;
        out += GHASH_CHUNK;
        in += GHASH_CHUNK;
        len -= GHASH_CHUNK;
    }
#  endif
# endif
    if ((i = (len & (size_t)-16))) {
        size_t j = i / 16;

# if defined(GHASH)
        GHASH(ctx, in, i);
# else
        while (j--) {
            size_t k;
            for (k = 0; k < 16; ++k)
                ctx->Xi.c[k] ^= in[k];
            GCM_MUL(ctx);
            in += 16;
        }
        j = i / 16;
        in -= i;
# endif
        (*stream) (in, out, j, key, ctx->Yi.c);
        ctr += (unsigned int)j;
        if (is_endian.little)
# ifdef BSWAP4
            ctx->Yi.d[3] = BSWAP4(ctr);
# else
            PUTU32(ctx->Yi.c + 12, ctr);
# endif
        else
            ctx->Yi.d[3] = ctr;
        out += i;
        in += i;
        len -= i;
    }
    if (len) {
        (*ctx->block) (ctx->Yi.c, ctx->EKi.c, key);
        ++ctr;
        if (is_endian.little)
# ifdef BSWAP4
            ctx->Yi.d[3] = BSWAP4(ctr);
# else
            PUTU32(ctx->Yi.c + 12, ctr);
# endif
        else
            ctx->Yi.d[3] = ctr;
        while (len--) {
# if defined(GHASH)
            out[n] = (ctx->Xn[mres++] = in[n]) ^ ctx->EKi.c[n];
# else
            u8 c = in[n];
            ctx->Xi.c[mres++] ^= c;
            out[n] = c ^ ctx->EKi.c[n];
# endif
            ++n;
        }
    }

    ctx->mres = mres;
    return 0;
#endif
}

int CRYPTO_gcm128_finish(GCM128_CONTEXT *ctx, const unsigned char *tag,
                         size_t len)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };
    u64 alen = ctx->len.u[0] << 3;
    u64 clen = ctx->len.u[1] << 3;
#ifdef GCM_FUNCREF_4BIT
    void (*gcm_gmult_p) (u64 Xi[2], const u128 Htable[16]) = ctx->gmult;
# if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
    void (*gcm_ghash_p) (u64 Xi[2], const u128 Htable[16],
                         const u8 *inp, size_t len) = ctx->ghash;
# endif
#endif

#if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
    u128 bitlen;
    unsigned int mres = ctx->mres;

    if (mres) {
        unsigned blocks = (mres + 15) & -16;

        memset(ctx->Xn + mres, 0, blocks - mres);
        mres = blocks;
        if (mres == sizeof(ctx->Xn)) {
            GHASH(ctx, ctx->Xn, mres);
            mres = 0;
        }
    } else if (ctx->ares) {
        GCM_MUL(ctx);
    }
#else
    if (ctx->mres || ctx->ares)
        GCM_MUL(ctx);
#endif

    if (is_endian.little) {
#ifdef BSWAP8
        alen = BSWAP8(alen);
        clen = BSWAP8(clen);
#else
        u8 *p = ctx->len.c;

        ctx->len.u[0] = alen;
        ctx->len.u[1] = clen;

        alen = (u64)GETU32(p) << 32 | GETU32(p + 4);
        clen = (u64)GETU32(p + 8) << 32 | GETU32(p + 12);
#endif
    }

#if defined(GHASH) && !defined(OPENSSL_SMALL_FOOTPRINT)
    bitlen.hi = alen;
    bitlen.lo = clen;
    memcpy(ctx->Xn + mres, &bitlen, sizeof(bitlen));
    mres += sizeof(bitlen);
    GHASH(ctx, ctx->Xn, mres);
#else
    ctx->Xi.u[0] ^= alen;
    ctx->Xi.u[1] ^= clen;
    GCM_MUL(ctx);
#endif

    ctx->Xi.u[0] ^= ctx->EK0.u[0];
    ctx->Xi.u[1] ^= ctx->EK0.u[1];

    if (tag && len <= sizeof(ctx->Xi))
        return CRYPTO_memcmp(ctx->Xi.c, tag, len);
    else
        return -1;
}

void CRYPTO_gcm128_tag(GCM128_CONTEXT *ctx, unsigned char *tag, size_t len)
{
    CRYPTO_gcm128_finish(ctx, NULL, 0);
    memcpy(tag, ctx->Xi.c,
           len <= sizeof(ctx->Xi.c) ? len : sizeof(ctx->Xi.c));
}

GCM128_CONTEXT *CRYPTO_gcm128_new(void *key, block128_f block)
{
    GCM128_CONTEXT *ret;

    if ((ret = OPENSSL_malloc(sizeof(*ret))) != NULL)
        CRYPTO_gcm128_init(ret, key, block);

    return ret;
}

void CRYPTO_gcm128_release(GCM128_CONTEXT *ctx)
{
    OPENSSL_clear_free(ctx, sizeof(*ctx));
}
