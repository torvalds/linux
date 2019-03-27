//===-- X86MCInstLower.cpp - Convert X86 MachineInstr to an MCInst --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower X86 MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "InstPrinter/X86ATTInstPrinter.h"
#include "InstPrinter/X86InstComments.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86TargetStreamer.h"
#include "Utils/X86ShuffleDecode.h"
#include "X86AsmPrinter.h"
#include "X86RegisterInfo.h"
#include "X86ShuffleDecodeConstantPool.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

using namespace llvm;

namespace {

/// X86MCInstLower - This class is used to lower an MachineInstr into an MCInst.
class X86MCInstLower {
  MCContext &Ctx;
  const MachineFunction &MF;
  const TargetMachine &TM;
  const MCAsmInfo &MAI;
  X86AsmPrinter &AsmPrinter;

public:
  X86MCInstLower(const MachineFunction &MF, X86AsmPrinter &asmprinter);

  Optional<MCOperand> LowerMachineOperand(const MachineInstr *MI,
                                          const MachineOperand &MO) const;
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;

  MCSymbol *GetSymbolFromOperand(const MachineOperand &MO) const;
  MCOperand LowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

private:
  MachineModuleInfoMachO &getMachOMMI() const;
};

} // end anonymous namespace

// Emit a minimal sequence of nops spanning NumBytes bytes.
static void EmitNops(MCStreamer &OS, unsigned NumBytes, bool Is64Bit,
                     const MCSubtargetInfo &STI);

void X86AsmPrinter::StackMapShadowTracker::count(MCInst &Inst,
                                                 const MCSubtargetInfo &STI,
                                                 MCCodeEmitter *CodeEmitter) {
  if (InShadow) {
    SmallString<256> Code;
    SmallVector<MCFixup, 4> Fixups;
    raw_svector_ostream VecOS(Code);
    CodeEmitter->encodeInstruction(Inst, VecOS, Fixups, STI);
    CurrentShadowSize += Code.size();
    if (CurrentShadowSize >= RequiredShadowSize)
      InShadow = false; // The shadow is big enough. Stop counting.
  }
}

void X86AsmPrinter::StackMapShadowTracker::emitShadowPadding(
    MCStreamer &OutStreamer, const MCSubtargetInfo &STI) {
  if (InShadow && CurrentShadowSize < RequiredShadowSize) {
    InShadow = false;
    EmitNops(OutStreamer, RequiredShadowSize - CurrentShadowSize,
             MF->getSubtarget<X86Subtarget>().is64Bit(), STI);
  }
}

void X86AsmPrinter::EmitAndCountInstruction(MCInst &Inst) {
  OutStreamer->EmitInstruction(Inst, getSubtargetInfo(),
                               EnablePrintSchedInfo &&
                                   !(Inst.getFlags() & X86::NO_SCHED_INFO));
  SMShadowTracker.count(Inst, getSubtargetInfo(), CodeEmitter.get());
}

X86MCInstLower::X86MCInstLower(const MachineFunction &mf,
                               X86AsmPrinter &asmprinter)
    : Ctx(mf.getContext()), MF(mf), TM(mf.getTarget()), MAI(*TM.getMCAsmInfo()),
      AsmPrinter(asmprinter) {}

MachineModuleInfoMachO &X86MCInstLower::getMachOMMI() const {
  return MF.getMMI().getObjFileInfo<MachineModuleInfoMachO>();
}

/// GetSymbolFromOperand - Lower an MO_GlobalAddress or MO_ExternalSymbol
/// operand to an MCSymbol.
MCSymbol *X86MCInstLower::GetSymbolFromOperand(const MachineOperand &MO) const {
  const DataLayout &DL = MF.getDataLayout();
  assert((MO.isGlobal() || MO.isSymbol() || MO.isMBB()) &&
         "Isn't a symbol reference");

  MCSymbol *Sym = nullptr;
  SmallString<128> Name;
  StringRef Suffix;

  switch (MO.getTargetFlags()) {
  case X86II::MO_DLLIMPORT:
    // Handle dllimport linkage.
    Name += "__imp_";
    break;
  case X86II::MO_COFFSTUB:
    Name += ".refptr.";
    break;
  case X86II::MO_DARWIN_NONLAZY:
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE:
    Suffix = "$non_lazy_ptr";
    break;
  }

  if (!Suffix.empty())
    Name += DL.getPrivateGlobalPrefix();

  if (MO.isGlobal()) {
    const GlobalValue *GV = MO.getGlobal();
    AsmPrinter.getNameWithPrefix(Name, GV);
  } else if (MO.isSymbol()) {
    Mangler::getNameWithPrefix(Name, MO.getSymbolName(), DL);
  } else if (MO.isMBB()) {
    assert(Suffix.empty());
    Sym = MO.getMBB()->getSymbol();
  }

  Name += Suffix;
  if (!Sym)
    Sym = Ctx.getOrCreateSymbol(Name);

  // If the target flags on the operand changes the name of the symbol, do that
  // before we return the symbol.
  switch (MO.getTargetFlags()) {
  default:
    break;
  case X86II::MO_COFFSTUB: {
    MachineModuleInfoCOFF &MMICOFF =
        MF.getMMI().getObjFileInfo<MachineModuleInfoCOFF>();
    MachineModuleInfoImpl::StubValueTy &StubSym = MMICOFF.getGVStubEntry(Sym);
    if (!StubSym.getPointer()) {
      assert(MO.isGlobal() && "Extern symbol not handled yet");
      StubSym = MachineModuleInfoImpl::StubValueTy(
          AsmPrinter.getSymbol(MO.getGlobal()), true);
    }
    break;
  }
  case X86II::MO_DARWIN_NONLAZY:
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE: {
    MachineModuleInfoImpl::StubValueTy &StubSym =
        getMachOMMI().getGVStubEntry(Sym);
    if (!StubSym.getPointer()) {
      assert(MO.isGlobal() && "Extern symbol not handled yet");
      StubSym = MachineModuleInfoImpl::StubValueTy(
          AsmPrinter.getSymbol(MO.getGlobal()),
          !MO.getGlobal()->hasInternalLinkage());
    }
    break;
  }
  }

  return Sym;
}

MCOperand X86MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                             MCSymbol *Sym) const {
  // FIXME: We would like an efficient form for this, so we don't have to do a
  // lot of extra uniquing.
  const MCExpr *Expr = nullptr;
  MCSymbolRefExpr::VariantKind RefKind = MCSymbolRefExpr::VK_None;

  switch (MO.getTargetFlags()) {
  default:
    llvm_unreachable("Unknown target flag on GV operand");
  case X86II::MO_NO_FLAG: // No flag.
  // These affect the name of the symbol, not any suffix.
  case X86II::MO_DARWIN_NONLAZY:
  case X86II::MO_DLLIMPORT:
  case X86II::MO_COFFSTUB:
    break;

  case X86II::MO_TLVP:
    RefKind = MCSymbolRefExpr::VK_TLVP;
    break;
  case X86II::MO_TLVP_PIC_BASE:
    Expr = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_TLVP, Ctx);
    // Subtract the pic base.
    Expr = MCBinaryExpr::createSub(
        Expr, MCSymbolRefExpr::create(MF.getPICBaseSymbol(), Ctx), Ctx);
    break;
  case X86II::MO_SECREL:
    RefKind = MCSymbolRefExpr::VK_SECREL;
    break;
  case X86II::MO_TLSGD:
    RefKind = MCSymbolRefExpr::VK_TLSGD;
    break;
  case X86II::MO_TLSLD:
    RefKind = MCSymbolRefExpr::VK_TLSLD;
    break;
  case X86II::MO_TLSLDM:
    RefKind = MCSymbolRefExpr::VK_TLSLDM;
    break;
  case X86II::MO_GOTTPOFF:
    RefKind = MCSymbolRefExpr::VK_GOTTPOFF;
    break;
  case X86II::MO_INDNTPOFF:
    RefKind = MCSymbolRefExpr::VK_INDNTPOFF;
    break;
  case X86II::MO_TPOFF:
    RefKind = MCSymbolRefExpr::VK_TPOFF;
    break;
  case X86II::MO_DTPOFF:
    RefKind = MCSymbolRefExpr::VK_DTPOFF;
    break;
  case X86II::MO_NTPOFF:
    RefKind = MCSymbolRefExpr::VK_NTPOFF;
    break;
  case X86II::MO_GOTNTPOFF:
    RefKind = MCSymbolRefExpr::VK_GOTNTPOFF;
    break;
  case X86II::MO_GOTPCREL:
    RefKind = MCSymbolRefExpr::VK_GOTPCREL;
    break;
  case X86II::MO_GOT:
    RefKind = MCSymbolRefExpr::VK_GOT;
    break;
  case X86II::MO_GOTOFF:
    RefKind = MCSymbolRefExpr::VK_GOTOFF;
    break;
  case X86II::MO_PLT:
    RefKind = MCSymbolRefExpr::VK_PLT;
    break;
  case X86II::MO_ABS8:
    RefKind = MCSymbolRefExpr::VK_X86_ABS8;
    break;
  case X86II::MO_PIC_BASE_OFFSET:
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE:
    Expr = MCSymbolRefExpr::create(Sym, Ctx);
    // Subtract the pic base.
    Expr = MCBinaryExpr::createSub(
        Expr, MCSymbolRefExpr::create(MF.getPICBaseSymbol(), Ctx), Ctx);
    if (MO.isJTI()) {
      assert(MAI.doesSetDirectiveSuppressReloc());
      // If .set directive is supported, use it to reduce the number of
      // relocations the assembler will generate for differences between
      // local labels. This is only safe when the symbols are in the same
      // section so we are restricting it to jumptable references.
      MCSymbol *Label = Ctx.createTempSymbol();
      AsmPrinter.OutStreamer->EmitAssignment(Label, Expr);
      Expr = MCSymbolRefExpr::create(Label, Ctx);
    }
    break;
  }

  if (!Expr)
    Expr = MCSymbolRefExpr::create(Sym, RefKind, Ctx);

  if (!MO.isJTI() && !MO.isMBB() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

/// Simplify FOO $imm, %{al,ax,eax,rax} to FOO $imm, for instruction with
/// a short fixed-register form.
static void SimplifyShortImmForm(MCInst &Inst, unsigned Opcode) {
  unsigned ImmOp = Inst.getNumOperands() - 1;
  assert(Inst.getOperand(0).isReg() &&
         (Inst.getOperand(ImmOp).isImm() || Inst.getOperand(ImmOp).isExpr()) &&
         ((Inst.getNumOperands() == 3 && Inst.getOperand(1).isReg() &&
           Inst.getOperand(0).getReg() == Inst.getOperand(1).getReg()) ||
          Inst.getNumOperands() == 2) &&
         "Unexpected instruction!");

  // Check whether the destination register can be fixed.
  unsigned Reg = Inst.getOperand(0).getReg();
  if (Reg != X86::AL && Reg != X86::AX && Reg != X86::EAX && Reg != X86::RAX)
    return;

  // If so, rewrite the instruction.
  MCOperand Saved = Inst.getOperand(ImmOp);
  Inst = MCInst();
  Inst.setOpcode(Opcode);
  Inst.addOperand(Saved);
}

/// If a movsx instruction has a shorter encoding for the used register
/// simplify the instruction to use it instead.
static void SimplifyMOVSX(MCInst &Inst) {
  unsigned NewOpcode = 0;
  unsigned Op0 = Inst.getOperand(0).getReg(), Op1 = Inst.getOperand(1).getReg();
  switch (Inst.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instruction!");
  case X86::MOVSX16rr8: // movsbw %al, %ax   --> cbtw
    if (Op0 == X86::AX && Op1 == X86::AL)
      NewOpcode = X86::CBW;
    break;
  case X86::MOVSX32rr16: // movswl %ax, %eax  --> cwtl
    if (Op0 == X86::EAX && Op1 == X86::AX)
      NewOpcode = X86::CWDE;
    break;
  case X86::MOVSX64rr32: // movslq %eax, %rax --> cltq
    if (Op0 == X86::RAX && Op1 == X86::EAX)
      NewOpcode = X86::CDQE;
    break;
  }

  if (NewOpcode != 0) {
    Inst = MCInst();
    Inst.setOpcode(NewOpcode);
  }
}

/// Simplify things like MOV32rm to MOV32o32a.
static void SimplifyShortMoveForm(X86AsmPrinter &Printer, MCInst &Inst,
                                  unsigned Opcode) {
  // Don't make these simplifications in 64-bit mode; other assemblers don't
  // perform them because they make the code larger.
  if (Printer.getSubtarget().is64Bit())
    return;

  bool IsStore = Inst.getOperand(0).isReg() && Inst.getOperand(1).isReg();
  unsigned AddrBase = IsStore;
  unsigned RegOp = IsStore ? 0 : 5;
  unsigned AddrOp = AddrBase + 3;
  assert(
      Inst.getNumOperands() == 6 && Inst.getOperand(RegOp).isReg() &&
      Inst.getOperand(AddrBase + X86::AddrBaseReg).isReg() &&
      Inst.getOperand(AddrBase + X86::AddrScaleAmt).isImm() &&
      Inst.getOperand(AddrBase + X86::AddrIndexReg).isReg() &&
      Inst.getOperand(AddrBase + X86::AddrSegmentReg).isReg() &&
      (Inst.getOperand(AddrOp).isExpr() || Inst.getOperand(AddrOp).isImm()) &&
      "Unexpected instruction!");

  // Check whether the destination register can be fixed.
  unsigned Reg = Inst.getOperand(RegOp).getReg();
  if (Reg != X86::AL && Reg != X86::AX && Reg != X86::EAX && Reg != X86::RAX)
    return;

  // Check whether this is an absolute address.
  // FIXME: We know TLVP symbol refs aren't, but there should be a better way
  // to do this here.
  bool Absolute = true;
  if (Inst.getOperand(AddrOp).isExpr()) {
    const MCExpr *MCE = Inst.getOperand(AddrOp).getExpr();
    if (const MCSymbolRefExpr *SRE = dyn_cast<MCSymbolRefExpr>(MCE))
      if (SRE->getKind() == MCSymbolRefExpr::VK_TLVP)
        Absolute = false;
  }

  if (Absolute &&
      (Inst.getOperand(AddrBase + X86::AddrBaseReg).getReg() != 0 ||
       Inst.getOperand(AddrBase + X86::AddrScaleAmt).getImm() != 1 ||
       Inst.getOperand(AddrBase + X86::AddrIndexReg).getReg() != 0))
    return;

  // If so, rewrite the instruction.
  MCOperand Saved = Inst.getOperand(AddrOp);
  MCOperand Seg = Inst.getOperand(AddrBase + X86::AddrSegmentReg);
  Inst = MCInst();
  Inst.setOpcode(Opcode);
  Inst.addOperand(Saved);
  Inst.addOperand(Seg);
}

static unsigned getRetOpcode(const X86Subtarget &Subtarget) {
  return Subtarget.is64Bit() ? X86::RETQ : X86::RETL;
}

Optional<MCOperand>
X86MCInstLower::LowerMachineOperand(const MachineInstr *MI,
                                    const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    MI->print(errs());
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    // Ignore all implicit register operands.
    if (MO.isImplicit())
      return None;
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
    return LowerSymbolOperand(MO, GetSymbolFromOperand(MO));
  case MachineOperand::MO_MCSymbol:
    return LowerSymbolOperand(MO, MO.getMCSymbol());
  case MachineOperand::MO_JumpTableIndex:
    return LowerSymbolOperand(MO, AsmPrinter.GetJTISymbol(MO.getIndex()));
  case MachineOperand::MO_ConstantPoolIndex:
    return LowerSymbolOperand(MO, AsmPrinter.GetCPISymbol(MO.getIndex()));
  case MachineOperand::MO_BlockAddress:
    return LowerSymbolOperand(
        MO, AsmPrinter.GetBlockAddressSymbol(MO.getBlockAddress()));
  case MachineOperand::MO_RegisterMask:
    // Ignore call clobbers.
    return None;
  }
}

void X86MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  for (const MachineOperand &MO : MI->operands())
    if (auto MaybeMCOp = LowerMachineOperand(MI, MO))
      OutMI.addOperand(MaybeMCOp.getValue());

  // Handle a few special cases to eliminate operand modifiers.
ReSimplify:
  switch (OutMI.getOpcode()) {
  case X86::LEA64_32r:
  case X86::LEA64r:
  case X86::LEA16r:
  case X86::LEA32r:
    // LEA should have a segment register, but it must be empty.
    assert(OutMI.getNumOperands() == 1 + X86::AddrNumOperands &&
           "Unexpected # of LEA operands");
    assert(OutMI.getOperand(1 + X86::AddrSegmentReg).getReg() == 0 &&
           "LEA has segment specified!");
    break;

  // Commute operands to get a smaller encoding by using VEX.R instead of VEX.B
  // if one of the registers is extended, but other isn't.
  case X86::VMOVZPQILo2PQIrr:
  case X86::VMOVAPDrr:
  case X86::VMOVAPDYrr:
  case X86::VMOVAPSrr:
  case X86::VMOVAPSYrr:
  case X86::VMOVDQArr:
  case X86::VMOVDQAYrr:
  case X86::VMOVDQUrr:
  case X86::VMOVDQUYrr:
  case X86::VMOVUPDrr:
  case X86::VMOVUPDYrr:
  case X86::VMOVUPSrr:
  case X86::VMOVUPSYrr: {
    if (!X86II::isX86_64ExtendedReg(OutMI.getOperand(0).getReg()) &&
        X86II::isX86_64ExtendedReg(OutMI.getOperand(1).getReg())) {
      unsigned NewOpc;
      switch (OutMI.getOpcode()) {
      default: llvm_unreachable("Invalid opcode");
      case X86::VMOVZPQILo2PQIrr: NewOpc = X86::VMOVPQI2QIrr;   break;
      case X86::VMOVAPDrr:        NewOpc = X86::VMOVAPDrr_REV;  break;
      case X86::VMOVAPDYrr:       NewOpc = X86::VMOVAPDYrr_REV; break;
      case X86::VMOVAPSrr:        NewOpc = X86::VMOVAPSrr_REV;  break;
      case X86::VMOVAPSYrr:       NewOpc = X86::VMOVAPSYrr_REV; break;
      case X86::VMOVDQArr:        NewOpc = X86::VMOVDQArr_REV;  break;
      case X86::VMOVDQAYrr:       NewOpc = X86::VMOVDQAYrr_REV; break;
      case X86::VMOVDQUrr:        NewOpc = X86::VMOVDQUrr_REV;  break;
      case X86::VMOVDQUYrr:       NewOpc = X86::VMOVDQUYrr_REV; break;
      case X86::VMOVUPDrr:        NewOpc = X86::VMOVUPDrr_REV;  break;
      case X86::VMOVUPDYrr:       NewOpc = X86::VMOVUPDYrr_REV; break;
      case X86::VMOVUPSrr:        NewOpc = X86::VMOVUPSrr_REV;  break;
      case X86::VMOVUPSYrr:       NewOpc = X86::VMOVUPSYrr_REV; break;
      }
      OutMI.setOpcode(NewOpc);
    }
    break;
  }
  case X86::VMOVSDrr:
  case X86::VMOVSSrr: {
    if (!X86II::isX86_64ExtendedReg(OutMI.getOperand(0).getReg()) &&
        X86II::isX86_64ExtendedReg(OutMI.getOperand(2).getReg())) {
      unsigned NewOpc;
      switch (OutMI.getOpcode()) {
      default: llvm_unreachable("Invalid opcode");
      case X86::VMOVSDrr: NewOpc = X86::VMOVSDrr_REV; break;
      case X86::VMOVSSrr: NewOpc = X86::VMOVSSrr_REV; break;
      }
      OutMI.setOpcode(NewOpc);
    }
    break;
  }

  // TAILJMPr64, CALL64r, CALL64pcrel32 - These instructions have register
  // inputs modeled as normal uses instead of implicit uses.  As such, truncate
  // off all but the first operand (the callee).  FIXME: Change isel.
  case X86::TAILJMPr64:
  case X86::TAILJMPr64_REX:
  case X86::CALL64r:
  case X86::CALL64pcrel32: {
    unsigned Opcode = OutMI.getOpcode();
    MCOperand Saved = OutMI.getOperand(0);
    OutMI = MCInst();
    OutMI.setOpcode(Opcode);
    OutMI.addOperand(Saved);
    break;
  }

  case X86::EH_RETURN:
  case X86::EH_RETURN64: {
    OutMI = MCInst();
    OutMI.setOpcode(getRetOpcode(AsmPrinter.getSubtarget()));
    break;
  }

  case X86::CLEANUPRET: {
    // Replace CLEANUPRET with the appropriate RET.
    OutMI = MCInst();
    OutMI.setOpcode(getRetOpcode(AsmPrinter.getSubtarget()));
    break;
  }

  case X86::CATCHRET: {
    // Replace CATCHRET with the appropriate RET.
    const X86Subtarget &Subtarget = AsmPrinter.getSubtarget();
    unsigned ReturnReg = Subtarget.is64Bit() ? X86::RAX : X86::EAX;
    OutMI = MCInst();
    OutMI.setOpcode(getRetOpcode(Subtarget));
    OutMI.addOperand(MCOperand::createReg(ReturnReg));
    break;
  }

    // TAILJMPd, TAILJMPd64, TailJMPd_cc - Lower to the correct jump
    // instruction.
    {
      unsigned Opcode;
    case X86::TAILJMPr:
      Opcode = X86::JMP32r;
      goto SetTailJmpOpcode;
    case X86::TAILJMPd:
    case X86::TAILJMPd64:
      Opcode = X86::JMP_1;
      goto SetTailJmpOpcode;
    case X86::TAILJMPd_CC:
    case X86::TAILJMPd64_CC:
      Opcode = X86::GetCondBranchFromCond(
          static_cast<X86::CondCode>(MI->getOperand(1).getImm()));
      goto SetTailJmpOpcode;

    SetTailJmpOpcode:
      MCOperand Saved = OutMI.getOperand(0);
      OutMI = MCInst();
      OutMI.setOpcode(Opcode);
      OutMI.addOperand(Saved);
      break;
    }

  case X86::DEC16r:
  case X86::DEC32r:
  case X86::INC16r:
  case X86::INC32r:
    // If we aren't in 64-bit mode we can use the 1-byte inc/dec instructions.
    if (!AsmPrinter.getSubtarget().is64Bit()) {
      unsigned Opcode;
      switch (OutMI.getOpcode()) {
      default: llvm_unreachable("Invalid opcode");
      case X86::DEC16r: Opcode = X86::DEC16r_alt; break;
      case X86::DEC32r: Opcode = X86::DEC32r_alt; break;
      case X86::INC16r: Opcode = X86::INC16r_alt; break;
      case X86::INC32r: Opcode = X86::INC32r_alt; break;
      }
      OutMI.setOpcode(Opcode);
    }
    break;

  // These are pseudo-ops for OR to help with the OR->ADD transformation.  We do
  // this with an ugly goto in case the resultant OR uses EAX and needs the
  // short form.
  case X86::ADD16rr_DB:   OutMI.setOpcode(X86::OR16rr);   goto ReSimplify;
  case X86::ADD32rr_DB:   OutMI.setOpcode(X86::OR32rr);   goto ReSimplify;
  case X86::ADD64rr_DB:   OutMI.setOpcode(X86::OR64rr);   goto ReSimplify;
  case X86::ADD16ri_DB:   OutMI.setOpcode(X86::OR16ri);   goto ReSimplify;
  case X86::ADD32ri_DB:   OutMI.setOpcode(X86::OR32ri);   goto ReSimplify;
  case X86::ADD64ri32_DB: OutMI.setOpcode(X86::OR64ri32); goto ReSimplify;
  case X86::ADD16ri8_DB:  OutMI.setOpcode(X86::OR16ri8);  goto ReSimplify;
  case X86::ADD32ri8_DB:  OutMI.setOpcode(X86::OR32ri8);  goto ReSimplify;
  case X86::ADD64ri8_DB:  OutMI.setOpcode(X86::OR64ri8);  goto ReSimplify;

  // We don't currently select the correct instruction form for instructions
  // which have a short %eax, etc. form. Handle this by custom lowering, for
  // now.
  //
  // Note, we are currently not handling the following instructions:
  // MOV64ao8, MOV64o8a
  // XCHG16ar, XCHG32ar, XCHG64ar
  case X86::MOV8mr_NOREX:
  case X86::MOV8mr:
  case X86::MOV8rm_NOREX:
  case X86::MOV8rm:
  case X86::MOV16mr:
  case X86::MOV16rm:
  case X86::MOV32mr:
  case X86::MOV32rm: {
    unsigned NewOpc;
    switch (OutMI.getOpcode()) {
    default: llvm_unreachable("Invalid opcode");
    case X86::MOV8mr_NOREX:
    case X86::MOV8mr:  NewOpc = X86::MOV8o32a; break;
    case X86::MOV8rm_NOREX:
    case X86::MOV8rm:  NewOpc = X86::MOV8ao32; break;
    case X86::MOV16mr: NewOpc = X86::MOV16o32a; break;
    case X86::MOV16rm: NewOpc = X86::MOV16ao32; break;
    case X86::MOV32mr: NewOpc = X86::MOV32o32a; break;
    case X86::MOV32rm: NewOpc = X86::MOV32ao32; break;
    }
    SimplifyShortMoveForm(AsmPrinter, OutMI, NewOpc);
    break;
  }

  case X86::ADC8ri: case X86::ADC16ri: case X86::ADC32ri: case X86::ADC64ri32:
  case X86::ADD8ri: case X86::ADD16ri: case X86::ADD32ri: case X86::ADD64ri32:
  case X86::AND8ri: case X86::AND16ri: case X86::AND32ri: case X86::AND64ri32:
  case X86::CMP8ri: case X86::CMP16ri: case X86::CMP32ri: case X86::CMP64ri32:
  case X86::OR8ri:  case X86::OR16ri:  case X86::OR32ri:  case X86::OR64ri32:
  case X86::SBB8ri: case X86::SBB16ri: case X86::SBB32ri: case X86::SBB64ri32:
  case X86::SUB8ri: case X86::SUB16ri: case X86::SUB32ri: case X86::SUB64ri32:
  case X86::TEST8ri:case X86::TEST16ri:case X86::TEST32ri:case X86::TEST64ri32:
  case X86::XOR8ri: case X86::XOR16ri: case X86::XOR32ri: case X86::XOR64ri32: {
    unsigned NewOpc;
    switch (OutMI.getOpcode()) {
    default: llvm_unreachable("Invalid opcode");
    case X86::ADC8ri:     NewOpc = X86::ADC8i8;    break;
    case X86::ADC16ri:    NewOpc = X86::ADC16i16;  break;
    case X86::ADC32ri:    NewOpc = X86::ADC32i32;  break;
    case X86::ADC64ri32:  NewOpc = X86::ADC64i32;  break;
    case X86::ADD8ri:     NewOpc = X86::ADD8i8;    break;
    case X86::ADD16ri:    NewOpc = X86::ADD16i16;  break;
    case X86::ADD32ri:    NewOpc = X86::ADD32i32;  break;
    case X86::ADD64ri32:  NewOpc = X86::ADD64i32;  break;
    case X86::AND8ri:     NewOpc = X86::AND8i8;    break;
    case X86::AND16ri:    NewOpc = X86::AND16i16;  break;
    case X86::AND32ri:    NewOpc = X86::AND32i32;  break;
    case X86::AND64ri32:  NewOpc = X86::AND64i32;  break;
    case X86::CMP8ri:     NewOpc = X86::CMP8i8;    break;
    case X86::CMP16ri:    NewOpc = X86::CMP16i16;  break;
    case X86::CMP32ri:    NewOpc = X86::CMP32i32;  break;
    case X86::CMP64ri32:  NewOpc = X86::CMP64i32;  break;
    case X86::OR8ri:      NewOpc = X86::OR8i8;     break;
    case X86::OR16ri:     NewOpc = X86::OR16i16;   break;
    case X86::OR32ri:     NewOpc = X86::OR32i32;   break;
    case X86::OR64ri32:   NewOpc = X86::OR64i32;   break;
    case X86::SBB8ri:     NewOpc = X86::SBB8i8;    break;
    case X86::SBB16ri:    NewOpc = X86::SBB16i16;  break;
    case X86::SBB32ri:    NewOpc = X86::SBB32i32;  break;
    case X86::SBB64ri32:  NewOpc = X86::SBB64i32;  break;
    case X86::SUB8ri:     NewOpc = X86::SUB8i8;    break;
    case X86::SUB16ri:    NewOpc = X86::SUB16i16;  break;
    case X86::SUB32ri:    NewOpc = X86::SUB32i32;  break;
    case X86::SUB64ri32:  NewOpc = X86::SUB64i32;  break;
    case X86::TEST8ri:    NewOpc = X86::TEST8i8;   break;
    case X86::TEST16ri:   NewOpc = X86::TEST16i16; break;
    case X86::TEST32ri:   NewOpc = X86::TEST32i32; break;
    case X86::TEST64ri32: NewOpc = X86::TEST64i32; break;
    case X86::XOR8ri:     NewOpc = X86::XOR8i8;    break;
    case X86::XOR16ri:    NewOpc = X86::XOR16i16;  break;
    case X86::XOR32ri:    NewOpc = X86::XOR32i32;  break;
    case X86::XOR64ri32:  NewOpc = X86::XOR64i32;  break;
    }
    SimplifyShortImmForm(OutMI, NewOpc);
    break;
  }

  // Try to shrink some forms of movsx.
  case X86::MOVSX16rr8:
  case X86::MOVSX32rr16:
  case X86::MOVSX64rr32:
    SimplifyMOVSX(OutMI);
    break;
  }
}

void X86AsmPrinter::LowerTlsAddr(X86MCInstLower &MCInstLowering,
                                 const MachineInstr &MI) {

  bool is64Bits = MI.getOpcode() == X86::TLS_addr64 ||
                  MI.getOpcode() == X86::TLS_base_addr64;

  bool needsPadding = MI.getOpcode() == X86::TLS_addr64;

  MCContext &context = OutStreamer->getContext();

  if (needsPadding)
    EmitAndCountInstruction(MCInstBuilder(X86::DATA16_PREFIX));

  MCSymbolRefExpr::VariantKind SRVK;
  switch (MI.getOpcode()) {
  case X86::TLS_addr32:
  case X86::TLS_addr64:
    SRVK = MCSymbolRefExpr::VK_TLSGD;
    break;
  case X86::TLS_base_addr32:
    SRVK = MCSymbolRefExpr::VK_TLSLDM;
    break;
  case X86::TLS_base_addr64:
    SRVK = MCSymbolRefExpr::VK_TLSLD;
    break;
  default:
    llvm_unreachable("unexpected opcode");
  }

  MCSymbol *sym = MCInstLowering.GetSymbolFromOperand(MI.getOperand(3));
  const MCSymbolRefExpr *symRef = MCSymbolRefExpr::create(sym, SRVK, context);

  MCInst LEA;
  if (is64Bits) {
    LEA.setOpcode(X86::LEA64r);
    LEA.addOperand(MCOperand::createReg(X86::RDI)); // dest
    LEA.addOperand(MCOperand::createReg(X86::RIP)); // base
    LEA.addOperand(MCOperand::createImm(1));        // scale
    LEA.addOperand(MCOperand::createReg(0));        // index
    LEA.addOperand(MCOperand::createExpr(symRef));  // disp
    LEA.addOperand(MCOperand::createReg(0));        // seg
  } else if (SRVK == MCSymbolRefExpr::VK_TLSLDM) {
    LEA.setOpcode(X86::LEA32r);
    LEA.addOperand(MCOperand::createReg(X86::EAX)); // dest
    LEA.addOperand(MCOperand::createReg(X86::EBX)); // base
    LEA.addOperand(MCOperand::createImm(1));        // scale
    LEA.addOperand(MCOperand::createReg(0));        // index
    LEA.addOperand(MCOperand::createExpr(symRef));  // disp
    LEA.addOperand(MCOperand::createReg(0));        // seg
  } else {
    LEA.setOpcode(X86::LEA32r);
    LEA.addOperand(MCOperand::createReg(X86::EAX)); // dest
    LEA.addOperand(MCOperand::createReg(0));        // base
    LEA.addOperand(MCOperand::createImm(1));        // scale
    LEA.addOperand(MCOperand::createReg(X86::EBX)); // index
    LEA.addOperand(MCOperand::createExpr(symRef));  // disp
    LEA.addOperand(MCOperand::createReg(0));        // seg
  }
  EmitAndCountInstruction(LEA);

  if (needsPadding) {
    EmitAndCountInstruction(MCInstBuilder(X86::DATA16_PREFIX));
    EmitAndCountInstruction(MCInstBuilder(X86::DATA16_PREFIX));
    EmitAndCountInstruction(MCInstBuilder(X86::REX64_PREFIX));
  }

  StringRef name = is64Bits ? "__tls_get_addr" : "___tls_get_addr";
  MCSymbol *tlsGetAddr = context.getOrCreateSymbol(name);
  const MCSymbolRefExpr *tlsRef =
      MCSymbolRefExpr::create(tlsGetAddr, MCSymbolRefExpr::VK_PLT, context);

  EmitAndCountInstruction(
      MCInstBuilder(is64Bits ? X86::CALL64pcrel32 : X86::CALLpcrel32)
          .addExpr(tlsRef));
}

/// Emit the largest nop instruction smaller than or equal to \p NumBytes
/// bytes.  Return the size of nop emitted.
static unsigned EmitNop(MCStreamer &OS, unsigned NumBytes, bool Is64Bit,
                        const MCSubtargetInfo &STI) {
  // This works only for 64bit. For 32bit we have to do additional checking if
  // the CPU supports multi-byte nops.
  assert(Is64Bit && "EmitNops only supports X86-64");

  unsigned NopSize;
  unsigned Opc, BaseReg, ScaleVal, IndexReg, Displacement, SegmentReg;
  Opc = IndexReg = Displacement = SegmentReg = 0;
  BaseReg = X86::RAX;
  ScaleVal = 1;
  switch (NumBytes) {
  case 0:
    llvm_unreachable("Zero nops?");
    break;
  case 1:
    NopSize = 1;
    Opc = X86::NOOP;
    break;
  case 2:
    NopSize = 2;
    Opc = X86::XCHG16ar;
    break;
  case 3:
    NopSize = 3;
    Opc = X86::NOOPL;
    break;
  case 4:
    NopSize = 4;
    Opc = X86::NOOPL;
    Displacement = 8;
    break;
  case 5:
    NopSize = 5;
    Opc = X86::NOOPL;
    Displacement = 8;
    IndexReg = X86::RAX;
    break;
  case 6:
    NopSize = 6;
    Opc = X86::NOOPW;
    Displacement = 8;
    IndexReg = X86::RAX;
    break;
  case 7:
    NopSize = 7;
    Opc = X86::NOOPL;
    Displacement = 512;
    break;
  case 8:
    NopSize = 8;
    Opc = X86::NOOPL;
    Displacement = 512;
    IndexReg = X86::RAX;
    break;
  case 9:
    NopSize = 9;
    Opc = X86::NOOPW;
    Displacement = 512;
    IndexReg = X86::RAX;
    break;
  default:
    NopSize = 10;
    Opc = X86::NOOPW;
    Displacement = 512;
    IndexReg = X86::RAX;
    SegmentReg = X86::CS;
    break;
  }

  unsigned NumPrefixes = std::min(NumBytes - NopSize, 5U);
  NopSize += NumPrefixes;
  for (unsigned i = 0; i != NumPrefixes; ++i)
    OS.EmitBytes("\x66");

  switch (Opc) {
  default: llvm_unreachable("Unexpected opcode");
  case X86::NOOP:
    OS.EmitInstruction(MCInstBuilder(Opc), STI);
    break;
  case X86::XCHG16ar:
    OS.EmitInstruction(MCInstBuilder(Opc).addReg(X86::AX).addReg(X86::AX), STI);
    break;
  case X86::NOOPL:
  case X86::NOOPW:
    OS.EmitInstruction(MCInstBuilder(Opc)
                           .addReg(BaseReg)
                           .addImm(ScaleVal)
                           .addReg(IndexReg)
                           .addImm(Displacement)
                           .addReg(SegmentReg),
                       STI);
    break;
  }
  assert(NopSize <= NumBytes && "We overemitted?");
  return NopSize;
}

/// Emit the optimal amount of multi-byte nops on X86.
static void EmitNops(MCStreamer &OS, unsigned NumBytes, bool Is64Bit,
                     const MCSubtargetInfo &STI) {
  unsigned NopsToEmit = NumBytes;
  (void)NopsToEmit;
  while (NumBytes) {
    NumBytes -= EmitNop(OS, NumBytes, Is64Bit, STI);
    assert(NopsToEmit >= NumBytes && "Emitted more than I asked for!");
  }
}

void X86AsmPrinter::LowerSTATEPOINT(const MachineInstr &MI,
                                    X86MCInstLower &MCIL) {
  assert(Subtarget->is64Bit() && "Statepoint currently only supports X86-64");

  StatepointOpers SOpers(&MI);
  if (unsigned PatchBytes = SOpers.getNumPatchBytes()) {
    EmitNops(*OutStreamer, PatchBytes, Subtarget->is64Bit(),
             getSubtargetInfo());
  } else {
    // Lower call target and choose correct opcode
    const MachineOperand &CallTarget = SOpers.getCallTarget();
    MCOperand CallTargetMCOp;
    unsigned CallOpcode;
    switch (CallTarget.getType()) {
    case MachineOperand::MO_GlobalAddress:
    case MachineOperand::MO_ExternalSymbol:
      CallTargetMCOp = MCIL.LowerSymbolOperand(
          CallTarget, MCIL.GetSymbolFromOperand(CallTarget));
      CallOpcode = X86::CALL64pcrel32;
      // Currently, we only support relative addressing with statepoints.
      // Otherwise, we'll need a scratch register to hold the target
      // address.  You'll fail asserts during load & relocation if this
      // symbol is to far away. (TODO: support non-relative addressing)
      break;
    case MachineOperand::MO_Immediate:
      CallTargetMCOp = MCOperand::createImm(CallTarget.getImm());
      CallOpcode = X86::CALL64pcrel32;
      // Currently, we only support relative addressing with statepoints.
      // Otherwise, we'll need a scratch register to hold the target
      // immediate.  You'll fail asserts during load & relocation if this
      // address is to far away. (TODO: support non-relative addressing)
      break;
    case MachineOperand::MO_Register:
      // FIXME: Add retpoline support and remove this.
      if (Subtarget->useRetpolineIndirectCalls())
        report_fatal_error("Lowering register statepoints with retpoline not "
                           "yet implemented.");
      CallTargetMCOp = MCOperand::createReg(CallTarget.getReg());
      CallOpcode = X86::CALL64r;
      break;
    default:
      llvm_unreachable("Unsupported operand type in statepoint call target");
      break;
    }

    // Emit call
    MCInst CallInst;
    CallInst.setOpcode(CallOpcode);
    CallInst.addOperand(CallTargetMCOp);
    OutStreamer->EmitInstruction(CallInst, getSubtargetInfo());
  }

  // Record our statepoint node in the same section used by STACKMAP
  // and PATCHPOINT
  SM.recordStatepoint(MI);
}

void X86AsmPrinter::LowerFAULTING_OP(const MachineInstr &FaultingMI,
                                     X86MCInstLower &MCIL) {
  // FAULTING_LOAD_OP <def>, <faltinf type>, <MBB handler>,
  //                  <opcode>, <operands>

  unsigned DefRegister = FaultingMI.getOperand(0).getReg();
  FaultMaps::FaultKind FK =
      static_cast<FaultMaps::FaultKind>(FaultingMI.getOperand(1).getImm());
  MCSymbol *HandlerLabel = FaultingMI.getOperand(2).getMBB()->getSymbol();
  unsigned Opcode = FaultingMI.getOperand(3).getImm();
  unsigned OperandsBeginIdx = 4;

  assert(FK < FaultMaps::FaultKindMax && "Invalid Faulting Kind!");
  FM.recordFaultingOp(FK, HandlerLabel);

  MCInst MI;
  MI.setOpcode(Opcode);

  if (DefRegister != X86::NoRegister)
    MI.addOperand(MCOperand::createReg(DefRegister));

  for (auto I = FaultingMI.operands_begin() + OperandsBeginIdx,
            E = FaultingMI.operands_end();
       I != E; ++I)
    if (auto MaybeOperand = MCIL.LowerMachineOperand(&FaultingMI, *I))
      MI.addOperand(MaybeOperand.getValue());

  OutStreamer->EmitInstruction(MI, getSubtargetInfo());
}

void X86AsmPrinter::LowerFENTRY_CALL(const MachineInstr &MI,
                                     X86MCInstLower &MCIL) {
  bool Is64Bits = Subtarget->is64Bit();
  MCContext &Ctx = OutStreamer->getContext();
  MCSymbol *fentry = Ctx.getOrCreateSymbol("__fentry__");
  const MCSymbolRefExpr *Op =
      MCSymbolRefExpr::create(fentry, MCSymbolRefExpr::VK_None, Ctx);

  EmitAndCountInstruction(
      MCInstBuilder(Is64Bits ? X86::CALL64pcrel32 : X86::CALLpcrel32)
          .addExpr(Op));
}

void X86AsmPrinter::LowerPATCHABLE_OP(const MachineInstr &MI,
                                      X86MCInstLower &MCIL) {
  // PATCHABLE_OP minsize, opcode, operands

  unsigned MinSize = MI.getOperand(0).getImm();
  unsigned Opcode = MI.getOperand(1).getImm();

  MCInst MCI;
  MCI.setOpcode(Opcode);
  for (auto &MO : make_range(MI.operands_begin() + 2, MI.operands_end()))
    if (auto MaybeOperand = MCIL.LowerMachineOperand(&MI, MO))
      MCI.addOperand(MaybeOperand.getValue());

  SmallString<256> Code;
  SmallVector<MCFixup, 4> Fixups;
  raw_svector_ostream VecOS(Code);
  CodeEmitter->encodeInstruction(MCI, VecOS, Fixups, getSubtargetInfo());

  if (Code.size() < MinSize) {
    if (MinSize == 2 && Opcode == X86::PUSH64r) {
      // This is an optimization that lets us get away without emitting a nop in
      // many cases.
      //
      // NB! In some cases the encoding for PUSH64r (e.g. PUSH64r %r9) takes two
      // bytes too, so the check on MinSize is important.
      MCI.setOpcode(X86::PUSH64rmr);
    } else {
      unsigned NopSize = EmitNop(*OutStreamer, MinSize, Subtarget->is64Bit(),
                                 getSubtargetInfo());
      assert(NopSize == MinSize && "Could not implement MinSize!");
      (void)NopSize;
    }
  }

  OutStreamer->EmitInstruction(MCI, getSubtargetInfo());
}

// Lower a stackmap of the form:
// <id>, <shadowBytes>, ...
void X86AsmPrinter::LowerSTACKMAP(const MachineInstr &MI) {
  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
  SM.recordStackMap(MI);
  unsigned NumShadowBytes = MI.getOperand(1).getImm();
  SMShadowTracker.reset(NumShadowBytes);
}

// Lower a patchpoint of the form:
// [<def>], <id>, <numBytes>, <target>, <numArgs>, <cc>, ...
void X86AsmPrinter::LowerPATCHPOINT(const MachineInstr &MI,
                                    X86MCInstLower &MCIL) {
  assert(Subtarget->is64Bit() && "Patchpoint currently only supports X86-64");

  SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());

  SM.recordPatchPoint(MI);

  PatchPointOpers opers(&MI);
  unsigned ScratchIdx = opers.getNextScratchIdx();
  unsigned EncodedBytes = 0;
  const MachineOperand &CalleeMO = opers.getCallTarget();

  // Check for null target. If target is non-null (i.e. is non-zero or is
  // symbolic) then emit a call.
  if (!(CalleeMO.isImm() && !CalleeMO.getImm())) {
    MCOperand CalleeMCOp;
    switch (CalleeMO.getType()) {
    default:
      /// FIXME: Add a verifier check for bad callee types.
      llvm_unreachable("Unrecognized callee operand type.");
    case MachineOperand::MO_Immediate:
      if (CalleeMO.getImm())
        CalleeMCOp = MCOperand::createImm(CalleeMO.getImm());
      break;
    case MachineOperand::MO_ExternalSymbol:
    case MachineOperand::MO_GlobalAddress:
      CalleeMCOp = MCIL.LowerSymbolOperand(CalleeMO,
                                           MCIL.GetSymbolFromOperand(CalleeMO));
      break;
    }

    // Emit MOV to materialize the target address and the CALL to target.
    // This is encoded with 12-13 bytes, depending on which register is used.
    unsigned ScratchReg = MI.getOperand(ScratchIdx).getReg();
    if (X86II::isX86_64ExtendedReg(ScratchReg))
      EncodedBytes = 13;
    else
      EncodedBytes = 12;

    EmitAndCountInstruction(
        MCInstBuilder(X86::MOV64ri).addReg(ScratchReg).addOperand(CalleeMCOp));
    // FIXME: Add retpoline support and remove this.
    if (Subtarget->useRetpolineIndirectCalls())
      report_fatal_error(
          "Lowering patchpoint with retpoline not yet implemented.");
    EmitAndCountInstruction(MCInstBuilder(X86::CALL64r).addReg(ScratchReg));
  }

  // Emit padding.
  unsigned NumBytes = opers.getNumPatchBytes();
  assert(NumBytes >= EncodedBytes &&
         "Patchpoint can't request size less than the length of a call.");

  EmitNops(*OutStreamer, NumBytes - EncodedBytes, Subtarget->is64Bit(),
           getSubtargetInfo());
}

void X86AsmPrinter::LowerPATCHABLE_EVENT_CALL(const MachineInstr &MI,
                                              X86MCInstLower &MCIL) {
  assert(Subtarget->is64Bit() && "XRay custom events only supports X86-64");

  // We want to emit the following pattern, which follows the x86 calling
  // convention to prepare for the trampoline call to be patched in.
  //
  //   .p2align 1, ...
  // .Lxray_event_sled_N:
  //   jmp +N                        // jump across the instrumentation sled
  //   ...                           // set up arguments in register
  //   callq __xray_CustomEvent@plt  // force dependency to symbol
  //   ...
  //   <jump here>
  //
  // After patching, it would look something like:
  //
  //   nopw (2-byte nop)
  //   ...
  //   callq __xrayCustomEvent  // already lowered
  //   ...
  //
  // ---
  // First we emit the label and the jump.
  auto CurSled = OutContext.createTempSymbol("xray_event_sled_", true);
  OutStreamer->AddComment("# XRay Custom Event Log");
  OutStreamer->EmitCodeAlignment(2);
  OutStreamer->EmitLabel(CurSled);

  // Use a two-byte `jmp`. This version of JMP takes an 8-bit relative offset as
  // an operand (computed as an offset from the jmp instruction).
  // FIXME: Find another less hacky way do force the relative jump.
  OutStreamer->EmitBinaryData("\xeb\x0f");

  // The default C calling convention will place two arguments into %rcx and
  // %rdx -- so we only work with those.
  unsigned DestRegs[] = {X86::RDI, X86::RSI};
  bool UsedMask[] = {false, false};
  // Filled out in loop.
  unsigned SrcRegs[] = {0, 0};

  // Then we put the operands in the %rdi and %rsi registers. We spill the
  // values in the register before we clobber them, and mark them as used in
  // UsedMask. In case the arguments are already in the correct register, we use
  // emit nops appropriately sized to keep the sled the same size in every
  // situation.
  for (unsigned I = 0; I < MI.getNumOperands(); ++I)
    if (auto Op = MCIL.LowerMachineOperand(&MI, MI.getOperand(I))) {
      assert(Op->isReg() && "Only support arguments in registers");
      SrcRegs[I] = Op->getReg();
      if (SrcRegs[I] != DestRegs[I]) {
        UsedMask[I] = true;
        EmitAndCountInstruction(
            MCInstBuilder(X86::PUSH64r).addReg(DestRegs[I]));
      } else {
        EmitNops(*OutStreamer, 4, Subtarget->is64Bit(), getSubtargetInfo());
      }
    }

  // Now that the register values are stashed, mov arguments into place.
  for (unsigned I = 0; I < MI.getNumOperands(); ++I)
    if (SrcRegs[I] != DestRegs[I])
      EmitAndCountInstruction(
          MCInstBuilder(X86::MOV64rr).addReg(DestRegs[I]).addReg(SrcRegs[I]));

  // We emit a hard dependency on the __xray_CustomEvent symbol, which is the
  // name of the trampoline to be implemented by the XRay runtime.
  auto TSym = OutContext.getOrCreateSymbol("__xray_CustomEvent");
  MachineOperand TOp = MachineOperand::CreateMCSymbol(TSym);
  if (isPositionIndependent())
    TOp.setTargetFlags(X86II::MO_PLT);

  // Emit the call instruction.
  EmitAndCountInstruction(MCInstBuilder(X86::CALL64pcrel32)
                              .addOperand(MCIL.LowerSymbolOperand(TOp, TSym)));

  // Restore caller-saved and used registers.
  for (unsigned I = sizeof UsedMask; I-- > 0;)
    if (UsedMask[I])
      EmitAndCountInstruction(MCInstBuilder(X86::POP64r).addReg(DestRegs[I]));
    else
      EmitNops(*OutStreamer, 1, Subtarget->is64Bit(), getSubtargetInfo());

  OutStreamer->AddComment("xray custom event end.");

  // Record the sled version. Older versions of this sled were spelled
  // differently, so we let the runtime handle the different offsets we're
  // using.
  recordSled(CurSled, MI, SledKind::CUSTOM_EVENT, 1);
}

void X86AsmPrinter::LowerPATCHABLE_TYPED_EVENT_CALL(const MachineInstr &MI,
                                                    X86MCInstLower &MCIL) {
  assert(Subtarget->is64Bit() && "XRay typed events only supports X86-64");

  // We want to emit the following pattern, which follows the x86 calling
  // convention to prepare for the trampoline call to be patched in.
  //
  //   .p2align 1, ...
  // .Lxray_event_sled_N:
  //   jmp +N                        // jump across the instrumentation sled
  //   ...                           // set up arguments in register
  //   callq __xray_TypedEvent@plt  // force dependency to symbol
  //   ...
  //   <jump here>
  //
  // After patching, it would look something like:
  //
  //   nopw (2-byte nop)
  //   ...
  //   callq __xrayTypedEvent  // already lowered
  //   ...
  //
  // ---
  // First we emit the label and the jump.
  auto CurSled = OutContext.createTempSymbol("xray_typed_event_sled_", true);
  OutStreamer->AddComment("# XRay Typed Event Log");
  OutStreamer->EmitCodeAlignment(2);
  OutStreamer->EmitLabel(CurSled);

  // Use a two-byte `jmp`. This version of JMP takes an 8-bit relative offset as
  // an operand (computed as an offset from the jmp instruction).
  // FIXME: Find another less hacky way do force the relative jump.
  OutStreamer->EmitBinaryData("\xeb\x14");

  // An x86-64 convention may place three arguments into %rcx, %rdx, and R8,
  // so we'll work with those. Or we may be called via SystemV, in which case
  // we don't have to do any translation.
  unsigned DestRegs[] = {X86::RDI, X86::RSI, X86::RDX};
  bool UsedMask[] = {false, false, false};

  // Will fill out src regs in the loop.
  unsigned SrcRegs[] = {0, 0, 0};

  // Then we put the operands in the SystemV registers. We spill the values in
  // the registers before we clobber them, and mark them as used in UsedMask.
  // In case the arguments are already in the correct register, we emit nops
  // appropriately sized to keep the sled the same size in every situation.
  for (unsigned I = 0; I < MI.getNumOperands(); ++I)
    if (auto Op = MCIL.LowerMachineOperand(&MI, MI.getOperand(I))) {
      // TODO: Is register only support adequate?
      assert(Op->isReg() && "Only supports arguments in registers");
      SrcRegs[I] = Op->getReg();
      if (SrcRegs[I] != DestRegs[I]) {
        UsedMask[I] = true;
        EmitAndCountInstruction(
            MCInstBuilder(X86::PUSH64r).addReg(DestRegs[I]));
      } else {
        EmitNops(*OutStreamer, 4, Subtarget->is64Bit(), getSubtargetInfo());
      }
    }

  // In the above loop we only stash all of the destination registers or emit
  // nops if the arguments are already in the right place. Doing the actually
  // moving is postponed until after all the registers are stashed so nothing
  // is clobbers. We've already added nops to account for the size of mov and
  // push if the register is in the right place, so we only have to worry about
  // emitting movs.
  for (unsigned I = 0; I < MI.getNumOperands(); ++I)
    if (UsedMask[I])
      EmitAndCountInstruction(
          MCInstBuilder(X86::MOV64rr).addReg(DestRegs[I]).addReg(SrcRegs[I]));

  // We emit a hard dependency on the __xray_TypedEvent symbol, which is the
  // name of the trampoline to be implemented by the XRay runtime.
  auto TSym = OutContext.getOrCreateSymbol("__xray_TypedEvent");
  MachineOperand TOp = MachineOperand::CreateMCSymbol(TSym);
  if (isPositionIndependent())
    TOp.setTargetFlags(X86II::MO_PLT);

  // Emit the call instruction.
  EmitAndCountInstruction(MCInstBuilder(X86::CALL64pcrel32)
                              .addOperand(MCIL.LowerSymbolOperand(TOp, TSym)));

  // Restore caller-saved and used registers.
  for (unsigned I = sizeof UsedMask; I-- > 0;)
    if (UsedMask[I])
      EmitAndCountInstruction(MCInstBuilder(X86::POP64r).addReg(DestRegs[I]));
    else
      EmitNops(*OutStreamer, 1, Subtarget->is64Bit(), getSubtargetInfo());

  OutStreamer->AddComment("xray typed event end.");

  // Record the sled version.
  recordSled(CurSled, MI, SledKind::TYPED_EVENT, 0);
}

void X86AsmPrinter::LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI,
                                                  X86MCInstLower &MCIL) {
  // We want to emit the following pattern:
  //
  //   .p2align 1, ...
  // .Lxray_sled_N:
  //   jmp .tmpN
  //   # 9 bytes worth of noops
  //
  // We need the 9 bytes because at runtime, we'd be patching over the full 11
  // bytes with the following pattern:
  //
  //   mov %r10, <function id, 32-bit>   // 6 bytes
  //   call <relative offset, 32-bits>   // 5 bytes
  //
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->EmitCodeAlignment(2);
  OutStreamer->EmitLabel(CurSled);

  // Use a two-byte `jmp`. This version of JMP takes an 8-bit relative offset as
  // an operand (computed as an offset from the jmp instruction).
  // FIXME: Find another less hacky way do force the relative jump.
  OutStreamer->EmitBytes("\xeb\x09");
  EmitNops(*OutStreamer, 9, Subtarget->is64Bit(), getSubtargetInfo());
  recordSled(CurSled, MI, SledKind::FUNCTION_ENTER);
}

void X86AsmPrinter::LowerPATCHABLE_RET(const MachineInstr &MI,
                                       X86MCInstLower &MCIL) {
  // Since PATCHABLE_RET takes the opcode of the return statement as an
  // argument, we use that to emit the correct form of the RET that we want.
  // i.e. when we see this:
  //
  //   PATCHABLE_RET X86::RET ...
  //
  // We should emit the RET followed by sleds.
  //
  //   .p2align 1, ...
  // .Lxray_sled_N:
  //   ret  # or equivalent instruction
  //   # 10 bytes worth of noops
  //
  // This just makes sure that the alignment for the next instruction is 2.
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->EmitCodeAlignment(2);
  OutStreamer->EmitLabel(CurSled);
  unsigned OpCode = MI.getOperand(0).getImm();
  MCInst Ret;
  Ret.setOpcode(OpCode);
  for (auto &MO : make_range(MI.operands_begin() + 1, MI.operands_end()))
    if (auto MaybeOperand = MCIL.LowerMachineOperand(&MI, MO))
      Ret.addOperand(MaybeOperand.getValue());
  OutStreamer->EmitInstruction(Ret, getSubtargetInfo());
  EmitNops(*OutStreamer, 10, Subtarget->is64Bit(), getSubtargetInfo());
  recordSled(CurSled, MI, SledKind::FUNCTION_EXIT);
}

void X86AsmPrinter::LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI,
                                             X86MCInstLower &MCIL) {
  // Like PATCHABLE_RET, we have the actual instruction in the operands to this
  // instruction so we lower that particular instruction and its operands.
  // Unlike PATCHABLE_RET though, we put the sled before the JMP, much like how
  // we do it for PATCHABLE_FUNCTION_ENTER. The sled should be very similar to
  // the PATCHABLE_FUNCTION_ENTER case, followed by the lowering of the actual
  // tail call much like how we have it in PATCHABLE_RET.
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->EmitCodeAlignment(2);
  OutStreamer->EmitLabel(CurSled);
  auto Target = OutContext.createTempSymbol();

  // Use a two-byte `jmp`. This version of JMP takes an 8-bit relative offset as
  // an operand (computed as an offset from the jmp instruction).
  // FIXME: Find another less hacky way do force the relative jump.
  OutStreamer->EmitBytes("\xeb\x09");
  EmitNops(*OutStreamer, 9, Subtarget->is64Bit(), getSubtargetInfo());
  OutStreamer->EmitLabel(Target);
  recordSled(CurSled, MI, SledKind::TAIL_CALL);

  unsigned OpCode = MI.getOperand(0).getImm();
  MCInst TC;
  TC.setOpcode(OpCode);

  // Before emitting the instruction, add a comment to indicate that this is
  // indeed a tail call.
  OutStreamer->AddComment("TAILCALL");
  for (auto &MO : make_range(MI.operands_begin() + 1, MI.operands_end()))
    if (auto MaybeOperand = MCIL.LowerMachineOperand(&MI, MO))
      TC.addOperand(MaybeOperand.getValue());
  OutStreamer->EmitInstruction(TC, getSubtargetInfo());
}

// Returns instruction preceding MBBI in MachineFunction.
// If MBBI is the first instruction of the first basic block, returns null.
static MachineBasicBlock::const_iterator
PrevCrossBBInst(MachineBasicBlock::const_iterator MBBI) {
  const MachineBasicBlock *MBB = MBBI->getParent();
  while (MBBI == MBB->begin()) {
    if (MBB == &MBB->getParent()->front())
      return MachineBasicBlock::const_iterator();
    MBB = MBB->getPrevNode();
    MBBI = MBB->end();
  }
  return --MBBI;
}

static const Constant *getConstantFromPool(const MachineInstr &MI,
                                           const MachineOperand &Op) {
  if (!Op.isCPI() || Op.getOffset() != 0)
    return nullptr;

  ArrayRef<MachineConstantPoolEntry> Constants =
      MI.getParent()->getParent()->getConstantPool()->getConstants();
  const MachineConstantPoolEntry &ConstantEntry = Constants[Op.getIndex()];

  // Bail if this is a machine constant pool entry, we won't be able to dig out
  // anything useful.
  if (ConstantEntry.isMachineConstantPoolEntry())
    return nullptr;

  const Constant *C = ConstantEntry.Val.ConstVal;
  assert((!C || ConstantEntry.getType() == C->getType()) &&
         "Expected a constant of the same type!");
  return C;
}

static std::string getShuffleComment(const MachineInstr *MI, unsigned SrcOp1Idx,
                                     unsigned SrcOp2Idx, ArrayRef<int> Mask) {
  std::string Comment;

  // Compute the name for a register. This is really goofy because we have
  // multiple instruction printers that could (in theory) use different
  // names. Fortunately most people use the ATT style (outside of Windows)
  // and they actually agree on register naming here. Ultimately, this is
  // a comment, and so its OK if it isn't perfect.
  auto GetRegisterName = [](unsigned RegNum) -> StringRef {
    return X86ATTInstPrinter::getRegisterName(RegNum);
  };

  const MachineOperand &DstOp = MI->getOperand(0);
  const MachineOperand &SrcOp1 = MI->getOperand(SrcOp1Idx);
  const MachineOperand &SrcOp2 = MI->getOperand(SrcOp2Idx);

  StringRef DstName = DstOp.isReg() ? GetRegisterName(DstOp.getReg()) : "mem";
  StringRef Src1Name =
      SrcOp1.isReg() ? GetRegisterName(SrcOp1.getReg()) : "mem";
  StringRef Src2Name =
      SrcOp2.isReg() ? GetRegisterName(SrcOp2.getReg()) : "mem";

  // One source operand, fix the mask to print all elements in one span.
  SmallVector<int, 8> ShuffleMask(Mask.begin(), Mask.end());
  if (Src1Name == Src2Name)
    for (int i = 0, e = ShuffleMask.size(); i != e; ++i)
      if (ShuffleMask[i] >= e)
        ShuffleMask[i] -= e;

  raw_string_ostream CS(Comment);
  CS << DstName;

  // Handle AVX512 MASK/MASXZ write mask comments.
  // MASK: zmmX {%kY}
  // MASKZ: zmmX {%kY} {z}
  if (SrcOp1Idx > 1) {
    assert((SrcOp1Idx == 2 || SrcOp1Idx == 3) && "Unexpected writemask");

    const MachineOperand &WriteMaskOp = MI->getOperand(SrcOp1Idx - 1);
    if (WriteMaskOp.isReg()) {
      CS << " {%" << GetRegisterName(WriteMaskOp.getReg()) << "}";

      if (SrcOp1Idx == 2) {
        CS << " {z}";
      }
    }
  }

  CS << " = ";

  for (int i = 0, e = ShuffleMask.size(); i != e; ++i) {
    if (i != 0)
      CS << ",";
    if (ShuffleMask[i] == SM_SentinelZero) {
      CS << "zero";
      continue;
    }

    // Otherwise, it must come from src1 or src2.  Print the span of elements
    // that comes from this src.
    bool isSrc1 = ShuffleMask[i] < (int)e;
    CS << (isSrc1 ? Src1Name : Src2Name) << '[';

    bool IsFirst = true;
    while (i != e && ShuffleMask[i] != SM_SentinelZero &&
           (ShuffleMask[i] < (int)e) == isSrc1) {
      if (!IsFirst)
        CS << ',';
      else
        IsFirst = false;
      if (ShuffleMask[i] == SM_SentinelUndef)
        CS << "u";
      else
        CS << ShuffleMask[i] % (int)e;
      ++i;
    }
    CS << ']';
    --i; // For loop increments element #.
  }
  CS.flush();

  return Comment;
}

static void printConstant(const APInt &Val, raw_ostream &CS) {
  if (Val.getBitWidth() <= 64) {
    CS << Val.getZExtValue();
  } else {
    // print multi-word constant as (w0,w1)
    CS << "(";
    for (int i = 0, N = Val.getNumWords(); i < N; ++i) {
      if (i > 0)
        CS << ",";
      CS << Val.getRawData()[i];
    }
    CS << ")";
  }
}

static void printConstant(const APFloat &Flt, raw_ostream &CS) {
  SmallString<32> Str;
  // Force scientific notation to distinquish from integers.
  Flt.toString(Str, 0, 0);
  CS << Str;
}

static void printConstant(const Constant *COp, raw_ostream &CS) {
  if (isa<UndefValue>(COp)) {
    CS << "u";
  } else if (auto *CI = dyn_cast<ConstantInt>(COp)) {
    printConstant(CI->getValue(), CS);
  } else if (auto *CF = dyn_cast<ConstantFP>(COp)) {
    printConstant(CF->getValueAPF(), CS);
  } else {
    CS << "?";
  }
}

void X86AsmPrinter::EmitSEHInstruction(const MachineInstr *MI) {
  assert(MF->hasWinCFI() && "SEH_ instruction in function without WinCFI?");
  assert(getSubtarget().isOSWindows() && "SEH_ instruction Windows only");
  const X86RegisterInfo *RI =
      MF->getSubtarget<X86Subtarget>().getRegisterInfo();

  // Use the .cv_fpo directives if we're emitting CodeView on 32-bit x86.
  if (EmitFPOData) {
    X86TargetStreamer *XTS =
        static_cast<X86TargetStreamer *>(OutStreamer->getTargetStreamer());
    switch (MI->getOpcode()) {
    case X86::SEH_PushReg:
      XTS->emitFPOPushReg(MI->getOperand(0).getImm());
      break;
    case X86::SEH_StackAlloc:
      XTS->emitFPOStackAlloc(MI->getOperand(0).getImm());
      break;
    case X86::SEH_StackAlign:
      XTS->emitFPOStackAlign(MI->getOperand(0).getImm());
      break;
    case X86::SEH_SetFrame:
      assert(MI->getOperand(1).getImm() == 0 &&
             ".cv_fpo_setframe takes no offset");
      XTS->emitFPOSetFrame(MI->getOperand(0).getImm());
      break;
    case X86::SEH_EndPrologue:
      XTS->emitFPOEndPrologue();
      break;
    case X86::SEH_SaveReg:
    case X86::SEH_SaveXMM:
    case X86::SEH_PushFrame:
      llvm_unreachable("SEH_ directive incompatible with FPO");
      break;
    default:
      llvm_unreachable("expected SEH_ instruction");
    }
    return;
  }

  // Otherwise, use the .seh_ directives for all other Windows platforms.
  switch (MI->getOpcode()) {
  case X86::SEH_PushReg:
    OutStreamer->EmitWinCFIPushReg(
        RI->getSEHRegNum(MI->getOperand(0).getImm()));
    break;

  case X86::SEH_SaveReg:
    OutStreamer->EmitWinCFISaveReg(RI->getSEHRegNum(MI->getOperand(0).getImm()),
                                   MI->getOperand(1).getImm());
    break;

  case X86::SEH_SaveXMM:
    OutStreamer->EmitWinCFISaveXMM(RI->getSEHRegNum(MI->getOperand(0).getImm()),
                                   MI->getOperand(1).getImm());
    break;

  case X86::SEH_StackAlloc:
    OutStreamer->EmitWinCFIAllocStack(MI->getOperand(0).getImm());
    break;

  case X86::SEH_SetFrame:
    OutStreamer->EmitWinCFISetFrame(
        RI->getSEHRegNum(MI->getOperand(0).getImm()),
        MI->getOperand(1).getImm());
    break;

  case X86::SEH_PushFrame:
    OutStreamer->EmitWinCFIPushFrame(MI->getOperand(0).getImm());
    break;

  case X86::SEH_EndPrologue:
    OutStreamer->EmitWinCFIEndProlog();
    break;

  default:
    llvm_unreachable("expected SEH_ instruction");
  }
}

static unsigned getRegisterWidth(const MCOperandInfo &Info) {
  if (Info.RegClass == X86::VR128RegClassID ||
      Info.RegClass == X86::VR128XRegClassID)
    return 128;
  if (Info.RegClass == X86::VR256RegClassID ||
      Info.RegClass == X86::VR256XRegClassID)
    return 256;
  if (Info.RegClass == X86::VR512RegClassID)
    return 512;
  llvm_unreachable("Unknown register class!");
}

void X86AsmPrinter::EmitInstruction(const MachineInstr *MI) {
  X86MCInstLower MCInstLowering(*MF, *this);
  const X86RegisterInfo *RI =
      MF->getSubtarget<X86Subtarget>().getRegisterInfo();

  // Add a comment about EVEX-2-VEX compression for AVX-512 instrs that
  // are compressed from EVEX encoding to VEX encoding.
  if (TM.Options.MCOptions.ShowMCEncoding) {
    if (MI->getAsmPrinterFlags() & X86::AC_EVEX_2_VEX)
      OutStreamer->AddComment("EVEX TO VEX Compression ", false);
  }

  switch (MI->getOpcode()) {
  case TargetOpcode::DBG_VALUE:
    llvm_unreachable("Should be handled target independently");

  // Emit nothing here but a comment if we can.
  case X86::Int_MemBarrier:
    OutStreamer->emitRawComment("MEMBARRIER");
    return;

  case X86::EH_RETURN:
  case X86::EH_RETURN64: {
    // Lower these as normal, but add some comments.
    unsigned Reg = MI->getOperand(0).getReg();
    OutStreamer->AddComment(StringRef("eh_return, addr: %") +
                            X86ATTInstPrinter::getRegisterName(Reg));
    break;
  }
  case X86::CLEANUPRET: {
    // Lower these as normal, but add some comments.
    OutStreamer->AddComment("CLEANUPRET");
    break;
  }

  case X86::CATCHRET: {
    // Lower these as normal, but add some comments.
    OutStreamer->AddComment("CATCHRET");
    break;
  }

  case X86::TAILJMPr:
  case X86::TAILJMPm:
  case X86::TAILJMPd:
  case X86::TAILJMPd_CC:
  case X86::TAILJMPr64:
  case X86::TAILJMPm64:
  case X86::TAILJMPd64:
  case X86::TAILJMPd64_CC:
  case X86::TAILJMPr64_REX:
  case X86::TAILJMPm64_REX:
    // Lower these as normal, but add some comments.
    OutStreamer->AddComment("TAILCALL");
    break;

  case X86::TLS_addr32:
  case X86::TLS_addr64:
  case X86::TLS_base_addr32:
  case X86::TLS_base_addr64:
    return LowerTlsAddr(MCInstLowering, *MI);

  case X86::MOVPC32r: {
    // This is a pseudo op for a two instruction sequence with a label, which
    // looks like:
    //     call "L1$pb"
    // "L1$pb":
    //     popl %esi

    // Emit the call.
    MCSymbol *PICBase = MF->getPICBaseSymbol();
    // FIXME: We would like an efficient form for this, so we don't have to do a
    // lot of extra uniquing.
    EmitAndCountInstruction(
        MCInstBuilder(X86::CALLpcrel32)
            .addExpr(MCSymbolRefExpr::create(PICBase, OutContext)));

    const X86FrameLowering *FrameLowering =
        MF->getSubtarget<X86Subtarget>().getFrameLowering();
    bool hasFP = FrameLowering->hasFP(*MF);

    // TODO: This is needed only if we require precise CFA.
    bool HasActiveDwarfFrame = OutStreamer->getNumFrameInfos() &&
                               !OutStreamer->getDwarfFrameInfos().back().End;

    int stackGrowth = -RI->getSlotSize();

    if (HasActiveDwarfFrame && !hasFP) {
      OutStreamer->EmitCFIAdjustCfaOffset(-stackGrowth);
    }

    // Emit the label.
    OutStreamer->EmitLabel(PICBase);

    // popl $reg
    EmitAndCountInstruction(
        MCInstBuilder(X86::POP32r).addReg(MI->getOperand(0).getReg()));

    if (HasActiveDwarfFrame && !hasFP) {
      OutStreamer->EmitCFIAdjustCfaOffset(stackGrowth);
    }
    return;
  }

  case X86::ADD32ri: {
    // Lower the MO_GOT_ABSOLUTE_ADDRESS form of ADD32ri.
    if (MI->getOperand(2).getTargetFlags() != X86II::MO_GOT_ABSOLUTE_ADDRESS)
      break;

    // Okay, we have something like:
    //  EAX = ADD32ri EAX, MO_GOT_ABSOLUTE_ADDRESS(@MYGLOBAL)

    // For this, we want to print something like:
    //   MYGLOBAL + (. - PICBASE)
    // However, we can't generate a ".", so just emit a new label here and refer
    // to it.
    MCSymbol *DotSym = OutContext.createTempSymbol();
    OutStreamer->EmitLabel(DotSym);

    // Now that we have emitted the label, lower the complex operand expression.
    MCSymbol *OpSym = MCInstLowering.GetSymbolFromOperand(MI->getOperand(2));

    const MCExpr *DotExpr = MCSymbolRefExpr::create(DotSym, OutContext);
    const MCExpr *PICBase =
        MCSymbolRefExpr::create(MF->getPICBaseSymbol(), OutContext);
    DotExpr = MCBinaryExpr::createSub(DotExpr, PICBase, OutContext);

    DotExpr = MCBinaryExpr::createAdd(
        MCSymbolRefExpr::create(OpSym, OutContext), DotExpr, OutContext);

    EmitAndCountInstruction(MCInstBuilder(X86::ADD32ri)
                                .addReg(MI->getOperand(0).getReg())
                                .addReg(MI->getOperand(1).getReg())
                                .addExpr(DotExpr));
    return;
  }
  case TargetOpcode::STATEPOINT:
    return LowerSTATEPOINT(*MI, MCInstLowering);

  case TargetOpcode::FAULTING_OP:
    return LowerFAULTING_OP(*MI, MCInstLowering);

  case TargetOpcode::FENTRY_CALL:
    return LowerFENTRY_CALL(*MI, MCInstLowering);

  case TargetOpcode::PATCHABLE_OP:
    return LowerPATCHABLE_OP(*MI, MCInstLowering);

  case TargetOpcode::STACKMAP:
    return LowerSTACKMAP(*MI);

  case TargetOpcode::PATCHPOINT:
    return LowerPATCHPOINT(*MI, MCInstLowering);

  case TargetOpcode::PATCHABLE_FUNCTION_ENTER:
    return LowerPATCHABLE_FUNCTION_ENTER(*MI, MCInstLowering);

  case TargetOpcode::PATCHABLE_RET:
    return LowerPATCHABLE_RET(*MI, MCInstLowering);

  case TargetOpcode::PATCHABLE_TAIL_CALL:
    return LowerPATCHABLE_TAIL_CALL(*MI, MCInstLowering);

  case TargetOpcode::PATCHABLE_EVENT_CALL:
    return LowerPATCHABLE_EVENT_CALL(*MI, MCInstLowering);

  case TargetOpcode::PATCHABLE_TYPED_EVENT_CALL:
    return LowerPATCHABLE_TYPED_EVENT_CALL(*MI, MCInstLowering);

  case X86::MORESTACK_RET:
    EmitAndCountInstruction(MCInstBuilder(getRetOpcode(*Subtarget)));
    return;

  case X86::MORESTACK_RET_RESTORE_R10:
    // Return, then restore R10.
    EmitAndCountInstruction(MCInstBuilder(getRetOpcode(*Subtarget)));
    EmitAndCountInstruction(
        MCInstBuilder(X86::MOV64rr).addReg(X86::R10).addReg(X86::RAX));
    return;

  case X86::SEH_PushReg:
  case X86::SEH_SaveReg:
  case X86::SEH_SaveXMM:
  case X86::SEH_StackAlloc:
  case X86::SEH_StackAlign:
  case X86::SEH_SetFrame:
  case X86::SEH_PushFrame:
  case X86::SEH_EndPrologue:
    EmitSEHInstruction(MI);
    return;

  case X86::SEH_Epilogue: {
    assert(MF->hasWinCFI() && "SEH_ instruction in function without WinCFI?");
    MachineBasicBlock::const_iterator MBBI(MI);
    // Check if preceded by a call and emit nop if so.
    for (MBBI = PrevCrossBBInst(MBBI);
         MBBI != MachineBasicBlock::const_iterator();
         MBBI = PrevCrossBBInst(MBBI)) {
      // Conservatively assume that pseudo instructions don't emit code and keep
      // looking for a call. We may emit an unnecessary nop in some cases.
      if (!MBBI->isPseudo()) {
        if (MBBI->isCall())
          EmitAndCountInstruction(MCInstBuilder(X86::NOOP));
        break;
      }
    }
    return;
  }

  // Lower PSHUFB and VPERMILP normally but add a comment if we can find
  // a constant shuffle mask. We won't be able to do this at the MC layer
  // because the mask isn't an immediate.
  case X86::PSHUFBrm:
  case X86::VPSHUFBrm:
  case X86::VPSHUFBYrm:
  case X86::VPSHUFBZ128rm:
  case X86::VPSHUFBZ128rmk:
  case X86::VPSHUFBZ128rmkz:
  case X86::VPSHUFBZ256rm:
  case X86::VPSHUFBZ256rmk:
  case X86::VPSHUFBZ256rmkz:
  case X86::VPSHUFBZrm:
  case X86::VPSHUFBZrmk:
  case X86::VPSHUFBZrmkz: {
    if (!OutStreamer->isVerboseAsm())
      break;
    unsigned SrcIdx, MaskIdx;
    switch (MI->getOpcode()) {
    default: llvm_unreachable("Invalid opcode");
    case X86::PSHUFBrm:
    case X86::VPSHUFBrm:
    case X86::VPSHUFBYrm:
    case X86::VPSHUFBZ128rm:
    case X86::VPSHUFBZ256rm:
    case X86::VPSHUFBZrm:
      SrcIdx = 1; MaskIdx = 5; break;
    case X86::VPSHUFBZ128rmkz:
    case X86::VPSHUFBZ256rmkz:
    case X86::VPSHUFBZrmkz:
      SrcIdx = 2; MaskIdx = 6; break;
    case X86::VPSHUFBZ128rmk:
    case X86::VPSHUFBZ256rmk:
    case X86::VPSHUFBZrmk:
      SrcIdx = 3; MaskIdx = 7; break;
    }

    assert(MI->getNumOperands() >= 6 &&
           "We should always have at least 6 operands!");

    const MachineOperand &MaskOp = MI->getOperand(MaskIdx);
    if (auto *C = getConstantFromPool(*MI, MaskOp)) {
      unsigned Width = getRegisterWidth(MI->getDesc().OpInfo[0]);
      SmallVector<int, 64> Mask;
      DecodePSHUFBMask(C, Width, Mask);
      if (!Mask.empty())
        OutStreamer->AddComment(getShuffleComment(MI, SrcIdx, SrcIdx, Mask),
                                !EnablePrintSchedInfo);
    }
    break;
  }

  case X86::VPERMILPSrm:
  case X86::VPERMILPSYrm:
  case X86::VPERMILPSZ128rm:
  case X86::VPERMILPSZ128rmk:
  case X86::VPERMILPSZ128rmkz:
  case X86::VPERMILPSZ256rm:
  case X86::VPERMILPSZ256rmk:
  case X86::VPERMILPSZ256rmkz:
  case X86::VPERMILPSZrm:
  case X86::VPERMILPSZrmk:
  case X86::VPERMILPSZrmkz:
  case X86::VPERMILPDrm:
  case X86::VPERMILPDYrm:
  case X86::VPERMILPDZ128rm:
  case X86::VPERMILPDZ128rmk:
  case X86::VPERMILPDZ128rmkz:
  case X86::VPERMILPDZ256rm:
  case X86::VPERMILPDZ256rmk:
  case X86::VPERMILPDZ256rmkz:
  case X86::VPERMILPDZrm:
  case X86::VPERMILPDZrmk:
  case X86::VPERMILPDZrmkz: {
    if (!OutStreamer->isVerboseAsm())
      break;
    unsigned SrcIdx, MaskIdx;
    unsigned ElSize;
    switch (MI->getOpcode()) {
    default: llvm_unreachable("Invalid opcode");
    case X86::VPERMILPSrm:
    case X86::VPERMILPSYrm:
    case X86::VPERMILPSZ128rm:
    case X86::VPERMILPSZ256rm:
    case X86::VPERMILPSZrm:
      SrcIdx = 1; MaskIdx = 5; ElSize = 32; break;
    case X86::VPERMILPSZ128rmkz:
    case X86::VPERMILPSZ256rmkz:
    case X86::VPERMILPSZrmkz:
      SrcIdx = 2; MaskIdx = 6; ElSize = 32; break;
    case X86::VPERMILPSZ128rmk:
    case X86::VPERMILPSZ256rmk:
    case X86::VPERMILPSZrmk:
      SrcIdx = 3; MaskIdx = 7; ElSize = 32; break;
    case X86::VPERMILPDrm:
    case X86::VPERMILPDYrm:
    case X86::VPERMILPDZ128rm:
    case X86::VPERMILPDZ256rm:
    case X86::VPERMILPDZrm:
      SrcIdx = 1; MaskIdx = 5; ElSize = 64; break;
    case X86::VPERMILPDZ128rmkz:
    case X86::VPERMILPDZ256rmkz:
    case X86::VPERMILPDZrmkz:
      SrcIdx = 2; MaskIdx = 6; ElSize = 64; break;
    case X86::VPERMILPDZ128rmk:
    case X86::VPERMILPDZ256rmk:
    case X86::VPERMILPDZrmk:
      SrcIdx = 3; MaskIdx = 7; ElSize = 64; break;
    }

    assert(MI->getNumOperands() >= 6 &&
           "We should always have at least 6 operands!");

    const MachineOperand &MaskOp = MI->getOperand(MaskIdx);
    if (auto *C = getConstantFromPool(*MI, MaskOp)) {
      unsigned Width = getRegisterWidth(MI->getDesc().OpInfo[0]);
      SmallVector<int, 16> Mask;
      DecodeVPERMILPMask(C, ElSize, Width, Mask);
      if (!Mask.empty())
        OutStreamer->AddComment(getShuffleComment(MI, SrcIdx, SrcIdx, Mask),
                                !EnablePrintSchedInfo);
    }
    break;
  }

  case X86::VPERMIL2PDrm:
  case X86::VPERMIL2PSrm:
  case X86::VPERMIL2PDYrm:
  case X86::VPERMIL2PSYrm: {
    if (!OutStreamer->isVerboseAsm())
      break;
    assert(MI->getNumOperands() >= 8 &&
           "We should always have at least 8 operands!");

    const MachineOperand &CtrlOp = MI->getOperand(MI->getNumOperands() - 1);
    if (!CtrlOp.isImm())
      break;

    unsigned ElSize;
    switch (MI->getOpcode()) {
    default: llvm_unreachable("Invalid opcode");
    case X86::VPERMIL2PSrm: case X86::VPERMIL2PSYrm: ElSize = 32; break;
    case X86::VPERMIL2PDrm: case X86::VPERMIL2PDYrm: ElSize = 64; break;
    }

    const MachineOperand &MaskOp = MI->getOperand(6);
    if (auto *C = getConstantFromPool(*MI, MaskOp)) {
      unsigned Width = getRegisterWidth(MI->getDesc().OpInfo[0]);
      SmallVector<int, 16> Mask;
      DecodeVPERMIL2PMask(C, (unsigned)CtrlOp.getImm(), ElSize, Width, Mask);
      if (!Mask.empty())
        OutStreamer->AddComment(getShuffleComment(MI, 1, 2, Mask),
                                !EnablePrintSchedInfo);
    }
    break;
  }

  case X86::VPPERMrrm: {
    if (!OutStreamer->isVerboseAsm())
      break;
    assert(MI->getNumOperands() >= 7 &&
           "We should always have at least 7 operands!");

    const MachineOperand &MaskOp = MI->getOperand(6);
    if (auto *C = getConstantFromPool(*MI, MaskOp)) {
      unsigned Width = getRegisterWidth(MI->getDesc().OpInfo[0]);
      SmallVector<int, 16> Mask;
      DecodeVPPERMMask(C, Width, Mask);
      if (!Mask.empty())
        OutStreamer->AddComment(getShuffleComment(MI, 1, 2, Mask),
                                !EnablePrintSchedInfo);
    }
    break;
  }

  case X86::MMX_MOVQ64rm: {
    if (!OutStreamer->isVerboseAsm())
      break;
    if (MI->getNumOperands() <= 4)
      break;
    if (auto *C = getConstantFromPool(*MI, MI->getOperand(4))) {
      std::string Comment;
      raw_string_ostream CS(Comment);
      const MachineOperand &DstOp = MI->getOperand(0);
      CS << X86ATTInstPrinter::getRegisterName(DstOp.getReg()) << " = ";
      if (auto *CF = dyn_cast<ConstantFP>(C)) {
        CS << "0x" << CF->getValueAPF().bitcastToAPInt().toString(16, false);
        OutStreamer->AddComment(CS.str(), !EnablePrintSchedInfo);
      }
    }
    break;
  }

#define MOV_CASE(Prefix, Suffix)                                               \
  case X86::Prefix##MOVAPD##Suffix##rm:                                        \
  case X86::Prefix##MOVAPS##Suffix##rm:                                        \
  case X86::Prefix##MOVUPD##Suffix##rm:                                        \
  case X86::Prefix##MOVUPS##Suffix##rm:                                        \
  case X86::Prefix##MOVDQA##Suffix##rm:                                        \
  case X86::Prefix##MOVDQU##Suffix##rm:

#define MOV_AVX512_CASE(Suffix)                                                \
  case X86::VMOVDQA64##Suffix##rm:                                             \
  case X86::VMOVDQA32##Suffix##rm:                                             \
  case X86::VMOVDQU64##Suffix##rm:                                             \
  case X86::VMOVDQU32##Suffix##rm:                                             \
  case X86::VMOVDQU16##Suffix##rm:                                             \
  case X86::VMOVDQU8##Suffix##rm:                                              \
  case X86::VMOVAPS##Suffix##rm:                                               \
  case X86::VMOVAPD##Suffix##rm:                                               \
  case X86::VMOVUPS##Suffix##rm:                                               \
  case X86::VMOVUPD##Suffix##rm:

#define CASE_ALL_MOV_RM()                                                      \
  MOV_CASE(, )   /* SSE */                                                     \
  MOV_CASE(V, )  /* AVX-128 */                                                 \
  MOV_CASE(V, Y) /* AVX-256 */                                                 \
  MOV_AVX512_CASE(Z)                                                           \
  MOV_AVX512_CASE(Z256)                                                        \
  MOV_AVX512_CASE(Z128)

    // For loads from a constant pool to a vector register, print the constant
    // loaded.
    CASE_ALL_MOV_RM()
  case X86::VBROADCASTF128:
  case X86::VBROADCASTI128:
  case X86::VBROADCASTF32X4Z256rm:
  case X86::VBROADCASTF32X4rm:
  case X86::VBROADCASTF32X8rm:
  case X86::VBROADCASTF64X2Z128rm:
  case X86::VBROADCASTF64X2rm:
  case X86::VBROADCASTF64X4rm:
  case X86::VBROADCASTI32X4Z256rm:
  case X86::VBROADCASTI32X4rm:
  case X86::VBROADCASTI32X8rm:
  case X86::VBROADCASTI64X2Z128rm:
  case X86::VBROADCASTI64X2rm:
  case X86::VBROADCASTI64X4rm:
    if (!OutStreamer->isVerboseAsm())
      break;
    if (MI->getNumOperands() <= 4)
      break;
    if (auto *C = getConstantFromPool(*MI, MI->getOperand(4))) {
      int NumLanes = 1;
      // Override NumLanes for the broadcast instructions.
      switch (MI->getOpcode()) {
      case X86::VBROADCASTF128:        NumLanes = 2; break;
      case X86::VBROADCASTI128:        NumLanes = 2; break;
      case X86::VBROADCASTF32X4Z256rm: NumLanes = 2; break;
      case X86::VBROADCASTF32X4rm:     NumLanes = 4; break;
      case X86::VBROADCASTF32X8rm:     NumLanes = 2; break;
      case X86::VBROADCASTF64X2Z128rm: NumLanes = 2; break;
      case X86::VBROADCASTF64X2rm:     NumLanes = 4; break;
      case X86::VBROADCASTF64X4rm:     NumLanes = 2; break;
      case X86::VBROADCASTI32X4Z256rm: NumLanes = 2; break;
      case X86::VBROADCASTI32X4rm:     NumLanes = 4; break;
      case X86::VBROADCASTI32X8rm:     NumLanes = 2; break;
      case X86::VBROADCASTI64X2Z128rm: NumLanes = 2; break;
      case X86::VBROADCASTI64X2rm:     NumLanes = 4; break;
      case X86::VBROADCASTI64X4rm:     NumLanes = 2; break;
      }

      std::string Comment;
      raw_string_ostream CS(Comment);
      const MachineOperand &DstOp = MI->getOperand(0);
      CS << X86ATTInstPrinter::getRegisterName(DstOp.getReg()) << " = ";
      if (auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
        CS << "[";
        for (int l = 0; l != NumLanes; ++l) {
          for (int i = 0, NumElements = CDS->getNumElements(); i < NumElements;
               ++i) {
            if (i != 0 || l != 0)
              CS << ",";
            if (CDS->getElementType()->isIntegerTy())
              printConstant(CDS->getElementAsAPInt(i), CS);
            else if (CDS->getElementType()->isHalfTy() ||
                     CDS->getElementType()->isFloatTy() ||
                     CDS->getElementType()->isDoubleTy())
              printConstant(CDS->getElementAsAPFloat(i), CS);
            else
              CS << "?";
          }
        }
        CS << "]";
        OutStreamer->AddComment(CS.str(), !EnablePrintSchedInfo);
      } else if (auto *CV = dyn_cast<ConstantVector>(C)) {
        CS << "<";
        for (int l = 0; l != NumLanes; ++l) {
          for (int i = 0, NumOperands = CV->getNumOperands(); i < NumOperands;
               ++i) {
            if (i != 0 || l != 0)
              CS << ",";
            printConstant(CV->getOperand(i), CS);
          }
        }
        CS << ">";
        OutStreamer->AddComment(CS.str(), !EnablePrintSchedInfo);
      }
    }
    break;
  case X86::MOVDDUPrm:
  case X86::VMOVDDUPrm:
  case X86::VMOVDDUPZ128rm:
  case X86::VBROADCASTSSrm:
  case X86::VBROADCASTSSYrm:
  case X86::VBROADCASTSSZ128m:
  case X86::VBROADCASTSSZ256m:
  case X86::VBROADCASTSSZm:
  case X86::VBROADCASTSDYrm:
  case X86::VBROADCASTSDZ256m:
  case X86::VBROADCASTSDZm:
  case X86::VPBROADCASTBrm:
  case X86::VPBROADCASTBYrm:
  case X86::VPBROADCASTBZ128m:
  case X86::VPBROADCASTBZ256m:
  case X86::VPBROADCASTBZm:
  case X86::VPBROADCASTDrm:
  case X86::VPBROADCASTDYrm:
  case X86::VPBROADCASTDZ128m:
  case X86::VPBROADCASTDZ256m:
  case X86::VPBROADCASTDZm:
  case X86::VPBROADCASTQrm:
  case X86::VPBROADCASTQYrm:
  case X86::VPBROADCASTQZ128m:
  case X86::VPBROADCASTQZ256m:
  case X86::VPBROADCASTQZm:
  case X86::VPBROADCASTWrm:
  case X86::VPBROADCASTWYrm:
  case X86::VPBROADCASTWZ128m:
  case X86::VPBROADCASTWZ256m:
  case X86::VPBROADCASTWZm:
    if (!OutStreamer->isVerboseAsm())
      break;
    if (MI->getNumOperands() <= 4)
      break;
    if (auto *C = getConstantFromPool(*MI, MI->getOperand(4))) {
      int NumElts;
      switch (MI->getOpcode()) {
      default: llvm_unreachable("Invalid opcode");
      case X86::MOVDDUPrm:         NumElts = 2;  break;
      case X86::VMOVDDUPrm:        NumElts = 2;  break;
      case X86::VMOVDDUPZ128rm:    NumElts = 2;  break;
      case X86::VBROADCASTSSrm:    NumElts = 4;  break;
      case X86::VBROADCASTSSYrm:   NumElts = 8;  break;
      case X86::VBROADCASTSSZ128m: NumElts = 4;  break;
      case X86::VBROADCASTSSZ256m: NumElts = 8;  break;
      case X86::VBROADCASTSSZm:    NumElts = 16; break;
      case X86::VBROADCASTSDYrm:   NumElts = 4;  break;
      case X86::VBROADCASTSDZ256m: NumElts = 4;  break;
      case X86::VBROADCASTSDZm:    NumElts = 8;  break;
      case X86::VPBROADCASTBrm:    NumElts = 16; break;
      case X86::VPBROADCASTBYrm:   NumElts = 32; break;
      case X86::VPBROADCASTBZ128m: NumElts = 16; break;
      case X86::VPBROADCASTBZ256m: NumElts = 32; break;
      case X86::VPBROADCASTBZm:    NumElts = 64; break;
      case X86::VPBROADCASTDrm:    NumElts = 4;  break;
      case X86::VPBROADCASTDYrm:   NumElts = 8;  break;
      case X86::VPBROADCASTDZ128m: NumElts = 4;  break;
      case X86::VPBROADCASTDZ256m: NumElts = 8;  break;
      case X86::VPBROADCASTDZm:    NumElts = 16; break;
      case X86::VPBROADCASTQrm:    NumElts = 2;  break;
      case X86::VPBROADCASTQYrm:   NumElts = 4;  break;
      case X86::VPBROADCASTQZ128m: NumElts = 2;  break;
      case X86::VPBROADCASTQZ256m: NumElts = 4;  break;
      case X86::VPBROADCASTQZm:    NumElts = 8;  break;
      case X86::VPBROADCASTWrm:    NumElts = 8;  break;
      case X86::VPBROADCASTWYrm:   NumElts = 16; break;
      case X86::VPBROADCASTWZ128m: NumElts = 8;  break;
      case X86::VPBROADCASTWZ256m: NumElts = 16; break;
      case X86::VPBROADCASTWZm:    NumElts = 32; break;
      }

      std::string Comment;
      raw_string_ostream CS(Comment);
      const MachineOperand &DstOp = MI->getOperand(0);
      CS << X86ATTInstPrinter::getRegisterName(DstOp.getReg()) << " = ";
      CS << "[";
      for (int i = 0; i != NumElts; ++i) {
        if (i != 0)
          CS << ",";
        printConstant(C, CS);
      }
      CS << "]";
      OutStreamer->AddComment(CS.str(), !EnablePrintSchedInfo);
    }
  }

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  if (MI->getAsmPrinterFlag(MachineInstr::NoSchedComment))
    TmpInst.setFlags(TmpInst.getFlags() | X86::NO_SCHED_INFO);

  // Stackmap shadows cannot include branch targets, so we can count the bytes
  // in a call towards the shadow, but must ensure that the no thread returns
  // in to the stackmap shadow.  The only way to achieve this is if the call
  // is at the end of the shadow.
  if (MI->isCall()) {
    // Count then size of the call towards the shadow
    SMShadowTracker.count(TmpInst, getSubtargetInfo(), CodeEmitter.get());
    // Then flush the shadow so that we fill with nops before the call, not
    // after it.
    SMShadowTracker.emitShadowPadding(*OutStreamer, getSubtargetInfo());
    // Then emit the call
    OutStreamer->EmitInstruction(TmpInst, getSubtargetInfo());
    return;
  }

  EmitAndCountInstruction(TmpInst);
}
