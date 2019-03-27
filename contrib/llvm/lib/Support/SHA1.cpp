//====- SHA1.cpp - Private copy of the SHA1 implementation ---*- C++ -* ======//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This code is taken from public domain
// (http://oauth.googlecode.com/svn/code/c/liboauth/src/sha1.c and
// http://cvsweb.netbsd.org/bsdweb.cgi/src/common/lib/libc/hash/sha1/sha1.c?rev=1.6)
// and modified by wrapping it in a C++ interface for LLVM,
// and removing unnecessary code.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/SHA1.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Host.h"
using namespace llvm;

#include <stdint.h>
#include <string.h>

#if defined(BYTE_ORDER) && defined(BIG_ENDIAN) && BYTE_ORDER == BIG_ENDIAN
#define SHA_BIG_ENDIAN
#endif

static uint32_t rol(uint32_t Number, int Bits) {
  return (Number << Bits) | (Number >> (32 - Bits));
}

static uint32_t blk0(uint32_t *Buf, int I) { return Buf[I]; }

static uint32_t blk(uint32_t *Buf, int I) {
  Buf[I & 15] = rol(Buf[(I + 13) & 15] ^ Buf[(I + 8) & 15] ^ Buf[(I + 2) & 15] ^
                        Buf[I & 15],
                    1);
  return Buf[I & 15];
}

static void r0(uint32_t &A, uint32_t &B, uint32_t &C, uint32_t &D, uint32_t &E,
               int I, uint32_t *Buf) {
  E += ((B & (C ^ D)) ^ D) + blk0(Buf, I) + 0x5A827999 + rol(A, 5);
  B = rol(B, 30);
}

static void r1(uint32_t &A, uint32_t &B, uint32_t &C, uint32_t &D, uint32_t &E,
               int I, uint32_t *Buf) {
  E += ((B & (C ^ D)) ^ D) + blk(Buf, I) + 0x5A827999 + rol(A, 5);
  B = rol(B, 30);
}

static void r2(uint32_t &A, uint32_t &B, uint32_t &C, uint32_t &D, uint32_t &E,
               int I, uint32_t *Buf) {
  E += (B ^ C ^ D) + blk(Buf, I) + 0x6ED9EBA1 + rol(A, 5);
  B = rol(B, 30);
}

static void r3(uint32_t &A, uint32_t &B, uint32_t &C, uint32_t &D, uint32_t &E,
               int I, uint32_t *Buf) {
  E += (((B | C) & D) | (B & C)) + blk(Buf, I) + 0x8F1BBCDC + rol(A, 5);
  B = rol(B, 30);
}

static void r4(uint32_t &A, uint32_t &B, uint32_t &C, uint32_t &D, uint32_t &E,
               int I, uint32_t *Buf) {
  E += (B ^ C ^ D) + blk(Buf, I) + 0xCA62C1D6 + rol(A, 5);
  B = rol(B, 30);
}

/* code */
#define SHA1_K0 0x5a827999
#define SHA1_K20 0x6ed9eba1
#define SHA1_K40 0x8f1bbcdc
#define SHA1_K60 0xca62c1d6

#define SEED_0 0x67452301
#define SEED_1 0xefcdab89
#define SEED_2 0x98badcfe
#define SEED_3 0x10325476
#define SEED_4 0xc3d2e1f0

void SHA1::init() {
  InternalState.State[0] = SEED_0;
  InternalState.State[1] = SEED_1;
  InternalState.State[2] = SEED_2;
  InternalState.State[3] = SEED_3;
  InternalState.State[4] = SEED_4;
  InternalState.ByteCount = 0;
  InternalState.BufferOffset = 0;
}

void SHA1::hashBlock() {
  uint32_t A = InternalState.State[0];
  uint32_t B = InternalState.State[1];
  uint32_t C = InternalState.State[2];
  uint32_t D = InternalState.State[3];
  uint32_t E = InternalState.State[4];

  // 4 rounds of 20 operations each. Loop unrolled.
  r0(A, B, C, D, E, 0, InternalState.Buffer.L);
  r0(E, A, B, C, D, 1, InternalState.Buffer.L);
  r0(D, E, A, B, C, 2, InternalState.Buffer.L);
  r0(C, D, E, A, B, 3, InternalState.Buffer.L);
  r0(B, C, D, E, A, 4, InternalState.Buffer.L);
  r0(A, B, C, D, E, 5, InternalState.Buffer.L);
  r0(E, A, B, C, D, 6, InternalState.Buffer.L);
  r0(D, E, A, B, C, 7, InternalState.Buffer.L);
  r0(C, D, E, A, B, 8, InternalState.Buffer.L);
  r0(B, C, D, E, A, 9, InternalState.Buffer.L);
  r0(A, B, C, D, E, 10, InternalState.Buffer.L);
  r0(E, A, B, C, D, 11, InternalState.Buffer.L);
  r0(D, E, A, B, C, 12, InternalState.Buffer.L);
  r0(C, D, E, A, B, 13, InternalState.Buffer.L);
  r0(B, C, D, E, A, 14, InternalState.Buffer.L);
  r0(A, B, C, D, E, 15, InternalState.Buffer.L);
  r1(E, A, B, C, D, 16, InternalState.Buffer.L);
  r1(D, E, A, B, C, 17, InternalState.Buffer.L);
  r1(C, D, E, A, B, 18, InternalState.Buffer.L);
  r1(B, C, D, E, A, 19, InternalState.Buffer.L);

  r2(A, B, C, D, E, 20, InternalState.Buffer.L);
  r2(E, A, B, C, D, 21, InternalState.Buffer.L);
  r2(D, E, A, B, C, 22, InternalState.Buffer.L);
  r2(C, D, E, A, B, 23, InternalState.Buffer.L);
  r2(B, C, D, E, A, 24, InternalState.Buffer.L);
  r2(A, B, C, D, E, 25, InternalState.Buffer.L);
  r2(E, A, B, C, D, 26, InternalState.Buffer.L);
  r2(D, E, A, B, C, 27, InternalState.Buffer.L);
  r2(C, D, E, A, B, 28, InternalState.Buffer.L);
  r2(B, C, D, E, A, 29, InternalState.Buffer.L);
  r2(A, B, C, D, E, 30, InternalState.Buffer.L);
  r2(E, A, B, C, D, 31, InternalState.Buffer.L);
  r2(D, E, A, B, C, 32, InternalState.Buffer.L);
  r2(C, D, E, A, B, 33, InternalState.Buffer.L);
  r2(B, C, D, E, A, 34, InternalState.Buffer.L);
  r2(A, B, C, D, E, 35, InternalState.Buffer.L);
  r2(E, A, B, C, D, 36, InternalState.Buffer.L);
  r2(D, E, A, B, C, 37, InternalState.Buffer.L);
  r2(C, D, E, A, B, 38, InternalState.Buffer.L);
  r2(B, C, D, E, A, 39, InternalState.Buffer.L);

  r3(A, B, C, D, E, 40, InternalState.Buffer.L);
  r3(E, A, B, C, D, 41, InternalState.Buffer.L);
  r3(D, E, A, B, C, 42, InternalState.Buffer.L);
  r3(C, D, E, A, B, 43, InternalState.Buffer.L);
  r3(B, C, D, E, A, 44, InternalState.Buffer.L);
  r3(A, B, C, D, E, 45, InternalState.Buffer.L);
  r3(E, A, B, C, D, 46, InternalState.Buffer.L);
  r3(D, E, A, B, C, 47, InternalState.Buffer.L);
  r3(C, D, E, A, B, 48, InternalState.Buffer.L);
  r3(B, C, D, E, A, 49, InternalState.Buffer.L);
  r3(A, B, C, D, E, 50, InternalState.Buffer.L);
  r3(E, A, B, C, D, 51, InternalState.Buffer.L);
  r3(D, E, A, B, C, 52, InternalState.Buffer.L);
  r3(C, D, E, A, B, 53, InternalState.Buffer.L);
  r3(B, C, D, E, A, 54, InternalState.Buffer.L);
  r3(A, B, C, D, E, 55, InternalState.Buffer.L);
  r3(E, A, B, C, D, 56, InternalState.Buffer.L);
  r3(D, E, A, B, C, 57, InternalState.Buffer.L);
  r3(C, D, E, A, B, 58, InternalState.Buffer.L);
  r3(B, C, D, E, A, 59, InternalState.Buffer.L);

  r4(A, B, C, D, E, 60, InternalState.Buffer.L);
  r4(E, A, B, C, D, 61, InternalState.Buffer.L);
  r4(D, E, A, B, C, 62, InternalState.Buffer.L);
  r4(C, D, E, A, B, 63, InternalState.Buffer.L);
  r4(B, C, D, E, A, 64, InternalState.Buffer.L);
  r4(A, B, C, D, E, 65, InternalState.Buffer.L);
  r4(E, A, B, C, D, 66, InternalState.Buffer.L);
  r4(D, E, A, B, C, 67, InternalState.Buffer.L);
  r4(C, D, E, A, B, 68, InternalState.Buffer.L);
  r4(B, C, D, E, A, 69, InternalState.Buffer.L);
  r4(A, B, C, D, E, 70, InternalState.Buffer.L);
  r4(E, A, B, C, D, 71, InternalState.Buffer.L);
  r4(D, E, A, B, C, 72, InternalState.Buffer.L);
  r4(C, D, E, A, B, 73, InternalState.Buffer.L);
  r4(B, C, D, E, A, 74, InternalState.Buffer.L);
  r4(A, B, C, D, E, 75, InternalState.Buffer.L);
  r4(E, A, B, C, D, 76, InternalState.Buffer.L);
  r4(D, E, A, B, C, 77, InternalState.Buffer.L);
  r4(C, D, E, A, B, 78, InternalState.Buffer.L);
  r4(B, C, D, E, A, 79, InternalState.Buffer.L);

  InternalState.State[0] += A;
  InternalState.State[1] += B;
  InternalState.State[2] += C;
  InternalState.State[3] += D;
  InternalState.State[4] += E;
}

void SHA1::addUncounted(uint8_t Data) {
#ifdef SHA_BIG_ENDIAN
  InternalState.Buffer.C[InternalState.BufferOffset] = Data;
#else
  InternalState.Buffer.C[InternalState.BufferOffset ^ 3] = Data;
#endif

  InternalState.BufferOffset++;
  if (InternalState.BufferOffset == BLOCK_LENGTH) {
    hashBlock();
    InternalState.BufferOffset = 0;
  }
}

void SHA1::writebyte(uint8_t Data) {
  ++InternalState.ByteCount;
  addUncounted(Data);
}

void SHA1::update(ArrayRef<uint8_t> Data) {
  for (auto &C : Data)
    writebyte(C);
}

void SHA1::pad() {
  // Implement SHA-1 padding (fips180-2 5.1.1)

  // Pad with 0x80 followed by 0x00 until the end of the block
  addUncounted(0x80);
  while (InternalState.BufferOffset != 56)
    addUncounted(0x00);

  // Append length in the last 8 bytes
  addUncounted(0); // We're only using 32 bit lengths
  addUncounted(0); // But SHA-1 supports 64 bit lengths
  addUncounted(0); // So zero pad the top bits
  addUncounted(InternalState.ByteCount >> 29); // Shifting to multiply by 8
  addUncounted(InternalState.ByteCount >>
               21); // as SHA-1 supports bitstreams as well as
  addUncounted(InternalState.ByteCount >> 13); // byte.
  addUncounted(InternalState.ByteCount >> 5);
  addUncounted(InternalState.ByteCount << 3);
}

StringRef SHA1::final() {
  // Pad to complete the last block
  pad();

#ifdef SHA_BIG_ENDIAN
  // Just copy the current state
  for (int i = 0; i < 5; i++) {
    HashResult[i] = InternalState.State[i];
  }
#else
  // Swap byte order back
  for (int i = 0; i < 5; i++) {
    HashResult[i] = (((InternalState.State[i]) << 24) & 0xff000000) |
                    (((InternalState.State[i]) << 8) & 0x00ff0000) |
                    (((InternalState.State[i]) >> 8) & 0x0000ff00) |
                    (((InternalState.State[i]) >> 24) & 0x000000ff);
  }
#endif

  // Return pointer to hash (20 characters)
  return StringRef((char *)HashResult, HASH_LENGTH);
}

StringRef SHA1::result() {
  auto StateToRestore = InternalState;

  auto Hash = final();

  // Restore the state
  InternalState = StateToRestore;

  // Return pointer to hash (20 characters)
  return Hash;
}

std::array<uint8_t, 20> SHA1::hash(ArrayRef<uint8_t> Data) {
  SHA1 Hash;
  Hash.update(Data);
  StringRef S = Hash.final();

  std::array<uint8_t, 20> Arr;
  memcpy(Arr.data(), S.data(), S.size());
  return Arr;
}
