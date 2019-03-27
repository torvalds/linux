//===- HexagonVExtract.cpp ------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass will replace multiple occurrences of V6_extractw from the same
// vector register with a combination of a vector store and scalar loads.
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/PassSupport.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

#include <map>

using namespace llvm;

static cl::opt<unsigned> VExtractThreshold("hexagon-vextract-threshold",
  cl::Hidden, cl::ZeroOrMore, cl::init(1),
  cl::desc("Threshold for triggering vextract replacement"));

namespace llvm {
  void initializeHexagonVExtractPass(PassRegistry& Registry);
  FunctionPass *createHexagonVExtract();
}

namespace {
  class HexagonVExtract : public MachineFunctionPass {
  public:
    static char ID;
    HexagonVExtract() : MachineFunctionPass(ID) {}

    StringRef getPassName() const override {
      return "Hexagon optimize vextract";
    }
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    const HexagonSubtarget *HST = nullptr;
    const HexagonInstrInfo *HII = nullptr;

    unsigned genElemLoad(MachineInstr *ExtI, unsigned BaseR,
                         MachineRegisterInfo &MRI);
  };

  char HexagonVExtract::ID = 0;
}

INITIALIZE_PASS(HexagonVExtract, "hexagon-vextract",
  "Hexagon optimize vextract", false, false)

unsigned HexagonVExtract::genElemLoad(MachineInstr *ExtI, unsigned BaseR,
                                      MachineRegisterInfo &MRI) {
  MachineBasicBlock &ExtB = *ExtI->getParent();
  DebugLoc DL = ExtI->getDebugLoc();
  unsigned ElemR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);

  unsigned ExtIdxR = ExtI->getOperand(2).getReg();
  unsigned ExtIdxS = ExtI->getOperand(2).getSubReg();

  // Simplified check for a compile-time constant value of ExtIdxR.
  if (ExtIdxS == 0) {
    MachineInstr *DI = MRI.getVRegDef(ExtIdxR);
    if (DI->getOpcode() == Hexagon::A2_tfrsi) {
      unsigned V = DI->getOperand(1).getImm();
      V &= (HST->getVectorLength()-1) & -4u;

      BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::L2_loadri_io), ElemR)
        .addReg(BaseR)
        .addImm(V);
      return ElemR;
    }
  }

  unsigned IdxR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::A2_andir), IdxR)
    .add(ExtI->getOperand(2))
    .addImm(-4);
  BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::L4_loadri_rr), ElemR)
    .addReg(BaseR)
    .addReg(IdxR)
    .addImm(0);
  return ElemR;
}

bool HexagonVExtract::runOnMachineFunction(MachineFunction &MF) {
  HST = &MF.getSubtarget<HexagonSubtarget>();
  HII = HST->getInstrInfo();
  const auto &HRI = *HST->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  std::map<unsigned, SmallVector<MachineInstr*,4>> VExtractMap;
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc != Hexagon::V6_extractw)
        continue;
      unsigned VecR = MI.getOperand(1).getReg();
      VExtractMap[VecR].push_back(&MI);
    }
  }

  for (auto &P : VExtractMap) {
    unsigned VecR = P.first;
    if (P.second.size() <= VExtractThreshold)
      continue;

    const auto &VecRC = *MRI.getRegClass(VecR);
    int FI = MFI.CreateSpillStackObject(HRI.getSpillSize(VecRC),
                                        HRI.getSpillAlignment(VecRC));
    MachineInstr *DefI = MRI.getVRegDef(VecR);
    MachineBasicBlock::iterator At = std::next(DefI->getIterator());
    MachineBasicBlock &DefB = *DefI->getParent();
    unsigned StoreOpc = VecRC.getID() == Hexagon::HvxVRRegClassID
                          ? Hexagon::V6_vS32b_ai
                          : Hexagon::PS_vstorerw_ai;
    BuildMI(DefB, At, DefI->getDebugLoc(), HII->get(StoreOpc))
      .addFrameIndex(FI)
      .addImm(0)
      .addReg(VecR);

    unsigned VecSize = HRI.getRegSizeInBits(VecRC) / 8;

    for (MachineInstr *ExtI : P.second) {
      assert(ExtI->getOpcode() == Hexagon::V6_extractw);
      unsigned SR = ExtI->getOperand(1).getSubReg();
      assert(ExtI->getOperand(1).getReg() == VecR);

      MachineBasicBlock &ExtB = *ExtI->getParent();
      DebugLoc DL = ExtI->getDebugLoc();
      unsigned BaseR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::PS_fi), BaseR)
        .addFrameIndex(FI)
        .addImm(SR == 0 ? 0 : VecSize/2);

      unsigned ElemR = genElemLoad(ExtI, BaseR, MRI);
      unsigned ExtR = ExtI->getOperand(0).getReg();
      MRI.replaceRegWith(ExtR, ElemR);
      ExtB.erase(ExtI);
      Changed = true;
    }
  }

  return Changed;
}

FunctionPass *llvm::createHexagonVExtract() {
  return new HexagonVExtract();
}
