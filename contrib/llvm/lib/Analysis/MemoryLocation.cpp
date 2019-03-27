//===- MemoryLocation.cpp - Memory location descriptions -------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
using namespace llvm;

void LocationSize::print(raw_ostream &OS) const {
  OS << "LocationSize::";
  if (*this == unknown())
    OS << "unknown";
  else if (*this == mapEmpty())
    OS << "mapEmpty";
  else if (*this == mapTombstone())
    OS << "mapTombstone";
  else if (isPrecise())
    OS << "precise(" << getValue() << ')';
  else
    OS << "upperBound(" << getValue() << ')';
}

MemoryLocation MemoryLocation::get(const LoadInst *LI) {
  AAMDNodes AATags;
  LI->getAAMetadata(AATags);
  const auto &DL = LI->getModule()->getDataLayout();

  return MemoryLocation(
      LI->getPointerOperand(),
      LocationSize::precise(DL.getTypeStoreSize(LI->getType())), AATags);
}

MemoryLocation MemoryLocation::get(const StoreInst *SI) {
  AAMDNodes AATags;
  SI->getAAMetadata(AATags);
  const auto &DL = SI->getModule()->getDataLayout();

  return MemoryLocation(SI->getPointerOperand(),
                        LocationSize::precise(DL.getTypeStoreSize(
                            SI->getValueOperand()->getType())),
                        AATags);
}

MemoryLocation MemoryLocation::get(const VAArgInst *VI) {
  AAMDNodes AATags;
  VI->getAAMetadata(AATags);

  return MemoryLocation(VI->getPointerOperand(), LocationSize::unknown(),
                        AATags);
}

MemoryLocation MemoryLocation::get(const AtomicCmpXchgInst *CXI) {
  AAMDNodes AATags;
  CXI->getAAMetadata(AATags);
  const auto &DL = CXI->getModule()->getDataLayout();

  return MemoryLocation(CXI->getPointerOperand(),
                        LocationSize::precise(DL.getTypeStoreSize(
                            CXI->getCompareOperand()->getType())),
                        AATags);
}

MemoryLocation MemoryLocation::get(const AtomicRMWInst *RMWI) {
  AAMDNodes AATags;
  RMWI->getAAMetadata(AATags);
  const auto &DL = RMWI->getModule()->getDataLayout();

  return MemoryLocation(RMWI->getPointerOperand(),
                        LocationSize::precise(DL.getTypeStoreSize(
                            RMWI->getValOperand()->getType())),
                        AATags);
}

MemoryLocation MemoryLocation::getForSource(const MemTransferInst *MTI) {
  return getForSource(cast<AnyMemTransferInst>(MTI));
}

MemoryLocation MemoryLocation::getForSource(const AtomicMemTransferInst *MTI) {
  return getForSource(cast<AnyMemTransferInst>(MTI));
}

MemoryLocation MemoryLocation::getForSource(const AnyMemTransferInst *MTI) {
  auto Size = LocationSize::unknown();
  if (ConstantInt *C = dyn_cast<ConstantInt>(MTI->getLength()))
    Size = LocationSize::precise(C->getValue().getZExtValue());

  // memcpy/memmove can have AA tags. For memcpy, they apply
  // to both the source and the destination.
  AAMDNodes AATags;
  MTI->getAAMetadata(AATags);

  return MemoryLocation(MTI->getRawSource(), Size, AATags);
}

MemoryLocation MemoryLocation::getForDest(const MemIntrinsic *MI) {
  return getForDest(cast<AnyMemIntrinsic>(MI));
}

MemoryLocation MemoryLocation::getForDest(const AtomicMemIntrinsic *MI) {
  return getForDest(cast<AnyMemIntrinsic>(MI));
}

MemoryLocation MemoryLocation::getForDest(const AnyMemIntrinsic *MI) {
  auto Size = LocationSize::unknown();
  if (ConstantInt *C = dyn_cast<ConstantInt>(MI->getLength()))
    Size = LocationSize::precise(C->getValue().getZExtValue());

  // memcpy/memmove can have AA tags. For memcpy, they apply
  // to both the source and the destination.
  AAMDNodes AATags;
  MI->getAAMetadata(AATags);

  return MemoryLocation(MI->getRawDest(), Size, AATags);
}

MemoryLocation MemoryLocation::getForArgument(const CallBase *Call,
                                              unsigned ArgIdx,
                                              const TargetLibraryInfo *TLI) {
  AAMDNodes AATags;
  Call->getAAMetadata(AATags);
  const Value *Arg = Call->getArgOperand(ArgIdx);

  // We may be able to produce an exact size for known intrinsics.
  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(Call)) {
    const DataLayout &DL = II->getModule()->getDataLayout();

    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::memset:
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
      assert((ArgIdx == 0 || ArgIdx == 1) &&
             "Invalid argument index for memory intrinsic");
      if (ConstantInt *LenCI = dyn_cast<ConstantInt>(II->getArgOperand(2)))
        return MemoryLocation(Arg, LocationSize::precise(LenCI->getZExtValue()),
                              AATags);
      break;

    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::invariant_start:
      assert(ArgIdx == 1 && "Invalid argument index");
      return MemoryLocation(
          Arg,
          LocationSize::precise(
              cast<ConstantInt>(II->getArgOperand(0))->getZExtValue()),
          AATags);

    case Intrinsic::invariant_end:
      // The first argument to an invariant.end is a "descriptor" type (e.g. a
      // pointer to a empty struct) which is never actually dereferenced.
      if (ArgIdx == 0)
        return MemoryLocation(Arg, LocationSize::precise(0), AATags);
      assert(ArgIdx == 2 && "Invalid argument index");
      return MemoryLocation(
          Arg,
          LocationSize::precise(
              cast<ConstantInt>(II->getArgOperand(1))->getZExtValue()),
          AATags);

    case Intrinsic::arm_neon_vld1:
      assert(ArgIdx == 0 && "Invalid argument index");
      // LLVM's vld1 and vst1 intrinsics currently only support a single
      // vector register.
      return MemoryLocation(
          Arg, LocationSize::precise(DL.getTypeStoreSize(II->getType())),
          AATags);

    case Intrinsic::arm_neon_vst1:
      assert(ArgIdx == 0 && "Invalid argument index");
      return MemoryLocation(Arg,
                            LocationSize::precise(DL.getTypeStoreSize(
                                II->getArgOperand(1)->getType())),
                            AATags);
    }
  }

  // We can bound the aliasing properties of memset_pattern16 just as we can
  // for memcpy/memset.  This is particularly important because the
  // LoopIdiomRecognizer likes to turn loops into calls to memset_pattern16
  // whenever possible.
  LibFunc F;
  if (TLI && Call->getCalledFunction() &&
      TLI->getLibFunc(*Call->getCalledFunction(), F) &&
      F == LibFunc_memset_pattern16 && TLI->has(F)) {
    assert((ArgIdx == 0 || ArgIdx == 1) &&
           "Invalid argument index for memset_pattern16");
    if (ArgIdx == 1)
      return MemoryLocation(Arg, LocationSize::precise(16), AATags);
    if (const ConstantInt *LenCI =
            dyn_cast<ConstantInt>(Call->getArgOperand(2)))
      return MemoryLocation(Arg, LocationSize::precise(LenCI->getZExtValue()),
                            AATags);
  }
  // FIXME: Handle memset_pattern4 and memset_pattern8 also.

  return MemoryLocation(Call->getArgOperand(ArgIdx), LocationSize::unknown(),
                        AATags);
}
