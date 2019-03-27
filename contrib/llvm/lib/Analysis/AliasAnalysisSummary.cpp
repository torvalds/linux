#include "AliasAnalysisSummary.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace cflaa {

namespace {
const unsigned AttrEscapedIndex = 0;
const unsigned AttrUnknownIndex = 1;
const unsigned AttrGlobalIndex = 2;
const unsigned AttrCallerIndex = 3;
const unsigned AttrFirstArgIndex = 4;
const unsigned AttrLastArgIndex = NumAliasAttrs;
const unsigned AttrMaxNumArgs = AttrLastArgIndex - AttrFirstArgIndex;

// It would be *slightly* prettier if we changed these to AliasAttrs, but it
// seems that both GCC and MSVC emit dynamic initializers for const bitsets.
using AliasAttr = unsigned;
const AliasAttr AttrNone = 0;
const AliasAttr AttrEscaped = 1 << AttrEscapedIndex;
const AliasAttr AttrUnknown = 1 << AttrUnknownIndex;
const AliasAttr AttrGlobal = 1 << AttrGlobalIndex;
const AliasAttr AttrCaller = 1 << AttrCallerIndex;
const AliasAttr ExternalAttrMask = AttrEscaped | AttrUnknown | AttrGlobal;
}

AliasAttrs getAttrNone() { return AttrNone; }

AliasAttrs getAttrUnknown() { return AttrUnknown; }
bool hasUnknownAttr(AliasAttrs Attr) { return Attr.test(AttrUnknownIndex); }

AliasAttrs getAttrCaller() { return AttrCaller; }
bool hasCallerAttr(AliasAttrs Attr) { return Attr.test(AttrCaller); }
bool hasUnknownOrCallerAttr(AliasAttrs Attr) {
  return Attr.test(AttrUnknownIndex) || Attr.test(AttrCallerIndex);
}

AliasAttrs getAttrEscaped() { return AttrEscaped; }
bool hasEscapedAttr(AliasAttrs Attr) { return Attr.test(AttrEscapedIndex); }

static AliasAttr argNumberToAttr(unsigned ArgNum) {
  if (ArgNum >= AttrMaxNumArgs)
    return AttrUnknown;
  // N.B. MSVC complains if we use `1U` here, since AliasAttr' ctor takes
  // an unsigned long long.
  return AliasAttr(1ULL << (ArgNum + AttrFirstArgIndex));
}

AliasAttrs getGlobalOrArgAttrFromValue(const Value &Val) {
  if (isa<GlobalValue>(Val))
    return AttrGlobal;

  if (auto *Arg = dyn_cast<Argument>(&Val))
    // Only pointer arguments should have the argument attribute,
    // because things can't escape through scalars without us seeing a
    // cast, and thus, interaction with them doesn't matter.
    if (!Arg->hasNoAliasAttr() && Arg->getType()->isPointerTy())
      return argNumberToAttr(Arg->getArgNo());
  return AttrNone;
}

bool isGlobalOrArgAttr(AliasAttrs Attr) {
  return Attr.reset(AttrEscapedIndex)
      .reset(AttrUnknownIndex)
      .reset(AttrCallerIndex)
      .any();
}

AliasAttrs getExternallyVisibleAttrs(AliasAttrs Attr) {
  return Attr & AliasAttrs(ExternalAttrMask);
}

Optional<InstantiatedValue> instantiateInterfaceValue(InterfaceValue IValue,
                                                      CallSite CS) {
  auto Index = IValue.Index;
  auto Value = (Index == 0) ? CS.getInstruction() : CS.getArgument(Index - 1);
  if (Value->getType()->isPointerTy())
    return InstantiatedValue{Value, IValue.DerefLevel};
  return None;
}

Optional<InstantiatedRelation>
instantiateExternalRelation(ExternalRelation ERelation, CallSite CS) {
  auto From = instantiateInterfaceValue(ERelation.From, CS);
  if (!From)
    return None;
  auto To = instantiateInterfaceValue(ERelation.To, CS);
  if (!To)
    return None;
  return InstantiatedRelation{*From, *To, ERelation.Offset};
}

Optional<InstantiatedAttr> instantiateExternalAttribute(ExternalAttribute EAttr,
                                                        CallSite CS) {
  auto Value = instantiateInterfaceValue(EAttr.IValue, CS);
  if (!Value)
    return None;
  return InstantiatedAttr{*Value, EAttr.Attr};
}
}
}
