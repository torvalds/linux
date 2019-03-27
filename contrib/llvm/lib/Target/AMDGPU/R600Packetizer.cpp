//===----- R600Packetizer.cpp - VLIW packetizer ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass implements instructions packetization for R600. It unsets isLast
/// bit of instructions inside a bundle and substitutes src register with
/// PreviousVector when applicable.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "R600InstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "packets"

namespace {

class R600Packetizer : public MachineFunctionPass {

public:
  static char ID;
  R600Packetizer() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addPreserved<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "R600 Packetizer"; }

  bool runOnMachineFunction(MachineFunction &Fn) override;
};

class R600PacketizerList : public VLIWPacketizerList {
private:
  const R600InstrInfo *TII;
  const R600RegisterInfo &TRI;
  bool VLIW5;
  bool ConsideredInstUsesAlreadyWrittenVectorElement;

  unsigned getSlot(const MachineInstr &MI) const {
    return TRI.getHWRegChan(MI.getOperand(0).getReg());
  }

  /// \returns register to PV chan mapping for bundle/single instructions that
  /// immediately precedes I.
  DenseMap<unsigned, unsigned> getPreviousVector(MachineBasicBlock::iterator I)
      const {
    DenseMap<unsigned, unsigned> Result;
    I--;
    if (!TII->isALUInstr(I->getOpcode()) && !I->isBundle())
      return Result;
    MachineBasicBlock::instr_iterator BI = I.getInstrIterator();
    if (I->isBundle())
      BI++;
    int LastDstChan = -1;
    do {
      bool isTrans = false;
      int BISlot = getSlot(*BI);
      if (LastDstChan >= BISlot)
        isTrans = true;
      LastDstChan = BISlot;
      if (TII->isPredicated(*BI))
        continue;
      int OperandIdx = TII->getOperandIdx(BI->getOpcode(), R600::OpName::write);
      if (OperandIdx > -1 && BI->getOperand(OperandIdx).getImm() == 0)
        continue;
      int DstIdx = TII->getOperandIdx(BI->getOpcode(), R600::OpName::dst);
      if (DstIdx == -1) {
        continue;
      }
      unsigned Dst = BI->getOperand(DstIdx).getReg();
      if (isTrans || TII->isTransOnly(*BI)) {
        Result[Dst] = R600::PS;
        continue;
      }
      if (BI->getOpcode() == R600::DOT4_r600 ||
          BI->getOpcode() == R600::DOT4_eg) {
        Result[Dst] = R600::PV_X;
        continue;
      }
      if (Dst == R600::OQAP) {
        continue;
      }
      unsigned PVReg = 0;
      switch (TRI.getHWRegChan(Dst)) {
      case 0:
        PVReg = R600::PV_X;
        break;
      case 1:
        PVReg = R600::PV_Y;
        break;
      case 2:
        PVReg = R600::PV_Z;
        break;
      case 3:
        PVReg = R600::PV_W;
        break;
      default:
        llvm_unreachable("Invalid Chan");
      }
      Result[Dst] = PVReg;
    } while ((++BI)->isBundledWithPred());
    return Result;
  }

  void substitutePV(MachineInstr &MI, const DenseMap<unsigned, unsigned> &PVs)
      const {
    unsigned Ops[] = {
      R600::OpName::src0,
      R600::OpName::src1,
      R600::OpName::src2
    };
    for (unsigned i = 0; i < 3; i++) {
      int OperandIdx = TII->getOperandIdx(MI.getOpcode(), Ops[i]);
      if (OperandIdx < 0)
        continue;
      unsigned Src = MI.getOperand(OperandIdx).getReg();
      const DenseMap<unsigned, unsigned>::const_iterator It = PVs.find(Src);
      if (It != PVs.end())
        MI.getOperand(OperandIdx).setReg(It->second);
    }
  }
public:
  // Ctor.
  R600PacketizerList(MachineFunction &MF, const R600Subtarget &ST,
                     MachineLoopInfo &MLI)
      : VLIWPacketizerList(MF, MLI, nullptr),
        TII(ST.getInstrInfo()),
        TRI(TII->getRegisterInfo()) {
    VLIW5 = !ST.hasCaymanISA();
  }

  // initPacketizerState - initialize some internal flags.
  void initPacketizerState() override {
    ConsideredInstUsesAlreadyWrittenVectorElement = false;
  }

  // ignorePseudoInstruction - Ignore bundling of pseudo instructions.
  bool ignorePseudoInstruction(const MachineInstr &MI,
                               const MachineBasicBlock *MBB) override {
    return false;
  }

  // isSoloInstruction - return true if instruction MI can not be packetized
  // with any other instruction, which means that MI itself is a packet.
  bool isSoloInstruction(const MachineInstr &MI) override {
    if (TII->isVector(MI))
      return true;
    if (!TII->isALUInstr(MI.getOpcode()))
      return true;
    if (MI.getOpcode() == R600::GROUP_BARRIER)
      return true;
    // XXX: This can be removed once the packetizer properly handles all the
    // LDS instruction group restrictions.
    return TII->isLDSInstr(MI.getOpcode());
  }

  // isLegalToPacketizeTogether - Is it legal to packetize SUI and SUJ
  // together.
  bool isLegalToPacketizeTogether(SUnit *SUI, SUnit *SUJ) override {
    MachineInstr *MII = SUI->getInstr(), *MIJ = SUJ->getInstr();
    if (getSlot(*MII) == getSlot(*MIJ))
      ConsideredInstUsesAlreadyWrittenVectorElement = true;
    // Does MII and MIJ share the same pred_sel ?
    int OpI = TII->getOperandIdx(MII->getOpcode(), R600::OpName::pred_sel),
        OpJ = TII->getOperandIdx(MIJ->getOpcode(), R600::OpName::pred_sel);
    unsigned PredI = (OpI > -1)?MII->getOperand(OpI).getReg():0,
        PredJ = (OpJ > -1)?MIJ->getOperand(OpJ).getReg():0;
    if (PredI != PredJ)
      return false;
    if (SUJ->isSucc(SUI)) {
      for (unsigned i = 0, e = SUJ->Succs.size(); i < e; ++i) {
        const SDep &Dep = SUJ->Succs[i];
        if (Dep.getSUnit() != SUI)
          continue;
        if (Dep.getKind() == SDep::Anti)
          continue;
        if (Dep.getKind() == SDep::Output)
          if (MII->getOperand(0).getReg() != MIJ->getOperand(0).getReg())
            continue;
        return false;
      }
    }

    bool ARDef =
        TII->definesAddressRegister(*MII) || TII->definesAddressRegister(*MIJ);
    bool ARUse =
        TII->usesAddressRegister(*MII) || TII->usesAddressRegister(*MIJ);

    return !ARDef || !ARUse;
  }

  // isLegalToPruneDependencies - Is it legal to prune dependece between SUI
  // and SUJ.
  bool isLegalToPruneDependencies(SUnit *SUI, SUnit *SUJ) override {
    return false;
  }

  void setIsLastBit(MachineInstr *MI, unsigned Bit) const {
    unsigned LastOp = TII->getOperandIdx(MI->getOpcode(), R600::OpName::last);
    MI->getOperand(LastOp).setImm(Bit);
  }

  bool isBundlableWithCurrentPMI(MachineInstr &MI,
                                 const DenseMap<unsigned, unsigned> &PV,
                                 std::vector<R600InstrInfo::BankSwizzle> &BS,
                                 bool &isTransSlot) {
    isTransSlot = TII->isTransOnly(MI);
    assert (!isTransSlot || VLIW5);

    // Is the dst reg sequence legal ?
    if (!isTransSlot && !CurrentPacketMIs.empty()) {
      if (getSlot(MI) <= getSlot(*CurrentPacketMIs.back())) {
        if (ConsideredInstUsesAlreadyWrittenVectorElement &&
            !TII->isVectorOnly(MI) && VLIW5) {
          isTransSlot = true;
          LLVM_DEBUG({
            dbgs() << "Considering as Trans Inst :";
            MI.dump();
          });
        }
        else
          return false;
      }
    }

    // Are the Constants limitations met ?
    CurrentPacketMIs.push_back(&MI);
    if (!TII->fitsConstReadLimitations(CurrentPacketMIs)) {
      LLVM_DEBUG({
        dbgs() << "Couldn't pack :\n";
        MI.dump();
        dbgs() << "with the following packets :\n";
        for (unsigned i = 0, e = CurrentPacketMIs.size() - 1; i < e; i++) {
          CurrentPacketMIs[i]->dump();
          dbgs() << "\n";
        }
        dbgs() << "because of Consts read limitations\n";
      });
      CurrentPacketMIs.pop_back();
      return false;
    }

    // Is there a BankSwizzle set that meet Read Port limitations ?
    if (!TII->fitsReadPortLimitations(CurrentPacketMIs,
            PV, BS, isTransSlot)) {
      LLVM_DEBUG({
        dbgs() << "Couldn't pack :\n";
        MI.dump();
        dbgs() << "with the following packets :\n";
        for (unsigned i = 0, e = CurrentPacketMIs.size() - 1; i < e; i++) {
          CurrentPacketMIs[i]->dump();
          dbgs() << "\n";
        }
        dbgs() << "because of Read port limitations\n";
      });
      CurrentPacketMIs.pop_back();
      return false;
    }

    // We cannot read LDS source registers from the Trans slot.
    if (isTransSlot && TII->readsLDSSrcReg(MI))
      return false;

    CurrentPacketMIs.pop_back();
    return true;
  }

  MachineBasicBlock::iterator addToPacket(MachineInstr &MI) override {
    MachineBasicBlock::iterator FirstInBundle =
        CurrentPacketMIs.empty() ? &MI : CurrentPacketMIs.front();
    const DenseMap<unsigned, unsigned> &PV =
        getPreviousVector(FirstInBundle);
    std::vector<R600InstrInfo::BankSwizzle> BS;
    bool isTransSlot;

    if (isBundlableWithCurrentPMI(MI, PV, BS, isTransSlot)) {
      for (unsigned i = 0, e = CurrentPacketMIs.size(); i < e; i++) {
        MachineInstr *MI = CurrentPacketMIs[i];
        unsigned Op = TII->getOperandIdx(MI->getOpcode(),
            R600::OpName::bank_swizzle);
        MI->getOperand(Op).setImm(BS[i]);
      }
      unsigned Op =
          TII->getOperandIdx(MI.getOpcode(), R600::OpName::bank_swizzle);
      MI.getOperand(Op).setImm(BS.back());
      if (!CurrentPacketMIs.empty())
        setIsLastBit(CurrentPacketMIs.back(), 0);
      substitutePV(MI, PV);
      MachineBasicBlock::iterator It = VLIWPacketizerList::addToPacket(MI);
      if (isTransSlot) {
        endPacket(std::next(It)->getParent(), std::next(It));
      }
      return It;
    }
    endPacket(MI.getParent(), MI);
    if (TII->isTransOnly(MI))
      return MI;
    return VLIWPacketizerList::addToPacket(MI);
  }
};

bool R600Packetizer::runOnMachineFunction(MachineFunction &Fn) {
  const R600Subtarget &ST = Fn.getSubtarget<R600Subtarget>();
  const R600InstrInfo *TII = ST.getInstrInfo();

  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfo>();

  // Instantiate the packetizer.
  R600PacketizerList Packetizer(Fn, ST, MLI);

  // DFA state table should not be empty.
  assert(Packetizer.getResourceTracker() && "Empty DFA table!");
  assert(Packetizer.getResourceTracker()->getInstrItins());

  if (Packetizer.getResourceTracker()->getInstrItins()->isEmpty())
    return false;

  //
  // Loop over all basic blocks and remove KILL pseudo-instructions
  // These instructions confuse the dependence analysis. Consider:
  // D0 = ...   (Insn 0)
  // R0 = KILL R0, D0 (Insn 1)
  // R0 = ... (Insn 2)
  // Here, Insn 1 will result in the dependence graph not emitting an output
  // dependence between Insn 0 and Insn 2. This can lead to incorrect
  // packetization
  //
  for (MachineFunction::iterator MBB = Fn.begin(), MBBe = Fn.end();
       MBB != MBBe; ++MBB) {
    MachineBasicBlock::iterator End = MBB->end();
    MachineBasicBlock::iterator MI = MBB->begin();
    while (MI != End) {
      if (MI->isKill() || MI->getOpcode() == R600::IMPLICIT_DEF ||
          (MI->getOpcode() == R600::CF_ALU && !MI->getOperand(8).getImm())) {
        MachineBasicBlock::iterator DeleteMI = MI;
        ++MI;
        MBB->erase(DeleteMI);
        End = MBB->end();
        continue;
      }
      ++MI;
    }
  }

  // Loop over all of the basic blocks.
  for (MachineFunction::iterator MBB = Fn.begin(), MBBe = Fn.end();
       MBB != MBBe; ++MBB) {
    // Find scheduling regions and schedule / packetize each region.
    unsigned RemainingCount = MBB->size();
    for(MachineBasicBlock::iterator RegionEnd = MBB->end();
        RegionEnd != MBB->begin();) {
      // The next region starts above the previous region. Look backward in the
      // instruction stream until we find the nearest boundary.
      MachineBasicBlock::iterator I = RegionEnd;
      for(;I != MBB->begin(); --I, --RemainingCount) {
        if (TII->isSchedulingBoundary(*std::prev(I), &*MBB, Fn))
          break;
      }
      I = MBB->begin();

      // Skip empty scheduling regions.
      if (I == RegionEnd) {
        RegionEnd = std::prev(RegionEnd);
        --RemainingCount;
        continue;
      }
      // Skip regions with one instruction.
      if (I == std::prev(RegionEnd)) {
        RegionEnd = std::prev(RegionEnd);
        continue;
      }

      Packetizer.PacketizeMIs(&*MBB, &*I, RegionEnd);
      RegionEnd = I;
    }
  }

  return true;

}

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(R600Packetizer, DEBUG_TYPE,
                     "R600 Packetizer", false, false)
INITIALIZE_PASS_END(R600Packetizer, DEBUG_TYPE,
                    "R600 Packetizer", false, false)

char R600Packetizer::ID = 0;

char &llvm::R600PacketizerID = R600Packetizer::ID;

llvm::FunctionPass *llvm::createR600Packetizer() {
  return new R600Packetizer();
}
