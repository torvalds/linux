//===- X86AvoidStoreForwardingBlockis.cpp - Avoid HW Store Forward Block --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// If a load follows a store and reloads data that the store has written to
// memory, Intel microarchitectures can in many cases forward the data directly
// from the store to the load, This "store forwarding" saves cycles by enabling
// the load to directly obtain the data instead of accessing the data from
// cache or memory.
// A "store forward block" occurs in cases that a store cannot be forwarded to
// the load. The most typical case of store forward block on Intel Core
// microarchitecture that a small store cannot be forwarded to a large load.
// The estimated penalty for a store forward block is ~13 cycles.
//
// This pass tries to recognize and handle cases where "store forward block"
// is created by the compiler when lowering memcpy calls to a sequence
// of a load and a store.
//
// The pass currently only handles cases where memcpy is lowered to
// XMM/YMM registers, it tries to break the memcpy into smaller copies.
// breaking the memcpy should be possible since there is no atomicity
// guarantee for loads and stores to XMM/YMM.
//
// It could be better for performance to solve the problem by loading
// to XMM/YMM then inserting the partial store before storing back from XMM/YMM
// to memory, but this will result in a more conservative optimization since it
// requires we prove that all memory accesses between the blocking store and the
// load must alias/don't alias before we can move the store, whereas the
// transformation done here is correct regardless to other memory accesses.
//===----------------------------------------------------------------------===//

#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCInstrDesc.h"

using namespace llvm;

#define DEBUG_TYPE "x86-avoid-SFB"

static cl::opt<bool> DisableX86AvoidStoreForwardBlocks(
    "x86-disable-avoid-SFB", cl::Hidden,
    cl::desc("X86: Disable Store Forwarding Blocks fixup."), cl::init(false));

static cl::opt<unsigned> X86AvoidSFBInspectionLimit(
    "x86-sfb-inspection-limit",
    cl::desc("X86: Number of instructions backward to "
             "inspect for store forwarding blocks."),
    cl::init(20), cl::Hidden);

namespace {

using DisplacementSizeMap = std::map<int64_t, unsigned>;

class X86AvoidSFBPass : public MachineFunctionPass {
public:
  static char ID;
  X86AvoidSFBPass() : MachineFunctionPass(ID) {
    initializeX86AvoidSFBPassPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "X86 Avoid Store Forwarding Blocks";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.addRequired<AAResultsWrapperPass>();
  }

private:
  MachineRegisterInfo *MRI;
  const X86InstrInfo *TII;
  const X86RegisterInfo *TRI;
  SmallVector<std::pair<MachineInstr *, MachineInstr *>, 2>
      BlockedLoadsStoresPairs;
  SmallVector<MachineInstr *, 2> ForRemoval;
  AliasAnalysis *AA;

  /// Returns couples of Load then Store to memory which look
  ///  like a memcpy.
  void findPotentiallylBlockedCopies(MachineFunction &MF);
  /// Break the memcpy's load and store into smaller copies
  /// such that each memory load that was blocked by a smaller store
  /// would now be copied separately.
  void breakBlockedCopies(MachineInstr *LoadInst, MachineInstr *StoreInst,
                          const DisplacementSizeMap &BlockingStoresDispSizeMap);
  /// Break a copy of size Size to smaller copies.
  void buildCopies(int Size, MachineInstr *LoadInst, int64_t LdDispImm,
                   MachineInstr *StoreInst, int64_t StDispImm,
                   int64_t LMMOffset, int64_t SMMOffset);

  void buildCopy(MachineInstr *LoadInst, unsigned NLoadOpcode, int64_t LoadDisp,
                 MachineInstr *StoreInst, unsigned NStoreOpcode,
                 int64_t StoreDisp, unsigned Size, int64_t LMMOffset,
                 int64_t SMMOffset);

  bool alias(const MachineMemOperand &Op1, const MachineMemOperand &Op2) const;

  unsigned getRegSizeInBytes(MachineInstr *Inst);
};

} // end anonymous namespace

char X86AvoidSFBPass::ID = 0;

INITIALIZE_PASS_BEGIN(X86AvoidSFBPass, DEBUG_TYPE, "Machine code sinking",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(X86AvoidSFBPass, DEBUG_TYPE, "Machine code sinking", false,
                    false)

FunctionPass *llvm::createX86AvoidStoreForwardingBlocks() {
  return new X86AvoidSFBPass();
}

static bool isXMMLoadOpcode(unsigned Opcode) {
  return Opcode == X86::MOVUPSrm || Opcode == X86::MOVAPSrm ||
         Opcode == X86::VMOVUPSrm || Opcode == X86::VMOVAPSrm ||
         Opcode == X86::VMOVUPDrm || Opcode == X86::VMOVAPDrm ||
         Opcode == X86::VMOVDQUrm || Opcode == X86::VMOVDQArm ||
         Opcode == X86::VMOVUPSZ128rm || Opcode == X86::VMOVAPSZ128rm ||
         Opcode == X86::VMOVUPDZ128rm || Opcode == X86::VMOVAPDZ128rm ||
         Opcode == X86::VMOVDQU64Z128rm || Opcode == X86::VMOVDQA64Z128rm ||
         Opcode == X86::VMOVDQU32Z128rm || Opcode == X86::VMOVDQA32Z128rm;
}
static bool isYMMLoadOpcode(unsigned Opcode) {
  return Opcode == X86::VMOVUPSYrm || Opcode == X86::VMOVAPSYrm ||
         Opcode == X86::VMOVUPDYrm || Opcode == X86::VMOVAPDYrm ||
         Opcode == X86::VMOVDQUYrm || Opcode == X86::VMOVDQAYrm ||
         Opcode == X86::VMOVUPSZ256rm || Opcode == X86::VMOVAPSZ256rm ||
         Opcode == X86::VMOVUPDZ256rm || Opcode == X86::VMOVAPDZ256rm ||
         Opcode == X86::VMOVDQU64Z256rm || Opcode == X86::VMOVDQA64Z256rm ||
         Opcode == X86::VMOVDQU32Z256rm || Opcode == X86::VMOVDQA32Z256rm;
}

static bool isPotentialBlockedMemCpyLd(unsigned Opcode) {
  return isXMMLoadOpcode(Opcode) || isYMMLoadOpcode(Opcode);
}

static bool isPotentialBlockedMemCpyPair(int LdOpcode, int StOpcode) {
  switch (LdOpcode) {
  case X86::MOVUPSrm:
  case X86::MOVAPSrm:
    return StOpcode == X86::MOVUPSmr || StOpcode == X86::MOVAPSmr;
  case X86::VMOVUPSrm:
  case X86::VMOVAPSrm:
    return StOpcode == X86::VMOVUPSmr || StOpcode == X86::VMOVAPSmr;
  case X86::VMOVUPDrm:
  case X86::VMOVAPDrm:
    return StOpcode == X86::VMOVUPDmr || StOpcode == X86::VMOVAPDmr;
  case X86::VMOVDQUrm:
  case X86::VMOVDQArm:
    return StOpcode == X86::VMOVDQUmr || StOpcode == X86::VMOVDQAmr;
  case X86::VMOVUPSZ128rm:
  case X86::VMOVAPSZ128rm:
    return StOpcode == X86::VMOVUPSZ128mr || StOpcode == X86::VMOVAPSZ128mr;
  case X86::VMOVUPDZ128rm:
  case X86::VMOVAPDZ128rm:
    return StOpcode == X86::VMOVUPDZ128mr || StOpcode == X86::VMOVAPDZ128mr;
  case X86::VMOVUPSYrm:
  case X86::VMOVAPSYrm:
    return StOpcode == X86::VMOVUPSYmr || StOpcode == X86::VMOVAPSYmr;
  case X86::VMOVUPDYrm:
  case X86::VMOVAPDYrm:
    return StOpcode == X86::VMOVUPDYmr || StOpcode == X86::VMOVAPDYmr;
  case X86::VMOVDQUYrm:
  case X86::VMOVDQAYrm:
    return StOpcode == X86::VMOVDQUYmr || StOpcode == X86::VMOVDQAYmr;
  case X86::VMOVUPSZ256rm:
  case X86::VMOVAPSZ256rm:
    return StOpcode == X86::VMOVUPSZ256mr || StOpcode == X86::VMOVAPSZ256mr;
  case X86::VMOVUPDZ256rm:
  case X86::VMOVAPDZ256rm:
    return StOpcode == X86::VMOVUPDZ256mr || StOpcode == X86::VMOVAPDZ256mr;
  case X86::VMOVDQU64Z128rm:
  case X86::VMOVDQA64Z128rm:
    return StOpcode == X86::VMOVDQU64Z128mr || StOpcode == X86::VMOVDQA64Z128mr;
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQA32Z128rm:
    return StOpcode == X86::VMOVDQU32Z128mr || StOpcode == X86::VMOVDQA32Z128mr;
  case X86::VMOVDQU64Z256rm:
  case X86::VMOVDQA64Z256rm:
    return StOpcode == X86::VMOVDQU64Z256mr || StOpcode == X86::VMOVDQA64Z256mr;
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQA32Z256rm:
    return StOpcode == X86::VMOVDQU32Z256mr || StOpcode == X86::VMOVDQA32Z256mr;
  default:
    return false;
  }
}

static bool isPotentialBlockingStoreInst(int Opcode, int LoadOpcode) {
  bool PBlock = false;
  PBlock |= Opcode == X86::MOV64mr || Opcode == X86::MOV64mi32 ||
            Opcode == X86::MOV32mr || Opcode == X86::MOV32mi ||
            Opcode == X86::MOV16mr || Opcode == X86::MOV16mi ||
            Opcode == X86::MOV8mr || Opcode == X86::MOV8mi;
  if (isYMMLoadOpcode(LoadOpcode))
    PBlock |= Opcode == X86::VMOVUPSmr || Opcode == X86::VMOVAPSmr ||
              Opcode == X86::VMOVUPDmr || Opcode == X86::VMOVAPDmr ||
              Opcode == X86::VMOVDQUmr || Opcode == X86::VMOVDQAmr ||
              Opcode == X86::VMOVUPSZ128mr || Opcode == X86::VMOVAPSZ128mr ||
              Opcode == X86::VMOVUPDZ128mr || Opcode == X86::VMOVAPDZ128mr ||
              Opcode == X86::VMOVDQU64Z128mr ||
              Opcode == X86::VMOVDQA64Z128mr ||
              Opcode == X86::VMOVDQU32Z128mr || Opcode == X86::VMOVDQA32Z128mr;
  return PBlock;
}

static const int MOV128SZ = 16;
static const int MOV64SZ = 8;
static const int MOV32SZ = 4;
static const int MOV16SZ = 2;
static const int MOV8SZ = 1;

static unsigned getYMMtoXMMLoadOpcode(unsigned LoadOpcode) {
  switch (LoadOpcode) {
  case X86::VMOVUPSYrm:
  case X86::VMOVAPSYrm:
    return X86::VMOVUPSrm;
  case X86::VMOVUPDYrm:
  case X86::VMOVAPDYrm:
    return X86::VMOVUPDrm;
  case X86::VMOVDQUYrm:
  case X86::VMOVDQAYrm:
    return X86::VMOVDQUrm;
  case X86::VMOVUPSZ256rm:
  case X86::VMOVAPSZ256rm:
    return X86::VMOVUPSZ128rm;
  case X86::VMOVUPDZ256rm:
  case X86::VMOVAPDZ256rm:
    return X86::VMOVUPDZ128rm;
  case X86::VMOVDQU64Z256rm:
  case X86::VMOVDQA64Z256rm:
    return X86::VMOVDQU64Z128rm;
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQA32Z256rm:
    return X86::VMOVDQU32Z128rm;
  default:
    llvm_unreachable("Unexpected Load Instruction Opcode");
  }
  return 0;
}

static unsigned getYMMtoXMMStoreOpcode(unsigned StoreOpcode) {
  switch (StoreOpcode) {
  case X86::VMOVUPSYmr:
  case X86::VMOVAPSYmr:
    return X86::VMOVUPSmr;
  case X86::VMOVUPDYmr:
  case X86::VMOVAPDYmr:
    return X86::VMOVUPDmr;
  case X86::VMOVDQUYmr:
  case X86::VMOVDQAYmr:
    return X86::VMOVDQUmr;
  case X86::VMOVUPSZ256mr:
  case X86::VMOVAPSZ256mr:
    return X86::VMOVUPSZ128mr;
  case X86::VMOVUPDZ256mr:
  case X86::VMOVAPDZ256mr:
    return X86::VMOVUPDZ128mr;
  case X86::VMOVDQU64Z256mr:
  case X86::VMOVDQA64Z256mr:
    return X86::VMOVDQU64Z128mr;
  case X86::VMOVDQU32Z256mr:
  case X86::VMOVDQA32Z256mr:
    return X86::VMOVDQU32Z128mr;
  default:
    llvm_unreachable("Unexpected Load Instruction Opcode");
  }
  return 0;
}

static int getAddrOffset(MachineInstr *MI) {
  const MCInstrDesc &Descl = MI->getDesc();
  int AddrOffset = X86II::getMemoryOperandNo(Descl.TSFlags);
  assert(AddrOffset != -1 && "Expected Memory Operand");
  AddrOffset += X86II::getOperandBias(Descl);
  return AddrOffset;
}

static MachineOperand &getBaseOperand(MachineInstr *MI) {
  int AddrOffset = getAddrOffset(MI);
  return MI->getOperand(AddrOffset + X86::AddrBaseReg);
}

static MachineOperand &getDispOperand(MachineInstr *MI) {
  int AddrOffset = getAddrOffset(MI);
  return MI->getOperand(AddrOffset + X86::AddrDisp);
}

// Relevant addressing modes contain only base register and immediate
// displacement or frameindex and immediate displacement.
// TODO: Consider expanding to other addressing modes in the future
static bool isRelevantAddressingMode(MachineInstr *MI) {
  int AddrOffset = getAddrOffset(MI);
  MachineOperand &Base = getBaseOperand(MI);
  MachineOperand &Disp = getDispOperand(MI);
  MachineOperand &Scale = MI->getOperand(AddrOffset + X86::AddrScaleAmt);
  MachineOperand &Index = MI->getOperand(AddrOffset + X86::AddrIndexReg);
  MachineOperand &Segment = MI->getOperand(AddrOffset + X86::AddrSegmentReg);

  if (!((Base.isReg() && Base.getReg() != X86::NoRegister) || Base.isFI()))
    return false;
  if (!Disp.isImm())
    return false;
  if (Scale.getImm() != 1)
    return false;
  if (!(Index.isReg() && Index.getReg() == X86::NoRegister))
    return false;
  if (!(Segment.isReg() && Segment.getReg() == X86::NoRegister))
    return false;
  return true;
}

// Collect potentially blocking stores.
// Limit the number of instructions backwards we want to inspect
// since the effect of store block won't be visible if the store
// and load instructions have enough instructions in between to
// keep the core busy.
static SmallVector<MachineInstr *, 2>
findPotentialBlockers(MachineInstr *LoadInst) {
  SmallVector<MachineInstr *, 2> PotentialBlockers;
  unsigned BlockCount = 0;
  const unsigned InspectionLimit = X86AvoidSFBInspectionLimit;
  for (auto PBInst = std::next(MachineBasicBlock::reverse_iterator(LoadInst)),
            E = LoadInst->getParent()->rend();
       PBInst != E; ++PBInst) {
    BlockCount++;
    if (BlockCount >= InspectionLimit)
      break;
    MachineInstr &MI = *PBInst;
    if (MI.getDesc().isCall())
      return PotentialBlockers;
    PotentialBlockers.push_back(&MI);
  }
  // If we didn't get to the instructions limit try predecessing blocks.
  // Ideally we should traverse the predecessor blocks in depth with some
  // coloring algorithm, but for now let's just look at the first order
  // predecessors.
  if (BlockCount < InspectionLimit) {
    MachineBasicBlock *MBB = LoadInst->getParent();
    int LimitLeft = InspectionLimit - BlockCount;
    for (MachineBasicBlock::pred_iterator PB = MBB->pred_begin(),
                                          PE = MBB->pred_end();
         PB != PE; ++PB) {
      MachineBasicBlock *PMBB = *PB;
      int PredCount = 0;
      for (MachineBasicBlock::reverse_iterator PBInst = PMBB->rbegin(),
                                               PME = PMBB->rend();
           PBInst != PME; ++PBInst) {
        PredCount++;
        if (PredCount >= LimitLeft)
          break;
        if (PBInst->getDesc().isCall())
          break;
        PotentialBlockers.push_back(&*PBInst);
      }
    }
  }
  return PotentialBlockers;
}

void X86AvoidSFBPass::buildCopy(MachineInstr *LoadInst, unsigned NLoadOpcode,
                                int64_t LoadDisp, MachineInstr *StoreInst,
                                unsigned NStoreOpcode, int64_t StoreDisp,
                                unsigned Size, int64_t LMMOffset,
                                int64_t SMMOffset) {
  MachineOperand &LoadBase = getBaseOperand(LoadInst);
  MachineOperand &StoreBase = getBaseOperand(StoreInst);
  MachineBasicBlock *MBB = LoadInst->getParent();
  MachineMemOperand *LMMO = *LoadInst->memoperands_begin();
  MachineMemOperand *SMMO = *StoreInst->memoperands_begin();

  unsigned Reg1 = MRI->createVirtualRegister(
      TII->getRegClass(TII->get(NLoadOpcode), 0, TRI, *(MBB->getParent())));
  MachineInstr *NewLoad =
      BuildMI(*MBB, LoadInst, LoadInst->getDebugLoc(), TII->get(NLoadOpcode),
              Reg1)
          .add(LoadBase)
          .addImm(1)
          .addReg(X86::NoRegister)
          .addImm(LoadDisp)
          .addReg(X86::NoRegister)
          .addMemOperand(
              MBB->getParent()->getMachineMemOperand(LMMO, LMMOffset, Size));
  if (LoadBase.isReg())
    getBaseOperand(NewLoad).setIsKill(false);
  LLVM_DEBUG(NewLoad->dump());
  // If the load and store are consecutive, use the loadInst location to
  // reduce register pressure.
  MachineInstr *StInst = StoreInst;
  if (StoreInst->getPrevNode() == LoadInst)
    StInst = LoadInst;
  MachineInstr *NewStore =
      BuildMI(*MBB, StInst, StInst->getDebugLoc(), TII->get(NStoreOpcode))
          .add(StoreBase)
          .addImm(1)
          .addReg(X86::NoRegister)
          .addImm(StoreDisp)
          .addReg(X86::NoRegister)
          .addReg(Reg1)
          .addMemOperand(
              MBB->getParent()->getMachineMemOperand(SMMO, SMMOffset, Size));
  if (StoreBase.isReg())
    getBaseOperand(NewStore).setIsKill(false);
  MachineOperand &StoreSrcVReg = StoreInst->getOperand(X86::AddrNumOperands);
  assert(StoreSrcVReg.isReg() && "Expected virtual register");
  NewStore->getOperand(X86::AddrNumOperands).setIsKill(StoreSrcVReg.isKill());
  LLVM_DEBUG(NewStore->dump());
}

void X86AvoidSFBPass::buildCopies(int Size, MachineInstr *LoadInst,
                                  int64_t LdDispImm, MachineInstr *StoreInst,
                                  int64_t StDispImm, int64_t LMMOffset,
                                  int64_t SMMOffset) {
  int LdDisp = LdDispImm;
  int StDisp = StDispImm;
  while (Size > 0) {
    if ((Size - MOV128SZ >= 0) && isYMMLoadOpcode(LoadInst->getOpcode())) {
      Size = Size - MOV128SZ;
      buildCopy(LoadInst, getYMMtoXMMLoadOpcode(LoadInst->getOpcode()), LdDisp,
                StoreInst, getYMMtoXMMStoreOpcode(StoreInst->getOpcode()),
                StDisp, MOV128SZ, LMMOffset, SMMOffset);
      LdDisp += MOV128SZ;
      StDisp += MOV128SZ;
      LMMOffset += MOV128SZ;
      SMMOffset += MOV128SZ;
      continue;
    }
    if (Size - MOV64SZ >= 0) {
      Size = Size - MOV64SZ;
      buildCopy(LoadInst, X86::MOV64rm, LdDisp, StoreInst, X86::MOV64mr, StDisp,
                MOV64SZ, LMMOffset, SMMOffset);
      LdDisp += MOV64SZ;
      StDisp += MOV64SZ;
      LMMOffset += MOV64SZ;
      SMMOffset += MOV64SZ;
      continue;
    }
    if (Size - MOV32SZ >= 0) {
      Size = Size - MOV32SZ;
      buildCopy(LoadInst, X86::MOV32rm, LdDisp, StoreInst, X86::MOV32mr, StDisp,
                MOV32SZ, LMMOffset, SMMOffset);
      LdDisp += MOV32SZ;
      StDisp += MOV32SZ;
      LMMOffset += MOV32SZ;
      SMMOffset += MOV32SZ;
      continue;
    }
    if (Size - MOV16SZ >= 0) {
      Size = Size - MOV16SZ;
      buildCopy(LoadInst, X86::MOV16rm, LdDisp, StoreInst, X86::MOV16mr, StDisp,
                MOV16SZ, LMMOffset, SMMOffset);
      LdDisp += MOV16SZ;
      StDisp += MOV16SZ;
      LMMOffset += MOV16SZ;
      SMMOffset += MOV16SZ;
      continue;
    }
    if (Size - MOV8SZ >= 0) {
      Size = Size - MOV8SZ;
      buildCopy(LoadInst, X86::MOV8rm, LdDisp, StoreInst, X86::MOV8mr, StDisp,
                MOV8SZ, LMMOffset, SMMOffset);
      LdDisp += MOV8SZ;
      StDisp += MOV8SZ;
      LMMOffset += MOV8SZ;
      SMMOffset += MOV8SZ;
      continue;
    }
  }
  assert(Size == 0 && "Wrong size division");
}

static void updateKillStatus(MachineInstr *LoadInst, MachineInstr *StoreInst) {
  MachineOperand &LoadBase = getBaseOperand(LoadInst);
  MachineOperand &StoreBase = getBaseOperand(StoreInst);
  if (LoadBase.isReg()) {
    MachineInstr *LastLoad = LoadInst->getPrevNode();
    // If the original load and store to xmm/ymm were consecutive
    // then the partial copies were also created in
    // a consecutive order to reduce register pressure,
    // and the location of the last load is before the last store.
    if (StoreInst->getPrevNode() == LoadInst)
      LastLoad = LoadInst->getPrevNode()->getPrevNode();
    getBaseOperand(LastLoad).setIsKill(LoadBase.isKill());
  }
  if (StoreBase.isReg()) {
    MachineInstr *StInst = StoreInst;
    if (StoreInst->getPrevNode() == LoadInst)
      StInst = LoadInst;
    getBaseOperand(StInst->getPrevNode()).setIsKill(StoreBase.isKill());
  }
}

bool X86AvoidSFBPass::alias(const MachineMemOperand &Op1,
                            const MachineMemOperand &Op2) const {
  if (!Op1.getValue() || !Op2.getValue())
    return true;

  int64_t MinOffset = std::min(Op1.getOffset(), Op2.getOffset());
  int64_t Overlapa = Op1.getSize() + Op1.getOffset() - MinOffset;
  int64_t Overlapb = Op2.getSize() + Op2.getOffset() - MinOffset;

  AliasResult AAResult =
      AA->alias(MemoryLocation(Op1.getValue(), Overlapa, Op1.getAAInfo()),
                MemoryLocation(Op2.getValue(), Overlapb, Op2.getAAInfo()));
  return AAResult != NoAlias;
}

void X86AvoidSFBPass::findPotentiallylBlockedCopies(MachineFunction &MF) {
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      if (!isPotentialBlockedMemCpyLd(MI.getOpcode()))
        continue;
      int DefVR = MI.getOperand(0).getReg();
      if (!MRI->hasOneUse(DefVR))
        continue;
      for (auto UI = MRI->use_nodbg_begin(DefVR), UE = MRI->use_nodbg_end();
           UI != UE;) {
        MachineOperand &StoreMO = *UI++;
        MachineInstr &StoreMI = *StoreMO.getParent();
        // Skip cases where the memcpy may overlap.
        if (StoreMI.getParent() == MI.getParent() &&
            isPotentialBlockedMemCpyPair(MI.getOpcode(), StoreMI.getOpcode()) &&
            isRelevantAddressingMode(&MI) &&
            isRelevantAddressingMode(&StoreMI)) {
          assert(MI.hasOneMemOperand() &&
                 "Expected one memory operand for load instruction");
          assert(StoreMI.hasOneMemOperand() &&
                 "Expected one memory operand for store instruction");
          if (!alias(**MI.memoperands_begin(), **StoreMI.memoperands_begin()))
            BlockedLoadsStoresPairs.push_back(std::make_pair(&MI, &StoreMI));
        }
      }
    }
}

unsigned X86AvoidSFBPass::getRegSizeInBytes(MachineInstr *LoadInst) {
  auto TRC = TII->getRegClass(TII->get(LoadInst->getOpcode()), 0, TRI,
                              *LoadInst->getParent()->getParent());
  return TRI->getRegSizeInBits(*TRC) / 8;
}

void X86AvoidSFBPass::breakBlockedCopies(
    MachineInstr *LoadInst, MachineInstr *StoreInst,
    const DisplacementSizeMap &BlockingStoresDispSizeMap) {
  int64_t LdDispImm = getDispOperand(LoadInst).getImm();
  int64_t StDispImm = getDispOperand(StoreInst).getImm();
  int64_t LMMOffset = 0;
  int64_t SMMOffset = 0;

  int64_t LdDisp1 = LdDispImm;
  int64_t LdDisp2 = 0;
  int64_t StDisp1 = StDispImm;
  int64_t StDisp2 = 0;
  unsigned Size1 = 0;
  unsigned Size2 = 0;
  int64_t LdStDelta = StDispImm - LdDispImm;

  for (auto DispSizePair : BlockingStoresDispSizeMap) {
    LdDisp2 = DispSizePair.first;
    StDisp2 = DispSizePair.first + LdStDelta;
    Size2 = DispSizePair.second;
    // Avoid copying overlapping areas.
    if (LdDisp2 < LdDisp1) {
      int OverlapDelta = LdDisp1 - LdDisp2;
      LdDisp2 += OverlapDelta;
      StDisp2 += OverlapDelta;
      Size2 -= OverlapDelta;
    }
    Size1 = LdDisp2 - LdDisp1;

    // Build a copy for the point until the current blocking store's
    // displacement.
    buildCopies(Size1, LoadInst, LdDisp1, StoreInst, StDisp1, LMMOffset,
                SMMOffset);
    // Build a copy for the current blocking store.
    buildCopies(Size2, LoadInst, LdDisp2, StoreInst, StDisp2, LMMOffset + Size1,
                SMMOffset + Size1);
    LdDisp1 = LdDisp2 + Size2;
    StDisp1 = StDisp2 + Size2;
    LMMOffset += Size1 + Size2;
    SMMOffset += Size1 + Size2;
  }
  unsigned Size3 = (LdDispImm + getRegSizeInBytes(LoadInst)) - LdDisp1;
  buildCopies(Size3, LoadInst, LdDisp1, StoreInst, StDisp1, LMMOffset,
              LMMOffset);
}

static bool hasSameBaseOpValue(MachineInstr *LoadInst,
                               MachineInstr *StoreInst) {
  MachineOperand &LoadBase = getBaseOperand(LoadInst);
  MachineOperand &StoreBase = getBaseOperand(StoreInst);
  if (LoadBase.isReg() != StoreBase.isReg())
    return false;
  if (LoadBase.isReg())
    return LoadBase.getReg() == StoreBase.getReg();
  return LoadBase.getIndex() == StoreBase.getIndex();
}

static bool isBlockingStore(int64_t LoadDispImm, unsigned LoadSize,
                            int64_t StoreDispImm, unsigned StoreSize) {
  return ((StoreDispImm >= LoadDispImm) &&
          (StoreDispImm <= LoadDispImm + (LoadSize - StoreSize)));
}

// Keep track of all stores blocking a load
static void
updateBlockingStoresDispSizeMap(DisplacementSizeMap &BlockingStoresDispSizeMap,
                                int64_t DispImm, unsigned Size) {
  if (BlockingStoresDispSizeMap.count(DispImm)) {
    // Choose the smallest blocking store starting at this displacement.
    if (BlockingStoresDispSizeMap[DispImm] > Size)
      BlockingStoresDispSizeMap[DispImm] = Size;

  } else
    BlockingStoresDispSizeMap[DispImm] = Size;
}

// Remove blocking stores contained in each other.
static void
removeRedundantBlockingStores(DisplacementSizeMap &BlockingStoresDispSizeMap) {
  if (BlockingStoresDispSizeMap.size() <= 1)
    return;

  SmallVector<std::pair<int64_t, unsigned>, 0> DispSizeStack;
  for (auto DispSizePair : BlockingStoresDispSizeMap) {
    int64_t CurrDisp = DispSizePair.first;
    unsigned CurrSize = DispSizePair.second;
    while (DispSizeStack.size()) {
      int64_t PrevDisp = DispSizeStack.back().first;
      unsigned PrevSize = DispSizeStack.back().second;
      if (CurrDisp + CurrSize > PrevDisp + PrevSize)
        break;
      DispSizeStack.pop_back();
    }
    DispSizeStack.push_back(DispSizePair);
  }
  BlockingStoresDispSizeMap.clear();
  for (auto Disp : DispSizeStack)
    BlockingStoresDispSizeMap.insert(Disp);
}

bool X86AvoidSFBPass::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;

  if (DisableX86AvoidStoreForwardBlocks || skipFunction(MF.getFunction()) ||
      !MF.getSubtarget<X86Subtarget>().is64Bit())
    return false;

  MRI = &MF.getRegInfo();
  assert(MRI->isSSA() && "Expected MIR to be in SSA form");
  TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
  TRI = MF.getSubtarget<X86Subtarget>().getRegisterInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  LLVM_DEBUG(dbgs() << "Start X86AvoidStoreForwardBlocks\n";);
  // Look for a load then a store to XMM/YMM which look like a memcpy
  findPotentiallylBlockedCopies(MF);

  for (auto LoadStoreInstPair : BlockedLoadsStoresPairs) {
    MachineInstr *LoadInst = LoadStoreInstPair.first;
    int64_t LdDispImm = getDispOperand(LoadInst).getImm();
    DisplacementSizeMap BlockingStoresDispSizeMap;

    SmallVector<MachineInstr *, 2> PotentialBlockers =
        findPotentialBlockers(LoadInst);
    for (auto PBInst : PotentialBlockers) {
      if (!isPotentialBlockingStoreInst(PBInst->getOpcode(),
                                        LoadInst->getOpcode()) ||
          !isRelevantAddressingMode(PBInst))
        continue;
      int64_t PBstDispImm = getDispOperand(PBInst).getImm();
      assert(PBInst->hasOneMemOperand() && "Expected One Memory Operand");
      unsigned PBstSize = (*PBInst->memoperands_begin())->getSize();
      // This check doesn't cover all cases, but it will suffice for now.
      // TODO: take branch probability into consideration, if the blocking
      // store is in an unreached block, breaking the memcopy could lose
      // performance.
      if (hasSameBaseOpValue(LoadInst, PBInst) &&
          isBlockingStore(LdDispImm, getRegSizeInBytes(LoadInst), PBstDispImm,
                          PBstSize))
        updateBlockingStoresDispSizeMap(BlockingStoresDispSizeMap, PBstDispImm,
                                        PBstSize);
    }

    if (BlockingStoresDispSizeMap.empty())
      continue;

    // We found a store forward block, break the memcpy's load and store
    // into smaller copies such that each smaller store that was causing
    // a store block would now be copied separately.
    MachineInstr *StoreInst = LoadStoreInstPair.second;
    LLVM_DEBUG(dbgs() << "Blocked load and store instructions: \n");
    LLVM_DEBUG(LoadInst->dump());
    LLVM_DEBUG(StoreInst->dump());
    LLVM_DEBUG(dbgs() << "Replaced with:\n");
    removeRedundantBlockingStores(BlockingStoresDispSizeMap);
    breakBlockedCopies(LoadInst, StoreInst, BlockingStoresDispSizeMap);
    updateKillStatus(LoadInst, StoreInst);
    ForRemoval.push_back(LoadInst);
    ForRemoval.push_back(StoreInst);
  }
  for (auto RemovedInst : ForRemoval) {
    RemovedInst->eraseFromParent();
  }
  ForRemoval.clear();
  BlockedLoadsStoresPairs.clear();
  LLVM_DEBUG(dbgs() << "End X86AvoidStoreForwardBlocks\n";);

  return Changed;
}
