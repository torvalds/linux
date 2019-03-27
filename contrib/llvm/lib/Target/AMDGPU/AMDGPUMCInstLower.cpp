//===- AMDGPUMCInstLower.cpp - Lower AMDGPU MachineInstr to an MCInst -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Code to lower AMDGPU MachineInstrs to their corresponding MCInst.
//
//===----------------------------------------------------------------------===//
//

#include "AMDGPUAsmPrinter.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "InstPrinter/AMDGPUInstPrinter.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "R600AsmPrinter.h"
#include "SIInstrInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include <algorithm>

using namespace llvm;

namespace {

class AMDGPUMCInstLower {
  MCContext &Ctx;
  const TargetSubtargetInfo &ST;
  const AsmPrinter &AP;

  const MCExpr *getLongBranchBlockExpr(const MachineBasicBlock &SrcBB,
                                       const MachineOperand &MO) const;

public:
  AMDGPUMCInstLower(MCContext &ctx, const TargetSubtargetInfo &ST,
                    const AsmPrinter &AP);

  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const;

  /// Lower a MachineInstr to an MCInst
  void lower(const MachineInstr *MI, MCInst &OutMI) const;

};

class R600MCInstLower : public AMDGPUMCInstLower {
public:
  R600MCInstLower(MCContext &ctx, const R600Subtarget &ST,
                  const AsmPrinter &AP);

  /// Lower a MachineInstr to an MCInst
  void lower(const MachineInstr *MI, MCInst &OutMI) const;
};


} // End anonymous namespace

#include "AMDGPUGenMCPseudoLowering.inc"

AMDGPUMCInstLower::AMDGPUMCInstLower(MCContext &ctx,
                                     const TargetSubtargetInfo &st,
                                     const AsmPrinter &ap):
  Ctx(ctx), ST(st), AP(ap) { }

static MCSymbolRefExpr::VariantKind getVariantKind(unsigned MOFlags) {
  switch (MOFlags) {
  default:
    return MCSymbolRefExpr::VK_None;
  case SIInstrInfo::MO_GOTPCREL:
    return MCSymbolRefExpr::VK_GOTPCREL;
  case SIInstrInfo::MO_GOTPCREL32_LO:
    return MCSymbolRefExpr::VK_AMDGPU_GOTPCREL32_LO;
  case SIInstrInfo::MO_GOTPCREL32_HI:
    return MCSymbolRefExpr::VK_AMDGPU_GOTPCREL32_HI;
  case SIInstrInfo::MO_REL32_LO:
    return MCSymbolRefExpr::VK_AMDGPU_REL32_LO;
  case SIInstrInfo::MO_REL32_HI:
    return MCSymbolRefExpr::VK_AMDGPU_REL32_HI;
  }
}

const MCExpr *AMDGPUMCInstLower::getLongBranchBlockExpr(
  const MachineBasicBlock &SrcBB,
  const MachineOperand &MO) const {
  const MCExpr *DestBBSym
    = MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx);
  const MCExpr *SrcBBSym = MCSymbolRefExpr::create(SrcBB.getSymbol(), Ctx);

  assert(SrcBB.front().getOpcode() == AMDGPU::S_GETPC_B64 &&
         ST.getInstrInfo()->get(AMDGPU::S_GETPC_B64).Size == 4);

  // s_getpc_b64 returns the address of next instruction.
  const MCConstantExpr *One = MCConstantExpr::create(4, Ctx);
  SrcBBSym = MCBinaryExpr::createAdd(SrcBBSym, One, Ctx);

  if (MO.getTargetFlags() == AMDGPU::TF_LONG_BRANCH_FORWARD)
    return MCBinaryExpr::createSub(DestBBSym, SrcBBSym, Ctx);

  assert(MO.getTargetFlags() == AMDGPU::TF_LONG_BRANCH_BACKWARD);
  return MCBinaryExpr::createSub(SrcBBSym, DestBBSym, Ctx);
}

bool AMDGPUMCInstLower::lowerOperand(const MachineOperand &MO,
                                     MCOperand &MCOp) const {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Immediate:
    MCOp = MCOperand::createImm(MO.getImm());
    return true;
  case MachineOperand::MO_Register:
    MCOp = MCOperand::createReg(AMDGPU::getMCReg(MO.getReg(), ST));
    return true;
  case MachineOperand::MO_MachineBasicBlock: {
    if (MO.getTargetFlags() != 0) {
      MCOp = MCOperand::createExpr(
        getLongBranchBlockExpr(*MO.getParent()->getParent(), MO));
    } else {
      MCOp = MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx));
    }

    return true;
  }
  case MachineOperand::MO_GlobalAddress: {
    const GlobalValue *GV = MO.getGlobal();
    SmallString<128> SymbolName;
    AP.getNameWithPrefix(SymbolName, GV);
    MCSymbol *Sym = Ctx.getOrCreateSymbol(SymbolName);
    const MCExpr *SymExpr =
      MCSymbolRefExpr::create(Sym, getVariantKind(MO.getTargetFlags()),Ctx);
    const MCExpr *Expr = MCBinaryExpr::createAdd(SymExpr,
      MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
    MCOp = MCOperand::createExpr(Expr);
    return true;
  }
  case MachineOperand::MO_ExternalSymbol: {
    MCSymbol *Sym = Ctx.getOrCreateSymbol(StringRef(MO.getSymbolName()));
    Sym->setExternal(true);
    const MCSymbolRefExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
    MCOp = MCOperand::createExpr(Expr);
    return true;
  }
  case MachineOperand::MO_RegisterMask:
    // Regmasks are like implicit defs.
    return false;
  }
}

void AMDGPUMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  unsigned Opcode = MI->getOpcode();
  const auto *TII = static_cast<const SIInstrInfo*>(ST.getInstrInfo());

  // FIXME: Should be able to handle this with emitPseudoExpansionLowering. We
  // need to select it to the subtarget specific version, and there's no way to
  // do that with a single pseudo source operation.
  if (Opcode == AMDGPU::S_SETPC_B64_return)
    Opcode = AMDGPU::S_SETPC_B64;
  else if (Opcode == AMDGPU::SI_CALL) {
    // SI_CALL is just S_SWAPPC_B64 with an additional operand to track the
    // called function (which we need to remove here).
    OutMI.setOpcode(TII->pseudoToMCOpcode(AMDGPU::S_SWAPPC_B64));
    MCOperand Dest, Src;
    lowerOperand(MI->getOperand(0), Dest);
    lowerOperand(MI->getOperand(1), Src);
    OutMI.addOperand(Dest);
    OutMI.addOperand(Src);
    return;
  } else if (Opcode == AMDGPU::SI_TCRETURN) {
    // TODO: How to use branch immediate and avoid register+add?
    Opcode = AMDGPU::S_SETPC_B64;
  }

  int MCOpcode = TII->pseudoToMCOpcode(Opcode);
  if (MCOpcode == -1) {
    LLVMContext &C = MI->getParent()->getParent()->getFunction().getContext();
    C.emitError("AMDGPUMCInstLower::lower - Pseudo instruction doesn't have "
                "a target-specific version: " + Twine(MI->getOpcode()));
  }

  OutMI.setOpcode(MCOpcode);

  for (const MachineOperand &MO : MI->explicit_operands()) {
    MCOperand MCOp;
    lowerOperand(MO, MCOp);
    OutMI.addOperand(MCOp);
  }
}

bool AMDGPUAsmPrinter::lowerOperand(const MachineOperand &MO,
                                    MCOperand &MCOp) const {
  const GCNSubtarget &STI = MF->getSubtarget<GCNSubtarget>();
  AMDGPUMCInstLower MCInstLowering(OutContext, STI, *this);
  return MCInstLowering.lowerOperand(MO, MCOp);
}

static const MCExpr *lowerAddrSpaceCast(const TargetMachine &TM,
                                        const Constant *CV,
                                        MCContext &OutContext) {
  // TargetMachine does not support llvm-style cast. Use C++-style cast.
  // This is safe since TM is always of type AMDGPUTargetMachine or its
  // derived class.
  auto &AT = static_cast<const AMDGPUTargetMachine&>(TM);
  auto *CE = dyn_cast<ConstantExpr>(CV);

  // Lower null pointers in private and local address space.
  // Clang generates addrspacecast for null pointers in private and local
  // address space, which needs to be lowered.
  if (CE && CE->getOpcode() == Instruction::AddrSpaceCast) {
    auto Op = CE->getOperand(0);
    auto SrcAddr = Op->getType()->getPointerAddressSpace();
    if (Op->isNullValue() && AT.getNullPointerValue(SrcAddr) == 0) {
      auto DstAddr = CE->getType()->getPointerAddressSpace();
      return MCConstantExpr::create(AT.getNullPointerValue(DstAddr),
        OutContext);
    }
  }
  return nullptr;
}

const MCExpr *AMDGPUAsmPrinter::lowerConstant(const Constant *CV) {
  if (const MCExpr *E = lowerAddrSpaceCast(TM, CV, OutContext))
    return E;
  return AsmPrinter::lowerConstant(CV);
}

void AMDGPUAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  if (emitPseudoExpansionLowering(*OutStreamer, MI))
    return;

  const GCNSubtarget &STI = MF->getSubtarget<GCNSubtarget>();
  AMDGPUMCInstLower MCInstLowering(OutContext, STI, *this);

  StringRef Err;
  if (!STI.getInstrInfo()->verifyInstruction(*MI, Err)) {
    LLVMContext &C = MI->getParent()->getParent()->getFunction().getContext();
    C.emitError("Illegal instruction detected: " + Err);
    MI->print(errs());
  }

  if (MI->isBundle()) {
    const MachineBasicBlock *MBB = MI->getParent();
    MachineBasicBlock::const_instr_iterator I = ++MI->getIterator();
    while (I != MBB->instr_end() && I->isInsideBundle()) {
      EmitInstruction(&*I);
      ++I;
    }
  } else {
    // We don't want SI_MASK_BRANCH/SI_RETURN_TO_EPILOG encoded. They are
    // placeholder terminator instructions and should only be printed as
    // comments.
    if (MI->getOpcode() == AMDGPU::SI_MASK_BRANCH) {
      if (isVerbose()) {
        SmallVector<char, 16> BBStr;
        raw_svector_ostream Str(BBStr);

        const MachineBasicBlock *MBB = MI->getOperand(0).getMBB();
        const MCSymbolRefExpr *Expr
          = MCSymbolRefExpr::create(MBB->getSymbol(), OutContext);
        Expr->print(Str, MAI);
        OutStreamer->emitRawComment(Twine(" mask branch ") + BBStr);
      }

      return;
    }

    if (MI->getOpcode() == AMDGPU::SI_RETURN_TO_EPILOG) {
      if (isVerbose())
        OutStreamer->emitRawComment(" return to shader part epilog");
      return;
    }

    if (MI->getOpcode() == AMDGPU::WAVE_BARRIER) {
      if (isVerbose())
        OutStreamer->emitRawComment(" wave barrier");
      return;
    }

    if (MI->getOpcode() == AMDGPU::SI_MASKED_UNREACHABLE) {
      if (isVerbose())
        OutStreamer->emitRawComment(" divergent unreachable");
      return;
    }

    MCInst TmpInst;
    MCInstLowering.lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);

#ifdef EXPENSIVE_CHECKS
    // Sanity-check getInstSizeInBytes on explicitly specified CPUs (it cannot
    // work correctly for the generic CPU).
    //
    // The isPseudo check really shouldn't be here, but unfortunately there are
    // some negative lit tests that depend on being able to continue through
    // here even when pseudo instructions haven't been lowered.
    if (!MI->isPseudo() && STI.isCPUStringValid(STI.getCPU())) {
      SmallVector<MCFixup, 4> Fixups;
      SmallVector<char, 16> CodeBytes;
      raw_svector_ostream CodeStream(CodeBytes);

      std::unique_ptr<MCCodeEmitter> InstEmitter(createSIMCCodeEmitter(
          *STI.getInstrInfo(), *OutContext.getRegisterInfo(), OutContext));
      InstEmitter->encodeInstruction(TmpInst, CodeStream, Fixups, STI);

      assert(CodeBytes.size() == STI.getInstrInfo()->getInstSizeInBytes(*MI));
    }
#endif

    if (STI.dumpCode()) {
      // Disassemble instruction/operands to text.
      DisasmLines.resize(DisasmLines.size() + 1);
      std::string &DisasmLine = DisasmLines.back();
      raw_string_ostream DisasmStream(DisasmLine);

      AMDGPUInstPrinter InstPrinter(*TM.getMCAsmInfo(),
                                    *STI.getInstrInfo(),
                                    *STI.getRegisterInfo());
      InstPrinter.printInst(&TmpInst, DisasmStream, StringRef(), STI);

      // Disassemble instruction/operands to hex representation.
      SmallVector<MCFixup, 4> Fixups;
      SmallVector<char, 16> CodeBytes;
      raw_svector_ostream CodeStream(CodeBytes);

      auto &ObjStreamer = static_cast<MCObjectStreamer&>(*OutStreamer);
      MCCodeEmitter &InstEmitter = ObjStreamer.getAssembler().getEmitter();
      InstEmitter.encodeInstruction(TmpInst, CodeStream, Fixups,
                                    MF->getSubtarget<MCSubtargetInfo>());
      HexLines.resize(HexLines.size() + 1);
      std::string &HexLine = HexLines.back();
      raw_string_ostream HexStream(HexLine);

      for (size_t i = 0; i < CodeBytes.size(); i += 4) {
        unsigned int CodeDWord = *(unsigned int *)&CodeBytes[i];
        HexStream << format("%s%08X", (i > 0 ? " " : ""), CodeDWord);
      }

      DisasmStream.flush();
      DisasmLineMaxLen = std::max(DisasmLineMaxLen, DisasmLine.size());
    }
  }
}

R600MCInstLower::R600MCInstLower(MCContext &Ctx, const R600Subtarget &ST,
                                 const AsmPrinter &AP) :
        AMDGPUMCInstLower(Ctx, ST, AP) { }

void R600MCInstLower::lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());
  for (const MachineOperand &MO : MI->explicit_operands()) {
    MCOperand MCOp;
    lowerOperand(MO, MCOp);
    OutMI.addOperand(MCOp);
  }
}

void R600AsmPrinter::EmitInstruction(const MachineInstr *MI) {
  const R600Subtarget &STI = MF->getSubtarget<R600Subtarget>();
  R600MCInstLower MCInstLowering(OutContext, STI, *this);

  StringRef Err;
  if (!STI.getInstrInfo()->verifyInstruction(*MI, Err)) {
    LLVMContext &C = MI->getParent()->getParent()->getFunction().getContext();
    C.emitError("Illegal instruction detected: " + Err);
    MI->print(errs());
  }

  if (MI->isBundle()) {
    const MachineBasicBlock *MBB = MI->getParent();
    MachineBasicBlock::const_instr_iterator I = ++MI->getIterator();
    while (I != MBB->instr_end() && I->isInsideBundle()) {
      EmitInstruction(&*I);
      ++I;
    }
  } else {
    MCInst TmpInst;
    MCInstLowering.lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
 }
}

const MCExpr *R600AsmPrinter::lowerConstant(const Constant *CV) {
  if (const MCExpr *E = lowerAddrSpaceCast(TM, CV, OutContext))
    return E;
  return AsmPrinter::lowerConstant(CV);
}
