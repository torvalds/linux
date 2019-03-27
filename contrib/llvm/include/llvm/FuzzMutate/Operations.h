//===-- Operations.h - ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementations of common fuzzer operation descriptors for building an IR
// mutator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_OPERATIONS_H
#define LLVM_FUZZMUTATE_OPERATIONS_H

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

/// Getters for the default sets of operations, per general category.
/// @{
void describeFuzzerIntOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerFloatOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerControlFlowOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerPointerOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerAggregateOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerVectorOps(std::vector<fuzzerop::OpDescriptor> &Ops);
/// @}

namespace fuzzerop {

/// Descriptors for individual operations.
/// @{
OpDescriptor binOpDescriptor(unsigned Weight, Instruction::BinaryOps Op);
OpDescriptor cmpOpDescriptor(unsigned Weight, Instruction::OtherOps CmpOp,
                             CmpInst::Predicate Pred);
OpDescriptor splitBlockDescriptor(unsigned Weight);
OpDescriptor gepDescriptor(unsigned Weight);
OpDescriptor extractValueDescriptor(unsigned Weight);
OpDescriptor insertValueDescriptor(unsigned Weight);
OpDescriptor extractElementDescriptor(unsigned Weight);
OpDescriptor insertElementDescriptor(unsigned Weight);
OpDescriptor shuffleVectorDescriptor(unsigned Weight);
/// @}

} // end fuzzerop namespace

} // end llvm namespace

#endif // LLVM_FUZZMUTATE_OPERATIONS_H
