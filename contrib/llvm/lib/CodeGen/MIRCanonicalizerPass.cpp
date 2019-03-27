//===-------------- MIRCanonicalizer.cpp - MIR Canonicalizer --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The purpose of this pass is to employ a canonical code transformation so
// that code compiled with slightly different IR passes can be diffed more
// effectively than otherwise. This is done by renaming vregs in a given
// LiveRange in a canonical way. This pass also does a pseudo-scheduling to
// move defs closer to their use inorder to reduce diffs caused by slightly
// different schedules.
//
// Basic Usage:
//
// llc -o - -run-pass mir-canonicalizer example.mir
//
// Reorders instructions canonically.
// Renames virtual register operands canonically.
// Strips certain MIR artifacts (optionally).
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/raw_ostream.h"

#include <queue>

using namespace llvm;

namespace llvm {
extern char &MIRCanonicalizerID;
} // namespace llvm

#define DEBUG_TYPE "mir-canonicalizer"

static cl::opt<unsigned>
    CanonicalizeFunctionNumber("canon-nth-function", cl::Hidden, cl::init(~0u),
                               cl::value_desc("N"),
                               cl::desc("Function number to canonicalize."));

static cl::opt<unsigned> CanonicalizeBasicBlockNumber(
    "canon-nth-basicblock", cl::Hidden, cl::init(~0u), cl::value_desc("N"),
    cl::desc("BasicBlock number to canonicalize."));

namespace {

class MIRCanonicalizer : public MachineFunctionPass {
public:
  static char ID;
  MIRCanonicalizer() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Rename register operands in a canonical ordering.";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

enum VRType { RSE_Reg = 0, RSE_FrameIndex, RSE_NewCandidate };
class TypedVReg {
  VRType type;
  unsigned reg;

public:
  TypedVReg(unsigned reg) : type(RSE_Reg), reg(reg) {}
  TypedVReg(VRType type) : type(type), reg(~0U) {
    assert(type != RSE_Reg && "Expected a non-register type.");
  }

  bool isReg() const { return type == RSE_Reg; }
  bool isFrameIndex() const { return type == RSE_FrameIndex; }
  bool isCandidate() const { return type == RSE_NewCandidate; }

  VRType getType() const { return type; }
  unsigned getReg() const {
    assert(this->isReg() && "Expected a virtual or physical register.");
    return reg;
  }
};

char MIRCanonicalizer::ID;

char &llvm::MIRCanonicalizerID = MIRCanonicalizer::ID;

INITIALIZE_PASS_BEGIN(MIRCanonicalizer, "mir-canonicalizer",
                      "Rename Register Operands Canonically", false, false)

INITIALIZE_PASS_END(MIRCanonicalizer, "mir-canonicalizer",
                    "Rename Register Operands Canonically", false, false)

static std::vector<MachineBasicBlock *> GetRPOList(MachineFunction &MF) {
  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(&*MF.begin());
  std::vector<MachineBasicBlock *> RPOList;
  for (auto MBB : RPOT) {
    RPOList.push_back(MBB);
  }

  return RPOList;
}

static bool
rescheduleLexographically(std::vector<MachineInstr *> instructions,
                          MachineBasicBlock *MBB,
                          std::function<MachineBasicBlock::iterator()> getPos) {

  bool Changed = false;
  using StringInstrPair = std::pair<std::string, MachineInstr *>;
  std::vector<StringInstrPair> StringInstrMap;

  for (auto *II : instructions) {
    std::string S;
    raw_string_ostream OS(S);
    II->print(OS);
    OS.flush();

    // Trim the assignment, or start from the begining in the case of a store.
    const size_t i = S.find("=");
    StringInstrMap.push_back({(i == std::string::npos) ? S : S.substr(i), II});
  }

  llvm::sort(StringInstrMap,
             [](const StringInstrPair &a, const StringInstrPair &b) -> bool {
               return (a.first < b.first);
             });

  for (auto &II : StringInstrMap) {

    LLVM_DEBUG({
      dbgs() << "Splicing ";
      II.second->dump();
      dbgs() << " right before: ";
      getPos()->dump();
    });

    Changed = true;
    MBB->splice(getPos(), MBB, II.second);
  }

  return Changed;
}

static bool rescheduleCanonically(unsigned &PseudoIdempotentInstCount,
                                  MachineBasicBlock *MBB) {

  bool Changed = false;

  // Calculates the distance of MI from the begining of its parent BB.
  auto getInstrIdx = [](const MachineInstr &MI) {
    unsigned i = 0;
    for (auto &CurMI : *MI.getParent()) {
      if (&CurMI == &MI)
        return i;
      i++;
    }
    return ~0U;
  };

  // Pre-Populate vector of instructions to reschedule so that we don't
  // clobber the iterator.
  std::vector<MachineInstr *> Instructions;
  for (auto &MI : *MBB) {
    Instructions.push_back(&MI);
  }

  std::map<MachineInstr *, std::vector<MachineInstr *>> MultiUsers;
  std::vector<MachineInstr *> PseudoIdempotentInstructions;
  std::vector<unsigned> PhysRegDefs;
  for (auto *II : Instructions) {
    for (unsigned i = 1; i < II->getNumOperands(); i++) {
      MachineOperand &MO = II->getOperand(i);
      if (!MO.isReg())
        continue;

      if (TargetRegisterInfo::isVirtualRegister(MO.getReg()))
        continue;

      if (!MO.isDef())
        continue;

      PhysRegDefs.push_back(MO.getReg());
    }
  }

  for (auto *II : Instructions) {
    if (II->getNumOperands() == 0)
      continue;
    if (II->mayLoadOrStore())
      continue;

    MachineOperand &MO = II->getOperand(0);
    if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
      continue;
    if (!MO.isDef())
      continue;

    bool IsPseudoIdempotent = true;
    for (unsigned i = 1; i < II->getNumOperands(); i++) {

      if (II->getOperand(i).isImm()) {
        continue;
      }

      if (II->getOperand(i).isReg()) {
        if (!TargetRegisterInfo::isVirtualRegister(II->getOperand(i).getReg()))
          if (llvm::find(PhysRegDefs, II->getOperand(i).getReg()) ==
              PhysRegDefs.end()) {
            continue;
          }
      }

      IsPseudoIdempotent = false;
      break;
    }

    if (IsPseudoIdempotent) {
      PseudoIdempotentInstructions.push_back(II);
      continue;
    }

    LLVM_DEBUG(dbgs() << "Operand " << 0 << " of "; II->dump(); MO.dump(););

    MachineInstr *Def = II;
    unsigned Distance = ~0U;
    MachineInstr *UseToBringDefCloserTo = nullptr;
    MachineRegisterInfo *MRI = &MBB->getParent()->getRegInfo();
    for (auto &UO : MRI->use_nodbg_operands(MO.getReg())) {
      MachineInstr *UseInst = UO.getParent();

      const unsigned DefLoc = getInstrIdx(*Def);
      const unsigned UseLoc = getInstrIdx(*UseInst);
      const unsigned Delta = (UseLoc - DefLoc);

      if (UseInst->getParent() != Def->getParent())
        continue;
      if (DefLoc >= UseLoc)
        continue;

      if (Delta < Distance) {
        Distance = Delta;
        UseToBringDefCloserTo = UseInst;
      }
    }

    const auto BBE = MBB->instr_end();
    MachineBasicBlock::iterator DefI = BBE;
    MachineBasicBlock::iterator UseI = BBE;

    for (auto BBI = MBB->instr_begin(); BBI != BBE; ++BBI) {

      if (DefI != BBE && UseI != BBE)
        break;

      if (&*BBI == Def) {
        DefI = BBI;
        continue;
      }

      if (&*BBI == UseToBringDefCloserTo) {
        UseI = BBI;
        continue;
      }
    }

    if (DefI == BBE || UseI == BBE)
      continue;

    LLVM_DEBUG({
      dbgs() << "Splicing ";
      DefI->dump();
      dbgs() << " right before: ";
      UseI->dump();
    });

    MultiUsers[UseToBringDefCloserTo].push_back(Def);
    Changed = true;
    MBB->splice(UseI, MBB, DefI);
  }

  // Sort the defs for users of multiple defs lexographically.
  for (const auto &E : MultiUsers) {

    auto UseI =
        std::find_if(MBB->instr_begin(), MBB->instr_end(),
                     [&](MachineInstr &MI) -> bool { return &MI == E.first; });

    if (UseI == MBB->instr_end())
      continue;

    LLVM_DEBUG(
        dbgs() << "Rescheduling Multi-Use Instructions Lexographically.";);
    Changed |= rescheduleLexographically(
        E.second, MBB, [&]() -> MachineBasicBlock::iterator { return UseI; });
  }

  PseudoIdempotentInstCount = PseudoIdempotentInstructions.size();
  LLVM_DEBUG(
      dbgs() << "Rescheduling Idempotent Instructions Lexographically.";);
  Changed |= rescheduleLexographically(
      PseudoIdempotentInstructions, MBB,
      [&]() -> MachineBasicBlock::iterator { return MBB->begin(); });

  return Changed;
}

static bool propagateLocalCopies(MachineBasicBlock *MBB) {
  bool Changed = false;
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

  std::vector<MachineInstr *> Copies;
  for (MachineInstr &MI : MBB->instrs()) {
    if (MI.isCopy())
      Copies.push_back(&MI);
  }

  for (MachineInstr *MI : Copies) {

    if (!MI->getOperand(0).isReg())
      continue;
    if (!MI->getOperand(1).isReg())
      continue;

    const unsigned Dst = MI->getOperand(0).getReg();
    const unsigned Src = MI->getOperand(1).getReg();

    if (!TargetRegisterInfo::isVirtualRegister(Dst))
      continue;
    if (!TargetRegisterInfo::isVirtualRegister(Src))
      continue;
    if (MRI.getRegClass(Dst) != MRI.getRegClass(Src))
      continue;

    for (auto UI = MRI.use_begin(Dst); UI != MRI.use_end(); ++UI) {
      MachineOperand *MO = &*UI;
      MO->setReg(Src);
      Changed = true;
    }

    MI->eraseFromParent();
  }

  return Changed;
}

/// Here we find our candidates. What makes an interesting candidate?
/// An candidate for a canonicalization tree root is normally any kind of
/// instruction that causes side effects such as a store to memory or a copy to
/// a physical register or a return instruction. We use these as an expression
/// tree root that we walk inorder to build a canonical walk which should result
/// in canoncal vreg renaming.
static std::vector<MachineInstr *> populateCandidates(MachineBasicBlock *MBB) {
  std::vector<MachineInstr *> Candidates;
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();

  for (auto II = MBB->begin(), IE = MBB->end(); II != IE; ++II) {
    MachineInstr *MI = &*II;

    bool DoesMISideEffect = false;

    if (MI->getNumOperands() > 0 && MI->getOperand(0).isReg()) {
      const unsigned Dst = MI->getOperand(0).getReg();
      DoesMISideEffect |= !TargetRegisterInfo::isVirtualRegister(Dst);

      for (auto UI = MRI.use_begin(Dst); UI != MRI.use_end(); ++UI) {
        if (DoesMISideEffect)
          break;
        DoesMISideEffect |= (UI->getParent()->getParent() != MI->getParent());
      }
    }

    if (!MI->mayStore() && !MI->isBranch() && !DoesMISideEffect)
      continue;

    LLVM_DEBUG(dbgs() << "Found Candidate:  "; MI->dump(););
    Candidates.push_back(MI);
  }

  return Candidates;
}

static void doCandidateWalk(std::vector<TypedVReg> &VRegs,
                            std::queue<TypedVReg> &RegQueue,
                            std::vector<MachineInstr *> &VisitedMIs,
                            const MachineBasicBlock *MBB) {

  const MachineFunction &MF = *MBB->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  while (!RegQueue.empty()) {

    auto TReg = RegQueue.front();
    RegQueue.pop();

    if (TReg.isFrameIndex()) {
      LLVM_DEBUG(dbgs() << "Popping frame index.\n";);
      VRegs.push_back(TypedVReg(RSE_FrameIndex));
      continue;
    }

    assert(TReg.isReg() && "Expected vreg or physreg.");
    unsigned Reg = TReg.getReg();

    if (TargetRegisterInfo::isVirtualRegister(Reg)) {
      LLVM_DEBUG({
        dbgs() << "Popping vreg ";
        MRI.def_begin(Reg)->dump();
        dbgs() << "\n";
      });

      if (!llvm::any_of(VRegs, [&](const TypedVReg &TR) {
            return TR.isReg() && TR.getReg() == Reg;
          })) {
        VRegs.push_back(TypedVReg(Reg));
      }
    } else {
      LLVM_DEBUG(dbgs() << "Popping physreg.\n";);
      VRegs.push_back(TypedVReg(Reg));
      continue;
    }

    for (auto RI = MRI.def_begin(Reg), RE = MRI.def_end(); RI != RE; ++RI) {
      MachineInstr *Def = RI->getParent();

      if (Def->getParent() != MBB)
        continue;

      if (llvm::any_of(VisitedMIs,
                       [&](const MachineInstr *VMI) { return Def == VMI; })) {
        break;
      }

      LLVM_DEBUG({
        dbgs() << "\n========================\n";
        dbgs() << "Visited MI: ";
        Def->dump();
        dbgs() << "BB Name: " << Def->getParent()->getName() << "\n";
        dbgs() << "\n========================\n";
      });
      VisitedMIs.push_back(Def);
      for (unsigned I = 1, E = Def->getNumOperands(); I != E; ++I) {

        MachineOperand &MO = Def->getOperand(I);
        if (MO.isFI()) {
          LLVM_DEBUG(dbgs() << "Pushing frame index.\n";);
          RegQueue.push(TypedVReg(RSE_FrameIndex));
        }

        if (!MO.isReg())
          continue;
        RegQueue.push(TypedVReg(MO.getReg()));
      }
    }
  }
}

namespace {
class NamedVRegCursor {
  MachineRegisterInfo &MRI;
  unsigned virtualVRegNumber;

public:
  NamedVRegCursor(MachineRegisterInfo &MRI) : MRI(MRI) {
    unsigned VRegGapIndex = 0;
    const unsigned VR_GAP = (++VRegGapIndex * 1000);

    unsigned I = MRI.createIncompleteVirtualRegister();
    const unsigned E = (((I + VR_GAP) / VR_GAP) + 1) * VR_GAP;

    virtualVRegNumber = E;
  }

  void SkipVRegs() {
    unsigned VRegGapIndex = 1;
    const unsigned VR_GAP = (++VRegGapIndex * 1000);

    unsigned I = virtualVRegNumber;
    const unsigned E = (((I + VR_GAP) / VR_GAP) + 1) * VR_GAP;

    virtualVRegNumber = E;
  }

  unsigned getVirtualVReg() const { return virtualVRegNumber; }

  unsigned incrementVirtualVReg(unsigned incr = 1) {
    virtualVRegNumber += incr;
    return virtualVRegNumber;
  }

  unsigned createVirtualRegister(const TargetRegisterClass *RC) {
    std::string S;
    raw_string_ostream OS(S);
    OS << "namedVReg" << (virtualVRegNumber & ~0x80000000);
    OS.flush();
    virtualVRegNumber++;

    return MRI.createVirtualRegister(RC, OS.str());
  }
};
} // namespace

static std::map<unsigned, unsigned>
GetVRegRenameMap(const std::vector<TypedVReg> &VRegs,
                 const std::vector<unsigned> &renamedInOtherBB,
                 MachineRegisterInfo &MRI, NamedVRegCursor &NVC) {
  std::map<unsigned, unsigned> VRegRenameMap;
  bool FirstCandidate = true;

  for (auto &vreg : VRegs) {
    if (vreg.isFrameIndex()) {
      // We skip one vreg for any frame index because there is a good chance
      // (especially when comparing SelectionDAG to GlobalISel generated MIR)
      // that in the other file we are just getting an incoming vreg that comes
      // from a copy from a frame index. So it's safe to skip by one.
      unsigned LastRenameReg = NVC.incrementVirtualVReg();
      (void)LastRenameReg;
      LLVM_DEBUG(dbgs() << "Skipping rename for FI " << LastRenameReg << "\n";);
      continue;
    } else if (vreg.isCandidate()) {

      // After the first candidate, for every subsequent candidate, we skip mod
      // 10 registers so that the candidates are more likely to start at the
      // same vreg number making it more likely that the canonical walk from the
      // candidate insruction. We don't need to skip from the first candidate of
      // the BasicBlock because we already skip ahead several vregs for each BB.
      unsigned LastRenameReg = NVC.getVirtualVReg();
      if (FirstCandidate)
        NVC.incrementVirtualVReg(LastRenameReg % 10);
      FirstCandidate = false;
      continue;
    } else if (!TargetRegisterInfo::isVirtualRegister(vreg.getReg())) {
      unsigned LastRenameReg = NVC.incrementVirtualVReg();
      (void)LastRenameReg;
      LLVM_DEBUG({
        dbgs() << "Skipping rename for Phys Reg " << LastRenameReg << "\n";
      });
      continue;
    }

    auto Reg = vreg.getReg();
    if (llvm::find(renamedInOtherBB, Reg) != renamedInOtherBB.end()) {
      LLVM_DEBUG(dbgs() << "Vreg " << Reg
                        << " already renamed in other BB.\n";);
      continue;
    }

    auto Rename = NVC.createVirtualRegister(MRI.getRegClass(Reg));

    if (VRegRenameMap.find(Reg) == VRegRenameMap.end()) {
      LLVM_DEBUG(dbgs() << "Mapping vreg ";);
      if (MRI.reg_begin(Reg) != MRI.reg_end()) {
        LLVM_DEBUG(auto foo = &*MRI.reg_begin(Reg); foo->dump(););
      } else {
        LLVM_DEBUG(dbgs() << Reg;);
      }
      LLVM_DEBUG(dbgs() << " to ";);
      if (MRI.reg_begin(Rename) != MRI.reg_end()) {
        LLVM_DEBUG(auto foo = &*MRI.reg_begin(Rename); foo->dump(););
      } else {
        LLVM_DEBUG(dbgs() << Rename;);
      }
      LLVM_DEBUG(dbgs() << "\n";);

      VRegRenameMap.insert(std::pair<unsigned, unsigned>(Reg, Rename));
    }
  }

  return VRegRenameMap;
}

static bool doVRegRenaming(std::vector<unsigned> &RenamedInOtherBB,
                           const std::map<unsigned, unsigned> &VRegRenameMap,
                           MachineRegisterInfo &MRI) {
  bool Changed = false;
  for (auto I = VRegRenameMap.begin(), E = VRegRenameMap.end(); I != E; ++I) {

    auto VReg = I->first;
    auto Rename = I->second;

    RenamedInOtherBB.push_back(Rename);

    std::vector<MachineOperand *> RenameMOs;
    for (auto &MO : MRI.reg_operands(VReg)) {
      RenameMOs.push_back(&MO);
    }

    for (auto *MO : RenameMOs) {
      Changed = true;
      MO->setReg(Rename);

      if (!MO->isDef())
        MO->setIsKill(false);
    }
  }

  return Changed;
}

static bool doDefKillClear(MachineBasicBlock *MBB) {
  bool Changed = false;

  for (auto &MI : *MBB) {
    for (auto &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      if (!MO.isDef() && MO.isKill()) {
        Changed = true;
        MO.setIsKill(false);
      }

      if (MO.isDef() && MO.isDead()) {
        Changed = true;
        MO.setIsDead(false);
      }
    }
  }

  return Changed;
}

static bool runOnBasicBlock(MachineBasicBlock *MBB,
                            std::vector<StringRef> &bbNames,
                            std::vector<unsigned> &renamedInOtherBB,
                            unsigned &basicBlockNum, unsigned &VRegGapIndex,
                            NamedVRegCursor &NVC) {

  if (CanonicalizeBasicBlockNumber != ~0U) {
    if (CanonicalizeBasicBlockNumber != basicBlockNum++)
      return false;
    LLVM_DEBUG(dbgs() << "\n Canonicalizing BasicBlock " << MBB->getName()
                      << "\n";);
  }

  if (llvm::find(bbNames, MBB->getName()) != bbNames.end()) {
    LLVM_DEBUG({
      dbgs() << "Found potentially duplicate BasicBlocks: " << MBB->getName()
             << "\n";
    });
    return false;
  }

  LLVM_DEBUG({
    dbgs() << "\n\n  NEW BASIC BLOCK: " << MBB->getName() << "  \n\n";
    dbgs() << "\n\n================================================\n\n";
  });

  bool Changed = false;
  MachineFunction &MF = *MBB->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  bbNames.push_back(MBB->getName());
  LLVM_DEBUG(dbgs() << "\n\n NEW BASIC BLOCK: " << MBB->getName() << "\n\n";);

  LLVM_DEBUG(dbgs() << "MBB Before Canonical Copy Propagation:\n";
             MBB->dump(););
  Changed |= propagateLocalCopies(MBB);
  LLVM_DEBUG(dbgs() << "MBB After Canonical Copy Propagation:\n"; MBB->dump(););

  LLVM_DEBUG(dbgs() << "MBB Before Scheduling:\n"; MBB->dump(););
  unsigned IdempotentInstCount = 0;
  Changed |= rescheduleCanonically(IdempotentInstCount, MBB);
  LLVM_DEBUG(dbgs() << "MBB After Scheduling:\n"; MBB->dump(););

  std::vector<MachineInstr *> Candidates = populateCandidates(MBB);
  std::vector<MachineInstr *> VisitedMIs;
  llvm::copy(Candidates, std::back_inserter(VisitedMIs));

  std::vector<TypedVReg> VRegs;
  for (auto candidate : Candidates) {
    VRegs.push_back(TypedVReg(RSE_NewCandidate));

    std::queue<TypedVReg> RegQueue;

    // Here we walk the vreg operands of a non-root node along our walk.
    // The root nodes are the original candidates (stores normally).
    // These are normally not the root nodes (except for the case of copies to
    // physical registers).
    for (unsigned i = 1; i < candidate->getNumOperands(); i++) {
      if (candidate->mayStore() || candidate->isBranch())
        break;

      MachineOperand &MO = candidate->getOperand(i);
      if (!(MO.isReg() && TargetRegisterInfo::isVirtualRegister(MO.getReg())))
        continue;

      LLVM_DEBUG(dbgs() << "Enqueue register"; MO.dump(); dbgs() << "\n";);
      RegQueue.push(TypedVReg(MO.getReg()));
    }

    // Here we walk the root candidates. We start from the 0th operand because
    // the root is normally a store to a vreg.
    for (unsigned i = 0; i < candidate->getNumOperands(); i++) {

      if (!candidate->mayStore() && !candidate->isBranch())
        break;

      MachineOperand &MO = candidate->getOperand(i);

      // TODO: Do we want to only add vregs here?
      if (!MO.isReg() && !MO.isFI())
        continue;

      LLVM_DEBUG(dbgs() << "Enqueue Reg/FI"; MO.dump(); dbgs() << "\n";);

      RegQueue.push(MO.isReg() ? TypedVReg(MO.getReg())
                               : TypedVReg(RSE_FrameIndex));
    }

    doCandidateWalk(VRegs, RegQueue, VisitedMIs, MBB);
  }

  // If we have populated no vregs to rename then bail.
  // The rest of this function does the vreg remaping.
  if (VRegs.size() == 0)
    return Changed;

  auto VRegRenameMap = GetVRegRenameMap(VRegs, renamedInOtherBB, MRI, NVC);
  Changed |= doVRegRenaming(renamedInOtherBB, VRegRenameMap, MRI);

  // Here we renumber the def vregs for the idempotent instructions from the top
  // of the MachineBasicBlock so that they are named in the order that we sorted
  // them alphabetically. Eventually we wont need SkipVRegs because we will use
  // named vregs instead.
  NVC.SkipVRegs();

  auto MII = MBB->begin();
  for (unsigned i = 0; i < IdempotentInstCount && MII != MBB->end(); ++i) {
    MachineInstr &MI = *MII++;
    Changed = true;
    unsigned vRegToRename = MI.getOperand(0).getReg();
    auto Rename = NVC.createVirtualRegister(MRI.getRegClass(vRegToRename));

    std::vector<MachineOperand *> RenameMOs;
    for (auto &MO : MRI.reg_operands(vRegToRename)) {
      RenameMOs.push_back(&MO);
    }

    for (auto *MO : RenameMOs) {
      MO->setReg(Rename);
    }
  }

  Changed |= doDefKillClear(MBB);

  LLVM_DEBUG(dbgs() << "Updated MachineBasicBlock:\n"; MBB->dump();
             dbgs() << "\n";);
  LLVM_DEBUG(
      dbgs() << "\n\n================================================\n\n");
  return Changed;
}

bool MIRCanonicalizer::runOnMachineFunction(MachineFunction &MF) {

  static unsigned functionNum = 0;
  if (CanonicalizeFunctionNumber != ~0U) {
    if (CanonicalizeFunctionNumber != functionNum++)
      return false;
    LLVM_DEBUG(dbgs() << "\n Canonicalizing Function " << MF.getName()
                      << "\n";);
  }

  // we need a valid vreg to create a vreg type for skipping all those
  // stray vreg numbers so reach alignment/canonical vreg values.
  std::vector<MachineBasicBlock *> RPOList = GetRPOList(MF);

  LLVM_DEBUG(
      dbgs() << "\n\n  NEW MACHINE FUNCTION: " << MF.getName() << "  \n\n";
      dbgs() << "\n\n================================================\n\n";
      dbgs() << "Total Basic Blocks: " << RPOList.size() << "\n";
      for (auto MBB
           : RPOList) { dbgs() << MBB->getName() << "\n"; } dbgs()
      << "\n\n================================================\n\n";);

  std::vector<StringRef> BBNames;
  std::vector<unsigned> RenamedInOtherBB;

  unsigned GapIdx = 0;
  unsigned BBNum = 0;

  bool Changed = false;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  NamedVRegCursor NVC(MRI);
  for (auto MBB : RPOList)
    Changed |=
        runOnBasicBlock(MBB, BBNames, RenamedInOtherBB, BBNum, GapIdx, NVC);

  return Changed;
}
