//===- TaintManager.h - Managing taint --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides APIs for adding, removing, querying symbol taint.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_TAINTMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_TAINTMANAGER_H

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/TaintTag.h"
#include "llvm/ADT/ImmutableMap.h"

namespace clang {
namespace ento {

/// The GDM component containing the tainted root symbols. We lazily infer the
/// taint of the dependent symbols. Currently, this is a map from a symbol to
/// tag kind. TODO: Should support multiple tag kinds.
// FIXME: This does not use the nice trait macros because it must be accessible
// from multiple translation units.
struct TaintMap {};

using TaintMapImpl = llvm::ImmutableMap<SymbolRef, TaintTagType>;

template<> struct ProgramStateTrait<TaintMap>
    :  public ProgramStatePartialTrait<TaintMapImpl> {
  static void *GDMIndex();
};

/// The GDM component mapping derived symbols' parent symbols to their
/// underlying regions. This is used to efficiently check whether a symbol is
/// tainted when it represents a sub-region of a tainted symbol.
struct DerivedSymTaint {};

using DerivedSymTaintImpl = llvm::ImmutableMap<SymbolRef, TaintedSubRegions>;

template<> struct ProgramStateTrait<DerivedSymTaint>
    :  public ProgramStatePartialTrait<DerivedSymTaintImpl> {
  static void *GDMIndex();
};

class TaintManager {
  TaintManager() = default;
};

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_TAINTMANAGER_H
