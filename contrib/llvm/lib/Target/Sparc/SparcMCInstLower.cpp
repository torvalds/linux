//===-- SparcMCInstLower.cpp - Convert Sparc MachineInstr to MCInst -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Sparc MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SparcMCExpr.h"
#include "Sparc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"

using namespace llvm;


static MCOperand LowerSymbolOperand(const MachineInstr *MI,
                                    const MachineOperand &MO,
                                    AsmPrinter &AP) {

  SparcMCExpr::VariantKind Kind =
    (SparcMCExpr::VariantKind)MO.getTargetFlags();
  const MCSymbol *Symbol = nullptr;

  switch(MO.getType()) {
  default: llvm_unreachable("Unknown type in LowerSymbolOperand");
  case MachineOperand::MO_MachineBasicBlock:
    Symbol = MO.getMBB()->getSymbol();
    break;

  case MachineOperand::MO_GlobalAddress:
    Symbol = AP.getSymbol(MO.getGlobal());
    break;

  case MachineOperand::MO_BlockAddress:
    Symbol = AP.GetBlockAddressSymbol(MO.getBlockAddress());
    break;

  case MachineOperand::MO_ExternalSymbol:
    Symbol = AP.GetExternalSymbolSymbol(MO.getSymbolName());
    break;

  case MachineOperand::MO_ConstantPoolIndex:
    Symbol = AP.GetCPISymbol(MO.getIndex());
    break;
  }

  const MCSymbolRefExpr *MCSym = MCSymbolRefExpr::create(Symbol,
                                                         AP.OutContext);
  const SparcMCExpr *expr = SparcMCExpr::create(Kind, MCSym,
                                                AP.OutContext);
  return MCOperand::createExpr(expr);
}

static MCOperand LowerOperand(const MachineInstr *MI,
                              const MachineOperand &MO,
                              AsmPrinter &AP) {
  switch(MO.getType()) {
  default: llvm_unreachable("unknown operand type"); break;
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      break;
    return MCOperand::createReg(MO.getReg());

  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());

  case MachineOperand::MO_MachineBasicBlock:
  case MachineOperand::MO_GlobalAddress:
  case MachineOperand::MO_BlockAddress:
  case MachineOperand::MO_ExternalSymbol:
  case MachineOperand::MO_ConstantPoolIndex:
    return LowerSymbolOperand(MI, MO, AP);

  case MachineOperand::MO_RegisterMask:   break;

  }
  return MCOperand();
}

void llvm::LowerSparcMachineInstrToMCInst(const MachineInstr *MI,
                                          MCInst &OutMI,
                                          AsmPrinter &AP)
{

  OutMI.setOpcode(MI->getOpcode());

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    MCOperand MCOp = LowerOperand(MI, MO, AP);

    if (MCOp.isValid())
      OutMI.addOperand(MCOp);
  }
}
