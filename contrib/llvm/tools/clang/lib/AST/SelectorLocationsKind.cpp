//===--- SelectorLocationsKind.cpp - Kind of selector locations -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Describes whether the identifier locations for a selector are "standard"
// or not.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/SelectorLocationsKind.h"
#include "clang/AST/Expr.h"

using namespace clang;

static SourceLocation getStandardSelLoc(unsigned Index,
                                        Selector Sel,
                                        bool WithArgSpace,
                                        SourceLocation ArgLoc,
                                        SourceLocation EndLoc) {
  unsigned NumSelArgs = Sel.getNumArgs();
  if (NumSelArgs == 0) {
    assert(Index == 0);
    if (EndLoc.isInvalid())
      return SourceLocation();
    IdentifierInfo *II = Sel.getIdentifierInfoForSlot(0);
    unsigned Len = II ? II->getLength() : 0;
    return EndLoc.getLocWithOffset(-Len);
  }

  assert(Index < NumSelArgs);
  if (ArgLoc.isInvalid())
    return SourceLocation();
  IdentifierInfo *II = Sel.getIdentifierInfoForSlot(Index);
  unsigned Len = /* selector id */ (II ? II->getLength() : 0) + /* ':' */ 1;
  if (WithArgSpace)
    ++Len;
  return ArgLoc.getLocWithOffset(-Len);
}

namespace {

template <typename T>
SourceLocation getArgLoc(T* Arg);

template <>
SourceLocation getArgLoc<Expr>(Expr *Arg) {
  return Arg->getBeginLoc();
}

template <>
SourceLocation getArgLoc<ParmVarDecl>(ParmVarDecl *Arg) {
  SourceLocation Loc = Arg->getBeginLoc();
  if (Loc.isInvalid())
    return Loc;
  // -1 to point to left paren of the method parameter's type.
  return Loc.getLocWithOffset(-1);
}

template <typename T>
SourceLocation getArgLoc(unsigned Index, ArrayRef<T*> Args) {
  return Index < Args.size() ? getArgLoc(Args[Index]) : SourceLocation();
}

template <typename T>
SelectorLocationsKind hasStandardSelLocs(Selector Sel,
                                         ArrayRef<SourceLocation> SelLocs,
                                         ArrayRef<T *> Args,
                                         SourceLocation EndLoc) {
  // Are selector locations in standard position with no space between args ?
  unsigned i;
  for (i = 0; i != SelLocs.size(); ++i) {
    if (SelLocs[i] != getStandardSelectorLoc(i, Sel, /*WithArgSpace=*/false,
                                             Args, EndLoc))
      break;
  }
  if (i == SelLocs.size())
    return SelLoc_StandardNoSpace;

  // Are selector locations in standard position with space between args ?
  for (i = 0; i != SelLocs.size(); ++i) {
    if (SelLocs[i] != getStandardSelectorLoc(i, Sel, /*WithArgSpace=*/true,
                                             Args, EndLoc))
      return SelLoc_NonStandard;
  }

  return SelLoc_StandardWithSpace;
}

} // anonymous namespace

SelectorLocationsKind
clang::hasStandardSelectorLocs(Selector Sel,
                               ArrayRef<SourceLocation> SelLocs,
                               ArrayRef<Expr *> Args,
                               SourceLocation EndLoc) {
  return hasStandardSelLocs(Sel, SelLocs, Args, EndLoc);
}

SourceLocation clang::getStandardSelectorLoc(unsigned Index,
                                             Selector Sel,
                                             bool WithArgSpace,
                                             ArrayRef<Expr *> Args,
                                             SourceLocation EndLoc) {
  return getStandardSelLoc(Index, Sel, WithArgSpace,
                           getArgLoc(Index, Args), EndLoc);
}

SelectorLocationsKind
clang::hasStandardSelectorLocs(Selector Sel,
                               ArrayRef<SourceLocation> SelLocs,
                               ArrayRef<ParmVarDecl *> Args,
                               SourceLocation EndLoc) {
  return hasStandardSelLocs(Sel, SelLocs, Args, EndLoc);
}

SourceLocation clang::getStandardSelectorLoc(unsigned Index,
                                             Selector Sel,
                                             bool WithArgSpace,
                                             ArrayRef<ParmVarDecl *> Args,
                                             SourceLocation EndLoc) {
  return getStandardSelLoc(Index, Sel, WithArgSpace,
                           getArgLoc(Index, Args), EndLoc);
}
