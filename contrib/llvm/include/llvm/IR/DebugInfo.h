//===- DebugInfo.h - Debug Information Helpers ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a bunch of datatypes that are useful for creating and
// walking debug info in LLVM IR form. They essentially provide wrappers around
// the information in the global variables that's needed when constructing the
// DWARF information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DEBUGINFO_H
#define LLVM_IR_DEBUGINFO_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/DebugInfoMetadata.h"

namespace llvm {

class DbgDeclareInst;
class DbgValueInst;
class Module;

/// Find subprogram that is enclosing this scope.
DISubprogram *getDISubprogram(const MDNode *Scope);

/// Strip debug info in the module if it exists.
///
/// To do this, we remove all calls to the debugger intrinsics and any named
/// metadata for debugging. We also remove debug locations for instructions.
/// Return true if module is modified.
bool StripDebugInfo(Module &M);
bool stripDebugInfo(Function &F);

/// Downgrade the debug info in a module to contain only line table information.
///
/// In order to convert debug info to what -gline-tables-only would have
/// created, this does the following:
///   1) Delete all debug intrinsics.
///   2) Delete all non-CU named metadata debug info nodes.
///   3) Create new DebugLocs for each instruction.
///   4) Create a new CU debug info, and similarly for every metadata node
///      that's reachable from the CU debug info.
///   All debug type metadata nodes are unreachable and garbage collected.
bool stripNonLineTableDebugInfo(Module &M);

/// Return Debug Info Metadata Version by checking module flags.
unsigned getDebugMetadataVersionFromModule(const Module &M);

/// Utility to find all debug info in a module.
///
/// DebugInfoFinder tries to list all debug info MDNodes used in a module. To
/// list debug info MDNodes used by an instruction, DebugInfoFinder uses
/// processDeclare, processValue and processLocation to handle DbgDeclareInst,
/// DbgValueInst and DbgLoc attached to instructions. processModule will go
/// through all DICompileUnits in llvm.dbg.cu and list debug info MDNodes
/// used by the CUs.
class DebugInfoFinder {
public:
  /// Process entire module and collect debug info anchors.
  void processModule(const Module &M);
  /// Process a single instruction and collect debug info anchors.
  void processInstruction(const Module &M, const Instruction &I);

  /// Process DbgDeclareInst.
  void processDeclare(const Module &M, const DbgDeclareInst *DDI);
  /// Process DbgValueInst.
  void processValue(const Module &M, const DbgValueInst *DVI);
  /// Process debug info location.
  void processLocation(const Module &M, const DILocation *Loc);

  /// Clear all lists.
  void reset();

private:
  void InitializeTypeMap(const Module &M);

  void processCompileUnit(DICompileUnit *CU);
  void processScope(DIScope *Scope);
  void processSubprogram(DISubprogram *SP);
  void processType(DIType *DT);
  bool addCompileUnit(DICompileUnit *CU);
  bool addGlobalVariable(DIGlobalVariableExpression *DIG);
  bool addScope(DIScope *Scope);
  bool addSubprogram(DISubprogram *SP);
  bool addType(DIType *DT);

public:
  using compile_unit_iterator =
      SmallVectorImpl<DICompileUnit *>::const_iterator;
  using subprogram_iterator = SmallVectorImpl<DISubprogram *>::const_iterator;
  using global_variable_expression_iterator =
      SmallVectorImpl<DIGlobalVariableExpression *>::const_iterator;
  using type_iterator = SmallVectorImpl<DIType *>::const_iterator;
  using scope_iterator = SmallVectorImpl<DIScope *>::const_iterator;

  iterator_range<compile_unit_iterator> compile_units() const {
    return make_range(CUs.begin(), CUs.end());
  }

  iterator_range<subprogram_iterator> subprograms() const {
    return make_range(SPs.begin(), SPs.end());
  }

  iterator_range<global_variable_expression_iterator> global_variables() const {
    return make_range(GVs.begin(), GVs.end());
  }

  iterator_range<type_iterator> types() const {
    return make_range(TYs.begin(), TYs.end());
  }

  iterator_range<scope_iterator> scopes() const {
    return make_range(Scopes.begin(), Scopes.end());
  }

  unsigned compile_unit_count() const { return CUs.size(); }
  unsigned global_variable_count() const { return GVs.size(); }
  unsigned subprogram_count() const { return SPs.size(); }
  unsigned type_count() const { return TYs.size(); }
  unsigned scope_count() const { return Scopes.size(); }

private:
  SmallVector<DICompileUnit *, 8> CUs;
  SmallVector<DISubprogram *, 8> SPs;
  SmallVector<DIGlobalVariableExpression *, 8> GVs;
  SmallVector<DIType *, 8> TYs;
  SmallVector<DIScope *, 8> Scopes;
  SmallPtrSet<const MDNode *, 32> NodesSeen;
};

} // end namespace llvm

#endif // LLVM_IR_DEBUGINFO_H
