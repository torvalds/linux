//===- CodeGen/Analysis.h - CodeGen LLVM IR Analysis Utilities --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares several CodeGen-specific LLVM IR analysis utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ANALYSIS_H
#define LLVM_CODEGEN_ANALYSIS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class GlobalValue;
class MachineBasicBlock;
class MachineFunction;
class TargetLoweringBase;
class TargetLowering;
class TargetMachine;
class SDNode;
class SDValue;
class SelectionDAG;
struct EVT;

/// Compute the linearized index of a member in a nested
/// aggregate/struct/array.
///
/// Given an LLVM IR aggregate type and a sequence of insertvalue or
/// extractvalue indices that identify a member, return the linearized index of
/// the start of the member, i.e the number of element in memory before the
/// sought one. This is disconnected from the number of bytes.
///
/// \param Ty is the type indexed by \p Indices.
/// \param Indices is an optional pointer in the indices list to the current
/// index.
/// \param IndicesEnd is the end of the indices list.
/// \param CurIndex is the current index in the recursion.
///
/// \returns \p CurIndex plus the linear index in \p Ty  the indices list.
unsigned ComputeLinearIndex(Type *Ty,
                            const unsigned *Indices,
                            const unsigned *IndicesEnd,
                            unsigned CurIndex = 0);

inline unsigned ComputeLinearIndex(Type *Ty,
                                   ArrayRef<unsigned> Indices,
                                   unsigned CurIndex = 0) {
  return ComputeLinearIndex(Ty, Indices.begin(), Indices.end(), CurIndex);
}

/// ComputeValueVTs - Given an LLVM IR type, compute a sequence of
/// EVTs that represent all the individual underlying
/// non-aggregate types that comprise it.
///
/// If Offsets is non-null, it points to a vector to be filled in
/// with the in-memory offsets of each of the individual values.
///
void ComputeValueVTs(const TargetLowering &TLI, const DataLayout &DL, Type *Ty,
                     SmallVectorImpl<EVT> &ValueVTs,
                     SmallVectorImpl<uint64_t> *Offsets = nullptr,
                     uint64_t StartingOffset = 0);

/// ExtractTypeInfo - Returns the type info, possibly bitcast, encoded in V.
GlobalValue *ExtractTypeInfo(Value *V);

/// hasInlineAsmMemConstraint - Return true if the inline asm instruction being
/// processed uses a memory 'm' constraint.
bool hasInlineAsmMemConstraint(InlineAsm::ConstraintInfoVector &CInfos,
                               const TargetLowering &TLI);

/// getFCmpCondCode - Return the ISD condition code corresponding to
/// the given LLVM IR floating-point condition code.  This includes
/// consideration of global floating-point math flags.
///
ISD::CondCode getFCmpCondCode(FCmpInst::Predicate Pred);

/// getFCmpCodeWithoutNaN - Given an ISD condition code comparing floats,
/// return the equivalent code if we're allowed to assume that NaNs won't occur.
ISD::CondCode getFCmpCodeWithoutNaN(ISD::CondCode CC);

/// getICmpCondCode - Return the ISD condition code corresponding to
/// the given LLVM IR integer condition code.
///
ISD::CondCode getICmpCondCode(ICmpInst::Predicate Pred);

/// Test if the given instruction is in a position to be optimized
/// with a tail-call. This roughly means that it's in a block with
/// a return and there's nothing that needs to be scheduled
/// between it and the return.
///
/// This function only tests target-independent requirements.
bool isInTailCallPosition(ImmutableCallSite CS, const TargetMachine &TM);

/// Test if given that the input instruction is in the tail call position, if
/// there is an attribute mismatch between the caller and the callee that will
/// inhibit tail call optimizations.
/// \p AllowDifferingSizes is an output parameter which, if forming a tail call
/// is permitted, determines whether it's permitted only if the size of the
/// caller's and callee's return types match exactly.
bool attributesPermitTailCall(const Function *F, const Instruction *I,
                              const ReturnInst *Ret,
                              const TargetLoweringBase &TLI,
                              bool *AllowDifferingSizes = nullptr);

/// Test if given that the input instruction is in the tail call position if the
/// return type or any attributes of the function will inhibit tail call
/// optimization.
bool returnTypeIsEligibleForTailCall(const Function *F, const Instruction *I,
                                     const ReturnInst *Ret,
                                     const TargetLoweringBase &TLI);

DenseMap<const MachineBasicBlock *, int>
getEHScopeMembership(const MachineFunction &MF);

} // End llvm namespace

#endif
