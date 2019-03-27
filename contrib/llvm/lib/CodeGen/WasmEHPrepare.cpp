//===-- WasmEHPrepare - Prepare excepton handling for WebAssembly --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation is designed for use by code generators which use
// WebAssembly exception handling scheme.
//
// WebAssembly exception handling uses Windows exception IR for the middle level
// representation. This pass does the following transformation for every
// catchpad block:
// (In C-style pseudocode)
//
// - Before:
//   catchpad ...
//   exn = wasm.get.exception();
//   selector = wasm.get.selector();
//   ...
//
// - After:
//   catchpad ...
//   exn = wasm.catch(0); // 0 is a tag for C++
//   wasm.landingpad.index(index);
//   // Only add below in case it's not a single catch (...)
//   __wasm_lpad_context.lpad_index = index;
//   __wasm_lpad_context.lsda = wasm.lsda();
//   _Unwind_CallPersonality(exn);
//   int selector = __wasm.landingpad_context.selector;
//   ...
//
// Also, does the following for a cleanuppad block with a call to
// __clang_call_terminate():
// - Before:
//   cleanuppad ...
//   exn = wasm.get.exception();
//   __clang_call_terminate(exn);
//
// - After:
//   cleanuppad ...
//   exn = wasm.catch(0); // 0 is a tag for C++
//   __clang_call_terminate(exn);
//
//
// * Background: WebAssembly EH instructions
// WebAssembly's try and catch instructions are structured as follows:
// try
//   instruction*
// catch (C++ tag)
//   instruction*
// ...
// catch_all
//   instruction*
// try_end
//
// A catch instruction in WebAssembly does not correspond to a C++ catch clause.
// In WebAssembly, there is a single catch instruction for all C++ exceptions.
// There can be more catch instructions for exceptions in other languages, but
// they are not generated for now. catch_all catches all exceptions including
// foreign exceptions (e.g. JavaScript). We turn catchpads into catch (C++ tag)
// and cleanuppads into catch_all, with one exception: cleanuppad with a call to
// __clang_call_terminate should be both in catch (C++ tag) and catch_all.
//
//
// * Background: Direct personality function call
// In WebAssembly EH, the VM is responsible for unwinding the stack once an
// exception is thrown. After the stack is unwound, the control flow is
// transfered to WebAssembly 'catch' instruction, which returns a caught
// exception object.
//
// Unwinding the stack is not done by libunwind but the VM, so the personality
// function in libcxxabi cannot be called from libunwind during the unwinding
// process. So after a catch instruction, we insert a call to a wrapper function
// in libunwind that in turn calls the real personality function.
//
// In Itanium EH, if the personality function decides there is no matching catch
// clause in a call frame and no cleanup action to perform, the unwinder doesn't
// stop there and continues unwinding. But in Wasm EH, the unwinder stops at
// every call frame with a catch intruction, after which the personality
// function is called from the compiler-generated user code here.
//
// In libunwind, we have this struct that serves as a communincation channel
// between the compiler-generated user code and the personality function in
// libcxxabi.
//
// struct _Unwind_LandingPadContext {
//   uintptr_t lpad_index;
//   uintptr_t lsda;
//   uintptr_t selector;
// };
// struct _Unwind_LandingPadContext __wasm_lpad_context = ...;
//
// And this wrapper in libunwind calls the personality function.
//
// _Unwind_Reason_Code _Unwind_CallPersonality(void *exception_ptr) {
//   struct _Unwind_Exception *exception_obj =
//       (struct _Unwind_Exception *)exception_ptr;
//   _Unwind_Reason_Code ret = __gxx_personality_v0(
//       1, _UA_CLEANUP_PHASE, exception_obj->exception_class, exception_obj,
//       (struct _Unwind_Context *)__wasm_lpad_context);
//   return ret;
// }
//
// We pass a landing pad index, and the address of LSDA for the current function
// to the wrapper function _Unwind_CallPersonality in libunwind, and we retrieve
// the selector after it returns.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Triple.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "wasmehprepare"

namespace {
class WasmEHPrepare : public FunctionPass {
  Type *LPadContextTy = nullptr; // type of 'struct _Unwind_LandingPadContext'
  GlobalVariable *LPadContextGV = nullptr; // __wasm_lpad_context

  // Field addresses of struct _Unwind_LandingPadContext
  Value *LPadIndexField = nullptr; // lpad_index field
  Value *LSDAField = nullptr;      // lsda field
  Value *SelectorField = nullptr;  // selector

  Function *ThrowF = nullptr;           // wasm.throw() intrinsic
  Function *CatchF = nullptr;           // wasm.catch.extract() intrinsic
  Function *LPadIndexF = nullptr;       // wasm.landingpad.index() intrinsic
  Function *LSDAF = nullptr;            // wasm.lsda() intrinsic
  Function *GetExnF = nullptr;          // wasm.get.exception() intrinsic
  Function *GetSelectorF = nullptr;     // wasm.get.ehselector() intrinsic
  Function *CallPersonalityF = nullptr; // _Unwind_CallPersonality() wrapper
  Function *ClangCallTermF = nullptr;   // __clang_call_terminate() function

  bool prepareEHPads(Function &F);
  bool prepareThrows(Function &F);

  void prepareEHPad(BasicBlock *BB, unsigned Index);
  void prepareTerminateCleanupPad(BasicBlock *BB);

public:
  static char ID; // Pass identification, replacement for typeid

  WasmEHPrepare() : FunctionPass(ID) {}

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    return "WebAssembly Exception handling preparation";
  }
};
} // end anonymous namespace

char WasmEHPrepare::ID = 0;
INITIALIZE_PASS(WasmEHPrepare, DEBUG_TYPE, "Prepare WebAssembly exceptions",
                false, false)

FunctionPass *llvm::createWasmEHPass() { return new WasmEHPrepare(); }

bool WasmEHPrepare::doInitialization(Module &M) {
  IRBuilder<> IRB(M.getContext());
  LPadContextTy = StructType::get(IRB.getInt32Ty(),   // lpad_index
                                  IRB.getInt8PtrTy(), // lsda
                                  IRB.getInt32Ty()    // selector
  );
  return false;
}

// Erase the specified BBs if the BB does not have any remaining predecessors,
// and also all its dead children.
template <typename Container>
static void eraseDeadBBsAndChildren(const Container &BBs) {
  SmallVector<BasicBlock *, 8> WL(BBs.begin(), BBs.end());
  while (!WL.empty()) {
    auto *BB = WL.pop_back_val();
    if (pred_begin(BB) != pred_end(BB))
      continue;
    WL.append(succ_begin(BB), succ_end(BB));
    DeleteDeadBlock(BB);
  }
}

bool WasmEHPrepare::runOnFunction(Function &F) {
  bool Changed = false;
  Changed |= prepareThrows(F);
  Changed |= prepareEHPads(F);
  return Changed;
}

bool WasmEHPrepare::prepareThrows(Function &F) {
  Module &M = *F.getParent();
  IRBuilder<> IRB(F.getContext());
  bool Changed = false;

  // wasm.throw() intinsic, which will be lowered to wasm 'throw' instruction.
  ThrowF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_throw);

  // Insert an unreachable instruction after a call to @llvm.wasm.throw and
  // delete all following instructions within the BB, and delete all the dead
  // children of the BB as well.
  for (User *U : ThrowF->users()) {
    // A call to @llvm.wasm.throw() is only generated from
    // __builtin_wasm_throw() builtin call within libcxxabi, and cannot be an
    // InvokeInst.
    auto *ThrowI = cast<CallInst>(U);
    if (ThrowI->getFunction() != &F)
      continue;
    Changed = true;
    auto *BB = ThrowI->getParent();
    SmallVector<BasicBlock *, 4> Succs(succ_begin(BB), succ_end(BB));
    auto &InstList = BB->getInstList();
    InstList.erase(std::next(BasicBlock::iterator(ThrowI)), InstList.end());
    IRB.SetInsertPoint(BB);
    IRB.CreateUnreachable();
    eraseDeadBBsAndChildren(Succs);
  }

  return Changed;
}

bool WasmEHPrepare::prepareEHPads(Function &F) {
  Module &M = *F.getParent();
  IRBuilder<> IRB(F.getContext());

  SmallVector<BasicBlock *, 16> CatchPads;
  SmallVector<BasicBlock *, 16> CleanupPads;
  for (BasicBlock &BB : F) {
    if (!BB.isEHPad())
      continue;
    auto *Pad = BB.getFirstNonPHI();
    if (isa<CatchPadInst>(Pad))
      CatchPads.push_back(&BB);
    else if (isa<CleanupPadInst>(Pad))
      CleanupPads.push_back(&BB);
  }

  if (CatchPads.empty() && CleanupPads.empty())
    return false;
  assert(F.hasPersonalityFn() && "Personality function not found");

  // __wasm_lpad_context global variable
  LPadContextGV = cast<GlobalVariable>(
      M.getOrInsertGlobal("__wasm_lpad_context", LPadContextTy));
  LPadIndexField = IRB.CreateConstGEP2_32(LPadContextTy, LPadContextGV, 0, 0,
                                          "lpad_index_gep");
  LSDAField =
      IRB.CreateConstGEP2_32(LPadContextTy, LPadContextGV, 0, 1, "lsda_gep");
  SelectorField = IRB.CreateConstGEP2_32(LPadContextTy, LPadContextGV, 0, 2,
                                         "selector_gep");

  // wasm.catch() intinsic, which will be lowered to wasm 'catch' instruction.
  CatchF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_catch);
  // wasm.landingpad.index() intrinsic, which is to specify landingpad index
  LPadIndexF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_landingpad_index);
  // wasm.lsda() intrinsic. Returns the address of LSDA table for the current
  // function.
  LSDAF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_lsda);
  // wasm.get.exception() and wasm.get.ehselector() intrinsics. Calls to these
  // are generated in clang.
  GetExnF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_get_exception);
  GetSelectorF = Intrinsic::getDeclaration(&M, Intrinsic::wasm_get_ehselector);

  // _Unwind_CallPersonality() wrapper function, which calls the personality
  CallPersonalityF = cast<Function>(M.getOrInsertFunction(
      "_Unwind_CallPersonality", IRB.getInt32Ty(), IRB.getInt8PtrTy()));
  CallPersonalityF->setDoesNotThrow();

  // __clang_call_terminate() function, which is inserted by clang in case a
  // cleanup throws
  ClangCallTermF = M.getFunction("__clang_call_terminate");

  unsigned Index = 0;
  for (auto *BB : CatchPads) {
    auto *CPI = cast<CatchPadInst>(BB->getFirstNonPHI());
    // In case of a single catch (...), we don't need to emit LSDA
    if (CPI->getNumArgOperands() == 1 &&
        cast<Constant>(CPI->getArgOperand(0))->isNullValue())
      prepareEHPad(BB, -1);
    else
      prepareEHPad(BB, Index++);
  }

  if (!ClangCallTermF)
    return !CatchPads.empty();

  // Cleanuppads will turn into catch_all later, but cleanuppads with a call to
  // __clang_call_terminate() is a special case. __clang_call_terminate() takes
  // an exception object, so we have to duplicate call in both 'catch <C++ tag>'
  // and 'catch_all' clauses. Here we only insert a call to catch; the
  // duplication will be done later. In catch_all, the exception object will be
  // set to null.
  for (auto *BB : CleanupPads)
    for (auto &I : *BB)
      if (auto *CI = dyn_cast<CallInst>(&I))
        if (CI->getCalledValue() == ClangCallTermF)
          prepareEHPad(BB, -1);

  return true;
}

void WasmEHPrepare::prepareEHPad(BasicBlock *BB, unsigned Index) {
  assert(BB->isEHPad() && "BB is not an EHPad!");
  IRBuilder<> IRB(BB->getContext());

  IRB.SetInsertPoint(&*BB->getFirstInsertionPt());
  // The argument to wasm.catch() is the tag for C++ exceptions, which we set to
  // 0 for this module.
  // Pseudocode: void *exn = wasm.catch(0);
  Instruction *Exn = IRB.CreateCall(CatchF, IRB.getInt32(0), "exn");
  // Replace the return value of wasm.get.exception() with the return value from
  // wasm.catch().
  auto *FPI = cast<FuncletPadInst>(BB->getFirstNonPHI());
  Instruction *GetExnCI = nullptr, *GetSelectorCI = nullptr;
  for (auto &U : FPI->uses()) {
    if (auto *CI = dyn_cast<CallInst>(U.getUser())) {
      if (CI->getCalledValue() == GetExnF)
        GetExnCI = CI;
      else if (CI->getCalledValue() == GetSelectorF)
        GetSelectorCI = CI;
    }
  }

  assert(GetExnCI && "wasm.get.exception() call does not exist");
  GetExnCI->replaceAllUsesWith(Exn);
  GetExnCI->eraseFromParent();

  // In case it is a catchpad with single catch (...) or a cleanuppad, we don't
  // need to call personality function because we don't need a selector.
  if (FPI->getNumArgOperands() == 0 ||
      (FPI->getNumArgOperands() == 1 &&
       cast<Constant>(FPI->getArgOperand(0))->isNullValue())) {
    if (GetSelectorCI) {
      assert(GetSelectorCI->use_empty() &&
             "wasm.get.ehselector() still has uses!");
      GetSelectorCI->eraseFromParent();
    }
    return;
  }
  IRB.SetInsertPoint(Exn->getNextNode());

  // This is to create a map of <landingpad EH label, landingpad index> in
  // SelectionDAGISel, which is to be used in EHStreamer to emit LSDA tables.
  // Pseudocode: wasm.landingpad.index(Index);
  IRB.CreateCall(LPadIndexF, {FPI, IRB.getInt32(Index)});

  // Pseudocode: __wasm_lpad_context.lpad_index = index;
  IRB.CreateStore(IRB.getInt32(Index), LPadIndexField);

  // Store LSDA address only if this catchpad belongs to a top-level
  // catchswitch. If there is another catchpad that dominates this pad, we don't
  // need to store LSDA address again, because they are the same throughout the
  // function and have been already stored before.
  // TODO Can we not store LSDA address in user function but make libcxxabi
  // compute it?
  auto *CPI = cast<CatchPadInst>(FPI);
  if (isa<ConstantTokenNone>(CPI->getCatchSwitch()->getParentPad()))
    // Pseudocode: __wasm_lpad_context.lsda = wasm.lsda();
    IRB.CreateStore(IRB.CreateCall(LSDAF), LSDAField);

  // Pseudocode: _Unwind_CallPersonality(exn);
  CallInst *PersCI =
      IRB.CreateCall(CallPersonalityF, Exn, OperandBundleDef("funclet", CPI));
  PersCI->setDoesNotThrow();

  // Pseudocode: int selector = __wasm.landingpad_context.selector;
  Instruction *Selector = IRB.CreateLoad(SelectorField, "selector");

  // Replace the return value from wasm.get.ehselector() with the selector value
  // loaded from __wasm_lpad_context.selector.
  assert(GetSelectorCI && "wasm.get.ehselector() call does not exist");
  GetSelectorCI->replaceAllUsesWith(Selector);
  GetSelectorCI->eraseFromParent();
}

void llvm::calculateWasmEHInfo(const Function *F, WasmEHFuncInfo &EHInfo) {
  for (const auto &BB : *F) {
    if (!BB.isEHPad())
      continue;
    const Instruction *Pad = BB.getFirstNonPHI();

    // If an exception is not caught by a catchpad (i.e., it is a foreign
    // exception), it will unwind to its parent catchswitch's unwind
    // destination. We don't record an unwind destination for cleanuppads
    // because every exception should be caught by it.
    if (const auto *CatchPad = dyn_cast<CatchPadInst>(Pad)) {
      const auto *UnwindBB = CatchPad->getCatchSwitch()->getUnwindDest();
      if (!UnwindBB)
        continue;
      const Instruction *UnwindPad = UnwindBB->getFirstNonPHI();
      if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(UnwindPad))
        // Currently there should be only one handler per a catchswitch.
        EHInfo.setEHPadUnwindDest(&BB, *CatchSwitch->handlers().begin());
      else // cleanuppad
        EHInfo.setEHPadUnwindDest(&BB, UnwindBB);
    }
  }

  // Record the unwind destination for invoke and cleanupret instructions.
  for (const auto &BB : *F) {
    const Instruction *TI = BB.getTerminator();
    BasicBlock *UnwindBB = nullptr;
    if (const auto *Invoke = dyn_cast<InvokeInst>(TI))
      UnwindBB = Invoke->getUnwindDest();
    else if (const auto *CleanupRet = dyn_cast<CleanupReturnInst>(TI))
      UnwindBB = CleanupRet->getUnwindDest();
    if (!UnwindBB)
      continue;
    const Instruction *UnwindPad = UnwindBB->getFirstNonPHI();
    if (const auto *CatchSwitch = dyn_cast<CatchSwitchInst>(UnwindPad))
      // Currently there should be only one handler per a catchswitch.
      EHInfo.setThrowUnwindDest(&BB, *CatchSwitch->handlers().begin());
    else // cleanuppad
      EHInfo.setThrowUnwindDest(&BB, UnwindBB);
  }
}
