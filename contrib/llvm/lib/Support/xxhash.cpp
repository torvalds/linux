/*
*  xxHash - Fast Hash algorithm
*  Copyright (C) 2012-2016, Yann Collet
*
*  BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions are
*  met:
*
*  * Redistributions of source code must retain the above copyright
*  notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*  copyright notice, this list of conditions and the following disclaimer
*  in the documentation and/or other materials provided with the
*  distribution.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*  You can contact the author at :
*  - xxHash homepage: http://www.xxhash.com
*  - xxHash source repository : https://github.com/Cyan4973/xxHash
*/

/* based on revision d2df04efcbef7d7f6886d345861e5dfda4edacc1 Removed
 * everything but a simple interface for computing XXh64. */

#include "llvm/Support/xxhash.h"
#include "llvm/Support/Endian.h"

#include <stdlib.h>
#include <string.h>

using namespace llvm;
using namespace support;

static uint64_t rotl64(uint64_t X, size_t R) {
  return (X << R) | (X >> (64 - R));
}

static const uint64_t PRIME64_1 = 11400714785074694791ULL;
static const uint64_t PRIME64_2 = 14029467366897019727ULL;
static const uint64_t PRIME64_3 = 1609587929392839161ULL;
static const uint64_t PRIME64_4 = 9650029242287828579ULL;
static const uint64_t PRIME64_5 = 2870177450012600261ULL;

static uint64_t round(uint64_t Acc, uint64_t Input) {
  Acc += Input * PRIME64_2;
  Acc = rotl64(Acc, 31);
  Acc *= PRIME64_1;
  return Acc;
}

static uint64_t mergeRound(uint64_t Acc, uint64_t Val) {
  Val = round(0, Val);
  Acc ^= Val;
  Acc = Acc * PRIME64_1 + PRIME64_4;
  return Acc;
}

uint64_t llvm::xxHash64(StringRef Data) {
  size_t Len = Data.size();
  uint64_t Seed = 0;
  const unsigned char *P = Data.bytes_begin();
  const unsigned char *const BEnd = Data.bytes_end();
  uint64_t H64;

  if (Len >= 32) {
    const unsigned char *const Limit = BEnd - 32;
    uint64_t V1 = Seed + PRIME64_1 + PRIME64_2;
    uint64_t V2 = Seed + PRIME64_2;
    uint64_t V3 = Seed + 0;
    uint64_t V4 = Seed - PRIME64_1;

    do {
      V1 = round(V1, endian::read64le(P));
      P += 8;
      V2 = round(V2, endian::read64le(P));
      P += 8;
      V3 = round(V3, endian::read64le(P));
      P += 8;
      V4 = round(V4, endian::read64le(P));
      P += 8;
    } while (P <= Limit);

    H64 = rotl64(V1, 1) + rotl64(V2, 7) + rotl64(V3, 12) + rotl64(V4, 18);
    H64 = mergeRound(H64, V1);
    H64 = mergeRound(H64, V2);
    H64 = mergeRound(H64, V3);
    H64 = mergeRound(H64, V4);

  } else {
    H64 = Seed + PRIME64_5;
  }

  H64 += (uint64_t)Len;

  while (P + 8 <= BEnd) {
    uint64_t const K1 = round(0, endian::read64le(P));
    H64 ^= K1;
    H64 = rotl64(H64, 27) * PRIME64_1 + PRIME64_4;
    P += 8;
  }

  if (P + 4 <= BEnd) {
    H64 ^= (uint64_t)(endian::read32le(P)) * PRIME64_1;
    H64 = rotl64(H64, 23) * PRIME64_2 + PRIME64_3;
    P += 4;
  }

  while (P < BEnd) {
    H64 ^= (*P) * PRIME64_5;
    H64 = rotl64(H64, 11) * PRIME64_1;
    P++;
  }

  H64 ^= H64 >> 33;
  H64 *= PRIME64_2;
  H64 ^= H64 >> 29;
  H64 *= PRIME64_3;
  H64 ^= H64 >> 32;

  return H64;
}

uint64_t llvm::xxHash64(ArrayRef<uint8_t> Data) {
  return xxHash64({(const char *)Data.data(), Data.size()});
}
