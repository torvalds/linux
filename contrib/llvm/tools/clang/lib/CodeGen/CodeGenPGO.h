//===--- CodeGenPGO.h - PGO Instrumentation for LLVM CodeGen ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based profile-guided optimization
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENPGO_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENPGO_H

#include "CGBuilder.h"
#include "CodeGenModule.h"
#include "CodeGenTypes.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include <array>
#include <memory>

namespace clang {
namespace CodeGen {

/// Per-function PGO state.
class CodeGenPGO {
private:
  CodeGenModule &CGM;
  std::string FuncName;
  llvm::GlobalVariable *FuncNameVar;

  std::array <unsigned, llvm::IPVK_Last + 1> NumValueSites;
  unsigned NumRegionCounters;
  uint64_t FunctionHash;
  std::unique_ptr<llvm::DenseMap<const Stmt *, unsigned>> RegionCounterMap;
  std::unique_ptr<llvm::DenseMap<const Stmt *, uint64_t>> StmtCountMap;
  std::unique_ptr<llvm::InstrProfRecord> ProfRecord;
  std::vector<uint64_t> RegionCounts;
  uint64_t CurrentRegionCount;

public:
  CodeGenPGO(CodeGenModule &CGM)
      : CGM(CGM), NumValueSites({{0}}), NumRegionCounters(0), FunctionHash(0),
        CurrentRegionCount(0) {}

  /// Whether or not we have PGO region data for the current function. This is
  /// false both when we have no data at all and when our data has been
  /// discarded.
  bool haveRegionCounts() const { return !RegionCounts.empty(); }

  /// Return the counter value of the current region.
  uint64_t getCurrentRegionCount() const { return CurrentRegionCount; }

  /// Set the counter value for the current region. This is used to keep track
  /// of changes to the most recent counter from control flow and non-local
  /// exits.
  void setCurrentRegionCount(uint64_t Count) { CurrentRegionCount = Count; }

  /// Check if an execution count is known for a given statement. If so, return
  /// true and put the value in Count; else return false.
  Optional<uint64_t> getStmtCount(const Stmt *S) {
    if (!StmtCountMap)
      return None;
    auto I = StmtCountMap->find(S);
    if (I == StmtCountMap->end())
      return None;
    return I->second;
  }

  /// If the execution count for the current statement is known, record that
  /// as the current count.
  void setCurrentStmt(const Stmt *S) {
    if (auto Count = getStmtCount(S))
      setCurrentRegionCount(*Count);
  }

  /// Assign counters to regions and configure them for PGO of a given
  /// function. Does nothing if instrumentation is not enabled and either
  /// generates global variables or associates PGO data with each of the
  /// counters depending on whether we are generating or using instrumentation.
  void assignRegionCounters(GlobalDecl GD, llvm::Function *Fn);
  /// Emit a coverage mapping range with a counter zero
  /// for an unused declaration.
  void emitEmptyCounterMapping(const Decl *D, StringRef FuncName,
                               llvm::GlobalValue::LinkageTypes Linkage);
  // Insert instrumentation or attach profile metadata at value sites
  void valueProfile(CGBuilderTy &Builder, uint32_t ValueKind,
                    llvm::Instruction *ValueSite, llvm::Value *ValuePtr);
private:
  void setFuncName(llvm::Function *Fn);
  void setFuncName(StringRef Name, llvm::GlobalValue::LinkageTypes Linkage);
  void mapRegionCounters(const Decl *D);
  void computeRegionCounts(const Decl *D);
  void applyFunctionAttributes(llvm::IndexedInstrProfReader *PGOReader,
                               llvm::Function *Fn);
  void loadRegionCounts(llvm::IndexedInstrProfReader *PGOReader,
                        bool IsInMainFile);
  bool skipRegionMappingForDecl(const Decl *D);
  void emitCounterRegionMapping(const Decl *D);

public:
  void emitCounterIncrement(CGBuilderTy &Builder, const Stmt *S,
                            llvm::Value *StepV);

  /// Return the region count for the counter at the given index.
  uint64_t getRegionCount(const Stmt *S) {
    if (!RegionCounterMap)
      return 0;
    if (!haveRegionCounts())
      return 0;
    return RegionCounts[(*RegionCounterMap)[S]];
  }
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
