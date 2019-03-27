//===-- AArch64AdvSIMDScalar.cpp - Replace dead defs w/ zero reg --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// When profitable, replace GPR targeting i64 instructions with their
// AdvSIMD scalar equivalents. Generally speaking, "profitable" is defined
// as minimizing the number of cross-class register copies.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// TODO: Graph based predicate heuristics.
// Walking the instruction list linearly will get many, perhaps most, of
// the cases, but to do a truly thorough job of this, we need a more
// wholistic approach.
//
// This optimization is very similar in spirit to the register allocator's
// spill placement, only here we're determining where to place cross-class
// register copies rather than spills. As such, a similar approach is
// called for.
//
// We want to build up a set of graphs of all instructions which are candidates
// for transformation along with instructions which generate their inputs and
// consume their outputs. For each edge in the graph, we assign a weight
// based on whether there is a copy required there (weight zero if not) and
// the block frequency of the block containing the defining or using
// instruction, whichever is less. Our optimization is then a graph problem
// to minimize the total weight of all the graphs, then transform instructions
// and add or remove copy instructions as called for to implement the
// solution.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64RegisterInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-simd-scalar"

// Allow forcing all i64 operations with equivalent SIMD instructions to use
// them. For stress-testing the transformation function.
static cl::opt<bool>
TransformAll("aarch64-simd-scalar-force-all",
             cl::desc("Force use of AdvSIMD scalar instructions everywhere"),
             cl::init(false), cl::Hidden);

STATISTIC(NumScalarInsnsUsed, "Number of scalar instructions used");
STATISTIC(NumCopiesDeleted, "Number of cross-class copies deleted");
STATISTIC(NumCopiesInserted, "Number of cross-class copies inserted");

#define AARCH64_ADVSIMD_NAME "AdvSIMD Scalar Operation Optimization"

namespace {
class AArch64AdvSIMDScalar : public MachineFunctionPass {
  MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;

private:
  // isProfitableToTransform - Predicate function to determine whether an
  // instruction should be transformed to its equivalent AdvSIMD scalar
  // instruction. "add Xd, Xn, Xm" ==> "add Dd, Da, Db", for example.
  bool isProfitableToTransform(const MachineInstr &MI) const;

  // transformInstruction - Perform the transformation of an instruction
  // to its equivalant AdvSIMD scalar instruction. Update inputs and outputs
  // to be the correct register class, minimizing cross-class copies.
  void transformInstruction(MachineInstr &MI);

  // processMachineBasicBlock - Main optimzation loop.
  bool processMachineBasicBlock(MachineBasicBlock *MBB);

public:
  static char ID; // Pass identification, replacement for typeid.
  explicit AArch64AdvSIMDScalar() : MachineFunctionPass(ID) {
    initializeAArch64AdvSIMDScalarPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  StringRef getPassName() const override { return AARCH64_ADVSIMD_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
char AArch64AdvSIMDScalar::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS(AArch64AdvSIMDScalar, "aarch64-simd-scalar",
                AARCH64_ADVSIMD_NAME, false, false)

static bool isGPR64(unsigned Reg, unsigned SubReg,
                    const MachineRegisterInfo *MRI) {
  if (SubReg)
    return false;
  if (TargetRegisterInfo::isVirtualRegister(Reg))
    return MRI->getRegClass(Reg)->hasSuperClassEq(&AArch64::GPR64RegClass);
  return AArch64::GPR64RegClass.contains(Reg);
}

static bool isFPR64(unsigned Reg, unsigned SubReg,
                    const MachineRegisterInfo *MRI) {
  if (TargetRegisterInfo::isVirtualRegister(Reg))
    return (MRI->getRegClass(Reg)->hasSuperClassEq(&AArch64::FPR64RegClass) &&
            SubReg == 0) ||
           (MRI->getRegClass(Reg)->hasSuperClassEq(&AArch64::FPR128RegClass) &&
            SubReg == AArch64::dsub);
  // Physical register references just check the register class directly.
  return (AArch64::FPR64RegClass.contains(Reg) && SubReg == 0) ||
         (AArch64::FPR128RegClass.contains(Reg) && SubReg == AArch64::dsub);
}

// getSrcFromCopy - Get the original source register for a GPR64 <--> FPR64
// copy instruction. Return zero_reg if the instruction is not a copy.
static MachineOperand *getSrcFromCopy(MachineInstr *MI,
                                      const MachineRegisterInfo *MRI,
                                      unsigned &SubReg) {
  SubReg = 0;
  // The "FMOV Xd, Dn" instruction is the typical form.
  if (MI->getOpcode() == AArch64::FMOVDXr ||
      MI->getOpcode() == AArch64::FMOVXDr)
    return &MI->getOperand(1);
  // A lane zero extract "UMOV.d Xd, Vn[0]" is equivalent. We shouldn't see
  // these at this stage, but it's easy to check for.
  if (MI->getOpcode() == AArch64::UMOVvi64 && MI->getOperand(2).getImm() == 0) {
    SubReg = AArch64::dsub;
    return &MI->getOperand(1);
  }
  // Or just a plain COPY instruction. This can be directly to/from FPR64,
  // or it can be a dsub subreg reference to an FPR128.
  if (MI->getOpcode() == AArch64::COPY) {
    if (isFPR64(MI->getOperand(0).getReg(), MI->getOperand(0).getSubReg(),
                MRI) &&
        isGPR64(MI->getOperand(1).getReg(), MI->getOperand(1).getSubReg(), MRI))
      return &MI->getOperand(1);
    if (isGPR64(MI->getOperand(0).getReg(), MI->getOperand(0).getSubReg(),
                MRI) &&
        isFPR64(MI->getOperand(1).getReg(), MI->getOperand(1).getSubReg(),
                MRI)) {
      SubReg = MI->getOperand(1).getSubReg();
      return &MI->getOperand(1);
    }
  }

  // Otherwise, this is some other kind of instruction.
  return nullptr;
}

// getTransformOpcode - For any opcode for which there is an AdvSIMD equivalent
// that we're considering transforming to, return that AdvSIMD opcode. For all
// others, return the original opcode.
static unsigned getTransformOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    break;
  // FIXME: Lots more possibilities.
  case AArch64::ADDXrr:
    return AArch64::ADDv1i64;
  case AArch64::SUBXrr:
    return AArch64::SUBv1i64;
  case AArch64::ANDXrr:
    return AArch64::ANDv8i8;
  case AArch64::EORXrr:
    return AArch64::EORv8i8;
  case AArch64::ORRXrr:
    return AArch64::ORRv8i8;
  }
  // No AdvSIMD equivalent, so just return the original opcode.
  return Opc;
}

static bool isTransformable(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return Opc != getTransformOpcode(Opc);
}

// isProfitableToTransform - Predicate function to determine whether an
// instruction should be transformed to its equivalent AdvSIMD scalar
// instruction. "add Xd, Xn, Xm" ==> "add Dd, Da, Db", for example.
bool AArch64AdvSIMDScalar::isProfitableToTransform(
    const MachineInstr &MI) const {
  // If this instruction isn't eligible to be transformed (no SIMD equivalent),
  // early exit since that's the common case.
  if (!isTransformable(MI))
    return false;

  // Count the number of copies we'll need to add and approximate the number
  // of copies that a transform will enable us to remove.
  unsigned NumNewCopies = 3;
  unsigned NumRemovableCopies = 0;

  unsigned OrigSrc0 = MI.getOperand(1).getReg();
  unsigned OrigSrc1 = MI.getOperand(2).getReg();
  unsigned SubReg0;
  unsigned SubReg1;
  if (!MRI->def_empty(OrigSrc0)) {
    MachineRegisterInfo::def_instr_iterator Def =
        MRI->def_instr_begin(OrigSrc0);
    assert(std::next(Def) == MRI->def_instr_end() && "Multiple def in SSA!");
    MachineOperand *MOSrc0 = getSrcFromCopy(&*Def, MRI, SubReg0);
    // If the source was from a copy, we don't need to insert a new copy.
    if (MOSrc0)
      --NumNewCopies;
    // If there are no other users of the original source, we can delete
    // that instruction.
    if (MOSrc0 && MRI->hasOneNonDBGUse(OrigSrc0))
      ++NumRemovableCopies;
  }
  if (!MRI->def_empty(OrigSrc1)) {
    MachineRegisterInfo::def_instr_iterator Def =
        MRI->def_instr_begin(OrigSrc1);
    assert(std::next(Def) == MRI->def_instr_end() && "Multiple def in SSA!");
    MachineOperand *MOSrc1 = getSrcFromCopy(&*Def, MRI, SubReg1);
    if (MOSrc1)
      --NumNewCopies;
    // If there are no other users of the original source, we can delete
    // that instruction.
    if (MOSrc1 && MRI->hasOneNonDBGUse(OrigSrc1))
      ++NumRemovableCopies;
  }

  // If any of the uses of the original instructions is a cross class copy,
  // that's a copy that will be removable if we transform. Likewise, if
  // any of the uses is a transformable instruction, it's likely the tranforms
  // will chain, enabling us to save a copy there, too. This is an aggressive
  // heuristic that approximates the graph based cost analysis described above.
  unsigned Dst = MI.getOperand(0).getReg();
  bool AllUsesAreCopies = true;
  for (MachineRegisterInfo::use_instr_nodbg_iterator
           Use = MRI->use_instr_nodbg_begin(Dst),
           E = MRI->use_instr_nodbg_end();
       Use != E; ++Use) {
    unsigned SubReg;
    if (getSrcFromCopy(&*Use, MRI, SubReg) || isTransformable(*Use))
      ++NumRemovableCopies;
    // If the use is an INSERT_SUBREG, that's still something that can
    // directly use the FPR64, so we don't invalidate AllUsesAreCopies. It's
    // preferable to have it use the FPR64 in most cases, as if the source
    // vector is an IMPLICIT_DEF, the INSERT_SUBREG just goes away entirely.
    // Ditto for a lane insert.
    else if (Use->getOpcode() == AArch64::INSERT_SUBREG ||
             Use->getOpcode() == AArch64::INSvi64gpr)
      ;
    else
      AllUsesAreCopies = false;
  }
  // If all of the uses of the original destination register are copies to
  // FPR64, then we won't end up having a new copy back to GPR64 either.
  if (AllUsesAreCopies)
    --NumNewCopies;

  // If a transform will not increase the number of cross-class copies required,
  // return true.
  if (NumNewCopies <= NumRemovableCopies)
    return true;

  // Finally, even if we otherwise wouldn't transform, check if we're forcing
  // transformation of everything.
  return TransformAll;
}

static MachineInstr *insertCopy(const TargetInstrInfo *TII, MachineInstr &MI,
                                unsigned Dst, unsigned Src, bool IsKill) {
  MachineInstrBuilder MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                                    TII->get(AArch64::COPY), Dst)
                                .addReg(Src, getKillRegState(IsKill));
  LLVM_DEBUG(dbgs() << "    adding copy: " << *MIB);
  ++NumCopiesInserted;
  return MIB;
}

// transformInstruction - Perform the transformation of an instruction
// to its equivalant AdvSIMD scalar instruction. Update inputs and outputs
// to be the correct register class, minimizing cross-class copies.
void AArch64AdvSIMDScalar::transformInstruction(MachineInstr &MI) {
  LLVM_DEBUG(dbgs() << "Scalar transform: " << MI);

  MachineBasicBlock *MBB = MI.getParent();
  unsigned OldOpc = MI.getOpcode();
  unsigned NewOpc = getTransformOpcode(OldOpc);
  assert(OldOpc != NewOpc && "transform an instruction to itself?!");

  // Check if we need a copy for the source registers.
  unsigned OrigSrc0 = MI.getOperand(1).getReg();
  unsigned OrigSrc1 = MI.getOperand(2).getReg();
  unsigned Src0 = 0, SubReg0;
  unsigned Src1 = 0, SubReg1;
  bool KillSrc0 = false, KillSrc1 = false;
  if (!MRI->def_empty(OrigSrc0)) {
    MachineRegisterInfo::def_instr_iterator Def =
        MRI->def_instr_begin(OrigSrc0);
    assert(std::next(Def) == MRI->def_instr_end() && "Multiple def in SSA!");
    MachineOperand *MOSrc0 = getSrcFromCopy(&*Def, MRI, SubReg0);
    // If there are no other users of the original source, we can delete
    // that instruction.
    if (MOSrc0) {
      Src0 = MOSrc0->getReg();
      KillSrc0 = MOSrc0->isKill();
      // Src0 is going to be reused, thus, it cannot be killed anymore.
      MOSrc0->setIsKill(false);
      if (MRI->hasOneNonDBGUse(OrigSrc0)) {
        assert(MOSrc0 && "Can't delete copy w/o a valid original source!");
        Def->eraseFromParent();
        ++NumCopiesDeleted;
      }
    }
  }
  if (!MRI->def_empty(OrigSrc1)) {
    MachineRegisterInfo::def_instr_iterator Def =
        MRI->def_instr_begin(OrigSrc1);
    assert(std::next(Def) == MRI->def_instr_end() && "Multiple def in SSA!");
    MachineOperand *MOSrc1 = getSrcFromCopy(&*Def, MRI, SubReg1);
    // If there are no other users of the original source, we can delete
    // that instruction.
    if (MOSrc1) {
      Src1 = MOSrc1->getReg();
      KillSrc1 = MOSrc1->isKill();
      // Src0 is going to be reused, thus, it cannot be killed anymore.
      MOSrc1->setIsKill(false);
      if (MRI->hasOneNonDBGUse(OrigSrc1)) {
        assert(MOSrc1 && "Can't delete copy w/o a valid original source!");
        Def->eraseFromParent();
        ++NumCopiesDeleted;
      }
    }
  }
  // If we weren't able to reference the original source directly, create a
  // copy.
  if (!Src0) {
    SubReg0 = 0;
    Src0 = MRI->createVirtualRegister(&AArch64::FPR64RegClass);
    insertCopy(TII, MI, Src0, OrigSrc0, KillSrc0);
    KillSrc0 = true;
  }
  if (!Src1) {
    SubReg1 = 0;
    Src1 = MRI->createVirtualRegister(&AArch64::FPR64RegClass);
    insertCopy(TII, MI, Src1, OrigSrc1, KillSrc1);
    KillSrc1 = true;
  }

  // Create a vreg for the destination.
  // FIXME: No need to do this if the ultimate user expects an FPR64.
  // Check for that and avoid the copy if possible.
  unsigned Dst = MRI->createVirtualRegister(&AArch64::FPR64RegClass);

  // For now, all of the new instructions have the same simple three-register
  // form, so no need to special case based on what instruction we're
  // building.
  BuildMI(*MBB, MI, MI.getDebugLoc(), TII->get(NewOpc), Dst)
      .addReg(Src0, getKillRegState(KillSrc0), SubReg0)
      .addReg(Src1, getKillRegState(KillSrc1), SubReg1);

  // Now copy the result back out to a GPR.
  // FIXME: Try to avoid this if all uses could actually just use the FPR64
  // directly.
  insertCopy(TII, MI, MI.getOperand(0).getReg(), Dst, true);

  // Erase the old instruction.
  MI.eraseFromParent();

  ++NumScalarInsnsUsed;
}

// processMachineBasicBlock - Main optimzation loop.
bool AArch64AdvSIMDScalar::processMachineBasicBlock(MachineBasicBlock *MBB) {
  bool Changed = false;
  for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end(); I != E;) {
    MachineInstr &MI = *I++;
    if (isProfitableToTransform(MI)) {
      transformInstruction(MI);
      Changed = true;
    }
  }
  return Changed;
}

// runOnMachineFunction - Pass entry point from PassManager.
bool AArch64AdvSIMDScalar::runOnMachineFunction(MachineFunction &mf) {
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "***** AArch64AdvSIMDScalar *****\n");

  if (skipFunction(mf.getFunction()))
    return false;

  MRI = &mf.getRegInfo();
  TII = mf.getSubtarget().getInstrInfo();

  // Just check things on a one-block-at-a-time basis.
  for (MachineFunction::iterator I = mf.begin(), E = mf.end(); I != E; ++I)
    if (processMachineBasicBlock(&*I))
      Changed = true;
  return Changed;
}

// createAArch64AdvSIMDScalar - Factory function used by AArch64TargetMachine
// to add the pass to the PassManager.
FunctionPass *llvm::createAArch64AdvSIMDScalar() {
  return new AArch64AdvSIMDScalar();
}
