//===- HashTable.cpp - PDB Hash Table -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;
using namespace llvm::pdb;

Error llvm::pdb::readSparseBitVector(BinaryStreamReader &Stream,
                                     SparseBitVector<> &V) {
  uint32_t NumWords;
  if (auto EC = Stream.readInteger(NumWords))
    return joinErrors(
        std::move(EC),
        make_error<RawError>(raw_error_code::corrupt_file,
                             "Expected hash table number of words"));

  for (uint32_t I = 0; I != NumWords; ++I) {
    uint32_t Word;
    if (auto EC = Stream.readInteger(Word))
      return joinErrors(std::move(EC),
                        make_error<RawError>(raw_error_code::corrupt_file,
                                             "Expected hash table word"));
    for (unsigned Idx = 0; Idx < 32; ++Idx)
      if (Word & (1U << Idx))
        V.set((I * 32) + Idx);
  }
  return Error::success();
}

Error llvm::pdb::writeSparseBitVector(BinaryStreamWriter &Writer,
                                      SparseBitVector<> &Vec) {
  constexpr int BitsPerWord = 8 * sizeof(uint32_t);

  int ReqBits = Vec.find_last() + 1;
  uint32_t ReqWords = alignTo(ReqBits, BitsPerWord) / BitsPerWord;
  if (auto EC = Writer.writeInteger(ReqWords))
    return joinErrors(
        std::move(EC),
        make_error<RawError>(raw_error_code::corrupt_file,
                             "Could not write linear map number of words"));

  uint32_t Idx = 0;
  for (uint32_t I = 0; I != ReqWords; ++I) {
    uint32_t Word = 0;
    for (uint32_t WordIdx = 0; WordIdx < 32; ++WordIdx, ++Idx) {
      if (Vec.test(Idx))
        Word |= (1 << WordIdx);
    }
    if (auto EC = Writer.writeInteger(Word))
      return joinErrors(std::move(EC), make_error<RawError>(
                                           raw_error_code::corrupt_file,
                                           "Could not write linear map word"));
  }
  return Error::success();
}
