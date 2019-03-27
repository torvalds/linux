//===-- NVPTXReplaceImageHandles.cpp - Replace image handles for Fermi ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// On Fermi, image handles are not supported. To work around this, we traverse
// the machine code and replace image handles with concrete symbols. For this
// to work reliably, inlining of all function call must be performed.
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "NVPTXMachineFunctionInfo.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
class NVPTXReplaceImageHandles : public MachineFunctionPass {
private:
  static char ID;
  DenseSet<MachineInstr *> InstrsToRemove;

public:
  NVPTXReplaceImageHandles();

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "NVPTX Replace Image Handles";
  }
private:
  bool processInstr(MachineInstr &MI);
  void replaceImageHandle(MachineOperand &Op, MachineFunction &MF);
  bool findIndexForHandle(MachineOperand &Op, MachineFunction &MF,
                          unsigned &Idx);
};
}

char NVPTXReplaceImageHandles::ID = 0;

NVPTXReplaceImageHandles::NVPTXReplaceImageHandles()
  : MachineFunctionPass(ID) {}

bool NVPTXReplaceImageHandles::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  InstrsToRemove.clear();

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end(); BI != BE;
       ++BI) {
    for (MachineBasicBlock::iterator I = (*BI).begin(), E = (*BI).end();
         I != E; ++I) {
      MachineInstr &MI = *I;
      Changed |= processInstr(MI);
    }
  }

  // Now clean up any handle-access instructions
  // This is needed in debug mode when code cleanup passes are not executed,
  // but we need the handle access to be eliminated because they are not
  // valid instructions when image handles are disabled.
  for (DenseSet<MachineInstr *>::iterator I = InstrsToRemove.begin(),
       E = InstrsToRemove.end(); I != E; ++I) {
    (*I)->eraseFromParent();
  }
  return Changed;
}

bool NVPTXReplaceImageHandles::processInstr(MachineInstr &MI) {
  MachineFunction &MF = *MI.getParent()->getParent();
  const MCInstrDesc &MCID = MI.getDesc();

  if (MCID.TSFlags & NVPTXII::IsTexFlag) {
    // This is a texture fetch, so operand 4 is a texref and operand 5 is
    // a samplerref
    MachineOperand &TexHandle = MI.getOperand(4);
    replaceImageHandle(TexHandle, MF);

    if (!(MCID.TSFlags & NVPTXII::IsTexModeUnifiedFlag)) {
      MachineOperand &SampHandle = MI.getOperand(5);
      replaceImageHandle(SampHandle, MF);
    }

    return true;
  } else if (MCID.TSFlags & NVPTXII::IsSuldMask) {
    unsigned VecSize =
      1 << (((MCID.TSFlags & NVPTXII::IsSuldMask) >> NVPTXII::IsSuldShift) - 1);

    // For a surface load of vector size N, the Nth operand will be the surfref
    MachineOperand &SurfHandle = MI.getOperand(VecSize);

    replaceImageHandle(SurfHandle, MF);

    return true;
  } else if (MCID.TSFlags & NVPTXII::IsSustFlag) {
    // This is a surface store, so operand 0 is a surfref
    MachineOperand &SurfHandle = MI.getOperand(0);

    replaceImageHandle(SurfHandle, MF);

    return true;
  } else if (MCID.TSFlags & NVPTXII::IsSurfTexQueryFlag) {
    // This is a query, so operand 1 is a surfref/texref
    MachineOperand &Handle = MI.getOperand(1);

    replaceImageHandle(Handle, MF);

    return true;
  }

  return false;
}

void NVPTXReplaceImageHandles::
replaceImageHandle(MachineOperand &Op, MachineFunction &MF) {
  unsigned Idx;
  if (findIndexForHandle(Op, MF, Idx)) {
    Op.ChangeToImmediate(Idx);
  }
}

bool NVPTXReplaceImageHandles::
findIndexForHandle(MachineOperand &Op, MachineFunction &MF, unsigned &Idx) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  NVPTXMachineFunctionInfo *MFI = MF.getInfo<NVPTXMachineFunctionInfo>();

  assert(Op.isReg() && "Handle is not in a reg?");

  // Which instruction defines the handle?
  MachineInstr &TexHandleDef = *MRI.getVRegDef(Op.getReg());

  switch (TexHandleDef.getOpcode()) {
  case NVPTX::LD_i64_avar: {
    // The handle is a parameter value being loaded, replace with the
    // parameter symbol
    const NVPTXTargetMachine &TM =
        static_cast<const NVPTXTargetMachine &>(MF.getTarget());
    if (TM.getDrvInterface() == NVPTX::CUDA) {
      // For CUDA, we preserve the param loads coming from function arguments
      return false;
    }

    assert(TexHandleDef.getOperand(6).isSymbol() && "Load is not a symbol!");
    StringRef Sym = TexHandleDef.getOperand(6).getSymbolName();
    std::string ParamBaseName = MF.getName();
    ParamBaseName += "_param_";
    assert(Sym.startswith(ParamBaseName) && "Invalid symbol reference");
    unsigned Param = atoi(Sym.data()+ParamBaseName.size());
    std::string NewSym;
    raw_string_ostream NewSymStr(NewSym);
    NewSymStr << MF.getName() << "_param_" << Param;

    InstrsToRemove.insert(&TexHandleDef);
    Idx = MFI->getImageHandleSymbolIndex(NewSymStr.str().c_str());
    return true;
  }
  case NVPTX::texsurf_handles: {
    // The handle is a global variable, replace with the global variable name
    assert(TexHandleDef.getOperand(1).isGlobal() && "Load is not a global!");
    const GlobalValue *GV = TexHandleDef.getOperand(1).getGlobal();
    assert(GV->hasName() && "Global sampler must be named!");
    InstrsToRemove.insert(&TexHandleDef);
    Idx = MFI->getImageHandleSymbolIndex(GV->getName().data());
    return true;
  }
  case NVPTX::nvvm_move_i64:
  case TargetOpcode::COPY: {
    bool Res = findIndexForHandle(TexHandleDef.getOperand(1), MF, Idx);
    if (Res) {
      InstrsToRemove.insert(&TexHandleDef);
    }
    return Res;
  }
  default:
    llvm_unreachable("Unknown instruction operating on handle");
  }
}

MachineFunctionPass *llvm::createNVPTXReplaceImageHandlesPass() {
  return new NVPTXReplaceImageHandles();
}
