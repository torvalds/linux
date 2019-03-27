//===-- WebAssemblyUtilities - WebAssembly Utility Functions ---*- C++ -*-====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the WebAssembly-specific
/// utility functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYUTILITIES_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYUTILITIES_H

#include "llvm/CodeGen/MachineBasicBlock.h"

namespace llvm {

class WebAssemblyFunctionInfo;

namespace WebAssembly {

bool isArgument(const MachineInstr &MI);
bool isCopy(const MachineInstr &MI);
bool isTee(const MachineInstr &MI);
bool isChild(const MachineInstr &MI, const WebAssemblyFunctionInfo &MFI);
bool isCallDirect(const MachineInstr &MI);
bool isCallIndirect(const MachineInstr &MI);
bool isMarker(const MachineInstr &MI);
bool isThrow(const MachineInstr &MI);
bool isRethrow(const MachineInstr &MI);
bool isCatch(const MachineInstr &MI);
bool mayThrow(const MachineInstr &MI);

/// Returns the operand number of a callee, assuming the argument is a call
/// instruction.
unsigned getCalleeOpNo(const MachineInstr &MI);

/// Returns if the given BB is a single BB terminate pad which starts with a
/// 'catch' instruction.
bool isCatchTerminatePad(const MachineBasicBlock &MBB);
/// Returns if the given BB is a single BB terminate pad which starts with a
/// 'catch_all' insrtruction.
bool isCatchAllTerminatePad(const MachineBasicBlock &MBB);

// Exception-related function names
extern const char *const ClangCallTerminateFn;
extern const char *const CxaBeginCatchFn;
extern const char *const CxaRethrowFn;
extern const char *const StdTerminateFn;
extern const char *const PersonalityWrapperFn;

/// Return the "bottom" block of an entity, which can be either a MachineLoop or
/// WebAssemblyException. This differs from MachineLoop::getBottomBlock in that
/// it works even if the entity is discontiguous.
template <typename T> MachineBasicBlock *getBottom(const T *Unit) {
  MachineBasicBlock *Bottom = Unit->getHeader();
  for (MachineBasicBlock *MBB : Unit->blocks())
    if (MBB->getNumber() > Bottom->getNumber())
      Bottom = MBB;
  return Bottom;
}

} // end namespace WebAssembly

} // end namespace llvm

#endif
