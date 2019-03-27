//===-- MipsSubtarget.h - Define Subtarget for the Mips ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Mips specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSSUBTARGET_H
#define LLVM_LIB_TARGET_MIPS_MIPSSUBTARGET_H

#include "MCTargetDesc/MipsABIInfo.h"
#include "MipsFrameLowering.h"
#include "MipsISelLowering.h"
#include "MipsInstrInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Support/ErrorHandling.h"
#include <string>

#define GET_SUBTARGETINFO_HEADER
#include "MipsGenSubtargetInfo.inc"

namespace llvm {
class StringRef;

class MipsTargetMachine;

class MipsSubtarget : public MipsGenSubtargetInfo {
  virtual void anchor();

  enum MipsArchEnum {
    MipsDefault,
    Mips1, Mips2, Mips32, Mips32r2, Mips32r3, Mips32r5, Mips32r6, Mips32Max,
    Mips3, Mips4, Mips5, Mips64, Mips64r2, Mips64r3, Mips64r5, Mips64r6
  };

  enum class CPU { P5600 };

  // Used to avoid printing dsp warnings multiple times.
  static bool DspWarningPrinted;

  // Used to avoid printing msa warnings multiple times.
  static bool MSAWarningPrinted;

  // Used to avoid printing crc warnings multiple times.
  static bool CRCWarningPrinted;

  // Used to avoid printing ginv warnings multiple times.
  static bool GINVWarningPrinted;

  // Used to avoid printing virt warnings multiple times.
  static bool VirtWarningPrinted;

  // Mips architecture version
  MipsArchEnum MipsArchVersion;

  // Processor implementation (unused but required to exist by
  // tablegen-erated code).
  CPU ProcImpl;

  // IsLittle - The target is Little Endian
  bool IsLittle;

  // IsSoftFloat - The target does not support any floating point instructions.
  bool IsSoftFloat;

  // IsSingleFloat - The target only supports single precision float
  // point operations. This enable the target to use all 32 32-bit
  // floating point registers instead of only using even ones.
  bool IsSingleFloat;

  // IsFPXX - MIPS O32 modeless ABI.
  bool IsFPXX;

  // NoABICalls - Disable SVR4-style position-independent code.
  bool NoABICalls;

  // IsFP64bit - The target processor has 64-bit floating point registers.
  bool IsFP64bit;

  /// Are odd single-precision registers permitted?
  /// This corresponds to -modd-spreg and -mno-odd-spreg
  bool UseOddSPReg;

  // IsNan2008 - IEEE 754-2008 NaN encoding.
  bool IsNaN2008bit;

  // IsGP64bit - General-purpose registers are 64 bits wide
  bool IsGP64bit;

  // IsPTR64bit - Pointers are 64 bit wide
  bool IsPTR64bit;

  // HasVFPU - Processor has a vector floating point unit.
  bool HasVFPU;

  // CPU supports cnMIPS (Cavium Networks Octeon CPU).
  bool HasCnMips;

  // isLinux - Target system is Linux. Is false we consider ELFOS for now.
  bool IsLinux;

  // UseSmallSection - Small section is used.
  bool UseSmallSection;

  /// Features related to the presence of specific instructions.

  // HasMips3_32 - The subset of MIPS-III instructions added to MIPS32
  bool HasMips3_32;

  // HasMips3_32r2 - The subset of MIPS-III instructions added to MIPS32r2
  bool HasMips3_32r2;

  // HasMips4_32 - Has the subset of MIPS-IV present in MIPS32
  bool HasMips4_32;

  // HasMips4_32r2 - Has the subset of MIPS-IV present in MIPS32r2
  bool HasMips4_32r2;

  // HasMips5_32r2 - Has the subset of MIPS-V present in MIPS32r2
  bool HasMips5_32r2;

  // InMips16 -- can process Mips16 instructions
  bool InMips16Mode;

  // Mips16 hard float
  bool InMips16HardFloat;

  // InMicroMips -- can process MicroMips instructions
  bool InMicroMipsMode;

  // HasDSP, HasDSPR2, HasDSPR3 -- supports DSP ASE.
  bool HasDSP, HasDSPR2, HasDSPR3;

  // Allow mixed Mips16 and Mips32 in one source file
  bool AllowMixed16_32;

  // Optimize for space by compiling all functions as Mips 16 unless
  // it needs floating point. Functions needing floating point are
  // compiled as Mips32
  bool Os16;

  // HasMSA -- supports MSA ASE.
  bool HasMSA;

  // UseTCCInDIV -- Enables the use of trapping in the assembler.
  bool UseTCCInDIV;

  // Sym32 -- On Mips64 symbols are 32 bits.
  bool HasSym32;

  // HasEVA -- supports EVA ASE.
  bool HasEVA;

  // nomadd4 - disables generation of 4-operand madd.s, madd.d and
  // related instructions.
  bool DisableMadd4;

  // HasMT -- support MT ASE.
  bool HasMT;

  // HasCRC -- supports R6 CRC ASE
  bool HasCRC;

  // HasVirt -- supports Virtualization ASE
  bool HasVirt;

  // HasGINV -- supports R6 Global INValidate ASE
  bool HasGINV;

  // Use hazard variants of the jump register instructions for indirect
  // function calls and jump tables.
  bool UseIndirectJumpsHazard;

  // Disable use of the `jal` instruction.
  bool UseLongCalls = false;

  /// The minimum alignment known to hold of the stack frame on
  /// entry to the function and which must be maintained by every function.
  unsigned stackAlignment;

  /// The overridden stack alignment.
  unsigned StackAlignOverride;

  InstrItineraryData InstrItins;

  // We can override the determination of whether we are in mips16 mode
  // as from the command line
  enum {NoOverride, Mips16Override, NoMips16Override} OverrideMode;

  const MipsTargetMachine &TM;

  Triple TargetTriple;

  const SelectionDAGTargetInfo TSInfo;
  std::unique_ptr<const MipsInstrInfo> InstrInfo;
  std::unique_ptr<const MipsFrameLowering> FrameLowering;
  std::unique_ptr<const MipsTargetLowering> TLInfo;

public:
  bool isPositionIndependent() const;
  /// This overrides the PostRAScheduler bit in the SchedModel for each CPU.
  bool enablePostRAScheduler() const override;
  void getCriticalPathRCs(RegClassVector &CriticalPathRCs) const override;
  CodeGenOpt::Level getOptLevelToEnablePostRAScheduler() const override;

  bool isABI_N64() const;
  bool isABI_N32() const;
  bool isABI_O32() const;
  const MipsABIInfo &getABI() const;
  bool isABI_FPXX() const { return isABI_O32() && IsFPXX; }

  /// This constructor initializes the data members to match that
  /// of the specified triple.
  MipsSubtarget(const Triple &TT, StringRef CPU, StringRef FS, bool little,
                const MipsTargetMachine &TM, unsigned StackAlignOverride);

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef FS);

  bool hasMips1() const { return MipsArchVersion >= Mips1; }
  bool hasMips2() const { return MipsArchVersion >= Mips2; }
  bool hasMips3() const { return MipsArchVersion >= Mips3; }
  bool hasMips4() const { return MipsArchVersion >= Mips4; }
  bool hasMips5() const { return MipsArchVersion >= Mips5; }
  bool hasMips4_32() const { return HasMips4_32; }
  bool hasMips4_32r2() const { return HasMips4_32r2; }
  bool hasMips32() const {
    return (MipsArchVersion >= Mips32 && MipsArchVersion < Mips32Max) ||
           hasMips64();
  }
  bool hasMips32r2() const {
    return (MipsArchVersion >= Mips32r2 && MipsArchVersion < Mips32Max) ||
           hasMips64r2();
  }
  bool hasMips32r3() const {
    return (MipsArchVersion >= Mips32r3 && MipsArchVersion < Mips32Max) ||
           hasMips64r2();
  }
  bool hasMips32r5() const {
    return (MipsArchVersion >= Mips32r5 && MipsArchVersion < Mips32Max) ||
           hasMips64r5();
  }
  bool hasMips32r6() const {
    return (MipsArchVersion >= Mips32r6 && MipsArchVersion < Mips32Max) ||
           hasMips64r6();
  }
  bool hasMips64() const { return MipsArchVersion >= Mips64; }
  bool hasMips64r2() const { return MipsArchVersion >= Mips64r2; }
  bool hasMips64r3() const { return MipsArchVersion >= Mips64r3; }
  bool hasMips64r5() const { return MipsArchVersion >= Mips64r5; }
  bool hasMips64r6() const { return MipsArchVersion >= Mips64r6; }

  bool hasCnMips() const { return HasCnMips; }

  bool isLittle() const { return IsLittle; }
  bool isABICalls() const { return !NoABICalls; }
  bool isFPXX() const { return IsFPXX; }
  bool isFP64bit() const { return IsFP64bit; }
  bool useOddSPReg() const { return UseOddSPReg; }
  bool noOddSPReg() const { return !UseOddSPReg; }
  bool isNaN2008() const { return IsNaN2008bit; }
  bool isGP64bit() const { return IsGP64bit; }
  bool isGP32bit() const { return !IsGP64bit; }
  unsigned getGPRSizeInBytes() const { return isGP64bit() ? 8 : 4; }
  bool isPTR64bit() const { return IsPTR64bit; }
  bool isPTR32bit() const { return !IsPTR64bit; }
  bool hasSym32() const {
    return (HasSym32 && isABI_N64()) || isABI_N32() || isABI_O32();
  }
  bool isSingleFloat() const { return IsSingleFloat; }
  bool isTargetELF() const { return TargetTriple.isOSBinFormatELF(); }
  bool hasVFPU() const { return HasVFPU; }
  bool inMips16Mode() const { return InMips16Mode; }
  bool inMips16ModeDefault() const {
    return InMips16Mode;
  }
  // Hard float for mips16 means essentially to compile as soft float
  // but to use a runtime library for soft float that is written with
  // native mips32 floating point instructions (those runtime routines
  // run in mips32 hard float mode).
  bool inMips16HardFloat() const {
    return inMips16Mode() && InMips16HardFloat;
  }
  bool inMicroMipsMode() const { return InMicroMipsMode && !InMips16Mode; }
  bool inMicroMips32r6Mode() const {
    return inMicroMipsMode() && hasMips32r6();
  }
  bool hasDSP() const { return HasDSP; }
  bool hasDSPR2() const { return HasDSPR2; }
  bool hasDSPR3() const { return HasDSPR3; }
  bool hasMSA() const { return HasMSA; }
  bool disableMadd4() const { return DisableMadd4; }
  bool hasEVA() const { return HasEVA; }
  bool hasMT() const { return HasMT; }
  bool hasCRC() const { return HasCRC; }
  bool hasVirt() const { return HasVirt; }
  bool hasGINV() const { return HasGINV; }
  bool useIndirectJumpsHazard() const {
    return UseIndirectJumpsHazard && hasMips32r2();
  }
  bool useSmallSection() const { return UseSmallSection; }

  bool hasStandardEncoding() const { return !InMips16Mode && !InMicroMipsMode; }

  bool useSoftFloat() const { return IsSoftFloat; }

  bool useLongCalls() const { return UseLongCalls; }

  bool enableLongBranchPass() const {
    return hasStandardEncoding() || inMicroMipsMode() || allowMixed16_32();
  }

  /// Features related to the presence of specific instructions.
  bool hasExtractInsert() const { return !inMips16Mode() && hasMips32r2(); }
  bool hasMTHC1() const { return hasMips32r2(); }

  bool allowMixed16_32() const { return inMips16ModeDefault() |
                                        AllowMixed16_32; }

  bool os16() const { return Os16; }

  bool isTargetNaCl() const { return TargetTriple.isOSNaCl(); }

  bool isXRaySupported() const override { return true; }

  // for now constant islands are on for the whole compilation unit but we only
  // really use them if in addition we are in mips16 mode
  static bool useConstantIslands();

  unsigned getStackAlignment() const { return stackAlignment; }

  // Grab relocation model
  Reloc::Model getRelocationModel() const;

  MipsSubtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS,
                                                 const TargetMachine &TM);

  /// Does the system support unaligned memory access.
  ///
  /// MIPS32r6/MIPS64r6 require full unaligned access support but does not
  /// specify which component of the system provides it. Hardware, software, and
  /// hybrid implementations are all valid.
  bool systemSupportsUnalignedAccess() const { return hasMips32r6(); }

  // Set helper classes
  void setHelperClassesMips16();
  void setHelperClassesMipsSE();

  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  const MipsInstrInfo *getInstrInfo() const override { return InstrInfo.get(); }
  const TargetFrameLowering *getFrameLowering() const override {
    return FrameLowering.get();
  }
  const MipsRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo->getRegisterInfo();
  }
  const MipsTargetLowering *getTargetLowering() const override {
    return TLInfo.get();
  }
  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }

protected:
  // GlobalISel related APIs.
  std::unique_ptr<CallLowering> CallLoweringInfo;
  std::unique_ptr<LegalizerInfo> Legalizer;
  std::unique_ptr<RegisterBankInfo> RegBankInfo;
  std::unique_ptr<InstructionSelector> InstSelector;

public:
  const CallLowering *getCallLowering() const override;
  const LegalizerInfo *getLegalizerInfo() const override;
  const RegisterBankInfo *getRegBankInfo() const override;
  const InstructionSelector *getInstructionSelector() const override;
};
} // End llvm namespace

#endif
