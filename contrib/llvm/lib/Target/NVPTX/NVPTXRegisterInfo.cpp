//===- NVPTXRegisterInfo.cpp - NVPTX Register Information -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the NVPTX implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "NVPTXRegisterInfo.h"
#include "NVPTX.h"
#include "NVPTXSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/MC/MachineLocation.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-reg-info"

namespace llvm {
std::string getNVPTXRegClassName(TargetRegisterClass const *RC) {
  if (RC == &NVPTX::Float32RegsRegClass)
    return ".f32";
  if (RC == &NVPTX::Float16RegsRegClass)
    // Ideally fp16 registers should be .f16, but this syntax is only
    // supported on sm_53+. On the other hand, .b16 registers are
    // accepted for all supported fp16 instructions on all GPU
    // variants, so we can use them instead.
    return ".b16";
  if (RC == &NVPTX::Float16x2RegsRegClass)
    return ".b32";
  if (RC == &NVPTX::Float64RegsRegClass)
    return ".f64";
  if (RC == &NVPTX::Int64RegsRegClass)
    // We use untyped (.b) integer registers here as NVCC does.
    // Correctness of generated code does not depend on register type,
    // but using .s/.u registers runs into ptxas bug that prevents
    // assembly of otherwise valid PTX into SASS. Despite PTX ISA
    // specifying only argument size for fp16 instructions, ptxas does
    // not allow using .s16 or .u16 arguments for .fp16
    // instructions. At the same time it allows using .s32/.u32
    // arguments for .fp16v2 instructions:
    //
    //   .reg .b16 rb16
    //   .reg .s16 rs16
    //   add.f16 rb16,rb16,rb16; // OK
    //   add.f16 rs16,rs16,rs16; // Arguments mismatch for instruction 'add'
    // but:
    //   .reg .b32 rb32
    //   .reg .s32 rs32
    //   add.f16v2 rb32,rb32,rb32; // OK
    //   add.f16v2 rs32,rs32,rs32; // OK
    return ".b64";
  if (RC == &NVPTX::Int32RegsRegClass)
    return ".b32";
  if (RC == &NVPTX::Int16RegsRegClass)
    return ".b16";
  if (RC == &NVPTX::Int1RegsRegClass)
    return ".pred";
  if (RC == &NVPTX::SpecialRegsRegClass)
    return "!Special!";
  return "INTERNAL";
}

std::string getNVPTXRegClassStr(TargetRegisterClass const *RC) {
  if (RC == &NVPTX::Float32RegsRegClass)
    return "%f";
  if (RC == &NVPTX::Float16RegsRegClass)
    return "%h";
  if (RC == &NVPTX::Float16x2RegsRegClass)
    return "%hh";
  if (RC == &NVPTX::Float64RegsRegClass)
    return "%fd";
  if (RC == &NVPTX::Int64RegsRegClass)
    return "%rd";
  if (RC == &NVPTX::Int32RegsRegClass)
    return "%r";
  if (RC == &NVPTX::Int16RegsRegClass)
    return "%rs";
  if (RC == &NVPTX::Int1RegsRegClass)
    return "%p";
  if (RC == &NVPTX::SpecialRegsRegClass)
    return "!Special!";
  return "INTERNAL";
}
}

NVPTXRegisterInfo::NVPTXRegisterInfo() : NVPTXGenRegisterInfo(0) {}

#define GET_REGINFO_TARGET_DESC
#include "NVPTXGenRegisterInfo.inc"

/// NVPTX Callee Saved Registers
const MCPhysReg *
NVPTXRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  static const MCPhysReg CalleeSavedRegs[] = { 0 };
  return CalleeSavedRegs;
}

BitVector NVPTXRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  return Reserved;
}

void NVPTXRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  MachineFunction &MF = *MI.getParent()->getParent();
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex) +
               MI.getOperand(FIOperandNum + 1).getImm();

  // Using I0 as the frame pointer
  MI.getOperand(FIOperandNum).ChangeToRegister(NVPTX::VRFrame, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
}

unsigned NVPTXRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return NVPTX::VRFrame;
}
