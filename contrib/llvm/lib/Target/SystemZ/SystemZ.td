//===-- SystemZ.td - Describe the SystemZ target machine -----*- tblgen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// SystemZ subtarget features
//===----------------------------------------------------------------------===//

include "SystemZFeatures.td"

//===----------------------------------------------------------------------===//
// SystemZ subtarget scheduling models
//===----------------------------------------------------------------------===//

include "SystemZSchedule.td"

//===----------------------------------------------------------------------===//
// SystemZ supported processors
//===----------------------------------------------------------------------===//

include "SystemZProcessors.td"

//===----------------------------------------------------------------------===//
// Register file description
//===----------------------------------------------------------------------===//

include "SystemZRegisterInfo.td"

//===----------------------------------------------------------------------===//
// Calling convention description
//===----------------------------------------------------------------------===//

include "SystemZCallingConv.td"

//===----------------------------------------------------------------------===//
// Instruction descriptions
//===----------------------------------------------------------------------===//

include "SystemZOperators.td"
include "SystemZOperands.td"
include "SystemZPatterns.td"
include "SystemZInstrFormats.td"
include "SystemZInstrInfo.td"
include "SystemZInstrVector.td"
include "SystemZInstrFP.td"
include "SystemZInstrHFP.td"
include "SystemZInstrDFP.td"
include "SystemZInstrSystem.td"

def SystemZInstrInfo : InstrInfo { let guessInstructionProperties = 0; }

//===----------------------------------------------------------------------===//
// Assembly parser
//===----------------------------------------------------------------------===//

def SystemZAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 0;
}

//===----------------------------------------------------------------------===//
// Top-level target declaration
//===----------------------------------------------------------------------===//

def SystemZ : Target {
  let InstructionSet = SystemZInstrInfo;
  let AssemblyParsers = [SystemZAsmParser];
  let AllowRegisterRenaming = 1;
}
