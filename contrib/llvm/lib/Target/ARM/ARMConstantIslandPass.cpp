//===- ARMConstantIslandPass.cpp - ARM constant islands -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that splits the constant pool up into 'islands'
// which are scattered through-out the function.  This is required due to the
// limited pc-relative displacements that ARM has.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBasicBlockInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Thumb2InstrInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "arm-cp-islands"

#define ARM_CP_ISLANDS_OPT_NAME \
  "ARM constant island placement and branch shortening pass"
STATISTIC(NumCPEs,       "Number of constpool entries");
STATISTIC(NumSplit,      "Number of uncond branches inserted");
STATISTIC(NumCBrFixed,   "Number of cond branches fixed");
STATISTIC(NumUBrFixed,   "Number of uncond branches fixed");
STATISTIC(NumTBs,        "Number of table branches generated");
STATISTIC(NumT2CPShrunk, "Number of Thumb2 constantpool instructions shrunk");
STATISTIC(NumT2BrShrunk, "Number of Thumb2 immediate branches shrunk");
STATISTIC(NumCBZ,        "Number of CBZ / CBNZ formed");
STATISTIC(NumJTMoved,    "Number of jump table destination blocks moved");
STATISTIC(NumJTInserted, "Number of jump table intermediate blocks inserted");

static cl::opt<bool>
AdjustJumpTableBlocks("arm-adjust-jump-tables", cl::Hidden, cl::init(true),
          cl::desc("Adjust basic block layout to better use TB[BH]"));

static cl::opt<unsigned>
CPMaxIteration("arm-constant-island-max-iteration", cl::Hidden, cl::init(30),
          cl::desc("The max number of iteration for converge"));

static cl::opt<bool> SynthesizeThumb1TBB(
    "arm-synthesize-thumb-1-tbb", cl::Hidden, cl::init(true),
    cl::desc("Use compressed jump tables in Thumb-1 by synthesizing an "
             "equivalent to the TBB/TBH instructions"));

namespace {

  /// ARMConstantIslands - Due to limited PC-relative displacements, ARM
  /// requires constant pool entries to be scattered among the instructions
  /// inside a function.  To do this, it completely ignores the normal LLVM
  /// constant pool; instead, it places constants wherever it feels like with
  /// special instructions.
  ///
  /// The terminology used in this pass includes:
  ///   Islands - Clumps of constants placed in the function.
  ///   Water   - Potential places where an island could be formed.
  ///   CPE     - A constant pool entry that has been placed somewhere, which
  ///             tracks a list of users.
  class ARMConstantIslands : public MachineFunctionPass {
    std::vector<BasicBlockInfo> BBInfo;

    /// WaterList - A sorted list of basic blocks where islands could be placed
    /// (i.e. blocks that don't fall through to the following block, due
    /// to a return, unreachable, or unconditional branch).
    std::vector<MachineBasicBlock*> WaterList;

    /// NewWaterList - The subset of WaterList that was created since the
    /// previous iteration by inserting unconditional branches.
    SmallSet<MachineBasicBlock*, 4> NewWaterList;

    using water_iterator = std::vector<MachineBasicBlock *>::iterator;

    /// CPUser - One user of a constant pool, keeping the machine instruction
    /// pointer, the constant pool being referenced, and the max displacement
    /// allowed from the instruction to the CP.  The HighWaterMark records the
    /// highest basic block where a new CPEntry can be placed.  To ensure this
    /// pass terminates, the CP entries are initially placed at the end of the
    /// function and then move monotonically to lower addresses.  The
    /// exception to this rule is when the current CP entry for a particular
    /// CPUser is out of range, but there is another CP entry for the same
    /// constant value in range.  We want to use the existing in-range CP
    /// entry, but if it later moves out of range, the search for new water
    /// should resume where it left off.  The HighWaterMark is used to record
    /// that point.
    struct CPUser {
      MachineInstr *MI;
      MachineInstr *CPEMI;
      MachineBasicBlock *HighWaterMark;
      unsigned MaxDisp;
      bool NegOk;
      bool IsSoImm;
      bool KnownAlignment = false;

      CPUser(MachineInstr *mi, MachineInstr *cpemi, unsigned maxdisp,
             bool neg, bool soimm)
        : MI(mi), CPEMI(cpemi), MaxDisp(maxdisp), NegOk(neg), IsSoImm(soimm) {
        HighWaterMark = CPEMI->getParent();
      }

      /// getMaxDisp - Returns the maximum displacement supported by MI.
      /// Correct for unknown alignment.
      /// Conservatively subtract 2 bytes to handle weird alignment effects.
      unsigned getMaxDisp() const {
        return (KnownAlignment ? MaxDisp : MaxDisp - 2) - 2;
      }
    };

    /// CPUsers - Keep track of all of the machine instructions that use various
    /// constant pools and their max displacement.
    std::vector<CPUser> CPUsers;

    /// CPEntry - One per constant pool entry, keeping the machine instruction
    /// pointer, the constpool index, and the number of CPUser's which
    /// reference this entry.
    struct CPEntry {
      MachineInstr *CPEMI;
      unsigned CPI;
      unsigned RefCount;

      CPEntry(MachineInstr *cpemi, unsigned cpi, unsigned rc = 0)
        : CPEMI(cpemi), CPI(cpi), RefCount(rc) {}
    };

    /// CPEntries - Keep track of all of the constant pool entry machine
    /// instructions. For each original constpool index (i.e. those that existed
    /// upon entry to this pass), it keeps a vector of entries.  Original
    /// elements are cloned as we go along; the clones are put in the vector of
    /// the original element, but have distinct CPIs.
    ///
    /// The first half of CPEntries contains generic constants, the second half
    /// contains jump tables. Use getCombinedIndex on a generic CPEMI to look up
    /// which vector it will be in here.
    std::vector<std::vector<CPEntry>> CPEntries;

    /// Maps a JT index to the offset in CPEntries containing copies of that
    /// table. The equivalent map for a CONSTPOOL_ENTRY is the identity.
    DenseMap<int, int> JumpTableEntryIndices;

    /// Maps a JT index to the LEA that actually uses the index to calculate its
    /// base address.
    DenseMap<int, int> JumpTableUserIndices;

    /// ImmBranch - One per immediate branch, keeping the machine instruction
    /// pointer, conditional or unconditional, the max displacement,
    /// and (if isCond is true) the corresponding unconditional branch
    /// opcode.
    struct ImmBranch {
      MachineInstr *MI;
      unsigned MaxDisp : 31;
      bool isCond : 1;
      unsigned UncondBr;

      ImmBranch(MachineInstr *mi, unsigned maxdisp, bool cond, unsigned ubr)
        : MI(mi), MaxDisp(maxdisp), isCond(cond), UncondBr(ubr) {}
    };

    /// ImmBranches - Keep track of all the immediate branch instructions.
    std::vector<ImmBranch> ImmBranches;

    /// PushPopMIs - Keep track of all the Thumb push / pop instructions.
    SmallVector<MachineInstr*, 4> PushPopMIs;

    /// T2JumpTables - Keep track of all the Thumb2 jumptable instructions.
    SmallVector<MachineInstr*, 4> T2JumpTables;

    /// HasFarJump - True if any far jump instruction has been emitted during
    /// the branch fix up pass.
    bool HasFarJump;

    MachineFunction *MF;
    MachineConstantPool *MCP;
    const ARMBaseInstrInfo *TII;
    const ARMSubtarget *STI;
    ARMFunctionInfo *AFI;
    bool isThumb;
    bool isThumb1;
    bool isThumb2;
    bool isPositionIndependentOrROPI;

  public:
    static char ID;

    ARMConstantIslands() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &MF) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return ARM_CP_ISLANDS_OPT_NAME;
    }

  private:
    void doInitialConstPlacement(std::vector<MachineInstr *> &CPEMIs);
    void doInitialJumpTablePlacement(std::vector<MachineInstr *> &CPEMIs);
    bool BBHasFallthrough(MachineBasicBlock *MBB);
    CPEntry *findConstPoolEntry(unsigned CPI, const MachineInstr *CPEMI);
    unsigned getCPELogAlign(const MachineInstr *CPEMI);
    void scanFunctionJumpTables();
    void initializeFunctionInfo(const std::vector<MachineInstr*> &CPEMIs);
    MachineBasicBlock *splitBlockBeforeInstr(MachineInstr *MI);
    void updateForInsertedWaterBlock(MachineBasicBlock *NewBB);
    void adjustBBOffsetsAfter(MachineBasicBlock *BB);
    bool decrementCPEReferenceCount(unsigned CPI, MachineInstr* CPEMI);
    unsigned getCombinedIndex(const MachineInstr *CPEMI);
    int findInRangeCPEntry(CPUser& U, unsigned UserOffset);
    bool findAvailableWater(CPUser&U, unsigned UserOffset,
                            water_iterator &WaterIter, bool CloserWater);
    void createNewWater(unsigned CPUserIndex, unsigned UserOffset,
                        MachineBasicBlock *&NewMBB);
    bool handleConstantPoolUser(unsigned CPUserIndex, bool CloserWater);
    void removeDeadCPEMI(MachineInstr *CPEMI);
    bool removeUnusedCPEntries();
    bool isCPEntryInRange(MachineInstr *MI, unsigned UserOffset,
                          MachineInstr *CPEMI, unsigned Disp, bool NegOk,
                          bool DoDump = false);
    bool isWaterInRange(unsigned UserOffset, MachineBasicBlock *Water,
                        CPUser &U, unsigned &Growth);
    bool isBBInRange(MachineInstr *MI, MachineBasicBlock *BB, unsigned Disp);
    bool fixupImmediateBr(ImmBranch &Br);
    bool fixupConditionalBr(ImmBranch &Br);
    bool fixupUnconditionalBr(ImmBranch &Br);
    bool undoLRSpillRestore();
    bool optimizeThumb2Instructions();
    bool optimizeThumb2Branches();
    bool reorderThumb2JumpTables();
    bool preserveBaseRegister(MachineInstr *JumpMI, MachineInstr *LEAMI,
                              unsigned &DeadSize, bool &CanDeleteLEA,
                              bool &BaseRegKill);
    bool optimizeThumb2JumpTables();
    MachineBasicBlock *adjustJTTargetBlockForward(MachineBasicBlock *BB,
                                                  MachineBasicBlock *JTBB);

    unsigned getOffsetOf(MachineInstr *MI) const;
    unsigned getUserOffset(CPUser&) const;
    void dumpBBs();
    void verify();

    bool isOffsetInRange(unsigned UserOffset, unsigned TrialOffset,
                         unsigned Disp, bool NegativeOK, bool IsSoImm = false);
    bool isOffsetInRange(unsigned UserOffset, unsigned TrialOffset,
                         const CPUser &U) {
      return isOffsetInRange(UserOffset, TrialOffset,
                             U.getMaxDisp(), U.NegOk, U.IsSoImm);
    }
  };

} // end anonymous namespace

char ARMConstantIslands::ID = 0;

/// verify - check BBOffsets, BBSizes, alignment of islands
void ARMConstantIslands::verify() {
#ifndef NDEBUG
  assert(std::is_sorted(MF->begin(), MF->end(),
                        [this](const MachineBasicBlock &LHS,
                               const MachineBasicBlock &RHS) {
                          return BBInfo[LHS.getNumber()].postOffset() <
                                 BBInfo[RHS.getNumber()].postOffset();
                        }));
  LLVM_DEBUG(dbgs() << "Verifying " << CPUsers.size() << " CP users.\n");
  for (unsigned i = 0, e = CPUsers.size(); i != e; ++i) {
    CPUser &U = CPUsers[i];
    unsigned UserOffset = getUserOffset(U);
    // Verify offset using the real max displacement without the safety
    // adjustment.
    if (isCPEntryInRange(U.MI, UserOffset, U.CPEMI, U.getMaxDisp()+2, U.NegOk,
                         /* DoDump = */ true)) {
      LLVM_DEBUG(dbgs() << "OK\n");
      continue;
    }
    LLVM_DEBUG(dbgs() << "Out of range.\n");
    dumpBBs();
    LLVM_DEBUG(MF->dump());
    llvm_unreachable("Constant pool entry out of range!");
  }
#endif
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// print block size and offset information - debugging
LLVM_DUMP_METHOD void ARMConstantIslands::dumpBBs() {
  LLVM_DEBUG({
    for (unsigned J = 0, E = BBInfo.size(); J !=E; ++J) {
      const BasicBlockInfo &BBI = BBInfo[J];
      dbgs() << format("%08x %bb.%u\t", BBI.Offset, J)
             << " kb=" << unsigned(BBI.KnownBits)
             << " ua=" << unsigned(BBI.Unalign)
             << " pa=" << unsigned(BBI.PostAlign)
             << format(" size=%#x\n", BBInfo[J].Size);
    }
  });
}
#endif

bool ARMConstantIslands::runOnMachineFunction(MachineFunction &mf) {
  MF = &mf;
  MCP = mf.getConstantPool();

  LLVM_DEBUG(dbgs() << "***** ARMConstantIslands: "
                    << MCP->getConstants().size() << " CP entries, aligned to "
                    << MCP->getConstantPoolAlignment() << " bytes *****\n");

  STI = &static_cast<const ARMSubtarget &>(MF->getSubtarget());
  TII = STI->getInstrInfo();
  isPositionIndependentOrROPI =
      STI->getTargetLowering()->isPositionIndependent() || STI->isROPI();
  AFI = MF->getInfo<ARMFunctionInfo>();

  isThumb = AFI->isThumbFunction();
  isThumb1 = AFI->isThumb1OnlyFunction();
  isThumb2 = AFI->isThumb2Function();

  HasFarJump = false;
  bool GenerateTBB = isThumb2 || (isThumb1 && SynthesizeThumb1TBB);

  // This pass invalidates liveness information when it splits basic blocks.
  MF->getRegInfo().invalidateLiveness();

  // Renumber all of the machine basic blocks in the function, guaranteeing that
  // the numbers agree with the position of the block in the function.
  MF->RenumberBlocks();

  // Try to reorder and otherwise adjust the block layout to make good use
  // of the TB[BH] instructions.
  bool MadeChange = false;
  if (GenerateTBB && AdjustJumpTableBlocks) {
    scanFunctionJumpTables();
    MadeChange |= reorderThumb2JumpTables();
    // Data is out of date, so clear it. It'll be re-computed later.
    T2JumpTables.clear();
    // Blocks may have shifted around. Keep the numbering up to date.
    MF->RenumberBlocks();
  }

  // Perform the initial placement of the constant pool entries.  To start with,
  // we put them all at the end of the function.
  std::vector<MachineInstr*> CPEMIs;
  if (!MCP->isEmpty())
    doInitialConstPlacement(CPEMIs);

  if (MF->getJumpTableInfo())
    doInitialJumpTablePlacement(CPEMIs);

  /// The next UID to take is the first unused one.
  AFI->initPICLabelUId(CPEMIs.size());

  // Do the initial scan of the function, building up information about the
  // sizes of each block, the location of all the water, and finding all of the
  // constant pool users.
  initializeFunctionInfo(CPEMIs);
  CPEMIs.clear();
  LLVM_DEBUG(dumpBBs());

  // Functions with jump tables need an alignment of 4 because they use the ADR
  // instruction, which aligns the PC to 4 bytes before adding an offset.
  if (!T2JumpTables.empty())
    MF->ensureAlignment(2);

  /// Remove dead constant pool entries.
  MadeChange |= removeUnusedCPEntries();

  // Iteratively place constant pool entries and fix up branches until there
  // is no change.
  unsigned NoCPIters = 0, NoBRIters = 0;
  while (true) {
    LLVM_DEBUG(dbgs() << "Beginning CP iteration #" << NoCPIters << '\n');
    bool CPChange = false;
    for (unsigned i = 0, e = CPUsers.size(); i != e; ++i)
      // For most inputs, it converges in no more than 5 iterations.
      // If it doesn't end in 10, the input may have huge BB or many CPEs.
      // In this case, we will try different heuristics.
      CPChange |= handleConstantPoolUser(i, NoCPIters >= CPMaxIteration / 2);
    if (CPChange && ++NoCPIters > CPMaxIteration)
      report_fatal_error("Constant Island pass failed to converge!");
    LLVM_DEBUG(dumpBBs());

    // Clear NewWaterList now.  If we split a block for branches, it should
    // appear as "new water" for the next iteration of constant pool placement.
    NewWaterList.clear();

    LLVM_DEBUG(dbgs() << "Beginning BR iteration #" << NoBRIters << '\n');
    bool BRChange = false;
    for (unsigned i = 0, e = ImmBranches.size(); i != e; ++i)
      BRChange |= fixupImmediateBr(ImmBranches[i]);
    if (BRChange && ++NoBRIters > 30)
      report_fatal_error("Branch Fix Up pass failed to converge!");
    LLVM_DEBUG(dumpBBs());

    if (!CPChange && !BRChange)
      break;
    MadeChange = true;
  }

  // Shrink 32-bit Thumb2 load and store instructions.
  if (isThumb2 && !STI->prefers32BitThumb())
    MadeChange |= optimizeThumb2Instructions();

  // Shrink 32-bit branch instructions.
  if (isThumb && STI->hasV8MBaselineOps())
    MadeChange |= optimizeThumb2Branches();

  // Optimize jump tables using TBB / TBH.
  if (GenerateTBB && !STI->genExecuteOnly())
    MadeChange |= optimizeThumb2JumpTables();

  // After a while, this might be made debug-only, but it is not expensive.
  verify();

  // If LR has been forced spilled and no far jump (i.e. BL) has been issued,
  // undo the spill / restore of LR if possible.
  if (isThumb && !HasFarJump && AFI->isLRSpilledForFarJump())
    MadeChange |= undoLRSpillRestore();

  // Save the mapping between original and cloned constpool entries.
  for (unsigned i = 0, e = CPEntries.size(); i != e; ++i) {
    for (unsigned j = 0, je = CPEntries[i].size(); j != je; ++j) {
      const CPEntry & CPE = CPEntries[i][j];
      if (CPE.CPEMI && CPE.CPEMI->getOperand(1).isCPI())
        AFI->recordCPEClone(i, CPE.CPI);
    }
  }

  LLVM_DEBUG(dbgs() << '\n'; dumpBBs());

  BBInfo.clear();
  WaterList.clear();
  CPUsers.clear();
  CPEntries.clear();
  JumpTableEntryIndices.clear();
  JumpTableUserIndices.clear();
  ImmBranches.clear();
  PushPopMIs.clear();
  T2JumpTables.clear();

  return MadeChange;
}

/// Perform the initial placement of the regular constant pool entries.
/// To start with, we put them all at the end of the function.
void
ARMConstantIslands::doInitialConstPlacement(std::vector<MachineInstr*> &CPEMIs) {
  // Create the basic block to hold the CPE's.
  MachineBasicBlock *BB = MF->CreateMachineBasicBlock();
  MF->push_back(BB);

  // MachineConstantPool measures alignment in bytes. We measure in log2(bytes).
  unsigned MaxAlign = Log2_32(MCP->getConstantPoolAlignment());

  // Mark the basic block as required by the const-pool.
  BB->setAlignment(MaxAlign);

  // The function needs to be as aligned as the basic blocks. The linker may
  // move functions around based on their alignment.
  MF->ensureAlignment(BB->getAlignment());

  // Order the entries in BB by descending alignment.  That ensures correct
  // alignment of all entries as long as BB is sufficiently aligned.  Keep
  // track of the insertion point for each alignment.  We are going to bucket
  // sort the entries as they are created.
  SmallVector<MachineBasicBlock::iterator, 8> InsPoint(MaxAlign + 1, BB->end());

  // Add all of the constants from the constant pool to the end block, use an
  // identity mapping of CPI's to CPE's.
  const std::vector<MachineConstantPoolEntry> &CPs = MCP->getConstants();

  const DataLayout &TD = MF->getDataLayout();
  for (unsigned i = 0, e = CPs.size(); i != e; ++i) {
    unsigned Size = TD.getTypeAllocSize(CPs[i].getType());
    unsigned Align = CPs[i].getAlignment();
    assert(isPowerOf2_32(Align) && "Invalid alignment");
    // Verify that all constant pool entries are a multiple of their alignment.
    // If not, we would have to pad them out so that instructions stay aligned.
    assert((Size % Align) == 0 && "CP Entry not multiple of 4 bytes!");

    // Insert CONSTPOOL_ENTRY before entries with a smaller alignment.
    unsigned LogAlign = Log2_32(Align);
    MachineBasicBlock::iterator InsAt = InsPoint[LogAlign];
    MachineInstr *CPEMI =
      BuildMI(*BB, InsAt, DebugLoc(), TII->get(ARM::CONSTPOOL_ENTRY))
        .addImm(i).addConstantPoolIndex(i).addImm(Size);
    CPEMIs.push_back(CPEMI);

    // Ensure that future entries with higher alignment get inserted before
    // CPEMI. This is bucket sort with iterators.
    for (unsigned a = LogAlign + 1; a <= MaxAlign; ++a)
      if (InsPoint[a] == InsAt)
        InsPoint[a] = CPEMI;

    // Add a new CPEntry, but no corresponding CPUser yet.
    CPEntries.emplace_back(1, CPEntry(CPEMI, i));
    ++NumCPEs;
    LLVM_DEBUG(dbgs() << "Moved CPI#" << i << " to end of function, size = "
                      << Size << ", align = " << Align << '\n');
  }
  LLVM_DEBUG(BB->dump());
}

/// Do initial placement of the jump tables. Because Thumb2's TBB and TBH
/// instructions can be made more efficient if the jump table immediately
/// follows the instruction, it's best to place them immediately next to their
/// jumps to begin with. In almost all cases they'll never be moved from that
/// position.
void ARMConstantIslands::doInitialJumpTablePlacement(
    std::vector<MachineInstr *> &CPEMIs) {
  unsigned i = CPEntries.size();
  auto MJTI = MF->getJumpTableInfo();
  const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();

  MachineBasicBlock *LastCorrectlyNumberedBB = nullptr;
  for (MachineBasicBlock &MBB : *MF) {
    auto MI = MBB.getLastNonDebugInstr();
    if (MI == MBB.end())
      continue;

    unsigned JTOpcode;
    switch (MI->getOpcode()) {
    default:
      continue;
    case ARM::BR_JTadd:
    case ARM::BR_JTr:
    case ARM::tBR_JTr:
    case ARM::BR_JTm_i12:
    case ARM::BR_JTm_rs:
      JTOpcode = ARM::JUMPTABLE_ADDRS;
      break;
    case ARM::t2BR_JT:
      JTOpcode = ARM::JUMPTABLE_INSTS;
      break;
    case ARM::tTBB_JT:
    case ARM::t2TBB_JT:
      JTOpcode = ARM::JUMPTABLE_TBB;
      break;
    case ARM::tTBH_JT:
    case ARM::t2TBH_JT:
      JTOpcode = ARM::JUMPTABLE_TBH;
      break;
    }

    unsigned NumOps = MI->getDesc().getNumOperands();
    MachineOperand JTOp =
      MI->getOperand(NumOps - (MI->isPredicable() ? 2 : 1));
    unsigned JTI = JTOp.getIndex();
    unsigned Size = JT[JTI].MBBs.size() * sizeof(uint32_t);
    MachineBasicBlock *JumpTableBB = MF->CreateMachineBasicBlock();
    MF->insert(std::next(MachineFunction::iterator(MBB)), JumpTableBB);
    MachineInstr *CPEMI = BuildMI(*JumpTableBB, JumpTableBB->begin(),
                                  DebugLoc(), TII->get(JTOpcode))
                              .addImm(i++)
                              .addJumpTableIndex(JTI)
                              .addImm(Size);
    CPEMIs.push_back(CPEMI);
    CPEntries.emplace_back(1, CPEntry(CPEMI, JTI));
    JumpTableEntryIndices.insert(std::make_pair(JTI, CPEntries.size() - 1));
    if (!LastCorrectlyNumberedBB)
      LastCorrectlyNumberedBB = &MBB;
  }

  // If we did anything then we need to renumber the subsequent blocks.
  if (LastCorrectlyNumberedBB)
    MF->RenumberBlocks(LastCorrectlyNumberedBB);
}

/// BBHasFallthrough - Return true if the specified basic block can fallthrough
/// into the block immediately after it.
bool ARMConstantIslands::BBHasFallthrough(MachineBasicBlock *MBB) {
  // Get the next machine basic block in the function.
  MachineFunction::iterator MBBI = MBB->getIterator();
  // Can't fall off end of function.
  if (std::next(MBBI) == MBB->getParent()->end())
    return false;

  MachineBasicBlock *NextBB = &*std::next(MBBI);
  if (!MBB->isSuccessor(NextBB))
    return false;

  // Try to analyze the end of the block. A potential fallthrough may already
  // have an unconditional branch for whatever reason.
  MachineBasicBlock *TBB, *FBB;
  SmallVector<MachineOperand, 4> Cond;
  bool TooDifficult = TII->analyzeBranch(*MBB, TBB, FBB, Cond);
  return TooDifficult || FBB == nullptr;
}

/// findConstPoolEntry - Given the constpool index and CONSTPOOL_ENTRY MI,
/// look up the corresponding CPEntry.
ARMConstantIslands::CPEntry *
ARMConstantIslands::findConstPoolEntry(unsigned CPI,
                                       const MachineInstr *CPEMI) {
  std::vector<CPEntry> &CPEs = CPEntries[CPI];
  // Number of entries per constpool index should be small, just do a
  // linear search.
  for (unsigned i = 0, e = CPEs.size(); i != e; ++i) {
    if (CPEs[i].CPEMI == CPEMI)
      return &CPEs[i];
  }
  return nullptr;
}

/// getCPELogAlign - Returns the required alignment of the constant pool entry
/// represented by CPEMI.  Alignment is measured in log2(bytes) units.
unsigned ARMConstantIslands::getCPELogAlign(const MachineInstr *CPEMI) {
  switch (CPEMI->getOpcode()) {
  case ARM::CONSTPOOL_ENTRY:
    break;
  case ARM::JUMPTABLE_TBB:
    return isThumb1 ? 2 : 0;
  case ARM::JUMPTABLE_TBH:
    return isThumb1 ? 2 : 1;
  case ARM::JUMPTABLE_INSTS:
    return 1;
  case ARM::JUMPTABLE_ADDRS:
    return 2;
  default:
    llvm_unreachable("unknown constpool entry kind");
  }

  unsigned CPI = getCombinedIndex(CPEMI);
  assert(CPI < MCP->getConstants().size() && "Invalid constant pool index.");
  unsigned Align = MCP->getConstants()[CPI].getAlignment();
  assert(isPowerOf2_32(Align) && "Invalid CPE alignment");
  return Log2_32(Align);
}

/// scanFunctionJumpTables - Do a scan of the function, building up
/// information about the sizes of each block and the locations of all
/// the jump tables.
void ARMConstantIslands::scanFunctionJumpTables() {
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &I : MBB)
      if (I.isBranch() &&
          (I.getOpcode() == ARM::t2BR_JT || I.getOpcode() == ARM::tBR_JTr))
        T2JumpTables.push_back(&I);
  }
}

/// initializeFunctionInfo - Do the initial scan of the function, building up
/// information about the sizes of each block, the location of all the water,
/// and finding all of the constant pool users.
void ARMConstantIslands::
initializeFunctionInfo(const std::vector<MachineInstr*> &CPEMIs) {

  BBInfo = computeAllBlockSizes(MF);

  // The known bits of the entry block offset are determined by the function
  // alignment.
  BBInfo.front().KnownBits = MF->getAlignment();

  // Compute block offsets and known bits.
  adjustBBOffsetsAfter(&MF->front());

  // Now go back through the instructions and build up our data structures.
  for (MachineBasicBlock &MBB : *MF) {
    // If this block doesn't fall through into the next MBB, then this is
    // 'water' that a constant pool island could be placed.
    if (!BBHasFallthrough(&MBB))
      WaterList.push_back(&MBB);

    for (MachineInstr &I : MBB) {
      if (I.isDebugInstr())
        continue;

      unsigned Opc = I.getOpcode();
      if (I.isBranch()) {
        bool isCond = false;
        unsigned Bits = 0;
        unsigned Scale = 1;
        int UOpc = Opc;
        switch (Opc) {
        default:
          continue;  // Ignore other JT branches
        case ARM::t2BR_JT:
        case ARM::tBR_JTr:
          T2JumpTables.push_back(&I);
          continue;   // Does not get an entry in ImmBranches
        case ARM::Bcc:
          isCond = true;
          UOpc = ARM::B;
          LLVM_FALLTHROUGH;
        case ARM::B:
          Bits = 24;
          Scale = 4;
          break;
        case ARM::tBcc:
          isCond = true;
          UOpc = ARM::tB;
          Bits = 8;
          Scale = 2;
          break;
        case ARM::tB:
          Bits = 11;
          Scale = 2;
          break;
        case ARM::t2Bcc:
          isCond = true;
          UOpc = ARM::t2B;
          Bits = 20;
          Scale = 2;
          break;
        case ARM::t2B:
          Bits = 24;
          Scale = 2;
          break;
        }

        // Record this immediate branch.
        unsigned MaxOffs = ((1 << (Bits-1))-1) * Scale;
        ImmBranches.push_back(ImmBranch(&I, MaxOffs, isCond, UOpc));
      }

      if (Opc == ARM::tPUSH || Opc == ARM::tPOP_RET)
        PushPopMIs.push_back(&I);

      if (Opc == ARM::CONSTPOOL_ENTRY || Opc == ARM::JUMPTABLE_ADDRS ||
          Opc == ARM::JUMPTABLE_INSTS || Opc == ARM::JUMPTABLE_TBB ||
          Opc == ARM::JUMPTABLE_TBH)
        continue;

      // Scan the instructions for constant pool operands.
      for (unsigned op = 0, e = I.getNumOperands(); op != e; ++op)
        if (I.getOperand(op).isCPI() || I.getOperand(op).isJTI()) {
          // We found one.  The addressing mode tells us the max displacement
          // from the PC that this instruction permits.

          // Basic size info comes from the TSFlags field.
          unsigned Bits = 0;
          unsigned Scale = 1;
          bool NegOk = false;
          bool IsSoImm = false;

          switch (Opc) {
          default:
            llvm_unreachable("Unknown addressing mode for CP reference!");

          // Taking the address of a CP entry.
          case ARM::LEApcrel:
          case ARM::LEApcrelJT:
            // This takes a SoImm, which is 8 bit immediate rotated. We'll
            // pretend the maximum offset is 255 * 4. Since each instruction
            // 4 byte wide, this is always correct. We'll check for other
            // displacements that fits in a SoImm as well.
            Bits = 8;
            Scale = 4;
            NegOk = true;
            IsSoImm = true;
            break;
          case ARM::t2LEApcrel:
          case ARM::t2LEApcrelJT:
            Bits = 12;
            NegOk = true;
            break;
          case ARM::tLEApcrel:
          case ARM::tLEApcrelJT:
            Bits = 8;
            Scale = 4;
            break;

          case ARM::LDRBi12:
          case ARM::LDRi12:
          case ARM::LDRcp:
          case ARM::t2LDRpci:
          case ARM::t2LDRHpci:
          case ARM::t2LDRBpci:
            Bits = 12;  // +-offset_12
            NegOk = true;
            break;

          case ARM::tLDRpci:
            Bits = 8;
            Scale = 4;  // +(offset_8*4)
            break;

          case ARM::VLDRD:
          case ARM::VLDRS:
            Bits = 8;
            Scale = 4;  // +-(offset_8*4)
            NegOk = true;
            break;
          case ARM::VLDRH:
            Bits = 8;
            Scale = 2;  // +-(offset_8*2)
            NegOk = true;
            break;

          case ARM::tLDRHi:
            Bits = 5;
            Scale = 2; // +(offset_5*2)
            break;
          }

          // Remember that this is a user of a CP entry.
          unsigned CPI = I.getOperand(op).getIndex();
          if (I.getOperand(op).isJTI()) {
            JumpTableUserIndices.insert(std::make_pair(CPI, CPUsers.size()));
            CPI = JumpTableEntryIndices[CPI];
          }

          MachineInstr *CPEMI = CPEMIs[CPI];
          unsigned MaxOffs = ((1 << Bits)-1) * Scale;
          CPUsers.push_back(CPUser(&I, CPEMI, MaxOffs, NegOk, IsSoImm));

          // Increment corresponding CPEntry reference count.
          CPEntry *CPE = findConstPoolEntry(CPI, CPEMI);
          assert(CPE && "Cannot find a corresponding CPEntry!");
          CPE->RefCount++;

          // Instructions can only use one CP entry, don't bother scanning the
          // rest of the operands.
          break;
        }
    }
  }
}

/// getOffsetOf - Return the current offset of the specified machine instruction
/// from the start of the function.  This offset changes as stuff is moved
/// around inside the function.
unsigned ARMConstantIslands::getOffsetOf(MachineInstr *MI) const {
  MachineBasicBlock *MBB = MI->getParent();

  // The offset is composed of two things: the sum of the sizes of all MBB's
  // before this instruction's block, and the offset from the start of the block
  // it is in.
  unsigned Offset = BBInfo[MBB->getNumber()].Offset;

  // Sum instructions before MI in MBB.
  for (MachineBasicBlock::iterator I = MBB->begin(); &*I != MI; ++I) {
    assert(I != MBB->end() && "Didn't find MI in its own basic block?");
    Offset += TII->getInstSizeInBytes(*I);
  }
  return Offset;
}

/// CompareMBBNumbers - Little predicate function to sort the WaterList by MBB
/// ID.
static bool CompareMBBNumbers(const MachineBasicBlock *LHS,
                              const MachineBasicBlock *RHS) {
  return LHS->getNumber() < RHS->getNumber();
}

/// updateForInsertedWaterBlock - When a block is newly inserted into the
/// machine function, it upsets all of the block numbers.  Renumber the blocks
/// and update the arrays that parallel this numbering.
void ARMConstantIslands::updateForInsertedWaterBlock(MachineBasicBlock *NewBB) {
  // Renumber the MBB's to keep them consecutive.
  NewBB->getParent()->RenumberBlocks(NewBB);

  // Insert an entry into BBInfo to align it properly with the (newly
  // renumbered) block numbers.
  BBInfo.insert(BBInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  // Next, update WaterList.  Specifically, we need to add NewMBB as having
  // available water after it.
  water_iterator IP =
    std::lower_bound(WaterList.begin(), WaterList.end(), NewBB,
                     CompareMBBNumbers);
  WaterList.insert(IP, NewBB);
}

/// Split the basic block containing MI into two blocks, which are joined by
/// an unconditional branch.  Update data structures and renumber blocks to
/// account for this change and returns the newly created block.
MachineBasicBlock *ARMConstantIslands::splitBlockBeforeInstr(MachineInstr *MI) {
  MachineBasicBlock *OrigBB = MI->getParent();

  // Create a new MBB for the code after the OrigBB.
  MachineBasicBlock *NewBB =
    MF->CreateMachineBasicBlock(OrigBB->getBasicBlock());
  MachineFunction::iterator MBBI = ++OrigBB->getIterator();
  MF->insert(MBBI, NewBB);

  // Splice the instructions starting with MI over to NewBB.
  NewBB->splice(NewBB->end(), OrigBB, MI, OrigBB->end());

  // Add an unconditional branch from OrigBB to NewBB.
  // Note the new unconditional branch is not being recorded.
  // There doesn't seem to be meaningful DebugInfo available; this doesn't
  // correspond to anything in the source.
  unsigned Opc = isThumb ? (isThumb2 ? ARM::t2B : ARM::tB) : ARM::B;
  if (!isThumb)
    BuildMI(OrigBB, DebugLoc(), TII->get(Opc)).addMBB(NewBB);
  else
    BuildMI(OrigBB, DebugLoc(), TII->get(Opc))
        .addMBB(NewBB)
        .add(predOps(ARMCC::AL));
  ++NumSplit;

  // Update the CFG.  All succs of OrigBB are now succs of NewBB.
  NewBB->transferSuccessors(OrigBB);

  // OrigBB branches to NewBB.
  OrigBB->addSuccessor(NewBB);

  // Update internal data structures to account for the newly inserted MBB.
  // This is almost the same as updateForInsertedWaterBlock, except that
  // the Water goes after OrigBB, not NewBB.
  MF->RenumberBlocks(NewBB);

  // Insert an entry into BBInfo to align it properly with the (newly
  // renumbered) block numbers.
  BBInfo.insert(BBInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  // Next, update WaterList.  Specifically, we need to add OrigMBB as having
  // available water after it (but not if it's already there, which happens
  // when splitting before a conditional branch that is followed by an
  // unconditional branch - in that case we want to insert NewBB).
  water_iterator IP =
    std::lower_bound(WaterList.begin(), WaterList.end(), OrigBB,
                     CompareMBBNumbers);
  MachineBasicBlock* WaterBB = *IP;
  if (WaterBB == OrigBB)
    WaterList.insert(std::next(IP), NewBB);
  else
    WaterList.insert(IP, OrigBB);
  NewWaterList.insert(OrigBB);

  // Figure out how large the OrigBB is.  As the first half of the original
  // block, it cannot contain a tablejump.  The size includes
  // the new jump we added.  (It should be possible to do this without
  // recounting everything, but it's very confusing, and this is rarely
  // executed.)
  computeBlockSize(MF, OrigBB, BBInfo[OrigBB->getNumber()]);

  // Figure out how large the NewMBB is.  As the second half of the original
  // block, it may contain a tablejump.
  computeBlockSize(MF, NewBB, BBInfo[NewBB->getNumber()]);

  // All BBOffsets following these blocks must be modified.
  adjustBBOffsetsAfter(OrigBB);

  return NewBB;
}

/// getUserOffset - Compute the offset of U.MI as seen by the hardware
/// displacement computation.  Update U.KnownAlignment to match its current
/// basic block location.
unsigned ARMConstantIslands::getUserOffset(CPUser &U) const {
  unsigned UserOffset = getOffsetOf(U.MI);
  const BasicBlockInfo &BBI = BBInfo[U.MI->getParent()->getNumber()];
  unsigned KnownBits = BBI.internalKnownBits();

  // The value read from PC is offset from the actual instruction address.
  UserOffset += (isThumb ? 4 : 8);

  // Because of inline assembly, we may not know the alignment (mod 4) of U.MI.
  // Make sure U.getMaxDisp() returns a constrained range.
  U.KnownAlignment = (KnownBits >= 2);

  // On Thumb, offsets==2 mod 4 are rounded down by the hardware for
  // purposes of the displacement computation; compensate for that here.
  // For unknown alignments, getMaxDisp() constrains the range instead.
  if (isThumb && U.KnownAlignment)
    UserOffset &= ~3u;

  return UserOffset;
}

/// isOffsetInRange - Checks whether UserOffset (the location of a constant pool
/// reference) is within MaxDisp of TrialOffset (a proposed location of a
/// constant pool entry).
/// UserOffset is computed by getUserOffset above to include PC adjustments. If
/// the mod 4 alignment of UserOffset is not known, the uncertainty must be
/// subtracted from MaxDisp instead. CPUser::getMaxDisp() does that.
bool ARMConstantIslands::isOffsetInRange(unsigned UserOffset,
                                         unsigned TrialOffset, unsigned MaxDisp,
                                         bool NegativeOK, bool IsSoImm) {
  if (UserOffset <= TrialOffset) {
    // User before the Trial.
    if (TrialOffset - UserOffset <= MaxDisp)
      return true;
    // FIXME: Make use full range of soimm values.
  } else if (NegativeOK) {
    if (UserOffset - TrialOffset <= MaxDisp)
      return true;
    // FIXME: Make use full range of soimm values.
  }
  return false;
}

/// isWaterInRange - Returns true if a CPE placed after the specified
/// Water (a basic block) will be in range for the specific MI.
///
/// Compute how much the function will grow by inserting a CPE after Water.
bool ARMConstantIslands::isWaterInRange(unsigned UserOffset,
                                        MachineBasicBlock* Water, CPUser &U,
                                        unsigned &Growth) {
  unsigned CPELogAlign = getCPELogAlign(U.CPEMI);
  unsigned CPEOffset = BBInfo[Water->getNumber()].postOffset(CPELogAlign);
  unsigned NextBlockOffset, NextBlockAlignment;
  MachineFunction::const_iterator NextBlock = Water->getIterator();
  if (++NextBlock == MF->end()) {
    NextBlockOffset = BBInfo[Water->getNumber()].postOffset();
    NextBlockAlignment = 0;
  } else {
    NextBlockOffset = BBInfo[NextBlock->getNumber()].Offset;
    NextBlockAlignment = NextBlock->getAlignment();
  }
  unsigned Size = U.CPEMI->getOperand(2).getImm();
  unsigned CPEEnd = CPEOffset + Size;

  // The CPE may be able to hide in the alignment padding before the next
  // block. It may also cause more padding to be required if it is more aligned
  // that the next block.
  if (CPEEnd > NextBlockOffset) {
    Growth = CPEEnd - NextBlockOffset;
    // Compute the padding that would go at the end of the CPE to align the next
    // block.
    Growth += OffsetToAlignment(CPEEnd, 1ULL << NextBlockAlignment);

    // If the CPE is to be inserted before the instruction, that will raise
    // the offset of the instruction. Also account for unknown alignment padding
    // in blocks between CPE and the user.
    if (CPEOffset < UserOffset)
      UserOffset += Growth + UnknownPadding(MF->getAlignment(), CPELogAlign);
  } else
    // CPE fits in existing padding.
    Growth = 0;

  return isOffsetInRange(UserOffset, CPEOffset, U);
}

/// isCPEntryInRange - Returns true if the distance between specific MI and
/// specific ConstPool entry instruction can fit in MI's displacement field.
bool ARMConstantIslands::isCPEntryInRange(MachineInstr *MI, unsigned UserOffset,
                                      MachineInstr *CPEMI, unsigned MaxDisp,
                                      bool NegOk, bool DoDump) {
  unsigned CPEOffset  = getOffsetOf(CPEMI);

  if (DoDump) {
    LLVM_DEBUG({
      unsigned Block = MI->getParent()->getNumber();
      const BasicBlockInfo &BBI = BBInfo[Block];
      dbgs() << "User of CPE#" << CPEMI->getOperand(0).getImm()
             << " max delta=" << MaxDisp
             << format(" insn address=%#x", UserOffset) << " in "
             << printMBBReference(*MI->getParent()) << ": "
             << format("%#x-%x\t", BBI.Offset, BBI.postOffset()) << *MI
             << format("CPE address=%#x offset=%+d: ", CPEOffset,
                       int(CPEOffset - UserOffset));
    });
  }

  return isOffsetInRange(UserOffset, CPEOffset, MaxDisp, NegOk);
}

#ifndef NDEBUG
/// BBIsJumpedOver - Return true of the specified basic block's only predecessor
/// unconditionally branches to its only successor.
static bool BBIsJumpedOver(MachineBasicBlock *MBB) {
  if (MBB->pred_size() != 1 || MBB->succ_size() != 1)
    return false;

  MachineBasicBlock *Succ = *MBB->succ_begin();
  MachineBasicBlock *Pred = *MBB->pred_begin();
  MachineInstr *PredMI = &Pred->back();
  if (PredMI->getOpcode() == ARM::B || PredMI->getOpcode() == ARM::tB
      || PredMI->getOpcode() == ARM::t2B)
    return PredMI->getOperand(0).getMBB() == Succ;
  return false;
}
#endif // NDEBUG

void ARMConstantIslands::adjustBBOffsetsAfter(MachineBasicBlock *BB) {
  unsigned BBNum = BB->getNumber();
  for(unsigned i = BBNum + 1, e = MF->getNumBlockIDs(); i < e; ++i) {
    // Get the offset and known bits at the end of the layout predecessor.
    // Include the alignment of the current block.
    unsigned LogAlign = MF->getBlockNumbered(i)->getAlignment();
    unsigned Offset = BBInfo[i - 1].postOffset(LogAlign);
    unsigned KnownBits = BBInfo[i - 1].postKnownBits(LogAlign);

    // This is where block i begins.  Stop if the offset is already correct,
    // and we have updated 2 blocks.  This is the maximum number of blocks
    // changed before calling this function.
    if (i > BBNum + 2 &&
        BBInfo[i].Offset == Offset &&
        BBInfo[i].KnownBits == KnownBits)
      break;

    BBInfo[i].Offset = Offset;
    BBInfo[i].KnownBits = KnownBits;
  }
}

/// decrementCPEReferenceCount - find the constant pool entry with index CPI
/// and instruction CPEMI, and decrement its refcount.  If the refcount
/// becomes 0 remove the entry and instruction.  Returns true if we removed
/// the entry, false if we didn't.
bool ARMConstantIslands::decrementCPEReferenceCount(unsigned CPI,
                                                    MachineInstr *CPEMI) {
  // Find the old entry. Eliminate it if it is no longer used.
  CPEntry *CPE = findConstPoolEntry(CPI, CPEMI);
  assert(CPE && "Unexpected!");
  if (--CPE->RefCount == 0) {
    removeDeadCPEMI(CPEMI);
    CPE->CPEMI = nullptr;
    --NumCPEs;
    return true;
  }
  return false;
}

unsigned ARMConstantIslands::getCombinedIndex(const MachineInstr *CPEMI) {
  if (CPEMI->getOperand(1).isCPI())
    return CPEMI->getOperand(1).getIndex();

  return JumpTableEntryIndices[CPEMI->getOperand(1).getIndex()];
}

/// LookForCPEntryInRange - see if the currently referenced CPE is in range;
/// if not, see if an in-range clone of the CPE is in range, and if so,
/// change the data structures so the user references the clone.  Returns:
/// 0 = no existing entry found
/// 1 = entry found, and there were no code insertions or deletions
/// 2 = entry found, and there were code insertions or deletions
int ARMConstantIslands::findInRangeCPEntry(CPUser& U, unsigned UserOffset) {
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI  = U.CPEMI;

  // Check to see if the CPE is already in-range.
  if (isCPEntryInRange(UserMI, UserOffset, CPEMI, U.getMaxDisp(), U.NegOk,
                       true)) {
    LLVM_DEBUG(dbgs() << "In range\n");
    return 1;
  }

  // No.  Look for previously created clones of the CPE that are in range.
  unsigned CPI = getCombinedIndex(CPEMI);
  std::vector<CPEntry> &CPEs = CPEntries[CPI];
  for (unsigned i = 0, e = CPEs.size(); i != e; ++i) {
    // We already tried this one
    if (CPEs[i].CPEMI == CPEMI)
      continue;
    // Removing CPEs can leave empty entries, skip
    if (CPEs[i].CPEMI == nullptr)
      continue;
    if (isCPEntryInRange(UserMI, UserOffset, CPEs[i].CPEMI, U.getMaxDisp(),
                     U.NegOk)) {
      LLVM_DEBUG(dbgs() << "Replacing CPE#" << CPI << " with CPE#"
                        << CPEs[i].CPI << "\n");
      // Point the CPUser node to the replacement
      U.CPEMI = CPEs[i].CPEMI;
      // Change the CPI in the instruction operand to refer to the clone.
      for (unsigned j = 0, e = UserMI->getNumOperands(); j != e; ++j)
        if (UserMI->getOperand(j).isCPI()) {
          UserMI->getOperand(j).setIndex(CPEs[i].CPI);
          break;
        }
      // Adjust the refcount of the clone...
      CPEs[i].RefCount++;
      // ...and the original.  If we didn't remove the old entry, none of the
      // addresses changed, so we don't need another pass.
      return decrementCPEReferenceCount(CPI, CPEMI) ? 2 : 1;
    }
  }
  return 0;
}

/// getUnconditionalBrDisp - Returns the maximum displacement that can fit in
/// the specific unconditional branch instruction.
static inline unsigned getUnconditionalBrDisp(int Opc) {
  switch (Opc) {
  case ARM::tB:
    return ((1<<10)-1)*2;
  case ARM::t2B:
    return ((1<<23)-1)*2;
  default:
    break;
  }

  return ((1<<23)-1)*4;
}

/// findAvailableWater - Look for an existing entry in the WaterList in which
/// we can place the CPE referenced from U so it's within range of U's MI.
/// Returns true if found, false if not.  If it returns true, WaterIter
/// is set to the WaterList entry.  For Thumb, prefer water that will not
/// introduce padding to water that will.  To ensure that this pass
/// terminates, the CPE location for a particular CPUser is only allowed to
/// move to a lower address, so search backward from the end of the list and
/// prefer the first water that is in range.
bool ARMConstantIslands::findAvailableWater(CPUser &U, unsigned UserOffset,
                                            water_iterator &WaterIter,
                                            bool CloserWater) {
  if (WaterList.empty())
    return false;

  unsigned BestGrowth = ~0u;
  // The nearest water without splitting the UserBB is right after it.
  // If the distance is still large (we have a big BB), then we need to split it
  // if we don't converge after certain iterations. This helps the following
  // situation to converge:
  //   BB0:
  //      Big BB
  //   BB1:
  //      Constant Pool
  // When a CP access is out of range, BB0 may be used as water. However,
  // inserting islands between BB0 and BB1 makes other accesses out of range.
  MachineBasicBlock *UserBB = U.MI->getParent();
  unsigned MinNoSplitDisp =
      BBInfo[UserBB->getNumber()].postOffset(getCPELogAlign(U.CPEMI));
  if (CloserWater && MinNoSplitDisp > U.getMaxDisp() / 2)
    return false;
  for (water_iterator IP = std::prev(WaterList.end()), B = WaterList.begin();;
       --IP) {
    MachineBasicBlock* WaterBB = *IP;
    // Check if water is in range and is either at a lower address than the
    // current "high water mark" or a new water block that was created since
    // the previous iteration by inserting an unconditional branch.  In the
    // latter case, we want to allow resetting the high water mark back to
    // this new water since we haven't seen it before.  Inserting branches
    // should be relatively uncommon and when it does happen, we want to be
    // sure to take advantage of it for all the CPEs near that block, so that
    // we don't insert more branches than necessary.
    // When CloserWater is true, we try to find the lowest address after (or
    // equal to) user MI's BB no matter of padding growth.
    unsigned Growth;
    if (isWaterInRange(UserOffset, WaterBB, U, Growth) &&
        (WaterBB->getNumber() < U.HighWaterMark->getNumber() ||
         NewWaterList.count(WaterBB) || WaterBB == U.MI->getParent()) &&
        Growth < BestGrowth) {
      // This is the least amount of required padding seen so far.
      BestGrowth = Growth;
      WaterIter = IP;
      LLVM_DEBUG(dbgs() << "Found water after " << printMBBReference(*WaterBB)
                        << " Growth=" << Growth << '\n');

      if (CloserWater && WaterBB == U.MI->getParent())
        return true;
      // Keep looking unless it is perfect and we're not looking for the lowest
      // possible address.
      if (!CloserWater && BestGrowth == 0)
        return true;
    }
    if (IP == B)
      break;
  }
  return BestGrowth != ~0u;
}

/// createNewWater - No existing WaterList entry will work for
/// CPUsers[CPUserIndex], so create a place to put the CPE.  The end of the
/// block is used if in range, and the conditional branch munged so control
/// flow is correct.  Otherwise the block is split to create a hole with an
/// unconditional branch around it.  In either case NewMBB is set to a
/// block following which the new island can be inserted (the WaterList
/// is not adjusted).
void ARMConstantIslands::createNewWater(unsigned CPUserIndex,
                                        unsigned UserOffset,
                                        MachineBasicBlock *&NewMBB) {
  CPUser &U = CPUsers[CPUserIndex];
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI  = U.CPEMI;
  unsigned CPELogAlign = getCPELogAlign(CPEMI);
  MachineBasicBlock *UserMBB = UserMI->getParent();
  const BasicBlockInfo &UserBBI = BBInfo[UserMBB->getNumber()];

  // If the block does not end in an unconditional branch already, and if the
  // end of the block is within range, make new water there.  (The addition
  // below is for the unconditional branch we will be adding: 4 bytes on ARM +
  // Thumb2, 2 on Thumb1.
  if (BBHasFallthrough(UserMBB)) {
    // Size of branch to insert.
    unsigned Delta = isThumb1 ? 2 : 4;
    // Compute the offset where the CPE will begin.
    unsigned CPEOffset = UserBBI.postOffset(CPELogAlign) + Delta;

    if (isOffsetInRange(UserOffset, CPEOffset, U)) {
      LLVM_DEBUG(dbgs() << "Split at end of " << printMBBReference(*UserMBB)
                        << format(", expected CPE offset %#x\n", CPEOffset));
      NewMBB = &*++UserMBB->getIterator();
      // Add an unconditional branch from UserMBB to fallthrough block.  Record
      // it for branch lengthening; this new branch will not get out of range,
      // but if the preceding conditional branch is out of range, the targets
      // will be exchanged, and the altered branch may be out of range, so the
      // machinery has to know about it.
      int UncondBr = isThumb ? ((isThumb2) ? ARM::t2B : ARM::tB) : ARM::B;
      if (!isThumb)
        BuildMI(UserMBB, DebugLoc(), TII->get(UncondBr)).addMBB(NewMBB);
      else
        BuildMI(UserMBB, DebugLoc(), TII->get(UncondBr))
            .addMBB(NewMBB)
            .add(predOps(ARMCC::AL));
      unsigned MaxDisp = getUnconditionalBrDisp(UncondBr);
      ImmBranches.push_back(ImmBranch(&UserMBB->back(),
                                      MaxDisp, false, UncondBr));
      computeBlockSize(MF, UserMBB, BBInfo[UserMBB->getNumber()]);
      adjustBBOffsetsAfter(UserMBB);
      return;
    }
  }

  // What a big block.  Find a place within the block to split it.  This is a
  // little tricky on Thumb1 since instructions are 2 bytes and constant pool
  // entries are 4 bytes: if instruction I references island CPE, and
  // instruction I+1 references CPE', it will not work well to put CPE as far
  // forward as possible, since then CPE' cannot immediately follow it (that
  // location is 2 bytes farther away from I+1 than CPE was from I) and we'd
  // need to create a new island.  So, we make a first guess, then walk through
  // the instructions between the one currently being looked at and the
  // possible insertion point, and make sure any other instructions that
  // reference CPEs will be able to use the same island area; if not, we back
  // up the insertion point.

  // Try to split the block so it's fully aligned.  Compute the latest split
  // point where we can add a 4-byte branch instruction, and then align to
  // LogAlign which is the largest possible alignment in the function.
  unsigned LogAlign = MF->getAlignment();
  assert(LogAlign >= CPELogAlign && "Over-aligned constant pool entry");
  unsigned KnownBits = UserBBI.internalKnownBits();
  unsigned UPad = UnknownPadding(LogAlign, KnownBits);
  unsigned BaseInsertOffset = UserOffset + U.getMaxDisp() - UPad;
  LLVM_DEBUG(dbgs() << format("Split in middle of big block before %#x",
                              BaseInsertOffset));

  // The 4 in the following is for the unconditional branch we'll be inserting
  // (allows for long branch on Thumb1).  Alignment of the island is handled
  // inside isOffsetInRange.
  BaseInsertOffset -= 4;

  LLVM_DEBUG(dbgs() << format(", adjusted to %#x", BaseInsertOffset)
                    << " la=" << LogAlign << " kb=" << KnownBits
                    << " up=" << UPad << '\n');

  // This could point off the end of the block if we've already got constant
  // pool entries following this block; only the last one is in the water list.
  // Back past any possible branches (allow for a conditional and a maximally
  // long unconditional).
  if (BaseInsertOffset + 8 >= UserBBI.postOffset()) {
    // Ensure BaseInsertOffset is larger than the offset of the instruction
    // following UserMI so that the loop which searches for the split point
    // iterates at least once.
    BaseInsertOffset =
        std::max(UserBBI.postOffset() - UPad - 8,
                 UserOffset + TII->getInstSizeInBytes(*UserMI) + 1);
    LLVM_DEBUG(dbgs() << format("Move inside block: %#x\n", BaseInsertOffset));
  }
  unsigned EndInsertOffset = BaseInsertOffset + 4 + UPad +
    CPEMI->getOperand(2).getImm();
  MachineBasicBlock::iterator MI = UserMI;
  ++MI;
  unsigned CPUIndex = CPUserIndex+1;
  unsigned NumCPUsers = CPUsers.size();
  MachineInstr *LastIT = nullptr;
  for (unsigned Offset = UserOffset + TII->getInstSizeInBytes(*UserMI);
       Offset < BaseInsertOffset;
       Offset += TII->getInstSizeInBytes(*MI), MI = std::next(MI)) {
    assert(MI != UserMBB->end() && "Fell off end of block");
    if (CPUIndex < NumCPUsers && CPUsers[CPUIndex].MI == &*MI) {
      CPUser &U = CPUsers[CPUIndex];
      if (!isOffsetInRange(Offset, EndInsertOffset, U)) {
        // Shift intertion point by one unit of alignment so it is within reach.
        BaseInsertOffset -= 1u << LogAlign;
        EndInsertOffset  -= 1u << LogAlign;
      }
      // This is overly conservative, as we don't account for CPEMIs being
      // reused within the block, but it doesn't matter much.  Also assume CPEs
      // are added in order with alignment padding.  We may eventually be able
      // to pack the aligned CPEs better.
      EndInsertOffset += U.CPEMI->getOperand(2).getImm();
      CPUIndex++;
    }

    // Remember the last IT instruction.
    if (MI->getOpcode() == ARM::t2IT)
      LastIT = &*MI;
  }

  --MI;

  // Avoid splitting an IT block.
  if (LastIT) {
    unsigned PredReg = 0;
    ARMCC::CondCodes CC = getITInstrPredicate(*MI, PredReg);
    if (CC != ARMCC::AL)
      MI = LastIT;
  }

  // Avoid splitting a MOVW+MOVT pair with a relocation on Windows.
  // On Windows, this instruction pair is covered by one single
  // IMAGE_REL_ARM_MOV32T relocation which covers both instructions. If a
  // constant island is injected inbetween them, the relocation will clobber
  // the instruction and fail to update the MOVT instruction.
  // (These instructions are bundled up until right before the ConstantIslands
  // pass.)
  if (STI->isTargetWindows() && isThumb && MI->getOpcode() == ARM::t2MOVTi16 &&
      (MI->getOperand(2).getTargetFlags() & ARMII::MO_OPTION_MASK) ==
          ARMII::MO_HI16) {
    --MI;
    assert(MI->getOpcode() == ARM::t2MOVi16 &&
           (MI->getOperand(1).getTargetFlags() & ARMII::MO_OPTION_MASK) ==
               ARMII::MO_LO16);
  }

  // We really must not split an IT block.
  LLVM_DEBUG(unsigned PredReg; assert(
                 !isThumb || getITInstrPredicate(*MI, PredReg) == ARMCC::AL));

  NewMBB = splitBlockBeforeInstr(&*MI);
}

/// handleConstantPoolUser - Analyze the specified user, checking to see if it
/// is out-of-range.  If so, pick up the constant pool value and move it some
/// place in-range.  Return true if we changed any addresses (thus must run
/// another pass of branch lengthening), false otherwise.
bool ARMConstantIslands::handleConstantPoolUser(unsigned CPUserIndex,
                                                bool CloserWater) {
  CPUser &U = CPUsers[CPUserIndex];
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI  = U.CPEMI;
  unsigned CPI = getCombinedIndex(CPEMI);
  unsigned Size = CPEMI->getOperand(2).getImm();
  // Compute this only once, it's expensive.
  unsigned UserOffset = getUserOffset(U);

  // See if the current entry is within range, or there is a clone of it
  // in range.
  int result = findInRangeCPEntry(U, UserOffset);
  if (result==1) return false;
  else if (result==2) return true;

  // No existing clone of this CPE is within range.
  // We will be generating a new clone.  Get a UID for it.
  unsigned ID = AFI->createPICLabelUId();

  // Look for water where we can place this CPE.
  MachineBasicBlock *NewIsland = MF->CreateMachineBasicBlock();
  MachineBasicBlock *NewMBB;
  water_iterator IP;
  if (findAvailableWater(U, UserOffset, IP, CloserWater)) {
    LLVM_DEBUG(dbgs() << "Found water in range\n");
    MachineBasicBlock *WaterBB = *IP;

    // If the original WaterList entry was "new water" on this iteration,
    // propagate that to the new island.  This is just keeping NewWaterList
    // updated to match the WaterList, which will be updated below.
    if (NewWaterList.erase(WaterBB))
      NewWaterList.insert(NewIsland);

    // The new CPE goes before the following block (NewMBB).
    NewMBB = &*++WaterBB->getIterator();
  } else {
    // No water found.
    LLVM_DEBUG(dbgs() << "No water found\n");
    createNewWater(CPUserIndex, UserOffset, NewMBB);

    // splitBlockBeforeInstr adds to WaterList, which is important when it is
    // called while handling branches so that the water will be seen on the
    // next iteration for constant pools, but in this context, we don't want
    // it.  Check for this so it will be removed from the WaterList.
    // Also remove any entry from NewWaterList.
    MachineBasicBlock *WaterBB = &*--NewMBB->getIterator();
    IP = find(WaterList, WaterBB);
    if (IP != WaterList.end())
      NewWaterList.erase(WaterBB);

    // We are adding new water.  Update NewWaterList.
    NewWaterList.insert(NewIsland);
  }
  // Always align the new block because CP entries can be smaller than 4
  // bytes. Be careful not to decrease the existing alignment, e.g. NewMBB may
  // be an already aligned constant pool block.
  const unsigned Align = isThumb ? 1 : 2;
  if (NewMBB->getAlignment() < Align)
    NewMBB->setAlignment(Align);

  // Remove the original WaterList entry; we want subsequent insertions in
  // this vicinity to go after the one we're about to insert.  This
  // considerably reduces the number of times we have to move the same CPE
  // more than once and is also important to ensure the algorithm terminates.
  if (IP != WaterList.end())
    WaterList.erase(IP);

  // Okay, we know we can put an island before NewMBB now, do it!
  MF->insert(NewMBB->getIterator(), NewIsland);

  // Update internal data structures to account for the newly inserted MBB.
  updateForInsertedWaterBlock(NewIsland);

  // Now that we have an island to add the CPE to, clone the original CPE and
  // add it to the island.
  U.HighWaterMark = NewIsland;
  U.CPEMI = BuildMI(NewIsland, DebugLoc(), CPEMI->getDesc())
                .addImm(ID)
                .add(CPEMI->getOperand(1))
                .addImm(Size);
  CPEntries[CPI].push_back(CPEntry(U.CPEMI, ID, 1));
  ++NumCPEs;

  // Decrement the old entry, and remove it if refcount becomes 0.
  decrementCPEReferenceCount(CPI, CPEMI);

  // Mark the basic block as aligned as required by the const-pool entry.
  NewIsland->setAlignment(getCPELogAlign(U.CPEMI));

  // Increase the size of the island block to account for the new entry.
  BBInfo[NewIsland->getNumber()].Size += Size;
  adjustBBOffsetsAfter(&*--NewIsland->getIterator());

  // Finally, change the CPI in the instruction operand to be ID.
  for (unsigned i = 0, e = UserMI->getNumOperands(); i != e; ++i)
    if (UserMI->getOperand(i).isCPI()) {
      UserMI->getOperand(i).setIndex(ID);
      break;
    }

  LLVM_DEBUG(
      dbgs() << "  Moved CPE to #" << ID << " CPI=" << CPI
             << format(" offset=%#x\n", BBInfo[NewIsland->getNumber()].Offset));

  return true;
}

/// removeDeadCPEMI - Remove a dead constant pool entry instruction. Update
/// sizes and offsets of impacted basic blocks.
void ARMConstantIslands::removeDeadCPEMI(MachineInstr *CPEMI) {
  MachineBasicBlock *CPEBB = CPEMI->getParent();
  unsigned Size = CPEMI->getOperand(2).getImm();
  CPEMI->eraseFromParent();
  BBInfo[CPEBB->getNumber()].Size -= Size;
  // All succeeding offsets have the current size value added in, fix this.
  if (CPEBB->empty()) {
    BBInfo[CPEBB->getNumber()].Size = 0;

    // This block no longer needs to be aligned.
    CPEBB->setAlignment(0);
  } else
    // Entries are sorted by descending alignment, so realign from the front.
    CPEBB->setAlignment(getCPELogAlign(&*CPEBB->begin()));

  adjustBBOffsetsAfter(CPEBB);
  // An island has only one predecessor BB and one successor BB. Check if
  // this BB's predecessor jumps directly to this BB's successor. This
  // shouldn't happen currently.
  assert(!BBIsJumpedOver(CPEBB) && "How did this happen?");
  // FIXME: remove the empty blocks after all the work is done?
}

/// removeUnusedCPEntries - Remove constant pool entries whose refcounts
/// are zero.
bool ARMConstantIslands::removeUnusedCPEntries() {
  unsigned MadeChange = false;
  for (unsigned i = 0, e = CPEntries.size(); i != e; ++i) {
      std::vector<CPEntry> &CPEs = CPEntries[i];
      for (unsigned j = 0, ee = CPEs.size(); j != ee; ++j) {
        if (CPEs[j].RefCount == 0 && CPEs[j].CPEMI) {
          removeDeadCPEMI(CPEs[j].CPEMI);
          CPEs[j].CPEMI = nullptr;
          MadeChange = true;
        }
      }
  }
  return MadeChange;
}

/// isBBInRange - Returns true if the distance between specific MI and
/// specific BB can fit in MI's displacement field.
bool ARMConstantIslands::isBBInRange(MachineInstr *MI,MachineBasicBlock *DestBB,
                                     unsigned MaxDisp) {
  unsigned PCAdj      = isThumb ? 4 : 8;
  unsigned BrOffset   = getOffsetOf(MI) + PCAdj;
  unsigned DestOffset = BBInfo[DestBB->getNumber()].Offset;

  LLVM_DEBUG(dbgs() << "Branch of destination " << printMBBReference(*DestBB)
                    << " from " << printMBBReference(*MI->getParent())
                    << " max delta=" << MaxDisp << " from " << getOffsetOf(MI)
                    << " to " << DestOffset << " offset "
                    << int(DestOffset - BrOffset) << "\t" << *MI);

  if (BrOffset <= DestOffset) {
    // Branch before the Dest.
    if (DestOffset-BrOffset <= MaxDisp)
      return true;
  } else {
    if (BrOffset-DestOffset <= MaxDisp)
      return true;
  }
  return false;
}

/// fixupImmediateBr - Fix up an immediate branch whose destination is too far
/// away to fit in its displacement field.
bool ARMConstantIslands::fixupImmediateBr(ImmBranch &Br) {
  MachineInstr *MI = Br.MI;
  MachineBasicBlock *DestBB = MI->getOperand(0).getMBB();

  // Check to see if the DestBB is already in-range.
  if (isBBInRange(MI, DestBB, Br.MaxDisp))
    return false;

  if (!Br.isCond)
    return fixupUnconditionalBr(Br);
  return fixupConditionalBr(Br);
}

/// fixupUnconditionalBr - Fix up an unconditional branch whose destination is
/// too far away to fit in its displacement field. If the LR register has been
/// spilled in the epilogue, then we can use BL to implement a far jump.
/// Otherwise, add an intermediate branch instruction to a branch.
bool
ARMConstantIslands::fixupUnconditionalBr(ImmBranch &Br) {
  MachineInstr *MI = Br.MI;
  MachineBasicBlock *MBB = MI->getParent();
  if (!isThumb1)
    llvm_unreachable("fixupUnconditionalBr is Thumb1 only!");

  // Use BL to implement far jump.
  Br.MaxDisp = (1 << 21) * 2;
  MI->setDesc(TII->get(ARM::tBfar));
  BBInfo[MBB->getNumber()].Size += 2;
  adjustBBOffsetsAfter(MBB);
  HasFarJump = true;
  ++NumUBrFixed;

  LLVM_DEBUG(dbgs() << "  Changed B to long jump " << *MI);

  return true;
}

/// fixupConditionalBr - Fix up a conditional branch whose destination is too
/// far away to fit in its displacement field. It is converted to an inverse
/// conditional branch + an unconditional branch to the destination.
bool
ARMConstantIslands::fixupConditionalBr(ImmBranch &Br) {
  MachineInstr *MI = Br.MI;
  MachineBasicBlock *DestBB = MI->getOperand(0).getMBB();

  // Add an unconditional branch to the destination and invert the branch
  // condition to jump over it:
  // blt L1
  // =>
  // bge L2
  // b   L1
  // L2:
  ARMCC::CondCodes CC = (ARMCC::CondCodes)MI->getOperand(1).getImm();
  CC = ARMCC::getOppositeCondition(CC);
  unsigned CCReg = MI->getOperand(2).getReg();

  // If the branch is at the end of its MBB and that has a fall-through block,
  // direct the updated conditional branch to the fall-through block. Otherwise,
  // split the MBB before the next instruction.
  MachineBasicBlock *MBB = MI->getParent();
  MachineInstr *BMI = &MBB->back();
  bool NeedSplit = (BMI != MI) || !BBHasFallthrough(MBB);

  ++NumCBrFixed;
  if (BMI != MI) {
    if (std::next(MachineBasicBlock::iterator(MI)) == std::prev(MBB->end()) &&
        BMI->getOpcode() == Br.UncondBr) {
      // Last MI in the BB is an unconditional branch. Can we simply invert the
      // condition and swap destinations:
      // beq L1
      // b   L2
      // =>
      // bne L2
      // b   L1
      MachineBasicBlock *NewDest = BMI->getOperand(0).getMBB();
      if (isBBInRange(MI, NewDest, Br.MaxDisp)) {
        LLVM_DEBUG(
            dbgs() << "  Invert Bcc condition and swap its destination with "
                   << *BMI);
        BMI->getOperand(0).setMBB(DestBB);
        MI->getOperand(0).setMBB(NewDest);
        MI->getOperand(1).setImm(CC);
        return true;
      }
    }
  }

  if (NeedSplit) {
    splitBlockBeforeInstr(MI);
    // No need for the branch to the next block. We're adding an unconditional
    // branch to the destination.
    int delta = TII->getInstSizeInBytes(MBB->back());
    BBInfo[MBB->getNumber()].Size -= delta;
    MBB->back().eraseFromParent();

    // The conditional successor will be swapped between the BBs after this, so
    // update CFG.
    MBB->addSuccessor(DestBB);
    std::next(MBB->getIterator())->removeSuccessor(DestBB);

    // BBInfo[SplitBB].Offset is wrong temporarily, fixed below
  }
  MachineBasicBlock *NextBB = &*++MBB->getIterator();

  LLVM_DEBUG(dbgs() << "  Insert B to " << printMBBReference(*DestBB)
                    << " also invert condition and change dest. to "
                    << printMBBReference(*NextBB) << "\n");

  // Insert a new conditional branch and a new unconditional branch.
  // Also update the ImmBranch as well as adding a new entry for the new branch.
  BuildMI(MBB, DebugLoc(), TII->get(MI->getOpcode()))
    .addMBB(NextBB).addImm(CC).addReg(CCReg);
  Br.MI = &MBB->back();
  BBInfo[MBB->getNumber()].Size += TII->getInstSizeInBytes(MBB->back());
  if (isThumb)
    BuildMI(MBB, DebugLoc(), TII->get(Br.UncondBr))
        .addMBB(DestBB)
        .add(predOps(ARMCC::AL));
  else
    BuildMI(MBB, DebugLoc(), TII->get(Br.UncondBr)).addMBB(DestBB);
  BBInfo[MBB->getNumber()].Size += TII->getInstSizeInBytes(MBB->back());
  unsigned MaxDisp = getUnconditionalBrDisp(Br.UncondBr);
  ImmBranches.push_back(ImmBranch(&MBB->back(), MaxDisp, false, Br.UncondBr));

  // Remove the old conditional branch.  It may or may not still be in MBB.
  BBInfo[MI->getParent()->getNumber()].Size -= TII->getInstSizeInBytes(*MI);
  MI->eraseFromParent();
  adjustBBOffsetsAfter(MBB);
  return true;
}

/// undoLRSpillRestore - Remove Thumb push / pop instructions that only spills
/// LR / restores LR to pc. FIXME: This is done here because it's only possible
/// to do this if tBfar is not used.
bool ARMConstantIslands::undoLRSpillRestore() {
  bool MadeChange = false;
  for (unsigned i = 0, e = PushPopMIs.size(); i != e; ++i) {
    MachineInstr *MI = PushPopMIs[i];
    // First two operands are predicates.
    if (MI->getOpcode() == ARM::tPOP_RET &&
        MI->getOperand(2).getReg() == ARM::PC &&
        MI->getNumExplicitOperands() == 3) {
      // Create the new insn and copy the predicate from the old.
      BuildMI(MI->getParent(), MI->getDebugLoc(), TII->get(ARM::tBX_RET))
          .add(MI->getOperand(0))
          .add(MI->getOperand(1));
      MI->eraseFromParent();
      MadeChange = true;
    } else if (MI->getOpcode() == ARM::tPUSH &&
               MI->getOperand(2).getReg() == ARM::LR &&
               MI->getNumExplicitOperands() == 3) {
      // Just remove the push.
      MI->eraseFromParent();
      MadeChange = true;
    }
  }
  return MadeChange;
}

bool ARMConstantIslands::optimizeThumb2Instructions() {
  bool MadeChange = false;

  // Shrink ADR and LDR from constantpool.
  for (unsigned i = 0, e = CPUsers.size(); i != e; ++i) {
    CPUser &U = CPUsers[i];
    unsigned Opcode = U.MI->getOpcode();
    unsigned NewOpc = 0;
    unsigned Scale = 1;
    unsigned Bits = 0;
    switch (Opcode) {
    default: break;
    case ARM::t2LEApcrel:
      if (isARMLowRegister(U.MI->getOperand(0).getReg())) {
        NewOpc = ARM::tLEApcrel;
        Bits = 8;
        Scale = 4;
      }
      break;
    case ARM::t2LDRpci:
      if (isARMLowRegister(U.MI->getOperand(0).getReg())) {
        NewOpc = ARM::tLDRpci;
        Bits = 8;
        Scale = 4;
      }
      break;
    }

    if (!NewOpc)
      continue;

    unsigned UserOffset = getUserOffset(U);
    unsigned MaxOffs = ((1 << Bits) - 1) * Scale;

    // Be conservative with inline asm.
    if (!U.KnownAlignment)
      MaxOffs -= 2;

    // FIXME: Check if offset is multiple of scale if scale is not 4.
    if (isCPEntryInRange(U.MI, UserOffset, U.CPEMI, MaxOffs, false, true)) {
      LLVM_DEBUG(dbgs() << "Shrink: " << *U.MI);
      U.MI->setDesc(TII->get(NewOpc));
      MachineBasicBlock *MBB = U.MI->getParent();
      BBInfo[MBB->getNumber()].Size -= 2;
      adjustBBOffsetsAfter(MBB);
      ++NumT2CPShrunk;
      MadeChange = true;
    }
  }

  return MadeChange;
}

bool ARMConstantIslands::optimizeThumb2Branches() {
  bool MadeChange = false;

  // The order in which branches appear in ImmBranches is approximately their
  // order within the function body. By visiting later branches first, we reduce
  // the distance between earlier forward branches and their targets, making it
  // more likely that the cbn?z optimization, which can only apply to forward
  // branches, will succeed.
  for (unsigned i = ImmBranches.size(); i != 0; --i) {
    ImmBranch &Br = ImmBranches[i-1];
    unsigned Opcode = Br.MI->getOpcode();
    unsigned NewOpc = 0;
    unsigned Scale = 1;
    unsigned Bits = 0;
    switch (Opcode) {
    default: break;
    case ARM::t2B:
      NewOpc = ARM::tB;
      Bits = 11;
      Scale = 2;
      break;
    case ARM::t2Bcc:
      NewOpc = ARM::tBcc;
      Bits = 8;
      Scale = 2;
      break;
    }
    if (NewOpc) {
      unsigned MaxOffs = ((1 << (Bits-1))-1) * Scale;
      MachineBasicBlock *DestBB = Br.MI->getOperand(0).getMBB();
      if (isBBInRange(Br.MI, DestBB, MaxOffs)) {
        LLVM_DEBUG(dbgs() << "Shrink branch: " << *Br.MI);
        Br.MI->setDesc(TII->get(NewOpc));
        MachineBasicBlock *MBB = Br.MI->getParent();
        BBInfo[MBB->getNumber()].Size -= 2;
        adjustBBOffsetsAfter(MBB);
        ++NumT2BrShrunk;
        MadeChange = true;
      }
    }

    Opcode = Br.MI->getOpcode();
    if (Opcode != ARM::tBcc)
      continue;

    // If the conditional branch doesn't kill CPSR, then CPSR can be liveout
    // so this transformation is not safe.
    if (!Br.MI->killsRegister(ARM::CPSR))
      continue;

    NewOpc = 0;
    unsigned PredReg = 0;
    ARMCC::CondCodes Pred = getInstrPredicate(*Br.MI, PredReg);
    if (Pred == ARMCC::EQ)
      NewOpc = ARM::tCBZ;
    else if (Pred == ARMCC::NE)
      NewOpc = ARM::tCBNZ;
    if (!NewOpc)
      continue;
    MachineBasicBlock *DestBB = Br.MI->getOperand(0).getMBB();
    // Check if the distance is within 126. Subtract starting offset by 2
    // because the cmp will be eliminated.
    unsigned BrOffset = getOffsetOf(Br.MI) + 4 - 2;
    unsigned DestOffset = BBInfo[DestBB->getNumber()].Offset;
    if (BrOffset < DestOffset && (DestOffset - BrOffset) <= 126) {
      MachineBasicBlock::iterator CmpMI = Br.MI;
      if (CmpMI != Br.MI->getParent()->begin()) {
        --CmpMI;
        if (CmpMI->getOpcode() == ARM::tCMPi8) {
          unsigned Reg = CmpMI->getOperand(0).getReg();
          Pred = getInstrPredicate(*CmpMI, PredReg);
          if (Pred == ARMCC::AL &&
              CmpMI->getOperand(1).getImm() == 0 &&
              isARMLowRegister(Reg)) {
            MachineBasicBlock *MBB = Br.MI->getParent();
            LLVM_DEBUG(dbgs() << "Fold: " << *CmpMI << " and: " << *Br.MI);
            MachineInstr *NewBR =
              BuildMI(*MBB, CmpMI, Br.MI->getDebugLoc(), TII->get(NewOpc))
              .addReg(Reg).addMBB(DestBB,Br.MI->getOperand(0).getTargetFlags());
            CmpMI->eraseFromParent();
            Br.MI->eraseFromParent();
            Br.MI = NewBR;
            BBInfo[MBB->getNumber()].Size -= 2;
            adjustBBOffsetsAfter(MBB);
            ++NumCBZ;
            MadeChange = true;
          }
        }
      }
    }
  }

  return MadeChange;
}

static bool isSimpleIndexCalc(MachineInstr &I, unsigned EntryReg,
                              unsigned BaseReg) {
  if (I.getOpcode() != ARM::t2ADDrs)
    return false;

  if (I.getOperand(0).getReg() != EntryReg)
    return false;

  if (I.getOperand(1).getReg() != BaseReg)
    return false;

  // FIXME: what about CC and IdxReg?
  return true;
}

/// While trying to form a TBB/TBH instruction, we may (if the table
/// doesn't immediately follow the BR_JT) need access to the start of the
/// jump-table. We know one instruction that produces such a register; this
/// function works out whether that definition can be preserved to the BR_JT,
/// possibly by removing an intervening addition (which is usually needed to
/// calculate the actual entry to jump to).
bool ARMConstantIslands::preserveBaseRegister(MachineInstr *JumpMI,
                                              MachineInstr *LEAMI,
                                              unsigned &DeadSize,
                                              bool &CanDeleteLEA,
                                              bool &BaseRegKill) {
  if (JumpMI->getParent() != LEAMI->getParent())
    return false;

  // Now we hope that we have at least these instructions in the basic block:
  //     BaseReg = t2LEA ...
  //     [...]
  //     EntryReg = t2ADDrs BaseReg, ...
  //     [...]
  //     t2BR_JT EntryReg
  //
  // We have to be very conservative about what we recognise here though. The
  // main perturbing factors to watch out for are:
  //    + Spills at any point in the chain: not direct problems but we would
  //      expect a blocking Def of the spilled register so in practice what we
  //      can do is limited.
  //    + EntryReg == BaseReg: this is the one situation we should allow a Def
  //      of BaseReg, but only if the t2ADDrs can be removed.
  //    + Some instruction other than t2ADDrs computing the entry. Not seen in
  //      the wild, but we should be careful.
  unsigned EntryReg = JumpMI->getOperand(0).getReg();
  unsigned BaseReg = LEAMI->getOperand(0).getReg();

  CanDeleteLEA = true;
  BaseRegKill = false;
  MachineInstr *RemovableAdd = nullptr;
  MachineBasicBlock::iterator I(LEAMI);
  for (++I; &*I != JumpMI; ++I) {
    if (isSimpleIndexCalc(*I, EntryReg, BaseReg)) {
      RemovableAdd = &*I;
      break;
    }

    for (unsigned K = 0, E = I->getNumOperands(); K != E; ++K) {
      const MachineOperand &MO = I->getOperand(K);
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (MO.isDef() && MO.getReg() == BaseReg)
        return false;
      if (MO.isUse() && MO.getReg() == BaseReg) {
        BaseRegKill = BaseRegKill || MO.isKill();
        CanDeleteLEA = false;
      }
    }
  }

  if (!RemovableAdd)
    return true;

  // Check the add really is removable, and that nothing else in the block
  // clobbers BaseReg.
  for (++I; &*I != JumpMI; ++I) {
    for (unsigned K = 0, E = I->getNumOperands(); K != E; ++K) {
      const MachineOperand &MO = I->getOperand(K);
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (MO.isDef() && MO.getReg() == BaseReg)
        return false;
      if (MO.isUse() && MO.getReg() == EntryReg)
        RemovableAdd = nullptr;
    }
  }

  if (RemovableAdd) {
    RemovableAdd->eraseFromParent();
    DeadSize += isThumb2 ? 4 : 2;
  } else if (BaseReg == EntryReg) {
    // The add wasn't removable, but clobbered the base for the TBB. So we can't
    // preserve it.
    return false;
  }

  // We reached the end of the block without seeing another definition of
  // BaseReg (except, possibly the t2ADDrs, which was removed). BaseReg can be
  // used in the TBB/TBH if necessary.
  return true;
}

/// Returns whether CPEMI is the first instruction in the block
/// immediately following JTMI (assumed to be a TBB or TBH terminator). If so,
/// we can switch the first register to PC and usually remove the address
/// calculation that preceded it.
static bool jumpTableFollowsTB(MachineInstr *JTMI, MachineInstr *CPEMI) {
  MachineFunction::iterator MBB = JTMI->getParent()->getIterator();
  MachineFunction *MF = MBB->getParent();
  ++MBB;

  return MBB != MF->end() && MBB->begin() != MBB->end() &&
         &*MBB->begin() == CPEMI;
}

static void RemoveDeadAddBetweenLEAAndJT(MachineInstr *LEAMI,
                                         MachineInstr *JumpMI,
                                         unsigned &DeadSize) {
  // Remove a dead add between the LEA and JT, which used to compute EntryReg,
  // but the JT now uses PC. Finds the last ADD (if any) that def's EntryReg
  // and is not clobbered / used.
  MachineInstr *RemovableAdd = nullptr;
  unsigned EntryReg = JumpMI->getOperand(0).getReg();

  // Find the last ADD to set EntryReg
  MachineBasicBlock::iterator I(LEAMI);
  for (++I; &*I != JumpMI; ++I) {
    if (I->getOpcode() == ARM::t2ADDrs && I->getOperand(0).getReg() == EntryReg)
      RemovableAdd = &*I;
  }

  if (!RemovableAdd)
    return;

  // Ensure EntryReg is not clobbered or used.
  MachineBasicBlock::iterator J(RemovableAdd);
  for (++J; &*J != JumpMI; ++J) {
    for (unsigned K = 0, E = J->getNumOperands(); K != E; ++K) {
      const MachineOperand &MO = J->getOperand(K);
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (MO.isDef() && MO.getReg() == EntryReg)
        return;
      if (MO.isUse() && MO.getReg() == EntryReg)
        return;
    }
  }

  LLVM_DEBUG(dbgs() << "Removing Dead Add: " << *RemovableAdd);
  RemovableAdd->eraseFromParent();
  DeadSize += 4;
}

static bool registerDefinedBetween(unsigned Reg,
                                   MachineBasicBlock::iterator From,
                                   MachineBasicBlock::iterator To,
                                   const TargetRegisterInfo *TRI) {
  for (auto I = From; I != To; ++I)
    if (I->modifiesRegister(Reg, TRI))
      return true;
  return false;
}

/// optimizeThumb2JumpTables - Use tbb / tbh instructions to generate smaller
/// jumptables when it's possible.
bool ARMConstantIslands::optimizeThumb2JumpTables() {
  bool MadeChange = false;

  // FIXME: After the tables are shrunk, can we get rid some of the
  // constantpool tables?
  MachineJumpTableInfo *MJTI = MF->getJumpTableInfo();
  if (!MJTI) return false;

  const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();
  for (unsigned i = 0, e = T2JumpTables.size(); i != e; ++i) {
    MachineInstr *MI = T2JumpTables[i];
    const MCInstrDesc &MCID = MI->getDesc();
    unsigned NumOps = MCID.getNumOperands();
    unsigned JTOpIdx = NumOps - (MI->isPredicable() ? 2 : 1);
    MachineOperand JTOP = MI->getOperand(JTOpIdx);
    unsigned JTI = JTOP.getIndex();
    assert(JTI < JT.size());

    bool ByteOk = true;
    bool HalfWordOk = true;
    unsigned JTOffset = getOffsetOf(MI) + 4;
    const std::vector<MachineBasicBlock*> &JTBBs = JT[JTI].MBBs;
    for (unsigned j = 0, ee = JTBBs.size(); j != ee; ++j) {
      MachineBasicBlock *MBB = JTBBs[j];
      unsigned DstOffset = BBInfo[MBB->getNumber()].Offset;
      // Negative offset is not ok. FIXME: We should change BB layout to make
      // sure all the branches are forward.
      if (ByteOk && (DstOffset - JTOffset) > ((1<<8)-1)*2)
        ByteOk = false;
      unsigned TBHLimit = ((1<<16)-1)*2;
      if (HalfWordOk && (DstOffset - JTOffset) > TBHLimit)
        HalfWordOk = false;
      if (!ByteOk && !HalfWordOk)
        break;
    }

    if (!ByteOk && !HalfWordOk)
      continue;

    CPUser &User = CPUsers[JumpTableUserIndices[JTI]];
    MachineBasicBlock *MBB = MI->getParent();
    if (!MI->getOperand(0).isKill()) // FIXME: needed now?
      continue;

    unsigned DeadSize = 0;
    bool CanDeleteLEA = false;
    bool BaseRegKill = false;

    unsigned IdxReg = ~0U;
    bool IdxRegKill = true;
    if (isThumb2) {
      IdxReg = MI->getOperand(1).getReg();
      IdxRegKill = MI->getOperand(1).isKill();

      bool PreservedBaseReg =
        preserveBaseRegister(MI, User.MI, DeadSize, CanDeleteLEA, BaseRegKill);
      if (!jumpTableFollowsTB(MI, User.CPEMI) && !PreservedBaseReg)
        continue;
    } else {
      // We're in thumb-1 mode, so we must have something like:
      //   %idx = tLSLri %idx, 2
      //   %base = tLEApcrelJT
      //   %t = tLDRr %base, %idx
      unsigned BaseReg = User.MI->getOperand(0).getReg();

      if (User.MI->getIterator() == User.MI->getParent()->begin())
        continue;
      MachineInstr *Shift = User.MI->getPrevNode();
      if (Shift->getOpcode() != ARM::tLSLri ||
          Shift->getOperand(3).getImm() != 2 ||
          !Shift->getOperand(2).isKill())
        continue;
      IdxReg = Shift->getOperand(2).getReg();
      unsigned ShiftedIdxReg = Shift->getOperand(0).getReg();

      // It's important that IdxReg is live until the actual TBB/TBH. Most of
      // the range is checked later, but the LEA might still clobber it and not
      // actually get removed.
      if (BaseReg == IdxReg && !jumpTableFollowsTB(MI, User.CPEMI))
        continue;

      MachineInstr *Load = User.MI->getNextNode();
      if (Load->getOpcode() != ARM::tLDRr)
        continue;
      if (Load->getOperand(1).getReg() != BaseReg ||
          Load->getOperand(2).getReg() != ShiftedIdxReg ||
          !Load->getOperand(2).isKill())
        continue;

      // If we're in PIC mode, there should be another ADD following.
      auto *TRI = STI->getRegisterInfo();

      // %base cannot be redefined after the load as it will appear before
      // TBB/TBH like:
      //      %base =
      //      %base =
      //      tBB %base, %idx
      if (registerDefinedBetween(BaseReg, Load->getNextNode(), MBB->end(), TRI))
        continue;

      if (isPositionIndependentOrROPI) {
        MachineInstr *Add = Load->getNextNode();
        if (Add->getOpcode() != ARM::tADDrr ||
            Add->getOperand(2).getReg() != BaseReg ||
            Add->getOperand(3).getReg() != Load->getOperand(0).getReg() ||
            !Add->getOperand(3).isKill())
          continue;
        if (Add->getOperand(0).getReg() != MI->getOperand(0).getReg())
          continue;
        if (registerDefinedBetween(IdxReg, Add->getNextNode(), MI, TRI))
          // IdxReg gets redefined in the middle of the sequence.
          continue;
        Add->eraseFromParent();
        DeadSize += 2;
      } else {
        if (Load->getOperand(0).getReg() != MI->getOperand(0).getReg())
          continue;
        if (registerDefinedBetween(IdxReg, Load->getNextNode(), MI, TRI))
          // IdxReg gets redefined in the middle of the sequence.
          continue;
      }

      // Now safe to delete the load and lsl. The LEA will be removed later.
      CanDeleteLEA = true;
      Shift->eraseFromParent();
      Load->eraseFromParent();
      DeadSize += 4;
    }

    LLVM_DEBUG(dbgs() << "Shrink JT: " << *MI);
    MachineInstr *CPEMI = User.CPEMI;
    unsigned Opc = ByteOk ? ARM::t2TBB_JT : ARM::t2TBH_JT;
    if (!isThumb2)
      Opc = ByteOk ? ARM::tTBB_JT : ARM::tTBH_JT;

    MachineBasicBlock::iterator MI_JT = MI;
    MachineInstr *NewJTMI =
        BuildMI(*MBB, MI_JT, MI->getDebugLoc(), TII->get(Opc))
            .addReg(User.MI->getOperand(0).getReg(),
                    getKillRegState(BaseRegKill))
            .addReg(IdxReg, getKillRegState(IdxRegKill))
            .addJumpTableIndex(JTI, JTOP.getTargetFlags())
            .addImm(CPEMI->getOperand(0).getImm());
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << ": " << *NewJTMI);

    unsigned JTOpc = ByteOk ? ARM::JUMPTABLE_TBB : ARM::JUMPTABLE_TBH;
    CPEMI->setDesc(TII->get(JTOpc));

    if (jumpTableFollowsTB(MI, User.CPEMI)) {
      NewJTMI->getOperand(0).setReg(ARM::PC);
      NewJTMI->getOperand(0).setIsKill(false);

      if (CanDeleteLEA) {
        if (isThumb2)
          RemoveDeadAddBetweenLEAAndJT(User.MI, MI, DeadSize);

        User.MI->eraseFromParent();
        DeadSize += isThumb2 ? 4 : 2;

        // The LEA was eliminated, the TBB instruction becomes the only new user
        // of the jump table.
        User.MI = NewJTMI;
        User.MaxDisp = 4;
        User.NegOk = false;
        User.IsSoImm = false;
        User.KnownAlignment = false;
      } else {
        // The LEA couldn't be eliminated, so we must add another CPUser to
        // record the TBB or TBH use.
        int CPEntryIdx = JumpTableEntryIndices[JTI];
        auto &CPEs = CPEntries[CPEntryIdx];
        auto Entry =
            find_if(CPEs, [&](CPEntry &E) { return E.CPEMI == User.CPEMI; });
        ++Entry->RefCount;
        CPUsers.emplace_back(CPUser(NewJTMI, User.CPEMI, 4, false, false));
      }
    }

    unsigned NewSize = TII->getInstSizeInBytes(*NewJTMI);
    unsigned OrigSize = TII->getInstSizeInBytes(*MI);
    MI->eraseFromParent();

    int Delta = OrigSize - NewSize + DeadSize;
    BBInfo[MBB->getNumber()].Size -= Delta;
    adjustBBOffsetsAfter(MBB);

    ++NumTBs;
    MadeChange = true;
  }

  return MadeChange;
}

/// reorderThumb2JumpTables - Adjust the function's block layout to ensure that
/// jump tables always branch forwards, since that's what tbb and tbh need.
bool ARMConstantIslands::reorderThumb2JumpTables() {
  bool MadeChange = false;

  MachineJumpTableInfo *MJTI = MF->getJumpTableInfo();
  if (!MJTI) return false;

  const std::vector<MachineJumpTableEntry> &JT = MJTI->getJumpTables();
  for (unsigned i = 0, e = T2JumpTables.size(); i != e; ++i) {
    MachineInstr *MI = T2JumpTables[i];
    const MCInstrDesc &MCID = MI->getDesc();
    unsigned NumOps = MCID.getNumOperands();
    unsigned JTOpIdx = NumOps - (MI->isPredicable() ? 2 : 1);
    MachineOperand JTOP = MI->getOperand(JTOpIdx);
    unsigned JTI = JTOP.getIndex();
    assert(JTI < JT.size());

    // We prefer if target blocks for the jump table come after the jump
    // instruction so we can use TB[BH]. Loop through the target blocks
    // and try to adjust them such that that's true.
    int JTNumber = MI->getParent()->getNumber();
    const std::vector<MachineBasicBlock*> &JTBBs = JT[JTI].MBBs;
    for (unsigned j = 0, ee = JTBBs.size(); j != ee; ++j) {
      MachineBasicBlock *MBB = JTBBs[j];
      int DTNumber = MBB->getNumber();

      if (DTNumber < JTNumber) {
        // The destination precedes the switch. Try to move the block forward
        // so we have a positive offset.
        MachineBasicBlock *NewBB =
          adjustJTTargetBlockForward(MBB, MI->getParent());
        if (NewBB)
          MJTI->ReplaceMBBInJumpTable(JTI, JTBBs[j], NewBB);
        MadeChange = true;
      }
    }
  }

  return MadeChange;
}

MachineBasicBlock *ARMConstantIslands::
adjustJTTargetBlockForward(MachineBasicBlock *BB, MachineBasicBlock *JTBB) {
  // If the destination block is terminated by an unconditional branch,
  // try to move it; otherwise, create a new block following the jump
  // table that branches back to the actual target. This is a very simple
  // heuristic. FIXME: We can definitely improve it.
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  SmallVector<MachineOperand, 4> CondPrior;
  MachineFunction::iterator BBi = BB->getIterator();
  MachineFunction::iterator OldPrior = std::prev(BBi);

  // If the block terminator isn't analyzable, don't try to move the block
  bool B = TII->analyzeBranch(*BB, TBB, FBB, Cond);

  // If the block ends in an unconditional branch, move it. The prior block
  // has to have an analyzable terminator for us to move this one. Be paranoid
  // and make sure we're not trying to move the entry block of the function.
  if (!B && Cond.empty() && BB != &MF->front() &&
      !TII->analyzeBranch(*OldPrior, TBB, FBB, CondPrior)) {
    BB->moveAfter(JTBB);
    OldPrior->updateTerminator();
    BB->updateTerminator();
    // Update numbering to account for the block being moved.
    MF->RenumberBlocks();
    ++NumJTMoved;
    return nullptr;
  }

  // Create a new MBB for the code after the jump BB.
  MachineBasicBlock *NewBB =
    MF->CreateMachineBasicBlock(JTBB->getBasicBlock());
  MachineFunction::iterator MBBI = ++JTBB->getIterator();
  MF->insert(MBBI, NewBB);

  // Add an unconditional branch from NewBB to BB.
  // There doesn't seem to be meaningful DebugInfo available; this doesn't
  // correspond directly to anything in the source.
  if (isThumb2)
    BuildMI(NewBB, DebugLoc(), TII->get(ARM::t2B))
        .addMBB(BB)
        .add(predOps(ARMCC::AL));
  else
    BuildMI(NewBB, DebugLoc(), TII->get(ARM::tB))
        .addMBB(BB)
        .add(predOps(ARMCC::AL));

  // Update internal data structures to account for the newly inserted MBB.
  MF->RenumberBlocks(NewBB);

  // Update the CFG.
  NewBB->addSuccessor(BB);
  JTBB->replaceSuccessor(BB, NewBB);

  ++NumJTInserted;
  return NewBB;
}

/// createARMConstantIslandPass - returns an instance of the constpool
/// island pass.
FunctionPass *llvm::createARMConstantIslandPass() {
  return new ARMConstantIslands();
}

INITIALIZE_PASS(ARMConstantIslands, "arm-cp-islands", ARM_CP_ISLANDS_OPT_NAME,
                false, false)
