// WebAssemblyDebugValueManager.h - WebAssembly DebugValue Manager -*- C++ -*-//
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
/// manager for DebugValues associated with the specific MachineInstr.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYDEBUGVALUEMANAGER_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYDEBUGVALUEMANAGER_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class MachineInstr;

class WebAssemblyDebugValueManager {
  SmallVector<MachineInstr *, 2> DbgValues;

public:
  WebAssemblyDebugValueManager(MachineInstr *Instr);

  void move(MachineInstr *Insert);
  void updateReg(unsigned Reg);
  void clone(MachineInstr *Insert, unsigned NewReg);
};

} // end namespace llvm

#endif
