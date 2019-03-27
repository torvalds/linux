//===- HexagonOptAddrMode.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This implements a Hexagon-specific pass to optimize addressing mode for
// load/store instructions.
//===----------------------------------------------------------------------===//

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "RDFGraph.h"
#include "RDFLiveness.h"
#include "RDFRegisters.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

#define DEBUG_TYPE "opt-addr-mode"

using namespace llvm;
using namespace rdf;

static cl::opt<int> CodeGrowthLimit("hexagon-amode-growth-limit",
  cl::Hidden, cl::init(0), cl::desc("Code growth limit for address mode "
  "optimization"));

namespace llvm {

  FunctionPass *createHexagonOptAddrMode();
  void initializeHexagonOptAddrModePass(PassRegistry&);

} // end namespace llvm

namespace {

class HexagonOptAddrMode : public MachineFunctionPass {
public:
  static char ID;

  HexagonOptAddrMode() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Optimize addressing mode of load/store";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachineDominanceFrontier>();
    AU.setPreservesAll();
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  using MISetType = DenseSet<MachineInstr *>;
  using InstrEvalMap = DenseMap<MachineInstr *, bool>;

  MachineRegisterInfo *MRI = nullptr;
  const HexagonInstrInfo *HII = nullptr;
  const HexagonRegisterInfo *HRI = nullptr;
  MachineDominatorTree *MDT = nullptr;
  DataFlowGraph *DFG = nullptr;
  DataFlowGraph::DefStackMap DefM;
  Liveness *LV = nullptr;
  MISetType Deleted;

  bool processBlock(NodeAddr<BlockNode *> BA);
  bool xformUseMI(MachineInstr *TfrMI, MachineInstr *UseMI,
                  NodeAddr<UseNode *> UseN, unsigned UseMOnum);
  bool processAddUses(NodeAddr<StmtNode *> AddSN, MachineInstr *AddMI,
                      const NodeList &UNodeList);
  bool updateAddUses(MachineInstr *AddMI, MachineInstr *UseMI);
  bool analyzeUses(unsigned DefR, const NodeList &UNodeList,
                   InstrEvalMap &InstrEvalResult, short &SizeInc);
  bool hasRepForm(MachineInstr &MI, unsigned TfrDefR);
  bool canRemoveAddasl(NodeAddr<StmtNode *> AddAslSN, MachineInstr &MI,
                       const NodeList &UNodeList);
  bool isSafeToExtLR(NodeAddr<StmtNode *> SN, MachineInstr *MI,
                     unsigned LRExtReg, const NodeList &UNodeList);
  void getAllRealUses(NodeAddr<StmtNode *> SN, NodeList &UNodeList);
  bool allValidCandidates(NodeAddr<StmtNode *> SA, NodeList &UNodeList);
  short getBaseWithLongOffset(const MachineInstr &MI) const;
  bool changeStore(MachineInstr *OldMI, MachineOperand ImmOp,
                   unsigned ImmOpNum);
  bool changeLoad(MachineInstr *OldMI, MachineOperand ImmOp, unsigned ImmOpNum);
  bool changeAddAsl(NodeAddr<UseNode *> AddAslUN, MachineInstr *AddAslMI,
                    const MachineOperand &ImmOp, unsigned ImmOpNum);
  bool isValidOffset(MachineInstr *MI, int Offset);
};

} // end anonymous namespace

char HexagonOptAddrMode::ID = 0;

INITIALIZE_PASS_BEGIN(HexagonOptAddrMode, "amode-opt",
                      "Optimize addressing mode", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineDominanceFrontier)
INITIALIZE_PASS_END(HexagonOptAddrMode, "amode-opt", "Optimize addressing mode",
                    false, false)

bool HexagonOptAddrMode::hasRepForm(MachineInstr &MI, unsigned TfrDefR) {
  const MCInstrDesc &MID = MI.getDesc();

  if ((!MID.mayStore() && !MID.mayLoad()) || HII->isPredicated(MI))
    return false;

  if (MID.mayStore()) {
    MachineOperand StOp = MI.getOperand(MI.getNumOperands() - 1);
    if (StOp.isReg() && StOp.getReg() == TfrDefR)
      return false;
  }

  if (HII->getAddrMode(MI) == HexagonII::BaseRegOffset)
    // Tranform to Absolute plus register offset.
    return (HII->changeAddrMode_rr_ur(MI) >= 0);
  else if (HII->getAddrMode(MI) == HexagonII::BaseImmOffset)
    // Tranform to absolute addressing mode.
    return (HII->changeAddrMode_io_abs(MI) >= 0);

  return false;
}

// Check if addasl instruction can be removed. This is possible only
// if it's feeding to only load/store instructions with base + register
// offset as these instruction can be tranformed to use 'absolute plus
// shifted register offset'.
// ex:
// Rs = ##foo
// Rx = addasl(Rs, Rt, #2)
// Rd = memw(Rx + #28)
// Above three instructions can be replaced with Rd = memw(Rt<<#2 + ##foo+28)

bool HexagonOptAddrMode::canRemoveAddasl(NodeAddr<StmtNode *> AddAslSN,
                                         MachineInstr &MI,
                                         const NodeList &UNodeList) {
  // check offset size in addasl. if 'offset > 3' return false
  const MachineOperand &OffsetOp = MI.getOperand(3);
  if (!OffsetOp.isImm() || OffsetOp.getImm() > 3)
    return false;

  unsigned OffsetReg = MI.getOperand(2).getReg();
  RegisterRef OffsetRR;
  NodeId OffsetRegRD = 0;
  for (NodeAddr<UseNode *> UA : AddAslSN.Addr->members_if(DFG->IsUse, *DFG)) {
    RegisterRef RR = UA.Addr->getRegRef(*DFG);
    if (OffsetReg == RR.Reg) {
      OffsetRR = RR;
      OffsetRegRD = UA.Addr->getReachingDef();
    }
  }

  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    NodeAddr<UseNode *> UA = *I;
    NodeAddr<InstrNode *> IA = UA.Addr->getOwner(*DFG);
    if (UA.Addr->getFlags() & NodeAttrs::PhiRef)
      return false;
    NodeAddr<RefNode*> AA = LV->getNearestAliasedRef(OffsetRR, IA);
    if ((DFG->IsDef(AA) && AA.Id != OffsetRegRD) ||
         AA.Addr->getReachingDef() != OffsetRegRD)
      return false;

    MachineInstr &UseMI = *NodeAddr<StmtNode *>(IA).Addr->getCode();
    NodeAddr<DefNode *> OffsetRegDN = DFG->addr<DefNode *>(OffsetRegRD);
    // Reaching Def to an offset register can't be a phi.
    if ((OffsetRegDN.Addr->getFlags() & NodeAttrs::PhiRef) &&
        MI.getParent() != UseMI.getParent())
    return false;

    const MCInstrDesc &UseMID = UseMI.getDesc();
    if ((!UseMID.mayLoad() && !UseMID.mayStore()) ||
        HII->getAddrMode(UseMI) != HexagonII::BaseImmOffset ||
        getBaseWithLongOffset(UseMI) < 0)
      return false;

    // Addasl output can't be a store value.
    if (UseMID.mayStore() && UseMI.getOperand(2).isReg() &&
        UseMI.getOperand(2).getReg() == MI.getOperand(0).getReg())
      return false;

    for (auto &Mo : UseMI.operands())
      if (Mo.isFI())
        return false;
  }
  return true;
}

bool HexagonOptAddrMode::allValidCandidates(NodeAddr<StmtNode *> SA,
                                            NodeList &UNodeList) {
  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    NodeAddr<UseNode *> UN = *I;
    RegisterRef UR = UN.Addr->getRegRef(*DFG);
    NodeSet Visited, Defs;
    const auto &P = LV->getAllReachingDefsRec(UR, UN, Visited, Defs);
    if (!P.second) {
      LLVM_DEBUG({
        dbgs() << "*** Unable to collect all reaching defs for use ***\n"
               << PrintNode<UseNode*>(UN, *DFG) << '\n'
               << "The program's complexity may exceed the limits.\n";
      });
      return false;
    }
    const auto &ReachingDefs = P.first;
    if (ReachingDefs.size() > 1) {
      LLVM_DEBUG({
        dbgs() << "*** Multiple Reaching Defs found!!! ***\n";
        for (auto DI : ReachingDefs) {
          NodeAddr<UseNode *> DA = DFG->addr<UseNode *>(DI);
          NodeAddr<StmtNode *> TempIA = DA.Addr->getOwner(*DFG);
          dbgs() << "\t\t[Reaching Def]: "
                 << Print<NodeAddr<InstrNode *>>(TempIA, *DFG) << "\n";
        }
      });
      return false;
    }
  }
  return true;
}

void HexagonOptAddrMode::getAllRealUses(NodeAddr<StmtNode *> SA,
                                        NodeList &UNodeList) {
  for (NodeAddr<DefNode *> DA : SA.Addr->members_if(DFG->IsDef, *DFG)) {
    LLVM_DEBUG(dbgs() << "\t\t[DefNode]: "
                      << Print<NodeAddr<DefNode *>>(DA, *DFG) << "\n");
    RegisterRef DR = DFG->getPRI().normalize(DA.Addr->getRegRef(*DFG));

    auto UseSet = LV->getAllReachedUses(DR, DA);

    for (auto UI : UseSet) {
      NodeAddr<UseNode *> UA = DFG->addr<UseNode *>(UI);
      LLVM_DEBUG({
        NodeAddr<StmtNode *> TempIA = UA.Addr->getOwner(*DFG);
        dbgs() << "\t\t\t[Reached Use]: "
               << Print<NodeAddr<InstrNode *>>(TempIA, *DFG) << "\n";
      });

      if (UA.Addr->getFlags() & NodeAttrs::PhiRef) {
        NodeAddr<PhiNode *> PA = UA.Addr->getOwner(*DFG);
        NodeId id = PA.Id;
        const Liveness::RefMap &phiUse = LV->getRealUses(id);
        LLVM_DEBUG(dbgs() << "\t\t\t\tphi real Uses"
                          << Print<Liveness::RefMap>(phiUse, *DFG) << "\n");
        if (!phiUse.empty()) {
          for (auto I : phiUse) {
            if (!DFG->getPRI().alias(RegisterRef(I.first), DR))
              continue;
            auto phiUseSet = I.second;
            for (auto phiUI : phiUseSet) {
              NodeAddr<UseNode *> phiUA = DFG->addr<UseNode *>(phiUI.first);
              UNodeList.push_back(phiUA);
            }
          }
        }
      } else
        UNodeList.push_back(UA);
    }
  }
}

bool HexagonOptAddrMode::isSafeToExtLR(NodeAddr<StmtNode *> SN,
                                       MachineInstr *MI, unsigned LRExtReg,
                                       const NodeList &UNodeList) {
  RegisterRef LRExtRR;
  NodeId LRExtRegRD = 0;
  // Iterate through all the UseNodes in SN and find the reaching def
  // for the LRExtReg.
  for (NodeAddr<UseNode *> UA : SN.Addr->members_if(DFG->IsUse, *DFG)) {
    RegisterRef RR = UA.Addr->getRegRef(*DFG);
    if (LRExtReg == RR.Reg) {
      LRExtRR = RR;
      LRExtRegRD = UA.Addr->getReachingDef();
    }
  }

  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    NodeAddr<UseNode *> UA = *I;
    NodeAddr<InstrNode *> IA = UA.Addr->getOwner(*DFG);
    // The reaching def of LRExtRR at load/store node should be same as the
    // one reaching at the SN.
    if (UA.Addr->getFlags() & NodeAttrs::PhiRef)
      return false;
    NodeAddr<RefNode*> AA = LV->getNearestAliasedRef(LRExtRR, IA);
    if ((DFG->IsDef(AA) && AA.Id != LRExtRegRD) ||
        AA.Addr->getReachingDef() != LRExtRegRD) {
      LLVM_DEBUG(
          dbgs() << "isSafeToExtLR: Returning false; another reaching def\n");
      return false;
    }

    MachineInstr *UseMI = NodeAddr<StmtNode *>(IA).Addr->getCode();
    NodeAddr<DefNode *> LRExtRegDN = DFG->addr<DefNode *>(LRExtRegRD);
    // Reaching Def to LRExtReg can't be a phi.
    if ((LRExtRegDN.Addr->getFlags() & NodeAttrs::PhiRef) &&
        MI->getParent() != UseMI->getParent())
    return false;
  }
  return true;
}

bool HexagonOptAddrMode::isValidOffset(MachineInstr *MI, int Offset) {
  unsigned AlignMask = 0;
  switch (HII->getMemAccessSize(*MI)) {
  case HexagonII::MemAccessSize::DoubleWordAccess:
    AlignMask = 0x7;
    break;
  case HexagonII::MemAccessSize::WordAccess:
    AlignMask = 0x3;
    break;
  case HexagonII::MemAccessSize::HalfWordAccess:
    AlignMask = 0x1;
    break;
  case HexagonII::MemAccessSize::ByteAccess:
    AlignMask = 0x0;
    break;
  default:
    return false;
  }

  if ((AlignMask & Offset) != 0)
    return false;
  return HII->isValidOffset(MI->getOpcode(), Offset, HRI, false);
}

bool HexagonOptAddrMode::processAddUses(NodeAddr<StmtNode *> AddSN,
                                        MachineInstr *AddMI,
                                        const NodeList &UNodeList) {

  unsigned AddDefR = AddMI->getOperand(0).getReg();
  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    NodeAddr<UseNode *> UN = *I;
    NodeAddr<StmtNode *> SN = UN.Addr->getOwner(*DFG);
    MachineInstr *MI = SN.Addr->getCode();
    const MCInstrDesc &MID = MI->getDesc();
    if ((!MID.mayLoad() && !MID.mayStore()) ||
        HII->getAddrMode(*MI) != HexagonII::BaseImmOffset ||
        HII->isHVXVec(*MI))
      return false;

    MachineOperand BaseOp = MID.mayLoad() ? MI->getOperand(1)
                                          : MI->getOperand(0);

    if (!BaseOp.isReg() || BaseOp.getReg() != AddDefR)
      return false;

    MachineOperand OffsetOp = MID.mayLoad() ? MI->getOperand(2)
                                            : MI->getOperand(1);
    if (!OffsetOp.isImm())
      return false;

    int64_t newOffset = OffsetOp.getImm() + AddMI->getOperand(2).getImm();
    if (!isValidOffset(MI, newOffset))
      return false;

    // Since we'll be extending the live range of Rt in the following example,
    // make sure that is safe. another definition of Rt doesn't exist between 'add'
    // and load/store instruction.
    //
    // Ex: Rx= add(Rt,#10)
    //     memw(Rx+#0) = Rs
    // will be replaced with =>  memw(Rt+#10) = Rs
    unsigned BaseReg = AddMI->getOperand(1).getReg();
    if (!isSafeToExtLR(AddSN, AddMI, BaseReg, UNodeList))
      return false;
  }

  // Update all the uses of 'add' with the appropriate base and offset
  // values.
  bool Changed = false;
  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    NodeAddr<UseNode *> UseN = *I;
    assert(!(UseN.Addr->getFlags() & NodeAttrs::PhiRef) &&
           "Found a PhiRef node as a real reached use!!");

    NodeAddr<StmtNode *> OwnerN = UseN.Addr->getOwner(*DFG);
    MachineInstr *UseMI = OwnerN.Addr->getCode();
    LLVM_DEBUG(dbgs() << "\t\t[MI <BB#" << UseMI->getParent()->getNumber()
                      << ">]: " << *UseMI << "\n");
    Changed |= updateAddUses(AddMI, UseMI);
  }

  if (Changed)
    Deleted.insert(AddMI);

  return Changed;
}

bool HexagonOptAddrMode::updateAddUses(MachineInstr *AddMI,
                                        MachineInstr *UseMI) {
  const MachineOperand ImmOp = AddMI->getOperand(2);
  const MachineOperand AddRegOp = AddMI->getOperand(1);
  unsigned newReg = AddRegOp.getReg();
  const MCInstrDesc &MID = UseMI->getDesc();

  MachineOperand &BaseOp = MID.mayLoad() ? UseMI->getOperand(1)
                                         : UseMI->getOperand(0);
  MachineOperand &OffsetOp = MID.mayLoad() ? UseMI->getOperand(2)
                                           : UseMI->getOperand(1);
  BaseOp.setReg(newReg);
  BaseOp.setIsUndef(AddRegOp.isUndef());
  BaseOp.setImplicit(AddRegOp.isImplicit());
  OffsetOp.setImm(ImmOp.getImm() + OffsetOp.getImm());
  MRI->clearKillFlags(newReg);

  return true;
}

bool HexagonOptAddrMode::analyzeUses(unsigned tfrDefR,
                                     const NodeList &UNodeList,
                                     InstrEvalMap &InstrEvalResult,
                                     short &SizeInc) {
  bool KeepTfr = false;
  bool HasRepInstr = false;
  InstrEvalResult.clear();

  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    bool CanBeReplaced = false;
    NodeAddr<UseNode *> UN = *I;
    NodeAddr<StmtNode *> SN = UN.Addr->getOwner(*DFG);
    MachineInstr &MI = *SN.Addr->getCode();
    const MCInstrDesc &MID = MI.getDesc();
    if ((MID.mayLoad() || MID.mayStore())) {
      if (!hasRepForm(MI, tfrDefR)) {
        KeepTfr = true;
        continue;
      }
      SizeInc++;
      CanBeReplaced = true;
    } else if (MI.getOpcode() == Hexagon::S2_addasl_rrri) {
      NodeList AddaslUseList;

      LLVM_DEBUG(dbgs() << "\nGetting ReachedUses for === " << MI << "\n");
      getAllRealUses(SN, AddaslUseList);
      // Process phi nodes.
      if (allValidCandidates(SN, AddaslUseList) &&
          canRemoveAddasl(SN, MI, AddaslUseList)) {
        SizeInc += AddaslUseList.size();
        SizeInc -= 1; // Reduce size by 1 as addasl itself can be removed.
        CanBeReplaced = true;
      } else
        SizeInc++;
    } else
      // Currently, only load/store and addasl are handled.
      // Some other instructions to consider -
      // A2_add -> A2_addi
      // M4_mpyrr_addr -> M4_mpyrr_addi
      KeepTfr = true;

    InstrEvalResult[&MI] = CanBeReplaced;
    HasRepInstr |= CanBeReplaced;
  }

  // Reduce total size by 2 if original tfr can be deleted.
  if (!KeepTfr)
    SizeInc -= 2;

  return HasRepInstr;
}

bool HexagonOptAddrMode::changeLoad(MachineInstr *OldMI, MachineOperand ImmOp,
                                    unsigned ImmOpNum) {
  bool Changed = false;
  MachineBasicBlock *BB = OldMI->getParent();
  auto UsePos = MachineBasicBlock::iterator(OldMI);
  MachineBasicBlock::instr_iterator InsertPt = UsePos.getInstrIterator();
  ++InsertPt;
  unsigned OpStart;
  unsigned OpEnd = OldMI->getNumOperands();
  MachineInstrBuilder MIB;

  if (ImmOpNum == 1) {
    if (HII->getAddrMode(*OldMI) == HexagonII::BaseRegOffset) {
      short NewOpCode = HII->changeAddrMode_rr_ur(*OldMI);
      assert(NewOpCode >= 0 && "Invalid New opcode\n");
      MIB = BuildMI(*BB, InsertPt, OldMI->getDebugLoc(), HII->get(NewOpCode));
      MIB.add(OldMI->getOperand(0));
      MIB.add(OldMI->getOperand(2));
      MIB.add(OldMI->getOperand(3));
      MIB.add(ImmOp);
      OpStart = 4;
      Changed = true;
    } else if (HII->getAddrMode(*OldMI) == HexagonII::BaseImmOffset &&
               OldMI->getOperand(2).isImm()) {
      short NewOpCode = HII->changeAddrMode_io_abs(*OldMI);
      assert(NewOpCode >= 0 && "Invalid New opcode\n");
      MIB = BuildMI(*BB, InsertPt, OldMI->getDebugLoc(), HII->get(NewOpCode))
                .add(OldMI->getOperand(0));
      const GlobalValue *GV = ImmOp.getGlobal();
      int64_t Offset = ImmOp.getOffset() + OldMI->getOperand(2).getImm();

      MIB.addGlobalAddress(GV, Offset, ImmOp.getTargetFlags());
      OpStart = 3;
      Changed = true;
    } else
      Changed = false;

    LLVM_DEBUG(dbgs() << "[Changing]: " << *OldMI << "\n");
    LLVM_DEBUG(dbgs() << "[TO]: " << *MIB << "\n");
  } else if (ImmOpNum == 2) {
    if (OldMI->getOperand(3).isImm() && OldMI->getOperand(3).getImm() == 0) {
      short NewOpCode = HII->changeAddrMode_rr_io(*OldMI);
      assert(NewOpCode >= 0 && "Invalid New opcode\n");
      MIB = BuildMI(*BB, InsertPt, OldMI->getDebugLoc(), HII->get(NewOpCode));
      MIB.add(OldMI->getOperand(0));
      MIB.add(OldMI->getOperand(1));
      MIB.add(ImmOp);
      OpStart = 4;
      Changed = true;
      LLVM_DEBUG(dbgs() << "[Changing]: " << *OldMI << "\n");
      LLVM_DEBUG(dbgs() << "[TO]: " << *MIB << "\n");
    }
  }

  if (Changed)
    for (unsigned i = OpStart; i < OpEnd; ++i)
      MIB.add(OldMI->getOperand(i));

  return Changed;
}

bool HexagonOptAddrMode::changeStore(MachineInstr *OldMI, MachineOperand ImmOp,
                                     unsigned ImmOpNum) {
  bool Changed = false;
  unsigned OpStart;
  unsigned OpEnd = OldMI->getNumOperands();
  MachineBasicBlock *BB = OldMI->getParent();
  auto UsePos = MachineBasicBlock::iterator(OldMI);
  MachineBasicBlock::instr_iterator InsertPt = UsePos.getInstrIterator();
  ++InsertPt;
  MachineInstrBuilder MIB;
  if (ImmOpNum == 0) {
    if (HII->getAddrMode(*OldMI) == HexagonII::BaseRegOffset) {
      short NewOpCode = HII->changeAddrMode_rr_ur(*OldMI);
      assert(NewOpCode >= 0 && "Invalid New opcode\n");
      MIB = BuildMI(*BB, InsertPt, OldMI->getDebugLoc(), HII->get(NewOpCode));
      MIB.add(OldMI->getOperand(1));
      MIB.add(OldMI->getOperand(2));
      MIB.add(ImmOp);
      MIB.add(OldMI->getOperand(3));
      OpStart = 4;
    } else if (HII->getAddrMode(*OldMI) == HexagonII::BaseImmOffset) {
      short NewOpCode = HII->changeAddrMode_io_abs(*OldMI);
      assert(NewOpCode >= 0 && "Invalid New opcode\n");
      MIB = BuildMI(*BB, InsertPt, OldMI->getDebugLoc(), HII->get(NewOpCode));
      const GlobalValue *GV = ImmOp.getGlobal();
      int64_t Offset = ImmOp.getOffset() + OldMI->getOperand(1).getImm();
      MIB.addGlobalAddress(GV, Offset, ImmOp.getTargetFlags());
      MIB.add(OldMI->getOperand(2));
      OpStart = 3;
    }
    Changed = true;
    LLVM_DEBUG(dbgs() << "[Changing]: " << *OldMI << "\n");
    LLVM_DEBUG(dbgs() << "[TO]: " << *MIB << "\n");
  } else if (ImmOpNum == 1 && OldMI->getOperand(2).getImm() == 0) {
    short NewOpCode = HII->changeAddrMode_rr_io(*OldMI);
    assert(NewOpCode >= 0 && "Invalid New opcode\n");
    MIB = BuildMI(*BB, InsertPt, OldMI->getDebugLoc(), HII->get(NewOpCode));
    MIB.add(OldMI->getOperand(0));
    MIB.add(ImmOp);
    OpStart = 3;
    Changed = true;
    LLVM_DEBUG(dbgs() << "[Changing]: " << *OldMI << "\n");
    LLVM_DEBUG(dbgs() << "[TO]: " << *MIB << "\n");
  }
  if (Changed)
    for (unsigned i = OpStart; i < OpEnd; ++i)
      MIB.add(OldMI->getOperand(i));

  return Changed;
}

short HexagonOptAddrMode::getBaseWithLongOffset(const MachineInstr &MI) const {
  if (HII->getAddrMode(MI) == HexagonII::BaseImmOffset) {
    short TempOpCode = HII->changeAddrMode_io_rr(MI);
    return HII->changeAddrMode_rr_ur(TempOpCode);
  }
  return HII->changeAddrMode_rr_ur(MI);
}

bool HexagonOptAddrMode::changeAddAsl(NodeAddr<UseNode *> AddAslUN,
                                      MachineInstr *AddAslMI,
                                      const MachineOperand &ImmOp,
                                      unsigned ImmOpNum) {
  NodeAddr<StmtNode *> SA = AddAslUN.Addr->getOwner(*DFG);

  LLVM_DEBUG(dbgs() << "Processing addasl :" << *AddAslMI << "\n");

  NodeList UNodeList;
  getAllRealUses(SA, UNodeList);

  for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
    NodeAddr<UseNode *> UseUN = *I;
    assert(!(UseUN.Addr->getFlags() & NodeAttrs::PhiRef) &&
           "Can't transform this 'AddAsl' instruction!");

    NodeAddr<StmtNode *> UseIA = UseUN.Addr->getOwner(*DFG);
    LLVM_DEBUG(dbgs() << "[InstrNode]: "
                      << Print<NodeAddr<InstrNode *>>(UseIA, *DFG) << "\n");
    MachineInstr *UseMI = UseIA.Addr->getCode();
    LLVM_DEBUG(dbgs() << "[MI <" << printMBBReference(*UseMI->getParent())
                      << ">]: " << *UseMI << "\n");
    const MCInstrDesc &UseMID = UseMI->getDesc();
    assert(HII->getAddrMode(*UseMI) == HexagonII::BaseImmOffset);

    auto UsePos = MachineBasicBlock::iterator(UseMI);
    MachineBasicBlock::instr_iterator InsertPt = UsePos.getInstrIterator();
    short NewOpCode = getBaseWithLongOffset(*UseMI);
    assert(NewOpCode >= 0 && "Invalid New opcode\n");

    unsigned OpStart;
    unsigned OpEnd = UseMI->getNumOperands();

    MachineBasicBlock *BB = UseMI->getParent();
    MachineInstrBuilder MIB =
        BuildMI(*BB, InsertPt, UseMI->getDebugLoc(), HII->get(NewOpCode));
    // change mem(Rs + # ) -> mem(Rt << # + ##)
    if (UseMID.mayLoad()) {
      MIB.add(UseMI->getOperand(0));
      MIB.add(AddAslMI->getOperand(2));
      MIB.add(AddAslMI->getOperand(3));
      const GlobalValue *GV = ImmOp.getGlobal();
      MIB.addGlobalAddress(GV, UseMI->getOperand(2).getImm()+ImmOp.getOffset(),
                           ImmOp.getTargetFlags());
      OpStart = 3;
    } else if (UseMID.mayStore()) {
      MIB.add(AddAslMI->getOperand(2));
      MIB.add(AddAslMI->getOperand(3));
      const GlobalValue *GV = ImmOp.getGlobal();
      MIB.addGlobalAddress(GV, UseMI->getOperand(1).getImm()+ImmOp.getOffset(),
                           ImmOp.getTargetFlags());
      MIB.add(UseMI->getOperand(2));
      OpStart = 3;
    } else
      llvm_unreachable("Unhandled instruction");

    for (unsigned i = OpStart; i < OpEnd; ++i)
      MIB.add(UseMI->getOperand(i));

    Deleted.insert(UseMI);
  }

  return true;
}

bool HexagonOptAddrMode::xformUseMI(MachineInstr *TfrMI, MachineInstr *UseMI,
                                    NodeAddr<UseNode *> UseN,
                                    unsigned UseMOnum) {
  const MachineOperand ImmOp = TfrMI->getOperand(1);
  const MCInstrDesc &MID = UseMI->getDesc();
  unsigned Changed = false;
  if (MID.mayLoad())
    Changed = changeLoad(UseMI, ImmOp, UseMOnum);
  else if (MID.mayStore())
    Changed = changeStore(UseMI, ImmOp, UseMOnum);
  else if (UseMI->getOpcode() == Hexagon::S2_addasl_rrri)
    Changed = changeAddAsl(UseN, UseMI, ImmOp, UseMOnum);

  if (Changed)
    Deleted.insert(UseMI);

  return Changed;
}

bool HexagonOptAddrMode::processBlock(NodeAddr<BlockNode *> BA) {
  bool Changed = false;

  for (auto IA : BA.Addr->members(*DFG)) {
    if (!DFG->IsCode<NodeAttrs::Stmt>(IA))
      continue;

    NodeAddr<StmtNode *> SA = IA;
    MachineInstr *MI = SA.Addr->getCode();
    if ((MI->getOpcode() != Hexagon::A2_tfrsi ||
         !MI->getOperand(1).isGlobal()) &&
        (MI->getOpcode() != Hexagon::A2_addi ||
         !MI->getOperand(2).isImm() || HII->isConstExtended(*MI)))
    continue;

    LLVM_DEBUG(dbgs() << "[Analyzing " << HII->getName(MI->getOpcode())
                      << "]: " << *MI << "\n\t[InstrNode]: "
                      << Print<NodeAddr<InstrNode *>>(IA, *DFG) << '\n');

    NodeList UNodeList;
    getAllRealUses(SA, UNodeList);

    if (!allValidCandidates(SA, UNodeList))
      continue;

    // Analyze all uses of 'add'. If the output of 'add' is used as an address
    // in the base+immediate addressing mode load/store instructions, see if
    // they can be updated to use the immediate value as an offet. Thus,
    // providing us the opportunity to eliminate 'add'.
    // Ex: Rx= add(Rt,#12)
    //     memw(Rx+#0) = Rs
    // This can be replaced with memw(Rt+#12) = Rs
    //
    // This transformation is only performed if all uses can be updated and
    // the offset isn't required to be constant extended.
    if (MI->getOpcode() == Hexagon::A2_addi) {
      Changed |= processAddUses(SA, MI, UNodeList);
      continue;
    }

    short SizeInc = 0;
    unsigned DefR = MI->getOperand(0).getReg();
    InstrEvalMap InstrEvalResult;

    // Analyze all uses and calculate increase in size. Perform the optimization
    // only if there is no increase in size.
    if (!analyzeUses(DefR, UNodeList, InstrEvalResult, SizeInc))
      continue;
    if (SizeInc > CodeGrowthLimit)
      continue;

    bool KeepTfr = false;

    LLVM_DEBUG(dbgs() << "\t[Total reached uses] : " << UNodeList.size()
                      << "\n");
    LLVM_DEBUG(dbgs() << "\t[Processing Reached Uses] ===\n");
    for (auto I = UNodeList.rbegin(), E = UNodeList.rend(); I != E; ++I) {
      NodeAddr<UseNode *> UseN = *I;
      assert(!(UseN.Addr->getFlags() & NodeAttrs::PhiRef) &&
             "Found a PhiRef node as a real reached use!!");

      NodeAddr<StmtNode *> OwnerN = UseN.Addr->getOwner(*DFG);
      MachineInstr *UseMI = OwnerN.Addr->getCode();
      LLVM_DEBUG(dbgs() << "\t\t[MI <" << printMBBReference(*UseMI->getParent())
                        << ">]: " << *UseMI << "\n");

      int UseMOnum = -1;
      unsigned NumOperands = UseMI->getNumOperands();
      for (unsigned j = 0; j < NumOperands - 1; ++j) {
        const MachineOperand &op = UseMI->getOperand(j);
        if (op.isReg() && op.isUse() && DefR == op.getReg())
          UseMOnum = j;
      }
      // It is possible that the register will not be found in any operand.
      // This could happen, for example, when DefR = R4, but the used
      // register is D2.

      // Change UseMI if replacement is possible. If any replacement failed,
      // or wasn't attempted, make sure to keep the TFR.
      bool Xformed = false;
      if (UseMOnum >= 0 && InstrEvalResult[UseMI])
        Xformed = xformUseMI(MI, UseMI, UseN, UseMOnum);
      Changed |=  Xformed;
      KeepTfr |= !Xformed;
    }
    if (!KeepTfr)
      Deleted.insert(MI);
  }
  return Changed;
}

bool HexagonOptAddrMode::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  bool Changed = false;
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  MRI = &MF.getRegInfo();
  HII = HST.getInstrInfo();
  HRI = HST.getRegisterInfo();
  const auto &MDF = getAnalysis<MachineDominanceFrontier>();
  MDT = &getAnalysis<MachineDominatorTree>();
  const TargetOperandInfo TOI(*HII);

  DataFlowGraph G(MF, *HII, *HRI, *MDT, MDF, TOI);
  // Need to keep dead phis because we can propagate uses of registers into
  // nodes dominated by those would-be phis.
  G.build(BuildOptions::KeepDeadPhis);
  DFG = &G;

  Liveness L(*MRI, *DFG);
  L.computePhiInfo();
  LV = &L;

  Deleted.clear();
  NodeAddr<FuncNode *> FA = DFG->getFunc();
  LLVM_DEBUG(dbgs() << "==== [RefMap#]=====:\n "
                    << Print<NodeAddr<FuncNode *>>(FA, *DFG) << "\n");

  for (NodeAddr<BlockNode *> BA : FA.Addr->members(*DFG))
    Changed |= processBlock(BA);

  for (auto MI : Deleted)
    MI->eraseFromParent();

  if (Changed) {
    G.build();
    L.computeLiveIns();
    L.resetLiveIns();
    L.resetKills();
  }

  return Changed;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createHexagonOptAddrMode() {
  return new HexagonOptAddrMode();
}
