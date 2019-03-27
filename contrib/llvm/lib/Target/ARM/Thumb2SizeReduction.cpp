//===-- Thumb2SizeReduction.cpp - Thumb2 code size reduction pass -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Thumb2InstrInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "t2-reduce-size"
#define THUMB2_SIZE_REDUCE_NAME "Thumb2 instruction size reduce pass"

STATISTIC(NumNarrows,  "Number of 32-bit instrs reduced to 16-bit ones");
STATISTIC(Num2Addrs,   "Number of 32-bit instrs reduced to 2addr 16-bit ones");
STATISTIC(NumLdSts,    "Number of 32-bit load / store reduced to 16-bit ones");

static cl::opt<int> ReduceLimit("t2-reduce-limit",
                                cl::init(-1), cl::Hidden);
static cl::opt<int> ReduceLimit2Addr("t2-reduce-limit2",
                                     cl::init(-1), cl::Hidden);
static cl::opt<int> ReduceLimitLdSt("t2-reduce-limit3",
                                     cl::init(-1), cl::Hidden);

namespace {

  /// ReduceTable - A static table with information on mapping from wide
  /// opcodes to narrow
  struct ReduceEntry {
    uint16_t WideOpc;      // Wide opcode
    uint16_t NarrowOpc1;   // Narrow opcode to transform to
    uint16_t NarrowOpc2;   // Narrow opcode when it's two-address
    uint8_t  Imm1Limit;    // Limit of immediate field (bits)
    uint8_t  Imm2Limit;    // Limit of immediate field when it's two-address
    unsigned LowRegs1 : 1; // Only possible if low-registers are used
    unsigned LowRegs2 : 1; // Only possible if low-registers are used (2addr)
    unsigned PredCC1  : 2; // 0 - If predicated, cc is on and vice versa.
                           // 1 - No cc field.
                           // 2 - Always set CPSR.
    unsigned PredCC2  : 2;
    unsigned PartFlag : 1; // 16-bit instruction does partial flag update
    unsigned Special  : 1; // Needs to be dealt with specially
    unsigned AvoidMovs: 1; // Avoid movs with shifter operand (for Swift)
  };

  static const ReduceEntry ReduceTable[] = {
  // Wide,        Narrow1,      Narrow2,     imm1,imm2, lo1, lo2, P/C,PF,S,AM
  { ARM::t2ADCrr, 0,            ARM::tADC,     0,   0,   0,   1,  0,0, 0,0,0 },
  { ARM::t2ADDri, ARM::tADDi3,  ARM::tADDi8,   3,   8,   1,   1,  0,0, 0,1,0 },
  { ARM::t2ADDrr, ARM::tADDrr,  ARM::tADDhirr, 0,   0,   1,   0,  0,1, 0,0,0 },
  { ARM::t2ADDSri,ARM::tADDi3,  ARM::tADDi8,   3,   8,   1,   1,  2,2, 0,1,0 },
  { ARM::t2ADDSrr,ARM::tADDrr,  0,             0,   0,   1,   0,  2,0, 0,1,0 },
  { ARM::t2ANDrr, 0,            ARM::tAND,     0,   0,   0,   1,  0,0, 1,0,0 },
  { ARM::t2ASRri, ARM::tASRri,  0,             5,   0,   1,   0,  0,0, 1,0,1 },
  { ARM::t2ASRrr, 0,            ARM::tASRrr,   0,   0,   0,   1,  0,0, 1,0,1 },
  { ARM::t2BICrr, 0,            ARM::tBIC,     0,   0,   0,   1,  0,0, 1,0,0 },
  //FIXME: Disable CMN, as CCodes are backwards from compare expectations
  //{ ARM::t2CMNrr, ARM::tCMN,  0,             0,   0,   1,   0,  2,0, 0,0,0 },
  { ARM::t2CMNzrr, ARM::tCMNz,  0,             0,   0,   1,   0,  2,0, 0,0,0 },
  { ARM::t2CMPri, ARM::tCMPi8,  0,             8,   0,   1,   0,  2,0, 0,0,0 },
  { ARM::t2CMPrr, ARM::tCMPhir, 0,             0,   0,   0,   0,  2,0, 0,1,0 },
  { ARM::t2EORrr, 0,            ARM::tEOR,     0,   0,   0,   1,  0,0, 1,0,0 },
  // FIXME: adr.n immediate offset must be multiple of 4.
  //{ ARM::t2LEApcrelJT,ARM::tLEApcrelJT, 0,   0,   0,   1,   0,  1,0, 0,0,0 },
  { ARM::t2LSLri, ARM::tLSLri,  0,             5,   0,   1,   0,  0,0, 1,0,1 },
  { ARM::t2LSLrr, 0,            ARM::tLSLrr,   0,   0,   0,   1,  0,0, 1,0,1 },
  { ARM::t2LSRri, ARM::tLSRri,  0,             5,   0,   1,   0,  0,0, 1,0,1 },
  { ARM::t2LSRrr, 0,            ARM::tLSRrr,   0,   0,   0,   1,  0,0, 1,0,1 },
  { ARM::t2MOVi,  ARM::tMOVi8,  0,             8,   0,   1,   0,  0,0, 1,0,0 },
  { ARM::t2MOVi16,ARM::tMOVi8,  0,             8,   0,   1,   0,  0,0, 1,1,0 },
  // FIXME: Do we need the 16-bit 'S' variant?
  { ARM::t2MOVr,ARM::tMOVr,     0,             0,   0,   0,   0,  1,0, 0,0,0 },
  { ARM::t2MUL,   0,            ARM::tMUL,     0,   0,   0,   1,  0,0, 1,0,0 },
  { ARM::t2MVNr,  ARM::tMVN,    0,             0,   0,   1,   0,  0,0, 0,0,0 },
  { ARM::t2ORRrr, 0,            ARM::tORR,     0,   0,   0,   1,  0,0, 1,0,0 },
  { ARM::t2REV,   ARM::tREV,    0,             0,   0,   1,   0,  1,0, 0,0,0 },
  { ARM::t2REV16, ARM::tREV16,  0,             0,   0,   1,   0,  1,0, 0,0,0 },
  { ARM::t2REVSH, ARM::tREVSH,  0,             0,   0,   1,   0,  1,0, 0,0,0 },
  { ARM::t2RORrr, 0,            ARM::tROR,     0,   0,   0,   1,  0,0, 1,0,0 },
  { ARM::t2RSBri, ARM::tRSB,    0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2RSBSri,ARM::tRSB,    0,             0,   0,   1,   0,  2,0, 0,1,0 },
  { ARM::t2SBCrr, 0,            ARM::tSBC,     0,   0,   0,   1,  0,0, 0,0,0 },
  { ARM::t2SUBri, ARM::tSUBi3,  ARM::tSUBi8,   3,   8,   1,   1,  0,0, 0,0,0 },
  { ARM::t2SUBrr, ARM::tSUBrr,  0,             0,   0,   1,   0,  0,0, 0,0,0 },
  { ARM::t2SUBSri,ARM::tSUBi3,  ARM::tSUBi8,   3,   8,   1,   1,  2,2, 0,0,0 },
  { ARM::t2SUBSrr,ARM::tSUBrr,  0,             0,   0,   1,   0,  2,0, 0,0,0 },
  { ARM::t2SXTB,  ARM::tSXTB,   0,             0,   0,   1,   0,  1,0, 0,1,0 },
  { ARM::t2SXTH,  ARM::tSXTH,   0,             0,   0,   1,   0,  1,0, 0,1,0 },
  { ARM::t2TEQrr, ARM::tEOR,    0,             0,   0,   1,   0,  2,0, 0,1,0 },
  { ARM::t2TSTrr, ARM::tTST,    0,             0,   0,   1,   0,  2,0, 0,0,0 },
  { ARM::t2UXTB,  ARM::tUXTB,   0,             0,   0,   1,   0,  1,0, 0,1,0 },
  { ARM::t2UXTH,  ARM::tUXTH,   0,             0,   0,   1,   0,  1,0, 0,1,0 },

  // FIXME: Clean this up after splitting each Thumb load / store opcode
  // into multiple ones.
  { ARM::t2LDRi12,ARM::tLDRi,   ARM::tLDRspi,  5,   8,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRs,  ARM::tLDRr,   0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRBi12,ARM::tLDRBi, 0,             5,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRBs, ARM::tLDRBr,  0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRHi12,ARM::tLDRHi, 0,             5,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRHs, ARM::tLDRHr,  0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRSBs,ARM::tLDRSB,  0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDRSHs,ARM::tLDRSH,  0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2LDR_POST,ARM::tLDMIA_UPD,0,         0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STRi12,ARM::tSTRi,   ARM::tSTRspi,  5,   8,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STRs,  ARM::tSTRr,   0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STRBi12,ARM::tSTRBi, 0,             5,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STRBs, ARM::tSTRBr,  0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STRHi12,ARM::tSTRHi, 0,             5,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STRHs, ARM::tSTRHr,  0,             0,   0,   1,   0,  0,0, 0,1,0 },
  { ARM::t2STR_POST,ARM::tSTMIA_UPD,0,         0,   0,   1,   0,  0,0, 0,1,0 },

  { ARM::t2LDMIA, ARM::tLDMIA,  0,             0,   0,   1,   1,  1,1, 0,1,0 },
  { ARM::t2LDMIA_RET,0,         ARM::tPOP_RET, 0,   0,   1,   1,  1,1, 0,1,0 },
  { ARM::t2LDMIA_UPD,ARM::tLDMIA_UPD,ARM::tPOP,0,   0,   1,   1,  1,1, 0,1,0 },
  // ARM::t2STMIA (with no basereg writeback) has no Thumb1 equivalent.
  // tSTMIA_UPD is a change in semantics which can only be used if the base
  // register is killed. This difference is correctly handled elsewhere.
  { ARM::t2STMIA, ARM::tSTMIA_UPD, 0,          0,   0,   1,   1,  1,1, 0,1,0 },
  { ARM::t2STMIA_UPD,ARM::tSTMIA_UPD, 0,       0,   0,   1,   1,  1,1, 0,1,0 },
  { ARM::t2STMDB_UPD, 0,        ARM::tPUSH,    0,   0,   1,   1,  1,1, 0,1,0 }
  };

  class Thumb2SizeReduce : public MachineFunctionPass {
  public:
    static char ID;

    const Thumb2InstrInfo *TII;
    const ARMSubtarget *STI;

    Thumb2SizeReduce(std::function<bool(const Function &)> Ftor = nullptr);

    bool runOnMachineFunction(MachineFunction &MF) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return THUMB2_SIZE_REDUCE_NAME;
    }

  private:
    /// ReduceOpcodeMap - Maps wide opcode to index of entry in ReduceTable.
    DenseMap<unsigned, unsigned> ReduceOpcodeMap;

    bool canAddPseudoFlagDep(MachineInstr *Use, bool IsSelfLoop);

    bool VerifyPredAndCC(MachineInstr *MI, const ReduceEntry &Entry,
                         bool is2Addr, ARMCC::CondCodes Pred,
                         bool LiveCPSR, bool &HasCC, bool &CCDead);

    bool ReduceLoadStore(MachineBasicBlock &MBB, MachineInstr *MI,
                         const ReduceEntry &Entry);

    bool ReduceSpecial(MachineBasicBlock &MBB, MachineInstr *MI,
                       const ReduceEntry &Entry, bool LiveCPSR, bool IsSelfLoop);

    /// ReduceTo2Addr - Reduce a 32-bit instruction to a 16-bit two-address
    /// instruction.
    bool ReduceTo2Addr(MachineBasicBlock &MBB, MachineInstr *MI,
                       const ReduceEntry &Entry, bool LiveCPSR,
                       bool IsSelfLoop);

    /// ReduceToNarrow - Reduce a 32-bit instruction to a 16-bit
    /// non-two-address instruction.
    bool ReduceToNarrow(MachineBasicBlock &MBB, MachineInstr *MI,
                        const ReduceEntry &Entry, bool LiveCPSR,
                        bool IsSelfLoop);

    /// ReduceMI - Attempt to reduce MI, return true on success.
    bool ReduceMI(MachineBasicBlock &MBB, MachineInstr *MI,
                  bool LiveCPSR, bool IsSelfLoop);

    /// ReduceMBB - Reduce width of instructions in the specified basic block.
    bool ReduceMBB(MachineBasicBlock &MBB);

    bool OptimizeSize;
    bool MinimizeSize;

    // Last instruction to define CPSR in the current block.
    MachineInstr *CPSRDef;
    // Was CPSR last defined by a high latency instruction?
    // When CPSRDef is null, this refers to CPSR defs in predecessors.
    bool HighLatencyCPSR;

    struct MBBInfo {
      // The flags leaving this block have high latency.
      bool HighLatencyCPSR = false;
      // Has this block been visited yet?
      bool Visited = false;

      MBBInfo() = default;
    };

    SmallVector<MBBInfo, 8> BlockInfo;

    std::function<bool(const Function &)> PredicateFtor;
  };

  char Thumb2SizeReduce::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(Thumb2SizeReduce, DEBUG_TYPE, THUMB2_SIZE_REDUCE_NAME, false,
                false)

Thumb2SizeReduce::Thumb2SizeReduce(std::function<bool(const Function &)> Ftor)
    : MachineFunctionPass(ID), PredicateFtor(std::move(Ftor)) {
  OptimizeSize = MinimizeSize = false;
  for (unsigned i = 0, e = array_lengthof(ReduceTable); i != e; ++i) {
    unsigned FromOpc = ReduceTable[i].WideOpc;
    if (!ReduceOpcodeMap.insert(std::make_pair(FromOpc, i)).second)
      llvm_unreachable("Duplicated entries?");
  }
}

static bool HasImplicitCPSRDef(const MCInstrDesc &MCID) {
  for (const MCPhysReg *Regs = MCID.getImplicitDefs(); *Regs; ++Regs)
    if (*Regs == ARM::CPSR)
      return true;
  return false;
}

// Check for a likely high-latency flag def.
static bool isHighLatencyCPSR(MachineInstr *Def) {
  switch(Def->getOpcode()) {
  case ARM::FMSTAT:
  case ARM::tMUL:
    return true;
  }
  return false;
}

/// canAddPseudoFlagDep - For A9 (and other out-of-order) implementations,
/// the 's' 16-bit instruction partially update CPSR. Abort the
/// transformation to avoid adding false dependency on last CPSR setting
/// instruction which hurts the ability for out-of-order execution engine
/// to do register renaming magic.
/// This function checks if there is a read-of-write dependency between the
/// last instruction that defines the CPSR and the current instruction. If there
/// is, then there is no harm done since the instruction cannot be retired
/// before the CPSR setting instruction anyway.
/// Note, we are not doing full dependency analysis here for the sake of compile
/// time. We're not looking for cases like:
/// r0 = muls ...
/// r1 = add.w r0, ...
/// ...
///    = mul.w r1
/// In this case it would have been ok to narrow the mul.w to muls since there
/// are indirect RAW dependency between the muls and the mul.w
bool
Thumb2SizeReduce::canAddPseudoFlagDep(MachineInstr *Use, bool FirstInSelfLoop) {
  // Disable the check for -Oz (aka OptimizeForSizeHarder).
  if (MinimizeSize || !STI->avoidCPSRPartialUpdate())
    return false;

  if (!CPSRDef)
    // If this BB loops back to itself, conservatively avoid narrowing the
    // first instruction that does partial flag update.
    return HighLatencyCPSR || FirstInSelfLoop;

  SmallSet<unsigned, 2> Defs;
  for (const MachineOperand &MO : CPSRDef->operands()) {
    if (!MO.isReg() || MO.isUndef() || MO.isUse())
      continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0 || Reg == ARM::CPSR)
      continue;
    Defs.insert(Reg);
  }

  for (const MachineOperand &MO : Use->operands()) {
    if (!MO.isReg() || MO.isUndef() || MO.isDef())
      continue;
    unsigned Reg = MO.getReg();
    if (Defs.count(Reg))
      return false;
  }

  // If the current CPSR has high latency, try to avoid the false dependency.
  if (HighLatencyCPSR)
    return true;

  // tMOVi8 usually doesn't start long dependency chains, and there are a lot
  // of them, so always shrink them when CPSR doesn't have high latency.
  if (Use->getOpcode() == ARM::t2MOVi ||
      Use->getOpcode() == ARM::t2MOVi16)
    return false;

  // No read-after-write dependency. The narrowing will add false dependency.
  return true;
}

bool
Thumb2SizeReduce::VerifyPredAndCC(MachineInstr *MI, const ReduceEntry &Entry,
                                  bool is2Addr, ARMCC::CondCodes Pred,
                                  bool LiveCPSR, bool &HasCC, bool &CCDead) {
  if ((is2Addr  && Entry.PredCC2 == 0) ||
      (!is2Addr && Entry.PredCC1 == 0)) {
    if (Pred == ARMCC::AL) {
      // Not predicated, must set CPSR.
      if (!HasCC) {
        // Original instruction was not setting CPSR, but CPSR is not
        // currently live anyway. It's ok to set it. The CPSR def is
        // dead though.
        if (!LiveCPSR) {
          HasCC = true;
          CCDead = true;
          return true;
        }
        return false;
      }
    } else {
      // Predicated, must not set CPSR.
      if (HasCC)
        return false;
    }
  } else if ((is2Addr  && Entry.PredCC2 == 2) ||
             (!is2Addr && Entry.PredCC1 == 2)) {
    /// Old opcode has an optional def of CPSR.
    if (HasCC)
      return true;
    // If old opcode does not implicitly define CPSR, then it's not ok since
    // these new opcodes' CPSR def is not meant to be thrown away. e.g. CMP.
    if (!HasImplicitCPSRDef(MI->getDesc()))
      return false;
    HasCC = true;
  } else {
    // 16-bit instruction does not set CPSR.
    if (HasCC)
      return false;
  }

  return true;
}

static bool VerifyLowRegs(MachineInstr *MI) {
  unsigned Opc = MI->getOpcode();
  bool isPCOk = (Opc == ARM::t2LDMIA_RET || Opc == ARM::t2LDMIA_UPD);
  bool isLROk = (Opc == ARM::t2STMDB_UPD);
  bool isSPOk = isPCOk || isLROk;
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || MO.isImplicit())
      continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0 || Reg == ARM::CPSR)
      continue;
    if (isPCOk && Reg == ARM::PC)
      continue;
    if (isLROk && Reg == ARM::LR)
      continue;
    if (Reg == ARM::SP) {
      if (isSPOk)
        continue;
      if (i == 1 && (Opc == ARM::t2LDRi12 || Opc == ARM::t2STRi12))
        // Special case for these ldr / str with sp as base register.
        continue;
    }
    if (!isARMLowRegister(Reg))
      return false;
  }
  return true;
}

bool
Thumb2SizeReduce::ReduceLoadStore(MachineBasicBlock &MBB, MachineInstr *MI,
                                  const ReduceEntry &Entry) {
  if (ReduceLimitLdSt != -1 && ((int)NumLdSts >= ReduceLimitLdSt))
    return false;

  unsigned Scale = 1;
  bool HasImmOffset = false;
  bool HasShift = false;
  bool HasOffReg = true;
  bool isLdStMul = false;
  unsigned Opc = Entry.NarrowOpc1;
  unsigned OpNum = 3; // First 'rest' of operands.
  uint8_t  ImmLimit = Entry.Imm1Limit;

  switch (Entry.WideOpc) {
  default:
    llvm_unreachable("Unexpected Thumb2 load / store opcode!");
  case ARM::t2LDRi12:
  case ARM::t2STRi12:
    if (MI->getOperand(1).getReg() == ARM::SP) {
      Opc = Entry.NarrowOpc2;
      ImmLimit = Entry.Imm2Limit;
    }

    Scale = 4;
    HasImmOffset = true;
    HasOffReg = false;
    break;
  case ARM::t2LDRBi12:
  case ARM::t2STRBi12:
    HasImmOffset = true;
    HasOffReg = false;
    break;
  case ARM::t2LDRHi12:
  case ARM::t2STRHi12:
    Scale = 2;
    HasImmOffset = true;
    HasOffReg = false;
    break;
  case ARM::t2LDRs:
  case ARM::t2LDRBs:
  case ARM::t2LDRHs:
  case ARM::t2LDRSBs:
  case ARM::t2LDRSHs:
  case ARM::t2STRs:
  case ARM::t2STRBs:
  case ARM::t2STRHs:
    HasShift = true;
    OpNum = 4;
    break;
  case ARM::t2LDR_POST:
  case ARM::t2STR_POST: {
    if (!MBB.getParent()->getFunction().optForMinSize())
      return false;

    if (!MI->hasOneMemOperand() ||
        (*MI->memoperands_begin())->getAlignment() < 4)
      return false;

    // We're creating a completely different type of load/store - LDM from LDR.
    // For this reason we can't reuse the logic at the end of this function; we
    // have to implement the MI building here.
    bool IsStore = Entry.WideOpc == ARM::t2STR_POST;
    unsigned Rt = MI->getOperand(IsStore ? 1 : 0).getReg();
    unsigned Rn = MI->getOperand(IsStore ? 0 : 1).getReg();
    unsigned Offset = MI->getOperand(3).getImm();
    unsigned PredImm = MI->getOperand(4).getImm();
    unsigned PredReg = MI->getOperand(5).getReg();
    assert(isARMLowRegister(Rt));
    assert(isARMLowRegister(Rn));

    if (Offset != 4)
      return false;

    // Add the 16-bit load / store instruction.
    DebugLoc dl = MI->getDebugLoc();
    auto MIB = BuildMI(MBB, MI, dl, TII->get(Entry.NarrowOpc1))
                   .addReg(Rn, RegState::Define)
                   .addReg(Rn)
                   .addImm(PredImm)
                   .addReg(PredReg)
                   .addReg(Rt, IsStore ? 0 : RegState::Define);

    // Transfer memoperands.
    MIB.setMemRefs(MI->memoperands());

    // Transfer MI flags.
    MIB.setMIFlags(MI->getFlags());

    // Kill the old instruction.
    MI->eraseFromBundle();
    ++NumLdSts;
    return true;
  }
  case ARM::t2LDMIA: {
    unsigned BaseReg = MI->getOperand(0).getReg();
    assert(isARMLowRegister(BaseReg));

    // For the non-writeback version (this one), the base register must be
    // one of the registers being loaded.
    bool isOK = false;
    for (unsigned i = 3; i < MI->getNumOperands(); ++i) {
      if (MI->getOperand(i).getReg() == BaseReg) {
        isOK = true;
        break;
      }
    }

    if (!isOK)
      return false;

    OpNum = 0;
    isLdStMul = true;
    break;
  }
  case ARM::t2STMIA:
    // If the base register is killed, we don't care what its value is after the
    // instruction, so we can use an updating STMIA.
    if (!MI->getOperand(0).isKill())
      return false;

    break;
  case ARM::t2LDMIA_RET: {
    unsigned BaseReg = MI->getOperand(1).getReg();
    if (BaseReg != ARM::SP)
      return false;
    Opc = Entry.NarrowOpc2; // tPOP_RET
    OpNum = 2;
    isLdStMul = true;
    break;
  }
  case ARM::t2LDMIA_UPD:
  case ARM::t2STMIA_UPD:
  case ARM::t2STMDB_UPD: {
    OpNum = 0;

    unsigned BaseReg = MI->getOperand(1).getReg();
    if (BaseReg == ARM::SP &&
        (Entry.WideOpc == ARM::t2LDMIA_UPD ||
         Entry.WideOpc == ARM::t2STMDB_UPD)) {
      Opc = Entry.NarrowOpc2; // tPOP or tPUSH
      OpNum = 2;
    } else if (!isARMLowRegister(BaseReg) ||
               (Entry.WideOpc != ARM::t2LDMIA_UPD &&
                Entry.WideOpc != ARM::t2STMIA_UPD)) {
      return false;
    }

    isLdStMul = true;
    break;
  }
  }

  unsigned OffsetReg = 0;
  bool OffsetKill = false;
  bool OffsetInternal = false;
  if (HasShift) {
    OffsetReg  = MI->getOperand(2).getReg();
    OffsetKill = MI->getOperand(2).isKill();
    OffsetInternal = MI->getOperand(2).isInternalRead();

    if (MI->getOperand(3).getImm())
      // Thumb1 addressing mode doesn't support shift.
      return false;
  }

  unsigned OffsetImm = 0;
  if (HasImmOffset) {
    OffsetImm = MI->getOperand(2).getImm();
    unsigned MaxOffset = ((1 << ImmLimit) - 1) * Scale;

    if ((OffsetImm & (Scale - 1)) || OffsetImm > MaxOffset)
      // Make sure the immediate field fits.
      return false;
  }

  // Add the 16-bit load / store instruction.
  DebugLoc dl = MI->getDebugLoc();
  MachineInstrBuilder MIB = BuildMI(MBB, MI, dl, TII->get(Opc));

  // tSTMIA_UPD takes a defining register operand. We've already checked that
  // the register is killed, so mark it as dead here.
  if (Entry.WideOpc == ARM::t2STMIA)
    MIB.addReg(MI->getOperand(0).getReg(), RegState::Define | RegState::Dead);

  if (!isLdStMul) {
    MIB.add(MI->getOperand(0));
    MIB.add(MI->getOperand(1));

    if (HasImmOffset)
      MIB.addImm(OffsetImm / Scale);

    assert((!HasShift || OffsetReg) && "Invalid so_reg load / store address!");

    if (HasOffReg)
      MIB.addReg(OffsetReg, getKillRegState(OffsetKill) |
                            getInternalReadRegState(OffsetInternal));
  }

  // Transfer the rest of operands.
  for (unsigned e = MI->getNumOperands(); OpNum != e; ++OpNum)
    MIB.add(MI->getOperand(OpNum));

  // Transfer memoperands.
  MIB.setMemRefs(MI->memoperands());

  // Transfer MI flags.
  MIB.setMIFlags(MI->getFlags());

  LLVM_DEBUG(errs() << "Converted 32-bit: " << *MI
                    << "       to 16-bit: " << *MIB);

  MBB.erase_instr(MI);
  ++NumLdSts;
  return true;
}

bool
Thumb2SizeReduce::ReduceSpecial(MachineBasicBlock &MBB, MachineInstr *MI,
                                const ReduceEntry &Entry,
                                bool LiveCPSR, bool IsSelfLoop) {
  unsigned Opc = MI->getOpcode();
  if (Opc == ARM::t2ADDri) {
    // If the source register is SP, try to reduce to tADDrSPi, otherwise
    // it's a normal reduce.
    if (MI->getOperand(1).getReg() != ARM::SP) {
      if (ReduceTo2Addr(MBB, MI, Entry, LiveCPSR, IsSelfLoop))
        return true;
      return ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop);
    }
    // Try to reduce to tADDrSPi.
    unsigned Imm = MI->getOperand(2).getImm();
    // The immediate must be in range, the destination register must be a low
    // reg, the predicate must be "always" and the condition flags must not
    // be being set.
    if (Imm & 3 || Imm > 1020)
      return false;
    if (!isARMLowRegister(MI->getOperand(0).getReg()))
      return false;
    if (MI->getOperand(3).getImm() != ARMCC::AL)
      return false;
    const MCInstrDesc &MCID = MI->getDesc();
    if (MCID.hasOptionalDef() &&
        MI->getOperand(MCID.getNumOperands()-1).getReg() == ARM::CPSR)
      return false;

    MachineInstrBuilder MIB =
        BuildMI(MBB, MI, MI->getDebugLoc(),
                TII->get(ARM::tADDrSPi))
            .add(MI->getOperand(0))
            .add(MI->getOperand(1))
            .addImm(Imm / 4) // The tADDrSPi has an implied scale by four.
            .add(predOps(ARMCC::AL));

    // Transfer MI flags.
    MIB.setMIFlags(MI->getFlags());

    LLVM_DEBUG(errs() << "Converted 32-bit: " << *MI
                      << "       to 16-bit: " << *MIB);

    MBB.erase_instr(MI);
    ++NumNarrows;
    return true;
  }

  if (Entry.LowRegs1 && !VerifyLowRegs(MI))
    return false;

  if (MI->mayLoadOrStore())
    return ReduceLoadStore(MBB, MI, Entry);

  switch (Opc) {
  default: break;
  case ARM::t2ADDSri:
  case ARM::t2ADDSrr: {
    unsigned PredReg = 0;
    if (getInstrPredicate(*MI, PredReg) == ARMCC::AL) {
      switch (Opc) {
      default: break;
      case ARM::t2ADDSri:
        if (ReduceTo2Addr(MBB, MI, Entry, LiveCPSR, IsSelfLoop))
          return true;
        LLVM_FALLTHROUGH;
      case ARM::t2ADDSrr:
        return ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop);
      }
    }
    break;
  }
  case ARM::t2RSBri:
  case ARM::t2RSBSri:
  case ARM::t2SXTB:
  case ARM::t2SXTH:
  case ARM::t2UXTB:
  case ARM::t2UXTH:
    if (MI->getOperand(2).getImm() == 0)
      return ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop);
    break;
  case ARM::t2MOVi16:
    // Can convert only 'pure' immediate operands, not immediates obtained as
    // globals' addresses.
    if (MI->getOperand(1).isImm())
      return ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop);
    break;
  case ARM::t2CMPrr: {
    // Try to reduce to the lo-reg only version first. Why there are two
    // versions of the instruction is a mystery.
    // It would be nice to just have two entries in the master table that
    // are prioritized, but the table assumes a unique entry for each
    // source insn opcode. So for now, we hack a local entry record to use.
    static const ReduceEntry NarrowEntry =
      { ARM::t2CMPrr,ARM::tCMPr, 0, 0, 0, 1, 1,2, 0, 0,1,0 };
    if (ReduceToNarrow(MBB, MI, NarrowEntry, LiveCPSR, IsSelfLoop))
      return true;
    return ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop);
  }
  case ARM::t2TEQrr: {
    unsigned PredReg = 0;
    // Can only convert to eors if we're not in an IT block.
    if (getInstrPredicate(*MI, PredReg) != ARMCC::AL)
      break;
    // TODO if Operand 0 is not killed but Operand 1 is, then we could write
    // to Op1 instead.
    if (MI->getOperand(0).isKill())
      return ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop);
  }
  }
  return false;
}

bool
Thumb2SizeReduce::ReduceTo2Addr(MachineBasicBlock &MBB, MachineInstr *MI,
                                const ReduceEntry &Entry,
                                bool LiveCPSR, bool IsSelfLoop) {
  if (ReduceLimit2Addr != -1 && ((int)Num2Addrs >= ReduceLimit2Addr))
    return false;

  if (!OptimizeSize && Entry.AvoidMovs && STI->avoidMOVsShifterOperand())
    // Don't issue movs with shifter operand for some CPUs unless we
    // are optimizing for size.
    return false;

  unsigned Reg0 = MI->getOperand(0).getReg();
  unsigned Reg1 = MI->getOperand(1).getReg();
  // t2MUL is "special". The tied source operand is second, not first.
  if (MI->getOpcode() == ARM::t2MUL) {
    unsigned Reg2 = MI->getOperand(2).getReg();
    // Early exit if the regs aren't all low regs.
    if (!isARMLowRegister(Reg0) || !isARMLowRegister(Reg1)
        || !isARMLowRegister(Reg2))
      return false;
    if (Reg0 != Reg2) {
      // If the other operand also isn't the same as the destination, we
      // can't reduce.
      if (Reg1 != Reg0)
        return false;
      // Try to commute the operands to make it a 2-address instruction.
      MachineInstr *CommutedMI = TII->commuteInstruction(*MI);
      if (!CommutedMI)
        return false;
    }
  } else if (Reg0 != Reg1) {
    // Try to commute the operands to make it a 2-address instruction.
    unsigned CommOpIdx1 = 1;
    unsigned CommOpIdx2 = TargetInstrInfo::CommuteAnyOperandIndex;
    if (!TII->findCommutedOpIndices(*MI, CommOpIdx1, CommOpIdx2) ||
        MI->getOperand(CommOpIdx2).getReg() != Reg0)
      return false;
    MachineInstr *CommutedMI =
        TII->commuteInstruction(*MI, false, CommOpIdx1, CommOpIdx2);
    if (!CommutedMI)
      return false;
  }
  if (Entry.LowRegs2 && !isARMLowRegister(Reg0))
    return false;
  if (Entry.Imm2Limit) {
    unsigned Imm = MI->getOperand(2).getImm();
    unsigned Limit = (1 << Entry.Imm2Limit) - 1;
    if (Imm > Limit)
      return false;
  } else {
    unsigned Reg2 = MI->getOperand(2).getReg();
    if (Entry.LowRegs2 && !isARMLowRegister(Reg2))
      return false;
  }

  // Check if it's possible / necessary to transfer the predicate.
  const MCInstrDesc &NewMCID = TII->get(Entry.NarrowOpc2);
  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);
  bool SkipPred = false;
  if (Pred != ARMCC::AL) {
    if (!NewMCID.isPredicable())
      // Can't transfer predicate, fail.
      return false;
  } else {
    SkipPred = !NewMCID.isPredicable();
  }

  bool HasCC = false;
  bool CCDead = false;
  const MCInstrDesc &MCID = MI->getDesc();
  if (MCID.hasOptionalDef()) {
    unsigned NumOps = MCID.getNumOperands();
    HasCC = (MI->getOperand(NumOps-1).getReg() == ARM::CPSR);
    if (HasCC && MI->getOperand(NumOps-1).isDead())
      CCDead = true;
  }
  if (!VerifyPredAndCC(MI, Entry, true, Pred, LiveCPSR, HasCC, CCDead))
    return false;

  // Avoid adding a false dependency on partial flag update by some 16-bit
  // instructions which has the 's' bit set.
  if (Entry.PartFlag && NewMCID.hasOptionalDef() && HasCC &&
      canAddPseudoFlagDep(MI, IsSelfLoop))
    return false;

  // Add the 16-bit instruction.
  DebugLoc dl = MI->getDebugLoc();
  MachineInstrBuilder MIB = BuildMI(MBB, MI, dl, NewMCID);
  MIB.add(MI->getOperand(0));
  if (NewMCID.hasOptionalDef())
    MIB.add(HasCC ? t1CondCodeOp(CCDead) : condCodeOp());

  // Transfer the rest of operands.
  unsigned NumOps = MCID.getNumOperands();
  for (unsigned i = 1, e = MI->getNumOperands(); i != e; ++i) {
    if (i < NumOps && MCID.OpInfo[i].isOptionalDef())
      continue;
    if (SkipPred && MCID.OpInfo[i].isPredicate())
      continue;
    MIB.add(MI->getOperand(i));
  }

  // Transfer MI flags.
  MIB.setMIFlags(MI->getFlags());

  LLVM_DEBUG(errs() << "Converted 32-bit: " << *MI
                    << "       to 16-bit: " << *MIB);

  MBB.erase_instr(MI);
  ++Num2Addrs;
  return true;
}

bool
Thumb2SizeReduce::ReduceToNarrow(MachineBasicBlock &MBB, MachineInstr *MI,
                                 const ReduceEntry &Entry,
                                 bool LiveCPSR, bool IsSelfLoop) {
  if (ReduceLimit != -1 && ((int)NumNarrows >= ReduceLimit))
    return false;

  if (!OptimizeSize && Entry.AvoidMovs && STI->avoidMOVsShifterOperand())
    // Don't issue movs with shifter operand for some CPUs unless we
    // are optimizing for size.
    return false;

  unsigned Limit = ~0U;
  if (Entry.Imm1Limit)
    Limit = (1 << Entry.Imm1Limit) - 1;

  const MCInstrDesc &MCID = MI->getDesc();
  for (unsigned i = 0, e = MCID.getNumOperands(); i != e; ++i) {
    if (MCID.OpInfo[i].isPredicate())
      continue;
    const MachineOperand &MO = MI->getOperand(i);
    if (MO.isReg()) {
      unsigned Reg = MO.getReg();
      if (!Reg || Reg == ARM::CPSR)
        continue;
      if (Entry.LowRegs1 && !isARMLowRegister(Reg))
        return false;
    } else if (MO.isImm() &&
               !MCID.OpInfo[i].isPredicate()) {
      if (((unsigned)MO.getImm()) > Limit)
        return false;
    }
  }

  // Check if it's possible / necessary to transfer the predicate.
  const MCInstrDesc &NewMCID = TII->get(Entry.NarrowOpc1);
  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = getInstrPredicate(*MI, PredReg);
  bool SkipPred = false;
  if (Pred != ARMCC::AL) {
    if (!NewMCID.isPredicable())
      // Can't transfer predicate, fail.
      return false;
  } else {
    SkipPred = !NewMCID.isPredicable();
  }

  bool HasCC = false;
  bool CCDead = false;
  if (MCID.hasOptionalDef()) {
    unsigned NumOps = MCID.getNumOperands();
    HasCC = (MI->getOperand(NumOps-1).getReg() == ARM::CPSR);
    if (HasCC && MI->getOperand(NumOps-1).isDead())
      CCDead = true;
  }
  if (!VerifyPredAndCC(MI, Entry, false, Pred, LiveCPSR, HasCC, CCDead))
    return false;

  // Avoid adding a false dependency on partial flag update by some 16-bit
  // instructions which has the 's' bit set.
  if (Entry.PartFlag && NewMCID.hasOptionalDef() && HasCC &&
      canAddPseudoFlagDep(MI, IsSelfLoop))
    return false;

  // Add the 16-bit instruction.
  DebugLoc dl = MI->getDebugLoc();
  MachineInstrBuilder MIB = BuildMI(MBB, MI, dl, NewMCID);

  // TEQ is special in that it doesn't define a register but we're converting
  // it into an EOR which does. So add the first operand as a def and then
  // again as a use.
  if (MCID.getOpcode() == ARM::t2TEQrr) {
    MIB.add(MI->getOperand(0));
    MIB->getOperand(0).setIsKill(false);
    MIB->getOperand(0).setIsDef(true);
    MIB->getOperand(0).setIsDead(true);

    if (NewMCID.hasOptionalDef())
      MIB.add(HasCC ? t1CondCodeOp(CCDead) : condCodeOp());
    MIB.add(MI->getOperand(0));
  } else {
    MIB.add(MI->getOperand(0));
    if (NewMCID.hasOptionalDef())
      MIB.add(HasCC ? t1CondCodeOp(CCDead) : condCodeOp());
  }

  // Transfer the rest of operands.
  unsigned NumOps = MCID.getNumOperands();
  for (unsigned i = 1, e = MI->getNumOperands(); i != e; ++i) {
    if (i < NumOps && MCID.OpInfo[i].isOptionalDef())
      continue;
    if ((MCID.getOpcode() == ARM::t2RSBSri ||
         MCID.getOpcode() == ARM::t2RSBri ||
         MCID.getOpcode() == ARM::t2SXTB ||
         MCID.getOpcode() == ARM::t2SXTH ||
         MCID.getOpcode() == ARM::t2UXTB ||
         MCID.getOpcode() == ARM::t2UXTH) && i == 2)
      // Skip the zero immediate operand, it's now implicit.
      continue;
    bool isPred = (i < NumOps && MCID.OpInfo[i].isPredicate());
    if (SkipPred && isPred)
        continue;
    const MachineOperand &MO = MI->getOperand(i);
    if (MO.isReg() && MO.isImplicit() && MO.getReg() == ARM::CPSR)
      // Skip implicit def of CPSR. Either it's modeled as an optional
      // def now or it's already an implicit def on the new instruction.
      continue;
    MIB.add(MO);
  }
  if (!MCID.isPredicable() && NewMCID.isPredicable())
    MIB.add(predOps(ARMCC::AL));

  // Transfer MI flags.
  MIB.setMIFlags(MI->getFlags());

  LLVM_DEBUG(errs() << "Converted 32-bit: " << *MI
                    << "       to 16-bit: " << *MIB);

  MBB.erase_instr(MI);
  ++NumNarrows;
  return true;
}

static bool UpdateCPSRDef(MachineInstr &MI, bool LiveCPSR, bool &DefCPSR) {
  bool HasDef = false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isUndef() || MO.isUse())
      continue;
    if (MO.getReg() != ARM::CPSR)
      continue;

    DefCPSR = true;
    if (!MO.isDead())
      HasDef = true;
  }

  return HasDef || LiveCPSR;
}

static bool UpdateCPSRUse(MachineInstr &MI, bool LiveCPSR) {
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || MO.isUndef() || MO.isDef())
      continue;
    if (MO.getReg() != ARM::CPSR)
      continue;
    assert(LiveCPSR && "CPSR liveness tracking is wrong!");
    if (MO.isKill()) {
      LiveCPSR = false;
      break;
    }
  }

  return LiveCPSR;
}

bool Thumb2SizeReduce::ReduceMI(MachineBasicBlock &MBB, MachineInstr *MI,
                                bool LiveCPSR, bool IsSelfLoop) {
  unsigned Opcode = MI->getOpcode();
  DenseMap<unsigned, unsigned>::iterator OPI = ReduceOpcodeMap.find(Opcode);
  if (OPI == ReduceOpcodeMap.end())
    return false;
  const ReduceEntry &Entry = ReduceTable[OPI->second];

  // Don't attempt normal reductions on "special" cases for now.
  if (Entry.Special)
    return ReduceSpecial(MBB, MI, Entry, LiveCPSR, IsSelfLoop);

  // Try to transform to a 16-bit two-address instruction.
  if (Entry.NarrowOpc2 &&
      ReduceTo2Addr(MBB, MI, Entry, LiveCPSR, IsSelfLoop))
    return true;

  // Try to transform to a 16-bit non-two-address instruction.
  if (Entry.NarrowOpc1 &&
      ReduceToNarrow(MBB, MI, Entry, LiveCPSR, IsSelfLoop))
    return true;

  return false;
}

bool Thumb2SizeReduce::ReduceMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  // Yes, CPSR could be livein.
  bool LiveCPSR = MBB.isLiveIn(ARM::CPSR);
  MachineInstr *BundleMI = nullptr;

  CPSRDef = nullptr;
  HighLatencyCPSR = false;

  // Check predecessors for the latest CPSRDef.
  for (auto *Pred : MBB.predecessors()) {
    const MBBInfo &PInfo = BlockInfo[Pred->getNumber()];
    if (!PInfo.Visited) {
      // Since blocks are visited in RPO, this must be a back-edge.
      continue;
    }
    if (PInfo.HighLatencyCPSR) {
      HighLatencyCPSR = true;
      break;
    }
  }

  // If this BB loops back to itself, conservatively avoid narrowing the
  // first instruction that does partial flag update.
  bool IsSelfLoop = MBB.isSuccessor(&MBB);
  MachineBasicBlock::instr_iterator MII = MBB.instr_begin(),E = MBB.instr_end();
  MachineBasicBlock::instr_iterator NextMII;
  for (; MII != E; MII = NextMII) {
    NextMII = std::next(MII);

    MachineInstr *MI = &*MII;
    if (MI->isBundle()) {
      BundleMI = MI;
      continue;
    }
    if (MI->isDebugInstr())
      continue;

    LiveCPSR = UpdateCPSRUse(*MI, LiveCPSR);

    // Does NextMII belong to the same bundle as MI?
    bool NextInSameBundle = NextMII != E && NextMII->isBundledWithPred();

    if (ReduceMI(MBB, MI, LiveCPSR, IsSelfLoop)) {
      Modified = true;
      MachineBasicBlock::instr_iterator I = std::prev(NextMII);
      MI = &*I;
      // Removing and reinserting the first instruction in a bundle will break
      // up the bundle. Fix the bundling if it was broken.
      if (NextInSameBundle && !NextMII->isBundledWithPred())
        NextMII->bundleWithPred();
    }

    if (BundleMI && !NextInSameBundle && MI->isInsideBundle()) {
      // FIXME: Since post-ra scheduler operates on bundles, the CPSR kill
      // marker is only on the BUNDLE instruction. Process the BUNDLE
      // instruction as we finish with the bundled instruction to work around
      // the inconsistency.
      if (BundleMI->killsRegister(ARM::CPSR))
        LiveCPSR = false;
      MachineOperand *MO = BundleMI->findRegisterDefOperand(ARM::CPSR);
      if (MO && !MO->isDead())
        LiveCPSR = true;
      MO = BundleMI->findRegisterUseOperand(ARM::CPSR);
      if (MO && !MO->isKill())
        LiveCPSR = true;
    }

    bool DefCPSR = false;
    LiveCPSR = UpdateCPSRDef(*MI, LiveCPSR, DefCPSR);
    if (MI->isCall()) {
      // Calls don't really set CPSR.
      CPSRDef = nullptr;
      HighLatencyCPSR = false;
      IsSelfLoop = false;
    } else if (DefCPSR) {
      // This is the last CPSR defining instruction.
      CPSRDef = MI;
      HighLatencyCPSR = isHighLatencyCPSR(CPSRDef);
      IsSelfLoop = false;
    }
  }

  MBBInfo &Info = BlockInfo[MBB.getNumber()];
  Info.HighLatencyCPSR = HighLatencyCPSR;
  Info.Visited = true;
  return Modified;
}

bool Thumb2SizeReduce::runOnMachineFunction(MachineFunction &MF) {
  if (PredicateFtor && !PredicateFtor(MF.getFunction()))
    return false;

  STI = &static_cast<const ARMSubtarget &>(MF.getSubtarget());
  if (STI->isThumb1Only() || STI->prefers32BitThumb())
    return false;

  TII = static_cast<const Thumb2InstrInfo *>(STI->getInstrInfo());

  // Optimizing / minimizing size? Minimizing size implies optimizing for size.
  OptimizeSize = MF.getFunction().optForSize();
  MinimizeSize = MF.getFunction().optForMinSize();

  BlockInfo.clear();
  BlockInfo.resize(MF.getNumBlockIDs());

  // Visit blocks in reverse post-order so LastCPSRDef is known for all
  // predecessors.
  ReversePostOrderTraversal<MachineFunction*> RPOT(&MF);
  bool Modified = false;
  for (ReversePostOrderTraversal<MachineFunction*>::rpo_iterator
       I = RPOT.begin(), E = RPOT.end(); I != E; ++I)
    Modified |= ReduceMBB(**I);
  return Modified;
}

/// createThumb2SizeReductionPass - Returns an instance of the Thumb2 size
/// reduction pass.
FunctionPass *llvm::createThumb2SizeReductionPass(
    std::function<bool(const Function &)> Ftor) {
  return new Thumb2SizeReduce(std::move(Ftor));
}
