//===--- llvm/CodeGen/WasmEHFuncInfo.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Data structures for Wasm exception handling schemes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WASMEHFUNCINFO_H
#define LLVM_CODEGEN_WASMEHFUNCINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/BasicBlock.h"

namespace llvm {

enum EventTag { CPP_EXCEPTION = 0, C_LONGJMP = 1 };

using BBOrMBB = PointerUnion<const BasicBlock *, MachineBasicBlock *>;

struct WasmEHFuncInfo {
  // When there is an entry <A, B>, if an exception is not caught by A, it
  // should next unwind to the EH pad B.
  DenseMap<BBOrMBB, BBOrMBB> EHPadUnwindMap;
  // For entry <A, B>, A is a BB with an instruction that may throw
  // (invoke/cleanupret in LLVM IR, call/rethrow in the backend) and B is an EH
  // pad that A unwinds to.
  DenseMap<BBOrMBB, BBOrMBB> ThrowUnwindMap;

  // Helper functions
  const BasicBlock *getEHPadUnwindDest(const BasicBlock *BB) const {
    return EHPadUnwindMap.lookup(BB).get<const BasicBlock *>();
  }
  void setEHPadUnwindDest(const BasicBlock *BB, const BasicBlock *Dest) {
    EHPadUnwindMap[BB] = Dest;
  }
  const BasicBlock *getThrowUnwindDest(BasicBlock *BB) const {
    return ThrowUnwindMap.lookup(BB).get<const BasicBlock *>();
  }
  void setThrowUnwindDest(const BasicBlock *BB, const BasicBlock *Dest) {
    ThrowUnwindMap[BB] = Dest;
  }
  bool hasEHPadUnwindDest(const BasicBlock *BB) const {
    return EHPadUnwindMap.count(BB);
  }
  bool hasThrowUnwindDest(const BasicBlock *BB) const {
    return ThrowUnwindMap.count(BB);
  }

  MachineBasicBlock *getEHPadUnwindDest(MachineBasicBlock *MBB) const {
    return EHPadUnwindMap.lookup(MBB).get<MachineBasicBlock *>();
  }
  void setEHPadUnwindDest(MachineBasicBlock *MBB, MachineBasicBlock *Dest) {
    EHPadUnwindMap[MBB] = Dest;
  }
  MachineBasicBlock *getThrowUnwindDest(MachineBasicBlock *MBB) const {
    return ThrowUnwindMap.lookup(MBB).get<MachineBasicBlock *>();
  }
  void setThrowUnwindDest(MachineBasicBlock *MBB, MachineBasicBlock *Dest) {
    ThrowUnwindMap[MBB] = Dest;
  }
  bool hasEHPadUnwindDest(MachineBasicBlock *MBB) const {
    return EHPadUnwindMap.count(MBB);
  }
  bool hasThrowUnwindDest(MachineBasicBlock *MBB) const {
    return ThrowUnwindMap.count(MBB);
  }
};

// Analyze the IR in the given function to build WasmEHFuncInfo.
void calculateWasmEHInfo(const Function *F, WasmEHFuncInfo &EHInfo);

} // namespace llvm

#endif // LLVM_CODEGEN_WASMEHFUNCINFO_H
