//===-- X86AsmBackend.cpp - X86 Assembler Backend -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86FixupKinds.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static unsigned getFixupKindLog2Size(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("invalid fixup kind!");
  case FK_PCRel_1:
  case FK_SecRel_1:
  case FK_Data_1:
    return 0;
  case FK_PCRel_2:
  case FK_SecRel_2:
  case FK_Data_2:
    return 1;
  case FK_PCRel_4:
  case X86::reloc_riprel_4byte:
  case X86::reloc_riprel_4byte_relax:
  case X86::reloc_riprel_4byte_relax_rex:
  case X86::reloc_riprel_4byte_movq_load:
  case X86::reloc_signed_4byte:
  case X86::reloc_signed_4byte_relax:
  case X86::reloc_global_offset_table:
  case X86::reloc_branch_4byte_pcrel:
  case FK_SecRel_4:
  case FK_Data_4:
    return 2;
  case FK_PCRel_8:
  case FK_SecRel_8:
  case FK_Data_8:
  case X86::reloc_global_offset_table8:
    return 3;
  }
}

namespace {

class X86ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  X86ELFObjectWriter(bool is64Bit, uint8_t OSABI, uint16_t EMachine,
                     bool HasRelocationAddend, bool foobar)
    : MCELFObjectTargetWriter(is64Bit, OSABI, EMachine, HasRelocationAddend) {}
};

class X86AsmBackend : public MCAsmBackend {
  const MCSubtargetInfo &STI;
public:
  X86AsmBackend(const Target &T, const MCSubtargetInfo &STI)
      : MCAsmBackend(support::little), STI(STI) {}

  unsigned getNumFixupKinds() const override {
    return X86::NumTargetFixupKinds;
  }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override {
    const static MCFixupKindInfo Infos[X86::NumTargetFixupKinds] = {
        {"reloc_riprel_4byte", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"reloc_riprel_4byte_movq_load", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"reloc_riprel_4byte_relax", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"reloc_riprel_4byte_relax_rex", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
        {"reloc_signed_4byte", 0, 32, 0},
        {"reloc_signed_4byte_relax", 0, 32, 0},
        {"reloc_global_offset_table", 0, 32, 0},
        {"reloc_global_offset_table8", 0, 64, 0},
        {"reloc_branch_4byte_pcrel", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
    };

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);

    assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
           "Invalid kind!");
    assert(Infos[Kind - FirstTargetFixupKind].Name && "Empty fixup name!");
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override {
    unsigned Size = 1 << getFixupKindLog2Size(Fixup.getKind());

    assert(Fixup.getOffset() + Size <= Data.size() && "Invalid fixup offset!");

    // Check that uppper bits are either all zeros or all ones.
    // Specifically ignore overflow/underflow as long as the leakage is
    // limited to the lower bits. This is to remain compatible with
    // other assemblers.
    assert(isIntN(Size * 8 + 1, Value) &&
           "Value does not fit in the Fixup field");

    for (unsigned i = 0; i != Size; ++i)
      Data[Fixup.getOffset() + i] = uint8_t(Value >> (i * 8));
  }

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override;

  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override;

  void relaxInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                        MCInst &Res) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count) const override;
};
} // end anonymous namespace

static unsigned getRelaxedOpcodeBranch(const MCInst &Inst, bool is16BitMode) {
  unsigned Op = Inst.getOpcode();
  switch (Op) {
  default:
    return Op;
  case X86::JAE_1:
    return (is16BitMode) ? X86::JAE_2 : X86::JAE_4;
  case X86::JA_1:
    return (is16BitMode) ? X86::JA_2 : X86::JA_4;
  case X86::JBE_1:
    return (is16BitMode) ? X86::JBE_2 : X86::JBE_4;
  case X86::JB_1:
    return (is16BitMode) ? X86::JB_2 : X86::JB_4;
  case X86::JE_1:
    return (is16BitMode) ? X86::JE_2 : X86::JE_4;
  case X86::JGE_1:
    return (is16BitMode) ? X86::JGE_2 : X86::JGE_4;
  case X86::JG_1:
    return (is16BitMode) ? X86::JG_2 : X86::JG_4;
  case X86::JLE_1:
    return (is16BitMode) ? X86::JLE_2 : X86::JLE_4;
  case X86::JL_1:
    return (is16BitMode) ? X86::JL_2 : X86::JL_4;
  case X86::JMP_1:
    return (is16BitMode) ? X86::JMP_2 : X86::JMP_4;
  case X86::JNE_1:
    return (is16BitMode) ? X86::JNE_2 : X86::JNE_4;
  case X86::JNO_1:
    return (is16BitMode) ? X86::JNO_2 : X86::JNO_4;
  case X86::JNP_1:
    return (is16BitMode) ? X86::JNP_2 : X86::JNP_4;
  case X86::JNS_1:
    return (is16BitMode) ? X86::JNS_2 : X86::JNS_4;
  case X86::JO_1:
    return (is16BitMode) ? X86::JO_2 : X86::JO_4;
  case X86::JP_1:
    return (is16BitMode) ? X86::JP_2 : X86::JP_4;
  case X86::JS_1:
    return (is16BitMode) ? X86::JS_2 : X86::JS_4;
  }
}

static unsigned getRelaxedOpcodeArith(const MCInst &Inst) {
  unsigned Op = Inst.getOpcode();
  switch (Op) {
  default:
    return Op;

    // IMUL
  case X86::IMUL16rri8: return X86::IMUL16rri;
  case X86::IMUL16rmi8: return X86::IMUL16rmi;
  case X86::IMUL32rri8: return X86::IMUL32rri;
  case X86::IMUL32rmi8: return X86::IMUL32rmi;
  case X86::IMUL64rri8: return X86::IMUL64rri32;
  case X86::IMUL64rmi8: return X86::IMUL64rmi32;

    // AND
  case X86::AND16ri8: return X86::AND16ri;
  case X86::AND16mi8: return X86::AND16mi;
  case X86::AND32ri8: return X86::AND32ri;
  case X86::AND32mi8: return X86::AND32mi;
  case X86::AND64ri8: return X86::AND64ri32;
  case X86::AND64mi8: return X86::AND64mi32;

    // OR
  case X86::OR16ri8: return X86::OR16ri;
  case X86::OR16mi8: return X86::OR16mi;
  case X86::OR32ri8: return X86::OR32ri;
  case X86::OR32mi8: return X86::OR32mi;
  case X86::OR64ri8: return X86::OR64ri32;
  case X86::OR64mi8: return X86::OR64mi32;

    // XOR
  case X86::XOR16ri8: return X86::XOR16ri;
  case X86::XOR16mi8: return X86::XOR16mi;
  case X86::XOR32ri8: return X86::XOR32ri;
  case X86::XOR32mi8: return X86::XOR32mi;
  case X86::XOR64ri8: return X86::XOR64ri32;
  case X86::XOR64mi8: return X86::XOR64mi32;

    // ADD
  case X86::ADD16ri8: return X86::ADD16ri;
  case X86::ADD16mi8: return X86::ADD16mi;
  case X86::ADD32ri8: return X86::ADD32ri;
  case X86::ADD32mi8: return X86::ADD32mi;
  case X86::ADD64ri8: return X86::ADD64ri32;
  case X86::ADD64mi8: return X86::ADD64mi32;

   // ADC
  case X86::ADC16ri8: return X86::ADC16ri;
  case X86::ADC16mi8: return X86::ADC16mi;
  case X86::ADC32ri8: return X86::ADC32ri;
  case X86::ADC32mi8: return X86::ADC32mi;
  case X86::ADC64ri8: return X86::ADC64ri32;
  case X86::ADC64mi8: return X86::ADC64mi32;

    // SUB
  case X86::SUB16ri8: return X86::SUB16ri;
  case X86::SUB16mi8: return X86::SUB16mi;
  case X86::SUB32ri8: return X86::SUB32ri;
  case X86::SUB32mi8: return X86::SUB32mi;
  case X86::SUB64ri8: return X86::SUB64ri32;
  case X86::SUB64mi8: return X86::SUB64mi32;

   // SBB
  case X86::SBB16ri8: return X86::SBB16ri;
  case X86::SBB16mi8: return X86::SBB16mi;
  case X86::SBB32ri8: return X86::SBB32ri;
  case X86::SBB32mi8: return X86::SBB32mi;
  case X86::SBB64ri8: return X86::SBB64ri32;
  case X86::SBB64mi8: return X86::SBB64mi32;

    // CMP
  case X86::CMP16ri8: return X86::CMP16ri;
  case X86::CMP16mi8: return X86::CMP16mi;
  case X86::CMP32ri8: return X86::CMP32ri;
  case X86::CMP32mi8: return X86::CMP32mi;
  case X86::CMP64ri8: return X86::CMP64ri32;
  case X86::CMP64mi8: return X86::CMP64mi32;

    // PUSH
  case X86::PUSH32i8:  return X86::PUSHi32;
  case X86::PUSH16i8:  return X86::PUSHi16;
  case X86::PUSH64i8:  return X86::PUSH64i32;
  }
}

static unsigned getRelaxedOpcode(const MCInst &Inst, bool is16BitMode) {
  unsigned R = getRelaxedOpcodeArith(Inst);
  if (R != Inst.getOpcode())
    return R;
  return getRelaxedOpcodeBranch(Inst, is16BitMode);
}

bool X86AsmBackend::mayNeedRelaxation(const MCInst &Inst,
                                      const MCSubtargetInfo &STI) const {
  // Branches can always be relaxed in either mode.
  if (getRelaxedOpcodeBranch(Inst, false) != Inst.getOpcode())
    return true;

  // Check if this instruction is ever relaxable.
  if (getRelaxedOpcodeArith(Inst) == Inst.getOpcode())
    return false;


  // Check if the relaxable operand has an expression. For the current set of
  // relaxable instructions, the relaxable operand is always the last operand.
  unsigned RelaxableOp = Inst.getNumOperands() - 1;
  if (Inst.getOperand(RelaxableOp).isExpr())
    return true;

  return false;
}

bool X86AsmBackend::fixupNeedsRelaxation(const MCFixup &Fixup,
                                         uint64_t Value,
                                         const MCRelaxableFragment *DF,
                                         const MCAsmLayout &Layout) const {
  // Relax if the value is too big for a (signed) i8.
  return int64_t(Value) != int64_t(int8_t(Value));
}

// FIXME: Can tblgen help at all here to verify there aren't other instructions
// we can relax?
void X86AsmBackend::relaxInstruction(const MCInst &Inst,
                                     const MCSubtargetInfo &STI,
                                     MCInst &Res) const {
  // The only relaxations X86 does is from a 1byte pcrel to a 4byte pcrel.
  bool is16BitMode = STI.getFeatureBits()[X86::Mode16Bit];
  unsigned RelaxedOp = getRelaxedOpcode(Inst, is16BitMode);

  if (RelaxedOp == Inst.getOpcode()) {
    SmallString<256> Tmp;
    raw_svector_ostream OS(Tmp);
    Inst.dump_pretty(OS);
    OS << "\n";
    report_fatal_error("unexpected instruction to relax: " + OS.str());
  }

  Res = Inst;
  Res.setOpcode(RelaxedOp);
}

/// Write a sequence of optimal nops to the output, covering \p Count
/// bytes.
/// \return - true on success, false on failure
bool X86AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count) const {
  static const char Nops[10][11] = {
    // nop
    "\x90",
    // xchg %ax,%ax
    "\x66\x90",
    // nopl (%[re]ax)
    "\x0f\x1f\x00",
    // nopl 0(%[re]ax)
    "\x0f\x1f\x40\x00",
    // nopl 0(%[re]ax,%[re]ax,1)
    "\x0f\x1f\x44\x00\x00",
    // nopw 0(%[re]ax,%[re]ax,1)
    "\x66\x0f\x1f\x44\x00\x00",
    // nopl 0L(%[re]ax)
    "\x0f\x1f\x80\x00\x00\x00\x00",
    // nopl 0L(%[re]ax,%[re]ax,1)
    "\x0f\x1f\x84\x00\x00\x00\x00\x00",
    // nopw 0L(%[re]ax,%[re]ax,1)
    "\x66\x0f\x1f\x84\x00\x00\x00\x00\x00",
    // nopw %cs:0L(%[re]ax,%[re]ax,1)
    "\x66\x2e\x0f\x1f\x84\x00\x00\x00\x00\x00",
  };

  // This CPU doesn't support long nops. If needed add more.
  // FIXME: We could generated something better than plain 0x90.
  if (!STI.getFeatureBits()[X86::FeatureNOPL]) {
    for (uint64_t i = 0; i < Count; ++i)
      OS << '\x90';
    return true;
  }

  // 15-bytes is the longest single NOP instruction, but 10-bytes is
  // commonly the longest that can be efficiently decoded.
  uint64_t MaxNopLength = 10;
  if (STI.getFeatureBits()[X86::ProcIntelSLM])
    MaxNopLength = 7;
  else if (STI.getFeatureBits()[X86::FeatureFast15ByteNOP])
    MaxNopLength = 15;
  else if (STI.getFeatureBits()[X86::FeatureFast11ByteNOP])
    MaxNopLength = 11;

  // Emit as many MaxNopLength NOPs as needed, then emit a NOP of the remaining
  // length.
  do {
    const uint8_t ThisNopLength = (uint8_t) std::min(Count, MaxNopLength);
    const uint8_t Prefixes = ThisNopLength <= 10 ? 0 : ThisNopLength - 10;
    for (uint8_t i = 0; i < Prefixes; i++)
      OS << '\x66';
    const uint8_t Rest = ThisNopLength - Prefixes;
    if (Rest != 0)
      OS.write(Nops[Rest - 1], Rest);
    Count -= ThisNopLength;
  } while (Count != 0);

  return true;
}

/* *** */

namespace {

class ELFX86AsmBackend : public X86AsmBackend {
public:
  uint8_t OSABI;
  ELFX86AsmBackend(const Target &T, uint8_t OSABI, const MCSubtargetInfo &STI)
      : X86AsmBackend(T, STI), OSABI(OSABI) {}
};

class ELFX86_32AsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_32AsmBackend(const Target &T, uint8_t OSABI,
                      const MCSubtargetInfo &STI)
    : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ false, OSABI, ELF::EM_386);
  }
};

class ELFX86_X32AsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_X32AsmBackend(const Target &T, uint8_t OSABI,
                       const MCSubtargetInfo &STI)
      : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ false, OSABI,
                                    ELF::EM_X86_64);
  }
};

class ELFX86_IAMCUAsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_IAMCUAsmBackend(const Target &T, uint8_t OSABI,
                         const MCSubtargetInfo &STI)
      : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ false, OSABI,
                                    ELF::EM_IAMCU);
  }
};

class ELFX86_64AsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_64AsmBackend(const Target &T, uint8_t OSABI,
                      const MCSubtargetInfo &STI)
    : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ true, OSABI, ELF::EM_X86_64);
  }
};

class WindowsX86AsmBackend : public X86AsmBackend {
  bool Is64Bit;

public:
  WindowsX86AsmBackend(const Target &T, bool is64Bit,
                       const MCSubtargetInfo &STI)
    : X86AsmBackend(T, STI)
    , Is64Bit(is64Bit) {
  }

  Optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    return StringSwitch<Optional<MCFixupKind>>(Name)
        .Case("dir32", FK_Data_4)
        .Case("secrel32", FK_SecRel_4)
        .Case("secidx", FK_SecRel_2)
        .Default(MCAsmBackend::getFixupKind(Name));
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86WinCOFFObjectWriter(Is64Bit);
  }
};

namespace CU {

  /// Compact unwind encoding values.
  enum CompactUnwindEncodings {
    /// [RE]BP based frame where [RE]BP is pused on the stack immediately after
    /// the return address, then [RE]SP is moved to [RE]BP.
    UNWIND_MODE_BP_FRAME                   = 0x01000000,

    /// A frameless function with a small constant stack size.
    UNWIND_MODE_STACK_IMMD                 = 0x02000000,

    /// A frameless function with a large constant stack size.
    UNWIND_MODE_STACK_IND                  = 0x03000000,

    /// No compact unwind encoding is available.
    UNWIND_MODE_DWARF                      = 0x04000000,

    /// Mask for encoding the frame registers.
    UNWIND_BP_FRAME_REGISTERS              = 0x00007FFF,

    /// Mask for encoding the frameless registers.
    UNWIND_FRAMELESS_STACK_REG_PERMUTATION = 0x000003FF
  };

} // end CU namespace

class DarwinX86AsmBackend : public X86AsmBackend {
  const MCRegisterInfo &MRI;

  /// Number of registers that can be saved in a compact unwind encoding.
  enum { CU_NUM_SAVED_REGS = 6 };

  mutable unsigned SavedRegs[CU_NUM_SAVED_REGS];
  bool Is64Bit;

  unsigned OffsetSize;                   ///< Offset of a "push" instruction.
  unsigned MoveInstrSize;                ///< Size of a "move" instruction.
  unsigned StackDivide;                  ///< Amount to adjust stack size by.
protected:
  /// Size of a "push" instruction for the given register.
  unsigned PushInstrSize(unsigned Reg) const {
    switch (Reg) {
      case X86::EBX:
      case X86::ECX:
      case X86::EDX:
      case X86::EDI:
      case X86::ESI:
      case X86::EBP:
      case X86::RBX:
      case X86::RBP:
        return 1;
      case X86::R12:
      case X86::R13:
      case X86::R14:
      case X86::R15:
        return 2;
    }
    return 1;
  }

  /// Implementation of algorithm to generate the compact unwind encoding
  /// for the CFI instructions.
  uint32_t
  generateCompactUnwindEncodingImpl(ArrayRef<MCCFIInstruction> Instrs) const {
    if (Instrs.empty()) return 0;

    // Reset the saved registers.
    unsigned SavedRegIdx = 0;
    memset(SavedRegs, 0, sizeof(SavedRegs));

    bool HasFP = false;

    // Encode that we are using EBP/RBP as the frame pointer.
    uint32_t CompactUnwindEncoding = 0;

    unsigned SubtractInstrIdx = Is64Bit ? 3 : 2;
    unsigned InstrOffset = 0;
    unsigned StackAdjust = 0;
    unsigned StackSize = 0;
    unsigned NumDefCFAOffsets = 0;

    for (unsigned i = 0, e = Instrs.size(); i != e; ++i) {
      const MCCFIInstruction &Inst = Instrs[i];

      switch (Inst.getOperation()) {
      default:
        // Any other CFI directives indicate a frame that we aren't prepared
        // to represent via compact unwind, so just bail out.
        return 0;
      case MCCFIInstruction::OpDefCfaRegister: {
        // Defines a frame pointer. E.g.
        //
        //     movq %rsp, %rbp
        //  L0:
        //     .cfi_def_cfa_register %rbp
        //
        HasFP = true;

        // If the frame pointer is other than esp/rsp, we do not have a way to
        // generate a compact unwinding representation, so bail out.
        if (MRI.getLLVMRegNum(Inst.getRegister(), true) !=
            (Is64Bit ? X86::RBP : X86::EBP))
          return 0;

        // Reset the counts.
        memset(SavedRegs, 0, sizeof(SavedRegs));
        StackAdjust = 0;
        SavedRegIdx = 0;
        InstrOffset += MoveInstrSize;
        break;
      }
      case MCCFIInstruction::OpDefCfaOffset: {
        // Defines a new offset for the CFA. E.g.
        //
        //  With frame:
        //
        //     pushq %rbp
        //  L0:
        //     .cfi_def_cfa_offset 16
        //
        //  Without frame:
        //
        //     subq $72, %rsp
        //  L0:
        //     .cfi_def_cfa_offset 80
        //
        StackSize = std::abs(Inst.getOffset()) / StackDivide;
        ++NumDefCFAOffsets;
        break;
      }
      case MCCFIInstruction::OpOffset: {
        // Defines a "push" of a callee-saved register. E.g.
        //
        //     pushq %r15
        //     pushq %r14
        //     pushq %rbx
        //  L0:
        //     subq $120, %rsp
        //  L1:
        //     .cfi_offset %rbx, -40
        //     .cfi_offset %r14, -32
        //     .cfi_offset %r15, -24
        //
        if (SavedRegIdx == CU_NUM_SAVED_REGS)
          // If there are too many saved registers, we cannot use a compact
          // unwind encoding.
          return CU::UNWIND_MODE_DWARF;

        unsigned Reg = MRI.getLLVMRegNum(Inst.getRegister(), true);
        SavedRegs[SavedRegIdx++] = Reg;
        StackAdjust += OffsetSize;
        InstrOffset += PushInstrSize(Reg);
        break;
      }
      }
    }

    StackAdjust /= StackDivide;

    if (HasFP) {
      if ((StackAdjust & 0xFF) != StackAdjust)
        // Offset was too big for a compact unwind encoding.
        return CU::UNWIND_MODE_DWARF;

      // Get the encoding of the saved registers when we have a frame pointer.
      uint32_t RegEnc = encodeCompactUnwindRegistersWithFrame();
      if (RegEnc == ~0U) return CU::UNWIND_MODE_DWARF;

      CompactUnwindEncoding |= CU::UNWIND_MODE_BP_FRAME;
      CompactUnwindEncoding |= (StackAdjust & 0xFF) << 16;
      CompactUnwindEncoding |= RegEnc & CU::UNWIND_BP_FRAME_REGISTERS;
    } else {
      SubtractInstrIdx += InstrOffset;
      ++StackAdjust;

      if ((StackSize & 0xFF) == StackSize) {
        // Frameless stack with a small stack size.
        CompactUnwindEncoding |= CU::UNWIND_MODE_STACK_IMMD;

        // Encode the stack size.
        CompactUnwindEncoding |= (StackSize & 0xFF) << 16;
      } else {
        if ((StackAdjust & 0x7) != StackAdjust)
          // The extra stack adjustments are too big for us to handle.
          return CU::UNWIND_MODE_DWARF;

        // Frameless stack with an offset too large for us to encode compactly.
        CompactUnwindEncoding |= CU::UNWIND_MODE_STACK_IND;

        // Encode the offset to the nnnnnn value in the 'subl $nnnnnn, ESP'
        // instruction.
        CompactUnwindEncoding |= (SubtractInstrIdx & 0xFF) << 16;

        // Encode any extra stack adjustments (done via push instructions).
        CompactUnwindEncoding |= (StackAdjust & 0x7) << 13;
      }

      // Encode the number of registers saved. (Reverse the list first.)
      std::reverse(&SavedRegs[0], &SavedRegs[SavedRegIdx]);
      CompactUnwindEncoding |= (SavedRegIdx & 0x7) << 10;

      // Get the encoding of the saved registers when we don't have a frame
      // pointer.
      uint32_t RegEnc = encodeCompactUnwindRegistersWithoutFrame(SavedRegIdx);
      if (RegEnc == ~0U) return CU::UNWIND_MODE_DWARF;

      // Encode the register encoding.
      CompactUnwindEncoding |=
        RegEnc & CU::UNWIND_FRAMELESS_STACK_REG_PERMUTATION;
    }

    return CompactUnwindEncoding;
  }

private:
  /// Get the compact unwind number for a given register. The number
  /// corresponds to the enum lists in compact_unwind_encoding.h.
  int getCompactUnwindRegNum(unsigned Reg) const {
    static const MCPhysReg CU32BitRegs[7] = {
      X86::EBX, X86::ECX, X86::EDX, X86::EDI, X86::ESI, X86::EBP, 0
    };
    static const MCPhysReg CU64BitRegs[] = {
      X86::RBX, X86::R12, X86::R13, X86::R14, X86::R15, X86::RBP, 0
    };
    const MCPhysReg *CURegs = Is64Bit ? CU64BitRegs : CU32BitRegs;
    for (int Idx = 1; *CURegs; ++CURegs, ++Idx)
      if (*CURegs == Reg)
        return Idx;

    return -1;
  }

  /// Return the registers encoded for a compact encoding with a frame
  /// pointer.
  uint32_t encodeCompactUnwindRegistersWithFrame() const {
    // Encode the registers in the order they were saved --- 3-bits per
    // register. The list of saved registers is assumed to be in reverse
    // order. The registers are numbered from 1 to CU_NUM_SAVED_REGS.
    uint32_t RegEnc = 0;
    for (int i = 0, Idx = 0; i != CU_NUM_SAVED_REGS; ++i) {
      unsigned Reg = SavedRegs[i];
      if (Reg == 0) break;

      int CURegNum = getCompactUnwindRegNum(Reg);
      if (CURegNum == -1) return ~0U;

      // Encode the 3-bit register number in order, skipping over 3-bits for
      // each register.
      RegEnc |= (CURegNum & 0x7) << (Idx++ * 3);
    }

    assert((RegEnc & 0x3FFFF) == RegEnc &&
           "Invalid compact register encoding!");
    return RegEnc;
  }

  /// Create the permutation encoding used with frameless stacks. It is
  /// passed the number of registers to be saved and an array of the registers
  /// saved.
  uint32_t encodeCompactUnwindRegistersWithoutFrame(unsigned RegCount) const {
    // The saved registers are numbered from 1 to 6. In order to encode the
    // order in which they were saved, we re-number them according to their
    // place in the register order. The re-numbering is relative to the last
    // re-numbered register. E.g., if we have registers {6, 2, 4, 5} saved in
    // that order:
    //
    //    Orig  Re-Num
    //    ----  ------
    //     6       6
    //     2       2
    //     4       3
    //     5       3
    //
    for (unsigned i = 0; i < RegCount; ++i) {
      int CUReg = getCompactUnwindRegNum(SavedRegs[i]);
      if (CUReg == -1) return ~0U;
      SavedRegs[i] = CUReg;
    }

    // Reverse the list.
    std::reverse(&SavedRegs[0], &SavedRegs[CU_NUM_SAVED_REGS]);

    uint32_t RenumRegs[CU_NUM_SAVED_REGS];
    for (unsigned i = CU_NUM_SAVED_REGS - RegCount; i < CU_NUM_SAVED_REGS; ++i){
      unsigned Countless = 0;
      for (unsigned j = CU_NUM_SAVED_REGS - RegCount; j < i; ++j)
        if (SavedRegs[j] < SavedRegs[i])
          ++Countless;

      RenumRegs[i] = SavedRegs[i] - Countless - 1;
    }

    // Take the renumbered values and encode them into a 10-bit number.
    uint32_t permutationEncoding = 0;
    switch (RegCount) {
    case 6:
      permutationEncoding |= 120 * RenumRegs[0] + 24 * RenumRegs[1]
                             + 6 * RenumRegs[2] +  2 * RenumRegs[3]
                             +     RenumRegs[4];
      break;
    case 5:
      permutationEncoding |= 120 * RenumRegs[1] + 24 * RenumRegs[2]
                             + 6 * RenumRegs[3] +  2 * RenumRegs[4]
                             +     RenumRegs[5];
      break;
    case 4:
      permutationEncoding |=  60 * RenumRegs[2] + 12 * RenumRegs[3]
                             + 3 * RenumRegs[4] +      RenumRegs[5];
      break;
    case 3:
      permutationEncoding |=  20 * RenumRegs[3] +  4 * RenumRegs[4]
                             +     RenumRegs[5];
      break;
    case 2:
      permutationEncoding |=   5 * RenumRegs[4] +      RenumRegs[5];
      break;
    case 1:
      permutationEncoding |=       RenumRegs[5];
      break;
    }

    assert((permutationEncoding & 0x3FF) == permutationEncoding &&
           "Invalid compact register encoding!");
    return permutationEncoding;
  }

public:
  DarwinX86AsmBackend(const Target &T, const MCRegisterInfo &MRI,
                      const MCSubtargetInfo &STI, bool Is64Bit)
    : X86AsmBackend(T, STI), MRI(MRI), Is64Bit(Is64Bit) {
    memset(SavedRegs, 0, sizeof(SavedRegs));
    OffsetSize = Is64Bit ? 8 : 4;
    MoveInstrSize = Is64Bit ? 3 : 2;
    StackDivide = Is64Bit ? 8 : 4;
  }
};

class DarwinX86_32AsmBackend : public DarwinX86AsmBackend {
public:
  DarwinX86_32AsmBackend(const Target &T, const MCRegisterInfo &MRI,
                         const MCSubtargetInfo &STI)
      : DarwinX86AsmBackend(T, MRI, STI, false) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86MachObjectWriter(/*Is64Bit=*/false,
                                     MachO::CPU_TYPE_I386,
                                     MachO::CPU_SUBTYPE_I386_ALL);
  }

  /// Generate the compact unwind encoding for the CFI instructions.
  uint32_t generateCompactUnwindEncoding(
                             ArrayRef<MCCFIInstruction> Instrs) const override {
    return generateCompactUnwindEncodingImpl(Instrs);
  }
};

class DarwinX86_64AsmBackend : public DarwinX86AsmBackend {
  const MachO::CPUSubTypeX86 Subtype;
public:
  DarwinX86_64AsmBackend(const Target &T, const MCRegisterInfo &MRI,
                         const MCSubtargetInfo &STI, MachO::CPUSubTypeX86 st)
      : DarwinX86AsmBackend(T, MRI, STI, true), Subtype(st) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86MachObjectWriter(/*Is64Bit=*/true, MachO::CPU_TYPE_X86_64,
                                     Subtype);
  }

  /// Generate the compact unwind encoding for the CFI instructions.
  uint32_t generateCompactUnwindEncoding(
                             ArrayRef<MCCFIInstruction> Instrs) const override {
    return generateCompactUnwindEncodingImpl(Instrs);
  }
};

} // end anonymous namespace

MCAsmBackend *llvm::createX86_32AsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  const Triple &TheTriple = STI.getTargetTriple();
  if (TheTriple.isOSBinFormatMachO())
    return new DarwinX86_32AsmBackend(T, MRI, STI);

  if (TheTriple.isOSWindows() && TheTriple.isOSBinFormatCOFF())
    return new WindowsX86AsmBackend(T, false, STI);

  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());

  if (TheTriple.isOSIAMCU())
    return new ELFX86_IAMCUAsmBackend(T, OSABI, STI);

  return new ELFX86_32AsmBackend(T, OSABI, STI);
}

MCAsmBackend *llvm::createX86_64AsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  const Triple &TheTriple = STI.getTargetTriple();
  if (TheTriple.isOSBinFormatMachO()) {
    MachO::CPUSubTypeX86 CS =
        StringSwitch<MachO::CPUSubTypeX86>(TheTriple.getArchName())
            .Case("x86_64h", MachO::CPU_SUBTYPE_X86_64_H)
            .Default(MachO::CPU_SUBTYPE_X86_64_ALL);
    return new DarwinX86_64AsmBackend(T, MRI, STI, CS);
  }

  if (TheTriple.isOSWindows() && TheTriple.isOSBinFormatCOFF())
    return new WindowsX86AsmBackend(T, true, STI);

  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());

  if (TheTriple.getEnvironment() == Triple::GNUX32)
    return new ELFX86_X32AsmBackend(T, OSABI, STI);
  return new ELFX86_64AsmBackend(T, OSABI, STI);
}
