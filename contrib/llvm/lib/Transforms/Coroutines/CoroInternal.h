//===- CoroInternal.h - Internal Coroutine interfaces ---------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Common definitions/declarations used internally by coroutine lowering passes.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_COROUTINES_COROINTERNAL_H
#define LLVM_LIB_TRANSFORMS_COROUTINES_COROINTERNAL_H

#include "CoroInstr.h"
#include "llvm/Transforms/Coroutines.h"

namespace llvm {

class CallGraph;
class CallGraphSCC;
class PassRegistry;

void initializeCoroEarlyPass(PassRegistry &);
void initializeCoroSplitPass(PassRegistry &);
void initializeCoroElidePass(PassRegistry &);
void initializeCoroCleanupPass(PassRegistry &);

// CoroEarly pass marks every function that has coro.begin with a string
// attribute "coroutine.presplit"="0". CoroSplit pass processes the coroutine
// twice. First, it lets it go through complete IPO optimization pipeline as a
// single function. It forces restart of the pipeline by inserting an indirect
// call to an empty function "coro.devirt.trigger" which is devirtualized by
// CoroElide pass that triggers a restart of the pipeline by CGPassManager.
// When CoroSplit pass sees the same coroutine the second time, it splits it up,
// adds coroutine subfunctions to the SCC to be processed by IPO pipeline.

#define CORO_PRESPLIT_ATTR "coroutine.presplit"
#define UNPREPARED_FOR_SPLIT "0"
#define PREPARED_FOR_SPLIT "1"

#define CORO_DEVIRT_TRIGGER_FN "coro.devirt.trigger"

namespace coro {

bool declaresIntrinsics(Module &M, std::initializer_list<StringRef>);
void replaceAllCoroAllocs(CoroBeginInst *CB, bool Replacement);
void replaceAllCoroFrees(CoroBeginInst *CB, Value *Replacement);
void replaceCoroFree(CoroIdInst *CoroId, bool Elide);
void updateCallGraph(Function &Caller, ArrayRef<Function *> Funcs,
                     CallGraph &CG, CallGraphSCC &SCC);

// Keeps data and helper functions for lowering coroutine intrinsics.
struct LowererBase {
  Module &TheModule;
  LLVMContext &Context;
  PointerType *const Int8Ptr;
  FunctionType *const ResumeFnType;
  ConstantPointerNull *const NullPtr;

  LowererBase(Module &M);
  Value *makeSubFnCall(Value *Arg, int Index, Instruction *InsertPt);
};

// Holds structural Coroutine Intrinsics for a particular function and other
// values used during CoroSplit pass.
struct LLVM_LIBRARY_VISIBILITY Shape {
  CoroBeginInst *CoroBegin;
  SmallVector<CoroEndInst *, 4> CoroEnds;
  SmallVector<CoroSizeInst *, 2> CoroSizes;
  SmallVector<CoroSuspendInst *, 4> CoroSuspends;

  // Field Indexes for known coroutine frame fields.
  enum {
    ResumeField,
    DestroyField,
    PromiseField,
    IndexField,
  };

  StructType *FrameTy;
  Instruction *FramePtr;
  BasicBlock *AllocaSpillBlock;
  SwitchInst *ResumeSwitch;
  AllocaInst *PromiseAlloca;
  bool HasFinalSuspend;

  IntegerType *getIndexType() const {
    assert(FrameTy && "frame type not assigned");
    return cast<IntegerType>(FrameTy->getElementType(IndexField));
  }
  ConstantInt *getIndex(uint64_t Value) const {
    return ConstantInt::get(getIndexType(), Value);
  }

  Shape() = default;
  explicit Shape(Function &F) { buildFrom(F); }
  void buildFrom(Function &F);
};

void buildCoroutineFrame(Function &F, Shape &Shape);

} // End namespace coro.
} // End namespace llvm

#endif
