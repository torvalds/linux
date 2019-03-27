//===- llvm/CodeGen/GlobalISel/LegalizerInfo.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Interface for Targets to specify which operations they can successfully
/// select and how the others should be expanded most efficiently.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGALIZERINFO_H
#define LLVM_CODEGEN_GLOBALISEL_LEGALIZERINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/LowLevelTypeImpl.h"
#include <cassert>
#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace llvm {

extern cl::opt<bool> DisableGISelLegalityCheck;

class MachineInstr;
class MachineIRBuilder;
class MachineRegisterInfo;
class MCInstrInfo;
class GISelChangeObserver;

namespace LegalizeActions {
enum LegalizeAction : std::uint8_t {
  /// The operation is expected to be selectable directly by the target, and
  /// no transformation is necessary.
  Legal,

  /// The operation should be synthesized from multiple instructions acting on
  /// a narrower scalar base-type. For example a 64-bit add might be
  /// implemented in terms of 32-bit add-with-carry.
  NarrowScalar,

  /// The operation should be implemented in terms of a wider scalar
  /// base-type. For example a <2 x s8> add could be implemented as a <2
  /// x s32> add (ignoring the high bits).
  WidenScalar,

  /// The (vector) operation should be implemented by splitting it into
  /// sub-vectors where the operation is legal. For example a <8 x s64> add
  /// might be implemented as 4 separate <2 x s64> adds.
  FewerElements,

  /// The (vector) operation should be implemented by widening the input
  /// vector and ignoring the lanes added by doing so. For example <2 x i8> is
  /// rarely legal, but you might perform an <8 x i8> and then only look at
  /// the first two results.
  MoreElements,

  /// The operation itself must be expressed in terms of simpler actions on
  /// this target. E.g. a SREM replaced by an SDIV and subtraction.
  Lower,

  /// The operation should be implemented as a call to some kind of runtime
  /// support library. For example this usually happens on machines that don't
  /// support floating-point operations natively.
  Libcall,

  /// The target wants to do something special with this combination of
  /// operand and type. A callback will be issued when it is needed.
  Custom,

  /// This operation is completely unsupported on the target. A programming
  /// error has occurred.
  Unsupported,

  /// Sentinel value for when no action was found in the specified table.
  NotFound,

  /// Fall back onto the old rules.
  /// TODO: Remove this once we've migrated
  UseLegacyRules,
};
} // end namespace LegalizeActions

using LegalizeActions::LegalizeAction;

/// Legalization is decided based on an instruction's opcode, which type slot
/// we're considering, and what the existing type is. These aspects are gathered
/// together for convenience in the InstrAspect class.
struct InstrAspect {
  unsigned Opcode;
  unsigned Idx = 0;
  LLT Type;

  InstrAspect(unsigned Opcode, LLT Type) : Opcode(Opcode), Type(Type) {}
  InstrAspect(unsigned Opcode, unsigned Idx, LLT Type)
      : Opcode(Opcode), Idx(Idx), Type(Type) {}

  bool operator==(const InstrAspect &RHS) const {
    return Opcode == RHS.Opcode && Idx == RHS.Idx && Type == RHS.Type;
  }
};

/// The LegalityQuery object bundles together all the information that's needed
/// to decide whether a given operation is legal or not.
/// For efficiency, it doesn't make a copy of Types so care must be taken not
/// to free it before using the query.
struct LegalityQuery {
  unsigned Opcode;
  ArrayRef<LLT> Types;

  struct MemDesc {
    uint64_t SizeInBits;
    AtomicOrdering Ordering;
  };

  /// Operations which require memory can use this to place requirements on the
  /// memory type for each MMO.
  ArrayRef<MemDesc> MMODescrs;

  constexpr LegalityQuery(unsigned Opcode, const ArrayRef<LLT> Types,
                          const ArrayRef<MemDesc> MMODescrs)
      : Opcode(Opcode), Types(Types), MMODescrs(MMODescrs) {}
  constexpr LegalityQuery(unsigned Opcode, const ArrayRef<LLT> Types)
      : LegalityQuery(Opcode, Types, {}) {}

  raw_ostream &print(raw_ostream &OS) const;
};

/// The result of a query. It either indicates a final answer of Legal or
/// Unsupported or describes an action that must be taken to make an operation
/// more legal.
struct LegalizeActionStep {
  /// The action to take or the final answer.
  LegalizeAction Action;
  /// If describing an action, the type index to change. Otherwise zero.
  unsigned TypeIdx;
  /// If describing an action, the new type for TypeIdx. Otherwise LLT{}.
  LLT NewType;

  LegalizeActionStep(LegalizeAction Action, unsigned TypeIdx,
                     const LLT &NewType)
      : Action(Action), TypeIdx(TypeIdx), NewType(NewType) {}

  bool operator==(const LegalizeActionStep &RHS) const {
    return std::tie(Action, TypeIdx, NewType) ==
        std::tie(RHS.Action, RHS.TypeIdx, RHS.NewType);
  }
};

using LegalityPredicate = std::function<bool (const LegalityQuery &)>;
using LegalizeMutation =
    std::function<std::pair<unsigned, LLT>(const LegalityQuery &)>;

namespace LegalityPredicates {
struct TypePairAndMemSize {
  LLT Type0;
  LLT Type1;
  uint64_t MemSize;

  bool operator==(const TypePairAndMemSize &Other) const {
    return Type0 == Other.Type0 && Type1 == Other.Type1 &&
           MemSize == Other.MemSize;
  }
};

/// True iff P0 and P1 are true.
template<typename Predicate>
Predicate all(Predicate P0, Predicate P1) {
  return [=](const LegalityQuery &Query) {
    return P0(Query) && P1(Query);
  };
}
/// True iff all given predicates are true.
template<typename Predicate, typename... Args>
Predicate all(Predicate P0, Predicate P1, Args... args) {
  return all(all(P0, P1), args...);
}
/// True iff the given type index is the specified types.
LegalityPredicate typeIs(unsigned TypeIdx, LLT TypesInit);
/// True iff the given type index is one of the specified types.
LegalityPredicate typeInSet(unsigned TypeIdx,
                            std::initializer_list<LLT> TypesInit);
/// True iff the given types for the given pair of type indexes is one of the
/// specified type pairs.
LegalityPredicate
typePairInSet(unsigned TypeIdx0, unsigned TypeIdx1,
              std::initializer_list<std::pair<LLT, LLT>> TypesInit);
/// True iff the given types for the given pair of type indexes is one of the
/// specified type pairs.
LegalityPredicate typePairAndMemSizeInSet(
    unsigned TypeIdx0, unsigned TypeIdx1, unsigned MMOIdx,
    std::initializer_list<TypePairAndMemSize> TypesAndMemSizeInit);
/// True iff the specified type index is a scalar.
LegalityPredicate isScalar(unsigned TypeIdx);
/// True iff the specified type index is a scalar that's narrower than the given
/// size.
LegalityPredicate narrowerThan(unsigned TypeIdx, unsigned Size);
/// True iff the specified type index is a scalar that's wider than the given
/// size.
LegalityPredicate widerThan(unsigned TypeIdx, unsigned Size);
/// True iff the specified type index is a scalar whose size is not a power of
/// 2.
LegalityPredicate sizeNotPow2(unsigned TypeIdx);
/// True iff the specified MMO index has a size that is not a power of 2
LegalityPredicate memSizeInBytesNotPow2(unsigned MMOIdx);
/// True iff the specified type index is a vector whose element count is not a
/// power of 2.
LegalityPredicate numElementsNotPow2(unsigned TypeIdx);
/// True iff the specified MMO index has at an atomic ordering of at Ordering or
/// stronger.
LegalityPredicate atomicOrderingAtLeastOrStrongerThan(unsigned MMOIdx,
                                                      AtomicOrdering Ordering);
} // end namespace LegalityPredicates

namespace LegalizeMutations {
/// Select this specific type for the given type index.
LegalizeMutation changeTo(unsigned TypeIdx, LLT Ty);
/// Keep the same type as the given type index.
LegalizeMutation changeTo(unsigned TypeIdx, unsigned FromTypeIdx);
/// Widen the type for the given type index to the next power of 2.
LegalizeMutation widenScalarToNextPow2(unsigned TypeIdx, unsigned Min = 0);
/// Add more elements to the type for the given type index to the next power of
/// 2.
LegalizeMutation moreElementsToNextPow2(unsigned TypeIdx, unsigned Min = 0);
} // end namespace LegalizeMutations

/// A single rule in a legalizer info ruleset.
/// The specified action is chosen when the predicate is true. Where appropriate
/// for the action (e.g. for WidenScalar) the new type is selected using the
/// given mutator.
class LegalizeRule {
  LegalityPredicate Predicate;
  LegalizeAction Action;
  LegalizeMutation Mutation;

public:
  LegalizeRule(LegalityPredicate Predicate, LegalizeAction Action,
               LegalizeMutation Mutation = nullptr)
      : Predicate(Predicate), Action(Action), Mutation(Mutation) {}

  /// Test whether the LegalityQuery matches.
  bool match(const LegalityQuery &Query) const {
    return Predicate(Query);
  }

  LegalizeAction getAction() const { return Action; }

  /// Determine the change to make.
  std::pair<unsigned, LLT> determineMutation(const LegalityQuery &Query) const {
    if (Mutation)
      return Mutation(Query);
    return std::make_pair(0, LLT{});
  }
};

class LegalizeRuleSet {
  /// When non-zero, the opcode we are an alias of
  unsigned AliasOf;
  /// If true, there is another opcode that aliases this one
  bool IsAliasedByAnother;
  SmallVector<LegalizeRule, 2> Rules;

#ifndef NDEBUG
  /// If bit I is set, this rule set contains a rule that may handle (predicate
  /// or perform an action upon (or both)) the type index I. The uncertainty
  /// comes from free-form rules executing user-provided lambda functions. We
  /// conservatively assume such rules do the right thing and cover all type
  /// indices. The bitset is intentionally 1 bit wider than it absolutely needs
  /// to be to distinguish such cases from the cases where all type indices are
  /// individually handled.
  SmallBitVector TypeIdxsCovered{MCOI::OPERAND_LAST_GENERIC -
                                 MCOI::OPERAND_FIRST_GENERIC + 2};
#endif

  unsigned typeIdx(unsigned TypeIdx) {
    assert(TypeIdx <=
               (MCOI::OPERAND_LAST_GENERIC - MCOI::OPERAND_FIRST_GENERIC) &&
           "Type Index is out of bounds");
#ifndef NDEBUG
    TypeIdxsCovered.set(TypeIdx);
#endif
    return TypeIdx;
  }
  void markAllTypeIdxsAsCovered() {
#ifndef NDEBUG
    TypeIdxsCovered.set();
#endif
  }

  void add(const LegalizeRule &Rule) {
    assert(AliasOf == 0 &&
           "RuleSet is aliased, change the representative opcode instead");
    Rules.push_back(Rule);
  }

  static bool always(const LegalityQuery &) { return true; }

  /// Use the given action when the predicate is true.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionIf(LegalizeAction Action,
                            LegalityPredicate Predicate) {
    add({Predicate, Action});
    return *this;
  }
  /// Use the given action when the predicate is true.
  /// Action should be an action that requires mutation.
  LegalizeRuleSet &actionIf(LegalizeAction Action, LegalityPredicate Predicate,
                            LegalizeMutation Mutation) {
    add({Predicate, Action, Mutation});
    return *this;
  }
  /// Use the given action when type index 0 is any type in the given list.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<LLT> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action, typeInSet(typeIdx(0), Types));
  }
  /// Use the given action when type index 0 is any type in the given list.
  /// Action should be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<LLT> Types,
                             LegalizeMutation Mutation) {
    using namespace LegalityPredicates;
    return actionIf(Action, typeInSet(typeIdx(0), Types), Mutation);
  }
  /// Use the given action when type indexes 0 and 1 is any type pair in the
  /// given list.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<std::pair<LLT, LLT>> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action, typePairInSet(typeIdx(0), typeIdx(1), Types));
  }
  /// Use the given action when type indexes 0 and 1 is any type pair in the
  /// given list.
  /// Action should be an action that requires mutation.
  LegalizeRuleSet &actionFor(LegalizeAction Action,
                             std::initializer_list<std::pair<LLT, LLT>> Types,
                             LegalizeMutation Mutation) {
    using namespace LegalityPredicates;
    return actionIf(Action, typePairInSet(typeIdx(0), typeIdx(1), Types),
                    Mutation);
  }
  /// Use the given action when type indexes 0 and 1 are both in the given list.
  /// That is, the type pair is in the cartesian product of the list.
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionForCartesianProduct(LegalizeAction Action,
                                             std::initializer_list<LLT> Types) {
    using namespace LegalityPredicates;
    return actionIf(Action, all(typeInSet(typeIdx(0), Types),
                                typeInSet(typeIdx(1), Types)));
  }
  /// Use the given action when type indexes 0 and 1 are both in their
  /// respective lists.
  /// That is, the type pair is in the cartesian product of the lists
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &
  actionForCartesianProduct(LegalizeAction Action,
                            std::initializer_list<LLT> Types0,
                            std::initializer_list<LLT> Types1) {
    using namespace LegalityPredicates;
    return actionIf(Action, all(typeInSet(typeIdx(0), Types0),
                                typeInSet(typeIdx(1), Types1)));
  }
  /// Use the given action when type indexes 0, 1, and 2 are all in their
  /// respective lists.
  /// That is, the type triple is in the cartesian product of the lists
  /// Action should not be an action that requires mutation.
  LegalizeRuleSet &actionForCartesianProduct(
      LegalizeAction Action, std::initializer_list<LLT> Types0,
      std::initializer_list<LLT> Types1, std::initializer_list<LLT> Types2) {
    using namespace LegalityPredicates;
    return actionIf(Action, all(typeInSet(typeIdx(0), Types0),
                                all(typeInSet(typeIdx(1), Types1),
                                    typeInSet(typeIdx(2), Types2))));
  }

public:
  LegalizeRuleSet() : AliasOf(0), IsAliasedByAnother(false), Rules() {}

  bool isAliasedByAnother() { return IsAliasedByAnother; }
  void setIsAliasedByAnother() { IsAliasedByAnother = true; }
  void aliasTo(unsigned Opcode) {
    assert((AliasOf == 0 || AliasOf == Opcode) &&
           "Opcode is already aliased to another opcode");
    assert(Rules.empty() && "Aliasing will discard rules");
    AliasOf = Opcode;
  }
  unsigned getAlias() const { return AliasOf; }

  /// The instruction is legal if predicate is true.
  LegalizeRuleSet &legalIf(LegalityPredicate Predicate) {
    // We have no choice but conservatively assume that the free-form
    // user-provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::Legal, Predicate);
  }
  /// The instruction is legal when type index 0 is any type in the given list.
  LegalizeRuleSet &legalFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal when type indexes 0 and 1 is any type pair in the
  /// given list.
  LegalizeRuleSet &legalFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal when type indexes 0 and 1 along with the memory
  /// size is any type and size tuple in the given list.
  LegalizeRuleSet &legalForTypesWithMemSize(
      std::initializer_list<LegalityPredicates::TypePairAndMemSize>
          TypesAndMemSize) {
    return actionIf(LegalizeAction::Legal,
                    LegalityPredicates::typePairAndMemSizeInSet(
                        typeIdx(0), typeIdx(1), /*MMOIdx*/ 0, TypesAndMemSize));
  }
  /// The instruction is legal when type indexes 0 and 1 are both in the given
  /// list. That is, the type pair is in the cartesian product of the list.
  LegalizeRuleSet &legalForCartesianProduct(std::initializer_list<LLT> Types) {
    return actionForCartesianProduct(LegalizeAction::Legal, Types);
  }
  /// The instruction is legal when type indexes 0 and 1 are both their
  /// respective lists.
  LegalizeRuleSet &legalForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1) {
    return actionForCartesianProduct(LegalizeAction::Legal, Types0, Types1);
  }

  /// The instruction is lowered.
  LegalizeRuleSet &lower() {
    using namespace LegalizeMutations;
    // We have no choice but conservatively assume that predicate-less lowering
    // properly handles all type indices by design:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::Lower, always);
  }
  /// The instruction is lowered if predicate is true. Keep type index 0 as the
  /// same type.
  LegalizeRuleSet &lowerIf(LegalityPredicate Predicate) {
    using namespace LegalizeMutations;
    // We have no choice but conservatively assume that lowering with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::Lower, Predicate);
  }
  /// The instruction is lowered if predicate is true.
  LegalizeRuleSet &lowerIf(LegalityPredicate Predicate,
                           LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that lowering with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::Lower, Predicate, Mutation);
  }
  /// The instruction is lowered when type index 0 is any type in the given
  /// list. Keep type index 0 as the same type.
  LegalizeRuleSet &lowerFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Lower, Types,
                     LegalizeMutations::changeTo(0, 0));
  }
  /// The instruction is lowered when type index 0 is any type in the given
  /// list.
  LegalizeRuleSet &lowerFor(std::initializer_list<LLT> Types,
                            LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::Lower, Types, Mutation);
  }
  /// The instruction is lowered when type indexes 0 and 1 is any type pair in
  /// the given list. Keep type index 0 as the same type.
  LegalizeRuleSet &lowerFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Lower, Types,
                     LegalizeMutations::changeTo(0, 0));
  }
  /// The instruction is lowered when type indexes 0 and 1 is any type pair in
  /// the given list.
  LegalizeRuleSet &lowerFor(std::initializer_list<std::pair<LLT, LLT>> Types,
                            LegalizeMutation Mutation) {
    return actionFor(LegalizeAction::Lower, Types, Mutation);
  }
  /// The instruction is lowered when type indexes 0 and 1 are both in their
  /// respective lists.
  LegalizeRuleSet &lowerForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1) {
    using namespace LegalityPredicates;
    return actionForCartesianProduct(LegalizeAction::Lower, Types0, Types1);
  }
  /// The instruction is lowered when when type indexes 0, 1, and 2 are all in
  /// their respective lists.
  LegalizeRuleSet &lowerForCartesianProduct(std::initializer_list<LLT> Types0,
                                            std::initializer_list<LLT> Types1,
                                            std::initializer_list<LLT> Types2) {
    using namespace LegalityPredicates;
    return actionForCartesianProduct(LegalizeAction::Lower, Types0, Types1,
                                     Types2);
  }

  /// Like legalIf, but for the Libcall action.
  LegalizeRuleSet &libcallIf(LegalityPredicate Predicate) {
    // We have no choice but conservatively assume that a libcall with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::Libcall, Predicate);
  }
  LegalizeRuleSet &libcallFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Libcall, Types);
  }
  LegalizeRuleSet &
  libcallFor(std::initializer_list<std::pair<LLT, LLT>> Types) {
    return actionFor(LegalizeAction::Libcall, Types);
  }
  LegalizeRuleSet &
  libcallForCartesianProduct(std::initializer_list<LLT> Types) {
    return actionForCartesianProduct(LegalizeAction::Libcall, Types);
  }
  LegalizeRuleSet &
  libcallForCartesianProduct(std::initializer_list<LLT> Types0,
                             std::initializer_list<LLT> Types1) {
    return actionForCartesianProduct(LegalizeAction::Libcall, Types0, Types1);
  }

  /// Widen the scalar to the one selected by the mutation if the predicate is
  /// true.
  LegalizeRuleSet &widenScalarIf(LegalityPredicate Predicate,
                                 LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::WidenScalar, Predicate, Mutation);
  }
  /// Narrow the scalar to the one selected by the mutation if the predicate is
  /// true.
  LegalizeRuleSet &narrowScalarIf(LegalityPredicate Predicate,
                                  LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::NarrowScalar, Predicate, Mutation);
  }

  /// Add more elements to reach the type selected by the mutation if the
  /// predicate is true.
  LegalizeRuleSet &moreElementsIf(LegalityPredicate Predicate,
                                  LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::MoreElements, Predicate, Mutation);
  }
  /// Remove elements to reach the type selected by the mutation if the
  /// predicate is true.
  LegalizeRuleSet &fewerElementsIf(LegalityPredicate Predicate,
                                   LegalizeMutation Mutation) {
    // We have no choice but conservatively assume that an action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::FewerElements, Predicate, Mutation);
  }

  /// The instruction is unsupported.
  LegalizeRuleSet &unsupported() {
    return actionIf(LegalizeAction::Unsupported, always);
  }
  LegalizeRuleSet &unsupportedIf(LegalityPredicate Predicate) {
    return actionIf(LegalizeAction::Unsupported, Predicate);
  }
  LegalizeRuleSet &unsupportedIfMemSizeNotPow2() {
    return actionIf(LegalizeAction::Unsupported,
                    LegalityPredicates::memSizeInBytesNotPow2(0));
  }

  LegalizeRuleSet &customIf(LegalityPredicate Predicate) {
    // We have no choice but conservatively assume that a custom action with a
    // free-form user provided Predicate properly handles all type indices:
    markAllTypeIdxsAsCovered();
    return actionIf(LegalizeAction::Custom, Predicate);
  }
  LegalizeRuleSet &customFor(std::initializer_list<LLT> Types) {
    return actionFor(LegalizeAction::Custom, Types);
  }
  LegalizeRuleSet &customForCartesianProduct(std::initializer_list<LLT> Types) {
    return actionForCartesianProduct(LegalizeAction::Custom, Types);
  }
  LegalizeRuleSet &
  customForCartesianProduct(std::initializer_list<LLT> Types0,
                            std::initializer_list<LLT> Types1) {
    return actionForCartesianProduct(LegalizeAction::Custom, Types0, Types1);
  }

  /// Widen the scalar to the next power of two that is at least MinSize.
  /// No effect if the type is not a scalar or is a power of two.
  LegalizeRuleSet &widenScalarToNextPow2(unsigned TypeIdx,
                                         unsigned MinSize = 0) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::WidenScalar, sizeNotPow2(typeIdx(TypeIdx)),
                    LegalizeMutations::widenScalarToNextPow2(TypeIdx, MinSize));
  }

  LegalizeRuleSet &narrowScalar(unsigned TypeIdx, LegalizeMutation Mutation) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::NarrowScalar, isScalar(typeIdx(TypeIdx)),
                    Mutation);
  }

  /// Ensure the scalar is at least as wide as Ty.
  LegalizeRuleSet &minScalar(unsigned TypeIdx, const LLT &Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::WidenScalar,
                    narrowerThan(TypeIdx, Ty.getSizeInBits()),
                    changeTo(typeIdx(TypeIdx), Ty));
  }

  /// Ensure the scalar is at most as wide as Ty.
  LegalizeRuleSet &maxScalar(unsigned TypeIdx, const LLT &Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::NarrowScalar,
                    widerThan(TypeIdx, Ty.getSizeInBits()),
                    changeTo(typeIdx(TypeIdx), Ty));
  }

  /// Conditionally limit the maximum size of the scalar.
  /// For example, when the maximum size of one type depends on the size of
  /// another such as extracting N bits from an M bit container.
  LegalizeRuleSet &maxScalarIf(LegalityPredicate Predicate, unsigned TypeIdx,
                               const LLT &Ty) {
    using namespace LegalityPredicates;
    using namespace LegalizeMutations;
    return actionIf(LegalizeAction::NarrowScalar,
                    [=](const LegalityQuery &Query) {
                      return widerThan(TypeIdx, Ty.getSizeInBits()) &&
                             Predicate(Query);
                    },
                    changeTo(typeIdx(TypeIdx), Ty));
  }

  /// Limit the range of scalar sizes to MinTy and MaxTy.
  LegalizeRuleSet &clampScalar(unsigned TypeIdx, const LLT &MinTy,
                               const LLT &MaxTy) {
    assert(MinTy.isScalar() && MaxTy.isScalar() && "Expected scalar types");
    return minScalar(TypeIdx, MinTy).maxScalar(TypeIdx, MaxTy);
  }

  /// Widen the scalar to match the size of another.
  LegalizeRuleSet &minScalarSameAs(unsigned TypeIdx, unsigned LargeTypeIdx) {
    typeIdx(TypeIdx);
    return widenScalarIf(
        [=](const LegalityQuery &Query) {
          return Query.Types[LargeTypeIdx].getScalarSizeInBits() >
                 Query.Types[TypeIdx].getSizeInBits();
        },
        [=](const LegalityQuery &Query) {
          return std::make_pair(TypeIdx,
                                Query.Types[LargeTypeIdx].getElementType());
        });
  }

  /// Add more elements to the vector to reach the next power of two.
  /// No effect if the type is not a vector or the element count is a power of
  /// two.
  LegalizeRuleSet &moreElementsToNextPow2(unsigned TypeIdx) {
    using namespace LegalityPredicates;
    return actionIf(LegalizeAction::MoreElements,
                    numElementsNotPow2(typeIdx(TypeIdx)),
                    LegalizeMutations::moreElementsToNextPow2(TypeIdx));
  }

  /// Limit the number of elements in EltTy vectors to at least MinElements.
  LegalizeRuleSet &clampMinNumElements(unsigned TypeIdx, const LLT &EltTy,
                                       unsigned MinElements) {
    // Mark the type index as covered:
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::MoreElements,
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return VecTy.isVector() && VecTy.getElementType() == EltTy &&
                 VecTy.getNumElements() < MinElements;
        },
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return std::make_pair(
              TypeIdx, LLT::vector(MinElements, VecTy.getScalarSizeInBits()));
        });
  }
  /// Limit the number of elements in EltTy vectors to at most MaxElements.
  LegalizeRuleSet &clampMaxNumElements(unsigned TypeIdx, const LLT &EltTy,
                                       unsigned MaxElements) {
    // Mark the type index as covered:
    typeIdx(TypeIdx);
    return actionIf(
        LegalizeAction::FewerElements,
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          return VecTy.isVector() && VecTy.getElementType() == EltTy &&
                 VecTy.getNumElements() > MaxElements;
        },
        [=](const LegalityQuery &Query) {
          LLT VecTy = Query.Types[TypeIdx];
          if (MaxElements == 1)
            return std::make_pair(TypeIdx, VecTy.getElementType());
          return std::make_pair(
              TypeIdx, LLT::vector(MaxElements, VecTy.getScalarSizeInBits()));
        });
  }
  /// Limit the number of elements for the given vectors to at least MinTy's
  /// number of elements and at most MaxTy's number of elements.
  ///
  /// No effect if the type is not a vector or does not have the same element
  /// type as the constraints.
  /// The element type of MinTy and MaxTy must match.
  LegalizeRuleSet &clampNumElements(unsigned TypeIdx, const LLT &MinTy,
                                    const LLT &MaxTy) {
    assert(MinTy.getElementType() == MaxTy.getElementType() &&
           "Expected element types to agree");

    const LLT &EltTy = MinTy.getElementType();
    return clampMinNumElements(TypeIdx, EltTy, MinTy.getNumElements())
        .clampMaxNumElements(TypeIdx, EltTy, MaxTy.getNumElements());
  }

  /// Fallback on the previous implementation. This should only be used while
  /// porting a rule.
  LegalizeRuleSet &fallback() {
    add({always, LegalizeAction::UseLegacyRules});
    return *this;
  }

  /// Check if there is no type index which is obviously not handled by the
  /// LegalizeRuleSet in any way at all.
  /// \pre Type indices of the opcode form a dense [0, \p NumTypeIdxs) set.
  bool verifyTypeIdxsCoverage(unsigned NumTypeIdxs) const;

  /// Apply the ruleset to the given LegalityQuery.
  LegalizeActionStep apply(const LegalityQuery &Query) const;
};

class LegalizerInfo {
public:
  LegalizerInfo();
  virtual ~LegalizerInfo() = default;

  unsigned getOpcodeIdxForOpcode(unsigned Opcode) const;
  unsigned getActionDefinitionsIdx(unsigned Opcode) const;

  /// Compute any ancillary tables needed to quickly decide how an operation
  /// should be handled. This must be called after all "set*Action"methods but
  /// before any query is made or incorrect results may be returned.
  void computeTables();

  /// Perform simple self-diagnostic and assert if there is anything obviously
  /// wrong with the actions set up.
  void verify(const MCInstrInfo &MII) const;

  static bool needsLegalizingToDifferentSize(const LegalizeAction Action) {
    using namespace LegalizeActions;
    switch (Action) {
    case NarrowScalar:
    case WidenScalar:
    case FewerElements:
    case MoreElements:
    case Unsupported:
      return true;
    default:
      return false;
    }
  }

  using SizeAndAction = std::pair<uint16_t, LegalizeAction>;
  using SizeAndActionsVec = std::vector<SizeAndAction>;
  using SizeChangeStrategy =
      std::function<SizeAndActionsVec(const SizeAndActionsVec &v)>;

  /// More friendly way to set an action for common types that have an LLT
  /// representation.
  /// The LegalizeAction must be one for which NeedsLegalizingToDifferentSize
  /// returns false.
  void setAction(const InstrAspect &Aspect, LegalizeAction Action) {
    assert(!needsLegalizingToDifferentSize(Action));
    TablesInitialized = false;
    const unsigned OpcodeIdx = Aspect.Opcode - FirstOp;
    if (SpecifiedActions[OpcodeIdx].size() <= Aspect.Idx)
      SpecifiedActions[OpcodeIdx].resize(Aspect.Idx + 1);
    SpecifiedActions[OpcodeIdx][Aspect.Idx][Aspect.Type] = Action;
  }

  /// The setAction calls record the non-size-changing legalization actions
  /// to take on specificly-sized types. The SizeChangeStrategy defines what
  /// to do when the size of the type needs to be changed to reach a legally
  /// sized type (i.e., one that was defined through a setAction call).
  /// e.g.
  /// setAction ({G_ADD, 0, LLT::scalar(32)}, Legal);
  /// setLegalizeScalarToDifferentSizeStrategy(
  ///   G_ADD, 0, widenToLargerTypesAndNarrowToLargest);
  /// will end up defining getAction({G_ADD, 0, T}) to return the following
  /// actions for different scalar types T:
  ///  LLT::scalar(1)..LLT::scalar(31): {WidenScalar, 0, LLT::scalar(32)}
  ///  LLT::scalar(32):                 {Legal, 0, LLT::scalar(32)}
  ///  LLT::scalar(33)..:               {NarrowScalar, 0, LLT::scalar(32)}
  ///
  /// If no SizeChangeAction gets defined, through this function,
  /// the default is unsupportedForDifferentSizes.
  void setLegalizeScalarToDifferentSizeStrategy(const unsigned Opcode,
                                                const unsigned TypeIdx,
                                                SizeChangeStrategy S) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (ScalarSizeChangeStrategies[OpcodeIdx].size() <= TypeIdx)
      ScalarSizeChangeStrategies[OpcodeIdx].resize(TypeIdx + 1);
    ScalarSizeChangeStrategies[OpcodeIdx][TypeIdx] = S;
  }

  /// See also setLegalizeScalarToDifferentSizeStrategy.
  /// This function allows to set the SizeChangeStrategy for vector elements.
  void setLegalizeVectorElementToDifferentSizeStrategy(const unsigned Opcode,
                                                       const unsigned TypeIdx,
                                                       SizeChangeStrategy S) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (VectorElementSizeChangeStrategies[OpcodeIdx].size() <= TypeIdx)
      VectorElementSizeChangeStrategies[OpcodeIdx].resize(TypeIdx + 1);
    VectorElementSizeChangeStrategies[OpcodeIdx][TypeIdx] = S;
  }

  /// A SizeChangeStrategy for the common case where legalization for a
  /// particular operation consists of only supporting a specific set of type
  /// sizes. E.g.
  ///   setAction ({G_DIV, 0, LLT::scalar(32)}, Legal);
  ///   setAction ({G_DIV, 0, LLT::scalar(64)}, Legal);
  ///   setLegalizeScalarToDifferentSizeStrategy(
  ///     G_DIV, 0, unsupportedForDifferentSizes);
  /// will result in getAction({G_DIV, 0, T}) to return Legal for s32 and s64,
  /// and Unsupported for all other scalar types T.
  static SizeAndActionsVec
  unsupportedForDifferentSizes(const SizeAndActionsVec &v) {
    using namespace LegalizeActions;
    return increaseToLargerTypesAndDecreaseToLargest(v, Unsupported,
                                                     Unsupported);
  }

  /// A SizeChangeStrategy for the common case where legalization for a
  /// particular operation consists of widening the type to a large legal type,
  /// unless there is no such type and then instead it should be narrowed to the
  /// largest legal type.
  static SizeAndActionsVec
  widenToLargerTypesAndNarrowToLargest(const SizeAndActionsVec &v) {
    using namespace LegalizeActions;
    assert(v.size() > 0 &&
           "At least one size that can be legalized towards is needed"
           " for this SizeChangeStrategy");
    return increaseToLargerTypesAndDecreaseToLargest(v, WidenScalar,
                                                     NarrowScalar);
  }

  static SizeAndActionsVec
  widenToLargerTypesUnsupportedOtherwise(const SizeAndActionsVec &v) {
    using namespace LegalizeActions;
    return increaseToLargerTypesAndDecreaseToLargest(v, WidenScalar,
                                                     Unsupported);
  }

  static SizeAndActionsVec
  narrowToSmallerAndUnsupportedIfTooSmall(const SizeAndActionsVec &v) {
    using namespace LegalizeActions;
    return decreaseToSmallerTypesAndIncreaseToSmallest(v, NarrowScalar,
                                                       Unsupported);
  }

  static SizeAndActionsVec
  narrowToSmallerAndWidenToSmallest(const SizeAndActionsVec &v) {
    using namespace LegalizeActions;
    assert(v.size() > 0 &&
           "At least one size that can be legalized towards is needed"
           " for this SizeChangeStrategy");
    return decreaseToSmallerTypesAndIncreaseToSmallest(v, NarrowScalar,
                                                       WidenScalar);
  }

  /// A SizeChangeStrategy for the common case where legalization for a
  /// particular vector operation consists of having more elements in the
  /// vector, to a type that is legal. Unless there is no such type and then
  /// instead it should be legalized towards the widest vector that's still
  /// legal. E.g.
  ///   setAction({G_ADD, LLT::vector(8, 8)}, Legal);
  ///   setAction({G_ADD, LLT::vector(16, 8)}, Legal);
  ///   setAction({G_ADD, LLT::vector(2, 32)}, Legal);
  ///   setAction({G_ADD, LLT::vector(4, 32)}, Legal);
  ///   setLegalizeVectorElementToDifferentSizeStrategy(
  ///     G_ADD, 0, moreToWiderTypesAndLessToWidest);
  /// will result in the following getAction results:
  ///   * getAction({G_ADD, LLT::vector(8,8)}) returns
  ///       (Legal, vector(8,8)).
  ///   * getAction({G_ADD, LLT::vector(9,8)}) returns
  ///       (MoreElements, vector(16,8)).
  ///   * getAction({G_ADD, LLT::vector(8,32)}) returns
  ///       (FewerElements, vector(4,32)).
  static SizeAndActionsVec
  moreToWiderTypesAndLessToWidest(const SizeAndActionsVec &v) {
    using namespace LegalizeActions;
    return increaseToLargerTypesAndDecreaseToLargest(v, MoreElements,
                                                     FewerElements);
  }

  /// Helper function to implement many typical SizeChangeStrategy functions.
  static SizeAndActionsVec
  increaseToLargerTypesAndDecreaseToLargest(const SizeAndActionsVec &v,
                                            LegalizeAction IncreaseAction,
                                            LegalizeAction DecreaseAction);
  /// Helper function to implement many typical SizeChangeStrategy functions.
  static SizeAndActionsVec
  decreaseToSmallerTypesAndIncreaseToSmallest(const SizeAndActionsVec &v,
                                              LegalizeAction DecreaseAction,
                                              LegalizeAction IncreaseAction);

  /// Get the action definitions for the given opcode. Use this to run a
  /// LegalityQuery through the definitions.
  const LegalizeRuleSet &getActionDefinitions(unsigned Opcode) const;

  /// Get the action definition builder for the given opcode. Use this to define
  /// the action definitions.
  ///
  /// It is an error to request an opcode that has already been requested by the
  /// multiple-opcode variant.
  LegalizeRuleSet &getActionDefinitionsBuilder(unsigned Opcode);

  /// Get the action definition builder for the given set of opcodes. Use this
  /// to define the action definitions for multiple opcodes at once. The first
  /// opcode given will be considered the representative opcode and will hold
  /// the definitions whereas the other opcodes will be configured to refer to
  /// the representative opcode. This lowers memory requirements and very
  /// slightly improves performance.
  ///
  /// It would be very easy to introduce unexpected side-effects as a result of
  /// this aliasing if it were permitted to request different but intersecting
  /// sets of opcodes but that is difficult to keep track of. It is therefore an
  /// error to request the same opcode twice using this API, to request an
  /// opcode that already has definitions, or to use the single-opcode API on an
  /// opcode that has already been requested by this API.
  LegalizeRuleSet &
  getActionDefinitionsBuilder(std::initializer_list<unsigned> Opcodes);
  void aliasActionDefinitions(unsigned OpcodeTo, unsigned OpcodeFrom);

  /// Determine what action should be taken to legalize the described
  /// instruction. Requires computeTables to have been called.
  ///
  /// \returns a description of the next legalization step to perform.
  LegalizeActionStep getAction(const LegalityQuery &Query) const;

  /// Determine what action should be taken to legalize the given generic
  /// instruction.
  ///
  /// \returns a description of the next legalization step to perform.
  LegalizeActionStep getAction(const MachineInstr &MI,
                               const MachineRegisterInfo &MRI) const;

  bool isLegal(const MachineInstr &MI, const MachineRegisterInfo &MRI) const;

  virtual bool legalizeCustom(MachineInstr &MI, MachineRegisterInfo &MRI,
                              MachineIRBuilder &MIRBuilder,
                              GISelChangeObserver &Observer) const;

private:
  /// Determine what action should be taken to legalize the given generic
  /// instruction opcode, type-index and type. Requires computeTables to have
  /// been called.
  ///
  /// \returns a pair consisting of the kind of legalization that should be
  /// performed and the destination type.
  std::pair<LegalizeAction, LLT>
  getAspectAction(const InstrAspect &Aspect) const;

  /// The SizeAndActionsVec is a representation mapping between all natural
  /// numbers and an Action. The natural number represents the bit size of
  /// the InstrAspect. For example, for a target with native support for 32-bit
  /// and 64-bit additions, you'd express that as:
  /// setScalarAction(G_ADD, 0,
  ///           {{1, WidenScalar},  // bit sizes [ 1, 31[
  ///            {32, Legal},       // bit sizes [32, 33[
  ///            {33, WidenScalar}, // bit sizes [33, 64[
  ///            {64, Legal},       // bit sizes [64, 65[
  ///            {65, NarrowScalar} // bit sizes [65, +inf[
  ///           });
  /// It may be that only 64-bit pointers are supported on your target:
  /// setPointerAction(G_GEP, 0, LLT:pointer(1),
  ///           {{1, Unsupported},  // bit sizes [ 1, 63[
  ///            {64, Legal},       // bit sizes [64, 65[
  ///            {65, Unsupported}, // bit sizes [65, +inf[
  ///           });
  void setScalarAction(const unsigned Opcode, const unsigned TypeIndex,
                       const SizeAndActionsVec &SizeAndActions) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    SmallVector<SizeAndActionsVec, 1> &Actions = ScalarActions[OpcodeIdx];
    setActions(TypeIndex, Actions, SizeAndActions);
  }
  void setPointerAction(const unsigned Opcode, const unsigned TypeIndex,
                        const unsigned AddressSpace,
                        const SizeAndActionsVec &SizeAndActions) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (AddrSpace2PointerActions[OpcodeIdx].find(AddressSpace) ==
        AddrSpace2PointerActions[OpcodeIdx].end())
      AddrSpace2PointerActions[OpcodeIdx][AddressSpace] = {{}};
    SmallVector<SizeAndActionsVec, 1> &Actions =
        AddrSpace2PointerActions[OpcodeIdx].find(AddressSpace)->second;
    setActions(TypeIndex, Actions, SizeAndActions);
  }

  /// If an operation on a given vector type (say <M x iN>) isn't explicitly
  /// specified, we proceed in 2 stages. First we legalize the underlying scalar
  /// (so that there's at least one legal vector with that scalar), then we
  /// adjust the number of elements in the vector so that it is legal. The
  /// desired action in the first step is controlled by this function.
  void setScalarInVectorAction(const unsigned Opcode, const unsigned TypeIndex,
                               const SizeAndActionsVec &SizeAndActions) {
    unsigned OpcodeIdx = Opcode - FirstOp;
    SmallVector<SizeAndActionsVec, 1> &Actions =
        ScalarInVectorActions[OpcodeIdx];
    setActions(TypeIndex, Actions, SizeAndActions);
  }

  /// See also setScalarInVectorAction.
  /// This function let's you specify the number of elements in a vector that
  /// are legal for a legal element size.
  void setVectorNumElementAction(const unsigned Opcode,
                                 const unsigned TypeIndex,
                                 const unsigned ElementSize,
                                 const SizeAndActionsVec &SizeAndActions) {
    const unsigned OpcodeIdx = Opcode - FirstOp;
    if (NumElements2Actions[OpcodeIdx].find(ElementSize) ==
        NumElements2Actions[OpcodeIdx].end())
      NumElements2Actions[OpcodeIdx][ElementSize] = {{}};
    SmallVector<SizeAndActionsVec, 1> &Actions =
        NumElements2Actions[OpcodeIdx].find(ElementSize)->second;
    setActions(TypeIndex, Actions, SizeAndActions);
  }

  /// A partial SizeAndActionsVec potentially doesn't cover all bit sizes,
  /// i.e. it's OK if it doesn't start from size 1.
  static void checkPartialSizeAndActionsVector(const SizeAndActionsVec& v) {
    using namespace LegalizeActions;
#ifndef NDEBUG
    // The sizes should be in increasing order
    int prev_size = -1;
    for(auto SizeAndAction: v) {
      assert(SizeAndAction.first > prev_size);
      prev_size = SizeAndAction.first;
    }
    // - for every Widen action, there should be a larger bitsize that
    //   can be legalized towards (e.g. Legal, Lower, Libcall or Custom
    //   action).
    // - for every Narrow action, there should be a smaller bitsize that
    //   can be legalized towards.
    int SmallestNarrowIdx = -1;
    int LargestWidenIdx = -1;
    int SmallestLegalizableToSameSizeIdx = -1;
    int LargestLegalizableToSameSizeIdx = -1;
    for(size_t i=0; i<v.size(); ++i) {
      switch (v[i].second) {
        case FewerElements:
        case NarrowScalar:
          if (SmallestNarrowIdx == -1)
            SmallestNarrowIdx = i;
          break;
        case WidenScalar:
        case MoreElements:
          LargestWidenIdx = i;
          break;
        case Unsupported:
          break;
        default:
          if (SmallestLegalizableToSameSizeIdx == -1)
            SmallestLegalizableToSameSizeIdx = i;
          LargestLegalizableToSameSizeIdx = i;
      }
    }
    if (SmallestNarrowIdx != -1) {
      assert(SmallestLegalizableToSameSizeIdx != -1);
      assert(SmallestNarrowIdx > SmallestLegalizableToSameSizeIdx);
    }
    if (LargestWidenIdx != -1)
      assert(LargestWidenIdx < LargestLegalizableToSameSizeIdx);
#endif
  }

  /// A full SizeAndActionsVec must cover all bit sizes, i.e. must start with
  /// from size 1.
  static void checkFullSizeAndActionsVector(const SizeAndActionsVec& v) {
#ifndef NDEBUG
    // Data structure invariant: The first bit size must be size 1.
    assert(v.size() >= 1);
    assert(v[0].first == 1);
    checkPartialSizeAndActionsVector(v);
#endif
  }

  /// Sets actions for all bit sizes on a particular generic opcode, type
  /// index and scalar or pointer type.
  void setActions(unsigned TypeIndex,
                  SmallVector<SizeAndActionsVec, 1> &Actions,
                  const SizeAndActionsVec &SizeAndActions) {
    checkFullSizeAndActionsVector(SizeAndActions);
    if (Actions.size() <= TypeIndex)
      Actions.resize(TypeIndex + 1);
    Actions[TypeIndex] = SizeAndActions;
  }

  static SizeAndAction findAction(const SizeAndActionsVec &Vec,
                                  const uint32_t Size);

  /// Returns the next action needed to get the scalar or pointer type closer
  /// to being legal
  /// E.g. findLegalAction({G_REM, 13}) should return
  /// (WidenScalar, 32). After that, findLegalAction({G_REM, 32}) will
  /// probably be called, which should return (Lower, 32).
  /// This is assuming the setScalarAction on G_REM was something like:
  /// setScalarAction(G_REM, 0,
  ///           {{1, WidenScalar},  // bit sizes [ 1, 31[
  ///            {32, Lower},       // bit sizes [32, 33[
  ///            {33, NarrowScalar} // bit sizes [65, +inf[
  ///           });
  std::pair<LegalizeAction, LLT>
  findScalarLegalAction(const InstrAspect &Aspect) const;

  /// Returns the next action needed towards legalizing the vector type.
  std::pair<LegalizeAction, LLT>
  findVectorLegalAction(const InstrAspect &Aspect) const;

  static const int FirstOp = TargetOpcode::PRE_ISEL_GENERIC_OPCODE_START;
  static const int LastOp = TargetOpcode::PRE_ISEL_GENERIC_OPCODE_END;

  // Data structures used temporarily during construction of legality data:
  using TypeMap = DenseMap<LLT, LegalizeAction>;
  SmallVector<TypeMap, 1> SpecifiedActions[LastOp - FirstOp + 1];
  SmallVector<SizeChangeStrategy, 1>
      ScalarSizeChangeStrategies[LastOp - FirstOp + 1];
  SmallVector<SizeChangeStrategy, 1>
      VectorElementSizeChangeStrategies[LastOp - FirstOp + 1];
  bool TablesInitialized;

  // Data structures used by getAction:
  SmallVector<SizeAndActionsVec, 1> ScalarActions[LastOp - FirstOp + 1];
  SmallVector<SizeAndActionsVec, 1> ScalarInVectorActions[LastOp - FirstOp + 1];
  std::unordered_map<uint16_t, SmallVector<SizeAndActionsVec, 1>>
      AddrSpace2PointerActions[LastOp - FirstOp + 1];
  std::unordered_map<uint16_t, SmallVector<SizeAndActionsVec, 1>>
      NumElements2Actions[LastOp - FirstOp + 1];

  LegalizeRuleSet RulesForOpcode[LastOp - FirstOp + 1];
};

#ifndef NDEBUG
/// Checks that MIR is fully legal, returns an illegal instruction if it's not,
/// nullptr otherwise
const MachineInstr *machineFunctionIsIllegal(const MachineFunction &MF);
#endif

} // end namespace llvm.

#endif // LLVM_CODEGEN_GLOBALISEL_LEGALIZERINFO_H
