//===-- Mutator.h - Utils for randomly mutation IR --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Provides the Mutator class, which is used to mutate IR for fuzzing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_RANDOMIRBUILDER_H
#define LLVM_FUZZMUTATE_RANDOMIRBUILDER_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/FuzzMutate/IRMutator.h"
#include "llvm/FuzzMutate/Random.h"
#include <random>

namespace llvm {

using RandomEngine = std::mt19937;

struct RandomIRBuilder {
  RandomEngine Rand;
  SmallVector<Type *, 16> KnownTypes;

  RandomIRBuilder(int Seed, ArrayRef<Type *> AllowedTypes)
      : Rand(Seed), KnownTypes(AllowedTypes.begin(), AllowedTypes.end()) {}

  // TODO: Try to make this a bit less of a random mishmash of functions.

  /// Find a "source" for some operation, which will be used in one of the
  /// operation's operands. This either selects an instruction in \c Insts or
  /// returns some new arbitrary Value.
  Value *findOrCreateSource(BasicBlock &BB, ArrayRef<Instruction *> Insts);
  /// Find a "source" for some operation, which will be used in one of the
  /// operation's operands. This either selects an instruction in \c Insts that
  /// matches \c Pred, or returns some new Value that matches \c Pred. The
  /// values in \c Srcs should be source operands that have already been
  /// selected.
  Value *findOrCreateSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                            ArrayRef<Value *> Srcs, fuzzerop::SourcePred Pred);
  /// Create some Value suitable as a source for some operation.
  Value *newSource(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                   ArrayRef<Value *> Srcs, fuzzerop::SourcePred Pred);
  /// Find a viable user for \c V in \c Insts, which should all be contained in
  /// \c BB. This may also create some new instruction in \c BB and use that.
  void connectToSink(BasicBlock &BB, ArrayRef<Instruction *> Insts, Value *V);
  /// Create a user for \c V in \c BB.
  void newSink(BasicBlock &BB, ArrayRef<Instruction *> Insts, Value *V);
  Value *findPointer(BasicBlock &BB, ArrayRef<Instruction *> Insts,
                     ArrayRef<Value *> Srcs, fuzzerop::SourcePred Pred);
  Type *chooseType(LLVMContext &Context, ArrayRef<Value *> Srcs,
                   fuzzerop::SourcePred Pred);
};

} // end llvm namespace

#endif // LLVM_FUZZMUTATE_RANDOMIRBUILDER_H
