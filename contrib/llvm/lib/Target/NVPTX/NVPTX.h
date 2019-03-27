//===-- NVPTX.h - Top-level interface for NVPTX representation --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM NVPTX back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTX_H
#define LLVM_LIB_TARGET_NVPTX_NVPTX_H

#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <iosfwd>

namespace llvm {
class NVPTXTargetMachine;
class FunctionPass;
class MachineFunctionPass;
class formatted_raw_ostream;

namespace NVPTXCC {
enum CondCodes {
  EQ,
  NE,
  LT,
  LE,
  GT,
  GE
};
}

FunctionPass *createNVPTXISelDag(NVPTXTargetMachine &TM,
                                 llvm::CodeGenOpt::Level OptLevel);
ModulePass *createNVPTXAssignValidGlobalNamesPass();
ModulePass *createGenericToNVVMPass();
FunctionPass *createNVVMIntrRangePass(unsigned int SmVersion);
FunctionPass *createNVVMReflectPass(unsigned int SmVersion);
MachineFunctionPass *createNVPTXPrologEpilogPass();
MachineFunctionPass *createNVPTXReplaceImageHandlesPass();
FunctionPass *createNVPTXImageOptimizerPass();
FunctionPass *createNVPTXLowerArgsPass(const NVPTXTargetMachine *TM);
BasicBlockPass *createNVPTXLowerAllocaPass();
MachineFunctionPass *createNVPTXPeephole();
MachineFunctionPass *createNVPTXProxyRegErasurePass();

Target &getTheNVPTXTarget32();
Target &getTheNVPTXTarget64();

namespace NVPTX {
enum DrvInterface {
  NVCL,
  CUDA
};

// A field inside TSFlags needs a shift and a mask. The usage is
// always as follows :
// ((TSFlags & fieldMask) >> fieldShift)
// The enum keeps the mask, the shift, and all valid values of the
// field in one place.
enum VecInstType {
  VecInstTypeShift = 0,
  VecInstTypeMask = 0xF,

  VecNOP = 0,
  VecLoad = 1,
  VecStore = 2,
  VecBuild = 3,
  VecShuffle = 4,
  VecExtract = 5,
  VecInsert = 6,
  VecDest = 7,
  VecOther = 15
};

enum SimpleMove {
  SimpleMoveMask = 0x10,
  SimpleMoveShift = 4
};
enum LoadStore {
  isLoadMask = 0x20,
  isLoadShift = 5,
  isStoreMask = 0x40,
  isStoreShift = 6
};

namespace PTXLdStInstCode {
enum AddressSpace {
  GENERIC = 0,
  GLOBAL = 1,
  CONSTANT = 2,
  SHARED = 3,
  PARAM = 4,
  LOCAL = 5
};
enum FromType {
  Unsigned = 0,
  Signed,
  Float,
  Untyped
};
enum VecType {
  Scalar = 1,
  V2 = 2,
  V4 = 4
};
}

/// PTXCvtMode - Conversion code enumeration
namespace PTXCvtMode {
enum CvtMode {
  NONE = 0,
  RNI,
  RZI,
  RMI,
  RPI,
  RN,
  RZ,
  RM,
  RP,

  BASE_MASK = 0x0F,
  FTZ_FLAG = 0x10,
  SAT_FLAG = 0x20
};
}

/// PTXCmpMode - Comparison mode enumeration
namespace PTXCmpMode {
enum CmpMode {
  EQ = 0,
  NE,
  LT,
  LE,
  GT,
  GE,
  LO,
  LS,
  HI,
  HS,
  EQU,
  NEU,
  LTU,
  LEU,
  GTU,
  GEU,
  NUM,
  // NAN is a MACRO
  NotANumber,

  BASE_MASK = 0xFF,
  FTZ_FLAG = 0x100
};
}
}
} // end namespace llvm;

// Defines symbolic names for NVPTX registers.  This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "NVPTXGenRegisterInfo.inc"

// Defines symbolic names for the NVPTX instructions.
#define GET_INSTRINFO_ENUM
#include "NVPTXGenInstrInfo.inc"

#endif
