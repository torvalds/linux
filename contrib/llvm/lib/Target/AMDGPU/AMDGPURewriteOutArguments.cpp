//===- AMDGPURewriteOutArgumentsPass.cpp - Create struct returns ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This pass attempts to replace out argument usage with a return of a
/// struct.
///
/// We can support returning a lot of values directly in registers, but
/// idiomatic C code frequently uses a pointer argument to return a second value
/// rather than returning a struct by value. GPU stack access is also quite
/// painful, so we want to avoid that if possible. Passing a stack object
/// pointer to a function also requires an additional address expansion code
/// sequence to convert the pointer to be relative to the kernel's scratch wave
/// offset register since the callee doesn't know what stack frame the incoming
/// pointer is relative to.
///
/// The goal is to try rewriting code that looks like this:
///
///  int foo(int a, int b, int* out) {
///     *out = bar();
///     return a + b;
/// }
///
/// into something like this:
///
///  std::pair<int, int> foo(int a, int b) {
///     return std::make_pair(a + b, bar());
/// }
///
/// Typically the incoming pointer is a simple alloca for a temporary variable
/// to use the API, which if replaced with a struct return will be easily SROA'd
/// out when the stub function we create is inlined
///
/// This pass introduces the struct return, but leaves the unused pointer
/// arguments and introduces a new stub function calling the struct returning
/// body. DeadArgumentElimination should be run after this to clean these up.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

#define DEBUG_TYPE "amdgpu-rewrite-out-arguments"

using namespace llvm;

static cl::opt<bool> AnyAddressSpace(
  "amdgpu-any-address-space-out-arguments",
  cl::desc("Replace pointer out arguments with "
           "struct returns for non-private address space"),
  cl::Hidden,
  cl::init(false));

static cl::opt<unsigned> MaxNumRetRegs(
  "amdgpu-max-return-arg-num-regs",
  cl::desc("Approximately limit number of return registers for replacing out arguments"),
  cl::Hidden,
  cl::init(16));

STATISTIC(NumOutArgumentsReplaced,
          "Number out arguments moved to struct return values");
STATISTIC(NumOutArgumentFunctionsReplaced,
          "Number of functions with out arguments moved to struct return values");

namespace {

class AMDGPURewriteOutArguments : public FunctionPass {
private:
  const DataLayout *DL = nullptr;
  MemoryDependenceResults *MDA = nullptr;

  bool checkArgumentUses(Value &Arg) const;
  bool isOutArgumentCandidate(Argument &Arg) const;

#ifndef NDEBUG
  bool isVec3ToVec4Shuffle(Type *Ty0, Type* Ty1) const;
#endif

public:
  static char ID;

  AMDGPURewriteOutArguments() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MemoryDependenceWrapperPass>();
    FunctionPass::getAnalysisUsage(AU);
  }

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;
};

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(AMDGPURewriteOutArguments, DEBUG_TYPE,
                      "AMDGPU Rewrite Out Arguments", false, false)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_END(AMDGPURewriteOutArguments, DEBUG_TYPE,
                    "AMDGPU Rewrite Out Arguments", false, false)

char AMDGPURewriteOutArguments::ID = 0;

bool AMDGPURewriteOutArguments::checkArgumentUses(Value &Arg) const {
  const int MaxUses = 10;
  int UseCount = 0;

  for (Use &U : Arg.uses()) {
    StoreInst *SI = dyn_cast<StoreInst>(U.getUser());
    if (UseCount > MaxUses)
      return false;

    if (!SI) {
      auto *BCI = dyn_cast<BitCastInst>(U.getUser());
      if (!BCI || !BCI->hasOneUse())
        return false;

      // We don't handle multiple stores currently, so stores to aggregate
      // pointers aren't worth the trouble since they are canonically split up.
      Type *DestEltTy = BCI->getType()->getPointerElementType();
      if (DestEltTy->isAggregateType())
        return false;

      // We could handle these if we had a convenient way to bitcast between
      // them.
      Type *SrcEltTy = Arg.getType()->getPointerElementType();
      if (SrcEltTy->isArrayTy())
        return false;

      // Special case handle structs with single members. It is useful to handle
      // some casts between structs and non-structs, but we can't bitcast
      // directly between them.  directly bitcast between them.  Blender uses
      // some casts that look like { <3 x float> }* to <4 x float>*
      if ((SrcEltTy->isStructTy() && (SrcEltTy->getStructNumElements() != 1)))
        return false;

      // Clang emits OpenCL 3-vector type accesses with a bitcast to the
      // equivalent 4-element vector and accesses that, and we're looking for
      // this pointer cast.
      if (DL->getTypeAllocSize(SrcEltTy) != DL->getTypeAllocSize(DestEltTy))
        return false;

      return checkArgumentUses(*BCI);
    }

    if (!SI->isSimple() ||
        U.getOperandNo() != StoreInst::getPointerOperandIndex())
      return false;

    ++UseCount;
  }

  // Skip unused arguments.
  return UseCount > 0;
}

bool AMDGPURewriteOutArguments::isOutArgumentCandidate(Argument &Arg) const {
  const unsigned MaxOutArgSizeBytes = 4 * MaxNumRetRegs;
  PointerType *ArgTy = dyn_cast<PointerType>(Arg.getType());

  // TODO: It might be useful for any out arguments, not just privates.
  if (!ArgTy || (ArgTy->getAddressSpace() != DL->getAllocaAddrSpace() &&
                 !AnyAddressSpace) ||
      Arg.hasByValAttr() || Arg.hasStructRetAttr() ||
      DL->getTypeStoreSize(ArgTy->getPointerElementType()) > MaxOutArgSizeBytes) {
    return false;
  }

  return checkArgumentUses(Arg);
}

bool AMDGPURewriteOutArguments::doInitialization(Module &M) {
  DL = &M.getDataLayout();
  return false;
}

#ifndef NDEBUG
bool AMDGPURewriteOutArguments::isVec3ToVec4Shuffle(Type *Ty0, Type* Ty1) const {
  VectorType *VT0 = dyn_cast<VectorType>(Ty0);
  VectorType *VT1 = dyn_cast<VectorType>(Ty1);
  if (!VT0 || !VT1)
    return false;

  if (VT0->getNumElements() != 3 ||
      VT1->getNumElements() != 4)
    return false;

  return DL->getTypeSizeInBits(VT0->getElementType()) ==
         DL->getTypeSizeInBits(VT1->getElementType());
}
#endif

bool AMDGPURewriteOutArguments::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  // TODO: Could probably handle variadic functions.
  if (F.isVarArg() || F.hasStructRetAttr() ||
      AMDGPU::isEntryFunctionCC(F.getCallingConv()))
    return false;

  MDA = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();

  unsigned ReturnNumRegs = 0;
  SmallSet<int, 4> OutArgIndexes;
  SmallVector<Type *, 4> ReturnTypes;
  Type *RetTy = F.getReturnType();
  if (!RetTy->isVoidTy()) {
    ReturnNumRegs = DL->getTypeStoreSize(RetTy) / 4;

    if (ReturnNumRegs >= MaxNumRetRegs)
      return false;

    ReturnTypes.push_back(RetTy);
  }

  SmallVector<Argument *, 4> OutArgs;
  for (Argument &Arg : F.args()) {
    if (isOutArgumentCandidate(Arg)) {
      LLVM_DEBUG(dbgs() << "Found possible out argument " << Arg
                        << " in function " << F.getName() << '\n');
      OutArgs.push_back(&Arg);
    }
  }

  if (OutArgs.empty())
    return false;

  using ReplacementVec = SmallVector<std::pair<Argument *, Value *>, 4>;

  DenseMap<ReturnInst *, ReplacementVec> Replacements;

  SmallVector<ReturnInst *, 4> Returns;
  for (BasicBlock &BB : F) {
    if (ReturnInst *RI = dyn_cast<ReturnInst>(&BB.back()))
      Returns.push_back(RI);
  }

  if (Returns.empty())
    return false;

  bool Changing;

  do {
    Changing = false;

    // Keep retrying if we are able to successfully eliminate an argument. This
    // helps with cases with multiple arguments which may alias, such as in a
    // sincos implemntation. If we have 2 stores to arguments, on the first
    // attempt the MDA query will succeed for the second store but not the
    // first. On the second iteration we've removed that out clobbering argument
    // (by effectively moving it into another function) and will find the second
    // argument is OK to move.
    for (Argument *OutArg : OutArgs) {
      bool ThisReplaceable = true;
      SmallVector<std::pair<ReturnInst *, StoreInst *>, 4> ReplaceableStores;

      Type *ArgTy = OutArg->getType()->getPointerElementType();

      // Skip this argument if converting it will push us over the register
      // count to return limit.

      // TODO: This is an approximation. When legalized this could be more. We
      // can ask TLI for exactly how many.
      unsigned ArgNumRegs = DL->getTypeStoreSize(ArgTy) / 4;
      if (ArgNumRegs + ReturnNumRegs > MaxNumRetRegs)
        continue;

      // An argument is convertible only if all exit blocks are able to replace
      // it.
      for (ReturnInst *RI : Returns) {
        BasicBlock *BB = RI->getParent();

        MemDepResult Q = MDA->getPointerDependencyFrom(MemoryLocation(OutArg),
                                                       true, BB->end(), BB, RI);
        StoreInst *SI = nullptr;
        if (Q.isDef())
          SI = dyn_cast<StoreInst>(Q.getInst());

        if (SI) {
          LLVM_DEBUG(dbgs() << "Found out argument store: " << *SI << '\n');
          ReplaceableStores.emplace_back(RI, SI);
        } else {
          ThisReplaceable = false;
          break;
        }
      }

      if (!ThisReplaceable)
        continue; // Try the next argument candidate.

      for (std::pair<ReturnInst *, StoreInst *> Store : ReplaceableStores) {
        Value *ReplVal = Store.second->getValueOperand();

        auto &ValVec = Replacements[Store.first];
        if (llvm::find_if(ValVec,
              [OutArg](const std::pair<Argument *, Value *> &Entry) {
                 return Entry.first == OutArg;}) != ValVec.end()) {
          LLVM_DEBUG(dbgs()
                     << "Saw multiple out arg stores" << *OutArg << '\n');
          // It is possible to see stores to the same argument multiple times,
          // but we expect these would have been optimized out already.
          ThisReplaceable = false;
          break;
        }

        ValVec.emplace_back(OutArg, ReplVal);
        Store.second->eraseFromParent();
      }

      if (ThisReplaceable) {
        ReturnTypes.push_back(ArgTy);
        OutArgIndexes.insert(OutArg->getArgNo());
        ++NumOutArgumentsReplaced;
        Changing = true;
      }
    }
  } while (Changing);

  if (Replacements.empty())
    return false;

  LLVMContext &Ctx = F.getParent()->getContext();
  StructType *NewRetTy = StructType::create(Ctx, ReturnTypes, F.getName());

  FunctionType *NewFuncTy = FunctionType::get(NewRetTy,
                                              F.getFunctionType()->params(),
                                              F.isVarArg());

  LLVM_DEBUG(dbgs() << "Computed new return type: " << *NewRetTy << '\n');

  Function *NewFunc = Function::Create(NewFuncTy, Function::PrivateLinkage,
                                       F.getName() + ".body");
  F.getParent()->getFunctionList().insert(F.getIterator(), NewFunc);
  NewFunc->copyAttributesFrom(&F);
  NewFunc->setComdat(F.getComdat());

  // We want to preserve the function and param attributes, but need to strip
  // off any return attributes, e.g. zeroext doesn't make sense with a struct.
  NewFunc->stealArgumentListFrom(F);

  AttrBuilder RetAttrs;
  RetAttrs.addAttribute(Attribute::SExt);
  RetAttrs.addAttribute(Attribute::ZExt);
  RetAttrs.addAttribute(Attribute::NoAlias);
  NewFunc->removeAttributes(AttributeList::ReturnIndex, RetAttrs);
  // TODO: How to preserve metadata?

  // Move the body of the function into the new rewritten function, and replace
  // this function with a stub.
  NewFunc->getBasicBlockList().splice(NewFunc->begin(), F.getBasicBlockList());

  for (std::pair<ReturnInst *, ReplacementVec> &Replacement : Replacements) {
    ReturnInst *RI = Replacement.first;
    IRBuilder<> B(RI);
    B.SetCurrentDebugLocation(RI->getDebugLoc());

    int RetIdx = 0;
    Value *NewRetVal = UndefValue::get(NewRetTy);

    Value *RetVal = RI->getReturnValue();
    if (RetVal)
      NewRetVal = B.CreateInsertValue(NewRetVal, RetVal, RetIdx++);

    for (std::pair<Argument *, Value *> ReturnPoint : Replacement.second) {
      Argument *Arg = ReturnPoint.first;
      Value *Val = ReturnPoint.second;
      Type *EltTy = Arg->getType()->getPointerElementType();
      if (Val->getType() != EltTy) {
        Type *EffectiveEltTy = EltTy;
        if (StructType *CT = dyn_cast<StructType>(EltTy)) {
          assert(CT->getNumElements() == 1);
          EffectiveEltTy = CT->getElementType(0);
        }

        if (DL->getTypeSizeInBits(EffectiveEltTy) !=
            DL->getTypeSizeInBits(Val->getType())) {
          assert(isVec3ToVec4Shuffle(EffectiveEltTy, Val->getType()));
          Val = B.CreateShuffleVector(Val, UndefValue::get(Val->getType()),
                                      { 0, 1, 2 });
        }

        Val = B.CreateBitCast(Val, EffectiveEltTy);

        // Re-create single element composite.
        if (EltTy != EffectiveEltTy)
          Val = B.CreateInsertValue(UndefValue::get(EltTy), Val, 0);
      }

      NewRetVal = B.CreateInsertValue(NewRetVal, Val, RetIdx++);
    }

    if (RetVal)
      RI->setOperand(0, NewRetVal);
    else {
      B.CreateRet(NewRetVal);
      RI->eraseFromParent();
    }
  }

  SmallVector<Value *, 16> StubCallArgs;
  for (Argument &Arg : F.args()) {
    if (OutArgIndexes.count(Arg.getArgNo())) {
      // It's easier to preserve the type of the argument list. We rely on
      // DeadArgumentElimination to take care of these.
      StubCallArgs.push_back(UndefValue::get(Arg.getType()));
    } else {
      StubCallArgs.push_back(&Arg);
    }
  }

  BasicBlock *StubBB = BasicBlock::Create(Ctx, "", &F);
  IRBuilder<> B(StubBB);
  CallInst *StubCall = B.CreateCall(NewFunc, StubCallArgs);

  int RetIdx = RetTy->isVoidTy() ? 0 : 1;
  for (Argument &Arg : F.args()) {
    if (!OutArgIndexes.count(Arg.getArgNo()))
      continue;

    PointerType *ArgType = cast<PointerType>(Arg.getType());

    auto *EltTy = ArgType->getElementType();
    unsigned Align = Arg.getParamAlignment();
    if (Align == 0)
      Align = DL->getABITypeAlignment(EltTy);

    Value *Val = B.CreateExtractValue(StubCall, RetIdx++);
    Type *PtrTy = Val->getType()->getPointerTo(ArgType->getAddressSpace());

    // We can peek through bitcasts, so the type may not match.
    Value *PtrVal = B.CreateBitCast(&Arg, PtrTy);

    B.CreateAlignedStore(Val, PtrVal, Align);
  }

  if (!RetTy->isVoidTy()) {
    B.CreateRet(B.CreateExtractValue(StubCall, 0));
  } else {
    B.CreateRetVoid();
  }

  // The function is now a stub we want to inline.
  F.addFnAttr(Attribute::AlwaysInline);

  ++NumOutArgumentFunctionsReplaced;
  return true;
}

FunctionPass *llvm::createAMDGPURewriteOutArgumentsPass() {
  return new AMDGPURewriteOutArguments();
}
