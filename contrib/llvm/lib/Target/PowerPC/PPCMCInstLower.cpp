//===-- PPCMCInstLower.cpp - Convert PPC MachineInstr to an MCInst --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower PPC MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCExpr.h"
#include "PPC.h"
#include "PPCSubtarget.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
using namespace llvm;

static MachineModuleInfoMachO &getMachOMMI(AsmPrinter &AP) {
  return AP.MMI->getObjFileInfo<MachineModuleInfoMachO>();
}

static MCSymbol *GetSymbolFromOperand(const MachineOperand &MO,
                                      AsmPrinter &AP) {
  const TargetMachine &TM = AP.TM;
  Mangler &Mang = TM.getObjFileLowering()->getMangler();
  const DataLayout &DL = AP.getDataLayout();
  MCContext &Ctx = AP.OutContext;

  SmallString<128> Name;
  StringRef Suffix;
  if (MO.getTargetFlags() & PPCII::MO_NLP_FLAG)
    Suffix = "$non_lazy_ptr";

  if (!Suffix.empty())
    Name += DL.getPrivateGlobalPrefix();

  if (!MO.isGlobal()) {
    assert(MO.isSymbol() && "Isn't a symbol reference");
    Mangler::getNameWithPrefix(Name, MO.getSymbolName(), DL);
  } else {
    const GlobalValue *GV = MO.getGlobal();
    TM.getNameWithPrefix(Name, GV, Mang);
  }

  Name += Suffix;
  MCSymbol *Sym = Ctx.getOrCreateSymbol(Name);

  // If the symbol reference is actually to a non_lazy_ptr, not to the symbol,
  // then add the suffix.
  if (MO.getTargetFlags() & PPCII::MO_NLP_FLAG) {
    MachineModuleInfoMachO &MachO = getMachOMMI(AP);

    MachineModuleInfoImpl::StubValueTy &StubSym = MachO.getGVStubEntry(Sym);

    if (!StubSym.getPointer()) {
      assert(MO.isGlobal() && "Extern symbol not handled yet");
      StubSym = MachineModuleInfoImpl::
                   StubValueTy(AP.getSymbol(MO.getGlobal()),
                               !MO.getGlobal()->hasInternalLinkage());
    }
    return Sym;
  }

  return Sym;
}

static MCOperand GetSymbolRef(const MachineOperand &MO, const MCSymbol *Symbol,
                              AsmPrinter &Printer, bool isDarwin) {
  MCContext &Ctx = Printer.OutContext;
  MCSymbolRefExpr::VariantKind RefKind = MCSymbolRefExpr::VK_None;

  unsigned access = MO.getTargetFlags() & PPCII::MO_ACCESS_MASK;

  switch (access) {
    case PPCII::MO_TPREL_LO:
      RefKind = MCSymbolRefExpr::VK_PPC_TPREL_LO;
      break;
    case PPCII::MO_TPREL_HA:
      RefKind = MCSymbolRefExpr::VK_PPC_TPREL_HA;
      break;
    case PPCII::MO_DTPREL_LO:
      RefKind = MCSymbolRefExpr::VK_PPC_DTPREL_LO;
      break;
    case PPCII::MO_TLSLD_LO:
      RefKind = MCSymbolRefExpr::VK_PPC_GOT_TLSLD_LO;
      break;
    case PPCII::MO_TOC_LO:
      RefKind = MCSymbolRefExpr::VK_PPC_TOC_LO;
      break;
    case PPCII::MO_TLS:
      RefKind = MCSymbolRefExpr::VK_PPC_TLS;
      break;
  }

 if (MO.getTargetFlags() == PPCII::MO_PLT)
    RefKind = MCSymbolRefExpr::VK_PLT;

  const MachineFunction *MF = MO.getParent()->getParent()->getParent();
  const PPCSubtarget *Subtarget = &(MF->getSubtarget<PPCSubtarget>());
  const TargetMachine &TM = Printer.TM;
  const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, RefKind, Ctx);
  // -msecure-plt option works only in PIC mode. If secure plt mode
  // is on add 32768 to symbol.
  if (Subtarget->isSecurePlt() && TM.isPositionIndependent() &&
      MO.getTargetFlags() == PPCII::MO_PLT)
    Expr = MCBinaryExpr::createAdd(Expr,
                                   MCConstantExpr::create(32768, Ctx),
                                   Ctx);

  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(Expr,
                                   MCConstantExpr::create(MO.getOffset(), Ctx),
                                   Ctx);

  // Subtract off the PIC base if required.
  if (MO.getTargetFlags() & PPCII::MO_PIC_FLAG) {
    const MachineFunction *MF = MO.getParent()->getParent()->getParent();

    const MCExpr *PB = MCSymbolRefExpr::create(MF->getPICBaseSymbol(), Ctx);
    Expr = MCBinaryExpr::createSub(Expr, PB, Ctx);
  }

  // Add ha16() / lo16() markers if required.
  switch (access) {
    case PPCII::MO_LO:
      Expr = PPCMCExpr::createLo(Expr, isDarwin, Ctx);
      break;
    case PPCII::MO_HA:
      Expr = PPCMCExpr::createHa(Expr, isDarwin, Ctx);
      break;
  }

  return MCOperand::createExpr(Expr);
}

void llvm::LowerPPCMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                        AsmPrinter &AP, bool isDarwin) {
  OutMI.setOpcode(MI->getOpcode());

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    MCOperand MCOp;
    if (LowerPPCMachineOperandToMCOperand(MI->getOperand(i), MCOp, AP,
                                          isDarwin))
      OutMI.addOperand(MCOp);
  }
}

bool llvm::LowerPPCMachineOperandToMCOperand(const MachineOperand &MO,
                                             MCOperand &OutMO, AsmPrinter &AP,
                                             bool isDarwin) {
  switch (MO.getType()) {
  default:
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    assert(!MO.getSubReg() && "Subregs should be eliminated!");
    assert(MO.getReg() > PPC::NoRegister &&
           MO.getReg() < PPC::NUM_TARGET_REGS &&
           "Invalid register for this target!");
    OutMO = MCOperand::createReg(MO.getReg());
    return true;
  case MachineOperand::MO_Immediate:
    OutMO = MCOperand::createImm(MO.getImm());
    return true;
  case MachineOperand::MO_MachineBasicBlock:
    OutMO = MCOperand::createExpr(
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), AP.OutContext));
    return true;
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_ExternalSymbol:
    OutMO = GetSymbolRef(MO, GetSymbolFromOperand(MO, AP), AP, isDarwin);
    return true;
  case MachineOperand::MO_JumpTableIndex:
    OutMO = GetSymbolRef(MO, AP.GetJTISymbol(MO.getIndex()), AP, isDarwin);
    return true;
  case MachineOperand::MO_ConstantPoolIndex:
    OutMO = GetSymbolRef(MO, AP.GetCPISymbol(MO.getIndex()), AP, isDarwin);
    return true;
  case MachineOperand::MO_BlockAddress:
    OutMO = GetSymbolRef(MO, AP.GetBlockAddressSymbol(MO.getBlockAddress()), AP,
                         isDarwin);
    return true;
  case MachineOperand::MO_RegisterMask:
    return false;
  }
}
