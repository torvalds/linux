//===-- llvm/MC/MCInstrInfo.h - Target Instruction Info ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the target machine instruction set.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRINFO_H
#define LLVM_MC_MCINSTRINFO_H

#include "llvm/MC/MCInstrDesc.h"
#include <cassert>

namespace llvm {

//---------------------------------------------------------------------------
/// Interface to description of machine instruction set.
class MCInstrInfo {
  const MCInstrDesc *Desc;          // Raw array to allow static init'n
  const unsigned *InstrNameIndices; // Array for name indices in InstrNameData
  const char *InstrNameData;        // Instruction name string pool
  unsigned NumOpcodes;              // Number of entries in the desc array

public:
  /// Initialize MCInstrInfo, called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  void InitMCInstrInfo(const MCInstrDesc *D, const unsigned *NI, const char *ND,
                       unsigned NO) {
    Desc = D;
    InstrNameIndices = NI;
    InstrNameData = ND;
    NumOpcodes = NO;
  }

  unsigned getNumOpcodes() const { return NumOpcodes; }

  /// Return the machine instruction descriptor that corresponds to the
  /// specified instruction opcode.
  const MCInstrDesc &get(unsigned Opcode) const {
    assert(Opcode < NumOpcodes && "Invalid opcode!");
    return Desc[Opcode];
  }

  /// Returns the name for the instructions with the given opcode.
  StringRef getName(unsigned Opcode) const {
    assert(Opcode < NumOpcodes && "Invalid opcode!");
    return StringRef(&InstrNameData[InstrNameIndices[Opcode]]);
  }
};

} // End llvm namespace

#endif
