//===- llvm/CodeGen/GlobalISel/InstructionSelectorImpl.h --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API for the instruction selector.
/// This class is responsible for selecting machine instructions.
/// It's implemented by the target. It's used by the InstructionSelect pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTORIMPL_H
#define LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTORIMPL_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/RegisterBankInfo.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

/// GlobalISel PatFrag Predicates
enum {
  GIPFP_I64_Invalid = 0,
  GIPFP_APInt_Invalid = 0,
  GIPFP_APFloat_Invalid = 0,
  GIPFP_MI_Invalid = 0,
};

template <class TgtInstructionSelector, class PredicateBitset,
          class ComplexMatcherMemFn, class CustomRendererFn>
bool InstructionSelector::executeMatchTable(
    TgtInstructionSelector &ISel, NewMIVector &OutMIs, MatcherState &State,
    const ISelInfoTy<PredicateBitset, ComplexMatcherMemFn, CustomRendererFn>
        &ISelInfo,
    const int64_t *MatchTable, const TargetInstrInfo &TII,
    MachineRegisterInfo &MRI, const TargetRegisterInfo &TRI,
    const RegisterBankInfo &RBI, const PredicateBitset &AvailableFeatures,
    CodeGenCoverage &CoverageInfo) const {

  uint64_t CurrentIdx = 0;
  SmallVector<uint64_t, 4> OnFailResumeAt;

  enum RejectAction { RejectAndGiveUp, RejectAndResume };
  auto handleReject = [&]() -> RejectAction {
    DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                    dbgs() << CurrentIdx << ": Rejected\n");
    if (OnFailResumeAt.empty())
      return RejectAndGiveUp;
    CurrentIdx = OnFailResumeAt.pop_back_val();
    DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                    dbgs() << CurrentIdx << ": Resume at " << CurrentIdx << " ("
                           << OnFailResumeAt.size() << " try-blocks remain)\n");
    return RejectAndResume;
  };

  while (true) {
    assert(CurrentIdx != ~0u && "Invalid MatchTable index");
    int64_t MatcherOpcode = MatchTable[CurrentIdx++];
    switch (MatcherOpcode) {
    case GIM_Try: {
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": Begin try-block\n");
      OnFailResumeAt.push_back(MatchTable[CurrentIdx++]);
      break;
    }

    case GIM_RecordInsn: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];

      // As an optimisation we require that MIs[0] is always the root. Refuse
      // any attempt to modify it.
      assert(NewInsnID != 0 && "Refusing to modify MIs[0]");

      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isReg()) {
        DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                        dbgs() << CurrentIdx << ": Not a register\n");
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }
      if (TRI.isPhysicalRegister(MO.getReg())) {
        DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                        dbgs() << CurrentIdx << ": Is a physical register\n");
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }

      MachineInstr *NewMI = MRI.getVRegDef(MO.getReg());
      if ((size_t)NewInsnID < State.MIs.size())
        State.MIs[NewInsnID] = NewMI;
      else {
        assert((size_t)NewInsnID == State.MIs.size() &&
               "Expected to store MIs in order");
        State.MIs.push_back(NewMI);
      }
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": MIs[" << NewInsnID
                             << "] = GIM_RecordInsn(" << InsnID << ", " << OpIdx
                             << ")\n");
      break;
    }

    case GIM_CheckFeatures: {
      int64_t ExpectedBitsetID = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx
                             << ": GIM_CheckFeatures(ExpectedBitsetID="
                             << ExpectedBitsetID << ")\n");
      if ((AvailableFeatures & ISelInfo.FeatureBitsets[ExpectedBitsetID]) !=
          ISelInfo.FeatureBitsets[ExpectedBitsetID]) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckOpcode: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Expected = MatchTable[CurrentIdx++];

      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      unsigned Opcode = State.MIs[InsnID]->getOpcode();

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckOpcode(MIs[" << InsnID
                             << "], ExpectedOpcode=" << Expected
                             << ") // Got=" << Opcode << "\n");
      if (Opcode != Expected) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_SwitchOpcode: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t LowerBound = MatchTable[CurrentIdx++];
      int64_t UpperBound = MatchTable[CurrentIdx++];
      int64_t Default = MatchTable[CurrentIdx++];

      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      const int64_t Opcode = State.MIs[InsnID]->getOpcode();

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(), {
        dbgs() << CurrentIdx << ": GIM_SwitchOpcode(MIs[" << InsnID << "], ["
               << LowerBound << ", " << UpperBound << "), Default=" << Default
               << ", JumpTable...) // Got=" << Opcode << "\n";
      });
      if (Opcode < LowerBound || UpperBound <= Opcode) {
        CurrentIdx = Default;
        break;
      }
      CurrentIdx = MatchTable[CurrentIdx + (Opcode - LowerBound)];
      if (!CurrentIdx) {
        CurrentIdx = Default;
	break;
      }
      OnFailResumeAt.push_back(Default);
      break;
    }

    case GIM_SwitchType: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t LowerBound = MatchTable[CurrentIdx++];
      int64_t UpperBound = MatchTable[CurrentIdx++];
      int64_t Default = MatchTable[CurrentIdx++];

      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(), {
        dbgs() << CurrentIdx << ": GIM_SwitchType(MIs[" << InsnID
               << "]->getOperand(" << OpIdx << "), [" << LowerBound << ", "
               << UpperBound << "), Default=" << Default
               << ", JumpTable...) // Got=";
        if (!MO.isReg())
          dbgs() << "Not a VReg\n";
        else
          dbgs() << MRI.getType(MO.getReg()) << "\n";
      });
      if (!MO.isReg()) {
        CurrentIdx = Default;
        break;
      }
      const LLT Ty = MRI.getType(MO.getReg());
      const auto TyI = ISelInfo.TypeIDMap.find(Ty);
      if (TyI == ISelInfo.TypeIDMap.end()) {
        CurrentIdx = Default;
        break;
      }
      const int64_t TypeID = TyI->second;
      if (TypeID < LowerBound || UpperBound <= TypeID) {
        CurrentIdx = Default;
        break;
      }
      CurrentIdx = MatchTable[CurrentIdx + (TypeID - LowerBound)];
      if (!CurrentIdx) {
        CurrentIdx = Default;
        break;
      }
      OnFailResumeAt.push_back(Default);
      break;
    }

    case GIM_CheckNumOperands: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Expected = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckNumOperands(MIs["
                             << InsnID << "], Expected=" << Expected << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (State.MIs[InsnID]->getNumOperands() != Expected) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }
    case GIM_CheckI64ImmPredicate: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Predicate = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs()
                          << CurrentIdx << ": GIM_CheckI64ImmPredicate(MIs["
                          << InsnID << "], Predicate=" << Predicate << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      assert(State.MIs[InsnID]->getOpcode() == TargetOpcode::G_CONSTANT &&
             "Expected G_CONSTANT");
      assert(Predicate > GIPFP_I64_Invalid && "Expected a valid predicate");
      int64_t Value = 0;
      if (State.MIs[InsnID]->getOperand(1).isCImm())
        Value = State.MIs[InsnID]->getOperand(1).getCImm()->getSExtValue();
      else if (State.MIs[InsnID]->getOperand(1).isImm())
        Value = State.MIs[InsnID]->getOperand(1).getImm();
      else
        llvm_unreachable("Expected Imm or CImm operand");

      if (!testImmPredicate_I64(Predicate, Value))
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }
    case GIM_CheckAPIntImmPredicate: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Predicate = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs()
                          << CurrentIdx << ": GIM_CheckAPIntImmPredicate(MIs["
                          << InsnID << "], Predicate=" << Predicate << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      assert(State.MIs[InsnID]->getOpcode() == TargetOpcode::G_CONSTANT &&
             "Expected G_CONSTANT");
      assert(Predicate > GIPFP_APInt_Invalid && "Expected a valid predicate");
      APInt Value;
      if (State.MIs[InsnID]->getOperand(1).isCImm())
        Value = State.MIs[InsnID]->getOperand(1).getCImm()->getValue();
      else
        llvm_unreachable("Expected Imm or CImm operand");

      if (!testImmPredicate_APInt(Predicate, Value))
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }
    case GIM_CheckAPFloatImmPredicate: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Predicate = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs()
                          << CurrentIdx << ": GIM_CheckAPFloatImmPredicate(MIs["
                          << InsnID << "], Predicate=" << Predicate << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      assert(State.MIs[InsnID]->getOpcode() == TargetOpcode::G_FCONSTANT &&
             "Expected G_FCONSTANT");
      assert(State.MIs[InsnID]->getOperand(1).isFPImm() && "Expected FPImm operand");
      assert(Predicate > GIPFP_APFloat_Invalid && "Expected a valid predicate");
      APFloat Value = State.MIs[InsnID]->getOperand(1).getFPImm()->getValueAPF();

      if (!testImmPredicate_APFloat(Predicate, Value))
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }
    case GIM_CheckCxxInsnPredicate: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Predicate = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs()
                          << CurrentIdx << ": GIM_CheckCxxPredicate(MIs["
                          << InsnID << "], Predicate=" << Predicate << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      assert(Predicate > GIPFP_MI_Invalid && "Expected a valid predicate");

      if (!testMIPredicate_MI(Predicate, *State.MIs[InsnID]))
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }
    case GIM_CheckAtomicOrdering: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      AtomicOrdering Ordering = (AtomicOrdering)MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckAtomicOrdering(MIs["
                             << InsnID << "], " << (uint64_t)Ordering << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!State.MIs[InsnID]->hasOneMemOperand())
        if (handleReject() == RejectAndGiveUp)
          return false;

      for (const auto &MMO : State.MIs[InsnID]->memoperands())
        if (MMO->getOrdering() != Ordering)
          if (handleReject() == RejectAndGiveUp)
            return false;
      break;
    }
    case GIM_CheckAtomicOrderingOrStrongerThan: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      AtomicOrdering Ordering = (AtomicOrdering)MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx
                             << ": GIM_CheckAtomicOrderingOrStrongerThan(MIs["
                             << InsnID << "], " << (uint64_t)Ordering << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!State.MIs[InsnID]->hasOneMemOperand())
        if (handleReject() == RejectAndGiveUp)
          return false;

      for (const auto &MMO : State.MIs[InsnID]->memoperands())
        if (!isAtLeastOrStrongerThan(MMO->getOrdering(), Ordering))
          if (handleReject() == RejectAndGiveUp)
            return false;
      break;
    }
    case GIM_CheckAtomicOrderingWeakerThan: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      AtomicOrdering Ordering = (AtomicOrdering)MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx
                             << ": GIM_CheckAtomicOrderingWeakerThan(MIs["
                             << InsnID << "], " << (uint64_t)Ordering << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!State.MIs[InsnID]->hasOneMemOperand())
        if (handleReject() == RejectAndGiveUp)
          return false;

      for (const auto &MMO : State.MIs[InsnID]->memoperands())
        if (!isStrongerThan(Ordering, MMO->getOrdering()))
          if (handleReject() == RejectAndGiveUp)
            return false;
      break;
    }
    case GIM_CheckMemorySizeEqualTo: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t MMOIdx = MatchTable[CurrentIdx++];
      uint64_t Size = MatchTable[CurrentIdx++];

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx
                             << ": GIM_CheckMemorySizeEqual(MIs[" << InsnID
                             << "]->memoperands() + " << MMOIdx
                             << ", Size=" << Size << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");

      if (State.MIs[InsnID]->getNumMemOperands() <= MMOIdx) {
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }

      MachineMemOperand *MMO = *(State.MIs[InsnID]->memoperands_begin() + MMOIdx);

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << MMO->getSize() << " bytes vs " << Size
                             << " bytes\n");
      if (MMO->getSize() != Size)
        if (handleReject() == RejectAndGiveUp)
          return false;

      break;
    }
    case GIM_CheckMemorySizeEqualToLLT:
    case GIM_CheckMemorySizeLessThanLLT:
    case GIM_CheckMemorySizeGreaterThanLLT: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t MMOIdx = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];

      DEBUG_WITH_TYPE(
          TgtInstructionSelector::getName(),
          dbgs() << CurrentIdx << ": GIM_CheckMemorySize"
                 << (MatcherOpcode == GIM_CheckMemorySizeEqualToLLT
                         ? "EqualTo"
                         : MatcherOpcode == GIM_CheckMemorySizeGreaterThanLLT
                               ? "GreaterThan"
                               : "LessThan")
                 << "LLT(MIs[" << InsnID << "]->memoperands() + " << MMOIdx
                 << ", OpIdx=" << OpIdx << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");

      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isReg()) {
        DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                        dbgs() << CurrentIdx << ": Not a register\n");
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }

      if (State.MIs[InsnID]->getNumMemOperands() <= MMOIdx) {
        if (handleReject() == RejectAndGiveUp)
          return false;
        break;
      }

      MachineMemOperand *MMO = *(State.MIs[InsnID]->memoperands_begin() + MMOIdx);

      unsigned Size = MRI.getType(MO.getReg()).getSizeInBits();
      if (MatcherOpcode == GIM_CheckMemorySizeEqualToLLT &&
          MMO->getSize() * 8 != Size) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      } else if (MatcherOpcode == GIM_CheckMemorySizeLessThanLLT &&
                 MMO->getSize() * 8 >= Size) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      } else if (MatcherOpcode == GIM_CheckMemorySizeGreaterThanLLT &&
                 MMO->getSize() * 8 <= Size)
        if (handleReject() == RejectAndGiveUp)
          return false;

      break;
    }
    case GIM_CheckType: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t TypeID = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckType(MIs[" << InsnID
                             << "]->getOperand(" << OpIdx
                             << "), TypeID=" << TypeID << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isReg() ||
          MRI.getType(MO.getReg()) != ISelInfo.TypeObjects[TypeID]) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }
    case GIM_CheckPointerToAny: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t SizeInBits = MatchTable[CurrentIdx++];

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckPointerToAny(MIs["
                             << InsnID << "]->getOperand(" << OpIdx
                             << "), SizeInBits=" << SizeInBits << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      // iPTR must be looked up in the target.
      if (SizeInBits == 0) {
        MachineFunction *MF = State.MIs[InsnID]->getParent()->getParent();
        SizeInBits = MF->getDataLayout().getPointerSizeInBits(0);
      }

      assert(SizeInBits != 0 && "Pointer size must be known");

      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (MO.isReg()) {
        const LLT &Ty = MRI.getType(MO.getReg());
        if (!Ty.isPointer() || Ty.getSizeInBits() != SizeInBits)
          if (handleReject() == RejectAndGiveUp)
            return false;
      } else if (handleReject() == RejectAndGiveUp)
        return false;

      break;
    }
    case GIM_CheckRegBankForClass: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t RCEnum = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckRegBankForClass(MIs["
                             << InsnID << "]->getOperand(" << OpIdx
                             << "), RCEnum=" << RCEnum << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isReg() ||
          &RBI.getRegBankFromRegClass(*TRI.getRegClass(RCEnum)) !=
              RBI.getRegBank(MO.getReg(), MRI, TRI)) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckComplexPattern: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t RendererID = MatchTable[CurrentIdx++];
      int64_t ComplexPredicateID = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": State.Renderers[" << RendererID
                             << "] = GIM_CheckComplexPattern(MIs[" << InsnID
                             << "]->getOperand(" << OpIdx
                             << "), ComplexPredicateID=" << ComplexPredicateID
                             << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      // FIXME: Use std::invoke() when it's available.
      ComplexRendererFns Renderer =
          (ISel.*ISelInfo.ComplexPredicates[ComplexPredicateID])(
              State.MIs[InsnID]->getOperand(OpIdx));
      if (Renderer.hasValue())
        State.Renderers[RendererID] = Renderer.getValue();
      else
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }

    case GIM_CheckConstantInt: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t Value = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckConstantInt(MIs["
                             << InsnID << "]->getOperand(" << OpIdx
                             << "), Value=" << Value << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (MO.isReg()) {
        // isOperandImmEqual() will sign-extend to 64-bits, so should we.
        LLT Ty = MRI.getType(MO.getReg());
        Value = SignExtend64(Value, Ty.getSizeInBits());

        if (!isOperandImmEqual(MO, Value, MRI)) {
          if (handleReject() == RejectAndGiveUp)
            return false;
        }
      } else if (handleReject() == RejectAndGiveUp)
        return false;

      break;
    }

    case GIM_CheckLiteralInt: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t Value = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckLiteralInt(MIs["
                             << InsnID << "]->getOperand(" << OpIdx
                             << "), Value=" << Value << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isCImm() || !MO.getCImm()->equalsInt(Value)) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckIntrinsicID: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t Value = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckIntrinsicID(MIs["
                             << InsnID << "]->getOperand(" << OpIdx
                             << "), Value=" << Value << ")\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      MachineOperand &MO = State.MIs[InsnID]->getOperand(OpIdx);
      if (!MO.isIntrinsicID() || MO.getIntrinsicID() != Value)
        if (handleReject() == RejectAndGiveUp)
          return false;
      break;
    }

    case GIM_CheckIsMBB: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckIsMBB(MIs[" << InsnID
                             << "]->getOperand(" << OpIdx << "))\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!State.MIs[InsnID]->getOperand(OpIdx).isMBB()) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }

    case GIM_CheckIsSafeToFold: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckIsSafeToFold(MIs["
                             << InsnID << "])\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      if (!isObviouslySafeToFold(*State.MIs[InsnID], *State.MIs[0])) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }
    case GIM_CheckIsSameOperand: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t OtherInsnID = MatchTable[CurrentIdx++];
      int64_t OtherOpIdx = MatchTable[CurrentIdx++];
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_CheckIsSameOperand(MIs["
                             << InsnID << "][" << OpIdx << "], MIs["
                             << OtherInsnID << "][" << OtherOpIdx << "])\n");
      assert(State.MIs[InsnID] != nullptr && "Used insn before defined");
      assert(State.MIs[OtherInsnID] != nullptr && "Used insn before defined");
      if (!State.MIs[InsnID]->getOperand(OpIdx).isIdenticalTo(
              State.MIs[OtherInsnID]->getOperand(OtherOpIdx))) {
        if (handleReject() == RejectAndGiveUp)
          return false;
      }
      break;
    }
    case GIM_Reject:
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIM_Reject\n");
      if (handleReject() == RejectAndGiveUp)
        return false;
      break;

    case GIR_MutateOpcode: {
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      uint64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t NewOpcode = MatchTable[CurrentIdx++];
      if (NewInsnID >= OutMIs.size())
        OutMIs.resize(NewInsnID + 1);

      OutMIs[NewInsnID] = MachineInstrBuilder(*State.MIs[OldInsnID]->getMF(),
                                              State.MIs[OldInsnID]);
      OutMIs[NewInsnID]->setDesc(TII.get(NewOpcode));
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_MutateOpcode(OutMIs["
                             << NewInsnID << "], MIs[" << OldInsnID << "], "
                             << NewOpcode << ")\n");
      break;
    }

    case GIR_BuildMI: {
      uint64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t Opcode = MatchTable[CurrentIdx++];
      if (NewInsnID >= OutMIs.size())
        OutMIs.resize(NewInsnID + 1);

      OutMIs[NewInsnID] = BuildMI(*State.MIs[0]->getParent(), State.MIs[0],
                                  State.MIs[0]->getDebugLoc(), TII.get(Opcode));
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_BuildMI(OutMIs["
                             << NewInsnID << "], " << Opcode << ")\n");
      break;
    }

    case GIR_Copy: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      OutMIs[NewInsnID].add(State.MIs[OldInsnID]->getOperand(OpIdx));
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs()
                          << CurrentIdx << ": GIR_Copy(OutMIs[" << NewInsnID
                          << "], MIs[" << OldInsnID << "], " << OpIdx << ")\n");
      break;
    }

    case GIR_CopyOrAddZeroReg: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t ZeroReg = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      MachineOperand &MO = State.MIs[OldInsnID]->getOperand(OpIdx);
      if (isOperandImmEqual(MO, 0, MRI))
        OutMIs[NewInsnID].addReg(ZeroReg);
      else
        OutMIs[NewInsnID].add(MO);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_CopyOrAddZeroReg(OutMIs["
                             << NewInsnID << "], MIs[" << OldInsnID << "], "
                             << OpIdx << ", " << ZeroReg << ")\n");
      break;
    }

    case GIR_CopySubReg: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t SubRegIdx = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      OutMIs[NewInsnID].addReg(State.MIs[OldInsnID]->getOperand(OpIdx).getReg(),
                               0, SubRegIdx);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_CopySubReg(OutMIs["
                             << NewInsnID << "], MIs[" << OldInsnID << "], "
                             << OpIdx << ", " << SubRegIdx << ")\n");
      break;
    }

    case GIR_AddImplicitDef: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RegNum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addDef(RegNum, RegState::Implicit);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_AddImplicitDef(OutMIs["
                             << InsnID << "], " << RegNum << ")\n");
      break;
    }

    case GIR_AddImplicitUse: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RegNum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addUse(RegNum, RegState::Implicit);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_AddImplicitUse(OutMIs["
                             << InsnID << "], " << RegNum << ")\n");
      break;
    }

    case GIR_AddRegister: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RegNum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addReg(RegNum);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_AddRegister(OutMIs["
                             << InsnID << "], " << RegNum << ")\n");
      break;
    }

    case GIR_AddTempRegister: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t TempRegID = MatchTable[CurrentIdx++];
      uint64_t TempRegFlags = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addReg(State.TempRegisters[TempRegID], TempRegFlags);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_AddTempRegister(OutMIs["
                             << InsnID << "], TempRegisters[" << TempRegID
                             << "], " << TempRegFlags << ")\n");
      break;
    }

    case GIR_AddImm: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t Imm = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      OutMIs[InsnID].addImm(Imm);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_AddImm(OutMIs[" << InsnID
                             << "], " << Imm << ")\n");
      break;
    }

    case GIR_ComplexRenderer: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RendererID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      for (const auto &RenderOpFn : State.Renderers[RendererID])
        RenderOpFn(OutMIs[InsnID]);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_ComplexRenderer(OutMIs["
                             << InsnID << "], " << RendererID << ")\n");
      break;
    }
    case GIR_ComplexSubOperandRenderer: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t RendererID = MatchTable[CurrentIdx++];
      int64_t RenderOpID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      State.Renderers[RendererID][RenderOpID](OutMIs[InsnID]);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx
                             << ": GIR_ComplexSubOperandRenderer(OutMIs["
                             << InsnID << "], " << RendererID << ", "
                             << RenderOpID << ")\n");
      break;
    }

    case GIR_CopyConstantAsSImm: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      assert(State.MIs[OldInsnID]->getOpcode() == TargetOpcode::G_CONSTANT && "Expected G_CONSTANT");
      if (State.MIs[OldInsnID]->getOperand(1).isCImm()) {
        OutMIs[NewInsnID].addImm(
            State.MIs[OldInsnID]->getOperand(1).getCImm()->getSExtValue());
      } else if (State.MIs[OldInsnID]->getOperand(1).isImm())
        OutMIs[NewInsnID].add(State.MIs[OldInsnID]->getOperand(1));
      else
        llvm_unreachable("Expected Imm or CImm operand");
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_CopyConstantAsSImm(OutMIs["
                             << NewInsnID << "], MIs[" << OldInsnID << "])\n");
      break;
    }

    // TODO: Needs a test case once we have a pattern that uses this.
    case GIR_CopyFConstantAsFPImm: {
      int64_t NewInsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[NewInsnID] && "Attempted to add to undefined instruction");
      assert(State.MIs[OldInsnID]->getOpcode() == TargetOpcode::G_FCONSTANT && "Expected G_FCONSTANT");
      if (State.MIs[OldInsnID]->getOperand(1).isFPImm())
        OutMIs[NewInsnID].addFPImm(
            State.MIs[OldInsnID]->getOperand(1).getFPImm());
      else
        llvm_unreachable("Expected FPImm operand");
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_CopyFPConstantAsFPImm(OutMIs["
                             << NewInsnID << "], MIs[" << OldInsnID << "])\n");
      break;
    }

    case GIR_CustomRenderer: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OldInsnID = MatchTable[CurrentIdx++];
      int64_t RendererFnID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_CustomRenderer(OutMIs["
                             << InsnID << "], MIs[" << OldInsnID << "], "
                             << RendererFnID << ")\n");
      (ISel.*ISelInfo.CustomRenderers[RendererFnID])(OutMIs[InsnID],
                                                     *State.MIs[OldInsnID]);
      break;
    }
    case GIR_ConstrainOperandRC: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      int64_t OpIdx = MatchTable[CurrentIdx++];
      int64_t RCEnum = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      constrainOperandRegToRegClass(*OutMIs[InsnID].getInstr(), OpIdx,
                                    *TRI.getRegClass(RCEnum), TII, TRI, RBI);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_ConstrainOperandRC(OutMIs["
                             << InsnID << "], " << OpIdx << ", " << RCEnum
                             << ")\n");
      break;
    }

    case GIR_ConstrainSelectedInstOperands: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");
      constrainSelectedInstRegOperands(*OutMIs[InsnID].getInstr(), TII, TRI,
                                       RBI);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx
                             << ": GIR_ConstrainSelectedInstOperands(OutMIs["
                             << InsnID << "])\n");
      break;
    }

    case GIR_MergeMemOperands: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      assert(OutMIs[InsnID] && "Attempted to add to undefined instruction");

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_MergeMemOperands(OutMIs["
                             << InsnID << "]");
      int64_t MergeInsnID = GIU_MergeMemOperands_EndOfList;
      while ((MergeInsnID = MatchTable[CurrentIdx++]) !=
             GIU_MergeMemOperands_EndOfList) {
        DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                        dbgs() << ", MIs[" << MergeInsnID << "]");
        for (const auto &MMO : State.MIs[MergeInsnID]->memoperands())
          OutMIs[InsnID].addMemOperand(MMO);
      }
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(), dbgs() << ")\n");
      break;
    }

    case GIR_EraseFromParent: {
      int64_t InsnID = MatchTable[CurrentIdx++];
      assert(State.MIs[InsnID] &&
             "Attempted to erase an undefined instruction");
      State.MIs[InsnID]->eraseFromParent();
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_EraseFromParent(MIs["
                             << InsnID << "])\n");
      break;
    }

    case GIR_MakeTempReg: {
      int64_t TempRegID = MatchTable[CurrentIdx++];
      int64_t TypeID = MatchTable[CurrentIdx++];

      State.TempRegisters[TempRegID] =
          MRI.createGenericVirtualRegister(ISelInfo.TypeObjects[TypeID]);
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": TempRegs[" << TempRegID
                             << "] = GIR_MakeTempReg(" << TypeID << ")\n");
      break;
    }

    case GIR_Coverage: {
      int64_t RuleID = MatchTable[CurrentIdx++];
      CoverageInfo.setCovered(RuleID);

      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs()
                          << CurrentIdx << ": GIR_Coverage(" << RuleID << ")");
      break;
    }

    case GIR_Done:
      DEBUG_WITH_TYPE(TgtInstructionSelector::getName(),
                      dbgs() << CurrentIdx << ": GIR_Done\n");
      return true;

    default:
      llvm_unreachable("Unexpected command");
    }
  }
}

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECTORIMPL_H
