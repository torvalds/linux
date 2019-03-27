//===-- MipsTargetStreamer.h - Mips Target Streamer ------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSTARGETSTREAMER_H
#define LLVM_LIB_TARGET_MIPS_MIPSTARGETSTREAMER_H

#include "MCTargetDesc/MipsABIFlagsSection.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"

namespace llvm {

struct MipsABIFlagsSection;

class MipsTargetStreamer : public MCTargetStreamer {
public:
  MipsTargetStreamer(MCStreamer &S);

  virtual void setPic(bool Value) {}

  virtual void emitDirectiveSetMicroMips();
  virtual void emitDirectiveSetNoMicroMips();
  virtual void setUsesMicroMips();
  virtual void emitDirectiveSetMips16();
  virtual void emitDirectiveSetNoMips16();

  virtual void emitDirectiveSetReorder();
  virtual void emitDirectiveSetNoReorder();
  virtual void emitDirectiveSetMacro();
  virtual void emitDirectiveSetNoMacro();
  virtual void emitDirectiveSetMsa();
  virtual void emitDirectiveSetNoMsa();
  virtual void emitDirectiveSetMt();
  virtual void emitDirectiveSetNoMt();
  virtual void emitDirectiveSetCRC();
  virtual void emitDirectiveSetNoCRC();
  virtual void emitDirectiveSetVirt();
  virtual void emitDirectiveSetNoVirt();
  virtual void emitDirectiveSetGINV();
  virtual void emitDirectiveSetNoGINV();
  virtual void emitDirectiveSetAt();
  virtual void emitDirectiveSetAtWithArg(unsigned RegNo);
  virtual void emitDirectiveSetNoAt();
  virtual void emitDirectiveEnd(StringRef Name);

  virtual void emitDirectiveEnt(const MCSymbol &Symbol);
  virtual void emitDirectiveAbiCalls();
  virtual void emitDirectiveNaN2008();
  virtual void emitDirectiveNaNLegacy();
  virtual void emitDirectiveOptionPic0();
  virtual void emitDirectiveOptionPic2();
  virtual void emitDirectiveInsn();
  virtual void emitFrame(unsigned StackReg, unsigned StackSize,
                         unsigned ReturnReg);
  virtual void emitMask(unsigned CPUBitmask, int CPUTopSavedRegOff);
  virtual void emitFMask(unsigned FPUBitmask, int FPUTopSavedRegOff);

  virtual void emitDirectiveSetArch(StringRef Arch);
  virtual void emitDirectiveSetMips0();
  virtual void emitDirectiveSetMips1();
  virtual void emitDirectiveSetMips2();
  virtual void emitDirectiveSetMips3();
  virtual void emitDirectiveSetMips4();
  virtual void emitDirectiveSetMips5();
  virtual void emitDirectiveSetMips32();
  virtual void emitDirectiveSetMips32R2();
  virtual void emitDirectiveSetMips32R3();
  virtual void emitDirectiveSetMips32R5();
  virtual void emitDirectiveSetMips32R6();
  virtual void emitDirectiveSetMips64();
  virtual void emitDirectiveSetMips64R2();
  virtual void emitDirectiveSetMips64R3();
  virtual void emitDirectiveSetMips64R5();
  virtual void emitDirectiveSetMips64R6();
  virtual void emitDirectiveSetDsp();
  virtual void emitDirectiveSetDspr2();
  virtual void emitDirectiveSetNoDsp();
  virtual void emitDirectiveSetPop();
  virtual void emitDirectiveSetPush();
  virtual void emitDirectiveSetSoftFloat();
  virtual void emitDirectiveSetHardFloat();

  // PIC support
  virtual void emitDirectiveCpLoad(unsigned RegNo);
  virtual bool emitDirectiveCpRestore(int Offset,
                                      function_ref<unsigned()> GetATReg,
                                      SMLoc IDLoc, const MCSubtargetInfo *STI);
  virtual void emitDirectiveCpsetup(unsigned RegNo, int RegOrOffset,
                                    const MCSymbol &Sym, bool IsReg);
  virtual void emitDirectiveCpreturn(unsigned SaveLocation,
                                     bool SaveLocationIsRegister);

  // FP abiflags directives
  virtual void emitDirectiveModuleFP();
  virtual void emitDirectiveModuleOddSPReg();
  virtual void emitDirectiveModuleSoftFloat();
  virtual void emitDirectiveModuleHardFloat();
  virtual void emitDirectiveModuleMT();
  virtual void emitDirectiveSetFp(MipsABIFlagsSection::FpABIKind Value);
  virtual void emitDirectiveSetOddSPReg();
  virtual void emitDirectiveSetNoOddSPReg();
  virtual void emitDirectiveModuleCRC();
  virtual void emitDirectiveModuleNoCRC();
  virtual void emitDirectiveModuleVirt();
  virtual void emitDirectiveModuleNoVirt();
  virtual void emitDirectiveModuleGINV();
  virtual void emitDirectiveModuleNoGINV();

  void emitR(unsigned Opcode, unsigned Reg0, SMLoc IDLoc,
             const MCSubtargetInfo *STI);
  void emitII(unsigned Opcode, int16_t Imm1, int16_t Imm2, SMLoc IDLoc,
              const MCSubtargetInfo *STI);
  void emitRX(unsigned Opcode, unsigned Reg0, MCOperand Op1, SMLoc IDLoc,
              const MCSubtargetInfo *STI);
  void emitRI(unsigned Opcode, unsigned Reg0, int32_t Imm, SMLoc IDLoc,
              const MCSubtargetInfo *STI);
  void emitRR(unsigned Opcode, unsigned Reg0, unsigned Reg1, SMLoc IDLoc,
              const MCSubtargetInfo *STI);
  void emitRRX(unsigned Opcode, unsigned Reg0, unsigned Reg1, MCOperand Op2,
               SMLoc IDLoc, const MCSubtargetInfo *STI);
  void emitRRR(unsigned Opcode, unsigned Reg0, unsigned Reg1, unsigned Reg2,
               SMLoc IDLoc, const MCSubtargetInfo *STI);
  void emitRRI(unsigned Opcode, unsigned Reg0, unsigned Reg1, int16_t Imm,
               SMLoc IDLoc, const MCSubtargetInfo *STI);
  void emitRRIII(unsigned Opcode, unsigned Reg0, unsigned Reg1, int16_t Imm0,
                 int16_t Imm1, int16_t Imm2, SMLoc IDLoc,
                 const MCSubtargetInfo *STI);
  void emitAddu(unsigned DstReg, unsigned SrcReg, unsigned TrgReg, bool Is64Bit,
                const MCSubtargetInfo *STI);
  void emitDSLL(unsigned DstReg, unsigned SrcReg, int16_t ShiftAmount,
                SMLoc IDLoc, const MCSubtargetInfo *STI);
  void emitEmptyDelaySlot(bool hasShortDelaySlot, SMLoc IDLoc,
                          const MCSubtargetInfo *STI);
  void emitNop(SMLoc IDLoc, const MCSubtargetInfo *STI);

  /// Emit a store instruction with an offset. If the offset is out of range
  /// then it will be synthesized using the assembler temporary.
  ///
  /// GetATReg() is a callback that can be used to obtain the current assembler
  /// temporary and is only called when the assembler temporary is required. It
  /// must handle the case where no assembler temporary is available (typically
  /// by reporting an error).
  void emitStoreWithImmOffset(unsigned Opcode, unsigned SrcReg,
                              unsigned BaseReg, int64_t Offset,
                              function_ref<unsigned()> GetATReg, SMLoc IDLoc,
                              const MCSubtargetInfo *STI);
  void emitStoreWithSymOffset(unsigned Opcode, unsigned SrcReg,
                              unsigned BaseReg, MCOperand &HiOperand,
                              MCOperand &LoOperand, unsigned ATReg, SMLoc IDLoc,
                              const MCSubtargetInfo *STI);
  void emitLoadWithImmOffset(unsigned Opcode, unsigned DstReg, unsigned BaseReg,
                             int64_t Offset, unsigned TmpReg, SMLoc IDLoc,
                             const MCSubtargetInfo *STI);
  void emitLoadWithSymOffset(unsigned Opcode, unsigned DstReg, unsigned BaseReg,
                             MCOperand &HiOperand, MCOperand &LoOperand,
                             unsigned ATReg, SMLoc IDLoc,
                             const MCSubtargetInfo *STI);
  void emitGPRestore(int Offset, SMLoc IDLoc, const MCSubtargetInfo *STI);

  void forbidModuleDirective() { ModuleDirectiveAllowed = false; }
  void reallowModuleDirective() { ModuleDirectiveAllowed = true; }
  bool isModuleDirectiveAllowed() { return ModuleDirectiveAllowed; }

  // This method enables template classes to set internal abi flags
  // structure values.
  template <class PredicateLibrary>
  void updateABIInfo(const PredicateLibrary &P) {
    ABI = P.getABI();
    ABIFlagsSection.setAllFromPredicates(P);
  }

  MipsABIFlagsSection &getABIFlagsSection() { return ABIFlagsSection; }
  const MipsABIInfo &getABI() const {
    assert(ABI.hasValue() && "ABI hasn't been set!");
    return *ABI;
  }

protected:
  llvm::Optional<MipsABIInfo> ABI;
  MipsABIFlagsSection ABIFlagsSection;

  bool GPRInfoSet;
  unsigned GPRBitMask;
  int GPROffset;

  bool FPRInfoSet;
  unsigned FPRBitMask;
  int FPROffset;

  bool FrameInfoSet;
  int FrameOffset;
  unsigned FrameReg;
  unsigned ReturnReg;

private:
  bool ModuleDirectiveAllowed;
};

// This part is for ascii assembly output
class MipsTargetAsmStreamer : public MipsTargetStreamer {
  formatted_raw_ostream &OS;

public:
  MipsTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
  void emitDirectiveSetMicroMips() override;
  void emitDirectiveSetNoMicroMips() override;
  void emitDirectiveSetMips16() override;
  void emitDirectiveSetNoMips16() override;

  void emitDirectiveSetReorder() override;
  void emitDirectiveSetNoReorder() override;
  void emitDirectiveSetMacro() override;
  void emitDirectiveSetNoMacro() override;
  void emitDirectiveSetMsa() override;
  void emitDirectiveSetNoMsa() override;
  void emitDirectiveSetMt() override;
  void emitDirectiveSetNoMt() override;
  void emitDirectiveSetCRC() override;
  void emitDirectiveSetNoCRC() override;
  void emitDirectiveSetVirt() override;
  void emitDirectiveSetNoVirt() override;
  void emitDirectiveSetGINV() override;
  void emitDirectiveSetNoGINV() override;
  void emitDirectiveSetAt() override;
  void emitDirectiveSetAtWithArg(unsigned RegNo) override;
  void emitDirectiveSetNoAt() override;
  void emitDirectiveEnd(StringRef Name) override;

  void emitDirectiveEnt(const MCSymbol &Symbol) override;
  void emitDirectiveAbiCalls() override;
  void emitDirectiveNaN2008() override;
  void emitDirectiveNaNLegacy() override;
  void emitDirectiveOptionPic0() override;
  void emitDirectiveOptionPic2() override;
  void emitDirectiveInsn() override;
  void emitFrame(unsigned StackReg, unsigned StackSize,
                 unsigned ReturnReg) override;
  void emitMask(unsigned CPUBitmask, int CPUTopSavedRegOff) override;
  void emitFMask(unsigned FPUBitmask, int FPUTopSavedRegOff) override;

  void emitDirectiveSetArch(StringRef Arch) override;
  void emitDirectiveSetMips0() override;
  void emitDirectiveSetMips1() override;
  void emitDirectiveSetMips2() override;
  void emitDirectiveSetMips3() override;
  void emitDirectiveSetMips4() override;
  void emitDirectiveSetMips5() override;
  void emitDirectiveSetMips32() override;
  void emitDirectiveSetMips32R2() override;
  void emitDirectiveSetMips32R3() override;
  void emitDirectiveSetMips32R5() override;
  void emitDirectiveSetMips32R6() override;
  void emitDirectiveSetMips64() override;
  void emitDirectiveSetMips64R2() override;
  void emitDirectiveSetMips64R3() override;
  void emitDirectiveSetMips64R5() override;
  void emitDirectiveSetMips64R6() override;
  void emitDirectiveSetDsp() override;
  void emitDirectiveSetDspr2() override;
  void emitDirectiveSetNoDsp() override;
  void emitDirectiveSetPop() override;
  void emitDirectiveSetPush() override;
  void emitDirectiveSetSoftFloat() override;
  void emitDirectiveSetHardFloat() override;

  // PIC support
  void emitDirectiveCpLoad(unsigned RegNo) override;

  /// Emit a .cprestore directive.  If the offset is out of range then it will
  /// be synthesized using the assembler temporary.
  ///
  /// GetATReg() is a callback that can be used to obtain the current assembler
  /// temporary and is only called when the assembler temporary is required. It
  /// must handle the case where no assembler temporary is available (typically
  /// by reporting an error).
  bool emitDirectiveCpRestore(int Offset, function_ref<unsigned()> GetATReg,
                              SMLoc IDLoc, const MCSubtargetInfo *STI) override;
  void emitDirectiveCpsetup(unsigned RegNo, int RegOrOffset,
                            const MCSymbol &Sym, bool IsReg) override;
  void emitDirectiveCpreturn(unsigned SaveLocation,
                             bool SaveLocationIsRegister) override;

  // FP abiflags directives
  void emitDirectiveModuleFP() override;
  void emitDirectiveModuleOddSPReg() override;
  void emitDirectiveModuleSoftFloat() override;
  void emitDirectiveModuleHardFloat() override;
  void emitDirectiveModuleMT() override;
  void emitDirectiveModuleCRC() override;
  void emitDirectiveModuleNoCRC() override;
  void emitDirectiveModuleVirt() override;
  void emitDirectiveModuleNoVirt() override;
  void emitDirectiveModuleGINV() override;
  void emitDirectiveModuleNoGINV() override;
  void emitDirectiveSetFp(MipsABIFlagsSection::FpABIKind Value) override;
  void emitDirectiveSetOddSPReg() override;
  void emitDirectiveSetNoOddSPReg() override;
};

// This part is for ELF object output
class MipsTargetELFStreamer : public MipsTargetStreamer {
  bool MicroMipsEnabled;
  const MCSubtargetInfo &STI;
  bool Pic;

public:
  bool isMicroMipsEnabled() const { return MicroMipsEnabled; }
  MCELFStreamer &getStreamer();
  MipsTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  void setPic(bool Value) override { Pic = Value; }

  void emitLabel(MCSymbol *Symbol) override;
  void emitAssignment(MCSymbol *Symbol, const MCExpr *Value) override;
  void finish() override;

  void emitDirectiveSetMicroMips() override;
  void emitDirectiveSetNoMicroMips() override;
  void setUsesMicroMips() override;
  void emitDirectiveSetMips16() override;

  void emitDirectiveSetNoReorder() override;
  void emitDirectiveEnd(StringRef Name) override;

  void emitDirectiveEnt(const MCSymbol &Symbol) override;
  void emitDirectiveAbiCalls() override;
  void emitDirectiveNaN2008() override;
  void emitDirectiveNaNLegacy() override;
  void emitDirectiveOptionPic0() override;
  void emitDirectiveOptionPic2() override;
  void emitDirectiveInsn() override;
  void emitFrame(unsigned StackReg, unsigned StackSize,
                 unsigned ReturnReg) override;
  void emitMask(unsigned CPUBitmask, int CPUTopSavedRegOff) override;
  void emitFMask(unsigned FPUBitmask, int FPUTopSavedRegOff) override;

  // PIC support
  void emitDirectiveCpLoad(unsigned RegNo) override;
  bool emitDirectiveCpRestore(int Offset, function_ref<unsigned()> GetATReg,
                              SMLoc IDLoc, const MCSubtargetInfo *STI) override;
  void emitDirectiveCpsetup(unsigned RegNo, int RegOrOffset,
                            const MCSymbol &Sym, bool IsReg) override;
  void emitDirectiveCpreturn(unsigned SaveLocation,
                             bool SaveLocationIsRegister) override;

  void emitMipsAbiFlags();
};
}
#endif
