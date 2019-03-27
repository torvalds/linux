//===- StringTableBuilder.cpp - String table building utility -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/StringTableBuilder.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

using namespace llvm;

StringTableBuilder::~StringTableBuilder() = default;

void StringTableBuilder::initSize() {
  // Account for leading bytes in table so that offsets returned from add are
  // correct.
  switch (K) {
  case RAW:
  case DWARF:
    Size = 0;
    break;
  case MachO:
  case ELF:
    // Start the table with a NUL byte.
    Size = 1;
    break;
  case WinCOFF:
    // Make room to write the table size later.
    Size = 4;
    break;
  }
}

StringTableBuilder::StringTableBuilder(Kind K, unsigned Alignment)
    : K(K), Alignment(Alignment) {
  initSize();
}

void StringTableBuilder::write(raw_ostream &OS) const {
  assert(isFinalized());
  SmallString<0> Data;
  Data.resize(getSize());
  write((uint8_t *)Data.data());
  OS << Data;
}

using StringPair = std::pair<CachedHashStringRef, size_t>;

void StringTableBuilder::write(uint8_t *Buf) const {
  assert(isFinalized());
  for (const StringPair &P : StringIndexMap) {
    StringRef Data = P.first.val();
    if (!Data.empty())
      memcpy(Buf + P.second, Data.data(), Data.size());
  }
  if (K != WinCOFF)
    return;
  support::endian::write32le(Buf, Size);
}

// Returns the character at Pos from end of a string.
static int charTailAt(StringPair *P, size_t Pos) {
  StringRef S = P->first.val();
  if (Pos >= S.size())
    return -1;
  return (unsigned char)S[S.size() - Pos - 1];
}

// Three-way radix quicksort. This is much faster than std::sort with strcmp
// because it does not compare characters that we already know the same.
static void multikeySort(MutableArrayRef<StringPair *> Vec, int Pos) {
tailcall:
  if (Vec.size() <= 1)
    return;

  // Partition items so that items in [0, I) are greater than the pivot,
  // [I, J) are the same as the pivot, and [J, Vec.size()) are less than
  // the pivot.
  int Pivot = charTailAt(Vec[0], Pos);
  size_t I = 0;
  size_t J = Vec.size();
  for (size_t K = 1; K < J;) {
    int C = charTailAt(Vec[K], Pos);
    if (C > Pivot)
      std::swap(Vec[I++], Vec[K++]);
    else if (C < Pivot)
      std::swap(Vec[--J], Vec[K]);
    else
      K++;
  }

  multikeySort(Vec.slice(0, I), Pos);
  multikeySort(Vec.slice(J), Pos);

  // multikeySort(Vec.slice(I, J - I), Pos + 1), but with
  // tail call optimization.
  if (Pivot != -1) {
    Vec = Vec.slice(I, J - I);
    ++Pos;
    goto tailcall;
  }
}

void StringTableBuilder::finalize() {
  assert(K != DWARF);
  finalizeStringTable(/*Optimize=*/true);
}

void StringTableBuilder::finalizeInOrder() {
  finalizeStringTable(/*Optimize=*/false);
}

void StringTableBuilder::finalizeStringTable(bool Optimize) {
  Finalized = true;

  if (Optimize) {
    std::vector<StringPair *> Strings;
    Strings.reserve(StringIndexMap.size());
    for (StringPair &P : StringIndexMap)
      Strings.push_back(&P);

    multikeySort(Strings, 0);
    initSize();

    StringRef Previous;
    for (StringPair *P : Strings) {
      StringRef S = P->first.val();
      if (Previous.endswith(S)) {
        size_t Pos = Size - S.size() - (K != RAW);
        if (!(Pos & (Alignment - 1))) {
          P->second = Pos;
          continue;
        }
      }

      Size = alignTo(Size, Alignment);
      P->second = Size;

      Size += S.size();
      if (K != RAW)
        ++Size;
      Previous = S;
    }
  }

  if (K == MachO)
    Size = alignTo(Size, 4); // Pad to multiple of 4.
}

void StringTableBuilder::clear() {
  Finalized = false;
  StringIndexMap.clear();
}

size_t StringTableBuilder::getOffset(CachedHashStringRef S) const {
  assert(isFinalized());
  auto I = StringIndexMap.find(S);
  assert(I != StringIndexMap.end() && "String is not in table!");
  return I->second;
}

size_t StringTableBuilder::add(CachedHashStringRef S) {
  if (K == WinCOFF)
    assert(S.size() > COFF::NameSize && "Short string in COFF string table!");

  assert(!isFinalized());
  auto P = StringIndexMap.insert(std::make_pair(S, 0));
  if (P.second) {
    size_t Start = alignTo(Size, Alignment);
    P.first->second = Start;
    Size = Start + S.size() + (K != RAW);
  }
  return P.first->second;
}
