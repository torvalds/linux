//===-- AArch64TargetStreamer.h - AArch64 Target Streamer ------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64TARGETSTREAMER_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64TARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace {
class AArch64ELFStreamer;
}

namespace llvm {

class AArch64TargetStreamer : public MCTargetStreamer {
public:
  AArch64TargetStreamer(MCStreamer &S);
  ~AArch64TargetStreamer() override;

  void finish() override;

  /// Callback used to implement the ldr= pseudo.
  /// Add a new entry to the constant pool for the current section and return an
  /// MCExpr that can be used to refer to the constant pool location.
  const MCExpr *addConstantPoolEntry(const MCExpr *, unsigned Size, SMLoc Loc);

  /// Callback used to implemnt the .ltorg directive.
  /// Emit contents of constant pool for the current section.
  void emitCurrentConstantPool();

  /// Callback used to implement the .inst directive.
  virtual void emitInst(uint32_t Inst);

  virtual void EmitARM64WinCFIAllocStack(unsigned Size) {}
  virtual void EmitARM64WinCFISaveFPLR(int Offset) {}
  virtual void EmitARM64WinCFISaveFPLRX(int Offset) {}
  virtual void EmitARM64WinCFISaveReg(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveRegX(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveRegP(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveRegPX(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveFReg(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveFRegX(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveFRegP(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISaveFRegPX(unsigned Reg, int Offset) {}
  virtual void EmitARM64WinCFISetFP() {}
  virtual void EmitARM64WinCFIAddFP(unsigned Size) {}
  virtual void EmitARM64WinCFINop() {}
  virtual void EmitARM64WinCFIPrologEnd() {}
  virtual void EmitARM64WinCFIEpilogStart() {}
  virtual void EmitARM64WinCFIEpilogEnd() {}

private:
  std::unique_ptr<AssemblerConstantPools> ConstantPools;
};

class AArch64TargetELFStreamer : public AArch64TargetStreamer {
private:
  AArch64ELFStreamer &getStreamer();

  void emitInst(uint32_t Inst) override;

public:
  AArch64TargetELFStreamer(MCStreamer &S) : AArch64TargetStreamer(S) {}
};

class AArch64TargetWinCOFFStreamer : public llvm::AArch64TargetStreamer {
private:
  // True if we are processing SEH directives in an epilogue.
  bool InEpilogCFI = false;

  // Symbol of the current epilog for which we are processing SEH directives.
  MCSymbol *CurrentEpilog = nullptr;
public:
  AArch64TargetWinCOFFStreamer(llvm::MCStreamer &S)
    : AArch64TargetStreamer(S) {}

  // The unwind codes on ARM64 Windows are documented at
  // https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
  void EmitARM64WinCFIAllocStack(unsigned Size) override;
  void EmitARM64WinCFISaveFPLR(int Offset) override;
  void EmitARM64WinCFISaveFPLRX(int Offset) override;
  void EmitARM64WinCFISaveReg(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveRegX(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveRegP(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveRegPX(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveFReg(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveFRegX(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveFRegP(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISaveFRegPX(unsigned Reg, int Offset) override;
  void EmitARM64WinCFISetFP() override;
  void EmitARM64WinCFIAddFP(unsigned Size) override;
  void EmitARM64WinCFINop() override;
  void EmitARM64WinCFIPrologEnd() override;
  void EmitARM64WinCFIEpilogStart() override;
  void EmitARM64WinCFIEpilogEnd() override;
private:
  void EmitARM64WinUnwindCode(unsigned UnwindCode, int Reg, int Offset);
};

MCTargetStreamer *
createAArch64ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

} // end namespace llvm

#endif
