//======- ParsedAttr.cpp --------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the ParsedAttr class implementation
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/ParsedAttr.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/AttrSubjectMatchRules.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstddef>
#include <utility>

using namespace clang;

IdentifierLoc *IdentifierLoc::create(ASTContext &Ctx, SourceLocation Loc,
                                     IdentifierInfo *Ident) {
  IdentifierLoc *Result = new (Ctx) IdentifierLoc;
  Result->Loc = Loc;
  Result->Ident = Ident;
  return Result;
}

size_t ParsedAttr::allocated_size() const {
  if (IsAvailability) return AttributeFactory::AvailabilityAllocSize;
  else if (IsTypeTagForDatatype)
    return AttributeFactory::TypeTagForDatatypeAllocSize;
  else if (IsProperty)
    return AttributeFactory::PropertyAllocSize;
  else if (HasParsedType)
    return totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                            detail::TypeTagForDatatypeData, ParsedType,
                            detail::PropertyData>(0, 0, 0, 1, 0);
  return totalSizeToAlloc<ArgsUnion, detail::AvailabilityData,
                          detail::TypeTagForDatatypeData, ParsedType,
                          detail::PropertyData>(NumArgs, 0, 0, 0, 0);
}

AttributeFactory::AttributeFactory() {
  // Go ahead and configure all the inline capacity.  This is just a memset.
  FreeLists.resize(InlineFreeListsCapacity);
}
AttributeFactory::~AttributeFactory() = default;

static size_t getFreeListIndexForSize(size_t size) {
  assert(size >= sizeof(ParsedAttr));
  assert((size % sizeof(void*)) == 0);
  return ((size - sizeof(ParsedAttr)) / sizeof(void *));
}

void *AttributeFactory::allocate(size_t size) {
  // Check for a previously reclaimed attribute.
  size_t index = getFreeListIndexForSize(size);
  if (index < FreeLists.size() && !FreeLists[index].empty()) {
    ParsedAttr *attr = FreeLists[index].back();
    FreeLists[index].pop_back();
    return attr;
  }

  // Otherwise, allocate something new.
  return Alloc.Allocate(size, alignof(AttributeFactory));
}

void AttributeFactory::deallocate(ParsedAttr *Attr) {
  size_t size = Attr->allocated_size();
  size_t freeListIndex = getFreeListIndexForSize(size);

  // Expand FreeLists to the appropriate size, if required.
  if (freeListIndex >= FreeLists.size())
    FreeLists.resize(freeListIndex + 1);

#ifndef NDEBUG
  // In debug mode, zero out the attribute to help find memory overwriting.
  memset(Attr, 0, size);
#endif

  // Add 'Attr' to the appropriate free-list.
  FreeLists[freeListIndex].push_back(Attr);
}

void AttributeFactory::reclaimPool(AttributePool &cur) {
  for (ParsedAttr *AL : cur.Attrs)
    deallocate(AL);
}

void AttributePool::takePool(AttributePool &pool) {
  Attrs.insert(Attrs.end(), pool.Attrs.begin(), pool.Attrs.end());
  pool.Attrs.clear();
}

#include "clang/Sema/AttrParsedAttrKinds.inc"

static StringRef normalizeAttrScopeName(StringRef ScopeName,
                                        ParsedAttr::Syntax SyntaxUsed) {
  // Normalize the "__gnu__" scope name to be "gnu" and the "_Clang" scope name
  // to be "clang".
  if (SyntaxUsed == ParsedAttr::AS_CXX11 ||
    SyntaxUsed == ParsedAttr::AS_C2x) {
    if (ScopeName == "__gnu__")
      ScopeName = "gnu";
    else if (ScopeName == "_Clang")
      ScopeName = "clang";
  }
  return ScopeName;
}

static StringRef normalizeAttrName(StringRef AttrName,
                                   StringRef NormalizedScopeName,
                                   ParsedAttr::Syntax SyntaxUsed) {
  // Normalize the attribute name, __foo__ becomes foo. This is only allowable
  // for GNU attributes, and attributes using the double square bracket syntax.
  bool ShouldNormalize =
      SyntaxUsed == ParsedAttr::AS_GNU ||
      ((SyntaxUsed == ParsedAttr::AS_CXX11 ||
        SyntaxUsed == ParsedAttr::AS_C2x) &&
       (NormalizedScopeName == "gnu" || NormalizedScopeName == "clang"));
  if (ShouldNormalize && AttrName.size() >= 4 && AttrName.startswith("__") &&
      AttrName.endswith("__"))
    AttrName = AttrName.slice(2, AttrName.size() - 2);

  return AttrName;
}

ParsedAttr::Kind ParsedAttr::getKind(const IdentifierInfo *Name,
                                     const IdentifierInfo *ScopeName,
                                     Syntax SyntaxUsed) {
  StringRef AttrName = Name->getName();

  SmallString<64> FullName;
  if (ScopeName)
    FullName += normalizeAttrScopeName(ScopeName->getName(), SyntaxUsed);

  AttrName = normalizeAttrName(AttrName, FullName, SyntaxUsed);

  // Ensure that in the case of C++11 attributes, we look for '::foo' if it is
  // unscoped.
  if (ScopeName || SyntaxUsed == AS_CXX11 || SyntaxUsed == AS_C2x)
    FullName += "::";
  FullName += AttrName;

  return ::getAttrKind(FullName, SyntaxUsed);
}

unsigned ParsedAttr::getAttributeSpellingListIndex() const {
  // Both variables will be used in tablegen generated
  // attribute spell list index matching code.
  auto Syntax = static_cast<ParsedAttr::Syntax>(SyntaxUsed);
  StringRef Scope =
      ScopeName ? normalizeAttrScopeName(ScopeName->getName(), Syntax) : "";
  StringRef Name = normalizeAttrName(AttrName->getName(), Scope, Syntax);

#include "clang/Sema/AttrSpellingListIndex.inc"

}

struct ParsedAttrInfo {
  unsigned NumArgs : 4;
  unsigned OptArgs : 4;
  unsigned HasCustomParsing : 1;
  unsigned IsTargetSpecific : 1;
  unsigned IsType : 1;
  unsigned IsStmt : 1;
  unsigned IsKnownToGCC : 1;
  unsigned IsSupportedByPragmaAttribute : 1;

  bool (*DiagAppertainsToDecl)(Sema &S, const ParsedAttr &Attr, const Decl *);
  bool (*DiagLangOpts)(Sema &S, const ParsedAttr &Attr);
  bool (*ExistsInTarget)(const TargetInfo &Target);
  unsigned (*SpellingIndexToSemanticSpelling)(const ParsedAttr &Attr);
  void (*GetPragmaAttributeMatchRules)(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>> &Rules,
      const LangOptions &LangOpts);
};

namespace {

#include "clang/Sema/AttrParsedAttrImpl.inc"

} // namespace

static const ParsedAttrInfo &getInfo(const ParsedAttr &A) {
  return AttrInfoMap[A.getKind()];
}

unsigned ParsedAttr::getMinArgs() const { return getInfo(*this).NumArgs; }

unsigned ParsedAttr::getMaxArgs() const {
  return getMinArgs() + getInfo(*this).OptArgs;
}

bool ParsedAttr::hasCustomParsing() const {
  return getInfo(*this).HasCustomParsing;
}

bool ParsedAttr::diagnoseAppertainsTo(Sema &S, const Decl *D) const {
  return getInfo(*this).DiagAppertainsToDecl(S, *this, D);
}

bool ParsedAttr::appliesToDecl(const Decl *D,
                               attr::SubjectMatchRule MatchRule) const {
  return checkAttributeMatchRuleAppliesTo(D, MatchRule);
}

void ParsedAttr::getMatchRules(
    const LangOptions &LangOpts,
    SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>> &MatchRules)
    const {
  return getInfo(*this).GetPragmaAttributeMatchRules(MatchRules, LangOpts);
}

bool ParsedAttr::diagnoseLangOpts(Sema &S) const {
  return getInfo(*this).DiagLangOpts(S, *this);
}

bool ParsedAttr::isTargetSpecificAttr() const {
  return getInfo(*this).IsTargetSpecific;
}

bool ParsedAttr::isTypeAttr() const { return getInfo(*this).IsType; }

bool ParsedAttr::isStmtAttr() const { return getInfo(*this).IsStmt; }

bool ParsedAttr::existsInTarget(const TargetInfo &Target) const {
  return getInfo(*this).ExistsInTarget(Target);
}

bool ParsedAttr::isKnownToGCC() const { return getInfo(*this).IsKnownToGCC; }

bool ParsedAttr::isSupportedByPragmaAttribute() const {
  return getInfo(*this).IsSupportedByPragmaAttribute;
}

unsigned ParsedAttr::getSemanticSpelling() const {
  return getInfo(*this).SpellingIndexToSemanticSpelling(*this);
}

bool ParsedAttr::hasVariadicArg() const {
  // If the attribute has the maximum number of optional arguments, we will
  // claim that as being variadic. If we someday get an attribute that
  // legitimately bumps up against that maximum, we can use another bit to track
  // whether it's truly variadic or not.
  return getInfo(*this).OptArgs == 15;
}
