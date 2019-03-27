//===--- ConstantEmitter.h - IR constant emission ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A helper class for emitting expressions and values as llvm::Constants
// and as initializers for global variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CONSTANTEMITTER_H
#define LLVM_CLANG_LIB_CODEGEN_CONSTANTEMITTER_H

#include "CodeGenFunction.h"
#include "CodeGenModule.h"

namespace clang {
namespace CodeGen {

class ConstantEmitter {
public:
  CodeGenModule &CGM;
  CodeGenFunction *CGF;

private:
  bool Abstract = false;

  /// Whether non-abstract components of the emitter have been initialized.
  bool InitializedNonAbstract = false;

  /// Whether the emitter has been finalized.
  bool Finalized = false;

  /// Whether the constant-emission failed.
  bool Failed = false;

  /// Whether we're in a constant context.
  bool InConstantContext = false;

  /// The AST address space where this (non-abstract) initializer is going.
  /// Used for generating appropriate placeholders.
  LangAS DestAddressSpace;

  llvm::SmallVector<std::pair<llvm::Constant *, llvm::GlobalVariable*>, 4>
    PlaceholderAddresses;

public:
  ConstantEmitter(CodeGenModule &CGM, CodeGenFunction *CGF = nullptr)
    : CGM(CGM), CGF(CGF) {}

  /// Initialize this emission in the context of the given function.
  /// Use this if the expression might contain contextual references like
  /// block addresses or PredefinedExprs.
  ConstantEmitter(CodeGenFunction &CGF)
    : CGM(CGF.CGM), CGF(&CGF) {}

  ConstantEmitter(const ConstantEmitter &other) = delete;
  ConstantEmitter &operator=(const ConstantEmitter &other) = delete;

  ~ConstantEmitter();

  /// Is the current emission context abstract?
  bool isAbstract() const {
    return Abstract;
  }

  /// Try to emit the initiaizer of the given declaration as an abstract
  /// constant.  If this succeeds, the emission must be finalized.
  llvm::Constant *tryEmitForInitializer(const VarDecl &D);
  llvm::Constant *tryEmitForInitializer(const Expr *E, LangAS destAddrSpace,
                                        QualType destType);
  llvm::Constant *emitForInitializer(const APValue &value, LangAS destAddrSpace,
                                     QualType destType);

  void finalize(llvm::GlobalVariable *global);

  // All of the "abstract" emission methods below permit the emission to
  // be immediately discarded without finalizing anything.  Therefore, they
  // must also promise not to do anything that will, in the future, require
  // finalization:
  //
  //   - using the CGF (if present) for anything other than establishing
  //     semantic context; for example, an expression with ignored
  //     side-effects must not be emitted as an abstract expression
  //
  //   - doing anything that would not be safe to duplicate within an
  //     initializer or to propagate to another context; for example,
  //     side effects, or emitting an initialization that requires a
  //     reference to its current location.

  /// Try to emit the initializer of the given declaration as an abstract
  /// constant.
  llvm::Constant *tryEmitAbstractForInitializer(const VarDecl &D);

  /// Emit the result of the given expression as an abstract constant,
  /// asserting that it succeeded.  This is only safe to do when the
  /// expression is known to be a constant expression with either a fairly
  /// simple type or a known simple form.
  llvm::Constant *emitAbstract(const Expr *E, QualType T);
  llvm::Constant *emitAbstract(SourceLocation loc, const APValue &value,
                               QualType T);

  /// Try to emit the result of the given expression as an abstract constant.
  llvm::Constant *tryEmitAbstract(const Expr *E, QualType T);
  llvm::Constant *tryEmitAbstractForMemory(const Expr *E, QualType T);

  llvm::Constant *tryEmitAbstract(const APValue &value, QualType T);
  llvm::Constant *tryEmitAbstractForMemory(const APValue &value, QualType T);

  llvm::Constant *emitNullForMemory(QualType T) {
    return emitNullForMemory(CGM, T);
  }
  llvm::Constant *emitForMemory(llvm::Constant *C, QualType T) {
    return emitForMemory(CGM, C, T);
  }

  static llvm::Constant *emitNullForMemory(CodeGenModule &CGM, QualType T);
  static llvm::Constant *emitForMemory(CodeGenModule &CGM, llvm::Constant *C,
                                       QualType T);

  // These are private helper routines of the constant emitter that
  // can't actually be private because things are split out into helper
  // functions and classes.

  llvm::Constant *tryEmitPrivateForVarInit(const VarDecl &D);

  llvm::Constant *tryEmitPrivate(const Expr *E, QualType T);
  llvm::Constant *tryEmitPrivateForMemory(const Expr *E, QualType T);

  llvm::Constant *tryEmitPrivate(const APValue &value, QualType T);
  llvm::Constant *tryEmitPrivateForMemory(const APValue &value, QualType T);

  /// Get the address of the current location.  This is a constant
  /// that will resolve, after finalization, to the address of the
  /// 'signal' value that is registered with the emitter later.
  llvm::GlobalValue *getCurrentAddrPrivate();

  /// Register a 'signal' value with the emitter to inform it where to
  /// resolve a placeholder.  The signal value must be unique in the
  /// initializer; it might, for example, be the address of a global that
  /// refers to the current-address value in its own initializer.
  ///
  /// Uses of the placeholder must be properly anchored before finalizing
  /// the emitter, e.g. by being installed as the initializer of a global
  /// variable.  That is, it must be possible to replaceAllUsesWith
  /// the placeholder with the proper address of the signal.
  void registerCurrentAddrPrivate(llvm::Constant *signal,
                                  llvm::GlobalValue *placeholder);

private:
  void initializeNonAbstract(LangAS destAS) {
    assert(!InitializedNonAbstract);
    InitializedNonAbstract = true;
    DestAddressSpace = destAS;
  }
  llvm::Constant *markIfFailed(llvm::Constant *init) {
    if (!init)
      Failed = true;
    return init;
  }

  struct AbstractState {
    bool OldValue;
    size_t OldPlaceholdersSize;
  };
  AbstractState pushAbstract() {
    AbstractState saved = { Abstract, PlaceholderAddresses.size() };
    Abstract = true;
    return saved;
  }
  llvm::Constant *validateAndPopAbstract(llvm::Constant *C, AbstractState save);
};

}
}

#endif
