//===-- PPCISelDAGToDAG.cpp - PPC --pattern matching inst selector --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pattern matching instruction selector for PowerPC,
// converting from a legalized dag to a PPC dag.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCISelLowering.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "ppc-codegen"

STATISTIC(NumSextSetcc,
          "Number of (sext(setcc)) nodes expanded into GPR sequence.");
STATISTIC(NumZextSetcc,
          "Number of (zext(setcc)) nodes expanded into GPR sequence.");
STATISTIC(SignExtensionsAdded,
          "Number of sign extensions for compare inputs added.");
STATISTIC(ZeroExtensionsAdded,
          "Number of zero extensions for compare inputs added.");
STATISTIC(NumLogicOpsOnComparison,
          "Number of logical ops on i1 values calculated in GPR.");
STATISTIC(OmittedForNonExtendUses,
          "Number of compares not eliminated as they have non-extending uses.");
STATISTIC(NumP9Setb,
          "Number of compares lowered to setb.");

// FIXME: Remove this once the bug has been fixed!
cl::opt<bool> ANDIGlueBug("expose-ppc-andi-glue-bug",
cl::desc("expose the ANDI glue bug on PPC"), cl::Hidden);

static cl::opt<bool>
    UseBitPermRewriter("ppc-use-bit-perm-rewriter", cl::init(true),
                       cl::desc("use aggressive ppc isel for bit permutations"),
                       cl::Hidden);
static cl::opt<bool> BPermRewriterNoMasking(
    "ppc-bit-perm-rewriter-stress-rotates",
    cl::desc("stress rotate selection in aggressive ppc isel for "
             "bit permutations"),
    cl::Hidden);

static cl::opt<bool> EnableBranchHint(
  "ppc-use-branch-hint", cl::init(true),
    cl::desc("Enable static hinting of branches on ppc"),
    cl::Hidden);

static cl::opt<bool> EnableTLSOpt(
  "ppc-tls-opt", cl::init(true),
    cl::desc("Enable tls optimization peephole"),
    cl::Hidden);

enum ICmpInGPRType { ICGPR_All, ICGPR_None, ICGPR_I32, ICGPR_I64,
  ICGPR_NonExtIn, ICGPR_Zext, ICGPR_Sext, ICGPR_ZextI32,
  ICGPR_SextI32, ICGPR_ZextI64, ICGPR_SextI64 };

static cl::opt<ICmpInGPRType> CmpInGPR(
  "ppc-gpr-icmps", cl::Hidden, cl::init(ICGPR_All),
  cl::desc("Specify the types of comparisons to emit GPR-only code for."),
  cl::values(clEnumValN(ICGPR_None, "none", "Do not modify integer comparisons."),
             clEnumValN(ICGPR_All, "all", "All possible int comparisons in GPRs."),
             clEnumValN(ICGPR_I32, "i32", "Only i32 comparisons in GPRs."),
             clEnumValN(ICGPR_I64, "i64", "Only i64 comparisons in GPRs."),
             clEnumValN(ICGPR_NonExtIn, "nonextin",
                        "Only comparisons where inputs don't need [sz]ext."),
             clEnumValN(ICGPR_Zext, "zext", "Only comparisons with zext result."),
             clEnumValN(ICGPR_ZextI32, "zexti32",
                        "Only i32 comparisons with zext result."),
             clEnumValN(ICGPR_ZextI64, "zexti64",
                        "Only i64 comparisons with zext result."),
             clEnumValN(ICGPR_Sext, "sext", "Only comparisons with sext result."),
             clEnumValN(ICGPR_SextI32, "sexti32",
                        "Only i32 comparisons with sext result."),
             clEnumValN(ICGPR_SextI64, "sexti64",
                        "Only i64 comparisons with sext result.")));
namespace {

  //===--------------------------------------------------------------------===//
  /// PPCDAGToDAGISel - PPC specific code to select PPC machine
  /// instructions for SelectionDAG operations.
  ///
  class PPCDAGToDAGISel : public SelectionDAGISel {
    const PPCTargetMachine &TM;
    const PPCSubtarget *PPCSubTarget;
    const PPCTargetLowering *PPCLowering;
    unsigned GlobalBaseReg;

  public:
    explicit PPCDAGToDAGISel(PPCTargetMachine &tm, CodeGenOpt::Level OptLevel)
        : SelectionDAGISel(tm, OptLevel), TM(tm) {}

    bool runOnMachineFunction(MachineFunction &MF) override {
      // Make sure we re-emit a set of the global base reg if necessary
      GlobalBaseReg = 0;
      PPCSubTarget = &MF.getSubtarget<PPCSubtarget>();
      PPCLowering = PPCSubTarget->getTargetLowering();
      SelectionDAGISel::runOnMachineFunction(MF);

      if (!PPCSubTarget->isSVR4ABI())
        InsertVRSaveCode(MF);

      return true;
    }

    void PreprocessISelDAG() override;
    void PostprocessISelDAG() override;

    /// getI16Imm - Return a target constant with the specified value, of type
    /// i16.
    inline SDValue getI16Imm(unsigned Imm, const SDLoc &dl) {
      return CurDAG->getTargetConstant(Imm, dl, MVT::i16);
    }

    /// getI32Imm - Return a target constant with the specified value, of type
    /// i32.
    inline SDValue getI32Imm(unsigned Imm, const SDLoc &dl) {
      return CurDAG->getTargetConstant(Imm, dl, MVT::i32);
    }

    /// getI64Imm - Return a target constant with the specified value, of type
    /// i64.
    inline SDValue getI64Imm(uint64_t Imm, const SDLoc &dl) {
      return CurDAG->getTargetConstant(Imm, dl, MVT::i64);
    }

    /// getSmallIPtrImm - Return a target constant of pointer type.
    inline SDValue getSmallIPtrImm(unsigned Imm, const SDLoc &dl) {
      return CurDAG->getTargetConstant(
          Imm, dl, PPCLowering->getPointerTy(CurDAG->getDataLayout()));
    }

    /// isRotateAndMask - Returns true if Mask and Shift can be folded into a
    /// rotate and mask opcode and mask operation.
    static bool isRotateAndMask(SDNode *N, unsigned Mask, bool isShiftMask,
                                unsigned &SH, unsigned &MB, unsigned &ME);

    /// getGlobalBaseReg - insert code into the entry mbb to materialize the PIC
    /// base register.  Return the virtual register that holds this value.
    SDNode *getGlobalBaseReg();

    void selectFrameIndex(SDNode *SN, SDNode *N, unsigned Offset = 0);

    // Select - Convert the specified operand from a target-independent to a
    // target-specific node if it hasn't already been changed.
    void Select(SDNode *N) override;

    bool tryBitfieldInsert(SDNode *N);
    bool tryBitPermutation(SDNode *N);
    bool tryIntCompareInGPR(SDNode *N);

    // tryTLSXFormLoad - Convert an ISD::LOAD fed by a PPCISD::ADD_TLS into
    // an X-Form load instruction with the offset being a relocation coming from
    // the PPCISD::ADD_TLS.
    bool tryTLSXFormLoad(LoadSDNode *N);
    // tryTLSXFormStore - Convert an ISD::STORE fed by a PPCISD::ADD_TLS into
    // an X-Form store instruction with the offset being a relocation coming from
    // the PPCISD::ADD_TLS.
    bool tryTLSXFormStore(StoreSDNode *N);
    /// SelectCC - Select a comparison of the specified values with the
    /// specified condition code, returning the CR# of the expression.
    SDValue SelectCC(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                     const SDLoc &dl);

    /// SelectAddrImm - Returns true if the address N can be represented by
    /// a base register plus a signed 16-bit displacement [r+imm].
    bool SelectAddrImm(SDValue N, SDValue &Disp,
                       SDValue &Base) {
      return PPCLowering->SelectAddressRegImm(N, Disp, Base, *CurDAG, 0);
    }

    /// SelectAddrImmOffs - Return true if the operand is valid for a preinc
    /// immediate field.  Note that the operand at this point is already the
    /// result of a prior SelectAddressRegImm call.
    bool SelectAddrImmOffs(SDValue N, SDValue &Out) const {
      if (N.getOpcode() == ISD::TargetConstant ||
          N.getOpcode() == ISD::TargetGlobalAddress) {
        Out = N;
        return true;
      }

      return false;
    }

    /// SelectAddrIdx - Given the specified addressed, check to see if it can be
    /// represented as an indexed [r+r] operation.  Returns false if it can
    /// be represented by [r+imm], which are preferred.
    bool SelectAddrIdx(SDValue N, SDValue &Base, SDValue &Index) {
      return PPCLowering->SelectAddressRegReg(N, Base, Index, *CurDAG);
    }

    /// SelectAddrIdxOnly - Given the specified addressed, force it to be
    /// represented as an indexed [r+r] operation.
    bool SelectAddrIdxOnly(SDValue N, SDValue &Base, SDValue &Index) {
      return PPCLowering->SelectAddressRegRegOnly(N, Base, Index, *CurDAG);
    }

    /// SelectAddrImmX4 - Returns true if the address N can be represented by
    /// a base register plus a signed 16-bit displacement that is a multiple of 4.
    /// Suitable for use by STD and friends.
    bool SelectAddrImmX4(SDValue N, SDValue &Disp, SDValue &Base) {
      return PPCLowering->SelectAddressRegImm(N, Disp, Base, *CurDAG, 4);
    }

    bool SelectAddrImmX16(SDValue N, SDValue &Disp, SDValue &Base) {
      return PPCLowering->SelectAddressRegImm(N, Disp, Base, *CurDAG, 16);
    }

    // Select an address into a single register.
    bool SelectAddr(SDValue N, SDValue &Base) {
      Base = N;
      return true;
    }

    /// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
    /// inline asm expressions.  It is always correct to compute the value into
    /// a register.  The case of adding a (possibly relocatable) constant to a
    /// register can be improved, but it is wrong to substitute Reg+Reg for
    /// Reg in an asm, because the load or store opcode would have to change.
    bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                      unsigned ConstraintID,
                                      std::vector<SDValue> &OutOps) override {
      switch(ConstraintID) {
      default:
        errs() << "ConstraintID: " << ConstraintID << "\n";
        llvm_unreachable("Unexpected asm memory constraint");
      case InlineAsm::Constraint_es:
      case InlineAsm::Constraint_i:
      case InlineAsm::Constraint_m:
      case InlineAsm::Constraint_o:
      case InlineAsm::Constraint_Q:
      case InlineAsm::Constraint_Z:
      case InlineAsm::Constraint_Zy:
        // We need to make sure that this one operand does not end up in r0
        // (because we might end up lowering this as 0(%op)).
        const TargetRegisterInfo *TRI = PPCSubTarget->getRegisterInfo();
        const TargetRegisterClass *TRC = TRI->getPointerRegClass(*MF, /*Kind=*/1);
        SDLoc dl(Op);
        SDValue RC = CurDAG->getTargetConstant(TRC->getID(), dl, MVT::i32);
        SDValue NewOp =
          SDValue(CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS,
                                         dl, Op.getValueType(),
                                         Op, RC), 0);

        OutOps.push_back(NewOp);
        return false;
      }
      return true;
    }

    void InsertVRSaveCode(MachineFunction &MF);

    StringRef getPassName() const override {
      return "PowerPC DAG->DAG Pattern Instruction Selection";
    }

// Include the pieces autogenerated from the target description.
#include "PPCGenDAGISel.inc"

private:
    bool trySETCC(SDNode *N);

    void PeepholePPC64();
    void PeepholePPC64ZExt();
    void PeepholeCROps();

    SDValue combineToCMPB(SDNode *N);
    void foldBoolExts(SDValue &Res, SDNode *&N);

    bool AllUsersSelectZero(SDNode *N);
    void SwapAllSelectUsers(SDNode *N);

    bool isOffsetMultipleOf(SDNode *N, unsigned Val) const;
    void transferMemOperands(SDNode *N, SDNode *Result);
  };

} // end anonymous namespace

/// InsertVRSaveCode - Once the entire function has been instruction selected,
/// all virtual registers are created and all machine instructions are built,
/// check to see if we need to save/restore VRSAVE.  If so, do it.
void PPCDAGToDAGISel::InsertVRSaveCode(MachineFunction &Fn) {
  // Check to see if this function uses vector registers, which means we have to
  // save and restore the VRSAVE register and update it with the regs we use.
  //
  // In this case, there will be virtual registers of vector type created
  // by the scheduler.  Detect them now.
  bool HasVectorVReg = false;
  for (unsigned i = 0, e = RegInfo->getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (RegInfo->getRegClass(Reg) == &PPC::VRRCRegClass) {
      HasVectorVReg = true;
      break;
    }
  }
  if (!HasVectorVReg) return;  // nothing to do.

  // If we have a vector register, we want to emit code into the entry and exit
  // blocks to save and restore the VRSAVE register.  We do this here (instead
  // of marking all vector instructions as clobbering VRSAVE) for two reasons:
  //
  // 1. This (trivially) reduces the load on the register allocator, by not
  //    having to represent the live range of the VRSAVE register.
  // 2. This (more significantly) allows us to create a temporary virtual
  //    register to hold the saved VRSAVE value, allowing this temporary to be
  //    register allocated, instead of forcing it to be spilled to the stack.

  // Create two vregs - one to hold the VRSAVE register that is live-in to the
  // function and one for the value after having bits or'd into it.
  unsigned InVRSAVE = RegInfo->createVirtualRegister(&PPC::GPRCRegClass);
  unsigned UpdatedVRSAVE = RegInfo->createVirtualRegister(&PPC::GPRCRegClass);

  const TargetInstrInfo &TII = *PPCSubTarget->getInstrInfo();
  MachineBasicBlock &EntryBB = *Fn.begin();
  DebugLoc dl;
  // Emit the following code into the entry block:
  // InVRSAVE = MFVRSAVE
  // UpdatedVRSAVE = UPDATE_VRSAVE InVRSAVE
  // MTVRSAVE UpdatedVRSAVE
  MachineBasicBlock::iterator IP = EntryBB.begin();  // Insert Point
  BuildMI(EntryBB, IP, dl, TII.get(PPC::MFVRSAVE), InVRSAVE);
  BuildMI(EntryBB, IP, dl, TII.get(PPC::UPDATE_VRSAVE),
          UpdatedVRSAVE).addReg(InVRSAVE);
  BuildMI(EntryBB, IP, dl, TII.get(PPC::MTVRSAVE)).addReg(UpdatedVRSAVE);

  // Find all return blocks, outputting a restore in each epilog.
  for (MachineFunction::iterator BB = Fn.begin(), E = Fn.end(); BB != E; ++BB) {
    if (BB->isReturnBlock()) {
      IP = BB->end(); --IP;

      // Skip over all terminator instructions, which are part of the return
      // sequence.
      MachineBasicBlock::iterator I2 = IP;
      while (I2 != BB->begin() && (--I2)->isTerminator())
        IP = I2;

      // Emit: MTVRSAVE InVRSave
      BuildMI(*BB, IP, dl, TII.get(PPC::MTVRSAVE)).addReg(InVRSAVE);
    }
  }
}

/// getGlobalBaseReg - Output the instructions required to put the
/// base address to use for accessing globals into a register.
///
SDNode *PPCDAGToDAGISel::getGlobalBaseReg() {
  if (!GlobalBaseReg) {
    const TargetInstrInfo &TII = *PPCSubTarget->getInstrInfo();
    // Insert the set of GlobalBaseReg into the first MBB of the function
    MachineBasicBlock &FirstMBB = MF->front();
    MachineBasicBlock::iterator MBBI = FirstMBB.begin();
    const Module *M = MF->getFunction().getParent();
    DebugLoc dl;

    if (PPCLowering->getPointerTy(CurDAG->getDataLayout()) == MVT::i32) {
      if (PPCSubTarget->isTargetELF()) {
        GlobalBaseReg = PPC::R30;
        if (M->getPICLevel() == PICLevel::SmallPIC) {
          BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MoveGOTtoLR));
          BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MFLR), GlobalBaseReg);
          MF->getInfo<PPCFunctionInfo>()->setUsesPICBase(true);
        } else {
          BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MovePCtoLR));
          BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MFLR), GlobalBaseReg);
          unsigned TempReg = RegInfo->createVirtualRegister(&PPC::GPRCRegClass);
          BuildMI(FirstMBB, MBBI, dl,
                  TII.get(PPC::UpdateGBR), GlobalBaseReg)
                  .addReg(TempReg, RegState::Define).addReg(GlobalBaseReg);
          MF->getInfo<PPCFunctionInfo>()->setUsesPICBase(true);
        }
      } else {
        GlobalBaseReg =
          RegInfo->createVirtualRegister(&PPC::GPRC_and_GPRC_NOR0RegClass);
        BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MovePCtoLR));
        BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MFLR), GlobalBaseReg);
      }
    } else {
      // We must ensure that this sequence is dominated by the prologue.
      // FIXME: This is a bit of a big hammer since we don't get the benefits
      // of shrink-wrapping whenever we emit this instruction. Considering
      // this is used in any function where we emit a jump table, this may be
      // a significant limitation. We should consider inserting this in the
      // block where it is used and then commoning this sequence up if it
      // appears in multiple places.
      // Note: on ISA 3.0 cores, we can use lnia (addpcis) instead of
      // MovePCtoLR8.
      MF->getInfo<PPCFunctionInfo>()->setShrinkWrapDisabled(true);
      GlobalBaseReg = RegInfo->createVirtualRegister(&PPC::G8RC_and_G8RC_NOX0RegClass);
      BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MovePCtoLR8));
      BuildMI(FirstMBB, MBBI, dl, TII.get(PPC::MFLR8), GlobalBaseReg);
    }
  }
  return CurDAG->getRegister(GlobalBaseReg,
                             PPCLowering->getPointerTy(CurDAG->getDataLayout()))
      .getNode();
}

/// isInt32Immediate - This method tests to see if the node is a 32-bit constant
/// operand. If so Imm will receive the 32-bit value.
static bool isInt32Immediate(SDNode *N, unsigned &Imm) {
  if (N->getOpcode() == ISD::Constant && N->getValueType(0) == MVT::i32) {
    Imm = cast<ConstantSDNode>(N)->getZExtValue();
    return true;
  }
  return false;
}

/// isInt64Immediate - This method tests to see if the node is a 64-bit constant
/// operand.  If so Imm will receive the 64-bit value.
static bool isInt64Immediate(SDNode *N, uint64_t &Imm) {
  if (N->getOpcode() == ISD::Constant && N->getValueType(0) == MVT::i64) {
    Imm = cast<ConstantSDNode>(N)->getZExtValue();
    return true;
  }
  return false;
}

// isInt32Immediate - This method tests to see if a constant operand.
// If so Imm will receive the 32 bit value.
static bool isInt32Immediate(SDValue N, unsigned &Imm) {
  return isInt32Immediate(N.getNode(), Imm);
}

/// isInt64Immediate - This method tests to see if the value is a 64-bit
/// constant operand. If so Imm will receive the 64-bit value.
static bool isInt64Immediate(SDValue N, uint64_t &Imm) {
  return isInt64Immediate(N.getNode(), Imm);
}

static unsigned getBranchHint(unsigned PCC, FunctionLoweringInfo *FuncInfo,
                              const SDValue &DestMBB) {
  assert(isa<BasicBlockSDNode>(DestMBB));

  if (!FuncInfo->BPI) return PPC::BR_NO_HINT;

  const BasicBlock *BB = FuncInfo->MBB->getBasicBlock();
  const Instruction *BBTerm = BB->getTerminator();

  if (BBTerm->getNumSuccessors() != 2) return PPC::BR_NO_HINT;

  const BasicBlock *TBB = BBTerm->getSuccessor(0);
  const BasicBlock *FBB = BBTerm->getSuccessor(1);

  auto TProb = FuncInfo->BPI->getEdgeProbability(BB, TBB);
  auto FProb = FuncInfo->BPI->getEdgeProbability(BB, FBB);

  // We only want to handle cases which are easy to predict at static time, e.g.
  // C++ throw statement, that is very likely not taken, or calling never
  // returned function, e.g. stdlib exit(). So we set Threshold to filter
  // unwanted cases.
  //
  // Below is LLVM branch weight table, we only want to handle case 1, 2
  //
  // Case                  Taken:Nontaken  Example
  // 1. Unreachable        1048575:1       C++ throw, stdlib exit(),
  // 2. Invoke-terminating 1:1048575
  // 3. Coldblock          4:64            __builtin_expect
  // 4. Loop Branch        124:4           For loop
  // 5. PH/ZH/FPH          20:12
  const uint32_t Threshold = 10000;

  if (std::max(TProb, FProb) / Threshold < std::min(TProb, FProb))
    return PPC::BR_NO_HINT;

  LLVM_DEBUG(dbgs() << "Use branch hint for '" << FuncInfo->Fn->getName()
                    << "::" << BB->getName() << "'\n"
                    << " -> " << TBB->getName() << ": " << TProb << "\n"
                    << " -> " << FBB->getName() << ": " << FProb << "\n");

  const BasicBlockSDNode *BBDN = cast<BasicBlockSDNode>(DestMBB);

  // If Dest BasicBlock is False-BasicBlock (FBB), swap branch probabilities,
  // because we want 'TProb' stands for 'branch probability' to Dest BasicBlock
  if (BBDN->getBasicBlock()->getBasicBlock() != TBB)
    std::swap(TProb, FProb);

  return (TProb > FProb) ? PPC::BR_TAKEN_HINT : PPC::BR_NONTAKEN_HINT;
}

// isOpcWithIntImmediate - This method tests to see if the node is a specific
// opcode and that it has a immediate integer right operand.
// If so Imm will receive the 32 bit value.
static bool isOpcWithIntImmediate(SDNode *N, unsigned Opc, unsigned& Imm) {
  return N->getOpcode() == Opc
         && isInt32Immediate(N->getOperand(1).getNode(), Imm);
}

void PPCDAGToDAGISel::selectFrameIndex(SDNode *SN, SDNode *N, unsigned Offset) {
  SDLoc dl(SN);
  int FI = cast<FrameIndexSDNode>(N)->getIndex();
  SDValue TFI = CurDAG->getTargetFrameIndex(FI, N->getValueType(0));
  unsigned Opc = N->getValueType(0) == MVT::i32 ? PPC::ADDI : PPC::ADDI8;
  if (SN->hasOneUse())
    CurDAG->SelectNodeTo(SN, Opc, N->getValueType(0), TFI,
                         getSmallIPtrImm(Offset, dl));
  else
    ReplaceNode(SN, CurDAG->getMachineNode(Opc, dl, N->getValueType(0), TFI,
                                           getSmallIPtrImm(Offset, dl)));
}

bool PPCDAGToDAGISel::isRotateAndMask(SDNode *N, unsigned Mask,
                                      bool isShiftMask, unsigned &SH,
                                      unsigned &MB, unsigned &ME) {
  // Don't even go down this path for i64, since different logic will be
  // necessary for rldicl/rldicr/rldimi.
  if (N->getValueType(0) != MVT::i32)
    return false;

  unsigned Shift  = 32;
  unsigned Indeterminant = ~0;  // bit mask marking indeterminant results
  unsigned Opcode = N->getOpcode();
  if (N->getNumOperands() != 2 ||
      !isInt32Immediate(N->getOperand(1).getNode(), Shift) || (Shift > 31))
    return false;

  if (Opcode == ISD::SHL) {
    // apply shift left to mask if it comes first
    if (isShiftMask) Mask = Mask << Shift;
    // determine which bits are made indeterminant by shift
    Indeterminant = ~(0xFFFFFFFFu << Shift);
  } else if (Opcode == ISD::SRL) {
    // apply shift right to mask if it comes first
    if (isShiftMask) Mask = Mask >> Shift;
    // determine which bits are made indeterminant by shift
    Indeterminant = ~(0xFFFFFFFFu >> Shift);
    // adjust for the left rotate
    Shift = 32 - Shift;
  } else if (Opcode == ISD::ROTL) {
    Indeterminant = 0;
  } else {
    return false;
  }

  // if the mask doesn't intersect any Indeterminant bits
  if (Mask && !(Mask & Indeterminant)) {
    SH = Shift & 31;
    // make sure the mask is still a mask (wrap arounds may not be)
    return isRunOfOnes(Mask, MB, ME);
  }
  return false;
}

bool PPCDAGToDAGISel::tryTLSXFormStore(StoreSDNode *ST) {
  SDValue Base = ST->getBasePtr();
  if (Base.getOpcode() != PPCISD::ADD_TLS)
    return false;
  SDValue Offset = ST->getOffset();
  if (!Offset.isUndef())
    return false;

  SDLoc dl(ST);
  EVT MemVT = ST->getMemoryVT();
  EVT RegVT = ST->getValue().getValueType();

  unsigned Opcode;
  switch (MemVT.getSimpleVT().SimpleTy) {
    default:
      return false;
    case MVT::i8: {
      Opcode = (RegVT == MVT::i32) ? PPC::STBXTLS_32 : PPC::STBXTLS;
      break;
    }
    case MVT::i16: {
      Opcode = (RegVT == MVT::i32) ? PPC::STHXTLS_32 : PPC::STHXTLS;
      break;
    }
    case MVT::i32: {
      Opcode = (RegVT == MVT::i32) ? PPC::STWXTLS_32 : PPC::STWXTLS;
      break;
    }
    case MVT::i64: {
      Opcode = PPC::STDXTLS;
      break;
    }
  }
  SDValue Chain = ST->getChain();
  SDVTList VTs = ST->getVTList();
  SDValue Ops[] = {ST->getValue(), Base.getOperand(0), Base.getOperand(1),
                   Chain};
  SDNode *MN = CurDAG->getMachineNode(Opcode, dl, VTs, Ops);
  transferMemOperands(ST, MN);
  ReplaceNode(ST, MN);
  return true;
}

bool PPCDAGToDAGISel::tryTLSXFormLoad(LoadSDNode *LD) {
  SDValue Base = LD->getBasePtr();
  if (Base.getOpcode() != PPCISD::ADD_TLS)
    return false;
  SDValue Offset = LD->getOffset();
  if (!Offset.isUndef())
    return false;

  SDLoc dl(LD);
  EVT MemVT = LD->getMemoryVT();
  EVT RegVT = LD->getValueType(0);
  unsigned Opcode;
  switch (MemVT.getSimpleVT().SimpleTy) {
    default:
      return false;
    case MVT::i8: {
      Opcode = (RegVT == MVT::i32) ? PPC::LBZXTLS_32 : PPC::LBZXTLS;
      break;
    }
    case MVT::i16: {
      Opcode = (RegVT == MVT::i32) ? PPC::LHZXTLS_32 : PPC::LHZXTLS;
      break;
    }
    case MVT::i32: {
      Opcode = (RegVT == MVT::i32) ? PPC::LWZXTLS_32 : PPC::LWZXTLS;
      break;
    }
    case MVT::i64: {
      Opcode = PPC::LDXTLS;
      break;
    }
  }
  SDValue Chain = LD->getChain();
  SDVTList VTs = LD->getVTList();
  SDValue Ops[] = {Base.getOperand(0), Base.getOperand(1), Chain};
  SDNode *MN = CurDAG->getMachineNode(Opcode, dl, VTs, Ops);
  transferMemOperands(LD, MN);
  ReplaceNode(LD, MN);
  return true;
}

/// Turn an or of two masked values into the rotate left word immediate then
/// mask insert (rlwimi) instruction.
bool PPCDAGToDAGISel::tryBitfieldInsert(SDNode *N) {
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDLoc dl(N);

  KnownBits LKnown = CurDAG->computeKnownBits(Op0);
  KnownBits RKnown = CurDAG->computeKnownBits(Op1);

  unsigned TargetMask = LKnown.Zero.getZExtValue();
  unsigned InsertMask = RKnown.Zero.getZExtValue();

  if ((TargetMask | InsertMask) == 0xFFFFFFFF) {
    unsigned Op0Opc = Op0.getOpcode();
    unsigned Op1Opc = Op1.getOpcode();
    unsigned Value, SH = 0;
    TargetMask = ~TargetMask;
    InsertMask = ~InsertMask;

    // If the LHS has a foldable shift and the RHS does not, then swap it to the
    // RHS so that we can fold the shift into the insert.
    if (Op0Opc == ISD::AND && Op1Opc == ISD::AND) {
      if (Op0.getOperand(0).getOpcode() == ISD::SHL ||
          Op0.getOperand(0).getOpcode() == ISD::SRL) {
        if (Op1.getOperand(0).getOpcode() != ISD::SHL &&
            Op1.getOperand(0).getOpcode() != ISD::SRL) {
          std::swap(Op0, Op1);
          std::swap(Op0Opc, Op1Opc);
          std::swap(TargetMask, InsertMask);
        }
      }
    } else if (Op0Opc == ISD::SHL || Op0Opc == ISD::SRL) {
      if (Op1Opc == ISD::AND && Op1.getOperand(0).getOpcode() != ISD::SHL &&
          Op1.getOperand(0).getOpcode() != ISD::SRL) {
        std::swap(Op0, Op1);
        std::swap(Op0Opc, Op1Opc);
        std::swap(TargetMask, InsertMask);
      }
    }

    unsigned MB, ME;
    if (isRunOfOnes(InsertMask, MB, ME)) {
      if ((Op1Opc == ISD::SHL || Op1Opc == ISD::SRL) &&
          isInt32Immediate(Op1.getOperand(1), Value)) {
        Op1 = Op1.getOperand(0);
        SH  = (Op1Opc == ISD::SHL) ? Value : 32 - Value;
      }
      if (Op1Opc == ISD::AND) {
       // The AND mask might not be a constant, and we need to make sure that
       // if we're going to fold the masking with the insert, all bits not
       // know to be zero in the mask are known to be one.
        KnownBits MKnown = CurDAG->computeKnownBits(Op1.getOperand(1));
        bool CanFoldMask = InsertMask == MKnown.One.getZExtValue();

        unsigned SHOpc = Op1.getOperand(0).getOpcode();
        if ((SHOpc == ISD::SHL || SHOpc == ISD::SRL) && CanFoldMask &&
            isInt32Immediate(Op1.getOperand(0).getOperand(1), Value)) {
          // Note that Value must be in range here (less than 32) because
          // otherwise there would not be any bits set in InsertMask.
          Op1 = Op1.getOperand(0).getOperand(0);
          SH  = (SHOpc == ISD::SHL) ? Value : 32 - Value;
        }
      }

      SH &= 31;
      SDValue Ops[] = { Op0, Op1, getI32Imm(SH, dl), getI32Imm(MB, dl),
                          getI32Imm(ME, dl) };
      ReplaceNode(N, CurDAG->getMachineNode(PPC::RLWIMI, dl, MVT::i32, Ops));
      return true;
    }
  }
  return false;
}

// Predict the number of instructions that would be generated by calling
// selectI64Imm(N).
static unsigned selectI64ImmInstrCountDirect(int64_t Imm) {
  // Assume no remaining bits.
  unsigned Remainder = 0;
  // Assume no shift required.
  unsigned Shift = 0;

  // If it can't be represented as a 32 bit value.
  if (!isInt<32>(Imm)) {
    Shift = countTrailingZeros<uint64_t>(Imm);
    int64_t ImmSh = static_cast<uint64_t>(Imm) >> Shift;

    // If the shifted value fits 32 bits.
    if (isInt<32>(ImmSh)) {
      // Go with the shifted value.
      Imm = ImmSh;
    } else {
      // Still stuck with a 64 bit value.
      Remainder = Imm;
      Shift = 32;
      Imm >>= 32;
    }
  }

  // Intermediate operand.
  unsigned Result = 0;

  // Handle first 32 bits.
  unsigned Lo = Imm & 0xFFFF;

  // Simple value.
  if (isInt<16>(Imm)) {
    // Just the Lo bits.
    ++Result;
  } else if (Lo) {
    // Handle the Hi bits and Lo bits.
    Result += 2;
  } else {
    // Just the Hi bits.
    ++Result;
  }

  // If no shift, we're done.
  if (!Shift) return Result;

  // If Hi word == Lo word,
  // we can use rldimi to insert the Lo word into Hi word.
  if ((unsigned)(Imm & 0xFFFFFFFF) == Remainder) {
    ++Result;
    return Result;
  }

  // Shift for next step if the upper 32-bits were not zero.
  if (Imm)
    ++Result;

  // Add in the last bits as required.
  if ((Remainder >> 16) & 0xFFFF)
    ++Result;
  if (Remainder & 0xFFFF)
    ++Result;

  return Result;
}

static uint64_t Rot64(uint64_t Imm, unsigned R) {
  return (Imm << R) | (Imm >> (64 - R));
}

static unsigned selectI64ImmInstrCount(int64_t Imm) {
  unsigned Count = selectI64ImmInstrCountDirect(Imm);

  // If the instruction count is 1 or 2, we do not need further analysis
  // since rotate + load constant requires at least 2 instructions.
  if (Count <= 2)
    return Count;

  for (unsigned r = 1; r < 63; ++r) {
    uint64_t RImm = Rot64(Imm, r);
    unsigned RCount = selectI64ImmInstrCountDirect(RImm) + 1;
    Count = std::min(Count, RCount);

    // See comments in selectI64Imm for an explanation of the logic below.
    unsigned LS = findLastSet(RImm);
    if (LS != r-1)
      continue;

    uint64_t OnesMask = -(int64_t) (UINT64_C(1) << (LS+1));
    uint64_t RImmWithOnes = RImm | OnesMask;

    RCount = selectI64ImmInstrCountDirect(RImmWithOnes) + 1;
    Count = std::min(Count, RCount);
  }

  return Count;
}

// Select a 64-bit constant. For cost-modeling purposes, selectI64ImmInstrCount
// (above) needs to be kept in sync with this function.
static SDNode *selectI64ImmDirect(SelectionDAG *CurDAG, const SDLoc &dl,
                                  int64_t Imm) {
  // Assume no remaining bits.
  unsigned Remainder = 0;
  // Assume no shift required.
  unsigned Shift = 0;

  // If it can't be represented as a 32 bit value.
  if (!isInt<32>(Imm)) {
    Shift = countTrailingZeros<uint64_t>(Imm);
    int64_t ImmSh = static_cast<uint64_t>(Imm) >> Shift;

    // If the shifted value fits 32 bits.
    if (isInt<32>(ImmSh)) {
      // Go with the shifted value.
      Imm = ImmSh;
    } else {
      // Still stuck with a 64 bit value.
      Remainder = Imm;
      Shift = 32;
      Imm >>= 32;
    }
  }

  // Intermediate operand.
  SDNode *Result;

  // Handle first 32 bits.
  unsigned Lo = Imm & 0xFFFF;
  unsigned Hi = (Imm >> 16) & 0xFFFF;

  auto getI32Imm = [CurDAG, dl](unsigned Imm) {
      return CurDAG->getTargetConstant(Imm, dl, MVT::i32);
  };

  // Simple value.
  if (isInt<16>(Imm)) {
    uint64_t SextImm = SignExtend64(Lo, 16);
    SDValue SDImm = CurDAG->getTargetConstant(SextImm, dl, MVT::i64);
    // Just the Lo bits.
    Result = CurDAG->getMachineNode(PPC::LI8, dl, MVT::i64, SDImm);
  } else if (Lo) {
    // Handle the Hi bits.
    unsigned OpC = Hi ? PPC::LIS8 : PPC::LI8;
    Result = CurDAG->getMachineNode(OpC, dl, MVT::i64, getI32Imm(Hi));
    // And Lo bits.
    Result = CurDAG->getMachineNode(PPC::ORI8, dl, MVT::i64,
                                    SDValue(Result, 0), getI32Imm(Lo));
  } else {
    // Just the Hi bits.
    Result = CurDAG->getMachineNode(PPC::LIS8, dl, MVT::i64, getI32Imm(Hi));
  }

  // If no shift, we're done.
  if (!Shift) return Result;

  // If Hi word == Lo word,
  // we can use rldimi to insert the Lo word into Hi word.
  if ((unsigned)(Imm & 0xFFFFFFFF) == Remainder) {
    SDValue Ops[] =
      { SDValue(Result, 0), SDValue(Result, 0), getI32Imm(Shift), getI32Imm(0)};
    return CurDAG->getMachineNode(PPC::RLDIMI, dl, MVT::i64, Ops);
  }

  // Shift for next step if the upper 32-bits were not zero.
  if (Imm) {
    Result = CurDAG->getMachineNode(PPC::RLDICR, dl, MVT::i64,
                                    SDValue(Result, 0),
                                    getI32Imm(Shift),
                                    getI32Imm(63 - Shift));
  }

  // Add in the last bits as required.
  if ((Hi = (Remainder >> 16) & 0xFFFF)) {
    Result = CurDAG->getMachineNode(PPC::ORIS8, dl, MVT::i64,
                                    SDValue(Result, 0), getI32Imm(Hi));
  }
  if ((Lo = Remainder & 0xFFFF)) {
    Result = CurDAG->getMachineNode(PPC::ORI8, dl, MVT::i64,
                                    SDValue(Result, 0), getI32Imm(Lo));
  }

  return Result;
}

static SDNode *selectI64Imm(SelectionDAG *CurDAG, const SDLoc &dl,
                            int64_t Imm) {
  unsigned Count = selectI64ImmInstrCountDirect(Imm);

  // If the instruction count is 1 or 2, we do not need further analysis
  // since rotate + load constant requires at least 2 instructions.
  if (Count <= 2)
    return selectI64ImmDirect(CurDAG, dl, Imm);

  unsigned RMin = 0;

  int64_t MatImm;
  unsigned MaskEnd;

  for (unsigned r = 1; r < 63; ++r) {
    uint64_t RImm = Rot64(Imm, r);
    unsigned RCount = selectI64ImmInstrCountDirect(RImm) + 1;
    if (RCount < Count) {
      Count = RCount;
      RMin = r;
      MatImm = RImm;
      MaskEnd = 63;
    }

    // If the immediate to generate has many trailing zeros, it might be
    // worthwhile to generate a rotated value with too many leading ones
    // (because that's free with li/lis's sign-extension semantics), and then
    // mask them off after rotation.

    unsigned LS = findLastSet(RImm);
    // We're adding (63-LS) higher-order ones, and we expect to mask them off
    // after performing the inverse rotation by (64-r). So we need that:
    //   63-LS == 64-r => LS == r-1
    if (LS != r-1)
      continue;

    uint64_t OnesMask = -(int64_t) (UINT64_C(1) << (LS+1));
    uint64_t RImmWithOnes = RImm | OnesMask;

    RCount = selectI64ImmInstrCountDirect(RImmWithOnes) + 1;
    if (RCount < Count) {
      Count = RCount;
      RMin = r;
      MatImm = RImmWithOnes;
      MaskEnd = LS;
    }
  }

  if (!RMin)
    return selectI64ImmDirect(CurDAG, dl, Imm);

  auto getI32Imm = [CurDAG, dl](unsigned Imm) {
      return CurDAG->getTargetConstant(Imm, dl, MVT::i32);
  };

  SDValue Val = SDValue(selectI64ImmDirect(CurDAG, dl, MatImm), 0);
  return CurDAG->getMachineNode(PPC::RLDICR, dl, MVT::i64, Val,
                                getI32Imm(64 - RMin), getI32Imm(MaskEnd));
}

static unsigned allUsesTruncate(SelectionDAG *CurDAG, SDNode *N) {
  unsigned MaxTruncation = 0;
  // Cannot use range-based for loop here as we need the actual use (i.e. we
  // need the operand number corresponding to the use). A range-based for
  // will unbox the use and provide an SDNode*.
  for (SDNode::use_iterator Use = N->use_begin(), UseEnd = N->use_end();
       Use != UseEnd; ++Use) {
    unsigned Opc =
      Use->isMachineOpcode() ? Use->getMachineOpcode() : Use->getOpcode();
    switch (Opc) {
    default: return 0;
    case ISD::TRUNCATE:
      if (Use->isMachineOpcode())
        return 0;
      MaxTruncation =
        std::max(MaxTruncation, Use->getValueType(0).getSizeInBits());
      continue;
    case ISD::STORE: {
      if (Use->isMachineOpcode())
        return 0;
      StoreSDNode *STN = cast<StoreSDNode>(*Use);
      unsigned MemVTSize = STN->getMemoryVT().getSizeInBits();
      if (MemVTSize == 64 || Use.getOperandNo() != 0)
        return 0;
      MaxTruncation = std::max(MaxTruncation, MemVTSize);
      continue;
    }
    case PPC::STW8:
    case PPC::STWX8:
    case PPC::STWU8:
    case PPC::STWUX8:
      if (Use.getOperandNo() != 0)
        return 0;
      MaxTruncation = std::max(MaxTruncation, 32u);
      continue;
    case PPC::STH8:
    case PPC::STHX8:
    case PPC::STHU8:
    case PPC::STHUX8:
      if (Use.getOperandNo() != 0)
        return 0;
      MaxTruncation = std::max(MaxTruncation, 16u);
      continue;
    case PPC::STB8:
    case PPC::STBX8:
    case PPC::STBU8:
    case PPC::STBUX8:
      if (Use.getOperandNo() != 0)
        return 0;
      MaxTruncation = std::max(MaxTruncation, 8u);
      continue;
    }
  }
  return MaxTruncation;
}

// Select a 64-bit constant.
static SDNode *selectI64Imm(SelectionDAG *CurDAG, SDNode *N) {
  SDLoc dl(N);

  // Get 64 bit value.
  int64_t Imm = cast<ConstantSDNode>(N)->getZExtValue();
  if (unsigned MinSize = allUsesTruncate(CurDAG, N)) {
    uint64_t SextImm = SignExtend64(Imm, MinSize);
    SDValue SDImm = CurDAG->getTargetConstant(SextImm, dl, MVT::i64);
    if (isInt<16>(SextImm))
      return CurDAG->getMachineNode(PPC::LI8, dl, MVT::i64, SDImm);
  }
  return selectI64Imm(CurDAG, dl, Imm);
}

namespace {

class BitPermutationSelector {
  struct ValueBit {
    SDValue V;

    // The bit number in the value, using a convention where bit 0 is the
    // lowest-order bit.
    unsigned Idx;

    // ConstZero means a bit we need to mask off.
    // Variable is a bit comes from an input variable.
    // VariableKnownToBeZero is also a bit comes from an input variable,
    // but it is known to be already zero. So we do not need to mask them.
    enum Kind {
      ConstZero,
      Variable,
      VariableKnownToBeZero
    } K;

    ValueBit(SDValue V, unsigned I, Kind K = Variable)
      : V(V), Idx(I), K(K) {}
    ValueBit(Kind K = Variable)
      : V(SDValue(nullptr, 0)), Idx(UINT32_MAX), K(K) {}

    bool isZero() const {
      return K == ConstZero || K == VariableKnownToBeZero;
    }

    bool hasValue() const {
      return K == Variable || K == VariableKnownToBeZero;
    }

    SDValue getValue() const {
      assert(hasValue() && "Cannot get the value of a constant bit");
      return V;
    }

    unsigned getValueBitIndex() const {
      assert(hasValue() && "Cannot get the value bit index of a constant bit");
      return Idx;
    }
  };

  // A bit group has the same underlying value and the same rotate factor.
  struct BitGroup {
    SDValue V;
    unsigned RLAmt;
    unsigned StartIdx, EndIdx;

    // This rotation amount assumes that the lower 32 bits of the quantity are
    // replicated in the high 32 bits by the rotation operator (which is done
    // by rlwinm and friends in 64-bit mode).
    bool Repl32;
    // Did converting to Repl32 == true change the rotation factor? If it did,
    // it decreased it by 32.
    bool Repl32CR;
    // Was this group coalesced after setting Repl32 to true?
    bool Repl32Coalesced;

    BitGroup(SDValue V, unsigned R, unsigned S, unsigned E)
      : V(V), RLAmt(R), StartIdx(S), EndIdx(E), Repl32(false), Repl32CR(false),
        Repl32Coalesced(false) {
      LLVM_DEBUG(dbgs() << "\tbit group for " << V.getNode() << " RLAmt = " << R
                        << " [" << S << ", " << E << "]\n");
    }
  };

  // Information on each (Value, RLAmt) pair (like the number of groups
  // associated with each) used to choose the lowering method.
  struct ValueRotInfo {
    SDValue V;
    unsigned RLAmt = std::numeric_limits<unsigned>::max();
    unsigned NumGroups = 0;
    unsigned FirstGroupStartIdx = std::numeric_limits<unsigned>::max();
    bool Repl32 = false;

    ValueRotInfo() = default;

    // For sorting (in reverse order) by NumGroups, and then by
    // FirstGroupStartIdx.
    bool operator < (const ValueRotInfo &Other) const {
      // We need to sort so that the non-Repl32 come first because, when we're
      // doing masking, the Repl32 bit groups might be subsumed into the 64-bit
      // masking operation.
      if (Repl32 < Other.Repl32)
        return true;
      else if (Repl32 > Other.Repl32)
        return false;
      else if (NumGroups > Other.NumGroups)
        return true;
      else if (NumGroups < Other.NumGroups)
        return false;
      else if (RLAmt == 0 && Other.RLAmt != 0)
        return true;
      else if (RLAmt != 0 && Other.RLAmt == 0)
        return false;
      else if (FirstGroupStartIdx < Other.FirstGroupStartIdx)
        return true;
      return false;
    }
  };

  using ValueBitsMemoizedValue = std::pair<bool, SmallVector<ValueBit, 64>>;
  using ValueBitsMemoizer =
      DenseMap<SDValue, std::unique_ptr<ValueBitsMemoizedValue>>;
  ValueBitsMemoizer Memoizer;

  // Return a pair of bool and a SmallVector pointer to a memoization entry.
  // The bool is true if something interesting was deduced, otherwise if we're
  // providing only a generic representation of V (or something else likewise
  // uninteresting for instruction selection) through the SmallVector.
  std::pair<bool, SmallVector<ValueBit, 64> *> getValueBits(SDValue V,
                                                            unsigned NumBits) {
    auto &ValueEntry = Memoizer[V];
    if (ValueEntry)
      return std::make_pair(ValueEntry->first, &ValueEntry->second);
    ValueEntry.reset(new ValueBitsMemoizedValue());
    bool &Interesting = ValueEntry->first;
    SmallVector<ValueBit, 64> &Bits = ValueEntry->second;
    Bits.resize(NumBits);

    switch (V.getOpcode()) {
    default: break;
    case ISD::ROTL:
      if (isa<ConstantSDNode>(V.getOperand(1))) {
        unsigned RotAmt = V.getConstantOperandVal(1);

        const auto &LHSBits = *getValueBits(V.getOperand(0), NumBits).second;

        for (unsigned i = 0; i < NumBits; ++i)
          Bits[i] = LHSBits[i < RotAmt ? i + (NumBits - RotAmt) : i - RotAmt];

        return std::make_pair(Interesting = true, &Bits);
      }
      break;
    case ISD::SHL:
      if (isa<ConstantSDNode>(V.getOperand(1))) {
        unsigned ShiftAmt = V.getConstantOperandVal(1);

        const auto &LHSBits = *getValueBits(V.getOperand(0), NumBits).second;

        for (unsigned i = ShiftAmt; i < NumBits; ++i)
          Bits[i] = LHSBits[i - ShiftAmt];

        for (unsigned i = 0; i < ShiftAmt; ++i)
          Bits[i] = ValueBit(ValueBit::ConstZero);

        return std::make_pair(Interesting = true, &Bits);
      }
      break;
    case ISD::SRL:
      if (isa<ConstantSDNode>(V.getOperand(1))) {
        unsigned ShiftAmt = V.getConstantOperandVal(1);

        const auto &LHSBits = *getValueBits(V.getOperand(0), NumBits).second;

        for (unsigned i = 0; i < NumBits - ShiftAmt; ++i)
          Bits[i] = LHSBits[i + ShiftAmt];

        for (unsigned i = NumBits - ShiftAmt; i < NumBits; ++i)
          Bits[i] = ValueBit(ValueBit::ConstZero);

        return std::make_pair(Interesting = true, &Bits);
      }
      break;
    case ISD::AND:
      if (isa<ConstantSDNode>(V.getOperand(1))) {
        uint64_t Mask = V.getConstantOperandVal(1);

        const SmallVector<ValueBit, 64> *LHSBits;
        // Mark this as interesting, only if the LHS was also interesting. This
        // prevents the overall procedure from matching a single immediate 'and'
        // (which is non-optimal because such an and might be folded with other
        // things if we don't select it here).
        std::tie(Interesting, LHSBits) = getValueBits(V.getOperand(0), NumBits);

        for (unsigned i = 0; i < NumBits; ++i)
          if (((Mask >> i) & 1) == 1)
            Bits[i] = (*LHSBits)[i];
          else {
            // AND instruction masks this bit. If the input is already zero,
            // we have nothing to do here. Otherwise, make the bit ConstZero.
            if ((*LHSBits)[i].isZero())
              Bits[i] = (*LHSBits)[i];
            else
              Bits[i] = ValueBit(ValueBit::ConstZero);
          }

        return std::make_pair(Interesting, &Bits);
      }
      break;
    case ISD::OR: {
      const auto &LHSBits = *getValueBits(V.getOperand(0), NumBits).second;
      const auto &RHSBits = *getValueBits(V.getOperand(1), NumBits).second;

      bool AllDisjoint = true;
      SDValue LastVal = SDValue();
      unsigned LastIdx = 0;
      for (unsigned i = 0; i < NumBits; ++i) {
        if (LHSBits[i].isZero() && RHSBits[i].isZero()) {
          // If both inputs are known to be zero and one is ConstZero and
          // another is VariableKnownToBeZero, we can select whichever
          // we like. To minimize the number of bit groups, we select
          // VariableKnownToBeZero if this bit is the next bit of the same
          // input variable from the previous bit. Otherwise, we select
          // ConstZero.
          if (LHSBits[i].hasValue() && LHSBits[i].getValue() == LastVal &&
              LHSBits[i].getValueBitIndex() == LastIdx + 1)
            Bits[i] = LHSBits[i];
          else if (RHSBits[i].hasValue() && RHSBits[i].getValue() == LastVal &&
                   RHSBits[i].getValueBitIndex() == LastIdx + 1)
            Bits[i] = RHSBits[i];
          else
            Bits[i] = ValueBit(ValueBit::ConstZero);
        }
        else if (LHSBits[i].isZero())
          Bits[i] = RHSBits[i];
        else if (RHSBits[i].isZero())
          Bits[i] = LHSBits[i];
        else {
          AllDisjoint = false;
          break;
        }
        // We remember the value and bit index of this bit.
        if (Bits[i].hasValue()) {
          LastVal = Bits[i].getValue();
          LastIdx = Bits[i].getValueBitIndex();
        }
        else {
          if (LastVal) LastVal = SDValue();
          LastIdx = 0;
        }
      }

      if (!AllDisjoint)
        break;

      return std::make_pair(Interesting = true, &Bits);
    }
    case ISD::ZERO_EXTEND: {
      // We support only the case with zero extension from i32 to i64 so far.
      if (V.getValueType() != MVT::i64 ||
          V.getOperand(0).getValueType() != MVT::i32)
        break;

      const SmallVector<ValueBit, 64> *LHSBits;
      const unsigned NumOperandBits = 32;
      std::tie(Interesting, LHSBits) = getValueBits(V.getOperand(0),
                                                    NumOperandBits);

      for (unsigned i = 0; i < NumOperandBits; ++i)
        Bits[i] = (*LHSBits)[i];

      for (unsigned i = NumOperandBits; i < NumBits; ++i)
        Bits[i] = ValueBit(ValueBit::ConstZero);

      return std::make_pair(Interesting, &Bits);
    }
    case ISD::TRUNCATE: {
      EVT FromType = V.getOperand(0).getValueType();
      EVT ToType = V.getValueType();
      // We support only the case with truncate from i64 to i32.
      if (FromType != MVT::i64 || ToType != MVT::i32)
        break;
      const unsigned NumAllBits = FromType.getSizeInBits();
      SmallVector<ValueBit, 64> *InBits;
      std::tie(Interesting, InBits) = getValueBits(V.getOperand(0),
                                                    NumAllBits);
      const unsigned NumValidBits = ToType.getSizeInBits();

      // A 32-bit instruction cannot touch upper 32-bit part of 64-bit value.
      // So, we cannot include this truncate.
      bool UseUpper32bit = false;
      for (unsigned i = 0; i < NumValidBits; ++i)
        if ((*InBits)[i].hasValue() && (*InBits)[i].getValueBitIndex() >= 32) {
          UseUpper32bit = true;
          break;
        }
      if (UseUpper32bit)
        break;

      for (unsigned i = 0; i < NumValidBits; ++i)
        Bits[i] = (*InBits)[i];

      return std::make_pair(Interesting, &Bits);
    }
    case ISD::AssertZext: {
      // For AssertZext, we look through the operand and
      // mark the bits known to be zero.
      const SmallVector<ValueBit, 64> *LHSBits;
      std::tie(Interesting, LHSBits) = getValueBits(V.getOperand(0),
                                                    NumBits);

      EVT FromType = cast<VTSDNode>(V.getOperand(1))->getVT();
      const unsigned NumValidBits = FromType.getSizeInBits();
      for (unsigned i = 0; i < NumValidBits; ++i)
        Bits[i] = (*LHSBits)[i];

      // These bits are known to be zero.
      for (unsigned i = NumValidBits; i < NumBits; ++i)
        Bits[i] = ValueBit((*LHSBits)[i].getValue(),
                           (*LHSBits)[i].getValueBitIndex(),
                           ValueBit::VariableKnownToBeZero);

      return std::make_pair(Interesting, &Bits);
    }
    case ISD::LOAD:
      LoadSDNode *LD = cast<LoadSDNode>(V);
      if (ISD::isZEXTLoad(V.getNode()) && V.getResNo() == 0) {
        EVT VT = LD->getMemoryVT();
        const unsigned NumValidBits = VT.getSizeInBits();

        for (unsigned i = 0; i < NumValidBits; ++i)
          Bits[i] = ValueBit(V, i);

        // These bits are known to be zero.
        for (unsigned i = NumValidBits; i < NumBits; ++i)
          Bits[i] = ValueBit(V, i, ValueBit::VariableKnownToBeZero);

        // Zero-extending load itself cannot be optimized. So, it is not
        // interesting by itself though it gives useful information.
        return std::make_pair(Interesting = false, &Bits);
      }
      break;
    }

    for (unsigned i = 0; i < NumBits; ++i)
      Bits[i] = ValueBit(V, i);

    return std::make_pair(Interesting = false, &Bits);
  }

  // For each value (except the constant ones), compute the left-rotate amount
  // to get it from its original to final position.
  void computeRotationAmounts() {
    NeedMask = false;
    RLAmt.resize(Bits.size());
    for (unsigned i = 0; i < Bits.size(); ++i)
      if (Bits[i].hasValue()) {
        unsigned VBI = Bits[i].getValueBitIndex();
        if (i >= VBI)
          RLAmt[i] = i - VBI;
        else
          RLAmt[i] = Bits.size() - (VBI - i);
      } else if (Bits[i].isZero()) {
        NeedMask = true;
        RLAmt[i] = UINT32_MAX;
      } else {
        llvm_unreachable("Unknown value bit type");
      }
  }

  // Collect groups of consecutive bits with the same underlying value and
  // rotation factor. If we're doing late masking, we ignore zeros, otherwise
  // they break up groups.
  void collectBitGroups(bool LateMask) {
    BitGroups.clear();

    unsigned LastRLAmt = RLAmt[0];
    SDValue LastValue = Bits[0].hasValue() ? Bits[0].getValue() : SDValue();
    unsigned LastGroupStartIdx = 0;
    bool IsGroupOfZeros = !Bits[LastGroupStartIdx].hasValue();
    for (unsigned i = 1; i < Bits.size(); ++i) {
      unsigned ThisRLAmt = RLAmt[i];
      SDValue ThisValue = Bits[i].hasValue() ? Bits[i].getValue() : SDValue();
      if (LateMask && !ThisValue) {
        ThisValue = LastValue;
        ThisRLAmt = LastRLAmt;
        // If we're doing late masking, then the first bit group always starts
        // at zero (even if the first bits were zero).
        if (BitGroups.empty())
          LastGroupStartIdx = 0;
      }

      // If this bit is known to be zero and the current group is a bit group
      // of zeros, we do not need to terminate the current bit group even the
      // Value or RLAmt does not match here. Instead, we terminate this group
      // when the first non-zero bit appears later.
      if (IsGroupOfZeros && Bits[i].isZero())
        continue;

      // If this bit has the same underlying value and the same rotate factor as
      // the last one, then they're part of the same group.
      if (ThisRLAmt == LastRLAmt && ThisValue == LastValue)
        // We cannot continue the current group if this bits is not known to
        // be zero in a bit group of zeros.
        if (!(IsGroupOfZeros && ThisValue && !Bits[i].isZero()))
          continue;

      if (LastValue.getNode())
        BitGroups.push_back(BitGroup(LastValue, LastRLAmt, LastGroupStartIdx,
                                     i-1));
      LastRLAmt = ThisRLAmt;
      LastValue = ThisValue;
      LastGroupStartIdx = i;
      IsGroupOfZeros = !Bits[LastGroupStartIdx].hasValue();
    }
    if (LastValue.getNode())
      BitGroups.push_back(BitGroup(LastValue, LastRLAmt, LastGroupStartIdx,
                                   Bits.size()-1));

    if (BitGroups.empty())
      return;

    // We might be able to combine the first and last groups.
    if (BitGroups.size() > 1) {
      // If the first and last groups are the same, then remove the first group
      // in favor of the last group, making the ending index of the last group
      // equal to the ending index of the to-be-removed first group.
      if (BitGroups[0].StartIdx == 0 &&
          BitGroups[BitGroups.size()-1].EndIdx == Bits.size()-1 &&
          BitGroups[0].V == BitGroups[BitGroups.size()-1].V &&
          BitGroups[0].RLAmt == BitGroups[BitGroups.size()-1].RLAmt) {
        LLVM_DEBUG(dbgs() << "\tcombining final bit group with initial one\n");
        BitGroups[BitGroups.size()-1].EndIdx = BitGroups[0].EndIdx;
        BitGroups.erase(BitGroups.begin());
      }
    }
  }

  // Take all (SDValue, RLAmt) pairs and sort them by the number of groups
  // associated with each. If the number of groups are same, we prefer a group
  // which does not require rotate, i.e. RLAmt is 0, to avoid the first rotate
  // instruction. If there is a degeneracy, pick the one that occurs
  // first (in the final value).
  void collectValueRotInfo() {
    ValueRots.clear();

    for (auto &BG : BitGroups) {
      unsigned RLAmtKey = BG.RLAmt + (BG.Repl32 ? 64 : 0);
      ValueRotInfo &VRI = ValueRots[std::make_pair(BG.V, RLAmtKey)];
      VRI.V = BG.V;
      VRI.RLAmt = BG.RLAmt;
      VRI.Repl32 = BG.Repl32;
      VRI.NumGroups += 1;
      VRI.FirstGroupStartIdx = std::min(VRI.FirstGroupStartIdx, BG.StartIdx);
    }

    // Now that we've collected the various ValueRotInfo instances, we need to
    // sort them.
    ValueRotsVec.clear();
    for (auto &I : ValueRots) {
      ValueRotsVec.push_back(I.second);
    }
    llvm::sort(ValueRotsVec);
  }

  // In 64-bit mode, rlwinm and friends have a rotation operator that
  // replicates the low-order 32 bits into the high-order 32-bits. The mask
  // indices of these instructions can only be in the lower 32 bits, so they
  // can only represent some 64-bit bit groups. However, when they can be used,
  // the 32-bit replication can be used to represent, as a single bit group,
  // otherwise separate bit groups. We'll convert to replicated-32-bit bit
  // groups when possible. Returns true if any of the bit groups were
  // converted.
  void assignRepl32BitGroups() {
    // If we have bits like this:
    //
    // Indices:    15 14 13 12 11 10 9 8  7  6  5  4  3  2  1  0
    // V bits: ... 7  6  5  4  3  2  1 0 31 30 29 28 27 26 25 24
    // Groups:    |      RLAmt = 8      |      RLAmt = 40       |
    //
    // But, making use of a 32-bit operation that replicates the low-order 32
    // bits into the high-order 32 bits, this can be one bit group with a RLAmt
    // of 8.

    auto IsAllLow32 = [this](BitGroup & BG) {
      if (BG.StartIdx <= BG.EndIdx) {
        for (unsigned i = BG.StartIdx; i <= BG.EndIdx; ++i) {
          if (!Bits[i].hasValue())
            continue;
          if (Bits[i].getValueBitIndex() >= 32)
            return false;
        }
      } else {
        for (unsigned i = BG.StartIdx; i < Bits.size(); ++i) {
          if (!Bits[i].hasValue())
            continue;
          if (Bits[i].getValueBitIndex() >= 32)
            return false;
        }
        for (unsigned i = 0; i <= BG.EndIdx; ++i) {
          if (!Bits[i].hasValue())
            continue;
          if (Bits[i].getValueBitIndex() >= 32)
            return false;
        }
      }

      return true;
    };

    for (auto &BG : BitGroups) {
      // If this bit group has RLAmt of 0 and will not be merged with
      // another bit group, we don't benefit from Repl32. We don't mark
      // such group to give more freedom for later instruction selection.
      if (BG.RLAmt == 0) {
        auto PotentiallyMerged = [this](BitGroup & BG) {
          for (auto &BG2 : BitGroups)
            if (&BG != &BG2 && BG.V == BG2.V &&
                (BG2.RLAmt == 0 || BG2.RLAmt == 32))
              return true;
          return false;
        };
        if (!PotentiallyMerged(BG))
          continue;
      }
      if (BG.StartIdx < 32 && BG.EndIdx < 32) {
        if (IsAllLow32(BG)) {
          if (BG.RLAmt >= 32) {
            BG.RLAmt -= 32;
            BG.Repl32CR = true;
          }

          BG.Repl32 = true;

          LLVM_DEBUG(dbgs() << "\t32-bit replicated bit group for "
                            << BG.V.getNode() << " RLAmt = " << BG.RLAmt << " ["
                            << BG.StartIdx << ", " << BG.EndIdx << "]\n");
        }
      }
    }

    // Now walk through the bit groups, consolidating where possible.
    for (auto I = BitGroups.begin(); I != BitGroups.end();) {
      // We might want to remove this bit group by merging it with the previous
      // group (which might be the ending group).
      auto IP = (I == BitGroups.begin()) ?
                std::prev(BitGroups.end()) : std::prev(I);
      if (I->Repl32 && IP->Repl32 && I->V == IP->V && I->RLAmt == IP->RLAmt &&
          I->StartIdx == (IP->EndIdx + 1) % 64 && I != IP) {

        LLVM_DEBUG(dbgs() << "\tcombining 32-bit replicated bit group for "
                          << I->V.getNode() << " RLAmt = " << I->RLAmt << " ["
                          << I->StartIdx << ", " << I->EndIdx
                          << "] with group with range [" << IP->StartIdx << ", "
                          << IP->EndIdx << "]\n");

        IP->EndIdx = I->EndIdx;
        IP->Repl32CR = IP->Repl32CR || I->Repl32CR;
        IP->Repl32Coalesced = true;
        I = BitGroups.erase(I);
        continue;
      } else {
        // There is a special case worth handling: If there is a single group
        // covering the entire upper 32 bits, and it can be merged with both
        // the next and previous groups (which might be the same group), then
        // do so. If it is the same group (so there will be only one group in
        // total), then we need to reverse the order of the range so that it
        // covers the entire 64 bits.
        if (I->StartIdx == 32 && I->EndIdx == 63) {
          assert(std::next(I) == BitGroups.end() &&
                 "bit group ends at index 63 but there is another?");
          auto IN = BitGroups.begin();

          if (IP->Repl32 && IN->Repl32 && I->V == IP->V && I->V == IN->V &&
              (I->RLAmt % 32) == IP->RLAmt && (I->RLAmt % 32) == IN->RLAmt &&
              IP->EndIdx == 31 && IN->StartIdx == 0 && I != IP &&
              IsAllLow32(*I)) {

            LLVM_DEBUG(dbgs() << "\tcombining bit group for " << I->V.getNode()
                              << " RLAmt = " << I->RLAmt << " [" << I->StartIdx
                              << ", " << I->EndIdx
                              << "] with 32-bit replicated groups with ranges ["
                              << IP->StartIdx << ", " << IP->EndIdx << "] and ["
                              << IN->StartIdx << ", " << IN->EndIdx << "]\n");

            if (IP == IN) {
              // There is only one other group; change it to cover the whole
              // range (backward, so that it can still be Repl32 but cover the
              // whole 64-bit range).
              IP->StartIdx = 31;
              IP->EndIdx = 30;
              IP->Repl32CR = IP->Repl32CR || I->RLAmt >= 32;
              IP->Repl32Coalesced = true;
              I = BitGroups.erase(I);
            } else {
              // There are two separate groups, one before this group and one
              // after us (at the beginning). We're going to remove this group,
              // but also the group at the very beginning.
              IP->EndIdx = IN->EndIdx;
              IP->Repl32CR = IP->Repl32CR || IN->Repl32CR || I->RLAmt >= 32;
              IP->Repl32Coalesced = true;
              I = BitGroups.erase(I);
              BitGroups.erase(BitGroups.begin());
            }

            // This must be the last group in the vector (and we might have
            // just invalidated the iterator above), so break here.
            break;
          }
        }
      }

      ++I;
    }
  }

  SDValue getI32Imm(unsigned Imm, const SDLoc &dl) {
    return CurDAG->getTargetConstant(Imm, dl, MVT::i32);
  }

  uint64_t getZerosMask() {
    uint64_t Mask = 0;
    for (unsigned i = 0; i < Bits.size(); ++i) {
      if (Bits[i].hasValue())
        continue;
      Mask |= (UINT64_C(1) << i);
    }

    return ~Mask;
  }

  // This method extends an input value to 64 bit if input is 32-bit integer.
  // While selecting instructions in BitPermutationSelector in 64-bit mode,
  // an input value can be a 32-bit integer if a ZERO_EXTEND node is included.
  // In such case, we extend it to 64 bit to be consistent with other values.
  SDValue ExtendToInt64(SDValue V, const SDLoc &dl) {
    if (V.getValueSizeInBits() == 64)
      return V;

    assert(V.getValueSizeInBits() == 32);
    SDValue SubRegIdx = CurDAG->getTargetConstant(PPC::sub_32, dl, MVT::i32);
    SDValue ImDef = SDValue(CurDAG->getMachineNode(PPC::IMPLICIT_DEF, dl,
                                                   MVT::i64), 0);
    SDValue ExtVal = SDValue(CurDAG->getMachineNode(PPC::INSERT_SUBREG, dl,
                                                    MVT::i64, ImDef, V,
                                                    SubRegIdx), 0);
    return ExtVal;
  }

  SDValue TruncateToInt32(SDValue V, const SDLoc &dl) {
    if (V.getValueSizeInBits() == 32)
      return V;

    assert(V.getValueSizeInBits() == 64);
    SDValue SubRegIdx = CurDAG->getTargetConstant(PPC::sub_32, dl, MVT::i32);
    SDValue SubVal = SDValue(CurDAG->getMachineNode(PPC::EXTRACT_SUBREG, dl,
                                                    MVT::i32, V, SubRegIdx), 0);
    return SubVal;
  }

  // Depending on the number of groups for a particular value, it might be
  // better to rotate, mask explicitly (using andi/andis), and then or the
  // result. Select this part of the result first.
  void SelectAndParts32(const SDLoc &dl, SDValue &Res, unsigned *InstCnt) {
    if (BPermRewriterNoMasking)
      return;

    for (ValueRotInfo &VRI : ValueRotsVec) {
      unsigned Mask = 0;
      for (unsigned i = 0; i < Bits.size(); ++i) {
        if (!Bits[i].hasValue() || Bits[i].getValue() != VRI.V)
          continue;
        if (RLAmt[i] != VRI.RLAmt)
          continue;
        Mask |= (1u << i);
      }

      // Compute the masks for andi/andis that would be necessary.
      unsigned ANDIMask = (Mask & UINT16_MAX), ANDISMask = Mask >> 16;
      assert((ANDIMask != 0 || ANDISMask != 0) &&
             "No set bits in mask for value bit groups");
      bool NeedsRotate = VRI.RLAmt != 0;

      // We're trying to minimize the number of instructions. If we have one
      // group, using one of andi/andis can break even.  If we have three
      // groups, we can use both andi and andis and break even (to use both
      // andi and andis we also need to or the results together). We need four
      // groups if we also need to rotate. To use andi/andis we need to do more
      // than break even because rotate-and-mask instructions tend to be easier
      // to schedule.

      // FIXME: We've biased here against using andi/andis, which is right for
      // POWER cores, but not optimal everywhere. For example, on the A2,
      // andi/andis have single-cycle latency whereas the rotate-and-mask
      // instructions take two cycles, and it would be better to bias toward
      // andi/andis in break-even cases.

      unsigned NumAndInsts = (unsigned) NeedsRotate +
                             (unsigned) (ANDIMask != 0) +
                             (unsigned) (ANDISMask != 0) +
                             (unsigned) (ANDIMask != 0 && ANDISMask != 0) +
                             (unsigned) (bool) Res;

      LLVM_DEBUG(dbgs() << "\t\trotation groups for " << VRI.V.getNode()
                        << " RL: " << VRI.RLAmt << ":"
                        << "\n\t\t\tisel using masking: " << NumAndInsts
                        << " using rotates: " << VRI.NumGroups << "\n");

      if (NumAndInsts >= VRI.NumGroups)
        continue;

      LLVM_DEBUG(dbgs() << "\t\t\t\tusing masking\n");

      if (InstCnt) *InstCnt += NumAndInsts;

      SDValue VRot;
      if (VRI.RLAmt) {
        SDValue Ops[] =
          { TruncateToInt32(VRI.V, dl), getI32Imm(VRI.RLAmt, dl),
            getI32Imm(0, dl), getI32Imm(31, dl) };
        VRot = SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32,
                                              Ops), 0);
      } else {
        VRot = TruncateToInt32(VRI.V, dl);
      }

      SDValue ANDIVal, ANDISVal;
      if (ANDIMask != 0)
        ANDIVal = SDValue(CurDAG->getMachineNode(PPC::ANDIo, dl, MVT::i32,
                            VRot, getI32Imm(ANDIMask, dl)), 0);
      if (ANDISMask != 0)
        ANDISVal = SDValue(CurDAG->getMachineNode(PPC::ANDISo, dl, MVT::i32,
                             VRot, getI32Imm(ANDISMask, dl)), 0);

      SDValue TotalVal;
      if (!ANDIVal)
        TotalVal = ANDISVal;
      else if (!ANDISVal)
        TotalVal = ANDIVal;
      else
        TotalVal = SDValue(CurDAG->getMachineNode(PPC::OR, dl, MVT::i32,
                             ANDIVal, ANDISVal), 0);

      if (!Res)
        Res = TotalVal;
      else
        Res = SDValue(CurDAG->getMachineNode(PPC::OR, dl, MVT::i32,
                        Res, TotalVal), 0);

      // Now, remove all groups with this underlying value and rotation
      // factor.
      eraseMatchingBitGroups([VRI](const BitGroup &BG) {
        return BG.V == VRI.V && BG.RLAmt == VRI.RLAmt;
      });
    }
  }

  // Instruction selection for the 32-bit case.
  SDNode *Select32(SDNode *N, bool LateMask, unsigned *InstCnt) {
    SDLoc dl(N);
    SDValue Res;

    if (InstCnt) *InstCnt = 0;

    // Take care of cases that should use andi/andis first.
    SelectAndParts32(dl, Res, InstCnt);

    // If we've not yet selected a 'starting' instruction, and we have no zeros
    // to fill in, select the (Value, RLAmt) with the highest priority (largest
    // number of groups), and start with this rotated value.
    if ((!NeedMask || LateMask) && !Res) {
      ValueRotInfo &VRI = ValueRotsVec[0];
      if (VRI.RLAmt) {
        if (InstCnt) *InstCnt += 1;
        SDValue Ops[] =
          { TruncateToInt32(VRI.V, dl), getI32Imm(VRI.RLAmt, dl),
            getI32Imm(0, dl), getI32Imm(31, dl) };
        Res = SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, Ops),
                      0);
      } else {
        Res = TruncateToInt32(VRI.V, dl);
      }

      // Now, remove all groups with this underlying value and rotation factor.
      eraseMatchingBitGroups([VRI](const BitGroup &BG) {
        return BG.V == VRI.V && BG.RLAmt == VRI.RLAmt;
      });
    }

    if (InstCnt) *InstCnt += BitGroups.size();

    // Insert the other groups (one at a time).
    for (auto &BG : BitGroups) {
      if (!Res) {
        SDValue Ops[] =
          { TruncateToInt32(BG.V, dl), getI32Imm(BG.RLAmt, dl),
            getI32Imm(Bits.size() - BG.EndIdx - 1, dl),
            getI32Imm(Bits.size() - BG.StartIdx - 1, dl) };
        Res = SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, Ops), 0);
      } else {
        SDValue Ops[] =
          { Res, TruncateToInt32(BG.V, dl), getI32Imm(BG.RLAmt, dl),
              getI32Imm(Bits.size() - BG.EndIdx - 1, dl),
            getI32Imm(Bits.size() - BG.StartIdx - 1, dl) };
        Res = SDValue(CurDAG->getMachineNode(PPC::RLWIMI, dl, MVT::i32, Ops), 0);
      }
    }

    if (LateMask) {
      unsigned Mask = (unsigned) getZerosMask();

      unsigned ANDIMask = (Mask & UINT16_MAX), ANDISMask = Mask >> 16;
      assert((ANDIMask != 0 || ANDISMask != 0) &&
             "No set bits in zeros mask?");

      if (InstCnt) *InstCnt += (unsigned) (ANDIMask != 0) +
                               (unsigned) (ANDISMask != 0) +
                               (unsigned) (ANDIMask != 0 && ANDISMask != 0);

      SDValue ANDIVal, ANDISVal;
      if (ANDIMask != 0)
        ANDIVal = SDValue(CurDAG->getMachineNode(PPC::ANDIo, dl, MVT::i32,
                            Res, getI32Imm(ANDIMask, dl)), 0);
      if (ANDISMask != 0)
        ANDISVal = SDValue(CurDAG->getMachineNode(PPC::ANDISo, dl, MVT::i32,
                             Res, getI32Imm(ANDISMask, dl)), 0);

      if (!ANDIVal)
        Res = ANDISVal;
      else if (!ANDISVal)
        Res = ANDIVal;
      else
        Res = SDValue(CurDAG->getMachineNode(PPC::OR, dl, MVT::i32,
                        ANDIVal, ANDISVal), 0);
    }

    return Res.getNode();
  }

  unsigned SelectRotMask64Count(unsigned RLAmt, bool Repl32,
                                unsigned MaskStart, unsigned MaskEnd,
                                bool IsIns) {
    // In the notation used by the instructions, 'start' and 'end' are reversed
    // because bits are counted from high to low order.
    unsigned InstMaskStart = 64 - MaskEnd - 1,
             InstMaskEnd   = 64 - MaskStart - 1;

    if (Repl32)
      return 1;

    if ((!IsIns && (InstMaskEnd == 63 || InstMaskStart == 0)) ||
        InstMaskEnd == 63 - RLAmt)
      return 1;

    return 2;
  }

  // For 64-bit values, not all combinations of rotates and masks are
  // available. Produce one if it is available.
  SDValue SelectRotMask64(SDValue V, const SDLoc &dl, unsigned RLAmt,
                          bool Repl32, unsigned MaskStart, unsigned MaskEnd,
                          unsigned *InstCnt = nullptr) {
    // In the notation used by the instructions, 'start' and 'end' are reversed
    // because bits are counted from high to low order.
    unsigned InstMaskStart = 64 - MaskEnd - 1,
             InstMaskEnd   = 64 - MaskStart - 1;

    if (InstCnt) *InstCnt += 1;

    if (Repl32) {
      // This rotation amount assumes that the lower 32 bits of the quantity
      // are replicated in the high 32 bits by the rotation operator (which is
      // done by rlwinm and friends).
      assert(InstMaskStart >= 32 && "Mask cannot start out of range");
      assert(InstMaskEnd   >= 32 && "Mask cannot end out of range");
      SDValue Ops[] =
        { ExtendToInt64(V, dl), getI32Imm(RLAmt, dl),
          getI32Imm(InstMaskStart - 32, dl), getI32Imm(InstMaskEnd - 32, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLWINM8, dl, MVT::i64,
                                            Ops), 0);
    }

    if (InstMaskEnd == 63) {
      SDValue Ops[] =
        { ExtendToInt64(V, dl), getI32Imm(RLAmt, dl),
          getI32Imm(InstMaskStart, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, Ops), 0);
    }

    if (InstMaskStart == 0) {
      SDValue Ops[] =
        { ExtendToInt64(V, dl), getI32Imm(RLAmt, dl),
          getI32Imm(InstMaskEnd, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLDICR, dl, MVT::i64, Ops), 0);
    }

    if (InstMaskEnd == 63 - RLAmt) {
      SDValue Ops[] =
        { ExtendToInt64(V, dl), getI32Imm(RLAmt, dl),
          getI32Imm(InstMaskStart, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLDIC, dl, MVT::i64, Ops), 0);
    }

    // We cannot do this with a single instruction, so we'll use two. The
    // problem is that we're not free to choose both a rotation amount and mask
    // start and end independently. We can choose an arbitrary mask start and
    // end, but then the rotation amount is fixed. Rotation, however, can be
    // inverted, and so by applying an "inverse" rotation first, we can get the
    // desired result.
    if (InstCnt) *InstCnt += 1;

    // The rotation mask for the second instruction must be MaskStart.
    unsigned RLAmt2 = MaskStart;
    // The first instruction must rotate V so that the overall rotation amount
    // is RLAmt.
    unsigned RLAmt1 = (64 + RLAmt - RLAmt2) % 64;
    if (RLAmt1)
      V = SelectRotMask64(V, dl, RLAmt1, false, 0, 63);
    return SelectRotMask64(V, dl, RLAmt2, false, MaskStart, MaskEnd);
  }

  // For 64-bit values, not all combinations of rotates and masks are
  // available. Produce a rotate-mask-and-insert if one is available.
  SDValue SelectRotMaskIns64(SDValue Base, SDValue V, const SDLoc &dl,
                             unsigned RLAmt, bool Repl32, unsigned MaskStart,
                             unsigned MaskEnd, unsigned *InstCnt = nullptr) {
    // In the notation used by the instructions, 'start' and 'end' are reversed
    // because bits are counted from high to low order.
    unsigned InstMaskStart = 64 - MaskEnd - 1,
             InstMaskEnd   = 64 - MaskStart - 1;

    if (InstCnt) *InstCnt += 1;

    if (Repl32) {
      // This rotation amount assumes that the lower 32 bits of the quantity
      // are replicated in the high 32 bits by the rotation operator (which is
      // done by rlwinm and friends).
      assert(InstMaskStart >= 32 && "Mask cannot start out of range");
      assert(InstMaskEnd   >= 32 && "Mask cannot end out of range");
      SDValue Ops[] =
        { ExtendToInt64(Base, dl), ExtendToInt64(V, dl), getI32Imm(RLAmt, dl),
          getI32Imm(InstMaskStart - 32, dl), getI32Imm(InstMaskEnd - 32, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLWIMI8, dl, MVT::i64,
                                            Ops), 0);
    }

    if (InstMaskEnd == 63 - RLAmt) {
      SDValue Ops[] =
        { ExtendToInt64(Base, dl), ExtendToInt64(V, dl), getI32Imm(RLAmt, dl),
          getI32Imm(InstMaskStart, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLDIMI, dl, MVT::i64, Ops), 0);
    }

    // We cannot do this with a single instruction, so we'll use two. The
    // problem is that we're not free to choose both a rotation amount and mask
    // start and end independently. We can choose an arbitrary mask start and
    // end, but then the rotation amount is fixed. Rotation, however, can be
    // inverted, and so by applying an "inverse" rotation first, we can get the
    // desired result.
    if (InstCnt) *InstCnt += 1;

    // The rotation mask for the second instruction must be MaskStart.
    unsigned RLAmt2 = MaskStart;
    // The first instruction must rotate V so that the overall rotation amount
    // is RLAmt.
    unsigned RLAmt1 = (64 + RLAmt - RLAmt2) % 64;
    if (RLAmt1)
      V = SelectRotMask64(V, dl, RLAmt1, false, 0, 63);
    return SelectRotMaskIns64(Base, V, dl, RLAmt2, false, MaskStart, MaskEnd);
  }

  void SelectAndParts64(const SDLoc &dl, SDValue &Res, unsigned *InstCnt) {
    if (BPermRewriterNoMasking)
      return;

    // The idea here is the same as in the 32-bit version, but with additional
    // complications from the fact that Repl32 might be true. Because we
    // aggressively convert bit groups to Repl32 form (which, for small
    // rotation factors, involves no other change), and then coalesce, it might
    // be the case that a single 64-bit masking operation could handle both
    // some Repl32 groups and some non-Repl32 groups. If converting to Repl32
    // form allowed coalescing, then we must use a 32-bit rotaton in order to
    // completely capture the new combined bit group.

    for (ValueRotInfo &VRI : ValueRotsVec) {
      uint64_t Mask = 0;

      // We need to add to the mask all bits from the associated bit groups.
      // If Repl32 is false, we need to add bits from bit groups that have
      // Repl32 true, but are trivially convertable to Repl32 false. Such a
      // group is trivially convertable if it overlaps only with the lower 32
      // bits, and the group has not been coalesced.
      auto MatchingBG = [VRI](const BitGroup &BG) {
        if (VRI.V != BG.V)
          return false;

        unsigned EffRLAmt = BG.RLAmt;
        if (!VRI.Repl32 && BG.Repl32) {
          if (BG.StartIdx < 32 && BG.EndIdx < 32 && BG.StartIdx <= BG.EndIdx &&
              !BG.Repl32Coalesced) {
            if (BG.Repl32CR)
              EffRLAmt += 32;
          } else {
            return false;
          }
        } else if (VRI.Repl32 != BG.Repl32) {
          return false;
        }

        return VRI.RLAmt == EffRLAmt;
      };

      for (auto &BG : BitGroups) {
        if (!MatchingBG(BG))
          continue;

        if (BG.StartIdx <= BG.EndIdx) {
          for (unsigned i = BG.StartIdx; i <= BG.EndIdx; ++i)
            Mask |= (UINT64_C(1) << i);
        } else {
          for (unsigned i = BG.StartIdx; i < Bits.size(); ++i)
            Mask |= (UINT64_C(1) << i);
          for (unsigned i = 0; i <= BG.EndIdx; ++i)
            Mask |= (UINT64_C(1) << i);
        }
      }

      // We can use the 32-bit andi/andis technique if the mask does not
      // require any higher-order bits. This can save an instruction compared
      // to always using the general 64-bit technique.
      bool Use32BitInsts = isUInt<32>(Mask);
      // Compute the masks for andi/andis that would be necessary.
      unsigned ANDIMask = (Mask & UINT16_MAX),
               ANDISMask = (Mask >> 16) & UINT16_MAX;

      bool NeedsRotate = VRI.RLAmt || (VRI.Repl32 && !isUInt<32>(Mask));

      unsigned NumAndInsts = (unsigned) NeedsRotate +
                             (unsigned) (bool) Res;
      if (Use32BitInsts)
        NumAndInsts += (unsigned) (ANDIMask != 0) + (unsigned) (ANDISMask != 0) +
                       (unsigned) (ANDIMask != 0 && ANDISMask != 0);
      else
        NumAndInsts += selectI64ImmInstrCount(Mask) + /* and */ 1;

      unsigned NumRLInsts = 0;
      bool FirstBG = true;
      bool MoreBG = false;
      for (auto &BG : BitGroups) {
        if (!MatchingBG(BG)) {
          MoreBG = true;
          continue;
        }
        NumRLInsts +=
          SelectRotMask64Count(BG.RLAmt, BG.Repl32, BG.StartIdx, BG.EndIdx,
                               !FirstBG);
        FirstBG = false;
      }

      LLVM_DEBUG(dbgs() << "\t\trotation groups for " << VRI.V.getNode()
                        << " RL: " << VRI.RLAmt << (VRI.Repl32 ? " (32):" : ":")
                        << "\n\t\t\tisel using masking: " << NumAndInsts
                        << " using rotates: " << NumRLInsts << "\n");

      // When we'd use andi/andis, we bias toward using the rotates (andi only
      // has a record form, and is cracked on POWER cores). However, when using
      // general 64-bit constant formation, bias toward the constant form,
      // because that exposes more opportunities for CSE.
      if (NumAndInsts > NumRLInsts)
        continue;
      // When merging multiple bit groups, instruction or is used.
      // But when rotate is used, rldimi can inert the rotated value into any
      // register, so instruction or can be avoided.
      if ((Use32BitInsts || MoreBG) && NumAndInsts == NumRLInsts)
        continue;

      LLVM_DEBUG(dbgs() << "\t\t\t\tusing masking\n");

      if (InstCnt) *InstCnt += NumAndInsts;

      SDValue VRot;
      // We actually need to generate a rotation if we have a non-zero rotation
      // factor or, in the Repl32 case, if we care about any of the
      // higher-order replicated bits. In the latter case, we generate a mask
      // backward so that it actually includes the entire 64 bits.
      if (VRI.RLAmt || (VRI.Repl32 && !isUInt<32>(Mask)))
        VRot = SelectRotMask64(VRI.V, dl, VRI.RLAmt, VRI.Repl32,
                               VRI.Repl32 ? 31 : 0, VRI.Repl32 ? 30 : 63);
      else
        VRot = VRI.V;

      SDValue TotalVal;
      if (Use32BitInsts) {
        assert((ANDIMask != 0 || ANDISMask != 0) &&
               "No set bits in mask when using 32-bit ands for 64-bit value");

        SDValue ANDIVal, ANDISVal;
        if (ANDIMask != 0)
          ANDIVal = SDValue(CurDAG->getMachineNode(PPC::ANDIo8, dl, MVT::i64,
                                                   ExtendToInt64(VRot, dl),
                                                   getI32Imm(ANDIMask, dl)),
                            0);
        if (ANDISMask != 0)
          ANDISVal = SDValue(CurDAG->getMachineNode(PPC::ANDISo8, dl, MVT::i64,
                                                    ExtendToInt64(VRot, dl),
                                                    getI32Imm(ANDISMask, dl)),
                             0);

        if (!ANDIVal)
          TotalVal = ANDISVal;
        else if (!ANDISVal)
          TotalVal = ANDIVal;
        else
          TotalVal = SDValue(CurDAG->getMachineNode(PPC::OR8, dl, MVT::i64,
                               ExtendToInt64(ANDIVal, dl), ANDISVal), 0);
      } else {
        TotalVal = SDValue(selectI64Imm(CurDAG, dl, Mask), 0);
        TotalVal =
          SDValue(CurDAG->getMachineNode(PPC::AND8, dl, MVT::i64,
                                         ExtendToInt64(VRot, dl), TotalVal),
                  0);
     }

      if (!Res)
        Res = TotalVal;
      else
        Res = SDValue(CurDAG->getMachineNode(PPC::OR8, dl, MVT::i64,
                                             ExtendToInt64(Res, dl), TotalVal),
                      0);

      // Now, remove all groups with this underlying value and rotation
      // factor.
      eraseMatchingBitGroups(MatchingBG);
    }
  }

  // Instruction selection for the 64-bit case.
  SDNode *Select64(SDNode *N, bool LateMask, unsigned *InstCnt) {
    SDLoc dl(N);
    SDValue Res;

    if (InstCnt) *InstCnt = 0;

    // Take care of cases that should use andi/andis first.
    SelectAndParts64(dl, Res, InstCnt);

    // If we've not yet selected a 'starting' instruction, and we have no zeros
    // to fill in, select the (Value, RLAmt) with the highest priority (largest
    // number of groups), and start with this rotated value.
    if ((!NeedMask || LateMask) && !Res) {
      // If we have both Repl32 groups and non-Repl32 groups, the non-Repl32
      // groups will come first, and so the VRI representing the largest number
      // of groups might not be first (it might be the first Repl32 groups).
      unsigned MaxGroupsIdx = 0;
      if (!ValueRotsVec[0].Repl32) {
        for (unsigned i = 0, ie = ValueRotsVec.size(); i < ie; ++i)
          if (ValueRotsVec[i].Repl32) {
            if (ValueRotsVec[i].NumGroups > ValueRotsVec[0].NumGroups)
              MaxGroupsIdx = i;
            break;
          }
      }

      ValueRotInfo &VRI = ValueRotsVec[MaxGroupsIdx];
      bool NeedsRotate = false;
      if (VRI.RLAmt) {
        NeedsRotate = true;
      } else if (VRI.Repl32) {
        for (auto &BG : BitGroups) {
          if (BG.V != VRI.V || BG.RLAmt != VRI.RLAmt ||
              BG.Repl32 != VRI.Repl32)
            continue;

          // We don't need a rotate if the bit group is confined to the lower
          // 32 bits.
          if (BG.StartIdx < 32 && BG.EndIdx < 32 && BG.StartIdx < BG.EndIdx)
            continue;

          NeedsRotate = true;
          break;
        }
      }

      if (NeedsRotate)
        Res = SelectRotMask64(VRI.V, dl, VRI.RLAmt, VRI.Repl32,
                              VRI.Repl32 ? 31 : 0, VRI.Repl32 ? 30 : 63,
                              InstCnt);
      else
        Res = VRI.V;

      // Now, remove all groups with this underlying value and rotation factor.
      if (Res)
        eraseMatchingBitGroups([VRI](const BitGroup &BG) {
          return BG.V == VRI.V && BG.RLAmt == VRI.RLAmt &&
                 BG.Repl32 == VRI.Repl32;
        });
    }

    // Because 64-bit rotates are more flexible than inserts, we might have a
    // preference regarding which one we do first (to save one instruction).
    if (!Res)
      for (auto I = BitGroups.begin(), IE = BitGroups.end(); I != IE; ++I) {
        if (SelectRotMask64Count(I->RLAmt, I->Repl32, I->StartIdx, I->EndIdx,
                                false) <
            SelectRotMask64Count(I->RLAmt, I->Repl32, I->StartIdx, I->EndIdx,
                                true)) {
          if (I != BitGroups.begin()) {
            BitGroup BG = *I;
            BitGroups.erase(I);
            BitGroups.insert(BitGroups.begin(), BG);
          }

          break;
        }
      }

    // Insert the other groups (one at a time).
    for (auto &BG : BitGroups) {
      if (!Res)
        Res = SelectRotMask64(BG.V, dl, BG.RLAmt, BG.Repl32, BG.StartIdx,
                              BG.EndIdx, InstCnt);
      else
        Res = SelectRotMaskIns64(Res, BG.V, dl, BG.RLAmt, BG.Repl32,
                                 BG.StartIdx, BG.EndIdx, InstCnt);
    }

    if (LateMask) {
      uint64_t Mask = getZerosMask();

      // We can use the 32-bit andi/andis technique if the mask does not
      // require any higher-order bits. This can save an instruction compared
      // to always using the general 64-bit technique.
      bool Use32BitInsts = isUInt<32>(Mask);
      // Compute the masks for andi/andis that would be necessary.
      unsigned ANDIMask = (Mask & UINT16_MAX),
               ANDISMask = (Mask >> 16) & UINT16_MAX;

      if (Use32BitInsts) {
        assert((ANDIMask != 0 || ANDISMask != 0) &&
               "No set bits in mask when using 32-bit ands for 64-bit value");

        if (InstCnt) *InstCnt += (unsigned) (ANDIMask != 0) +
                                 (unsigned) (ANDISMask != 0) +
                                 (unsigned) (ANDIMask != 0 && ANDISMask != 0);

        SDValue ANDIVal, ANDISVal;
        if (ANDIMask != 0)
          ANDIVal = SDValue(CurDAG->getMachineNode(PPC::ANDIo8, dl, MVT::i64,
                              ExtendToInt64(Res, dl), getI32Imm(ANDIMask, dl)), 0);
        if (ANDISMask != 0)
          ANDISVal = SDValue(CurDAG->getMachineNode(PPC::ANDISo8, dl, MVT::i64,
                               ExtendToInt64(Res, dl), getI32Imm(ANDISMask, dl)), 0);

        if (!ANDIVal)
          Res = ANDISVal;
        else if (!ANDISVal)
          Res = ANDIVal;
        else
          Res = SDValue(CurDAG->getMachineNode(PPC::OR8, dl, MVT::i64,
                          ExtendToInt64(ANDIVal, dl), ANDISVal), 0);
      } else {
        if (InstCnt) *InstCnt += selectI64ImmInstrCount(Mask) + /* and */ 1;

        SDValue MaskVal = SDValue(selectI64Imm(CurDAG, dl, Mask), 0);
        Res =
          SDValue(CurDAG->getMachineNode(PPC::AND8, dl, MVT::i64,
                                         ExtendToInt64(Res, dl), MaskVal), 0);
      }
    }

    return Res.getNode();
  }

  SDNode *Select(SDNode *N, bool LateMask, unsigned *InstCnt = nullptr) {
    // Fill in BitGroups.
    collectBitGroups(LateMask);
    if (BitGroups.empty())
      return nullptr;

    // For 64-bit values, figure out when we can use 32-bit instructions.
    if (Bits.size() == 64)
      assignRepl32BitGroups();

    // Fill in ValueRotsVec.
    collectValueRotInfo();

    if (Bits.size() == 32) {
      return Select32(N, LateMask, InstCnt);
    } else {
      assert(Bits.size() == 64 && "Not 64 bits here?");
      return Select64(N, LateMask, InstCnt);
    }

    return nullptr;
  }

  void eraseMatchingBitGroups(function_ref<bool(const BitGroup &)> F) {
    BitGroups.erase(remove_if(BitGroups, F), BitGroups.end());
  }

  SmallVector<ValueBit, 64> Bits;

  bool NeedMask;
  SmallVector<unsigned, 64> RLAmt;

  SmallVector<BitGroup, 16> BitGroups;

  DenseMap<std::pair<SDValue, unsigned>, ValueRotInfo> ValueRots;
  SmallVector<ValueRotInfo, 16> ValueRotsVec;

  SelectionDAG *CurDAG;

public:
  BitPermutationSelector(SelectionDAG *DAG)
    : CurDAG(DAG) {}

  // Here we try to match complex bit permutations into a set of
  // rotate-and-shift/shift/and/or instructions, using a set of heuristics
  // known to produce optimial code for common cases (like i32 byte swapping).
  SDNode *Select(SDNode *N) {
    Memoizer.clear();
    auto Result =
        getValueBits(SDValue(N, 0), N->getValueType(0).getSizeInBits());
    if (!Result.first)
      return nullptr;
    Bits = std::move(*Result.second);

    LLVM_DEBUG(dbgs() << "Considering bit-permutation-based instruction"
                         " selection for:    ");
    LLVM_DEBUG(N->dump(CurDAG));

    // Fill it RLAmt and set NeedMask.
    computeRotationAmounts();

    if (!NeedMask)
      return Select(N, false);

    // We currently have two techniques for handling results with zeros: early
    // masking (the default) and late masking. Late masking is sometimes more
    // efficient, but because the structure of the bit groups is different, it
    // is hard to tell without generating both and comparing the results. With
    // late masking, we ignore zeros in the resulting value when inserting each
    // set of bit groups, and then mask in the zeros at the end. With early
    // masking, we only insert the non-zero parts of the result at every step.

    unsigned InstCnt = 0, InstCntLateMask = 0;
    LLVM_DEBUG(dbgs() << "\tEarly masking:\n");
    SDNode *RN = Select(N, false, &InstCnt);
    LLVM_DEBUG(dbgs() << "\t\tisel would use " << InstCnt << " instructions\n");

    LLVM_DEBUG(dbgs() << "\tLate masking:\n");
    SDNode *RNLM = Select(N, true, &InstCntLateMask);
    LLVM_DEBUG(dbgs() << "\t\tisel would use " << InstCntLateMask
                      << " instructions\n");

    if (InstCnt <= InstCntLateMask) {
      LLVM_DEBUG(dbgs() << "\tUsing early-masking for isel\n");
      return RN;
    }

    LLVM_DEBUG(dbgs() << "\tUsing late-masking for isel\n");
    return RNLM;
  }
};

class IntegerCompareEliminator {
  SelectionDAG *CurDAG;
  PPCDAGToDAGISel *S;
  // Conversion type for interpreting results of a 32-bit instruction as
  // a 64-bit value or vice versa.
  enum ExtOrTruncConversion { Ext, Trunc };

  // Modifiers to guide how an ISD::SETCC node's result is to be computed
  // in a GPR.
  // ZExtOrig - use the original condition code, zero-extend value
  // ZExtInvert - invert the condition code, zero-extend value
  // SExtOrig - use the original condition code, sign-extend value
  // SExtInvert - invert the condition code, sign-extend value
  enum SetccInGPROpts { ZExtOrig, ZExtInvert, SExtOrig, SExtInvert };

  // Comparisons against zero to emit GPR code sequences for. Each of these
  // sequences may need to be emitted for two or more equivalent patterns.
  // For example (a >= 0) == (a > -1). The direction of the comparison (</>)
  // matters as well as the extension type: sext (-1/0), zext (1/0).
  // GEZExt - (zext (LHS >= 0))
  // GESExt - (sext (LHS >= 0))
  // LEZExt - (zext (LHS <= 0))
  // LESExt - (sext (LHS <= 0))
  enum ZeroCompare { GEZExt, GESExt, LEZExt, LESExt };

  SDNode *tryEXTEND(SDNode *N);
  SDNode *tryLogicOpOfCompares(SDNode *N);
  SDValue computeLogicOpInGPR(SDValue LogicOp);
  SDValue signExtendInputIfNeeded(SDValue Input);
  SDValue zeroExtendInputIfNeeded(SDValue Input);
  SDValue addExtOrTrunc(SDValue NatWidthRes, ExtOrTruncConversion Conv);
  SDValue getCompoundZeroComparisonInGPR(SDValue LHS, SDLoc dl,
                                        ZeroCompare CmpTy);
  SDValue get32BitZExtCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                              int64_t RHSValue, SDLoc dl);
 SDValue get32BitSExtCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                              int64_t RHSValue, SDLoc dl);
  SDValue get64BitZExtCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                              int64_t RHSValue, SDLoc dl);
  SDValue get64BitSExtCompare(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                              int64_t RHSValue, SDLoc dl);
  SDValue getSETCCInGPR(SDValue Compare, SetccInGPROpts ConvOpts);

public:
  IntegerCompareEliminator(SelectionDAG *DAG,
                           PPCDAGToDAGISel *Sel) : CurDAG(DAG), S(Sel) {
    assert(CurDAG->getTargetLoweringInfo()
           .getPointerTy(CurDAG->getDataLayout()).getSizeInBits() == 64 &&
           "Only expecting to use this on 64 bit targets.");
  }
  SDNode *Select(SDNode *N) {
    if (CmpInGPR == ICGPR_None)
      return nullptr;
    switch (N->getOpcode()) {
    default: break;
    case ISD::ZERO_EXTEND:
      if (CmpInGPR == ICGPR_Sext || CmpInGPR == ICGPR_SextI32 ||
          CmpInGPR == ICGPR_SextI64)
        return nullptr;
      LLVM_FALLTHROUGH;
    case ISD::SIGN_EXTEND:
      if (CmpInGPR == ICGPR_Zext || CmpInGPR == ICGPR_ZextI32 ||
          CmpInGPR == ICGPR_ZextI64)
        return nullptr;
      return tryEXTEND(N);
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
      return tryLogicOpOfCompares(N);
    }
    return nullptr;
  }
};

static bool isLogicOp(unsigned Opc) {
  return Opc == ISD::AND || Opc == ISD::OR || Opc == ISD::XOR;
}
// The obvious case for wanting to keep the value in a GPR. Namely, the
// result of the comparison is actually needed in a GPR.
SDNode *IntegerCompareEliminator::tryEXTEND(SDNode *N) {
  assert((N->getOpcode() == ISD::ZERO_EXTEND ||
          N->getOpcode() == ISD::SIGN_EXTEND) &&
         "Expecting a zero/sign extend node!");
  SDValue WideRes;
  // If we are zero-extending the result of a logical operation on i1
  // values, we can keep the values in GPRs.
  if (isLogicOp(N->getOperand(0).getOpcode()) &&
      N->getOperand(0).getValueType() == MVT::i1 &&
      N->getOpcode() == ISD::ZERO_EXTEND)
    WideRes = computeLogicOpInGPR(N->getOperand(0));
  else if (N->getOperand(0).getOpcode() != ISD::SETCC)
    return nullptr;
  else
    WideRes =
      getSETCCInGPR(N->getOperand(0),
                    N->getOpcode() == ISD::SIGN_EXTEND ?
                    SetccInGPROpts::SExtOrig : SetccInGPROpts::ZExtOrig);

  if (!WideRes)
    return nullptr;

  SDLoc dl(N);
  bool Input32Bit = WideRes.getValueType() == MVT::i32;
  bool Output32Bit = N->getValueType(0) == MVT::i32;

  NumSextSetcc += N->getOpcode() == ISD::SIGN_EXTEND ? 1 : 0;
  NumZextSetcc += N->getOpcode() == ISD::SIGN_EXTEND ? 0 : 1;

  SDValue ConvOp = WideRes;
  if (Input32Bit != Output32Bit)
    ConvOp = addExtOrTrunc(WideRes, Input32Bit ? ExtOrTruncConversion::Ext :
                           ExtOrTruncConversion::Trunc);
  return ConvOp.getNode();
}

// Attempt to perform logical operations on the results of comparisons while
// keeping the values in GPRs. Without doing so, these would end up being
// lowered to CR-logical operations which suffer from significant latency and
// low ILP.
SDNode *IntegerCompareEliminator::tryLogicOpOfCompares(SDNode *N) {
  if (N->getValueType(0) != MVT::i1)
    return nullptr;
  assert(isLogicOp(N->getOpcode()) &&
         "Expected a logic operation on setcc results.");
  SDValue LoweredLogical = computeLogicOpInGPR(SDValue(N, 0));
  if (!LoweredLogical)
    return nullptr;

  SDLoc dl(N);
  bool IsBitwiseNegate = LoweredLogical.getMachineOpcode() == PPC::XORI8;
  unsigned SubRegToExtract = IsBitwiseNegate ? PPC::sub_eq : PPC::sub_gt;
  SDValue CR0Reg = CurDAG->getRegister(PPC::CR0, MVT::i32);
  SDValue LHS = LoweredLogical.getOperand(0);
  SDValue RHS = LoweredLogical.getOperand(1);
  SDValue WideOp;
  SDValue OpToConvToRecForm;

  // Look through any 32-bit to 64-bit implicit extend nodes to find the
  // opcode that is input to the XORI.
  if (IsBitwiseNegate &&
      LoweredLogical.getOperand(0).getMachineOpcode() == PPC::INSERT_SUBREG)
    OpToConvToRecForm = LoweredLogical.getOperand(0).getOperand(1);
  else if (IsBitwiseNegate)
    // If the input to the XORI isn't an extension, that's what we're after.
    OpToConvToRecForm = LoweredLogical.getOperand(0);
  else
    // If this is not an XORI, it is a reg-reg logical op and we can convert
    // it to record-form.
    OpToConvToRecForm = LoweredLogical;

  // Get the record-form version of the node we're looking to use to get the
  // CR result from.
  uint16_t NonRecOpc = OpToConvToRecForm.getMachineOpcode();
  int NewOpc = PPCInstrInfo::getRecordFormOpcode(NonRecOpc);

  // Convert the right node to record-form. This is either the logical we're
  // looking at or it is the input node to the negation (if we're looking at
  // a bitwise negation).
  if (NewOpc != -1 && IsBitwiseNegate) {
    // The input to the XORI has a record-form. Use it.
    assert(LoweredLogical.getConstantOperandVal(1) == 1 &&
           "Expected a PPC::XORI8 only for bitwise negation.");
    // Emit the record-form instruction.
    std::vector<SDValue> Ops;
    for (int i = 0, e = OpToConvToRecForm.getNumOperands(); i < e; i++)
      Ops.push_back(OpToConvToRecForm.getOperand(i));

    WideOp =
      SDValue(CurDAG->getMachineNode(NewOpc, dl,
                                     OpToConvToRecForm.getValueType(),
                                     MVT::Glue, Ops), 0);
  } else {
    assert((NewOpc != -1 || !IsBitwiseNegate) &&
           "No record form available for AND8/OR8/XOR8?");
    WideOp =
      SDValue(CurDAG->getMachineNode(NewOpc == -1 ? PPC::ANDIo8 : NewOpc, dl,
                                     MVT::i64, MVT::Glue, LHS, RHS), 0);
  }

  // Select this node to a single bit from CR0 set by the record-form node
  // just created. For bitwise negation, use the EQ bit which is the equivalent
  // of negating the result (i.e. it is a bit set when the result of the
  // operation is zero).
  SDValue SRIdxVal =
    CurDAG->getTargetConstant(SubRegToExtract, dl, MVT::i32);
  SDValue CRBit =
    SDValue(CurDAG->getMachineNode(TargetOpcode::EXTRACT_SUBREG, dl,
                                   MVT::i1, CR0Reg, SRIdxVal,
                                   WideOp.getValue(1)), 0);
  return CRBit.getNode();
}

// Lower a logical operation on i1 values into a GPR sequence if possible.
// The result can be kept in a GPR if requested.
// Three types of inputs can be handled:
// - SETCC
// - TRUNCATE
// - Logical operation (AND/OR/XOR)
// There is also a special case that is handled (namely a complement operation
// achieved with xor %a, -1).
SDValue IntegerCompareEliminator::computeLogicOpInGPR(SDValue LogicOp) {
  assert(isLogicOp(LogicOp.getOpcode()) &&
        "Can only handle logic operations here.");
  assert(LogicOp.getValueType() == MVT::i1 &&
         "Can only handle logic operations on i1 values here.");
  SDLoc dl(LogicOp);
  SDValue LHS, RHS;

 // Special case: xor %a, -1
  bool IsBitwiseNegation = isBitwiseNot(LogicOp);

  // Produces a GPR sequence for each operand of the binary logic operation.
  // For SETCC, it produces the respective comparison, for TRUNCATE it truncates
  // the value in a GPR and for logic operations, it will recursively produce
  // a GPR sequence for the operation.
 auto getLogicOperand = [&] (SDValue Operand) -> SDValue {
    unsigned OperandOpcode = Operand.getOpcode();
    if (OperandOpcode == ISD::SETCC)
      return getSETCCInGPR(Operand, SetccInGPROpts::ZExtOrig);
    else if (OperandOpcode == ISD::TRUNCATE) {
      SDValue InputOp = Operand.getOperand(0);
     EVT InVT = InputOp.getValueType();
      return SDValue(CurDAG->getMachineNode(InVT == MVT::i32 ? PPC::RLDICL_32 :
                                            PPC::RLDICL, dl, InVT, InputOp,
                                            S->getI64Imm(0, dl),
                                            S->getI64Imm(63, dl)), 0);
    } else if (isLogicOp(OperandOpcode))
      return computeLogicOpInGPR(Operand);
    return SDValue();
  };
  LHS = getLogicOperand(LogicOp.getOperand(0));
  RHS = getLogicOperand(LogicOp.getOperand(1));

  // If a GPR sequence can't be produced for the LHS we can't proceed.
  // Not producing a GPR sequence for the RHS is only a problem if this isn't
  // a bitwise negation operation.
  if (!LHS || (!RHS && !IsBitwiseNegation))
    return SDValue();

  NumLogicOpsOnComparison++;

  // We will use the inputs as 64-bit values.
  if (LHS.getValueType() == MVT::i32)
    LHS = addExtOrTrunc(LHS, ExtOrTruncConversion::Ext);
  if (!IsBitwiseNegation && RHS.getValueType() == MVT::i32)
    RHS = addExtOrTrunc(RHS, ExtOrTruncConversion::Ext);

  unsigned NewOpc;
  switch (LogicOp.getOpcode()) {
  default: llvm_unreachable("Unknown logic operation.");
  case ISD::AND: NewOpc = PPC::AND8; break;
  case ISD::OR:  NewOpc = PPC::OR8;  break;
  case ISD::XOR: NewOpc = PPC::XOR8; break;
  }

  if (IsBitwiseNegation) {
    RHS = S->getI64Imm(1, dl);
    NewOpc = PPC::XORI8;
  }

  return SDValue(CurDAG->getMachineNode(NewOpc, dl, MVT::i64, LHS, RHS), 0);

}

/// If the value isn't guaranteed to be sign-extended to 64-bits, extend it.
/// Otherwise just reinterpret it as a 64-bit value.
/// Useful when emitting comparison code for 32-bit values without using
/// the compare instruction (which only considers the lower 32-bits).
SDValue IntegerCompareEliminator::signExtendInputIfNeeded(SDValue Input) {
  assert(Input.getValueType() == MVT::i32 &&
         "Can only sign-extend 32-bit values here.");
  unsigned Opc = Input.getOpcode();

  // The value was sign extended and then truncated to 32-bits. No need to
  // sign extend it again.
  if (Opc == ISD::TRUNCATE &&
      (Input.getOperand(0).getOpcode() == ISD::AssertSext ||
       Input.getOperand(0).getOpcode() == ISD::SIGN_EXTEND))
    return addExtOrTrunc(Input, ExtOrTruncConversion::Ext);

  LoadSDNode *InputLoad = dyn_cast<LoadSDNode>(Input);
  // The input is a sign-extending load. All ppc sign-extending loads
  // sign-extend to the full 64-bits.
  if (InputLoad && InputLoad->getExtensionType() == ISD::SEXTLOAD)
    return addExtOrTrunc(Input, ExtOrTruncConversion::Ext);

  ConstantSDNode *InputConst = dyn_cast<ConstantSDNode>(Input);
  // We don't sign-extend constants.
  if (InputConst)
    return addExtOrTrunc(Input, ExtOrTruncConversion::Ext);

  SDLoc dl(Input);
  SignExtensionsAdded++;
  return SDValue(CurDAG->getMachineNode(PPC::EXTSW_32_64, dl,
                                        MVT::i64, Input), 0);
}

/// If the value isn't guaranteed to be zero-extended to 64-bits, extend it.
/// Otherwise just reinterpret it as a 64-bit value.
/// Useful when emitting comparison code for 32-bit values without using
/// the compare instruction (which only considers the lower 32-bits).
SDValue IntegerCompareEliminator::zeroExtendInputIfNeeded(SDValue Input) {
  assert(Input.getValueType() == MVT::i32 &&
         "Can only zero-extend 32-bit values here.");
  unsigned Opc = Input.getOpcode();

  // The only condition under which we can omit the actual extend instruction:
  // - The value is a positive constant
  // - The value comes from a load that isn't a sign-extending load
  // An ISD::TRUNCATE needs to be zero-extended unless it is fed by a zext.
  bool IsTruncateOfZExt = Opc == ISD::TRUNCATE &&
    (Input.getOperand(0).getOpcode() == ISD::AssertZext ||
     Input.getOperand(0).getOpcode() == ISD::ZERO_EXTEND);
  if (IsTruncateOfZExt)
    return addExtOrTrunc(Input, ExtOrTruncConversion::Ext);

  ConstantSDNode *InputConst = dyn_cast<ConstantSDNode>(Input);
  if (InputConst && InputConst->getSExtValue() >= 0)
    return addExtOrTrunc(Input, ExtOrTruncConversion::Ext);

  LoadSDNode *InputLoad = dyn_cast<LoadSDNode>(Input);
  // The input is a load that doesn't sign-extend (it will be zero-extended).
  if (InputLoad && InputLoad->getExtensionType() != ISD::SEXTLOAD)
    return addExtOrTrunc(Input, ExtOrTruncConversion::Ext);

  // None of the above, need to zero-extend.
  SDLoc dl(Input);
  ZeroExtensionsAdded++;
  return SDValue(CurDAG->getMachineNode(PPC::RLDICL_32_64, dl, MVT::i64, Input,
                                        S->getI64Imm(0, dl),
                                        S->getI64Imm(32, dl)), 0);
}

// Handle a 32-bit value in a 64-bit register and vice-versa. These are of
// course not actual zero/sign extensions that will generate machine code,
// they're just a way to reinterpret a 32 bit value in a register as a
// 64 bit value and vice-versa.
SDValue IntegerCompareEliminator::addExtOrTrunc(SDValue NatWidthRes,
                                                ExtOrTruncConversion Conv) {
  SDLoc dl(NatWidthRes);

  // For reinterpreting 32-bit values as 64 bit values, we generate
  // INSERT_SUBREG IMPLICIT_DEF:i64, <input>, TargetConstant:i32<1>
  if (Conv == ExtOrTruncConversion::Ext) {
    SDValue ImDef(CurDAG->getMachineNode(PPC::IMPLICIT_DEF, dl, MVT::i64), 0);
    SDValue SubRegIdx =
      CurDAG->getTargetConstant(PPC::sub_32, dl, MVT::i32);
    return SDValue(CurDAG->getMachineNode(PPC::INSERT_SUBREG, dl, MVT::i64,
                                          ImDef, NatWidthRes, SubRegIdx), 0);
  }

  assert(Conv == ExtOrTruncConversion::Trunc &&
         "Unknown convertion between 32 and 64 bit values.");
  // For reinterpreting 64-bit values as 32-bit values, we just need to
  // EXTRACT_SUBREG (i.e. extract the low word).
  SDValue SubRegIdx =
    CurDAG->getTargetConstant(PPC::sub_32, dl, MVT::i32);
  return SDValue(CurDAG->getMachineNode(PPC::EXTRACT_SUBREG, dl, MVT::i32,
                                        NatWidthRes, SubRegIdx), 0);
}

// Produce a GPR sequence for compound comparisons (<=, >=) against zero.
// Handle both zero-extensions and sign-extensions.
SDValue
IntegerCompareEliminator::getCompoundZeroComparisonInGPR(SDValue LHS, SDLoc dl,
                                                         ZeroCompare CmpTy) {
  EVT InVT = LHS.getValueType();
  bool Is32Bit = InVT == MVT::i32;
  SDValue ToExtend;

  // Produce the value that needs to be either zero or sign extended.
  switch (CmpTy) {
  case ZeroCompare::GEZExt:
  case ZeroCompare::GESExt:
    ToExtend = SDValue(CurDAG->getMachineNode(Is32Bit ? PPC::NOR : PPC::NOR8,
                                              dl, InVT, LHS, LHS), 0);
    break;
  case ZeroCompare::LEZExt:
  case ZeroCompare::LESExt: {
    if (Is32Bit) {
      // Upper 32 bits cannot be undefined for this sequence.
      LHS = signExtendInputIfNeeded(LHS);
      SDValue Neg =
        SDValue(CurDAG->getMachineNode(PPC::NEG8, dl, MVT::i64, LHS), 0);
      ToExtend =
        SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                       Neg, S->getI64Imm(1, dl),
                                       S->getI64Imm(63, dl)), 0);
    } else {
      SDValue Addi =
        SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64, LHS,
                                       S->getI64Imm(~0ULL, dl)), 0);
      ToExtend = SDValue(CurDAG->getMachineNode(PPC::OR8, dl, MVT::i64,
                                                Addi, LHS), 0);
    }
    break;
  }
  }

  // For 64-bit sequences, the extensions are the same for the GE/LE cases.
  if (!Is32Bit &&
      (CmpTy == ZeroCompare::GEZExt || CmpTy == ZeroCompare::LEZExt))
    return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                          ToExtend, S->getI64Imm(1, dl),
                                          S->getI64Imm(63, dl)), 0);
  if (!Is32Bit &&
      (CmpTy == ZeroCompare::GESExt || CmpTy == ZeroCompare::LESExt))
    return SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, ToExtend,
                                          S->getI64Imm(63, dl)), 0);

  assert(Is32Bit && "Should have handled the 32-bit sequences above.");
  // For 32-bit sequences, the extensions differ between GE/LE cases.
  switch (CmpTy) {
  case ZeroCompare::GEZExt: {
    SDValue ShiftOps[] = { ToExtend, S->getI32Imm(1, dl), S->getI32Imm(31, dl),
                           S->getI32Imm(31, dl) };
    return SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32,
                                          ShiftOps), 0);
  }
  case ZeroCompare::GESExt:
    return SDValue(CurDAG->getMachineNode(PPC::SRAWI, dl, MVT::i32, ToExtend,
                                          S->getI32Imm(31, dl)), 0);
  case ZeroCompare::LEZExt:
    return SDValue(CurDAG->getMachineNode(PPC::XORI8, dl, MVT::i64, ToExtend,
                                          S->getI32Imm(1, dl)), 0);
  case ZeroCompare::LESExt:
    return SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64, ToExtend,
                                          S->getI32Imm(-1, dl)), 0);
  }

  // The above case covers all the enumerators so it can't have a default clause
  // to avoid compiler warnings.
  llvm_unreachable("Unknown zero-comparison type.");
}

/// Produces a zero-extended result of comparing two 32-bit values according to
/// the passed condition code.
SDValue
IntegerCompareEliminator::get32BitZExtCompare(SDValue LHS, SDValue RHS,
                                              ISD::CondCode CC,
                                              int64_t RHSValue, SDLoc dl) {
  if (CmpInGPR == ICGPR_I64 || CmpInGPR == ICGPR_SextI64 ||
      CmpInGPR == ICGPR_ZextI64 || CmpInGPR == ICGPR_Sext)
    return SDValue();
  bool IsRHSZero = RHSValue == 0;
  bool IsRHSOne = RHSValue == 1;
  bool IsRHSNegOne = RHSValue == -1LL;
  switch (CC) {
  default: return SDValue();
  case ISD::SETEQ: {
    // (zext (setcc %a, %b, seteq)) -> (lshr (cntlzw (xor %a, %b)), 5)
    // (zext (setcc %a, 0, seteq))  -> (lshr (cntlzw %a), 5)
    SDValue Xor = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR, dl, MVT::i32, LHS, RHS), 0);
    SDValue Clz =
      SDValue(CurDAG->getMachineNode(PPC::CNTLZW, dl, MVT::i32, Xor), 0);
    SDValue ShiftOps[] = { Clz, S->getI32Imm(27, dl), S->getI32Imm(5, dl),
      S->getI32Imm(31, dl) };
    return SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32,
                                          ShiftOps), 0);
  }
  case ISD::SETNE: {
    // (zext (setcc %a, %b, setne)) -> (xor (lshr (cntlzw (xor %a, %b)), 5), 1)
    // (zext (setcc %a, 0, setne))  -> (xor (lshr (cntlzw %a), 5), 1)
    SDValue Xor = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR, dl, MVT::i32, LHS, RHS), 0);
    SDValue Clz =
      SDValue(CurDAG->getMachineNode(PPC::CNTLZW, dl, MVT::i32, Xor), 0);
    SDValue ShiftOps[] = { Clz, S->getI32Imm(27, dl), S->getI32Imm(5, dl),
      S->getI32Imm(31, dl) };
    SDValue Shift =
      SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, ShiftOps), 0);
    return SDValue(CurDAG->getMachineNode(PPC::XORI, dl, MVT::i32, Shift,
                                          S->getI32Imm(1, dl)), 0);
  }
  case ISD::SETGE: {
    // (zext (setcc %a, %b, setge)) -> (xor (lshr (sub %a, %b), 63), 1)
    // (zext (setcc %a, 0, setge))  -> (lshr (~ %a), 31)
    if(IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GEZExt);

    // Not a special case (i.e. RHS == 0). Handle (%a >= %b) as (%b <= %a)
    // by swapping inputs and falling through.
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLE: {
    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // (zext (setcc %a, %b, setle)) -> (xor (lshr (sub %b, %a), 63), 1)
    // (zext (setcc %a, 0, setle))  -> (xor (lshr (- %a), 63), 1)
    if(IsRHSZero) {
      if (CmpInGPR == ICGPR_NonExtIn)
        return SDValue();
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LEZExt);
    }

    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = signExtendInputIfNeeded(LHS);
    RHS = signExtendInputIfNeeded(RHS);
    SDValue Sub =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, LHS, RHS), 0);
    SDValue Shift =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, Sub,
                                     S->getI64Imm(1, dl), S->getI64Imm(63, dl)),
              0);
    return
      SDValue(CurDAG->getMachineNode(PPC::XORI8, dl,
                                     MVT::i64, Shift, S->getI32Imm(1, dl)), 0);
  }
  case ISD::SETGT: {
    // (zext (setcc %a, %b, setgt)) -> (lshr (sub %b, %a), 63)
    // (zext (setcc %a, -1, setgt)) -> (lshr (~ %a), 31)
    // (zext (setcc %a, 0, setgt))  -> (lshr (- %a), 63)
    // Handle SETLT -1 (which is equivalent to SETGE 0).
    if (IsRHSNegOne)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GEZExt);

    if (IsRHSZero) {
      if (CmpInGPR == ICGPR_NonExtIn)
        return SDValue();
      // The upper 32-bits of the register can't be undefined for this sequence.
      LHS = signExtendInputIfNeeded(LHS);
      RHS = signExtendInputIfNeeded(RHS);
      SDValue Neg =
        SDValue(CurDAG->getMachineNode(PPC::NEG8, dl, MVT::i64, LHS), 0);
      return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                     Neg, S->getI32Imm(1, dl), S->getI32Imm(63, dl)), 0);
    }
    // Not a special case (i.e. RHS == 0 or RHS == -1). Handle (%a > %b) as
    // (%b < %a) by swapping inputs and falling through.
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    IsRHSOne = RHSConst && RHSConst->getSExtValue() == 1;
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLT: {
    // (zext (setcc %a, %b, setlt)) -> (lshr (sub %a, %b), 63)
    // (zext (setcc %a, 1, setlt))  -> (xor (lshr (- %a), 63), 1)
    // (zext (setcc %a, 0, setlt))  -> (lshr %a, 31)
    // Handle SETLT 1 (which is equivalent to SETLE 0).
    if (IsRHSOne) {
      if (CmpInGPR == ICGPR_NonExtIn)
        return SDValue();
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LEZExt);
    }

    if (IsRHSZero) {
      SDValue ShiftOps[] = { LHS, S->getI32Imm(1, dl), S->getI32Imm(31, dl),
                             S->getI32Imm(31, dl) };
      return SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32,
                                            ShiftOps), 0);
    }

    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = signExtendInputIfNeeded(LHS);
    RHS = signExtendInputIfNeeded(RHS);
    SDValue SUBFNode =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, RHS, LHS), 0);
    return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                    SUBFNode, S->getI64Imm(1, dl),
                                    S->getI64Imm(63, dl)), 0);
  }
  case ISD::SETUGE:
    // (zext (setcc %a, %b, setuge)) -> (xor (lshr (sub %b, %a), 63), 1)
    // (zext (setcc %a, %b, setule)) -> (xor (lshr (sub %a, %b), 63), 1)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULE: {
    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = zeroExtendInputIfNeeded(LHS);
    RHS = zeroExtendInputIfNeeded(RHS);
    SDValue Subtract =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, LHS, RHS), 0);
    SDValue SrdiNode =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                          Subtract, S->getI64Imm(1, dl),
                                          S->getI64Imm(63, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::XORI8, dl, MVT::i64, SrdiNode,
                                            S->getI32Imm(1, dl)), 0);
  }
  case ISD::SETUGT:
    // (zext (setcc %a, %b, setugt)) -> (lshr (sub %b, %a), 63)
    // (zext (setcc %a, %b, setult)) -> (lshr (sub %a, %b), 63)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULT: {
    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = zeroExtendInputIfNeeded(LHS);
    RHS = zeroExtendInputIfNeeded(RHS);
    SDValue Subtract =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, RHS, LHS), 0);
    return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                          Subtract, S->getI64Imm(1, dl),
                                          S->getI64Imm(63, dl)), 0);
  }
  }
}

/// Produces a sign-extended result of comparing two 32-bit values according to
/// the passed condition code.
SDValue
IntegerCompareEliminator::get32BitSExtCompare(SDValue LHS, SDValue RHS,
                                              ISD::CondCode CC,
                                              int64_t RHSValue, SDLoc dl) {
  if (CmpInGPR == ICGPR_I64 || CmpInGPR == ICGPR_SextI64 ||
      CmpInGPR == ICGPR_ZextI64 || CmpInGPR == ICGPR_Zext)
    return SDValue();
  bool IsRHSZero = RHSValue == 0;
  bool IsRHSOne = RHSValue == 1;
  bool IsRHSNegOne = RHSValue == -1LL;

  switch (CC) {
  default: return SDValue();
  case ISD::SETEQ: {
    // (sext (setcc %a, %b, seteq)) ->
    //   (ashr (shl (ctlz (xor %a, %b)), 58), 63)
    // (sext (setcc %a, 0, seteq)) ->
    //   (ashr (shl (ctlz %a), 58), 63)
    SDValue CountInput = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR, dl, MVT::i32, LHS, RHS), 0);
    SDValue Cntlzw =
      SDValue(CurDAG->getMachineNode(PPC::CNTLZW, dl, MVT::i32, CountInput), 0);
    SDValue SHLOps[] = { Cntlzw, S->getI32Imm(27, dl),
                         S->getI32Imm(5, dl), S->getI32Imm(31, dl) };
    SDValue Slwi =
      SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, SHLOps), 0);
    return SDValue(CurDAG->getMachineNode(PPC::NEG, dl, MVT::i32, Slwi), 0);
  }
  case ISD::SETNE: {
    // Bitwise xor the operands, count leading zeros, shift right by 5 bits and
    // flip the bit, finally take 2's complement.
    // (sext (setcc %a, %b, setne)) ->
    //   (neg (xor (lshr (ctlz (xor %a, %b)), 5), 1))
    // Same as above, but the first xor is not needed.
    // (sext (setcc %a, 0, setne)) ->
    //   (neg (xor (lshr (ctlz %a), 5), 1))
    SDValue Xor = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR, dl, MVT::i32, LHS, RHS), 0);
    SDValue Clz =
      SDValue(CurDAG->getMachineNode(PPC::CNTLZW, dl, MVT::i32, Xor), 0);
    SDValue ShiftOps[] =
      { Clz, S->getI32Imm(27, dl), S->getI32Imm(5, dl), S->getI32Imm(31, dl) };
    SDValue Shift =
      SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, ShiftOps), 0);
    SDValue Xori =
      SDValue(CurDAG->getMachineNode(PPC::XORI, dl, MVT::i32, Shift,
                                     S->getI32Imm(1, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::NEG, dl, MVT::i32, Xori), 0);
  }
  case ISD::SETGE: {
    // (sext (setcc %a, %b, setge)) -> (add (lshr (sub %a, %b), 63), -1)
    // (sext (setcc %a, 0, setge))  -> (ashr (~ %a), 31)
    if (IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GESExt);

    // Not a special case (i.e. RHS == 0). Handle (%a >= %b) as (%b <= %a)
    // by swapping inputs and falling through.
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLE: {
    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // (sext (setcc %a, %b, setge)) -> (add (lshr (sub %b, %a), 63), -1)
    // (sext (setcc %a, 0, setle))  -> (add (lshr (- %a), 63), -1)
    if (IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LESExt);

    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = signExtendInputIfNeeded(LHS);
    RHS = signExtendInputIfNeeded(RHS);
    SDValue SUBFNode =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, MVT::Glue,
                                     LHS, RHS), 0);
    SDValue Srdi =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                     SUBFNode, S->getI64Imm(1, dl),
                                     S->getI64Imm(63, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64, Srdi,
                                          S->getI32Imm(-1, dl)), 0);
  }
  case ISD::SETGT: {
    // (sext (setcc %a, %b, setgt)) -> (ashr (sub %b, %a), 63)
    // (sext (setcc %a, -1, setgt)) -> (ashr (~ %a), 31)
    // (sext (setcc %a, 0, setgt))  -> (ashr (- %a), 63)
    if (IsRHSNegOne)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GESExt);
    if (IsRHSZero) {
      if (CmpInGPR == ICGPR_NonExtIn)
        return SDValue();
      // The upper 32-bits of the register can't be undefined for this sequence.
      LHS = signExtendInputIfNeeded(LHS);
      RHS = signExtendInputIfNeeded(RHS);
      SDValue Neg =
        SDValue(CurDAG->getMachineNode(PPC::NEG8, dl, MVT::i64, LHS), 0);
        return SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, Neg,
                                              S->getI64Imm(63, dl)), 0);
    }
    // Not a special case (i.e. RHS == 0 or RHS == -1). Handle (%a > %b) as
    // (%b < %a) by swapping inputs and falling through.
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    IsRHSOne = RHSConst && RHSConst->getSExtValue() == 1;
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLT: {
    // (sext (setcc %a, %b, setgt)) -> (ashr (sub %a, %b), 63)
    // (sext (setcc %a, 1, setgt))  -> (add (lshr (- %a), 63), -1)
    // (sext (setcc %a, 0, setgt))  -> (ashr %a, 31)
    if (IsRHSOne) {
      if (CmpInGPR == ICGPR_NonExtIn)
        return SDValue();
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LESExt);
    }
    if (IsRHSZero)
      return SDValue(CurDAG->getMachineNode(PPC::SRAWI, dl, MVT::i32, LHS,
                                            S->getI32Imm(31, dl)), 0);

    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = signExtendInputIfNeeded(LHS);
    RHS = signExtendInputIfNeeded(RHS);
    SDValue SUBFNode =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, RHS, LHS), 0);
    return SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64,
                                          SUBFNode, S->getI64Imm(63, dl)), 0);
  }
  case ISD::SETUGE:
    // (sext (setcc %a, %b, setuge)) -> (add (lshr (sub %a, %b), 63), -1)
    // (sext (setcc %a, %b, setule)) -> (add (lshr (sub %b, %a), 63), -1)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULE: {
    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = zeroExtendInputIfNeeded(LHS);
    RHS = zeroExtendInputIfNeeded(RHS);
    SDValue Subtract =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, LHS, RHS), 0);
    SDValue Shift =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, Subtract,
                                     S->getI32Imm(1, dl), S->getI32Imm(63,dl)),
              0);
    return SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64, Shift,
                                          S->getI32Imm(-1, dl)), 0);
  }
  case ISD::SETUGT:
    // (sext (setcc %a, %b, setugt)) -> (ashr (sub %b, %a), 63)
    // (sext (setcc %a, %b, setugt)) -> (ashr (sub %a, %b), 63)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULT: {
    if (CmpInGPR == ICGPR_NonExtIn)
      return SDValue();
    // The upper 32-bits of the register can't be undefined for this sequence.
    LHS = zeroExtendInputIfNeeded(LHS);
    RHS = zeroExtendInputIfNeeded(RHS);
    SDValue Subtract =
      SDValue(CurDAG->getMachineNode(PPC::SUBF8, dl, MVT::i64, RHS, LHS), 0);
    return SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64,
                                          Subtract, S->getI64Imm(63, dl)), 0);
  }
  }
}

/// Produces a zero-extended result of comparing two 64-bit values according to
/// the passed condition code.
SDValue
IntegerCompareEliminator::get64BitZExtCompare(SDValue LHS, SDValue RHS,
                                              ISD::CondCode CC,
                                              int64_t RHSValue, SDLoc dl) {
  if (CmpInGPR == ICGPR_I32 || CmpInGPR == ICGPR_SextI32 ||
      CmpInGPR == ICGPR_ZextI32 || CmpInGPR == ICGPR_Sext)
    return SDValue();
  bool IsRHSZero = RHSValue == 0;
  bool IsRHSOne = RHSValue == 1;
  bool IsRHSNegOne = RHSValue == -1LL;
  switch (CC) {
  default: return SDValue();
  case ISD::SETEQ: {
    // (zext (setcc %a, %b, seteq)) -> (lshr (ctlz (xor %a, %b)), 6)
    // (zext (setcc %a, 0, seteq)) ->  (lshr (ctlz %a), 6)
    SDValue Xor = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR8, dl, MVT::i64, LHS, RHS), 0);
    SDValue Clz =
      SDValue(CurDAG->getMachineNode(PPC::CNTLZD, dl, MVT::i64, Xor), 0);
    return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, Clz,
                                          S->getI64Imm(58, dl),
                                          S->getI64Imm(63, dl)), 0);
  }
  case ISD::SETNE: {
    // {addc.reg, addc.CA} = (addcarry (xor %a, %b), -1)
    // (zext (setcc %a, %b, setne)) -> (sube addc.reg, addc.reg, addc.CA)
    // {addcz.reg, addcz.CA} = (addcarry %a, -1)
    // (zext (setcc %a, 0, setne)) -> (sube addcz.reg, addcz.reg, addcz.CA)
    SDValue Xor = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR8, dl, MVT::i64, LHS, RHS), 0);
    SDValue AC =
      SDValue(CurDAG->getMachineNode(PPC::ADDIC8, dl, MVT::i64, MVT::Glue,
                                     Xor, S->getI32Imm(~0U, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64, AC,
                                          Xor, AC.getValue(1)), 0);
  }
  case ISD::SETGE: {
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (zext (setcc %a, %b, setge)) ->
    //   (adde (lshr %b, 63), (ashr %a, 63), subc.CA)
    // (zext (setcc %a, 0, setge)) -> (lshr (~ %a), 63)
    if (IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GEZExt);
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLE: {
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (zext (setcc %a, %b, setge)) ->
    //   (adde (lshr %a, 63), (ashr %b, 63), subc.CA)
    // (zext (setcc %a, 0, setge)) -> (lshr (or %a, (add %a, -1)), 63)
    if (IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LEZExt);
    SDValue ShiftL =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, LHS,
                                     S->getI64Imm(1, dl),
                                     S->getI64Imm(63, dl)), 0);
    SDValue ShiftR =
      SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, RHS,
                                     S->getI64Imm(63, dl)), 0);
    SDValue SubtractCarry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     LHS, RHS), 1);
    return SDValue(CurDAG->getMachineNode(PPC::ADDE8, dl, MVT::i64, MVT::Glue,
                                          ShiftR, ShiftL, SubtractCarry), 0);
  }
  case ISD::SETGT: {
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (zext (setcc %a, %b, setgt)) ->
    //   (xor (adde (lshr %a, 63), (ashr %b, 63), subc.CA), 1)
    // (zext (setcc %a, 0, setgt)) -> (lshr (nor (add %a, -1), %a), 63)
    if (IsRHSNegOne)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GEZExt);
    if (IsRHSZero) {
      SDValue Addi =
        SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64, LHS,
                                       S->getI64Imm(~0ULL, dl)), 0);
      SDValue Nor =
        SDValue(CurDAG->getMachineNode(PPC::NOR8, dl, MVT::i64, Addi, LHS), 0);
      return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, Nor,
                                            S->getI64Imm(1, dl),
                                            S->getI64Imm(63, dl)), 0);
    }
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    IsRHSOne = RHSConst && RHSConst->getSExtValue() == 1;
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLT: {
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (zext (setcc %a, %b, setlt)) ->
    //   (xor (adde (lshr %b, 63), (ashr %a, 63), subc.CA), 1)
    // (zext (setcc %a, 0, setlt)) -> (lshr %a, 63)
    if (IsRHSOne)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LEZExt);
    if (IsRHSZero)
      return SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, LHS,
                                            S->getI64Imm(1, dl),
                                            S->getI64Imm(63, dl)), 0);
    SDValue SRADINode =
      SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64,
                                     LHS, S->getI64Imm(63, dl)), 0);
    SDValue SRDINode =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                     RHS, S->getI64Imm(1, dl),
                                     S->getI64Imm(63, dl)), 0);
    SDValue SUBFC8Carry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     RHS, LHS), 1);
    SDValue ADDE8Node =
      SDValue(CurDAG->getMachineNode(PPC::ADDE8, dl, MVT::i64, MVT::Glue,
                                     SRDINode, SRADINode, SUBFC8Carry), 0);
    return SDValue(CurDAG->getMachineNode(PPC::XORI8, dl, MVT::i64,
                                          ADDE8Node, S->getI64Imm(1, dl)), 0);
  }
  case ISD::SETUGE:
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (zext (setcc %a, %b, setuge)) -> (add (sube %b, %b, subc.CA), 1)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULE: {
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (zext (setcc %a, %b, setule)) -> (add (sube %a, %a, subc.CA), 1)
    SDValue SUBFC8Carry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     LHS, RHS), 1);
    SDValue SUBFE8Node =
      SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64, MVT::Glue,
                                     LHS, LHS, SUBFC8Carry), 0);
    return SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64,
                                          SUBFE8Node, S->getI64Imm(1, dl)), 0);
  }
  case ISD::SETUGT:
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (zext (setcc %a, %b, setugt)) -> -(sube %b, %b, subc.CA)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULT: {
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (zext (setcc %a, %b, setult)) -> -(sube %a, %a, subc.CA)
    SDValue SubtractCarry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     RHS, LHS), 1);
    SDValue ExtSub =
      SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64,
                                     LHS, LHS, SubtractCarry), 0);
    return SDValue(CurDAG->getMachineNode(PPC::NEG8, dl, MVT::i64,
                                          ExtSub), 0);
  }
  }
}

/// Produces a sign-extended result of comparing two 64-bit values according to
/// the passed condition code.
SDValue
IntegerCompareEliminator::get64BitSExtCompare(SDValue LHS, SDValue RHS,
                                              ISD::CondCode CC,
                                              int64_t RHSValue, SDLoc dl) {
  if (CmpInGPR == ICGPR_I32 || CmpInGPR == ICGPR_SextI32 ||
      CmpInGPR == ICGPR_ZextI32 || CmpInGPR == ICGPR_Zext)
    return SDValue();
  bool IsRHSZero = RHSValue == 0;
  bool IsRHSOne = RHSValue == 1;
  bool IsRHSNegOne = RHSValue == -1LL;
  switch (CC) {
  default: return SDValue();
  case ISD::SETEQ: {
    // {addc.reg, addc.CA} = (addcarry (xor %a, %b), -1)
    // (sext (setcc %a, %b, seteq)) -> (sube addc.reg, addc.reg, addc.CA)
    // {addcz.reg, addcz.CA} = (addcarry %a, -1)
    // (sext (setcc %a, 0, seteq)) -> (sube addcz.reg, addcz.reg, addcz.CA)
    SDValue AddInput = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR8, dl, MVT::i64, LHS, RHS), 0);
    SDValue Addic =
      SDValue(CurDAG->getMachineNode(PPC::ADDIC8, dl, MVT::i64, MVT::Glue,
                                     AddInput, S->getI32Imm(~0U, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64, Addic,
                                          Addic, Addic.getValue(1)), 0);
  }
  case ISD::SETNE: {
    // {subfc.reg, subfc.CA} = (subcarry 0, (xor %a, %b))
    // (sext (setcc %a, %b, setne)) -> (sube subfc.reg, subfc.reg, subfc.CA)
    // {subfcz.reg, subfcz.CA} = (subcarry 0, %a)
    // (sext (setcc %a, 0, setne)) -> (sube subfcz.reg, subfcz.reg, subfcz.CA)
    SDValue Xor = IsRHSZero ? LHS :
      SDValue(CurDAG->getMachineNode(PPC::XOR8, dl, MVT::i64, LHS, RHS), 0);
    SDValue SC =
      SDValue(CurDAG->getMachineNode(PPC::SUBFIC8, dl, MVT::i64, MVT::Glue,
                                     Xor, S->getI32Imm(0, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64, SC,
                                          SC, SC.getValue(1)), 0);
  }
  case ISD::SETGE: {
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (zext (setcc %a, %b, setge)) ->
    //   (- (adde (lshr %b, 63), (ashr %a, 63), subc.CA))
    // (zext (setcc %a, 0, setge)) -> (~ (ashr %a, 63))
    if (IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GESExt);
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLE: {
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (zext (setcc %a, %b, setge)) ->
    //   (- (adde (lshr %a, 63), (ashr %b, 63), subc.CA))
    // (zext (setcc %a, 0, setge)) -> (ashr (or %a, (add %a, -1)), 63)
    if (IsRHSZero)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LESExt);
    SDValue ShiftR =
      SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, RHS,
                                     S->getI64Imm(63, dl)), 0);
    SDValue ShiftL =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64, LHS,
                                     S->getI64Imm(1, dl),
                                     S->getI64Imm(63, dl)), 0);
    SDValue SubtractCarry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     LHS, RHS), 1);
    SDValue Adde =
      SDValue(CurDAG->getMachineNode(PPC::ADDE8, dl, MVT::i64, MVT::Glue,
                                     ShiftR, ShiftL, SubtractCarry), 0);
    return SDValue(CurDAG->getMachineNode(PPC::NEG8, dl, MVT::i64, Adde), 0);
  }
  case ISD::SETGT: {
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (zext (setcc %a, %b, setgt)) ->
    //   -(xor (adde (lshr %a, 63), (ashr %b, 63), subc.CA), 1)
    // (zext (setcc %a, 0, setgt)) -> (ashr (nor (add %a, -1), %a), 63)
    if (IsRHSNegOne)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::GESExt);
    if (IsRHSZero) {
      SDValue Add =
        SDValue(CurDAG->getMachineNode(PPC::ADDI8, dl, MVT::i64, LHS,
                                       S->getI64Imm(-1, dl)), 0);
      SDValue Nor =
        SDValue(CurDAG->getMachineNode(PPC::NOR8, dl, MVT::i64, Add, LHS), 0);
      return SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, Nor,
                                            S->getI64Imm(63, dl)), 0);
    }
    std::swap(LHS, RHS);
    ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
    IsRHSZero = RHSConst && RHSConst->isNullValue();
    IsRHSOne = RHSConst && RHSConst->getSExtValue() == 1;
    LLVM_FALLTHROUGH;
  }
  case ISD::SETLT: {
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (zext (setcc %a, %b, setlt)) ->
    //   -(xor (adde (lshr %b, 63), (ashr %a, 63), subc.CA), 1)
    // (zext (setcc %a, 0, setlt)) -> (ashr %a, 63)
    if (IsRHSOne)
      return getCompoundZeroComparisonInGPR(LHS, dl, ZeroCompare::LESExt);
    if (IsRHSZero) {
      return SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, LHS,
                                            S->getI64Imm(63, dl)), 0);
    }
    SDValue SRADINode =
      SDValue(CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64,
                                     LHS, S->getI64Imm(63, dl)), 0);
    SDValue SRDINode =
      SDValue(CurDAG->getMachineNode(PPC::RLDICL, dl, MVT::i64,
                                     RHS, S->getI64Imm(1, dl),
                                     S->getI64Imm(63, dl)), 0);
    SDValue SUBFC8Carry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     RHS, LHS), 1);
    SDValue ADDE8Node =
      SDValue(CurDAG->getMachineNode(PPC::ADDE8, dl, MVT::i64,
                                     SRDINode, SRADINode, SUBFC8Carry), 0);
    SDValue XORI8Node =
      SDValue(CurDAG->getMachineNode(PPC::XORI8, dl, MVT::i64,
                                     ADDE8Node, S->getI64Imm(1, dl)), 0);
    return SDValue(CurDAG->getMachineNode(PPC::NEG8, dl, MVT::i64,
                                          XORI8Node), 0);
  }
  case ISD::SETUGE:
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (sext (setcc %a, %b, setuge)) -> ~(sube %b, %b, subc.CA)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULE: {
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (sext (setcc %a, %b, setule)) -> ~(sube %a, %a, subc.CA)
    SDValue SubtractCarry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     LHS, RHS), 1);
    SDValue ExtSub =
      SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64, MVT::Glue, LHS,
                                     LHS, SubtractCarry), 0);
    return SDValue(CurDAG->getMachineNode(PPC::NOR8, dl, MVT::i64,
                                          ExtSub, ExtSub), 0);
  }
  case ISD::SETUGT:
    // {subc.reg, subc.CA} = (subcarry %b, %a)
    // (sext (setcc %a, %b, setugt)) -> (sube %b, %b, subc.CA)
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ISD::SETULT: {
    // {subc.reg, subc.CA} = (subcarry %a, %b)
    // (sext (setcc %a, %b, setult)) -> (sube %a, %a, subc.CA)
    SDValue SubCarry =
      SDValue(CurDAG->getMachineNode(PPC::SUBFC8, dl, MVT::i64, MVT::Glue,
                                     RHS, LHS), 1);
    return SDValue(CurDAG->getMachineNode(PPC::SUBFE8, dl, MVT::i64,
                                     LHS, LHS, SubCarry), 0);
  }
  }
}

/// Do all uses of this SDValue need the result in a GPR?
/// This is meant to be used on values that have type i1 since
/// it is somewhat meaningless to ask if values of other types
/// should be kept in GPR's.
static bool allUsesExtend(SDValue Compare, SelectionDAG *CurDAG) {
  assert(Compare.getOpcode() == ISD::SETCC &&
         "An ISD::SETCC node required here.");

  // For values that have a single use, the caller should obviously already have
  // checked if that use is an extending use. We check the other uses here.
  if (Compare.hasOneUse())
    return true;
  // We want the value in a GPR if it is being extended, used for a select, or
  // used in logical operations.
  for (auto CompareUse : Compare.getNode()->uses())
    if (CompareUse->getOpcode() != ISD::SIGN_EXTEND &&
        CompareUse->getOpcode() != ISD::ZERO_EXTEND &&
        CompareUse->getOpcode() != ISD::SELECT &&
        !isLogicOp(CompareUse->getOpcode())) {
      OmittedForNonExtendUses++;
      return false;
    }
  return true;
}

/// Returns an equivalent of a SETCC node but with the result the same width as
/// the inputs. This can also be used for SELECT_CC if either the true or false
/// values is a power of two while the other is zero.
SDValue IntegerCompareEliminator::getSETCCInGPR(SDValue Compare,
                                                SetccInGPROpts ConvOpts) {
  assert((Compare.getOpcode() == ISD::SETCC ||
          Compare.getOpcode() == ISD::SELECT_CC) &&
         "An ISD::SETCC node required here.");

  // Don't convert this comparison to a GPR sequence because there are uses
  // of the i1 result (i.e. uses that require the result in the CR).
  if ((Compare.getOpcode() == ISD::SETCC) && !allUsesExtend(Compare, CurDAG))
    return SDValue();

  SDValue LHS = Compare.getOperand(0);
  SDValue RHS = Compare.getOperand(1);

  // The condition code is operand 2 for SETCC and operand 4 for SELECT_CC.
  int CCOpNum = Compare.getOpcode() == ISD::SELECT_CC ? 4 : 2;
  ISD::CondCode CC =
    cast<CondCodeSDNode>(Compare.getOperand(CCOpNum))->get();
  EVT InputVT = LHS.getValueType();
  if (InputVT != MVT::i32 && InputVT != MVT::i64)
    return SDValue();

  if (ConvOpts == SetccInGPROpts::ZExtInvert ||
      ConvOpts == SetccInGPROpts::SExtInvert)
    CC = ISD::getSetCCInverse(CC, true);

  bool Inputs32Bit = InputVT == MVT::i32;

  SDLoc dl(Compare);
  ConstantSDNode *RHSConst = dyn_cast<ConstantSDNode>(RHS);
  int64_t RHSValue = RHSConst ? RHSConst->getSExtValue() : INT64_MAX;
  bool IsSext = ConvOpts == SetccInGPROpts::SExtOrig ||
    ConvOpts == SetccInGPROpts::SExtInvert;

  if (IsSext && Inputs32Bit)
    return get32BitSExtCompare(LHS, RHS, CC, RHSValue, dl);
  else if (Inputs32Bit)
    return get32BitZExtCompare(LHS, RHS, CC, RHSValue, dl);
  else if (IsSext)
    return get64BitSExtCompare(LHS, RHS, CC, RHSValue, dl);
  return get64BitZExtCompare(LHS, RHS, CC, RHSValue, dl);
}

} // end anonymous namespace

bool PPCDAGToDAGISel::tryIntCompareInGPR(SDNode *N) {
  if (N->getValueType(0) != MVT::i32 &&
      N->getValueType(0) != MVT::i64)
    return false;

  // This optimization will emit code that assumes 64-bit registers
  // so we don't want to run it in 32-bit mode. Also don't run it
  // on functions that are not to be optimized.
  if (TM.getOptLevel() == CodeGenOpt::None || !TM.isPPC64())
    return false;

  switch (N->getOpcode()) {
  default: break;
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR: {
    IntegerCompareEliminator ICmpElim(CurDAG, this);
    if (SDNode *New = ICmpElim.Select(N)) {
      ReplaceNode(N, New);
      return true;
    }
  }
  }
  return false;
}

bool PPCDAGToDAGISel::tryBitPermutation(SDNode *N) {
  if (N->getValueType(0) != MVT::i32 &&
      N->getValueType(0) != MVT::i64)
    return false;

  if (!UseBitPermRewriter)
    return false;

  switch (N->getOpcode()) {
  default: break;
  case ISD::ROTL:
  case ISD::SHL:
  case ISD::SRL:
  case ISD::AND:
  case ISD::OR: {
    BitPermutationSelector BPS(CurDAG);
    if (SDNode *New = BPS.Select(N)) {
      ReplaceNode(N, New);
      return true;
    }
    return false;
  }
  }

  return false;
}

/// SelectCC - Select a comparison of the specified values with the specified
/// condition code, returning the CR# of the expression.
SDValue PPCDAGToDAGISel::SelectCC(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                  const SDLoc &dl) {
  // Always select the LHS.
  unsigned Opc;

  if (LHS.getValueType() == MVT::i32) {
    unsigned Imm;
    if (CC == ISD::SETEQ || CC == ISD::SETNE) {
      if (isInt32Immediate(RHS, Imm)) {
        // SETEQ/SETNE comparison with 16-bit immediate, fold it.
        if (isUInt<16>(Imm))
          return SDValue(CurDAG->getMachineNode(PPC::CMPLWI, dl, MVT::i32, LHS,
                                                getI32Imm(Imm & 0xFFFF, dl)),
                         0);
        // If this is a 16-bit signed immediate, fold it.
        if (isInt<16>((int)Imm))
          return SDValue(CurDAG->getMachineNode(PPC::CMPWI, dl, MVT::i32, LHS,
                                                getI32Imm(Imm & 0xFFFF, dl)),
                         0);

        // For non-equality comparisons, the default code would materialize the
        // constant, then compare against it, like this:
        //   lis r2, 4660
        //   ori r2, r2, 22136
        //   cmpw cr0, r3, r2
        // Since we are just comparing for equality, we can emit this instead:
        //   xoris r0,r3,0x1234
        //   cmplwi cr0,r0,0x5678
        //   beq cr0,L6
        SDValue Xor(CurDAG->getMachineNode(PPC::XORIS, dl, MVT::i32, LHS,
                                           getI32Imm(Imm >> 16, dl)), 0);
        return SDValue(CurDAG->getMachineNode(PPC::CMPLWI, dl, MVT::i32, Xor,
                                              getI32Imm(Imm & 0xFFFF, dl)), 0);
      }
      Opc = PPC::CMPLW;
    } else if (ISD::isUnsignedIntSetCC(CC)) {
      if (isInt32Immediate(RHS, Imm) && isUInt<16>(Imm))
        return SDValue(CurDAG->getMachineNode(PPC::CMPLWI, dl, MVT::i32, LHS,
                                              getI32Imm(Imm & 0xFFFF, dl)), 0);
      Opc = PPC::CMPLW;
    } else {
      int16_t SImm;
      if (isIntS16Immediate(RHS, SImm))
        return SDValue(CurDAG->getMachineNode(PPC::CMPWI, dl, MVT::i32, LHS,
                                              getI32Imm((int)SImm & 0xFFFF,
                                                        dl)),
                         0);
      Opc = PPC::CMPW;
    }
  } else if (LHS.getValueType() == MVT::i64) {
    uint64_t Imm;
    if (CC == ISD::SETEQ || CC == ISD::SETNE) {
      if (isInt64Immediate(RHS.getNode(), Imm)) {
        // SETEQ/SETNE comparison with 16-bit immediate, fold it.
        if (isUInt<16>(Imm))
          return SDValue(CurDAG->getMachineNode(PPC::CMPLDI, dl, MVT::i64, LHS,
                                                getI32Imm(Imm & 0xFFFF, dl)),
                         0);
        // If this is a 16-bit signed immediate, fold it.
        if (isInt<16>(Imm))
          return SDValue(CurDAG->getMachineNode(PPC::CMPDI, dl, MVT::i64, LHS,
                                                getI32Imm(Imm & 0xFFFF, dl)),
                         0);

        // For non-equality comparisons, the default code would materialize the
        // constant, then compare against it, like this:
        //   lis r2, 4660
        //   ori r2, r2, 22136
        //   cmpd cr0, r3, r2
        // Since we are just comparing for equality, we can emit this instead:
        //   xoris r0,r3,0x1234
        //   cmpldi cr0,r0,0x5678
        //   beq cr0,L6
        if (isUInt<32>(Imm)) {
          SDValue Xor(CurDAG->getMachineNode(PPC::XORIS8, dl, MVT::i64, LHS,
                                             getI64Imm(Imm >> 16, dl)), 0);
          return SDValue(CurDAG->getMachineNode(PPC::CMPLDI, dl, MVT::i64, Xor,
                                                getI64Imm(Imm & 0xFFFF, dl)),
                         0);
        }
      }
      Opc = PPC::CMPLD;
    } else if (ISD::isUnsignedIntSetCC(CC)) {
      if (isInt64Immediate(RHS.getNode(), Imm) && isUInt<16>(Imm))
        return SDValue(CurDAG->getMachineNode(PPC::CMPLDI, dl, MVT::i64, LHS,
                                              getI64Imm(Imm & 0xFFFF, dl)), 0);
      Opc = PPC::CMPLD;
    } else {
      int16_t SImm;
      if (isIntS16Immediate(RHS, SImm))
        return SDValue(CurDAG->getMachineNode(PPC::CMPDI, dl, MVT::i64, LHS,
                                              getI64Imm(SImm & 0xFFFF, dl)),
                         0);
      Opc = PPC::CMPD;
    }
  } else if (LHS.getValueType() == MVT::f32) {
    if (PPCSubTarget->hasSPE()) {
      switch (CC) {
        default:
        case ISD::SETEQ:
        case ISD::SETNE:
          Opc = PPC::EFSCMPEQ;
          break;
        case ISD::SETLT:
        case ISD::SETGE:
        case ISD::SETOLT:
        case ISD::SETOGE:
        case ISD::SETULT:
        case ISD::SETUGE:
          Opc = PPC::EFSCMPLT;
          break;
        case ISD::SETGT:
        case ISD::SETLE:
        case ISD::SETOGT:
        case ISD::SETOLE:
        case ISD::SETUGT:
        case ISD::SETULE:
          Opc = PPC::EFSCMPGT;
          break;
      }
    } else
      Opc = PPC::FCMPUS;
  } else if (LHS.getValueType() == MVT::f64) {
    if (PPCSubTarget->hasSPE()) {
      switch (CC) {
        default:
        case ISD::SETEQ:
        case ISD::SETNE:
          Opc = PPC::EFDCMPEQ;
          break;
        case ISD::SETLT:
        case ISD::SETGE:
        case ISD::SETOLT:
        case ISD::SETOGE:
        case ISD::SETULT:
        case ISD::SETUGE:
          Opc = PPC::EFDCMPLT;
          break;
        case ISD::SETGT:
        case ISD::SETLE:
        case ISD::SETOGT:
        case ISD::SETOLE:
        case ISD::SETUGT:
        case ISD::SETULE:
          Opc = PPC::EFDCMPGT;
          break;
      }
    } else
      Opc = PPCSubTarget->hasVSX() ? PPC::XSCMPUDP : PPC::FCMPUD;
  } else {
    assert(LHS.getValueType() == MVT::f128 && "Unknown vt!");
    assert(PPCSubTarget->hasVSX() && "__float128 requires VSX");
    Opc = PPC::XSCMPUQP;
  }
  return SDValue(CurDAG->getMachineNode(Opc, dl, MVT::i32, LHS, RHS), 0);
}

static PPC::Predicate getPredicateForSetCC(ISD::CondCode CC) {
  switch (CC) {
  case ISD::SETUEQ:
  case ISD::SETONE:
  case ISD::SETOLE:
  case ISD::SETOGE:
    llvm_unreachable("Should be lowered by legalize!");
  default: llvm_unreachable("Unknown condition!");
  case ISD::SETOEQ:
  case ISD::SETEQ:  return PPC::PRED_EQ;
  case ISD::SETUNE:
  case ISD::SETNE:  return PPC::PRED_NE;
  case ISD::SETOLT:
  case ISD::SETLT:  return PPC::PRED_LT;
  case ISD::SETULE:
  case ISD::SETLE:  return PPC::PRED_LE;
  case ISD::SETOGT:
  case ISD::SETGT:  return PPC::PRED_GT;
  case ISD::SETUGE:
  case ISD::SETGE:  return PPC::PRED_GE;
  case ISD::SETO:   return PPC::PRED_NU;
  case ISD::SETUO:  return PPC::PRED_UN;
    // These two are invalid for floating point.  Assume we have int.
  case ISD::SETULT: return PPC::PRED_LT;
  case ISD::SETUGT: return PPC::PRED_GT;
  }
}

/// getCRIdxForSetCC - Return the index of the condition register field
/// associated with the SetCC condition, and whether or not the field is
/// treated as inverted.  That is, lt = 0; ge = 0 inverted.
static unsigned getCRIdxForSetCC(ISD::CondCode CC, bool &Invert) {
  Invert = false;
  switch (CC) {
  default: llvm_unreachable("Unknown condition!");
  case ISD::SETOLT:
  case ISD::SETLT:  return 0;                  // Bit #0 = SETOLT
  case ISD::SETOGT:
  case ISD::SETGT:  return 1;                  // Bit #1 = SETOGT
  case ISD::SETOEQ:
  case ISD::SETEQ:  return 2;                  // Bit #2 = SETOEQ
  case ISD::SETUO:  return 3;                  // Bit #3 = SETUO
  case ISD::SETUGE:
  case ISD::SETGE:  Invert = true; return 0;   // !Bit #0 = SETUGE
  case ISD::SETULE:
  case ISD::SETLE:  Invert = true; return 1;   // !Bit #1 = SETULE
  case ISD::SETUNE:
  case ISD::SETNE:  Invert = true; return 2;   // !Bit #2 = SETUNE
  case ISD::SETO:   Invert = true; return 3;   // !Bit #3 = SETO
  case ISD::SETUEQ:
  case ISD::SETOGE:
  case ISD::SETOLE:
  case ISD::SETONE:
    llvm_unreachable("Invalid branch code: should be expanded by legalize");
  // These are invalid for floating point.  Assume integer.
  case ISD::SETULT: return 0;
  case ISD::SETUGT: return 1;
  }
}

// getVCmpInst: return the vector compare instruction for the specified
// vector type and condition code. Since this is for altivec specific code,
// only support the altivec types (v16i8, v8i16, v4i32, v2i64, and v4f32).
static unsigned int getVCmpInst(MVT VecVT, ISD::CondCode CC,
                                bool HasVSX, bool &Swap, bool &Negate) {
  Swap = false;
  Negate = false;

  if (VecVT.isFloatingPoint()) {
    /* Handle some cases by swapping input operands.  */
    switch (CC) {
      case ISD::SETLE: CC = ISD::SETGE; Swap = true; break;
      case ISD::SETLT: CC = ISD::SETGT; Swap = true; break;
      case ISD::SETOLE: CC = ISD::SETOGE; Swap = true; break;
      case ISD::SETOLT: CC = ISD::SETOGT; Swap = true; break;
      case ISD::SETUGE: CC = ISD::SETULE; Swap = true; break;
      case ISD::SETUGT: CC = ISD::SETULT; Swap = true; break;
      default: break;
    }
    /* Handle some cases by negating the result.  */
    switch (CC) {
      case ISD::SETNE: CC = ISD::SETEQ; Negate = true; break;
      case ISD::SETUNE: CC = ISD::SETOEQ; Negate = true; break;
      case ISD::SETULE: CC = ISD::SETOGT; Negate = true; break;
      case ISD::SETULT: CC = ISD::SETOGE; Negate = true; break;
      default: break;
    }
    /* We have instructions implementing the remaining cases.  */
    switch (CC) {
      case ISD::SETEQ:
      case ISD::SETOEQ:
        if (VecVT == MVT::v4f32)
          return HasVSX ? PPC::XVCMPEQSP : PPC::VCMPEQFP;
        else if (VecVT == MVT::v2f64)
          return PPC::XVCMPEQDP;
        break;
      case ISD::SETGT:
      case ISD::SETOGT:
        if (VecVT == MVT::v4f32)
          return HasVSX ? PPC::XVCMPGTSP : PPC::VCMPGTFP;
        else if (VecVT == MVT::v2f64)
          return PPC::XVCMPGTDP;
        break;
      case ISD::SETGE:
      case ISD::SETOGE:
        if (VecVT == MVT::v4f32)
          return HasVSX ? PPC::XVCMPGESP : PPC::VCMPGEFP;
        else if (VecVT == MVT::v2f64)
          return PPC::XVCMPGEDP;
        break;
      default:
        break;
    }
    llvm_unreachable("Invalid floating-point vector compare condition");
  } else {
    /* Handle some cases by swapping input operands.  */
    switch (CC) {
      case ISD::SETGE: CC = ISD::SETLE; Swap = true; break;
      case ISD::SETLT: CC = ISD::SETGT; Swap = true; break;
      case ISD::SETUGE: CC = ISD::SETULE; Swap = true; break;
      case ISD::SETULT: CC = ISD::SETUGT; Swap = true; break;
      default: break;
    }
    /* Handle some cases by negating the result.  */
    switch (CC) {
      case ISD::SETNE: CC = ISD::SETEQ; Negate = true; break;
      case ISD::SETUNE: CC = ISD::SETUEQ; Negate = true; break;
      case ISD::SETLE: CC = ISD::SETGT; Negate = true; break;
      case ISD::SETULE: CC = ISD::SETUGT; Negate = true; break;
      default: break;
    }
    /* We have instructions implementing the remaining cases.  */
    switch (CC) {
      case ISD::SETEQ:
      case ISD::SETUEQ:
        if (VecVT == MVT::v16i8)
          return PPC::VCMPEQUB;
        else if (VecVT == MVT::v8i16)
          return PPC::VCMPEQUH;
        else if (VecVT == MVT::v4i32)
          return PPC::VCMPEQUW;
        else if (VecVT == MVT::v2i64)
          return PPC::VCMPEQUD;
        break;
      case ISD::SETGT:
        if (VecVT == MVT::v16i8)
          return PPC::VCMPGTSB;
        else if (VecVT == MVT::v8i16)
          return PPC::VCMPGTSH;
        else if (VecVT == MVT::v4i32)
          return PPC::VCMPGTSW;
        else if (VecVT == MVT::v2i64)
          return PPC::VCMPGTSD;
        break;
      case ISD::SETUGT:
        if (VecVT == MVT::v16i8)
          return PPC::VCMPGTUB;
        else if (VecVT == MVT::v8i16)
          return PPC::VCMPGTUH;
        else if (VecVT == MVT::v4i32)
          return PPC::VCMPGTUW;
        else if (VecVT == MVT::v2i64)
          return PPC::VCMPGTUD;
        break;
      default:
        break;
    }
    llvm_unreachable("Invalid integer vector compare condition");
  }
}

bool PPCDAGToDAGISel::trySETCC(SDNode *N) {
  SDLoc dl(N);
  unsigned Imm;
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  EVT PtrVT =
      CurDAG->getTargetLoweringInfo().getPointerTy(CurDAG->getDataLayout());
  bool isPPC64 = (PtrVT == MVT::i64);

  if (!PPCSubTarget->useCRBits() &&
      isInt32Immediate(N->getOperand(1), Imm)) {
    // We can codegen setcc op, imm very efficiently compared to a brcond.
    // Check for those cases here.
    // setcc op, 0
    if (Imm == 0) {
      SDValue Op = N->getOperand(0);
      switch (CC) {
      default: break;
      case ISD::SETEQ: {
        Op = SDValue(CurDAG->getMachineNode(PPC::CNTLZW, dl, MVT::i32, Op), 0);
        SDValue Ops[] = { Op, getI32Imm(27, dl), getI32Imm(5, dl),
                          getI32Imm(31, dl) };
        CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
        return true;
      }
      case ISD::SETNE: {
        if (isPPC64) break;
        SDValue AD =
          SDValue(CurDAG->getMachineNode(PPC::ADDIC, dl, MVT::i32, MVT::Glue,
                                         Op, getI32Imm(~0U, dl)), 0);
        CurDAG->SelectNodeTo(N, PPC::SUBFE, MVT::i32, AD, Op, AD.getValue(1));
        return true;
      }
      case ISD::SETLT: {
        SDValue Ops[] = { Op, getI32Imm(1, dl), getI32Imm(31, dl),
                          getI32Imm(31, dl) };
        CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
        return true;
      }
      case ISD::SETGT: {
        SDValue T =
          SDValue(CurDAG->getMachineNode(PPC::NEG, dl, MVT::i32, Op), 0);
        T = SDValue(CurDAG->getMachineNode(PPC::ANDC, dl, MVT::i32, T, Op), 0);
        SDValue Ops[] = { T, getI32Imm(1, dl), getI32Imm(31, dl),
                          getI32Imm(31, dl) };
        CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
        return true;
      }
      }
    } else if (Imm == ~0U) {        // setcc op, -1
      SDValue Op = N->getOperand(0);
      switch (CC) {
      default: break;
      case ISD::SETEQ:
        if (isPPC64) break;
        Op = SDValue(CurDAG->getMachineNode(PPC::ADDIC, dl, MVT::i32, MVT::Glue,
                                            Op, getI32Imm(1, dl)), 0);
        CurDAG->SelectNodeTo(N, PPC::ADDZE, MVT::i32,
                             SDValue(CurDAG->getMachineNode(PPC::LI, dl,
                                                            MVT::i32,
                                                            getI32Imm(0, dl)),
                                     0), Op.getValue(1));
        return true;
      case ISD::SETNE: {
        if (isPPC64) break;
        Op = SDValue(CurDAG->getMachineNode(PPC::NOR, dl, MVT::i32, Op, Op), 0);
        SDNode *AD = CurDAG->getMachineNode(PPC::ADDIC, dl, MVT::i32, MVT::Glue,
                                            Op, getI32Imm(~0U, dl));
        CurDAG->SelectNodeTo(N, PPC::SUBFE, MVT::i32, SDValue(AD, 0), Op,
                             SDValue(AD, 1));
        return true;
      }
      case ISD::SETLT: {
        SDValue AD = SDValue(CurDAG->getMachineNode(PPC::ADDI, dl, MVT::i32, Op,
                                                    getI32Imm(1, dl)), 0);
        SDValue AN = SDValue(CurDAG->getMachineNode(PPC::AND, dl, MVT::i32, AD,
                                                    Op), 0);
        SDValue Ops[] = { AN, getI32Imm(1, dl), getI32Imm(31, dl),
                          getI32Imm(31, dl) };
        CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
        return true;
      }
      case ISD::SETGT: {
        SDValue Ops[] = { Op, getI32Imm(1, dl), getI32Imm(31, dl),
                          getI32Imm(31, dl) };
        Op = SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, Ops), 0);
        CurDAG->SelectNodeTo(N, PPC::XORI, MVT::i32, Op, getI32Imm(1, dl));
        return true;
      }
      }
    }
  }

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // Altivec Vector compare instructions do not set any CR register by default and
  // vector compare operations return the same type as the operands.
  if (LHS.getValueType().isVector()) {
    if (PPCSubTarget->hasQPX() || PPCSubTarget->hasSPE())
      return false;

    EVT VecVT = LHS.getValueType();
    bool Swap, Negate;
    unsigned int VCmpInst = getVCmpInst(VecVT.getSimpleVT(), CC,
                                        PPCSubTarget->hasVSX(), Swap, Negate);
    if (Swap)
      std::swap(LHS, RHS);

    EVT ResVT = VecVT.changeVectorElementTypeToInteger();
    if (Negate) {
      SDValue VCmp(CurDAG->getMachineNode(VCmpInst, dl, ResVT, LHS, RHS), 0);
      CurDAG->SelectNodeTo(N, PPCSubTarget->hasVSX() ? PPC::XXLNOR : PPC::VNOR,
                           ResVT, VCmp, VCmp);
      return true;
    }

    CurDAG->SelectNodeTo(N, VCmpInst, ResVT, LHS, RHS);
    return true;
  }

  if (PPCSubTarget->useCRBits())
    return false;

  bool Inv;
  unsigned Idx = getCRIdxForSetCC(CC, Inv);
  SDValue CCReg = SelectCC(LHS, RHS, CC, dl);
  SDValue IntCR;

  // SPE e*cmp* instructions only set the 'gt' bit, so hard-code that
  // The correct compare instruction is already set by SelectCC()
  if (PPCSubTarget->hasSPE() && LHS.getValueType().isFloatingPoint()) {
    Idx = 1;
  }

  // Force the ccreg into CR7.
  SDValue CR7Reg = CurDAG->getRegister(PPC::CR7, MVT::i32);

  SDValue InFlag(nullptr, 0);  // Null incoming flag value.
  CCReg = CurDAG->getCopyToReg(CurDAG->getEntryNode(), dl, CR7Reg, CCReg,
                               InFlag).getValue(1);

  IntCR = SDValue(CurDAG->getMachineNode(PPC::MFOCRF, dl, MVT::i32, CR7Reg,
                                         CCReg), 0);

  SDValue Ops[] = { IntCR, getI32Imm((32 - (3 - Idx)) & 31, dl),
                      getI32Imm(31, dl), getI32Imm(31, dl) };
  if (!Inv) {
    CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
    return true;
  }

  // Get the specified bit.
  SDValue Tmp =
    SDValue(CurDAG->getMachineNode(PPC::RLWINM, dl, MVT::i32, Ops), 0);
  CurDAG->SelectNodeTo(N, PPC::XORI, MVT::i32, Tmp, getI32Imm(1, dl));
  return true;
}

/// Does this node represent a load/store node whose address can be represented
/// with a register plus an immediate that's a multiple of \p Val:
bool PPCDAGToDAGISel::isOffsetMultipleOf(SDNode *N, unsigned Val) const {
  LoadSDNode *LDN = dyn_cast<LoadSDNode>(N);
  StoreSDNode *STN = dyn_cast<StoreSDNode>(N);
  SDValue AddrOp;
  if (LDN)
    AddrOp = LDN->getOperand(1);
  else if (STN)
    AddrOp = STN->getOperand(2);

  // If the address points a frame object or a frame object with an offset,
  // we need to check the object alignment.
  short Imm = 0;
  if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(
          AddrOp.getOpcode() == ISD::ADD ? AddrOp.getOperand(0) :
                                           AddrOp)) {
    // If op0 is a frame index that is under aligned, we can't do it either,
    // because it is translated to r31 or r1 + slot + offset. We won't know the
    // slot number until the stack frame is finalized.
    const MachineFrameInfo &MFI = CurDAG->getMachineFunction().getFrameInfo();
    unsigned SlotAlign = MFI.getObjectAlignment(FI->getIndex());
    if ((SlotAlign % Val) != 0)
      return false;

    // If we have an offset, we need further check on the offset.
    if (AddrOp.getOpcode() != ISD::ADD)
      return true;
  }

  if (AddrOp.getOpcode() == ISD::ADD)
    return isIntS16Immediate(AddrOp.getOperand(1), Imm) && !(Imm % Val);

  // If the address comes from the outside, the offset will be zero.
  return AddrOp.getOpcode() == ISD::CopyFromReg;
}

void PPCDAGToDAGISel::transferMemOperands(SDNode *N, SDNode *Result) {
  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Result), {MemOp});
}

static bool mayUseP9Setb(SDNode *N, const ISD::CondCode &CC, SelectionDAG *DAG,
                         bool &NeedSwapOps, bool &IsUnCmp) {

  assert(N->getOpcode() == ISD::SELECT_CC && "Expecting a SELECT_CC here.");

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue TrueRes = N->getOperand(2);
  SDValue FalseRes = N->getOperand(3);
  ConstantSDNode *TrueConst = dyn_cast<ConstantSDNode>(TrueRes);
  if (!TrueConst)
    return false;

  assert((N->getSimpleValueType(0) == MVT::i64 ||
          N->getSimpleValueType(0) == MVT::i32) &&
         "Expecting either i64 or i32 here.");

  // We are looking for any of:
  // (select_cc lhs, rhs,  1, (sext (setcc [lr]hs, [lr]hs, cc2)), cc1)
  // (select_cc lhs, rhs, -1, (zext (setcc [lr]hs, [lr]hs, cc2)), cc1)
  // (select_cc lhs, rhs,  0, (select_cc [lr]hs, [lr]hs,  1, -1, cc2), seteq)
  // (select_cc lhs, rhs,  0, (select_cc [lr]hs, [lr]hs, -1,  1, cc2), seteq)
  int64_t TrueResVal = TrueConst->getSExtValue();
  if ((TrueResVal < -1 || TrueResVal > 1) ||
      (TrueResVal == -1 && FalseRes.getOpcode() != ISD::ZERO_EXTEND) ||
      (TrueResVal == 1 && FalseRes.getOpcode() != ISD::SIGN_EXTEND) ||
      (TrueResVal == 0 &&
       (FalseRes.getOpcode() != ISD::SELECT_CC || CC != ISD::SETEQ)))
    return false;

  bool InnerIsSel = FalseRes.getOpcode() == ISD::SELECT_CC;
  SDValue SetOrSelCC = InnerIsSel ? FalseRes : FalseRes.getOperand(0);
  if (SetOrSelCC.getOpcode() != ISD::SETCC &&
      SetOrSelCC.getOpcode() != ISD::SELECT_CC)
    return false;

  // Without this setb optimization, the outer SELECT_CC will be manually
  // selected to SELECT_CC_I4/SELECT_CC_I8 Pseudo, then expand-isel-pseudos pass
  // transforms pseduo instruction to isel instruction. When there are more than
  // one use for result like zext/sext, with current optimization we only see
  // isel is replaced by setb but can't see any significant gain. Since
  // setb has longer latency than original isel, we should avoid this. Another
  // point is that setb requires comparison always kept, it can break the
  // oppotunity to get the comparison away if we have in future.
  if (!SetOrSelCC.hasOneUse() || (!InnerIsSel && !FalseRes.hasOneUse()))
    return false;

  SDValue InnerLHS = SetOrSelCC.getOperand(0);
  SDValue InnerRHS = SetOrSelCC.getOperand(1);
  ISD::CondCode InnerCC =
      cast<CondCodeSDNode>(SetOrSelCC.getOperand(InnerIsSel ? 4 : 2))->get();
  // If the inner comparison is a select_cc, make sure the true/false values are
  // 1/-1 and canonicalize it if needed.
  if (InnerIsSel) {
    ConstantSDNode *SelCCTrueConst =
        dyn_cast<ConstantSDNode>(SetOrSelCC.getOperand(2));
    ConstantSDNode *SelCCFalseConst =
        dyn_cast<ConstantSDNode>(SetOrSelCC.getOperand(3));
    if (!SelCCTrueConst || !SelCCFalseConst)
      return false;
    int64_t SelCCTVal = SelCCTrueConst->getSExtValue();
    int64_t SelCCFVal = SelCCFalseConst->getSExtValue();
    // The values must be -1/1 (requiring a swap) or 1/-1.
    if (SelCCTVal == -1 && SelCCFVal == 1) {
      std::swap(InnerLHS, InnerRHS);
    } else if (SelCCTVal != 1 || SelCCFVal != -1)
      return false;
  }

  // Canonicalize unsigned case
  if (InnerCC == ISD::SETULT || InnerCC == ISD::SETUGT) {
    IsUnCmp = true;
    InnerCC = (InnerCC == ISD::SETULT) ? ISD::SETLT : ISD::SETGT;
  }

  bool InnerSwapped = false;
  if (LHS == InnerRHS && RHS == InnerLHS)
    InnerSwapped = true;
  else if (LHS != InnerLHS || RHS != InnerRHS)
    return false;

  switch (CC) {
  // (select_cc lhs, rhs,  0, \
  //     (select_cc [lr]hs, [lr]hs, 1, -1, setlt/setgt), seteq)
  case ISD::SETEQ:
    if (!InnerIsSel)
      return false;
    if (InnerCC != ISD::SETLT && InnerCC != ISD::SETGT)
      return false;
    NeedSwapOps = (InnerCC == ISD::SETGT) ? InnerSwapped : !InnerSwapped;
    break;

  // (select_cc lhs, rhs, -1, (zext (setcc [lr]hs, [lr]hs, setne)), setu?lt)
  // (select_cc lhs, rhs, -1, (zext (setcc lhs, rhs, setgt)), setu?lt)
  // (select_cc lhs, rhs, -1, (zext (setcc rhs, lhs, setlt)), setu?lt)
  // (select_cc lhs, rhs, 1, (sext (setcc [lr]hs, [lr]hs, setne)), setu?lt)
  // (select_cc lhs, rhs, 1, (sext (setcc lhs, rhs, setgt)), setu?lt)
  // (select_cc lhs, rhs, 1, (sext (setcc rhs, lhs, setlt)), setu?lt)
  case ISD::SETULT:
    if (!IsUnCmp && InnerCC != ISD::SETNE)
      return false;
    IsUnCmp = true;
    LLVM_FALLTHROUGH;
  case ISD::SETLT:
    if (InnerCC == ISD::SETNE || (InnerCC == ISD::SETGT && !InnerSwapped) ||
        (InnerCC == ISD::SETLT && InnerSwapped))
      NeedSwapOps = (TrueResVal == 1);
    else
      return false;
    break;

  // (select_cc lhs, rhs, 1, (sext (setcc [lr]hs, [lr]hs, setne)), setu?gt)
  // (select_cc lhs, rhs, 1, (sext (setcc lhs, rhs, setlt)), setu?gt)
  // (select_cc lhs, rhs, 1, (sext (setcc rhs, lhs, setgt)), setu?gt)
  // (select_cc lhs, rhs, -1, (zext (setcc [lr]hs, [lr]hs, setne)), setu?gt)
  // (select_cc lhs, rhs, -1, (zext (setcc lhs, rhs, setlt)), setu?gt)
  // (select_cc lhs, rhs, -1, (zext (setcc rhs, lhs, setgt)), setu?gt)
  case ISD::SETUGT:
    if (!IsUnCmp && InnerCC != ISD::SETNE)
      return false;
    IsUnCmp = true;
    LLVM_FALLTHROUGH;
  case ISD::SETGT:
    if (InnerCC == ISD::SETNE || (InnerCC == ISD::SETLT && !InnerSwapped) ||
        (InnerCC == ISD::SETGT && InnerSwapped))
      NeedSwapOps = (TrueResVal == -1);
    else
      return false;
    break;

  default:
    return false;
  }

  LLVM_DEBUG(dbgs() << "Found a node that can be lowered to a SETB: ");
  LLVM_DEBUG(N->dump());

  return true;
}

// Select - Convert the specified operand from a target-independent to a
// target-specific node if it hasn't already been changed.
void PPCDAGToDAGISel::Select(SDNode *N) {
  SDLoc dl(N);
  if (N->isMachineOpcode()) {
    N->setNodeId(-1);
    return;   // Already selected.
  }

  // In case any misguided DAG-level optimizations form an ADD with a
  // TargetConstant operand, crash here instead of miscompiling (by selecting
  // an r+r add instead of some kind of r+i add).
  if (N->getOpcode() == ISD::ADD &&
      N->getOperand(1).getOpcode() == ISD::TargetConstant)
    llvm_unreachable("Invalid ADD with TargetConstant operand");

  // Try matching complex bit permutations before doing anything else.
  if (tryBitPermutation(N))
    return;

  // Try to emit integer compares as GPR-only sequences (i.e. no use of CR).
  if (tryIntCompareInGPR(N))
    return;

  switch (N->getOpcode()) {
  default: break;

  case ISD::Constant:
    if (N->getValueType(0) == MVT::i64) {
      ReplaceNode(N, selectI64Imm(CurDAG, N));
      return;
    }
    break;

  case ISD::SETCC:
    if (trySETCC(N))
      return;
    break;

  case PPCISD::CALL: {
    const Module *M = MF->getFunction().getParent();

    if (PPCLowering->getPointerTy(CurDAG->getDataLayout()) != MVT::i32 ||
        !PPCSubTarget->isSecurePlt() || !PPCSubTarget->isTargetELF() ||
        M->getPICLevel() == PICLevel::SmallPIC)
      break;

    SDValue Op = N->getOperand(1);

    if (GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(Op)) {
      if (GA->getTargetFlags() == PPCII::MO_PLT)
        getGlobalBaseReg();
    }
    else if (ExternalSymbolSDNode *ES = dyn_cast<ExternalSymbolSDNode>(Op)) {
      if (ES->getTargetFlags() == PPCII::MO_PLT)
        getGlobalBaseReg();
    }
  }
    break;

  case PPCISD::GlobalBaseReg:
    ReplaceNode(N, getGlobalBaseReg());
    return;

  case ISD::FrameIndex:
    selectFrameIndex(N, N);
    return;

  case PPCISD::MFOCRF: {
    SDValue InFlag = N->getOperand(1);
    ReplaceNode(N, CurDAG->getMachineNode(PPC::MFOCRF, dl, MVT::i32,
                                          N->getOperand(0), InFlag));
    return;
  }

  case PPCISD::READ_TIME_BASE:
    ReplaceNode(N, CurDAG->getMachineNode(PPC::ReadTB, dl, MVT::i32, MVT::i32,
                                          MVT::Other, N->getOperand(0)));
    return;

  case PPCISD::SRA_ADDZE: {
    SDValue N0 = N->getOperand(0);
    SDValue ShiftAmt =
      CurDAG->getTargetConstant(*cast<ConstantSDNode>(N->getOperand(1))->
                                  getConstantIntValue(), dl,
                                  N->getValueType(0));
    if (N->getValueType(0) == MVT::i64) {
      SDNode *Op =
        CurDAG->getMachineNode(PPC::SRADI, dl, MVT::i64, MVT::Glue,
                               N0, ShiftAmt);
      CurDAG->SelectNodeTo(N, PPC::ADDZE8, MVT::i64, SDValue(Op, 0),
                           SDValue(Op, 1));
      return;
    } else {
      assert(N->getValueType(0) == MVT::i32 &&
             "Expecting i64 or i32 in PPCISD::SRA_ADDZE");
      SDNode *Op =
        CurDAG->getMachineNode(PPC::SRAWI, dl, MVT::i32, MVT::Glue,
                               N0, ShiftAmt);
      CurDAG->SelectNodeTo(N, PPC::ADDZE, MVT::i32, SDValue(Op, 0),
                           SDValue(Op, 1));
      return;
    }
  }

  case ISD::STORE: {
    // Change TLS initial-exec D-form stores to X-form stores.
    StoreSDNode *ST = cast<StoreSDNode>(N);
    if (EnableTLSOpt && PPCSubTarget->isELFv2ABI() &&
        ST->getAddressingMode() != ISD::PRE_INC)
      if (tryTLSXFormStore(ST))
        return;
    break;
  }
  case ISD::LOAD: {
    // Handle preincrement loads.
    LoadSDNode *LD = cast<LoadSDNode>(N);
    EVT LoadedVT = LD->getMemoryVT();

    // Normal loads are handled by code generated from the .td file.
    if (LD->getAddressingMode() != ISD::PRE_INC) {
      // Change TLS initial-exec D-form loads to X-form loads.
      if (EnableTLSOpt && PPCSubTarget->isELFv2ABI())
        if (tryTLSXFormLoad(LD))
          return;
      break;
    }

    SDValue Offset = LD->getOffset();
    if (Offset.getOpcode() == ISD::TargetConstant ||
        Offset.getOpcode() == ISD::TargetGlobalAddress) {

      unsigned Opcode;
      bool isSExt = LD->getExtensionType() == ISD::SEXTLOAD;
      if (LD->getValueType(0) != MVT::i64) {
        // Handle PPC32 integer and normal FP loads.
        assert((!isSExt || LoadedVT == MVT::i16) && "Invalid sext update load");
        switch (LoadedVT.getSimpleVT().SimpleTy) {
          default: llvm_unreachable("Invalid PPC load type!");
          case MVT::f64: Opcode = PPC::LFDU; break;
          case MVT::f32: Opcode = PPC::LFSU; break;
          case MVT::i32: Opcode = PPC::LWZU; break;
          case MVT::i16: Opcode = isSExt ? PPC::LHAU : PPC::LHZU; break;
          case MVT::i1:
          case MVT::i8:  Opcode = PPC::LBZU; break;
        }
      } else {
        assert(LD->getValueType(0) == MVT::i64 && "Unknown load result type!");
        assert((!isSExt || LoadedVT == MVT::i16) && "Invalid sext update load");
        switch (LoadedVT.getSimpleVT().SimpleTy) {
          default: llvm_unreachable("Invalid PPC load type!");
          case MVT::i64: Opcode = PPC::LDU; break;
          case MVT::i32: Opcode = PPC::LWZU8; break;
          case MVT::i16: Opcode = isSExt ? PPC::LHAU8 : PPC::LHZU8; break;
          case MVT::i1:
          case MVT::i8:  Opcode = PPC::LBZU8; break;
        }
      }

      SDValue Chain = LD->getChain();
      SDValue Base = LD->getBasePtr();
      SDValue Ops[] = { Offset, Base, Chain };
      SDNode *MN = CurDAG->getMachineNode(
          Opcode, dl, LD->getValueType(0),
          PPCLowering->getPointerTy(CurDAG->getDataLayout()), MVT::Other, Ops);
      transferMemOperands(N, MN);
      ReplaceNode(N, MN);
      return;
    } else {
      unsigned Opcode;
      bool isSExt = LD->getExtensionType() == ISD::SEXTLOAD;
      if (LD->getValueType(0) != MVT::i64) {
        // Handle PPC32 integer and normal FP loads.
        assert((!isSExt || LoadedVT == MVT::i16) && "Invalid sext update load");
        switch (LoadedVT.getSimpleVT().SimpleTy) {
          default: llvm_unreachable("Invalid PPC load type!");
          case MVT::v4f64: Opcode = PPC::QVLFDUX; break; // QPX
          case MVT::v4f32: Opcode = PPC::QVLFSUX; break; // QPX
          case MVT::f64: Opcode = PPC::LFDUX; break;
          case MVT::f32: Opcode = PPC::LFSUX; break;
          case MVT::i32: Opcode = PPC::LWZUX; break;
          case MVT::i16: Opcode = isSExt ? PPC::LHAUX : PPC::LHZUX; break;
          case MVT::i1:
          case MVT::i8:  Opcode = PPC::LBZUX; break;
        }
      } else {
        assert(LD->getValueType(0) == MVT::i64 && "Unknown load result type!");
        assert((!isSExt || LoadedVT == MVT::i16 || LoadedVT == MVT::i32) &&
               "Invalid sext update load");
        switch (LoadedVT.getSimpleVT().SimpleTy) {
          default: llvm_unreachable("Invalid PPC load type!");
          case MVT::i64: Opcode = PPC::LDUX; break;
          case MVT::i32: Opcode = isSExt ? PPC::LWAUX  : PPC::LWZUX8; break;
          case MVT::i16: Opcode = isSExt ? PPC::LHAUX8 : PPC::LHZUX8; break;
          case MVT::i1:
          case MVT::i8:  Opcode = PPC::LBZUX8; break;
        }
      }

      SDValue Chain = LD->getChain();
      SDValue Base = LD->getBasePtr();
      SDValue Ops[] = { Base, Offset, Chain };
      SDNode *MN = CurDAG->getMachineNode(
          Opcode, dl, LD->getValueType(0),
          PPCLowering->getPointerTy(CurDAG->getDataLayout()), MVT::Other, Ops);
      transferMemOperands(N, MN);
      ReplaceNode(N, MN);
      return;
    }
  }

  case ISD::AND: {
    unsigned Imm, Imm2, SH, MB, ME;
    uint64_t Imm64;

    // If this is an and of a value rotated between 0 and 31 bits and then and'd
    // with a mask, emit rlwinm
    if (isInt32Immediate(N->getOperand(1), Imm) &&
        isRotateAndMask(N->getOperand(0).getNode(), Imm, false, SH, MB, ME)) {
      SDValue Val = N->getOperand(0).getOperand(0);
      SDValue Ops[] = { Val, getI32Imm(SH, dl), getI32Imm(MB, dl),
                        getI32Imm(ME, dl) };
      CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
      return;
    }
    // If this is just a masked value where the input is not handled above, and
    // is not a rotate-left (handled by a pattern in the .td file), emit rlwinm
    if (isInt32Immediate(N->getOperand(1), Imm) &&
        isRunOfOnes(Imm, MB, ME) &&
        N->getOperand(0).getOpcode() != ISD::ROTL) {
      SDValue Val = N->getOperand(0);
      SDValue Ops[] = { Val, getI32Imm(0, dl), getI32Imm(MB, dl),
                        getI32Imm(ME, dl) };
      CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
      return;
    }
    // If this is a 64-bit zero-extension mask, emit rldicl.
    if (isInt64Immediate(N->getOperand(1).getNode(), Imm64) &&
        isMask_64(Imm64)) {
      SDValue Val = N->getOperand(0);
      MB = 64 - countTrailingOnes(Imm64);
      SH = 0;

      if (Val.getOpcode() == ISD::ANY_EXTEND) {
        auto Op0 = Val.getOperand(0);
        if ( Op0.getOpcode() == ISD::SRL &&
           isInt32Immediate(Op0.getOperand(1).getNode(), Imm) && Imm <= MB) {

           auto ResultType = Val.getNode()->getValueType(0);
           auto ImDef = CurDAG->getMachineNode(PPC::IMPLICIT_DEF, dl,
                                               ResultType);
           SDValue IDVal (ImDef, 0);

           Val = SDValue(CurDAG->getMachineNode(PPC::INSERT_SUBREG, dl,
                         ResultType, IDVal, Op0.getOperand(0),
                         getI32Imm(1, dl)), 0);
           SH = 64 - Imm;
        }
      }

      // If the operand is a logical right shift, we can fold it into this
      // instruction: rldicl(rldicl(x, 64-n, n), 0, mb) -> rldicl(x, 64-n, mb)
      // for n <= mb. The right shift is really a left rotate followed by a
      // mask, and this mask is a more-restrictive sub-mask of the mask implied
      // by the shift.
      if (Val.getOpcode() == ISD::SRL &&
          isInt32Immediate(Val.getOperand(1).getNode(), Imm) && Imm <= MB) {
        assert(Imm < 64 && "Illegal shift amount");
        Val = Val.getOperand(0);
        SH = 64 - Imm;
      }

      SDValue Ops[] = { Val, getI32Imm(SH, dl), getI32Imm(MB, dl) };
      CurDAG->SelectNodeTo(N, PPC::RLDICL, MVT::i64, Ops);
      return;
    }
    // If this is a negated 64-bit zero-extension mask,
    // i.e. the immediate is a sequence of ones from most significant side
    // and all zero for reminder, we should use rldicr.
    if (isInt64Immediate(N->getOperand(1).getNode(), Imm64) &&
        isMask_64(~Imm64)) {
      SDValue Val = N->getOperand(0);
      MB = 63 - countTrailingOnes(~Imm64);
      SH = 0;
      SDValue Ops[] = { Val, getI32Imm(SH, dl), getI32Imm(MB, dl) };
      CurDAG->SelectNodeTo(N, PPC::RLDICR, MVT::i64, Ops);
      return;
    }

    // AND X, 0 -> 0, not "rlwinm 32".
    if (isInt32Immediate(N->getOperand(1), Imm) && (Imm == 0)) {
      ReplaceUses(SDValue(N, 0), N->getOperand(1));
      return;
    }
    // ISD::OR doesn't get all the bitfield insertion fun.
    // (and (or x, c1), c2) where isRunOfOnes(~(c1^c2)) might be a
    // bitfield insert.
    if (isInt32Immediate(N->getOperand(1), Imm) &&
        N->getOperand(0).getOpcode() == ISD::OR &&
        isInt32Immediate(N->getOperand(0).getOperand(1), Imm2)) {
      // The idea here is to check whether this is equivalent to:
      //   (c1 & m) | (x & ~m)
      // where m is a run-of-ones mask. The logic here is that, for each bit in
      // c1 and c2:
      //  - if both are 1, then the output will be 1.
      //  - if both are 0, then the output will be 0.
      //  - if the bit in c1 is 0, and the bit in c2 is 1, then the output will
      //    come from x.
      //  - if the bit in c1 is 1, and the bit in c2 is 0, then the output will
      //    be 0.
      //  If that last condition is never the case, then we can form m from the
      //  bits that are the same between c1 and c2.
      unsigned MB, ME;
      if (isRunOfOnes(~(Imm^Imm2), MB, ME) && !(~Imm & Imm2)) {
        SDValue Ops[] = { N->getOperand(0).getOperand(0),
                            N->getOperand(0).getOperand(1),
                            getI32Imm(0, dl), getI32Imm(MB, dl),
                            getI32Imm(ME, dl) };
        ReplaceNode(N, CurDAG->getMachineNode(PPC::RLWIMI, dl, MVT::i32, Ops));
        return;
      }
    }

    // Other cases are autogenerated.
    break;
  }
  case ISD::OR: {
    if (N->getValueType(0) == MVT::i32)
      if (tryBitfieldInsert(N))
        return;

    int16_t Imm;
    if (N->getOperand(0)->getOpcode() == ISD::FrameIndex &&
        isIntS16Immediate(N->getOperand(1), Imm)) {
      KnownBits LHSKnown = CurDAG->computeKnownBits(N->getOperand(0));

      // If this is equivalent to an add, then we can fold it with the
      // FrameIndex calculation.
      if ((LHSKnown.Zero.getZExtValue()|~(uint64_t)Imm) == ~0ULL) {
        selectFrameIndex(N, N->getOperand(0).getNode(), (int)Imm);
        return;
      }
    }

    // OR with a 32-bit immediate can be handled by ori + oris
    // without creating an immediate in a GPR.
    uint64_t Imm64 = 0;
    bool IsPPC64 = PPCSubTarget->isPPC64();
    if (IsPPC64 && isInt64Immediate(N->getOperand(1), Imm64) &&
        (Imm64 & ~0xFFFFFFFFuLL) == 0) {
      // If ImmHi (ImmHi) is zero, only one ori (oris) is generated later.
      uint64_t ImmHi = Imm64 >> 16;
      uint64_t ImmLo = Imm64 & 0xFFFF;
      if (ImmHi != 0 && ImmLo != 0) {
        SDNode *Lo = CurDAG->getMachineNode(PPC::ORI8, dl, MVT::i64,
                                            N->getOperand(0),
                                            getI16Imm(ImmLo, dl));
        SDValue Ops1[] = { SDValue(Lo, 0), getI16Imm(ImmHi, dl)};
        CurDAG->SelectNodeTo(N, PPC::ORIS8, MVT::i64, Ops1);
        return;
      }
    }

    // Other cases are autogenerated.
    break;
  }
  case ISD::XOR: {
    // XOR with a 32-bit immediate can be handled by xori + xoris
    // without creating an immediate in a GPR.
    uint64_t Imm64 = 0;
    bool IsPPC64 = PPCSubTarget->isPPC64();
    if (IsPPC64 && isInt64Immediate(N->getOperand(1), Imm64) &&
        (Imm64 & ~0xFFFFFFFFuLL) == 0) {
      // If ImmHi (ImmHi) is zero, only one xori (xoris) is generated later.
      uint64_t ImmHi = Imm64 >> 16;
      uint64_t ImmLo = Imm64 & 0xFFFF;
      if (ImmHi != 0 && ImmLo != 0) {
        SDNode *Lo = CurDAG->getMachineNode(PPC::XORI8, dl, MVT::i64,
                                            N->getOperand(0),
                                            getI16Imm(ImmLo, dl));
        SDValue Ops1[] = { SDValue(Lo, 0), getI16Imm(ImmHi, dl)};
        CurDAG->SelectNodeTo(N, PPC::XORIS8, MVT::i64, Ops1);
        return;
      }
    }

    break;
  }
  case ISD::ADD: {
    int16_t Imm;
    if (N->getOperand(0)->getOpcode() == ISD::FrameIndex &&
        isIntS16Immediate(N->getOperand(1), Imm)) {
      selectFrameIndex(N, N->getOperand(0).getNode(), (int)Imm);
      return;
    }

    break;
  }
  case ISD::SHL: {
    unsigned Imm, SH, MB, ME;
    if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::AND, Imm) &&
        isRotateAndMask(N, Imm, true, SH, MB, ME)) {
      SDValue Ops[] = { N->getOperand(0).getOperand(0),
                          getI32Imm(SH, dl), getI32Imm(MB, dl),
                          getI32Imm(ME, dl) };
      CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
      return;
    }

    // Other cases are autogenerated.
    break;
  }
  case ISD::SRL: {
    unsigned Imm, SH, MB, ME;
    if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::AND, Imm) &&
        isRotateAndMask(N, Imm, true, SH, MB, ME)) {
      SDValue Ops[] = { N->getOperand(0).getOperand(0),
                          getI32Imm(SH, dl), getI32Imm(MB, dl),
                          getI32Imm(ME, dl) };
      CurDAG->SelectNodeTo(N, PPC::RLWINM, MVT::i32, Ops);
      return;
    }

    // Other cases are autogenerated.
    break;
  }
  // FIXME: Remove this once the ANDI glue bug is fixed:
  case PPCISD::ANDIo_1_EQ_BIT:
  case PPCISD::ANDIo_1_GT_BIT: {
    if (!ANDIGlueBug)
      break;

    EVT InVT = N->getOperand(0).getValueType();
    assert((InVT == MVT::i64 || InVT == MVT::i32) &&
           "Invalid input type for ANDIo_1_EQ_BIT");

    unsigned Opcode = (InVT == MVT::i64) ? PPC::ANDIo8 : PPC::ANDIo;
    SDValue AndI(CurDAG->getMachineNode(Opcode, dl, InVT, MVT::Glue,
                                        N->getOperand(0),
                                        CurDAG->getTargetConstant(1, dl, InVT)),
                 0);
    SDValue CR0Reg = CurDAG->getRegister(PPC::CR0, MVT::i32);
    SDValue SRIdxVal =
      CurDAG->getTargetConstant(N->getOpcode() == PPCISD::ANDIo_1_EQ_BIT ?
                                PPC::sub_eq : PPC::sub_gt, dl, MVT::i32);

    CurDAG->SelectNodeTo(N, TargetOpcode::EXTRACT_SUBREG, MVT::i1, CR0Reg,
                         SRIdxVal, SDValue(AndI.getNode(), 1) /* glue */);
    return;
  }
  case ISD::SELECT_CC: {
    ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(4))->get();
    EVT PtrVT =
        CurDAG->getTargetLoweringInfo().getPointerTy(CurDAG->getDataLayout());
    bool isPPC64 = (PtrVT == MVT::i64);

    // If this is a select of i1 operands, we'll pattern match it.
    if (PPCSubTarget->useCRBits() &&
        N->getOperand(0).getValueType() == MVT::i1)
      break;

    if (PPCSubTarget->isISA3_0() && PPCSubTarget->isPPC64()) {
      bool NeedSwapOps = false;
      bool IsUnCmp = false;
      if (mayUseP9Setb(N, CC, CurDAG, NeedSwapOps, IsUnCmp)) {
        SDValue LHS = N->getOperand(0);
        SDValue RHS = N->getOperand(1);
        if (NeedSwapOps)
          std::swap(LHS, RHS);

        // Make use of SelectCC to generate the comparison to set CR bits, for
        // equality comparisons having one literal operand, SelectCC probably
        // doesn't need to materialize the whole literal and just use xoris to
        // check it first, it leads the following comparison result can't
        // exactly represent GT/LT relationship. So to avoid this we specify
        // SETGT/SETUGT here instead of SETEQ.
        SDValue GenCC =
            SelectCC(LHS, RHS, IsUnCmp ? ISD::SETUGT : ISD::SETGT, dl);
        CurDAG->SelectNodeTo(
            N, N->getSimpleValueType(0) == MVT::i64 ? PPC::SETB8 : PPC::SETB,
            N->getValueType(0), GenCC);
        NumP9Setb++;
        return;
      }
    }

    // Handle the setcc cases here.  select_cc lhs, 0, 1, 0, cc
    if (!isPPC64)
      if (ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1)))
        if (ConstantSDNode *N2C = dyn_cast<ConstantSDNode>(N->getOperand(2)))
          if (ConstantSDNode *N3C = dyn_cast<ConstantSDNode>(N->getOperand(3)))
            if (N1C->isNullValue() && N3C->isNullValue() &&
                N2C->getZExtValue() == 1ULL && CC == ISD::SETNE &&
                // FIXME: Implement this optzn for PPC64.
                N->getValueType(0) == MVT::i32) {
              SDNode *Tmp =
                CurDAG->getMachineNode(PPC::ADDIC, dl, MVT::i32, MVT::Glue,
                                       N->getOperand(0), getI32Imm(~0U, dl));
              CurDAG->SelectNodeTo(N, PPC::SUBFE, MVT::i32, SDValue(Tmp, 0),
                                   N->getOperand(0), SDValue(Tmp, 1));
              return;
            }

    SDValue CCReg = SelectCC(N->getOperand(0), N->getOperand(1), CC, dl);

    if (N->getValueType(0) == MVT::i1) {
      // An i1 select is: (c & t) | (!c & f).
      bool Inv;
      unsigned Idx = getCRIdxForSetCC(CC, Inv);

      unsigned SRI;
      switch (Idx) {
      default: llvm_unreachable("Invalid CC index");
      case 0: SRI = PPC::sub_lt; break;
      case 1: SRI = PPC::sub_gt; break;
      case 2: SRI = PPC::sub_eq; break;
      case 3: SRI = PPC::sub_un; break;
      }

      SDValue CCBit = CurDAG->getTargetExtractSubreg(SRI, dl, MVT::i1, CCReg);

      SDValue NotCCBit(CurDAG->getMachineNode(PPC::CRNOR, dl, MVT::i1,
                                              CCBit, CCBit), 0);
      SDValue C =    Inv ? NotCCBit : CCBit,
              NotC = Inv ? CCBit    : NotCCBit;

      SDValue CAndT(CurDAG->getMachineNode(PPC::CRAND, dl, MVT::i1,
                                           C, N->getOperand(2)), 0);
      SDValue NotCAndF(CurDAG->getMachineNode(PPC::CRAND, dl, MVT::i1,
                                              NotC, N->getOperand(3)), 0);

      CurDAG->SelectNodeTo(N, PPC::CROR, MVT::i1, CAndT, NotCAndF);
      return;
    }

    unsigned BROpc = getPredicateForSetCC(CC);

    unsigned SelectCCOp;
    if (N->getValueType(0) == MVT::i32)
      SelectCCOp = PPC::SELECT_CC_I4;
    else if (N->getValueType(0) == MVT::i64)
      SelectCCOp = PPC::SELECT_CC_I8;
    else if (N->getValueType(0) == MVT::f32) {
      if (PPCSubTarget->hasP8Vector())
        SelectCCOp = PPC::SELECT_CC_VSSRC;
      else if (PPCSubTarget->hasSPE())
        SelectCCOp = PPC::SELECT_CC_SPE4;
      else
        SelectCCOp = PPC::SELECT_CC_F4;
    } else if (N->getValueType(0) == MVT::f64) {
      if (PPCSubTarget->hasVSX())
        SelectCCOp = PPC::SELECT_CC_VSFRC;
      else if (PPCSubTarget->hasSPE())
        SelectCCOp = PPC::SELECT_CC_SPE;
      else
        SelectCCOp = PPC::SELECT_CC_F8;
    } else if (N->getValueType(0) == MVT::f128)
      SelectCCOp = PPC::SELECT_CC_F16;
    else if (PPCSubTarget->hasSPE())
      SelectCCOp = PPC::SELECT_CC_SPE;
    else if (PPCSubTarget->hasQPX() && N->getValueType(0) == MVT::v4f64)
      SelectCCOp = PPC::SELECT_CC_QFRC;
    else if (PPCSubTarget->hasQPX() && N->getValueType(0) == MVT::v4f32)
      SelectCCOp = PPC::SELECT_CC_QSRC;
    else if (PPCSubTarget->hasQPX() && N->getValueType(0) == MVT::v4i1)
      SelectCCOp = PPC::SELECT_CC_QBRC;
    else if (N->getValueType(0) == MVT::v2f64 ||
             N->getValueType(0) == MVT::v2i64)
      SelectCCOp = PPC::SELECT_CC_VSRC;
    else
      SelectCCOp = PPC::SELECT_CC_VRRC;

    SDValue Ops[] = { CCReg, N->getOperand(2), N->getOperand(3),
                        getI32Imm(BROpc, dl) };
    CurDAG->SelectNodeTo(N, SelectCCOp, N->getValueType(0), Ops);
    return;
  }
  case ISD::VECTOR_SHUFFLE:
    if (PPCSubTarget->hasVSX() && (N->getValueType(0) == MVT::v2f64 ||
                                  N->getValueType(0) == MVT::v2i64)) {
      ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N);

      SDValue Op1 = N->getOperand(SVN->getMaskElt(0) < 2 ? 0 : 1),
              Op2 = N->getOperand(SVN->getMaskElt(1) < 2 ? 0 : 1);
      unsigned DM[2];

      for (int i = 0; i < 2; ++i)
        if (SVN->getMaskElt(i) <= 0 || SVN->getMaskElt(i) == 2)
          DM[i] = 0;
        else
          DM[i] = 1;

      if (Op1 == Op2 && DM[0] == 0 && DM[1] == 0 &&
          Op1.getOpcode() == ISD::SCALAR_TO_VECTOR &&
          isa<LoadSDNode>(Op1.getOperand(0))) {
        LoadSDNode *LD = cast<LoadSDNode>(Op1.getOperand(0));
        SDValue Base, Offset;

        if (LD->isUnindexed() && LD->hasOneUse() && Op1.hasOneUse() &&
            (LD->getMemoryVT() == MVT::f64 ||
             LD->getMemoryVT() == MVT::i64) &&
            SelectAddrIdxOnly(LD->getBasePtr(), Base, Offset)) {
          SDValue Chain = LD->getChain();
          SDValue Ops[] = { Base, Offset, Chain };
          MachineMemOperand *MemOp = LD->getMemOperand();
          SDNode *NewN = CurDAG->SelectNodeTo(N, PPC::LXVDSX,
                                              N->getValueType(0), Ops);
          CurDAG->setNodeMemRefs(cast<MachineSDNode>(NewN), {MemOp});
          return;
        }
      }

      // For little endian, we must swap the input operands and adjust
      // the mask elements (reverse and invert them).
      if (PPCSubTarget->isLittleEndian()) {
        std::swap(Op1, Op2);
        unsigned tmp = DM[0];
        DM[0] = 1 - DM[1];
        DM[1] = 1 - tmp;
      }

      SDValue DMV = CurDAG->getTargetConstant(DM[1] | (DM[0] << 1), dl,
                                              MVT::i32);
      SDValue Ops[] = { Op1, Op2, DMV };
      CurDAG->SelectNodeTo(N, PPC::XXPERMDI, N->getValueType(0), Ops);
      return;
    }

    break;
  case PPCISD::BDNZ:
  case PPCISD::BDZ: {
    bool IsPPC64 = PPCSubTarget->isPPC64();
    SDValue Ops[] = { N->getOperand(1), N->getOperand(0) };
    CurDAG->SelectNodeTo(N, N->getOpcode() == PPCISD::BDNZ
                                ? (IsPPC64 ? PPC::BDNZ8 : PPC::BDNZ)
                                : (IsPPC64 ? PPC::BDZ8 : PPC::BDZ),
                         MVT::Other, Ops);
    return;
  }
  case PPCISD::COND_BRANCH: {
    // Op #0 is the Chain.
    // Op #1 is the PPC::PRED_* number.
    // Op #2 is the CR#
    // Op #3 is the Dest MBB
    // Op #4 is the Flag.
    // Prevent PPC::PRED_* from being selected into LI.
    unsigned PCC = cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
    if (EnableBranchHint)
      PCC |= getBranchHint(PCC, FuncInfo, N->getOperand(3));

    SDValue Pred = getI32Imm(PCC, dl);
    SDValue Ops[] = { Pred, N->getOperand(2), N->getOperand(3),
      N->getOperand(0), N->getOperand(4) };
    CurDAG->SelectNodeTo(N, PPC::BCC, MVT::Other, Ops);
    return;
  }
  case ISD::BR_CC: {
    ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
    unsigned PCC = getPredicateForSetCC(CC);

    if (N->getOperand(2).getValueType() == MVT::i1) {
      unsigned Opc;
      bool Swap;
      switch (PCC) {
      default: llvm_unreachable("Unexpected Boolean-operand predicate");
      case PPC::PRED_LT: Opc = PPC::CRANDC; Swap = true;  break;
      case PPC::PRED_LE: Opc = PPC::CRORC;  Swap = true;  break;
      case PPC::PRED_EQ: Opc = PPC::CREQV;  Swap = false; break;
      case PPC::PRED_GE: Opc = PPC::CRORC;  Swap = false; break;
      case PPC::PRED_GT: Opc = PPC::CRANDC; Swap = false; break;
      case PPC::PRED_NE: Opc = PPC::CRXOR;  Swap = false; break;
      }

      // A signed comparison of i1 values produces the opposite result to an
      // unsigned one if the condition code includes less-than or greater-than.
      // This is because 1 is the most negative signed i1 number and the most
      // positive unsigned i1 number. The CR-logical operations used for such
      // comparisons are non-commutative so for signed comparisons vs. unsigned
      // ones, the input operands just need to be swapped.
      if (ISD::isSignedIntSetCC(CC))
        Swap = !Swap;

      SDValue BitComp(CurDAG->getMachineNode(Opc, dl, MVT::i1,
                                             N->getOperand(Swap ? 3 : 2),
                                             N->getOperand(Swap ? 2 : 3)), 0);
      CurDAG->SelectNodeTo(N, PPC::BC, MVT::Other, BitComp, N->getOperand(4),
                           N->getOperand(0));
      return;
    }

    if (EnableBranchHint)
      PCC |= getBranchHint(PCC, FuncInfo, N->getOperand(4));

    SDValue CondCode = SelectCC(N->getOperand(2), N->getOperand(3), CC, dl);
    SDValue Ops[] = { getI32Imm(PCC, dl), CondCode,
                        N->getOperand(4), N->getOperand(0) };
    CurDAG->SelectNodeTo(N, PPC::BCC, MVT::Other, Ops);
    return;
  }
  case ISD::BRIND: {
    // FIXME: Should custom lower this.
    SDValue Chain = N->getOperand(0);
    SDValue Target = N->getOperand(1);
    unsigned Opc = Target.getValueType() == MVT::i32 ? PPC::MTCTR : PPC::MTCTR8;
    unsigned Reg = Target.getValueType() == MVT::i32 ? PPC::BCTR : PPC::BCTR8;
    Chain = SDValue(CurDAG->getMachineNode(Opc, dl, MVT::Glue, Target,
                                           Chain), 0);
    CurDAG->SelectNodeTo(N, Reg, MVT::Other, Chain);
    return;
  }
  case PPCISD::TOC_ENTRY: {
    assert ((PPCSubTarget->isPPC64() || PPCSubTarget->isSVR4ABI()) &&
            "Only supported for 64-bit ABI and 32-bit SVR4");
    if (PPCSubTarget->isSVR4ABI() && !PPCSubTarget->isPPC64()) {
      SDValue GA = N->getOperand(0);
      SDNode *MN = CurDAG->getMachineNode(PPC::LWZtoc, dl, MVT::i32, GA,
                                          N->getOperand(1));
      transferMemOperands(N, MN);
      ReplaceNode(N, MN);
      return;
    }

    // For medium and large code model, we generate two instructions as
    // described below.  Otherwise we allow SelectCodeCommon to handle this,
    // selecting one of LDtoc, LDtocJTI, LDtocCPT, and LDtocBA.
    CodeModel::Model CModel = TM.getCodeModel();
    if (CModel != CodeModel::Medium && CModel != CodeModel::Large)
      break;

    // The first source operand is a TargetGlobalAddress or a TargetJumpTable.
    // If it must be toc-referenced according to PPCSubTarget, we generate:
    //   LDtocL(@sym, ADDIStocHA(%x2, @sym))
    // Otherwise we generate:
    //   ADDItocL(ADDIStocHA(%x2, @sym), @sym)
    SDValue GA = N->getOperand(0);
    SDValue TOCbase = N->getOperand(1);
    SDNode *Tmp = CurDAG->getMachineNode(PPC::ADDIStocHA, dl, MVT::i64,
                                         TOCbase, GA);
    if (PPCLowering->isAccessedAsGotIndirect(GA)) {
      // If it is access as got-indirect, we need an extra LD to load
      // the address.
      SDNode *MN = CurDAG->getMachineNode(PPC::LDtocL, dl, MVT::i64, GA,
                                          SDValue(Tmp, 0));
      transferMemOperands(N, MN);
      ReplaceNode(N, MN);
      return;
    }

    // Build the address relative to the TOC-pointer..
    ReplaceNode(N, CurDAG->getMachineNode(PPC::ADDItocL, dl, MVT::i64,
                                          SDValue(Tmp, 0), GA));
    return;
  }
  case PPCISD::PPC32_PICGOT:
    // Generate a PIC-safe GOT reference.
    assert(!PPCSubTarget->isPPC64() && PPCSubTarget->isSVR4ABI() &&
      "PPCISD::PPC32_PICGOT is only supported for 32-bit SVR4");
    CurDAG->SelectNodeTo(N, PPC::PPC32PICGOT,
                         PPCLowering->getPointerTy(CurDAG->getDataLayout()),
                         MVT::i32);
    return;

  case PPCISD::VADD_SPLAT: {
    // This expands into one of three sequences, depending on whether
    // the first operand is odd or even, positive or negative.
    assert(isa<ConstantSDNode>(N->getOperand(0)) &&
           isa<ConstantSDNode>(N->getOperand(1)) &&
           "Invalid operand on VADD_SPLAT!");

    int Elt     = N->getConstantOperandVal(0);
    int EltSize = N->getConstantOperandVal(1);
    unsigned Opc1, Opc2, Opc3;
    EVT VT;

    if (EltSize == 1) {
      Opc1 = PPC::VSPLTISB;
      Opc2 = PPC::VADDUBM;
      Opc3 = PPC::VSUBUBM;
      VT = MVT::v16i8;
    } else if (EltSize == 2) {
      Opc1 = PPC::VSPLTISH;
      Opc2 = PPC::VADDUHM;
      Opc3 = PPC::VSUBUHM;
      VT = MVT::v8i16;
    } else {
      assert(EltSize == 4 && "Invalid element size on VADD_SPLAT!");
      Opc1 = PPC::VSPLTISW;
      Opc2 = PPC::VADDUWM;
      Opc3 = PPC::VSUBUWM;
      VT = MVT::v4i32;
    }

    if ((Elt & 1) == 0) {
      // Elt is even, in the range [-32,-18] + [16,30].
      //
      // Convert: VADD_SPLAT elt, size
      // Into:    tmp = VSPLTIS[BHW] elt
      //          VADDU[BHW]M tmp, tmp
      // Where:   [BHW] = B for size = 1, H for size = 2, W for size = 4
      SDValue EltVal = getI32Imm(Elt >> 1, dl);
      SDNode *Tmp = CurDAG->getMachineNode(Opc1, dl, VT, EltVal);
      SDValue TmpVal = SDValue(Tmp, 0);
      ReplaceNode(N, CurDAG->getMachineNode(Opc2, dl, VT, TmpVal, TmpVal));
      return;
    } else if (Elt > 0) {
      // Elt is odd and positive, in the range [17,31].
      //
      // Convert: VADD_SPLAT elt, size
      // Into:    tmp1 = VSPLTIS[BHW] elt-16
      //          tmp2 = VSPLTIS[BHW] -16
      //          VSUBU[BHW]M tmp1, tmp2
      SDValue EltVal = getI32Imm(Elt - 16, dl);
      SDNode *Tmp1 = CurDAG->getMachineNode(Opc1, dl, VT, EltVal);
      EltVal = getI32Imm(-16, dl);
      SDNode *Tmp2 = CurDAG->getMachineNode(Opc1, dl, VT, EltVal);
      ReplaceNode(N, CurDAG->getMachineNode(Opc3, dl, VT, SDValue(Tmp1, 0),
                                            SDValue(Tmp2, 0)));
      return;
    } else {
      // Elt is odd and negative, in the range [-31,-17].
      //
      // Convert: VADD_SPLAT elt, size
      // Into:    tmp1 = VSPLTIS[BHW] elt+16
      //          tmp2 = VSPLTIS[BHW] -16
      //          VADDU[BHW]M tmp1, tmp2
      SDValue EltVal = getI32Imm(Elt + 16, dl);
      SDNode *Tmp1 = CurDAG->getMachineNode(Opc1, dl, VT, EltVal);
      EltVal = getI32Imm(-16, dl);
      SDNode *Tmp2 = CurDAG->getMachineNode(Opc1, dl, VT, EltVal);
      ReplaceNode(N, CurDAG->getMachineNode(Opc2, dl, VT, SDValue(Tmp1, 0),
                                            SDValue(Tmp2, 0)));
      return;
    }
  }
  }

  SelectCode(N);
}

// If the target supports the cmpb instruction, do the idiom recognition here.
// We don't do this as a DAG combine because we don't want to do it as nodes
// are being combined (because we might miss part of the eventual idiom). We
// don't want to do it during instruction selection because we want to reuse
// the logic for lowering the masking operations already part of the
// instruction selector.
SDValue PPCDAGToDAGISel::combineToCMPB(SDNode *N) {
  SDLoc dl(N);

  assert(N->getOpcode() == ISD::OR &&
         "Only OR nodes are supported for CMPB");

  SDValue Res;
  if (!PPCSubTarget->hasCMPB())
    return Res;

  if (N->getValueType(0) != MVT::i32 &&
      N->getValueType(0) != MVT::i64)
    return Res;

  EVT VT = N->getValueType(0);

  SDValue RHS, LHS;
  bool BytesFound[8] = {false, false, false, false, false, false, false, false};
  uint64_t Mask = 0, Alt = 0;

  auto IsByteSelectCC = [this](SDValue O, unsigned &b,
                               uint64_t &Mask, uint64_t &Alt,
                               SDValue &LHS, SDValue &RHS) {
    if (O.getOpcode() != ISD::SELECT_CC)
      return false;
    ISD::CondCode CC = cast<CondCodeSDNode>(O.getOperand(4))->get();

    if (!isa<ConstantSDNode>(O.getOperand(2)) ||
        !isa<ConstantSDNode>(O.getOperand(3)))
      return false;

    uint64_t PM = O.getConstantOperandVal(2);
    uint64_t PAlt = O.getConstantOperandVal(3);
    for (b = 0; b < 8; ++b) {
      uint64_t Mask = UINT64_C(0xFF) << (8*b);
      if (PM && (PM & Mask) == PM && (PAlt & Mask) == PAlt)
        break;
    }

    if (b == 8)
      return false;
    Mask |= PM;
    Alt  |= PAlt;

    if (!isa<ConstantSDNode>(O.getOperand(1)) ||
        O.getConstantOperandVal(1) != 0) {
      SDValue Op0 = O.getOperand(0), Op1 = O.getOperand(1);
      if (Op0.getOpcode() == ISD::TRUNCATE)
        Op0 = Op0.getOperand(0);
      if (Op1.getOpcode() == ISD::TRUNCATE)
        Op1 = Op1.getOperand(0);

      if (Op0.getOpcode() == ISD::SRL && Op1.getOpcode() == ISD::SRL &&
          Op0.getOperand(1) == Op1.getOperand(1) && CC == ISD::SETEQ &&
          isa<ConstantSDNode>(Op0.getOperand(1))) {

        unsigned Bits = Op0.getValueSizeInBits();
        if (b != Bits/8-1)
          return false;
        if (Op0.getConstantOperandVal(1) != Bits-8)
          return false;

        LHS = Op0.getOperand(0);
        RHS = Op1.getOperand(0);
        return true;
      }

      // When we have small integers (i16 to be specific), the form present
      // post-legalization uses SETULT in the SELECT_CC for the
      // higher-order byte, depending on the fact that the
      // even-higher-order bytes are known to all be zero, for example:
      //   select_cc (xor $lhs, $rhs), 256, 65280, 0, setult
      // (so when the second byte is the same, because all higher-order
      // bits from bytes 3 and 4 are known to be zero, the result of the
      // xor can be at most 255)
      if (Op0.getOpcode() == ISD::XOR && CC == ISD::SETULT &&
          isa<ConstantSDNode>(O.getOperand(1))) {

        uint64_t ULim = O.getConstantOperandVal(1);
        if (ULim != (UINT64_C(1) << b*8))
          return false;

        // Now we need to make sure that the upper bytes are known to be
        // zero.
        unsigned Bits = Op0.getValueSizeInBits();
        if (!CurDAG->MaskedValueIsZero(
                Op0, APInt::getHighBitsSet(Bits, Bits - (b + 1) * 8)))
          return false;

        LHS = Op0.getOperand(0);
        RHS = Op0.getOperand(1);
        return true;
      }

      return false;
    }

    if (CC != ISD::SETEQ)
      return false;

    SDValue Op = O.getOperand(0);
    if (Op.getOpcode() == ISD::AND) {
      if (!isa<ConstantSDNode>(Op.getOperand(1)))
        return false;
      if (Op.getConstantOperandVal(1) != (UINT64_C(0xFF) << (8*b)))
        return false;

      SDValue XOR = Op.getOperand(0);
      if (XOR.getOpcode() == ISD::TRUNCATE)
        XOR = XOR.getOperand(0);
      if (XOR.getOpcode() != ISD::XOR)
        return false;

      LHS = XOR.getOperand(0);
      RHS = XOR.getOperand(1);
      return true;
    } else if (Op.getOpcode() == ISD::SRL) {
      if (!isa<ConstantSDNode>(Op.getOperand(1)))
        return false;
      unsigned Bits = Op.getValueSizeInBits();
      if (b != Bits/8-1)
        return false;
      if (Op.getConstantOperandVal(1) != Bits-8)
        return false;

      SDValue XOR = Op.getOperand(0);
      if (XOR.getOpcode() == ISD::TRUNCATE)
        XOR = XOR.getOperand(0);
      if (XOR.getOpcode() != ISD::XOR)
        return false;

      LHS = XOR.getOperand(0);
      RHS = XOR.getOperand(1);
      return true;
    }

    return false;
  };

  SmallVector<SDValue, 8> Queue(1, SDValue(N, 0));
  while (!Queue.empty()) {
    SDValue V = Queue.pop_back_val();

    for (const SDValue &O : V.getNode()->ops()) {
      unsigned b;
      uint64_t M = 0, A = 0;
      SDValue OLHS, ORHS;
      if (O.getOpcode() == ISD::OR) {
        Queue.push_back(O);
      } else if (IsByteSelectCC(O, b, M, A, OLHS, ORHS)) {
        if (!LHS) {
          LHS = OLHS;
          RHS = ORHS;
          BytesFound[b] = true;
          Mask |= M;
          Alt  |= A;
        } else if ((LHS == ORHS && RHS == OLHS) ||
                   (RHS == ORHS && LHS == OLHS)) {
          BytesFound[b] = true;
          Mask |= M;
          Alt  |= A;
        } else {
          return Res;
        }
      } else {
        return Res;
      }
    }
  }

  unsigned LastB = 0, BCnt = 0;
  for (unsigned i = 0; i < 8; ++i)
    if (BytesFound[LastB]) {
      ++BCnt;
      LastB = i;
    }

  if (!LastB || BCnt < 2)
    return Res;

  // Because we'll be zero-extending the output anyway if don't have a specific
  // value for each input byte (via the Mask), we can 'anyext' the inputs.
  if (LHS.getValueType() != VT) {
    LHS = CurDAG->getAnyExtOrTrunc(LHS, dl, VT);
    RHS = CurDAG->getAnyExtOrTrunc(RHS, dl, VT);
  }

  Res = CurDAG->getNode(PPCISD::CMPB, dl, VT, LHS, RHS);

  bool NonTrivialMask = ((int64_t) Mask) != INT64_C(-1);
  if (NonTrivialMask && !Alt) {
    // Res = Mask & CMPB
    Res = CurDAG->getNode(ISD::AND, dl, VT, Res,
                          CurDAG->getConstant(Mask, dl, VT));
  } else if (Alt) {
    // Res = (CMPB & Mask) | (~CMPB & Alt)
    // Which, as suggested here:
    //   https://graphics.stanford.edu/~seander/bithacks.html#MaskedMerge
    // can be written as:
    // Res = Alt ^ ((Alt ^ Mask) & CMPB)
    // useful because the (Alt ^ Mask) can be pre-computed.
    Res = CurDAG->getNode(ISD::AND, dl, VT, Res,
                          CurDAG->getConstant(Mask ^ Alt, dl, VT));
    Res = CurDAG->getNode(ISD::XOR, dl, VT, Res,
                          CurDAG->getConstant(Alt, dl, VT));
  }

  return Res;
}

// When CR bit registers are enabled, an extension of an i1 variable to a i32
// or i64 value is lowered in terms of a SELECT_I[48] operation, and thus
// involves constant materialization of a 0 or a 1 or both. If the result of
// the extension is then operated upon by some operator that can be constant
// folded with a constant 0 or 1, and that constant can be materialized using
// only one instruction (like a zero or one), then we should fold in those
// operations with the select.
void PPCDAGToDAGISel::foldBoolExts(SDValue &Res, SDNode *&N) {
  if (!PPCSubTarget->useCRBits())
    return;

  if (N->getOpcode() != ISD::ZERO_EXTEND &&
      N->getOpcode() != ISD::SIGN_EXTEND &&
      N->getOpcode() != ISD::ANY_EXTEND)
    return;

  if (N->getOperand(0).getValueType() != MVT::i1)
    return;

  if (!N->hasOneUse())
    return;

  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Cond = N->getOperand(0);
  SDValue ConstTrue =
    CurDAG->getConstant(N->getOpcode() == ISD::SIGN_EXTEND ? -1 : 1, dl, VT);
  SDValue ConstFalse = CurDAG->getConstant(0, dl, VT);

  do {
    SDNode *User = *N->use_begin();
    if (User->getNumOperands() != 2)
      break;

    auto TryFold = [this, N, User, dl](SDValue Val) {
      SDValue UserO0 = User->getOperand(0), UserO1 = User->getOperand(1);
      SDValue O0 = UserO0.getNode() == N ? Val : UserO0;
      SDValue O1 = UserO1.getNode() == N ? Val : UserO1;

      return CurDAG->FoldConstantArithmetic(User->getOpcode(), dl,
                                            User->getValueType(0),
                                            O0.getNode(), O1.getNode());
    };

    // FIXME: When the semantics of the interaction between select and undef
    // are clearly defined, it may turn out to be unnecessary to break here.
    SDValue TrueRes = TryFold(ConstTrue);
    if (!TrueRes || TrueRes.isUndef())
      break;
    SDValue FalseRes = TryFold(ConstFalse);
    if (!FalseRes || FalseRes.isUndef())
      break;

    // For us to materialize these using one instruction, we must be able to
    // represent them as signed 16-bit integers.
    uint64_t True  = cast<ConstantSDNode>(TrueRes)->getZExtValue(),
             False = cast<ConstantSDNode>(FalseRes)->getZExtValue();
    if (!isInt<16>(True) || !isInt<16>(False))
      break;

    // We can replace User with a new SELECT node, and try again to see if we
    // can fold the select with its user.
    Res = CurDAG->getSelect(dl, User->getValueType(0), Cond, TrueRes, FalseRes);
    N = User;
    ConstTrue = TrueRes;
    ConstFalse = FalseRes;
  } while (N->hasOneUse());
}

void PPCDAGToDAGISel::PreprocessISelDAG() {
  SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_end();

  bool MadeChange = false;
  while (Position != CurDAG->allnodes_begin()) {
    SDNode *N = &*--Position;
    if (N->use_empty())
      continue;

    SDValue Res;
    switch (N->getOpcode()) {
    default: break;
    case ISD::OR:
      Res = combineToCMPB(N);
      break;
    }

    if (!Res)
      foldBoolExts(Res, N);

    if (Res) {
      LLVM_DEBUG(dbgs() << "PPC DAG preprocessing replacing:\nOld:    ");
      LLVM_DEBUG(N->dump(CurDAG));
      LLVM_DEBUG(dbgs() << "\nNew: ");
      LLVM_DEBUG(Res.getNode()->dump(CurDAG));
      LLVM_DEBUG(dbgs() << "\n");

      CurDAG->ReplaceAllUsesOfValueWith(SDValue(N, 0), Res);
      MadeChange = true;
    }
  }

  if (MadeChange)
    CurDAG->RemoveDeadNodes();
}

/// PostprocessISelDAG - Perform some late peephole optimizations
/// on the DAG representation.
void PPCDAGToDAGISel::PostprocessISelDAG() {
  // Skip peepholes at -O0.
  if (TM.getOptLevel() == CodeGenOpt::None)
    return;

  PeepholePPC64();
  PeepholeCROps();
  PeepholePPC64ZExt();
}

// Check if all users of this node will become isel where the second operand
// is the constant zero. If this is so, and if we can negate the condition,
// then we can flip the true and false operands. This will allow the zero to
// be folded with the isel so that we don't need to materialize a register
// containing zero.
bool PPCDAGToDAGISel::AllUsersSelectZero(SDNode *N) {
  for (SDNode::use_iterator UI = N->use_begin(), UE = N->use_end();
       UI != UE; ++UI) {
    SDNode *User = *UI;
    if (!User->isMachineOpcode())
      return false;
    if (User->getMachineOpcode() != PPC::SELECT_I4 &&
        User->getMachineOpcode() != PPC::SELECT_I8)
      return false;

    SDNode *Op2 = User->getOperand(2).getNode();
    if (!Op2->isMachineOpcode())
      return false;

    if (Op2->getMachineOpcode() != PPC::LI &&
        Op2->getMachineOpcode() != PPC::LI8)
      return false;

    ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op2->getOperand(0));
    if (!C)
      return false;

    if (!C->isNullValue())
      return false;
  }

  return true;
}

void PPCDAGToDAGISel::SwapAllSelectUsers(SDNode *N) {
  SmallVector<SDNode *, 4> ToReplace;
  for (SDNode::use_iterator UI = N->use_begin(), UE = N->use_end();
       UI != UE; ++UI) {
    SDNode *User = *UI;
    assert((User->getMachineOpcode() == PPC::SELECT_I4 ||
            User->getMachineOpcode() == PPC::SELECT_I8) &&
           "Must have all select users");
    ToReplace.push_back(User);
  }

  for (SmallVector<SDNode *, 4>::iterator UI = ToReplace.begin(),
       UE = ToReplace.end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    SDNode *ResNode =
      CurDAG->getMachineNode(User->getMachineOpcode(), SDLoc(User),
                             User->getValueType(0), User->getOperand(0),
                             User->getOperand(2),
                             User->getOperand(1));

    LLVM_DEBUG(dbgs() << "CR Peephole replacing:\nOld:    ");
    LLVM_DEBUG(User->dump(CurDAG));
    LLVM_DEBUG(dbgs() << "\nNew: ");
    LLVM_DEBUG(ResNode->dump(CurDAG));
    LLVM_DEBUG(dbgs() << "\n");

    ReplaceUses(User, ResNode);
  }
}

void PPCDAGToDAGISel::PeepholeCROps() {
  bool IsModified;
  do {
    IsModified = false;
    for (SDNode &Node : CurDAG->allnodes()) {
      MachineSDNode *MachineNode = dyn_cast<MachineSDNode>(&Node);
      if (!MachineNode || MachineNode->use_empty())
        continue;
      SDNode *ResNode = MachineNode;

      bool Op1Set   = false, Op1Unset = false,
           Op1Not   = false,
           Op2Set   = false, Op2Unset = false,
           Op2Not   = false;

      unsigned Opcode = MachineNode->getMachineOpcode();
      switch (Opcode) {
      default: break;
      case PPC::CRAND:
      case PPC::CRNAND:
      case PPC::CROR:
      case PPC::CRXOR:
      case PPC::CRNOR:
      case PPC::CREQV:
      case PPC::CRANDC:
      case PPC::CRORC: {
        SDValue Op = MachineNode->getOperand(1);
        if (Op.isMachineOpcode()) {
          if (Op.getMachineOpcode() == PPC::CRSET)
            Op2Set = true;
          else if (Op.getMachineOpcode() == PPC::CRUNSET)
            Op2Unset = true;
          else if (Op.getMachineOpcode() == PPC::CRNOR &&
                   Op.getOperand(0) == Op.getOperand(1))
            Op2Not = true;
        }
        LLVM_FALLTHROUGH;
      }
      case PPC::BC:
      case PPC::BCn:
      case PPC::SELECT_I4:
      case PPC::SELECT_I8:
      case PPC::SELECT_F4:
      case PPC::SELECT_F8:
      case PPC::SELECT_QFRC:
      case PPC::SELECT_QSRC:
      case PPC::SELECT_QBRC:
      case PPC::SELECT_SPE:
      case PPC::SELECT_SPE4:
      case PPC::SELECT_VRRC:
      case PPC::SELECT_VSFRC:
      case PPC::SELECT_VSSRC:
      case PPC::SELECT_VSRC: {
        SDValue Op = MachineNode->getOperand(0);
        if (Op.isMachineOpcode()) {
          if (Op.getMachineOpcode() == PPC::CRSET)
            Op1Set = true;
          else if (Op.getMachineOpcode() == PPC::CRUNSET)
            Op1Unset = true;
          else if (Op.getMachineOpcode() == PPC::CRNOR &&
                   Op.getOperand(0) == Op.getOperand(1))
            Op1Not = true;
        }
        }
        break;
      }

      bool SelectSwap = false;
      switch (Opcode) {
      default: break;
      case PPC::CRAND:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // x & x = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Set)
          // 1 & y = y
          ResNode = MachineNode->getOperand(1).getNode();
        else if (Op2Set)
          // x & 1 = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Unset || Op2Unset)
          // x & 0 = 0 & y = 0
          ResNode = CurDAG->getMachineNode(PPC::CRUNSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Not)
          // ~x & y = andc(y, x)
          ResNode = CurDAG->getMachineNode(PPC::CRANDC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(0).
                                             getOperand(0));
        else if (Op2Not)
          // x & ~y = andc(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRANDC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1).
                                             getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CRNAND, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1));
          SelectSwap = true;
        }
        break;
      case PPC::CRNAND:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // nand(x, x) -> nor(x, x)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(0));
        else if (Op1Set)
          // nand(1, y) -> nor(y, y)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(1));
        else if (Op2Set)
          // nand(x, 1) -> nor(x, x)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(0));
        else if (Op1Unset || Op2Unset)
          // nand(x, 0) = nand(0, y) = 1
          ResNode = CurDAG->getMachineNode(PPC::CRSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Not)
          // nand(~x, y) = ~(~x & y) = x | ~y = orc(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRORC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0).
                                                      getOperand(0),
                                           MachineNode->getOperand(1));
        else if (Op2Not)
          // nand(x, ~y) = ~x | y = orc(y, x)
          ResNode = CurDAG->getMachineNode(PPC::CRORC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1).
                                                      getOperand(0),
                                           MachineNode->getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CRAND, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1));
          SelectSwap = true;
        }
        break;
      case PPC::CROR:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // x | x = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Set || Op2Set)
          // x | 1 = 1 | y = 1
          ResNode = CurDAG->getMachineNode(PPC::CRSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Unset)
          // 0 | y = y
          ResNode = MachineNode->getOperand(1).getNode();
        else if (Op2Unset)
          // x | 0 = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Not)
          // ~x | y = orc(y, x)
          ResNode = CurDAG->getMachineNode(PPC::CRORC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(0).
                                             getOperand(0));
        else if (Op2Not)
          // x | ~y = orc(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRORC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1).
                                             getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1));
          SelectSwap = true;
        }
        break;
      case PPC::CRXOR:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // xor(x, x) = 0
          ResNode = CurDAG->getMachineNode(PPC::CRUNSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Set)
          // xor(1, y) -> nor(y, y)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(1));
        else if (Op2Set)
          // xor(x, 1) -> nor(x, x)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(0));
        else if (Op1Unset)
          // xor(0, y) = y
          ResNode = MachineNode->getOperand(1).getNode();
        else if (Op2Unset)
          // xor(x, 0) = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Not)
          // xor(~x, y) = eqv(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CREQV, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0).
                                                      getOperand(0),
                                           MachineNode->getOperand(1));
        else if (Op2Not)
          // xor(x, ~y) = eqv(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CREQV, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1).
                                             getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CREQV, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1));
          SelectSwap = true;
        }
        break;
      case PPC::CRNOR:
        if (Op1Set || Op2Set)
          // nor(1, y) -> 0
          ResNode = CurDAG->getMachineNode(PPC::CRUNSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Unset)
          // nor(0, y) = ~y -> nor(y, y)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(1));
        else if (Op2Unset)
          // nor(x, 0) = ~x
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(0));
        else if (Op1Not)
          // nor(~x, y) = andc(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRANDC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0).
                                                      getOperand(0),
                                           MachineNode->getOperand(1));
        else if (Op2Not)
          // nor(x, ~y) = andc(y, x)
          ResNode = CurDAG->getMachineNode(PPC::CRANDC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1).
                                                      getOperand(0),
                                           MachineNode->getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CROR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1));
          SelectSwap = true;
        }
        break;
      case PPC::CREQV:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // eqv(x, x) = 1
          ResNode = CurDAG->getMachineNode(PPC::CRSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Set)
          // eqv(1, y) = y
          ResNode = MachineNode->getOperand(1).getNode();
        else if (Op2Set)
          // eqv(x, 1) = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Unset)
          // eqv(0, y) = ~y -> nor(y, y)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(1));
        else if (Op2Unset)
          // eqv(x, 0) = ~x
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(0));
        else if (Op1Not)
          // eqv(~x, y) = xor(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRXOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0).
                                                      getOperand(0),
                                           MachineNode->getOperand(1));
        else if (Op2Not)
          // eqv(x, ~y) = xor(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRXOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1).
                                             getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CRXOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1));
          SelectSwap = true;
        }
        break;
      case PPC::CRANDC:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // andc(x, x) = 0
          ResNode = CurDAG->getMachineNode(PPC::CRUNSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Set)
          // andc(1, y) = ~y
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(1));
        else if (Op1Unset || Op2Set)
          // andc(0, y) = andc(x, 1) = 0
          ResNode = CurDAG->getMachineNode(PPC::CRUNSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op2Unset)
          // andc(x, 0) = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Not)
          // andc(~x, y) = ~(x | y) = nor(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0).
                                                      getOperand(0),
                                           MachineNode->getOperand(1));
        else if (Op2Not)
          // andc(x, ~y) = x & y
          ResNode = CurDAG->getMachineNode(PPC::CRAND, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1).
                                             getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CRORC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(0));
          SelectSwap = true;
        }
        break;
      case PPC::CRORC:
        if (MachineNode->getOperand(0) == MachineNode->getOperand(1))
          // orc(x, x) = 1
          ResNode = CurDAG->getMachineNode(PPC::CRSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op1Set || Op2Unset)
          // orc(1, y) = orc(x, 0) = 1
          ResNode = CurDAG->getMachineNode(PPC::CRSET, SDLoc(MachineNode),
                                           MVT::i1);
        else if (Op2Set)
          // orc(x, 1) = x
          ResNode = MachineNode->getOperand(0).getNode();
        else if (Op1Unset)
          // orc(0, y) = ~y
          ResNode = CurDAG->getMachineNode(PPC::CRNOR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(1));
        else if (Op1Not)
          // orc(~x, y) = ~(x & y) = nand(x, y)
          ResNode = CurDAG->getMachineNode(PPC::CRNAND, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0).
                                                      getOperand(0),
                                           MachineNode->getOperand(1));
        else if (Op2Not)
          // orc(x, ~y) = x | y
          ResNode = CurDAG->getMachineNode(PPC::CROR, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(0),
                                           MachineNode->getOperand(1).
                                             getOperand(0));
        else if (AllUsersSelectZero(MachineNode)) {
          ResNode = CurDAG->getMachineNode(PPC::CRANDC, SDLoc(MachineNode),
                                           MVT::i1, MachineNode->getOperand(1),
                                           MachineNode->getOperand(0));
          SelectSwap = true;
        }
        break;
      case PPC::SELECT_I4:
      case PPC::SELECT_I8:
      case PPC::SELECT_F4:
      case PPC::SELECT_F8:
      case PPC::SELECT_QFRC:
      case PPC::SELECT_QSRC:
      case PPC::SELECT_QBRC:
      case PPC::SELECT_SPE:
      case PPC::SELECT_SPE4:
      case PPC::SELECT_VRRC:
      case PPC::SELECT_VSFRC:
      case PPC::SELECT_VSSRC:
      case PPC::SELECT_VSRC:
        if (Op1Set)
          ResNode = MachineNode->getOperand(1).getNode();
        else if (Op1Unset)
          ResNode = MachineNode->getOperand(2).getNode();
        else if (Op1Not)
          ResNode = CurDAG->getMachineNode(MachineNode->getMachineOpcode(),
                                           SDLoc(MachineNode),
                                           MachineNode->getValueType(0),
                                           MachineNode->getOperand(0).
                                             getOperand(0),
                                           MachineNode->getOperand(2),
                                           MachineNode->getOperand(1));
        break;
      case PPC::BC:
      case PPC::BCn:
        if (Op1Not)
          ResNode = CurDAG->getMachineNode(Opcode == PPC::BC ? PPC::BCn :
                                                               PPC::BC,
                                           SDLoc(MachineNode),
                                           MVT::Other,
                                           MachineNode->getOperand(0).
                                             getOperand(0),
                                           MachineNode->getOperand(1),
                                           MachineNode->getOperand(2));
        // FIXME: Handle Op1Set, Op1Unset here too.
        break;
      }

      // If we're inverting this node because it is used only by selects that
      // we'd like to swap, then swap the selects before the node replacement.
      if (SelectSwap)
        SwapAllSelectUsers(MachineNode);

      if (ResNode != MachineNode) {
        LLVM_DEBUG(dbgs() << "CR Peephole replacing:\nOld:    ");
        LLVM_DEBUG(MachineNode->dump(CurDAG));
        LLVM_DEBUG(dbgs() << "\nNew: ");
        LLVM_DEBUG(ResNode->dump(CurDAG));
        LLVM_DEBUG(dbgs() << "\n");

        ReplaceUses(MachineNode, ResNode);
        IsModified = true;
      }
    }
    if (IsModified)
      CurDAG->RemoveDeadNodes();
  } while (IsModified);
}

// Gather the set of 32-bit operations that are known to have their
// higher-order 32 bits zero, where ToPromote contains all such operations.
static bool PeepholePPC64ZExtGather(SDValue Op32,
                                    SmallPtrSetImpl<SDNode *> &ToPromote) {
  if (!Op32.isMachineOpcode())
    return false;

  // First, check for the "frontier" instructions (those that will clear the
  // higher-order 32 bits.

  // For RLWINM and RLWNM, we need to make sure that the mask does not wrap
  // around. If it does not, then these instructions will clear the
  // higher-order bits.
  if ((Op32.getMachineOpcode() == PPC::RLWINM ||
       Op32.getMachineOpcode() == PPC::RLWNM) &&
      Op32.getConstantOperandVal(2) <= Op32.getConstantOperandVal(3)) {
    ToPromote.insert(Op32.getNode());
    return true;
  }

  // SLW and SRW always clear the higher-order bits.
  if (Op32.getMachineOpcode() == PPC::SLW ||
      Op32.getMachineOpcode() == PPC::SRW) {
    ToPromote.insert(Op32.getNode());
    return true;
  }

  // For LI and LIS, we need the immediate to be positive (so that it is not
  // sign extended).
  if (Op32.getMachineOpcode() == PPC::LI ||
      Op32.getMachineOpcode() == PPC::LIS) {
    if (!isUInt<15>(Op32.getConstantOperandVal(0)))
      return false;

    ToPromote.insert(Op32.getNode());
    return true;
  }

  // LHBRX and LWBRX always clear the higher-order bits.
  if (Op32.getMachineOpcode() == PPC::LHBRX ||
      Op32.getMachineOpcode() == PPC::LWBRX) {
    ToPromote.insert(Op32.getNode());
    return true;
  }

  // CNT[LT]ZW always produce a 64-bit value in [0,32], and so is zero extended.
  if (Op32.getMachineOpcode() == PPC::CNTLZW ||
      Op32.getMachineOpcode() == PPC::CNTTZW) {
    ToPromote.insert(Op32.getNode());
    return true;
  }

  // Next, check for those instructions we can look through.

  // Assuming the mask does not wrap around, then the higher-order bits are
  // taken directly from the first operand.
  if (Op32.getMachineOpcode() == PPC::RLWIMI &&
      Op32.getConstantOperandVal(3) <= Op32.getConstantOperandVal(4)) {
    SmallPtrSet<SDNode *, 16> ToPromote1;
    if (!PeepholePPC64ZExtGather(Op32.getOperand(0), ToPromote1))
      return false;

    ToPromote.insert(Op32.getNode());
    ToPromote.insert(ToPromote1.begin(), ToPromote1.end());
    return true;
  }

  // For OR, the higher-order bits are zero if that is true for both operands.
  // For SELECT_I4, the same is true (but the relevant operand numbers are
  // shifted by 1).
  if (Op32.getMachineOpcode() == PPC::OR ||
      Op32.getMachineOpcode() == PPC::SELECT_I4) {
    unsigned B = Op32.getMachineOpcode() == PPC::SELECT_I4 ? 1 : 0;
    SmallPtrSet<SDNode *, 16> ToPromote1;
    if (!PeepholePPC64ZExtGather(Op32.getOperand(B+0), ToPromote1))
      return false;
    if (!PeepholePPC64ZExtGather(Op32.getOperand(B+1), ToPromote1))
      return false;

    ToPromote.insert(Op32.getNode());
    ToPromote.insert(ToPromote1.begin(), ToPromote1.end());
    return true;
  }

  // For ORI and ORIS, we need the higher-order bits of the first operand to be
  // zero, and also for the constant to be positive (so that it is not sign
  // extended).
  if (Op32.getMachineOpcode() == PPC::ORI ||
      Op32.getMachineOpcode() == PPC::ORIS) {
    SmallPtrSet<SDNode *, 16> ToPromote1;
    if (!PeepholePPC64ZExtGather(Op32.getOperand(0), ToPromote1))
      return false;
    if (!isUInt<15>(Op32.getConstantOperandVal(1)))
      return false;

    ToPromote.insert(Op32.getNode());
    ToPromote.insert(ToPromote1.begin(), ToPromote1.end());
    return true;
  }

  // The higher-order bits of AND are zero if that is true for at least one of
  // the operands.
  if (Op32.getMachineOpcode() == PPC::AND) {
    SmallPtrSet<SDNode *, 16> ToPromote1, ToPromote2;
    bool Op0OK =
      PeepholePPC64ZExtGather(Op32.getOperand(0), ToPromote1);
    bool Op1OK =
      PeepholePPC64ZExtGather(Op32.getOperand(1), ToPromote2);
    if (!Op0OK && !Op1OK)
      return false;

    ToPromote.insert(Op32.getNode());

    if (Op0OK)
      ToPromote.insert(ToPromote1.begin(), ToPromote1.end());

    if (Op1OK)
      ToPromote.insert(ToPromote2.begin(), ToPromote2.end());

    return true;
  }

  // For ANDI and ANDIS, the higher-order bits are zero if either that is true
  // of the first operand, or if the second operand is positive (so that it is
  // not sign extended).
  if (Op32.getMachineOpcode() == PPC::ANDIo ||
      Op32.getMachineOpcode() == PPC::ANDISo) {
    SmallPtrSet<SDNode *, 16> ToPromote1;
    bool Op0OK =
      PeepholePPC64ZExtGather(Op32.getOperand(0), ToPromote1);
    bool Op1OK = isUInt<15>(Op32.getConstantOperandVal(1));
    if (!Op0OK && !Op1OK)
      return false;

    ToPromote.insert(Op32.getNode());

    if (Op0OK)
      ToPromote.insert(ToPromote1.begin(), ToPromote1.end());

    return true;
  }

  return false;
}

void PPCDAGToDAGISel::PeepholePPC64ZExt() {
  if (!PPCSubTarget->isPPC64())
    return;

  // When we zero-extend from i32 to i64, we use a pattern like this:
  // def : Pat<(i64 (zext i32:$in)),
  //           (RLDICL (INSERT_SUBREG (i64 (IMPLICIT_DEF)), $in, sub_32),
  //                   0, 32)>;
  // There are several 32-bit shift/rotate instructions, however, that will
  // clear the higher-order bits of their output, rendering the RLDICL
  // unnecessary. When that happens, we remove it here, and redefine the
  // relevant 32-bit operation to be a 64-bit operation.

  SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_end();

  bool MadeChange = false;
  while (Position != CurDAG->allnodes_begin()) {
    SDNode *N = &*--Position;
    // Skip dead nodes and any non-machine opcodes.
    if (N->use_empty() || !N->isMachineOpcode())
      continue;

    if (N->getMachineOpcode() != PPC::RLDICL)
      continue;

    if (N->getConstantOperandVal(1) != 0 ||
        N->getConstantOperandVal(2) != 32)
      continue;

    SDValue ISR = N->getOperand(0);
    if (!ISR.isMachineOpcode() ||
        ISR.getMachineOpcode() != TargetOpcode::INSERT_SUBREG)
      continue;

    if (!ISR.hasOneUse())
      continue;

    if (ISR.getConstantOperandVal(2) != PPC::sub_32)
      continue;

    SDValue IDef = ISR.getOperand(0);
    if (!IDef.isMachineOpcode() ||
        IDef.getMachineOpcode() != TargetOpcode::IMPLICIT_DEF)
      continue;

    // We now know that we're looking at a canonical i32 -> i64 zext. See if we
    // can get rid of it.

    SDValue Op32 = ISR->getOperand(1);
    if (!Op32.isMachineOpcode())
      continue;

    // There are some 32-bit instructions that always clear the high-order 32
    // bits, there are also some instructions (like AND) that we can look
    // through.
    SmallPtrSet<SDNode *, 16> ToPromote;
    if (!PeepholePPC64ZExtGather(Op32, ToPromote))
      continue;

    // If the ToPromote set contains nodes that have uses outside of the set
    // (except for the original INSERT_SUBREG), then abort the transformation.
    bool OutsideUse = false;
    for (SDNode *PN : ToPromote) {
      for (SDNode *UN : PN->uses()) {
        if (!ToPromote.count(UN) && UN != ISR.getNode()) {
          OutsideUse = true;
          break;
        }
      }

      if (OutsideUse)
        break;
    }
    if (OutsideUse)
      continue;

    MadeChange = true;

    // We now know that this zero extension can be removed by promoting to
    // nodes in ToPromote to 64-bit operations, where for operations in the
    // frontier of the set, we need to insert INSERT_SUBREGs for their
    // operands.
    for (SDNode *PN : ToPromote) {
      unsigned NewOpcode;
      switch (PN->getMachineOpcode()) {
      default:
        llvm_unreachable("Don't know the 64-bit variant of this instruction");
      case PPC::RLWINM:    NewOpcode = PPC::RLWINM8; break;
      case PPC::RLWNM:     NewOpcode = PPC::RLWNM8; break;
      case PPC::SLW:       NewOpcode = PPC::SLW8; break;
      case PPC::SRW:       NewOpcode = PPC::SRW8; break;
      case PPC::LI:        NewOpcode = PPC::LI8; break;
      case PPC::LIS:       NewOpcode = PPC::LIS8; break;
      case PPC::LHBRX:     NewOpcode = PPC::LHBRX8; break;
      case PPC::LWBRX:     NewOpcode = PPC::LWBRX8; break;
      case PPC::CNTLZW:    NewOpcode = PPC::CNTLZW8; break;
      case PPC::CNTTZW:    NewOpcode = PPC::CNTTZW8; break;
      case PPC::RLWIMI:    NewOpcode = PPC::RLWIMI8; break;
      case PPC::OR:        NewOpcode = PPC::OR8; break;
      case PPC::SELECT_I4: NewOpcode = PPC::SELECT_I8; break;
      case PPC::ORI:       NewOpcode = PPC::ORI8; break;
      case PPC::ORIS:      NewOpcode = PPC::ORIS8; break;
      case PPC::AND:       NewOpcode = PPC::AND8; break;
      case PPC::ANDIo:     NewOpcode = PPC::ANDIo8; break;
      case PPC::ANDISo:    NewOpcode = PPC::ANDISo8; break;
      }

      // Note: During the replacement process, the nodes will be in an
      // inconsistent state (some instructions will have operands with values
      // of the wrong type). Once done, however, everything should be right
      // again.

      SmallVector<SDValue, 4> Ops;
      for (const SDValue &V : PN->ops()) {
        if (!ToPromote.count(V.getNode()) && V.getValueType() == MVT::i32 &&
            !isa<ConstantSDNode>(V)) {
          SDValue ReplOpOps[] = { ISR.getOperand(0), V, ISR.getOperand(2) };
          SDNode *ReplOp =
            CurDAG->getMachineNode(TargetOpcode::INSERT_SUBREG, SDLoc(V),
                                   ISR.getNode()->getVTList(), ReplOpOps);
          Ops.push_back(SDValue(ReplOp, 0));
        } else {
          Ops.push_back(V);
        }
      }

      // Because all to-be-promoted nodes only have users that are other
      // promoted nodes (or the original INSERT_SUBREG), we can safely replace
      // the i32 result value type with i64.

      SmallVector<EVT, 2> NewVTs;
      SDVTList VTs = PN->getVTList();
      for (unsigned i = 0, ie = VTs.NumVTs; i != ie; ++i)
        if (VTs.VTs[i] == MVT::i32)
          NewVTs.push_back(MVT::i64);
        else
          NewVTs.push_back(VTs.VTs[i]);

      LLVM_DEBUG(dbgs() << "PPC64 ZExt Peephole morphing:\nOld:    ");
      LLVM_DEBUG(PN->dump(CurDAG));

      CurDAG->SelectNodeTo(PN, NewOpcode, CurDAG->getVTList(NewVTs), Ops);

      LLVM_DEBUG(dbgs() << "\nNew: ");
      LLVM_DEBUG(PN->dump(CurDAG));
      LLVM_DEBUG(dbgs() << "\n");
    }

    // Now we replace the original zero extend and its associated INSERT_SUBREG
    // with the value feeding the INSERT_SUBREG (which has now been promoted to
    // return an i64).

    LLVM_DEBUG(dbgs() << "PPC64 ZExt Peephole replacing:\nOld:    ");
    LLVM_DEBUG(N->dump(CurDAG));
    LLVM_DEBUG(dbgs() << "\nNew: ");
    LLVM_DEBUG(Op32.getNode()->dump(CurDAG));
    LLVM_DEBUG(dbgs() << "\n");

    ReplaceUses(N, Op32.getNode());
  }

  if (MadeChange)
    CurDAG->RemoveDeadNodes();
}

void PPCDAGToDAGISel::PeepholePPC64() {
  // These optimizations are currently supported only for 64-bit SVR4.
  if (PPCSubTarget->isDarwin() || !PPCSubTarget->isPPC64())
    return;

  SelectionDAG::allnodes_iterator Position = CurDAG->allnodes_end();

  while (Position != CurDAG->allnodes_begin()) {
    SDNode *N = &*--Position;
    // Skip dead nodes and any non-machine opcodes.
    if (N->use_empty() || !N->isMachineOpcode())
      continue;

    unsigned FirstOp;
    unsigned StorageOpcode = N->getMachineOpcode();
    bool RequiresMod4Offset = false;

    switch (StorageOpcode) {
    default: continue;

    case PPC::LWA:
    case PPC::LD:
    case PPC::DFLOADf64:
    case PPC::DFLOADf32:
      RequiresMod4Offset = true;
      LLVM_FALLTHROUGH;
    case PPC::LBZ:
    case PPC::LBZ8:
    case PPC::LFD:
    case PPC::LFS:
    case PPC::LHA:
    case PPC::LHA8:
    case PPC::LHZ:
    case PPC::LHZ8:
    case PPC::LWZ:
    case PPC::LWZ8:
      FirstOp = 0;
      break;

    case PPC::STD:
    case PPC::DFSTOREf64:
    case PPC::DFSTOREf32:
      RequiresMod4Offset = true;
      LLVM_FALLTHROUGH;
    case PPC::STB:
    case PPC::STB8:
    case PPC::STFD:
    case PPC::STFS:
    case PPC::STH:
    case PPC::STH8:
    case PPC::STW:
    case PPC::STW8:
      FirstOp = 1;
      break;
    }

    // If this is a load or store with a zero offset, or within the alignment,
    // we may be able to fold an add-immediate into the memory operation.
    // The check against alignment is below, as it can't occur until we check
    // the arguments to N
    if (!isa<ConstantSDNode>(N->getOperand(FirstOp)))
      continue;

    SDValue Base = N->getOperand(FirstOp + 1);
    if (!Base.isMachineOpcode())
      continue;

    unsigned Flags = 0;
    bool ReplaceFlags = true;

    // When the feeding operation is an add-immediate of some sort,
    // determine whether we need to add relocation information to the
    // target flags on the immediate operand when we fold it into the
    // load instruction.
    //
    // For something like ADDItocL, the relocation information is
    // inferred from the opcode; when we process it in the AsmPrinter,
    // we add the necessary relocation there.  A load, though, can receive
    // relocation from various flavors of ADDIxxx, so we need to carry
    // the relocation information in the target flags.
    switch (Base.getMachineOpcode()) {
    default: continue;

    case PPC::ADDI8:
    case PPC::ADDI:
      // In some cases (such as TLS) the relocation information
      // is already in place on the operand, so copying the operand
      // is sufficient.
      ReplaceFlags = false;
      // For these cases, the immediate may not be divisible by 4, in
      // which case the fold is illegal for DS-form instructions.  (The
      // other cases provide aligned addresses and are always safe.)
      if (RequiresMod4Offset &&
          (!isa<ConstantSDNode>(Base.getOperand(1)) ||
           Base.getConstantOperandVal(1) % 4 != 0))
        continue;
      break;
    case PPC::ADDIdtprelL:
      Flags = PPCII::MO_DTPREL_LO;
      break;
    case PPC::ADDItlsldL:
      Flags = PPCII::MO_TLSLD_LO;
      break;
    case PPC::ADDItocL:
      Flags = PPCII::MO_TOC_LO;
      break;
    }

    SDValue ImmOpnd = Base.getOperand(1);

    // On PPC64, the TOC base pointer is guaranteed by the ABI only to have
    // 8-byte alignment, and so we can only use offsets less than 8 (otherwise,
    // we might have needed different @ha relocation values for the offset
    // pointers).
    int MaxDisplacement = 7;
    if (GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(ImmOpnd)) {
      const GlobalValue *GV = GA->getGlobal();
      MaxDisplacement = std::min((int) GV->getAlignment() - 1, MaxDisplacement);
    }

    bool UpdateHBase = false;
    SDValue HBase = Base.getOperand(0);

    int Offset = N->getConstantOperandVal(FirstOp);
    if (ReplaceFlags) {
      if (Offset < 0 || Offset > MaxDisplacement) {
        // If we have a addi(toc@l)/addis(toc@ha) pair, and the addis has only
        // one use, then we can do this for any offset, we just need to also
        // update the offset (i.e. the symbol addend) on the addis also.
        if (Base.getMachineOpcode() != PPC::ADDItocL)
          continue;

        if (!HBase.isMachineOpcode() ||
            HBase.getMachineOpcode() != PPC::ADDIStocHA)
          continue;

        if (!Base.hasOneUse() || !HBase.hasOneUse())
          continue;

        SDValue HImmOpnd = HBase.getOperand(1);
        if (HImmOpnd != ImmOpnd)
          continue;

        UpdateHBase = true;
      }
    } else {
      // If we're directly folding the addend from an addi instruction, then:
      //  1. In general, the offset on the memory access must be zero.
      //  2. If the addend is a constant, then it can be combined with a
      //     non-zero offset, but only if the result meets the encoding
      //     requirements.
      if (auto *C = dyn_cast<ConstantSDNode>(ImmOpnd)) {
        Offset += C->getSExtValue();

        if (RequiresMod4Offset && (Offset % 4) != 0)
          continue;

        if (!isInt<16>(Offset))
          continue;

        ImmOpnd = CurDAG->getTargetConstant(Offset, SDLoc(ImmOpnd),
                                            ImmOpnd.getValueType());
      } else if (Offset != 0) {
        continue;
      }
    }

    // We found an opportunity.  Reverse the operands from the add
    // immediate and substitute them into the load or store.  If
    // needed, update the target flags for the immediate operand to
    // reflect the necessary relocation information.
    LLVM_DEBUG(dbgs() << "Folding add-immediate into mem-op:\nBase:    ");
    LLVM_DEBUG(Base->dump(CurDAG));
    LLVM_DEBUG(dbgs() << "\nN: ");
    LLVM_DEBUG(N->dump(CurDAG));
    LLVM_DEBUG(dbgs() << "\n");

    // If the relocation information isn't already present on the
    // immediate operand, add it now.
    if (ReplaceFlags) {
      if (GlobalAddressSDNode *GA = dyn_cast<GlobalAddressSDNode>(ImmOpnd)) {
        SDLoc dl(GA);
        const GlobalValue *GV = GA->getGlobal();
        // We can't perform this optimization for data whose alignment
        // is insufficient for the instruction encoding.
        if (GV->getAlignment() < 4 &&
            (RequiresMod4Offset || (Offset % 4) != 0)) {
          LLVM_DEBUG(dbgs() << "Rejected this candidate for alignment.\n\n");
          continue;
        }
        ImmOpnd = CurDAG->getTargetGlobalAddress(GV, dl, MVT::i64, Offset, Flags);
      } else if (ConstantPoolSDNode *CP =
                 dyn_cast<ConstantPoolSDNode>(ImmOpnd)) {
        const Constant *C = CP->getConstVal();
        ImmOpnd = CurDAG->getTargetConstantPool(C, MVT::i64,
                                                CP->getAlignment(),
                                                Offset, Flags);
      }
    }

    if (FirstOp == 1) // Store
      (void)CurDAG->UpdateNodeOperands(N, N->getOperand(0), ImmOpnd,
                                       Base.getOperand(0), N->getOperand(3));
    else // Load
      (void)CurDAG->UpdateNodeOperands(N, ImmOpnd, Base.getOperand(0),
                                       N->getOperand(2));

    if (UpdateHBase)
      (void)CurDAG->UpdateNodeOperands(HBase.getNode(), HBase.getOperand(0),
                                       ImmOpnd);

    // The add-immediate may now be dead, in which case remove it.
    if (Base.getNode()->use_empty())
      CurDAG->RemoveDeadNode(Base.getNode());
  }
}

/// createPPCISelDag - This pass converts a legalized DAG into a
/// PowerPC-specific DAG, ready for instruction scheduling.
///
FunctionPass *llvm::createPPCISelDag(PPCTargetMachine &TM,
                                     CodeGenOpt::Level OptLevel) {
  return new PPCDAGToDAGISel(TM, OptLevel);
}
