//===--------------------- SourceMgr.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements class SourceMgr. Class SourceMgr abstracts the input
/// code sequence (a sequence of MCInst), and assings unique identifiers to
/// every instruction in the sequence.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_SOURCEMGR_H
#define LLVM_MCA_SOURCEMGR_H

#include "llvm/ADT/ArrayRef.h"

namespace llvm {
namespace mca {

class Instruction;

typedef std::pair<unsigned, const Instruction &> SourceRef;

class SourceMgr {
  using UniqueInst = std::unique_ptr<Instruction>;
  ArrayRef<UniqueInst> Sequence;
  unsigned Current;
  const unsigned Iterations;
  static const unsigned DefaultIterations = 100;

public:
  SourceMgr(ArrayRef<UniqueInst> S, unsigned Iter)
      : Sequence(S), Current(0), Iterations(Iter ? Iter : DefaultIterations) {}

  unsigned getNumIterations() const { return Iterations; }
  unsigned size() const { return Sequence.size(); }
  bool hasNext() const { return Current < (Iterations * Sequence.size()); }
  void updateNext() { ++Current; }

  SourceRef peekNext() const {
    assert(hasNext() && "Already at end of sequence!");
    return SourceRef(Current, *Sequence[Current % Sequence.size()]);
  }

  using const_iterator = ArrayRef<UniqueInst>::const_iterator;
  const_iterator begin() const { return Sequence.begin(); }
  const_iterator end() const { return Sequence.end(); }
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_SOURCEMGR_H
