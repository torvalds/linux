//===- ObjCARCAliasAnalysis.cpp - ObjC ARC Optimization -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines a simple ARC-aware AliasAnalysis using special knowledge
/// of Objective C to enhance other optimization passes which rely on the Alias
/// Analysis infrastructure.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
/// TODO: Theoretically we could check for dependencies between objc_* calls
/// and FMRB_OnlyAccessesArgumentPointees calls or other well-behaved calls.
///
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ObjCARCAliasAnalysis.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassAnalysisSupport.h"
#include "llvm/PassSupport.h"

#define DEBUG_TYPE "objc-arc-aa"

using namespace llvm;
using namespace llvm::objcarc;

AliasResult ObjCARCAAResult::alias(const MemoryLocation &LocA,
                                   const MemoryLocation &LocB) {
  if (!EnableARCOpts)
    return AAResultBase::alias(LocA, LocB);

  // First, strip off no-ops, including ObjC-specific no-ops, and try making a
  // precise alias query.
  const Value *SA = GetRCIdentityRoot(LocA.Ptr);
  const Value *SB = GetRCIdentityRoot(LocB.Ptr);
  AliasResult Result =
      AAResultBase::alias(MemoryLocation(SA, LocA.Size, LocA.AATags),
                          MemoryLocation(SB, LocB.Size, LocB.AATags));
  if (Result != MayAlias)
    return Result;

  // If that failed, climb to the underlying object, including climbing through
  // ObjC-specific no-ops, and try making an imprecise alias query.
  const Value *UA = GetUnderlyingObjCPtr(SA, DL);
  const Value *UB = GetUnderlyingObjCPtr(SB, DL);
  if (UA != SA || UB != SB) {
    Result = AAResultBase::alias(MemoryLocation(UA), MemoryLocation(UB));
    // We can't use MustAlias or PartialAlias results here because
    // GetUnderlyingObjCPtr may return an offsetted pointer value.
    if (Result == NoAlias)
      return NoAlias;
  }

  // If that failed, fail. We don't need to chain here, since that's covered
  // by the earlier precise query.
  return MayAlias;
}

bool ObjCARCAAResult::pointsToConstantMemory(const MemoryLocation &Loc,
                                             bool OrLocal) {
  if (!EnableARCOpts)
    return AAResultBase::pointsToConstantMemory(Loc, OrLocal);

  // First, strip off no-ops, including ObjC-specific no-ops, and try making
  // a precise alias query.
  const Value *S = GetRCIdentityRoot(Loc.Ptr);
  if (AAResultBase::pointsToConstantMemory(
          MemoryLocation(S, Loc.Size, Loc.AATags), OrLocal))
    return true;

  // If that failed, climb to the underlying object, including climbing through
  // ObjC-specific no-ops, and try making an imprecise alias query.
  const Value *U = GetUnderlyingObjCPtr(S, DL);
  if (U != S)
    return AAResultBase::pointsToConstantMemory(MemoryLocation(U), OrLocal);

  // If that failed, fail. We don't need to chain here, since that's covered
  // by the earlier precise query.
  return false;
}

FunctionModRefBehavior ObjCARCAAResult::getModRefBehavior(const Function *F) {
  if (!EnableARCOpts)
    return AAResultBase::getModRefBehavior(F);

  switch (GetFunctionClass(F)) {
  case ARCInstKind::NoopCast:
    return FMRB_DoesNotAccessMemory;
  default:
    break;
  }

  return AAResultBase::getModRefBehavior(F);
}

ModRefInfo ObjCARCAAResult::getModRefInfo(const CallBase *Call,
                                          const MemoryLocation &Loc) {
  if (!EnableARCOpts)
    return AAResultBase::getModRefInfo(Call, Loc);

  switch (GetBasicARCInstKind(Call)) {
  case ARCInstKind::Retain:
  case ARCInstKind::RetainRV:
  case ARCInstKind::Autorelease:
  case ARCInstKind::AutoreleaseRV:
  case ARCInstKind::NoopCast:
  case ARCInstKind::AutoreleasepoolPush:
  case ARCInstKind::FusedRetainAutorelease:
  case ARCInstKind::FusedRetainAutoreleaseRV:
    // These functions don't access any memory visible to the compiler.
    // Note that this doesn't include objc_retainBlock, because it updates
    // pointers when it copies block data.
    return ModRefInfo::NoModRef;
  default:
    break;
  }

  return AAResultBase::getModRefInfo(Call, Loc);
}

ObjCARCAAResult ObjCARCAA::run(Function &F, FunctionAnalysisManager &AM) {
  return ObjCARCAAResult(F.getParent()->getDataLayout());
}

char ObjCARCAAWrapperPass::ID = 0;
INITIALIZE_PASS(ObjCARCAAWrapperPass, "objc-arc-aa",
                "ObjC-ARC-Based Alias Analysis", false, true)

ImmutablePass *llvm::createObjCARCAAWrapperPass() {
  return new ObjCARCAAWrapperPass();
}

ObjCARCAAWrapperPass::ObjCARCAAWrapperPass() : ImmutablePass(ID) {
  initializeObjCARCAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool ObjCARCAAWrapperPass::doInitialization(Module &M) {
  Result.reset(new ObjCARCAAResult(M.getDataLayout()));
  return false;
}

bool ObjCARCAAWrapperPass::doFinalization(Module &M) {
  Result.reset();
  return false;
}

void ObjCARCAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}
