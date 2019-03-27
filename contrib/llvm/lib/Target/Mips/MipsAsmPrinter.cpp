//===- MipsAsmPrinter.cpp - Mips LLVM Assembly Printer --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format MIPS assembly language.
//
//===----------------------------------------------------------------------===//

#include "MipsAsmPrinter.h"
#include "InstPrinter/MipsInstPrinter.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "MCTargetDesc/MipsMCNaCl.h"
#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "Mips.h"
#include "MipsMCInstLower.h"
#include "MipsMachineFunction.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "MipsTargetStreamer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "mips-asm-printer"

extern cl::opt<bool> EmitJalrReloc;

MipsTargetStreamer &MipsAsmPrinter::getTargetStreamer() const {
  return static_cast<MipsTargetStreamer &>(*OutStreamer->getTargetStreamer());
}

bool MipsAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<MipsSubtarget>();

  MipsFI = MF.getInfo<MipsFunctionInfo>();
  if (Subtarget->inMips16Mode())
    for (std::map<
             const char *,
             const Mips16HardFloatInfo::FuncSignature *>::const_iterator
             it = MipsFI->StubsNeeded.begin();
         it != MipsFI->StubsNeeded.end(); ++it) {
      const char *Symbol = it->first;
      const Mips16HardFloatInfo::FuncSignature *Signature = it->second;
      if (StubsNeeded.find(Symbol) == StubsNeeded.end())
        StubsNeeded[Symbol] = Signature;
    }
  MCP = MF.getConstantPool();

  // In NaCl, all indirect jump targets must be aligned to bundle size.
  if (Subtarget->isTargetNaCl())
    NaClAlignIndirectJumpTargets(MF);

  AsmPrinter::runOnMachineFunction(MF);

  emitXRayTable();

  return true;
}

bool MipsAsmPrinter::lowerOperand(const MachineOperand &MO, MCOperand &MCOp) {
  MCOp = MCInstLowering.LowerOperand(MO);
  return MCOp.isValid();
}

#include "MipsGenMCPseudoLowering.inc"

// Lower PseudoReturn/PseudoIndirectBranch/PseudoIndirectBranch64 to JR, JR_MM,
// JALR, or JALR64 as appropriate for the target.
void MipsAsmPrinter::emitPseudoIndirectBranch(MCStreamer &OutStreamer,
                                              const MachineInstr *MI) {
  bool HasLinkReg = false;
  bool InMicroMipsMode = Subtarget->inMicroMipsMode();
  MCInst TmpInst0;

  if (Subtarget->hasMips64r6()) {
    // MIPS64r6 should use (JALR64 ZERO_64, $rs)
    TmpInst0.setOpcode(Mips::JALR64);
    HasLinkReg = true;
  } else if (Subtarget->hasMips32r6()) {
    // MIPS32r6 should use (JALR ZERO, $rs)
    if (InMicroMipsMode)
      TmpInst0.setOpcode(Mips::JRC16_MMR6);
    else {
      TmpInst0.setOpcode(Mips::JALR);
      HasLinkReg = true;
    }
  } else if (Subtarget->inMicroMipsMode())
    // microMIPS should use (JR_MM $rs)
    TmpInst0.setOpcode(Mips::JR_MM);
  else {
    // Everything else should use (JR $rs)
    TmpInst0.setOpcode(Mips::JR);
  }

  MCOperand MCOp;

  if (HasLinkReg) {
    unsigned ZeroReg = Subtarget->isGP64bit() ? Mips::ZERO_64 : Mips::ZERO;
    TmpInst0.addOperand(MCOperand::createReg(ZeroReg));
  }

  lowerOperand(MI->getOperand(0), MCOp);
  TmpInst0.addOperand(MCOp);

  EmitToStreamer(OutStreamer, TmpInst0);
}

// If there is an MO_JALR operand, insert:
//
// .reloc tmplabel, R_{MICRO}MIPS_JALR, symbol
// tmplabel:
//
// This is an optimization hint for the linker which may then replace
// an indirect call with a direct branch.
static void emitDirectiveRelocJalr(const MachineInstr &MI,
                                   MCContext &OutContext,
                                   TargetMachine &TM,
                                   MCStreamer &OutStreamer,
                                   const MipsSubtarget &Subtarget) {
  for (unsigned int I = MI.getDesc().getNumOperands(), E = MI.getNumOperands();
       I < E; ++I) {
    MachineOperand MO = MI.getOperand(I);
    if (MO.isMCSymbol() && (MO.getTargetFlags() & MipsII::MO_JALR)) {
      MCSymbol *Callee = MO.getMCSymbol();
      if (Callee && !Callee->getName().empty()) {
        MCSymbol *OffsetLabel = OutContext.createTempSymbol();
        const MCExpr *OffsetExpr =
            MCSymbolRefExpr::create(OffsetLabel, OutContext);
        const MCExpr *CaleeExpr =
            MCSymbolRefExpr::create(Callee, OutContext);
        OutStreamer.EmitRelocDirective
            (*OffsetExpr,
             Subtarget.inMicroMipsMode() ? "R_MICROMIPS_JALR" : "R_MIPS_JALR",
             CaleeExpr, SMLoc(), *TM.getMCSubtargetInfo());
        OutStreamer.EmitLabel(OffsetLabel);
        return;
      }
    }
  }
}

void MipsAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  MipsTargetStreamer &TS = getTargetStreamer();
  unsigned Opc = MI->getOpcode();
  TS.forbidModuleDirective();

  if (MI->isDebugValue()) {
    SmallString<128> Str;
    raw_svector_ostream OS(Str);

    PrintDebugValueComment(MI, OS);
    return;
  }
  if (MI->isDebugLabel())
    return;

  // If we just ended a constant pool, mark it as such.
  if (InConstantPool && Opc != Mips::CONSTPOOL_ENTRY) {
    OutStreamer->EmitDataRegion(MCDR_DataRegionEnd);
    InConstantPool = false;
  }
  if (Opc == Mips::CONSTPOOL_ENTRY) {
    // CONSTPOOL_ENTRY - This instruction represents a floating
    // constant pool in the function.  The first operand is the ID#
    // for this instruction, the second is the index into the
    // MachineConstantPool that this is, the third is the size in
    // bytes of this constant pool entry.
    // The required alignment is specified on the basic block holding this MI.
    //
    unsigned LabelId = (unsigned)MI->getOperand(0).getImm();
    unsigned CPIdx = (unsigned)MI->getOperand(1).getIndex();

    // If this is the first entry of the pool, mark it.
    if (!InConstantPool) {
      OutStreamer->EmitDataRegion(MCDR_DataRegion);
      InConstantPool = true;
    }

    OutStreamer->EmitLabel(GetCPISymbol(LabelId));

    const MachineConstantPoolEntry &MCPE = MCP->getConstants()[CPIdx];
    if (MCPE.isMachineConstantPoolEntry())
      EmitMachineConstantPoolValue(MCPE.Val.MachineCPVal);
    else
      EmitGlobalConstant(MF->getDataLayout(), MCPE.Val.ConstVal);
    return;
  }

  switch (Opc) {
  case Mips::PATCHABLE_FUNCTION_ENTER:
    LowerPATCHABLE_FUNCTION_ENTER(*MI);
    return;
  case Mips::PATCHABLE_FUNCTION_EXIT:
    LowerPATCHABLE_FUNCTION_EXIT(*MI);
    return;
  case Mips::PATCHABLE_TAIL_CALL:
    LowerPATCHABLE_TAIL_CALL(*MI);
    return;
  }

  if (EmitJalrReloc &&
      (MI->isReturn() || MI->isCall() || MI->isIndirectBranch())) {
    emitDirectiveRelocJalr(*MI, OutContext, TM, *OutStreamer, *Subtarget);
  }

  MachineBasicBlock::const_instr_iterator I = MI->getIterator();
  MachineBasicBlock::const_instr_iterator E = MI->getParent()->instr_end();

  do {
    // Do any auto-generated pseudo lowerings.
    if (emitPseudoExpansionLowering(*OutStreamer, &*I))
      continue;

    if (I->getOpcode() == Mips::PseudoReturn ||
        I->getOpcode() == Mips::PseudoReturn64 ||
        I->getOpcode() == Mips::PseudoIndirectBranch ||
        I->getOpcode() == Mips::PseudoIndirectBranch64 ||
        I->getOpcode() == Mips::TAILCALLREG ||
        I->getOpcode() == Mips::TAILCALLREG64) {
      emitPseudoIndirectBranch(*OutStreamer, &*I);
      continue;
    }

    // The inMips16Mode() test is not permanent.
    // Some instructions are marked as pseudo right now which
    // would make the test fail for the wrong reason but
    // that will be fixed soon. We need this here because we are
    // removing another test for this situation downstream in the
    // callchain.
    //
    if (I->isPseudo() && !Subtarget->inMips16Mode()
        && !isLongBranchPseudo(I->getOpcode()))
      llvm_unreachable("Pseudo opcode found in EmitInstruction()");

    MCInst TmpInst0;
    MCInstLowering.Lower(&*I, TmpInst0);
    EmitToStreamer(*OutStreamer, TmpInst0);
  } while ((++I != E) && I->isInsideBundle()); // Delay slot check
}

//===----------------------------------------------------------------------===//
//
//  Mips Asm Directives
//
//  -- Frame directive "frame Stackpointer, Stacksize, RARegister"
//  Describe the stack frame.
//
//  -- Mask directives "(f)mask  bitmask, offset"
//  Tells the assembler which registers are saved and where.
//  bitmask - contain a little endian bitset indicating which registers are
//            saved on function prologue (e.g. with a 0x80000000 mask, the
//            assembler knows the register 31 (RA) is saved at prologue.
//  offset  - the position before stack pointer subtraction indicating where
//            the first saved register on prologue is located. (e.g. with a
//
//  Consider the following function prologue:
//
//    .frame  $fp,48,$ra
//    .mask   0xc0000000,-8
//       addiu $sp, $sp, -48
//       sw $ra, 40($sp)
//       sw $fp, 36($sp)
//
//    With a 0xc0000000 mask, the assembler knows the register 31 (RA) and
//    30 (FP) are saved at prologue. As the save order on prologue is from
//    left to right, RA is saved first. A -8 offset means that after the
//    stack pointer subtration, the first register in the mask (RA) will be
//    saved at address 48-8=40.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Mask directives
//===----------------------------------------------------------------------===//

// Create a bitmask with all callee saved registers for CPU or Floating Point
// registers. For CPU registers consider RA, GP and FP for saving if necessary.
void MipsAsmPrinter::printSavedRegsBitmask() {
  // CPU and FPU Saved Registers Bitmasks
  unsigned CPUBitmask = 0, FPUBitmask = 0;
  int CPUTopSavedRegOff, FPUTopSavedRegOff;

  // Set the CPU and FPU Bitmasks
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  // size of stack area to which FP callee-saved regs are saved.
  unsigned CPURegSize = TRI->getRegSizeInBits(Mips::GPR32RegClass) / 8;
  unsigned FGR32RegSize = TRI->getRegSizeInBits(Mips::FGR32RegClass) / 8;
  unsigned AFGR64RegSize = TRI->getRegSizeInBits(Mips::AFGR64RegClass) / 8;
  bool HasAFGR64Reg = false;
  unsigned CSFPRegsSize = 0;

  for (const auto &I : CSI) {
    unsigned Reg = I.getReg();
    unsigned RegNum = TRI->getEncodingValue(Reg);

    // If it's a floating point register, set the FPU Bitmask.
    // If it's a general purpose register, set the CPU Bitmask.
    if (Mips::FGR32RegClass.contains(Reg)) {
      FPUBitmask |= (1 << RegNum);
      CSFPRegsSize += FGR32RegSize;
    } else if (Mips::AFGR64RegClass.contains(Reg)) {
      FPUBitmask |= (3 << RegNum);
      CSFPRegsSize += AFGR64RegSize;
      HasAFGR64Reg = true;
    } else if (Mips::GPR32RegClass.contains(Reg))
      CPUBitmask |= (1 << RegNum);
  }

  // FP Regs are saved right below where the virtual frame pointer points to.
  FPUTopSavedRegOff = FPUBitmask ?
    (HasAFGR64Reg ? -AFGR64RegSize : -FGR32RegSize) : 0;

  // CPU Regs are saved below FP Regs.
  CPUTopSavedRegOff = CPUBitmask ? -CSFPRegsSize - CPURegSize : 0;

  MipsTargetStreamer &TS = getTargetStreamer();
  // Print CPUBitmask
  TS.emitMask(CPUBitmask, CPUTopSavedRegOff);

  // Print FPUBitmask
  TS.emitFMask(FPUBitmask, FPUTopSavedRegOff);
}

//===----------------------------------------------------------------------===//
// Frame and Set directives
//===----------------------------------------------------------------------===//

/// Frame Directive
void MipsAsmPrinter::emitFrameDirective() {
  const TargetRegisterInfo &RI = *MF->getSubtarget().getRegisterInfo();

  unsigned stackReg  = RI.getFrameRegister(*MF);
  unsigned returnReg = RI.getRARegister();
  unsigned stackSize = MF->getFrameInfo().getStackSize();

  getTargetStreamer().emitFrame(stackReg, stackSize, returnReg);
}

/// Emit Set directives.
const char *MipsAsmPrinter::getCurrentABIString() const {
  switch (static_cast<MipsTargetMachine &>(TM).getABI().GetEnumValue()) {
  case MipsABIInfo::ABI::O32:  return "abi32";
  case MipsABIInfo::ABI::N32:  return "abiN32";
  case MipsABIInfo::ABI::N64:  return "abi64";
  default: llvm_unreachable("Unknown Mips ABI");
  }
}

void MipsAsmPrinter::EmitFunctionEntryLabel() {
  MipsTargetStreamer &TS = getTargetStreamer();

  // NaCl sandboxing requires that indirect call instructions are masked.
  // This means that function entry points should be bundle-aligned.
  if (Subtarget->isTargetNaCl())
    EmitAlignment(std::max(MF->getAlignment(), MIPS_NACL_BUNDLE_ALIGN));

  if (Subtarget->inMicroMipsMode()) {
    TS.emitDirectiveSetMicroMips();
    TS.setUsesMicroMips();
    TS.updateABIInfo(*Subtarget);
  } else
    TS.emitDirectiveSetNoMicroMips();

  if (Subtarget->inMips16Mode())
    TS.emitDirectiveSetMips16();
  else
    TS.emitDirectiveSetNoMips16();

  TS.emitDirectiveEnt(*CurrentFnSym);
  OutStreamer->EmitLabel(CurrentFnSym);
}

/// EmitFunctionBodyStart - Targets can override this to emit stuff before
/// the first basic block in the function.
void MipsAsmPrinter::EmitFunctionBodyStart() {
  MipsTargetStreamer &TS = getTargetStreamer();

  MCInstLowering.Initialize(&MF->getContext());

  bool IsNakedFunction = MF->getFunction().hasFnAttribute(Attribute::Naked);
  if (!IsNakedFunction)
    emitFrameDirective();

  if (!IsNakedFunction)
    printSavedRegsBitmask();

  if (!Subtarget->inMips16Mode()) {
    TS.emitDirectiveSetNoReorder();
    TS.emitDirectiveSetNoMacro();
    TS.emitDirectiveSetNoAt();
  }
}

/// EmitFunctionBodyEnd - Targets can override this to emit stuff after
/// the last basic block in the function.
void MipsAsmPrinter::EmitFunctionBodyEnd() {
  MipsTargetStreamer &TS = getTargetStreamer();

  // There are instruction for this macros, but they must
  // always be at the function end, and we can't emit and
  // break with BB logic.
  if (!Subtarget->inMips16Mode()) {
    TS.emitDirectiveSetAt();
    TS.emitDirectiveSetMacro();
    TS.emitDirectiveSetReorder();
  }
  TS.emitDirectiveEnd(CurrentFnSym->getName());
  // Make sure to terminate any constant pools that were at the end
  // of the function.
  if (!InConstantPool)
    return;
  InConstantPool = false;
  OutStreamer->EmitDataRegion(MCDR_DataRegionEnd);
}

void MipsAsmPrinter::EmitBasicBlockEnd(const MachineBasicBlock &MBB) {
  AsmPrinter::EmitBasicBlockEnd(MBB);
  MipsTargetStreamer &TS = getTargetStreamer();
  if (MBB.empty())
    TS.emitDirectiveInsn();
}

/// isBlockOnlyReachableByFallthough - Return true if the basic block has
/// exactly one predecessor and the control transfer mechanism between
/// the predecessor and this block is a fall-through.
bool MipsAsmPrinter::isBlockOnlyReachableByFallthrough(const MachineBasicBlock*
                                                       MBB) const {
  // The predecessor has to be immediately before this block.
  const MachineBasicBlock *Pred = *MBB->pred_begin();

  // If the predecessor is a switch statement, assume a jump table
  // implementation, so it is not a fall through.
  if (const BasicBlock *bb = Pred->getBasicBlock())
    if (isa<SwitchInst>(bb->getTerminator()))
      return false;

  // If this is a landing pad, it isn't a fall through.  If it has no preds,
  // then nothing falls through to it.
  if (MBB->isEHPad() || MBB->pred_empty())
    return false;

  // If there isn't exactly one predecessor, it can't be a fall through.
  MachineBasicBlock::const_pred_iterator PI = MBB->pred_begin(), PI2 = PI;
  ++PI2;

  if (PI2 != MBB->pred_end())
    return false;

  // The predecessor has to be immediately before this block.
  if (!Pred->isLayoutSuccessor(MBB))
    return false;

  // If the block is completely empty, then it definitely does fall through.
  if (Pred->empty())
    return true;

  // Otherwise, check the last instruction.
  // Check if the last terminator is an unconditional branch.
  MachineBasicBlock::const_iterator I = Pred->end();
  while (I != Pred->begin() && !(--I)->isTerminator()) ;

  return !I->isBarrier();
}

// Print out an operand for an inline asm expression.
bool MipsAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                                     unsigned AsmVariant, const char *ExtraCode,
                                     raw_ostream &O) {
  // Does this asm operand have a single letter operand modifier?
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0) return true; // Unknown modifier.

    const MachineOperand &MO = MI->getOperand(OpNum);
    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI,OpNum,AsmVariant,ExtraCode,O);
    case 'X': // hex const int
      if ((MO.getType()) != MachineOperand::MO_Immediate)
        return true;
      O << "0x" << Twine::utohexstr(MO.getImm());
      return false;
    case 'x': // hex const int (low 16 bits)
      if ((MO.getType()) != MachineOperand::MO_Immediate)
        return true;
      O << "0x" << Twine::utohexstr(MO.getImm() & 0xffff);
      return false;
    case 'd': // decimal const int
      if ((MO.getType()) != MachineOperand::MO_Immediate)
        return true;
      O << MO.getImm();
      return false;
    case 'm': // decimal const int minus 1
      if ((MO.getType()) != MachineOperand::MO_Immediate)
        return true;
      O << MO.getImm() - 1;
      return false;
    case 'y': // exact log2
      if ((MO.getType()) != MachineOperand::MO_Immediate)
        return true;
      if (!isPowerOf2_64(MO.getImm()))
        return true;
      O << Log2_64(MO.getImm());
      return false;
    case 'z':
      // $0 if zero, regular printing otherwise
      if (MO.getType() == MachineOperand::MO_Immediate && MO.getImm() == 0) {
        O << "$0";
        return false;
      }
      // If not, call printOperand as normal.
      break;
    case 'D': // Second part of a double word register operand
    case 'L': // Low order register of a double word register operand
    case 'M': // High order register of a double word register operand
    {
      if (OpNum == 0)
        return true;
      const MachineOperand &FlagsOP = MI->getOperand(OpNum - 1);
      if (!FlagsOP.isImm())
        return true;
      unsigned Flags = FlagsOP.getImm();
      unsigned NumVals = InlineAsm::getNumOperandRegisters(Flags);
      // Number of registers represented by this operand. We are looking
      // for 2 for 32 bit mode and 1 for 64 bit mode.
      if (NumVals != 2) {
        if (Subtarget->isGP64bit() && NumVals == 1 && MO.isReg()) {
          unsigned Reg = MO.getReg();
          O << '$' << MipsInstPrinter::getRegisterName(Reg);
          return false;
        }
        return true;
      }

      unsigned RegOp = OpNum;
      if (!Subtarget->isGP64bit()){
        // Endianness reverses which register holds the high or low value
        // between M and L.
        switch(ExtraCode[0]) {
        case 'M':
          RegOp = (Subtarget->isLittle()) ? OpNum + 1 : OpNum;
          break;
        case 'L':
          RegOp = (Subtarget->isLittle()) ? OpNum : OpNum + 1;
          break;
        case 'D': // Always the second part
          RegOp = OpNum + 1;
        }
        if (RegOp >= MI->getNumOperands())
          return true;
        const MachineOperand &MO = MI->getOperand(RegOp);
        if (!MO.isReg())
          return true;
        unsigned Reg = MO.getReg();
        O << '$' << MipsInstPrinter::getRegisterName(Reg);
        return false;
      }
      break;
    }
    case 'w':
      // Print MSA registers for the 'f' constraint
      // In LLVM, the 'w' modifier doesn't need to do anything.
      // We can just call printOperand as normal.
      break;
    }
  }

  printOperand(MI, OpNum, O);
  return false;
}

bool MipsAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                           unsigned OpNum, unsigned AsmVariant,
                                           const char *ExtraCode,
                                           raw_ostream &O) {
  assert(OpNum + 1 < MI->getNumOperands() && "Insufficient operands");
  const MachineOperand &BaseMO = MI->getOperand(OpNum);
  const MachineOperand &OffsetMO = MI->getOperand(OpNum + 1);
  assert(BaseMO.isReg() && "Unexpected base pointer for inline asm memory operand.");
  assert(OffsetMO.isImm() && "Unexpected offset for inline asm memory operand.");
  int Offset = OffsetMO.getImm();

  // Currently we are expecting either no ExtraCode or 'D','M','L'.
  if (ExtraCode) {
    switch (ExtraCode[0]) {
    case 'D':
      Offset += 4;
      break;
    case 'M':
      if (Subtarget->isLittle())
        Offset += 4;
      break;
    case 'L':
      if (!Subtarget->isLittle())
        Offset += 4;
      break;
    default:
      return true; // Unknown modifier.
    }
  }

  O << Offset << "($" << MipsInstPrinter::getRegisterName(BaseMO.getReg())
    << ")";

  return false;
}

void MipsAsmPrinter::printOperand(const MachineInstr *MI, int opNum,
                                  raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(opNum);
  bool closeP = false;

  if (MO.getTargetFlags())
    closeP = true;

  switch(MO.getTargetFlags()) {
  case MipsII::MO_GPREL:    O << "%gp_rel("; break;
  case MipsII::MO_GOT_CALL: O << "%call16("; break;
  case MipsII::MO_GOT:      O << "%got(";    break;
  case MipsII::MO_ABS_HI:   O << "%hi(";     break;
  case MipsII::MO_ABS_LO:   O << "%lo(";     break;
  case MipsII::MO_HIGHER:   O << "%higher("; break;
  case MipsII::MO_HIGHEST:  O << "%highest(("; break;
  case MipsII::MO_TLSGD:    O << "%tlsgd(";  break;
  case MipsII::MO_GOTTPREL: O << "%gottprel("; break;
  case MipsII::MO_TPREL_HI: O << "%tprel_hi("; break;
  case MipsII::MO_TPREL_LO: O << "%tprel_lo("; break;
  case MipsII::MO_GPOFF_HI: O << "%hi(%neg(%gp_rel("; break;
  case MipsII::MO_GPOFF_LO: O << "%lo(%neg(%gp_rel("; break;
  case MipsII::MO_GOT_DISP: O << "%got_disp("; break;
  case MipsII::MO_GOT_PAGE: O << "%got_page("; break;
  case MipsII::MO_GOT_OFST: O << "%got_ofst("; break;
  }

  switch (MO.getType()) {
    case MachineOperand::MO_Register:
      O << '$'
        << StringRef(MipsInstPrinter::getRegisterName(MO.getReg())).lower();
      break;

    case MachineOperand::MO_Immediate:
      O << MO.getImm();
      break;

    case MachineOperand::MO_MachineBasicBlock:
      MO.getMBB()->getSymbol()->print(O, MAI);
      return;

    case MachineOperand::MO_GlobalAddress:
      getSymbol(MO.getGlobal())->print(O, MAI);
      break;

    case MachineOperand::MO_BlockAddress: {
      MCSymbol *BA = GetBlockAddressSymbol(MO.getBlockAddress());
      O << BA->getName();
      break;
    }

    case MachineOperand::MO_ConstantPoolIndex:
      O << getDataLayout().getPrivateGlobalPrefix() << "CPI"
        << getFunctionNumber() << "_" << MO.getIndex();
      if (MO.getOffset())
        O << "+" << MO.getOffset();
      break;

    default:
      llvm_unreachable("<unknown operand type>");
  }

  if (closeP) O << ")";
}

void MipsAsmPrinter::
printMemOperand(const MachineInstr *MI, int opNum, raw_ostream &O) {
  // Load/Store memory operands -- imm($reg)
  // If PIC target the target is loaded as the
  // pattern lw $25,%call16($28)

  // opNum can be invalid if instruction has reglist as operand.
  // MemOperand is always last operand of instruction (base + offset).
  switch (MI->getOpcode()) {
  default:
    break;
  case Mips::SWM32_MM:
  case Mips::LWM32_MM:
    opNum = MI->getNumOperands() - 2;
    break;
  }

  printOperand(MI, opNum+1, O);
  O << "(";
  printOperand(MI, opNum, O);
  O << ")";
}

void MipsAsmPrinter::
printMemOperandEA(const MachineInstr *MI, int opNum, raw_ostream &O) {
  // when using stack locations for not load/store instructions
  // print the same way as all normal 3 operand instructions.
  printOperand(MI, opNum, O);
  O << ", ";
  printOperand(MI, opNum+1, O);
}

void MipsAsmPrinter::
printFCCOperand(const MachineInstr *MI, int opNum, raw_ostream &O,
                const char *Modifier) {
  const MachineOperand &MO = MI->getOperand(opNum);
  O << Mips::MipsFCCToString((Mips::CondCode)MO.getImm());
}

void MipsAsmPrinter::
printRegisterList(const MachineInstr *MI, int opNum, raw_ostream &O) {
  for (int i = opNum, e = MI->getNumOperands(); i != e; ++i) {
    if (i != opNum) O << ", ";
    printOperand(MI, i, O);
  }
}

void MipsAsmPrinter::EmitStartOfAsmFile(Module &M) {
  MipsTargetStreamer &TS = getTargetStreamer();

  // MipsTargetStreamer has an initialization order problem when emitting an
  // object file directly (see MipsTargetELFStreamer for full details). Work
  // around it by re-initializing the PIC state here.
  TS.setPic(OutContext.getObjectFileInfo()->isPositionIndependent());

  // Compute MIPS architecture attributes based on the default subtarget
  // that we'd have constructed. Module level directives aren't LTO
  // clean anyhow.
  // FIXME: For ifunc related functions we could iterate over and look
  // for a feature string that doesn't match the default one.
  const Triple &TT = TM.getTargetTriple();
  StringRef CPU = MIPS_MC::selectMipsCPU(TT, TM.getTargetCPU());
  StringRef FS = TM.getTargetFeatureString();
  const MipsTargetMachine &MTM = static_cast<const MipsTargetMachine &>(TM);
  const MipsSubtarget STI(TT, CPU, FS, MTM.isLittleEndian(), MTM, 0);

  bool IsABICalls = STI.isABICalls();
  const MipsABIInfo &ABI = MTM.getABI();
  if (IsABICalls) {
    TS.emitDirectiveAbiCalls();
    // FIXME: This condition should be a lot more complicated that it is here.
    //        Ideally it should test for properties of the ABI and not the ABI
    //        itself.
    //        For the moment, I'm only correcting enough to make MIPS-IV work.
    if (!isPositionIndependent() && STI.hasSym32())
      TS.emitDirectiveOptionPic0();
  }

  // Tell the assembler which ABI we are using
  std::string SectionName = std::string(".mdebug.") + getCurrentABIString();
  OutStreamer->SwitchSection(
      OutContext.getELFSection(SectionName, ELF::SHT_PROGBITS, 0));

  // NaN: At the moment we only support:
  // 1. .nan legacy (default)
  // 2. .nan 2008
  STI.isNaN2008() ? TS.emitDirectiveNaN2008()
                  : TS.emitDirectiveNaNLegacy();

  // TODO: handle O64 ABI

  TS.updateABIInfo(STI);

  // We should always emit a '.module fp=...' but binutils 2.24 does not accept
  // it. We therefore emit it when it contradicts the ABI defaults (-mfpxx or
  // -mfp64) and omit it otherwise.
  if (ABI.IsO32() && (STI.isABI_FPXX() || STI.isFP64bit()))
    TS.emitDirectiveModuleFP();

  // We should always emit a '.module [no]oddspreg' but binutils 2.24 does not
  // accept it. We therefore emit it when it contradicts the default or an
  // option has changed the default (i.e. FPXX) and omit it otherwise.
  if (ABI.IsO32() && (!STI.useOddSPReg() || STI.isABI_FPXX()))
    TS.emitDirectiveModuleOddSPReg();
}

void MipsAsmPrinter::emitInlineAsmStart() const {
  MipsTargetStreamer &TS = getTargetStreamer();

  // GCC's choice of assembler options for inline assembly code ('at', 'macro'
  // and 'reorder') is different from LLVM's choice for generated code ('noat',
  // 'nomacro' and 'noreorder').
  // In order to maintain compatibility with inline assembly code which depends
  // on GCC's assembler options being used, we have to switch to those options
  // for the duration of the inline assembly block and then switch back.
  TS.emitDirectiveSetPush();
  TS.emitDirectiveSetAt();
  TS.emitDirectiveSetMacro();
  TS.emitDirectiveSetReorder();
  OutStreamer->AddBlankLine();
}

void MipsAsmPrinter::emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                                      const MCSubtargetInfo *EndInfo) const {
  OutStreamer->AddBlankLine();
  getTargetStreamer().emitDirectiveSetPop();
}

void MipsAsmPrinter::EmitJal(const MCSubtargetInfo &STI, MCSymbol *Symbol) {
  MCInst I;
  I.setOpcode(Mips::JAL);
  I.addOperand(
      MCOperand::createExpr(MCSymbolRefExpr::create(Symbol, OutContext)));
  OutStreamer->EmitInstruction(I, STI);
}

void MipsAsmPrinter::EmitInstrReg(const MCSubtargetInfo &STI, unsigned Opcode,
                                  unsigned Reg) {
  MCInst I;
  I.setOpcode(Opcode);
  I.addOperand(MCOperand::createReg(Reg));
  OutStreamer->EmitInstruction(I, STI);
}

void MipsAsmPrinter::EmitInstrRegReg(const MCSubtargetInfo &STI,
                                     unsigned Opcode, unsigned Reg1,
                                     unsigned Reg2) {
  MCInst I;
  //
  // Because of the current td files for Mips32, the operands for MTC1
  // appear backwards from their normal assembly order. It's not a trivial
  // change to fix this in the td file so we adjust for it here.
  //
  if (Opcode == Mips::MTC1) {
    unsigned Temp = Reg1;
    Reg1 = Reg2;
    Reg2 = Temp;
  }
  I.setOpcode(Opcode);
  I.addOperand(MCOperand::createReg(Reg1));
  I.addOperand(MCOperand::createReg(Reg2));
  OutStreamer->EmitInstruction(I, STI);
}

void MipsAsmPrinter::EmitInstrRegRegReg(const MCSubtargetInfo &STI,
                                        unsigned Opcode, unsigned Reg1,
                                        unsigned Reg2, unsigned Reg3) {
  MCInst I;
  I.setOpcode(Opcode);
  I.addOperand(MCOperand::createReg(Reg1));
  I.addOperand(MCOperand::createReg(Reg2));
  I.addOperand(MCOperand::createReg(Reg3));
  OutStreamer->EmitInstruction(I, STI);
}

void MipsAsmPrinter::EmitMovFPIntPair(const MCSubtargetInfo &STI,
                                      unsigned MovOpc, unsigned Reg1,
                                      unsigned Reg2, unsigned FPReg1,
                                      unsigned FPReg2, bool LE) {
  if (!LE) {
    unsigned temp = Reg1;
    Reg1 = Reg2;
    Reg2 = temp;
  }
  EmitInstrRegReg(STI, MovOpc, Reg1, FPReg1);
  EmitInstrRegReg(STI, MovOpc, Reg2, FPReg2);
}

void MipsAsmPrinter::EmitSwapFPIntParams(const MCSubtargetInfo &STI,
                                         Mips16HardFloatInfo::FPParamVariant PV,
                                         bool LE, bool ToFP) {
  using namespace Mips16HardFloatInfo;

  unsigned MovOpc = ToFP ? Mips::MTC1 : Mips::MFC1;
  switch (PV) {
  case FSig:
    EmitInstrRegReg(STI, MovOpc, Mips::A0, Mips::F12);
    break;
  case FFSig:
    EmitMovFPIntPair(STI, MovOpc, Mips::A0, Mips::A1, Mips::F12, Mips::F14, LE);
    break;
  case FDSig:
    EmitInstrRegReg(STI, MovOpc, Mips::A0, Mips::F12);
    EmitMovFPIntPair(STI, MovOpc, Mips::A2, Mips::A3, Mips::F14, Mips::F15, LE);
    break;
  case DSig:
    EmitMovFPIntPair(STI, MovOpc, Mips::A0, Mips::A1, Mips::F12, Mips::F13, LE);
    break;
  case DDSig:
    EmitMovFPIntPair(STI, MovOpc, Mips::A0, Mips::A1, Mips::F12, Mips::F13, LE);
    EmitMovFPIntPair(STI, MovOpc, Mips::A2, Mips::A3, Mips::F14, Mips::F15, LE);
    break;
  case DFSig:
    EmitMovFPIntPair(STI, MovOpc, Mips::A0, Mips::A1, Mips::F12, Mips::F13, LE);
    EmitInstrRegReg(STI, MovOpc, Mips::A2, Mips::F14);
    break;
  case NoSig:
    return;
  }
}

void MipsAsmPrinter::EmitSwapFPIntRetval(
    const MCSubtargetInfo &STI, Mips16HardFloatInfo::FPReturnVariant RV,
    bool LE) {
  using namespace Mips16HardFloatInfo;

  unsigned MovOpc = Mips::MFC1;
  switch (RV) {
  case FRet:
    EmitInstrRegReg(STI, MovOpc, Mips::V0, Mips::F0);
    break;
  case DRet:
    EmitMovFPIntPair(STI, MovOpc, Mips::V0, Mips::V1, Mips::F0, Mips::F1, LE);
    break;
  case CFRet:
    EmitMovFPIntPair(STI, MovOpc, Mips::V0, Mips::V1, Mips::F0, Mips::F1, LE);
    break;
  case CDRet:
    EmitMovFPIntPair(STI, MovOpc, Mips::V0, Mips::V1, Mips::F0, Mips::F1, LE);
    EmitMovFPIntPair(STI, MovOpc, Mips::A0, Mips::A1, Mips::F2, Mips::F3, LE);
    break;
  case NoFPRet:
    break;
  }
}

void MipsAsmPrinter::EmitFPCallStub(
    const char *Symbol, const Mips16HardFloatInfo::FuncSignature *Signature) {
  using namespace Mips16HardFloatInfo;

  MCSymbol *MSymbol = OutContext.getOrCreateSymbol(StringRef(Symbol));
  bool LE = getDataLayout().isLittleEndian();
  // Construct a local MCSubtargetInfo here.
  // This is because the MachineFunction won't exist (but have not yet been
  // freed) and since we're at the global level we can use the default
  // constructed subtarget.
  std::unique_ptr<MCSubtargetInfo> STI(TM.getTarget().createMCSubtargetInfo(
      TM.getTargetTriple().str(), TM.getTargetCPU(),
      TM.getTargetFeatureString()));

  //
  // .global xxxx
  //
  OutStreamer->EmitSymbolAttribute(MSymbol, MCSA_Global);
  const char *RetType;
  //
  // make the comment field identifying the return and parameter
  // types of the floating point stub
  // # Stub function to call rettype xxxx (params)
  //
  switch (Signature->RetSig) {
  case FRet:
    RetType = "float";
    break;
  case DRet:
    RetType = "double";
    break;
  case CFRet:
    RetType = "complex";
    break;
  case CDRet:
    RetType = "double complex";
    break;
  case NoFPRet:
    RetType = "";
    break;
  }
  const char *Parms;
  switch (Signature->ParamSig) {
  case FSig:
    Parms = "float";
    break;
  case FFSig:
    Parms = "float, float";
    break;
  case FDSig:
    Parms = "float, double";
    break;
  case DSig:
    Parms = "double";
    break;
  case DDSig:
    Parms = "double, double";
    break;
  case DFSig:
    Parms = "double, float";
    break;
  case NoSig:
    Parms = "";
    break;
  }
  OutStreamer->AddComment("\t# Stub function to call " + Twine(RetType) + " " +
                          Twine(Symbol) + " (" + Twine(Parms) + ")");
  //
  // probably not necessary but we save and restore the current section state
  //
  OutStreamer->PushSection();
  //
  // .section mips16.call.fpxxxx,"ax",@progbits
  //
  MCSectionELF *M = OutContext.getELFSection(
      ".mips16.call.fp." + std::string(Symbol), ELF::SHT_PROGBITS,
      ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
  OutStreamer->SwitchSection(M, nullptr);
  //
  // .align 2
  //
  OutStreamer->EmitValueToAlignment(4);
  MipsTargetStreamer &TS = getTargetStreamer();
  //
  // .set nomips16
  // .set nomicromips
  //
  TS.emitDirectiveSetNoMips16();
  TS.emitDirectiveSetNoMicroMips();
  //
  // .ent __call_stub_fp_xxxx
  // .type  __call_stub_fp_xxxx,@function
  //  __call_stub_fp_xxxx:
  //
  std::string x = "__call_stub_fp_" + std::string(Symbol);
  MCSymbolELF *Stub =
      cast<MCSymbolELF>(OutContext.getOrCreateSymbol(StringRef(x)));
  TS.emitDirectiveEnt(*Stub);
  MCSymbol *MType =
      OutContext.getOrCreateSymbol("__call_stub_fp_" + Twine(Symbol));
  OutStreamer->EmitSymbolAttribute(MType, MCSA_ELF_TypeFunction);
  OutStreamer->EmitLabel(Stub);

  // Only handle non-pic for now.
  assert(!isPositionIndependent() &&
         "should not be here if we are compiling pic");
  TS.emitDirectiveSetReorder();
  //
  // We need to add a MipsMCExpr class to MCTargetDesc to fully implement
  // stubs without raw text but this current patch is for compiler generated
  // functions and they all return some value.
  // The calling sequence for non pic is different in that case and we need
  // to implement %lo and %hi in order to handle the case of no return value
  // See the corresponding method in Mips16HardFloat for details.
  //
  // mov the return address to S2.
  // we have no stack space to store it and we are about to make another call.
  // We need to make sure that the enclosing function knows to save S2
  // This should have already been handled.
  //
  // Mov $18, $31

  EmitInstrRegRegReg(*STI, Mips::OR, Mips::S2, Mips::RA, Mips::ZERO);

  EmitSwapFPIntParams(*STI, Signature->ParamSig, LE, true);

  // Jal xxxx
  //
  EmitJal(*STI, MSymbol);

  // fix return values
  EmitSwapFPIntRetval(*STI, Signature->RetSig, LE);
  //
  // do the return
  // if (Signature->RetSig == NoFPRet)
  //  llvm_unreachable("should not be any stubs here with no return value");
  // else
  EmitInstrReg(*STI, Mips::JR, Mips::S2);

  MCSymbol *Tmp = OutContext.createTempSymbol();
  OutStreamer->EmitLabel(Tmp);
  const MCSymbolRefExpr *E = MCSymbolRefExpr::create(Stub, OutContext);
  const MCSymbolRefExpr *T = MCSymbolRefExpr::create(Tmp, OutContext);
  const MCExpr *T_min_E = MCBinaryExpr::createSub(T, E, OutContext);
  OutStreamer->emitELFSize(Stub, T_min_E);
  TS.emitDirectiveEnd(x);
  OutStreamer->PopSection();
}

void MipsAsmPrinter::EmitEndOfAsmFile(Module &M) {
  // Emit needed stubs
  //
  for (std::map<
           const char *,
           const Mips16HardFloatInfo::FuncSignature *>::const_iterator
           it = StubsNeeded.begin();
       it != StubsNeeded.end(); ++it) {
    const char *Symbol = it->first;
    const Mips16HardFloatInfo::FuncSignature *Signature = it->second;
    EmitFPCallStub(Symbol, Signature);
  }
  // return to the text section
  OutStreamer->SwitchSection(OutContext.getObjectFileInfo()->getTextSection());
}

void MipsAsmPrinter::EmitSled(const MachineInstr &MI, SledKind Kind) {
  const uint8_t NoopsInSledCount = Subtarget->isGP64bit() ? 15 : 11;
  // For mips32 we want to emit the following pattern:
  //
  // .Lxray_sled_N:
  //   ALIGN
  //   B .tmpN
  //   11 NOP instructions (44 bytes)
  //   ADDIU T9, T9, 52
  // .tmpN
  //
  // We need the 44 bytes (11 instructions) because at runtime, we'd
  // be patching over the full 48 bytes (12 instructions) with the following
  // pattern:
  //
  //   ADDIU    SP, SP, -8
  //   NOP
  //   SW       RA, 4(SP)
  //   SW       T9, 0(SP)
  //   LUI      T9, %hi(__xray_FunctionEntry/Exit)
  //   ORI      T9, T9, %lo(__xray_FunctionEntry/Exit)
  //   LUI      T0, %hi(function_id)
  //   JALR     T9
  //   ORI      T0, T0, %lo(function_id)
  //   LW       T9, 0(SP)
  //   LW       RA, 4(SP)
  //   ADDIU    SP, SP, 8
  //
  // We add 52 bytes to t9 because we want to adjust the function pointer to
  // the actual start of function i.e. the address just after the noop sled.
  // We do this because gp displacement relocation is emitted at the start of
  // of the function i.e after the nop sled and to correctly calculate the
  // global offset table address, t9 must hold the address of the instruction
  // containing the gp displacement relocation.
  // FIXME: Is this correct for the static relocation model?
  //
  // For mips64 we want to emit the following pattern:
  //
  // .Lxray_sled_N:
  //   ALIGN
  //   B .tmpN
  //   15 NOP instructions (60 bytes)
  // .tmpN
  //
  // We need the 60 bytes (15 instructions) because at runtime, we'd
  // be patching over the full 64 bytes (16 instructions) with the following
  // pattern:
  //
  //   DADDIU   SP, SP, -16
  //   NOP
  //   SD       RA, 8(SP)
  //   SD       T9, 0(SP)
  //   LUI      T9, %highest(__xray_FunctionEntry/Exit)
  //   ORI      T9, T9, %higher(__xray_FunctionEntry/Exit)
  //   DSLL     T9, T9, 16
  //   ORI      T9, T9, %hi(__xray_FunctionEntry/Exit)
  //   DSLL     T9, T9, 16
  //   ORI      T9, T9, %lo(__xray_FunctionEntry/Exit)
  //   LUI      T0, %hi(function_id)
  //   JALR     T9
  //   ADDIU    T0, T0, %lo(function_id)
  //   LD       T9, 0(SP)
  //   LD       RA, 8(SP)
  //   DADDIU   SP, SP, 16
  //
  OutStreamer->EmitCodeAlignment(4);
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->EmitLabel(CurSled);
  auto Target = OutContext.createTempSymbol();

  // Emit "B .tmpN" instruction, which jumps over the nop sled to the actual
  // start of function
  const MCExpr *TargetExpr = MCSymbolRefExpr::create(
      Target, MCSymbolRefExpr::VariantKind::VK_None, OutContext);
  EmitToStreamer(*OutStreamer, MCInstBuilder(Mips::BEQ)
                                   .addReg(Mips::ZERO)
                                   .addReg(Mips::ZERO)
                                   .addExpr(TargetExpr));

  for (int8_t I = 0; I < NoopsInSledCount; I++)
    EmitToStreamer(*OutStreamer, MCInstBuilder(Mips::SLL)
                                     .addReg(Mips::ZERO)
                                     .addReg(Mips::ZERO)
                                     .addImm(0));

  OutStreamer->EmitLabel(Target);

  if (!Subtarget->isGP64bit()) {
    EmitToStreamer(*OutStreamer,
                   MCInstBuilder(Mips::ADDiu)
                       .addReg(Mips::T9)
                       .addReg(Mips::T9)
                       .addImm(0x34));
  }

  recordSled(CurSled, MI, Kind);
}

void MipsAsmPrinter::LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI) {
  EmitSled(MI, SledKind::FUNCTION_ENTER);
}

void MipsAsmPrinter::LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI) {
  EmitSled(MI, SledKind::FUNCTION_EXIT);
}

void MipsAsmPrinter::LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI) {
  EmitSled(MI, SledKind::TAIL_CALL);
}

void MipsAsmPrinter::PrintDebugValueComment(const MachineInstr *MI,
                                           raw_ostream &OS) {
  // TODO: implement
}

// Emit .dtprelword or .dtpreldword directive
// and value for debug thread local expression.
void MipsAsmPrinter::EmitDebugValue(const MCExpr *Value, unsigned Size) const {
  if (auto *MipsExpr = dyn_cast<MipsMCExpr>(Value)) {
    if (MipsExpr && MipsExpr->getKind() == MipsMCExpr::MEK_DTPREL) {
      switch (Size) {
      case 4:
        OutStreamer->EmitDTPRel32Value(MipsExpr->getSubExpr());
        break;
      case 8:
        OutStreamer->EmitDTPRel64Value(MipsExpr->getSubExpr());
        break;
      default:
        llvm_unreachable("Unexpected size of expression value.");
      }
      return;
    }
  }
  AsmPrinter::EmitDebugValue(Value, Size);
}

// Align all targets of indirect branches on bundle size.  Used only if target
// is NaCl.
void MipsAsmPrinter::NaClAlignIndirectJumpTargets(MachineFunction &MF) {
  // Align all blocks that are jumped to through jump table.
  if (MachineJumpTableInfo *JtInfo = MF.getJumpTableInfo()) {
    const std::vector<MachineJumpTableEntry> &JT = JtInfo->getJumpTables();
    for (unsigned I = 0; I < JT.size(); ++I) {
      const std::vector<MachineBasicBlock*> &MBBs = JT[I].MBBs;

      for (unsigned J = 0; J < MBBs.size(); ++J)
        MBBs[J]->setAlignment(MIPS_NACL_BUNDLE_ALIGN);
    }
  }

  // If basic block address is taken, block can be target of indirect branch.
  for (auto &MBB : MF) {
    if (MBB.hasAddressTaken())
      MBB.setAlignment(MIPS_NACL_BUNDLE_ALIGN);
  }
}

bool MipsAsmPrinter::isLongBranchPseudo(int Opcode) const {
  return (Opcode == Mips::LONG_BRANCH_LUi
          || Opcode == Mips::LONG_BRANCH_LUi2Op
          || Opcode == Mips::LONG_BRANCH_LUi2Op_64
          || Opcode == Mips::LONG_BRANCH_ADDiu
          || Opcode == Mips::LONG_BRANCH_ADDiu2Op
          || Opcode == Mips::LONG_BRANCH_DADDiu
          || Opcode == Mips::LONG_BRANCH_DADDiu2Op);
}

// Force static initialization.
extern "C" void LLVMInitializeMipsAsmPrinter() {
  RegisterAsmPrinter<MipsAsmPrinter> X(getTheMipsTarget());
  RegisterAsmPrinter<MipsAsmPrinter> Y(getTheMipselTarget());
  RegisterAsmPrinter<MipsAsmPrinter> A(getTheMips64Target());
  RegisterAsmPrinter<MipsAsmPrinter> B(getTheMips64elTarget());
}
