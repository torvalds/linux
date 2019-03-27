//===-- IR/Statepoint.cpp -- gc.statepoint utilities ---  -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains some utility functions to help recognize gc.statepoint
// intrinsics.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Statepoint.h"

#include "llvm/IR/Function.h"

using namespace llvm;

static const Function *getCalledFunction(ImmutableCallSite CS) {
  if (!CS.getInstruction())
    return nullptr;
  return CS.getCalledFunction();
}

bool llvm::isStatepoint(ImmutableCallSite CS) {
  if (auto *F = getCalledFunction(CS))
    return F->getIntrinsicID() == Intrinsic::experimental_gc_statepoint;
  return false;
}

bool llvm::isStatepoint(const Value *V) {
  if (auto CS = ImmutableCallSite(V))
    return isStatepoint(CS);
  return false;
}

bool llvm::isStatepoint(const Value &V) {
  return isStatepoint(&V);
}

bool llvm::isGCRelocate(ImmutableCallSite CS) {
  return CS.getInstruction() && isa<GCRelocateInst>(CS.getInstruction());
}

bool llvm::isGCRelocate(const Value *V) {
  if (auto CS = ImmutableCallSite(V))
    return isGCRelocate(CS);
  return false;
}

bool llvm::isGCResult(ImmutableCallSite CS) {
  return CS.getInstruction() && isa<GCResultInst>(CS.getInstruction());
}

bool llvm::isGCResult(const Value *V) {
  if (auto CS = ImmutableCallSite(V))
    return isGCResult(CS);
  return false;
}

bool llvm::isStatepointDirectiveAttr(Attribute Attr) {
  return Attr.hasAttribute("statepoint-id") ||
         Attr.hasAttribute("statepoint-num-patch-bytes");
}

StatepointDirectives
llvm::parseStatepointDirectivesFromAttrs(AttributeList AS) {
  StatepointDirectives Result;

  Attribute AttrID =
      AS.getAttribute(AttributeList::FunctionIndex, "statepoint-id");
  uint64_t StatepointID;
  if (AttrID.isStringAttribute())
    if (!AttrID.getValueAsString().getAsInteger(10, StatepointID))
      Result.StatepointID = StatepointID;

  uint32_t NumPatchBytes;
  Attribute AttrNumPatchBytes = AS.getAttribute(AttributeList::FunctionIndex,
                                                "statepoint-num-patch-bytes");
  if (AttrNumPatchBytes.isStringAttribute())
    if (!AttrNumPatchBytes.getValueAsString().getAsInteger(10, NumPatchBytes))
      Result.NumPatchBytes = NumPatchBytes;

  return Result;
}
