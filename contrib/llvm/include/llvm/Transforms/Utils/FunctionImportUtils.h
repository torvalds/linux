//===- FunctionImportUtils.h - Importing support utilities -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the FunctionImportGlobalProcessing class which is used
// to perform the necessary global value handling for function importing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_FUNCTIONIMPORTUTILS_H
#define LLVM_TRANSFORMS_UTILS_FUNCTIONIMPORTUTILS_H

#include "llvm/ADT/SetVector.h"
#include "llvm/IR/ModuleSummaryIndex.h"

namespace llvm {
class Module;

/// Class to handle necessary GlobalValue changes required by ThinLTO
/// function importing, including linkage changes and any necessary renaming.
class FunctionImportGlobalProcessing {
  /// The Module which we are exporting or importing functions from.
  Module &M;

  /// Module summary index passed in for function importing/exporting handling.
  const ModuleSummaryIndex &ImportIndex;

  /// Globals to import from this module, all other functions will be
  /// imported as declarations instead of definitions.
  SetVector<GlobalValue *> *GlobalsToImport;

  /// Set to true if the given ModuleSummaryIndex contains any functions
  /// from this source module, in which case we must conservatively assume
  /// that any of its functions may be imported into another module
  /// as part of a different backend compilation process.
  bool HasExportedFunctions = false;

  /// Set of llvm.*used values, in order to validate that we don't try
  /// to promote any non-renamable values.
  SmallPtrSet<GlobalValue *, 8> Used;

  /// Keep track of any COMDATs that require renaming (because COMDAT
  /// leader was promoted and renamed). Maps from original COMDAT to one
  /// with new name.
  DenseMap<const Comdat *, Comdat *> RenamedComdats;

  /// Check if we should promote the given local value to global scope.
  bool shouldPromoteLocalToGlobal(const GlobalValue *SGV);

#ifndef NDEBUG
  /// Check if the given value is a local that can't be renamed (promoted).
  /// Only used in assertion checking, and disabled under NDEBUG since the Used
  /// set will not be populated.
  bool isNonRenamableLocal(const GlobalValue &GV) const;
#endif

  /// Helper methods to check if we are importing from or potentially
  /// exporting from the current source module.
  bool isPerformingImport() const { return GlobalsToImport != nullptr; }
  bool isModuleExporting() const { return HasExportedFunctions; }

  /// If we are importing from the source module, checks if we should
  /// import SGV as a definition, otherwise import as a declaration.
  bool doImportAsDefinition(const GlobalValue *SGV);

  /// Get the name for SGV that should be used in the linked destination
  /// module. Specifically, this handles the case where we need to rename
  /// a local that is being promoted to global scope, which it will always
  /// do when \p DoPromote is true (or when importing a local).
  std::string getName(const GlobalValue *SGV, bool DoPromote);

  /// Process globals so that they can be used in ThinLTO. This includes
  /// promoting local variables so that they can be reference externally by
  /// thin lto imported globals and converting strong external globals to
  /// available_externally.
  void processGlobalsForThinLTO();
  void processGlobalForThinLTO(GlobalValue &GV);

  /// Get the new linkage for SGV that should be used in the linked destination
  /// module. Specifically, for ThinLTO importing or exporting it may need
  /// to be adjusted. When \p DoPromote is true then we must adjust the
  /// linkage for a required promotion of a local to global scope.
  GlobalValue::LinkageTypes getLinkage(const GlobalValue *SGV, bool DoPromote);

public:
  FunctionImportGlobalProcessing(
      Module &M, const ModuleSummaryIndex &Index,
      SetVector<GlobalValue *> *GlobalsToImport = nullptr)
      : M(M), ImportIndex(Index), GlobalsToImport(GlobalsToImport) {
    // If we have a ModuleSummaryIndex but no function to import,
    // then this is the primary module being compiled in a ThinLTO
    // backend compilation, and we need to see if it has functions that
    // may be exported to another backend compilation.
    if (!GlobalsToImport)
      HasExportedFunctions = ImportIndex.hasExportedFunctions(M);

#ifndef NDEBUG
    // First collect those in the llvm.used set.
    collectUsedGlobalVariables(M, Used, /*CompilerUsed*/ false);
    // Next collect those in the llvm.compiler.used set.
    collectUsedGlobalVariables(M, Used, /*CompilerUsed*/ true);
#endif
  }

  bool run();

  static bool doImportAsDefinition(const GlobalValue *SGV,
                                   SetVector<GlobalValue *> *GlobalsToImport);
};

/// Perform in-place global value handling on the given Module for
/// exported local functions renamed and promoted for ThinLTO.
bool renameModuleForThinLTO(
    Module &M, const ModuleSummaryIndex &Index,
    SetVector<GlobalValue *> *GlobalsToImport = nullptr);

/// Compute synthetic function entry counts.
void computeSyntheticCounts(ModuleSummaryIndex &Index);

} // End llvm namespace

#endif
