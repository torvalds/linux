//=== SelectorExtras.h - Helpers for checkers using selectors -----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_ANALYSIS_SELECTOREXTRAS_H
#define LLVM_CLANG_LIB_ANALYSIS_SELECTOREXTRAS_H

#include "clang/AST/ASTContext.h"

namespace clang {

template <typename... IdentifierInfos>
static inline Selector getKeywordSelector(ASTContext &Ctx,
                                          IdentifierInfos *... IIs) {
  static_assert(sizeof...(IdentifierInfos),
                "keyword selectors must have at least one argument");
  SmallVector<IdentifierInfo *, 10> II({&Ctx.Idents.get(IIs)...});

  return Ctx.Selectors.getSelector(II.size(), &II[0]);
}

template <typename... IdentifierInfos>
static inline void lazyInitKeywordSelector(Selector &Sel, ASTContext &Ctx,
                                           IdentifierInfos *... IIs) {
  if (!Sel.isNull())
    return;
  Sel = getKeywordSelector(Ctx, IIs...);
}

} // end namespace clang

#endif
