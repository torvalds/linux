//===- AMDGPUMachineCFGStructurizer.cpp - Machine code if conversion pass. ===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the machine instruction level CFG structurizer pass.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegionInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "amdgpucfgstructurizer"

namespace {

class PHILinearizeDestIterator;

class PHILinearize {
  friend class PHILinearizeDestIterator;

public:
  using PHISourceT = std::pair<unsigned, MachineBasicBlock *>;

private:
  using PHISourcesT = DenseSet<PHISourceT>;
  using PHIInfoElementT = struct {
    unsigned DestReg;
    DebugLoc DL;
    PHISourcesT Sources;
  };
  using PHIInfoT = SmallPtrSet<PHIInfoElementT *, 2>;
  PHIInfoT PHIInfo;

  static unsigned phiInfoElementGetDest(PHIInfoElementT *Info);
  static void phiInfoElementSetDef(PHIInfoElementT *Info, unsigned NewDef);
  static PHISourcesT &phiInfoElementGetSources(PHIInfoElementT *Info);
  static void phiInfoElementAddSource(PHIInfoElementT *Info, unsigned SourceReg,
                                      MachineBasicBlock *SourceMBB);
  static void phiInfoElementRemoveSource(PHIInfoElementT *Info,
                                         unsigned SourceReg,
                                         MachineBasicBlock *SourceMBB);
  PHIInfoElementT *findPHIInfoElement(unsigned DestReg);
  PHIInfoElementT *findPHIInfoElementFromSource(unsigned SourceReg,
                                                MachineBasicBlock *SourceMBB);

public:
  bool findSourcesFromMBB(MachineBasicBlock *SourceMBB,
                          SmallVector<unsigned, 4> &Sources);
  void addDest(unsigned DestReg, const DebugLoc &DL);
  void replaceDef(unsigned OldDestReg, unsigned NewDestReg);
  void deleteDef(unsigned DestReg);
  void addSource(unsigned DestReg, unsigned SourceReg,
                 MachineBasicBlock *SourceMBB);
  void removeSource(unsigned DestReg, unsigned SourceReg,
                    MachineBasicBlock *SourceMBB = nullptr);
  bool findDest(unsigned SourceReg, MachineBasicBlock *SourceMBB,
                unsigned &DestReg);
  bool isSource(unsigned Reg, MachineBasicBlock *SourceMBB = nullptr);
  unsigned getNumSources(unsigned DestReg);
  void dump(MachineRegisterInfo *MRI);
  void clear();

  using source_iterator = PHISourcesT::iterator;
  using dest_iterator = PHILinearizeDestIterator;

  dest_iterator dests_begin();
  dest_iterator dests_end();

  source_iterator sources_begin(unsigned Reg);
  source_iterator sources_end(unsigned Reg);
};

class PHILinearizeDestIterator {
private:
  PHILinearize::PHIInfoT::iterator Iter;

public:
  PHILinearizeDestIterator(PHILinearize::PHIInfoT::iterator I) : Iter(I) {}

  unsigned operator*() { return PHILinearize::phiInfoElementGetDest(*Iter); }
  PHILinearizeDestIterator &operator++() {
    ++Iter;
    return *this;
  }
  bool operator==(const PHILinearizeDestIterator &I) const {
    return I.Iter == Iter;
  }
  bool operator!=(const PHILinearizeDestIterator &I) const {
    return I.Iter != Iter;
  }
};

} // end anonymous namespace

unsigned PHILinearize::phiInfoElementGetDest(PHIInfoElementT *Info) {
  return Info->DestReg;
}

void PHILinearize::phiInfoElementSetDef(PHIInfoElementT *Info,
                                        unsigned NewDef) {
  Info->DestReg = NewDef;
}

PHILinearize::PHISourcesT &
PHILinearize::phiInfoElementGetSources(PHIInfoElementT *Info) {
  return Info->Sources;
}

void PHILinearize::phiInfoElementAddSource(PHIInfoElementT *Info,
                                           unsigned SourceReg,
                                           MachineBasicBlock *SourceMBB) {
  // Assertion ensures we don't use the same SourceMBB for the
  // sources, because we cannot have different registers with
  // identical predecessors, but we can have the same register for
  // multiple predecessors.
#if !defined(NDEBUG)
  for (auto SI : phiInfoElementGetSources(Info)) {
    assert((SI.second != SourceMBB || SourceReg == SI.first));
  }
#endif

  phiInfoElementGetSources(Info).insert(PHISourceT(SourceReg, SourceMBB));
}

void PHILinearize::phiInfoElementRemoveSource(PHIInfoElementT *Info,
                                              unsigned SourceReg,
                                              MachineBasicBlock *SourceMBB) {
  auto &Sources = phiInfoElementGetSources(Info);
  SmallVector<PHISourceT, 4> ElimiatedSources;
  for (auto SI : Sources) {
    if (SI.first == SourceReg &&
        (SI.second == nullptr || SI.second == SourceMBB)) {
      ElimiatedSources.push_back(PHISourceT(SI.first, SI.second));
    }
  }

  for (auto &Source : ElimiatedSources) {
    Sources.erase(Source);
  }
}

PHILinearize::PHIInfoElementT *
PHILinearize::findPHIInfoElement(unsigned DestReg) {
  for (auto I : PHIInfo) {
    if (phiInfoElementGetDest(I) == DestReg) {
      return I;
    }
  }
  return nullptr;
}

PHILinearize::PHIInfoElementT *
PHILinearize::findPHIInfoElementFromSource(unsigned SourceReg,
                                           MachineBasicBlock *SourceMBB) {
  for (auto I : PHIInfo) {
    for (auto SI : phiInfoElementGetSources(I)) {
      if (SI.first == SourceReg &&
          (SI.second == nullptr || SI.second == SourceMBB)) {
        return I;
      }
    }
  }
  return nullptr;
}

bool PHILinearize::findSourcesFromMBB(MachineBasicBlock *SourceMBB,
                                      SmallVector<unsigned, 4> &Sources) {
  bool FoundSource = false;
  for (auto I : PHIInfo) {
    for (auto SI : phiInfoElementGetSources(I)) {
      if (SI.second == SourceMBB) {
        FoundSource = true;
        Sources.push_back(SI.first);
      }
    }
  }
  return FoundSource;
}

void PHILinearize::addDest(unsigned DestReg, const DebugLoc &DL) {
  assert(findPHIInfoElement(DestReg) == nullptr && "Dest already exsists");
  PHISourcesT EmptySet;
  PHIInfoElementT *NewElement = new PHIInfoElementT();
  NewElement->DestReg = DestReg;
  NewElement->DL = DL;
  NewElement->Sources = EmptySet;
  PHIInfo.insert(NewElement);
}

void PHILinearize::replaceDef(unsigned OldDestReg, unsigned NewDestReg) {
  phiInfoElementSetDef(findPHIInfoElement(OldDestReg), NewDestReg);
}

void PHILinearize::deleteDef(unsigned DestReg) {
  PHIInfoElementT *InfoElement = findPHIInfoElement(DestReg);
  PHIInfo.erase(InfoElement);
  delete InfoElement;
}

void PHILinearize::addSource(unsigned DestReg, unsigned SourceReg,
                             MachineBasicBlock *SourceMBB) {
  phiInfoElementAddSource(findPHIInfoElement(DestReg), SourceReg, SourceMBB);
}

void PHILinearize::removeSource(unsigned DestReg, unsigned SourceReg,
                                MachineBasicBlock *SourceMBB) {
  phiInfoElementRemoveSource(findPHIInfoElement(DestReg), SourceReg, SourceMBB);
}

bool PHILinearize::findDest(unsigned SourceReg, MachineBasicBlock *SourceMBB,
                            unsigned &DestReg) {
  PHIInfoElementT *InfoElement =
      findPHIInfoElementFromSource(SourceReg, SourceMBB);
  if (InfoElement != nullptr) {
    DestReg = phiInfoElementGetDest(InfoElement);
    return true;
  }
  return false;
}

bool PHILinearize::isSource(unsigned Reg, MachineBasicBlock *SourceMBB) {
  unsigned DestReg;
  return findDest(Reg, SourceMBB, DestReg);
}

unsigned PHILinearize::getNumSources(unsigned DestReg) {
  return phiInfoElementGetSources(findPHIInfoElement(DestReg)).size();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void PHILinearize::dump(MachineRegisterInfo *MRI) {
  const TargetRegisterInfo *TRI = MRI->getTargetRegisterInfo();
  dbgs() << "=PHIInfo Start=\n";
  for (auto PII : this->PHIInfo) {
    PHIInfoElementT &Element = *PII;
    dbgs() << "Dest: " << printReg(Element.DestReg, TRI)
           << " Sources: {";
    for (auto &SI : Element.Sources) {
      dbgs() << printReg(SI.first, TRI) << '(' << printMBBReference(*SI.second)
             << "),";
    }
    dbgs() << "}\n";
  }
  dbgs() << "=PHIInfo End=\n";
}
#endif

void PHILinearize::clear() { PHIInfo = PHIInfoT(); }

PHILinearize::dest_iterator PHILinearize::dests_begin() {
  return PHILinearizeDestIterator(PHIInfo.begin());
}

PHILinearize::dest_iterator PHILinearize::dests_end() {
  return PHILinearizeDestIterator(PHIInfo.end());
}

PHILinearize::source_iterator PHILinearize::sources_begin(unsigned Reg) {
  auto InfoElement = findPHIInfoElement(Reg);
  return phiInfoElementGetSources(InfoElement).begin();
}

PHILinearize::source_iterator PHILinearize::sources_end(unsigned Reg) {
  auto InfoElement = findPHIInfoElement(Reg);
  return phiInfoElementGetSources(InfoElement).end();
}

static unsigned getPHINumInputs(MachineInstr &PHI) {
  assert(PHI.isPHI());
  return (PHI.getNumOperands() - 1) / 2;
}

static MachineBasicBlock *getPHIPred(MachineInstr &PHI, unsigned Index) {
  assert(PHI.isPHI());
  return PHI.getOperand(Index * 2 + 2).getMBB();
}

static void setPhiPred(MachineInstr &PHI, unsigned Index,
                       MachineBasicBlock *NewPred) {
  PHI.getOperand(Index * 2 + 2).setMBB(NewPred);
}

static unsigned getPHISourceReg(MachineInstr &PHI, unsigned Index) {
  assert(PHI.isPHI());
  return PHI.getOperand(Index * 2 + 1).getReg();
}

static unsigned getPHIDestReg(MachineInstr &PHI) {
  assert(PHI.isPHI());
  return PHI.getOperand(0).getReg();
}

namespace {

class RegionMRT;
class MBBMRT;

class LinearizedRegion {
protected:
  MachineBasicBlock *Entry;
  // The exit block is part of the region, and is the last
  // merge block before exiting the region.
  MachineBasicBlock *Exit;
  DenseSet<unsigned> LiveOuts;
  SmallPtrSet<MachineBasicBlock *, 1> MBBs;
  bool HasLoop;
  LinearizedRegion *Parent;
  RegionMRT *RMRT;

  void storeLiveOutReg(MachineBasicBlock *MBB, unsigned Reg,
                       MachineInstr *DefInstr, const MachineRegisterInfo *MRI,
                       const TargetRegisterInfo *TRI, PHILinearize &PHIInfo);

  void storeLiveOutRegRegion(RegionMRT *Region, unsigned Reg,
                             MachineInstr *DefInstr,
                             const MachineRegisterInfo *MRI,
                             const TargetRegisterInfo *TRI,
                             PHILinearize &PHIInfo);

  void storeMBBLiveOuts(MachineBasicBlock *MBB, const MachineRegisterInfo *MRI,
                        const TargetRegisterInfo *TRI, PHILinearize &PHIInfo,
                        RegionMRT *TopRegion);

  void storeLiveOuts(MachineBasicBlock *MBB, const MachineRegisterInfo *MRI,
                     const TargetRegisterInfo *TRI, PHILinearize &PHIInfo);

  void storeLiveOuts(RegionMRT *Region, const MachineRegisterInfo *MRI,
                     const TargetRegisterInfo *TRI, PHILinearize &PHIInfo,
                     RegionMRT *TopRegion = nullptr);

public:
  LinearizedRegion();
  LinearizedRegion(MachineBasicBlock *MBB, const MachineRegisterInfo *MRI,
                   const TargetRegisterInfo *TRI, PHILinearize &PHIInfo);
  ~LinearizedRegion() = default;

  void setRegionMRT(RegionMRT *Region) { RMRT = Region; }

  RegionMRT *getRegionMRT() { return RMRT; }

  void setParent(LinearizedRegion *P) { Parent = P; }

  LinearizedRegion *getParent() { return Parent; }

  void print(raw_ostream &OS, const TargetRegisterInfo *TRI = nullptr);

  void setBBSelectRegIn(unsigned Reg);

  unsigned getBBSelectRegIn();

  void setBBSelectRegOut(unsigned Reg, bool IsLiveOut);

  unsigned getBBSelectRegOut();

  void setHasLoop(bool Value);

  bool getHasLoop();

  void addLiveOut(unsigned VReg);

  void removeLiveOut(unsigned Reg);

  void replaceLiveOut(unsigned OldReg, unsigned NewReg);

  void replaceRegister(unsigned Register, unsigned NewRegister,
                       MachineRegisterInfo *MRI, bool ReplaceInside,
                       bool ReplaceOutside, bool IncludeLoopPHIs);

  void replaceRegisterInsideRegion(unsigned Register, unsigned NewRegister,
                                   bool IncludeLoopPHIs,
                                   MachineRegisterInfo *MRI);

  void replaceRegisterOutsideRegion(unsigned Register, unsigned NewRegister,
                                    bool IncludeLoopPHIs,
                                    MachineRegisterInfo *MRI);

  DenseSet<unsigned> *getLiveOuts();

  void setEntry(MachineBasicBlock *NewEntry);

  MachineBasicBlock *getEntry();

  void setExit(MachineBasicBlock *NewExit);

  MachineBasicBlock *getExit();

  void addMBB(MachineBasicBlock *MBB);

  void addMBBs(LinearizedRegion *InnerRegion);

  bool contains(MachineBasicBlock *MBB);

  bool isLiveOut(unsigned Reg);

  bool hasNoDef(unsigned Reg, MachineRegisterInfo *MRI);

  void removeFalseRegisterKills(MachineRegisterInfo *MRI);

  void initLiveOut(RegionMRT *Region, const MachineRegisterInfo *MRI,
                   const TargetRegisterInfo *TRI, PHILinearize &PHIInfo);
};

class MRT {
protected:
  RegionMRT *Parent;
  unsigned BBSelectRegIn;
  unsigned BBSelectRegOut;

public:
  virtual ~MRT() = default;

  unsigned getBBSelectRegIn() { return BBSelectRegIn; }

  unsigned getBBSelectRegOut() { return BBSelectRegOut; }

  void setBBSelectRegIn(unsigned Reg) { BBSelectRegIn = Reg; }

  void setBBSelectRegOut(unsigned Reg) { BBSelectRegOut = Reg; }

  virtual RegionMRT *getRegionMRT() { return nullptr; }

  virtual MBBMRT *getMBBMRT() { return nullptr; }

  bool isRegion() { return getRegionMRT() != nullptr; }

  bool isMBB() { return getMBBMRT() != nullptr; }

  bool isRoot() { return Parent == nullptr; }

  void setParent(RegionMRT *Region) { Parent = Region; }

  RegionMRT *getParent() { return Parent; }

  static MachineBasicBlock *
  initializeMRT(MachineFunction &MF, const MachineRegionInfo *RegionInfo,
                DenseMap<MachineRegion *, RegionMRT *> &RegionMap);

  static RegionMRT *buildMRT(MachineFunction &MF,
                             const MachineRegionInfo *RegionInfo,
                             const SIInstrInfo *TII,
                             MachineRegisterInfo *MRI);

  virtual void dump(const TargetRegisterInfo *TRI, int depth = 0) = 0;

  void dumpDepth(int depth) {
    for (int i = depth; i > 0; --i) {
      dbgs() << "  ";
    }
  }
};

class MBBMRT : public MRT {
  MachineBasicBlock *MBB;

public:
  MBBMRT(MachineBasicBlock *BB) : MBB(BB) {
    setParent(nullptr);
    setBBSelectRegOut(0);
    setBBSelectRegIn(0);
  }

  MBBMRT *getMBBMRT() override { return this; }

  MachineBasicBlock *getMBB() { return MBB; }

  void dump(const TargetRegisterInfo *TRI, int depth = 0) override {
    dumpDepth(depth);
    dbgs() << "MBB: " << getMBB()->getNumber();
    dbgs() << " In: " << printReg(getBBSelectRegIn(), TRI);
    dbgs() << ", Out: " << printReg(getBBSelectRegOut(), TRI) << "\n";
  }
};

class RegionMRT : public MRT {
protected:
  MachineRegion *Region;
  LinearizedRegion *LRegion = nullptr;
  MachineBasicBlock *Succ = nullptr;
  SetVector<MRT *> Children;

public:
  RegionMRT(MachineRegion *MachineRegion) : Region(MachineRegion) {
    setParent(nullptr);
    setBBSelectRegOut(0);
    setBBSelectRegIn(0);
  }

  ~RegionMRT() override {
    if (LRegion) {
      delete LRegion;
    }

    for (auto CI : Children) {
      delete &(*CI);
    }
  }

  RegionMRT *getRegionMRT() override { return this; }

  void setLinearizedRegion(LinearizedRegion *LinearizeRegion) {
    LRegion = LinearizeRegion;
  }

  LinearizedRegion *getLinearizedRegion() { return LRegion; }

  MachineRegion *getMachineRegion() { return Region; }

  unsigned getInnerOutputRegister() {
    return (*(Children.begin()))->getBBSelectRegOut();
  }

  void addChild(MRT *Tree) { Children.insert(Tree); }

  SetVector<MRT *> *getChildren() { return &Children; }

  void dump(const TargetRegisterInfo *TRI, int depth = 0) override {
    dumpDepth(depth);
    dbgs() << "Region: " << (void *)Region;
    dbgs() << " In: " << printReg(getBBSelectRegIn(), TRI);
    dbgs() << ", Out: " << printReg(getBBSelectRegOut(), TRI) << "\n";

    dumpDepth(depth);
    if (getSucc())
      dbgs() << "Succ: " << getSucc()->getNumber() << "\n";
    else
      dbgs() << "Succ: none \n";
    for (auto MRTI : Children) {
      MRTI->dump(TRI, depth + 1);
    }
  }

  MRT *getEntryTree() { return Children.back(); }

  MRT *getExitTree() { return Children.front(); }

  MachineBasicBlock *getEntry() {
    MRT *Tree = Children.back();
    return (Tree->isRegion()) ? Tree->getRegionMRT()->getEntry()
                              : Tree->getMBBMRT()->getMBB();
  }

  MachineBasicBlock *getExit() {
    MRT *Tree = Children.front();
    return (Tree->isRegion()) ? Tree->getRegionMRT()->getExit()
                              : Tree->getMBBMRT()->getMBB();
  }

  void setSucc(MachineBasicBlock *MBB) { Succ = MBB; }

  MachineBasicBlock *getSucc() { return Succ; }

  bool contains(MachineBasicBlock *MBB) {
    for (auto CI : Children) {
      if (CI->isMBB()) {
        if (MBB == CI->getMBBMRT()->getMBB()) {
          return true;
        }
      } else {
        if (CI->getRegionMRT()->contains(MBB)) {
          return true;
        } else if (CI->getRegionMRT()->getLinearizedRegion() != nullptr &&
                   CI->getRegionMRT()->getLinearizedRegion()->contains(MBB)) {
          return true;
        }
      }
    }
    return false;
  }

  void replaceLiveOutReg(unsigned Register, unsigned NewRegister) {
    LinearizedRegion *LRegion = getLinearizedRegion();
    LRegion->replaceLiveOut(Register, NewRegister);
    for (auto &CI : Children) {
      if (CI->isRegion()) {
        CI->getRegionMRT()->replaceLiveOutReg(Register, NewRegister);
      }
    }
  }
};

} // end anonymous namespace

static unsigned createBBSelectReg(const SIInstrInfo *TII,
                                  MachineRegisterInfo *MRI) {
  return MRI->createVirtualRegister(TII->getPreferredSelectRegClass(32));
}

MachineBasicBlock *
MRT::initializeMRT(MachineFunction &MF, const MachineRegionInfo *RegionInfo,
                   DenseMap<MachineRegion *, RegionMRT *> &RegionMap) {
  for (auto &MFI : MF) {
    MachineBasicBlock *ExitMBB = &MFI;
    if (ExitMBB->succ_size() == 0) {
      return ExitMBB;
    }
  }
  llvm_unreachable("CFG has no exit block");
  return nullptr;
}

RegionMRT *MRT::buildMRT(MachineFunction &MF,
                         const MachineRegionInfo *RegionInfo,
                         const SIInstrInfo *TII, MachineRegisterInfo *MRI) {
  SmallPtrSet<MachineRegion *, 4> PlacedRegions;
  DenseMap<MachineRegion *, RegionMRT *> RegionMap;
  MachineRegion *TopLevelRegion = RegionInfo->getTopLevelRegion();
  RegionMRT *Result = new RegionMRT(TopLevelRegion);
  RegionMap[TopLevelRegion] = Result;

  // Insert the exit block first, we need it to be the merge node
  // for the top level region.
  MachineBasicBlock *Exit = initializeMRT(MF, RegionInfo, RegionMap);

  unsigned BBSelectRegIn = createBBSelectReg(TII, MRI);
  MBBMRT *ExitMRT = new MBBMRT(Exit);
  RegionMap[RegionInfo->getRegionFor(Exit)]->addChild(ExitMRT);
  ExitMRT->setBBSelectRegIn(BBSelectRegIn);

  for (auto MBBI : post_order(&(MF.front()))) {
    MachineBasicBlock *MBB = &(*MBBI);

    // Skip Exit since we already added it
    if (MBB == Exit) {
      continue;
    }

    LLVM_DEBUG(dbgs() << "Visiting " << printMBBReference(*MBB) << "\n");
    MBBMRT *NewMBB = new MBBMRT(MBB);
    MachineRegion *Region = RegionInfo->getRegionFor(MBB);

    // Ensure we have the MRT region
    if (RegionMap.count(Region) == 0) {
      RegionMRT *NewMRTRegion = new RegionMRT(Region);
      RegionMap[Region] = NewMRTRegion;

      // Ensure all parents are in the RegionMap
      MachineRegion *Parent = Region->getParent();
      while (RegionMap.count(Parent) == 0) {
        RegionMRT *NewMRTParent = new RegionMRT(Parent);
        NewMRTParent->addChild(NewMRTRegion);
        NewMRTRegion->setParent(NewMRTParent);
        RegionMap[Parent] = NewMRTParent;
        NewMRTRegion = NewMRTParent;
        Parent = Parent->getParent();
      }
      RegionMap[Parent]->addChild(NewMRTRegion);
      NewMRTRegion->setParent(RegionMap[Parent]);
    }

    // Add MBB to Region MRT
    RegionMap[Region]->addChild(NewMBB);
    NewMBB->setParent(RegionMap[Region]);
    RegionMap[Region]->setSucc(Region->getExit());
  }
  return Result;
}

void LinearizedRegion::storeLiveOutReg(MachineBasicBlock *MBB, unsigned Reg,
                                       MachineInstr *DefInstr,
                                       const MachineRegisterInfo *MRI,
                                       const TargetRegisterInfo *TRI,
                                       PHILinearize &PHIInfo) {
  if (TRI->isVirtualRegister(Reg)) {
    LLVM_DEBUG(dbgs() << "Considering Register: " << printReg(Reg, TRI)
                      << "\n");
    // If this is a source register to a PHI we are chaining, it
    // must be live out.
    if (PHIInfo.isSource(Reg)) {
      LLVM_DEBUG(dbgs() << "Add LiveOut (PHI): " << printReg(Reg, TRI) << "\n");
      addLiveOut(Reg);
    } else {
      // If this is live out of the MBB
      for (auto &UI : MRI->use_operands(Reg)) {
        if (UI.getParent()->getParent() != MBB) {
          LLVM_DEBUG(dbgs() << "Add LiveOut (MBB " << printMBBReference(*MBB)
                            << "): " << printReg(Reg, TRI) << "\n");
          addLiveOut(Reg);
        } else {
          // If the use is in the same MBB we have to make sure
          // it is after the def, otherwise it is live out in a loop
          MachineInstr *UseInstr = UI.getParent();
          for (MachineBasicBlock::instr_iterator
                   MII = UseInstr->getIterator(),
                   MIE = UseInstr->getParent()->instr_end();
               MII != MIE; ++MII) {
            if ((&(*MII)) == DefInstr) {
              LLVM_DEBUG(dbgs() << "Add LiveOut (Loop): " << printReg(Reg, TRI)
                                << "\n");
              addLiveOut(Reg);
            }
          }
        }
      }
    }
  }
}

void LinearizedRegion::storeLiveOutRegRegion(RegionMRT *Region, unsigned Reg,
                                             MachineInstr *DefInstr,
                                             const MachineRegisterInfo *MRI,
                                             const TargetRegisterInfo *TRI,
                                             PHILinearize &PHIInfo) {
  if (TRI->isVirtualRegister(Reg)) {
    LLVM_DEBUG(dbgs() << "Considering Register: " << printReg(Reg, TRI)
                      << "\n");
    for (auto &UI : MRI->use_operands(Reg)) {
      if (!Region->contains(UI.getParent()->getParent())) {
        LLVM_DEBUG(dbgs() << "Add LiveOut (Region " << (void *)Region
                          << "): " << printReg(Reg, TRI) << "\n");
        addLiveOut(Reg);
      }
    }
  }
}

void LinearizedRegion::storeLiveOuts(MachineBasicBlock *MBB,
                                     const MachineRegisterInfo *MRI,
                                     const TargetRegisterInfo *TRI,
                                     PHILinearize &PHIInfo) {
  LLVM_DEBUG(dbgs() << "-Store Live Outs Begin (" << printMBBReference(*MBB)
                    << ")-\n");
  for (auto &II : *MBB) {
    for (auto &RI : II.defs()) {
      storeLiveOutReg(MBB, RI.getReg(), RI.getParent(), MRI, TRI, PHIInfo);
    }
    for (auto &IRI : II.implicit_operands()) {
      if (IRI.isDef()) {
        storeLiveOutReg(MBB, IRI.getReg(), IRI.getParent(), MRI, TRI, PHIInfo);
      }
    }
  }

  // If we have a successor with a PHI, source coming from this MBB we have to
  // add the register as live out
  for (MachineBasicBlock::succ_iterator SI = MBB->succ_begin(),
                                        E = MBB->succ_end();
       SI != E; ++SI) {
    for (auto &II : *(*SI)) {
      if (II.isPHI()) {
        MachineInstr &PHI = II;
        int numPreds = getPHINumInputs(PHI);
        for (int i = 0; i < numPreds; ++i) {
          if (getPHIPred(PHI, i) == MBB) {
            unsigned PHIReg = getPHISourceReg(PHI, i);
            LLVM_DEBUG(dbgs()
                       << "Add LiveOut (PhiSource " << printMBBReference(*MBB)
                       << " -> " << printMBBReference(*(*SI))
                       << "): " << printReg(PHIReg, TRI) << "\n");
            addLiveOut(PHIReg);
          }
        }
      }
    }
  }

  LLVM_DEBUG(dbgs() << "-Store Live Outs Endn-\n");
}

void LinearizedRegion::storeMBBLiveOuts(MachineBasicBlock *MBB,
                                        const MachineRegisterInfo *MRI,
                                        const TargetRegisterInfo *TRI,
                                        PHILinearize &PHIInfo,
                                        RegionMRT *TopRegion) {
  for (auto &II : *MBB) {
    for (auto &RI : II.defs()) {
      storeLiveOutRegRegion(TopRegion, RI.getReg(), RI.getParent(), MRI, TRI,
                            PHIInfo);
    }
    for (auto &IRI : II.implicit_operands()) {
      if (IRI.isDef()) {
        storeLiveOutRegRegion(TopRegion, IRI.getReg(), IRI.getParent(), MRI,
                              TRI, PHIInfo);
      }
    }
  }
}

void LinearizedRegion::storeLiveOuts(RegionMRT *Region,
                                     const MachineRegisterInfo *MRI,
                                     const TargetRegisterInfo *TRI,
                                     PHILinearize &PHIInfo,
                                     RegionMRT *CurrentTopRegion) {
  MachineBasicBlock *Exit = Region->getSucc();

  RegionMRT *TopRegion =
      CurrentTopRegion == nullptr ? Region : CurrentTopRegion;

  // Check if exit is end of function, if so, no live outs.
  if (Exit == nullptr)
    return;

  auto Children = Region->getChildren();
  for (auto CI : *Children) {
    if (CI->isMBB()) {
      auto MBB = CI->getMBBMRT()->getMBB();
      storeMBBLiveOuts(MBB, MRI, TRI, PHIInfo, TopRegion);
    } else {
      LinearizedRegion *SubRegion = CI->getRegionMRT()->getLinearizedRegion();
      // We should be limited to only store registers that are live out from the
      // lineaized region
      for (auto MBBI : SubRegion->MBBs) {
        storeMBBLiveOuts(MBBI, MRI, TRI, PHIInfo, TopRegion);
      }
    }
  }

  if (CurrentTopRegion == nullptr) {
    auto Succ = Region->getSucc();
    for (auto &II : *Succ) {
      if (II.isPHI()) {
        MachineInstr &PHI = II;
        int numPreds = getPHINumInputs(PHI);
        for (int i = 0; i < numPreds; ++i) {
          if (Region->contains(getPHIPred(PHI, i))) {
            unsigned PHIReg = getPHISourceReg(PHI, i);
            LLVM_DEBUG(dbgs() << "Add Region LiveOut (" << (void *)Region
                              << "): " << printReg(PHIReg, TRI) << "\n");
            addLiveOut(PHIReg);
          }
        }
      }
    }
  }
}

#ifndef NDEBUG
void LinearizedRegion::print(raw_ostream &OS, const TargetRegisterInfo *TRI) {
  OS << "Linearized Region {";
  bool IsFirst = true;
  for (const auto &MBB : MBBs) {
    if (IsFirst) {
      IsFirst = false;
    } else {
      OS << " ,";
    }
    OS << MBB->getNumber();
  }
  OS << "} (" << Entry->getNumber() << ", "
     << (Exit == nullptr ? -1 : Exit->getNumber())
     << "): In:" << printReg(getBBSelectRegIn(), TRI)
     << " Out:" << printReg(getBBSelectRegOut(), TRI) << " {";
  for (auto &LI : LiveOuts) {
    OS << printReg(LI, TRI) << " ";
  }
  OS << "} \n";
}
#endif

unsigned LinearizedRegion::getBBSelectRegIn() {
  return getRegionMRT()->getBBSelectRegIn();
}

unsigned LinearizedRegion::getBBSelectRegOut() {
  return getRegionMRT()->getBBSelectRegOut();
}

void LinearizedRegion::setHasLoop(bool Value) { HasLoop = Value; }

bool LinearizedRegion::getHasLoop() { return HasLoop; }

void LinearizedRegion::addLiveOut(unsigned VReg) { LiveOuts.insert(VReg); }

void LinearizedRegion::removeLiveOut(unsigned Reg) {
  if (isLiveOut(Reg))
    LiveOuts.erase(Reg);
}

void LinearizedRegion::replaceLiveOut(unsigned OldReg, unsigned NewReg) {
  if (isLiveOut(OldReg)) {
    removeLiveOut(OldReg);
    addLiveOut(NewReg);
  }
}

void LinearizedRegion::replaceRegister(unsigned Register, unsigned NewRegister,
                                       MachineRegisterInfo *MRI,
                                       bool ReplaceInside, bool ReplaceOutside,
                                       bool IncludeLoopPHI) {
  assert(Register != NewRegister && "Cannot replace a reg with itself");

  LLVM_DEBUG(
      dbgs() << "Pepareing to replace register (region): "
             << printReg(Register, MRI->getTargetRegisterInfo()) << " with "
             << printReg(NewRegister, MRI->getTargetRegisterInfo()) << "\n");

  // If we are replacing outside, we also need to update the LiveOuts
  if (ReplaceOutside &&
      (isLiveOut(Register) || this->getParent()->isLiveOut(Register))) {
    LinearizedRegion *Current = this;
    while (Current != nullptr && Current->getEntry() != nullptr) {
      LLVM_DEBUG(dbgs() << "Region before register replace\n");
      LLVM_DEBUG(Current->print(dbgs(), MRI->getTargetRegisterInfo()));
      Current->replaceLiveOut(Register, NewRegister);
      LLVM_DEBUG(dbgs() << "Region after register replace\n");
      LLVM_DEBUG(Current->print(dbgs(), MRI->getTargetRegisterInfo()));
      Current = Current->getParent();
    }
  }

  for (MachineRegisterInfo::reg_iterator I = MRI->reg_begin(Register),
                                         E = MRI->reg_end();
       I != E;) {
    MachineOperand &O = *I;
    ++I;

    // We don't rewrite defs.
    if (O.isDef())
      continue;

    bool IsInside = contains(O.getParent()->getParent());
    bool IsLoopPHI = IsInside && (O.getParent()->isPHI() &&
                                  O.getParent()->getParent() == getEntry());
    bool ShouldReplace = (IsInside && ReplaceInside) ||
                         (!IsInside && ReplaceOutside) ||
                         (IncludeLoopPHI && IsLoopPHI);
    if (ShouldReplace) {

      if (TargetRegisterInfo::isPhysicalRegister(NewRegister)) {
        LLVM_DEBUG(dbgs() << "Trying to substitute physical register: "
                          << printReg(NewRegister, MRI->getTargetRegisterInfo())
                          << "\n");
        llvm_unreachable("Cannot substitute physical registers");
      } else {
        LLVM_DEBUG(dbgs() << "Replacing register (region): "
                          << printReg(Register, MRI->getTargetRegisterInfo())
                          << " with "
                          << printReg(NewRegister, MRI->getTargetRegisterInfo())
                          << "\n");
        O.setReg(NewRegister);
      }
    }
  }
}

void LinearizedRegion::replaceRegisterInsideRegion(unsigned Register,
                                                   unsigned NewRegister,
                                                   bool IncludeLoopPHIs,
                                                   MachineRegisterInfo *MRI) {
  replaceRegister(Register, NewRegister, MRI, true, false, IncludeLoopPHIs);
}

void LinearizedRegion::replaceRegisterOutsideRegion(unsigned Register,
                                                    unsigned NewRegister,
                                                    bool IncludeLoopPHIs,
                                                    MachineRegisterInfo *MRI) {
  replaceRegister(Register, NewRegister, MRI, false, true, IncludeLoopPHIs);
}

DenseSet<unsigned> *LinearizedRegion::getLiveOuts() { return &LiveOuts; }

void LinearizedRegion::setEntry(MachineBasicBlock *NewEntry) {
  Entry = NewEntry;
}

MachineBasicBlock *LinearizedRegion::getEntry() { return Entry; }

void LinearizedRegion::setExit(MachineBasicBlock *NewExit) { Exit = NewExit; }

MachineBasicBlock *LinearizedRegion::getExit() { return Exit; }

void LinearizedRegion::addMBB(MachineBasicBlock *MBB) { MBBs.insert(MBB); }

void LinearizedRegion::addMBBs(LinearizedRegion *InnerRegion) {
  for (const auto &MBB : InnerRegion->MBBs) {
    addMBB(MBB);
  }
}

bool LinearizedRegion::contains(MachineBasicBlock *MBB) {
  return MBBs.count(MBB) == 1;
}

bool LinearizedRegion::isLiveOut(unsigned Reg) {
  return LiveOuts.count(Reg) == 1;
}

bool LinearizedRegion::hasNoDef(unsigned Reg, MachineRegisterInfo *MRI) {
  return MRI->def_begin(Reg) == MRI->def_end();
}

// After the code has been structurized, what was flagged as kills
// before are no longer register kills.
void LinearizedRegion::removeFalseRegisterKills(MachineRegisterInfo *MRI) {
  const TargetRegisterInfo *TRI = MRI->getTargetRegisterInfo();
  for (auto MBBI : MBBs) {
    MachineBasicBlock *MBB = MBBI;
    for (auto &II : *MBB) {
      for (auto &RI : II.uses()) {
        if (RI.isReg()) {
          unsigned Reg = RI.getReg();
          if (TRI->isVirtualRegister(Reg)) {
            if (hasNoDef(Reg, MRI))
              continue;
            if (!MRI->hasOneDef(Reg)) {
              LLVM_DEBUG(this->getEntry()->getParent()->dump());
              LLVM_DEBUG(dbgs() << printReg(Reg, TRI) << "\n");
            }

            if (MRI->def_begin(Reg) == MRI->def_end()) {
              LLVM_DEBUG(dbgs() << "Register "
                                << printReg(Reg, MRI->getTargetRegisterInfo())
                                << " has NO defs\n");
            } else if (!MRI->hasOneDef(Reg)) {
              LLVM_DEBUG(dbgs() << "Register "
                                << printReg(Reg, MRI->getTargetRegisterInfo())
                                << " has multiple defs\n");
            }

            assert(MRI->hasOneDef(Reg) && "Register has multiple definitions");
            MachineOperand *Def = &(*(MRI->def_begin(Reg)));
            MachineOperand *UseOperand = &(RI);
            bool UseIsOutsideDefMBB = Def->getParent()->getParent() != MBB;
            if (UseIsOutsideDefMBB && UseOperand->isKill()) {
              LLVM_DEBUG(dbgs() << "Removing kill flag on register: "
                                << printReg(Reg, TRI) << "\n");
              UseOperand->setIsKill(false);
            }
          }
        }
      }
    }
  }
}

void LinearizedRegion::initLiveOut(RegionMRT *Region,
                                   const MachineRegisterInfo *MRI,
                                   const TargetRegisterInfo *TRI,
                                   PHILinearize &PHIInfo) {
  storeLiveOuts(Region, MRI, TRI, PHIInfo);
}

LinearizedRegion::LinearizedRegion(MachineBasicBlock *MBB,
                                   const MachineRegisterInfo *MRI,
                                   const TargetRegisterInfo *TRI,
                                   PHILinearize &PHIInfo) {
  setEntry(MBB);
  setExit(MBB);
  storeLiveOuts(MBB, MRI, TRI, PHIInfo);
  MBBs.insert(MBB);
  Parent = nullptr;
}

LinearizedRegion::LinearizedRegion() {
  setEntry(nullptr);
  setExit(nullptr);
  Parent = nullptr;
}

namespace {

class AMDGPUMachineCFGStructurizer : public MachineFunctionPass {
private:
  const MachineRegionInfo *Regions;
  const SIInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;
  unsigned BBSelectRegister;
  PHILinearize PHIInfo;
  DenseMap<MachineBasicBlock *, MachineBasicBlock *> FallthroughMap;
  RegionMRT *RMRT;

  void getPHIRegionIndices(RegionMRT *Region, MachineInstr &PHI,
                           SmallVector<unsigned, 2> &RegionIndices);
  void getPHIRegionIndices(LinearizedRegion *Region, MachineInstr &PHI,
                           SmallVector<unsigned, 2> &RegionIndices);
  void getPHINonRegionIndices(LinearizedRegion *Region, MachineInstr &PHI,
                              SmallVector<unsigned, 2> &PHINonRegionIndices);

  void storePHILinearizationInfoDest(
      unsigned LDestReg, MachineInstr &PHI,
      SmallVector<unsigned, 2> *RegionIndices = nullptr);

  unsigned storePHILinearizationInfo(MachineInstr &PHI,
                                     SmallVector<unsigned, 2> *RegionIndices);

  void extractKilledPHIs(MachineBasicBlock *MBB);

  bool shrinkPHI(MachineInstr &PHI, SmallVector<unsigned, 2> &PHIIndices,
                 unsigned *ReplaceReg);

  bool shrinkPHI(MachineInstr &PHI, unsigned CombinedSourceReg,
                 MachineBasicBlock *SourceMBB,
                 SmallVector<unsigned, 2> &PHIIndices, unsigned *ReplaceReg);

  void replacePHI(MachineInstr &PHI, unsigned CombinedSourceReg,
                  MachineBasicBlock *LastMerge,
                  SmallVector<unsigned, 2> &PHIRegionIndices);
  void replaceEntryPHI(MachineInstr &PHI, unsigned CombinedSourceReg,
                       MachineBasicBlock *IfMBB,
                       SmallVector<unsigned, 2> &PHIRegionIndices);
  void replaceLiveOutRegs(MachineInstr &PHI,
                          SmallVector<unsigned, 2> &PHIRegionIndices,
                          unsigned CombinedSourceReg,
                          LinearizedRegion *LRegion);
  void rewriteRegionExitPHI(RegionMRT *Region, MachineBasicBlock *LastMerge,
                            MachineInstr &PHI, LinearizedRegion *LRegion);

  void rewriteRegionExitPHIs(RegionMRT *Region, MachineBasicBlock *LastMerge,
                             LinearizedRegion *LRegion);
  void rewriteRegionEntryPHI(LinearizedRegion *Region, MachineBasicBlock *IfMBB,
                             MachineInstr &PHI);
  void rewriteRegionEntryPHIs(LinearizedRegion *Region,
                              MachineBasicBlock *IfMBB);

  bool regionIsSimpleIf(RegionMRT *Region);

  void transformSimpleIfRegion(RegionMRT *Region);

  void eliminateDeadBranchOperands(MachineBasicBlock::instr_iterator &II);

  void insertUnconditionalBranch(MachineBasicBlock *MBB,
                                 MachineBasicBlock *Dest,
                                 const DebugLoc &DL = DebugLoc());

  MachineBasicBlock *createLinearizedExitBlock(RegionMRT *Region);

  void insertMergePHI(MachineBasicBlock *IfBB, MachineBasicBlock *CodeBB,
                      MachineBasicBlock *MergeBB, unsigned DestRegister,
                      unsigned IfSourceRegister, unsigned CodeSourceRegister,
                      bool IsUndefIfSource = false);

  MachineBasicBlock *createIfBlock(MachineBasicBlock *MergeBB,
                                   MachineBasicBlock *CodeBBStart,
                                   MachineBasicBlock *CodeBBEnd,
                                   MachineBasicBlock *SelectBB, unsigned IfReg,
                                   bool InheritPreds);

  void prunePHIInfo(MachineBasicBlock *MBB);
  void createEntryPHI(LinearizedRegion *CurrentRegion, unsigned DestReg);

  void createEntryPHIs(LinearizedRegion *CurrentRegion);
  void resolvePHIInfos(MachineBasicBlock *FunctionEntry);

  void replaceRegisterWith(unsigned Register, unsigned NewRegister);

  MachineBasicBlock *createIfRegion(MachineBasicBlock *MergeBB,
                                    MachineBasicBlock *CodeBB,
                                    LinearizedRegion *LRegion,
                                    unsigned BBSelectRegIn,
                                    unsigned BBSelectRegOut);

  MachineBasicBlock *
  createIfRegion(MachineBasicBlock *MergeMBB, LinearizedRegion *InnerRegion,
                 LinearizedRegion *CurrentRegion, MachineBasicBlock *SelectBB,
                 unsigned BBSelectRegIn, unsigned BBSelectRegOut);
  void ensureCondIsNotKilled(SmallVector<MachineOperand, 1> Cond);

  void rewriteCodeBBTerminator(MachineBasicBlock *CodeBB,
                               MachineBasicBlock *MergeBB,
                               unsigned BBSelectReg);

  MachineInstr *getDefInstr(unsigned Reg);
  void insertChainedPHI(MachineBasicBlock *IfBB, MachineBasicBlock *CodeBB,
                        MachineBasicBlock *MergeBB,
                        LinearizedRegion *InnerRegion, unsigned DestReg,
                        unsigned SourceReg);
  bool containsDef(MachineBasicBlock *MBB, LinearizedRegion *InnerRegion,
                   unsigned Register);
  void rewriteLiveOutRegs(MachineBasicBlock *IfBB, MachineBasicBlock *CodeBB,
                          MachineBasicBlock *MergeBB,
                          LinearizedRegion *InnerRegion,
                          LinearizedRegion *LRegion);

  void splitLoopPHI(MachineInstr &PHI, MachineBasicBlock *Entry,
                    MachineBasicBlock *EntrySucc, LinearizedRegion *LRegion);
  void splitLoopPHIs(MachineBasicBlock *Entry, MachineBasicBlock *EntrySucc,
                     LinearizedRegion *LRegion);

  MachineBasicBlock *splitExit(LinearizedRegion *LRegion);

  MachineBasicBlock *splitEntry(LinearizedRegion *LRegion);

  LinearizedRegion *initLinearizedRegion(RegionMRT *Region);

  bool structurizeComplexRegion(RegionMRT *Region);

  bool structurizeRegion(RegionMRT *Region);

  bool structurizeRegions(RegionMRT *Region, bool isTopRegion);

public:
  static char ID;

  AMDGPUMachineCFGStructurizer() : MachineFunctionPass(ID) {
    initializeAMDGPUMachineCFGStructurizerPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineRegionInfoPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  void initFallthroughMap(MachineFunction &MF);

  void createLinearizedRegion(RegionMRT *Region, unsigned SelectOut);

  unsigned initializeSelectRegisters(MRT *MRT, unsigned ExistingExitReg,
                                     MachineRegisterInfo *MRI,
                                     const SIInstrInfo *TII);

  void setRegionMRT(RegionMRT *RegionTree) { RMRT = RegionTree; }

  RegionMRT *getRegionMRT() { return RMRT; }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char AMDGPUMachineCFGStructurizer::ID = 0;

bool AMDGPUMachineCFGStructurizer::regionIsSimpleIf(RegionMRT *Region) {
  MachineBasicBlock *Entry = Region->getEntry();
  MachineBasicBlock *Succ = Region->getSucc();
  bool FoundBypass = false;
  bool FoundIf = false;

  if (Entry->succ_size() != 2) {
    return false;
  }

  for (MachineBasicBlock::const_succ_iterator SI = Entry->succ_begin(),
                                              E = Entry->succ_end();
       SI != E; ++SI) {
    MachineBasicBlock *Current = *SI;

    if (Current == Succ) {
      FoundBypass = true;
    } else if ((Current->succ_size() == 1) &&
               *(Current->succ_begin()) == Succ) {
      FoundIf = true;
    }
  }

  return FoundIf && FoundBypass;
}

void AMDGPUMachineCFGStructurizer::transformSimpleIfRegion(RegionMRT *Region) {
  MachineBasicBlock *Entry = Region->getEntry();
  MachineBasicBlock *Exit = Region->getExit();
  TII->convertNonUniformIfRegion(Entry, Exit);
}

static void fixMBBTerminator(MachineBasicBlock *MBB) {
  if (MBB->succ_size() == 1) {
    auto *Succ = *(MBB->succ_begin());
    for (auto &TI : MBB->terminators()) {
      for (auto &UI : TI.uses()) {
        if (UI.isMBB() && UI.getMBB() != Succ) {
          UI.setMBB(Succ);
        }
      }
    }
  }
}

static void fixRegionTerminator(RegionMRT *Region) {
  MachineBasicBlock *InternalSucc = nullptr;
  MachineBasicBlock *ExternalSucc = nullptr;
  LinearizedRegion *LRegion = Region->getLinearizedRegion();
  auto Exit = LRegion->getExit();

  SmallPtrSet<MachineBasicBlock *, 2> Successors;
  for (MachineBasicBlock::const_succ_iterator SI = Exit->succ_begin(),
                                              SE = Exit->succ_end();
       SI != SE; ++SI) {
    MachineBasicBlock *Succ = *SI;
    if (LRegion->contains(Succ)) {
      // Do not allow re-assign
      assert(InternalSucc == nullptr);
      InternalSucc = Succ;
    } else {
      // Do not allow re-assign
      assert(ExternalSucc == nullptr);
      ExternalSucc = Succ;
    }
  }

  for (auto &TI : Exit->terminators()) {
    for (auto &UI : TI.uses()) {
      if (UI.isMBB()) {
        auto Target = UI.getMBB();
        if (Target != InternalSucc && Target != ExternalSucc) {
          UI.setMBB(ExternalSucc);
        }
      }
    }
  }
}

// If a region region is just a sequence of regions (and the exit
// block in the case of the top level region), we can simply skip
// linearizing it, because it is already linear
bool regionIsSequence(RegionMRT *Region) {
  auto Children = Region->getChildren();
  for (auto CI : *Children) {
    if (!CI->isRegion()) {
      if (CI->getMBBMRT()->getMBB()->succ_size() > 1) {
        return false;
      }
    }
  }
  return true;
}

void fixupRegionExits(RegionMRT *Region) {
  auto Children = Region->getChildren();
  for (auto CI : *Children) {
    if (!CI->isRegion()) {
      fixMBBTerminator(CI->getMBBMRT()->getMBB());
    } else {
      fixRegionTerminator(CI->getRegionMRT());
    }
  }
}

void AMDGPUMachineCFGStructurizer::getPHIRegionIndices(
    RegionMRT *Region, MachineInstr &PHI,
    SmallVector<unsigned, 2> &PHIRegionIndices) {
  unsigned NumInputs = getPHINumInputs(PHI);
  for (unsigned i = 0; i < NumInputs; ++i) {
    MachineBasicBlock *Pred = getPHIPred(PHI, i);
    if (Region->contains(Pred)) {
      PHIRegionIndices.push_back(i);
    }
  }
}

void AMDGPUMachineCFGStructurizer::getPHIRegionIndices(
    LinearizedRegion *Region, MachineInstr &PHI,
    SmallVector<unsigned, 2> &PHIRegionIndices) {
  unsigned NumInputs = getPHINumInputs(PHI);
  for (unsigned i = 0; i < NumInputs; ++i) {
    MachineBasicBlock *Pred = getPHIPred(PHI, i);
    if (Region->contains(Pred)) {
      PHIRegionIndices.push_back(i);
    }
  }
}

void AMDGPUMachineCFGStructurizer::getPHINonRegionIndices(
    LinearizedRegion *Region, MachineInstr &PHI,
    SmallVector<unsigned, 2> &PHINonRegionIndices) {
  unsigned NumInputs = getPHINumInputs(PHI);
  for (unsigned i = 0; i < NumInputs; ++i) {
    MachineBasicBlock *Pred = getPHIPred(PHI, i);
    if (!Region->contains(Pred)) {
      PHINonRegionIndices.push_back(i);
    }
  }
}

void AMDGPUMachineCFGStructurizer::storePHILinearizationInfoDest(
    unsigned LDestReg, MachineInstr &PHI,
    SmallVector<unsigned, 2> *RegionIndices) {
  if (RegionIndices) {
    for (auto i : *RegionIndices) {
      PHIInfo.addSource(LDestReg, getPHISourceReg(PHI, i), getPHIPred(PHI, i));
    }
  } else {
    unsigned NumInputs = getPHINumInputs(PHI);
    for (unsigned i = 0; i < NumInputs; ++i) {
      PHIInfo.addSource(LDestReg, getPHISourceReg(PHI, i), getPHIPred(PHI, i));
    }
  }
}

unsigned AMDGPUMachineCFGStructurizer::storePHILinearizationInfo(
    MachineInstr &PHI, SmallVector<unsigned, 2> *RegionIndices) {
  unsigned DestReg = getPHIDestReg(PHI);
  unsigned LinearizeDestReg =
      MRI->createVirtualRegister(MRI->getRegClass(DestReg));
  PHIInfo.addDest(LinearizeDestReg, PHI.getDebugLoc());
  storePHILinearizationInfoDest(LinearizeDestReg, PHI, RegionIndices);
  return LinearizeDestReg;
}

void AMDGPUMachineCFGStructurizer::extractKilledPHIs(MachineBasicBlock *MBB) {
  // We need to create a new chain for the killed phi, but there is no
  // need to do the renaming outside or inside the block.
  SmallPtrSet<MachineInstr *, 2> PHIs;
  for (MachineBasicBlock::instr_iterator I = MBB->instr_begin(),
                                         E = MBB->instr_end();
       I != E; ++I) {
    MachineInstr &Instr = *I;
    if (Instr.isPHI()) {
      unsigned PHIDestReg = getPHIDestReg(Instr);
      LLVM_DEBUG(dbgs() << "Extractking killed phi:\n");
      LLVM_DEBUG(Instr.dump());
      PHIs.insert(&Instr);
      PHIInfo.addDest(PHIDestReg, Instr.getDebugLoc());
      storePHILinearizationInfoDest(PHIDestReg, Instr);
    }
  }

  for (auto PI : PHIs) {
    PI->eraseFromParent();
  }
}

static bool isPHIRegionIndex(SmallVector<unsigned, 2> PHIRegionIndices,
                             unsigned Index) {
  for (auto i : PHIRegionIndices) {
    if (i == Index)
      return true;
  }
  return false;
}

bool AMDGPUMachineCFGStructurizer::shrinkPHI(MachineInstr &PHI,
                                       SmallVector<unsigned, 2> &PHIIndices,
                                       unsigned *ReplaceReg) {
  return shrinkPHI(PHI, 0, nullptr, PHIIndices, ReplaceReg);
}

bool AMDGPUMachineCFGStructurizer::shrinkPHI(MachineInstr &PHI,
                                       unsigned CombinedSourceReg,
                                       MachineBasicBlock *SourceMBB,
                                       SmallVector<unsigned, 2> &PHIIndices,
                                       unsigned *ReplaceReg) {
  LLVM_DEBUG(dbgs() << "Shrink PHI: ");
  LLVM_DEBUG(PHI.dump());
  LLVM_DEBUG(dbgs() << " to " << printReg(getPHIDestReg(PHI), TRI)
                    << " = PHI(");

  bool Replaced = false;
  unsigned NumInputs = getPHINumInputs(PHI);
  int SingleExternalEntryIndex = -1;
  for (unsigned i = 0; i < NumInputs; ++i) {
    if (!isPHIRegionIndex(PHIIndices, i)) {
      if (SingleExternalEntryIndex == -1) {
        // Single entry
        SingleExternalEntryIndex = i;
      } else {
        // Multiple entries
        SingleExternalEntryIndex = -2;
      }
    }
  }

  if (SingleExternalEntryIndex > -1) {
    *ReplaceReg = getPHISourceReg(PHI, SingleExternalEntryIndex);
    // We should not rewrite the code, we should only pick up the single value
    // that represents the shrunk PHI.
    Replaced = true;
  } else {
    MachineBasicBlock *MBB = PHI.getParent();
    MachineInstrBuilder MIB =
        BuildMI(*MBB, PHI, PHI.getDebugLoc(), TII->get(TargetOpcode::PHI),
                getPHIDestReg(PHI));
    if (SourceMBB) {
      MIB.addReg(CombinedSourceReg);
      MIB.addMBB(SourceMBB);
      LLVM_DEBUG(dbgs() << printReg(CombinedSourceReg, TRI) << ", "
                        << printMBBReference(*SourceMBB));
    }

    for (unsigned i = 0; i < NumInputs; ++i) {
      if (isPHIRegionIndex(PHIIndices, i)) {
        continue;
      }
      unsigned SourceReg = getPHISourceReg(PHI, i);
      MachineBasicBlock *SourcePred = getPHIPred(PHI, i);
      MIB.addReg(SourceReg);
      MIB.addMBB(SourcePred);
      LLVM_DEBUG(dbgs() << printReg(SourceReg, TRI) << ", "
                        << printMBBReference(*SourcePred));
    }
    LLVM_DEBUG(dbgs() << ")\n");
  }
  PHI.eraseFromParent();
  return Replaced;
}

void AMDGPUMachineCFGStructurizer::replacePHI(
    MachineInstr &PHI, unsigned CombinedSourceReg, MachineBasicBlock *LastMerge,
    SmallVector<unsigned, 2> &PHIRegionIndices) {
  LLVM_DEBUG(dbgs() << "Replace PHI: ");
  LLVM_DEBUG(PHI.dump());
  LLVM_DEBUG(dbgs() << " with " << printReg(getPHIDestReg(PHI), TRI)
                    << " = PHI(");

  bool HasExternalEdge = false;
  unsigned NumInputs = getPHINumInputs(PHI);
  for (unsigned i = 0; i < NumInputs; ++i) {
    if (!isPHIRegionIndex(PHIRegionIndices, i)) {
      HasExternalEdge = true;
    }
  }

  if (HasExternalEdge) {
    MachineBasicBlock *MBB = PHI.getParent();
    MachineInstrBuilder MIB =
        BuildMI(*MBB, PHI, PHI.getDebugLoc(), TII->get(TargetOpcode::PHI),
                getPHIDestReg(PHI));
    MIB.addReg(CombinedSourceReg);
    MIB.addMBB(LastMerge);
    LLVM_DEBUG(dbgs() << printReg(CombinedSourceReg, TRI) << ", "
                      << printMBBReference(*LastMerge));
    for (unsigned i = 0; i < NumInputs; ++i) {
      if (isPHIRegionIndex(PHIRegionIndices, i)) {
        continue;
      }
      unsigned SourceReg = getPHISourceReg(PHI, i);
      MachineBasicBlock *SourcePred = getPHIPred(PHI, i);
      MIB.addReg(SourceReg);
      MIB.addMBB(SourcePred);
      LLVM_DEBUG(dbgs() << printReg(SourceReg, TRI) << ", "
                        << printMBBReference(*SourcePred));
    }
    LLVM_DEBUG(dbgs() << ")\n");
  } else {
    replaceRegisterWith(getPHIDestReg(PHI), CombinedSourceReg);
  }
  PHI.eraseFromParent();
}

void AMDGPUMachineCFGStructurizer::replaceEntryPHI(
    MachineInstr &PHI, unsigned CombinedSourceReg, MachineBasicBlock *IfMBB,
    SmallVector<unsigned, 2> &PHIRegionIndices) {
  LLVM_DEBUG(dbgs() << "Replace entry PHI: ");
  LLVM_DEBUG(PHI.dump());
  LLVM_DEBUG(dbgs() << " with ");

  unsigned NumInputs = getPHINumInputs(PHI);
  unsigned NumNonRegionInputs = NumInputs;
  for (unsigned i = 0; i < NumInputs; ++i) {
    if (isPHIRegionIndex(PHIRegionIndices, i)) {
      NumNonRegionInputs--;
    }
  }

  if (NumNonRegionInputs == 0) {
    auto DestReg = getPHIDestReg(PHI);
    replaceRegisterWith(DestReg, CombinedSourceReg);
    LLVM_DEBUG(dbgs() << " register " << printReg(CombinedSourceReg, TRI)
                      << "\n");
    PHI.eraseFromParent();
  } else {
    LLVM_DEBUG(dbgs() << printReg(getPHIDestReg(PHI), TRI) << " = PHI(");
    MachineBasicBlock *MBB = PHI.getParent();
    MachineInstrBuilder MIB =
        BuildMI(*MBB, PHI, PHI.getDebugLoc(), TII->get(TargetOpcode::PHI),
                getPHIDestReg(PHI));
    MIB.addReg(CombinedSourceReg);
    MIB.addMBB(IfMBB);
    LLVM_DEBUG(dbgs() << printReg(CombinedSourceReg, TRI) << ", "
                      << printMBBReference(*IfMBB));
    unsigned NumInputs = getPHINumInputs(PHI);
    for (unsigned i = 0; i < NumInputs; ++i) {
      if (isPHIRegionIndex(PHIRegionIndices, i)) {
        continue;
      }
      unsigned SourceReg = getPHISourceReg(PHI, i);
      MachineBasicBlock *SourcePred = getPHIPred(PHI, i);
      MIB.addReg(SourceReg);
      MIB.addMBB(SourcePred);
      LLVM_DEBUG(dbgs() << printReg(SourceReg, TRI) << ", "
                        << printMBBReference(*SourcePred));
    }
    LLVM_DEBUG(dbgs() << ")\n");
    PHI.eraseFromParent();
  }
}

void AMDGPUMachineCFGStructurizer::replaceLiveOutRegs(
    MachineInstr &PHI, SmallVector<unsigned, 2> &PHIRegionIndices,
    unsigned CombinedSourceReg, LinearizedRegion *LRegion) {
  bool WasLiveOut = false;
  for (auto PII : PHIRegionIndices) {
    unsigned Reg = getPHISourceReg(PHI, PII);
    if (LRegion->isLiveOut(Reg)) {
      bool IsDead = true;

      // Check if register is live out of the basic block
      MachineBasicBlock *DefMBB = getDefInstr(Reg)->getParent();
      for (auto UI = MRI->use_begin(Reg), E = MRI->use_end(); UI != E; ++UI) {
        if ((*UI).getParent()->getParent() != DefMBB) {
          IsDead = false;
        }
      }

      LLVM_DEBUG(dbgs() << "Register " << printReg(Reg, TRI) << " is "
                        << (IsDead ? "dead" : "alive")
                        << " after PHI replace\n");
      if (IsDead) {
        LRegion->removeLiveOut(Reg);
      }
      WasLiveOut = true;
    }
  }

  if (WasLiveOut)
    LRegion->addLiveOut(CombinedSourceReg);
}

void AMDGPUMachineCFGStructurizer::rewriteRegionExitPHI(RegionMRT *Region,
                                                  MachineBasicBlock *LastMerge,
                                                  MachineInstr &PHI,
                                                  LinearizedRegion *LRegion) {
  SmallVector<unsigned, 2> PHIRegionIndices;
  getPHIRegionIndices(Region, PHI, PHIRegionIndices);
  unsigned LinearizedSourceReg =
      storePHILinearizationInfo(PHI, &PHIRegionIndices);

  replacePHI(PHI, LinearizedSourceReg, LastMerge, PHIRegionIndices);
  replaceLiveOutRegs(PHI, PHIRegionIndices, LinearizedSourceReg, LRegion);
}

void AMDGPUMachineCFGStructurizer::rewriteRegionEntryPHI(LinearizedRegion *Region,
                                                   MachineBasicBlock *IfMBB,
                                                   MachineInstr &PHI) {
  SmallVector<unsigned, 2> PHINonRegionIndices;
  getPHINonRegionIndices(Region, PHI, PHINonRegionIndices);
  unsigned LinearizedSourceReg =
      storePHILinearizationInfo(PHI, &PHINonRegionIndices);
  replaceEntryPHI(PHI, LinearizedSourceReg, IfMBB, PHINonRegionIndices);
}

static void collectPHIs(MachineBasicBlock *MBB,
                        SmallVector<MachineInstr *, 2> &PHIs) {
  for (auto &BBI : *MBB) {
    if (BBI.isPHI()) {
      PHIs.push_back(&BBI);
    }
  }
}

void AMDGPUMachineCFGStructurizer::rewriteRegionExitPHIs(RegionMRT *Region,
                                                   MachineBasicBlock *LastMerge,
                                                   LinearizedRegion *LRegion) {
  SmallVector<MachineInstr *, 2> PHIs;
  auto Exit = Region->getSucc();
  if (Exit == nullptr)
    return;

  collectPHIs(Exit, PHIs);

  for (auto PHII : PHIs) {
    rewriteRegionExitPHI(Region, LastMerge, *PHII, LRegion);
  }
}

void AMDGPUMachineCFGStructurizer::rewriteRegionEntryPHIs(LinearizedRegion *Region,
                                                    MachineBasicBlock *IfMBB) {
  SmallVector<MachineInstr *, 2> PHIs;
  auto Entry = Region->getEntry();

  collectPHIs(Entry, PHIs);

  for (auto PHII : PHIs) {
    rewriteRegionEntryPHI(Region, IfMBB, *PHII);
  }
}

void AMDGPUMachineCFGStructurizer::insertUnconditionalBranch(MachineBasicBlock *MBB,
                                                       MachineBasicBlock *Dest,
                                                       const DebugLoc &DL) {
  LLVM_DEBUG(dbgs() << "Inserting unconditional branch: " << MBB->getNumber()
                    << " -> " << Dest->getNumber() << "\n");
  MachineBasicBlock::instr_iterator Terminator = MBB->getFirstInstrTerminator();
  bool HasTerminator = Terminator != MBB->instr_end();
  if (HasTerminator) {
    TII->ReplaceTailWithBranchTo(Terminator, Dest);
  }
  if (++MachineFunction::iterator(MBB) != MachineFunction::iterator(Dest)) {
    TII->insertUnconditionalBranch(*MBB, Dest, DL);
  }
}

static MachineBasicBlock *getSingleExitNode(MachineFunction &MF) {
  MachineBasicBlock *result = nullptr;
  for (auto &MFI : MF) {
    if (MFI.succ_size() == 0) {
      if (result == nullptr) {
        result = &MFI;
      } else {
        return nullptr;
      }
    }
  }

  return result;
}

static bool hasOneExitNode(MachineFunction &MF) {
  return getSingleExitNode(MF) != nullptr;
}

MachineBasicBlock *
AMDGPUMachineCFGStructurizer::createLinearizedExitBlock(RegionMRT *Region) {
  auto Exit = Region->getSucc();

  // If the exit is the end of the function, we just use the existing
  MachineFunction *MF = Region->getEntry()->getParent();
  if (Exit == nullptr && hasOneExitNode(*MF)) {
    return &(*(--(Region->getEntry()->getParent()->end())));
  }

  MachineBasicBlock *LastMerge = MF->CreateMachineBasicBlock();
  if (Exit == nullptr) {
    MachineFunction::iterator ExitIter = MF->end();
    MF->insert(ExitIter, LastMerge);
  } else {
    MachineFunction::iterator ExitIter = Exit->getIterator();
    MF->insert(ExitIter, LastMerge);
    LastMerge->addSuccessor(Exit);
    insertUnconditionalBranch(LastMerge, Exit);
    LLVM_DEBUG(dbgs() << "Created exit block: " << LastMerge->getNumber()
                      << "\n");
  }
  return LastMerge;
}

void AMDGPUMachineCFGStructurizer::insertMergePHI(MachineBasicBlock *IfBB,
                                            MachineBasicBlock *CodeBB,
                                            MachineBasicBlock *MergeBB,
                                            unsigned DestRegister,
                                            unsigned IfSourceRegister,
                                            unsigned CodeSourceRegister,
                                            bool IsUndefIfSource) {
  // If this is the function exit block, we don't need a phi.
  if (MergeBB->succ_begin() == MergeBB->succ_end()) {
    return;
  }
  LLVM_DEBUG(dbgs() << "Merge PHI (" << printMBBReference(*MergeBB)
                    << "): " << printReg(DestRegister, TRI) << " = PHI("
                    << printReg(IfSourceRegister, TRI) << ", "
                    << printMBBReference(*IfBB)
                    << printReg(CodeSourceRegister, TRI) << ", "
                    << printMBBReference(*CodeBB) << ")\n");
  const DebugLoc &DL = MergeBB->findDebugLoc(MergeBB->begin());
  MachineInstrBuilder MIB = BuildMI(*MergeBB, MergeBB->instr_begin(), DL,
                                    TII->get(TargetOpcode::PHI), DestRegister);
  if (IsUndefIfSource && false) {
    MIB.addReg(IfSourceRegister, RegState::Undef);
  } else {
    MIB.addReg(IfSourceRegister);
  }
  MIB.addMBB(IfBB);
  MIB.addReg(CodeSourceRegister);
  MIB.addMBB(CodeBB);
}

static void removeExternalCFGSuccessors(MachineBasicBlock *MBB) {
  for (MachineBasicBlock::succ_iterator PI = MBB->succ_begin(),
                                        E = MBB->succ_end();
       PI != E; ++PI) {
    if ((*PI) != MBB) {
      (MBB)->removeSuccessor(*PI);
    }
  }
}

static void removeExternalCFGEdges(MachineBasicBlock *StartMBB,
                                   MachineBasicBlock *EndMBB) {

  // We have to check against the StartMBB successor becasuse a
  // structurized region with a loop will have the entry block split,
  // and the backedge will go to the entry successor.
  DenseSet<std::pair<MachineBasicBlock *, MachineBasicBlock *>> Succs;
  unsigned SuccSize = StartMBB->succ_size();
  if (SuccSize > 0) {
    MachineBasicBlock *StartMBBSucc = *(StartMBB->succ_begin());
    for (MachineBasicBlock::succ_iterator PI = EndMBB->succ_begin(),
                                          E = EndMBB->succ_end();
         PI != E; ++PI) {
      // Either we have a back-edge to the entry block, or a back-edge to the
      // successor of the entry block since the block may be split.
      if ((*PI) != StartMBB &&
          !((*PI) == StartMBBSucc && StartMBB != EndMBB && SuccSize == 1)) {
        Succs.insert(
            std::pair<MachineBasicBlock *, MachineBasicBlock *>(EndMBB, *PI));
      }
    }
  }

  for (MachineBasicBlock::pred_iterator PI = StartMBB->pred_begin(),
                                        E = StartMBB->pred_end();
       PI != E; ++PI) {
    if ((*PI) != EndMBB) {
      Succs.insert(
          std::pair<MachineBasicBlock *, MachineBasicBlock *>(*PI, StartMBB));
    }
  }

  for (auto SI : Succs) {
    std::pair<MachineBasicBlock *, MachineBasicBlock *> Edge = SI;
    LLVM_DEBUG(dbgs() << "Removing edge: " << printMBBReference(*Edge.first)
                      << " -> " << printMBBReference(*Edge.second) << "\n");
    Edge.first->removeSuccessor(Edge.second);
  }
}

MachineBasicBlock *AMDGPUMachineCFGStructurizer::createIfBlock(
    MachineBasicBlock *MergeBB, MachineBasicBlock *CodeBBStart,
    MachineBasicBlock *CodeBBEnd, MachineBasicBlock *SelectBB, unsigned IfReg,
    bool InheritPreds) {
  MachineFunction *MF = MergeBB->getParent();
  MachineBasicBlock *IfBB = MF->CreateMachineBasicBlock();

  if (InheritPreds) {
    for (MachineBasicBlock::pred_iterator PI = CodeBBStart->pred_begin(),
                                          E = CodeBBStart->pred_end();
         PI != E; ++PI) {
      if ((*PI) != CodeBBEnd) {
        MachineBasicBlock *Pred = (*PI);
        Pred->addSuccessor(IfBB);
      }
    }
  }

  removeExternalCFGEdges(CodeBBStart, CodeBBEnd);

  auto CodeBBStartI = CodeBBStart->getIterator();
  auto CodeBBEndI = CodeBBEnd->getIterator();
  auto MergeIter = MergeBB->getIterator();
  MF->insert(MergeIter, IfBB);
  MF->splice(MergeIter, CodeBBStartI, ++CodeBBEndI);
  IfBB->addSuccessor(MergeBB);
  IfBB->addSuccessor(CodeBBStart);

  LLVM_DEBUG(dbgs() << "Created If block: " << IfBB->getNumber() << "\n");
  // Ensure that the MergeBB is a successor of the CodeEndBB.
  if (!CodeBBEnd->isSuccessor(MergeBB))
    CodeBBEnd->addSuccessor(MergeBB);

  LLVM_DEBUG(dbgs() << "Moved " << printMBBReference(*CodeBBStart)
                    << " through " << printMBBReference(*CodeBBEnd) << "\n");

  // If we have a single predecessor we can find a reasonable debug location
  MachineBasicBlock *SinglePred =
      CodeBBStart->pred_size() == 1 ? *(CodeBBStart->pred_begin()) : nullptr;
  const DebugLoc &DL = SinglePred
                    ? SinglePred->findDebugLoc(SinglePred->getFirstTerminator())
                    : DebugLoc();

  unsigned Reg =
      TII->insertEQ(IfBB, IfBB->begin(), DL, IfReg,
                    SelectBB->getNumber() /* CodeBBStart->getNumber() */);
  if (&(*(IfBB->getParent()->begin())) == IfBB) {
    TII->materializeImmediate(*IfBB, IfBB->begin(), DL, IfReg,
                              CodeBBStart->getNumber());
  }
  MachineOperand RegOp = MachineOperand::CreateReg(Reg, false, false, true);
  ArrayRef<MachineOperand> Cond(RegOp);
  TII->insertBranch(*IfBB, MergeBB, CodeBBStart, Cond, DL);

  return IfBB;
}

void AMDGPUMachineCFGStructurizer::ensureCondIsNotKilled(
    SmallVector<MachineOperand, 1> Cond) {
  if (Cond.size() != 1)
    return;
  if (!Cond[0].isReg())
    return;

  unsigned CondReg = Cond[0].getReg();
  for (auto UI = MRI->use_begin(CondReg), E = MRI->use_end(); UI != E; ++UI) {
    (*UI).setIsKill(false);
  }
}

void AMDGPUMachineCFGStructurizer::rewriteCodeBBTerminator(MachineBasicBlock *CodeBB,
                                                     MachineBasicBlock *MergeBB,
                                                     unsigned BBSelectReg) {
  MachineBasicBlock *TrueBB = nullptr;
  MachineBasicBlock *FalseBB = nullptr;
  SmallVector<MachineOperand, 1> Cond;
  MachineBasicBlock *FallthroughBB = FallthroughMap[CodeBB];
  TII->analyzeBranch(*CodeBB, TrueBB, FalseBB, Cond);

  const DebugLoc &DL = CodeBB->findDebugLoc(CodeBB->getFirstTerminator());

  if (FalseBB == nullptr && TrueBB == nullptr && FallthroughBB == nullptr) {
    // This is an exit block, hence no successors. We will assign the
    // bb select register to the entry block.
    TII->materializeImmediate(*CodeBB, CodeBB->getFirstTerminator(), DL,
                              BBSelectReg,
                              CodeBB->getParent()->begin()->getNumber());
    insertUnconditionalBranch(CodeBB, MergeBB, DL);
    return;
  }

  if (FalseBB == nullptr && TrueBB == nullptr) {
    TrueBB = FallthroughBB;
  } else if (TrueBB != nullptr) {
    FalseBB =
        (FallthroughBB && (FallthroughBB != TrueBB)) ? FallthroughBB : FalseBB;
  }

  if ((TrueBB != nullptr && FalseBB == nullptr) || (TrueBB == FalseBB)) {
    TII->materializeImmediate(*CodeBB, CodeBB->getFirstTerminator(), DL,
                              BBSelectReg, TrueBB->getNumber());
  } else {
    const TargetRegisterClass *RegClass = MRI->getRegClass(BBSelectReg);
    unsigned TrueBBReg = MRI->createVirtualRegister(RegClass);
    unsigned FalseBBReg = MRI->createVirtualRegister(RegClass);
    TII->materializeImmediate(*CodeBB, CodeBB->getFirstTerminator(), DL,
                              TrueBBReg, TrueBB->getNumber());
    TII->materializeImmediate(*CodeBB, CodeBB->getFirstTerminator(), DL,
                              FalseBBReg, FalseBB->getNumber());
    ensureCondIsNotKilled(Cond);
    TII->insertVectorSelect(*CodeBB, CodeBB->getFirstTerminator(), DL,
                            BBSelectReg, Cond, TrueBBReg, FalseBBReg);
  }

  insertUnconditionalBranch(CodeBB, MergeBB, DL);
}

MachineInstr *AMDGPUMachineCFGStructurizer::getDefInstr(unsigned Reg) {
  if (MRI->def_begin(Reg) == MRI->def_end()) {
    LLVM_DEBUG(dbgs() << "Register "
                      << printReg(Reg, MRI->getTargetRegisterInfo())
                      << " has NO defs\n");
  } else if (!MRI->hasOneDef(Reg)) {
    LLVM_DEBUG(dbgs() << "Register "
                      << printReg(Reg, MRI->getTargetRegisterInfo())
                      << " has multiple defs\n");
    LLVM_DEBUG(dbgs() << "DEFS BEGIN:\n");
    for (auto DI = MRI->def_begin(Reg), DE = MRI->def_end(); DI != DE; ++DI) {
      LLVM_DEBUG(DI->getParent()->dump());
    }
    LLVM_DEBUG(dbgs() << "DEFS END\n");
  }

  assert(MRI->hasOneDef(Reg) && "Register has multiple definitions");
  return (*(MRI->def_begin(Reg))).getParent();
}

void AMDGPUMachineCFGStructurizer::insertChainedPHI(MachineBasicBlock *IfBB,
                                              MachineBasicBlock *CodeBB,
                                              MachineBasicBlock *MergeBB,
                                              LinearizedRegion *InnerRegion,
                                              unsigned DestReg,
                                              unsigned SourceReg) {
  // In this function we know we are part of a chain already, so we need
  // to add the registers to the existing chain, and rename the register
  // inside the region.
  bool IsSingleBB = InnerRegion->getEntry() == InnerRegion->getExit();
  MachineInstr *DefInstr = getDefInstr(SourceReg);
  if (DefInstr->isPHI() && DefInstr->getParent() == CodeBB && IsSingleBB) {
    // Handle the case where the def is a PHI-def inside a basic
    // block, then we only need to do renaming. Special care needs to
    // be taken if the PHI-def is part of an existing chain, or if a
    // new one needs to be created.
    InnerRegion->replaceRegisterInsideRegion(SourceReg, DestReg, true, MRI);

    // We collect all PHI Information, and if we are at the region entry,
    // all PHIs will be removed, and then re-introduced if needed.
    storePHILinearizationInfoDest(DestReg, *DefInstr);
    // We have picked up all the information we need now and can remove
    // the PHI
    PHIInfo.removeSource(DestReg, SourceReg, CodeBB);
    DefInstr->eraseFromParent();
  } else {
    // If this is not a phi-def, or it is a phi-def but from a linearized region
    if (IsSingleBB && DefInstr->getParent() == InnerRegion->getEntry()) {
      // If this is a single BB and the definition is in this block we
      // need to replace any uses outside the region.
      InnerRegion->replaceRegisterOutsideRegion(SourceReg, DestReg, false, MRI);
    }
    const TargetRegisterClass *RegClass = MRI->getRegClass(DestReg);
    unsigned NextDestReg = MRI->createVirtualRegister(RegClass);
    bool IsLastDef = PHIInfo.getNumSources(DestReg) == 1;
    LLVM_DEBUG(dbgs() << "Insert Chained PHI\n");
    insertMergePHI(IfBB, InnerRegion->getExit(), MergeBB, DestReg, NextDestReg,
                   SourceReg, IsLastDef);

    PHIInfo.removeSource(DestReg, SourceReg, CodeBB);
    if (IsLastDef) {
      const DebugLoc &DL = IfBB->findDebugLoc(IfBB->getFirstTerminator());
      TII->materializeImmediate(*IfBB, IfBB->getFirstTerminator(), DL,
                                NextDestReg, 0);
      PHIInfo.deleteDef(DestReg);
    } else {
      PHIInfo.replaceDef(DestReg, NextDestReg);
    }
  }
}

bool AMDGPUMachineCFGStructurizer::containsDef(MachineBasicBlock *MBB,
                                         LinearizedRegion *InnerRegion,
                                         unsigned Register) {
  return getDefInstr(Register)->getParent() == MBB ||
         InnerRegion->contains(getDefInstr(Register)->getParent());
}

void AMDGPUMachineCFGStructurizer::rewriteLiveOutRegs(MachineBasicBlock *IfBB,
                                                MachineBasicBlock *CodeBB,
                                                MachineBasicBlock *MergeBB,
                                                LinearizedRegion *InnerRegion,
                                                LinearizedRegion *LRegion) {
  DenseSet<unsigned> *LiveOuts = InnerRegion->getLiveOuts();
  SmallVector<unsigned, 4> OldLiveOuts;
  bool IsSingleBB = InnerRegion->getEntry() == InnerRegion->getExit();
  for (auto OLI : *LiveOuts) {
    OldLiveOuts.push_back(OLI);
  }

  for (auto LI : OldLiveOuts) {
    LLVM_DEBUG(dbgs() << "LiveOut: " << printReg(LI, TRI));
    if (!containsDef(CodeBB, InnerRegion, LI) ||
        (!IsSingleBB && (getDefInstr(LI)->getParent() == LRegion->getExit()))) {
      // If the register simly lives through the CodeBB, we don't have
      // to rewrite anything since the register is not defined in this
      // part of the code.
      LLVM_DEBUG(dbgs() << "- through");
      continue;
    }
    LLVM_DEBUG(dbgs() << "\n");
    unsigned Reg = LI;
    if (/*!PHIInfo.isSource(Reg) &&*/ Reg != InnerRegion->getBBSelectRegOut()) {
      // If the register is live out, we do want to create a phi,
      // unless it is from the Exit block, becasuse in that case there
      // is already a PHI, and no need to create a new one.

      // If the register is just a live out def and not part of a phi
      // chain, we need to create a PHI node to handle the if region,
      // and replace all uses outside of the region with the new dest
      // register, unless it is the outgoing BB select register. We have
      // already creaed phi nodes for these.
      const TargetRegisterClass *RegClass = MRI->getRegClass(Reg);
      unsigned PHIDestReg = MRI->createVirtualRegister(RegClass);
      unsigned IfSourceReg = MRI->createVirtualRegister(RegClass);
      // Create initializer, this value is never used, but is needed
      // to satisfy SSA.
      LLVM_DEBUG(dbgs() << "Initializer for reg: " << printReg(Reg) << "\n");
      TII->materializeImmediate(*IfBB, IfBB->getFirstTerminator(), DebugLoc(),
                        IfSourceReg, 0);

      InnerRegion->replaceRegisterOutsideRegion(Reg, PHIDestReg, true, MRI);
      LLVM_DEBUG(dbgs() << "Insert Non-Chained Live out PHI\n");
      insertMergePHI(IfBB, InnerRegion->getExit(), MergeBB, PHIDestReg,
                     IfSourceReg, Reg, true);
    }
  }

  // Handle the chained definitions in PHIInfo, checking if this basic block
  // is a source block for a definition.
  SmallVector<unsigned, 4> Sources;
  if (PHIInfo.findSourcesFromMBB(CodeBB, Sources)) {
    LLVM_DEBUG(dbgs() << "Inserting PHI Live Out from "
                      << printMBBReference(*CodeBB) << "\n");
    for (auto SI : Sources) {
      unsigned DestReg;
      PHIInfo.findDest(SI, CodeBB, DestReg);
      insertChainedPHI(IfBB, CodeBB, MergeBB, InnerRegion, DestReg, SI);
    }
    LLVM_DEBUG(dbgs() << "Insertion done.\n");
  }

  LLVM_DEBUG(PHIInfo.dump(MRI));
}

void AMDGPUMachineCFGStructurizer::prunePHIInfo(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Before PHI Prune\n");
  LLVM_DEBUG(PHIInfo.dump(MRI));
  SmallVector<std::tuple<unsigned, unsigned, MachineBasicBlock *>, 4>
      ElimiatedSources;
  for (auto DRI = PHIInfo.dests_begin(), DE = PHIInfo.dests_end(); DRI != DE;
       ++DRI) {

    unsigned DestReg = *DRI;
    auto SE = PHIInfo.sources_end(DestReg);

    bool MBBContainsPHISource = false;
    // Check if there is a PHI source in this MBB
    for (auto SRI = PHIInfo.sources_begin(DestReg); SRI != SE; ++SRI) {
      unsigned SourceReg = (*SRI).first;
      MachineOperand *Def = &(*(MRI->def_begin(SourceReg)));
      if (Def->getParent()->getParent() == MBB) {
        MBBContainsPHISource = true;
      }
    }

    // If so, all other sources are useless since we know this block
    // is always executed when the region is executed.
    if (MBBContainsPHISource) {
      for (auto SRI = PHIInfo.sources_begin(DestReg); SRI != SE; ++SRI) {
        PHILinearize::PHISourceT Source = *SRI;
        unsigned SourceReg = Source.first;
        MachineBasicBlock *SourceMBB = Source.second;
        MachineOperand *Def = &(*(MRI->def_begin(SourceReg)));
        if (Def->getParent()->getParent() != MBB) {
          ElimiatedSources.push_back(
              std::make_tuple(DestReg, SourceReg, SourceMBB));
        }
      }
    }
  }

  // Remove the PHI sources that are in the given MBB
  for (auto &SourceInfo : ElimiatedSources) {
    PHIInfo.removeSource(std::get<0>(SourceInfo), std::get<1>(SourceInfo),
                         std::get<2>(SourceInfo));
  }
  LLVM_DEBUG(dbgs() << "After PHI Prune\n");
  LLVM_DEBUG(PHIInfo.dump(MRI));
}

void AMDGPUMachineCFGStructurizer::createEntryPHI(LinearizedRegion *CurrentRegion,
                                            unsigned DestReg) {
  MachineBasicBlock *Entry = CurrentRegion->getEntry();
  MachineBasicBlock *Exit = CurrentRegion->getExit();

  LLVM_DEBUG(dbgs() << "RegionExit: " << Exit->getNumber() << " Pred: "
                    << (*(Entry->pred_begin()))->getNumber() << "\n");

  int NumSources = 0;
  auto SE = PHIInfo.sources_end(DestReg);

  for (auto SRI = PHIInfo.sources_begin(DestReg); SRI != SE; ++SRI) {
    NumSources++;
  }

  if (NumSources == 1) {
    auto SRI = PHIInfo.sources_begin(DestReg);
    unsigned SourceReg = (*SRI).first;
    replaceRegisterWith(DestReg, SourceReg);
  } else {
    const DebugLoc &DL = Entry->findDebugLoc(Entry->begin());
    MachineInstrBuilder MIB = BuildMI(*Entry, Entry->instr_begin(), DL,
                                      TII->get(TargetOpcode::PHI), DestReg);
    LLVM_DEBUG(dbgs() << "Entry PHI " << printReg(DestReg, TRI) << " = PHI(");

    unsigned CurrentBackedgeReg = 0;

    for (auto SRI = PHIInfo.sources_begin(DestReg); SRI != SE; ++SRI) {
      unsigned SourceReg = (*SRI).first;

      if (CurrentRegion->contains((*SRI).second)) {
        if (CurrentBackedgeReg == 0) {
          CurrentBackedgeReg = SourceReg;
        } else {
          MachineInstr *PHIDefInstr = getDefInstr(SourceReg);
          MachineBasicBlock *PHIDefMBB = PHIDefInstr->getParent();
          const TargetRegisterClass *RegClass =
              MRI->getRegClass(CurrentBackedgeReg);
          unsigned NewBackedgeReg = MRI->createVirtualRegister(RegClass);
          MachineInstrBuilder BackedgePHI =
              BuildMI(*PHIDefMBB, PHIDefMBB->instr_begin(), DL,
                      TII->get(TargetOpcode::PHI), NewBackedgeReg);
          BackedgePHI.addReg(CurrentBackedgeReg);
          BackedgePHI.addMBB(getPHIPred(*PHIDefInstr, 0));
          BackedgePHI.addReg(getPHISourceReg(*PHIDefInstr, 1));
          BackedgePHI.addMBB((*SRI).second);
          CurrentBackedgeReg = NewBackedgeReg;
          LLVM_DEBUG(dbgs()
                     << "Inserting backedge PHI: "
                     << printReg(NewBackedgeReg, TRI) << " = PHI("
                     << printReg(CurrentBackedgeReg, TRI) << ", "
                     << printMBBReference(*getPHIPred(*PHIDefInstr, 0)) << ", "
                     << printReg(getPHISourceReg(*PHIDefInstr, 1), TRI) << ", "
                     << printMBBReference(*(*SRI).second));
        }
      } else {
        MIB.addReg(SourceReg);
        MIB.addMBB((*SRI).second);
        LLVM_DEBUG(dbgs() << printReg(SourceReg, TRI) << ", "
                          << printMBBReference(*(*SRI).second) << ", ");
      }
    }

    // Add the final backedge register source to the entry phi
    if (CurrentBackedgeReg != 0) {
      MIB.addReg(CurrentBackedgeReg);
      MIB.addMBB(Exit);
      LLVM_DEBUG(dbgs() << printReg(CurrentBackedgeReg, TRI) << ", "
                        << printMBBReference(*Exit) << ")\n");
    } else {
      LLVM_DEBUG(dbgs() << ")\n");
    }
  }
}

void AMDGPUMachineCFGStructurizer::createEntryPHIs(LinearizedRegion *CurrentRegion) {
  LLVM_DEBUG(PHIInfo.dump(MRI));

  for (auto DRI = PHIInfo.dests_begin(), DE = PHIInfo.dests_end(); DRI != DE;
       ++DRI) {

    unsigned DestReg = *DRI;
    createEntryPHI(CurrentRegion, DestReg);
  }
  PHIInfo.clear();
}

void AMDGPUMachineCFGStructurizer::replaceRegisterWith(unsigned Register,
                                                 unsigned NewRegister) {
  assert(Register != NewRegister && "Cannot replace a reg with itself");

  for (MachineRegisterInfo::reg_iterator I = MRI->reg_begin(Register),
                                         E = MRI->reg_end();
       I != E;) {
    MachineOperand &O = *I;
    ++I;
    if (TargetRegisterInfo::isPhysicalRegister(NewRegister)) {
      LLVM_DEBUG(dbgs() << "Trying to substitute physical register: "
                        << printReg(NewRegister, MRI->getTargetRegisterInfo())
                        << "\n");
      llvm_unreachable("Cannot substitute physical registers");
      // We don't handle physical registers, but if we need to
      // in the future This is how we do it:
      // O.substPhysReg(NewRegister, *TRI);
    } else {
      LLVM_DEBUG(dbgs() << "Replacing register: "
                        << printReg(Register, MRI->getTargetRegisterInfo())
                        << " with "
                        << printReg(NewRegister, MRI->getTargetRegisterInfo())
                        << "\n");
      O.setReg(NewRegister);
    }
  }
  PHIInfo.deleteDef(Register);

  getRegionMRT()->replaceLiveOutReg(Register, NewRegister);

  LLVM_DEBUG(PHIInfo.dump(MRI));
}

void AMDGPUMachineCFGStructurizer::resolvePHIInfos(MachineBasicBlock *FunctionEntry) {
  LLVM_DEBUG(dbgs() << "Resolve PHI Infos\n");
  LLVM_DEBUG(PHIInfo.dump(MRI));
  for (auto DRI = PHIInfo.dests_begin(), DE = PHIInfo.dests_end(); DRI != DE;
       ++DRI) {
    unsigned DestReg = *DRI;
    LLVM_DEBUG(dbgs() << "DestReg: " << printReg(DestReg, TRI) << "\n");
    auto SRI = PHIInfo.sources_begin(DestReg);
    unsigned SourceReg = (*SRI).first;
    LLVM_DEBUG(dbgs() << "DestReg: " << printReg(DestReg, TRI)
                      << " SourceReg: " << printReg(SourceReg, TRI) << "\n");

    assert(PHIInfo.sources_end(DestReg) == ++SRI &&
           "More than one phi source in entry node");
    replaceRegisterWith(DestReg, SourceReg);
  }
}

static bool isFunctionEntryBlock(MachineBasicBlock *MBB) {
  return ((&(*(MBB->getParent()->begin()))) == MBB);
}

MachineBasicBlock *AMDGPUMachineCFGStructurizer::createIfRegion(
    MachineBasicBlock *MergeBB, MachineBasicBlock *CodeBB,
    LinearizedRegion *CurrentRegion, unsigned BBSelectRegIn,
    unsigned BBSelectRegOut) {
  if (isFunctionEntryBlock(CodeBB) && !CurrentRegion->getHasLoop()) {
    // Handle non-loop function entry block.
    // We need to allow loops to the entry block and then
    rewriteCodeBBTerminator(CodeBB, MergeBB, BBSelectRegOut);
    resolvePHIInfos(CodeBB);
    removeExternalCFGSuccessors(CodeBB);
    CodeBB->addSuccessor(MergeBB);
    CurrentRegion->addMBB(CodeBB);
    return nullptr;
  }
  if (CurrentRegion->getEntry() == CodeBB && !CurrentRegion->getHasLoop()) {
    // Handle non-loop region entry block.
    MachineFunction *MF = MergeBB->getParent();
    auto MergeIter = MergeBB->getIterator();
    auto CodeBBStartIter = CodeBB->getIterator();
    auto CodeBBEndIter = ++(CodeBB->getIterator());
    if (CodeBBEndIter != MergeIter) {
      MF->splice(MergeIter, CodeBBStartIter, CodeBBEndIter);
    }
    rewriteCodeBBTerminator(CodeBB, MergeBB, BBSelectRegOut);
    prunePHIInfo(CodeBB);
    createEntryPHIs(CurrentRegion);
    removeExternalCFGSuccessors(CodeBB);
    CodeBB->addSuccessor(MergeBB);
    CurrentRegion->addMBB(CodeBB);
    return nullptr;
  } else {
    // Handle internal block.
    const TargetRegisterClass *RegClass = MRI->getRegClass(BBSelectRegIn);
    unsigned CodeBBSelectReg = MRI->createVirtualRegister(RegClass);
    rewriteCodeBBTerminator(CodeBB, MergeBB, CodeBBSelectReg);
    bool IsRegionEntryBB = CurrentRegion->getEntry() == CodeBB;
    MachineBasicBlock *IfBB = createIfBlock(MergeBB, CodeBB, CodeBB, CodeBB,
                                            BBSelectRegIn, IsRegionEntryBB);
    CurrentRegion->addMBB(IfBB);
    // If this is the entry block we need to make the If block the new
    // linearized region entry.
    if (IsRegionEntryBB) {
      CurrentRegion->setEntry(IfBB);

      if (CurrentRegion->getHasLoop()) {
        MachineBasicBlock *RegionExit = CurrentRegion->getExit();
        MachineBasicBlock *ETrueBB = nullptr;
        MachineBasicBlock *EFalseBB = nullptr;
        SmallVector<MachineOperand, 1> ECond;

        const DebugLoc &DL = DebugLoc();
        TII->analyzeBranch(*RegionExit, ETrueBB, EFalseBB, ECond);
        TII->removeBranch(*RegionExit);

        // We need to create a backedge if there is a loop
        unsigned Reg = TII->insertNE(
            RegionExit, RegionExit->instr_end(), DL,
            CurrentRegion->getRegionMRT()->getInnerOutputRegister(),
            CurrentRegion->getRegionMRT()->getEntry()->getNumber());
        MachineOperand RegOp =
            MachineOperand::CreateReg(Reg, false, false, true);
        ArrayRef<MachineOperand> Cond(RegOp);
        LLVM_DEBUG(dbgs() << "RegionExitReg: ");
        LLVM_DEBUG(Cond[0].print(dbgs(), TRI));
        LLVM_DEBUG(dbgs() << "\n");
        TII->insertBranch(*RegionExit, CurrentRegion->getEntry(), RegionExit,
                          Cond, DebugLoc());
        RegionExit->addSuccessor(CurrentRegion->getEntry());
      }
    }
    CurrentRegion->addMBB(CodeBB);
    LinearizedRegion InnerRegion(CodeBB, MRI, TRI, PHIInfo);

    InnerRegion.setParent(CurrentRegion);
    LLVM_DEBUG(dbgs() << "Insert BB Select PHI (BB)\n");
    insertMergePHI(IfBB, CodeBB, MergeBB, BBSelectRegOut, BBSelectRegIn,
                   CodeBBSelectReg);
    InnerRegion.addMBB(MergeBB);

    LLVM_DEBUG(InnerRegion.print(dbgs(), TRI));
    rewriteLiveOutRegs(IfBB, CodeBB, MergeBB, &InnerRegion, CurrentRegion);
    extractKilledPHIs(CodeBB);
    if (IsRegionEntryBB) {
      createEntryPHIs(CurrentRegion);
    }
    return IfBB;
  }
}

MachineBasicBlock *AMDGPUMachineCFGStructurizer::createIfRegion(
    MachineBasicBlock *MergeBB, LinearizedRegion *InnerRegion,
    LinearizedRegion *CurrentRegion, MachineBasicBlock *SelectBB,
    unsigned BBSelectRegIn, unsigned BBSelectRegOut) {
  unsigned CodeBBSelectReg =
      InnerRegion->getRegionMRT()->getInnerOutputRegister();
  MachineBasicBlock *CodeEntryBB = InnerRegion->getEntry();
  MachineBasicBlock *CodeExitBB = InnerRegion->getExit();
  MachineBasicBlock *IfBB = createIfBlock(MergeBB, CodeEntryBB, CodeExitBB,
                                          SelectBB, BBSelectRegIn, true);
  CurrentRegion->addMBB(IfBB);
  bool isEntry = CurrentRegion->getEntry() == InnerRegion->getEntry();
  if (isEntry) {

    if (CurrentRegion->getHasLoop()) {
      MachineBasicBlock *RegionExit = CurrentRegion->getExit();
      MachineBasicBlock *ETrueBB = nullptr;
      MachineBasicBlock *EFalseBB = nullptr;
      SmallVector<MachineOperand, 1> ECond;

      const DebugLoc &DL = DebugLoc();
      TII->analyzeBranch(*RegionExit, ETrueBB, EFalseBB, ECond);
      TII->removeBranch(*RegionExit);

      // We need to create a backedge if there is a loop
      unsigned Reg =
          TII->insertNE(RegionExit, RegionExit->instr_end(), DL,
                        CurrentRegion->getRegionMRT()->getInnerOutputRegister(),
                        CurrentRegion->getRegionMRT()->getEntry()->getNumber());
      MachineOperand RegOp = MachineOperand::CreateReg(Reg, false, false, true);
      ArrayRef<MachineOperand> Cond(RegOp);
      LLVM_DEBUG(dbgs() << "RegionExitReg: ");
      LLVM_DEBUG(Cond[0].print(dbgs(), TRI));
      LLVM_DEBUG(dbgs() << "\n");
      TII->insertBranch(*RegionExit, CurrentRegion->getEntry(), RegionExit,
                        Cond, DebugLoc());
      RegionExit->addSuccessor(IfBB);
    }
  }
  CurrentRegion->addMBBs(InnerRegion);
  LLVM_DEBUG(dbgs() << "Insert BB Select PHI (region)\n");
  insertMergePHI(IfBB, CodeExitBB, MergeBB, BBSelectRegOut, BBSelectRegIn,
                 CodeBBSelectReg);

  rewriteLiveOutRegs(IfBB, /* CodeEntryBB */ CodeExitBB, MergeBB, InnerRegion,
                     CurrentRegion);

  rewriteRegionEntryPHIs(InnerRegion, IfBB);

  if (isEntry) {
    CurrentRegion->setEntry(IfBB);
  }

  if (isEntry) {
    createEntryPHIs(CurrentRegion);
  }

  return IfBB;
}

void AMDGPUMachineCFGStructurizer::splitLoopPHI(MachineInstr &PHI,
                                          MachineBasicBlock *Entry,
                                          MachineBasicBlock *EntrySucc,
                                          LinearizedRegion *LRegion) {
  SmallVector<unsigned, 2> PHIRegionIndices;
  getPHIRegionIndices(LRegion, PHI, PHIRegionIndices);

  assert(PHIRegionIndices.size() == 1);

  unsigned RegionIndex = PHIRegionIndices[0];
  unsigned RegionSourceReg = getPHISourceReg(PHI, RegionIndex);
  MachineBasicBlock *RegionSourceMBB = getPHIPred(PHI, RegionIndex);
  unsigned PHIDest = getPHIDestReg(PHI);
  unsigned PHISource = PHIDest;
  unsigned ReplaceReg;

  if (shrinkPHI(PHI, PHIRegionIndices, &ReplaceReg)) {
    PHISource = ReplaceReg;
  }

  const TargetRegisterClass *RegClass = MRI->getRegClass(PHIDest);
  unsigned NewDestReg = MRI->createVirtualRegister(RegClass);
  LRegion->replaceRegisterInsideRegion(PHIDest, NewDestReg, false, MRI);
  MachineInstrBuilder MIB =
      BuildMI(*EntrySucc, EntrySucc->instr_begin(), PHI.getDebugLoc(),
              TII->get(TargetOpcode::PHI), NewDestReg);
  LLVM_DEBUG(dbgs() << "Split Entry PHI " << printReg(NewDestReg, TRI)
                    << " = PHI(");
  MIB.addReg(PHISource);
  MIB.addMBB(Entry);
  LLVM_DEBUG(dbgs() << printReg(PHISource, TRI) << ", "
                    << printMBBReference(*Entry));
  MIB.addReg(RegionSourceReg);
  MIB.addMBB(RegionSourceMBB);
  LLVM_DEBUG(dbgs() << " ," << printReg(RegionSourceReg, TRI) << ", "
                    << printMBBReference(*RegionSourceMBB) << ")\n");
}

void AMDGPUMachineCFGStructurizer::splitLoopPHIs(MachineBasicBlock *Entry,
                                           MachineBasicBlock *EntrySucc,
                                           LinearizedRegion *LRegion) {
  SmallVector<MachineInstr *, 2> PHIs;
  collectPHIs(Entry, PHIs);

  for (auto PHII : PHIs) {
    splitLoopPHI(*PHII, Entry, EntrySucc, LRegion);
  }
}

// Split the exit block so that we can insert a end control flow
MachineBasicBlock *
AMDGPUMachineCFGStructurizer::splitExit(LinearizedRegion *LRegion) {
  auto MRTRegion = LRegion->getRegionMRT();
  auto Exit = LRegion->getExit();
  auto MF = Exit->getParent();
  auto Succ = MRTRegion->getSucc();

  auto NewExit = MF->CreateMachineBasicBlock();
  auto AfterExitIter = Exit->getIterator();
  AfterExitIter++;
  MF->insert(AfterExitIter, NewExit);
  Exit->removeSuccessor(Succ);
  Exit->addSuccessor(NewExit);
  NewExit->addSuccessor(Succ);
  insertUnconditionalBranch(NewExit, Succ);
  LRegion->addMBB(NewExit);
  LRegion->setExit(NewExit);

  LLVM_DEBUG(dbgs() << "Created new exit block: " << NewExit->getNumber()
                    << "\n");

  // Replace any PHI Predecessors in the successor with NewExit
  for (auto &II : *Succ) {
    MachineInstr &Instr = II;

    // If we are past the PHI instructions we are done
    if (!Instr.isPHI())
      break;

    int numPreds = getPHINumInputs(Instr);
    for (int i = 0; i < numPreds; ++i) {
      auto Pred = getPHIPred(Instr, i);
      if (Pred == Exit) {
        setPhiPred(Instr, i, NewExit);
      }
    }
  }

  return NewExit;
}

static MachineBasicBlock *split(MachineBasicBlock::iterator I) {
  // Create the fall-through block.
  MachineBasicBlock *MBB = (*I).getParent();
  MachineFunction *MF = MBB->getParent();
  MachineBasicBlock *SuccMBB = MF->CreateMachineBasicBlock();
  auto MBBIter = ++(MBB->getIterator());
  MF->insert(MBBIter, SuccMBB);
  SuccMBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(SuccMBB);

  // Splice the code over.
  SuccMBB->splice(SuccMBB->end(), MBB, I, MBB->end());

  return SuccMBB;
}

// Split the entry block separating PHI-nodes and the rest of the code
// This is needed to insert an initializer for the bb select register
// inloop regions.

MachineBasicBlock *
AMDGPUMachineCFGStructurizer::splitEntry(LinearizedRegion *LRegion) {
  MachineBasicBlock *Entry = LRegion->getEntry();
  MachineBasicBlock *EntrySucc = split(Entry->getFirstNonPHI());
  MachineBasicBlock *Exit = LRegion->getExit();

  LLVM_DEBUG(dbgs() << "Split " << printMBBReference(*Entry) << " to "
                    << printMBBReference(*Entry) << " -> "
                    << printMBBReference(*EntrySucc) << "\n");
  LRegion->addMBB(EntrySucc);

  // Make the backedge go to Entry Succ
  if (Exit->isSuccessor(Entry)) {
    Exit->removeSuccessor(Entry);
  }
  Exit->addSuccessor(EntrySucc);
  MachineInstr &Branch = *(Exit->instr_rbegin());
  for (auto &UI : Branch.uses()) {
    if (UI.isMBB() && UI.getMBB() == Entry) {
      UI.setMBB(EntrySucc);
    }
  }

  splitLoopPHIs(Entry, EntrySucc, LRegion);

  return EntrySucc;
}

LinearizedRegion *
AMDGPUMachineCFGStructurizer::initLinearizedRegion(RegionMRT *Region) {
  LinearizedRegion *LRegion = Region->getLinearizedRegion();
  LRegion->initLiveOut(Region, MRI, TRI, PHIInfo);
  LRegion->setEntry(Region->getEntry());
  return LRegion;
}

static void removeOldExitPreds(RegionMRT *Region) {
  MachineBasicBlock *Exit = Region->getSucc();
  if (Exit == nullptr) {
    return;
  }
  for (MachineBasicBlock::pred_iterator PI = Exit->pred_begin(),
                                        E = Exit->pred_end();
       PI != E; ++PI) {
    if (Region->contains(*PI)) {
      (*PI)->removeSuccessor(Exit);
    }
  }
}

static bool mbbHasBackEdge(MachineBasicBlock *MBB,
                           SmallPtrSet<MachineBasicBlock *, 8> &MBBs) {
  for (auto SI = MBB->succ_begin(), SE = MBB->succ_end(); SI != SE; ++SI) {
    if (MBBs.count(*SI) != 0) {
      return true;
    }
  }
  return false;
}

static bool containsNewBackedge(MRT *Tree,
                                SmallPtrSet<MachineBasicBlock *, 8> &MBBs) {
  // Need to traverse this in reverse since it is in post order.
  if (Tree == nullptr)
    return false;

  if (Tree->isMBB()) {
    MachineBasicBlock *MBB = Tree->getMBBMRT()->getMBB();
    MBBs.insert(MBB);
    if (mbbHasBackEdge(MBB, MBBs)) {
      return true;
    }
  } else {
    RegionMRT *Region = Tree->getRegionMRT();
    SetVector<MRT *> *Children = Region->getChildren();
    for (auto CI = Children->rbegin(), CE = Children->rend(); CI != CE; ++CI) {
      if (containsNewBackedge(*CI, MBBs))
        return true;
    }
  }
  return false;
}

static bool containsNewBackedge(RegionMRT *Region) {
  SmallPtrSet<MachineBasicBlock *, 8> MBBs;
  return containsNewBackedge(Region, MBBs);
}

bool AMDGPUMachineCFGStructurizer::structurizeComplexRegion(RegionMRT *Region) {
  auto *LRegion = initLinearizedRegion(Region);
  LRegion->setHasLoop(containsNewBackedge(Region));
  MachineBasicBlock *LastMerge = createLinearizedExitBlock(Region);
  MachineBasicBlock *CurrentMerge = LastMerge;
  LRegion->addMBB(LastMerge);
  LRegion->setExit(LastMerge);

  rewriteRegionExitPHIs(Region, LastMerge, LRegion);
  removeOldExitPreds(Region);

  LLVM_DEBUG(PHIInfo.dump(MRI));

  SetVector<MRT *> *Children = Region->getChildren();
  LLVM_DEBUG(dbgs() << "===========If Region Start===============\n");
  if (LRegion->getHasLoop()) {
    LLVM_DEBUG(dbgs() << "Has Backedge: Yes\n");
  } else {
    LLVM_DEBUG(dbgs() << "Has Backedge: No\n");
  }

  unsigned BBSelectRegIn;
  unsigned BBSelectRegOut;
  for (auto CI = Children->begin(), CE = Children->end(); CI != CE; ++CI) {
    LLVM_DEBUG(dbgs() << "CurrentRegion: \n");
    LLVM_DEBUG(LRegion->print(dbgs(), TRI));

    auto CNI = CI;
    ++CNI;

    MRT *Child = (*CI);

    if (Child->isRegion()) {

      LinearizedRegion *InnerLRegion =
          Child->getRegionMRT()->getLinearizedRegion();
      // We found the block is the exit of an inner region, we need
      // to put it in the current linearized region.

      LLVM_DEBUG(dbgs() << "Linearizing region: ");
      LLVM_DEBUG(InnerLRegion->print(dbgs(), TRI));
      LLVM_DEBUG(dbgs() << "\n");

      MachineBasicBlock *InnerEntry = InnerLRegion->getEntry();
      if ((&(*(InnerEntry->getParent()->begin()))) == InnerEntry) {
        // Entry has already been linearized, no need to do this region.
        unsigned OuterSelect = InnerLRegion->getBBSelectRegOut();
        unsigned InnerSelectReg =
            InnerLRegion->getRegionMRT()->getInnerOutputRegister();
        replaceRegisterWith(InnerSelectReg, OuterSelect),
            resolvePHIInfos(InnerEntry);
        if (!InnerLRegion->getExit()->isSuccessor(CurrentMerge))
          InnerLRegion->getExit()->addSuccessor(CurrentMerge);
        continue;
      }

      BBSelectRegOut = Child->getBBSelectRegOut();
      BBSelectRegIn = Child->getBBSelectRegIn();

      LLVM_DEBUG(dbgs() << "BBSelectRegIn: " << printReg(BBSelectRegIn, TRI)
                        << "\n");
      LLVM_DEBUG(dbgs() << "BBSelectRegOut: " << printReg(BBSelectRegOut, TRI)
                        << "\n");

      MachineBasicBlock *IfEnd = CurrentMerge;
      CurrentMerge = createIfRegion(CurrentMerge, InnerLRegion, LRegion,
                                    Child->getRegionMRT()->getEntry(),
                                    BBSelectRegIn, BBSelectRegOut);
      TII->convertNonUniformIfRegion(CurrentMerge, IfEnd);
    } else {
      MachineBasicBlock *MBB = Child->getMBBMRT()->getMBB();
      LLVM_DEBUG(dbgs() << "Linearizing block: " << MBB->getNumber() << "\n");

      if (MBB == getSingleExitNode(*(MBB->getParent()))) {
        // If this is the exit block then we need to skip to the next.
        // The "in" register will be transferred to "out" in the next
        // iteration.
        continue;
      }

      BBSelectRegOut = Child->getBBSelectRegOut();
      BBSelectRegIn = Child->getBBSelectRegIn();

      LLVM_DEBUG(dbgs() << "BBSelectRegIn: " << printReg(BBSelectRegIn, TRI)
                        << "\n");
      LLVM_DEBUG(dbgs() << "BBSelectRegOut: " << printReg(BBSelectRegOut, TRI)
                        << "\n");

      MachineBasicBlock *IfEnd = CurrentMerge;
      // This is a basic block that is not part of an inner region, we
      // need to put it in the current linearized region.
      CurrentMerge = createIfRegion(CurrentMerge, MBB, LRegion, BBSelectRegIn,
                                    BBSelectRegOut);
      if (CurrentMerge) {
        TII->convertNonUniformIfRegion(CurrentMerge, IfEnd);
      }

      LLVM_DEBUG(PHIInfo.dump(MRI));
    }
  }

  LRegion->removeFalseRegisterKills(MRI);

  if (LRegion->getHasLoop()) {
    MachineBasicBlock *NewSucc = splitEntry(LRegion);
    if (isFunctionEntryBlock(LRegion->getEntry())) {
      resolvePHIInfos(LRegion->getEntry());
    }
    const DebugLoc &DL = NewSucc->findDebugLoc(NewSucc->getFirstNonPHI());
    unsigned InReg = LRegion->getBBSelectRegIn();
    unsigned InnerSelectReg =
        MRI->createVirtualRegister(MRI->getRegClass(InReg));
    unsigned NewInReg = MRI->createVirtualRegister(MRI->getRegClass(InReg));
    TII->materializeImmediate(*(LRegion->getEntry()),
                              LRegion->getEntry()->getFirstTerminator(), DL,
                              NewInReg, Region->getEntry()->getNumber());
    // Need to be careful about updating the registers inside the region.
    LRegion->replaceRegisterInsideRegion(InReg, InnerSelectReg, false, MRI);
    LLVM_DEBUG(dbgs() << "Loop BBSelect Merge PHI:\n");
    insertMergePHI(LRegion->getEntry(), LRegion->getExit(), NewSucc,
                   InnerSelectReg, NewInReg,
                   LRegion->getRegionMRT()->getInnerOutputRegister());
    splitExit(LRegion);
    TII->convertNonUniformLoopRegion(NewSucc, LastMerge);
  }

  if (Region->isRoot()) {
    TII->insertReturn(*LastMerge);
  }

  LLVM_DEBUG(Region->getEntry()->getParent()->dump());
  LLVM_DEBUG(LRegion->print(dbgs(), TRI));
  LLVM_DEBUG(PHIInfo.dump(MRI));

  LLVM_DEBUG(dbgs() << "===========If Region End===============\n");

  Region->setLinearizedRegion(LRegion);
  return true;
}

bool AMDGPUMachineCFGStructurizer::structurizeRegion(RegionMRT *Region) {
  if (false && regionIsSimpleIf(Region)) {
    transformSimpleIfRegion(Region);
    return true;
  } else if (regionIsSequence(Region)) {
    fixupRegionExits(Region);
    return false;
  } else {
    structurizeComplexRegion(Region);
  }
  return false;
}

static int structurize_once = 0;

bool AMDGPUMachineCFGStructurizer::structurizeRegions(RegionMRT *Region,
                                                bool isTopRegion) {
  bool Changed = false;

  auto Children = Region->getChildren();
  for (auto CI : *Children) {
    if (CI->isRegion()) {
      Changed |= structurizeRegions(CI->getRegionMRT(), false);
    }
  }

  if (structurize_once < 2 || true) {
    Changed |= structurizeRegion(Region);
    structurize_once++;
  }
  return Changed;
}

void AMDGPUMachineCFGStructurizer::initFallthroughMap(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "Fallthrough Map:\n");
  for (auto &MBBI : MF) {
    MachineBasicBlock *MBB = MBBI.getFallThrough();
    if (MBB != nullptr) {
      LLVM_DEBUG(dbgs() << "Fallthrough: " << MBBI.getNumber() << " -> "
                        << MBB->getNumber() << "\n");
    }
    FallthroughMap[&MBBI] = MBB;
  }
}

void AMDGPUMachineCFGStructurizer::createLinearizedRegion(RegionMRT *Region,
                                                    unsigned SelectOut) {
  LinearizedRegion *LRegion = new LinearizedRegion();
  if (SelectOut) {
    LRegion->addLiveOut(SelectOut);
    LLVM_DEBUG(dbgs() << "Add LiveOut (BBSelect): " << printReg(SelectOut, TRI)
                      << "\n");
  }
  LRegion->setRegionMRT(Region);
  Region->setLinearizedRegion(LRegion);
  LRegion->setParent(Region->getParent()
                         ? Region->getParent()->getLinearizedRegion()
                         : nullptr);
}

unsigned
AMDGPUMachineCFGStructurizer::initializeSelectRegisters(MRT *MRT, unsigned SelectOut,
                                                  MachineRegisterInfo *MRI,
                                                  const SIInstrInfo *TII) {
  if (MRT->isRegion()) {
    RegionMRT *Region = MRT->getRegionMRT();
    Region->setBBSelectRegOut(SelectOut);
    unsigned InnerSelectOut = createBBSelectReg(TII, MRI);

    // Fixme: Move linearization creation to the original spot
    createLinearizedRegion(Region, SelectOut);

    for (auto CI = Region->getChildren()->begin(),
              CE = Region->getChildren()->end();
         CI != CE; ++CI) {
      InnerSelectOut =
          initializeSelectRegisters((*CI), InnerSelectOut, MRI, TII);
    }
    MRT->setBBSelectRegIn(InnerSelectOut);
    return InnerSelectOut;
  } else {
    MRT->setBBSelectRegOut(SelectOut);
    unsigned NewSelectIn = createBBSelectReg(TII, MRI);
    MRT->setBBSelectRegIn(NewSelectIn);
    return NewSelectIn;
  }
}

static void checkRegOnlyPHIInputs(MachineFunction &MF) {
  for (auto &MBBI : MF) {
    for (MachineBasicBlock::instr_iterator I = MBBI.instr_begin(),
                                           E = MBBI.instr_end();
         I != E; ++I) {
      MachineInstr &Instr = *I;
      if (Instr.isPHI()) {
        int numPreds = getPHINumInputs(Instr);
        for (int i = 0; i < numPreds; ++i) {
          assert(Instr.getOperand(i * 2 + 1).isReg() &&
                 "PHI Operand not a register");
        }
      }
    }
  }
}

bool AMDGPUMachineCFGStructurizer::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MRI = &(MF.getRegInfo());
  initFallthroughMap(MF);

  checkRegOnlyPHIInputs(MF);
  LLVM_DEBUG(dbgs() << "----STRUCTURIZER START----\n");
  LLVM_DEBUG(MF.dump());

  Regions = &(getAnalysis<MachineRegionInfoPass>().getRegionInfo());
  LLVM_DEBUG(Regions->dump());

  RegionMRT *RTree = MRT::buildMRT(MF, Regions, TII, MRI);
  setRegionMRT(RTree);
  initializeSelectRegisters(RTree, 0, MRI, TII);
  LLVM_DEBUG(RTree->dump(TRI));
  bool result = structurizeRegions(RTree, true);
  delete RTree;
  LLVM_DEBUG(dbgs() << "----STRUCTURIZER END----\n");
  initFallthroughMap(MF);
  return result;
}

char AMDGPUMachineCFGStructurizerID = AMDGPUMachineCFGStructurizer::ID;

INITIALIZE_PASS_BEGIN(AMDGPUMachineCFGStructurizer, "amdgpu-machine-cfg-structurizer",
                      "AMDGPU Machine CFG Structurizer", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineRegionInfoPass)
INITIALIZE_PASS_END(AMDGPUMachineCFGStructurizer, "amdgpu-machine-cfg-structurizer",
                    "AMDGPU Machine CFG Structurizer", false, false)

FunctionPass *llvm::createAMDGPUMachineCFGStructurizerPass() {
  return new AMDGPUMachineCFGStructurizer();
}
