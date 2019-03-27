//===- FastISel.cpp - Implementation of the FastISel class ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the FastISel class.
//
// "Fast" instruction selection is designed to emit very poor code quickly.
// Also, it is not designed to be able to do much lowering, so most illegal
// types (e.g. i64 on 32-bit targets) and operations are not supported.  It is
// also not intended to be able to do much optimization, except in a few cases
// where doing optimizations reduces overall compile time.  For example, folding
// constants into immediate fields is often done, because it's cheap and it
// reduces the number of instructions later phases have to examine.
//
// "Fast" instruction selection is able to fail gracefully and transfer
// control to the SelectionDAG selector for operations that it doesn't
// support.  In many cases, this allows us to avoid duplicating a lot of
// the complicated lowering logic that SelectionDAG currently has.
//
// The intended use for "fast" instruction selection is "-O0" mode
// compilation, where the quality of the generated code is irrelevant when
// weighed against the speed at which the code can be generated.  Also,
// at -O0, the LLVM optimizers are not running, and this makes the
// compile time of codegen a much higher portion of the overall compile
// time.  Despite its limitations, "fast" instruction selection is able to
// handle enough code on its own to provide noticeable overall speedups
// in -O0 compiles.
//
// Basic operations are supported in a target-independent way, by reading
// the same instruction descriptions that the SelectionDAG selector reads,
// and identifying simple arithmetic operations that can be directly selected
// from simple operators.  More complicated operations currently require
// target-specific code.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/FastISel.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "isel"

// FIXME: Remove this after the feature has proven reliable.
static cl::opt<bool> SinkLocalValues("fast-isel-sink-local-values",
                                     cl::init(true), cl::Hidden,
                                     cl::desc("Sink local values in FastISel"));

STATISTIC(NumFastIselSuccessIndependent, "Number of insts selected by "
                                         "target-independent selector");
STATISTIC(NumFastIselSuccessTarget, "Number of insts selected by "
                                    "target-specific selector");
STATISTIC(NumFastIselDead, "Number of dead insts removed on failure");

/// Set the current block to which generated machine instructions will be
/// appended.
void FastISel::startNewBlock() {
  assert(LocalValueMap.empty() &&
         "local values should be cleared after finishing a BB");

  // Instructions are appended to FuncInfo.MBB. If the basic block already
  // contains labels or copies, use the last instruction as the last local
  // value.
  EmitStartPt = nullptr;
  if (!FuncInfo.MBB->empty())
    EmitStartPt = &FuncInfo.MBB->back();
  LastLocalValue = EmitStartPt;
}

/// Flush the local CSE map and sink anything we can.
void FastISel::finishBasicBlock() { flushLocalValueMap(); }

bool FastISel::lowerArguments() {
  if (!FuncInfo.CanLowerReturn)
    // Fallback to SDISel argument lowering code to deal with sret pointer
    // parameter.
    return false;

  if (!fastLowerArguments())
    return false;

  // Enter arguments into ValueMap for uses in non-entry BBs.
  for (Function::const_arg_iterator I = FuncInfo.Fn->arg_begin(),
                                    E = FuncInfo.Fn->arg_end();
       I != E; ++I) {
    DenseMap<const Value *, unsigned>::iterator VI = LocalValueMap.find(&*I);
    assert(VI != LocalValueMap.end() && "Missed an argument?");
    FuncInfo.ValueMap[&*I] = VI->second;
  }
  return true;
}

/// Return the defined register if this instruction defines exactly one
/// virtual register and uses no other virtual registers. Otherwise return 0.
static unsigned findSinkableLocalRegDef(MachineInstr &MI) {
  unsigned RegDef = 0;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (MO.isDef()) {
      if (RegDef)
        return 0;
      RegDef = MO.getReg();
    } else if (TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
      // This is another use of a vreg. Don't try to sink it.
      return 0;
    }
  }
  return RegDef;
}

void FastISel::flushLocalValueMap() {
  // Try to sink local values down to their first use so that we can give them a
  // better debug location. This has the side effect of shrinking local value
  // live ranges, which helps out fast regalloc.
  if (SinkLocalValues && LastLocalValue != EmitStartPt) {
    // Sink local value materialization instructions between EmitStartPt and
    // LastLocalValue. Visit them bottom-up, starting from LastLocalValue, to
    // avoid inserting into the range that we're iterating over.
    MachineBasicBlock::reverse_iterator RE =
        EmitStartPt ? MachineBasicBlock::reverse_iterator(EmitStartPt)
                    : FuncInfo.MBB->rend();
    MachineBasicBlock::reverse_iterator RI(LastLocalValue);

    InstOrderMap OrderMap;
    for (; RI != RE;) {
      MachineInstr &LocalMI = *RI;
      ++RI;
      bool Store = true;
      if (!LocalMI.isSafeToMove(nullptr, Store))
        continue;
      unsigned DefReg = findSinkableLocalRegDef(LocalMI);
      if (DefReg == 0)
        continue;

      sinkLocalValueMaterialization(LocalMI, DefReg, OrderMap);
    }
  }

  LocalValueMap.clear();
  LastLocalValue = EmitStartPt;
  recomputeInsertPt();
  SavedInsertPt = FuncInfo.InsertPt;
  LastFlushPoint = FuncInfo.InsertPt;
}

static bool isRegUsedByPhiNodes(unsigned DefReg,
                                FunctionLoweringInfo &FuncInfo) {
  for (auto &P : FuncInfo.PHINodesToUpdate)
    if (P.second == DefReg)
      return true;
  return false;
}

/// Build a map of instruction orders. Return the first terminator and its
/// order. Consider EH_LABEL instructions to be terminators as well, since local
/// values for phis after invokes must be materialized before the call.
void FastISel::InstOrderMap::initialize(
    MachineBasicBlock *MBB, MachineBasicBlock::iterator LastFlushPoint) {
  unsigned Order = 0;
  for (MachineInstr &I : *MBB) {
    if (!FirstTerminator &&
        (I.isTerminator() || (I.isEHLabel() && &I != &MBB->front()))) {
      FirstTerminator = &I;
      FirstTerminatorOrder = Order;
    }
    Orders[&I] = Order++;

    // We don't need to order instructions past the last flush point.
    if (I.getIterator() == LastFlushPoint)
      break;
  }
}

void FastISel::sinkLocalValueMaterialization(MachineInstr &LocalMI,
                                             unsigned DefReg,
                                             InstOrderMap &OrderMap) {
  // If this register is used by a register fixup, MRI will not contain all
  // the uses until after register fixups, so don't attempt to sink or DCE
  // this instruction. Register fixups typically come from no-op cast
  // instructions, which replace the cast instruction vreg with the local
  // value vreg.
  if (FuncInfo.RegsWithFixups.count(DefReg))
    return;

  // We can DCE this instruction if there are no uses and it wasn't a
  // materialized for a successor PHI node.
  bool UsedByPHI = isRegUsedByPhiNodes(DefReg, FuncInfo);
  if (!UsedByPHI && MRI.use_nodbg_empty(DefReg)) {
    if (EmitStartPt == &LocalMI)
      EmitStartPt = EmitStartPt->getPrevNode();
    LLVM_DEBUG(dbgs() << "removing dead local value materialization "
                      << LocalMI);
    OrderMap.Orders.erase(&LocalMI);
    LocalMI.eraseFromParent();
    return;
  }

  // Number the instructions if we haven't yet so we can efficiently find the
  // earliest use.
  if (OrderMap.Orders.empty())
    OrderMap.initialize(FuncInfo.MBB, LastFlushPoint);

  // Find the first user in the BB.
  MachineInstr *FirstUser = nullptr;
  unsigned FirstOrder = std::numeric_limits<unsigned>::max();
  for (MachineInstr &UseInst : MRI.use_nodbg_instructions(DefReg)) {
    auto I = OrderMap.Orders.find(&UseInst);
    assert(I != OrderMap.Orders.end() &&
           "local value used by instruction outside local region");
    unsigned UseOrder = I->second;
    if (UseOrder < FirstOrder) {
      FirstOrder = UseOrder;
      FirstUser = &UseInst;
    }
  }

  // The insertion point will be the first terminator or the first user,
  // whichever came first. If there was no terminator, this must be a
  // fallthrough block and the insertion point is the end of the block.
  MachineBasicBlock::instr_iterator SinkPos;
  if (UsedByPHI && OrderMap.FirstTerminatorOrder < FirstOrder) {
    FirstOrder = OrderMap.FirstTerminatorOrder;
    SinkPos = OrderMap.FirstTerminator->getIterator();
  } else if (FirstUser) {
    SinkPos = FirstUser->getIterator();
  } else {
    assert(UsedByPHI && "must be users if not used by a phi");
    SinkPos = FuncInfo.MBB->instr_end();
  }

  // Collect all DBG_VALUEs before the new insertion position so that we can
  // sink them.
  SmallVector<MachineInstr *, 1> DbgValues;
  for (MachineInstr &DbgVal : MRI.use_instructions(DefReg)) {
    if (!DbgVal.isDebugValue())
      continue;
    unsigned UseOrder = OrderMap.Orders[&DbgVal];
    if (UseOrder < FirstOrder)
      DbgValues.push_back(&DbgVal);
  }

  // Sink LocalMI before SinkPos and assign it the same DebugLoc.
  LLVM_DEBUG(dbgs() << "sinking local value to first use " << LocalMI);
  FuncInfo.MBB->remove(&LocalMI);
  FuncInfo.MBB->insert(SinkPos, &LocalMI);
  if (SinkPos != FuncInfo.MBB->end())
    LocalMI.setDebugLoc(SinkPos->getDebugLoc());

  // Sink any debug values that we've collected.
  for (MachineInstr *DI : DbgValues) {
    FuncInfo.MBB->remove(DI);
    FuncInfo.MBB->insert(SinkPos, DI);
  }
}

bool FastISel::hasTrivialKill(const Value *V) {
  // Don't consider constants or arguments to have trivial kills.
  const Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  // No-op casts are trivially coalesced by fast-isel.
  if (const auto *Cast = dyn_cast<CastInst>(I))
    if (Cast->isNoopCast(DL) && !hasTrivialKill(Cast->getOperand(0)))
      return false;

  // Even the value might have only one use in the LLVM IR, it is possible that
  // FastISel might fold the use into another instruction and now there is more
  // than one use at the Machine Instruction level.
  unsigned Reg = lookUpRegForValue(V);
  if (Reg && !MRI.use_empty(Reg))
    return false;

  // GEPs with all zero indices are trivially coalesced by fast-isel.
  if (const auto *GEP = dyn_cast<GetElementPtrInst>(I))
    if (GEP->hasAllZeroIndices() && !hasTrivialKill(GEP->getOperand(0)))
      return false;

  // Only instructions with a single use in the same basic block are considered
  // to have trivial kills.
  return I->hasOneUse() &&
         !(I->getOpcode() == Instruction::BitCast ||
           I->getOpcode() == Instruction::PtrToInt ||
           I->getOpcode() == Instruction::IntToPtr) &&
         cast<Instruction>(*I->user_begin())->getParent() == I->getParent();
}

unsigned FastISel::getRegForValue(const Value *V) {
  EVT RealVT = TLI.getValueType(DL, V->getType(), /*AllowUnknown=*/true);
  // Don't handle non-simple values in FastISel.
  if (!RealVT.isSimple())
    return 0;

  // Ignore illegal types. We must do this before looking up the value
  // in ValueMap because Arguments are given virtual registers regardless
  // of whether FastISel can handle them.
  MVT VT = RealVT.getSimpleVT();
  if (!TLI.isTypeLegal(VT)) {
    // Handle integer promotions, though, because they're common and easy.
    if (VT == MVT::i1 || VT == MVT::i8 || VT == MVT::i16)
      VT = TLI.getTypeToTransformTo(V->getContext(), VT).getSimpleVT();
    else
      return 0;
  }

  // Look up the value to see if we already have a register for it.
  unsigned Reg = lookUpRegForValue(V);
  if (Reg)
    return Reg;

  // In bottom-up mode, just create the virtual register which will be used
  // to hold the value. It will be materialized later.
  if (isa<Instruction>(V) &&
      (!isa<AllocaInst>(V) ||
       !FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(V))))
    return FuncInfo.InitializeRegForValue(V);

  SavePoint SaveInsertPt = enterLocalValueArea();

  // Materialize the value in a register. Emit any instructions in the
  // local value area.
  Reg = materializeRegForValue(V, VT);

  leaveLocalValueArea(SaveInsertPt);

  return Reg;
}

unsigned FastISel::materializeConstant(const Value *V, MVT VT) {
  unsigned Reg = 0;
  if (const auto *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getValue().getActiveBits() <= 64)
      Reg = fastEmit_i(VT, VT, ISD::Constant, CI->getZExtValue());
  } else if (isa<AllocaInst>(V))
    Reg = fastMaterializeAlloca(cast<AllocaInst>(V));
  else if (isa<ConstantPointerNull>(V))
    // Translate this as an integer zero so that it can be
    // local-CSE'd with actual integer zeros.
    Reg = getRegForValue(
        Constant::getNullValue(DL.getIntPtrType(V->getContext())));
  else if (const auto *CF = dyn_cast<ConstantFP>(V)) {
    if (CF->isNullValue())
      Reg = fastMaterializeFloatZero(CF);
    else
      // Try to emit the constant directly.
      Reg = fastEmit_f(VT, VT, ISD::ConstantFP, CF);

    if (!Reg) {
      // Try to emit the constant by using an integer constant with a cast.
      const APFloat &Flt = CF->getValueAPF();
      EVT IntVT = TLI.getPointerTy(DL);
      uint32_t IntBitWidth = IntVT.getSizeInBits();
      APSInt SIntVal(IntBitWidth, /*isUnsigned=*/false);
      bool isExact;
      (void)Flt.convertToInteger(SIntVal, APFloat::rmTowardZero, &isExact);
      if (isExact) {
        unsigned IntegerReg =
            getRegForValue(ConstantInt::get(V->getContext(), SIntVal));
        if (IntegerReg != 0)
          Reg = fastEmit_r(IntVT.getSimpleVT(), VT, ISD::SINT_TO_FP, IntegerReg,
                           /*Kill=*/false);
      }
    }
  } else if (const auto *Op = dyn_cast<Operator>(V)) {
    if (!selectOperator(Op, Op->getOpcode()))
      if (!isa<Instruction>(Op) ||
          !fastSelectInstruction(cast<Instruction>(Op)))
        return 0;
    Reg = lookUpRegForValue(Op);
  } else if (isa<UndefValue>(V)) {
    Reg = createResultReg(TLI.getRegClassFor(VT));
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::IMPLICIT_DEF), Reg);
  }
  return Reg;
}

/// Helper for getRegForValue. This function is called when the value isn't
/// already available in a register and must be materialized with new
/// instructions.
unsigned FastISel::materializeRegForValue(const Value *V, MVT VT) {
  unsigned Reg = 0;
  // Give the target-specific code a try first.
  if (isa<Constant>(V))
    Reg = fastMaterializeConstant(cast<Constant>(V));

  // If target-specific code couldn't or didn't want to handle the value, then
  // give target-independent code a try.
  if (!Reg)
    Reg = materializeConstant(V, VT);

  // Don't cache constant materializations in the general ValueMap.
  // To do so would require tracking what uses they dominate.
  if (Reg) {
    LocalValueMap[V] = Reg;
    LastLocalValue = MRI.getVRegDef(Reg);
  }
  return Reg;
}

unsigned FastISel::lookUpRegForValue(const Value *V) {
  // Look up the value to see if we already have a register for it. We
  // cache values defined by Instructions across blocks, and other values
  // only locally. This is because Instructions already have the SSA
  // def-dominates-use requirement enforced.
  DenseMap<const Value *, unsigned>::iterator I = FuncInfo.ValueMap.find(V);
  if (I != FuncInfo.ValueMap.end())
    return I->second;
  return LocalValueMap[V];
}

void FastISel::updateValueMap(const Value *I, unsigned Reg, unsigned NumRegs) {
  if (!isa<Instruction>(I)) {
    LocalValueMap[I] = Reg;
    return;
  }

  unsigned &AssignedReg = FuncInfo.ValueMap[I];
  if (AssignedReg == 0)
    // Use the new register.
    AssignedReg = Reg;
  else if (Reg != AssignedReg) {
    // Arrange for uses of AssignedReg to be replaced by uses of Reg.
    for (unsigned i = 0; i < NumRegs; i++) {
      FuncInfo.RegFixups[AssignedReg + i] = Reg + i;
      FuncInfo.RegsWithFixups.insert(Reg + i);
    }

    AssignedReg = Reg;
  }
}

std::pair<unsigned, bool> FastISel::getRegForGEPIndex(const Value *Idx) {
  unsigned IdxN = getRegForValue(Idx);
  if (IdxN == 0)
    // Unhandled operand. Halt "fast" selection and bail.
    return std::pair<unsigned, bool>(0, false);

  bool IdxNIsKill = hasTrivialKill(Idx);

  // If the index is smaller or larger than intptr_t, truncate or extend it.
  MVT PtrVT = TLI.getPointerTy(DL);
  EVT IdxVT = EVT::getEVT(Idx->getType(), /*HandleUnknown=*/false);
  if (IdxVT.bitsLT(PtrVT)) {
    IdxN = fastEmit_r(IdxVT.getSimpleVT(), PtrVT, ISD::SIGN_EXTEND, IdxN,
                      IdxNIsKill);
    IdxNIsKill = true;
  } else if (IdxVT.bitsGT(PtrVT)) {
    IdxN =
        fastEmit_r(IdxVT.getSimpleVT(), PtrVT, ISD::TRUNCATE, IdxN, IdxNIsKill);
    IdxNIsKill = true;
  }
  return std::pair<unsigned, bool>(IdxN, IdxNIsKill);
}

void FastISel::recomputeInsertPt() {
  if (getLastLocalValue()) {
    FuncInfo.InsertPt = getLastLocalValue();
    FuncInfo.MBB = FuncInfo.InsertPt->getParent();
    ++FuncInfo.InsertPt;
  } else
    FuncInfo.InsertPt = FuncInfo.MBB->getFirstNonPHI();

  // Now skip past any EH_LABELs, which must remain at the beginning.
  while (FuncInfo.InsertPt != FuncInfo.MBB->end() &&
         FuncInfo.InsertPt->getOpcode() == TargetOpcode::EH_LABEL)
    ++FuncInfo.InsertPt;
}

void FastISel::removeDeadCode(MachineBasicBlock::iterator I,
                              MachineBasicBlock::iterator E) {
  assert(I.isValid() && E.isValid() && std::distance(I, E) > 0 &&
         "Invalid iterator!");
  while (I != E) {
    if (LastFlushPoint == I)
      LastFlushPoint = E;
    if (SavedInsertPt == I)
      SavedInsertPt = E;
    if (EmitStartPt == I)
      EmitStartPt = E.isValid() ? &*E : nullptr;
    if (LastLocalValue == I)
      LastLocalValue = E.isValid() ? &*E : nullptr;

    MachineInstr *Dead = &*I;
    ++I;
    Dead->eraseFromParent();
    ++NumFastIselDead;
  }
  recomputeInsertPt();
}

FastISel::SavePoint FastISel::enterLocalValueArea() {
  MachineBasicBlock::iterator OldInsertPt = FuncInfo.InsertPt;
  DebugLoc OldDL = DbgLoc;
  recomputeInsertPt();
  DbgLoc = DebugLoc();
  SavePoint SP = {OldInsertPt, OldDL};
  return SP;
}

void FastISel::leaveLocalValueArea(SavePoint OldInsertPt) {
  if (FuncInfo.InsertPt != FuncInfo.MBB->begin())
    LastLocalValue = &*std::prev(FuncInfo.InsertPt);

  // Restore the previous insert position.
  FuncInfo.InsertPt = OldInsertPt.InsertPt;
  DbgLoc = OldInsertPt.DL;
}

bool FastISel::selectBinaryOp(const User *I, unsigned ISDOpcode) {
  EVT VT = EVT::getEVT(I->getType(), /*HandleUnknown=*/true);
  if (VT == MVT::Other || !VT.isSimple())
    // Unhandled type. Halt "fast" selection and bail.
    return false;

  // We only handle legal types. For example, on x86-32 the instruction
  // selector contains all of the 64-bit instructions from x86-64,
  // under the assumption that i64 won't be used if the target doesn't
  // support it.
  if (!TLI.isTypeLegal(VT)) {
    // MVT::i1 is special. Allow AND, OR, or XOR because they
    // don't require additional zeroing, which makes them easy.
    if (VT == MVT::i1 && (ISDOpcode == ISD::AND || ISDOpcode == ISD::OR ||
                          ISDOpcode == ISD::XOR))
      VT = TLI.getTypeToTransformTo(I->getContext(), VT);
    else
      return false;
  }

  // Check if the first operand is a constant, and handle it as "ri".  At -O0,
  // we don't have anything that canonicalizes operand order.
  if (const auto *CI = dyn_cast<ConstantInt>(I->getOperand(0)))
    if (isa<Instruction>(I) && cast<Instruction>(I)->isCommutative()) {
      unsigned Op1 = getRegForValue(I->getOperand(1));
      if (!Op1)
        return false;
      bool Op1IsKill = hasTrivialKill(I->getOperand(1));

      unsigned ResultReg =
          fastEmit_ri_(VT.getSimpleVT(), ISDOpcode, Op1, Op1IsKill,
                       CI->getZExtValue(), VT.getSimpleVT());
      if (!ResultReg)
        return false;

      // We successfully emitted code for the given LLVM Instruction.
      updateValueMap(I, ResultReg);
      return true;
    }

  unsigned Op0 = getRegForValue(I->getOperand(0));
  if (!Op0) // Unhandled operand. Halt "fast" selection and bail.
    return false;
  bool Op0IsKill = hasTrivialKill(I->getOperand(0));

  // Check if the second operand is a constant and handle it appropriately.
  if (const auto *CI = dyn_cast<ConstantInt>(I->getOperand(1))) {
    uint64_t Imm = CI->getSExtValue();

    // Transform "sdiv exact X, 8" -> "sra X, 3".
    if (ISDOpcode == ISD::SDIV && isa<BinaryOperator>(I) &&
        cast<BinaryOperator>(I)->isExact() && isPowerOf2_64(Imm)) {
      Imm = Log2_64(Imm);
      ISDOpcode = ISD::SRA;
    }

    // Transform "urem x, pow2" -> "and x, pow2-1".
    if (ISDOpcode == ISD::UREM && isa<BinaryOperator>(I) &&
        isPowerOf2_64(Imm)) {
      --Imm;
      ISDOpcode = ISD::AND;
    }

    unsigned ResultReg = fastEmit_ri_(VT.getSimpleVT(), ISDOpcode, Op0,
                                      Op0IsKill, Imm, VT.getSimpleVT());
    if (!ResultReg)
      return false;

    // We successfully emitted code for the given LLVM Instruction.
    updateValueMap(I, ResultReg);
    return true;
  }

  unsigned Op1 = getRegForValue(I->getOperand(1));
  if (!Op1) // Unhandled operand. Halt "fast" selection and bail.
    return false;
  bool Op1IsKill = hasTrivialKill(I->getOperand(1));

  // Now we have both operands in registers. Emit the instruction.
  unsigned ResultReg = fastEmit_rr(VT.getSimpleVT(), VT.getSimpleVT(),
                                   ISDOpcode, Op0, Op0IsKill, Op1, Op1IsKill);
  if (!ResultReg)
    // Target-specific code wasn't able to find a machine opcode for
    // the given ISD opcode and type. Halt "fast" selection and bail.
    return false;

  // We successfully emitted code for the given LLVM Instruction.
  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectGetElementPtr(const User *I) {
  unsigned N = getRegForValue(I->getOperand(0));
  if (!N) // Unhandled operand. Halt "fast" selection and bail.
    return false;
  bool NIsKill = hasTrivialKill(I->getOperand(0));

  // Keep a running tab of the total offset to coalesce multiple N = N + Offset
  // into a single N = N + TotalOffset.
  uint64_t TotalOffs = 0;
  // FIXME: What's a good SWAG number for MaxOffs?
  uint64_t MaxOffs = 2048;
  MVT VT = TLI.getPointerTy(DL);
  for (gep_type_iterator GTI = gep_type_begin(I), E = gep_type_end(I);
       GTI != E; ++GTI) {
    const Value *Idx = GTI.getOperand();
    if (StructType *StTy = GTI.getStructTypeOrNull()) {
      uint64_t Field = cast<ConstantInt>(Idx)->getZExtValue();
      if (Field) {
        // N = N + Offset
        TotalOffs += DL.getStructLayout(StTy)->getElementOffset(Field);
        if (TotalOffs >= MaxOffs) {
          N = fastEmit_ri_(VT, ISD::ADD, N, NIsKill, TotalOffs, VT);
          if (!N) // Unhandled operand. Halt "fast" selection and bail.
            return false;
          NIsKill = true;
          TotalOffs = 0;
        }
      }
    } else {
      Type *Ty = GTI.getIndexedType();

      // If this is a constant subscript, handle it quickly.
      if (const auto *CI = dyn_cast<ConstantInt>(Idx)) {
        if (CI->isZero())
          continue;
        // N = N + Offset
        uint64_t IdxN = CI->getValue().sextOrTrunc(64).getSExtValue();
        TotalOffs += DL.getTypeAllocSize(Ty) * IdxN;
        if (TotalOffs >= MaxOffs) {
          N = fastEmit_ri_(VT, ISD::ADD, N, NIsKill, TotalOffs, VT);
          if (!N) // Unhandled operand. Halt "fast" selection and bail.
            return false;
          NIsKill = true;
          TotalOffs = 0;
        }
        continue;
      }
      if (TotalOffs) {
        N = fastEmit_ri_(VT, ISD::ADD, N, NIsKill, TotalOffs, VT);
        if (!N) // Unhandled operand. Halt "fast" selection and bail.
          return false;
        NIsKill = true;
        TotalOffs = 0;
      }

      // N = N + Idx * ElementSize;
      uint64_t ElementSize = DL.getTypeAllocSize(Ty);
      std::pair<unsigned, bool> Pair = getRegForGEPIndex(Idx);
      unsigned IdxN = Pair.first;
      bool IdxNIsKill = Pair.second;
      if (!IdxN) // Unhandled operand. Halt "fast" selection and bail.
        return false;

      if (ElementSize != 1) {
        IdxN = fastEmit_ri_(VT, ISD::MUL, IdxN, IdxNIsKill, ElementSize, VT);
        if (!IdxN) // Unhandled operand. Halt "fast" selection and bail.
          return false;
        IdxNIsKill = true;
      }
      N = fastEmit_rr(VT, VT, ISD::ADD, N, NIsKill, IdxN, IdxNIsKill);
      if (!N) // Unhandled operand. Halt "fast" selection and bail.
        return false;
    }
  }
  if (TotalOffs) {
    N = fastEmit_ri_(VT, ISD::ADD, N, NIsKill, TotalOffs, VT);
    if (!N) // Unhandled operand. Halt "fast" selection and bail.
      return false;
  }

  // We successfully emitted code for the given LLVM Instruction.
  updateValueMap(I, N);
  return true;
}

bool FastISel::addStackMapLiveVars(SmallVectorImpl<MachineOperand> &Ops,
                                   const CallInst *CI, unsigned StartIdx) {
  for (unsigned i = StartIdx, e = CI->getNumArgOperands(); i != e; ++i) {
    Value *Val = CI->getArgOperand(i);
    // Check for constants and encode them with a StackMaps::ConstantOp prefix.
    if (const auto *C = dyn_cast<ConstantInt>(Val)) {
      Ops.push_back(MachineOperand::CreateImm(StackMaps::ConstantOp));
      Ops.push_back(MachineOperand::CreateImm(C->getSExtValue()));
    } else if (isa<ConstantPointerNull>(Val)) {
      Ops.push_back(MachineOperand::CreateImm(StackMaps::ConstantOp));
      Ops.push_back(MachineOperand::CreateImm(0));
    } else if (auto *AI = dyn_cast<AllocaInst>(Val)) {
      // Values coming from a stack location also require a special encoding,
      // but that is added later on by the target specific frame index
      // elimination implementation.
      auto SI = FuncInfo.StaticAllocaMap.find(AI);
      if (SI != FuncInfo.StaticAllocaMap.end())
        Ops.push_back(MachineOperand::CreateFI(SI->second));
      else
        return false;
    } else {
      unsigned Reg = getRegForValue(Val);
      if (!Reg)
        return false;
      Ops.push_back(MachineOperand::CreateReg(Reg, /*IsDef=*/false));
    }
  }
  return true;
}

bool FastISel::selectStackmap(const CallInst *I) {
  // void @llvm.experimental.stackmap(i64 <id>, i32 <numShadowBytes>,
  //                                  [live variables...])
  assert(I->getCalledFunction()->getReturnType()->isVoidTy() &&
         "Stackmap cannot return a value.");

  // The stackmap intrinsic only records the live variables (the arguments
  // passed to it) and emits NOPS (if requested). Unlike the patchpoint
  // intrinsic, this won't be lowered to a function call. This means we don't
  // have to worry about calling conventions and target-specific lowering code.
  // Instead we perform the call lowering right here.
  //
  // CALLSEQ_START(0, 0...)
  // STACKMAP(id, nbytes, ...)
  // CALLSEQ_END(0, 0)
  //
  SmallVector<MachineOperand, 32> Ops;

  // Add the <id> and <numBytes> constants.
  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::IDPos)) &&
         "Expected a constant integer.");
  const auto *ID = cast<ConstantInt>(I->getOperand(PatchPointOpers::IDPos));
  Ops.push_back(MachineOperand::CreateImm(ID->getZExtValue()));

  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos)) &&
         "Expected a constant integer.");
  const auto *NumBytes =
      cast<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(MachineOperand::CreateImm(NumBytes->getZExtValue()));

  // Push live variables for the stack map (skipping the first two arguments
  // <id> and <numBytes>).
  if (!addStackMapLiveVars(Ops, I, 2))
    return false;

  // We are not adding any register mask info here, because the stackmap doesn't
  // clobber anything.

  // Add scratch registers as implicit def and early clobber.
  CallingConv::ID CC = I->getCallingConv();
  const MCPhysReg *ScratchRegs = TLI.getScratchRegisters(CC);
  for (unsigned i = 0; ScratchRegs[i]; ++i)
    Ops.push_back(MachineOperand::CreateReg(
        ScratchRegs[i], /*IsDef=*/true, /*IsImp=*/true, /*IsKill=*/false,
        /*IsDead=*/false, /*IsUndef=*/false, /*IsEarlyClobber=*/true));

  // Issue CALLSEQ_START
  unsigned AdjStackDown = TII.getCallFrameSetupOpcode();
  auto Builder =
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, TII.get(AdjStackDown));
  const MCInstrDesc &MCID = Builder.getInstr()->getDesc();
  for (unsigned I = 0, E = MCID.getNumOperands(); I < E; ++I)
    Builder.addImm(0);

  // Issue STACKMAP.
  MachineInstrBuilder MIB = BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
                                    TII.get(TargetOpcode::STACKMAP));
  for (auto const &MO : Ops)
    MIB.add(MO);

  // Issue CALLSEQ_END
  unsigned AdjStackUp = TII.getCallFrameDestroyOpcode();
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, TII.get(AdjStackUp))
      .addImm(0)
      .addImm(0);

  // Inform the Frame Information that we have a stackmap in this function.
  FuncInfo.MF->getFrameInfo().setHasStackMap();

  return true;
}

/// Lower an argument list according to the target calling convention.
///
/// This is a helper for lowering intrinsics that follow a target calling
/// convention or require stack pointer adjustment. Only a subset of the
/// intrinsic's operands need to participate in the calling convention.
bool FastISel::lowerCallOperands(const CallInst *CI, unsigned ArgIdx,
                                 unsigned NumArgs, const Value *Callee,
                                 bool ForceRetVoidTy, CallLoweringInfo &CLI) {
  ArgListTy Args;
  Args.reserve(NumArgs);

  // Populate the argument list.
  ImmutableCallSite CS(CI);
  for (unsigned ArgI = ArgIdx, ArgE = ArgIdx + NumArgs; ArgI != ArgE; ++ArgI) {
    Value *V = CI->getOperand(ArgI);

    assert(!V->getType()->isEmptyTy() && "Empty type passed to intrinsic.");

    ArgListEntry Entry;
    Entry.Val = V;
    Entry.Ty = V->getType();
    Entry.setAttributes(&CS, ArgI);
    Args.push_back(Entry);
  }

  Type *RetTy = ForceRetVoidTy ? Type::getVoidTy(CI->getType()->getContext())
                               : CI->getType();
  CLI.setCallee(CI->getCallingConv(), RetTy, Callee, std::move(Args), NumArgs);

  return lowerCallTo(CLI);
}

FastISel::CallLoweringInfo &FastISel::CallLoweringInfo::setCallee(
    const DataLayout &DL, MCContext &Ctx, CallingConv::ID CC, Type *ResultTy,
    StringRef Target, ArgListTy &&ArgsList, unsigned FixedArgs) {
  SmallString<32> MangledName;
  Mangler::getNameWithPrefix(MangledName, Target, DL);
  MCSymbol *Sym = Ctx.getOrCreateSymbol(MangledName);
  return setCallee(CC, ResultTy, Sym, std::move(ArgsList), FixedArgs);
}

bool FastISel::selectPatchpoint(const CallInst *I) {
  // void|i64 @llvm.experimental.patchpoint.void|i64(i64 <id>,
  //                                                 i32 <numBytes>,
  //                                                 i8* <target>,
  //                                                 i32 <numArgs>,
  //                                                 [Args...],
  //                                                 [live variables...])
  CallingConv::ID CC = I->getCallingConv();
  bool IsAnyRegCC = CC == CallingConv::AnyReg;
  bool HasDef = !I->getType()->isVoidTy();
  Value *Callee = I->getOperand(PatchPointOpers::TargetPos)->stripPointerCasts();

  // Get the real number of arguments participating in the call <numArgs>
  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::NArgPos)) &&
         "Expected a constant integer.");
  const auto *NumArgsVal =
      cast<ConstantInt>(I->getOperand(PatchPointOpers::NArgPos));
  unsigned NumArgs = NumArgsVal->getZExtValue();

  // Skip the four meta args: <id>, <numNopBytes>, <target>, <numArgs>
  // This includes all meta-operands up to but not including CC.
  unsigned NumMetaOpers = PatchPointOpers::CCPos;
  assert(I->getNumArgOperands() >= NumMetaOpers + NumArgs &&
         "Not enough arguments provided to the patchpoint intrinsic");

  // For AnyRegCC the arguments are lowered later on manually.
  unsigned NumCallArgs = IsAnyRegCC ? 0 : NumArgs;
  CallLoweringInfo CLI;
  CLI.setIsPatchPoint();
  if (!lowerCallOperands(I, NumMetaOpers, NumCallArgs, Callee, IsAnyRegCC, CLI))
    return false;

  assert(CLI.Call && "No call instruction specified.");

  SmallVector<MachineOperand, 32> Ops;

  // Add an explicit result reg if we use the anyreg calling convention.
  if (IsAnyRegCC && HasDef) {
    assert(CLI.NumResultRegs == 0 && "Unexpected result register.");
    CLI.ResultReg = createResultReg(TLI.getRegClassFor(MVT::i64));
    CLI.NumResultRegs = 1;
    Ops.push_back(MachineOperand::CreateReg(CLI.ResultReg, /*IsDef=*/true));
  }

  // Add the <id> and <numBytes> constants.
  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::IDPos)) &&
         "Expected a constant integer.");
  const auto *ID = cast<ConstantInt>(I->getOperand(PatchPointOpers::IDPos));
  Ops.push_back(MachineOperand::CreateImm(ID->getZExtValue()));

  assert(isa<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos)) &&
         "Expected a constant integer.");
  const auto *NumBytes =
      cast<ConstantInt>(I->getOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(MachineOperand::CreateImm(NumBytes->getZExtValue()));

  // Add the call target.
  if (const auto *C = dyn_cast<IntToPtrInst>(Callee)) {
    uint64_t CalleeConstAddr =
      cast<ConstantInt>(C->getOperand(0))->getZExtValue();
    Ops.push_back(MachineOperand::CreateImm(CalleeConstAddr));
  } else if (const auto *C = dyn_cast<ConstantExpr>(Callee)) {
    if (C->getOpcode() == Instruction::IntToPtr) {
      uint64_t CalleeConstAddr =
        cast<ConstantInt>(C->getOperand(0))->getZExtValue();
      Ops.push_back(MachineOperand::CreateImm(CalleeConstAddr));
    } else
      llvm_unreachable("Unsupported ConstantExpr.");
  } else if (const auto *GV = dyn_cast<GlobalValue>(Callee)) {
    Ops.push_back(MachineOperand::CreateGA(GV, 0));
  } else if (isa<ConstantPointerNull>(Callee))
    Ops.push_back(MachineOperand::CreateImm(0));
  else
    llvm_unreachable("Unsupported callee address.");

  // Adjust <numArgs> to account for any arguments that have been passed on
  // the stack instead.
  unsigned NumCallRegArgs = IsAnyRegCC ? NumArgs : CLI.OutRegs.size();
  Ops.push_back(MachineOperand::CreateImm(NumCallRegArgs));

  // Add the calling convention
  Ops.push_back(MachineOperand::CreateImm((unsigned)CC));

  // Add the arguments we omitted previously. The register allocator should
  // place these in any free register.
  if (IsAnyRegCC) {
    for (unsigned i = NumMetaOpers, e = NumMetaOpers + NumArgs; i != e; ++i) {
      unsigned Reg = getRegForValue(I->getArgOperand(i));
      if (!Reg)
        return false;
      Ops.push_back(MachineOperand::CreateReg(Reg, /*IsDef=*/false));
    }
  }

  // Push the arguments from the call instruction.
  for (auto Reg : CLI.OutRegs)
    Ops.push_back(MachineOperand::CreateReg(Reg, /*IsDef=*/false));

  // Push live variables for the stack map.
  if (!addStackMapLiveVars(Ops, I, NumMetaOpers + NumArgs))
    return false;

  // Push the register mask info.
  Ops.push_back(MachineOperand::CreateRegMask(
      TRI.getCallPreservedMask(*FuncInfo.MF, CC)));

  // Add scratch registers as implicit def and early clobber.
  const MCPhysReg *ScratchRegs = TLI.getScratchRegisters(CC);
  for (unsigned i = 0; ScratchRegs[i]; ++i)
    Ops.push_back(MachineOperand::CreateReg(
        ScratchRegs[i], /*IsDef=*/true, /*IsImp=*/true, /*IsKill=*/false,
        /*IsDead=*/false, /*IsUndef=*/false, /*IsEarlyClobber=*/true));

  // Add implicit defs (return values).
  for (auto Reg : CLI.InRegs)
    Ops.push_back(MachineOperand::CreateReg(Reg, /*IsDef=*/true,
                                            /*IsImpl=*/true));

  // Insert the patchpoint instruction before the call generated by the target.
  MachineInstrBuilder MIB = BuildMI(*FuncInfo.MBB, CLI.Call, DbgLoc,
                                    TII.get(TargetOpcode::PATCHPOINT));

  for (auto &MO : Ops)
    MIB.add(MO);

  MIB->setPhysRegsDeadExcept(CLI.InRegs, TRI);

  // Delete the original call instruction.
  CLI.Call->eraseFromParent();

  // Inform the Frame Information that we have a patchpoint in this function.
  FuncInfo.MF->getFrameInfo().setHasPatchPoint();

  if (CLI.NumResultRegs)
    updateValueMap(I, CLI.ResultReg, CLI.NumResultRegs);
  return true;
}

bool FastISel::selectXRayCustomEvent(const CallInst *I) {
  const auto &Triple = TM.getTargetTriple();
  if (Triple.getArch() != Triple::x86_64 || !Triple.isOSLinux())
    return true; // don't do anything to this instruction.
  SmallVector<MachineOperand, 8> Ops;
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(0)),
                                          /*IsDef=*/false));
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
                                          /*IsDef=*/false));
  MachineInstrBuilder MIB =
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
              TII.get(TargetOpcode::PATCHABLE_EVENT_CALL));
  for (auto &MO : Ops)
    MIB.add(MO);

  // Insert the Patchable Event Call instruction, that gets lowered properly.
  return true;
}

bool FastISel::selectXRayTypedEvent(const CallInst *I) {
  const auto &Triple = TM.getTargetTriple();
  if (Triple.getArch() != Triple::x86_64 || !Triple.isOSLinux())
    return true; // don't do anything to this instruction.
  SmallVector<MachineOperand, 8> Ops;
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(0)),
                                          /*IsDef=*/false));
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
                                          /*IsDef=*/false));
  Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(2)),
                                          /*IsDef=*/false));
  MachineInstrBuilder MIB =
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
              TII.get(TargetOpcode::PATCHABLE_TYPED_EVENT_CALL));
  for (auto &MO : Ops)
    MIB.add(MO);

  // Insert the Patchable Typed Event Call instruction, that gets lowered properly.
  return true;
}

/// Returns an AttributeList representing the attributes applied to the return
/// value of the given call.
static AttributeList getReturnAttrs(FastISel::CallLoweringInfo &CLI) {
  SmallVector<Attribute::AttrKind, 2> Attrs;
  if (CLI.RetSExt)
    Attrs.push_back(Attribute::SExt);
  if (CLI.RetZExt)
    Attrs.push_back(Attribute::ZExt);
  if (CLI.IsInReg)
    Attrs.push_back(Attribute::InReg);

  return AttributeList::get(CLI.RetTy->getContext(), AttributeList::ReturnIndex,
                            Attrs);
}

bool FastISel::lowerCallTo(const CallInst *CI, const char *SymName,
                           unsigned NumArgs) {
  MCContext &Ctx = MF->getContext();
  SmallString<32> MangledName;
  Mangler::getNameWithPrefix(MangledName, SymName, DL);
  MCSymbol *Sym = Ctx.getOrCreateSymbol(MangledName);
  return lowerCallTo(CI, Sym, NumArgs);
}

bool FastISel::lowerCallTo(const CallInst *CI, MCSymbol *Symbol,
                           unsigned NumArgs) {
  ImmutableCallSite CS(CI);

  FunctionType *FTy = CS.getFunctionType();
  Type *RetTy = CS.getType();

  ArgListTy Args;
  Args.reserve(NumArgs);

  // Populate the argument list.
  // Attributes for args start at offset 1, after the return attribute.
  for (unsigned ArgI = 0; ArgI != NumArgs; ++ArgI) {
    Value *V = CI->getOperand(ArgI);

    assert(!V->getType()->isEmptyTy() && "Empty type passed to intrinsic.");

    ArgListEntry Entry;
    Entry.Val = V;
    Entry.Ty = V->getType();
    Entry.setAttributes(&CS, ArgI);
    Args.push_back(Entry);
  }
  TLI.markLibCallAttributes(MF, CS.getCallingConv(), Args);

  CallLoweringInfo CLI;
  CLI.setCallee(RetTy, FTy, Symbol, std::move(Args), CS, NumArgs);

  return lowerCallTo(CLI);
}

bool FastISel::lowerCallTo(CallLoweringInfo &CLI) {
  // Handle the incoming return values from the call.
  CLI.clearIns();
  SmallVector<EVT, 4> RetTys;
  ComputeValueVTs(TLI, DL, CLI.RetTy, RetTys);

  SmallVector<ISD::OutputArg, 4> Outs;
  GetReturnInfo(CLI.CallConv, CLI.RetTy, getReturnAttrs(CLI), Outs, TLI, DL);

  bool CanLowerReturn = TLI.CanLowerReturn(
      CLI.CallConv, *FuncInfo.MF, CLI.IsVarArg, Outs, CLI.RetTy->getContext());

  // FIXME: sret demotion isn't supported yet - bail out.
  if (!CanLowerReturn)
    return false;

  for (unsigned I = 0, E = RetTys.size(); I != E; ++I) {
    EVT VT = RetTys[I];
    MVT RegisterVT = TLI.getRegisterType(CLI.RetTy->getContext(), VT);
    unsigned NumRegs = TLI.getNumRegisters(CLI.RetTy->getContext(), VT);
    for (unsigned i = 0; i != NumRegs; ++i) {
      ISD::InputArg MyFlags;
      MyFlags.VT = RegisterVT;
      MyFlags.ArgVT = VT;
      MyFlags.Used = CLI.IsReturnValueUsed;
      if (CLI.RetSExt)
        MyFlags.Flags.setSExt();
      if (CLI.RetZExt)
        MyFlags.Flags.setZExt();
      if (CLI.IsInReg)
        MyFlags.Flags.setInReg();
      CLI.Ins.push_back(MyFlags);
    }
  }

  // Handle all of the outgoing arguments.
  CLI.clearOuts();
  for (auto &Arg : CLI.getArgs()) {
    Type *FinalType = Arg.Ty;
    if (Arg.IsByVal)
      FinalType = cast<PointerType>(Arg.Ty)->getElementType();
    bool NeedsRegBlock = TLI.functionArgumentNeedsConsecutiveRegisters(
        FinalType, CLI.CallConv, CLI.IsVarArg);

    ISD::ArgFlagsTy Flags;
    if (Arg.IsZExt)
      Flags.setZExt();
    if (Arg.IsSExt)
      Flags.setSExt();
    if (Arg.IsInReg)
      Flags.setInReg();
    if (Arg.IsSRet)
      Flags.setSRet();
    if (Arg.IsSwiftSelf)
      Flags.setSwiftSelf();
    if (Arg.IsSwiftError)
      Flags.setSwiftError();
    if (Arg.IsByVal)
      Flags.setByVal();
    if (Arg.IsInAlloca) {
      Flags.setInAlloca();
      // Set the byval flag for CCAssignFn callbacks that don't know about
      // inalloca. This way we can know how many bytes we should've allocated
      // and how many bytes a callee cleanup function will pop.  If we port
      // inalloca to more targets, we'll have to add custom inalloca handling in
      // the various CC lowering callbacks.
      Flags.setByVal();
    }
    if (Arg.IsByVal || Arg.IsInAlloca) {
      PointerType *Ty = cast<PointerType>(Arg.Ty);
      Type *ElementTy = Ty->getElementType();
      unsigned FrameSize = DL.getTypeAllocSize(ElementTy);
      // For ByVal, alignment should come from FE. BE will guess if this info is
      // not there, but there are cases it cannot get right.
      unsigned FrameAlign = Arg.Alignment;
      if (!FrameAlign)
        FrameAlign = TLI.getByValTypeAlignment(ElementTy, DL);
      Flags.setByValSize(FrameSize);
      Flags.setByValAlign(FrameAlign);
    }
    if (Arg.IsNest)
      Flags.setNest();
    if (NeedsRegBlock)
      Flags.setInConsecutiveRegs();
    unsigned OriginalAlignment = DL.getABITypeAlignment(Arg.Ty);
    Flags.setOrigAlign(OriginalAlignment);

    CLI.OutVals.push_back(Arg.Val);
    CLI.OutFlags.push_back(Flags);
  }

  if (!fastLowerCall(CLI))
    return false;

  // Set all unused physreg defs as dead.
  assert(CLI.Call && "No call instruction specified.");
  CLI.Call->setPhysRegsDeadExcept(CLI.InRegs, TRI);

  if (CLI.NumResultRegs && CLI.CS)
    updateValueMap(CLI.CS->getInstruction(), CLI.ResultReg, CLI.NumResultRegs);

  return true;
}

bool FastISel::lowerCall(const CallInst *CI) {
  ImmutableCallSite CS(CI);

  FunctionType *FuncTy = CS.getFunctionType();
  Type *RetTy = CS.getType();

  ArgListTy Args;
  ArgListEntry Entry;
  Args.reserve(CS.arg_size());

  for (ImmutableCallSite::arg_iterator i = CS.arg_begin(), e = CS.arg_end();
       i != e; ++i) {
    Value *V = *i;

    // Skip empty types
    if (V->getType()->isEmptyTy())
      continue;

    Entry.Val = V;
    Entry.Ty = V->getType();

    // Skip the first return-type Attribute to get to params.
    Entry.setAttributes(&CS, i - CS.arg_begin());
    Args.push_back(Entry);
  }

  // Check if target-independent constraints permit a tail call here.
  // Target-dependent constraints are checked within fastLowerCall.
  bool IsTailCall = CI->isTailCall();
  if (IsTailCall && !isInTailCallPosition(CS, TM))
    IsTailCall = false;

  CallLoweringInfo CLI;
  CLI.setCallee(RetTy, FuncTy, CI->getCalledValue(), std::move(Args), CS)
      .setTailCall(IsTailCall);

  return lowerCallTo(CLI);
}

bool FastISel::selectCall(const User *I) {
  const CallInst *Call = cast<CallInst>(I);

  // Handle simple inline asms.
  if (const InlineAsm *IA = dyn_cast<InlineAsm>(Call->getCalledValue())) {
    // If the inline asm has side effects, then make sure that no local value
    // lives across by flushing the local value map.
    if (IA->hasSideEffects())
      flushLocalValueMap();

    // Don't attempt to handle constraints.
    if (!IA->getConstraintString().empty())
      return false;

    unsigned ExtraInfo = 0;
    if (IA->hasSideEffects())
      ExtraInfo |= InlineAsm::Extra_HasSideEffects;
    if (IA->isAlignStack())
      ExtraInfo |= InlineAsm::Extra_IsAlignStack;

    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::INLINEASM))
        .addExternalSymbol(IA->getAsmString().c_str())
        .addImm(ExtraInfo);
    return true;
  }

  MachineModuleInfo &MMI = FuncInfo.MF->getMMI();
  computeUsesVAFloatArgument(*Call, MMI);

  // Handle intrinsic function calls.
  if (const auto *II = dyn_cast<IntrinsicInst>(Call))
    return selectIntrinsicCall(II);

  // Usually, it does not make sense to initialize a value,
  // make an unrelated function call and use the value, because
  // it tends to be spilled on the stack. So, we move the pointer
  // to the last local value to the beginning of the block, so that
  // all the values which have already been materialized,
  // appear after the call. It also makes sense to skip intrinsics
  // since they tend to be inlined.
  flushLocalValueMap();

  return lowerCall(Call);
}

bool FastISel::selectIntrinsicCall(const IntrinsicInst *II) {
  switch (II->getIntrinsicID()) {
  default:
    break;
  // At -O0 we don't care about the lifetime intrinsics.
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  // The donothing intrinsic does, well, nothing.
  case Intrinsic::donothing:
  // Neither does the sideeffect intrinsic.
  case Intrinsic::sideeffect:
  // Neither does the assume intrinsic; it's also OK not to codegen its operand.
  case Intrinsic::assume:
    return true;
  case Intrinsic::dbg_declare: {
    const DbgDeclareInst *DI = cast<DbgDeclareInst>(II);
    assert(DI->getVariable() && "Missing variable");
    if (!FuncInfo.MF->getMMI().hasDebugInfo()) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
      return true;
    }

    const Value *Address = DI->getAddress();
    if (!Address || isa<UndefValue>(Address)) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
      return true;
    }

    // Byval arguments with frame indices were already handled after argument
    // lowering and before isel.
    const auto *Arg =
        dyn_cast<Argument>(Address->stripInBoundsConstantOffsets());
    if (Arg && FuncInfo.getArgumentFrameIndex(Arg) != INT_MAX)
      return true;

    Optional<MachineOperand> Op;
    if (unsigned Reg = lookUpRegForValue(Address))
      Op = MachineOperand::CreateReg(Reg, false);

    // If we have a VLA that has a "use" in a metadata node that's then used
    // here but it has no other uses, then we have a problem. E.g.,
    //
    //   int foo (const int *x) {
    //     char a[*x];
    //     return 0;
    //   }
    //
    // If we assign 'a' a vreg and fast isel later on has to use the selection
    // DAG isel, it will want to copy the value to the vreg. However, there are
    // no uses, which goes counter to what selection DAG isel expects.
    if (!Op && !Address->use_empty() && isa<Instruction>(Address) &&
        (!isa<AllocaInst>(Address) ||
         !FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(Address))))
      Op = MachineOperand::CreateReg(FuncInfo.InitializeRegForValue(Address),
                                     false);

    if (Op) {
      assert(DI->getVariable()->isValidLocationForIntrinsic(DbgLoc) &&
             "Expected inlined-at fields to agree");
      // A dbg.declare describes the address of a source variable, so lower it
      // into an indirect DBG_VALUE.
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
              TII.get(TargetOpcode::DBG_VALUE), /*IsIndirect*/ true,
              *Op, DI->getVariable(), DI->getExpression());
    } else {
      // We can't yet handle anything else here because it would require
      // generating code, thus altering codegen because of debug info.
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
    }
    return true;
  }
  case Intrinsic::dbg_value: {
    // This form of DBG_VALUE is target-independent.
    const DbgValueInst *DI = cast<DbgValueInst>(II);
    const MCInstrDesc &II = TII.get(TargetOpcode::DBG_VALUE);
    const Value *V = DI->getValue();
    assert(DI->getVariable()->isValidLocationForIntrinsic(DbgLoc) &&
           "Expected inlined-at fields to agree");
    if (!V) {
      // Currently the optimizer can produce this; insert an undef to
      // help debugging.  Probably the optimizer should not do this.
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, false, 0U,
              DI->getVariable(), DI->getExpression());
    } else if (const auto *CI = dyn_cast<ConstantInt>(V)) {
      if (CI->getBitWidth() > 64)
        BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
            .addCImm(CI)
            .addImm(0U)
            .addMetadata(DI->getVariable())
            .addMetadata(DI->getExpression());
      else
        BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
            .addImm(CI->getZExtValue())
            .addImm(0U)
            .addMetadata(DI->getVariable())
            .addMetadata(DI->getExpression());
    } else if (const auto *CF = dyn_cast<ConstantFP>(V)) {
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
          .addFPImm(CF)
          .addImm(0U)
          .addMetadata(DI->getVariable())
          .addMetadata(DI->getExpression());
    } else if (unsigned Reg = lookUpRegForValue(V)) {
      // FIXME: This does not handle register-indirect values at offset 0.
      bool IsIndirect = false;
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, IsIndirect, Reg,
              DI->getVariable(), DI->getExpression());
    } else {
      // We can't yet handle anything else here because it would require
      // generating code, thus altering codegen because of debug info.
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
    }
    return true;
  }
  case Intrinsic::dbg_label: {
    const DbgLabelInst *DI = cast<DbgLabelInst>(II);
    assert(DI->getLabel() && "Missing label");
    if (!FuncInfo.MF->getMMI().hasDebugInfo()) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
      return true;
    }

    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::DBG_LABEL)).addMetadata(DI->getLabel());
    return true;
  }
  case Intrinsic::objectsize: {
    ConstantInt *CI = cast<ConstantInt>(II->getArgOperand(1));
    unsigned long long Res = CI->isZero() ? -1ULL : 0;
    Constant *ResCI = ConstantInt::get(II->getType(), Res);
    unsigned ResultReg = getRegForValue(ResCI);
    if (!ResultReg)
      return false;
    updateValueMap(II, ResultReg);
    return true;
  }
  case Intrinsic::is_constant: {
    Constant *ResCI = ConstantInt::get(II->getType(), 0);
    unsigned ResultReg = getRegForValue(ResCI);
    if (!ResultReg)
      return false;
    updateValueMap(II, ResultReg);
    return true;
  }
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
  case Intrinsic::expect: {
    unsigned ResultReg = getRegForValue(II->getArgOperand(0));
    if (!ResultReg)
      return false;
    updateValueMap(II, ResultReg);
    return true;
  }
  case Intrinsic::experimental_stackmap:
    return selectStackmap(II);
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint_i64:
    return selectPatchpoint(II);

  case Intrinsic::xray_customevent:
    return selectXRayCustomEvent(II);
  case Intrinsic::xray_typedevent:
    return selectXRayTypedEvent(II);
  }

  return fastLowerIntrinsicCall(II);
}

bool FastISel::selectCast(const User *I, unsigned Opcode) {
  EVT SrcVT = TLI.getValueType(DL, I->getOperand(0)->getType());
  EVT DstVT = TLI.getValueType(DL, I->getType());

  if (SrcVT == MVT::Other || !SrcVT.isSimple() || DstVT == MVT::Other ||
      !DstVT.isSimple())
    // Unhandled type. Halt "fast" selection and bail.
    return false;

  // Check if the destination type is legal.
  if (!TLI.isTypeLegal(DstVT))
    return false;

  // Check if the source operand is legal.
  if (!TLI.isTypeLegal(SrcVT))
    return false;

  unsigned InputReg = getRegForValue(I->getOperand(0));
  if (!InputReg)
    // Unhandled operand.  Halt "fast" selection and bail.
    return false;

  bool InputRegIsKill = hasTrivialKill(I->getOperand(0));

  unsigned ResultReg = fastEmit_r(SrcVT.getSimpleVT(), DstVT.getSimpleVT(),
                                  Opcode, InputReg, InputRegIsKill);
  if (!ResultReg)
    return false;

  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectBitCast(const User *I) {
  // If the bitcast doesn't change the type, just use the operand value.
  if (I->getType() == I->getOperand(0)->getType()) {
    unsigned Reg = getRegForValue(I->getOperand(0));
    if (!Reg)
      return false;
    updateValueMap(I, Reg);
    return true;
  }

  // Bitcasts of other values become reg-reg copies or BITCAST operators.
  EVT SrcEVT = TLI.getValueType(DL, I->getOperand(0)->getType());
  EVT DstEVT = TLI.getValueType(DL, I->getType());
  if (SrcEVT == MVT::Other || DstEVT == MVT::Other ||
      !TLI.isTypeLegal(SrcEVT) || !TLI.isTypeLegal(DstEVT))
    // Unhandled type. Halt "fast" selection and bail.
    return false;

  MVT SrcVT = SrcEVT.getSimpleVT();
  MVT DstVT = DstEVT.getSimpleVT();
  unsigned Op0 = getRegForValue(I->getOperand(0));
  if (!Op0) // Unhandled operand. Halt "fast" selection and bail.
    return false;
  bool Op0IsKill = hasTrivialKill(I->getOperand(0));

  // First, try to perform the bitcast by inserting a reg-reg copy.
  unsigned ResultReg = 0;
  if (SrcVT == DstVT) {
    const TargetRegisterClass *SrcClass = TLI.getRegClassFor(SrcVT);
    const TargetRegisterClass *DstClass = TLI.getRegClassFor(DstVT);
    // Don't attempt a cross-class copy. It will likely fail.
    if (SrcClass == DstClass) {
      ResultReg = createResultReg(DstClass);
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
              TII.get(TargetOpcode::COPY), ResultReg).addReg(Op0);
    }
  }

  // If the reg-reg copy failed, select a BITCAST opcode.
  if (!ResultReg)
    ResultReg = fastEmit_r(SrcVT, DstVT, ISD::BITCAST, Op0, Op0IsKill);

  if (!ResultReg)
    return false;

  updateValueMap(I, ResultReg);
  return true;
}

// Remove local value instructions starting from the instruction after
// SavedLastLocalValue to the current function insert point.
void FastISel::removeDeadLocalValueCode(MachineInstr *SavedLastLocalValue)
{
  MachineInstr *CurLastLocalValue = getLastLocalValue();
  if (CurLastLocalValue != SavedLastLocalValue) {
    // Find the first local value instruction to be deleted.
    // This is the instruction after SavedLastLocalValue if it is non-NULL.
    // Otherwise it's the first instruction in the block.
    MachineBasicBlock::iterator FirstDeadInst(SavedLastLocalValue);
    if (SavedLastLocalValue)
      ++FirstDeadInst;
    else
      FirstDeadInst = FuncInfo.MBB->getFirstNonPHI();
    setLastLocalValue(SavedLastLocalValue);
    removeDeadCode(FirstDeadInst, FuncInfo.InsertPt);
  }
}

bool FastISel::selectInstruction(const Instruction *I) {
  MachineInstr *SavedLastLocalValue = getLastLocalValue();
  // Just before the terminator instruction, insert instructions to
  // feed PHI nodes in successor blocks.
  if (I->isTerminator()) {
    if (!handlePHINodesInSuccessorBlocks(I->getParent())) {
      // PHI node handling may have generated local value instructions,
      // even though it failed to handle all PHI nodes.
      // We remove these instructions because SelectionDAGISel will generate
      // them again.
      removeDeadLocalValueCode(SavedLastLocalValue);
      return false;
    }
  }

  // FastISel does not handle any operand bundles except OB_funclet.
  if (ImmutableCallSite CS = ImmutableCallSite(I))
    for (unsigned i = 0, e = CS.getNumOperandBundles(); i != e; ++i)
      if (CS.getOperandBundleAt(i).getTagID() != LLVMContext::OB_funclet)
        return false;

  DbgLoc = I->getDebugLoc();

  SavedInsertPt = FuncInfo.InsertPt;

  if (const auto *Call = dyn_cast<CallInst>(I)) {
    const Function *F = Call->getCalledFunction();
    LibFunc Func;

    // As a special case, don't handle calls to builtin library functions that
    // may be translated directly to target instructions.
    if (F && !F->hasLocalLinkage() && F->hasName() &&
        LibInfo->getLibFunc(F->getName(), Func) &&
        LibInfo->hasOptimizedCodeGen(Func))
      return false;

    // Don't handle Intrinsic::trap if a trap function is specified.
    if (F && F->getIntrinsicID() == Intrinsic::trap &&
        Call->hasFnAttr("trap-func-name"))
      return false;
  }

  // First, try doing target-independent selection.
  if (!SkipTargetIndependentISel) {
    if (selectOperator(I, I->getOpcode())) {
      ++NumFastIselSuccessIndependent;
      DbgLoc = DebugLoc();
      return true;
    }
    // Remove dead code.
    recomputeInsertPt();
    if (SavedInsertPt != FuncInfo.InsertPt)
      removeDeadCode(FuncInfo.InsertPt, SavedInsertPt);
    SavedInsertPt = FuncInfo.InsertPt;
  }
  // Next, try calling the target to attempt to handle the instruction.
  if (fastSelectInstruction(I)) {
    ++NumFastIselSuccessTarget;
    DbgLoc = DebugLoc();
    return true;
  }
  // Remove dead code.
  recomputeInsertPt();
  if (SavedInsertPt != FuncInfo.InsertPt)
    removeDeadCode(FuncInfo.InsertPt, SavedInsertPt);

  DbgLoc = DebugLoc();
  // Undo phi node updates, because they will be added again by SelectionDAG.
  if (I->isTerminator()) {
    // PHI node handling may have generated local value instructions.
    // We remove them because SelectionDAGISel will generate them again.
    removeDeadLocalValueCode(SavedLastLocalValue);
    FuncInfo.PHINodesToUpdate.resize(FuncInfo.OrigNumPHINodesToUpdate);
  }
  return false;
}

/// Emit an unconditional branch to the given block, unless it is the immediate
/// (fall-through) successor, and update the CFG.
void FastISel::fastEmitBranch(MachineBasicBlock *MSucc,
                              const DebugLoc &DbgLoc) {
  if (FuncInfo.MBB->getBasicBlock()->size() > 1 &&
      FuncInfo.MBB->isLayoutSuccessor(MSucc)) {
    // For more accurate line information if this is the only instruction
    // in the block then emit it, otherwise we have the unconditional
    // fall-through case, which needs no instructions.
  } else {
    // The unconditional branch case.
    TII.insertBranch(*FuncInfo.MBB, MSucc, nullptr,
                     SmallVector<MachineOperand, 0>(), DbgLoc);
  }
  if (FuncInfo.BPI) {
    auto BranchProbability = FuncInfo.BPI->getEdgeProbability(
        FuncInfo.MBB->getBasicBlock(), MSucc->getBasicBlock());
    FuncInfo.MBB->addSuccessor(MSucc, BranchProbability);
  } else
    FuncInfo.MBB->addSuccessorWithoutProb(MSucc);
}

void FastISel::finishCondBranch(const BasicBlock *BranchBB,
                                MachineBasicBlock *TrueMBB,
                                MachineBasicBlock *FalseMBB) {
  // Add TrueMBB as successor unless it is equal to the FalseMBB: This can
  // happen in degenerate IR and MachineIR forbids to have a block twice in the
  // successor/predecessor lists.
  if (TrueMBB != FalseMBB) {
    if (FuncInfo.BPI) {
      auto BranchProbability =
          FuncInfo.BPI->getEdgeProbability(BranchBB, TrueMBB->getBasicBlock());
      FuncInfo.MBB->addSuccessor(TrueMBB, BranchProbability);
    } else
      FuncInfo.MBB->addSuccessorWithoutProb(TrueMBB);
  }

  fastEmitBranch(FalseMBB, DbgLoc);
}

/// Emit an FNeg operation.
bool FastISel::selectFNeg(const User *I) {
  Value *X;
  if (!match(I, m_FNeg(m_Value(X))))
    return false;
  unsigned OpReg = getRegForValue(X);
  if (!OpReg)
    return false;
  bool OpRegIsKill = hasTrivialKill(I);

  // If the target has ISD::FNEG, use it.
  EVT VT = TLI.getValueType(DL, I->getType());
  unsigned ResultReg = fastEmit_r(VT.getSimpleVT(), VT.getSimpleVT(), ISD::FNEG,
                                  OpReg, OpRegIsKill);
  if (ResultReg) {
    updateValueMap(I, ResultReg);
    return true;
  }

  // Bitcast the value to integer, twiddle the sign bit with xor,
  // and then bitcast it back to floating-point.
  if (VT.getSizeInBits() > 64)
    return false;
  EVT IntVT = EVT::getIntegerVT(I->getContext(), VT.getSizeInBits());
  if (!TLI.isTypeLegal(IntVT))
    return false;

  unsigned IntReg = fastEmit_r(VT.getSimpleVT(), IntVT.getSimpleVT(),
                               ISD::BITCAST, OpReg, OpRegIsKill);
  if (!IntReg)
    return false;

  unsigned IntResultReg = fastEmit_ri_(
      IntVT.getSimpleVT(), ISD::XOR, IntReg, /*IsKill=*/true,
      UINT64_C(1) << (VT.getSizeInBits() - 1), IntVT.getSimpleVT());
  if (!IntResultReg)
    return false;

  ResultReg = fastEmit_r(IntVT.getSimpleVT(), VT.getSimpleVT(), ISD::BITCAST,
                         IntResultReg, /*IsKill=*/true);
  if (!ResultReg)
    return false;

  updateValueMap(I, ResultReg);
  return true;
}

bool FastISel::selectExtractValue(const User *U) {
  const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(U);
  if (!EVI)
    return false;

  // Make sure we only try to handle extracts with a legal result.  But also
  // allow i1 because it's easy.
  EVT RealVT = TLI.getValueType(DL, EVI->getType(), /*AllowUnknown=*/true);
  if (!RealVT.isSimple())
    return false;
  MVT VT = RealVT.getSimpleVT();
  if (!TLI.isTypeLegal(VT) && VT != MVT::i1)
    return false;

  const Value *Op0 = EVI->getOperand(0);
  Type *AggTy = Op0->getType();

  // Get the base result register.
  unsigned ResultReg;
  DenseMap<const Value *, unsigned>::iterator I = FuncInfo.ValueMap.find(Op0);
  if (I != FuncInfo.ValueMap.end())
    ResultReg = I->second;
  else if (isa<Instruction>(Op0))
    ResultReg = FuncInfo.InitializeRegForValue(Op0);
  else
    return false; // fast-isel can't handle aggregate constants at the moment

  // Get the actual result register, which is an offset from the base register.
  unsigned VTIndex = ComputeLinearIndex(AggTy, EVI->getIndices());

  SmallVector<EVT, 4> AggValueVTs;
  ComputeValueVTs(TLI, DL, AggTy, AggValueVTs);

  for (unsigned i = 0; i < VTIndex; i++)
    ResultReg += TLI.getNumRegisters(FuncInfo.Fn->getContext(), AggValueVTs[i]);

  updateValueMap(EVI, ResultReg);
  return true;
}

bool FastISel::selectOperator(const User *I, unsigned Opcode) {
  switch (Opcode) {
  case Instruction::Add:
    return selectBinaryOp(I, ISD::ADD);
  case Instruction::FAdd:
    return selectBinaryOp(I, ISD::FADD);
  case Instruction::Sub:
    return selectBinaryOp(I, ISD::SUB);
  case Instruction::FSub: 
    // FNeg is currently represented in LLVM IR as a special case of FSub.
    return selectFNeg(I) || selectBinaryOp(I, ISD::FSUB);
  case Instruction::Mul:
    return selectBinaryOp(I, ISD::MUL);
  case Instruction::FMul:
    return selectBinaryOp(I, ISD::FMUL);
  case Instruction::SDiv:
    return selectBinaryOp(I, ISD::SDIV);
  case Instruction::UDiv:
    return selectBinaryOp(I, ISD::UDIV);
  case Instruction::FDiv:
    return selectBinaryOp(I, ISD::FDIV);
  case Instruction::SRem:
    return selectBinaryOp(I, ISD::SREM);
  case Instruction::URem:
    return selectBinaryOp(I, ISD::UREM);
  case Instruction::FRem:
    return selectBinaryOp(I, ISD::FREM);
  case Instruction::Shl:
    return selectBinaryOp(I, ISD::SHL);
  case Instruction::LShr:
    return selectBinaryOp(I, ISD::SRL);
  case Instruction::AShr:
    return selectBinaryOp(I, ISD::SRA);
  case Instruction::And:
    return selectBinaryOp(I, ISD::AND);
  case Instruction::Or:
    return selectBinaryOp(I, ISD::OR);
  case Instruction::Xor:
    return selectBinaryOp(I, ISD::XOR);

  case Instruction::GetElementPtr:
    return selectGetElementPtr(I);

  case Instruction::Br: {
    const BranchInst *BI = cast<BranchInst>(I);

    if (BI->isUnconditional()) {
      const BasicBlock *LLVMSucc = BI->getSuccessor(0);
      MachineBasicBlock *MSucc = FuncInfo.MBBMap[LLVMSucc];
      fastEmitBranch(MSucc, BI->getDebugLoc());
      return true;
    }

    // Conditional branches are not handed yet.
    // Halt "fast" selection and bail.
    return false;
  }

  case Instruction::Unreachable:
    if (TM.Options.TrapUnreachable)
      return fastEmit_(MVT::Other, MVT::Other, ISD::TRAP) != 0;
    else
      return true;

  case Instruction::Alloca:
    // FunctionLowering has the static-sized case covered.
    if (FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(I)))
      return true;

    // Dynamic-sized alloca is not handled yet.
    return false;

  case Instruction::Call:
    return selectCall(I);

  case Instruction::BitCast:
    return selectBitCast(I);

  case Instruction::FPToSI:
    return selectCast(I, ISD::FP_TO_SINT);
  case Instruction::ZExt:
    return selectCast(I, ISD::ZERO_EXTEND);
  case Instruction::SExt:
    return selectCast(I, ISD::SIGN_EXTEND);
  case Instruction::Trunc:
    return selectCast(I, ISD::TRUNCATE);
  case Instruction::SIToFP:
    return selectCast(I, ISD::SINT_TO_FP);

  case Instruction::IntToPtr: // Deliberate fall-through.
  case Instruction::PtrToInt: {
    EVT SrcVT = TLI.getValueType(DL, I->getOperand(0)->getType());
    EVT DstVT = TLI.getValueType(DL, I->getType());
    if (DstVT.bitsGT(SrcVT))
      return selectCast(I, ISD::ZERO_EXTEND);
    if (DstVT.bitsLT(SrcVT))
      return selectCast(I, ISD::TRUNCATE);
    unsigned Reg = getRegForValue(I->getOperand(0));
    if (!Reg)
      return false;
    updateValueMap(I, Reg);
    return true;
  }

  case Instruction::ExtractValue:
    return selectExtractValue(I);

  case Instruction::PHI:
    llvm_unreachable("FastISel shouldn't visit PHI nodes!");

  default:
    // Unhandled instruction. Halt "fast" selection and bail.
    return false;
  }
}

FastISel::FastISel(FunctionLoweringInfo &FuncInfo,
                   const TargetLibraryInfo *LibInfo,
                   bool SkipTargetIndependentISel)
    : FuncInfo(FuncInfo), MF(FuncInfo.MF), MRI(FuncInfo.MF->getRegInfo()),
      MFI(FuncInfo.MF->getFrameInfo()), MCP(*FuncInfo.MF->getConstantPool()),
      TM(FuncInfo.MF->getTarget()), DL(MF->getDataLayout()),
      TII(*MF->getSubtarget().getInstrInfo()),
      TLI(*MF->getSubtarget().getTargetLowering()),
      TRI(*MF->getSubtarget().getRegisterInfo()), LibInfo(LibInfo),
      SkipTargetIndependentISel(SkipTargetIndependentISel) {}

FastISel::~FastISel() = default;

bool FastISel::fastLowerArguments() { return false; }

bool FastISel::fastLowerCall(CallLoweringInfo & /*CLI*/) { return false; }

bool FastISel::fastLowerIntrinsicCall(const IntrinsicInst * /*II*/) {
  return false;
}

unsigned FastISel::fastEmit_(MVT, MVT, unsigned) { return 0; }

unsigned FastISel::fastEmit_r(MVT, MVT, unsigned, unsigned /*Op0*/,
                              bool /*Op0IsKill*/) {
  return 0;
}

unsigned FastISel::fastEmit_rr(MVT, MVT, unsigned, unsigned /*Op0*/,
                               bool /*Op0IsKill*/, unsigned /*Op1*/,
                               bool /*Op1IsKill*/) {
  return 0;
}

unsigned FastISel::fastEmit_i(MVT, MVT, unsigned, uint64_t /*Imm*/) {
  return 0;
}

unsigned FastISel::fastEmit_f(MVT, MVT, unsigned,
                              const ConstantFP * /*FPImm*/) {
  return 0;
}

unsigned FastISel::fastEmit_ri(MVT, MVT, unsigned, unsigned /*Op0*/,
                               bool /*Op0IsKill*/, uint64_t /*Imm*/) {
  return 0;
}

/// This method is a wrapper of fastEmit_ri. It first tries to emit an
/// instruction with an immediate operand using fastEmit_ri.
/// If that fails, it materializes the immediate into a register and try
/// fastEmit_rr instead.
unsigned FastISel::fastEmit_ri_(MVT VT, unsigned Opcode, unsigned Op0,
                                bool Op0IsKill, uint64_t Imm, MVT ImmType) {
  // If this is a multiply by a power of two, emit this as a shift left.
  if (Opcode == ISD::MUL && isPowerOf2_64(Imm)) {
    Opcode = ISD::SHL;
    Imm = Log2_64(Imm);
  } else if (Opcode == ISD::UDIV && isPowerOf2_64(Imm)) {
    // div x, 8 -> srl x, 3
    Opcode = ISD::SRL;
    Imm = Log2_64(Imm);
  }

  // Horrible hack (to be removed), check to make sure shift amounts are
  // in-range.
  if ((Opcode == ISD::SHL || Opcode == ISD::SRA || Opcode == ISD::SRL) &&
      Imm >= VT.getSizeInBits())
    return 0;

  // First check if immediate type is legal. If not, we can't use the ri form.
  unsigned ResultReg = fastEmit_ri(VT, VT, Opcode, Op0, Op0IsKill, Imm);
  if (ResultReg)
    return ResultReg;
  unsigned MaterialReg = fastEmit_i(ImmType, ImmType, ISD::Constant, Imm);
  bool IsImmKill = true;
  if (!MaterialReg) {
    // This is a bit ugly/slow, but failing here means falling out of
    // fast-isel, which would be very slow.
    IntegerType *ITy =
        IntegerType::get(FuncInfo.Fn->getContext(), VT.getSizeInBits());
    MaterialReg = getRegForValue(ConstantInt::get(ITy, Imm));
    if (!MaterialReg)
      return 0;
    // FIXME: If the materialized register here has no uses yet then this
    // will be the first use and we should be able to mark it as killed.
    // However, the local value area for materialising constant expressions
    // grows down, not up, which means that any constant expressions we generate
    // later which also use 'Imm' could be after this instruction and therefore
    // after this kill.
    IsImmKill = false;
  }
  return fastEmit_rr(VT, VT, Opcode, Op0, Op0IsKill, MaterialReg, IsImmKill);
}

unsigned FastISel::createResultReg(const TargetRegisterClass *RC) {
  return MRI.createVirtualRegister(RC);
}

unsigned FastISel::constrainOperandRegClass(const MCInstrDesc &II, unsigned Op,
                                            unsigned OpNum) {
  if (TargetRegisterInfo::isVirtualRegister(Op)) {
    const TargetRegisterClass *RegClass =
        TII.getRegClass(II, OpNum, &TRI, *FuncInfo.MF);
    if (!MRI.constrainRegClass(Op, RegClass)) {
      // If it's not legal to COPY between the register classes, something
      // has gone very wrong before we got here.
      unsigned NewOp = createResultReg(RegClass);
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
              TII.get(TargetOpcode::COPY), NewOp).addReg(Op);
      return NewOp;
    }
  }
  return Op;
}

unsigned FastISel::fastEmitInst_(unsigned MachineInstOpcode,
                                 const TargetRegisterClass *RC) {
  unsigned ResultReg = createResultReg(RC);
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg);
  return ResultReg;
}

unsigned FastISel::fastEmitInst_r(unsigned MachineInstOpcode,
                                  const TargetRegisterClass *RC, unsigned Op0,
                                  bool Op0IsKill) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addReg(Op0, getKillRegState(Op0IsKill));
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addReg(Op0, getKillRegState(Op0IsKill));
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }

  return ResultReg;
}

unsigned FastISel::fastEmitInst_rr(unsigned MachineInstOpcode,
                                   const TargetRegisterClass *RC, unsigned Op0,
                                   bool Op0IsKill, unsigned Op1,
                                   bool Op1IsKill) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());
  Op1 = constrainOperandRegClass(II, Op1, II.getNumDefs() + 1);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addReg(Op1, getKillRegState(Op1IsKill));
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addReg(Op1, getKillRegState(Op1IsKill));
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_rrr(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC, unsigned Op0,
                                    bool Op0IsKill, unsigned Op1,
                                    bool Op1IsKill, unsigned Op2,
                                    bool Op2IsKill) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());
  Op1 = constrainOperandRegClass(II, Op1, II.getNumDefs() + 1);
  Op2 = constrainOperandRegClass(II, Op2, II.getNumDefs() + 2);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addReg(Op1, getKillRegState(Op1IsKill))
        .addReg(Op2, getKillRegState(Op2IsKill));
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addReg(Op1, getKillRegState(Op1IsKill))
        .addReg(Op2, getKillRegState(Op2IsKill));
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_ri(unsigned MachineInstOpcode,
                                   const TargetRegisterClass *RC, unsigned Op0,
                                   bool Op0IsKill, uint64_t Imm) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addImm(Imm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addImm(Imm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_rii(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC, unsigned Op0,
                                    bool Op0IsKill, uint64_t Imm1,
                                    uint64_t Imm2) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addImm(Imm1)
        .addImm(Imm2);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addImm(Imm1)
        .addImm(Imm2);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_f(unsigned MachineInstOpcode,
                                  const TargetRegisterClass *RC,
                                  const ConstantFP *FPImm) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addFPImm(FPImm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addFPImm(FPImm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_rri(unsigned MachineInstOpcode,
                                    const TargetRegisterClass *RC, unsigned Op0,
                                    bool Op0IsKill, unsigned Op1,
                                    bool Op1IsKill, uint64_t Imm) {
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  unsigned ResultReg = createResultReg(RC);
  Op0 = constrainOperandRegClass(II, Op0, II.getNumDefs());
  Op1 = constrainOperandRegClass(II, Op1, II.getNumDefs() + 1);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addReg(Op1, getKillRegState(Op1IsKill))
        .addImm(Imm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II)
        .addReg(Op0, getKillRegState(Op0IsKill))
        .addReg(Op1, getKillRegState(Op1IsKill))
        .addImm(Imm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_i(unsigned MachineInstOpcode,
                                  const TargetRegisterClass *RC, uint64_t Imm) {
  unsigned ResultReg = createResultReg(RC);
  const MCInstrDesc &II = TII.get(MachineInstOpcode);

  if (II.getNumDefs() >= 1)
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II, ResultReg)
        .addImm(Imm);
  else {
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, II).addImm(Imm);
    BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc,
            TII.get(TargetOpcode::COPY), ResultReg).addReg(II.ImplicitDefs[0]);
  }
  return ResultReg;
}

unsigned FastISel::fastEmitInst_extractsubreg(MVT RetVT, unsigned Op0,
                                              bool Op0IsKill, uint32_t Idx) {
  unsigned ResultReg = createResultReg(TLI.getRegClassFor(RetVT));
  assert(TargetRegisterInfo::isVirtualRegister(Op0) &&
         "Cannot yet extract from physregs");
  const TargetRegisterClass *RC = MRI.getRegClass(Op0);
  MRI.constrainRegClass(Op0, TRI.getSubClassWithSubReg(RC, Idx));
  BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, DbgLoc, TII.get(TargetOpcode::COPY),
          ResultReg).addReg(Op0, getKillRegState(Op0IsKill), Idx);
  return ResultReg;
}

/// Emit MachineInstrs to compute the value of Op with all but the least
/// significant bit set to zero.
unsigned FastISel::fastEmitZExtFromI1(MVT VT, unsigned Op0, bool Op0IsKill) {
  return fastEmit_ri(VT, VT, ISD::AND, Op0, Op0IsKill, 1);
}

/// HandlePHINodesInSuccessorBlocks - Handle PHI nodes in successor blocks.
/// Emit code to ensure constants are copied into registers when needed.
/// Remember the virtual registers that need to be added to the Machine PHI
/// nodes as input.  We cannot just directly add them, because expansion
/// might result in multiple MBB's for one BB.  As such, the start of the
/// BB might correspond to a different MBB than the end.
bool FastISel::handlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB) {
  const Instruction *TI = LLVMBB->getTerminator();

  SmallPtrSet<MachineBasicBlock *, 4> SuccsHandled;
  FuncInfo.OrigNumPHINodesToUpdate = FuncInfo.PHINodesToUpdate.size();

  // Check successor nodes' PHI nodes that expect a constant to be available
  // from this block.
  for (unsigned succ = 0, e = TI->getNumSuccessors(); succ != e; ++succ) {
    const BasicBlock *SuccBB = TI->getSuccessor(succ);
    if (!isa<PHINode>(SuccBB->begin()))
      continue;
    MachineBasicBlock *SuccMBB = FuncInfo.MBBMap[SuccBB];

    // If this terminator has multiple identical successors (common for
    // switches), only handle each succ once.
    if (!SuccsHandled.insert(SuccMBB).second)
      continue;

    MachineBasicBlock::iterator MBBI = SuccMBB->begin();

    // At this point we know that there is a 1-1 correspondence between LLVM PHI
    // nodes and Machine PHI nodes, but the incoming operands have not been
    // emitted yet.
    for (const PHINode &PN : SuccBB->phis()) {
      // Ignore dead phi's.
      if (PN.use_empty())
        continue;

      // Only handle legal types. Two interesting things to note here. First,
      // by bailing out early, we may leave behind some dead instructions,
      // since SelectionDAG's HandlePHINodesInSuccessorBlocks will insert its
      // own moves. Second, this check is necessary because FastISel doesn't
      // use CreateRegs to create registers, so it always creates
      // exactly one register for each non-void instruction.
      EVT VT = TLI.getValueType(DL, PN.getType(), /*AllowUnknown=*/true);
      if (VT == MVT::Other || !TLI.isTypeLegal(VT)) {
        // Handle integer promotions, though, because they're common and easy.
        if (!(VT == MVT::i1 || VT == MVT::i8 || VT == MVT::i16)) {
          FuncInfo.PHINodesToUpdate.resize(FuncInfo.OrigNumPHINodesToUpdate);
          return false;
        }
      }

      const Value *PHIOp = PN.getIncomingValueForBlock(LLVMBB);

      // Set the DebugLoc for the copy. Prefer the location of the operand
      // if there is one; use the location of the PHI otherwise.
      DbgLoc = PN.getDebugLoc();
      if (const auto *Inst = dyn_cast<Instruction>(PHIOp))
        DbgLoc = Inst->getDebugLoc();

      unsigned Reg = getRegForValue(PHIOp);
      if (!Reg) {
        FuncInfo.PHINodesToUpdate.resize(FuncInfo.OrigNumPHINodesToUpdate);
        return false;
      }
      FuncInfo.PHINodesToUpdate.push_back(std::make_pair(&*MBBI++, Reg));
      DbgLoc = DebugLoc();
    }
  }

  return true;
}

bool FastISel::tryToFoldLoad(const LoadInst *LI, const Instruction *FoldInst) {
  assert(LI->hasOneUse() &&
         "tryToFoldLoad expected a LoadInst with a single use");
  // We know that the load has a single use, but don't know what it is.  If it
  // isn't one of the folded instructions, then we can't succeed here.  Handle
  // this by scanning the single-use users of the load until we get to FoldInst.
  unsigned MaxUsers = 6; // Don't scan down huge single-use chains of instrs.

  const Instruction *TheUser = LI->user_back();
  while (TheUser != FoldInst && // Scan up until we find FoldInst.
         // Stay in the right block.
         TheUser->getParent() == FoldInst->getParent() &&
         --MaxUsers) { // Don't scan too far.
    // If there are multiple or no uses of this instruction, then bail out.
    if (!TheUser->hasOneUse())
      return false;

    TheUser = TheUser->user_back();
  }

  // If we didn't find the fold instruction, then we failed to collapse the
  // sequence.
  if (TheUser != FoldInst)
    return false;

  // Don't try to fold volatile loads.  Target has to deal with alignment
  // constraints.
  if (LI->isVolatile())
    return false;

  // Figure out which vreg this is going into.  If there is no assigned vreg yet
  // then there actually was no reference to it.  Perhaps the load is referenced
  // by a dead instruction.
  unsigned LoadReg = getRegForValue(LI);
  if (!LoadReg)
    return false;

  // We can't fold if this vreg has no uses or more than one use.  Multiple uses
  // may mean that the instruction got lowered to multiple MIs, or the use of
  // the loaded value ended up being multiple operands of the result.
  if (!MRI.hasOneUse(LoadReg))
    return false;

  MachineRegisterInfo::reg_iterator RI = MRI.reg_begin(LoadReg);
  MachineInstr *User = RI->getParent();

  // Set the insertion point properly.  Folding the load can cause generation of
  // other random instructions (like sign extends) for addressing modes; make
  // sure they get inserted in a logical place before the new instruction.
  FuncInfo.InsertPt = User;
  FuncInfo.MBB = User->getParent();

  // Ask the target to try folding the load.
  return tryToFoldLoadIntoMI(User, RI.getOperandNo(), LI);
}

bool FastISel::canFoldAddIntoGEP(const User *GEP, const Value *Add) {
  // Must be an add.
  if (!isa<AddOperator>(Add))
    return false;
  // Type size needs to match.
  if (DL.getTypeSizeInBits(GEP->getType()) !=
      DL.getTypeSizeInBits(Add->getType()))
    return false;
  // Must be in the same basic block.
  if (isa<Instruction>(Add) &&
      FuncInfo.MBBMap[cast<Instruction>(Add)->getParent()] != FuncInfo.MBB)
    return false;
  // Must have a constant operand.
  return isa<ConstantInt>(cast<AddOperator>(Add)->getOperand(1));
}

MachineMemOperand *
FastISel::createMachineMemOperandFor(const Instruction *I) const {
  const Value *Ptr;
  Type *ValTy;
  unsigned Alignment;
  MachineMemOperand::Flags Flags;
  bool IsVolatile;

  if (const auto *LI = dyn_cast<LoadInst>(I)) {
    Alignment = LI->getAlignment();
    IsVolatile = LI->isVolatile();
    Flags = MachineMemOperand::MOLoad;
    Ptr = LI->getPointerOperand();
    ValTy = LI->getType();
  } else if (const auto *SI = dyn_cast<StoreInst>(I)) {
    Alignment = SI->getAlignment();
    IsVolatile = SI->isVolatile();
    Flags = MachineMemOperand::MOStore;
    Ptr = SI->getPointerOperand();
    ValTy = SI->getValueOperand()->getType();
  } else
    return nullptr;

  bool IsNonTemporal = I->getMetadata(LLVMContext::MD_nontemporal) != nullptr;
  bool IsInvariant = I->getMetadata(LLVMContext::MD_invariant_load) != nullptr;
  bool IsDereferenceable =
      I->getMetadata(LLVMContext::MD_dereferenceable) != nullptr;
  const MDNode *Ranges = I->getMetadata(LLVMContext::MD_range);

  AAMDNodes AAInfo;
  I->getAAMetadata(AAInfo);

  if (Alignment == 0) // Ensure that codegen never sees alignment 0.
    Alignment = DL.getABITypeAlignment(ValTy);

  unsigned Size = DL.getTypeStoreSize(ValTy);

  if (IsVolatile)
    Flags |= MachineMemOperand::MOVolatile;
  if (IsNonTemporal)
    Flags |= MachineMemOperand::MONonTemporal;
  if (IsDereferenceable)
    Flags |= MachineMemOperand::MODereferenceable;
  if (IsInvariant)
    Flags |= MachineMemOperand::MOInvariant;

  return FuncInfo.MF->getMachineMemOperand(MachinePointerInfo(Ptr), Flags, Size,
                                           Alignment, AAInfo, Ranges);
}

CmpInst::Predicate FastISel::optimizeCmpPredicate(const CmpInst *CI) const {
  // If both operands are the same, then try to optimize or fold the cmp.
  CmpInst::Predicate Predicate = CI->getPredicate();
  if (CI->getOperand(0) != CI->getOperand(1))
    return Predicate;

  switch (Predicate) {
  default: llvm_unreachable("Invalid predicate!");
  case CmpInst::FCMP_FALSE: Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_OEQ:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_OGT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_OGE:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_OLT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_OLE:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_ONE:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::FCMP_ORD:   Predicate = CmpInst::FCMP_ORD;   break;
  case CmpInst::FCMP_UNO:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_UEQ:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::FCMP_UGT:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_UGE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::FCMP_ULT:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_ULE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::FCMP_UNE:   Predicate = CmpInst::FCMP_UNO;   break;
  case CmpInst::FCMP_TRUE:  Predicate = CmpInst::FCMP_TRUE;  break;

  case CmpInst::ICMP_EQ:    Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_NE:    Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_UGT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_UGE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_ULT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_ULE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_SGT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_SGE:   Predicate = CmpInst::FCMP_TRUE;  break;
  case CmpInst::ICMP_SLT:   Predicate = CmpInst::FCMP_FALSE; break;
  case CmpInst::ICMP_SLE:   Predicate = CmpInst::FCMP_TRUE;  break;
  }

  return Predicate;
}
