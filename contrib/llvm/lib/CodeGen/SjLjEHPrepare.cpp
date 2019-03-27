//===- SjLjEHPrepare.cpp - Eliminate Invoke & Unwind instructions ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation is designed for use by code generators which use SjLj
// based exception handling.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "sjljehprepare"

STATISTIC(NumInvokes, "Number of invokes replaced");
STATISTIC(NumSpilled, "Number of registers live across unwind edges");

namespace {
class SjLjEHPrepare : public FunctionPass {
  Type *doubleUnderDataTy;
  Type *doubleUnderJBufTy;
  Type *FunctionContextTy;
  Constant *RegisterFn;
  Constant *UnregisterFn;
  Constant *BuiltinSetupDispatchFn;
  Constant *FrameAddrFn;
  Constant *StackAddrFn;
  Constant *StackRestoreFn;
  Constant *LSDAAddrFn;
  Constant *CallSiteFn;
  Constant *FuncCtxFn;
  AllocaInst *FuncCtx;

public:
  static char ID; // Pass identification, replacement for typeid
  explicit SjLjEHPrepare() : FunctionPass(ID) {}
  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {}
  StringRef getPassName() const override {
    return "SJLJ Exception Handling preparation";
  }

private:
  bool setupEntryBlockAndCallSites(Function &F);
  void substituteLPadValues(LandingPadInst *LPI, Value *ExnVal, Value *SelVal);
  Value *setupFunctionContext(Function &F, ArrayRef<LandingPadInst *> LPads);
  void lowerIncomingArguments(Function &F);
  void lowerAcrossUnwindEdges(Function &F, ArrayRef<InvokeInst *> Invokes);
  void insertCallSiteStore(Instruction *I, int Number);
};
} // end anonymous namespace

char SjLjEHPrepare::ID = 0;
INITIALIZE_PASS(SjLjEHPrepare, DEBUG_TYPE, "Prepare SjLj exceptions",
                false, false)

// Public Interface To the SjLjEHPrepare pass.
FunctionPass *llvm::createSjLjEHPreparePass() { return new SjLjEHPrepare(); }
// doInitialization - Set up decalarations and types needed to process
// exceptions.
bool SjLjEHPrepare::doInitialization(Module &M) {
  // Build the function context structure.
  // builtin_setjmp uses a five word jbuf
  Type *VoidPtrTy = Type::getInt8PtrTy(M.getContext());
  Type *Int32Ty = Type::getInt32Ty(M.getContext());
  doubleUnderDataTy = ArrayType::get(Int32Ty, 4);
  doubleUnderJBufTy = ArrayType::get(VoidPtrTy, 5);
  FunctionContextTy = StructType::get(VoidPtrTy,         // __prev
                                      Int32Ty,           // call_site
                                      doubleUnderDataTy, // __data
                                      VoidPtrTy,         // __personality
                                      VoidPtrTy,         // __lsda
                                      doubleUnderJBufTy  // __jbuf
                                      );

  return true;
}

/// insertCallSiteStore - Insert a store of the call-site value to the
/// function context
void SjLjEHPrepare::insertCallSiteStore(Instruction *I, int Number) {
  IRBuilder<> Builder(I);

  // Get a reference to the call_site field.
  Type *Int32Ty = Type::getInt32Ty(I->getContext());
  Value *Zero = ConstantInt::get(Int32Ty, 0);
  Value *One = ConstantInt::get(Int32Ty, 1);
  Value *Idxs[2] = { Zero, One };
  Value *CallSite =
      Builder.CreateGEP(FunctionContextTy, FuncCtx, Idxs, "call_site");

  // Insert a store of the call-site number
  ConstantInt *CallSiteNoC =
      ConstantInt::get(Type::getInt32Ty(I->getContext()), Number);
  Builder.CreateStore(CallSiteNoC, CallSite, true /*volatile*/);
}

/// MarkBlocksLiveIn - Insert BB and all of its predecessors into LiveBBs until
/// we reach blocks we've already seen.
static void MarkBlocksLiveIn(BasicBlock *BB,
                             SmallPtrSetImpl<BasicBlock *> &LiveBBs) {
  if (!LiveBBs.insert(BB).second)
    return; // already been here.

  df_iterator_default_set<BasicBlock*> Visited;

  for (BasicBlock *B : inverse_depth_first_ext(BB, Visited))
    LiveBBs.insert(B);

}

/// substituteLPadValues - Substitute the values returned by the landingpad
/// instruction with those returned by the personality function.
void SjLjEHPrepare::substituteLPadValues(LandingPadInst *LPI, Value *ExnVal,
                                         Value *SelVal) {
  SmallVector<Value *, 8> UseWorkList(LPI->user_begin(), LPI->user_end());
  while (!UseWorkList.empty()) {
    Value *Val = UseWorkList.pop_back_val();
    auto *EVI = dyn_cast<ExtractValueInst>(Val);
    if (!EVI)
      continue;
    if (EVI->getNumIndices() != 1)
      continue;
    if (*EVI->idx_begin() == 0)
      EVI->replaceAllUsesWith(ExnVal);
    else if (*EVI->idx_begin() == 1)
      EVI->replaceAllUsesWith(SelVal);
    if (EVI->use_empty())
      EVI->eraseFromParent();
  }

  if (LPI->use_empty())
    return;

  // There are still some uses of LPI. Construct an aggregate with the exception
  // values and replace the LPI with that aggregate.
  Type *LPadType = LPI->getType();
  Value *LPadVal = UndefValue::get(LPadType);
  auto *SelI = cast<Instruction>(SelVal);
  IRBuilder<> Builder(SelI->getParent(), std::next(SelI->getIterator()));
  LPadVal = Builder.CreateInsertValue(LPadVal, ExnVal, 0, "lpad.val");
  LPadVal = Builder.CreateInsertValue(LPadVal, SelVal, 1, "lpad.val");

  LPI->replaceAllUsesWith(LPadVal);
}

/// setupFunctionContext - Allocate the function context on the stack and fill
/// it with all of the data that we know at this point.
Value *SjLjEHPrepare::setupFunctionContext(Function &F,
                                           ArrayRef<LandingPadInst *> LPads) {
  BasicBlock *EntryBB = &F.front();

  // Create an alloca for the incoming jump buffer ptr and the new jump buffer
  // that needs to be restored on all exits from the function. This is an alloca
  // because the value needs to be added to the global context list.
  auto &DL = F.getParent()->getDataLayout();
  unsigned Align = DL.getPrefTypeAlignment(FunctionContextTy);
  FuncCtx = new AllocaInst(FunctionContextTy, DL.getAllocaAddrSpace(),
                           nullptr, Align, "fn_context", &EntryBB->front());

  // Fill in the function context structure.
  for (LandingPadInst *LPI : LPads) {
    IRBuilder<> Builder(LPI->getParent(),
                        LPI->getParent()->getFirstInsertionPt());

    // Reference the __data field.
    Value *FCData =
        Builder.CreateConstGEP2_32(FunctionContextTy, FuncCtx, 0, 2, "__data");

    // The exception values come back in context->__data[0].
    Value *ExceptionAddr = Builder.CreateConstGEP2_32(doubleUnderDataTy, FCData,
                                                      0, 0, "exception_gep");
    Value *ExnVal = Builder.CreateLoad(ExceptionAddr, true, "exn_val");
    ExnVal = Builder.CreateIntToPtr(ExnVal, Builder.getInt8PtrTy());

    Value *SelectorAddr = Builder.CreateConstGEP2_32(doubleUnderDataTy, FCData,
                                                     0, 1, "exn_selector_gep");
    Value *SelVal = Builder.CreateLoad(SelectorAddr, true, "exn_selector_val");

    substituteLPadValues(LPI, ExnVal, SelVal);
  }

  // Personality function
  IRBuilder<> Builder(EntryBB->getTerminator());
  Value *PersonalityFn = F.getPersonalityFn();
  Value *PersonalityFieldPtr = Builder.CreateConstGEP2_32(
      FunctionContextTy, FuncCtx, 0, 3, "pers_fn_gep");
  Builder.CreateStore(
      Builder.CreateBitCast(PersonalityFn, Builder.getInt8PtrTy()),
      PersonalityFieldPtr, /*isVolatile=*/true);

  // LSDA address
  Value *LSDA = Builder.CreateCall(LSDAAddrFn, {}, "lsda_addr");
  Value *LSDAFieldPtr =
      Builder.CreateConstGEP2_32(FunctionContextTy, FuncCtx, 0, 4, "lsda_gep");
  Builder.CreateStore(LSDA, LSDAFieldPtr, /*isVolatile=*/true);

  return FuncCtx;
}

/// lowerIncomingArguments - To avoid having to handle incoming arguments
/// specially, we lower each arg to a copy instruction in the entry block. This
/// ensures that the argument value itself cannot be live out of the entry
/// block.
void SjLjEHPrepare::lowerIncomingArguments(Function &F) {
  BasicBlock::iterator AfterAllocaInsPt = F.begin()->begin();
  while (isa<AllocaInst>(AfterAllocaInsPt) &&
         cast<AllocaInst>(AfterAllocaInsPt)->isStaticAlloca())
    ++AfterAllocaInsPt;
  assert(AfterAllocaInsPt != F.front().end());

  for (auto &AI : F.args()) {
    // Swift error really is a register that we model as memory -- instruction
    // selection will perform mem-to-reg for us and spill/reload appropriately
    // around calls that clobber it. There is no need to spill this
    // value to the stack and doing so would not be allowed.
    if (AI.isSwiftError())
      continue;

    Type *Ty = AI.getType();

    // Use 'select i8 true, %arg, undef' to simulate a 'no-op' instruction.
    Value *TrueValue = ConstantInt::getTrue(F.getContext());
    Value *UndefValue = UndefValue::get(Ty);
    Instruction *SI = SelectInst::Create(
        TrueValue, &AI, UndefValue, AI.getName() + ".tmp", &*AfterAllocaInsPt);
    AI.replaceAllUsesWith(SI);

    // Reset the operand, because it  was clobbered by the RAUW above.
    SI->setOperand(1, &AI);
  }
}

/// lowerAcrossUnwindEdges - Find all variables which are alive across an unwind
/// edge and spill them.
void SjLjEHPrepare::lowerAcrossUnwindEdges(Function &F,
                                           ArrayRef<InvokeInst *> Invokes) {
  // Finally, scan the code looking for instructions with bad live ranges.
  for (BasicBlock &BB : F) {
    for (Instruction &Inst : BB) {
      // Ignore obvious cases we don't have to handle. In particular, most
      // instructions either have no uses or only have a single use inside the
      // current block. Ignore them quickly.
      if (Inst.use_empty())
        continue;
      if (Inst.hasOneUse() &&
          cast<Instruction>(Inst.user_back())->getParent() == &BB &&
          !isa<PHINode>(Inst.user_back()))
        continue;

      // If this is an alloca in the entry block, it's not a real register
      // value.
      if (auto *AI = dyn_cast<AllocaInst>(&Inst))
        if (AI->isStaticAlloca())
          continue;

      // Avoid iterator invalidation by copying users to a temporary vector.
      SmallVector<Instruction *, 16> Users;
      for (User *U : Inst.users()) {
        Instruction *UI = cast<Instruction>(U);
        if (UI->getParent() != &BB || isa<PHINode>(UI))
          Users.push_back(UI);
      }

      // Find all of the blocks that this value is live in.
      SmallPtrSet<BasicBlock *, 32> LiveBBs;
      LiveBBs.insert(&BB);
      while (!Users.empty()) {
        Instruction *U = Users.pop_back_val();

        if (!isa<PHINode>(U)) {
          MarkBlocksLiveIn(U->getParent(), LiveBBs);
        } else {
          // Uses for a PHI node occur in their predecessor block.
          PHINode *PN = cast<PHINode>(U);
          for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
            if (PN->getIncomingValue(i) == &Inst)
              MarkBlocksLiveIn(PN->getIncomingBlock(i), LiveBBs);
        }
      }

      // Now that we know all of the blocks that this thing is live in, see if
      // it includes any of the unwind locations.
      bool NeedsSpill = false;
      for (InvokeInst *Invoke : Invokes) {
        BasicBlock *UnwindBlock = Invoke->getUnwindDest();
        if (UnwindBlock != &BB && LiveBBs.count(UnwindBlock)) {
          LLVM_DEBUG(dbgs() << "SJLJ Spill: " << Inst << " around "
                            << UnwindBlock->getName() << "\n");
          NeedsSpill = true;
          break;
        }
      }

      // If we decided we need a spill, do it.
      // FIXME: Spilling this way is overkill, as it forces all uses of
      // the value to be reloaded from the stack slot, even those that aren't
      // in the unwind blocks. We should be more selective.
      if (NeedsSpill) {
        DemoteRegToStack(Inst, true);
        ++NumSpilled;
      }
    }
  }

  // Go through the landing pads and remove any PHIs there.
  for (InvokeInst *Invoke : Invokes) {
    BasicBlock *UnwindBlock = Invoke->getUnwindDest();
    LandingPadInst *LPI = UnwindBlock->getLandingPadInst();

    // Place PHIs into a set to avoid invalidating the iterator.
    SmallPtrSet<PHINode *, 8> PHIsToDemote;
    for (BasicBlock::iterator PN = UnwindBlock->begin(); isa<PHINode>(PN); ++PN)
      PHIsToDemote.insert(cast<PHINode>(PN));
    if (PHIsToDemote.empty())
      continue;

    // Demote the PHIs to the stack.
    for (PHINode *PN : PHIsToDemote)
      DemotePHIToStack(PN);

    // Move the landingpad instruction back to the top of the landing pad block.
    LPI->moveBefore(&UnwindBlock->front());
  }
}

/// setupEntryBlockAndCallSites - Setup the entry block by creating and filling
/// the function context and marking the call sites with the appropriate
/// values. These values are used by the DWARF EH emitter.
bool SjLjEHPrepare::setupEntryBlockAndCallSites(Function &F) {
  SmallVector<ReturnInst *, 16> Returns;
  SmallVector<InvokeInst *, 16> Invokes;
  SmallSetVector<LandingPadInst *, 16> LPads;

  // Look through the terminators of the basic blocks to find invokes.
  for (BasicBlock &BB : F)
    if (auto *II = dyn_cast<InvokeInst>(BB.getTerminator())) {
      if (Function *Callee = II->getCalledFunction())
        if (Callee->getIntrinsicID() == Intrinsic::donothing) {
          // Remove the NOP invoke.
          BranchInst::Create(II->getNormalDest(), II);
          II->eraseFromParent();
          continue;
        }

      Invokes.push_back(II);
      LPads.insert(II->getUnwindDest()->getLandingPadInst());
    } else if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
      Returns.push_back(RI);
    }

  if (Invokes.empty())
    return false;

  NumInvokes += Invokes.size();

  lowerIncomingArguments(F);
  lowerAcrossUnwindEdges(F, Invokes);

  Value *FuncCtx =
      setupFunctionContext(F, makeArrayRef(LPads.begin(), LPads.end()));
  BasicBlock *EntryBB = &F.front();
  IRBuilder<> Builder(EntryBB->getTerminator());

  // Get a reference to the jump buffer.
  Value *JBufPtr =
      Builder.CreateConstGEP2_32(FunctionContextTy, FuncCtx, 0, 5, "jbuf_gep");

  // Save the frame pointer.
  Value *FramePtr = Builder.CreateConstGEP2_32(doubleUnderJBufTy, JBufPtr, 0, 0,
                                               "jbuf_fp_gep");

  Value *Val = Builder.CreateCall(FrameAddrFn, Builder.getInt32(0), "fp");
  Builder.CreateStore(Val, FramePtr, /*isVolatile=*/true);

  // Save the stack pointer.
  Value *StackPtr = Builder.CreateConstGEP2_32(doubleUnderJBufTy, JBufPtr, 0, 2,
                                               "jbuf_sp_gep");

  Val = Builder.CreateCall(StackAddrFn, {}, "sp");
  Builder.CreateStore(Val, StackPtr, /*isVolatile=*/true);

  // Call the setup_dispatch instrinsic. It fills in the rest of the jmpbuf.
  Builder.CreateCall(BuiltinSetupDispatchFn, {});

  // Store a pointer to the function context so that the back-end will know
  // where to look for it.
  Value *FuncCtxArg = Builder.CreateBitCast(FuncCtx, Builder.getInt8PtrTy());
  Builder.CreateCall(FuncCtxFn, FuncCtxArg);

  // At this point, we are all set up, update the invoke instructions to mark
  // their call_site values.
  for (unsigned I = 0, E = Invokes.size(); I != E; ++I) {
    insertCallSiteStore(Invokes[I], I + 1);

    ConstantInt *CallSiteNum =
        ConstantInt::get(Type::getInt32Ty(F.getContext()), I + 1);

    // Record the call site value for the back end so it stays associated with
    // the invoke.
    CallInst::Create(CallSiteFn, CallSiteNum, "", Invokes[I]);
  }

  // Mark call instructions that aren't nounwind as no-action (call_site ==
  // -1). Skip the entry block, as prior to then, no function context has been
  // created for this function and any unexpected exceptions thrown will go
  // directly to the caller's context, which is what we want anyway, so no need
  // to do anything here.
  for (BasicBlock &BB : F) {
    if (&BB == &F.front())
      continue;
    for (Instruction &I : BB)
      if (I.mayThrow())
        insertCallSiteStore(&I, -1);
  }

  // Register the function context and make sure it's known to not throw
  CallInst *Register =
      CallInst::Create(RegisterFn, FuncCtx, "", EntryBB->getTerminator());
  Register->setDoesNotThrow();

  // Following any allocas not in the entry block, update the saved SP in the
  // jmpbuf to the new value.
  for (BasicBlock &BB : F) {
    if (&BB == &F.front())
      continue;
    for (Instruction &I : BB) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        if (CI->getCalledFunction() != StackRestoreFn)
          continue;
      } else if (!isa<AllocaInst>(&I)) {
        continue;
      }
      Instruction *StackAddr = CallInst::Create(StackAddrFn, "sp");
      StackAddr->insertAfter(&I);
      Instruction *StoreStackAddr = new StoreInst(StackAddr, StackPtr, true);
      StoreStackAddr->insertAfter(StackAddr);
    }
  }

  // Finally, for any returns from this function, if this function contains an
  // invoke, add a call to unregister the function context.
  for (ReturnInst *Return : Returns)
    CallInst::Create(UnregisterFn, FuncCtx, "", Return);

  return true;
}

bool SjLjEHPrepare::runOnFunction(Function &F) {
  Module &M = *F.getParent();
  RegisterFn = M.getOrInsertFunction(
      "_Unwind_SjLj_Register", Type::getVoidTy(M.getContext()),
      PointerType::getUnqual(FunctionContextTy));
  UnregisterFn = M.getOrInsertFunction(
      "_Unwind_SjLj_Unregister", Type::getVoidTy(M.getContext()),
      PointerType::getUnqual(FunctionContextTy));
  FrameAddrFn = Intrinsic::getDeclaration(&M, Intrinsic::frameaddress);
  StackAddrFn = Intrinsic::getDeclaration(&M, Intrinsic::stacksave);
  StackRestoreFn = Intrinsic::getDeclaration(&M, Intrinsic::stackrestore);
  BuiltinSetupDispatchFn =
    Intrinsic::getDeclaration(&M, Intrinsic::eh_sjlj_setup_dispatch);
  LSDAAddrFn = Intrinsic::getDeclaration(&M, Intrinsic::eh_sjlj_lsda);
  CallSiteFn = Intrinsic::getDeclaration(&M, Intrinsic::eh_sjlj_callsite);
  FuncCtxFn = Intrinsic::getDeclaration(&M, Intrinsic::eh_sjlj_functioncontext);

  bool Res = setupEntryBlockAndCallSites(F);
  return Res;
}
