//===-- X86AsmInstrumentation.cpp - Instrument X86 inline assembly --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "X86AsmInstrumentation.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "X86Operand.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

// Following comment describes how assembly instrumentation works.
// Currently we have only AddressSanitizer instrumentation, but we're
// planning to implement MemorySanitizer for inline assembly too. If
// you're not familiar with AddressSanitizer algorithm, please, read
// https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm
//
// When inline assembly is parsed by an instance of X86AsmParser, all
// instructions are emitted via EmitInstruction method. That's the
// place where X86AsmInstrumentation analyzes an instruction and
// decides, whether the instruction should be emitted as is or
// instrumentation is required. The latter case happens when an
// instruction reads from or writes to memory. Now instruction opcode
// is explicitly checked, and if an instruction has a memory operand
// (for instance, movq (%rsi, %rcx, 8), %rax) - it should be
// instrumented.  There're also exist instructions that modify
// memory but don't have an explicit memory operands, for instance,
// movs.
//
// Let's consider at first 8-byte memory accesses when an instruction
// has an explicit memory operand. In this case we need two registers -
// AddressReg to compute address of a memory cells which are accessed
// and ShadowReg to compute corresponding shadow address. So, we need
// to spill both registers before instrumentation code and restore them
// after instrumentation. Thus, in general, instrumentation code will
// look like this:
// PUSHF  # Store flags, otherwise they will be overwritten
// PUSH AddressReg  # spill AddressReg
// PUSH ShadowReg   # spill ShadowReg
// LEA MemOp, AddressReg  # compute address of the memory operand
// MOV AddressReg, ShadowReg
// SHR ShadowReg, 3
// # ShadowOffset(AddressReg >> 3) contains address of a shadow
// # corresponding to MemOp.
// CMP ShadowOffset(ShadowReg), 0  # test shadow value
// JZ .Done  # when shadow equals to zero, everything is fine
// MOV AddressReg, RDI
// # Call __asan_report function with AddressReg as an argument
// CALL __asan_report
// .Done:
// POP ShadowReg  # Restore ShadowReg
// POP AddressReg  # Restore AddressReg
// POPF  # Restore flags
//
// Memory accesses with different size (1-, 2-, 4- and 16-byte) are
// handled in a similar manner, but small memory accesses (less than 8
// byte) require an additional ScratchReg, which is used for shadow value.
//
// If, suppose, we're instrumenting an instruction like movs, only
// contents of RDI, RDI + AccessSize * RCX, RSI, RSI + AccessSize *
// RCX are checked.  In this case there're no need to spill and restore
// AddressReg , ShadowReg or flags four times, they're saved on stack
// just once, before instrumentation of these four addresses, and restored
// at the end of the instrumentation.
//
// There exist several things which complicate this simple algorithm.
// * Instrumented memory operand can have RSP as a base or an index
//   register.  So we need to add a constant offset before computation
//   of memory address, since flags, AddressReg, ShadowReg, etc. were
//   already stored on stack and RSP was modified.
// * Debug info (usually, DWARF) should be adjusted, because sometimes
//   RSP is used as a frame register. So, we need to select some
//   register as a frame register and temprorary override current CFA
//   register.

using namespace llvm;

static cl::opt<bool> ClAsanInstrumentAssembly(
    "asan-instrument-assembly",
    cl::desc("instrument assembly with AddressSanitizer checks"), cl::Hidden,
    cl::init(false));

static const int64_t MinAllowedDisplacement =
    std::numeric_limits<int32_t>::min();
static const int64_t MaxAllowedDisplacement =
    std::numeric_limits<int32_t>::max();

static int64_t ApplyDisplacementBounds(int64_t Displacement) {
  return std::max(std::min(MaxAllowedDisplacement, Displacement),
                  MinAllowedDisplacement);
}

static void CheckDisplacementBounds(int64_t Displacement) {
  assert(Displacement >= MinAllowedDisplacement &&
         Displacement <= MaxAllowedDisplacement);
}

static bool IsStackReg(unsigned Reg) {
  return Reg == X86::RSP || Reg == X86::ESP;
}

static bool IsSmallMemAccess(unsigned AccessSize) { return AccessSize < 8; }

namespace {

class X86AddressSanitizer : public X86AsmInstrumentation {
public:
  struct RegisterContext {
  private:
    enum RegOffset {
      REG_OFFSET_ADDRESS = 0,
      REG_OFFSET_SHADOW,
      REG_OFFSET_SCRATCH
    };

  public:
    RegisterContext(unsigned AddressReg, unsigned ShadowReg,
                    unsigned ScratchReg) {
      BusyRegs.push_back(convReg(AddressReg, 64));
      BusyRegs.push_back(convReg(ShadowReg, 64));
      BusyRegs.push_back(convReg(ScratchReg, 64));
    }

    unsigned AddressReg(unsigned Size) const {
      return convReg(BusyRegs[REG_OFFSET_ADDRESS], Size);
    }

    unsigned ShadowReg(unsigned Size) const {
      return convReg(BusyRegs[REG_OFFSET_SHADOW], Size);
    }

    unsigned ScratchReg(unsigned Size) const {
      return convReg(BusyRegs[REG_OFFSET_SCRATCH], Size);
    }

    void AddBusyReg(unsigned Reg) {
      if (Reg != X86::NoRegister)
        BusyRegs.push_back(convReg(Reg, 64));
    }

    void AddBusyRegs(const X86Operand &Op) {
      AddBusyReg(Op.getMemBaseReg());
      AddBusyReg(Op.getMemIndexReg());
    }

    unsigned ChooseFrameReg(unsigned Size) const {
      static const MCPhysReg Candidates[] = { X86::RBP, X86::RAX, X86::RBX,
                                              X86::RCX, X86::RDX, X86::RDI,
                                              X86::RSI };
      for (unsigned Reg : Candidates) {
        if (!std::count(BusyRegs.begin(), BusyRegs.end(), Reg))
          return convReg(Reg, Size);
      }
      return X86::NoRegister;
    }

  private:
    unsigned convReg(unsigned Reg, unsigned Size) const {
      return Reg == X86::NoRegister ? Reg : getX86SubSuperRegister(Reg, Size);
    }

    std::vector<unsigned> BusyRegs;
  };

  X86AddressSanitizer(const MCSubtargetInfo *&STI)
      : X86AsmInstrumentation(STI), RepPrefix(false), OrigSPOffset(0) {}

  ~X86AddressSanitizer() override = default;

  // X86AsmInstrumentation implementation:
  void InstrumentAndEmitInstruction(const MCInst &Inst, OperandVector &Operands,
                                    MCContext &Ctx, const MCInstrInfo &MII,
                                    MCStreamer &Out,
                                    /* unused */ bool) override {
    InstrumentMOVS(Inst, Operands, Ctx, MII, Out);
    if (RepPrefix)
      EmitInstruction(Out, MCInstBuilder(X86::REP_PREFIX));

    InstrumentMOV(Inst, Operands, Ctx, MII, Out);

    RepPrefix = (Inst.getOpcode() == X86::REP_PREFIX);
    if (!RepPrefix)
      EmitInstruction(Out, Inst);
  }

  // Adjusts up stack and saves all registers used in instrumentation.
  virtual void InstrumentMemOperandPrologue(const RegisterContext &RegCtx,
                                            MCContext &Ctx,
                                            MCStreamer &Out) = 0;

  // Restores all registers used in instrumentation and adjusts stack.
  virtual void InstrumentMemOperandEpilogue(const RegisterContext &RegCtx,
                                            MCContext &Ctx,
                                            MCStreamer &Out) = 0;

  virtual void InstrumentMemOperandSmall(X86Operand &Op, unsigned AccessSize,
                                         bool IsWrite,
                                         const RegisterContext &RegCtx,
                                         MCContext &Ctx, MCStreamer &Out) = 0;
  virtual void InstrumentMemOperandLarge(X86Operand &Op, unsigned AccessSize,
                                         bool IsWrite,
                                         const RegisterContext &RegCtx,
                                         MCContext &Ctx, MCStreamer &Out) = 0;

  virtual void InstrumentMOVSImpl(unsigned AccessSize, MCContext &Ctx,
                                  MCStreamer &Out) = 0;

  void InstrumentMemOperand(X86Operand &Op, unsigned AccessSize, bool IsWrite,
                            const RegisterContext &RegCtx, MCContext &Ctx,
                            MCStreamer &Out);
  void InstrumentMOVSBase(unsigned DstReg, unsigned SrcReg, unsigned CntReg,
                          unsigned AccessSize, MCContext &Ctx, MCStreamer &Out);

  void InstrumentMOVS(const MCInst &Inst, OperandVector &Operands,
                      MCContext &Ctx, const MCInstrInfo &MII, MCStreamer &Out);
  void InstrumentMOV(const MCInst &Inst, OperandVector &Operands,
                     MCContext &Ctx, const MCInstrInfo &MII, MCStreamer &Out);

protected:
  void EmitLabel(MCStreamer &Out, MCSymbol *Label) { Out.EmitLabel(Label); }

  void EmitLEA(X86Operand &Op, unsigned Size, unsigned Reg, MCStreamer &Out) {
    assert(Size == 32 || Size == 64);
    MCInst Inst;
    Inst.setOpcode(Size == 32 ? X86::LEA32r : X86::LEA64r);
    Inst.addOperand(MCOperand::createReg(getX86SubSuperRegister(Reg, Size)));
    Op.addMemOperands(Inst, 5);
    EmitInstruction(Out, Inst);
  }

  void ComputeMemOperandAddress(X86Operand &Op, unsigned Size,
                                unsigned Reg, MCContext &Ctx, MCStreamer &Out);

  // Creates new memory operand with Displacement added to an original
  // displacement. Residue will contain a residue which could happen when the
  // total displacement exceeds 32-bit limitation.
  std::unique_ptr<X86Operand> AddDisplacement(X86Operand &Op,
                                              int64_t Displacement,
                                              MCContext &Ctx, int64_t *Residue);

  bool is64BitMode() const {
    return STI->getFeatureBits()[X86::Mode64Bit];
  }

  bool is32BitMode() const {
    return STI->getFeatureBits()[X86::Mode32Bit];
  }

  bool is16BitMode() const {
    return STI->getFeatureBits()[X86::Mode16Bit];
  }

  unsigned getPointerWidth() {
    if (is16BitMode()) return 16;
    if (is32BitMode()) return 32;
    if (is64BitMode()) return 64;
    llvm_unreachable("invalid mode");
  }

  // True when previous instruction was actually REP prefix.
  bool RepPrefix;

  // Offset from the original SP register.
  int64_t OrigSPOffset;
};

void X86AddressSanitizer::InstrumentMemOperand(
    X86Operand &Op, unsigned AccessSize, bool IsWrite,
    const RegisterContext &RegCtx, MCContext &Ctx, MCStreamer &Out) {
  assert(Op.isMem() && "Op should be a memory operand.");
  assert((AccessSize & (AccessSize - 1)) == 0 && AccessSize <= 16 &&
         "AccessSize should be a power of two, less or equal than 16.");
  // FIXME: take into account load/store alignment.
  if (IsSmallMemAccess(AccessSize))
    InstrumentMemOperandSmall(Op, AccessSize, IsWrite, RegCtx, Ctx, Out);
  else
    InstrumentMemOperandLarge(Op, AccessSize, IsWrite, RegCtx, Ctx, Out);
}

void X86AddressSanitizer::InstrumentMOVSBase(unsigned DstReg, unsigned SrcReg,
                                             unsigned CntReg,
                                             unsigned AccessSize,
                                             MCContext &Ctx, MCStreamer &Out) {
  // FIXME: check whole ranges [DstReg .. DstReg + AccessSize * (CntReg - 1)]
  // and [SrcReg .. SrcReg + AccessSize * (CntReg - 1)].
  RegisterContext RegCtx(X86::RDX /* AddressReg */, X86::RAX /* ShadowReg */,
                         IsSmallMemAccess(AccessSize)
                             ? X86::RBX
                             : X86::NoRegister /* ScratchReg */);
  RegCtx.AddBusyReg(DstReg);
  RegCtx.AddBusyReg(SrcReg);
  RegCtx.AddBusyReg(CntReg);

  InstrumentMemOperandPrologue(RegCtx, Ctx, Out);

  // Test (%SrcReg)
  {
    const MCExpr *Disp = MCConstantExpr::create(0, Ctx);
    std::unique_ptr<X86Operand> Op(X86Operand::CreateMem(
        getPointerWidth(), 0, Disp, SrcReg, 0, AccessSize, SMLoc(), SMLoc()));
    InstrumentMemOperand(*Op, AccessSize, false /* IsWrite */, RegCtx, Ctx,
                         Out);
  }

  // Test -1(%SrcReg, %CntReg, AccessSize)
  {
    const MCExpr *Disp = MCConstantExpr::create(-1, Ctx);
    std::unique_ptr<X86Operand> Op(X86Operand::CreateMem(
        getPointerWidth(), 0, Disp, SrcReg, CntReg, AccessSize, SMLoc(),
        SMLoc()));
    InstrumentMemOperand(*Op, AccessSize, false /* IsWrite */, RegCtx, Ctx,
                         Out);
  }

  // Test (%DstReg)
  {
    const MCExpr *Disp = MCConstantExpr::create(0, Ctx);
    std::unique_ptr<X86Operand> Op(X86Operand::CreateMem(
        getPointerWidth(), 0, Disp, DstReg, 0, AccessSize, SMLoc(), SMLoc()));
    InstrumentMemOperand(*Op, AccessSize, true /* IsWrite */, RegCtx, Ctx, Out);
  }

  // Test -1(%DstReg, %CntReg, AccessSize)
  {
    const MCExpr *Disp = MCConstantExpr::create(-1, Ctx);
    std::unique_ptr<X86Operand> Op(X86Operand::CreateMem(
        getPointerWidth(), 0, Disp, DstReg, CntReg, AccessSize, SMLoc(),
        SMLoc()));
    InstrumentMemOperand(*Op, AccessSize, true /* IsWrite */, RegCtx, Ctx, Out);
  }

  InstrumentMemOperandEpilogue(RegCtx, Ctx, Out);
}

void X86AddressSanitizer::InstrumentMOVS(const MCInst &Inst,
                                         OperandVector &Operands,
                                         MCContext &Ctx, const MCInstrInfo &MII,
                                         MCStreamer &Out) {
  // Access size in bytes.
  unsigned AccessSize = 0;

  switch (Inst.getOpcode()) {
  case X86::MOVSB:
    AccessSize = 1;
    break;
  case X86::MOVSW:
    AccessSize = 2;
    break;
  case X86::MOVSL:
    AccessSize = 4;
    break;
  case X86::MOVSQ:
    AccessSize = 8;
    break;
  default:
    return;
  }

  InstrumentMOVSImpl(AccessSize, Ctx, Out);
}

void X86AddressSanitizer::InstrumentMOV(const MCInst &Inst,
                                        OperandVector &Operands, MCContext &Ctx,
                                        const MCInstrInfo &MII,
                                        MCStreamer &Out) {
  // Access size in bytes.
  unsigned AccessSize = 0;

  switch (Inst.getOpcode()) {
  case X86::MOV8mi:
  case X86::MOV8mr:
  case X86::MOV8rm:
    AccessSize = 1;
    break;
  case X86::MOV16mi:
  case X86::MOV16mr:
  case X86::MOV16rm:
    AccessSize = 2;
    break;
  case X86::MOV32mi:
  case X86::MOV32mr:
  case X86::MOV32rm:
    AccessSize = 4;
    break;
  case X86::MOV64mi32:
  case X86::MOV64mr:
  case X86::MOV64rm:
    AccessSize = 8;
    break;
  case X86::MOVAPDmr:
  case X86::MOVAPSmr:
  case X86::MOVAPDrm:
  case X86::MOVAPSrm:
    AccessSize = 16;
    break;
  default:
    return;
  }

  const bool IsWrite = MII.get(Inst.getOpcode()).mayStore();

  for (unsigned Ix = 0; Ix < Operands.size(); ++Ix) {
    assert(Operands[Ix]);
    MCParsedAsmOperand &Op = *Operands[Ix];
    if (Op.isMem()) {
      X86Operand &MemOp = static_cast<X86Operand &>(Op);
      RegisterContext RegCtx(
          X86::RDI /* AddressReg */, X86::RAX /* ShadowReg */,
          IsSmallMemAccess(AccessSize) ? X86::RCX
                                       : X86::NoRegister /* ScratchReg */);
      RegCtx.AddBusyRegs(MemOp);
      InstrumentMemOperandPrologue(RegCtx, Ctx, Out);
      InstrumentMemOperand(MemOp, AccessSize, IsWrite, RegCtx, Ctx, Out);
      InstrumentMemOperandEpilogue(RegCtx, Ctx, Out);
    }
  }
}

void X86AddressSanitizer::ComputeMemOperandAddress(X86Operand &Op,
                                                   unsigned Size,
                                                   unsigned Reg, MCContext &Ctx,
                                                   MCStreamer &Out) {
  int64_t Displacement = 0;
  if (IsStackReg(Op.getMemBaseReg()))
    Displacement -= OrigSPOffset;
  if (IsStackReg(Op.getMemIndexReg()))
    Displacement -= OrigSPOffset * Op.getMemScale();

  assert(Displacement >= 0);

  // Emit Op as is.
  if (Displacement == 0) {
    EmitLEA(Op, Size, Reg, Out);
    return;
  }

  int64_t Residue;
  std::unique_ptr<X86Operand> NewOp =
      AddDisplacement(Op, Displacement, Ctx, &Residue);
  EmitLEA(*NewOp, Size, Reg, Out);

  while (Residue != 0) {
    const MCConstantExpr *Disp =
        MCConstantExpr::create(ApplyDisplacementBounds(Residue), Ctx);
    std::unique_ptr<X86Operand> DispOp =
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, Reg, 0, 1, SMLoc(),
                              SMLoc());
    EmitLEA(*DispOp, Size, Reg, Out);
    Residue -= Disp->getValue();
  }
}

std::unique_ptr<X86Operand>
X86AddressSanitizer::AddDisplacement(X86Operand &Op, int64_t Displacement,
                                     MCContext &Ctx, int64_t *Residue) {
  assert(Displacement >= 0);

  if (Displacement == 0 ||
      (Op.getMemDisp() && Op.getMemDisp()->getKind() != MCExpr::Constant)) {
    *Residue = Displacement;
    return X86Operand::CreateMem(Op.getMemModeSize(), Op.getMemSegReg(),
                                 Op.getMemDisp(), Op.getMemBaseReg(),
                                 Op.getMemIndexReg(), Op.getMemScale(),
                                 SMLoc(), SMLoc());
  }

  int64_t OrigDisplacement =
      static_cast<const MCConstantExpr *>(Op.getMemDisp())->getValue();
  CheckDisplacementBounds(OrigDisplacement);
  Displacement += OrigDisplacement;

  int64_t NewDisplacement = ApplyDisplacementBounds(Displacement);
  CheckDisplacementBounds(NewDisplacement);

  *Residue = Displacement - NewDisplacement;
  const MCExpr *Disp = MCConstantExpr::create(NewDisplacement, Ctx);
  return X86Operand::CreateMem(Op.getMemModeSize(), Op.getMemSegReg(), Disp,
                               Op.getMemBaseReg(), Op.getMemIndexReg(),
                               Op.getMemScale(), SMLoc(), SMLoc());
}

class X86AddressSanitizer32 : public X86AddressSanitizer {
public:
  static const long kShadowOffset = 0x20000000;

  X86AddressSanitizer32(const MCSubtargetInfo *&STI)
      : X86AddressSanitizer(STI) {}

  ~X86AddressSanitizer32() override = default;

  unsigned GetFrameReg(const MCContext &Ctx, MCStreamer &Out) {
    unsigned FrameReg = GetFrameRegGeneric(Ctx, Out);
    if (FrameReg == X86::NoRegister)
      return FrameReg;
    return getX86SubSuperRegister(FrameReg, 32);
  }

  void SpillReg(MCStreamer &Out, unsigned Reg) {
    EmitInstruction(Out, MCInstBuilder(X86::PUSH32r).addReg(Reg));
    OrigSPOffset -= 4;
  }

  void RestoreReg(MCStreamer &Out, unsigned Reg) {
    EmitInstruction(Out, MCInstBuilder(X86::POP32r).addReg(Reg));
    OrigSPOffset += 4;
  }

  void StoreFlags(MCStreamer &Out) {
    EmitInstruction(Out, MCInstBuilder(X86::PUSHF32));
    OrigSPOffset -= 4;
  }

  void RestoreFlags(MCStreamer &Out) {
    EmitInstruction(Out, MCInstBuilder(X86::POPF32));
    OrigSPOffset += 4;
  }

  void InstrumentMemOperandPrologue(const RegisterContext &RegCtx,
                                    MCContext &Ctx,
                                    MCStreamer &Out) override {
    unsigned LocalFrameReg = RegCtx.ChooseFrameReg(32);
    assert(LocalFrameReg != X86::NoRegister);

    const MCRegisterInfo *MRI = Ctx.getRegisterInfo();
    unsigned FrameReg = GetFrameReg(Ctx, Out);
    if (MRI && FrameReg != X86::NoRegister) {
      SpillReg(Out, LocalFrameReg);
      if (FrameReg == X86::ESP) {
        Out.EmitCFIAdjustCfaOffset(4 /* byte size of the LocalFrameReg */);
        Out.EmitCFIRelOffset(
            MRI->getDwarfRegNum(LocalFrameReg, true /* IsEH */), 0);
      }
      EmitInstruction(
          Out,
          MCInstBuilder(X86::MOV32rr).addReg(LocalFrameReg).addReg(FrameReg));
      Out.EmitCFIRememberState();
      Out.EmitCFIDefCfaRegister(
          MRI->getDwarfRegNum(LocalFrameReg, true /* IsEH */));
    }

    SpillReg(Out, RegCtx.AddressReg(32));
    SpillReg(Out, RegCtx.ShadowReg(32));
    if (RegCtx.ScratchReg(32) != X86::NoRegister)
      SpillReg(Out, RegCtx.ScratchReg(32));
    StoreFlags(Out);
  }

  void InstrumentMemOperandEpilogue(const RegisterContext &RegCtx,
                                    MCContext &Ctx,
                                    MCStreamer &Out) override {
    unsigned LocalFrameReg = RegCtx.ChooseFrameReg(32);
    assert(LocalFrameReg != X86::NoRegister);

    RestoreFlags(Out);
    if (RegCtx.ScratchReg(32) != X86::NoRegister)
      RestoreReg(Out, RegCtx.ScratchReg(32));
    RestoreReg(Out, RegCtx.ShadowReg(32));
    RestoreReg(Out, RegCtx.AddressReg(32));

    unsigned FrameReg = GetFrameReg(Ctx, Out);
    if (Ctx.getRegisterInfo() && FrameReg != X86::NoRegister) {
      RestoreReg(Out, LocalFrameReg);
      Out.EmitCFIRestoreState();
      if (FrameReg == X86::ESP)
        Out.EmitCFIAdjustCfaOffset(-4 /* byte size of the LocalFrameReg */);
    }
  }

  void InstrumentMemOperandSmall(X86Operand &Op, unsigned AccessSize,
                                 bool IsWrite,
                                 const RegisterContext &RegCtx,
                                 MCContext &Ctx,
                                 MCStreamer &Out) override;
  void InstrumentMemOperandLarge(X86Operand &Op, unsigned AccessSize,
                                 bool IsWrite,
                                 const RegisterContext &RegCtx,
                                 MCContext &Ctx,
                                 MCStreamer &Out) override;
  void InstrumentMOVSImpl(unsigned AccessSize, MCContext &Ctx,
                          MCStreamer &Out) override;

private:
  void EmitCallAsanReport(unsigned AccessSize, bool IsWrite, MCContext &Ctx,
                          MCStreamer &Out, const RegisterContext &RegCtx) {
    EmitInstruction(Out, MCInstBuilder(X86::CLD));
    EmitInstruction(Out, MCInstBuilder(X86::MMX_EMMS));

    EmitInstruction(Out, MCInstBuilder(X86::AND32ri8)
                             .addReg(X86::ESP)
                             .addReg(X86::ESP)
                             .addImm(-16));
    EmitInstruction(
        Out, MCInstBuilder(X86::PUSH32r).addReg(RegCtx.AddressReg(32)));

    MCSymbol *FnSym = Ctx.getOrCreateSymbol(Twine("__asan_report_") +
                                            (IsWrite ? "store" : "load") +
                                            Twine(AccessSize));
    const MCSymbolRefExpr *FnExpr =
        MCSymbolRefExpr::create(FnSym, MCSymbolRefExpr::VK_PLT, Ctx);
    EmitInstruction(Out, MCInstBuilder(X86::CALLpcrel32).addExpr(FnExpr));
  }
};

void X86AddressSanitizer32::InstrumentMemOperandSmall(
    X86Operand &Op, unsigned AccessSize, bool IsWrite,
    const RegisterContext &RegCtx, MCContext &Ctx, MCStreamer &Out) {
  unsigned AddressRegI32 = RegCtx.AddressReg(32);
  unsigned ShadowRegI32 = RegCtx.ShadowReg(32);
  unsigned ShadowRegI8 = RegCtx.ShadowReg(8);

  assert(RegCtx.ScratchReg(32) != X86::NoRegister);
  unsigned ScratchRegI32 = RegCtx.ScratchReg(32);

  ComputeMemOperandAddress(Op, 32, AddressRegI32, Ctx, Out);

  EmitInstruction(Out, MCInstBuilder(X86::MOV32rr).addReg(ShadowRegI32).addReg(
                           AddressRegI32));
  EmitInstruction(Out, MCInstBuilder(X86::SHR32ri)
                           .addReg(ShadowRegI32)
                           .addReg(ShadowRegI32)
                           .addImm(3));

  {
    MCInst Inst;
    Inst.setOpcode(X86::MOV8rm);
    Inst.addOperand(MCOperand::createReg(ShadowRegI8));
    const MCExpr *Disp = MCConstantExpr::create(kShadowOffset, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, ShadowRegI32, 0, 1,
                              SMLoc(), SMLoc()));
    Op->addMemOperands(Inst, 5);
    EmitInstruction(Out, Inst);
  }

  EmitInstruction(
      Out, MCInstBuilder(X86::TEST8rr).addReg(ShadowRegI8).addReg(ShadowRegI8));
  MCSymbol *DoneSym = Ctx.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(DoneSym, Ctx);
  EmitInstruction(Out, MCInstBuilder(X86::JE_1).addExpr(DoneExpr));

  EmitInstruction(Out, MCInstBuilder(X86::MOV32rr).addReg(ScratchRegI32).addReg(
                           AddressRegI32));
  EmitInstruction(Out, MCInstBuilder(X86::AND32ri)
                           .addReg(ScratchRegI32)
                           .addReg(ScratchRegI32)
                           .addImm(7));

  switch (AccessSize) {
  default: llvm_unreachable("Incorrect access size");
  case 1:
    break;
  case 2: {
    const MCExpr *Disp = MCConstantExpr::create(1, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, ScratchRegI32, 0, 1,
                              SMLoc(), SMLoc()));
    EmitLEA(*Op, 32, ScratchRegI32, Out);
    break;
  }
  case 4:
    EmitInstruction(Out, MCInstBuilder(X86::ADD32ri8)
                             .addReg(ScratchRegI32)
                             .addReg(ScratchRegI32)
                             .addImm(3));
    break;
  }

  EmitInstruction(
      Out,
      MCInstBuilder(X86::MOVSX32rr8).addReg(ShadowRegI32).addReg(ShadowRegI8));
  EmitInstruction(Out, MCInstBuilder(X86::CMP32rr).addReg(ScratchRegI32).addReg(
                           ShadowRegI32));
  EmitInstruction(Out, MCInstBuilder(X86::JL_1).addExpr(DoneExpr));

  EmitCallAsanReport(AccessSize, IsWrite, Ctx, Out, RegCtx);
  EmitLabel(Out, DoneSym);
}

void X86AddressSanitizer32::InstrumentMemOperandLarge(
    X86Operand &Op, unsigned AccessSize, bool IsWrite,
    const RegisterContext &RegCtx, MCContext &Ctx, MCStreamer &Out) {
  unsigned AddressRegI32 = RegCtx.AddressReg(32);
  unsigned ShadowRegI32 = RegCtx.ShadowReg(32);

  ComputeMemOperandAddress(Op, 32, AddressRegI32, Ctx, Out);

  EmitInstruction(Out, MCInstBuilder(X86::MOV32rr).addReg(ShadowRegI32).addReg(
                           AddressRegI32));
  EmitInstruction(Out, MCInstBuilder(X86::SHR32ri)
                           .addReg(ShadowRegI32)
                           .addReg(ShadowRegI32)
                           .addImm(3));
  {
    MCInst Inst;
    switch (AccessSize) {
    default: llvm_unreachable("Incorrect access size");
    case 8:
      Inst.setOpcode(X86::CMP8mi);
      break;
    case 16:
      Inst.setOpcode(X86::CMP16mi);
      break;
    }
    const MCExpr *Disp = MCConstantExpr::create(kShadowOffset, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, ShadowRegI32, 0, 1,
                              SMLoc(), SMLoc()));
    Op->addMemOperands(Inst, 5);
    Inst.addOperand(MCOperand::createImm(0));
    EmitInstruction(Out, Inst);
  }
  MCSymbol *DoneSym = Ctx.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(DoneSym, Ctx);
  EmitInstruction(Out, MCInstBuilder(X86::JE_1).addExpr(DoneExpr));

  EmitCallAsanReport(AccessSize, IsWrite, Ctx, Out, RegCtx);
  EmitLabel(Out, DoneSym);
}

void X86AddressSanitizer32::InstrumentMOVSImpl(unsigned AccessSize,
                                               MCContext &Ctx,
                                               MCStreamer &Out) {
  StoreFlags(Out);

  // No need to test when ECX is equals to zero.
  MCSymbol *DoneSym = Ctx.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(DoneSym, Ctx);
  EmitInstruction(
      Out, MCInstBuilder(X86::TEST32rr).addReg(X86::ECX).addReg(X86::ECX));
  EmitInstruction(Out, MCInstBuilder(X86::JE_1).addExpr(DoneExpr));

  // Instrument first and last elements in src and dst range.
  InstrumentMOVSBase(X86::EDI /* DstReg */, X86::ESI /* SrcReg */,
                     X86::ECX /* CntReg */, AccessSize, Ctx, Out);

  EmitLabel(Out, DoneSym);
  RestoreFlags(Out);
}

class X86AddressSanitizer64 : public X86AddressSanitizer {
public:
  static const long kShadowOffset = 0x7fff8000;

  X86AddressSanitizer64(const MCSubtargetInfo *&STI)
      : X86AddressSanitizer(STI) {}

  ~X86AddressSanitizer64() override = default;

  unsigned GetFrameReg(const MCContext &Ctx, MCStreamer &Out) {
    unsigned FrameReg = GetFrameRegGeneric(Ctx, Out);
    if (FrameReg == X86::NoRegister)
      return FrameReg;
    return getX86SubSuperRegister(FrameReg, 64);
  }

  void SpillReg(MCStreamer &Out, unsigned Reg) {
    EmitInstruction(Out, MCInstBuilder(X86::PUSH64r).addReg(Reg));
    OrigSPOffset -= 8;
  }

  void RestoreReg(MCStreamer &Out, unsigned Reg) {
    EmitInstruction(Out, MCInstBuilder(X86::POP64r).addReg(Reg));
    OrigSPOffset += 8;
  }

  void StoreFlags(MCStreamer &Out) {
    EmitInstruction(Out, MCInstBuilder(X86::PUSHF64));
    OrigSPOffset -= 8;
  }

  void RestoreFlags(MCStreamer &Out) {
    EmitInstruction(Out, MCInstBuilder(X86::POPF64));
    OrigSPOffset += 8;
  }

  void InstrumentMemOperandPrologue(const RegisterContext &RegCtx,
                                    MCContext &Ctx,
                                    MCStreamer &Out) override {
    unsigned LocalFrameReg = RegCtx.ChooseFrameReg(64);
    assert(LocalFrameReg != X86::NoRegister);

    const MCRegisterInfo *MRI = Ctx.getRegisterInfo();
    unsigned FrameReg = GetFrameReg(Ctx, Out);
    if (MRI && FrameReg != X86::NoRegister) {
      SpillReg(Out, X86::RBP);
      if (FrameReg == X86::RSP) {
        Out.EmitCFIAdjustCfaOffset(8 /* byte size of the LocalFrameReg */);
        Out.EmitCFIRelOffset(
            MRI->getDwarfRegNum(LocalFrameReg, true /* IsEH */), 0);
      }
      EmitInstruction(
          Out,
          MCInstBuilder(X86::MOV64rr).addReg(LocalFrameReg).addReg(FrameReg));
      Out.EmitCFIRememberState();
      Out.EmitCFIDefCfaRegister(
          MRI->getDwarfRegNum(LocalFrameReg, true /* IsEH */));
    }

    EmitAdjustRSP(Ctx, Out, -128);
    SpillReg(Out, RegCtx.ShadowReg(64));
    SpillReg(Out, RegCtx.AddressReg(64));
    if (RegCtx.ScratchReg(64) != X86::NoRegister)
      SpillReg(Out, RegCtx.ScratchReg(64));
    StoreFlags(Out);
  }

  void InstrumentMemOperandEpilogue(const RegisterContext &RegCtx,
                                    MCContext &Ctx,
                                    MCStreamer &Out) override {
    unsigned LocalFrameReg = RegCtx.ChooseFrameReg(64);
    assert(LocalFrameReg != X86::NoRegister);

    RestoreFlags(Out);
    if (RegCtx.ScratchReg(64) != X86::NoRegister)
      RestoreReg(Out, RegCtx.ScratchReg(64));
    RestoreReg(Out, RegCtx.AddressReg(64));
    RestoreReg(Out, RegCtx.ShadowReg(64));
    EmitAdjustRSP(Ctx, Out, 128);

    unsigned FrameReg = GetFrameReg(Ctx, Out);
    if (Ctx.getRegisterInfo() && FrameReg != X86::NoRegister) {
      RestoreReg(Out, LocalFrameReg);
      Out.EmitCFIRestoreState();
      if (FrameReg == X86::RSP)
        Out.EmitCFIAdjustCfaOffset(-8 /* byte size of the LocalFrameReg */);
    }
  }

  void InstrumentMemOperandSmall(X86Operand &Op, unsigned AccessSize,
                                 bool IsWrite,
                                 const RegisterContext &RegCtx,
                                 MCContext &Ctx,
                                 MCStreamer &Out) override;
  void InstrumentMemOperandLarge(X86Operand &Op, unsigned AccessSize,
                                 bool IsWrite,
                                 const RegisterContext &RegCtx,
                                 MCContext &Ctx,
                                 MCStreamer &Out) override;
  void InstrumentMOVSImpl(unsigned AccessSize, MCContext &Ctx,
                          MCStreamer &Out) override;

private:
  void EmitAdjustRSP(MCContext &Ctx, MCStreamer &Out, long Offset) {
    const MCExpr *Disp = MCConstantExpr::create(Offset, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, X86::RSP, 0, 1,
                              SMLoc(), SMLoc()));
    EmitLEA(*Op, 64, X86::RSP, Out);
    OrigSPOffset += Offset;
  }

  void EmitCallAsanReport(unsigned AccessSize, bool IsWrite, MCContext &Ctx,
                          MCStreamer &Out, const RegisterContext &RegCtx) {
    EmitInstruction(Out, MCInstBuilder(X86::CLD));
    EmitInstruction(Out, MCInstBuilder(X86::MMX_EMMS));

    EmitInstruction(Out, MCInstBuilder(X86::AND64ri8)
                             .addReg(X86::RSP)
                             .addReg(X86::RSP)
                             .addImm(-16));

    if (RegCtx.AddressReg(64) != X86::RDI) {
      EmitInstruction(Out, MCInstBuilder(X86::MOV64rr).addReg(X86::RDI).addReg(
                               RegCtx.AddressReg(64)));
    }
    MCSymbol *FnSym = Ctx.getOrCreateSymbol(Twine("__asan_report_") +
                                            (IsWrite ? "store" : "load") +
                                            Twine(AccessSize));
    const MCSymbolRefExpr *FnExpr =
        MCSymbolRefExpr::create(FnSym, MCSymbolRefExpr::VK_PLT, Ctx);
    EmitInstruction(Out, MCInstBuilder(X86::CALL64pcrel32).addExpr(FnExpr));
  }
};

} // end anonymous namespace

void X86AddressSanitizer64::InstrumentMemOperandSmall(
    X86Operand &Op, unsigned AccessSize, bool IsWrite,
    const RegisterContext &RegCtx, MCContext &Ctx, MCStreamer &Out) {
  unsigned AddressRegI64 = RegCtx.AddressReg(64);
  unsigned AddressRegI32 = RegCtx.AddressReg(32);
  unsigned ShadowRegI64 = RegCtx.ShadowReg(64);
  unsigned ShadowRegI32 = RegCtx.ShadowReg(32);
  unsigned ShadowRegI8 = RegCtx.ShadowReg(8);

  assert(RegCtx.ScratchReg(32) != X86::NoRegister);
  unsigned ScratchRegI32 = RegCtx.ScratchReg(32);

  ComputeMemOperandAddress(Op, 64, AddressRegI64, Ctx, Out);

  EmitInstruction(Out, MCInstBuilder(X86::MOV64rr).addReg(ShadowRegI64).addReg(
                           AddressRegI64));
  EmitInstruction(Out, MCInstBuilder(X86::SHR64ri)
                           .addReg(ShadowRegI64)
                           .addReg(ShadowRegI64)
                           .addImm(3));
  {
    MCInst Inst;
    Inst.setOpcode(X86::MOV8rm);
    Inst.addOperand(MCOperand::createReg(ShadowRegI8));
    const MCExpr *Disp = MCConstantExpr::create(kShadowOffset, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, ShadowRegI64, 0, 1,
                              SMLoc(), SMLoc()));
    Op->addMemOperands(Inst, 5);
    EmitInstruction(Out, Inst);
  }

  EmitInstruction(
      Out, MCInstBuilder(X86::TEST8rr).addReg(ShadowRegI8).addReg(ShadowRegI8));
  MCSymbol *DoneSym = Ctx.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(DoneSym, Ctx);
  EmitInstruction(Out, MCInstBuilder(X86::JE_1).addExpr(DoneExpr));

  EmitInstruction(Out, MCInstBuilder(X86::MOV32rr).addReg(ScratchRegI32).addReg(
                           AddressRegI32));
  EmitInstruction(Out, MCInstBuilder(X86::AND32ri)
                           .addReg(ScratchRegI32)
                           .addReg(ScratchRegI32)
                           .addImm(7));

  switch (AccessSize) {
  default: llvm_unreachable("Incorrect access size");
  case 1:
    break;
  case 2: {
    const MCExpr *Disp = MCConstantExpr::create(1, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, ScratchRegI32, 0, 1,
                              SMLoc(), SMLoc()));
    EmitLEA(*Op, 32, ScratchRegI32, Out);
    break;
  }
  case 4:
    EmitInstruction(Out, MCInstBuilder(X86::ADD32ri8)
                             .addReg(ScratchRegI32)
                             .addReg(ScratchRegI32)
                             .addImm(3));
    break;
  }

  EmitInstruction(
      Out,
      MCInstBuilder(X86::MOVSX32rr8).addReg(ShadowRegI32).addReg(ShadowRegI8));
  EmitInstruction(Out, MCInstBuilder(X86::CMP32rr).addReg(ScratchRegI32).addReg(
                           ShadowRegI32));
  EmitInstruction(Out, MCInstBuilder(X86::JL_1).addExpr(DoneExpr));

  EmitCallAsanReport(AccessSize, IsWrite, Ctx, Out, RegCtx);
  EmitLabel(Out, DoneSym);
}

void X86AddressSanitizer64::InstrumentMemOperandLarge(
    X86Operand &Op, unsigned AccessSize, bool IsWrite,
    const RegisterContext &RegCtx, MCContext &Ctx, MCStreamer &Out) {
  unsigned AddressRegI64 = RegCtx.AddressReg(64);
  unsigned ShadowRegI64 = RegCtx.ShadowReg(64);

  ComputeMemOperandAddress(Op, 64, AddressRegI64, Ctx, Out);

  EmitInstruction(Out, MCInstBuilder(X86::MOV64rr).addReg(ShadowRegI64).addReg(
                           AddressRegI64));
  EmitInstruction(Out, MCInstBuilder(X86::SHR64ri)
                           .addReg(ShadowRegI64)
                           .addReg(ShadowRegI64)
                           .addImm(3));
  {
    MCInst Inst;
    switch (AccessSize) {
    default: llvm_unreachable("Incorrect access size");
    case 8:
      Inst.setOpcode(X86::CMP8mi);
      break;
    case 16:
      Inst.setOpcode(X86::CMP16mi);
      break;
    }
    const MCExpr *Disp = MCConstantExpr::create(kShadowOffset, Ctx);
    std::unique_ptr<X86Operand> Op(
        X86Operand::CreateMem(getPointerWidth(), 0, Disp, ShadowRegI64, 0, 1,
                              SMLoc(), SMLoc()));
    Op->addMemOperands(Inst, 5);
    Inst.addOperand(MCOperand::createImm(0));
    EmitInstruction(Out, Inst);
  }

  MCSymbol *DoneSym = Ctx.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(DoneSym, Ctx);
  EmitInstruction(Out, MCInstBuilder(X86::JE_1).addExpr(DoneExpr));

  EmitCallAsanReport(AccessSize, IsWrite, Ctx, Out, RegCtx);
  EmitLabel(Out, DoneSym);
}

void X86AddressSanitizer64::InstrumentMOVSImpl(unsigned AccessSize,
                                               MCContext &Ctx,
                                               MCStreamer &Out) {
  StoreFlags(Out);

  // No need to test when RCX is equals to zero.
  MCSymbol *DoneSym = Ctx.createTempSymbol();
  const MCExpr *DoneExpr = MCSymbolRefExpr::create(DoneSym, Ctx);
  EmitInstruction(
      Out, MCInstBuilder(X86::TEST64rr).addReg(X86::RCX).addReg(X86::RCX));
  EmitInstruction(Out, MCInstBuilder(X86::JE_1).addExpr(DoneExpr));

  // Instrument first and last elements in src and dst range.
  InstrumentMOVSBase(X86::RDI /* DstReg */, X86::RSI /* SrcReg */,
                     X86::RCX /* CntReg */, AccessSize, Ctx, Out);

  EmitLabel(Out, DoneSym);
  RestoreFlags(Out);
}

X86AsmInstrumentation::X86AsmInstrumentation(const MCSubtargetInfo *&STI)
    : STI(STI) {}

X86AsmInstrumentation::~X86AsmInstrumentation() = default;

void X86AsmInstrumentation::InstrumentAndEmitInstruction(
    const MCInst &Inst, OperandVector &Operands, MCContext &Ctx,
    const MCInstrInfo &MII, MCStreamer &Out, bool PrintSchedInfoEnabled) {
  EmitInstruction(Out, Inst, PrintSchedInfoEnabled);
}

void X86AsmInstrumentation::EmitInstruction(MCStreamer &Out, const MCInst &Inst,
                                            bool PrintSchedInfoEnabled) {
  Out.EmitInstruction(Inst, *STI, PrintSchedInfoEnabled);
}

unsigned X86AsmInstrumentation::GetFrameRegGeneric(const MCContext &Ctx,
                                                   MCStreamer &Out) {
  if (!Out.getNumFrameInfos()) // No active dwarf frame
    return X86::NoRegister;
  const MCDwarfFrameInfo &Frame = Out.getDwarfFrameInfos().back();
  if (Frame.End) // Active dwarf frame is closed
    return X86::NoRegister;
  const MCRegisterInfo *MRI = Ctx.getRegisterInfo();
  if (!MRI) // No register info
    return X86::NoRegister;

  if (InitialFrameReg) {
    // FrameReg is set explicitly, we're instrumenting a MachineFunction.
    return InitialFrameReg;
  }

  return MRI->getLLVMRegNum(Frame.CurrentCfaRegister, true /* IsEH */);
}

X86AsmInstrumentation *
llvm::CreateX86AsmInstrumentation(const MCTargetOptions &MCOptions,
                                  const MCContext &Ctx,
                                  const MCSubtargetInfo *&STI) {
  Triple T(STI->getTargetTriple());
  const bool hasCompilerRTSupport = T.isOSLinux();
  if (ClAsanInstrumentAssembly && hasCompilerRTSupport &&
      MCOptions.SanitizeAddress) {
    if (STI->getFeatureBits()[X86::Mode32Bit] != 0)
      return new X86AddressSanitizer32(STI);
    if (STI->getFeatureBits()[X86::Mode64Bit] != 0)
      return new X86AddressSanitizer64(STI);
  }
  return new X86AsmInstrumentation(STI);
}
