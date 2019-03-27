//===- lib/CodeGen/GlobalISel/LegalizerInfo.cpp - Legalizer ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implement an interface to specify and query how an illegal operation on a
// given type should be expanded.
//
// Issues to be resolved:
//   + Make it fast.
//   + Support weird types like i3, <7 x i3>, ...
//   + Operations with more than one type (ICMP, CMPXCHG, intrinsics, ...)
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LowLevelTypeImpl.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <map>

using namespace llvm;
using namespace LegalizeActions;

#define DEBUG_TYPE "legalizer-info"

cl::opt<bool> llvm::DisableGISelLegalityCheck(
    "disable-gisel-legality-check",
    cl::desc("Don't verify that MIR is fully legal between GlobalISel passes"),
    cl::Hidden);

raw_ostream &LegalityQuery::print(raw_ostream &OS) const {
  OS << Opcode << ", Tys={";
  for (const auto &Type : Types) {
    OS << Type << ", ";
  }
  OS << "}, Opcode=";

  OS << Opcode << ", MMOs={";
  for (const auto &MMODescr : MMODescrs) {
    OS << MMODescr.SizeInBits << ", ";
  }
  OS << "}";

  return OS;
}

LegalizeActionStep LegalizeRuleSet::apply(const LegalityQuery &Query) const {
  LLVM_DEBUG(dbgs() << "Applying legalizer ruleset to: "; Query.print(dbgs());
             dbgs() << "\n");
  if (Rules.empty()) {
    LLVM_DEBUG(dbgs() << ".. fallback to legacy rules (no rules defined)\n");
    return {LegalizeAction::UseLegacyRules, 0, LLT{}};
  }
  for (const auto &Rule : Rules) {
    if (Rule.match(Query)) {
      LLVM_DEBUG(dbgs() << ".. match\n");
      std::pair<unsigned, LLT> Mutation = Rule.determineMutation(Query);
      LLVM_DEBUG(dbgs() << ".. .. " << (unsigned)Rule.getAction() << ", "
                        << Mutation.first << ", " << Mutation.second << "\n");
      assert((Query.Types[Mutation.first] != Mutation.second ||
              Rule.getAction() == Lower ||
              Rule.getAction() == MoreElements ||
              Rule.getAction() == FewerElements) &&
             "Simple loop detected");
      return {Rule.getAction(), Mutation.first, Mutation.second};
    } else
      LLVM_DEBUG(dbgs() << ".. no match\n");
  }
  LLVM_DEBUG(dbgs() << ".. unsupported\n");
  return {LegalizeAction::Unsupported, 0, LLT{}};
}

bool LegalizeRuleSet::verifyTypeIdxsCoverage(unsigned NumTypeIdxs) const {
#ifndef NDEBUG
  if (Rules.empty()) {
    LLVM_DEBUG(
        dbgs() << ".. type index coverage check SKIPPED: no rules defined\n");
    return true;
  }
  const int64_t FirstUncovered = TypeIdxsCovered.find_first_unset();
  if (FirstUncovered < 0) {
    LLVM_DEBUG(dbgs() << ".. type index coverage check SKIPPED:"
                         " user-defined predicate detected\n");
    return true;
  }
  const bool AllCovered = (FirstUncovered >= NumTypeIdxs);
  LLVM_DEBUG(dbgs() << ".. the first uncovered type index: " << FirstUncovered
                    << ", " << (AllCovered ? "OK" : "FAIL") << "\n");
  return AllCovered;
#else
  return true;
#endif
}

LegalizerInfo::LegalizerInfo() : TablesInitialized(false) {
  // Set defaults.
  // FIXME: these two (G_ANYEXT and G_TRUNC?) can be legalized to the
  // fundamental load/store Jakob proposed. Once loads & stores are supported.
  setScalarAction(TargetOpcode::G_ANYEXT, 1, {{1, Legal}});
  setScalarAction(TargetOpcode::G_ZEXT, 1, {{1, Legal}});
  setScalarAction(TargetOpcode::G_SEXT, 1, {{1, Legal}});
  setScalarAction(TargetOpcode::G_TRUNC, 0, {{1, Legal}});
  setScalarAction(TargetOpcode::G_TRUNC, 1, {{1, Legal}});

  setScalarAction(TargetOpcode::G_INTRINSIC, 0, {{1, Legal}});
  setScalarAction(TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS, 0, {{1, Legal}});

  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_IMPLICIT_DEF, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_ADD, 0, widenToLargerTypesAndNarrowToLargest);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_OR, 0, widenToLargerTypesAndNarrowToLargest);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_LOAD, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_STORE, 0, narrowToSmallerAndUnsupportedIfTooSmall);

  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_BRCOND, 0, widenToLargerTypesUnsupportedOtherwise);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_INSERT, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_EXTRACT, 0, narrowToSmallerAndUnsupportedIfTooSmall);
  setLegalizeScalarToDifferentSizeStrategy(
      TargetOpcode::G_EXTRACT, 1, narrowToSmallerAndUnsupportedIfTooSmall);
  setScalarAction(TargetOpcode::G_FNEG, 0, {{1, Lower}});
}

void LegalizerInfo::computeTables() {
  assert(TablesInitialized == false);

  for (unsigned OpcodeIdx = 0; OpcodeIdx <= LastOp - FirstOp; ++OpcodeIdx) {
    const unsigned Opcode = FirstOp + OpcodeIdx;
    for (unsigned TypeIdx = 0; TypeIdx != SpecifiedActions[OpcodeIdx].size();
         ++TypeIdx) {
      // 0. Collect information specified through the setAction API, i.e.
      // for specific bit sizes.
      // For scalar types:
      SizeAndActionsVec ScalarSpecifiedActions;
      // For pointer types:
      std::map<uint16_t, SizeAndActionsVec> AddressSpace2SpecifiedActions;
      // For vector types:
      std::map<uint16_t, SizeAndActionsVec> ElemSize2SpecifiedActions;
      for (auto LLT2Action : SpecifiedActions[OpcodeIdx][TypeIdx]) {
        const LLT Type = LLT2Action.first;
        const LegalizeAction Action = LLT2Action.second;

        auto SizeAction = std::make_pair(Type.getSizeInBits(), Action);
        if (Type.isPointer())
          AddressSpace2SpecifiedActions[Type.getAddressSpace()].push_back(
              SizeAction);
        else if (Type.isVector())
          ElemSize2SpecifiedActions[Type.getElementType().getSizeInBits()]
              .push_back(SizeAction);
        else
          ScalarSpecifiedActions.push_back(SizeAction);
      }

      // 1. Handle scalar types
      {
        // Decide how to handle bit sizes for which no explicit specification
        // was given.
        SizeChangeStrategy S = &unsupportedForDifferentSizes;
        if (TypeIdx < ScalarSizeChangeStrategies[OpcodeIdx].size() &&
            ScalarSizeChangeStrategies[OpcodeIdx][TypeIdx] != nullptr)
          S = ScalarSizeChangeStrategies[OpcodeIdx][TypeIdx];
        llvm::sort(ScalarSpecifiedActions.begin(),
                   ScalarSpecifiedActions.end());
        checkPartialSizeAndActionsVector(ScalarSpecifiedActions);
        setScalarAction(Opcode, TypeIdx, S(ScalarSpecifiedActions));
      }

      // 2. Handle pointer types
      for (auto PointerSpecifiedActions : AddressSpace2SpecifiedActions) {
        llvm::sort(PointerSpecifiedActions.second.begin(),
                   PointerSpecifiedActions.second.end());
        checkPartialSizeAndActionsVector(PointerSpecifiedActions.second);
        // For pointer types, we assume that there isn't a meaningfull way
        // to change the number of bits used in the pointer.
        setPointerAction(
            Opcode, TypeIdx, PointerSpecifiedActions.first,
            unsupportedForDifferentSizes(PointerSpecifiedActions.second));
      }

      // 3. Handle vector types
      SizeAndActionsVec ElementSizesSeen;
      for (auto VectorSpecifiedActions : ElemSize2SpecifiedActions) {
        llvm::sort(VectorSpecifiedActions.second.begin(),
                   VectorSpecifiedActions.second.end());
        const uint16_t ElementSize = VectorSpecifiedActions.first;
        ElementSizesSeen.push_back({ElementSize, Legal});
        checkPartialSizeAndActionsVector(VectorSpecifiedActions.second);
        // For vector types, we assume that the best way to adapt the number
        // of elements is to the next larger number of elements type for which
        // the vector type is legal, unless there is no such type. In that case,
        // legalize towards a vector type with a smaller number of elements.
        SizeAndActionsVec NumElementsActions;
        for (SizeAndAction BitsizeAndAction : VectorSpecifiedActions.second) {
          assert(BitsizeAndAction.first % ElementSize == 0);
          const uint16_t NumElements = BitsizeAndAction.first / ElementSize;
          NumElementsActions.push_back({NumElements, BitsizeAndAction.second});
        }
        setVectorNumElementAction(
            Opcode, TypeIdx, ElementSize,
            moreToWiderTypesAndLessToWidest(NumElementsActions));
      }
      llvm::sort(ElementSizesSeen);
      SizeChangeStrategy VectorElementSizeChangeStrategy =
          &unsupportedForDifferentSizes;
      if (TypeIdx < VectorElementSizeChangeStrategies[OpcodeIdx].size() &&
          VectorElementSizeChangeStrategies[OpcodeIdx][TypeIdx] != nullptr)
        VectorElementSizeChangeStrategy =
            VectorElementSizeChangeStrategies[OpcodeIdx][TypeIdx];
      setScalarInVectorAction(
          Opcode, TypeIdx, VectorElementSizeChangeStrategy(ElementSizesSeen));
    }
  }

  TablesInitialized = true;
}

// FIXME: inefficient implementation for now. Without ComputeValueVTs we're
// probably going to need specialized lookup structures for various types before
// we have any hope of doing well with something like <13 x i3>. Even the common
// cases should do better than what we have now.
std::pair<LegalizeAction, LLT>
LegalizerInfo::getAspectAction(const InstrAspect &Aspect) const {
  assert(TablesInitialized && "backend forgot to call computeTables");
  // These *have* to be implemented for now, they're the fundamental basis of
  // how everything else is transformed.
  if (Aspect.Type.isScalar() || Aspect.Type.isPointer())
    return findScalarLegalAction(Aspect);
  assert(Aspect.Type.isVector());
  return findVectorLegalAction(Aspect);
}

/// Helper function to get LLT for the given type index.
static LLT getTypeFromTypeIdx(const MachineInstr &MI,
                              const MachineRegisterInfo &MRI, unsigned OpIdx,
                              unsigned TypeIdx) {
  assert(TypeIdx < MI.getNumOperands() && "Unexpected TypeIdx");
  // G_UNMERGE_VALUES has variable number of operands, but there is only
  // one source type and one destination type as all destinations must be the
  // same type. So, get the last operand if TypeIdx == 1.
  if (MI.getOpcode() == TargetOpcode::G_UNMERGE_VALUES && TypeIdx == 1)
    return MRI.getType(MI.getOperand(MI.getNumOperands() - 1).getReg());
  return MRI.getType(MI.getOperand(OpIdx).getReg());
}

unsigned LegalizerInfo::getOpcodeIdxForOpcode(unsigned Opcode) const {
  assert(Opcode >= FirstOp && Opcode <= LastOp && "Unsupported opcode");
  return Opcode - FirstOp;
}

unsigned LegalizerInfo::getActionDefinitionsIdx(unsigned Opcode) const {
  unsigned OpcodeIdx = getOpcodeIdxForOpcode(Opcode);
  if (unsigned Alias = RulesForOpcode[OpcodeIdx].getAlias()) {
    LLVM_DEBUG(dbgs() << ".. opcode " << Opcode << " is aliased to " << Alias
                      << "\n");
    OpcodeIdx = getOpcodeIdxForOpcode(Alias);
    LLVM_DEBUG(dbgs() << ".. opcode " << Alias << " is aliased to "
                      << RulesForOpcode[OpcodeIdx].getAlias() << "\n");
    assert(RulesForOpcode[OpcodeIdx].getAlias() == 0 && "Cannot chain aliases");
  }

  return OpcodeIdx;
}

const LegalizeRuleSet &
LegalizerInfo::getActionDefinitions(unsigned Opcode) const {
  unsigned OpcodeIdx = getActionDefinitionsIdx(Opcode);
  return RulesForOpcode[OpcodeIdx];
}

LegalizeRuleSet &LegalizerInfo::getActionDefinitionsBuilder(unsigned Opcode) {
  unsigned OpcodeIdx = getActionDefinitionsIdx(Opcode);
  auto &Result = RulesForOpcode[OpcodeIdx];
  assert(!Result.isAliasedByAnother() && "Modifying this opcode will modify aliases");
  return Result;
}

LegalizeRuleSet &LegalizerInfo::getActionDefinitionsBuilder(
    std::initializer_list<unsigned> Opcodes) {
  unsigned Representative = *Opcodes.begin();

  assert(!empty(Opcodes) && Opcodes.begin() + 1 != Opcodes.end() &&
         "Initializer list must have at least two opcodes");

  for (auto I = Opcodes.begin() + 1, E = Opcodes.end(); I != E; ++I)
    aliasActionDefinitions(Representative, *I);

  auto &Return = getActionDefinitionsBuilder(Representative);
  Return.setIsAliasedByAnother();
  return Return;
}

void LegalizerInfo::aliasActionDefinitions(unsigned OpcodeTo,
                                           unsigned OpcodeFrom) {
  assert(OpcodeTo != OpcodeFrom && "Cannot alias to self");
  assert(OpcodeTo >= FirstOp && OpcodeTo <= LastOp && "Unsupported opcode");
  const unsigned OpcodeFromIdx = getOpcodeIdxForOpcode(OpcodeFrom);
  RulesForOpcode[OpcodeFromIdx].aliasTo(OpcodeTo);
}

LegalizeActionStep
LegalizerInfo::getAction(const LegalityQuery &Query) const {
  LegalizeActionStep Step = getActionDefinitions(Query.Opcode).apply(Query);
  if (Step.Action != LegalizeAction::UseLegacyRules) {
    return Step;
  }

  for (unsigned i = 0; i < Query.Types.size(); ++i) {
    auto Action = getAspectAction({Query.Opcode, i, Query.Types[i]});
    if (Action.first != Legal) {
      LLVM_DEBUG(dbgs() << ".. (legacy) Type " << i
                        << " Action=" << (unsigned)Action.first << ", "
                        << Action.second << "\n");
      return {Action.first, i, Action.second};
    } else
      LLVM_DEBUG(dbgs() << ".. (legacy) Type " << i << " Legal\n");
  }
  LLVM_DEBUG(dbgs() << ".. (legacy) Legal\n");
  return {Legal, 0, LLT{}};
}

LegalizeActionStep
LegalizerInfo::getAction(const MachineInstr &MI,
                         const MachineRegisterInfo &MRI) const {
  SmallVector<LLT, 2> Types;
  SmallBitVector SeenTypes(8);
  const MCOperandInfo *OpInfo = MI.getDesc().OpInfo;
  // FIXME: probably we'll need to cache the results here somehow?
  for (unsigned i = 0; i < MI.getDesc().getNumOperands(); ++i) {
    if (!OpInfo[i].isGenericType())
      continue;

    // We must only record actions once for each TypeIdx; otherwise we'd
    // try to legalize operands multiple times down the line.
    unsigned TypeIdx = OpInfo[i].getGenericTypeIndex();
    if (SeenTypes[TypeIdx])
      continue;

    SeenTypes.set(TypeIdx);

    LLT Ty = getTypeFromTypeIdx(MI, MRI, i, TypeIdx);
    Types.push_back(Ty);
  }

  SmallVector<LegalityQuery::MemDesc, 2> MemDescrs;
  for (const auto &MMO : MI.memoperands())
    MemDescrs.push_back(
        {MMO->getSize() /* in bytes */ * 8, MMO->getOrdering()});

  return getAction({MI.getOpcode(), Types, MemDescrs});
}

bool LegalizerInfo::isLegal(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI) const {
  return getAction(MI, MRI).Action == Legal;
}

bool LegalizerInfo::legalizeCustom(MachineInstr &MI, MachineRegisterInfo &MRI,
                                   MachineIRBuilder &MIRBuilder,
                                   GISelChangeObserver &Observer) const {
  return false;
}

LegalizerInfo::SizeAndActionsVec
LegalizerInfo::increaseToLargerTypesAndDecreaseToLargest(
    const SizeAndActionsVec &v, LegalizeAction IncreaseAction,
    LegalizeAction DecreaseAction) {
  SizeAndActionsVec result;
  unsigned LargestSizeSoFar = 0;
  if (v.size() >= 1 && v[0].first != 1)
    result.push_back({1, IncreaseAction});
  for (size_t i = 0; i < v.size(); ++i) {
    result.push_back(v[i]);
    LargestSizeSoFar = v[i].first;
    if (i + 1 < v.size() && v[i + 1].first != v[i].first + 1) {
      result.push_back({LargestSizeSoFar + 1, IncreaseAction});
      LargestSizeSoFar = v[i].first + 1;
    }
  }
  result.push_back({LargestSizeSoFar + 1, DecreaseAction});
  return result;
}

LegalizerInfo::SizeAndActionsVec
LegalizerInfo::decreaseToSmallerTypesAndIncreaseToSmallest(
    const SizeAndActionsVec &v, LegalizeAction DecreaseAction,
    LegalizeAction IncreaseAction) {
  SizeAndActionsVec result;
  if (v.size() == 0 || v[0].first != 1)
    result.push_back({1, IncreaseAction});
  for (size_t i = 0; i < v.size(); ++i) {
    result.push_back(v[i]);
    if (i + 1 == v.size() || v[i + 1].first != v[i].first + 1) {
      result.push_back({v[i].first + 1, DecreaseAction});
    }
  }
  return result;
}

LegalizerInfo::SizeAndAction
LegalizerInfo::findAction(const SizeAndActionsVec &Vec, const uint32_t Size) {
  assert(Size >= 1);
  // Find the last element in Vec that has a bitsize equal to or smaller than
  // the requested bit size.
  // That is the element just before the first element that is bigger than Size.
  auto VecIt = std::upper_bound(
      Vec.begin(), Vec.end(), Size,
      [](const uint32_t Size, const SizeAndAction lhs) -> bool {
        return Size < lhs.first;
      });
  assert(VecIt != Vec.begin() && "Does Vec not start with size 1?");
  --VecIt;
  int VecIdx = VecIt - Vec.begin();

  LegalizeAction Action = Vec[VecIdx].second;
  switch (Action) {
  case Legal:
  case Lower:
  case Libcall:
  case Custom:
    return {Size, Action};
  case FewerElements:
    // FIXME: is this special case still needed and correct?
    // Special case for scalarization:
    if (Vec == SizeAndActionsVec({{1, FewerElements}}))
      return {1, FewerElements};
    LLVM_FALLTHROUGH;
  case NarrowScalar: {
    // The following needs to be a loop, as for now, we do allow needing to
    // go over "Unsupported" bit sizes before finding a legalizable bit size.
    // e.g. (s8, WidenScalar), (s9, Unsupported), (s32, Legal). if Size==8,
    // we need to iterate over s9, and then to s32 to return (s32, Legal).
    // If we want to get rid of the below loop, we should have stronger asserts
    // when building the SizeAndActionsVecs, probably not allowing
    // "Unsupported" unless at the ends of the vector.
    for (int i = VecIdx - 1; i >= 0; --i)
      if (!needsLegalizingToDifferentSize(Vec[i].second) &&
          Vec[i].second != Unsupported)
        return {Vec[i].first, Action};
    llvm_unreachable("");
  }
  case WidenScalar:
  case MoreElements: {
    // See above, the following needs to be a loop, at least for now.
    for (std::size_t i = VecIdx + 1; i < Vec.size(); ++i)
      if (!needsLegalizingToDifferentSize(Vec[i].second) &&
          Vec[i].second != Unsupported)
        return {Vec[i].first, Action};
    llvm_unreachable("");
  }
  case Unsupported:
    return {Size, Unsupported};
  case NotFound:
  case UseLegacyRules:
    llvm_unreachable("NotFound");
  }
  llvm_unreachable("Action has an unknown enum value");
}

std::pair<LegalizeAction, LLT>
LegalizerInfo::findScalarLegalAction(const InstrAspect &Aspect) const {
  assert(Aspect.Type.isScalar() || Aspect.Type.isPointer());
  if (Aspect.Opcode < FirstOp || Aspect.Opcode > LastOp)
    return {NotFound, LLT()};
  const unsigned OpcodeIdx = getOpcodeIdxForOpcode(Aspect.Opcode);
  if (Aspect.Type.isPointer() &&
      AddrSpace2PointerActions[OpcodeIdx].find(Aspect.Type.getAddressSpace()) ==
          AddrSpace2PointerActions[OpcodeIdx].end()) {
    return {NotFound, LLT()};
  }
  const SmallVector<SizeAndActionsVec, 1> &Actions =
      Aspect.Type.isPointer()
          ? AddrSpace2PointerActions[OpcodeIdx]
                .find(Aspect.Type.getAddressSpace())
                ->second
          : ScalarActions[OpcodeIdx];
  if (Aspect.Idx >= Actions.size())
    return {NotFound, LLT()};
  const SizeAndActionsVec &Vec = Actions[Aspect.Idx];
  // FIXME: speed up this search, e.g. by using a results cache for repeated
  // queries?
  auto SizeAndAction = findAction(Vec, Aspect.Type.getSizeInBits());
  return {SizeAndAction.second,
          Aspect.Type.isScalar() ? LLT::scalar(SizeAndAction.first)
                                 : LLT::pointer(Aspect.Type.getAddressSpace(),
                                                SizeAndAction.first)};
}

std::pair<LegalizeAction, LLT>
LegalizerInfo::findVectorLegalAction(const InstrAspect &Aspect) const {
  assert(Aspect.Type.isVector());
  // First legalize the vector element size, then legalize the number of
  // lanes in the vector.
  if (Aspect.Opcode < FirstOp || Aspect.Opcode > LastOp)
    return {NotFound, Aspect.Type};
  const unsigned OpcodeIdx = getOpcodeIdxForOpcode(Aspect.Opcode);
  const unsigned TypeIdx = Aspect.Idx;
  if (TypeIdx >= ScalarInVectorActions[OpcodeIdx].size())
    return {NotFound, Aspect.Type};
  const SizeAndActionsVec &ElemSizeVec =
      ScalarInVectorActions[OpcodeIdx][TypeIdx];

  LLT IntermediateType;
  auto ElementSizeAndAction =
      findAction(ElemSizeVec, Aspect.Type.getScalarSizeInBits());
  IntermediateType =
      LLT::vector(Aspect.Type.getNumElements(), ElementSizeAndAction.first);
  if (ElementSizeAndAction.second != Legal)
    return {ElementSizeAndAction.second, IntermediateType};

  auto i = NumElements2Actions[OpcodeIdx].find(
      IntermediateType.getScalarSizeInBits());
  if (i == NumElements2Actions[OpcodeIdx].end()) {
    return {NotFound, IntermediateType};
  }
  const SizeAndActionsVec &NumElementsVec = (*i).second[TypeIdx];
  auto NumElementsAndAction =
      findAction(NumElementsVec, IntermediateType.getNumElements());
  return {NumElementsAndAction.second,
          LLT::vector(NumElementsAndAction.first,
                      IntermediateType.getScalarSizeInBits())};
}

/// \pre Type indices of every opcode form a dense set starting from 0.
void LegalizerInfo::verify(const MCInstrInfo &MII) const {
#ifndef NDEBUG
  std::vector<unsigned> FailedOpcodes;
  for (unsigned Opcode = FirstOp; Opcode <= LastOp; ++Opcode) {
    const MCInstrDesc &MCID = MII.get(Opcode);
    const unsigned NumTypeIdxs = std::accumulate(
        MCID.opInfo_begin(), MCID.opInfo_end(), 0U,
        [](unsigned Acc, const MCOperandInfo &OpInfo) {
          return OpInfo.isGenericType()
                     ? std::max(OpInfo.getGenericTypeIndex() + 1U, Acc)
                     : Acc;
        });
    LLVM_DEBUG(dbgs() << MII.getName(Opcode) << " (opcode " << Opcode
                      << "): " << NumTypeIdxs << " type ind"
                      << (NumTypeIdxs == 1 ? "ex" : "ices") << "\n");
    const LegalizeRuleSet &RuleSet = getActionDefinitions(Opcode);
    if (!RuleSet.verifyTypeIdxsCoverage(NumTypeIdxs))
      FailedOpcodes.push_back(Opcode);
  }
  if (!FailedOpcodes.empty()) {
    errs() << "The following opcodes have ill-defined legalization rules:";
    for (unsigned Opcode : FailedOpcodes)
      errs() << " " << MII.getName(Opcode);
    errs() << "\n";

    report_fatal_error("ill-defined LegalizerInfo"
                       ", try -debug-only=legalizer-info for details");
  }
#endif
}

#ifndef NDEBUG
// FIXME: This should be in the MachineVerifier, but it can't use the
// LegalizerInfo as it's currently in the separate GlobalISel library.
// Note that RegBankSelected property already checked in the verifier
// has the same layering problem, but we only use inline methods so
// end up not needing to link against the GlobalISel library.
const MachineInstr *llvm::machineFunctionIsIllegal(const MachineFunction &MF) {
  if (const LegalizerInfo *MLI = MF.getSubtarget().getLegalizerInfo()) {
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB)
        if (isPreISelGenericOpcode(MI.getOpcode()) && !MLI->isLegal(MI, MRI))
          return &MI;
  }
  return nullptr;
}
#endif
