//===- FuzzerRandom.h - Internal header for the Fuzzer ----------*- C++ -* ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// fuzzer::Random
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_RANDOM_H
#define LLVM_FUZZER_RANDOM_H

#include <random>

namespace fuzzer {
class Random : public std::mt19937 {
 public:
  Random(unsigned int seed) : std::mt19937(seed) {}
  result_type operator()() { return this->std::mt19937::operator()(); }
  size_t Rand() { return this->operator()(); }
  size_t RandBool() { return Rand() % 2; }
  size_t operator()(size_t n) { return n ? Rand() % n : 0; }
  intptr_t operator()(intptr_t From, intptr_t To) {
    assert(From < To);
    intptr_t RangeSize = To - From + 1;
    return operator()(RangeSize) + From;
  }
};

}  // namespace fuzzer

#endif  // LLVM_FUZZER_RANDOM_H
