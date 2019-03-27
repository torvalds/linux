//===--- VariantValue.cpp - Polymorphic value type -*- C++ -*-===/
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Polymorphic value type.
///
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/Dynamic/VariantValue.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/STLExtras.h"

namespace clang {
namespace ast_matchers {
namespace dynamic {

std::string ArgKind::asString() const {
  switch (getArgKind()) {
  case AK_Matcher:
    return (Twine("Matcher<") + MatcherKind.asStringRef() + ">").str();
  case AK_Boolean:
    return "boolean";
  case AK_Double:
    return "double";
  case AK_Unsigned:
    return "unsigned";
  case AK_String:
    return "string";
  }
  llvm_unreachable("unhandled ArgKind");
}

bool ArgKind::isConvertibleTo(ArgKind To, unsigned *Specificity) const {
  if (K != To.K)
    return false;
  if (K != AK_Matcher) {
    if (Specificity)
      *Specificity = 1;
    return true;
  }
  unsigned Distance;
  if (!MatcherKind.isBaseOf(To.MatcherKind, &Distance))
    return false;

  if (Specificity)
    *Specificity = 100 - Distance;
  return true;
}

bool
VariantMatcher::MatcherOps::canConstructFrom(const DynTypedMatcher &Matcher,
                                             bool &IsExactMatch) const {
  IsExactMatch = Matcher.getSupportedKind().isSame(NodeKind);
  return Matcher.canConvertTo(NodeKind);
}

llvm::Optional<DynTypedMatcher>
VariantMatcher::MatcherOps::constructVariadicOperator(
    DynTypedMatcher::VariadicOperator Op,
    ArrayRef<VariantMatcher> InnerMatchers) const {
  std::vector<DynTypedMatcher> DynMatchers;
  for (const auto &InnerMatcher : InnerMatchers) {
    // Abort if any of the inner matchers can't be converted to
    // Matcher<T>.
    if (!InnerMatcher.Value)
      return llvm::None;
    llvm::Optional<DynTypedMatcher> Inner =
        InnerMatcher.Value->getTypedMatcher(*this);
    if (!Inner)
      return llvm::None;
    DynMatchers.push_back(*Inner);
  }
  return DynTypedMatcher::constructVariadic(Op, NodeKind, DynMatchers);
}

VariantMatcher::Payload::~Payload() {}

class VariantMatcher::SinglePayload : public VariantMatcher::Payload {
public:
  SinglePayload(const DynTypedMatcher &Matcher) : Matcher(Matcher) {}

  llvm::Optional<DynTypedMatcher> getSingleMatcher() const override {
    return Matcher;
  }

  std::string getTypeAsString() const override {
    return (Twine("Matcher<") + Matcher.getSupportedKind().asStringRef() + ">")
        .str();
  }

  llvm::Optional<DynTypedMatcher>
  getTypedMatcher(const MatcherOps &Ops) const override {
    bool Ignore;
    if (Ops.canConstructFrom(Matcher, Ignore))
      return Matcher;
    return llvm::None;
  }

  bool isConvertibleTo(ast_type_traits::ASTNodeKind Kind,
                       unsigned *Specificity) const override {
    return ArgKind(Matcher.getSupportedKind())
        .isConvertibleTo(Kind, Specificity);
  }

private:
  const DynTypedMatcher Matcher;
};

class VariantMatcher::PolymorphicPayload : public VariantMatcher::Payload {
public:
  PolymorphicPayload(std::vector<DynTypedMatcher> MatchersIn)
      : Matchers(std::move(MatchersIn)) {}

  ~PolymorphicPayload() override {}

  llvm::Optional<DynTypedMatcher> getSingleMatcher() const override {
    if (Matchers.size() != 1)
      return llvm::Optional<DynTypedMatcher>();
    return Matchers[0];
  }

  std::string getTypeAsString() const override {
    std::string Inner;
    for (size_t i = 0, e = Matchers.size(); i != e; ++i) {
      if (i != 0)
        Inner += "|";
      Inner += Matchers[i].getSupportedKind().asStringRef();
    }
    return (Twine("Matcher<") + Inner + ">").str();
  }

  llvm::Optional<DynTypedMatcher>
  getTypedMatcher(const MatcherOps &Ops) const override {
    bool FoundIsExact = false;
    const DynTypedMatcher *Found = nullptr;
    int NumFound = 0;
    for (size_t i = 0, e = Matchers.size(); i != e; ++i) {
      bool IsExactMatch;
      if (Ops.canConstructFrom(Matchers[i], IsExactMatch)) {
        if (Found) {
          if (FoundIsExact) {
            assert(!IsExactMatch && "We should not have two exact matches.");
            continue;
          }
        }
        Found = &Matchers[i];
        FoundIsExact = IsExactMatch;
        ++NumFound;
      }
    }
    // We only succeed if we found exactly one, or if we found an exact match.
    if (Found && (FoundIsExact || NumFound == 1))
      return *Found;
    return llvm::None;
  }

  bool isConvertibleTo(ast_type_traits::ASTNodeKind Kind,
                       unsigned *Specificity) const override {
    unsigned MaxSpecificity = 0;
    for (const DynTypedMatcher &Matcher : Matchers) {
      unsigned ThisSpecificity;
      if (ArgKind(Matcher.getSupportedKind())
              .isConvertibleTo(Kind, &ThisSpecificity)) {
        MaxSpecificity = std::max(MaxSpecificity, ThisSpecificity);
      }
    }
    if (Specificity)
      *Specificity = MaxSpecificity;
    return MaxSpecificity > 0;
  }

  const std::vector<DynTypedMatcher> Matchers;
};

class VariantMatcher::VariadicOpPayload : public VariantMatcher::Payload {
public:
  VariadicOpPayload(DynTypedMatcher::VariadicOperator Op,
                    std::vector<VariantMatcher> Args)
      : Op(Op), Args(std::move(Args)) {}

  llvm::Optional<DynTypedMatcher> getSingleMatcher() const override {
    return llvm::Optional<DynTypedMatcher>();
  }

  std::string getTypeAsString() const override {
    std::string Inner;
    for (size_t i = 0, e = Args.size(); i != e; ++i) {
      if (i != 0)
        Inner += "&";
      Inner += Args[i].getTypeAsString();
    }
    return Inner;
  }

  llvm::Optional<DynTypedMatcher>
  getTypedMatcher(const MatcherOps &Ops) const override {
    return Ops.constructVariadicOperator(Op, Args);
  }

  bool isConvertibleTo(ast_type_traits::ASTNodeKind Kind,
                       unsigned *Specificity) const override {
    for (const VariantMatcher &Matcher : Args) {
      if (!Matcher.isConvertibleTo(Kind, Specificity))
        return false;
    }
    return true;
  }

private:
  const DynTypedMatcher::VariadicOperator Op;
  const std::vector<VariantMatcher> Args;
};

VariantMatcher::VariantMatcher() {}

VariantMatcher VariantMatcher::SingleMatcher(const DynTypedMatcher &Matcher) {
  return VariantMatcher(std::make_shared<SinglePayload>(Matcher));
}

VariantMatcher
VariantMatcher::PolymorphicMatcher(std::vector<DynTypedMatcher> Matchers) {
  return VariantMatcher(
      std::make_shared<PolymorphicPayload>(std::move(Matchers)));
}

VariantMatcher VariantMatcher::VariadicOperatorMatcher(
    DynTypedMatcher::VariadicOperator Op,
    std::vector<VariantMatcher> Args) {
  return VariantMatcher(
      std::make_shared<VariadicOpPayload>(Op, std::move(Args)));
}

llvm::Optional<DynTypedMatcher> VariantMatcher::getSingleMatcher() const {
  return Value ? Value->getSingleMatcher() : llvm::Optional<DynTypedMatcher>();
}

void VariantMatcher::reset() { Value.reset(); }

std::string VariantMatcher::getTypeAsString() const {
  if (Value) return Value->getTypeAsString();
  return "<Nothing>";
}

VariantValue::VariantValue(const VariantValue &Other) : Type(VT_Nothing) {
  *this = Other;
}

VariantValue::VariantValue(bool Boolean) : Type(VT_Nothing) {
  setBoolean(Boolean);
}

VariantValue::VariantValue(double Double) : Type(VT_Nothing) {
  setDouble(Double);
}

VariantValue::VariantValue(unsigned Unsigned) : Type(VT_Nothing) {
  setUnsigned(Unsigned);
}

VariantValue::VariantValue(StringRef String) : Type(VT_Nothing) {
  setString(String);
}

VariantValue::VariantValue(const VariantMatcher &Matcher) : Type(VT_Nothing) {
  setMatcher(Matcher);
}

VariantValue::~VariantValue() { reset(); }

VariantValue &VariantValue::operator=(const VariantValue &Other) {
  if (this == &Other) return *this;
  reset();
  switch (Other.Type) {
  case VT_Boolean:
    setBoolean(Other.getBoolean());
    break;
  case VT_Double:
    setDouble(Other.getDouble());
    break;
  case VT_Unsigned:
    setUnsigned(Other.getUnsigned());
    break;
  case VT_String:
    setString(Other.getString());
    break;
  case VT_Matcher:
    setMatcher(Other.getMatcher());
    break;
  case VT_Nothing:
    Type = VT_Nothing;
    break;
  }
  return *this;
}

void VariantValue::reset() {
  switch (Type) {
  case VT_String:
    delete Value.String;
    break;
  case VT_Matcher:
    delete Value.Matcher;
    break;
  // Cases that do nothing.
  case VT_Boolean:
  case VT_Double:
  case VT_Unsigned:
  case VT_Nothing:
    break;
  }
  Type = VT_Nothing;
}

bool VariantValue::isBoolean() const {
  return Type == VT_Boolean;
}

bool VariantValue::getBoolean() const {
  assert(isBoolean());
  return Value.Boolean;
}

void VariantValue::setBoolean(bool NewValue) {
  reset();
  Type = VT_Boolean;
  Value.Boolean = NewValue;
}

bool VariantValue::isDouble() const {
  return Type == VT_Double;
}

double VariantValue::getDouble() const {
  assert(isDouble());
  return Value.Double;
}

void VariantValue::setDouble(double NewValue) {
  reset();
  Type = VT_Double;
  Value.Double = NewValue;
}

bool VariantValue::isUnsigned() const {
  return Type == VT_Unsigned;
}

unsigned VariantValue::getUnsigned() const {
  assert(isUnsigned());
  return Value.Unsigned;
}

void VariantValue::setUnsigned(unsigned NewValue) {
  reset();
  Type = VT_Unsigned;
  Value.Unsigned = NewValue;
}

bool VariantValue::isString() const {
  return Type == VT_String;
}

const std::string &VariantValue::getString() const {
  assert(isString());
  return *Value.String;
}

void VariantValue::setString(StringRef NewValue) {
  reset();
  Type = VT_String;
  Value.String = new std::string(NewValue);
}

bool VariantValue::isMatcher() const {
  return Type == VT_Matcher;
}

const VariantMatcher &VariantValue::getMatcher() const {
  assert(isMatcher());
  return *Value.Matcher;
}

void VariantValue::setMatcher(const VariantMatcher &NewValue) {
  reset();
  Type = VT_Matcher;
  Value.Matcher = new VariantMatcher(NewValue);
}

bool VariantValue::isConvertibleTo(ArgKind Kind, unsigned *Specificity) const {
  switch (Kind.getArgKind()) {
  case ArgKind::AK_Boolean:
    if (!isBoolean())
      return false;
    *Specificity = 1;
    return true;

  case ArgKind::AK_Double:
    if (!isDouble())
      return false;
    *Specificity = 1;
    return true;

  case ArgKind::AK_Unsigned:
    if (!isUnsigned())
      return false;
    *Specificity = 1;
    return true;

  case ArgKind::AK_String:
    if (!isString())
      return false;
    *Specificity = 1;
    return true;

  case ArgKind::AK_Matcher:
    if (!isMatcher())
      return false;
    return getMatcher().isConvertibleTo(Kind.getMatcherKind(), Specificity);
  }
  llvm_unreachable("Invalid Type");
}

bool VariantValue::isConvertibleTo(ArrayRef<ArgKind> Kinds,
                                   unsigned *Specificity) const {
  unsigned MaxSpecificity = 0;
  for (const ArgKind& Kind : Kinds) {
    unsigned ThisSpecificity;
    if (!isConvertibleTo(Kind, &ThisSpecificity))
      continue;
    MaxSpecificity = std::max(MaxSpecificity, ThisSpecificity);
  }
  if (Specificity && MaxSpecificity > 0) {
    *Specificity = MaxSpecificity;
  }
  return MaxSpecificity > 0;
}

std::string VariantValue::getTypeAsString() const {
  switch (Type) {
  case VT_String: return "String";
  case VT_Matcher: return getMatcher().getTypeAsString();
  case VT_Boolean: return "Boolean";
  case VT_Double: return "Double";
  case VT_Unsigned: return "Unsigned";
  case VT_Nothing: return "Nothing";
  }
  llvm_unreachable("Invalid Type");
}

} // end namespace dynamic
} // end namespace ast_matchers
} // end namespace clang
