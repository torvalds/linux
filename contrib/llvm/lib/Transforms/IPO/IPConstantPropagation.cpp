//===-- IPConstantPropagation.cpp - Propagate constants through calls -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements an _extremely_ simple interprocedural constant
// propagation pass.  It could certainly be improved in many different ways,
// like using a worklist.  This pass makes arguments dead, but does not remove
// them.  The existing dead argument elimination pass should be run after this
// to clean up the mess.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"
using namespace llvm;

#define DEBUG_TYPE "ipconstprop"

STATISTIC(NumArgumentsProped, "Number of args turned into constants");
STATISTIC(NumReturnValProped, "Number of return values turned into constants");

namespace {
  /// IPCP - The interprocedural constant propagation pass
  ///
  struct IPCP : public ModulePass {
    static char ID; // Pass identification, replacement for typeid
    IPCP() : ModulePass(ID) {
      initializeIPCPPass(*PassRegistry::getPassRegistry());
    }

    bool runOnModule(Module &M) override;
  };
}

/// PropagateConstantsIntoArguments - Look at all uses of the specified
/// function.  If all uses are direct call sites, and all pass a particular
/// constant in for an argument, propagate that constant in as the argument.
///
static bool PropagateConstantsIntoArguments(Function &F) {
  if (F.arg_empty() || F.use_empty()) return false; // No arguments? Early exit.

  // For each argument, keep track of its constant value and whether it is a
  // constant or not.  The bool is driven to true when found to be non-constant.
  SmallVector<std::pair<Constant*, bool>, 16> ArgumentConstants;
  ArgumentConstants.resize(F.arg_size());

  unsigned NumNonconstant = 0;
  for (Use &U : F.uses()) {
    User *UR = U.getUser();
    // Ignore blockaddress uses.
    if (isa<BlockAddress>(UR)) continue;

    // Used by a non-instruction, or not the callee of a function, do not
    // transform.
    if (!isa<CallInst>(UR) && !isa<InvokeInst>(UR))
      return false;

    CallSite CS(cast<Instruction>(UR));
    if (!CS.isCallee(&U))
      return false;

    // Check out all of the potentially constant arguments.  Note that we don't
    // inspect varargs here.
    CallSite::arg_iterator AI = CS.arg_begin();
    Function::arg_iterator Arg = F.arg_begin();
    for (unsigned i = 0, e = ArgumentConstants.size(); i != e;
         ++i, ++AI, ++Arg) {

      // If this argument is known non-constant, ignore it.
      if (ArgumentConstants[i].second)
        continue;

      Constant *C = dyn_cast<Constant>(*AI);
      if (C && ArgumentConstants[i].first == nullptr) {
        ArgumentConstants[i].first = C;   // First constant seen.
      } else if (C && ArgumentConstants[i].first == C) {
        // Still the constant value we think it is.
      } else if (*AI == &*Arg) {
        // Ignore recursive calls passing argument down.
      } else {
        // Argument became non-constant.  If all arguments are non-constant now,
        // give up on this function.
        if (++NumNonconstant == ArgumentConstants.size())
          return false;
        ArgumentConstants[i].second = true;
      }
    }
  }

  // If we got to this point, there is a constant argument!
  assert(NumNonconstant != ArgumentConstants.size());
  bool MadeChange = false;
  Function::arg_iterator AI = F.arg_begin();
  for (unsigned i = 0, e = ArgumentConstants.size(); i != e; ++i, ++AI) {
    // Do we have a constant argument?
    if (ArgumentConstants[i].second || AI->use_empty() ||
        AI->hasInAllocaAttr() || (AI->hasByValAttr() && !F.onlyReadsMemory()))
      continue;

    Value *V = ArgumentConstants[i].first;
    if (!V) V = UndefValue::get(AI->getType());
    AI->replaceAllUsesWith(V);
    ++NumArgumentsProped;
    MadeChange = true;
  }
  return MadeChange;
}


// Check to see if this function returns one or more constants. If so, replace
// all callers that use those return values with the constant value. This will
// leave in the actual return values and instructions, but deadargelim will
// clean that up.
//
// Additionally if a function always returns one of its arguments directly,
// callers will be updated to use the value they pass in directly instead of
// using the return value.
static bool PropagateConstantReturn(Function &F) {
  if (F.getReturnType()->isVoidTy())
    return false; // No return value.

  // We can infer and propagate the return value only when we know that the
  // definition we'll get at link time is *exactly* the definition we see now.
  // For more details, see GlobalValue::mayBeDerefined.
  if (!F.isDefinitionExact())
    return false;

  // Don't touch naked functions. The may contain asm returning
  // value we don't see, so we may end up interprocedurally propagating
  // the return value incorrectly.
  if (F.hasFnAttribute(Attribute::Naked))
    return false;

  // Check to see if this function returns a constant.
  SmallVector<Value *,4> RetVals;
  StructType *STy = dyn_cast<StructType>(F.getReturnType());
  if (STy)
    for (unsigned i = 0, e = STy->getNumElements(); i < e; ++i)
      RetVals.push_back(UndefValue::get(STy->getElementType(i)));
  else
    RetVals.push_back(UndefValue::get(F.getReturnType()));

  unsigned NumNonConstant = 0;
  for (BasicBlock &BB : F)
    if (ReturnInst *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
      for (unsigned i = 0, e = RetVals.size(); i != e; ++i) {
        // Already found conflicting return values?
        Value *RV = RetVals[i];
        if (!RV)
          continue;

        // Find the returned value
        Value *V;
        if (!STy)
          V = RI->getOperand(0);
        else
          V = FindInsertedValue(RI->getOperand(0), i);

        if (V) {
          // Ignore undefs, we can change them into anything
          if (isa<UndefValue>(V))
            continue;

          // Try to see if all the rets return the same constant or argument.
          if (isa<Constant>(V) || isa<Argument>(V)) {
            if (isa<UndefValue>(RV)) {
              // No value found yet? Try the current one.
              RetVals[i] = V;
              continue;
            }
            // Returning the same value? Good.
            if (RV == V)
              continue;
          }
        }
        // Different or no known return value? Don't propagate this return
        // value.
        RetVals[i] = nullptr;
        // All values non-constant? Stop looking.
        if (++NumNonConstant == RetVals.size())
          return false;
      }
    }

  // If we got here, the function returns at least one constant value.  Loop
  // over all users, replacing any uses of the return value with the returned
  // constant.
  bool MadeChange = false;
  for (Use &U : F.uses()) {
    CallSite CS(U.getUser());
    Instruction* Call = CS.getInstruction();

    // Not a call instruction or a call instruction that's not calling F
    // directly?
    if (!Call || !CS.isCallee(&U))
      continue;

    // Call result not used?
    if (Call->use_empty())
      continue;

    MadeChange = true;

    if (!STy) {
      Value* New = RetVals[0];
      if (Argument *A = dyn_cast<Argument>(New))
        // Was an argument returned? Then find the corresponding argument in
        // the call instruction and use that.
        New = CS.getArgument(A->getArgNo());
      Call->replaceAllUsesWith(New);
      continue;
    }

    for (auto I = Call->user_begin(), E = Call->user_end(); I != E;) {
      Instruction *Ins = cast<Instruction>(*I);

      // Increment now, so we can remove the use
      ++I;

      // Find the index of the retval to replace with
      int index = -1;
      if (ExtractValueInst *EV = dyn_cast<ExtractValueInst>(Ins))
        if (EV->hasIndices())
          index = *EV->idx_begin();

      // If this use uses a specific return value, and we have a replacement,
      // replace it.
      if (index != -1) {
        Value *New = RetVals[index];
        if (New) {
          if (Argument *A = dyn_cast<Argument>(New))
            // Was an argument returned? Then find the corresponding argument in
            // the call instruction and use that.
            New = CS.getArgument(A->getArgNo());
          Ins->replaceAllUsesWith(New);
          Ins->eraseFromParent();
        }
      }
    }
  }

  if (MadeChange) ++NumReturnValProped;
  return MadeChange;
}

char IPCP::ID = 0;
INITIALIZE_PASS(IPCP, "ipconstprop",
                "Interprocedural constant propagation", false, false)

ModulePass *llvm::createIPConstantPropagationPass() { return new IPCP(); }

bool IPCP::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  bool Changed = false;
  bool LocalChange = true;

  // FIXME: instead of using smart algorithms, we just iterate until we stop
  // making changes.
  while (LocalChange) {
    LocalChange = false;
    for (Function &F : M)
      if (!F.isDeclaration()) {
        // Delete any klingons.
        F.removeDeadConstantUsers();
        if (F.hasLocalLinkage())
          LocalChange |= PropagateConstantsIntoArguments(F);
        Changed |= PropagateConstantReturn(F);
      }
    Changed |= LocalChange;
  }
  return Changed;
}
