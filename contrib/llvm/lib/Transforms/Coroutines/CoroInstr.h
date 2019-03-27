//===-- CoroInstr.h - Coroutine Intrinsics Instruction Wrappers -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file defines classes that make it really easy to deal with intrinsic
// functions with the isa/dyncast family of functions.  In particular, this
// allows you to do things like:
//
//     if (auto *SF = dyn_cast<CoroSubFnInst>(Inst))
//        ... SF->getFrame() ...
//
// All intrinsic function calls are instances of the call instruction, so these
// are all subclasses of the CallInst class.  Note that none of these classes
// has state or virtual methods, which is an important part of this gross/neat
// hack working.
//
// The helpful comment above is borrowed from llvm/IntrinsicInst.h, we keep
// coroutine intrinsic wrappers here since they are only used by the passes in
// the Coroutine library.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_COROUTINES_COROINSTR_H
#define LLVM_LIB_TRANSFORMS_COROUTINES_COROINSTR_H

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"

namespace llvm {

/// This class represents the llvm.coro.subfn.addr instruction.
class LLVM_LIBRARY_VISIBILITY CoroSubFnInst : public IntrinsicInst {
  enum { FrameArg, IndexArg };

public:
  enum ResumeKind {
    RestartTrigger = -1,
    ResumeIndex,
    DestroyIndex,
    CleanupIndex,
    IndexLast,
    IndexFirst = RestartTrigger
  };

  Value *getFrame() const { return getArgOperand(FrameArg); }
  ResumeKind getIndex() const {
    int64_t Index = getRawIndex()->getValue().getSExtValue();
    assert(Index >= IndexFirst && Index < IndexLast &&
           "unexpected CoroSubFnInst index argument");
    return static_cast<ResumeKind>(Index);
  }

  ConstantInt *getRawIndex() const {
    return cast<ConstantInt>(getArgOperand(IndexArg));
  }

  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_subfn_addr;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.alloc instruction.
class LLVM_LIBRARY_VISIBILITY CoroAllocInst : public IntrinsicInst {
public:
  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_alloc;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.alloc instruction.
class LLVM_LIBRARY_VISIBILITY CoroIdInst : public IntrinsicInst {
  enum { AlignArg, PromiseArg, CoroutineArg, InfoArg };

public:
  CoroAllocInst *getCoroAlloc() {
    for (User *U : users())
      if (auto *CA = dyn_cast<CoroAllocInst>(U))
        return CA;
    return nullptr;
  }

  IntrinsicInst *getCoroBegin() {
    for (User *U : users())
      if (auto *II = dyn_cast<IntrinsicInst>(U))
        if (II->getIntrinsicID() == Intrinsic::coro_begin)
          return II;
    llvm_unreachable("no coro.begin associated with coro.id");
  }

  AllocaInst *getPromise() const {
    Value *Arg = getArgOperand(PromiseArg);
    return isa<ConstantPointerNull>(Arg)
               ? nullptr
               : cast<AllocaInst>(Arg->stripPointerCasts());
  }

  void clearPromise() {
    Value *Arg = getArgOperand(PromiseArg);
    setArgOperand(PromiseArg,
                  ConstantPointerNull::get(Type::getInt8PtrTy(getContext())));
    if (isa<AllocaInst>(Arg))
      return;
    assert((isa<BitCastInst>(Arg) || isa<GetElementPtrInst>(Arg)) &&
           "unexpected instruction designating the promise");
    // TODO: Add a check that any remaining users of Inst are after coro.begin
    // or add code to move the users after coro.begin.
    auto *Inst = cast<Instruction>(Arg);
    if (Inst->use_empty()) {
      Inst->eraseFromParent();
      return;
    }
    Inst->moveBefore(getCoroBegin()->getNextNode());
  }

  // Info argument of coro.id is
  //   fresh out of the frontend: null ;
  //   outlined                 : {Init, Return, Susp1, Susp2, ...} ;
  //   postsplit                : [resume, destroy, cleanup] ;
  //
  // If parts of the coroutine were outlined to protect against undesirable
  // code motion, these functions will be stored in a struct literal referred to
  // by the Info parameter. Note: this is only needed before coroutine is split.
  //
  // After coroutine is split, resume functions are stored in an array
  // referred to by this parameter.

  struct Info {
    ConstantStruct *OutlinedParts = nullptr;
    ConstantArray *Resumers = nullptr;

    bool hasOutlinedParts() const { return OutlinedParts != nullptr; }
    bool isPostSplit() const { return Resumers != nullptr; }
    bool isPreSplit() const { return !isPostSplit(); }
  };
  Info getInfo() const {
    Info Result;
    auto *GV = dyn_cast<GlobalVariable>(getRawInfo());
    if (!GV)
      return Result;

    assert(GV->isConstant() && GV->hasDefinitiveInitializer());
    Constant *Initializer = GV->getInitializer();
    if ((Result.OutlinedParts = dyn_cast<ConstantStruct>(Initializer)))
      return Result;

    Result.Resumers = cast<ConstantArray>(Initializer);
    return Result;
  }
  Constant *getRawInfo() const {
    return cast<Constant>(getArgOperand(InfoArg)->stripPointerCasts());
  }

  void setInfo(Constant *C) { setArgOperand(InfoArg, C); }

  Function *getCoroutine() const {
    return cast<Function>(getArgOperand(CoroutineArg)->stripPointerCasts());
  }
  void setCoroutineSelf() {
    assert(isa<ConstantPointerNull>(getArgOperand(CoroutineArg)) &&
           "Coroutine argument is already assigned");
    auto *const Int8PtrTy = Type::getInt8PtrTy(getContext());
    setArgOperand(CoroutineArg,
                  ConstantExpr::getBitCast(getFunction(), Int8PtrTy));
  }

  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_id;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.frame instruction.
class LLVM_LIBRARY_VISIBILITY CoroFrameInst : public IntrinsicInst {
public:
  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_frame;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.free instruction.
class LLVM_LIBRARY_VISIBILITY CoroFreeInst : public IntrinsicInst {
  enum { IdArg, FrameArg };

public:
  Value *getFrame() const { return getArgOperand(FrameArg); }

  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_free;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents the llvm.coro.begin instruction.
class LLVM_LIBRARY_VISIBILITY CoroBeginInst : public IntrinsicInst {
  enum { IdArg, MemArg };

public:
  CoroIdInst *getId() const { return cast<CoroIdInst>(getArgOperand(IdArg)); }

  Value *getMem() const { return getArgOperand(MemArg); }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_begin;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.save instruction.
class LLVM_LIBRARY_VISIBILITY CoroSaveInst : public IntrinsicInst {
public:
  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_save;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.promise instruction.
class LLVM_LIBRARY_VISIBILITY CoroPromiseInst : public IntrinsicInst {
  enum { FrameArg, AlignArg, FromArg };

public:
  bool isFromPromise() const {
    return cast<Constant>(getArgOperand(FromArg))->isOneValue();
  }
  unsigned getAlignment() const {
    return cast<ConstantInt>(getArgOperand(AlignArg))->getZExtValue();
  }

  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_promise;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.suspend instruction.
class LLVM_LIBRARY_VISIBILITY CoroSuspendInst : public IntrinsicInst {
  enum { SaveArg, FinalArg };

public:
  CoroSaveInst *getCoroSave() const {
    Value *Arg = getArgOperand(SaveArg);
    if (auto *SI = dyn_cast<CoroSaveInst>(Arg))
      return SI;
    assert(isa<ConstantTokenNone>(Arg));
    return nullptr;
  }
  bool isFinal() const {
    return cast<Constant>(getArgOperand(FinalArg))->isOneValue();
  }

  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_suspend;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.size instruction.
class LLVM_LIBRARY_VISIBILITY CoroSizeInst : public IntrinsicInst {
public:
  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_size;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.coro.end instruction.
class LLVM_LIBRARY_VISIBILITY CoroEndInst : public IntrinsicInst {
  enum { FrameArg, UnwindArg };

public:
  bool isFallthrough() const { return !isUnwind(); }
  bool isUnwind() const {
    return cast<Constant>(getArgOperand(UnwindArg))->isOneValue();
  }

  // Methods to support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::coro_end;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

} // End namespace llvm.

#endif
