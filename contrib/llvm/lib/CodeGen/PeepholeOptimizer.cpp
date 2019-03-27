//===- PeepholeOptimizer.cpp - Peephole Optimizations ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Perform peephole optimizations on the machine code:
//
// - Optimize Extensions
//
//     Optimization of sign / zero extension instructions. It may be extended to
//     handle other instructions with similar properties.
//
//     On some targets, some instructions, e.g. X86 sign / zero extension, may
//     leave the source value in the lower part of the result. This optimization
//     will replace some uses of the pre-extension value with uses of the
//     sub-register of the results.
//
// - Optimize Comparisons
//
//     Optimization of comparison instructions. For instance, in this code:
//
//       sub r1, 1
//       cmp r1, 0
//       bz  L1
//
//     If the "sub" instruction all ready sets (or could be modified to set) the
//     same flag that the "cmp" instruction sets and that "bz" uses, then we can
//     eliminate the "cmp" instruction.
//
//     Another instance, in this code:
//
//       sub r1, r3 | sub r1, imm
//       cmp r3, r1 or cmp r1, r3 | cmp r1, imm
//       bge L1
//
//     If the branch instruction can use flag from "sub", then we can replace
//     "sub" with "subs" and eliminate the "cmp" instruction.
//
// - Optimize Loads:
//
//     Loads that can be folded into a later instruction. A load is foldable
//     if it loads to virtual registers and the virtual register defined has
//     a single use.
//
// - Optimize Copies and Bitcast (more generally, target specific copies):
//
//     Rewrite copies and bitcasts to avoid cross register bank copies
//     when possible.
//     E.g., Consider the following example, where capital and lower
//     letters denote different register file:
//     b = copy A <-- cross-bank copy
//     C = copy b <-- cross-bank copy
//   =>
//     b = copy A <-- cross-bank copy
//     C = copy A <-- same-bank copy
//
//     E.g., for bitcast:
//     b = bitcast A <-- cross-bank copy
//     C = bitcast b <-- cross-bank copy
//   =>
//     b = bitcast A <-- cross-bank copy
//     C = copy A    <-- same-bank copy
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

using namespace llvm;
using RegSubRegPair = TargetInstrInfo::RegSubRegPair;
using RegSubRegPairAndIdx = TargetInstrInfo::RegSubRegPairAndIdx;

#define DEBUG_TYPE "peephole-opt"

// Optimize Extensions
static cl::opt<bool>
Aggressive("aggressive-ext-opt", cl::Hidden,
           cl::desc("Aggressive extension optimization"));

static cl::opt<bool>
DisablePeephole("disable-peephole", cl::Hidden, cl::init(false),
                cl::desc("Disable the peephole optimizer"));

/// Specifiy whether or not the value tracking looks through
/// complex instructions. When this is true, the value tracker
/// bails on everything that is not a copy or a bitcast.
static cl::opt<bool>
DisableAdvCopyOpt("disable-adv-copy-opt", cl::Hidden, cl::init(false),
                  cl::desc("Disable advanced copy optimization"));

static cl::opt<bool> DisableNAPhysCopyOpt(
    "disable-non-allocatable-phys-copy-opt", cl::Hidden, cl::init(false),
    cl::desc("Disable non-allocatable physical register copy optimization"));

// Limit the number of PHI instructions to process
// in PeepholeOptimizer::getNextSource.
static cl::opt<unsigned> RewritePHILimit(
    "rewrite-phi-limit", cl::Hidden, cl::init(10),
    cl::desc("Limit the length of PHI chains to lookup"));

// Limit the length of recurrence chain when evaluating the benefit of
// commuting operands.
static cl::opt<unsigned> MaxRecurrenceChain(
    "recurrence-chain-limit", cl::Hidden, cl::init(3),
    cl::desc("Maximum length of recurrence chain when evaluating the benefit "
             "of commuting operands"));


STATISTIC(NumReuse, "Number of extension results reused");
STATISTIC(NumCmps, "Number of compares eliminated");
STATISTIC(NumImmFold, "Number of move immediate folded");
STATISTIC(NumLoadFold, "Number of loads folded");
STATISTIC(NumSelects, "Number of selects optimized");
STATISTIC(NumUncoalescableCopies, "Number of uncoalescable copies optimized");
STATISTIC(NumRewrittenCopies, "Number of copies rewritten");
STATISTIC(NumNAPhysCopies, "Number of non-allocatable physical copies removed");

namespace {

  class ValueTrackerResult;
  class RecurrenceInstr;

  class PeepholeOptimizer : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    MachineRegisterInfo *MRI;
    MachineDominatorTree *DT;  // Machine dominator tree
    MachineLoopInfo *MLI;

  public:
    static char ID; // Pass identification

    PeepholeOptimizer() : MachineFunctionPass(ID) {
      initializePeepholeOptimizerPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineLoopInfo>();
      if (Aggressive) {
        AU.addRequired<MachineDominatorTree>();
        AU.addPreserved<MachineDominatorTree>();
      }
    }

    /// Track Def -> Use info used for rewriting copies.
    using RewriteMapTy = SmallDenseMap<RegSubRegPair, ValueTrackerResult>;

    /// Sequence of instructions that formulate recurrence cycle.
    using RecurrenceCycle = SmallVector<RecurrenceInstr, 4>;

  private:
    bool optimizeCmpInstr(MachineInstr &MI);
    bool optimizeExtInstr(MachineInstr &MI, MachineBasicBlock &MBB,
                          SmallPtrSetImpl<MachineInstr*> &LocalMIs);
    bool optimizeSelect(MachineInstr &MI,
                        SmallPtrSetImpl<MachineInstr *> &LocalMIs);
    bool optimizeCondBranch(MachineInstr &MI);
    bool optimizeCoalescableCopy(MachineInstr &MI);
    bool optimizeUncoalescableCopy(MachineInstr &MI,
                                   SmallPtrSetImpl<MachineInstr *> &LocalMIs);
    bool optimizeRecurrence(MachineInstr &PHI);
    bool findNextSource(RegSubRegPair RegSubReg, RewriteMapTy &RewriteMap);
    bool isMoveImmediate(MachineInstr &MI,
                         SmallSet<unsigned, 4> &ImmDefRegs,
                         DenseMap<unsigned, MachineInstr*> &ImmDefMIs);
    bool foldImmediate(MachineInstr &MI, SmallSet<unsigned, 4> &ImmDefRegs,
                       DenseMap<unsigned, MachineInstr*> &ImmDefMIs);

    /// Finds recurrence cycles, but only ones that formulated around
    /// a def operand and a use operand that are tied. If there is a use
    /// operand commutable with the tied use operand, find recurrence cycle
    /// along that operand as well.
    bool findTargetRecurrence(unsigned Reg,
                              const SmallSet<unsigned, 2> &TargetReg,
                              RecurrenceCycle &RC);

    /// If copy instruction \p MI is a virtual register copy, track it in
    /// the set \p CopySrcRegs and \p CopyMIs. If this virtual register was
    /// previously seen as a copy, replace the uses of this copy with the
    /// previously seen copy's destination register.
    bool foldRedundantCopy(MachineInstr &MI,
                           SmallSet<unsigned, 4> &CopySrcRegs,
                           DenseMap<unsigned, MachineInstr *> &CopyMIs);

    /// Is the register \p Reg a non-allocatable physical register?
    bool isNAPhysCopy(unsigned Reg);

    /// If copy instruction \p MI is a non-allocatable virtual<->physical
    /// register copy, track it in the \p NAPhysToVirtMIs map. If this
    /// non-allocatable physical register was previously copied to a virtual
    /// registered and hasn't been clobbered, the virt->phys copy can be
    /// deleted.
    bool foldRedundantNAPhysCopy(MachineInstr &MI,
        DenseMap<unsigned, MachineInstr *> &NAPhysToVirtMIs);

    bool isLoadFoldable(MachineInstr &MI,
                        SmallSet<unsigned, 16> &FoldAsLoadDefCandidates);

    /// Check whether \p MI is understood by the register coalescer
    /// but may require some rewriting.
    bool isCoalescableCopy(const MachineInstr &MI) {
      // SubregToRegs are not interesting, because they are already register
      // coalescer friendly.
      return MI.isCopy() || (!DisableAdvCopyOpt &&
                             (MI.isRegSequence() || MI.isInsertSubreg() ||
                              MI.isExtractSubreg()));
    }

    /// Check whether \p MI is a copy like instruction that is
    /// not recognized by the register coalescer.
    bool isUncoalescableCopy(const MachineInstr &MI) {
      return MI.isBitcast() ||
             (!DisableAdvCopyOpt &&
              (MI.isRegSequenceLike() || MI.isInsertSubregLike() ||
               MI.isExtractSubregLike()));
    }

    MachineInstr &rewriteSource(MachineInstr &CopyLike,
                                RegSubRegPair Def, RewriteMapTy &RewriteMap);
  };

  /// Helper class to hold instructions that are inside recurrence cycles.
  /// The recurrence cycle is formulated around 1) a def operand and its
  /// tied use operand, or 2) a def operand and a use operand that is commutable
  /// with another use operand which is tied to the def operand. In the latter
  /// case, index of the tied use operand and the commutable use operand are
  /// maintained with CommutePair.
  class RecurrenceInstr {
  public:
    using IndexPair = std::pair<unsigned, unsigned>;

    RecurrenceInstr(MachineInstr *MI) : MI(MI) {}
    RecurrenceInstr(MachineInstr *MI, unsigned Idx1, unsigned Idx2)
      : MI(MI), CommutePair(std::make_pair(Idx1, Idx2)) {}

    MachineInstr *getMI() const { return MI; }
    Optional<IndexPair> getCommutePair() const { return CommutePair; }

  private:
    MachineInstr *MI;
    Optional<IndexPair> CommutePair;
  };

  /// Helper class to hold a reply for ValueTracker queries.
  /// Contains the returned sources for a given search and the instructions
  /// where the sources were tracked from.
  class ValueTrackerResult {
  private:
    /// Track all sources found by one ValueTracker query.
    SmallVector<RegSubRegPair, 2> RegSrcs;

    /// Instruction using the sources in 'RegSrcs'.
    const MachineInstr *Inst = nullptr;

  public:
    ValueTrackerResult() = default;

    ValueTrackerResult(unsigned Reg, unsigned SubReg) {
      addSource(Reg, SubReg);
    }

    bool isValid() const { return getNumSources() > 0; }

    void setInst(const MachineInstr *I) { Inst = I; }
    const MachineInstr *getInst() const { return Inst; }

    void clear() {
      RegSrcs.clear();
      Inst = nullptr;
    }

    void addSource(unsigned SrcReg, unsigned SrcSubReg) {
      RegSrcs.push_back(RegSubRegPair(SrcReg, SrcSubReg));
    }

    void setSource(int Idx, unsigned SrcReg, unsigned SrcSubReg) {
      assert(Idx < getNumSources() && "Reg pair source out of index");
      RegSrcs[Idx] = RegSubRegPair(SrcReg, SrcSubReg);
    }

    int getNumSources() const { return RegSrcs.size(); }

    RegSubRegPair getSrc(int Idx) const {
      return RegSrcs[Idx];
    }

    unsigned getSrcReg(int Idx) const {
      assert(Idx < getNumSources() && "Reg source out of index");
      return RegSrcs[Idx].Reg;
    }

    unsigned getSrcSubReg(int Idx) const {
      assert(Idx < getNumSources() && "SubReg source out of index");
      return RegSrcs[Idx].SubReg;
    }

    bool operator==(const ValueTrackerResult &Other) {
      if (Other.getInst() != getInst())
        return false;

      if (Other.getNumSources() != getNumSources())
        return false;

      for (int i = 0, e = Other.getNumSources(); i != e; ++i)
        if (Other.getSrcReg(i) != getSrcReg(i) ||
            Other.getSrcSubReg(i) != getSrcSubReg(i))
          return false;
      return true;
    }
  };

  /// Helper class to track the possible sources of a value defined by
  /// a (chain of) copy related instructions.
  /// Given a definition (instruction and definition index), this class
  /// follows the use-def chain to find successive suitable sources.
  /// The given source can be used to rewrite the definition into
  /// def = COPY src.
  ///
  /// For instance, let us consider the following snippet:
  /// v0 =
  /// v2 = INSERT_SUBREG v1, v0, sub0
  /// def = COPY v2.sub0
  ///
  /// Using a ValueTracker for def = COPY v2.sub0 will give the following
  /// suitable sources:
  /// v2.sub0 and v0.
  /// Then, def can be rewritten into def = COPY v0.
  class ValueTracker {
  private:
    /// The current point into the use-def chain.
    const MachineInstr *Def = nullptr;

    /// The index of the definition in Def.
    unsigned DefIdx = 0;

    /// The sub register index of the definition.
    unsigned DefSubReg;

    /// The register where the value can be found.
    unsigned Reg;

    /// MachineRegisterInfo used to perform tracking.
    const MachineRegisterInfo &MRI;

    /// Optional TargetInstrInfo used to perform some complex tracking.
    const TargetInstrInfo *TII;

    /// Dispatcher to the right underlying implementation of getNextSource.
    ValueTrackerResult getNextSourceImpl();

    /// Specialized version of getNextSource for Copy instructions.
    ValueTrackerResult getNextSourceFromCopy();

    /// Specialized version of getNextSource for Bitcast instructions.
    ValueTrackerResult getNextSourceFromBitcast();

    /// Specialized version of getNextSource for RegSequence instructions.
    ValueTrackerResult getNextSourceFromRegSequence();

    /// Specialized version of getNextSource for InsertSubreg instructions.
    ValueTrackerResult getNextSourceFromInsertSubreg();

    /// Specialized version of getNextSource for ExtractSubreg instructions.
    ValueTrackerResult getNextSourceFromExtractSubreg();

    /// Specialized version of getNextSource for SubregToReg instructions.
    ValueTrackerResult getNextSourceFromSubregToReg();

    /// Specialized version of getNextSource for PHI instructions.
    ValueTrackerResult getNextSourceFromPHI();

  public:
    /// Create a ValueTracker instance for the value defined by \p Reg.
    /// \p DefSubReg represents the sub register index the value tracker will
    /// track. It does not need to match the sub register index used in the
    /// definition of \p Reg.
    /// If \p Reg is a physical register, a value tracker constructed with
    /// this constructor will not find any alternative source.
    /// Indeed, when \p Reg is a physical register that constructor does not
    /// know which definition of \p Reg it should track.
    /// Use the next constructor to track a physical register.
    ValueTracker(unsigned Reg, unsigned DefSubReg,
                 const MachineRegisterInfo &MRI,
                 const TargetInstrInfo *TII = nullptr)
        : DefSubReg(DefSubReg), Reg(Reg), MRI(MRI), TII(TII) {
      if (!TargetRegisterInfo::isPhysicalRegister(Reg)) {
        Def = MRI.getVRegDef(Reg);
        DefIdx = MRI.def_begin(Reg).getOperandNo();
      }
    }

    /// Following the use-def chain, get the next available source
    /// for the tracked value.
    /// \return A ValueTrackerResult containing a set of registers
    /// and sub registers with tracked values. A ValueTrackerResult with
    /// an empty set of registers means no source was found.
    ValueTrackerResult getNextSource();
  };

} // end anonymous namespace

char PeepholeOptimizer::ID = 0;

char &llvm::PeepholeOptimizerID = PeepholeOptimizer::ID;

INITIALIZE_PASS_BEGIN(PeepholeOptimizer, DEBUG_TYPE,
                      "Peephole Optimizations", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(PeepholeOptimizer, DEBUG_TYPE,
                    "Peephole Optimizations", false, false)

/// If instruction is a copy-like instruction, i.e. it reads a single register
/// and writes a single register and it does not modify the source, and if the
/// source value is preserved as a sub-register of the result, then replace all
/// reachable uses of the source with the subreg of the result.
///
/// Do not generate an EXTRACT that is used only in a debug use, as this changes
/// the code. Since this code does not currently share EXTRACTs, just ignore all
/// debug uses.
bool PeepholeOptimizer::
optimizeExtInstr(MachineInstr &MI, MachineBasicBlock &MBB,
                 SmallPtrSetImpl<MachineInstr*> &LocalMIs) {
  unsigned SrcReg, DstReg, SubIdx;
  if (!TII->isCoalescableExtInstr(MI, SrcReg, DstReg, SubIdx))
    return false;

  if (TargetRegisterInfo::isPhysicalRegister(DstReg) ||
      TargetRegisterInfo::isPhysicalRegister(SrcReg))
    return false;

  if (MRI->hasOneNonDBGUse(SrcReg))
    // No other uses.
    return false;

  // Ensure DstReg can get a register class that actually supports
  // sub-registers. Don't change the class until we commit.
  const TargetRegisterClass *DstRC = MRI->getRegClass(DstReg);
  DstRC = TRI->getSubClassWithSubReg(DstRC, SubIdx);
  if (!DstRC)
    return false;

  // The ext instr may be operating on a sub-register of SrcReg as well.
  // PPC::EXTSW is a 32 -> 64-bit sign extension, but it reads a 64-bit
  // register.
  // If UseSrcSubIdx is Set, SubIdx also applies to SrcReg, and only uses of
  // SrcReg:SubIdx should be replaced.
  bool UseSrcSubIdx =
      TRI->getSubClassWithSubReg(MRI->getRegClass(SrcReg), SubIdx) != nullptr;

  // The source has other uses. See if we can replace the other uses with use of
  // the result of the extension.
  SmallPtrSet<MachineBasicBlock*, 4> ReachedBBs;
  for (MachineInstr &UI : MRI->use_nodbg_instructions(DstReg))
    ReachedBBs.insert(UI.getParent());

  // Uses that are in the same BB of uses of the result of the instruction.
  SmallVector<MachineOperand*, 8> Uses;

  // Uses that the result of the instruction can reach.
  SmallVector<MachineOperand*, 8> ExtendedUses;

  bool ExtendLife = true;
  for (MachineOperand &UseMO : MRI->use_nodbg_operands(SrcReg)) {
    MachineInstr *UseMI = UseMO.getParent();
    if (UseMI == &MI)
      continue;

    if (UseMI->isPHI()) {
      ExtendLife = false;
      continue;
    }

    // Only accept uses of SrcReg:SubIdx.
    if (UseSrcSubIdx && UseMO.getSubReg() != SubIdx)
      continue;

    // It's an error to translate this:
    //
    //    %reg1025 = <sext> %reg1024
    //     ...
    //    %reg1026 = SUBREG_TO_REG 0, %reg1024, 4
    //
    // into this:
    //
    //    %reg1025 = <sext> %reg1024
    //     ...
    //    %reg1027 = COPY %reg1025:4
    //    %reg1026 = SUBREG_TO_REG 0, %reg1027, 4
    //
    // The problem here is that SUBREG_TO_REG is there to assert that an
    // implicit zext occurs. It doesn't insert a zext instruction. If we allow
    // the COPY here, it will give us the value after the <sext>, not the
    // original value of %reg1024 before <sext>.
    if (UseMI->getOpcode() == TargetOpcode::SUBREG_TO_REG)
      continue;

    MachineBasicBlock *UseMBB = UseMI->getParent();
    if (UseMBB == &MBB) {
      // Local uses that come after the extension.
      if (!LocalMIs.count(UseMI))
        Uses.push_back(&UseMO);
    } else if (ReachedBBs.count(UseMBB)) {
      // Non-local uses where the result of the extension is used. Always
      // replace these unless it's a PHI.
      Uses.push_back(&UseMO);
    } else if (Aggressive && DT->dominates(&MBB, UseMBB)) {
      // We may want to extend the live range of the extension result in order
      // to replace these uses.
      ExtendedUses.push_back(&UseMO);
    } else {
      // Both will be live out of the def MBB anyway. Don't extend live range of
      // the extension result.
      ExtendLife = false;
      break;
    }
  }

  if (ExtendLife && !ExtendedUses.empty())
    // Extend the liveness of the extension result.
    Uses.append(ExtendedUses.begin(), ExtendedUses.end());

  // Now replace all uses.
  bool Changed = false;
  if (!Uses.empty()) {
    SmallPtrSet<MachineBasicBlock*, 4> PHIBBs;

    // Look for PHI uses of the extended result, we don't want to extend the
    // liveness of a PHI input. It breaks all kinds of assumptions down
    // stream. A PHI use is expected to be the kill of its source values.
    for (MachineInstr &UI : MRI->use_nodbg_instructions(DstReg))
      if (UI.isPHI())
        PHIBBs.insert(UI.getParent());

    const TargetRegisterClass *RC = MRI->getRegClass(SrcReg);
    for (unsigned i = 0, e = Uses.size(); i != e; ++i) {
      MachineOperand *UseMO = Uses[i];
      MachineInstr *UseMI = UseMO->getParent();
      MachineBasicBlock *UseMBB = UseMI->getParent();
      if (PHIBBs.count(UseMBB))
        continue;

      // About to add uses of DstReg, clear DstReg's kill flags.
      if (!Changed) {
        MRI->clearKillFlags(DstReg);
        MRI->constrainRegClass(DstReg, DstRC);
      }

      unsigned NewVR = MRI->createVirtualRegister(RC);
      MachineInstr *Copy = BuildMI(*UseMBB, UseMI, UseMI->getDebugLoc(),
                                   TII->get(TargetOpcode::COPY), NewVR)
        .addReg(DstReg, 0, SubIdx);
      // SubIdx applies to both SrcReg and DstReg when UseSrcSubIdx is set.
      if (UseSrcSubIdx) {
        Copy->getOperand(0).setSubReg(SubIdx);
        Copy->getOperand(0).setIsUndef();
      }
      UseMO->setReg(NewVR);
      ++NumReuse;
      Changed = true;
    }
  }

  return Changed;
}

/// If the instruction is a compare and the previous instruction it's comparing
/// against already sets (or could be modified to set) the same flag as the
/// compare, then we can remove the comparison and use the flag from the
/// previous instruction.
bool PeepholeOptimizer::optimizeCmpInstr(MachineInstr &MI) {
  // If this instruction is a comparison against zero and isn't comparing a
  // physical register, we can try to optimize it.
  unsigned SrcReg, SrcReg2;
  int CmpMask, CmpValue;
  if (!TII->analyzeCompare(MI, SrcReg, SrcReg2, CmpMask, CmpValue) ||
      TargetRegisterInfo::isPhysicalRegister(SrcReg) ||
      (SrcReg2 != 0 && TargetRegisterInfo::isPhysicalRegister(SrcReg2)))
    return false;

  // Attempt to optimize the comparison instruction.
  if (TII->optimizeCompareInstr(MI, SrcReg, SrcReg2, CmpMask, CmpValue, MRI)) {
    ++NumCmps;
    return true;
  }

  return false;
}

/// Optimize a select instruction.
bool PeepholeOptimizer::optimizeSelect(MachineInstr &MI,
                            SmallPtrSetImpl<MachineInstr *> &LocalMIs) {
  unsigned TrueOp = 0;
  unsigned FalseOp = 0;
  bool Optimizable = false;
  SmallVector<MachineOperand, 4> Cond;
  if (TII->analyzeSelect(MI, Cond, TrueOp, FalseOp, Optimizable))
    return false;
  if (!Optimizable)
    return false;
  if (!TII->optimizeSelect(MI, LocalMIs))
    return false;
  MI.eraseFromParent();
  ++NumSelects;
  return true;
}

/// Check if a simpler conditional branch can be generated.
bool PeepholeOptimizer::optimizeCondBranch(MachineInstr &MI) {
  return TII->optimizeCondBranch(MI);
}

/// Try to find the next source that share the same register file
/// for the value defined by \p Reg and \p SubReg.
/// When true is returned, the \p RewriteMap can be used by the client to
/// retrieve all Def -> Use along the way up to the next source. Any found
/// Use that is not itself a key for another entry, is the next source to
/// use. During the search for the next source, multiple sources can be found
/// given multiple incoming sources of a PHI instruction. In this case, we
/// look in each PHI source for the next source; all found next sources must
/// share the same register file as \p Reg and \p SubReg. The client should
/// then be capable to rewrite all intermediate PHIs to get the next source.
/// \return False if no alternative sources are available. True otherwise.
bool PeepholeOptimizer::findNextSource(RegSubRegPair RegSubReg,
                                       RewriteMapTy &RewriteMap) {
  // Do not try to find a new source for a physical register.
  // So far we do not have any motivating example for doing that.
  // Thus, instead of maintaining untested code, we will revisit that if
  // that changes at some point.
  unsigned Reg = RegSubReg.Reg;
  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return false;
  const TargetRegisterClass *DefRC = MRI->getRegClass(Reg);

  SmallVector<RegSubRegPair, 4> SrcToLook;
  RegSubRegPair CurSrcPair = RegSubReg;
  SrcToLook.push_back(CurSrcPair);

  unsigned PHICount = 0;
  do {
    CurSrcPair = SrcToLook.pop_back_val();
    // As explained above, do not handle physical registers
    if (TargetRegisterInfo::isPhysicalRegister(CurSrcPair.Reg))
      return false;

    ValueTracker ValTracker(CurSrcPair.Reg, CurSrcPair.SubReg, *MRI, TII);

    // Follow the chain of copies until we find a more suitable source, a phi
    // or have to abort.
    while (true) {
      ValueTrackerResult Res = ValTracker.getNextSource();
      // Abort at the end of a chain (without finding a suitable source).
      if (!Res.isValid())
        return false;

      // Insert the Def -> Use entry for the recently found source.
      ValueTrackerResult CurSrcRes = RewriteMap.lookup(CurSrcPair);
      if (CurSrcRes.isValid()) {
        assert(CurSrcRes == Res && "ValueTrackerResult found must match");
        // An existent entry with multiple sources is a PHI cycle we must avoid.
        // Otherwise it's an entry with a valid next source we already found.
        if (CurSrcRes.getNumSources() > 1) {
          LLVM_DEBUG(dbgs()
                     << "findNextSource: found PHI cycle, aborting...\n");
          return false;
        }
        break;
      }
      RewriteMap.insert(std::make_pair(CurSrcPair, Res));

      // ValueTrackerResult usually have one source unless it's the result from
      // a PHI instruction. Add the found PHI edges to be looked up further.
      unsigned NumSrcs = Res.getNumSources();
      if (NumSrcs > 1) {
        PHICount++;
        if (PHICount >= RewritePHILimit) {
          LLVM_DEBUG(dbgs() << "findNextSource: PHI limit reached\n");
          return false;
        }

        for (unsigned i = 0; i < NumSrcs; ++i)
          SrcToLook.push_back(Res.getSrc(i));
        break;
      }

      CurSrcPair = Res.getSrc(0);
      // Do not extend the live-ranges of physical registers as they add
      // constraints to the register allocator. Moreover, if we want to extend
      // the live-range of a physical register, unlike SSA virtual register,
      // we will have to check that they aren't redefine before the related use.
      if (TargetRegisterInfo::isPhysicalRegister(CurSrcPair.Reg))
        return false;

      // Keep following the chain if the value isn't any better yet.
      const TargetRegisterClass *SrcRC = MRI->getRegClass(CurSrcPair.Reg);
      if (!TRI->shouldRewriteCopySrc(DefRC, RegSubReg.SubReg, SrcRC,
                                     CurSrcPair.SubReg))
        continue;

      // We currently cannot deal with subreg operands on PHI instructions
      // (see insertPHI()).
      if (PHICount > 0 && CurSrcPair.SubReg != 0)
        continue;

      // We found a suitable source, and are done with this chain.
      break;
    }
  } while (!SrcToLook.empty());

  // If we did not find a more suitable source, there is nothing to optimize.
  return CurSrcPair.Reg != Reg;
}

/// Insert a PHI instruction with incoming edges \p SrcRegs that are
/// guaranteed to have the same register class. This is necessary whenever we
/// successfully traverse a PHI instruction and find suitable sources coming
/// from its edges. By inserting a new PHI, we provide a rewritten PHI def
/// suitable to be used in a new COPY instruction.
static MachineInstr &
insertPHI(MachineRegisterInfo &MRI, const TargetInstrInfo &TII,
          const SmallVectorImpl<RegSubRegPair> &SrcRegs,
          MachineInstr &OrigPHI) {
  assert(!SrcRegs.empty() && "No sources to create a PHI instruction?");

  const TargetRegisterClass *NewRC = MRI.getRegClass(SrcRegs[0].Reg);
  // NewRC is only correct if no subregisters are involved. findNextSource()
  // should have rejected those cases already.
  assert(SrcRegs[0].SubReg == 0 && "should not have subreg operand");
  unsigned NewVR = MRI.createVirtualRegister(NewRC);
  MachineBasicBlock *MBB = OrigPHI.getParent();
  MachineInstrBuilder MIB = BuildMI(*MBB, &OrigPHI, OrigPHI.getDebugLoc(),
                                    TII.get(TargetOpcode::PHI), NewVR);

  unsigned MBBOpIdx = 2;
  for (const RegSubRegPair &RegPair : SrcRegs) {
    MIB.addReg(RegPair.Reg, 0, RegPair.SubReg);
    MIB.addMBB(OrigPHI.getOperand(MBBOpIdx).getMBB());
    // Since we're extended the lifetime of RegPair.Reg, clear the
    // kill flags to account for that and make RegPair.Reg reaches
    // the new PHI.
    MRI.clearKillFlags(RegPair.Reg);
    MBBOpIdx += 2;
  }

  return *MIB;
}

namespace {

/// Interface to query instructions amenable to copy rewriting.
class Rewriter {
protected:
  MachineInstr &CopyLike;
  unsigned CurrentSrcIdx = 0;   ///< The index of the source being rewritten.
public:
  Rewriter(MachineInstr &CopyLike) : CopyLike(CopyLike) {}
  virtual ~Rewriter() {}

  /// Get the next rewritable source (SrcReg, SrcSubReg) and
  /// the related value that it affects (DstReg, DstSubReg).
  /// A source is considered rewritable if its register class and the
  /// register class of the related DstReg may not be register
  /// coalescer friendly. In other words, given a copy-like instruction
  /// not all the arguments may be returned at rewritable source, since
  /// some arguments are none to be register coalescer friendly.
  ///
  /// Each call of this method moves the current source to the next
  /// rewritable source.
  /// For instance, let CopyLike be the instruction to rewrite.
  /// CopyLike has one definition and one source:
  /// dst.dstSubIdx = CopyLike src.srcSubIdx.
  ///
  /// The first call will give the first rewritable source, i.e.,
  /// the only source this instruction has:
  /// (SrcReg, SrcSubReg) = (src, srcSubIdx).
  /// This source defines the whole definition, i.e.,
  /// (DstReg, DstSubReg) = (dst, dstSubIdx).
  ///
  /// The second and subsequent calls will return false, as there is only one
  /// rewritable source.
  ///
  /// \return True if a rewritable source has been found, false otherwise.
  /// The output arguments are valid if and only if true is returned.
  virtual bool getNextRewritableSource(RegSubRegPair &Src,
                                       RegSubRegPair &Dst) = 0;

  /// Rewrite the current source with \p NewReg and \p NewSubReg if possible.
  /// \return True if the rewriting was possible, false otherwise.
  virtual bool RewriteCurrentSource(unsigned NewReg, unsigned NewSubReg) = 0;
};

/// Rewriter for COPY instructions.
class CopyRewriter : public Rewriter {
public:
  CopyRewriter(MachineInstr &MI) : Rewriter(MI) {
    assert(MI.isCopy() && "Expected copy instruction");
  }
  virtual ~CopyRewriter() = default;

  bool getNextRewritableSource(RegSubRegPair &Src,
                               RegSubRegPair &Dst) override {
    // CurrentSrcIdx > 0 means this function has already been called.
    if (CurrentSrcIdx > 0)
      return false;
    // This is the first call to getNextRewritableSource.
    // Move the CurrentSrcIdx to remember that we made that call.
    CurrentSrcIdx = 1;
    // The rewritable source is the argument.
    const MachineOperand &MOSrc = CopyLike.getOperand(1);
    Src = RegSubRegPair(MOSrc.getReg(), MOSrc.getSubReg());
    // What we track are the alternative sources of the definition.
    const MachineOperand &MODef = CopyLike.getOperand(0);
    Dst = RegSubRegPair(MODef.getReg(), MODef.getSubReg());
    return true;
  }

  bool RewriteCurrentSource(unsigned NewReg, unsigned NewSubReg) override {
    if (CurrentSrcIdx != 1)
      return false;
    MachineOperand &MOSrc = CopyLike.getOperand(CurrentSrcIdx);
    MOSrc.setReg(NewReg);
    MOSrc.setSubReg(NewSubReg);
    return true;
  }
};

/// Helper class to rewrite uncoalescable copy like instructions
/// into new COPY (coalescable friendly) instructions.
class UncoalescableRewriter : public Rewriter {
  unsigned NumDefs;  ///< Number of defs in the bitcast.

public:
  UncoalescableRewriter(MachineInstr &MI) : Rewriter(MI) {
    NumDefs = MI.getDesc().getNumDefs();
  }

  /// \see See Rewriter::getNextRewritableSource()
  /// All such sources need to be considered rewritable in order to
  /// rewrite a uncoalescable copy-like instruction. This method return
  /// each definition that must be checked if rewritable.
  bool getNextRewritableSource(RegSubRegPair &Src,
                               RegSubRegPair &Dst) override {
    // Find the next non-dead definition and continue from there.
    if (CurrentSrcIdx == NumDefs)
      return false;

    while (CopyLike.getOperand(CurrentSrcIdx).isDead()) {
      ++CurrentSrcIdx;
      if (CurrentSrcIdx == NumDefs)
        return false;
    }

    // What we track are the alternative sources of the definition.
    Src = RegSubRegPair(0, 0);
    const MachineOperand &MODef = CopyLike.getOperand(CurrentSrcIdx);
    Dst = RegSubRegPair(MODef.getReg(), MODef.getSubReg());

    CurrentSrcIdx++;
    return true;
  }

  bool RewriteCurrentSource(unsigned NewReg, unsigned NewSubReg) override {
    return false;
  }
};

/// Specialized rewriter for INSERT_SUBREG instruction.
class InsertSubregRewriter : public Rewriter {
public:
  InsertSubregRewriter(MachineInstr &MI) : Rewriter(MI) {
    assert(MI.isInsertSubreg() && "Invalid instruction");
  }

  /// \see See Rewriter::getNextRewritableSource()
  /// Here CopyLike has the following form:
  /// dst = INSERT_SUBREG Src1, Src2.src2SubIdx, subIdx.
  /// Src1 has the same register class has dst, hence, there is
  /// nothing to rewrite.
  /// Src2.src2SubIdx, may not be register coalescer friendly.
  /// Therefore, the first call to this method returns:
  /// (SrcReg, SrcSubReg) = (Src2, src2SubIdx).
  /// (DstReg, DstSubReg) = (dst, subIdx).
  ///
  /// Subsequence calls will return false.
  bool getNextRewritableSource(RegSubRegPair &Src,
                               RegSubRegPair &Dst) override {
    // If we already get the only source we can rewrite, return false.
    if (CurrentSrcIdx == 2)
      return false;
    // We are looking at v2 = INSERT_SUBREG v0, v1, sub0.
    CurrentSrcIdx = 2;
    const MachineOperand &MOInsertedReg = CopyLike.getOperand(2);
    Src = RegSubRegPair(MOInsertedReg.getReg(), MOInsertedReg.getSubReg());
    const MachineOperand &MODef = CopyLike.getOperand(0);

    // We want to track something that is compatible with the
    // partial definition.
    if (MODef.getSubReg())
      // Bail if we have to compose sub-register indices.
      return false;
    Dst = RegSubRegPair(MODef.getReg(),
                        (unsigned)CopyLike.getOperand(3).getImm());
    return true;
  }

  bool RewriteCurrentSource(unsigned NewReg, unsigned NewSubReg) override {
    if (CurrentSrcIdx != 2)
      return false;
    // We are rewriting the inserted reg.
    MachineOperand &MO = CopyLike.getOperand(CurrentSrcIdx);
    MO.setReg(NewReg);
    MO.setSubReg(NewSubReg);
    return true;
  }
};

/// Specialized rewriter for EXTRACT_SUBREG instruction.
class ExtractSubregRewriter : public Rewriter {
  const TargetInstrInfo &TII;

public:
  ExtractSubregRewriter(MachineInstr &MI, const TargetInstrInfo &TII)
      : Rewriter(MI), TII(TII) {
    assert(MI.isExtractSubreg() && "Invalid instruction");
  }

  /// \see Rewriter::getNextRewritableSource()
  /// Here CopyLike has the following form:
  /// dst.dstSubIdx = EXTRACT_SUBREG Src, subIdx.
  /// There is only one rewritable source: Src.subIdx,
  /// which defines dst.dstSubIdx.
  bool getNextRewritableSource(RegSubRegPair &Src,
                               RegSubRegPair &Dst) override {
    // If we already get the only source we can rewrite, return false.
    if (CurrentSrcIdx == 1)
      return false;
    // We are looking at v1 = EXTRACT_SUBREG v0, sub0.
    CurrentSrcIdx = 1;
    const MachineOperand &MOExtractedReg = CopyLike.getOperand(1);
    // If we have to compose sub-register indices, bail out.
    if (MOExtractedReg.getSubReg())
      return false;

    Src = RegSubRegPair(MOExtractedReg.getReg(),
                        CopyLike.getOperand(2).getImm());

    // We want to track something that is compatible with the definition.
    const MachineOperand &MODef = CopyLike.getOperand(0);
    Dst = RegSubRegPair(MODef.getReg(), MODef.getSubReg());
    return true;
  }

  bool RewriteCurrentSource(unsigned NewReg, unsigned NewSubReg) override {
    // The only source we can rewrite is the input register.
    if (CurrentSrcIdx != 1)
      return false;

    CopyLike.getOperand(CurrentSrcIdx).setReg(NewReg);

    // If we find a source that does not require to extract something,
    // rewrite the operation with a copy.
    if (!NewSubReg) {
      // Move the current index to an invalid position.
      // We do not want another call to this method to be able
      // to do any change.
      CurrentSrcIdx = -1;
      // Rewrite the operation as a COPY.
      // Get rid of the sub-register index.
      CopyLike.RemoveOperand(2);
      // Morph the operation into a COPY.
      CopyLike.setDesc(TII.get(TargetOpcode::COPY));
      return true;
    }
    CopyLike.getOperand(CurrentSrcIdx + 1).setImm(NewSubReg);
    return true;
  }
};

/// Specialized rewriter for REG_SEQUENCE instruction.
class RegSequenceRewriter : public Rewriter {
public:
  RegSequenceRewriter(MachineInstr &MI) : Rewriter(MI) {
    assert(MI.isRegSequence() && "Invalid instruction");
  }

  /// \see Rewriter::getNextRewritableSource()
  /// Here CopyLike has the following form:
  /// dst = REG_SEQUENCE Src1.src1SubIdx, subIdx1, Src2.src2SubIdx, subIdx2.
  /// Each call will return a different source, walking all the available
  /// source.
  ///
  /// The first call returns:
  /// (SrcReg, SrcSubReg) = (Src1, src1SubIdx).
  /// (DstReg, DstSubReg) = (dst, subIdx1).
  ///
  /// The second call returns:
  /// (SrcReg, SrcSubReg) = (Src2, src2SubIdx).
  /// (DstReg, DstSubReg) = (dst, subIdx2).
  ///
  /// And so on, until all the sources have been traversed, then
  /// it returns false.
  bool getNextRewritableSource(RegSubRegPair &Src,
                               RegSubRegPair &Dst) override {
    // We are looking at v0 = REG_SEQUENCE v1, sub1, v2, sub2, etc.

    // If this is the first call, move to the first argument.
    if (CurrentSrcIdx == 0) {
      CurrentSrcIdx = 1;
    } else {
      // Otherwise, move to the next argument and check that it is valid.
      CurrentSrcIdx += 2;
      if (CurrentSrcIdx >= CopyLike.getNumOperands())
        return false;
    }
    const MachineOperand &MOInsertedReg = CopyLike.getOperand(CurrentSrcIdx);
    Src.Reg = MOInsertedReg.getReg();
    // If we have to compose sub-register indices, bail out.
    if ((Src.SubReg = MOInsertedReg.getSubReg()))
      return false;

    // We want to track something that is compatible with the related
    // partial definition.
    Dst.SubReg = CopyLike.getOperand(CurrentSrcIdx + 1).getImm();

    const MachineOperand &MODef = CopyLike.getOperand(0);
    Dst.Reg = MODef.getReg();
    // If we have to compose sub-registers, bail.
    return MODef.getSubReg() == 0;
  }

  bool RewriteCurrentSource(unsigned NewReg, unsigned NewSubReg) override {
    // We cannot rewrite out of bound operands.
    // Moreover, rewritable sources are at odd positions.
    if ((CurrentSrcIdx & 1) != 1 || CurrentSrcIdx > CopyLike.getNumOperands())
      return false;

    MachineOperand &MO = CopyLike.getOperand(CurrentSrcIdx);
    MO.setReg(NewReg);
    MO.setSubReg(NewSubReg);
    return true;
  }
};

} // end anonymous namespace

/// Get the appropriated Rewriter for \p MI.
/// \return A pointer to a dynamically allocated Rewriter or nullptr if no
/// rewriter works for \p MI.
static Rewriter *getCopyRewriter(MachineInstr &MI, const TargetInstrInfo &TII) {
  // Handle uncoalescable copy-like instructions.
  if (MI.isBitcast() || MI.isRegSequenceLike() || MI.isInsertSubregLike() ||
      MI.isExtractSubregLike())
    return new UncoalescableRewriter(MI);

  switch (MI.getOpcode()) {
  default:
    return nullptr;
  case TargetOpcode::COPY:
    return new CopyRewriter(MI);
  case TargetOpcode::INSERT_SUBREG:
    return new InsertSubregRewriter(MI);
  case TargetOpcode::EXTRACT_SUBREG:
    return new ExtractSubregRewriter(MI, TII);
  case TargetOpcode::REG_SEQUENCE:
    return new RegSequenceRewriter(MI);
  }
}

/// Given a \p Def.Reg and Def.SubReg  pair, use \p RewriteMap to find
/// the new source to use for rewrite. If \p HandleMultipleSources is true and
/// multiple sources for a given \p Def are found along the way, we found a
/// PHI instructions that needs to be rewritten.
/// TODO: HandleMultipleSources should be removed once we test PHI handling
/// with coalescable copies.
static RegSubRegPair
getNewSource(MachineRegisterInfo *MRI, const TargetInstrInfo *TII,
             RegSubRegPair Def,
             const PeepholeOptimizer::RewriteMapTy &RewriteMap,
             bool HandleMultipleSources = true) {
  RegSubRegPair LookupSrc(Def.Reg, Def.SubReg);
  while (true) {
    ValueTrackerResult Res = RewriteMap.lookup(LookupSrc);
    // If there are no entries on the map, LookupSrc is the new source.
    if (!Res.isValid())
      return LookupSrc;

    // There's only one source for this definition, keep searching...
    unsigned NumSrcs = Res.getNumSources();
    if (NumSrcs == 1) {
      LookupSrc.Reg = Res.getSrcReg(0);
      LookupSrc.SubReg = Res.getSrcSubReg(0);
      continue;
    }

    // TODO: Remove once multiple srcs w/ coalescable copies are supported.
    if (!HandleMultipleSources)
      break;

    // Multiple sources, recurse into each source to find a new source
    // for it. Then, rewrite the PHI accordingly to its new edges.
    SmallVector<RegSubRegPair, 4> NewPHISrcs;
    for (unsigned i = 0; i < NumSrcs; ++i) {
      RegSubRegPair PHISrc(Res.getSrcReg(i), Res.getSrcSubReg(i));
      NewPHISrcs.push_back(
          getNewSource(MRI, TII, PHISrc, RewriteMap, HandleMultipleSources));
    }

    // Build the new PHI node and return its def register as the new source.
    MachineInstr &OrigPHI = const_cast<MachineInstr &>(*Res.getInst());
    MachineInstr &NewPHI = insertPHI(*MRI, *TII, NewPHISrcs, OrigPHI);
    LLVM_DEBUG(dbgs() << "-- getNewSource\n");
    LLVM_DEBUG(dbgs() << "   Replacing: " << OrigPHI);
    LLVM_DEBUG(dbgs() << "        With: " << NewPHI);
    const MachineOperand &MODef = NewPHI.getOperand(0);
    return RegSubRegPair(MODef.getReg(), MODef.getSubReg());
  }

  return RegSubRegPair(0, 0);
}

/// Optimize generic copy instructions to avoid cross register bank copy.
/// The optimization looks through a chain of copies and tries to find a source
/// that has a compatible register class.
/// Two register classes are considered to be compatible if they share the same
/// register bank.
/// New copies issued by this optimization are register allocator
/// friendly. This optimization does not remove any copy as it may
/// overconstrain the register allocator, but replaces some operands
/// when possible.
/// \pre isCoalescableCopy(*MI) is true.
/// \return True, when \p MI has been rewritten. False otherwise.
bool PeepholeOptimizer::optimizeCoalescableCopy(MachineInstr &MI) {
  assert(isCoalescableCopy(MI) && "Invalid argument");
  assert(MI.getDesc().getNumDefs() == 1 &&
         "Coalescer can understand multiple defs?!");
  const MachineOperand &MODef = MI.getOperand(0);
  // Do not rewrite physical definitions.
  if (TargetRegisterInfo::isPhysicalRegister(MODef.getReg()))
    return false;

  bool Changed = false;
  // Get the right rewriter for the current copy.
  std::unique_ptr<Rewriter> CpyRewriter(getCopyRewriter(MI, *TII));
  // If none exists, bail out.
  if (!CpyRewriter)
    return false;
  // Rewrite each rewritable source.
  RegSubRegPair Src;
  RegSubRegPair TrackPair;
  while (CpyRewriter->getNextRewritableSource(Src, TrackPair)) {
    // Keep track of PHI nodes and its incoming edges when looking for sources.
    RewriteMapTy RewriteMap;
    // Try to find a more suitable source. If we failed to do so, or get the
    // actual source, move to the next source.
    if (!findNextSource(TrackPair, RewriteMap))
      continue;

    // Get the new source to rewrite. TODO: Only enable handling of multiple
    // sources (PHIs) once we have a motivating example and testcases for it.
    RegSubRegPair NewSrc = getNewSource(MRI, TII, TrackPair, RewriteMap,
                                        /*HandleMultipleSources=*/false);
    if (Src.Reg == NewSrc.Reg || NewSrc.Reg == 0)
      continue;

    // Rewrite source.
    if (CpyRewriter->RewriteCurrentSource(NewSrc.Reg, NewSrc.SubReg)) {
      // We may have extended the live-range of NewSrc, account for that.
      MRI->clearKillFlags(NewSrc.Reg);
      Changed = true;
    }
  }
  // TODO: We could have a clean-up method to tidy the instruction.
  // E.g., v0 = INSERT_SUBREG v1, v1.sub0, sub0
  // => v0 = COPY v1
  // Currently we haven't seen motivating example for that and we
  // want to avoid untested code.
  NumRewrittenCopies += Changed;
  return Changed;
}

/// Rewrite the source found through \p Def, by using the \p RewriteMap
/// and create a new COPY instruction. More info about RewriteMap in
/// PeepholeOptimizer::findNextSource. Right now this is only used to handle
/// Uncoalescable copies, since they are copy like instructions that aren't
/// recognized by the register allocator.
MachineInstr &
PeepholeOptimizer::rewriteSource(MachineInstr &CopyLike,
                                 RegSubRegPair Def, RewriteMapTy &RewriteMap) {
  assert(!TargetRegisterInfo::isPhysicalRegister(Def.Reg) &&
         "We do not rewrite physical registers");

  // Find the new source to use in the COPY rewrite.
  RegSubRegPair NewSrc = getNewSource(MRI, TII, Def, RewriteMap);

  // Insert the COPY.
  const TargetRegisterClass *DefRC = MRI->getRegClass(Def.Reg);
  unsigned NewVReg = MRI->createVirtualRegister(DefRC);

  MachineInstr *NewCopy =
      BuildMI(*CopyLike.getParent(), &CopyLike, CopyLike.getDebugLoc(),
              TII->get(TargetOpcode::COPY), NewVReg)
          .addReg(NewSrc.Reg, 0, NewSrc.SubReg);

  if (Def.SubReg) {
    NewCopy->getOperand(0).setSubReg(Def.SubReg);
    NewCopy->getOperand(0).setIsUndef();
  }

  LLVM_DEBUG(dbgs() << "-- RewriteSource\n");
  LLVM_DEBUG(dbgs() << "   Replacing: " << CopyLike);
  LLVM_DEBUG(dbgs() << "        With: " << *NewCopy);
  MRI->replaceRegWith(Def.Reg, NewVReg);
  MRI->clearKillFlags(NewVReg);

  // We extended the lifetime of NewSrc.Reg, clear the kill flags to
  // account for that.
  MRI->clearKillFlags(NewSrc.Reg);

  return *NewCopy;
}

/// Optimize copy-like instructions to create
/// register coalescer friendly instruction.
/// The optimization tries to kill-off the \p MI by looking
/// through a chain of copies to find a source that has a compatible
/// register class.
/// If such a source is found, it replace \p MI by a generic COPY
/// operation.
/// \pre isUncoalescableCopy(*MI) is true.
/// \return True, when \p MI has been optimized. In that case, \p MI has
/// been removed from its parent.
/// All COPY instructions created, are inserted in \p LocalMIs.
bool PeepholeOptimizer::optimizeUncoalescableCopy(
    MachineInstr &MI, SmallPtrSetImpl<MachineInstr *> &LocalMIs) {
  assert(isUncoalescableCopy(MI) && "Invalid argument");
  UncoalescableRewriter CpyRewriter(MI);

  // Rewrite each rewritable source by generating new COPYs. This works
  // differently from optimizeCoalescableCopy since it first makes sure that all
  // definitions can be rewritten.
  RewriteMapTy RewriteMap;
  RegSubRegPair Src;
  RegSubRegPair Def;
  SmallVector<RegSubRegPair, 4> RewritePairs;
  while (CpyRewriter.getNextRewritableSource(Src, Def)) {
    // If a physical register is here, this is probably for a good reason.
    // Do not rewrite that.
    if (TargetRegisterInfo::isPhysicalRegister(Def.Reg))
      return false;

    // If we do not know how to rewrite this definition, there is no point
    // in trying to kill this instruction.
    if (!findNextSource(Def, RewriteMap))
      return false;

    RewritePairs.push_back(Def);
  }

  // The change is possible for all defs, do it.
  for (const RegSubRegPair &Def : RewritePairs) {
    // Rewrite the "copy" in a way the register coalescer understands.
    MachineInstr &NewCopy = rewriteSource(MI, Def, RewriteMap);
    LocalMIs.insert(&NewCopy);
  }

  // MI is now dead.
  MI.eraseFromParent();
  ++NumUncoalescableCopies;
  return true;
}

/// Check whether MI is a candidate for folding into a later instruction.
/// We only fold loads to virtual registers and the virtual register defined
/// has a single use.
bool PeepholeOptimizer::isLoadFoldable(
    MachineInstr &MI, SmallSet<unsigned, 16> &FoldAsLoadDefCandidates) {
  if (!MI.canFoldAsLoad() || !MI.mayLoad())
    return false;
  const MCInstrDesc &MCID = MI.getDesc();
  if (MCID.getNumDefs() != 1)
    return false;

  unsigned Reg = MI.getOperand(0).getReg();
  // To reduce compilation time, we check MRI->hasOneNonDBGUse when inserting
  // loads. It should be checked when processing uses of the load, since
  // uses can be removed during peephole.
  if (!MI.getOperand(0).getSubReg() &&
      TargetRegisterInfo::isVirtualRegister(Reg) &&
      MRI->hasOneNonDBGUse(Reg)) {
    FoldAsLoadDefCandidates.insert(Reg);
    return true;
  }
  return false;
}

bool PeepholeOptimizer::isMoveImmediate(
    MachineInstr &MI, SmallSet<unsigned, 4> &ImmDefRegs,
    DenseMap<unsigned, MachineInstr *> &ImmDefMIs) {
  const MCInstrDesc &MCID = MI.getDesc();
  if (!MI.isMoveImmediate())
    return false;
  if (MCID.getNumDefs() != 1)
    return false;
  unsigned Reg = MI.getOperand(0).getReg();
  if (TargetRegisterInfo::isVirtualRegister(Reg)) {
    ImmDefMIs.insert(std::make_pair(Reg, &MI));
    ImmDefRegs.insert(Reg);
    return true;
  }

  return false;
}

/// Try folding register operands that are defined by move immediate
/// instructions, i.e. a trivial constant folding optimization, if
/// and only if the def and use are in the same BB.
bool PeepholeOptimizer::foldImmediate(MachineInstr &MI,
    SmallSet<unsigned, 4> &ImmDefRegs,
    DenseMap<unsigned, MachineInstr *> &ImmDefMIs) {
  for (unsigned i = 0, e = MI.getDesc().getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || MO.isDef())
      continue;
    // Ignore dead implicit defs.
    if (MO.isImplicit() && MO.isDead())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    if (ImmDefRegs.count(Reg) == 0)
      continue;
    DenseMap<unsigned, MachineInstr*>::iterator II = ImmDefMIs.find(Reg);
    assert(II != ImmDefMIs.end() && "couldn't find immediate definition");
    if (TII->FoldImmediate(MI, *II->second, Reg, MRI)) {
      ++NumImmFold;
      return true;
    }
  }
  return false;
}

// FIXME: This is very simple and misses some cases which should be handled when
// motivating examples are found.
//
// The copy rewriting logic should look at uses as well as defs and be able to
// eliminate copies across blocks.
//
// Later copies that are subregister extracts will also not be eliminated since
// only the first copy is considered.
//
// e.g.
// %1 = COPY %0
// %2 = COPY %0:sub1
//
// Should replace %2 uses with %1:sub1
bool PeepholeOptimizer::foldRedundantCopy(MachineInstr &MI,
    SmallSet<unsigned, 4> &CopySrcRegs,
    DenseMap<unsigned, MachineInstr *> &CopyMIs) {
  assert(MI.isCopy() && "expected a COPY machine instruction");

  unsigned SrcReg = MI.getOperand(1).getReg();
  if (!TargetRegisterInfo::isVirtualRegister(SrcReg))
    return false;

  unsigned DstReg = MI.getOperand(0).getReg();
  if (!TargetRegisterInfo::isVirtualRegister(DstReg))
    return false;

  if (CopySrcRegs.insert(SrcReg).second) {
    // First copy of this reg seen.
    CopyMIs.insert(std::make_pair(SrcReg, &MI));
    return false;
  }

  MachineInstr *PrevCopy = CopyMIs.find(SrcReg)->second;

  unsigned SrcSubReg = MI.getOperand(1).getSubReg();
  unsigned PrevSrcSubReg = PrevCopy->getOperand(1).getSubReg();

  // Can't replace different subregister extracts.
  if (SrcSubReg != PrevSrcSubReg)
    return false;

  unsigned PrevDstReg = PrevCopy->getOperand(0).getReg();

  // Only replace if the copy register class is the same.
  //
  // TODO: If we have multiple copies to different register classes, we may want
  // to track multiple copies of the same source register.
  if (MRI->getRegClass(DstReg) != MRI->getRegClass(PrevDstReg))
    return false;

  MRI->replaceRegWith(DstReg, PrevDstReg);

  // Lifetime of the previous copy has been extended.
  MRI->clearKillFlags(PrevDstReg);
  return true;
}

bool PeepholeOptimizer::isNAPhysCopy(unsigned Reg) {
  return TargetRegisterInfo::isPhysicalRegister(Reg) &&
         !MRI->isAllocatable(Reg);
}

bool PeepholeOptimizer::foldRedundantNAPhysCopy(
    MachineInstr &MI, DenseMap<unsigned, MachineInstr *> &NAPhysToVirtMIs) {
  assert(MI.isCopy() && "expected a COPY machine instruction");

  if (DisableNAPhysCopyOpt)
    return false;

  unsigned DstReg = MI.getOperand(0).getReg();
  unsigned SrcReg = MI.getOperand(1).getReg();
  if (isNAPhysCopy(SrcReg) && TargetRegisterInfo::isVirtualRegister(DstReg)) {
    // %vreg = COPY %physreg
    // Avoid using a datastructure which can track multiple live non-allocatable
    // phys->virt copies since LLVM doesn't seem to do this.
    NAPhysToVirtMIs.insert({SrcReg, &MI});
    return false;
  }

  if (!(TargetRegisterInfo::isVirtualRegister(SrcReg) && isNAPhysCopy(DstReg)))
    return false;

  // %physreg = COPY %vreg
  auto PrevCopy = NAPhysToVirtMIs.find(DstReg);
  if (PrevCopy == NAPhysToVirtMIs.end()) {
    // We can't remove the copy: there was an intervening clobber of the
    // non-allocatable physical register after the copy to virtual.
    LLVM_DEBUG(dbgs() << "NAPhysCopy: intervening clobber forbids erasing "
                      << MI);
    return false;
  }

  unsigned PrevDstReg = PrevCopy->second->getOperand(0).getReg();
  if (PrevDstReg == SrcReg) {
    // Remove the virt->phys copy: we saw the virtual register definition, and
    // the non-allocatable physical register's state hasn't changed since then.
    LLVM_DEBUG(dbgs() << "NAPhysCopy: erasing " << MI);
    ++NumNAPhysCopies;
    return true;
  }

  // Potential missed optimization opportunity: we saw a different virtual
  // register get a copy of the non-allocatable physical register, and we only
  // track one such copy. Avoid getting confused by this new non-allocatable
  // physical register definition, and remove it from the tracked copies.
  LLVM_DEBUG(dbgs() << "NAPhysCopy: missed opportunity " << MI);
  NAPhysToVirtMIs.erase(PrevCopy);
  return false;
}

/// \bried Returns true if \p MO is a virtual register operand.
static bool isVirtualRegisterOperand(MachineOperand &MO) {
  if (!MO.isReg())
    return false;
  return TargetRegisterInfo::isVirtualRegister(MO.getReg());
}

bool PeepholeOptimizer::findTargetRecurrence(
    unsigned Reg, const SmallSet<unsigned, 2> &TargetRegs,
    RecurrenceCycle &RC) {
  // Recurrence found if Reg is in TargetRegs.
  if (TargetRegs.count(Reg))
    return true;

  // TODO: Curerntly, we only allow the last instruction of the recurrence
  // cycle (the instruction that feeds the PHI instruction) to have more than
  // one uses to guarantee that commuting operands does not tie registers
  // with overlapping live range. Once we have actual live range info of
  // each register, this constraint can be relaxed.
  if (!MRI->hasOneNonDBGUse(Reg))
    return false;

  // Give up if the reccurrence chain length is longer than the limit.
  if (RC.size() >= MaxRecurrenceChain)
    return false;

  MachineInstr &MI = *(MRI->use_instr_nodbg_begin(Reg));
  unsigned Idx = MI.findRegisterUseOperandIdx(Reg);

  // Only interested in recurrences whose instructions have only one def, which
  // is a virtual register.
  if (MI.getDesc().getNumDefs() != 1)
    return false;

  MachineOperand &DefOp = MI.getOperand(0);
  if (!isVirtualRegisterOperand(DefOp))
    return false;

  // Check if def operand of MI is tied to any use operand. We are only
  // interested in the case that all the instructions in the recurrence chain
  // have there def operand tied with one of the use operand.
  unsigned TiedUseIdx;
  if (!MI.isRegTiedToUseOperand(0, &TiedUseIdx))
    return false;

  if (Idx == TiedUseIdx) {
    RC.push_back(RecurrenceInstr(&MI));
    return findTargetRecurrence(DefOp.getReg(), TargetRegs, RC);
  } else {
    // If Idx is not TiedUseIdx, check if Idx is commutable with TiedUseIdx.
    unsigned CommIdx = TargetInstrInfo::CommuteAnyOperandIndex;
    if (TII->findCommutedOpIndices(MI, Idx, CommIdx) && CommIdx == TiedUseIdx) {
      RC.push_back(RecurrenceInstr(&MI, Idx, CommIdx));
      return findTargetRecurrence(DefOp.getReg(), TargetRegs, RC);
    }
  }

  return false;
}

/// Phi instructions will eventually be lowered to copy instructions.
/// If phi is in a loop header, a recurrence may formulated around the source
/// and destination of the phi. For such case commuting operands of the
/// instructions in the recurrence may enable coalescing of the copy instruction
/// generated from the phi. For example, if there is a recurrence of
///
/// LoopHeader:
///   %1 = phi(%0, %100)
/// LoopLatch:
///   %0<def, tied1> = ADD %2<def, tied0>, %1
///
/// , the fact that %0 and %2 are in the same tied operands set makes
/// the coalescing of copy instruction generated from the phi in
/// LoopHeader(i.e. %1 = COPY %0) impossible, because %1 and
/// %2 have overlapping live range. This introduces additional move
/// instruction to the final assembly. However, if we commute %2 and
/// %1 of ADD instruction, the redundant move instruction can be
/// avoided.
bool PeepholeOptimizer::optimizeRecurrence(MachineInstr &PHI) {
  SmallSet<unsigned, 2> TargetRegs;
  for (unsigned Idx = 1; Idx < PHI.getNumOperands(); Idx += 2) {
    MachineOperand &MO = PHI.getOperand(Idx);
    assert(isVirtualRegisterOperand(MO) && "Invalid PHI instruction");
    TargetRegs.insert(MO.getReg());
  }

  bool Changed = false;
  RecurrenceCycle RC;
  if (findTargetRecurrence(PHI.getOperand(0).getReg(), TargetRegs, RC)) {
    // Commutes operands of instructions in RC if necessary so that the copy to
    // be generated from PHI can be coalesced.
    LLVM_DEBUG(dbgs() << "Optimize recurrence chain from " << PHI);
    for (auto &RI : RC) {
      LLVM_DEBUG(dbgs() << "\tInst: " << *(RI.getMI()));
      auto CP = RI.getCommutePair();
      if (CP) {
        Changed = true;
        TII->commuteInstruction(*(RI.getMI()), false, (*CP).first,
                                (*CP).second);
        LLVM_DEBUG(dbgs() << "\t\tCommuted: " << *(RI.getMI()));
      }
    }
  }

  return Changed;
}

bool PeepholeOptimizer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "********** PEEPHOLE OPTIMIZER **********\n");
  LLVM_DEBUG(dbgs() << "********** Function: " << MF.getName() << '\n');

  if (DisablePeephole)
    return false;

  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  DT  = Aggressive ? &getAnalysis<MachineDominatorTree>() : nullptr;
  MLI = &getAnalysis<MachineLoopInfo>();

  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    bool SeenMoveImm = false;

    // During this forward scan, at some point it needs to answer the question
    // "given a pointer to an MI in the current BB, is it located before or
    // after the current instruction".
    // To perform this, the following set keeps track of the MIs already seen
    // during the scan, if a MI is not in the set, it is assumed to be located
    // after. Newly created MIs have to be inserted in the set as well.
    SmallPtrSet<MachineInstr*, 16> LocalMIs;
    SmallSet<unsigned, 4> ImmDefRegs;
    DenseMap<unsigned, MachineInstr*> ImmDefMIs;
    SmallSet<unsigned, 16> FoldAsLoadDefCandidates;

    // Track when a non-allocatable physical register is copied to a virtual
    // register so that useless moves can be removed.
    //
    // %physreg is the map index; MI is the last valid `%vreg = COPY %physreg`
    // without any intervening re-definition of %physreg.
    DenseMap<unsigned, MachineInstr *> NAPhysToVirtMIs;

    // Set of virtual registers that are copied from.
    SmallSet<unsigned, 4> CopySrcRegs;
    DenseMap<unsigned, MachineInstr *> CopySrcMIs;

    bool IsLoopHeader = MLI->isLoopHeader(&MBB);

    for (MachineBasicBlock::iterator MII = MBB.begin(), MIE = MBB.end();
         MII != MIE; ) {
      MachineInstr *MI = &*MII;
      // We may be erasing MI below, increment MII now.
      ++MII;
      LocalMIs.insert(MI);

      // Skip debug instructions. They should not affect this peephole optimization.
      if (MI->isDebugInstr())
          continue;

      if (MI->isPosition())
        continue;

      if (IsLoopHeader && MI->isPHI()) {
        if (optimizeRecurrence(*MI)) {
          Changed = true;
          continue;
        }
      }

      if (!MI->isCopy()) {
        for (const MachineOperand &MO : MI->operands()) {
          // Visit all operands: definitions can be implicit or explicit.
          if (MO.isReg()) {
            unsigned Reg = MO.getReg();
            if (MO.isDef() && isNAPhysCopy(Reg)) {
              const auto &Def = NAPhysToVirtMIs.find(Reg);
              if (Def != NAPhysToVirtMIs.end()) {
                // A new definition of the non-allocatable physical register
                // invalidates previous copies.
                LLVM_DEBUG(dbgs()
                           << "NAPhysCopy: invalidating because of " << *MI);
                NAPhysToVirtMIs.erase(Def);
              }
            }
          } else if (MO.isRegMask()) {
            const uint32_t *RegMask = MO.getRegMask();
            for (auto &RegMI : NAPhysToVirtMIs) {
              unsigned Def = RegMI.first;
              if (MachineOperand::clobbersPhysReg(RegMask, Def)) {
                LLVM_DEBUG(dbgs()
                           << "NAPhysCopy: invalidating because of " << *MI);
                NAPhysToVirtMIs.erase(Def);
              }
            }
          }
        }
      }

      if (MI->isImplicitDef() || MI->isKill())
        continue;

      if (MI->isInlineAsm() || MI->hasUnmodeledSideEffects()) {
        // Blow away all non-allocatable physical registers knowledge since we
        // don't know what's correct anymore.
        //
        // FIXME: handle explicit asm clobbers.
        LLVM_DEBUG(dbgs() << "NAPhysCopy: blowing away all info due to "
                          << *MI);
        NAPhysToVirtMIs.clear();
      }

      if ((isUncoalescableCopy(*MI) &&
           optimizeUncoalescableCopy(*MI, LocalMIs)) ||
          (MI->isCompare() && optimizeCmpInstr(*MI)) ||
          (MI->isSelect() && optimizeSelect(*MI, LocalMIs))) {
        // MI is deleted.
        LocalMIs.erase(MI);
        Changed = true;
        continue;
      }

      if (MI->isConditionalBranch() && optimizeCondBranch(*MI)) {
        Changed = true;
        continue;
      }

      if (isCoalescableCopy(*MI) && optimizeCoalescableCopy(*MI)) {
        // MI is just rewritten.
        Changed = true;
        continue;
      }

      if (MI->isCopy() &&
          (foldRedundantCopy(*MI, CopySrcRegs, CopySrcMIs) ||
           foldRedundantNAPhysCopy(*MI, NAPhysToVirtMIs))) {
        LocalMIs.erase(MI);
        MI->eraseFromParent();
        Changed = true;
        continue;
      }

      if (isMoveImmediate(*MI, ImmDefRegs, ImmDefMIs)) {
        SeenMoveImm = true;
      } else {
        Changed |= optimizeExtInstr(*MI, MBB, LocalMIs);
        // optimizeExtInstr might have created new instructions after MI
        // and before the already incremented MII. Adjust MII so that the
        // next iteration sees the new instructions.
        MII = MI;
        ++MII;
        if (SeenMoveImm)
          Changed |= foldImmediate(*MI, ImmDefRegs, ImmDefMIs);
      }

      // Check whether MI is a load candidate for folding into a later
      // instruction. If MI is not a candidate, check whether we can fold an
      // earlier load into MI.
      if (!isLoadFoldable(*MI, FoldAsLoadDefCandidates) &&
          !FoldAsLoadDefCandidates.empty()) {

        // We visit each operand even after successfully folding a previous
        // one.  This allows us to fold multiple loads into a single
        // instruction.  We do assume that optimizeLoadInstr doesn't insert
        // foldable uses earlier in the argument list.  Since we don't restart
        // iteration, we'd miss such cases.
        const MCInstrDesc &MIDesc = MI->getDesc();
        for (unsigned i = MIDesc.getNumDefs(); i != MI->getNumOperands();
             ++i) {
          const MachineOperand &MOp = MI->getOperand(i);
          if (!MOp.isReg())
            continue;
          unsigned FoldAsLoadDefReg = MOp.getReg();
          if (FoldAsLoadDefCandidates.count(FoldAsLoadDefReg)) {
            // We need to fold load after optimizeCmpInstr, since
            // optimizeCmpInstr can enable folding by converting SUB to CMP.
            // Save FoldAsLoadDefReg because optimizeLoadInstr() resets it and
            // we need it for markUsesInDebugValueAsUndef().
            unsigned FoldedReg = FoldAsLoadDefReg;
            MachineInstr *DefMI = nullptr;
            if (MachineInstr *FoldMI =
                    TII->optimizeLoadInstr(*MI, MRI, FoldAsLoadDefReg, DefMI)) {
              // Update LocalMIs since we replaced MI with FoldMI and deleted
              // DefMI.
              LLVM_DEBUG(dbgs() << "Replacing: " << *MI);
              LLVM_DEBUG(dbgs() << "     With: " << *FoldMI);
              LocalMIs.erase(MI);
              LocalMIs.erase(DefMI);
              LocalMIs.insert(FoldMI);
              MI->eraseFromParent();
              DefMI->eraseFromParent();
              MRI->markUsesInDebugValueAsUndef(FoldedReg);
              FoldAsLoadDefCandidates.erase(FoldedReg);
              ++NumLoadFold;

              // MI is replaced with FoldMI so we can continue trying to fold
              Changed = true;
              MI = FoldMI;
            }
          }
        }
      }

      // If we run into an instruction we can't fold across, discard
      // the load candidates.  Note: We might be able to fold *into* this
      // instruction, so this needs to be after the folding logic.
      if (MI->isLoadFoldBarrier()) {
        LLVM_DEBUG(dbgs() << "Encountered load fold barrier on " << *MI);
        FoldAsLoadDefCandidates.clear();
      }
    }
  }

  return Changed;
}

ValueTrackerResult ValueTracker::getNextSourceFromCopy() {
  assert(Def->isCopy() && "Invalid definition");
  // Copy instruction are supposed to be: Def = Src.
  // If someone breaks this assumption, bad things will happen everywhere.
  assert(Def->getNumOperands() == 2 && "Invalid number of operands");

  if (Def->getOperand(DefIdx).getSubReg() != DefSubReg)
    // If we look for a different subreg, it means we want a subreg of src.
    // Bails as we do not support composing subregs yet.
    return ValueTrackerResult();
  // Otherwise, we want the whole source.
  const MachineOperand &Src = Def->getOperand(1);
  if (Src.isUndef())
    return ValueTrackerResult();
  return ValueTrackerResult(Src.getReg(), Src.getSubReg());
}

ValueTrackerResult ValueTracker::getNextSourceFromBitcast() {
  assert(Def->isBitcast() && "Invalid definition");

  // Bail if there are effects that a plain copy will not expose.
  if (Def->hasUnmodeledSideEffects())
    return ValueTrackerResult();

  // Bitcasts with more than one def are not supported.
  if (Def->getDesc().getNumDefs() != 1)
    return ValueTrackerResult();
  const MachineOperand DefOp = Def->getOperand(DefIdx);
  if (DefOp.getSubReg() != DefSubReg)
    // If we look for a different subreg, it means we want a subreg of the src.
    // Bails as we do not support composing subregs yet.
    return ValueTrackerResult();

  unsigned SrcIdx = Def->getNumOperands();
  for (unsigned OpIdx = DefIdx + 1, EndOpIdx = SrcIdx; OpIdx != EndOpIdx;
       ++OpIdx) {
    const MachineOperand &MO = Def->getOperand(OpIdx);
    if (!MO.isReg() || !MO.getReg())
      continue;
    // Ignore dead implicit defs.
    if (MO.isImplicit() && MO.isDead())
      continue;
    assert(!MO.isDef() && "We should have skipped all the definitions by now");
    if (SrcIdx != EndOpIdx)
      // Multiple sources?
      return ValueTrackerResult();
    SrcIdx = OpIdx;
  }

  // Stop when any user of the bitcast is a SUBREG_TO_REG, replacing with a COPY
  // will break the assumed guarantees for the upper bits.
  for (const MachineInstr &UseMI : MRI.use_nodbg_instructions(DefOp.getReg())) {
    if (UseMI.isSubregToReg())
      return ValueTrackerResult();
  }

  const MachineOperand &Src = Def->getOperand(SrcIdx);
  if (Src.isUndef())
    return ValueTrackerResult();
  return ValueTrackerResult(Src.getReg(), Src.getSubReg());
}

ValueTrackerResult ValueTracker::getNextSourceFromRegSequence() {
  assert((Def->isRegSequence() || Def->isRegSequenceLike()) &&
         "Invalid definition");

  if (Def->getOperand(DefIdx).getSubReg())
    // If we are composing subregs, bail out.
    // The case we are checking is Def.<subreg> = REG_SEQUENCE.
    // This should almost never happen as the SSA property is tracked at
    // the register level (as opposed to the subreg level).
    // I.e.,
    // Def.sub0 =
    // Def.sub1 =
    // is a valid SSA representation for Def.sub0 and Def.sub1, but not for
    // Def. Thus, it must not be generated.
    // However, some code could theoretically generates a single
    // Def.sub0 (i.e, not defining the other subregs) and we would
    // have this case.
    // If we can ascertain (or force) that this never happens, we could
    // turn that into an assertion.
    return ValueTrackerResult();

  if (!TII)
    // We could handle the REG_SEQUENCE here, but we do not want to
    // duplicate the code from the generic TII.
    return ValueTrackerResult();

  SmallVector<RegSubRegPairAndIdx, 8> RegSeqInputRegs;
  if (!TII->getRegSequenceInputs(*Def, DefIdx, RegSeqInputRegs))
    return ValueTrackerResult();

  // We are looking at:
  // Def = REG_SEQUENCE v0, sub0, v1, sub1, ...
  // Check if one of the operand defines the subreg we are interested in.
  for (const RegSubRegPairAndIdx &RegSeqInput : RegSeqInputRegs) {
    if (RegSeqInput.SubIdx == DefSubReg) {
      if (RegSeqInput.SubReg)
        // Bail if we have to compose sub registers.
        return ValueTrackerResult();

      return ValueTrackerResult(RegSeqInput.Reg, RegSeqInput.SubReg);
    }
  }

  // If the subreg we are tracking is super-defined by another subreg,
  // we could follow this value. However, this would require to compose
  // the subreg and we do not do that for now.
  return ValueTrackerResult();
}

ValueTrackerResult ValueTracker::getNextSourceFromInsertSubreg() {
  assert((Def->isInsertSubreg() || Def->isInsertSubregLike()) &&
         "Invalid definition");

  if (Def->getOperand(DefIdx).getSubReg())
    // If we are composing subreg, bail out.
    // Same remark as getNextSourceFromRegSequence.
    // I.e., this may be turned into an assert.
    return ValueTrackerResult();

  if (!TII)
    // We could handle the REG_SEQUENCE here, but we do not want to
    // duplicate the code from the generic TII.
    return ValueTrackerResult();

  RegSubRegPair BaseReg;
  RegSubRegPairAndIdx InsertedReg;
  if (!TII->getInsertSubregInputs(*Def, DefIdx, BaseReg, InsertedReg))
    return ValueTrackerResult();

  // We are looking at:
  // Def = INSERT_SUBREG v0, v1, sub1
  // There are two cases:
  // 1. DefSubReg == sub1, get v1.
  // 2. DefSubReg != sub1, the value may be available through v0.

  // #1 Check if the inserted register matches the required sub index.
  if (InsertedReg.SubIdx == DefSubReg) {
    return ValueTrackerResult(InsertedReg.Reg, InsertedReg.SubReg);
  }
  // #2 Otherwise, if the sub register we are looking for is not partial
  // defined by the inserted element, we can look through the main
  // register (v0).
  const MachineOperand &MODef = Def->getOperand(DefIdx);
  // If the result register (Def) and the base register (v0) do not
  // have the same register class or if we have to compose
  // subregisters, bail out.
  if (MRI.getRegClass(MODef.getReg()) != MRI.getRegClass(BaseReg.Reg) ||
      BaseReg.SubReg)
    return ValueTrackerResult();

  // Get the TRI and check if the inserted sub-register overlaps with the
  // sub-register we are tracking.
  const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
  if (!TRI ||
      !(TRI->getSubRegIndexLaneMask(DefSubReg) &
        TRI->getSubRegIndexLaneMask(InsertedReg.SubIdx)).none())
    return ValueTrackerResult();
  // At this point, the value is available in v0 via the same subreg
  // we used for Def.
  return ValueTrackerResult(BaseReg.Reg, DefSubReg);
}

ValueTrackerResult ValueTracker::getNextSourceFromExtractSubreg() {
  assert((Def->isExtractSubreg() ||
          Def->isExtractSubregLike()) && "Invalid definition");
  // We are looking at:
  // Def = EXTRACT_SUBREG v0, sub0

  // Bail if we have to compose sub registers.
  // Indeed, if DefSubReg != 0, we would have to compose it with sub0.
  if (DefSubReg)
    return ValueTrackerResult();

  if (!TII)
    // We could handle the EXTRACT_SUBREG here, but we do not want to
    // duplicate the code from the generic TII.
    return ValueTrackerResult();

  RegSubRegPairAndIdx ExtractSubregInputReg;
  if (!TII->getExtractSubregInputs(*Def, DefIdx, ExtractSubregInputReg))
    return ValueTrackerResult();

  // Bail if we have to compose sub registers.
  // Likewise, if v0.subreg != 0, we would have to compose v0.subreg with sub0.
  if (ExtractSubregInputReg.SubReg)
    return ValueTrackerResult();
  // Otherwise, the value is available in the v0.sub0.
  return ValueTrackerResult(ExtractSubregInputReg.Reg,
                            ExtractSubregInputReg.SubIdx);
}

ValueTrackerResult ValueTracker::getNextSourceFromSubregToReg() {
  assert(Def->isSubregToReg() && "Invalid definition");
  // We are looking at:
  // Def = SUBREG_TO_REG Imm, v0, sub0

  // Bail if we have to compose sub registers.
  // If DefSubReg != sub0, we would have to check that all the bits
  // we track are included in sub0 and if yes, we would have to
  // determine the right subreg in v0.
  if (DefSubReg != Def->getOperand(3).getImm())
    return ValueTrackerResult();
  // Bail if we have to compose sub registers.
  // Likewise, if v0.subreg != 0, we would have to compose it with sub0.
  if (Def->getOperand(2).getSubReg())
    return ValueTrackerResult();

  return ValueTrackerResult(Def->getOperand(2).getReg(),
                            Def->getOperand(3).getImm());
}

/// Explore each PHI incoming operand and return its sources.
ValueTrackerResult ValueTracker::getNextSourceFromPHI() {
  assert(Def->isPHI() && "Invalid definition");
  ValueTrackerResult Res;

  // If we look for a different subreg, bail as we do not support composing
  // subregs yet.
  if (Def->getOperand(0).getSubReg() != DefSubReg)
    return ValueTrackerResult();

  // Return all register sources for PHI instructions.
  for (unsigned i = 1, e = Def->getNumOperands(); i < e; i += 2) {
    const MachineOperand &MO = Def->getOperand(i);
    assert(MO.isReg() && "Invalid PHI instruction");
    // We have no code to deal with undef operands. They shouldn't happen in
    // normal programs anyway.
    if (MO.isUndef())
      return ValueTrackerResult();
    Res.addSource(MO.getReg(), MO.getSubReg());
  }

  return Res;
}

ValueTrackerResult ValueTracker::getNextSourceImpl() {
  assert(Def && "This method needs a valid definition");

  assert(((Def->getOperand(DefIdx).isDef() &&
           (DefIdx < Def->getDesc().getNumDefs() ||
            Def->getDesc().isVariadic())) ||
          Def->getOperand(DefIdx).isImplicit()) &&
         "Invalid DefIdx");
  if (Def->isCopy())
    return getNextSourceFromCopy();
  if (Def->isBitcast())
    return getNextSourceFromBitcast();
  // All the remaining cases involve "complex" instructions.
  // Bail if we did not ask for the advanced tracking.
  if (DisableAdvCopyOpt)
    return ValueTrackerResult();
  if (Def->isRegSequence() || Def->isRegSequenceLike())
    return getNextSourceFromRegSequence();
  if (Def->isInsertSubreg() || Def->isInsertSubregLike())
    return getNextSourceFromInsertSubreg();
  if (Def->isExtractSubreg() || Def->isExtractSubregLike())
    return getNextSourceFromExtractSubreg();
  if (Def->isSubregToReg())
    return getNextSourceFromSubregToReg();
  if (Def->isPHI())
    return getNextSourceFromPHI();
  return ValueTrackerResult();
}

ValueTrackerResult ValueTracker::getNextSource() {
  // If we reach a point where we cannot move up in the use-def chain,
  // there is nothing we can get.
  if (!Def)
    return ValueTrackerResult();

  ValueTrackerResult Res = getNextSourceImpl();
  if (Res.isValid()) {
    // Update definition, definition index, and subregister for the
    // next call of getNextSource.
    // Update the current register.
    bool OneRegSrc = Res.getNumSources() == 1;
    if (OneRegSrc)
      Reg = Res.getSrcReg(0);
    // Update the result before moving up in the use-def chain
    // with the instruction containing the last found sources.
    Res.setInst(Def);

    // If we can still move up in the use-def chain, move to the next
    // definition.
    if (!TargetRegisterInfo::isPhysicalRegister(Reg) && OneRegSrc) {
      MachineRegisterInfo::def_iterator DI = MRI.def_begin(Reg);
      if (DI != MRI.def_end()) {
        Def = DI->getParent();
        DefIdx = DI.getOperandNo();
        DefSubReg = Res.getSrcSubReg(0);
      } else {
        Def = nullptr;
      }
      return Res;
    }
  }
  // If we end up here, this means we will not be able to find another source
  // for the next iteration. Make sure any new call to getNextSource bails out
  // early by cutting the use-def chain.
  Def = nullptr;
  return Res;
}
