//===- AutoUpgrade.h - AutoUpgrade Helpers ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  These functions are implemented by lib/IR/AutoUpgrade.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_AUTOUPGRADE_H
#define LLVM_IR_AUTOUPGRADE_H

#include "llvm/ADT/StringRef.h"

namespace llvm {
  class CallInst;
  class Constant;
  class Function;
  class Instruction;
  class MDNode;
  class Module;
  class GlobalVariable;
  class Type;
  class Value;

  /// This is a more granular function that simply checks an intrinsic function
  /// for upgrading, and returns true if it requires upgrading. It may return
  /// null in NewFn if the all calls to the original intrinsic function
  /// should be transformed to non-function-call instructions.
  bool UpgradeIntrinsicFunction(Function *F, Function *&NewFn);

  /// This is the complement to the above, replacing a specific call to an
  /// intrinsic function with a call to the specified new function.
  void UpgradeIntrinsicCall(CallInst *CI, Function *NewFn);

  // This upgrades the comment for objc retain release markers in inline asm
  // calls
  void UpgradeInlineAsmString(std::string *AsmStr);

  /// This is an auto-upgrade hook for any old intrinsic function syntaxes
  /// which need to have both the function updated as well as all calls updated
  /// to the new function. This should only be run in a post-processing fashion
  /// so that it can update all calls to the old function.
  void UpgradeCallsToIntrinsic(Function* F);

  /// This checks for global variables which should be upgraded. It returns true
  /// if it requires upgrading.
  bool UpgradeGlobalVariable(GlobalVariable *GV);

  /// This checks for module flags which should be upgraded. It returns true if
  /// module is modified.
  bool UpgradeModuleFlags(Module &M);

  /// This checks for objc retain release marker which should be upgraded. It
  /// returns true if module is modified.
  bool UpgradeRetainReleaseMarker(Module &M);

  void UpgradeSectionAttributes(Module &M);

  /// If the given TBAA tag uses the scalar TBAA format, create a new node
  /// corresponding to the upgrade to the struct-path aware TBAA format.
  /// Otherwise return the \p TBAANode itself.
  MDNode *UpgradeTBAANode(MDNode &TBAANode);

  /// This is an auto-upgrade for bitcast between pointers with different
  /// address spaces: the instruction is replaced by a pair ptrtoint+inttoptr.
  Instruction *UpgradeBitCastInst(unsigned Opc, Value *V, Type *DestTy,
                                  Instruction *&Temp);

  /// This is an auto-upgrade for bitcast constant expression between pointers
  /// with different address spaces: the instruction is replaced by a pair
  /// ptrtoint+inttoptr.
  Value *UpgradeBitCastExpr(unsigned Opc, Constant *C, Type *DestTy);

  /// Check the debug info version number, if it is out-dated, drop the debug
  /// info. Return true if module is modified.
  bool UpgradeDebugInfo(Module &M);

  /// Check whether a string looks like an old loop attachment tag.
  inline bool mayBeOldLoopAttachmentTag(StringRef Name) {
    return Name.startswith("llvm.vectorizer.");
  }

  /// Upgrade the loop attachment metadata node.
  MDNode *upgradeInstructionLoopAttachment(MDNode &N);

} // End llvm namespace

#endif
