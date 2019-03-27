//===- FuzzerDictionary.h - Internal header for the Fuzzer ------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// fuzzer::Dictionary
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_DICTIONARY_H
#define LLVM_FUZZER_DICTIONARY_H

#include "FuzzerDefs.h"
#include "FuzzerIO.h"
#include "FuzzerUtil.h"
#include <algorithm>
#include <limits>

namespace fuzzer {
// A simple POD sized array of bytes.
template <size_t kMaxSizeT> class FixedWord {
public:
  static const size_t kMaxSize = kMaxSizeT;
  FixedWord() {}
  FixedWord(const uint8_t *B, uint8_t S) { Set(B, S); }

  void Set(const uint8_t *B, uint8_t S) {
    assert(S <= kMaxSize);
    memcpy(Data, B, S);
    Size = S;
  }

  bool operator==(const FixedWord<kMaxSize> &w) const {
    return Size == w.Size && 0 == memcmp(Data, w.Data, Size);
  }

  static size_t GetMaxSize() { return kMaxSize; }
  const uint8_t *data() const { return Data; }
  uint8_t size() const { return Size; }

private:
  uint8_t Size = 0;
  uint8_t Data[kMaxSize];
};

typedef FixedWord<64> Word;

class DictionaryEntry {
 public:
  DictionaryEntry() {}
  DictionaryEntry(Word W) : W(W) {}
  DictionaryEntry(Word W, size_t PositionHint) : W(W), PositionHint(PositionHint) {}
  const Word &GetW() const { return W; }

  bool HasPositionHint() const { return PositionHint != std::numeric_limits<size_t>::max(); }
  size_t GetPositionHint() const {
    assert(HasPositionHint());
    return PositionHint;
  }
  void IncUseCount() { UseCount++; }
  void IncSuccessCount() { SuccessCount++; }
  size_t GetUseCount() const { return UseCount; }
  size_t GetSuccessCount() const {return SuccessCount; }

  void Print(const char *PrintAfter = "\n") {
    PrintASCII(W.data(), W.size());
    if (HasPositionHint())
      Printf("@%zd", GetPositionHint());
    Printf("%s", PrintAfter);
  }

private:
  Word W;
  size_t PositionHint = std::numeric_limits<size_t>::max();
  size_t UseCount = 0;
  size_t SuccessCount = 0;
};

class Dictionary {
 public:
  static const size_t kMaxDictSize = 1 << 14;

  bool ContainsWord(const Word &W) const {
    return std::any_of(begin(), end(), [&](const DictionaryEntry &DE) {
      return DE.GetW() == W;
    });
  }
  const DictionaryEntry *begin() const { return &DE[0]; }
  const DictionaryEntry *end() const { return begin() + Size; }
  DictionaryEntry & operator[] (size_t Idx) {
    assert(Idx < Size);
    return DE[Idx];
  }
  void push_back(DictionaryEntry DE) {
    if (Size < kMaxDictSize)
      this->DE[Size++] = DE;
  }
  void clear() { Size = 0; }
  bool empty() const { return Size == 0; }
  size_t size() const { return Size; }

private:
  DictionaryEntry DE[kMaxDictSize];
  size_t Size = 0;
};

// Parses one dictionary entry.
// If successful, write the enty to Unit and returns true,
// otherwise returns false.
bool ParseOneDictionaryEntry(const std::string &Str, Unit *U);
// Parses the dictionary file, fills Units, returns true iff all lines
// were parsed successfully.
bool ParseDictionaryFile(const std::string &Text, Vector<Unit> *Units);

}  // namespace fuzzer

#endif  // LLVM_FUZZER_DICTIONARY_H
