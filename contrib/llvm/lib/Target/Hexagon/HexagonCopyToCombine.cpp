//===------- HexagonCopyToCombine.cpp - Hexagon Copy-To-Combine Pass ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass replaces transfer instructions by combine instructions.
// We walk along a basic block and look for two combinable instructions and try
// to move them together. If we can move them next to each other we do so and
// replace them with a combine instruction.
//===----------------------------------------------------------------------===//
#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/PassSupport.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "hexagon-copy-combine"

static
cl::opt<bool> IsCombinesDisabled("disable-merge-into-combines",
                                 cl::Hidden, cl::ZeroOrMore,
                                 cl::init(false),
                                 cl::desc("Disable merging into combines"));
static
cl::opt<bool> IsConst64Disabled("disable-const64",
                                 cl::Hidden, cl::ZeroOrMore,
                                 cl::init(false),
                                 cl::desc("Disable generation of const64"));
static
cl::opt<unsigned>
MaxNumOfInstsBetweenNewValueStoreAndTFR("max-num-inst-between-tfr-and-nv-store",
                   cl::Hidden, cl::init(4),
                   cl::desc("Maximum distance between a tfr feeding a store we "
                            "consider the store still to be newifiable"));

namespace llvm {
  FunctionPass *createHexagonCopyToCombine();
  void initializeHexagonCopyToCombinePass(PassRegistry&);
}


namespace {

class HexagonCopyToCombine : public MachineFunctionPass  {
  const HexagonInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const HexagonSubtarget *ST;
  bool ShouldCombineAggressively;

  DenseSet<MachineInstr *> PotentiallyNewifiableTFR;
  SmallVector<MachineInstr *, 8> DbgMItoMove;

public:
  static char ID;

  HexagonCopyToCombine() : MachineFunctionPass(ID) {
    initializeHexagonCopyToCombinePass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return "Hexagon Copy-To-Combine Pass";
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  MachineInstr *findPairable(MachineInstr &I1, bool &DoInsertAtI1,
                             bool AllowC64);

  void findPotentialNewifiableTFRs(MachineBasicBlock &);

  void combine(MachineInstr &I1, MachineInstr &I2,
               MachineBasicBlock::iterator &MI, bool DoInsertAtI1,
               bool OptForSize);

  bool isSafeToMoveTogether(MachineInstr &I1, MachineInstr &I2,
                            unsigned I1DestReg, unsigned I2DestReg,
                            bool &DoInsertAtI1);

  void emitCombineRR(MachineBasicBlock::iterator &Before, unsigned DestReg,
                     MachineOperand &HiOperand, MachineOperand &LoOperand);

  void emitCombineRI(MachineBasicBlock::iterator &Before, unsigned DestReg,
                     MachineOperand &HiOperand, MachineOperand &LoOperand);

  void emitCombineIR(MachineBasicBlock::iterator &Before, unsigned DestReg,
                     MachineOperand &HiOperand, MachineOperand &LoOperand);

  void emitCombineII(MachineBasicBlock::iterator &Before, unsigned DestReg,
                     MachineOperand &HiOperand, MachineOperand &LoOperand);

  void emitConst64(MachineBasicBlock::iterator &Before, unsigned DestReg,
                   MachineOperand &HiOperand, MachineOperand &LoOperand);
};

} // End anonymous namespace.

char HexagonCopyToCombine::ID = 0;

INITIALIZE_PASS(HexagonCopyToCombine, "hexagon-copy-combine",
                "Hexagon Copy-To-Combine Pass", false, false)

static bool isCombinableInstType(MachineInstr &MI, const HexagonInstrInfo *TII,
                                 bool ShouldCombineAggressively) {
  switch (MI.getOpcode()) {
  case Hexagon::A2_tfr: {
    // A COPY instruction can be combined if its arguments are IntRegs (32bit).
    const MachineOperand &Op0 = MI.getOperand(0);
    const MachineOperand &Op1 = MI.getOperand(1);
    assert(Op0.isReg() && Op1.isReg());

    unsigned DestReg = Op0.getReg();
    unsigned SrcReg = Op1.getReg();
    return Hexagon::IntRegsRegClass.contains(DestReg) &&
           Hexagon::IntRegsRegClass.contains(SrcReg);
  }

  case Hexagon::A2_tfrsi: {
    // A transfer-immediate can be combined if its argument is a signed 8bit
    // value.
    const MachineOperand &Op0 = MI.getOperand(0);
    const MachineOperand &Op1 = MI.getOperand(1);
    assert(Op0.isReg());

    unsigned DestReg = Op0.getReg();
    // Ensure that TargetFlags are MO_NO_FLAG for a global. This is a
    // workaround for an ABI bug that prevents GOT relocations on combine
    // instructions
    if (!Op1.isImm() && Op1.getTargetFlags() != HexagonII::MO_NO_FLAG)
      return false;

    // Only combine constant extended A2_tfrsi if we are in aggressive mode.
    bool NotExt = Op1.isImm() && isInt<8>(Op1.getImm());
    return Hexagon::IntRegsRegClass.contains(DestReg) &&
           (ShouldCombineAggressively || NotExt);
  }

  case Hexagon::V6_vassign:
    return true;

  default:
    break;
  }

  return false;
}

template <unsigned N> static bool isGreaterThanNBitTFRI(const MachineInstr &I) {
  if (I.getOpcode() == Hexagon::TFRI64_V4 ||
      I.getOpcode() == Hexagon::A2_tfrsi) {
    const MachineOperand &Op = I.getOperand(1);
    return !Op.isImm() || !isInt<N>(Op.getImm());
  }
  return false;
}

/// areCombinableOperations - Returns true if the two instruction can be merge
/// into a combine (ignoring register constraints).
static bool areCombinableOperations(const TargetRegisterInfo *TRI,
                                    MachineInstr &HighRegInst,
                                    MachineInstr &LowRegInst, bool AllowC64) {
  unsigned HiOpc = HighRegInst.getOpcode();
  unsigned LoOpc = LowRegInst.getOpcode();

  auto verifyOpc = [](unsigned Opc) -> void {
    switch (Opc) {
      case Hexagon::A2_tfr:
      case Hexagon::A2_tfrsi:
      case Hexagon::V6_vassign:
        break;
      default:
        llvm_unreachable("Unexpected opcode");
    }
  };
  verifyOpc(HiOpc);
  verifyOpc(LoOpc);

  if (HiOpc == Hexagon::V6_vassign || LoOpc == Hexagon::V6_vassign)
    return HiOpc == LoOpc;

  if (!AllowC64) {
    // There is no combine of two constant extended values.
    if (isGreaterThanNBitTFRI<8>(HighRegInst) &&
        isGreaterThanNBitTFRI<6>(LowRegInst))
      return false;
  }

  // There is a combine of two constant extended values into CONST64,
  // provided both constants are true immediates.
  if (isGreaterThanNBitTFRI<16>(HighRegInst) &&
      isGreaterThanNBitTFRI<16>(LowRegInst))
    return (HighRegInst.getOperand(1).isImm() &&
            LowRegInst.getOperand(1).isImm());

  // There is no combine of two constant extended values, unless handled above
  // Make both 8-bit size checks to allow both combine (#,##) and combine(##,#)
  if (isGreaterThanNBitTFRI<8>(HighRegInst) &&
      isGreaterThanNBitTFRI<8>(LowRegInst))
    return false;

  return true;
}

static bool isEvenReg(unsigned Reg) {
  assert(TargetRegisterInfo::isPhysicalRegister(Reg));
  if (Hexagon::IntRegsRegClass.contains(Reg))
    return (Reg - Hexagon::R0) % 2 == 0;
  if (Hexagon::HvxVRRegClass.contains(Reg))
    return (Reg - Hexagon::V0) % 2 == 0;
  llvm_unreachable("Invalid register");
}

static void removeKillInfo(MachineInstr &MI, unsigned RegNotKilled) {
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    MachineOperand &Op = MI.getOperand(I);
    if (!Op.isReg() || Op.getReg() != RegNotKilled || !Op.isKill())
      continue;
    Op.setIsKill(false);
  }
}

/// Returns true if it is unsafe to move a copy instruction from \p UseReg to
/// \p DestReg over the instruction \p MI.
static bool isUnsafeToMoveAcross(MachineInstr &MI, unsigned UseReg,
                                 unsigned DestReg,
                                 const TargetRegisterInfo *TRI) {
  return (UseReg && (MI.modifiesRegister(UseReg, TRI))) ||
         MI.modifiesRegister(DestReg, TRI) || MI.readsRegister(DestReg, TRI) ||
         MI.hasUnmodeledSideEffects() || MI.isInlineAsm() ||
         MI.isMetaInstruction();
}

static unsigned UseReg(const MachineOperand& MO) {
  return MO.isReg() ? MO.getReg() : 0;
}

/// isSafeToMoveTogether - Returns true if it is safe to move I1 next to I2 such
/// that the two instructions can be paired in a combine.
bool HexagonCopyToCombine::isSafeToMoveTogether(MachineInstr &I1,
                                                MachineInstr &I2,
                                                unsigned I1DestReg,
                                                unsigned I2DestReg,
                                                bool &DoInsertAtI1) {
  unsigned I2UseReg = UseReg(I2.getOperand(1));

  // It is not safe to move I1 and I2 into one combine if I2 has a true
  // dependence on I1.
  if (I2UseReg && I1.modifiesRegister(I2UseReg, TRI))
    return false;

  bool isSafe = true;

  // First try to move I2 towards I1.
  {
    // A reverse_iterator instantiated like below starts before I2, and I1
    // respectively.
    // Look at instructions I in between I2 and (excluding) I1.
    MachineBasicBlock::reverse_iterator I(I2),
      End = --(MachineBasicBlock::reverse_iterator(I1));
    // At 03 we got better results (dhrystone!) by being more conservative.
    if (!ShouldCombineAggressively)
      End = MachineBasicBlock::reverse_iterator(I1);
    // If I2 kills its operand and we move I2 over an instruction that also
    // uses I2's use reg we need to modify that (first) instruction to now kill
    // this reg.
    unsigned KilledOperand = 0;
    if (I2.killsRegister(I2UseReg))
      KilledOperand = I2UseReg;
    MachineInstr *KillingInstr = nullptr;

    for (; I != End; ++I) {
      // If the intervening instruction I:
      //   * modifies I2's use reg
      //   * modifies I2's def reg
      //   * reads I2's def reg
      //   * or has unmodelled side effects
      // we can't move I2 across it.
      if (I->isDebugInstr())
        continue;

      if (isUnsafeToMoveAcross(*I, I2UseReg, I2DestReg, TRI)) {
        isSafe = false;
        break;
      }

      // Update first use of the killed operand.
      if (!KillingInstr && KilledOperand &&
          I->readsRegister(KilledOperand, TRI))
        KillingInstr = &*I;
    }
    if (isSafe) {
      // Update the intermediate instruction to with the kill flag.
      if (KillingInstr) {
        bool Added = KillingInstr->addRegisterKilled(KilledOperand, TRI, true);
        (void)Added; // suppress compiler warning
        assert(Added && "Must successfully update kill flag");
        removeKillInfo(I2, KilledOperand);
      }
      DoInsertAtI1 = true;
      return true;
    }
  }

  // Try to move I1 towards I2.
  {
    // Look at instructions I in between I1 and (excluding) I2.
    MachineBasicBlock::iterator I(I1), End(I2);
    // At O3 we got better results (dhrystone) by being more conservative here.
    if (!ShouldCombineAggressively)
      End = std::next(MachineBasicBlock::iterator(I2));
    unsigned I1UseReg = UseReg(I1.getOperand(1));
    // Track killed operands. If we move across an instruction that kills our
    // operand, we need to update the kill information on the moved I1. It kills
    // the operand now.
    MachineInstr *KillingInstr = nullptr;
    unsigned KilledOperand = 0;

    while(++I != End) {
      MachineInstr &MI = *I;
      // If the intervening instruction MI:
      //   * modifies I1's use reg
      //   * modifies I1's def reg
      //   * reads I1's def reg
      //   * or has unmodelled side effects
      //   We introduce this special case because llvm has no api to remove a
      //   kill flag for a register (a removeRegisterKilled() analogous to
      //   addRegisterKilled) that handles aliased register correctly.
      //   * or has a killed aliased register use of I1's use reg
      //           %d4 = A2_tfrpi 16
      //           %r6 = A2_tfr %r9
      //           %r8 = KILL %r8, implicit killed %d4
      //      If we want to move R6 = across the KILL instruction we would have
      //      to remove the implicit killed %d4 operand. For now, we are
      //      conservative and disallow the move.
      // we can't move I1 across it.
      if (MI.isDebugInstr()) {
        if (MI.readsRegister(I1DestReg, TRI)) // Move this instruction after I2.
          DbgMItoMove.push_back(&MI);
        continue;
      }

      if (isUnsafeToMoveAcross(MI, I1UseReg, I1DestReg, TRI) ||
          // Check for an aliased register kill. Bail out if we see one.
          (!MI.killsRegister(I1UseReg) && MI.killsRegister(I1UseReg, TRI)))
        return false;

      // Check for an exact kill (registers match).
      if (I1UseReg && MI.killsRegister(I1UseReg)) {
        assert(!KillingInstr && "Should only see one killing instruction");
        KilledOperand = I1UseReg;
        KillingInstr = &MI;
      }
    }
    if (KillingInstr) {
      removeKillInfo(*KillingInstr, KilledOperand);
      // Update I1 to set the kill flag. This flag will later be picked up by
      // the new COMBINE instruction.
      bool Added = I1.addRegisterKilled(KilledOperand, TRI);
      (void)Added; // suppress compiler warning
      assert(Added && "Must successfully update kill flag");
    }
    DoInsertAtI1 = false;
  }

  return true;
}

/// findPotentialNewifiableTFRs - Finds tranfers that feed stores that could be
/// newified. (A use of a 64 bit register define can not be newified)
void
HexagonCopyToCombine::findPotentialNewifiableTFRs(MachineBasicBlock &BB) {
  DenseMap<unsigned, MachineInstr *> LastDef;
  for (MachineInstr &MI : BB) {
    if (MI.isDebugInstr())
      continue;

    // Mark TFRs that feed a potential new value store as such.
    if (TII->mayBeNewStore(MI)) {
      // Look for uses of TFR instructions.
      for (unsigned OpdIdx = 0, OpdE = MI.getNumOperands(); OpdIdx != OpdE;
           ++OpdIdx) {
        MachineOperand &Op = MI.getOperand(OpdIdx);

        // Skip over anything except register uses.
        if (!Op.isReg() || !Op.isUse() || !Op.getReg())
          continue;

        // Look for the defining instruction.
        unsigned Reg = Op.getReg();
        MachineInstr *DefInst = LastDef[Reg];
        if (!DefInst)
          continue;
        if (!isCombinableInstType(*DefInst, TII, ShouldCombineAggressively))
          continue;

        // Only close newifiable stores should influence the decision.
        // Ignore the debug instructions in between.
        MachineBasicBlock::iterator It(DefInst);
        unsigned NumInstsToDef = 0;
        while (&*It != &MI) {
          if (!It->isDebugInstr())
            ++NumInstsToDef;
          ++It;
        }

        if (NumInstsToDef > MaxNumOfInstsBetweenNewValueStoreAndTFR)
          continue;

        PotentiallyNewifiableTFR.insert(DefInst);
      }
      // Skip to next instruction.
      continue;
    }

    // Put instructions that last defined integer or double registers into the
    // map.
    for (MachineOperand &Op : MI.operands()) {
      if (Op.isReg()) {
        if (!Op.isDef() || !Op.getReg())
          continue;
        unsigned Reg = Op.getReg();
        if (Hexagon::DoubleRegsRegClass.contains(Reg)) {
          for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs)
            LastDef[*SubRegs] = &MI;
        } else if (Hexagon::IntRegsRegClass.contains(Reg))
          LastDef[Reg] = &MI;
      } else if (Op.isRegMask()) {
        for (unsigned Reg : Hexagon::IntRegsRegClass)
          if (Op.clobbersPhysReg(Reg))
            LastDef[Reg] = &MI;
      }
    }
  }
}

bool HexagonCopyToCombine::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  if (IsCombinesDisabled) return false;

  bool HasChanged = false;

  // Get target info.
  ST = &MF.getSubtarget<HexagonSubtarget>();
  TRI = ST->getRegisterInfo();
  TII = ST->getInstrInfo();

  const Function &F = MF.getFunction();
  bool OptForSize = F.hasFnAttribute(Attribute::OptimizeForSize);

  // Combine aggressively (for code size)
  ShouldCombineAggressively =
    MF.getTarget().getOptLevel() <= CodeGenOpt::Default;

  // Traverse basic blocks.
  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end(); BI != BE;
       ++BI) {
    PotentiallyNewifiableTFR.clear();
    findPotentialNewifiableTFRs(*BI);

    // Traverse instructions in basic block.
    for(MachineBasicBlock::iterator MI = BI->begin(), End = BI->end();
        MI != End;) {
      MachineInstr &I1 = *MI++;

      if (I1.isDebugInstr())
        continue;

      // Don't combine a TFR whose user could be newified (instructions that
      // define double registers can not be newified - Programmer's Ref Manual
      // 5.4.2 New-value stores).
      if (ShouldCombineAggressively && PotentiallyNewifiableTFR.count(&I1))
        continue;

      // Ignore instructions that are not combinable.
      if (!isCombinableInstType(I1, TII, ShouldCombineAggressively))
        continue;

      // Find a second instruction that can be merged into a combine
      // instruction. In addition, also find all the debug instructions that
      // need to be moved along with it.
      bool DoInsertAtI1 = false;
      DbgMItoMove.clear();
      MachineInstr *I2 = findPairable(I1, DoInsertAtI1, OptForSize);
      if (I2) {
        HasChanged = true;
        combine(I1, *I2, MI, DoInsertAtI1, OptForSize);
      }
    }
  }

  return HasChanged;
}

/// findPairable - Returns an instruction that can be merged with \p I1 into a
/// COMBINE instruction or 0 if no such instruction can be found. Returns true
/// in \p DoInsertAtI1 if the combine must be inserted at instruction \p I1
/// false if the combine must be inserted at the returned instruction.
MachineInstr *HexagonCopyToCombine::findPairable(MachineInstr &I1,
                                                 bool &DoInsertAtI1,
                                                 bool AllowC64) {
  MachineBasicBlock::iterator I2 = std::next(MachineBasicBlock::iterator(I1));
  while (I2 != I1.getParent()->end() && I2->isDebugInstr())
    ++I2;

  unsigned I1DestReg = I1.getOperand(0).getReg();

  for (MachineBasicBlock::iterator End = I1.getParent()->end(); I2 != End;
       ++I2) {
    // Bail out early if we see a second definition of I1DestReg.
    if (I2->modifiesRegister(I1DestReg, TRI))
      break;

    // Ignore non-combinable instructions.
    if (!isCombinableInstType(*I2, TII, ShouldCombineAggressively))
      continue;

    // Don't combine a TFR whose user could be newified.
    if (ShouldCombineAggressively && PotentiallyNewifiableTFR.count(&*I2))
      continue;

    unsigned I2DestReg = I2->getOperand(0).getReg();

    // Check that registers are adjacent and that the first destination register
    // is even.
    bool IsI1LowReg = (I2DestReg - I1DestReg) == 1;
    bool IsI2LowReg = (I1DestReg - I2DestReg) == 1;
    unsigned FirstRegIndex = IsI1LowReg ? I1DestReg : I2DestReg;
    if ((!IsI1LowReg && !IsI2LowReg) || !isEvenReg(FirstRegIndex))
      continue;

    // Check that the two instructions are combinable.
    // The order matters because in a A2_tfrsi we might can encode a int8 as
    // the hi reg operand but only a uint6 as the low reg operand.
    if ((IsI2LowReg && !areCombinableOperations(TRI, I1, *I2, AllowC64)) ||
        (IsI1LowReg && !areCombinableOperations(TRI, *I2, I1, AllowC64)))
      break;

    if (isSafeToMoveTogether(I1, *I2, I1DestReg, I2DestReg, DoInsertAtI1))
      return &*I2;

    // Not safe. Stop searching.
    break;
  }
  return nullptr;
}

void HexagonCopyToCombine::combine(MachineInstr &I1, MachineInstr &I2,
                                   MachineBasicBlock::iterator &MI,
                                   bool DoInsertAtI1, bool OptForSize) {
  // We are going to delete I2. If MI points to I2 advance it to the next
  // instruction.
  if (MI == I2.getIterator())
    ++MI;

  // Figure out whether I1 or I2 goes into the lowreg part.
  unsigned I1DestReg = I1.getOperand(0).getReg();
  unsigned I2DestReg = I2.getOperand(0).getReg();
  bool IsI1Loreg = (I2DestReg - I1DestReg) == 1;
  unsigned LoRegDef = IsI1Loreg ? I1DestReg : I2DestReg;
  unsigned SubLo;

  const TargetRegisterClass *SuperRC = nullptr;
  if (Hexagon::IntRegsRegClass.contains(LoRegDef)) {
    SuperRC = &Hexagon::DoubleRegsRegClass;
    SubLo = Hexagon::isub_lo;
  } else if (Hexagon::HvxVRRegClass.contains(LoRegDef)) {
    assert(ST->useHVXOps());
    SuperRC = &Hexagon::HvxWRRegClass;
    SubLo = Hexagon::vsub_lo;
  } else
    llvm_unreachable("Unexpected register class");

  // Get the double word register.
  unsigned DoubleRegDest = TRI->getMatchingSuperReg(LoRegDef, SubLo, SuperRC);
  assert(DoubleRegDest != 0 && "Expect a valid register");

  // Setup source operands.
  MachineOperand &LoOperand = IsI1Loreg ? I1.getOperand(1) : I2.getOperand(1);
  MachineOperand &HiOperand = IsI1Loreg ? I2.getOperand(1) : I1.getOperand(1);

  // Figure out which source is a register and which a constant.
  bool IsHiReg = HiOperand.isReg();
  bool IsLoReg = LoOperand.isReg();

  // There is a combine of two constant extended values into CONST64.
  bool IsC64 = OptForSize && LoOperand.isImm() && HiOperand.isImm() &&
               isGreaterThanNBitTFRI<16>(I1) && isGreaterThanNBitTFRI<16>(I2);

  MachineBasicBlock::iterator InsertPt(DoInsertAtI1 ? I1 : I2);
  // Emit combine.
  if (IsHiReg && IsLoReg)
    emitCombineRR(InsertPt, DoubleRegDest, HiOperand, LoOperand);
  else if (IsHiReg)
    emitCombineRI(InsertPt, DoubleRegDest, HiOperand, LoOperand);
  else if (IsLoReg)
    emitCombineIR(InsertPt, DoubleRegDest, HiOperand, LoOperand);
  else if (IsC64 && !IsConst64Disabled)
    emitConst64(InsertPt, DoubleRegDest, HiOperand, LoOperand);
  else
    emitCombineII(InsertPt, DoubleRegDest, HiOperand, LoOperand);

  // Move debug instructions along with I1 if it's being
  // moved towards I2.
  if (!DoInsertAtI1 && DbgMItoMove.size() != 0) {
    // Insert debug instructions at the new location before I2.
    MachineBasicBlock *BB = InsertPt->getParent();
    for (auto NewMI : DbgMItoMove) {
      // If iterator MI is pointing to DEBUG_VAL, make sure
      // MI now points to next relevant instruction.
      if (NewMI == MI)
        ++MI;
      BB->splice(InsertPt, BB, NewMI);
    }
  }

  I1.eraseFromParent();
  I2.eraseFromParent();
}

void HexagonCopyToCombine::emitConst64(MachineBasicBlock::iterator &InsertPt,
                                       unsigned DoubleDestReg,
                                       MachineOperand &HiOperand,
                                       MachineOperand &LoOperand) {
  LLVM_DEBUG(dbgs() << "Found a CONST64\n");

  DebugLoc DL = InsertPt->getDebugLoc();
  MachineBasicBlock *BB = InsertPt->getParent();
  assert(LoOperand.isImm() && HiOperand.isImm() &&
         "Both operands must be immediate");

  int64_t V = HiOperand.getImm();
  V = (V << 32) | (0x0ffffffffLL & LoOperand.getImm());
  BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::CONST64), DoubleDestReg)
    .addImm(V);
}

void HexagonCopyToCombine::emitCombineII(MachineBasicBlock::iterator &InsertPt,
                                         unsigned DoubleDestReg,
                                         MachineOperand &HiOperand,
                                         MachineOperand &LoOperand) {
  DebugLoc DL = InsertPt->getDebugLoc();
  MachineBasicBlock *BB = InsertPt->getParent();

  // Handle globals.
  if (HiOperand.isGlobal()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A2_combineii), DoubleDestReg)
      .addGlobalAddress(HiOperand.getGlobal(), HiOperand.getOffset(),
                        HiOperand.getTargetFlags())
      .addImm(LoOperand.getImm());
    return;
  }
  if (LoOperand.isGlobal()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineii), DoubleDestReg)
      .addImm(HiOperand.getImm())
      .addGlobalAddress(LoOperand.getGlobal(), LoOperand.getOffset(),
                        LoOperand.getTargetFlags());
    return;
  }

  // Handle block addresses.
  if (HiOperand.isBlockAddress()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A2_combineii), DoubleDestReg)
      .addBlockAddress(HiOperand.getBlockAddress(), HiOperand.getOffset(),
                       HiOperand.getTargetFlags())
      .addImm(LoOperand.getImm());
    return;
  }
  if (LoOperand.isBlockAddress()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineii), DoubleDestReg)
      .addImm(HiOperand.getImm())
      .addBlockAddress(LoOperand.getBlockAddress(), LoOperand.getOffset(),
                       LoOperand.getTargetFlags());
    return;
  }

  // Handle jump tables.
  if (HiOperand.isJTI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A2_combineii), DoubleDestReg)
      .addJumpTableIndex(HiOperand.getIndex(), HiOperand.getTargetFlags())
      .addImm(LoOperand.getImm());
    return;
  }
  if (LoOperand.isJTI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineii), DoubleDestReg)
      .addImm(HiOperand.getImm())
      .addJumpTableIndex(LoOperand.getIndex(), LoOperand.getTargetFlags());
    return;
  }

  // Handle constant pools.
  if (HiOperand.isCPI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A2_combineii), DoubleDestReg)
      .addConstantPoolIndex(HiOperand.getIndex(), HiOperand.getOffset(),
                            HiOperand.getTargetFlags())
      .addImm(LoOperand.getImm());
    return;
  }
  if (LoOperand.isCPI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineii), DoubleDestReg)
      .addImm(HiOperand.getImm())
      .addConstantPoolIndex(LoOperand.getIndex(), LoOperand.getOffset(),
                            LoOperand.getTargetFlags());
    return;
  }

  // First preference should be given to Hexagon::A2_combineii instruction
  // as it can include U6 (in Hexagon::A4_combineii) as well.
  // In this instruction, HiOperand is const extended, if required.
  if (isInt<8>(LoOperand.getImm())) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A2_combineii), DoubleDestReg)
      .addImm(HiOperand.getImm())
      .addImm(LoOperand.getImm());
      return;
  }

  // In this instruction, LoOperand is const extended, if required.
  if (isInt<8>(HiOperand.getImm())) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineii), DoubleDestReg)
      .addImm(HiOperand.getImm())
      .addImm(LoOperand.getImm());
    return;
  }

  // Insert new combine instruction.
  //  DoubleRegDest = combine #HiImm, #LoImm
  BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A2_combineii), DoubleDestReg)
    .addImm(HiOperand.getImm())
    .addImm(LoOperand.getImm());
}

void HexagonCopyToCombine::emitCombineIR(MachineBasicBlock::iterator &InsertPt,
                                         unsigned DoubleDestReg,
                                         MachineOperand &HiOperand,
                                         MachineOperand &LoOperand) {
  unsigned LoReg = LoOperand.getReg();
  unsigned LoRegKillFlag = getKillRegState(LoOperand.isKill());

  DebugLoc DL = InsertPt->getDebugLoc();
  MachineBasicBlock *BB = InsertPt->getParent();

  // Handle globals.
  if (HiOperand.isGlobal()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineir), DoubleDestReg)
      .addGlobalAddress(HiOperand.getGlobal(), HiOperand.getOffset(),
                        HiOperand.getTargetFlags())
      .addReg(LoReg, LoRegKillFlag);
    return;
  }
  // Handle block addresses.
  if (HiOperand.isBlockAddress()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineir), DoubleDestReg)
      .addBlockAddress(HiOperand.getBlockAddress(), HiOperand.getOffset(),
                       HiOperand.getTargetFlags())
      .addReg(LoReg, LoRegKillFlag);
    return;
  }
  // Handle jump tables.
  if (HiOperand.isJTI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineir), DoubleDestReg)
      .addJumpTableIndex(HiOperand.getIndex(), HiOperand.getTargetFlags())
      .addReg(LoReg, LoRegKillFlag);
    return;
  }
  // Handle constant pools.
  if (HiOperand.isCPI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineir), DoubleDestReg)
      .addConstantPoolIndex(HiOperand.getIndex(), HiOperand.getOffset(),
                            HiOperand.getTargetFlags())
      .addReg(LoReg, LoRegKillFlag);
    return;
  }
  // Insert new combine instruction.
  //  DoubleRegDest = combine #HiImm, LoReg
  BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineir), DoubleDestReg)
    .addImm(HiOperand.getImm())
    .addReg(LoReg, LoRegKillFlag);
}

void HexagonCopyToCombine::emitCombineRI(MachineBasicBlock::iterator &InsertPt,
                                         unsigned DoubleDestReg,
                                         MachineOperand &HiOperand,
                                         MachineOperand &LoOperand) {
  unsigned HiRegKillFlag = getKillRegState(HiOperand.isKill());
  unsigned HiReg = HiOperand.getReg();

  DebugLoc DL = InsertPt->getDebugLoc();
  MachineBasicBlock *BB = InsertPt->getParent();

  // Handle global.
  if (LoOperand.isGlobal()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineri), DoubleDestReg)
      .addReg(HiReg, HiRegKillFlag)
      .addGlobalAddress(LoOperand.getGlobal(), LoOperand.getOffset(),
                        LoOperand.getTargetFlags());
    return;
  }
  // Handle block addresses.
  if (LoOperand.isBlockAddress()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineri), DoubleDestReg)
      .addReg(HiReg, HiRegKillFlag)
      .addBlockAddress(LoOperand.getBlockAddress(), LoOperand.getOffset(),
                       LoOperand.getTargetFlags());
    return;
  }
  // Handle jump tables.
  if (LoOperand.isJTI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineri), DoubleDestReg)
      .addReg(HiOperand.getReg(), HiRegKillFlag)
      .addJumpTableIndex(LoOperand.getIndex(), LoOperand.getTargetFlags());
    return;
  }
  // Handle constant pools.
  if (LoOperand.isCPI()) {
    BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineri), DoubleDestReg)
      .addReg(HiOperand.getReg(), HiRegKillFlag)
      .addConstantPoolIndex(LoOperand.getIndex(), LoOperand.getOffset(),
                            LoOperand.getTargetFlags());
    return;
  }

  // Insert new combine instruction.
  //  DoubleRegDest = combine HiReg, #LoImm
  BuildMI(*BB, InsertPt, DL, TII->get(Hexagon::A4_combineri), DoubleDestReg)
    .addReg(HiReg, HiRegKillFlag)
    .addImm(LoOperand.getImm());
}

void HexagonCopyToCombine::emitCombineRR(MachineBasicBlock::iterator &InsertPt,
                                         unsigned DoubleDestReg,
                                         MachineOperand &HiOperand,
                                         MachineOperand &LoOperand) {
  unsigned LoRegKillFlag = getKillRegState(LoOperand.isKill());
  unsigned HiRegKillFlag = getKillRegState(HiOperand.isKill());
  unsigned LoReg = LoOperand.getReg();
  unsigned HiReg = HiOperand.getReg();

  DebugLoc DL = InsertPt->getDebugLoc();
  MachineBasicBlock *BB = InsertPt->getParent();

  // Insert new combine instruction.
  //  DoubleRegDest = combine HiReg, LoReg
  unsigned NewOpc;
  if (Hexagon::DoubleRegsRegClass.contains(DoubleDestReg)) {
    NewOpc = Hexagon::A2_combinew;
  } else if (Hexagon::HvxWRRegClass.contains(DoubleDestReg)) {
    assert(ST->useHVXOps());
    NewOpc = Hexagon::V6_vcombine;
  } else
    llvm_unreachable("Unexpected register");

  BuildMI(*BB, InsertPt, DL, TII->get(NewOpc), DoubleDestReg)
    .addReg(HiReg, HiRegKillFlag)
    .addReg(LoReg, LoRegKillFlag);
}

FunctionPass *llvm::createHexagonCopyToCombine() {
  return new HexagonCopyToCombine();
}
