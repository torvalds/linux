//===- DynamicTypeMap.h - Dynamic type map ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides APIs for tracking dynamic type information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEMAP_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEMAP_H

#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicTypeInfo.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/ImmutableMap.h"
#include "clang/AST/Type.h"

namespace clang {
namespace ento {

class MemRegion;

/// The GDM component containing the dynamic type info. This is a map from a
/// symbol to its most likely type.
struct DynamicTypeMap {};

using DynamicTypeMapImpl =
    llvm::ImmutableMap<const MemRegion *, DynamicTypeInfo>;

template <>
struct ProgramStateTrait<DynamicTypeMap>
    : public ProgramStatePartialTrait<DynamicTypeMapImpl> {
  static void *GDMIndex();
};

/// Get dynamic type information for a region.
DynamicTypeInfo getDynamicTypeInfo(ProgramStateRef State,
                                   const MemRegion *Reg);

/// Set dynamic type information of the region; return the new state.
ProgramStateRef setDynamicTypeInfo(ProgramStateRef State, const MemRegion *Reg,
                                   DynamicTypeInfo NewTy);

/// Set dynamic type information of the region; return the new state.
inline ProgramStateRef setDynamicTypeInfo(ProgramStateRef State,
                                          const MemRegion *Reg, QualType NewTy,
                                          bool CanBeSubClassed = true) {
  return setDynamicTypeInfo(State, Reg,
                            DynamicTypeInfo(NewTy, CanBeSubClassed));
}

void printDynamicTypeInfo(ProgramStateRef State, raw_ostream &Out,
                          const char *NL, const char *Sep);

} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_DYNAMICTYPEMAP_H
