//===- ARMLoadStoreOptimizer.cpp - ARM load / store opt. pass -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains a pass that performs load / store related peephole
/// optimizations. This pass should be run after register allocation.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMISelLowering.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "arm-ldst-opt"

STATISTIC(NumLDMGened , "Number of ldm instructions generated");
STATISTIC(NumSTMGened , "Number of stm instructions generated");
STATISTIC(NumVLDMGened, "Number of vldm instructions generated");
STATISTIC(NumVSTMGened, "Number of vstm instructions generated");
STATISTIC(NumLdStMoved, "Number of load / store instructions moved");
STATISTIC(NumLDRDFormed,"Number of ldrd created before allocation");
STATISTIC(NumSTRDFormed,"Number of strd created before allocation");
STATISTIC(NumLDRD2LDM,  "Number of ldrd instructions turned back into ldm");
STATISTIC(NumSTRD2STM,  "Number of strd instructions turned back into stm");
STATISTIC(NumLDRD2LDR,  "Number of ldrd instructions turned back into ldr's");
STATISTIC(NumSTRD2STR,  "Number of strd instructions turned back into str's");

/// This switch disables formation of double/multi instructions that could
/// potentially lead to (new) alignment traps even with CCR.UNALIGN_TRP
/// disabled. This can be used to create libraries that are robust even when
/// users provoke undefined behaviour by supplying misaligned pointers.
/// \see mayCombineMisaligned()
static cl::opt<bool>
AssumeMisalignedLoadStores("arm-assume-misaligned-load-store", cl::Hidden,
    cl::init(false), cl::desc("Be more conservative in ARM load/store opt"));

#define ARM_LOAD_STORE_OPT_NAME "ARM load / store optimization pass"

namespace {

  /// Post- register allocation pass the combine load / store instructions to
  /// form ldm / stm instructions.
  struct ARMLoadStoreOpt : public MachineFunctionPass {
    static char ID;

    const MachineFunction *MF;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const ARMSubtarget *STI;
    const TargetLowering *TL;
    ARMFunctionInfo *AFI;
    LivePhysRegs LiveRegs;
    RegisterClassInfo RegClassInfo;
    MachineBasicBlock::const_iterator LiveRegPos;
    bool LiveRegsValid;
    bool RegClassInfoValid;
    bool isThumb1, isThumb2;

    ARMLoadStoreOpt() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override { return ARM_LOAD_STORE_OPT_NAME; }

  private:
    /// A set of load/store MachineInstrs with same base register sorted by
    /// offset.
    struct MemOpQueueEntry {
      MachineInstr *MI;
      int Offset;        ///< Load/Store offset.
      unsigned Position; ///< Position as counted from end of basic block.

      MemOpQueueEntry(MachineInstr &MI, int Offset, unsigned Position)
          : MI(&MI), Offset(Offset), Position(Position) {}
    };
    using MemOpQueue = SmallVector<MemOpQueueEntry, 8>;

    /// A set of MachineInstrs that fulfill (nearly all) conditions to get
    /// merged into a LDM/STM.
    struct MergeCandidate {
      /// List of instructions ordered by load/store offset.
      SmallVector<MachineInstr*, 4> Instrs;

      /// Index in Instrs of the instruction being latest in the schedule.
      unsigned LatestMIIdx;

      /// Index in Instrs of the instruction being earliest in the schedule.
      unsigned EarliestMIIdx;

      /// Index into the basic block where the merged instruction will be
      /// inserted. (See MemOpQueueEntry.Position)
      unsigned InsertPos;

      /// Whether the instructions can be merged into a ldm/stm instruction.
      bool CanMergeToLSMulti;

      /// Whether the instructions can be merged into a ldrd/strd instruction.
      bool CanMergeToLSDouble;
    };
    SpecificBumpPtrAllocator<MergeCandidate> Allocator;
    SmallVector<const MergeCandidate*,4> Candidates;
    SmallVector<MachineInstr*,4> MergeBaseCandidates;

    void moveLiveRegsBefore(const MachineBasicBlock &MBB,
                            MachineBasicBlock::const_iterator Before);
    unsigned findFreeReg(const TargetRegisterClass &RegClass);
    void UpdateBaseRegUses(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                           unsigned Base, unsigned WordOffset,
                           ARMCC::CondCodes Pred, unsigned PredReg);
    MachineInstr *CreateLoadStoreMulti(
        MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
        int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
        ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
        ArrayRef<std::pair<unsigned, bool>> Regs);
    MachineInstr *CreateLoadStoreDouble(
        MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
        int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
        ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
        ArrayRef<std::pair<unsigned, bool>> Regs) const;
    void FormCandidates(const MemOpQueue &MemOps);
    MachineInstr *MergeOpsUpdate(const MergeCandidate &Cand);
    bool FixInvalidRegPairOp(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MBBI);
    bool MergeBaseUpdateLoadStore(MachineInstr *MI);
    bool MergeBaseUpdateLSMultiple(MachineInstr *MI);
    bool MergeBaseUpdateLSDouble(MachineInstr &MI) const;
    bool LoadStoreMultipleOpti(MachineBasicBlock &MBB);
    bool MergeReturnIntoLDM(MachineBasicBlock &MBB);
    bool CombineMovBx(MachineBasicBlock &MBB);
  };

} // end anonymous namespace

char ARMLoadStoreOpt::ID = 0;

INITIALIZE_PASS(ARMLoadStoreOpt, "arm-ldst-opt", ARM_LOAD_STORE_OPT_NAME, false,
                false)

static bool definesCPSR(const MachineInstr &MI) {
  for (const auto &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (MO.isDef() && MO.getReg() == ARM::CPSR && !MO.isDead())
      // If the instruction has live CPSR def, then it's not safe to fold it
      // into load / store.
      return true;
  }

  return false;
}

static int getMemoryOpOffset(const MachineInstr &MI) {
  unsigned Opcode = MI.getOpcode();
  bool isAM3 = Opcode == ARM::LDRD || Opcode == ARM::STRD;
  unsigned NumOperands = MI.getDesc().getNumOperands();
  unsigned OffField = MI.getOperand(NumOperands - 3).getImm();

  if (Opcode == ARM::t2LDRi12 || Opcode == ARM::t2LDRi8 ||
      Opcode == ARM::t2STRi12 || Opcode == ARM::t2STRi8 ||
      Opcode == ARM::t2LDRDi8 || Opcode == ARM::t2STRDi8 ||
      Opcode == ARM::LDRi12   || Opcode == ARM::STRi12)
    return OffField;

  // Thumb1 immediate offsets are scaled by 4
  if (Opcode == ARM::tLDRi || Opcode == ARM::tSTRi ||
      Opcode == ARM::tLDRspi || Opcode == ARM::tSTRspi)
    return OffField * 4;

  int Offset = isAM3 ? ARM_AM::getAM3Offset(OffField)
    : ARM_AM::getAM5Offset(OffField) * 4;
  ARM_AM::AddrOpc Op = isAM3 ? ARM_AM::getAM3Op(OffField)
    : ARM_AM::getAM5Op(OffField);

  if (Op == ARM_AM::sub)
    return -Offset;

  return Offset;
}

static const MachineOperand &getLoadStoreBaseOp(const MachineInstr &MI) {
  return MI.getOperand(1);
}

static const MachineOperand &getLoadStoreRegOp(const MachineInstr &MI) {
  return MI.getOperand(0);
}

static int getLoadStoreMultipleOpcode(unsigned Opcode, ARM_AM::AMSubMode Mode) {
  switch (Opcode) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::LDRi12:
    ++NumLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::LDMIA;
    case ARM_AM::da: return ARM::LDMDA;
    case ARM_AM::db: return ARM::LDMDB;
    case ARM_AM::ib: return ARM::LDMIB;
    }
  case ARM::STRi12:
    ++NumSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::STMIA;
    case ARM_AM::da: return ARM::STMDA;
    case ARM_AM::db: return ARM::STMDB;
    case ARM_AM::ib: return ARM::STMIB;
    }
  case ARM::tLDRi:
  case ARM::tLDRspi:
    // tLDMIA is writeback-only - unless the base register is in the input
    // reglist.
    ++NumLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::tLDMIA;
    }
  case ARM::tSTRi:
  case ARM::tSTRspi:
    // There is no non-writeback tSTMIA either.
    ++NumSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::tSTMIA_UPD;
    }
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
    ++NumLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2LDMIA;
    case ARM_AM::db: return ARM::t2LDMDB;
    }
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    ++NumSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2STMIA;
    case ARM_AM::db: return ARM::t2STMDB;
    }
  case ARM::VLDRS:
    ++NumVLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMSIA;
    case ARM_AM::db: return 0; // Only VLDMSDB_UPD exists.
    }
  case ARM::VSTRS:
    ++NumVSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMSIA;
    case ARM_AM::db: return 0; // Only VSTMSDB_UPD exists.
    }
  case ARM::VLDRD:
    ++NumVLDMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMDIA;
    case ARM_AM::db: return 0; // Only VLDMDDB_UPD exists.
    }
  case ARM::VSTRD:
    ++NumVSTMGened;
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMDIA;
    case ARM_AM::db: return 0; // Only VSTMDDB_UPD exists.
    }
  }
}

static ARM_AM::AMSubMode getLoadStoreMultipleSubMode(unsigned Opcode) {
  switch (Opcode) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::LDMIA_RET:
  case ARM::LDMIA:
  case ARM::LDMIA_UPD:
  case ARM::STMIA:
  case ARM::STMIA_UPD:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA:
  case ARM::t2LDMIA_UPD:
  case ARM::t2STMIA:
  case ARM::t2STMIA_UPD:
  case ARM::VLDMSIA:
  case ARM::VLDMSIA_UPD:
  case ARM::VSTMSIA:
  case ARM::VSTMSIA_UPD:
  case ARM::VLDMDIA:
  case ARM::VLDMDIA_UPD:
  case ARM::VSTMDIA:
  case ARM::VSTMDIA_UPD:
    return ARM_AM::ia;

  case ARM::LDMDA:
  case ARM::LDMDA_UPD:
  case ARM::STMDA:
  case ARM::STMDA_UPD:
    return ARM_AM::da;

  case ARM::LDMDB:
  case ARM::LDMDB_UPD:
  case ARM::STMDB:
  case ARM::STMDB_UPD:
  case ARM::t2LDMDB:
  case ARM::t2LDMDB_UPD:
  case ARM::t2STMDB:
  case ARM::t2STMDB_UPD:
  case ARM::VLDMSDB_UPD:
  case ARM::VSTMSDB_UPD:
  case ARM::VLDMDDB_UPD:
  case ARM::VSTMDDB_UPD:
    return ARM_AM::db;

  case ARM::LDMIB:
  case ARM::LDMIB_UPD:
  case ARM::STMIB:
  case ARM::STMIB_UPD:
    return ARM_AM::ib;
  }
}

static bool isT1i32Load(unsigned Opc) {
  return Opc == ARM::tLDRi || Opc == ARM::tLDRspi;
}

static bool isT2i32Load(unsigned Opc) {
  return Opc == ARM::t2LDRi12 || Opc == ARM::t2LDRi8;
}

static bool isi32Load(unsigned Opc) {
  return Opc == ARM::LDRi12 || isT1i32Load(Opc) || isT2i32Load(Opc) ;
}

static bool isT1i32Store(unsigned Opc) {
  return Opc == ARM::tSTRi || Opc == ARM::tSTRspi;
}

static bool isT2i32Store(unsigned Opc) {
  return Opc == ARM::t2STRi12 || Opc == ARM::t2STRi8;
}

static bool isi32Store(unsigned Opc) {
  return Opc == ARM::STRi12 || isT1i32Store(Opc) || isT2i32Store(Opc);
}

static bool isLoadSingle(unsigned Opc) {
  return isi32Load(Opc) || Opc == ARM::VLDRS || Opc == ARM::VLDRD;
}

static unsigned getImmScale(unsigned Opc) {
  switch (Opc) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::tLDRi:
  case ARM::tSTRi:
  case ARM::tLDRspi:
  case ARM::tSTRspi:
    return 1;
  case ARM::tLDRHi:
  case ARM::tSTRHi:
    return 2;
  case ARM::tLDRBi:
  case ARM::tSTRBi:
    return 4;
  }
}

static unsigned getLSMultipleTransferSize(const MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default: return 0;
  case ARM::LDRi12:
  case ARM::STRi12:
  case ARM::tLDRi:
  case ARM::tSTRi:
  case ARM::tLDRspi:
  case ARM::tSTRspi:
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
  case ARM::t2STRi8:
  case ARM::t2STRi12:
  case ARM::VLDRS:
  case ARM::VSTRS:
    return 4;
  case ARM::VLDRD:
  case ARM::VSTRD:
    return 8;
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
  case ARM::tLDMIA:
  case ARM::tLDMIA_UPD:
  case ARM::tSTMIA_UPD:
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
  case ARM::t2STMIA:
  case ARM::t2STMDB:
  case ARM::VLDMSIA:
  case ARM::VSTMSIA:
    return (MI->getNumOperands() - MI->getDesc().getNumOperands() + 1) * 4;
  case ARM::VLDMDIA:
  case ARM::VSTMDIA:
    return (MI->getNumOperands() - MI->getDesc().getNumOperands() + 1) * 8;
  }
}

/// Update future uses of the base register with the offset introduced
/// due to writeback. This function only works on Thumb1.
void ARMLoadStoreOpt::UpdateBaseRegUses(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const DebugLoc &DL, unsigned Base,
                                        unsigned WordOffset,
                                        ARMCC::CondCodes Pred,
                                        unsigned PredReg) {
  assert(isThumb1 && "Can only update base register uses for Thumb1!");
  // Start updating any instructions with immediate offsets. Insert a SUB before
  // the first non-updateable instruction (if any).
  for (; MBBI != MBB.end(); ++MBBI) {
    bool InsertSub = false;
    unsigned Opc = MBBI->getOpcode();

    if (MBBI->readsRegister(Base)) {
      int Offset;
      bool IsLoad =
        Opc == ARM::tLDRi || Opc == ARM::tLDRHi || Opc == ARM::tLDRBi;
      bool IsStore =
        Opc == ARM::tSTRi || Opc == ARM::tSTRHi || Opc == ARM::tSTRBi;

      if (IsLoad || IsStore) {
        // Loads and stores with immediate offsets can be updated, but only if
        // the new offset isn't negative.
        // The MachineOperand containing the offset immediate is the last one
        // before predicates.
        MachineOperand &MO =
          MBBI->getOperand(MBBI->getDesc().getNumOperands() - 3);
        // The offsets are scaled by 1, 2 or 4 depending on the Opcode.
        Offset = MO.getImm() - WordOffset * getImmScale(Opc);

        // If storing the base register, it needs to be reset first.
        unsigned InstrSrcReg = getLoadStoreRegOp(*MBBI).getReg();

        if (Offset >= 0 && !(IsStore && InstrSrcReg == Base))
          MO.setImm(Offset);
        else
          InsertSub = true;
      } else if ((Opc == ARM::tSUBi8 || Opc == ARM::tADDi8) &&
                 !definesCPSR(*MBBI)) {
        // SUBS/ADDS using this register, with a dead def of the CPSR.
        // Merge it with the update; if the merged offset is too large,
        // insert a new sub instead.
        MachineOperand &MO =
          MBBI->getOperand(MBBI->getDesc().getNumOperands() - 3);
        Offset = (Opc == ARM::tSUBi8) ?
          MO.getImm() + WordOffset * 4 :
          MO.getImm() - WordOffset * 4 ;
        if (Offset >= 0 && TL->isLegalAddImmediate(Offset)) {
          // FIXME: Swap ADDS<->SUBS if Offset < 0, erase instruction if
          // Offset == 0.
          MO.setImm(Offset);
          // The base register has now been reset, so exit early.
          return;
        } else {
          InsertSub = true;
        }
      } else {
        // Can't update the instruction.
        InsertSub = true;
      }
    } else if (definesCPSR(*MBBI) || MBBI->isCall() || MBBI->isBranch()) {
      // Since SUBS sets the condition flags, we can't place the base reset
      // after an instruction that has a live CPSR def.
      // The base register might also contain an argument for a function call.
      InsertSub = true;
    }

    if (InsertSub) {
      // An instruction above couldn't be updated, so insert a sub.
      BuildMI(MBB, MBBI, DL, TII->get(ARM::tSUBi8), Base)
          .add(t1CondCodeOp(true))
          .addReg(Base)
          .addImm(WordOffset * 4)
          .addImm(Pred)
          .addReg(PredReg);
      return;
    }

    if (MBBI->killsRegister(Base) || MBBI->definesRegister(Base))
      // Register got killed. Stop updating.
      return;
  }

  // End of block was reached.
  if (MBB.succ_size() > 0) {
    // FIXME: Because of a bug, live registers are sometimes missing from
    // the successor blocks' live-in sets. This means we can't trust that
    // information and *always* have to reset at the end of a block.
    // See PR21029.
    if (MBBI != MBB.end()) --MBBI;
    BuildMI(MBB, MBBI, DL, TII->get(ARM::tSUBi8), Base)
        .add(t1CondCodeOp(true))
        .addReg(Base)
        .addImm(WordOffset * 4)
        .addImm(Pred)
        .addReg(PredReg);
  }
}

/// Return the first register of class \p RegClass that is not in \p Regs.
unsigned ARMLoadStoreOpt::findFreeReg(const TargetRegisterClass &RegClass) {
  if (!RegClassInfoValid) {
    RegClassInfo.runOnMachineFunction(*MF);
    RegClassInfoValid = true;
  }

  for (unsigned Reg : RegClassInfo.getOrder(&RegClass))
    if (!LiveRegs.contains(Reg))
      return Reg;
  return 0;
}

/// Compute live registers just before instruction \p Before (in normal schedule
/// direction). Computes backwards so multiple queries in the same block must
/// come in reverse order.
void ARMLoadStoreOpt::moveLiveRegsBefore(const MachineBasicBlock &MBB,
    MachineBasicBlock::const_iterator Before) {
  // Initialize if we never queried in this block.
  if (!LiveRegsValid) {
    LiveRegs.init(*TRI);
    LiveRegs.addLiveOuts(MBB);
    LiveRegPos = MBB.end();
    LiveRegsValid = true;
  }
  // Move backward just before the "Before" position.
  while (LiveRegPos != Before) {
    --LiveRegPos;
    LiveRegs.stepBackward(*LiveRegPos);
  }
}

static bool ContainsReg(const ArrayRef<std::pair<unsigned, bool>> &Regs,
                        unsigned Reg) {
  for (const std::pair<unsigned, bool> &R : Regs)
    if (R.first == Reg)
      return true;
  return false;
}

/// Create and insert a LDM or STM with Base as base register and registers in
/// Regs as the register operands that would be loaded / stored.  It returns
/// true if the transformation is done.
MachineInstr *ARMLoadStoreOpt::CreateLoadStoreMulti(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
    int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
    ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
    ArrayRef<std::pair<unsigned, bool>> Regs) {
  unsigned NumRegs = Regs.size();
  assert(NumRegs > 1);

  // For Thumb1 targets, it might be necessary to clobber the CPSR to merge.
  // Compute liveness information for that register to make the decision.
  bool SafeToClobberCPSR = !isThumb1 ||
    (MBB.computeRegisterLiveness(TRI, ARM::CPSR, InsertBefore, 20) ==
     MachineBasicBlock::LQR_Dead);

  bool Writeback = isThumb1; // Thumb1 LDM/STM have base reg writeback.

  // Exception: If the base register is in the input reglist, Thumb1 LDM is
  // non-writeback.
  // It's also not possible to merge an STR of the base register in Thumb1.
  if (isThumb1 && ContainsReg(Regs, Base)) {
    assert(Base != ARM::SP && "Thumb1 does not allow SP in register list");
    if (Opcode == ARM::tLDRi)
      Writeback = false;
    else if (Opcode == ARM::tSTRi)
      return nullptr;
  }

  ARM_AM::AMSubMode Mode = ARM_AM::ia;
  // VFP and Thumb2 do not support IB or DA modes. Thumb1 only supports IA.
  bool isNotVFP = isi32Load(Opcode) || isi32Store(Opcode);
  bool haveIBAndDA = isNotVFP && !isThumb2 && !isThumb1;

  if (Offset == 4 && haveIBAndDA) {
    Mode = ARM_AM::ib;
  } else if (Offset == -4 * (int)NumRegs + 4 && haveIBAndDA) {
    Mode = ARM_AM::da;
  } else if (Offset == -4 * (int)NumRegs && isNotVFP && !isThumb1) {
    // VLDM/VSTM do not support DB mode without also updating the base reg.
    Mode = ARM_AM::db;
  } else if (Offset != 0 || Opcode == ARM::tLDRspi || Opcode == ARM::tSTRspi) {
    // Check if this is a supported opcode before inserting instructions to
    // calculate a new base register.
    if (!getLoadStoreMultipleOpcode(Opcode, Mode)) return nullptr;

    // If starting offset isn't zero, insert a MI to materialize a new base.
    // But only do so if it is cost effective, i.e. merging more than two
    // loads / stores.
    if (NumRegs <= 2)
      return nullptr;

    // On Thumb1, it's not worth materializing a new base register without
    // clobbering the CPSR (i.e. not using ADDS/SUBS).
    if (!SafeToClobberCPSR)
      return nullptr;

    unsigned NewBase;
    if (isi32Load(Opcode)) {
      // If it is a load, then just use one of the destination registers
      // as the new base. Will no longer be writeback in Thumb1.
      NewBase = Regs[NumRegs-1].first;
      Writeback = false;
    } else {
      // Find a free register that we can use as scratch register.
      moveLiveRegsBefore(MBB, InsertBefore);
      // The merged instruction does not exist yet but will use several Regs if
      // it is a Store.
      if (!isLoadSingle(Opcode))
        for (const std::pair<unsigned, bool> &R : Regs)
          LiveRegs.addReg(R.first);

      NewBase = findFreeReg(isThumb1 ? ARM::tGPRRegClass : ARM::GPRRegClass);
      if (NewBase == 0)
        return nullptr;
    }

    int BaseOpc =
      isThumb2 ? ARM::t2ADDri :
      (isThumb1 && Base == ARM::SP) ? ARM::tADDrSPi :
      (isThumb1 && Offset < 8) ? ARM::tADDi3 :
      isThumb1 ? ARM::tADDi8  : ARM::ADDri;

    if (Offset < 0) {
      Offset = - Offset;
      BaseOpc =
        isThumb2 ? ARM::t2SUBri :
        (isThumb1 && Offset < 8 && Base != ARM::SP) ? ARM::tSUBi3 :
        isThumb1 ? ARM::tSUBi8  : ARM::SUBri;
    }

    if (!TL->isLegalAddImmediate(Offset))
      // FIXME: Try add with register operand?
      return nullptr; // Probably not worth it then.

    // We can only append a kill flag to the add/sub input if the value is not
    // used in the register list of the stm as well.
    bool KillOldBase = BaseKill &&
      (!isi32Store(Opcode) || !ContainsReg(Regs, Base));

    if (isThumb1) {
      // Thumb1: depending on immediate size, use either
      //   ADDS NewBase, Base, #imm3
      // or
      //   MOV  NewBase, Base
      //   ADDS NewBase, #imm8.
      if (Base != NewBase &&
          (BaseOpc == ARM::tADDi8 || BaseOpc == ARM::tSUBi8)) {
        // Need to insert a MOV to the new base first.
        if (isARMLowRegister(NewBase) && isARMLowRegister(Base) &&
            !STI->hasV6Ops()) {
          // thumbv4t doesn't have lo->lo copies, and we can't predicate tMOVSr
          if (Pred != ARMCC::AL)
            return nullptr;
          BuildMI(MBB, InsertBefore, DL, TII->get(ARM::tMOVSr), NewBase)
            .addReg(Base, getKillRegState(KillOldBase));
        } else
          BuildMI(MBB, InsertBefore, DL, TII->get(ARM::tMOVr), NewBase)
              .addReg(Base, getKillRegState(KillOldBase))
              .add(predOps(Pred, PredReg));

        // The following ADDS/SUBS becomes an update.
        Base = NewBase;
        KillOldBase = true;
      }
      if (BaseOpc == ARM::tADDrSPi) {
        assert(Offset % 4 == 0 && "tADDrSPi offset is scaled by 4");
        BuildMI(MBB, InsertBefore, DL, TII->get(BaseOpc), NewBase)
            .addReg(Base, getKillRegState(KillOldBase))
            .addImm(Offset / 4)
            .add(predOps(Pred, PredReg));
      } else
        BuildMI(MBB, InsertBefore, DL, TII->get(BaseOpc), NewBase)
            .add(t1CondCodeOp(true))
            .addReg(Base, getKillRegState(KillOldBase))
            .addImm(Offset)
            .add(predOps(Pred, PredReg));
    } else {
      BuildMI(MBB, InsertBefore, DL, TII->get(BaseOpc), NewBase)
          .addReg(Base, getKillRegState(KillOldBase))
          .addImm(Offset)
          .add(predOps(Pred, PredReg))
          .add(condCodeOp());
    }
    Base = NewBase;
    BaseKill = true; // New base is always killed straight away.
  }

  bool isDef = isLoadSingle(Opcode);

  // Get LS multiple opcode. Note that for Thumb1 this might be an opcode with
  // base register writeback.
  Opcode = getLoadStoreMultipleOpcode(Opcode, Mode);
  if (!Opcode)
    return nullptr;

  // Check if a Thumb1 LDM/STM merge is safe. This is the case if:
  // - There is no writeback (LDM of base register),
  // - the base register is killed by the merged instruction,
  // - or it's safe to overwrite the condition flags, i.e. to insert a SUBS
  //   to reset the base register.
  // Otherwise, don't merge.
  // It's safe to return here since the code to materialize a new base register
  // above is also conditional on SafeToClobberCPSR.
  if (isThumb1 && !SafeToClobberCPSR && Writeback && !BaseKill)
    return nullptr;

  MachineInstrBuilder MIB;

  if (Writeback) {
    assert(isThumb1 && "expected Writeback only inThumb1");
    if (Opcode == ARM::tLDMIA) {
      assert(!(ContainsReg(Regs, Base)) && "Thumb1 can't LDM ! with Base in Regs");
      // Update tLDMIA with writeback if necessary.
      Opcode = ARM::tLDMIA_UPD;
    }

    MIB = BuildMI(MBB, InsertBefore, DL, TII->get(Opcode));

    // Thumb1: we might need to set base writeback when building the MI.
    MIB.addReg(Base, getDefRegState(true))
       .addReg(Base, getKillRegState(BaseKill));

    // The base isn't dead after a merged instruction with writeback.
    // Insert a sub instruction after the newly formed instruction to reset.
    if (!BaseKill)
      UpdateBaseRegUses(MBB, InsertBefore, DL, Base, NumRegs, Pred, PredReg);
  } else {
    // No writeback, simply build the MachineInstr.
    MIB = BuildMI(MBB, InsertBefore, DL, TII->get(Opcode));
    MIB.addReg(Base, getKillRegState(BaseKill));
  }

  MIB.addImm(Pred).addReg(PredReg);

  for (const std::pair<unsigned, bool> &R : Regs)
    MIB.addReg(R.first, getDefRegState(isDef) | getKillRegState(R.second));

  return MIB.getInstr();
}

MachineInstr *ARMLoadStoreOpt::CreateLoadStoreDouble(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
    int Offset, unsigned Base, bool BaseKill, unsigned Opcode,
    ARMCC::CondCodes Pred, unsigned PredReg, const DebugLoc &DL,
    ArrayRef<std::pair<unsigned, bool>> Regs) const {
  bool IsLoad = isi32Load(Opcode);
  assert((IsLoad || isi32Store(Opcode)) && "Must have integer load or store");
  unsigned LoadStoreOpcode = IsLoad ? ARM::t2LDRDi8 : ARM::t2STRDi8;

  assert(Regs.size() == 2);
  MachineInstrBuilder MIB = BuildMI(MBB, InsertBefore, DL,
                                    TII->get(LoadStoreOpcode));
  if (IsLoad) {
    MIB.addReg(Regs[0].first, RegState::Define)
       .addReg(Regs[1].first, RegState::Define);
  } else {
    MIB.addReg(Regs[0].first, getKillRegState(Regs[0].second))
       .addReg(Regs[1].first, getKillRegState(Regs[1].second));
  }
  MIB.addReg(Base).addImm(Offset).addImm(Pred).addReg(PredReg);
  return MIB.getInstr();
}

/// Call MergeOps and update MemOps and merges accordingly on success.
MachineInstr *ARMLoadStoreOpt::MergeOpsUpdate(const MergeCandidate &Cand) {
  const MachineInstr *First = Cand.Instrs.front();
  unsigned Opcode = First->getOpcode();
  bool IsLoad = isLoadSingle(Opcode);
  SmallVector<std::pair<unsigned, bool>, 8> Regs;
  SmallVector<unsigned, 4> ImpDefs;
  DenseSet<unsigned> KilledRegs;
  DenseSet<unsigned> UsedRegs;
  // Determine list of registers and list of implicit super-register defs.
  for (const MachineInstr *MI : Cand.Instrs) {
    const MachineOperand &MO = getLoadStoreRegOp(*MI);
    unsigned Reg = MO.getReg();
    bool IsKill = MO.isKill();
    if (IsKill)
      KilledRegs.insert(Reg);
    Regs.push_back(std::make_pair(Reg, IsKill));
    UsedRegs.insert(Reg);

    if (IsLoad) {
      // Collect any implicit defs of super-registers, after merging we can't
      // be sure anymore that we properly preserved these live ranges and must
      // removed these implicit operands.
      for (const MachineOperand &MO : MI->implicit_operands()) {
        if (!MO.isReg() || !MO.isDef() || MO.isDead())
          continue;
        assert(MO.isImplicit());
        unsigned DefReg = MO.getReg();

        if (is_contained(ImpDefs, DefReg))
          continue;
        // We can ignore cases where the super-reg is read and written.
        if (MI->readsRegister(DefReg))
          continue;
        ImpDefs.push_back(DefReg);
      }
    }
  }

  // Attempt the merge.
  using iterator = MachineBasicBlock::iterator;

  MachineInstr *LatestMI = Cand.Instrs[Cand.LatestMIIdx];
  iterator InsertBefore = std::next(iterator(LatestMI));
  MachineBasicBlock &MBB = *LatestMI->getParent();
  unsigned Offset = getMemoryOpOffset(*First);
  unsigned Base = getLoadStoreBaseOp(*First).getReg();
  bool BaseKill = LatestMI->killsRegister(Base);
  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = getInstrPredicate(*First, PredReg);
  DebugLoc DL = First->getDebugLoc();
  MachineInstr *Merged = nullptr;
  if (Cand.CanMergeToLSDouble)
    Merged = CreateLoadStoreDouble(MBB, InsertBefore, Offset, Base, BaseKill,
                                   Opcode, Pred, PredReg, DL, Regs);
  if (!Merged && Cand.CanMergeToLSMulti)
    Merged = CreateLoadStoreMulti(MBB, InsertBefore, Offset, Base, BaseKill,
                                  Opcode, Pred, PredReg, DL, Regs);
  if (!Merged)
    return nullptr;

  // Determine earliest instruction that will get removed. We then keep an
  // iterator just above it so the following erases don't invalidated it.
  iterator EarliestI(Cand.Instrs[Cand.EarliestMIIdx]);
  bool EarliestAtBegin = false;
  if (EarliestI == MBB.begin()) {
    EarliestAtBegin = true;
  } else {
    EarliestI = std::prev(EarliestI);
  }

  // Remove instructions which have been merged.
  for (MachineInstr *MI : Cand.Instrs)
    MBB.erase(MI);

  // Determine range between the earliest removed instruction and the new one.
  if (EarliestAtBegin)
    EarliestI = MBB.begin();
  else
    EarliestI = std::next(EarliestI);
  auto FixupRange = make_range(EarliestI, iterator(Merged));

  if (isLoadSingle(Opcode)) {
    // If the previous loads defined a super-reg, then we have to mark earlier
    // operands undef; Replicate the super-reg def on the merged instruction.
    for (MachineInstr &MI : FixupRange) {
      for (unsigned &ImpDefReg : ImpDefs) {
        for (MachineOperand &MO : MI.implicit_operands()) {
          if (!MO.isReg() || MO.getReg() != ImpDefReg)
            continue;
          if (MO.readsReg())
            MO.setIsUndef();
          else if (MO.isDef())
            ImpDefReg = 0;
        }
      }
    }

    MachineInstrBuilder MIB(*Merged->getParent()->getParent(), Merged);
    for (unsigned ImpDef : ImpDefs)
      MIB.addReg(ImpDef, RegState::ImplicitDefine);
  } else {
    // Remove kill flags: We are possibly storing the values later now.
    assert(isi32Store(Opcode) || Opcode == ARM::VSTRS || Opcode == ARM::VSTRD);
    for (MachineInstr &MI : FixupRange) {
      for (MachineOperand &MO : MI.uses()) {
        if (!MO.isReg() || !MO.isKill())
          continue;
        if (UsedRegs.count(MO.getReg()))
          MO.setIsKill(false);
      }
    }
    assert(ImpDefs.empty());
  }

  return Merged;
}

static bool isValidLSDoubleOffset(int Offset) {
  unsigned Value = abs(Offset);
  // t2LDRDi8/t2STRDi8 supports an 8 bit immediate which is internally
  // multiplied by 4.
  return (Value % 4) == 0 && Value < 1024;
}

/// Return true for loads/stores that can be combined to a double/multi
/// operation without increasing the requirements for alignment.
static bool mayCombineMisaligned(const TargetSubtargetInfo &STI,
                                 const MachineInstr &MI) {
  // vldr/vstr trap on misaligned pointers anyway, forming vldm makes no
  // difference.
  unsigned Opcode = MI.getOpcode();
  if (!isi32Load(Opcode) && !isi32Store(Opcode))
    return true;

  // Stack pointer alignment is out of the programmers control so we can trust
  // SP-relative loads/stores.
  if (getLoadStoreBaseOp(MI).getReg() == ARM::SP &&
      STI.getFrameLowering()->getTransientStackAlignment() >= 4)
    return true;
  return false;
}

/// Find candidates for load/store multiple merge in list of MemOpQueueEntries.
void ARMLoadStoreOpt::FormCandidates(const MemOpQueue &MemOps) {
  const MachineInstr *FirstMI = MemOps[0].MI;
  unsigned Opcode = FirstMI->getOpcode();
  bool isNotVFP = isi32Load(Opcode) || isi32Store(Opcode);
  unsigned Size = getLSMultipleTransferSize(FirstMI);

  unsigned SIndex = 0;
  unsigned EIndex = MemOps.size();
  do {
    // Look at the first instruction.
    const MachineInstr *MI = MemOps[SIndex].MI;
    int Offset = MemOps[SIndex].Offset;
    const MachineOperand &PMO = getLoadStoreRegOp(*MI);
    unsigned PReg = PMO.getReg();
    unsigned PRegNum = PMO.isUndef() ? std::numeric_limits<unsigned>::max()
                                     : TRI->getEncodingValue(PReg);
    unsigned Latest = SIndex;
    unsigned Earliest = SIndex;
    unsigned Count = 1;
    bool CanMergeToLSDouble =
      STI->isThumb2() && isNotVFP && isValidLSDoubleOffset(Offset);
    // ARM errata 602117: LDRD with base in list may result in incorrect base
    // register when interrupted or faulted.
    if (STI->isCortexM3() && isi32Load(Opcode) &&
        PReg == getLoadStoreBaseOp(*MI).getReg())
      CanMergeToLSDouble = false;

    bool CanMergeToLSMulti = true;
    // On swift vldm/vstm starting with an odd register number as that needs
    // more uops than single vldrs.
    if (STI->hasSlowOddRegister() && !isNotVFP && (PRegNum % 2) == 1)
      CanMergeToLSMulti = false;

    // LDRD/STRD do not allow SP/PC. LDM/STM do not support it or have it
    // deprecated; LDM to PC is fine but cannot happen here.
    if (PReg == ARM::SP || PReg == ARM::PC)
      CanMergeToLSMulti = CanMergeToLSDouble = false;

    // Should we be conservative?
    if (AssumeMisalignedLoadStores && !mayCombineMisaligned(*STI, *MI))
      CanMergeToLSMulti = CanMergeToLSDouble = false;

    // vldm / vstm limit are 32 for S variants, 16 for D variants.
    unsigned Limit;
    switch (Opcode) {
    default:
      Limit = UINT_MAX;
      break;
    case ARM::VLDRD:
    case ARM::VSTRD:
      Limit = 16;
      break;
    }

    // Merge following instructions where possible.
    for (unsigned I = SIndex+1; I < EIndex; ++I, ++Count) {
      int NewOffset = MemOps[I].Offset;
      if (NewOffset != Offset + (int)Size)
        break;
      const MachineOperand &MO = getLoadStoreRegOp(*MemOps[I].MI);
      unsigned Reg = MO.getReg();
      if (Reg == ARM::SP || Reg == ARM::PC)
        break;
      if (Count == Limit)
        break;

      // See if the current load/store may be part of a multi load/store.
      unsigned RegNum = MO.isUndef() ? std::numeric_limits<unsigned>::max()
                                     : TRI->getEncodingValue(Reg);
      bool PartOfLSMulti = CanMergeToLSMulti;
      if (PartOfLSMulti) {
        // Register numbers must be in ascending order.
        if (RegNum <= PRegNum)
          PartOfLSMulti = false;
        // For VFP / NEON load/store multiples, the registers must be
        // consecutive and within the limit on the number of registers per
        // instruction.
        else if (!isNotVFP && RegNum != PRegNum+1)
          PartOfLSMulti = false;
      }
      // See if the current load/store may be part of a double load/store.
      bool PartOfLSDouble = CanMergeToLSDouble && Count <= 1;

      if (!PartOfLSMulti && !PartOfLSDouble)
        break;
      CanMergeToLSMulti &= PartOfLSMulti;
      CanMergeToLSDouble &= PartOfLSDouble;
      // Track MemOp with latest and earliest position (Positions are
      // counted in reverse).
      unsigned Position = MemOps[I].Position;
      if (Position < MemOps[Latest].Position)
        Latest = I;
      else if (Position > MemOps[Earliest].Position)
        Earliest = I;
      // Prepare for next MemOp.
      Offset += Size;
      PRegNum = RegNum;
    }

    // Form a candidate from the Ops collected so far.
    MergeCandidate *Candidate = new(Allocator.Allocate()) MergeCandidate;
    for (unsigned C = SIndex, CE = SIndex + Count; C < CE; ++C)
      Candidate->Instrs.push_back(MemOps[C].MI);
    Candidate->LatestMIIdx = Latest - SIndex;
    Candidate->EarliestMIIdx = Earliest - SIndex;
    Candidate->InsertPos = MemOps[Latest].Position;
    if (Count == 1)
      CanMergeToLSMulti = CanMergeToLSDouble = false;
    Candidate->CanMergeToLSMulti = CanMergeToLSMulti;
    Candidate->CanMergeToLSDouble = CanMergeToLSDouble;
    Candidates.push_back(Candidate);
    // Continue after the chain.
    SIndex += Count;
  } while (SIndex < EIndex);
}

static unsigned getUpdatingLSMultipleOpcode(unsigned Opc,
                                            ARM_AM::AMSubMode Mode) {
  switch (Opc) {
  default: llvm_unreachable("Unhandled opcode!");
  case ARM::LDMIA:
  case ARM::LDMDA:
  case ARM::LDMDB:
  case ARM::LDMIB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::LDMIA_UPD;
    case ARM_AM::ib: return ARM::LDMIB_UPD;
    case ARM_AM::da: return ARM::LDMDA_UPD;
    case ARM_AM::db: return ARM::LDMDB_UPD;
    }
  case ARM::STMIA:
  case ARM::STMDA:
  case ARM::STMDB:
  case ARM::STMIB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::STMIA_UPD;
    case ARM_AM::ib: return ARM::STMIB_UPD;
    case ARM_AM::da: return ARM::STMDA_UPD;
    case ARM_AM::db: return ARM::STMDB_UPD;
    }
  case ARM::t2LDMIA:
  case ARM::t2LDMDB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2LDMIA_UPD;
    case ARM_AM::db: return ARM::t2LDMDB_UPD;
    }
  case ARM::t2STMIA:
  case ARM::t2STMDB:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::t2STMIA_UPD;
    case ARM_AM::db: return ARM::t2STMDB_UPD;
    }
  case ARM::VLDMSIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMSIA_UPD;
    case ARM_AM::db: return ARM::VLDMSDB_UPD;
    }
  case ARM::VLDMDIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VLDMDIA_UPD;
    case ARM_AM::db: return ARM::VLDMDDB_UPD;
    }
  case ARM::VSTMSIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMSIA_UPD;
    case ARM_AM::db: return ARM::VSTMSDB_UPD;
    }
  case ARM::VSTMDIA:
    switch (Mode) {
    default: llvm_unreachable("Unhandled submode!");
    case ARM_AM::ia: return ARM::VSTMDIA_UPD;
    case ARM_AM::db: return ARM::VSTMDDB_UPD;
    }
  }
}

/// Check if the given instruction increments or decrements a register and
/// return the amount it is incremented/decremented. Returns 0 if the CPSR flags
/// generated by the instruction are possibly read as well.
static int isIncrementOrDecrement(const MachineInstr &MI, unsigned Reg,
                                  ARMCC::CondCodes Pred, unsigned PredReg) {
  bool CheckCPSRDef;
  int Scale;
  switch (MI.getOpcode()) {
  case ARM::tADDi8:  Scale =  4; CheckCPSRDef = true; break;
  case ARM::tSUBi8:  Scale = -4; CheckCPSRDef = true; break;
  case ARM::t2SUBri:
  case ARM::SUBri:   Scale = -1; CheckCPSRDef = true; break;
  case ARM::t2ADDri:
  case ARM::ADDri:   Scale =  1; CheckCPSRDef = true; break;
  case ARM::tADDspi: Scale =  4; CheckCPSRDef = false; break;
  case ARM::tSUBspi: Scale = -4; CheckCPSRDef = false; break;
  default: return 0;
  }

  unsigned MIPredReg;
  if (MI.getOperand(0).getReg() != Reg ||
      MI.getOperand(1).getReg() != Reg ||
      getInstrPredicate(MI, MIPredReg) != Pred ||
      MIPredReg != PredReg)
    return 0;

  if (CheckCPSRDef && definesCPSR(MI))
    return 0;
  return MI.getOperand(2).getImm() * Scale;
}

/// Searches for an increment or decrement of \p Reg before \p MBBI.
static MachineBasicBlock::iterator
findIncDecBefore(MachineBasicBlock::iterator MBBI, unsigned Reg,
                 ARMCC::CondCodes Pred, unsigned PredReg, int &Offset) {
  Offset = 0;
  MachineBasicBlock &MBB = *MBBI->getParent();
  MachineBasicBlock::iterator BeginMBBI = MBB.begin();
  MachineBasicBlock::iterator EndMBBI = MBB.end();
  if (MBBI == BeginMBBI)
    return EndMBBI;

  // Skip debug values.
  MachineBasicBlock::iterator PrevMBBI = std::prev(MBBI);
  while (PrevMBBI->isDebugInstr() && PrevMBBI != BeginMBBI)
    --PrevMBBI;

  Offset = isIncrementOrDecrement(*PrevMBBI, Reg, Pred, PredReg);
  return Offset == 0 ? EndMBBI : PrevMBBI;
}

/// Searches for a increment or decrement of \p Reg after \p MBBI.
static MachineBasicBlock::iterator
findIncDecAfter(MachineBasicBlock::iterator MBBI, unsigned Reg,
                ARMCC::CondCodes Pred, unsigned PredReg, int &Offset) {
  Offset = 0;
  MachineBasicBlock &MBB = *MBBI->getParent();
  MachineBasicBlock::iterator EndMBBI = MBB.end();
  MachineBasicBlock::iterator NextMBBI = std::next(MBBI);
  // Skip debug values.
  while (NextMBBI != EndMBBI && NextMBBI->isDebugInstr())
    ++NextMBBI;
  if (NextMBBI == EndMBBI)
    return EndMBBI;

  Offset = isIncrementOrDecrement(*NextMBBI, Reg, Pred, PredReg);
  return Offset == 0 ? EndMBBI : NextMBBI;
}

/// Fold proceeding/trailing inc/dec of base register into the
/// LDM/STM/VLDM{D|S}/VSTM{D|S} op when possible:
///
/// stmia rn, <ra, rb, rc>
/// rn := rn + 4 * 3;
/// =>
/// stmia rn!, <ra, rb, rc>
///
/// rn := rn - 4 * 3;
/// ldmia rn, <ra, rb, rc>
/// =>
/// ldmdb rn!, <ra, rb, rc>
bool ARMLoadStoreOpt::MergeBaseUpdateLSMultiple(MachineInstr *MI) {
  // Thumb1 is already using updating loads/stores.
  if (isThumb1) return false;

  const MachineOperand &BaseOP = MI->getOperand(0);
  unsigned Base = BaseOP.getReg();
  bool BaseKill = BaseOP.isKill();
  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);
  unsigned Opcode = MI->getOpcode();
  DebugLoc DL = MI->getDebugLoc();

  // Can't use an updating ld/st if the base register is also a dest
  // register. e.g. ldmdb r0!, {r0, r1, r2}. The behavior is undefined.
  for (unsigned i = 2, e = MI->getNumOperands(); i != e; ++i)
    if (MI->getOperand(i).getReg() == Base)
      return false;

  int Bytes = getLSMultipleTransferSize(MI);
  MachineBasicBlock &MBB = *MI->getParent();
  MachineBasicBlock::iterator MBBI(MI);
  int Offset;
  MachineBasicBlock::iterator MergeInstr
    = findIncDecBefore(MBBI, Base, Pred, PredReg, Offset);
  ARM_AM::AMSubMode Mode = getLoadStoreMultipleSubMode(Opcode);
  if (Mode == ARM_AM::ia && Offset == -Bytes) {
    Mode = ARM_AM::db;
  } else if (Mode == ARM_AM::ib && Offset == -Bytes) {
    Mode = ARM_AM::da;
  } else {
    MergeInstr = findIncDecAfter(MBBI, Base, Pred, PredReg, Offset);
    if (((Mode != ARM_AM::ia && Mode != ARM_AM::ib) || Offset != Bytes) &&
        ((Mode != ARM_AM::da && Mode != ARM_AM::db) || Offset != -Bytes)) {

      // We couldn't find an inc/dec to merge. But if the base is dead, we
      // can still change to a writeback form as that will save us 2 bytes
      // of code size. It can create WAW hazards though, so only do it if
      // we're minimizing code size.
      if (!MBB.getParent()->getFunction().optForMinSize() || !BaseKill)
        return false;

      bool HighRegsUsed = false;
      for (unsigned i = 2, e = MI->getNumOperands(); i != e; ++i)
        if (MI->getOperand(i).getReg() >= ARM::R8) {
          HighRegsUsed = true;
          break;
        }

      if (!HighRegsUsed)
        MergeInstr = MBB.end();
      else
        return false;
    }
  }
  if (MergeInstr != MBB.end())
    MBB.erase(MergeInstr);

  unsigned NewOpc = getUpdatingLSMultipleOpcode(Opcode, Mode);
  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc))
    .addReg(Base, getDefRegState(true)) // WB base register
    .addReg(Base, getKillRegState(BaseKill))
    .addImm(Pred).addReg(PredReg);

  // Transfer the rest of operands.
  for (unsigned OpNum = 3, e = MI->getNumOperands(); OpNum != e; ++OpNum)
    MIB.add(MI->getOperand(OpNum));

  // Transfer memoperands.
  MIB.setMemRefs(MI->memoperands());

  MBB.erase(MBBI);
  return true;
}

static unsigned getPreIndexedLoadStoreOpcode(unsigned Opc,
                                             ARM_AM::AddrOpc Mode) {
  switch (Opc) {
  case ARM::LDRi12:
    return ARM::LDR_PRE_IMM;
  case ARM::STRi12:
    return ARM::STR_PRE_IMM;
  case ARM::VLDRS:
    return Mode == ARM_AM::add ? ARM::VLDMSIA_UPD : ARM::VLDMSDB_UPD;
  case ARM::VLDRD:
    return Mode == ARM_AM::add ? ARM::VLDMDIA_UPD : ARM::VLDMDDB_UPD;
  case ARM::VSTRS:
    return Mode == ARM_AM::add ? ARM::VSTMSIA_UPD : ARM::VSTMSDB_UPD;
  case ARM::VSTRD:
    return Mode == ARM_AM::add ? ARM::VSTMDIA_UPD : ARM::VSTMDDB_UPD;
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
    return ARM::t2LDR_PRE;
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    return ARM::t2STR_PRE;
  default: llvm_unreachable("Unhandled opcode!");
  }
}

static unsigned getPostIndexedLoadStoreOpcode(unsigned Opc,
                                              ARM_AM::AddrOpc Mode) {
  switch (Opc) {
  case ARM::LDRi12:
    return ARM::LDR_POST_IMM;
  case ARM::STRi12:
    return ARM::STR_POST_IMM;
  case ARM::VLDRS:
    return Mode == ARM_AM::add ? ARM::VLDMSIA_UPD : ARM::VLDMSDB_UPD;
  case ARM::VLDRD:
    return Mode == ARM_AM::add ? ARM::VLDMDIA_UPD : ARM::VLDMDDB_UPD;
  case ARM::VSTRS:
    return Mode == ARM_AM::add ? ARM::VSTMSIA_UPD : ARM::VSTMSDB_UPD;
  case ARM::VSTRD:
    return Mode == ARM_AM::add ? ARM::VSTMDIA_UPD : ARM::VSTMDDB_UPD;
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
    return ARM::t2LDR_POST;
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    return ARM::t2STR_POST;
  default: llvm_unreachable("Unhandled opcode!");
  }
}

/// Fold proceeding/trailing inc/dec of base register into the
/// LDR/STR/FLD{D|S}/FST{D|S} op when possible:
bool ARMLoadStoreOpt::MergeBaseUpdateLoadStore(MachineInstr *MI) {
  // Thumb1 doesn't have updating LDR/STR.
  // FIXME: Use LDM/STM with single register instead.
  if (isThumb1) return false;

  unsigned Base = getLoadStoreBaseOp(*MI).getReg();
  bool BaseKill = getLoadStoreBaseOp(*MI).isKill();
  unsigned Opcode = MI->getOpcode();
  DebugLoc DL = MI->getDebugLoc();
  bool isAM5 = (Opcode == ARM::VLDRD || Opcode == ARM::VLDRS ||
                Opcode == ARM::VSTRD || Opcode == ARM::VSTRS);
  bool isAM2 = (Opcode == ARM::LDRi12 || Opcode == ARM::STRi12);
  if (isi32Load(Opcode) || isi32Store(Opcode))
    if (MI->getOperand(2).getImm() != 0)
      return false;
  if (isAM5 && ARM_AM::getAM5Offset(MI->getOperand(2).getImm()) != 0)
    return false;

  // Can't do the merge if the destination register is the same as the would-be
  // writeback register.
  if (MI->getOperand(0).getReg() == Base)
    return false;

  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);
  int Bytes = getLSMultipleTransferSize(MI);
  MachineBasicBlock &MBB = *MI->getParent();
  MachineBasicBlock::iterator MBBI(MI);
  int Offset;
  MachineBasicBlock::iterator MergeInstr
    = findIncDecBefore(MBBI, Base, Pred, PredReg, Offset);
  unsigned NewOpc;
  if (!isAM5 && Offset == Bytes) {
    NewOpc = getPreIndexedLoadStoreOpcode(Opcode, ARM_AM::add);
  } else if (Offset == -Bytes) {
    NewOpc = getPreIndexedLoadStoreOpcode(Opcode, ARM_AM::sub);
  } else {
    MergeInstr = findIncDecAfter(MBBI, Base, Pred, PredReg, Offset);
    if (Offset == Bytes) {
      NewOpc = getPostIndexedLoadStoreOpcode(Opcode, ARM_AM::add);
    } else if (!isAM5 && Offset == -Bytes) {
      NewOpc = getPostIndexedLoadStoreOpcode(Opcode, ARM_AM::sub);
    } else
      return false;
  }
  MBB.erase(MergeInstr);

  ARM_AM::AddrOpc AddSub = Offset < 0 ? ARM_AM::sub : ARM_AM::add;

  bool isLd = isLoadSingle(Opcode);
  if (isAM5) {
    // VLDM[SD]_UPD, VSTM[SD]_UPD
    // (There are no base-updating versions of VLDR/VSTR instructions, but the
    // updating load/store-multiple instructions can be used with only one
    // register.)
    MachineOperand &MO = MI->getOperand(0);
    BuildMI(MBB, MBBI, DL, TII->get(NewOpc))
      .addReg(Base, getDefRegState(true)) // WB base register
      .addReg(Base, getKillRegState(isLd ? BaseKill : false))
      .addImm(Pred).addReg(PredReg)
      .addReg(MO.getReg(), (isLd ? getDefRegState(true) :
                            getKillRegState(MO.isKill())));
  } else if (isLd) {
    if (isAM2) {
      // LDR_PRE, LDR_POST
      if (NewOpc == ARM::LDR_PRE_IMM || NewOpc == ARM::LDRB_PRE_IMM) {
        BuildMI(MBB, MBBI, DL, TII->get(NewOpc), MI->getOperand(0).getReg())
          .addReg(Base, RegState::Define)
          .addReg(Base).addImm(Offset).addImm(Pred).addReg(PredReg);
      } else {
        int Imm = ARM_AM::getAM2Opc(AddSub, Bytes, ARM_AM::no_shift);
        BuildMI(MBB, MBBI, DL, TII->get(NewOpc), MI->getOperand(0).getReg())
            .addReg(Base, RegState::Define)
            .addReg(Base)
            .addReg(0)
            .addImm(Imm)
            .add(predOps(Pred, PredReg));
      }
    } else {
      // t2LDR_PRE, t2LDR_POST
      BuildMI(MBB, MBBI, DL, TII->get(NewOpc), MI->getOperand(0).getReg())
          .addReg(Base, RegState::Define)
          .addReg(Base)
          .addImm(Offset)
          .add(predOps(Pred, PredReg));
    }
  } else {
    MachineOperand &MO = MI->getOperand(0);
    // FIXME: post-indexed stores use am2offset_imm, which still encodes
    // the vestigal zero-reg offset register. When that's fixed, this clause
    // can be removed entirely.
    if (isAM2 && NewOpc == ARM::STR_POST_IMM) {
      int Imm = ARM_AM::getAM2Opc(AddSub, Bytes, ARM_AM::no_shift);
      // STR_PRE, STR_POST
      BuildMI(MBB, MBBI, DL, TII->get(NewOpc), Base)
          .addReg(MO.getReg(), getKillRegState(MO.isKill()))
          .addReg(Base)
          .addReg(0)
          .addImm(Imm)
          .add(predOps(Pred, PredReg));
    } else {
      // t2STR_PRE, t2STR_POST
      BuildMI(MBB, MBBI, DL, TII->get(NewOpc), Base)
          .addReg(MO.getReg(), getKillRegState(MO.isKill()))
          .addReg(Base)
          .addImm(Offset)
          .add(predOps(Pred, PredReg));
    }
  }
  MBB.erase(MBBI);

  return true;
}

bool ARMLoadStoreOpt::MergeBaseUpdateLSDouble(MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  assert((Opcode == ARM::t2LDRDi8 || Opcode == ARM::t2STRDi8) &&
         "Must have t2STRDi8 or t2LDRDi8");
  if (MI.getOperand(3).getImm() != 0)
    return false;

  // Behaviour for writeback is undefined if base register is the same as one
  // of the others.
  const MachineOperand &BaseOp = MI.getOperand(2);
  unsigned Base = BaseOp.getReg();
  const MachineOperand &Reg0Op = MI.getOperand(0);
  const MachineOperand &Reg1Op = MI.getOperand(1);
  if (Reg0Op.getReg() == Base || Reg1Op.getReg() == Base)
    return false;

  unsigned PredReg;
  ARMCC::CondCodes Pred = getInstrPredicate(MI, PredReg);
  MachineBasicBlock::iterator MBBI(MI);
  MachineBasicBlock &MBB = *MI.getParent();
  int Offset;
  MachineBasicBlock::iterator MergeInstr = findIncDecBefore(MBBI, Base, Pred,
                                                            PredReg, Offset);
  unsigned NewOpc;
  if (Offset == 8 || Offset == -8) {
    NewOpc = Opcode == ARM::t2LDRDi8 ? ARM::t2LDRD_PRE : ARM::t2STRD_PRE;
  } else {
    MergeInstr = findIncDecAfter(MBBI, Base, Pred, PredReg, Offset);
    if (Offset == 8 || Offset == -8) {
      NewOpc = Opcode == ARM::t2LDRDi8 ? ARM::t2LDRD_POST : ARM::t2STRD_POST;
    } else
      return false;
  }
  MBB.erase(MergeInstr);

  DebugLoc DL = MI.getDebugLoc();
  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc));
  if (NewOpc == ARM::t2LDRD_PRE || NewOpc == ARM::t2LDRD_POST) {
    MIB.add(Reg0Op).add(Reg1Op).addReg(BaseOp.getReg(), RegState::Define);
  } else {
    assert(NewOpc == ARM::t2STRD_PRE || NewOpc == ARM::t2STRD_POST);
    MIB.addReg(BaseOp.getReg(), RegState::Define).add(Reg0Op).add(Reg1Op);
  }
  MIB.addReg(BaseOp.getReg(), RegState::Kill)
     .addImm(Offset).addImm(Pred).addReg(PredReg);
  assert(TII->get(Opcode).getNumOperands() == 6 &&
         TII->get(NewOpc).getNumOperands() == 7 &&
         "Unexpected number of operands in Opcode specification.");

  // Transfer implicit operands.
  for (const MachineOperand &MO : MI.implicit_operands())
    MIB.add(MO);
  MIB.setMemRefs(MI.memoperands());

  MBB.erase(MBBI);
  return true;
}

/// Returns true if instruction is a memory operation that this pass is capable
/// of operating on.
static bool isMemoryOp(const MachineInstr &MI) {
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
  case ARM::VLDRS:
  case ARM::VSTRS:
  case ARM::VLDRD:
  case ARM::VSTRD:
  case ARM::LDRi12:
  case ARM::STRi12:
  case ARM::tLDRi:
  case ARM::tSTRi:
  case ARM::tLDRspi:
  case ARM::tSTRspi:
  case ARM::t2LDRi8:
  case ARM::t2LDRi12:
  case ARM::t2STRi8:
  case ARM::t2STRi12:
    break;
  default:
    return false;
  }
  if (!MI.getOperand(1).isReg())
    return false;

  // When no memory operands are present, conservatively assume unaligned,
  // volatile, unfoldable.
  if (!MI.hasOneMemOperand())
    return false;

  const MachineMemOperand &MMO = **MI.memoperands_begin();

  // Don't touch volatile memory accesses - we may be changing their order.
  if (MMO.isVolatile())
    return false;

  // Unaligned ldr/str is emulated by some kernels, but unaligned ldm/stm is
  // not.
  if (MMO.getAlignment() < 4)
    return false;

  // str <undef> could probably be eliminated entirely, but for now we just want
  // to avoid making a mess of it.
  // FIXME: Use str <undef> as a wildcard to enable better stm folding.
  if (MI.getOperand(0).isReg() && MI.getOperand(0).isUndef())
    return false;

  // Likewise don't mess with references to undefined addresses.
  if (MI.getOperand(1).isUndef())
    return false;

  return true;
}

static void InsertLDR_STR(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MBBI, int Offset,
                          bool isDef, unsigned NewOpc, unsigned Reg,
                          bool RegDeadKill, bool RegUndef, unsigned BaseReg,
                          bool BaseKill, bool BaseUndef, ARMCC::CondCodes Pred,
                          unsigned PredReg, const TargetInstrInfo *TII) {
  if (isDef) {
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MBBI->getDebugLoc(),
                                      TII->get(NewOpc))
      .addReg(Reg, getDefRegState(true) | getDeadRegState(RegDeadKill))
      .addReg(BaseReg, getKillRegState(BaseKill)|getUndefRegState(BaseUndef));
    MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
  } else {
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MBBI->getDebugLoc(),
                                      TII->get(NewOpc))
      .addReg(Reg, getKillRegState(RegDeadKill) | getUndefRegState(RegUndef))
      .addReg(BaseReg, getKillRegState(BaseKill)|getUndefRegState(BaseUndef));
    MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
  }
}

bool ARMLoadStoreOpt::FixInvalidRegPairOp(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator &MBBI) {
  MachineInstr *MI = &*MBBI;
  unsigned Opcode = MI->getOpcode();
  // FIXME: Code/comments below check Opcode == t2STRDi8, but this check returns
  // if we see this opcode.
  if (Opcode != ARM::LDRD && Opcode != ARM::STRD && Opcode != ARM::t2LDRDi8)
    return false;

  const MachineOperand &BaseOp = MI->getOperand(2);
  unsigned BaseReg = BaseOp.getReg();
  unsigned EvenReg = MI->getOperand(0).getReg();
  unsigned OddReg  = MI->getOperand(1).getReg();
  unsigned EvenRegNum = TRI->getDwarfRegNum(EvenReg, false);
  unsigned OddRegNum  = TRI->getDwarfRegNum(OddReg, false);

  // ARM errata 602117: LDRD with base in list may result in incorrect base
  // register when interrupted or faulted.
  bool Errata602117 = EvenReg == BaseReg &&
    (Opcode == ARM::LDRD || Opcode == ARM::t2LDRDi8) && STI->isCortexM3();
  // ARM LDRD/STRD needs consecutive registers.
  bool NonConsecutiveRegs = (Opcode == ARM::LDRD || Opcode == ARM::STRD) &&
    (EvenRegNum % 2 != 0 || EvenRegNum + 1 != OddRegNum);

  if (!Errata602117 && !NonConsecutiveRegs)
    return false;

  bool isT2 = Opcode == ARM::t2LDRDi8 || Opcode == ARM::t2STRDi8;
  bool isLd = Opcode == ARM::LDRD || Opcode == ARM::t2LDRDi8;
  bool EvenDeadKill = isLd ?
    MI->getOperand(0).isDead() : MI->getOperand(0).isKill();
  bool EvenUndef = MI->getOperand(0).isUndef();
  bool OddDeadKill  = isLd ?
    MI->getOperand(1).isDead() : MI->getOperand(1).isKill();
  bool OddUndef = MI->getOperand(1).isUndef();
  bool BaseKill = BaseOp.isKill();
  bool BaseUndef = BaseOp.isUndef();
  assert((isT2 || MI->getOperand(3).getReg() == ARM::NoRegister) &&
         "register offset not handled below");
  int OffImm = getMemoryOpOffset(*MI);
  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);

  if (OddRegNum > EvenRegNum && OffImm == 0) {
    // Ascending register numbers and no offset. It's safe to change it to a
    // ldm or stm.
    unsigned NewOpc = (isLd)
      ? (isT2 ? ARM::t2LDMIA : ARM::LDMIA)
      : (isT2 ? ARM::t2STMIA : ARM::STMIA);
    if (isLd) {
      BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(NewOpc))
        .addReg(BaseReg, getKillRegState(BaseKill))
        .addImm(Pred).addReg(PredReg)
        .addReg(EvenReg, getDefRegState(isLd) | getDeadRegState(EvenDeadKill))
        .addReg(OddReg,  getDefRegState(isLd) | getDeadRegState(OddDeadKill));
      ++NumLDRD2LDM;
    } else {
      BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(NewOpc))
        .addReg(BaseReg, getKillRegState(BaseKill))
        .addImm(Pred).addReg(PredReg)
        .addReg(EvenReg,
                getKillRegState(EvenDeadKill) | getUndefRegState(EvenUndef))
        .addReg(OddReg,
                getKillRegState(OddDeadKill)  | getUndefRegState(OddUndef));
      ++NumSTRD2STM;
    }
  } else {
    // Split into two instructions.
    unsigned NewOpc = (isLd)
      ? (isT2 ? (OffImm < 0 ? ARM::t2LDRi8 : ARM::t2LDRi12) : ARM::LDRi12)
      : (isT2 ? (OffImm < 0 ? ARM::t2STRi8 : ARM::t2STRi12) : ARM::STRi12);
    // Be extra careful for thumb2. t2LDRi8 can't reference a zero offset,
    // so adjust and use t2LDRi12 here for that.
    unsigned NewOpc2 = (isLd)
      ? (isT2 ? (OffImm+4 < 0 ? ARM::t2LDRi8 : ARM::t2LDRi12) : ARM::LDRi12)
      : (isT2 ? (OffImm+4 < 0 ? ARM::t2STRi8 : ARM::t2STRi12) : ARM::STRi12);
    // If this is a load, make sure the first load does not clobber the base
    // register before the second load reads it.
    if (isLd && TRI->regsOverlap(EvenReg, BaseReg)) {
      assert(!TRI->regsOverlap(OddReg, BaseReg));
      InsertLDR_STR(MBB, MBBI, OffImm + 4, isLd, NewOpc2, OddReg, OddDeadKill,
                    false, BaseReg, false, BaseUndef, Pred, PredReg, TII);
      InsertLDR_STR(MBB, MBBI, OffImm, isLd, NewOpc, EvenReg, EvenDeadKill,
                    false, BaseReg, BaseKill, BaseUndef, Pred, PredReg, TII);
    } else {
      if (OddReg == EvenReg && EvenDeadKill) {
        // If the two source operands are the same, the kill marker is
        // probably on the first one. e.g.
        // t2STRDi8 killed %r5, %r5, killed %r9, 0, 14, %reg0
        EvenDeadKill = false;
        OddDeadKill = true;
      }
      // Never kill the base register in the first instruction.
      if (EvenReg == BaseReg)
        EvenDeadKill = false;
      InsertLDR_STR(MBB, MBBI, OffImm, isLd, NewOpc, EvenReg, EvenDeadKill,
                    EvenUndef, BaseReg, false, BaseUndef, Pred, PredReg, TII);
      InsertLDR_STR(MBB, MBBI, OffImm + 4, isLd, NewOpc2, OddReg, OddDeadKill,
                    OddUndef, BaseReg, BaseKill, BaseUndef, Pred, PredReg, TII);
    }
    if (isLd)
      ++NumLDRD2LDR;
    else
      ++NumSTRD2STR;
  }

  MBBI = MBB.erase(MBBI);
  return true;
}

/// An optimization pass to turn multiple LDR / STR ops of the same base and
/// incrementing offset into LDM / STM ops.
bool ARMLoadStoreOpt::LoadStoreMultipleOpti(MachineBasicBlock &MBB) {
  MemOpQueue MemOps;
  unsigned CurrBase = 0;
  unsigned CurrOpc = ~0u;
  ARMCC::CondCodes CurrPred = ARMCC::AL;
  unsigned Position = 0;
  assert(Candidates.size() == 0);
  assert(MergeBaseCandidates.size() == 0);
  LiveRegsValid = false;

  for (MachineBasicBlock::iterator I = MBB.end(), MBBI; I != MBB.begin();
       I = MBBI) {
    // The instruction in front of the iterator is the one we look at.
    MBBI = std::prev(I);
    if (FixInvalidRegPairOp(MBB, MBBI))
      continue;
    ++Position;

    if (isMemoryOp(*MBBI)) {
      unsigned Opcode = MBBI->getOpcode();
      const MachineOperand &MO = MBBI->getOperand(0);
      unsigned Reg = MO.getReg();
      unsigned Base = getLoadStoreBaseOp(*MBBI).getReg();
      unsigned PredReg = 0;
      ARMCC::CondCodes Pred = getInstrPredicate(*MBBI, PredReg);
      int Offset = getMemoryOpOffset(*MBBI);
      if (CurrBase == 0) {
        // Start of a new chain.
        CurrBase = Base;
        CurrOpc  = Opcode;
        CurrPred = Pred;
        MemOps.push_back(MemOpQueueEntry(*MBBI, Offset, Position));
        continue;
      }
      // Note: No need to match PredReg in the next if.
      if (CurrOpc == Opcode && CurrBase == Base && CurrPred == Pred) {
        // Watch out for:
        //   r4 := ldr [r0, #8]
        //   r4 := ldr [r0, #4]
        // or
        //   r0 := ldr [r0]
        // If a load overrides the base register or a register loaded by
        // another load in our chain, we cannot take this instruction.
        bool Overlap = false;
        if (isLoadSingle(Opcode)) {
          Overlap = (Base == Reg);
          if (!Overlap) {
            for (const MemOpQueueEntry &E : MemOps) {
              if (TRI->regsOverlap(Reg, E.MI->getOperand(0).getReg())) {
                Overlap = true;
                break;
              }
            }
          }
        }

        if (!Overlap) {
          // Check offset and sort memory operation into the current chain.
          if (Offset > MemOps.back().Offset) {
            MemOps.push_back(MemOpQueueEntry(*MBBI, Offset, Position));
            continue;
          } else {
            MemOpQueue::iterator MI, ME;
            for (MI = MemOps.begin(), ME = MemOps.end(); MI != ME; ++MI) {
              if (Offset < MI->Offset) {
                // Found a place to insert.
                break;
              }
              if (Offset == MI->Offset) {
                // Collision, abort.
                MI = ME;
                break;
              }
            }
            if (MI != MemOps.end()) {
              MemOps.insert(MI, MemOpQueueEntry(*MBBI, Offset, Position));
              continue;
            }
          }
        }
      }

      // Don't advance the iterator; The op will start a new chain next.
      MBBI = I;
      --Position;
      // Fallthrough to look into existing chain.
    } else if (MBBI->isDebugInstr()) {
      continue;
    } else if (MBBI->getOpcode() == ARM::t2LDRDi8 ||
               MBBI->getOpcode() == ARM::t2STRDi8) {
      // ARMPreAllocLoadStoreOpt has already formed some LDRD/STRD instructions
      // remember them because we may still be able to merge add/sub into them.
      MergeBaseCandidates.push_back(&*MBBI);
    }

    // If we are here then the chain is broken; Extract candidates for a merge.
    if (MemOps.size() > 0) {
      FormCandidates(MemOps);
      // Reset for the next chain.
      CurrBase = 0;
      CurrOpc = ~0u;
      CurrPred = ARMCC::AL;
      MemOps.clear();
    }
  }
  if (MemOps.size() > 0)
    FormCandidates(MemOps);

  // Sort candidates so they get processed from end to begin of the basic
  // block later; This is necessary for liveness calculation.
  auto LessThan = [](const MergeCandidate* M0, const MergeCandidate *M1) {
    return M0->InsertPos < M1->InsertPos;
  };
  llvm::sort(Candidates, LessThan);

  // Go through list of candidates and merge.
  bool Changed = false;
  for (const MergeCandidate *Candidate : Candidates) {
    if (Candidate->CanMergeToLSMulti || Candidate->CanMergeToLSDouble) {
      MachineInstr *Merged = MergeOpsUpdate(*Candidate);
      // Merge preceding/trailing base inc/dec into the merged op.
      if (Merged) {
        Changed = true;
        unsigned Opcode = Merged->getOpcode();
        if (Opcode == ARM::t2STRDi8 || Opcode == ARM::t2LDRDi8)
          MergeBaseUpdateLSDouble(*Merged);
        else
          MergeBaseUpdateLSMultiple(Merged);
      } else {
        for (MachineInstr *MI : Candidate->Instrs) {
          if (MergeBaseUpdateLoadStore(MI))
            Changed = true;
        }
      }
    } else {
      assert(Candidate->Instrs.size() == 1);
      if (MergeBaseUpdateLoadStore(Candidate->Instrs.front()))
        Changed = true;
    }
  }
  Candidates.clear();
  // Try to fold add/sub into the LDRD/STRD formed by ARMPreAllocLoadStoreOpt.
  for (MachineInstr *MI : MergeBaseCandidates)
    MergeBaseUpdateLSDouble(*MI);
  MergeBaseCandidates.clear();

  return Changed;
}

/// If this is a exit BB, try merging the return ops ("bx lr" and "mov pc, lr")
/// into the preceding stack restore so it directly restore the value of LR
/// into pc.
///   ldmfd sp!, {..., lr}
///   bx lr
/// or
///   ldmfd sp!, {..., lr}
///   mov pc, lr
/// =>
///   ldmfd sp!, {..., pc}
bool ARMLoadStoreOpt::MergeReturnIntoLDM(MachineBasicBlock &MBB) {
  // Thumb1 LDM doesn't allow high registers.
  if (isThumb1) return false;
  if (MBB.empty()) return false;

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  if (MBBI != MBB.begin() && MBBI != MBB.end() &&
      (MBBI->getOpcode() == ARM::BX_RET ||
       MBBI->getOpcode() == ARM::tBX_RET ||
       MBBI->getOpcode() == ARM::MOVPCLR)) {
    MachineBasicBlock::iterator PrevI = std::prev(MBBI);
    // Ignore any debug instructions.
    while (PrevI->isDebugInstr() && PrevI != MBB.begin())
      --PrevI;
    MachineInstr &PrevMI = *PrevI;
    unsigned Opcode = PrevMI.getOpcode();
    if (Opcode == ARM::LDMIA_UPD || Opcode == ARM::LDMDA_UPD ||
        Opcode == ARM::LDMDB_UPD || Opcode == ARM::LDMIB_UPD ||
        Opcode == ARM::t2LDMIA_UPD || Opcode == ARM::t2LDMDB_UPD) {
      MachineOperand &MO = PrevMI.getOperand(PrevMI.getNumOperands() - 1);
      if (MO.getReg() != ARM::LR)
        return false;
      unsigned NewOpc = (isThumb2 ? ARM::t2LDMIA_RET : ARM::LDMIA_RET);
      assert(((isThumb2 && Opcode == ARM::t2LDMIA_UPD) ||
              Opcode == ARM::LDMIA_UPD) && "Unsupported multiple load-return!");
      PrevMI.setDesc(TII->get(NewOpc));
      MO.setReg(ARM::PC);
      PrevMI.copyImplicitOps(*MBB.getParent(), *MBBI);
      MBB.erase(MBBI);
      // We now restore LR into PC so it is not live-out of the return block
      // anymore: Clear the CSI Restored bit.
      MachineFrameInfo &MFI = MBB.getParent()->getFrameInfo();
      // CSI should be fixed after PrologEpilog Insertion
      assert(MFI.isCalleeSavedInfoValid() && "CSI should be valid");
      for (CalleeSavedInfo &Info : MFI.getCalleeSavedInfo()) {
        if (Info.getReg() == ARM::LR) {
          Info.setRestored(false);
          break;
        }
      }
      return true;
    }
  }
  return false;
}

bool ARMLoadStoreOpt::CombineMovBx(MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  if (MBBI == MBB.begin() || MBBI == MBB.end() ||
      MBBI->getOpcode() != ARM::tBX_RET)
    return false;

  MachineBasicBlock::iterator Prev = MBBI;
  --Prev;
  if (Prev->getOpcode() != ARM::tMOVr || !Prev->definesRegister(ARM::LR))
    return false;

  for (auto Use : Prev->uses())
    if (Use.isKill()) {
      assert(STI->hasV4TOps());
      BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(ARM::tBX))
          .addReg(Use.getReg(), RegState::Kill)
          .add(predOps(ARMCC::AL))
          .copyImplicitOps(*MBBI);
      MBB.erase(MBBI);
      MBB.erase(Prev);
      return true;
    }

  llvm_unreachable("tMOVr doesn't kill a reg before tBX_RET?");
}

bool ARMLoadStoreOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  MF = &Fn;
  STI = &static_cast<const ARMSubtarget &>(Fn.getSubtarget());
  TL = STI->getTargetLowering();
  AFI = Fn.getInfo<ARMFunctionInfo>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();

  RegClassInfoValid = false;
  isThumb2 = AFI->isThumb2Function();
  isThumb1 = AFI->isThumbFunction() && !isThumb2;

  bool Modified = false;
  for (MachineFunction::iterator MFI = Fn.begin(), E = Fn.end(); MFI != E;
       ++MFI) {
    MachineBasicBlock &MBB = *MFI;
    Modified |= LoadStoreMultipleOpti(MBB);
    if (STI->hasV5TOps())
      Modified |= MergeReturnIntoLDM(MBB);
    if (isThumb1)
      Modified |= CombineMovBx(MBB);
  }

  Allocator.DestroyAll();
  return Modified;
}

#define ARM_PREALLOC_LOAD_STORE_OPT_NAME                                       \
  "ARM pre- register allocation load / store optimization pass"

namespace {

  /// Pre- register allocation pass that move load / stores from consecutive
  /// locations close to make it more likely they will be combined later.
  struct ARMPreAllocLoadStoreOpt : public MachineFunctionPass{
    static char ID;

    AliasAnalysis *AA;
    const DataLayout *TD;
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const ARMSubtarget *STI;
    MachineRegisterInfo *MRI;
    MachineFunction *MF;

    ARMPreAllocLoadStoreOpt() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    StringRef getPassName() const override {
      return ARM_PREALLOC_LOAD_STORE_OPT_NAME;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<AAResultsWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

  private:
    bool CanFormLdStDWord(MachineInstr *Op0, MachineInstr *Op1, DebugLoc &dl,
                          unsigned &NewOpc, unsigned &EvenReg,
                          unsigned &OddReg, unsigned &BaseReg,
                          int &Offset,
                          unsigned &PredReg, ARMCC::CondCodes &Pred,
                          bool &isT2);
    bool RescheduleOps(MachineBasicBlock *MBB,
                       SmallVectorImpl<MachineInstr *> &Ops,
                       unsigned Base, bool isLd,
                       DenseMap<MachineInstr*, unsigned> &MI2LocMap);
    bool RescheduleLoadStoreInstrs(MachineBasicBlock *MBB);
  };

} // end anonymous namespace

char ARMPreAllocLoadStoreOpt::ID = 0;

INITIALIZE_PASS(ARMPreAllocLoadStoreOpt, "arm-prera-ldst-opt",
                ARM_PREALLOC_LOAD_STORE_OPT_NAME, false, false)

bool ARMPreAllocLoadStoreOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (AssumeMisalignedLoadStores || skipFunction(Fn.getFunction()))
    return false;

  TD = &Fn.getDataLayout();
  STI = &static_cast<const ARMSubtarget &>(Fn.getSubtarget());
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  MRI = &Fn.getRegInfo();
  MF  = &Fn;
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  bool Modified = false;
  for (MachineBasicBlock &MFI : Fn)
    Modified |= RescheduleLoadStoreInstrs(&MFI);

  return Modified;
}

static bool IsSafeAndProfitableToMove(bool isLd, unsigned Base,
                                      MachineBasicBlock::iterator I,
                                      MachineBasicBlock::iterator E,
                                      SmallPtrSetImpl<MachineInstr*> &MemOps,
                                      SmallSet<unsigned, 4> &MemRegs,
                                      const TargetRegisterInfo *TRI,
                                      AliasAnalysis *AA) {
  // Are there stores / loads / calls between them?
  SmallSet<unsigned, 4> AddedRegPressure;
  while (++I != E) {
    if (I->isDebugInstr() || MemOps.count(&*I))
      continue;
    if (I->isCall() || I->isTerminator() || I->hasUnmodeledSideEffects())
      return false;
    if (I->mayStore() || (!isLd && I->mayLoad()))
      for (MachineInstr *MemOp : MemOps)
        if (I->mayAlias(AA, *MemOp, /*UseTBAA*/ false))
          return false;
    for (unsigned j = 0, NumOps = I->getNumOperands(); j != NumOps; ++j) {
      MachineOperand &MO = I->getOperand(j);
      if (!MO.isReg())
        continue;
      unsigned Reg = MO.getReg();
      if (MO.isDef() && TRI->regsOverlap(Reg, Base))
        return false;
      if (Reg != Base && !MemRegs.count(Reg))
        AddedRegPressure.insert(Reg);
    }
  }

  // Estimate register pressure increase due to the transformation.
  if (MemRegs.size() <= 4)
    // Ok if we are moving small number of instructions.
    return true;
  return AddedRegPressure.size() <= MemRegs.size() * 2;
}

bool
ARMPreAllocLoadStoreOpt::CanFormLdStDWord(MachineInstr *Op0, MachineInstr *Op1,
                                          DebugLoc &dl, unsigned &NewOpc,
                                          unsigned &FirstReg,
                                          unsigned &SecondReg,
                                          unsigned &BaseReg, int &Offset,
                                          unsigned &PredReg,
                                          ARMCC::CondCodes &Pred,
                                          bool &isT2) {
  // Make sure we're allowed to generate LDRD/STRD.
  if (!STI->hasV5TEOps())
    return false;

  // FIXME: VLDRS / VSTRS -> VLDRD / VSTRD
  unsigned Scale = 1;
  unsigned Opcode = Op0->getOpcode();
  if (Opcode == ARM::LDRi12) {
    NewOpc = ARM::LDRD;
  } else if (Opcode == ARM::STRi12) {
    NewOpc = ARM::STRD;
  } else if (Opcode == ARM::t2LDRi8 || Opcode == ARM::t2LDRi12) {
    NewOpc = ARM::t2LDRDi8;
    Scale = 4;
    isT2 = true;
  } else if (Opcode == ARM::t2STRi8 || Opcode == ARM::t2STRi12) {
    NewOpc = ARM::t2STRDi8;
    Scale = 4;
    isT2 = true;
  } else {
    return false;
  }

  // Make sure the base address satisfies i64 ld / st alignment requirement.
  // At the moment, we ignore the memoryoperand's value.
  // If we want to use AliasAnalysis, we should check it accordingly.
  if (!Op0->hasOneMemOperand() ||
      (*Op0->memoperands_begin())->isVolatile())
    return false;

  unsigned Align = (*Op0->memoperands_begin())->getAlignment();
  const Function &Func = MF->getFunction();
  unsigned ReqAlign = STI->hasV6Ops()
    ? TD->getABITypeAlignment(Type::getInt64Ty(Func.getContext()))
    : 8;  // Pre-v6 need 8-byte align
  if (Align < ReqAlign)
    return false;

  // Then make sure the immediate offset fits.
  int OffImm = getMemoryOpOffset(*Op0);
  if (isT2) {
    int Limit = (1 << 8) * Scale;
    if (OffImm >= Limit || (OffImm <= -Limit) || (OffImm & (Scale-1)))
      return false;
    Offset = OffImm;
  } else {
    ARM_AM::AddrOpc AddSub = ARM_AM::add;
    if (OffImm < 0) {
      AddSub = ARM_AM::sub;
      OffImm = - OffImm;
    }
    int Limit = (1 << 8) * Scale;
    if (OffImm >= Limit || (OffImm & (Scale-1)))
      return false;
    Offset = ARM_AM::getAM3Opc(AddSub, OffImm);
  }
  FirstReg = Op0->getOperand(0).getReg();
  SecondReg = Op1->getOperand(0).getReg();
  if (FirstReg == SecondReg)
    return false;
  BaseReg = Op0->getOperand(1).getReg();
  Pred = getInstrPredicate(*Op0, PredReg);
  dl = Op0->getDebugLoc();
  return true;
}

bool ARMPreAllocLoadStoreOpt::RescheduleOps(MachineBasicBlock *MBB,
                                 SmallVectorImpl<MachineInstr *> &Ops,
                                 unsigned Base, bool isLd,
                                 DenseMap<MachineInstr*, unsigned> &MI2LocMap) {
  bool RetVal = false;

  // Sort by offset (in reverse order).
  llvm::sort(Ops, [](const MachineInstr *LHS, const MachineInstr *RHS) {
    int LOffset = getMemoryOpOffset(*LHS);
    int ROffset = getMemoryOpOffset(*RHS);
    assert(LHS == RHS || LOffset != ROffset);
    return LOffset > ROffset;
  });

  // The loads / stores of the same base are in order. Scan them from first to
  // last and check for the following:
  // 1. Any def of base.
  // 2. Any gaps.
  while (Ops.size() > 1) {
    unsigned FirstLoc = ~0U;
    unsigned LastLoc = 0;
    MachineInstr *FirstOp = nullptr;
    MachineInstr *LastOp = nullptr;
    int LastOffset = 0;
    unsigned LastOpcode = 0;
    unsigned LastBytes = 0;
    unsigned NumMove = 0;
    for (int i = Ops.size() - 1; i >= 0; --i) {
      // Make sure each operation has the same kind.
      MachineInstr *Op = Ops[i];
      unsigned LSMOpcode
        = getLoadStoreMultipleOpcode(Op->getOpcode(), ARM_AM::ia);
      if (LastOpcode && LSMOpcode != LastOpcode)
        break;

      // Check that we have a continuous set of offsets.
      int Offset = getMemoryOpOffset(*Op);
      unsigned Bytes = getLSMultipleTransferSize(Op);
      if (LastBytes) {
        if (Bytes != LastBytes || Offset != (LastOffset + (int)Bytes))
          break;
      }

      // Don't try to reschedule too many instructions.
      if (NumMove == 8) // FIXME: Tune this limit.
        break;

      // Found a mergable instruction; save information about it.
      ++NumMove;
      LastOffset = Offset;
      LastBytes = Bytes;
      LastOpcode = LSMOpcode;

      unsigned Loc = MI2LocMap[Op];
      if (Loc <= FirstLoc) {
        FirstLoc = Loc;
        FirstOp = Op;
      }
      if (Loc >= LastLoc) {
        LastLoc = Loc;
        LastOp = Op;
      }
    }

    if (NumMove <= 1)
      Ops.pop_back();
    else {
      SmallPtrSet<MachineInstr*, 4> MemOps;
      SmallSet<unsigned, 4> MemRegs;
      for (size_t i = Ops.size() - NumMove, e = Ops.size(); i != e; ++i) {
        MemOps.insert(Ops[i]);
        MemRegs.insert(Ops[i]->getOperand(0).getReg());
      }

      // Be conservative, if the instructions are too far apart, don't
      // move them. We want to limit the increase of register pressure.
      bool DoMove = (LastLoc - FirstLoc) <= NumMove*4; // FIXME: Tune this.
      if (DoMove)
        DoMove = IsSafeAndProfitableToMove(isLd, Base, FirstOp, LastOp,
                                           MemOps, MemRegs, TRI, AA);
      if (!DoMove) {
        for (unsigned i = 0; i != NumMove; ++i)
          Ops.pop_back();
      } else {
        // This is the new location for the loads / stores.
        MachineBasicBlock::iterator InsertPos = isLd ? FirstOp : LastOp;
        while (InsertPos != MBB->end() &&
               (MemOps.count(&*InsertPos) || InsertPos->isDebugInstr()))
          ++InsertPos;

        // If we are moving a pair of loads / stores, see if it makes sense
        // to try to allocate a pair of registers that can form register pairs.
        MachineInstr *Op0 = Ops.back();
        MachineInstr *Op1 = Ops[Ops.size()-2];
        unsigned FirstReg = 0, SecondReg = 0;
        unsigned BaseReg = 0, PredReg = 0;
        ARMCC::CondCodes Pred = ARMCC::AL;
        bool isT2 = false;
        unsigned NewOpc = 0;
        int Offset = 0;
        DebugLoc dl;
        if (NumMove == 2 && CanFormLdStDWord(Op0, Op1, dl, NewOpc,
                                             FirstReg, SecondReg, BaseReg,
                                             Offset, PredReg, Pred, isT2)) {
          Ops.pop_back();
          Ops.pop_back();

          const MCInstrDesc &MCID = TII->get(NewOpc);
          const TargetRegisterClass *TRC = TII->getRegClass(MCID, 0, TRI, *MF);
          MRI->constrainRegClass(FirstReg, TRC);
          MRI->constrainRegClass(SecondReg, TRC);

          // Form the pair instruction.
          if (isLd) {
            MachineInstrBuilder MIB = BuildMI(*MBB, InsertPos, dl, MCID)
              .addReg(FirstReg, RegState::Define)
              .addReg(SecondReg, RegState::Define)
              .addReg(BaseReg);
            // FIXME: We're converting from LDRi12 to an insn that still
            // uses addrmode2, so we need an explicit offset reg. It should
            // always by reg0 since we're transforming LDRi12s.
            if (!isT2)
              MIB.addReg(0);
            MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
            MIB.cloneMergedMemRefs({Op0, Op1});
            LLVM_DEBUG(dbgs() << "Formed " << *MIB << "\n");
            ++NumLDRDFormed;
          } else {
            MachineInstrBuilder MIB = BuildMI(*MBB, InsertPos, dl, MCID)
              .addReg(FirstReg)
              .addReg(SecondReg)
              .addReg(BaseReg);
            // FIXME: We're converting from LDRi12 to an insn that still
            // uses addrmode2, so we need an explicit offset reg. It should
            // always by reg0 since we're transforming STRi12s.
            if (!isT2)
              MIB.addReg(0);
            MIB.addImm(Offset).addImm(Pred).addReg(PredReg);
            MIB.cloneMergedMemRefs({Op0, Op1});
            LLVM_DEBUG(dbgs() << "Formed " << *MIB << "\n");
            ++NumSTRDFormed;
          }
          MBB->erase(Op0);
          MBB->erase(Op1);

          if (!isT2) {
            // Add register allocation hints to form register pairs.
            MRI->setRegAllocationHint(FirstReg, ARMRI::RegPairEven, SecondReg);
            MRI->setRegAllocationHint(SecondReg,  ARMRI::RegPairOdd, FirstReg);
          }
        } else {
          for (unsigned i = 0; i != NumMove; ++i) {
            MachineInstr *Op = Ops.back();
            Ops.pop_back();
            MBB->splice(InsertPos, MBB, Op);
          }
        }

        NumLdStMoved += NumMove;
        RetVal = true;
      }
    }
  }

  return RetVal;
}

bool
ARMPreAllocLoadStoreOpt::RescheduleLoadStoreInstrs(MachineBasicBlock *MBB) {
  bool RetVal = false;

  DenseMap<MachineInstr*, unsigned> MI2LocMap;
  DenseMap<unsigned, SmallVector<MachineInstr *, 4>> Base2LdsMap;
  DenseMap<unsigned, SmallVector<MachineInstr *, 4>> Base2StsMap;
  SmallVector<unsigned, 4> LdBases;
  SmallVector<unsigned, 4> StBases;

  unsigned Loc = 0;
  MachineBasicBlock::iterator MBBI = MBB->begin();
  MachineBasicBlock::iterator E = MBB->end();
  while (MBBI != E) {
    for (; MBBI != E; ++MBBI) {
      MachineInstr &MI = *MBBI;
      if (MI.isCall() || MI.isTerminator()) {
        // Stop at barriers.
        ++MBBI;
        break;
      }

      if (!MI.isDebugInstr())
        MI2LocMap[&MI] = ++Loc;

      if (!isMemoryOp(MI))
        continue;
      unsigned PredReg = 0;
      if (getInstrPredicate(MI, PredReg) != ARMCC::AL)
        continue;

      int Opc = MI.getOpcode();
      bool isLd = isLoadSingle(Opc);
      unsigned Base = MI.getOperand(1).getReg();
      int Offset = getMemoryOpOffset(MI);

      bool StopHere = false;
      if (isLd) {
        DenseMap<unsigned, SmallVector<MachineInstr *, 4>>::iterator BI =
          Base2LdsMap.find(Base);
        if (BI != Base2LdsMap.end()) {
          for (unsigned i = 0, e = BI->second.size(); i != e; ++i) {
            if (Offset == getMemoryOpOffset(*BI->second[i])) {
              StopHere = true;
              break;
            }
          }
          if (!StopHere)
            BI->second.push_back(&MI);
        } else {
          Base2LdsMap[Base].push_back(&MI);
          LdBases.push_back(Base);
        }
      } else {
        DenseMap<unsigned, SmallVector<MachineInstr *, 4>>::iterator BI =
          Base2StsMap.find(Base);
        if (BI != Base2StsMap.end()) {
          for (unsigned i = 0, e = BI->second.size(); i != e; ++i) {
            if (Offset == getMemoryOpOffset(*BI->second[i])) {
              StopHere = true;
              break;
            }
          }
          if (!StopHere)
            BI->second.push_back(&MI);
        } else {
          Base2StsMap[Base].push_back(&MI);
          StBases.push_back(Base);
        }
      }

      if (StopHere) {
        // Found a duplicate (a base+offset combination that's seen earlier).
        // Backtrack.
        --Loc;
        break;
      }
    }

    // Re-schedule loads.
    for (unsigned i = 0, e = LdBases.size(); i != e; ++i) {
      unsigned Base = LdBases[i];
      SmallVectorImpl<MachineInstr *> &Lds = Base2LdsMap[Base];
      if (Lds.size() > 1)
        RetVal |= RescheduleOps(MBB, Lds, Base, true, MI2LocMap);
    }

    // Re-schedule stores.
    for (unsigned i = 0, e = StBases.size(); i != e; ++i) {
      unsigned Base = StBases[i];
      SmallVectorImpl<MachineInstr *> &Sts = Base2StsMap[Base];
      if (Sts.size() > 1)
        RetVal |= RescheduleOps(MBB, Sts, Base, false, MI2LocMap);
    }

    if (MBBI != E) {
      Base2LdsMap.clear();
      Base2StsMap.clear();
      LdBases.clear();
      StBases.clear();
    }
  }

  return RetVal;
}

/// Returns an instance of the load / store optimization pass.
FunctionPass *llvm::createARMLoadStoreOptimizationPass(bool PreAlloc) {
  if (PreAlloc)
    return new ARMPreAllocLoadStoreOpt();
  return new ARMLoadStoreOpt();
}
