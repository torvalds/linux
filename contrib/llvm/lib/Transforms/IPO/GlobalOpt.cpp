//===- GlobalOpt.cpp - Optimize Global Variables --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass transforms simple global variables that never have their address
// taken.  If obviously true, it marks read/write globals as constant, deletes
// variables only stored to, etc.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/GlobalOpt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/CtorUtils.h"
#include "llvm/Transforms/Utils/Evaluator.h"
#include "llvm/Transforms/Utils/GlobalStatus.h"
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "globalopt"

STATISTIC(NumMarked    , "Number of globals marked constant");
STATISTIC(NumUnnamed   , "Number of globals marked unnamed_addr");
STATISTIC(NumSRA       , "Number of aggregate globals broken into scalars");
STATISTIC(NumHeapSRA   , "Number of heap objects SRA'd");
STATISTIC(NumSubstitute,"Number of globals with initializers stored into them");
STATISTIC(NumDeleted   , "Number of globals deleted");
STATISTIC(NumGlobUses  , "Number of global uses devirtualized");
STATISTIC(NumLocalized , "Number of globals localized");
STATISTIC(NumShrunkToBool  , "Number of global vars shrunk to booleans");
STATISTIC(NumFastCallFns   , "Number of functions converted to fastcc");
STATISTIC(NumCtorsEvaluated, "Number of static ctors evaluated");
STATISTIC(NumNestRemoved   , "Number of nest attributes removed");
STATISTIC(NumAliasesResolved, "Number of global aliases resolved");
STATISTIC(NumAliasesRemoved, "Number of global aliases eliminated");
STATISTIC(NumCXXDtorsRemoved, "Number of global C++ destructors removed");
STATISTIC(NumInternalFunc, "Number of internal functions");
STATISTIC(NumColdCC, "Number of functions marked coldcc");

static cl::opt<bool>
    EnableColdCCStressTest("enable-coldcc-stress-test",
                           cl::desc("Enable stress test of coldcc by adding "
                                    "calling conv to all internal functions."),
                           cl::init(false), cl::Hidden);

static cl::opt<int> ColdCCRelFreq(
    "coldcc-rel-freq", cl::Hidden, cl::init(2), cl::ZeroOrMore,
    cl::desc(
        "Maximum block frequency, expressed as a percentage of caller's "
        "entry frequency, for a call site to be considered cold for enabling"
        "coldcc"));

/// Is this global variable possibly used by a leak checker as a root?  If so,
/// we might not really want to eliminate the stores to it.
static bool isLeakCheckerRoot(GlobalVariable *GV) {
  // A global variable is a root if it is a pointer, or could plausibly contain
  // a pointer.  There are two challenges; one is that we could have a struct
  // the has an inner member which is a pointer.  We recurse through the type to
  // detect these (up to a point).  The other is that we may actually be a union
  // of a pointer and another type, and so our LLVM type is an integer which
  // gets converted into a pointer, or our type is an [i8 x #] with a pointer
  // potentially contained here.

  if (GV->hasPrivateLinkage())
    return false;

  SmallVector<Type *, 4> Types;
  Types.push_back(GV->getValueType());

  unsigned Limit = 20;
  do {
    Type *Ty = Types.pop_back_val();
    switch (Ty->getTypeID()) {
      default: break;
      case Type::PointerTyID: return true;
      case Type::ArrayTyID:
      case Type::VectorTyID: {
        SequentialType *STy = cast<SequentialType>(Ty);
        Types.push_back(STy->getElementType());
        break;
      }
      case Type::StructTyID: {
        StructType *STy = cast<StructType>(Ty);
        if (STy->isOpaque()) return true;
        for (StructType::element_iterator I = STy->element_begin(),
                 E = STy->element_end(); I != E; ++I) {
          Type *InnerTy = *I;
          if (isa<PointerType>(InnerTy)) return true;
          if (isa<CompositeType>(InnerTy))
            Types.push_back(InnerTy);
        }
        break;
      }
    }
    if (--Limit == 0) return true;
  } while (!Types.empty());
  return false;
}

/// Given a value that is stored to a global but never read, determine whether
/// it's safe to remove the store and the chain of computation that feeds the
/// store.
static bool IsSafeComputationToRemove(Value *V, const TargetLibraryInfo *TLI) {
  do {
    if (isa<Constant>(V))
      return true;
    if (!V->hasOneUse())
      return false;
    if (isa<LoadInst>(V) || isa<InvokeInst>(V) || isa<Argument>(V) ||
        isa<GlobalValue>(V))
      return false;
    if (isAllocationFn(V, TLI))
      return true;

    Instruction *I = cast<Instruction>(V);
    if (I->mayHaveSideEffects())
      return false;
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I)) {
      if (!GEP->hasAllConstantIndices())
        return false;
    } else if (I->getNumOperands() != 1) {
      return false;
    }

    V = I->getOperand(0);
  } while (true);
}

/// This GV is a pointer root.  Loop over all users of the global and clean up
/// any that obviously don't assign the global a value that isn't dynamically
/// allocated.
static bool CleanupPointerRootUsers(GlobalVariable *GV,
                                    const TargetLibraryInfo *TLI) {
  // A brief explanation of leak checkers.  The goal is to find bugs where
  // pointers are forgotten, causing an accumulating growth in memory
  // usage over time.  The common strategy for leak checkers is to whitelist the
  // memory pointed to by globals at exit.  This is popular because it also
  // solves another problem where the main thread of a C++ program may shut down
  // before other threads that are still expecting to use those globals.  To
  // handle that case, we expect the program may create a singleton and never
  // destroy it.

  bool Changed = false;

  // If Dead[n].first is the only use of a malloc result, we can delete its
  // chain of computation and the store to the global in Dead[n].second.
  SmallVector<std::pair<Instruction *, Instruction *>, 32> Dead;

  // Constants can't be pointers to dynamically allocated memory.
  for (Value::user_iterator UI = GV->user_begin(), E = GV->user_end();
       UI != E;) {
    User *U = *UI++;
    if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
      Value *V = SI->getValueOperand();
      if (isa<Constant>(V)) {
        Changed = true;
        SI->eraseFromParent();
      } else if (Instruction *I = dyn_cast<Instruction>(V)) {
        if (I->hasOneUse())
          Dead.push_back(std::make_pair(I, SI));
      }
    } else if (MemSetInst *MSI = dyn_cast<MemSetInst>(U)) {
      if (isa<Constant>(MSI->getValue())) {
        Changed = true;
        MSI->eraseFromParent();
      } else if (Instruction *I = dyn_cast<Instruction>(MSI->getValue())) {
        if (I->hasOneUse())
          Dead.push_back(std::make_pair(I, MSI));
      }
    } else if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(U)) {
      GlobalVariable *MemSrc = dyn_cast<GlobalVariable>(MTI->getSource());
      if (MemSrc && MemSrc->isConstant()) {
        Changed = true;
        MTI->eraseFromParent();
      } else if (Instruction *I = dyn_cast<Instruction>(MemSrc)) {
        if (I->hasOneUse())
          Dead.push_back(std::make_pair(I, MTI));
      }
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U)) {
      if (CE->use_empty()) {
        CE->destroyConstant();
        Changed = true;
      }
    } else if (Constant *C = dyn_cast<Constant>(U)) {
      if (isSafeToDestroyConstant(C)) {
        C->destroyConstant();
        // This could have invalidated UI, start over from scratch.
        Dead.clear();
        CleanupPointerRootUsers(GV, TLI);
        return true;
      }
    }
  }

  for (int i = 0, e = Dead.size(); i != e; ++i) {
    if (IsSafeComputationToRemove(Dead[i].first, TLI)) {
      Dead[i].second->eraseFromParent();
      Instruction *I = Dead[i].first;
      do {
        if (isAllocationFn(I, TLI))
          break;
        Instruction *J = dyn_cast<Instruction>(I->getOperand(0));
        if (!J)
          break;
        I->eraseFromParent();
        I = J;
      } while (true);
      I->eraseFromParent();
    }
  }

  return Changed;
}

/// We just marked GV constant.  Loop over all users of the global, cleaning up
/// the obvious ones.  This is largely just a quick scan over the use list to
/// clean up the easy and obvious cruft.  This returns true if it made a change.
static bool CleanupConstantGlobalUsers(Value *V, Constant *Init,
                                       const DataLayout &DL,
                                       TargetLibraryInfo *TLI) {
  bool Changed = false;
  // Note that we need to use a weak value handle for the worklist items. When
  // we delete a constant array, we may also be holding pointer to one of its
  // elements (or an element of one of its elements if we're dealing with an
  // array of arrays) in the worklist.
  SmallVector<WeakTrackingVH, 8> WorkList(V->user_begin(), V->user_end());
  while (!WorkList.empty()) {
    Value *UV = WorkList.pop_back_val();
    if (!UV)
      continue;

    User *U = cast<User>(UV);

    if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
      if (Init) {
        // Replace the load with the initializer.
        LI->replaceAllUsesWith(Init);
        LI->eraseFromParent();
        Changed = true;
      }
    } else if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
      // Store must be unreachable or storing Init into the global.
      SI->eraseFromParent();
      Changed = true;
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U)) {
      if (CE->getOpcode() == Instruction::GetElementPtr) {
        Constant *SubInit = nullptr;
        if (Init)
          SubInit = ConstantFoldLoadThroughGEPConstantExpr(Init, CE);
        Changed |= CleanupConstantGlobalUsers(CE, SubInit, DL, TLI);
      } else if ((CE->getOpcode() == Instruction::BitCast &&
                  CE->getType()->isPointerTy()) ||
                 CE->getOpcode() == Instruction::AddrSpaceCast) {
        // Pointer cast, delete any stores and memsets to the global.
        Changed |= CleanupConstantGlobalUsers(CE, nullptr, DL, TLI);
      }

      if (CE->use_empty()) {
        CE->destroyConstant();
        Changed = true;
      }
    } else if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // Do not transform "gepinst (gep constexpr (GV))" here, because forming
      // "gepconstexpr (gep constexpr (GV))" will cause the two gep's to fold
      // and will invalidate our notion of what Init is.
      Constant *SubInit = nullptr;
      if (!isa<ConstantExpr>(GEP->getOperand(0))) {
        ConstantExpr *CE = dyn_cast_or_null<ConstantExpr>(
            ConstantFoldInstruction(GEP, DL, TLI));
        if (Init && CE && CE->getOpcode() == Instruction::GetElementPtr)
          SubInit = ConstantFoldLoadThroughGEPConstantExpr(Init, CE);

        // If the initializer is an all-null value and we have an inbounds GEP,
        // we already know what the result of any load from that GEP is.
        // TODO: Handle splats.
        if (Init && isa<ConstantAggregateZero>(Init) && GEP->isInBounds())
          SubInit = Constant::getNullValue(GEP->getResultElementType());
      }
      Changed |= CleanupConstantGlobalUsers(GEP, SubInit, DL, TLI);

      if (GEP->use_empty()) {
        GEP->eraseFromParent();
        Changed = true;
      }
    } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(U)) { // memset/cpy/mv
      if (MI->getRawDest() == V) {
        MI->eraseFromParent();
        Changed = true;
      }

    } else if (Constant *C = dyn_cast<Constant>(U)) {
      // If we have a chain of dead constantexprs or other things dangling from
      // us, and if they are all dead, nuke them without remorse.
      if (isSafeToDestroyConstant(C)) {
        C->destroyConstant();
        CleanupConstantGlobalUsers(V, Init, DL, TLI);
        return true;
      }
    }
  }
  return Changed;
}

static bool isSafeSROAElementUse(Value *V);

/// Return true if the specified GEP is a safe user of a derived
/// expression from a global that we want to SROA.
static bool isSafeSROAGEP(User *U) {
  // Check to see if this ConstantExpr GEP is SRA'able.  In particular, we
  // don't like < 3 operand CE's, and we don't like non-constant integer
  // indices.  This enforces that all uses are 'gep GV, 0, C, ...' for some
  // value of C.
  if (U->getNumOperands() < 3 || !isa<Constant>(U->getOperand(1)) ||
      !cast<Constant>(U->getOperand(1))->isNullValue())
    return false;

  gep_type_iterator GEPI = gep_type_begin(U), E = gep_type_end(U);
  ++GEPI; // Skip over the pointer index.

  // For all other level we require that the indices are constant and inrange.
  // In particular, consider: A[0][i].  We cannot know that the user isn't doing
  // invalid things like allowing i to index an out-of-range subscript that
  // accesses A[1]. This can also happen between different members of a struct
  // in llvm IR.
  for (; GEPI != E; ++GEPI) {
    if (GEPI.isStruct())
      continue;

    ConstantInt *IdxVal = dyn_cast<ConstantInt>(GEPI.getOperand());
    if (!IdxVal || (GEPI.isBoundedSequential() &&
                    IdxVal->getZExtValue() >= GEPI.getSequentialNumElements()))
      return false;
  }

  return llvm::all_of(U->users(),
                      [](User *UU) { return isSafeSROAElementUse(UU); });
}

/// Return true if the specified instruction is a safe user of a derived
/// expression from a global that we want to SROA.
static bool isSafeSROAElementUse(Value *V) {
  // We might have a dead and dangling constant hanging off of here.
  if (Constant *C = dyn_cast<Constant>(V))
    return isSafeToDestroyConstant(C);

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return false;

  // Loads are ok.
  if (isa<LoadInst>(I)) return true;

  // Stores *to* the pointer are ok.
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->getOperand(0) != V;

  // Otherwise, it must be a GEP. Check it and its users are safe to SRA.
  return isa<GetElementPtrInst>(I) && isSafeSROAGEP(I);
}

/// Look at all uses of the global and decide whether it is safe for us to
/// perform this transformation.
static bool GlobalUsersSafeToSRA(GlobalValue *GV) {
  for (User *U : GV->users()) {
    // The user of the global must be a GEP Inst or a ConstantExpr GEP.
    if (!isa<GetElementPtrInst>(U) &&
        (!isa<ConstantExpr>(U) ||
        cast<ConstantExpr>(U)->getOpcode() != Instruction::GetElementPtr))
      return false;

    // Check the gep and it's users are safe to SRA
    if (!isSafeSROAGEP(U))
      return false;
  }

  return true;
}

/// Copy over the debug info for a variable to its SRA replacements.
static void transferSRADebugInfo(GlobalVariable *GV, GlobalVariable *NGV,
                                 uint64_t FragmentOffsetInBits,
                                 uint64_t FragmentSizeInBits,
                                 unsigned NumElements) {
  SmallVector<DIGlobalVariableExpression *, 1> GVs;
  GV->getDebugInfo(GVs);
  for (auto *GVE : GVs) {
    DIVariable *Var = GVE->getVariable();
    DIExpression *Expr = GVE->getExpression();
    if (NumElements > 1) {
      if (auto E = DIExpression::createFragmentExpression(
              Expr, FragmentOffsetInBits, FragmentSizeInBits))
        Expr = *E;
      else
        return;
    }
    auto *NGVE = DIGlobalVariableExpression::get(GVE->getContext(), Var, Expr);
    NGV->addDebugInfo(NGVE);
  }
}

/// Perform scalar replacement of aggregates on the specified global variable.
/// This opens the door for other optimizations by exposing the behavior of the
/// program in a more fine-grained way.  We have determined that this
/// transformation is safe already.  We return the first global variable we
/// insert so that the caller can reprocess it.
static GlobalVariable *SRAGlobal(GlobalVariable *GV, const DataLayout &DL) {
  // Make sure this global only has simple uses that we can SRA.
  if (!GlobalUsersSafeToSRA(GV))
    return nullptr;

  assert(GV->hasLocalLinkage());
  Constant *Init = GV->getInitializer();
  Type *Ty = Init->getType();

  std::vector<GlobalVariable *> NewGlobals;
  Module::GlobalListType &Globals = GV->getParent()->getGlobalList();

  // Get the alignment of the global, either explicit or target-specific.
  unsigned StartAlignment = GV->getAlignment();
  if (StartAlignment == 0)
    StartAlignment = DL.getABITypeAlignment(GV->getType());

  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    unsigned NumElements = STy->getNumElements();
    NewGlobals.reserve(NumElements);
    const StructLayout &Layout = *DL.getStructLayout(STy);
    for (unsigned i = 0, e = NumElements; i != e; ++i) {
      Constant *In = Init->getAggregateElement(i);
      assert(In && "Couldn't get element of initializer?");
      GlobalVariable *NGV = new GlobalVariable(STy->getElementType(i), false,
                                               GlobalVariable::InternalLinkage,
                                               In, GV->getName()+"."+Twine(i),
                                               GV->getThreadLocalMode(),
                                              GV->getType()->getAddressSpace());
      NGV->setExternallyInitialized(GV->isExternallyInitialized());
      NGV->copyAttributesFrom(GV);
      Globals.push_back(NGV);
      NewGlobals.push_back(NGV);

      // Calculate the known alignment of the field.  If the original aggregate
      // had 256 byte alignment for example, something might depend on that:
      // propagate info to each field.
      uint64_t FieldOffset = Layout.getElementOffset(i);
      unsigned NewAlign = (unsigned)MinAlign(StartAlignment, FieldOffset);
      if (NewAlign > DL.getABITypeAlignment(STy->getElementType(i)))
        NGV->setAlignment(NewAlign);

      // Copy over the debug info for the variable.
      uint64_t Size = DL.getTypeAllocSizeInBits(NGV->getValueType());
      uint64_t FragmentOffsetInBits = Layout.getElementOffsetInBits(i);
      transferSRADebugInfo(GV, NGV, FragmentOffsetInBits, Size, NumElements);
    }
  } else if (SequentialType *STy = dyn_cast<SequentialType>(Ty)) {
    unsigned NumElements = STy->getNumElements();
    if (NumElements > 16 && GV->hasNUsesOrMore(16))
      return nullptr; // It's not worth it.
    NewGlobals.reserve(NumElements);
    auto ElTy = STy->getElementType();
    uint64_t EltSize = DL.getTypeAllocSize(ElTy);
    unsigned EltAlign = DL.getABITypeAlignment(ElTy);
    uint64_t FragmentSizeInBits = DL.getTypeAllocSizeInBits(ElTy);
    for (unsigned i = 0, e = NumElements; i != e; ++i) {
      Constant *In = Init->getAggregateElement(i);
      assert(In && "Couldn't get element of initializer?");

      GlobalVariable *NGV = new GlobalVariable(STy->getElementType(), false,
                                               GlobalVariable::InternalLinkage,
                                               In, GV->getName()+"."+Twine(i),
                                               GV->getThreadLocalMode(),
                                              GV->getType()->getAddressSpace());
      NGV->setExternallyInitialized(GV->isExternallyInitialized());
      NGV->copyAttributesFrom(GV);
      Globals.push_back(NGV);
      NewGlobals.push_back(NGV);

      // Calculate the known alignment of the field.  If the original aggregate
      // had 256 byte alignment for example, something might depend on that:
      // propagate info to each field.
      unsigned NewAlign = (unsigned)MinAlign(StartAlignment, EltSize*i);
      if (NewAlign > EltAlign)
        NGV->setAlignment(NewAlign);
      transferSRADebugInfo(GV, NGV, FragmentSizeInBits * i, FragmentSizeInBits,
                           NumElements);
    }
  }

  if (NewGlobals.empty())
    return nullptr;

  LLVM_DEBUG(dbgs() << "PERFORMING GLOBAL SRA ON: " << *GV << "\n");

  Constant *NullInt =Constant::getNullValue(Type::getInt32Ty(GV->getContext()));

  // Loop over all of the uses of the global, replacing the constantexpr geps,
  // with smaller constantexpr geps or direct references.
  while (!GV->use_empty()) {
    User *GEP = GV->user_back();
    assert(((isa<ConstantExpr>(GEP) &&
             cast<ConstantExpr>(GEP)->getOpcode()==Instruction::GetElementPtr)||
            isa<GetElementPtrInst>(GEP)) && "NonGEP CE's are not SRAable!");

    // Ignore the 1th operand, which has to be zero or else the program is quite
    // broken (undefined).  Get the 2nd operand, which is the structure or array
    // index.
    unsigned Val = cast<ConstantInt>(GEP->getOperand(2))->getZExtValue();
    if (Val >= NewGlobals.size()) Val = 0; // Out of bound array access.

    Value *NewPtr = NewGlobals[Val];
    Type *NewTy = NewGlobals[Val]->getValueType();

    // Form a shorter GEP if needed.
    if (GEP->getNumOperands() > 3) {
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(GEP)) {
        SmallVector<Constant*, 8> Idxs;
        Idxs.push_back(NullInt);
        for (unsigned i = 3, e = CE->getNumOperands(); i != e; ++i)
          Idxs.push_back(CE->getOperand(i));
        NewPtr =
            ConstantExpr::getGetElementPtr(NewTy, cast<Constant>(NewPtr), Idxs);
      } else {
        GetElementPtrInst *GEPI = cast<GetElementPtrInst>(GEP);
        SmallVector<Value*, 8> Idxs;
        Idxs.push_back(NullInt);
        for (unsigned i = 3, e = GEPI->getNumOperands(); i != e; ++i)
          Idxs.push_back(GEPI->getOperand(i));
        NewPtr = GetElementPtrInst::Create(
            NewTy, NewPtr, Idxs, GEPI->getName() + "." + Twine(Val), GEPI);
      }
    }
    GEP->replaceAllUsesWith(NewPtr);

    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(GEP))
      GEPI->eraseFromParent();
    else
      cast<ConstantExpr>(GEP)->destroyConstant();
  }

  // Delete the old global, now that it is dead.
  Globals.erase(GV);
  ++NumSRA;

  // Loop over the new globals array deleting any globals that are obviously
  // dead.  This can arise due to scalarization of a structure or an array that
  // has elements that are dead.
  unsigned FirstGlobal = 0;
  for (unsigned i = 0, e = NewGlobals.size(); i != e; ++i)
    if (NewGlobals[i]->use_empty()) {
      Globals.erase(NewGlobals[i]);
      if (FirstGlobal == i) ++FirstGlobal;
    }

  return FirstGlobal != NewGlobals.size() ? NewGlobals[FirstGlobal] : nullptr;
}

/// Return true if all users of the specified value will trap if the value is
/// dynamically null.  PHIs keeps track of any phi nodes we've seen to avoid
/// reprocessing them.
static bool AllUsesOfValueWillTrapIfNull(const Value *V,
                                        SmallPtrSetImpl<const PHINode*> &PHIs) {
  for (const User *U : V->users()) {
    if (const Instruction *I = dyn_cast<Instruction>(U)) {
      // If null pointer is considered valid, then all uses are non-trapping.
      // Non address-space 0 globals have already been pruned by the caller.
      if (NullPointerIsDefined(I->getFunction()))
        return false;
    }
    if (isa<LoadInst>(U)) {
      // Will trap.
    } else if (const StoreInst *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getOperand(0) == V) {
        //cerr << "NONTRAPPING USE: " << *U;
        return false;  // Storing the value.
      }
    } else if (const CallInst *CI = dyn_cast<CallInst>(U)) {
      if (CI->getCalledValue() != V) {
        //cerr << "NONTRAPPING USE: " << *U;
        return false;  // Not calling the ptr
      }
    } else if (const InvokeInst *II = dyn_cast<InvokeInst>(U)) {
      if (II->getCalledValue() != V) {
        //cerr << "NONTRAPPING USE: " << *U;
        return false;  // Not calling the ptr
      }
    } else if (const BitCastInst *CI = dyn_cast<BitCastInst>(U)) {
      if (!AllUsesOfValueWillTrapIfNull(CI, PHIs)) return false;
    } else if (const GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U)) {
      if (!AllUsesOfValueWillTrapIfNull(GEPI, PHIs)) return false;
    } else if (const PHINode *PN = dyn_cast<PHINode>(U)) {
      // If we've already seen this phi node, ignore it, it has already been
      // checked.
      if (PHIs.insert(PN).second && !AllUsesOfValueWillTrapIfNull(PN, PHIs))
        return false;
    } else if (isa<ICmpInst>(U) &&
               isa<ConstantPointerNull>(U->getOperand(1))) {
      // Ignore icmp X, null
    } else {
      //cerr << "NONTRAPPING USE: " << *U;
      return false;
    }
  }
  return true;
}

/// Return true if all uses of any loads from GV will trap if the loaded value
/// is null.  Note that this also permits comparisons of the loaded value
/// against null, as a special case.
static bool AllUsesOfLoadedValueWillTrapIfNull(const GlobalVariable *GV) {
  for (const User *U : GV->users())
    if (const LoadInst *LI = dyn_cast<LoadInst>(U)) {
      SmallPtrSet<const PHINode*, 8> PHIs;
      if (!AllUsesOfValueWillTrapIfNull(LI, PHIs))
        return false;
    } else if (isa<StoreInst>(U)) {
      // Ignore stores to the global.
    } else {
      // We don't know or understand this user, bail out.
      //cerr << "UNKNOWN USER OF GLOBAL!: " << *U;
      return false;
    }
  return true;
}

static bool OptimizeAwayTrappingUsesOfValue(Value *V, Constant *NewV) {
  bool Changed = false;
  for (auto UI = V->user_begin(), E = V->user_end(); UI != E; ) {
    Instruction *I = cast<Instruction>(*UI++);
    // Uses are non-trapping if null pointer is considered valid.
    // Non address-space 0 globals are already pruned by the caller.
    if (NullPointerIsDefined(I->getFunction()))
      return false;
    if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
      LI->setOperand(0, NewV);
      Changed = true;
    } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
      if (SI->getOperand(1) == V) {
        SI->setOperand(1, NewV);
        Changed = true;
      }
    } else if (isa<CallInst>(I) || isa<InvokeInst>(I)) {
      CallSite CS(I);
      if (CS.getCalledValue() == V) {
        // Calling through the pointer!  Turn into a direct call, but be careful
        // that the pointer is not also being passed as an argument.
        CS.setCalledFunction(NewV);
        Changed = true;
        bool PassedAsArg = false;
        for (unsigned i = 0, e = CS.arg_size(); i != e; ++i)
          if (CS.getArgument(i) == V) {
            PassedAsArg = true;
            CS.setArgument(i, NewV);
          }

        if (PassedAsArg) {
          // Being passed as an argument also.  Be careful to not invalidate UI!
          UI = V->user_begin();
        }
      }
    } else if (CastInst *CI = dyn_cast<CastInst>(I)) {
      Changed |= OptimizeAwayTrappingUsesOfValue(CI,
                                ConstantExpr::getCast(CI->getOpcode(),
                                                      NewV, CI->getType()));
      if (CI->use_empty()) {
        Changed = true;
        CI->eraseFromParent();
      }
    } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(I)) {
      // Should handle GEP here.
      SmallVector<Constant*, 8> Idxs;
      Idxs.reserve(GEPI->getNumOperands()-1);
      for (User::op_iterator i = GEPI->op_begin() + 1, e = GEPI->op_end();
           i != e; ++i)
        if (Constant *C = dyn_cast<Constant>(*i))
          Idxs.push_back(C);
        else
          break;
      if (Idxs.size() == GEPI->getNumOperands()-1)
        Changed |= OptimizeAwayTrappingUsesOfValue(
            GEPI, ConstantExpr::getGetElementPtr(nullptr, NewV, Idxs));
      if (GEPI->use_empty()) {
        Changed = true;
        GEPI->eraseFromParent();
      }
    }
  }

  return Changed;
}

/// The specified global has only one non-null value stored into it.  If there
/// are uses of the loaded value that would trap if the loaded value is
/// dynamically null, then we know that they cannot be reachable with a null
/// optimize away the load.
static bool OptimizeAwayTrappingUsesOfLoads(GlobalVariable *GV, Constant *LV,
                                            const DataLayout &DL,
                                            TargetLibraryInfo *TLI) {
  bool Changed = false;

  // Keep track of whether we are able to remove all the uses of the global
  // other than the store that defines it.
  bool AllNonStoreUsesGone = true;

  // Replace all uses of loads with uses of uses of the stored value.
  for (Value::user_iterator GUI = GV->user_begin(), E = GV->user_end(); GUI != E;){
    User *GlobalUser = *GUI++;
    if (LoadInst *LI = dyn_cast<LoadInst>(GlobalUser)) {
      Changed |= OptimizeAwayTrappingUsesOfValue(LI, LV);
      // If we were able to delete all uses of the loads
      if (LI->use_empty()) {
        LI->eraseFromParent();
        Changed = true;
      } else {
        AllNonStoreUsesGone = false;
      }
    } else if (isa<StoreInst>(GlobalUser)) {
      // Ignore the store that stores "LV" to the global.
      assert(GlobalUser->getOperand(1) == GV &&
             "Must be storing *to* the global");
    } else {
      AllNonStoreUsesGone = false;

      // If we get here we could have other crazy uses that are transitively
      // loaded.
      assert((isa<PHINode>(GlobalUser) || isa<SelectInst>(GlobalUser) ||
              isa<ConstantExpr>(GlobalUser) || isa<CmpInst>(GlobalUser) ||
              isa<BitCastInst>(GlobalUser) ||
              isa<GetElementPtrInst>(GlobalUser)) &&
             "Only expect load and stores!");
    }
  }

  if (Changed) {
    LLVM_DEBUG(dbgs() << "OPTIMIZED LOADS FROM STORED ONCE POINTER: " << *GV
                      << "\n");
    ++NumGlobUses;
  }

  // If we nuked all of the loads, then none of the stores are needed either,
  // nor is the global.
  if (AllNonStoreUsesGone) {
    if (isLeakCheckerRoot(GV)) {
      Changed |= CleanupPointerRootUsers(GV, TLI);
    } else {
      Changed = true;
      CleanupConstantGlobalUsers(GV, nullptr, DL, TLI);
    }
    if (GV->use_empty()) {
      LLVM_DEBUG(dbgs() << "  *** GLOBAL NOW DEAD!\n");
      Changed = true;
      GV->eraseFromParent();
      ++NumDeleted;
    }
  }
  return Changed;
}

/// Walk the use list of V, constant folding all of the instructions that are
/// foldable.
static void ConstantPropUsersOf(Value *V, const DataLayout &DL,
                                TargetLibraryInfo *TLI) {
  for (Value::user_iterator UI = V->user_begin(), E = V->user_end(); UI != E; )
    if (Instruction *I = dyn_cast<Instruction>(*UI++))
      if (Constant *NewC = ConstantFoldInstruction(I, DL, TLI)) {
        I->replaceAllUsesWith(NewC);

        // Advance UI to the next non-I use to avoid invalidating it!
        // Instructions could multiply use V.
        while (UI != E && *UI == I)
          ++UI;
        if (isInstructionTriviallyDead(I, TLI))
          I->eraseFromParent();
      }
}

/// This function takes the specified global variable, and transforms the
/// program as if it always contained the result of the specified malloc.
/// Because it is always the result of the specified malloc, there is no reason
/// to actually DO the malloc.  Instead, turn the malloc into a global, and any
/// loads of GV as uses of the new global.
static GlobalVariable *
OptimizeGlobalAddressOfMalloc(GlobalVariable *GV, CallInst *CI, Type *AllocTy,
                              ConstantInt *NElements, const DataLayout &DL,
                              TargetLibraryInfo *TLI) {
  LLVM_DEBUG(errs() << "PROMOTING GLOBAL: " << *GV << "  CALL = " << *CI
                    << '\n');

  Type *GlobalType;
  if (NElements->getZExtValue() == 1)
    GlobalType = AllocTy;
  else
    // If we have an array allocation, the global variable is of an array.
    GlobalType = ArrayType::get(AllocTy, NElements->getZExtValue());

  // Create the new global variable.  The contents of the malloc'd memory is
  // undefined, so initialize with an undef value.
  GlobalVariable *NewGV = new GlobalVariable(
      *GV->getParent(), GlobalType, false, GlobalValue::InternalLinkage,
      UndefValue::get(GlobalType), GV->getName() + ".body", nullptr,
      GV->getThreadLocalMode());

  // If there are bitcast users of the malloc (which is typical, usually we have
  // a malloc + bitcast) then replace them with uses of the new global.  Update
  // other users to use the global as well.
  BitCastInst *TheBC = nullptr;
  while (!CI->use_empty()) {
    Instruction *User = cast<Instruction>(CI->user_back());
    if (BitCastInst *BCI = dyn_cast<BitCastInst>(User)) {
      if (BCI->getType() == NewGV->getType()) {
        BCI->replaceAllUsesWith(NewGV);
        BCI->eraseFromParent();
      } else {
        BCI->setOperand(0, NewGV);
      }
    } else {
      if (!TheBC)
        TheBC = new BitCastInst(NewGV, CI->getType(), "newgv", CI);
      User->replaceUsesOfWith(CI, TheBC);
    }
  }

  Constant *RepValue = NewGV;
  if (NewGV->getType() != GV->getValueType())
    RepValue = ConstantExpr::getBitCast(RepValue, GV->getValueType());

  // If there is a comparison against null, we will insert a global bool to
  // keep track of whether the global was initialized yet or not.
  GlobalVariable *InitBool =
    new GlobalVariable(Type::getInt1Ty(GV->getContext()), false,
                       GlobalValue::InternalLinkage,
                       ConstantInt::getFalse(GV->getContext()),
                       GV->getName()+".init", GV->getThreadLocalMode());
  bool InitBoolUsed = false;

  // Loop over all uses of GV, processing them in turn.
  while (!GV->use_empty()) {
    if (StoreInst *SI = dyn_cast<StoreInst>(GV->user_back())) {
      // The global is initialized when the store to it occurs.
      new StoreInst(ConstantInt::getTrue(GV->getContext()), InitBool, false, 0,
                    SI->getOrdering(), SI->getSyncScopeID(), SI);
      SI->eraseFromParent();
      continue;
    }

    LoadInst *LI = cast<LoadInst>(GV->user_back());
    while (!LI->use_empty()) {
      Use &LoadUse = *LI->use_begin();
      ICmpInst *ICI = dyn_cast<ICmpInst>(LoadUse.getUser());
      if (!ICI) {
        LoadUse = RepValue;
        continue;
      }

      // Replace the cmp X, 0 with a use of the bool value.
      // Sink the load to where the compare was, if atomic rules allow us to.
      Value *LV = new LoadInst(InitBool, InitBool->getName()+".val", false, 0,
                               LI->getOrdering(), LI->getSyncScopeID(),
                               LI->isUnordered() ? (Instruction*)ICI : LI);
      InitBoolUsed = true;
      switch (ICI->getPredicate()) {
      default: llvm_unreachable("Unknown ICmp Predicate!");
      case ICmpInst::ICMP_ULT:
      case ICmpInst::ICMP_SLT:   // X < null -> always false
        LV = ConstantInt::getFalse(GV->getContext());
        break;
      case ICmpInst::ICMP_ULE:
      case ICmpInst::ICMP_SLE:
      case ICmpInst::ICMP_EQ:
        LV = BinaryOperator::CreateNot(LV, "notinit", ICI);
        break;
      case ICmpInst::ICMP_NE:
      case ICmpInst::ICMP_UGE:
      case ICmpInst::ICMP_SGE:
      case ICmpInst::ICMP_UGT:
      case ICmpInst::ICMP_SGT:
        break;  // no change.
      }
      ICI->replaceAllUsesWith(LV);
      ICI->eraseFromParent();
    }
    LI->eraseFromParent();
  }

  // If the initialization boolean was used, insert it, otherwise delete it.
  if (!InitBoolUsed) {
    while (!InitBool->use_empty())  // Delete initializations
      cast<StoreInst>(InitBool->user_back())->eraseFromParent();
    delete InitBool;
  } else
    GV->getParent()->getGlobalList().insert(GV->getIterator(), InitBool);

  // Now the GV is dead, nuke it and the malloc..
  GV->eraseFromParent();
  CI->eraseFromParent();

  // To further other optimizations, loop over all users of NewGV and try to
  // constant prop them.  This will promote GEP instructions with constant
  // indices into GEP constant-exprs, which will allow global-opt to hack on it.
  ConstantPropUsersOf(NewGV, DL, TLI);
  if (RepValue != NewGV)
    ConstantPropUsersOf(RepValue, DL, TLI);

  return NewGV;
}

/// Scan the use-list of V checking to make sure that there are no complex uses
/// of V.  We permit simple things like dereferencing the pointer, but not
/// storing through the address, unless it is to the specified global.
static bool ValueIsOnlyUsedLocallyOrStoredToOneGlobal(const Instruction *V,
                                                      const GlobalVariable *GV,
                                        SmallPtrSetImpl<const PHINode*> &PHIs) {
  for (const User *U : V->users()) {
    const Instruction *Inst = cast<Instruction>(U);

    if (isa<LoadInst>(Inst) || isa<CmpInst>(Inst)) {
      continue; // Fine, ignore.
    }

    if (const StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
      if (SI->getOperand(0) == V && SI->getOperand(1) != GV)
        return false;  // Storing the pointer itself... bad.
      continue; // Otherwise, storing through it, or storing into GV... fine.
    }

    // Must index into the array and into the struct.
    if (isa<GetElementPtrInst>(Inst) && Inst->getNumOperands() >= 3) {
      if (!ValueIsOnlyUsedLocallyOrStoredToOneGlobal(Inst, GV, PHIs))
        return false;
      continue;
    }

    if (const PHINode *PN = dyn_cast<PHINode>(Inst)) {
      // PHIs are ok if all uses are ok.  Don't infinitely recurse through PHI
      // cycles.
      if (PHIs.insert(PN).second)
        if (!ValueIsOnlyUsedLocallyOrStoredToOneGlobal(PN, GV, PHIs))
          return false;
      continue;
    }

    if (const BitCastInst *BCI = dyn_cast<BitCastInst>(Inst)) {
      if (!ValueIsOnlyUsedLocallyOrStoredToOneGlobal(BCI, GV, PHIs))
        return false;
      continue;
    }

    return false;
  }
  return true;
}

/// The Alloc pointer is stored into GV somewhere.  Transform all uses of the
/// allocation into loads from the global and uses of the resultant pointer.
/// Further, delete the store into GV.  This assumes that these value pass the
/// 'ValueIsOnlyUsedLocallyOrStoredToOneGlobal' predicate.
static void ReplaceUsesOfMallocWithGlobal(Instruction *Alloc,
                                          GlobalVariable *GV) {
  while (!Alloc->use_empty()) {
    Instruction *U = cast<Instruction>(*Alloc->user_begin());
    Instruction *InsertPt = U;
    if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
      // If this is the store of the allocation into the global, remove it.
      if (SI->getOperand(1) == GV) {
        SI->eraseFromParent();
        continue;
      }
    } else if (PHINode *PN = dyn_cast<PHINode>(U)) {
      // Insert the load in the corresponding predecessor, not right before the
      // PHI.
      InsertPt = PN->getIncomingBlock(*Alloc->use_begin())->getTerminator();
    } else if (isa<BitCastInst>(U)) {
      // Must be bitcast between the malloc and store to initialize the global.
      ReplaceUsesOfMallocWithGlobal(U, GV);
      U->eraseFromParent();
      continue;
    } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(U)) {
      // If this is a "GEP bitcast" and the user is a store to the global, then
      // just process it as a bitcast.
      if (GEPI->hasAllZeroIndices() && GEPI->hasOneUse())
        if (StoreInst *SI = dyn_cast<StoreInst>(GEPI->user_back()))
          if (SI->getOperand(1) == GV) {
            // Must be bitcast GEP between the malloc and store to initialize
            // the global.
            ReplaceUsesOfMallocWithGlobal(GEPI, GV);
            GEPI->eraseFromParent();
            continue;
          }
    }

    // Insert a load from the global, and use it instead of the malloc.
    Value *NL = new LoadInst(GV, GV->getName()+".val", InsertPt);
    U->replaceUsesOfWith(Alloc, NL);
  }
}

/// Verify that all uses of V (a load, or a phi of a load) are simple enough to
/// perform heap SRA on.  This permits GEP's that index through the array and
/// struct field, icmps of null, and PHIs.
static bool LoadUsesSimpleEnoughForHeapSRA(const Value *V,
                        SmallPtrSetImpl<const PHINode*> &LoadUsingPHIs,
                        SmallPtrSetImpl<const PHINode*> &LoadUsingPHIsPerLoad) {
  // We permit two users of the load: setcc comparing against the null
  // pointer, and a getelementptr of a specific form.
  for (const User *U : V->users()) {
    const Instruction *UI = cast<Instruction>(U);

    // Comparison against null is ok.
    if (const ICmpInst *ICI = dyn_cast<ICmpInst>(UI)) {
      if (!isa<ConstantPointerNull>(ICI->getOperand(1)))
        return false;
      continue;
    }

    // getelementptr is also ok, but only a simple form.
    if (const GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(UI)) {
      // Must index into the array and into the struct.
      if (GEPI->getNumOperands() < 3)
        return false;

      // Otherwise the GEP is ok.
      continue;
    }

    if (const PHINode *PN = dyn_cast<PHINode>(UI)) {
      if (!LoadUsingPHIsPerLoad.insert(PN).second)
        // This means some phi nodes are dependent on each other.
        // Avoid infinite looping!
        return false;
      if (!LoadUsingPHIs.insert(PN).second)
        // If we have already analyzed this PHI, then it is safe.
        continue;

      // Make sure all uses of the PHI are simple enough to transform.
      if (!LoadUsesSimpleEnoughForHeapSRA(PN,
                                          LoadUsingPHIs, LoadUsingPHIsPerLoad))
        return false;

      continue;
    }

    // Otherwise we don't know what this is, not ok.
    return false;
  }

  return true;
}

/// If all users of values loaded from GV are simple enough to perform HeapSRA,
/// return true.
static bool AllGlobalLoadUsesSimpleEnoughForHeapSRA(const GlobalVariable *GV,
                                                    Instruction *StoredVal) {
  SmallPtrSet<const PHINode*, 32> LoadUsingPHIs;
  SmallPtrSet<const PHINode*, 32> LoadUsingPHIsPerLoad;
  for (const User *U : GV->users())
    if (const LoadInst *LI = dyn_cast<LoadInst>(U)) {
      if (!LoadUsesSimpleEnoughForHeapSRA(LI, LoadUsingPHIs,
                                          LoadUsingPHIsPerLoad))
        return false;
      LoadUsingPHIsPerLoad.clear();
    }

  // If we reach here, we know that all uses of the loads and transitive uses
  // (through PHI nodes) are simple enough to transform.  However, we don't know
  // that all inputs the to the PHI nodes are in the same equivalence sets.
  // Check to verify that all operands of the PHIs are either PHIS that can be
  // transformed, loads from GV, or MI itself.
  for (const PHINode *PN : LoadUsingPHIs) {
    for (unsigned op = 0, e = PN->getNumIncomingValues(); op != e; ++op) {
      Value *InVal = PN->getIncomingValue(op);

      // PHI of the stored value itself is ok.
      if (InVal == StoredVal) continue;

      if (const PHINode *InPN = dyn_cast<PHINode>(InVal)) {
        // One of the PHIs in our set is (optimistically) ok.
        if (LoadUsingPHIs.count(InPN))
          continue;
        return false;
      }

      // Load from GV is ok.
      if (const LoadInst *LI = dyn_cast<LoadInst>(InVal))
        if (LI->getOperand(0) == GV)
          continue;

      // UNDEF? NULL?

      // Anything else is rejected.
      return false;
    }
  }

  return true;
}

static Value *GetHeapSROAValue(Value *V, unsigned FieldNo,
              DenseMap<Value *, std::vector<Value *>> &InsertedScalarizedValues,
                   std::vector<std::pair<PHINode *, unsigned>> &PHIsToRewrite) {
  std::vector<Value *> &FieldVals = InsertedScalarizedValues[V];

  if (FieldNo >= FieldVals.size())
    FieldVals.resize(FieldNo+1);

  // If we already have this value, just reuse the previously scalarized
  // version.
  if (Value *FieldVal = FieldVals[FieldNo])
    return FieldVal;

  // Depending on what instruction this is, we have several cases.
  Value *Result;
  if (LoadInst *LI = dyn_cast<LoadInst>(V)) {
    // This is a scalarized version of the load from the global.  Just create
    // a new Load of the scalarized global.
    Result = new LoadInst(GetHeapSROAValue(LI->getOperand(0), FieldNo,
                                           InsertedScalarizedValues,
                                           PHIsToRewrite),
                          LI->getName()+".f"+Twine(FieldNo), LI);
  } else {
    PHINode *PN = cast<PHINode>(V);
    // PN's type is pointer to struct.  Make a new PHI of pointer to struct
    // field.

    PointerType *PTy = cast<PointerType>(PN->getType());
    StructType *ST = cast<StructType>(PTy->getElementType());

    unsigned AS = PTy->getAddressSpace();
    PHINode *NewPN =
      PHINode::Create(PointerType::get(ST->getElementType(FieldNo), AS),
                     PN->getNumIncomingValues(),
                     PN->getName()+".f"+Twine(FieldNo), PN);
    Result = NewPN;
    PHIsToRewrite.push_back(std::make_pair(PN, FieldNo));
  }

  return FieldVals[FieldNo] = Result;
}

/// Given a load instruction and a value derived from the load, rewrite the
/// derived value to use the HeapSRoA'd load.
static void RewriteHeapSROALoadUser(Instruction *LoadUser,
              DenseMap<Value *, std::vector<Value *>> &InsertedScalarizedValues,
                   std::vector<std::pair<PHINode *, unsigned>> &PHIsToRewrite) {
  // If this is a comparison against null, handle it.
  if (ICmpInst *SCI = dyn_cast<ICmpInst>(LoadUser)) {
    assert(isa<ConstantPointerNull>(SCI->getOperand(1)));
    // If we have a setcc of the loaded pointer, we can use a setcc of any
    // field.
    Value *NPtr = GetHeapSROAValue(SCI->getOperand(0), 0,
                                   InsertedScalarizedValues, PHIsToRewrite);

    Value *New = new ICmpInst(SCI, SCI->getPredicate(), NPtr,
                              Constant::getNullValue(NPtr->getType()),
                              SCI->getName());
    SCI->replaceAllUsesWith(New);
    SCI->eraseFromParent();
    return;
  }

  // Handle 'getelementptr Ptr, Idx, i32 FieldNo ...'
  if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(LoadUser)) {
    assert(GEPI->getNumOperands() >= 3 && isa<ConstantInt>(GEPI->getOperand(2))
           && "Unexpected GEPI!");

    // Load the pointer for this field.
    unsigned FieldNo = cast<ConstantInt>(GEPI->getOperand(2))->getZExtValue();
    Value *NewPtr = GetHeapSROAValue(GEPI->getOperand(0), FieldNo,
                                     InsertedScalarizedValues, PHIsToRewrite);

    // Create the new GEP idx vector.
    SmallVector<Value*, 8> GEPIdx;
    GEPIdx.push_back(GEPI->getOperand(1));
    GEPIdx.append(GEPI->op_begin()+3, GEPI->op_end());

    Value *NGEPI = GetElementPtrInst::Create(GEPI->getResultElementType(), NewPtr, GEPIdx,
                                             GEPI->getName(), GEPI);
    GEPI->replaceAllUsesWith(NGEPI);
    GEPI->eraseFromParent();
    return;
  }

  // Recursively transform the users of PHI nodes.  This will lazily create the
  // PHIs that are needed for individual elements.  Keep track of what PHIs we
  // see in InsertedScalarizedValues so that we don't get infinite loops (very
  // antisocial).  If the PHI is already in InsertedScalarizedValues, it has
  // already been seen first by another load, so its uses have already been
  // processed.
  PHINode *PN = cast<PHINode>(LoadUser);
  if (!InsertedScalarizedValues.insert(std::make_pair(PN,
                                              std::vector<Value *>())).second)
    return;

  // If this is the first time we've seen this PHI, recursively process all
  // users.
  for (auto UI = PN->user_begin(), E = PN->user_end(); UI != E;) {
    Instruction *User = cast<Instruction>(*UI++);
    RewriteHeapSROALoadUser(User, InsertedScalarizedValues, PHIsToRewrite);
  }
}

/// We are performing Heap SRoA on a global.  Ptr is a value loaded from the
/// global.  Eliminate all uses of Ptr, making them use FieldGlobals instead.
/// All uses of loaded values satisfy AllGlobalLoadUsesSimpleEnoughForHeapSRA.
static void RewriteUsesOfLoadForHeapSRoA(LoadInst *Load,
              DenseMap<Value *, std::vector<Value *>> &InsertedScalarizedValues,
                  std::vector<std::pair<PHINode *, unsigned> > &PHIsToRewrite) {
  for (auto UI = Load->user_begin(), E = Load->user_end(); UI != E;) {
    Instruction *User = cast<Instruction>(*UI++);
    RewriteHeapSROALoadUser(User, InsertedScalarizedValues, PHIsToRewrite);
  }

  if (Load->use_empty()) {
    Load->eraseFromParent();
    InsertedScalarizedValues.erase(Load);
  }
}

/// CI is an allocation of an array of structures.  Break it up into multiple
/// allocations of arrays of the fields.
static GlobalVariable *PerformHeapAllocSRoA(GlobalVariable *GV, CallInst *CI,
                                            Value *NElems, const DataLayout &DL,
                                            const TargetLibraryInfo *TLI) {
  LLVM_DEBUG(dbgs() << "SROA HEAP ALLOC: " << *GV << "  MALLOC = " << *CI
                    << '\n');
  Type *MAT = getMallocAllocatedType(CI, TLI);
  StructType *STy = cast<StructType>(MAT);

  // There is guaranteed to be at least one use of the malloc (storing
  // it into GV).  If there are other uses, change them to be uses of
  // the global to simplify later code.  This also deletes the store
  // into GV.
  ReplaceUsesOfMallocWithGlobal(CI, GV);

  // Okay, at this point, there are no users of the malloc.  Insert N
  // new mallocs at the same place as CI, and N globals.
  std::vector<Value *> FieldGlobals;
  std::vector<Value *> FieldMallocs;

  SmallVector<OperandBundleDef, 1> OpBundles;
  CI->getOperandBundlesAsDefs(OpBundles);

  unsigned AS = GV->getType()->getPointerAddressSpace();
  for (unsigned FieldNo = 0, e = STy->getNumElements(); FieldNo != e;++FieldNo){
    Type *FieldTy = STy->getElementType(FieldNo);
    PointerType *PFieldTy = PointerType::get(FieldTy, AS);

    GlobalVariable *NGV = new GlobalVariable(
        *GV->getParent(), PFieldTy, false, GlobalValue::InternalLinkage,
        Constant::getNullValue(PFieldTy), GV->getName() + ".f" + Twine(FieldNo),
        nullptr, GV->getThreadLocalMode());
    NGV->copyAttributesFrom(GV);
    FieldGlobals.push_back(NGV);

    unsigned TypeSize = DL.getTypeAllocSize(FieldTy);
    if (StructType *ST = dyn_cast<StructType>(FieldTy))
      TypeSize = DL.getStructLayout(ST)->getSizeInBytes();
    Type *IntPtrTy = DL.getIntPtrType(CI->getType());
    Value *NMI = CallInst::CreateMalloc(CI, IntPtrTy, FieldTy,
                                        ConstantInt::get(IntPtrTy, TypeSize),
                                        NElems, OpBundles, nullptr,
                                        CI->getName() + ".f" + Twine(FieldNo));
    FieldMallocs.push_back(NMI);
    new StoreInst(NMI, NGV, CI);
  }

  // The tricky aspect of this transformation is handling the case when malloc
  // fails.  In the original code, malloc failing would set the result pointer
  // of malloc to null.  In this case, some mallocs could succeed and others
  // could fail.  As such, we emit code that looks like this:
  //    F0 = malloc(field0)
  //    F1 = malloc(field1)
  //    F2 = malloc(field2)
  //    if (F0 == 0 || F1 == 0 || F2 == 0) {
  //      if (F0) { free(F0); F0 = 0; }
  //      if (F1) { free(F1); F1 = 0; }
  //      if (F2) { free(F2); F2 = 0; }
  //    }
  // The malloc can also fail if its argument is too large.
  Constant *ConstantZero = ConstantInt::get(CI->getArgOperand(0)->getType(), 0);
  Value *RunningOr = new ICmpInst(CI, ICmpInst::ICMP_SLT, CI->getArgOperand(0),
                                  ConstantZero, "isneg");
  for (unsigned i = 0, e = FieldMallocs.size(); i != e; ++i) {
    Value *Cond = new ICmpInst(CI, ICmpInst::ICMP_EQ, FieldMallocs[i],
                             Constant::getNullValue(FieldMallocs[i]->getType()),
                               "isnull");
    RunningOr = BinaryOperator::CreateOr(RunningOr, Cond, "tmp", CI);
  }

  // Split the basic block at the old malloc.
  BasicBlock *OrigBB = CI->getParent();
  BasicBlock *ContBB =
      OrigBB->splitBasicBlock(CI->getIterator(), "malloc_cont");

  // Create the block to check the first condition.  Put all these blocks at the
  // end of the function as they are unlikely to be executed.
  BasicBlock *NullPtrBlock = BasicBlock::Create(OrigBB->getContext(),
                                                "malloc_ret_null",
                                                OrigBB->getParent());

  // Remove the uncond branch from OrigBB to ContBB, turning it into a cond
  // branch on RunningOr.
  OrigBB->getTerminator()->eraseFromParent();
  BranchInst::Create(NullPtrBlock, ContBB, RunningOr, OrigBB);

  // Within the NullPtrBlock, we need to emit a comparison and branch for each
  // pointer, because some may be null while others are not.
  for (unsigned i = 0, e = FieldGlobals.size(); i != e; ++i) {
    Value *GVVal = new LoadInst(FieldGlobals[i], "tmp", NullPtrBlock);
    Value *Cmp = new ICmpInst(*NullPtrBlock, ICmpInst::ICMP_NE, GVVal,
                              Constant::getNullValue(GVVal->getType()));
    BasicBlock *FreeBlock = BasicBlock::Create(Cmp->getContext(), "free_it",
                                               OrigBB->getParent());
    BasicBlock *NextBlock = BasicBlock::Create(Cmp->getContext(), "next",
                                               OrigBB->getParent());
    Instruction *BI = BranchInst::Create(FreeBlock, NextBlock,
                                         Cmp, NullPtrBlock);

    // Fill in FreeBlock.
    CallInst::CreateFree(GVVal, OpBundles, BI);
    new StoreInst(Constant::getNullValue(GVVal->getType()), FieldGlobals[i],
                  FreeBlock);
    BranchInst::Create(NextBlock, FreeBlock);

    NullPtrBlock = NextBlock;
  }

  BranchInst::Create(ContBB, NullPtrBlock);

  // CI is no longer needed, remove it.
  CI->eraseFromParent();

  /// As we process loads, if we can't immediately update all uses of the load,
  /// keep track of what scalarized loads are inserted for a given load.
  DenseMap<Value *, std::vector<Value *>> InsertedScalarizedValues;
  InsertedScalarizedValues[GV] = FieldGlobals;

  std::vector<std::pair<PHINode *, unsigned>> PHIsToRewrite;

  // Okay, the malloc site is completely handled.  All of the uses of GV are now
  // loads, and all uses of those loads are simple.  Rewrite them to use loads
  // of the per-field globals instead.
  for (auto UI = GV->user_begin(), E = GV->user_end(); UI != E;) {
    Instruction *User = cast<Instruction>(*UI++);

    if (LoadInst *LI = dyn_cast<LoadInst>(User)) {
      RewriteUsesOfLoadForHeapSRoA(LI, InsertedScalarizedValues, PHIsToRewrite);
      continue;
    }

    // Must be a store of null.
    StoreInst *SI = cast<StoreInst>(User);
    assert(isa<ConstantPointerNull>(SI->getOperand(0)) &&
           "Unexpected heap-sra user!");

    // Insert a store of null into each global.
    for (unsigned i = 0, e = FieldGlobals.size(); i != e; ++i) {
      Type *ValTy = cast<GlobalValue>(FieldGlobals[i])->getValueType();
      Constant *Null = Constant::getNullValue(ValTy);
      new StoreInst(Null, FieldGlobals[i], SI);
    }
    // Erase the original store.
    SI->eraseFromParent();
  }

  // While we have PHIs that are interesting to rewrite, do it.
  while (!PHIsToRewrite.empty()) {
    PHINode *PN = PHIsToRewrite.back().first;
    unsigned FieldNo = PHIsToRewrite.back().second;
    PHIsToRewrite.pop_back();
    PHINode *FieldPN = cast<PHINode>(InsertedScalarizedValues[PN][FieldNo]);
    assert(FieldPN->getNumIncomingValues() == 0 &&"Already processed this phi");

    // Add all the incoming values.  This can materialize more phis.
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      Value *InVal = PN->getIncomingValue(i);
      InVal = GetHeapSROAValue(InVal, FieldNo, InsertedScalarizedValues,
                               PHIsToRewrite);
      FieldPN->addIncoming(InVal, PN->getIncomingBlock(i));
    }
  }

  // Drop all inter-phi links and any loads that made it this far.
  for (DenseMap<Value *, std::vector<Value *>>::iterator
       I = InsertedScalarizedValues.begin(), E = InsertedScalarizedValues.end();
       I != E; ++I) {
    if (PHINode *PN = dyn_cast<PHINode>(I->first))
      PN->dropAllReferences();
    else if (LoadInst *LI = dyn_cast<LoadInst>(I->first))
      LI->dropAllReferences();
  }

  // Delete all the phis and loads now that inter-references are dead.
  for (DenseMap<Value *, std::vector<Value *>>::iterator
       I = InsertedScalarizedValues.begin(), E = InsertedScalarizedValues.end();
       I != E; ++I) {
    if (PHINode *PN = dyn_cast<PHINode>(I->first))
      PN->eraseFromParent();
    else if (LoadInst *LI = dyn_cast<LoadInst>(I->first))
      LI->eraseFromParent();
  }

  // The old global is now dead, remove it.
  GV->eraseFromParent();

  ++NumHeapSRA;
  return cast<GlobalVariable>(FieldGlobals[0]);
}

/// This function is called when we see a pointer global variable with a single
/// value stored it that is a malloc or cast of malloc.
static bool tryToOptimizeStoreOfMallocToGlobal(GlobalVariable *GV, CallInst *CI,
                                               Type *AllocTy,
                                               AtomicOrdering Ordering,
                                               const DataLayout &DL,
                                               TargetLibraryInfo *TLI) {
  // If this is a malloc of an abstract type, don't touch it.
  if (!AllocTy->isSized())
    return false;

  // We can't optimize this global unless all uses of it are *known* to be
  // of the malloc value, not of the null initializer value (consider a use
  // that compares the global's value against zero to see if the malloc has
  // been reached).  To do this, we check to see if all uses of the global
  // would trap if the global were null: this proves that they must all
  // happen after the malloc.
  if (!AllUsesOfLoadedValueWillTrapIfNull(GV))
    return false;

  // We can't optimize this if the malloc itself is used in a complex way,
  // for example, being stored into multiple globals.  This allows the
  // malloc to be stored into the specified global, loaded icmp'd, and
  // GEP'd.  These are all things we could transform to using the global
  // for.
  SmallPtrSet<const PHINode*, 8> PHIs;
  if (!ValueIsOnlyUsedLocallyOrStoredToOneGlobal(CI, GV, PHIs))
    return false;

  // If we have a global that is only initialized with a fixed size malloc,
  // transform the program to use global memory instead of malloc'd memory.
  // This eliminates dynamic allocation, avoids an indirection accessing the
  // data, and exposes the resultant global to further GlobalOpt.
  // We cannot optimize the malloc if we cannot determine malloc array size.
  Value *NElems = getMallocArraySize(CI, DL, TLI, true);
  if (!NElems)
    return false;

  if (ConstantInt *NElements = dyn_cast<ConstantInt>(NElems))
    // Restrict this transformation to only working on small allocations
    // (2048 bytes currently), as we don't want to introduce a 16M global or
    // something.
    if (NElements->getZExtValue() * DL.getTypeAllocSize(AllocTy) < 2048) {
      OptimizeGlobalAddressOfMalloc(GV, CI, AllocTy, NElements, DL, TLI);
      return true;
    }

  // If the allocation is an array of structures, consider transforming this
  // into multiple malloc'd arrays, one for each field.  This is basically
  // SRoA for malloc'd memory.

  if (Ordering != AtomicOrdering::NotAtomic)
    return false;

  // If this is an allocation of a fixed size array of structs, analyze as a
  // variable size array.  malloc [100 x struct],1 -> malloc struct, 100
  if (NElems == ConstantInt::get(CI->getArgOperand(0)->getType(), 1))
    if (ArrayType *AT = dyn_cast<ArrayType>(AllocTy))
      AllocTy = AT->getElementType();

  StructType *AllocSTy = dyn_cast<StructType>(AllocTy);
  if (!AllocSTy)
    return false;

  // This the structure has an unreasonable number of fields, leave it
  // alone.
  if (AllocSTy->getNumElements() <= 16 && AllocSTy->getNumElements() != 0 &&
      AllGlobalLoadUsesSimpleEnoughForHeapSRA(GV, CI)) {

    // If this is a fixed size array, transform the Malloc to be an alloc of
    // structs.  malloc [100 x struct],1 -> malloc struct, 100
    if (ArrayType *AT = dyn_cast<ArrayType>(getMallocAllocatedType(CI, TLI))) {
      Type *IntPtrTy = DL.getIntPtrType(CI->getType());
      unsigned TypeSize = DL.getStructLayout(AllocSTy)->getSizeInBytes();
      Value *AllocSize = ConstantInt::get(IntPtrTy, TypeSize);
      Value *NumElements = ConstantInt::get(IntPtrTy, AT->getNumElements());
      SmallVector<OperandBundleDef, 1> OpBundles;
      CI->getOperandBundlesAsDefs(OpBundles);
      Instruction *Malloc =
          CallInst::CreateMalloc(CI, IntPtrTy, AllocSTy, AllocSize, NumElements,
                                 OpBundles, nullptr, CI->getName());
      Instruction *Cast = new BitCastInst(Malloc, CI->getType(), "tmp", CI);
      CI->replaceAllUsesWith(Cast);
      CI->eraseFromParent();
      if (BitCastInst *BCI = dyn_cast<BitCastInst>(Malloc))
        CI = cast<CallInst>(BCI->getOperand(0));
      else
        CI = cast<CallInst>(Malloc);
    }

    PerformHeapAllocSRoA(GV, CI, getMallocArraySize(CI, DL, TLI, true), DL,
                         TLI);
    return true;
  }

  return false;
}

// Try to optimize globals based on the knowledge that only one value (besides
// its initializer) is ever stored to the global.
static bool optimizeOnceStoredGlobal(GlobalVariable *GV, Value *StoredOnceVal,
                                     AtomicOrdering Ordering,
                                     const DataLayout &DL,
                                     TargetLibraryInfo *TLI) {
  // Ignore no-op GEPs and bitcasts.
  StoredOnceVal = StoredOnceVal->stripPointerCasts();

  // If we are dealing with a pointer global that is initialized to null and
  // only has one (non-null) value stored into it, then we can optimize any
  // users of the loaded value (often calls and loads) that would trap if the
  // value was null.
  if (GV->getInitializer()->getType()->isPointerTy() &&
      GV->getInitializer()->isNullValue() &&
      !NullPointerIsDefined(
          nullptr /* F */,
          GV->getInitializer()->getType()->getPointerAddressSpace())) {
    if (Constant *SOVC = dyn_cast<Constant>(StoredOnceVal)) {
      if (GV->getInitializer()->getType() != SOVC->getType())
        SOVC = ConstantExpr::getBitCast(SOVC, GV->getInitializer()->getType());

      // Optimize away any trapping uses of the loaded value.
      if (OptimizeAwayTrappingUsesOfLoads(GV, SOVC, DL, TLI))
        return true;
    } else if (CallInst *CI = extractMallocCall(StoredOnceVal, TLI)) {
      Type *MallocType = getMallocAllocatedType(CI, TLI);
      if (MallocType && tryToOptimizeStoreOfMallocToGlobal(GV, CI, MallocType,
                                                           Ordering, DL, TLI))
        return true;
    }
  }

  return false;
}

/// At this point, we have learned that the only two values ever stored into GV
/// are its initializer and OtherVal.  See if we can shrink the global into a
/// boolean and select between the two values whenever it is used.  This exposes
/// the values to other scalar optimizations.
static bool TryToShrinkGlobalToBoolean(GlobalVariable *GV, Constant *OtherVal) {
  Type *GVElType = GV->getValueType();

  // If GVElType is already i1, it is already shrunk.  If the type of the GV is
  // an FP value, pointer or vector, don't do this optimization because a select
  // between them is very expensive and unlikely to lead to later
  // simplification.  In these cases, we typically end up with "cond ? v1 : v2"
  // where v1 and v2 both require constant pool loads, a big loss.
  if (GVElType == Type::getInt1Ty(GV->getContext()) ||
      GVElType->isFloatingPointTy() ||
      GVElType->isPointerTy() || GVElType->isVectorTy())
    return false;

  // Walk the use list of the global seeing if all the uses are load or store.
  // If there is anything else, bail out.
  for (User *U : GV->users())
    if (!isa<LoadInst>(U) && !isa<StoreInst>(U))
      return false;

  LLVM_DEBUG(dbgs() << "   *** SHRINKING TO BOOL: " << *GV << "\n");

  // Create the new global, initializing it to false.
  GlobalVariable *NewGV = new GlobalVariable(Type::getInt1Ty(GV->getContext()),
                                             false,
                                             GlobalValue::InternalLinkage,
                                        ConstantInt::getFalse(GV->getContext()),
                                             GV->getName()+".b",
                                             GV->getThreadLocalMode(),
                                             GV->getType()->getAddressSpace());
  NewGV->copyAttributesFrom(GV);
  GV->getParent()->getGlobalList().insert(GV->getIterator(), NewGV);

  Constant *InitVal = GV->getInitializer();
  assert(InitVal->getType() != Type::getInt1Ty(GV->getContext()) &&
         "No reason to shrink to bool!");

  SmallVector<DIGlobalVariableExpression *, 1> GVs;
  GV->getDebugInfo(GVs);

  // If initialized to zero and storing one into the global, we can use a cast
  // instead of a select to synthesize the desired value.
  bool IsOneZero = false;
  bool EmitOneOrZero = true;
  if (ConstantInt *CI = dyn_cast<ConstantInt>(OtherVal)){
    IsOneZero = InitVal->isNullValue() && CI->isOne();

    if (ConstantInt *CIInit = dyn_cast<ConstantInt>(GV->getInitializer())){
      uint64_t ValInit = CIInit->getZExtValue();
      uint64_t ValOther = CI->getZExtValue();
      uint64_t ValMinus = ValOther - ValInit;

      for(auto *GVe : GVs){
        DIGlobalVariable *DGV = GVe->getVariable();
        DIExpression *E = GVe->getExpression();

        // It is expected that the address of global optimized variable is on
        // top of the stack. After optimization, value of that variable will
        // be ether 0 for initial value or 1 for other value. The following
        // expression should return constant integer value depending on the
        // value at global object address:
        // val * (ValOther - ValInit) + ValInit:
        // DW_OP_deref DW_OP_constu <ValMinus>
        // DW_OP_mul DW_OP_constu <ValInit> DW_OP_plus DW_OP_stack_value
        SmallVector<uint64_t, 12> Ops = {
            dwarf::DW_OP_deref, dwarf::DW_OP_constu, ValMinus,
            dwarf::DW_OP_mul,   dwarf::DW_OP_constu, ValInit,
            dwarf::DW_OP_plus};
        E = DIExpression::prependOpcodes(E, Ops, DIExpression::WithStackValue);
        DIGlobalVariableExpression *DGVE =
          DIGlobalVariableExpression::get(NewGV->getContext(), DGV, E);
        NewGV->addDebugInfo(DGVE);
     }
     EmitOneOrZero = false;
    }
  }

  if (EmitOneOrZero) {
     // FIXME: This will only emit address for debugger on which will
     // be written only 0 or 1.
     for(auto *GV : GVs)
       NewGV->addDebugInfo(GV);
   }

  while (!GV->use_empty()) {
    Instruction *UI = cast<Instruction>(GV->user_back());
    if (StoreInst *SI = dyn_cast<StoreInst>(UI)) {
      // Change the store into a boolean store.
      bool StoringOther = SI->getOperand(0) == OtherVal;
      // Only do this if we weren't storing a loaded value.
      Value *StoreVal;
      if (StoringOther || SI->getOperand(0) == InitVal) {
        StoreVal = ConstantInt::get(Type::getInt1Ty(GV->getContext()),
                                    StoringOther);
      } else {
        // Otherwise, we are storing a previously loaded copy.  To do this,
        // change the copy from copying the original value to just copying the
        // bool.
        Instruction *StoredVal = cast<Instruction>(SI->getOperand(0));

        // If we've already replaced the input, StoredVal will be a cast or
        // select instruction.  If not, it will be a load of the original
        // global.
        if (LoadInst *LI = dyn_cast<LoadInst>(StoredVal)) {
          assert(LI->getOperand(0) == GV && "Not a copy!");
          // Insert a new load, to preserve the saved value.
          StoreVal = new LoadInst(NewGV, LI->getName()+".b", false, 0,
                                  LI->getOrdering(), LI->getSyncScopeID(), LI);
        } else {
          assert((isa<CastInst>(StoredVal) || isa<SelectInst>(StoredVal)) &&
                 "This is not a form that we understand!");
          StoreVal = StoredVal->getOperand(0);
          assert(isa<LoadInst>(StoreVal) && "Not a load of NewGV!");
        }
      }
      StoreInst *NSI =
          new StoreInst(StoreVal, NewGV, false, 0, SI->getOrdering(),
                        SI->getSyncScopeID(), SI);
      NSI->setDebugLoc(SI->getDebugLoc());
    } else {
      // Change the load into a load of bool then a select.
      LoadInst *LI = cast<LoadInst>(UI);
      LoadInst *NLI = new LoadInst(NewGV, LI->getName()+".b", false, 0,
                                   LI->getOrdering(), LI->getSyncScopeID(), LI);
      Instruction *NSI;
      if (IsOneZero)
        NSI = new ZExtInst(NLI, LI->getType(), "", LI);
      else
        NSI = SelectInst::Create(NLI, OtherVal, InitVal, "", LI);
      NSI->takeName(LI);
      // Since LI is split into two instructions, NLI and NSI both inherit the
      // same DebugLoc
      NLI->setDebugLoc(LI->getDebugLoc());
      NSI->setDebugLoc(LI->getDebugLoc());
      LI->replaceAllUsesWith(NSI);
    }
    UI->eraseFromParent();
  }

  // Retain the name of the old global variable. People who are debugging their
  // programs may expect these variables to be named the same.
  NewGV->takeName(GV);
  GV->eraseFromParent();
  return true;
}

static bool deleteIfDead(
    GlobalValue &GV, SmallPtrSetImpl<const Comdat *> &NotDiscardableComdats) {
  GV.removeDeadConstantUsers();

  if (!GV.isDiscardableIfUnused() && !GV.isDeclaration())
    return false;

  if (const Comdat *C = GV.getComdat())
    if (!GV.hasLocalLinkage() && NotDiscardableComdats.count(C))
      return false;

  bool Dead;
  if (auto *F = dyn_cast<Function>(&GV))
    Dead = (F->isDeclaration() && F->use_empty()) || F->isDefTriviallyDead();
  else
    Dead = GV.use_empty();
  if (!Dead)
    return false;

  LLVM_DEBUG(dbgs() << "GLOBAL DEAD: " << GV << "\n");
  GV.eraseFromParent();
  ++NumDeleted;
  return true;
}

static bool isPointerValueDeadOnEntryToFunction(
    const Function *F, GlobalValue *GV,
    function_ref<DominatorTree &(Function &)> LookupDomTree) {
  // Find all uses of GV. We expect them all to be in F, and if we can't
  // identify any of the uses we bail out.
  //
  // On each of these uses, identify if the memory that GV points to is
  // used/required/live at the start of the function. If it is not, for example
  // if the first thing the function does is store to the GV, the GV can
  // possibly be demoted.
  //
  // We don't do an exhaustive search for memory operations - simply look
  // through bitcasts as they're quite common and benign.
  const DataLayout &DL = GV->getParent()->getDataLayout();
  SmallVector<LoadInst *, 4> Loads;
  SmallVector<StoreInst *, 4> Stores;
  for (auto *U : GV->users()) {
    if (Operator::getOpcode(U) == Instruction::BitCast) {
      for (auto *UU : U->users()) {
        if (auto *LI = dyn_cast<LoadInst>(UU))
          Loads.push_back(LI);
        else if (auto *SI = dyn_cast<StoreInst>(UU))
          Stores.push_back(SI);
        else
          return false;
      }
      continue;
    }

    Instruction *I = dyn_cast<Instruction>(U);
    if (!I)
      return false;
    assert(I->getParent()->getParent() == F);

    if (auto *LI = dyn_cast<LoadInst>(I))
      Loads.push_back(LI);
    else if (auto *SI = dyn_cast<StoreInst>(I))
      Stores.push_back(SI);
    else
      return false;
  }

  // We have identified all uses of GV into loads and stores. Now check if all
  // of them are known not to depend on the value of the global at the function
  // entry point. We do this by ensuring that every load is dominated by at
  // least one store.
  auto &DT = LookupDomTree(*const_cast<Function *>(F));

  // The below check is quadratic. Check we're not going to do too many tests.
  // FIXME: Even though this will always have worst-case quadratic time, we
  // could put effort into minimizing the average time by putting stores that
  // have been shown to dominate at least one load at the beginning of the
  // Stores array, making subsequent dominance checks more likely to succeed
  // early.
  //
  // The threshold here is fairly large because global->local demotion is a
  // very powerful optimization should it fire.
  const unsigned Threshold = 100;
  if (Loads.size() * Stores.size() > Threshold)
    return false;

  for (auto *L : Loads) {
    auto *LTy = L->getType();
    if (none_of(Stores, [&](const StoreInst *S) {
          auto *STy = S->getValueOperand()->getType();
          // The load is only dominated by the store if DomTree says so
          // and the number of bits loaded in L is less than or equal to
          // the number of bits stored in S.
          return DT.dominates(S, L) &&
                 DL.getTypeStoreSize(LTy) <= DL.getTypeStoreSize(STy);
        }))
      return false;
  }
  // All loads have known dependences inside F, so the global can be localized.
  return true;
}

/// C may have non-instruction users. Can all of those users be turned into
/// instructions?
static bool allNonInstructionUsersCanBeMadeInstructions(Constant *C) {
  // We don't do this exhaustively. The most common pattern that we really need
  // to care about is a constant GEP or constant bitcast - so just looking
  // through one single ConstantExpr.
  //
  // The set of constants that this function returns true for must be able to be
  // handled by makeAllConstantUsesInstructions.
  for (auto *U : C->users()) {
    if (isa<Instruction>(U))
      continue;
    if (!isa<ConstantExpr>(U))
      // Non instruction, non-constantexpr user; cannot convert this.
      return false;
    for (auto *UU : U->users())
      if (!isa<Instruction>(UU))
        // A constantexpr used by another constant. We don't try and recurse any
        // further but just bail out at this point.
        return false;
  }

  return true;
}

/// C may have non-instruction users, and
/// allNonInstructionUsersCanBeMadeInstructions has returned true. Convert the
/// non-instruction users to instructions.
static void makeAllConstantUsesInstructions(Constant *C) {
  SmallVector<ConstantExpr*,4> Users;
  for (auto *U : C->users()) {
    if (isa<ConstantExpr>(U))
      Users.push_back(cast<ConstantExpr>(U));
    else
      // We should never get here; allNonInstructionUsersCanBeMadeInstructions
      // should not have returned true for C.
      assert(
          isa<Instruction>(U) &&
          "Can't transform non-constantexpr non-instruction to instruction!");
  }

  SmallVector<Value*,4> UUsers;
  for (auto *U : Users) {
    UUsers.clear();
    for (auto *UU : U->users())
      UUsers.push_back(UU);
    for (auto *UU : UUsers) {
      Instruction *UI = cast<Instruction>(UU);
      Instruction *NewU = U->getAsInstruction();
      NewU->insertBefore(UI);
      UI->replaceUsesOfWith(U, NewU);
    }
    // We've replaced all the uses, so destroy the constant. (destroyConstant
    // will update value handles and metadata.)
    U->destroyConstant();
  }
}

/// Analyze the specified global variable and optimize
/// it if possible.  If we make a change, return true.
static bool processInternalGlobal(
    GlobalVariable *GV, const GlobalStatus &GS, TargetLibraryInfo *TLI,
    function_ref<DominatorTree &(Function &)> LookupDomTree) {
  auto &DL = GV->getParent()->getDataLayout();
  // If this is a first class global and has only one accessing function and
  // this function is non-recursive, we replace the global with a local alloca
  // in this function.
  //
  // NOTE: It doesn't make sense to promote non-single-value types since we
  // are just replacing static memory to stack memory.
  //
  // If the global is in different address space, don't bring it to stack.
  if (!GS.HasMultipleAccessingFunctions &&
      GS.AccessingFunction &&
      GV->getValueType()->isSingleValueType() &&
      GV->getType()->getAddressSpace() == 0 &&
      !GV->isExternallyInitialized() &&
      allNonInstructionUsersCanBeMadeInstructions(GV) &&
      GS.AccessingFunction->doesNotRecurse() &&
      isPointerValueDeadOnEntryToFunction(GS.AccessingFunction, GV,
                                          LookupDomTree)) {
    const DataLayout &DL = GV->getParent()->getDataLayout();

    LLVM_DEBUG(dbgs() << "LOCALIZING GLOBAL: " << *GV << "\n");
    Instruction &FirstI = const_cast<Instruction&>(*GS.AccessingFunction
                                                   ->getEntryBlock().begin());
    Type *ElemTy = GV->getValueType();
    // FIXME: Pass Global's alignment when globals have alignment
    AllocaInst *Alloca = new AllocaInst(ElemTy, DL.getAllocaAddrSpace(), nullptr,
                                        GV->getName(), &FirstI);
    if (!isa<UndefValue>(GV->getInitializer()))
      new StoreInst(GV->getInitializer(), Alloca, &FirstI);

    makeAllConstantUsesInstructions(GV);

    GV->replaceAllUsesWith(Alloca);
    GV->eraseFromParent();
    ++NumLocalized;
    return true;
  }

  // If the global is never loaded (but may be stored to), it is dead.
  // Delete it now.
  if (!GS.IsLoaded) {
    LLVM_DEBUG(dbgs() << "GLOBAL NEVER LOADED: " << *GV << "\n");

    bool Changed;
    if (isLeakCheckerRoot(GV)) {
      // Delete any constant stores to the global.
      Changed = CleanupPointerRootUsers(GV, TLI);
    } else {
      // Delete any stores we can find to the global.  We may not be able to
      // make it completely dead though.
      Changed = CleanupConstantGlobalUsers(GV, GV->getInitializer(), DL, TLI);
    }

    // If the global is dead now, delete it.
    if (GV->use_empty()) {
      GV->eraseFromParent();
      ++NumDeleted;
      Changed = true;
    }
    return Changed;

  }
  if (GS.StoredType <= GlobalStatus::InitializerStored) {
    LLVM_DEBUG(dbgs() << "MARKING CONSTANT: " << *GV << "\n");
    GV->setConstant(true);

    // Clean up any obviously simplifiable users now.
    CleanupConstantGlobalUsers(GV, GV->getInitializer(), DL, TLI);

    // If the global is dead now, just nuke it.
    if (GV->use_empty()) {
      LLVM_DEBUG(dbgs() << "   *** Marking constant allowed us to simplify "
                        << "all users and delete global!\n");
      GV->eraseFromParent();
      ++NumDeleted;
      return true;
    }

    // Fall through to the next check; see if we can optimize further.
    ++NumMarked;
  }
  if (!GV->getInitializer()->getType()->isSingleValueType()) {
    const DataLayout &DL = GV->getParent()->getDataLayout();
    if (SRAGlobal(GV, DL))
      return true;
  }
  if (GS.StoredType == GlobalStatus::StoredOnce && GS.StoredOnceValue) {
    // If the initial value for the global was an undef value, and if only
    // one other value was stored into it, we can just change the
    // initializer to be the stored value, then delete all stores to the
    // global.  This allows us to mark it constant.
    if (Constant *SOVConstant = dyn_cast<Constant>(GS.StoredOnceValue))
      if (isa<UndefValue>(GV->getInitializer())) {
        // Change the initial value here.
        GV->setInitializer(SOVConstant);

        // Clean up any obviously simplifiable users now.
        CleanupConstantGlobalUsers(GV, GV->getInitializer(), DL, TLI);

        if (GV->use_empty()) {
          LLVM_DEBUG(dbgs() << "   *** Substituting initializer allowed us to "
                            << "simplify all users and delete global!\n");
          GV->eraseFromParent();
          ++NumDeleted;
        }
        ++NumSubstitute;
        return true;
      }

    // Try to optimize globals based on the knowledge that only one value
    // (besides its initializer) is ever stored to the global.
    if (optimizeOnceStoredGlobal(GV, GS.StoredOnceValue, GS.Ordering, DL, TLI))
      return true;

    // Otherwise, if the global was not a boolean, we can shrink it to be a
    // boolean.
    if (Constant *SOVConstant = dyn_cast<Constant>(GS.StoredOnceValue)) {
      if (GS.Ordering == AtomicOrdering::NotAtomic) {
        if (TryToShrinkGlobalToBoolean(GV, SOVConstant)) {
          ++NumShrunkToBool;
          return true;
        }
      }
    }
  }

  return false;
}

/// Analyze the specified global variable and optimize it if possible.  If we
/// make a change, return true.
static bool
processGlobal(GlobalValue &GV, TargetLibraryInfo *TLI,
              function_ref<DominatorTree &(Function &)> LookupDomTree) {
  if (GV.getName().startswith("llvm."))
    return false;

  GlobalStatus GS;

  if (GlobalStatus::analyzeGlobal(&GV, GS))
    return false;

  bool Changed = false;
  if (!GS.IsCompared && !GV.hasGlobalUnnamedAddr()) {
    auto NewUnnamedAddr = GV.hasLocalLinkage() ? GlobalValue::UnnamedAddr::Global
                                               : GlobalValue::UnnamedAddr::Local;
    if (NewUnnamedAddr != GV.getUnnamedAddr()) {
      GV.setUnnamedAddr(NewUnnamedAddr);
      NumUnnamed++;
      Changed = true;
    }
  }

  // Do more involved optimizations if the global is internal.
  if (!GV.hasLocalLinkage())
    return Changed;

  auto *GVar = dyn_cast<GlobalVariable>(&GV);
  if (!GVar)
    return Changed;

  if (GVar->isConstant() || !GVar->hasInitializer())
    return Changed;

  return processInternalGlobal(GVar, GS, TLI, LookupDomTree) || Changed;
}

/// Walk all of the direct calls of the specified function, changing them to
/// FastCC.
static void ChangeCalleesToFastCall(Function *F) {
  for (User *U : F->users()) {
    if (isa<BlockAddress>(U))
      continue;
    CallSite CS(cast<Instruction>(U));
    CS.setCallingConv(CallingConv::Fast);
  }
}

static AttributeList StripNest(LLVMContext &C, AttributeList Attrs) {
  // There can be at most one attribute set with a nest attribute.
  unsigned NestIndex;
  if (Attrs.hasAttrSomewhere(Attribute::Nest, &NestIndex))
    return Attrs.removeAttribute(C, NestIndex, Attribute::Nest);
  return Attrs;
}

static void RemoveNestAttribute(Function *F) {
  F->setAttributes(StripNest(F->getContext(), F->getAttributes()));
  for (User *U : F->users()) {
    if (isa<BlockAddress>(U))
      continue;
    CallSite CS(cast<Instruction>(U));
    CS.setAttributes(StripNest(F->getContext(), CS.getAttributes()));
  }
}

/// Return true if this is a calling convention that we'd like to change.  The
/// idea here is that we don't want to mess with the convention if the user
/// explicitly requested something with performance implications like coldcc,
/// GHC, or anyregcc.
static bool hasChangeableCC(Function *F) {
  CallingConv::ID CC = F->getCallingConv();

  // FIXME: Is it worth transforming x86_stdcallcc and x86_fastcallcc?
  if (CC != CallingConv::C && CC != CallingConv::X86_ThisCall)
    return false;

  // Don't break the invariant that the inalloca parameter is the only parameter
  // passed in memory.
  // FIXME: GlobalOpt should remove inalloca when possible and hoist the dynamic
  // alloca it uses to the entry block if possible.
  if (F->getAttributes().hasAttrSomewhere(Attribute::InAlloca))
    return false;

  // FIXME: Change CC for the whole chain of musttail calls when possible.
  //
  // Can't change CC of the function that either has musttail calls, or is a
  // musttail callee itself
  for (User *U : F->users()) {
    if (isa<BlockAddress>(U))
      continue;
    CallInst* CI = dyn_cast<CallInst>(U);
    if (!CI)
      continue;

    if (CI->isMustTailCall())
      return false;
  }

  for (BasicBlock &BB : *F)
    if (BB.getTerminatingMustTailCall())
      return false;

  return true;
}

/// Return true if the block containing the call site has a BlockFrequency of
/// less than ColdCCRelFreq% of the entry block.
static bool isColdCallSite(CallSite CS, BlockFrequencyInfo &CallerBFI) {
  const BranchProbability ColdProb(ColdCCRelFreq, 100);
  auto CallSiteBB = CS.getInstruction()->getParent();
  auto CallSiteFreq = CallerBFI.getBlockFreq(CallSiteBB);
  auto CallerEntryFreq =
      CallerBFI.getBlockFreq(&(CS.getCaller()->getEntryBlock()));
  return CallSiteFreq < CallerEntryFreq * ColdProb;
}

// This function checks if the input function F is cold at all call sites. It
// also looks each call site's containing function, returning false if the
// caller function contains other non cold calls. The input vector AllCallsCold
// contains a list of functions that only have call sites in cold blocks.
static bool
isValidCandidateForColdCC(Function &F,
                          function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
                          const std::vector<Function *> &AllCallsCold) {

  if (F.user_empty())
    return false;

  for (User *U : F.users()) {
    if (isa<BlockAddress>(U))
      continue;

    CallSite CS(cast<Instruction>(U));
    Function *CallerFunc = CS.getInstruction()->getParent()->getParent();
    BlockFrequencyInfo &CallerBFI = GetBFI(*CallerFunc);
    if (!isColdCallSite(CS, CallerBFI))
      return false;
    auto It = std::find(AllCallsCold.begin(), AllCallsCold.end(), CallerFunc);
    if (It == AllCallsCold.end())
      return false;
  }
  return true;
}

static void changeCallSitesToColdCC(Function *F) {
  for (User *U : F->users()) {
    if (isa<BlockAddress>(U))
      continue;
    CallSite CS(cast<Instruction>(U));
    CS.setCallingConv(CallingConv::Cold);
  }
}

// This function iterates over all the call instructions in the input Function
// and checks that all call sites are in cold blocks and are allowed to use the
// coldcc calling convention.
static bool
hasOnlyColdCalls(Function &F,
                 function_ref<BlockFrequencyInfo &(Function &)> GetBFI) {
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        CallSite CS(cast<Instruction>(CI));
        // Skip over isline asm instructions since they aren't function calls.
        if (CI->isInlineAsm())
          continue;
        Function *CalledFn = CI->getCalledFunction();
        if (!CalledFn)
          return false;
        if (!CalledFn->hasLocalLinkage())
          return false;
        // Skip over instrinsics since they won't remain as function calls.
        if (CalledFn->getIntrinsicID() != Intrinsic::not_intrinsic)
          continue;
        // Check if it's valid to use coldcc calling convention.
        if (!hasChangeableCC(CalledFn) || CalledFn->isVarArg() ||
            CalledFn->hasAddressTaken())
          return false;
        BlockFrequencyInfo &CallerBFI = GetBFI(F);
        if (!isColdCallSite(CS, CallerBFI))
          return false;
      }
    }
  }
  return true;
}

static bool
OptimizeFunctions(Module &M, TargetLibraryInfo *TLI,
                  function_ref<TargetTransformInfo &(Function &)> GetTTI,
                  function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
                  function_ref<DominatorTree &(Function &)> LookupDomTree,
                  SmallPtrSetImpl<const Comdat *> &NotDiscardableComdats) {

  bool Changed = false;

  std::vector<Function *> AllCallsCold;
  for (Module::iterator FI = M.begin(), E = M.end(); FI != E;) {
    Function *F = &*FI++;
    if (hasOnlyColdCalls(*F, GetBFI))
      AllCallsCold.push_back(F);
  }

  // Optimize functions.
  for (Module::iterator FI = M.begin(), E = M.end(); FI != E; ) {
    Function *F = &*FI++;

    // Don't perform global opt pass on naked functions; we don't want fast
    // calling conventions for naked functions.
    if (F->hasFnAttribute(Attribute::Naked))
      continue;

    // Functions without names cannot be referenced outside this module.
    if (!F->hasName() && !F->isDeclaration() && !F->hasLocalLinkage())
      F->setLinkage(GlobalValue::InternalLinkage);

    if (deleteIfDead(*F, NotDiscardableComdats)) {
      Changed = true;
      continue;
    }

    // LLVM's definition of dominance allows instructions that are cyclic
    // in unreachable blocks, e.g.:
    // %pat = select i1 %condition, @global, i16* %pat
    // because any instruction dominates an instruction in a block that's
    // not reachable from entry.
    // So, remove unreachable blocks from the function, because a) there's
    // no point in analyzing them and b) GlobalOpt should otherwise grow
    // some more complicated logic to break these cycles.
    // Removing unreachable blocks might invalidate the dominator so we
    // recalculate it.
    if (!F->isDeclaration()) {
      if (removeUnreachableBlocks(*F)) {
        auto &DT = LookupDomTree(*F);
        DT.recalculate(*F);
        Changed = true;
      }
    }

    Changed |= processGlobal(*F, TLI, LookupDomTree);

    if (!F->hasLocalLinkage())
      continue;

    if (hasChangeableCC(F) && !F->isVarArg() && !F->hasAddressTaken()) {
      NumInternalFunc++;
      TargetTransformInfo &TTI = GetTTI(*F);
      // Change the calling convention to coldcc if either stress testing is
      // enabled or the target would like to use coldcc on functions which are
      // cold at all call sites and the callers contain no other non coldcc
      // calls.
      if (EnableColdCCStressTest ||
          (isValidCandidateForColdCC(*F, GetBFI, AllCallsCold) &&
           TTI.useColdCCForColdCall(*F))) {
        F->setCallingConv(CallingConv::Cold);
        changeCallSitesToColdCC(F);
        Changed = true;
        NumColdCC++;
      }
    }

    if (hasChangeableCC(F) && !F->isVarArg() &&
        !F->hasAddressTaken()) {
      // If this function has a calling convention worth changing, is not a
      // varargs function, and is only called directly, promote it to use the
      // Fast calling convention.
      F->setCallingConv(CallingConv::Fast);
      ChangeCalleesToFastCall(F);
      ++NumFastCallFns;
      Changed = true;
    }

    if (F->getAttributes().hasAttrSomewhere(Attribute::Nest) &&
        !F->hasAddressTaken()) {
      // The function is not used by a trampoline intrinsic, so it is safe
      // to remove the 'nest' attribute.
      RemoveNestAttribute(F);
      ++NumNestRemoved;
      Changed = true;
    }
  }
  return Changed;
}

static bool
OptimizeGlobalVars(Module &M, TargetLibraryInfo *TLI,
                   function_ref<DominatorTree &(Function &)> LookupDomTree,
                   SmallPtrSetImpl<const Comdat *> &NotDiscardableComdats) {
  bool Changed = false;

  for (Module::global_iterator GVI = M.global_begin(), E = M.global_end();
       GVI != E; ) {
    GlobalVariable *GV = &*GVI++;
    // Global variables without names cannot be referenced outside this module.
    if (!GV->hasName() && !GV->isDeclaration() && !GV->hasLocalLinkage())
      GV->setLinkage(GlobalValue::InternalLinkage);
    // Simplify the initializer.
    if (GV->hasInitializer())
      if (auto *C = dyn_cast<Constant>(GV->getInitializer())) {
        auto &DL = M.getDataLayout();
        Constant *New = ConstantFoldConstant(C, DL, TLI);
        if (New && New != C)
          GV->setInitializer(New);
      }

    if (deleteIfDead(*GV, NotDiscardableComdats)) {
      Changed = true;
      continue;
    }

    Changed |= processGlobal(*GV, TLI, LookupDomTree);
  }
  return Changed;
}

/// Evaluate a piece of a constantexpr store into a global initializer.  This
/// returns 'Init' modified to reflect 'Val' stored into it.  At this point, the
/// GEP operands of Addr [0, OpNo) have been stepped into.
static Constant *EvaluateStoreInto(Constant *Init, Constant *Val,
                                   ConstantExpr *Addr, unsigned OpNo) {
  // Base case of the recursion.
  if (OpNo == Addr->getNumOperands()) {
    assert(Val->getType() == Init->getType() && "Type mismatch!");
    return Val;
  }

  SmallVector<Constant*, 32> Elts;
  if (StructType *STy = dyn_cast<StructType>(Init->getType())) {
    // Break up the constant into its elements.
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i)
      Elts.push_back(Init->getAggregateElement(i));

    // Replace the element that we are supposed to.
    ConstantInt *CU = cast<ConstantInt>(Addr->getOperand(OpNo));
    unsigned Idx = CU->getZExtValue();
    assert(Idx < STy->getNumElements() && "Struct index out of range!");
    Elts[Idx] = EvaluateStoreInto(Elts[Idx], Val, Addr, OpNo+1);

    // Return the modified struct.
    return ConstantStruct::get(STy, Elts);
  }

  ConstantInt *CI = cast<ConstantInt>(Addr->getOperand(OpNo));
  SequentialType *InitTy = cast<SequentialType>(Init->getType());
  uint64_t NumElts = InitTy->getNumElements();

  // Break up the array into elements.
  for (uint64_t i = 0, e = NumElts; i != e; ++i)
    Elts.push_back(Init->getAggregateElement(i));

  assert(CI->getZExtValue() < NumElts);
  Elts[CI->getZExtValue()] =
    EvaluateStoreInto(Elts[CI->getZExtValue()], Val, Addr, OpNo+1);

  if (Init->getType()->isArrayTy())
    return ConstantArray::get(cast<ArrayType>(InitTy), Elts);
  return ConstantVector::get(Elts);
}

/// We have decided that Addr (which satisfies the predicate
/// isSimpleEnoughPointerToCommit) should get Val as its value.  Make it happen.
static void CommitValueTo(Constant *Val, Constant *Addr) {
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    assert(GV->hasInitializer());
    GV->setInitializer(Val);
    return;
  }

  ConstantExpr *CE = cast<ConstantExpr>(Addr);
  GlobalVariable *GV = cast<GlobalVariable>(CE->getOperand(0));
  GV->setInitializer(EvaluateStoreInto(GV->getInitializer(), Val, CE, 2));
}

/// Given a map of address -> value, where addresses are expected to be some form
/// of either a global or a constant GEP, set the initializer for the address to
/// be the value. This performs mostly the same function as CommitValueTo()
/// and EvaluateStoreInto() but is optimized to be more efficient for the common
/// case where the set of addresses are GEPs sharing the same underlying global,
/// processing the GEPs in batches rather than individually.
///
/// To give an example, consider the following C++ code adapted from the clang
/// regression tests:
/// struct S {
///  int n = 10;
///  int m = 2 * n;
///  S(int a) : n(a) {}
/// };
///
/// template<typename T>
/// struct U {
///  T *r = &q;
///  T q = 42;
///  U *p = this;
/// };
///
/// U<S> e;
///
/// The global static constructor for 'e' will need to initialize 'r' and 'p' of
/// the outer struct, while also initializing the inner 'q' structs 'n' and 'm'
/// members. This batch algorithm will simply use general CommitValueTo() method
/// to handle the complex nested S struct initialization of 'q', before
/// processing the outermost members in a single batch. Using CommitValueTo() to
/// handle member in the outer struct is inefficient when the struct/array is
/// very large as we end up creating and destroy constant arrays for each
/// initialization.
/// For the above case, we expect the following IR to be generated:
///
/// %struct.U = type { %struct.S*, %struct.S, %struct.U* }
/// %struct.S = type { i32, i32 }
/// @e = global %struct.U { %struct.S* gep inbounds (%struct.U, %struct.U* @e,
///                                                  i64 0, i32 1),
///                         %struct.S { i32 42, i32 84 }, %struct.U* @e }
/// The %struct.S { i32 42, i32 84 } inner initializer is treated as a complex
/// constant expression, while the other two elements of @e are "simple".
static void BatchCommitValueTo(const DenseMap<Constant*, Constant*> &Mem) {
  SmallVector<std::pair<GlobalVariable*, Constant*>, 32> GVs;
  SmallVector<std::pair<ConstantExpr*, Constant*>, 32> ComplexCEs;
  SmallVector<std::pair<ConstantExpr*, Constant*>, 32> SimpleCEs;
  SimpleCEs.reserve(Mem.size());

  for (const auto &I : Mem) {
    if (auto *GV = dyn_cast<GlobalVariable>(I.first)) {
      GVs.push_back(std::make_pair(GV, I.second));
    } else {
      ConstantExpr *GEP = cast<ConstantExpr>(I.first);
      // We don't handle the deeply recursive case using the batch method.
      if (GEP->getNumOperands() > 3)
        ComplexCEs.push_back(std::make_pair(GEP, I.second));
      else
        SimpleCEs.push_back(std::make_pair(GEP, I.second));
    }
  }

  // The algorithm below doesn't handle cases like nested structs, so use the
  // slower fully general method if we have to.
  for (auto ComplexCE : ComplexCEs)
    CommitValueTo(ComplexCE.second, ComplexCE.first);

  for (auto GVPair : GVs) {
    assert(GVPair.first->hasInitializer());
    GVPair.first->setInitializer(GVPair.second);
  }

  if (SimpleCEs.empty())
    return;

  // We cache a single global's initializer elements in the case where the
  // subsequent address/val pair uses the same one. This avoids throwing away and
  // rebuilding the constant struct/vector/array just because one element is
  // modified at a time.
  SmallVector<Constant *, 32> Elts;
  Elts.reserve(SimpleCEs.size());
  GlobalVariable *CurrentGV = nullptr;

  auto commitAndSetupCache = [&](GlobalVariable *GV, bool Update) {
    Constant *Init = GV->getInitializer();
    Type *Ty = Init->getType();
    if (Update) {
      if (CurrentGV) {
        assert(CurrentGV && "Expected a GV to commit to!");
        Type *CurrentInitTy = CurrentGV->getInitializer()->getType();
        // We have a valid cache that needs to be committed.
        if (StructType *STy = dyn_cast<StructType>(CurrentInitTy))
          CurrentGV->setInitializer(ConstantStruct::get(STy, Elts));
        else if (ArrayType *ArrTy = dyn_cast<ArrayType>(CurrentInitTy))
          CurrentGV->setInitializer(ConstantArray::get(ArrTy, Elts));
        else
          CurrentGV->setInitializer(ConstantVector::get(Elts));
      }
      if (CurrentGV == GV)
        return;
      // Need to clear and set up cache for new initializer.
      CurrentGV = GV;
      Elts.clear();
      unsigned NumElts;
      if (auto *STy = dyn_cast<StructType>(Ty))
        NumElts = STy->getNumElements();
      else
        NumElts = cast<SequentialType>(Ty)->getNumElements();
      for (unsigned i = 0, e = NumElts; i != e; ++i)
        Elts.push_back(Init->getAggregateElement(i));
    }
  };

  for (auto CEPair : SimpleCEs) {
    ConstantExpr *GEP = CEPair.first;
    Constant *Val = CEPair.second;

    GlobalVariable *GV = cast<GlobalVariable>(GEP->getOperand(0));
    commitAndSetupCache(GV, GV != CurrentGV);
    ConstantInt *CI = cast<ConstantInt>(GEP->getOperand(2));
    Elts[CI->getZExtValue()] = Val;
  }
  // The last initializer in the list needs to be committed, others
  // will be committed on a new initializer being processed.
  commitAndSetupCache(CurrentGV, true);
}

/// Evaluate static constructors in the function, if we can.  Return true if we
/// can, false otherwise.
static bool EvaluateStaticConstructor(Function *F, const DataLayout &DL,
                                      TargetLibraryInfo *TLI) {
  // Call the function.
  Evaluator Eval(DL, TLI);
  Constant *RetValDummy;
  bool EvalSuccess = Eval.EvaluateFunction(F, RetValDummy,
                                           SmallVector<Constant*, 0>());

  if (EvalSuccess) {
    ++NumCtorsEvaluated;

    // We succeeded at evaluation: commit the result.
    LLVM_DEBUG(dbgs() << "FULLY EVALUATED GLOBAL CTOR FUNCTION '"
                      << F->getName() << "' to "
                      << Eval.getMutatedMemory().size() << " stores.\n");
    BatchCommitValueTo(Eval.getMutatedMemory());
    for (GlobalVariable *GV : Eval.getInvariants())
      GV->setConstant(true);
  }

  return EvalSuccess;
}

static int compareNames(Constant *const *A, Constant *const *B) {
  Value *AStripped = (*A)->stripPointerCastsNoFollowAliases();
  Value *BStripped = (*B)->stripPointerCastsNoFollowAliases();
  return AStripped->getName().compare(BStripped->getName());
}

static void setUsedInitializer(GlobalVariable &V,
                               const SmallPtrSetImpl<GlobalValue *> &Init) {
  if (Init.empty()) {
    V.eraseFromParent();
    return;
  }

  // Type of pointer to the array of pointers.
  PointerType *Int8PtrTy = Type::getInt8PtrTy(V.getContext(), 0);

  SmallVector<Constant *, 8> UsedArray;
  for (GlobalValue *GV : Init) {
    Constant *Cast
      = ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, Int8PtrTy);
    UsedArray.push_back(Cast);
  }
  // Sort to get deterministic order.
  array_pod_sort(UsedArray.begin(), UsedArray.end(), compareNames);
  ArrayType *ATy = ArrayType::get(Int8PtrTy, UsedArray.size());

  Module *M = V.getParent();
  V.removeFromParent();
  GlobalVariable *NV =
      new GlobalVariable(*M, ATy, false, GlobalValue::AppendingLinkage,
                         ConstantArray::get(ATy, UsedArray), "");
  NV->takeName(&V);
  NV->setSection("llvm.metadata");
  delete &V;
}

namespace {

/// An easy to access representation of llvm.used and llvm.compiler.used.
class LLVMUsed {
  SmallPtrSet<GlobalValue *, 8> Used;
  SmallPtrSet<GlobalValue *, 8> CompilerUsed;
  GlobalVariable *UsedV;
  GlobalVariable *CompilerUsedV;

public:
  LLVMUsed(Module &M) {
    UsedV = collectUsedGlobalVariables(M, Used, false);
    CompilerUsedV = collectUsedGlobalVariables(M, CompilerUsed, true);
  }

  using iterator = SmallPtrSet<GlobalValue *, 8>::iterator;
  using used_iterator_range = iterator_range<iterator>;

  iterator usedBegin() { return Used.begin(); }
  iterator usedEnd() { return Used.end(); }

  used_iterator_range used() {
    return used_iterator_range(usedBegin(), usedEnd());
  }

  iterator compilerUsedBegin() { return CompilerUsed.begin(); }
  iterator compilerUsedEnd() { return CompilerUsed.end(); }

  used_iterator_range compilerUsed() {
    return used_iterator_range(compilerUsedBegin(), compilerUsedEnd());
  }

  bool usedCount(GlobalValue *GV) const { return Used.count(GV); }

  bool compilerUsedCount(GlobalValue *GV) const {
    return CompilerUsed.count(GV);
  }

  bool usedErase(GlobalValue *GV) { return Used.erase(GV); }
  bool compilerUsedErase(GlobalValue *GV) { return CompilerUsed.erase(GV); }
  bool usedInsert(GlobalValue *GV) { return Used.insert(GV).second; }

  bool compilerUsedInsert(GlobalValue *GV) {
    return CompilerUsed.insert(GV).second;
  }

  void syncVariablesAndSets() {
    if (UsedV)
      setUsedInitializer(*UsedV, Used);
    if (CompilerUsedV)
      setUsedInitializer(*CompilerUsedV, CompilerUsed);
  }
};

} // end anonymous namespace

static bool hasUseOtherThanLLVMUsed(GlobalAlias &GA, const LLVMUsed &U) {
  if (GA.use_empty()) // No use at all.
    return false;

  assert((!U.usedCount(&GA) || !U.compilerUsedCount(&GA)) &&
         "We should have removed the duplicated "
         "element from llvm.compiler.used");
  if (!GA.hasOneUse())
    // Strictly more than one use. So at least one is not in llvm.used and
    // llvm.compiler.used.
    return true;

  // Exactly one use. Check if it is in llvm.used or llvm.compiler.used.
  return !U.usedCount(&GA) && !U.compilerUsedCount(&GA);
}

static bool hasMoreThanOneUseOtherThanLLVMUsed(GlobalValue &V,
                                               const LLVMUsed &U) {
  unsigned N = 2;
  assert((!U.usedCount(&V) || !U.compilerUsedCount(&V)) &&
         "We should have removed the duplicated "
         "element from llvm.compiler.used");
  if (U.usedCount(&V) || U.compilerUsedCount(&V))
    ++N;
  return V.hasNUsesOrMore(N);
}

static bool mayHaveOtherReferences(GlobalAlias &GA, const LLVMUsed &U) {
  if (!GA.hasLocalLinkage())
    return true;

  return U.usedCount(&GA) || U.compilerUsedCount(&GA);
}

static bool hasUsesToReplace(GlobalAlias &GA, const LLVMUsed &U,
                             bool &RenameTarget) {
  RenameTarget = false;
  bool Ret = false;
  if (hasUseOtherThanLLVMUsed(GA, U))
    Ret = true;

  // If the alias is externally visible, we may still be able to simplify it.
  if (!mayHaveOtherReferences(GA, U))
    return Ret;

  // If the aliasee has internal linkage, give it the name and linkage
  // of the alias, and delete the alias.  This turns:
  //   define internal ... @f(...)
  //   @a = alias ... @f
  // into:
  //   define ... @a(...)
  Constant *Aliasee = GA.getAliasee();
  GlobalValue *Target = cast<GlobalValue>(Aliasee->stripPointerCasts());
  if (!Target->hasLocalLinkage())
    return Ret;

  // Do not perform the transform if multiple aliases potentially target the
  // aliasee. This check also ensures that it is safe to replace the section
  // and other attributes of the aliasee with those of the alias.
  if (hasMoreThanOneUseOtherThanLLVMUsed(*Target, U))
    return Ret;

  RenameTarget = true;
  return true;
}

static bool
OptimizeGlobalAliases(Module &M,
                      SmallPtrSetImpl<const Comdat *> &NotDiscardableComdats) {
  bool Changed = false;
  LLVMUsed Used(M);

  for (GlobalValue *GV : Used.used())
    Used.compilerUsedErase(GV);

  for (Module::alias_iterator I = M.alias_begin(), E = M.alias_end();
       I != E;) {
    GlobalAlias *J = &*I++;

    // Aliases without names cannot be referenced outside this module.
    if (!J->hasName() && !J->isDeclaration() && !J->hasLocalLinkage())
      J->setLinkage(GlobalValue::InternalLinkage);

    if (deleteIfDead(*J, NotDiscardableComdats)) {
      Changed = true;
      continue;
    }

    // If the alias can change at link time, nothing can be done - bail out.
    if (J->isInterposable())
      continue;

    Constant *Aliasee = J->getAliasee();
    GlobalValue *Target = dyn_cast<GlobalValue>(Aliasee->stripPointerCasts());
    // We can't trivially replace the alias with the aliasee if the aliasee is
    // non-trivial in some way.
    // TODO: Try to handle non-zero GEPs of local aliasees.
    if (!Target)
      continue;
    Target->removeDeadConstantUsers();

    // Make all users of the alias use the aliasee instead.
    bool RenameTarget;
    if (!hasUsesToReplace(*J, Used, RenameTarget))
      continue;

    J->replaceAllUsesWith(ConstantExpr::getBitCast(Aliasee, J->getType()));
    ++NumAliasesResolved;
    Changed = true;

    if (RenameTarget) {
      // Give the aliasee the name, linkage and other attributes of the alias.
      Target->takeName(&*J);
      Target->setLinkage(J->getLinkage());
      Target->setDSOLocal(J->isDSOLocal());
      Target->setVisibility(J->getVisibility());
      Target->setDLLStorageClass(J->getDLLStorageClass());

      if (Used.usedErase(&*J))
        Used.usedInsert(Target);

      if (Used.compilerUsedErase(&*J))
        Used.compilerUsedInsert(Target);
    } else if (mayHaveOtherReferences(*J, Used))
      continue;

    // Delete the alias.
    M.getAliasList().erase(J);
    ++NumAliasesRemoved;
    Changed = true;
  }

  Used.syncVariablesAndSets();

  return Changed;
}

static Function *FindCXAAtExit(Module &M, TargetLibraryInfo *TLI) {
  LibFunc F = LibFunc_cxa_atexit;
  if (!TLI->has(F))
    return nullptr;

  Function *Fn = M.getFunction(TLI->getName(F));
  if (!Fn)
    return nullptr;

  // Make sure that the function has the correct prototype.
  if (!TLI->getLibFunc(*Fn, F) || F != LibFunc_cxa_atexit)
    return nullptr;

  return Fn;
}

/// Returns whether the given function is an empty C++ destructor and can
/// therefore be eliminated.
/// Note that we assume that other optimization passes have already simplified
/// the code so we only look for a function with a single basic block, where
/// the only allowed instructions are 'ret', 'call' to an empty C++ dtor and
/// other side-effect free instructions.
static bool cxxDtorIsEmpty(const Function &Fn,
                           SmallPtrSet<const Function *, 8> &CalledFunctions) {
  // FIXME: We could eliminate C++ destructors if they're readonly/readnone and
  // nounwind, but that doesn't seem worth doing.
  if (Fn.isDeclaration())
    return false;

  if (++Fn.begin() != Fn.end())
    return false;

  const BasicBlock &EntryBlock = Fn.getEntryBlock();
  for (BasicBlock::const_iterator I = EntryBlock.begin(), E = EntryBlock.end();
       I != E; ++I) {
    if (const CallInst *CI = dyn_cast<CallInst>(I)) {
      // Ignore debug intrinsics.
      if (isa<DbgInfoIntrinsic>(CI))
        continue;

      const Function *CalledFn = CI->getCalledFunction();

      if (!CalledFn)
        return false;

      SmallPtrSet<const Function *, 8> NewCalledFunctions(CalledFunctions);

      // Don't treat recursive functions as empty.
      if (!NewCalledFunctions.insert(CalledFn).second)
        return false;

      if (!cxxDtorIsEmpty(*CalledFn, NewCalledFunctions))
        return false;
    } else if (isa<ReturnInst>(*I))
      return true; // We're done.
    else if (I->mayHaveSideEffects())
      return false; // Destructor with side effects, bail.
  }

  return false;
}

static bool OptimizeEmptyGlobalCXXDtors(Function *CXAAtExitFn) {
  /// Itanium C++ ABI p3.3.5:
  ///
  ///   After constructing a global (or local static) object, that will require
  ///   destruction on exit, a termination function is registered as follows:
  ///
  ///   extern "C" int __cxa_atexit ( void (*f)(void *), void *p, void *d );
  ///
  ///   This registration, e.g. __cxa_atexit(f,p,d), is intended to cause the
  ///   call f(p) when DSO d is unloaded, before all such termination calls
  ///   registered before this one. It returns zero if registration is
  ///   successful, nonzero on failure.

  // This pass will look for calls to __cxa_atexit where the function is trivial
  // and remove them.
  bool Changed = false;

  for (auto I = CXAAtExitFn->user_begin(), E = CXAAtExitFn->user_end();
       I != E;) {
    // We're only interested in calls. Theoretically, we could handle invoke
    // instructions as well, but neither llvm-gcc nor clang generate invokes
    // to __cxa_atexit.
    CallInst *CI = dyn_cast<CallInst>(*I++);
    if (!CI)
      continue;

    Function *DtorFn =
      dyn_cast<Function>(CI->getArgOperand(0)->stripPointerCasts());
    if (!DtorFn)
      continue;

    SmallPtrSet<const Function *, 8> CalledFunctions;
    if (!cxxDtorIsEmpty(*DtorFn, CalledFunctions))
      continue;

    // Just remove the call.
    CI->replaceAllUsesWith(Constant::getNullValue(CI->getType()));
    CI->eraseFromParent();

    ++NumCXXDtorsRemoved;

    Changed |= true;
  }

  return Changed;
}

static bool optimizeGlobalsInModule(
    Module &M, const DataLayout &DL, TargetLibraryInfo *TLI,
    function_ref<TargetTransformInfo &(Function &)> GetTTI,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI,
    function_ref<DominatorTree &(Function &)> LookupDomTree) {
  SmallPtrSet<const Comdat *, 8> NotDiscardableComdats;
  bool Changed = false;
  bool LocalChange = true;
  while (LocalChange) {
    LocalChange = false;

    NotDiscardableComdats.clear();
    for (const GlobalVariable &GV : M.globals())
      if (const Comdat *C = GV.getComdat())
        if (!GV.isDiscardableIfUnused() || !GV.use_empty())
          NotDiscardableComdats.insert(C);
    for (Function &F : M)
      if (const Comdat *C = F.getComdat())
        if (!F.isDefTriviallyDead())
          NotDiscardableComdats.insert(C);
    for (GlobalAlias &GA : M.aliases())
      if (const Comdat *C = GA.getComdat())
        if (!GA.isDiscardableIfUnused() || !GA.use_empty())
          NotDiscardableComdats.insert(C);

    // Delete functions that are trivially dead, ccc -> fastcc
    LocalChange |= OptimizeFunctions(M, TLI, GetTTI, GetBFI, LookupDomTree,
                                     NotDiscardableComdats);

    // Optimize global_ctors list.
    LocalChange |= optimizeGlobalCtorsList(M, [&](Function *F) {
      return EvaluateStaticConstructor(F, DL, TLI);
    });

    // Optimize non-address-taken globals.
    LocalChange |= OptimizeGlobalVars(M, TLI, LookupDomTree,
                                      NotDiscardableComdats);

    // Resolve aliases, when possible.
    LocalChange |= OptimizeGlobalAliases(M, NotDiscardableComdats);

    // Try to remove trivial global destructors if they are not removed
    // already.
    Function *CXAAtExitFn = FindCXAAtExit(M, TLI);
    if (CXAAtExitFn)
      LocalChange |= OptimizeEmptyGlobalCXXDtors(CXAAtExitFn);

    Changed |= LocalChange;
  }

  // TODO: Move all global ctors functions to the end of the module for code
  // layout.

  return Changed;
}

PreservedAnalyses GlobalOptPass::run(Module &M, ModuleAnalysisManager &AM) {
    auto &DL = M.getDataLayout();
    auto &TLI = AM.getResult<TargetLibraryAnalysis>(M);
    auto &FAM =
        AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    auto LookupDomTree = [&FAM](Function &F) -> DominatorTree &{
      return FAM.getResult<DominatorTreeAnalysis>(F);
    };
    auto GetTTI = [&FAM](Function &F) -> TargetTransformInfo & {
      return FAM.getResult<TargetIRAnalysis>(F);
    };

    auto GetBFI = [&FAM](Function &F) -> BlockFrequencyInfo & {
      return FAM.getResult<BlockFrequencyAnalysis>(F);
    };

    if (!optimizeGlobalsInModule(M, DL, &TLI, GetTTI, GetBFI, LookupDomTree))
      return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}

namespace {

struct GlobalOptLegacyPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid

  GlobalOptLegacyPass() : ModulePass(ID) {
    initializeGlobalOptLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;

    auto &DL = M.getDataLayout();
    auto *TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
    auto LookupDomTree = [this](Function &F) -> DominatorTree & {
      return this->getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    };
    auto GetTTI = [this](Function &F) -> TargetTransformInfo & {
      return this->getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    };

    auto GetBFI = [this](Function &F) -> BlockFrequencyInfo & {
      return this->getAnalysis<BlockFrequencyInfoWrapperPass>(F).getBFI();
    };

    return optimizeGlobalsInModule(M, DL, TLI, GetTTI, GetBFI, LookupDomTree);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
  }
};

} // end anonymous namespace

char GlobalOptLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(GlobalOptLegacyPass, "globalopt",
                      "Global Variable Optimizer", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(GlobalOptLegacyPass, "globalopt",
                    "Global Variable Optimizer", false, false)

ModulePass *llvm::createGlobalOptimizerPass() {
  return new GlobalOptLegacyPass();
}
